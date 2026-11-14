#pragma once
// What a DS leaves out of a map's level, for a map too big for its heap.
//
// The level file is the same for every console (a DSi loads it whole, and
// its checksum keys saves and games between consoles). A map the converter
// found short of room on a DS (HlDsRoom.hpp) has, beside it in its archive,
// "ds.cut": which of the level's sections a DS leaves out of memory once the
// level is read and checked, and where it has them instead.
//
//   kDsCutLightPatch  the lights that change are not worked again: a DS's
//                     lights stay as the map was lit (no flicker, no switch).
//   kDsCutVis         the visibility rows are read from "ds.vis" (a copy of
//                     the Vis section) a row at a time, as the view moves.
//   kDsCutGx          the geometry is read from "ds.gx" (a copy of the
//                     GxStream section) a page at a time, as it is drawn:
//                     pages are runs of whole surfaces, `pageCount` of them,
//                     page i the words from start[i] to start[i + 1]. A DS
//                     keeps `slotCount` of them at once. Implies no light patch.
//
// A console with an Expansion Pak, and a DSi, read none of it.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hldata
{
    inline constexpr char kDsCutMember[] = "ds.cut";
    inline constexpr char kDsVisMember[] = "ds.vis";
    inline constexpr char kDsGxMember[] = "ds.gx";

    inline constexpr std::uint16_t kDsCutLightPatch = 1;
    inline constexpr std::uint16_t kDsCutVis = 2;
    inline constexpr std::uint16_t kDsCutGx = 4;

    inline constexpr char kDsCutMagic[4] = {'H', 'D', 'S', 'C'};
    inline constexpr std::uint16_t kDsCutVersion = 1;
    /// A page's words: what one card read brings, near enough (a surface
    /// longer than this is a page of its own).
    inline constexpr std::uint32_t kDsGxPageWords = 1024;
    /// The most pages a level may be cut into, and a DS hold at once.
    inline constexpr std::uint32_t kDsGxMostPages = 4096;
    inline constexpr std::uint32_t kDsGxMostSlots = 512;
    /// The most words a page holds (a surface's, the longest there is).
    inline constexpr std::uint32_t kDsGxMostPageWords = 16384;

    struct DsCutHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t flags;
        std::uint32_t levelCrc;   ///< the level's LevelHeader::crc32: the cut is of that level only
        std::uint32_t pageCount;  ///< kDsCutGx: pages; then pageCount + 1 word starts follow
        std::uint32_t slotCount;  ///< ... how many a DS keeps at once
        std::uint32_t slotWords;  ///< ... and the longest of them, in words
        std::uint32_t gxWords;    ///< ... the GxStream's words, all told
        std::uint32_t visBytes;   ///< kDsCutVis: the Vis section's bytes
    };
    static_assert(sizeof(DsCutHeader) == 32);

    /// The member's bytes: the header, and the page starts if it has pages.
    inline std::size_t dsCutBytes(const DsCutHeader &h)
    {
        return sizeof(DsCutHeader) + ((h.flags & kDsCutGx) != 0 ? (h.pageCount + 1) * 4u : 0u);
    }

    /// Whether `bytes` (`size` of them) are a cut a reader can trust: the
    /// right kind and version, its pages in order, covering the stream
    /// exactly, none longer than `slotWords`, and as many slots as it has
    /// pages at most.
    inline bool dsCutValid(const std::uint8_t *bytes, std::size_t size)
    {
        if (size < sizeof(DsCutHeader))
            return false;
        DsCutHeader h;
        std::memcpy(&h, bytes, sizeof(h));
        if (std::memcmp(h.magic, kDsCutMagic, sizeof(kDsCutMagic)) != 0 || h.version != kDsCutVersion ||
            (h.flags & ~(kDsCutLightPatch | kDsCutVis | kDsCutGx)) != 0)
            return false;
        if ((h.flags & kDsCutGx) == 0)
            return size == sizeof(DsCutHeader);
        if (h.pageCount == 0 || h.pageCount > kDsGxMostPages || h.slotCount == 0 || h.slotCount > kDsGxMostSlots ||
            h.slotCount > h.pageCount || h.slotWords == 0 || h.slotWords > kDsGxMostPageWords ||
            (h.flags & kDsCutLightPatch) == 0 || size != dsCutBytes(h))
            return false;
        std::uint32_t last = 0;
        for (std::uint32_t i = 0; i <= h.pageCount; ++i)
        {
            std::uint32_t start;
            std::memcpy(&start, bytes + sizeof(DsCutHeader) + i * 4u, 4);
            if ((i == 0 && start != 0) || (i != 0 && (start <= last || start - last > h.slotWords)))
                return false;
            last = start;
        }
        return last == h.gxWords;
    }
} // namespace hldata
