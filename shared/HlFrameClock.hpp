#pragma once

// A frame's time in milliseconds, from the sixtieths of a second (vblanks)
// it took.
//
// A vblank is 16 2/3 ms. Rounded on its own each frame it is 17 (2% fast)
// or, cut down, 16 (4% slow), and two of them 33 -- so a clock made of such
// frames drifts, and drifts differently with the frame rate, away from the
// player's movement, which counts exact time. This carries what is left of
// a millisecond to the next frame instead: over any run of frames the sum
// is the run's exact time, rounded down, never as much as a millisecond out.
//
// std-only and inline: the host tests run it.

#include <cstdint>

namespace hldata
{
    struct FrameClock
    {
        /// Sixtieths of a millisecond not yet given out (0 to 59).
        std::int32_t carry = 0;

        /// This frame's milliseconds, for `vblanks` sixtieths of a second.
        /// (A divide by a constant in 32 bits: a multiply.)
        std::int32_t ms(std::int32_t vblanks)
        {
            const std::int32_t sixtieths = vblanks * 1000 + carry;
            const std::int32_t out = sixtieths / 60;
            carry = sixtieths - out * 60;
            return out;
        }
    };
} // namespace hldata
