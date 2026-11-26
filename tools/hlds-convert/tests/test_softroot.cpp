// The square root without the unit (HlSoftRoot.hpp): every answer is
// floor(sqrt(v)), held to that by integers alone (r^2 <= v < (r + 1)^2),
// over every value below 2^24, every square and its neighbours, the 64-bit
// edges and random values of every size; and the steps it is put right by at
// the end stay few. "--all" also walks every 32-bit value (some minutes).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "HlSoftRoot.hpp"

using namespace hldata;

namespace
{
    int g_failures = 0;
    long g_checked = 0;

    std::uint64_t g_seed = 0x9E3779B97F4A7C15ull;
    std::uint64_t next()
    {
        g_seed ^= g_seed << 13;
        g_seed ^= g_seed >> 7;
        g_seed ^= g_seed << 17;
        return g_seed;
    }

    /// Whether r is floor(sqrt(v)), without overflowing at the top.
    bool isRoot(std::uint64_t v, std::uint64_t r)
    {
        if (r > 0xFFFFFFFFull || r * r > v)
            return false;
        return r == 0xFFFFFFFFull || (r + 1) * (r + 1) > v;
    }

    void fail(const char *what, std::uint64_t v, std::uint32_t got)
    {
        if (++g_failures <= 10)
            std::printf("FAIL: %s(%llu) gave %lu\n", what, static_cast<unsigned long long>(v),
                        static_cast<unsigned long>(got));
    }

    void one32(std::uint32_t v)
    {
        ++g_checked;
        const std::uint32_t r = softroot::root32(v);
        if (!isRoot(v, r))
            fail("root32", v, r);
    }

    void one64(std::uint64_t v)
    {
        ++g_checked;
        const std::uint32_t r = softroot::root64(v);
        if (!isRoot(v, r))
            fail("root64", v, r);
    }

    /// How far the estimate before the last steps is from the root, in the
    /// worst case seen (32 and 64 bits): the same sums as the header's.
    int g_worst32 = 0, g_worst64 = 0;
    void steps32(std::uint32_t v)
    {
        if (v == 0)
            return;
        const int c = __builtin_clz(v) & ~1;
        const std::uint32_t m = v << c;
        const std::uint32_t y = softroot::detail::inverseRoot(m, 1);
        const auto s0 = static_cast<std::uint32_t>((static_cast<std::uint64_t>(m) * y) >> 46);
        const std::int64_t miss = static_cast<std::int64_t>(m) - static_cast<std::int64_t>(s0) * s0;
        const auto s = static_cast<std::uint32_t>(static_cast<std::int64_t>(s0) + ((miss * y) >> 47));
        const std::int64_t off = static_cast<std::int64_t>(s >> (c / 2)) - softroot::root32(v);
        const int a = static_cast<int>(off < 0 ? -off : off);
        g_worst32 = a > g_worst32 ? a : g_worst32;
    }
    void steps64(std::uint64_t v)
    {
        if ((v >> 32) == 0)
            return;
        const int c = __builtin_clzll(v) & ~1;
        const std::uint64_t m = v << c;
        const auto top = static_cast<std::uint32_t>(m >> 32);
        const std::uint32_t y = softroot::detail::inverseRoot(top, 2);
        const std::uint64_t wide = (static_cast<std::uint64_t>(top) * y) >> 30;
        const auto s0 = static_cast<std::uint32_t>(wide > 0xFFFFFFFFull ? 0xFFFFFFFFull : wide);
        const auto miss = static_cast<std::int64_t>(m - static_cast<std::uint64_t>(s0) * s0);
        std::int64_t s = static_cast<std::int64_t>(s0) + (((miss >> 14) * static_cast<std::int64_t>(y)) >> 49);
        s = s > 0xFFFFFFFFll ? 0xFFFFFFFFll : (s < 0 ? 0 : s);
        const std::int64_t off =
            static_cast<std::int64_t>(static_cast<std::uint64_t>(s) >> (c / 2)) - softroot::root64(v);
        const int a = static_cast<int>(off < 0 ? -off : off);
        g_worst64 = a > g_worst64 ? a : g_worst64;
    }
} // namespace

int main(int argc, char **argv)
{
    const bool all = argc > 1 && std::strcmp(argv[1], "--all") == 0;
    // Every value below 2^24.
    for (std::uint32_t v = 0; v < (1u << 24); ++v)
    {
        one32(v);
        steps32(v);
    }
    // Every square of 32 bits and its neighbours, as 32 and as 64 bits.
    for (std::uint64_t r = 0; r <= 0xFFFF; ++r)
    {
        for (const std::uint64_t v : {r * r, r * r + 1, r * r + 2 * r, r * r - 1})
        {
            if (v > 0xFFFFFFFFull)
                continue;
            one32(static_cast<std::uint32_t>(v));
            one64(v);
            steps32(static_cast<std::uint32_t>(v));
        }
    }
    // Squares of 64 bits and their neighbours: roots of every size.
    for (long k = 0; k < 4000000; ++k)
    {
        const int bits = 1 + static_cast<int>(next() % 32);
        const std::uint64_t r = (next() >> (64 - bits)) | (std::uint64_t(1) << (bits - 1));
        const std::uint64_t sq = r * r;
        one64(sq);
        one64(sq - 1);
        if (r < 0xFFFFFFFFull)
        {
            one64(sq + 2 * r); // (r + 1)^2 - 1
        }
        one64(sq + r);
        steps64(sq);
        steps64(sq - 1);
    }
    // The 64-bit edges.
    for (const std::uint64_t v : {~0ull, ~0ull - 1, 0xFFFFFFFE00000001ull, 0xFFFFFFFE00000000ull, 1ull << 63,
                                  (1ull << 63) - 1, 1ull << 62, (1ull << 62) - 1, 1ull << 32, (1ull << 32) - 1,
                                  1ull << 33, 3ull << 62})
    {
        one64(v);
        steps64(v);
    }
    // Random values of every size, 32 and 64 bits.
    for (long k = 0; k < 30000000; ++k)
    {
        const int bits = 1 + static_cast<int>(next() % 64);
        const std::uint64_t v = next() >> (64 - bits);
        one64(v);
        steps64(v);
        if (bits <= 32)
        {
            one32(static_cast<std::uint32_t>(v));
            steps32(static_cast<std::uint32_t>(v));
        }
    }
    if (all)
    {
        for (std::uint64_t v = 0; v <= 0xFFFFFFFFull; ++v)
            one32(static_cast<std::uint32_t>(v));
        std::printf("test_softroot: every 32-bit value walked\n");
    }
    std::printf("test_softroot: %ld roots; before the last steps, off by %d (32 bits) and %d (64 bits) at most\n",
                g_checked, g_worst32, g_worst64);
    if (g_worst32 > 2 || g_worst64 > 2)
    {
        ++g_failures;
        std::printf("FAIL: the estimate is more than 2 off\n");
    }
    if (g_failures == 0)
        std::printf("test_softroot: ok\n");
    return g_failures == 0 ? 0 : 1;
}
