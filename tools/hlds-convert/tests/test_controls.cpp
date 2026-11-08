// What the buttons and the stylus mean (shared/HlControls.hpp): the classic
// layout is, to the frame, what the game did before there was a choice; the
// modern one gives each button its work; the hand changes sides; the pad and
// the stylus can each take the other's axis.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HlControls.hpp"
#include "HlMonsters.hpp"
#include "HlSettings.hpp"
#include "HlWeaponList.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    /// The controls as they were when there was one layout: the game's own
    /// code of that time, kept here to be compared with.
    struct Before
    {
        int moveForward = 0, moveSide = 0;
        bool jump = false, duck = false, usePressed = false, attack = false, second = false;
        int weaponPick = 0;
        bool menuPressed = false, reloadPressed = false;
        int touchOn = kTouchAim;
        bool lampPressed = false, startPressed = false, tapped = false;
        int tapX = 0, tapY = 0, lookDx = 0, lookDy = 0;

        int touch_ = kTouchAim;
        bool touching_ = false, jumping_ = false;
        bool duckSlide_ = false; ///< (since: DUCK held, slid off, turns the view still crouched)
        bool walkSlide_ = false; ///< (the same of WALK)
        std::uint32_t lastTapBlank_ = 0;
        int lastTapX_ = -1000, lastTapY_ = -1000;
        std::uint32_t lastHeld_ = 0;
        int lastX_ = 0, lastY_ = 0;

        void poll(const ControlsIn &in, bool leftHanded)
        {
            const std::uint32_t held = in.held;
            moveForward = ((held & (kButtonUp | kButtonX)) ? 1 : 0) - ((held & (kButtonDown | kButtonB)) ? 1 : 0);
            moveSide = ((held & (kButtonRight | kButtonA)) ? 1 : 0) - ((held & (kButtonLeft | kButtonY)) ? 1 : 0);
            lampPressed = (held & ~lastHeld_ & kButtonSelect) != 0;
            startPressed = (held & ~lastHeld_ & kButtonStart) != 0;
            tapped = false;
            usePressed = false;
            weaponPick = 0;
            menuPressed = false;
            reloadPressed = false;
            lastHeld_ = held;
            lookDx = 0;
            lookDy = 0;
            if (in.touching)
            {
                if (!touching_)
                {
                    tapped = true;
                    duckSlide_ = false;
                    walkSlide_ = false;
                    tapX = in.touchX;
                    tapY = in.touchY;
                    touch_ = touchHit(touchLayout(leftHanded), in.touchX, in.touchY);
                    usePressed = touch_ == kTouchUse;
                    menuPressed = touch_ == kTouchMenu;
                    reloadPressed = touch_ == kTouchReload;
                    lampPressed = lampPressed || touch_ == kTouchLamp;
                    if (touch_ >= kTouchSlot && touch_ < kTouchSlot + kTouchSlots)
                        weaponPick = touch_ - kTouchSlot + 1;
                    jumping_ = false;
                    if (touch_ == kTouchAim)
                    {
                        const std::uint32_t now = in.blank;
                        const int dx = in.touchX - lastTapX_, dy = in.touchY - lastTapY_;
                        jumping_ = now - lastTapBlank_ <= 20 && dx > -40 && dx < 40 && dy > -40 && dy < 40;
                        lastTapBlank_ = jumping_ ? now - 1000 : now;
                        lastTapX_ = in.touchX;
                        lastTapY_ = in.touchY;
                    }
                }
                else if (touch_ == kTouchDuck && touchHit(touchLayout(leftHanded), in.touchX, in.touchY) != kTouchDuck)
                {
                    touch_ = kTouchAim;
                    duckSlide_ = true;
                }
                else if (touch_ == kTouchWalk && touchHit(touchLayout(leftHanded), in.touchX, in.touchY) != kTouchWalk)
                {
                    touch_ = kTouchAim;
                    walkSlide_ = true;
                }
                else if (touch_ == kTouchAim)
                {
                    lookDx = in.touchX - lastX_;
                    lookDy = in.touchY - lastY_;
                }
                lastX_ = in.touchX;
                lastY_ = in.touchY;
                touching_ = true;
            }
            else
            {
                touching_ = false;
                touch_ = kTouchAim;
                jumping_ = false;
                duckSlide_ = false;
                walkSlide_ = false;
            }
            jump = jumping_ || touch_ == kTouchJump;
            duck = touch_ == kTouchDuck || duckSlide_;
            touchOn = touching_ ? (duckSlide_ ? kTouchDuck : (walkSlide_ ? kTouchWalk : touch_)) : kTouchAim;
            attack = (held & kButtonL) != 0;
            second = (held & kButtonR) != 0;
        }
    };

    std::uint32_t g_seed = 12345;
    std::uint32_t rnd()
    {
        g_seed = g_seed * 1664525u + 1013904223u;
        return g_seed >> 8;
    }

    /// Fifty thousand frames of someone leaning on everything at once.
    void testClassicIsAsBefore()
    {
        Before before;
        ControlsState state;
        ControlSettings set; // classic, right-handed, nothing swapped
        ControlsIn in;
        int differ = 0;
        for (int frame = 0; frame < 50000; ++frame)
        {
            in.blank += 1 + rnd() % 2;
            // (Buttons change now and then; the stylus comes down, drags,
            // jumps about and lifts.)
            if (rnd() % 4 == 0)
                in.held ^= 1u << (rnd() % 12);
            if (rnd() % 6 == 0)
                in.touching = !in.touching;
            if (in.touching && rnd() % 3 == 0)
            {
                in.touchX = static_cast<int>(rnd() % 256);
                in.touchY = static_cast<int>(rnd() % 192);
            }
            else if (in.touching)
            {
                in.touchX += static_cast<int>(rnd() % 7) - 3;
                in.touchY += static_cast<int>(rnd() % 7) - 3;
            }
            before.poll(in, false);
            const ControlsOut now = controlsStep(set, in, state);
            differ += now.moveForward != before.moveForward || now.moveSide != before.moveSide ||
                      now.jump != before.jump || now.duck != before.duck || now.usePressed != before.usePressed ||
                      now.attack != before.attack || now.second != before.second ||
                      now.weaponPick != before.weaponPick || now.weaponStep != 0 ||
                      now.menuPressed != before.menuPressed || now.reloadPressed != before.reloadPressed ||
                      now.touchOn != before.touchOn || now.lampPressed != before.lampPressed ||
                      now.startPressed != before.startPressed || now.tapped != before.tapped ||
                      (now.tapped && (now.tapX != before.tapX || now.tapY != before.tapY)) ||
                      now.lookDx != before.lookDx || now.lookDy != before.lookDy;
        }
        check(differ == 0, "the classic layout, right-handed, is what the controls were: every frame of 50000");
    }

    /// One frame with `held` down and no stylus.
    ControlsOut press(const ControlSettings &set, ControlsState &state, std::uint32_t held, std::uint32_t &blank)
    {
        ControlsIn in;
        in.held = held;
        in.blank = ++blank;
        return controlsStep(set, in, state);
    }
    ControlsOut touch(const ControlSettings &set, ControlsState &state, int x, int y, std::uint32_t &blank,
                      std::uint32_t held = 0)
    {
        ControlsIn in;
        in.held = held;
        in.touching = true;
        in.touchX = x;
        in.touchY = y;
        in.blank = ++blank;
        return controlsStep(set, in, state);
    }

    void testModern()
    {
        ControlSettings set;
        set.scheme = kControlsModern;
        ControlsState state;
        std::uint32_t blank = 100;

        ControlsOut out = press(set, state, kButtonUp | kButtonRight, blank);
        check(out.moveForward == 1 && out.moveSide == 1, "modern: the D-pad moves");
        out = press(set, state, kButtonL, blank);
        check(out.attack && !out.second, "modern: L fires");
        out = press(set, state, kButtonR, blank);
        check(out.second && !out.attack, "modern: R is the second trigger");
        out = press(set, state, 0, blank);

        out = press(set, state, kButtonX, blank);
        check(out.weaponStep == 1 && out.moveForward == 0, "modern: X is the next weapon, and no step forward");
        out = press(set, state, kButtonX, blank);
        check(out.weaponStep == 0, "... once, however long it is held");
        out = press(set, state, kButtonY, blank);
        check(out.reloadPressed && out.moveSide == 0, "modern: Y reloads");
        out = press(set, state, kButtonY, blank);
        check(!out.reloadPressed, "... once");
        out = press(set, state, kButtonB, blank);
        check(out.duck && out.moveForward == 0 && !out.reloadPressed, "modern: B crouches");
        out = press(set, state, kButtonB | kButtonUp, blank);
        check(out.duck && out.moveForward == 1, "... for as long as it is held, walking or not");
        out = press(set, state, 0, blank);
        check(!out.duck, "... and let go, stands");
        out = press(set, state, kButtonA, blank);
        check(out.jump && out.moveSide == 0, "modern: A jumps");
        out = press(set, state, kButtonA, blank);
        check(out.jump, "... for as long as it is held");
        out = press(set, state, 0, blank);
        check(!out.jump && !out.duck, "... and no longer");

        // The second shoulder with the pad: how the player stands.
        out = press(set, state, kButtonR, blank);
        out = press(set, state, kButtonR | kButtonDown, blank);
        check(out.duck && out.moveForward == 0, "modern: R and down crouches, and is not a step back");
        out = press(set, state, 0, blank);
        check(out.duck, "... and stays crouched with both let go");
        out = press(set, state, kButtonDown, blank);
        check(out.duck && out.moveForward == -1, "... walking, crouched");
        out = press(set, state, kButtonR | kButtonLeft, blank);
        check(out.duck && out.moveSide == -1, "the pad's left and right sidestep whatever R is doing");
        out = press(set, state, kButtonR | kButtonUp, blank);
        check(!out.duck && out.moveForward == 0, "modern: R and up stands");
        out = press(set, state, kButtonSelect | kButtonStart, blank);
        check(out.lampPressed && out.startPressed, "Select is the lamp and Start the menu, as ever");

        // The button on the screen stands someone who crouched by R.
        press(set, state, 0, blank);
        press(set, state, kButtonR, blank);
        press(set, state, kButtonR | kButtonDown, blank);
        const TouchLayout at = touchLayout(false);
        out = touch(set, state, at.duck.x + 5, at.duck.y + 5, blank);
        check(out.duck, "the screen's DUCK, held, crouches");
        out = press(set, state, 0, blank);
        check(!out.duck, "... and let go, stands -- also someone who had crouched with R");
    }

    /// A slot's list of weapons (HlWeaponList.hpp): up when the slot is
    /// touched, a weapon taken by a touch or by sliding onto it from the
    /// slot; away on a touch elsewhere, the slot again, or idleness.
    void testWeaponList()
    {
        for (const bool left : {false, true})
        {
            const TouchLayout at = touchLayout(left);
            const unsigned have = (1u << kWeaponMp5) | (1u << kWeaponShotgun) | (1u << kWeaponCrossbow) |
                                  (1u << kWeaponCrowbar);
            std::uint32_t blank = 0;
            auto settle = [&](WeaponList &list)
            {
                for (int i = 0; i < 20; ++i)
                    weaponListStep(list, at, false, false, 0, 0, ++blank);
            };
            WeaponList list;
            check(weaponListOpen(list, 1, have, 0, blank) == kWeaponCrowbar && !list.open(),
                  "a slot with one weapon in it takes that one at once");
            check(weaponListOpen(list, 3, have, 0, blank) == 0 && list.open() && list.count == 3 &&
                      list.weapons[0] == kWeaponMp5 && list.weapons[1] == kWeaponShotgun &&
                      list.weapons[2] == kWeaponCrossbow,
                  "slot 3: its three weapons carried, in the game's order");
            settle(list);
            const TouchRect r = weaponListRect(list, at);
            check(list.slide == 256 && r.x >= 0 && r.x + r.w <= kTouchWidth && r.y >= at.pad.y + 1 &&
                      r.y + r.h <= at.pad.y + at.pad.h - 3,
                  left ? "... up, over the pad, left-handed" : "... up, over the pad");
            // (Every slot's list, of as many as a slot has: on the screen,
            // above the slots, and over its own slot -- centred on it as far
            // as the screen allows.)
            for (int category = 1; category <= kTouchSlots; ++category)
            {
                WeaponList each;
                each.category = category;
                each.count = kWeaponListMost;
                const TouchRect e = weaponListRect(each, at);
                const TouchRect &slot = at.slot[category - 1];
                const int centre = slot.x + slot.w / 2, middle = e.x + e.w / 2;
                check(e.x >= kWeaponListInset && e.x + e.w <= kTouchWidth - kWeaponListInset &&
                          e.y >= at.pad.y + 1 && e.y + e.h <= at.pad.y + at.pad.h - 3,
                      "every slot's list, at its longest, is on the screen and above the slots");
                check(e.x <= slot.x && e.x + e.w >= slot.x + slot.w, "... over all of its own slot");
                check(middle == centre || e.x == kWeaponListInset || e.x + e.w == kTouchWidth - kWeaponListInset,
                      "... centred on it, unless the screen's edge is in the way");
            }
            {
                WeaponList five;
                five.category = 5;
                five.count = 4;
                const TouchRect e = weaponListRect(five, at);
                check(e.x + e.w == kTouchWidth - kWeaponListInset, "slot 5's list stands at the screen's right edge");
            }
            // A weapon touched: taken.
            const int x = r.x + r.w / 2, rowY = r.y + 2 + kWeaponListRow + kWeaponListRow / 2;
            WeaponListTouch got = weaponListStep(list, at, true, true, x, rowY, ++blank);
            check(got.consumed && got.chosen == 0, "a weapon in it touched is the list's, not the controls'");
            got = weaponListStep(list, at, false, false, x, rowY, ++blank);
            check(got.chosen == kWeaponShotgun && list.closing, "... lifted: that one is taken, and the list goes");
            settle(list);
            check(!list.open(), "... gone");

            // The slot pressed, slid up onto a weapon, lifted.
            weaponListOpen(list, 3, have, 0, blank);
            list.owned = true; // (as Input has it: the touch that opened it)
            settle(list);
            list.owned = true;
            // (Across the first row and on: the move is heard only where the
            // stylus stops, once.)
            int moves = 0;
            for (int n = 0; n < 3; ++n)
                moves += weaponListStep(list, at, true, false, x, r.y + 2 + kWeaponListRow / 2, ++blank).moved;
            check(moves == 0, "... slid across a weapon of it: nothing heard");
            for (std::uint32_t n = 0; n <= kWeaponListSettle + 5; ++n)
            {
                got = weaponListStep(list, at, true, false, x, r.y + 2 + kWeaponListRow * 2 + 4, ++blank);
                moves += got.moved;
                check(!got.dismissed, "... and stays up");
            }
            check(moves == 1, "... stopped on one: the menu's move, once");
            got = weaponListStep(list, at, false, false, x, r.y + 2 + kWeaponListRow * 2 + 4, ++blank);
            check(got.chosen == kWeaponCrossbow, "the slot pressed and slid up onto a weapon, lifted: taken");
            settle(list);

            // Pressed and lifted on the slot: the list stays, to be touched.
            weaponListOpen(list, 3, have, 0, blank);
            list.owned = true;
            weaponListStep(list, at, true, false, at.slot[2].x + 5, at.slot[2].y + 5, ++blank);
            got = weaponListStep(list, at, false, false, at.slot[2].x + 5, at.slot[2].y + 5, ++blank);
            check(got.chosen == 0 && list.open() && !list.closing, "a slot only tapped: its list stays up");
            got = weaponListStep(list, at, true, true, at.slot[2].x + 5, at.slot[2].y + 5, ++blank);
            check(got.consumed && list.closing && got.dismissed, "... its slot again: away, and nothing else");
            settle(list);

            // Elsewhere: away, and the touch is the controls'.
            weaponListOpen(list, 3, have, 0, blank);
            settle(list);
            got = weaponListStep(list, at, true, true, at.pad.x + 5, at.pad.y + 5, ++blank);
            check(!got.consumed && list.closing, "a touch elsewhere: the list goes, and the touch is the controls'");
            settle(list);

            // Nothing for three seconds: away.
            weaponListOpen(list, 3, have, 0, blank);
            for (int i = 0; i < 200; ++i)
                weaponListStep(list, at, false, false, 0, 0, ++blank);
            check(!list.open(), "three seconds of nothing: away");
            weaponListOpen(list, 3, have, 0, blank);
            bool dismissed = false;
            for (int i = 0; i < 200; ++i)
                dismissed = weaponListStep(list, at, false, false, 0, 0, ++blank).dismissed || dismissed;
            check(dismissed, "... said to be put away with nothing taken");
        }
    }

    /// Crouched, and still looking about and using things, as with the
    /// game's own crouch key held: the screen's DUCK held and slid off onto
    /// the pad; or, as a toggle, tapped.
    void testCrouchAim()
    {
        const TouchLayout at = touchLayout(false);
        const int px = at.pad.x + 80, py = at.pad.y + 60;
        std::uint32_t blank = 0;
        {
            ControlSettings set; // (classic, held)
            ControlsState state;
            ControlsOut out = touch(set, state, at.duck.x + 5, at.duck.y + 5, blank);
            check(out.duck, "DUCK held crouches");
            out = touch(set, state, px, py, blank);
            check(out.duck && out.touchOn == kTouchDuck && out.lookDx == 0 && out.lookDy == 0,
                  "... slid off onto the pad: still crouched, and the view does not jump");
            out = touch(set, state, px + 20, py + 6, blank);
            check(out.duck && (out.lookDx != 0 || out.lookFineX != 0), "... and the stylus turns the view");
            out = press(set, state, 0, blank);
            check(!out.duck && !out.usePressed && !out.jump, "... lifted: stands, and it was no tap");
        }
        {
            ControlSettings set;
            set.duckToggle = 1;
            ControlsState state;
            ControlsOut out = touch(set, state, at.duck.x + 5, at.duck.y + 5, blank);
            out = press(set, state, 0, blank);
            check(out.duck, "DUCK a toggle: tapped, crouched, with the stylus lifted");
            touch(set, state, px, py, blank);
            out = touch(set, state, px + 20, py, blank);
            check(out.duck && (out.lookDx != 0 || out.lookFineX != 0), "... and the stylus turns the view, crouched");
            press(set, state, 0, blank);
            out = touch(set, state, at.use.x + 5, at.use.y + 5, blank);
            check(out.duck && out.usePressed, "... and USE is touched, crouched");
            press(set, state, 0, blank);
            for (int i = 0; i < 5; ++i)
                out = press(set, state, 0, blank);
            check(out.duck, "... frames on, the classic layout keeps the crouch");
            touch(set, state, at.duck.x + 5, at.duck.y + 5, blank);
            out = press(set, state, 0, blank);
            check(!out.duck, "... DUCK tapped again: stands");
        }
    }

    void testTaps()
    {
        ControlSettings modern;
        modern.scheme = kControlsModern;
        modern.tapUse = true;
        ControlSettings classic;
        classic.tapUse = true;
        const TouchLayout at = touchLayout(false);
        const int x = at.pad.x + 80, y = at.pad.y + 60;

        // A tap: down three frames, up; and the frames after, until the time a
        // second tap could come in has gone by. How many USEs it made.
        auto tapUses = [&](const ControlSettings &set, ControlsState &state, std::uint32_t &blank, bool &tapped)
        {
            int uses = 0;
            ControlsOut out = touch(set, state, x, y, blank);
            tapped = out.tapped;
            uses += out.usePressed;
            uses += touch(set, state, x + 1, y, blank).usePressed;
            uses += touch(set, state, x + 1, y + 1, blank).usePressed;
            out = press(set, state, 0, blank);
            const bool atOnce = out.usePressed;
            uses += out.usePressed;
            for (std::uint32_t n = 0; n <= kDoubleTapBlanks; ++n)
                uses += press(set, state, 0, blank).usePressed;
            return atOnce ? -uses : uses; // (negative: it was USE as it lifted)
        };
        for (int which = 0; which < 2; ++which)
        {
            const ControlSettings &set = which ? modern : classic;
            ControlsState state;
            std::uint32_t blank = 1000;
            bool tapped = false;
            const int uses = tapUses(set, state, blank, tapped);
            check(tapped, "a touch of the aim pad is a tap");
            check(uses == (which == 1 ? 1 : 0), which ? "modern, tap to use on: a short tap is USE, once"
                                                       : "classic: a tap is nothing");
            // Two quick taps: a jump, and no USE at all -- not even the first.
            blank += 100;
            int jumpUses = 0;
            ControlsOut out = touch(set, state, x, y, blank);
            jumpUses += out.usePressed;
            jumpUses += press(set, state, 0, blank).usePressed;
            out = touch(set, state, x + 3, y - 2, blank);
            jumpUses += out.usePressed;
            check(out.jump, "two quick taps jump");
            out = touch(set, state, x + 3, y - 2, blank);
            check(out.jump, "... for as long as the second is held");
            for (std::uint32_t n = 0; n <= kDoubleTapBlanks + 2; ++n)
                jumpUses += press(set, state, 0, blank).usePressed;
            check(jumpUses == 0, "... and neither tap is a USE");
        }
        {
            // Off, as it is unless asked for: a tap is nothing.
            ControlSettings plain;
            plain.scheme = kControlsModern;
            ControlsState state;
            std::uint32_t blank = 2000;
            bool tapped = false;
            check(tapUses(plain, state, blank, tapped) == 0, "tap to use off: a tap is not USE");
            // On, with two taps not jumping: USE as it lifts, not waiting.
            ControlSettings quick = modern;
            quick.tapJump = false;
            check(tapUses(quick, state, blank, tapped) == -1, "... on, with the double tap off: USE as it lifts");
        }

        ControlsState state;
        std::uint32_t blank = 5000;
        // A drag is not a tap.
        touch(modern, state, x, y, blank);
        ControlsOut out = touch(modern, state, x + 12, y, blank);
        check(out.lookDx == 12 && out.lookDy == 0, "a drag turns the view by as far as it went");
        out = press(modern, state, 0, blank);
        check(!out.usePressed, "... and is not a tap, though it was short");
        // Nor is a long press.
        blank += 100;
        touch(modern, state, x, y, blank);
        for (int n = 0; n < 20; ++n)
            touch(modern, state, x, y, blank);
        out = press(modern, state, 0, blank);
        check(!out.usePressed, "a touch held a third of a second is not a tap");
        // Nor a touch that started on a control.
        blank += 100;
        out = touch(modern, state, at.jump.x + 4, at.jump.y + 4, blank);
        check(out.jump && out.touchOn == kTouchJump, "the screen's JUMP jumps");
        out = press(modern, state, 0, blank);
        check(!out.usePressed, "... and lifting from it is not USE");
        blank += 100;
        out = touch(modern, state, at.use.x + 4, at.use.y + 4, blank);
        check(out.usePressed, "the screen's USE is still USE, as the stylus lands");
        out = touch(modern, state, at.slot[2].x + 4, at.slot[2].y + 4, blank);
        check(out.weaponPick == 0, "(sliding on from it to a weapon's slot takes no weapon)");
        press(modern, state, 0, blank);
        out = touch(modern, state, at.slot[2].x + 4, at.slot[2].y + 4, blank);
        check(out.weaponPick == 3, "a slot touched takes its weapon");
    }

    void testHand()
    {
        ControlSettings set;
        set.scheme = kControlsModern;
        set.leftHanded = true;
        ControlsState state;
        std::uint32_t blank = 100;
        ControlsOut out = press(set, state, kButtonX | kButtonA, blank);
        check(out.moveForward == 1 && out.moveSide == 1 && !out.jump && out.weaponStep == 0,
              "left-handed: A B X Y are the pad, by where they are");
        out = press(set, state, kButtonB | kButtonY, blank);
        check(out.moveForward == -1 && out.moveSide == -1 && !out.reloadPressed, "... all four");
        out = press(set, state, kButtonR, blank);
        check(out.attack && !out.second, "left-handed: R fires");
        out = press(set, state, kButtonL, blank);
        check(out.second && !out.attack, "... and L is the second trigger");
        press(set, state, 0, blank);
        out = press(set, state, kButtonUp, blank);
        check(out.weaponStep == 1 && out.moveForward == 0, "left-handed: the D-pad has the face buttons' work");
        out = press(set, state, kButtonLeft, blank);
        check(out.reloadPressed, "... a reload");
        out = press(set, state, kButtonDown, blank);
        check(out.duck, "... a crouch");
        out = press(set, state, kButtonRight, blank);
        check(out.jump && !out.duck, "... a jump");
        press(set, state, 0, blank);
        press(set, state, kButtonL, blank);
        out = press(set, state, kButtonL | kButtonB, blank);
        check(out.duck, "left-handed: L and the pad's down (B) crouches");
        out = press(set, state, kButtonL | kButtonX, blank);
        check(!out.duck, "... and its up (X) stands");
        const TouchLayout mirrored = touchLayout(true);
        out = touch(set, state, mirrored.jump.x + 4, mirrored.jump.y + 4, blank);
        check(out.jump, "left-handed: the touch screen is the other way round");

        ControlSettings classic;
        classic.leftHanded = true;
        ControlsState other;
        out = press(classic, other, kButtonR, blank);
        check(out.attack && !out.second, "classic, left-handed: R fires too");
        out = press(classic, other, kButtonUp | kButtonA, blank);
        check(out.moveForward == 1 && out.moveSide == 1, "... and both sets of four still move");
    }

    void testSticks()
    {
        const TouchLayout at = touchLayout(false);
        const int x = at.pad.x + 100, y = at.pad.y + 60;
        {
            ControlSettings set;
            set.padTurns = true;
            ControlsState state;
            std::uint32_t blank = 100;
            press(set, state, 0, blank); // (the first frame is as long as it likes)
            ControlsOut out = press(set, state, kButtonRight, blank);
            check(out.moveSide == 0 && out.lookDx == kPadTurnPixels, "pad turns: right turns right, and is no sidestep");
            out = press(set, state, kButtonLeft | kButtonUp, blank);
            check(out.lookDx == -kPadTurnPixels && out.moveForward == 1, "... left turns left; up still walks");
            blank += 1; // (a frame that took two)
            out = press(set, state, kButtonRight, blank);
            check(out.lookDx == 2 * kPadTurnPixels, "... twice as far in a frame twice as long");
            press(set, state, 0, blank);
            touch(set, state, x, y, blank);
            out = touch(set, state, x + 4, y - 9, blank);
            check(out.moveSide == 0 && out.lookDx == 0 && out.lookDy == -9,
                  "pad turns: the stylus still looks up and down, and near where it landed does not sidestep");
            out = touch(set, state, x + 30, y - 9, blank);
            check(out.moveSide == 1 && out.lookDx == 0, "... pushed right, it sidesteps right");
            out = touch(set, state, x + 30, y - 9, blank);
            check(out.moveSide == 1, "... for as long as it is held there");
            out = touch(set, state, x - 30, y, blank);
            check(out.moveSide == -1, "... and left");
            out = press(set, state, 0, blank);
            check(out.moveSide == 0, "... and lifted, stops");
        }
        {
            ControlSettings set;
            set.padLooks = true;
            ControlsState state;
            std::uint32_t blank = 100;
            press(set, state, 0, blank); // (the first frame is as long as it likes)
            ControlsOut out = press(set, state, kButtonUp, blank);
            check(out.moveForward == 0 && out.lookDy == -kPadLookPixels, "pad looks: up looks up, and is no step");
            out = press(set, state, kButtonDown | kButtonRight, blank);
            check(out.lookDy == kPadLookPixels && out.moveSide == 1, "... down looks down; right still sidesteps");
            press(set, state, 0, blank);
            touch(set, state, x, y, blank);
            out = touch(set, state, x + 7, y - 25, blank);
            check(out.moveForward == 1 && out.lookDx == 7 && out.lookDy == 0,
                  "pad looks: the stylus pushed up walks forward, and still turns");
            out = touch(set, state, x + 7, y + 25, blank);
            check(out.moveForward == -1, "... pulled down, back");
        }
        {
            ControlSettings set;
            set.padTurns = set.padLooks = true;
            ControlsState state;
            std::uint32_t blank = 100;
            press(set, state, 0, blank); // (the first frame is as long as it likes)
            ControlsOut out = press(set, state, kButtonUp | kButtonLeft, blank);
            check(out.moveForward == 0 && out.moveSide == 0 && out.lookDx == -kPadTurnPixels &&
                      out.lookDy == -kPadLookPixels,
                  "both: the pad is the view");
            press(set, state, 0, blank);
            touch(set, state, x, y, blank);
            out = touch(set, state, x - 20, y - 20, blank);
            check(out.moveForward == 1 && out.moveSide == -1 && out.lookDx == 0 && out.lookDy == 0,
                  "... and the stylus the legs");
        }
    }

    /// BUTTONS ONLY: the D-pad looks, A B X Y step, the other shoulder is a
    /// shift (and alone, a reload); the look actions in a custom layout; and
    /// a settings file from before them.
    void testButtonsOnly()
    {
        std::uint32_t blank = 100;
        ControlSettings set;
        set.scheme = kControlsButtons;
        ControlsState state;
        press(set, state, 0, blank); // (the first frame is as long as it likes)
        ControlsOut out = press(set, state, kButtonUp | kButtonRight, blank);
        check(out.moveForward == 0 && out.moveSide == 0 && out.lookDx == kPadTurnPixels &&
                  out.lookDy == -kPadLookPixels && out.lookFineX == kPadTurnPixels << kFineShift &&
                  out.lookFineY == -kPadLookPixels << kFineShift,
              "buttons only: the D-pad looks up and turns right, and is no step");
        out = press(set, state, kButtonX | kButtonA, blank);
        check(out.moveForward == 1 && out.moveSide == 1 && out.lookDx == 0 && out.lookDy == 0,
              "... X steps forward, A right, and do not look");
        out = press(set, state, kButtonB | kButtonY | kButtonL | kButtonSelect, blank);
        check(out.moveForward == -1 && out.moveSide == -1 && out.attack && !out.second && out.usePressed &&
                  out.useHeld && !out.lampPressed,
              "... B back, Y left, L fires, Select is USE");
        press(set, state, 0, blank);
        // Under the shift: forward still, and a jump on the run.
        press(set, state, kButtonX, blank);
        press(set, state, kButtonX | kButtonR, blank);
        out = press(set, state, kButtonX | kButtonR | kButtonUp, blank);
        check(out.jump && out.moveForward == 1 && out.lookDy == 0 && !out.reloadPressed,
              "R held: up jumps, the step goes on, and the view stands still");
        out = press(set, state, kButtonR | kButtonDown, blank);
        check(out.duck && !out.jump, "... down crouches");
        out = press(set, state, kButtonR | kButtonRight, blank);
        check(out.weaponStep == 1 && out.lookDx == 0, "... right is the weapon after");
        press(set, state, kButtonR, blank);
        out = press(set, state, kButtonR | kButtonLeft, blank);
        check(out.weaponStep == -1, "... left the weapon before");
        out = press(set, state, kButtonR | kButtonL | kButtonSelect, blank);
        check(out.second && !out.attack && out.lampPressed && !out.usePressed,
              "... L is the second fire, Select the lamp");
        out = press(set, state, 0, blank);
        check(!out.reloadPressed, "... and let go of after all that, no reload");
        // Tapped alone: a reload, once; held long: nothing.
        press(set, state, kButtonR, blank);
        out = press(set, state, 0, blank);
        check(out.reloadPressed, "R tapped alone reloads");
        out = press(set, state, 0, blank);
        check(!out.reloadPressed, "... once");
        press(set, state, kButtonR, blank);
        for (int n = 0; n < 40; ++n)
            press(set, state, kButtonR, blank);
        out = press(set, state, 0, blank);
        check(!out.reloadPressed, "R held a while and let go: no reload");
        // The pad's choices are not this layout's.
        set.padTurns = set.padLooks = true;
        out = press(set, state, kButtonX | kButtonLeft, blank);
        check(out.moveForward == 1 && out.lookDx == -kPadTurnPixels, "the pad's turn and look settings change nothing here");
        set.padTurns = set.padLooks = false;
        // The left hand: all mirrored.
        set.leftHanded = true;
        ControlsState left;
        press(set, left, 0, blank);
        out = press(set, left, kButtonX | kButtonUp, blank);
        check(out.lookDy == -kPadLookPixels && out.moveForward == 1, "left hand: X looks up, the D-pad's up steps");
        press(set, left, kButtonL, blank);
        out = press(set, left, kButtonL | kButtonX, blank);
        check(out.jump, "... L is the shift: L+X jumps");
        out = press(set, left, kButtonR, blank);
        check(out.attack, "... R fires");
        check(jumpsOtherwise(set) && std::strcmp(controlsName(kControlsButtons), "buttons only") == 0 &&
                  controlsLegend(set, 0) != nullptr && controlsLegend(set, 4) == nullptr,
              "it jumps without the double tap, and has a name and a legend");

        // A custom layout's look buttons.
        ControlSettings custom;
        custom.scheme = kControlsCustom;
        defaultKeys(kControlsModern, false, custom.keys);
        assignKey(custom.keys, kActionTurnLeft, kButtonY);
        ControlsState cs;
        press(custom, cs, 0, blank);
        out = press(custom, cs, kButtonY, blank);
        check(out.lookDx == -kPadTurnPixels && !out.reloadPressed && custom.keys[kActionReload] == 0,
              "custom: a button given TURN LEFT turns, and what it was is on no button");
        ControlSettings modern;
        modern.scheme = kControlsModern;
        std::uint16_t keys[kActionCount];
        keysOf(modern, keys);
        check(keys[kActionLookUp] == 0 && keys[kActionLookDown] == 0 && keys[kActionTurnLeft] == 0 &&
                  keys[kActionTurnRight] == 0,
              "the modern layout has no buttons that look");

        // Settings: the look buttons kept; a file of version 23 read with none.
        SettingsData data;
        data.controlScheme = kControlsButtons;
        data.keys[kActionLookUp] = static_cast<std::uint16_t>(kButtonX);
        data.keys[kActionTurnRight] = static_cast<std::uint16_t>(kButtonA);
        std::vector<std::uint8_t> written = writeSettings(data);
        SettingsData back;
        check(readSettings(written.data(), written.size(), back) && back.controlScheme == kControlsButtons &&
                  back.keys[kActionLookUp] == kButtonX && back.keys[kActionTurnRight] == kButtonA &&
                  written.size() <= kSettingsMostBytes,
              "buttons only and the look buttons are kept, and the file still fits");
        SettingsData older;
        older.controlScheme = kControlsCustom;
        older.keys[kActionFire] = static_cast<std::uint16_t>(kButtonL);
        older.keys[kActionLookUp] = static_cast<std::uint16_t>(kButtonX); // (not in that version)
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        yas::mem_ostream os;
        yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
        older.serializeTwentyThree(oa);
        const auto buf = os.get_intrusive_buffer();
        SettingsHeader header{};
        std::memcpy(header.magic, kSettingsMagic, sizeof(header.magic));
        header.version = kSettingsVersionTwentyThree;
        header.payloadBytes = static_cast<std::uint16_t>(buf.size);
        header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data), static_cast<std::uint32_t>(buf.size));
        std::vector<std::uint8_t> file(sizeof(header) + buf.size);
        std::memcpy(file.data(), &header, sizeof(header));
        std::memcpy(file.data() + sizeof(header), buf.data, buf.size);
        check(readSettings(file.data(), file.size(), back) && back.controlScheme == kControlsCustom &&
                  back.keys[kActionFire] == kButtonL && back.keys[kActionLookUp] == 0,
              "a file of version 23 keeps its buttons, and has none that look");

        // The screen's shape: kept; a file of version 24 read as auto; one
        // there is not, refused.
        data.screenShape = kScreenWide;
        written = writeSettings(data);
        check(readSettings(written.data(), written.size(), back) && back.screenShape == kScreenWide,
              "the wide screen is kept");
        SettingsData before;
        before.keys[kActionLookUp] = static_cast<std::uint16_t>(kButtonX);
        yas::mem_ostream os24;
        yas::binary_oarchive<yas::mem_ostream, flags> oa24(os24);
        before.serializeTwentyFour(oa24);
        const auto buf24 = os24.get_intrusive_buffer();
        SettingsHeader header24{};
        std::memcpy(header24.magic, kSettingsMagic, sizeof(header24.magic));
        header24.version = kSettingsVersionTwentyFour;
        header24.payloadBytes = static_cast<std::uint16_t>(buf24.size);
        header24.crc32 =
            crc32(reinterpret_cast<const std::uint8_t *>(buf24.data), static_cast<std::uint32_t>(buf24.size));
        std::vector<std::uint8_t> file24(sizeof(header24) + buf24.size);
        std::memcpy(file24.data(), &header24, sizeof(header24));
        std::memcpy(file24.data() + sizeof(header24), buf24.data, buf24.size);
        back.screenShape = kScreenStandard;
        check(readSettings(file24.data(), file24.size(), back) && back.screenShape == kScreenAuto &&
                  back.keys[kActionLookUp] == kButtonX,
              "a file of version 24 keeps its buttons, and its screen is auto");
        data.screenShape = kScreenChoices;
        written = writeSettings(data);
        check(!readSettings(written.data(), written.size(), back), "a screen shape there is not: not read");
        data.screenShape = kScreenAuto;

        data.controlScheme = kControlsSchemes;
        written = writeSettings(data);
        check(!readSettings(written.data(), written.size(), back), "a layout there is not: not read");
    }

    void testWeaponStep()
    {
        Inventory inv;
        inv.have = static_cast<std::uint8_t>((1u << kWeaponCrowbar) | (1u << kWeaponShotgun) | (1u << kWeaponPython));
        inv.current = kWeaponShotgun;
        check(weaponStepped(inv, 0) == kWeaponNone, "no step: no weapon asked for");
        check(weaponStepped(inv, 1) == kWeaponPython, "the weapon after, of those held");
        check(weaponStepped(inv, -1) == kWeaponCrowbar, "the weapon before");
        inv.current = kWeaponPython;
        check(weaponStepped(inv, 1) == kWeaponCrowbar, "... round from the last to the first");
        inv.current = kWeaponCrowbar;
        check(weaponStepped(inv, -1) == kWeaponPython, "... and from the first to the last");
        inv.have = static_cast<std::uint8_t>(1u << kWeaponCrowbar);
        check(weaponStepped(inv, 1) == kWeaponNone && weaponStepped(inv, -1) == kWeaponNone,
              "with one weapon there is none to step to");
        inv.have = 1; // (the suit, which is no weapon)
        inv.current = kWeaponNone;
        check(weaponStepped(inv, 1) == kWeaponNone, "... nor with none");
    }

    void testSettings()
    {
        SettingsData data;
        data.controlScheme = kControlsModern;
        data.padTurns = 1;
        data.leftHanded = 1;
        std::vector<std::uint8_t> bytes = writeSettings(data);
        SettingsData back;
        check(bytes.size() <= kSettingsMostBytes && readSettings(bytes.data(), bytes.size(), back) &&
                  back.controlScheme == kControlsModern && back.padTurns == 1 && back.padLooks == 0 &&
                  back.leftHanded == 1,
              "the layout and the swaps are kept with the other settings");
        check(SettingsData().controlScheme == kControlsModern && SettingsData().padTurns == 0 &&
                  SettingsData().padLooks == 0,
              "... and are the modern layout, nothing swapped, unless it is said");
        data.controlScheme = kControlsSchemes;
        bytes = writeSettings(data);
        SettingsData untouched;
        check(!readSettings(bytes.data(), bytes.size(), untouched) && untouched.controlScheme == kControlsModern,
              "a layout there is not is not believed");

        // A file of the version before: all it had is kept.
        SettingsData old;
        old.leftHanded = 1;
        old.viewBob = 1;
        old.volume[kVolumeMusic] = 3;
        old.renderMode = 3;
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        yas::mem_ostream os;
        yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
        old.serializeEight(oa);
        const auto buf = os.get_intrusive_buffer();
        SettingsHeader header{};
        std::memcpy(header.magic, kSettingsMagic, sizeof(header.magic));
        header.version = kSettingsVersionBefore;
        header.payloadBytes = static_cast<std::uint16_t>(buf.size);
        header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data), static_cast<std::uint32_t>(buf.size));
        std::vector<std::uint8_t> file(sizeof(header) + buf.size);
        std::memcpy(file.data(), &header, sizeof(header));
        std::memcpy(file.data() + sizeof(header), buf.data, buf.size);
        SettingsData read;
        check(readSettings(file.data(), file.size(), read) && read.leftHanded == 1 && read.viewBob == 1 &&
                  read.volume[kVolumeMusic] == 3 && read.renderMode == 3 &&
                  read.controlScheme == kControlsClassic && read.padTurns == 0 && read.padLooks == 0,
              "settings saved before there were layouts are read as they were, with the classic layout");

        // A file of version 9: its layout kept, the rest as they come.
        {
            SettingsData nine;
            nine.controlScheme = kControlsModern;
            nine.padLooks = 1;
            nine.viewBob = 1;
            yas::mem_ostream os9;
            yas::binary_oarchive<yas::mem_ostream, flags> oa9(os9);
            nine.serializeNine(oa9);
            const auto buf9 = os9.get_intrusive_buffer();
            SettingsHeader h9{};
            std::memcpy(h9.magic, kSettingsMagic, sizeof(h9.magic));
            h9.version = kSettingsVersionNine;
            h9.payloadBytes = static_cast<std::uint16_t>(buf9.size);
            h9.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf9.data), static_cast<std::uint32_t>(buf9.size));
            std::vector<std::uint8_t> file9(sizeof(h9) + buf9.size);
            std::memcpy(file9.data(), &h9, sizeof(h9));
            std::memcpy(file9.data() + sizeof(h9), buf9.data, buf9.size);
            SettingsData read9;
            check(readSettings(file9.data(), file9.size(), read9) && read9.controlScheme == kControlsModern &&
                      read9.padLooks == 1 && read9.viewBob == 1 && read9.aimSpeed == kAimSpeedDefault &&
                      read9.pitchSpeed == kAimSpeedDefault && read9.invertPitch == 0 && read9.duckToggle == 0 &&
                      read9.firmPress == kFirmOff,
                  "settings of version 9 are read, with the aim and the new buttons at their defaults");
        }
        // Version 10: all of it kept; nonsense not believed.
        {
            SettingsData ten;
            ten.controlScheme = kControlsCustom;
            ten.aimSpeed = 17;
            ten.pitchSpeed = 3;
            ten.invertPitch = 1;
            ten.duckToggle = 1;
            ten.firmness = 9;
            ten.firmPress = kFirmUse;
            defaultKeys(kControlsModern, false, ten.keys);
            assignKey(ten.keys, kActionJump, kButtonL);
            std::vector<std::uint8_t> file10 = writeSettings(ten);
            SettingsData read10;
            check(file10.size() <= kSettingsMostBytes && readSettings(file10.data(), file10.size(), read10) &&
                      read10.controlScheme == kControlsCustom && read10.aimSpeed == 17 && read10.pitchSpeed == 3 &&
                      read10.invertPitch == 1 && read10.duckToggle == 1 && read10.firmness == 9 &&
                      read10.firmPress == kFirmUse && read10.keys[kActionJump] == kButtonL,
                  "the aim, the crouch, the stylus and a custom layout are kept");
            for (int bad = 0; bad < 4; ++bad)
            {
                SettingsData wrong = ten;
                if (bad == 0)
                    wrong.aimSpeed = 0;
                else if (bad == 1)
                    wrong.firmness = kFirmnessMost + 1;
                else if (bad == 2)
                    wrong.firmPress = kFirmActions;
                else
                    wrong.keys[kActionUse] = static_cast<std::uint16_t>(kButtonStart);
                const std::vector<std::uint8_t> written = writeSettings(wrong);
                SettingsData readBad;
                check(!readSettings(written.data(), written.size(), readBad), "a setting out of range is not believed");
            }
        }
        // Version 12: the achievements' pop-ups, on unless turned off; a
        // file of version 11, which had no say, leaves them on.
        {
            check(SettingsData().achievementPopups == 1, "the achievements' pop-ups are on unless it is said");
            SettingsData off;
            off.achievementPopups = 0;
            off.autoaim = 0;
            std::vector<std::uint8_t> written = writeSettings(off);
            SettingsData read12;
            check(readSettings(written.data(), written.size(), read12) && read12.achievementPopups == 0 &&
                      read12.autoaim == 0,
                  "pop-ups turned off are kept");
            off.achievementPopups = 2;
            written = writeSettings(off);
            check(!readSettings(written.data(), written.size(), read12), "... and nonsense is not believed");

            SettingsData eleven;
            eleven.autoaim = 0;
            yas::mem_ostream os11;
            yas::binary_oarchive<yas::mem_ostream, flags> oa11(os11);
            eleven.serializeEleven(oa11);
            const auto buf11 = os11.get_intrusive_buffer();
            SettingsHeader h11{};
            std::memcpy(h11.magic, kSettingsMagic, sizeof(h11.magic));
            h11.version = kSettingsVersionEleven;
            h11.payloadBytes = static_cast<std::uint16_t>(buf11.size);
            h11.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf11.data), static_cast<std::uint32_t>(buf11.size));
            std::vector<std::uint8_t> file11(sizeof(h11) + buf11.size);
            std::memcpy(file11.data(), &h11, sizeof(h11));
            std::memcpy(file11.data() + sizeof(h11), buf11.data, buf11.size);
            SettingsData read11;
            read11.achievementPopups = 0;
            check(readSettings(file11.data(), file11.size(), read11) && read11.autoaim == 0 &&
                      read11.achievementPopups == 1,
                  "settings of version 11 are read, with the pop-ups on");

            // Version 17: the game's own save, on unless turned off; autoaim
            // off unless asked for -- and off once for a file from before.
            check(SettingsData().autosave == 1 && SettingsData().autoaim == 0, "autosave is on and autoaim off to begin with");
            SettingsData chosen;
            chosen.autosave = 0;
            chosen.autoaim = 1;
            written = writeSettings(chosen);
            SettingsData read17;
            check(readSettings(written.data(), written.size(), read17) && read17.autosave == 0 && read17.autoaim == 1,
                  "both choices are kept");
            chosen.autosave = 2;
            written = writeSettings(chosen);
            check(!readSettings(written.data(), written.size(), read17), "... and nonsense is not believed");

            SettingsData sixteen;
            sixteen.autoaim = 1;
            yas::mem_ostream os16;
            yas::binary_oarchive<yas::mem_ostream, flags> oa16(os16);
            sixteen.serializeSixteen(oa16);
            const auto buf16 = os16.get_intrusive_buffer();
            SettingsHeader h16{};
            std::memcpy(h16.magic, kSettingsMagic, sizeof(h16.magic));
            h16.version = kSettingsVersionSixteen;
            h16.payloadBytes = static_cast<std::uint16_t>(buf16.size);
            h16.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf16.data), static_cast<std::uint32_t>(buf16.size));
            std::vector<std::uint8_t> file16(sizeof(h16) + buf16.size);
            std::memcpy(file16.data(), &h16, sizeof(h16));
            std::memcpy(file16.data() + sizeof(h16), buf16.data, buf16.size);
            SettingsData read16;
            read16.autosave = 0;
            check(readSettings(file16.data(), file16.size(), read16) && read16.autoaim == 0 && read16.autosave == 1,
                  "settings of version 16 are read, with autoaim off and autosave on");

            // The last before the walk: its custom buttons stay each action's,
            // and autoaim as it was chosen.
            SettingsData seventeen;
            seventeen.autoaim = 1;
            seventeen.autosave = 0;
            seventeen.keys[kActionLamp] = static_cast<std::uint16_t>(kButtonX);
            yas::mem_ostream os17;
            yas::binary_oarchive<yas::mem_ostream, flags> oa17(os17);
            seventeen.serializeSeventeen(oa17);
            const auto buf17 = os17.get_intrusive_buffer();
            SettingsHeader h17{};
            std::memcpy(h17.magic, kSettingsMagic, sizeof(h17.magic));
            h17.version = kSettingsVersionSeventeen;
            h17.payloadBytes = static_cast<std::uint16_t>(buf17.size);
            h17.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf17.data), static_cast<std::uint32_t>(buf17.size));
            std::vector<std::uint8_t> file17(sizeof(h17) + buf17.size);
            std::memcpy(file17.data(), &h17, sizeof(h17));
            std::memcpy(file17.data() + sizeof(h17), buf17.data, buf17.size);
            SettingsData from17;
            check(readSettings(file17.data(), file17.size(), from17) && from17.autoaim == 1 && from17.autosave == 0 &&
                      from17.keys[kActionLamp] == kButtonX && from17.keys[kActionWalk] == 0 && from17.walkToggle == 0 &&
                      from17.jumpWalks == 0,
                  "settings of version 17 are read as chosen, with no walk");
            // The newest, and its size: what the console reads it into.
            SettingsData walks;
            walks.walkToggle = 1;
            walks.jumpWalks = 1;
            walks.keys[kActionWalk] = static_cast<std::uint16_t>(kButtonY);
            written = writeSettings(walks);
            SettingsData read18;
            check(readSettings(written.data(), written.size(), read18) && read18.walkToggle == 1 && read18.jumpWalks == 1 &&
                      read18.keys[kActionWalk] == kButtonY && written.size() <= kSettingsMostBytes,
                  "the walk's settings are kept, and the file still fits");
            walks.walkToggle = 2;
            written = writeSettings(walks);
            check(!readSettings(written.data(), written.size(), read18), "... and nonsense is not believed");

            // The last before the double tap was a choice: it jumps, as it did.
            SettingsData eighteen;
            eighteen.jumpWalks = 1;
            eighteen.tapJump = 0; // (not in the file: read back as on)
            yas::mem_ostream os18;
            yas::binary_oarchive<yas::mem_ostream, flags> oa18(os18);
            eighteen.serializeEighteen(oa18);
            const auto buf18 = os18.get_intrusive_buffer();
            SettingsHeader h18{};
            std::memcpy(h18.magic, kSettingsMagic, sizeof(h18.magic));
            h18.version = kSettingsVersionEighteen;
            h18.payloadBytes = static_cast<std::uint16_t>(buf18.size);
            h18.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf18.data), static_cast<std::uint32_t>(buf18.size));
            std::vector<std::uint8_t> file18(sizeof(h18) + buf18.size);
            std::memcpy(file18.data(), &h18, sizeof(h18));
            std::memcpy(file18.data() + sizeof(h18), buf18.data, buf18.size);
            SettingsData from18;
            check(readSettings(file18.data(), file18.size(), from18) && from18.jumpWalks == 1 && from18.tapJump == 1,
                  "settings of version 18 are read, the double tap jumping");
            SettingsData noTap;
            noTap.tapJump = 0;
            written = writeSettings(noTap);
            SettingsData read19;
            check(readSettings(written.data(), written.size(), read19) && read19.tapJump == 0 &&
                      written.size() <= kSettingsMostBytes,
                  "the double tap turned off is kept, and the file still fits");
            noTap.tapJump = 2;
            written = writeSettings(noTap);
            check(!readSettings(written.data(), written.size(), read19), "... and nonsense is not believed");

            // The last before a tap was a choice: read with taps not USE.
            SettingsData nineteen;
            nineteen.tapJump = 0;
            nineteen.tapUse = 1; // (not in the file)
            yas::mem_ostream os19;
            yas::binary_oarchive<yas::mem_ostream, flags> oa19(os19);
            nineteen.serializeNineteen(oa19);
            const auto buf19 = os19.get_intrusive_buffer();
            SettingsHeader h19{};
            std::memcpy(h19.magic, kSettingsMagic, sizeof(h19.magic));
            h19.version = kSettingsVersionNineteen;
            h19.payloadBytes = static_cast<std::uint16_t>(buf19.size);
            h19.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf19.data), static_cast<std::uint32_t>(buf19.size));
            std::vector<std::uint8_t> file19(sizeof(h19) + buf19.size);
            std::memcpy(file19.data(), &h19, sizeof(h19));
            std::memcpy(file19.data() + sizeof(h19), buf19.data, buf19.size);
            SettingsData from19;
            check(readSettings(file19.data(), file19.size(), from19) && from19.tapJump == 0 && from19.tapUse == 0,
                  "settings of version 19 are read, with a tap not USE");
            SettingsData taps;
            taps.tapUse = 1;
            written = writeSettings(taps);
            SettingsData read20;
            check(readSettings(written.data(), written.size(), read20) && read20.tapUse == 1 &&
                      written.size() <= kSettingsMostBytes,
                  "tap to use asked for is kept");

            // Version 20, before the Cheats switch: every old setting kept,
            // and the switch off.
            SettingsData twenty;
            twenty.tapUse = 1;
            twenty.autosave = 0;
            twenty.aimSpeed = 7;
            twenty.cheats = 1; // (not in the file)
            twenty.keyboardLayout = kKeyboardAzerty; // (nor this)
            yas::mem_ostream os20;
            yas::binary_oarchive<yas::mem_ostream, flags> oa20(os20);
            twenty.serializeTwenty(oa20);
            const auto buf20 = os20.get_intrusive_buffer();
            SettingsHeader h20{};
            std::memcpy(h20.magic, kSettingsMagic, sizeof(h20.magic));
            h20.version = kSettingsVersionTwenty;
            h20.payloadBytes = static_cast<std::uint16_t>(buf20.size);
            h20.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf20.data), static_cast<std::uint32_t>(buf20.size));
            std::vector<std::uint8_t> file20(sizeof(h20) + buf20.size);
            std::memcpy(file20.data(), &h20, sizeof(h20));
            std::memcpy(file20.data() + sizeof(h20), buf20.data, buf20.size);
            SettingsData from20;
            check(readSettings(file20.data(), file20.size(), from20) && from20.tapUse == 1 && from20.autosave == 0 &&
                      from20.aimSpeed == 7 && from20.cheats == 0 &&
                      from20.keyboardLayout == kKeyboardQwerty,
                  "settings of version 20 are read, every one kept, with cheats off and the keyboard QWERTY");
            SettingsData cheating;
            cheating.cheats = 1;
            written = writeSettings(cheating);
            SettingsData read21;
            check(readSettings(written.data(), written.size(), read21) && read21.cheats == 1 &&
                      written.size() <= kSettingsMostBytes,
                  "cheats turned on are kept, and the file still fits");
            check(SettingsData().cheats == 0, "... and they are off until asked for");
            cheating.cheats = 2;
            written = writeSettings(cheating);
            check(!readSettings(written.data(), written.size(), read21), "... and nonsense is not believed");

            // The keyboard's layout (version 21 too).
            SettingsData azerty;
            azerty.keyboardLayout = kKeyboardAzerty;
            written = writeSettings(azerty);
            check(readSettings(written.data(), written.size(), read21) && read21.keyboardLayout == kKeyboardAzerty &&
                      written.size() <= kSettingsMostBytes,
                  "an AZERTY keyboard is kept, and the file still fits");
            check(SettingsData().keyboardLayout == kKeyboardQwerty, "... QWERTY until asked otherwise");
            azerty.keyboardLayout = kKeyboardLayouts;
            written = writeSettings(azerty);
            check(!readSettings(written.data(), written.size(), read21), "... and a layout there is not, not believed");

            // A file whose length is not its version's (an earlier build's
            // version 21, a byte short): refused, not read past its end.
            SettingsHeader shortHeader = h20;
            shortHeader.version = kSettingsVersion;
            std::vector<std::uint8_t> shortFile = file20;
            std::memcpy(shortFile.data(), &shortHeader, sizeof(shortHeader));
            check(settingsPayloadBytes(kSettingsVersion) == written.size() - sizeof(SettingsHeader) &&
                      !readSettings(shortFile.data(), shortFile.size(), read21),
                  "a file too short for its version is refused");
        }
    }

    /// The walk: on the screen, on a button, held or toggled; and the jump
    /// button asked to walk.
    void testWalk()
    {
        std::uint32_t blank = 100;
        ControlSettings set;
        set.scheme = kControlsModern;
        ControlsState state;
        const TouchLayout at = touchLayout(false);
        const int wx = at.walk.x + at.walk.w / 2, wy = at.walk.y + at.walk.h / 2;
        ControlsOut out = press(set, state, 0, blank);
        check(!out.walk, "a run, unless told otherwise");
        out = touch(set, state, wx, wy, blank);
        check(out.walk && out.touchOn == kTouchWalk, "the screen's WALK, held, walks");
        out = touch(set, state, 100, 90, blank);
        check(out.walk && out.touchOn == kTouchWalk && out.lookDx == 0, "... slid off it, still walks, with no jerk");
        out = touch(set, state, 110, 90, blank);
        check(out.walk && out.lookDx == 10, "... and turns the view");
        out = press(set, state, 0, blank);
        check(!out.walk && !out.usePressed && !out.jump, "... and lifted, runs: it was no tap");

        set.walkToggle = true;
        state = ControlsState();
        out = touch(set, state, wx, wy, blank);
        check(out.walk, "toggled: touched once, walks");
        out = press(set, state, 0, blank);
        check(out.walk, "... and goes on when lifted");
        out = touch(set, state, 100, 90, blank);
        out = touch(set, state, 110, 90, blank);
        check(out.walk && out.lookDx == 10 && out.touchOn == kTouchAim, "... with the stylus free to look");
        out = press(set, state, 0, blank);
        out = touch(set, state, wx, wy, blank);
        check(!out.walk, "... and touched again, runs");
        out = press(set, state, 0, blank);
        check(!out.walk, "... and goes on running");
        out = touch(set, state, wx, wy, blank);
        set.walkToggle = false;
        out = press(set, state, 0, blank);
        check(!out.walk, "the toggle turned off leaves nobody walking");

        // A button of a custom layout: held, and toggled.
        ControlSettings custom;
        custom.scheme = kControlsCustom;
        defaultKeys(kControlsModern, false, custom.keys);
        assignKey(custom.keys, kActionWalk, kButtonSelect);
        state = ControlsState();
        check(press(custom, state, kButtonSelect, blank).walk, "a custom layout's button walks while held");
        check(!press(custom, state, 0, blank).walk, "... and runs when let go");
        custom.walkToggle = true;
        check(press(custom, state, kButtonSelect, blank).walk, "... or, toggled, walks when pressed");
        check(press(custom, state, 0, blank).walk, "... goes on");
        press(custom, state, kButtonSelect, blank);
        check(!press(custom, state, 0, blank).walk, "... and pressed again, runs");

        // The jump button asked to walk.
        for (const bool left : {false, true})
        {
            ControlSettings walks;
            walks.scheme = kControlsModern;
            walks.leftHanded = left;
            walks.jumpWalks = true;
            const std::uint32_t button = left ? kButtonRight : kButtonA;
            state = ControlsState();
            out = press(walks, state, button, blank);
            check(out.walk && !out.jump, "modern's jump button walks, by either hand");
            press(walks, state, 0, blank);
            const TouchLayout where = touchLayout(left, false, true);
            out = touch(walks, state, where.walk.x + 5, where.walk.y + 5, blank);
            check(out.walk && !out.jump, "... so does the screen's");
            press(walks, state, 0, blank);
            blank += 100;
            const int mid = where.pad.x + where.pad.w / 2;
            touch(walks, state, mid, 90, blank);
            press(walks, state, 0, blank);
            out = touch(walks, state, mid, 90, blank);
            check(out.jump, "... and two taps still jump");
            press(walks, state, 0, blank);
        }
        ControlSettings classicWalks;
        classicWalks.scheme = kControlsClassic;
        classicWalks.jumpWalks = true;
        state = ControlsState();
        out = press(classicWalks, state, kButtonA, blank);
        check(out.moveSide == 1 && !out.walk, "classic's A is still the pad's: it has no jump button to give");

        // The double tap turned off: two taps on the aim pad do not jump --
        // unless nothing else would (the jump button walking, and no button
        // that jumps), when they still do.
        {
            auto twoTaps = [&](const ControlSettings &set)
            {
                ControlsState s;
                const TouchLayout where = touchLayout(set.leftHanded, false, set.jumpWalks);
                const int mid = where.pad.x + where.pad.w / 2;
                blank += 100;
                touch(set, s, mid, 90, blank);
                press(set, s, 0, blank);
                const bool jumped = touch(set, s, mid, 90, blank).jump;
                press(set, s, 0, blank);
                return jumped;
            };
            ControlSettings modern;
            modern.scheme = kControlsModern;
            modern.tapJump = false;
            check(!twoTaps(modern) && jumpsOtherwise(modern), "the double tap off: two taps do not jump");
            modern.tapJump = true;
            check(twoTaps(modern), "... on: they do");
            ControlSettings only = classicWalks;
            only.tapJump = false;
            check(!jumpsOtherwise(only) && twoTaps(only), "with no other way to jump, two taps still do");
            ControlSettings custom;
            custom.scheme = kControlsCustom;
            custom.jumpWalks = true;
            custom.tapJump = false;
            defaultKeys(kControlsModern, false, custom.keys);
            check(jumpsOtherwise(custom) && !twoTaps(custom), "... but a button of one's own that jumps is one");
        }
    }

    /// LONG JUMP: there with the module, a crouch and a jump at once, and the
    /// crouch held a moment longer than the shortest touch.
    void testLongJumpTouch()
    {
        ControlSettings set;
        const TouchLayout none = touchLayout(false);
        check(none.longJump.w == 0, "no LONG JUMP without the module");
        set.longJump = true;
        const TouchLayout at = touchLayout(false, false, false, true);
        check(at.longJump.w > 0 && at.jump.y + at.jump.h <= at.longJump.y && at.jump.x == at.longJump.x,
              "with it, LONG JUMP is under JUMP");
        std::uint32_t blank = 500;
        ControlsState state;
        press(set, state, kButtonUp, blank);
        ControlsOut out = touch(set, state, at.longJump.x + 5, at.longJump.y + 5, blank);
        check(out.jump && out.duck && out.touchOn == kTouchLongJump && out.lookDx == 0, "a touch is a crouch and a jump, on the same frame");
        out = press(set, state, 0, blank);
        check(!out.jump && out.duck, "lifted at once, the crouch stays a moment");
        for (std::uint32_t n = 0; n < kLongJumpBlanks; ++n)
            out = press(set, state, 0, blank);
        check(!out.duck, "... and then goes");
        const TouchLayout walks = touchLayout(false, false, true, true);
        check(walks.walk.y == at.jump.y && walks.walk.h == at.jump.h && walks.longJump.y == at.longJump.y,
              "with the jump button walking, LONG JUMP is under WALK");
        set.jumpWalks = true;
        state = ControlsState();
        out = touch(set, state, walks.longJump.x + 5, walks.longJump.y + 5, blank);
        check(out.jump && out.duck, "... and still jumps");
    }

    void testCustom()
    {
        ControlSettings set;
        set.scheme = kControlsCustom;
        defaultKeys(kControlsModern, false, set.keys);
        // Jump onto L: L was fire, which is given A (jump's old button).
        assignKey(set.keys, kActionJump, kButtonL);
        check(set.keys[kActionJump] == kButtonL && set.keys[kActionFire] == kButtonA,
              "a button given to one action is taken from another, which has the first's instead");
        ControlsState state;
        std::uint32_t blank = 100;
        ControlsOut out = press(set, state, kButtonL, blank);
        check(out.jump && !out.attack, "custom: L jumps");
        out = press(set, state, kButtonA, blank);
        check(out.attack && !out.jump, "... and A fires");
        assignKey(set.keys, kActionUse, kButtonStart);
        check(set.keys[kActionUse] == 0, "Start is the menu, and nothing else");
        out = press(set, state, kButtonStart, blank);
        check(out.startPressed && !out.usePressed, "... and opens it whatever the layout");
        // A crouch that toggles.
        set.duckToggle = true;
        press(set, state, 0, blank);
        out = press(set, state, kButtonB, blank);
        check(out.duck, "toggle: B crouches");
        out = press(set, state, 0, blank);
        check(out.duck, "... and let go, stays crouched");
        out = press(set, state, kButtonB, blank);
        check(!out.duck, "... until pressed again");
        // The legend says what the table says.
        bool saysL = false;
        for (int line = 0; controlsLegend(set, line) != nullptr; ++line)
        {
            const char *text = controlsLegend(set, line);
            saysL = saysL || (text[0] == 'L' && text[1] == ' ' && std::strstr(text, "jump") != nullptr);
        }
        check(saysL, "the options' lines say L jumps");
    }

    /// The stylus finely, and how hard it presses.
    void testTouchFilter()
    {
        const TouchLayout at = touchLayout(false);
        const int x = at.pad.x + 80, y = at.pad.y + 60;
        auto fine = [&](const ControlSettings &set, ControlsState &state, int fx, int fy, int pressure,
                        std::uint32_t &blank)
        {
            ControlsIn in;
            in.touching = true;
            in.touchX = fx >> kFineShift;
            in.touchY = fy >> kFineShift;
            in.fineX = fx;
            in.fineY = fy;
            in.pressure = pressure;
            in.blank = ++blank;
            return controlsStep(set, in, state);
        };
        // No pressure: each step as it comes, finely.
        {
            ControlSettings set;
            set.firmness = 6;
            ControlsState state;
            std::uint32_t blank = 1;
            fine(set, state, x * 16, y * 16, 0, blank);
            ControlsOut out = fine(set, state, x * 16 + 37, y * 16 - 5, 0, blank);
            check(out.lookFineX == 37 && out.lookFineY == -5, "no pressure: finer than a pixel, at once");
            press(set, state, 0, blank);
        }
        // Pixels only, as ever: sixteen sixteenths a pixel.
        {
            ControlSettings set;
            ControlsState state;
            std::uint32_t blank = 1;
            touch(set, state, x, y, blank);
            ControlsOut out = touch(set, state, x + 3, y, blank);
            check(out.lookFineX == 3 * 16 && out.lookDx == 3, "a pixel is sixteen sixteenths");
        }
        // With pressure: averaged with the reading before; a light reading
        // (the stylus leaving, far off) left out.
        {
            ControlSettings set;
            set.firmness = 6;
            ControlsState state;
            std::uint32_t blank = 1;
            int sum = 0;
            fine(set, state, x * 16, y * 16, 400, blank);
            ControlsOut out = fine(set, state, x * 16 + 32, y * 16, 400, blank);
            check(out.lookFineX == 16, "pressure: a step, averaged with the reading before");
            sum += out.lookFineX;
            out = fine(set, state, x * 16 + 64, y * 16, 400, blank);
            check(out.lookFineX == 32, "... and the next");
            sum += out.lookFineX;
            out = fine(set, state, x * 16 + 64 + 800, y * 16, 4000, blank); // (lifting: light, and far)
            check(out.lookFineX == 0, "a light reading is left out");
            sum += out.lookFineX;
            out = press(set, state, 0, blank);
            check(out.lookFineX == 0 && sum == 48, "... and the view is not thrown by the stylus leaving");
        }
        // Averaged, the firmer counting for more.
        {
            ControlSettings set;
            set.firmness = 1;
            ControlsState state;
            std::uint32_t blank = 1;
            fine(set, state, x * 16, y * 16, 100, blank);
            fine(set, state, x * 16 + 160, y * 16, 300, blank); // (a light one: counts for a third)
            const ControlsOut out = fine(set, state, x * 16 + 160, y * 16, 300, blank);
            check(out.lookFineX > 0 && out.lookFineX < 160, "two readings averaged, by how firm each was");
        }
        // The firm press: once a press for USE, held for fire.
        {
            ControlSettings set;
            set.firmPress = kFirmUse;
            ControlsState state;
            std::uint32_t blank = 1;
            for (int i = 0; i < 5; ++i)
                fine(set, state, x * 16, y * 16, 800, blank);
            ControlsOut out = fine(set, state, x * 16, y * 16, 300, blank);
            check(out.firm && out.usePressed, "pressed hard: USE");
            out = fine(set, state, x * 16, y * 16, 300, blank);
            check(out.firm && !out.usePressed, "... once a press");
            out = fine(set, state, x * 16, y * 16, 500, blank);
            check(out.firm, "... still firm until it eases past two thirds");
            out = fine(set, state, x * 16, y * 16, 700, blank);
            check(!out.firm, "... and then not");
            set.firmPress = kFirmFire;
            out = fine(set, state, x * 16, y * 16, 200, blank);
            check(out.attack, "a firm press can fire, for as long as it is held");
        }
    }

    void testSensitivity()
    {
        check(turnOf(16, kAimSpeedDefault) == 128, "at the speed of 10, a pixel is 1/512 of a turn, as ever");
        check(turnOf(16, 20) == 256 && turnOf(16, 5) == 64, "... twice that at 20, half at 5");
        check(turnOf(-48, kAimSpeedDefault) == -384, "... either way");
    }
} // namespace

namespace
{
    /// USE held: its button, or the touch screen's USE, for as long as it
    /// is down -- what a wall charger is used by; a tap of the aim pad is a
    /// press and no more.
    void testUseHeld()
    {
        // (A layout of the player's own, with USE on A: the others have it
        // on the touch screen only.)
        ControlSettings set;
        set.scheme = kControlsCustom;
        set.keys[kActionUse] = kButtonA;
        std::uint16_t keys[kActionCount];
        keysOf(set, keys);
        ControlsState state;
        std::uint32_t blank = 100;
        ControlsOut out = press(set, state, keys[kActionUse], blank);
        check(out.usePressed && out.useHeld, "USE's button: pressed, and held");
        out = press(set, state, keys[kActionUse], blank);
        check(!out.usePressed && out.useHeld, "... held, it is held and not pressed again");
        out = press(set, state, 0, blank);
        check(!out.useHeld, "... let go, not held");
        const TouchLayout at = touchLayout(false);
        out = touch(set, state, at.use.x + 2, at.use.y + 2, blank);
        check(out.usePressed && out.useHeld, "the touch screen's USE: pressed, and held");
        out = touch(set, state, at.use.x + 3, at.use.y + 2, blank);
        check(!out.usePressed && out.useHeld, "... and held while the stylus stays");
        out = press(set, state, 0, blank);
        check(!out.useHeld, "... and not once it is lifted");
        ControlSettings modern;
        modern.scheme = kControlsModern;
        modern.tapUse = true;   // (asked for)
        modern.tapJump = false; // (so that it is USE as it lifts, not after the double tap's time)
        ControlsState tapped;
        out = touch(modern, tapped, at.pad.x + 80, at.pad.y + 60, blank);
        check(!out.useHeld, "a touch of the aim pad is not USE held");
        out = press(modern, tapped, 0, blank);
        check(out.usePressed && !out.useHeld, "... its tap is USE pressed, once, and not held");
    }
} // namespace

int main()
{
    testClassicIsAsBefore();
    testModern();
    testCrouchAim();
    testWeaponList();
    testTaps();
    testHand();
    testSticks();
    testButtonsOnly();
    testWeaponStep();
    testSettings();
    testWalk();
    testLongJumpTouch();
    testCustom();
    testTouchFilter();
    testSensitivity();
    testUseHeld();
    if (g_failures == 0)
        std::printf("test_controls: ok\n");
    return g_failures == 0 ? 0 : 1;
}
