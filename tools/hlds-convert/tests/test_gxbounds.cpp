// The box the console works out round each surface from its own commands
// (shared/HlGxBounds.hpp), against the converter's own reading of them
// (decodeGxStream): the same vertices, so the same box. And the box's
// numbers fit the box test's 16 bits, on the game's own maps.
//
// The box room always; the game's maps with HLDS_VALVE_DIR.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "HlGxBounds.hpp"
#include "HlLevelReader.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/BspFile.hpp"
#include "hlds/Cooker.hpp"
#include "hlds/GxStream.hpp"
#include "hlds/LevelWriter.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    struct Tally
    {
        int surfaces = 0, tooWide = 0, outsideBall = 0;
        int widest = 0;
    };

    /// Every surface of a cooked level: its box as the console works it
    /// out, the converter's reading of the same words, and its ball.
    void checkLevel(const CookedLevel &level, const char *name, Tally &tally)
    {
        std::vector<std::uint8_t> bytes;
        std::string error;
        LevelView view;
        if (!writeLevel(level, bytes, error) ||
            view.open(bytes.data(), static_cast<std::uint32_t>(bytes.size())) != LevelError::Ok)
        {
            check(false, (std::string(name) + ": the level writes and opens").c_str());
            return;
        }
        bool same = true, inside = true;
        for (std::uint32_t s = 0; s < view.surfaceCount; ++s)
        {
            const Surface &surface = view.surfaces[s];
            const std::uint32_t *words = view.gxStream + surface.gxOfs;
            std::int16_t mins[3], maxs[3];
            std::vector<GxRun> runs;
            if (!decodeGxStream(words, surface.gxWords, runs, error))
            {
                check(false, "a surface's commands decode");
                continue;
            }
            int lo[3] = {32767, 32767, 32767}, hi[3] = {-32768, -32768, -32768};
            bool any = false;
            for (const GxRun &run : runs)
            {
                for (const GxVertex &v : run.vertices)
                {
                    const int p[3] = {v.x, v.y, v.z};
                    for (int i = 0; i < 3; ++i)
                    {
                        lo[i] = std::min(lo[i], p[i]);
                        hi[i] = std::max(hi[i], p[i]);
                    }
                    any = true;
                }
            }
            const bool got = gxStreamBounds(words, surface.gxWords, mins, maxs);
            if (got != any)
                same = false;
            if (!got)
                continue;
            ++tally.surfaces;
            for (int i = 0; i < 3; ++i)
            {
                same = same && mins[i] == lo[i] && maxs[i] == hi[i];
                tally.widest = std::max(tally.widest, maxs[i] - mins[i]);
                // (In quarter-units; the ball is in whole units, rounded
                // up, about a middle rounded to the unit: a unit of slack.)
                const int middle = surface.center[i] * 4, reach = (surface.radius + 1) * 4;
                if (mins[i] < middle - reach || maxs[i] > middle + reach)
                {
                    inside = false;
                    ++tally.outsideBall;
                }
            }
            if (!gxBoxFits(mins, maxs))
                ++tally.tooWide;
        }
        check(same, (std::string(name) + ": every surface's box is its vertices' box, as the converter reads them").c_str());
        check(inside, (std::string(name) + ": every box is inside its surface's ball").c_str());
    }

    void testBoxRoom()
    {
        BspFile bsp;
        std::string error;
        CookedLevel level;
        CookReport report;
        if (!bsp.parse(buildBoxRoom(BoxRoom()), error) || !cookLevel(bsp, CookOptions(), {}, level, report, error))
        {
            check(false, "the box room cooks");
            return;
        }
        Tally tally;
        checkLevel(level, "box room", tally);
        check(tally.surfaces == 6 && tally.tooWide == 0, "the box room: six walls, each a box that fits");
    }

    void testMap(const char *dir, const char *map)
    {
        BspFile bsp;
        std::string error;
        if (!bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
        {
            check(false, (std::string(map) + ".bsp loads").c_str());
            return;
        }
        CookedLevel level;
        CookReport report;
        if (!cookLevel(bsp, CookOptions(), TexturePlan(), level, report, error))
        {
            check(false, (std::string(map) + " cooks").c_str());
            return;
        }
        Tally tally;
        checkLevel(level, map, tally);
        std::printf("test_gxbounds: %s: %d surfaces, widest %d quarter-units, %d too wide for the box test\n", map,
                    tally.surfaces, tally.widest, tally.tooWide);
        check(tally.tooWide == 0, (std::string(map) + ": every box's size fits the box test's 16 bits").c_str());
    }
} // namespace

int main()
{
    testBoxRoom();
    if (const char *dir = std::getenv("HLDS_VALVE_DIR"); dir != nullptr && dir[0] != '\0')
    {
        for (const char *map : {"c1a0", "c1a2", "c2a5", "c2a5x", "c4a1"})
            testMap(dir, map);
    }
    else
    {
        std::printf("test_gxbounds: HLDS_VALVE_DIR not set, the game's maps were not read\n");
    }
    if (g_failures == 0)
        std::printf("test_gxbounds: ok\n");
    return g_failures == 0 ? 0 : 1;
}
