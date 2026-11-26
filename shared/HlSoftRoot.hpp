#pragma once

// A square root without the square-root unit, by multiplies alone: like the
// soft divide (HlSoftDiv.hpp), it may run anywhere, with nothing shared.
// Whether it beats the unit is the console's to say (tools/hwtest/divtricks):
// a root of 32 bits may, one of 64 bits likely only on a DSi.
//
// How: the value shifted up by an even count until its top two bits are not
// both 0 (x, in [1/4, 1) of a word); 1/sqrt(x) from a small table, then made
// good by Newton's step y (3 - x y^2) / 2, multiplies only; the root as x y,
// put right once by what its square misses by, (x - s^2) y / 2; shifted back
// down by half the count, and the last step or two found by comparing
// squares. Every answer is floor(sqrt(v)): test_softroot holds it to that
// over every 32-bit value and the 64-bit edges.
//
// std-only: the converter's tests run the same code the console does.

#include <cstdint>

namespace hldata
{
    namespace softroot
    {
        namespace detail
        {
            /// 1/sqrt(x) at the middle of each 64th of [1/4, 1) (index i - 16
            /// for x's top six bits i), less 1, in 256ths: round(2048 sqrt(2 /
            /// (2 i + 1))) - 256.
            inline constexpr std::uint8_t kSeed[48] = {
                248, 234, 220, 208, 196, 186, 176, 166, 158, 150, 142, 135, 128, 121, 115, 109,
                103, 98,  93,  88,  83,  78,  74,  70,  66,  62,  58,  55,  51,  48,  44,  41,
                38,  35,  32,  29,  27,  24,  21,  19,  16,  14,  12,  10,  7,   5,   3,   1,
            };

            inline std::uint32_t mulHigh(std::uint32_t a, std::uint32_t b)
            {
                return static_cast<std::uint32_t>((static_cast<std::uint64_t>(a) * b) >> 32);
            }

            /// 1/sqrt(x), x = top / 2^32 in [1/4, 1), in 2^30ths, after
            /// `steps` Newton steps on the table's.
            inline std::uint32_t inverseRoot(std::uint32_t top, int steps)
            {
                std::uint32_t y = (256u + kSeed[(top >> 26) - 16]) << 22;
                for (int i = 0; i < steps; ++i)
                {
                    const std::uint32_t y2 = mulHigh(y, y);      // y^2, 2^28ths
                    const std::uint32_t xy2 = mulHigh(top, y2);  // x y^2, 2^28ths
                    const std::uint32_t t = (3u << 28) - xy2;    // 3 - x y^2
                    y = static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) * t) >> 29);
                }
                return y;
            }
        } // namespace detail

        /// floor(sqrt(v)), v of 32 bits.
        inline std::uint32_t root32(std::uint32_t v)
        {
            if (v == 0)
                return 0;
            const int c = __builtin_clz(v) & ~1;
            const std::uint32_t m = v << c;
            const std::uint32_t y = detail::inverseRoot(m, 1);
            // sqrt(m) = m y / 2^46; what its square misses by, over twice it.
            const auto s0 = static_cast<std::uint32_t>((static_cast<std::uint64_t>(m) * y) >> 46);
            const std::int64_t miss = static_cast<std::int64_t>(m) - static_cast<std::int64_t>(s0) * s0;
            const auto s = static_cast<std::uint32_t>(static_cast<std::int64_t>(s0) + ((miss * y) >> 47));
            // Down by half the shift, then put right by squares.
            std::uint32_t r = s >> (c / 2);
            std::int64_t left = static_cast<std::int64_t>(v) - static_cast<std::int64_t>(r) * r;
            while (left < 0)
            {
                --r;
                left += 2 * static_cast<std::int64_t>(r) + 1;
            }
            while (left > 2 * static_cast<std::int64_t>(r))
            {
                left -= 2 * static_cast<std::int64_t>(r) + 1;
                ++r;
            }
            return r;
        }

        /// floor(sqrt(v)), v of 64 bits.
        inline std::uint32_t root64(std::uint64_t v)
        {
            if ((v >> 32) == 0)
                return root32(static_cast<std::uint32_t>(v));
            const int c = __builtin_clzll(v) & ~1;
            const std::uint64_t m = v << c;
            const auto top = static_cast<std::uint32_t>(m >> 32);
            const std::uint32_t y = detail::inverseRoot(top, 2);
            // sqrt(m) = top y / 2^30, from the top word alone. (y may be a
            // hair above 1/sqrt(x), the sums' roundings: near the top that
            // makes 2^32, held to a word.)
            const std::uint64_t wide = (static_cast<std::uint64_t>(top) * y) >> 30;
            const auto s0 = static_cast<std::uint32_t>(wide > 0xFFFFFFFFull ? 0xFFFFFFFFull : wide);
            // What its square misses by (small: it fits 64 bits signed), over
            // twice it: miss y / 2^63, the miss cut to 32 bits first.
            const auto miss = static_cast<std::int64_t>(m - static_cast<std::uint64_t>(s0) * s0);
            const std::int64_t step = ((miss >> 14) * static_cast<std::int64_t>(y)) >> 49;
            std::int64_t s = static_cast<std::int64_t>(s0) + step;
            s = s > 0xFFFFFFFFll ? 0xFFFFFFFFll : (s < 0 ? 0 : s);
            std::uint32_t r = static_cast<std::uint32_t>(static_cast<std::uint64_t>(s) >> (c / 2));
            // (v - r^2 never overflows: r^2 is near v. And above 2 r is the
            // step up: (r + 1)^2 <= v, without making (r + 1)^2.)
            std::uint64_t r2 = static_cast<std::uint64_t>(r) * r;
            while (r2 > v)
            {
                --r;
                r2 -= 2 * static_cast<std::uint64_t>(r) + 1;
            }
            while (v - r2 > 2 * static_cast<std::uint64_t>(r))
            {
                r2 += 2 * static_cast<std::uint64_t>(r) + 1;
                ++r;
            }
            return r;
        }
    } // namespace softroot
} // namespace hldata
