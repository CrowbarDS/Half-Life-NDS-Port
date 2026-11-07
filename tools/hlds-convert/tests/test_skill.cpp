// How hard the game is (shared/HlSkill.hpp): the game's skill.cfg, every
// number of it, at the level a game was begun at -- and what the level
// changes besides the numbers: the wall chargers (how much they hold, and
// how they sound as USE is held on them: CWallHealth, CRecharge), the rules
// the SDK keeps for DIFFICULT and EASY, and autoaim.
//
// The table is checked against the game's own skill.cfg; the rest is played
// on the game's maps. Needs HLDS_VALVE_DIR for all but the first part.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>

#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    /// A map played at a level: its switches say how hard.
    struct Played
    {
        Globals globals;
        Game game;
    };

    std::unique_ptr<Played> load(const char *dir, const char *map, int level)
    {
        auto played = std::make_unique<Played>();
        played->globals.set(kGlobalSkill, static_cast<std::uint8_t>(level));
        std::string error;
        if (!played->game.bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
        {
            check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
            return nullptr;
        }
        finish(played->game, &played->globals, dir);
        return played->game.ok ? std::move(played) : nullptr;
    }

    void tick(Game &g, int ms = 33)
    {
        logicTick(g.logic, g.player, ms);
        monstersTick(g.logic, g.player, ms);
        g.drain();
    }

    int findKind(const Game &g, ActorKind kind, int nth = 0)
    {
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(kind) && nth-- == 0)
                return i;
        }
        return -1;
    }

    // ------------------------------------------------------------ the table

    void testTable(const char *dir)
    {
        struct Field
        {
            const char *name;
            int SkillData::*member;
        };
        static const Field kFields[] = {
            {"sk_agrunt_health", &SkillData::agruntHealth},
            {"sk_agrunt_dmg_punch", &SkillData::agruntDmgPunch},
            {"sk_apache_health", &SkillData::apacheHealth},
            {"sk_barney_health", &SkillData::barneyHealth},
            {"sk_bullsquid_health", &SkillData::bullsquidHealth},
            {"sk_bullsquid_dmg_bite", &SkillData::bullsquidDmgBite},
            {"sk_bullsquid_dmg_whip", &SkillData::bullsquidDmgWhip},
            {"sk_bullsquid_dmg_spit", &SkillData::bullsquidDmgSpit},
            {"sk_bigmomma_health_factor", &SkillData::bigmommaHealthFactorTenths},
            {"sk_bigmomma_dmg_slash", &SkillData::bigmommaDmgSlash},
            {"sk_bigmomma_dmg_blast", &SkillData::bigmommaDmgBlast},
            {"sk_bigmomma_radius_blast", &SkillData::bigmommaRadiusBlast},
            {"sk_gargantua_health", &SkillData::gargantuaHealth},
            {"sk_gargantua_dmg_slash", &SkillData::gargantuaDmgSlash},
            {"sk_gargantua_dmg_fire", &SkillData::gargantuaDmgFire},
            {"sk_gargantua_dmg_stomp", &SkillData::gargantuaDmgStomp},
            {"sk_hassassin_health", &SkillData::hassassinHealth},
            {"sk_headcrab_health", &SkillData::headcrabHealth},
            {"sk_headcrab_dmg_bite", &SkillData::headcrabDmgBite},
            {"sk_hgrunt_health", &SkillData::hgruntHealth},
            {"sk_hgrunt_kick", &SkillData::hgruntKick},
            {"sk_hgrunt_pellets", &SkillData::hgruntPellets},
            {"sk_hgrunt_gspeed", &SkillData::hgruntGspeed},
            {"sk_houndeye_health", &SkillData::houndeyeHealth},
            {"sk_houndeye_dmg_blast", &SkillData::houndeyeDmgBlast},
            {"sk_islave_health", &SkillData::islaveHealth},
            {"sk_islave_dmg_claw", &SkillData::islaveDmgClaw},
            {"sk_islave_dmg_clawrake", &SkillData::islaveDmgClawrake},
            {"sk_islave_dmg_zap", &SkillData::islaveDmgZap},
            {"sk_ichthyosaur_health", &SkillData::ichthyosaurHealth},
            {"sk_ichthyosaur_shake", &SkillData::ichthyosaurShake},
            {"sk_leech_health", &SkillData::leechHealth},
            {"sk_leech_dmg_bite", &SkillData::leechDmgBite},
            {"sk_controller_health", &SkillData::controllerHealth},
            {"sk_controller_dmgzap", &SkillData::controllerDmgzap},
            {"sk_controller_speedball", &SkillData::controllerSpeedball},
            {"sk_controller_dmgball", &SkillData::controllerDmgball},
            {"sk_nihilanth_health", &SkillData::nihilanthHealth},
            {"sk_nihilanth_zap", &SkillData::nihilanthZap},
            {"sk_scientist_health", &SkillData::scientistHealth},
            {"sk_snark_health", &SkillData::snarkHealth},
            {"sk_snark_dmg_bite", &SkillData::snarkDmgBite},
            {"sk_snark_dmg_pop", &SkillData::snarkDmgPop},
            {"sk_zombie_health", &SkillData::zombieHealth},
            {"sk_zombie_dmg_one_slash", &SkillData::zombieDmgOneSlash},
            {"sk_zombie_dmg_both_slash", &SkillData::zombieDmgBothSlash},
            {"sk_turret_health", &SkillData::turretHealth},
            {"sk_miniturret_health", &SkillData::miniturretHealth},
            {"sk_sentry_health", &SkillData::sentryHealth},
            {"sk_plr_crowbar", &SkillData::plrCrowbar},
            {"sk_plr_9mm_bullet", &SkillData::plr9mmBullet},
            {"sk_plr_357_bullet", &SkillData::plr357Bullet},
            {"sk_plr_9mmAR_bullet", &SkillData::plr9mmARBullet},
            {"sk_plr_9mmAR_grenade", &SkillData::plr9mmARGrenade},
            {"sk_plr_buckshot", &SkillData::plrBuckshot},
            {"sk_plr_xbow_bolt_client", &SkillData::plrXbowBoltClient},
            {"sk_plr_xbow_bolt_monster", &SkillData::plrXbowBoltMonster},
            {"sk_plr_rpg", &SkillData::plrRpg},
            {"sk_plr_gauss", &SkillData::plrGauss},
            {"sk_plr_egon_narrow", &SkillData::plrEgonNarrow},
            {"sk_plr_egon_wide", &SkillData::plrEgonWide},
            {"sk_plr_hand_grenade", &SkillData::plrHandGrenade},
            {"sk_plr_satchel", &SkillData::plrSatchel},
            {"sk_plr_tripmine", &SkillData::plrTripmine},
            {"sk_12mm_bullet", &SkillData::monster12mmBullet},
            {"sk_9mmAR_bullet", &SkillData::monster9mmARBullet},
            {"sk_9mm_bullet", &SkillData::monster9mmBullet},
            {"sk_hornet_dmg", &SkillData::hornetDmg},
            {"sk_suitcharger", &SkillData::suitcharger},
            {"sk_battery", &SkillData::battery},
            {"sk_healthcharger", &SkillData::healthcharger},
            {"sk_healthkit", &SkillData::healthkit},
            {"sk_scientist_heal", &SkillData::scientistHeal},
            {"sk_monster_head", &SkillData::monsterHead},
            {"sk_monster_chest", &SkillData::monsterChest},
            {"sk_monster_stomach", &SkillData::monsterStomach},
            {"sk_monster_arm", &SkillData::monsterArm},
            {"sk_monster_leg", &SkillData::monsterLeg},
            {"sk_player_head", &SkillData::playerHead},
            {"sk_player_chest", &SkillData::playerChest},
            {"sk_player_stomach", &SkillData::playerStomach},
            {"sk_player_arm", &SkillData::playerArm},
            {"sk_player_leg", &SkillData::playerLeg}
        };
        check(sizeof(kFields) / sizeof(kFields[0]) == 83, "every family of skill.cfg has its field");
        check(skillData(kSkillEasy).healthcharger == 50 && skillData(kSkillMedium).healthcharger == 40 &&
                  skillData(kSkillHard).healthcharger == 25 && skillData(0).headcrabHealth == 10 &&
                  skillData(9).suitcharger == 50,
              "the three levels, and MEDIUM for anything else");
        if (dir == nullptr)
            return;
        std::ifstream file(std::string(dir) + "/skill.cfg");
        std::stringstream text;
        text << file.rdbuf();
        const std::string all = text.str();
        int compared = 0, wrong = 0;
        const std::regex line(R"re((sk_\w+?)([123])\s+"([^"]+)")re");
        for (auto it = std::sregex_iterator(all.begin(), all.end(), line); it != std::sregex_iterator(); ++it)
        {
            const std::string name = (*it)[1];
            const int level = std::stoi((*it)[2]);
            const double value = std::stod((*it)[3]);
            for (const Field &field : kFields)
            {
                if (name != field.name)
                    continue;
                const int ours = skillData(level).*field.member;
                const double want = name == "sk_bigmomma_health_factor" ? value * 10 : value;
                ++compared;
                if (std::lround(want) != ours)
                {
                    ++wrong;
                    std::printf("test_skill: %s%d is %s, the table has %d\n", name.c_str(), level,
                                std::string((*it)[3]).c_str(), ours);
                }
            }
        }
        std::printf("test_skill: %d values of skill.cfg against the table, %d wrong\n", compared, wrong);
        check(compared == 83 * 3 && wrong == 0, "the table is skill.cfg's, value for value");
    }

    // ------------------------------------------------------- the levels

    void testLevels()
    {
        LogicWorld w;
        check(skillLevelOf(w) == kSkillMedium, "a game with no switches is MEDIUM");
        Globals globals;
        w.globals = &globals;
        check(skillLevelOf(w) == kSkillMedium, "... and one whose switches say nothing (a save from before)");
        globals.set(kGlobalSkill, 3);
        check(skillLevelOf(w) == kSkillHard && skillOf(w).zombieHealth == 100, "DIFFICULT, with its numbers");
        globals.set(kGlobalSkill, 7);
        check(skillLevelOf(w) == kSkillMedium, "a level there is not is MEDIUM");
        check(skillHealth(skillData(kSkillHard), static_cast<std::uint8_t>(ActorKind::Headcrab), 0) == 20 &&
                  skillHealth(skillData(kSkillEasy), static_cast<std::uint8_t>(ActorKind::Headcrab), 0) == 10 &&
                  skillHealth(skillData(kSkillMedium), static_cast<std::uint8_t>(ActorKind::BigMomma), 0) == 225,
              "a monster's health by its kind and the level");
        // A health kit: 15, 15, 10.
        for (int level = kSkillEasy; level <= kSkillHard; ++level)
        {
            PlayerState player;
            player.health = 50;
            std::uint32_t got = 0;
            hldata::detail::itemTake(player, static_cast<std::uint8_t>(ItemKind::HealthKit), got, skillData(level));
            check(player.health == 50 + (level == kSkillHard ? 10 : 15), "a health kit gives the level's");
        }
    }

    /// c1a1's zombie is made with the level's health.
    void testHealth(const char *dir)
    {
        const int want[3] = {50, 50, 100};
        for (int level = kSkillEasy; level <= kSkillHard; ++level)
        {
            auto played = load(dir, "c1a1", level);
            check(played != nullptr, "c1a1 loads");
            if (!played)
                return;
            Game &g = played->game;
            g.player.origin[2] += fxFromInt(30000);
            for (int n = 0; n < 10; ++n)
                tick(g);
            const int zombie = findKind(g, ActorKind::Zombie);
            const AiMonster *m = zombie >= 0 ? aiFind(g.actors, zombie) : nullptr;
            check(m != nullptr && m->maxHealth == want[level - 1] && g.actors.states[zombie].health == want[level - 1],
                  "a zombie has the level's health");
        }
    }

    // -------------------------------------------------------- the chargers

    /// What a frame of holding USE on c1a0's health charger (or not) made
    /// heard of it.
    struct Heard
    {
        int start = 0, loopOn = 0, loopOff = 0, refused = 0;
    };

    Heard frame(Game &g, int charger, bool held)
    {
        logicTick(g.logic, g.player, 33);
        if (held)
            logicUse(g.logic, g.player, charger, -1, kUseSet);
        monstersTick(g.logic, g.player, 33);
        Heard h;
        const LogicSound &s = g.logic.sounds[charger];
        for (int i = 0; i < g.heard.count; ++i)
        {
            const SoundEvent &e = g.heard.events[i];
            if (e.tag != charger && !(e.flags & kSoundLoopOff))
                continue;
            if (e.flags & kSoundLoopOff)
                h.loopOff += e.tag == charger;
            else if (e.flags & kSoundLoopOn)
                h.loopOn += e.sound == s.move;
            else if (e.sound == s.unlocked)
                ++h.start;
            else if (e.sound == s.locked)
                ++h.refused;
        }
        g.drain();
        return h;
    }

    void testChargers(const char *dir)
    {
        auto played = load(dir, "c1a0", kSkillMedium);
        check(played != nullptr, "c1a0 loads");
        if (!played)
            return;
        Game &g = played->game;
        const int charger = g.recordOfKind(LogicKind::HealthCharger);
        check(charger >= 0, "c1a0 has a health charger");
        if (charger < 0)
            return;
        const LogicSound &s = g.logic.sounds[charger];
        check(s.unlocked != kNoSound && s.move != kNoSound && s.locked != kNoSound, "... with its three sounds");
        g.player.inv.have |= kHaveSuit;
        g.player.health = 60;
        // Held: its first sound at once, its loop once that is done.
        int startAt = -1, loopAt = -1, given = 0, at100 = -1;
        for (int n = 0; n < 60; ++n)
        {
            const Heard h = frame(g, charger, true);
            startAt = startAt < 0 && h.start > 0 ? n : startAt;
            loopAt = loopAt < 0 && h.loopOn > 0 ? n : loopAt;
            given = g.player.health - 60;
            at100 = at100 < 0 && g.player.health >= 100 ? n : at100;
        }
        std::printf("test_skill: the health charger held: its sound at frame %d, its loop at %d, %d health in two "
                    "seconds\n", startAt, loopAt, given);
        check(startAt == 0, "held, it is heard at once");
        check(loopAt >= 17 && loopAt <= 21, "... and its loop once its first sound is done (0.56 s, at a point)");
        check(given >= 13 && given <= 21, "... and it gives a point every tenth of a second, as it is let");
        // Let go: off within a quarter of a second.
        int offAt = -1;
        for (int n = 0; n < 20 && offAt < 0; ++n)
            offAt = frame(g, charger, false).loopOff > 0 ? n : -1;
        check(offAt >= 6 && offAt <= 8 && !g.states[charger].running, "let go, its loop stops a quarter of a second on");
        // Filled while it is held: its loop stops then and there.
        // (Enough to want that its loop starts.)
        for (int n = 0; n < 30; ++n)
            frame(g, charger, false);
        g.player.health = 85;
        int filledOff = -1, filledAt = -1, loopedAfter = 0;
        bool runningAtFull = false;
        for (int n = 0; n < 120; ++n)
        {
            const Heard h = frame(g, charger, true);
            if (filledOff < 0 && h.loopOff > 0)
                filledOff = n;
            if (filledAt < 0 && g.player.health >= 100)
            {
                filledAt = n;
                runningAtFull = g.states[charger].running;
            }
            else if (filledAt >= 0)
                loopedAfter += h.loopOn;
        }
        check(filledOff >= 0 && filledOff == filledAt && !runningAtFull && g.player.health == 100 &&
                  !g.states[charger].running && loopedAfter == 0,
              "held until they are full, it stops on the point that fills them");
        // Filled on the very point its loop would start: no loop at all.
        for (int n = 0; n < 30; ++n)
            frame(g, charger, false);
        g.player.health = 98;
        Heard brief;
        for (int n = 0; n < 60; ++n)
        {
            const Heard h = frame(g, charger, true);
            brief.loopOn += h.loopOn;
            brief.start += h.start;
        }
        check(brief.start == 1 && brief.loopOn == 0 && g.player.health == 100 && !g.states[charger].running,
              "two points short, it is heard once and never loops");
        // Let go on the point that fills them: off already, not a quarter
        // of a second on.
        for (int n = 0; n < 30; ++n)
            frame(g, charger, false);
        g.player.health = 99;
        frame(g, charger, true);
        check(g.player.health == 100 && !g.states[charger].running, "the last point turns it off at once");
        // Held by someone full: not a sound, and nothing spent.
        const int spent = static_cast<int>(g.states[charger].bits);
        Heard full;
        for (int n = 0; n < 30; ++n)
        {
            const Heard h = frame(g, charger, true);
            full.start += h.start;
            full.loopOn += h.loopOn;
            full.refused += h.refused;
        }
        check(full.start == 0 && full.loopOn == 0 && full.refused == 0 &&
                  static_cast<int>(g.states[charger].bits) == spent,
              "held by someone full, it is silent and spends nothing");
        // A tap: its sound, and one point.
        for (int n = 0; n < 30; ++n)
            frame(g, charger, false);
        g.player.health = 50;
        Heard tap;
        for (int n = 0; n < 30; ++n)
        {
            const Heard h = frame(g, charger, n == 0);
            tap.start += h.start;
            tap.loopOn += h.loopOn;
        }
        check(tap.start == 1 && tap.loopOn == 0 && g.player.health == 51, "tapped, it is heard and gives one point");
        // No suit: it refuses, so often as it is heard, and gives nothing.
        g.player.inv.have &= ~kHaveSuit;
        int refused = 0;
        for (int n = 0; n < 60; ++n)
            refused += frame(g, charger, true).refused;
        check(refused >= 3 && refused <= 4 && g.player.health == 51, "without the suit it refuses, every 0.62 s");

        // How much it holds: 50, 40, 25.
        const int hold[3] = {50, 40, 25};
        for (int level = kSkillEasy; level <= kSkillHard; ++level)
        {
            auto fresh = load(dir, "c1a0", level);
            if (!fresh)
                return;
            Game &f = fresh->game;
            f.player.inv.have |= kHaveSuit;
            f.player.health = 1;
            int denied = 0;
            for (int n = 0; n < 30 * 12; ++n)
                denied += frame(f, charger, true).refused;
            check(f.player.health == 1 + hold[level - 1] && denied > 0 &&
                      f.brushStates[f.view.logic[charger].brush].frame == 1,
                  "a charger holds the level's, then refuses (and shows it is empty)");
        }
    }

    // --------------------------------------------------------- the rules

    void testRules(const char *dir)
    {
        // A grunt of a squad, run for cover with a grenade to hand: on
        // DIFFICULT he tosses it first.
        for (int level : {kSkillMedium, kSkillHard})
        {
            auto played = load(dir, "c1a3a", level);
            if (!played)
                return;
            Game &g = played->game;
            g.player.origin[2] += fxFromInt(30000);
            for (int n = 0; n < 10; ++n)
                tick(g);
            int index = -1;
            for (int i = 0; i < g.actors.count && index < 0; ++i)
            {
                const AiMonster *m = aiFind(g.actors, i);
                if (g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Grunt) && m != nullptr &&
                    m->squadLeader != kAiNone)
                    index = i;
            }
            check(index >= 0, "c1a3a has a grunt in a squad");
            if (index < 0)
                return;
            AiCtx c{g.logic, g.player, g.actors, g.ai, index, g.actors.states[index], *aiOf(g.actors, index),
                    grunt::kind()};
            aiSet(c, kCondCanRangeAttack2);
            const AiSchedule *schedule = grunt::scheduleOfType(c, kSchedTakeCoverFromEnemy);
            check(schedule == (level == kSkillHard ? &grunt::slTossGrenadeCover : &grunt::slTakeCover),
                  level == kSkillHard ? "DIFFICULT: a grunt tosses a grenade as he runs for cover"
                                      : "MEDIUM: he only runs");
        }
        // An assassin standing her ground: on DIFFICULT she fades, to a
        // twentieth of herself; walking, she is seen again.
        for (int level : {kSkillMedium, kSkillHard})
        {
            auto played = load(dir, "c2a3d", level);
            if (!played)
                return;
            Game &g = played->game;
            g.player.origin[2] += fxFromInt(30000);
            tick(g);
            const int k = findKind(g, ActorKind::Assassin);
            check(k >= 0 && aiOf(g.actors, k) != nullptr, "c2a3d has its assassins");
            if (k < 0 || aiOf(g.actors, k) == nullptr)
                return;
            AiCtx c{g.logic, g.player, g.actors, g.ai, k, g.actors.states[k], *aiOf(g.actors, k), assassin::kind()};
            c.m.enemy = kAiPlayer;
            c.m.activity = kActIdle;
            c.m.flag2 = false;
            for (int n = 0; n < 6; ++n)
                assassin::runAI(c);
            const int hidden = c.me.cloak.value;
            c.m.activity = kActRun;
            assassin::runAI(c);
            const int moving = c.me.cloak.value;
            check(level == kSkillHard ? hidden == 235 && moving == 185 : hidden == 0 && moving == 0,
                  level == kSkillHard ? "DIFFICULT: an assassin standing still fades; moving, she comes back"
                                      : "MEDIUM: she is always seen");
        }
        // An alien slave's zap: on DIFFICULT the rest of it half again as
        // fast.
        for (int level : {kSkillMedium, kSkillHard})
        {
            auto played = load(dir, "c2a1", level);
            if (!played)
                return;
            Game &g = played->game;
            g.player.origin[2] += fxFromInt(30000);
            tick(g);
            int k = -1;
            for (int i = 0; i < g.actors.count && k < 0; ++i)
            {
                if (g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Slave) && aiOf(g.actors, i) != nullptr)
                    k = i;
            }
            if (k < 0)
            {
                check(false, "c2a1 has an alien slave");
                return;
            }
            AiCtx c{g.logic, g.player, g.actors, g.ai, k, g.actors.states[k], *aiOf(g.actors, k), slave::kind()};
            slave::animEvent(c, slave::kZapPowerUp);
            check((c.me.quickClip.value != 0) == (level == kSkillHard),
                  level == kSkillHard ? "DIFFICULT: an alien slave's zap goes half again as fast" : "MEDIUM: as made");
        }
    }

    // --------------------------------------------------------- autoaim

    /// From where the map starts the player, the first way with room: a
    /// zombie put 200 units along it, and the aim 8 degrees to its left.
    void testAutoaim(const char *dir)
    {
        int bent[3] = {0, 0, 0};
        bool hits[3] = {false, false, false};
        for (int level = kSkillEasy; level <= kSkillHard; ++level)
        {
            auto played = load(dir, "c1a1", level);
            if (!played)
                return;
            Game &g = played->game;
            for (int n = 0; n < 3; ++n)
                tick(g);
            const int zombie = findKind(g, ActorKind::Zombie);
            if (zombie < 0)
            {
                check(false, "c1a1 has its zombie");
                return;
            }
            const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + fxFromInt(28)};
            int way = -1;
            for (int yaw = 0; yaw < 360 && way < 0; yaw += 15)
            {
                fx s, c;
                fxSinCos(static_cast<std::uint16_t>(yaw * 65536 / 360), s, c);
                const fx far[3] = {eye[0] + c * 320, eye[1] + s * 320, eye[2]};
                if (traceShot(*g.logic.world, eye, far).fraction >= kFractionOne)
                    way = yaw;
            }
            check(way >= 0, "room in front of the start");
            if (way < 0)
                return;
            fx s, c;
            fxSinCos(static_cast<std::uint16_t>(way * 65536 / 360), s, c);
            ActorState &z = g.actors.states[zombie];
            z.pos[0] = eye[0] + c * 200;
            z.pos[1] = eye[1] + s * 200;
            z.pos[2] = eye[2] - fxFromInt(36 + 28);
            {
                // (Its middle at the eye's height.)
                fx bm[3], bx[3];
                targetBox(z, bm, bx);
                z.pos[2] += eye[2] - (bm[2] + bx[2]) / 2;
            }
            // The other monsters out of the way.
            for (int i = 0; i < g.actors.count; ++i)
            {
                if (i != zombie && fights(g.actors.states[i].kind))
                    g.actors.states[i].active = false;
            }
            const int off = way + 8;
            fx os, oc;
            fxSinCos(static_cast<std::uint16_t>(off * 65536 / 360), os, oc);
            const fx forward[3] = {oc, os, 0}, right[3] = {os, -oc, 0}, up[3] = {0, 0, kFxOne};
            fx aimed[3];
            autoaim(g.logic, g.player, eye, forward, right, up, autoaimCone(kWeaponGlock, false), aimed);
            bent[level - 1] = g.player.autoAim.value.yaw;
            fx mins[3], maxs[3];
            targetBox(z, mins, maxs);
            const fx far[3] = {eye[0] + aimed[0] * 400, eye[1] + aimed[1] * 400, eye[2] + aimed[2] * 400};
            std::int64_t enter;
            hits[level - 1] = lineEntersBox(eye, far, mins, maxs, enter);
            if (level == kSkillMedium)
            {
                // A scientist there instead: a friend, not aimed at.
                const int person = findKind(g, ActorKind::Scientist);
                if (person >= 0)
                {
                    z.active = false;
                    ActorState &p = g.actors.states[person];
                    for (int k = 0; k < 3; ++k)
                        p.pos[k] = z.pos[k];
                    autoaim(g.logic, g.player, eye, forward, right, up, autoaimCone(kWeaponGlock, false), aimed);
                    check(g.player.autoAim.value.yaw == 0 && !g.player.autoAim.value.onTarget,
                          "a friend is never aimed at");
                }
                // The setting off: straight.
                z.active = true;
                g.logic.noAutoaim.value = true;
                autoaim(g.logic, g.player, eye, forward, right, up, autoaimCone(kWeaponGlock, false), aimed);
                check(aimed[0] == forward[0] && aimed[1] == forward[1], "AUTOAIM off: straight");
            }
        }
        const int eight = 8 * 65536 / 360;
        std::printf("test_skill: 8 degrees off a zombie, the pistol's aim bent by %d / %d / %d (of %d), hits %d %d %d\n",
                    bent[0], bent[1], bent[2], eight, hits[0], hits[1], hits[2]);
        check(bent[2] == 0 && !hits[2], "DIFFICULT: straight, and a miss");
        check(bent[1] < 0 && std::abs(std::abs(bent[1]) - eight * 9 / 10) < eight / 10 && hits[1],
              "MEDIUM: nine tenths of the way, and a hit");
        check(bent[0] < 0 && std::abs(std::abs(bent[0]) - eight / 3) < eight / 10, "EASY: a third of the way");
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    testTable(dir != nullptr && dir[0] != '\0' ? dir : nullptr);
    testLevels();
    if (dir == nullptr || dir[0] == '\0')
    {
        std::printf("test_skill: HLDS_VALVE_DIR not set, the maps were not played\n");
        if (g_failures == 0)
            std::printf("test_skill: ok\n");
        return g_failures == 0 ? 0 : 1;
    }
    testHealth(dir);
    testChargers(dir);
    testRules(dir);
    testAutoaim(dir);
    if (g_failures == 0)
        std::printf("test_skill: ok\n");
    return g_failures == 0 ? 0 : 1;
}
