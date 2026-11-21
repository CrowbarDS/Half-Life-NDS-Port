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

} // namespace

int main()
{
    testSlabs();
    std::printf("test_divfree: %s\n", failures == 0 ? "all passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
