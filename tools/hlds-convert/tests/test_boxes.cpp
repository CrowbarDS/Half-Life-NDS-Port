// The boxes that are pushed (func_pushable), on the game's own maps: a box
// collides as wide as it is -- it is never left inside a wall, another box,
// or the player -- and the player who is put inside one is got out of it.
// (It used to move as something a person wide, whatever it was: a crate 64
// across went 16 units into every wall.)

#include <algorithm>
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
    std::unique_ptr<Game> load(const char *dir, const char *map)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
            return nullptr;
        finish(*game, nullptr, dir);
        return game->ok ? std::move(game) : nullptr;
    }

    std::vector<int> boxesOf(const Game &g)
    {
        std::vector<int> out;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Pushable) && g.view.logic[i].brush != kNoBrush)
                out.push_back(static_cast<int>(i));
        }
        return out;
    }

    /// How far into the world's solid a box is, at the worst of its corners
    /// and the middles of its sides (looked for up to 20 units in): 0 if it
    /// is nowhere in it.
    int sunk(const Game &g, int record)
    {
        const MoverHull &hull = g.movers[g.view.logic[record].brush];
        int worst = 0;
        for (int depth = 2; depth <= 20; depth += 2)
        {
            bool in = false;
            for (int ix = 0; ix < 3 && !in; ++ix)
            {
                for (int iy = 0; iy < 3 && !in; ++iy)
                {
                    for (int iz = 0; iz < 2 && !in; ++iz)
                    {
                        if (ix == 1 && iy == 1)
                            continue;
                        const fx inset = fxFromInt(depth);
                        const fx mid[2] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2};
                        // (Not its bottom, nor a stair's height above it:
                        // it stands on the floor, a floor is seldom quite
                        // flat, and a pushed box climbs a step as the
                        // player does.)
                        const fx p[3] = {ix == 0 ? hull.mins[0] + inset : (ix == 1 ? mid[0] : hull.maxs[0] - inset),
                                         iy == 0 ? hull.mins[1] + inset : (iy == 1 ? mid[1] : hull.maxs[1] - inset),
                                         iz == 0 ? hull.mins[2] + fxFromInt(20) : hull.maxs[2] - inset};
                        in = pointContents(g.move, p) == kContentsSolid;
                        if (in && depth >= 4 && std::getenv("HLDS_BOX_WHERE") != nullptr)
                            std::printf("  box %d (%dx%dx%d at %d %d %d): corner %d %d %s is %d in\n", record,
                                        fxToInt(hull.maxs[0] - hull.mins[0]), fxToInt(hull.maxs[1] - hull.mins[1]),
                                        fxToInt(hull.maxs[2] - hull.mins[2]), fxToInt(hull.mins[0]), fxToInt(hull.mins[1]),
                                        fxToInt(hull.mins[2]), ix, iy, iz ? "top" : "low", depth);
                    }
                }
            }
            if (!in)
                break;
            worst = depth;
        }
        return worst;
    }

    /// Whether the map's own collision lets a box be where it is: the
    /// person-wide shape it is moved as, at every eight units across it.
    bool refused(const Game &g, int record)
    {
        const MoverHull &hull = g.movers[g.view.logic[record].brush];
        const bool low = g.view.logic[record].extra[3] != 0;
        const fx wide[2] = {hull.maxs[0] - hull.mins[0] - fxFromInt(32), hull.maxs[1] - hull.mins[1] - fxFromInt(32)};
        for (int i = 0; i <= 8; ++i)
        {
            for (int j = 0; j <= 8; ++j)
            {
                const fx at[3] = {hull.mins[0] + fxFromInt(16) + (wide[0] > 0 ? wide[0] * i / 8 : 0),
                                  hull.mins[1] + fxFromInt(16) + (wide[1] > 0 ? wide[1] * j / 8 : 0),
                                  hull.mins[2] + hldata::detail::kPlayerHalfHeight[low ? 1 : 0]};
                if (!hullPositionFree(low ? g.move.crouched : g.move.standing, at))
                    return true;
            }
        }
        return false;
    }

    /// How far two boxes are into each other: the least of the three ways
    /// they overlap (0 if they do not).
    int overlap(const MoverHull &a, const MoverHull &b)
    {
        fx least = fxFromInt(10000);
        for (int k = 0; k < 3; ++k)
        {
            const fx by = (a.maxs[k] < b.maxs[k] ? a.maxs[k] : b.maxs[k]) - (a.mins[k] > b.mins[k] ? a.mins[k] : b.mins[k]);
            if (by <= 0)
                return 0;
            least = by < least ? by : least;
        }
        return fxToInt(least);
    }

    void pushedAbout(const char *dir, const char *map)
    {
        const std::unique_ptr<Game> game = load(dir, map);
        if (game == nullptr)
            return;
        Game &g = *game;
        const std::vector<int> boxes = boxesOf(g);
        // Out of it: this is about the boxes.
        g.player.origin[2] += fxFromInt(6000);
        for (int n = 0; n < 30; ++n)
        {
            logicTick(g.logic, g.player, 33);
            g.drain();
        }
        // Left alone they come to rest, and stay at rest: a box that goes
        // on being worked out every tick costs the console time for
        // nothing (eighteen of them in this map).
        {
            int restless = 0;
            for (int n = 0; n < 60; ++n)
            {
                logicTick(g.logic, g.player, 33);
                g.drain();
            }
            for (int box : boxes)
            {
                const LogicState &state = g.logic.states[box];
                if (g.brushStates[g.view.logic[box].brush].active &&
                    (state.bits != 1 || state.travel[0] != 0 || state.travel[1] != 0 || state.travel[2] != 0))
                {
                    ++restless;
                    if (std::getenv("HLDS_BOX_WHERE") != nullptr)
                        std::printf("  box %d is not at rest: footing %u, speed %d %d %d\n", box, static_cast<unsigned>(state.bits),
                                    fxToInt(state.travel[0]), fxToInt(state.travel[1]), fxToInt(state.travel[2]));
                }
            }
            std::printf("test_boxes: %-6s left alone, %d of %zu boxes are not at rest on the world\n", map, restless, boxes.size());
        }
        // (What the map itself built into something is not the push's doing.)
        std::vector<int> built;
        std::vector<bool> builtIn;
        for (int box : boxes)
        {
            built.push_back(sunk(g, box));
            builtIn.push_back(refused(g, box));
        }
        int walled = 0;
        std::vector<std::vector<int>> between(boxes.size(), std::vector<int>(boxes.size(), 0));
        for (std::size_t a = 0; a < boxes.size(); ++a)
            for (std::size_t b = 0; b < boxes.size(); ++b)
                between[a][b] = overlap(g.movers[g.view.logic[boxes[a]].brush], g.movers[g.view.logic[boxes[b]].brush]);
        int worstWall = 0, worstBox = 0, moved = 0, widest = 0;
        static const int kWays[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (std::size_t b = 0; b < boxes.size(); ++b)
        {
            const int box = boxes[b];
            const MoverHull &hull = g.movers[g.view.logic[box].brush];
            widest = std::max(widest, fxToInt(hull.maxs[0] - hull.mins[0]));
            for (const int *way : kWays)
            {
                const fx from[2] = {hull.mins[0], hull.mins[1]};
                for (int tick = 0; tick < 120; ++tick)
                {
                    if (!g.brushStates[g.view.logic[box].brush].active)
                        break;
                    // (Someone pushing, steadily.)
                    g.logic.states[box].travel[0] = fxFromInt(120 * way[0]);
                    g.logic.states[box].travel[1] = fxFromInt(120 * way[1]);
                    logicTick(g.logic, g.player, 33);
                    g.drain();
                    for (std::size_t o = 0; o < boxes.size(); ++o)
                    {
                        if (!g.brushStates[g.view.logic[boxes[o]].brush].active)
                            continue;
                        worstWall = std::max(worstWall, sunk(g, boxes[o]) - built[o]);
                        if (!builtIn[o] && refused(g, boxes[o]))
                        {
                            const MoverHull &in = g.movers[g.view.logic[boxes[o]].brush];
                            if (std::getenv("HLDS_BOX_WHERE") != nullptr)
                                std::printf("  box %d (%dx%dx%d) at %d %d %d, while %d is shoved %d %d (tick %d)\n", boxes[o],
                                            fxToInt(in.maxs[0] - in.mins[0]), fxToInt(in.maxs[1] - in.mins[1]),
                                            fxToInt(in.maxs[2] - in.mins[2]), fxToInt(in.mins[0]), fxToInt(in.mins[1]),
                                            fxToInt(in.mins[2]), box, way[0], way[1], tick);
                            ++walled;
                        }
                        if (o != b)
                        {
                            const MoverHull &other = g.movers[g.view.logic[boxes[o]].brush];
                            const int by = overlap(hull, other) - between[b][o];
                            if (by > 2 && std::getenv("HLDS_BOX_WHERE") != nullptr)
                                std::printf("  boxes %d (%d %d %d..%d %d %d) and %d (%d %d %d..%d %d %d): %d\n", box,
                                            fxToInt(hull.mins[0]), fxToInt(hull.mins[1]), fxToInt(hull.mins[2]),
                                            fxToInt(hull.maxs[0]), fxToInt(hull.maxs[1]), fxToInt(hull.maxs[2]), boxes[o],
                                            fxToInt(other.mins[0]), fxToInt(other.mins[1]), fxToInt(other.mins[2]),
                                            fxToInt(other.maxs[0]), fxToInt(other.maxs[1]), fxToInt(other.maxs[2]), by);
                            worstBox = std::max(worstBox, by);
                        }
                    }
                }
                moved += hull.mins[0] != from[0] || hull.mins[1] != from[1] ? 1 : 0;
            }
        }
        std::printf("test_boxes: %-6s %2zu boxes (the widest %d), shoved four ways each: %d of the shoves moved one; "
                    "in what stops a person for %d ticks; at worst %d units into what is SEEN of a wall, %d into another box\n",
                    map, boxes.size(), widest, moved, walled, worstWall, worstBox);
        check(!boxes.empty() && moved > static_cast<int>(boxes.size()), (std::string(map) + ": its boxes can be pushed").c_str());
        // The walls that count are the map's collision: where the player
        // is stopped, so is a box, across all of its width. (What is DRAWN
        // of a wall and what stops things are not the same to the unit in
        // these maps -- a slanted wall's foot stands a few units out of
        // what stops the player too -- so against what is seen, a little
        // is let pass: it used to be sixteen and more.)
        check(walled == 0, (std::string(map) + ": a pushed box is never left where the map's collision would stop it").c_str());
        check(worstWall <= 6, (std::string(map) + ": nor more than a few units into what is seen of a wall").c_str());
        check(worstBox <= 2, (std::string(map) + ": nor inside another box").c_str());
    }

    void playerAndBox(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a2");
        if (game == nullptr)
            return;
        Game &g = *game;
        // The crate that is 64 each way and stands on an open floor.
        int box = -1;
        for (int candidate : boxesOf(g))
        {
            const MoverHull &hull = g.movers[g.view.logic[candidate].brush];
            if (fxToInt(hull.mins[0]) == 256 && fxToInt(hull.mins[1]) == -833)
                box = candidate;
        }
        check(box >= 0, "c1a2 has its crate");
        if (box < 0)
            return;
        const int brush = g.view.logic[box].brush;
        const MoverHull &hull = g.movers[brush];
        for (int n = 0; n < 10; ++n)
        {
            logicTick(g.logic, g.player, 33);
            g.drain();
        }
        // Put inside it -- half-way in from one side, on its floor: within
        // a second one of the two is out of the other.
        const fx floorZ = hull.mins[2];
        g.player.origin[0] = hull.mins[0] + fxFromInt(8);
        g.player.origin[1] = (hull.mins[1] + hull.maxs[1]) / 2;
        g.player.origin[2] = floorZ + fxFromInt(37);
        for (fx &c : g.player.velocity)
            c = 0;
        check(hldata::detail::playerInsideBrush(g.logic, g.player, brush), "(the player is put inside the crate)");
        int out = -1;
        for (int n = 0; n < 60 && out < 0; ++n)
        {
            g.frame();
            if (!hldata::detail::playerInsideBrush(g.logic, g.player, brush))
                out = n;
        }
        std::printf("test_boxes: put inside a crate, the player and it are apart %d ticks later\n", out);
        check(out >= 0 && out <= 30, "a player put inside a box is not left in it");
        check(pointContents(g.move, g.player.origin) != kContentsSolid && !refused(g, box),
              "... and neither of them is in a wall for it");

        // Walked at, from each side in turn, for a few seconds each: never
        // inside it, and it never inside a wall.
        int inside = 0, worst = 0;
        static const int kWays[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const int *way : kWays)
        {
            const fx mid[2] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2};
            g.player.origin[0] = mid[0] - fxFromInt(60 * way[0]);
            g.player.origin[1] = mid[1] - fxFromInt(60 * way[1]);
            g.player.origin[2] = hull.mins[2] + fxFromInt(37);
            for (fx &c : g.player.velocity)
                c = 0;
            if (pointContents(g.move, g.player.origin) == kContentsSolid)
                continue; // (no room to stand on that side)
            MoveCommand walk;
            walk.forward = 1;
            walk.forwardXY[0] = kFxOne * way[0];
            walk.forwardXY[1] = kFxOne * way[1];
            walk.rightXY[0] = kFxOne * way[1];
            walk.rightXY[1] = -kFxOne * way[0];
            for (int n = 0; n < 120; ++n)
            {
                logicTick(g.logic, g.player, 33);
                g.logic.viewTurn = 0;
                playerMove(g.player, walk, g.move, kPlayerTick);
                g.drain();
                inside += hldata::detail::playerInsideBrush(g.logic, g.player, brush) ? 1 : 0;
                worst = std::max(worst, refused(g, box) ? 99 : sunk(g, box));
            }
        }
        std::printf("test_boxes: walked at from every side: inside it for %d ticks, the crate at worst %d units into a wall\n",
                    inside, worst);
        check(inside == 0, "a player walking into a box is never inside it");
        check(worst <= 2, "... and does not walk it into a wall");
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || dir[0] == '\0')
    {
        std::printf("test_boxes: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 0;
    }
    pushedAbout(dir, "c1a2");
    pushedAbout(dir, "c1a1b");
    pushedAbout(dir, "c1a1c");
    pushedAbout(dir, "c0a0a");
    playerAndBox(dir);
    if (g_failures == 0)
        std::printf("test_boxes: ok\n");
    return g_failures == 0 ? 0 : 1;
}
