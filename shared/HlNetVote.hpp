#pragma once

// The vote for the next map, as every console keeps it: every map that may be
// played, how many have chosen each, and who chose what. The host's is the one
// that counts; the others are told. A vote, once made, is final; when everyone
// has voted it is over. (Who comes while it goes on has no vote; who leaves
// takes theirs back. Nobody voting, or a tie, is settled by a draw.)
//
// std-only: the host tests run it.

#include <cstdint>

#include "HlNet.hpp"

namespace hldata
{
    /// NextMap's `a`: not a map, but the vote given up; or, with this bit, the
    /// rest of the name of map `a`.
    inline constexpr int kNetVoteCancel = 255;
    inline constexpr int kNetVoteNameRest = 0x40;
    /// What the vote came to is shown this long.
    inline constexpr int kNetVoteResultMs = 4000;

    struct NetVote
    {
        static constexpr int kMost = 32;
        static constexpr int kNameBytes = 2 * (kNetTextBytes - 1) + 1;
        std::uint8_t count = 0;
        char names[kMost][kNameBytes] = {};
        std::uint8_t tally[kMost] = {};
        std::int8_t choice[kNetPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};
        std::uint8_t voters = 0;  ///< bit p: player p may vote (the host's)
        std::int8_t decided = -1; ///< the map it came to
        std::uint16_t stamp = 0;  ///< changes with whatever is shown of it
        bool open = false;        ///< its time has come: voting
        std::int8_t chooser = -1; ///< the player who alone chooses (-1: everyone votes)

        /// Player `p` chooses `slot`, once and for all.
        bool cast(int p, int slot)
        {
            if (p < 0 || p >= kNetPlayers || !((voters >> p) & 1) || slot < 0 || slot >= count || decided >= 0 ||
                choice[p] >= 0)
                return false;
            choice[p] = static_cast<std::int8_t>(slot);
            ++tally[slot];
            ++stamp;
            return true;
        }
        /// Player `p` has gone: no vote, and theirs does not count. The map it
        /// was for, if it was (-1: none).
        int drop(int p)
        {
            if (p < 0 || p >= kNetPlayers)
                return -1;
            voters = static_cast<std::uint8_t>(voters & ~(1u << p));
            const int was = choice[p];
            if (was >= 0)
            {
                --tally[was];
                ++stamp;
            }
            choice[p] = -1;
            return was;
        }
        /// Everyone who may vote has (and there is someone).
        bool allVoted() const
        {
            if (voters == 0)
                return false;
            for (int p = 0; p < kNetPlayers; ++p)
            {
                if (((voters >> p) & 1) && choice[p] < 0)
                    return false;
            }
            return true;
        }
        /// What it comes to: the most chosen; among those level (all of them,
        /// if nobody chose), by `draw`. -1 if nothing was offered.
        int winner(std::uint32_t draw) const
        {
            int most = 0, level = 0;
            for (int s = 0; s < count; ++s)
                most = tally[s] > most ? tally[s] : most;
            for (int s = 0; s < count; ++s)
                level += tally[s] == most ? 1 : 0;
            if (level == 0)
                return -1;
            int wanted = static_cast<int>(draw % static_cast<std::uint32_t>(level));
            for (int s = 0; s < count; ++s)
            {
                if (tally[s] == most && wanted-- == 0)
                    return s;
            }
            return -1;
        }
        /// The maps to name when it is over: the one it came to, then those
        /// with the most votes. Those with none are not named. How many.
        int top(int *out, int most) const
        {
            int found = 0;
            if (decided >= 0 && found < most)
                out[found++] = decided;
            for (int votes = 255; votes >= 1 && found < most; --votes)
            {
                for (int s = 0; s < count && found < most; ++s)
                {
                    if (tally[s] == votes && s != decided)
                        out[found++] = s;
                }
            }
            return found;
        }
        /// Part of a map's name, as the events bring it.
        void setName(int slot, bool rest, const char *text)
        {
            if (slot < 0 || slot >= kMost)
                return;
            char *to = names[slot] + (rest ? kNetTextBytes - 1 : 0);
            for (int n = 0; n < kNetTextBytes - 1; ++n)
                to[n] = text[n];
            // (What it ends with is already nothing: whichever part comes first.)
            if (slot >= count)
                count = static_cast<std::uint8_t>(slot + 1);
            ++stamp;
        }
    };

    /// Whose turn it is to choose after player `last` (-1: nobody yet): the next
    /// seat that has someone (bit p of `here`), round and round. -1: nobody.
    inline int netNextSeat(int last, unsigned here)
    {
        for (int n = 1; n <= kNetPlayers; ++n)
        {
            const int seat = (last + n + kNetPlayers) % kNetPlayers;
            if ((here >> seat) & 1)
                return seat;
        }
        return -1;
    }

    /// `most` of the `count` in `list`, drawn at random by `seed`, into their
    /// first places. How many.
    inline int netDrawSome(int *list, int count, std::uint32_t seed, int most)
    {
        const int some = count < most ? count : most;
        for (int n = 0; n < some; ++n)
        {
            seed = seed * 1664525u + 1013904223u;
            const int other = n + static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(count - n));
            const int was = list[n];
            list[n] = list[other];
            list[other] = was;
        }
        return some;
    }

    /// The list of the vote's maps, on the touch screen: a button every
    /// kNetVoteRowPixels, slid by the pixel, seen through a window
    /// kNetVoteViewPixels tall. At most kNetVoteRows are in sight at once.
    inline constexpr int kNetVoteRowPixels = 64, kNetVoteViewPixels = 168, kNetVoteRows = 4;
    /// How far the list may be slid.
    inline int netVoteLastScroll(int count)
    {
        const int last = count * kNetVoteRowPixels - kNetVoteViewPixels;
        return last > 0 ? last : 0;
    }
    inline int netVoteClampScroll(int scroll, int count)
    {
        const int last = netVoteLastScroll(count);
        return scroll < 0 ? 0 : (scroll > last ? last : scroll);
    }
    /// Where it comes to rest once let go: on a button, or at an end.
    inline int netVoteRestScroll(int scroll, int count)
    {
        // (Near the end, the end: the last map is to be seen whole.)
        const int last = netVoteLastScroll(count);
        if (scroll > last - kNetVoteRowPixels / 2)
            return last;
        const int to = (scroll + kNetVoteRowPixels / 2) / kNetVoteRowPixels * kNetVoteRowPixels;
        return netVoteClampScroll(to, count);
    }
    /// The first map in sight.
    inline int netVoteFirstShown(int scroll) { return scroll / kNetVoteRowPixels; }
    /// The map at `y` pixels down the window; -1: none there.
    inline int netVoteMapAt(int scroll, int y, int count)
    {
        if (y < 0 || y >= kNetVoteViewPixels)
            return -1;
        const int map = (scroll + y) / kNetVoteRowPixels;
        return map < count ? map : -1;
    }

    /// The pictures kept of the vote's maps: only a few, a console having little
    /// to spare. The caller keeps their bytes, place by place.
    struct NetVotePictures
    {
        static constexpr int kPlaces = 6;
        std::int8_t slot[kPlaces] = {-1, -1, -1, -1, -1, -1};
        std::uint32_t used[kPlaces] = {};
        std::uint32_t clock = 0;

        int find(int map) const
        {
            for (int n = 0; n < kPlaces; ++n)
            {
                if (slot[n] == map)
                    return n;
            }
            return -1;
        }
        void touch(int place) { used[place] = ++clock; }
        /// Where to put another: an empty place, or that used longest ago that
        /// is not one of the `shown` maps (-1 in them: none).
        int room(const int *shown, int shownCount) const
        {
            int best = -1;
            for (int n = 0; n < kPlaces; ++n)
            {
                if (slot[n] < 0)
                    return n;
                bool seen = false;
                for (int s = 0; s < shownCount; ++s)
                    seen = seen || shown[s] == slot[n];
                if (!seen && (best < 0 || used[n] < used[best]))
                    best = n;
            }
            return best;
        }
    };

    /// Which picture to have next: those shown first, then those a slide away.
    /// `known` (bit: map): had already, or there is none. -1: nothing is wanted.
    inline int netVoteWanted(const int *shown, int shownCount, int top, int count, std::uint32_t known)
    {
        const auto want = [&](int map) { return map >= 0 && map < count && !((known >> map) & 1); };
        for (int s = 0; s < shownCount; ++s)
        {
            if (want(shown[s]))
                return shown[s];
        }
        if (want(top + kNetVoteRows))
            return top + kNetVoteRows;
        if (want(top - 1))
            return top - 1;
        return -1;
    }
} // namespace hldata
