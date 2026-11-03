// Which brush entities a pass draws (shared/HlBrushPlan.hpp): the solid ones
// nearest first, one that has no room left out and those behind it still
// drawn if they fit; the
// see-through ones farthest first; and, more of them in view than a list
// holds, the farthest left out.

#include <cstdio>

#include "HlBrushPlan.hpp"
#include "HlPolygonIds.hpp" // (its static checks: no two of its IDs overlap)
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    void testNearFirst()
    {
        BrushList<8, true> solid;
        // A far brush first in the map, the near door after it.
        solid.add(1, 900 * 900, 300, 900);
        solid.add(2, 100 * 100, 300, 900);
        check(solid.count == 2 && solid.index[0] == 2 && solid.index[1] == 1, "solid brushes are listed nearest first");
        // Room for one of them: the near door.
        bool take[8] = {};
        const int fit = brushesThatFit(solid, 400, 2000, take);
        check(fit == 1 && take[0] && !take[1] && solid.index[0] == 2,
              "with room for one, the near door is drawn and the far brush is not");
        // A small far one after a big near one that does not fit: drawn --
        // one costly thing left out does not take everything behind it.
        solid.clear();
        solid.add(3, 50 * 50, 900, 2000);
        solid.add(4, 800 * 800, 10, 30);
        check(brushesThatFit(solid, 500, 5000, take) == 1 && !take[0] && take[1],
              "... and a small one behind a near one that does not fit is still drawn");
        // Many in a row that do not fit: the rest are not tried.
        BrushList<32, true> many;
        for (int n = 0; n < 20; ++n)
            many.add(static_cast<std::uint16_t>(n), (n + 1) * 100, n < kBrushMisses ? 900 : 1, 3);
        bool all[32] = {};
        check(brushesThatFit(many, 500, 5000, all) == 0, "... until many in a row have not fitted");
    }

    void testOverflow()
    {
        BrushList<4, true> solid;
        int out = 0;
        for (int n = 0; n < 6; ++n)
        {
            // Farther and farther, then two near ones.
            const std::int32_t away = n < 4 ? (n + 1) * 1000 : 10 + n;
            if (solid.add(static_cast<std::uint16_t>(n), away, 10, 30).index != 0xFFFF)
                ++out;
        }
        bool nearKept = false, farthestOut = true;
        for (int k = 0; k < solid.count; ++k)
        {
            nearKept = nearKept || solid.index[k] == 5;
            farthestOut = farthestOut && solid.index[k] != 3 && solid.index[k] != 2;
        }
        check(solid.count == 4 && out == 2 && nearKept && farthestOut,
              "a full list of solid brushes leaves out the farthest, not the last met");
        BrushList<3, false> later;
        later.add(1, 500, 1, 1);
        later.add(2, 900, 1, 1);
        later.add(3, 100, 1, 1);
        check(later.index[0] == 2 && later.index[2] == 3, "see-through brushes are listed farthest first");
        later.add(4, 50, 1, 1);
        check(later.count == 3 && later.index[0] == 1 && later.index[2] == 4,
              "... and, full, leave out the farthest of them");
    }
} // namespace

int main()
{
    testNearFirst();
    testOverflow();
    if (g_failures == 0)
        std::printf("test_brushplan: ok\n");
    return g_failures == 0 ? 0 : 1;
}
