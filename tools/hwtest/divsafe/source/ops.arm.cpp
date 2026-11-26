// The sums, each way of keeping the units safe, in ARM code and in ITCM (as
// a library's hot maths would be). Every way is a template inlined into its
// own small function, so that each costs what it adds and nothing else.
//
// Users: the main thread (id 1), the other thread (id 2) and the interrupt
// handler (id 3), all using the same way at a time, as they would if all of
// them went through one library.

#include <cstdint>

#include <nds.h>

#include "HlSoftRoot.hpp"
#include "ops.hpp"

#define HOT [[gnu::section(".itcm.text.divsafe"), gnu::noinline]]
#define INLINE inline __attribute__((always_inline))

namespace
{
    // Counters by user (index = id): each written by its user alone.
    volatile std::uint32_t retriesBy[4], fallbacksBy[4], skipsBy[4], wrongBy[4], doneBy[4];
    volatile std::uint32_t irqOffMost;

    volatile std::uint32_t divOwner, sqrtOwner;
    comutex_t divMutex, sqrtMutex;

    /// The way the interrupt handler and the other thread use, or -1: they
    /// leave the units alone.
    volatile int otherScheme = -1;

    constexpr bool isRoot(Op op) { return op >= kSqrt32; }

    // ---- The units -------------------------------------------------------

    /// The operands, the mode always written (first: the operands start the
    /// unit again after it).
    template <Op op>
    INLINE void start(const Case &c)
    {
        if constexpr (op == kDiv32)
        {
            REG_DIVCNT = DIV_32_32;
            REG_DIV_NUMER_L = static_cast<std::int32_t>(c.n);
            REG_DIV_DENOM_L = static_cast<std::int32_t>(c.d);
        }
        else if constexpr (op == kDiv64_32)
        {
            REG_DIVCNT = DIV_64_32;
            REG_DIV_NUMER = c.n;
            REG_DIV_DENOM_L = static_cast<std::int32_t>(c.d);
        }
        else if constexpr (op == kDiv64_64)
        {
            REG_DIVCNT = DIV_64_64;
            REG_DIV_NUMER = c.n;
            REG_DIV_DENOM = c.d;
        }
        else if constexpr (op == kSqrt32)
        {
            REG_SQRTCNT = SQRT_32;
            REG_SQRT_PARAM_L = static_cast<std::uint32_t>(c.n);
        }
        else
        {
            REG_SQRTCNT = SQRT_64;
            REG_SQRT_PARAM = static_cast<std::uint64_t>(c.n);
        }
    }

    template <Op op>
    INLINE void waitReady()
    {
        if constexpr (isRoot(op))
            while (REG_SQRTCNT & SQRT_BUSY)
                ;
        else
            while (REG_DIVCNT & DIV_BUSY)
                ;
    }

    template <Op op>
    INLINE std::int64_t readResult()
    {
        if constexpr (op == kDiv32)
            return static_cast<std::int32_t>(REG_DIV_RESULT_L);
        else if constexpr (op == kDiv64_32 || op == kDiv64_64)
            return REG_DIV_RESULT;
        else
            return static_cast<std::int64_t>(REG_SQRT_RESULT);
    }

    // ---- Checking an answer by multiplying back --------------------------

    /// a * b, 128 bits.
    INLINE void mul64(std::uint64_t a, std::uint64_t b, std::uint64_t &hi, std::uint64_t &lo)
    {
        const std::uint64_t a0 = static_cast<std::uint32_t>(a), a1 = a >> 32;
        const std::uint64_t b0 = static_cast<std::uint32_t>(b), b1 = b >> 32;
        const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
        const std::uint64_t mid = (p00 >> 32) + static_cast<std::uint32_t>(p01) + static_cast<std::uint32_t>(p10);
        lo = (mid << 32) | static_cast<std::uint32_t>(p00);
        hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    }

    /// Whether q is the sum's one right answer: a divide's truncated
    /// quotient (|q d| <= |n| < |q d| + |d|, q's sign n's times d's), a
    /// root's floor (r^2 <= x < (r + 1)^2).
    template <Op op>
    INLINE bool right(const Case &c, std::int64_t q)
    {
        if constexpr (op == kDiv32)
        {
            const std::int32_t n = static_cast<std::int32_t>(c.n), d = static_cast<std::int32_t>(c.d);
            const std::int64_t r = static_cast<std::int64_t>(n) - static_cast<std::int64_t>(static_cast<std::int32_t>(q)) * d;
            const std::int64_t ad = d < 0 ? -static_cast<std::int64_t>(d) : d;
            return (r < 0 ? -r : r) < ad && (r == 0 || (r < 0) == (n < 0));
        }
        else if constexpr (op == kDiv64_32 || op == kDiv64_64)
        {
            const std::int64_t d = op == kDiv64_32 ? static_cast<std::int32_t>(c.d) : c.d;
            const std::uint64_t un = c.n < 0 ? 0 - static_cast<std::uint64_t>(c.n) : static_cast<std::uint64_t>(c.n);
            // (64 by 32: |d| is known to fit 32 bits, half the multiplies go.)
            const std::uint64_t ud = op == kDiv64_32
                                         ? static_cast<std::uint32_t>(d < 0 ? -d : d)
                                         : (d < 0 ? 0 - static_cast<std::uint64_t>(d) : static_cast<std::uint64_t>(d));
            if (q != 0 && (q < 0) != ((c.n < 0) != (d < 0)))
                return false;
            const std::uint64_t uq = q < 0 ? 0 - static_cast<std::uint64_t>(q) : static_cast<std::uint64_t>(q);
            std::uint64_t hi, lo;
            mul64(uq, ud, hi, lo);
            return hi == 0 && lo <= un && un - lo < ud;
        }
        else if constexpr (op == kSqrt32)
        {
            const std::uint64_t x = static_cast<std::uint32_t>(c.n), r = static_cast<std::uint64_t>(q);
            return r <= 0xFFFF && r * r <= x && (r + 1) * (r + 1) > x;
        }
        else
        {
            const std::uint64_t x = static_cast<std::uint64_t>(c.n), r = static_cast<std::uint64_t>(q);
            return r <= 0xFFFFFFFFull && r * r <= x && (r == 0xFFFFFFFFull || (r + 1) * (r + 1) > x);
        }
    }

    // ---- The ways ----------------------------------------------------------

    INLINE std::uint32_t irqOff()
    {
        std::uint32_t old;
        asm volatile("mrs %0, cpsr" : "=r"(old));
        asm volatile("msr cpsr_c, %0" : : "r"(old | 0x80) : "memory");
        return old;
    }
    INLINE void irqRestore(std::uint32_t old) { asm volatile("msr cpsr_c, %0" : : "r"(old) : "memory"); }

    template <Op op>
    INLINE volatile std::uint32_t &ownerOf()
    {
        return isRoot(op) ? sqrtOwner : divOwner;
    }
    template <Op op>
    INLINE comutex_t *mutexOf()
    {
        return isRoot(op) ? &sqrtMutex : &divMutex;
    }

    /// One sum by user `id`, kept safe by `s`. In the interrupt handler
    /// (`inIrq`) the mutex cannot be waited for: the sum is left (skipped).
    template <Op op, Scheme s, int id, bool inIrq>
    INLINE std::int64_t sum(const Case &c, bool &skipped)
    {
        if constexpr (s == kNone)
        {
            start<op>(c);
            waitReady<op>();
            return readResult<op>();
        }
        else if constexpr (s == kMutex)
        {
            if constexpr (inIrq)
            {
                if (!comutex_try_acquire(mutexOf<op>()))
                {
                    skipped = true;
                    return 0;
                }
            }
            else
            {
                comutex_acquire(mutexOf<op>());
            }
            start<op>(c);
            waitReady<op>();
            const std::int64_t r = readResult<op>();
            comutex_release(mutexOf<op>());
            return r;
        }
        else if constexpr (s == kIrqOff)
        {
            const std::uint32_t old = irqOff();
            start<op>(c);
            waitReady<op>();
            const std::int64_t r = readResult<op>();
            irqRestore(old);
            return r;
        }
        else if constexpr (s == kOwner)
        {
            for (int tries = 0; tries < 8; ++tries)
            {
                ownerOf<op>() = id;
                start<op>(c);
                waitReady<op>();
                const std::int64_t r = readResult<op>();
                if (ownerOf<op>() == static_cast<std::uint32_t>(id))
                    return r;
                retriesBy[id] = retriesBy[id] + 1;
            }
            fallbacksBy[id] = fallbacksBy[id] + 1;
            return softSum(op, c);
        }
        else
        {
            start<op>(c);
            waitReady<op>();
            const std::int64_t r = readResult<op>();
            if (right<op>(c, r))
                return r;
            fallbacksBy[id] = fallbacksBy[id] + 1;
            return softSum(op, c);
        }
    }

    /// Other work between the start and the read: a loop of 16 turns.
    INLINE void work()
    {
        for (int i = 0; i < 16; ++i)
            asm volatile("");
    }

    // ---- The main thread's ---------------------------------------------------

    template <Op op, Scheme s>
    HOT std::int64_t syncOne(const Case &c)
    {
        bool skipped = false;
        return sum<op, s, 1, false>(c, skipped);
    }

    template <Op op, Scheme s>
    HOT std::int64_t asyncOne(const Case &c)
    {
        if constexpr (s == kNone)
        {
            start<op>(c);
            work();
            waitReady<op>();
            return readResult<op>();
        }
        else if constexpr (s == kMutex)
        {
            comutex_acquire(mutexOf<op>());
            start<op>(c);
            work();
            waitReady<op>();
            const std::int64_t r = readResult<op>();
            comutex_release(mutexOf<op>());
            return r;
        }
        else if constexpr (s == kIrqOff)
        {
            // (Interrupts off through the other work too.)
            const std::uint32_t old = irqOff();
            start<op>(c);
            work();
            waitReady<op>();
            const std::int64_t r = readResult<op>();
            irqRestore(old);
            return r;
        }
        else if constexpr (s == kOwner)
        {
            ownerOf<op>() = 1;
            start<op>(c);
            work();
            waitReady<op>();
            const std::int64_t r = readResult<op>();
            if (ownerOf<op>() == 1u)
                return r;
            retriesBy[1] = retriesBy[1] + 1;
            bool skipped = false;
            return sum<op, kOwner, 1, false>(c, skipped);
        }
        else
        {
            start<op>(c);
            work();
            waitReady<op>();
            const std::int64_t r = readResult<op>();
            if (right<op>(c, r))
                return r;
            fallbacksBy[1] = fallbacksBy[1] + 1;
            return softSum(op, c);
        }
    }

    /// c, the time interrupts are off measured on timer 0 (bus ticks, the
    /// two reads of the timer in it).
    template <Op op>
    HOT std::int64_t irqOffOne(const Case &c)
    {
        const std::uint32_t old = irqOff();
        const std::uint16_t t0 = TIMER_DATA(0);
        start<op>(c);
        waitReady<op>();
        const std::int64_t r = readResult<op>();
        const std::uint16_t t1 = TIMER_DATA(0);
        irqRestore(old);
        const std::uint32_t ticks = static_cast<std::uint16_t>(t1 - t0);
        if (ticks > irqOffMost)
            irqOffMost = ticks;
        return r;
    }

    template <Op op>
    constexpr SumFn syncRow[kSchemes] = {syncOne<op, kNone>, syncOne<op, kMutex>, syncOne<op, kIrqOff>,
                                         syncOne<op, kOwner>, syncOne<op, kVerify>};
    template <Op op>
    constexpr SumFn asyncRow[kSchemes] = {asyncOne<op, kNone>, asyncOne<op, kMutex>, asyncOne<op, kIrqOff>,
                                          asyncOne<op, kOwner>, asyncOne<op, kVerify>};
    constexpr const SumFn *syncTable[kOps] = {syncRow<kDiv32>, syncRow<kDiv64_32>, syncRow<kDiv64_64>,
                                              syncRow<kSqrt32>, syncRow<kSqrt64>};
    constexpr const SumFn *asyncTable[kOps] = {asyncRow<kDiv32>, asyncRow<kDiv64_32>, asyncRow<kDiv64_64>,
                                               asyncRow<kSqrt32>, asyncRow<kSqrt64>};
    constexpr SumFn irqOffTable[kOps] = {irqOffOne<kDiv32>, irqOffOne<kDiv64_32>, irqOffOne<kDiv64_64>,
                                         irqOffOne<kSqrt32>, irqOffOne<kSqrt64>};

    // ---- The others: an interrupt handler and a second thread --------------

    constexpr int kOtherCases = 64;
    Case irqDiv[kOtherCases], irqRoot[kOtherCases], threadDiv[kOtherCases], threadRoot[kOtherCases];

    /// One sum of the others' by the way in use, its answer checked.
    template <Op op, int id, bool inIrq>
    INLINE void otherSum(int scheme, const Case &c)
    {
        bool skipped = false;
        std::int64_t r;
        switch (scheme)
        {
        case kNone: r = sum<op, kNone, id, inIrq>(c, skipped); break;
        case kMutex: r = sum<op, kMutex, id, inIrq>(c, skipped); break;
        case kIrqOff: r = sum<op, kIrqOff, id, inIrq>(c, skipped); break;
        case kOwner: r = sum<op, kOwner, id, inIrq>(c, skipped); break;
        default: r = sum<op, kVerify, id, inIrq>(c, skipped); break;
        }
        if (skipped)
        {
            skipsBy[id] = skipsBy[id] + 1;
            return;
        }
        doneBy[id] = doneBy[id] + 1;
        if (r != c.want)
            wrongBy[id] = wrongBy[id] + 1;
    }

    /// Timer 2: a divide of 32 bits and a root of 32 (other modes than most
    /// of the main thread's, so that an answer it leaves is surely wrong).
    HOT void irqWork()
    {
        const int scheme = otherScheme;
        if (scheme < 0)
            return;
        static unsigned k;
        const unsigned i = k++ % kOtherCases;
        otherSum<kDiv32, 3, true>(scheme, irqDiv[i]);
        otherSum<kSqrt32, 3, true>(scheme, irqRoot[i]);
    }

    void wakeOnly() {}

    /// Woken by timer 3 (an interrupt, so it cuts into the main thread
    /// wherever it is: libnds switches threads at every interrupt): divides
    /// of 64 by 64 bits and roots of 64, four of each, then back to sleep.
    int contender(void *)
    {
        unsigned k = 0;
        for (;;)
        {
            cothread_yield_irq(IRQ_TIMER3);
            const int scheme = otherScheme;
            if (scheme < 0)
                continue;
            for (int j = 0; j < 4; ++j, ++k)
            {
                otherSum<kDiv64_64, 2, false>(scheme, threadDiv[k % kOtherCases]);
                otherSum<kSqrt64, 2, false>(scheme, threadRoot[k % kOtherCases]);
            }
        }
        return 0;
    }

    std::uint64_t seed = 0x2545F4914F6CDD1Dull;
    std::uint64_t next()
    {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
    }
} // namespace

std::int64_t softSum(Op op, const Case &c)
{
    switch (op)
    {
    case kDiv32: return static_cast<std::int32_t>(c.n) / static_cast<std::int32_t>(c.d);
    case kDiv64_32: return c.n / static_cast<std::int32_t>(c.d);
    case kDiv64_64: return c.n / c.d;
    case kSqrt32: return hldata::softroot::root32(static_cast<std::uint32_t>(c.n));
    default: return hldata::softroot::root64(static_cast<std::uint64_t>(c.n));
    }
}

HOT std::int64_t workAlone(const Case &)
{
    work();
    return 0;
}

SumFn syncSum(Op op, Scheme scheme) { return syncTable[op][scheme]; }
SumFn asyncSum(Op op, Scheme scheme) { return asyncTable[op][scheme]; }
SumFn irqOffTimed(Op op) { return irqOffTable[op]; }

Stats statsTake()
{
    const int saved = REG_IME;
    REG_IME = 0;
    Stats s = {};
    s.retries = retriesBy[1];
    s.fallbacks = fallbacksBy[1];
    s.othersRetries = retriesBy[2] + retriesBy[3];
    s.othersFallbacks = fallbacksBy[2] + fallbacksBy[3];
    s.skips = skipsBy[3];
    s.othersWrong = wrongBy[2] + wrongBy[3];
    s.othersDone = doneBy[2] + doneBy[3];
    s.irqOffMost = irqOffMost;
    for (int i = 0; i < 4; ++i)
    {
        retriesBy[i] = 0;
        fallbacksBy[i] = 0;
        skipsBy[i] = 0;
        wrongBy[i] = 0;
        doneBy[i] = 0;
    }
    irqOffMost = 0;
    REG_IME = saved;
    return s;
}

void contentionInit()
{
    comutex_init(&divMutex);
    comutex_init(&sqrtMutex);
    // The others' sums: a divide of 32 bits and a root of 32 for the
    // handler, 64 by 64 and a root of 64 for the thread.
    for (int i = 0; i < kOtherCases; ++i)
    {
        Case &a = irqDiv[i];
        a.n = static_cast<std::int32_t>(next());
        a.d = static_cast<std::int32_t>(next()) >> (next() % 24);
        if (a.d == 0 || a.d == -1)
            a.d = 7;
        a.want = softSum(kDiv32, a);
        Case &b = irqRoot[i];
        b.n = static_cast<std::uint32_t>(next());
        b.d = 0;
        b.want = softSum(kSqrt32, b);
        Case &e = threadDiv[i];
        e.n = static_cast<std::int64_t>(next());
        e.d = static_cast<std::int64_t>(next()) >> (next() % 60);
        if (e.d == 0 || e.d == -1)
            e.d = 11;
        e.want = softSum(kDiv64_64, e);
        Case &f = threadRoot[i];
        f.n = static_cast<std::int64_t>(next());
        f.d = 0;
        f.want = softSum(kSqrt64, f);
    }
    irqSet(IRQ_TIMER2, irqWork);
    irqSet(IRQ_TIMER3, wakeOnly);
    cothread_create(contender, nullptr, 4096, COTHREAD_DETACHED);
}

void contentionStart(Scheme scheme)
{
    statsTake();
    otherScheme = scheme;
    TIMER_DATA(2) = TIMER_FREQ(4000);
    TIMER_CR(2) = TIMER_ENABLE | TIMER_DIV_1 | TIMER_IRQ_REQ;
    TIMER_DATA(3) = TIMER_FREQ(3000);
    TIMER_CR(3) = TIMER_ENABLE | TIMER_DIV_1 | TIMER_IRQ_REQ;
    irqEnable(IRQ_TIMER2 | IRQ_TIMER3);
}

void contentionStop()
{
    otherScheme = -1;
    irqDisable(IRQ_TIMER2 | IRQ_TIMER3);
    TIMER_CR(2) = 0;
    TIMER_CR(3) = 0;
}

/// The answer itself: what the loop costs, to take off.
HOT std::int64_t nullSum(const Case &c) { return c.want; }

/// n sums by `f` over `cases` (`count` of them, a power of 2), wrong
/// answers counted; bus ticks for all, on timers 0 and 1 (cascaded).
HOT std::uint32_t runLoop(SumFn f, const Case *cases, int count, int n, std::uint32_t &wrong)
{
    auto now = []()
    {
        std::uint16_t hi, lo;
        do
        {
            hi = TIMER_DATA(1);
            lo = TIMER_DATA(0);
        } while (hi != TIMER_DATA(1));
        return (static_cast<std::uint32_t>(hi) << 16) | lo;
    };
    std::uint32_t bad = 0;
    const std::uint32_t t0 = now();
    for (int i = 0; i < n; ++i)
    {
        const Case &c = cases[i & (count - 1)];
        bad += f(c) != c.want;
    }
    const std::uint32_t t1 = now();
    wrong = bad;
    return t1 - t0;
}
