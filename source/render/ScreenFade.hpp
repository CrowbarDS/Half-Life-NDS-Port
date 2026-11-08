#pragma once

#include "EngineObject.hpp"

// The top screen's brightness, which three things want a say in: a fade the
// map has asked for, the dimness of being under water, and the curtain that
// is down while a map is loaded (the 3D picture left on the screen is drawn
// again every frame out of video memory, and video memory is being written
// over). One owner, so that none of them undoes another: the darkest wins.
//
// A fade is the game's env_fade: a colour over the picture, by how much of
// 255. Into any colour but white it is a layer of that one colour (BG1, out
// of Bank D, which is otherwise only the frozen picture's) blended over the
// 3D picture and the HUD, which are behind it (BG0 and the HUD's sprites at
// priority 1, BG1 at 0); the writing the map shows (TopText, priority 0) is
// in front of it, read on the black as the game shows it. Into white -- or
// black, with no layer to be had (a frozen picture; a DS sent the game) --
// it is the hardware's brightness, over everything on the screen. The hardware
// has 16 steps of either; the steps between them are made by showing the one
// below and the one above in turn, frame by frame, as often as each is
// nearer.

#include <cstdint>

namespace hlrt
{
    class ScreenFade
    {
    public:
        /// How far the map has the screen faded: 0 (not at all) to 255 (all
        /// of it), into `colour` (RGB15). Shown from the next tick().
        void setFade(int alpha, std::uint16_t colour);
        void setDim(bool under);
        /// Down: black, whatever else (and the map's fade is over: the
        /// next map, or the title, starts without it). `frames`, when it
        /// is let up: how many more pictures are to be handed to the
        /// hardware first (see tick()), so that the first thing seen is a
        /// whole new one.
        void curtainDown();
        void curtainUp(int frames = 0);
        /// Once for each picture handed to the hardware.
        void tick();
        bool curtained() const { return curtain_; }
        /// The fade's layer is up: the blend is its own (whoever sets the
        /// sprites' blend leaves it be -- set in the middle of a picture,
        /// the layer would show whole down to where it is set back).
        bool layered() const { return layer_; }
        /// Bank D has been taken (FrameFreeze): the colour's layer is gone,
        /// and is set up again by the next tick() that wants it.
        void layerLost();

    private:
        int fade_ = 0;
        std::uint16_t colour_ = 0;
        bool dim_ = false;
        bool curtain_ = false;
        int wait_ = 0;
        int shown_ = 0;
        int carried_ = 0; ///< what is owed of a step between two (of 256): the next frame's
        bool layer_ = false;
        int blend_ = -1; ///< the layer's EVA as last set
        /// Show it all; `advance`: a new picture, so the steps between two
        /// move on.
        void apply(bool advance);
        /// Whether the colour's layer can be had now.
        bool layerUsable() const;
        void showLayer(int eva);
        void hideLayer();
    };

    HL_ENGINE_OBJECT(ScreenFade, screenFade);
} // namespace hlrt
