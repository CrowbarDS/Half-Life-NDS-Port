#pragma once

#include "EngineObject.hpp"

// The running map: everything about it that changes.
//
// The level file is read-only tables. This holds the other half -- where each
// brush entity is right now, what each logic record is doing, the collision's
// picture of both -- and advances it, once per displayed frame:
//
//   1. the entity logic (shared/HlLogic.hpp): timers, doors and lifts moving,
//      triggers the player has walked into. Moving brushes carry and push the
//      player here.
//   2. USE, if the player pressed it.
//   3. the player's own movement, against the world as the logic left it.
//
// The order matters: a lift moves and takes the player with it BEFORE the
// player's move looks for the ground, or they would fall through it for a
// frame every frame.
//
// One arrays-and-a-loop design on purpose (see HlLogic.hpp). `game` is it.

#include <string>
#include <vector>

#include "HlLamp.hpp"
#include "HlLightStyle.hpp"
#include "HlMonsters.hpp"
#include "HlSave.hpp"
#include "HlTransition.hpp"

namespace hldata::detail
{
    struct StreamOut; // (HlSaveState.hpp)
    struct StreamIn;
}

namespace hlrt
{
    class Game
    {
    public:
        /// Everything the player can collide with. The player's movement and
        /// the logic both work on this one.
        hldata::MoveWorld world;
        hldata::LogicWorld logic;
        /// The map's actors: its own, and room for those who arrive with the
        /// player.
        hldata::ActorWorld actors;
        /// What the player holds, as an actor of its own: it is drawn at the
        /// eye, turned as the view is. Inactive on a map without one.
        hldata::ActorState held;
        /// What has been heard and not yet played (audio/Sound empties it).
        hldata::SoundQueue heard;
        /// The suit's lamp; and, while it is alight, its beam as it is this
        /// frame (`lampLit`: it has one).
        hldata::LampState lamp;
        hldata::LampBeam lampBeam;
        /// The COMMAND line's cheats, for the session (not saved): the
        /// player's state points here.
        hldata::Cheats cheats;
        /// What the game has told the achievements this frame (the logic
        /// points here once a map is loaded: Achievements::attach).
        hldata::AchEvents achievements;
        /// Whether the crosshair is drawn (the COMMAND line's crosshair).
        bool crosshair = true;
        /// Whether the guns' aim may bend towards a monster (the OPTIONS'
        /// AUTOAIM, the game's sv_aim): put here each frame from the
        /// settings, for the game to give its logic.
        bool autoaim = false;
        /// The models of what the player's weapons throw and leave about, by
        /// the projectile's kind (a hand grenade, the launcher's, and
        /// kShotBolt..kShotHornet), found by name when the map is set up: -1
        /// where the map has not got one (its mark is drawn instead).
        int thrownModel(int kind) const
        {
            if (kind == hldata::kShotGrenade)
                return grenade_[0];
            if (kind == hldata::kShotContact)
                return grenade_[1];
            // (The monsters' snarks and hornets are the player's to look at.)
            if (kind == hldata::kShotSnarkFoe)
                kind = hldata::kShotSnark;
            else if (kind == hldata::kShotHornetFoe)
                kind = hldata::kShotHornet;
            else if (kind == hldata::kShotRocketFoe)
                kind = hldata::kShotRocket; // (a tank's: the RPG's rocket)
            return kind >= hldata::kShotBolt && kind <= hldata::kShotHornet ? thrown_[kind - hldata::kShotBolt] : -1;
        }
        /// The model (into the map's ModelRefs) a spent casing of piece
        /// kind `kind` is drawn as -- shell.mdl, shotgunshell.mdl -- or -1:
        /// another kind, or a map that has not got it (it is a triangle).
        int shellModel(int kind) const
        {
            return kind == hldata::kPieceShell ? shell_[0] : (kind == hldata::kPieceShotShell ? shell_[1] : -1);
        }
        bool lampLit = false;
        int lampDistance = 0; ///< how far off the lamp's light is, in units
        /// The switches that outlive a map. Not reset by create() or
        /// destroy(): only a new game clears them.
        hldata::Globals globals;

        /// Allocate and initialise the live state for the loaded level.
        bool create();
        void destroy();

        /// Advance by `vblanks` sixtieths of a second.
        void frame(int vblanks);

        /// The ChangeLevel record the player has set off, or -1. main() acts
        /// on it between frames.
        int pendingChange() const { return logic.pendingChange; }
        /// What crosses to the next map, for the pending change.
        hldata::Arrival makeArrival() const;
        /// Put the player where `arrival` says they are in this map. False if
        /// it cannot (no such landmark): they stay at the map's own start.
        bool applyArrival(const hldata::Arrival &arrival);

        // ---- what outlives a map, and saving it ----

        /// Forget everything a game in progress has built up: a new game.
        void resetProgress();
        /// Note what is broken in the map that is loaded, under its name's
        /// hash. Call before the map is let go, and before saving.
        void rememberMap(std::uint32_t mapHash);
        /// Break again what was broken the last time this map was here. Call
        /// once the map is up.
        void restoreMap(std::uint32_t mapHash);
        /// Everything a save holds, for the map called `map`.
        hldata::SaveData snapshot(const std::string &map);
        /// Take a save's switches and broken things as the game's own. Call
        /// BEFORE loading its map: the map reads the switches as it starts.
        void adoptProgress(const hldata::SaveData &save);
        /// Put the player (and the train they rode) where the save has them.
        /// Call once its map is up.
        void placeFromSave(const hldata::SaveData &save);
        /// Who came into a save's map with the player, if anyone did: what
        /// its map is to be begun with (loadLevel's arrival), so that they
        /// are there for the save's state to be laid over. False if it has
        /// none.
        static bool arrivalOf(const hldata::SaveData &save, hldata::Arrival &out);
        /// The whole of the map's state as the save has it, laid over the
        /// map just begun: every door, trigger and actor as it was. False
        /// if the save has none that fits this map as it is cooked -- the
        /// map is then as it begins, with the player put back (the older,
        /// coarser way). Call after placeFromSave.
        bool layState(const hldata::SaveData &save);
        /// What a save says of itself: where, the chapter, how long played.
        hldata::SaveHeading heading(const std::string &map) const;

        // ---- the maps left, kept to come back to (HlMapStore.hpp) ----

        /// How long what storeMap writes is.
        std::uint32_t storedBytes();
        /// The whole of the map as it is left -- its state and its monsters'
        /// minds -- through `out`. Call after makeArrival (who went along is
        /// leftWith()), before the map is let go.
        bool storeMap(hldata::detail::StreamOut &out);
        /// ... laid back over the map just begun (before applyArrival), and
        /// those of sentAway who `landed` (bit by bit) taken out of it. False
        /// if it does not fit the map as it is cooked -- or ran out part of
        /// the way, and the map is to be begun again.
        bool layStored(hldata::detail::StreamIn &in, const std::int16_t *sentAway, std::uint8_t landed);
        /// Which actors went along with the player at the last makeArrival
        /// (-1: none), and which of an Arrival's visitors the last
        /// applyArrival placed (bit by bit).
        const std::int16_t *leftWith() const { return leftWith_; }
        std::uint8_t placed() const { return placed_; }
        /// Seconds played, this game.
        std::uint32_t playSeconds() const { return static_cast<std::uint32_t>(playMs_ / 1000); }

        /// Where each brush entity is, for the renderer.
        const hldata::BrushState *brushStates() const { return brushStates_; }

        Game() = default;
        Game(const Game &) = delete;
        Game &operator=(const Game &) = delete;

    private:
        hldata::BrushState *brushStates_ = nullptr;
        hldata::MoverHull *movers_ = nullptr;
        hldata::MoverReach *reach_ = nullptr; ///< MoveWorld::reach
        hldata::LogicState *states_ = nullptr;
        hldata::LadderVolume *ladders_ = nullptr;
        std::uint8_t *cornerState_ = nullptr;
        hldata::ActorState *actorStates_ = nullptr;
        hldata::SolidBox *actorBoxes_ = nullptr;
        /// Put in hand the model of the weapon the player has out.
        void takeInHand();
        hldata::WeaponState weapon_;
        int inHand_ = -1; ///< the weapon `held` is the model of
        bool radio_ = false; ///< ... and it is the satchels' radio
        std::int16_t thrown_[6] = {-1, -1, -1, -1, -1, -1}; ///< thrownModel
        std::int16_t shell_[2] = {-1, -1};                    ///< shellModel
        std::int16_t grenade_[2] = {-1, -1};                  ///< thrownModel: w_grenade, grenade
        /// Whether any satchel charge is out (and so the radio in hand).
        bool satchelsOut() const
        {
            for (const hldata::Projectile &shot : actors.shots)
            {
                if (shot.active && shot.kind == hldata::kShotSatchel)
                    return true;
            }
            return false;
        }
        int lastHealth_ = 100;
        /// How strong each of the map's changing lights was when the
        /// vertices they reach were last worked out (HlLightStyle.hpp).
        std::uint16_t styleValues_[hldata::kLightStyles];
        /// Until what the map writes as it is come into is written (ms);
        /// negative: it has been, or is not to be.
        int startMessagesMs_ = -1;
        std::uint32_t stepCount_ = 0;
        std::uint8_t *linkShut_ = nullptr; ///< ActorWorld::linkShut
        /// What the monsters think and hear (HlAi.hpp; ActorWorld::ai): a
        /// monster's worth for each actor that thinks, and a place for each
        /// actor.
        hldata::AiWorld ai_;
        hldata::AiMonster *aiMonsters_ = nullptr;
        std::int16_t *aiSlots_ = nullptr;
        std::uint8_t *aiHeld_ = nullptr;
        bool wasDeep_ = false; ///< the player was in water to the waist a frame ago
        int trainForward_ = 0; ///< forward or back as it was held a frame ago: a press drives a train
        /// The view from a trigger_camera (LogicWorld::camera), put over the
        /// player's own after it is worked out -- whose yaw and pitch (the
        /// way the player looks, which is the camera's) are kept here
        /// meanwhile, and given back as it ends.
        void cameraView();
        mutable std::int16_t leftWith_[hldata::kMaxVisitors] = {-1, -1, -1, -1};
        std::uint8_t placed_ = 0;
        int cameraWas_ = -1;
        std::uint16_t ownYaw_ = 0;
        std::int16_t ownPitch_ = 0;
        /// What laying a state back over the map (a save's, a map come back
        /// to) does besides: what it was doing at that moment is cleared,
        /// what moves sounds again, the limits are this console's.
        void afterLay();
        /// The player left the last map at its train's controls (makeArrival),
        /// and has this one's (applyArrival).
        mutable bool trainCarried_ = false;
        std::vector<hldata::SaveMapState> visited_;
        std::uint64_t playMs_ = 0;
        int slowCarry_ = 0; ///< host_timescale: the part of a sixtieth not yet run (Q8)
        char chapter_[44] = {};
        /// How the player came into this map (who came with them is in
        /// it): kept for a save of the map.
        hldata::Arrival came_;
    };

    HL_ENGINE_OBJECT(Game, game);
} // namespace hlrt
