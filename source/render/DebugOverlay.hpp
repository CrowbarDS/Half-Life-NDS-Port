#pragma once

// The text readout over the 3D picture: frame rate, what the renderer chose
// and what the hardware made of it.
//
// It prints ONE ROW at a time, a few frames apart, and that is not a nicety:
// the libnds console costs about 13 ms for the whole readout, most of a frame.
// Printed every frame it halved the frame rate it was reporting; printed all
// at once it would still cost a frame now and then.

namespace hlrt
{
    class DebugOverlay
    {
    public:
        void setMapName(const char *name) { mapName_ = name; }
        /// Call once per frame, after the frame's drawing has been submitted.
        void frame();

    private:
        static constexpr int kInterval = 6; ///< frames between rows
        const char *mapName_ = "";
        int countdown_ = 0;
        int row_ = 0;
        bool hblank_ = false; ///< the HBlank interrupt on, for the CPU meter
    };

    extern DebugOverlay debugOverlay;
} // namespace hlrt
