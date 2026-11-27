#pragma once

// A 64-bit divide without the divider, by multiplies alone: it may run
// anywhere -- in an interrupt handler, in a thread switched away from
// mid-divide -- with nothing shared to save or hold off.
//
// Kuratius's method (udivf32), used with his agreement under this project's
// licence, its reciprocal now begun from a table of seeds (reciprocalOf).
// The console runs it as source/HwMath.s; this is the same steps,
// word for word, so that test_softdiv's divides on the host hold the
// console's arithmetic, truncations included.
//
// It answers a numerator of up to 64 bits by a divisor of up to 32 bits, not
// 0, where the quotient fits 32 bits. The quotient and remainder are
// __aeabi_ldivmod's exactly: the quotient rounded toward 0, the remainder
// with the numerator's sign.
//
// It comes in two halves, the divisor's reciprocal (reciprocalOf) and the
// divide by it (divideBy), so that one divisor's can be kept for several
// numbers. For two numbers that gains little over two divides by hand
// (tools/hwtest/softdiv, its walk step page): the game does not use it yet.
//
// std-only, but for HlDsp.hpp's multiplies: the converter's tests run the
// same code the console does.

#include <cstdint>

#include "HlDsp.hpp"

// On the console the reciprocal and the divide by it are ARM code whoever
// calls them (Thumb has no long multiply, and GCC would make them Thumb in a
// Thumb caller), one copy each for the whole program.
#if defined(__arm__) && defined(HL_HW_MATH)
#define HL_SOFTDIV_ONCE __attribute__((noinline, target("arm")))
#else
#define HL_SOFTDIV_ONCE
#endif

namespace hldata
{
    namespace softdiv
    {
        /// The high word of a * b (UMULL's).
        inline std::uint32_t mulHigh(std::uint32_t a, std::uint32_t b)
        {
            return static_cast<std::uint32_t>((static_cast<std::uint64_t>(a) * b) >> 32);
        }

        /// What dividing by d takes: an estimate of 2^62 / (d << clz(d)),
        /// never above it, and the shift that brings n times it to n / d.
        struct Reciprocal
        {
            std::uint32_t d;
            std::uint32_t r;
            int shift; ///< 30 - clz(d): 0..30
        };

        namespace detail
        {
            /// The seeds of reciprocalOf: for each of 128 equal steps of
            /// x = b0 / 2^32 over [1/2, 1), 1 / x at the step's middle, Q14
            /// (16384..32768: a signed half-word, as SMULWB takes it),
            /// rounded to the nearest -- in integers, 2^23 / (257 + 2i).
            struct Seeds
            {
                std::int16_t at[128];
            };
            constexpr Seeds makeSeeds()
            {
                Seeds s{};
                for (int i = 0; i < 128; ++i)
                {
                    const std::int32_t middle = 257 + 2 * i; // (x at the middle, in 512ths)
                    s.at[i] = static_cast<std::int16_t>((2 * (std::int32_t(1) << 23) + middle) / (2 * middle));
                }
                return s;
            }
            inline constexpr Seeds kSeeds = makeSeeds();
        } // namespace detail

        /// d's reciprocal, 2^62 / b0 (b0 = d << clz(d), its top bit set),
        /// never above it and a unit below at most (test_softdiv, for every
        /// divisor). Begun from a table: the seed y0 of 1 / x (x = b0 / 2^32),
        /// good to 2^-8; a Newton step in two SMULWBs, y0 (1 + (1 - x y0)),
        /// good to 2^-16; and one more in long multiplies. (The way the ARM9E
        /// books take a reciprocal; these numbers are this project's. It
        /// began from eight steps of factors before: a sixth slower.) d must
        /// not be 0.
        inline HL_SOFTDIV_ONCE Reciprocal reciprocalOf(std::uint32_t d)
        {
            const int c = __builtin_clz(d);
            // By 1: n (2^32 - 1) >> 32 is n - 1, put right by one step. (The
            // divide by hand takes the numerator as it is.)
            if (c == 31)
                return {d, 0xFFFFFFFFu, 0};
            const std::uint32_t b0 = d << c;
            // (The step: b0's seven bits under its top one.)
            const std::int32_t y0 = detail::kSeeds.at[(b0 >> 24) - 128]; // Q14
            // x y0, Q29, x to 31 bits; then 1 - x y0, a few thousandths at
            // most either way (the seed is the middle's: above or below).
            const std::int32_t e = (std::int32_t(1) << 29) - smulwb(static_cast<std::int32_t>(b0 >> 1), y0);
            // y0 (1 + e): Q28, below 2^29.
            const std::int32_t y1 = (y0 << 14) + (smulwb(e, y0) << 1);
            // 2^63 / b0: 1 / x in Q31.
            std::uint32_t r = static_cast<std::uint32_t>(y1) << 3;
            // r (2 - r b0): 2^62 / b0, about 30 bits.
            const std::uint64_t t = 0 - static_cast<std::uint64_t>(r) * b0;
            r = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(r) * static_cast<std::uint32_t>(t >> 32) +
                 mulHigh(r, static_cast<std::uint32_t>(t))) >> 32);
            return {d, r, 30 - c};
        }

        /// n / d from the reciprocal alone: never above it, and a few below
        /// at most. n < d * 2^32.
        inline std::uint32_t estimate(std::uint64_t n, const Reciprocal &rec)
        {
            // (n >> 32 is below d: a 32-bit number, so one UMULL and one UMLAL.)
            const std::uint64_t top = static_cast<std::uint64_t>(static_cast<std::uint32_t>(n >> 32)) * rec.r +
                                      mulHigh(static_cast<std::uint32_t>(n), rec.r);
            // top >> shift, in words: the shift is 30 at most.
            const auto low = static_cast<std::uint32_t>(top), high = static_cast<std::uint32_t>(top >> 32);
            return (low >> rec.shift) | ((high << 1) << (31 - rec.shift));
        }

        /// n / d and n % d. n < d * 2^32.
        inline std::uint32_t divide(std::uint64_t n, const Reciprocal &rec, std::uint32_t &rem)
        {
            std::uint32_t q = estimate(n, rec);
            std::uint64_t left = n - static_cast<std::uint64_t>(q) * rec.d;
            while (left >= rec.d)
            {
                ++q;
                left -= rec.d;
            }
            rem = static_cast<std::uint32_t>(left);
            return q;
        }

        /// How many steps the shift-and-subtract divide takes for n / d: one
        /// for each bit the quotient may have, from the two numbers' sizes
        /// (n < d << k), 1 at least and 32 at most (n < d * 2^32 already).
        /// d not 0.
        inline int quotientBits(std::uint64_t n, std::uint32_t d)
        {
            const int nBits = n == 0 ? 0 : 64 - __builtin_clzll(n);
            const int k = nBits - (32 - __builtin_clz(d)) + 1;
            return k < 1 ? 1 : (k > 32 ? 32 : k);
        }

        /// n / d and n % d a bit of quotient at a step, k steps (k from
        /// quotientBits): for a short quotient, quicker than the reciprocal.
        /// d below 2^31, so that twice what is left never overflows a word.
        /// The steps are source/HwMath.s's three instructions each, word for
        /// word: what is left (hi) doubled with the numerator's next bit and
        /// d taken off by adding -d, the carry saying whether it went (the
        /// quotient's bit), and given back if not.
        inline std::uint32_t shiftDivide(std::uint64_t n, std::uint32_t d, int k, std::uint32_t &rem)
        {
            // The numerator's top part, below d, and its last k bits at the
            // top of a word. (A shift by 32 makes 0, as ARM's does.)
            const auto nLow = static_cast<std::uint32_t>(n), nHigh = static_cast<std::uint32_t>(n >> 32);
            std::uint32_t hi = (k == 32 ? 0u : nLow >> k) | (nHigh << (32 - k));
            std::uint32_t lo = k == 32 ? nLow : nLow << (32 - k);
            const std::uint32_t minusD = 0u - d;
            bool carry = false; // (made 0 on the way in)
            for (int i = 0; i < k; ++i)
            {
                // adcs lo, lo, lo: the last bit of quotient in, the next of
                // the numerator out.
                const bool bit = (lo >> 31) != 0;
                lo = (lo << 1) | (carry ? 1u : 0u);
                // adcs hi, minusD, hi, lsl #1: 2 hi + bit - d, carried if not
                // below 0.
                const std::uint64_t sum = static_cast<std::uint64_t>(minusD) + (static_cast<std::uint64_t>(hi) << 1) +
                                          (bit ? 1u : 0u);
                hi = static_cast<std::uint32_t>(sum);
                carry = (sum >> 32) != 0;
                // subcc hi, hi, minusD: given back.
                if (!carry)
                    hi -= minusD;
            }
            // adc lo, lo, lo: the last bit. (The numerator's bits have all
            // gone off the top, and the 0 carried in at first is above the
            // quotient's k bits, with only 0s above it: lo is the quotient.)
            lo = (lo << 1) | (carry ? 1u : 0u);
            rem = hi;
            return lo;
        }

        /// n / d rounded toward 0, as `/` gives it, d the reciprocal's. (A
        /// quotient of more than 32 bits is left to `/`.)
        inline HL_SOFTDIV_ONCE std::int64_t divideBy(std::int64_t n, const Reciprocal &rec)
        {
            const std::uint64_t un = n < 0 ? 0 - static_cast<std::uint64_t>(n) : static_cast<std::uint64_t>(n);
            if ((un >> 32) >= rec.d)
                return n / static_cast<std::int64_t>(rec.d);
            std::uint32_t rem;
            const std::uint32_t q = divide(un, rec, rem);
            return n < 0 ? -static_cast<std::int64_t>(q) : static_cast<std::int64_t>(q);
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
            const std::uint32_t uq = divide(un, reciprocalOf(static_cast<std::uint32_t>(ud)), rem);
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
            q = divide(n, reciprocalOf(static_cast<std::uint32_t>(d)), rem);
            r = rem;
            return true;
        }
    } // namespace softdiv
} // namespace hldata
