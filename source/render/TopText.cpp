#include "global_header.hpp"

#include "render/TopText.hpp"

#include "Global.hpp"
#include "HlUi.hpp"
#include "ui/TouchCanvas.hpp"
#ifndef HLDS_CHILD
#include "HlKeyWords.hpp"
#include "Settings.hpp"
#endif

namespace hlrt
{
    TopText topText;

    namespace
    {
        using namespace hldata;

        constexpr int kBandHeight = 32;
        constexpr int kBands = 4;
        // Each slot's: one under the other, 64 rows.
        constexpr int kSlotBands = 2;
        constexpr int kSlotRows = kSlotBands * kBandHeight;
        // A slot's colours on the sprites' palette: three of its final
        // colour (a letter's faint edge, its half, its whole), then three of
        // the colour a letter is while it is new.
        constexpr int kInks = 6;
        constexpr int inkBase(int slot)
        {
            return 1 + slot * kInks;
        }

        // The game's title (ui/title.hic), if the card has it.
        std::uint32_t titleWords[(sizeof(IconsHeader) + kTitleHeight * iconRowWords(kTitleWidth) * 4) / 4];
        IconsView title;
        bool titleTried = false;

        std::uint16_t *oam()
        {
            return reinterpret_cast<std::uint16_t *>(0x07000000);
        }

        std::uint16_t scaled(std::uint16_t colour, int strength)
        {
            // (`strength` of 256.)
            const int r = (colour & 31) * strength >> 8, g = ((colour >> 5) & 31) * strength >> 8,
                      b = ((colour >> 10) & 31) * strength >> 8;
            return static_cast<std::uint16_t>(r | g << 5 | b << 10);
        }
    } // namespace

    void TopText::ensure()
    {
        // Whoever sets the video mode up takes the sprites away with it
        // (and may have had bank G): then all of this is done again.
        if (ready_ && (REG_DISPCNT & DISPLAY_SPR_ACTIVE))
            return;
        ready_ = true;
        vramSetBankG(VRAM_G_MAIN_SPRITE);
        canvas_.tiles = reinterpret_cast<std::uint32_t *>(0x06400000);
        canvas_.width = 256;
        canvas_.height = kBands * kBandHeight;
        canvas_.clear(0);
        // Every sprite out of sight, then the sixteen of the bands: each 64
        // by 32 of the sheet, which is laid out as it looks (two-dimensional
        // tile numbers: the display's default).
        std::uint16_t *sprite = oam();
        for (int n = 0; n < 128; ++n)
            sprite[n * 4] = 0x0200;
        // Added to what is behind them, whatever that is.
        REG_BLDCNT = 0x3F00;
        REG_BLDALPHA = 16 | (16 << 8);
        REG_DISPCNT = (REG_DISPCNT & ~(DISPLAY_SPR_1D | DISPLAY_SPR_1D_BMP)) | DISPLAY_SPR_ACTIVE;
        for (int s = 0; s < kSlots; ++s)
        {
            if (!slots_[s].busy)
                continue;
            // (What was written is gone with the bank: written again.)
            slots_[s].written = slots_[s].settled = 0;
            if (slots_[s].message.flags & kMessageLogo)
                drawLogo(slots_[s], s);
            if (slots_[s].badge)
                drawBadge(slots_[s], s);
            place(slots_[s], s);
        }
    }

    void TopText::place(const Slot &slot, int index)
    {
        std::uint16_t *sprite = oam();
        for (int b = 0; b < kSlotBands; ++b)
        {
            const int band = index * kSlotBands + b;
            // (The second only if the writing reaches into it.)
            const bool shown = slot.busy && slot.lineCount > 0 && slot.rows > b * kBandHeight;
            for (int k = 0; k < 4; ++k)
            {
                std::uint16_t *entry = sprite + (band * 4 + k) * 4;
                if (!shown)
                {
                    entry[0] = 0x0200;
                    continue;
                }
                // (Wide, 64 by 32; drawn added to what is behind.)
                entry[0] = static_cast<std::uint16_t>(((slot.top + b * kBandHeight) & 0xFF) | 0x0400 | 0x4000);
                entry[1] = static_cast<std::uint16_t>((k * 64) | 0xC000);
                entry[2] = static_cast<std::uint16_t>((band * 128 + k * 8) | ATTR2_PRIORITY(1));
            }
        }
    }

    void TopText::colours(int index, const Slot &slot, int strength)
    {
        strength = strength < 0 ? 0 : (strength > 256 ? 256 : strength);
        const int base = inkBase(index);
        for (int level = 0; level < 3; ++level)
        {
            SPRITE_PALETTE[base + level] = scaled(slot.message.colour, strength * (level + 1) / 3);
            SPRITE_PALETTE[base + 3 + level] = scaled(slot.message.colour2, strength * (level + 1) / 3);
        }
    }

    /// Letter `n` of the slot's writing, in its new colour or its final one.
    void TopText::letter(const Slot &slot, int index, int n, bool fresh)
    {
        for (int l = 0; l < slot.lineCount; ++l)
        {
            const Line &line = slot.lines[l];
            if (n >= line.count)
            {
                n -= line.count;
                continue;
            }
            const FontView &font = slot.font < 0 ? touchCanvas.plain() : touchCanvas.font(slot.font);
            const int base = inkBase(index) + (fresh ? 3 : 0);
            const Ink ink{{0, static_cast<std::uint8_t>(base), static_cast<std::uint8_t>(base + 1),
                           static_cast<std::uint8_t>(base + 2)}};
            drawText(canvas_, font, line.x + textWidth(font, line.text, n), index * kSlotRows + line.row, line.text + n,
                     ink, 1);
            return;
        }
    }

    void TopText::loadTitle()
    {
        if (titleTried)
            return;
        titleTried = true;
        if (global.hasData)
        {
            if (std::FILE *file = std::fopen(global.commonPath(kTitleFile).c_str(), "rb"))
            {
                const std::size_t size = std::fread(titleWords, 1, sizeof(titleWords), file);
                std::fclose(file);
                title.open(reinterpret_cast<const std::uint8_t *>(titleWords), size, 255);
            }
        }
    }

    HL_ARM void TopText::drawLogo(Slot &slot, int index)
    {
        loadTitle();
        canvas_.fill(0, index * kSlotRows, 256, kSlotRows, 0);
        const int left = (256 - title.width()) / 2;
        for (int y = 0; y < title.height() && y < kBandHeight; ++y)
        {
            for (int x = 0; x < title.width(); ++x)
            {
                if (const int level = title.level(0, x, y))
                    canvas_.pixel(left + x, index * kSlotRows + y, inkBase(index) + level - 1);
            }
        }
        slot.lineCount = 1;
        slot.rows = title.height();
        const int y = slot.message.y < 0 ? (192 - title.height()) / 2 : slot.message.y * 192 / 256;
        slot.top = y > 192 - title.height() ? 192 - title.height() : y;
    }

    void TopText::show(const MessageRecord &message, const char *text)
    {
        ensure();
        const bool logo = (message.flags & kMessageLogo) != 0;
        if (!logo && (text == nullptr || text[0] == '\0'))
            return;
        // Where: a slot nothing is in; else the one that has what is
        // written in the same place (a name follows a name); else the one
        // that has been there longest.
        int index = -1;
        for (int s = 0; s < kSlots && index < 0; ++s)
        {
            if (slots_[s].busy && slots_[s].message.x == message.x && slots_[s].message.y == message.y &&
                !((slots_[s].message.flags ^ message.flags) & kMessageLogo))
                index = s;
        }
        for (int s = 0; s < kSlots && index < 0; ++s)
        {
            if (!slots_[s].busy)
                index = s;
        }
        if (index < 0)
            index = slots_[0].order < slots_[1].order ? 0 : 1;
        if (index == noticeSlot_)
            noticeSlot_ = -1; // (what the map writes comes first)
#ifndef HLDS_CHILD
        // (A computer's keys in the DS's: HlKeyWords.hpp. Not on a console
        // sent the game: a match has no lessons.)
        if (!logo)
        {
            hldata::topKeyWords(text, settings.controls(), words_[index], sizeof(words_[index]));
            text = words_[index];
        }
#endif
        layout(slots_[index], index, message, text);
    }

    void TopText::layout(Slot &slot, int index, const MessageRecord &message, const char *text)
    {
        const bool logo = (message.flags & kMessageLogo) != 0;
        slot = Slot();
        slot.busy = true;
        slot.message = message;
        slot.order = ++orders_;
        canvas_.fill(0, index * kSlotRows, 256, kSlotRows, 0);
        if (logo)
        {
            // The game's own title: a picture, which comes and goes as the
            // game's file says its title does, and only fades.
            slot.message.effect = kMessageFade;
            drawLogo(slot, index);
            if (!title.ok())
            {
                slot.busy = false;
                return;
            }
        }
        else
        {
            // Its lines, wrapped to the screen, all in the largest of the
            // game's faces they fit the slot in -- or, if none, in the
            // console's own letters (shared/HlTopLayout.hpp).
            // (The console's face, the smallest of the game's, only on a
            // card that has it: else the 8x8 letters come next.)
            const FontView &console = touchCanvas.font(kFontConsole);
            const bool hasConsole = &console != &touchCanvas.plain();
            const FontView *faces[] = {&touchCanvas.font(kFontTitle), &touchCanvas.font(kFontMedium),
                                       &touchCanvas.font(kFontSmall), hasConsole ? &console : &touchCanvas.plain(),
                                       &touchCanvas.plain()};
            const int kFaceOf[] = {kFontTitle, kFontMedium, kFontSmall, hasConsole ? kFontConsole : -1, -1};
            const int faceCount = hasConsole ? 5 : 4;
            int forced = -1;
            for (int f = 0; f < faceCount - 1 && font_ >= 0; ++f)
                forced = kFaceOf[f] == font_ ? f : forced;
            // (A lesson in the console's letters, every one alike.)
            if (forced < 0 && hasConsole && (message.flags & kMessageLesson))
                forced = 3;
            const TopBlock block = topLayout(faces, faceCount, forced, text, message.x, message.y, 252, kSlotRows);
            slot.font = kFaceOf[block.face];
            slot.top = block.top;
            slot.rows = block.pitch * block.lineCount;
            for (int l = 0; l < block.lineCount; ++l)
            {
                Line &line = slot.lines[l];
                line.text = text + block.lines[l].start;
                line.count = block.lines[l].count;
                line.x = block.lines[l].x;
                line.row = block.lines[l].row;
                slot.letters += line.count;
            }
            slot.lineCount = block.lineCount;
            if (slot.lineCount == 0)
            {
                slot.busy = false;
                return;
            }
        }
        colours(index, slot, 0);
        place(slot, index);
        tick(0);
    }

    HL_ARM void TopText::tick(int ms)
    {
        if (!slots_[0].busy && !slots_[1].busy)
            return;
        ensure();
        for (int s = 0; s < kSlots; ++s)
        {
            Slot &slot = slots_[s];
            if (!slot.busy)
                continue;
            slot.elapsed += ms;
            const MessageRecord &m = slot.message;
            const bool logo = (m.flags & kMessageLogo) != 0;
            const bool scan = !logo && m.effect == kMessageScan;
            // How long it takes to come: written a letter at a time, or
            // faded in whole.
            const int perLetter = m.fadeInMs > 0 ? m.fadeInMs : 1;
            const int coming = scan ? slot.letters * perLetter + m.fxMs : m.fadeInMs;
            if (scan)
            {
                const int due = slot.elapsed / perLetter;
                while (slot.written < slot.letters && slot.written <= due)
                    letter(slot, s, slot.written++, true);
                // (A letter is new for a while, then as it stays.)
                const int old = (slot.elapsed - m.fxMs) / perLetter;
                while (slot.settled < slot.written && slot.settled <= old && slot.elapsed >= m.fxMs)
                    letter(slot, s, slot.settled++, false);
            }
            else if (!logo && slot.written < slot.letters)
            {
                while (slot.written < slot.letters)
                    letter(slot, s, slot.written++, false);
                slot.settled = slot.written;
            }
            int strength = 256;
            const int going = slot.elapsed - coming - m.holdMs;
            if (!scan && slot.elapsed < coming)
                strength = slot.elapsed * 256 / (coming > 0 ? coming : 1);
            else if (going > 0)
                strength = 256 - going * 256 / (m.fadeOutMs > 0 ? m.fadeOutMs : 1);
            if (going >= m.fadeOutMs)
            {
                slot.busy = false;
                canvas_.fill(0, s * kSlotRows, 256, kSlotRows, 0);
                place(slot, s);
                continue;
            }
            colours(s, slot, strength);
        }
    }

    void TopText::notice(const char *text)
    {
        ensure();
        int index = noticeSlot_ >= 0 && slots_[noticeSlot_].busy ? noticeSlot_ : -1;
        const bool up = index >= 0;
        for (int s = 0; s < kSlots && index < 0; ++s)
        {
            if (!slots_[s].busy)
                index = s;
        }
        if (index < 0)
            return;
        // In the top left corner (the lamp's sign is in the right one), small
        // and faint (added to the picture, as all of this is: a dim amber is
        // a faint one).
        MessageRecord m{};
        m.x = 4;
        m.y = 6;
        m.effect = kMessageFade;
        m.colour = m.colour2 = static_cast<std::uint16_t>(18 | 11 << 5 | 2 << 10);
        m.fadeInMs = 200;
        m.holdMs = 60000; // (until endNotice)
        m.fadeOutMs = 500;
        canvas_.fill(0, index * kSlotRows, 256, kSlotRows, 0);
        font_ = kFontSmall;
        layout(slots_[index], index, m, text);
        font_ = -1;
        if (!slots_[index].busy)
            return;
        noticeSlot_ = index;
        if (up)
        {
            // (Already up: the word changes, and it does not fade in again.)
            slots_[index].elapsed = m.fadeInMs;
            tick(0);
        }
    }

    void TopText::endNotice(int holdMs)
    {
        if (noticeSlot_ < 0 || !slots_[noticeSlot_].busy)
            return;
        Slot &slot = slots_[noticeSlot_];
        const int shown = slot.elapsed - slot.message.fadeInMs;
        slot.message.holdMs = static_cast<std::uint16_t>((shown > 0 ? shown : 0) + holdMs);
        noticeSlot_ = -1;
    }

    bool TopText::announce(const char *text, int holdMs)
    {
        ensure();
        int index = -1;
        for (int s = 0; s < kSlots && index < 0; ++s)
        {
            if (!slots_[s].busy)
                index = s;
        }
        if (index < 0)
            return false;
        // Under the notice's corner, in the HUD's amber, the trophy's width
        // in from the edge.
        MessageRecord m{};
        m.x = 24;
        m.y = 18;
        m.effect = kMessageFade;
        m.colour = m.colour2 = static_cast<std::uint16_t>(31 | 20 << 5 | 2 << 10);
        m.fadeInMs = 250;
        m.holdMs = static_cast<std::uint16_t>(holdMs);
        m.fadeOutMs = 600;
        font_ = kFontMedium;
        layout(slots_[index], index, m, text);
        font_ = -1;
        if (!slots_[index].busy)
            return false;
        slots_[index].badge = true;
        drawBadge(slots_[index], index);
        return true;
    }

    /// A cup on a stem, 16 by 16, in the band of the slot's first line: in
    /// its inks, so that it fades as the writing does.
    void TopText::drawBadge(const Slot &slot, int index)
    {
        static constexpr const char *kTrophy[16] = {
            "..############..", ".#.#o########.#.", "#..#o########..#", "#..#o########..#",
            ".#.#o########.#.", "..##o#########..", "...#o########...", "....#o######....",
            ".....######.....", "......####......", ".......##.......", ".......##.......",
            "......####......", ".....######.....", "....########....", "................",
        };
        const int top = index * kSlotRows + slot.lines[0].row;
        const int left = slot.lines[0].x - 20;
        for (int y = 0; y < 16; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                const char c = kTrophy[y][x];
                if (c != '.')
                    canvas_.pixel(left + x, top + y, inkBase(index) + (c == 'o' ? 1 : 2));
            }
        }
    }

    void TopText::clear()
    {
        noticeSlot_ = -1;
        for (int s = 0; s < kSlots; ++s)
        {
            if (!slots_[s].busy)
                continue;
            slots_[s].busy = false;
            if (ready_)
            {
                canvas_.fill(0, s * kSlotRows, 256, kSlotRows, 0);
                place(slots_[s], s);
            }
        }
    }
} // namespace hlrt
