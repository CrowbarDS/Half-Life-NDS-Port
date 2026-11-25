// The divide without the divider (HlSoftDiv.hpp, Kuratius's method): every
// quotient and remainder it gives is the host's own, bit for bit; it answers
// every divide it should (the quotient within 32 bits, the divisor within 32
// and not 0) and none it should not; its estimate is never above the
// quotient (source/HwMath.s's step down never runs) and a few steps below at
// most; and a reciprocal kept for several divides (divideBy) gives `/`'s.
//
// Integers only: the host's own 64-bit divide is the reference.

#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "HlSoftDiv.hpp"

using namespace hldata;

namespace
{
    int g_failures = 0;
    long g_checked = 0, g_answered = 0;
    int g_worstSteps = 0;
    long g_steps[8] = {};
    bool g_shapes = false; // counting the game's shapes apart
    long g_shapeSteps[8] = {};

    void check(bool ok, const char *what)
    {
        if (!ok)
        {
            ++g_failures;
            std::printf("FAIL: %s\n", what);
        }
    }

    std::uint64_t g_seed = 0x9E3779B97F4A7C15ull;
    std::uint64_t next()
    {
        g_seed ^= g_seed << 13;
        g_seed ^= g_seed >> 7;
        g_seed ^= g_seed << 17;
        return g_seed;
    }

    /// One signed divide, against the host's. Prints the first few wrong.
    void one(std::int64_t n, std::int64_t d)
    {
        ++g_checked;
        std::int64_t q = 0, r = 0;
        const bool answered = softdiv::divmod64(n, d, q, r);
        const std::uint64_t un = n < 0 ? 0 - static_cast<std::uint64_t>(n) : static_cast<std::uint64_t>(n);
        const std::uint64_t ud = d < 0 ? 0 - static_cast<std::uint64_t>(d) : static_cast<std::uint64_t>(d);
        const bool should = ud != 0 && (ud >> 32) == 0 && (un >> 32) < ud;
        static int told = 0;
        if (answered != should)
        {
            ++g_failures;
            if (told++ < 10)
                std::printf("FAIL: %lld / %lld %s\n", static_cast<long long>(n), static_cast<long long>(d),
                            answered ? "answered, and should not have been" : "not answered");
            return;
        }
        if (!answered)
            return;
        ++g_answered;
        if (q != n / d || r != n % d)
        {
            ++g_failures;
            if (told++ < 10)
                std::printf("FAIL: %lld / %lld gave %lld rem %lld, not %lld rem %lld\n", static_cast<long long>(n),
                            static_cast<long long>(d), static_cast<long long>(q), static_cast<long long>(r),
                            static_cast<long long>(n / d), static_cast<long long>(n % d));
        }
        const softdiv::Reciprocal rec = softdiv::reciprocalOf(static_cast<std::uint32_t>(ud));
        const std::uint64_t want = un / ud, guess = softdiv::estimate(un, rec);
        if (guess > want)
        {
            ++g_failures;
            if (told++ < 10)
                std::printf("FAIL: %lld / %lld estimated above the quotient\n", static_cast<long long>(n),
                            static_cast<long long>(d));
            return;
        }
        const int steps = static_cast<int>(want - guess);
        if (steps > g_worstSteps)
            g_worstSteps = steps;
        ++g_steps[steps < 7 ? steps : 7];
        if (g_shapes)
            ++g_shapeSteps[steps < 7 ? steps : 7];
        if (d > 0 && softdiv::divideBy(n, rec) != n / d)
        {
            ++g_failures;
            if (told++ < 10)
                std::printf("FAIL: divideBy %lld / %lld\n", static_cast<long long>(n), static_cast<long long>(d));
        }
    }

    /// Unsigned, likewise.
    void oneUnsigned(std::uint64_t n, std::uint64_t d)
    {
        std::uint64_t q = 0, r = 0;
        const bool answered = softdiv::udivmod64(n, d, q, r);
        const bool should = d != 0 && (d >> 32) == 0 && (n >> 32) < d;
        check(answered == should, "unsigned: answered exactly where it should be");
        if (answered && should)
            check(q == n / d && r == n % d, "unsigned: the host's quotient and remainder");
    }

    /// A number of `bits` bits at most (0..64), either sign.
    std::int64_t ofBits(int bits)
    {
        if (bits <= 0)
            return 0;
        const std::uint64_t v = bits >= 64 ? next() : next() & ((std::uint64_t(1) << bits) - 1);
        const std::int64_t s = static_cast<std::int64_t>(v >> 1);
        return (next() & 1) ? -s : s;
    }

    void testRandom()
    {
        // Every width of numerator by every width of divisor.
        for (long k = 0; k < 40000000; ++k)
        {
            const int nb = static_cast<int>(next() % 65), db = 1 + static_cast<int>(next() % 34);
            std::int64_t d = ofBits(db);
            if (d == 0)
                d = 1;
            one(ofBits(nb), d);
        }
        for (long k = 0; k < 4000000; ++k)
        {
            const int nb = static_cast<int>(next() % 65), db = 1 + static_cast<int>(next() % 34);
            oneUnsigned(next() >> (64 - nb), (next() >> (64 - db)) | 1);
        }
    }

    void testEdges()
    {
        // Every divisor up to 2^16, by the numerators at its edges.
        for (std::int64_t d = 1; d <= 65536; ++d)
        {
            const std::int64_t most = (d << 32) - 1; // the largest the quick way takes
            const std::int64_t ns[] = {0, 1, d - 1, d, d + 1, 2 * d - 1, most, most - d + 1, most - 1,
                                       std::int64_t(4095) << 12, std::int64_t(1) << 30, (d - 1) << 30,
                                       (d << 31) + d / 2};
            for (const std::int64_t n : ns)
            {
                one(n, d);
                one(-n, d);
                one(n, -d);
                one(-n, -d);
            }
        }
        // Powers of two and their neighbours, by numerators at the edges.
        for (int k = 0; k < 32; ++k)
            for (const std::int64_t d : {std::int64_t(1) << k, (std::int64_t(1) << k) + 1, (std::int64_t(1) << k) - 1,
                                         (std::int64_t(1) << (k + 1)) - 1})
            {
                if (d <= 0 || d > 0xFFFFFFFFll)
                    continue;
                const std::int64_t most = static_cast<std::int64_t>((static_cast<std::uint64_t>(d) << 32) - 1);
                for (const std::int64_t n : {std::int64_t(0), std::int64_t(1), d, most, most - 1, most / 3, most / 2 + 1})
                {
                    one(n, d);
                    one(-n, -d);
                }
            }
        // The largest divisors.
        for (std::int64_t d = 0xFFFFFFFFll; d > 0xFFFFFFFFll - 4096; --d)
            for (const std::int64_t n : {std::int64_t(0x7FFFFFFFFFFFFFFFll), std::int64_t(1) << 62, d * 12345 + 17})
                one(n, d);
        // What it must refuse: 0, a divisor of more than 32 bits, a quotient of
        // more, the one that overflows.
        one(5, 0);
        one(1ll << 40, 1ll << 32);
        one(1ll << 40, 255);
        one(INT64_MIN, -1);
        one(INT64_MIN, 1);
        one(INT64_MIN, 0x7FFFFFFF);
        one(INT64_MAX, 0xFFFFFFFFll);
    }

    /// The game's divides: a trace's fraction ((t << 30) / d), a vector made
    /// unit ((v << 12) / length), a clip's place in its span.
    void testShapes()
    {
        g_shapes = true;
        for (long k = 0; k < 4000000; ++k)
        {
            const std::int64_t t = ofBits(20), d = ofBits(22) | 1;
            if ((t < 0 ? -t : t) <= (d < 0 ? -d : d))
                one(t << 30, d);
            const std::int64_t v = ofBits(24), length = (ofBits(26) & 0x3FFFFFF) | 1;
            one(v << 12, length);
            const std::uint64_t span = (next() & 0xFFFFFF) | 1, done = next() % (span + 1);
            oneUnsigned(done << 12, span);
        }
        g_shapes = false;
    }
} // namespace

int main()
{
    testEdges();
    testShapes();
    testRandom();
    std::printf("test_softdiv: %ld divides, %ld answered the quick way; put right by %d steps at most\n", g_checked,
                g_answered, g_worstSteps);
    std::printf("test_softdiv: steps 0..5: %ld %ld %ld %ld %ld %ld\n", g_steps[0], g_steps[1], g_steps[2], g_steps[3],
                g_steps[4], g_steps[5]);
    std::printf("test_softdiv: the game's shapes, steps 0..2: %ld %ld %ld\n", g_shapeSteps[0], g_shapeSteps[1],
                g_shapeSteps[2]);
    // (The reciprocal is good to about 2^-30: a quotient near 2^32 is about
    // four steps off from it, and one more from the truncation.)
    check(g_worstSteps <= 6, "the estimate is never more than 6 steps off");
    if (g_failures == 0)
        std::printf("test_softdiv: ok\n");
    return g_failures == 0 ? 0 : 1;
}
