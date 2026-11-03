#pragma once

// Which of the brush entities in view a pass draws (WorldDraw::planBrushes,
// drawBrushes), kept apart from the drawing so that it can be tried on the PC.
//
// The solid ones -- doors, lifts, trains, what moves and what must not be
// missing -- are listed nearest first and drawn in that order; once one does
// not fit, nothing farther is drawn either: a near door is never left out
// while a far brush is drawn. The see-through ones are listed farthest first,
// as they must be drawn. When there are more of either than a list holds,
// the farthest are the ones left out, not whichever came last in the map.

#include <cstdint>

namespace hldata
{
    /// What was left out by a BrushList add: nothing (`index` 0xFFFF), the
    /// new one, or the one it put out.
    struct BrushOut
    {
        std::uint16_t index = 0xFFFF;
        int polygons = 0, vertices = 0;
    };

    /// `kMost` brushes, by distance: nearest first (`nearFirst`), or farthest
    /// first.
    template <int kMost, bool nearFirst>
    struct BrushList
    {
        std::uint16_t index[kMost];
        std::int32_t distance[kMost];
        std::int16_t cost[kMost][2]; ///< polygons, vertices
        int count = 0;

        void clear() { count = 0; }

        using Out = BrushOut;

        /// In its place by distance. Full, the farthest of the list and it is
        /// left out.
        Out add(std::uint16_t brush, std::int32_t away, int polygons, int vertices)
        {
            Out out;
            if (count == kMost)
            {
                // The farthest there is: the last (nearest first) or the first.
                const int far = nearFirst ? count - 1 : 0;
                if (away >= distance[far])
                {
                    out.index = brush;
                    out.polygons = polygons;
                    out.vertices = vertices;
                    return out;
                }
                out.index = index[far];
                out.polygons = cost[far][0];
                out.vertices = cost[far][1];
                if (!nearFirst)
                {
                    for (int k = 0; k + 1 < count; ++k)
                        move(k + 1, k);
                }
                --count;
            }
            int at = count++;
            while (at > 0 && (nearFirst ? distance[at - 1] > away : distance[at - 1] < away))
            {
                move(at - 1, at);
                --at;
            }
            index[at] = brush;
            distance[at] = away;
            cost[at][0] = static_cast<std::int16_t>(polygons);
            cost[at][1] = static_cast<std::int16_t>(vertices);
            return out;
        }

    private:
        void move(int from, int to)
        {
            index[to] = index[from];
            distance[to] = distance[from];
            cost[to][0] = cost[from][0];
            cost[to][1] = cost[from][1];
        }
    };

    /// Which of a nearest-first list are drawn in `polygons` and `vertices`
    /// of room (`take[k]`), and how many: each in turn that fits. One that
    /// does not is left out and the rest are still tried -- a big near one
    /// left out does not take every small one behind it with it -- until
    /// this many in a row have not fitted.
    inline constexpr int kBrushMisses = 16;
    template <class List>
    inline int brushesThatFit(const List &list, int polygons, int vertices, bool *take)
    {
        int drawn = 0, misses = 0;
        for (int k = 0; k < list.count; ++k)
        {
            take[k] = misses < kBrushMisses && list.cost[k][0] <= polygons && list.cost[k][1] <= vertices;
            if (!take[k])
            {
                ++misses;
                continue;
            }
            misses = 0;
            polygons -= list.cost[k][0];
            vertices -= list.cost[k][1];
            ++drawn;
        }
        return drawn;
    }
} // namespace hldata
