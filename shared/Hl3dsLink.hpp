#pragma once

// What the ARM7 reads from a 3DS's own side each frame (arm7/source/Rtcom.cpp)
// for the ARM9 (source/game/Input.cpp): the circle pad and the gyroscope. One small block in
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
        /// The gyroscope's rates, as the chip gives them (its own axes; 14.375
        /// a degree a second, its zero at rest not quite 0), and whether it
        /// answers.
        volatile std::int16_t gyroX = 0, gyroY = 0, gyroZ = 0;
        volatile std::uint8_t gyroPresent = 0;
    };

    /// Which of the chip's axes is a turn left or right (yaw) and which a
    /// tilt up or down (pitch), and which way: +1 when the chip's rate is
    /// positive turning right / tilting the top back (looking up), -1 the
    /// other way. The chip's axes as the 3DS is held: to be measured on
    /// hardware with tools/hwtest/rtcom (A, then L).
    inline constexpr int kGyroYawAxis = 1, kGyroYawSign = 1;
    inline constexpr int kGyroPitchAxis = 0, kGyroPitchSign = 1;

    /// The chip's rate on `axis` (0 X, 1 Y, 2 Z), made positive the way `sign` says.
    inline int gyroAxis(const Link3ds &l, int axis, int sign)
    {
        const int v = axis == 0 ? l.gyroX : (axis == 1 ? l.gyroY : l.gyroZ);
        return sign < 0 ? -v : v;
    }
} // namespace hldata
