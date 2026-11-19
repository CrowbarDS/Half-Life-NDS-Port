// The overlay probe (tools/ovl.py, source/Overlay.*): two overlays in one
// region, swapped and checked, on a DS and a DSi.
//
// What it says on the top screen, each a PASS or a FAIL:
//   pack     the pack is found (nitro:/ovl/arm9.ovl) and is this program's
//   one/two  each answers as itself once loaded: code, constants
//   swaps    1000 swaps, every answer right -- a stale instruction cache
//            would answer as the overlay before
//   zeroed   each load's zeroed data starts at 0, its data as built
//   heap     no heap taken by a load
//   irq      a timer's interrupt went on firing through the loads
//   twl      (DSi) DSi-only code calls into an overlay through its gate
//   KB/s     how fast a load reads
// Then: A calls `two` through its gate with `one` loaded, which must stop
// on the halt that names it (the gate's whole point).

#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <nds.h>
#include <filesystem.h>

#include "Overlay.hpp"
#include "probe.hpp"

using hlrt::ovl::Id;

namespace
{
    volatile int ticks = 0;
    void onTimer() { ++ticks; }

    // The gates: the program's way into each overlay.
    HL_OVL_GATED int oneCompute(int x)
    {
        HL_OVL_GATE(One);
        return one::compute(x);
    }
    HL_OVL_GATED int oneCounter()
    {
        HL_OVL_GATE(One);
        return one::bssCounter();
    }
    HL_OVL_GATED int oneData()
    {
        HL_OVL_GATE(One);
        return one::dataBump();
    }
    HL_OVL_GATED int twoCompute(int x)
    {
        HL_OVL_GATE(Two);
        return two::compute(x);
    }
    HL_OVL_GATED int twoCounter()
    {
        HL_OVL_GATE(Two);
        return two::bssCounter();
    }
    HL_OVL_GATED int twoData()
    {
        HL_OVL_GATE(Two);
        return two::dataBump();
    }

    void result(const char *what, bool ok, const char *detail = "")
    {
        std::printf("%-7s %s %s\n", what, ok ? "PASS" : "FAIL", detail);
    }

    void waitForever()
    {
        for (;;)
            swiWaitForVBlank();
    }
} // namespace

// The loader's halt: said, and stopped.
extern "C" [[noreturn]] void hlOvlHalt(const char *what, const char *detail)
{
    std::printf("\nHALT: %s\n  %s\n", what, detail);
    waitForever();
    __builtin_unreachable();
}

// DSi-only code, in DSi memory, calling an overlay through its gate.
TWL_CODE __attribute__((noinline)) int twlCall(int x) { return oneCompute(x); }

int main(int, char **)
{
    consoleDemoInit();
    std::printf("overlay probe (%s)\n\n", isDSiMode() ? "DSi" : "DS");
    if (!nitroFSInit(nullptr))
    {
        std::printf("no NitroFS\n");
        waitForever();
    }
    const char *why = nullptr;
    const bool packed = hlrt::ovl::init(nullptr, &why);
    result("pack", packed, packed ? "" : why);
    if (!packed)
        waitForever();

    timerStart(0, ClockDivider_1024, TIMER_FREQ_1024(1000), onTimer);

    hlrt::ovl::load(Id::One);
    result("one", oneCompute(5) == 16 && oneCounter() == 0 && oneData() == 101);
    hlrt::ovl::load(Id::Two);
    result("two", twoCompute(5) == 37 && twoCounter() == 0 && twoData() == 201);

    // Swapped, again and again: every answer its own.
    const struct mallinfo before = mallinfo();
    const int ticksBefore = ticks;
    int wrong = 0, notZeroed = 0;
    cpuStartTiming(1);
    for (int n = 0; n < 1000; ++n)
    {
        const int x = n & 255;
        if ((n & 1) == 0)
        {
            hlrt::ovl::load(Id::One);
            wrong += oneCompute(x) != x * 3 + 1;
            notZeroed += oneCounter() != 0 || oneData() != 101;
        }
        else
        {
            hlrt::ovl::load(Id::Two);
            wrong += twoCompute(x) != x * 7 + 2;
            notZeroed += twoCounter() != 0 || twoData() != 201;
        }
    }
    const u32 elapsed = cpuEndTiming();
    const struct mallinfo after = mallinfo();
    char line[64];
    std::snprintf(line, sizeof(line), "(%d wrong)", wrong);
    result("swaps", wrong == 0, line);
    std::snprintf(line, sizeof(line), "(%d not)", notZeroed);
    result("zeroed", notZeroed == 0, line);
    std::snprintf(line, sizeof(line), "(%d bytes)", after.uordblks - before.uordblks);
    result("heap", after.uordblks == before.uordblks, line);
    std::snprintf(line, sizeof(line), "(%d ticks)", ticks - ticksBefore);
    result("irq", ticks - ticksBefore > 0, line);
    if (isDSiMode())
    {
        hlrt::ovl::load(Id::One);
        result("twl", twlCall(9) == 28);
    }
    // (1000 loads of the larger overlay's size at most; the timer counts
    // at 33.5 MHz.)
    extern char __ovl_two_load_end[], __ovl_two_start[];
    const unsigned bytes = 1000u * static_cast<unsigned>(__ovl_two_load_end - __ovl_two_start);
    const unsigned ms = static_cast<unsigned>(timerTicks2msec(elapsed));
    std::printf("loads   %u ms for %u KB at most: %u KB/s\n", ms, bytes / 1024, ms ? bytes / ms : 0);
    std::printf("\nA: call two through its gate\n   with one loaded (must HALT)\n");

    hlrt::ovl::load(Id::One);
    for (;;)
    {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_A)
        {
#ifdef NEG_UNGATED
            // (A negative build: into an overlay without its gate, which
            // tools/ovl.py check must refuse.)
            std::printf("ungated %d\n", two::compute(1));
#else
            std::printf("two says %d\n", twoCompute(1));
#endif
        }
    }
}
