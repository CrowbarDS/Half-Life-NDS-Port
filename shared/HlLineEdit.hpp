#pragma once

// A line being typed on the touch screen's keyboard (source/ui/Keyboard.cpp):
// where the cursor is, what is selected, whether the next letter is a capital,
// and which letters are on which keys. What is drawn, and the touches, are
// the keyboard's.
//
// std-only and inline: the host tests run it (test_lineedit).

#include <cstddef>
#include <cstdint>
#include <string>

namespace hldata
{
    /// Where the letters are: OTHER's "Keyboard".
    enum KeyboardLayout : std::uint8_t
    {
        kKeyboardQwerty,
        kKeyboardAzerty,
        kKeyboardLayouts,
    };

    /// The keys, four rows of ten, in each layout: the digits first (the
    /// same in both, and never shifted), '<' rubbing out. Both have the
    /// same characters.
    inline constexpr int kKeyRows = 4, kKeyColumns = 10;
    inline constexpr const char *kKeyTable[kKeyboardLayouts][kKeyRows] = {
        {"1234567890", "qwertyuiop", "asdfghjkl_", "zxcvbnm-.<"},
        {"1234567890", "azertyuiop", "qsdfghjklm", "wxcvbn_-.<"},
    };
    inline constexpr char kKeyRubOut = '<';
    /// Two taps on SHIFT this close together (in frames, 0.4 s) lock it.
    inline constexpr std::uint32_t kCapsLockFrames = 24;

    enum class Shift : std::uint8_t
    {
        Off,
        Once,   ///< the next character typed, then off
        Locked, ///< until SHIFT again
    };

    struct LineEdit
    {
        std::string &text;
        std::size_t most;
        std::size_t cursor = 0; ///< where what is typed goes: 0..size()
        std::size_t anchor = 0; ///< where a selection started; == cursor: none
        std::size_t scroll = 0; ///< the first character on show
        Shift shift = Shift::Off;
        std::uint32_t lastShiftTap = 0;

        LineEdit(std::string &line, std::size_t longest) : text(line), most(longest) { replaced(); }

        /// What a key in `layout` types (capitals: a letter's capital).
        static char keyChar(int layout, int row, int col, bool capitals)
        {
            if (layout < 0 || layout >= kKeyboardLayouts || row < 0 || row >= kKeyRows || col < 0 ||
                col >= kKeyColumns)
                return ' ';
            const char c = kKeyTable[layout][row][col];
            return capitals && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        }

        bool capitals() const { return shift != Shift::Off; }
        bool selecting() const { return anchor != cursor; }
        std::size_t selectionStart() const { return anchor < cursor ? anchor : cursor; }
        std::size_t selectionEnd() const { return anchor < cursor ? cursor : anchor; }

        /// The cursor at `at` (as far as the line goes), nothing selected.
        void place(std::size_t at)
        {
            cursor = anchor = at > text.size() ? text.size() : at;
        }
        /// The cursor to `at`, what is between it and where the selection
        /// started selected.
        void extendTo(std::size_t at) { cursor = at > text.size() ? text.size() : at; }
        /// The line was set from outside: the cursor at its end.
        void replaced() { place(text.size()); }

        /// `c` typed: in place of what is selected, if anything is. Whether
        /// the line changed (not past `most`).
        bool insert(char c)
        {
            const std::size_t from = selectionStart(), to = selectionEnd();
            const std::size_t after = text.size() - (to - from) + 1;
            if (after > most && after > text.size())
                return false;
            if (shift != Shift::Off && c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
            if (shift == Shift::Once)
                shift = Shift::Off;
            text.replace(from, to - from, 1, c);
            place(from + 1);
            return true;
        }
        /// What is selected rubbed out, or else the character before the
        /// cursor. Whether the line changed.
        bool erase()
        {
            if (selecting())
            {
                const std::size_t from = selectionStart();
                text.erase(from, selectionEnd() - from);
                place(from);
                return true;
            }
            if (cursor == 0)
                return false;
            text.erase(cursor - 1, 1);
            place(cursor - 1);
            return true;
        }
        /// A character left or right; with a selection, to its start or end.
        void left()
        {
            if (selecting())
                place(selectionStart());
            else if (cursor > 0)
                place(cursor - 1);
        }
        void right()
        {
            if (selecting())
                place(selectionEnd());
            else
                place(cursor + 1);
        }

        /// SHIFT touched on frame `frame`: once; again soon after, locked;
        /// again later, or once locked, off.
        void shiftTap(std::uint32_t frame)
        {
            if (shift == Shift::Off)
                shift = Shift::Once;
            else if (shift == Shift::Once && frame - lastShiftTap <= kCapsLockFrames)
                shift = Shift::Locked;
            else
                shift = Shift::Off;
            lastShiftTap = frame;
        }

        /// The first character on show, `shown` cells of them: the cursor's
        /// cell among them, and no more empty cells after the line than its
        /// cursor's.
        std::size_t scrollFor(std::size_t shown)
        {
            if (shown == 0)
                return scroll = cursor;
            const std::size_t cells = text.size() + 1;
            const std::size_t furthest = cells > shown ? cells - shown : 0;
            if (scroll > furthest)
                scroll = furthest;
            if (cursor < scroll)
                scroll = cursor;
            else if (cursor >= scroll + shown)
                scroll = cursor + 1 - shown;
            return scroll;
        }
    };
} // namespace hldata
