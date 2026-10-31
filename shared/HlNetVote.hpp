#pragma once

// The vote for the next map, as every console keeps it: the maps offered, how
// many have chosen each, and who chose what. The host's is the one that counts;
// the others are told. (Who comes while it goes on has no vote; who leaves
// takes theirs back. Nobody voting, or a tie, is settled by a draw.)
//
// std-only: the host tests run it.

#include <cstdint>

#include "HlNet.hpp"

namespace hldata
{
    /// NextMap's `a`: not a map, but the vote given up.
    inline constexpr int kNetVoteCancel = 255;
    /// What the vote came to is shown this long; and it opens this long after
    /// it began at the latest, whatever pictures have yet to come.
    inline constexpr int kNetVoteResultMs = 2000, kNetVotePicturesMs = 10000;

    struct NetVote
    {
        static constexpr int kMost = 4;
        std::uint8_t count = 0;
        char names[kMost][kNetTextBytes] = {};
        std::uint8_t tally[kMost] = {};
        std::int8_t choice[kNetPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};
        std::uint8_t voters = 0;  ///< bit p: player p may vote (the host's)
        std::int8_t decided = -1; ///< the map it came to
        std::uint16_t stamp = 0;  ///< changes with whatever is shown of it
        bool open = false;        ///< its pictures have come (or long enough has gone by): voting
        std::uint8_t ready = 0;   ///< bit p: player p has all its pictures (the host's)

        /// Whether every one with a vote but `me` has their pictures.
        bool allReady(int me) const
        {
            const unsigned others = voters & ~(1u << me);
            return (ready & others) == others;
        }
        /// Player `p` chooses `slot`, in place of what they chose before.
        bool cast(int p, int slot)
        {
            if (p < 0 || p >= kNetPlayers || !((voters >> p) & 1) || slot < 0 || slot >= count || decided >= 0 ||
                choice[p] == slot)
                return false;
            drop(p);
            voters = static_cast<std::uint8_t>(voters | 1u << p);
            choice[p] = static_cast<std::int8_t>(slot);
            ++tally[slot];
            ++stamp;
            return true;
        }
        /// Player `p` has gone: no vote, and theirs does not count.
        void drop(int p)
        {
            if (p < 0 || p >= kNetPlayers)
                return;
            voters = static_cast<std::uint8_t>(voters & ~(1u << p));
            if (choice[p] >= 0)
            {
                --tally[choice[p]];
                ++stamp;
            }
            choice[p] = -1;
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
    };

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
} // namespace hldata
