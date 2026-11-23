#pragma once

// The maps a match can be played on, as the menus offer them: the card's
// multiplayer maps (the manifest's: those with a place a player comes in at),
// each with how many such places it has and whether a console without the
// game can be sent it (HlManifest.hpp's guestFits). With consoles without the
// game let in, only those that fit them are offered -- and co-op, which every
// player needs the game for, is not.
//
// std-only: the host tests run it.

#include <cstdint>
#include <string>
#include <vector>

#include "HlNet.hpp"

namespace hldata
{
    struct NetMapInfo
    {
        std::string name;
        int spawns = 0;
        bool guestFits = false;
        /// Its group (HlManifest.hpp's AirGroup): whose base a console without
        /// the game is sent with it.
        int airGroup = 0;
        /// How far up a console without the game's memory it reaches, in KB:
        /// the program, the engine's reserve, the map's data and the group's
        /// base, and room to play (HlGuest.hpp's guestPeak, and play; 0: it is
        /// not sent).
        std::uint32_t guestNeed = 0;
        /// What a DS's heap has left over for it, in KB (HlManifest.hpp's
        /// ManifestMap::dsSpareKb; 0xFFFF: not known).
        std::uint16_t dsSpareKb = 0xFFFF;
    };

    /// Whether `map` fits a console without the game whose heap reaches
    /// `room` KB up its memory (as it says: the Room event); 0 -- none has
    /// said -- a console of the least there is, a DS (guestFits).
    inline bool netMapFits(const NetMapInfo &map, std::uint32_t room)
    {
        return room == 0 ? map.guestFits : (map.guestNeed != 0 && map.guestNeed <= room);
    }

    /// The players a map is made for: its places to come in at, no more
    /// than a match can have (and two at the least).
    inline int netMapPlayers(const NetMapInfo &map)
    {
        return map.spawns < 2 ? 2 : (map.spawns > kNetPlayers ? kNetPlayers : map.spawns);
    }

    /// Whether `map` may be chosen, consoles without the game let in or not
    /// (with `room`: the least any of them there has, netMapFits).
    inline bool netMapOffered(const NetMapInfo &map, bool guests, std::uint32_t room = 0)
    {
        return !guests || netMapFits(map, room);
    }

    /// Whether any map may be chosen with consoles without the game let in.
    inline bool netAnyGuestMap(const std::vector<NetMapInfo> &maps, std::uint32_t room = 0)
    {
        for (const NetMapInfo &map : maps)
        {
            if (netMapFits(map, room))
                return true;
        }
        return false;
    }

    /// The next map from `at` that may be chosen (`step` 1 or -1, round and
    /// round); `at` itself if it is the only one, or -1 if none is.
    inline int netMapStep(const std::vector<NetMapInfo> &maps, int at, int step, bool guests, std::uint32_t room = 0)
    {
        const int count = static_cast<int>(maps.size());
        for (int n = 1; n <= count; ++n)
        {
            const int k = ((at + step * n) % count + count) % count;
            if (netMapOffered(maps[static_cast<std::size_t>(k)], guests, room))
                return k;
        }
        return -1;
    }

    /// `at`, if it may be chosen; else the first that may (-1: none).
    inline int netMapKept(const std::vector<NetMapInfo> &maps, int at, bool guests, std::uint32_t room = 0)
    {
        if (at >= 0 && at < static_cast<int>(maps.size()) &&
            netMapOffered(maps[static_cast<std::size_t>(at)], guests, room))
            return at;
        return maps.empty() ? -1 : netMapStep(maps, -1, 1, guests, room);
    }

    /// Whether a mode may be played (HlSettings.hpp's kMode*: co-op is not
    /// built yet, and with consoles without the game it could not be).
    inline bool netModeOffered(int mode, bool guests)
    {
        (void)guests;
        return mode == kNetModeDeathmatch || mode == kNetModeTeamplay;
    }
} // namespace hldata
