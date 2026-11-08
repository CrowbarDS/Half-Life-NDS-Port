// Ladders that are not boxes: one turned (a diagonal one), one leaning, and
// one bent from leaning to upright in three pieces, as down c1a4e's silo by
// the fan. The game (PM_Ladder, PM_LadderMove) has the player on a ladder
// when their origin is inside its brush's hull for their size, takes which
// way it faces from the face a line to its middle meets, and turns what would
// carry the player into it into speed UP it -- along that face, which may
// lean: lateral - into * (n x normalize(up x n)). A box ladder is as before.
//
// With HLDS_VALVE_DIR, c1a4e's own: climbed from below, never off it in the
// air, and on up the upright one above it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    using Cut = std::array<float, 4>;

    double units(fx v) { return v / 4096.0; }

    /// A ladder brush: its box, and the planes that make a slab of it.
    BrushBox ladder(float x0, float y0, float z0, float x1, float y1, float z1, std::vector<Cut> cuts = {})
    {
        BrushBox b;
        b.className = "func_ladder";
        b.mins[0] = x0, b.mins[1] = y0, b.mins[2] = z0;
        b.maxs[0] = x1, b.maxs[1] = y1, b.maxs[2] = z1;
        b.cuts = std::move(cuts);
        return b;
    }

    /// A slab 8 thick whose front face is {n, d} (n towards the player).
    std::vector<Cut> slab(float nx, float ny, float nz, float d)
    {
        return {Cut{nx, ny, nz, d}, Cut{-nx, -ny, -nz, -(d - 8)}};
    }

    std::unique_ptr<Game> cook(const BoxRoom &room)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.parse(buildBoxRoom(room), error))
            return nullptr;
        finish(*game, nullptr);
        return game->ok ? std::move(game) : nullptr;
    }

    BoxRoom bigRoom()
    {
        BoxRoom room;
        room.mins[0] = room.mins[1] = -256;
        room.maxs[0] = room.maxs[1] = 256;
        room.mins[2] = 0;
        room.maxs[2] = 512;
        return room;
    }

    /// Facing along (x, y), level.
    MoveCommand facing(double x, double y)
    {
        MoveCommand cmd;
        cmd.forward = 1;
        const double len = std::hypot(x, y);
        cmd.forwardXY[0] = static_cast<fx>(x / len * kFxOne);
        cmd.forwardXY[1] = static_cast<fx>(y / len * kFxOne);
        cmd.rightXY[0] = cmd.forwardXY[1];
        cmd.rightXY[1] = -cmd.forwardXY[0];
        cmd.viewForward[0] = cmd.forwardXY[0];
        cmd.viewForward[1] = cmd.forwardXY[1];
        cmd.viewForward[2] = 0;
        cmd.viewRight[0] = cmd.rightXY[0];
        cmd.viewRight[1] = cmd.rightXY[1];
        cmd.viewRight[2] = 0;
        return cmd;
    }

    void place(Game &g, double x, double y, double z)
    {
        g.player.origin[0] = static_cast<fx>(x * 4096);
        g.player.origin[1] = static_cast<fx>(y * 4096);
        g.player.origin[2] = static_cast<fx>(z * 4096);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        g.player.onGround = false;
        g.player.onLadder = false;
    }

    /// How far the point is in front of the face {n, d}.
    double out(const Game &g, const Cut &face)
    {
        return face[0] * units(g.player.origin[0]) + face[1] * units(g.player.origin[1]) +
               face[2] * units(g.player.origin[2]) - face[3];
    }

    /// The game's climbing velocity for the wish `w` at the face normal `n`.
    std::array<double, 3> gameClimb(const std::array<double, 3> &w, const Cut &n)
    {
        const double into = w[0] * n[0] + w[1] * n[1] + w[2] * n[2];
        const double c = std::hypot(n[0], n[1]);
        const std::array<double, 3> up = {-n[2] * n[0] / c, -n[2] * n[1] / c, c};
        return {w[0] - into * n[0] - into * up[0], w[1] - into * n[1] - into * up[1],
                w[2] - into * n[2] - into * up[2]};
    }

    struct Climb
    {
        int onTicks = 0, ticks = 0;
        double worstSide = 0;   ///< the most it moved sideways in a tick
        double nearest = 1e9, furthest = -1e9; ///< from the face, while on it
        double rose = 0;
        std::array<double, 3> velocity = {0, 0, 0}; ///< the last tick's, on it
    };

    /// Forward for `ticks` ticks; `face` the face to measure from (none: no
    /// measuring).
    Climb climb(Game &g, const MoveCommand &cmd, int ticks, const Cut *face)
    {
        Climb c;
        const double fromZ = units(g.player.origin[2]);
        for (int n = 0; n < ticks; ++n)
        {
            const double was[2] = {units(g.player.origin[0]), units(g.player.origin[1])};
            playerMove(g.player, cmd, g.move, kPlayerTick);
            ++c.ticks;
            if (!g.player.onLadder)
                continue;
            ++c.onTicks;
            c.worstSide = std::max(c.worstSide, std::hypot(units(g.player.origin[0]) - was[0],
                                                           units(g.player.origin[1]) - was[1]));
            if (face != nullptr)
            {
                c.nearest = std::min(c.nearest, out(g, *face));
                c.furthest = std::max(c.furthest, out(g, *face));
            }
            c.velocity = {units(g.player.velocity[0]), units(g.player.velocity[1]), units(g.player.velocity[2])};
        }
        c.rose = units(g.player.origin[2]) - fromZ;
        return c;
    }

    /// A box ladder climbs as it always has: straight up at 200.
    void testBox()
    {
        BoxRoom room = bigRoom();
        room.brushes.push_back(ladder(100, -16, 0, 108, 16, 300));
        auto game = cook(room);
        check(game != nullptr, "the room with a box ladder cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        check(g.ladderModels[0] == 0, "a box ladder keeps no model: its bounds say it all");
        place(g, 90, 0, 80);
        const Climb c = climb(g, facing(1, 0), 15, nullptr);
        std::printf("test_ladders: a box ladder: on it %d of %d ticks, velocity %.1f %.1f %.1f\n", c.onTicks, c.ticks,
                    c.velocity[0], c.velocity[1], c.velocity[2]);
        check(c.onTicks == c.ticks && std::fabs(c.velocity[0]) < 0.01 && std::fabs(c.velocity[1]) < 0.01 &&
                  std::fabs(c.velocity[2] - 200) < 0.01,
              "... is climbed straight up at 200, as before");
    }

    /// Turned 45 degrees: faced square on, climbed straight up, never off its
    /// face, and not flung sideways.
    void testDiagonal()
    {
        const float k = 0.70710678f;
        BoxRoom room = bigRoom();
        std::vector<Cut> cuts = slab(k, k, 0, 4);
        cuts.push_back(Cut{k, -k, 0, 16});
        cuts.push_back(Cut{-k, k, 0, 16});
        room.brushes.push_back(ladder(-24, -24, 0, 24, 24, 300, cuts));
        auto game = cook(room);
        check(game != nullptr, "the room with a diagonal ladder cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        check(g.ladderModels[0] != 0, "a turned ladder keeps its model");
        const Cut face{k, k, 0, 4};
        // In front of it by 20, square on; then 40 out, still in its bounds.
        place(g, 24 * k, 24 * k, 60);
        const Climb c = climb(g, facing(-1, -1), 20, &face);
        std::printf("test_ladders: a diagonal ladder: on it %d of %d ticks, rose %.1f, %.1f..%.1f from its face, "
                    "sideways at worst %.2f a tick\n",
                    c.onTicks, c.ticks, c.rose, c.nearest, c.furthest, c.worstSide);
        check(c.onTicks == c.ticks && c.rose > 120, "a diagonal ladder is climbed");
        check(c.furthest - c.nearest < 2 && c.worstSide < 0.5, "... straight up its face, never sideways");
        place(g, 34 * k, 34 * k, 60);
        const Climb far = climb(g, facing(-1, -1), 1, nullptr);
        check(far.onTicks == 0, "... and not from 30 in front of it, in its bounds but in the air");
    }

    /// Leaning 15 degrees back: climbed along its face, at the game's
    /// velocity, the same way from it all the way up.
    void testLeaning()
    {
        const float c15 = 0.96592583f, s15 = 0.25881905f;
        BoxRoom room = bigRoom();
        room.brushes.push_back(ladder(-16, -60, 0, 16, 24, 200, slab(0, c15, s15, 19.3f)));
        auto game = cook(room);
        check(game != nullptr, "the room with a leaning ladder cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        const Cut face{0, c15, s15, 19.3f};
        // 10 in front of its face, at 60 up.
        place(g, 0, (19.3 + 10 - s15 * 60) / c15, 60);
        const Climb c = climb(g, facing(0, -1), 20, &face);
        const std::array<double, 3> want = gameClimb({0, -200, 0}, face);
        std::printf("test_ladders: a leaning ladder: on it %d of %d ticks, rose %.1f, %.1f..%.1f from its face, "
                    "velocity %.1f %.1f %.1f (the game's %.1f %.1f %.1f)\n",
                    c.onTicks, c.ticks, c.rose, c.nearest, c.furthest, c.velocity[0], c.velocity[1], c.velocity[2],
                    want[0], want[1], want[2]);
        check(c.onTicks == c.ticks && c.rose > 100, "a leaning ladder is climbed");
        check(c.furthest - c.nearest < 2, "... along its face: as far from it at the top as at the foot");
        const double err = std::hypot(c.velocity[1] - gameClimb({0, -200, 0}, face)[1],
                                      c.velocity[2] - gameClimb({0, -200, 0}, face)[2]);
        std::printf("test_ladders: ... against the game's for a wish of 200: off by %.2f\n", err);
        check(err < 2.5, "... at the game's velocity (within 1%)");
        place(g, 0, (19.3 + 30 - s15 * 100) / c15, 100);
        const Climb far = climb(g, facing(0, -1), 1, nullptr);
        check(far.onTicks == 0, "... and not from 30 in front of it");
    }

    /// Bent: leaning 15, then 7.5, then upright (as c1a4e's leans 15 and
    /// goes on upright), each a ladder of its own and each face going on
    /// from the last. Climbed from the foot to the top, on a ladder all the
    /// way, never lurching sideways.
    void testBent()
    {
        const float c30 = 0.96592583f, s30 = 0.25881905f, c15 = 0.99144486f, s15 = 0.13052619f;
        const float d1 = c30 * 20;                 // through (y 20, z 0)
        const float y1 = (d1 - s30 * 100) / c30;   // where it is at 100
        const float d2 = c15 * y1 + s15 * 100;     // through (y1, 100)
        const float y2 = (d2 - s15 * 200) / c15;   // where it is at 200
        BoxRoom room = bigRoom();
        room.brushes.push_back(ladder(-16, y1 - 12, 0, 16, 21, 100, slab(0, c30, s30, d1)));
        room.brushes.push_back(ladder(-16, y2 - 12, 100, 16, y1 + 1, 200, slab(0, c15, s15, d2)));
        room.brushes.push_back(ladder(-16, std::floor(y2 - 8), 200, 16, std::floor(y2), 320));
        auto game = cook(room);
        check(game != nullptr, "the room with a bent ladder cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        place(g, 0, (d1 + 10 - s30 * 40) / c30, 40);
        const Climb c = climb(g, facing(0, -1), 45, nullptr);
        std::printf("test_ladders: a bent ladder: on one %d of %d ticks, rose %.1f to %.1f, sideways at worst %.2f "
                    "a tick\n",
                    c.onTicks, c.ticks, c.rose, units(g.player.origin[2]), c.worstSide);
        check(c.onTicks >= c.ticks - 2, "a bent ladder is held all the way up (let go of over its top)");
        check(units(g.player.origin[2]) > 320, "... from leaning to upright, and over its top");
        check(c.worstSide < 4, "... and never flung sideways where it bends");
    }

    /// c1a4e's: leaning up the pipe below the fan's silo, then upright.
    void testSilo(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c1a4e.bsp", error))
        {
            std::printf("test_ladders: no c1a4e (%s)\n", error.c_str());
            return;
        }
        finish(*game, nullptr, dir);
        if (!game->ok)
        {
            check(false, "c1a4e cooks");
            return;
        }
        Game &g = *game;
        int shaped = 0;
        for (int i = 0; i < g.move.ladderCount; ++i)
            shaped += g.ladderModels[i] != 0 ? 1 : 0;
        const Cut face{0, 0.965f, 0.263f, -513.1f};
        place(g, 1504, 36, -2000);
        const Climb c = climb(g, facing(0, -1), 150, &face);
        std::printf("test_ladders: c1a4e: %d of %d ladders not boxes; climbed to z %.0f, on one %d of %d ticks, "
                    "sideways at worst %.2f a tick\n",
                    shaped, g.move.ladderCount, units(g.player.origin[2]), c.onTicks, c.ticks, c.worstSide);
        check(shaped == 1, "c1a4e's leaning ladder is the one not a box");
        check(units(g.player.origin[2]) > -1700, "... it is climbed, and the upright one above it");
        check(c.worstSide < 8, "... with no lurch where they meet");
    }
} // namespace

int main()
{
    testBox();
    testDiagonal();
    testLeaning();
    testBent();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
        testSilo(dir);
    else
        std::printf("test_ladders: HLDS_VALVE_DIR not set, c1a4e was not climbed\n");
    if (g_failures == 0)
        std::printf("test_ladders: ok\n");
    return g_failures == 0 ? 0 : 1;
}
