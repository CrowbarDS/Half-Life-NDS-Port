// The ARM9's 16-bit multiplies (shared/HlDsp.hpp) as the host computes them,
// against the instructions' definition worked out bit by bit; and the game's
// uses of them against the 64-bit sums they took the place of.
//
// The console's own answers are held against these same formulas by
// tools/hwtest/dspmath.

#include <cstdint>
#include <cstdio>
#include <random>

#include "HlAnim.hpp"
#include "HlDsp.hpp"
#include "HlPlayerMove.hpp"

using namespace hldata;

namespace
{
    int failures = 0;
    void check(bool ok, const char *what)
    {
        if (!ok)
        {
            ++failures;
            std::printf("FAIL: %s\n", what);
        }
    }

    // The definition: a half is bits 15..0 or 31..16, sign-extended; SMULW
    // keeps bits 47..16 of the 48-bit product; an accumulate wraps at 32 bits.
    std::int64_t half(std::uint32_t x, bool topHalf)
    {
        const std::uint32_t h = (topHalf ? x >> 16 : x) & 0xFFFFu;
        return h >= 0x8000u ? static_cast<std::int64_t>(h) - 0x10000 : static_cast<std::int64_t>(h);
    }
    std::int32_t low32(std::int64_t x)
    {
        const std::uint32_t bits = static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) & 0xFFFFFFFFu);
        return bits >= 0x80000000u ? static_cast<std::int32_t>(static_cast<std::int64_t>(bits) - 0x100000000ll)
                                   : static_cast<std::int32_t>(bits);
    }
    std::int32_t word(std::int64_t product48) // bits 47..16
    {
        const std::uint64_t bits = (static_cast<std::uint64_t>(product48) >> 16) & 0xFFFFFFFFu;
        return low32(static_cast<std::int64_t>(bits));
    }

    int wrongOn(std::int32_t a, std::int32_t b, std::int32_t c)
    {
        const std::uint32_t x = static_cast<std::uint32_t>(a), y = static_cast<std::uint32_t>(b);
        const std::int64_t acc = c;
        const std::int64_t want[12] = {
            half(x, false) * half(y, false),
            half(x, false) * half(y, true),
            half(x, true) * half(y, false),
            half(x, true) * half(y, true),
            word(static_cast<std::int64_t>(a) * half(y, false)),
            word(static_cast<std::int64_t>(a) * half(y, true)),
            low32(acc + half(x, false) * half(y, false)),
            low32(acc + half(x, false) * half(y, true)),
            low32(acc + half(x, true) * half(y, false)),
            low32(acc + half(x, true) * half(y, true)),
            low32(acc + word(static_cast<std::int64_t>(a) * half(y, false))),
            low32(acc + word(static_cast<std::int64_t>(a) * half(y, true))),
        };
        const std::int32_t got[12] = {smulbb(a, b),    smulbt(a, b),    smultb(a, b),    smultt(a, b),
                                      smulwb(a, b),    smulwt(a, b),    smlabb(a, b, c), smlabt(a, b, c),
                                      smlatb(a, b, c), smlatt(a, b, c), smlawb(a, b, c), smlawt(a, b, c)};
        int wrong = 0;
        for (int op = 0; op < 12; ++op)
            wrong += got[op] != want[op];
        return wrong;
    }

    void testDefinition()
    {
        const std::int32_t edges[] = {0, 1, -1, 0x7FFF, -0x8000, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF,
                                      static_cast<std::int32_t>(0x80000000u), 0x7FFF7FFF,
                                      static_cast<std::int32_t>(0x80008000u), 0x12345678, -0x12345678};
        int wrong = 0;
        for (std::int32_t a : edges)
            for (std::int32_t b : edges)
                for (std::int32_t c : edges)
                    wrong += wrongOn(a, b, c);
        std::mt19937 random(7);
        for (int n = 0; n < 200000; ++n)
            wrong += wrongOn(static_cast<std::int32_t>(random()), static_cast<std::int32_t>(random()),
                             static_cast<std::int32_t>(random()));
        check(wrong == 0, "the twelve multiplies as the instructions define them");

        static_assert(smultt(0x30000, 0x50000) == 15 && smulwb(0x10000, 0x8000) == -0x8000,
                      "worked out by the compiler too");
    }

    void testUses()
    {
        std::mt19937 random(11);
        int wrong = 0;
        // A step under half a second: SMULWB, the same as through 64 bits.
        for (int n = 0; n < 200000; ++n)
        {
            const fx value = static_cast<fx>(random());
            const std::int32_t dt = static_cast<std::int32_t>(random() % 65536) - 32768;
            wrong += detail::scaleTime(value, dt) != static_cast<fx>((static_cast<std::int64_t>(value) * dt) >> kTimeShift);
        }
        for (const std::int32_t dt : {32767, 32768, -32768, -32769, 65536, 2185})
            for (const fx value : {0x7FFFFFFF, static_cast<fx>(0x80000000u), 12345678, -1})
                wrong += detail::scaleTime(value, dt) !=
                         static_cast<fx>((static_cast<std::int64_t>(value) * dt) >> kTimeShift);
        check(wrong == 0, "scaleTime as through 64 bits");

        // Rotations: entries at most 16384 (a little more, rounded).
        wrong = 0;
        for (int n = 0; n < 200000; ++n)
        {
            std::int32_t row[3], column[9];
            for (std::int32_t &v : row)
                v = static_cast<std::int32_t>(random() % 32801) - 16400;
            for (std::int32_t &v : column)
                v = static_cast<std::int32_t>(random() % 32801) - 16400;
            const std::int32_t wide = static_cast<std::int32_t>(
                (static_cast<std::int64_t>(row[0]) * column[0] + static_cast<std::int64_t>(row[1]) * column[3] +
                 static_cast<std::int64_t>(row[2]) * column[6]) >>
                kBoneRotShift);
            wrong += detail::rotationDot(row, column) != wide;
        }
        // ... and a quaternion's matrix, as with the 64-bit products.
        for (int n = 0; n < 200000; ++n)
        {
            std::int32_t q[4], m[9];
            for (std::int32_t &v : q)
                v = static_cast<std::int32_t>(random() % 32769) - 16384;
            detail::quatToMatrix(q, m);
            const auto mul = [](std::int32_t a, std::int32_t b)
            { return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 13); };
            const std::int32_t xx = mul(q[0], q[0]), yy = mul(q[1], q[1]), zz = mul(q[2], q[2]);
            const std::int32_t xy = mul(q[0], q[1]), wz = mul(q[3], q[2]);
            wrong += m[0] != (1 << 14) - yy - zz || m[1] != xy - wz || m[8] != (1 << 14) - xx - yy;
        }
        check(wrong == 0, "bone rotations as through 64 bits");
    }
} // namespace

int main()
{
    testDefinition();
    testUses();
    if (failures == 0)
        std::printf("test_dsp: ok\n");
    return failures == 0 ? 0 : 1;
}
