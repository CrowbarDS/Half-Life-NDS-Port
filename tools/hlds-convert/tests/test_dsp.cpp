// The ARM9's 16-bit multiplies (shared/HlDsp.hpp) as the host computes them,
// against the instructions' definition worked out bit by bit; and the game's
// uses of them against the 64-bit sums they took the place of.
//
// The console's own answers are held against these same formulas by
// tools/hwtest/dspmath.

#include <array>
#include <cmath>
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
    std::int32_t wideDot(const std::int32_t *a, int sa, const std::int32_t *b, int sb, int shift)
    {
        return static_cast<std::int32_t>((static_cast<std::int64_t>(a[0]) * b[0] +
                                          static_cast<std::int64_t>(a[sa]) * b[sb] +
                                          static_cast<std::int64_t>(a[2 * sa]) * b[2 * sb]) >>
                                         shift);
    }

    /// axisTurn as it was, through 64 bits.
    void axisTurnBefore(const std::int16_t *axis, std::int32_t angle, std::int32_t *m)
    {
        std::int32_t sine, cosine;
        fxSinCosFine(static_cast<std::uint16_t>(angle), sine, cosine);
        const std::int32_t s = sine >> 16, c = cosine >> 16;
        const std::int32_t k = (1 << 14) - c;
        const auto mul = [](std::int32_t a, std::int32_t b)
        { return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 14); };
        const std::int32_t x = axis[0], y = axis[1], z = axis[2];
        const std::int32_t want[9] = {c + mul(mul(x, x), k),          mul(mul(x, y), k) - mul(z, s),
                                      mul(mul(x, z), k) + mul(y, s),  mul(mul(y, x), k) + mul(z, s),
                                      c + mul(mul(y, y), k),          mul(mul(y, z), k) - mul(x, s),
                                      mul(mul(z, x), k) - mul(y, s),  mul(mul(z, y), k) + mul(x, s),
                                      c + mul(mul(z, z), k)};
        for (int i = 0; i < 9; ++i)
            m[i] = want[i];
    }

    std::int32_t rotationEntry(std::mt19937 &random) { return static_cast<std::int32_t>(random() % 32801) - 16400; }

    void testBones()
    {
        std::mt19937 random(23);
        int wrong = 0;
        // Q12 by Q12, both within 16 bits: fxMul's answer.
        for (int n = 0; n < 200000; ++n)
        {
            const std::int32_t a = static_cast<std::int32_t>(random() % 65536) - 32768;
            const std::int32_t b = static_cast<std::int32_t>(random() % 65536) - 32768;
            wrong += fxMulShort(a, b) != fxMul(a, b);
        }
        for (const std::int32_t a : {-32768, -4096, -1, 0, 1, 4096, 32767})
            for (const std::int32_t b : {-32768, -4096, -1, 0, 1, 4096, 32767})
                wrong += fxMulShort(a, b) != fxMul(a, b);
        check(wrong == 0, "fxMulShort as fxMul");

        // A bone's place: a rotation's row times a 16-bit key << 6, >> 14.
        wrong = 0;
        for (int n = 0; n < 300000; ++n)
        {
            std::int32_t row[3], key[3], pos[3];
            for (int i = 0; i < 3; ++i)
            {
                row[i] = rotationEntry(random);
                key[i] = static_cast<std::int32_t>(random() % 65536) - 32768;
                pos[i] = key[i] * 64;
            }
            wrong += (dotShort(row, 1, key, 1) >> 8) != wideDot(row, 1, pos, 1, kBoneRotShift);
            // ... and an attachment's 16-bit point << 8.
            const std::int32_t local[3] = {key[0] * 256, key[1] * 256, key[2] * 256};
            wrong += (dotShort(row, 1, key, 1) >> 6) != wideDot(row, 1, local, 1, kBoneRotShift);
        }
        check(wrong == 0, "a bone's place and an attachment's, as through 64 bits");

        // A quaternion's four squares, parts at most 16400.
        wrong = 0;
        for (int n = 0; n < 200000; ++n)
        {
            std::int32_t q[4];
            std::int64_t want = 0;
            for (std::int32_t &v : q)
            {
                v = rotationEntry(random);
                want += static_cast<std::int64_t>(v) * v;
            }
            wrong += detail::squaresShort(q) != want;
        }
        check(wrong == 0, "a quaternion's length squared");

        // The turn about a unit axis, every 64th angle.
        wrong = 0;
        for (int n = 0; n < 2000; ++n)
        {
            // A unit axis in Q14, as the converter writes one.
            const double x = static_cast<double>(random()) / random.max() * 2 - 1;
            const double y = static_cast<double>(random()) / random.max() * 2 - 1;
            const double z = static_cast<double>(random()) / random.max() * 2 - 1;
            const double length = std::sqrt(x * x + y * y + z * z);
            if (length < 1e-3)
                continue;
            const std::int16_t axis[3] = {static_cast<std::int16_t>(std::lround(x / length * 16384)),
                                          static_cast<std::int16_t>(std::lround(y / length * 16384)),
                                          static_cast<std::int16_t>(std::lround(z / length * 16384))};
            for (std::int32_t angle = 0; angle < 65536; angle += 64)
            {
                std::int32_t got[9], want[9];
                detail::axisTurn(axis, angle, got);
                axisTurnBefore(axis, angle, want);
                for (int i = 0; i < 9; ++i)
                    wrong += got[i] != want[i];
            }
        }
        // ... and along the axes, at every angle.
        for (const std::array<std::int16_t, 3> axis : {std::array<std::int16_t, 3>{16384, 0, 0},
                                                       std::array<std::int16_t, 3>{0, -16384, 0},
                                                       std::array<std::int16_t, 3>{0, 0, 16384}})
            for (std::int32_t angle = 0; angle < 65536; ++angle)
            {
                std::int32_t got[9], want[9];
                detail::axisTurn(axis.data(), angle, got);
                axisTurnBefore(axis.data(), angle, want);
                for (int i = 0; i < 9; ++i)
                    wrong += got[i] != want[i];
            }
        check(wrong == 0, "a turn about an axis as through 64 bits");

        // The blend of an aim: every pitch.
        wrong = 0;
        for (std::int32_t pitch = -32768; pitch < 32768; ++pitch)
            wrong += ((pitch * 45 + 16) >> 5) !=
                     static_cast<std::int32_t>((static_cast<std::int64_t>(pitch) * 360 * 256 + 32768) >> 16);
        check(wrong == 0, "an aim's blend in 32 bits");
    }
    void testDrawing()
    {
        // ModelDraw::drawActor: a Q12 turn (entries up to ~5800, rolled)
        // through the view, whose rows are Q12 << 3.
        std::mt19937 random(31);
        int wrong = 0;
        for (int n = 0; n < 300000; ++n)
        {
            std::int32_t turn[9], axes[9], view[9];
            for (int k = 0; k < 9; ++k)
            {
                turn[k] = static_cast<std::int32_t>(random() % 11601) - 5800;
                axes[k] = static_cast<std::int32_t>(random() % 11601) - 5800;
                view[k] = axes[k] << 3;
            }
            for (int j = 0; j < 3; ++j)
                for (int m = 0; m < 3; ++m)
                    wrong += (dotShort(turn + m, 3, axes + j, 3) >> 9) != wideDot(turn + m, 3, view + j, 3, 12);
            // ... a held gun: the Q12 turn times the hand's Q14 rotation.
            std::int32_t hand[9];
            for (std::int32_t &v : hand)
                v = rotationEntry(random);
            wrong += (dotShort(turn, 1, hand, 3) >> kBoneRotShift) != wideDot(turn, 1, hand, 3, kBoneRotShift);
        }
        check(wrong == 0, "a model's turn through the view, as through 64 bits");
    }
    /// fxAtan2Turns as it was, through 64 bits.
    std::uint16_t atan2Before(fx y, fx x)
    {
        const std::int64_t ax = x < 0 ? -static_cast<std::int64_t>(x) : x;
        const std::int64_t ay = y < 0 ? -static_cast<std::int64_t>(y) : y;
        if (ax == 0 && ay == 0)
            return 0;
        const std::int64_t z = ax >= ay ? (ay << 15) / ax : (ax << 15) / ay;
        const std::int64_t bend = (z * (32768 - z)) >> 15;
        std::int64_t angle = ((8192 * z) >> 15) + ((bend * (2552 + ((692 * z) >> 15))) >> 15);
        if (ay > ax)
            angle = 16384 - angle;
        if (x < 0)
            angle = 32768 - angle;
        if (y < 0)
            angle = 65536 - angle;
        return static_cast<std::uint16_t>(angle);
    }

    void testDirections()
    {
        int wrong = 0;
        // Every ratio the near-square region gives, and far apart ones.
        for (fx x = -2048; x <= 2048; x += 3)
            for (fx y = -2048; y <= 2048; y += 5)
                wrong += fxAtan2Turns(y, x) != atan2Before(y, x);
        std::mt19937 random(41);
        for (int n = 0; n < 500000; ++n)
        {
            const fx x = static_cast<fx>(random()) >> (random() % 31);
            const fx y = static_cast<fx>(random()) >> (random() % 31);
            wrong += fxAtan2Turns(y, x) != atan2Before(y, x);
        }
        for (const fx a : {0, 1, -1, 4096, -4096, 0x7FFFFFFF, static_cast<fx>(0x80000000u)})
            for (const fx b : {0, 1, -1, 4096, -4096, 0x7FFFFFFF, static_cast<fx>(0x80000000u)})
                wrong += fxAtan2Turns(a, b) != atan2Before(a, b);
        check(wrong == 0, "fxAtan2Turns as through 64 bits");

        // Turns to degrees (Q12), every 16-bit turn.
        wrong = 0;
        for (std::int32_t t = -32768; t < 32768; ++t)
            wrong += ((t * 45) >> 1) != static_cast<fx>((static_cast<std::int64_t>(t) * 360 * kFxOne) >> 16);
        check(wrong == 0, "turns to degrees in 32 bits");
    }
    /// fxSinCos as it was, with its own table and a plain multiply: the shared
    /// table and SMLABB must give every angle's same bits.
    void sinCosBefore(std::uint16_t angle, fx &sine, fx &cosine)
    {
        constexpr std::int16_t kQuarter[65] = {
            0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981,
            4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005, 7366, 7723, 8076,
            8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585,
            11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256,
            14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557, 15679, 15791, 15893,
            15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};
        const auto quarterSine = [&](std::uint32_t a) -> fx
        {
            const std::uint32_t index = a >> 8;
            const std::int32_t frac = static_cast<std::int32_t>(a & 255);
            std::int32_t fine = kQuarter[index >= 64 ? 64 : index];
            if (index < 64)
                fine += ((kQuarter[index + 1] - kQuarter[index]) * frac + 128) >> 8;
            return (fine + 2) >> 2;
        };
        const auto wave = [&](std::uint16_t a) -> fx
        {
            const std::uint32_t within = a & 16383;
            switch (a >> 14)
            {
            case 0:  return quarterSine(within);
            case 1:  return quarterSine(16384 - within);
            case 2:  return -quarterSine(within);
            default: return -quarterSine(16384 - within);
            }
        };
        sine = wave(angle);
        cosine = wave(static_cast<std::uint16_t>(angle + 16384));
    }

    void testSine()
    {
        int wrong = 0;
        for (std::uint32_t a = 0; a < 65536; ++a)
        {
            fx s, c, ws, wc;
            fxSinCos(static_cast<std::uint16_t>(a), s, c);
            sinCosBefore(static_cast<std::uint16_t>(a), ws, wc);
            wrong += s != ws || c != wc;
        }
        // And worked out by the compiler, as a door's constant angle is.
        constexpr auto atCompile = []
        {
            fx s = 0, c = 0;
            fxSinCos(12345, s, c);
            return s * 65536 + c;
        }();
        fx ws, wc;
        sinCosBefore(12345, ws, wc);
        wrong += atCompile != ws * 65536 + wc;
        check(wrong == 0, "fxSinCos at every angle as with its own table");
    }
    /// A blended quaternion back to unit length as it was: a root and a divide.
    void toUnitBefore(std::int32_t *quat)
    {
        const auto length = static_cast<std::int32_t>(isqrt64(static_cast<std::uint64_t>(detail::squaresShort(quat))));
        if (length > 0)
        {
            const auto reciprocal = static_cast<std::int32_t>((std::int64_t(1) << 28) / length);
            for (int i = 0; i < 4; ++i)
                quat[i] = (quat[i] * reciprocal) >> 14;
        }
    }

    void testUnit()
    {
        // The table's scale against 2^28 / sqrt(x), every length squared it
        // covers: within a unit and a quarter, (4R - 5)^2 x < 2^60 <
        // (4R + 5)^2 x (measured: -0.99 to +1.18). The root and divide before
        // were within three (-1.00 to +2.63: cut short twice).
        const std::uint64_t whole = std::uint64_t(1) << 56;
        int wrong = 0, wrongBefore = 0;
        for (std::uint32_t x = detail::kUnitLow; x < detail::kUnitLow + (1u << 28); ++x)
        {
            const std::uint64_t r = static_cast<std::uint64_t>(detail::unitScale(static_cast<std::int32_t>(x)));
            wrong += !((4 * r - 5) * (4 * r - 5) * x < 16 * whole && 16 * whole < (4 * r + 5) * (4 * r + 5) * x);
            if ((x & 255) == 0)
            {
                const std::uint64_t b = (std::uint64_t(1) << 28) / isqrt64(x);
                wrongBefore += !((b - 3) * (b - 3) * x < whole && whole < (b + 3) * (b + 3) * x);
            }
        }
        check(wrong == 0, "the unit table within 1.25 of 2^28 / sqrt");
        check(wrongBefore == 0, "the root and divide within 3 of 2^28 / sqrt");
        int steep = 0;
        for (int i = 0; i < 256; ++i)
            steep = std::max(steep, std::abs(detail::kUnitTable.at[i + 1] - detail::kUnitTable.at[i]));
        check(steep <= 140 && detail::kUnitTable.at[0] <= 26755, "the unit table's steps and top 16-bit");

        // Blends as sampleTrack makes them, from keys as the converter writes
        // them: the parts as before give or take two, and the length squared
        // no farther from one than it was (both about 3/10000: each part's
        // last bit cut off).
        std::mt19937 random(31);
        const auto key = [&random]
        {
            // A unit quaternion in Q14 with w not negative, w left out.
            double v[4], length = 0;
            do
            {
                length = 0;
                for (double &p : v)
                {
                    p = static_cast<double>(random()) / random.max() * 2 - 1;
                    length += p * p;
                }
            } while (length < 1e-3 || length > 1);
            length = std::sqrt(length);
            const double sign = v[3] < 0 ? -1 : 1; // (-q for a negative w)
            ModelKey k{};
            for (int i = 0; i < 3; ++i)
                k.v[i] = static_cast<std::int16_t>(std::lround(v[i] * sign / length * 16384));
            return k;
        };
        wrong = 0;
        std::int64_t worst = 0, worstBefore = 0;
        int far = 0;
        for (int n = 0; n < 500000; ++n)
        {
            std::int32_t a[4], b[4];
            detail::fullQuat(key(), a);
            detail::fullQuat(key(), b);
            const std::int32_t s = static_cast<std::int32_t>(random() % 4097);
            const std::int32_t sign = detail::dotShort4(a, b) < 0 ? -1 : 1;
            std::int32_t q[4], before[4];
            for (int i = 0; i < 4; ++i)
                q[i] = before[i] = a[i] + (((sign * b[i] - a[i]) * s) >> 12);
            detail::toUnit(q);
            toUnitBefore(before);
            for (int i = 0; i < 4; ++i)
                far = std::max(far, std::abs(q[i] - before[i]));
            const std::int64_t off = std::abs(static_cast<std::int64_t>(detail::squaresShort(q)) - (1 << 28));
            const std::int64_t offBefore = std::abs(static_cast<std::int64_t>(detail::squaresShort(before)) - (1 << 28));
            worst = std::max(worst, off);
            worstBefore = std::max(worstBefore, offBefore);
            // (The dot as through 64 bits.)
            std::int64_t dot = 0;
            for (int i = 0; i < 4; ++i)
                dot += static_cast<std::int64_t>(a[i]) * b[i];
            wrong += detail::dotShort4(a, b) != dot;
        }
        check(wrong == 0, "a quaternion dot in 32 bits as through 64");
        check(far <= 2, "a blend's parts back to unit as before, give or take two");
        check(worst <= worstBefore && worst * 3000 <= (std::int64_t(1) << 28),
              "a blend's length squared as near one as before, within 1/3000");
        std::printf("test_dsp: unit length off by at most %lld/2^28 (before: %lld), parts by %d\n",
                    static_cast<long long>(worst), static_cast<long long>(worstBefore), far);

        // The keys' own: fullQuat's squares as through 64 bits.
        wrong = 0;
        for (int n = 0; n < 200000; ++n)
        {
            const ModelKey k = key();
            std::int32_t q[4];
            detail::fullQuat(k, q);
            const std::int64_t rest = (std::int64_t(1) << 28) - static_cast<std::int64_t>(k.v[0]) * k.v[0] -
                                      static_cast<std::int64_t>(k.v[1]) * k.v[1] - static_cast<std::int64_t>(k.v[2]) * k.v[2];
            wrong += q[3] != (rest > 0 ? static_cast<std::int32_t>(isqrt64(static_cast<std::uint64_t>(rest))) : 0);
        }
        check(wrong == 0, "a key's w as through 64 bits");
    }
} // namespace

int main()
{
    testSine();
    testUnit();
    testDefinition();
    testUses();
    testBones();
    testDrawing();
    testDirections();
    if (failures == 0)
        std::printf("test_dsp: ok\n");
    return failures == 0 ? 0 : 1;
}
