#pragma once

#include "EngineObject.hpp"

// The player on the console: the shared movement code (HlPlayerMove.hpp) fed
// from the DS's input, and the camera placed at its eyes.
//
// The movement itself is not here. It lives in shared/ so the host tests run
// it; this class only gathers what it needs each frame -- the keys, the view
// direction, how much time has passed -- and is the one translation unit that
// instantiates it for the ARM9 (PlayerMove.itcm.arm.cpp).
//
// TIME STEP. The move is run once per rendered frame with the real time that
// frame took, in whole vblanks. Half-Life's movement is written for a variable
// step, and stepping with the frame keeps the view smooth at 60 fps without
// interpolating between ticks; the split gravity step keeps a jump the same
// height at 30.

#include "HlFrameClock.hpp"
#include "HlPlayerMove.hpp"
#include "HlView.hpp"

namespace hlrt
{
    class Player
    {
    public:
        hldata::PlayerState state;
        /// What they carry of the weapons that came later (state.arsenal
        /// points here): spawn() empties it, as it empties the rest; a level
        /// change keeps it (main.cpp's loadLevel), a save has it.
        hldata::Arsenal arsenal;
        /// What moves the view besides the stylus, and what being hurt
        /// shows (shared/HlView.hpp).
        hldata::ViewState view;
        /// Free flight through walls, for looking at a map (the command line's "noclip").
        bool noclip = false;

        /// Stand at the loaded level's player start.
        void spawn();
        /// A development aid: the track of music debug_start.txt asks for, or -1.
        int debugTrack = -1;
        int debugDieMs = -1; ///< debug_start.txt: until the player dies of it (ms); -1: they do not

        /// Turn the view, move (against Game::world), and put the camera at the
        /// eyes. `vblanks` is how many sixtieths of a second this frame covers.
        /// Called by Game::frame, after the entity logic has run.
        void frame(int vblanks);

        /// The view turned by the stylus, and nothing else (a frame the
        /// game's slowed time leaves no time in: host_timescale).
        void turn();

    private:
        void fly(int vblanks);
        /// The frame's milliseconds, as the game's own clock has them: fed
        /// the same sixtieths each frame, it gives the same.
        hldata::FrameClock clock_;
    };

    HL_ENGINE_OBJECT(Player, player);
} // namespace hlrt
