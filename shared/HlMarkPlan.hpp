#pragma once

// Which of the marks on the walls (blood, holes, scorches) are drawn, when
// not all of them can be.
//
// See-through polygons one over another are what the hardware runs out of
// time on, so the marks have a share of the screen between them, and no
// more of them than a count. The nearest are drawn first. Each mark is
// either drawn whole or not at all: one that is not drawn costs nothing
// (it used to thin out at the edge of its range and of the share, and was
// sent, and paid for, while hardly seen).
//
// A mark's share is how much of the screen it is, about: from how far it is
// from the eye, not how far ahead -- a mark beside the eye at the edge of
// the view is a sliver on the screen, not the whole of it -- and no one
// mark more than a quarter of the share, so that the nearest never leaves
// the rest out. (It did: a stain on a corridor's wall at the edge of the
// view counted as the whole screen, and every other mark went until the
// view turned to it.)
//
// std-only and inline: the host tests run it (test_view).

#include <cstdint>

#include "HlFixed.hpp"

namespace hldata
{
    /// The screen's share of the marks, in pixels (a DSi; a DS less).
    inline constexpr int kMarkAreaDsi = 30000;
    inline constexpr int kMarkAreaDs = 18000;
    /// How much of a mark is drawn over its wall (of 31): the rest is the
    /// wall's own light through it -- a mark in a dark corridor is dark.
    inline constexpr int kMarkAlpha = 22;

    /// How far off a mark of `half` is still drawn (the largest of its
    /// distances along x, y and z): a hole not far, a scorch much farther.
    constexpr std::int32_t markRange(int half) { return half * 60 + 200; }

    /// Whether a mark `away` (as markRange measures it) is in range: one
    /// drawn the pass before keeps its place to the end of it, one new
    /// comes in a sixteenth nearer, so that one at the edge does not come
    /// and go as the player moves.
    constexpr bool markInRange(std::int32_t away, int half, bool shown)
    {
        const std::int32_t far = markRange(half);
        return away <= (shown ? far : far - far / 16);
    }

    /// About how much of the screen (pixels) a mark of `half` is, `distance`
    /// whole units from the eye, seen at `cosine` (4096ths: square on is
    /// 4096) -- its width, 128 pixels to a unit at a unit's distance, by its
    /// height, less as it is seen edge on -- and never more than a quarter
    /// of `share`.
    constexpr int markArea(int half, std::int32_t distance, int cosine, int share)
    {
        const std::int32_t d = distance < 16 ? 16 : distance;
        const int across = static_cast<int>((half * 256) / d);
        const int wide = across > 256 ? 256 : across;
        cosine = cosine < 0 ? 0 : (cosine > 4096 ? 4096 : cosine);
        int high = (across * cosine) >> 12;
        high = high > 192 ? 192 : (high < 1 ? 1 : high);
        const int area = wide * high;
        return area > share / 4 ? share / 4 : area;
    }

    /// How see-through a mark is drawn (of 31), with `leftMs` of its life
    /// left: as it is, then fading over its last three seconds.
    constexpr int markAlpha(std::int32_t leftMs)
    {
        return leftMs >= 3000 ? kMarkAlpha : 1 + static_cast<int>((leftMs * (kMarkAlpha - 1)) / 3000);
    }

    /// How much nearer the eye (of 4096) to draw something lying on a wall
    /// seen at `cosine` (4096ths: square on is 4096), each corner along its
    /// own line of sight (the same pixels), with the ordinary depth test:
    /// by as much as the depth changes across two pixels there and 1/128 of
    /// the distance more. Seen at a low angle the "same depth" test eats it
    /// in patches -- a pixel there spans more depth than the test allows
    /// (tools/hwtest/markpull, lamppatch). What it costs: it shows over what
    /// stands that close in front of its wall.
    inline int markPull(int cosine)
    {
        // tan = sin / cos (a pixel is 1/128 of the distance across; the
        // depth changes by that times the tangent).
        const int c = cosine < 82 ? 82 : (cosine > 4096 ? 4096 : cosine);
        const int sine = static_cast<int>(isqrt64(static_cast<std::uint64_t>(4096 * 4096 - c * c)));
        const int tangent = sine * 4096 / c;
        const int pull = (2 * tangent + 4096) / 128;
        return pull < 16 ? 16 : (pull > 2048 ? 2048 : pull);
    }

    /// A mark facing the eye, in range and in the view: what the choice
    /// needs to know of it.
    struct MarkCandidate
    {
        std::uint8_t slot = 0;    ///< its place in ActorWorld::decals
        bool shown = false;       ///< drawn the pass before
        std::int32_t near = 0;    ///< how near (any measure: the nearest first)
        int area = 0;             ///< markArea
    };

    /// Of `count` candidates, which are drawn: the nearest first, no more
    /// than `most` of them, while what they cover fits `share` -- one drawn
    /// the pass before to the end of it, one new only with a tenth of it to
    /// spare (at the edge, a mark does not come and go as the view turns).
    /// One that does not fit leaves room for a smaller one after it.
    /// Sorts `list` nearest first; the chosen are marked by `drawn` (a bit
    /// by their place in `list`). Returns how many.
    inline int markChoose(MarkCandidate *list, int count, int most, int share, std::uint64_t &drawn)
    {
        for (int i = 1; i < count; ++i)
        {
            const MarkCandidate one = list[i];
            int at = i;
            while (at > 0 && list[at - 1].near > one.near)
            {
                list[at] = list[at - 1];
                --at;
            }
            list[at] = one;
        }
        drawn = 0;
        int chosen = 0, covered = 0;
        for (int i = 0; i < count && chosen < most; ++i)
        {
            const MarkCandidate &one = list[i];
            const int room = one.shown ? share : share - share / 10;
            if (covered + one.area > room)
                continue;
            covered += one.area;
            drawn |= std::uint64_t(1) << i;
            ++chosen;
        }
        return chosen;
    }
} // namespace hldata
