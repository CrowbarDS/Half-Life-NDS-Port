#pragma once

// The fast memories' heaps (libnds's itcm_malloc), kept by rules
// (docs/TECHNICAL.md, "the fast memories"):
//  - placed once, at start-up, and never let go: nothing is taken from them
//    while a map is played, so what is there never moves or goes while a
//    thread or an interrupt may be running it;
//  - always with a way without: a caller that is refused keeps its code in
//    main RAM, slower, the same;
//  - ARM9 only: nothing a DMA, the ARM7 or the DSP reads goes there;
//  - never from an interrupt;
//  - each placement kept, by its owner, for perf.txt.
// DTCM is the stack's (DTCM_DATA_SIZE in build.py): nothing is taken from
// its heap.

#include <cstddef>
#include <cstdint>

namespace hlrt::tcm
{
    /// Code copied into ITCM, `bytes` of it, for `owner` (a name kept, not
    /// copied): where it now is (0x0100xxxx), or null -- no room, or after
    /// seal().
    void *placeCode(const void *from, std::size_t bytes, const char *owner);
    /// From here on nothing more is placed (the first map is near).
    void seal();

    struct Placed
    {
        const char *owner;
        std::uint32_t at; ///< its offset in ITCM
        std::uint32_t bytes;
    };
    int placedCount();
    const Placed &placed(int i);
} // namespace hlrt::tcm
