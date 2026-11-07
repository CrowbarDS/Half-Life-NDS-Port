// The touch keyboard's line (shared/HlLineEdit.hpp): typing and rubbing out
// at the cursor and over a selection, the longest a line may be, SHIFT once
// and locked, what is on show of a long line, and the two layouts' keys.

#include <cstdio>
#include <string>

#include "HlLineEdit.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    void testEditing()
    {
        std::string text = "god";
        LineEdit edit(text, 8);
        check(edit.cursor == 3 && !edit.selecting(), "the cursor starts at the line's end");
        edit.insert('x');
        check(text == "godx" && edit.cursor == 4, "typed at the end");
        edit.place(0);
        edit.insert('a');
        check(text == "agodx" && edit.cursor == 1, "typed at the start");
        edit.place(3);
        edit.insert('-');
        check(text == "ago-dx" && edit.cursor == 4, "typed in the middle");
        edit.erase();
        check(text == "agodx" && edit.cursor == 3, "rubbed out before the cursor");
        edit.place(0);
        check(!edit.erase() && text == "agodx", "nothing before the cursor to rub out");

        edit.place(1);
        edit.extendTo(4);
        check(edit.selecting() && edit.selectionStart() == 1 && edit.selectionEnd() == 4, "a selection");
        edit.insert('z');
        check(text == "azx" && edit.cursor == 2 && !edit.selecting(), "typed over what is selected");
        edit.place(3);
        edit.extendTo(0);
        check(edit.erase() && text.empty() && edit.cursor == 0, "a selection made backwards, rubbed out");

        text = "12345678";
        edit.replaced();
        check(!edit.insert('9') && text == "12345678", "no longer than the longest");
        edit.place(2);
        edit.extendTo(4);
        check(edit.insert('x') && text == "12x5678", "... but typing over a selection may shorten it");
        edit.place(0);
        edit.extendTo(1);
        check(edit.insert('y') && text == "y2x5678", "... or keep it as long");

        edit.place(99);
        check(edit.cursor == text.size(), "placed past the end: at the end");
        edit.place(1);
        edit.extendTo(99);
        check(edit.cursor == text.size() && edit.anchor == 1, "dragged past the end: to the end");

        edit.place(3);
        edit.left();
        check(edit.cursor == 2, "left");
        edit.right();
        edit.right();
        check(edit.cursor == 4, "right");
        edit.place(0);
        edit.left();
        check(edit.cursor == 0, "left at the start stays");
        edit.replaced();
        edit.right();
        check(edit.cursor == text.size(), "right at the end stays");
        edit.place(2);
        edit.extendTo(5);
        edit.left();
        check(edit.cursor == 2 && !edit.selecting(), "left with a selection: to its start");
        edit.place(5);
        edit.extendTo(2);
        edit.right();
        check(edit.cursor == 5 && !edit.selecting(), "right with a selection: to its end");

        text = "help";
        edit.place(1);
        edit.extendTo(3);
        edit.replaced();
        check(edit.cursor == 4 && !edit.selecting(), "a line set from outside: the cursor at its end");
    }

    void testShift()
    {
        std::string text;
        LineEdit edit(text, 32);
        edit.shiftTap(100);
        check(edit.shift == Shift::Once && edit.capitals(), "SHIFT once");
        edit.insert('h');
        edit.insert('l');
        check(text == "Hl" && edit.shift == Shift::Off, "... is one capital");
        edit.shiftTap(200);
        edit.shiftTap(220);
        check(edit.shift == Shift::Locked, "two taps close together lock it");
        for (char c : std::string("abc"))
            edit.insert(c);
        check(text == "HlABC" && edit.shift == Shift::Locked, "... for every letter");
        edit.shiftTap(400);
        edit.insert('d');
        check(edit.shift == Shift::Off && text == "HlABCd", "... until SHIFT again");
        edit.shiftTap(500);
        edit.shiftTap(530);
        check(edit.shift == Shift::Off, "a second tap not soon after takes it off");
        edit.shiftTap(600);
        edit.insert('1');
        edit.insert('_');
        check(text == "HlABCd1_" && edit.shift == Shift::Off, "digits and the rest have no capitals (and spend it)");
        edit.shiftTap(700);
        edit.shiftTap(701);
        edit.insert('-');
        edit.insert('.');
        check(text == "HlABCd1_-.", "... locked, too");
        check(LineEdit::keyChar(kKeyboardQwerty, 1, 0, true) == 'Q' && LineEdit::keyChar(kKeyboardQwerty, 0, 0, true) == '1' &&
                  LineEdit::keyChar(kKeyboardAzerty, 1, 0, false) == 'a',
              "a key's character, in capitals or not");
    }

    void testScroll()
    {
        std::string text(40, 'a');
        LineEdit edit(text, 64);
        check(edit.scrollFor(27) == 14, "a long line: its end on show, the cursor's cell the last");
        edit.place(0);
        check(edit.scrollFor(27) == 0, "the cursor at the start: the start on show");
        edit.place(30);
        check(edit.scrollFor(27) == 4, "... moved right past the window: it follows");
        edit.place(20);
        check(edit.scrollFor(27) == 4, "... within it: it stays");
        text = "short";
        edit.replaced();
        check(edit.scrollFor(27) == 0, "the line shortened: all of it on show");
    }

    void testLayouts()
    {
        auto characters = [](int layout)
        {
            std::string all;
            for (int row = 0; row < kKeyRows; ++row)
                all += kKeyTable[layout][row];
            std::string sorted = all;
            for (std::size_t i = 1; i < sorted.size(); ++i)
                for (std::size_t j = i; j > 0 && sorted[j - 1] > sorted[j]; --j)
                    std::swap(sorted[j - 1], sorted[j]);
            return sorted;
        };
        bool rows = true;
        for (int layout = 0; layout < kKeyboardLayouts; ++layout)
        {
            for (int row = 0; row < kKeyRows; ++row)
                rows = rows && std::string(kKeyTable[layout][row]).size() == kKeyColumns;
        }
        check(rows, "every layout: four rows of ten");
        check(std::string(kKeyTable[kKeyboardQwerty][0]) == kKeyTable[kKeyboardAzerty][0], "the digits the same in both");
        const std::string qwerty = characters(kKeyboardQwerty);
        check(qwerty == characters(kKeyboardAzerty), "both have the same keys");
        std::size_t rubs = 0;
        bool once = true;
        for (std::size_t i = 0; i < qwerty.size(); ++i)
        {
            rubs += qwerty[i] == kKeyRubOut;
            once = once && (i == 0 || qwerty[i] != qwerty[i - 1]);
        }
        check(rubs == 1 && once, "... each once, one of them rubbing out");
        bool letters = true;
        for (char c = 'a'; c <= 'z'; ++c)
            letters = letters && qwerty.find(c) != std::string::npos;
        check(letters, "... every letter among them");
    }
} // namespace

int main()
{
    testEditing();
    testShift();
    testScroll();
    testLayouts();
    if (g_failures == 0)
        std::printf("test_lineedit: ok\n");
    return g_failures == 0 ? 0 : 1;
}
