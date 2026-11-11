#pragma once

#include "EngineObject.hpp"

// The controls: what the buttons and the stylus mean is in
// shared/HlControls.hpp (two layouts, either hand, and which of the pad and
// the stylus turns and which moves); where things are on the touch screen is
// in shared/HlTouchLayout.hpp. This reads the hardware and asks them.
//
// poll() is called once per frame and leaves the result in plain fields, so
// nothing else touches the hardware's input registers.

#include <cstdint>

#include "HlControls.hpp"
#include "HlWeaponList.hpp"

namespace hlrt
{
    class Input : public hldata::ControlsOut
    {
    public:
        /// `playing`: the game is being played (not the title, nor the pause
        /// menu over it): only then is a touch a weapon's, and heard as one.
        void poll(bool playing = true);
        /// Nothing is held (the menu is up over a game that goes on).
        void quiet() { static_cast<hldata::ControlsOut &>(*this) = hldata::ControlsOut(); }
        /// Whether this console has said how hard the stylus presses (some
        /// do not: a DSi in its own touch mode, an emulator). Seen once, it
        /// is known.
        static bool pressureSeen() { return pressureSeen_; }
        /// The list of a slot's weapons, up over the pad or not (for the
        /// touch screen to draw).
        const hldata::WeaponList &weaponList() const { return weaponList_; }
        /// No list up (the menu over the game, a map loaded: it is not to be
        /// drawn again stale).
        void dropWeaponList() { weaponList_ = hldata::WeaponList(); }
        /// Back to the game from a menu: a touch still down then is the
        /// menu's (the tap that closed it), and is no one's until the
        /// stylus is lifted -- not a weapon picked, nor a look about.
        void holdTouch() { touchHeld_ = true; }
        static void notePressure(int z1, int z2)
        {
            if (z1 > 0 && z2 > z1)
                pressureSeen_ = true;
        }

    private:
        static void fine(int rawX, int rawY, int z1, int z2, hldata::ControlsIn &in);
        hldata::ControlsState state_;
        hldata::WeaponList weaponList_;
        bool touchWas_ = false;
        bool touchHeld_ = false;
        static inline bool pressureSeen_ = false;
    };

    HL_ENGINE_OBJECT(Input, input);
} // namespace hlrt
