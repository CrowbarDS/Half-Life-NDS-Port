#pragma once

// What the buttons and the stylus mean: from what is held this frame to what
// the player is doing.
//
// Two layouts, chosen in the options.
//
// CLASSIC is the way DS first-person shooters have it: the stylus turns the
// view, the D-pad moves, and A B X Y do what the D-pad does, for whichever
// thumb is free. One shoulder button fires, the other is the weapon's second
// use. Everything else is on the touch screen (HlTouchLayout.hpp).
//
// MODERN keeps the D-pad and the shoulders and gives the four face buttons
// work of their own: the top one the next weapon, the left one a reload, the
// bottom one a crouch (held, as the game's), the right one a jump. With the
// second shoulder button held, down on the pad crouches -- and stays
// crouched -- and up stands. One short tap of the stylus on the aim pad is
// USE.
//
// CUSTOM is the player's own: any action on any button (Start is always the
// menu, so nobody can lose the way out). Each layout is a table of which
// buttons each action is (defaultKeys); a custom one starts as a copy of the
// layout it was made from.
//
// THE WALK. Half-Life runs unless told otherwise: there is a WALK on the touch
// screen, held or touched once to walk and again to run, and an action for a
// custom layout. No button of the other two is free for it; but as two taps
// jump anyway, the player can ask for the jump button to walk instead: the
// screen's, and MODERN's.
//
// THE LONG JUMP. With the module, it is a crouch and a jump at once while
// moving: on the pad and two taps, not to be done with one stylus. So the screen
// has LONG JUMP then: a touch is both, and the crouch is held a moment longer
// however short the touch, for the legs to come up.
//
// Either way, two quick taps on the aim pad jump (and holding the second
// keeps the jump held, for swimming up); a touch that starts on a control is
// that control and never turns the view, however far it then slides.
//
// THE HAND. "Pad" above is the D-pad for someone with the stylus in their
// right hand; with it in the left, the pad is A B X Y -- by where they are:
// X up, B down, Y left, A right -- the D-pad has the face buttons' work, and
// the shoulders change places: the hand that holds the console is the one
// that moves and fires.
//
// THE STICKS. The pad and the stylus are two sticks, and which does what can
// be changed an axis at a time: the pad's left and right can turn the view
// while the stylus's sideways travel sidesteps, and its up and down can look
// while the stylus's moves. A stylus that moves is a stick: how far it is
// from where it came down, past a dead zone, is the direction.
//
// THE STYLUS, FINELY. Where the console can say so, a touch is read finer than
// a pixel (its raw reading, by the firmware's calibration: sixteenths of a
// pixel) and with how hard it is pressed. A light touch -- the stylus landing
// or leaving, a palm brushing -- reads wrong, so a touch lighter than the
// firmest of its stroke by more than the "firmness" setting allows is left
// out (which is what takes away the jump as it lifts); and two readings are
// averaged, the firmer counting for more. Pressed hard on the aim pad, it can be an action of its
// own (the "firm press"). Where the console cannot say how hard (a DSi in its
// own touch mode; an emulator), none of this is done: pixels, as before.
//
// std-only and inline: the host tests run it (test_controls).

#include <cstdint>
#include <cstdio>

#include "HlTouchLayout.hpp"

namespace hldata
{
    // The buttons, as the console's key register has them.
    inline constexpr std::uint32_t kButtonA = 1u << 0;
    inline constexpr std::uint32_t kButtonB = 1u << 1;
    inline constexpr std::uint32_t kButtonSelect = 1u << 2;
    inline constexpr std::uint32_t kButtonStart = 1u << 3;
    inline constexpr std::uint32_t kButtonRight = 1u << 4;
    inline constexpr std::uint32_t kButtonLeft = 1u << 5;
    inline constexpr std::uint32_t kButtonUp = 1u << 6;
    inline constexpr std::uint32_t kButtonDown = 1u << 7;
    inline constexpr std::uint32_t kButtonR = 1u << 8;
    inline constexpr std::uint32_t kButtonL = 1u << 9;
    inline constexpr std::uint32_t kButtonX = 1u << 10;
    inline constexpr std::uint32_t kButtonY = 1u << 11;

    inline constexpr std::uint8_t kControlsClassic = 0;
    inline constexpr std::uint8_t kControlsModern = 1;
    inline constexpr std::uint8_t kControlsCustom = 2;
    inline constexpr std::uint8_t kControlsSchemes = 3;

    /// What a button can be made to do.
    enum Action : std::uint8_t
    {
        kActionForward,
        kActionBack,
        kActionLeft,
        kActionRight,
        kActionFire,
        kActionAltFire,
        kActionJump,
        kActionDuck,
        kActionReload,
        kActionUse,
        kActionNextWeapon,
        kActionPrevWeapon,
        kActionLamp,
        kActionWalk,
        kActionCount
    };
    /// How many a settings file before version 18 keeps (HlSettings.hpp).
    inline constexpr int kActionsBeforeWalk = kActionWalk;
    /// Their names, for the options.
    inline const char *actionName(int action)
    {
        static const char *const kNames[kActionCount] = {"Forward", "Back",   "Left",   "Right",       "Fire",
                                                     "Alt fire", "Jump",  "Duck",   "Reload",      "Use",
                                                     "Next weapon", "Weapon before", "Flashlight", "Walk"};
        return action >= 0 && action < kActionCount ? kNames[action] : "";
    }
    /// The buttons that may be given an action: all but Start.
    inline constexpr std::uint32_t kAssignable = 0xFFFu & ~(1u << 3);

    /// What a firm press on the aim pad does.
    inline constexpr std::uint8_t kFirmOff = 0;
    inline constexpr std::uint8_t kFirmFire = 1;
    inline constexpr std::uint8_t kFirmUse = 2;
    inline constexpr std::uint8_t kFirmJump = 3;
    inline constexpr std::uint8_t kFirmAltFire = 4;
    inline constexpr std::uint8_t kFirmActions = 5;
    /// How strict the stylus is about light touches: 0 (not at all) to 10.
    inline constexpr std::uint8_t kFirmnessMost = 10;
    /// Aim speed: 10 is a pixel of travel to 1/512 of a turn (as it always
    /// was); 1 to 20.
    inline constexpr std::uint8_t kAimSpeedDefault = 10;
    inline constexpr std::uint8_t kAimSpeedMost = 20;

    struct ControlSettings
    {
        std::uint8_t scheme = kControlsClassic;
        bool leftHanded = false;
        bool padTurns = false; ///< the pad's left and right turn; the stylus sidesteps
        bool padLooks = false; ///< the pad's up and down look; the stylus moves
        bool duckToggle = false; ///< the crouch button crouches, and again stands (else: held)
        bool walkToggle = false; ///< the walk button walks, and again runs (else: held)
        bool jumpWalks = false;  ///< the jump button walks (two taps still jump)
        /// The player has the long jump module: the screen has LONG JUMP. Not
        /// a setting: the game's state.
        bool longJump = false;
        std::uint8_t firmness = 0; ///< 0..kFirmnessMost
        std::uint8_t firmPress = kFirmOff;
        /// CUSTOM: each action's buttons (kButton*; any of them does it).
        std::uint16_t keys[kActionCount] = {};
        /// A match between consoles is on: the touch screen has SPRAY
        /// (HlTouchLayout.hpp). Not a setting: the game's state.
        bool match = false;
    };

    /// Each action's buttons in a layout (not CUSTOM, whose are its own).
    inline void defaultKeys(std::uint8_t scheme, bool leftHanded, std::uint16_t *keys, bool jumpWalks = false)
    {
        // The pad, the four with work of their own, and the shoulders, by
        // the hand the stylus is in.
        const std::uint32_t padUp = leftHanded ? kButtonX : kButtonUp;
        const std::uint32_t padDown = leftHanded ? kButtonB : kButtonDown;
        const std::uint32_t padLeft = leftHanded ? kButtonY : kButtonLeft;
        const std::uint32_t padRight = leftHanded ? kButtonA : kButtonRight;
        const std::uint32_t faceTop = leftHanded ? kButtonUp : kButtonX;
        const std::uint32_t faceBottom = leftHanded ? kButtonDown : kButtonB;
        const std::uint32_t faceLeft = leftHanded ? kButtonLeft : kButtonY;
        const std::uint32_t faceRight = leftHanded ? kButtonRight : kButtonA;
        const std::uint32_t trigger = leftHanded ? kButtonR : kButtonL;
        const std::uint32_t other = leftHanded ? kButtonL : kButtonR;
        for (int a = 0; a < kActionCount; ++a)
            keys[a] = 0;
        keys[kActionFire] = static_cast<std::uint16_t>(trigger);
        keys[kActionAltFire] = static_cast<std::uint16_t>(other);
        keys[kActionLamp] = static_cast<std::uint16_t>(kButtonSelect);
        if (scheme == kControlsModern)
        {
            keys[kActionForward] = static_cast<std::uint16_t>(padUp);
            keys[kActionBack] = static_cast<std::uint16_t>(padDown);
            keys[kActionLeft] = static_cast<std::uint16_t>(padLeft);
            keys[kActionRight] = static_cast<std::uint16_t>(padRight);
            keys[kActionNextWeapon] = static_cast<std::uint16_t>(faceTop);
            keys[kActionReload] = static_cast<std::uint16_t>(faceLeft);
            keys[kActionDuck] = static_cast<std::uint16_t>(faceBottom);
            keys[jumpWalks ? kActionWalk : kActionJump] = static_cast<std::uint16_t>(faceRight);
            return;
        }
        // CLASSIC: the face buttons are a second pad.
        keys[kActionForward] = static_cast<std::uint16_t>(padUp | faceTop);
        keys[kActionBack] = static_cast<std::uint16_t>(padDown | faceBottom);
        keys[kActionLeft] = static_cast<std::uint16_t>(padLeft | faceLeft);
        keys[kActionRight] = static_cast<std::uint16_t>(padRight | faceRight);
    }

    /// The table `set` plays by.
    inline void keysOf(const ControlSettings &set, std::uint16_t *keys)
    {
        if (set.scheme == kControlsCustom)
        {
            for (int a = 0; a < kActionCount; ++a)
                keys[a] = static_cast<std::uint16_t>(set.keys[a] & kAssignable);
        }
        else
        {
            defaultKeys(set.scheme, set.leftHanded, keys, set.jumpWalks);
        }
    }

    /// `button` (one kButton* bit) given to `action` in a custom table, and
    /// taken from whatever had it -- which is given the action's old
    /// button(s) instead, so that nothing is left on no button.
    inline void assignKey(std::uint16_t *keys, int action, std::uint32_t button)
    {
        if (action < 0 || action >= kActionCount || (button & kAssignable) == 0)
            return;
        const std::uint16_t old = keys[action];
        for (int a = 0; a < kActionCount; ++a)
        {
            if (a != action && (keys[a] & button))
                keys[a] = static_cast<std::uint16_t>((keys[a] & ~button) | old);
        }
        keys[action] = static_cast<std::uint16_t>(button);
    }

    /// A button's name: "A", "L", "Up"...
    inline const char *buttonName(std::uint32_t button)
    {
        static const char *const kNames[12] = {"A", "B", "Select", "Start", "Right", "Left",
                                               "Up", "Down", "R", "L", "X", "Y"};
        for (int bit = 0; bit < 12; ++bit)
        {
            if (button & (1u << bit))
                return kNames[bit];
        }
        return "-";
    }

    /// Two taps this close together -- a third of a second, forty pixels --
    /// are a double tap.
    inline constexpr std::uint32_t kDoubleTapBlanks = 20;
    inline constexpr int kDoubleTapReach = 40;
    /// A touch lifted this soon (a fifth of a second), having gone no
    /// further than this, was a tap.
    inline constexpr std::uint32_t kTapBlanks = 12;
    inline constexpr int kTapReach = 6;
    /// The stylus as a stick: nearer than this to where it came down, it
    /// is at rest.
    inline constexpr int kStickDeadZone = 10;
    /// The pad as the stick that turns: as much, each sixtieth of a second,
    /// as the stylus dragged this many pixels (three is about 125 degrees a
    /// second), and as much up and down.
    inline constexpr int kPadTurnPixels = 3;
    inline constexpr int kPadLookPixels = 2;
    /// A stylus's finer reading: sixteenths of a pixel.
    inline constexpr int kFineShift = 4;
    /// How long LONG JUMP keeps the crouch held, at least: a third of a second.
    inline constexpr std::uint32_t kLongJumpBlanks = 20;

    /// What is held this frame.
    struct ControlsIn
    {
        std::uint32_t held = 0; ///< kButton*
        bool touching = false;
        int touchX = 0, touchY = 0;
        /// The same, finer (sixteenths of a pixel), where the console can
        /// say; -1: it cannot (touchX and touchY are all there is).
        int fineX = -1, fineY = -1;
        /// How hard the stylus is pressed, as the touch screen's resistance:
        /// smaller is firmer. 0: the console cannot say.
        int pressure = 0;
        std::uint32_t blank = 0; ///< the count of sixtieths of a second so far
    };

    /// What that means.
    struct ControlsOut
    {
        int moveForward = 0; ///< -1, 0 or 1
        int moveSide = 0;    ///< -1 left, 1 right
        bool jump = false;
        bool duck = false;
        bool walk = false; ///< a walk, not a run
        bool usePressed = false;
        /// USE is held down (its button, the touch screen's USE, a firm press
        /// that is USE): what is used for as long as it is held -- a wall
        /// charger -- is used again each frame. (A tap of the aim pad is a
        /// press only.)
        bool useHeld = false;
        bool attack = false;
        bool second = false;
        int weaponPick = 0; ///< a slot was touched: its kind of weapon (1..5: weaponOfCategory), or 0
        int weaponStep = 0; ///< the weapon before (-1) or after (1) the one in hand
        int weaponChoose = 0; ///< a weapon taken from a slot's list (a kWeapon*: HlWeaponList.hpp), or 0
        bool menuPressed = false;
        bool reloadPressed = false;
        int touchOn = kTouchAim;
        bool lampPressed = false;
        bool sprayPressed = false; ///< (a match's: the touch screen's SPRAY)
        bool startPressed = false;
        bool tapped = false; ///< the stylus came down this frame, at (tapX, tapY)
        int tapX = 0, tapY = 0;
        int lookDx = 0; ///< how far to turn, as pixels of stylus travel
        int lookDy = 0;
        /// The same, finer: sixteenths of a pixel (what the view is turned by).
        int lookFineX = 0;
        int lookFineY = 0;
        bool firm = false; ///< the stylus is pressed hard on the aim pad
    };

    /// What has to be remembered from one frame to the next.
    struct ControlsState
    {
        std::uint32_t lastHeld = 0;
        std::uint32_t lastBlank = 0;
        int touch = kTouchAim; ///< what this touch started on
        bool touching = false;
        bool jumping = false; ///< this touch is the second of a double tap
        bool strayed = false; ///< ... has gone too far to be a tap
        std::uint32_t downBlank = 0;
        int downX = 0, downY = 0;
        std::uint32_t lastTapBlank = 0; ///< when the aim pad was last touched
        int lastTapX = -1000, lastTapY = -1000;
        int lastX = 0, lastY = 0;
        bool crouched = false; ///< crouching until told to stand
        /// A touch that began on the screen's DUCK (held: the crouch) and
        /// has slid off it: from there it turns the view, and the player
        /// stays crouched until it is lifted.
        bool duckSlide = false;
        /// ... and the same of the screen's WALK; and walking until told to
        /// run, as `crouched` is for the crouch.
        bool walkSlide = false;
        bool walking = false;
        /// LONG JUMP touched: since when, and that the crouch is on it yet.
        std::uint32_t longJumpBlank = 0;
        bool longJumping = false;
        // The stylus, finely: the last reading used (and how hard), where the
        // view was last turned from, the firmest this stroke has been, and
        // how hard it began.
        int fineX = 0, fineY = 0, finePressure = 0;
        int aimX = 0, aimY = 0;
        bool aimed = false;
        int firmest = 0;
        int baseline = 0, baseSamples = 0;
        bool firm = false;
    };

    /// How far a stylus step of `fine` sixteenths of a pixel turns the view,
    /// in 65536ths of a turn, at aim speed `speed` (10: 1/512 of a turn a
    /// pixel).
    inline int turnOf(int fine, int speed)
    {
        return fine * 8 * speed / kAimSpeedDefault;
    }

    /// One frame of the controls.
    inline ControlsOut controlsStep(const ControlSettings &set, const ControlsIn &in, ControlsState &state)
    {
        ControlsOut out;
        const std::uint32_t held = in.held;
        const std::uint32_t pressed = held & ~state.lastHeld;
        const bool classic = set.scheme == kControlsClassic;
        std::uint16_t keys[kActionCount];
        keysOf(set, keys);
        auto on = [&](int action) { return (held & keys[action]) != 0; };
        auto now = [&](int action) { return (pressed & keys[action]) != 0; };

        out.attack = on(kActionFire);
        out.second = on(kActionAltFire);
        // Edges are worked out here, from what was held last frame.
        out.lampPressed = now(kActionLamp);
        out.startPressed = (pressed & kButtonStart) != 0;
        out.reloadPressed = now(kActionReload);
        out.usePressed = now(kActionUse);
        if (now(kActionNextWeapon))
            out.weaponStep = 1;
        else if (now(kActionPrevWeapon))
            out.weaponStep = -1;

        int padForward = (on(kActionForward) ? 1 : 0) - (on(kActionBack) ? 1 : 0);
        const int padSide = (on(kActionRight) ? 1 : 0) - (on(kActionLeft) ? 1 : 0);
        const bool faceJump = on(kActionJump);
        bool duckHeld = false;
        if (set.duckToggle)
        {
            if (now(kActionDuck))
                state.crouched = !state.crouched;
        }
        else
        {
            duckHeld = on(kActionDuck);
        }
        if (set.scheme == kControlsModern)
        {
            // With the second shoulder held, the pad's up and down are how
            // the player stands, and not a step.
            if (on(kActionAltFire))
            {
                if (now(kActionBack))
                    state.crouched = true;
                if (now(kActionForward))
                    state.crouched = false;
                padForward = 0;
            }
        }
        else if (classic && !set.duckToggle)
        {
            // (No crouch to stay in here but the screen's DUCK, which is
            // held -- or, crouching by a toggle, tapped.)
            state.crouched = false;
        }
        const bool walkHeld = !set.walkToggle && on(kActionWalk);
        if (set.walkToggle && now(kActionWalk))
            state.walking = !state.walking;
        else if (!set.walkToggle)
            state.walking = false;
        // The stylus.
        const TouchLayout layout = touchLayout(set.leftHanded, set.match, set.jumpWalks, set.longJump);
        int dragX = 0, dragY = 0;   // its travel this frame, on the aim pad
        int stickX = 0, stickY = 0; // ... and how far it is from where it came down
        if (in.touching)
        {
            if (!state.touching)
            {
                out.tapped = true;
                out.tapX = in.touchX;
                out.tapY = in.touchY;
                // Where a touch STARTS decides what it is for its whole life.
                state.touch = touchHit(layout, in.touchX, in.touchY);
                // USE, MENU, RELOAD and a weapon's slot are presses, not
                // holds: they happen on the frame the stylus lands on them.
                out.usePressed = out.usePressed || state.touch == kTouchUse;
                out.menuPressed = state.touch == kTouchMenu;
                out.reloadPressed = out.reloadPressed || state.touch == kTouchReload;
                out.lampPressed = out.lampPressed || state.touch == kTouchLamp;
                out.sprayPressed = state.touch == kTouchSpray;
                if (state.touch >= kTouchSlot && state.touch < kTouchSlot + kTouchSlots)
                    out.weaponPick = state.touch - kTouchSlot + 1;
                // The button on the screen: held to crouch (and then slid off
                // to turn the view, still crouched: duckSlide); or, with the
                // crouch a toggle, touched once to crouch and once to stand
                // -- the stylus is then free to look about and to USE. (Held
                // while crouching by the other way, it is the way to stand.)
                if (state.touch == kTouchDuck)
                    state.crouched = set.duckToggle ? !state.crouched : false;
                state.duckSlide = false;
                if (state.touch == kTouchWalk && set.walkToggle)
                    state.walking = !state.walking;
                state.walkSlide = false;
                if (state.touch == kTouchLongJump)
                {
                    state.longJumping = true;
                    state.longJumpBlank = in.blank;
                }
                state.jumping = false;
                state.strayed = false;
                state.downBlank = in.blank;
                state.downX = in.touchX;
                state.downY = in.touchY;
                if (state.touch == kTouchAim)
                {
                    // The second of two taps close together, in time and in
                    // place, is a jump.
                    const int dx = in.touchX - state.lastTapX, dy = in.touchY - state.lastTapY;
                    state.jumping = in.blank - state.lastTapBlank <= kDoubleTapBlanks && dx > -kDoubleTapReach &&
                                    dx < kDoubleTapReach && dy > -kDoubleTapReach && dy < kDoubleTapReach;
                    // (A third tap is not a second double tap with the
                    // second.)
                    state.lastTapBlank = state.jumping ? in.blank - 1000 : in.blank;
                    state.lastTapX = in.touchX;
                    state.lastTapY = in.touchY;
                }
            }
            else if (((state.touch == kTouchDuck && !set.duckToggle) || (state.touch == kTouchWalk && !set.walkToggle)) &&
                     touchHit(layout, in.touchX, in.touchY) != state.touch)
            {
                // Held DUCK or WALK slid off it: the view turns from here on
                // (from where it is now: no jerk), still crouched or walking
                // -- and it is no tap, nor the second of two.
                state.duckSlide = state.touch == kTouchDuck;
                state.walkSlide = state.touch == kTouchWalk;
                state.touch = kTouchAim;
                state.strayed = true;
                state.jumping = false;
                state.downX = in.touchX;
                state.downY = in.touchY;
            }
            else if (state.touch == kTouchAim)
            {
                // The first frame of a touch only set the reference point:
                // otherwise putting the stylus down would jerk the view by
                // however far it landed from where it last lifted.
                dragX = in.touchX - state.lastX;
                dragY = in.touchY - state.lastY;
            }
            if (state.touch == kTouchAim)
            {
                stickX = in.touchX - state.downX;
                stickY = in.touchY - state.downY;
                if (stickX <= -kTapReach || stickX >= kTapReach || stickY <= -kTapReach || stickY >= kTapReach)
                    state.strayed = true;
            }
            state.lastX = in.touchX;
            state.lastY = in.touchY;
            state.touching = true;
        }
        else
        {
            // Lifted: a touch of the aim pad that was short and went nowhere
            // was a tap, and in the MODERN layout a tap is USE. (Not the
            // second of two: that one was a jump.)
            if (!classic && state.touching && state.touch == kTouchAim && !state.jumping && !state.strayed &&
                in.blank - state.downBlank <= kTapBlanks)
                out.usePressed = true;
            state.touching = false;
            state.touch = kTouchAim;
            state.jumping = false;
            state.duckSlide = false;
            state.walkSlide = false;
        }

        // The two sticks, an axis at a time.
        std::uint32_t blanks = in.blank - state.lastBlank;
        blanks = blanks < 1 ? 1 : (blanks > 4 ? 4 : blanks);
        if (set.padTurns)
        {
            out.moveSide = stickX >= kStickDeadZone ? 1 : (stickX <= -kStickDeadZone ? -1 : 0);
            out.lookDx = padSide * kPadTurnPixels * static_cast<int>(blanks);
        }
        else
        {
            out.moveSide = padSide;
            out.lookDx = dragX;
        }
        if (set.padLooks)
        {
            // (Up the screen is forward; and the pad's up looks up, as a
            // stylus pushed up does.)
            out.moveForward = stickY <= -kStickDeadZone ? 1 : (stickY >= kStickDeadZone ? -1 : 0);
            out.lookDy = -padForward * kPadLookPixels * static_cast<int>(blanks);
        }
        else
        {
            out.moveForward = padForward;
            out.lookDy = dragY;
        }

        // The stylus finely, and how hard: the view's turn.
        const bool weighed = in.touching && in.pressure > 0 && state.touch == kTouchAim;
        if (in.touching && state.touch == kTouchAim && !set.padTurns && !set.padLooks)
        {
            const int fx = in.fineX >= 0 ? in.fineX : in.touchX << kFineShift;
            const int fy = in.fineY >= 0 ? in.fineY : in.touchY << kFineShift;
            if (!weighed || set.firmness == 0)
            {
                // As it always was, only finer: each step as it comes.
                if (state.aimed)
                {
                    out.lookFineX = fx - state.aimX;
                    out.lookFineY = fy - state.aimY;
                }
                state.aimX = fx;
                state.aimY = fy;
                state.aimed = true;
            }
            else
            {
                // Too light, against the firmest of this stroke: left out.
                // (Pressure is a resistance: the lighter, the higher.)
                const int allowed = 64 - 4 * (set.firmness - 1); // sixteenths: 4x down to 1.75x
                const bool light = state.firmest > 0 && in.pressure * 16 > state.firmest * allowed;
                if (!light)
                {
                    if (state.firmest == 0 || in.pressure < state.firmest)
                        state.firmest = in.pressure;
                    // Averaged with the reading before, the firmer counting
                    // for more.
                    int ax = fx, ay = fy;
                    if (state.finePressure > 0)
                    {
                        const std::int64_t w1 = 65536 / in.pressure + 1, w0 = 65536 / state.finePressure + 1;
                        ax = static_cast<int>((fx * w1 + state.fineX * w0) / (w1 + w0));
                        ay = static_cast<int>((fy * w1 + state.fineY * w0) / (w1 + w0));
                    }
                    state.fineX = fx;
                    state.fineY = fy;
                    state.finePressure = in.pressure;
                    if (state.aimed)
                    {
                        out.lookFineX = ax - state.aimX;
                        out.lookFineY = ay - state.aimY;
                    }
                    state.aimX = ax;
                    state.aimY = ay;
                    state.aimed = true;
                }
            }
        }
        else if (!in.touching)
        {
            // Lifted: the next stroke starts afresh.
            state.aimed = false;
            state.firmest = 0;
            state.finePressure = 0;
            state.baseline = state.baseSamples = 0;
            state.firm = false;
        }
        // (A pad that turns or looks: whole pixels, as the sticks have them.)
        if (set.padTurns || set.padLooks)
        {
            out.lookFineX = out.lookDx << kFineShift;
            out.lookFineY = out.lookDy << kFineShift;
        }
        // Pressed hard: against how hard the stroke began (its first few
        // readings), half the resistance is firm, and it stays firm until
        // it is back past two thirds.
        const bool wasFirm = state.firm;
        if (weighed)
        {
            if (state.baseSamples < 4)
            {
                state.baseline += in.pressure;
                if (++state.baseSamples == 4)
                    state.baseline /= 4;
            }
            else if (!state.firm && in.pressure * 2 < state.baseline)
                state.firm = true;
            else if (state.firm && in.pressure * 3 > state.baseline * 2)
                state.firm = false;
        }
        out.firm = state.firm;
        const bool firmNow = state.firm && set.firmPress != kFirmOff;
        if (firmNow)
        {
            if (set.firmPress == kFirmFire)
                out.attack = true;
            else if (set.firmPress == kFirmAltFire)
                out.second = true;
            else if (set.firmPress == kFirmUse && !wasFirm)
                out.usePressed = true; // (once a press)
        }

        if (state.longJumping && state.touch != kTouchLongJump && in.blank - state.longJumpBlank >= kLongJumpBlanks)
            state.longJumping = false;
        out.jump = state.jumping || state.touch == kTouchJump || state.touch == kTouchLongJump || faceJump || (firmNow && set.firmPress == kFirmJump);
        out.duck = (state.touch == kTouchDuck && !set.duckToggle) || (state.duckSlide && state.touching) ||
                   state.crouched || duckHeld || state.longJumping;
        out.walk = (state.touch == kTouchWalk && !set.walkToggle) || (state.walkSlide && state.touching) ||
                   state.walking || walkHeld;
        out.touchOn = !state.touching ? kTouchAim
                                      : (state.duckSlide ? kTouchDuck : (state.walkSlide ? kTouchWalk : state.touch));
        out.useHeld = on(kActionUse) || (state.touching && state.touch == kTouchUse) ||
                      (firmNow && set.firmPress == kFirmUse);
        state.lastHeld = held;
        state.lastBlank = in.blank;
        return out;
    }

    /// What each button does, in a few short lines, for the options to
    /// show: `line` from 0; null past the last. (Made from the table, into
    /// a buffer of its own: good until the next call.)
    inline const char *controlsLegend(const ControlSettings &set, int line)
    {
        static char text[48];
        std::uint16_t keys[kActionCount];
        keysOf(set, keys);
        // The buttons of an action, "A", or "X/Up" for two.
        auto names = [&](int action, char *out, int size)
        {
            int n = 0;
            out[0] = '\0';
            for (int bit = 0; bit < 12; ++bit)
            {
                if (!(keys[action] & (1u << bit)))
                    continue;
                const char *name = buttonName(1u << bit);
                if (n > 0 && n < size - 1)
                    out[n++] = '/';
                for (const char *p = name; *p != '\0' && n < size - 1; ++p)
                    out[n++] = *p;
                out[n] = '\0';
            }
            if (n == 0 && size > 1)
            {
                out[0] = '-';
                out[1] = '\0';
            }
        };
        auto pair = [&](int a, const char *what, int b, const char *other) -> const char *
        {
            char x[16], y[16];
            names(a, x, sizeof(x));
            names(b, y, sizeof(y));
            std::snprintf(text, sizeof(text), "%s %s   %s %s", x, what, y, other);
            return text;
        };
        const bool classic = set.scheme == kControlsClassic;
        if (classic)
        {
            switch (line)
            {
            case 0: return pair(kActionFire, "fire", kActionAltFire, "second fire");
            case 1: return "A B X Y as the D-pad";
            case 2: return "double tap: jump";
            default: return nullptr;
            }
        }
        switch (line)
        {
        case 0: return pair(kActionFire, "fire", kActionAltFire, "second fire");
        case 1:
            return pair(set.jumpWalks ? kActionWalk : kActionJump, set.jumpWalks ? "walk" : "jump", kActionDuck,
                        set.duckToggle ? "crouch/stand" : "crouch");
        case 2: return pair(kActionReload, "reload", kActionNextWeapon, "weapon");
        case 3: return "tap: use   double tap: jump";
        default: return nullptr;
        }
    }
} // namespace hldata
