// The divide tricks probe (Kuratius's ideas, 2026-11-26): what the console
// says about each, on the game's own shapes of divide and root.
//
// Every figure is bus-clock ticks per call (33.5 MHz on a DS and a DSi
// alike), the cost of measuring taken off; "warm" with the code in the
// cache, "cold" with the instruction cache emptied before each call (the
// routines are in ITCM, as the game's are); "bad" is how many answers were
// wrong, and must be 0 on every line but the sweep's.
//
// At start, as the game's boot test would, it finds the shortest wait after
// which the divider's and the root unit's answers are right (interrupts off,
// the mode always written), and the quotient size up to which a divide a bit
// at a time beats the soft divide; the pages use what it found.
//
//   1  Divide: the old hardware divide, the divider with the dirty count
//      (polled, and timed), the game's soft divide, and the "smart" one
//      (a bit at a time for short quotients, soft for the rest).
//   2  Quotient bits: a bit at a time against the soft divide, by the size
//      of the quotient.
//   3  Pairs and roots: a walk's step done as two soft divides, by one
//      reciprocal, or by the divider and the soft divide at once; and the
//      root by the unit (the game's, polled with the count, timed) and
//      without it.
//   4  Timed read: what the wait sweep found.
//
// A turns the page.

#include <cstdint>
#include <cstdio>

#include <nds.h>

extern "C"
{
    std::uint32_t probeTime(const void *routine, const std::uint32_t *in, std::uint32_t *out);
    void probeNull();
    void divHwOld();
    void divPollCount();
    void divPollCountMode();
    void divTimedCount();
    void divShift();
    void divSmart();
    void rootPollCount();
    void rootTimedCount();
    void rootGame();
    void rootValueMode();
    void rootModeValue();
    void rootValueCond();
    void pairHwSoft();
    void pairDivide();
    void pairReciprocal();
    std::uint32_t rootSoft32(std::uint32_t v);
    std::uint32_t rootSoft64(std::uint64_t v);
    void __wrap___aeabi_ldivmod();
    void __real___aeabi_ldivmod();
    extern std::uint32_t divCount, sqrtCount, divDelay, sqrtDelay, rootWait;
    extern std::uint32_t smartSteps;
}

namespace
{
    constexpr int kCases = 1024;
    constexpr int kMaxDelay = 96;

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
    std::int64_t magnitude(std::int64_t v) { return v < 0 ? -v : v; }

    // (The answers are the library's own, called by name: a "/" here would
    // be the game's divide.)
    void put(Case &c, std::int64_t n, std::int64_t d)
    {
        c.in[0] = static_cast<std::uint32_t>(n);
        c.in[1] = static_cast<std::uint32_t>(n >> 32);
        c.in[2] = static_cast<std::uint32_t>(d);
        c.in[3] = static_cast<std::uint32_t>(d >> 32);
        probeTime(reinterpret_cast<const void *>(__real___aeabi_ldivmod), c.in, c.want);
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
            while (magnitude(t) > magnitude(d))
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
            while (magnitude(v) > length)
                v /= 2;
            put(c, v << 12, length);
        }
    }
    /// A clip's place in its span: (done << 12) / span.
    void fillAnim()
    {
        for (Case &c : cases)
        {
            const std::int64_t span = static_cast<std::int64_t>((next() & 0xFFFFFF) | 1);
            std::int64_t done = static_cast<std::int64_t>(next() & 0xFFFFFF);
            while (done > span)
                done /= 2;
            put(c, done << 12, span);
        }
    }
    /// Quotients of exactly `bits` bits, by divisors of 12 to 24 bits.
    void fillBits(int bits)
    {
        for (Case &c : cases)
        {
            const int dBits = 12 + static_cast<int>(next() % 13);
            const std::int64_t d = static_cast<std::int64_t>((next() & ((1ull << dBits) - 1)) | (1ull << (dBits - 1)));
            const std::int64_t q = static_cast<std::int64_t>((next() & ((1ull << bits) - 1)) | (1ull << (bits - 1)));
            const std::int64_t r = static_cast<std::int64_t>(next() & ((1ull << dBits) - 1)) / 2;
            const std::int64_t n = q * d + (r < d ? r : d - 1);
            put(c, (next() & 1) ? -n : n, (next() & 1) ? -d : d);
        }
    }
    /// A walk's step: two numbers, each at most the distance (16 units and
    /// up), by it. The answers in the first two words.
    void fillPair()
    {
        for (Case &c : cases)
        {
            const std::int32_t distance = static_cast<std::int32_t>((next() & 0x7FFFFF) | 0x10000);
            std::int32_t d[2];
            for (std::int32_t &v : d)
            {
                v = static_cast<std::int32_t>(ofBits(23));
                while (magnitude(v) > distance)
                    v /= 2;
            }
            c.in[0] = static_cast<std::uint32_t>(d[0]);
            c.in[1] = static_cast<std::uint32_t>(d[1]);
            c.in[2] = static_cast<std::uint32_t>(distance);
            c.in[3] = 0;
            for (int k = 0; k < 2; ++k)
            {
                const std::int64_t n = static_cast<std::int64_t>(d[k]) * 65536;
                const std::uint32_t in[4] = {static_cast<std::uint32_t>(n), static_cast<std::uint32_t>(n >> 32),
                                             static_cast<std::uint32_t>(distance), 0};
                std::uint32_t out[4];
                probeTime(reinterpret_cast<const void *>(__real___aeabi_ldivmod), in, out);
                c.want[k] = out[0];
            }
        }
    }

    /// floor(sqrt(v)) a bit at a time: the answers the roots are held to.
    std::uint32_t slowRoot(std::uint64_t v)
    {
        std::uint64_t result = 0, bit = std::uint64_t(1) << 62;
        while (bit > v)
            bit >>= 2;
        while (bit != 0)
        {
            if (v >= result + bit)
            {
                v -= result + bit;
                result = (result >> 1) + bit;
            }
            else
                result >>= 1;
            bit >>= 2;
        }
        return static_cast<std::uint32_t>(result);
    }
    void putRoot(Case &c, std::uint64_t v)
    {
        c.in[0] = static_cast<std::uint32_t>(v);
        c.in[1] = static_cast<std::uint32_t>(v >> 32);
        c.in[2] = c.in[3] = 0;
        c.want[0] = slowRoot(v);
    }
    /// A length: three Q12 numbers of up to 22 bits, squared and summed.
    void fillLength()
    {
        for (Case &c : cases)
        {
            std::uint64_t sum = 0;
            for (int k = 0; k < 3; ++k)
            {
                const std::int64_t v = ofBits(4 + static_cast<int>(next() % 19));
                sum += static_cast<std::uint64_t>(v * v);
            }
            putRoot(c, sum);
        }
    }
    /// Roots of 32 bits: 8 to 32 bits.
    void fillRoot32()
    {
        for (Case &c : cases)
            putRoot(c, next() >> (32 + static_cast<int>(next() % 25)));
    }

    struct Result
    {
        int warmTenths;
        int coldTenths;
        int bad;
    };

    int nullTenths = 0;
    /// The mode the root unit is left in before each call: 32 bits (another
    /// user's) or 64 (the game's, where nothing else takes roots).
    bool sqrtLeft64 = false;

    int runOnce(const void *routine, int words, bool cold, int &bad)
    {
        std::uint32_t total = 0, out[4];
        for (const Case &c : cases)
        {
            REG_DIVCNT = DIV_64_64; // (as another user might leave it)
            REG_SQRTCNT = sqrtLeft64 ? SQRT_64 : SQRT_32;
            if (cold)
                IC_InvalidateAll();
            total += probeTime(routine, c.in, out);
            for (int w = 0; w < words; ++w)
                bad += out[w] != c.want[w];
        }
        return static_cast<int>(total * 10 / kCases);
    }

    Result measure(const void *routine, int words = 4)
    {
        Result r = {0, 0, 0};
        const int saved = REG_IME;
        REG_IME = 0;
        runOnce(routine, 0, false, r.bad); // into the cache
        r.warmTenths = runOnce(routine, words, false, r.bad) - nullTenths;
        r.coldTenths = runOnce(routine, words, true, r.bad) - nullTenths;
        REG_IME = saved;
        return r;
    }

    void show(const char *name, const Result &r)
    {
        printf("%-12s%4d.%d %4d.%d %4d\n", name, r.warmTenths / 10, r.warmTenths % 10, r.coldTenths / 10,
               r.coldTenths % 10, r.bad);
    }

    template <typename F>
    const void *fn(F *f)
    {
        return reinterpret_cast<const void *>(f);
    }

    // ---- What the start finds ----------------------------------------------

    struct Sweep
    {
        int delay;        ///< the first wait with none wrong (and none after), or -1
        int badBefore;    ///< wrong at the wait before it
        int pollTenths;   ///< polled, for comparison
        int timedTenths;  ///< at that wait
    };
    Sweep divSweep, rootSweep;
    /// The root's three orders of writing, left in 32 bits then in 64.
    Sweep orderSweep[3][2];
    int shiftTenths[33], softTenths[33];

    /// The shortest wait (turns of the loop) after which every answer of
    /// `timed` is right, for three waits in a row; -1 if none up to the most.
    Sweep sweep(const void *timed, const void *polled, std::uint32_t &delay, int words)
    {
        Sweep s = {-1, 0, 0, 0};
        int lastBad = 0, run = 0;
        for (int d = 0; d <= kMaxDelay; ++d)
        {
            delay = static_cast<std::uint32_t>(d);
            const Result r = measure(timed, words);
            if (r.bad == 0)
            {
                if (run++ == 0)
                {
                    s.delay = d;
                    s.badBefore = lastBad;
                    s.timedTenths = r.warmTenths;
                }
                if (run == 3)
                    break;
            }
            else
            {
                run = 0;
                s.delay = -1;
                lastBad = r.bad;
            }
        }
        if (run < 3)
            s.delay = -1;
        s.pollTenths = measure(polled, words).warmTenths;
        delay = s.delay < 0 ? 0 : static_cast<std::uint32_t>(s.delay);
        return s;
    }

    void findAll()
    {
        // The divider's wait, on a trace's divides.
        fillTrace();
        divSweep = sweep(fn(divTimedCount), fn(divPollCount), divDelay, 2);
        // The root's, on lengths.
        fillLength();
        rootSweep = sweep(fn(rootTimedCount), fn(rootPollCount), sqrtDelay, 1);
        // The orders, each its own shortest wait.
        const void *const orders[3] = {fn(rootValueMode), fn(rootModeValue), fn(rootValueCond)};
        for (int o = 0; o < 3; ++o)
            for (int left = 0; left < 2; ++left)
            {
                sqrtLeft64 = left != 0;
                orderSweep[o][left] = sweep(orders[o], fn(rootPollCount), rootWait, 1);
            }
        sqrtLeft64 = false;
        // A bit at a time against the soft divide, by the quotient's size; the
        // smart divide takes the first for as long as it is no slower.
        smartSteps = 0;
        bool ahead = true;
        for (int k = 1; k <= 32; ++k)
        {
            fillBits(k);
            shiftTenths[k] = measure(fn(divShift), 4).warmTenths;
            softTenths[k] = measure(fn(__wrap___aeabi_ldivmod), 4).warmTenths;
            if (ahead && shiftTenths[k] <= softTenths[k])
                smartSteps = static_cast<std::uint32_t>(k);
            else
                ahead = false;
        }
    }

    // ---- The pages ------------------------------------------------------------

    void divideBlock(const char *title, void (*fill)())
    {
        fill();
        printf("%s\n", title);
        show(" hw old", measure(fn(divHwOld)));
        show(" hw count", measure(fn(divPollCount)));
        show(" hw cnt mode", measure(fn(divPollCountMode)));
        show(" hw timed", measure(fn(divTimedCount)));
        show(" soft", measure(fn(__wrap___aeabi_ldivmod)));
        show(" smart", measure(fn(divSmart)));
    }

    void pageDivide()
    {
        printf("Divide (%s) smart<=%lu\n", isDSiMode() ? "DSi" : "DS", static_cast<unsigned long>(smartSteps));
        printf("%-12s  warm   cold  bad\n", "");
        divideBlock("trace (t<<30)/d", fillTrace);
        divideBlock("unit (v<<12)/len", fillUnit);
        divideBlock("anim (n<<12)/span", fillAnim);
    }

    void pageBits()
    {
        printf("Quotient bits (%s)\n", isDSiMode() ? "DSi" : "DS");
        printf(" k  bit-at-a-time  soft (warm)\n");
        for (int k = 2; k <= 32; k += 2)
            printf("%2d  %5d.%d       %5d.%d\n", k, shiftTenths[k] / 10, shiftTenths[k] % 10, softTenths[k] / 10,
                   softTenths[k] % 10);
        printf("bit at a time up to %lu bits\n", static_cast<unsigned long>(smartSteps));
    }

    void pagePairsRoots()
    {
        printf("Pairs, roots (%s)\n", isDSiMode() ? "DSi" : "DS");
        printf("%-12s  warm   cold  bad\n", "");
        fillPair();
        printf("walk step: 2 by 1 distance\n");
        show(" 2 soft", measure(fn(pairDivide), 2));
        show(" reciprocal", measure(fn(pairReciprocal), 2));
        show(" hw + soft", measure(fn(pairHwSoft), 2));
        fillLength();
        printf("root of a length (<2^46)\n");
        show(" game", measure(fn(rootGame), 1));
        show(" hw count", measure(fn(rootPollCount), 1));
        show(" hw timed", measure(fn(rootTimedCount), 1));
        show(" soft 64", measure(fn(rootSoft64), 1));
        fillRoot32();
        printf("root of 32 bits\n");
        show(" game", measure(fn(rootGame), 1));
        show(" hw timed", measure(fn(rootTimedCount), 1));
        show(" soft 32", measure(fn(rootSoft32), 1));
    }

    void showSweep(const char *name, const Sweep &s)
    {
        printf("%s\n", name);
        printf(" polled      %4d.%d\n", s.pollTenths / 10, s.pollTenths % 10);
        if (s.delay < 0)
        {
            printf(" no wait up to %d is right\n", kMaxDelay);
            return;
        }
        printf(" wait %d turns %4d.%d\n", s.delay, s.timedTenths / 10, s.timedTenths % 10);
        printf(" (wrong at %d: %d of %d)\n", s.delay - 1, s.badBefore, kCases);
    }

    void pageOrder()
    {
        printf("Root, order of writes (%s)\n", isDSiMode() ? "DSi" : "DS");
        printf("IRQs off, as the game\n");
        printf("left in:      32 bits   64 bits\n");
        printf("%-11s%3s%6s %3s%6s\n", "", "wt", "ticks", "wt", "ticks");
        const char *const names[3] = {"value,mode", "mode,value", "value,mode?"};
        for (int o = 0; o < 3; ++o)
        {
            printf("%-11s", names[o]);
            for (int left = 0; left < 2; ++left)
            {
                const Sweep &w = orderSweep[o][left];
                if (w.delay < 0)
                    printf("%3s%6s%s", "-", "-", left == 0 ? " " : "");
                else
                    printf("%3d%4d.%d%s", w.delay, w.timedTenths / 10, w.timedTenths % 10, left == 0 ? " " : "");
            }
            printf("\n");
        }
        printf("\nvalue,mode: the game's now\nmode?: only if not in it\n(a turn: 2 instructions)\n");
    }

    void pageTimed()
    {
        printf("Timed read (%s)\n", isDSiMode() ? "DSi" : "DS");
        printf("interrupts off, mode written\n\n");
        showSweep("divider 64/32, traces", divSweep);
        printf("\n");
        showSweep("root 64, lengths", rootSweep);
        printf("\n(a turn: 2 instructions)\n");
    }

    void waitForA()
    {
        printf("A: next page");
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
        runOnce(fn(probeNull), 0, false, ignored);
        nullTenths = runOnce(fn(probeNull), 0, false, ignored);
        REG_IME = saved;
    }
    printf("Finding the waits...\n");
    findAll();

    for (;;)
    {
        void (*const pages[])() = {pageDivide, pageBits, pagePairsRoots, pageTimed, pageOrder};
        for (void (*page)() : pages)
        {
            consoleClear();
            page();
            waitForA();
        }
    }
}
