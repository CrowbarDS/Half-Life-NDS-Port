// Xen's ways (HlLogic.hpp's trigger volumes, HlPlayerMove.hpp's move): a
// trigger_gravity makes the world pull the player less from then on, into
// the next map and through a save; a trigger_push is a base velocity, a push
// up that is an acceleration and a carry along the ground, as pm_shared has
// it -- so a column that pushes up throws a player who jumps into it high,
// and puts them down again alive, and one who stays in it builds up no
// speed the world does not show.
//
// On a synthetic room, and with HLDS_VALVE_DIR on the game's own: c4a1's
// arrival, carried into c4a1a, and c4a3's pads.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "HlView.hpp"
#include "TestBspBuilder.hpp"
#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    double toUnits(fx v) { return v / 4096.0; }

    /// The worst fall the player has landed from (fallDamage of it), since
    /// it was last looked at.
    int worstFall = 0;

    /// A frame: the logic, then the player's move with `cmd`.
    void step(Game &g, const MoveCommand &cmd = MoveCommand())
    {
        logicTick(g.logic, g.player, 33);
        g.logic.viewTurn = 0;
        playerMove(g.player, cmd, g.move, kPlayerTick);
        if (g.player.landed > 0)
            worstFall = std::max(worstFall, fallDamage(fxToInt(g.player.landed)));
        g.drain();
    }

    /// Stood at (x, y) on the floor, still.
    void standAt(Game &g, int x, int y)
    {
        g.player.origin[0] = fxFromInt(x);
        g.player.origin[1] = fxFromInt(y);
        g.player.origin[2] = kStandingFeet + fxFromInt(1);
        for (int a = 0; a < 3; ++a)
            g.player.velocity[a] = 0;
        for (int i = 0; i < 10; ++i)
            step(g);
    }

    /// How high a jump from where they stand takes them.
    double jumpHeight(Game &g)
    {
        const fx from = g.player.origin[2];
        fx top = from;
        MoveCommand jump;
        jump.jump = true;
        step(g, jump);
        for (int i = 0; i < 90 && !g.player.onGround; ++i)
        {
            top = std::max(top, g.player.origin[2]);
            step(g);
        }
        for (int i = 0; i < 10; ++i)
            step(g); // (jump let go of)
        return toUnits(top - from);
    }

    std::unique_ptr<Game> cook(BoxRoom &room, Globals *globals)
    {
        auto g = std::make_unique<Game>();
        std::string error;
        g->ok = g->bsp.parse(buildBoxRoom(room), error);
        if (g->ok)
            finish(*g, globals);
        return g;
    }

    BrushBox volume(const char *cls, float x0, float y0, float z0, float x1, float y1, float z1, std::string keys)
    {
        BrushBox box;
        box.className = cls;
        box.mins[0] = x0, box.mins[1] = y0, box.mins[2] = z0;
        box.maxs[0] = x1, box.maxs[1] = y1, box.maxs[2] = z1;
        box.keys = std::move(keys);
        return box;
    }

    void testGravity()
    {
        BoxRoom room;
        room.mins[0] = -512, room.maxs[0] = 512;
        room.maxs[2] = 512;
        room.brushes.push_back(volume("trigger_gravity", -400, -64, 0, -300, 64, 8, "\"gravity\" \".5\"\n"));
        Globals globals;
        auto game = cook(room, &globals);
        check(game->ok, "the gravity room cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int field = g.recordOfKind(LogicKind::TriggerGravity);
        check(field >= 0 && g.view.logic[field].extra[0] == 50, "a trigger_gravity, of half");
        standAt(g, 200, 0);
        const double earth = jumpHeight(g);
        standAt(g, -350, 0); // in it
        standAt(g, 200, 0);  // and out again
        const double xen = jumpHeight(g);
        std::printf("test_xen: a jump goes %.1f units up, %.1f at half the pull\n", earth, xen);
        check(earth > 40 && earth < 50, "a jump as the game's: 45 units");
        check(xen > earth * 1.8 && xen < earth * 2.2, "touched, half the pull: twice as high, after it too");
        check(globals.get(kGlobalPlayerGravity) == 50, "... and kept among the game's switches");

        // The next map, with the same switches: the same pull.
        auto next = cook(room, &globals);
        if (next->ok)
        {
            standAt(*next, 200, 0);
            check(next->player.gravityPercent.value == 50, "the next map pulls the player as little");
        }
        // A volume of 1 puts it back.
        BoxRoom back = room;
        back.brushes[0].keys = "\"gravity\" \"1\"\n";
        auto earthAgain = cook(back, &globals);
        if (earthAgain->ok)
        {
            standAt(*earthAgain, -350, 0);
            standAt(*earthAgain, 200, 0);
            check(earthAgain->player.gravityPercent.value == 0 && std::fabs(jumpHeight(*earthAgain) - earth) < 2,
                  "and a trigger_gravity of 1 puts it back as it was");
        }
    }

    void testPadColumn()
    {
        // A column 160 high that pushes up at 2500, in a room at 0.3 of the
        // pull (c4a3's pads).
        BoxRoom room;
        room.mins[0] = -512, room.maxs[0] = 512;
        room.maxs[2] = 2048;
        room.brushes.push_back(volume("trigger_push", -40, -40, 6, 40, 40, 166,
                                      "\"speed\" \"2500\"\n\"angle\" \"-1\"\n"));
        Globals globals;
        globals.set(kGlobalPlayerGravity, 30);
        auto game = cook(room, &globals);
        check(game->ok, "the pad room cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int pad = g.recordOfKind(LogicKind::TriggerPush);
        check(pad >= 0 && g.view.logic[pad].move[2] == fxFromInt(2500), "a push up of 2500");
        standAt(g, 0, 0);
        const fx floorZ = g.player.origin[2];
        check(g.player.gravityPercent.value == 30, "the room's pull: 0.3 of it");
        worstFall = 0;
        MoveCommand jump;
        jump.jump = true;
        step(g, jump);
        fx top = g.player.origin[2], fastest = 0;
        for (int i = 0; i < 30 * 12; ++i)
        {
            step(g);
            top = std::max(top, g.player.origin[2]);
            fastest = std::max(fastest, g.player.velocity[2] < 0 ? -g.player.velocity[2] : g.player.velocity[2]);
        }
        std::printf("test_xen: a jump onto the pad: up %.0f units, at most %.0f a second, the worst landing %d\n",
                    toUnits(top - floorZ), toUnits(fastest), worstFall);
        check(toUnits(top - floorZ) > 600, "a jump onto the pad throws the player high above it");
        check(toUnits(fastest) < 1200, "... no faster than the push and the pull make them");
        check(worstFall == 0, "... and they come down on it unhurt");

        // On it, standing, they are on the ground: the game zeroes what goes
        // up of a player stood on the ground, push and all.
        standAt(g, 0, 0);
        for (int i = 0; i < 30; ++i)
            step(g);
        check(g.player.onGround && std::fabs(toUnits(g.player.origin[2] - floorZ)) < 1,
              "stood on the pad without jumping, they stay stood (pm_shared's)");

        // The old way's harm: hovering in a column, a speed downwards that
        // grows unseen. A column too weak to throw them: they float in it,
        // and leave it no faster than they went in.
        BoxRoom weak = room;
        weak.brushes[0].keys = "\"speed\" \"300\"\n\"angle\" \"-1\"\n";
        auto hover = cook(weak, &globals);
        if (hover->ok)
        {
            Game &h = *hover;
            standAt(h, 0, 0);
            worstFall = 0;
            step(h, jump);
            for (int i = 0; i < 30 * 6; ++i)
                step(h);
            check(worstFall == 0 && h.player.onGround, "a weak push up only slows a fall: no harm");
        }
    }

    void testCarryAndOnce()
    {
        BoxRoom room;
        room.mins[0] = -512, room.maxs[0] = 512;
        room.maxs[2] = 512;
        room.brushes.push_back(volume("trigger_push", -300, -64, 0, 100, 64, 24, "\"speed\" \"150\"\n\"angle\" \"0\"\n"));
        room.brushes.push_back(volume("trigger_push", 300, -64, 0, 400, 64, 24,
                                      "\"speed\" \"300\"\n\"angle\" \"-1\"\n\"spawnflags\" \"1\"\n"));
        auto game = cook(room, nullptr);
        check(game->ok, "the carry room cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        standAt(g, -250, 0);
        const fx from = g.player.origin[0];
        for (int i = 0; i < 30; ++i)
            step(g);
        const double carried = toUnits(g.player.origin[0] - from);
        std::printf("test_xen: a push along of 150 carried a player stood in it %.0f units in a second\n", carried);
        check(carried > 130 && carried < 170, "a push along the ground carries them at its speed");
        standAt(g, 200, 0);
        const fx out = g.player.origin[0];
        for (int i = 0; i < 30; ++i)
            step(g);
        check(std::fabs(toUnits(g.player.origin[0] - out)) < 1 && std::fabs(toUnits(g.player.velocity[0])) < 1,
              "out of it, they stand, with nothing of it left in them");

        const int once = g.recordOfKind(LogicKind::TriggerPush, 1);
        check(once >= 0 && (g.view.logic[once].flags & kLogicFireOnce), "a push once");
        standAt(g, 350, 0);
        check(g.states[once].dead, "it has thrown them, and is gone");
        standAt(g, 350, 0);
        MoveCommand none;
        step(g, none);
        check(g.player.onGround && g.player.velocity[2] == 0, "... and does not again");
    }

    void testTheGame(const char *dir)
    {
        Globals globals;
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c4a1.bsp", error))
        {
            check(false, (std::string(dir) + "/maps/c4a1.bsp loads").c_str());
            return;
        }
        finish(*game, &globals);
        check(game->ok, "c4a1 cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int field = g.recordOfKind(LogicKind::TriggerGravity);
        check(field >= 0 && g.view.logic[field].extra[0] == 60, "c4a1's arrival pulls at 0.6");
        if (field < 0)
            return;
        // Dropped into it from above, as the player comes into Xen.
        const LogicRecord &arrival = g.view.logic[field];
        g.player.origin[0] = fxFromInt((arrival.mins[0] + arrival.maxs[0]) / 2);
        g.player.origin[1] = fxFromInt((arrival.mins[1] + arrival.maxs[1]) / 2);
        g.player.origin[2] = fxFromInt(arrival.maxs[2]) + kStandingFeet + fxFromInt(40);
        for (int i = 0; i < 60; ++i)
            step(g);
        check(g.player.gravityPercent.value == 60 && globals.get(kGlobalPlayerGravity) == 60,
              "landed in it, the player is pulled at 0.6");
        // On into c4a1a: the switches go along.
        auto next = std::make_unique<Game>();
        if (next->bsp.load(std::string(dir) + "/maps/c4a1a.bsp", error))
        {
            finish(*next, &globals);
            step(*next);
            check(next->player.gravityPercent.value == 60, "c4a1a pulls the player at 0.6 still");
        }
        else
        {
            check(false, "c4a1a loads");
        }

        // c4a2a: stood on its pod (a push up over a door that pops when
        // it is stepped on), as the real game has it: the pop only lifts
        // them with it; a jump from it throws them up its tube, and they
        // come down unhurt.
        auto pods = std::make_unique<Game>();
        if (pods->bsp.load(std::string(dir) + "/maps/c4a2a.bsp", error))
        {
            finish(*pods, &globals);
            Game &p = *pods;
            globals.set(kGlobalPlayerGravity, 60);
            ActorState probe;
            probe.pos[0] = fxFromInt(-496);
            probe.pos[1] = fxFromInt(-336);
            probe.pos[2] = fxFromInt(-250);
            actorToFloor(p.move, probe, 0, fxFromInt(128));
            for (int a = 0; a < 3; ++a)
                p.player.origin[a] = probe.pos[a];
            p.player.origin[2] += kStandingFeet;
            p.player.onGround = true;
            const fx stood = p.player.origin[2];
            fx high = stood;
            for (int i = 0; i < 30 * 3; ++i)
            {
                step(p);
                high = std::max(high, p.player.origin[2]);
            }
            std::printf("test_xen: c4a2a, stood on the pod: lifted %.0f units at most\n", toUnits(high - stood));
            check(toUnits(high - stood) < 50, "c4a2a's pod only lifts whoever stands on it, its own 46 units");
            worstFall = 0;
            MoveCommand jump;
            jump.jump = true;
            const fx from = p.player.origin[2];
            step(p, jump);
            high = from;
            for (int i = 0; i < 30 * 6; ++i)
            {
                step(p);
                high = std::max(high, p.player.origin[2]);
            }
            std::printf("test_xen: c4a2a, a jump from the pod: up %.0f units, the worst landing %d\n",
                        toUnits(high - from), worstFall);
            check(toUnits(high - from) > 200 && worstFall == 0, "... a jump from it throws them up, and down unhurt");
        }
        else
        {
            check(false, "c4a2a loads");
        }

        // c4a3: a jump onto one of its pads.
        auto lair = std::make_unique<Game>();
        if (!lair->bsp.load(std::string(dir) + "/maps/c4a3.bsp", error))
        {
            check(false, (std::string(dir) + "/maps/c4a3.bsp loads").c_str());
            return;
        }
        finish(*lair, &globals);
        Game &l = *lair;
        globals.set(kGlobalPlayerGravity, 30);
        int pad = -1;
        for (int n = 0; pad < 0; ++n)
        {
            const int i = l.recordOfKind(LogicKind::TriggerPush, n);
            if (i < 0)
                break;
            if (l.view.logic[i].move[2] == fxFromInt(2500))
                pad = i;
        }
        check(pad >= 0, "c4a3's pads: pushes up of 2500");
        if (pad < 0)
            return;
        const LogicRecord &column = l.view.logic[pad];
        ActorState probe;
        probe.pos[0] = fxFromInt((column.mins[0] + column.maxs[0]) / 2);
        probe.pos[1] = fxFromInt((column.mins[1] + column.maxs[1]) / 2);
        probe.pos[2] = fxFromInt(column.mins[2] + 16);
        actorToFloor(l.move, probe, 0, fxFromInt(256));
        for (int a = 0; a < 3; ++a)
            l.player.origin[a] = probe.pos[a];
        l.player.origin[2] += kStandingFeet + fxFromInt(1);
        for (int i = 0; i < 10; ++i)
            step(l);
        const fx from = l.player.origin[2];
        worstFall = 0;
        MoveCommand jump;
        jump.jump = true;
        step(l, jump);
        fx top = from;
        for (int i = 0; i < 30 * 10; ++i)
        {
            step(l);
            top = std::max(top, l.player.origin[2]);
        }
        std::printf("test_xen: c4a3, a jump onto a pad: up %.0f units, the worst landing %d\n", toUnits(top - from),
                    worstFall);
        check(toUnits(top - from) > 400, "c4a3's pad throws the player high");
        check(worstFall < 100, "... and they come down alive");
    }
} // namespace

int main()
{
    testGravity();
    testPadColumn();
    testCarryAndOnce();
    const char *valve = std::getenv("HLDS_VALVE_DIR");
    if (valve != nullptr && *valve != '\0')
        testTheGame(valve);
    else
        std::printf("test_xen: HLDS_VALVE_DIR not set, the game's maps were not read\n");
    if (g_failures == 0)
        std::printf("test_xen: ok\n");
    return g_failures == 0 ? 0 : 1;
}
