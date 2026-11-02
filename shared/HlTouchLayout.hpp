#pragma once

// Where things are on the touch screen while the game is played: one table,
// used by the code that tests a touch (Input) and by the code that draws
// (BottomScreen), so the two cannot disagree.
//
//    0 +--------------------------------+
//      |[ MENU ] [     USE     ] [RELOAD]|   the bar
//   24 |+------------------------+ +---+|
//      ||                        | | ^ ||   JUMP
//      ||        the aim pad     | +---+|
//      ||                        | | * ||   the lamp, and what is in it
//      ||                        | +---+|
//      ||                        | | v ||   DUCK
//  156 |+------------------------+ +---+|
//  158 |[crow][9mm ][shot][gren][mp5 ][357]|   a slot for each weapon
//  192 +--------------------------------+
//
// The bar has WALK too, beside a narrower USE; or, if the player has asked for
// the jump button to walk (a double tap still jumps), WALK is where JUMP was
// and the bar is as drawn. In a match between consoles the bar has SPRAY too,
// and everything on it is narrower.
//
// That is for a stylus in the right hand. Mirrored, for the left, the column
// and the bar's ends change sides; the weapons stay in their order.
//
// (What the suit says -- health, its charge -- is on the top screen, and
// not said again here.)
//
// Whatever is not a control is the aim pad: a touch anywhere else turns the
// view, as one inside the frame does.
//
// std-only and inline: the host tests run it.

#include <cstdint>
#include <initializer_list>

namespace hldata
{
    struct TouchRect
    {
        std::int16_t x, y, w, h;
        constexpr bool has(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    };

    inline constexpr int kTouchSlots = 5; ///< slot n is the game's slot n + 1: a kind of weapon (weaponsOfCategory)

    // What a touch is on.
    inline constexpr int kTouchAim = 0;
    inline constexpr int kTouchMenu = 1;
    inline constexpr int kTouchUse = 2;
    inline constexpr int kTouchJump = 3;
    inline constexpr int kTouchDuck = 4;
    inline constexpr int kTouchReload = 5;
    inline constexpr int kTouchLamp = 6;
    inline constexpr int kTouchSlot = 7; ///< ... and the four after it
    inline constexpr int kTouchSpray = kTouchSlot + kTouchSlots; ///< (in a match)
    inline constexpr int kTouchWalk = kTouchSpray + 1;

    struct TouchLayout
    {
        TouchRect menu, use, reload, pad, jump, lamp, duck;
        TouchRect spray; ///< none (no width) but in a match
        TouchRect walk;
        TouchRect slot[kTouchSlots];
    };

    inline constexpr int kTouchWidth = 256;
    inline constexpr int kTouchBarHeight = 22;
    inline constexpr int kTouchSlotTop = 158;

    inline constexpr TouchLayout touchLayout(bool mirrored, bool match = false, bool jumpWalks = false)
    {
        TouchLayout at{};
        at.menu = {0, 0, 64, kTouchBarHeight};
        at.use = {68, 0, 120, kTouchBarHeight};
        at.spray = {0, 0, 0, 0};
        if (match)
        {
            at.use = {68, 0, 62, kTouchBarHeight};
            at.spray = {134, 0, 54, kTouchBarHeight};
            if (!jumpWalks)
            {
                at.menu = {0, 0, 44, kTouchBarHeight};
                at.walk = {46, 0, 44, kTouchBarHeight};
                at.use = {92, 0, 50, kTouchBarHeight};
                at.spray = {144, 0, 46, kTouchBarHeight};
            }
        }
        else if (!jumpWalks)
        {
            at.use = {68, 0, 62, kTouchBarHeight};
            at.walk = {134, 0, 54, kTouchBarHeight};
        }
        at.reload = {192, 0, 64, kTouchBarHeight};
        at.pad = {0, 24, 208, 132};
        at.jump = {212, 24, 44, 48};
        at.lamp = {212, 74, 44, 32};
        at.duck = {212, 108, 44, 48};
        if (jumpWalks)
        {
            at.walk = at.jump;
            at.jump = {0, 0, 0, 0};
        }
        if (mirrored)
        {
            for (TouchRect *r : {&at.menu, &at.use, &at.reload, &at.pad, &at.jump, &at.lamp, &at.duck, &at.spray, &at.walk})
                r->x = static_cast<std::int16_t>(kTouchWidth - r->x - r->w);
        }
        for (int n = 0; n < kTouchSlots; ++n)
            at.slot[n] = {static_cast<std::int16_t>(3 + 50 * n), kTouchSlotTop, 50, 34};
        return at;
    }

    /// What the point (x, y) is on: a kTouch*.
    inline constexpr int touchHit(const TouchLayout &at, int x, int y)
    {
        if (at.menu.has(x, y))
            return kTouchMenu;
        if (at.use.has(x, y))
            return kTouchUse;
        if (at.spray.has(x, y))
            return kTouchSpray;
        if (at.reload.has(x, y))
            return kTouchReload;
        if (at.walk.has(x, y))
            return kTouchWalk;
        if (at.jump.has(x, y))
            return kTouchJump;
        if (at.lamp.has(x, y))
            return kTouchLamp;
        if (at.duck.has(x, y))
            return kTouchDuck;
        for (int n = 0; n < kTouchSlots; ++n)
        {
            if (at.slot[n].has(x, y))
                return kTouchSlot + n;
        }
        return kTouchAim;
    }
} // namespace hldata
