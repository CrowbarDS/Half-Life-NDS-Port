// The ITCM copy probe: what an ITCM overlay would cost to bring in, and what
// a miss of the instruction cache costs -- the two numbers
// tools/icache_rank.py --by-phase needs (--copy-cycles, --miss-cycles), on the
// console itself, not the emulator (whose timings are not the console's).
//
// In CPU cycles (the timer counts the 33.5 MHz bus: twice that on a DS, four
// times on a DSi in its own mode at 134 MHz):
//   copy N KB   main RAM into ITCM, per byte: memcpy and an LDM/STM of
//               eight registers; the source warm in the data cache, cold (the
//               cache cleaned and emptied first), and through the uncached
//               mirror
//   line fill   a chain of branches, one to each 32-byte line of main RAM,
//               run with the instruction cache emptied and again with it
//               warm: the difference, per line, is one miss

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <nds.h>
#include <nds/arm9/tcm_alloc.h>

#include "kernels.hpp"

namespace
{
    constexpr std::uint32_t kMost = 16 * 1024;
    alignas(32) std::uint8_t source[kMost];

    // The branch chain: one instruction at the start of each line. One line
    // to each of the cache's 64 sets (2 KB), no more: a 4-way cache whose
    // replacement counter is shared by all its sets puts lines 64 apart in
    // the same way, where each pushes the other out -- the first probe's 192
    // lines did, and its "warm" run still missed (54 cycles a line on a DSi).
    constexpr int kLines = 64;
    alignas(32) std::uint32_t chain[kLines * 8];

    std::uint32_t cyclesPerTick() { return isDSiMode() ? 4 : 2; }

    void report(const char *what, std::uint32_t ticks, std::uint32_t items)
    {
        const std::uint32_t hundred = ticks * cyclesPerTick() * 100 / items;
        std::printf("%-22s %4lu.%02lu\n", what, (unsigned long)(hundred / 100), (unsigned long)(hundred % 100));
    }

    void timeCopy(const char *how, void *itcm, const void *from, std::uint32_t bytes, bool block, bool cold)
    {
        if (cold)
        {
            DC_FlushAll();
            DC_InvalidateAll();
        }
        cpuStartTiming(0);
        if (block)
            copy32(itcm, from, bytes);
        else
            std::memcpy(itcm, from, bytes);
        const std::uint32_t ticks = cpuEndTiming();
        char what[40];
        std::snprintf(what, sizeof(what), "%2luK %s", (unsigned long)(bytes / 1024), how);
        report(what, ticks, bytes);
    }
} // namespace

int main()
{
    consoleDemoInit();
    std::printf("ITCM copies, I-cache misses (%s)\ncycles a byte, or a line\n\n", isDSiMode() ? "DSi" : "DS");
    for (std::uint32_t i = 0; i < kMost; ++i)
        source[i] = static_cast<std::uint8_t>(i * 7);
    void *itcm = itcm_memalign(32, kMost);
    if (itcm == nullptr)
    {
        std::printf("no %lu bytes of ITCM free\n", (unsigned long)kMost);
        for (;;)
            swiWaitForVBlank();
    }
    const void *uncached = memUncached(source);
    std::memcpy(itcm, source, kMost); // (warm)
    timeCopy("memcpy warm", itcm, source, kMost, false, false);
    for (std::uint32_t bytes = 4096; bytes <= kMost; bytes *= 2)
    {
        timeCopy("ldm/stm warm", itcm, source, bytes, true, false);
        timeCopy("ldm/stm cold", itcm, source, bytes, true, true);
        timeCopy("ldm/stm uncached", itcm, uncached, bytes, true, false);
    }
    const bool same = std::memcmp(itcm, source, kMost) == 0;
    std::printf("copies whole: %s\n\n", same ? "yes" : "NO");

    // The chain: at each line, B to the next line; the last returns.
    for (int l = 0; l < kLines; ++l)
    {
        for (int w = 0; w < 8; ++w)
            chain[l * 8 + w] = 0xE1A00000; // (mov r0, r0)
        // B +32 from this word: offset (32 - 8) / 4 = 6.
        chain[l * 8] = l + 1 < kLines ? 0xEA000006u : 0xE12FFF1Eu; // (b next / bx lr)
    }
    DC_FlushRange(chain, sizeof(chain));
    IC_InvalidateRange(chain, sizeof(chain));
    std::uint32_t coldTicks = 0, warmTicks = 0;
    constexpr int kRounds = 48;
    for (int r = 0; r < kRounds; ++r)
    {
        IC_InvalidateAll();
        cpuStartTiming(0);
        runChain(chain);
        coldTicks += cpuEndTiming();
        cpuStartTiming(0);
        runChain(chain);
        warmTicks += cpuEndTiming();
    }
    report("chain cold, a line", coldTicks, kLines * kRounds);
    report("chain warm, a line", warmTicks, kLines * kRounds);
    // (An emulator that does not model the cache may run it no slower cold.)
    report("one miss", coldTicks > warmTicks ? coldTicks - warmTicks : 0, kLines * kRounds);
    std::printf("\n(a line of the cache is 32 bytes; the\nARM9 runs at %s)\n", isDSiMode() ? "134 MHz" : "67 MHz");
    for (;;)
        swiWaitForVBlank();
}
