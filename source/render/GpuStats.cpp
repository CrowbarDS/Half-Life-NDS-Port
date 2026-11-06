#include "render/GpuStats.hpp"

#include <cstdio>

#include <nds.h>

#include "Global.hpp"

namespace hlrt
{
    GpuStats gpuStats;

    namespace
    {
        constexpr std::uint16_t kUnderflow = 1 << 12;
        constexpr std::uint16_t kOverflow = 1 << 13;

        // Written by the interrupt, taken by frame().
        volatile std::uint8_t seenUnderflow = 0;
        volatile std::uint8_t seenOverflow = 0;
        volatile std::uint8_t seenLines = 46;
    } // namespace

    void GpuStats::onVblank()
    {
        const std::uint16_t control = GFX_CONTROL;
        if (control & (kUnderflow | kOverflow))
        {
            if (control & kUnderflow)
                seenUnderflow = 1;
            if (control & kOverflow)
                seenOverflow = 1;
            // Writing a set bit back clears it; the rest is written as read.
            GFX_CONTROL = control;
        }
        const std::uint8_t lines = static_cast<std::uint8_t>(GFX_RDLINES_COUNT & 0x3F);
        if (lines < seenLines)
            seenLines = lines;
    }

    void GpuStats::frame(int polygons, int vertices, int vblanks)
    {
        const int state = enterCriticalSection();
        last_.underflow = seenUnderflow != 0;
        last_.overflow = seenOverflow != 0;
        last_.lines = seenLines;
        seenUnderflow = 0;
        seenOverflow = 0;
        seenLines = 46;
        leaveCriticalSection(state);

        // A second and more is not a slow frame: it is a map being loaded.
        if (vblanks > 60)
            vblanks = 1;
        if (last_.lines < lowLines_)
            lowLines_ = last_.lines;
        ++map_.frames;
        map_.underflows += last_.underflow ? 1 : 0;
        map_.overflows += last_.overflow ? 1 : 0;
        if (last_.lines < map_.lowLines)
            map_.lowLines = last_.lines;
        if (polygons > map_.polygons)
            map_.polygons = polygons;
        if (vertices > map_.vertices)
            map_.vertices = vertices;
        if (vblanks > map_.vblanks)
            map_.vblanks = vblanks;
        if (vblanks > 1)
            ++map_.slow;
    }

    int GpuStats::takeLowLines()
    {
        const int low = lowLines_;
        lowLines_ = 46;
        return low;
    }

    void GpuStats::beginMap()
    {
        map_ = Totals{};
        lowLines_ = 46;
    }

    int GpuStats::under95(const std::uint32_t *counts, std::uint32_t passes, int step)
    {
        std::uint32_t seen = 0;
        for (int b = 0; b < kBuckets; ++b)
        {
            seen += counts[b];
            if (seen * 20 >= passes * 19)
                return (b - 16 + 1) * step;
        }
        return (kBuckets - 16) * step;
    }

    bool GpuStats::writeLog(const char *map, const char *mode) const
    {
        if (!global.hasFat || map_.frames == 0)
            return false;
        std::FILE *file = std::fopen(global.dataPath("perf.txt").c_str(), "a");
        if (file == nullptr)
            return false;
        std::fprintf(file,
                     "%s %s %s passes %lu slow %lu longest %d poly %d vert %d lines %d "
                     "underflow %d overflow %d cut-world %lu cut-brush %lu pressed %lu enemies-cut %lu enemies-seen-cut %lu effects-refused %lu models-hidden %lu "
                     "poly-mean %lu surfaces-cut-mean %lu.%02lu surfaces-cut-most %d detail-cut-mean %lu brushes-dropped-mean %lu.%02lu "
                     "brushes-dropped-most %d brushes-overflow %lu enemies-dropped %lu heap-least %luK "
                     "excess-poly-most %d excess-poly-95 %d excess-vert-most %d excess-vert-95 %d full-poly-most %d full-vert-most %d\n",
                     map, global.isDSi ? "DSi" : "DS", mode,
                     static_cast<unsigned long>(map_.frames), static_cast<unsigned long>(map_.slow),
                     map_.vblanks, map_.polygons, map_.vertices, map_.lowLines, map_.underflows,
                     map_.overflows, static_cast<unsigned long>(map_.cut), static_cast<unsigned long>(map_.brushCut),
                     static_cast<unsigned long>(map_.pressed), static_cast<unsigned long>(map_.enemiesCut),
                     static_cast<unsigned long>(map_.enemiesSeenCut),
                     static_cast<unsigned long>(map_.effectsRefused), static_cast<unsigned long>(map_.modelsHidden),
                     static_cast<unsigned long>(map_.polygonsSum / map_.frames),
                     static_cast<unsigned long>(map_.surfacesCutSum / map_.frames),
                     static_cast<unsigned long>((map_.surfacesCutSum % map_.frames) * 100 / map_.frames),
                     map_.surfacesCutMost, static_cast<unsigned long>(map_.detailCutSum / map_.frames),
                     static_cast<unsigned long>(map_.brushesDroppedSum / map_.frames),
                     static_cast<unsigned long>((map_.brushesDroppedSum % map_.frames) * 100 / map_.frames),
                     map_.brushesDroppedMost, static_cast<unsigned long>(map_.brushesOverflowSum),
                     static_cast<unsigned long>(map_.enemiesCutTotal),
                     static_cast<unsigned long>(map_.heapLeast == 0xFFFFFFFFu ? 0 : map_.heapLeast / 1024),
                     map_.excessPolygonsMost, under95(map_.excessPolygons, map_.frames, kPolygonStep),
                     map_.excessVerticesMost, under95(map_.excessVertices, map_.frames, kVertexStep),
                     map_.fullPolygonsMost, map_.fullVerticesMost);
        return std::fclose(file) == 0;
    }
} // namespace hlrt
