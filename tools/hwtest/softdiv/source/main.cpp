// The soft divide probe: the divide without the divider (HlSoftDiv.hpp,
// Kuratius's method) against the hardware divide the game had and the
// library's, on the game's own shapes of divide -- and whether its answers
// are right. "game" is the game's divide as it is now (source/HwMath.s, the
// same steps by hand, linked here as the game links it); "C++" is
// HlSoftDiv.hpp as the compiler makes it.
//
// Every figure is bus-clock ticks per call (33.5 MHz on a DS and a DSi alike),
// the cost of the measuring taken off. "warm" is with the code in the cache,
// "cold" with the instruction cache emptied before each call: the routines
// under test are in ITCM, as the game's are, and the library's in main RAM.
// "bad" is how many answers were wrong: it must be 0. "other" is how many the
// soft divide left to the library (a quotient of more than 32 bits).
//
// A goes round the pages again.

#include <cstdint>
#include <cstdio>

#include <nds.h>

extern "C"
{
    std::uint32_t probeTime(const void *routine, const std::uint32_t *in, std::uint32_t *out);
    void probeNull();
    void divHw();
    void divSoft();
    void divSoftU();
    void __wrap___aeabi_ldivmod();
    void __wrap___aeabi_uldivmod();
    void __real___aeabi_ldivmod();
    void __real___aeabi_uldivmod();
    extern volatile std::uint32_t softOther;
}

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
    std::int64_t ofBits(int bits)
    {
        const std::int64_t v = static_cast<std::int64_t>(next() & ((std::uint64_t(1) << bits) - 1));
        return (next() & 1) ? -v : v;
    }

    // (The answers are the library's own, called by name: a "/" here would
    // be the divide under test.)
    void put(Case &c, std::int64_t n, std::int64_t d)
    {
        c.in[0] = static_cast<std::uint32_t>(n);
        c.in[1] = static_cast<std::uint32_t>(n >> 32);
        c.in[2] = static_cast<std::uint32_t>(d);
        c.in[3] = static_cast<std::uint32_t>(d >> 32);
        probeTime(reinterpret_cast<const void *>(__real___aeabi_ldivmod), c.in, c.want);
    }
    void putUnsigned(Case &c, std::uint64_t n, std::uint64_t d)
    {
        c.in[0] = static_cast<std::uint32_t>(n);
        c.in[1] = static_cast<std::uint32_t>(n >> 32);
        c.in[2] = static_cast<std::uint32_t>(d);
        c.in[3] = static_cast<std::uint32_t>(d >> 32);
        probeTime(reinterpret_cast<const void *>(__real___aeabi_uldivmod), c.in, c.want);
    }

    /// A trace's fraction: (t << 30) / d, |t| at most |d|.
    void fillTrace()
    {
        for (Case &c : cases)
        {
            std::int64_t d = ofBits(22);
            if (d == 0)
                d = 7;
            std::int64_t t = ofBits(22);
            while ((t < 0 ? -t : t) > (d < 0 ? -d : d)) // (no "%": it is the divide under test)
                t /= 2;
            put(c, t << 30, d);
        }
    }
    /// A vector made unit: (v << 12) / length, |v| at most length.
    void fillUnit()
    {
        for (Case &c : cases)
        {
            const std::int64_t length = (ofBits(24) & 0xFFFFFF) | 1;
            std::int64_t v = ofBits(24);
            while ((v < 0 ? -v : v) > length)
                v /= 2;
            put(c, v << 12, length);
        }
    }
    /// A clip's place in its span: (done << 12) / span, unsigned.
    void fillAnim()
    {
        for (Case &c : cases)
        {
            const std::uint64_t span = (next() & 0xFFFFFF) | 1;
            std::uint64_t done = next() & 0xFFFFFF;
            while (done > span)
                done /= 2;
            putUnsigned(c, done << 12, span);
        }
    }
    /// Long numbers by short ones, either sign (the divider probe's mix):
    /// some quotients of more than 32 bits among them.
    void fillMix()
    {
        for (Case &c : cases)
        {
            std::int64_t n = static_cast<std::int64_t>(next()) >> 18;
            std::int64_t d = static_cast<std::int64_t>(next()) >> 42;
            if (d == 0)
                d = 3;
            put(c, n, d);
        }
    }

    struct Result
    {
        int warmTenths;
        int coldTenths;
        int bad;
    };

    int nullTenths = 0;

    int runOnce(const void *routine, int words, bool cold, int &bad)
    {
        std::uint32_t total = 0, out[4];
        for (const Case &c : cases)
        {
            REG_DIVCNT = kModeDiv64; // (as the game leaves it, mostly)
            if (cold)
                IC_InvalidateAll();
            total += probeTime(routine, c.in, out);
            for (int w = 0; w < words; ++w)
                bad += out[w] != c.want[w];
        }
        return static_cast<int>(total * 10 / kCases);
    }

    Result measure(const void *routine)
    {
        Result r = {0, 0, 0};
        const int saved = REG_IME;
        REG_IME = 0;
        runOnce(routine, 0, false, r.bad); // into the cache
        r.warmTenths = runOnce(routine, 4, false, r.bad) - nullTenths;
        r.coldTenths = runOnce(routine, 4, true, r.bad) - nullTenths;
        REG_IME = saved;
        return r;
    }

    void show(const char *name, const Result &r)
    {
        printf("%-12s%4d.%d %4d.%d %4d\n", name, r.warmTenths / 10, r.warmTenths % 10, r.coldTenths / 10,
               r.coldTenths % 10, r.bad);
    }

    void block(const char *title, void (*fill)(), bool isUnsigned)
    {
        fill();
        printf("%s\n", title);
        show(" hardware", measure(reinterpret_cast<const void *>(divHw)));
        softOther = 0;
        show(" C++", measure(reinterpret_cast<const void *>(isUnsigned ? divSoftU : divSoft)));
        const std::uint32_t other = softOther / 3; // (three runs over the cases)
        show(" game", measure(reinterpret_cast<const void *>(isUnsigned ? __wrap___aeabi_uldivmod
                                                                         : __wrap___aeabi_ldivmod)));
        show(" library", measure(reinterpret_cast<const void *>(isUnsigned ? __real___aeabi_uldivmod
                                                                            : __real___aeabi_ldivmod)));
        if (other != 0)
            printf("  (%lu/%d to the library)\n", static_cast<unsigned long>(other), kCases);
    }

    void waitForA()
    {
        printf("A: again");
        for (;;)
        {
            swiWaitForVBlank();
            scanKeys();
            if (keysDown() & KEY_A)
                return;
        }
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
        fillTrace();
        const int saved = REG_IME;
        REG_IME = 0;
        int ignored = 0;
        runOnce(reinterpret_cast<const void *>(probeNull), 0, false, ignored);
        nullTenths = runOnce(reinterpret_cast<const void *>(probeNull), 0, false, ignored);
        REG_IME = saved;
    }

    for (;;)
    {
        consoleClear();
        printf("Soft divide (%s)\n", isDSiMode() ? "DSi" : "DS");
        printf("%-12s  warm   cold  bad\n", "");
        block("trace (t<<30)/d", fillTrace, false);
        block("unit (v<<12)/len", fillUnit, false);
        block("anim (n<<12)/span", fillAnim, true);
        block("mix 46/22 bits", fillMix, false);
        waitForA();
    }
}
