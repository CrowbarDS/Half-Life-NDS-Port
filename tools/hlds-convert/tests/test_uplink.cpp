// Wheels turned by hand (momentary_rot_button) in the stock maps -- first
// Uplink's satellite dish (hldemo3).
//
// The wheel turns the dish, and with it two rails in a room out of sight
// (momentary_door). A crate stands on them and is carried along;
// thin triggers that only a box can set off stand across its way, and the
// middle one -- reached when the dish is lined up -- lights the lamp by the
// wheel, beeps, and unlocks the button that sends the signal. If the rails do
// not take the crate with them, the dish turns for nothing.
//
// A wheel tells everything it targets how far round it is while it turns;
// what is not a momentary_door is simply used, and a door opens (c3a1's
// sewer lid). One that goes back by itself when let go does so, and what it
// moves with it (c2a3a's cage), heard as it moves and stops. And a wheel
// turns while USE is held on it, as in the game.
//
// Needs HLDS_VALVE_DIR.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    int recordTargeting(const Game &g, const char *target, LogicKind kind)
    {
        const std::uint32_t hash = nameHash(target);
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(kind) && r.target != 0 && g.view.nameHashes[r.target] == hash)
                return static_cast<int>(i);
        }
        return -1;
    }

    void testDish(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/hldemo3.bsp", error))
        {
            check(false, "hldemo3 is read");
            return;
        }
        finish(*game, nullptr, dir);
        check(game->ok, "hldemo3 cooks");
        if (!game->ok)
            return;
        Game &g = *game;

        // The wheel is the one of the two that may be used; the dish turns
        // with it.
        int wheel = -1, door = -1, crate = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::MomentaryButton) && !(r.flags & kLogicTriggerOnly))
                wheel = static_cast<int>(i);
            if (r.kind == static_cast<std::uint8_t>(LogicKind::MomentaryDoor) && door < 0)
                door = static_cast<int>(i);
            // (The crate between the rails, at x 1052..1084.)
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Pushable) && r.brush != kNoBrush &&
                fxToInt(g.movers[r.brush].mins[0]) >= 1040 && fxToInt(g.movers[r.brush].maxs[0]) <= 1100)
                crate = static_cast<int>(i);
        }
        const int button = recordTargeting(g, "dish_mm", LogicKind::Button);
        check(wheel >= 0 && door >= 0 && crate >= 0 && button >= 0, "the wheel, the rails, the crate and the button are there");
        if (wheel < 0 || door < 0 || crate < 0 || button < 0)
            return;
        const int box = g.view.logic[crate].brush;
        auto run = [&](int frames)
        {
            for (int n = 0; n < frames; ++n)
            {
                logicTick(g.logic, g.player, 33);
                g.drain();
            }
        };
        // Out of the way: this is the machine's business.
        g.player.origin[2] += fxFromInt(4000);
        run(60);
        const int startX = fxToInt(g.movers[box].mins[0]);
        const int restZ = fxToInt(g.movers[box].mins[2]);
        std::printf("test_uplink: the crate rests at x %d, z %d\n", startX, restZ);
        // (On the rails' edge, not between them: the game's own crate, seen
        // in the real game, rests there too -- its middle is on the face of
        // the far rail's hull, and on the face is in it.)
        check(restZ == 316, "the crate comes to rest on the rails, as in the game");
        check(!logicMasterAllows(g.logic, button), "the button is locked before the dish is lined up");

        // Turn until the crate should be at the middle trigger (x 1372):
        // its near side at 1356.
        const fx travel = fxLength(g.view.logic[door].move);
        const std::int64_t want = (static_cast<std::int64_t>(fxFromInt(1356 - startX)) << 16) / travel;
        logicUse(g.logic, g.player, wheel, -1, kUseToggle);
        int frames = 0;
        while (g.logic.states[wheel].phase < want && frames < 30 * 30)
        {
            run(1);
            ++frames;
        }
        logicUse(g.logic, g.player, wheel, -1, kUseToggle);
        run(60);
        const int nowX = fxToInt(g.movers[box].mins[0]);
        std::printf("test_uplink: after %.1f s at the wheel (%d%% round), the crate is at x %d\n", frames / 30.0,
                    static_cast<int>(g.logic.states[wheel].phase * 100 / 65536), nowX);
        check(nowX > 1338 && nowX < 1375, "the rails carry the crate to the middle trigger");
        check(logicMasterAllows(g.logic, button), "... which unlocks the button that sends the signal");
        // The beep from the panel by the wheel, that says it is lined up.
        int beep = -1;
        for (std::uint32_t i = 0; i < g.view.soundCount; ++i)
        {
            if (std::string(g.view.sounds[i].name).find("blip2") != std::string::npos)
                beep = static_cast<int>(i);
        }
        check(beep >= 0 && g.timesHeard[static_cast<std::size_t>(beep)] == 1, "... and beeps, once");

        // Turned on past it, and back to it: the lamp goes out and on again,
        // but the beep was the game's to give once.
        logicUse(g.logic, g.player, wheel, -1, kUseToggle);
        run(30 * 3);
        logicUse(g.logic, g.player, wheel, -1, kUseToggle);
        run(60);
        const int pastX = fxToInt(g.movers[box].mins[0]);
        std::printf("test_uplink: turned on, the crate is at x %d\n", pastX);
        check(pastX > 1375, "turned on, the crate goes on past the middle trigger");
        check(beep >= 0 && g.timesHeard[static_cast<std::size_t>(beep)] == 1, "... and nothing beeps again");
    }

    int recordNamed(const Game &g, const char *name, LogicKind kind)
    {
        const std::uint32_t hash = nameHash(name);
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(kind) && r.name != 0 && g.view.nameHashes[r.name] == hash)
                return static_cast<int>(i);
        }
        return -1;
    }

    /// c3a1: the wheel by the sewer lid opens it (a func_door_rotating).
    void testSewerLid(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c3a1.bsp", error))
        {
            check(false, "c3a1 is read");
            return;
        }
        finish(*game, nullptr, dir);
        check(game->ok, "c3a1 cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int wheel = recordNamed(g, "momwheel", LogicKind::MomentaryButton);
        const int lid = recordNamed(g, "sewlid", LogicKind::DoorRotating);
        check(wheel >= 0 && lid >= 0, "the sewer lid and its wheel are there");
        if (wheel < 0 || lid < 0)
            return;
        auto run = [&](int frames)
        {
            for (int n = 0; n < frames; ++n)
            {
                logicTick(g.logic, g.player, 33);
                g.drain();
            }
        };
        g.player.origin[2] += fxFromInt(4000);
        run(30);
        check(g.logic.states[lid].move == MoveState::AtBottom, "the lid is shut");
        logicUse(g.logic, g.player, wheel, -1, kUseToggle);
        run(15);
        logicUse(g.logic, g.player, wheel, -1, kUseToggle);
        run(30 * 4);
        check(g.logic.states[lid].move == MoveState::AtTop, "a turn of the wheel opens it, and it stays open");
    }

    /// c2a3a: the cage's wheel goes back by itself when let go (flag 16, at
    /// its "returnspeed"), and the cage door with it.
    void testCageReturns(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c2a3a.bsp", error))
        {
            check(false, "c2a3a is read");
            return;
        }
        finish(*game, nullptr, dir);
        check(game->ok, "c2a3a cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int door = recordNamed(g, "cagedoor", LogicKind::MomentaryDoor);
        int wheel = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::MomentaryButton) && r.target != 0 &&
                g.view.nameHashes[r.target] == nameHash("cagedoor"))
                wheel = static_cast<int>(i);
        }
        check(wheel >= 0 && door >= 0, "the cage door and its wheel are there");
        if (wheel < 0 || door < 0)
            return;
        check(g.view.logic[wheel].move[1] > 0, "the wheel is cooked to go back by itself");
        auto run = [&](int frames)
        {
            for (int n = 0; n < frames; ++n)
            {
                logicTick(g.logic, g.player, 33);
                g.drain();
            }
        };
        g.player.origin[2] += fxFromInt(4000);
        run(30);
        logicUse(g.logic, g.player, wheel, -1, kUseToggle);
        run(45);
        logicUse(g.logic, g.player, wheel, -1, kUseToggle);
        run(2);
        const int turned = g.logic.states[wheel].phase;
        const int opened = g.logic.states[door].phase;
        std::printf("test_uplink: c2a3a's wheel let go at %d%% round, its door %d%% open\n", turned * 100 / 65536,
                    opened * 100 / 65536);
        check(turned > 0 && opened > 0, "turned, the wheel opens the cage");
        run(30 * 5);
        std::printf("test_uplink: ... five seconds on, %d%% and %d%%\n", g.logic.states[wheel].phase * 100 / 65536,
                    g.logic.states[door].phase * 100 / 65536);
        check(g.logic.states[wheel].phase == 0 && g.logic.states[door].phase == 0,
              "let go of, it goes back by itself, and the cage shuts");
        // Its sounds (movesnd 6, stopsnd 2): set off once on the way out and
        // once on the way back, and stopped at each end -- not once a frame.
        auto heard = [&](const char *name)
        {
            int n = 0;
            for (std::uint32_t i = 0; i < g.view.soundCount; ++i)
            {
                if (std::string(g.view.sounds[i].name).find(name) != std::string::npos)
                    n += g.timesHeard[i];
            }
            return n;
        };
        const int moved = heard("doormove6"), stopped = heard("doorstop2");
        std::printf("test_uplink: the cage door was heard to move %d times and stop %d\n", moved, stopped);
        check(moved >= 1 && moved <= 3 && stopped >= 1 && stopped <= 3, "the cage door sounds as it moves, and as it stops");
    }

    /// A wheel held as the game holds it: turned while USE comes in each
    /// frame, let go of when it stops, and turned the other way when taken
    /// hold of again (hldemo3's, which does not go back by itself).
    void testHeld(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/hldemo3.bsp", error))
        {
            check(false, "hldemo3 is read");
            return;
        }
        finish(*game, nullptr, dir);
        if (!game->ok)
            return;
        Game &g = *game;
        int wheel = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::MomentaryButton) && !(r.flags & kLogicTriggerOnly))
                wheel = static_cast<int>(i);
        }
        if (wheel < 0)
        {
            check(false, "the wheel is there");
            return;
        }
        g.player.origin[2] += fxFromInt(4000);
        // (A frame: the logic, then the USE of whoever holds it.)
        auto frames = [&](int n, bool holding)
        {
            for (int k = 0; k < n; ++k)
            {
                g.logic.useHeld.value = holding;
                logicTick(g.logic, g.player, 33);
                if (holding)
                    logicUse(g.logic, g.player, wheel, -1, kUseSet);
                g.drain();
            }
        };
        const int &phase = g.logic.states[wheel].phase;
        frames(30 * 4, true);
        const int held = phase;
        frames(30, false);
        const int let = phase;
        frames(30 * 2, true);
        const int again = phase;
        frames(30, false);
        std::printf("test_uplink: held 4 s, %d%% round; let go, %d%%; held 2 s more, %d%%\n", held * 100 / 65536,
                    let * 100 / 65536, again * 100 / 65536);
        check(held > 65536 / 3 && held < 65536 / 2, "held, it turns at its speed (35 a second, of 360)");
        check(let - held < 65536 / 50, "let go of, it stops within a tenth of a second, and stays");
        check(again < let && again > 0, "taken hold of again, it turns back");
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || dir[0] == '\0')
    {
        std::printf("test_uplink: HLDS_VALVE_DIR not set, the map was not played\n");
        return 77;
    }
    testDish(dir);
    testSewerLid(dir);
    testCageReturns(dir);
    testHeld(dir);
    if (g_failures == 0)
        std::printf("test_uplink: ok\n");
    return g_failures == 0 ? 0 : 1;
}
