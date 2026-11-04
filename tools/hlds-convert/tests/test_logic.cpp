// The entity logic: shared/HlLogic.hpp driven on the host, on maps cooked from
// the synthetic room.
//
// Each scenario is one rule a real map depends on, taken from the SDK: a
// button that starts a multi_manager that opens a door half a second later; a
// door with a name that will NOT open when walked into; a door that turns
// round when the player is in the way; a lift that carries the player up. The
// maps are a few brushes and a few lines of entity text, so that what is
// being tested is the rule and nothing else.

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstring>
#include <functional>
#include <random>

#include "HlLevelReader.hpp"
#include "HlFrameClock.hpp"
#include "HlLogic.hpp"
#include "HlMonsters.hpp"
#include "HlTransition.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/Cooker.hpp"
#include "hlds/EntityCook.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    double toUnits(fx v) { return v / 4096.0; }

    /// A cooked map with the logic set up the way the DS sets it up.
    struct Game
    {
        BspFile bsp;
        std::vector<std::uint8_t> bytes;
        LevelView view;
        MoveWorld move;
        std::vector<MoverHull> movers;
        std::vector<BrushState> brushStates;
        std::vector<LogicState> states;
        std::vector<LadderVolume> ladders;
        LogicWorld logic;
        PlayerState player;
        bool ok = false;

        void tick(int ms, int times = 1)
        {
            for (int i = 0; i < times; ++i)
                logicTick(logic, player, ms);
        }
        /// Logic and movement together, as a frame does them.
        void frame(const MoveCommand &cmd, int times = 1)
        {
            for (int i = 0; i < times; ++i)
            {
                logicTick(logic, player, 33);
                playerMove(player, cmd, move, kPlayerTick);
            }
        }
        /// The record whose brush is model `model`.
        int recordOfModel(int model) const
        {
            for (std::uint32_t i = 0; i < view.logicCount; ++i)
            {
                if (view.logic[i].brush != kNoBrush &&
                    view.brushes[view.logic[i].brush].model == model)
                    return static_cast<int>(i);
            }
            return -1;
        }
        int recordOfKind(LogicKind kind, int nth = 0) const
        {
            for (std::uint32_t i = 0; i < view.logicCount; ++i)
            {
                if (view.logic[i].kind == static_cast<std::uint8_t>(kind) && nth-- == 0)
                    return static_cast<int>(i);
            }
            return -1;
        }
        const BrushState &brushOfModel(int model) const
        {
            for (std::uint32_t i = 0; i < view.brushCount; ++i)
            {
                if (view.brushes[i].model == model)
                    return brushStates[i];
            }
            return brushStates[0];
        }
        void use(int record) { logicUse(logic, player, record, -1, kUseToggle); }
    };

    void build(Game &g, const BoxRoom &room)
    {
        std::string error;
        CookedLevel level;
        CookReport report;
        g.ok = g.bsp.parse(buildBoxRoom(room), error) &&
               cookLevel(g.bsp, CookOptions(), TexturePlan(), level, report, error) &&
               writeLevel(level, g.bytes, error) &&
               g.view.open(g.bytes.data(), static_cast<std::uint32_t>(g.bytes.size())) ==
                   LevelError::Ok;
        if (!g.ok)
        {
            std::fprintf(stderr, "game failed: %s\n", error.c_str());
            return;
        }
        const LevelView &v = g.view;
        g.move.standing = Hull{v.clipNodes, v.planes, v.planeTypes, v.models[0].clipStanding};
        g.move.crouched = Hull{v.clipNodes, v.planes, v.planeTypes, v.models[0].clipCrouched};
        g.movers.resize(v.brushCount);
        g.brushStates.resize(v.brushCount);
        g.states.resize(v.logicCount);
        for (std::uint32_t i = 0; i < v.brushCount; ++i)
        {
            const Model &model = v.models[v.brushes[i].model];
            g.movers[i].standing = Hull{v.clipNodes, v.planes, v.planeTypes, model.clipStanding};
            g.movers[i].crouched = Hull{v.clipNodes, v.planes, v.planeTypes, model.clipCrouched};
        }
        g.move.movers = g.movers.data();
        g.move.moverCount = static_cast<int>(g.movers.size());

        g.logic.records = v.logic;
        g.logic.recordCount = static_cast<int>(v.logicCount);
        g.logic.aux = v.logicAux;
        g.logic.corners = v.corners;
        g.logic.brushes = v.brushes;
        g.logic.brushCount = static_cast<int>(v.brushCount);
        g.logic.states = g.states.data();
        g.logic.brushStates = g.brushStates.data();
        g.logic.movers = g.movers.data();
        g.logic.world = &g.move;
        int ladderCount = 0;
        for (std::uint32_t i = 0; i < v.logicCount; ++i)
            ladderCount += v.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Ladder);
        g.ladders.resize(ladderCount + 1);
        g.move.ladders = g.ladders.data();
        g.move.ladderCount = ladderCount;
        g.logic.ladders = g.ladders.data();
        logicInit(g.logic);

        g.player = PlayerState();
        g.player.origin[2] = fxFromInt(40);
        MoveCommand idle;
        for (int i = 0; i < 30; ++i)
            playerMove(g.player, idle, g.move, kPlayerTick);
    }

    BrushBox brush(const char *cls, const char *keys, float x0, float y0, float z0, float x1,
                   float y1, float z1)
    {
        BrushBox b;
        b.className = cls;
        b.keys = keys;
        b.mins[0] = x0;
        b.mins[1] = y0;
        b.mins[2] = z0;
        b.maxs[0] = x1;
        b.maxs[1] = y1;
        b.maxs[2] = z1;
        return b;
    }

    // ---- the converter's arithmetic ---------------------------------------

    void testCookRules()
    {
        std::vector<Entity> e;
        std::string error;
        auto dir = [&](const char *keys, double *out)
        {
            parseEntities(std::string("{") + keys + "}", e, error);
            moverDirection(e[0], out);
        };
        double d[3];
        dir("\"angle\" \"-1\"", d);
        check(d[0] == 0 && d[1] == 0 && d[2] == 1, "angle -1 is up");
        dir("\"angle\" \"-2\"", d);
        check(d[2] == -1, "angle -2 is down");
        dir("\"angle\" \"90\"", d);
        check(d[0] == 0 && std::fabs(d[1] - 1) < 1e-12 && d[2] == 0, "angle 90 is exactly +Y");
        dir("\"angles\" \"0 180 0\"", d);
        check(std::fabs(d[0] + 1) < 1e-12 && d[1] == 0, "angles 0 180 0 is -X");
        dir("\"angles\" \"0 -1 0\"", d);
        check(d[2] == 1, "the three-number form of -1 is up too");
        dir("", d);
        check(d[0] == 1 && d[1] == 0 && d[2] == 0, "no angle at all is +X");

        // The engine's travel: the size along the direction, less 2, less lip.
        const double up[3] = {0, 0, 1};
        const double size[3] = {64, 8, 96};
        double move[3];
        moverTravel(up, size, 8, move);
        check(move[0] == 0 && move[1] == 0 && move[2] == 86, "a 96-high door with lip 8 rises 86");
        const double side[3] = {-1, 0, 0};
        moverTravel(side, size, 0, move);
        check(move[0] == -62 && move[2] == 0, "sideways it slides its width less 2");
    }

    void testCookedRecords()
    {
        BoxRoom room;
        room.brushes = {
            brush("func_door",
                  "\"targetname\" \"door1\"\n\"angle\" \"-1\"\n\"speed\" \"100\"\n\"wait\" \"2\"\n"
                  "\"lip\" \"4\"\n\"netname\" \"shut\"\n\"spawnflags\" \"32\"\n\"dmg\" \"5\"\n",
                  40, -32, 0, 48, 32, 64),
            brush("func_button", "\"target\" \"mm1\"\n\"spawnflags\" \"1\"\n", -100, 0, 40, -96, 8, 48),
            brush("func_button", "\"target\" \"door1\"\n\"angle\" \"90\"\n\"master\" \"ms1\"\n",
                  -100, 20, 40, -96, 28, 48),
            brush("trigger_once", "\"target\" \"door1\"\n\"delay\" \"1.5\"\n\"killtarget\" \"gone\"\n",
                  -64, -64, 0, -32, -32, 64),
            brush("trigger_multiple", "\"target\" \"x\"\n\"spawnflags\" \"2\"\n", 0, 0, 0, 8, 8, 8),
            brush("func_wall", "\"targetname\" \"gone\"\n", 80, 0, 0, 88, 8, 64),
            brush("func_wall", "", 80, 20, 0, 88, 28, 64),
        };
        room.extraEntities =
            "{\n\"classname\" \"multi_manager\"\n\"targetname\" \"mm1\"\n\"door1\" \"0.5\"\n"
            "\"later\" \"2\"\n\"door1#1\" \"0\"\n\"spawnflags\" \"1\"\n}\n"
            "{\n\"classname\" \"multisource\"\n\"targetname\" \"ms1\"\n\"target\" \"out\"\n}\n"
            "{\n\"classname\" \"trigger_relay\"\n\"targetname\" \"r\"\n\"target\" \"ms1\"\n"
            "\"triggerstate\" \"1\"\n\"spawnflags\" \"1\"\n}\n"
            "{\n\"classname\" \"trigger_auto\"\n\"target\" \"door1\"\n\"triggerstate\" \"0\"\n}\n"
            "{\n\"classname\" \"monster_scientist\"\n}\n{\n\"classname\" \"monster_scientist\"\n}\n"
            "{\n\"classname\" \"env_beam\"\n}\n";
        Game g;
        build(g, room);
        check(g.ok, "the records room cooks");
        if (!g.ok)
            return;
        const LevelView &v = g.view;

        const LogicRecord &door = v.logic[g.recordOfModel(1)];
        check(door.kind == static_cast<std::uint8_t>(LogicKind::Door) &&
                  door.speed == fxFromInt(100) && door.waitMs == 2000 && door.damage == 5,
              "the door's numbers");
        check(door.move[0] == 0 && door.move[1] == 0 && door.move[2] == fxFromInt(64 - 2 - 4),
              "its travel: up by its height less 2 and its lip");
        check((door.flags & kLogicToggle) && !(door.flags & kLogicTouch) &&
                  !(door.flags & kLogicStartOpen),
              "flag 32 makes it a toggle; having a name means walking into it does nothing");
        check(door.name != 0 && door.netName != 0 && door.netName != door.name, "its names");
        check(v.brushes[door.brush].logic == g.recordOfModel(1), "brush and record point at each other");

        const LogicRecord &plain = v.logic[g.recordOfModel(2)];
        check(plain.kind == static_cast<std::uint8_t>(LogicKind::Button) && plain.waitMs == 1000 &&
                  plain.speed == fxFromInt(40) && plain.move[0] == 0 && plain.move[1] == 0 &&
                  plain.move[2] == 0,
              "a don't-move button: default wait 1 s, speed 40, no travel");
        const LogicRecord &moving = v.logic[g.recordOfModel(3)];
        // 8 deep along Y: 8 - 2 - the default lip of 4.
        check(moving.move[1] == fxFromInt(2) && moving.move[0] == 0 && moving.master != 0,
              "a moving button travels its depth less 2 and the default lip of 4");
        check(moving.target == door.name, "a target is the id of the name it names");

        // A trigger has no brush record -- it is neither seen nor solid -- so
        // it is found by kind, not by model.
        const LogicRecord &once = v.logic[g.recordOfKind(LogicKind::TriggerOnce)];
        check(once.kind == static_cast<std::uint8_t>(LogicKind::TriggerOnce) &&
                  once.delayMs == 1500 && once.waitMs == -1 && once.brush == kNoBrush,
              "a trigger: delay in ms, never re-arms, has no brush");
        check(once.mins[0] == -64 && once.maxs[0] == -32 && once.maxs[2] == 64, "its volume");
        check(g.recordOfKind(LogicKind::TriggerMultiple) == -1,
              "a trigger that ignores the player gets no record");

        const LogicRecord &gone = v.logic[g.recordOfModel(6)];
        check(gone.kind == static_cast<std::uint8_t>(LogicKind::Static) && gone.name == once.killTarget,
              "a named wall gets a record so that a killtarget can find it");
        check(g.recordOfModel(7) == -1, "an unnamed wall needs none");

        const LogicRecord &manager = v.logic[g.recordOfKind(LogicKind::MultiManager)];
        check(manager.auxCount == 3 && (manager.flags & kLogicThread) && manager.target == 0,
              "the multi_manager has three outputs and no target of its own");
        const LogicAux *out = v.logicAux + manager.firstAux;
        check(out[0].delayMs == 0 && out[0].target == door.name && out[1].delayMs == 500 &&
                  out[1].target == door.name && out[2].delayMs == 2000,
              "sorted by delay, with the editor's #1 suffix taken off");

        const int ms = g.recordOfKind(LogicKind::MultiSource);
        const LogicRecord &source = v.logic[ms];
        check(source.auxCount == 1 &&
                  v.logicAux[source.firstAux].target == g.recordOfKind(LogicKind::TriggerRelay),
              "the multisource's one input is the relay that targets it");
        check(g.states[g.recordOfModel(3)].master == ms, "and it is the second button's master");

        const LogicRecord &relay = v.logic[g.recordOfKind(LogicKind::TriggerRelay)];
        check(relay.useType == kUseOn && (relay.flags & kLogicFireOnce), "the relay: on, once");
        check(v.logic[g.recordOfKind(LogicKind::TriggerAuto)].useType == kUseOff, "trigger_auto: off");

        // The report of what is not handled.
        BspFile bsp;
        std::string error;
        CookedLevel level;
        CookReport report;
        bsp.parse(buildBoxRoom(room), error);
        cookLevel(bsp, CookOptions(), TexturePlan(), level, report, error);
        // The trigger that ignores the player is in the list too: what would
        // fire it (a monster) does not exist yet.
        check(report.unsupported.size() == 3 && report.unsupported["monster_scientist"] == 2 &&
                  report.unsupported["env_beam"] == 1 &&
                  report.unsupported["trigger_multiple"] == 1,
              "classes with no handling are counted, by class");
        check(report.logicRecords == static_cast<int>(v.logicCount), "and the records are counted");
    }

    // ---- behaviour ----------------------------------------------------------

    /// A button starts a multi_manager; the manager opens a door half a second
    /// later; the door goes up, waits, and comes back.
    void testButtonManagerDoor()
    {
        BoxRoom room;
        room.brushes = {
            brush("func_door",
                  "\"targetname\" \"door1\"\n\"angle\" \"-1\"\n\"speed\" \"100\"\n\"wait\" \"2\"\n"
                  "\"target\" \"relay1\"\n",
                  40, -32, 0, 48, 32, 64),
            brush("func_button", "\"target\" \"mm1\"\n\"spawnflags\" \"1\"\n\"wait\" \"3\"\n",
                  -100, 0, 40, -96, 8, 48),
            brush("func_wall_toggle", "\"targetname\" \"lamp\"\n", 80, 0, 0, 88, 8, 16),
        };
        room.extraEntities =
            "{\n\"classname\" \"multi_manager\"\n\"targetname\" \"mm1\"\n\"door1\" \"0.5\"\n}\n"
            // "triggerstate" 2 is toggle. Left out, a relay sends OFF -- the
            // SDK's default -- and the lamp would be switched off twice.
            "{\n\"classname\" \"trigger_relay\"\n\"targetname\" \"relay1\"\n\"target\" \"lamp\"\n"
            "\"triggerstate\" \"2\"\n}\n";
        Game g;
        build(g, room);
        check(g.ok, "button room");
        if (!g.ok)
            return;
        const int door = g.recordOfModel(1);
        const int button = g.recordOfModel(2);
        const BrushState &slab = g.brushOfModel(1);
        const BrushState &lamp = g.brushOfModel(3);

        g.tick(33, 10);
        check(g.states[door].move == MoveState::AtBottom && slab.pos[2] == 0, "nothing moves unasked");

        g.use(button);
        g.tick(33);
        check(g.states[button].move == MoveState::AtTop, "a don't-move button is 'in' at once");
        g.tick(33, 12); // about 0.43 s
        check(g.states[door].move == MoveState::AtBottom, "the manager's half second is not up");
        g.tick(33, 4);
        check(g.states[door].move == MoveState::GoingUp, "after it, the door starts");
        check(lamp.active, "the door's own target has not fired yet");

        // 62 units at 100 a second: 0.62 s.
        g.tick(33, 10);
        check(g.states[door].move == MoveState::GoingUp && slab.pos[2] > fxFromInt(20) &&
                  slab.pos[2] < fxFromInt(50),
              "half way up after a third of a second");
        g.tick(33, 12);
        check(g.states[door].move == MoveState::AtTop && slab.pos[2] == fxFromInt(62),
              "it stops exactly at the top of its travel");
        check(!lamp.active, "reaching the top fires its target (the relay switches the lamp)");
        const std::size_t doorBrush = g.view.logic[door].brush;
        check(g.movers[doorBrush].origin[2] == fxFromInt(62) &&
                  g.movers[doorBrush].mins[2] == fxFromInt(62),
              "and the collision has moved with it");

        // Using the button again while it is in does nothing.
        g.use(button);
        check(g.states[button].move == MoveState::AtTop, "a pressed button ignores another press");

        g.tick(33, 62); // its 2 s wait, then it starts down
        check(g.states[door].move == MoveState::GoingDown, "after its wait it comes back");
        g.tick(33, 22);
        check(g.states[door].move == MoveState::AtBottom && slab.pos[2] == 0, "and shuts");
        check(lamp.active, "shutting fires the target again: the lamp is back");

        check(g.states[button].move == MoveState::AtBottom, "the button popped out after its 3 s");
        check(g.logic.eventsDropped == 0, "no event was lost");
    }

    /// A door that opens and shuts in frames where the queue of what is
    /// heard is already full: its move sound is still started, and still
    /// stopped -- lost, the stop would leave it going round until the map
    /// changes.
    void testMoverLoopFullQueue()
    {
        BoxRoom room;
        room.brushes = {
            brush("func_door", "\"targetname\" \"door1\"\n\"angle\" \"-1\"\n\"speed\" \"100\"\n\"wait\" \"-1\"\n",
                  40, -32, 0, 48, 32, 64),
        };
        Game g;
        build(g, room);
        check(g.ok, "full-queue door room");
        if (!g.ok)
            return;
        const int door = g.recordOfModel(1);
        // (Its sounds, as a map with noises gives them: 1 moving, 2 stopping.)
        std::vector<LogicSound> sounds(g.view.logicCount);
        for (LogicSound &s : sounds)
            s = LogicSound{kNoSound, kNoSound, kNoSound, kNoSound, kNoSound, kNoSound};
        sounds[door].move = 1;
        sounds[door].stop = 2;
        SoundQueue heard;
        g.logic.heard = &heard;
        g.logic.sounds = sounds.data();

        const fx at[3] = {0, 0, 0};
        // A frame: the fighting fills the queue, then the logic has its turn.
        auto busy = [&](bool use)
        {
            heard.count = 0;
            for (int i = 0; i < kMaxSoundEvents; ++i)
                heard.add(static_cast<std::uint16_t>(10 + i), at, 127, kRadiusLarge);
            if (use)
                g.use(door);
            g.tick(33);
        };
        auto loop = [&](std::uint8_t flag)
        {
            for (int i = 0; i < heard.count; ++i)
            {
                if ((heard.events[i].flags & flag) && heard.events[i].tag == door)
                    return true;
            }
            return false;
        };

        busy(true);
        check(g.states[door].move == MoveState::GoingUp && loop(kSoundLoopOn),
              "a door set off in a busy frame starts its move sound");
        bool stopped = false;
        for (int n = 0; n < 60 && g.states[door].move == MoveState::GoingUp; ++n)
        {
            busy(false);
            stopped = stopped || loop(kSoundLoopOff);
        }
        check(g.states[door].move == MoveState::AtTop && stopped,
              "... and, arrived in a busy frame, stops it");
        check(heard.count == kMaxSoundEvents, "the queue holds no more than it does");
    }

    /// What the sound looks at in case a mover's stop was never heard: is
    /// it still moving? Never "no" while it is -- that would cut its sound
    /// off, and nothing would start it again.
    void testMoverSounding()
    {
        BoxRoom room;
        room.brushes = {
            brush("func_door", "\"targetname\" \"door1\"\n\"angle\" \"-1\"\n\"speed\" \"100\"\n\"wait\" \"1\"\n",
                  40, -32, 0, 48, 32, 64),
            // 2: a train that waits to be told, on a straight route: through
            //    p2 without waiting, half a second at p3, held at p4.
            brush("func_train", "\"targetname\" \"t1\"\n\"target\" \"p1\"\n\"speed\" \"100\"\n", -80, -80, 0,
                  -40, -40, 8),
        };
        room.extraEntities =
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"p1\"\n\"target\" \"p2\"\n\"origin\" \"-60 -60 4\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"p2\"\n\"target\" \"p3\"\n\"origin\" \"-20 -60 4\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"p3\"\n\"target\" \"p4\"\n\"origin\" \"20 -60 4\"\n"
            "\"wait\" \"0.5\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"p4\"\n\"target\" \"p1\"\n\"origin\" \"60 -60 4\"\n"
            "\"spawnflags\" \"1\"\n}\n";
        Game g;
        build(g, room);
        check(g.ok, "mover-sound room");
        if (!g.ok)
            return;
        // (Out of the way of both.)
        g.player.origin[0] = fxFromInt(-100);
        g.player.origin[1] = fxFromInt(100);
        const int door = g.recordOfModel(1);
        const int train = g.recordOfModel(2);
        const LogicRecord &doorRecord = g.view.logic[door];
        const LogicRecord &trainRecord = g.view.logic[train];
        check(moverLoops(static_cast<LogicKind>(doorRecord.kind)) &&
                  moverLoops(static_cast<LogicKind>(trainRecord.kind)) && !moverLoops(LogicKind::Button) &&
                  !moverLoops(LogicKind::TrackTrain),
              "doors and trains are heard as they move; a button and a track train are not that way");
        auto doorOn = [&] { return moverSounding(doorRecord, g.states[door]); };
        auto trainOn = [&] { return moverSounding(trainRecord, g.states[train]); };

        // The door: heard opening and shutting, not at either end.
        check(!doorOn(), "a shut door is not heard");
        g.use(door);
        g.tick(33);
        check(g.states[door].move == MoveState::GoingUp && doorOn(), "opening, it is");
        g.tick(33, 30);
        check(g.states[door].move == MoveState::AtTop && !doorOn(), "open, it is not");
        g.tick(33, 32);
        check(g.states[door].move == MoveState::GoingDown && doorOn(), "shutting after its wait, it is");
        g.tick(33, 30);
        check(g.states[door].move == MoveState::AtBottom && !doorOn(), "shut again, it is not");

        // The train: from setting off to the corner it waits at, every frame
        // -- the one it is at p2 too.
        const std::uint16_t p1 = trainRecord.firstAux;
        const std::uint16_t p2 = g.view.corners[p1].next;
        const std::uint16_t p3 = g.view.corners[p2].next;
        g.tick(33, 10);
        check(!trainOn(), "a train that has not set off is not heard");
        logicFire(g.logic, g.player, trainRecord.name, -1, kUseToggle, 0);
        bool off = false, always = true, throughP2 = false, waited = false;
        for (int n = 0; n < 60 && !waited; ++n)
        {
            g.tick(33);
            const LogicState &s = g.states[train];
            off = off || s.move == MoveState::GoingUp;
            waited = s.move == MoveState::AtBottom && static_cast<std::uint16_t>(s.bits) == p3;
            throughP2 = throughP2 || (static_cast<std::uint16_t>(s.bits) == p2 && s.move == MoveState::AtBottom);
            if (off && !waited)
                always = always && trainOn();
        }
        check(off && waited && throughP2, "the train sets off, goes through p2 and comes to p3");
        check(always, "heard all the way, through the corner it does not stop at");
        check(!trainOn(), "waiting at a corner, it is not");
        bool left = false;
        for (int n = 0; n < 30 && !left; ++n)
        {
            g.tick(33);
            left = g.states[train].move == MoveState::GoingUp;
        }
        check(left && trainOn(), "its wait over, it is again");
        for (int n = 0; n < 30 && g.states[train].running; ++n)
            g.tick(33);
        check(!g.states[train].running && !trainOn(), "held at a corner until used, it is not");
        g.use(train);
        g.tick(33);
        check(trainOn(), "used, it is");
        g.use(train);
        check(!trainOn(), "used again on the way, it stops, and is not");

        // The other two kinds go as a door does.
        LogicRecord plat{};
        plat.kind = static_cast<std::uint8_t>(LogicKind::PlatRot);
        LogicState platState;
        platState.move = MoveState::GoingDown;
        check(moverLoops(LogicKind::PlatRot) && moverLoops(LogicKind::DoorRotating) && moverSounding(plat, platState),
              "a turning platform going down is heard");
        platState.move = MoveState::AtTop;
        check(!moverSounding(plat, platState), "... and at the top is not");
        platState.move = MoveState::GoingUp;
        platState.dead = true;
        check(!moverSounding(plat, platState), "and nothing that is gone is");
    }

    /// A train is heard setting off whenever it sets off from rest -- as
    /// the map begins, from a corner it waited at, used again after being
    /// stopped on its way, told to go, come from the map before -- and not
    /// again at a corner it goes straight through: its sound goes on there.
    void testTrainSetsOffHeard()
    {
        BoxRoom room;
        room.brushes = {
            // 1: no name: it sets off by itself. Through p2, half a second
            //    at p3, held at p4.
            brush("func_train", "\"target\" \"p1\"\n\"speed\" \"100\"\n", -80, -80, 0, -40, -40, 8),
            // 2: named: it waits to be told.
            brush("func_train", "\"targetname\" \"t2\"\n\"target\" \"q1\"\n\"speed\" \"100\"\n", -80, 40, 0, -40, 80,
                  8),
        };
        room.extraEntities =
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"p1\"\n\"target\" \"p2\"\n\"origin\" \"-60 -60 4\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"p2\"\n\"target\" \"p3\"\n\"origin\" \"-20 -60 4\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"p3\"\n\"target\" \"p4\"\n\"origin\" \"20 -60 4\"\n"
            "\"wait\" \"0.5\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"p4\"\n\"target\" \"p1\"\n\"origin\" \"60 -60 4\"\n"
            "\"spawnflags\" \"1\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"q1\"\n\"target\" \"q2\"\n\"origin\" \"-60 60 4\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"q2\"\n\"origin\" \"60 60 4\"\n}\n";
        Game g;
        build(g, room);
        check(g.ok, "set-off room");
        if (!g.ok)
            return;
        g.player.origin[0] = fxFromInt(-100);
        g.player.origin[1] = fxFromInt(0);
        const int train = g.recordOfModel(1);
        const int named = g.recordOfModel(2);
        std::vector<LogicSound> sounds(g.view.logicCount);
        for (LogicSound &s : sounds)
            s = LogicSound{kNoSound, kNoSound, kNoSound, kNoSound, kNoSound, kNoSound};
        sounds[train].move = sounds[named].move = 1;
        sounds[train].stop = sounds[named].stop = 2;
        SoundQueue heard;
        g.logic.heard = &heard;
        g.logic.sounds = sounds.data();
        // How many times `who`'s move sound was started since the queue was
        // last emptied.
        auto starts = [&](int who)
        {
            int n = 0;
            for (int i = 0; i < heard.count; ++i)
                n += (heard.events[i].flags & kSoundLoopOn) && heard.events[i].tag == who;
            return n;
        };
        // Ticks until `done`, counting `who`'s starts on the way.
        auto run = [&](int who, int limit, const std::function<bool()> &done)
        {
            int n = 0;
            for (int i = 0; i < limit && !done(); ++i)
            {
                heard.count = 0;
                g.tick(33);
                n += starts(who);
            }
            return n;
        };
        const LogicState &s = g.states[train];
        const std::uint16_t p1 = g.view.logic[train].firstAux;
        const std::uint16_t p2 = g.view.corners[p1].next;
        const std::uint16_t p3 = g.view.corners[p2].next;
        auto corner = [&] { return static_cast<std::uint16_t>(s.bits); };

        check(run(train, 10, [&] { return s.move == MoveState::GoingUp; }) == 1,
              "a train with no name is heard setting off as the map begins");
        check(run(train, 30, [&] { return corner() == p2 && s.move == MoveState::GoingUp; }) == 0,
              "... and not again at the corner it goes straight through");
        // On its way from p2, which has no wait: stopped, then used again.
        g.tick(33, 2);
        heard.count = 0;
        g.use(train);
        bool loopOff = false, stopSound = false;
        for (int i = 0; i < heard.count; ++i)
        {
            const SoundEvent &e = heard.events[i];
            loopOff = loopOff || ((e.flags & kSoundLoopOff) && e.tag == train);
            stopSound = stopSound || (!(e.flags & (kSoundLoopOn | kSoundLoopOff)) && e.sound == 2);
        }
        check(!s.running, "used on its way, it stops");
        check(loopOff && stopSound, "... its move sound stopped, and its stop sound heard");
        heard.count = 0;
        g.use(train);
        check(s.running && starts(train) == 1, "used again, it is heard setting off");
        check(run(train, 30, [&] { return s.move == MoveState::AtBottom && corner() == p3; }) == 0,
              "and goes on to the next corner without being heard setting off twice");
        check(run(train, 30, [&] { return s.move == MoveState::GoingUp; }) == 1,
              "it is heard leaving the corner it waited at");
        run(train, 30, [&] { return !s.running; });
        heard.count = 0;
        g.use(train);
        check(s.running && starts(train) == 1, "held at a corner until used, used, it is heard");

        // Told to go.
        heard.count = 0;
        logicFire(g.logic, g.player, g.view.logic[named].name, -1, kUseToggle, 0);
        check(g.states[named].running && starts(named) == 1, "a train with a name is heard when told to go");
        // Come from the map before, on its way to a corner: set off again
        // there, it is heard too.
        g.use(named);
        LogicState &n = g.states[named];
        n.bits = static_cast<std::uint32_t>(g.view.corners[g.view.logic[named].firstAux].next) | hldata::detail::kTrainHeading;
        heard.count = 0;
        g.use(named);
        check(n.running && n.move == MoveState::GoingUp && starts(named) == 1,
              "one come from another map, heading for a corner, is heard setting off");
    }

    /// Doors and the player: touch, names, being in the way, being carried.
    void testDoorsAndThePlayer()
    {
        BoxRoom room;
        room.brushes = {
            // 1: a door with no name, across the player's path to +x.
            brush("func_door", "\"angle\" \"-1\"\n\"speed\" \"200\"\n\"wait\" \"1\"\n", 40, -64, 0, 48, 64, 100),
            // 2: a named door, across the path to -x.
            brush("func_door", "\"targetname\" \"named\"\n\"angle\" \"-1\"\n\"speed\" \"200\"\n\"wait\" \"1\"\n",
                  -48, -64, 0, -40, 64, 100),
            // 3: a platform the player can stand on, that rises: a door that
            //    goes up and stays.
            brush("func_door", "\"targetname\" \"lift\"\n\"angle\" \"-1\"\n\"speed\" \"50\"\n\"wait\" \"-1\"\n"
                               "\"lip\" \"-40\"\n",
                  -30, 80, 0, 30, 112, 8),
        };
        Game g;
        build(g, room);
        check(g.ok, "door room");
        if (!g.ok)
            return;
        const int touchDoor = g.recordOfModel(1);
        const int namedDoor = g.recordOfModel(2);
        const int lift = g.recordOfModel(3);
        check((g.view.logic[touchDoor].flags & kLogicTouch) &&
                  !(g.view.logic[namedDoor].flags & kLogicTouch),
              "only the nameless door answers to touch");

        // Walk at the named door: it stays shut and stops the player.
        MoveCommand cmd;
        cmd.forward = 1;
        cmd.forwardXY[0] = -kFxOne;
        cmd.forwardXY[1] = 0;
        g.frame(cmd, 40);
        check(g.states[namedDoor].move == MoveState::AtBottom, "a named door ignores being walked into");
        check(toUnits(g.player.origin[0]) > -24.1 && toUnits(g.player.origin[0]) < -23.9,
              "and stops the player");

        // Walk at the nameless one: it opens, and the player goes through.
        cmd.forwardXY[0] = kFxOne;
        g.frame(cmd, 25);
        check(g.states[touchDoor].move != MoveState::AtBottom, "a nameless door opens when touched");
        g.frame(cmd, 60);
        check(toUnits(g.player.origin[0]) > 80, "and the player walks through the opening");

        // Wait for it to shut, open it again by name-less touch from the far
        // side, and this time stand in the doorway once it is fully up. It
        // comes down after its wait, meets the player, cannot push them
        // through the floor, and goes back up.
        cmd.forward = 0;
        g.frame(cmd, 120);
        check(g.states[touchDoor].move == MoveState::AtBottom, "left alone, it shuts");
        logicUse(g.logic, g.player, touchDoor, -1, kUseToggle);
        for (int i = 0; i < 60 && g.states[touchDoor].move != MoveState::AtTop; ++i)
            g.frame(cmd);
        check(g.states[touchDoor].move == MoveState::AtTop, "and opens again when used");
        g.player.origin[0] = fxFromInt(44);
        g.player.origin[2] = fxFromInt(40);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        int reversals = 0;
        double lowest = 1e9;
        MoveState last = g.states[touchDoor].move;
        for (int i = 0; i < 200; ++i)
        {
            g.frame(cmd);
            if (last == MoveState::GoingDown && g.states[touchDoor].move == MoveState::GoingUp)
                ++reversals;
            last = g.states[touchDoor].move;
            lowest = std::min(lowest, toUnits(g.brushOfModel(1).pos[2]));
            check(hullPositionFree(g.move.standing, g.player.origin), "never pushed into the world");
        }
        check(reversals >= 2, "a door that meets the player turns round, every time it tries");
        // The player's head is 72 above the floor; the door's underside is at
        // its position.
        check(lowest > 70, "and never comes down past the top of their head");
        check(std::fabs(toUnits(g.player.origin[0]) - 44) < 0.5 && g.player.onGround,
              "the player was not shoved aside, and is still standing");

        // The lift: stand on it, send it up, and go up with it.
        g.player.origin[0] = 0;
        g.player.origin[1] = fxFromInt(96);
        g.player.origin[2] = fxFromInt(60);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        g.frame(cmd, 30);
        const double standing = toUnits(g.player.origin[2]);
        check(g.player.onGround && g.player.groundMover == static_cast<int>(g.view.logic[lift].brush) &&
                  std::fabs(standing - (8 + 36 + 1.0 / 32)) < 0.01,
              "standing on the lift, and the movement knows which brush that is");
        logicFire(g.logic, g.player, g.view.logic[lift].name, -1, kUseToggle, 0);
        g.frame(cmd, 20);
        check(g.states[lift].move == MoveState::GoingUp, "the lift starts when its name is fired");
        check(toUnits(g.player.origin[2]) > standing + 20, "and the player rises with it");
        g.frame(cmd, 60);
        // Travel: 8 high, less 2, less a lip of -40: 46 units.
        check(g.states[lift].move == MoveState::AtTop &&
                  g.brushOfModel(3).pos[2] == fxFromInt(46),
              "it stops at the top of its travel and, with wait -1, stays");
        check(g.player.onGround && std::fabs(toUnits(g.player.origin[2]) - (standing + 46)) < 0.1,
              "with the player still standing on it, 46 units higher");
        g.frame(cmd, 90);
        check(g.states[lift].move == MoveState::AtTop, "wait -1: it does not come back by itself");
    }

    void testTriggersAndWiring()
    {
        BoxRoom room;
        room.brushes = {
            brush("trigger_once", "\"target\" \"wallA\"\n", 40, -128, 0, 128, 128, 128),
            brush("trigger_multiple", "\"target\" \"count\"\n\"wait\" \"0.5\"\n", -128, -128, 0, -40, 128, 128),
            brush("func_wall_toggle", "\"targetname\" \"wallA\"\n", 0, 100, 0, 8, 108, 64),
            brush("func_wall_toggle", "\"targetname\" \"count\"\n", 0, 80, 0, 8, 88, 64),
            brush("func_wall", "\"targetname\" \"doomed\"\n", 0, 60, 0, 8, 68, 64),
            brush("func_button", "\"target\" \"ms\"\n\"spawnflags\" \"1\"\n\"wait\" \"-1\"\n", 0, 40, 40, 4, 44, 48),
            brush("func_button", "\"target\" \"ms\"\n\"spawnflags\" \"1\"\n\"wait\" \"0.3\"\n", 0, 30, 40, 4, 34, 48),
            brush("func_door", "\"targetname\" \"gated\"\n\"master\" \"ms\"\n\"angle\" \"-1\"\n", 0, 0, 100, 8, 8, 120),
            brush("func_wall_toggle", "\"targetname\" \"result\"\n\"spawnflags\" \"1\"\n", 0, -20, 0, 8, -12, 64),
            // "wait" -1: without it a door waits 0 seconds and goes straight back.
            brush("func_door", "\"targetname\" \"open\"\n\"angle\" \"-1\"\n\"spawnflags\" \"1\"\n\"speed\" \"100\"\n\"wait\" \"-1\"\n",
                  0, -60, 0, 8, -52, 32),
            brush("func_breakable", "\"targetname\" \"glass\"\n\"target\" \"result\"\n", 0, -90, 0, 8, -82, 32),
        };
        room.extraEntities =
            "{\n\"classname\" \"multisource\"\n\"targetname\" \"ms\"\n\"target\" \"result\"\n}\n"
            "{\n\"classname\" \"trigger_relay\"\n\"targetname\" \"killer\"\n\"killtarget\" \"doomed\"\n"
            "\"delay\" \"0.2\"\n}\n"
            "{\n\"classname\" \"trigger_auto\"\n\"target\" \"count\"\n\"delay\" \"0.3\"\n}\n"
            "{\n\"classname\" \"trigger_relay\"\n\"targetname\" \"loop\"\n\"target\" \"loop\"\n}\n";
        Game g;
        build(g, room);
        check(g.ok, "wiring room");
        if (!g.ok)
            return;
        const BrushState &wallA = g.brushOfModel(3);
        const BrushState &counter = g.brushOfModel(4);
        MoveCommand idle;

        // trigger_auto: a tenth of a second after the start, then its own
        // delay, with nobody doing anything.
        check(counter.active, "the counter wall starts on");
        g.tick(33, 11); // 0.36 s
        check(counter.active, "0.1 s + 0.3 s has not passed");
        g.tick(33, 3);
        check(!counter.active, "trigger_auto fires by itself after 0.1 s and its delay");

        // The player is at the origin, in neither trigger.
        check(wallA.active, "trigger_once has not fired");

        // Into the trigger_multiple (x < -40): fires, re-arms after half a second.
        g.player.origin[0] = fxFromInt(-80);
        g.tick(33);
        check(counter.active, "entering trigger_multiple fires it (toggling the wall back)");
        g.tick(33, 10);
        check(counter.active, "it does not fire again inside its wait");
        g.tick(33, 6);
        check(!counter.active, "and does once the wait is over, the player still inside");

        // Into the trigger_once: fires once only.
        g.player.origin[0] = fxFromInt(80);
        g.tick(33);
        check(!wallA.active, "entering trigger_once fires it");
        g.tick(33, 60);
        check(!wallA.active, "and never again");
        g.player.origin[0] = 0;

        // killtarget, after a delay: the wall is gone, and so is its collision.
        const BrushState &doomed = g.brushOfModel(5);
        const int killer = g.recordOfKind(LogicKind::TriggerRelay, 0);
        g.use(killer);
        g.tick(33, 5);
        check(doomed.active, "a delayed killtarget waits");
        g.tick(33, 2);
        check(!doomed.active, "then removes the wall");
        const fx a[3] = {fxFromInt(-40), fxFromInt(64), fxFromInt(64)};
        const fx b[3] = {fxFromInt(60), fxFromInt(64), fxFromInt(64)};
        check(traceAll(g.move, false, a, b).fraction == kFractionOne,
              "and a removed wall stops nothing");

        // The multisource: an AND of two buttons, and a master for a door.
        const int stay = g.recordOfModel(6);
        const int pop = g.recordOfModel(7);
        const int gated = g.recordOfModel(8);
        const BrushState &result = g.brushOfModel(9);
        check(!result.active, "the result wall starts off");
        logicFire(g.logic, g.player, g.view.logic[gated].name, -1, kUseToggle, 0);
        g.tick(33);
        check(g.states[gated].move == MoveState::AtBottom, "a door whose master is off does not open");

        g.use(stay);
        g.tick(33);
        check(!result.active, "one button of two is not enough");
        g.use(pop);
        g.tick(33);
        check(result.active, "both pressed: the multisource fires its target");
        logicFire(g.logic, g.player, g.view.logic[gated].name, -1, kUseToggle, 0);
        g.tick(33);
        check(g.states[gated].move != MoveState::AtBottom, "and the door it masters now opens");

        // The second button pops out after 0.3 s and takes its input back.
        g.tick(33, 12);
        check(g.states[pop].move == MoveState::AtBottom, "the short button has popped out");
        check(!logicMasterAllows(g.logic, gated), "which switches the multisource off again");
        check(g.states[stay].move == MoveState::AtTop, "the wait -1 button stays in");

        // A door that starts open sits at the top of its travel and "opens"
        // towards where it was built.
        const int open = g.recordOfModel(10);
        check(g.brushOfModel(10).pos[2] == fxFromInt(30), "a start-open door begins raised");
        logicFire(g.logic, g.player, g.view.logic[open].name, -1, kUseToggle, 0);
        g.tick(33, 14);
        check(g.brushOfModel(10).pos[2] == 0 && g.states[open].move == MoveState::AtTop,
              "and closes when used");

        // A breakable used by name breaks and fires its target.
        const bool before = result.active;
        logicFire(g.logic, g.player, g.view.logic[g.recordOfModel(11)].name, -1, kUseToggle, 0);
        check(!g.brushOfModel(11).active && result.active != before,
              "a breakable breaks when used, and fires its target");
        logicFire(g.logic, g.player, g.view.logic[g.recordOfModel(11)].name, -1, kUseToggle, 0);
        check(result.active != before, "once: a broken thing is not used again");

        // A relay that targets itself must not hang the game.
        g.use(g.recordOfKind(LogicKind::TriggerRelay, 1));
        check(true, "a loop in the wiring ends at the depth limit");

        // More fires than the queue holds are counted, not silently lost.
        const int dropsBefore = g.logic.eventsDropped;
        for (int i = 0; i < kMaxLogicEvents + 5; ++i)
            logicEnqueue(g.logic, 1, 0, kUseToggle, -1, 100000);
        check(g.logic.eventsDropped - dropsBefore == 5, "a full event queue counts what it drops");
        (void)idle;
    }

    /// A train on a route of path corners, with everything a corner can ask
    /// for: a wait, a target to fire, a change of speed, a stop until used.
    void testTrains()
    {
        BoxRoom room;
        room.brushes = {
            // 1: the train, a platform 60 x 60 x 8 built in a corner of the
            //    room. Its middle is (-90, -90, 4). No name: it starts itself.
            brush("func_train", "\"target\" \"c1\"\n\"speed\" \"100\"\n", -120, -120, 0, -60, -60, 8),
            brush("func_wall_toggle", "\"targetname\" \"arrived\"\n", 100, 100, 0, 108, 108, 16),
            // 3: a named train: it waits to be told.
            brush("func_train", "\"targetname\" \"t2\"\n\"target\" \"d1\"\n\"speed\" \"200\"\n", 90, -120, 0, 110,
                  -100, 8),
        };
        room.extraEntities =
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"c1\"\n\"target\" \"c2\"\n\"origin\" \"0 90 4\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"c2\"\n\"target\" \"c3\"\n\"origin\" \"0 90 40\"\n"
            "\"wait\" \"0.5\"\n\"message\" \"arrived\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"c3\"\n\"target\" \"c1\"\n\"origin\" \"60 90 40\"\n"
            "\"speed\" \"50\"\n\"spawnflags\" \"1\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"d1\"\n\"target\" \"d2\"\n\"origin\" \"100 -110 4\"\n"
            "\"spawnflags\" \"2\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"d2\"\n\"origin\" \"100 0 4\"\n}\n";
        Game g;
        build(g, room);
        check(g.ok, "train room");
        if (!g.ok)
            return;
        const LevelView &v = g.view;
        const int train = g.recordOfModel(1);
        const int named = g.recordOfModel(3);
        const BrushState &platform = g.brushOfModel(1);
        const BrushState &lamp = g.brushOfModel(2);

        check(v.cornerCount == 5 && v.logic[train].kind == static_cast<std::uint8_t>(LogicKind::Train),
              "five corners and a train");
        const PathCorner &c1 = v.corners[v.logic[train].firstAux];
        check(c1.pos[1] == fxFromInt(90) && c1.next != kNoCorner &&
                  v.corners[c1.next].waitMs == 500 && v.corners[c1.next].fireTarget != 0,
              "the train's target is its first corner, and corners chain by name");
        check(v.corners[v.corners[c1.next].next].speed == fxFromInt(50) &&
                  (v.corners[v.corners[c1.next].next].flags & kCornerWaitForUse) &&
                  v.corners[v.corners[c1.next].next].next == v.logic[train].firstAux,
              "the third corner: its speed, its stop, and the loop back to the first");
        check(v.logic[train].target == 0, "a train's 'target' key is not something it fires");

        // It begins ON its first corner, not where it was built: the middle of
        // the brush, built at (-90, -90, 4), is put at (0, 90, 4).
        check(platform.pos[0] == fxFromInt(90) && platform.pos[1] == fxFromInt(180) &&
                  platform.pos[2] == 0,
              "a train starts with its middle on its first corner");
        const std::size_t trainBrush = v.logic[train].brush;
        check(g.movers[trainBrush].mins[0] == fxFromInt(-30) &&
                  g.movers[trainBrush].maxs[1] == fxFromInt(120),
              "and its collision is there with it");

        // Put the player on it and let it run. The route stays low: the room
        // is 128 high and a rider's head must clear the ceiling, or the train
        // (rightly) stops rather than crush them.
        g.player.origin[0] = 0;
        g.player.origin[1] = fxFromInt(90);
        g.player.origin[2] = fxFromInt(60);
        MoveCommand idle;
        // Frames until something is so, up to a limit; -1 if it never is.
        auto until = [&](int limit, const std::function<bool()> &done)
        {
            for (int i = 1; i <= limit; ++i)
            {
                g.frame(idle);
                if (done())
                    return i;
            }
            return -1;
        };

        g.frame(idle, 2);
        check(g.states[train].move == MoveState::AtBottom, "it waits a tenth of a second");
        check(until(8, [&] { return g.states[train].move == MoveState::GoingUp; }) > 0,
              "then sets off by itself: it has no name");
        // 36 units up at 100 a second: about eleven frames.
        const int rise = until(20, [&] { return g.states[train].move == MoveState::AtBottom; });
        check(rise >= 9 && rise <= 12, "it takes a third of a second to rise 36 units");
        check(platform.pos[2] == fxFromInt(36), "and stops exactly on the second corner");
        check(!lamp.active, "firing that corner's target on arriving");
        check(g.player.onGround &&
                  std::fabs(toUnits(g.player.origin[2]) - (36 + 8 + 36 + 1.0 / 32)) < 0.1,
              "the player has ridden up with it");

        const int wait = until(30, [&] { return g.states[train].move == MoveState::GoingUp; });
        check(wait >= 15 && wait <= 17, "it waits out the corner's half second, then goes on");
        // 60 units sideways, still at 100: the second corner gave no speed.
        const int across = until(30, [&] { return !g.states[train].running; });
        check(across >= 17 && across <= 20, "0.6 s to the third corner, which holds it");
        check(platform.pos[0] == fxFromInt(150) && platform.pos[2] == fxFromInt(36),
              "it is exactly on the third corner");
        check(std::fabs(toUnits(g.player.origin[0]) - 60) < 0.5, "having carried the player sideways");
        g.frame(idle, 60);
        check(platform.pos[0] == fxFromInt(150) && g.states[train].move == MoveState::AtBottom,
              "and it stays held until it is used");

        // Used: on to the first corner, at the 50 a second the third corner
        // set. 70 units: 1.4 seconds.
        g.use(train);
        check(g.states[train].move == MoveState::GoingUp && g.states[train].speed == fxFromInt(50),
              "used, it leaves at the speed the corner it leaves gives");
        g.frame(idle, 15);
        check(g.states[train].move == MoveState::GoingUp, "still on its way after half a second");
        // Used again while moving: it stops where it is.
        g.use(train);
        const fx held = platform.pos[0];
        g.frame(idle, 20);
        check(platform.pos[0] == held && !g.states[train].running, "used while moving, it stops");
        g.use(train);
        const int rest = until(60, [&]
        {
            return platform.pos[0] == fxFromInt(90) && platform.pos[2] == 0;
        });
        check(rest > 0, "and used once more, finishes the trip to the first corner");

        // The named train has not moved in all this time.
        const BrushState &second = g.brushOfModel(3);
        check(g.states[named].move == MoveState::AtBottom && !g.states[named].running &&
                  second.pos[1] == 0,
              "a train with a name waits to be told");
        // Its first corner teleports: told to go, it is simply at the second.
        logicFire(g.logic, g.player, v.logic[named].name, -1, kUseToggle, 0);
        check(second.pos[1] == fxFromInt(110), "a teleport corner moves the train at once");
        g.frame(idle, 10);
        check(!g.states[named].running, "and a corner with no next is the end of the line");

        // A train that meets the player does not turn round: it waits.
        Game h;
        BoxRoom squeeze;
        squeeze.brushes = {brush("func_train", "\"target\" \"top\"\n\"speed\" \"100\"\n", -30, -30, 100, 30, 30, 108)};
        squeeze.extraEntities =
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"top\"\n\"target\" \"floor\"\n\"origin\" \"0 0 104\"\n}\n"
            "{\n\"classname\" \"path_corner\"\n\"targetname\" \"floor\"\n\"target\" \"top\"\n\"origin\" \"0 0 4\"\n}\n";
        build(h, squeeze);
        check(h.ok, "squeeze room");
        if (!h.ok)
            return;
        h.frame(idle, 60);
        const int press = h.recordOfModel(1);
        check(h.states[press].move == MoveState::GoingUp, "a blocked train keeps trying");
        // The player's head is at 72; the platform's underside stops there.
        check(toUnits(h.brushOfModel(1).pos[2]) > -30 && toUnits(h.brushOfModel(1).pos[2]) < -25,
              "stopped on the player's head, not through them");
        check(hullPositionFree(h.move.standing, h.player.origin), "who is not pushed into the floor");
        // Step out from under it and it carries on down (and, its route being
        // a loop, straight back up: so look for the moment it touches bottom).
        h.player.origin[0] = fxFromInt(90);
        bool reachedFloor = false;
        for (int i = 0; i < 40 && !reachedFloor; ++i)
        {
            h.frame(idle);
            reachedFloor = h.brushOfModel(1).pos[2] == fxFromInt(-100);
        }
        check(reachedFloor, "out of the way, it finishes its trip");
    }

    /// Ladders: a volume in which forward is up.
    void testLadders()
    {
        BoxRoom room;
        room.brushes = {
            // A ladder against the +x wall: a thin volume floor to ceiling.
            brush("func_ladder", "\"targetname\" \"rungs\"\n", 104, -16, 0, 112, 16, 128),
        };
        room.extraEntities =
            "{\n\"classname\" \"trigger_relay\"\n\"targetname\" \"saw\"\n\"killtarget\" \"rungs\"\n}\n";
        Game g;
        build(g, room);
        check(g.ok, "ladder room");
        if (!g.ok)
            return;
        check(g.view.brushCount == 0, "a ladder is not a brush: it is neither seen nor solid");
        check(g.move.ladderCount == 1 && g.ladders[0].active &&
                  g.ladders[0].mins[0] == fxFromInt(104) && g.ladders[0].maxs[2] == fxFromInt(128),
              "it is a volume the movement knows about");

        MoveCommand cmd;
        // In the middle of the room the ladder does nothing.
        g.frame(cmd, 5);
        check(!g.player.onLadder, "not on a ladder in the middle of the room");

        // Walk at it (facing +x, level view). The player takes hold the
        // moment their box touches the volume -- 16 units before their centre
        // reaches it -- and from then on forward is up.
        cmd.forward = 1;
        int reached = -1;
        for (int i = 0; i < 20 && reached < 0; ++i)
        {
            g.frame(cmd);
            if (g.player.onLadder)
                reached = i;
        }
        check(reached >= 0, "walking into the ladder takes hold of it");
        const double before = toUnits(g.player.origin[2]);
        const double contactX = toUnits(g.player.origin[0]);
        check(contactX > 87.9 && contactX < 100, "as soon as the player's box touches it");
        g.frame(cmd, 6);
        const double climbed = toUnits(g.player.origin[2]) - before;
        // 200 units a second for a fifth of a second.
        check(climbed > 35 && climbed < 41, "forward, into the ladder, climbs at 200 units a second");
        check(std::fabs(toUnits(g.player.origin[0]) - contactX) < 0.5,
              "straight up: what would have gone into the ladder went up it instead");

        // Let go of the keys: hang there. No gravity on a ladder.
        cmd.forward = 0;
        const double hung = toUnits(g.player.origin[2]);
        g.frame(cmd, 30);
        check(g.player.onLadder && std::fabs(toUnits(g.player.origin[2]) - hung) < 0.01,
              "with no keys held the player hangs where they are");

        // Back: down.
        cmd.forward = -1;
        g.frame(cmd, 4);
        check(toUnits(g.player.origin[2]) < hung - 20, "back climbs down");

        // Looking straight up, forward still climbs; looking straight down
        // it descends -- the view decides, not just the key.
        cmd.forward = 1;
        cmd.viewForward[0] = 0;
        cmd.viewForward[2] = -kFxOne;
        const double high = toUnits(g.player.origin[2]);
        g.frame(cmd, 3);
        check(toUnits(g.player.origin[2]) < high - 15, "looking down, forward descends");
        cmd.viewForward[0] = kFxOne;
        cmd.viewForward[2] = 0;

        // Jump: kicked off the ladder, away from the wall.
        cmd.forward = 0;
        cmd.jump = true;
        g.frame(cmd, 1);
        check(!g.player.onLadder && g.player.velocity[0] < -fxFromInt(200),
              "jump kicks the player off, away from the ladder");
        cmd.jump = false;
        g.frame(cmd, 40);
        check(g.player.onGround && toUnits(g.player.origin[0]) < 60, "and they land on the floor");

        // On the floor at its foot, pushing AWAY from it steps off rather
        // than clinging.
        g.player.origin[0] = fxFromInt(100);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        g.frame(cmd, 3);
        cmd.forward = -1;
        g.frame(cmd, 10);
        check(toUnits(g.player.origin[0]) < 60 && !g.player.onLadder,
              "on the floor, walking away from a ladder leaves it");

        // A ladder that is removed is no longer climbed.
        g.use(g.recordOfKind(LogicKind::TriggerRelay));
        check(!g.ladders[0].active, "a removed ladder is switched off");
        g.player.origin[0] = fxFromInt(110);
        cmd.forward = 1;
        g.frame(cmd, 5);
        check(!g.player.onLadder, "and cannot be climbed");
    }

    /// Which way a hinged thing turns: a door on an upright hinge away from
    /// whoever opens it, its mapped way turned round; a lever that tips or
    /// rolls always its mapped way (c1a2c's freezer levers).
    void testHingeDirections()
    {
        auto turnFrom = [](const char *keys, int side)
        {
            BoxRoom room;
            room.brushes = {brush("func_door_rotating", keys, 0, -2, 0, 48, 2, 100)};
            Game g;
            build(g, room);
            if (!g.ok)
                return fx(0);
            const int door = g.recordOfModel(1);
            // Standing to one side of it, facing across it.
            g.player.origin[0] = fxFromInt(64);
            g.player.origin[1] = fxFromInt(60 * side);
            g.player.facingXY[0] = 0;
            g.player.facingXY[1] = -side * kFxOne;
            g.use(door);
            return g.states[door].travel[0];
        };
        // (Use only; "reverse direction"; 90 degrees is 16384.)
        const char *lever = "\"origin\" \"40 0 0\"\n\"distance\" \"90\"\n\"spawnflags\" \"386\"\n\"targetname\" \"l\"\n";
        const fx leverNorth = turnFrom(lever, 1), leverSouth = turnFrom(lever, -1);
        check(leverNorth == -16384 && leverSouth == -16384,
              "a lever that tips turns its mapped way, reversed as the map says, from either side");
        const char *door = "\"origin\" \"40 0 0\"\n\"distance\" \"90\"\n\"spawnflags\" \"258\"\n\"targetname\" \"d\"\n";
        const fx doorNorth = turnFrom(door, 1), doorSouth = turnFrom(door, -1);
        check(doorNorth == -doorSouth && (doorNorth == 16384 || doorNorth == -16384),
              "a door on an upright hinge still turns away from whoever opens it");
    }

    /// A door on a hinge: collision against a turned brush, and the door's
    /// own rules.
    void testRotatingDoors()
    {
        // The shared sine: every angle against the maths library.
        double worst = 0;
        for (int angle = 0; angle < 65536; angle += 7)
        {
            fx sine, cosine;
            fxSinCos(static_cast<std::uint16_t>(angle), sine, cosine);
            const double turn = angle * (2 * M_PI / 65536.0);
            worst = std::max(worst, std::fabs(sine - 4096 * std::sin(turn)));
            worst = std::max(worst, std::fabs(cosine - 4096 * std::cos(turn)));
        }
        check(worst < 1.0, "the sine table is within one unit in the last place everywhere");
        fx sine, cosine;
        fxSinCos(0, sine, cosine);
        check(sine == 0 && cosine == 4096, "exact at 0");
        fxSinCos(16384, sine, cosine);
        check(sine == 4096 && cosine == 0, "exact at a quarter turn");
        fxSinCos(49152, sine, cosine);
        check(sine == -4096 && cosine == 0, "exact at three quarters");

        // A door 48 long and 4 thick, built round its hinge and stood with
        // the hinge at (40, 0): shut, it runs along +x from x = 40 to 88.
        const char *doorKeys = "\"origin\" \"40 0 0\"\n\"speed\" \"180\"\n\"wait\" \"2\"\n\"distance\" \"90\"\n";
        BoxRoom room;
        room.brushes = {brush("func_door_rotating", doorKeys, 0, -2, 0, 48, 2, 100)};
        Game g;
        build(g, room);
        check(g.ok, "hinged door room");
        if (!g.ok)
            return;
        const int door = g.recordOfModel(1);
        const LogicRecord &record = g.view.logic[door];
        const std::size_t brushIndex = record.brush;
        check(record.kind == static_cast<std::uint8_t>(LogicKind::DoorRotating) &&
                  record.move[0] == 16384 && record.speed == 32768 && (record.flags & kLogicTouch),
              "90 degrees at 180 a second, as turns; no name, so it opens to the touch");
        check((g.view.brushes[brushIndex].flags & kBrushHasPivot) &&
                  g.view.brushes[brushIndex].mins[0] == 40 && g.view.brushes[brushIndex].maxs[0] == 88,
              "its brush is placed by its hinge");

        // Shut: it blocks the way across it, and its reach box is the circle
        // it can sweep.
        const fx south[3] = {fxFromInt(60), fxFromInt(-50), fxFromInt(64)};
        const fx north[3] = {fxFromInt(60), fxFromInt(50), fxFromInt(64)};
        Trace tr = traceAll(g.move, false, south, north);
        check(tr.hit && std::fabs(toUnits(tr.endPos[1]) - (-2 - 16 - 1.0 / 32)) < 0.01,
              "shut, it stops a move across it");
        check(g.movers[brushIndex].mins[1] < fxFromInt(-47) && g.movers[brushIndex].maxs[0] > fxFromInt(87),
              "and its quick-reject box covers everywhere it can swing");

        // Turned by hand to an odd angle: the fixed-point trace against a
        // float one that turns the segment the same way.
        auto compareAt = [&](std::uint16_t yaw, unsigned seed)
        {
            logicPlaceBrush(g.logic, static_cast<int>(brushIndex), g.brushStates[brushIndex].pos, yaw);
            const double turn = yaw * (2 * M_PI / 65536.0);
            const double c = std::cos(turn), sn = std::sin(turn);
            std::mt19937 rng(seed);
            int differ = 0;
            for (int n = 0; n < 3000; ++n)
            {
                fx a[3], b[3];
                double la[3], lb[3];
                for (int i = 0; i < 3; ++i)
                {
                    std::uniform_int_distribution<int> pick(fxFromInt(i == 2 ? 40 : -100),
                                                            fxFromInt(i == 2 ? 90 : 100));
                    a[i] = pick(rng);
                    b[i] = pick(rng);
                }
                // Into the door's space, in doubles.
                auto local = [&](const fx *w, double *out)
                {
                    const double x = toUnits(w[0]) - 40, y = toUnits(w[1]);
                    out[0] = x * c + y * sn;
                    out[1] = y * c - x * sn;
                    out[2] = toUnits(w[2]);
                };
                local(a, la);
                local(b, lb);
                // The door's standing hull is its box grown by 16, 16, 36:
                // solve the segment against that box directly.
                const double lo[3] = {-16, -18, -36}, hi[3] = {64, 18, 136};
                double enter = 0, leave = 1;
                bool inside = true, miss = false;
                for (int i = 0; i < 3 && !miss; ++i)
                {
                    const double d = lb[i] - la[i];
                    if (la[i] < lo[i] || la[i] > hi[i])
                        inside = false;
                    if (std::fabs(d) < 1e-12)
                    {
                        miss = la[i] < lo[i] || la[i] > hi[i];
                        continue;
                    }
                    double t0 = (lo[i] - la[i]) / d, t1 = (hi[i] - la[i]) / d;
                    if (t0 > t1)
                        std::swap(t0, t1);
                    enter = std::max(enter, t0);
                    leave = std::min(leave, t1);
                    miss = enter > leave;
                }
                const Trace fixed = traceAll(g.move, false, a, b);
                const Trace world = traceHull(g.move.standing, a, b);
                // Only judge segments the world leaves alone and that do not
                // start inside the door.
                if (world.fraction != kFractionOne || inside)
                    continue;
                const bool refHit = !miss && enter > 0 && enter < 1;
                if (fixed.hit != refHit)
                {
                    ++differ;
                    continue;
                }
                if (!refHit)
                    continue;
                // Where the fixed-point trace stopped, in the door's space: it
                // must be just outside the box -- 1/32 of a unit off the face
                // it hit, measured square to the face. (Measured along the
                // segment that distance is anything at all, at a glance.)
                const double f = fixed.fraction / double(kFractionOne);
                double outside = -1e9;
                for (int i = 0; i < 3; ++i)
                {
                    const double pnt = la[i] + (lb[i] - la[i]) * f;
                    outside = std::max(outside, std::max(lo[i] - pnt, pnt - hi[i]));
                }
                if (outside < 0.0 || outside > 0.08)
                    ++differ;
            }
            return differ;
        };
        // A segment that passes within 1/32 of an edge of the box is a hit to
        // one and a miss to the other, legitimately; a handful in 3000 is that,
        // more would be the turning being wrong.
        check(compareAt(0, 1) <= 3, "3000 traces against the shut door match a float box");
        check(compareAt(5000, 2) <= 3, "and against the door turned 27 degrees");
        check(compareAt(40000, 3) <= 3, "and turned 220 degrees");
        logicPlaceBrush(g.logic, static_cast<int>(brushIndex), g.brushStates[brushIndex].pos, 0);

        // Walk at it from the south: it swings north, away.
        MoveCommand cmd;
        cmd.forward = 1;
        cmd.forwardXY[0] = 0;
        cmd.forwardXY[1] = kFxOne;
        cmd.rightXY[0] = kFxOne;
        cmd.rightXY[1] = 0;
        g.player.origin[0] = fxFromInt(64);
        g.player.origin[1] = fxFromInt(-60);
        int opened = -1;
        for (int i = 0; i < 60 && opened < 0; ++i)
        {
            g.frame(cmd);
            if (g.states[door].move == MoveState::AtTop)
                opened = i;
        }
        check(opened > 0, "walked into, it opens");
        check(g.brushStates[brushIndex].angles[1] == 16384,
              "a quarter turn anticlockwise: away from a player coming from the south");
        tr = traceAll(g.move, false, south, north);
        check(tr.fraction == kFractionOne, "open, the way across where it stood is clear");
        const fx west[3] = {0, fxFromInt(30), fxFromInt(64)};
        const fx east[3] = {fxFromInt(100), fxFromInt(30), fxFromInt(64)};
        tr = traceAll(g.move, false, west, east);
        check(tr.hit && std::fabs(toUnits(tr.endPos[0]) - (38 - 16 - 1.0 / 32)) < 0.05,
              "and it is solid where it stands now");
        check(tr.normal[0] < -kNormalOne + 4096 && std::abs(tr.normal[1]) < 4096 * 64,
              "with a normal turned along with it");
        g.frame(cmd, 30);
        check(toUnits(g.player.origin[1]) > 40, "the player walks through the doorway");

        // It shuts again after its wait.
        cmd.forward = 0;
        int shut = -1;
        for (int i = 0; i < 120 && shut < 0; ++i)
        {
            g.frame(cmd);
            if (g.states[door].move == MoveState::AtBottom)
                shut = i;
        }
        check(shut > 0 && g.brushStates[brushIndex].angles[1] == 0, "after its wait it swings shut");

        // From the north it opens the other way: south.
        cmd.forward = 1;
        cmd.forwardXY[1] = -kFxOne;
        cmd.rightXY[0] = -kFxOne;
        g.player.origin[0] = fxFromInt(64);
        g.player.origin[1] = fxFromInt(60);
        for (int i = 0; i < 60 && g.states[door].move != MoveState::AtTop; ++i)
            g.frame(cmd);
        check(g.brushStates[brushIndex].angles[1] == 49152,
              "from the other side it opens the other way: always away");

        // Standing where it has to swing: it turns back rather than pass
        // through the player. Let it open (south), then stand in its path
        // home, on the arc between.
        cmd.forward = 0;
        g.player.origin[0] = fxFromInt(40 + 24);
        g.player.origin[1] = fxFromInt(-24);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        int turnedBack = 0;
        MoveState last = g.states[door].move;
        for (int i = 0; i < 300; ++i)
        {
            g.frame(cmd);
            if (last == MoveState::GoingDown && g.states[door].move == MoveState::GoingUp)
                ++turnedBack;
            last = g.states[door].move;
            check(positionFree(g.move, false, g.player.origin), "the door never turns into the player");
        }
        check(turnedBack >= 1, "a hinged door that meets the player turns back");

        // Flags: one-way, reverse, start open, and the axes that are not
        // handled.
        BoxRoom flags;
        flags.brushes = {
            brush("func_door_rotating", "\"origin\" \"40 0 0\"\n\"spawnflags\" \"16\"\n\"distance\" \"45\"\n\"wait\" \"-1\"\n",
                  0, -2, 0, 48, 2, 100),
            brush("func_door_rotating",
                  "\"targetname\" \"d\"\n\"origin\" \"-40 80 0\"\n\"spawnflags\" \"19\"\n\"wait\" \"-1\"\n", 0,
                  -2, 0, 30, 2, 100),
            brush("func_door_rotating", "\"origin\" \"-80 -80 0\"\n\"spawnflags\" \"64\"\n\"wait\" \"-1\"\n", 0, -2, 0, 20, 2,
                  100),
        };
        Game f;
        build(f, flags);
        check(f.ok, "flags room");
        if (!f.ok)
            return;
        const int oneWay = f.recordOfModel(1);
        check((f.view.logic[oneWay].flags & kLogicOneWay) && f.view.logic[oneWay].move[0] == 8192,
              "flag 16 is one-way; 45 degrees is an eighth of a turn");
        // From the north, where a two-way door would open south.
        f.player.origin[0] = fxFromInt(64);
        f.player.origin[1] = fxFromInt(60);
        f.player.facingXY[0] = 0;
        f.player.facingXY[1] = -kFxOne;
        f.use(oneWay);
        f.tick(33, 20);
        check(f.brushStates[f.view.logic[oneWay].brush].angles[1] == 8192,
              "a one-way door opens its own way whichever side it is opened from");

        const int reversed = f.recordOfModel(2);
        check(f.view.logic[reversed].move[0] == -16384 &&
                  (f.view.logic[reversed].flags & kLogicStartOpen),
              "flag 2 reverses it; flag 1 starts it open");
        check(f.brushStates[f.view.logic[reversed].brush].angles[1] == 49152,
              "starting open, it starts turned");
        logicFire(f.logic, f.player, f.view.logic[reversed].name, -1, kUseToggle, 0);
        f.tick(33, 40);
        check(f.brushStates[f.view.logic[reversed].brush].angles[1] == 0 &&
                  f.states[reversed].move == MoveState::AtTop,
              "and used, turns shut -- which for it is the far end of its travel");

        // Flag 64 is a flap: it turns about a horizontal axis (its roll).
        const int flap = f.recordOfModel(3);
        check(flap >= 0 && f.view.logic[flap].extra[0] == kRoll &&
                  (f.view.brushes[f.view.logic[flap].brush].flags & kBrushTumbles),
              "a door on a horizontal hinge turns about that, and is boxed as a ball");
        if (flap >= 0)
        {
            f.player.origin[0] = fxFromInt(100);
            f.player.origin[1] = fxFromInt(100);
            f.use(flap);
            f.tick(33, 40);
            const BrushState &state = f.brushStates[f.view.logic[flap].brush];
            // Which way depends on where the player stands, as for any door
            // that is not one-way.
            check((state.angles[kRoll] == 16384 || state.angles[kRoll] == 49152) &&
                      state.angles[kYaw] == 0,
                  "and opens a quarter turn about it");
            // Its top corner (20, 2, 100) from the hinge, rolled a quarter
            // turn: y -> z and z -> -y, so it now reaches y = -100.
            const MoverHull &mover = f.movers[f.view.logic[flap].brush];
            const fx local[3] = {fxFromInt(20), fxFromInt(2), fxFromInt(100)};
            fx world[3];
            hldata::detail::turnToWorld(mover, local, world);
            const double sign = state.angles[kRoll] == 16384 ? 1 : -1;
            check(std::fabs(toUnits(world[1]) + sign * 100) < 0.5 &&
                      std::fabs(toUnits(world[2]) - sign * 2) < 0.5,
                  "turned the way the game turns it");
        }
    }

    void testRotation()
    {
        // The matrix against the game's own formula, in doubles, at angles
        // all over the circle.
        std::mt19937 rng(7);
        std::uniform_int_distribution<int> any(0, 65535);
        double worst = 0;
        for (int n = 0; n < 2000; ++n)
        {
            const std::uint16_t angles[3] = {static_cast<std::uint16_t>(any(rng)),
                                             static_cast<std::uint16_t>(any(rng)),
                                             static_cast<std::uint16_t>(any(rng))};
            std::int32_t m[9];
            anglesToAxes(angles, m);
            const double k = 2 * M_PI / 65536.0;
            const double sp = std::sin(angles[0] * k), cp = std::cos(angles[0] * k);
            const double sy = std::sin(angles[1] * k), cy = std::cos(angles[1] * k);
            const double sr = std::sin(angles[2] * k), cr = std::cos(angles[2] * k);
            const double want[9] = {cp * cy, sr * sp * cy - cr * sy, cr * sp * cy + sr * sy,
                                    cp * sy, sr * sp * sy + cr * cy, cr * sp * sy - sr * cy,
                                    -sp,     sr * cp,                cr * cp};
            for (int i = 0; i < 9; ++i)
                worst = std::max(worst, std::fabs(m[i] / 1073741824.0 - want[i]));
        }
        check(worst < 1e-6, "the rotation matrix is the game's, to a part in a million");

        // A fan: an origin brush in the middle of the room, spinning about
        // the vertical at 90 degrees a second, started by name; and one about
        // a horizontal axis that starts by itself and winds up.
        BoxRoom room;
        BrushBox fan;
        fan.className = "func_rotating";
        fan.mins[0] = -40, fan.mins[1] = -4, fan.mins[2] = 100;
        fan.maxs[0] = 40, fan.maxs[1] = 4, fan.maxs[2] = 104;
        fan.keys = "\"targetname\" \"fan\"\n\"origin\" \"0 0 102\"\n\"speed\" \"90\"\n";
        room.brushes.push_back(fan);
        BrushBox wheel = fan;
        wheel.mins[0] = 90, wheel.maxs[0] = 94, wheel.mins[1] = -20, wheel.maxs[1] = 20;
        wheel.mins[2] = 60, wheel.maxs[2] = 100;
        wheel.keys = "\"origin\" \"92 0 80\"\n\"speed\" \"180\"\n\"spawnflags\" \"25\"\n"
                     "\"fanfriction\" \"10\"\n";
        room.brushes.push_back(wheel);
        Game g;
        build(g, room);
        check(g.ok, "fan room");
        if (!g.ok)
            return;
        const int fanRecord = g.recordOfModel(1);
        const int wheelRecord = g.recordOfModel(2);
        check(fanRecord >= 0 && wheelRecord >= 0 &&
                  g.view.logic[fanRecord].kind == static_cast<std::uint8_t>(LogicKind::Rotating) &&
                  g.view.logic[fanRecord].speed == 16384 && g.view.logic[fanRecord].extra[0] == kYaw &&
                  g.view.logic[wheelRecord].extra[0] == kPitch &&
                  (g.view.logic[wheelRecord].flags & kLogicStartOpen) &&
                  (g.view.logic[wheelRecord].flags & kLogicToggle),
              "fans are cooked: speed as turns, the axis and the start flags from the map's");
        if (fanRecord < 0 || wheelRecord < 0)
            return;
        g.player.origin[0] = fxFromInt(-100);
        g.player.origin[1] = fxFromInt(-100);
        g.player.origin[2] = fxFromInt(36);
        const BrushState &fanState = g.brushStates[g.view.logic[fanRecord].brush];
        const BrushState &wheelState = g.brushStates[g.view.logic[wheelRecord].brush];
        g.tick(33, 30);
        check(fanState.angles[kYaw] == 0, "a fan that is not told to start stands still");
        check(wheelState.angles[kPitch] == 0, "one that starts on waits a second and a half");
        g.tick(33, 30); // two seconds in: half a second of winding up at 10% a tenth
        const int early = wheelState.angles[kPitch];
        check(early > 0 && early < 65536 * 180 / 360 / 2,
              "then winds up: after half a second it has not turned as far as full speed would");
        g.use(fanRecord);
        g.tick(33, 30); // about a second at 90 degrees a second
        const double turned = fanState.angles[kYaw] * 360.0 / 65536.0;
        check(turned > 84 && turned < 96, "started by name, it turns at its speed");
        g.use(fanRecord);
        g.tick(33, 10);
        const std::uint16_t rest = fanState.angles[kYaw];
        g.tick(33, 10);
        check(fanState.angles[kYaw] == rest, "and used again, it stops");
    }

    void testUse()
    {
        BoxRoom room;
        room.brushes = {
            brush("func_button", "\"target\" \"x\"\n\"spawnflags\" \"1\"\n", 40, -8, 56, 44, 8, 72),
            brush("func_button", "\"target\" \"x\"\n\"spawnflags\" \"257\"\n", -44, -8, 56, -40, 8, 72),
            brush("func_door", "\"angle\" \"-1\"\n\"spawnflags\" \"256\"\n", -8, 60, 0, 8, 64, 100),
        };
        Game g;
        build(g, room);
        check(g.ok, "use room");
        if (!g.ok)
            return;
        const fx eye[3] = {0, 0, fxFromInt(64)};
        const fx east[3] = {kFxOne, 0, 0};
        const fx west[3] = {-kFxOne, 0, 0};
        const fx north[3] = {0, kFxOne, 0};
        const fx up[3] = {0, 0, kFxOne};

        check(logicFindUsable(g.logic, eye, east) == g.recordOfModel(1),
              "facing a button within reach: it is what USE works on");
        check(logicFindUsable(g.logic, eye, west) == -1,
              "a touch-only button cannot be USEd");
        check(logicFindUsable(g.logic, eye, north) == g.recordOfModel(3),
              "a use-only door can");
        check(logicFindUsable(g.logic, eye, up) == -1, "looking at the ceiling: nothing");
        const fx farEye[3] = {fxFromInt(-60), 0, fxFromInt(64)};
        check(logicFindUsable(g.logic, farEye, east) == -1, "100 units away is out of reach");

        check(!(g.view.logic[g.recordOfModel(3)].flags & kLogicTouch),
              "and a use-only door does not open by touch");
        // The touch-only button works by being walked into.
        g.player.origin[0] = fxFromInt(-23);
        g.player.origin[2] = fxFromInt(64);
        g.tick(33);
        check(g.states[g.recordOfModel(2)].move != MoveState::AtBottom,
              "the touch-only button is pressed by walking into it");
    }
    void testBreakingAndPushing()
    {
        // A crate that takes two blows; a pane only its name breaks; a plank
        // that gives way when stood on; a box to shove; and three volumes: one
        // that hurts, one that carries, one that sends the player elsewhere.
        BoxRoom room;
        room.brushes = {
            brush("func_breakable", "\"health\" \"40\"\n\"target\" \"lamp\"\n", 40, -8, 0, 56, 8, 48),
            brush("func_breakable", "\"targetname\" \"pane\"\n\"spawnflags\" \"1\"\n\"health\" \"5\"\n",
                  40, 40, 0, 56, 56, 48),
            brush("func_breakable", "\"spawnflags\" \"4\"\n\"delay\" \"0.3\"\n", -100, -100, 0, -60, -60, 8),
            brush("func_pushable", "\"friction\" \"200\"\n\"spawnflags\" \"128\"\n\"health\" \"20\"\n",
                  -16, 60, 0, 16, 92, 32),
            brush("func_wall_toggle", "\"targetname\" \"lamp\"\n", 100, 100, 0, 110, 110, 20),
            brush("trigger_hurt", "\"dmg\" \"20\"\n", 60, -120, 0, 120, -80, 64),
            brush("trigger_teleport", "\"target\" \"there\"\n", -120, 60, 0, -90, 120, 64),
        };
        room.extraEntities = "{\n\"classname\" \"info_teleport_destination\"\n"
                             "\"targetname\" \"there\"\n\"origin\" \"0 -60 4\"\n\"angle\" \"90\"\n}\n";
        Game g;
        build(g, room);
        check(g.ok, "breaking room");
        if (!g.ok)
            return;
        const int crate = g.recordOfModel(1), pane = g.recordOfModel(2), plank = g.recordOfModel(3),
                  box = g.recordOfModel(4);
        check(crate >= 0 && pane >= 0 && plank >= 0 && box >= 0,
              "everything that breaks has a record, named or not");
        if (crate < 0 || pane < 0 || plank < 0 || box < 0)
            return;
        check(g.view.logic[crate].extra[0] == 40 && (g.view.logic[pane].flags & kLogicTriggerOnly) &&
                  (g.view.logic[plank].flags & kLogicPressure) &&
                  g.view.logic[box].kind == static_cast<std::uint8_t>(LogicKind::Pushable) &&
                  (g.view.logic[box].flags & kLogicBreaks) &&
                  g.view.logic[box].extra[2] == static_cast<std::uint32_t>(fxFromInt(200)),
              "with what it takes, how it breaks, and how fast a box can be pushed");

        g.player.origin[0] = 0, g.player.origin[1] = 0, g.player.origin[2] = fxFromInt(36);
        const BrushState &crateState = g.brushStates[g.view.logic[crate].brush];
        const BrushState &lamp = g.brushOfModel(5);
        const bool lampWas = lamp.active;
        logicStrike(g.logic, g.player, crate, 20);
        check(crateState.active, "one blow of twenty does not break forty");
        logicStrike(g.logic, g.player, crate, 20);
        check(!crateState.active && g.states[crate].dead && lamp.active != lampWas,
              "the second does, and its target is fired");

        const BrushState &paneState = g.brushStates[g.view.logic[pane].brush];
        logicStrike(g.logic, g.player, pane, 1000);
        check(paneState.active, "what only its name breaks takes any blow");
        logicFire(g.logic, g.player, g.view.logic[pane].name, -1, kUseToggle, 0);
        check(!paneState.active, "and breaks when its name is fired");

        // Onto the plank: it holds for its delay, then goes, and the player
        // with it.
        g.player.origin[0] = fxFromInt(-80);
        g.player.origin[1] = fxFromInt(-80);
        g.player.origin[2] = fxFromInt(8 + 36 + 1);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        MoveCommand idle;
        g.frame(idle, 5);
        const BrushState &plankState = g.brushStates[g.view.logic[plank].brush];
        check(plankState.active && g.player.onGround, "a plank holds for a moment when stood on");
        g.frame(idle, 12);
        check(!plankState.active, "then gives way");
        g.frame(idle, 10);
        check(toUnits(g.player.origin[2]) < 37, "and whoever stood on it lands on the floor");

        // The box: walked into from the south, it moves north, no faster than
        // it can be pushed, and stops when the pushing does.
        g.player.origin[0] = 0, g.player.origin[1] = fxFromInt(20), g.player.origin[2] = fxFromInt(36);
        g.frame(idle, 3);
        const BrushState &boxState = g.brushStates[g.view.logic[box].brush];
        const double boxWas = toUnits(boxState.pos[1]);
        MoveCommand north;
        north.forward = 1;
        north.forwardXY[0] = 0, north.forwardXY[1] = kFxOne;
        north.rightXY[0] = kFxOne, north.rightXY[1] = 0;
        g.frame(north, 30);
        const double pushed = toUnits(boxState.pos[1]) - boxWas;
        check(pushed > 5 && pushed < 40, "a box moves when it is walked into, as far as the wall lets it");
        const double restAt = toUnits(boxState.pos[1]);
        g.frame(idle, 30);
        check(std::fabs(toUnits(boxState.pos[1]) - restAt) < 1, "and stays where it is left");
        logicStrike(g.logic, g.player, box, 20);
        check(!boxState.active, "a box that can break breaks like anything else");

        // The volume that hurts: twenty a second, in halves.
        g.player.health = 100;
        g.player.origin[0] = fxFromInt(90), g.player.origin[1] = fxFromInt(-100);
        g.player.origin[2] = fxFromInt(36);
        g.tick(33, 31);
        check(g.player.health == 80 || g.player.health == 70,
              "a second in a trigger_hurt costs a second of its damage");
        g.player.origin[1] = 0;
        const int after = g.player.health;
        g.tick(33, 31);
        check(g.player.health == after, "and nothing once out of it");

        // And the one that sends them elsewhere.
        g.logic.faceSet = false;
        g.player.origin[0] = fxFromInt(-100), g.player.origin[1] = fxFromInt(90);
        g.tick(33);
        check(g.player.origin[0] == 0 && g.player.origin[1] == fxFromInt(-60) &&
                  g.player.origin[2] == fxFromInt(40) && g.logic.faceSet && g.logic.faceYaw == 16384,
              "a teleport puts the player on its destination, facing the way it faces");
    }

    void testFrameClock()
    {
        // A minute at 60, 30 and 20 frames a second, and one that changes
        // its pace: what the frames add up to is the exact time, at every
        // frame less than a millisecond behind it. Rounded each frame on its
        // own, as it was, a minute came out a second fast, or too slow.
        const int paces[][4] = {{1, 1, 1, 1}, {2, 2, 2, 2}, {3, 3, 3, 3}, {1, 2, 3, 2}};
        for (const auto &pace : paces)
        {
            FrameClock clock;
            std::int64_t vblanks = 0, sum = 0, rounded = 0;
            bool close = true;
            for (int f = 0; vblanks < 3600; ++f)
            {
                const int v = pace[f % 4];
                vblanks += v;
                sum += clock.ms(v);
                rounded += (v * 1000 + 30) / 60;
                const std::int64_t exact = vblanks * 1000 / 60;
                close = close && sum <= exact && exact - sum < 1;
            }
            check(close && sum == vblanks * 1000 / 60, "the frames' milliseconds add up to the exact time");
            if (pace[0] == 1 && pace[1] == 1)
                check(rounded - sum > 900, "(where each frame rounded on its own ran a second a minute fast)");
        }
    }

    void testWater()
    {
        // A pool: a func_water filling the bottom 80 units of the room's east
        // half. (The box room has no water leaves of its own; a body of water
        // that is a brush is the other way the game makes one.)
        BoxRoom room;
        room.maxs[2] = 200;
        BrushBox pool;
        pool.className = "func_water";
        pool.mins[0] = 0, pool.mins[1] = -128, pool.mins[2] = 0;
        pool.maxs[0] = 128, pool.maxs[1] = 128, pool.maxs[2] = 80;
        pool.keys = "\"skin\" \"-3\"\n\"rendermode\" \"2\"\n\"renderamt\" \"120\"\n"
                    "\"targetname\" \"pool\"\n\"wait\" \"-1\"\n";
        room.brushes.push_back(pool);
        Game g;
        build(g, room);
        check(g.ok, "pool room");
        if (!g.ok)
            return;
        g.move.nodes = g.view.nodes;
        g.move.leaves = g.view.leaves;
        g.move.headNode = g.view.models[0].headNode;
        check(g.view.brushCount == 1 &&
                  static_cast<std::int8_t>(g.view.brushes[0].kind) == kContentsWater &&
                  !(g.view.brushes[0].flags & kBrushSolid),
              "a func_water is cooked as a body of water, not as something solid");

        const fx inPool[3] = {fxFromInt(60), 0, fxFromInt(40)};
        const fx above[3] = {fxFromInt(60), 0, fxFromInt(120)};
        const fx dry[3] = {fxFromInt(-60), 0, fxFromInt(40)};
        check(pointContents(g.move, inPool) == kContentsWater &&
                  pointContents(g.move, above) == kContentsEmpty &&
                  pointContents(g.move, dry) == kContentsEmpty,
              "what is in its box is in water, and nothing else is");

        // On the dry side nothing changes.
        MoveCommand idle;
        g.player.origin[0] = fxFromInt(-60), g.player.origin[1] = 0, g.player.origin[2] = fxFromInt(37);
        g.frame(idle, 10);
        check(g.player.waterLevel == 0 && g.player.onGround, "dry land is dry");

        // Dropped in from above the pool: no longer falling at 800 a second
        // squared, but sinking at a walk.
        g.player.origin[0] = fxFromInt(60);
        g.player.origin[2] = fxFromInt(60);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        g.frame(idle, 3);
        check(g.player.waterLevel >= 2, "in over the waist is swimming");
        const double sinking = toUnits(g.player.velocity[2]);
        check(sinking < -20 && sinking > -70, "left alone in water, the player sinks slowly");
        g.frame(idle, 60);
        check(g.player.waterLevel == 3 && toUnits(g.player.origin[2]) < 40,
              "to the bottom, eyes under");

        // Jump is "up", to the surface and no higher than it.
        MoveCommand up;
        up.jump = true;
        g.frame(up, 45);
        check(toUnits(g.player.origin[2]) > 60 && toUnits(g.player.origin[2]) < 110,
              "held jump swims up to the surface");
        check(g.player.health == 100, "a few seconds under costs nothing");

        // Kept under for a quarter of a minute: the air runs out.
        g.player.origin[2] = fxFromInt(37);
        g.frame(idle, 30 * 15);
        check(g.player.health < 100 && g.player.health > 60, "but twelve seconds does, a little at a time");

        // Swimming forwards goes where the player looks.
        g.player.origin[0] = fxFromInt(40), g.player.origin[1] = 0, g.player.origin[2] = fxFromInt(40);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        MoveCommand east;
        east.forward = 1;
        g.frame(east, 20);
        check(toUnits(g.player.origin[0]) > 70, "forward swims the way the view points");
    }

    // Crouched, the player climbs and swims a third as fast, as they walk
    // (the game cuts the keys before the ladder or the water is looked at).
    void testCrouchedClimbAndSwim()
    {
        BoxRoom room;
        room.maxs[2] = 200;
        room.brushes = {brush("func_ladder", "", 104, -16, 0, 112, 16, 200)};
        BrushBox pool;
        pool.className = "func_water";
        pool.mins[0] = -128, pool.mins[1] = -128, pool.mins[2] = 0;
        pool.maxs[0] = 0, pool.maxs[1] = 128, pool.maxs[2] = 80;
        pool.keys = "\"skin\" \"-3\"\n\"targetname\" \"pool\"\n\"wait\" \"-1\"\n";
        room.brushes.push_back(pool);
        Game g;
        build(g, room);
        check(g.ok, "ladder and pool room");
        if (!g.ok)
            return;
        g.move.nodes = g.view.nodes;
        g.move.leaves = g.view.leaves;
        g.move.headNode = g.view.models[0].headNode;

        // Up the ladder for a fifth of a second, standing and then crouched.
        auto climb = [&](bool ducked)
        {
            g.player.ducked = ducked;
            g.player.ducking = false;
            g.player.origin[0] = fxFromInt(95), g.player.origin[1] = 0;
            g.player.origin[2] = fxFromInt(100);
            g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
            MoveCommand cmd;
            cmd.duck = ducked;
            cmd.forward = 1;
            g.frame(cmd, 1);
            const double from = toUnits(g.player.origin[2]);
            g.frame(cmd, 6);
            return g.player.onLadder ? toUnits(g.player.origin[2]) - from : -1.0;
        };
        const double standing = climb(false);
        const double crouched = climb(true);
        std::printf("  climbed in 0.2 s: %.1f standing, %.1f crouched\n", standing, crouched);
        check(standing > 35 && standing < 41, "standing, a ladder is climbed at 200 units a second");
        check(crouched > 11.5 && crouched < 15, "crouched, at a third of that");

        // Swimming forwards a second, standing and then crouched: the speed
        // it settles at (0.8 of what the keys ask, 320 at most).
        auto swim = [&](bool ducked)
        {
            g.player.ducked = ducked;
            g.player.ducking = false;
            g.player.origin[0] = fxFromInt(-110), g.player.origin[1] = 0;
            g.player.origin[2] = fxFromInt(30);
            g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
            MoveCommand cmd;
            cmd.duck = ducked;
            cmd.forward = 1;
            g.frame(cmd, 12);
            return toUnits(g.player.velocity[0]);
        };
        const double swum = swim(false);
        const double swumCrouched = swim(true);
        std::printf("  swimming: %.1f standing, %.1f crouched\n", swum, swumCrouched);
        check(swum > 200, "standing, the player swims at up to 256 units a second");
        check(swumCrouched > 80 && swumCrouched < 110, "crouched, at no more than 107");
    }

    void testDrowning()
    {
        BoxRoom room;
        room.maxs[2] = 200;
        BrushBox pool;
        pool.className = "func_water";
        pool.mins[0] = 0, pool.mins[1] = -128, pool.mins[2] = 0;
        pool.maxs[0] = 128, pool.maxs[1] = 128, pool.maxs[2] = 120;
        pool.keys = "\"skin\" \"-3\"\n\"targetname\" \"pool\"\n\"wait\" \"-1\"\n";
        room.brushes.push_back(pool);
        Game g;
        build(g, room);
        check(g.ok, "drowning pool room");
        if (!g.ok)
            return;
        g.move.nodes = g.view.nodes;
        g.move.leaves = g.view.leaves;
        g.move.headNode = g.view.models[0].headNode;

        // Held under: the air lasts twelve seconds, then a breath not taken
        // costs 3, then 4, then 5 a second, and no more than 5.
        MoveCommand idle;
        std::vector<int> hits;
        for (int i = 0; i < 30 * 17; ++i)
        {
            g.player.origin[0] = fxFromInt(60), g.player.origin[1] = 0, g.player.origin[2] = fxFromInt(37);
            g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
            const int before = g.player.health;
            g.frame(idle);
            if (g.player.health < before)
                hits.push_back(before - g.player.health);
        }
        check(hits.size() >= 4 && hits[0] == 3 && hits[1] == 4 && hits[2] == 5 && hits[3] == 5,
              "drowning costs 3, 4, then 5 a second");
        const int drowned = g.player.health;
        check(drowned < 85, "and adds up");

        // Out on dry land: what drowning took is given back, 10 at a time.
        g.player.origin[0] = fxFromInt(-60), g.player.origin[2] = fxFromInt(37);
        g.frame(idle, 30 * 3);
        check(g.player.health > drowned && g.player.health < 100, "breathing again gives it back, a little at a time");
        g.frame(idle, 30 * 10);
        check(g.player.health == 100, "all of it, in the end");
    }

    void testWaterJump()
    {
        // A pool in the west of the room, up to 80, and east of it a ledge
        // whose top is just above the water.
        BoxRoom room;
        room.maxs[2] = 200;
        BrushBox pool;
        pool.className = "func_water";
        pool.mins[0] = -128, pool.mins[1] = -128, pool.mins[2] = 0;
        pool.maxs[0] = 64, pool.maxs[1] = 128, pool.maxs[2] = 80;
        pool.keys = "\"skin\" \"-3\"\n\"targetname\" \"pool\"\n\"wait\" \"-1\"\n";
        room.brushes.push_back(pool);
        BrushBox ledge;
        ledge.mins[0] = 64, ledge.mins[1] = -128, ledge.mins[2] = 0;
        ledge.maxs[0] = 128, ledge.maxs[1] = 128, ledge.maxs[2] = 82;
        room.brushes.push_back(ledge);
        Game g;
        build(g, room);
        check(g.ok, "pool and ledge room");
        if (!g.ok)
            return;
        g.move.nodes = g.view.nodes;
        g.move.leaves = g.view.leaves;
        g.move.headNode = g.view.models[0].headNode;

        // Waist deep against the ledge, swimming at it: up and out.
        g.player.origin[0] = fxFromInt(44), g.player.origin[1] = 0, g.player.origin[2] = fxFromInt(66);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        MoveCommand east;
        east.forward = 1;
        bool jumped = false;
        for (int i = 0; i < 60; ++i)
        {
            g.frame(east);
            jumped = jumped || g.player.waterJumpMs > 0;
        }
        check(jumped, "swimming at a ledge at the surface starts a jump out of the water");
        const double z = toUnits(g.player.origin[2]);
        check(g.player.onGround && toUnits(g.player.origin[0]) > 64 && z > 116 && z < 120,
              "and it ends standing on the ledge");
    }

    /// A thing thrown into a box room from (-100, 0, `z`) at 200 a second
    /// along x, run at `ms` a frame for `total` ms: how high it came up
    /// again after it first met the floor, where it got to, and whether it
    /// has stopped dead.
    struct Thrown
    {
        double rebound = 0, x = 0;
        bool still = false;
    };
    Thrown throwInRoom(Game &g, std::uint8_t kind, bool modelGib, fx z, int ms, int total)
    {
        ActorWorld a;
        g.logic.actors = &a;
        const fx from[3] = {fxFromInt(-100), 0, z};
        const fx velocity[3] = {fxFromInt(200), 0, 0};
        Gib *gib = nullptr;
        Projectile *shot = nullptr;
        if (modelGib)
            gib = &gibAdd(a, 0, 0, from, velocity, total + 1000);
        else
            shot = projectileAdd(a, kind, from, velocity, total + 1000);
        Thrown out;
        bool landed = false;
        double lowest = 1e9, last[3] = {0, 0, 0};
        for (int t = 0; t < total; t += ms)
        {
            hldata::detail::projectilesTick(g.logic, g.player, ms);
            const fx *pos = modelGib ? gib->pos : shot->pos;
            const fx *vel = modelGib ? gib->velocity : shot->velocity;
            const double at = toUnits(pos[2]);
            if (!landed && vel[2] > 0)
                landed = true, lowest = at;
            if (landed && at - lowest > out.rebound)
                out.rebound = at - lowest;
            out.x = toUnits(pos[0]);
            out.still = (modelGib ? gib->resting : vel[0] == 0 && vel[1] == 0 && vel[2] == 0) && last[0] == out.x &&
                        last[2] == at;
            last[0] = out.x, last[2] = at;
        }
        g.logic.actors = nullptr;
        return out;
    }

    void testThrownComeToRest()
    {
        BoxRoom room;
        room.mins[0] = -512, room.maxs[0] = 512;
        room.maxs[2] = 256;
        Game g;
        build(g, room);
        check(g.ok, "a long room");
        if (!g.ok)
            return;
        g.move.nodes = g.view.nodes;
        g.move.leaves = g.view.leaves;
        g.move.headNode = g.view.models[0].headNode;
        g.logic.world = &g.move;

        // Dropped from 100 units: it meets the floor at about 280 a second
        // (half gravity) and comes back with a fifth of that -- a few units
        // up, not most of the drop -- and in the end lies still.
        const fx high = fxFromInt(100);
        const Thrown dropped = throwInRoom(g, kShotGrenade, false, high, 33, 4000);
        std::printf("test_logic: grenade dropped: rebound %.1f, rests at x %.1f\n", dropped.rebound, dropped.x);
        check(dropped.rebound < 6, "a grenade hardly bounces off the floor");
        check(dropped.still, "and comes to rest");

        // Set rolling along the floor at 200 a second: it stops within a
        // few units, and the same few whatever the frame rate.
        const fx floor = fxFromInt(18) + kFxOne / 16; // (its centre, a crouched person's half-height up)
        const Thrown at30 = throwInRoom(g, kShotGrenade, false, floor, 33, 2000);
        const Thrown at60 = throwInRoom(g, kShotGrenade, false, floor, 16, 2000);
        const Thrown at20 = throwInRoom(g, kShotGrenade, false, floor, 50, 2000);
        std::printf("test_logic: grenade rolled: stops at x %.1f / %.1f / %.1f (30, 60, 20 a second)\n", at30.x,
                    at60.x, at20.x);
        check(at30.still && at60.still && at20.still, "a rolling grenade stops");
        check(at30.x > -100 && at30.x < -80, "within a few units");
        check(std::fabs(at60.x - at30.x) < 2.5 && std::fabs(at20.x - at30.x) < 2.5, "as far at any frame rate");

        // Gibs, of a model or a sprite: full gravity, a little more of a
        // bounce, and at rest in the end.
        const Thrown scrap = throwInRoom(g, kShotGib, false, high, 33, 4000);
        const Thrown piece = throwInRoom(g, 0, true, high, 33, 4000);
        std::printf("test_logic: gibs dropped: rebound %.1f / %.1f, rest at x %.1f / %.1f\n", scrap.rebound,
                    piece.rebound, scrap.x, piece.x);
        check(scrap.rebound > 5 && scrap.rebound < 30 && piece.rebound > 5 && piece.rebound < 30,
              "a gib bounces a little");
        check(scrap.still && piece.still, "and comes to rest");
        const Thrown scrap20 = throwInRoom(g, kShotGib, false, floor, 50, 2000);
        const Thrown scrap60 = throwInRoom(g, kShotGib, false, floor, 16, 2000);
        std::printf("test_logic: gib rolled: stops at x %.1f / %.1f (20, 60 a second)\n", scrap20.x, scrap60.x);
        check(scrap20.still && scrap60.still && std::fabs(scrap20.x - scrap60.x) < 4, "a rolling gib stops alike");
    }

    void testWaterStep()
    {
        // A pool over the whole east half, and on its floor a step 16 high.
        BoxRoom room;
        room.maxs[2] = 200;
        BrushBox pool;
        pool.className = "func_water";
        pool.mins[0] = 0, pool.mins[1] = -128, pool.mins[2] = 0;
        pool.maxs[0] = 128, pool.maxs[1] = 128, pool.maxs[2] = 120;
        pool.keys = "\"skin\" \"-3\"\n\"targetname\" \"pool\"\n\"wait\" \"-1\"\n";
        room.brushes.push_back(pool);
        BrushBox step;
        step.mins[0] = 64, step.mins[1] = -128, step.mins[2] = 0;
        step.maxs[0] = 128, step.maxs[1] = 128, step.maxs[2] = 16;
        room.brushes.push_back(step);
        Game g;
        build(g, room);
        check(g.ok, "pool with a step room");
        if (!g.ok)
            return;
        g.move.nodes = g.view.nodes;
        g.move.leaves = g.view.leaves;
        g.move.headNode = g.view.models[0].headNode;

        // Under water on the pool's floor, swimming level at the step: up
        // onto it, as the game does, and on.
        g.player.origin[0] = fxFromInt(30), g.player.origin[1] = 0, g.player.origin[2] = fxFromInt(37);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        MoveCommand east;
        east.forward = 1;
        // (At its face by the fourth frame; held there, it is not climbed.)
        g.frame(east, 8);
        check(g.player.waterLevel == 3, "under water");
        check(toUnits(g.player.origin[0]) > 52 && toUnits(g.player.origin[2]) > 51,
              "swimming into a step under water goes up onto it, without being held at it");
    }

    void testLevelChange()
    {
        // Map A: a volume by the east wall that leads to "mapb", a landmark,
        // and a switch that outlives the map, set as the map starts.
        BoxRoom a;
        BrushBox way;
        way.className = "trigger_changelevel";
        way.mins[0] = 96, way.mins[1] = -32, way.mins[2] = 0;
        way.maxs[0] = 120, way.maxs[1] = 32, way.maxs[2] = 96;
        way.keys = "\"map\" \"MapB\"\n\"landmark\" \"join\"\n"
                   "\"changetarget\" \"welcome\"\n\"changedelay\" \"0.5\"\n";
        a.brushes.push_back(way);
        a.extraEntities =
            "{\n\"classname\" \"info_landmark\"\n\"targetname\" \"join\"\n"
            "\"origin\" \"100 0 0\"\n}\n"
            "{\n\"classname\" \"env_global\"\n\"globalstate\" \"power\"\n"
            "\"spawnflags\" \"1\"\n\"initialstate\" \"1\"\n}\n";
        // Map B: the same landmark somewhere else, a door the old map asks to
        // have opened on arrival, and one that opens by itself if the switch
        // is on.
        BoxRoom b;
        BrushBox welcome;
        welcome.className = "func_door";
        welcome.mins[0] = 60, welcome.mins[1] = 100, welcome.mins[2] = 0;
        welcome.maxs[0] = 68, welcome.maxs[1] = 120, welcome.maxs[2] = 64;
        welcome.keys = "\"targetname\" \"welcome\"\n\"angle\" \"-1\"\n\"wait\" \"-1\"\n";
        b.brushes.push_back(welcome);
        BrushBox powered = welcome;
        powered.mins[0] = 80, powered.maxs[0] = 88;
        powered.keys = "\"targetname\" \"powered\"\n\"angle\" \"-1\"\n\"wait\" \"-1\"\n";
        b.brushes.push_back(powered);
        b.extraEntities =
            "{\n\"classname\" \"info_landmark\"\n\"targetname\" \"join\"\n"
            "\"origin\" \"-90 20 0\"\n}\n"
            "{\n\"classname\" \"trigger_auto\"\n\"target\" \"powered\"\n"
            "\"globalstate\" \"power\"\n}\n"
            "{\n\"classname\" \"trigger_auto\"\n\"target\" \"welcome\"\n"
            "\"globalstate\" \"nothing_sets_this\"\n}\n";

        Globals globals;
        Game ga;
        ga.logic.globals = &globals;
        build(ga, a);
        check(ga.ok, "map A cooks");
        if (!ga.ok)
            return;
        const int way0 = ga.recordOfKind(LogicKind::ChangeLevel);
        check(way0 >= 0 && ga.view.landmarkCount == 1, "a way out and a landmark are cooked");
        if (way0 < 0)
            return;
        const LogicRecord &rec = ga.view.logic[way0];
        check(std::strcmp(ga.view.strings + rec.extra[0], "mapb") == 0,
              "the map's name, lower-cased");
        check(rec.extra[1] == nameHash("join") && rec.extra[2] == nameHash("welcome") &&
                  rec.extra[3] == 500 && (rec.flags & kLogicTouch),
              "with its landmark, what to fire on arrival, and when");
        check(globals.get(nameHash("power")) == kGlobalOn, "the switch is set as the map starts");

        // Standing in the middle: nothing. Walking into the volume: the map
        // asks to be changed, once.
        ga.player.origin[0] = 0, ga.player.origin[1] = 0, ga.player.origin[2] = fxFromInt(36);
        ga.tick(33, 5);
        check(ga.logic.pendingChange < 0, "nothing happens away from the way out");
        ga.player.origin[0] = fxFromInt(90);
        ga.player.origin[1] = fxFromInt(-10);
        ga.player.velocity[0] = fxFromInt(200);
        ga.player.ducked = false;
        ga.tick(33);
        check(ga.logic.pendingChange == way0, "walking into it asks for the next map");

        Arrival arrival = makeArrival(ga.logic, ga.view.landmarks, ga.view.landmarkCount,
                                      ga.player, ga.logic.pendingChange);
        check(arrival.valid && arrival.landmark == nameHash("join") &&
                  arrival.offset[0] == fxFromInt(-10) && arrival.offset[1] == fxFromInt(-10) &&
                  arrival.offset[2] == fxFromInt(36),
              "what crosses: where the player is from the landmark");

        Game gb;
        gb.logic.globals = &globals;
        build(gb, b);
        check(gb.ok, "map B cooks");
        if (!gb.ok)
            return;
        const bool placed =
            applyArrival(gb.logic, gb.view.landmarks, gb.view.landmarkCount, gb.view.nameHashes,
                         gb.view.nameCount, gb.player, arrival);
        check(placed && gb.player.origin[0] == fxFromInt(-100) &&
                  gb.player.origin[1] == fxFromInt(10) && gb.player.origin[2] == fxFromInt(36) &&
                  gb.player.velocity[0] == fxFromInt(200),
              "the player arrives as far from the new landmark, moving as before");

        // The door the old map named opens half a second after arriving, not
        // before; the one that waits on the switch opens by itself; and the
        // trigger_auto whose switch nobody set does nothing.
        const BrushState &welcomeDoor = gb.brushOfModel(1);
        const BrushState &poweredDoor = gb.brushOfModel(2);
        gb.tick(33, 12);
        check(toUnits(welcomeDoor.pos[2]) < 0.01, "the arrival's door has not moved after 0.4 s");
        check(toUnits(poweredDoor.pos[2]) > 1, "the door behind the switch is opening");
        gb.tick(33, 6);
        check(toUnits(welcomeDoor.pos[2]) > 1, "... and it opens once half a second has gone");

        // A map that does not have the landmark: the caller is told.
        Arrival lost = arrival;
        lost.landmark = nameHash("no_such_place");
        Game gc;
        build(gc, b);
        check(gc.ok && !applyArrival(gc.logic, gc.view.landmarks, gc.view.landmarkCount,
                                     gc.view.nameHashes, gc.view.nameCount, gc.player, lost),
              "without the landmark the player is left for the map's own start");
    }
    void testTransitionVolume()
    {
        // A way back fired by name only (c2a5c's: the door that shuts behind
        // the player as they arrive fires it), whose landmark has a volume
        // by the west wall. Outside it, firing it does nothing; inside, the
        // map changes. A way whose landmark has no volume goes from anywhere.
        BoxRoom room;
        BrushBox back;
        back.className = "trigger_changelevel";
        back.mins[0] = 96, back.mins[1] = -32, back.mins[2] = 0;
        back.maxs[0] = 120, back.maxs[1] = 32, back.maxs[2] = 96;
        back.keys = "\"map\" \"mapb\"\n\"landmark\" \"join\"\n\"targetname\" \"back\"\n\"spawnflags\" \"2\"\n";
        room.brushes.push_back(back);
        BrushBox volume;
        volume.className = "trigger_transition";
        volume.mins[0] = -120, volume.mins[1] = -64, volume.mins[2] = 0;
        volume.maxs[0] = -40, volume.maxs[1] = 64, volume.maxs[2] = 96;
        volume.keys = "\"targetname\" \"join\"\n";
        room.brushes.push_back(volume);
        BrushBox other = back;
        other.mins[1] = 64, other.maxs[1] = 96;
        other.keys = "\"map\" \"mapc\"\n\"landmark\" \"elsewhere\"\n\"targetname\" \"free\"\n\"spawnflags\" \"2\"\n";
        room.brushes.push_back(other);
        room.extraEntities = "{\n\"classname\" \"info_landmark\"\n\"targetname\" \"join\"\n\"origin\" \"0 0 0\"\n}\n"
                             "{\n\"classname\" \"info_landmark\"\n\"targetname\" \"elsewhere\"\n\"origin\" \"0 0 0\"\n}\n";
        Game g;
        build(g, room);
        check(g.ok, "the room with a transition volume cooks");
        if (!g.ok)
            return;
        const int inside = g.recordOfKind(LogicKind::Transition);
        int way = -1, wayFree = -1;
        for (int i = 0; i < g.logic.recordCount; ++i)
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::ChangeLevel))
                (g.view.logic[i].extra[1] == nameHash("join") ? way : wayFree) = i;
        check(inside >= 0 && g.view.logic[inside].extra[0] == nameHash("join") && way >= 0 && wayFree >= 0,
              "a trigger_transition is cooked, named as its landmark");
        if (inside < 0 || way < 0 || wayFree < 0)
            return;
        g.player.origin[0] = fxFromInt(40), g.player.origin[1] = 0, g.player.origin[2] = fxFromInt(36);
        logicUse(g.logic, g.player, way, -1, kUseToggle);
        check(g.logic.pendingChange < 0, "fired with the player outside its landmark's volume: no change");
        g.player.origin[0] = fxFromInt(-80);
        logicUse(g.logic, g.player, way, -1, kUseToggle);
        check(g.logic.pendingChange == way, "... and with the player inside it, the map changes");
        g.logic.pendingChange = -1;
        g.player.origin[0] = fxFromInt(40);
        logicUse(g.logic, g.player, wayFree, -1, kUseToggle);
        check(g.logic.pendingChange == wayFree, "a way whose landmark has no volume goes from anywhere");
    }
    /// What hurts in a mover and what a manager taken away still does: a
    /// door with "dmg" that comes down on the player, each time it tries; a
    /// train that does, every half second (CFuncTrain::Blocked); a
    /// multi_manager killed half way through, which fires no more; and a
    /// pendulum, which swings between its ends -- and, damped, dies down to
    /// rest in its middle, or, stopped, goes back to where it began.
    void testCrushKillAndSwing()
    {
        auto standUnder = [](Game &g)
        {
            g.player.origin[0] = g.player.origin[1] = 0;
            g.player.origin[2] = fxFromInt(40);
            g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
            g.player.health = 100;
        };
        {
            BoxRoom room;
            // 1: a door over the player's head that comes down 48 (to 32):
            //    it meets them at 72.
            room.brushes = {brush("func_door", "\"targetname\" \"press\"\n\"angle\" \"-2\"\n\"speed\" \"100\"\n"
                                               "\"wait\" \"-1\"\n\"dmg\" \"10\"\n",
                                  -20, -20, 80, 20, 20, 128)};
            Game g;
            build(g, room);
            check(g.ok, "crush room");
            if (!g.ok)
                return;
            standUnder(g);
            const int press = g.recordOfModel(1);
            check(g.view.logic[press].damage == 10, "a door's dmg is cooked");
            g.use(press);
            MoveCommand idle;
            g.frame(idle, 8);
            std::printf("test_logic: a door of dmg 10 come down on the player: health %d\n", g.player.health);
            check(g.player.health < 100 && (100 - g.player.health) % 10 == 0, "a door that hurts hurts whoever holds it up");
        }
        {
            BoxRoom room;
            // 1: a train (dmg 4) that comes down from 110 to 4 through where
            //    the player stands.
            room.brushes = {brush("func_train", "\"target\" \"e1\"\n\"speed\" \"200\"\n\"dmg\" \"4\"\n", 60, 60, 100,
                                  100, 100, 108)};
            room.extraEntities =
                "{\n\"classname\" \"path_corner\"\n\"targetname\" \"e1\"\n\"target\" \"e2\"\n\"origin\" \"0 0 110\"\n}\n"
                "{\n\"classname\" \"path_corner\"\n\"targetname\" \"e2\"\n\"origin\" \"0 0 4\"\n}\n";
            Game g;
            build(g, room);
            check(g.ok, "crushing train room");
            if (!g.ok)
                return;
            standUnder(g);
            MoveCommand idle;
            g.frame(idle, 61); // (two seconds)
            std::printf("test_logic: two seconds under a train of dmg 4: health %d\n", g.player.health);
            check(g.player.health <= 100 - 3 * 4 && g.player.health >= 100 - 5 * 4,
                  "a train that hurts hurts whoever holds it up, every half second");
        }
        {
            BoxRoom room;
            room.brushes = {brush("func_wall_toggle", "\"targetname\" \"soon\"\n", 0, 100, 0, 8, 108, 64),
                            brush("func_wall_toggle", "\"targetname\" \"later\"\n", 0, 80, 0, 8, 88, 64)};
            room.extraEntities =
                "{\n\"classname\" \"multi_manager\"\n\"targetname\" \"mm\"\n\"soon\" \"0.5\"\n\"later\" \"2\"\n}\n"
                "{\n\"classname\" \"trigger_relay\"\n\"targetname\" \"stop\"\n\"killtarget\" \"mm\"\n}\n";
            Game g;
            build(g, room);
            check(g.ok, "manager room");
            if (!g.ok)
                return;
            const int manager = g.recordOfKind(LogicKind::MultiManager);
            const int relay = g.recordOfKind(LogicKind::TriggerRelay);
            g.use(manager);
            g.tick(33, 30); // (a second)
            check(!g.brushOfModel(1).active, "the manager's first target has gone off");
            g.use(relay);
            g.tick(33, 60);
            check(g.brushOfModel(2).active, "a manager taken away fires nothing more");
        }
        {
            BoxRoom room;
            // 1: swings 90 degrees at 90 a second, for ever. 2: the same,
            //    damped (0.1 a second): it comes to rest at 45. 3: stopped,
            //    it goes back.
            room.brushes = {
                brush("func_pendulum", "\"spawnflags\" \"1\"\n\"distance\" \"90\"\n\"speed\" \"90\"\n"
                                       "\"origin\" \"-80 -80 32\"\n",
                      -88, -88, 0, -72, -72, 64),
                brush("func_pendulum", "\"spawnflags\" \"1\"\n\"distance\" \"90\"\n\"speed\" \"90\"\n"
                                       "\"damp\" \"100\"\n\"origin\" \"80 -80 32\"\n",
                      72, -88, 0, 88, -72, 64),
                brush("func_pendulum", "\"spawnflags\" \"17\"\n\"distance\" \"90\"\n\"speed\" \"90\"\n"
                                       "\"origin\" \"80 80 32\"\n",
                      72, 72, 0, 88, 88, 64),
            };
            Game g;
            build(g, room);
            check(g.ok, "pendulum room");
            if (!g.ok)
                return;
            const int free = g.recordOfModel(1), damped = g.recordOfModel(2), back = g.recordOfModel(3);
            const int axis = static_cast<int>(g.view.logic[free].extra[0] % 3);
            auto degrees = [&](int model)
            { return static_cast<std::int16_t>(g.brushOfModel(model).angles[axis]) * 360.0 / 65536.0; };
            double least = 1e9, most = -1e9;
            for (int i = 0; i < 300; ++i) // (ten seconds)
            {
                g.tick(33);
                least = std::min(least, degrees(1));
                most = std::max(most, degrees(1));
            }
            std::printf("test_logic: a pendulum of 90: swung between %.1f and %.1f degrees\n", least, most);
            check(least > -1 && least < 2 && most > 88 && most < 91 && g.states[free].running,
                  "a pendulum swings from where it began to its distance, and on");
            g.tick(33, 300);
            std::printf("test_logic: damped: %s at %.1f degrees after twenty seconds\n",
                        g.states[damped].running ? "still swinging" : "at rest", degrees(2));
            check(!g.states[damped].running && std::fabs(degrees(2) - 45) < 1, "damped, it comes to rest in its middle");
            g.use(back);
            g.tick(33, 60);
            check(!g.states[back].running && std::fabs(degrees(3)) < 0.5, "stopped, one that goes back goes back");
            check(g.states[free].running, "(the undamped one still swings)");
        }
    }

    /// What crushes, as the game's Blocked() and Touch() have it: a lift
    /// that carries the player into a ceiling does one point and goes back
    /// down (CFuncPlat); a wheel held up by them crushes by its "dmg" each
    /// frame (CFuncRotating); one marked to hurt by its touch hurts by its
    /// speed and throws them off (HurtTouch); a pendulum by its "dmg" and its
    /// speed (CPendulum::Touch). All of it through the armour.
    void testMoversCrush()
    {
        MoveCommand idle;
        {
            BoxRoom room;
            // 1: a lift the player stands on that goes 64 up (made at its
            //    top: it starts at the bottom, on the floor); 2: a bar 100
            //    above the floor it cannot carry them past.
            room.brushes = {brush("func_plat", "\"height\" \"64\"\n\"speed\" \"100\"\n", -30, -30, 64, 30, 30, 72),
                            brush("func_wall", "", -60, -60, 100, 60, 60, 110)};
            Game g;
            build(g, room);
            check(g.ok, "lift room");
            if (!g.ok)
                return;
            g.player.origin[0] = g.player.origin[1] = 0;
            g.player.origin[2] = fxFromInt(8 + 36);
            g.player.health = 100;
            g.player.inv.armor = 0;
            const int lift = g.recordOfModel(1);
            // (Back at the bottom with them on it, it goes up again -- the
            // game's plat does too: a point each time it is held.)
            int blocks = 0, lost = 0;
            bool everyTimeOne = true, highest = true;
            for (int i = 0; i < 90; ++i)
            {
                const int before = g.player.health;
                const MoveState was = g.states[lift].move;
                g.frame(idle);
                if (g.player.health < before)
                {
                    ++blocks;
                    lost += before - g.player.health;
                    everyTimeOne = everyTimeOne && before - g.player.health == 1 && was == MoveState::GoingUp &&
                                   g.states[lift].move == MoveState::GoingDown;
                }
                highest = highest && g.player.origin[2] < fxFromInt(100 - 36 + 1);
            }
            std::printf("test_logic: a lift into a ceiling: held %d times, %d lost\n", blocks, lost);
            check(blocks > 0 && everyTimeOne, "a lift that carries the player into a ceiling does one point and goes back");
            check(highest, "... and the player is never pushed into it");
        }
        {
            BoxRoom room;
            // 1: a bar 60 long turning about (0, 0) at 90 a second, dmg 5;
            //    the player stands in its sweep. 2: the same, marked to hurt
            //    by its touch (32), and no dmg.
            room.brushes = {brush("func_rotating", "\"spawnflags\" \"1\"\n\"speed\" \"90\"\n\"dmg\" \"5\"\n"
                                                    "\"origin\" \"0 0 32\"\n",
                                  -4, -4, 0, 64, 4, 64)};
            Game g;
            build(g, room);
            check(g.ok, "wheel room");
            if (!g.ok)
                return;
            g.player.origin[0] = fxFromInt(24);
            g.player.origin[1] = fxFromInt(40);
            g.player.origin[2] = fxFromInt(36);
            g.player.health = 100;
            g.player.inv.armor = 0;
            for (int i = 0; i < 60; ++i)
                g.frame(idle);
            std::printf("test_logic: two seconds in a wheel's way (dmg 5): health %d\n", g.player.health);
            check(g.player.health < 100 && (100 - g.player.health) % 5 == 0, "a wheel held up crushes by its dmg");
        }
        {
            BoxRoom room;
            room.brushes = {brush("func_rotating", "\"spawnflags\" \"33\"\n\"speed\" \"300\"\n"
                                                    "\"origin\" \"0 0 32\"\n",
                                  -4, -4, 0, 64, 4, 64)};
            Game g;
            build(g, room);
            check(g.ok, "fan room");
            if (!g.ok)
                return;
            g.player.origin[0] = fxFromInt(24);
            g.player.origin[1] = fxFromInt(40);
            g.player.origin[2] = fxFromInt(36);
            g.player.health = 100;
            g.player.inv.armor = 0;
            bool thrown = false;
            for (int i = 0; i < 30 && g.player.health == 100; ++i)
            {
                g.frame(idle);
                thrown = thrown || g.player.velocity[0] != 0 || g.player.velocity[1] != 0;
            }
            std::printf("test_logic: in a hurting fan's way (300 a second): health %d\n", g.player.health);
            check(g.player.health == 70 && thrown, "a fan that hurts by its touch hurts a tenth of its speed, and throws");
        }
        {
            BoxRoom room;
            // A pendulum swinging through where the player stands, dmg 50.
            room.brushes = {brush("func_pendulum", "\"spawnflags\" \"1\"\n\"distance\" \"90\"\n\"speed\" \"90\"\n"
                                                    "\"dmg\" \"50\"\n\"origin\" \"0 0 32\"\n",
                                  -4, -4, 0, 64, 4, 64)};
            Game g;
            build(g, room);
            check(g.ok, "pendulum room");
            if (!g.ok)
                return;
            check(g.view.logic[g.recordOfModel(1)].damage == 50, "a pendulum's dmg is cooked");
            g.player.origin[0] = fxFromInt(24);
            g.player.origin[1] = fxFromInt(40);
            g.player.origin[2] = fxFromInt(36);
            g.player.health = 100;
            g.player.inv.armor = 50;
            for (int i = 0; i < 60 && g.player.health == 100; ++i)
                g.frame(idle);
            std::printf("test_logic: struck by a pendulum (dmg 50): health %d, armour %d\n", g.player.health,
                        g.player.inv.armor);
            check(g.player.health < 100 && g.player.inv.armor < 50, "a pendulum hurts what it swings into, through the armour");
        }
    }
} // namespace

int main()
{
    testCookRules();
    testCookedRecords();
    testButtonManagerDoor();
    testMoverLoopFullQueue();
    testMoverSounding();
    testTrainSetsOffHeard();
    testDoorsAndThePlayer();
    testTriggersAndWiring();
    testTrains();
    testLadders();
    testMoversCrush();
    testRotatingDoors();
    testHingeDirections();
    testRotation();
    testUse();
    testBreakingAndPushing();
    testFrameClock();
    testWater();
    testCrouchedClimbAndSwim();
    testDrowning();
    testWaterJump();
    testWaterStep();
    testThrownComeToRest();
    testLevelChange();
    testTransitionVolume();
    testCrushKillAndSwing();
    if (g_failures == 0)
        std::printf("test_logic: ok\n");
    return g_failures == 0 ? 0 : 1;
}
