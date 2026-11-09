#pragma once

// The members of an NPAC archive (HlArchive.hpp, NEANPAC.h) as its first
// bytes say them: for each, its path, where its stored bytes are in the file,
// how many, and how it is packed. What a client being sent a map's archive
// finds on its own card -- the weapons, the players, their sounds, in common
// with every other map -- is copied from there, not sent (net/MapShare.hpp).
//
// The layout is NitroARC's: a header, then BTAF (each member's stored extent
// in GMIF), BTNF (the directory tree, NitroROM's name table), CMPT (how each
// is packed: the BIOS header word) and GMIF (the members, in file-ID order,
// each padded to a word with FFh).
//
// std-only and inline: the host tests run it. Nothing in it is trusted: out
// of bounds is "no archive".

#include <cstddef>
#include <cstdint>
#include <cstring>

// (Where the console puts this code: the DSi's memory, where its one user
// is. Defined before this is included; nothing, for the host tests.)
#ifndef HL_NPAC_CODE
#define HL_NPAC_CODE
#endif

namespace hldata
{
    /// A member's path, as one number (FNV-1a of "snd/x.wav").
    HL_NPAC_CODE inline std::uint32_t npacPathHash(const char *path, std::size_t length)
    {
        std::uint32_t hash = 2166136261u;
        for (std::size_t i = 0; i < length; ++i)
            hash = (hash ^ static_cast<std::uint8_t>(path[i])) * 16777619u;
        return hash;
    }

    namespace detail
    {
        struct NpacWalk
        {
            const std::uint8_t *data;
            std::size_t size;
            std::size_t names;  ///< where BTNF's table begins
            std::size_t namesEnd;
            char path[192];
            bool ok = true;

            std::uint32_t rd16(std::size_t at) const
            {
                return at + 2 <= size ? static_cast<std::uint32_t>(data[at] | data[at + 1] << 8) : 0;
            }
            std::uint32_t rd32(std::size_t at) const { return rd16(at) | rd16(at + 2) << 16; }

            /// Directory `id`'s files, with `prefix` bytes of path before
            /// them: each's ID and path hash to `file`.
            template <class File>
            HL_NPAC_CODE void walk(std::uint32_t id, std::size_t prefix, int depth, File &file)
            {
                const std::size_t entry = names + (id & 0xFFFu) * 8;
                if (depth > 16 || entry + 8 > namesEnd)
                {
                    ok = false;
                    return;
                }
                std::size_t at = names + rd32(entry);
                std::uint32_t next = rd16(entry + 4);
                while (ok)
                {
                    if (at >= namesEnd)
                    {
                        ok = false;
                        return;
                    }
                    const std::uint32_t type = data[at++];
                    if (type == 0)
                        return;
                    const std::size_t length = type & 0x7Fu;
                    if (at + length > namesEnd || prefix + length + 1 >= sizeof(path))
                    {
                        ok = false;
                        return;
                    }
                    std::memcpy(path + prefix, data + at, length);
                    at += length;
                    if (type < 0x80)
                    {
                        file(next++, npacPathHash(path, prefix + length));
                        continue;
                    }
                    const std::uint32_t sub = rd16(at);
                    at += 2;
                    path[prefix + length] = '/';
                    walk(sub, prefix + length + 1, depth + 1, file);
                }
            }
        };
    } // namespace detail

    /// How much of the file's start npacMembers needs: up to its members'
    /// data (GMIF's own head included). 0 if what is there is no archive, or
    /// does not reach that far.
    HL_NPAC_CODE inline std::uint32_t npacHeadBytes(const std::uint8_t *data, std::size_t size)
    {
        auto rd16 = [&](std::size_t at) { return static_cast<std::uint32_t>(data[at] | data[at + 1] << 8); };
        auto rd32 = [&](std::size_t at) { return rd16(at) | rd16(at + 2) << 16; };
        if (size < 16 || (std::memcmp(data, "NPAC", 4) != 0 && std::memcmp(data, "NARC", 4) != 0))
            return 0;
        std::size_t pos = rd16(12);
        const std::uint32_t chunks = rd16(14);
        for (std::uint32_t i = 0; i < chunks && pos + 8 <= size; ++i)
        {
            const std::uint32_t bytes = rd32(pos + 4);
            if (bytes < 8)
                return 0;
            if (std::memcmp(data + pos, "GMIF", 4) == 0)
                return static_cast<std::uint32_t>(pos + 8);
            pos += (bytes + 3) & ~3u;
        }
        return 0;
    }

    /// The archive whose file begins with `size` bytes at `data` (its index,
    /// HlArchive.hpp's npacIndexBytes past its chunk heads, is enough):
    /// `each(id, pathHash, at, stored, packing)` for every member -- `at` from
    /// the file's start, `packing` the CMPT word (0: stored, as a NARC has
    /// none). `files` is how many there are. False if it is not an archive, or
    /// is cut short.
    template <class Each>
    HL_NPAC_CODE bool npacMembers(const std::uint8_t *data, std::size_t size, std::uint32_t &files, Each &&each)
    {
        detail::NpacWalk walk{data, size, 0, 0, {}};
        files = 0;
        if (size < 16 || (std::memcmp(data, "NPAC", 4) != 0 && std::memcmp(data, "NARC", 4) != 0))
            return false;
        std::size_t pos = walk.rd16(12);
        const std::uint32_t chunks = walk.rd16(14);
        std::size_t table = 0, packing = 0, members = 0;
        for (std::uint32_t i = 0; i < chunks && pos + 8 <= size; ++i)
        {
            const std::uint32_t bytes = walk.rd32(pos + 4);
            if (bytes < 8)
                return false;
            if (std::memcmp(data + pos, "BTAF", 4) == 0)
            {
                files = walk.rd16(pos + 8);
                table = pos + 12;
            }
            else if (std::memcmp(data + pos, "BTNF", 4) == 0)
            {
                walk.names = pos + 8;
                walk.namesEnd = pos + bytes;
            }
            else if (std::memcmp(data + pos, "CMPT", 4) == 0)
                packing = pos + 12;
            else if (std::memcmp(data + pos, "GMIF", 4) == 0)
            {
                members = pos + 8;
                break;
            }
            pos += (bytes + 3) & ~3u;
        }
        if (table == 0 || walk.names == 0 || members == 0 || table + files * 8 > size || walk.namesEnd > size ||
            (packing != 0 && packing + files * 4 > size))
            return false;
        // (The paths by a walk of the tree; the extents by ID.)
        auto file = [&](std::uint32_t id, std::uint32_t hash) {
            if (id >= files)
            {
                walk.ok = false;
                return;
            }
            const std::uint32_t start = walk.rd32(table + id * 8), end = walk.rd32(table + id * 8 + 4);
            if (end < start)
            {
                walk.ok = false;
                return;
            }
            each(id, hash, static_cast<std::uint32_t>(members) + start, end - start,
                 packing != 0 ? walk.rd32(packing + id * 4) : 0u);
        };
        walk.walk(0xF000, 0, 0, file);
        return walk.ok;
    }
} // namespace hldata
