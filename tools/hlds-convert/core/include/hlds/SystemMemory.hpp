#pragma once

// How much of the computer's memory is free for the converter to take: to
// cook no more maps at once than there is room for.

#include <cstdint>

namespace hlds
{
    /// Bytes of memory free to be taken now (what the system would give
    /// without swapping); 0 if it cannot say.
    std::uint64_t freeMemoryBytes();
} // namespace hlds
