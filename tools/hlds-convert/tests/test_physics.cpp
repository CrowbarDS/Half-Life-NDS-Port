// Collision and player movement: the code in shared/ that the DS runs, driven
// on the host.
//
// Two kinds of check. The hull trace is compared against a floating-point
// version of the same algorithm on thousands of random segments -- the fixed
// point is the thing under test, and "it agrees with floats to a fiftieth of a
// unit" is the claim. The movement is checked against what Half-Life's own
// numbers say should happen: top speed 320, a 45-unit jump, an 18-unit crouch.
//
// The box room has only axis-aligned walls, so set HLDS_VALVE_DIR to a game
// folder to run the same comparisons on a real map with sloped planes.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "HlLevelReader.hpp"
#include "HlPlayerMove.hpp"
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
        bool ok = false;
    };

    void finish(World &w)
    {
        std::string error;
        CookedLevel level;
        CookReport report;
        w.ok = cookLevel(w.bsp, CookOptions(), {}, level, report, error) &&
               writeLevel(level, w.bytes, error) &&
               w.view.open(w.bytes.data(), static_cast<std::uint32_t>(w.bytes.size())) ==
                   LevelError::Ok;
        if (!w.ok)
        {
            std::fprintf(stderr, "world failed: %s\n", error.c_str());
            return;
        }
        w.move.standing = Hull{w.view.clipNodes, w.view.planes, w.view.planeTypes,
                               w.view.models[0].clipStanding};
        w.move.crouched = Hull{w.view.clipNodes, w.view.planes, w.view.planeTypes,
                               w.view.models[0].clipCrouched};
    }

    void boxWorld(World &w, const BoxRoom &room = BoxRoom())
    {
        std::string error;
        if (w.bsp.parse(buildBoxRoom(room), error))
            finish(w);
    }

    double toUnits(fx v) { return v / 4096.0; }

    // ---- the floating-point reference -------------------------------------

    struct RefTrace
    {
        bool allSolid = true, startSolid = false, hit = false;
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
                  const double *p2, RefTrace &tr)
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
        tr.hit = true;
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

    RefTrace refTrace(const BspFile &bsp, int root, const double *a, const double *b)
    {
        RefTrace tr;
        for (int i = 0; i < 3; ++i)
            tr.end[i] = b[i];
        refCheck(bsp, root, root, 0, 1, a, b, tr);
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

    /// Random segments through hull `bspHull`, fixed point against floats.
    /// Returns how many disagreed by more than `tolerance` units.
    int compareTraces(const World &w, const Hull &hull, int bspHull, int count, double tolerance,
                      const float *mins, const float *maxs, unsigned seed)
    {
        std::mt19937 rng(seed);
        const int root = w.bsp.clipHullRoot(0, bspHull);
        int disagreements = 0;
        for (int n = 0; n < count; ++n)
        {
            fx a[3], b[3];
            double da[3], db[3];
            for (int i = 0; i < 3; ++i)
            {
                // A little outside the map too, so some start in solid.
                std::uniform_int_distribution<int> pick(fxFromDouble(mins[i] - 24),
                                                        fxFromDouble(maxs[i] + 24));
                a[i] = pick(rng);
                // Half the segments are short, like a frame of movement.
                b[i] = (n & 1) ? a[i] + (pick(rng) % fxFromInt(40)) : pick(rng);
                da[i] = toUnits(a[i]);
                db[i] = toUnits(b[i]);
            }
            const Trace fixed = traceHull(hull, a, b);
            const RefTrace ref = refTrace(w.bsp, root, da, db);
            bool same = fixed.startSolid == ref.startSolid && fixed.hit == ref.hit;
            for (int i = 0; i < 3 && same; ++i)
                same = std::fabs(toUnits(fixed.endPos[i]) - ref.end[i]) <= tolerance;
            if (!same)
                ++disagreements;
            // Whatever else, a trace that did not start in solid must not END
            // in solid: that is the property movement depends on.
            if (!fixed.startSolid && !hullPositionFree(hull, fixed.endPos))
                check(false, "a trace ended inside solid");
        }
        return disagreements;
    }

    void testTraces()
    {
        World w;
        boxWorld(w);
        check(w.ok, "box world");
        if (!w.ok)
            return;
        const Hull &hull = w.move.standing;

        // The standing hull is the room moved in by (16, 16, 36):
        // x, y in [-112, 112], z in [36, 92].
        const fx centre[3] = {0, 0, fxFromInt(64)};
        check(hullPointContents(hull, hull.root, centre) == kContentsEmpty, "centre is empty");
        const fx outside[3] = {fxFromInt(113), 0, fxFromInt(64)};
        check(hullPointContents(hull, hull.root, outside) == kContentsSolid, "past the wall is solid");

        const fx below[3] = {0, 0, fxFromInt(-100)};
        Trace tr = traceHull(hull, centre, below);
        check(tr.hit && !tr.startSolid && !tr.allSolid, "a drop hits the floor");
        check(std::fabs(toUnits(tr.endPos[2]) - (36 + 1.0 / 32)) < 0.002,
              "and stops 1/32 above the hull's floor");
        check(tr.normal[0] == 0 && tr.normal[1] == 0 && tr.normal[2] == kNormalOne,
              "the floor's normal points up");
        check(std::fabs(tr.fraction / double(kFractionOne) - (28 - 1.0 / 32) / 164) < 1e-6,
              "fraction is the share of the move made");

        const fx up[3] = {0, 0, fxFromInt(500)};
        tr = traceHull(hull, centre, up);
        check(tr.hit && tr.normal[2] == -kNormalOne, "the ceiling's normal points down");
        check(std::fabs(toUnits(tr.endPos[2]) - (92 - 1.0 / 32)) < 0.002, "stops under the ceiling");

        const fx near[3] = {fxFromInt(50), fxFromInt(-30), fxFromInt(70)};
        tr = traceHull(hull, centre, near);
        check(!tr.hit && tr.fraction == kFractionOne && tr.endPos[0] == near[0], "a free move");
        check(tr.normal[2] == 0, "and no plane");

        tr = traceHull(hull, outside, centre);
        check(tr.startSolid && tr.fraction == 0 && tr.endPos[0] == outside[0],
              "starting in solid goes nowhere");

        // A long diagonal: the point must still land within 1/32 of the wall.
        const fx far[3] = {fxFromInt(4000), fxFromInt(3000), fxFromInt(64)};
        tr = traceHull(hull, centre, far);
        check(tr.hit && toUnits(tr.endPos[0]) < 112 && toUnits(tr.endPos[0]) > 112 - 0.04,
              "a 5000-unit trace stops within 1/32 of the wall");

        check(compareTraces(w, hull, 1, 4000, 0.02, w.bsp.models[0].mins, w.bsp.models[0].maxs,
                            1) == 0,
              "standing hull: 4000 random traces agree with the float reference");
        check(compareTraces(w, w.move.crouched, 3, 4000, 0.02, w.bsp.models[0].mins,
                            w.bsp.models[0].maxs, 2) == 0,
              "crouched hull likewise");
    }

    // ---- movement ----------------------------------------------------------

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
        double speed2d() const
        {
            return std::hypot(toUnits(s.velocity[0]), toUnits(s.velocity[1]));
        }
    };

    Sim spawn(World &w)
    {
        Sim sim{&w, PlayerState(), MoveCommand()};
        sim.s.origin[2] = fxFromInt(40);
        return sim;
    }

    const double kFloor = 36 + 1.0 / 32;

    void testMovement()
    {
        World w;
        boxWorld(w);
        if (!w.ok)
            return;

        Sim sim = spawn(w);
        sim.run(30);
        check(sim.s.onGround && std::fabs(sim.z() - kFloor) < 0.01,
              "the player falls to the floor and stands on it");
        check(sim.s.velocity[2] == 0, "with no vertical speed left");
        check(sim.s.viewHeight == kViewHeight, "eyes 28 above the origin");

        // Run at the +x wall.
        sim.cmd.forward = 1;
        double top = 0;
        for (int i = 0; i < 6; ++i)
        {
            sim.run(1);
            top = std::max(top, sim.speed2d());
        }
        check(top > 250 && top < 320.5, "reaches running speed within a fifth of a second");
        for (int i = 0; i < 60; ++i)
        {
            sim.run(1);
            top = std::max(top, sim.speed2d());
            check(hullPositionFree(w.move.standing, sim.s.origin), "never inside the wall");
        }
        check(top < 320.5, "and never exceeds 320 units a second");
        check(sim.x() > 111.9 && sim.x() < 112.0, "stops against the wall");
        check(sim.speed2d() < 1.0, "with no speed left into it");

        // Both keys: still 320, not 320 * sqrt(2).
        sim = spawn(w);
        sim.s.origin[0] = fxFromInt(-100);
        sim.s.origin[1] = fxFromInt(100);
        sim.run(30);
        sim.cmd.forward = 1;
        sim.cmd.side = 1; // forward is +x, right is -y
        top = 0;
        for (int i = 0; i < 12; ++i)
        {
            sim.run(1);
            top = std::max(top, sim.speed2d());
        }
        check(top > 300 && top < 320.5, "diagonal movement is capped at 320 too");
        check(sim.x() > -100 && sim.y() < 100, "forward is +x and right is -y at yaw 0");

        // Into the +x wall at an angle: slide along it into the corner.
        sim.run(90);
        check(sim.x() > 111.9 && sim.y() < -111.9, "slides along the wall into the corner");

        // Stop: friction brings the player to rest.
        sim = spawn(w);
        sim.run(30);
        sim.cmd.forward = 1;
        sim.run(5);
        sim.cmd.forward = 0;
        sim.run(30);
        check(sim.speed2d() == 0, "friction stops the player dead");

        // On a func_friction's puddle (its modifier 15: the ground holds them
        // 15 percent of the world's): the same stop takes far longer.
        double slid[2] = {0, 0};
        for (int slippery = 0; slippery < 2; ++slippery)
        {
            sim = spawn(w);
            sim.s.origin[0] = fxFromInt(-100);
            sim.run(30);
            sim.cmd.forward = 1;
            sim.run(5);
            sim.cmd.forward = 0;
            sim.s.slip.value = slippery ? 85 : 0;
            const double from = sim.x();
            sim.run(15);
            slid[slippery] = sim.x() - from;
        }
        std::printf("test_physics: half a second's stop slides %.1f units, %.1f on a puddle\n", slid[0], slid[1]);
        check(slid[1] > slid[0] * 2, "the ground that holds them less lets them slide on");

        // Where it holds them not at all (modifier 0, c1a1c's lift shaft)
        // the keys cannot get them going either: what they add is scaled
        // by the same friction.
        sim = spawn(w);
        sim.run(30);
        sim.s.slip.value = 100;
        sim.cmd.forward = 1;
        sim.run(15);
        check(sim.speed2d() == 0, "friction 0: the keys add nothing");
    }

    /// The walk: three tenths of what the keys ask for, whichever way they
    /// ask (and so more across a corner, as the game's), and less crouched.
    void testWalk()
    {
        World w;
        boxWorld(w);
        if (!w.ok)
            return;
        // (Each from a stand, and looked at before the room's wall is reached.)
        auto speedAfter = [&](int side, bool walk, bool duck)
        {
            Sim sim = spawn(w);
            sim.cmd.duck = duck;
            sim.run(30);
            sim.cmd.forward = 1;
            sim.cmd.side = side;
            sim.cmd.walk = walk;
            sim.run(8);
            return sim.speed2d();
        };
        check(std::fabs(speedAfter(0, true, false) - 120) < 0.5, "a walk is 120 units a second");
        check(std::fabs(speedAfter(1, true, false) - 169.7) < 1, "... about 170 across a corner, as the game's");
        // (Asking for 40, the ground's friction takes as much as the keys add:
        // the game's sums, whose answer depends on the frame's length.)
        const double crouched = speedAfter(0, true, true);
        check(crouched > 5 && crouched <= 40.5, "... and no more than 40 crouched");
        check(speedAfter(0, false, false) > 250, "not asked for, a run");
    }

    /// The long jump: moving, the crouch just pressed, with the module; and
    /// an ordinary jump for each thing missing.
    void testLongJump()
    {
        World w;
        BoxRoom hall;
        hall.mins[0] = hall.mins[1] = -2000;
        hall.maxs[0] = hall.maxs[1] = 2000;
        boxWorld(w, hall);
        if (!w.ok)
            return;
        Arsenal module;
        module.have = kArsenalLongJump;
        Arsenal none;
        // How far it carries, in the air: up to the ground again, measured in
        // a slice of time (the room is small: by the speed, not the walls).
        struct Result
        {
            double along, up;
        };
        auto jump = [&](const Arsenal *has, bool moving, int duckFirstTicks, fx look)
        {
            Sim sim = spawn(w);
            Arsenal copy = *has;
            sim.s.arsenal = &copy;
            sim.run(30);
            sim.cmd.viewForward[0] = look;
            if (moving)
            {
                sim.cmd.forward = 1;
                sim.run(2);
            }
            sim.cmd.duck = true;
            sim.run(duckFirstTicks);
            sim.cmd.jump = true;
            sim.run(1);
            const Result out{toUnits(sim.s.velocity[0]), toUnits(sim.s.velocity[2])};
            return out;
        };
        const Result yes = jump(&module, true, 0, kFxOne);
        check(std::fabs(yes.along - 560) < 45 && yes.up > 270 && yes.up < 300, "the module, moving, duck and jump together: 560 along, 299 up");
        // How far it carries on the flat: 560 units a second, for most of 0.75 s.
        {
            Sim sim = spawn(w);
            Arsenal copy = module;
            sim.s.arsenal = &copy;
            sim.run(30);
            sim.cmd.forward = 1;
            sim.run(3);
            sim.cmd.forward = 0;
            sim.cmd.duck = sim.cmd.jump = true;
            const double from = sim.x();
            sim.run(1);
            while (!sim.s.onGround && sim.z() < 400)
                sim.run(1);
            const double far = sim.x() - from;
            std::printf("test_physics: a long jump carries %.1f units on the flat\n", far);
            // (Crouched in the air the feet are 18 units higher, and so have
            // that much further to fall: 0.8 s, not 0.75.)
            check(far > 430 && far < 470, "it carries about 450 units on the flat");
        }
        const Result soon = jump(&module, true, 25, kFxOne);
        check(soon.up > 270, "... and with the crouch pressed under a second ago");
        const Result late = jump(&module, true, 35, kFxOne);
        check(late.along < 200 && late.up < 270, "... but not over a second ago");
        const Result still = jump(&module, false, 0, kFxOne);
        check(still.along < 1 && still.up < 270, "standing still it is an ordinary jump");
        const Result without = jump(&none, true, 0, kFxOne);
        check(without.along < 320 && without.up < 270, "without the module it is an ordinary jump");
        const Result down = jump(&module, true, 0, kFxOne / 2);
        check(down.along < yes.along * 0.6, "looking down takes it from how far it goes, as the game's");
    }

    void testJump()
    {
        World w;
        boxWorld(w);
        if (!w.ok)
            return;
        Sim sim = spawn(w);
        sim.run(30);

        sim.cmd.jump = true;
        double apex = 0;
        int takeOffs = 0;
        bool wasOnGround = true;
        for (int i = 0; i < 120; ++i) // four seconds with the button held
        {
            sim.run(1);
            apex = std::max(apex, sim.z());
            if (wasOnGround && !sim.s.onGround)
                ++takeOffs;
            wasOnGround = sim.s.onGround;
        }
        check(std::fabs(apex - (kFloor + 45)) < 1.0, "a jump rises 45 units");
        check(takeOffs == 1, "holding jump does not jump again on landing");
        check(sim.s.onGround && std::fabs(sim.z() - kFloor) < 0.01, "and lands where it started");

        sim.cmd.jump = false;
        sim.run(1);
        sim.cmd.jump = true;
        sim.run(2);
        check(!sim.s.onGround, "releasing and pressing again jumps again");

        // In the air the keys add at most 30 units a second.
        sim.cmd.jump = false;
        sim.cmd.forward = 1;
        double airSpeed = 0;
        while (!sim.s.onGround)
        {
            sim.run(1);
            if (!sim.s.onGround)
                airSpeed = std::max(airSpeed, sim.speed2d());
        }
        check(airSpeed > 20 && airSpeed < 30.5, "air control is capped at 30 units a second");
    }

    void testDuck()
    {
        World w;
        boxWorld(w);
        if (!w.ok)
            return;
        Sim sim = spawn(w);
        sim.run(30);

        sim.cmd.duck = true;
        double eye = sim.z() + toUnits(sim.s.viewHeight);
        bool monotonic = true;
        for (int i = 0; i < 20; ++i)
        {
            sim.run(1);
            const double now = sim.z() + toUnits(sim.s.viewHeight);
            if (now > eye + 1e-9)
                monotonic = false;
            eye = now;
        }
        check(sim.s.ducked && !sim.s.ducking, "crouched after 0.4 seconds");
        check(std::fabs(sim.z() - (18 + 1.0 / 32)) < 0.01, "the origin drops 18 with the hull");
        check(sim.s.viewHeight == kDuckViewHeight, "eyes 12 above it");
        check(monotonic, "the eyes go down smoothly, never back up");
        check(std::fabs(eye - (kFloor - 18 + 12)) < 0.05, "ending 34 units lower than standing");

        // Crouched movement is a third of the speed.
        sim.cmd.forward = 1;
        double top = 0;
        for (int i = 0; i < 20; ++i)
        {
            sim.run(1);
            top = std::max(top, sim.speed2d());
        }
        check(top > 120 && top < 134, "crouched top speed is a third of 400");
        sim.cmd.forward = 0;

        sim.cmd.duck = false;
        sim.run(2);
        check(!sim.s.ducked && std::fabs(sim.z() - kFloor) < 0.01 &&
                  sim.s.viewHeight == kViewHeight,
              "standing up restores height and view");

        // Under a ceiling too low to stand under, the player stays down.
        World low;
        BoxRoom room;
        room.maxs[2] = 60;
        boxWorld(low, room);
        check(low.ok, "low room cooks");
        if (!low.ok)
            return;
        Sim crawl{&low, PlayerState(), MoveCommand()};
        crawl.s.origin[2] = fxFromInt(20);
        crawl.s.ducked = true;
        crawl.s.viewHeight = kDuckViewHeight;
        crawl.s.duckHeld = true;
        crawl.cmd.duck = true;
        crawl.run(15);
        check(crawl.s.onGround && crawl.s.ducked, "crouched on the low room's floor");
        crawl.cmd.duck = false;
        crawl.run(30);
        check(crawl.s.ducked, "cannot stand up where there is no room");
        check(hullPositionFree(low.move.crouched, crawl.s.origin), "and is not in solid");
    }

    void testStuck()
    {
        World w;
        boxWorld(w);
        if (!w.ok)
            return;
        Sim sim = spawn(w);
        sim.s.origin[0] = fxFromInt(114); // two units into the wall
        sim.s.origin[2] = fxFromInt(64);
        sim.run(1);
        check(!sim.s.stuck && hullPositionFree(w.move.standing, sim.s.origin),
              "slightly inside a wall: nudged free");

        sim = spawn(w);
        sim.s.origin[0] = fxFromInt(900);
        sim.s.velocity[0] = fxFromInt(100);
        sim.run(5);
        check(sim.s.stuck && sim.s.velocity[0] == 0 && sim.s.origin[0] == fxFromInt(900),
              "deep in solid: reported stuck, not moved, not crashed");

        check(isqrt64(0) == 0 && isqrt64(15) == 3 && isqrt64(16) == 4 &&
                  isqrt64(0xFFFFFFFFFFFFFFFFull) == 0xFFFFFFFFu,
              "integer square root rounds down");
        const fx v[3] = {fxFromInt(3), fxFromInt(4), fxFromInt(12)};
        check(fxLength(v) == fxFromInt(13), "vector length");
    }

    /// The same checks on a real map, where planes are not axis-aligned.
    void testRealMap()
    {
        const char *dir = std::getenv("HLDS_VALVE_DIR");
        if (dir == nullptr || *dir == '\0')
        {
            std::printf("test_physics: HLDS_VALVE_DIR not set, real-map checks skipped\n");
            return;
        }
        World w;
        std::string error;
        if (!w.bsp.load(std::string(dir) + "/maps/c1a0.bsp", error))
        {
            check(false, "c1a0 loads");
            return;
        }
        finish(w);
        check(w.ok, "c1a0 cooks");
        if (!w.ok)
            return;

        // Float planes and Q30 planes are not the same planes, so a few traces
        // that graze an edge legitimately differ. More than one in a hundred
        // would mean the fixed point is wrong rather than merely quantised.
        const int count = 20000;
        const int differ = compareTraces(w, w.move.standing, 1, count, 0.05,
                                         w.bsp.models[0].mins, w.bsp.models[0].maxs, 3);
        std::printf("test_physics: c1a0, %d of %d traces differ from the float reference\n",
                    differ, count);
        check(differ < count / 100, "real map: fixed and float traces agree");

        // Walk the map for a minute, turning all the while, jumping and
        // crouching now and then. The claim is modest and important: the
        // player is never left inside solid.
        Sim sim{&w, PlayerState(), MoveCommand()};
        for (std::uint32_t i = 0; i < w.view.entityCount; ++i)
        {
            if (w.view.entities[i].classId ==
                static_cast<std::uint16_t>(EntityClass::InfoPlayerStart))
            {
                for (int a = 0; a < 3; ++a)
                    sim.s.origin[a] = w.view.entities[i].origin[a];
            }
        }
        int stuckTicks = 0;
        double travelled = 0;
        for (int tick = 0; tick < 30 * 60; ++tick)
        {
            const double yaw = tick * 0.013;
            sim.cmd.forwardXY[0] = fxFromDouble(std::cos(yaw));
            sim.cmd.forwardXY[1] = fxFromDouble(std::sin(yaw));
            sim.cmd.rightXY[0] = fxFromDouble(std::sin(yaw));
            sim.cmd.rightXY[1] = fxFromDouble(-std::cos(yaw));
            sim.cmd.forward = 1;
            sim.cmd.side = (tick / 90) % 3 - 1;
            sim.cmd.jump = (tick % 75) < 3;
            sim.cmd.duck = (tick % 400) > 340;
            const double x0 = sim.x(), y0 = sim.y();
            sim.run(1);
            travelled += std::hypot(sim.x() - x0, sim.y() - y0);
            if (sim.s.stuck || !hullPositionFree(sim.s.ducked ? w.move.crouched : w.move.standing,
                                                 sim.s.origin))
                ++stuckTicks;
        }
        std::printf("test_physics: c1a0 walk, %.0f units travelled, %d stuck ticks\n", travelled,
                    stuckTicks);
        check(stuckTicks == 0, "real map: a minute of movement never ends a tick in solid");
        check(travelled > 500, "real map: and actually goes somewhere");
    }
} // namespace

int main()
{
    testTraces();
    testMovement();
    testWalk();
    testLongJump();
    testJump();
    testDuck();
    testStuck();
    testRealMap();
    if (g_failures == 0)
        std::printf("test_physics: ok\n");
    return g_failures == 0 ? 0 : 1;
}
