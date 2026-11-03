// The divider probe: what each way of driving the divider and the square-root
// unit costs, and whether its answers are right.
//
// Every figure is bus-clock ticks per call (33.5 MHz on a DS and a DSi alike),
// the cost of the measuring taken off. "warm" is with the code in the cache,
// "cold" with the instruction cache emptied before each call: the routines
// under test are in ITCM, as the game's are, and the library's are in main
// RAM, as the game's are. "bad" is how many answers were wrong: it must be 0.
//
// Page 1: the 64-bit divide. Page 2: the root and the wrong-mode restart.
// Page 3: 32-bit divides, library against hardware. Page 4: fixed waits.
// A goes to the next page.

#include <cstdint>
#include <cstdio>

#include <nds.h>

extern "C"
{
    std::uint32_t probeTime(const void *routine, const std::uint32_t *in, std::uint32_t *out);
    void probeNull();
    void divOld();
    void divStmIme();
    void divStmCpsr();
    void divMovAdd();
    void divPopPc();
    void divBare();
    void divWait();
    void sqrtOld();
    void sqrtStmCpsr();
    void sqrtMovAdd();
    void hwIdiv();
    void __aeabi_idiv();
    void __aeabi_idivmod();
    void __aeabi_uidivmod();
    extern volatile std::uint32_t probeWaitLoops;
}

volatile std::uint32_t probeWaitLoops = 1;

namespace
{
    constexpr int kCases = 1024;
    constexpr int kModeDiv64 = 2;

    struct Case
    {
        std::uint32_t in[4];
        std::uint32_t want[4];
    };
    Case cases[kCases];

    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    std::uint64_t next()
    {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
    }

    /// Long numbers by short ones, either sign, as the game's are.
    void fillDiv64()
    {
        for (Case &c : cases)
        {
            std::int64_t n = static_cast<std::int64_t>(next()) >> 18;
            std::int64_t d = static_cast<std::int64_t>(next()) >> 42;
            if (d == 0)
                d = 3;
            const std::int64_t q = n / d, r = n % d; // the library's: this probe does not replace it
            c.in[0] = static_cast<std::uint32_t>(n);
            c.in[1] = static_cast<std::uint32_t>(n >> 32);
            c.in[2] = static_cast<std::uint32_t>(d);
            c.in[3] = static_cast<std::uint32_t>(d >> 32);
            c.want[0] = static_cast<std::uint32_t>(q);
            c.want[1] = static_cast<std::uint32_t>(q >> 32);
            c.want[2] = static_cast<std::uint32_t>(r);
            c.want[3] = static_cast<std::uint32_t>(r >> 32);
        }
    }

    void fillRoot()
    {
        for (Case &c : cases)
        {
            const std::uint64_t v = next() >> (2 + next() % 40);
            std::uint64_t value = v, result = 0, bit = std::uint64_t(1) << 62;
            while (bit > value)
                bit >>= 2;
            while (bit != 0)
            {
                if (value >= result + bit)
                {
                    value -= result + bit;
                    result = (result >> 1) + bit;
                }
                else
                {
                    result >>= 1;
                }
                bit >>= 2;
            }
            c.in[0] = static_cast<std::uint32_t>(v);
            c.in[1] = static_cast<std::uint32_t>(v >> 32);
            c.in[2] = c.in[3] = 0;
            c.want[0] = static_cast<std::uint32_t>(result);
        }
    }

    /// Positive numbers below 2^numBits by ones below 2^denBits. Either sign
    /// when `signedToo`.
    void fillDiv32(int numBits, int denBits, bool signedToo)
    {
        for (Case &c : cases)
        {
            std::int32_t n = static_cast<std::int32_t>(next() & ((std::uint64_t(1) << numBits) - 1));
            std::int32_t d = static_cast<std::int32_t>(next() & ((std::uint64_t(1) << denBits) - 1)) | 1;
            if (signedToo && (next() & 1))
                n = -n;
            if (signedToo && (next() & 1))
                d = -d;
            c.in[0] = static_cast<std::uint32_t>(n);
            c.in[1] = static_cast<std::uint32_t>(d);
            c.in[2] = c.in[3] = 0;
            c.want[0] = static_cast<std::uint32_t>(n / d);
            c.want[1] = static_cast<std::uint32_t>(n % d);
        }
    }

    struct Result
    {
        int warmTenths;
        int coldTenths;
        int bad;
    };

    int nullTenths = 0;

    /// Ticks of `routine` over all the cases, and its wrong answers, the
    /// first `words` of each answer compared. `before`, if any, runs ahead of
    /// each call and is not timed. Interrupts are off for all of it.
    template <typename Before>
    int runOnce(const void *routine, int words, bool cold, int &bad, Before before)
    {
        std::uint32_t total = 0, out[4];
        for (const Case &c : cases)
        {
            before();
            if (cold)
                IC_InvalidateAll();
            total += probeTime(routine, c.in, out);
            for (int w = 0; w < words; ++w)
                bad += out[w] != c.want[w];
        }
        return static_cast<int>(total * 10 / kCases);
    }

    template <typename Before>
    Result measure(const void *routine, int words, Before before)
    {
        Result r = {0, 0, 0};
        const int saved = REG_IME;
        REG_IME = 0;
        runOnce(routine, 0, false, r.bad, before); // into the cache
        r.warmTenths = runOnce(routine, words, false, r.bad, before) - nullTenths;
        r.coldTenths = runOnce(routine, words, true, r.bad, before) - nullTenths;
        REG_IME = saved;
        return r;
    }

    Result measure(const void *routine, int words)
    {
        return measure(routine, words, [] {});
    }

    void show(const char *name, const Result &r)
    {
        printf("%-13s%4d.%d %4d.%d %4d\n", name, r.warmTenths / 10, r.warmTenths % 10, r.coldTenths / 10,
               r.coldTenths % 10, r.bad);
    }

    void heading(const char *title)
    {
        consoleClear();
        printf("%s (%s)\n", title, isDSiMode() ? "DSi" : "DS");
        printf("%-13s  warm   cold  bad\n", "");
    }

    void waitForA()
    {
        printf("\nA: next\n");
        for (;;)
        {
            swiWaitForVBlank();
            scanKeys();
            if (keysDown() & KEY_A)
                return;
        }
    }

    void pageDiv64()
    {
        heading("64-bit divide");
        fillDiv64();
        REG_DIVCNT = kModeDiv64;
        show("old", measure(reinterpret_cast<const void *>(divOld), 4));
        show("stm, IME", measure(reinterpret_cast<const void *>(divStmIme), 4));
        show("stm, CPSR", measure(reinterpret_cast<const void *>(divStmCpsr), 4));
        show("mov+add", measure(reinterpret_cast<const void *>(divMovAdd), 4));
        show("... pop pc", measure(reinterpret_cast<const void *>(divPopPc), 4));
        show("stm, no guard", measure(reinterpret_cast<const void *>(divBare), 4));
        waitForA();
    }

    void pageRoot()
    {
        heading("64-bit root");
        fillRoot();
        REG_SQRTCNT = 1;
        show("old", measure(reinterpret_cast<const void *>(sqrtOld), 1));
        show("stm, CPSR", measure(reinterpret_cast<const void *>(sqrtStmCpsr), 1));
        show("mov+add", measure(reinterpret_cast<const void *>(sqrtMovAdd), 1));
        show("wrong mode", measure(reinterpret_cast<const void *>(sqrtStmCpsr), 1, [] { REG_SQRTCNT = 0; }));

        fillDiv64();
        printf("\nDivide left in the wrong mode\n");
        show("was 32/32", measure(reinterpret_cast<const void *>(divStmCpsr), 4, [] { REG_DIVCNT = 0; }));
        show("was 64/32", measure(reinterpret_cast<const void *>(divStmCpsr), 4, [] { REG_DIVCNT = 1; }));
        waitForA();
    }

    void pageDiv32()
    {
        heading("32-bit divide");
        const struct
        {
            const char *name;
            int numBits, denBits;
        } sizes[] = {{"12/6", 12, 6}, {"20/10", 20, 10}, {"30/16", 30, 16}, {"30/4", 30, 4}};
        for (const auto &size : sizes)
        {
            fillDiv32(size.numBits, size.denBits, true);
            printf("%s\n", size.name);
            show(" library", measure(reinterpret_cast<const void *>(__aeabi_idiv), 1));
            show(" hardware", measure(reinterpret_cast<const void *>(hwIdiv), 1));
        }
        fillDiv32(20, 10, true);
        printf("With the remainder, 20/10\n");
        show(" library", measure(reinterpret_cast<const void *>(__aeabi_idivmod), 2));
        show(" hardware", measure(reinterpret_cast<const void *>(hwIdiv), 2));
        fillDiv32(20, 10, false);
        printf("Unsigned, 20/10\n");
        show(" library", measure(reinterpret_cast<const void *>(__aeabi_uidivmod), 2));
        waitForA();
    }

    void pageWait()
    {
        heading("Fixed wait, no polling");
        fillDiv64();
        REG_DIVCNT = kModeDiv64;
        int lowest = 0;
        for (std::uint32_t loops = 1; loops <= 80 && lowest == 0; ++loops)
        {
            probeWaitLoops = loops;
            if (measure(reinterpret_cast<const void *>(divWait), 4).bad == 0)
                lowest = static_cast<int>(loops);
        }
        printf("Fewest loops right: %d\n", lowest);
        if (lowest != 0)
        {
            probeWaitLoops = static_cast<std::uint32_t>(lowest);
            show("at that", measure(reinterpret_cast<const void *>(divWait), 4));
            if (lowest > 1)
            {
                probeWaitLoops = static_cast<std::uint32_t>(lowest - 1);
                show("one fewer", measure(reinterpret_cast<const void *>(divWait), 4));
            }
        }
        show("polling", measure(reinterpret_cast<const void *>(divBare), 4));
        waitForA();
    }
} // namespace

int main()
{
    consoleDemoInit();
    if (isDSiMode())
        setCpuClock(true); // as the game runs

    TIMER_DATA(0) = 0;
    TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1;

    // What measuring takes, with no routine in it.
    {
        fillDiv64();
        const int saved = REG_IME;
        REG_IME = 0;
        int ignored = 0;
        runOnce(reinterpret_cast<const void *>(probeNull), 0, false, ignored, [] {});
        nullTenths = runOnce(reinterpret_cast<const void *>(probeNull), 0, false, ignored, [] {});
        REG_IME = saved;
    }

    for (;;)
    {
        pageDiv64();
        pageRoot();
        pageDiv32();
        pageWait();
    }
}
