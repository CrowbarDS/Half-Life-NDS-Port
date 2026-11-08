#pragma once

// What the ARM7 learns from a 3DS's own side at start (arm7/source/Rtcom.cpp)
// and tells the ARM9 once, as a value on FIFO_USER_02.

#include <cstdint>

namespace hldata
{
    /// TwlBg answered rtcom (TWPatch's channel to the 3DS's ARM11).
    inline constexpr std::uint32_t kRtcomAnswered = 1;
    /// ... and its top screen is shown at 16:10 (the wide TwlBg).
    inline constexpr std::uint32_t kRtcomWide = 2;
} // namespace hldata
