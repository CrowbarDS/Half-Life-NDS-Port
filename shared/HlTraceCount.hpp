#pragma once

// Who traces, and how long it takes them: in a profiling build of the
// console's program (HLDS_PROFILE), each of the traces the game asks for --
// traceAll, traceHull, traceShot, walkStep, lineClear, walkClear -- notes
// where it was called from (its return address), which it is and the time
// it took; perf.txt's "tracesites:" lines say the costliest, and
// tools/tracesites.py names them (source/DebugLog.cpp). What a monster's
// walk, its sight and its shots cost, by the line that asks.
//
// Those functions are kept out of line in such a build only
// (HL_TRACE_NOINLINE), so that the return address is the caller's: inlined,
// it would be the caller's caller. The think worker's time away -- stopped
// at a thinkPoint while the frame has the CPU -- is not counted to the trace
// it stopped in (hlProfileAway). Nothing of it elsewhere: not the host, not
// the game's families (a family's own inline traces are not counted; the
// engine's traceAll they call is), not the Download Play child.

#include <cstdint>

namespace hldata
{
    /// Which trace (with kTraceOnWorker: on the think worker's thread).
    inline constexpr std::uint32_t kTraceAll = 0;
    inline constexpr std::uint32_t kTraceHull = 1;
    inline constexpr std::uint32_t kTraceShot = 2;
    inline constexpr std::uint32_t kTraceWalkStep = 3;
    inline constexpr std::uint32_t kTraceLineClear = 4;
    inline constexpr std::uint32_t kTraceWalkClear = 5;
    inline constexpr std::uint32_t kTraceOnWorker = 8;
} // namespace hldata

#if defined(HLDS_PROFILE) && defined(__NDS__) && !defined(HL_DSL) && !defined(HLDS_CHILD)
// (source/DebugLog.cpp: its clock, and where a trace is counted.)
extern "C" std::uint32_t hlProfileNow();
extern "C" void hlProfileTrace(std::uint32_t at, std::uint32_t kind, std::uint32_t ticks);
/// The think worker's time away so far, all of it (source/game/Think.cpp).
extern "C" std::uint32_t hlProfileAway;

namespace hldata
{
    /// A trace's time, from here to the end of the function, counted to the
    /// line that called it.
    struct TraceSpent
    {
        std::uint32_t at, kind, start, away;
        TraceSpent(std::uint32_t caller, std::uint32_t which)
            : at(caller), kind(which), start(hlProfileNow()), away(hlProfileAway)
        {
        }
        ~TraceSpent() { hlProfileTrace(at, kind, hlProfileNow() - start - (hlProfileAway - away)); }
    };
} // namespace hldata

#define HL_TRACE_NOINLINE __attribute__((noinline))
#define HL_TRACE_SPENT(kind)                                                                                           \
    const ::hldata::TraceSpent hlTraceSpent_(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(              \
                                                 __builtin_return_address(0))),                                        \
                                             (kind))
#else
#define HL_TRACE_NOINLINE
#define HL_TRACE_SPENT(kind) ((void)0)
#endif
