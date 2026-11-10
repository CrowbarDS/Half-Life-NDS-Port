#include "game/Input.hpp"

#include <nds.h>

#include "Global.hpp"
#include "game/Player.hpp"
#include "Settings.hpp"
#include "audio/Sound.hpp"
#include "net/NetGame.hpp"
#include "render/BottomScreen.hpp"

namespace hlrt
{
    Input input;

    // (HlControls.hpp names the buttons by the bits of the key register.)
    static_assert(hldata::kButtonA == KEY_A && hldata::kButtonB == KEY_B && hldata::kButtonX == KEY_X &&
                  hldata::kButtonY == KEY_Y && hldata::kButtonL == KEY_L && hldata::kButtonR == KEY_R &&
                  hldata::kButtonUp == KEY_UP && hldata::kButtonDown == KEY_DOWN &&
                  hldata::kButtonLeft == KEY_LEFT && hldata::kButtonRight == KEY_RIGHT &&
                  hldata::kButtonSelect == KEY_SELECT && hldata::kButtonStart == KEY_START);

    // The stylus finer than a pixel, by the firmware's own calibration (two
    // points, raw and on the screen, for each axis); and how hard it is
    // pressed, as the touch controller's datasheet has it: the plate's
    // resistance is the raw X times (Z2 / Z1 - 1) -- the firmer, the less.
    // A reading the calibration cannot be trusted for (a DSi's own touch
    // mode reads otherwise), or that has no Z, is left at what it was: a
    // pixel, and no pressure.
    void Input::fine(int rawX, int rawY, int z1, int z2, hldata::ControlsIn &in)
    {
        const PERSONAL_DATA &data = *PersonalData;
        const int spanX = data.calX2 - data.calX1, spanY = data.calY2 - data.calY1;
        const int pxX = data.calX2px - data.calX1px, pxY = data.calY2px - data.calY1px;
        if (spanX != 0 && spanY != 0 && pxX != 0 && pxY != 0)
        {
            const int fx = ((rawX - data.calX1) * pxX * 16) / spanX + (data.calX1px - 1) * 16;
            const int fy = ((rawY - data.calY1) * pxY * 16) / spanY + (data.calY1px - 1) * 16;
            // (Within half a dozen pixels of where the console says it is.)
            if (fx >= 0 && fy >= 0 && fx / 16 - in.touchX < 6 && in.touchX - fx / 16 < 6 &&
                fy / 16 - in.touchY < 6 && in.touchY - fy / 16 < 6)
            {
                in.fineX = fx;
                in.fineY = fy;
            }
        }
        notePressure(z1, z2);
        if (z1 > 0 && z2 > z1 && rawX > 0)
        {
            const int pressure = static_cast<int>((static_cast<std::int64_t>(rawX) * (z2 - z1)) / z1);
            in.pressure = pressure > 0 ? pressure : 1;
        }
    }

    HL_ARM void Input::poll(bool playing)
    {
        scanKeys();
        // What is HELD, and never keysDown():
        // keysDown() only reports what changed since the
        // LAST scan, whoever made it. The edges are worked out from this.
        const std::uint32_t held = keysHeld();
        hldata::ControlsIn in;
        in.held = held;
        in.blank = Global::vblanks();
        // (A touch the menu had, still down: as though it were not.)
        if (touchHeld_ && !(held & KEY_TOUCH))
            touchHeld_ = false;
        if (touchHeld_)
            in.held &= ~static_cast<std::uint32_t>(KEY_TOUCH);
        else if (held & KEY_TOUCH)
        {
            touchPosition touch;
            touchRead(&touch);
            in.touching = true;
            in.touchX = touch.px;
            in.touchY = touch.py;
            fine(touch.rawx, touch.rawy, touch.z1, touch.z2, in);
        }
        // A slot's list of weapons first: a touch that is the list's is not
        // the controls'.
        const bool longJump = (player.arsenal.have & hldata::kArsenalLongJump) != 0;
        const hldata::TouchLayout at = hldata::touchLayout(settings.leftHanded, netGame.playing(), settings.jumpWalks, longJump);
        const bool landed = in.touching && !touchWas_;
        touchWas_ = in.touching;
        const hldata::WeaponListTouch list =
            playing ? hldata::weaponListStep(weaponList_, at, in.touching, landed, in.touchX, in.touchY, in.blank)
                    : hldata::WeaponListTouch();
        hldata::ControlsIn theirs = in;
        if (list.consumed)
            theirs.touching = false;
        hldata::ControlSettings controls = settings.controls();
        controls.match = netGame.playing();
#ifndef HLDS_CHILD
        controls.talk = netGame.talkAllowed();
#endif
        controls.longJump = longJump;
        static_cast<hldata::ControlsOut &>(*this) = hldata::controlsStep(controls, theirs, state_);
        weaponChoose = list.chosen;
        // (A menu over the game: its touches are the menu's, and no weapon
        // is asked for nor heard.)
        if (!playing)
        {
            weaponPick = weaponStep = 0;
            return;
        }
        // As the game's weapon menu sounds (cl_dll/ammo.cpp): along it, away.
        if (list.moved)
            sound.ui(hldata::kUiSoundMove, 127);
        if (list.dismissed)
            sound.ui(hldata::kUiSoundHudOff, 127);
        // A slot touched: its list up (or, with one weapon in it, that one),
        // and the menu's sound of coming up.
        if (weaponPick != 0)
        {
            weaponChoose = hldata::weaponListOpen(weaponList_, weaponPick, bottomScreen.have(), bottomScreen.empty(), in.blank);
            weaponList_.owned = weaponList_.open();
            if (weaponList_.open() || weaponChoose != 0)
                sound.ui(hldata::kUiSoundHudOn, 127);
            weaponPick = 0;
        }
        // (The one in hand, taken again, is no change.) A weapon asked for:
        // the game's sound of a choice -- on asking, not when it is drawn.
        if (weaponChoose == bottomScreen.weapon())
            weaponChoose = 0;
        if (weaponChoose != 0 || weaponStep != 0)
            sound.ui(hldata::kUiSoundWeapon, 127);
    }
} // namespace hlrt
