// The game's arithmetic with fewer divides, held to what it was, bit for bit.
//
// Each change that takes a divide out of a hot path (the console's divider
// costs 54 bus ticks a divide; tools/divsites.py says where they come from)
// keeps its old body here, as it was, and is compared with it over the
// edges that matter -- ends on a face, a hair either side of one, a line
// along an axis -- and over many random cases besides.

#include <cstdint>
#include <cstdio>
#include <random>

#include "HlMonsters.hpp"

using namespace hldata;

namespace
{
    int failures = 0;
    void check(bool ok, const char *what)
    {
        if (!ok)
        {
            ++failures;
            std::printf("FAIL: %s\n", what);
        }
    }

    // ------------------------------------------------ the slab tests, as they were

    bool lineEntersBoxBefore(const fx *from, const fx *to, const fx *mins, const fx *maxs, std::int64_t &enter)
    {
        std::int64_t leave = kFractionOne;
        enter = 0;
        for (int k = 0; k < 3; ++k)
        {
            const fx d = to[k] - from[k];
            if (d == 0)
            {
                if (from[k] < mins[k] || from[k] > maxs[k])
                    return false;
                continue;
            }
            std::int64_t t0 = (static_cast<std::int64_t>(mins[k] - from[k]) << kFractionShift) / d;
            std::int64_t t1 = (static_cast<std::int64_t>(maxs[k] - from[k]) << kFractionShift) / d;
            if (t0 > t1)
            {
                const std::int64_t swap = t0;
                t0 = t1;
                t1 = swap;
            }
            enter = t0 > enter ? t0 : enter;
            leave = t1 < leave ? t1 : leave;
            if (enter > leave)
                return false;
        }
        return true;
    }

    void traceBoxesBefore(const MoveWorld &world, const fx *start, const fx *end, fx growXY, fx growZ, Trace &best)
    {
        for (int n = 0; n < world.boxCount; ++n)
        {
            const SolidBox &box = world.boxes[n];
            if (!box.active)
                continue;
            std::int64_t enter = -1;
            std::int64_t leave = kFractionOne;
            int axis = -1;
            bool inside = true, miss = false;
            for (int i = 0; i < 3 && !miss; ++i)
            {
                const fx grow = i == 2 ? growZ : growXY;
                const fx lo = box.mins[i] - grow, hi = box.maxs[i] + grow;
                const fx d = end[i] - start[i];
                if (start[i] <= lo || start[i] >= hi)
                    inside = false;
                if (d == 0)
                {
                    miss = start[i] <= lo || start[i] >= hi;
                    continue;
                }
                std::int64_t t0 = (static_cast<std::int64_t>(lo - start[i]) << kFractionShift) / d;
                std::int64_t t1 = (static_cast<std::int64_t>(hi - start[i]) << kFractionShift) / d;
                if (t0 > t1)
                {
                    const std::int64_t swap = t0;
                    t0 = t1;
                    t1 = swap;
                }
                if (t0 > enter)
                {
                    enter = t0;
                    axis = i;
                }
                if (t1 < leave)
                    leave = t1;
                miss = enter > leave;
            }
            if (miss || inside || axis < 0 || enter < 0 || enter >= best.fraction)
                continue;
            const fx d = end[axis] - start[axis];
            const std::int64_t back = (static_cast<std::int64_t>(kFxOne / 32) << kFractionShift) / (d < 0 ? -d : d);
            const std::int64_t fraction = enter > back ? enter - back : 0;
            best.fraction = static_cast<std::int32_t>(fraction);
            for (int i = 0; i < 3; ++i)
            {
                best.endPos[i] = start[i] + static_cast<fx>((static_cast<std::int64_t>(end[i] - start[i]) * fraction) >>
                                                            kFractionShift);
                best.normal[i] = 0;
            }
            best.normal[axis] = d > 0 ? -(1 << kNormalShift) : (1 << kNormalShift);
            {
                const fx grow = axis == 2 ? growZ : growXY;
                best.dist = d > 0 ? -(box.mins[axis] - grow) : box.maxs[axis] + grow;
            }
            best.mover = -1;
            best.box = static_cast<std::int16_t>(n);
            best.hit = true;
        }
    }

    /// boxTrace's box against another, as it was (its loop's body).
    std::int64_t boxMeetsBoxBefore(const fx *mins, const fx *maxs, const fx *half, const fx *centre, const fx *move,
                                   std::int64_t best, int &stopAxis)
    {
        std::int64_t enter = -1, leave = kFractionOne;
        int axis = -1;
        bool apart = false;
        fx least = 0;
        int leastAxis = -1;
        bool in = true;
        for (int k = 0; k < 3; ++k)
        {
            const fx lo = mins[k] - half[k] - centre[k], hi = maxs[k] + half[k] - centre[k];
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
        stopAxis = axis;
        if (in)
        {
            const fx lo = mins[leastAxis] - half[leastAxis] - centre[leastAxis];
            const fx hi = maxs[leastAxis] + half[leastAxis] - centre[leastAxis];
            const bool outwards = (-lo < hi) ? move[leastAxis] <= 0 : move[leastAxis] >= 0;
            const bool sideways = move[leastAxis] == 0;
            if (!outwards || (sideways && least > fxFromInt(1)))
            {
                stop = 0;
                stopAxis = leastAxis;
            }
        }
        else if (!apart && axis >= 0 && enter >= 0 && enter < leave && enter < best)
        {
            stop = enter;
        }
        return stop;
    }

    bool sameTrace(const Trace &a, const Trace &b)
    {
        bool same = a.allSolid == b.allSolid && a.startSolid == b.startSolid && a.inOpen == b.inOpen &&
                    a.inWater == b.inWater && a.hit == b.hit && a.fraction == b.fraction && a.dist == b.dist &&
                    a.mover == b.mover && a.box == b.box;
        for (int i = 0; i < 3; ++i)
            same = same && a.endPos[i] == b.endPos[i] && a.normal[i] == b.normal[i];
        return same;
    }

    // ------------------------------------------------ the cases

    /// A coordinate near a face at `face`: on it, a raw unit or two either
    /// side, or well off it.
    fx near(std::mt19937 &rng, fx face)
    {
        static const fx offsets[] = {0, 1, -1, 2, -2, kFxOne, -kFxOne, kFxOne * 37, -kFxOne * 37};
        return face + offsets[rng() % (sizeof(offsets) / sizeof(offsets[0]))];
    }

    /// A random coordinate: near one of the box's faces, or anywhere in a
    /// map's room (whole units or not).
    fx coordinate(std::mt19937 &rng, fx lo, fx hi)
    {
        switch (rng() % 4)
        {
        case 0: return near(rng, lo);
        case 1: return near(rng, hi);
        case 2: return static_cast<fx>(static_cast<std::int32_t>(rng() % (8192u * 2)) - 8192) * kFxOne;
        default: return static_cast<fx>(static_cast<std::int32_t>(rng() % (1u << 27)) - (1 << 26));
        }
    }

    void randomBox(std::mt19937 &rng, fx *mins, fx *maxs)
    {
        for (int k = 0; k < 3; ++k)
        {
            const fx middle = static_cast<fx>(static_cast<std::int32_t>(rng() % 4096u) - 2048) * kFxOne +
                              static_cast<fx>(rng() % 8u);
            const fx half = static_cast<fx>(1 + rng() % 64u) * kFxOne / 2 + static_cast<fx>(rng() % 3u);
            mins[k] = middle - half;
            maxs[k] = middle + half;
        }
    }

    void randomLine(std::mt19937 &rng, const fx *mins, const fx *maxs, fx *a, fx *b)
    {
        for (int k = 0; k < 3; ++k)
        {
            a[k] = coordinate(rng, mins[k], maxs[k]);
            // (Along an axis, now and then: the same on both ends.)
            b[k] = rng() % 5 == 0 ? a[k] : coordinate(rng, mins[k], maxs[k]);
        }
    }

    void testSlabs()
    {
        std::mt19937 rng(20261121);
        long mismatch = 0, entered = 0;
        for (long n = 0; n < 4000000; ++n)
        {
            fx mins[3], maxs[3], a[3], b[3];
            randomBox(rng, mins, maxs);
            randomLine(rng, mins, maxs, a, b);
            std::int64_t enterBefore = -7, enterNow = -7;
            const bool before = lineEntersBoxBefore(a, b, mins, maxs, enterBefore);
            const bool now = lineEntersBox(a, b, mins, maxs, enterNow);
            if (before != now || (before && enterBefore != enterNow))
                ++mismatch;
            entered += before;
        }
        std::printf("test_divfree: lineEntersBox, 4000000 lines, %ld entered, %ld differ\n", entered, mismatch);
        check(mismatch == 0, "lineEntersBox answers as it did");
        check(entered > 100000, "... over lines that enter as well as miss");
    }

    void testTraceBoxes()
    {
        std::mt19937 rng(112026);
        constexpr int kBoxes = 6;
        SolidBox boxes[kBoxes];
        MoveWorld world;
        world.boxes = boxes;
        world.boxCount = kBoxes;
        long mismatch = 0, hits = 0;
        for (long n = 0; n < 1500000; ++n)
        {
            for (SolidBox &box : boxes)
            {
                randomBox(rng, box.mins, box.maxs);
                box.active = rng() % 6 != 0;
            }
            fx a[3], b[3];
            const SolidBox &aim = boxes[rng() % kBoxes];
            randomLine(rng, aim.mins, aim.maxs, a, b);
            // (The player's sizes, or none, or a large hull's.)
            static const fx grows[][2] = {{fxFromInt(16), fxFromInt(36)}, {0, 0}, {fxFromInt(32), fxFromInt(18)}};
            const fx *grow = grows[rng() % 3];
            // What the world left of the move first: all of it, or less, or
            // nothing (a fraction is never more than all of it).
            Trace before;
            switch (rng() % 3)
            {
            case 0: before.fraction = kFractionOne; break;
            case 1: before.fraction = static_cast<std::int32_t>(rng() % (kFractionOne + 1u)); break;
            default: before.fraction = rng() % 2 ? 0 : kFractionOne - 1; break;
            }
            Trace now = before;
            traceBoxesBefore(world, a, b, grow[0], grow[1], before);
            detail::traceBoxes(world, a, b, grow[0], grow[1], now);
            mismatch += !sameTrace(before, now);
            hits += before.box >= 0;
        }
        std::printf("test_divfree: traceBoxes, 1500000 moves, %ld met a box, %ld differ\n", hits, mismatch);
        check(mismatch == 0, "traceBoxes answers as it did, whole Trace and all");
        check(hits > 50000, "... over moves that meet a box as well as miss");
    }
    void testBoxMeetsBox()
    {
        std::mt19937 rng(21112026);
        long mismatch = 0, met = 0, inside = 0;
        for (long n = 0; n < 4000000; ++n)
        {
            fx mins[3], maxs[3], half[3], centre[3], move[3];
            randomBox(rng, mins, maxs);
            for (int k = 0; k < 3; ++k)
            {
                half[k] = static_cast<fx>(1 + rng() % 48u) * kFxOne / 2 + static_cast<fx>(rng() % 3u);
                // (The moving box's middle, near the other's faces grown by
                // its own half, or inside, or anywhere.)
                const fx lo = mins[k] - half[k], hi = maxs[k] + half[k];
                centre[k] = rng() % 4 == 0 ? (lo + hi) / 2 : coordinate(rng, lo, hi);
                // (A move: none along this axis now and then; to a face or a
                // hair either side of one; or anything.)
                const fx to = rng() % 5 == 0 ? centre[k] : coordinate(rng, lo, hi);
                move[k] = to - centre[k];
            }
            const std::int64_t best = rng() % 3 == 0 ? static_cast<std::int64_t>(rng() % (kFractionOne + 1u)) : kFractionOne;
            int axisBefore = -9, axisNow = -9;
            const std::int64_t before = boxMeetsBoxBefore(mins, maxs, half, centre, move, best, axisBefore);
            const std::int64_t now = detail::boxMeetsBox(mins, maxs, half, centre, move, best, axisNow);
            // (What boxTrace takes of it: a stop before `best`, and its axis.)
            const bool tookBefore = before >= 0 && before < best, tookNow = now >= 0 && now < best;
            if (tookBefore != tookNow || (tookBefore && (before != now || axisBefore != axisNow)))
                ++mismatch;
            met += tookBefore;
            inside += tookBefore && before == 0;
        }
        std::printf("test_divfree: boxMeetsBox, 4000000 moves, %ld met (%ld from inside), %ld differ\n", met, inside,
                    mismatch);
        check(mismatch == 0, "boxTrace's box against another answers as it did");
        check(met > 100000 && inside > 1000, "... over moves that meet it, from outside and from in it");
    }
    /// fxTimesMs against the 64-bit sum it took the place of.
    void testTimesMs()
    {
        auto before = [](std::int32_t v, int ms) { return static_cast<fx>((static_cast<std::int64_t>(v) * ms) / 1000); };
        long mismatch = 0, wide = 0;
        // (The edges: a product just inside 32 bits and just outside, either
        // sign; a time step of nothing, of one, of a long frame, negative.)
        const int steps[] = {0, 1, -1, 16, 17, 33, 34, 100, 250, 1000, 65535, -1000, 2147483647, -2147483647 - 1};
        const std::int32_t values[] = {0, 1, -1, 999, -999, 1000, -1000, 4096, -4096, 2147483647, -2147483647 - 1,
                                       1073741824, -1073741824, 2147483, -2147483, 2147484, -2147484};
        for (int ms : steps)
        {
            for (std::int32_t v : values)
            {
                mismatch += fxTimesMs(v, ms) != before(v, ms);
                // (Right at the edge of 32 bits: v near INT32_MAX / ms.)
                if (ms != 0)
                {
                    const std::int32_t edge = static_cast<std::int32_t>(2147483647 / (ms < 0 ? -static_cast<std::int64_t>(ms) : ms));
                    for (std::int32_t d = -2; d <= 2; ++d)
                    {
                        const std::int64_t near = static_cast<std::int64_t>(edge) + d;
                        if (near > 2147483647 || near < -2147483647 - 1)
                            continue;
                        mismatch += fxTimesMs(static_cast<std::int32_t>(near), ms) != before(static_cast<std::int32_t>(near), ms);
                        mismatch += fxTimesMs(static_cast<std::int32_t>(-near), ms) != before(static_cast<std::int32_t>(-near), ms);
                    }
                }
            }
        }
        std::mt19937 rng(1000);
        for (long n = 0; n < 20000000; ++n)
        {
            const std::int32_t v = static_cast<std::int32_t>(rng());
            const int ms = n % 4 == 0 ? static_cast<int>(rng()) : static_cast<int>(rng() % 200u) - 20;
            const std::int64_t product = static_cast<std::int64_t>(v) * ms;
            wide += product > 2147483647 || product < -2147483647 - 1;
            mismatch += fxTimesMs(v, ms) != before(v, ms);
        }
        // (What the callers give it besides: 16-bit numbers, unsigned.)
        for (int v = -32768; v <= 65535; v += 7)
        {
            const auto small = static_cast<std::int16_t>(v);
            const auto unsignedSmall = static_cast<std::uint16_t>(v);
            mismatch += fxTimesMs(small, 33) != before(small, 33);
            mismatch += fxTimesMs(unsignedSmall, 33) != before(unsignedSmall, 33);
        }
        std::printf("test_divfree: fxTimesMs, 20000000 random (%ld past 32 bits) and the edges, %ld differ\n", wide,
                    mismatch);
        check(mismatch == 0, "fxTimesMs is v * ms / 1000 as the 64-bit sum gave it");
        check(wide > 1000000, "... with products past 32 bits as well as within");
    }
} // namespace

int main()
{
    testSlabs();
    testTraceBoxes();
    testBoxMeetsBox();
    testTimesMs();
    std::printf("test_divfree: %s\n", failures == 0 ? "all passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
