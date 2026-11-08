#pragma once

// What makes a map do things: doors, buttons, triggers and the wiring between
// them.
//
// In the PC game every one of these is an object with virtual Use, Touch and
// Think, driven by an engine that calls them. Here they are rows of a table
// (LogicRecord, HlLevel.hpp) that the converter filled in from the map's
// entity lump, a second table of live state, and one function per event --
// the design of the PS1 port this project follows. It is far less general and
// that is the point: the whole of it fits in the instruction cache's worth of
// code and a few kilobytes of state.
//
// The BEHAVIOUR, though, is the PC game's, rule for rule, because maps are
// built against the details: a door fires its target at BOTH ends of its
// travel; a door that has a name cannot be opened by walking into it; a button
// resets the multisource it feeds when it pops back out. Each rule below is
// from the SDK (dlls/doors.cpp, buttons.cpp, triggers.cpp, subs.cpp).
//
// HOW THINGS TALK. Every entity may have a NAME and a TARGET. Firing a target
// means "use" every entity with that name. Using does a different thing per
// kind (logicUse). A fire can be delayed -- that is the event queue -- and can
// remove entities by name first (killtarget).
//
// TIME is milliseconds, advanced once per displayed frame by however long the
// frame took.
//
// std-only and inline, like the collision it sits on: the host tests run it.

#include "HlAchEvents.hpp"
#include "HlActors.hpp"
#include "HlLevelReader.hpp"
#include "HlPlayerMove.hpp"
#include "HlSound.hpp"
#include "HlSkill.hpp"
#include "HlSprite.hpp"
#include "HlWalk.hpp"

namespace hldata
{
    /// A brush entity as it is right now. The renderer draws from this and the
    /// collision is kept in step with it (logicPlaceBrush).
    struct BrushState
    {
        fx pos[3];
        bool active; ///< present: drawn, and solid if its record says so
        /// How it is turned about `pos`: pitch, yaw, roll, in turns (65536
        /// to the circle). Only doors on hinges and fans are ever not 0.
        std::uint16_t angles[3];
        /// How solid it is drawn, if something (an env_render) has said
        /// otherwise than its record: 1..31, or kBrushUnseen; 0: as made.
        std::uint8_t look;
        /// Which face its '+' textures show: 0 their own, 1 the other (the
        /// game's "frame": a button pressed in, a light fixture switched
        /// off, a charger with nothing left). Worked out each tick from what
        /// the thing is doing.
        std::uint8_t frame;
        /// How far a conveyor's belt has slid: texels (Q12), round and
        /// round every 1024.
        fx scroll;
    };
    inline constexpr std::uint8_t kBrushUnseen = 32;

    enum class MoveState : std::uint8_t
    {
        AtBottom, ///< where it starts
        GoingUp,
        AtTop,
        GoingDown,
    };

    /// The live half of a LogicRecord.
    struct LogicState
    {
        MoveState move = MoveState::AtBottom;
        bool dead = false;    ///< removed, or used up
        bool running = false; ///< a multi_manager with targets still to fire
        std::int32_t timer = -1; ///< ms until its pending "think"; -1 = none
        std::int32_t phase = 0;  ///< Q30: how far along its travel
        /// A multisource's inputs that are on. For a train: the path corner it
        /// last arrived at. For a rotating door: the angle it rests at.
        std::uint32_t bits = 0;
        fx speed = 0;            ///< a train's current speed: corners change it
        std::int16_t master = -1; ///< its multisource, as a record index; -1 = none
        fx base[3] = {0, 0, 0};   ///< a mover's position at phase 0
        fx travel[3] = {0, 0, 0}; ///< ... and its whole travel from there
        /// What it fires, if a trigger_changetarget has given it something
        /// else to (a name id); 0: the map's own (LogicRecord::target).
        std::uint16_t target = 0;
        /// The damage a func_guntarget has taken this ride. For a
        /// scripted_sequence that takes its actor at the start: non-zero once
        /// it has (the game's CineThink takes it once, never again).
        std::int32_t hurt = 0;
    };

    struct LogicEvent
    {
        bool active = false;
        std::uint8_t useType = kUseToggle;
        std::uint16_t target = 0;
        std::uint16_t killTarget = 0;
        std::int16_t caller = -1;
        std::int32_t ms = 0;
    };

    // The states that outlive a map (env_global): a handful of named switches
    // one map sets and a later one reads. They are the caller's, not the
    // LogicWorld's, because the LogicWorld is torn down with its map.
    inline constexpr std::uint8_t kGlobalOff = 0;
    inline constexpr std::uint8_t kGlobalOn = 1;
    inline constexpr std::uint8_t kGlobalDead = 2;
    inline constexpr int kMaxGlobals = 32;
    /// Not a map's switch: how hard the world pulls the player, in percent
    /// (PlayerState::gravityPercent), kept among the switches so that it goes
    /// into the next map and into a save as they do.
    inline constexpr std::uint32_t kGlobalPlayerGravity = nameHash("!player_gravity");
    /// ... and how hard the game is (kSkillEasy..kSkillHard: HlSkill.hpp),
    /// set as a game is begun; a game that has none is MEDIUM.
    inline constexpr std::uint32_t kGlobalSkill = nameHash("!skill");

    struct Globals
    {
        std::uint32_t hash[kMaxGlobals] = {};
        std::uint8_t state[kMaxGlobals] = {};
        int count = 0;

        /// The state called `name`, or -1 if nothing has set it yet.
        int get(std::uint32_t name) const
        {
            for (int i = 0; i < count; ++i)
            {
                if (hash[i] == name)
                    return state[i];
            }
            return -1;
        }

        void set(std::uint32_t name, std::uint8_t value)
        {
            for (int i = 0; i < count; ++i)
            {
                if (hash[i] == name)
                {
                    state[i] = value;
                    return;
                }
            }
            if (count < kMaxGlobals)
            {
                hash[count] = name;
                state[count++] = value;
            }
        }
    };

    inline constexpr int kMaxLogicEvents = 128;
    /// A chain of entities firing each other stops here. Maps do not go this
    /// deep; a loop in a map's wiring would otherwise never end.
    inline constexpr int kMaxFireDepth = 8;
    /// How close the player's box has to come to a door for that to count as
    /// touching it.
    inline constexpr fx kTouchSlack = fxFromInt(1);
    /// USE reaches this far, and only works on what is roughly ahead.
    inline constexpr fx kUseRange = fxFromInt(64);
    inline constexpr fx kUseMinFacing = 2048; // cos 60 in Q12

    /// How many actors a map has room for beside its own: the visitors from
    /// the map before; or, on a multiplayer map (one with a place a match's
    /// player comes in at), the players of a match (HlActors.hpp).
    inline int spareActorsOf(const LevelView &view)
    {
        for (std::uint32_t e = 0; e < view.entityCount; ++e)
        {
            if (view.entities[e].classId == static_cast<std::uint16_t>(EntityClass::InfoPlayerDeathmatch))
                return kMaxMatchPlayers;
        }
        return kMaxVisitors;
    }

    /// Everything the logic works on. The pointers are the caller's arrays:
    /// level data is read, live state is written.
    struct LogicWorld
    {
        const LogicRecord *records = nullptr;
        int recordCount = 0;
        const LogicAux *aux = nullptr;
        const PathCorner *corners = nullptr;
        const Brush *brushes = nullptr;
        int brushCount = 0;
        /// The level's models, for the shapes of trigger volumes that are
        /// not boxes (LogicRecord::pad); may be null.
        Unsaved<const Model *> models;
        Unsaved<int> modelCount;

        LogicState *states = nullptr;      ///< one per record
        BrushState *brushStates = nullptr; ///< one per brush
        MoverHull *movers = nullptr;       ///< one per brush: the collision's copy
        MoveWorld *world = nullptr;        ///< the world those movers belong to

        /// The ladder volumes, one per record of kind Ladder in record order:
        /// the collision's copy, which the logic switches off if a ladder is
        /// removed.
        LadderVolume *ladders = nullptr;

        Globals *globals = nullptr; ///< may be null: then every state is unknown
        /// The map's actors; may be null (then scripts find nobody).
        ActorWorld *actors = nullptr;
        /// By name id, the hash of the name: how a clip's event, which knows
        /// a target only by hash, finds it. May be null.
        const std::uint32_t *nameHashes = nullptr;
        int nameCount = 0;

        /// What each record sounds like (the level's LogicSounds), and where
        /// what is heard is put for the caller to play. Either may be null:
        /// then nothing is heard.
        const LogicSound *sounds = nullptr;
        SoundQueue *heard = nullptr;
        /// The level itself, for what has to be looked up in it (what a
        /// wall is made of); may be null.
        const LevelView *level = nullptr;
        /// The sounds the map's models make in their clips, by the hash of
        /// their names (the level's ModelSounds, sorted); may be null.
        const ModelSound *modelSounds = nullptr;
        int modelSoundCount = 0;
        /// Until a locked door may say so again (detail::lockedSound).
        std::int32_t lockSaidMs = 0;
        /// The doors and buttons that have refused the player and not yet
        /// worked for them (record index + 1; 0: none). The last few.
        static constexpr int kRefused = 8;
        std::uint16_t refused[kRefused] = {};
        int refusedNext = 0;
        /// How many of the map's own marks are kept on the walls at once.
        int marksLimit = 16;
        /// Until the map's own marks are next looked at (detail::marksNear).
        std::int32_t marksDueMs = 0;
        /// ... and the ways between the nodes (detail::linksTick).
        std::int32_t linksDueMs = 0;
        /// What soldiers call out (Section::Speech), if the map has it; and
        /// after it, what the people say (kTalkTableAt: AiWorld::speechCount
        /// says if it is that long).
        const std::uint16_t *speech = nullptr;
        /// Set when the map starts a track of music: its number. The caller
        /// plays it, and sets this back to -1.
        int musicRequest = -1;
        /// What the map has asked to be written on the screen since the
        /// caller last looked (indices into the level's Messages): the
        /// caller shows them and sets the count back to 0.
        std::uint16_t shown[4] = {};
        int shownCount = 0;
        /// What the achievements are told of (HlAchEvents.hpp): the
        /// caller's, and emptied by it. Null: nothing is told.
        AchEvents *achievements = nullptr;
        /// What has broken since the caller last looked (record indices):
        /// the caller throws its pieces and sets the count back to 0. And
        /// the way the blow that last struck something was going (Q12),
        /// for which way they are thrown.
        std::uint16_t broke[4] = {};
        int brokeCount = 0;
        /// Which switched lights are off (a bit per style under
        /// kLightStyles): worked out each tick from the Light records.
        std::uint64_t stylesOff = 0;
        /// The level's strings (its Strings section), for what is read from
        /// them as the map goes: a light's pattern.
        Unsaved<const char *> strings;
        /// How each switched light (style 32 on) shines while it is on: its
        /// pattern (lights.cpp's m_iszPattern), or null for steady. Worked
        /// out each tick with stylesOff.
        Unsaved<std::array<const char *, kLightStyles - 32>> stylePattern;
        fx struckFrom[3] = {0, 0, 0};

        /// One byte per path corner, the caller's: what has changed about a
        /// track node since the map started (kNodeOff, kNodeSpent). May be
        /// null on a map with no track.
        std::uint8_t *cornerState = nullptr;
        int cornerCount = 0;
        /// Set when something has put the player somewhere else facing a
        /// given way (a teleport): the caller sets the view to `faceYaw` and
        /// clears it.
        bool faceSet = false;
        std::uint16_t faceYaw = 0;
        /// How far the player's view has been turned by what they are riding
        /// this tick, in turns: the caller adds it to the view and clears it.
        std::int32_t viewTurn = 0;

        LogicEvent events[kMaxLogicEvents];
        int eventsDropped = 0; ///< fires lost because the queue was full

        // What is seen and felt (HlEffects.hpp). The view's shake: how long
        // it has left, of how long, and how far it throws the eye at its
        // worst. The screen's fade: how far it is over the picture now, of
        // 255 (the game's alpha), and in what colour (RGB15; black unless
        // the env_fade said otherwise): the caller shows it.
        std::int32_t shakeMs = 0, shakeWholeMs = 0;
        fx shakeAmplitude = 0;
        bool fading = false, fadeFrom = false;
        std::int32_t fadeMs = 0, fadeWholeMs = 0, fadeHoldMs = 0;
        std::uint8_t fadeDark = 0, fadeLevel = 0;
        Unsaved<std::uint16_t> fadeColour;
        std::uint32_t effectSeed = 4711;
        /// The level's Sprites, for how many frames each has; may be null.
        const SpriteRef *sprites = nullptr;
        int spriteCount = 0;

        /// Set when the map asks for the game to be saved: the caller does
        /// it, and clears this.
        bool saveRequested = false;
        /// A game between consoles (HlNetGame.hpp): what the player's shots
        /// and blows have struck of the map's breakable things this tick --
        /// the record and how hard, in pairs -- for the other consoles to
        /// be told. Only kept while `sharing`; whoever reads it empties it.
        bool sharing = false;
        /// The player holds USE: a box they walk into is not pushed (it is
        /// theirs to pull, logicPull). Whoever reads the buttons sets it.
        Unsaved<bool> useHeld;
        /// The guns' aim is NOT to bend towards a monster near it (it may on
        /// EASY and MEDIUM: the game's sv_aim, HlAutoaim.hpp). The caller's
        /// setting, given each frame: not the game's state.
        Unsaved<bool> noAutoaim;
        /// A match between consoles' rules, as its host set them
        /// (HlNetGame.hpp's netStart; not the game's state): a weapon lying
        /// about stays where it is when taken, and is not taken by whoever
        /// has it already (mp_weaponstay); a fall costs a flat 10, not by
        /// how hard it was (mp_falldamage 0); the lamp is not to be lit
        /// (mp_flashlight 0); the guns' autoaim is the host's to say, not
        /// the player's (0 off, 1 on; -1: the player's).
        Unsaved<bool> weaponsStay;
        Unsaved<bool> flatFalls;
        Unsaved<bool> lampBanned;
        Unsaved<std::int8_t> matchAutoaim{-1};
        std::int16_t struck[16] = {};
        int struckCount = 0;
        /// The boxes that can be pushed, as record indices: looked up once
        /// (-1: not yet), for whatever moves and has to ask what is in its
        /// way (detail::moveBrush).
        std::int16_t pushables[48] = {};
        int pushableCount = -1;

        /// The ChangeLevel record that has been set off, or -1. The logic only
        /// says so: changing map is the caller's to do, between frames.
        int pendingChange = -1;
        /// The game is lost (LogicKind::LoadSaved): ms until the last save is
        /// to be loaded, -1 none -- and, once it has counted down to 0, the
        /// caller does it and sets it back to -1.
        std::int32_t reloadMs = -1;
        /// The end of a part of the game (LogicKind::EndSection): the caller
        /// goes back to the title, and clears this.
        bool endSection = false;
        /// The Camera record the view is from, -1 none; and how long it has
        /// still to be (ms). The caller sets the view from it after the
        /// player's own (cameraView).
        std::int16_t camera = -1;
        std::int32_t cameraLeftMs = 0;
    };

    /// How hard the game is (kSkillEasy..kSkillHard): as its switches say,
    /// MEDIUM if they say nothing (a game from before there was a choice, a
    /// test with no switches).
    inline int skillLevelOf(const LogicWorld &w)
    {
        const int level = w.globals != nullptr ? w.globals->get(kGlobalSkill) : -1;
        return level >= kSkillEasy && level <= kSkillHard ? level : kSkillMedium;
    }
    /// ... and its numbers (skill.cfg's, for that level).
    inline const SkillData &skillOf(const LogicWorld &w)
    {
        return skillData(skillLevelOf(w));
    }

    /// How much a wall charger holds, at the game's level (its Spawn's
    /// gSkillData.healthchargerCapacity, suitchargerCapacity).
    inline int chargerCapacity(const LogicWorld &w, const LogicRecord &record)
    {
        return record.kind == static_cast<std::uint8_t>(LogicKind::SuitCharger) ? skillOf(w).suitcharger
                                                                                : skillOf(w).healthcharger;
    }

    /// What is used again each frame while USE is held on it
    /// (FCAP_CONTINUOUS_USE): the wall chargers. (A box too, but that is
    /// logicPull's.)
    inline bool logicContinuousUse(const LogicRecord &record)
    {
        return record.kind == static_cast<std::uint8_t>(LogicKind::HealthCharger) ||
               record.kind == static_cast<std::uint8_t>(LogicKind::SuitCharger);
    }

    inline constexpr std::uint8_t kNodeOff = 1;   ///< the track ends before this node
    inline constexpr std::uint8_t kNodeSpent = 2; ///< its fire-once target has fired

    /// Whether the global state a record depends on lets it work: it names
    /// none, or the one it names is on.
    inline bool logicGlobalAllows(const LogicWorld &w, const LogicRecord &record)
    {
        return record.globalState == 0 ||
               (w.globals != nullptr && w.globals->get(record.globalState) == kGlobalOn);
    }

    // ------------------------------------------------------------------ brushes

    /// Put a brush somewhere, turned by `angles` (pitch, yaw, roll; null for
    /// not turned), for the renderer and the collision alike.
    inline void logicPlaceBrushTurned(LogicWorld &w, int index, const fx *pos,
                                      const std::uint16_t *angles)
    {
        const Brush &brush = w.brushes[index];
        BrushState &state = w.brushStates[index];
        MoverHull &mover = w.movers[index];
        const std::uint16_t turn[3] = {angles != nullptr ? angles[0] : std::uint16_t(0),
                                       angles != nullptr ? angles[1] : std::uint16_t(0),
                                       angles != nullptr ? angles[2] : std::uint16_t(0)};
        for (int a = 0; a < 3; ++a)
            state.angles[a] = turn[a];
        // Exactly the identity when it is not turned, not to a third of a
        // unit in the last place: this is what tells the trace it need not
        // turn anything.
        mover.turned = turn[0] != 0 || turn[1] != 0 || turn[2] != 0;
        if (mover.turned)
        {
            anglesToAxes(turn, mover.axes);
        }
        else
        {
            for (int i = 0; i < 9; ++i)
                mover.axes[i] = (i % 4 == 0) ? (1 << kNormalShift) : 0;
        }

        // A brush that turns sweeps a circle round its pivot (a ball, if it
        // tumbles): the box that holds it at any angle is the pivot plus or
        // minus the furthest its corners get from it.
        const bool pivots = (brush.flags & kBrushHasPivot) != 0;
        const bool tumbles = pivots && (brush.flags & kBrushTumbles) != 0;
        fx reach = 0;
        if (pivots)
        {
            for (int corner = 0; corner < 8; ++corner)
            {
                const fx far[3] = {
                    fxFromInt((corner & 1) ? brush.maxs[0] : brush.mins[0]) - brush.origin[0],
                    fxFromInt((corner & 2) ? brush.maxs[1] : brush.mins[1]) - brush.origin[1],
                    tumbles ? fxFromInt((corner & 4) ? brush.maxs[2] : brush.mins[2]) -
                                  brush.origin[2]
                            : 0};
                const fx length = fxLength(far);
                if (length > reach)
                    reach = length;
            }
        }
        for (int a = 0; a < 3; ++a)
        {
            state.pos[a] = pos[a];
            mover.origin[a] = pos[a];
            // The record's bounds are where the brush was built; it has moved
            // by however far it now is from its origin.
            const fx shift = pos[a] - brush.origin[a];
            mover.mins[a] = fxFromInt(brush.mins[a]) + shift;
            mover.maxs[a] = fxFromInt(brush.maxs[a]) + shift;
            if (pivots && (a < 2 || tumbles))
            {
                mover.mins[a] = pos[a] - reach;
                mover.maxs[a] = pos[a] + reach;
            }
        }
        mover.solid = (brush.flags & kBrushSolid) != 0 && state.active;
        mover.present = state.active;
        mover.contents = static_cast<std::int8_t>(brush.kind);
        if (w.world != nullptr)
            moverReachSet(*w.world, index);
    }

    /// The same for the ordinary case: turned about the vertical only.
    inline void logicPlaceBrush(LogicWorld &w, int index, const fx *pos, std::uint16_t yaw = 0)
    {
        const std::uint16_t angles[3] = {0, yaw, 0};
        logicPlaceBrushTurned(w, index, pos, angles);
    }

    /// Move a brush without turning it from how it is turned now.
    inline void logicMoveBrushTo(LogicWorld &w, int index, const fx *pos)
    {
        const std::uint16_t angles[3] = {w.brushStates[index].angles[0],
                                         w.brushStates[index].angles[1],
                                         w.brushStates[index].angles[2]};
        logicPlaceBrushTurned(w, index, pos, angles);
    }

    inline void logicSetBrushActive(LogicWorld &w, int index, bool active)
    {
        w.brushStates[index].active = active;
        logicMoveBrushTo(w, index, w.brushStates[index].pos);
    }

    namespace detail
    {
        inline bool playerInsideBrush(const LogicWorld &w, const PlayerState &player, int index)
        {
            const MoverHull &mover = w.movers[index];
            if (!mover.solid || !nearMover(mover, player.ducked, player.origin, player.origin))
                return false;
            fx local[3];
            toMoverSpace(mover, player.origin, local);
            return !hullPositionFree(player.ducked ? mover.crouched : mover.standing, local);
        }

        /// Shove the player by `delta`, stopped by the world and by every
        /// brush except the one doing the shoving.
        inline void pushPlayer(LogicWorld &w, PlayerState &player, int pusher, const fx *delta)
        {
            const bool wasSolid = w.movers[pusher].solid;
            w.movers[pusher].solid = false;
            moverReachSet(*w.world, pusher);
            const fx to[3] = {player.origin[0] + delta[0], player.origin[1] + delta[1],
                              player.origin[2] + delta[2]};
            const Trace tr = traceAll(*w.world, player.ducked, player.origin, to);
            for (int a = 0; a < 3; ++a)
                player.origin[a] = tr.endPos[a];
            w.movers[pusher].solid = wasSolid;
            moverReachSet(*w.world, pusher);
        }

        inline bool boxShove(LogicWorld &w, PlayerState &player, int record, const fx *delta);
        /// The boxes there are to push, found once.
        inline void pushableList(LogicWorld &w)
        {
            if (w.pushableCount >= 0)
                return;
            w.pushableCount = 0;
            for (int i = 0; i < w.recordCount && w.pushableCount < 48; ++i)
            {
                if (w.records[i].kind == static_cast<std::uint8_t>(LogicKind::Pushable) && w.records[i].brush != kNoBrush)
                    w.pushables[w.pushableCount++] = static_cast<std::int16_t>(i);
            }
        }
        inline bool boxInMover(const LogicWorld &w, int record, int mover);
        inline bool boxStandsOn(LogicWorld &w, int record, int mover);
        inline void crushPlayer(LogicWorld &w, PlayerState &player, int index, int ms);

        /// Move a brush to `pos`, carrying the player if they stand on it and
        /// pushing them if it runs into them -- and likewise the boxes that
        /// can be pushed: one standing on it goes where it goes, one in its
        /// way is pushed along. False -- with the brush put back, and the
        /// player where they were -- if the player or a box ends up with
        /// nowhere to go: it is BLOCKED (`byPlayer`, if given, says whether by
        /// the player).
        inline bool moveBrush(LogicWorld &w, PlayerState &player, int index, const fx *pos,
                              bool *byPlayer = nullptr)
        {
            fx old[3], delta[3];
            for (int a = 0; a < 3; ++a)
            {
                old[a] = w.brushStates[index].pos[a];
                delta[a] = pos[a] - old[a];
            }
            const fx stood[3] = {player.origin[0], player.origin[1], player.origin[2]};
            const bool rider = player.onGround && player.groundMover == index;
            // (Which boxes stand on it, before it moves from under them.)
            pushableList(w);
            std::uint64_t riding = 0;
            const MoverHull &before = w.movers[index];
            for (int n = 0; n < w.pushableCount && before.solid; ++n)
            {
                const int box = w.records[w.pushables[n]].brush;
                const MoverHull &other = w.movers[box];
                if (box == index || !other.solid || !w.brushStates[box].active)
                    continue;
                const fx gap = other.mins[2] - before.maxs[2];
                if (gap < -fxFromInt(1) || gap > fxFromInt(2))
                    continue;
                const bool over = other.maxs[0] > before.mins[0] + fxFromInt(1) &&
                                  other.mins[0] < before.maxs[0] - fxFromInt(1) &&
                                  other.maxs[1] > before.mins[1] + fxFromInt(1) &&
                                  other.mins[1] < before.maxs[1] - fxFromInt(1);
                // (One that only meets it at an edge stands on it if that is
                // the floor its own shape finds, as the game's ground entity:
                // hldemo3's crate on the rails that carry it.)
                const bool edge = !over && other.maxs[0] >= before.mins[0] && other.mins[0] <= before.maxs[0] &&
                                  other.maxs[1] >= before.mins[1] && other.mins[1] <= before.maxs[1];
                if (over || (edge && boxStandsOn(w, w.pushables[n], index)))
                    riding |= std::uint64_t(1) << n;
            }
            logicMoveBrushTo(w, index, pos);
            if (!w.movers[index].solid)
                return true;
            auto refuse = [&]()
            {
                logicMoveBrushTo(w, index, old);
                for (int a = 0; a < 3; ++a)
                    player.origin[a] = stood[a];
                return false;
            };
            if (rider)
                pushPlayer(w, player, index, delta);
            if (playerInsideBrush(w, player, index))
            {
                pushPlayer(w, player, index, delta);
                if (playerInsideBrush(w, player, index))
                {
                    if (byPlayer != nullptr)
                        *byPlayer = true;
                    return refuse();
                }
            }
            // The boxes: carried, pushed, or in the way. (Not when this is
            // itself a box being shoved by another: one deep is enough.)
            static int depth = 0;
            if (depth >= 2)
                return true;
            ++depth;
            bool blocked = false;
            const MoverHull &mover = w.movers[index];
            for (int n = 0; n < w.pushableCount && !blocked; ++n)
            {
                const int box = w.records[w.pushables[n]].brush;
                const MoverHull &other = w.movers[box];
                if (box == index || !other.solid || !w.brushStates[box].active)
                    continue;
                // (In its way: within its outer box, and then truly within
                // IT -- a fence round a box, a lift with rails, has the box
                // inside its outer box and nowhere near its bars.)
                auto inside = [&]()
                {
                    const fx slack = fxFromInt(1);
                    return other.maxs[0] > mover.mins[0] + slack && other.mins[0] < mover.maxs[0] - slack &&
                           other.maxs[1] > mover.mins[1] + slack && other.mins[1] < mover.maxs[1] - slack &&
                           other.maxs[2] > mover.mins[2] + slack && other.mins[2] < mover.maxs[2] - slack &&
                           boxInMover(w, w.pushables[n], index);
                };
                if (riding & (std::uint64_t(1) << n))
                {
                    // (Up and down it follows its floor by itself.)
                    const fx along[3] = {delta[0], delta[1], 0};
                    if (along[0] != 0 || along[1] != 0)
                        boxShove(w, player, w.pushables[n], along);
                }
                else if (inside())
                {
                    boxShove(w, player, w.pushables[n], delta);
                    blocked = inside();
                }
            }
            --depth;
            return blocked ? refuse() : true;
        }

        inline bool playerTouchesBox(const PlayerState &player, const fx *mins, const fx *maxs,
                                     fx slack)
        {
            for (int a = 0; a < 3; ++a)
            {
                const fx half = (a == 2 ? kPlayerHalfHeight[player.ducked ? 1 : 0]
                                        : kPlayerHalfWidth) +
                                slack;
                if (player.origin[a] + half < mins[a] || player.origin[a] - half > maxs[a])
                    return false;
            }
            return true;
        }

        /// Whether the player is in a trigger volume: in its box, and -- for
        /// one that is not a box -- with their origin inside its brush's
        /// hull for their size, as the game's touch test has it
        /// (SV_TouchLinks).
        inline bool playerInVolume(const LogicWorld &w, const PlayerState &player, const LogicRecord &record)
        {
            const fx mins[3] = {fxFromInt(record.mins[0]), fxFromInt(record.mins[1]), fxFromInt(record.mins[2])};
            const fx maxs[3] = {fxFromInt(record.maxs[0]), fxFromInt(record.maxs[1]), fxFromInt(record.maxs[2])};
            if (!playerTouchesBox(player, mins, maxs, 0))
                return false;
            if (record.pad == 0 || w.models.value == nullptr || record.pad >= w.modelCount.value || w.world == nullptr)
                return true;
            const Model &model = w.models.value[record.pad];
            const Hull &like = player.ducked ? w.world->crouched : w.world->standing;
            const Hull shape{like.nodes, like.planes, like.planeTypes,
                             player.ducked ? model.clipCrouched : model.clipStanding};
            return hullPointContents(shape, shape.root, player.origin) == kContentsSolid;
        }

        /// Whether the player is in one of the volumes of the landmark
        /// `landmark` (its nameHash) -- or the map has none of them, and so
        /// the player is everywhere they could need to be.
        inline bool inTransition(const LogicWorld &w, const PlayerState &player, std::uint32_t landmark)
        {
            bool any = false;
            for (int i = 0; i < w.recordCount; ++i)
            {
                const LogicRecord &record = w.records[i];
                if (record.kind != static_cast<std::uint8_t>(LogicKind::Transition) || record.extra[0] != landmark)
                    continue;
                const fx mins[3] = {fxFromInt(record.mins[0]), fxFromInt(record.mins[1]),
                                    fxFromInt(record.mins[2])};
                const fx maxs[3] = {fxFromInt(record.maxs[0]), fxFromInt(record.maxs[1]),
                                    fxFromInt(record.maxs[2])};
                if (playerTouchesBox(player, mins, maxs, 0))
                    return true;
                any = true;
            }
            return !any;
        }
    } // namespace detail

    // ------------------------------------------------------------------ firing

    inline void logicUse(LogicWorld &w, PlayerState &player, int index, int caller, int useType,
                         int depth = 0);
    namespace detail
    {
        inline void chargerUse(LogicWorld &w, PlayerState &player, int index); // (HlMonsters.hpp)
    }

    /// Use every live entity called `name`.
    /// (HlEffects.hpp: the env_ entities that are only seen and felt.)
    inline void effectUse(LogicWorld &w, PlayerState &player, int index, int useType);

    inline void logicFire(LogicWorld &w, PlayerState &player, std::uint16_t name, int caller,
                          int useType, int depth)
    {
        if (name == 0 || depth > kMaxFireDepth)
            return;
        bool answered = false;
        for (int i = 0; i < w.recordCount; ++i)
        {
            if (w.records[i].name == name && !w.states[i].dead)
            {
                answered = true;
                logicUse(w, player, i, caller, useType, depth + 1);
            }
        }
        // A turret or a monster by that name is switched: it finds out when
        // it next thinks (HlMonsters.hpp, HlAi.hpp).
        for (int i = 0; w.actors != nullptr && i < w.actors->count; ++i)
        {
            ActorState &actor = w.actors->states[i];
            if (actor.active && actor.name == name && (isTurret(actor.kind) || actorThinks(actor.kind)))
            {
                answered = true;
                actor.switched = static_cast<std::uint8_t>(1 + useType);
            }
        }
        // (Only what something still there answered to: a name whose
        // entities are gone is fired at nothing -- c5a1's loser_mm is,
        // once the offer is taken.)
        if (answered && w.achievements != nullptr && w.nameHashes != nullptr && name < w.nameCount &&
            w.achievements->watches(w.nameHashes[name]))
            w.achievements->push(AchEventType::Fired, w.nameHashes[name]);
    }

    inline void aiSquadLeave(ActorWorld &a, int index); // (HlAi.hpp)

    /// Remove every entity called `name`: it stops working, and if it is a
    /// brush it is gone from the map.
    inline void logicKill(LogicWorld &w, std::uint16_t name)
    {
        if (name == 0)
            return;
        // (Someone, as well as something: the people and monsters a map
        // has done with are taken away like this.)
        for (int i = 0; w.actors != nullptr && i < w.actors->count; ++i)
        {
            ActorState &actor = w.actors->states[i];
            if (!actor.active || actor.name != name)
                continue;
            if (actor.script >= 0 && actor.script < w.recordCount)
                w.states[actor.script].dead = true;
            aiSquadLeave(*w.actors, i);
            actor.active = false;
            actorPlaceBox(*w.actors, i);
        }
        for (int i = 0; i < w.recordCount; ++i)
        {
            if (w.records[i].name != name)
                continue;
            w.states[i].dead = true;
            w.states[i].timer = -1;
            // A multi_manager taken away fires nothing more: what it has
            // still to fire goes with it (its think is gone -- but what is
            // due now goes off, as the think that removed it fires it).
            if (w.records[i].kind == static_cast<std::uint8_t>(LogicKind::MultiManager))
            {
                for (LogicEvent &event : w.events)
                {
                    if (event.active && event.caller == i && event.ms > 0)
                        event.active = false;
                }
            }
            if (w.records[i].brush != kNoBrush)
                logicSetBrushActive(w, w.records[i].brush, false);
            if (w.records[i].kind == static_cast<std::uint8_t>(LogicKind::Ladder) &&
                w.ladders != nullptr)
            {
                // Its place in the ladder list is how many ladders come
                // before it.
                int slot = 0;
                for (int k = 0; k < i; ++k)
                    slot += w.records[k].kind == static_cast<std::uint8_t>(LogicKind::Ladder);
                w.ladders[slot].active = false;
            }
        }
    }

    inline void logicEnqueue(LogicWorld &w, std::uint16_t target, std::uint16_t killTarget,
                             int useType, int caller, std::int32_t ms)
    {
        for (LogicEvent &event : w.events)
        {
            if (event.active)
                continue;
            event.active = true;
            event.target = target;
            event.killTarget = killTarget;
            event.useType = static_cast<std::uint8_t>(useType);
            event.caller = static_cast<std::int16_t>(caller);
            event.ms = ms;
            return;
        }
        ++w.eventsDropped;
    }

    /// What an entity does when it "fires": remove its killtarget and use its
    /// target -- after its delay, if it has one.
    /// What record `index` fires: what a trigger_changetarget gave it, or
    /// the map's own.
    inline std::uint16_t logicTargetOf(const LogicWorld &w, int index)
    {
        const std::uint16_t changed = w.states[index].target;
        return changed != 0 ? changed : w.records[index].target;
    }

    /// How much of what lies about and goes on at once this console has room
    /// for -- a DSi twice as much, and keeps it longer (`dsi`) -- set as a map
    /// is made, and again over a state laid back (a save made on the other
    /// kind of console brings its own); what lies beyond is let go.
    inline void consoleLimits(ActorWorld &actors, LogicWorld &w, bool dsi)
    {
        actors.gibLimit = dsi ? kMaxGibs : kMaxGibs / 2;
        actors.shotLimit = dsi ? kMaxProjectiles : kMaxProjectiles / 2;
        actors.effectLimit = dsi ? kMaxEffects : kMaxEffects / 2;
        actors.decalLimit = dsi ? kMaxDecals : 32;
        actors.pieceLimit = dsi ? kMaxPieces : 24;
        // (... and keeps them longer: the small things half as long again,
        // the marks twice.)
        actors.lifePercent = dsi ? 150 : 100;
        actors.markLifePercent = dsi ? 200 : 100;
        w.marksLimit = dsi ? 32 : 16;
        for (int i = actors.gibLimit; i < kMaxGibs; ++i)
            actors.gibs[i].active = false;
        for (int i = actors.shotLimit; i < kMaxProjectiles; ++i)
            actors.shots[i].active = false;
        for (int i = actors.effectLimit; i < kMaxEffects; ++i)
            actors.effects[i].kind = 0;
        for (int i = actors.decalLimit; i < kMaxDecals; ++i)
            actors.decals[i].active = false;
        for (int i = actors.pieceLimit; i < kMaxPieces; ++i)
            actors.pieces[i].kind = 0;
    }

    inline void logicUseTargets(LogicWorld &w, PlayerState &player, int index, int useType,
                                int depth)
    {
        const LogicRecord &record = w.records[index];
        if (record.delayMs != 0)
        {
            logicEnqueue(w, logicTargetOf(w, index), record.killTarget, useType, index,
                         static_cast<std::int32_t>(record.delayMs));
            return;
        }
        logicKill(w, record.killTarget);
        logicFire(w, player, logicTargetOf(w, index), index, useType, depth);
    }

    inline bool logicMasterAllows(const LogicWorld &w, int index)
    {
        const int master = w.states[index].master;
        if (master < 0)
            return true;
        const std::uint32_t all = w.records[master].auxCount >= 32
                                      ? 0xFFFFFFFFu
                                      : ((1u << w.records[master].auxCount) - 1u);
        return w.states[master].bits == all && logicGlobalAllows(w, w.records[master]);
    }

    // ------------------------------------------------------------------ movers

    /// What takes its time over moving, and is heard for as long as it does:
    /// its move sound goes round (if it is one that does) until it stops.
    inline bool moverLoops(LogicKind kind)
    {
        return kind == LogicKind::Door || kind == LogicKind::DoorRotating || kind == LogicKind::Train ||
               kind == LogicKind::PlatRot;
    }

    /// Whether such a mover is moving, and so is still to be heard: what the
    /// sound looks at, in case its stop was never heard. A train going
    /// straight through a corner is at it for a tick (a wait of 0): it goes
    /// on, and nothing will start its sound again.
    inline bool moverSounding(const LogicRecord &record, const LogicState &state)
    {
        if (state.dead)
            return false;
        const bool moving = state.move == MoveState::GoingUp || state.move == MoveState::GoingDown;
        if (record.kind == static_cast<std::uint8_t>(LogicKind::Train))
            return state.running && (moving || state.timer == 0);
        return moving;
    }

    namespace detail
    {
        /// The sound of record `index` -- its moving or its stopping -- is
        /// heard from the middle of its brush (or from where it is, for
        /// something with none).
        inline void recordSound(LogicWorld &w, int index, bool stopping)
        {
            if (w.heard == nullptr || w.sounds == nullptr)
                return;
            const LogicRecord &record = w.records[index];
            // What takes its time over moving is heard for as long as it
            // does (if its sound is one that goes round), and no longer.
            const bool travels = moverLoops(static_cast<LogicKind>(record.kind));
            if (stopping && travels)
                w.heard->add(kNoSound, nullptr, 0, 0, kSoundLoopOff, 100, static_cast<std::uint16_t>(index));
            const std::uint16_t sound = stopping ? w.sounds[index].stop : w.sounds[index].move;
            if (sound == kNoSound)
                return;
            fx at[3] = {record.move[0], record.move[1], record.move[2]};
            if (record.brush != kNoBrush)
            {
                const MoverHull &hull = w.movers[record.brush];
                for (int a = 0; a < 3; ++a)
                    at[a] = (hull.mins[a] + hull.maxs[a]) / 2;
            }
            if (!stopping && travels)
            {
                w.heard->add(sound, at, moverVolume(record), kRadiusLarge, kSoundLoopOn, 100,
                             static_cast<std::uint16_t>(index));
                return;
            }
            w.heard->add(sound, at, moverVolume(record), kRadiusLarge);
        }

        /// A map's mark (LogicKind::Mark), on whatever is within a hand's
        /// breadth of where it was put. Put again where it already is, it
        /// simply stays (HlActors.hpp, decalAdd).
        inline void markPlace(LogicWorld &w, const LogicRecord &record)
        {
            if (w.actors == nullptr || w.world == nullptr)
                return;
            static constexpr std::int8_t kWays[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (const auto &way : kWays)
            {
                const fx from[3] = {record.move[0] - fxFromInt(way[0] * 6), record.move[1] - fxFromInt(way[1] * 6),
                                    record.move[2] - fxFromInt(way[2] * 6)};
                const fx to[3] = {record.move[0] + fxFromInt(way[0] * 6), record.move[1] + fxFromInt(way[1] * 6),
                                  record.move[2] + fxFromInt(way[2] * 6)};
                fx at[3], normal[3];
                if (!traceShotWall(*w.world, from, to, at, normal))
                    continue;
                const auto picture = static_cast<std::uint8_t>(record.extra[0]);
                if (picture == kDecalSign)
                {
                    // A sign: one of the map's sprites, as large as its
                    // picture (a texel a unit), its colour its own.
                    const std::uint32_t sprite = record.extra[2];
                    if (w.level == nullptr || sprite >= w.level->spriteCount)
                        return;
                    const SpriteRef &ref = w.level->sprites[sprite];
                    const int half = (ref.worldWidth > ref.worldHeight ? ref.worldWidth : ref.worldHeight) / 2;
                    decalAdd(*w.actors, picture, at, normal, half, static_cast<std::uint16_t>(sprite), -1, true);
                    return;
                }
                const int family = decalFamily(picture);
                decalAdd(*w.actors, picture, at, normal, static_cast<int>(record.extra[1]),
                         family == 8 ? kMarkAlien
                                     : (family == 5 ? kMarkBlood
                                                    : (family == 10 ? kMarkSoot
                                                                    : (picture >= kDecalWallCrack ? kMarkWall : kMarkShot))),
                         -1, true);
                return;
            }
        }

        /// The marks that were on the map from the start, where the player
        /// is: the nearest (`marksLimit` of them, within `kMarkSight` units) are
        /// kept on the walls, out of the same store as the marks shots
        /// leave; the rest wait until the player comes near. A map may have
        /// a hundred and more, and the hardware has polygons for a handful.
        inline constexpr int kMarksNear = 32;  ///< the most (a DS keeps half: LogicWorld::marksLimit)
        inline constexpr int kMarkSight = 640;
        inline void marksNear(LogicWorld &w, const PlayerState &player)
        {
            int near[kMarksNear];
            fx nearD[kMarksNear];
            int count = 0;
            const int most = w.marksLimit < 1 ? 1 : (w.marksLimit > kMarksNear ? kMarksNear : w.marksLimit);
            for (int i = 0; i < w.recordCount; ++i)
            {
                const LogicRecord &record = w.records[i];
                if (record.kind != static_cast<std::uint8_t>(LogicKind::Mark) || !(record.flags & kLogicStartOpen) ||
                    w.states[i].dead)
                    continue;
                const fx d[3] = {record.move[0] - player.origin[0], record.move[1] - player.origin[1],
                                 record.move[2] - player.origin[2]};
                const fx distance = fxLength(d);
                if (distance > fxFromInt(kMarkSight))
                    continue;
                int at = count < most ? count : most - 1;
                if (count >= most && distance >= nearD[at])
                    continue;
                while (at > 0 && nearD[at - 1] > distance)
                {
                    near[at] = near[at - 1];
                    nearD[at] = nearD[at - 1];
                    --at;
                }
                near[at] = i;
                nearD[at] = distance;
                if (count < most)
                    ++count;
            }
            for (int n = 0; n < count; ++n)
                markPlace(w, w.records[near[n]]);
        }

        /// The ways between the path nodes, as they are now: a link that
        /// passes through a door is open, or shut (and opens for whoever
        /// opens doors), or no way at all (locked; or something that has to
        /// be broken and has not been). And the doors those who walk have
        /// asked for are opened.
        inline void doorActivate(LogicWorld &w, const PlayerState &player, int index);
        inline void linksTick(LogicWorld &w, const PlayerState &player)
        {
            if (w.actors == nullptr)
                return;
            ActorWorld &a = *w.actors;
            // Which record has each brush (few hundred of each at most).
            constexpr int kBrushes = 1024;
            std::int16_t recordOf[kBrushes];
            const int brushes = w.brushCount < kBrushes ? w.brushCount : kBrushes;
            for (int b = 0; b < brushes; ++b)
                recordOf[b] = -1;
            for (int i = 0; i < w.recordCount; ++i)
            {
                if (w.records[i].brush < brushes && recordOf[w.records[i].brush] < 0)
                    recordOf[w.records[i].brush] = static_cast<std::int16_t>(i);
            }
            for (int n = 0; n < a.doorsWanted; ++n)
            {
                const int brush = a.doorWanted[n];
                const int index = brush < brushes ? recordOf[brush] : -1;
                if (index < 0 || w.states[index].dead || w.states[index].move != MoveState::AtBottom)
                    continue;
                const auto kind = static_cast<LogicKind>(w.records[index].kind);
                if (kind == LogicKind::Door || kind == LogicKind::DoorRotating)
                    doorActivate(w, player, index);
            }
            a.doorsWanted = 0;
            if (a.linkShut == nullptr || a.links == nullptr)
                return;
            for (int l = 0; l < a.linkCount; ++l)
            {
                const int brush = a.links[l].brush;
                std::uint8_t shut = kLinkOpen;
                const int index = brush < brushes ? recordOf[brush] : -1;
                if (brush != kNoBrush && index >= 0 && !w.states[index].dead)
                {
                    const LogicRecord &record = w.records[index];
                    const auto kind = static_cast<LogicKind>(record.kind);
                    if (kind == LogicKind::Door || kind == LogicKind::DoorRotating)
                    {
                        const MoveState move = w.states[index].move;
                        // (One that works by being walked up to or used, and
                        // is not held shut by a master: that opens. One that
                        // waits for a button somewhere does not.)
                        if (move == MoveState::AtTop || move == MoveState::GoingUp)
                            shut = kLinkOpen;
                        else if (logicMasterAllows(w, index) &&
                                 (record.name == 0 || (record.flags & (kLogicUseOnly | kLogicTouch))))
                            shut = kLinkDoor;
                        else
                            shut = kLinkNoWay;
                    }
                    else
                    {
                        // Something to be broken first, or a wall that is
                        // there for now.
                        shut = (w.movers != nullptr && w.movers[brush].solid) ? kLinkNoWay : kLinkOpen;
                    }
                }
                a.linkShut[l] = shut;
            }
        }

        /// A door's or a button's locked (or unlocked) sound and line, from
        /// the middle of it, as the game's PlayLockSounds: the line at 0.85
        /// of full, and the sound at a quarter when the line comes with it
        /// (so the line is heard), full when alone.
        inline void lockSay(LogicWorld &w, int index, std::uint16_t sound, std::uint16_t line)
        {
            const LogicRecord &record = w.records[index];
            fx at[3] = {record.move[0], record.move[1], record.move[2]};
            if (record.brush != kNoBrush)
            {
                const MoverHull &hull = w.movers[record.brush];
                for (int a = 0; a < 3; ++a)
                    at[a] = (hull.mins[a] + hull.maxs[a]) / 2;
            }
            const bool both = sound != kNoSound && line != kNoSound;
            w.heard->add(sound, at, both ? 32 : 127, kRadiusLarge);
            if (line != kNoSound)
                w.heard->add(line, at, 108, kRadiusLarge, kSoundSentence);
        }

        /// What a door or a button that will not work for the player says
        /// for itself: its locked sound and its line, and not again for a
        /// couple of seconds however often it is tried.
        inline void lockedSound(LogicWorld &w, int index)
        {
            if (w.heard == nullptr || w.sounds == nullptr || w.lockSaidMs > 0)
                return;
            const LogicSound &sound = w.sounds[index];
            if (sound.locked == kNoSound && sound.lockedLine == kNoSound)
                return;
            lockSay(w, index, sound.locked, sound.lockedLine);
            w.lockSaidMs = 2000;
        }

        /// It has refused the player: remembered, so that the day it works
        /// for them it can say so (unlockedSound).
        inline void lockRefused(LogicWorld &w, int index)
        {
            for (const std::uint16_t known : w.refused)
            {
                if (known == index + 1)
                    return;
            }
            w.refused[w.refusedNext++ % LogicWorld::kRefused] = static_cast<std::uint16_t>(index + 1);
        }

        /// A door or a button works for the player: if it had refused them
        /// before, its unlocked sound and line, once.
        inline void unlockedSound(LogicWorld &w, int index)
        {
            bool had = false;
            for (std::uint16_t &known : w.refused)
            {
                if (known == index + 1)
                {
                    known = 0;
                    had = true;
                }
            }
            if (!had || w.heard == nullptr || w.sounds == nullptr)
                return;
            const LogicSound &sound = w.sounds[index];
            lockSay(w, index, sound.unlocked, sound.unlockedLine);
        }

        inline void moverStart(LogicWorld &w, int index, MoveState direction)
        {
            // A door sounds whichever way it goes; a button when it is
            // pressed, not when it comes back.
            if (w.records[index].kind != static_cast<std::uint8_t>(LogicKind::Button) ||
                direction == MoveState::GoingUp)
                recordSound(w, index, false);
            w.states[index].move = direction;
            w.states[index].timer = -1;
        }

        /// A door or button has arrived at one end of its travel.
        inline void moverArrived(LogicWorld &w, PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            const bool top = state.move == MoveState::GoingUp;
            state.move = top ? MoveState::AtTop : MoveState::AtBottom;
            recordSound(w, index, true);

            if (record.kind == static_cast<std::uint8_t>(LogicKind::Door) ||
                record.kind == static_cast<std::uint8_t>(LogicKind::DoorRotating))
            {
                // A door fires its target at BOTH ends, and its second target
                // at the end where it is shut.
                if (top)
                {
                    if (!(record.flags & kLogicToggle) && record.waitMs >= 0)
                        state.timer = record.waitMs; // then it goes back by itself
                    if (record.flags & kLogicStartOpen)
                        logicFire(w, player, record.netName, index, kUseToggle, 0);
                }
                else if (!(record.flags & kLogicStartOpen))
                {
                    logicFire(w, player, record.netName, index, kUseToggle, 0);
                }
                logicUseTargets(w, player, index, kUseToggle, 0);
                return;
            }

            // A button.
            if (top)
            {
                // Pushed in: it fires, and pops back out after its wait unless
                // it is a toggle or is made to stay in (wait -1).
                if (!(record.flags & kLogicToggle) && record.waitMs >= 0)
                    state.timer = record.waitMs;
                logicUseTargets(w, player, index, kUseToggle, 0);
                return;
            }
            // Back out. A toggle fires again; and any multisource it feeds is
            // used once more, which turns this button's input back off.
            if (record.flags & kLogicToggle)
                logicUseTargets(w, player, index, kUseToggle, 0);
            if (record.target != 0)
            {
                for (int i = 0; i < w.recordCount; ++i)
                {
                    if (w.records[i].name == record.target && !w.states[i].dead &&
                        w.records[i].kind == static_cast<std::uint8_t>(LogicKind::MultiSource))
                        logicUse(w, player, i, index, kUseToggle, 1);
                }
            }
        }

        /// What a brush on hinges is turned by at rest, before its hinge
        /// turns it (the game's CRotDoor opens FROM its angles): extra[1..3],
        /// pitch, yaw and roll. (0 from a converter before it was kept.)
        inline void hingeRest(const LogicRecord &record, std::uint16_t *angles)
        {
            for (int a = 0; a < 3; ++a)
                angles[a] = static_cast<std::uint16_t>(record.extra[1 + a]);
        }

        inline void moverTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (state.move != MoveState::GoingUp && state.move != MoveState::GoingDown)
                return;
            const bool up = state.move == MoveState::GoingUp;

            // (A door on hinges; or a button that turns: a valve's wheel.)
            const bool rotating =
                record.kind == static_cast<std::uint8_t>(LogicKind::DoorRotating) ||
                (record.kind == static_cast<std::uint8_t>(LogicKind::Button) && (record.flags & kLogicTurns));
            // How much of the travel this much time covers, in Q30. For a
            // rotating door the travel is an angle and the speed an angular
            // one, in the same unit, so the same division serves.
            const fx distance = rotating ? (state.travel[0] < 0 ? -state.travel[0] : state.travel[0])
                                         : fxLength(state.travel);
            std::int32_t step = kFractionOne;
            if (distance > 0 && record.speed > 0)
            {
                const std::int64_t travelMs =
                    (static_cast<std::int64_t>(distance) * 1000) / record.speed;
                if (travelMs > ms)
                    step = static_cast<std::int32_t>(
                        (static_cast<std::int64_t>(ms) << kFractionShift) / travelMs);
            }
            std::int32_t phase = up ? state.phase + step : state.phase - step;
            if (phase > kFractionOne || phase < state.phase - step - 1)
                phase = kFractionOne; // past the end (and guards the add's overflow)
            if (phase < 0)
                phase = 0;

            if (record.brush != kNoBrush && rotating)
            {
                // Turned, not moved. A door that turns into the player does
                // not shove them: it is blocked, and turns back.
                const int axis = static_cast<int>(record.extra[0] % 3);
                std::uint16_t angles[3];
                hingeRest(record, angles);
                const std::uint16_t before = w.brushStates[record.brush].angles[axis];
                // From the angle it rests at (state.bits), by its travel.
                angles[axis] = static_cast<std::uint16_t>(
                    angles[axis] + state.bits +
                    static_cast<std::uint32_t>(
                        (static_cast<std::int64_t>(state.travel[0]) * phase) >> kFractionShift));
                logicPlaceBrushTurned(w, record.brush, state.base, angles);
                if (playerInsideBrush(w, player, record.brush))
                {
                    angles[axis] = before;
                    logicPlaceBrushTurned(w, record.brush, state.base, angles);
                    crushPlayer(w, player, index, ms);
                    if (record.waitMs >= 0)
                        state.move = up ? MoveState::GoingDown : MoveState::GoingUp;
                    return;
                }
            }
            else if (record.brush != kNoBrush)
            {
                fx pos[3];
                for (int a = 0; a < 3; ++a)
                {
                    pos[a] = state.base[a] +
                             static_cast<fx>((static_cast<std::int64_t>(state.travel[a]) * phase) >>
                                             kFractionShift);
                }
                bool byPlayer = false;
                if (!moveBrush(w, player, record.brush, pos, &byPlayer))
                {
                    // Something is in the way: the player is hurt by it, if
                    // it hurts. A door that returns by itself turns round;
                    // one that waits for ever just keeps trying.
                    if (byPlayer)
                        crushPlayer(w, player, index, ms);
                    if (record.kind == static_cast<std::uint8_t>(LogicKind::Door) &&
                        record.waitMs >= 0)
                        state.move = up ? MoveState::GoingDown : MoveState::GoingUp;
                    return;
                }
            }
            state.phase = phase;
            if (phase == (up ? kFractionOne : 0))
                moverArrived(w, player, index);
        }

        inline void doorActivate(LogicWorld &w, const PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (!logicMasterAllows(w, index))
            {
                lockedSound(w, index);
                lockRefused(w, index);
                return;
            }
            unlockedSound(w, index);
            if (record.kind == static_cast<std::uint8_t>(LogicKind::DoorRotating) &&
                state.move == MoveState::AtBottom && record.extra[0] % 3 == kYaw &&
                !(record.flags & (kLogicOneWay | kLogicStartOpen)) && record.brush != kNoBrush)
            {
                // It opens AWAY from the player: which way that is depends on
                // which side of the hinge they are and which way they face.
                // The game's test: the turn from "hinge to player" to "hinge to
                // a point just ahead of the player". Only a door on an upright
                // hinge (the game's own rule): a lever that tips or rolls
                // always goes the way the map says. And the way the map says
                // is what is turned round (its sign kept: a negative distance,
                // "reverse direction").
                const fx *pivot = w.brushStates[record.brush].pos;
                const std::int64_t vx = player.origin[0] - pivot[0];
                const std::int64_t vy = player.origin[1] - pivot[1];
                const std::int64_t nx = vx + player.facingXY[0] * 10;
                const std::int64_t ny = vy + player.facingXY[1] * 10;
                const bool flip = (vx * ny - vy * nx) < 0;
                state.travel[0] = flip ? -record.move[0] : record.move[0];
            }
            if ((record.flags & kLogicToggle) && state.move == MoveState::AtTop)
                moverStart(w, index, MoveState::GoingDown);
            else
                moverStart(w, index, MoveState::GoingUp);
        }
    } // namespace detail

    // ------------------------------------------------------------------ trains

    namespace detail
    {
        /// A train's state.bits is the corner it is at, or last left; with
        /// this bit, the corner it is HEADING to (a train that came from the
        /// map before, on its way to a corner no corner of this map leads to).
        inline constexpr std::uint32_t kTrainHeading = 0x10000;
        /// ... and with this one, it is going straight through the corner it
        /// is at (a wait of 0): its sound goes on, and is not started again.
        /// Anything else it sets off from is a stop, and it is heard setting off.
        inline constexpr std::uint32_t kTrainPassing = 0x20000;

        /// Where a train's brush has to be for its middle to be on a corner.
        inline void trainPosAt(const LogicWorld &w, int index, std::uint16_t corner, fx *out)
        {
            const LogicRecord &record = w.records[index];
            for (int a = 0; a < 3; ++a)
            {
                // The brush is placed by its origin, and its middle is `move`
                // away from the origin it was built at.
                out[a] = w.corners[corner].pos[a] - record.move[a] +
                         w.brushes[record.brush].origin[a];
            }
        }

        inline void trainArrived(LogicWorld &w, PlayerState &player, int index, std::uint16_t corner);

        /// Set off for the corner after the one the train is at.
        inline void trainNext(LogicWorld &w, PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            const bool passing = (state.bits & kTrainPassing) != 0;
            state.bits &= ~kTrainPassing;
            const auto at = static_cast<std::uint16_t>(state.bits);
            if (record.brush == kNoBrush || at == kNoCorner)
                return;
            if (state.bits & kTrainHeading)
            {
                // On its way to `at` itself (stopped on the way, and set
                // off again): on to it, from where it is.
                fx target[3];
                trainPosAt(w, index, at, target);
                for (int a = 0; a < 3; ++a)
                {
                    state.base[a] = w.brushStates[record.brush].pos[a];
                    state.travel[a] = target[a] - state.base[a];
                }
                state.phase = 0;
                state.move = MoveState::GoingUp;
                state.timer = -1;
                state.running = true;
                if (!passing)
                    recordSound(w, index, false);
                return;
            }
            const PathCorner &here = w.corners[at];
            if (here.next == kNoCorner)
            {
                state.running = false; // the end of the line
                return;
            }
            // The speed is the one given by the corner being LEFT.
            if (here.speed != 0)
                state.speed = here.speed;

            fx target[3];
            trainPosAt(w, index, here.next, target);
            if (here.flags & kCornerTeleport)
            {
                moveBrush(w, player, record.brush, target);
                trainArrived(w, player, index, here.next);
                // (Put there from a stop: what comes after is setting off.)
                if (!passing)
                    state.bits &= ~kTrainPassing;
                return;
            }
            for (int a = 0; a < 3; ++a)
            {
                state.base[a] = w.brushStates[record.brush].pos[a];
                state.travel[a] = target[a] - state.base[a];
            }
            state.phase = 0;
            state.move = MoveState::GoingUp;
            state.timer = -1;
            // (Heard setting off from where it had stopped -- at a corner, or
            // on its way, or as the map begins: not at every corner of a
            // route it goes straight through.)
            if (!passing)
                recordSound(w, index, false);
            state.running = true;
        }

        inline void trainArrived(LogicWorld &w, PlayerState &player, int index, std::uint16_t corner)
        {
            LogicState &state = w.states[index];
            const PathCorner &here = w.corners[corner];
            state.bits = corner;
            state.move = MoveState::AtBottom;
            // (... and stopping, where it stops.)
            if (here.waitMs > 0 || (here.flags & kCornerWaitForUse) || here.next == kNoCorner)
                recordSound(w, index, true);
            logicFire(w, player, here.fireTarget, index, kUseToggle, 0);
            if (here.flags & kCornerWaitForUse)
            {
                state.running = false;
                state.timer = -1;
                return;
            }
            // Its wait, then on. A wait of 0 is "next tick", which keeps a
            // route of corners with no waits from being walked in one go.
            state.timer = here.waitMs > 0 ? here.waitMs : 0;
            if (here.waitMs <= 0 && here.next != kNoCorner)
                state.bits |= kTrainPassing;
        }

        inline void trainTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (state.move != MoveState::GoingUp || record.brush == kNoBrush)
                return;
            const fx distance = fxLength(state.travel);
            std::int32_t step = kFractionOne;
            if (distance > 0 && state.speed > 0)
            {
                const std::int64_t travelMs =
                    (static_cast<std::int64_t>(distance) * 1000) / state.speed;
                if (travelMs > ms)
                    step = static_cast<std::int32_t>(
                        (static_cast<std::int64_t>(ms) << kFractionShift) / travelMs);
            }
            std::int32_t phase = state.phase + step;
            if (phase > kFractionOne || phase < state.phase)
                phase = kFractionOne;
            fx pos[3];
            for (int a = 0; a < 3; ++a)
            {
                pos[a] = state.base[a] +
                         static_cast<fx>((static_cast<std::int64_t>(state.travel[a]) * phase) >>
                                         kFractionShift);
            }
            // A blocked train does not turn round: it waits, pushing -- and
            // hurting the player in its way.
            bool byPlayer = false;
            if (!moveBrush(w, player, record.brush, pos, &byPlayer))
            {
                if (byPlayer)
                    crushPlayer(w, player, index, ms);
                return;
            }
            state.phase = phase;
            if (phase == kFractionOne)
                trainArrived(w, player, index,
                             (state.bits & kTrainHeading) ? static_cast<std::uint16_t>(state.bits)
                                                         : w.corners[static_cast<std::uint16_t>(state.bits)].next);
        }
    } // namespace detail

    // ------------------------------------------------------------------ damage

    /// Hurt the player, as playerTakeDamage (or, with a negative amount,
    /// heal them: never past 100). Dying is the caller's to act on: it sees
    /// health at 0 or below. `kinds`: what kinds of hurt it is that the
    /// display shows (kHurt*); `type`: what kind it is (kDmg*).
    inline void logicHurt(PlayerState &player, std::int32_t amount, std::uint8_t kinds = 0,
                          std::uint32_t type = kDmgGeneric)
    {
        if (amount > 0)
        {
            playerTakeDamage(player, amount, type, nullptr, kinds);
            return;
        }
        player.health -= amount;
        if (player.health > 100)
            player.health = 100;
    }

    namespace detail
    {
        /// The mover `index` (a door, a train) is held up by the player: its
        /// "dmg" hurts them -- a door's each time (CBaseDoor::Blocked), a
        /// train's no more than every half second (CFuncTrain::Blocked:
        /// state.hurt is the ms until it can again).
        inline void crushPlayer(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (record.damage == 0)
                return;
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Train))
            {
                if ((state.hurt -= ms) > 0)
                    return;
                state.hurt = 500;
            }
            logicHurt(player, record.damage, 0, kDmgCrush);
        }

        /// What a breaking box had in it (its "spawnobject", a kind of item
        /// in the record's pad): the item the converter kept where the box's
        /// middle is at rest, put where its middle is now and let down to the
        /// floor -- the game's Create at VecBModelOrigin, and the item's
        /// DROP_TO_FLOOR.
        inline void breakSpill(LogicWorld &w, const LogicRecord &record)
        {
            if (record.pad == 0 || record.brush == kNoBrush || record.brush >= w.brushCount || w.brushes == nullptr ||
                w.actors == nullptr || w.actors->records == nullptr || w.world == nullptr)
                return;
            const Brush &brush = w.brushes[record.brush];
            const BrushState &now = w.brushStates[record.brush];
            fx rest[3], here[3];
            for (int a = 0; a < 3; ++a)
            {
                rest[a] = fxFromInt(brush.mins[a] + brush.maxs[a]) / 2;
                here[a] = rest[a] + now.pos[a] - brush.origin[a];
            }
            ActorWorld &a = *w.actors;
            for (int slot = 0; slot < a.recordCount && slot < a.count; ++slot)
            {
                const ActorRecord &item = a.records[slot];
                if (item.kind != static_cast<std::uint8_t>(ActorKind::Item) || !(item.flags & kActorDormant) ||
                    item.item != record.pad || a.states[slot].active)
                    continue;
                // (Its bounds are whole units, rounded out: near enough.)
                const fx d[3] = {item.origin[0] - rest[0], item.origin[1] - rest[1], item.origin[2] - rest[2]};
                if (fxLength(d) > fxFromInt(4))
                    continue;
                actorSetup(a, slot, item, *w.world);
                ActorState &spilled = a.states[slot];
                spilled.flags &= static_cast<std::uint8_t>(~kActorDormant);
                for (int k = 0; k < 3; ++k)
                    spilled.pos[k] = here[k];
                pointToFloor(*w.world, spilled.pos, fxFromInt(256));
                actorPlaceBox(a, slot);
                return;
            }
        }

        /// It breaks: gone from the map, whoever stood on it left in the air,
        /// what it had in it let out, and its targets fired.
        inline void breakIt(LogicWorld &w, PlayerState &player, int index, int depth)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (state.dead)
                return;
            state.dead = true;
            state.timer = -1;
            if (w.brokeCount < 4)
                w.broke[w.brokeCount++] = static_cast<std::uint16_t>(index);
            if (w.achievements != nullptr)
                w.achievements->push(AchEventType::Broke, 0);
            recordSound(w, index, false); // what it is made of, breaking
            if (record.brush != kNoBrush)
            {
                if (player.groundMover == static_cast<int>(record.brush))
                {
                    player.onGround = false;
                    player.groundMover = -1;
                }
                logicSetBrushActive(w, record.brush, false);
            }
            breakSpill(w, record);
            logicUseTargets(w, player, index, kUseToggle, depth);
        }

        inline bool isBreakable(const LogicRecord &record)
        {
            return record.kind == static_cast<std::uint8_t>(LogicKind::Breakable) ||
                   (record.kind == static_cast<std::uint8_t>(LogicKind::Pushable) &&
                    (record.flags & kLogicBreaks));
        }

        /// Whether a record, once used up (LogicState::dead), stays used up
        /// in a map that is come back to: a trigger that fires once, a
        /// monstermaker that has made all it makes, a scene that has been
        /// played, a line said once, a relay that removes itself. (The game
        /// keeps every entity of a map left; this is what of it would
        /// otherwise be seen to happen again -- the zombie of c1a1's maker.)
        inline bool staysSpent(const LogicRecord &record)
        {
            switch (static_cast<LogicKind>(record.kind))
            {
            case LogicKind::TriggerOnce:
            case LogicKind::Maker:
            case LogicKind::Script:
            case LogicKind::Sentence:
                return true;
            default:
                // (A trigger_auto marked so in a map cooked since: one
                // that is not fires again on every visit, as in the game.)
                return (record.flags & kLogicFireOnce) != 0 &&
                       (record.kind == static_cast<std::uint8_t>(LogicKind::TriggerRelay) ||
                        record.kind == static_cast<std::uint8_t>(LogicKind::TriggerAuto) ||
                        record.kind == static_cast<std::uint8_t>(LogicKind::Message));
            }
        }
    } // namespace detail

    // ------------------------------------------------------------------ round 39's

    namespace detail
    {
        /// What shots and blows land on, of the brushes: what breaks, and the
        /// two that only take note (a func_guntarget, a button_target).
        inline bool takesBlows(const LogicRecord &record)
        {
            return isBreakable(record) || record.kind == static_cast<std::uint8_t>(LogicKind::GunTarget) ||
                   record.kind == static_cast<std::uint8_t>(LogicKind::ButtonTarget);
        }

        /// The record whose brush is `mover` and that a blow to it can
        /// reach -- something that breaks, a target -- or -1.
        inline int blowTaker(const LogicWorld &w, int mover)
        {
            for (int i = 0; i < w.recordCount; ++i)
            {
                if (w.records[i].brush == static_cast<std::uint16_t>(mover) && !w.states[i].dead &&
                    takesBlows(w.records[i]))
                    return i;
            }
            return -1;
        }

        /// The game's chance for what talks to itself (a speaker): 24 bits.
        inline std::uint32_t logicChance(LogicWorld &w)
        {
            w.effectSeed = w.effectSeed * 1664525u + 1013904223u;
            return w.effectSeed >> 8;
        }

        /// The view goes back to the player's, and the camera fires what it
        /// was looking at (the game's: its target, toggled).
        inline void cameraEnd(LogicWorld &w, PlayerState &player)
        {
            const int index = w.camera;
            w.camera = -1;
            w.cameraLeftMs = 0;
            if (index >= 0 && index < w.recordCount)
                logicUseTargets(w, player, index, kUseToggle, 0);
        }

        inline void cameraUse(LogicWorld &w, PlayerState &player, int index, int useType)
        {
            const LogicRecord &record = w.records[index];
            const bool on = useType == kUseToggle ? w.camera != index : useType != kUseOff;
            if (!on)
            {
                if (w.camera == index)
                    cameraEnd(w, player);
                return;
            }
            // (One with nothing to look at is not looked through: the game's.)
            if (!(record.extra[0] & 2) && record.target == 0)
                return;
            LogicState &state = w.states[index];
            for (int a = 0; a < 3; ++a)
                state.base[a] = (record.extra[0] & 1) ? player.origin[a] : record.move[a];
            if (record.extra[0] & 1)
                state.base[2] += player.viewHeight;
            w.camera = static_cast<std::int16_t>(index);
            w.cameraLeftMs = record.waitMs > 0 ? record.waitMs : 0;
        }

        inline void speakerUse(LogicWorld &w, int index, int useType)
        {
            LogicState &state = w.states[index];
            const bool on = useType == kUseToggle ? !state.running : useType != kUseOff;
            // Switched on, it speaks straight away.
            if (on && !state.running)
                state.timer = 100;
            state.running = on;
        }

        inline void speakerTick(LogicWorld &w, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (!state.running || state.timer < 0)
                return;
            state.timer -= ms;
            if (state.timer > 0)
                return;
            // One of its lines, and the next a while on -- a quarter of a
            // minute to over two.
            if (w.heard != nullptr && record.auxCount > 0 && w.aux != nullptr)
            {
                const LogicAux &line = w.aux[record.firstAux + logicChance(w) % record.auxCount];
                // (From where it is, at the game's attenuation 0.3.)
                w.heard->add(line.target, record.move, static_cast<int>(record.extra[0]), 3333, kSoundSentence);
            }
            state.timer = 15000 + static_cast<std::int32_t>(logicChance(w) % 120000);
        }

        inline void gunTargetUse(LogicWorld &w, PlayerState &player, int index, int useType)
        {
            LogicState &state = w.states[index];
            const bool on = useType == kUseToggle ? !state.running : useType != kUseOff;
            if (on && !state.running)
            {
                // Whole again, and off along its path.
                state.hurt = 0;
                state.running = true;
                trainNext(w, player, index);
            }
            else if (!on && state.running)
            {
                state.running = false;
                state.move = MoveState::AtBottom;
                state.timer = -1;
                state.bits &= ~kTrainPassing;
            }
        }

        /// Hit while it rides: when it has taken all it takes it stops, and
        /// fires its `message` (netName).
        inline void gunTargetHurt(LogicWorld &w, PlayerState &player, int index, std::int32_t damage)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (!state.running)
                return;
            state.hurt += damage;
            if (state.hurt < static_cast<std::int32_t>(record.extra[0]))
                return;
            state.running = false;
            state.move = MoveState::AtBottom;
            state.timer = -1;
            state.bits &= ~detail::kTrainPassing;
            logicFire(w, player, record.netName, index, kUseToggle, 0);
        }

        /// Its other frame, and its targets switched to match: on with it,
        /// off with it.
        inline void buttonTargetToggle(LogicWorld &w, PlayerState &player, int index, int depth)
        {
            LogicState &state = w.states[index];
            state.bits ^= 1u;
            logicFire(w, player, logicTargetOf(w, index), index, (state.bits & 1u) ? kUseOn : kUseOff, depth);
        }

        inline void bloodUse(LogicWorld &w, PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            if (w.actors == nullptr)
                return;
            fx at[3];
            for (int a = 0; a < 3; ++a)
                at[a] = (record.extra[2] & 4) ? player.origin[a] : record.move[a];
            if (record.extra[2] & 4)
                at[2] += player.viewHeight;
            // Which way it goes: anywhere, or the way the map points it.
            std::uint16_t yaw = static_cast<std::uint16_t>(record.extra[3]);
            std::uint16_t pitch = static_cast<std::uint16_t>(record.extra[3] >> 16);
            if (record.extra[2] & 1)
            {
                yaw = static_cast<std::uint16_t>(logicChance(w));
                pitch = static_cast<std::uint16_t>(logicChance(w) & 0x3FFF);
            }
            fx sy, cy, sp, cp;
            fxSinCos(yaw, sy, cy);
            fxSinCos(pitch, sp, cp);
            const fx along[3] = {fxMulShort(cy, cp), fxMulShort(sy, cp), -sp}; // (16-bit sines)
            const bool yellow = record.extra[0] != 0;
            const int bursts = record.extra[1] > 150 ? 3 : 1;
            for (int n = 0; n < bursts; ++n)
            {
                fx where[3];
                for (int a = 0; a < 3; ++a)
                    where[a] = at[a] + along[a] * (8 * n);
                effectAdd(*w.actors, kEffectBlood, where, nullptr, yellow ? 1 : 0, 400);
            }
            if ((record.extra[2] & 8) && w.world != nullptr)
                markBlood(*w.actors, *w.world, at, along, yellow, record.extra[1] > 150);
        }
    } // namespace detail

    /// A blow from the player: `damage` off what the thing can take, and it
    /// breaks when that runs out. Nothing happens to what only its name can
    /// break, or to the glass that never does.
    /// A blow of `damage`, of the kind `type` (kDmg*), to what breaks (or a
    /// gun target, a button target). `byPlayer`: the player's own -- the
    /// only blow that breaks what one crowbar blow breaks.
    inline void logicStrike(LogicWorld &w, PlayerState &player, int index, std::int32_t damage,
                            std::uint32_t type = kDmgGeneric, bool byPlayer = false)
    {
        const LogicRecord &record = w.records[index];
        LogicState &state = w.states[index];
        if (!state.dead && record.kind == static_cast<std::uint8_t>(LogicKind::GunTarget))
        {
            detail::gunTargetHurt(w, player, index, damage);
            return;
        }
        if (!state.dead && record.kind == static_cast<std::uint8_t>(LogicKind::ButtonTarget))
        {
            detail::buttonTargetToggle(w, player, index, 0);
            return;
        }
        if (state.dead || !detail::isBreakable(record) || (record.flags & kLogicTriggerOnly) ||
            record.extra[1] == 7)
            return;
        // CBreakable::TakeDamage: the crowbar's blow does twice (breakables
        // take double from it), a poisoned dart a tenth; and one marked to
        // break at a crowbar's blow does, at the player's.
        if (type & kDmgClub)
            damage *= 2;
        if (type & kDmgPoison)
            damage /= 10;
        if ((record.flags & kLogicInstant) && byPlayer && (type & kDmgClub))
            damage = 0x7FFFFF;
        if (w.sharing && w.struckCount < 8)
        {
            w.struck[w.struckCount * 2] = static_cast<std::int16_t>(index);
            w.struck[w.struckCount * 2 + 1] = static_cast<std::int16_t>(damage > 32000 ? 32000 : damage);
            ++w.struckCount;
        }
        // state.phase holds the damage taken so far (a box that is pushed
        // keeps its own count apart from where it is).
        std::int32_t &taken = record.kind == static_cast<std::uint8_t>(LogicKind::Pushable)
                                  ? state.timer
                                  : state.phase;
        if (taken < 0)
            taken = 0;
        taken += damage;
        if (taken >= static_cast<std::int32_t>(record.extra[0]))
        {
            taken = -1;
            detail::breakIt(w, player, index, 0);
        }
    }

    // ------------------------------------------------------------------ boxes

    namespace detail
    {
        /// A box that can be pushed: shoved by the player walking into it,
        /// slowed by the floor, pulled down by gravity, stopped by the world.
        /// Its velocity is state.travel; state.bits is whether it rests on
        /// something.
        /// Where a box's middle is, for its move: the middle of its box as
        /// it stands -- but as high as the middle of one of the player's
        /// hulls whose feet are where the box's bottom is (a hull taller
        /// than the box would otherwise start out sunk in the floor).
        inline void boxMiddle(const LogicWorld &w, int brush, bool crouched, fx *middle)
        {
            const Brush &built = w.brushes[brush];
            const fx *pos = w.brushStates[brush].pos;
            for (int a = 0; a < 3; ++a)
                middle[a] = (fxFromInt(built.mins[a]) + fxFromInt(built.maxs[a])) / 2 + (pos[a] - built.origin[a]);
            middle[2] = fxFromInt(built.mins[2]) + (pos[2] - built.origin[2]) + kPlayerHalfHeight[crouched ? 1 : 0];
        }

        /// Whether a box (by its record) is inside the solid of brush
        /// `mover`, as the box is wide: any of the person-wide shapes it is
        /// made of (boxTrace) is.
        inline bool boxInMover(const LogicWorld &w, int record, int mover)
        {
            const int brush = w.records[record].brush;
            const bool crouched = w.records[record].extra[3] != 0;
            const Brush &built = w.brushes[brush];
            const MoverHull &hull = w.movers[mover];
            fx middle[3];
            boxMiddle(w, brush, crouched, middle);
            const int over[2] = {built.maxs[0] - built.mins[0] - 32, built.maxs[1] - built.mins[1] - 32};
            int count[2];
            for (int a = 0; a < 2; ++a)
                count[a] = over[a] <= 0 ? 1 : (over[a] + 31) / 32 + 1 > 5 ? 5 : (over[a] + 31) / 32 + 1;
            for (int i = 0; i < count[0]; ++i)
            {
                for (int j = 0; j < count[1]; ++j)
                {
                    const fx at[3] = {
                        middle[0] + (count[0] == 1 ? 0 : fxFromInt(-over[0]) / 2 + fxFromInt(over[0]) * i / (count[0] - 1)),
                        middle[1] + (count[1] == 1 ? 0 : fxFromInt(-over[1]) / 2 + fxFromInt(over[1]) * j / (count[1] - 1)),
                        middle[2]};
                    fx local[3];
                    toMoverSpace(hull, at, local);
                    if (!hullPositionFree(crouched ? hull.crouched : hull.standing, local))
                        return true;
                }
            }
            return false;
        }

        /// Whether a box with its middle at `middle` is clear of the world's
        /// solid: the person-wide shape it is moved as, at every eight
        /// units across it. (What a move ends on is asked this as well as
        /// being swept: a sweep of its corners can end with something
        /// between them -- the map's collision juts out a little at the
        /// point of a slanted wall, and where it does is narrower than a
        /// person.)
        inline bool boxFits(const LogicWorld &w, int brush, bool crouched, const fx *middle)
        {
            const Brush &built = w.brushes[brush];
            const int over[2] = {built.maxs[0] - built.mins[0] - 32, built.maxs[1] - built.mins[1] - 32};
            const Hull &hull = crouched ? w.world->crouched : w.world->standing;
            for (int x = 0;; x += 8)
            {
                const int ox = over[0] <= 0 ? 0 : (x > over[0] ? over[0] : x) - over[0] / 2;
                for (int y = 0;; y += 8)
                {
                    const int oy = over[1] <= 0 ? 0 : (y > over[1] ? over[1] : y) - over[1] / 2;
                    const fx at[3] = {middle[0] + fxFromInt(ox), middle[1] + fxFromInt(oy), middle[2]};
                    if (!hullPositionFree(hull, at))
                        return false;
                    if (over[1] <= 0 || y >= over[1])
                        break;
                }
                if (over[0] <= 0 || x >= over[0])
                    break;
            }
            return true;
        }

        /// A box moved from `from` to `to` (its middle: boxMiddle), through
        /// the world and everything solid in it but itself -- AS WIDE AND AS
        /// DEEP AS IT IS. The map's collision knows only shapes a person
        /// wide; so a wider box is that shape at each of its corners (and
        /// between, for a long one), and goes as far as the first of them
        /// to meet something. (It used to be one such shape at its middle:
        /// a crate 64 across went 16 units into every wall, and into
        /// whatever stood beside it.)
        /// (`probing`: a look for what is there, not a move -- a corner
        /// that starts in the world's solid then simply says nothing.)
        inline Trace boxTrace(LogicWorld &w, int brush, bool crouched, const fx *from, const fx *to, bool probing = false)
        {
            const Brush &built = w.brushes[brush];
            const int over[2] = {built.maxs[0] - built.mins[0] - 32, built.maxs[1] - built.mins[1] - 32};
            int count[2];
            for (int a = 0; a < 2; ++a)
                count[a] = over[a] <= 0 ? 1 : (over[a] + 31) / 32 + 1 > 5 ? 5 : (over[a] + 31) / 32 + 1;
            // (How far above the standing shape's middle the crouched one's
            // is when their tops are the box's: 0, it is as tall or taller.)
            const int height = built.maxs[2] - built.mins[2];
            const fx rise = height < 72 ? fxFromInt(height - 54) : 0;
            const bool was = w.movers[brush].solid;
            w.movers[brush].solid = false;
            moverReachSet(*w.world, brush);
            // A brush that is turned (a door on a hinge) is met by one shape
            // at the box's middle, not by its corners, as the game meets it
            // with one hull: the shapes' room is grown along the brush's own
            // axes, and turned, a lid stood open would catch a corner well
            // clear of it (c2a3's barrels). The corners pass it by.
            int turned[8];
            int turnedCount = 0;
            if (count[0] > 1 || count[1] > 1)
            {
                for (int m = 0; m < w.world->moverCount && turnedCount < 8; ++m)
                {
                    if (m != brush && w.movers[m].turned && w.movers[m].solid)
                    {
                        turned[turnedCount++] = m;
                        w.movers[m].solid = false;
                        moverReachSet(*w.world, m);
                    }
                }
            }
            Trace best;
            bool any = false, stuck = false;
            for (int i = 0; i < count[0]; ++i)
            {
                for (int j = 0; j < count[1]; ++j)
                {
                    const fx off[2] = {count[0] == 1 ? 0 : fxFromInt(-over[0]) / 2 + fxFromInt(over[0]) * i / (count[0] - 1),
                                       count[1] == 1 ? 0 : fxFromInt(-over[1]) / 2 + fxFromInt(over[1]) * j / (count[1] - 1)};
                    const fx a[3] = {from[0] + off[0], from[1] + off[1], from[2]};
                    const fx b[3] = {to[0] + off[0], to[1] + off[1], to[2]};
                    Trace t = traceAll(*w.world, crouched, a, b);
                    // A corner that starts in the world's solid (a box the
                    // map built into a wall; a hair's breadth of rounding)
                    // may come out of it, and that is all: a move that
                    // leaves it there goes nowhere.
                    if (t.startSolid || t.allSolid)
                    {
                        stuck = true;
                        if (probing || hullPositionFree(crouched ? w.world->crouched : w.world->standing, b))
                            continue;
                        t = Trace();
                        t.fraction = 0;
                        for (int k = 0; k < 3; ++k)
                            t.endPos[k] = a[k];
                        const fx along[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                        const fx far = fxLength(along);
                        for (int k = 0; k < 3; ++k)
                            t.normal[k] = far > 0 ? -static_cast<std::int32_t>((static_cast<std::int64_t>(along[k]) << kNormalShift) / far) : 0;
                    }
                    // Going up, a box lower than the standing shape is met
                    // at its own top too, by the crouched shape level with
                    // it: what is just above it (c2a3's barrels, under the
                    // lid of their cage) is otherwise already inside that
                    // shape, and a thing that moves lets through what is in
                    // it.
                    if (!crouched && rise > 0 && b[2] > a[2])
                    {
                        const fx at[3] = {a[0], a[1], a[2] + rise}, till[3] = {b[0], b[1], b[2] + rise};
                        Trace top = traceAll(*w.world, true, at, till);
                        if (!top.startSolid && !top.allSolid && top.fraction < t.fraction)
                        {
                            top.endPos[2] -= rise;
                            t = top;
                        }
                    }
                    if (any && t.fraction >= best.fraction)
                        continue;
                    t.endPos[0] -= off[0];
                    t.endPos[1] -= off[1];
                    best = t;
                    any = true;
                }
            }
            if (turnedCount > 0)
            {
                for (int k = 0; k < turnedCount; ++k)
                {
                    w.movers[turned[k]].solid = true;
                    moverReachSet(*w.world, turned[k]);
                }
                // (The crouched shape, at the box's mid-height: the standing
                // one's room above and below, turned on its side by a lid
                // stood open, would reach out further than the box does.)
                const fx lift = fxFromInt(height / 2) - kPlayerHalfHeight[crouched ? 1 : 0];
                const fx a[3] = {from[0], from[1], from[2] + lift}, b[3] = {to[0], to[1], to[2] + lift};
                Trace t = traceAll(*w.world, true, a, b);
                if (any && !t.startSolid && !t.allSolid && t.fraction < best.fraction)
                {
                    t.endPos[2] -= lift;
                    best = t;
                }
            }
            w.movers[brush].solid = was;
            moverReachSet(*w.world, brush);
            // Against the other boxes, box against box: what is above is
            // blind to anything a corner has got the least way into (as a
            // door is to someone it closes on), and two boxes sliding past
            // each other's corners would then sink into each other. A box
            // that is already in another may only move out of it.
            if (any)
            {
                pushableList(w);
                const fx half[3] = {fxFromInt(built.maxs[0] - built.mins[0]) / 2, fxFromInt(built.maxs[1] - built.mins[1]) / 2,
                                    fxFromInt(built.maxs[2] - built.mins[2]) / 2};
                const fx centre[3] = {from[0], from[1], from[2] - kPlayerHalfHeight[crouched ? 1 : 0] + half[2]};
                const fx move[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
                for (int n = 0; n < w.pushableCount; ++n)
                {
                    const int other = w.records[w.pushables[n]].brush;
                    const MoverHull &hull = w.movers[other];
                    if (other == brush || !hull.solid || !w.brushStates[other].active)
                        continue;
                    // When, along the move, it comes to touch on every
                    // side at once -- and until when.
                    std::int64_t enter = -1, leave = kFractionOne;
                    int axis = -1;
                    bool apart = false;
                    fx least = 0;
                    int leastAxis = -1;
                    bool in = true;
                    for (int k = 0; k < 3; ++k)
                    {
                        const fx lo = hull.mins[k] - half[k] - centre[k], hi = hull.maxs[k] + half[k] - centre[k];
                        // (How far into it on this side, at the start.)
                        const fx depth = -lo < hi ? -lo : hi;
                        if (lo >= 0 || hi <= 0)
                            in = false;
                        else if (leastAxis < 0 || depth < least)
                        {
                            least = depth;
                            leastAxis = k;
                        }
                        if (move[k] == 0)
                        {
                            if (lo >= 0 || hi <= 0)
                                apart = true;
                            continue;
                        }
                        std::int64_t t0 = (static_cast<std::int64_t>(lo) << kFractionShift) / move[k];
                        std::int64_t t1 = (static_cast<std::int64_t>(hi) << kFractionShift) / move[k];
                        if (t0 > t1)
                        {
                            const std::int64_t swap = t0;
                            t0 = t1;
                            t1 = swap;
                        }
                        if (t0 > enter)
                        {
                            enter = t0;
                            axis = k;
                        }
                        leave = t1 < leave ? t1 : leave;
                    }
                    std::int64_t stop = -1;
                    int stopAxis = axis;
                    if (in)
                    {
                        // In it already: only out, the short way.
                        const fx lo = hull.mins[leastAxis] - half[leastAxis] - centre[leastAxis];
                        const fx hi = hull.maxs[leastAxis] + half[leastAxis] - centre[leastAxis];
                        const bool outwards = (-lo < hi) ? move[leastAxis] <= 0 : move[leastAxis] >= 0;
                        const bool sideways = move[leastAxis] == 0;
                        if (!outwards || (sideways && least > fxFromInt(1)))
                        {
                            stop = 0;
                            stopAxis = leastAxis;
                        }
                    }
                    else if (!apart && axis >= 0 && enter >= 0 && enter < leave && enter < best.fraction)
                    {
                        stop = enter;
                    }
                    if (stop < 0 || stop >= best.fraction)
                        continue;
                    best.fraction = static_cast<std::int32_t>(stop);
                    for (int k = 0; k < 3; ++k)
                    {
                        best.endPos[k] = from[k] + static_cast<fx>((static_cast<std::int64_t>(move[k]) * stop) >> kFractionShift);
                        best.normal[k] = 0;
                    }
                    best.normal[stopAxis] = move[stopAxis] > 0 ? -(1 << kNormalShift) : (1 << kNormalShift);
                    best.mover = static_cast<std::int16_t>(other);
                }
            }
            if (!any)
            {
                // (All of it is in something: it does not move.)
                best = Trace();
                best.startSolid = stuck;
                best.allSolid = stuck;
                best.fraction = 0;
                for (int k = 0; k < 3; ++k)
                    best.endPos[k] = from[k];
            }
            return best;
        }

        /// Whether the floor a box (by its record) finds under itself is
        /// brush `mover`: looked for as the box looks for its floor.
        inline bool boxStandsOn(LogicWorld &w, int record, int mover)
        {
            const int brush = w.records[record].brush;
            const bool crouched = w.records[record].extra[3] != 0;
            fx middle[3];
            boxMiddle(w, brush, crouched, middle);
            const fx under[3] = {middle[0], middle[1], middle[2] - fxFromInt(2)};
            const fx above[3] = {middle[0], middle[1], middle[2] + fxFromInt(8)};
            const Trace floor = boxTrace(w, brush, crouched, above, under, true);
            return !floor.startSolid && floor.fraction < kFractionOne && floor.normal[2] >= kFloorNormalZ &&
                   floor.mover == mover;
        }

        /// A box's scrape along the floor, no oftener than the game has it.
        inline void boxScrape(LogicWorld &w, int index)
        {
            LogicState &state = w.states[index];
            if (state.base[0] > 0 || state.bits == 0 || w.heard == nullptr)
                return;
            state.base[0] = 700;
            w.effectSeed = w.effectSeed * 1664525u + 1013904223u;
            const MoverHull &hull = w.movers[w.records[index].brush];
            const fx from[3] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2, hull.mins[2]};
            const int which = static_cast<int>((w.effectSeed >> 12) % 3);
            w.heard->add(which == 0 ? CommonSound::PushBox1 : (which == 1 ? CommonSound::PushBox2 : CommonSound::PushBox3),
                         from, 64, kRadiusLarge);
        }

        /// A box no faster than its top speed, its direction kept.
        inline void boxLimit(const LogicRecord &record, fx *v)
        {
            const fx flat[3] = {v[0], v[1], 0};
            const fx speed = fxLength(flat);
            const fx top = static_cast<fx>(record.extra[2]);
            if (speed > top && speed > 0)
            {
                v[0] = static_cast<fx>((static_cast<std::int64_t>(v[0]) * top) / speed);
                v[1] = static_cast<fx>((static_cast<std::int64_t>(v[1]) * top) / speed);
            }
        }

        /// How far into water a box is, as the game's FL_FLOAT has it: its
        /// level (0 dry, 1 its foot wet, 3 its middle) and the depth of its
        /// foot below the surface, which is found by halving, six times, the
        /// part of it the surface is in.
        inline int boxInWater(const LogicWorld &w, int brush, fx *depth)
        {
            *depth = 0;
            if (w.world == nullptr)
                return 0;
            const MoverHull &hull = w.movers[brush];
            const fx middle[3] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2,
                                  (hull.mins[2] + hull.maxs[2]) / 2};
            const fx foot[3] = {middle[0], middle[1], hull.mins[2] + kFxOne};
            if (!isLiquid(pointContents(*w.world, foot)))
                return 0;
            const int level = isLiquid(pointContents(*w.world, middle)) ? 3 : 1;
            const fx top[3] = {middle[0], middle[1], hull.maxs[2]};
            const fx start = hull.mins[2] - middle[2];
            if (level == 3 && pointContents(*w.world, top) == kContentsWater)
            {
                *depth = hull.maxs[2] - hull.mins[2];
                return level;
            }
            fx out = level == 3 ? hull.maxs[2] - middle[2] : 0, in = level == 3 ? 0 : start, offset = 0;
            for (int n = 0; n < 6; ++n)
            {
                offset = (out - in) / 2 + in;
                const fx at[3] = {middle[0], middle[1], middle[2] + offset};
                (pointContents(*w.world, at) == kContentsWater ? in : out) = offset;
            }
            *depth = offset - start;
            return level;
        }

        inline void pushableTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (record.brush == kNoBrush || !w.brushStates[record.brush].active)
                return;
            const int brush = record.brush;
            fx *v = state.travel;
            // (Only one that floats is looked at for water: `speed`.)
            fx depth = 0;
            const int wet = record.speed > 0 ? boxInWater(w, brush, &depth) : 0;

            // The shove: the player's own speed along the floor, added to the
            // box's, up to the fastest it can go. Only from someone with
            // their feet on the ground, or swimming at one afloat (a tenth of
            // it then), and not from someone standing on it -- who, jumping
            // off one afloat, sends it down a little.
            if (state.base[0] > 0)
                state.base[0] -= ms; // (until its scrape may be heard again)
            if (player.groundMover == brush && player.onGround && wet > 0)
                v[2] += player.velocity[2] / 10;
            if (player.touchedMover == brush && (player.onGround || wet > 0) && player.groundMover != brush &&
                !w.useHeld.value)
            {
                if (player.touchVelocity[0] != 0 || player.touchVelocity[1] != 0)
                    boxScrape(w, index);
                v[0] += player.onGround ? player.touchVelocity[0] : player.touchVelocity[0] / 10;
                v[1] += player.onGround ? player.touchVelocity[1] : player.touchVelocity[1] / 10;
                boxLimit(record, v);
                // The pusher goes at the pace of the box.
                player.velocity[0] = v[0];
                player.velocity[1] = v[1];
            }

            // At rest on a floor with nobody at it, there is nothing to work
            // out: the floor is looked at again only twice a second (it may
            // be something that breaks). state.speed counts down to that.
            if (state.bits == 1 && v[0] == 0 && v[1] == 0 && v[2] == 0 &&
                player.touchedMover != brush)
            {
                state.speed -= ms;
                if (state.speed > 0)
                    return;
                state.speed = 500;
            }

            const bool crouched = record.extra[3] != 0;
            const Brush &built = w.brushes[brush];
            // The box moves as itself, through the world AND what else is
            // solid in it -- the lift it is delivered on, the fence round
            // it, a door, another box -- but not through itself.
            auto trace = [&](const fx *from, const fx *to) { return boxTrace(w, brush, crouched, from, to); };

            // The player is in it (carried into it, set down on it by a
            // load, stood up into it): one of the two gets out of the other
            // -- the box, by the shortest way, as far as it can go; and the
            // player the rest of the way.
            if (playerInsideBrush(w, player, brush))
            {
                const MoverHull &hull = w.movers[brush];
                const fx half = fxFromInt(16);
                const fx out[4] = {hull.maxs[0] - (player.origin[0] - half), (player.origin[0] + half) - hull.mins[0],
                                   hull.maxs[1] - (player.origin[1] - half), (player.origin[1] + half) - hull.mins[1]};
                int way = 0;
                for (int k = 1; k < 4; ++k)
                    way = out[k] < out[way] ? k : way;
                fx delta[3] = {0, 0, 0};
                delta[way >> 1] = (way & 1) ? out[way] + fxFromInt(1) : -(out[way] + fxFromInt(1));
                fx middle[3];
                boxMiddle(w, brush, crouched, middle);
                const fx to[3] = {middle[0] + delta[0], middle[1] + delta[1], middle[2]};
                const Trace gone = trace(middle, to);
                const fx *at = w.brushStates[brush].pos;
                const fx went[3] = {gone.endPos[0] - middle[0], gone.endPos[1] - middle[1], 0};
                const fx there[3] = {at[0] + went[0], at[1] + went[1], at[2]};
                if (!gone.startSolid)
                    logicMoveBrushTo(w, brush, there);
                if (playerInsideBrush(w, player, brush))
                {
                    const fx rest[3] = {-(delta[0] - (gone.startSolid ? 0 : went[0])),
                                        -(delta[1] - (gone.startSolid ? 0 : went[1])), 0};
                    pushPlayer(w, player, brush, rest);
                }
                v[0] = v[1] = 0;
                state.bits = 0; // (looked at again from here)
            }

            const fx *pos = w.brushStates[brush].pos;
            fx middle[3];
            boxMiddle(w, brush, crouched, middle);
            const fx was[3] = {middle[0], middle[1], middle[2]};

            // Is there floor under it? Looked for from a little above where
            // it is: a floor that is itself coming up (a lift) is then found
            // on top of where it was, and the box goes up with it, instead of
            // being found already inside and fallen through.
            const fx under[3] = {middle[0], middle[1], middle[2] - fxFromInt(2)};
            const fx above[3] = {middle[0], middle[1], middle[2] + fxFromInt(8)};
            Trace floor = boxTrace(w, brush, crouched, above, under, true);
            if (floor.startSolid)
                floor = trace(middle, under); // no room above it to look from
            const bool resting = !floor.startSolid && floor.fraction < kFractionOne &&
                                 floor.normal[2] >= kFloorNormalZ;
            // (1: at rest on the world, which stays where it is; 2: on
            // something that may move, which has to be watched.)
            state.bits = resting ? (floor.mover >= 0 ? 2 : 1) : 0;
            bool raised = false;
            if (resting)
            {
                if (floor.endPos[2] > middle[2] + kDistEpsilon)
                {
                    middle[2] = floor.endPos[2];
                    raised = true;
                }
                if (v[2] < 0)
                    v[2] = 0;
                // The floor's friction, as it slows the player.
                const fx flat[3] = {v[0], v[1], 0};
                const fx speed = fxLength(flat);
                if (speed > 0)
                {
                    const fx control = speed < fxFromInt(100) ? fxFromInt(100) : speed;
                    const fx drop = static_cast<fx>((static_cast<std::int64_t>(control) * 4 * ms) / 1000);
                    const fx left = speed > drop ? speed - drop : 0;
                    v[0] = static_cast<fx>((static_cast<std::int64_t>(v[0]) * left) / speed);
                    v[1] = static_cast<fx>((static_cast<std::int64_t>(v[1]) * left) / speed);
                }
            }
            else if (wet < 2)
            {
                // (Its foot only in water, it falls as if it weren't: the
                // game adds gravity for that, and again for the float below.)
                v[2] -= static_cast<fx>((static_cast<std::int64_t>(fxFromInt(800)) * ms) / 1000);
            }
            // Afloat: gravity, and the water's lift, as deep as it is in it
            // times its buoyancy. At the surface its bobbing dies away within
            // half a second or so, as the game's does (4 a second, the
            // water's friction on the player); under it, it comes up as fast
            // as the lift takes it (c2a3's barrels, let out of their cage).
            // On the bottom, a lift too weak to raise it does nothing.
            if (wet > 0)
            {
                const fx lift = fxMul(depth, record.speed) - fxFromInt(800);
                if (!resting || lift > 0)
                    v[2] += static_cast<fx>((static_cast<std::int64_t>(lift) * ms) / 1000);
                if (depth < fxFromInt(built.maxs[2] - built.mins[2]))
                    v[2] -= static_cast<fx>((static_cast<std::int64_t>(v[2]) * 4 * ms) / 1000);
            }
            if (v[0] == 0 && v[1] == 0 && v[2] == 0 && !raised)
                return;

            // The move, sliding along what it meets: twice is enough for a
            // box on a floor against a wall.
            fx at[3] = {middle[0], middle[1], middle[2]};
            std::int32_t left = ms;
            for (int bump = 0; bump < 2 && left > 0; ++bump)
            {
                const fx to[3] = {
                    at[0] + static_cast<fx>((static_cast<std::int64_t>(v[0]) * left) / 1000),
                    at[1] + static_cast<fx>((static_cast<std::int64_t>(v[1]) * left) / 1000),
                    at[2] + static_cast<fx>((static_cast<std::int64_t>(v[2]) * left) / 1000)};
                const Trace tr = trace(at, to);
                if (tr.startSolid || tr.allSolid)
                {
                    v[0] = v[1] = v[2] = 0;
                    break;
                }
                // Stopped by something upright while on the ground: it may
                // be no more than a step (the lip a lift stops short by), and
                // a pushed box climbs a step as the player does -- up a
                // stair's height, along, and down again.
                if (tr.fraction < kFractionOne && resting && tr.normal[2] < kFloorNormalZ)
                {
                    const fx up[3] = {at[0], at[1], at[2] + kStepSize};
                    const Trace lifted = trace(at, up);
                    const fx along[3] = {to[0], to[1], lifted.endPos[2]};
                    const Trace over = lifted.startSolid ? lifted : trace(lifted.endPos, along);
                    const fx down[3] = {over.endPos[0], over.endPos[1], over.endPos[2] - kStepSize};
                    const Trace landed = trace(over.endPos, down);
                    const fx gain[3] = {over.endPos[0] - tr.endPos[0], over.endPos[1] - tr.endPos[1], 0};
                    if (!lifted.startSolid && !over.startSolid && !landed.startSolid &&
                        landed.fraction < kFractionOne && landed.normal[2] >= kFloorNormalZ &&
                        fxLength(gain) > kDistEpsilon)
                    {
                        for (int a = 0; a < 3; ++a)
                            at[a] = landed.endPos[a];
                        break;
                    }
                }
                for (int a = 0; a < 3; ++a)
                    at[a] = tr.endPos[a];
                if (tr.fraction >= kFractionOne)
                    break;
                // Take away the part of the velocity that goes into what was
                // hit.
                std::int64_t into = 0;
                for (int a = 0; a < 3; ++a)
                    into += static_cast<std::int64_t>(v[a]) * tr.normal[a];
                into >>= kNormalShift;
                for (int a = 0; a < 3; ++a)
                    v[a] -= static_cast<fx>((into * tr.normal[a]) >> kNormalShift);
                left = static_cast<std::int32_t>(
                    (static_cast<std::int64_t>(left) * (kFractionOne - tr.fraction)) >> kFractionShift);
            }
            // (Where it has come to is somewhere it can be -- or, having
            // been somewhere it could, it stays there.)
            if (!boxFits(w, brush, crouched, at) && boxFits(w, brush, crouched, was))
            {
                v[0] = v[1] = 0;
                if (v[2] > 0)
                    v[2] = 0;
                return;
            }
            fx moved[3];
            for (int a = 0; a < 3; ++a)
                moved[a] = pos[a] + (at[a] - was[a]);
            // It carries whoever stands on it, and does not move into the
            // player: blocked, it stops -- and does not go on gathering
            // speed while it is held up.
            if (!moveBrush(w, player, brush, moved))
                v[0] = v[1] = v[2] = 0;
        }

        /// A box shoved by `delta` by something else that moves (a door, a
        /// lift, a train it stands on): as far as it can go. True if it
        /// went anywhere.
        inline bool boxShove(LogicWorld &w, PlayerState &player, int record, const fx *delta)
        {
            const int brush = w.records[record].brush;
            const bool crouched = w.records[record].extra[3] != 0;
            fx middle[3];
            boxMiddle(w, brush, crouched, middle);
            const fx to[3] = {middle[0] + delta[0], middle[1] + delta[1], middle[2] + delta[2]};
            const Trace tr = boxTrace(w, brush, crouched, middle, to);
            if (tr.startSolid || tr.allSolid ||
                (!boxFits(w, brush, crouched, tr.endPos) && boxFits(w, brush, crouched, middle)))
                return false;
            const fx *pos = w.brushStates[brush].pos;
            const fx moved[3] = {pos[0] + (tr.endPos[0] - middle[0]), pos[1] + (tr.endPos[1] - middle[1]),
                                 pos[2] + (tr.endPos[2] - middle[2])};
            if (moved[0] == pos[0] && moved[1] == pos[1] && moved[2] == pos[2])
                return false;
            w.states[record].bits = 0; // (its footing is looked at again)
            return moveBrush(w, player, brush, moved);
        }
    } // namespace detail

    // ------------------------------------------------------------------ the tram

    namespace detail
    {
        inline constexpr MoveState kCarried = MoveState::GoingDown; ///< a track train on a platform

        inline bool nodeOpen(const LogicWorld &w, std::uint16_t node, bool move)
        {
            if (node == kNoCorner)
                return false;
            return !move || w.cornerState == nullptr || !(w.cornerState[node] & kNodeOff);
        }

        inline void nodeSwitch(LogicWorld &w, std::uint16_t node, bool on)
        {
            if (node == kNoCorner || w.cornerState == nullptr)
                return;
            if (on)
                w.cornerState[node] &= static_cast<std::uint8_t>(~kNodeOff);
            else
                w.cornerState[node] |= kNodeOff;
        }

        /// The point `dist` further along the track from `p`, which is on the
        /// stretch that leaves `node`. Returns the node whose stretch that
        /// point is on, or kNoCorner if the track ends first (and `p` is then
        /// as far as it goes). `move` is whether a switched-off node ends the
        /// track: it does for where the train GOES, not for which way it
        /// looks. This is the game's CPathTrack::LookAhead, forwards.
        inline std::uint16_t trackAhead(const LogicWorld &w, std::uint16_t node, fx *p, fx dist,
                                        bool move)
        {
            const fx whole = dist;
            std::uint16_t at = node;
            for (int guard = 0; dist > 0 && guard < 256; ++guard)
            {
                const std::uint16_t next = w.corners[at].next;
                if (!nodeOpen(w, next, move))
                {
                    // The end of the track. For where the train LOOKS, the
                    // track is taken to run straight on from its last stretch
                    // -- or a train resting at the end would have no way to
                    // face.
                    for (int prev = 0; prev < w.cornerCount && !move; ++prev)
                    {
                        if (w.corners[prev].next != at)
                            continue;
                        const fx dir[3] = {w.corners[at].pos[0] - w.corners[prev].pos[0],
                                           w.corners[at].pos[1] - w.corners[prev].pos[1],
                                           w.corners[at].pos[2] - w.corners[prev].pos[2]};
                        const fx length = fxLength(dir);
                        for (int a = 0; a < 3 && length > 0; ++a)
                            p[a] += static_cast<fx>((static_cast<std::int64_t>(dir[a]) * dist) /
                                                    length);
                        break;
                    }
                    return kNoCorner;
                }
                const fx *to = w.corners[next].pos;
                const fx dir[3] = {to[0] - p[0], to[1] - p[1], to[2] - p[2]};
                const fx length = fxLength(dir);
                if (length == 0 && !nodeOpen(w, w.corners[next].next, move))
                    return dist == whole ? kNoCorner : at; // up against a dead end
                if (length > dist)
                {
                    for (int a = 0; a < 3; ++a)
                        p[a] += static_cast<fx>((static_cast<std::int64_t>(dir[a]) * dist) / length);
                    return at;
                }
                dist -= length;
                for (int a = 0; a < 3; ++a)
                    p[a] = to[a];
                at = next;
            }
            return at;
        }

        /// The node whose stretch leads INTO `node` (its path_track's
        /// "previous"), or kNoCorner.
        inline std::uint16_t trackPrevious(const LogicWorld &w, std::uint16_t node)
        {
            for (int c = 0; c < w.cornerCount; ++c)
            {
                if (w.corners[c].next == node && c != node)
                    return static_cast<std::uint16_t>(c);
            }
            return kNoCorner;
        }

        /// trackAhead backwards: the point `dist` further BACK along the
        /// track from `p`, which is on the stretch that leaves `node`.
        /// Returns the node whose stretch it is then on; or kNoCorner if the
        /// track's start comes first, `p` is left there, and `start` says
        /// which node that is.
        inline std::uint16_t trackBehind(const LogicWorld &w, std::uint16_t node, fx *p, fx dist, bool move,
                                         std::uint16_t &start)
        {
            std::uint16_t at = node;
            start = node;
            for (int guard = 0; dist > 0 && guard < 256; ++guard)
            {
                const fx *to = w.corners[at].pos;
                const fx dir[3] = {to[0] - p[0], to[1] - p[1], to[2] - p[2]};
                const fx length = fxLength(dir);
                if (length > dist)
                {
                    for (int a = 0; a < 3; ++a)
                        p[a] += static_cast<fx>((static_cast<std::int64_t>(dir[a]) * dist) / length);
                    return at;
                }
                dist -= length;
                for (int a = 0; a < 3; ++a)
                    p[a] = to[a];
                const std::uint16_t prev = trackPrevious(w, at);
                if (!nodeOpen(w, prev, move))
                {
                    start = at;
                    return kNoCorner;
                }
                at = prev;
            }
            return at;
        }

        /// Whether the player is IN a car: inside the box it was built as,
        /// taken in the car's own space, since it turns.
        inline bool playerAboard(const LogicWorld &w, const PlayerState &player, int index)
        {
            const Brush &built = w.brushes[index];
            fx local[3];
            toMoverSpace(w.movers[index], player.origin, local);
            for (int a = 0; a < 3; ++a)
            {
                if (local[a] < fxFromInt(built.mins[a]) - built.origin[a] ||
                    local[a] > fxFromInt(built.maxs[a]) - built.origin[a])
                    return false;
            }
            return true;
        }

        /// The player is inside what brush `index` is made of: out, by the
        /// shortest of a few ways tried in order -- up first, then sideways,
        /// a little further each time. What a rider is owed when the car
        /// they are in has moved onto them.
        /// Whether the player, where they are, is inside something other than
        /// brush `index`: the world, or another brush. (A trace from inside a
        /// brush passes out of it: the brushes are asked one by one.)
        inline bool playerStuckBesides(LogicWorld &w, const PlayerState &player, int index)
        {
            const bool was = w.movers[index].solid;
            w.movers[index].solid = false;
            moverReachSet(*w.world, index);
            const Trace here = traceAll(*w.world, player.ducked, player.origin, player.origin);
            w.movers[index].solid = was;
            moverReachSet(*w.world, index);
            if (here.startSolid)
                return true;
            for (int m = 0; m < w.brushCount; ++m)
            {
                if (m == index || !w.movers[m].solid || !w.brushStates[m].active)
                    continue;
                // (Only what stands still: what moves too -- a tram's doors,
                // which go after the tram itself each tick -- would hold up
                // what carries them.)
                const std::uint16_t logic = w.brushes[m].logic;
                if (logic != kNoLogic && logic < w.recordCount)
                {
                    const auto kind = static_cast<LogicKind>(w.records[logic].kind);
                    if (kind != LogicKind::Breakable && kind != LogicKind::WallToggle && kind != LogicKind::Pushable)
                        continue;
                }
                if (playerInsideBrush(w, player, m))
                    return true;
            }
            return false;
        }

        inline void pushOutOfBrush(LogicWorld &w, PlayerState &player, int index)
        {
            static constexpr std::int8_t kWays[6][3] = {{0, 0, 1},  {1, 0, 0}, {-1, 0, 0},
                                                        {0, 1, 0},  {0, -1, 0}, {0, 0, -1}};
            const fx from[3] = {player.origin[0], player.origin[1], player.origin[2]};
            for (fx reach = kFxOne / 8; reach <= fxFromInt(8); reach *= 2)
            {
                for (const auto &way : kWays)
                {
                    for (int a = 0; a < 3; ++a)
                        player.origin[a] = from[a] + way[a] * reach;
                    // (Out of it, and into nothing else.)
                    if (!playerInsideBrush(w, player, index) && !playerStuckBesides(w, player, index))
                        return;
                }
            }
            for (int a = 0; a < 3; ++a)
                player.origin[a] = from[a];
        }

        /// Put a brush at `pos` turned to `yaw`, taking whoever rides it
        /// along: moved with it, turned round its origin with it, and their
        /// view turned as far.
        ///
        /// A rider goes exactly where the brush goes. They are not traced
        /// through the world on the way (what is inside a car cannot meet
        /// anything the car does not), and where the brush does not turn
        /// they are moved by the very same amount, so that where they stand
        /// in it does not change by a hair however long the ride.
        inline bool carryBrush(LogicWorld &w, PlayerState &player, int index, const fx *pos,
                               std::uint16_t yaw)
        {
            const BrushState &state = w.brushStates[index];
            // Standing on it -- or, for a car one rides INSIDE, simply within
            // it: going down a shaft the floor drops away from the feet every
            // frame, and a rider who is "in the air" must not be left behind
            // or turned late.
            bool rider = player.onGround && player.groundMover == index;
            if (!rider && (w.brushes[index].flags & kBrushRoams) && w.movers[index].solid)
                rider = playerAboard(w, player, index);
            const auto turn = static_cast<std::int16_t>(yaw - state.angles[kYaw]);
            const fx stood[3] = {player.origin[0], player.origin[1], player.origin[2]};
            const fx moved[3] = {pos[0] - state.pos[0], pos[1] - state.pos[1], pos[2] - state.pos[2]};
            // (Only what the move itself puts them into: one already a hair
            // into a wall where they stand is not held up by it.)
            // (Not a car one rides inside, turned with it round its own
            // origin: its walls are its own to hold them.)
            const bool checked = rider && w.movers[index].solid && !(w.brushes[index].flags & kBrushRoams);
            const bool stuckBefore = checked && playerStuckBesides(w, player, index);
            if (rider)
            {
                fx x = player.origin[0] - state.pos[0];
                fx y = player.origin[1] - state.pos[1];
                if (turn != 0)
                {
                    std::int32_t sine, cosine;
                    fxSinCosFine(static_cast<std::uint16_t>(turn), sine, cosine);
                    const fx tx = static_cast<fx>((static_cast<std::int64_t>(x) * cosine -
                                                   static_cast<std::int64_t>(y) * sine) >>
                                                  kNormalShift);
                    const fx ty = static_cast<fx>((static_cast<std::int64_t>(x) * sine +
                                                   static_cast<std::int64_t>(y) * cosine) >>
                                                  kNormalShift);
                    x = tx;
                    y = ty;
                    w.viewTurn += turn;
                }
                player.origin[0] = pos[0] + x;
                player.origin[1] = pos[1] + y;
                player.origin[2] += pos[2] - state.pos[2];
            }
            const std::uint16_t angles[3] = {0, yaw, 0};
            logicPlaceBrushTurned(w, index, pos, angles);
            if (checked && !stuckBefore)
            {
                // Carried into something (a lift into a ceiling): it cannot
                // take them there, and is blocked by them (SV_PushMove).
                if (playerStuckBesides(w, player, index))
                {
                    for (int a = 0; a < 3; ++a)
                        player.origin[a] = stood[a];
                    if (turn != 0)
                        w.viewTurn -= turn;
                    return true;
                }
            }
            if (w.movers[index].solid && playerInsideBrush(w, player, index))
            {
                // In its way: shoved as far as it went (SV_PushMove), however
                // fast that is -- and failing that (what it turned into
                // them), nudged out.
                const fx there[3] = {player.origin[0], player.origin[1], player.origin[2]};
                pushPlayer(w, player, index, moved);
                if (playerInsideBrush(w, player, index))
                {
                    for (int a = 0; a < 3; ++a)
                        player.origin[a] = there[a];
                    pushOutOfBrush(w, player, index);
                }
                // Nowhere to put them: it is blocked by them.
                return playerInsideBrush(w, player, index);
            }
            return false;
        }

        /// Back where it was (a blocked pusher does not move: SV_PushMove).
        inline void brushPutBack(LogicWorld &w, int index, const fx *pos, std::uint16_t yaw)
        {
            const std::uint16_t angles[3] = {0, yaw, 0};
            logicPlaceBrushTurned(w, index, pos, angles);
        }

        /// The player, in the way of what moves or touching what hurts, is
        /// shoved off it at `speed` (Q12 units a second), away from its
        /// middle.
        inline void shoveFromBrush(LogicWorld &w, PlayerState &player, int index, fx speed)
        {
            const MoverHull &hull = w.movers[index];
            const fx d[3] = {player.origin[0] - (hull.mins[0] + hull.maxs[0]) / 2,
                             player.origin[1] - (hull.mins[1] + hull.maxs[1]) / 2,
                             player.origin[2] - (hull.mins[2] + hull.maxs[2]) / 2};
            const fx length = fxLength(d);
            for (int a = 0; a < 3 && length > 0; ++a)
                player.velocity[a] = static_cast<fx>((static_cast<std::int64_t>(d[a]) * speed) / length);
        }

        /// Which way a track train at `pos` on the stretch leaving `node`
        /// should face. False if the track gives no direction there.
        inline bool trackFacing(const LogicWorld &w, const LogicRecord &record, std::uint16_t node,
                                const fx *pos, std::uint16_t &yaw)
        {
            const fx height = static_cast<fx>(record.extra[1]);
            fx wheels = static_cast<fx>(record.extra[0]);
            if (wheels <= 0)
                wheels = fxFromInt(100);
            fx front[3] = {pos[0], pos[1], pos[2] - height};
            trackAhead(w, node, front, wheels, false);
            const fx dx = front[0] - pos[0];
            const fx dy = front[1] - pos[1];
            if (dx == 0 && dy == 0)
                return false;
            // "The train actually points west": its front is its -X.
            yaw = static_cast<std::uint16_t>(fxAtan2Turns(dy, dx) + 32768);
            return true;
        }

        /// A track train's LogicState::phase: the player has its controls.
        inline constexpr std::int32_t kTrainDriven = 1;
        /// ... and, from bit 4, what the display shows of how it goes (the
        /// player's m_iTrain): set as they take the controls and at each
        /// press, not as the train goes -- one at a dead end still shows the
        /// speed it was asked for.
        inline constexpr int kTrainShownShift = 4;
        inline constexpr std::int32_t kTrainShownMask = 7 << kTrainShownShift;

        /// TrainSpeed (player.cpp): which of the display's pictures `speed`
        /// of `whole` is -- the game's frames of sprites/640_train.spr: 0
        /// stopped, 1 to 3 slow to fast, 4 backwards.
        inline int trainPicture(fx speed, fx whole)
        {
            if (speed < 0)
                return 4;
            if (speed == 0)
                return 0;
            const std::int64_t hundredths = (static_cast<std::int64_t>(speed) * 100) / (whole > 0 ? whole : 1);
            return hundredths < 33 ? 1 : (hundredths < 66 ? 2 : 3);
        }
        inline void trainShow(LogicWorld &w, int index)
        {
            LogicState &state = w.states[index];
            state.phase = (state.phase & ~kTrainShownMask) |
                          (trainPicture(state.speed, w.records[index].speed) << kTrainShownShift);
        }

        /// It sets off: its start, as loud as its engine (UpdateSound,
        /// ttrain_start1; the engine itself is Sound.arm.cpp's loop).
        inline void trainStarts(LogicWorld &w, int index)
        {
            const LogicRecord &record = w.records[index];
            if (w.heard == nullptr || record.brush == kNoBrush)
                return;
            const MoverHull &hull = w.movers[record.brush];
            const fx at[3] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2,
                              (hull.mins[2] + hull.maxs[2]) / 2};
            w.heard->add(CommonSound::TrainStart, at, moverVolume(record), kRadiusLarge);
        }

        /// It has come to a stop: its brakes (its engine is the sound loop's
        /// to stop: Sound.arm.cpp keeps it going for as long as it moves).
        inline void trainBrakes(LogicWorld &w, int index)
        {
            const LogicRecord &record = w.records[index];
            if (w.heard == nullptr || w.sounds == nullptr || w.sounds[index].stop == kNoSound ||
                record.brush == kNoBrush)
                return;
            const MoverHull &hull = w.movers[record.brush];
            const fx at[3] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2,
                              (hull.mins[2] + hull.maxs[2]) / 2};
            w.heard->add(w.sounds[index].stop, at, moverVolume(record), kRadiusLarge);
        }

        /// CFuncTrackTrain::Blocked: one who rides it is lifted (as much as
        /// its speed, up to 50); anyone else is shoved off at its "dmg", and
        /// crushed by as much.
        inline void trackTrainBlocked(LogicWorld &w, PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            const LogicState &state = w.states[index];
            if (player.onGround && player.groundMover == static_cast<int>(record.brush))
            {
                fx lift = state.speed < 0 ? -state.speed : state.speed;
                if (lift > fxFromInt(50))
                    lift = fxFromInt(50);
                if (player.velocity[2] == 0)
                    player.velocity[2] += lift;
                return;
            }
            shoveFromBrush(w, player, record.brush, fxFromInt(record.damage));
            if (record.damage > 0)
                logicHurt(player, record.damage, 0, kDmgCrush);
        }

        inline void trackTrainTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            const auto node = static_cast<std::uint16_t>(state.bits);
            if (record.brush == kNoBrush || state.move == kCarried || state.speed == 0 ||
                node == kNoCorner)
                return;
            const fx height = static_cast<fx>(record.extra[1]);
            const BrushState &brush = w.brushStates[record.brush];
            const fx pos[3] = {brush.pos[0], brush.pos[1], brush.pos[2]};

            // Where it will be a tenth of a second on, along the track: it
            // heads for that, covering this tick's share of the tenth. (Driven
            // backwards, back along it.)
            const bool backwards = state.speed < 0;
            fx ahead[3] = {pos[0], pos[1], pos[2] - height};
            std::uint16_t start = node;
            const std::uint16_t next = backwards ? trackBehind(w, node, ahead, -state.speed / 10, true, start)
                                                 : trackAhead(w, node, ahead, state.speed / 10, true);
            ahead[2] += height;

            if (next == kNoCorner)
            {
                // The track ends. On to the end of it at the speed it had,
                // and there it stops and says so.
                const fx left[3] = {ahead[0] - pos[0], ahead[1] - pos[1], ahead[2] - pos[2]};
                const fx distance = fxLength(left);
                const fx step = static_cast<fx>(
                    (static_cast<std::int64_t>(backwards ? -state.speed : state.speed) * ms) / 1000);
                if (distance > step && step > 0)
                {
                    fx to[3];
                    for (int a = 0; a < 3; ++a)
                        to[a] = pos[a] + static_cast<fx>((static_cast<std::int64_t>(left[a]) * step) /
                                                         distance);
                    if (carryBrush(w, player, record.brush, to, brush.angles[kYaw]))
                    {
                        brushPutBack(w, record.brush, pos, brush.angles[kYaw]);
                        trackTrainBlocked(w, player, index);
                    }
                    return;
                }
                if (carryBrush(w, player, record.brush, ahead, brush.angles[kYaw]))
                {
                    brushPutBack(w, record.brush, pos, brush.angles[kYaw]);
                    trackTrainBlocked(w, player, index);
                    return;
                }
                state.speed = 0;
                trainBrakes(w, index);
                std::uint16_t last = node;
                for (int guard = 0; guard < 256 && !backwards && nodeOpen(w, w.corners[last].next, true); ++guard)
                    last = w.corners[last].next;
                if (backwards)
                    last = start;
                state.bits = last;
                logicFire(w, player, w.corners[last].netName, index, kUseToggle, 0);
                return;
            }

            std::uint16_t yaw = brush.angles[kYaw];
            std::uint16_t want = yaw;
            // Ten times the error a second, as the game turns it.
            const std::int32_t share = ms >= 100 ? 1000 : ms * 10; // thousandths of the way
            if (trackFacing(w, record, node, pos, want))
            {
                const auto error = static_cast<std::int16_t>(want - yaw);
                yaw = static_cast<std::uint16_t>(yaw + (error * share) / 1000);
            }
            fx to[3];
            for (int a = 0; a < 3; ++a)
                to[a] = pos[a] + static_cast<fx>(
                                     (static_cast<std::int64_t>(ahead[a] - pos[a]) * share) / 1000);
            if (carryBrush(w, player, record.brush, to, yaw))
            {
                brushPutBack(w, record.brush, pos, brush.angles[kYaw]);
                trackTrainBlocked(w, player, index);
                return;
            }

            if (next != node)
            {
                // Onto the next stretch: its node has things to say -- going
                // backwards, the node it has just gone back past.
                state.bits = next;
                const std::uint16_t said = backwards ? node : next;
                const PathCorner &passed = w.corners[said];
                const bool spent = w.cornerState != nullptr && (w.cornerState[said] & kNodeSpent);
                if (passed.flags & kCornerDisableTrain)
                {
                    state.running = true; // from here on the track drives it
                    state.phase &= ~kTrainDriven;
                }
                if (((record.flags & kLogicSelfDriven) || state.running) && passed.speed != 0)
                {
                    if (state.speed == 0)
                        trainStarts(w, index);
                    state.speed = passed.speed;
                }
                if (passed.fireTarget != 0 && !spent)
                {
                    if ((passed.flags & kCornerFireOnce) && w.cornerState != nullptr)
                        w.cornerState[said] |= kNodeSpent;
                    logicFire(w, player, passed.fireTarget, index, kUseToggle, 0);
                }
            }
        }

        /// The track node nearest `pos` (the train's origin, so `height` above
        /// the track), within 1024 units; kNoCorner if there is none.
        inline std::uint16_t trackNearest(const LogicWorld &w, const fx *pos, fx height)
        {
            std::uint16_t best = kNoCorner;
            fx bestDistance = fxFromInt(1024);
            for (int i = 0; i < w.cornerCount; ++i)
            {
                const fx d[3] = {w.corners[i].pos[0] - pos[0], w.corners[i].pos[1] - pos[1],
                                 w.corners[i].pos[2] + height - pos[2]};
                // Far off on any one axis is far off: and keeps the length's
                // squares inside 64 bits' comfortable half.
                if (d[0] > bestDistance || d[0] < -bestDistance || d[1] > bestDistance ||
                    d[1] < -bestDistance || d[2] > bestDistance || d[2] < -bestDistance)
                    continue;
                const fx distance = fxLength(d);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = static_cast<std::uint16_t>(i);
                }
            }
            return best;
        }

        inline int trackChangeTrain(const LogicWorld &w, const LogicRecord &record)
        {
            for (int i = 0; i < w.recordCount && record.extra[3] != 0; ++i)
            {
                if (w.records[i].name == record.extra[3] &&
                    w.records[i].kind == static_cast<std::uint8_t>(LogicKind::TrackTrain))
                    return i;
            }
            return -1;
        }

        inline void trackChangeTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (state.move != MoveState::GoingUp || record.brush == kNoBrush)
                return;
            const fx distance = fxLength(state.travel);
            std::int32_t step = kFractionOne;
            if (distance > 0 && record.speed > 0)
            {
                const std::int64_t travelMs =
                    (static_cast<std::int64_t>(distance) * 1000) / record.speed;
                if (travelMs > ms)
                    step = static_cast<std::int32_t>(
                        (static_cast<std::int64_t>(ms) << kFractionShift) / travelMs);
            }
            std::int32_t phase = state.phase + step;
            if (phase > kFractionOne || phase < state.phase)
                phase = kFractionOne;
            state.phase = phase;

            fx pos[3];
            for (int a = 0; a < 3; ++a)
                pos[a] = state.base[a] +
                         static_cast<fx>((static_cast<std::int64_t>(state.travel[a]) * phase) >>
                                         kFractionShift);
            const auto turned = static_cast<std::int32_t>(
                (static_cast<std::int64_t>(static_cast<std::int32_t>(record.extra[0])) * phase) >>
                kFractionShift);
            carryBrush(w, player, record.brush, pos, static_cast<std::uint16_t>(turned));

            // Its train goes with it: as far from the platform's middle as it
            // was, turned round it as the platform turns.
            const int train = static_cast<int>(state.bits);
            if (train < w.recordCount && w.records[train].brush != kNoBrush)
            {
                LogicState &riding = w.states[train];
                fx sine, cosine;
                fxSinCos(static_cast<std::uint16_t>(turned), sine, cosine);
                const fx to[3] = {
                    pos[0] + fxMul(riding.base[0], cosine) - fxMul(riding.base[1], sine),
                    pos[1] + fxMul(riding.base[0], sine) + fxMul(riding.base[1], cosine),
                    pos[2] + riding.base[2]};
                carryBrush(w, player, w.records[train].brush, to,
                           static_cast<std::uint16_t>(riding.travel[0] + turned));
                if (phase == kFractionOne)
                {
                    // Down: onto the lower track, and away.
                    riding.move = MoveState::AtBottom;
                    riding.bits = record.extra[2];
                    riding.speed = w.records[train].speed;
                }
            }
            if (phase == kFractionOne)
            {
                state.move = MoveState::AtTop;
                if (record.extra[1] != kNoCorner)
                    nodeSwitch(w, w.corners[record.extra[1]].next, false);
                if (record.extra[2] != kNoCorner)
                    nodeSwitch(w, w.corners[record.extra[2]].next, true);
            }
        }
    } // namespace detail

    // ------------------------------------------------------------------ scripts

    namespace detail
    {
        // What a script is up to (LogicState::phase).
        inline constexpr std::int32_t kScriptIdle = 0;    ///< has no actor
        inline constexpr std::int32_t kScriptHolding = 1; ///< has one, waiting to be told
        inline constexpr std::int32_t kScriptMoving = 2;
        inline constexpr std::int32_t kScriptPlaying = 3;

        // How hard a script may take its actor (SS_INTERRUPT_*): one that
        // is idle; one it names, alert as well; an aiscripted_sequence's.
        inline constexpr int kInterruptIdle = 0;
        inline constexpr int kInterruptByName = 1;
        inline constexpr int kInterruptAi = 2;

        /// Whether a script may have actor `actor` now (CanPlaySequence: its
        /// state, as its mind has it -- HlAiPeople.hpp).
        inline bool actorCanPlaySequence(LogicWorld &w, int actor, bool overrideState, int interruptLevel);
        /// A monster's mind, when its script lets it go: idle, or (an
        /// aiscripted_sequence's, `ambush`) lying in wait -- HlMonsters.hpp.
        inline void scriptRouse(LogicWorld &w, int actorIndex, bool ambush);

        // A script's own (LogicState), besides `phase`, `bits` (its actor)
        // and `travel` (the walk's tries and time): base[0] is the game's
        // m_iDelay -- how many of the scripts of its name have an actor
        // still on the way to its mark; base[1] that they have all got
        // there, which starts it; base[2] that it may not be broken off now
        // (event 1001).
        inline bool scriptIsAi(const LogicRecord &record) { return (record.pad & kScriptAi) != 0; }

        /// DelayStart: each scripted_sequence of the script's name (and the
        /// script itself, with none) has one more on its way (`more`), or
        /// one fewer -- none left, and they all start.
        inline void scriptDelayStart(LogicWorld &w, int index, bool more)
        {
            const std::uint16_t name = w.records[index].name;
            for (int i = 0; i < w.recordCount; ++i)
            {
                const LogicRecord &other = w.records[i];
                if (other.kind != static_cast<std::uint8_t>(LogicKind::Script) || scriptIsAi(other) ||
                    (i != index && (name == 0 || other.name != name)))
                    continue;
                LogicState &state = w.states[i];
                if (more)
                {
                    ++state.base[0];
                }
                else if (--state.base[0] <= 0)
                {
                    state.base[0] = 0;
                    state.base[1] = 1;
                }
            }
        }

        /// The actor a script is for: by name, or the nearest free one of a
        /// kind within its radius -- one that may be taken now. -1 if there
        /// is none it can have.
        inline int scriptFindActor(LogicWorld &w, int index)
        {
            if (w.actors == nullptr)
                return -1;
            const LogicRecord &record = w.records[index];
            const bool overrideState = (record.pad & kScriptOverrideState) != 0 || scriptIsAi(record);
            int best = -1;
            fx bestDistance = fxFromInt(record.waitMs > 0 ? record.waitMs : 512);
            for (int i = 0; i < w.actors->count; ++i)
            {
                const ActorState &actor = w.actors->states[i];
                // (Nor one of the people who has been killed: the dead
                // play no scenes. 5 and up: HlMonsters.hpp, kAiDying.)
                if (!actor.active || (actor.script >= 0 && actor.script != index) ||
                    ((actor.kind == static_cast<std::uint8_t>(ActorKind::Scientist) ||
                      actor.kind == static_cast<std::uint8_t>(ActorKind::Guard) ||
                      actor.kind == static_cast<std::uint8_t>(ActorKind::Sitter)) &&
                     actor.ai >= 5))
                    continue;
                if (record.extra[0] != 0)
                {
                    if (actor.name == record.extra[0] &&
                        actorCanPlaySequence(w, i, overrideState, scriptIsAi(record) ? kInterruptAi : kInterruptByName))
                        return i;
                    continue;
                }
                if (record.damage == 0 || actor.kind != record.damage - 1 ||
                    !actorCanPlaySequence(w, i, overrideState, scriptIsAi(record) ? kInterruptAi : kInterruptIdle))
                    continue;
                const fx d[3] = {actor.pos[0] - record.move[0], actor.pos[1] - record.move[1],
                                 actor.pos[2] - record.move[2]};
                if (d[0] > bestDistance || d[0] < -bestDistance || d[1] > bestDistance ||
                    d[1] < -bestDistance || d[2] > bestDistance || d[2] < -bestDistance)
                    continue;
                const fx distance = fxLength(d);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = i;
                }
            }
            return best;
        }

        /// Whether anyone the script could take is in the map at all, free
        /// or not: of its name, or of its class. (Not one of the people who
        /// has been killed: he will never be free -- and what waits on him
        /// is let go on, as when there is nobody.)
        inline bool scriptActorHere(const LogicWorld &w, int index)
        {
            const LogicRecord &record = w.records[index];
            for (int i = 0; i < w.actors->count; ++i)
            {
                const ActorState &actor = w.actors->states[i];
                const bool person = actor.kind == static_cast<std::uint8_t>(ActorKind::Scientist) ||
                                    actor.kind == static_cast<std::uint8_t>(ActorKind::Guard) ||
                                    actor.kind == static_cast<std::uint8_t>(ActorKind::Sitter);
                if (!actor.active || (person && actor.ai >= 5))
                    continue;
                if (record.extra[0] != 0 ? actor.name == record.extra[0]
                                         : (record.damage != 0 && actor.kind == record.damage - 1))
                    return true;
            }
            return false;
        }

        inline void scriptDone(LogicWorld &w, PlayerState &player, int index);

        /// The actor is on its mark, facing as the script says: act it out.
        inline void scriptPlay(LogicWorld &w, PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            ActorState &actor = w.actors->states[state.bits];
            state.phase = kScriptPlaying;
            actor.scriptPhase = kActorPlaying;
            // No clip to play (the script was only a walk), or one this map's
            // model was not given: it is over as it begins.
            if (!actorPlay(*w.actors, actor, record.extra[2], true))
                scriptDone(w, player, index);
        }

        /// The actor has got to its mark. A script that has been told to
        /// start (or never needs telling) plays; one that is still waiting
        /// has its actor wait there, in the idle clip if it has one.
        inline void scriptArrived(LogicWorld &w, PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            ActorState &actor = w.actors->states[state.bits];
            // One with an idle clip waits to be told even if it has no name
            // (nothing can tell it, then: that is how the map sits someone
            // down for good). And none plays while another of its name has
            // an actor still on the way (DelayStart): all of them at once.
            const bool waits = (record.flags & kLogicUseOnly) || record.extra[1] != 0;
            if ((state.running || state.base[1] != 0 || !waits) && state.base[0] <= 0)
            {
                scriptPlay(w, player, index);
                return;
            }
            state.phase = kScriptHolding;
            actor.scriptPhase = kActorHeld;
            // (An aiscripted_sequence has no idle clip of its own.)
            if (!scriptIsAi(record) && actorPlay(*w.actors, actor, record.extra[1], false))
            {
                // Waiting on the first frame of what it will do, if that is
                // what its idle is.
                actor.frozen = record.extra[1] == record.extra[2];
            }
            else
            {
                actorRest(actor);
            }
        }

        /// Get the actor to the mark the way the script says. The game does
        /// this the moment a script takes its actor -- which for a script
        /// with an idle clip is as the map starts, long before it is told to
        /// play: the scientist is already at the console when the player
        /// comes in.
        inline void scriptApproach(LogicWorld &w, PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            ActorState &actor = w.actors->states[state.bits];
            const auto yaw = static_cast<std::uint16_t>(record.speed);
            switch (record.extra[3])
            {
            case kScriptMoveWalk:
            case kScriptMoveRun:
            {
                const bool run = record.extra[3] == kScriptMoveRun;
                const std::uint32_t clip = nameHash(run ? "run" : "walk");
                actor.speed = actorClipSpeed(*w.actors, actor, clip, fxFromInt(run ? 270 : 60));
                for (int a = 0; a < 3; ++a)
                    actor.target[a] = record.move[a];
                const fx d[3] = {actor.target[0] - actor.pos[0], actor.target[1] - actor.pos[1], 0};
                const fx distance = fxLength(d);
                // Already there (to within a step): no walk to make.
                if (distance < fxFromInt(8))
                {
                    actor.yaw = yaw;
                    break;
                }
                // There is no finding a way round things: it walks straight
                // at the mark, and if it has not got there in half as long
                // again as the walk should take (and two seconds), it is put
                // there.
                actor.moveMs = static_cast<std::int32_t>(
                                   (static_cast<std::int64_t>(distance) * 3000) / actor.speed) +
                               4000;
                actor.thinkMs = 0;
                actor.stuckMs = 0;
                actor.hasGoal = false;
                actor.goalNode = actor.fromNode = -1;
                state.travel[1] = 0;
                actorPlay(*w.actors, actor, clip, false);
                actor.scriptPhase = kActorMoving;
                state.phase = kScriptMoving;
                if (!scriptIsAi(record))
                    scriptDelayStart(w, index, true);
                return;
            }
            case kScriptMoveInstant:
                for (int a = 0; a < 3; ++a)
                    actor.pos[a] = record.move[a];
                // (A mark is often drawn a little above the floor it means.
                // Not for the seated and the dead, who are put where they
                // are meant. What never walks -- a monster_generic, the
                // halves of c1a1's guard the laser cuts -- is put on the
                // floor under the mark itself, as the game puts it there: a
                // person's box by a wall would rest on the ledge beside it.)
                if (actorCentred(actor))
                    actorToFloorAs(*w.world, actor, fxFromInt(18), fxFromInt(36));
                else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Generic) ||
                         actor.kind == static_cast<std::uint8_t>(ActorKind::Furniture) ||
                         actor.kind == static_cast<std::uint8_t>(ActorKind::Cycler))
                    pointToFloor(*w.world, actor.pos, fxFromInt(36));
                else if (actor.kind != static_cast<std::uint8_t>(ActorKind::Pose) &&
                         actor.kind != static_cast<std::uint8_t>(ActorKind::Sitter))
                    actorToFloor(*w.world, actor, fxFromInt(18), fxFromInt(36));
                actor.yaw = yaw;
                break;
            case kScriptMoveTurn:
                actor.yaw = yaw;
                break;
            default:
                break;
            }
            scriptArrived(w, player, index);
        }

        /// Take the script's actor, if it can be had, and set it on its way.
        inline bool scriptPossess(LogicWorld &w, PlayerState &player, int index)
        {
            LogicState &state = w.states[index];
            const int found = scriptFindActor(w, index);
            if (found < 0)
                return false;
            ActorState &actor = w.actors->states[found];
            actor.script = static_cast<std::int16_t>(index);
            actor.scriptPhase = kActorHeld;
            state.bits = static_cast<std::uint32_t>(found);
            state.base[2] = 0; // (interruptible, until its clip says not)
            scriptApproach(w, player, index);
            return true;
        }


        inline void scriptDone(LogicWorld &w, PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            ActorState &actor = w.actors->states[state.bits];
            // What acting it out has done (GoldSrc's CineCleanup): a clip
            // with the "dead" event leaves a body, as it lies on the last
            // frame -- where the clip has it, which may be far from where it
            // stands (a scientist's fall down a shaft). Else the actor is
            // where the clip's root ended, across (unless that is no way at
            // all), then let down to a floor within 256 units; with none, as
            // high as the clip ended, and falling.
            const ModelView *model = actorModel(*w.actors, actor);
            const bool played = model != nullptr && actor.clip >= 0 && actor.scriptPhase == kActorPlaying;
            const bool dies = played && (model->clips[actor.clip].flags & kClipDies) != 0;
            fx endUp = 0;
            const fx was[3] = {actor.pos[0], actor.pos[1], actor.pos[2]};
            if (played && !dies && !(record.pad & kScriptNoScriptMovement))
            {
                const ModelClip &clip = model->clips[actor.clip];
                fx sine, cosine;
                fxSinCos(actor.yaw, sine, cosine);
                const fx dx = fxMul(clip.endOffset[0], cosine) - fxMul(clip.endOffset[1], sine);
                const fx dy = fxMul(clip.endOffset[0], sine) + fxMul(clip.endOffset[1], cosine);
                const fx d[3] = {dx, dy, 0};
                if (fxLength(d) >= fxFromInt(8))
                {
                    actor.pos[0] += dx;
                    actor.pos[1] += dy;
                }
                endUp = fxFromInt(clip.endUp);
            }
            if (dies)
            {
                actor.frozen = true;
                actor.ai = kAiDead;
                actor.flags &= static_cast<std::uint8_t>(~kActorSolid);
                actor.velocity[0] = actor.velocity[1] = actor.velocity[2] = 0;
                // (The body stays only if the map says so; else it fades.)
                if (!(record.pad & kScriptLeaveCorpse))
                    corpseFadeStart(actor);
            }
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Furniture) ||
                     actor.kind == static_cast<std::uint8_t>(ActorKind::Pose))
            {
                // A prop stays as the script left it.
                actor.frozen = true;
            }
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Sitter))
            {
                actorRest(actor); // (in his chair: not let down to the floor)
            }
            else if (actorCentred(actor))
            {
                actorRest(actor);
                actorToFloorAs(*w.world, actor, fxFromInt(18), fxFromInt(256));
            }
            else
            {
                actorRest(actor);
                const fx from[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36 + 18)};
                const fx to[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36 - 256)};
                const Trace tr = traceHull(w.world->standing, from, to);
                if (tr.startSolid || tr.allSolid)
                {
                    // (In something: back where the clip began.)
                    for (int k = 0; k < 3; ++k)
                        actor.pos[k] = was[k];
                }
                else if (tr.fraction < kFractionOne)
                {
                    actor.pos[2] = tr.endPos[2] - fxFromInt(36);
                }
                else if (played)
                {
                    // Nothing under them: where the clip left them, and down.
                    actor.pos[2] += endUp;
                    actor.airborne = true;
                    actor.velocity[0] = actor.velocity[1] = actor.velocity[2] = 0;
                    actor.groundMover = -1;
                }
            }
            actor.script = -1;
            actor.scriptPhase = kActorFree;
            // Its mind takes it back: idle, or (an aiscripted_sequence that
            // says so) lying in wait.
            if (!dies)
                scriptRouse(w, static_cast<int>(state.bits), scriptIsAi(record) && (record.pad & kScriptAmbush));
            state.phase = kScriptIdle;
            state.running = false;
            state.base[1] = 0;
            if (!(record.flags & kLogicToggle))
                state.dead = true;
            logicUseTargets(w, player, index, kUseToggle, 0);
        }

        inline void scriptTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (w.actors == nullptr)
                return;
            // Waiting for its actor to be free (or to exist): asked for again
            // every second.
            if (state.phase == kScriptIdle)
            {
                if (!state.running && (!(record.flags & kLogicStartOpen) || state.hurt != 0))
                    return;
                state.timer -= ms;
                if (state.timer > 0)
                    return;
                state.timer = 1000;
                if (!scriptPossess(w, player, index))
                {
                    // Nobody to act it out -- an actor this port does not
                    // have yet, or one that is not in this map. After a few
                    // tries the script is taken as played, so that the door
                    // that waits on it still opens. But one who IS here and
                    // only busy (in another scene, not free to be taken) is
                    // waited for, as the game waits: c0a0e's tram door opens
                    // when its guard has pushed its button, not three seconds
                    // after his scene first asked for him.
                    if (scriptActorHere(w, index))
                        state.travel[0] = 0;
                    else if (++state.travel[0] >= 3)
                    {
                        state.dead = !(record.flags & kLogicToggle);
                        state.running = false;
                        state.travel[0] = 0;
                        logicUseTargets(w, player, index, kUseToggle, 0);
                    }
                    return;
                }
                state.travel[0] = 0;
                state.timer = -1;
                state.hurt = 1;
                return;
            }
            ActorState &actor = w.actors->states[state.bits];
            // Held, and the last of its name has got to its mark: it plays.
            if (state.phase == kScriptHolding && state.base[1] != 0 && state.base[0] <= 0)
            {
                scriptPlay(w, player, index);
                return;
            }
            if (state.phase == kScriptMoving)
            {
                const fx d[3] = {actor.target[0] - actor.pos[0], actor.target[1] - actor.pos[1], 0};
                const fx distance = fxLength(d);
                const fx step = static_cast<fx>((static_cast<std::int64_t>(actor.speed) * ms) / 1000);
                actor.moveMs -= ms;
                // (The walk itself stops short by a few units: near enough.)
                if (distance <= step || distance < fxFromInt(6) || actor.moveMs <= 0 || step <= 0)
                {
                    for (int a = 0; a < 3; ++a)
                        actor.pos[a] = actor.target[a];
                    // A mark is often drawn a little above the floor it
                    // means.
                    actorToFloorAs(*w.world, actor, fxFromInt(18), fxFromInt(36));
                    actor.yaw = static_cast<std::uint16_t>(record.speed);
                    if (!scriptIsAi(record))
                        scriptDelayStart(w, index, false);
                    scriptArrived(w, player, index);
                    return;
                }
                // It walks there as anything with legs does: round what is
                // in the way, by the path nodes if not straight (HlWalk.hpp).
                const int went = walkTick(*w.actors, *w.world, static_cast<int>(state.bits), false,
                                          actor.target, actor.speed, ms, &player, fxFromInt(16),
                                          fxFromInt(72), true);
                if (went == kWalkMoved)
                    return;
                // Held up, or no way known. state.travel[1] counts how long.
                state.travel[1] += ms;
                if (distance > fxFromInt(48))
                {
                    // Three seconds of that and it is simply put on its
                    // mark: better to appear there than to go through a
                    // wall to it, and what waits on the script must not wait
                    // for ever.
                    if (state.travel[1] >= 3000)
                        actor.moveMs = 0;
                    return;
                }
                // A mark is often hard against something (the guard's is at
                // the tram's door; a scientist's at the desk he leans on),
                // and then the last of the way is not a walk the rules
                // allow: those few steps are taken straight.
                actor.yaw = fxAtan2Turns(d[1], d[0]);
                actor.pos[0] += static_cast<fx>((static_cast<std::int64_t>(d[0]) * step) / distance);
                actor.pos[1] += static_cast<fx>((static_cast<std::int64_t>(d[1]) * step) / distance);
                if (!actorToFloorAs(*w.world, actor, fxFromInt(18), fxFromInt(36)))
                    actor.pos[2] += static_cast<fx>(
                        (static_cast<std::int64_t>(actor.target[2] - actor.pos[2]) * step) / distance);
                return;
            }
            if (state.phase == kScriptPlaying && actor.finished)
                scriptDone(w, player, index);
        }

        /// CanInterrupt: the map lets it be broken off, its clip has not
        /// said otherwise, and its actor is alive.
        inline bool scriptCanInterrupt(const LogicWorld &w, int index)
        {
            if (index < 0 || index >= w.recordCount ||
                w.records[index].kind != static_cast<std::uint8_t>(LogicKind::Script))
                return false;
            const LogicState &state = w.states[index];
            if ((w.records[index].pad & kScriptNoInterrupt) || state.base[2] != 0)
                return false;
            if (w.actors != nullptr && state.phase != kScriptIdle && state.bits < static_cast<std::uint32_t>(w.actors->count))
                return w.actors->states[state.bits].ai < 5; // (kAiDying: HlActors.hpp)
            return true;
        }

        /// ScriptEntityCancel: a scripted_sequence (not an aiscripted one)
        /// lets its actor go where it stands, its scene unplayed; the
        /// script itself is ready again.
        inline void scriptEntityCancel(LogicWorld &w, int index)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (scriptIsAi(record) || state.phase == kScriptIdle || w.actors == nullptr ||
                state.bits >= static_cast<std::uint32_t>(w.actors->count))
                return;
            ActorState &actor = w.actors->states[state.bits];
            if (actor.script == index)
            {
                if (actor.scriptPhase == kActorMoving)
                    scriptDelayStart(w, index, false);
                // CineCleanup, for a scene that has a clip to play: the
                // actor is put where its body is in the clip it is in (its
                // root bone; a sitter's is in front of the wall he leans
                // on), unless that is less than 8 away, and let down to the
                // floor -- back where it was if that is in something.
                const ModelView *model = actorModel(*w.actors, actor);
                if (record.extra[1] != 0 && !(record.pad & kScriptNoScriptMovement) && model != nullptr &&
                    actor.clip >= 0 && actor.ai < 5 && w.world != nullptr)
                {
                    static BoneMatrix bones[kModelMaxBones];
                    poseModel(*model, actor.clip, actor.frame, bones, nullptr, actor.blend.value);
                    fx sine, cosine;
                    fxSinCos(actor.yaw, sine, cosine);
                    const fx root[2] = {actor.pos[0] + fxMul(bones[0].t[0], cosine) - fxMul(bones[0].t[1], sine),
                                        actor.pos[1] + fxMul(bones[0].t[0], sine) + fxMul(bones[0].t[1], cosine)};
                    const fx d[3] = {root[0] - actor.pos[0], root[1] - actor.pos[1], 0};
                    if (fxLength(d) >= fxFromInt(8))
                    {
                        const fx was[3] = {actor.pos[0], actor.pos[1], actor.pos[2]};
                        actor.pos[0] = root[0];
                        actor.pos[1] = root[1];
                        const fx from[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36 + 1)};
                        const fx to[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36 - 256)};
                        const Trace tr = traceHull(w.world->standing, from, to);
                        if (tr.startSolid || tr.allSolid)
                        {
                            for (int k = 0; k < 3; ++k)
                                actor.pos[k] = was[k];
                        }
                        else if (tr.fraction < kFractionOne)
                        {
                            actor.pos[2] = tr.endPos[2] - fxFromInt(36);
                        }
                    }
                }
                actor.script = -1;
                actor.scriptPhase = kActorFree;
                if (actor.ai < 5)
                    actorRest(actor);
                scriptRouse(w, static_cast<int>(state.bits), false);
            }
            state.phase = kScriptIdle;
            state.running = false;
            state.base[1] = 0;
            state.timer = -1;
        }

        /// CancelScript: every script of its name lets go (or it alone, with
        /// none).
        inline void scriptCancel(LogicWorld &w, PlayerState &player, int index)
        {
            (void)player;
            if (index < 0 || index >= w.recordCount)
                return;
            const std::uint16_t name = w.records[index].name;
            if (name == 0)
            {
                scriptEntityCancel(w, index);
                return;
            }
            for (int i = 0; i < w.recordCount; ++i)
            {
                if (w.records[i].kind == static_cast<std::uint8_t>(LogicKind::Script) && w.records[i].name == name)
                    scriptEntityCancel(w, i);
            }
        }

        /// Someone able to say the line of the scripted_sentence at
        /// `index` says it, and its targets fire; false if there is nobody
        /// (HlAiTalk.hpp).
        inline bool sentenceSpeak(LogicWorld &w, PlayerState &player, int index);

        /// A scripted_sentence's thinking (FindThink, DelayThink): it looks
        /// for its speaker when told, or by itself if it has no name; said,
        /// it waits out the line and its refire before it may be again.
        inline void sentenceTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (state.dead || state.phase < 0)
                return;
            state.phase -= ms;
            if (state.phase > 0)
                return;
            if (!state.running)
            {
                // DelayThink: ready again.
                state.running = true;
                state.phase = (record.flags & kLogicStartOpen) ? 100 : -1;
                return;
            }
            if (sentenceSpeak(w, player, index))
            {
                if (record.maxs[0] & kSentenceOnce)
                    state.dead = true;
                state.running = false;
                state.phase = static_cast<std::int32_t>(record.extra[1] + record.extra[3]);
                if (state.phase <= 0)
                    state.phase = 1;
            }
            else
            {
                state.phase = static_cast<std::int32_t>(record.extra[3]) + 500;
            }
        }

        /// Every actor's clip moved on, and the targets its clip's events
        /// name fired as they are passed.
        inline void xenEvent(LogicWorld &w, PlayerState &player, int index, std::uint32_t event); // (HlXen.hpp)

        inline void actorsTick(LogicWorld &w, PlayerState &player, int ms)
        {
            if (w.actors == nullptr)
                return;
            for (int i = 0; i < w.actors->count; ++i)
            {
                ActorState &actor = w.actors->states[i];
                if (!actor.active)
                    continue;
                const std::uint32_t before = actorAdvance(*w.actors, actor, ms);
                const ModelView *model = actorModel(*w.actors, actor);
                if (model != nullptr && actor.clip >= 0)
                {
                    const ModelClip &clip = model->clips[actor.clip];
                    // (A clip that goes round has come back to its start.)
                    const bool wrapped = actor.frame < before;
                    for (int e = 0; e < clip.eventCount; ++e)
                    {
                        const ModelEvent &event = model->events[clip.firstEvent + e];
                        const std::uint32_t at = static_cast<std::uint32_t>(event.frame) << 8;
                        // Passed this tick -- or the clip began on it, or
                        // went round past it.
                        const bool passed = wrapped ? (at > before || at <= actor.frame)
                                                    : ((before < at && at <= actor.frame) ||
                                                       (at == 0 && before == 0 && actor.frame > 0));
                        if (!passed)
                            continue;
                        // (The monster's own: its AI sees to those -- a plant's,
                        // HlXen.hpp.)
                        if (event.kind == kEventMonster)
                        {
                            if (actor.kind == static_cast<std::uint8_t>(ActorKind::Xen))
                                xenEvent(w, player, i, event.target);
                            continue;
                        }
                        // The scene may (no longer) be broken off -- unless
                        // the map says it never may (AllowInterrupt).
                        if (event.kind == kEventInterrupt)
                        {
                            if (actor.script >= 0 && actor.script < w.recordCount &&
                                !(w.records[actor.script].pad & kScriptNoInterrupt))
                                w.states[actor.script].base[2] = event.target == 0 ? 1 : 0;
                            continue;
                        }
                        if (event.kind == kEventFire)
                        {
                            // (Whatever it is doing: the game's
                            // SCRIPT_EVENT_FIREEVENT is any clip's.)
                            if (w.nameHashes == nullptr)
                                continue;
                            for (int n = 1; n < w.nameCount; ++n)
                            {
                                if (w.nameHashes[n] == event.target)
                                    logicFire(w, player, static_cast<std::uint16_t>(n), actor.script,
                                              kUseToggle, 0);
                            }
                            continue;
                        }
                        // Its body hitting the floor, if there is floor
                        // under it: one of two thuds, the heavy one lower.
                        if (event.kind == kEventBodyDrop)
                        {
                            if (w.heard != nullptr && !actor.airborne)
                                w.heard->add(((actor.frame >> 8) ^ i) & 1 ? CommonSound::BodyDrop4
                                                                          : CommonSound::BodyDrop3,
                                             actor.pos, 127, kRadiusLarge, event.target != 0 ? 90 : 100);
                            continue;
                        }
                        // A sound of its own: a footstep in its walk, a
                        // word in its scene. From where it is -- its mouth,
                        // for what it says -- at full volume, heard only
                        // near (the game's ATTN_IDLE).
                        if (w.heard == nullptr || w.modelSounds == nullptr)
                            continue;
                        int low = 0, high = w.modelSoundCount - 1, found = -1;
                        while (low <= high && found < 0)
                        {
                            const int middle = (low + high) / 2;
                            if (w.modelSounds[middle].hash == event.target)
                                found = middle;
                            else if (w.modelSounds[middle].hash < event.target)
                                low = middle + 1;
                            else
                                high = middle - 1;
                        }
                        if (found < 0)
                            continue;
                        const bool voice = event.kind == kEventVoice;
                        const fx from[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(voice ? 60 : 4)};
                        w.heard->add(w.modelSounds[found].sound, from, 127, kRadiusSmall);
                    }
                }
                actorPlaceBox(*w.actors, i);
            }
        }
    } // namespace detail

    // ------------------------------------------------------------------ fans

    namespace detail
    {
        /// How a turning platform is turned, `phase` (Q30) of the way along
        /// its travel: built turned, it is unturned at the far end (and the
        /// other way about for one that starts there).
        inline std::uint16_t platYaw(const LogicRecord &record, std::int32_t phase)
        {
            const auto turn = static_cast<std::int32_t>(record.extra[0]);
            const std::int32_t along = (record.flags & kLogicStartOpen) ? phase : kFractionOne - phase;
            return static_cast<std::uint16_t>((static_cast<std::int64_t>(turn) * along) >> kFractionShift);
        }

        /// A platform that travels and turns: whoever stands on it goes and
        /// turns with it.
        inline void platTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (record.brush == kNoBrush)
                return;
            // One that is not switched goes when stood on, and comes back
            // three seconds after it gets there.
            if (!(record.flags & kLogicToggle) && state.move == MoveState::AtBottom && player.onGround &&
                player.groundMover == static_cast<int>(record.brush))
                moverStart(w, index, MoveState::GoingUp);
            if (state.move != MoveState::GoingUp && state.move != MoveState::GoingDown)
                return;
            const bool up = state.move == MoveState::GoingUp;
            const fx distance = fxLength(state.travel);
            std::int32_t step = kFractionOne;
            if (distance > 0 && record.speed > 0)
            {
                const std::int64_t travelMs = (static_cast<std::int64_t>(distance) * 1000) / record.speed;
                if (travelMs > ms)
                    step = static_cast<std::int32_t>((static_cast<std::int64_t>(ms) << kFractionShift) / travelMs);
            }
            std::int32_t phase = up ? state.phase + step : state.phase - step;
            if (phase > kFractionOne || phase < state.phase - step - 1)
                phase = kFractionOne;
            if (phase < 0)
                phase = 0;
            fx pos[3];
            for (int a = 0; a < 3; ++a)
                pos[a] = state.base[a] + static_cast<fx>((static_cast<std::int64_t>(state.travel[a]) * phase) >> kFractionShift);
            const BrushState &was = w.brushStates[record.brush];
            const fx from[3] = {was.pos[0], was.pos[1], was.pos[2]};
            const std::uint16_t fromYaw = was.angles[kYaw];
            if (carryBrush(w, player, record.brush, pos, platYaw(record, phase)))
            {
                // CFuncPlat::Blocked: one point of crushing, and back the way
                // it came.
                brushPutBack(w, record.brush, from, fromYaw);
                logicHurt(player, 1, 0, kDmgCrush);
                state.move = up ? MoveState::GoingDown : MoveState::GoingUp;
                return;
            }
            state.phase = phase;
            if (phase == (up ? kFractionOne : 0))
            {
                state.move = up ? MoveState::AtTop : MoveState::AtBottom;
                recordSound(w, index, true);
                if (up && !(record.flags & kLogicToggle))
                    state.timer = 3000;
                if (record.target != 0)
                    logicUseTargets(w, player, index, kUseToggle, 0);
            }
        }

        /// A brush that swings (CPendulum::Swing): pulled towards the middle
        /// of its distance, so hard that from rest at one end it just reaches
        /// the other at its speed, and never faster; with damping, that most
        /// dies away until, slow enough, it stops at the middle. Its state:
        /// travel[0] how far round it is from where it began, `speed` how
        /// fast it turns, travel[1] the most it may (all Q8 of the record's
        /// units); bits 1 while it goes back to where it began.
        inline void pendulumTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (record.brush == kNoBrush || !state.running || record.speed <= 0 || record.move[0] == 0)
                return;
            const std::int64_t most = static_cast<std::int64_t>(record.speed) << 8;
            std::int64_t at = state.travel[0], speed = state.speed;
            if (state.bits & 1)
            {
                // Back to where it began, at its speed, and still.
                const std::int64_t step = most * ms / 1000;
                at = at > step ? at - step : (at < -step ? at + step : 0);
                if (at == 0)
                    state.running = false, state.bits = 0;
                speed = 0;
            }
            else
            {
                const std::int64_t distance = static_cast<std::int64_t>(record.move[0]) << 8;
                const std::int64_t accel = most * most / (2 * (distance < 0 ? -distance : distance));
                speed += (at - distance / 2 > 0 ? -accel : accel) * ms / 1000;
                std::int64_t limit = most;
                if (record.extra[1] != 0)
                {
                    // (m_damp is "damp" / 1000 a second -- extra[1] is in
                    // thousandths of "damp"; under 30 degrees a second it is
                    // done.)
                    std::int64_t damped = state.travel[1];
                    damped -= damped * static_cast<std::int64_t>(record.extra[1]) * ms / 1000000000;
                    state.travel[1] = static_cast<fx>(damped);
                    limit = damped;
                    if (damped < (static_cast<std::int64_t>(30 * 65536 / 360) << 8))
                    {
                        at = distance / 2;
                        speed = 0;
                        state.running = false;
                    }
                }
                speed = speed > limit ? limit : (speed < -limit ? -limit : speed);
                at += speed * ms / 1000;
            }
            state.travel[0] = static_cast<fx>(at);
            state.speed = static_cast<fx>(speed);
            std::uint16_t angles[3] = {0, 0, 0};
            angles[record.extra[0] % 3] = static_cast<std::uint16_t>(at >> 8);
            logicPlaceBrushTurned(w, record.brush, state.base, angles);
            // What it swings into is pushed out of its way; and what it
            // touches, its "dmg" hurts by how fast it goes -- dmg * speed /
            // 100, in degrees a second -- and throws (CPendulum::Touch).
            const bool inside = w.movers[record.brush].solid && playerInsideBrush(w, player, record.brush);
            if (inside)
                pushOutOfBrush(w, player, record.brush);
            if (record.damage > 0 && (inside || player.touchedMover == static_cast<int>(record.brush)))
            {
                const std::int64_t degrees = (static_cast<std::int64_t>(speed < 0 ? -speed : speed) * 360 / 65536) >> 8;
                const int hurt = static_cast<int>(record.damage * degrees / 100);
                shoveFromBrush(w, player, record.brush, fxFromInt(hurt));
                if (hurt > 0)
                    logicHurt(player, hurt, 0, kDmgCrush);
            }
        }

        /// A wheel where it is round to (`phase`, 0..65536 of its distance).
        inline void momentaryPlace(LogicWorld &w, int index)
        {
            const LogicRecord &record = w.records[index];
            const LogicState &state = w.states[index];
            if (record.brush == kNoBrush)
                return;
            std::uint16_t angles[3];
            hingeRest(record, angles);
            const int axis = static_cast<int>(record.extra[0] % 3);
            angles[axis] = static_cast<std::uint16_t>(
                angles[axis] + ((static_cast<std::int64_t>(record.move[0]) * state.phase) >> 16));
            logicPlaceBrushTurned(w, record.brush, state.base, angles);
        }

        /// A wheel turned by hand, a frame on: round at its speed, to its end
        /// -- or, let go of, one that goes back by itself (`state.bits` 1)
        /// back at its return speed (move[1]), to where it started. What it
        /// targets is told how far round it is: a momentary_door goes as far
        /// along, and anything else is used (USE_SET), as the game's
        /// UpdateTarget does while the wheel turns -- a door opens.
        inline void momentaryTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            const bool returning = state.bits == 1;
            if (record.brush == kNoBrush || (!state.running && !returning))
                return;
            const std::int64_t distance = record.move[0] < 0 ? -record.move[0] : record.move[0];
            if (distance == 0)
            {
                state.running = false;
                state.bits = 0;
                return;
            }
            const fx speed = returning ? record.move[1] : record.speed;
            const std::int64_t step = (static_cast<std::int64_t>(speed) * ms * 65536) / (1000 * distance);
            std::int64_t at = state.phase + (returning || state.speed < 0 ? -step : step);
            if (at <= 0 || at >= 65536)
            {
                at = at <= 0 ? 0 : 65536;
                state.running = false;
                state.bits = 0;
            }
            state.phase = static_cast<std::int32_t>(at);
            momentaryPlace(w, index);
            for (int r = 0; r < w.recordCount && record.target != 0; ++r)
            {
                const LogicRecord &other = w.records[r];
                if (other.name == record.target && other.kind == static_cast<std::uint8_t>(LogicKind::MomentaryDoor))
                    w.states[r].timer = state.phase;
                // (Every wheel that moves the same thing goes round with
                // it, as far round as it is: UpdateAllButtons. A bridge
                // built as a wheel turns so.)
                if (r != index && other.target == record.target &&
                    other.kind == static_cast<std::uint8_t>(LogicKind::MomentaryButton))
                {
                    w.states[r].phase = state.phase;
                    momentaryPlace(w, r);
                }
            }
            // (Ten times a second, `timer` counting down to it: the game's
            // is each time the player's USE comes in, or its Return thinks --
            // which, all the way back, tells nothing more.)
            if (record.target != 0 && state.timer < 0 && (!returning || state.phase > 0))
            {
                state.timer = 100;
                logicFire(w, player, record.target, index, kUseSet, 0);
            }
        }

        /// What a wheel moves, a frame on: towards as far along as the wheel
        /// is round (`timer`), at its speed -- pushing the player and the
        /// boxes as a door does, and carrying what stands on it. Blocked, it
        /// stays where it is until the way is clear.
        inline void momentaryDoorTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (record.brush == kNoBrush || state.phase == state.timer)
                return;
            const fx length = fxLength(record.move);
            const std::int64_t step =
                length > 0 ? (static_cast<std::int64_t>(record.speed) * ms * 65536) / (static_cast<std::int64_t>(length) * 1000) : 65536;
            const std::int64_t d = static_cast<std::int64_t>(state.timer) - state.phase;
            const std::int32_t to =
                static_cast<std::int32_t>(d > step ? state.phase + step : (d < -step ? state.phase - step : state.timer));
            fx pos[3];
            for (int a = 0; a < 3; ++a)
                pos[a] = state.base[a] + static_cast<fx>((static_cast<std::int64_t>(record.move[a]) * to) >> 16);
            if (moveBrush(w, player, record.brush, pos))
                state.phase = to;
        }

        inline void rotatingTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (record.brush == kNoBrush)
                return;

            // Its speed creeps towards full or towards nothing in steps of a
            // tenth of a second; state.phase counts the milliseconds between.
            const fx full = record.speed;
            const fx step = static_cast<fx>(record.extra[1]);
            const bool forwards = full >= 0;
            state.phase += ms;
            while (state.phase >= 100)
            {
                state.phase -= 100;
                fx magnitude = forwards ? state.speed : -state.speed;
                const fx top = forwards ? full : -full;
                if (state.running && magnitude < top)
                    magnitude = magnitude + step > top ? top : magnitude + step;
                else if (!state.running && magnitude > 0)
                    magnitude = magnitude - step < 0 ? 0 : magnitude - step;
                state.speed = forwards ? magnitude : -magnitude;
            }
            if (state.speed == 0)
                return;

            const int axis = static_cast<int>(record.extra[0] % 3);
            std::uint16_t angles[3] = {0, 0, 0};
            const std::uint16_t before = static_cast<std::uint16_t>(state.bits);
            const auto turn =
                static_cast<std::int32_t>((static_cast<std::int64_t>(state.speed) * ms) / 1000);
            angles[axis] = static_cast<std::uint16_t>(before + turn);
            // With the player nowhere near, only how it is DRAWN is kept up:
            // where its faces are for the collision is worked out again when
            // someone comes within reach of it. (Its box holds it at any
            // angle, so "near" does not depend on which.)
            const MoverHull &hull = w.movers[record.brush];
            if (!playerTouchesBox(player, hull.mins, hull.maxs, fxFromInt(96)))
            {
                w.brushStates[record.brush].angles[axis] = angles[axis];
                state.bits = angles[axis];
                return;
            }
            logicPlaceBrushTurned(w, record.brush, state.base, angles);
            if (playerInsideBrush(w, player, record.brush))
            {
                // Blocked: it does not turn this frame, and its "dmg" crushes
                // (CFuncRotating::Blocked) -- and one that hurts by its touch
                // hurts by how fast it goes, and throws them off (HurtTouch:
                // a tenth of its degrees a second).
                angles[axis] = before;
                logicPlaceBrushTurned(w, record.brush, state.base, angles);
                if (record.damage > 0)
                    logicHurt(player, record.damage, 0, kDmgCrush);
                if (record.flags & kLogicTouch)
                {
                    const fx speed = state.speed < 0 ? -state.speed : state.speed;
                    const int hurt = static_cast<int>((static_cast<std::int64_t>(speed) * 360 + 32768) / 65536 / 10);
                    shoveFromBrush(w, player, record.brush, fxFromInt(hurt));
                    if (hurt > 0)
                        logicHurt(player, hurt, 0, kDmgCrush);
                }
                return;
            }
            if ((record.flags & kLogicTouch) && player.touchedMover == static_cast<int>(record.brush))
            {
                const fx speed = state.speed < 0 ? -state.speed : state.speed;
                const int hurt = static_cast<int>((static_cast<std::int64_t>(speed) * 360 + 32768) / 65536 / 10);
                shoveFromBrush(w, player, record.brush, fxFromInt(hurt));
                if (hurt > 0)
                    logicHurt(player, hurt, 0, kDmgCrush);
            }
            state.bits = angles[axis];
        }
    } // namespace detail

    // ------------------------------------------------------------------ use

    inline void logicUse(LogicWorld &w, PlayerState &player, int index, int caller, int useType,
                         int depth)
    {
        const LogicRecord &record = w.records[index];
        LogicState &state = w.states[index];
        if (state.dead)
            return;

        switch (static_cast<LogicKind>(record.kind))
        {
        case LogicKind::Door:
        case LogicKind::DoorRotating:
            // Only from rest: at the bottom, or at the top if it is the kind
            // that stays there.
            if (state.move == MoveState::AtBottom ||
                ((record.flags & kLogicToggle) && state.move == MoveState::AtTop))
                detail::doorActivate(w, player, index);
            break;

        case LogicKind::Button:
            if (state.move == MoveState::GoingUp || state.move == MoveState::GoingDown)
                break;
            if (state.move == MoveState::AtTop)
            {
                // Already in: only a toggle that is not held in can be pressed
                // again, and that sends it back out.
                if ((record.flags & kLogicToggle) && record.waitMs >= 0)
                    detail::moverStart(w, index, MoveState::GoingDown);
                break;
            }
            if (logicMasterAllows(w, index))
            {
                detail::unlockedSound(w, index);
                detail::moverStart(w, index, MoveState::GoingUp);
            }
            else
            {
                detail::lockedSound(w, index);
                detail::lockRefused(w, index);
            }
            break;

        case LogicKind::Breakable:
            // By name it breaks at once, whatever it can take -- except the
            // glass that never does.
            if (record.extra[1] != 7)
                detail::breakIt(w, player, index, depth);
            break;

        case LogicKind::Pushable:
            if (record.flags & kLogicBreaks)
                detail::breakIt(w, player, index, depth);
            break;

        case LogicKind::TriggerHurt:
        case LogicKind::TriggerPush:
            // On and off. state.running is "off".
            state.running = useType == kUseOn ? false : (useType == kUseOff ? true : !state.running);
            break;

        case LogicKind::WallToggle:
            if (record.brush != kNoBrush)
            {
                const bool on = w.brushStates[record.brush].active;
                const bool want = useType == kUseOn ? true : (useType == kUseOff ? false : !on);
                logicSetBrushActive(w, record.brush, want);
            }
            break;

        case LogicKind::TriggerRelay:
            logicUseTargets(w, player, index, record.useType, depth);
            if (record.flags & kLogicFireOnce)
                state.dead = true;
            break;

        case LogicKind::MultiManager:
            if (state.running && !(record.flags & kLogicThread))
                break; // still working through the last use
            state.running = true;
            {
                std::int32_t longest = 0;
                for (int a = 0; a < record.auxCount; ++a)
                {
                    const LogicAux &out = w.aux[record.firstAux + a];
                    logicEnqueue(w, out.target, 0, kUseToggle, index,
                                 static_cast<std::int32_t>(out.delayMs));
                    if (static_cast<std::int32_t>(out.delayMs) > longest)
                        longest = static_cast<std::int32_t>(out.delayMs);
                }
                state.timer = longest; // usable again once the last one has gone
            }
            break;

        case LogicKind::MultiSource:
        {
            // Whoever used it has their input flipped; when all are on, it
            // fires.
            for (int a = 0; a < record.auxCount && a < 32; ++a)
            {
                if (w.aux[record.firstAux + a].target == caller)
                    state.bits ^= 1u << a;
            }
            if (logicMasterAllows(w, index) && record.auxCount != 0)
            {
                const std::uint32_t all = record.auxCount >= 32
                                              ? 0xFFFFFFFFu
                                              : ((1u << record.auxCount) - 1u);
                if (state.bits == all && logicGlobalAllows(w, record))
                    logicUseTargets(w, player, index, kUseToggle, depth);
            }
            break;
        }

        case LogicKind::Train:
            // Used while stopped, it sets off; used while going, it stops
            // where it is and carries on to the same corner when used again.
            if (!state.running)
            {
                state.running = true;
                detail::trainNext(w, player, index);
            }
            else
            {
                state.running = false;
                state.move = MoveState::AtBottom;
                state.timer = -1;
                state.bits &= ~detail::kTrainPassing;
                // Heard stopping, as the game has it; and its move sound
                // stops with it, which in the game it does not (it went on
                // round until the train moved again).
                detail::recordSound(w, index, true);
            }
            break;

        case LogicKind::Sentence:
            // (Not while it is being said, or waiting to be said again.)
            if (state.running)
                state.phase = 0;
            break;

        case LogicKind::Script:
            // Told to start. Holding its actor already: off it goes. Without
            // one: it asks for it now, and keeps asking.
            if (w.actors == nullptr)
            {
                // No actors at all (nothing was cooked for them): the script
                // is over at once, so that what waits on it does not wait
                // for ever.
                state.dead = !(record.flags & kLogicToggle);
                logicUseTargets(w, player, index, kUseToggle, depth);
                break;
            }
            if (state.phase == detail::kScriptHolding)
            {
                detail::scriptPlay(w, player, index);
            }
            else if (state.phase == detail::kScriptMoving)
            {
                state.running = true; // it plays when it gets there
            }
            else if (state.phase == detail::kScriptIdle)
            {
                state.running = true;
                state.timer = 0;
            }
            break;

        case LogicKind::TrackTrain:
            // Started if it is stopped, stopped if it is going; "on" and
            // "off" only ever do the one.
            if (state.speed == 0)
            {
                if (useType != kUseOff && record.speed != 0)
                {
                    state.speed = record.speed;
                    detail::trainStarts(w, index);
                }
            }
            else if (useType != kUseOn)
            {
                state.speed = 0;
                detail::trainBrakes(w, index);
            }
            break;

        case LogicKind::TrackChange:
        {
            // It goes down for its own train, and only when that has come to
            // rest on it: what sets it off is the end of the upper track
            // saying a train has stopped there.
            const int train = static_cast<int>(state.bits);
            if (state.move != MoveState::AtBottom || caller != train || train >= w.recordCount ||
                record.brush == kNoBrush || w.records[train].brush == kNoBrush ||
                w.states[train].speed != 0)
                break;
            const fx *platform = w.brushStates[record.brush].pos;
            const BrushState &car = w.brushStates[w.records[train].brush];
            const fx apart[3] = {car.pos[0] - platform[0], car.pos[1] - platform[1], 0};
            fx wheels = static_cast<fx>(w.records[train].extra[0]);
            if (wheels <= 0)
                wheels = fxFromInt(100);
            if (fxLength(apart) >= wheels)
                break;
            LogicState &riding = w.states[train];
            riding.move = detail::kCarried;
            for (int a = 0; a < 3; ++a)
                riding.base[a] = car.pos[a] - platform[a];
            riding.travel[0] = car.angles[kYaw];
            state.phase = 0;
            state.move = MoveState::GoingUp;
            break;
        }

        case LogicKind::Rotating:
            // Spinning, it is told to stop, and always winds down. Stopped,
            // it winds up if it is the kind that does, or is simply at speed.
            if (state.running)
            {
                state.running = false;
            }
            else
            {
                state.running = true;
                if (!(record.flags & kLogicToggle))
                    state.speed = record.speed;
            }
            break;

        case LogicKind::Tank:
            // One that is not for a player's hands is switched on and off;
            // one that is, is taken at its controls (tankUse, HlMonsters.hpp).
            if (!(record.maxs[0] & kTankCanControl))
            {
                const bool on = useType == kUseToggle ? !state.running : useType != kUseOff;
                state.running = on;
                state.timer = -1;
            }
            break;

        case LogicKind::Repel:
            // His rope (HlMilitary.hpp's repelTick), once.
            state.running = true;
            break;

        case LogicKind::MortarField:
            // Called in: the shells are on their way (mortarTick), on
            // whoever called them if that is how it aims.
            state.running = true;
            for (int a = 0; a < 3; ++a)
                state.base[a] = player.origin[a];
            break;

        case LogicKind::MomentaryButton:
            // Turned while it is held in the game; here, from one USE to the
            // next -- or to its end, and back from there the next time. Let
            // go of, one that goes back by itself does (CMomentaryRotButton::
            // Off), and taken hold of again, it turns on from there.
            if (state.running)
            {
                state.running = false;
                if (record.move[1] > 0 && state.phase > 0)
                    state.bits = 1;
            }
            else
            {
                if (state.bits == 1)
                    state.speed = 1;
                else if (state.phase >= 65536)
                    state.speed = -1;
                else if (state.phase <= 0)
                    state.speed = 1;
                state.bits = 0;
                state.running = true;
                detail::recordSound(w, index, false);
            }
            break;

        case LogicKind::ChangeLevel:
            // Not while the player is outside its landmark's volumes, if it
            // has any (LogicKind::Transition).
            if (!detail::inTransition(w, player, record.extra[1]))
                break;
            // Its own targets first, as the game does; then the map changes
            // -- at the end of this frame, by the caller.
            logicUseTargets(w, player, index, kUseToggle, depth);
            if (w.pendingChange < 0)
                w.pendingChange = index;
            break;

        case LogicKind::Ambient:
            // Something that loops is switched on and off (whoever plays the
            // sounds reads `running`); anything else is heard once, now.
            if (record.flags & kLogicToggle)
            {
                state.running = useType == kUseToggle ? !state.running : useType != kUseOff;
            }
            else if (w.heard != nullptr && useType != kUseOff && record.extra[3] != 0 && record.netName != 0 &&
                     w.actors != nullptr)
            {
                // A line with a speaker: from where they stand, if they are
                // there to say it; from everywhere, as before, if not.
                const fx *from = nullptr;
                fx mouth[3];
                for (int i = 0; i < w.actors->count && from == nullptr; ++i)
                {
                    const ActorState &actor = w.actors->states[i];
                    if (!actor.active || actor.name != record.netName)
                        continue;
                    mouth[0] = actor.pos[0];
                    mouth[1] = actor.pos[1];
                    mouth[2] = actor.pos[2] + fxFromInt(60);
                    from = mouth;
                }
                w.heard->add(static_cast<std::uint16_t>(record.extra[0]), from, static_cast<int>(record.extra[2]),
                             from != nullptr ? kRadiusLarge : kRadiusEverywhere, kSoundSentence, record.pad);
            }
            else if (w.heard != nullptr && useType != kUseOff)
            {
                w.heard->add(static_cast<std::uint16_t>(record.extra[0]),
                             record.extra[1] == 0 ? nullptr : record.move,
                             static_cast<int>(record.extra[2]), static_cast<int>(record.extra[1]),
                             record.extra[3] != 0 ? kSoundSentence : std::uint8_t(0), record.pad);
            }
            // (A scripted_sentence's target fires as the line starts.)
            if (record.target != 0 || record.killTarget != 0)
                logicUseTargets(w, player, index, kUseToggle, depth);
            break;

        case LogicKind::Sprite:
        case LogicKind::Beam:
        case LogicKind::Explosion:
        case LogicKind::Shake:
        case LogicKind::Spark:
        case LogicKind::Shooter:
        case LogicKind::Render:
        case LogicKind::Fade:
            effectUse(w, player, index, useType);
            break;

        case LogicKind::Music:
            w.musicRequest = static_cast<int>(record.extra[0]);
            state.dead = true; // a track is started once
            break;

        case LogicKind::Message:
            // (More at once than there is room for: the last stands.)
            if (w.shownCount == 4)
                --w.shownCount;
            w.shown[w.shownCount++] = static_cast<std::uint16_t>(record.extra[0]);
            if (record.flags & kLogicFireOnce)
                state.dead = true;
            break;

        case LogicKind::Maker:
            // One for each use, or on and off (HlMonsters.hpp does the
            // making: state.phase is how many are owed).
            if (record.flags & kLogicToggle)
                ++state.phase;
            else
                state.running = useType == kUseToggle ? !state.running : useType != kUseOff;
            break;

        case LogicKind::HealthCharger:
        case LogicKind::SuitCharger:
            // Used each frame USE is held on it (HlMonsters.hpp).
            detail::chargerUse(w, player, index);
            break;

        case LogicKind::EnvGlobal:
            if (w.globals != nullptr)
            {
                const int old = w.globals->get(record.extra[0]);
                std::uint8_t now = kGlobalOn;
                switch (record.extra[1])
                {
                case 0:  now = kGlobalOff; break;
                case 1:  now = kGlobalOn; break;
                case 2:  now = kGlobalDead; break;
                default: now = old == kGlobalOn ? kGlobalOff : kGlobalOn; break;
                }
                w.globals->set(record.extra[0], now);
            }
            break;

        case LogicKind::Static:
            // Something that is only there -- and, if it wears a '+'
            // texture, shows its other face while this is set: the light
            // fixtures a switch turns off and on.
            state.bits ^= 1;
            break;

        case LogicKind::Conveyor:
            state.bits ^= 1; // the other way
            break;

        case LogicKind::PlatRot:
            // From rest, to its other end.
            if (state.move == MoveState::AtBottom)
                detail::moverStart(w, index, MoveState::GoingUp);
            else if (state.move == MoveState::AtTop)
                detail::moverStart(w, index, MoveState::GoingDown);
            break;

        case LogicKind::Light:
            // On, off; "on" and "off" only ever do the one.
            state.bits = useType == kUseOn ? 0u : (useType == kUseOff ? 1u : (state.bits ^ 1u));
            break;

        case LogicKind::Mark:
            detail::markPlace(w, record);
            break;

        case LogicKind::Pendulum:
            // CPendulum::PendulumUse: one that swings stops where it is --
            // or goes back to where it began, at its speed -- and one that
            // is still sets off from where it is.
            if (state.running && state.bits == 0 && (record.extra[2] & 1))
                state.bits = 1;
            else if (state.running)
                state.running = false, state.speed = 0, state.bits = 0;
            else
                state.running = true, state.travel[1] = record.speed << 8, state.bits = 0;
            break;

        case LogicKind::Beverage:
            // A can, while there are any: it drops out, and is a point of
            // health to whoever is there to take it.
            if (state.bits < record.extra[0])
            {
                ++state.bits;
                logicHurt(player, -1);
                if (w.achievements != nullptr)
                    w.achievements->push(AchEventType::Soda, 0);
                if (w.actors != nullptr)
                {
                    const fx out[3] = {0, 0, -fxFromInt(40)};
                    Piece &can = pieceAdd(*w.actors, kPieceCan, record.move, out, 1500);
                    can.half = 2;
                    can.colour = static_cast<std::uint16_t>(26 | 6 << 5 | 5 << 10);
                }
            }
            break;

        case LogicKind::Funnel:
            if (w.actors != nullptr)
            {
                effectAdd(*w.actors, kEffectRing, record.move, nullptr, 160, 700);
            }
            break;

        case LogicKind::Counter:
            // The count-th use is the one that fires; then it is done.
            if (++state.phase >= static_cast<std::int32_t>(record.extra[0]))
            {
                state.dead = true;
                logicUseTargets(w, player, index, useType, depth);
            }
            break;

        case LogicKind::ChangeTarget:
            // (Its `target` names whose target is changed, not what it fires.)
            for (int i = 0; i < w.recordCount && record.target != 0; ++i)
            {
                if (w.records[i].name == record.target)
                    w.states[i].target = static_cast<std::uint16_t>(record.extra[0]);
            }
            break;

        case LogicKind::WeaponStrip:
        {
            // Everything but the suit (bit 0 of Inventory::have).
            Inventory &inv = player.inv;
            const std::uint8_t suit = static_cast<std::uint8_t>(inv.have & 1u);
            const std::int16_t armor = inv.armor;
            inv = Inventory();
            inv.have = suit;
            inv.armor = armor;
            if (player.arsenal != nullptr)
            {
                Arsenal &more = *player.arsenal;
                const std::int16_t antidotes = more.antidotes, cards = more.securityCards;
                more = Arsenal();
                more.antidotes = antidotes;
                more.securityCards = cards;
            }
            break;
        }

        case LogicKind::LoadSaved:
            // The screen goes dark, what it has to say is said, and the game
            // goes back to the last save (the caller's: LogicWorld::reloadMs).
            if (w.reloadMs >= 0)
                break;
            w.fadeFrom = false;
            w.fadeMs = 0;
            w.fadeWholeMs = static_cast<std::int32_t>(record.extra[0]);
            w.fadeHoldMs = static_cast<std::int32_t>(record.extra[1]) + 1000;
            w.fadeDark = static_cast<std::uint8_t>(record.extra[2] > 255 ? 255 : record.extra[2]);
            w.fadeColour.value = 0;
            w.fading = true;
            if (record.extra[3] != 0xFFFFFFFFu)
            {
                if (w.shownCount == 4)
                    --w.shownCount;
                w.shown[w.shownCount++] = static_cast<std::uint16_t>(record.extra[3]);
            }
            w.reloadMs = static_cast<std::int32_t>(record.extra[1]);
            break;

        case LogicKind::EndSection:
            w.endSection = true;
            break;

        case LogicKind::MonsterJump:
            // On, or off (it starts off when something switches it on).
            state.running = useType == kUseToggle ? !state.running : useType != kUseOff;
            break;

        case LogicKind::Camera:
            detail::cameraUse(w, player, index, useType);
            break;

        case LogicKind::Speaker:
            detail::speakerUse(w, index, useType);
            break;

        case LogicKind::GunTarget:
            detail::gunTargetUse(w, player, index, useType);
            break;

        case LogicKind::ButtonTarget:
            // Only if it may be used (otherwise only a shot turns it).
            if (record.flags & kLogicUseOnly)
                detail::buttonTargetToggle(w, player, index, depth);
            break;

        case LogicKind::Blood:
            detail::bloodUse(w, player, index);
            break;

        default:
            // The triggers: a trigger volume cannot be "used", only
            // entered.
            break;
        }
    }

    // ------------------------------------------------------------------ per frame

    /// Set everything to how the map starts. Call once the arrays are in place.
    inline void logicInit(LogicWorld &w)
    {
        for (LogicEvent &event : w.events)
            event = LogicEvent();
        w.eventsDropped = 0;

        w.viewTurn = 0;
        w.shakeMs = 0;
        w.fading = false;
        w.fadeLevel = 0;
        w.musicRequest = -1;
        for (int c = 0; c < w.cornerCount && w.cornerState != nullptr; ++c)
            w.cornerState[c] = (w.corners[c].flags & kCornerDisabled) ? kNodeOff : 0;

        for (int b = 0; b < w.brushCount; ++b)
        {
            w.brushStates[b].active = (w.brushes[b].flags & kBrushStartsOff) == 0;
            w.brushStates[b].look = (w.brushes[b].flags & kBrushStartsUnseen) ? kBrushUnseen : 0;
            w.brushStates[b].frame = 0;
            w.brushStates[b].scroll = 0;
            logicPlaceBrush(w, b, w.brushes[b].origin);
        }
        int ladderSlot = 0;
        for (int i = 0; i < w.recordCount; ++i)
        {
            const LogicRecord &record = w.records[i];
            LogicState &state = w.states[i];
            state = LogicState();
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Maker) ||
                record.kind == static_cast<std::uint8_t>(LogicKind::Ambient) ||
                record.kind == static_cast<std::uint8_t>(LogicKind::Sprite) ||
                record.kind == static_cast<std::uint8_t>(LogicKind::Beam) ||
                record.kind == static_cast<std::uint8_t>(LogicKind::Spark))
                state.running = (record.flags & kLogicStartOpen) != 0;
            // A speaker that speaks from the start does so 5 to 15 seconds in.
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Speaker))
            {
                state.running = (record.flags & kLogicStartOpen) != 0;
                state.timer = state.running ? 5000 + static_cast<std::int32_t>(detail::logicChance(w) % 10000) : -1;
            }
            if (record.kind == static_cast<std::uint8_t>(LogicKind::ButtonTarget))
                state.bits = (record.flags & kLogicStartOpen) ? 1u : 0u;
            if (record.kind == static_cast<std::uint8_t>(LogicKind::MonsterJump))
                state.running = (record.flags & kLogicStartOpen) == 0;
            // A scripted_sentence is ready to be said; with no name it
            // looks for its speaker a second in (and in `phase`, the ms to
            // its next look).
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Sentence))
            {
                state.running = true;
                state.phase = (record.flags & kLogicStartOpen) ? 1000 : -1;
            }

            if (record.kind == static_cast<std::uint8_t>(LogicKind::Ladder) && w.ladders != nullptr)
            {
                LadderVolume &ladder = w.ladders[ladderSlot++];
                for (int a = 0; a < 3; ++a)
                {
                    ladder.mins[a] = fxFromInt(record.mins[a]);
                    ladder.maxs[a] = fxFromInt(record.maxs[a]);
                }
                ladder.active = true;
            }

            // Its master, if that names a multisource. Anything else by that
            // name counts as "on", as it does on the PC.
            for (int m = 0; m < w.recordCount && record.master != 0; ++m)
            {
                if (w.records[m].name == record.master &&
                    w.records[m].kind == static_cast<std::uint8_t>(LogicKind::MultiSource))
                {
                    state.master = static_cast<std::int16_t>(m);
                    break;
                }
            }

            if (record.kind == static_cast<std::uint8_t>(LogicKind::GunTarget))
            {
                // On its first corner; ridden from the start if the map says.
                state.speed = record.speed;
                state.bits = record.firstAux;
                if (record.brush != kNoBrush && record.firstAux != kNoCorner)
                {
                    fx pos[3];
                    detail::trainPosAt(w, i, record.firstAux, pos);
                    logicPlaceBrush(w, record.brush, pos);
                    if (record.flags & kLogicStartOpen)
                    {
                        state.running = true;
                        state.timer = 100;
                    }
                }
            }
            else if (record.kind == static_cast<std::uint8_t>(LogicKind::Train))
            {
                // A train begins on its first corner. With no name it sets off
                // by itself; with one, it waits to be told.
                state.speed = record.speed;
                state.bits = record.firstAux;
                if (record.brush != kNoBrush && record.firstAux != kNoCorner)
                {
                    fx pos[3];
                    detail::trainPosAt(w, i, record.firstAux, pos);
                    // (Turned as it was made to be: a train keeps its
                    // angles for good -- the tram's door is a quarter turn
                    // round from how it was built.)
                    const std::uint16_t turned[3] = {static_cast<std::uint16_t>(record.extra[0]),
                                                     static_cast<std::uint16_t>(record.extra[1]),
                                                     static_cast<std::uint16_t>(record.extra[2])};
                    logicPlaceBrushTurned(w, record.brush, pos, turned);
                    if (record.name == 0)
                    {
                        state.running = true;
                        state.timer = 100;
                    }
                }
            }
            else if (record.kind == static_cast<std::uint8_t>(LogicKind::Tank))
            {
                // A gun on a mount rests turned as the map has it (its brush
                // is built pointing along x), on, or waiting to be, and has
                // not fired (`timer` -1: the game's m_fireLast 0).
                state.running = (record.flags & kLogicStartOpen) != 0;
                state.timer = -1;
                state.bits = record.extra[0];
                if (record.brush != kNoBrush)
                {
                    for (int a = 0; a < 3; ++a)
                        state.base[a] = w.brushes[record.brush].origin[a];
                    const std::uint16_t angles[3] = {static_cast<std::uint16_t>(state.bits >> 16),
                                                     static_cast<std::uint16_t>(state.bits), 0};
                    logicPlaceBrushTurned(w, record.brush, state.base, angles);
                }
            }
            else if (record.kind == static_cast<std::uint8_t>(LogicKind::MomentaryButton) ||
                     record.kind == static_cast<std::uint8_t>(LogicKind::MomentaryDoor))
            {
                // A wheel, and what it moves: where they were built, none of
                // the way round (`phase`, 0..65536); which way a wheel will go
                // when it is next used (`speed`, 1 on, -1 back); and where a
                // door is to go (`timer`).
                state.speed = 1;
                state.timer = 0;
                if (record.brush != kNoBrush)
                {
                    for (int a = 0; a < 3; ++a)
                        state.base[a] = w.brushes[record.brush].origin[a];
                    if (record.kind == static_cast<std::uint8_t>(LogicKind::MomentaryButton))
                    {
                        std::uint16_t angles[3];
                        detail::hingeRest(record, angles);
                        logicPlaceBrushTurned(w, record.brush, state.base, angles);
                    }
                }
            }
            else if (record.kind == static_cast<std::uint8_t>(LogicKind::DoorRotating) ||
                     (record.kind == static_cast<std::uint8_t>(LogicKind::Button) && (record.flags & kLogicTurns)))
            {
                // It turns where it stands: its base is its place, and its
                // travel an angle. One that starts open rests TURNED -- that
                // is its "bottom" -- and its travel is back to where it was
                // built, exactly as a sliding door that starts open rests at
                // the far end and travels home.
                const bool startOpen = (record.flags & kLogicStartOpen) != 0;
                if (record.brush != kNoBrush)
                {
                    for (int a = 0; a < 3; ++a)
                        state.base[a] = w.brushes[record.brush].origin[a];
                    state.bits = startOpen ? static_cast<std::uint16_t>(record.move[0]) : 0;
                    state.travel[0] = startOpen ? -record.move[0] : record.move[0];
                    std::uint16_t angles[3];
                    detail::hingeRest(record, angles);
                    angles[record.extra[0] % 3] = static_cast<std::uint16_t>(angles[record.extra[0] % 3] + state.bits);
                    logicPlaceBrushTurned(w, record.brush, state.base, angles);
                }
            }
            else if (record.brush != kNoBrush)
            {
                // A door that starts open sits at the far end of its travel
                // and "opens" back towards where it was built.
                const bool startOpen = (record.flags & kLogicStartOpen) != 0;
                for (int a = 0; a < 3; ++a)
                {
                    state.base[a] = w.brushes[record.brush].origin[a] +
                                    (startOpen ? record.move[a] : 0);
                    state.travel[a] = startOpen ? -record.move[a] : record.move[a];
                }
                if (startOpen)
                    logicPlaceBrush(w, record.brush, state.base);
            }
            if (record.kind == static_cast<std::uint8_t>(LogicKind::PlatRot) && record.brush != kNoBrush)
                logicPlaceBrush(w, record.brush, state.base, detail::platYaw(record, 0));
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Light))
                state.bits = (record.flags & kLogicStartOpen) ? 1 : 0;
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Pendulum))
            {
                state.running = (record.flags & kLogicStartOpen) != 0;
                state.travel[1] = record.speed << 8;
                if (record.brush != kNoBrush)
                {
                    for (int a = 0; a < 3; ++a)
                        state.base[a] = w.brushes[record.brush].origin[a];
                }
            }
            if (record.kind == static_cast<std::uint8_t>(LogicKind::TriggerAuto))
                state.timer = 100; // a tenth of a second after the map starts
            if (record.kind == static_cast<std::uint8_t>(LogicKind::TriggerHurt) ||
                record.kind == static_cast<std::uint8_t>(LogicKind::TriggerPush))
            {
                // (state.running is "off": a place that hurts, or a current,
                // that something has yet to switch on.)
                state.running = (record.flags & kLogicStartOpen) != 0;
                continue;
            }
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Script))
            {
                // One that starts by itself looks for its actor a second in.
                state.phase = detail::kScriptIdle;
                state.timer = 1000;
                continue;
            }
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Pushable))
            {
                // Where it was built, at rest; nothing of a door's about it.
                for (int a = 0; a < 3; ++a)
                    state.base[a] = state.travel[a] = 0;
                continue;
            }
            if (record.kind == static_cast<std::uint8_t>(LogicKind::TrackTrain))
            {
                // On its first node, its own height above the track, facing
                // along it, at the speed the map starts it with.
                state.bits = record.firstAux;
                state.speed = static_cast<fx>(record.extra[3]);
                if (record.brush != kNoBrush && record.firstAux != kNoCorner)
                {
                    const fx *node = w.corners[record.firstAux].pos;
                    const fx pos[3] = {node[0], node[1], node[2] + static_cast<fx>(record.extra[1])};
                    std::uint16_t yaw = 0;
                    detail::trackFacing(w, record, record.firstAux, pos, yaw);
                    logicPlaceBrush(w, record.brush, pos, yaw);
                }
                continue;
            }
            if (record.kind == static_cast<std::uint8_t>(LogicKind::TrackChange))
            {
                // At the top, with the way on from the upper track closed:
                // a train that comes along stops on it.
                state.bits = static_cast<std::uint32_t>(detail::trackChangeTrain(w, record));
                if (record.brush != kNoBrush)
                {
                    for (int a = 0; a < 3; ++a)
                    {
                        state.base[a] = w.brushes[record.brush].origin[a];
                        state.travel[a] = record.move[a];
                    }
                }
                if (record.extra[1] != kNoCorner)
                    detail::nodeSwitch(w, w.corners[record.extra[1]].next, false);
                continue;
            }
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Rotating))
            {
                // Its place is where it was built. One that starts on does so
                // a second and a half into the map, as the game has it.
                if (record.brush != kNoBrush)
                {
                    for (int a = 0; a < 3; ++a)
                        state.base[a] = w.brushes[record.brush].origin[a];
                }
                if (record.flags & kLogicStartOpen)
                    state.timer = 1500;
            }
            if (record.kind == static_cast<std::uint8_t>(LogicKind::EnvGlobal) &&
                (record.flags & kLogicStartOpen) && w.globals != nullptr &&
                w.globals->get(record.extra[0]) < 0)
                w.globals->set(record.extra[0], static_cast<std::uint8_t>(record.extra[2]));
        }
    }

    namespace detail
    {
        /// What record `i` does on being walked into by `player`: a trigger
        /// that fires its targets, a door or a button that goes at a touch.
        /// (Apart from logicTick so that it can be asked of someone who is
        /// not this console's player: logicGhostTouch.)
        inline void touchedBy(LogicWorld &w, PlayerState &player, int i)
        {
            const LogicRecord &record = w.records[i];
            LogicState &state = w.states[i];
            const auto kind = static_cast<LogicKind>(record.kind);
            if ((kind == LogicKind::TriggerOnce || kind == LogicKind::TriggerMultiple) &&
                     state.timer < 0)
            {
                const fx mins[3] = {fxFromInt(record.mins[0]), fxFromInt(record.mins[1]),
                                    fxFromInt(record.mins[2])};
                const fx maxs[3] = {fxFromInt(record.maxs[0]), fxFromInt(record.maxs[1]),
                                    fxFromInt(record.maxs[2])};
                // Whoever it is for: the player, unless it ignores them; and
                // if it says so, a box that is pushed into it (the sample
                // cart sets off the resonance cascade this way).
                bool touched = !(record.flags & kLogicNoPlayer) &&
                               detail::playerTouchesBox(player, mins, maxs, 0);
                for (int b = 0; !touched && (record.flags & kLogicPushables) && b < w.recordCount; ++b)
                {
                    const LogicRecord &other = w.records[b];
                    if (other.kind != static_cast<std::uint8_t>(LogicKind::Pushable) ||
                        other.brush == kNoBrush || w.states[b].dead ||
                        !w.brushStates[other.brush].active)
                        continue;
                    // (Its own box where it now is -- not the one round its
                    // origin that holds it at any angle: c1a0e's cart, built
                    // round one, would set the cascade off a hundred units
                    // early.)
                    const Brush &brush = w.brushes[other.brush];
                    const BrushState &at = w.brushStates[other.brush];
                    fx lo[3], hi[3];
                    for (int k = 0; k < 3; ++k)
                    {
                        lo[k] = fxFromInt(brush.mins[k]) + at.pos[k] - brush.origin[k];
                        hi[k] = fxFromInt(brush.maxs[k]) + at.pos[k] - brush.origin[k];
                    }
                    touched = lo[0] <= maxs[0] && hi[0] >= mins[0] && lo[1] <= maxs[1] && hi[1] >= mins[1] &&
                              lo[2] <= maxs[2] && hi[2] >= mins[2];
                }
                if (touched && logicMasterAllows(w, i))
                {
                    logicUseTargets(w, player, i, kUseToggle, 0);
                    if (kind == LogicKind::TriggerOnce || record.waitMs < 0)
                        state.dead = true;
                    else
                        state.timer = record.waitMs;
                }
            }
            else if ((record.flags & kLogicTouch) && record.brush != kNoBrush &&
                     state.move == MoveState::AtBottom && w.brushStates[record.brush].active)
            {
                const MoverHull &mover = w.movers[record.brush];
                if (detail::playerTouchesBox(player, mover.mins, mover.maxs, kTouchSlack))
                    logicUse(w, player, i, -1, kUseToggle);
            }
        }
    } // namespace detail

    /// Advance everything by `ms` milliseconds.
    inline void logicTick(LogicWorld &w, PlayerState &player, int ms)
    {
        w.lockSaidMs = w.lockSaidMs > ms ? w.lockSaidMs - ms : 0;
        // What the push fields push them by is said again each frame; and
        // how hard they are pulled is what the switches say (a map come into,
        // a save loaded).
        player.push.value = BaseVelocity();
        player.slip.value = 0;
        if (w.globals != nullptr)
        {
            const int gravity = w.globals->get(kGlobalPlayerGravity);
            if (gravity >= 0)
                player.gravityPercent.value = static_cast<std::uint8_t>(gravity == 100 ? 0 : gravity);
        }
        if ((w.marksDueMs -= ms) <= 0)
        {
            w.marksDueMs = 500;
            detail::marksNear(w, player);
        }
        if ((w.linksDueMs -= ms) <= 0)
        {
            w.linksDueMs = 250;
            detail::linksTick(w, player);
        }
        // A view from elsewhere, for as long as it lasts; and the count down
        // to loading the last save (the caller's at 0).
        if (w.camera >= 0 && (w.cameraLeftMs -= ms) <= 0)
            detail::cameraEnd(w, player);
        if (w.reloadMs > 0)
            w.reloadMs = w.reloadMs > ms ? w.reloadMs - ms : 0;
        // 1. Fires that have come due. A fire can queue more with no delay, so
        //    go round until nothing is due.
        for (LogicEvent &event : w.events)
        {
            if (event.active)
                event.ms -= ms;
        }
        for (int guard = 0; guard < kMaxLogicEvents; ++guard)
        {
            bool fired = false;
            for (LogicEvent &event : w.events)
            {
                if (!event.active || event.ms > 0)
                    continue;
                const LogicEvent due = event;
                event.active = false;
                logicKill(w, due.killTarget);
                logicFire(w, player, due.target, due.caller, due.useType, 0);
                fired = true;
            }
            if (!fired)
                break;
        }

        // The actors' clips, before the scripts look at where they have got to.
        detail::actorsTick(w, player, ms);

        for (int i = 0; i < w.recordCount; ++i)
        {
            const LogicRecord &record = w.records[i];
            LogicState &state = w.states[i];
            if (state.dead)
                continue;
            const auto kind = static_cast<LogicKind>(record.kind);

            // 2. Whatever it was waiting to do. (A box's timer is not one: it
            //    is the damage it has taken; nor a gun's, its rounds' time,
            //    a mortar field's, its shells', nor a wheel's door's, where it
            //    is to go, nor a speaker's, its next line.)
            if (state.timer >= 0 && kind != LogicKind::Pushable && kind != LogicKind::Script &&
                kind != LogicKind::Tank && kind != LogicKind::MortarField && kind != LogicKind::MomentaryDoor &&
                kind != LogicKind::Speaker)
            {
                state.timer -= ms;
                if (state.timer < 0)
                {
                    state.timer = -1;
                    switch (kind)
                    {
                    case LogicKind::Door:
                    case LogicKind::DoorRotating:
                    case LogicKind::Button:
                        if (state.move == MoveState::AtTop)
                            detail::moverStart(w, i, MoveState::GoingDown);
                        break;
                    case LogicKind::TriggerAuto:
                        if (logicGlobalAllows(w, record))
                            logicUseTargets(w, player, i, record.useType, 0);
                        state.dead = true;
                        break;
                    case LogicKind::MultiManager:
                        state.running = false;
                        break;
                    case LogicKind::Rotating:
                        logicUse(w, player, i, -1, kUseToggle);
                        break;
                    case LogicKind::PlatRot:
                        if (state.move == MoveState::AtTop)
                            detail::moverStart(w, i, MoveState::GoingDown);
                        break;
                    case LogicKind::Breakable:
                        detail::breakIt(w, player, i, 0); // stood on, and its delay is up
                        break;
                    case LogicKind::Train:
                    case LogicKind::GunTarget:
                        if (state.running)
                            detail::trainNext(w, player, i);
                        break;
                    default:
                        break; // a trigger_multiple: simply armed again
                    }
                }
            }

            // 3. Movement.
            if (kind == LogicKind::Door || kind == LogicKind::Button ||
                kind == LogicKind::DoorRotating)
                detail::moverTick(w, player, i, ms);
            else if (kind == LogicKind::Train || kind == LogicKind::GunTarget)
                detail::trainTick(w, player, i, ms);
            else if (kind == LogicKind::Speaker)
                detail::speakerTick(w, i, ms);
            else if (kind == LogicKind::Rotating)
                detail::rotatingTick(w, player, i, ms);
            else if (kind == LogicKind::PlatRot)
                detail::platTick(w, player, i, ms);
            else if (kind == LogicKind::Pendulum)
                detail::pendulumTick(w, player, i, ms);
            else if (kind == LogicKind::Pushable)
                detail::pushableTick(w, player, i, ms);
            else if (kind == LogicKind::Script)
                detail::scriptTick(w, player, i, ms);
            else if (kind == LogicKind::Sentence)
                detail::sentenceTick(w, player, i, ms);
            else if (kind == LogicKind::TrackTrain)
                detail::trackTrainTick(w, player, i, ms);
            else if (kind == LogicKind::TrackChange)
                detail::trackChangeTick(w, player, i, ms);
            else if (kind == LogicKind::MomentaryButton)
                detail::momentaryTick(w, player, i, ms);
            else if (kind == LogicKind::MomentaryDoor)
                detail::momentaryDoorTick(w, player, i, ms);

            // 4. The player walking into things.
            if (kind == LogicKind::Breakable && record.brush != kNoBrush && !state.dead)
            {
                const bool on = player.onGround && player.groundMover == static_cast<int>(record.brush);
                // Stood on, it gives way after its delay (a tenth of a second
                // if it has none).
                if ((record.flags & kLogicPressure) && on && state.timer < 0)
                    state.timer = record.delayMs > 0 ? static_cast<std::int32_t>(record.delayMs) : 100;
                // Run into hard enough, it breaks, and that hurts.
                if ((record.flags & kLogicTouch) && !(record.flags & kLogicTriggerOnly) &&
                    player.touchedMover == static_cast<int>(record.brush))
                {
                    const std::int32_t blow = fxToInt(fxLength(player.velocity)) / 100;
                    if (blow >= static_cast<std::int32_t>(record.extra[0]) - state.phase)
                    {
                        logicHurt(player, blow / 4, 0, kDmgSlash);
                        detail::breakIt(w, player, i, 0);
                    }
                }
            }
            else if (kind == LogicKind::EndSection && (record.flags & kLogicTouch) && !state.dead)
            {
                const fx mins[3] = {fxFromInt(record.mins[0]), fxFromInt(record.mins[1]),
                                    fxFromInt(record.mins[2])};
                const fx maxs[3] = {fxFromInt(record.maxs[0]), fxFromInt(record.maxs[1]),
                                    fxFromInt(record.maxs[2])};
                if (detail::playerTouchesBox(player, mins, maxs, 0))
                    logicUse(w, player, i, -1, kUseOn);
            }
            else if (kind == LogicKind::Music && (record.flags & kLogicTouch) && !state.dead)
            {
                const fx mins[3] = {fxFromInt(record.mins[0]), fxFromInt(record.mins[1]),
                                    fxFromInt(record.mins[2])};
                const fx maxs[3] = {fxFromInt(record.maxs[0]), fxFromInt(record.maxs[1]),
                                    fxFromInt(record.maxs[2])};
                if (detail::playerTouchesBox(player, mins, maxs, 0))
                    logicUse(w, player, i, -1, kUseOn);
            }
            else if (kind == LogicKind::TriggerHurt || kind == LogicKind::TriggerPush ||
                     kind == LogicKind::TriggerTeleport || kind == LogicKind::TriggerGravity ||
                     kind == LogicKind::Friction)
            {
                const bool inside = detail::playerInVolume(w, player, record) && logicMasterAllows(w, i);
                if (kind == LogicKind::TriggerHurt)
                {
                    // Half a second's worth every half second. state.phase
                    // counts the milliseconds to the next.
                    state.phase -= ms;
                    if (inside && !state.running && state.phase <= 0)
                    {
                        logicHurt(player, static_cast<std::int32_t>(record.extra[0]) / 2, hurtKindsOf(record.extra[1]),
                                  record.extra[1]);
                        state.phase = 500;
                        if (record.target != 0 && !(state.bits & 1))
                        {
                            state.bits |= 1;
                            logicUseTargets(w, player, i, kUseToggle, 0);
                        }
                    }
                    if (state.phase < 0)
                        state.phase = 0;
                }
                else if (kind == LogicKind::TriggerPush && inside && !state.running && !state.dead)
                {
                    // CTriggerPush::Touch. Once: its speed added to theirs, and
                    // it is gone. Otherwise their base velocity for as long
                    // as they are in it (the player's move makes of it a push
                    // up and a carry along: HlPlayerMove.hpp), what more than
                    // one push them by added together.
                    if (record.flags & kLogicFireOnce)
                    {
                        for (int a = 0; a < 3; ++a)
                            player.velocity[a] += record.move[a];
                        if (player.velocity[2] > 0)
                        {
                            player.onGround = false;
                            player.groundMover = -1;
                        }
                        state.dead = true;
                    }
                    else
                    {
                        for (int a = 0; a < 3; ++a)
                            player.push.value.v[a] += record.move[a];
                    }
                }
                else if (kind == LogicKind::Friction && inside)
                {
                    // CFrictionModifier::ChangeFriction, for this frame (the
                    // game's move puts it back after each).
                    const int hold = record.extra[0] > 100 ? 100 : static_cast<int>(record.extra[0]);
                    player.slip.value = static_cast<std::uint8_t>(100 - hold);
                }
                else if (kind == LogicKind::TriggerGravity && inside)
                {
                    // CTriggerGravity::GravityTouch: from now on, until
                    // another says otherwise.
                    const auto percent = static_cast<std::uint8_t>(record.extra[0] == 0 ? 100 : record.extra[0]);
                    player.gravityPercent.value = static_cast<std::uint8_t>(percent == 100 ? 0 : percent);
                    if (w.globals != nullptr)
                        w.globals->set(kGlobalPlayerGravity, percent);
                }
                else if (kind == LogicKind::TriggerTeleport && inside && !(record.flags & kLogicNoPlayer))
                {
                    for (int a = 0; a < 3; ++a)
                    {
                        player.origin[a] = record.move[a];
                        player.velocity[a] = 0;
                    }
                    player.onGround = false;
                    player.groundMover = -1;
                    w.faceSet = true;
                    w.faceYaw = static_cast<std::uint16_t>(record.extra[0]);
                }
            }
            else if (kind == LogicKind::AutoSave)
            {
                const fx mins[3] = {fxFromInt(record.mins[0]), fxFromInt(record.mins[1]),
                                    fxFromInt(record.mins[2])};
                const fx maxs[3] = {fxFromInt(record.maxs[0]), fxFromInt(record.maxs[1]),
                                    fxFromInt(record.maxs[2])};
                if (detail::playerTouchesBox(player, mins, maxs, 0) && logicMasterAllows(w, i))
                {
                    w.saveRequested = true;
                    state.dead = true;
                }
            }
            else if (kind == LogicKind::ChangeLevel)
            {
                const fx mins[3] = {fxFromInt(record.mins[0]), fxFromInt(record.mins[1]),
                                    fxFromInt(record.mins[2])};
                const fx maxs[3] = {fxFromInt(record.maxs[0]), fxFromInt(record.maxs[1]),
                                    fxFromInt(record.maxs[2])};
                if ((record.flags & kLogicTouch) && w.pendingChange < 0 &&
                    detail::playerTouchesBox(player, mins, maxs, 0))
                    logicUse(w, player, i, -1, kUseToggle);
            }
            else
                detail::touchedBy(w, player, i);
        }

        // 4. The switched lights: each style is as the last light of it
        //    says.
        w.stylesOff = 0;
        for (int i = 0; i < w.recordCount; ++i)
        {
            const LogicRecord &record = w.records[i];
            if (record.kind != static_cast<std::uint8_t>(LogicKind::Light) || record.extra[0] >= kLightStyles ||
                record.extra[0] < 32)
                continue;
            const std::uint64_t bit = std::uint64_t(1) << record.extra[0];
            const bool off = (w.states[i].bits & 1) != 0;
            w.stylesOff = off ? (w.stylesOff | bit) : (w.stylesOff & ~bit);
            // (On, it shines by its pattern, if it has one: CLight::Use.)
            w.stylePattern.value[record.extra[0] - 32] =
                !off && record.extra[1] != 0 && w.strings.value != nullptr ? w.strings.value + record.extra[1] : nullptr;
        }

        // 5. What each brush shows of its textures, and the conveyors.
        for (int i = 0; i < w.recordCount; ++i)
        {
            const LogicRecord &record = w.records[i];
            const LogicState &state = w.states[i];
            if (record.brush == kNoBrush || record.brush >= w.brushCount)
                continue;
            BrushState &brush = w.brushStates[record.brush];
            switch (static_cast<LogicKind>(record.kind))
            {
            case LogicKind::Button:
                brush.frame = state.move != MoveState::AtBottom;
                break;
            case LogicKind::Static:
            case LogicKind::ButtonTarget:
                brush.frame = state.bits & 1;
                break;
            case LogicKind::HealthCharger:
            case LogicKind::SuitCharger:
                brush.frame = static_cast<int>(state.bits) >= chargerCapacity(w, record); // nothing left in it
                break;
            case LogicKind::Conveyor:
            {
                if (state.dead || !brush.active)
                    break;
                const int way = (state.bits & 1) ? -1 : 1;
                brush.scroll = (brush.scroll + static_cast<fx>((static_cast<std::int64_t>(record.speed) * way * ms) / 1000)) &
                               (fxFromInt(1024) - 1);
                // Whoever stands on it goes with it, as far as the world
                // lets them.
                if (!(record.extra[0] & 1) && player.onGround && player.groundMover == static_cast<int>(record.brush) &&
                    w.world != nullptr)
                {
                    fx to[3];
                    for (int a = 0; a < 3; ++a)
                        to[a] = player.origin[a] + static_cast<fx>((static_cast<std::int64_t>(record.move[a]) * way * ms) / 1000);
                    if (positionFree(*w.world, player.ducked, to))
                    {
                        for (int a = 0; a < 3; ++a)
                            player.origin[a] = to[a];
                    }
                }
                break;
            }
            default:
                break;
            }
        }
    }

    /// A game between consoles: someone who is not this console's player
    /// (`ghost`: where their own console says they are) walks into things
    /// here too -- the triggers they are in fire, the doors and buttons
    /// they touch go, the lift they stand on starts, what gives way under
    /// them begins to. Every console works that out for every player, so
    /// the same things move on all of them. Nothing is done TO the ghost:
    /// what hurts, pushes or carries someone is their own console's
    /// business.
    inline void logicGhostTouch(LogicWorld &w, PlayerState &ghost)
    {
        const bool sharing = w.sharing;
        w.sharing = false; // (nothing a ghost does is told back)
        for (int i = 0; i < w.recordCount; ++i)
        {
            const LogicRecord &record = w.records[i];
            LogicState &state = w.states[i];
            if (state.dead)
                continue;
            const auto kind = static_cast<LogicKind>(record.kind);
            const bool on = record.brush != kNoBrush && ghost.onGround &&
                            ghost.groundMover == static_cast<int>(record.brush);
            if (kind == LogicKind::PlatRot)
            {
                if (!(record.flags & kLogicToggle) && state.move == MoveState::AtBottom && on)
                    detail::moverStart(w, i, MoveState::GoingUp);
            }
            else if (kind == LogicKind::Breakable)
            {
                if ((record.flags & kLogicPressure) && on && state.timer < 0)
                    state.timer = record.delayMs > 0 ? static_cast<std::int32_t>(record.delayMs) : 100;
            }
            else if (kind != LogicKind::TriggerHurt && kind != LogicKind::TriggerPush &&
                     kind != LogicKind::TriggerTeleport && kind != LogicKind::TriggerGravity &&
                     kind != LogicKind::Friction && kind != LogicKind::AutoSave &&
                     kind != LogicKind::ChangeLevel && kind != LogicKind::Music)
                detail::touchedBy(w, ghost, i);
        }
        w.sharing = sharing;
    }

    /// What USE would work on: the nearest usable thing within reach that the
    /// player is facing. `forward` is the view direction, unit length, Q12.
    /// Returns a record index, or -1.
    inline int logicFindUsable(const LogicWorld &w, const fx *eye, const fx *forward,
                               bool toStrike = false)
    {
        int best = -1;
        fx bestFacing = kUseMinFacing;
        for (int i = 0; i < w.recordCount; ++i)
        {
            const LogicRecord &record = w.records[i];
            const auto kind = static_cast<LogicKind>(record.kind);
            // Asked what a blow would land on, the answer is what breaks;
            // asked what USE would work, it is buttons, the doors that open
            // by hand, and the boxes to pull.
            const bool usable =
                toStrike ? (detail::isBreakable(record) && !(record.flags & kLogicTriggerOnly) &&
                            record.extra[1] != 7)
                         : ((kind == LogicKind::Button && !(record.flags & kLogicTouch)) ||
                            (kind == LogicKind::MomentaryButton && !(record.flags & kLogicTriggerOnly)) ||
                            kind == LogicKind::HealthCharger || kind == LogicKind::SuitCharger ||
                            kind == LogicKind::Pushable ||
                            ((kind == LogicKind::Door || kind == LogicKind::DoorRotating) &&
                             (record.flags & kLogicUseOnly)));
            if (!usable || w.states[i].dead || record.brush == kNoBrush ||
                !w.brushStates[record.brush].active)
                continue;
            // The point of its box nearest the eye.
            const MoverHull &mover = w.movers[record.brush];
            fx to[3];
            for (int a = 0; a < 3; ++a)
            {
                const fx nearest = eye[a] < mover.mins[a]
                                       ? mover.mins[a]
                                       : (eye[a] > mover.maxs[a] ? mover.maxs[a] : eye[a]);
                to[a] = nearest - eye[a];
            }
            const fx distance = fxLength(to);
            if (distance > kUseRange)
                continue;
            // Inside its box counts as facing it.
            fx facing = kFxOne;
            if (distance > 0)
            {
                facing = static_cast<fx>((static_cast<std::int64_t>(to[0]) * forward[0] +
                                          static_cast<std::int64_t>(to[1]) * forward[1] +
                                          static_cast<std::int64_t>(to[2]) * forward[2]) /
                                         distance);
            }
            // (Of two faced as squarely, the first, as the game takes them:
            // c1a0d's microwave is five buttons in one box, each locked until
            // the one before it is pressed -- and pressed, it is gone into the
            // wall, and the next is the first.)
            if (best < 0 ? facing >= bestFacing : facing > bestFacing)
            {
                bestFacing = facing;
                best = i;
            }
        }
        return best;
    }

    /// USE held on a box (CPushable::Use): the player's own speed along the
    /// floor comes to it bit by bit -- a quarter a frame at the game's 60 --
    /// each way only while it is slower than they are. (USE held already has them
    /// at a third of their speed, MoveCommand::use: that keeps it in reach.)
    /// Only from someone with their feet on the ground, and not on the box.
    inline void logicPull(LogicWorld &w, const PlayerState &player, int index, int ms)
    {
        const LogicRecord &record = w.records[index];
        if (!player.onGround || player.groundMover == record.brush ||
            (player.velocity[0] == 0 && player.velocity[1] == 0))
            return;
        fx *v = w.states[index].travel;
        for (int a = 0; a < 2; ++a)
        {
            const fx gain = static_cast<fx>((static_cast<std::int64_t>(player.velocity[a]) * ms * 15) / 1000);
            const fx limit = player.velocity[a] - gain;
            if ((v[a] < 0 ? -v[a] : v[a]) < (limit < 0 ? -limit : limit))
                v[a] += gain;
        }
        detail::boxLimit(record, v);
        detail::boxScrape(w, index);
    }

    // ------------------------------------------------------------------ driving the tram
    //
    // The player at a track train's controls (player.cpp's PFLAG_ONTRAIN and
    // plats.cpp's Use with USE_SET): forward and back step its speed a
    // quarter of its whole at a press, through stopped to backwards. What
    // has them is the train's own state (LogicState::phase).

    /// The track train the player is driving, or -1.
    inline int trainDriven(const LogicWorld &w)
    {
        for (int i = 0; i < w.recordCount; ++i)
        {
            if (w.records[i].kind == static_cast<std::uint8_t>(LogicKind::TrackTrain) &&
                (w.states[i].phase & detail::kTrainDriven))
                return i;
        }
        return -1;
    }

    namespace detail
    {
        /// CFuncTrackTrain::OnControls: whether the player stands where train
        /// `index` is driven from -- its func_traincontrols, in its own
        /// space as it turns; with none, anywhere in it and up to 72 units
        /// above.
        inline bool trainOnControls(const LogicWorld &w, const PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            const LogicState &state = w.states[index];
            if (record.brush == kNoBrush || (record.flags & kLogicSelfDriven) || state.running || state.dead ||
                state.move == kCarried)
                return false;
            const Brush &built = w.brushes[record.brush];
            const bool own = record.mins[0] != 0 || record.mins[1] != 0 || record.mins[2] != 0 ||
                             record.maxs[0] != 0 || record.maxs[1] != 0 || record.maxs[2] != 0;
            fx local[3];
            toMoverSpace(w.movers[record.brush], player.origin, local);
            for (int a = 0; a < 3; ++a)
            {
                const fx lo = fxFromInt(own ? record.mins[a] : built.mins[a]) - built.origin[a];
                const fx hi = fxFromInt(own ? record.maxs[a] : built.maxs[a] + (a == 2 ? 72 : 0)) - built.origin[a];
                if (local[a] < lo || local[a] > hi)
                    return false;
            }
            return true;
        }
    } // namespace detail

    /// Let go of the train, if the player is driving one. It goes on as it
    /// was going.
    inline void trainRelease(LogicWorld &w)
    {
        for (int i = 0; i < w.recordCount; ++i)
        {
            if (w.records[i].kind == static_cast<std::uint8_t>(LogicKind::TrackTrain))
                w.states[i].phase &= ~detail::kTrainDriven;
        }
    }

    /// USE: standing on a train at its controls, take them; having them,
    /// let go. False if neither: USE is something else's.
    inline bool trainUse(LogicWorld &w, const PlayerState &player)
    {
        if (trainDriven(w) >= 0)
        {
            trainRelease(w);
            return true;
        }
        if (!player.onGround || player.groundMover < 0)
            return false;
        for (int i = 0; i < w.recordCount; ++i)
        {
            const LogicRecord &record = w.records[i];
            if (record.kind != static_cast<std::uint8_t>(LogicKind::TrackTrain) ||
                record.brush != static_cast<std::uint16_t>(player.groundMover) || !detail::trainOnControls(w, player, i))
                continue;
            w.states[i].phase |= detail::kTrainDriven;
            detail::trainShow(w, i);
            if (w.heard != nullptr && w.sounds != nullptr && w.sounds[i].unlocked != kNoSound)
                w.heard->add(w.sounds[i].unlocked, nullptr, 102, 0);
            return true;
        }
        return false;
    }

    /// A frame of the player at the controls. `step` is +1 for a press of
    /// forward, -1 of back, 0 for neither. They let go by jumping, by
    /// stepping sideways, by leaving the train, or by dying; and the train
    /// lets go of them when its track takes it over.
    inline void trainControl(LogicWorld &w, const PlayerState &player, int step, bool side, bool jump)
    {
        const int index = trainDriven(w);
        if (index < 0)
            return;
        const LogicRecord &record = w.records[index];
        LogicState &state = w.states[index];
        if (player.health <= 0 || jump || side || !player.onGround ||
            player.groundMover != static_cast<int>(record.brush) || state.running || state.move == detail::kCarried)
        {
            trainRelease(w);
            return;
        }
        if (step == 0 || record.speed <= 0)
            return;
        // (The game's sum: what quarter it is at, as a whole number, and a
        // quarter on or back from there.)
        int quarter = static_cast<int>((static_cast<std::int64_t>(state.speed) * 4) / record.speed) + step;
        quarter = quarter > 4 ? 4 : (quarter < -4 ? -4 : quarter);
        if ((record.flags & kLogicForwardOnly) && quarter < 0)
            quarter = 0;
        const fx was = state.speed;
        state.speed = static_cast<fx>((static_cast<std::int64_t>(record.speed) * quarter) / 4);
        detail::trainShow(w, index);
        if (was != 0 && state.speed == 0)
            detail::trainBrakes(w, index);
        else if (was == 0 && state.speed != 0)
            detail::trainStarts(w, index);
    }

    /// What the display shows of the train the player drives: the frame of
    /// the game's picture (detail::trainPicture), as of their last press;
    /// -1 if they drive none.
    inline int trainSpeedStep(const LogicWorld &w)
    {
        const int index = trainDriven(w);
        return index < 0 ? -1 : (w.states[index].phase & detail::kTrainShownMask) >> detail::kTrainShownShift;
    }
} // namespace hldata

// (The env_ entities that are only seen and felt are used from here and are
// in HlEffects.hpp, which comes with the monsters: an explosion hurts.)
#include "HlMonsters.hpp"
