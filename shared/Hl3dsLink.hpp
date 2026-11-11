#pragma once

// What the ARM7 reads from a 3DS's own side each frame (arm7/source/Rtcom.cpp)
// for the ARM9 (source/game/Input.cpp): the circle pad. One small block in
// main RAM that the ARM9 owns and sends the address of once, on FIFO_USER_02;
// the ARM7 writes it, the ARM9 reads it through the uncached mirror. std-only.

#include <cstdint>

namespace hldata
{
    struct Link3ds
    {
        /// The circle pad, as TwlBg itself reads it (calibrated, 0 in the
        /// middle; measured on an Old 3DS: about -105..105). Right and up
        /// are positive.
        volatile std::int16_t padX = 0, padY = 0;
        /// Grows by one with each reading.
        volatile std::uint32_t count = 0;
        /// 1 while readings come (a 3DS, in DSi mode, with a TWPatch TwlBg
        /// that answered and whose pad was found); 0 otherwise, padX and
        /// padY then 0.
        volatile std::uint8_t present = 0;
    };
} // namespace hldata
