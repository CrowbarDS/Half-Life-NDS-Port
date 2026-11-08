#pragma once

// A weapon chosen as the game's numbered menu chooses one, with the stylus:
// a slot touched slides up the list of that slot's weapons (those carried,
// in the game's order) above it, over the aim pad and the column beside it; a weapon in it touched --
// or the slot pressed and the stylus slid up onto one and lifted -- is taken.
// A touch anywhere else, the same slot again, or three seconds of nothing,
// puts it away. A slot with only one weapon in it takes that one at once.
//
// What a touch of the list is, is decided here, before the controls are
// asked (HlControls.hpp): a touch that is the list's is not theirs. Drawn by
// render/BottomScreen; std-only, for the host tests.

#include <cstdint>

#include "HlPlayerMove.hpp"
#include "HlTouchLayout.hpp"

namespace hldata
{
    inline constexpr int kWeaponListRow = 24;     ///< a weapon's row, high enough for a finger
    inline constexpr int kWeaponListWidth = 132;  ///< its picture and its name
    inline constexpr int kWeaponListMost = 4;     ///< the most weapons a slot has
    inline constexpr std::uint32_t kWeaponListIdle = 180; ///< blanks of nothing until it goes (3 s)
    /// Blanks the stylus rests on a weapon before the menu's "move" is
    /// heard for it (a sixth of a second): slid across rows, only the one
    /// it stops on is.
    inline constexpr std::uint32_t kWeaponListSettle = 10;
    /// How far inside the screen's sides it stays.
    inline constexpr int kWeaponListInset = 2;

    struct WeaponList
    {
        int category = 0; ///< the slot's kind of weapon (1..5), 0 none
        int count = 0;
        std::uint8_t weapons[kWeaponListMost] = {};
        std::uint32_t empty = 0; ///< a bit per kWeapon*: carried with nothing to fire
        int hover = -1;          ///< the row under the stylus (-1 none)
        int heard = -1;          ///< the row the menu's "move" was last heard for
        std::uint32_t hoverSince = 0; ///< the blank the stylus came onto `hover`
        int slide = 0;           ///< how far up it has slid, of 256
        bool closing = false;    ///< sliding away
        bool owned = false;      ///< the touch down now is the list's
        std::uint32_t lastBlank = 0;

        bool open() const { return category != 0; }
    };

    /// The row the list stands on (it is never drawn below): inside the pad's
    /// frame, which the pad's own drawing covers.
    inline int weaponListFloor(const TouchLayout &at)
    {
        return at.pad.y + at.pad.h - 4;
    }

    /// Where the list is when it is all the way up: over its slot, centred on
    /// it as far as the screen allows, risen out of the pad's bottom edge. It
    /// may cover the column (JUMP, the lamp, DUCK) beside the pad, which is
    /// drawn again with the pad under it (BottomScreen); never the slots, nor
    /// the bar.
    inline TouchRect weaponListRect(const WeaponList &list, const TouchLayout &at)
    {
        const TouchRect &slot = at.slot[list.category > 0 ? list.category - 1 : 0];
        const int high = list.count * kWeaponListRow + 4;
        int x = slot.x + slot.w / 2 - kWeaponListWidth / 2;
        x = x < kWeaponListInset ? kWeaponListInset : x;
        x = x + kWeaponListWidth > kTouchWidth - kWeaponListInset ? kTouchWidth - kWeaponListInset - kWeaponListWidth
                                                                   : x;
        return TouchRect{static_cast<std::int16_t>(x), static_cast<std::int16_t>(weaponListFloor(at) - high),
                         static_cast<std::int16_t>(kWeaponListWidth), static_cast<std::int16_t>(high)};
    }

    /// The row of the list at (x, y), -1 none.
    inline int weaponListRow(const WeaponList &list, const TouchLayout &at, int x, int y)
    {
        const TouchRect r = weaponListRect(list, at);
        if (!r.has(x, y) || y < r.y + 2)
            return -1;
        const int row = (y - r.y - 2) / kWeaponListRow;
        return row < list.count ? row : -1;
    }

    /// Slot `category` touched: its weapons carried (`have`, a bit per
    /// kWeapon*), up the screen. A weapon to take at once if it is the one
    /// there is (0 if the list is up, or there is none).
    inline int weaponListOpen(WeaponList &list, int category, unsigned have, std::uint32_t empty, std::uint32_t blank)
    {
        int count = 0;
        const std::uint8_t *kinds = weaponsOfCategory(category, count);
        WeaponList fresh;
        for (int k = 0; k < count && fresh.count < kWeaponListMost; ++k)
        {
            if (have & (1u << kinds[k]))
                fresh.weapons[fresh.count++] = kinds[k];
        }
        if (fresh.count == 0)
            return 0;
        if (fresh.count == 1)
        {
            list.closing = list.open();
            return fresh.weapons[0];
        }
        fresh.category = category;
        fresh.empty = empty;
        fresh.lastBlank = blank;
        // (Over one already up: in its place, as far up as it was.)
        fresh.slide = list.open() && !list.closing ? list.slide : 0;
        list = fresh;
        return 0;
    }

    struct WeaponListTouch
    {
        bool consumed = false; ///< the controls are not to see this touch
        int chosen = 0;        ///< a kWeapon* taken, or 0
        bool moved = false;    ///< the stylus came to rest on another weapon of it (the game's "move select")
        bool dismissed = false; ///< it was put away with nothing taken
    };

    /// A frame of the stylus. `landed`: it came down this frame. (The touch
    /// that opened the list -- its slot, pressed -- is the list's too, once
    /// open: `owned`, so that slid up onto a weapon and lifted, it takes it.)
    inline WeaponListTouch weaponListStep(WeaponList &list, const TouchLayout &at, bool touching, bool landed, int x,
                                          int y, std::uint32_t blank)
    {
        WeaponListTouch out;
        // Sliding up, or away.
        if (list.open())
        {
            const int to = list.closing ? 0 : 256;
            int step = (to - list.slide) / 3;
            step = step > 0 && step < 24 ? 24 : (step < 0 && step > -24 ? -24 : step);
            list.slide += step;
            list.slide = list.slide < 0 ? 0 : (list.slide > 256 ? 256 : list.slide);
            if (list.closing && list.slide == 0)
            {
                list = WeaponList();
                return out;
            }
        }
        if (!list.open() || list.closing)
            return out;
        if (touching)
        {
            list.lastBlank = blank;
            if (landed)
            {
                const int row = weaponListRow(list, at, x, y);
                if (row >= 0)
                {
                    list.owned = true;
                    list.hover = list.heard = row;
                    list.hoverSince = blank;
                }
                else
                {
                    // Elsewhere: away it goes -- and a touch of its own slot
                    // is only that (another slot is for the controls: its
                    // own list).
                    list.closing = true;
                    list.hover = -1;
                    out.dismissed = true;
                    out.consumed = at.slot[list.category - 1].has(x, y);
                    return out;
                }
            }
            else if (list.owned)
            {
                const int row = weaponListRow(list, at, x, y);
                if (row != list.hover)
                    list.hoverSince = blank;
                list.hover = row;
                // (Heard once it has stopped there, not for each row crossed.)
                if (row >= 0 && row != list.heard && blank - list.hoverSince >= kWeaponListSettle)
                {
                    out.moved = true;
                    list.heard = row;
                }
            }
            out.consumed = list.owned;
            return out;
        }
        // Lifted: on a weapon, it is taken; anywhere else (still on the slot
        // that opened it, say) the list stays to be touched.
        if (list.owned)
        {
            list.owned = false;
            if (list.hover >= 0)
            {
                out.chosen = list.weapons[list.hover];
                list.closing = true;
            }
            list.hover = -1;
            out.consumed = true;
            return out;
        }
        if (blank - list.lastBlank > kWeaponListIdle)
        {
            list.closing = true;
            out.dismissed = true;
        }
        return out;
    }
} // namespace hldata
