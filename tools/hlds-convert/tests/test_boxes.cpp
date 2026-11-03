// The boxes that are pushed (func_pushable), on rooms made here and on the
// game's own maps (HLDS_VALVE_DIR): a box
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
        {
            check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
            return nullptr;
        }
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

    void pushedAbout(Game &g, const char *map)
    {
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

    void playerAndBox(Game &g, int box)
    {
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

    void pushedAbout(const char *dir, const char *map)
    {
        const std::unique_ptr<Game> game = load(dir, map);
        if (game == nullptr)
            return;
        pushedAbout(*game, map);
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
        playerAndBox(g, box);
    }

    BrushBox crate(float x, float y, float size, float high)
    {
        BrushBox box;
        box.className = "func_pushable";
        box.mins[0] = x, box.mins[1] = y, box.mins[2] = 0;
        box.maxs[0] = x + size, box.maxs[1] = y + size, box.maxs[2] = high;
        box.keys = "\"friction\" \"0\"\n";
        return box;
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

    /// USE held on a box: it is not pushed, and it follows someone walking off.
    void testPull(const BoxRoom &room)
    {
        std::unique_ptr<Game> game = cook(room);
        check(game != nullptr, "the room to pull a crate in cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        const int box = boxesOf(g)[0];
        const int brush = g.view.logic[box].brush;
        const MoverHull &hull = g.movers[brush];
        for (int tick = 0; tick < 30; ++tick)
            logicTick(g.logic, g.player, 16);
        const fx startX = hull.mins[0];
        const fx feet = hull.mins[2];
        g.player.origin[0] = startX - fxFromInt(17);
        g.player.origin[1] = 0;
        g.player.origin[2] = feet + fxFromInt(36);
        g.player.onGround = true;
        g.player.groundMover = -1;

        const fx eye[3] = {g.player.origin[0], 0, g.player.origin[2] + fxFromInt(28)};
        const fx east[3] = {kFxOne, 0, 0}, west[3] = {-kFxOne, 0, 0};
        check(logicFindUsable(g.logic, eye, east) == box, "USE, facing a box, is the box");
        check(logicFindUsable(g.logic, eye, west) == -1, "... and not with its back to it");

        // Walking into it, holding USE: it stays where it is.
        g.logic.useHeld.value = true;
        for (int tick = 0; tick < 30; ++tick)
        {
            g.player.touchedMover = static_cast<std::int16_t>(brush);
            g.player.touchVelocity[0] = g.player.velocity[0] = fxFromInt(100);
            logicTick(g.logic, g.player, 16);
        }
        check(hull.mins[0] == startX, "holding USE, walking into a box doesn't push it");
        g.player.touchedMover = -1;
        g.player.touchVelocity[0] = 0;

        // In the air, or standing on it: no pull.
        g.player.velocity[0] = -fxFromInt(200);
        g.player.onGround = false;
        logicPull(g.logic, g.player, box, 16);
        check(g.logic.states[box].travel[0] == 0, "someone in the air doesn't pull a box");
        g.player.onGround = true;
        g.player.groundMover = static_cast<std::int16_t>(brush);
        logicPull(g.logic, g.player, box, 16);
        check(g.logic.states[box].travel[0] == 0, "nor someone standing on it");
        g.player.groundMover = -1;

        // Walking off: it follows, never faster than they go, and they
        // aren't slowed.
        fx fastest = 0;
        for (int tick = 0; tick < 60; ++tick)
        {
            logicPull(g.logic, g.player, box, 16);
            fastest = std::max(fastest, static_cast<fx>(-g.logic.states[box].travel[0]));
            g.player.origin[0] -= fxFromInt(200) * 16 / 1000;
            logicTick(g.logic, g.player, 16);
        }
        const int went = fxToInt(startX - hull.mins[0]);
        std::printf("test_boxes: pulled for about a second by someone going 200: it came %d, at up to %d\n", went,
                    fxToInt(fastest));
        check(went > 60 && went < 192, "a pulled box follows, and falls behind");
        check(fastest <= fxFromInt(200), "... no faster than the one pulling");
        check(g.player.velocity[0] == -fxFromInt(200), "... who isn't slowed by it");

        // Let go: it pushes again.
        g.logic.useHeld.value = false;
        for (int tick = 0; tick < 120; ++tick)
            logicTick(g.logic, g.player, 16);
        const fx rest = hull.mins[0];
        g.player.origin[0] = rest - fxFromInt(17);
        g.player.touchedMover = static_cast<std::int16_t>(brush);
        g.player.touchVelocity[0] = g.player.velocity[0] = fxFromInt(100);
        logicTick(g.logic, g.player, 16);
        logicTick(g.logic, g.player, 16);
        check(hull.mins[0] > rest, "with USE let go, it is pushed again");
    }

    /// The boxes in water on a map: they come up and float, their tops out of
    /// it, and none of them goes off into the air or through the bottom.
    void testFloating(const char *dir, const char *map)
    {
        std::unique_ptr<Game> game = load(dir, map);
        if (game == nullptr)
            return;
        Game &g = *game;
        g.player.origin[2] += fxFromInt(6000); // (out of the way)
        std::vector<int> wet;
        for (const int b : boxesOf(g))
        {
            fx depth;
            if (hldata::detail::boxInWater(g.logic, g.view.logic[b].brush, &depth) > 0)
                wet.push_back(b);
        }
        std::vector<int> lowest(wet.size(), 1 << 30), highest(wet.size(), -(1 << 30));
        for (int tick = 0; tick < 60 * 20; ++tick)
        {
            logicTick(g.logic, g.player, 17);
            g.drain();
            for (std::size_t k = 0; k < wet.size() && tick >= 60 * 10; ++k)
            {
                const int z = fxToInt(g.movers[g.view.logic[wet[k]].brush].mins[2]);
                lowest[k] = std::min(lowest[k], z);
                highest[k] = std::max(highest[k], z);
            }
        }
        int afloat = 0;
        for (std::size_t k = 0; k < wet.size(); ++k)
        {
            const MoverHull &hull = g.movers[g.view.logic[wet[k]].brush];
            fx depth;
            const int level = hldata::detail::boxInWater(g.logic, g.view.logic[wet[k]].brush, &depth);
            const fx top[3] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2, hull.maxs[2] + kFxOne};
            const bool out = !isLiquid(pointContents(g.move, top));
            afloat += level > 0 && out ? 1 : 0;
            std::printf("test_boxes: %s box %d (buoyancy %d): its foot %d deep, between %d and %d, its top %s\n",
                        map, wet[k], fxToInt(g.view.logic[wet[k]].speed), fxToInt(depth), lowest[k], highest[k],
                        out ? "out" : "under");
            check(highest[k] - lowest[k] < 4, "a box afloat comes to rest");
        }
        std::printf("test_boxes: %s: %zu boxes in water, %d of them afloat\n", map, wet.size(), afloat);
        check(!wet.empty() && afloat > 0, "boxes in water come up and float");
        if (std::string(map) != "c2a3")
            return;
        // c2a3's barrels are under the lid of a cage: they stay down until
        // its lock is shot and the lid opens.
        int caged = 0, freed = 0;
        std::vector<int> cage;
        for (std::size_t k = 0; k < wet.size(); ++k)
        {
            if (g.view.logic[wet[k]].speed > 0 && highest[k] < 700)
                cage.push_back(wet[k]);
        }
        caged = static_cast<int>(cage.size());
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &record = g.view.logic[i];
            if (record.kind == static_cast<std::uint8_t>(LogicKind::DoorRotating) && record.name != 0 &&
                g.view.nameHashes[record.name] == nameHash("cagedoor"))
                logicUse(g.logic, g.player, static_cast<int>(i), -1, kUseToggle);
        }
        for (int tick = 0; tick < 60 * 10; ++tick)
        {
            logicTick(g.logic, g.player, 17);
            g.drain();
        }
        for (const int b : cage)
            freed += fxToInt(g.movers[g.view.logic[b].brush].mins[2]) > 900 ? 1 : 0;
        std::printf("test_boxes: c2a3: %d barrels held in their cage; the lid opened, %d come up\n", caged, freed);
        check(caged == 4, "c2a3's barrels stay under the lid of their cage");
        check(freed == 4, "... and all four come up once it is open, as in the game");
    }

    /// A crate of the Hazard Course, pulled as the game does it: USE looks for
    /// what is in view each frame, from a pace in front of it.
    void testPullOnMap(const char *dir)
    {
        std::unique_ptr<Game> game = load(dir, "t0a0b");
        if (game == nullptr)
            return;
        Game &g = *game;
        int box = -1;
        for (const int b : boxesOf(g))
        {
            if (fxToInt(g.movers[g.view.logic[b].brush].mins[0]) == 1152)
                box = b;
        }
        check(box >= 0, "t0a0b has its crate at 1152");
        if (box < 0)
            return;
        const MoverHull &hull = g.movers[g.view.logic[box].brush];
        for (int tick = 0; tick < 30; ++tick)
            logicTick(g.logic, g.player, 17);
        const fx startX = hull.mins[0];
        g.player.origin[0] = fxFromInt(1112);
        g.player.origin[1] = fxFromInt(-424);
        g.player.origin[2] = fxFromInt(-184);
        g.player.onGround = true;
        g.player.groundMover = -1;
        g.player.viewHeight = fxFromInt(28);
        g.logic.useHeld.value = true;
        const fx east[3] = {kFxOne, 0, 0};
        // (Going back at USE's third of the top speed, and speeding up to it.)
        int frames = 0;
        fx speed = 0;
        for (int tick = 0; tick < 60; ++tick)
        {
            speed = std::min(speed + fxFromInt(45), kMaxSpeed / 3);
            g.player.velocity[0] = -speed;
            g.player.origin[0] -= speed * 17 / 1000;
            logicTick(g.logic, g.player, 17);
            g.drain();
            const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
            if (logicFindUsable(g.logic, eye, east) == box)
            {
                logicPull(g.logic, g.player, box, 17);
                ++frames;
            }
        }
        std::printf("test_boxes: t0a0b's crate, pulled for a second: it came %d, in reach for %d frames of 60\n",
                    fxToInt(startX - hull.mins[0]), frames);
        // (It goes a little slower than they do: in the end it is out of reach, as
        // in the game.)
        check(frames >= 45 && startX - hull.mins[0] > fxFromInt(50), "a crate on the Hazard Course comes when pulled, and stays in reach a while");
    }

    /// The same on rooms made here, and what the maps cannot be asked for: a
    /// crate shoved into a wall stops at it with its own side, however wide.
    void testRooms()
    {
        BoxRoom room;
        room.mins[0] = room.mins[1] = -512;
        room.maxs[0] = room.maxs[1] = 512;
        room.maxs[2] = 256;
        room.brushes.push_back(crate(-300, -32, 64, 64));
        room.brushes.push_back(crate(100, -48, 96, 64)); // (in the first's way)
        room.brushes.push_back(crate(-32, 200, 64, 40)); // (low: moved as someone crouched)
        std::unique_ptr<Game> game = cook(room);
        check(game != nullptr, "the room of crates cooks");
        if (game != nullptr)
        {
            check(boxesOf(*game).size() == 3 && game->view.logic[boxesOf(*game)[0]].extra[2] == fxFromInt(400) &&
                      game->view.logic[boxesOf(*game)[0]].extra[3] == 0 && game->view.logic[boxesOf(*game)[2]].extra[3] != 0,
                  "its crates are boxes that go by their height, as fast as their friction lets them");
            pushedAbout(*game, "a room");
        }

        BoxRoom open;
        open.mins[0] = open.mins[1] = -512;
        open.maxs[0] = open.maxs[1] = 512;
        open.maxs[2] = 256;
        open.brushes.push_back(crate(100, -32, 64, 64));
        game = cook(open);
        check(game != nullptr, "the room of one crate cooks");
        if (game != nullptr)
            playerAndBox(*game, boxesOf(*game)[0]);

        testPull(open);

        for (const float width : {32.0f, 64.0f, 128.0f})
        {
            BoxRoom hall;
            hall.mins[0] = hall.mins[1] = -256;
            hall.maxs[0] = hall.maxs[1] = 256;
            hall.maxs[2] = 256;
            hall.brushes.push_back(crate(-width / 2, -width / 2, width, 64));
            game = cook(hall);
            check(game != nullptr, "the hall cooks");
            if (game == nullptr)
                continue;
            Game &g = *game;
            g.player.origin[2] += fxFromInt(6000);
            const int box = boxesOf(g)[0];
            const MoverHull &hull = g.movers[g.view.logic[box].brush];
            for (int tick = 0; tick < 150; ++tick)
            {
                g.logic.states[box].travel[0] = fxFromInt(200);
                g.logic.states[box].travel[1] = 0;
                logicTick(g.logic, g.player, 33);
                g.drain();
            }
            const int reached = fxToInt(hull.maxs[0]);
            std::printf("test_boxes: a crate %d wide, shoved at the wall at 256: its side gets to %d\n",
                        static_cast<int>(width), reached);
            check(reached > 256 - 4 && reached <= 256 + 1, "a crate shoved into a wall stops at it with its own side");
            check(fxToInt(hull.maxs[0] - hull.mins[0]) == static_cast<int>(width), "... and is as wide as ever");
        }
    }
} // namespace

int main()
{
    testRooms();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || dir[0] == '\0')
    {
        std::printf("test_boxes: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        if (g_failures == 0)
            std::printf("test_boxes: ok\n");
        return g_failures == 0 ? 0 : 1;
    }
    testPullOnMap(dir);
    testFloating(dir, "c2a3");
    testFloating(dir, "c2a4a");
    pushedAbout(dir, "c1a2");
    pushedAbout(dir, "c1a1b");
    pushedAbout(dir, "c1a1c");
    pushedAbout(dir, "c0a0a");
    playerAndBox(dir);
    if (g_failures == 0)
        std::printf("test_boxes: ok\n");
    return g_failures == 0 ? 0 : 1;
}
