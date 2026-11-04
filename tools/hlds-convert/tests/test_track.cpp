// The tram: a train on a track of path_tracks (shared/HlLogic.hpp), on a
// synthetic track and then on the real thing.
//
// The synthetic track checks the rules one at a time: a train that waits to be
// started, a node that sets its speed, a node that fires something as it is
// passed, the end of the track and what is fired there, and which way the
// train faces.
//
// A second synthetic track is driven, as the player drives a train (USE at
// its controls, forward and back a quarter of its speed at a press): through
// stopped to backwards, back past a node that fires again, to the start of
// the track; let go of by a step aside, a jump, or USE again. With the game,
// c2a2's train is driven, and into c2a2a at its controls still; c2a3's crash
// train is driven from its func_traincontrols only.
//
// With HLDS_VALVE_DIR set, the opening tram ride is run from end to end with
// nobody at the controls -- six maps, five level changes, two platforms that
// lower the tram from one track to another -- and has to finish with the tram
// at rest at the station and the player still inside it. Nothing draws; a
// minute of the ride takes a fraction of a second.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "HlLevelReader.hpp"
#include "HlTransition.hpp"
#include "TestBspBuilder.hpp"
#include "TestGame.hpp"
#include "hlds/Cooker.hpp"
#include "hlds/EntityCook.hpp"
#include "hlds/EntityParser.hpp"
#include "hlds/MdlFile.hpp"
#include "hlds/ModelCook.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    double toUnits(fx v) { return v / 4096.0; }

    void testSyntheticTrack()
    {
        // A track of four nodes along the room, a little above the floor, and
        // a small car on it. Node b sets the speed, node c fires a door, and
        // the track ends at d, which fires another.
        BoxRoom room;
        room.mins[0] = -400, room.maxs[0] = 400;
        BrushBox car;
        car.className = "func_tracktrain";
        car.mins[0] = -20, car.mins[1] = -10, car.mins[2] = -4;
        car.maxs[0] = 20, car.maxs[1] = 10, car.maxs[2] = 4;
        car.keys = "\"targetname\" \"car\"\n\"origin\" \"0 0 0\"\n\"target\" \"a\"\n"
                   "\"speed\" \"100\"\n\"height\" \"8\"\n\"wheels\" \"30\"\n\"spawnflags\" \"2\"\n";
        room.brushes.push_back(car);
        BrushBox door;
        door.className = "func_door";
        door.mins[0] = 300, door.mins[1] = 100, door.mins[2] = 0;
        door.maxs[0] = 310, door.maxs[1] = 120, door.maxs[2] = 64;
        door.keys = "\"targetname\" \"passing\"\n\"angle\" \"-1\"\n\"wait\" \"-1\"\n";
        room.brushes.push_back(door);
        BrushBox last = door;
        last.mins[0] = 330, last.maxs[0] = 340;
        last.keys = "\"targetname\" \"arrived\"\n\"angle\" \"-1\"\n\"wait\" \"-1\"\n";
        room.brushes.push_back(last);
        room.extraEntities =
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"a\"\n\"target\" \"b\"\n"
            "\"origin\" \"-300 0 16\"\n}\n"
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"b\"\n\"target\" \"c\"\n"
            "\"origin\" \"-200 0 16\"\n\"speed\" \"200\"\n}\n"
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"c\"\n\"target\" \"d\"\n"
            "\"origin\" \"0 0 16\"\n\"message\" \"passing\"\n}\n"
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"d\"\n"
            "\"origin\" \"200 0 16\"\n\"netname\" \"arrived\"\n}\n";
        Game g;
        std::string error;
        g.ok = g.bsp.parse(buildBoxRoom(room), error);
        if (g.ok)
            finish(g, nullptr);
        check(g.ok, "track room cooks");
        if (!g.ok)
            return;
        const int train = g.recordOfKind(LogicKind::TrackTrain);
        check(train >= 0 && g.view.cornerCount == 4, "a track train and its four nodes");
        if (train < 0)
            return;
        const LogicRecord &rec = g.view.logic[train];
        check(rec.speed == fxFromInt(100) && rec.extra[0] == static_cast<std::uint32_t>(fxFromInt(30)) &&
                  rec.extra[1] == static_cast<std::uint32_t>(fxFromInt(8)) &&
                  (rec.flags & kLogicSelfDriven) &&
                  (g.view.brushes[rec.brush].flags & kBrushRoams),
              "cooked with its speed, wheels and height, and marked as one that roams");
        // Out of the way, on the floor.
        g.player.origin[0] = fxFromInt(-380);
        g.player.origin[1] = fxFromInt(100);
        g.player.origin[2] = fxFromInt(40);

        const BrushState &car0 = g.brushStates[rec.brush];
        check(std::fabs(toUnits(car0.pos[0]) + 300) < 0.01 && std::fabs(toUnits(car0.pos[2]) - 24) < 0.01,
              "it starts on its first node, its height above the track");
        // Heading +x, and "the train points west": its yaw is half a turn.
        check(car0.angles[kYaw] == 32768, "facing along the track, back to front as the game has it");
        for (int i = 0; i < 30; ++i)
            g.frame();
        check(std::fabs(toUnits(car0.pos[0]) + 300) < 0.01, "and waits to be started");

        logicFire(g.logic, g.player, rec.name, -1, kUseToggle, 0);
        for (int i = 0; i < 30; ++i) // one second at 100
            g.frame();
        const double afterOne = toUnits(car0.pos[0]);
        check(afterOne > -210 && afterOne < -190, "started, it covers its speed in a second");
        for (int i = 0; i < 15; ++i) // half a second more: past b, at 200
            g.frame();
        const double afterMore = toUnits(car0.pos[0]);
        check(afterMore > afterOne + 85 && afterMore < afterOne + 115,
              "a node with a speed sets the speed of a train nobody drives");
        check(toUnits(g.brushStates[g.view.logic[g.recordOfKind(LogicKind::Door, 0)].brush].pos[2]) < 0.01,
              "nothing is fired before its node is reached");
        for (int i = 0; i < 20; ++i)
            g.frame();
        check(toUnits(g.brushStates[g.view.logic[g.recordOfKind(LogicKind::Door, 0)].brush].pos[2]) > 1,
              "a node's message fires as the train passes it");
        check(toUnits(g.brushStates[g.view.logic[g.recordOfKind(LogicKind::Door, 1)].brush].pos[2]) < 0.01,
              "and the end's does not, yet");
        for (int i = 0; i < 60; ++i)
            g.frame();
        check(std::fabs(toUnits(car0.pos[0]) - 200) < 0.5 && g.states[train].speed == 0,
              "it comes to rest on the last node");
        check(toUnits(g.brushStates[g.view.logic[g.recordOfKind(LogicKind::Door, 1)].brush].pos[2]) > 1,
              "which fires what the end of the track names");
        check(std::fabs(toUnits(car0.pos[1])) < 0.01 && std::fabs(toUnits(car0.pos[2]) - 24) < 0.01,
              "having kept to the track all the way");
    }

    /// A car faster than a push of 8 units a tick -- 600 a second -- and a
    /// player standing in its way in the open: it shoves them along as far
    /// as it goes each tick (SV_PushMove), and is not held up by them.
    void testFastCarPushes()
    {
        BoxRoom room;
        room.mins[0] = -600, room.maxs[0] = 600;
        room.maxs[2] = 200;
        BrushBox car;
        car.className = "func_tracktrain";
        car.mins[0] = -20, car.mins[1] = -32, car.mins[2] = -4;
        car.maxs[0] = 20, car.maxs[1] = 32, car.maxs[2] = 60;
        car.keys = "\"targetname\" \"car\"\n\"origin\" \"0 0 0\"\n\"target\" \"a\"\n"
                   "\"speed\" \"600\"\n\"height\" \"8\"\n\"wheels\" \"30\"\n\"spawnflags\" \"2\"\n";
        room.brushes.push_back(car);
        room.extraEntities =
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"a\"\n\"target\" \"b\"\n"
            "\"origin\" \"-400 0 16\"\n}\n"
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"b\"\n"
            "\"origin\" \"300 0 16\"\n}\n";
        Game g;
        std::string error;
        g.ok = g.bsp.parse(buildBoxRoom(room), error);
        if (g.ok)
            finish(g, nullptr);
        check(g.ok, "fast car room cooks");
        if (!g.ok)
            return;
        const int train = g.recordOfKind(LogicKind::TrackTrain);
        if (train < 0)
            return;
        const LogicRecord &rec = g.view.logic[train];
        const BrushState &body = g.brushStates[rec.brush];
        g.player.origin[0] = 0, g.player.origin[1] = 0, g.player.origin[2] = fxFromInt(37);
        g.frame();
        logicFire(g.logic, g.player, rec.name, -1, kUseToggle, 0);
        for (int i = 0; i < 60; ++i)
            g.frame();
        std::printf("test_track: fast car: at x %.1f, the player at x %.1f, health %d\n", toUnits(body.pos[0]),
                    toUnits(g.player.origin[0]), g.player.health);
        check(std::fabs(toUnits(body.pos[0]) - 300) < 0.5, "a car at 600 a second is not held up by the player");
        check(toUnits(g.player.origin[0]) > 300 + 20 + 16 - 1, "it pushes them along in front of it");
    }

    /// CFuncTrackTrain::Blocked: a car that pins the player against a wall
    /// shoves them off at its "dmg" and hurts them by as much -- and does
    /// not go through them. (One it can push along, it pushes.)
    void testTrainShoves()
    {
        BoxRoom room;
        room.mins[0] = -400, room.maxs[0] = 400;
        BrushBox car;
        car.className = "func_tracktrain";
        car.mins[0] = -20, car.mins[1] = -10, car.mins[2] = -4;
        car.maxs[0] = 20, car.maxs[1] = 10, car.maxs[2] = 4;
        car.keys = "\"targetname\" \"car\"\n\"origin\" \"0 0 0\"\n\"target\" \"a\"\n"
                   "\"speed\" \"100\"\n\"height\" \"8\"\n\"wheels\" \"30\"\n\"spawnflags\" \"2\"\n\"dmg\" \"20\"\n";
        room.brushes.push_back(car);
        // A wall across the track at 200: the player stands against it.
        BrushBox wall;
        wall.className = "func_wall";
        wall.mins[0] = 200, wall.mins[1] = -60, wall.mins[2] = 0;
        wall.maxs[0] = 220, wall.maxs[1] = 60, wall.maxs[2] = 128;
        room.brushes.push_back(wall);
        room.extraEntities =
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"a\"\n\"target\" \"b\"\n"
            "\"origin\" \"-300 0 16\"\n}\n"
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"b\"\n"
            "\"origin\" \"300 0 16\"\n}\n";
        Game g;
        std::string error;
        g.ok = g.bsp.parse(buildBoxRoom(room), error);
        if (g.ok)
            finish(g, nullptr);
        check(g.ok, "shoving room cooks");
        if (!g.ok)
            return;
        const int train = g.recordOfKind(LogicKind::TrackTrain);
        const LogicRecord &rec = g.view.logic[train];
        check(rec.damage == 20, "a track train's dmg is cooked");
        g.player.origin[0] = fxFromInt(200 - 17);
        g.player.origin[1] = 0;
        g.player.origin[2] = fxFromInt(36);
        g.player.health = 100;
        g.player.inv.armor = 0;
        logicFire(g.logic, g.player, rec.name, -1, kUseToggle, 0);
        bool shoved = false;
        for (int i = 0; i < 150 && g.player.health == 100; ++i)
        {
            g.frame();
            shoved = shoved || g.player.velocity[0] != 0 || g.player.velocity[1] != 0;
        }
        const BrushState &at = g.brushStates[rec.brush];
        std::printf("test_track: run into by a car of dmg 20: health %d, car at %.0f, player at %.0f\n",
                    g.player.health, toUnits(at.pos[0]), toUnits(g.player.origin[0]));
        check(g.player.health == 80 && shoved, "a car that pins the player shoves them and hurts by its dmg");
        check(toUnits(at.pos[0]) + 20 <= toUnits(g.player.origin[0]) - 15.9, "... and has not gone through them");
    }

    /// How the passenger behaves on a ride.
    enum class Passenger
    {
        Idle,    ///< never touches the controls
        Restless ///< walks into every wall in turn, jumps, ducks
    };

    /// Whether the player is where a passenger should be: inside the car's
    /// own box, taken in the car's own space (it turns), and not inside what
    /// it is made of.
    bool aboard(const Game &game, int brush, double &worst)
    {
        const Brush &built = game.view.brushes[brush];
        const MoverHull &hull = game.movers[brush];
        fx local[3];
        hldata::detail::toMoverSpace(hull, game.player.origin, local);
        bool inside = true;
        for (int a = 0; a < 3; ++a)
        {
            const fx lo = fxFromInt(built.mins[a]) - built.origin[a];
            const fx hi = fxFromInt(built.maxs[a]) - built.origin[a];
            if (local[a] < lo || local[a] > hi)
            {
                inside = false;
                worst = std::max(worst, toUnits(local[a] < lo ? lo - local[a] : local[a] - hi));
            }
        }
        return inside &&
               hullPositionFree(game.player.ducked ? hull.crouched : hull.standing, local);
    }

    /// The opening ride, from the first map to the station, with `vblanks`
    /// sixtieths of a second to a frame. Returns false -- having said where --
    /// at the first frame the passenger is not aboard.
    bool ride(const char *dir, int vblanks, Passenger passenger, bool saveHalfWay)
    {
        Globals globals;
        std::string map = "c0a0";
        std::string route = map;
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
        {
            check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
            return true;
        }
        finish(*game, &globals);
        if (!game->ok)
            return false;

        const int ms = (vblanks * 1000 + 30) / 60;
        const std::int32_t dt = 1092 * vblanks; // sixtieths of a second, Q16
        const char *who = passenger == Passenger::Idle ? "idle" : "restless";
        std::uint16_t yaw = 0;
        long elapsed = 0;
        int restMs = 0;
        bool wasAboard = false;
        for (int frame = 0; elapsed < 12L * 60 * 1000; ++frame, elapsed += ms)
        {
            // What the passenger does: three seconds each of walking forwards,
            // left, backwards and right as they see it, a jump at the start of
            // each second and a duck through the last of every four.
            MoveCommand cmd;
            if (passenger == Passenger::Restless)
            {
                const long second = elapsed / 1000;
                const int phase = static_cast<int>((second / 3) % 4);
                cmd.forward = phase == 0 ? 1 : (phase == 2 ? -1 : 0);
                cmd.side = phase == 1 ? -1 : (phase == 3 ? 1 : 0);
                cmd.jump = elapsed % 1000 < 100;
                cmd.duck = second % 4 == 3;
            }
            fx sine, cosine;
            fxSinCos(yaw, sine, cosine);
            cmd.forwardXY[0] = cosine, cmd.forwardXY[1] = sine;
            cmd.rightXY[0] = sine, cmd.rightXY[1] = -cosine;
            cmd.viewForward[0] = cosine, cmd.viewForward[1] = sine, cmd.viewForward[2] = 0;
            cmd.viewRight[0] = sine, cmd.viewRight[1] = -cosine, cmd.viewRight[2] = 0;

            logicTick(game->logic, game->player, ms);
            yaw = static_cast<std::uint16_t>(yaw + game->logic.viewTurn);
            game->logic.viewTurn = 0;
            playerMove(game->player, cmd, game->move, dt);
            // A move can end a hair inside a corner; the next one begins by
            // edging out of it. The passenger is judged as the next move will
            // find them.
            playerSettle(game->player, game->move);

            const int train = game->tram();
            const int brush = train >= 0 ? game->view.logic[train].brush : -1;
            double out = 0;
            const bool on = brush >= 0 && aboard(*game, brush, out);
            // The restless one starts by walking about; they count as aboard
            // from the first frame they are, and must stay so.
            if (wasAboard && !on)
            {
                fx local[3];
                hldata::detail::toMoverSpace(game->movers[brush], game->player.origin, local);
                std::printf("test_track: %d ms steps, %s passenger: off the tram in %s after %ld s "
                            "(in its space %.1f %.1f %.1f, %.1f outside, yaw %.1f, speed %.0f)\n",
                            ms, who, map.c_str(), elapsed / 1000, toUnits(local[0]),
                            toUnits(local[1]), toUnits(local[2]), out,
                            game->brushStates[brush].angles[kYaw] * 360.0 / 65536.0,
                            toUnits(game->states[train].speed));
                return false;
            }
            wasAboard = wasAboard || on;

            // A save and a load in the middle of the third map: everything is
            // torn down and put back from what a save holds.
            if (saveHalfWay && map == "c0a0b" && frame % 4096 == 2048 && brush >= 0)
            {
                Arrival held;
                held.valid = true;
                // (The save keeps absolute places; an Arrival against a
                // landmark at the origin is the same thing.)
                const BrushState car = game->brushStates[brush];
                const fx carSpeed = game->states[train].speed;
                const bool driven = game->states[train].running;
                const std::uint16_t node = static_cast<std::uint16_t>(game->states[train].bits);
                const PlayerState player = game->player;
                auto again = std::make_unique<Game>();
                if (!again->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
                {
                    check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
                    return false;
                }
                finish(*again, &globals);
                const int t2 = again->tram();
                logicPlaceBrush(again->logic, again->view.logic[t2].brush, car.pos,
                                car.angles[kYaw]);
                again->states[t2].speed = carSpeed;
                again->states[t2].running = driven;
                const std::uint16_t near = hldata::detail::trackNearest(
                    again->logic, car.pos, static_cast<fx>(again->view.logic[t2].extra[1]));
                again->states[t2].bits = near != kNoCorner ? near : node;
                again->player = player;
                again->player.onGround = true;
                again->player.groundMover = static_cast<std::int16_t>(again->view.logic[t2].brush);
                game = std::move(again);
                saveHalfWay = false;
                continue;
            }

            if (game->logic.pendingChange >= 0)
            {
                const int record = game->logic.pendingChange;
                const Arrival arrival = makeArrival(game->logic, game->view.landmarks,
                                                    game->view.landmarkCount, game->player, record);
                map = game->view.strings + game->view.logic[record].extra[0];
                route += " " + map;
                auto next = std::make_unique<Game>();
                if (!next->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
                {
                    check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
                    return false;
                }
                finish(*next, &globals);
                if (!next->ok)
                    return false;
                applyArrival(next->logic, next->view.landmarks, next->view.landmarkCount,
                             next->view.nameHashes, next->view.nameCount, next->player, arrival);
                game = std::move(next);
                continue;
            }
            if (map == "c0a0e")
            {
                const int t = game->tram();
                restMs = (t >= 0 && game->states[t].speed == 0) ? restMs + ms : 0;
                if (restMs > 5000)
                    break;
            }
        }
        if (route != "c0a0 c0a0a c0a0b c0a0c c0a0d c0a0e")
        {
            std::printf("test_track: %d ms steps, %s passenger: the ride went %s\n", ms, who,
                        route.c_str());
            return false;
        }
        const int train = game->tram();
        const LogicRecord &rec = game->view.logic[train];
        const BrushState &car = game->brushStates[rec.brush];
        const PathCorner &station = game->view.corners[rec.firstAux];
        const double off = std::fabs(toUnits(car.pos[0] - station.pos[0])) +
                           std::fabs(toUnits(car.pos[1] - station.pos[1])) +
                           std::fabs(toUnits(car.pos[2] - station.pos[2] - static_cast<fx>(rec.extra[1])));
        if (off > 2)
        {
            std::printf("test_track: %d ms steps, %s passenger: the tram stops %.1f from the station\n",
                        ms, who, off);
            return false;
        }
        return true;
    }

    /// The end of the ride with its cast: the guard who waits at the station
    /// is in the map before it, comes along when the tram crosses, walks up
    /// and works the lock -- and the tram's door opens BECAUSE he has.
    void testTheGuard(const char *dir)
    {
        Globals globals;
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c0a0d.bsp", error))
        {
            check(false, (std::string(dir) + "/maps/c0a0d.bsp loads").c_str());
            return;
        }
        finish(*game, &globals, dir);
        check(game->ok, "c0a0d cooks with its models");
        if (!game->ok)
            return;
        std::string map = "c0a0d";
        int inE = 0;
        bool sawGuard = false, walked = false, doorMoved = false;
        fx guardStart[3] = {0, 0, 0}, doorStart[3] = {0, 0, 0};
        int door = -1;
        for (int frame = 0; frame < 30 * 60 * 4 && inE < 30 * 40; ++frame)
        {
            logicTick(game->logic, game->player, 33);
            game->logic.viewTurn = 0;
            playerMove(game->player, MoveCommand(), game->move, kPlayerTick);
            if (game->logic.pendingChange >= 0)
            {
                const int record = game->logic.pendingChange;
                const Arrival arrival =
                    makeArrival(game->logic, game->view.landmarks, game->view.landmarkCount,
                                game->player, record, game->view.modelRefs);
                map = game->view.strings + game->view.logic[record].extra[0];
                int coming = 0;
                for (const Arrival::Visitor &visitor : arrival.visitors)
                    coming += visitor.model != 0;
                std::printf("test_track: into %s with %d actors coming along\n", map.c_str(), coming);
                auto next = std::make_unique<Game>();
                if (!next->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
                {
                    check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
                    return;
                }
                finish(*next, &globals, dir);
                applyArrival(next->logic, next->view.landmarks, next->view.landmarkCount,
                             next->view.nameHashes, next->view.nameCount, next->player, arrival,
                             next->view.modelRefs);
                game = std::move(next);
                for (std::uint32_t i = 0; i < game->view.logicCount; ++i)
                {
                    // The tram's door is the one func_train of the map.
                    if (game->view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Train))
                        door = game->view.logic[i].brush;
                }
                if (door >= 0)
                {
                    for (int a = 0; a < 3; ++a)
                        doorStart[a] = game->brushStates[door].pos[a];
                }
                continue;
            }
            if (map != "c0a0e")
                continue;
            ++inE;
            for (const ActorState &actor : game->actorStates)
            {
                if (!actor.active || actor.nameHash != nameHash("barney1"))
                    continue;
                if (!sawGuard)
                {
                    sawGuard = true;
                    for (int a = 0; a < 3; ++a)
                        guardStart[a] = actor.pos[a];
                }
                const fx d[3] = {actor.pos[0] - guardStart[0], actor.pos[1] - guardStart[1], 0};
                if (fxLength(d) > fxFromInt(100) && !doorMoved)
                    walked = true;
            }
            if (door >= 0)
            {
                const fx d[3] = {game->brushStates[door].pos[0] - doorStart[0],
                                 game->brushStates[door].pos[1] - doorStart[1],
                                 game->brushStates[door].pos[2] - doorStart[2]};
                if (fxLength(d) > fxFromInt(4))
                {
                    if (!doorMoved)
                        std::printf("test_track: the tram's door opens %d s after arriving\n", inE / 30);
                    doorMoved = true;
                }
            }
        }
        check(map == "c0a0e", "the tram gets from c0a0d to the station");
        check(sawGuard, "the guard comes along from the map before");
        check(walked, "he walks to the tram before its door moves");
        check(doorMoved, "and then the door opens");
    }

    /// A lift between two maps (a func_train known by the same name in
    /// both): ridden up from one map into the next, it carries the player on
    /// to the top of the next -- it does not start again at the bottom of the
    /// shaft, from under their feet, nor stop halfway.
    struct Lift
    {
        const char *from;
        const char *to;
        const char *name;
        /// Where the top of its path in `to` is, its middle's height; 0 =
        /// not checked.
        int top;
    };
    /// c1a3d's to c1a3a, whose corners line up; and c1a3a's to c1a3b, whose
    /// do not (c1a3a's top corner is 24 units higher).
    constexpr Lift kLifts[] = {{"c1a3d", "c1a3a", "c1a3_lift01", 0}, {"c1a3a", "c1a3b", "c1a3_lift02", 608}};

    /// The lift, by its own name; and with `asOld`, every train's global
    /// name forgotten, as in a map converted before it was kept.
    int liftOf(Game &g, const char *name, bool asOld)
    {
        int lift = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            auto &record = const_cast<LogicRecord &>(g.view.logic[i]);
            if (record.kind != static_cast<std::uint8_t>(LogicKind::Train))
                continue;
            if (asOld)
                record.extra[3] = 0;
            if (record.name != 0 && g.view.nameHashes[record.name] == nameHash(name))
                lift = static_cast<int>(i);
        }
        return lift;
    }

    void testTheLift(const char *dir, const Lift &which, bool asOld)
    {
        std::printf("test_track: the lift from %s to %s%s\n", which.from, which.to, asOld ? " (as old)" : "");
        Globals globals;
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + which.from + ".bsp", error))
        {
            check(false, (std::string(dir) + "/maps/" + which.from + ".bsp loads").c_str());
            return;
        }
        finish(*game, &globals);
        const int lift = game->ok ? liftOf(*game, which.name, asOld) : -1;
        check(lift >= 0 && (asOld || game->view.logic[lift].extra[3] != 0),
              "the first map's lift is there (known by a global name, in a map converted now)");
        if (lift < 0)
            return;
        // Stood on it, in the middle, and it is set going.
        {
            const MoverHull &hull = game->movers[game->view.logic[lift].brush];
            game->player.origin[0] = (hull.mins[0] + hull.maxs[0]) / 2;
            game->player.origin[1] = (hull.mins[1] + hull.maxs[1]) / 2;
            game->player.origin[2] = hull.maxs[2] + fxFromInt(37);
        }
        for (int f = 0; f < 30; ++f)
            game->frame();
        logicUse(game->logic, game->player, lift, -1, kUseToggle);
        bool crossed = false;
        for (int f = 0; f < 60 * 30 && !crossed; ++f)
        {
            game->frame();
            if (game->logic.pendingChange < 0)
                continue;
            const int record = game->logic.pendingChange;
            const Arrival arrival =
                makeArrival(game->logic, game->view.landmarks, game->view.landmarkCount, game->player, record);
            const std::string map = game->view.strings + game->view.logic[record].extra[0];
            check(map == which.to, "the lift's level change is to the next map");
            auto next = std::make_unique<Game>();
            if (!next->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
            {
                check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
                return;
            }
            finish(*next, &globals);
            liftOf(*next, which.name, asOld);
            applyArrival(next->logic, next->view.landmarks, next->view.landmarkCount, next->view.nameHashes,
                         next->view.nameCount, next->player, arrival);
            game = std::move(next);
            crossed = true;
        }
        check(crossed, "ridden up, the lift takes the player into the next map");
        if (!crossed)
            return;
        const int up = liftOf(*game, which.name, asOld);
        check(up >= 0 && (asOld || (game->view.brushes[game->view.logic[up].brush].flags & kBrushRoams)),
              "the next map's lift is there, and (converted now) seen wherever it goes");
        // (The lift that came is that one: on its way up.)
        check(up >= 0 && game->states[up].move == MoveState::GoingUp, "the lift that came up is the next map's lift");
        const fx arrivedAt = game->player.origin[2];
        double worstBelow = 0;
        bool backDown = false;
        for (int f = 0; f < 20 * 30; ++f)
        {
            game->frame();
            backDown = backDown || game->logic.pendingChange >= 0;
            if (up >= 0)
            {
                const MoverHull &hull = game->movers[game->view.logic[up].brush];
                worstBelow = std::max(worstBelow, toUnits(hull.maxs[2] - (game->player.origin[2] - fxFromInt(36))));
            }
        }
        check(up >= 0 && worstBelow < 4, "in the next map the player stands on the lift all the way");
        check(!backDown && game->player.health > 0, "... and is not sent back down");
        check(game->player.origin[2] - arrivedAt > fxFromInt(250), "... which carries them on up to the top");
        if (which.top != 0 && up >= 0)
        {
            const MoverHull &hull = game->movers[game->view.logic[up].brush];
            const double middle = toUnits((hull.mins[2] + hull.maxs[2]) / 2);
            std::printf("test_track: %s's lift stops with its middle at z %.1f\n", which.to, middle);
            check(middle > which.top - 2 && middle < which.top + 2, "... and stops at the top of this map's path");
        }
    }

    // ------------------------------------------------------------------ driving it

    /// A long car on a straight track of four nodes, with room to stand in
    /// it; node b toggles a door each time the car passes it, either way.
    /// `spawnflags` are the car's.
    std::unique_ptr<Game> drivingRoom(int spawnflags)
    {
        BoxRoom room;
        room.mins[0] = -500, room.maxs[0] = 500;
        room.maxs[2] = 256;
        BrushBox car;
        car.className = "func_tracktrain";
        car.mins[0] = -60, car.mins[1] = -40, car.mins[2] = -8;
        car.maxs[0] = 60, car.maxs[1] = 40, car.maxs[2] = 8;
        car.keys = "\"targetname\" \"car\"\n\"origin\" \"0 0 0\"\n\"target\" \"a\"\n"
                   "\"speed\" \"100\"\n\"height\" \"8\"\n\"wheels\" \"60\"\n\"spawnflags\" \"" +
                   std::to_string(spawnflags) + "\"\n";
        room.brushes.push_back(car);
        BrushBox door;
        door.className = "func_door";
        door.mins[0] = 400, door.mins[1] = 100, door.mins[2] = 0;
        door.maxs[0] = 410, door.maxs[1] = 120, door.maxs[2] = 64;
        door.keys = "\"targetname\" \"passb\"\n\"angle\" \"-1\"\n\"wait\" \"-1\"\n\"spawnflags\" \"32\"\n"
                    "\"speed\" \"400\"\n";
        room.brushes.push_back(door);
        room.extraEntities =
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"a\"\n\"target\" \"b\"\n"
            "\"origin\" \"-300 0 16\"\n}\n"
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"b\"\n\"target\" \"c\"\n"
            "\"origin\" \"-100 0 16\"\n\"message\" \"passb\"\n}\n"
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"c\"\n\"target\" \"d\"\n"
            "\"origin\" \"100 0 16\"\n}\n"
            "{\n\"classname\" \"path_track\"\n\"targetname\" \"d\"\n"
            "\"origin\" \"300 0 16\"\n}\n";
        auto g = std::make_unique<Game>();
        std::string error;
        g->ok = g->bsp.parse(buildBoxRoom(room), error);
        if (g->ok)
            finish(*g, nullptr);
        return g;
    }

    /// The player dropped onto the car's middle and let land.
    bool standOn(Game &g, const LogicRecord &car)
    {
        const MoverHull &hull = g.movers[car.brush];
        g.player.origin[0] = (hull.mins[0] + hull.maxs[0]) / 2;
        g.player.origin[1] = (hull.mins[1] + hull.maxs[1]) / 2;
        g.player.origin[2] = hull.maxs[2] + kStandingFeet + fxFromInt(4);
        for (int a = 0; a < 3; ++a)
            g.player.velocity[a] = 0;
        for (int i = 0; i < 15; ++i)
            g.frame();
        return g.player.onGround && g.player.groundMover == static_cast<int>(car.brush);
    }

    void testDriving()
    {
        auto game = drivingRoom(0);
        check(game->ok, "the driving room cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int train = g.recordOfKind(LogicKind::TrackTrain);
        const LogicRecord &rec = g.view.logic[train];
        const BrushState &car = g.brushStates[rec.brush];
        const int door = g.recordOfKind(LogicKind::Door);
        check(rec.mins[0] == 0 && rec.maxs[2] == 0 && !(rec.flags & kLogicSelfDriven),
              "a train with no controls of its own, that may be driven");
        check(!trainUse(g.logic, g.player), "USE off the train is not the train's");
        check(standOn(g, rec), "the player stands on the car");
        check(trainUse(g.logic, g.player) && trainDriven(g.logic) == train && trainSpeedStep(g.logic) == 0,
              "USE on it takes its controls, at a standstill");
        trainControl(g.logic, g.player, 1, false, false);
        trainControl(g.logic, g.player, 1, false, false);
        check(g.states[train].speed == fxFromInt(50) && trainSpeedStep(g.logic) == 2,
              "two presses of forward: half its speed");
        for (int i = 0; i < 4; ++i)
            trainControl(g.logic, g.player, 1, false, false);
        check(g.states[train].speed == fxFromInt(100) && trainSpeedStep(g.logic) == 3, "... and no more than all of it");
        const std::uint16_t facing = car.angles[kYaw];
        // On past b: the door opens.
        for (int i = 0; i < 30 * 3 && toUnits(car.pos[0]) < -20; ++i)
        {
            g.frame();
            trainControl(g.logic, g.player, 0, false, false);
        }
        check(toUnits(car.pos[0]) >= -20 && trainDriven(g.logic) == train, "it goes, the player at its controls");
        for (int i = 0; i < 10; ++i)
            g.frame();
        const fx doorOpen = g.brushStates[g.view.logic[door].brush].pos[2];
        check(doorOpen > fxFromInt(8), "passing b fires what b names");
        // Back: through stopped, to backwards.
        for (int i = 0; i < 4; ++i)
            trainControl(g.logic, g.player, -1, false, false);
        check(g.states[train].speed == 0 && trainSpeedStep(g.logic) == 0, "four presses of back: stopped");
        for (int i = 0; i < 6; ++i)
            trainControl(g.logic, g.player, -1, false, false);
        check(g.states[train].speed == -fxFromInt(100) && trainSpeedStep(g.logic) == 4, "... and on to backwards");
        for (int i = 0; i < 30 * 3 && toUnits(car.pos[0]) > -200; ++i)
        {
            g.frame();
            trainControl(g.logic, g.player, 0, false, false);
        }
        check(toUnits(car.pos[0]) <= -200, "it backs along its track");
        const int turned = static_cast<std::int16_t>(car.angles[kYaw] - facing);
        check(turned < 200 && turned > -200, "... facing the way it faced");
        for (int i = 0; i < 20; ++i)
            g.frame();
        check(g.brushStates[g.view.logic[door].brush].pos[2] < doorOpen,
              "going back past b fires what b names again");
        for (int i = 0; i < 30 * 3; ++i)
        {
            g.frame();
            trainControl(g.logic, g.player, 0, false, false);
        }
        check(std::fabs(toUnits(car.pos[0]) + 300) < 0.5 && g.states[train].speed == 0,
              "at the start of the track, it stops");
        check(trainDriven(g.logic) == train, "the player at its controls all the while");
        trainControl(g.logic, g.player, 0, true, false);
        check(trainDriven(g.logic) < 0, "a step sideways lets go");
        check(trainUse(g.logic, g.player) && trainDriven(g.logic) == train, "taken again");
        trainControl(g.logic, g.player, 0, false, true);
        check(trainDriven(g.logic) < 0, "a jump lets go");
        trainUse(g.logic, g.player);
        check(trainUse(g.logic, g.player) && trainDriven(g.logic) < 0, "and USE again lets go");

        // Forwards only: never below standing still.
        auto forwardOnly = drivingRoom(4);
        if (forwardOnly->ok)
        {
            Game &f = *forwardOnly;
            const int t = f.recordOfKind(LogicKind::TrackTrain);
            check((f.view.logic[t].flags & kLogicForwardOnly) && standOn(f, f.view.logic[t]) &&
                      trainUse(f.logic, f.player),
                  "a train driven forwards only is taken");
            trainControl(f.logic, f.player, 1, false, false);
            trainControl(f.logic, f.player, -1, false, false);
            trainControl(f.logic, f.player, -1, false, false);
            check(f.states[t].speed == 0, "... and back stops it, no further");
        }
        // A train nobody drives (spawnflags 2) is never taken.
        auto nobody = drivingRoom(2);
        if (nobody->ok)
        {
            Game &n = *nobody;
            const int t = n.recordOfKind(LogicKind::TrackTrain);
            check(standOn(n, n.view.logic[t]) && !trainUse(n.logic, n.player), "a train nobody drives is not taken");
        }
    }

    /// The record of `kind` whose name is `name`, or -1.
    int recordNamed(const Game &g, const char *name, LogicKind kind)
    {
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &record = g.view.logic[i];
            if (record.kind == static_cast<std::uint8_t>(kind) && record.name != 0 &&
                g.view.nameHashes[record.name] == nameHash(name))
                return static_cast<int>(i);
        }
        return -1;
    }

    /// On A Rail: c2a2's train is driven, along its track and into c2a2a at
    /// its controls still; c2a3's crash train only from its controls; the
    /// shark cage never.
    void testOnARail(const char *dir)
    {
        Globals globals;
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c2a2.bsp", error))
        {
            check(false, (std::string(dir) + "/maps/c2a2.bsp loads").c_str());
            return;
        }
        finish(*game, &globals);
        check(game->ok, "c2a2 cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int train = recordNamed(g, "train", LogicKind::TrackTrain);
        check(train >= 0, "c2a2's train");
        if (train < 0)
            return;
        const LogicRecord &rec = g.view.logic[train];
        check(standOn(g, rec), "the player stands in c2a2's train");
        check(trainUse(g.logic, g.player), "... and takes its controls");
        for (int i = 0; i < 3; ++i)
            trainControl(g.logic, g.player, 1, false, false);
        check(trainSpeedStep(g.logic) == 3, "three presses: fast");
        const fx from[3] = {g.brushStates[rec.brush].pos[0], g.brushStates[rec.brush].pos[1],
                            g.brushStates[rec.brush].pos[2]};
        for (int i = 0; i < 30 * 3; ++i)
        {
            g.frame();
            trainControl(g.logic, g.player, 0, false, false);
        }
        const fx *now = g.brushStates[rec.brush].pos;
        const fx moved[3] = {now[0] - from[0], now[1] - from[1], now[2] - from[2]};
        std::printf("test_track: c2a2's train went %.0f units in 3 s at the player's controls\n",
                    toUnits(fxLength(moved)));
        check(fxLength(moved) > fxFromInt(200), "it goes along its track");
        check(g.states[train].speed == 0 && trainSpeedStep(g.logic) == 3,
              "stopped at the end of its track, the display still shows the speed asked for (the game's)");
        check(trainDriven(g.logic) == train, "... with the player at its controls");

        // Into c2a2a, as though its way in were reached now.
        int change = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount && change < 0; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::ChangeLevel) &&
                std::string(g.view.strings + r.extra[0]) == "c2a2a")
                change = static_cast<int>(i);
        }
        check(change >= 0, "c2a2's way into c2a2a");
        if (change >= 0)
        {
            const Arrival arrival = makeArrival(g.logic, g.view.landmarks, g.view.landmarkCount, g.player, change);
            check(arrival.train != 0 && trainDriven(g.logic) >= 0, "the player leaves c2a2 in the train, at its controls");
            auto next = std::make_unique<Game>();
            if (next->bsp.load(std::string(dir) + "/maps/c2a2a.bsp", error))
            {
                finish(*next, &globals);
                applyArrival(next->logic, next->view.landmarks, next->view.landmarkCount, next->view.nameHashes,
                             next->view.nameCount, next->player, arrival);
                arrivalDriving(next->logic, arrival);
                const int there = trainDriven(next->logic);
                check(there >= 0 && next->states[there].speed == g.states[train].speed,
                      "... and arrives in c2a2a at them still, going as fast");
            }
            else
            {
                check(false, "c2a2a loads");
            }
        }
        const fx before = g.states[train].speed;
        trainUse(g.logic, g.player);
        check(trainDriven(g.logic) < 0 && g.states[train].speed == before, "USE lets go, and the train goes on as it was");

        // c2a3: its crash train is driven from its controls only.
        auto crash = std::make_unique<Game>();
        if (crash->bsp.load(std::string(dir) + "/maps/c2a3.bsp", error))
        {
            finish(*crash, &globals);
            const int t = recordNamed(*crash, "crashtrain", LogicKind::TrackTrain);
            check(t >= 0, "c2a3's crash train");
            if (t >= 0)
            {
                const LogicRecord &r = crash->view.logic[t];
                check(r.maxs[0] > r.mins[0] && r.maxs[2] > r.mins[2], "... with its controls cooked");
                // Its controls: the box in the train's own space, as it was
                // built (the train is where it was built at first).
                const bool on = standOn(*crash, r);
                if (on && !hldata::detail::trainOnControls(crash->logic, crash->player, t))
                    check(!trainUse(crash->logic, crash->player), "off its controls, USE does not take it");
                // (The middle of its controls, from the train's space to the
                // world's: the train has been put on its track since.)
                const Brush &built = crash->view.brushes[r.brush];
                const MoverHull &mover = crash->movers[r.brush];
                fx local[3];
                for (int a = 0; a < 3; ++a)
                    local[a] = fxFromInt(r.mins[a] + r.maxs[a]) / 2 - built.origin[a];
                for (int a = 0; a < 3; ++a)
                {
                    std::int64_t along = mover.turned ? 0 : (static_cast<std::int64_t>(local[a]) << kNormalShift);
                    for (int b = 0; b < 3 && mover.turned; ++b)
                        along += static_cast<std::int64_t>(mover.axes[3 * a + b]) * local[b];
                    crash->player.origin[a] = mover.origin[a] + static_cast<fx>(along >> kNormalShift);
                }
                for (int a = 0; a < 3; ++a)
                    crash->player.velocity[a] = 0;
                for (int i = 0; i < 15; ++i)
                    crash->frame();
                // (Where the train first stands it is level with the floor
                // beneath: the player comes to it on it, from c2a2g, as
                // applyArrival puts them -- standing on it.)
                crash->player.onGround = true;
                crash->player.groundMover = static_cast<std::int16_t>(r.brush);
                check(trainUse(crash->logic, crash->player) && trainDriven(crash->logic) == t,
                      "on its controls, it is taken");
            }
        }
        else
        {
            check(false, "c2a3 loads");
        }
        // c2a3a: the shark cage (spawnflags 3) is nobody's to drive.
        auto cage = std::make_unique<Game>();
        if (cage->bsp.load(std::string(dir) + "/maps/c2a3a.bsp", error))
        {
            finish(*cage, &globals);
            const int t = recordNamed(*cage, "sharkcage", LogicKind::TrackTrain);
            check(t >= 0 && !hldata::detail::trainOnControls(cage->logic, cage->player, t) &&
                      (cage->view.logic[t].flags & kLogicSelfDriven),
                  "c2a3a's shark cage is nobody's to drive");
        }
        else
        {
            check(false, "c2a3a loads");
        }
    }

    /// c1a1c's lift down the shaft (func_train "pl", diagonally down a rail).
    /// Under its way lies a func_friction of modifier 0: a slab along the
    /// slope, whose bounds fill the whole shaft. The game touches a brush
    /// trigger only with the player's origin inside the brush's own hull,
    /// so whoever rides the lift is never in it and walks about on it as on
    /// any floor -- at every frame rate, without being thrown off it.
    void testTheShaftLift(const char *dir)
    {
        for (int vblanks = 1; vblanks <= 4; ++vblanks)
        {
            auto game = std::make_unique<Game>();
            std::string error;
            if (!game->bsp.load(std::string(dir) + "/maps/c1a1c.bsp", error))
            {
                check(false, (std::string(dir) + "/maps/c1a1c.bsp loads").c_str());
                return;
            }
            finish(*game, nullptr);
            Game &g = *game;
            const int lift = recordNamed(g, "pl", LogicKind::Train);
            check(lift >= 0, "c1a1c's lift down the shaft");
            if (lift < 0)
                return;
            int slab = -1;
            for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
                if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Friction))
                    slab = static_cast<int>(i);
            check(slab >= 0 && g.view.logic[slab].pad != 0, "its slab of no friction is cooked with its shape");
            const LogicRecord &rec = g.view.logic[lift];
            check(standOn(g, rec), "the player stands on it");
            logicFire(g.logic, g.player, rec.name, -1, kUseToggle, 0);
            const MoverHull &hull = g.movers[rec.brush];
            int offLift = 0, frames = 0, slipped = 0;
            long offFor = 0, longestOff = 0;
            double lowest = 0;
            double walked = 0;
            for (long elapsed = 0; elapsed < 20000; elapsed += vblanks * 1000 / 60, ++frames)
            {
                // About the lift, to points well inside its edges.
                MoveCommand cmd;
                const int phase = static_cast<int>((elapsed / 800) % 4);
                const fx to[2] = {(hull.mins[0] + hull.maxs[0]) / 2 + fxFromInt(phase == 0 ? 80 : (phase == 2 ? -80 : 0)),
                                  (hull.mins[1] + hull.maxs[1]) / 2 + fxFromInt(phase == 1 ? 70 : (phase == 3 ? -70 : 0))};
                const double dx = toUnits(to[0] - g.player.origin[0]), dy = toUnits(to[1] - g.player.origin[1]);
                const double len = std::hypot(dx, dy);
                if (len > 24)
                {
                    cmd.forward = 1;
                    cmd.forwardXY[0] = static_cast<fx>(dx / len * kFxOne);
                    cmd.forwardXY[1] = static_cast<fx>(dy / len * kFxOne);
                    cmd.rightXY[0] = cmd.forwardXY[1];
                    cmd.rightXY[1] = -cmd.forwardXY[0];
                }
                const fx before[2] = {g.player.origin[0], g.player.origin[1]};
                logicTick(g.logic, g.player, (vblanks * 1000 + 30) / 60);
                slipped += g.player.slip.value != 0;
                const fx carried[2] = {g.player.origin[0] - before[0], g.player.origin[1] - before[1]};
                monstersTick(g.logic, g.player, (vblanks * 1000 + 30) / 60);
                playerMove(g.player, cmd, g.move, 1092 * vblanks);
                g.drain();
                walked += std::hypot(toUnits(g.player.origin[0] - before[0] - carried[0]),
                                     toUnits(g.player.origin[1] - before[1] - carried[1]));
                const bool off = !g.player.onGround || g.player.groundMover != static_cast<int>(rec.brush);
                offLift += off;
                offFor = off ? offFor + vblanks * 1000 / 60 : 0;
                longestOff = std::max(longestOff, offFor);
                lowest = std::min(lowest, toUnits(g.player.origin[2] - kStandingFeet - hull.maxs[2]));
            }
            std::printf("test_track: c1a1c's lift, %d vblank(s): %d of %d frames off it (at most %ld ms at a time, "
                        "the hop off its raised strip), %d without friction, %.0f units walked on it\n",
                        vblanks, offLift, frames, longestOff, slipped, walked);
            check(slipped == 0, "riding c1a1c's shaft lift is not riding in its slab of no friction");
            check(walked > 1000, "... the player walks about on it");
            check(longestOff <= 250 && lowest > -4, "... and is never thrown off it");
        }
    }

    void testTheRide()
    {
        const char *dir = std::getenv("HLDS_VALVE_DIR");
        if (dir == nullptr || *dir == '\0')
        {
            std::printf("test_track: HLDS_VALVE_DIR not set, the real ride is skipped\n");
            return;
        }
        // Every frame rate the console runs at
        // (60 a second; 30, 20 and 15 of a frame that ran long)
        // with a passenger who stands still and one who will not.
        for (int vblanks = 1; vblanks <= 4; ++vblanks)
        {
            check(ride(dir, vblanks, Passenger::Idle, false),
                  "the tram reaches the station with a passenger who stands still");
            check(ride(dir, vblanks, Passenger::Restless, false),
                  "... and with one who walks about, jumps and ducks all the way");
        }
        check(ride(dir, 2, Passenger::Idle, true), "... and with a save and a load on the way");
        testTheGuard(dir);
        testOnARail(dir);
        testTheShaftLift(dir);
        for (const Lift &lift : kLifts)
        {
            testTheLift(dir, lift, false);
            testTheLift(dir, lift, true); // (maps from a converter before the lift's global name was kept)
        }
    }
} // namespace

int main()
{
    testSyntheticTrack();
    testFastCarPushes();
    testTrainShoves();
    testDriving();
    testTheRide();
    if (g_failures == 0)
        std::printf("test_track: ok\n");
    return g_failures == 0 ? 0 : 1;
}
