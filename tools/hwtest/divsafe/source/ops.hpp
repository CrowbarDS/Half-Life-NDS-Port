#pragma once

// The divider and the square-root unit, each way of keeping them safe from
// whoever else may use them (libnds issue #413), behind one call per sum.

#include <cstdint>

enum Op : int
{
    kDiv32,    ///< 32 / 32 bits
    kDiv64_32, ///< 64 / 32 bits
    kDiv64_64, ///< 64 / 64 bits
    kSqrt32,   ///< root of 32 bits
    kSqrt64,   ///< root of 64 bits
    kOps
};

enum Scheme : int
{
    kNone,   ///< a) nothing: unsafe
    kMutex,  ///< b) a comutex held from the operands to the answer
    kIrqOff, ///< c) interrupts off (the CPU's own bit) from the operands to the answer
    kOwner,  ///< d) the user's id stored before the operands, compared after the answer
    kVerify, ///< e) the answer checked by multiplying back; software if it is wrong
    kSchemes
};

/// One sum and its right answer. A divide: n / d, truncated. A root: of n
/// (its bits, unsigned), rounded down.
struct Case
{
    std::int64_t n;
    std::int64_t d;
    std::int64_t want;
};

/// What the ways did while the probe measured one of them. (Each counter
/// is kept by one user alone -- the main thread, the other thread, the
/// interrupt handler -- so none is lost to a race; summed when read.)
struct Stats
{
    std::uint32_t retries;       ///< d, main thread: sums done again, the id having changed
    std::uint32_t fallbacks;     ///< d, e, main thread: answers taken from software
    std::uint32_t othersRetries; ///< the same, the interrupt handler and the other thread
    std::uint32_t othersFallbacks;
    std::uint32_t skips;       ///< b: sums the interrupt handler left, the mutex held
    std::uint32_t othersWrong; ///< wrong answers the interrupt handler and the other thread got
    std::uint32_t othersDone;  ///< sums they did
    std::uint32_t irqOffMost;  ///< c: the longest time interrupts were off, bus ticks
};

using SumFn = std::int64_t (*)(const Case &);

/// The main thread's sum `op` kept safe by `scheme`.
SumFn syncSum(Op op, Scheme scheme);
/// The same started, other work done, then read (a, b, d, e).
SumFn asyncSum(Op op, Scheme scheme);
/// c with the time interrupts are off measured (Stats::irqOffMost).
SumFn irqOffTimed(Op op);
/// The other work alone, as asyncSum does it.
std::int64_t workAlone(const Case &);

/// The sums done in software: the right answers, and the fallbacks.
std::int64_t softSum(Op op, const Case &c);

/// What was counted since the last call (and counting starts again).
Stats statsTake();

/// The interrupt handler (timer 2, a few kHz) and a second thread (woken by
/// timer 3) using the units all the time, the same way as the main thread.
void contentionStart(Scheme scheme);
void contentionStop();
void contentionInit();

/// The answer itself, for what the loop costs.
std::int64_t nullSum(const Case &c);
/// n sums by f over the cases (count a power of 2), wrong answers counted:
/// bus ticks for all of them (timers 0 and 1, cascaded).
std::uint32_t runLoop(SumFn f, const Case *cases, int count, int n, std::uint32_t &wrong);
