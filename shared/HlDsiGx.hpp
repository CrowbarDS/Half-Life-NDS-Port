#pragma once

// "dsi.gx": a DSi's light laid over some of the level's surfaces -- those
// whose light their corners cannot carry, a pool of lamp light in the middle
// of a wall -- cut into squares of the lightmap's grid, each point lit as the
// lightmap has it (the converter's LightRefine.hpp). A member of the map's
// archive beside the level; a DSi reads it with the level and draws each run
// here over its surface's own, see-through (alpha 30) at the same depth,
// straight after it (with the run's polygons and vertices added to the
// surface's, for the pass's budget). A DS never reads it, and the level is
// the same for both: a save, or a game between consoles, never knows of it.
// (Version 1's runs were drawn instead of the surface's own.)
//
// Only surfaces no changing light reaches have one: a LightPatch changes
// words of the level's own stream, and the light over it would not change.
//
//   DsiGxHeader
//   DsiGxSurface[count]   by surface, rising
//   u32[words]            the runs, one after another
//
// std-only: the converter writes it, the console reads it, the host tests both.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hldata
{
    inline constexpr char kDsiGxMember[] = "dsi.gx";
    inline constexpr char kDsiGxMagic[4] = {'H', 'L', 'D', 'F'};
    inline constexpr std::uint16_t kDsiGxVersion = 2;

    struct DsiGxHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t count;    ///< surfaces with light over them
        std::uint32_t levelCrc; ///< the level's LevelHeader::crc32: of that level only
        std::uint32_t words;    ///< of all the runs together
    };
    static_assert(sizeof(DsiGxHeader) == 16);

    struct DsiGxSurface
    {
        std::uint16_t surface;  ///< into Surfaces
        std::uint16_t words;
        std::uint16_t polygons; ///< what it costs the geometry engine, besides the surface's own
        std::uint16_t vertices;
        std::uint32_t ofs;      ///< its first word, among the runs
    };
    static_assert(sizeof(DsiGxSurface) == 12);

    /// Whether `data` is a whole, well-formed member for a level of
    /// `surfaceCount` surfaces whose checksum is `levelCrc`.
    inline bool dsiGxValid(const std::uint8_t *data, std::size_t size, std::uint32_t surfaceCount, std::uint32_t levelCrc)
    {
        if (data == nullptr || size < sizeof(DsiGxHeader))
            return false;
        DsiGxHeader h;
        std::memcpy(&h, data, sizeof(h));
        if (std::memcmp(h.magic, kDsiGxMagic, 4) != 0 || h.version != kDsiGxVersion || h.levelCrc != levelCrc)
            return false;
        const std::size_t want = sizeof(DsiGxHeader) + std::size_t(h.count) * sizeof(DsiGxSurface) + std::size_t(h.words) * 4;
        if (size != want)
            return false;
        std::uint32_t last = 0;
        for (std::uint32_t i = 0; i < h.count; ++i)
        {
            DsiGxSurface s;
            std::memcpy(&s, data + sizeof(DsiGxHeader) + i * sizeof(DsiGxSurface), sizeof(s));
            if (s.surface >= surfaceCount || (i != 0 && s.surface <= last) || s.words == 0 ||
                std::uint64_t(s.ofs) + s.words > h.words)
                return false;
            last = s.surface;
        }
        return true;
    }
} // namespace hldata
