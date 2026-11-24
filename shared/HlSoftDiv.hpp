#pragma once

// A 64-bit divide without the divider: by multiplies and shifts alone, so it
// may run anywhere -- in an interrupt handler, in a thread switched away from
// mid-divide -- with nothing shared to save or hold off.
//
// The method and its reciprocal are Kuratius's (udivf32), used here with his
// agreement under this project's licence:
//
//   - the divisor d is brought to [2^30, 2^31) and multiplied by factors
//     (1 + 2^-i), i = 1..8, each taken while the product stays under 2^31;
//     the same factors multiplied together from 2^31 make an estimate of its
//     reciprocal, good to about 2^-8;
//   - one Newton step is hidden in the last multiply (2^32 - d', with d' the
//     product, is 2^31 times one plus the estimate's error), and one more is
//     made: about 30 bits;
//   - the quotient is the numerator times the reciprocal, shifted, and put
//     right from its remainder: never more than a few steps off.
//
// What it answers: a numerator of up to 64 bits by a divisor of up to 32,
// where the quotient fits 32 bits -- the game's divides (a trace's fraction,
// (t << 30) / d; a vector made unit, (v << 12) / length; a clip's place in
// its span). Anything else -- a divisor of more than 32 bits, a quotient of
// more, a divisor of 0 -- it does not answer, and says so: the caller divides
// the other way (source/HwMath.s).
//
// The quotient and remainder are __aeabi_ldivmod's exactly: the quotient
// rounded toward 0, the remainder with the numerator's sign. The host's
// tests check them against the host's own divide (test_softdiv), and
// tools/hwtest/softdiv times them on the console against the hardware.
//
// std-only: the converter's tests run the same code the console does.

#include <cstdint>

namespace hldata
{
    namespace softdiv
    {
        /// An estimate of 2^62 / (d << clz(d)), never above it, within about
        /// 2^-30; `clz` is d's count of leading zeros. d must not be 0.
        inline std::uint32_t reciprocal(std::uint32_t d, int &clz)
        {
            const int c = __builtin_clz(d);
            const std::uint32_t b0 = d << c; // its top bit set
            // In [2^30, 2^31): the factors are taken while it stays below.
            std::uint32_t b = c == 0 ? d >> 1 : d << (c - 1);
            std::uint32_t r2 = 1u << 31;
#pragma GCC unroll 8
            for (int i = 1; i < 9; ++i)
            {
                const std::uint32_t t = b + (b >> i);
                if (t < (1u << 31))
                {
                    b = t;
                    r2 += r2 >> i;
                }
            }
            // The hidden Newton step: 2^32 - b is 2^31 times (1 + e), and r2
            // is 2^62 / d' times (1 - e).
            std::uint32_t r = static_cast<std::uint32_t>((static_cast<std::uint64_t>(0u - b) * r2) >> 32);
            // And one more: r (2 - r b0), in 2^62 / b0's scale.
            r <<= 1;
            const std::uint64_t t = 0 - static_cast<std::uint64_t>(r) * b0;
            const std::uint64_t hi = static_cast<std::uint64_t>(r) * static_cast<std::uint32_t>(t >> 32);
            const std::uint64_t lo = static_cast<std::uint64_t>(r) * static_cast<std::uint32_t>(t);
            clz = c;
            return static_cast<std::uint32_t>((hi + (lo >> 32)) >> 32);
        }

        /// n / d and n % d, for d not 0 and n < d * 2^32 (the quotient fits
        /// 32 bits). (`steps`, for the tests: how many the estimate was put
        /// right by.)
        inline std::uint32_t divmod64by32(std::uint64_t n, std::uint32_t d, std::uint32_t &rem, int *steps = nullptr)
        {
            int c;
            const std::uint32_t r = reciprocal(d, c);
            // n * r, 96 bits: hi, and the low word of lo.
            const std::uint64_t lo = static_cast<std::uint64_t>(static_cast<std::uint32_t>(n)) * r;
            const std::uint64_t hi = static_cast<std::uint64_t>(static_cast<std::uint32_t>(n >> 32)) * r + (lo >> 32);
            // ... shifted down by 62 - c, which is 31 to 62.
            const int s = 62 - c;
            std::uint64_t q = s >= 32 ? hi >> (s - 32)
                                      : (hi << 1) | (static_cast<std::uint32_t>(lo) >> 31);
            // Put right from the remainder (the estimate is a few steps off at
            // most: test_softdiv finds how many).
            std::uint64_t made = q * d;
            int put = 0;
            while (made > n)
            {
                --q;
                made -= d;
                ++put;
            }
            std::uint64_t left = n - made;
            while (left >= d)
            {
                ++q;
                left -= d;
                ++put;
            }
            if (steps != nullptr)
                *steps = put;
            rem = static_cast<std::uint32_t>(left);
            return static_cast<std::uint32_t>(q);
        }

        /// __aeabi_ldivmod's quotient and remainder of n by d, where they
        /// can be had this way (|d| below 2^32 and not 0, the quotient's
        /// size within 32 bits); false, and nothing written, where not.
        inline bool divmod64(std::int64_t n, std::int64_t d, std::int64_t &q, std::int64_t &r)
        {
            const std::uint64_t un = n < 0 ? 0 - static_cast<std::uint64_t>(n) : static_cast<std::uint64_t>(n);
            const std::uint64_t ud = d < 0 ? 0 - static_cast<std::uint64_t>(d) : static_cast<std::uint64_t>(d);
            if (ud == 0 || (ud >> 32) != 0 || (un >> 32) >= ud)
                return false;
            std::uint32_t rem;
            const std::uint32_t uq = divmod64by32(un, static_cast<std::uint32_t>(ud), rem);
            q = (n < 0) != (d < 0) ? -static_cast<std::int64_t>(uq) : static_cast<std::int64_t>(uq);
            r = n < 0 ? -static_cast<std::int64_t>(rem) : static_cast<std::int64_t>(rem);
            return true;
        }

        /// __aeabi_uldivmod's, likewise.
        inline bool udivmod64(std::uint64_t n, std::uint64_t d, std::uint64_t &q, std::uint64_t &r)
        {
            if (d == 0 || (d >> 32) != 0 || (n >> 32) >= d)
                return false;
            std::uint32_t rem;
            q = divmod64by32(n, static_cast<std::uint32_t>(d), rem);
            r = rem;
            return true;
        }
    } // namespace softdiv
} // namespace hldata
