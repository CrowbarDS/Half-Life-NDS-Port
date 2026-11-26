// The divider and root safety probe (libnds issue #413): five ways of
// keeping the divider and the square-root unit safe from whoever else uses
// them, for each of their sums (divides 32/32, 64/32, 64/64; roots of 32
// and 64 bits):
//
//   a) none: unsafe, the baseline
//   b) a comutex held from the operands to the answer
//   c) interrupts off (the CPU's I bit) from the operands to the answer
//   d) the user's id stored before the operands and compared after the
//      answer: if it changed, done again (software after 8 tries)
//   e) the answer checked by multiplying back (0 <= n - q d < d; for a
//      root r^2 <= x < (r + 1)^2): software if it is wrong
//
// Each is timed with nothing else using the units, then with an interrupt
// handler (timer 2, 4 kHz: a divide 32/32 and a root of 32 bits) and a
// second thread (woken by timer 3, 3 kHz, so it cuts into the main thread
// wherever it is -- libnds switches threads at every interrupt: four
// divides 64/64 and four roots of 64 bits) using the units all the time,
// the same way. Wrong answers must be 0 for b to e, for everyone.
//
// Also: the longest time c holds interrupts off, and each way with the sum
// started, other work done (a loop of 16 turns) and the answer read after.
//
// Every figure is bus ticks (33.514 MHz on a DS and a DSi: 2 CPU cycles on
// a DS, 4 on a DSi at its 134 MHz). The tables are written to the card as
// divsafe_DS.md / divsafe_DSi.md, ready to paste; A turns the screen's pages.

#include <cstdint>
#include <cstdio>

#include <fat.h>
#include <nds.h>

#include "ops.hpp"

namespace
{
    constexpr int kCases = 256;
    constexpr int kQuietSums = 4096;
    constexpr int kBusySums = 32768;

    Case cases[kOps][kCases];

    const char *const kOpNames[kOps] = {"div 32/32", "div 64/32", "div 64/64", "sqrt 32", "sqrt 64"};
    const char *const kSchemeNames[kSchemes] = {"a none", "b comutex", "c IRQs off", "d owner id", "e verify"};

    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    std::uint64_t next()
    {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
    }

    void fillCases()
    {
        for (int i = 0; i < kCases; ++i)
        {
            Case *c = &cases[0][i];
            // div 32/32: either sign, divisors of every size (never 0, never
            // the one that overflows).
            c->n = static_cast<std::int32_t>(next());
            c->d = static_cast<std::int32_t>(next()) >> (next() % 31);
            if (c->d == 0 || c->d == -1)
                c->d = 3;
            c = &cases[1][i];
            c->n = static_cast<std::int64_t>(next()) >> (next() % 32);
            c->d = static_cast<std::int32_t>(next()) >> (next() % 31);
            if (c->d == 0 || c->d == -1)
                c->d = 5;
            c = &cases[2][i];
            c->n = static_cast<std::int64_t>(next()) >> (next() % 32);
            c->d = static_cast<std::int64_t>(next()) >> (next() % 63);
            if (c->d == 0 || c->d == -1)
                c->d = 7;
            c = &cases[3][i];
            c->n = static_cast<std::uint32_t>(next()) >> (next() % 24);
            c->d = 0;
            c = &cases[4][i];
            c->n = static_cast<std::int64_t>(next() >> (next() % 40));
            c->d = 0;
        }
        for (int op = 0; op < kOps; ++op)
            for (Case &c : cases[op])
                c.want = softSum(static_cast<Op>(op), c);
    }

    struct Row
    {
        int quietTenths;
        int busyTenths;
        std::uint32_t wrongQuiet;
        std::uint32_t wrongBusy;
        Stats busy;
    };
    Row syncRows[kOps][kSchemes];
    Row asyncRows[kOps][kSchemes];
    int irqOffMost[kOps];
    int workTenths;
    int loopTenths;

    /// Ticks a sum, in tenths, the loop's own cost taken off.
    int tenthsOf(std::uint32_t ticks, int n)
    {
        return static_cast<int>(static_cast<std::uint64_t>(ticks) * 10 / static_cast<std::uint32_t>(n)) - loopTenths;
    }

    Row measure(SumFn f, Op op, Scheme scheme)
    {
        Row r = {};
        std::uint32_t wrong = 0;
        runLoop(f, cases[op], kCases, 64, wrong); // (into the cache)
        r.quietTenths = tenthsOf(runLoop(f, cases[op], kCases, kQuietSums, wrong), kQuietSums);
        r.wrongQuiet = wrong;
        contentionStart(scheme);
        const std::uint32_t ticks = runLoop(f, cases[op], kCases, kBusySums, wrong);
        contentionStop();
        r.busy = statsTake();
        r.busyTenths = tenthsOf(ticks, kBusySums);
        r.wrongBusy = wrong;
        return r;
    }

    void measureAll()
    {
        std::uint32_t wrong = 0;
        loopTenths = 0;
        runLoop(nullSum, cases[0], kCases, 64, wrong);
        loopTenths = tenthsOf(runLoop(nullSum, cases[0], kCases, kQuietSums, wrong), kQuietSums);
        workTenths = tenthsOf(runLoop(workAlone, cases[0], kCases, kQuietSums, wrong), kQuietSums);
        for (int op = 0; op < kOps; ++op)
        {
            printf(".");
            for (int s = 0; s < kSchemes; ++s)
            {
                syncRows[op][s] = measure(syncSum(static_cast<Op>(op), static_cast<Scheme>(s)), static_cast<Op>(op),
                                          static_cast<Scheme>(s));
                asyncRows[op][s] = measure(asyncSum(static_cast<Op>(op), static_cast<Scheme>(s)), static_cast<Op>(op),
                                           static_cast<Scheme>(s));
            }
            statsTake();
            runLoop(irqOffTimed(static_cast<Op>(op)), cases[op], kCases, kQuietSums, wrong);
            irqOffMost[op] = static_cast<int>(statsTake().irqOffMost);
        }
    }

    // ---- What is shown and written ---------------------------------------------

    const char *consoleName() { return isDSiMode() ? "DSi" : "DS"; }

    /// "12.3" from tenths.
    const char *t(int tenths, char *buffer)
    {
        if (tenths < 0)
            snprintf(buffer, 16, "-%d.%d", -tenths / 10, -tenths % 10);
        else
            snprintf(buffer, 16, "%d.%d", tenths / 10, tenths % 10);
        return buffer;
    }

    void writeTables(FILE *f)
    {
        char a[16], b[16], c[16];
        fprintf(f, "### %s mode%s\n\n", consoleName(), isDSiMode() ? " (CPU at 134 MHz)" : " (CPU at 67 MHz)");
        fprintf(f, "Bus ticks per operation (33.514 MHz; one tick is %d CPU cycles here). "
                   "Contended: a timer IRQ at 4 kHz (div 32/32 + sqrt 32) and a cothread woken at 3 kHz "
                   "(4 x div 64/64 + 4 x sqrt 64), both using the units with the same scheme. "
                   "%d operations quiet, %d contended.\n\n",
                isDSiMode() ? 4 : 2, kQuietSums, kBusySums);
        fprintf(f, "| Op | Scheme | quiet | contended | wrong (main) | wrong (IRQ+thread) | IRQ+thread ops | "
                   "retries | fallbacks | IRQ skips |\n");
        fprintf(f, "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|\n");
        for (int op = 0; op < kOps; ++op)
            for (int s = 0; s < kSchemes; ++s)
            {
                const Row &r = syncRows[op][s];
                fprintf(f, "| %s | %s | %s | %s | %lu | %lu | %lu | %lu (%lu) | %lu (%lu) | %lu |\n", kOpNames[op],
                        kSchemeNames[s], t(r.quietTenths, a), t(r.busyTenths, b),
                        static_cast<unsigned long>(r.wrongQuiet + r.wrongBusy),
                        static_cast<unsigned long>(r.busy.othersWrong), static_cast<unsigned long>(r.busy.othersDone),
                        static_cast<unsigned long>(r.busy.retries), static_cast<unsigned long>(r.busy.othersRetries),
                        static_cast<unsigned long>(r.busy.fallbacks), static_cast<unsigned long>(r.busy.othersFallbacks),
                        static_cast<unsigned long>(r.busy.skips));
            }
        fprintf(f, "\nRetries and fallbacks: main thread (IRQ handler + other thread).\n\n");
        fprintf(f, "| Op | c) longest IRQs-off window (ticks, incl. 2 timer reads) |\n|---|---:|\n");
        for (int op = 0; op < kOps; ++op)
            fprintf(f, "| %s | %d |\n", kOpNames[op], irqOffMost[op]);
        fprintf(f, "\nStart, other work (%s ticks alone), then read:\n\n", t(workTenths, c));
        fprintf(f, "| Op | Scheme | async quiet | sync + work | async contended | wrong (main) | retries | "
                   "fallbacks |\n");
        fprintf(f, "|---|---|---:|---:|---:|---:|---:|---:|\n");
        for (int op = 0; op < kOps; ++op)
            for (int s = 0; s < kSchemes; ++s)
            {
                const Row &r = asyncRows[op][s];
                fprintf(f, "| %s | %s | %s | %s | %s | %lu | %lu | %lu |\n", kOpNames[op], kSchemeNames[s],
                        t(r.quietTenths, a), t(syncRows[op][s].quietTenths + workTenths, b), t(r.busyTenths, c),
                        static_cast<unsigned long>(r.wrongQuiet + r.wrongBusy),
                        static_cast<unsigned long>(r.busy.retries), static_cast<unsigned long>(r.busy.fallbacks));
            }
        fprintf(f, "\n");
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

    void pageOp(int op)
    {
        char a[16], b[16];
        printf("%s (%s), bus ticks\n", kOpNames[op], consoleName());
        printf("           quiet  busy wrong\n");
        for (int s = 0; s < kSchemes; ++s)
        {
            const Row &r = syncRows[op][s];
            printf("%-10s%6s%6s%6lu\n", kSchemeNames[s], t(r.quietTenths, a), t(r.busyTenths, b),
                   static_cast<unsigned long>(r.wrongQuiet + r.wrongBusy + r.busy.othersWrong));
        }
        printf("\nwrong: main + the others'\n");
        printf("retry d %lu  fall d %lu e %lu\n", static_cast<unsigned long>(syncRows[op][kOwner].busy.retries),
               static_cast<unsigned long>(syncRows[op][kOwner].busy.fallbacks),
               static_cast<unsigned long>(syncRows[op][kVerify].busy.fallbacks));
        printf("b: IRQ skipped %lu\n", static_cast<unsigned long>(syncRows[op][kMutex].busy.skips));
        printf("c: IRQs off at most %d\n\n", irqOffMost[op]);
        printf("async (work %s)  quiet  busy\n", t(workTenths, a));
        for (int s = 0; s < kSchemes; ++s)
        {
            const Row &r = asyncRows[op][s];
            printf("%-10s     %6s%6s %lu\n", kSchemeNames[s], t(r.quietTenths, a), t(r.busyTenths, b),
                   static_cast<unsigned long>(r.wrongQuiet + r.wrongBusy));
        }
    }
} // namespace

int main()
{
    consoleDemoInit();
    if (isDSiMode())
        setCpuClock(true);

    // Timers 0 and 1: a 32-bit count of bus ticks.
    TIMER_DATA(0) = 0;
    TIMER_DATA(1) = 0;
    TIMER_CR(1) = TIMER_ENABLE | TIMER_CASCADE;
    TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1;

    printf("Divider and root safety (%s)\n", consoleName());
    fillCases();
    contentionInit();
    printf("measuring");
    measureAll();

    bool written = false;
    if (fatInitDefault())
    {
        char path[32];
        snprintf(path, sizeof(path), "divsafe_%s.md", consoleName());
        if (FILE *f = fopen(path, "w"))
        {
            writeTables(f);
            written = fclose(f) == 0;
        }
    }

    for (;;)
    {
        for (int op = 0; op < kOps; ++op)
        {
            consoleClear();
            pageOp(op);
            printf("\n%s", written ? "tables on the card\n" : "no card: tables not written\n");
            waitForA();
        }
    }
}
