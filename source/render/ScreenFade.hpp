#pragma once

#include "EngineObject.hpp"

// The top screen's brightness, which three things want a say in: a fade the
// map has asked for, the dimness of being under water, and the curtain that
// is down while a map is loaded (the 3D picture left on the screen is drawn
// again every frame out of video memory, and video memory is being written
// over). One owner, so that none of them undoes another: the darkest wins.
//
// A fade is the game's env_fade: a colour over the picture, by how much of
// 255. Into any colour but white it is a see-through quad of that colour,
// the last polygon of the 3D picture, in front of everything in it (the
// weapon in hand too). The HUD and the glows, sprites added to the picture,
// are under it as well: what of them is added is cut by as much as the fade
// covers (the sprites' blend, set in the blank the picture is shown from).
// The writing the map shows (TopText) is in front of it, read on the colour
// as the game shows it: not added while the quad is up. Into white -- or
// black, with no quad to be had (a frozen picture: FrameFreeze) -- it is
// the hardware's brightness, over everything on the screen. The hardware
// has 16 steps of either; the steps between them are made by showing the
// one below and the one above in turn, frame by frame, as often as each is
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
        /// The fade's quad is up: the sprites' blend is its own (whoever
        /// sets it leaves it be), and the writing is not added.
        bool layered() const { return layer_; }
        /// The 3D picture is going (FrameFreeze): the quad goes with it, and
        /// comes back with the next tick() that wants it.
        void layerLost();
        /// The quad, if there is one: last in the picture being drawn.
        void drawQuad();
        /// The picture has been handed to the hardware: the sprites' blend
        /// goes with it, from the blank it is shown from.
        void frameDrawn();
        /// (From the vertical blank.)
        void onVblank();

    private:
        int fade_ = 0;
        std::uint16_t colour_ = 0;
        bool dim_ = false;
        bool curtain_ = false;
        int wait_ = 0;
        int shown_ = 0;
        int carried_ = 0; ///< what is owed of a step between two (of 256): the next frame's
        bool layer_ = false;
        // The quad as tick() wants it; as drawn into the picture on its way
        // (drawQuad); and the sprites' share left (EVA, 0..16; -1: no fade)
        // waiting for, and as of, the blank.
        int quadAlpha_ = 0;
        std::uint16_t quadColour_ = 0;
        int eva_ = -1;
        int drawnAlpha_ = 0;
        int drawnEva_ = -1;
        volatile int pendingEva_ = -1;
        volatile bool pending_ = false;
        int shownEva_ = -1;
        /// Show it all; `advance`: a new picture, so the steps between two
        /// move on.
        void apply(bool advance);
        /// Whether the colour's quad can be had now.
        bool layerUsable() const;
        void showLayer(int step);
        void hideLayer();
    };

    HL_ENGINE_OBJECT(ScreenFade, screenFade);
} // namespace hlrt
