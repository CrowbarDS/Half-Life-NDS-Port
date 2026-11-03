#pragma once

// What the 3D hardware says about the frames it was given.
//
// The renderer keeps to a budget of its own (WorldDraw), but that is a count
// made on the CPU before clipping. The hardware has the last word, in three
// places:
//
//  - the polygon and vertex RAM counters: what a frame really used;
//  - DISP3DCNT bit 13: a frame asked for more than the 2048 polygons or 6144
//    vertices there are, and the rest of it was dropped;
//  - DISP3DCNT bit 12 and RDLINES_COUNT: the rasteriser draws 48 lines ahead
//    of the screen, and a line with too much on it eats into that lead. The
//    counter is the smallest lead of the last frame (46 is a frame with time
//    to spare); the bit says it ran out, and lines were shown unfinished.
//
// Both bits stay set until they are written to, so they are read and cleared
// once a frame, in the vertical blank. An emulator may not model the line
// buffer at all: the numbers that matter are a console's, which is why they
// can be written to the card (perf.txt) as well as shown.

#include <cstdint>

namespace hlrt
{
    class GpuStats
    {
    public:
        /// What the hardware reported since the last call of frame().
        struct Frame
        {
            bool underflow = false; ///< lines were shown unfinished
            bool overflow = false;  ///< polygons or vertices were dropped
            int lines = 46;         ///< the smallest lead, 0..46
        };

        /// From the vertical blank interrupt.
        static void onVblank();

        /// Once per pass, after it has been sent: gathers what the interrupt
        /// saw, with what the pass used and how many blanks it took.
        void frame(int polygons, int vertices, int vblanks);
        /// What the view cost a pass: whether it was cut off at a distance
        /// (and fogged there), and whether small things were being left out.
        void view(bool cut, bool brushCut, bool pressed)
        {
            map_.cut += cut;
            map_.brushCut += brushCut;
            map_.pressed += pressed;
        }
        /// What a pass left out that should not be: enemies in view not
        /// drawn, and effects (blood, a flash) in view not taken.
        void losses(int enemies, int effects, int seenEnemies = 0, int hidden = 0)
        {
            map_.enemiesCutTotal += static_cast<std::uint32_t>(enemies);
            map_.modelsHidden += static_cast<std::uint32_t>(hidden);
            map_.enemiesCut += enemies != 0;
            map_.enemiesSeenCut += seenEnemies != 0;
            map_.effectsRefused += static_cast<std::uint32_t>(effects);
        }
        /// What the pass sent of the world and left out of it, and the heap
        /// still free: totals and worsts for the measurements (perf.txt).
        void world(int polygons, int surfacesCut, int brushesDropped, int brushesOverflow, std::uint32_t heapFree)
        {
            map_.polygonsSum += static_cast<std::uint32_t>(polygons);
            map_.surfacesCutSum += static_cast<std::uint32_t>(surfacesCut);
            map_.brushesDroppedSum += static_cast<std::uint32_t>(brushesDropped);
            map_.brushesOverflowSum += static_cast<std::uint32_t>(brushesOverflow);
            if (surfacesCut > map_.surfacesCutMost)
                map_.surfacesCutMost = surfacesCut;
            if (brushesDropped > map_.brushesDroppedMost)
                map_.brushesDroppedMost = brushesDropped;
            if (heapFree < map_.heapLeast)
                map_.heapLeast = heapFree;
        }
        const Frame &last() const { return last_; }

        /// The lowest lead since this was last asked (the overlay asks).
        int takeLowLines();
        int underflows() const { return map_.underflows; }
        int overflows() const { return map_.overflows; }

        /// Start counting again, for a new map.
        void beginMap();
        /// Add a line about the map so far to perf.txt on the card.
        bool writeLog(const char *map, const char *mode) const;

    private:
        struct Totals
        {
            std::uint32_t frames = 0;
            int underflows = 0;
            int overflows = 0;
            int lowLines = 46;
            int polygons = 0; ///< the most in one pass
            int vertices = 0;
            int vblanks = 0;  ///< the longest pass
            std::uint32_t slow = 0; ///< passes that took more than one blank
            std::uint32_t cut = 0;     ///< passes cut off at a distance
            std::uint32_t brushCut = 0; ///< passes with a brush entity left out for room
            std::uint32_t pressed = 0; ///< passes with the small and far left out
            std::uint32_t enemiesCut = 0;     ///< passes with an enemy in view not drawn
            std::uint32_t enemiesSeenCut = 0; ///< ... one not behind a wall
            std::uint32_t modelsHidden = 0;   ///< models in view behind a wall, not drawn (all passes)
            std::uint32_t effectsRefused = 0; ///< effects in view not taken, in all
            std::uint32_t enemiesCutTotal = 0; ///< enemies in view not drawn, in all passes
            // (world(): sums over the passes, the worst of one pass.)
            std::uint32_t polygonsSum = 0;
            std::uint32_t surfacesCutSum = 0;
            std::uint32_t brushesDroppedSum = 0;
            std::uint32_t brushesOverflowSum = 0;
            int surfacesCutMost = 0;
            int brushesDroppedMost = 0;
            std::uint32_t heapLeast = 0xFFFFFFFFu;
        };
        Frame last_;
        Totals map_;
        int lowLines_ = 46;
    };

    extern GpuStats gpuStats;
} // namespace hlrt
