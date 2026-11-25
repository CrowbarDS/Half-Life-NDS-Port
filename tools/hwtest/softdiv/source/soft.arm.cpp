// The divide without the divider (HlSoftDiv.hpp) as the probe calls it, in
// ITCM and in ARM code, as the game would have it. What it does not answer
// (a quotient of more than 32 bits) goes to the library, and is counted.
// And a walk's step (shared/HlWalk.hpp: two numbers by one distance) both
// ways: two divides (the game's, by --wrap), or one reciprocal and a
// multiply each (HlSoftDiv.hpp's own copies, in main RAM as in the game).

#include <cstdint>

#include "HlSoftDiv.hpp"

extern "C"
{
    volatile std::uint32_t softOther = 0;

    [[gnu::section(".itcm.text.softdiv"), gnu::noinline]] std::int64_t softDivmodC(std::int64_t n, std::int64_t d,
                                                                                   std::int64_t *rem)
    {
        std::int64_t q, r;
        if (hldata::softdiv::divmod64(n, d, q, r))
        {
            *rem = r;
            return q;
        }
        softOther = softOther + 1;
        *rem = n % d;
        return n / d;
    }

    [[gnu::section(".itcm.text.softdiv"), gnu::noinline]] std::uint64_t softUdivmodC(std::uint64_t n, std::uint64_t d,
                                                                                     std::uint64_t *rem)
    {
        std::uint64_t q, r;
        if (hldata::softdiv::udivmod64(n, d, q, r))
        {
            *rem = r;
            return q;
        }
        softOther = softOther + 1;
        *rem = n % d;
        return n / d;
    }

    [[gnu::section(".itcm.text.softdiv"), gnu::noinline]] std::uint64_t pairDivide(std::int32_t d0, std::int32_t d1,
                                                                                   std::int32_t distance)
    {
        const auto q0 = static_cast<std::uint32_t>((std::int64_t{d0} * 65536) / distance);
        const auto q1 = static_cast<std::uint32_t>((std::int64_t{d1} * 65536) / distance);
        return q0 | (std::uint64_t{q1} << 32);
    }

    [[gnu::section(".itcm.text.softdiv"), gnu::noinline]] std::uint64_t pairReciprocal(std::int32_t d0, std::int32_t d1,
                                                                                       std::int32_t distance)
    {
        const hldata::softdiv::Reciprocal recip = hldata::softdiv::reciprocalOf(static_cast<std::uint32_t>(distance));
        const auto q0 = static_cast<std::uint32_t>(hldata::softdiv::divideBy(std::int64_t{d0} * 65536, recip));
        const auto q1 = static_cast<std::uint32_t>(hldata::softdiv::divideBy(std::int64_t{d1} * 65536, recip));
        return q0 | (std::uint64_t{q1} << 32);
    }
}
