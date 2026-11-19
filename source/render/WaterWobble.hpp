#pragma once

// The picture wobbling while the eyes are under water, as the game's
// software renderer warps it (Quake's D_WarpScreen, which it kept): each line
// of the picture moved across by a sine of its height and of the time --
// once round in 128 lines, the sine moving on 20 lines a second.
//
// The game's warp moves each column up and down too; the DS's 3D layer has
// only a horizontal scroll, so here it is the lines' half of it, by up to two
// pixels either way (the game's three, at 320 across). The edge left by a
// moved line shows the backdrop for those pixels.
//
// A DMA started by each horizontal blank writes the 3D layer's horizontal
// scroll (BG0HOFS) from a table of the frame's 192 lines, armed again in
// each vertical blank: no interrupt and nothing of the processor's per line.
// It is on DMA 1. A DSi's only: on a DS the geometry FIFO is fed by DMA 0 in
// the frame, which in the emulator holds DMA 1 back past its line (the
// effects probe, tools/hwtest/fxprobe, measures it on the console).

namespace hlrt
{
    class WaterWobble
    {
    public:
        /// Once a game frame: whether the eyes are under water. Stops by
        /// itself once it is not called (a menu, a map loading).
        void frame(bool under);
        /// From the vertical blank interrupt.
        void onVblank();

    private:
        volatile int fresh_ = 0;   ///< vertical blanks it stays armed for without frame()
        volatile int table_ = -1;  ///< which table the next frame shows (-1: none)
        bool armed_ = false;
    };

    extern WaterWobble waterWobble;
} // namespace hlrt
