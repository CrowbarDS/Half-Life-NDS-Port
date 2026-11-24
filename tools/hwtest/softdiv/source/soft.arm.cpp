// The divide without the divider (HlSoftDiv.hpp) as the probe calls it, in
// ITCM and in ARM code, as the game would have it. What it does not answer
// (a quotient of more than 32 bits) goes to the library, and is counted.

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
}
