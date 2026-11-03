// The DSP-multiply probe: shared/HlDsp.hpp's instructions (SMULxy, SMULWy,
// SMLAxy, SMLAWy) against the plain arithmetic the host's tests check, and
// what they save.
//
// First, every one of the twelve on edge values and random numbers: "wrong"
// must be 0. Then three loops, each over the same numbers, in CPU cycles per
// item (the timer's bus ticks times two), warm:
//   scale   a Q12 value times a Q16 step >> 16: SMULL and shifts, or SMULWB
//   dot3    three Q14 products summed >> 14: through 64 bits as the game
//           does, in 32 bits, or SMULBB and two SMLABB

#include <cstdint>
#include <cstdio>

#include <nds.h>

#include "kernels.hpp"

namespace
{
    constexpr int kItems = 3 * 512;
    std::int32_t a[kItems], b[kItems], c[kItems];
    std::int32_t q14a[kItems], q14b[kItems], dt[kItems];

    std::uint32_t seed = 0x12345678;
    std::uint32_t next()
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    }

    template <typename F>
    void time(const char *name, F run)
    {
        volatile std::int32_t keep = run(); // (warm the cache)
        cpuStartTiming(0);
        keep = run();
        const std::uint32_t ticks = cpuEndTiming();
        (void)keep;
        const std::uint32_t perHundred = ticks * 2 * 100 / kItems;
        std::printf("%-14s %3lu.%02lu\n", name, (unsigned long)(perHundred / 100), (unsigned long)(perHundred % 100));
    }
} // namespace

int main()
{
    consoleDemoInit();
    std::printf("DSP multiplies (ARMv5TE)\n\n");

    constexpr std::int32_t kEdges[] = {0, 1, -1, 0x7FFF, -0x8000, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF,
                                       static_cast<std::int32_t>(0x80000000), 0x7FFF7FFF, static_cast<std::int32_t>(0x80008000),
                                       0x12345678, -0x12345678};
    constexpr int kEdgeCount = sizeof(kEdges) / sizeof(kEdges[0]);
    int n = 0;
    for (int i = 0; i < kEdgeCount; ++i)
        for (int j = 0; j < kEdgeCount && n < kItems; ++j, ++n)
        {
            a[n] = kEdges[i];
            b[n] = kEdges[j];
            c[n] = kEdges[(i + j) % kEdgeCount];
        }
    for (; n < kItems; ++n)
    {
        a[n] = static_cast<std::int32_t>(next());
        b[n] = static_cast<std::int32_t>(next());
        c[n] = static_cast<std::int32_t>(next());
    }
    int wrong = 0, first = -1;
    for (int round = 0; round < 64; ++round)
    {
        int firstOp = -1;
        wrong += wrongAnswers(a, b, c, kItems, &firstOp);
        if (first < 0)
            first = firstOp;
        for (int i = 0; i < kItems; ++i)
        {
            a[i] = static_cast<std::int32_t>(next());
            b[i] = static_cast<std::int32_t>(next());
            c[i] = static_cast<std::int32_t>(next());
        }
    }
    std::printf("answers checked  %d\n", 64 * kItems * 12);
    std::printf("wrong            %d\n", wrong);
    if (first >= 0)
        std::printf("first wrong op   %d\n", first);

    for (int i = 0; i < kItems; ++i)
    {
        q14a[i] = static_cast<std::int32_t>(next() % 32769) - 16384;
        q14b[i] = static_cast<std::int32_t>(next() % 32769) - 16384;
        dt[i] = 2185; // (the player's step: a 30th of a second, Q16)
        a[i] = static_cast<std::int32_t>(next()) >> 4;
    }
    std::printf("\ncycles an item (warm)\n");
    time("scale smull", [] { return scaleWide(a, dt, kItems); });
    time("scale smulwb", [] { return scaleDsp(a, dt, kItems); });
    time("dot3 64-bit", [] { return dotWide(q14a, q14b, kItems) / 3; });
    time("dot3 32-bit", [] { return dotNarrow(q14a, q14b, kItems) / 3; });
    time("dot3 smlabb", [] { return dotDsp(q14a, q14b, kItems) / 3; });
    const bool same = scaleWide(a, dt, kItems) == scaleDsp(a, dt, kItems) &&
                      dotWide(q14a, q14b, kItems) == dotDsp(q14a, q14b, kItems) &&
                      dotNarrow(q14a, q14b, kItems) == dotDsp(q14a, q14b, kItems);
    std::printf("loops agree      %s\n", same ? "yes" : "NO");
    std::printf("\n%s\n", wrong == 0 && same ? "PASS" : "FAIL");
    while (true)
        swiWaitForVBlank();
    return 0;
}
