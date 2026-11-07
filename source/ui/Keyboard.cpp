#include "ui/Keyboard.hpp"

#include <nds.h>

#include "HlFont.hpp"
#include "HlUi.hpp"
#include "audio/Sound.hpp"
#include "ui/TouchCanvas.hpp"

namespace hlrt
{
    extern TouchCanvas touchCanvas;

    namespace
    {
        using namespace hldata;

        constexpr int kPromptHigh = 16;
        constexpr int kKey = 24, kLeft = 8;
        constexpr int kBottom = 180, kWideHigh = 12, kWide = 60, kWides = 4;
        // The line: "] " (or "< ", when its start is off the left), then
        // kShown cells of eight pixels, then ">" when there is more on the
        // right.
        constexpr int kMarkX = 12, kTextX = kMarkX + 16;
        constexpr std::size_t kShown = 26;
        // Held past either end of the line, a drag moves on a character this
        // often (frames).
        constexpr std::uint32_t kDragStep = 6;
    } // namespace

    Keyboard::Keyboard(std::string &text, std::size_t most, int promptTop)
        : edit_(text, most), layout_(kKeyboardQwerty),
          promptTop_(promptTop), keysTop_(promptTop + 18)
    {
    }

    void Keyboard::drawPrompt()
    {
        Canvas &canvas = touchCanvas.canvas();
        const FontView &plain = touchCanvas.plain();
        canvas.fill(0, promptTop_, 256, kPromptHigh, kUiBack);
        canvas.frame(6, promptTop_, 244, kPromptHigh, kUiEdge);
        const int y = promptTop_ + (kPromptHigh - plain.height()) / 2;
        const std::string &text = edit_.text;
        const std::size_t scroll = edit_.scrollFor(kShown);
        drawText(canvas, plain, kMarkX, y, scroll > 0 ? "<" : "]", inkFor(plain, kUiWhite));
        const std::size_t from = edit_.selectionStart(), to = edit_.selectionEnd();
        for (std::size_t i = 0; i < kShown && scroll + i < text.size(); ++i)
        {
            const std::size_t n = scroll + i;
            const int x = kTextX + static_cast<int>(i) * 8;
            const char one[2] = {text[n], '\0'};
            // (What is selected: the other way round.)
            if (n >= from && n < to)
            {
                canvas.fill(x, y - 1, 8, plain.height() + 2, kUiOrange + 2);
                drawText(canvas, plain, x, y, one, inkPlain(kUiBack));
            }
            else
            {
                drawText(canvas, plain, x, y, one, inkFor(plain, kUiWhite));
            }
        }
        if (!edit_.selecting())
            canvas.fill(kTextX + static_cast<int>(edit_.cursor - scroll) * 8, y + plain.height() + 1, 8, 2, kUiWhite + 2);
        if (scroll + kShown < text.size())
            drawText(canvas, plain, kTextX + static_cast<int>(kShown) * 8, y, ">", inkFor(plain, kUiOrange));
    }

    void Keyboard::replaced()
    {
        edit_.replaced();
        drawPrompt();
    }

    void Keyboard::key(int row, int col, bool down)
    {
        Canvas &canvas = touchCanvas.canvas();
        const FontView &font = touchCanvas.font(kFontMedium);
        const char text[2] = {LineEdit::keyChar(layout_, row, col, edit_.capitals()), '\0'};
        if (text[0] == ' ')
            return;
        canvas.button(kLeft + col * kKey, keysTop_ + row * kKey, kKey - 1, kKey - 1, down);
        drawTextCentred(canvas, font, kLeft + col * kKey + kKey / 2, keysTop_ + row * kKey + (kKey - font.height()) / 2,
                        text, inkFor(font, down ? kUiWhite : kUiOrange));
    }

    void Keyboard::keys()
    {
        for (int row = 0; row < kKeyRows; ++row)
        {
            for (int col = 0; col < kKeyColumns; ++col)
                key(row, col, row == heldRow_ && col == heldCol_);
        }
    }

    void Keyboard::wide(int n, bool down)
    {
        static const char *const kNames[kWides] = {"SHIFT", "SPACE", "ENTER", "CANCEL"};
        Canvas &canvas = touchCanvas.canvas();
        const FontView &small = touchCanvas.font(kFontSmall);
        // (SHIFT: down while locked, lit while on.)
        const bool lit = down || (n == 0 && edit_.capitals());
        canvas.button(kLeft + n * kWide, kBottom, kWide - 1, kWideHigh, down || (n == 0 && edit_.shift == Shift::Locked));
        drawTextCentred(canvas, small, kLeft + n * kWide + kWide / 2, kBottom + (kWideHigh - small.height()) / 2,
                        kNames[n], inkFor(small, lit ? kUiWhite : kUiOrange));
    }

    void Keyboard::draw()
    {
        drawPrompt();
        keys();
        for (int n = 0; n < kWides; ++n)
            wide(n, false);
    }

    std::size_t Keyboard::at(int x) const
    {
        const int cell = x < kTextX - 4 ? 0 : (x - kTextX + 4) >> 3;
        return edit_.scroll + static_cast<std::size_t>(cell);
    }

    Keyboard::Event Keyboard::poll()
    {
        ++frame_;
        const std::uint32_t pressed = keysDown();
        if (pressed & (KEY_B | KEY_START))
            return Event::Cancel;
        if (pressed & KEY_A)
            return Event::Enter;
        if (pressed & KEY_UP)
            return Event::Up;
        if (pressed & KEY_DOWN)
            return Event::Down;
        if (pressed & KEY_L)
            return Event::PageBack;
        if (pressed & KEY_R)
            return Event::PageOn;
        const std::uint32_t repeated = keysDownRepeat();
        if (repeated & (KEY_LEFT | KEY_RIGHT))
        {
            if (repeated & KEY_LEFT)
                edit_.left();
            else
                edit_.right();
            drawPrompt();
            return Event::None;
        }
        Event event = Event::None;
        const bool down = (keysHeld() & KEY_TOUCH) != 0;
        touchPosition touch{};
        if (down)
            touchRead(&touch);
        const bool onLine = touch.py >= promptTop_ && touch.py < promptTop_ + kPromptHigh;
        if (down && !touching_)
        {
            const int col = (touch.px - kLeft) / kKey, row = (touch.py - keysTop_) / kKey;
            if (onLine)
            {
                // The cursor where the line was touched; a drag selects.
                dragging_ = true;
                edit_.place(at(touch.px));
                drawPrompt();
            }
            else if (touch.py >= kBottom && touch.py < kBottom + kWideHigh && touch.px >= kLeft &&
                     touch.px < kLeft + kWides * kWide)
            {
                const int x = touch.px - kLeft;
                const int n = x < kWide ? 0 : (x < 2 * kWide ? 1 : (x < 3 * kWide ? 2 : 3));
                sound.ui(kUiSoundPress);
                heldWide_ = n;
                if (n == 0)
                {
                    edit_.shiftTap(frame_);
                    keys();
                }
                else if (n == 2)
                {
                    event = Event::Enter;
                }
                else if (n == 3)
                {
                    event = Event::Cancel;
                }
                else
                {
                    const bool capitals = edit_.capitals();
                    if (edit_.insert(' '))
                        event = Event::Changed;
                    if (capitals != edit_.capitals())
                    {
                        keys();
                        wide(0, false);
                    }
                }
                wide(n, true);
            }
            else if (touch.px >= kLeft && touch.py >= keysTop_ && col < kKeyColumns && row < kKeyRows)
            {
                const char c = kKeyTable[layout_][row][col];
                const bool capitals = edit_.capitals();
                sound.ui(kUiSoundTick);
                if (c == kKeyRubOut ? edit_.erase() : edit_.insert(c))
                    event = Event::Changed;
                heldRow_ = row;
                heldCol_ = col;
                // (A capital once: the keys are small letters again.)
                if (capitals != edit_.capitals())
                {
                    keys();
                    wide(0, false);
                }
                else
                {
                    key(row, col, true);
                }
            }
            if (event == Event::Changed)
                drawPrompt();
        }
        else if (down && dragging_)
        {
            // Past either end of the line: on a character every few frames,
            // which slides a long one along.
            const std::size_t before = edit_.cursor;
            if (touch.px < kTextX)
            {
                if (frame_ % kDragStep == 0 && edit_.cursor > 0)
                    edit_.extendTo(edit_.cursor - 1);
            }
            else if (touch.px >= kTextX + static_cast<int>(kShown) * 8)
            {
                if (frame_ % kDragStep == 0)
                    edit_.extendTo(edit_.cursor + 1);
            }
            else
            {
                edit_.extendTo(at(touch.px));
            }
            if (edit_.cursor != before)
                drawPrompt();
        }
        else if (!down)
        {
            dragging_ = false;
            if (heldRow_ >= 0)
            {
                const int row = heldRow_, col = heldCol_;
                heldRow_ = heldCol_ = -1;
                key(row, col, false);
            }
            if (heldWide_ >= 0)
            {
                wide(heldWide_, false);
                heldWide_ = -1;
            }
        }
        touching_ = down;
        return event;
    }
} // namespace hlrt
