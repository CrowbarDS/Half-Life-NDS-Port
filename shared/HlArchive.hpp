#pragma once

// What the console keeps of a map's archive (an NPAC, NEANPAC.h) while the
// map is played: its index -- the file table, the name table and the table
// of how each file is packed -- in a fixed buffer of this size, so that
// mounting one map after another never goes to the heap. An archive whose
// index is bigger still works, mounted with its index on the heap; the
// converter says so when it packs one.
//
// std-only and inline: the host tests run it.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hldata
{
    inline constexpr std::uint32_t kMapIndexMost = 32 * 1024;

    /// How much an NPAC archive's index takes in that buffer, from its chunk
    /// headers: each table rounded up to a word, as the engine lays them
    /// out (NEA_NpacIndexBytes says the same on the console). 0 if the bytes
    /// are no archive.
    inline std::uint32_t npacIndexBytes(const std::uint8_t *data, std::size_t size)
    {
        auto rd16 = [&](std::size_t at) { return static_cast<std::uint32_t>(data[at] | data[at + 1] << 8); };
        auto rd32 = [&](std::size_t at) { return rd16(at) | rd16(at + 2) << 16; };
        if (size < 16 || (std::memcmp(data, "NPAC", 4) != 0 && std::memcmp(data, "NARC", 4) != 0))
            return 0;
        std::size_t pos = rd16(12);
        const std::uint32_t chunks = rd16(14);
        std::uint32_t files = 0, names = 0;
        bool packing = false;
        for (std::uint32_t i = 0; i < chunks && pos + 8 <= size; ++i)
        {
            const std::uint32_t bytes = rd32(pos + 4);
            if (bytes < 8)
                return 0;
            if (std::memcmp(data + pos, "BTAF", 4) == 0 && pos + 10 <= size)
                files = rd16(pos + 8);
            else if (std::memcmp(data + pos, "BTNF", 4) == 0)
                names = bytes - 8;
            else if (std::memcmp(data + pos, "CMPT", 4) == 0)
                packing = true;
            else if (std::memcmp(data + pos, "GMIF", 4) == 0)
                break;
            pos += (bytes + 3) & ~3u;
        }
        auto word = [](std::uint32_t n) { return (n + 3) & ~3u; };
        return word(files * 8) + word(names) + (packing ? word(files * 4) : 0);
    }
} // namespace hldata
