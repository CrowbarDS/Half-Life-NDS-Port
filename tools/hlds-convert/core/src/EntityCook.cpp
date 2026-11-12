#include "HlSound.hpp"
#include "HlSprite.hpp"
#include "hlds/EntityCook.hpp"
#include "HlWeaponBundle.hpp"
#include "hlds/MdlFile.hpp"

#include "HlMonsters.hpp"
#include "HlUi.hpp"

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hlds
{
    using namespace hldata;

    namespace
    {
        double numberKey(const Entity &entity, std::string_view key, double fallback = 0)
        {
            const std::string text(entity.value(key));
            return text.empty() ? fallback : std::atof(text.c_str());
        }

        /// How loud a mover's sounds are, of 127 (LogicRecord::pad; never 0,
        /// which is a map cooked before): `volume` of full, held to 0..1 as
        /// the engine holds it, and `fallback` for none or 0 (the game's
        /// Spawn and Precache).
        std::uint16_t moverVolumeOf(double volume, double fallback)
        {
            if (volume == 0)
                volume = fallback;
            return static_cast<std::uint16_t>(std::clamp(std::lround(std::clamp(volume, 0.0, 1.0) * 127), 1L, 127L));
        }

        /// Seconds to milliseconds; a negative time is "never" and stays -1.
        std::int32_t millis(double seconds)
        {
            return seconds < 0 ? -1 : static_cast<std::int32_t>(std::lround(seconds * 1000));
        }

        // The map's own spawnflags, per class (SDK dlls/doors.h, buttons.cpp,
        // triggers.cpp).
        constexpr int kDoorStartOpen = 1;
        constexpr int kDoorNoAutoReturn = 32;
        constexpr int kDoorUseOnly = 256;
        constexpr int kButtonDontMove = 1;
        constexpr int kButtonToggle = 32;
        constexpr int kButtonTouchOnly = 256;
        constexpr int kTriggerNoClients = 2;
        constexpr int kRelayFireOnce = 1;
        constexpr int kManagerThread = 1;

        /// Classes that have no logic record and are not "unsupported" either:
        /// they are handled elsewhere, or are data the cooker has already used.
        bool isAccountedFor(const std::string &cls)
        {
            static const char *const kClasses[] = {
                "worldspawn", "info_player_start", "info_player_deathmatch", "light", "light_spot", "light_environment",
                "func_wall", "func_illusionary", "func_ladder", "infodecal", "info_null",
                "info_target", "path_corner", "info_landmark", "path_track",
                "info_teleport_destination", "monster_scientist", "monster_barney", "monster_gman",
                "monster_sitting_scientist", "monster_scientist_dead", "monster_barney_dead",
                "monster_miniturret", "monster_sentry", "monster_turret",
                "monster_generic", "monster_furniture", "cycler", "monster_headcrab",
                "monster_zombie", "monster_barnacle", "info_node", "weapon_crowbar", "weapon_glock",
                "weapon_9mmhandgun", "weapon_shotgun", "ammo_glockclip", "ammo_9mmclip",
                "ammo_buckshot", "item_healthkit", "item_battery", "item_suit", "world_items",
                "ambient_generic", "trigger_cdaudio", "target_cdaudio", "env_sound",
                "monster_alien_slave", "monster_bullchicken", "monster_houndeye",
                "weapon_handgrenade", "env_bubbles", "weapon_9mmAR", "weapon_mp5", "weapon_357",
                "weapon_python", "ammo_9mmAR", "ammo_mp5clip", "ammo_9mmbox", "ammo_ARgrenades",
                "ammo_mp5grenades", "ammo_357", "monster_human_grunt", "monster_hgrunt_dead",
                "monster_tripmine", "weapon_crossbow", "ammo_crossbow", "weapon_satchel", "weapon_tripmine",
                "weapon_snark", "weapon_rpg", "ammo_rpgclip", "weapon_gauss", "ammo_gaussclip", "ammo_egonclip",
                "weapon_egon", "weapon_hornetgun", "weaponbox", "monster_cockroach", "monster_leech",
                "monster_alien_grunt", "monster_alien_controller", "monster_snark", "monster_osprey",
                "xen_hair", "xen_spore_small", "xen_spore_medium", "xen_spore_large", "xen_tree", "xen_plantlight",
                "item_antidote", "item_security", "item_airtank", "item_longjump",
                "monster_gargantua", "monster_babycrab", "monster_hevsuit_dead",
                "monster_human_assassin", "monster_ichthyosaur", "monster_rat",
                "func_tank", "func_tanklaser", "func_tankrocket", "func_tankmortar", "func_tankcontrols",
                "func_mortar_field", "momentary_rot_button", "momentary_door", "monster_tentacle",
                "monster_apache", "monster_bigmomma", "info_bigmomma", "monster_nihilanth",
                "monster_flyer_flock", "monster_grunt_repel", "func_traincontrols"};
            for (const char *name : kClasses)
            {
                if (cls == name)
                    return true;
            }
            return false;
        }
    } // namespace

    namespace
    {
        /// Whether a box reaches any leaf of the world that is not solid.
        /// The game sends nobody an entity that touches none (SV_LinkEdict
        /// finds it no leaf): it is never seen.
        bool boxTouchesOpen(const BspFile &bsp, int child, const double *mins, const double *maxs)
        {
            while (child >= 0)
            {
                const BspNode &node = bsp.nodes[child];
                const BspPlane &plane = bsp.planes[node.plane];
                // The box's nearest and furthest corners along the plane.
                double near = -plane.dist, far = -plane.dist;
                for (int a = 0; a < 3; ++a)
                {
                    const double lo = plane.normal[a] * mins[a], hi = plane.normal[a] * maxs[a];
                    near += lo < hi ? lo : hi;
                    far += lo < hi ? hi : lo;
                }
                if (near >= 0)
                    child = node.children[0];
                else if (far < 0)
                    child = node.children[1];
                else
                {
                    if (boxTouchesOpen(bsp, node.children[0], mins, maxs))
                        return true;
                    child = node.children[1];
                }
            }
            const int leaf = -(child + 1);
            return leaf > 0 && leaf < static_cast<int>(bsp.leaves.size()) && bsp.leaves[leaf].contents != kContentsSolid;
        }

        /// For a trigger volume whose brush is not one plain box: its model,
        /// for the game's touch test -- the player's origin inside the
        /// brush's own hull, not just their box in its bounds (a slab lying
        /// along a slope, as down c1a1c's lift shaft, has bounds that fill
        /// the whole shaft). 0 for a box, where the bounds say it all.
        std::uint16_t shapedVolume(const BspFile &bsp, const BspModel *model)
        {
            if (model == nullptr || model->origin[0] != 0 || model->origin[1] != 0 || model->origin[2] != 0)
                return 0;
            const int index = static_cast<int>(model - bsp.models.data());
            // A box is six axial clip nodes; anything else is a shape.
            int nodes = 0;
            bool axial = true;
            std::vector<std::int32_t> open{bsp.clipHullRoot(index, 1)};
            while (!open.empty() && nodes <= 6)
            {
                const std::int32_t at = open.back();
                open.pop_back();
                if (at < 0 || at >= static_cast<std::int32_t>(bsp.clipNodes.size()))
                    continue;
                ++nodes;
                const BspClipNode &node = bsp.clipNodes[static_cast<std::size_t>(at)];
                axial = axial && bsp.planes[static_cast<std::size_t>(node.plane)].type < 3;
                open.push_back(node.children[0]);
                open.push_back(node.children[1]);
            }
            return nodes <= 6 && axial ? 0 : static_cast<std::uint16_t>(index);
        }

        std::string lowered(std::string_view text)
        {
            std::string out(text);
            for (char &c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        /// What an entity is as an actor, if it is one.
        struct ActorInfo
        {
            bool is = false;
            ActorKind kind = ActorKind::Generic;
            std::string file;              ///< the model, as the game names it
            std::vector<std::string> clips; ///< what it needs without being told
            std::string startClip;         ///< the clip it starts in; empty: the model's first
            bool firstSequence = false;
            bool activities = false; ///< every sequence with an activity (HlAi.hpp chooses by them)
            std::vector<std::uint8_t> dsActivities; ///< ... of which a DS keeps these (ModelNeed)
            bool solid = true;
            ItemKind item = ItemKind::None;
            /// For what a monstermaker makes: how many places are kept for
            /// its children (they are not there when the map starts).
            int spare = 0;
            XenPlant xen = XenPlant::Hair; ///< for an ActorKind::Xen: which
        };

        /// What lies about to be picked up, by class.
        struct ItemClass
        {
            const char *cls;
            ItemKind kind;
            const char *file;
        };
        const ItemClass kItemClasses[] = {
            {"weapon_crowbar", ItemKind::Crowbar, "models/w_crowbar.mdl"},
            {"weapon_glock", ItemKind::Glock, "models/w_9mmhandgun.mdl"},
            {"weapon_9mmhandgun", ItemKind::Glock, "models/w_9mmhandgun.mdl"},
            {"weapon_shotgun", ItemKind::Shotgun, "models/w_shotgun.mdl"},
            {"ammo_glockclip", ItemKind::GlockClip, "models/w_9mmclip.mdl"},
            {"ammo_9mmclip", ItemKind::GlockClip, "models/w_9mmclip.mdl"},
            {"ammo_buckshot", ItemKind::Buckshot, "models/w_shotbox.mdl"},
            {"item_healthkit", ItemKind::HealthKit, "models/w_medkit.mdl"},
            {"item_battery", ItemKind::Battery, "models/w_battery.mdl"},
            {"item_suit", ItemKind::Suit, "models/w_suit.mdl"},
            {"weapon_handgrenade", ItemKind::Grenades, "models/w_grenade.mdl"},
            {"weapon_9mmar", ItemKind::Mp5, "models/w_9mmar.mdl"},
            {"weapon_mp5", ItemKind::Mp5, "models/w_9mmar.mdl"},
            {"weapon_357", ItemKind::Python, "models/w_357.mdl"},
            {"weapon_python", ItemKind::Python, "models/w_357.mdl"},
            {"ammo_9mmar", ItemKind::Mp5Clip, "models/w_9mmarclip.mdl"},
            {"ammo_mp5clip", ItemKind::Mp5Clip, "models/w_9mmarclip.mdl"},
            {"ammo_9mmbox", ItemKind::AmmoBox, "models/w_chainammo.mdl"},
            {"ammo_argrenades", ItemKind::ArGrenades, "models/w_argrenade.mdl"},
            {"ammo_mp5grenades", ItemKind::ArGrenades, "models/w_argrenade.mdl"},
            {"ammo_357", ItemKind::Ammo357, "models/w_357ammobox.mdl"},
            {"weapon_crossbow", ItemKind::Crossbow, "models/w_crossbow.mdl"},
            {"ammo_crossbow", ItemKind::Bolts, "models/w_crossbow_clip.mdl"},
            {"weapon_satchel", ItemKind::Satchel, "models/w_satchel.mdl"},
            {"weapon_tripmine", ItemKind::Tripmine, "models/v_tripmine.mdl"},
            {"weapon_snark", ItemKind::Snark, "models/w_sqknest.mdl"},
            {"weapon_rpg", ItemKind::Rpg, "models/w_rpg.mdl"},
            {"ammo_rpgclip", ItemKind::RpgClip, "models/w_rpgammo.mdl"},
            {"weapon_gauss", ItemKind::Gauss, "models/w_gauss.mdl"},
            {"ammo_gaussclip", ItemKind::GaussClip, "models/w_gaussammo.mdl"},
            {"ammo_egonclip", ItemKind::GaussClip, "models/w_gaussammo.mdl"},
            {"weapon_egon", ItemKind::Egon, "models/w_egon.mdl"},
            {"weapon_hornetgun", ItemKind::Hornetgun, "models/w_hgun.mdl"},
            {"weaponbox", ItemKind::Weaponbox, "models/w_weaponbox.mdl"},
            {"item_antidote", ItemKind::Antidote, "models/w_antidote.mdl"},
            {"item_security", ItemKind::Security, "models/w_security.mdl"},
            {"item_airtank", ItemKind::Airtank, "models/w_oxygen.mdl"},
            {"item_longjump", ItemKind::LongJump, "models/w_longjump.mdl"},
        };

        /// What a func_breakable or func_pushable leaves when it breaks: its
        /// "spawnobject", an index into the game's own list
        /// (CBreakable::pSpawnObjects). Null for none, or one not lying about
        /// in this port.
        const ItemClass *spawnObjectOf(const Entity &entity)
        {
            static const char *const kSpawnObjects[] = {
                nullptr,           "item_battery",   "item_healthkit",   "weapon_9mmhandgun", "ammo_9mmclip",
                "weapon_9mmar",    "ammo_9mmar",     "ammo_argrenades",  "weapon_shotgun",    "ammo_buckshot",
                "weapon_crossbow", "ammo_crossbow",  "weapon_357",       "ammo_357",          "weapon_rpg",
                "ammo_rpgclip",    "ammo_gaussclip", "weapon_handgrenade", "weapon_tripmine", "weapon_satchel",
                "weapon_snark",    "weapon_hornetgun"};
            const std::string cls = lowered(entity.className());
            if (cls != "func_breakable" && cls != "func_pushable")
                return nullptr;
            const long object = std::lround(numberKey(entity, "spawnobject"));
            if (object <= 0 || object >= static_cast<long>(std::size(kSpawnObjects)))
                return nullptr;
            for (const ItemClass &item : kItemClasses)
            {
                if (std::string(item.cls) == kSpawnObjects[object])
                    return &item;
            }
            return nullptr;
        }

        /// One of the people (talkmonster.cpp's): given chosen clips, not
        /// every one their model has.
        bool isPersonKind(ActorKind kind)
        {
            return kind == ActorKind::Scientist || kind == ActorKind::Guard || kind == ActorKind::Sitter ||
                   kind == ActorKind::Gman || kind == ActorKind::Generic;
        }

        ActorInfo actorOf(const Entity &entity)
        {
            ActorInfo info;
            std::string cls(entity.className());
            const int pose = static_cast<int>(numberKey(entity, "pose"));
            int spare = 0;
            if (cls == "monstermaker")
            {
                // It is what it makes, a few times over: as many as may be
                // alive at once, and never more than three.
                cls = lowered(entity.value("monstertype"));
                const int count = static_cast<int>(numberKey(entity, "monstercount"));
                const int alive = static_cast<int>(numberKey(entity, "m_imaxlivechildren"));
                spare = std::clamp(alive > 0 ? alive : (count > 0 ? count : 2), 1, 3);
                if (count > 0)
                    spare = std::min(spare, count);
            }
            // A soldier who comes down a rope when told (CHGruntRepel): a
            // soldier, with his rope's clips (below), not there until then.
            const bool repel = cls == "monster_grunt_repel";
            if (repel)
                cls = "monster_human_grunt";
            // The campaign's suit is not an item_suit: it is a "world_items"
            // of type 45 (44 is a battery; the antidote and the security
            // card, 42 and 43, were never finished in the game either).
            if (cls == "world_items")
            {
                const int type = static_cast<int>(numberKey(entity, "type"));
                cls = type == 45 ? "item_suit" : (type == 44 ? "item_battery" : "");
            }
            for (const ItemClass &item : kItemClasses)
            {
                // (The maps write some of these with capitals: weapon_9mmAR.)
                if (lowered(cls) != item.cls)
                    continue;
                info = {true, ActorKind::Item, item.file, {"idle"}, "idle"};
                // (A tripmine lying about is the held model's mine alone, as
                // it lies on the ground: below.)
                if (item.kind == ItemKind::Tripmine)
                {
                    info.clips = {"ground"};
                    info.startClip = "ground";
                }
                info.solid = false;
                info.item = item.kind;
                return info;
            }
            if (cls == "monster_headcrab")
            {
                info = {true, ActorKind::Headcrab, "models/headcrab.mdl",
                        {"idle1", "run", "jump", "flinch", "dieback"}, "idle1"};
            }
            else if (cls == "monster_zombie")
            {
                info = {true, ActorKind::Zombie, "models/zombie.mdl",
                        {"idle1", "walk", "attack1", "attack2", "flinchsmall", "diesimple"}, "idle1"};
            }
            else if (cls == "monster_barnacle")
            {
                info = {true, ActorKind::Barnacle, "models/barnacle.mdl",
                        {"idle1", "attack1", "chewcycle", "flinch_sm", "death"}, "idle1"};
            }
            else if (cls == "monster_alien_slave")
            {
                info = {true, ActorKind::Slave, "models/islave.mdl",
                        {"idle1", "walk1", "attack1", "zapattack1", "flinch2", "diesimple"}, "idle1"};
            }
            else if (cls == "monster_bullchicken")
            {
                info = {true, ActorKind::Bullsquid, "models/bullsquid.mdl",
                        {"idle", "run", "range", "bite", "whip", "flinchs", "die1"}, "idle"};
            }
            else if (cls == "monster_houndeye")
            {
                info = {true, ActorKind::Houndeye, "models/houndeye.mdl",
                        {"idle1", "run", "attack", "flinch_small", "die", "madidle"}, "idle1"};
            }
            // Round 3 (HlAliens.hpp): what they play by name, besides what
            // their activities give them (kDsActivities, below).
            else if (cls == "monster_cockroach")
            {
                info = {true, ActorKind::Cockroach, "models/roach.mdl", {}, ""};
                info.firstSequence = true;
                info.solid = false; // (stepped on, not walked into)
            }
            else if (cls == "monster_rat")
            {
                info = {true, ActorKind::Rat, "models/bigrat.mdl", {}, "idle7"}; // (the heaviest idle: a DS keeps only it)
            }
            else if (cls == "monster_leech")
            {
                info = {true, ActorKind::Leech, "models/leech.mdl", {"swim", "attack", "death"}, "swim"};
                info.solid = false;
            }
            else if (cls == "monster_alien_grunt")
            {
                info = {true, ActorKind::AlienGrunt, "models/agrunt.mdl", {"idle1", "walk", "run", "diesimple"},
                        "idle1"};
            }
            else if (cls == "monster_alien_controller")
            {
                info = {true, ActorKind::Controller, "models/controller.mdl",
                        {"float", "forward", "backward", "up", "down", "fall", "die1"}, "float"};
            }
            else if (cls == "monster_snark")
            {
                info = {true, ActorKind::Snark, "models/w_squeak.mdl", {"run", "jump"}, "run"};
                info.solid = false;
            }
            else if (cls == "monster_miniturret" || cls == "monster_sentry" || cls == "monster_turret")
            {
                info = {true,
                        cls == "monster_sentry" ? ActorKind::Sentry
                                                : (cls == "monster_turret" ? ActorKind::Turret : ActorKind::Miniturret),
                        cls == "monster_sentry" ? "models/sentry.mdl"
                                                : (cls == "monster_turret" ? "models/turret.mdl" : "models/miniturret.mdl"),
                        {"idle_off", "fire", "spin", "deploy", "retire", "die"}, "idle_off"};
            }
            else if (cls == "monster_human_grunt")
            {
                // What he can be seen to do depends on what he carries (the
                // game's bits: none set is an MP5 and grenades).
                int weapons = static_cast<int>(numberKey(entity, "weapons"));
                weapons = weapons == 0 || spare != 0 ? 3 : weapons;
                std::vector<std::string> clips = {"idle1", "combatidle", "crouching_idle", "run", "limpingrun",
                                                  "smflinch", "frontkick", "diesimple", "dieback1", "diebackwards",
                                                  "dieforward", "dieheadshot", "diegutshot", "cower",
                                                  "victorydance", "advance_signal", "flank_signal", "drop_grenade",
                                                  "barnacled1", "barnacled2", "barnacled3", "barnacled4"};
                if (weapons & 8)
                    clips.insert(clips.end(), {"standing_shotgun", "crouching_shotgun", "reload_shotgun"});
                else
                    clips.insert(clips.end(), {"standing_mp5", "crouching_mp5", "reload_mp5"});
                if (weapons & 2)
                    clips.push_back("throwgrenade");
                if (weapons & 4)
                    clips.push_back("launchgrenade");
                if (repel)
                    clips.insert(clips.end(), {"repel_repel", "repel_shoot", "repel_land"});
                info = {true, ActorKind::Grunt, "models/hgrunt.mdl", clips, "idle1"};
            }
            else if (cls == "monster_tripmine")
            {
                info = {true, ActorKind::Tripmine, "models/v_tripmine.mdl", {"world"}, "world"};
                info.solid = false;
            }
            // Round 4 (HlMilitary.hpp).
            else if (cls == "monster_osprey")
            {
                info = {true, ActorKind::Osprey, "models/osprey.mdl", {"rotor"}, "rotor"};
                info.solid = false; // (in the sky; and its box would be its rotors')
            }
            else if (cls == "monster_gargantua")
            {
                info = {true, ActorKind::Gargantua, "models/garg.mdl", {"idle1", "walk", "run", "die"}, "idle1"};
            }
            else if (cls == "monster_human_assassin")
            {
                // (Its flight is no activity's: by name, as its code plays it.)
                info = {true, ActorKind::Assassin, "models/hassassin.mdl",
                        {"idle1", "run", "shoot", "jump", "fly_up", "fly_attack", "fly_down", "landfromjump",
                         "grenadethrow", "die_simple"},
                        "idle1"};
            }
            else if (cls == "monster_flyer_flock")
            {
                // (Its boids: cooked as many as it has, below.)
                info = {true, ActorKind::Boid, "models/boid.mdl", {"idle"}, "idle"};
                info.solid = false;
            }
            else if (cls == "monster_nihilanth")
            {
                // (Every clip its think plays by name.)
                info = {true, ActorKind::Nihilanth, "models/nihilanth.mdl",
                        {"float", "attack1", "attack2", "recharge", "float_open", "attack1_open", "attack2_open",
                         "flinch1", "die1", "walk_u", "walk_d", "walk_r", "walk_l"},
                        "float"};
            }
            else if (cls == "monster_bigmomma")
            {
                // (What her path's nodes have her play are added by name:
                // collectModelNeeds.)
                info = {true, ActorKind::BigMomma, "models/big_mom.mdl",
                        {"idle1", "walk", "run", "death", "claw", "spawn", "mortar", "flinch", "jump"}, "idle1"};
            }
            else if (cls == "monster_apache")
            {
                info = {true, ActorKind::Apache, "models/apache.mdl", {"idle"}, "idle"};
            }
            else if (cls == "monster_tentacle")
            {
                // Every clip its think finds by the model's own graph of them
                // (HlAliens.hpp's tentacle::kSeqs), and the map's scenes'.
                info = {true, ActorKind::Tentacle, "models/tentacle2.mdl",
                        {"pit_idle", "rise_to_temp1", "temp1_to_floor", "floor_idle", "floor_fidget_pissed",
                         "floor_fidget_smallrise", "floor_fidget_wave", "floor_strike", "floor_tap", "floor_rotate",
                         "floor_rear", "floor_rear_idle", "floor_to_lev1", "lev1_idle", "lev1_fidget_claw",
                         "lev1_fidget_shake", "lev1_fidget_snap", "lev1_strike", "lev1_tap", "lev1_rotate",
                         "lev1_rear", "lev1_rear_idle", "lev1_to_lev2", "lev2_idle", "lev2_fidget_shake",
                         "lev2_fidget_tut", "lev2_strike", "lev2_tap", "lev2_rotate", "lev2_rear",
                         "lev2_rear_idle", "lev2_to_lev3", "lev3_idle", "lev3_fidget_shake", "lev3_fidget_side",
                         "lev3_strike", "lev3_tap", "lev3_rotate", "lev3_rear", "lev3_rear_idle",
                         "lev3_to_engine", "engine_idle", "engine_sway", "engine_swat", "engine_bob",
                         "engine_death1", "engine_death2", "engine_death3"},
                        "pit_idle"};
            }
            else if (cls == "monster_ichthyosaur")
            {
                info = {true, ActorKind::Ichthyosaur, "models/icky.mdl", {"swim", "thrust", "bite_r", "bite_l",
                                                                          "die1", "bellyup"},
                        "swim"};
            }
            else if (cls == "monster_babycrab")
            {
                info = {true, ActorKind::BabyCrab, "models/baby_headcrab.mdl",
                        {"idle1", "run", "jump", "flinch", "dieback"}, "idle1"};
            }
            else if (spare != 0)
            {
                return info; // a maker of something there is not
            }
            else if (cls == "monster_hevsuit_dead")
            {
                // (The game's is player.mdl; the suit it lies in is the
                // same as the one a player in a game between consoles wears,
                // which is not called "player" here: kPlayerModelName.)
                static const char *const kPoses[] = {"deadback", "deadsitting", "deadstomach", "deadtable"};
                const char *clip = kPoses[std::clamp(pose, 0, 3)];
                info = {true, ActorKind::Pose, "models/player/gordon/gordon.mdl", {clip}, clip};
                info.solid = false;
            }
            else if (cls == "monster_hgrunt_dead")
            {
                static const char *const kPoses[] = {"deadstomach", "deadside", "deadsitting"};
                const char *clip = kPoses[std::clamp(pose, 0, 2)];
                info = {true, ActorKind::Pose, "models/hgrunt.mdl", {clip}, clip};
                info.solid = false;
            }
            else if (cls == "monster_scientist")
            {
                // (What his schedules ask for, by what each clip is for:
                // standing, going, going scared, flinching, crouching in
                // fear, his panic, his needle and its shot, his deaths and
                // the barnacle's -- not every one of his model's, which
                // would be three times the memory.)
                info = {true, ActorKind::Scientist, "models/scientist.mdl",
                        {"idle1", "walk", "walk_scared", "run", "run1", "flinch", "flinch1", "crouchstand",
                         "crouch_idle", "panic", "pull_needle", "return_needle", "give_shot", "diesimple",
                         "dieforward1", "diebackward", "headshot", "gutshot", "barnacled1", "barnacled2",
                         "barnacled3", "barnacled4"},
                        "idle1"};
            }
            else if (cls == "monster_barney")
            {
                info = {true, ActorKind::Guard, "models/barney.mdl",
                        {"idle1", "walk", "run", "diesimple", "diecrump", "dieforward", "diebackward", "diegutshot",
                         "shootgun", "shootgun2", "draw", "disarm", "smlflinch", "barnaclehit", "barnaclepull",
                         "barnaclecrunch", "barnaclechew"},
                        "idle1"};
            }
            else if (cls == "monster_gman")
            {
                info = {true, ActorKind::Gman, "models/gman.mdl", {"idle01", "walk"}, "idle01"};
            }
            else if (cls == "monster_sitting_scientist")
            {
                // (The five ways he sits, which he goes between: HlActors.hpp.)
                info = {true, ActorKind::Sitter, "models/scientist.mdl",
                        {"sitlookleft", "sitlookright", "sitscared", "sitting2", "sitting3", "diesimple"},
                        "sitlookleft"};
            }
            else if (cls == "monster_scientist_dead")
            {
                static const char *const kPoses[] = {"lying_on_back", "lying_on_stomach", "dead_sitting",
                                                     "dead_hang",     "dead_table1",      "dead_table2",
                                                     "dead_table3"};
                const char *clip = kPoses[std::clamp(pose, 0, 6)];
                info = {true, ActorKind::Pose, "models/scientist.mdl", {clip}, clip};
                info.solid = false;
            }
            else if (cls == "monster_barney_dead")
            {
                static const char *const kPoses[] = {"lying_on_back", "lying_on_side", "lying_on_stomach"};
                const char *clip = kPoses[std::clamp(pose, 0, 2)];
                info = {true, ActorKind::Pose, "models/barney.mdl", {clip}, clip};
                info.solid = false;
            }
            else if (cls.rfind("xen_", 0) == 0)
            {
                // The plants of Xen: their own models, as the game gives
                // them. A light and a tree do things, by what their clips
                // are for; the rest only sway.
                struct Plant
                {
                    const char *cls;
                    XenPlant kind;
                    const char *file;
                    bool solid;
                };
                static const Plant kPlants[] = {
                    {"xen_hair", XenPlant::Hair, "models/hair.mdl", false},
                    {"xen_spore_small", XenPlant::SporeSmall, "models/fungus(small).mdl", true},
                    {"xen_spore_medium", XenPlant::SporeMedium, "models/fungus.mdl", true},
                    {"xen_spore_large", XenPlant::SporeLarge, "models/fungus(large).mdl", true},
                    {"xen_tree", XenPlant::Tree, "models/tree.mdl", true},
                    {"xen_plantlight", XenPlant::Light, "models/light.mdl", false},
                };
                for (const Plant &plant : kPlants)
                {
                    if (cls != plant.cls)
                        continue;
                    info = {true, ActorKind::Xen, plant.file, {}, ""};
                    info.solid = plant.solid;
                    info.xen = plant.kind;
                    if (plant.kind == XenPlant::Tree || plant.kind == XenPlant::Light)
                    {
                        info.activities = true;
                        if (plant.kind == XenPlant::Tree)
                            info.dsActivities = {kActIdle, kActMeleeAttack1};
                        else
                            info.dsActivities = {kActIdle, kActCrouch, kActCrouchIdle, kActStand};
                    }
                    else
                    {
                        info.firstSequence = true;
                    }
                }
            }
            else if ((cls == "monster_generic" || cls == "monster_furniture" || cls == "cycler") &&
                     !entity.value("model").empty())
            {
                info.is = true;
                info.kind = cls == "cycler" ? ActorKind::Cycler
                                            : (cls == "monster_furniture" ? ActorKind::Furniture
                                                                          : ActorKind::Generic);
                info.file = lowered(entity.value("model"));
                info.firstSequence = true;
                // A monster_generic with flag 4 is scenery one walks through.
                info.solid = !(cls == "monster_generic" &&
                               (static_cast<int>(numberKey(entity, "spawnflags")) & 4));
            }
            // (The monsters that think as the game's do choose their clips by
            // what they are for: on a DSi all of those; on a DS what their
            // fights need -- not the hound's sleep or the squid's meal, nor
            // a death or a flinch for each place a blow lands.)
            // (People are given the clips their schedules ask for, above: the
            // DS has those; a DSi has every one with an activity besides.)
            static const std::uint8_t kDsActivities[] = {
                kActIdle,          kActWalk,          kActRun,           kActRangeAttack1, kActRangeAttack2,
                kActMeleeAttack1,  kActMeleeAttack2,  kActSpecialAttack1, kActSpecialAttack2, kActSmallFlinch,
                kActDieSimple,     kActHop,           kActLeap,          kActIdleAngry,    kActSignal1,
                kActSignal2,       kActSignal3,       kActCower,         kActWalkHurt,     kActRunHurt,
                kActBarnacleHit,   kActBarnaclePull,  kActBarnacleChomp, kActBarnacleChew, kActArm,
                kActDisarm};
            // (Not the tentacle: what it plays is no activity the game's
            // code knows; it is by name, all of it.)
            if (info.is && actorThinks(static_cast<std::uint8_t>(info.kind)) && info.kind != ActorKind::Generic &&
                info.kind != ActorKind::Tentacle && info.kind != ActorKind::Nihilanth)
            {
                info.activities = true;
                if (!isPersonKind(info.kind))
                    info.dsActivities.assign(std::begin(kDsActivities), std::end(kDsActivities));
            }
            // A monster_generic keeps what it stands about in (the game's
            // ACT_IDLE): its first sequence may be anything -- the
            // hologram's is a walk.
            if (info.is && info.kind == ActorKind::Generic)
                info.dsActivities.assign(1, kActIdle);
            // Only models, not sprites or brush models given by name.
            if (info.is && (info.file.size() < 5 || info.file.substr(info.file.size() - 4) != ".mdl"))
                info.is = false;
            info.spare = spare;
            return info;
        }

        std::string modelNameOf(const std::string &file)
        {
            const std::size_t slash = file.find_last_of("/\\");
            const std::string base = slash == std::string::npos ? file : file.substr(slash + 1);
            return base.substr(0, base.size() - 4);
        }

        /// The class name a scripted sequence may give in place of an
        /// actor's name, as the kind of actor it means; -1 if it is not one.
        int kindOfClass(const std::string &cls)
        {
            if (cls == "monster_scientist")
                return static_cast<int>(ActorKind::Scientist);
            if (cls == "monster_barney")
                return static_cast<int>(ActorKind::Guard);
            if (cls == "monster_gman")
                return static_cast<int>(ActorKind::Gman);
            if (cls == "monster_generic")
                return static_cast<int>(ActorKind::Generic);
            if (cls == "monster_furniture")
                return static_cast<int>(ActorKind::Furniture);
            if (cls == "monster_headcrab")
                return static_cast<int>(ActorKind::Headcrab);
            if (cls == "monster_zombie")
                return static_cast<int>(ActorKind::Zombie);
            if (cls == "monster_barnacle")
                return static_cast<int>(ActorKind::Barnacle);
            if (cls == "monster_alien_slave")
                return static_cast<int>(ActorKind::Slave);
            if (cls == "monster_bullchicken")
                return static_cast<int>(ActorKind::Bullsquid);
            if (cls == "monster_houndeye")
                return static_cast<int>(ActorKind::Houndeye);
            if (cls == "monster_human_grunt")
                return static_cast<int>(ActorKind::Grunt);
            if (cls == "monster_gargantua")
                return static_cast<int>(ActorKind::Gargantua);
            if (cls == "monster_human_assassin")
                return static_cast<int>(ActorKind::Assassin);
            if (cls == "monster_ichthyosaur")
                return static_cast<int>(ActorKind::Ichthyosaur);
            return -1;
        }

        bool isScript(const std::string &cls)
        {
            return cls == "scripted_sequence" || cls == "aiscripted_sequence";
        }

        void addActivities(ModelNeed &need, const std::vector<std::uint8_t> &activities)
        {
            for (std::uint8_t activity : activities)
            {
                if (std::find(need.dsActivities.begin(), need.dsActivities.end(), activity) == need.dsActivities.end())
                    need.dsActivities.push_back(activity);
            }
        }
    } // namespace

    namespace
    {
        /// What an env_shooter throws, as the game names it, tidied (the
        /// maps write it with either slash and any case).
        std::string shootModel(const Entity &entity)
        {
            std::string file = lowered(entity.value("shootmodel"));
            for (char &c : file)
                c = c == '\\' ? '/' : c;
            while (!file.empty() && file[0] == '/')
                file.erase(0, 1);
            return file;
        }
    } // namespace

    void modelClips(const MdlFile &mdl, const ModelNeed &need, std::vector<std::string> &ds,
                    std::vector<std::string> &dsi)
    {
        ds.clear();
        if (need.firstSequence && !mdl.sequences.empty())
            ds.push_back(mdl.sequences[0].label);
        ds.insert(ds.end(), need.clips.begin(), need.clips.end());
        // (A monster that chooses by what clips are for: on a DSi every one
        // that is for something; on a DS, for what it keeps of those, the
        // heaviest -- where none of its own clips is for that already.
        // People keep their own on a DS.)
        auto held = [&](const std::vector<std::string> &list, const std::string &label)
        {
            return std::find_if(list.begin(), list.end(), [&](const std::string &clip)
                                { return lowered(clip) == lowered(label); }) != list.end();
        };
        // (A sequence asked for by its place, "@n", where another has its
        // label: a roach's two are both "run" -- ModelCook.cpp.)
        auto nameOf = [&](const MdlSequence &seq)
        {
            int same = 0;
            for (const MdlSequence &other : mdl.sequences)
                same += lowered(other.label) == lowered(seq.label) ? 1 : 0;
            return same > 1 ? "@" + std::to_string(&seq - mdl.sequences.data()) : seq.label;
        };
        for (std::uint8_t activity : need.dsActivities)
        {
            const MdlSequence *heaviest = nullptr;
            bool has = false;
            for (const MdlSequence &seq : mdl.sequences)
            {
                if (seq.activity != activity)
                    continue;
                has = has || held(ds, nameOf(seq));
                if (heaviest == nullptr || seq.actWeight > heaviest->actWeight)
                    heaviest = &seq;
            }
            if (heaviest != nullptr && !has)
                ds.push_back(nameOf(*heaviest));
        }
        dsi = ds;
        for (const MdlSequence &seq : mdl.sequences)
        {
            if (need.activities && seq.activity != 0 && !held(dsi, nameOf(seq)))
                dsi.push_back(nameOf(seq));
        }
    }

    bool isMultiplayerMap(const std::vector<Entity> &entities)
    {
        for (const Entity &entity : entities)
        {
            if (entity.className() == "info_player_deathmatch")
                return true;
        }
        return false;
    }

    int standInEntities(std::vector<Entity> &entities)
    {
        if (!isMultiplayerMap(entities))
            return 0;
        // What stands in for what. As near in kind as the port's six
        // weapons allow: the heavy guns are the .357, the fast ones the
        // MP5, the crossbow the shotgun, and everything thrown or laid is
        // a hand grenade. Each line goes when its weapon comes.
        // (Every weapon is the port's now: only the long jump stands in.)
        static const char *const kStandIns[][2] = {
            {"item_longjump", "item_battery"},
        };
        int count = 0;
        for (Entity &entity : entities)
        {
            const std::string cls = lowered(entity.className());
            for (const auto &pair : kStandIns)
            {
                if (cls != pair[0])
                    continue;
                for (auto &keyValue : entity.pairs)
                {
                    if (keyValue.first == "classname")
                    {
                        keyValue.second = pair[1];
                        break;
                    }
                }
                ++count;
                break;
            }
        }
        return count;
    }

    void addStartDark(std::vector<Entity> &entities)
    {
        bool dark = false;
        for (const Entity &entity : entities)
            dark = dark || (entity.className() == "worldspawn" && numberKey(entity, "startdark") != 0);
        if (!dark)
            return;
        // A map that begins in the dark (worldspawn's "startdark": the
        // game's v_dark) is black as it starts, and the picture comes up
        // out of it -- held four and a half seconds, then a second and a
        // half (the client's fade with the game's title): a fade from black
        // that holds first, set off once as the map begins.
        Entity fade;
        fade.pairs = {{"classname", "env_fade"},  {"targetname", "__startdark"}, {"duration", "1.5"},
                      {"holdtime", "4.5"},       {"renderamt", "255"},          {"spawnflags", "1"},
                      {"_holdfirst", "1"}};
        Entity start;
        start.pairs = {{"classname", "trigger_auto"}, {"target", "__startdark"}, {"spawnflags", "1"}};
        entities.push_back(fade);
        entities.push_back(start);
    }

    void addStartMusic(std::vector<Entity> &entities)
    {
        int track = 0;
        for (const Entity &entity : entities)
        {
            if (entity.className() == "worldspawn")
                track = static_cast<int>(numberKey(entity, "sounds"));
        }
        // The track a map begins with (worldspawn's "sounds": the game's CD
        // track, started as the map is: the tram ride's music under the
        // announcer, c0a0's). 0 and 1 start nothing: none, and the disc's
        // program -- c0a0b's 0 does not stop the ride's.
        if (track < 2 || track > 99)
            return;
        Entity music;
        music.pairs = {{"classname", "target_cdaudio"}, {"targetname", "__startmusic"},
                       {"health", std::to_string(track)}, {"_untouched", "1"}};
        Entity start;
        start.pairs = {{"classname", "trigger_auto"}, {"target", "__startmusic"}, {"spawnflags", "1"}};
        entities.push_back(music);
        entities.push_back(start);
    }

    void addCasingNeeds(bool playerArmed, std::vector<ModelNeed> &needs)
    {
        bool brass = playerArmed;
        for (const ModelNeed &need : needs)
            brass = brass || need.name == "hgrunt" || need.name == "hassassin";
        if (!brass)
            return;
        for (const char *casing : {"shell", "shotgunshell"})
        {
            bool had = false;
            for (ModelNeed &need : needs)
            {
                if (need.name == casing)
                {
                    need.firstSequence = true;
                    had = true;
                }
            }
            if (had)
                continue;
            ModelNeed need;
            need.name = casing;
            need.file = std::string("models/") + casing + ".mdl";
            need.firstSequence = true;
            needs.push_back(need);
        }
    }

    void addGrenadeNeeds(bool playerGrenades, bool playerLauncher, std::vector<ModelNeed> &needs)
    {
        bool soldiers = false;
        for (const ModelNeed &need : needs)
            soldiers = soldiers || need.name == "hgrunt";
        auto want = [&](const char *name, std::vector<std::string> clips)
        {
            for (ModelNeed &need : needs)
            {
                if (need.name != name)
                    continue;
                // (A grenade to pick up is the same model: its clips are
                // added to.)
                for (const std::string &clip : clips)
                {
                    if (std::find(need.clips.begin(), need.clips.end(), clip) == need.clips.end())
                        need.clips.push_back(clip);
                }
                need.firstSequence = need.firstSequence || clips.empty();
                return;
            }
            ModelNeed need;
            need.name = name;
            need.file = std::string("models/") + name + ".mdl";
            need.clips = std::move(clips);
            need.firstSequence = need.clips.empty();
            needs.push_back(need);
        };
        if (playerGrenades || soldiers)
            want("w_grenade", {"idle", "roll1", "toss"});
        if (playerLauncher || soldiers)
            want("grenade", {});
    }

    void addMultiplayerNeeds(const std::vector<Entity> &entities, std::vector<ModelNeed> &needs)
    {
        if (!isMultiplayerMap(entities))
            return;
        ModelNeed other;
        other.name = kPlayerModelName;
        other.file = "models/player/gordon/gordon.mdl";
        other.clips = {"look_idle", "run",        "walk",          "crouch_idle",  "crawl",   "jump",
                       "die_simple", "die_backwards", "die_forwards", "headshot", "gutshot"};
    for (const char *aim : hldata::kAimClips)
        other.clips.push_back(aim);
        needs.push_back(other);
    }

    std::vector<ModelNeed> collectModelNeeds(const std::vector<Entity> &entities)
    {
        std::vector<ModelNeed> needs;
        auto needOf = [&](const std::string &file) -> ModelNeed &
        {
            for (ModelNeed &need : needs)
            {
                if (need.file == file)
                    return need;
            }
            needs.emplace_back();
            needs.back().file = file;
            needs.back().name = modelNameOf(file);
            return needs.back();
        };
        auto add = [](ModelNeed &need, const std::string &clip)
        {
            if (!clip.empty() && std::find(need.clips.begin(), need.clips.end(), clip) == need.clips.end())
                need.clips.push_back(clip);
        };
        for (const Entity &entity : entities)
        {
            const ActorInfo info = actorOf(entity);
            if (!info.is)
                continue;
            ModelNeed &need = needOf(info.file);
            need.firstSequence = need.firstSequence || info.firstSequence;
            need.activities = need.activities || info.activities;
            addActivities(need, info.dsActivities);
            for (const std::string &clip : info.clips)
                add(need, clip);
        }
        // What comes apart, and what is thrown out: the pieces an alien
        // and a person come apart into, where the map has either that can
        // be killed; and the model each shooter names.
        bool aliens = false, people = false;
        for (const Entity &entity : entities)
        {
            const ActorInfo info = actorOf(entity);
            if (info.is && (info.kind == ActorKind::Grunt || info.kind == ActorKind::Scientist ||
                            info.kind == ActorKind::Guard || info.kind == ActorKind::Sitter ||
                            info.kind == ActorKind::Assassin))
                people = true;
            else if (info.is && (info.kind == ActorKind::Headcrab || info.kind == ActorKind::BabyCrab ||
                                 info.kind == ActorKind::Zombie ||
                                 info.kind == ActorKind::Barnacle || info.kind == ActorKind::Slave ||
                                 info.kind == ActorKind::Bullsquid || info.kind == ActorKind::Houndeye ||
                                 info.kind == ActorKind::AlienGrunt || info.kind == ActorKind::Controller ||
                                 info.kind == ActorKind::Gargantua || info.kind == ActorKind::Ichthyosaur))
                aliens = true;
            const std::string cls(entity.className());
            const std::string thrown = cls == "gibshooter" ? "models/hgibs.mdl"
                                                           : (cls == "env_shooter" ? shootModel(entity) : "");
            if (thrown.size() > 4 && thrown.substr(thrown.size() - 4) == ".mdl")
                needOf(thrown).firstSequence = true;
        }
        // What a soldier drops when he dies (his gun; a launcher's grenades).
        for (const Entity &entity : entities)
        {
            if (lowered(entity.className()) != "monster_human_grunt")
                continue;
            const int weapons = static_cast<int>(numberKey(entity, "weapons"));
            add(needOf((weapons & 8) ? "models/w_shotgun.mdl" : "models/w_9mmar.mdl"), "idle");
            if (weapons & 4)
                add(needOf("models/w_argrenade.mdl"), "idle");
        }
        // ... and what a rocket tank fires: the RPG's rocket.
        for (const Entity &entity : entities)
        {
            const std::string fires = lowered(entity.className());
            if (fires == "func_tankrocket" || fires == "monster_apache")
                needOf("models/rpgrocket.mdl").firstSequence = true;
        }
        // ... and what big momma plays at the nodes of her path, and the young
        // she lays.
        bool momma = false;
        for (const Entity &entity : entities)
            momma = momma || lowered(entity.className()) == "monster_bigmomma";
        for (const Entity &entity : entities)
        {
            if (!momma || lowered(entity.className()) != "info_bigmomma")
                continue;
            ModelNeed &need = needOf("models/big_mom.mdl");
            add(need, lowered(entity.value("presequence")));
            add(need, lowered(entity.value("reachsequence")));
        }
        // ... and the friends a nihilanth calls up: as a map that has them
        // would have them.
        bool nihilanth = false;
        for (const Entity &entity : entities)
            nihilanth = nihilanth || lowered(entity.className()) == "monster_nihilanth";
        if (nihilanth)
        {
            for (const char *cls : {"monster_alien_slave", "monster_alien_controller"})
            {
                Entity one;
                one.pairs.emplace_back("classname", cls);
                const ActorInfo info = actorOf(one);
                ModelNeed &need = needOf(info.file);
                need.activities = need.activities || info.activities;
                addActivities(need, info.dsActivities);
                for (const std::string &clip : info.clips)
                    add(need, clip);
            }
        }
        if (momma)
        {
            ModelNeed &crab = needOf("models/baby_headcrab.mdl");
            // (By name: the headcrab's list of them, which hers is.)
            for (const char *clip : {"idle1", "run", "walk", "jump", "flinch", "dieback"})
                add(crab, clip);
        }
        // ... and how soldiers come down from an osprey: on a rope.
        for (const Entity &entity : entities)
        {
            if (lowered(entity.className()) != "monster_osprey")
                continue;
            ModelNeed &grunt = needOf("models/hgrunt.mdl");
            add(grunt, "repel_repel");
            add(grunt, "repel_shoot");
            add(grunt, "repel_land");
        }
        // ... and what a guard drops: his pistol.
        for (const Entity &entity : entities)
        {
            if (lowered(entity.className()) == "monster_barney")
                add(needOf("models/w_9mmhandgun.mdl"), "idle");
        }
        // ... and what breaks open to give something.
        for (const Entity &entity : entities)
        {
            if (const ItemClass *item = spawnObjectOf(entity))
                add(needOf(item->file), item->kind == ItemKind::Tripmine ? "ground" : "idle");
        }
        if (aliens)
            needOf("models/agibs.mdl").firstSequence = true;
        // (What an alien grunt's gun sends, and a monstermaker's snark --
        // the player's to look at: HlAliens.hpp.)
        for (const Entity &entity : entities)
        {
            const ActorInfo info = actorOf(entity);
            if (info.is && info.kind == ActorKind::AlienGrunt)
                needOf("models/hornet.mdl").firstSequence = true;
            if (info.is && info.kind == ActorKind::Snark)
                add(needOf("models/w_squeak.mdl"), "run");
        }
        // (A person's pieces: every map has the player, who may be blown
        // apart.)
        (void)people;
        needOf("models/hgibs.mdl").firstSequence = true;
        // What the scripts have their actors do: an actor by name, or every
        // actor of a class. (One a monstermaker makes is called by the
        // maker's netname, as it is named when it is made: the gargantua of
        // Uplink's end is the "garg" that spawn_garg makes, and acts out
        // what its scripts name.)
        for (const Entity &script : entities)
        {
            if (!isScript(std::string(script.className())))
                continue;
            const std::string who = lowered(script.value("m_iszEntity"));
            const std::string play = lowered(script.value("m_iszPlay"));
            const std::string idle = lowered(script.value("m_iszIdle"));
            const int moveTo = static_cast<int>(numberKey(script, "m_fMoveTo"));
            for (const Entity &entity : entities)
            {
                const ActorInfo info = actorOf(entity);
                if (!info.is)
                    continue;
                const std::string named = lowered(entity.value(info.spare != 0 ? "netname" : "targetname"));
                if (named != who && std::string(entity.className()) != who)
                    continue;
                ModelNeed &need = needOf(info.file);
                add(need, play);
                add(need, idle);
                // Anything may be told to walk or run somewhere.
                if (moveTo == 1 || moveTo == 2)
                {
                    add(need, "walk");
                    add(need, "run");
                }
            }
        }
        return needs;
    }

    void addVisitorNeeds(const std::vector<Entity> &entities, const std::vector<Entity> &neighbour,
                         const std::string &landmark, std::vector<ModelNeed> &needs)
    {
        double mark[3] = {0, 0, 0};
        bool found = false;
        for (const Entity &entity : neighbour)
        {
            if (entity.className() == "info_landmark" &&
                lowered(entity.value("targetname")) == lowered(landmark))
                found = entity.vec3("origin", mark) || true;
        }
        if (!found)
            return;
        for (const Entity &entity : neighbour)
        {
            const ActorInfo info = actorOf(entity);
            const std::string name = lowered(entity.value("targetname"));
            double origin[3] = {0, 0, 0};
            entity.vec3("origin", origin);
            // The same test the DS makes when the player crosses
            // (kVisitorRange) -- but one of the people may be following the
            // player, from anywhere in that map, named or not.
            const bool follower = info.kind == ActorKind::Scientist || info.kind == ActorKind::Guard;
            if (!info.is || (!follower && (name.empty() || std::fabs(origin[0] - mark[0]) > 2048 ||
                                           std::fabs(origin[1] - mark[1]) > 2048 ||
                                           std::fabs(origin[2] - mark[2]) > 2048)))
                continue;
            ModelNeed *need = nullptr;
            for (ModelNeed &have : needs)
            {
                if (have.file == info.file)
                    need = &have;
            }
            if (need == nullptr)
            {
                needs.emplace_back();
                need = &needs.back();
                need->file = info.file;
                need->name = modelNameOf(info.file);
            }
            auto add = [&](const std::string &clip)
            {
                if (!clip.empty() &&
                    std::find(need->clips.begin(), need->clips.end(), clip) == need->clips.end())
                    need->clips.push_back(clip);
            };
            need->firstSequence = need->firstSequence || info.firstSequence;
            need->activities = need->activities || info.activities;
            addActivities(*need, info.dsActivities);
            for (const std::string &clip : info.clips)
                add(clip);
            add("walk");
            add("run");
            for (const Entity &script : entities)
            {
                if (name.empty() || !isScript(std::string(script.className())) ||
                    lowered(script.value("m_iszEntity")) != name)
                    continue;
                add(lowered(script.value("m_iszPlay")));
                add(lowered(script.value("m_iszIdle")));
            }
        }
    }

    void entityAngles(const Entity &entity, double *out)
    {
        out[0] = out[1] = out[2] = 0;
        for (const auto &[key, value] : entity.pairs)
        {
            double three[3];
            if (key == "angles" && std::sscanf(value.c_str(), "%lf %lf %lf", &three[0], &three[1], &three[2]) == 3)
            {
                for (int a = 0; a < 3; ++a)
                    out[a] = three[a];
            }
            else if (key == "angle")
            {
                out[0] = out[2] = 0;
                out[1] = std::atof(value.c_str());
            }
        }
    }

    namespace
    {
        /// What a brush on hinges starts turned by (the game's CRotDoor
        /// keeps its angles: it opens from them), as turns, in the record's
        /// extra[1..3] (pitch, yaw, roll).
        /// Degrees as turns (65536 to the circle).
        std::uint32_t turnsFromDegrees(double degrees)
        {
            return static_cast<std::uint16_t>(
                static_cast<std::int32_t>(std::lround(std::remainder(degrees, 360.0) * 65536.0 / 360.0)) & 0xFFFF);
        }

        void turnedFrom(const Entity &entity, hldata::LogicRecord &record)
        {
            double angles[3];
            entityAngles(entity, angles);
            for (int a = 0; a < 3; ++a)
                record.extra[1 + a] = static_cast<std::uint16_t>(
                    static_cast<std::int32_t>(std::lround(std::remainder(angles[a], 360.0) * 65536.0 / 360.0)) & 0xFFFF);
        }
    } // namespace

    void moverDirection(const Entity &entity, double *out)
    {
        double angles[3];
        entityAngles(entity, angles);
        out[0] = out[1] = out[2] = 0;
        if (angles[0] == 0 && angles[2] == 0 && angles[1] == -1)
        {
            out[2] = 1;
            return;
        }
        if (angles[0] == 0 && angles[2] == 0 && angles[1] == -2)
        {
            out[2] = -1;
            return;
        }
        const double pitch = angles[0] * M_PI / 180.0;
        const double yaw = angles[1] * M_PI / 180.0;
        out[0] = std::cos(pitch) * std::cos(yaw);
        out[1] = std::cos(pitch) * std::sin(yaw);
        out[2] = -std::sin(pitch);
        // A heading of 90 degrees is exactly +Y, not +Y and a rounding error
        // of X: the travel below multiplies it by hundreds of units.
        for (int a = 0; a < 3; ++a)
        {
            if (std::fabs(out[a]) < 1e-9)
                out[a] = 0;
        }
    }

    void moverTravel(const double *direction, const double *size, double lip, double *out)
    {
        // The whole brush slides clear of where it stood, less two units and
        // less the lip that is meant to stay showing.
        const double distance = std::fabs(direction[0] * (size[0] - 2)) +
                                std::fabs(direction[1] * (size[1] - 2)) +
                                std::fabs(direction[2] * (size[2] - 2)) - lip;
        for (int a = 0; a < 3; ++a)
            out[a] = direction[a] * distance;
    }

    void cookLogic(const BspFile &bsp, const std::vector<Entity> &entities,
                   const std::vector<int> &brushRecordOfEntity,
                   const std::vector<std::string> &models, LogicCook &out, const SentenceBook *book,
                   const TitlesBook *titles, const std::string &chapterName,
                   const std::vector<std::string> &lazyModels)
    {
        out = LogicCook();
        out.recordOfEntity.assign(entities.size(), -1);
        // (A map with a nihilanth keeps its named info_targets: it goes by
        // them, and the map takes them away.)
        bool nihilanthMap = false;
        for (const Entity &entity : entities)
            nihilanthMap = nihilanthMap || entity.className() == "monster_nihilanth";

        // The sounds: the common ones first, at their fixed places, then
        // whatever the map names. A sound that loops and the same one played
        // once are two sounds (they are cooked differently).
        const int commonCount = static_cast<int>(CommonSound::Count);
        std::vector<bool> soundUsed(commonCount, false);
        for (int c = 0; c < commonCount; ++c)
        {
            out.sounds.push_back(commonSoundPath(static_cast<CommonSound>(c)));
            out.soundLoops.push_back(commonSoundLoops(static_cast<CommonSound>(c)));
        }
        // The sprites: the common ones first, as the sounds.
        for (int c = 0; c < static_cast<int>(CommonSprite::Count); ++c)
        {
            const auto sprite = static_cast<CommonSprite>(c);
            out.sprites.push_back(commonSpritePath(sprite));
            out.spriteLights.push_back(sprite == CommonSprite::Beam || sprite == CommonSprite::Lightning ||
                                       sprite == CommonSprite::EgonBeam || sprite == CommonSprite::EgonFlare);
        }
        auto spriteIndex = [&](std::string path, bool light) -> std::uint16_t
        {
            for (char &c : path)
                c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (path.size() < 5 || path.substr(path.size() - 4) != ".spr")
                return kNoSprite;
            for (std::size_t i = 0; i < out.sprites.size(); ++i)
            {
                if (out.sprites[i] == path && out.spriteLights[i] == light)
                    return static_cast<std::uint16_t>(i);
            }
            out.sprites.push_back(path);
            out.spriteLights.push_back(light);
            return static_cast<std::uint16_t>(out.sprites.size() - 1);
        };
        auto soundIndex = [&](std::string path, bool loops) -> std::uint16_t
        {
            for (char &c : path)
                c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            // (The game lets a name start with a character that says how it
            // is to be streamed: not part of the name.)
            while (!path.empty() && (path[0] == '*' || path[0] == '/' || path[0] == '#' || path[0] == '@'))
                path.erase(0, 1);
            if (path.empty() || path == "common/null.wav")
                return kNoSound;
            for (std::size_t i = commonCount; i < out.sounds.size(); ++i)
            {
                if (out.sounds[i] == path && out.soundLoops[i] == loops)
                    return static_cast<std::uint16_t>(i);
            }
            out.sounds.push_back(path);
            out.soundLoops.push_back(loops);
            return static_cast<std::uint16_t>(out.sounds.size() - 1);
        };
        auto numbered = [&](const char *stem, int number, int most) -> std::uint16_t
        {
            if (number < 1 || number > most)
                return kNoSound;
            return soundIndex(std::string(stem) + std::to_string(number) + ".wav", false);
        };
        // A spoken line, as an index into out.sentences; -1 if the book has
        // none of that name.
        std::map<std::string, int> sentenceIds;
        auto sentenceIndex = [&](const std::string &name) -> int
        {
            const Sentence *sentence = book != nullptr ? book->find(name) : nullptr;
            if (sentence == nullptr || sentence->words.size() > 255)
                return -1;
            const auto known = sentenceIds.find(name);
            if (known != sentenceIds.end())
                return known->second;
            SentenceRecord record{};
            record.firstWord = static_cast<std::uint16_t>(out.sentenceWords.size());
            record.volume = static_cast<std::uint8_t>(std::clamp(sentence->volume, 0, 127));
            record.pitch = 0;
            for (std::size_t w = 0; w < sentence->words.size(); ++w)
            {
                const std::uint16_t index = soundIndex(sentence->words[w], false);
                if (index == kNoSound)
                    continue;
                const WordSaid said = w < sentence->said.size() ? sentence->said[w] : WordSaid();
                SentenceWord word{};
                word.sound = index;
                word.pitch = static_cast<std::uint8_t>(said.pitch == 100 ? 0 : std::clamp(said.pitch, 30, 255));
                word.volume = static_cast<std::uint8_t>(std::clamp(said.volume, 0, 100));
                word.end = static_cast<std::uint8_t>(std::clamp(said.end, 10, 100));
                out.sentenceWords.push_back(word);
            }
            record.wordCount = static_cast<std::uint8_t>(out.sentenceWords.size() - record.firstWord);
            out.sentences.push_back(record);
            sentenceIds[name] = static_cast<int>(out.sentences.size() - 1);
            return sentenceIds[name];
        };
        // The suit's lines first, in SuitLine's order, so that each is its
        // own sentence number (a book without one: an empty line kept in its
        // place).
        for (int line = 0; line < kSuitLines; ++line)
        {
            if (sentenceIndex(kSuitLineNames[line]) != line)
            {
                out.sentences.resize(static_cast<std::size_t>(line));
                SentenceRecord none{};
                none.firstWord = static_cast<std::uint16_t>(out.sentenceWords.size());
                out.sentences.push_back(none);
            }
        }
        std::vector<LogicSound> heard; // parallel to out.records

        std::map<std::string, std::uint16_t> nameIds;
        auto nameId = [&](std::string_view text) -> std::uint16_t
        {
            if (text.empty())
                return 0;
            // The game compares names without regard to case.
            const std::string key = lowered(text);
            const auto found = nameIds.find(key);
            if (found != nameIds.end())
                return found->second;
            const auto id = static_cast<std::uint16_t>(nameIds.size() + 1);
            nameIds[key] = id;
            return id;
        };

        // Strings records point into; offset 0 is the empty string.
        out.strings.assign(1, '\0');
        auto stringOffset = [&](std::string_view text) -> std::uint32_t
        {
            const auto offset = static_cast<std::uint32_t>(out.strings.size());
            for (char c : text)
                out.strings.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            out.strings.push_back('\0');
            return offset;
        };
        // What the map writes on the screen: one record for each thing
        // written, however many entities write it.
        std::map<std::string, std::uint32_t> messageIds;
        auto addMessage = [&](const std::string &wanted, std::uint8_t flags) -> std::uint32_t
        {
            const std::string key = wanted + '\x01' + static_cast<char>('0' + flags);
            if (const auto found = messageIds.find(key); found != messageIds.end())
                return found->second;
            const Title *title = titles != nullptr ? titles->find(wanted) : nullptr;
            const Title plain;
            const Title &t = title != nullptr ? *title : plain;
            MessageRecord message{};
            message.text = static_cast<std::uint32_t>(out.strings.size());
            for (char c : title != nullptr ? t.text : wanted)
                out.strings.push_back(c);
            out.strings.push_back('\0');
            auto place = [](double v) -> std::int16_t
            { return v < 0 ? std::int16_t(-1) : static_cast<std::int16_t>(std::clamp(v, 0.0, 1.0) * 256 + 0.5); };
            auto colour = [](const int *c) -> std::uint16_t
            { return static_cast<std::uint16_t>((c[0] >> 3) | (c[1] >> 3) << 5 | (c[2] >> 3) << 10); };
            auto ms = [](double seconds) -> std::uint16_t
            { return static_cast<std::uint16_t>(std::clamp(seconds * 1000 + 0.5, 0.0, 60000.0)); };
            message.x = place(t.x);
            message.y = place(t.y);
            message.effect = static_cast<std::uint8_t>(std::clamp(t.effect, 0, 2));
            message.flags = flags;
            message.colour = colour(t.colour);
            message.colour2 = colour(t.colour2);
            message.fadeInMs = ms(t.fadeIn);
            message.fadeOutMs = ms(t.fadeOut);
            message.holdMs = ms(t.hold);
            message.fxMs = ms(t.fxTime);
            const auto id = static_cast<std::uint32_t>(out.messages.size());
            out.messages.push_back(message);
            messageIds[key] = id;
            return id;
        };
        // The world's own: the game's title (on the map the game begins
        // with) and the chapter's name, shown as the map is come into.
        for (const Entity &entity : entities)
        {
            if (entity.className() != "worldspawn")
                continue;
            if (numberKey(entity, "gametitle") != 0)
                addMessage("GAMETITLE", kMessageLogo | kMessageAtStart);
            const std::string chapter(entity.value("chaptertitle"));
            if (!chapter.empty())
                addMessage(chapter, kMessageAtStart);
        }
        // The chapter the map is part of, for whoever lists the saves: its
        // own title if it opens one, else the title of the chapter it is
        // in -- there to be read, not shown.
        if (!chapterName.empty())
        {
            const std::uint32_t id = addMessage(chapterName, 0);
            out.messages[id].flags |= kMessageChapter;
        }
        auto hashOf = [](std::string_view text) -> std::uint32_t
        {
            return text.empty() ? 0 : nameHash(std::string(text).c_str());
        };

        for (const Entity &entity : entities)
        {
            if (entity.className() != "info_landmark" || entity.value("targetname").empty())
                continue;
            Landmark landmark{};
            landmark.hash = hashOf(entity.value("targetname"));
            double origin[3] = {0, 0, 0};
            entity.vec3("origin", origin);
            for (int a = 0; a < 3; ++a)
                landmark.origin[a] = fxFromDouble(origin[a]);
            out.landmarks.push_back(landmark);
        }

        // Path corners first: a train names the one it starts at. Each is
        // found by its own name; "target" is the name of the next.
        constexpr int kCornerFlagWait = 1;
        constexpr int kCornerFlagTeleport = 2;
        std::map<std::string, std::uint16_t> cornerOfName;
        std::vector<std::string> cornerNext;
        for (const Entity &entity : entities)
        {
            if (entity.className() != "path_corner")
                continue;
            PathCorner corner{};
            double origin[3] = {0, 0, 0};
            entity.vec3("origin", origin);
            for (int a = 0; a < 3; ++a)
                corner.pos[a] = fxFromDouble(origin[a]);
            corner.speed = fxFromDouble(std::max(0.0, numberKey(entity, "speed")));
            corner.waitMs = std::max(0, millis(numberKey(entity, "wait")));
            corner.fireTarget = nameId(entity.value("message"));
            const int flags = static_cast<int>(numberKey(entity, "spawnflags"));
            corner.flags = static_cast<std::uint16_t>((flags & kCornerFlagWait ? kCornerWaitForUse : 0) |
                                                      (flags & kCornerFlagTeleport ? kCornerTeleport : 0));
            corner.next = kNoCorner;
            const std::string name(entity.value("targetname"));
            // Two corners with one name: the first is the one found, as in
            // the game.
            if (!name.empty() && cornerOfName.find(name) == cornerOfName.end())
                cornerOfName[name] = static_cast<std::uint16_t>(out.corners.size());
            cornerNext.emplace_back(entity.value("target"));
            out.corners.push_back(corner);
        }
        for (std::size_t c = 0; c < out.corners.size(); ++c)
        {
            const auto found = cornerOfName.find(cornerNext[c]);
            if (found != cornerOfName.end())
                out.corners[c].next = found->second;
        }

        // A track is the same list again, in a namespace of its own: a
        // path_track's "target" is another path_track.
        std::map<std::string, std::uint16_t> trackOfName;
        {
            const std::size_t firstTrack = out.corners.size();
            std::vector<std::string> trackNext;
            for (const Entity &entity : entities)
            {
                if (entity.className() != "path_track")
                    continue;
                PathCorner node{};
                double origin[3] = {0, 0, 0};
                entity.vec3("origin", origin);
                for (int a = 0; a < 3; ++a)
                    node.pos[a] = fxFromDouble(origin[a]);
                node.speed = fxFromDouble(std::max(0.0, numberKey(entity, "speed")));
                node.fireTarget = nameId(entity.value("message"));
                node.netName = nameId(entity.value("netname"));
                const int flags = static_cast<int>(numberKey(entity, "spawnflags"));
                node.flags = static_cast<std::uint16_t>((flags & 1 ? kCornerDisabled : 0) |
                                                        (flags & 2 ? kCornerFireOnce : 0) |
                                                        (flags & 8 ? kCornerDisableTrain : 0));
                node.next = kNoCorner;
                const std::string name(entity.value("targetname"));
                if (!name.empty() && trackOfName.find(name) == trackOfName.end())
                    trackOfName[name] = static_cast<std::uint16_t>(out.corners.size());
                trackNext.emplace_back(entity.value("target"));
                out.corners.push_back(node);
            }
            for (std::size_t c = firstTrack; c < out.corners.size(); ++c)
            {
                const auto found = trackOfName.find(trackNext[c - firstTrack]);
                if (found != trackOfName.end())
                    out.corners[c].next = found->second;
            }
        }
        auto trackNode = [&](std::string_view name) -> std::uint16_t
        {
            const auto found = trackOfName.find(std::string(name));
            return found != trackOfName.end() ? found->second : kNoCorner;
        };

        struct ManagerOutput
        {
            std::uint16_t target;
            std::uint32_t delayMs;
        };
        std::vector<std::vector<ManagerOutput>> managerOutputs; // by record
        std::vector<std::vector<std::array<double, 3>>> beamPlacesOf; // by record
        std::vector<std::vector<std::uint16_t>> speakerLinesOf;     // by record
        std::vector<std::uint16_t> trainCorners;                // by record

        for (std::size_t e = 0; e < entities.size(); ++e)
        {
            const Entity &entity = entities[e];
            const std::string cls(entity.className());
            const int spawnflags = static_cast<int>(numberKey(entity, "spawnflags"));
            const bool named = !entity.value("targetname").empty();

            // The entity's brush model, if it has one, for sizes and volumes.
            const BspModel *model = nullptr;
            {
                const std::string text(entity.value("model"));
                if (text.size() >= 2 && text[0] == '*')
                {
                    const int index = std::atoi(text.c_str() + 1);
                    if (index > 0 && index < static_cast<int>(bsp.models.size()))
                        model = &bsp.models[index];
                }
            }
            double origin[3] = {0, 0, 0};
            entity.vec3("origin", origin);

            if (cls == "env_sound")
            {
                // Not something that acts: a place, and how it sounds.
                RoomRecord room{};
                for (int a = 0; a < 3; ++a)
                    room.origin[a] = fxFromDouble(origin[a]);
                room.radius = static_cast<std::uint16_t>(
                    std::clamp(numberKey(entity, "radius", 128), 0.0, 65535.0));
                room.type = static_cast<std::uint8_t>(
                    std::clamp(static_cast<int>(numberKey(entity, "roomtype")), 0, 28));
                out.rooms.push_back(room);
                continue;
            }

            // A colour (the game's "rendercolor"; none, or black, is the
            // picture's own) and how solid (its "render amount", 0..255),
            // as the Sprite and Beam records keep them.
            auto look = [&](double amount) -> std::uint32_t
            {
                double colour[3] = {255, 255, 255};
                if (!entity.vec3("rendercolor", colour) || (colour[0] == 0 && colour[1] == 0 && colour[2] == 0))
                    colour[0] = colour[1] = colour[2] = 255;
                std::uint32_t packed = 0;
                for (int c = 0; c < 3; ++c)
                    packed |= static_cast<std::uint32_t>(std::clamp(std::lround(colour[c]), 0L, 255L) >> 3) << (c * 5);
                const long solid = std::clamp(std::lround(amount * 31 / 255), 1L, 31L);
                return packed | (static_cast<std::uint32_t>(solid) << 16);
            };
            // Where the entity called `name` is: its origin, or the middle
            // of its brush.
            auto pointOf = [&](std::string_view name, double *at) -> bool
            {
                if (name.empty())
                    return false;
                for (const Entity &other : entities)
                {
                    if (other.value("targetname") != name)
                        continue;
                    double place[3] = {0, 0, 0};
                    const bool has = other.vec3("origin", place);
                    const std::string text(other.value("model"));
                    if (text.size() >= 2 && text[0] == '*')
                    {
                        const int index = std::atoi(text.c_str() + 1);
                        if (index > 0 && index < static_cast<int>(bsp.models.size()))
                        {
                            for (int a = 0; a < 3; ++a)
                                at[a] = place[a] + (bsp.models[index].mins[a] + bsp.models[index].maxs[a]) * 0.5;
                            return true;
                        }
                    }
                    if (!has)
                        continue;
                    for (int a = 0; a < 3; ++a)
                        at[a] = place[a];
                    return true;
                }
                return false;
            };
            std::uint16_t heardSpark = kNoSound;
            std::uint16_t beamStart = 0;
            std::vector<std::array<double, 3>> beamPlaces;
            std::vector<std::uint16_t> speakerLines;
            bool isBeam = false;
            bool delayIsOwn = false, renderTarget = false;
            (void)renderTarget;

            LogicRecord record{};
            record.brush = brushRecordOfEntity[e] >= 0
                               ? static_cast<std::uint16_t>(brushRecordOfEntity[e])
                               : kNoBrush;
            record.useType = kUseToggle;
            record.waitMs = -1;
            bool make = false;
            std::uint16_t trainCorner = kNoCorner;
            std::vector<ManagerOutput> outputs;

            auto travel = [&](double lip)
            {
                if (model == nullptr)
                    return;
                double direction[3], move[3];
                const double size[3] = {double(model->maxs[0]) - model->mins[0],
                                        double(model->maxs[1]) - model->mins[1],
                                        double(model->maxs[2]) - model->mins[2]};
                moverDirection(entity, direction);
                moverTravel(direction, size, lip, move);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(move[a]);
            };

            if (cls == "func_door" || cls == "func_water")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Door);
                record.speed = fxFromDouble(numberKey(entity, "speed", 100));
                if (record.speed <= 0)
                    record.speed = fxFromInt(100);
                record.waitMs = millis(numberKey(entity, "wait"));
                record.damage = static_cast<std::uint16_t>(
                    std::clamp(numberKey(entity, "dmg"), 0.0, 65535.0));
                travel(numberKey(entity, "lip"));
                if (spawnflags & kDoorStartOpen)
                    record.flags |= kLogicStartOpen;
                if (spawnflags & kDoorNoAutoReturn)
                    record.flags |= kLogicToggle;
                if (spawnflags & kDoorUseOnly)
                    record.flags |= kLogicUseOnly;
                // Walking into a door opens it only if nothing else is meant
                // to: a door with a name waits to be told.
                else if (!named)
                    record.flags |= kLogicTouch;
                record.netName = nameId(entity.value("netname"));
            }
            else if (cls == "func_door_rotating")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::DoorRotating);
                // The hinge is vertical unless the map says otherwise. The
                // flags are named for the AXIS in the editor ("Z", "X") but
                // choose an ANGLE: 64 is roll, 128 is pitch.
                record.extra[0] = (spawnflags & 64) ? kRoll : ((spawnflags & 128) ? kPitch : kYaw);
                turnedFrom(entity, record);
                // Degrees, and degrees a second, as turns * 65536.
                double distance = numberKey(entity, "distance", 90);
                if (spawnflags & 2)
                    distance = -distance; // "reverse direction"
                record.move[0] = static_cast<fx>(std::lround(distance * 65536.0 / 360.0));
                double speed = numberKey(entity, "speed", 100);
                if (speed <= 0)
                    speed = 100;
                record.speed = static_cast<fx>(std::lround(speed * 65536.0 / 360.0));
                record.waitMs = millis(numberKey(entity, "wait"));
                record.damage = static_cast<std::uint16_t>(
                    std::clamp(numberKey(entity, "dmg"), 0.0, 65535.0));
                if (spawnflags & kDoorStartOpen)
                    record.flags |= kLogicStartOpen;
                if (spawnflags & kDoorNoAutoReturn)
                    record.flags |= kLogicToggle;
                if (spawnflags & 16)
                    record.flags |= kLogicOneWay;
                if (spawnflags & kDoorUseOnly)
                    record.flags |= kLogicUseOnly;
                else if (!named)
                    record.flags |= kLogicTouch;
                record.netName = nameId(entity.value("netname"));
            }
            else if (cls == "func_rot_button")
            {
                // A button that turns: a valve's wheel. Pressed, it turns
                // its `distance` at its `speed` (degrees, and degrees a
                // second), fires its target when it is round, waits, and
                // turns back -- the game's CRotButton. Which way round is
                // by the flags a door on hinges has (64 roll, 128 pitch;
                // else about the upright), and 2 turns it the other way.
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Button);
                record.flags |= kLogicTurns;
                record.extra[0] = (spawnflags & 64) ? kRoll : ((spawnflags & 128) ? kPitch : kYaw);
                turnedFrom(entity, record);
                double distance = numberKey(entity, "distance", 90);
                if (spawnflags & 2)
                    distance = -distance;
                record.move[0] = static_cast<fx>(std::lround(distance * 65536.0 / 360.0));
                double speed = numberKey(entity, "speed", 40);
                if (speed <= 0)
                    speed = 40;
                record.speed = static_cast<fx>(std::lround(speed * 65536.0 / 360.0));
                double wait = numberKey(entity, "wait");
                if (wait == 0)
                    wait = 1;
                record.waitMs = millis(wait);
                if (spawnflags & kButtonToggle)
                    record.flags |= kLogicToggle;
                if (spawnflags & kButtonTouchOnly)
                    record.flags |= kLogicTouch;
            }
            else if ((cls == "func_tank" || cls == "func_tanklaser" || cls == "func_tankrocket" ||
                      cls == "func_tankmortar") &&
                     brushRecordOfEntity[e] >= 0)
            {
                // A gun on a mount (the game's CFuncTank): its keys, packed
                // (LogicKind::Tank says where).
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Tank);
                double angles[3] = {0, 0, 0};
                if (!entity.vec3("angles", angles))
                    angles[1] = numberKey(entity, "angle");
                auto turns = [](double degrees)
                { return static_cast<std::uint32_t>(static_cast<std::uint16_t>(std::lround(degrees * 65536.0 / 360.0))); };
                auto byte = [](double value) { return static_cast<std::uint32_t>(std::clamp(std::lround(value), 0L, 255L)); };
                record.extra[0] = turns(angles[1]) | (turns(angles[0]) << 16);
                record.extra[1] = byte(numberKey(entity, "yawrange")) | (byte(numberKey(entity, "pitchrange")) << 8) |
                                  (byte(numberKey(entity, "yawtolerance")) << 16) |
                                  (byte(numberKey(entity, "pitchtolerance")) << 24);
                double rate = numberKey(entity, "firerate");
                if (rate <= 0)
                    rate = 1;
                const int variant = cls == "func_tanklaser" ? kTankLaser
                                    : cls == "func_tankrocket" ? kTankRocket
                                    : cls == "func_tankmortar" ? kTankMortar
                                                               : kTankGun;
                int spread = static_cast<int>(numberKey(entity, "firespread"));
                if (spread < 0 || spread > 4)
                    spread = 0;
                record.extra[2] = static_cast<std::uint32_t>(std::clamp(std::lround(rate * 16), 1L, 65535L)) |
                                  (byte(numberKey(entity, "pitchrate")) << 16) |
                                  (static_cast<std::uint32_t>(std::clamp(static_cast<int>(numberKey(entity, "bullet")), 0, 3)) << 24) |
                                  (static_cast<std::uint32_t>(spread) << 26) | (static_cast<std::uint32_t>(variant) << 29);
                record.extra[3] = static_cast<std::uint32_t>(std::clamp(millis(numberKey(entity, "persistence")), 0, 65535));
                record.speed = fxFromDouble(numberKey(entity, "yawrate"));
                record.move[0] = fxFromDouble(numberKey(entity, "barrel"));
                record.move[1] = fxFromDouble(numberKey(entity, "barrely"));
                record.move[2] = fxFromDouble(numberKey(entity, "barrelz"));
                record.mins[0] = static_cast<std::int16_t>(std::clamp(numberKey(entity, "minRange"), 0.0, 32767.0));
                record.mins[1] = static_cast<std::int16_t>(std::clamp(numberKey(entity, "maxRange"), 0.0, 32767.0));
                record.mins[2] = static_cast<std::int16_t>(std::clamp(numberKey(entity, "iMagnitude"), 0.0, 32767.0));
                record.maxs[0] = static_cast<std::int16_t>(spawnflags & 0x7FFF);
                record.damage = static_cast<std::uint16_t>(std::clamp(numberKey(entity, "bullet_damage"), 0.0, 65535.0));
                record.netName = nameId(entity.value("laserentity"));
                if (spawnflags & kTankActive)
                    record.flags |= kLogicStartOpen;
                // What it shows as it fires: its flash (light) and its smoke,
                // at its scale (none given: the sprite's own size).
                double scale = numberKey(entity, "spritescale");
                if (scale <= 0)
                    scale = 1;
                const std::uint16_t quarters = static_cast<std::uint16_t>(std::clamp(std::lround(scale * 4), 1L, 15L));
                auto packed = [&](std::uint16_t sprite)
                {
                    return static_cast<std::int16_t>(
                        static_cast<std::uint16_t>((sprite < kTankSpriteNone ? sprite : kTankSpriteNone) | (quarters << 12)));
                };
                record.maxs[1] = packed(spriteIndex(std::string(entity.value("spriteflash")), true));
                record.maxs[2] = packed(spriteIndex(std::string(entity.value("spritesmoke")), false));
            }
            else if (cls == "info_target" && named && nihilanthMap)
            {
                // A place the nihilanth goes by (LogicKind::Target).
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Target);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(origin[a]);
            }
            else if (cls == "monster_grunt_repel")
            {
                // A soldier down a rope when told (LogicKind::Repel); his
                // actor's place is filled in with the actors.
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Repel);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(origin[a]);
            }
            else if (cls == "info_bigmomma")
            {
                // A place on big momma's path (LogicKind::MommaNode).
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::MommaNode);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(origin[a]);
                record.extra[0] = static_cast<std::uint32_t>(std::clamp(numberKey(entity, "health"), 0.0, 30000.0));
                record.extra[1] = static_cast<std::uint32_t>(std::clamp(numberKey(entity, "radius"), 0.0, 32767.0));
                record.waitMs = millis(numberKey(entity, "reachdelay"));
                record.extra[2] = nameId(entity.value("reachtarget"));
                record.extra[3] = hashOf(lowered(entity.value("reachsequence")));
                const std::uint32_t before = hashOf(lowered(entity.value("presequence")));
                record.damage = static_cast<std::uint16_t>(before);
                record.pad = static_cast<std::uint16_t>(before >> 16);
                record.mins[0] = static_cast<std::int16_t>(spawnflags & 3);
                double angles[3] = {0, 0, 0};
                if (!entity.vec3("angles", angles))
                    angles[1] = numberKey(entity, "angle");
                record.maxs[0] = static_cast<std::int16_t>(std::lround(angles[1] * 65536.0 / 360.0));
            }
            else if ((cls == "func_tankcontrols" || cls == "func_mortar_field") && model != nullptr)
            {
                // Volumes: where a player takes a gun's controls; and where
                // mortars fall on call.
                make = true;
                for (int a = 0; a < 3; ++a)
                {
                    record.mins[a] = static_cast<std::int16_t>(
                        std::clamp(std::floor(model->mins[a] + origin[a]), -32768.0, 32767.0));
                    record.maxs[a] = static_cast<std::int16_t>(
                        std::clamp(std::ceil(model->maxs[a] + origin[a]), -32768.0, 32767.0));
                }
                if (cls == "func_tankcontrols")
                {
                    record.kind = static_cast<std::uint8_t>(LogicKind::TankControls);
                }
                else
                {
                    record.kind = static_cast<std::uint8_t>(LogicKind::MortarField);
                    record.extra[0] = static_cast<std::uint32_t>(std::clamp(static_cast<int>(numberKey(entity, "m_iCount")), 0, 32));
                    record.extra[1] = static_cast<std::uint32_t>(std::clamp(numberKey(entity, "m_flSpread"), 0.0, 4096.0));
                    record.extra[2] = static_cast<std::uint32_t>(std::clamp(static_cast<int>(numberKey(entity, "m_fControl")), 0, 2));
                    record.extra[3] = static_cast<std::uint32_t>(nameId(entity.value("m_iszXController"))) |
                                      (static_cast<std::uint32_t>(nameId(entity.value("m_iszYController"))) << 16);
                }
            }
            else if (cls == "momentary_rot_button" && brushRecordOfEntity[e] >= 0)
            {
                // A wheel turned by hand: as a func_rot_button turns
                // (CMomentaryRotButton), and what it targets goes with it.
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::MomentaryButton);
                // (Flag 1: not to be used, only turned with the wheel that
                // moves the same thing: t0a0a's bridge.)
                if (spawnflags & 1)
                    record.flags |= kLogicTriggerOnly;
                record.extra[0] = (spawnflags & 64) ? kRoll : ((spawnflags & 128) ? kPitch : kYaw);
                turnedFrom(entity, record);
                double distance = numberKey(entity, "distance", 90);
                if (spawnflags & 2)
                    distance = -distance;
                record.move[0] = static_cast<fx>(std::lround(distance * 65536.0 / 360.0));
                double speed = numberKey(entity, "speed", 40);
                if (speed <= 0)
                    speed = 40;
                record.speed = static_cast<fx>(std::lround(speed * 65536.0 / 360.0));
                // (Flag 16: let go of, it goes back by itself, at its
                // "returnspeed" -- none, it stays.)
                const double back = numberKey(entity, "returnspeed");
                if ((spawnflags & 16) && back > 0)
                    record.move[1] = static_cast<fx>(std::lround(back * 65536.0 / 360.0));
            }
            else if (cls == "momentary_door" && brushRecordOfEntity[e] >= 0)
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::MomentaryDoor);
                record.speed = fxFromDouble(numberKey(entity, "speed", 100));
                if (record.speed <= 0)
                    record.speed = fxFromInt(100);
                travel(numberKey(entity, "lip"));
            }
            else if (cls == "func_button")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Button);
                record.speed = fxFromDouble(numberKey(entity, "speed", 40));
                if (record.speed <= 0)
                    record.speed = fxFromInt(40);
                // The game's own defaults when the map leaves these at 0.
                double wait = numberKey(entity, "wait");
                if (wait == 0)
                    wait = 1;
                record.waitMs = millis(wait);
                double lip = numberKey(entity, "lip");
                if (lip == 0)
                    lip = 4;
                if (!(spawnflags & kButtonDontMove))
                {
                    travel(lip);
                    // A button whose travel comes to nothing does not move.
                    const fx length = fxLength(record.move);
                    if (length < kFxOne)
                        record.move[0] = record.move[1] = record.move[2] = 0;
                }
                if (spawnflags & kButtonToggle)
                    record.flags |= kLogicToggle;
                if (spawnflags & kButtonTouchOnly)
                    record.flags |= kLogicTouch;
            }
            else if (cls == "func_train" && model != nullptr)
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Train);
                // ("volume", as it is; 0.85 for none: CFuncTrain::Spawn.)
                record.pad = moverVolumeOf(numberKey(entity, "volume"), 0.85);
                record.speed = fxFromDouble(numberKey(entity, "speed", 100));
                if (record.speed <= 0)
                    record.speed = fxFromInt(100);
                // (None, or none said: two -- CFuncTrain::Spawn's.)
                const double dmg = numberKey(entity, "dmg");
                record.damage = static_cast<std::uint16_t>(std::clamp(dmg == 0 ? 2.0 : dmg, 0.0, 65535.0));
                // The point of the brush that visits each corner: its middle.
                for (int a = 0; a < 3; ++a)
                    record.move[a] =
                        fxFromDouble((double(model->mins[a]) + model->maxs[a]) / 2 + origin[a]);
                const auto first = cornerOfName.find(std::string(entity.value("target")));
                trainCorner = first != cornerOfName.end() ? first->second : kNoCorner;
                // Known by the same name in the next map (a lift that goes
                // from one map into the next): it crosses the level change.
                record.extra[3] = hashOf(entity.value("globalname"));
                // A train keeps the angles it is given (it is not a door,
                // for which "angle" is a way to go): built round an origin,
                // it is drawn turned about it. (pitch, yaw, roll.)
                double origin3[3];
                if (entity.vec3("origin", origin3))
                {
                    double angles[3] = {0, 0, 0};
                    if (!entity.vec3("angles", angles))
                        angles[1] = numberKey(entity, "angle");
                    // (Turned as any brush entity is, "angle" 90 a quarter
                    // turn to the left: the tram's door, c0a0e's and c5a1's,
                    // then has its outer face in line with the tram's side,
                    // and its first corner takes it out of the doorway.)
                    for (int a = 0; a < 3; ++a)
                        record.extra[a] = static_cast<std::uint16_t>(std::lround(angles[a] * 65536.0 / 360.0));
                }
            }
            else if (cls == "func_guntarget" && model != nullptr)
            {
                // A target on a path (its "target" the node it starts at):
                // hit while it rides, it takes "health" and then stops and
                // fires its "message". Flag 1: it rides from the start.
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::GunTarget);
                record.speed = fxFromDouble(numberKey(entity, "speed", 100));
                if (record.speed <= 0)
                    record.speed = fxFromInt(100);
                for (int a = 0; a < 3; ++a)
                    record.move[a] =
                        fxFromDouble((double(model->mins[a]) + model->maxs[a]) / 2 + origin[a]);
                // (A path_corner or a path_track -- t0a0b's ride tracks --
                // whichever of that name comes first in the map, as the
                // game finds it by name alone.)
                const std::string first(entity.value("target"));
                trainCorner = kNoCorner;
                for (const Entity &node : entities)
                {
                    if (node.value("targetname") != first)
                        continue;
                    if (node.className() == "path_corner" && cornerOfName.count(first) != 0)
                        trainCorner = cornerOfName[first];
                    else if (node.className() == "path_track" && trackOfName.count(first) != 0)
                        trainCorner = trackOfName[first];
                    else
                        continue;
                    break;
                }
                record.extra[0] = static_cast<std::uint32_t>(std::max(1.0, numberKey(entity, "health", 1)));
                record.netName = nameId(entity.value("message"));
                if (spawnflags & 1)
                    record.flags |= kLogicStartOpen;
            }
            else if ((cls == "func_platrot" || cls == "func_plat") && brushRecordOfEntity[e] >= 0 && model != nullptr)
            {
                // A platform: from where it is built to `height` under it
                // (its own height, less a step, if it says none), turning
                // on the way if it is the kind that does.
                make = true;
                // ("volume", as it is; 0.85 for none: CFuncPlat::Setup.)
                record.pad = moverVolumeOf(numberKey(entity, "volume"), 0.85);
                record.kind = static_cast<std::uint8_t>(LogicKind::PlatRot);
                double height = numberKey(entity, "height");
                if (height == 0)
                    height = double(model->maxs[2]) - model->mins[2] - 8;
                record.move[2] = fxFromDouble(-height);
                double speed = numberKey(entity, "speed");
                record.speed = fxFromDouble(speed > 0 ? speed : 150);
                if (cls == "func_platrot")
                    record.extra[0] = static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(std::lround(numberKey(entity, "rotation") * 65536.0 / 360.0)));
                if (spawnflags & 1)
                    record.flags |= kLogicToggle;
                // (With no name it waits at the far end to be stood on.)
                if (!named)
                    record.flags |= kLogicStartOpen;
            }
            else if ((cls == "light" || cls == "light_spot") && named)
            {
                // A light something switches: its style is the handle on
                // the light it gives (the level's LightPatch).
                const int style = static_cast<int>(numberKey(entity, "style"));
                if (style >= 32 && style < kLightStyles)
                {
                    make = true;
                    record.kind = static_cast<std::uint8_t>(LogicKind::Light);
                    record.extra[0] = static_cast<std::uint32_t>(style);
                    // How it shines while it is on (its "pattern": the
                    // game's letters, a to z, ten a second).
                    const std::string pattern(entity.value("pattern"));
                    if (!pattern.empty())
                        record.extra[1] = stringOffset(pattern);
                    if (spawnflags & 1)
                        record.flags |= kLogicStartOpen;
                }
            }
            else if (cls == "infodecal")
            {
                // A mark the map's makers put there: with a name, one that
                // appears when it is told to; without, one that is there
                // from the start. The nearest of the marks the card has to
                // the picture it names (and nothing, for the signs and
                // letters the card has not got).
                std::string picture(entity.value("texture"));
                for (char &c : picture)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                // (Each as big as its picture is in the game's decals.wad,
                // where a texel is a unit: these are that file's sizes.)
                static const struct
                {
                    const char *name;
                    int which;
                    int size;
                } kMarks[] = {
                    {"{blood1", kDecalBlood, 48},        {"{blood2", kDecalBlood + 1, 48},   {"{blood3", kDecalBlood + 2, 48},
                    {"{blood4", kDecalSplash2, 48},      {"{blood5", kDecalSplash2, 64},     {"{blood6", kDecalSplash, 48},
                    {"{blood7", kDecalBlood, 32},        {"{blood8", kDecalBlood + 1, 16},   {"{bigblood1", kDecalPool, 96},
                    {"{bigblood2", kDecalPool + 1, 80},  {"{bloodhand1", kDecalHand, 32},    {"{bloodhand2", kDecalHand, 64},
                    {"{bloodhand3", kDecalHand, 64},     {"{bloodhand4", kDecalHand, 16},    {"{bloodhand5", kDecalHand, 32},
                    {"{bloodhand6", kDecalHand, 32},     {"{yblood1", kDecalAlien, 48},      {"{yblood2", kDecalAlien2, 32},
                    {"{yblood3", kDecalAlien, 32},       {"{yblood4", kDecalAlien2, 32},     {"{yblood5", kDecalAlien, 32},
                    {"{yblood6", kDecalAlien2, 16},      {"{scorch1", kDecalScorch, 96},     {"{scorch2", kDecalScorch, 96},
                    {"{scorch3", kDecalScorch, 96},      {"{smscorch1", kDecalScorch, 32},   {"{smscorch2", kDecalScorch, 32},
                    {"{smscorch3", kDecalScorch, 48},    {"{spit1", kDecalSpit, 32},         {"{spit2", kDecalSpit, 32},
                    {"{crack1", kDecalWallCrack, 48},    {"{crack2", kDecalWallCrack, 96},   {"{crack3", kDecalWallCrack, 64},
                    {"{crack4", kDecalWallCrack, 64},    {"{dent1", kDecalWallDent, 32},     {"{dent2", kDecalWallDent, 32},
                    {"{dent3", kDecalWallDent, 32},      {"{dent4", kDecalWallDent, 32},     {"{dent5", kDecalWallDent, 32},
                    {"{dent6", kDecalWallDent, 32},      {"{break1", kDecalWallCrack, 48},   {"{break2", kDecalWallCrack, 48},
                    {"{break3", kDecalWallCrack, 48},
                };
                int which = -1, half = 12;
                for (const auto &mark : kMarks)
                {
                    if (picture == mark.name)
                    {
                        which = mark.which;
                        // (A sixteenth over half: the outer texel of its
                        // cell is kept clear.)
                        half = (mark.size * 8 + 7) / 15;
                    }
                }
                // (The rest by what they begin with: a hole is a hole, a
                // ding a dent.)
                std::uint16_t sign = kNoSprite;
                if (which < 0 && (picture.rfind("{shot", 0) == 0 || picture.rfind("{bigshot", 0) == 0))
                    which = kDecalShot, half = 8;
                else if (which < 0 && picture.rfind("{ding", 0) == 0)
                    which = kDecalWallDent, half = 32;
                // Anything else -- a number, an arrow, a letter, a stain of
                // the map's own -- is its own picture, as one of the map's
                // sprites (its size is the sprite's).
                else if (which < 0 && picture.size() > 1 && picture.size() < 16)
                {
                    sign = spriteIndex(std::string(kDecalSignPath) + picture + ".spr", false);
                    if (sign != kNoSprite)
                        which = kDecalSign, half = 0;
                }
                if (which >= 0 && !named)
                    record.flags |= kLogicStartOpen;
                if (which >= 0)
                {
                    make = true;
                    record.kind = static_cast<std::uint8_t>(LogicKind::Mark);
                    for (int a = 0; a < 3; ++a)
                        record.move[a] = fxFromDouble(origin[a]);
                    record.extra[0] = static_cast<std::uint32_t>(which);
                    record.extra[1] = static_cast<std::uint32_t>(half);
                    record.extra[2] = sign;
                }
            }
            else if (cls == "func_pendulum" && brushRecordOfEntity[e] >= 0)
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Pendulum);
                record.extra[0] = (spawnflags & 64) ? kRoll : ((spawnflags & 128) ? kPitch : kYaw);
                record.move[0] = static_cast<fx>(std::lround(numberKey(entity, "distance", 90) * 65536.0 / 360.0));
                record.speed = static_cast<fx>(std::lround(std::max(1.0, numberKey(entity, "speed", 100)) * 65536.0 / 360.0));
                record.extra[1] = static_cast<std::uint32_t>(std::clamp(std::lround(numberKey(entity, "damp") * 1000), 0L, 1000000L));
                record.extra[2] = (spawnflags & 16) ? 1u : 0u; // (stopped, it goes back)
                if (spawnflags & 1)
                    record.flags |= kLogicStartOpen;
                // What it does to whoever it swings into (CPendulum::Touch).
                record.damage = static_cast<std::uint16_t>(std::clamp(numberKey(entity, "dmg"), 0.0, 65535.0));
            }
            else if (cls == "env_beverage")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Beverage);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(origin[a]);
                const double cans = numberKey(entity, "health");
                record.extra[0] = static_cast<std::uint32_t>(cans > 0 ? cans : 10);
            }
            else if (cls == "env_funnel")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Funnel);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(origin[a]);
            }
            else if (cls == "func_tracktrain" && brushRecordOfEntity[e] >= 0)
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::TrackTrain);
                // ("volume", a whole number of tenths; full for none.)
                record.pad = moverVolumeOf(std::trunc(numberKey(entity, "volume")) * 0.1, 1.0);
                double speed = numberKey(entity, "speed");
                if (speed == 0)
                    speed = 100;
                record.speed = fxFromDouble(speed);
                record.extra[0] = static_cast<std::uint32_t>(fxFromDouble(numberKey(entity, "wheels")));
                record.extra[1] = static_cast<std::uint32_t>(fxFromDouble(numberKey(entity, "height")));
                record.extra[2] = hashOf(entity.value("globalname"));
                record.extra[3] =
                    static_cast<std::uint32_t>(fxFromDouble(numberKey(entity, "startspeed")));
                if (spawnflags & 2)
                    record.flags |= kLogicSelfDriven;
                if (spawnflags & 4)
                    record.flags |= kLogicForwardOnly;
                // How hard it shoves what is in its way, and hurts it
                // (CFuncTrackTrain::Blocked).
                record.damage = static_cast<std::uint16_t>(std::clamp(numberKey(entity, "dmg"), 0.0, 65535.0));
                trainCorner = trackNode(entity.value("target"));
            }
            else if ((cls == "func_trackautochange" || cls == "func_trackchange") &&
                     brushRecordOfEntity[e] >= 0)
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::TrackChange);
                double speed = numberKey(entity, "speed");
                if (speed <= 0)
                    speed = 150;
                record.speed = fxFromDouble(speed);
                record.move[2] = -fxFromDouble(numberKey(entity, "height"));
                record.extra[0] = static_cast<std::uint32_t>(static_cast<std::int32_t>(
                    std::lround(numberKey(entity, "rotation") * 65536.0 / 360.0)));
                record.extra[1] = trackNode(entity.value("toptrack"));
                record.extra[2] = trackNode(entity.value("bottomtrack"));
                record.extra[3] = nameId(entity.value("train"));
            }
            else if (cls == "func_rotating" && brushRecordOfEntity[e] >= 0)
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Rotating);
                // ("Volume", in tenths; full for none. The game asks for it
                // with a capital: the maps' own "volume" is not read, and
                // their fans play at full.)
                record.pad = moverVolumeOf(numberKey(entity, "Volume") / 10.0, 1.0);
                // As for a door: flag 4 ("Z axis") is roll, 8 ("X axis") is
                // pitch, neither is a turn about the vertical.
                record.extra[0] = (spawnflags & 4) ? kRoll : ((spawnflags & 8) ? kPitch : kYaw);
                // Degrees a second, as turns * 65536. The game's comment says
                // "default 100" and its code does not: no speed is no spin.
                double speed = std::max(0.0, numberKey(entity, "speed"));
                if (spawnflags & 2)
                    speed = -speed;
                record.speed = static_cast<fx>(std::lround(speed * 65536.0 / 360.0));
                // "fanfriction" is the share of full speed gained or lost
                // each tenth of a second, in percent; 0 is all of it.
                double friction = numberKey(entity, "fanfriction") / 100.0;
                if (friction <= 0)
                    friction = 1;
                record.extra[1] = static_cast<std::uint32_t>(
                    std::max(1L, std::lround(std::fabs(speed) * friction * 65536.0 / 360.0)));
                if (spawnflags & 1)
                    record.flags |= kLogicStartOpen;
                if (spawnflags & 16)
                    record.flags |= kLogicToggle;
                // 32: it hurts whatever touches it, by how fast it turns
                // (SF_BRUSH_HURT, HurtTouch).
                if (spawnflags & 32)
                    record.flags |= kLogicTouch;
                record.damage = static_cast<std::uint16_t>(
                    std::clamp(numberKey(entity, "dmg"), 0.0, 65535.0));
            }
            else if ((cls == "func_breakable" || cls == "func_pushable") &&
                     brushRecordOfEntity[e] >= 0)
            {
                make = true;
                const bool box = cls == "func_pushable";
                record.kind =
                    static_cast<std::uint8_t>(box ? LogicKind::Pushable : LogicKind::Breakable);
                // "health" has no default: with none, the first blow does it.
                record.extra[0] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "health")), 0L, 1000000L));
                record.extra[1] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "material")), 0L, 9L));
                if (box)
                {
                    // "friction" is how HARD it is to push: the fastest it
                    // goes is 400 less that.
                    const double friction = std::min(numberKey(entity, "friction"), 399.0);
                    record.extra[2] =
                        static_cast<std::uint32_t>(fxFromDouble(400.0 - std::max(0.0, friction)));
                    // It moves through the world as the player's standing
                    // hull or the crouched one, whichever is nearer its
                    // height: those are the hulls a map has.
                    const double height = model != nullptr ? double(model->maxs[2]) - model->mins[2] : 64;
                    record.extra[3] = height < 54 ? 1 : 0;
                    // How it floats: its buoyancy, by the area it sits on the
                    // water with (the game's CPushable::Spawn).
                    if (model != nullptr)
                        record.speed = fxFromDouble(std::max(0.0, numberKey(entity, "buoyancy")) *
                                                    (double(model->maxs[0]) - model->mins[0]) *
                                                    (double(model->maxs[1]) - model->mins[1]) * 0.0005);
                    if (spawnflags & 128)
                        record.flags |= kLogicBreaks;
                }
                else
                {
                    if (spawnflags & 1)
                        record.flags |= kLogicTriggerOnly;
                    if (spawnflags & 2)
                        record.flags |= kLogicTouch;
                    if (spawnflags & 4)
                        record.flags |= kLogicPressure;
                }
                if (spawnflags & 256)
                    record.flags |= kLogicInstant;
                // What it leaves when it breaks (an item kept where it is:
                // below, with the actors).
                if (const ItemClass *item = spawnObjectOf(entity))
                    record.pad = static_cast<std::uint16_t>(item->kind);
            }
            else if ((cls == "trigger_hurt" || cls == "trigger_push" || cls == "trigger_teleport" ||
                      cls == "trigger_gravity") &&
                     model != nullptr &&
                     (!(spawnflags & (cls == "trigger_hurt" ? 8 : (cls == "trigger_teleport" ? 2 : 0))) ||
                      (cls == "trigger_teleport" && (spawnflags & 1))))
            {
                // (Flag 8 on a trigger_hurt and 2 on a teleport: not for the
                // player. On a trigger_push 2 is something else: it starts
                // switched off. A teleport with flag 1 is for the monsters
                // too: c1a0e's sends the slaves of the cascade away.)
                if (cls == "trigger_teleport")
                {
                    if (spawnflags & 1)
                        record.flags |= kLogicMonsters;
                    if (spawnflags & 2)
                        record.flags |= kLogicNoPlayer;
                }
                record.pad = shapedVolume(bsp, model);
                for (int a = 0; a < 3; ++a)
                {
                    record.mins[a] = static_cast<std::int16_t>(
                        std::clamp(std::floor(model->mins[a] + origin[a]), -32768.0, 32767.0));
                    record.maxs[a] = static_cast<std::int16_t>(
                        std::clamp(std::ceil(model->maxs[a] + origin[a]), -32768.0, 32767.0));
                }
                if (cls == "trigger_hurt")
                {
                    make = true;
                    record.kind = static_cast<std::uint8_t>(LogicKind::TriggerHurt);
                    record.extra[0] = static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(std::lround(numberKey(entity, "dmg"))));
                    // (What kind of hurt it is: the game's DMG_* bits. A map
                    // cooked before this has none, and its hurt shows no
                    // picture of its kind.)
                    record.extra[1] = static_cast<std::uint32_t>(std::lround(numberKey(entity, "damagetype")));
                    if (spawnflags & 2)
                        record.flags |= kLogicStartOpen; // starts off
                }
                else if (cls == "trigger_gravity")
                {
                    // (The game's 0 is "as the world pulls".)
                    make = true;
                    record.kind = static_cast<std::uint8_t>(LogicKind::TriggerGravity);
                    const double gravity = numberKey(entity, "gravity");
                    record.extra[0] = static_cast<std::uint32_t>(
                        gravity > 0 ? std::clamp(std::lround(gravity * 100), 1L, 255L) : 100L);
                }
                else if (cls == "trigger_push")
                {
                    make = true;
                    record.kind = static_cast<std::uint8_t>(LogicKind::TriggerPush);
                    if (spawnflags & 2)
                        record.flags |= kLogicStartOpen; // starts off: something switches it on
                    if (spawnflags & 1)
                        record.flags |= kLogicFireOnce; // once, and gone
                    double direction[3];
                    moverDirection(entity, direction);
                    double speed = numberKey(entity, "speed");
                    if (speed == 0)
                        speed = 100;
                    for (int a = 0; a < 3; ++a)
                        record.move[a] = fxFromDouble(direction[a] * speed);
                }
                else
                {
                    // Where it sends them is another entity, found by name.
                    const std::string to(entity.value("target"));
                    for (const Entity &other : entities)
                    {
                        if (to.empty() || other.value("targetname") != to)
                            continue;
                        double at[3] = {0, 0, 0}, angles[3] = {0, 0, 0};
                        other.vec3("origin", at);
                        if (!other.vec3("angles", angles))
                            angles[1] = numberKey(other, "angle");
                        make = true;
                        record.kind = static_cast<std::uint8_t>(LogicKind::TriggerTeleport);
                        // The destination is where their FEET go: the middle
                        // of a standing player is 36 units above that.
                        record.move[0] = fxFromDouble(at[0]);
                        record.move[1] = fxFromDouble(at[1]);
                        record.move[2] = fxFromDouble(at[2] + 36);
                        record.extra[0] = static_cast<std::uint16_t>(
                            std::lround(angles[1] * 65536.0 / 360.0));
                        break;
                    }
                }
            }
            else if (cls == "func_wall_toggle")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::WallToggle);
            }
            else if (cls == "trigger_once" || cls == "trigger_multiple")
            {
                // A trigger that ignores the player is for a box that is
                // pushed into it (flag 4), or for the monsters (flag 1).
                if (model != nullptr && (!(spawnflags & kTriggerNoClients) || (spawnflags & 5)))
                {
                    make = true;
                    if (spawnflags & 4)
                        record.flags |= kLogicPushables;
                    if (spawnflags & 1)
                        record.flags |= kLogicMonsters;
                    if (spawnflags & kTriggerNoClients)
                        record.flags |= kLogicNoPlayer;
                    const bool once = cls == "trigger_once";
                    record.kind = static_cast<std::uint8_t>(once ? LogicKind::TriggerOnce
                                                                 : LogicKind::TriggerMultiple);
                    double wait = numberKey(entity, "wait");
                    if (wait == 0)
                        wait = 0.2;
                    record.waitMs = once ? -1 : millis(wait);
                    for (int a = 0; a < 3; ++a)
                    {
                        record.mins[a] = static_cast<std::int16_t>(
                            std::clamp(std::floor(model->mins[a] + origin[a]), -32768.0, 32767.0));
                        record.maxs[a] = static_cast<std::int16_t>(
                            std::clamp(std::ceil(model->maxs[a] + origin[a]), -32768.0, 32767.0));
                    }
                }
            }
            else if (cls == "func_ladder" && model != nullptr)
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Ladder);
                // (One that is not a box -- leaning, or turned -- keeps its
                // model: whether the player is on it is its hull's to say,
                // and which way it faces its own faces'.)
                record.pad = shapedVolume(bsp, model);
                for (int a = 0; a < 3; ++a)
                {
                    record.mins[a] = static_cast<std::int16_t>(
                        std::clamp(std::floor(model->mins[a] + origin[a]), -32768.0, 32767.0));
                    record.maxs[a] = static_cast<std::int16_t>(
                        std::clamp(std::ceil(model->maxs[a] + origin[a]), -32768.0, 32767.0));
                }
            }
            else if (cls == "func_friction" && model != nullptr)
            {
                // (Its "modifier": the percent of the world's friction the
                // ground has in it.)
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Friction);
                record.pad = shapedVolume(bsp, model);
                record.extra[0] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "modifier", 100)), 0L, 100L));
                for (int a = 0; a < 3; ++a)
                {
                    record.mins[a] = static_cast<std::int16_t>(
                        std::clamp(std::floor(model->mins[a] + origin[a]), -32768.0, 32767.0));
                    record.maxs[a] = static_cast<std::int16_t>(
                        std::clamp(std::ceil(model->maxs[a] + origin[a]), -32768.0, 32767.0));
                }
            }
            else if (cls == "trigger_changelevel" && !entity.value("map").empty())
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::ChangeLevel);
                record.extra[0] = stringOffset(entity.value("map"));
                record.extra[1] = hashOf(entity.value("landmark"));
                record.extra[2] = hashOf(entity.value("changetarget"));
                record.extra[3] =
                    static_cast<std::uint32_t>(std::max(0, millis(numberKey(entity, "changedelay"))));
                // Flag 2: only when fired by name. Otherwise walking in does
                // it, name or no name.
                if (!(spawnflags & 2) && model != nullptr)
                    record.flags |= kLogicTouch;
                if (model != nullptr)
                {
                    for (int a = 0; a < 3; ++a)
                    {
                        record.mins[a] = static_cast<std::int16_t>(
                            std::clamp(std::floor(model->mins[a] + origin[a]), -32768.0, 32767.0));
                        record.maxs[a] = static_cast<std::int16_t>(
                            std::clamp(std::ceil(model->maxs[a] + origin[a]), -32768.0, 32767.0));
                    }
                }
            }
            else if (cls == "trigger_transition" && model != nullptr && !entity.value("targetname").empty())
            {
                // Where the player has to be for the way out by the landmark
                // of its name to work (LogicKind::Transition).
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Transition);
                record.extra[0] = hashOf(entity.value("targetname"));
                for (int a = 0; a < 3; ++a)
                {
                    record.mins[a] = static_cast<std::int16_t>(
                        std::clamp(std::floor(model->mins[a] + origin[a]), -32768.0, 32767.0));
                    record.maxs[a] = static_cast<std::int16_t>(
                        std::clamp(std::ceil(model->maxs[a] + origin[a]), -32768.0, 32767.0));
                }
            }
            else if (cls == "trigger_counter")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Counter);
                const double count = numberKey(entity, "count");
                record.extra[0] = static_cast<std::uint32_t>(count >= 1 ? std::lround(count) : 2);
            }
            else if (cls == "trigger_changetarget")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::ChangeTarget);
                record.extra[0] = nameId(entity.value("m_iszNewTarget"));
            }
            else if (cls == "player_weaponstrip")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::WeaponStrip);
            }
            else if (cls == "player_loadsaved")
            {
                // The screen goes as dark as it says (the game's renderamt,
                // over "duration", held), and "loadtime" after, the last save.
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::LoadSaved);
                record.extra[0] = static_cast<std::uint32_t>(std::max(0, millis(numberKey(entity, "duration", 2))));
                record.extra[1] = static_cast<std::uint32_t>(std::max(0, millis(numberKey(entity, "loadtime"))));
                record.extra[2] = static_cast<std::uint32_t>(std::clamp(numberKey(entity, "renderamt", 255), 0.0, 255.0));
                const std::string said(entity.value("message"));
                record.extra[3] = said.empty() ? 0xFFFFFFFFu : addMessage(said, 0);
            }
            else if (cls == "trigger_endsection")
            {
                // Walked into (flag 1: only used), or used: the game is over,
                // as far as this part of it goes.
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::EndSection);
                if (model != nullptr && !(spawnflags & 1))
                {
                    record.flags |= kLogicTouch;
                    for (int a = 0; a < 3; ++a)
                    {
                        record.mins[a] = static_cast<std::int16_t>(
                            std::clamp(std::floor(model->mins[a] + origin[a]), -32768.0, 32767.0));
                        record.maxs[a] = static_cast<std::int16_t>(
                            std::clamp(std::ceil(model->maxs[a] + origin[a]), -32768.0, 32767.0));
                    }
                }
            }
            else if (cls == "trigger_monsterjump" && model != nullptr)
            {
                // The game throws at 200 along its angle and 150 up, whatever
                // the map says. Named, it waits to be switched on.
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::MonsterJump);
                double direction[3];
                moverDirection(entity, direction);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(direction[a] * 200 + (a == 2 ? 150 : 0));
                if (named)
                    record.flags |= kLogicStartOpen;
                for (int a = 0; a < 3; ++a)
                {
                    record.mins[a] = static_cast<std::int16_t>(
                        std::clamp(std::floor(model->mins[a] + origin[a]), -32768.0, 32767.0));
                    record.maxs[a] = static_cast<std::int16_t>(
                        std::clamp(std::ceil(model->maxs[a] + origin[a]), -32768.0, 32767.0));
                }
            }
            else if (cls == "trigger_camera")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Camera);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(origin[a]);
                record.extra[0] = static_cast<std::uint32_t>(spawnflags & 7);
                double angles[3];
                entityAngles(entity, angles);
                record.extra[1] = static_cast<std::uint32_t>(turnsFromDegrees(angles[1]));
                record.waitMs = std::max(0, millis(numberKey(entity, "wait")));
            }
            else if (cls == "speaker")
            {
                // The announcements: a group of the game's sentences by its
                // number ("preset"), or the map's own ("message": a group, or
                // one sentence after a '!').
                static const char *const kPresets[] = {"C1A0_", "C1A1_", "C1A2_", "C1A3_", "C1A4_", "C2A1_",
                                                       "C2A2_", "C2A3_", "C2A4_", "C2A5_", "C3A1_", "C3A2_"};
                const int preset = static_cast<int>(numberKey(entity, "preset"));
                std::string group = preset >= 1 && preset <= 12 ? kPresets[preset - 1]
                                                                 : std::string(entity.value("message"));
                if (!group.empty() && group[0] == '!')
                {
                    const int one = sentenceIndex(group.substr(1));
                    if (one >= 0)
                        speakerLines.push_back(static_cast<std::uint16_t>(one));
                }
                else
                {
                    for (int n = 0, misses = 0; n < 64 && misses < 3 && !group.empty(); ++n)
                    {
                        const int line = sentenceIndex(group + std::to_string(n));
                        misses = line < 0 ? misses + 1 : 0;
                        if (line >= 0)
                            speakerLines.push_back(static_cast<std::uint16_t>(line));
                    }
                }
                if (!speakerLines.empty())
                {
                    make = true;
                    record.kind = static_cast<std::uint8_t>(LogicKind::Speaker);
                    for (int a = 0; a < 3; ++a)
                        record.move[a] = fxFromDouble(origin[a]);
                    // (Its "health" is how loud, in tenths.)
                    record.extra[0] = static_cast<std::uint32_t>(
                        std::clamp(std::lround(numberKey(entity, "health", 10) * 0.1 * 127), 1L, 127L));
                    if (!(spawnflags & 1))
                        record.flags |= kLogicStartOpen;
                }
            }
            else if (cls == "button_target")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::ButtonTarget);
                if (spawnflags & 1)
                    record.flags |= kLogicUseOnly;
                if (spawnflags & 2)
                    record.flags |= kLogicStartOpen;
            }
            else if (cls == "env_blood")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Blood);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(origin[a]);
                record.extra[0] = numberKey(entity, "color") != 0 ? 1 : 0;
                record.extra[1] = static_cast<std::uint32_t>(std::clamp(numberKey(entity, "amount", 100), 0.0, 10000.0));
                record.extra[2] = static_cast<std::uint32_t>(spawnflags & 0xF);
                double angles[3];
                entityAngles(entity, angles);
                record.extra[3] = turnsFromDegrees(angles[1]) | (turnsFromDegrees(angles[0]) << 16);
            }
            else if (cls == "trigger_autosave" && model != nullptr)
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::AutoSave);
                for (int a = 0; a < 3; ++a)
                {
                    record.mins[a] = static_cast<std::int16_t>(
                        std::clamp(std::floor(model->mins[a] + origin[a]), -32768.0, 32767.0));
                    record.maxs[a] = static_cast<std::int16_t>(
                        std::clamp(std::ceil(model->maxs[a] + origin[a]), -32768.0, 32767.0));
                }
            }
            else if (cls == "env_global" && !entity.value("globalstate").empty())
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::EnvGlobal);
                record.extra[0] = hashOf(entity.value("globalstate"));
                record.extra[1] = static_cast<std::uint32_t>(
                    std::clamp(numberKey(entity, "triggermode"), 0.0, 3.0));
                record.extra[2] = static_cast<std::uint32_t>(
                    std::clamp(numberKey(entity, "initialstate"), 0.0, 2.0));
                if (spawnflags & 1)
                    record.flags |= kLogicStartOpen;
            }
            else if (cls == "trigger_auto" || cls == "trigger_relay")
            {
                make = true;
                const bool automatic = cls == "trigger_auto";
                record.kind = static_cast<std::uint8_t>(automatic ? LogicKind::TriggerAuto
                                                                  : LogicKind::TriggerRelay);
                // "triggerstate": 0 off, 1 on, 2 toggle.
                const int state = static_cast<int>(numberKey(entity, "triggerstate"));
                record.useType = state == 0 ? kUseOff : (state == 1 ? kUseOn : kUseToggle);
                // (Both remove themselves on "1": for a trigger_auto that
                // is only what a map come back to remembers.)
                if (spawnflags & kRelayFireOnce)
                    record.flags |= kLogicFireOnce;
            }
            else if (isScript(cls))
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Script);
                const std::string who = lowered(entity.value("m_iszEntity"));
                // An actor by name -- or, if nothing is called that, the
                // nearest actor of that class.
                bool byName = false;
                for (const Entity &other : entities)
                    byName = byName || (!who.empty() && lowered(other.value("targetname")) == who &&
                                        actorOf(other).is);
                // (A name nobody in this map has is still a name: its owner
                // may be someone who walks in from the map next door.)
                if (byName || kindOfClass(who) < 0)
                    record.extra[0] = nameId(who);
                else
                    record.damage = static_cast<std::uint16_t>(kindOfClass(who) + 1);
                double radius = numberKey(entity, "m_flRadius");
                if (radius <= 0)
                    radius = 512;
                record.waitMs = static_cast<std::int32_t>(std::min(radius, 32000.0));
                const std::string idle = lowered(entity.value("m_iszIdle"));
                const std::string play = lowered(entity.value("m_iszPlay"));
                record.extra[1] = idle.empty() ? 0 : nameHash(idle.c_str());
                record.extra[2] = play.empty() ? 0 : nameHash(play.c_str());
                record.extra[3] = static_cast<std::uint32_t>(
                    std::clamp(static_cast<int>(numberKey(entity, "m_fMoveTo")), 0, 5));
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(origin[a]);
                double angles[3] = {0, 0, 0};
                if (!entity.vec3("angles", angles))
                    angles[1] = numberKey(entity, "angle");
                record.speed = static_cast<fx>(
                    static_cast<std::uint16_t>(std::lround(angles[1] * 65536.0 / 360.0)));
                // With no name it has nothing to wait for and plays as soon
                // as it has its actor; with an idle clip it takes its actor
                // at once and holds it there until used.
                if (!named || !idle.empty())
                    record.flags |= kLogicStartOpen;
                if (named)
                    record.flags |= kLogicUseOnly;
                if (spawnflags & 4)
                    record.flags |= kLogicToggle; // can be played again
                // The rest of what the game's scripts are told (scripted.h).
                record.pad = static_cast<std::uint16_t>(
                    (spawnflags & (kScriptLeaveCorpse | kScriptNoInterrupt | kScriptOverrideState |
                                   kScriptNoScriptMovement)) |
                    (cls == "aiscripted_sequence" ? kScriptAi : 0) |
                    (cls == "aiscripted_sequence" && numberKey(entity, "m_iFinishSchedule") == 1 ? kScriptAmbush
                                                                                                   : 0));
            }
            else if (cls == "monstermaker" && actorOf(entity).is)
            {
                // Its children's places are filled in below, with the actors
                // (extra[0] and [1]).
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Maker);
                const double delay = numberKey(entity, "delay");
                record.waitMs = millis(delay > 0.1 ? delay : 1.0);
                const int count = static_cast<int>(numberKey(entity, "monstercount"));
                record.extra[2] = count > 0 ? static_cast<std::uint32_t>(count) : 0;
                if (spawnflags & 4)
                    record.flags |= kLogicToggle; // one each time it is used
                else if (spawnflags & 1)
                    record.flags |= kLogicStartOpen;
            }
            else if (cls == "func_conveyor" && brushRecordOfEntity[e] >= 0)
            {
                // A belt: its way and its speed, for what stands on it and
                // for its texture.
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Conveyor);
                double way[3];
                moverDirection(entity, way);
                const double speed = numberKey(entity, "speed", 100);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = static_cast<fx>(std::lround(way[a] * speed * 4096.0));
                record.speed = static_cast<fx>(std::lround(speed * 4096.0));
                if (spawnflags & 1)
                    record.extra[0] |= 1; // "no push"
            }
            else if ((cls == "func_healthcharger" || cls == "func_recharge") &&
                     brushRecordOfEntity[e] >= 0)
            {
                make = true;
                const bool suit = cls == "func_recharge";
                record.kind = static_cast<std::uint8_t>(suit ? LogicKind::SuitCharger
                                                             : LogicKind::HealthCharger);
                // (What it holds is the game's level's: chargerCapacity. This,
                // the easy level's, is for a build from before there was one.)
                record.extra[0] = suit ? 75 : 50;
            }
            else if (cls == "ambient_generic")
            {
                // Flags: 1 heard everywhere, 2 / 4 / 8 a small, medium or
                // large radius; 16 starts silent; 32 is played once each
                // time it is used rather than switched on and off.
                const std::string message(entity.value("message"));
                const bool spoken = !message.empty() && message[0] == '!';
                const bool looped = !spoken && !(spawnflags & 32);
                const int index = spoken ? sentenceIndex(message)
                                         : static_cast<int>(soundIndex(message, looped));
                if (index >= 0 && index != kNoSound)
                {
                    make = true;
                    record.kind = static_cast<std::uint8_t>(LogicKind::Ambient);
                    for (int a = 0; a < 3; ++a)
                        record.move[a] = fxFromDouble(origin[a]);
                    record.extra[0] = static_cast<std::uint32_t>(index);
                    // (Flags 1, 2, 4, 8: everywhere, small, medium, large;
                    // none is the game's medium.)
                    record.extra[1] = (spawnflags & 1)   ? kRadiusEverywhere
                                      : (spawnflags & 2) ? kRadiusSmall
                                      : (spawnflags & 8) && !(spawnflags & 4) ? kRadiusLarge
                                                         : kRadiusMedium;
                    const double volume = numberKey(entity, "health", 10);
                    record.extra[2] = static_cast<std::uint32_t>(
                        std::clamp(std::lround(volume * 12.7), 0L, 127L));
                    record.extra[3] = spoken ? 1 : 0;
                    // How fast it is played: "pitch", in percent (100, or
                    // none, is as it was recorded).
                    const long pitch = std::lround(numberKey(entity, "pitch", 100));
                    record.pad = pitch >= 30 && pitch <= 255 && pitch != 100 ? static_cast<std::uint16_t>(pitch) : 0;
                    if (looped)
                    {
                        record.flags |= kLogicToggle;
                        if (!(spawnflags & 16))
                            record.flags |= kLogicStartOpen;
                    }
                }
            }
            else if (cls == "env_message")
            {
                // Writes on the screen what the game's titles.txt has under
                // its "message" -- or, if it has nothing by that name, the
                // name itself.
                const std::string wanted(entity.value("message"));
                if (!wanted.empty())
                {
                    make = true;
                    record.kind = static_cast<std::uint8_t>(LogicKind::Message);
                    if (spawnflags & 1)
                        record.flags |= kLogicFireOnce;
                    // (The Hazard Course's lessons are all named HZ...)
                    const bool lesson = wanted.size() > 2 && (wanted[0] == 'H' || wanted[0] == 'h') &&
                                        (wanted[1] == 'Z' || wanted[1] == 'z');
                    record.extra[0] = addMessage(wanted, lesson ? kMessageLesson : 0);
                }
            }
            else if (cls == "trigger_cdaudio" && model != nullptr)
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Music);
                record.flags |= kLogicTouch;
                record.extra[0] = static_cast<std::uint32_t>(std::max(0.0, numberKey(entity, "health")));
                for (int a = 0; a < 3; ++a)
                {
                    record.mins[a] = static_cast<std::int16_t>(
                        std::clamp(std::floor(model->mins[a] + origin[a]), -32768.0, 32767.0));
                    record.maxs[a] = static_cast<std::int16_t>(
                        std::clamp(std::ceil(model->maxs[a] + origin[a]), -32768.0, 32767.0));
                }
            }
            else if (cls == "target_cdaudio")
            {
                // A point with a radius: coming within it starts the track.
                // (Or, the map's own start track, only when it is fired:
                // addStartMusic.)
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Music);
                record.extra[0] = static_cast<std::uint32_t>(std::max(0.0, numberKey(entity, "health")));
                if (numberKey(entity, "_untouched") == 0)
                    record.flags |= kLogicTouch;
                const double radius = std::max(16.0, numberKey(entity, "radius", 128));
                for (int a = 0; a < 3; ++a)
                {
                    record.mins[a] = static_cast<std::int16_t>(
                        std::clamp(std::floor(origin[a] - radius), -32768.0, 32767.0));
                    record.maxs[a] = static_cast<std::int16_t>(
                        std::clamp(std::ceil(origin[a] + radius), -32768.0, 32767.0));
                }
            }
            else if (cls == "scripted_sentence")
            {
                // Somebody says something (when there is someone able to),
                // and the target fires as they start (scripted.cpp's
                // CScriptedSentence).
                make = true;
                const int index = sentenceIndex(std::string(entity.value("sentence")));
                if (index >= 0)
                {
                    record.kind = static_cast<std::uint8_t>(LogicKind::Sentence);
                    for (int a = 0; a < 3; ++a)
                        record.move[a] = fxFromDouble(origin[a]);
                    record.extra[0] = static_cast<std::uint32_t>(index);
                    record.extra[1] = static_cast<std::uint32_t>(
                        std::clamp(std::lround(numberKey(entity, "duration") * 1000.0), 0L, 600000L));
                    // (The game's volume is tenths; none at all is full.)
                    const double volume = numberKey(entity, "volume") * 0.1;
                    record.extra[2] = static_cast<std::uint32_t>(
                        volume <= 0 ? 127L : std::clamp(std::lround(volume * 127.0), 0L, 127L));
                    record.extra[3] = static_cast<std::uint32_t>(
                        std::clamp(std::lround(numberKey(entity, "refire") * 1000.0), 0L, 600000L));
                    record.waitMs = static_cast<std::int32_t>(std::clamp(numberKey(entity, "radius"), 0.0, 32000.0));
                    // How far it carries: the game's attenuations (IDLE,
                    // STATIC, NORM, NONE), as distances.
                    static const std::int16_t kCarries[] = {kRadiusSmall, kRadiusMedium, kRadiusLarge,
                                                            kRadiusEverywhere};
                    const int attenuation = static_cast<int>(numberKey(entity, "attenuation"));
                    record.mins[0] = kCarries[attenuation >= 1 && attenuation <= 3 ? attenuation : 0];
                    const std::string listener = lowered(entity.value("listener"));
                    record.mins[1] = static_cast<std::int16_t>(listener.empty() || listener == "player" ? 0 : nameId(listener));
                    record.mins[2] = listener == "player" ? 1 : 0;
                    record.maxs[0] = static_cast<std::int16_t>(spawnflags & 15);
                    // Who says it: someone by name, or the nearest of a
                    // class within the radius.
                    const std::string speaker = lowered(entity.value("entity"));
                    if (kindOfClass(speaker) >= 0)
                        record.damage = static_cast<std::uint16_t>(kindOfClass(speaker) + 1);
                    else if (!speaker.empty())
                        record.netName = nameId(speaker);
                    if (!named)
                        record.flags |= kLogicStartOpen;
                }
                else
                {
                    // A line the book does not have: only the target fires.
                    record.kind = static_cast<std::uint8_t>(LogicKind::TriggerRelay);
                }
            }
            else if (cls == "env_sprite" || cls == "env_glow" || cls == "cycler_sprite")
            {
                // The game's render modes: 3 (glow) and 5 (additive) are
                // light; in the others the sprite's own file says.
                const int mode = static_cast<int>(numberKey(entity, "rendermode"));
                const std::uint16_t sprite =
                    spriteIndex(std::string(entity.value("model")), mode == 3 || mode == 5);
                if (sprite != kNoSprite)
                {
                    make = true;
                    record.kind = static_cast<std::uint8_t>(LogicKind::Sprite);
                    for (int a = 0; a < 3; ++a)
                        record.move[a] = fxFromDouble(origin[a]);
                    record.extra[0] = sprite;
                    record.extra[1] = static_cast<std::uint32_t>(
                        std::clamp(std::lround(numberKey(entity, "framerate", 10) * 256), 0L, 65535L));
                    record.extra[2] = look(mode == 0 ? 255.0 : numberKey(entity, "renderamt", 255)) |
                                      (static_cast<std::uint32_t>(std::clamp(
                                           std::lround(numberKey(entity, "scale", 1) * 16), 1L, 255L))
                                       << 24);
                    // A glow is always there. A sprite is if it says so (1),
                    // or if nothing could ever switch it on. A cycler_sprite
                    // is always there, and plays on and on; used, its frames
                    // stop, or start again (bit 2).
                    const bool glow = cls == "env_glow";
                    const bool cycler = cls == "cycler_sprite";
                    // (Bit 1: a glow, which the game draws over whatever
                    // it sits in -- a lamp's glass, as a rule. Bit 3: one
                    // whose render fx is 14, kRenderFxNoDissipation -- as
                    // big and as bright at any distance; the others fade
                    // and keep their size on the screen: glowFalloff.)
                    const bool keeps = static_cast<int>(numberKey(entity, "renderfx")) == 14;
                    record.extra[3] = (!glow && !cycler && (spawnflags & 2) ? 1 : 0) | (glow || mode == 3 ? 2 : 0) |
                                      (cycler ? 4 : 0) | (keeps ? 8 : 0);
                    if (glow || cycler || (spawnflags & 1) || !named)
                        record.flags |= kLogicStartOpen;
                }
            }
            else if (cls == "env_beam" || cls == "env_laser")
            {
                // An end of it: nothing (no name, or one nothing answers to);
                // a place, or several to pick from (entities that are only
                // places: the game's rule for which); or something that
                // moves -- a brush, a monster -- which the beam is to follow.
                struct End
                {
                    int kind = 0; ///< 0 none, 1 places, 2 follows
                    std::vector<std::array<double, 3>> places;
                    std::uint16_t name = 0;
                };
                auto endOf = [&](std::string_view name) -> End
                {
                    End end;
                    if (name.empty())
                        return end;
                    for (const Entity &other : entities)
                    {
                        if (other.value("targetname") != name)
                            continue;
                        const std::string its(other.className());
                        const std::string text(other.value("model"));
                        const bool place = text.empty() || its == "info_target" || its == "info_landmark" ||
                                           its == "path_corner";
                        if (!place)
                        {
                            end.kind = 2;
                            end.name = nameId(name);
                            end.places.clear();
                            return end;
                        }
                        double at[3] = {0, 0, 0};
                        if (!other.vec3("origin", at))
                            continue;
                        end.kind = 1;
                        end.places.push_back(std::array<double, 3>{at[0], at[1], at[2]});
                    }
                    return end;
                };
                const bool laser = cls == "env_laser";
                End start, finish;
                if (laser)
                {
                    start.kind = 1;
                    start.places.push_back(std::array<double, 3>{origin[0], origin[1], origin[2]});
                    finish = endOf(entity.value("LaserTarget"));
                }
                else
                {
                    start = endOf(entity.value("LightningStart"));
                    finish = endOf(entity.value("LightningEnd"));
                    // (With neither end the game strikes about its own
                    // place; with only an end, from there.)
                    if (start.kind == 0 && finish.kind != 0)
                        std::swap(start, finish);
                    if (start.kind == 0 && entity.value("LightningStart").empty() &&
                        entity.value("LightningEnd").empty())
                    {
                        start.kind = 1;
                        start.places.push_back(std::array<double, 3>{origin[0], origin[1], origin[2]});
                    }
                }
                const std::uint16_t sprite = spriteIndex(std::string(entity.value("texture")), true);
                // A name that nothing answers to (the maps have a few): no
                // beam. An end left out on purpose: struck at random.
                const bool lost = (!entity.value(laser ? "LaserTarget" : "LightningEnd").empty() && finish.kind == 0 &&
                                   !(start.kind != 0 && entity.value("LightningStart").empty())) ||
                                  (!laser && !entity.value("LightningStart").empty() && start.kind == 0);
                if (start.kind != 0 && !lost && sprite != kNoSprite)
                {
                    make = true;
                    record.kind = static_cast<std::uint8_t>(LogicKind::Beam);
                    const std::array<double, 3> from =
                        start.kind == 1 ? start.places[0] : std::array<double, 3>{origin[0], origin[1], origin[2]};
                    const std::array<double, 3> to =
                        finish.kind == 1 ? finish.places[0] : from;
                    for (int a = 0; a < 3; ++a)
                    {
                        record.move[a] = fxFromDouble(from[a]);
                        record.mins[a] = static_cast<std::int16_t>(std::clamp(std::lround(to[a]), -32768L, 32767L));
                    }
                    beamStart = start.kind == 2 ? start.name : std::uint16_t(0);
                    record.netName = finish.kind == 2 ? finish.name : std::uint16_t(0);
                    // The places its end is picked from, if it has more
                    // than one; the reach of a strike at random, if it has
                    // no end at all.
                    if (finish.kind == 1 && finish.places.size() > 1)
                        beamPlaces = finish.places;
                    std::uint16_t how = 0;
                    if (finish.kind == 0)
                    {
                        how |= kBeamRandom;
                        record.maxs[0] = static_cast<std::int16_t>(
                            std::clamp(std::lround(numberKey(entity, "Radius", 256)), 16L, 4096L));
                    }
                    if (!laser && (spawnflags & 8))
                        how |= kBeamRing;
                    // (16 and 32: sparks at its start, at its end.)
                    if (spawnflags & 16)
                        how |= kBeamSparkStart;
                    if (spawnflags & 32)
                        how |= kBeamSparkEnd;
                    record.maxs[1] = static_cast<std::int16_t>(how);
                    record.extra[0] = sprite;
                    const double width = numberKey(entity, laser ? "width" : "BoltWidth", 20);
                    const double noise = laser ? 0 : numberKey(entity, "NoiseAmplitude");
                    record.extra[1] = static_cast<std::uint32_t>(std::clamp(std::lround(width), 1L, 65535L)) |
                                      (static_cast<std::uint32_t>(std::clamp(std::lround(noise), 0L, 255L)) << 16);
                    record.extra[2] = look(numberKey(entity, "renderamt", 255));
                    const double life = laser ? 0 : numberKey(entity, "life");
                    const double again = laser ? 0 : numberKey(entity, "StrikeTime");
                    record.extra[3] =
                        static_cast<std::uint32_t>(std::clamp(std::lround(life * 1000), 0L, 65535L)) |
                        (static_cast<std::uint32_t>(std::clamp(std::lround(std::fabs(again) * 1000), 0L, 65535L))
                         << 16);
                    record.damage = static_cast<std::uint16_t>(
                        std::clamp(std::lround(numberKey(entity, "damage")), 0L, 65535L));
                    // Flags: 1 starts on; 2 (a beam's) is switched on and
                    // off. A laser always is.
                    if ((spawnflags & 1) || !named)
                        record.flags |= kLogicStartOpen;
                    if (laser || (spawnflags & 2) || life == 0)
                        record.flags |= kLogicToggle;
                    isBeam = true;
                }
            }
            else if (cls == "env_explosion")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Explosion);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(origin[a]);
                record.extra[0] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "iMagnitude", 100)), 0L, 1000L));
                record.extra[1] = static_cast<std::uint32_t>(spawnflags);
            }
            else if (cls == "env_shake")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Shake);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(origin[a]);
                record.extra[0] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "amplitude", 4) * 256), 0L, 16L * 256));
                record.extra[1] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "duration", 1) * 1000), 0L, 60000L));
                // (Flag 1: felt everywhere.)
                record.extra[2] = (spawnflags & 1) ? 0
                                                   : static_cast<std::uint32_t>(std::clamp(
                                                         std::lround(numberKey(entity, "radius", 500)), 1L, 65535L));
            }
            else if (cls == "env_spark")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Spark);
                for (int a = 0; a < 3; ++a)
                    record.move[a] = fxFromDouble(origin[a]);
                // (No delay, or none above 0: the game's 1.5 s.)
                const double maxDelay = numberKey(entity, "MaxDelay", 0);
                record.waitMs = static_cast<std::int32_t>(
                    std::clamp(std::lround((maxDelay > 0 ? maxDelay : 1.5) * 1000), 100L, 60000L));
                // Flags: 32 is switched on and off, 64 starts on. Without 32
                // it sparks from the start and using it does nothing; with
                // 32 and not 64 it waits to be used, even if nothing can.
                if (spawnflags & 32)
                    record.flags |= kLogicToggle;
                if ((spawnflags & 64) || !(spawnflags & 32))
                    record.flags |= kLogicStartOpen;
                // Its six sounds, one picked each time (two to a word of
                // extra[0..2], extra[3] how many); and the one a build that
                // reads only `move` plays.
                for (int n = 0; n < 6; ++n)
                    record.extra[n / 2] |= static_cast<std::uint32_t>(numbered("buttons/spark", n + 1, 6))
                                           << (n % 2 * 16);
                record.extra[3] = 6;
                heardSpark = soundIndex("buttons/spark5.wav", false);
            }
            else if (cls == "env_shooter" || cls == "gibshooter")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Shooter);
                double angles[3] = {0, 0, 0};
                if (!entity.vec3("angles", angles))
                    angles[1] = numberKey(entity, "angle");
                // (The game's "up" and "down" for an angle of -1 and -2.)
                double way[3];
                if (angles[1] == -1 && angles[0] == 0)
                    way[0] = way[1] = 0, way[2] = 1;
                else if (angles[1] == -2 && angles[0] == 0)
                    way[0] = way[1] = 0, way[2] = -1;
                else
                {
                    const double pitch = angles[0] * M_PI / 180.0, yaw = angles[1] * M_PI / 180.0;
                    way[0] = std::cos(pitch) * std::cos(yaw);
                    way[1] = std::cos(pitch) * std::sin(yaw);
                    way[2] = -std::sin(pitch);
                }
                for (int a = 0; a < 3; ++a)
                {
                    record.move[a] = fxFromDouble(origin[a]);
                    record.mins[a] = static_cast<std::int16_t>(std::lround(way[a] * 1000));
                }
                record.extra[0] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "m_iGibs", 3)), 1L, 32L));
                record.extra[1] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "delay") * 1000), 0L, 10000L));
                record.extra[2] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "m_flVelocity", 200)), 0L, 2000L));
                record.extra[3] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "m_flGibLife", 4) * 1000), 500L, 8000L));
                if (spawnflags & 1)
                    record.flags |= kLogicToggle;
                delayIsOwn = true;
                // What it throws: pieces of a model the map has had cooked,
                // or a sprite.
                const std::string thrown = cls == "gibshooter" ? "models/hgibs.mdl" : shootModel(entity);
                if (thrown.size() > 4 && thrown.substr(thrown.size() - 4) == ".mdl")
                {
                    const auto found = std::find(models.begin(), models.end(), modelNameOf(thrown));
                    if (found != models.end())
                        record.damage = static_cast<std::uint16_t>(found - models.begin() + 1);
                }
                else if (!thrown.empty())
                {
                    const std::uint16_t sprite = spriteIndex(thrown, false);
                    if (sprite != kNoSprite)
                        record.maxs[0] = static_cast<std::int16_t>(sprite + 1);
                }
            }
            else if (cls == "env_render")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Render);
                record.extra[0] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "renderamt")), 0L, 255L));
                record.extra[1] = static_cast<std::uint32_t>(std::max(0.0, numberKey(entity, "rendermode")));
                // (Flag 2: it leaves the amount as it is; then there is
                // nothing here for it to do.)
                if (spawnflags & 2)
                    make = false;
                renderTarget = true;
            }
            else if (cls == "env_fade")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Fade);
                record.extra[0] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "duration", 1) * 1000), 0L, 60000L));
                record.extra[1] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "holdtime") * 1000), 0L, 60000L));
                record.extra[2] = static_cast<std::uint32_t>(
                    std::clamp(std::lround(numberKey(entity, "renderamt", 255)), 0L, 255L));
                // Fade From; and its colour, as RGB15 (bit 31: it has one --
                // the DS was once given none, and read black).
                std::uint32_t colour = 0;
                double rgb[3];
                if (entity.vec3("rendercolor", rgb))
                {
                    for (int k = 0; k < 3; ++k)
                        colour |= static_cast<std::uint32_t>(std::clamp(std::lround(rgb[k]), 0L, 255L) >> 3) << (5 * k);
                }
                record.extra[3] = static_cast<std::uint32_t>(spawnflags & 1) | colour << 8 | 0x80000000u;
                // (Bit 1: a fade from that is held before it lifts -- the
                // dark a map begins in: addStartDark.)
                if (numberKey(entity, "_holdfirst") != 0)
                    record.extra[3] |= 2;
            }
            else if (cls == "multi_manager")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::MultiManager);
                if (spawnflags & kManagerThread)
                    record.flags |= kLogicThread;
                // Every key that is not one of the entity's own is a target,
                // and its value is how long after the manager is used it
                // fires. The editor writes a repeated name as "name#1".
                static const char *const kOwn[] = {"classname", "targetname", "origin",
                                                   "spawnflags", "angles", "angle", "wait",
                                                   "delay",     "master", "killtarget"};
                for (const auto &pair : entity.pairs)
                {
                    bool own = false;
                    for (const char *key : kOwn)
                        own = own || pair.first == key;
                    if (own)
                        continue;
                    std::string target = pair.first;
                    const std::size_t hash = target.find('#');
                    if (hash != std::string::npos)
                        target.resize(hash);
                    const double delay = std::atof(pair.second.c_str());
                    outputs.push_back(ManagerOutput{
                        nameId(target), static_cast<std::uint32_t>(std::max(0, millis(delay)))});
                }
                std::stable_sort(outputs.begin(), outputs.end(),
                                 [](const ManagerOutput &a, const ManagerOutput &b)
                                 { return a.delayMs < b.delayMs; });
            }
            else if (cls == "multisource")
            {
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::MultiSource);
            }
            else if (brushRecordOfEntity[e] >= 0 && named)
            {
                // A brush that does nothing but can be named: so that a
                // killtarget can take it away.
                make = true;
                record.kind = static_cast<std::uint8_t>(LogicKind::Static);
                // It stands there and can be taken away, but whatever its
                // class does is still missing.
                if (!isAccountedFor(cls))
                    ++out.unsupported[cls];
            }

            if (!make)
            {
                if (!isAccountedFor(cls) && brushRecordOfEntity[e] < 0)
                    ++out.unsupported[cls];
                else if (!isAccountedFor(cls) && cls.rfind("func_", 0) == 0)
                    ++out.unsupported[cls]; // drawn and solid, but its behaviour is missing
                continue;
            }

            // An env_global's "globalstate" is the state it SETS; on a
            // trigger_auto or a multisource it is one that has to be on.
            if (record.kind != static_cast<std::uint8_t>(LogicKind::EnvGlobal))
                record.globalState = hashOf(entity.value("globalstate"));
            record.name = nameId(entity.value("targetname"));
            // A multi_manager's "targets" are its keys, not a target key; and
            // a train's "target" is the corner it starts at, not something it
            // fires.
            if (record.kind != static_cast<std::uint8_t>(LogicKind::MultiManager) &&
                record.kind != static_cast<std::uint8_t>(LogicKind::Train) &&
                record.kind != static_cast<std::uint8_t>(LogicKind::GunTarget) &&
                record.kind != static_cast<std::uint8_t>(LogicKind::TrackTrain) &&
                record.kind != static_cast<std::uint8_t>(LogicKind::TriggerTeleport))
                record.target = nameId(entity.value("target"));
            // (A beam's "target" is what its start follows, if anything.)
            if (isBeam)
                record.target = beamStart;
            record.killTarget = nameId(entity.value("killtarget"));
            record.master = nameId(entity.value("master"));
            record.delayMs =
                static_cast<std::uint32_t>(std::max(0, millis(numberKey(entity, "delay"))));
            // (A shooter's "delay" is the time between two of what it
            // throws, which it has kept.)
            if (delayIsOwn)
                record.delayMs = 0;

            // What it sounds like: a door's or a button's sounds are numbers
            // into the game's fixed lists; what breaks sounds of what it is
            // made of.
            LogicSound sound{kNoSound, kNoSound, kNoSound, kNoSound, kNoSound, kNoSound};
            // (The game's one list for buttons, and for what a locked door
            // or button answers with: its buttons.cpp.)
            auto buttonSound = [&](int number) -> std::uint16_t
            {
                if (number >= 1 && number <= 11)
                    return soundIndex("buttons/button" + std::to_string(number) + ".wav", false);
                if (number == 12)
                    return soundIndex("buttons/latchlocked1.wav", false);
                if (number == 13)
                    return soundIndex("buttons/latchunlocked1.wav", false);
                if (number == 14)
                    return soundIndex("buttons/lightswitch2.wav", false);
                if (number >= 21 && number <= 25)
                    return soundIndex("buttons/lever" + std::to_string(number - 20) + ".wav", false);
                return kNoSound;
            };
            if (cls == "func_door" || cls == "func_door_rotating" || cls == "func_button" ||
                cls == "func_rot_button" || cls == "momentary_door")
            {
                // (A number into the game's list -- or, in a few maps, the
                // sound's own name.)
                auto lockSound = [&](const char *key) -> std::uint16_t
                {
                    const std::string value(entity.value(key));
                    if (value.find(".wav") != std::string::npos || value.find('/') != std::string::npos)
                        return soundIndex(value, false);
                    return buttonSound(static_cast<int>(numberKey(entity, key)));
                };
                sound.locked = lockSound("locked_sound");
                sound.unlocked = lockSound("unlocked_sound");
                // "Access denied" and "access granted" and their like: the
                // first line of the game's group for the number.
                static const char *const kLockedLines[] = {"", "NA", "ND", "NF", "NFIRE", "NCHEM", "NRAD", "NCON", "NH", "NG"};
                static const char *const kUnlockedLines[] = {"", "EA", "ED", "EF", "EFIRE", "ECHEM", "ERAD", "ECON", "EH"};
                const int line = static_cast<int>(numberKey(entity, "locked_sentence"));
                if (line >= 1 && line <= 9)
                {
                    const int index = sentenceIndex(std::string(kLockedLines[line]) + "0");
                    if (index >= 0)
                        sound.lockedLine = static_cast<std::uint16_t>(index);
                }
                const int open = static_cast<int>(numberKey(entity, "unlocked_sentence"));
                if (open >= 1 && open <= 8)
                {
                    const int index = sentenceIndex(std::string(kUnlockedLines[open]) + "0");
                    if (index >= 0)
                        sound.unlockedLine = static_cast<std::uint16_t>(index);
                }
            }
            if (cls == "func_door" || cls == "func_door_rotating" || cls == "func_water" || cls == "momentary_door")
            {
                // (Asked for as sounds that may go round: those that do are
                // heard for as long as the door moves.)
                const int move = static_cast<int>(numberKey(entity, "movesnd"));
                if (move >= 1 && move <= 10)
                    sound.move = soundIndex("doors/doormove" + std::to_string(move) + ".wav", true);
                sound.stop = numbered("doors/doorstop", static_cast<int>(numberKey(entity, "stopsnd")), 8);
            }
            else if (cls == "func_button" || cls == "func_rot_button" || cls == "momentary_rot_button")
            {
                sound.move = buttonSound(static_cast<int>(numberKey(entity, "sounds")));
            }
            else if (cls.rfind("func_tank", 0) == 0 && !entity.value("rotatesound").empty())
            {
                // What a tank sounds like for as long as it turns.
                sound.move = soundIndex(std::string(entity.value("rotatesound")), true);
            }
            else if (cls == "func_mortar_field")
            {
                // The shells' whistle as they come, and where they land
                // (mortar.cpp's).
                sound.move = soundIndex("weapons/mortar.wav", false);
                sound.stop = soundIndex("weapons/mortarhit.wav", false);
            }
            else if (cls == "func_rotating")
            {
                // What it hums like for as long as it turns: a sound of its
                // own ("message"), or one of the game's fans.
                const std::string own(entity.value("message"));
                const int fan = static_cast<int>(numberKey(entity, "sounds"));
                if (!own.empty())
                    sound.move = soundIndex(own, true);
                else if (fan >= 1 && fan <= 5)
                    sound.move = soundIndex("fans/fan" + std::to_string(fan) + ".wav", true);
            }
            else if (cls == "func_train" || cls == "func_plat" || cls == "func_platrot")
            {
                // A lift's or a train's sounds are numbers into two more of
                // the game's lists (its plats.cpp): going, and stopping.
                static const char *const kMoves[] = {"",
                                                     "plats/bigmove1.wav",     "plats/bigmove2.wav",  "plats/elevmove1.wav",
                                                     "plats/elevmove2.wav",    "plats/elevmove3.wav", "plats/freightmove1.wav",
                                                     "plats/freightmove2.wav", "plats/heavymove1.wav", "plats/rackmove1.wav",
                                                     "plats/railmove1.wav",    "plats/squeekmove1.wav", "plats/talkmove1.wav",
                                                     "plats/talkmove2.wav"};
                static const char *const kStops[] = {"",
                                                     "plats/bigstop1.wav",    "plats/bigstop2.wav",  "plats/freightstop1.wav",
                                                     "plats/heavystop2.wav",  "plats/rackstop1.wav", "plats/railstop1.wav",
                                                     "plats/squeekstop1.wav", "plats/talkstop1.wav"};
                const int move = static_cast<int>(numberKey(entity, "movesnd"));
                const int stop = static_cast<int>(numberKey(entity, "stopsnd"));
                if (move >= 1 && move <= 13)
                    sound.move = soundIndex(kMoves[move], true);
                if (stop >= 1 && stop <= 8)
                    sound.stop = soundIndex(kStops[stop], false);
            }
            else if (cls == "func_tracktrain")
            {
                // Its engine for as long as it goes, its brakes as it stops
                // (plats.cpp's Precache and StopSound); and what the player
                // hears taking its controls, said as "what it says when it
                // works for them".
                static const char *const kEngines[] = {"",
                                                       "plats/ttrain1.wav", "plats/ttrain2.wav", "plats/ttrain3.wav",
                                                       "plats/ttrain4.wav", "plats/ttrain6.wav", "plats/ttrain7.wav"};
                const int engine = static_cast<int>(numberKey(entity, "sounds"));
                if (engine >= 1 && engine <= 6)
                {
                    sound.move = soundIndex(kEngines[engine], true);
                    sound.stop = soundIndex("plats/ttrain_brake1.wav", false);
                }
                if (!(spawnflags & 2))
                    sound.unlocked = soundIndex("plats/train_use1.wav", false);
            }
            else if (cls == "func_breakable" || cls == "func_pushable")
            {
                static const char *const kBreaks[] = {
                    "debris/bustglass1.wav",  "debris/bustcrate1.wav",    "debris/bustmetal1.wav",
                    "debris/bustflesh1.wav",  "debris/bustconcrete1.wav", "debris/bustceiling.wav",
                    "debris/bustmetal2.wav",  "debris/bustglass2.wav",    "debris/bustconcrete2.wav"};
                const int material = std::clamp(static_cast<int>(numberKey(entity, "material")), 0, 8);
                sound.move = soundIndex(kBreaks[material], false);
            }
            else if (cls == "func_healthcharger" || cls == "func_recharge")
            {
                // A wall charger's: as it starts, as it goes on (round and
                // round, from the file's cue), and as it refuses.
                const bool suit = cls == "func_recharge";
                sound.unlocked = soundIndex(suit ? "items/suitchargeok1.wav" : "items/medshot4.wav", false);
                sound.move = soundIndex(suit ? "items/suitcharge1.wav" : "items/medcharge4.wav", true);
                sound.locked = soundIndex(suit ? "items/suitchargeno1.wav" : "items/medshotno1.wav", false);
            }
            if (heardSpark != kNoSound)
                sound.move = heardSpark;
            heard.push_back(sound);

            out.recordOfEntity[e] = static_cast<int>(out.records.size());
            out.records.push_back(record);
            managerOutputs.push_back(std::move(outputs));
            trainCorners.push_back(trainCorner);
            beamPlacesOf.push_back(std::move(beamPlaces));
            speakerLinesOf.push_back(std::move(speakerLines));
        }

        // The actors: every entity that is a model this map had cooked.
        // (Then the weapons it has not, to be loaded if one is taken up.)
        for (const std::string &model : models)
        {
            ModelRef ref{};
            std::strncpy(ref.name, model.c_str(), sizeof(ref.name) - 2);
            out.modelRefs.push_back(ref);
        }
        for (const std::string &model : lazyModels)
        {
            ModelRef ref{};
            std::strncpy(ref.name, model.c_str(), sizeof(ref.name) - 2);
            ref.name[sizeof(ref.name) - 1] = kModelRefLazy;
            out.modelRefs.push_back(ref);
        }
        // (In three rounds: the items of the weapons that came later after
        // all the rest, and those of the last ones after even those, so that
        // every other actor is where a save made before they were says it
        // is.)
        // (In rounds, each after all of the one before: what each version of
        // the format added comes after everything that was there before it,
        // so an actor a save points at keeps its place -- the later items,
        // then the last ones, then round 3's monsters, then round 4's, then
        // round 5's ... then the rats, level version 65's.)
        for (int round = 0; round < 9; ++round)
        for (std::size_t e = 0; e < entities.size(); ++e)
        {
            const Entity &entity = entities[e];
            const ActorInfo info = actorOf(entity);
            if (!info.is)
                continue;
            // (Round 4's include the dead man in the suit, who is a pose.)
            const bool round4 = static_cast<int>(info.kind) >= static_cast<int>(ActorKind::Osprey) ||
                                lowered(entity.className()) == "monster_hevsuit_dead";
            const bool round5 = static_cast<int>(info.kind) >= static_cast<int>(ActorKind::Tentacle) ||
                                lowered(entity.className()) == "monster_grunt_repel";
            // (Round 6: level version 39's -- Xen's plants, and three items.)
            const bool round6 = info.kind == ActorKind::Xen ||
                                static_cast<int>(info.item) >= static_cast<int>(ItemKind::Antidote);
            const int itemRound = info.kind == ActorKind::Rat ? 8 // (level version 65's)
                                  : info.item == ItemKind::LongJump ? 7 // (level version 45's)
                                  : round6 ? 6
                                  : round5 ? 5
                                  : round4 ? 4
                                  : info.item == ItemKind::None
                                      ? (static_cast<int>(info.kind) >= static_cast<int>(ActorKind::Cockroach) ? 3 : 0)
                                  : static_cast<int>(info.item) >= static_cast<int>(kItemFirstLast) ? 2
                                  : static_cast<int>(info.item) >= static_cast<int>(kItemFirstMore) ? 1
                                                                                                     : 0;
            if (itemRound != round)
                continue;
            const auto found = std::find(models.begin(), models.end(), modelNameOf(info.file));
            if (found == models.end())
            {
                ++out.unsupported[std::string(entity.className())];
                continue;
            }
            // A barnacle the map has buried in its ceiling (c1a4f's at 438
            // 266 1664, in solid all round) is never seen in the game, and
            // its tongue never comes down: left out. (The game's box for one
            // hangs from where it is fixed. One with a name may be wanted.)
            if (info.kind == ActorKind::Barnacle && entity.value("targetname").empty())
            {
                double at[3] = {0, 0, 0};
                entity.vec3("origin", at);
                const double lo[3] = {at[0] - 16, at[1] - 16, at[2] - 32};
                const double hi[3] = {at[0] + 16, at[1] + 16, at[2]};
                if (!boxTouchesOpen(bsp, bsp.models[0].headNode[0], lo, hi))
                    continue;
            }
            // (A maker whose model is missing keeps a record that makes
            // nothing: it has no places.)
            ActorRecord actor{};
            actor.model = static_cast<std::uint16_t>(found - models.begin());
            actor.name = nameId(entity.value(info.spare != 0 ? "netname" : "targetname"));
            // (An item's kind; for anything else, its skin.)
            actor.item = info.item != ItemKind::None
                             ? static_cast<std::uint32_t>(info.item)
                             : (static_cast<std::uint32_t>(std::clamp(
                                    static_cast<int>(numberKey(entity, "skin")), 0, 3))
                                << kActorSkinShift);
            if (info.item == ItemKind::Weaponbox)
            {
                // What is in it, written out where the level keeps its
                // strings: the game's ammunition names, as the game reads
                // them, made short ("Trip Mine" 7 is "tripmines=7").
                static const char *const kAmmo[][2] = {
                    {"9mm", "9mm"},           {"357", "357"},           {"buckshot", "buckshot"},
                    {"argrenades", "argrenades"}, {"bolts", "bolts"},   {"rockets", "rockets"},
                    {"uranium", "uranium"},   {"hand grenade", "grenades"}, {"trip mine", "tripmines"},
                    {"satchel charge", "satchels"}, {"snarks", "snarks"}, {"hornets", "hornets"}};
                std::string contents;
                for (const auto &pair : entity.pairs)
                {
                    const std::string key = lowered(pair.first);
                    for (const auto &ammo : kAmmo)
                    {
                        const int amount = std::atoi(pair.second.c_str());
                        if (key == ammo[0] && amount > 0)
                            contents += (contents.empty() ? "" : " ") + std::string(ammo[1]) + "=" + std::to_string(amount);
                    }
                }
                actor.item = static_cast<std::uint32_t>(ItemKind::Weaponbox) |
                             (stringOffset(contents) << kItemStringShift);
            }
            if (info.item == ItemKind::Suit)
            {
                // What it says as it is taken (items.cpp's CItemSuit): the
                // whole of its logon, or with flag 1 only its first words.
                const bool shortLogon = (static_cast<int>(numberKey(entity, "spawnflags")) & 1) != 0;
                const int line = sentenceIndex(shortLogon ? "HEV_A0" : "HEV_AAx");
                if (line >= 0 && line < 0xFFFF)
                    actor.item |= (static_cast<std::uint32_t>(line + 1) << kItemSuitLineShift) |
                                  (shortLogon ? kItemSuitShort : 0u);
            }
            if (info.item != ItemKind::None && !entity.value("target").empty())
            {
                // What it fires when taken goes through a relay of its own:
                // then a multisource it targets counts it among its inputs
                // like anything else that does.
                LogicRecord relay{};
                relay.kind = static_cast<std::uint8_t>(LogicKind::TriggerRelay);
                relay.useType = kUseToggle;
                relay.waitMs = -1;
                relay.brush = kNoBrush;
                relay.name = nameId("\x01item" + std::to_string(e));
                relay.target = nameId(entity.value("target"));
                out.records.push_back(relay);
                heard.push_back(LogicSound{kNoSound, kNoSound, kNoSound, kNoSound, kNoSound, kNoSound});
                managerOutputs.emplace_back();
                trainCorners.push_back(kNoCorner);
                actor.target = relay.name;
            }
            double origin[3] = {0, 0, 0}, angles[3] = {0, 0, 0};
            entity.vec3("origin", origin);
            if (!entity.vec3("angles", angles))
                angles[1] = numberKey(entity, "angle");
            for (int a = 0; a < 3; ++a)
                actor.origin[a] = fxFromDouble(origin[a]);
            actor.yaw = static_cast<std::uint16_t>(std::lround(angles[1] * 65536.0 / 360.0));
            // "body" -1 on a scientist is "any head": one is picked, the
            // same one every time the map is cooked.
            int body = static_cast<int>(numberKey(entity, "body"));
            if (body < 0)
                body = static_cast<int>(e % 4);
            actor.body = static_cast<std::uint8_t>(std::clamp(body, 0, 255));
            if (info.item == ItemKind::Tripmine)
                actor.body = 3; // (the mine alone)
            actor.kind = static_cast<std::uint8_t>(info.kind);
            actor.clip = info.startClip.empty() ? 0 : nameHash(info.startClip.c_str());
            actor.flags = info.solid ? kActorSolid : 0;
            actor.light = 24;
            if (static_cast<int>(numberKey(entity, "spawnflags")) & 16)
                actor.flags |= kActorPrisoner;
            // Scenery the map turns over (c1a4b and c1a4d's bubbles in their
            // pipes: "angles" 180 0 0): a half turn of pitch is a half turn
            // of yaw and one about the way it then faces, which is how a
            // turret under a ceiling is drawn (actorHungOver). The game's
            // models are turned no other way but about the upright.
            if (info.kind == ActorKind::Cycler || info.kind == ActorKind::Generic ||
                info.kind == ActorKind::Furniture)
            {
                auto half = [](double degrees)
                {
                    const long turns = std::lround(std::fmod(std::fabs(degrees), 360.0));
                    return turns == 180 ? 1 : (turns == 0 || turns == 360 ? 0 : -1);
                };
                const int pitch = half(angles[0]), roll = half(angles[2]);
                if (pitch == 1 && roll == 0)
                {
                    actor.flags |= kActorCeiling;
                    actor.yaw = static_cast<std::uint16_t>(actor.yaw + 32768);
                }
                else if (pitch != 0 || roll != 0)
                    ++out.unsupported[std::string(entity.className()) + " tipped (drawn upright)"];
            }
            // (SF_MONSTER_FADECORPSE: its body fades once it has fallen.)
            if (entity.className().rfind("monster_", 0) == 0 && (static_cast<int>(numberKey(entity, "spawnflags")) & 512))
                actor.light |= kActorLightFadeCorpse;
            const std::string actorClass = lowered(entity.className());
            if (info.kind == ActorKind::Xen)
            {
                // Which plant; a light's glow its colour, and what it
                // switches off as it hides.
                std::uint32_t colour = 0x7FFF;
                double rgb[3];
                if (entity.vec3("rendercolor", rgb) && (rgb[0] + rgb[1] + rgb[2]) > 0)
                    colour = (static_cast<std::uint32_t>(std::clamp(rgb[0], 0.0, 255.0)) >> 3) |
                             ((static_cast<std::uint32_t>(std::clamp(rgb[1], 0.0, 255.0)) >> 3) << 5) |
                             ((static_cast<std::uint32_t>(std::clamp(rgb[2], 0.0, 255.0)) >> 3) << 10);
                actor.item = static_cast<std::uint32_t>(info.xen) | (colour << 16);
                if (info.xen == XenPlant::Light)
                    actor.target = nameId(entity.value("target"));
            }
            // (A monster_generic of the player's own shape: its origin is its
            // middle -- genericmonster.cpp gives it the player's box.)
            if (info.kind == ActorKind::Generic)
            {
                const std::string file = lowered(entity.value("model"));
                if (file == "models/holo.mdl" || file == "models/player.mdl")
                    actor.flags |= kActorCentred;
            }
            if (info.kind == ActorKind::Grunt)
            {
                // His head and his gun are by what he carries (the model's
                // second and third body groups: four heads, three guns);
                // one in five is dark, and whoever has the launcher always.
                const bool made = info.spare != 0;
                const int spawnflags = made ? 0 : static_cast<int>(numberKey(entity, "spawnflags"));
                int weapons = made ? 0 : static_cast<int>(numberKey(entity, "weapons"));
                weapons = weapons == 0 ? 3 : (weapons & 15);
                const bool shotgun = (weapons & 8) != 0, launcher = (weapons & 4) != 0;
                actor.body = static_cast<std::uint8_t>((shotgun ? 2 : (launcher ? 3 : 0)) + (shotgun ? 4 : 0));
                const std::uint32_t skin = (launcher && !shotgun) || e % 5 == 0 ? 1 : 0;
                const std::uint32_t squad = made ? 0 : nameId(entity.value("netname"));
                // (What he sets off, and when: the game's own numbers.)
                const int condition = made ? 0 : std::clamp(static_cast<int>(numberKey(entity, "TriggerCondition")), 0, 15);
                if (condition != 0 && !made)
                    actor.target = nameId(entity.value("TriggerTarget"));
                actor.item = static_cast<std::uint32_t>(weapons) |
                             (static_cast<std::uint32_t>(condition) << kGruntTriggerShift) |
                             (skin << kActorSkinShift) | (squad << kActorSquadShift);
                if (spawnflags & 1)
                    actor.flags |= kActorWaitSeen;
                if (spawnflags & 2)
                    actor.flags |= kActorGag;
                if (spawnflags & 32)
                    actor.flags |= kActorLeader;
                if (spawnflags & 128)
                    actor.flags |= kActorWaitScript;
            }
            else if (info.kind == ActorKind::BigMomma)
            {
                // Her first node: the one her netname names (as a record).
                const std::uint16_t first = nameId(entity.value("netname"));
                actor.item = 0;
                for (std::size_t r = 0; r < out.records.size() && first != 0; ++r)
                {
                    if (out.records[r].name == first &&
                        out.records[r].kind == static_cast<std::uint8_t>(LogicKind::MommaNode))
                    {
                        actor.item = static_cast<std::uint32_t>(r) + 1;
                        break;
                    }
                }
                const int condition = std::clamp(static_cast<int>(numberKey(entity, "TriggerCondition")), 0, 15);
                if (condition != 0)
                    actor.target = nameId(entity.value("TriggerTarget"));
            }
            else if (info.kind == ActorKind::Apache)
            {
                // Where it flies first, as an osprey's; it waits to be
                // switched on (the game's 4 or 64), and leaves no wreck (8).
                const auto corner = cornerOfName.find(std::string(entity.value("target")));
                actor.item = corner != cornerOfName.end() ? static_cast<std::uint32_t>(corner->second) + 1 : 0;
                const int spawnflags = static_cast<int>(numberKey(entity, "spawnflags"));
                if (spawnflags & (4 | 64))
                    actor.flags |= kActorCeiling; // (apache::kActorWaitTrigger)
                if (spawnflags & 8)
                    actor.flags |= kActorAuto;
            }
            else if (info.kind == ActorKind::Osprey)
            {
                // Where it flies to first: the corner it targets (plus one;
                // 0, nowhere). It waits to be switched on, with the game's
                // flag 64.
                const auto corner = cornerOfName.find(std::string(entity.value("target")));
                actor.item = corner != cornerOfName.end() ? static_cast<std::uint32_t>(corner->second) + 1 : 0;
                if (static_cast<int>(numberKey(entity, "spawnflags")) & 64)
                    actor.flags |= kActorCeiling; // (osprey::kActorWaitTrigger)
            }
            else if (actorThinks(actor.kind) && info.spare == 0)
            {
                // A monster as the game has it: its squad (or whom it calls
                // for help), what it sets off and when, and its flags --
                // waits to be seen, says nothing, waits for its script,
                // leads its squad; an alien slave waits to be provoked.
                const int spawnflags = static_cast<int>(numberKey(entity, "spawnflags"));
                const std::uint32_t squad = nameId(entity.value("netname"));
                const int condition = std::clamp(static_cast<int>(numberKey(entity, "TriggerCondition")), 0, 15);
                if (condition != 0)
                    actor.target = nameId(entity.value("TriggerTarget"));
                actor.item |= (static_cast<std::uint32_t>(condition) << kGruntTriggerShift) |
                              (squad << kActorSquadShift);
                if (spawnflags & 1)
                    actor.flags |= kActorWaitSeen;
                if (spawnflags & 2)
                    actor.flags |= kActorGag;
                if (spawnflags & 32)
                    actor.flags |= kActorLeader;
                if (spawnflags & 128)
                    actor.flags |= kActorWaitScript;
                if (info.kind == ActorKind::Slave && (spawnflags & 64))
                    actor.flags |= kActorCeiling; // (HlAiMonsters.hpp: slave::kActorWaitProvoked)
                // One of the people before the disaster: will not follow.
                if (spawnflags & 256)
                    actor.item |= kPersonPreDisaster;
                // How a monster_generic is drawn: see-through (the game's
                // render modes 2 and 5), or as its hologram (renderfx 16).
                const int mode = static_cast<int>(numberKey(entity, "rendermode"));
                if (info.kind == ActorKind::Generic && (mode == 2 || mode == 5))
                {
                    const std::uint32_t look =
                        static_cast<int>(numberKey(entity, "renderfx")) == 16
                            ? kActorLookHologram
                            : static_cast<std::uint32_t>(
                                  std::clamp(std::lround(numberKey(entity, "renderamt") * 7 / 255), 1L, 6L));
                    actor.item |= look << kActorLookShift;
                }
                // (Luther's head is darker: the model's second skin.)
                if (info.kind == ActorKind::Scientist && actor.body % 4 == 2)
                    actor.item |= 1u << kActorSkinShift;
            }
            else if (info.kind == ActorKind::Sitter && actor.body % 4 == 2)
            {
                actor.item |= 1u << kActorSkinShift;
            }
            else if (actorClass == "monster_hgrunt_dead")
            {
                // The game's four: a soldier or his officer, with a gun or
                // without.
                const int which = std::clamp(body, 0, 3);
                actor.body = static_cast<std::uint8_t>(((which & 1) ? 1 : 0) + (which >= 2 ? 8 : 0));
            }
            else if (actorClass == "monster_hevsuit_dead")
            {
                actor.body = 0; // (the suit's model has the one body)
            }
            else if (info.kind == ActorKind::Tripmine)
            {
                // The mine alone (the model is the one held, hands and all),
                // pointing as the map has it; on at once, or after a while.
                actor.body = 3;
                // (Tipped right over is turned round: the model is drawn
                // by which way it faces alone.)
                double pitch = std::remainder(angles[0], 360.0);
                if (std::fabs(pitch) > 90)
                {
                    pitch = (pitch > 0 ? 180 : -180) - pitch;
                    actor.yaw = static_cast<std::uint16_t>(actor.yaw + 32768);
                }
                actor.item = static_cast<std::uint32_t>(
                    static_cast<std::uint8_t>(static_cast<std::int8_t>(std::lround(pitch * 256.0 / 360.0))));
                if (static_cast<int>(numberKey(entity, "spawnflags")) & 1)
                    actor.flags |= kActorQuick;
            }
            if (isTurret(actor.kind))
            {
                // Under a ceiling or on a floor; out by itself at the sight
                // of someone, or only when it is switched on; and what it
                // sets off as it comes out, folds away and is destroyed.
                const int spawnflags = static_cast<int>(numberKey(entity, "spawnflags"));
                if (static_cast<int>(numberKey(entity, "orientation")) == 1)
                    actor.flags |= kActorCeiling;
                if ((spawnflags & 32) && !(spawnflags & 64))
                    actor.flags |= kActorAuto;
                actor.target = nameId(entity.value("target"));
            }
            if (info.spare != 0)
            {
                // A maker's children: places kept, nobody in them yet.
                actor.flags |= kActorDormant;
                const int record = out.recordOfEntity[e];
                if (record >= 0)
                {
                    out.records[record].extra[0] = static_cast<std::uint32_t>(out.actors.size());
                    out.records[record].extra[1] = static_cast<std::uint32_t>(info.spare);
                }
                for (int n = 1; n < info.spare; ++n)
                    out.actors.push_back(actor);
            }
            if (actorClass == "monster_grunt_repel")
            {
                // Kept for him, not there until his rope is used: his record
                // (LogicKind::Repel) knows the place.
                actor.flags |= kActorDormant;
                actor.name = 0;
                const int record = out.recordOfEntity[e];
                if (record >= 0)
                    out.records[record].extra[0] = static_cast<std::uint32_t>(out.actors.size());
            }
            if (info.kind == ActorKind::Boid)
            {
                // SpawnFlock: iFlockSize boids within flFlockRadius about it
                // (by the map's own numbers, the same each cook), the first
                // their leader -- the others know it by its number.
                const int size = std::clamp(static_cast<int>(numberKey(entity, "iFlockSize", 8)), 1, 12);
                const double radius = std::max(16.0, numberKey(entity, "flFlockRadius", 128));
                const std::uint32_t leader = static_cast<std::uint32_t>(out.actors.size()) + 1;
                std::uint32_t roll = static_cast<std::uint32_t>(e) * 2654435761u + 12345u;
                auto next = [&]() { roll = roll * 1664525u + 1013904223u; return static_cast<double>(roll >> 8) / 16777216.0; };
                for (int n = 0; n < size; ++n)
                {
                    ActorRecord one = actor;
                    one.origin[0] = fxFromDouble(origin[0] + (next() * 2 - 1) * radius);
                    one.origin[1] = fxFromDouble(origin[1] + (next() * 2 - 1) * radius);
                    one.origin[2] = fxFromDouble(origin[2] + next() * 16);
                    one.item = n == 0 ? 0 : leader;
                    one.name = 0;
                    one.target = 0;
                    out.actors.push_back(one);
                }
                continue;
            }
            out.actors.push_back(actor);
            if (info.kind == ActorKind::Nihilanth)
            {
                // The places for its friends, after it (nihilanth::kFriends):
                // three alien slaves, three controllers, not there until it
                // calls them up.
                const auto slave = std::find(models.begin(), models.end(), std::string("islave"));
                const auto controller = std::find(models.begin(), models.end(), std::string("controller"));
                for (int n = 0; slave != models.end() && controller != models.end() && n < 6; ++n)
                {
                    ActorRecord friendly = actor;
                    friendly.model = static_cast<std::uint16_t>((n < 3 ? slave : controller) - models.begin());
                    friendly.name = 0;
                    friendly.kind = static_cast<std::uint8_t>(n < 3 ? ActorKind::Slave : ActorKind::Controller);
                    friendly.clip = nameHash(n < 3 ? "idle1" : "float");
                    friendly.flags = kActorSolid | kActorDormant;
                    friendly.item = 0;
                    friendly.target = 0;
                    friendly.body = 0;
                    out.actors.push_back(friendly);
                }
            }
            if (info.kind == ActorKind::BigMomma)
            {
                // The places for her young, after her (momma::kChildren):
                // baby crabs, not there until she lays them.
                const auto crab = std::find(models.begin(), models.end(), std::string("baby_headcrab"));
                for (int n = 0; crab != models.end() && n < 6; ++n)
                {
                    ActorRecord child = actor;
                    child.model = static_cast<std::uint16_t>(crab - models.begin());
                    child.name = 0;
                    child.kind = static_cast<std::uint8_t>(ActorKind::BabyCrab);
                    child.clip = nameHash("idle1");
                    child.flags = kActorSolid | kActorDormant;
                    child.item = 0;
                    child.target = 0;
                    child.body = 0;
                    out.actors.push_back(child);
                }
            }
        }
        // (After everything else, so that no other actor is moved: what a
        // soldier drops when he dies -- his gun, and a launcher's grenades --
        // where he stands, there but not yet: HlGrunt.hpp finds them by
        // that place.)
        for (const Entity &entity : entities)
        {
            if (lowered(entity.className()) != "monster_human_grunt")
                continue;
            int weapons = static_cast<int>(numberKey(entity, "weapons"));
            weapons = weapons == 0 ? 3 : (weapons & 15);
            double origin[3] = {0, 0, 0};
            entity.vec3("origin", origin);
            const bool shotgun = (weapons & 8) != 0;
            const struct
            {
                bool wanted;
                const char *model;
                ItemKind item;
            } kDrops[] = {{true, shotgun ? "w_shotgun" : "w_9mmar", shotgun ? ItemKind::Shotgun : ItemKind::Mp5},
                          {(weapons & 4) != 0, "w_argrenade", ItemKind::ArGrenades}};
            for (const auto &drop : kDrops)
            {
                const auto found = std::find(models.begin(), models.end(), std::string(drop.model));
                if (!drop.wanted || found == models.end())
                    continue;
                ActorRecord item{};
                item.model = static_cast<std::uint16_t>(found - models.begin());
                for (int a = 0; a < 3; ++a)
                    item.origin[a] = fxFromDouble(origin[a]);
                item.kind = static_cast<std::uint8_t>(ActorKind::Item);
                item.clip = nameHash("idle");
                item.flags = kActorDormant;
                item.light = 24;
                item.item = static_cast<std::uint32_t>(drop.item);
                out.actors.push_back(item);
            }
        }
        // (And after even those: a guard's pistol, likewise -- barney.cpp's
        // DropItem when he is killed. HlAiPeople.hpp finds it by his place.)
        for (const Entity &entity : entities)
        {
            if (lowered(entity.className()) != "monster_barney")
                continue;
            const auto found = std::find(models.begin(), models.end(), std::string("w_9mmhandgun"));
            if (found == models.end())
                continue;
            double origin[3] = {0, 0, 0};
            entity.vec3("origin", origin);
            ActorRecord item{};
            item.model = static_cast<std::uint16_t>(found - models.begin());
            for (int a = 0; a < 3; ++a)
                item.origin[a] = fxFromDouble(origin[a]);
            item.kind = static_cast<std::uint8_t>(ActorKind::Item);
            item.clip = nameHash("idle");
            item.flags = kActorDormant;
            item.light = 24;
            item.item = static_cast<std::uint32_t>(ItemKind::Glock);
            out.actors.push_back(item);
        }
        // (... and what a box or a crate has in it, where its middle is at
        // rest: HlLogic.hpp's breakIt finds it by that, and puts it where
        // the box then is -- the game's Create at VecBModelOrigin.)
        for (const Entity &entity : entities)
        {
            const ItemClass *what = spawnObjectOf(entity);
            const std::string text(entity.value("model"));
            const int index = text.size() >= 2 && text[0] == '*' ? std::atoi(text.c_str() + 1) : 0;
            if (what == nullptr || index <= 0 || index >= static_cast<int>(bsp.models.size()))
                continue;
            std::string file = what->file;
            file = file.substr(file.find('/') + 1);
            file = file.substr(0, file.rfind('.'));
            const auto found = std::find(models.begin(), models.end(), file);
            if (found == models.end())
                continue;
            const BspModel &model = bsp.models[index];
            double origin[3] = {0, 0, 0};
            entity.vec3("origin", origin);
            ActorRecord item{};
            item.model = static_cast<std::uint16_t>(found - models.begin());
            for (int a = 0; a < 3; ++a)
                item.origin[a] = fxFromDouble(origin[a] + (double(model.mins[a]) + model.maxs[a]) / 2);
            item.kind = static_cast<std::uint8_t>(ActorKind::Item);
            item.clip = nameHash(what->kind == ItemKind::Tripmine ? "ground" : "idle");
            item.flags = kActorDormant;
            item.light = 24;
            item.item = static_cast<std::uint32_t>(what->kind);
            out.actors.push_back(item);
        }

        // Where a train is driven from (CFuncTrainControls::Find and the
        // train's SetControls): the box of the func_traincontrols that names
        // it, as it is at the start, which is the train's own space as built.
        for (std::size_t c = 0; c < entities.size(); ++c)
        {
            const Entity &controls = entities[c];
            if (controls.className() != "func_traincontrols" || controls.value("target").empty())
                continue;
            const std::string text(controls.value("model"));
            const int index = text.size() >= 2 && text[0] == '*' ? std::atoi(text.c_str() + 1) : 0;
            if (index <= 0 || index >= static_cast<int>(bsp.models.size()))
                continue;
            const BspModel &model = bsp.models[index];
            double at[3] = {0, 0, 0};
            controls.vec3("origin", at);
            for (std::size_t t = 0; t < entities.size(); ++t)
            {
                const int r = out.recordOfEntity[t];
                if (r < 0 || entities[t].value("targetname") != controls.value("target") ||
                    out.records[r].kind != static_cast<std::uint8_t>(LogicKind::TrackTrain))
                    continue;
                for (int a = 0; a < 3; ++a)
                {
                    out.records[r].mins[a] = static_cast<std::int16_t>(
                        std::clamp(std::floor(model.mins[a] + at[a]), -32768.0, 32767.0));
                    out.records[r].maxs[a] = static_cast<std::int16_t>(
                        std::clamp(std::ceil(model.maxs[a] + at[a]), -32768.0, 32767.0));
                }
                break;
            }
        }

        // Lists. A multi_manager's outputs are known; a multisource's inputs
        // are every record that targets it, which needs all the records first.
        for (std::size_t r = 0; r < out.records.size(); ++r)
        {
            LogicRecord &record = out.records[r];
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Train) ||
                record.kind == static_cast<std::uint8_t>(LogicKind::GunTarget) ||
                record.kind == static_cast<std::uint8_t>(LogicKind::TrackTrain))
            {
                record.firstAux = trainCorners[r];
                record.auxCount = 0;
                continue;
            }
            record.firstAux = static_cast<std::uint16_t>(out.aux.size());
            if (record.kind == static_cast<std::uint8_t>(LogicKind::MultiManager))
            {
                for (const ManagerOutput &output : managerOutputs[r])
                    out.aux.push_back(LogicAux{output.target, 0, output.delayMs});
            }
            else if (record.kind == static_cast<std::uint8_t>(LogicKind::Speaker) && r < speakerLinesOf.size())
            {
                for (const std::uint16_t line : speakerLinesOf[r])
                    out.aux.push_back(LogicAux{line, 0, 0});
            }
            else if (record.kind == static_cast<std::uint8_t>(LogicKind::Beam) && r < beamPlacesOf.size())
            {
                // The places its end is picked from: x, y and z in whole
                // units, in the three fields.
                for (const std::array<double, 3> &place : beamPlacesOf[r])
                {
                    if (out.aux.size() - record.firstAux >= 16)
                        break;
                    out.aux.push_back(LogicAux{
                        static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(std::lround(place[0]), -32768L, 32767L))),
                        static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(std::lround(place[1]), -32768L, 32767L))),
                        static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(place[2])))});
                }
            }
            else if (record.kind == static_cast<std::uint8_t>(LogicKind::MultiSource) &&
                     record.name != 0)
            {
                for (std::size_t other = 0; other < out.records.size(); ++other)
                {
                    // 32 inputs is the width of the state word; the game's own
                    // limit is the same.
                    // What targets it -- and a multi_manager targets what
                    // its keys name, which is how the game's own maps switch
                    // a multisource on from a sequence.
                    bool targets = out.records[other].target == record.name;
                    for (const ManagerOutput &output : managerOutputs[other])
                        targets = targets || output.target == record.name;
                    if (targets && out.aux.size() - record.firstAux < 32)
                        out.aux.push_back(LogicAux{static_cast<std::uint16_t>(other), 0, 0});
                }
            }
            record.auxCount = static_cast<std::uint16_t>(out.aux.size() - record.firstAux);
        }
        // Which of the common sounds this map has a use for: the player's
        // own always, a monster's only where there is one.
        std::vector<bool> soundLazy(commonCount, false);
        bool lazy = false;
        auto use = [&](CommonSound from, CommonSound to)
        {
            for (int c = static_cast<int>(from); c <= static_cast<int>(to); ++c)
            {
                if (lazy)
                    soundLazy[c] = !soundUsed[c];
                else
                    soundUsed[c] = true;
            }
        };
        use(CommonSound::Step1, CommonSound::Explode);
        use(CommonSound::Ricochet1, CommonSound::BulletFlesh);
        // (Water and coming down hard are everywhere; rungs where there is
        // a ladder.)
        use(CommonSound::Wade1, CommonSound::JumpLand);
        // (Swimming where there is water to be under: the world's, or a
        // func_water's.)
        bool water = false;
        for (const BspLeaf &leaf : bsp.leaves)
            water = water || (leaf.contents <= kContentsWater && leaf.contents >= kContentsLava);
        for (const Entity &entity : entities)
            water = water || entity.value("classname") == "func_water";
        if (water)
            use(CommonSound::Swim1, CommonSound::Swim4);
        // (Steps by what is underfoot: all of them here; whoever knows the
        // map's textures drops those it has no floor for -- Cooker.cpp.)
        use(CommonSound::Metal1, CommonSound::Tile4);
        use(CommonSound::CrowbarHit2, CommonSound::GlassHit3);
        for (const LogicRecord &record : out.records)
        {
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Ladder))
                use(CommonSound::Ladder1, CommonSound::Ladder4);
            // (... and the scrape of a box, where there is one to push.)
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Pushable))
                use(CommonSound::PushBox1, CommonSound::PushBox3);
            // (... and a track train's start, where there is one.)
            if (record.kind == static_cast<std::uint8_t>(LogicKind::TrackTrain))
                use(CommonSound::TrainStart, CommonSound::TrainStart);
        }
        // The guns' own: those of what the map has to be held, and (`lazy`)
        // of what it has not -- a weapon given by a cheat, whose model and
        // sounds are loaded when it is taken up (ModelCache): kept named,
        // but neither loaded with the map nor in its archive.
        auto gunSounds = [&](const std::vector<std::string> &held)
        {
            // (The first six: where the map has them to be held. Those that
            // came later: where it has them to be held, or lying about to be
            // picked up.)
            auto holds = [&](const char *viewModel, ItemKind item, ItemKind other = ItemKind::None)
            {
                if (std::find(held.begin(), held.end(), viewModel) != held.end())
                    return true;
                if (lazy || item == ItemKind::None)
                    return false; // (what lies about is the map's own)
                for (const ActorRecord &actor : out.actors)
                {
                    if (actor.kind == static_cast<std::uint8_t>(ActorKind::Item) &&
                        (actor.item == static_cast<std::uint32_t>(item) || actor.item == static_cast<std::uint32_t>(other)))
                        return true;
                }
                return false;
            };
            for (const GunSounds &gun : kGunSounds)
            {
                if (holds(gun.viewModel, gun.item, gun.other))
                    use(gun.first, gun.last);
            }
        };
        gunSounds(models);
        // (A grenade's debris: where one of the guns that throw one -- a
        // grenade, the MP5's, a satchel, a tripmine, a rocket -- is to be
        // had, or a tank's rocket or an apache's.)
        bool blasts = soundUsed[static_cast<int>(CommonSound::GrenadeBounce2)] ||
                      soundUsed[static_cast<int>(CommonSound::Launcher)] ||
                      soundUsed[static_cast<int>(CommonSound::SatchelBounce)] ||
                      soundUsed[static_cast<int>(CommonSound::MineDeploy)] ||
                      soundUsed[static_cast<int>(CommonSound::RocketFire)];
        for (const Entity &entity : entities)
        {
            const std::string_view cls = entity.value("classname");
            blasts = blasts || cls == "func_tankrocket" || cls == "monster_apache" || cls == "monster_human_grunt" ||
                     cls == "monster_tripmine" || cls == "monster_satchel";
        }
        if (blasts)
            use(CommonSound::Debris1, CommonSound::Debris3);
        // (An air tank's, where there is one.)
        for (const ActorRecord &actor : out.actors)
        {
            if (actor.kind == static_cast<std::uint8_t>(ActorKind::Item) &&
                (actor.item & 0xFF) == static_cast<std::uint32_t>(ItemKind::Airtank))
                use(CommonSound::AirTank, CommonSound::AirTank);
        }
        lazy = true;
        gunSounds(lazyModels);
        lazy = false;
        // (What comes apart is heard to -- and a fall the player does not
        // get up from, and a snark's burst: every map.)
        use(CommonSound::BodySplat, CommonSound::BodySplat);
        // (A body falling to the floor: every map has someone to.)
        use(CommonSound::BodyDrop3, CommonSound::BodyDrop4);
        bool soldiers = false;
        for (const ActorRecord &actor : out.actors)
        {
            if (actor.kind == static_cast<std::uint8_t>(ActorKind::Scientist) ||
                actor.kind == static_cast<std::uint8_t>(ActorKind::Sitter))
            {
                use(CommonSound::ScientistPain, CommonSound::ScientistDie);
                use(CommonSound::ScientistPain2, CommonSound::ScientistPain5);
            }
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Guard))
            {
                use(CommonSound::GuardPain, CommonSound::GuardDie);
                use(CommonSound::GuardPain2, CommonSound::GuardShot);
            }
            if (actor.kind == static_cast<std::uint8_t>(ActorKind::Headcrab) ||
                actor.kind == static_cast<std::uint8_t>(ActorKind::BabyCrab))
                use(CommonSound::HeadcrabAlert, CommonSound::HeadcrabBite);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Zombie))
                use(CommonSound::ZombieAlert, CommonSound::ClawMiss);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Barnacle))
                use(CommonSound::BarnacleAlert, CommonSound::BarnacleDie);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Slave))
            {
                use(CommonSound::SlaveAlert, CommonSound::SlaveDie);
                use(CommonSound::ClawStrike, CommonSound::ClawMiss);
            }
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Bullsquid))
                use(CommonSound::SquidAlert, CommonSound::SpitHit);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Houndeye))
                use(CommonSound::HoundAlert, CommonSound::HoundDie);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Turret))
            {
                use(CommonSound::TurretAlert, CommonSound::TurretPing);
                use(CommonSound::TurretDie, CommonSound::TurretSpinDown);
            }
            else if (isTurret(actor.kind))
                use(CommonSound::TurretAlert, CommonSound::TurretDie);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Tripmine))
                use(CommonSound::MineActivate, CommonSound::MineActivate);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Grunt))
            {
                use(CommonSound::GruntFire1, CommonSound::GruntDie2);
                if (actor.item & kGruntLauncher)
                    use(CommonSound::Launcher, CommonSound::Launcher);
                soldiers = true;
            }
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Cockroach))
                use(CommonSound::RoachWalk, CommonSound::RoachDie);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Leech))
                use(CommonSound::LeechBite, CommonSound::LeechAlert);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::AlienGrunt))
            {
                use(CommonSound::AgruntIdle1, CommonSound::AgruntDie2);
                use(CommonSound::HornetFire1, CommonSound::HornetHit3);
                use(CommonSound::ClawStrike, CommonSound::ClawMiss);
            }
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Controller))
            {
                use(CommonSound::ControllerIdle, CommonSound::ControllerDie);
                use(CommonSound::Electro4, CommonSound::Electro4);
            }
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Snark))
                use(CommonSound::SnarkDeploy, CommonSound::SnarkBlast);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Osprey))
                use(CommonSound::OspreyRotor, CommonSound::MortarHit);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Assassin))
                use(CommonSound::AssassinShot1, CommonSound::AssassinShot2);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Ichthyosaur))
                use(CommonSound::IckyIdle, CommonSound::IckyDie);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Tentacle))
                use(CommonSound::TentacleAlert, CommonSound::Squirm);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Boid))
                use(CommonSound::BoidIdle, CommonSound::BoidAlert);
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Nihilanth))
            {
                use(CommonSound::BeamStart, CommonSound::NihilDie);
                use(CommonSound::Electro4, CommonSound::Electro4);
                use(CommonSound::SlaveAlert, CommonSound::SlaveDie);
                use(CommonSound::ControllerIdle, CommonSound::ControllerDie);
            }
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::BigMomma))
            {
                use(CommonSound::MommaAlert, CommonSound::MommaStep);
                use(CommonSound::ClawStrike, CommonSound::ClawMiss);
                use(CommonSound::SpitHit, CommonSound::SpitHit);
                use(CommonSound::HeadcrabAlert, CommonSound::HeadcrabBite);
            }
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Apache))
            {
                use(CommonSound::ApacheRotor, CommonSound::ApacheRotor);
                use(CommonSound::MortarHit, CommonSound::MortarHit);
                use(CommonSound::TurretFire, CommonSound::TurretFire);
                use(CommonSound::RocketFire, CommonSound::RocketFire);
            }
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Gargantua))
            {
                use(CommonSound::GargIdle, CommonSound::GargFlameOff);
                use(CommonSound::ClawStrike, CommonSound::ClawMiss);
            }
        }
        // What the people say (the game's talkmonster groups): for each voice
        // whose model the map has (its own people, and those who may come in
        // from next door), a few of each group's lines.
        bool voices[kVoices] = {
            std::find(models.begin(), models.end(), "scientist") != models.end(),
            std::find(models.begin(), models.end(), "barney") != models.end()};
        const bool talk = voices[kVoiceScientist] || voices[kVoiceBarney];
        // What soldiers call out: the first few of each of the game's lists.
        if (soldiers || talk)
            out.speech.assign(kSpeechGroups * kSpeechLines, 0xFFFF);
        if (talk)
        {
            // (Each group's heading, then its lines.)
            std::vector<std::uint16_t> lines;
            std::vector<std::uint16_t> heading(kTalkHeading, 0);
            for (int voice = 0; voice < kVoices; ++voice)
            {
                for (int slot = 0; slot < kTalkSlots; ++slot)
                {
                    const int g = voice * kTalkSlots + slot;
                    heading[g * 2] = static_cast<std::uint16_t>(lines.size());
                    const char *group = talkGroupName(voice, slot);
                    if (!voices[voice] || group == nullptr)
                        continue;
                    if (group[0] == '!')
                    {
                        const int index = sentenceIndex(group + 1);
                        if (index >= 0)
                            lines.push_back(static_cast<std::uint16_t>(index));
                    }
                    else
                    {
                        for (int n = 0; n < 32 && static_cast<int>(lines.size()) - heading[g * 2] < kTalkLines; ++n)
                        {
                            const int index = sentenceIndex(std::string(group) + std::to_string(n));
                            if (index >= 0)
                                lines.push_back(static_cast<std::uint16_t>(index));
                        }
                    }
                    heading[g * 2 + 1] = static_cast<std::uint16_t>(lines.size() - heading[g * 2]);
                }
            }
            out.speech.insert(out.speech.end(), heading.begin(), heading.end());
            out.speech.insert(out.speech.end(), lines.begin(), lines.end());
        }
        if (soldiers)
        {
            static const char *const kGroups[kSpeechGroups] = {"HG_GREN",   "HG_ALERT", "HG_COVER", "HG_THROW",
                                                               "HG_TAUNT",  "HG_CHARGE", "HG_IDLE",  "HG_QUEST",
                                                               "HG_ANSWER", "HG_CLEAR", "HG_CHECK",  "HG_MONST"};
            for (int g = 0; g < kSpeechGroups; ++g)
            {
                int kept = 0;
                for (int n = 0; n < 16 && kept < kSpeechLines; ++n)
                {
                    const int index = sentenceIndex(std::string(kGroups[g]) + std::to_string(n));
                    if (index >= 0)
                        out.speech[g * kSpeechLines + kept++] = static_cast<std::uint16_t>(index);
                }
            }
        }
        for (int c = 0; c < commonCount; ++c)
        {
            if (!soundUsed[c] && !soundLazy[c])
                out.sounds[c].clear();
        }
        out.soundLazy.assign(out.sounds.size(), false);
        for (int c = 0; c < commonCount; ++c)
            out.soundLazy[c] = !soundUsed[c] && soundLazy[c];
        out.logicSounds = std::move(heard);

        out.nameHashes.assign(nameIds.size() + 1, 0);
        for (const auto &entry : nameIds)
            out.nameHashes[entry.second] = nameHash(entry.first.c_str());
        out.names = static_cast<int>(nameIds.size());
    }
} // namespace hlds
