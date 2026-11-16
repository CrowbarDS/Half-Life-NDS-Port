#pragma once

// The game library's busiest functions moved into ITCM as it loads, as they
// were built (shared/HlKernelMove.hpp; the list: tools/dsl_kernels.py).

#include <cstdint>

namespace hlrt::kernels
{
    /// Moves the functions dsl/hl.kernels names, of the library loaded at
    /// `base` (its code: `codeBytes`), before any of it has run, and before
    /// any thread but this one could: how many were moved. Each one not --
    /// no room, a sum that does not match, a call that would not reach --
    /// stays where it is, as it was.
    int place(std::uint8_t *base, std::uint32_t codeBytes);
} // namespace hlrt::kernels
