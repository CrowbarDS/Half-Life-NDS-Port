// Collision against brush entities: shared/HlMovers.hpp and the player
// movement that runs on top of it.
//
// The world here is the box room with boxes in it -- a slab across the middle,
// a low step, a shelf overhead -- each a brush entity with clip hulls of its
// own, cooked like any map's. As with the world trace, the fixed-point result
// is compared against the same algorithm in floating point over thousands of
// random segments; then the things a player would actually notice are checked
// by walking into them.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

#include "HlLevelReader.hpp"
#include "HlPlayerMove.hpp"
#include "HlWalk.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/Cooker.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    struct World
    {
        BspFile bsp;
        std::vector<std::uint8_t> bytes;
        LevelView view;
        MoveWorld move;
        std::vector<MoverHull> movers;
        bool ok = false;
    };

    double toUnits(fx v) { return v / 4096.0; }

    void build(World &w, const BoxRoom &room)
    {
        std::string error;
        CookedLevel level;
        CookReport report;
        w.ok = w.bsp.parse(buildBoxRoom(room), error) &&
               cookLevel(w.bsp, CookOptions(), TexturePlan(), level, report, error) &&
               writeLevel(level, w.bytes, error) &&
               w.view.open(w.bytes.data(), static_cast<std::uint32_t>(w.bytes.size())) ==
                   LevelError::Ok;
        if (!w.ok)
        {
            std::fprintf(stderr, "world failed: %s\n", error.c_str());
            return;
        }
        const LevelView &v = w.view;
        w.move.standing = Hull{v.clipNodes, v.planes, v.planeTypes, v.models[0].clipStanding};
        w.move.crouched = Hull{v.clipNodes, v.planes, v.planeTypes, v.models[0].clipCrouched};
        // The same thing the DS does at spawn (Player::spawn, refreshMovers).
        w.movers.resize(v.brushCount);
        for (std::uint32_t i = 0; i < v.brushCount; ++i)
        {
            const Brush &brush = v.brushes[i];
            const Model &model = v.models[brush.model];
            MoverHull &m = w.movers[i];
            m.standing = Hull{v.clipNodes, v.planes, v.planeTypes, model.clipStanding};
            m.crouched = Hull{v.clipNodes, v.planes, v.planeTypes, model.clipCrouched};
            m.solid = (brush.flags & kBrushSolid) != 0;
            for (int a = 0; a < 3; ++a)
            {
                m.origin[a] = brush.origin[a];
                m.mins[a] = fxFromInt(brush.mins[a]);
                m.maxs[a] = fxFromInt(brush.maxs[a]);
            }
        }
        w.move.movers = w.movers.data();
        w.move.moverCount = static_cast<int>(w.movers.size());
    }

    BrushBox slab(float x0, float y0, float z0, float x1, float y1, float z1,
                  const char *cls = "func_wall", const char *keys = "")
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

    // ---- the floating-point reference: world hull, then each brush's -------

    struct Ref
    {
        bool allSolid = true, startSolid = false;
        double fraction = 1.0;
        double end[3];
    };

    int refContents(const BspFile &bsp, int num, const double *p)
    {
        while (num >= 0)
        {
            const BspClipNode &n = bsp.clipNodes[num];
            const BspPlane &pl = bsp.planes[n.plane];
            const double d = pl.normal[0] * p[0] + pl.normal[1] * p[1] + pl.normal[2] * p[2] - pl.dist;
            num = n.children[d < 0 ? 1 : 0];
        }
        return num;
    }

    bool refCheck(const BspFile &bsp, int root, int num, double p1f, double p2f, const double *p1,
                  const double *p2, Ref &tr)
    {
        if (num < 0)
        {
            if (num != kContentsSolid)
                tr.allSolid = false;
            else
                tr.startSolid = true;
            return true;
        }
        const BspClipNode &n = bsp.clipNodes[num];
        const BspPlane &pl = bsp.planes[n.plane];
        const double t1 = pl.normal[0] * p1[0] + pl.normal[1] * p1[1] + pl.normal[2] * p1[2] - pl.dist;
        const double t2 = pl.normal[0] * p2[0] + pl.normal[1] * p2[1] + pl.normal[2] * p2[2] - pl.dist;
        if (t1 >= 0 && t2 >= 0)
            return refCheck(bsp, root, n.children[0], p1f, p2f, p1, p2, tr);
        if (t1 < 0 && t2 < 0)
            return refCheck(bsp, root, n.children[1], p1f, p2f, p1, p2, tr);
        const int side = t1 < 0;
        double frac = side ? (t1 + 1.0 / 32) / (t1 - t2) : (t1 - 1.0 / 32) / (t1 - t2);
        frac = std::min(1.0, std::max(0.0, frac));
        double midf = p1f + (p2f - p1f) * frac;
        double mid[3];
        for (int i = 0; i < 3; ++i)
            mid[i] = p1[i] + frac * (p2[i] - p1[i]);
        if (!refCheck(bsp, root, n.children[side], p1f, midf, p1, mid, tr))
            return false;
        if (refContents(bsp, n.children[side ^ 1], mid) != kContentsSolid)
            return refCheck(bsp, root, n.children[side ^ 1], midf, p2f, mid, p2, tr);
        if (tr.allSolid)
            return false;
        while (refContents(bsp, root, mid) == kContentsSolid)
        {
            frac -= 0.1;
            if (frac < 0)
                break;
            midf = p1f + (p2f - p1f) * frac;
            for (int i = 0; i < 3; ++i)
                mid[i] = p1[i] + frac * (p2[i] - p1[i]);
        }
        tr.fraction = midf;
        for (int i = 0; i < 3; ++i)
            tr.end[i] = mid[i];
        return false;
    }

    Ref refHull(const BspFile &bsp, int root, const double *a, const double *b)
    {
        Ref tr;
        for (int i = 0; i < 3; ++i)
            tr.end[i] = b[i];
        if (root < 0)
        {
            // A hull that is one contents value everywhere.
            tr.allSolid = root == kContentsSolid;
        }
        else
        {
            refCheck(bsp, root, root, 0, 1, a, b, tr);
        }
        if (tr.allSolid)
            tr.startSolid = true;
        if (tr.startSolid)
        {
            tr.fraction = 0;
            for (int i = 0; i < 3; ++i)
                tr.end[i] = a[i];
        }
        return tr;
    }

    Ref refAll(const World &w, const double *a, const double *b)
    {
        Ref best = refHull(w.bsp, w.bsp.clipHullRoot(0, 1), a, b);
        for (std::uint32_t i = 0; i < w.view.brushCount; ++i)
        {
            if (!w.movers[i].solid)
                continue;
            const int model = w.view.brushes[i].model;
            double la[3], lb[3], origin[3];
            for (int k = 0; k < 3; ++k)
            {
                origin[k] = toUnits(w.movers[i].origin[k]);
                la[k] = a[k] - origin[k];
                lb[k] = b[k] - origin[k];
            }
            const Ref t = refHull(w.bsp, w.bsp.clipHullRoot(model, 1), la, lb);
            if (t.startSolid || t.fraction >= best.fraction)
                continue;
            best.fraction = t.fraction;
            for (int k = 0; k < 3; ++k)
                best.end[k] = t.end[k] + origin[k];
        }
        return best;
    }

    int compareTraces(const World &w, int count, unsigned seed)
    {
        std::mt19937 rng(seed);
        int differ = 0;
        for (int n = 0; n < count; ++n)
        {
            fx a[3], b[3];
            double da[3], db[3];
            for (int i = 0; i < 3; ++i)
            {
                std::uniform_int_distribution<int> pick(
                    fxFromDouble(w.bsp.models[0].mins[i] - 8),
                    fxFromDouble(w.bsp.models[0].maxs[i] + 8));
                a[i] = pick(rng);
                b[i] = (n & 1) ? a[i] + (pick(rng) % fxFromInt(40)) : pick(rng);
                da[i] = toUnits(a[i]);
                db[i] = toUnits(b[i]);
            }
            const Trace fixed = traceAll(w.move, false, a, b);
            const Ref ref = refAll(w, da, db);
            bool same = fixed.startSolid == ref.startSolid;
            for (int i = 0; i < 3 && same; ++i)
                same = std::fabs(toUnits(fixed.endPos[i]) - ref.end[i]) <= 0.02;
            if (!same)
                ++differ;
        }
        return differ;
    }

    struct Sim
    {
        World *w;
        PlayerState s;
        MoveCommand cmd;
        void run(int ticks)
        {
            for (int i = 0; i < ticks; ++i)
                playerMove(s, cmd, w->move, kPlayerTick);
        }
        double x() const { return toUnits(s.origin[0]); }
        double y() const { return toUnits(s.origin[1]); }
        double z() const { return toUnits(s.origin[2]); }
    };

    Sim spawn(World &w, double x = -80, double y = 0)
    {
        Sim sim{&w, PlayerState(), MoveCommand()};
        sim.s.origin[0] = fxFromDouble(x);
        sim.s.origin[1] = fxFromDouble(y);
        sim.s.origin[2] = fxFromInt(40);
        sim.run(30);
        return sim;
    }

    const double kFloor = 36 + 1.0 / 32;

    void testTraces()
    {
        // A wall across the middle of the room from y = -64 to 64, floor to
        // ceiling, 16 thick, starting at x = 40.
        World w;
        BoxRoom room;
        room.brushes = {slab(40, -64, 0, 56, 64, 128)};
        build(w, room);
        check(w.ok && w.view.brushCount == 1, "a room with one slab");
        if (!w.ok)
            return;

        const fx from[3] = {0, 0, fxFromInt(64)};
        const fx toWall[3] = {fxFromInt(200), 0, fxFromInt(64)};
        Trace tr = traceAll(w.move, false, from, toWall);
        // The slab's face is at 40; the player's box is 16 wide either side of
        // its centre, and a trace stops 1/32 short.
        check(tr.hit && std::fabs(toUnits(tr.endPos[0]) - (40 - 16 - 1.0 / 32)) < 0.002,
              "a move into the slab stops with the player's box against it");
        check(tr.normal[0] == -kNormalOne && tr.normal[1] == 0,
              "and the normal is the slab's face, pointing back at the player");

        const fx beside[3] = {0, fxFromInt(100), fxFromInt(64)};
        const fx besideEnd[3] = {fxFromInt(200), fxFromInt(100), fxFromInt(64)};
        tr = traceAll(w.move, false, beside, besideEnd);
        check(tr.hit && std::fabs(toUnits(tr.endPos[0]) - (112 - 1.0 / 32)) < 0.002,
              "a move past the end of the slab reaches the room's wall");

        // From the other side: the world's wall is nearer than nothing, the
        // slab is nearer than the wall.
        const fx behind[3] = {fxFromInt(100), 0, fxFromInt(64)};
        const fx back[3] = {fxFromInt(-200), 0, fxFromInt(64)};
        tr = traceAll(w.move, false, behind, back);
        check(std::fabs(toUnits(tr.endPos[0]) - (56 + 16 + 1.0 / 32)) < 0.002,
              "from the far side it stops against the slab's other face");

        check(!positionFree(w.move, false, toWall), "a point in the world's solid is not free");
        const fx inSlab[3] = {fxFromInt(48), 0, fxFromInt(64)};
        check(!positionFree(w.move, false, inSlab), "nor is a point inside the slab");
        check(positionFree(w.move, false, from), "the middle of the room is");
        check(hullPositionFree(w.move.standing, inSlab),
              "and the WORLD alone does not know the slab is there");

        // Starting inside the slab: it is ignored, so the player can leave.
        tr = traceAll(w.move, false, inSlab, from);
        check(!tr.startSolid && tr.fraction == kFractionOne,
              "a move that starts inside a brush is not frozen by it");

        // Not solid: walked through.
        w.movers[0].solid = false;
        tr = traceAll(w.move, false, from, toWall);
        check(std::fabs(toUnits(tr.endPos[0]) - (112 - 1.0 / 32)) < 0.002,
              "a brush that is not solid is not there");
        w.movers[0].solid = true;

        // Moved: lift the slab 200 units, as a door would lift, by its origin
        // and its bounds together.
        w.movers[0].origin[2] = fxFromInt(200);
        w.movers[0].mins[2] += fxFromInt(200);
        w.movers[0].maxs[2] += fxFromInt(200);
        tr = traceAll(w.move, false, from, toWall);
        check(std::fabs(toUnits(tr.endPos[0]) - (112 - 1.0 / 32)) < 0.002,
              "a brush that has moved out of the way is out of the way");
        // And half way down again, hit from below.
        w.movers[0].origin[2] = fxFromInt(100);
        w.movers[0].mins[2] = fxFromInt(100);
        w.movers[0].maxs[2] = fxFromInt(228);
        const fx under[3] = {fxFromInt(48), 0, fxFromInt(40)};
        const fx up[3] = {fxFromInt(48), 0, fxFromInt(300)};
        tr = traceAll(w.move, false, under, up);
        check(tr.hit && std::fabs(toUnits(tr.endPos[2]) - (100 - 36 - 1.0 / 32)) < 0.002,
              "a moved brush is hit where it is now, not where it was built");
        check(tr.normal[2] == -kNormalOne, "from below, on its underside");
    }

    void testAgainstReference()
    {
        World w;
        BoxRoom room;
        room.brushes = {slab(40, -64, 0, 56, 64, 128), slab(-60, 40, 0, -20, 80, 16),
                        slab(-100, -100, 60, -40, -40, 70),
                        slab(0, -100, 0, 20, -80, 128, "func_illusionary")};
        build(w, room);
        check(w.ok, "a room with four brushes");
        if (!w.ok)
            return;
        check(!w.movers[3].solid, "the illusionary one is not solid");
        const int differ = compareTraces(w, 6000, 11);
        check(differ == 0, "6000 random traces through world and brushes match the float reference");

        // The same with every brush displaced: origins are what the DS moves.
        for (std::size_t i = 0; i < w.movers.size(); ++i)
        {
            const fx shift[3] = {fxFromDouble(3.25 * (i + 1)), fxFromDouble(-5.5), fxFromInt(2)};
            for (int a = 0; a < 3; ++a)
            {
                w.movers[i].origin[a] += shift[a];
                w.movers[i].mins[a] += shift[a];
                w.movers[i].maxs[a] += shift[a];
            }
        }
        check(compareTraces(w, 6000, 12) == 0, "and still match with every brush moved");
    }

    void testWalking()
    {
        World w;
        BoxRoom room;
        room.brushes = {
            slab(40, -64, 0, 56, 64, 128),       // 0: a wall across the middle
            slab(-60, 80, 0, -20, 112, 16),      // 1: a step 16 high
            slab(-60, -112, 0, -20, -80, 40),    // 2: a block too high to step onto
            slab(-112, -30, 60, -90, 30, 70),    // 3: a shelf 60 above the floor
        };
        build(w, room);
        check(w.ok, "the walking room");
        if (!w.ok)
            return;

        // Into the wall.
        Sim sim = spawn(w, -10, 0);
        sim.cmd.forward = 1;
        sim.run(60);
        check(sim.x() > 23.9 && sim.x() < 24.0, "walking into a brush wall stops against it");
        check(sim.s.onGround && !sim.s.stuck, "standing, not stuck");

        // Round its end: aim diagonally and slide.
        sim = spawn(w, -10, 0);
        sim.cmd.forward = 1;
        sim.cmd.side = -1; // forward is +x, right is -y: this is towards +y
        sim.run(150);
        check(sim.x() > 100, "sliding along it leads round its end");

        // Up the step: 16 units is within the 18 the player steps over.
        sim = spawn(w, -40, 40);
        sim.cmd.forwardXY[0] = 0;
        sim.cmd.forwardXY[1] = kFxOne; // facing +y
        sim.cmd.rightXY[0] = kFxOne;
        sim.cmd.rightXY[1] = 0;
        sim.cmd.forward = 1;
        double top = 0;
        for (int i = 0; i < 20; ++i)
        {
            sim.run(1);
            top = std::max(top, sim.z());
        }
        check(top > kFloor + 15.9, "a 16-unit brush step is climbed");
        sim.cmd.forward = 0;
        sim.s.origin[1] = fxFromInt(96);
        sim.s.origin[2] = fxFromInt(60);
        sim.s.velocity[0] = sim.s.velocity[1] = sim.s.velocity[2] = 0;
        sim.run(30);
        check(sim.s.onGround && std::fabs(sim.z() - (kFloor + 16)) < 0.01,
              "and stood on: a brush is ground");

        // The 40-unit block is not.
        sim = spawn(w, -40, -40);
        sim.cmd.forwardXY[0] = 0;
        sim.cmd.forwardXY[1] = -kFxOne; // facing -y
        sim.cmd.rightXY[0] = -kFxOne;
        sim.cmd.rightXY[1] = 0;
        sim.cmd.forward = 1;
        sim.run(40);
        check(sim.y() > -64.1 && sim.y() < -63.9 && std::fabs(sim.z() - kFloor) < 0.01,
              "a 40-unit block stops the player at floor level");

        // Under the shelf: there is room to crouch (60 above the floor, the
        // crouched box is 36 tall) and none to stand (72).
        sim = spawn(w, -70, 0);
        sim.cmd.duck = true;
        sim.run(20);
        check(sim.s.ducked, "crouched in the open");
        sim.cmd.forwardXY[0] = -kFxOne; // facing -x, under the shelf
        sim.cmd.forwardXY[1] = 0;
        sim.cmd.rightXY[0] = 0;
        sim.cmd.rightXY[1] = kFxOne;
        sim.cmd.forward = 1;
        sim.run(40);
        check(sim.x() < -95, "crawls under the shelf");
        sim.cmd.forward = 0;
        sim.cmd.duck = false;
        sim.run(30);
        check(sim.s.ducked, "and cannot stand up under it");
        // Walking upright into the shelf is blocked by it.
        sim = spawn(w, -70, 0);
        sim.cmd.forwardXY[0] = -kFxOne;
        sim.cmd.forwardXY[1] = 0;
        sim.cmd.rightXY[0] = 0;
        sim.cmd.rightXY[1] = kFxOne;
        sim.cmd.forward = 1;
        sim.run(40);
        check(sim.x() > -74.1, "standing, the shelf is in the way");

        // A brush closing on the player: the move is not frozen, and the
        // player walks out.
        sim = spawn(w, -10, 0);
        w.movers[0].origin[0] = fxFromInt(-50); // the wall now spans x = -10..6
        w.movers[0].mins[0] -= fxFromInt(50);
        w.movers[0].maxs[0] -= fxFromInt(50);
        sim.cmd.forwardXY[0] = -kFxOne;
        sim.cmd.forwardXY[1] = 0;
        sim.cmd.forward = 1;
        sim.run(20);
        check(!sim.s.stuck && sim.x() < -40, "overlapped by a brush, the player can walk out");
    }

    /// The world's tree, for what asks it (walkBottom): the walking tests.
    void withTree(World &w)
    {
        w.move.nodes = w.view.nodes;
        w.move.leaves = w.view.leaves;
        w.move.headNode = w.view.models[0].headNode;
    }

    /// Steps of `dx` from `feet` until one is refused, or `most` are made.
    int stepUntilStopped(const World &w, fx *feet, fx dx, int most)
    {
        int made = 0;
        while (made < most && walkStep(w.move, nullptr, -1, kWalkStanding, feet, dx, 0))
            ++made;
        return made;
    }

    // SV_CheckBottom: a walker does not stand over an edge, its middle or
    // a corner more than a stair above what is under it; a stair is no edge.
    void testCheckBottom()
    {
        World w;
        BoxRoom room;
        room.brushes = {
            slab(-128, 0, 0, 0, 128, 64),   // 0: a platform 64 high, its edge at x = 0
            slab(40, -128, 0, 128, 0, 16),  // 1: a step 16 high, from x = 40
        };
        room.maxs[2] = 256; // (room to stand up on the platform)
        build(w, room);
        check(w.ok, "the edges room");
        if (!w.ok)
            return;
        withTree(w);

        // Out along the platform to its edge: stopped with every corner on
        // it (its middle 16 in), not hanging over the drop.
        fx feet[3] = {fxFromInt(-60), fxFromInt(64), fxFromInt(64)};
        const int made = stepUntilStopped(w, feet, fxFromInt(4), 40);
        std::printf("test_movers: walked to x = %.2f of a ledge at x = 0\n", toUnits(feet[0]));
        check(made > 0 && toUnits(feet[0]) > -20 && toUnits(feet[0]) <= -16 + 0.01,
              "a walker stops at a ledge with all of its feet on it");
        check(std::fabs(toUnits(feet[2]) - 64) < 0.1, "... still standing on it");

        // Hung over it already (put there): it may move, as the engine's
        // FL_PARTIALGROUND lets it, to get off it; back on, the edge holds
        // it again.
        fx over[3] = {fxFromInt(-8), fxFromInt(64), fxFromInt(64)};
        check(walkStep(w.move, nullptr, -1, kWalkStanding, over, -fxFromInt(4), 0) &&
                  walkStep(w.move, nullptr, -1, kWalkStanding, over, -fxFromInt(4), 0) &&
                  toUnits(over[0]) < -15.9,
              "over the edge, it can step back onto the platform");
        check(!walkStep(w.move, nullptr, -1, kWalkStanding, over, fxFromInt(4), 0),
              "... and then not off it again");

        // On the world's floor it walks, and a 16-unit stair is no edge: up
        // onto the step and along it.
        fx floor[3] = {fxFromInt(-60), fxFromInt(-60), 0};
        stepUntilStopped(w, floor, fxFromInt(4), 40);
        check(toUnits(floor[0]) > 99 && std::fabs(toUnits(floor[2]) - 16) < 0.1,
              "a walker crosses the floor and climbs a 16-unit step");
        // And back down it, straddling it on the way.
        stepUntilStopped(w, floor, -fxFromInt(4), 40);
        check(toUnits(floor[0]) < 0 && std::fabs(toUnits(floor[2])) < 0.1, "... and steps down off it");
    }

    // walkTick's turning: told where to put the way it is going, it leaves
    // the turn to whoever asked (a monster, at its yaw speed); otherwise it
    // turns the walker itself, a turn a second.
    void testWalkerTurn()
    {
        World w;
        build(w, BoxRoom());
        check(w.ok, "the empty room");
        if (!w.ok)
            return;
        withTree(w);
        for (int asked = 1; asked >= 0; --asked)
        {
            ActorState walker;
            walker.active = true;
            walker.pos[0] = fxFromInt(-60);
            walker.pos[1] = fxFromInt(60);
            walker.yaw = 0; // facing +x; it is going to -y
            ActorWorld a;
            a.states = &walker;
            a.count = 1;
            const fx dest[3] = {fxFromInt(-60), fxFromInt(-80), 0};
            std::uint16_t ideal = 12345;
            int worst = 0;
            for (int n = 0; n < 30; ++n)
            {
                const std::uint16_t before = walker.yaw;
                walkTick(a, w.move, 0, kWalkStanding, dest, fxFromInt(100), 33, nullptr, fxFromInt(16),
                         fxFromInt(72), false, asked ? &ideal : nullptr);
                const int turned = static_cast<std::int16_t>(walker.yaw - before);
                worst = std::max(worst, turned < 0 ? -turned : turned);
            }
            check(toUnits(walker.pos[1]) < 30, "the walker walks");
            if (asked)
            {
                check(walker.yaw == 0, "given an ideal yaw, walkTick does not turn the walker");
                check(ideal == 49152, "... and puts the way it is going there");
            }
            else
            {
                check(worst > 0 && worst <= 65536 * 33 / 1000, "otherwise it turns it, a turn a second at most");
                check(static_cast<std::int16_t>(walker.yaw - 49152) == 0, "... to face its way");
            }
        }
    }
} // namespace

int main()
{
    testTraces();
    testAgainstReference();
    testWalking();
    testCheckBottom();
    testWalkerTurn();
    if (g_failures == 0)
        std::printf("test_movers: ok\n");
    return g_failures == 0 ? 0 : 1;
}
