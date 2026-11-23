#pragma once

// The online library (source/net/OnlineApi.hpp): loaded the first time the
// console goes online, kept until it is turned off.

#include <cstddef>

namespace hlrt
{
    struct OnlineFuncs;
}

namespace hlrt::online
{
    /// Its functions, once loaded; null before.
    const OnlineFuncs *funcs();
    /// Loads it, if it is not: false, and `why` said, if it cannot be.
    bool load(const char **why);
    /// What it took of the heap as it loaded (0 before).
    std::size_t heapBytes();
    /// A DS that has gone online: the library and the IP stack now hold
    /// more of the heap than the heaviest campaign map can spare, so the
    /// campaign waits for the game to be started again. (Never on a DSi.)
    bool campaignBlocked();
} // namespace hlrt::online
