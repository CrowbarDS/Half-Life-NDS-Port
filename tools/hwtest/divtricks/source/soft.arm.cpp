// The C++ the divide tricks probe times, in ITCM and ARM code as the game
// would have it: the roots without the unit (HlSoftRoot.hpp), and a walk's
// step (shared/HlWalk.hpp: two numbers by one distance) as two divides (the
// game's, by --wrap) or one reciprocal and a multiply each.

#include <cstdint>

#include "HlSoftDiv.hpp"
#include "HlSoftRoot.hpp"

extern "C"
{
    // (What the game's root, linked here, calls on a DSi for 32 bits:
    // source/HwMathTune.arm.cpp's, the same root.)
    std::uint32_t hlSoftRoot32(std::uint32_t v) { return hldata::softroot::root32(v); }

    [[gnu::section(".itcm.text.soft"), gnu::noinline]] std::uint32_t rootSoft32(std::uint32_t v)
    {
        return hldata::softroot::root32(v);
    }

    [[gnu::section(".itcm.text.soft"), gnu::noinline]] std::uint32_t rootSoft64(std::uint64_t v)
    {
        return hldata::softroot::root64(v);
    }

    [[gnu::section(".itcm.text.soft"), gnu::noinline]] std::uint64_t pairDivide(std::int32_t d0, std::int32_t d1,
                                                                                std::int32_t distance)
    {
        const auto q0 = static_cast<std::uint32_t>((std::int64_t{d0} * 65536) / distance);
        const auto q1 = static_cast<std::uint32_t>((std::int64_t{d1} * 65536) / distance);
        return q0 | (std::uint64_t{q1} << 32);
    }

    [[gnu::section(".itcm.text.soft"), gnu::noinline]] std::uint64_t pairReciprocal(std::int32_t d0, std::int32_t d1,
                                                                                    std::int32_t distance)
    {
        const hldata::softdiv::Reciprocal recip = hldata::softdiv::reciprocalOf(static_cast<std::uint32_t>(distance));
        const auto q0 = static_cast<std::uint32_t>(hldata::softdiv::divideBy(std::int64_t{d0} * 65536, recip));
        const auto q1 = static_cast<std::uint32_t>(hldata::softdiv::divideBy(std::int64_t{d1} * 65536, recip));
        return q0 | (std::uint64_t{q1} << 32);
    }
}
