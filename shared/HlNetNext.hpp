#pragma once

// Which map a game between consoles goes on to when a match ends: the list
// of those that may be chosen, and the host's rotation (the game's
// mapcycle.txt: a list it goes round, edited on the host's page).
//
// std-only: the host tests run it.

#include <cstdint>
#include <vector>

#include "HlLevel.hpp"
#include "HlNetMaps.hpp"

namespace hldata
{
    /// The maps that may follow `played` (a name hash): those that fit every
    /// console there, never the one just played. Their places in `maps`, up to
    /// `most` of them, into `out`; how many.
    inline int netNextCandidates(const std::vector<NetMapInfo> &maps, std::uint32_t played, bool guests,
                                 std::uint32_t room, int *out, int most)
    {
        int count = 0;
        for (int m = 0; m < static_cast<int>(maps.size()) && count < most; ++m)
        {
            const NetMapInfo &map = maps[static_cast<std::size_t>(m)];
            if (netMapOffered(map, guests, room) && nameHash(map.name.c_str()) != played)
                out[count++] = m;
        }
        return count;
    }

    /// The next of the rotation after `played` (from its start if `played` is
    /// not on it), round and round, that may follow it: its place in `maps`.
    /// -1 if none may.
    inline int netNextRotation(const std::vector<NetMapInfo> &maps, const std::uint32_t *rotation, int count,
                               std::uint32_t played, bool guests, std::uint32_t room)
    {
        int at = -1;
        for (int n = 0; n < count && at < 0; ++n)
            at = rotation[n] == played ? n : -1;
        for (int n = 1; n <= count; ++n)
        {
            const std::uint32_t wanted = rotation[(at + n + count) % count];
            if (wanted == played)
                continue;
            for (int m = 0; m < static_cast<int>(maps.size()); ++m)
            {
                const NetMapInfo &map = maps[static_cast<std::size_t>(m)];
                if (nameHash(map.name.c_str()) == wanted && netMapOffered(map, guests, room))
                    return m;
            }
        }
        return -1;
    }

    // Editing the list. Each says whether anything changed.

    inline bool netRotationAdd(std::uint32_t *rotation, std::uint8_t &count, int most, std::uint32_t hash)
    {
        if (count >= most || hash == 0)
            return false;
        rotation[count++] = hash;
        return true;
    }
    inline bool netRotationRemove(std::uint32_t *rotation, std::uint8_t &count, int at)
    {
        if (at < 0 || at >= count)
            return false;
        for (int n = at + 1; n < count; ++n)
            rotation[n - 1] = rotation[n];
        rotation[--count] = 0;
        return true;
    }
    /// The one at `at` moved `step` places (1, -1), if it can be.
    inline bool netRotationMove(std::uint32_t *rotation, int count, int at, int step)
    {
        const int to = at + step;
        if (at < 0 || at >= count || to < 0 || to >= count)
            return false;
        const std::uint32_t moved = rotation[at];
        rotation[at] = rotation[to];
        rotation[to] = moved;
        return true;
    }
} // namespace hldata
