#pragma once
// The game's shared random numbers (its U_Random and UTIL_SharedRandom*):
// what its weapons draw from the command's random_seed, the same on the
// server and on the shooter's client, so that both see a round go the same
// way. Mirrored exactly here, so that a shot's spread is the game's for the
// same seed (tools/ref/diag.sh holds them together).
//
// A seed picks a start from a table of 256 (by its low byte only); each draw
// then multiplies by 69069 and adds the table's number for the low byte. The
// draws of a "float" are the third of three; the game adds the bits of the
// float bounds it was given into the seed (kSharedBits*).

#include <cstdint>

namespace hldata
{
    namespace detail
    {
        /// The game's seed_table (dlls/util.cpp of its SDK).
        inline constexpr std::uint16_t kSharedSeeds[256] = {
            28985, 27138, 26457,  9451, 17764, 10909, 28790,  8716,  6361,  4853,
            17798, 21977, 19643, 20662, 10834, 20103, 27067, 28634, 18623, 25849,
             8576, 26234, 23887, 18228, 32587,  4836,  3306,  1811,  3035, 24559,
            18399,   315, 26766,   907, 24102, 12370,  9674,  2972, 10472, 16492,
            22683, 11529, 27968, 30406, 13213,  2319, 23620, 16823, 10013, 23772,
            21567,  1251, 19579, 20313, 18241, 30130,  8402, 20807, 27354,  7169,
            21211, 17293,  5410, 19223, 10255, 22480, 27388,  9946, 15628, 24389,
            17308,  2370,  9530, 31683, 25927, 23567, 11694, 26397, 32602, 15031,
            18255, 17582,  1422, 28835, 23607, 12597, 20602, 10138,  5212,  1252,
            10074, 23166, 19823, 31667,  5902, 24630, 18948, 14330, 14950,  8939,
            23540, 21311, 22428, 22391,  3583, 29004, 30498, 18714,  4278,  2437,
            22430,  3439, 28313, 23161, 25396, 13471, 19324, 15287,  2563, 18901,
            13103, 16867,  9714, 14322, 15197, 26889, 19372, 26241, 31925, 14640,
            11497,  8941, 10056,  6451, 28656, 10737, 13874, 17356,  8281, 25937,
             1661,  4850,  7448, 12744, 21826,  5477, 10167, 16705, 26897,  8839,
            30947, 27978, 27283, 24685, 32298,  3525, 12398, 28726,  9475, 10208,
              617, 13467, 22287,  2376,  6097, 26312,  2974,  9114, 21787, 28010,
             4725, 15387,  3274, 10762, 31695, 17320, 18324, 12441, 16801, 27376,
            22464,  7500,  5666, 18144, 15314, 31914, 31627,  6495,  5226, 31203,
             2331,  4668, 12650, 18275,   351,  7268, 31319, 30119,  7600,  2905,
            13826, 11343, 13053, 15583, 30055, 31093,  5067,   761,  9685, 11070,
            21369, 27155,  3663, 26542, 20169, 12161, 15411, 30401,  7580, 31784,
             8985, 29367, 20989, 14203, 29694, 21167, 10337,  1706, 28578,   887,
             3373, 19477, 14382,   675,  7033, 15111, 26138, 12252, 30996, 21409,
            25678, 18555, 13256, 23316, 22407, 16727,   991,  9236,  5373, 29402,
             6117, 15241, 27715, 19291, 19888, 19847,
        };
    } // namespace detail

    /// U_Srand(seed) and U_Random()s after it.
    struct SharedRandom
    {
        std::uint32_t state;
        constexpr explicit SharedRandom(std::uint32_t seed) : state(detail::kSharedSeeds[seed & 255]) {}
        constexpr std::uint32_t next()
        {
            state *= 69069u;
            state += detail::kSharedSeeds[state & 255];
            return ++state & 0x0fffffffu;
        }
    };

    /// The bits of the bounds a UTIL_SharedRandomFloat is given, added, as
    /// the game adds them into its seed: -0.5..0.5 (a round's spread), 0..1,
    /// 10..15 (a weapon's idle).
    inline constexpr std::uint32_t kSharedBitsHalf = 0xBF000000u + 0x3F000000u;
    inline constexpr std::uint32_t kSharedBitsUnit = 0x00000000u + 0x3F800000u;
    inline constexpr std::uint32_t kSharedBitsIdle = 0x41200000u + 0x41700000u;

    /// UTIL_SharedRandomFloat(seed, low, high)'s draw, 0..65535: the float
    /// is low + draw / 65536 * (int)(high - low). `bits`: kSharedBits* for
    /// the bounds.
    constexpr std::uint32_t sharedRandom16(std::uint32_t seed, std::uint32_t bits)
    {
        SharedRandom r(seed + bits);
        r.next();
        r.next();
        return r.next() & 65535u;
    }

    /// UTIL_SharedRandomLong(seed, low, high): low..high. (Not in a hot
    /// path: its remainder divides.)
    constexpr int sharedRandomLong(std::uint32_t seed, int low, int high)
    {
        SharedRandom r(seed + static_cast<std::uint32_t>(low) + static_cast<std::uint32_t>(high));
        const std::uint32_t range = static_cast<std::uint32_t>(high - low) + 1u;
        if (range == 1u)
            return low;
        return low + static_cast<int>(static_cast<std::int32_t>(r.next()) % static_cast<std::int32_t>(range));
    }

    /// The spread of round `n` (from 1) of a shot on command seed `seed`, as
    /// the game's FireBulletsPlayer draws it: each way the sum of two draws
    /// of -0.5..0.5, here in 65536ths (-65536..65534).
    constexpr void sharedSpread(std::uint32_t seed, int n, std::int32_t &x, std::int32_t &y)
    {
        const std::uint32_t s = seed + static_cast<std::uint32_t>(n);
        x = static_cast<std::int32_t>(sharedRandom16(s, kSharedBitsHalf) + sharedRandom16(s + 1, kSharedBitsHalf)) - 65536;
        y = static_cast<std::int32_t>(sharedRandom16(s + 2, kSharedBitsHalf) + sharedRandom16(s + 3, kSharedBitsHalf)) - 65536;
    }
} // namespace hldata
