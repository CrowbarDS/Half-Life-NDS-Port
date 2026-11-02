// A check, for builds that measure (HLDS_PROFILE): the console's divider
// and square-root unit (HwMath.s) against the same sums done a bit at a
// time. They must agree on every one, whichever mode the unit was left in.

#include <cstdint>
#include <cstdio>

#include <nds.h>

#include "Global.hpp"
#include "HlFixed.hpp"

namespace hlrt
{
    namespace
    {
        /// Unsigned divide, a bit at a time: no "/" in it.
        std::uint64_t slowDivide(std::uint64_t n, std::uint64_t d, std::uint64_t &rest)
        {
            std::uint64_t q = 0, r = 0;
            for (int bit = 63; bit >= 0; --bit)
            {
                r = (r << 1) | ((n >> bit) & 1);
                if (r >= d)
                {
                    r -= d;
                    q |= std::uint64_t(1) << bit;
                }
            }
            rest = r;
            return q;
        }

        std::uint32_t slowRoot(std::uint64_t value)
        {
            std::uint64_t result = 0, bit = std::uint64_t(1) << 62;
            while (bit > value)
                bit >>= 2;
            while (bit != 0)
            {
                if (value >= result + bit)
                {
                    value -= result + bit;
                    result = (result >> 1) + bit;
                }
                else
                {
                    result >>= 1;
                }
                bit >>= 2;
            }
            return static_cast<std::uint32_t>(result);
        }
    } // namespace

    /// How many of `count` sums the hardware got differently. Written to
    /// perf.txt as well.
    int hwMathCheck(int count)
    {
        std::uint64_t seed = 0x9E3779B97F4A7C15ull;
        auto next = [&seed]()
        {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            return seed;
        };
        int wrongSigned = 0, wrongUnsigned = 0, wrongBig = 0, wrongRoot = 0;
        for (int i = 0; i < count; ++i)
        {
            // Of every size: the game's are mostly a long number by a short.
            volatile std::int64_t n = static_cast<std::int64_t>(next()) >> (next() % 60);
            volatile std::int64_t d = static_cast<std::int64_t>(next()) >> (next() % 62);
            if (d == 0)
                d = 3;
            REG_DIVCNT = i % 3;
            const std::int64_t q = n / d;
            const std::int64_t r = n % d;
            std::uint64_t rest;
            const std::uint64_t un = n < 0 ? 0 - static_cast<std::uint64_t>(n) : static_cast<std::uint64_t>(n);
            const std::uint64_t ud = d < 0 ? 0 - static_cast<std::uint64_t>(d) : static_cast<std::uint64_t>(d);
            const std::uint64_t uq = slowDivide(un, ud, rest);
            const std::int64_t wantQ = (n < 0) != (d < 0) ? -static_cast<std::int64_t>(uq) : static_cast<std::int64_t>(uq);
            const std::int64_t wantR = n < 0 ? -static_cast<std::int64_t>(rest) : static_cast<std::int64_t>(rest);
            wrongSigned += q != wantQ || r != wantR;
            // Unsigned, the top bit set now and then (the library's way).
            REG_DIVCNT = (i + 1) % 3;
            volatile std::uint64_t a = next() >> (next() % 8), b = (next() >> (next() % 62)) | 1;
            const std::uint64_t uq2 = a / b, ur2 = a % b;
            const std::uint64_t want2 = slowDivide(a, b, rest);
            const bool big = (a | b) >> 63;
            (big ? wrongBig : wrongUnsigned) += uq2 != want2 || ur2 != rest;
            volatile std::uint64_t v = next() >> (next() % 64);
            REG_SQRTCNT = i % 2;
            if (hldata::isqrt64(v) != slowRoot(v))
            {
                if (wrongRoot < 4)
                    if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
                    {
                        const std::uint64_t value = v;
                        std::fprintf(log, "hwmath: root of %08lx%08lx: %lu, slow %lu\n",
                                     static_cast<unsigned long>(value >> 32), static_cast<unsigned long>(value),
                                     static_cast<unsigned long>(hldata::isqrt64(value)),
                                     static_cast<unsigned long>(slowRoot(value)));
                        std::fclose(log);
                    }
                ++wrongRoot;
            }
        }
        if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
        {
            std::fprintf(log, "hwmath: of %d each, differ from the slow way: %d signed divides, %d unsigned, "
                              "%d unsigned with the top bit set, %d roots\n",
                         count, wrongSigned, wrongUnsigned, wrongBig, wrongRoot);
            std::fclose(log);
        }
        return wrongSigned + wrongUnsigned + wrongBig + wrongRoot;
    }
} // namespace hlrt
