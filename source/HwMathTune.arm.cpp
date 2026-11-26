// How the root is taken on this console, chosen at start (Kuratius's ideas,
// measured by tools/hwtest/divtricks on the owner's DS and DSi):
//
// - The square-root unit's answer is read after a set wait instead of
//   polling its busy bit (a DS 35 bus ticks a root instead of 46, a DSi 27
//   instead of 37). The wait is found here, once: the shortest after which
//   every answer of a set of roots is right, three waits in a row, and then
//   held to other roots of every size: if one is wrong, it polls. HwMath.s
//   holds interrupts off around the unit, so nothing can cut into a root and
//   make the wait look longer or shorter than it is. If no wait up to the
//   most is right (an emulator that times the unit otherwise), it polls.
//   The wait is for a unit already in 64 bits, whose mode HwMath.s does not
//   write again (a DS 27 ticks, a DSi 22): the first root here sets it.
// - A value of 32 bits takes its root a bit at a time, without the unit
//   (HwMath.s's hlBitRoot32, Kuratius's three instructions a bit): a DS 19
//   to 24 ticks instead of the unit's 35, a DSi 9 to 12 instead of the
//   multiplies' 17 (shared/HlSoftRoot.hpp, its way before).
//
// Here, not in ITCM: the start runs once.

#include <cstdint>

#include "HlFixed.hpp"

extern "C"
{
    /// HwMath.s's: bit 31, 32 bits a bit at a time; the rest, the turns of
    /// its loop to wait for the unit, or 0 to poll it. (In DTCM.)
    extern std::uint32_t hlRootWay;

    /// HwMath.s's root under a name not marked const: the tune takes the
    /// same roots again on purpose, each time after another wait, which the
    /// compiler must not take once for all.
    std::uint32_t hlRootFresh(std::uint64_t value) __asm__("hlHwSqrt64");
}

namespace hlrt
{
    namespace
    {
        constexpr int kValues = 256;
        constexpr std::uint32_t kMostTurns = 48;
        constexpr std::uint32_t kBits = 0x80000000u;

        /// The root a bit at a time: what the unit is held to.
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

    bool hwMathRootsRight();

    /// Chooses the root's way for this console: once, at start, after the
    /// CPU's clock is set (the wait is counted in its instructions).
    void hwMathTune()
    {
        // Values of every size, above 32 bits (the unit's, on either console).
        std::uint64_t values[kValues];
        std::uint32_t roots[kValues];
        std::uint64_t seed = 0x9E3779B97F4A7C15ull;
        for (int i = 0; i < kValues; ++i)
        {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            const std::uint64_t v = (seed >> (i % 31)) | (std::uint64_t(1) << 33);
            values[i] = i == 0 ? ~std::uint64_t(0) : v;
            roots[i] = slowRoot(values[i]);
        }
        hlRootWay = 0; // (polled, while it looks)
        std::uint32_t found = 0, inRow = 0;
        for (std::uint32_t turns = 1; turns <= kMostTurns && inRow < 3; ++turns)
        {
            hlRootWay = turns;
            bool right = true;
            for (int i = 0; i < kValues && right; ++i)
                right = hlRootFresh(values[i]) == roots[i];
            if (right)
            {
                if (inRow++ == 0)
                    found = turns;
            }
            else
            {
                inRow = 0;
            }
        }
        hlRootWay = (inRow >= 3 ? found : 0) | kBits;
        // The way chosen, held to other values of every size (those of 32
        // bits, a bit at a time, among them): one wrong, and the unit is
        // polled for every root, as before any of this.
        if (!hwMathRootsRight())
            hlRootWay = 0;
    }

    /// Whether the root as it is now taken gives the slow way's answer for a
    /// set of values of every size, 0 to 64 bits.
    bool hwMathRootsRight()
    {
        std::uint64_t seed = 0xD1B54A32D192ED03ull;
        for (int i = 0; i < kValues; ++i)
        {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            const std::uint64_t v = seed >> (i % 64);
            if (hlRootFresh(v) != slowRoot(v))
                return false;
        }
        return true;
    }
} // namespace hlrt
