#pragma once
// What the game's code tells tools/hlds-convert/tests/diag_replay as it runs,
// for its frame-by-frame comparison with the real game (tools/ref/diag.sh):
// hooks it sets, each null until then, called through HL_DIAG.
//
// On a host only. The console's builds (__NDS__) have none of it: HL_DIAG is
// nothing there, and costs nothing.

#include <cstdint>

#if !defined(__NDS__)
namespace hldata::diag
{
    /// A move on a ladder (playerMove's ladderMove), as the game's
    /// PM_LadderMove would have it: which ladder, whether its face was found
    /// (always, but for a shape the line to its middle missed), the normal
    /// found (unit, Q30), solid under the feet, and the climb speeds the keys
    /// asked for (Q12 units a second).
    struct Ladder
    {
        int index;
        bool hit;
        std::int32_t normal[3];
        bool onFloor;
        std::int32_t forward, right;
    };
    inline void (*ladder)(const Ladder &) = nullptr;
} // namespace hldata::diag

#define HL_DIAG(hook, ...)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (::hldata::diag::hook != nullptr)                                                                           \
            ::hldata::diag::hook(__VA_ARGS__);                                                                         \
    } while (0)
#else
#define HL_DIAG(hook, ...) ((void)0)
#endif
