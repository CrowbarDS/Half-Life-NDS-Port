// The screens' own things (shared/HlUi.hpp, HlFont.hpp, hlds/UiCook.hpp):
// a font read as the game keeps one and cooked to what the console draws
// with; text measured, fitted to a line and drawn on a canvas of tiles; the
// canvas's rectangles. With HLDS_VALVE_DIR set, the game's own fonts and
// pictures.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "HlFont.hpp"
#include "HlLamp.hpp"
#include "HlLampPatch.hpp"
#include "HlLampGrid.hpp"
#include "HlTopLayout.hpp"
#include "HlKeyWords.hpp"
#include "HlSettings.hpp"
#include "HlTouchLayout.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/UiCook.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/TitlesFile.hpp"
#include "hlds/GxStream.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    /// A font lump of two letters: 'A' three wide (a light letter with a
    /// dark edge), 'B' two wide, each four rows.
    std::vector<std::uint8_t> madeFont()
    {
        const int height = 8;
        std::vector<std::uint8_t> lump(16 + 1024 + 256 * height + 2 + 768 + 64, 0);
        auto put32 = [&](std::size_t at, std::uint32_t v)
        {
            for (int k = 0; k < 4; ++k)
                lump[at + k] = static_cast<std::uint8_t>(v >> (8 * k));
        };
        put32(0, 16);
        put32(4, height);
        put32(8, 2);
        put32(12, 4);
        auto glyph = [&](int c, int x, int y, int w)
        {
            const int start = y * 256 + x;
            lump[16 + 4 * c] = static_cast<std::uint8_t>(start);
            lump[17 + 4 * c] = static_cast<std::uint8_t>(start >> 8);
            lump[18 + 4 * c] = static_cast<std::uint8_t>(w);
        };
        glyph('A', 0, 0, 3);
        glyph('B', 16, 0, 2);
        glyph(' ', 32, 0, 2);
        std::uint8_t *pixels = lump.data() + 16 + 1024;
        std::memset(pixels, 255, 256 * height);
        // A: edge, letter, edge on its first row; letter alone on its second.
        pixels[0] = 32;
        pixels[1] = 192;
        pixels[2] = 32;
        pixels[256 + 1] = 192;
        // B: two of the letter.
        pixels[16] = 192;
        pixels[17] = 192;
        std::uint8_t *palette = lump.data() + 16 + 1024 + 256 * height + 2;
        for (int i = 0; i < 256; ++i)
            palette[i * 3] = palette[i * 3 + 1] = palette[i * 3 + 2] = static_cast<std::uint8_t>(i);
        return lump;
    }

    void testFont()
    {
        QFont font;
        std::string error;
        check(parseQFont(madeFont(), font, error) && font.rowHeight == 4 && font.glyphs['A'].width == 3,
              "a font lump reads: its row height, its glyphs' places and widths");
        std::vector<std::uint8_t> shortLump(100, 0);
        check(!parseQFont(shortLump, font, error), "one cut short is refused");
        parseQFont(madeFont(), font, error);
        const std::vector<std::uint8_t> cooked = cookFont(font, true);
        std::vector<std::uint32_t> aligned((cooked.size() + 3) / 4);
        std::memcpy(aligned.data(), cooked.data(), cooked.size());
        FontView view;
        check(view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()), cooked.size()) && view.height() == 4 &&
                  view.width('A') == 3 && view.width('B') == 2 && view.header->outlined == 1,
              "cooked, it opens as the console opens it");
        check(view.glyph('A') != nullptr && view.glyph('A')[0] == (1u | 3u << 2 | 1u << 4) && view.glyph('A')[1] == (3u << 2),
              "a glyph's rows are its pixels: the edge 1, the letter 3, nothing 0");
        check(view.width(0xBC) == view.width(' ') && view.glyph(0xBC) == view.glyph(' '),
              "a character it has not got, nor a letter like it, moves the pen as a space does");
        check(view.width(0xC0) == 3 && view.glyph(0xC0) == view.glyph('A') && view.glyph(0xC2) == view.glyph('A') &&
                  view.width(0xA0) == view.width(' '),
              "an accented letter it has not got is drawn as its letter (\xC0 and \xC2 as A)");
        check(baseLetter(0xE9) == 'e' && baseLetter(0xE7) == 'c' && baseLetter(0xF1) == 'n' && baseLetter(0xC9) == 'E' &&
                  baseLetter(0x9C) == 'o' && baseLetter(0xAB) == '"' && baseLetter(0xBF) == '?' && baseLetter('q') == 'q',
              "the letters under the accents: e, c, n, E, o, and the marks");
        check(!view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()), cooked.size() - 8),
              "a font file cut short is refused");
        view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()), cooked.size());
        {
            // A font with accented letters of its own (a translated game's):
            // they are kept, and drawn as they are.
            std::vector<std::uint8_t> lump = madeFont();
            lump[16 + 4 * 0xC9] = 16; // (É: drawn as B is)
            lump[18 + 4 * 0xC9] = 2;
            QFont accented;
            parseQFont(lump, accented, error);
            const std::vector<std::uint8_t> withAccents = cookFont(accented, true);
            std::vector<std::uint32_t> words((withAccents.size() + 3) / 4);
            std::memcpy(words.data(), withAccents.data(), withAccents.size());
            FontView wide;
            check(wide.open(reinterpret_cast<const std::uint8_t *>(words.data()), withAccents.size()) &&
                      wide.header->count == 224 && wide.width(0xC9) == 2 && wide.glyph(0xC9) != wide.glyph('E') &&
                      wide.glyph(0xC9)[0] == wide.glyph('B')[0] && wide.width(0xC0) == 3 && view.header->count == 96,
                  "a font with letters of 128..255 keeps them; one without stays at 32..127");
        }

        // Measuring, and fitting to a line.
        check(textWidth(view, "AB") == 5 && textWidth(view, "AB A") == 10 && textWidth(view, "ABAB", 2) == 5,
              "a line is as wide as its letters");
        check(textFit(view, "AB AB AB", 12) == 5 && textFit(view, "AB AB AB", 100) == 8,
              "what fits a line ends at the last space that does");
        check(textFit(view, "ABABABAB", 7) == 2 && textFit(view, "A", 1) == 1,
              "a word too long for the line is cut where it must be");

        // Drawing.
        std::vector<std::uint32_t> tiles(4 * 2 * 8, 0);
        Canvas canvas{tiles.data(), 32, 16};
        const Ink ink = inkFor(view, kUiWhite);
        const int end = drawText(canvas, view, 6, 7, "AB", ink);
        check(end == 11, "the pen ends where the line does");
        check(canvas.at(6, 7) == kUiShadow && canvas.at(7, 7) == kUiWhite + 2 && canvas.at(8, 7) == kUiShadow &&
                  canvas.at(7, 8) == kUiWhite + 2 && canvas.at(6, 8) == 0,
              "letters land on the canvas where they are put, across a tile's edge: the edge dark, the letter bright");
        check(canvas.at(9, 7) == kUiWhite + 2 && canvas.at(10, 7) == kUiWhite + 2 && canvas.at(11, 7) == 0,
              "... one after another");
        drawText(canvas, view, 30, 14, "AB", ink);
        drawText(canvas, view, -2, -1, "AB", ink);
        check(canvas.at(31, 14) == kUiWhite + 2, "what runs off the canvas is cut, not wrapped");
    }

    void testCanvas()
    {
        std::vector<std::uint32_t> tiles(4 * 3 * 8, 0);
        Canvas canvas{tiles.data(), 32, 24};
        canvas.fill(3, 2, 14, 9, 7);
        int inside = 0, outside = 0;
        for (int y = 0; y < 24; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                const bool in = x >= 3 && x < 17 && y >= 2 && y < 11;
                inside += in && canvas.at(x, y) == 7;
                outside += !in && canvas.at(x, y) != 0;
            }
        }
        check(inside == 14 * 9 && outside == 0, "a rectangle fills exactly its pixels, whole tiles and parts of tiles");
        canvas.fill(-5, -5, 100, 100, 2);
        check(canvas.at(0, 0) == 2 && canvas.at(31, 23) == 2, "one larger than the canvas fills the canvas");
        canvas.clear();
        canvas.frame(4, 4, 10, 6, 3);
        check(canvas.at(4, 4) == 3 && canvas.at(13, 9) == 3 && canvas.at(5, 5) == 0 && canvas.at(14, 4) == 0,
              "a frame is its edge alone");
        canvas.button(8, 8, 16, 12, true);
        check(canvas.at(8, 8) == kUiBack && canvas.at(9, 8) == kUiEdge && canvas.at(12, 12) == kUiPanelDown,
              "a button: edged, filled, its corners off");
        canvas.clear();
        canvas.line(2, 3, 20, 3, 6);
        canvas.line(5, 20, 5, 6, 7);
        canvas.line(10, 10, 16, 16, 8);
        int level = 0, upright = 0, slant = 0, others = 0;
        for (int y = 0; y < 24; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                const int c = canvas.at(x, y);
                level += c == 6 && y == 3 && x >= 2 && x <= 20;
                upright += c == 7 && x == 5 && y >= 6 && y <= 20;
                slant += c == 8 && x == y && x >= 10 && x <= 16;
                others += c != 0;
            }
        }
        check(level == 19 && upright == 15 && slant == 7 && others == 19 + 15 + 7,
              "a line is every pixel from its one end to its other, whichever way it is given");
        canvas.clear(1);
        canvas.plate(4, 4, 20, 14, 3, 5, 9, 2);
        check(canvas.at(4, 4) == 2 && canvas.at(5, 5) == 2 && canvas.at(6, 5) == 9 && canvas.at(7, 4) == 9 &&
                  canvas.at(4, 7) == 9 && canvas.at(23, 17) == 2 && canvas.at(20, 17) == 9 && canvas.at(12, 10) == 5 &&
                  canvas.at(3, 4) == 1 && canvas.at(24, 10) == 1 && canvas.at(23, 10) == 9,
              "a plate: its corners cut at a slant, edged all round, nothing drawn outside it");
        // The tile layout is the hardware's: the leftmost pixel in the
        // lowest four bits of the row's word.
        canvas.clear();
        canvas.pixel(9, 10, 5);
        check(tiles[(1 * 4 + 1) * 8 + 2] == (5u << 4), "a pixel is where the hardware will look for it");
    }

    void testPlain()
    {
        std::uint8_t bits[96 * 8] = {};
        bits[('A' - 32) * 8] = 0x80;
        std::vector<std::uint32_t> words(kPlainFontWords);
        plainFont(bits, true, words.data());
        FontView view;
        check(view.open(reinterpret_cast<const std::uint8_t *>(words.data()), words.size() * 4) && view.height() == 8 &&
                  view.width('z') == 8 && view.glyph('A')[0] == 3u,
              "the console's own 8x8 letters make a font of the same form");
    }

    /// A face of `height` rows whose every letter is `width` across.
    std::vector<std::uint32_t> evenFace(int height, int width)
    {
        std::vector<std::uint32_t> words((12 + 96 + 96 * height * 4) / 4, 0);
        FontHeader header{};
        std::memcpy(header.magic, kFontMagic, 4);
        header.version = kFontVersion;
        header.height = static_cast<std::uint8_t>(height);
        header.first = 32;
        header.count = 96;
        auto *bytes = reinterpret_cast<std::uint8_t *>(words.data());
        std::memcpy(bytes, &header, sizeof(header));
        std::memset(bytes + sizeof(header), width, 96);
        return words;
    }

    /// What the map writes over the picture: wrapped to the screen, one face
    /// for all of it, the console's letters only for what fits no other.
    void testTopLayout()
    {
        const std::vector<std::uint32_t> words[4] = {evenFace(20, 12), evenFace(16, 9), evenFace(14, 6), evenFace(8, 8)};
        FontView views[4];
        for (int f = 0; f < 4; ++f)
            views[f].open(reinterpret_cast<const std::uint8_t *>(words[f].data()), words[f].size() * 4);
        const FontView *faces[4] = {&views[0], &views[1], &views[2], &views[3]};

        TopBlock title = topLayout(faces, 4, -1, "HAZARD COURSE", -1, 102);
        check(title.face == 0 && title.lineCount == 1 && title.lines[0].x == (256 - 13 * 12) / 2 &&
                  title.top == 102 * 192 / 256,
              "a short title keeps the largest face, in the middle across, as far down as it says");

        const char *move = "MOVE FORWARD BY PRESSING FORWARD KEY\nMOVE BACKWARD BY PRESSING BACKWARD KEY\n"
                           "MOVE LEFT BY PRESSING MOVELEFT\nMOVE RIGHT BY PRESSING MOVERIGHT";
        TopBlock block = topLayout(faces, 4, -1, move, -1, 166);
        bool narrow = true, centred = true;
        int letters = 0;
        for (int l = 0; l < block.lineCount; ++l)
        {
            const int wide = textWidth(*faces[block.face], move + block.lines[l].start, block.lines[l].count);
            narrow = narrow && wide <= 252;
            centred = centred && block.lines[l].x == (256 - wide) / 2;
            letters += block.lines[l].count;
        }
        std::printf("test_ui: the movement text: face %d, %d lines at %d rows apart, from row %d\n", block.face,
                    block.lineCount, block.pitch, block.top);
        check(block.face == 2 && block.lineCount == 4 && narrow && centred,
              "four lines that fit the smallest face keep it, each on its own line, each in the middle");
        check(letters == 36 + 38 + 30 + 32 && block.top + block.lineCount * block.pitch <= 192,
              "... none of its letters left out, and all of it on the screen");

        TopBlock button = topLayout(faces, 4, -1, "PRESS YOUR USE KEY TO PUSH A BUTTON", -1, 77);
        check(button.face == 2 && button.lineCount == 1,
              "a line that fits whole in a smaller face goes in it, rather than wrapped in a larger");

        const char *longer = "PRESS FORWARD KEY TO RUN FORWARD AND THEN PRESS THE JUMP KEY TO JUMP OVER THE GAP, "
                             "AND KEEP RUNNING UNTIL YOU REACH THE FAR SIDE OF IT\n"
                             "PRESS DUCK KEY TO DUCK UNDER IT\nPRESS JUMP KEY TO JUMP";
        block = topLayout(faces, 4, -1, longer, -1, 166);
        bool fits = true;
        int kept = 0, all = 0;
        for (int l = 0; l < block.lineCount; ++l)
        {
            fits = fits && textWidth(*faces[3], longer + block.lines[l].start, block.lines[l].count) <= 252 &&
                   longer[block.lines[l].start] != ' ';
            for (int k = 0; k < block.lines[l].count; ++k)
                kept += longer[block.lines[l].start + k] != ' ' ? 1 : 0;
        }
        for (const char *c = longer; *c != '\0'; ++c)
            all += *c != '\n' && *c != ' ' ? 1 : 0;
        check(block.face == 3 && block.pitch == 9 && block.lineCount * 9 <= 64 && fits && kept == all,
              "what fits none of the game's faces is wrapped in the console's letters, each line starting on a word");

        block = topLayout(faces, 4, 2, "SAVING", 4, 6);
        check(block.face == 2 && block.lines[0].x == 4 && block.top == 6 * 192 / 256,
              "a face asked for is kept, where it is asked for");
        block = topLayout(faces, 4, 2, move, 4, 6);
        check(block.face == 2 && block.lineCount == 4, "... and what it cannot hold is left out, not drawn over");
        check(topLayout(faces, 4, -1, "\n\n", -1, -1).lineCount == 0, "empty lines are no lines");

        // A computer's keys, in the DS's: the layout's buttons, else the
        // touch screen's.
        ControlSettings set;
        char said[160];
        set.scheme = kControlsClassic;
        topKeyWords("PRESS YOUR USE KEY TO PUSH A BUTTON", set, said, sizeof(said));
        const bool classicUse = std::strcmp(said, "PRESS TOUCH USE TO PUSH A BUTTON") == 0;
        topKeyWords("MOVE BACKWARD BY PRESSING BACKWARD KEY\nAIM WITH THE MOUSE", set, said, sizeof(said));
        const bool classicPad = std::strcmp(said, "MOVE BACKWARD BY PRESSING DOWN\nAIM WITH THE STYLUS") == 0;
        set.scheme = kControlsModern;
        topKeyWords("PRESS RELOAD KEY TO RELOAD, THEN THE DUCK KEY", set, said, sizeof(said));
        const bool modern = std::strcmp(said, "PRESS Y TO RELOAD, THEN B") == 0;
        set.scheme = kControlsButtons;
        topKeyWords("PRESS FORWARD + JUMP TOGETHER,\nTHEN PRESS JUMP", set, said, sizeof(said));
        const bool buttons = std::strcmp(said, "PRESS X + R+UP TOGETHER,\nTHEN PRESS R+UP") == 0;
        topKeyWords("NOTHING OF KEYS HERE", set, said, 8);
        check(classicUse && classicPad && modern && buttons && std::strcmp(said, "NOTHING") == 0,
              "a lesson names the DS's buttons of the player's layout, or the touch screen's; the rest is kept");
    }

    /// A settings file as v0.2.1 wrote it (version 20), with every setting
    /// it had moved off its default: written by that release's own
    /// HlSettings.hpp, and kept here byte for byte.
    const std::uint8_t kSettingsOfV021[] = {
        0x48, 0x4c, 0x53, 0x54, 0x14, 0x00, 0xb0, 0x00, 0x4b, 0xeb, 0xb3, 0x7a, 0x00, 0x00, 0x03, 0x07,
        0x09, 0x02, 0x01, 0x01, 0x01, 0x00, 0x03, 0x08, 0x02, 0x02, 0x00, 0x06, 0x01, 0x02, 0x01, 0x01,
        0x0e, 0x06, 0x01, 0x01, 0x07, 0x02, 0x01, 0x00, 0x02, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00,
        0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x04, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x01, 0x01, 0x46, 0x72, 0x65, 0x65, 0x6d, 0x61, 0x6e, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x02, 0x64, 0xc8, 0x03, 0x04, 0x01, 0x63, 0x72, 0x6f, 0x73, 0x73, 0x66, 0x69, 0x72, 0x65,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x0f, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06, 0x00,
        0x14, 0x01, 0x02, 0x02, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x01, 0x01, 0x02, 0x00, 0x00, 0x01,
    };

    /// A player's settings from v0.2.1 come through the upgrade: read as
    /// they were, written again in this version, and read back the same,
    /// with what v0.2.1 did not have at its default.
    void testSettingsFromV021()
    {
        SettingsData read;
        check(readSettings(kSettingsOfV021, sizeof(kSettingsOfV021), read), "v0.2.1's settings file is read");
        auto same = [](const SettingsData &d)
        {
            bool keys = true;
            // (The actions v0.2.1 had; those after are on no button.)
            for (int i = 0; i < kActionCount; ++i)
                keys = keys && d.keys[i] == (i < kActionsBeforeLook ? static_cast<std::uint16_t>(1u << (i % 3)) : 0);
            return d.volume[0] == 3 && d.volume[1] == 7 && d.volume[2] == 9 && d.volume[3] == 2 && d.debugReadout == 1 &&
                   d.leftHanded == 1 && d.viewBob == 1 && d.graphics.fog == 0 && d.graphics.fogStrength == 3 &&
                   d.graphics.fogDistance == 8 && d.graphics.fogColour == 2 && d.graphics.modelDetail == 2 &&
                   d.graphics.worldDetail == 0 && d.graphics.modelLimit == 6 && d.graphics.drawDistance == 1 &&
                   d.controlScheme == 2 && d.padTurns == 1 && d.padLooks == 1 && d.aimSpeed == 14 && d.pitchSpeed == 6 &&
                   d.invertPitch == 1 && d.duckToggle == 1 && d.walkToggle == 1 && d.jumpWalks == 1 && d.tapJump == 0 &&
                   d.tapUse == 1 && d.firmness == 7 && d.firmPress == 2 && keys && d.autoaim == 1 && d.autosave == 0 &&
                   d.achievementPopups == 0 && d.roomEffects == kRoomOff && d.outputMode == kOutputHeadphones &&
                   d.soundQuality == kQualityHigh && std::strcmp(d.name, "Freeman") == 0 && d.playerModel == 2 &&
                   d.topColour == 100 && d.bottomColour == 200 && d.spray == 3 && d.sprayColour == 4 && d.hostMode == 1 &&
                   std::strcmp(d.hostMap, "crossfire") == 0 && d.fragLimit == 30 && d.minuteLimit == 15 &&
                   d.weaponsStay == 1 && d.fallDamage == 1 && d.friendlyFire == 1 && d.flashlight == 1 &&
                   d.hostAutoaim == 0 && d.maxPlayers == 6 && d.allowGuests == 0 && d.chatSeconds == 20 &&
                   d.sessionTotals == 1 && d.nextMode == 2 && d.rotationCount == 2 && d.rotation[0] == 0x11223344u &&
                   d.rotation[1] == 0x55667788u && d.voteSeconds == 30;
        };
        check(same(read), "... with every setting the player had, the volumes among them, as they were");
        check(read.cheats == 0 && read.keyboardLayout == kKeyboardQwerty && read.dialogueFocus == kFocusLight &&
                  read.hideMapName == 1,
              "... and what v0.2.1 had not, at its default (no cheats, QWERTY, dialogue focus light, map name hidden)");
        const std::vector<std::uint8_t> upgraded = writeSettings(read);
        SettingsHeader header;
        std::memcpy(&header, upgraded.data(), sizeof(header));
        SettingsData again;
        check(header.version == kSettingsVersion && upgraded.size() <= kSettingsMostBytes &&
                  readSettings(upgraded.data(), upgraded.size(), again) && same(again) &&
                  again.dialogueFocus == kFocusLight,
              "written again as this version, and read back the same");
    }

    /// The settings the menu changes: there and back, and nothing out of
    /// range believed.
    void testSettings()
    {
        SettingsData data;
        check(data.volume[kVolumeMusic] == 6 && data.volume[kVolumeEffects] == 10 && data.volume[kVolumeVoices] == 10 &&
                  data.volume[kVolumeAmbience] == 7 && data.debugReadout == 0,
              "settings start with the music and the ambience under the rest, and no readout");
        data.volume[kVolumeVoices] = 3;
        data.volume[kVolumeAmbience] = 0;
        data.debugReadout = 1;
        data.renderMode = 3;
        std::vector<std::uint8_t> bytes = writeSettings(data);
        SettingsData back;
        check(bytes.size() <= kSettingsMostBytes && readSettings(bytes.data(), bytes.size(), back) && back.volume[kVolumeVoices] == 3 &&
                  back.volume[kVolumeAmbience] == 0 && back.volume[kVolumeMusic] == 6 && back.debugReadout == 1 &&
                  back.renderMode == 3,
              "settings read back as they were written, the volumes and the readout among them");
        data.volume[kVolumeMusic] = 11;
        bytes = writeSettings(data);
        SettingsData untouched;
        check(!readSettings(bytes.data(), bytes.size(), untouched) && untouched.volume[kVolumeMusic] == 6,
              "a volume out of range is not believed");
        bytes = writeSettings(SettingsData());
        bytes[4] = 7; // (the version before this one)
        check(!readSettings(bytes.data(), bytes.size(), untouched) && untouched.leftHanded == 0,
              "nor a file of the version before: the defaults stand");
        SettingsData left;
        left.leftHanded = 1;
        bytes = writeSettings(left);
        check(readSettings(bytes.data(), bytes.size(), back) && back.leftHanded == 1 && SettingsData().leftHanded == 0,
              "which hand holds the stylus is kept: the right, unless it is said");
        SettingsData bobbing;
        bobbing.viewBob = 1;
        bytes = writeSettings(bobbing);
        check(SettingsData().viewBob == 0 && readSettings(bytes.data(), bytes.size(), back) && back.viewBob == 1,
              "the view does not bob unless it is asked to, and the asking is kept");
        // The sound's: the game's own rooms and the speakers unless asked
        // otherwise; what is asked kept; nothing out of range.
        SettingsData sounding;
        sounding.roomEffects = kRoomOff;
        sounding.outputMode = kOutputHeadphones;
        sounding.soundQuality = kQualityHigh;
        bytes = writeSettings(sounding);
        check(SettingsData().roomEffects == kRoomClassic && SettingsData().outputMode == kOutputSpeakers &&
                  readSettings(bytes.data(), bytes.size(), back) && back.roomEffects == kRoomOff &&
                  back.outputMode == kOutputHeadphones && back.soundQuality == kQualityHigh,
              "the rooms and the speakers unless asked otherwise; what is asked, kept");
        sounding.outputMode = kOutputChoices;
        bytes = writeSettings(sounding);
        check(!readSettings(bytes.data(), bytes.size(), back), "an output there is not: not read");
        // A file of version 12: its DSP switch is the DSP's rooms; off, the
        // game's own.
        for (int dsp = 0; dsp < 2; ++dsp)
        {
            SettingsData twelve;
            twelve.dspAudio = static_cast<std::uint8_t>(dsp);
            twelve.volume[kVolumeMusic] = 5;
            constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
            yas::mem_ostream os;
            yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
            twelve.serializeTwelve(oa);
            const auto buf = os.get_intrusive_buffer();
            SettingsHeader header{};
            std::memcpy(header.magic, kSettingsMagic, sizeof(header.magic));
            header.version = kSettingsVersionTwelve;
            header.payloadBytes = static_cast<std::uint16_t>(buf.size);
            header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data), static_cast<std::uint32_t>(buf.size));
            std::vector<std::uint8_t> file(sizeof(header) + buf.size);
            std::memcpy(file.data(), &header, sizeof(header));
            std::memcpy(file.data() + sizeof(header), buf.data, buf.size);
            SettingsData read;
            check(readSettings(file.data(), file.size(), read) && read.volume[kVolumeMusic] == 5 &&
                      read.roomEffects == (dsp ? kRoomEnhanced : kRoomClassic) && read.outputMode == kOutputSpeakers,
                  dsp ? "a file of version 12 with the DSP on: the DSP's rooms"
                      : "a file of version 12 with the DSP off: the game's own rooms");
        }
        // A file of version 15 has no vote time: as it is at first.
        {
            SettingsData fifteen;
            fifteen.chatSeconds = 25;
            constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
            yas::mem_ostream os;
            yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
            fifteen.serializeFifteen(oa);
            const auto buf = os.get_intrusive_buffer();
            SettingsHeader header{};
            std::memcpy(header.magic, kSettingsMagic, sizeof(header.magic));
            header.version = kSettingsVersionFifteen;
            header.payloadBytes = static_cast<std::uint16_t>(buf.size);
            header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data), static_cast<std::uint32_t>(buf.size));
            std::vector<std::uint8_t> file(sizeof(header) + buf.size);
            std::memcpy(file.data(), &header, sizeof(header));
            std::memcpy(file.data() + sizeof(header), buf.data, buf.size);
            SettingsData read;
            check(readSettings(file.data(), file.size(), read) && read.chatSeconds == 25 && read.voteSeconds == kVoteDefault,
                  "a file of version 15 is read, with the vote's time as it is at first");
        }
        // The dialogue focus: light unless asked; kept; nothing out of range;
        // a file of version 21, from before it, read with it light.
        {
            SettingsData focused;
            focused.dialogueFocus = kFocusStrong;
            bytes = writeSettings(focused);
            check(SettingsData().dialogueFocus == kFocusLight && readSettings(bytes.data(), bytes.size(), back) &&
                      back.dialogueFocus == kFocusStrong,
                  "the dialogue focus is light unless asked otherwise, and what is asked is kept");
            focused.dialogueFocus = kFocusChoices;
            bytes = writeSettings(focused);
            check(!readSettings(bytes.data(), bytes.size(), back), "a dialogue focus there is not: not read");
            SettingsData older;
            older.volume[kVolumeMusic] = 8; // (the defaults before)
            older.volume[kVolumeAmbience] = 8;
            older.dialogueFocus = kFocusOff;
            constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
            yas::mem_ostream os;
            yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
            older.serializeTwentyOne(oa);
            const auto buf = os.get_intrusive_buffer();
            SettingsHeader header{};
            std::memcpy(header.magic, kSettingsMagic, sizeof(header.magic));
            header.version = kSettingsVersionTwentyOne;
            header.payloadBytes = static_cast<std::uint16_t>(buf.size);
            header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data), static_cast<std::uint32_t>(buf.size));
            std::vector<std::uint8_t> file(sizeof(header) + buf.size);
            std::memcpy(file.data(), &header, sizeof(header));
            std::memcpy(file.data() + sizeof(header), buf.data, buf.size);
            SettingsData read;
            check(readSettings(file.data(), file.size(), read) && read.volume[kVolumeMusic] == 8 &&
                      read.volume[kVolumeAmbience] == 8 && read.dialogueFocus == kFocusLight,
                  "a file of version 21 keeps its volumes as they were, and its dialogue focus is light");
        }
        // The map's name on the loading screen: hidden unless asked; kept;
        // nothing out of range; a file of version 22, from before it, read
        // with it hidden.
        {
            SettingsData shown;
            shown.hideMapName = 0;
            bytes = writeSettings(shown);
            check(SettingsData().hideMapName == 1 && readSettings(bytes.data(), bytes.size(), back) &&
                      back.hideMapName == 0,
                  "the map's name is hidden unless asked otherwise, and what is asked is kept");
            shown.hideMapName = 2;
            bytes = writeSettings(shown);
            check(!readSettings(bytes.data(), bytes.size(), back), "a map's name neither hidden nor shown: not read");
            SettingsData older;
            older.dialogueFocus = kFocusStrong;
            older.hideMapName = 0;
            constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
            yas::mem_ostream os;
            yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
            older.serializeTwentyTwo(oa);
            const auto buf = os.get_intrusive_buffer();
            SettingsHeader header{};
            std::memcpy(header.magic, kSettingsMagic, sizeof(header.magic));
            header.version = kSettingsVersionTwentyTwo;
            header.payloadBytes = static_cast<std::uint16_t>(buf.size);
            header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data), static_cast<std::uint32_t>(buf.size));
            std::vector<std::uint8_t> file(sizeof(header) + buf.size);
            std::memcpy(file.data(), &header, sizeof(header));
            std::memcpy(file.data() + sizeof(header), buf.data, buf.size);
            SettingsData read;
            check(readSettings(file.data(), file.size(), read) && read.dialogueFocus == kFocusStrong &&
                      read.hideMapName == 1,
                  "a file of version 22 keeps its dialogue focus, and the map's name is hidden");
        }
        // A file of version 13 keeps all it had; the profile and the match
        // hosted are as they are at first.
        {
            SettingsData thirteen;
            thirteen.volume[kVolumeMusic] = 4;
            thirteen.roomEffects = kRoomOff;
            thirteen.leftHanded = 1;
            std::memcpy(thirteen.name, "lost", 5);
            constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
            yas::mem_ostream os;
            yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
            thirteen.serializeThirteen(oa);
            const auto buf = os.get_intrusive_buffer();
            SettingsHeader header{};
            std::memcpy(header.magic, kSettingsMagic, sizeof(header.magic));
            header.version = kSettingsVersionThirteen;
            header.payloadBytes = static_cast<std::uint16_t>(buf.size);
            header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data), static_cast<std::uint32_t>(buf.size));
            std::vector<std::uint8_t> file(sizeof(header) + buf.size);
            std::memcpy(file.data(), &header, sizeof(header));
            std::memcpy(file.data() + sizeof(header), buf.data, buf.size);
            SettingsData read;
            check(readSettings(file.data(), file.size(), read) && read.volume[kVolumeMusic] == 4 &&
                      read.roomEffects == kRoomOff && read.leftHanded == 1 && read.name[0] == 0 &&
                      read.topColour == 30 && read.bottomColour == 6 && read.maxPlayers == 4 &&
                      read.fragLimit == 20 && read.hostMode == kModeDeathmatch,
                  "a file of version 13 keeps its settings, and the profile is as at first");
        }
        // The profile and the match: there and back; a name of any bytes is
        // kept, only ended; a number out of range is not believed.
        {
            SettingsData mine;
            for (std::size_t i = 0; i < kProfileNameBytes; ++i)
                mine.name[i] = static_cast<char>(0x80 + i * 9);
            mine.topColour = 255;
            mine.bottomColour = 0;
            mine.spray = 17;
            mine.sprayColour = kSprayColours - 1;
            mine.hostMode = kModeTeamplay;
            std::memcpy(mine.hostMap, "rocket_frenzy", 14);
            mine.fragLimit = 50;
            mine.minuteLimit = 0;
            mine.weaponsStay = mine.fallDamage = mine.friendlyFire = mine.flashlight = 1;
            mine.hostAutoaim = 0;
            mine.maxPlayers = kPlayersMost;
            mine.allowGuests = 1;
            for (std::uint16_t &key : mine.keys)
                key = kAssignable;
            bytes = writeSettings(mine);
            SettingsData read;
            check(bytes.size() <= kSettingsMostBytes, "settings with every field in use still fit their file");
            check(readSettings(bytes.data(), bytes.size(), read) && read.topColour == 255 && read.bottomColour == 0 &&
                      read.spray == 17 && read.sprayColour == kSprayColours - 1 && read.hostMode == kModeTeamplay &&
                      std::strcmp(read.hostMap, "rocket_frenzy") == 0 && read.fragLimit == 50 &&
                      read.minuteLimit == 0 && read.weaponsStay == 1 && read.fallDamage == 1 &&
                      read.friendlyFire == 1 && read.flashlight == 1 && read.hostAutoaim == 0 &&
                      read.maxPlayers == kPlayersMost && read.allowGuests == 1,
                  "the profile and the match hosted read back as they were written");
            bool same = read.name[kProfileNameBytes - 1] == 0;
            for (std::size_t i = 0; i + 1 < kProfileNameBytes; ++i)
                same = same && read.name[i] == mine.name[i];
            check(same, "a name of any bytes is kept as typed, and never makes the file unread");
            const auto refused = [&](void (*spoil)(SettingsData &), const char *what)
            {
                SettingsData bad = mine;
                spoil(bad);
                const std::vector<std::uint8_t> file = writeSettings(bad);
                SettingsData untouched;
                check(!readSettings(file.data(), file.size(), untouched) && untouched.maxPlayers == 4, what);
            };
            refused([](SettingsData &d) { d.maxPlayers = kPlayersMost + 1; }, "nine players are not believed");
            refused([](SettingsData &d) { d.maxPlayers = 1; }, "nor one alone");
            refused([](SettingsData &d) { d.hostMode = kModeChoices; }, "nor a mode there is not");
            refused([](SettingsData &d) { d.sprayColour = kSprayColours; }, "nor a spray colour there is not");
            refused([](SettingsData &d) { d.friendlyFire = 2; }, "nor a switch that is neither on nor off");
        }
    }

    /// The lamp's rules: the game's.
    // The lamp's light added to a wall (HlLampPatch.hpp): a patch of the
    // wall's own texture, its corners as bright as the game makes them.
    void testLampPatch()
    {
        // The curve, the converter's: a colour put back through it untouched.
        bool same = true;
        for (int c = 0; c < 32; ++c)
            same = same && lampLift(c * 256, 0) == c;
        check(same, "a colour with no light added is the colour it was");
        bool rising = true;
        for (int c = 0; c < 31; ++c)
            rising = rising && lampLift(c * 256, 16) >= c && lampLift(c * 256, 800) >= lampLift(c * 256, 16);
        check(rising, "... and more light makes it no darker");
        // (The tables are the converter's curve: 255 * 338/256 * v^0.8,
        // shifted to five bits.)
        bool table = true;
        for (int i = 0; i < kLampColours; ++i)
        {
            const long v = i == 0 ? 0 : std::lround(255.0 * 338.0 / 256.0 * std::pow(i / 255.0, 0.8));
            table = table && kLampColourOf[i] == (std::min(255L, v) >> 3);
        }
        for (int c = 0; c < 32; ++c)
        {
            const double lm = 255.0 * std::pow((c * 8 + 3.5) / 255.0 / (338.0 / 256.0), 1.0 / 0.8);
            table = table && std::abs(kLampLightOf[c] - lm * 16) <= 1;
        }
        check(table, "the lamp's tables are the converter's light curve");

        // A floor 512 square at z 0, in two triangles, colour 10 all over,
        // its texture 16 sixteenths a unit along x and y; the light 10 above
        // it at the middle.
        auto corner = [](int x, int y) {
            LampCorner c{};
            c.pos[0] = fxFromInt(x);
            c.pos[1] = fxFromInt(y);
            c.colour = static_cast<std::uint16_t>(10 | 10 << 5 | 10 << 10);
            c.texCoord = (static_cast<std::uint32_t>(x * 16) & 0xFFFF) | (static_cast<std::uint32_t>(y * 16) << 16);
            return c;
        };
        const LampCorner first[3] = {corner(-256, -256), corner(256, -256), corner(256, 256)};
        const LampCorner second[3] = {corner(-256, -256), corner(256, 256), corner(-256, 256)};
        const fx up[3] = {0, 0, kFxOne};
        LampLight light{{0, 0, fxFromInt(10)}, 256};
        double area = 0;
        int centreColour = -1, edgeColour = -1, texOff = 0, polygons = 0;
        std::vector<std::array<long, 2>> seam[2];
        int side = 0;
        auto take = [&](const LampCorner *c, int n) {
            ++polygons;
            for (int i = 0; i < n; ++i)
            {
                const double x = c[i].pos[0] / 4096.0, y = c[i].pos[1] / 4096.0;
                const double x1 = c[(i + 1) % n].pos[0] / 4096.0, y1 = c[(i + 1) % n].pos[1] / 4096.0;
                area += (x * y1 - x1 * y) / 2;
                if (std::fabs(x) < 0.01 && std::fabs(y) < 0.01)
                    centreColour = c[i].colour & 31;
                if (std::fabs(std::max(std::fabs(x), std::fabs(y)) + std::min(std::fabs(x), std::fabs(y)) / 2 - 70) < 0.05)
                    edgeColour = c[i].colour & 31;
                const int s = static_cast<std::int16_t>(c[i].texCoord & 0xFFFF), t = static_cast<std::int16_t>(c[i].texCoord >> 16);
                texOff = std::max(texOff, std::max(std::abs(s - static_cast<int>(std::lround(x * 16))),
                                                   std::abs(t - static_cast<int>(std::lround(y * 16)))));
                if (std::fabs(x - y) < 0.0001)
                    seam[side].push_back({c[i].pos[0], c[i].pos[1]});
            }
        };
        const int made = lampPatchTriangle(first, up, light, take);
        side = 1;
        const int made2 = lampPatchTriangle(second, up, light, take);
        // rad = 80 - 10 = 70: the octagon's area is 8/3 of 70 squared.
        check(made > 0 && made2 > 0 && std::fabs(std::fabs(area) - 8.0 * 70 * 70 / 3) < 20,
              "the patch covers the game's octagon of light, 80 less how far the light is off the floor");
        check(texOff <= 1, "... in the floor's own texture, where it was");
        const double lm = kLampLightOf[10] / 16.0 + 70.0 * 255.0 / 264.0;
        const int want = static_cast<int>(std::min(255L, std::lround(255.0 * 338.0 / 256.0 * std::pow(lm / 255.0, 0.8))) >> 3);
        check(centreColour == want, "at its middle, the floor's light and the game's 70 added, through the curve");
        check(edgeColour == 10, "at its edge, the floor as it was: no seam");
        std::sort(seam[0].begin(), seam[0].end());
        std::sort(seam[1].begin(), seam[1].end());
        check(!seam[0].empty() && seam[0] == seam[1], "where the two triangles meet, both cut the light at the same points");
        int none = 0;
        light.at[2] = fxFromInt(81);
        none += lampPatchTriangle(first, up, light, take);
        light.at[2] = fxFromInt(10);
        light.at[0] = fxFromInt(600);
        none += lampPatchTriangle(first, up, light, take);
        check(none == 0, "no patch for a light 80 off the floor, or beside it");
        check(polygons > 8 && made + made2 < 80, "a patch is a few dozen triangles");
    }

    // The lamp's light on the game's grid (HlLampGrid.hpp): squares of the
    // texture's own grid, lit as the game lights its light map, meeting
    // without a gap or a seam however the surface is cut.
    namespace lampgrid
    {
        /// What the builder sent: each polygon (a strip's quads one by
        /// one), its corners with their texture coordinates put back.
        struct Taken
        {
            std::vector<std::vector<LampVertex>> polygons;
            int sent = 0; ///< polygons as the hardware counts them
            bool strip(const LampVertex *const *v, int n, std::int32_t ds, std::int32_t dt)
            {
                for (int i = 0; i + 3 < n; i += 2)
                {
                    std::vector<LampVertex> q = {*v[i], *v[i + 1], *v[i + 3], *v[i + 2]};
                    for (LampVertex &x : q)
                    {
                        x.s += ds;
                        x.t += dt;
                    }
                    polygons.push_back(q);
                    ++sent;
                }
                return true;
            }
            bool polygon(const LampVertex *const *v, int n, std::int32_t ds, std::int32_t dt)
            {
                std::vector<LampVertex> q;
                for (int i = 0; i < n; ++i)
                {
                    LampVertex x = *v[i];
                    x.s += ds;
                    x.t += dt;
                    q.push_back(x);
                }
                polygons.push_back(q);
                ++sent;
                return true;
            }
        };

        /// A floor at z 0, a texel a unit (s = 16 x, t = 16 y), its light
        /// running from 8 at x -256 to 24 at 256 (red only; green 10, blue
        /// 5): one quad per box (x0, y0, x1, y1) given, each moved by
        /// `shift` whole repeats of a 64-texel texture if asked.
        std::vector<std::uint32_t> floorOf(const std::vector<std::array<int, 4>> &boxes, int shiftedBox = -1, int shift = 0)
        {
            GxPacker packer;
            packer.begin(GxPrimitive::Quads);
            int index = 0;
            for (const auto &b : boxes)
            {
                const int xs[4] = {b[0], b[2], b[2], b[0]}, ys[4] = {b[1], b[1], b[3], b[3]};
                for (int i = 0; i < 4; ++i)
                {
                    GxVertex v;
                    v.x = static_cast<std::int16_t>(xs[i] * 4);
                    v.y = static_cast<std::int16_t>(ys[i] * 4);
                    v.z = 0;
                    const int moved = index == shiftedBox ? shift * 64 * 16 : 0;
                    v.s = static_cast<std::int16_t>(xs[i] * 16 + moved);
                    v.t = static_cast<std::int16_t>(ys[i] * 16);
                    const int red = 8 + (xs[i] + 256) * 16 / 512;
                    v.colour = static_cast<std::uint16_t>(red | 10 << 5 | 5 << 10);
                    packer.vertex(v);
                }
                ++index;
            }
            return packer.finish();
        }

        /// The same floor as two triangles, cut corner to corner.
        std::vector<std::uint32_t> floorOfTriangles()
        {
            GxPacker packer;
            packer.begin(GxPrimitive::Triangles);
            const int corners[6][2] = {{-256, -256}, {256, -256}, {256, 256}, {-256, -256}, {256, 256}, {-256, 256}};
            for (const auto &c : corners)
            {
                GxVertex v;
                v.x = static_cast<std::int16_t>(c[0] * 4);
                v.y = static_cast<std::int16_t>(c[1] * 4);
                v.s = static_cast<std::int16_t>(c[0] * 16);
                v.t = static_cast<std::int16_t>(c[1] * 16);
                const int red = 8 + (c[0] + 256) * 16 / 512;
                v.colour = static_cast<std::uint16_t>(red | 10 << 5 | 5 << 10);
                packer.vertex(v);
            }
            return packer.finish();
        }

        /// The wall's own light at x (as floorOf makes it), red in 256ths.
        double redAt(double x) { return (8 + (x + 256) * 16 / 512) * 256; }
    } // namespace lampgrid

    void testLampGrid()
    {
        using namespace lampgrid;
        static LampGrid grid; // (as the console has it: not on the stack)
        {
            // A piece sent as a quad must not be one the hardware draws
            // wrongly once rounded to quarter units (GBATEK's prohibited
            // quads): a square is; a side's middle a step inside its line is
            // not (concave); exactly on its line, it is.
            auto quad = [](std::initializer_list<std::array<double, 2>> xy)
            {
                std::array<LampVertex, 4> q{};
                int i = 0;
                for (const auto &p : xy)
                {
                    q[i].pos[0] = static_cast<fx>(p[0] * kFxOne);
                    q[i].pos[1] = static_cast<fx>(p[1] * kFxOne);
                    q[i].pos[2] = 0;
                    ++i;
                }
                return q;
            };
            auto safe = [](const std::array<LampVertex, 4> &q)
            {
                const LampVertex *v[4] = {&q[0], &q[1], &q[2], &q[3]};
                return LampGrid::quadSafe(v);
            };
            check(safe(quad({{0, 0}, {16, 0}, {16, 16}, {0, 16}})), "lamp: a square piece goes as a quad");
            check(!safe(quad({{0, 0}, {8, 0.25}, {16, 0}, {8, 16}})),
                  "lamp: ... not one with a corner a step inside the line of its neighbours");
            check(safe(quad({{0, 0}, {8, 0}, {16, 0}, {8, 16}})),
                  "lamp: ... but one with a corner exactly on that line, yes");
        }
        const fx up[3] = {0, 0, kFxOne};
        const LampGridTexture texture{64, 64, 64, 64};
        const LampLight light{{fxFromInt(10), fxFromInt(5), fxFromInt(10)}, 256};

        // The game's light, worked out apart: rad 70 (80 less the light's
        // 10 above the floor), dist max + min / 2 in texels from (10, 5),
        // 255 for each unit in a light map counting 264 for 256 of the
        // wall's, in sixteenths -- through the converter's curve.
        auto gameAdd = [](double x, double y) {
            const double a = std::fabs(x - 10), b = std::fabs(y - 5);
            const double over = 70 - (std::max(a, b) + std::min(a, b) / 2);
            return over <= 0 ? 0.0 : over * 255.0 / 264.0 * 16;
        };

        auto run = [&](const std::vector<std::uint32_t> &words, int step, Taken &taken, LampGridCounts &counts,
                       const LampGridTexture &tex) {
            return grid.build(words.data(), static_cast<int>(words.size()), up, light, tex, step, taken, &counts);
        };
        auto areaOf = [](const std::vector<LampVertex> &q) {
            double area = 0;
            for (std::size_t i = 0; i < q.size(); ++i)
            {
                const LampVertex &a = q[i], &b = q[(i + 1) % q.size()];
                area += (static_cast<double>(a.s) * b.t - static_cast<double>(b.s) * a.t) / 2;
            }
            return std::fabs(area);
        };

        // Cut three ways: whole; two quads meeting off the grid; and four,
        // one of whose corners lies in the middle of another's side, on a
        // grid line (y 0) -- a corner a square's side must have too.
        const std::vector<std::vector<std::array<int, 4>>> cuts = {
            {{-256, -256, 256, 256}},
            {{-256, -256, 40, 256}, {40, -256, 256, 256}},
            {{-256, 0, 256, 256}, {-256, -256, 40, 0}, {40, -256, 256, 0}},
        };
        for (int step : {16, 32})
        {
            for (std::size_t cut = 0; cut <= cuts.size(); ++cut)
            {
                Taken taken;
                LampGridCounts counts;
                // (The last: two triangles, their diagonal across the light.)
                const std::vector<std::uint32_t> words = cut < cuts.size() ? floorOf(cuts[cut]) : floorOfTriangles();
                run(words, step, taken, counts, texture);
                // The lit squares' area, square by square, against what was
                // sent: all of it, nothing twice.
                double want = 0;
                for (int m = 0; m + 1 < grid.columns(); ++m)
                {
                    for (int n = 0; n + 1 < grid.rows(); ++n)
                    {
                        if (grid.addAt(m, n) > 0 || grid.addAt(m + 1, n) > 0 || grid.addAt(m, n + 1) > 0 ||
                            grid.addAt(m + 1, n + 1) > 0)
                            want += static_cast<double>(grid.lineS(m + 1) - grid.lineS(m)) * (grid.lineT(n + 1) - grid.lineT(n));
                    }
                }
                double area = 0;
                for (const auto &q : taken.polygons)
                    area += areaOf(q);
                char what[160];
                std::snprintf(what, sizeof what, "grid %d, cut %zu: the light covers its lit squares, once (%.0f of %.0f)",
                              step, cut, area, want);
                check(want > 0 && std::fabs(area - want) < 1, what);

                // Every vertex at one place is the same vertex: one place, one
                // colour, whichever polygon or square made it.
                bool same = true, flat = true, colours = true, rims = true, texels = true;
                std::vector<LampVertex> all;
                for (const auto &q : taken.polygons)
                {
                    for (const LampVertex &v : q)
                    {
                        for (const LampVertex &o : all)
                        {
                            if (o.s == v.s && o.t == v.t)
                                same = same && o.colour == v.colour && o.pos[0] == v.pos[0] && o.pos[1] == v.pos[1];
                        }
                        all.push_back(v);
                        flat = flat && v.pos[2] == 0;
                        // Where it is, by its texture coordinate (a texel a unit).
                        const double x = v.s / 16.0, y = v.t / 16.0;
                        texels = texels && std::fabs(v.pos[0] / 4096.0 - x) < 0.07 && std::fabs(v.pos[1] / 4096.0 - y) < 0.07;
                        // At a grid crossing: the game's light added to the wall's.
                        const bool onGrid = (v.s % (step * 16)) == 0 && (v.t % (step * 16)) == 0;
                        const double add = gameAdd(x, y);
                        if (onGrid)
                        {
                            const int red = lampLift(static_cast<std::int32_t>(std::lround(redAt(x))), static_cast<std::int32_t>(std::lround(add)));
                            colours = colours && std::abs((v.colour & 31) - red) <= 1;
                        }
                        if (onGrid && add == 0)
                            rims = rims && std::abs((v.colour & 31) * 256 - redAt(x)) <= 256;
                    }
                }
                std::snprintf(what, sizeof what, "grid %d, cut %zu: one place, one vertex -- the same colour and position", step, cut);
                check(same, what);
                check(flat, "... on the floor exactly");
                check(texels, "... where its texture coordinate says");
                check(colours, "... at each crossing, the floor's light and the game's added, through the curve");
                check(rims, "... and outside the light, the floor as it was");

                // No vertex in the middle of another polygon's side (the
                // hardware leaves a pixel's gap there).
                bool joined = true;
                for (const auto &q : taken.polygons)
                {
                    for (std::size_t i = 0; i < q.size(); ++i)
                    {
                        const LampVertex &a = q[i], &b = q[(i + 1) % q.size()];
                        const double len = std::hypot(b.s - a.s, b.t - a.t);
                        for (const LampVertex &v : all)
                        {
                            const double cross = (static_cast<double>(b.s) - a.s) * (v.t - a.t) - (static_cast<double>(b.t) - a.t) * (v.s - a.s);
                            const double along = (static_cast<double>(v.s) - a.s) * (b.s - a.s) + (static_cast<double>(v.t) - a.t) * (b.t - a.t);
                            if (std::fabs(cross) <= len * 1.5 && along > len * 1.5 && along < len * len - len * 1.5)
                                joined = false;
                        }
                    }
                }
                std::snprintf(what, sizeof what, "grid %d, cut %zu: no vertex in the middle of a side", step, cut);
                check(joined, what);
                std::printf("test_ui: lamp grid %d, cut %zu: %d polygons (%d whole squares in strips, %d cut)\n", step, cut,
                            taken.sent, counts.cells, counts.pieces);
                std::snprintf(what, sizeof what, "grid %d, cut %zu: %d polygons for a light 140 across", step, cut, taken.sent);
                check(taken.sent > 0 && taken.sent <= (step == 16 ? 150 : 50), what);
            }
        }

        // A face moved by whole repeats (a group too wide for the
        // hardware's coordinates): the same light, its own coordinates.
        {
            Taken plain, moved;
            LampGridCounts counts;
            run(floorOf(cuts[1]), 16, plain, counts, texture);
            run(floorOf(cuts[1], 1, 3), 16, moved, counts, texture);
            bool same = plain.polygons.size() == moved.polygons.size();
            for (std::size_t i = 0; same && i < plain.polygons.size(); ++i)
            {
                for (std::size_t k = 0; same && k < plain.polygons[i].size(); ++k)
                {
                    const LampVertex &a = plain.polygons[i][k], &b = moved.polygons[i][k];
                    const int ds = b.s - a.s;
                    same = a.pos[0] == b.pos[0] && a.pos[1] == b.pos[1] && a.colour == b.colour && a.t == b.t &&
                           (ds == 0 || ds == 3 * 64 * 16);
                }
            }
            check(same, "a face moved by whole repeats gets the same light, in its own coordinates");
        }

        // A texture cooked at half the game's size: the grid is the game's
        // texels, twice as far apart in the cooked ones.
        {
            Taken taken;
            LampGridCounts counts;
            run(floorOf(cuts[0]), 16, taken, counts, LampGridTexture{64, 64, 128, 128});
            bool onGame = grid.columns() > 2;
            for (int m = 0; m < grid.columns(); ++m)
                onGame = onGame && grid.lineS(m) % 128 == 0;
            check(onGame, "a texture cooked smaller: the grid on the game's texels");
        }

        // No light: 80 above the floor, or off beside it.
        {
            Taken taken;
            LampGridCounts counts;
            const LampLight high{{0, 0, fxFromInt(81)}, 256};
            const std::vector<std::uint32_t> words = floorOf(cuts[0]);
            grid.build(words.data(), static_cast<int>(words.size()), up, high, texture, 16, taken, &counts);
            const LampLight beside{{fxFromInt(600), 0, fxFromInt(10)}, 256};
            grid.build(words.data(), static_cast<int>(words.size()), up, beside, texture, 16, taken, &counts);
            check(taken.polygons.empty(), "no light 80 off the floor, or beside it");
        }
    }

    // The lamp's grid on thousands of random surfaces: meshes whose
    // neighbouring polygons share their corners exactly -- quads of a
    // jittered lattice, some cut into triangles -- of four kinds: ordinary,
    // tiny, long and thin, and with corners on (or a quarter unit off) the
    // grid's lines. What is made must hold, every time: one vertex at one
    // place whichever polygon or square made it (a shared side cut the same
    // both ways round: no crack), each where its texture coordinate puts it
    // within a unit, and the lit squares covered once.
    void testLampGridRandom()
    {
        using namespace lampgrid;
        static LampGrid grid;
        std::uint64_t seed = 0x51ED27A3C0FFEE11ull;
        auto next = [&seed]()
        {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            return seed;
        };
        auto upTo = [&next](int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); };
        const fx up[3] = {0, 0, kFxOne};
        const LampGridTexture texture{64, 64, 64, 64};
        int surfaces = 0, polygonsIn = 0, polygonsOut = 0, badSame = 0, badPlace = 0, badArea = 0, badJoin = 0;
        std::uint64_t sum = 1469598103934665603ull; // (of everything made: FNV-1a)
        auto mix = [&sum](std::int64_t v)
        {
            sum = (sum ^ static_cast<std::uint64_t>(v)) * 1099511628211ull;
        };

        for (int run = 0; run < 3000; ++run)
        {
            const int kind = run % 4; // 0 ordinary, 1 tiny, 2 long and thin, 3 on the grid's lines
            const int step = (run / 4) % 2 ? 32 : 16;
            // The light: over the floor, 5 to 70 units up, about the middle.
            const int lightX = upTo(161) - 80, lightY = upTo(161) - 80;
            const LampLight light{{fxFromInt(lightX), fxFromInt(lightY), fxFromInt(5 + upTo(66))}, 64 + upTo(193)};
            // The lattice, in quarter units (a texel a unit: s = 16 x).
            int columns = 3 + upTo(4), rows = 3 + upTo(4), across = 0, along = 0;
            switch (kind)
            {
            case 0: across = 64 + upTo(193); along = 64 + upTo(193); break;
            case 1: across = 2 + upTo(11); along = 2 + upTo(11); columns = rows = 4 + upTo(5); break;
            case 2: across = 800 + upTo(1201); along = 4 + upTo(9); columns = 2 + upTo(2); rows = 3 + upTo(6); break;
            default: across = step * 4; along = step * 4; break;
            }
            const int x0 = lightX * 4 - columns * across / 2, y0 = lightY * 4 - rows * along / 2;
            std::vector<std::array<int, 2>> at((columns + 1) * (rows + 1));
            std::vector<std::uint16_t> colour(at.size());
            for (int j = 0; j <= rows; ++j)
            {
                for (int i = 0; i <= columns; ++i)
                {
                    int x = x0 + i * across, y = y0 + j * along;
                    if (kind == 3)
                    {
                        // On a line of the grid, or a quarter unit off it.
                        // (Rounded down, below 0 too: no two on one line.)
                        const int line = step * 4;
                        x = (x >= 0 ? x / line : -((-x + line - 1) / line)) * line + upTo(3) - 1;
                        y = (y >= 0 ? y / line : -((-y + line - 1) / line)) * line + upTo(3) - 1;
                    }
                    else if (i > 0 && i < columns && j > 0 && j < rows)
                    {
                        // Inside: moved by up to a fifth of a cell (still convex).
                        x += across >= 10 ? upTo(across / 5 * 2 + 1) - across / 5 : 0;
                        y += along >= 10 ? upTo(along / 5 * 2 + 1) - along / 5 : 0;
                    }
                    at[j * (columns + 1) + i] = {x, y};
                    colour[j * (columns + 1) + i] =
                        static_cast<std::uint16_t>(upTo(32) | upTo(32) << 5 | upTo(32) << 10);
                }
            }
            auto vertexOf = [&](int i, int j)
            {
                const auto &p = at[j * (columns + 1) + i];
                GxVertex v;
                v.x = static_cast<std::int16_t>(p[0]);
                v.y = static_cast<std::int16_t>(p[1]);
                v.z = 0;
                v.s = static_cast<std::int16_t>(p[0] * 4);
                v.t = static_cast<std::int16_t>(p[1] * 4);
                v.colour = colour[j * (columns + 1) + i];
                return v;
            };
            // Each cell a quad, or two triangles cut either way.
            std::vector<int> cut(columns * rows);
            for (int &c : cut)
                c = upTo(3);
            GxPacker packer;
            packer.begin(GxPrimitive::Quads);
            int sentIn = 0;
            for (int j = 0; j < rows; ++j)
                for (int i = 0; i < columns; ++i)
                {
                    if (cut[j * columns + i] != 0)
                        continue;
                    for (const auto &c : {std::array<int, 2>{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}})
                        packer.vertex(vertexOf(c[0], c[1]));
                    ++sentIn;
                }
            packer.begin(GxPrimitive::Triangles);
            for (int j = 0; j < rows; ++j)
                for (int i = 0; i < columns; ++i)
                {
                    const int c = cut[j * columns + i];
                    if (c == 0)
                        continue;
                    const std::array<std::array<int, 2>, 6> tri =
                        c == 1 ? std::array<std::array<int, 2>, 6>{{{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j}, {i + 1, j + 1}, {i, j + 1}}}
                               : std::array<std::array<int, 2>, 6>{{{i, j}, {i + 1, j}, {i, j + 1}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}}};
                    for (const auto &k : tri)
                        packer.vertex(vertexOf(k[0], k[1]));
                    sentIn += 2;
                }
            const std::vector<std::uint32_t> words = packer.finish();
            Taken taken;
            LampGridCounts counts;
            grid.build(words.data(), static_cast<int>(words.size()), up, light, texture, step, taken, &counts);
            ++surfaces;
            polygonsIn += sentIn;
            polygonsOut += taken.sent;

            // One place, one vertex: two points at the very same place are
            // the same -- texture coordinate and colour -- whichever polygon
            // or square made them. (Not the other way round: two sides of a
            // sliver may cross a grid line within a sixteenth of a texel of
            // each other, distinct points with one coordinate.)
            std::vector<const LampVertex *> all;
            bool same = true, placed = true;
            for (const auto &q : taken.polygons)
            {
                for (const LampVertex &v : q)
                {
                    for (const LampVertex *o : all)
                    {
                        if (o->pos[0] == v.pos[0] && o->pos[1] == v.pos[1] && o->pos[2] == v.pos[2])
                            same = same && o->colour == v.colour && o->s == v.s && o->t == v.t;
                    }
                    all.push_back(&v);
                    // Where its texture coordinate puts it: x = s / 16 units.
                    placed = placed && std::fabs(v.pos[0] / 4096.0 - v.s / 16.0) <= 1.0 &&
                             std::fabs(v.pos[1] / 4096.0 - v.t / 16.0) <= 1.0 && v.pos[2] == 0;
                }
            }
            for (const auto &q : taken.polygons)
                for (const LampVertex &v : q)
                {
                    mix(v.s);
                    mix(v.t);
                    mix(v.pos[0]);
                    mix(v.pos[1]);
                    mix(v.colour);
                }
            badSame += same ? 0 : 1;
            badPlace += placed ? 0 : 1;
            // No vertex in the middle of another piece's side: a side cut
            // the same both ways round (the hardware leaves a pixel's gap
            // at a T).
            for (const auto &q : taken.polygons)
            {
                for (std::size_t i = 0; i < q.size(); ++i)
                {
                    const LampVertex &a = q[i], &b = q[(i + 1) % q.size()];
                    const double dx = static_cast<double>(b.pos[0]) - a.pos[0], dy = static_cast<double>(b.pos[1]) - a.pos[1];
                    const double len = std::hypot(dx, dy);
                    if (len < 4096)
                        continue; // (a side under a unit: nothing to see in it)
                    for (const LampVertex *v : all)
                    {
                        const double cross = dx * (v->pos[1] - a.pos[1]) - dy * (v->pos[0] - a.pos[0]);
                        const double along = (v->pos[0] - a.pos[0]) * dx + (v->pos[1] - a.pos[1]) * dy;
                        // (On the side, to the rounding of a point worked out
                        // on it -- two 4096ths of a unit -- and well inside
                        // its ends.)
                        if (std::fabs(cross) <= len * 2 && along > len * 4096 && along < len * len - len * 4096)
                            ++badJoin;
                    }
                }
            }
            // The light's squares covered once: what was sent of them, their
            // area, against the polygons' area inside the squares lit.
            double want = 0, area = 0;
            for (const auto &q : taken.polygons)
            {
                double a = 0;
                for (std::size_t i = 0; i < q.size(); ++i)
                {
                    const LampVertex &p = q[i], &r = q[(i + 1) % q.size()];
                    a += (static_cast<double>(p.s) * r.t - static_cast<double>(r.s) * p.t) / 2;
                }
                area += std::fabs(a);
            }
            // (Each piece sent lies inside one lit square and inside the
            // mesh: their area is at most the lit squares'. Covered "once"
            // is that it never goes over.)
            for (int m = 0; m + 1 < grid.columns(); ++m)
                for (int n = 0; n + 1 < grid.rows(); ++n)
                    want += static_cast<double>(grid.lineS(m + 1) - grid.lineS(m)) * (grid.lineT(n + 1) - grid.lineT(n));
            if (area > want + 1)
                ++badArea;
        }
        std::printf("test_ui: lamp grid on %d random surfaces: %d polygons in, %d out (all made: %016llx)\n", surfaces,
                    polygonsIn, polygonsOut, static_cast<unsigned long long>(sum));
        check(polygonsOut > surfaces, "random surfaces: the light is made on them");
        check(badSame == 0, "random surfaces: one place, one vertex, whichever polygon or square made it");
        check(badPlace == 0, "random surfaces: every vertex within a unit of where its texture coordinate puts it");
        check(badArea == 0, "random surfaces: the light's pieces never cover more than its squares");
        check(badJoin == 0, "random surfaces: no vertex in the middle of another piece's side");
    }

    // How far the lamp's solid light is pulled toward the eye over its wall
    // (lampPull): a pixel's depth, and 8/4096 more -- the probe's "s1px"
    // (tools/hwtest/lamppatch P5) at the angles it was measured at.
    void testLampPull()
    {
        check(lampPull(4096) == 8 && lampPull(5000) == 8, "square on: only the rounding's 8/4096");
        bool same = true, grows = true;
        for (const int grazing : {90, 60, 45, 30, 20, 10})
        {
            const double off = (90 - grazing) * 3.14159265358979 / 180.0;
            const int cosine = static_cast<int>(std::lround(std::cos(off) * 4096));
            const double probe = 8 + std::tan(off) / 128.0 * 4096.0;
            same = same && std::abs(lampPull(cosine) - probe) <= 2;
        }
        int last = 0;
        for (int cosine = 4096; cosine >= 0; --cosine)
        {
            grows = grows && lampPull(cosine) >= last && lampPull(cosine) <= 2048;
            last = lampPull(cosine);
        }
        check(same, "it is the probe's pull at 90 to 10 degrees");
        check(grows && lampPull(0) == lampPull(82), "it grows as the wall is seen edge on, and stops");
    }

    // A surface's packed geometry read back a triangle at a time, as the
    // lamp's patches read it (lampStreamTriangles): every form of vertex the
    // converter packs, in every kind of run.
    void testLampStream()
    {
        // A strip of quads along x, its vertices on a grid so that the
        // packer takes each of its forms (16-bit, ten-bit, two axes, a
        // difference).
        std::vector<GxVertex> grid;
        for (int k = 0; k < 6; ++k)
        {
            for (int side = 0; side < 2; ++side)
            {
                GxVertex v;
                v.x = static_cast<std::int16_t>(k * (k % 2 ? 37 : 64) * 4);
                v.y = static_cast<std::int16_t>(side * 64 * 4);
                v.z = static_cast<std::int16_t>(k == 3 ? 12 : (k == 4 ? 12 : -64 * 4));
                v.s = static_cast<std::int16_t>(k * 16);
                v.t = static_cast<std::int16_t>(side * 16);
                v.colour = static_cast<std::uint16_t>(k + side * 32);
                grid.push_back(v);
            }
        }
        for (const GxPrimitive type : {GxPrimitive::QuadStrip, GxPrimitive::TriangleStrip, GxPrimitive::Triangles,
                                       GxPrimitive::Quads})
        {
            GxPacker packer;
            packer.begin(type);
            for (const GxVertex &v : grid)
                packer.vertex(v);
            const std::vector<std::uint32_t> words = packer.finish();
            std::vector<GxRun> runs;
            std::string error;
            const bool decoded = decodeGxStream(words.data(), words.size(), runs, error);
            std::vector<std::array<int, 6>> mine;
            const bool read = true;
            lampStreamTriangles(words.data(), static_cast<int>(words.size()), [&](const LampCorner *t) {
                for (int i = 0; i < 3; ++i)
                    mine.push_back({static_cast<int>(t[i].pos[0] >> 10), static_cast<int>(t[i].pos[1] >> 10),
                                    static_cast<int>(t[i].pos[2] >> 10), t[i].colour,
                                    static_cast<std::int16_t>(t[i].texCoord & 0xFFFF), static_cast<std::int16_t>(t[i].texCoord >> 16)});
            });
            // Every corner given is one of the run's own, as it was packed.
            bool same = decoded && read && !mine.empty();
            for (const auto &c : mine)
            {
                bool found = false;
                for (const GxVertex &v : runs[0].vertices)
                    found = found || (c[0] == v.x && c[1] == v.y && c[2] == v.z && c[3] == v.colour && c[4] == v.s && c[5] == v.t);
                same = same && found;
            }
            const int wanted = type == GxPrimitive::QuadStrip ? 10 : type == GxPrimitive::TriangleStrip ? 10 : type == GxPrimitive::Triangles ? 4 : 6;
            check(same && static_cast<int>(mine.size()) == wanted * 3,
                  "a surface's packed geometry is read back a triangle at a time, as it was packed");
        }
    }

    void testLamp()
    {
        LampState lamp;
        check(!lamp.on && lampPercent(lamp) == 99 && !lampToggle(lamp, false) && !lamp.on,
              "the lamp starts out with the game's 99, and without the suit there is none to light");
        check(lampToggle(lamp, true) && lamp.on, "with the suit it lights");
        int ms = 0;
        bool wentOut = false;
        while (ms < 200000 && !wentOut)
        {
            wentOut = lampTick(lamp, 33);
            ms += 33;
        }
        check(wentOut && !lamp.on && lampPercent(lamp) == 0 && ms > 110000 && ms < 130000,
              "alight it lasts about two minutes, and goes out by itself");
        check(lampToggle(lamp, true) && lamp.on && lampTick(lamp, 33) && !lamp.on,
              "with nothing in it, it lights as the game's does -- and is out again at once");
        for (ms = 0; ms < 1000; ms += 33)
            lampTick(lamp, 33);
        check(lampToggle(lamp, true) && lamp.on && lampToggle(lamp, true) && !lamp.on,
              "a second later there is something in it; the switch puts it out as well");
        for (ms = 0; lampPercent(lamp) < 100 && ms < 60000; ms += 33)
            lampTick(lamp, 33);
        check(ms > 15000 && ms < 25000 && lampPercent(lamp) == 100, "out, it is full again in about twenty seconds");
        // The light where it lands: the game's -- the same size however far
        // off, as bright as it gets out to five hundred units, fainter
        // beyond, nothing past its reach.
        check(kLampRadius == 80 && kLampPatchRadius == kLampRadius, "the lamp's light is eighty units across its half");
        check(lampFalloff(60) == 256 && lampFalloff(500) == 256 && lampFalloff(1000) == 64 && lampFalloff(1900) > 0 &&
                  lampFalloff(kLampReach) == 0,
              "as bright at five hundred units as at sixty, a quarter of that at a thousand, nothing at its reach");
    }

    /// The player's say about the picture (HlFog.hpp): kept, checked, and
    /// what each choice stands for.
    void testGraphics()
    {
        SettingsData data;
        check(data.graphics.valid() && data.graphics.fog == kFogOff && data.graphics.fogStrength == 7 &&
                  data.graphics.fogColour == 0 && data.graphics.modelDetail == 1 && data.graphics.worldDetail == 1 &&
                  data.graphics.modelLimit == 0 && data.graphics.drawDistance == 0 && data.graphics.dsiLight == 0,
              "to begin with: no fog (asked for, not quite solid, black); detail as it was; no DSi light");
        data.graphics.fog = kFogAlways;
        data.graphics.fogStrength = 4;
        data.graphics.fogDistance = 9;
        data.graphics.fogColour = 3;
        data.graphics.modelDetail = 2;
        data.graphics.worldDetail = 0;
        data.graphics.modelLimit = 10;
        data.graphics.drawDistance = 2;
        std::vector<std::uint8_t> bytes = writeSettings(data);
        SettingsData back;
        check(readSettings(bytes.data(), bytes.size(), back) && back.graphics.fog == kFogAlways &&
                  back.graphics.fogStrength == 4 && back.graphics.fogDistance == 9 && back.graphics.fogColour == 3 &&
                  back.graphics.modelDetail == 2 && back.graphics.worldDetail == 0 && back.graphics.modelLimit == 10 &&
                  back.graphics.drawDistance == 2,
              "every one of them is kept");
        for (int which = 0; which < 5; ++which)
        {
            SettingsData bad;
            (which == 0 ? bad.graphics.fog : which == 1 ? bad.graphics.fogStrength : which == 2 ? bad.graphics.fogColour
                                                       : which == 3 ? bad.graphics.modelLimit : bad.graphics.drawDistance) =
                which == 3 ? 3 : 11;
            bytes = writeSettings(bad);
            SettingsData untouched;
            check(!readSettings(bytes.data(), bytes.size(), untouched) && untouched.graphics.fogStrength == 7,
                  "one out of range is not believed");
        }
        // The fog's densities: none at its near edge, the strength at its
        // far one, never falling, slow to begin.
        std::uint8_t whole[32], soft[32], none[32];
        fogTable(10, whole);
        fogTable(7, soft);
        fogTable(0, none);
        bool rising = true, off = true;
        for (int i = 1; i < 32; ++i)
        {
            rising = rising && whole[i] >= whole[i - 1] && soft[i] >= soft[i - 1] && soft[i] <= whole[i];
            off = off && none[i] == 0;
        }
        check(whole[0] == 0 && whole[31] == 127 && soft[0] == 0 && soft[31] == 88 && rising && off && whole[15] < 40,
              "the fog is none at its near edge and as solid as it is set at its far one, coming on slowly");
        {
            std::uint8_t edge[32], noneEdge[32];
            fogTable(7, edge, true);
            fogTable(0, noneEdge, true);
            check(edge[31] == 127 && edge[30] == 127 && edge[29] == soft[29] && noneEdge[31] == 0,
                  "a fog hiding an edge is whole at its end, the rest as it is set");
        }
        {
            GraphicsSettings say;
            say.fog = kFogWhenNeeded;
            say.drawDistance = 0;
            check(fogWanted(say, false, 8192, false, 0).end == kFogNone, "when needed: none on a view seen whole");
            say.drawDistance = 1;
            const FogWant medium = fogWanted(say, false, 8192, false, 0);
            check(medium.end == drawDistanceUnits(1) && medium.hidesEdge,
                  "... but where a nearer draw distance cuts what could be seen, whole there");
            say.drawDistance = 0;
            const FogWant small = fogWanted(say, false, 2048, false, 0);
            check(small.end == 2048 && small.hidesEdge, "... and on a map seen no farther than it is drawn");
            const FogWant cut = fogWanted(say, false, 8192, true, 1600);
            check(cut.end == fogCutEnd(1600, say.fogDistance) && cut.hidesEdge, "... and where the view is cut");
            check(fogWanted(say, false, 8192, true, 100).end == kFogNone, "... but not for a cut too near to hide");
            say.fog = kFogAlways;
            const FogWant always = fogWanted(say, false, 8192, false, 0);
            check(always.end == fogAlwaysEnd(say.fogDistance, 0) && !always.hidesEdge,
                  "always: at the player's distance, as soft as they have it");
            say.fog = kFogOff;
            check(fogWanted(say, true, 2048, true, 1600).end == kFogNone, "off: none, whatever is cut");
        }
        check(fogAlwaysEnd(0, 0) == 768 && fogAlwaysEnd(10, 0) == 6144 && fogAlwaysEnd(5, 0) > 3000 &&
                  fogAlwaysEnd(10, 2) == drawDistanceUnits(2) && fogAlwaysEnd(0, 2) == 768,
              "a fog that is always there is from 768 to 6144 units off, and never past what is drawn");
        check(fogCutEnd(1600, 0) == 1400 && fogCutEnd(1600, 10) == 1600 && fogCutEnd(1600, 5) == 1500 &&
                  fogCutEnd(100, 5) == kFogNone && fogCutEnd(0, 5) == kFogNone &&
                  fogCutEnd(511, 10) == kFogNone && fogCutEnd(512, 10) == 512,
              "one that hides a cut is whole from seven eighths of the way to it to all the way, never nearer than 192");
        check(drawDistanceUnits(0) == 8192 && drawDistanceUnits(1) == 3072 && drawDistanceUnits(2) == 1536,
              "the world is drawn far, less far, or near");
        check(modelDetailWeight(1) == 16 && modelDetailWeight(0) * 2 == 16 && modelDetailWeight(2) == 32,
              "models coarsen where they did, at half the distance, or at twice");
        bool never = true, sooner = true, gently = true;
        for (int p = 0; p <= kPressureMost; ++p)
        {
            never = never && worldDetailRatio(2, p) == 0;
            sooner = sooner && worldDetailRatio(0, p) != 0 &&
                     (worldDetailRatio(1, p) == 0 || worldDetailRatio(0, p) < worldDetailRatio(1, p));
            // (Each step leaves out a little more, never a jump: at most a
            // quarter of the ratio at once.)
            if (p > 1)
                gently = gently && worldDetailRatio(1, p) < worldDetailRatio(1, p - 1) &&
                         worldDetailRatio(1, p) * 4 >= worldDetailRatio(1, p - 1) * 3;
        }
        check(never && sooner && worldDetailRatio(1, 0) == 0 && worldDetailRatio(1, kPressureMost) == 22,
              "small far things are left out under load as they were, sooner, or never");
        check(gently, "... a little more at each step of the pressure");

        // Left out or kept with a margin: a surface at the edge is what it
        // was, whichever way the eye moves a step.
        check(detailKeeps(1000, 1, 0, false) && detailKeeps(kDetailNearest, 1, 64, false),
              "no detail, or near: always kept");
        const int ratio = 32, radius = 20; // (kept to 640 units)
        check(detailKeeps(700, radius, ratio, true) && !detailKeeps(740, radius, ratio, true),
              "kept, it goes only past nine eighths");
        check(!detailKeeps(600, radius, ratio, false) && detailKeeps(560, radius, ratio, false),
              "left out, it comes back only within seven eighths");
        bool steady = true;
        bool kept = true;
        for (int step = 0; step < 200; ++step)
        {
            // (The eye at the edge, a few units either way.)
            const int distance = 640 + ((step & 1) ? 30 : -30);
            const bool now = detailKeeps(distance, radius, ratio, kept);
            steady = steady && (step == 0 || now == kept);
            kept = now;
        }
        check(steady, "... at the edge, it does not come and go");

        // The budget the hardware's word leaves: on a console that strains
        // past some number of polygons used, with some part of what is
        // counted off the screen, it settles -- no round of up, strained,
        // down, up -- whatever the two are.
        bool settles = true, gentle = true;
        for (const int off : {20, 30, 40})
        {
            for (const int strainAt : {1400, 1500, 1600})
            {
                BudgetState state;
                int lastLines = 46;
                bool lastUnder = false;
                int strained = 0, jump = 0, before = 0;
                for (int pass = 0; pass < 3000; ++pass)
                {
                    const int budget = 1700 + state.extra;
                    const int used = budget * (100 - off) / 100;
                    BudgetReport report;
                    report.polygons = budget;
                    report.vertices = budget * 3;
                    report.hwPolygons = used;
                    report.hwVertices = used * 3;
                    report.lines = lastLines;
                    report.underflow = lastUnder;
                    const int lines = 46 - (used > strainAt - 300 ? (used - (strainAt - 300)) / 6 : 0);
                    lastUnder = lines < 0;
                    lastLines = lines < 0 ? 0 : lines;
                    budgetStep(state, report);
                    if (pass >= 2400)
                    {
                        strained += report.underflow || report.lines < 6;
                        const int d = budget > before ? budget - before : before - budget;
                        jump = d > jump ? d : jump;
                    }
                    before = budget;
                }
                settles = settles && strained == 0;
                gentle = gentle && jump <= kBudgetClimb;
            }
        }
        check(settles, "the budget settles below where the hardware strains");
        check(gentle, "... and does not jump about once it has (no more than a climb's step a pass)");
        {
            BudgetState state;
            state.extra = 300;
            BudgetReport overflowed;
            overflowed.overflow = true;
            budgetStep(state, overflowed);
            check(state.extra == 60 && state.ceiling == 60, "an overflow takes a step off at once, and marks a ceiling");
        }
        {
            // Room to spare, a great deal of it off the screen: the budget
            // climbs a step a pass, and no further than the most spare.
            BudgetState state;
            BudgetReport easy;
            easy.polygons = 1900;
            easy.vertices = 5800;
            easy.hwPolygons = 400;
            easy.hwVertices = 1200;
            budgetStep(state, easy);
            budgetStep(state, easy);
            check(state.extra == kBudgetClimb && state.extraVertices == kBudgetClimb * 3,
                  "with room to spare, the budget climbs one step a pass");
            for (int pass = 0; pass < 1000; ++pass)
                budgetStep(state, easy);
            check(state.extra == kBudgetMostSparePolygons && state.extraVertices == kBudgetMostSpareVertices,
                  "... up to the most spare, and no further");
        }
    }

    /// The touch screen's table: every control where the other hand's is
    /// mirrored, none on another, and a touch anywhere on exactly one thing.
    void testLayout()
    {
        const TouchLayout right = touchLayout(false), left = touchLayout(true);
        auto rects = [](const TouchLayout &at)
        {
            std::vector<TouchRect> all = {at.menu, at.use, at.jump, at.duck, at.reload, at.lamp};
            for (const TouchRect &slot : at.slot)
                all.push_back(slot);
            all.push_back(at.spray);
            all.push_back(at.walk);
            all.push_back(at.longJump);
            return all;
        };
        bool whole = true, apart = true, hits = true;
        for (const TouchLayout &at : {right, left, touchLayout(false, true), touchLayout(true, true),
                                      touchLayout(false, false, true), touchLayout(true, false, true),
                                      touchLayout(false, true, true), touchLayout(true, true, true),
                                      touchLayout(false, false, false, true), touchLayout(true, false, false, true),
                                      touchLayout(false, true, true, true)})
        {
            const std::vector<TouchRect> all = rects(at);
            int counts[kTouchLongJump + 1] = {};
            for (int y = 0; y < 192; ++y)
            {
                for (int x = 0; x < 256; ++x)
                {
                    int on = 0, which = kTouchAim;
                    for (std::size_t i = 0; i < all.size(); ++i)
                    {
                        if (all[i].has(x, y))
                        {
                            ++on;
                            which = static_cast<int>(i) + 1;
                        }
                    }
                    apart = apart && on <= 1;
                    hits = hits && touchHit(at, x, y) == which;
                    ++counts[which];
                    // (Nothing the pad's frame holds is anything but pad.)
                    if (at.pad.has(x, y))
                        apart = apart && on == 0;
                }
            }
            for (std::size_t i = 0; i < all.size(); ++i)
            {
                // (What this layout has not got: SPRAY, JUMP.)
                if (all[i].w == 0)
                    continue;
                whole = whole && all[i].x >= 0 && all[i].y >= 0 && all[i].x + all[i].w <= 256 &&
                        all[i].y + all[i].h <= 192 && counts[i + 1] == all[i].w * all[i].h && all[i].w >= 40 &&
                        all[i].h >= 22;
            }
            whole = whole && counts[kTouchAim] > 256 * 192 / 2;
        }
        check(whole && apart && hits,
              "every control is whole on the screen, big enough for a thumb, apart from the rest, and found by a touch on it");
        check(right.jump.x > right.pad.x + right.pad.w - 1 && left.jump.x + left.jump.w <= left.pad.x &&
                  left.jump.x == 256 - right.jump.x - right.jump.w && left.menu.x == 256 - right.menu.x - right.menu.w &&
                  left.reload.x == 0 && right.reload.x + right.reload.w == 256 &&
                  left.walk.x == 256 - right.walk.x - right.walk.w && right.use.x > right.menu.x + right.menu.w &&
                  right.walk.x > right.use.x + right.use.w && right.walk.x + right.walk.w < right.reload.x &&
                  left.pad.w == right.pad.w && left.duck.y == right.duck.y &&
                  right.jump.y < right.lamp.y && right.lamp.y < right.duck.y && right.lamp.x == right.jump.x &&
                  left.lamp.x == left.jump.x,
              "for the left hand the column and the bar's ends change sides, and USE and WALK change places");
        const TouchLayout walks = touchLayout(false, false, true), walksLeft = touchLayout(true, false, true);
        check(walks.jump.w == 0 && walks.walk.x == right.jump.x && walks.walk.y == right.jump.y &&
                  walks.walk.h == right.jump.h && walksLeft.use.x == walks.use.x && walks.use.w > walks.menu.w &&
                  touchHit(walks, walks.walk.x + 5, walks.walk.y + 5) == kTouchWalk,
              "the jump button asked to walk: WALK is where JUMP was, and USE has the middle of the bar again");
        bool order = true;
        for (int n = 0; n < kTouchSlots; ++n)
        {
            order = order && left.slot[n].x == right.slot[n].x &&
                    touchHit(left, right.slot[n].x + 20, right.slot[n].y + 10) == kTouchSlot + n &&
                    (n == 0 || right.slot[n].x == right.slot[n - 1].x + right.slot[n - 1].w);
        }
        check(order, "... and the weapons stay in their order, side by side");
    }

    void testGame(const char *dir)
    {
        const std::string valve(dir);
        std::string error;
        for (const char *name : {"FONT0", "FONT1"})
        {
            std::vector<std::uint8_t> lump;
            QFont font;
            const bool read = readWadLump(valve + "/fonts.wad", name, lump, error) && parseQFont(lump, font, error);
            check(read, "the game's menu font reads");
            if (!read)
                continue;
            const std::vector<std::uint8_t> cooked = cookFont(font, true);
            std::vector<std::uint32_t> aligned((cooked.size() + 3) / 4);
            std::memcpy(aligned.data(), cooked.data(), cooked.size());
            FontView view;
            const bool opens = view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()), cooked.size());
            int edge = 0, letter = 0;
            for (int row = 0; opens && row < view.height(); ++row)
            {
                for (std::uint32_t bits = view.glyph('A')[row]; bits != 0; bits >>= 2)
                {
                    edge += (bits & 3) == 1;
                    letter += (bits & 3) == 3;
                }
            }
            std::printf("test_ui: %s: %d high, 'A' %d wide (%d of letter, %d of edge), \"LOADING\" %d wide\n", name,
                        view.height(), view.width('A'), letter, edge, textWidth(view, "LOADING"));
            check(opens && view.height() >= 12 && view.width('A') >= 6 && view.width('i') < view.width('W') &&
                      letter > 10 && edge > letter,
                  "... and cooks: proportional, a light letter in a dark edge");
        }
        {
            // The Hazard Course's writing, in the game's own faces: each
            // lesson in its console's, with the game's own lines, all on
            // the screen -- its key words the DS's or not.
            std::vector<std::uint32_t> aligned[5];
            FontView views[5];
            const char *names[4] = {"creditsfont", "FONT1", "FONT0", "CONCHARS"};
            bool read = true;
            for (int f = 0; f < 4; ++f)
            {
                std::vector<std::uint8_t> lump;
                QFont font;
                read = read && readWadLump(valve + (f == 0 || f == 3 ? "/gfx.wad" : "/fonts.wad"), names[f], lump, error) &&
                       parseQFont(lump, font, error);
                if (!read)
                    break;
                const std::vector<std::uint8_t> cooked = cookFont(font, f == 1 || f == 2, f != 3);
                aligned[f].resize((cooked.size() + 3) / 4);
                std::memcpy(aligned[f].data(), cooked.data(), cooked.size());
                views[f].open(reinterpret_cast<const std::uint8_t *>(aligned[f].data()), cooked.size());
            }
            std::uint8_t bits[96 * 8] = {};
            aligned[4].resize(kPlainFontWords);
            plainFont(bits, true, aligned[4].data());
            views[4].open(reinterpret_cast<const std::uint8_t *>(aligned[4].data()), aligned[4].size() * 4);
            const FontView *faces[5] = {&views[0], &views[1], &views[2], &views[3], &views[4]};
            std::vector<std::uint8_t> titleBytes;
            std::string why;
            read = read && readFile(valve + "/titles.txt", titleBytes, why);
            const TitlesBook book =
                parseTitles(toCp1252(std::string(titleBytes.begin(), titleBytes.end())));
            int lessons = 0, inConsole = 0, ownLines = 0, onScreen = 0, rewrittenFit = 0;
            ControlSettings classic;
            classic.scheme = kControlsClassic;
            ControlSettings buttons;
            buttons.scheme = kControlsButtons;
            for (const auto &[name, title] : book.titles)
            {
                if (!read || name.rfind("HZ", 0) != 0)
                    continue;
                ++lessons;
                const int y = title.y < 0 ? -1 : static_cast<int>(title.y * 256);
                // (As TopText has a lesson: in the console's face.)
                const TopBlock block = topLayout(faces, 5, 3, title.text.c_str(), -1, y);
                int whole = 0;
                for (std::size_t c = 0; c < title.text.size(); ++c)
                    whole += title.text[c] != '\n' && (c == 0 || title.text[c - 1] == '\n') ? 1 : 0;
                if (block.face != 3 || block.lineCount != whole)
                    std::printf("test_ui: %s: face %d, %d lines of %d\n", name.c_str(), block.face, block.lineCount, whole);
                inConsole += block.face == 3;
                ownLines += block.lineCount == whole;
                onScreen += block.top + block.lineCount * block.pitch <= 192;
                // Its keys the DS's, in either layout: still all of it, and
                // still in a game face.
                bool fits = true;
                for (const ControlSettings *set : {&classic, &buttons})
                {
                    char said[320];
                    topKeyWords(title.text.c_str(), *set, said, sizeof(said));
                    const TopBlock again = topLayout(faces, 5, 3, said, -1, y);
                    int kept = 0, all = 0;
                    for (int l = 0; l < again.lineCount; ++l)
                        for (int k = 0; k < again.lines[l].count; ++k)
                            kept += said[again.lines[l].start + k] != ' ' ? 1 : 0;
                    for (const char *c = said; *c != '\0'; ++c)
                        all += *c != '\n' && *c != ' ' ? 1 : 0;
                    fits = fits && kept == all &&
                           again.top + again.lineCount * again.pitch <= 192;
                    if (set == &classic && name == "HZMOVE")
                        std::printf("test_ui: HZMOVE, classic: \"%s\"\n", said);
                }
                rewrittenFit += fits;
            }
            std::printf("test_ui: %d Hazard lessons: %d in the console's face, %d with the game's lines, %d on the "
                        "screen, %d still whole with the DS's keys\n",
                        lessons, inConsole, ownLines, onScreen, rewrittenFit);
            if (read)
            {
                check(lessons >= 25 && inConsole == lessons && ownLines >= lessons - 1 && onScreen == lessons,
                      "every Hazard lesson fits the game's console face, all but the widest with its own lines, on "
                      "the screen");
                check(rewrittenFit == lessons, "... and with the DS's keys named, all of it still fits");
            }
        }
        {
            std::vector<std::uint8_t> lump;
            QFont font;
            check(readWadLump(valve + "/gfx.wad", "creditsfont", lump, error) && parseQFont(lump, font, error) &&
                      cookFont(font, false).size() > 1000,
                  "the game's title font reads, by a name in any case");
            check(!readWadLump(valve + "/gfx.wad", "NOSUCH", lump, error), "a lump that is not there is said not to be");
        }
        {
            std::vector<std::uint8_t> lump;
            IndexedImage picture;
            const bool read = readWadLump(valve + "/cached.wad", "LOADING", lump, error) && parseQPic(lump, picture, error);
            check(read && picture.width == 320 && picture.height == 200, "the game's loading picture reads");
            if (read)
            {
                const RgbaImage texture = loadingTexture(picture);
                bool below = true;
                for (int x = 0; x < 256; ++x)
                    below = below && texture.at(x, 200)[0] == 0 && texture.at(x, 200)[3] == 255;
                int lit = 0;
                for (int x = 0; x < 256; ++x)
                    lit += texture.at(x, 96)[0] + texture.at(x, 96)[1] > 40;
                check(texture.width == 256 && texture.height == 256 && below && lit > 100,
                      "... and becomes a texture: the picture in its top 192 rows, black under it");
            }
        }
        {
            // The weapons' pictures for the touch screen.
            SprFile sheet;
            const bool got = sheet.load(valve + "/sprites/320hud1.spr", error);
            check(got, "the game's sheet of weapon pictures reads");
            if (got)
            {
                const std::vector<std::uint8_t> cooked = weaponIcons(sheet);
                std::vector<std::uint32_t> aligned((cooked.size() + 3) / 4);
                std::memcpy(aligned.data(), cooked.data(), cooked.size());
                IconsView view;
                const bool opens = view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()), cooked.size());
                bool drawn = opens, differ = true;
                int inks[kWeaponIcons] = {};
                for (int n = 0; opens && n < kWeaponIcons; ++n)
                {
                    int bright = 0;
                    for (int y = 0; y < view.height(); ++y)
                    {
                        for (int x = 0; x < view.width(); ++x)
                        {
                            inks[n] += view.level(n, x, y) != 0;
                            bright += view.level(n, x, y) == 3;
                        }
                    }
                    // (A picture: some of it, not all of it, and some of
                    // that as bright as it goes.)
                    drawn = drawn && inks[n] > 30 && inks[n] < view.width() * view.height() * 3 / 4 && bright > 3;
                    std::printf("test_ui: weapon %d's picture: %d of %d pixels\n", n + 1, inks[n],
                                view.width() * view.height());
                    for (int y = 0; y < view.height(); ++y)
                    {
                        char row[kIconMaxWidth + 1] = {};
                        for (int x = 0; x < view.width(); ++x)
                            row[x] = " .+#"[view.level(n, x, y)];
                        std::printf("  %s\n", row);
                    }
                }
                for (int n = 1; opens && n < kWeaponIcons; ++n)
                    differ = differ && std::memcmp(view.rows, view.rows + static_cast<std::size_t>(n) * view.height() *
                                                                                iconRowWords(view.width()),
                                                   static_cast<std::size_t>(view.height()) * iconRowWords(view.width()) * 4) != 0;
                check(opens && view.count() == kWeaponIcons && view.width() == kWeaponIconWidth &&
                          view.height() == kWeaponIconHeight && view.level(0, -1, 0) == 0 &&
                          view.level(kWeaponIcons, 0, 0) == 0,
                      "... and cooks to a picture for each weapon, of the size the slots are");
                check(drawn && differ, "each a picture, and no two the same");
                check(!view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()), cooked.size() - 4),
                      "one cut short is refused");
            }
            SprFile hud, crosshairs;
            const bool read = hud.load(valve + "/sprites/320hud2.spr", error) &&
                              crosshairs.load(valve + "/sprites/crosshairs.spr", error);
            check(read, "the game's head-up display sprites read");
            if (read)
            {
                const RgbaImage atlas = hudAtlas(hud, crosshairs);
                auto solid = [&](UiRect r)
                {
                    int count = 0;
                    for (int y = r.y; y < r.y + r.h; ++y)
                    {
                        for (int x = r.x; x < r.x + r.w; ++x)
                            count += atlas.at(x, y)[3] > 64;
                    }
                    return count;
                };
                bool digits = true;
                for (int n = 0; n < 10; ++n)
                    digits = digits && solid(hudDigit(n)) > 20;
                // (A "1" is less ink than an "8".)
                check(atlas.width == kHudAtlasWidth && atlas.height == kHudAtlasHeight && digits &&
                          solid(hudDigit(1)) < solid(hudDigit(8)),
                      "its digits are in the atlas where the console looks for them");
                check(solid(kHudCross) > 40 && solid(kHudSuitFull) > solid(kHudSuitEmpty) && solid(kHudAmmo9mm) > 20 &&
                          solid(kHudAmmoBuck) > 20 && solid(kHudAmmo357) > 20 && solid(kHudAmmoGrenade) > 20 &&
                          solid(kHudAmmoAr) > 20 && solid(kHudCrosshair) > 4,
                      "... and the cross, the suit, each kind of ammunition, the crosshair");
            }
            SprFile hud7, hud8, hud9, pain;
            const bool read640 = hud7.load(valve + "/sprites/640hud7.spr", error) &&
                                 hud8.load(valve + "/sprites/640hud8.spr", error) &&
                                 hud9.load(valve + "/sprites/640hud9.spr", error) &&
                                 pain.load(valve + "/sprites/640_pain.spr", error) && read;
            check(read640, "the game's 640 head-up display sprites read");
            if (read640)
            {
                const RgbaImage atlas = hudAtlas640(hud7, hud8, hud9, pain, crosshairs);
                auto solid = [&](UiRect r)
                {
                    int count = 0;
                    for (int y = r.y; y < r.y + r.h; ++y)
                    {
                        for (int x = r.x; x < r.x + r.w; ++x)
                            count += atlas.at(x, y)[3] > 64;
                    }
                    return count;
                };
                // Nothing laid out over anything else, nor off the texture.
                std::vector<UiRect> all = {kHud640SuitFull,  kHud640SuitEmpty, kHud640Cross,    kHud640LampEmpty,
                                           kHud640LampFull,  kHud640LampBeam,  kHud640Crosshair, kHud640Solid};
                for (int n = 0; n < 10; ++n)
                    all.push_back(hud640Digit(n));
                for (int n = 0; n < kHudAmmo640Count; ++n)
                    all.push_back(hud640Ammo(n));
                for (int n = 0; n < 8; ++n)
                    all.push_back(hud640Hurt(n));
                for (const UiRect &r : kHud640Pain)
                    all.push_back(r);
                bool apart = true;
                for (std::size_t i = 0; i < all.size(); ++i)
                {
                    const UiRect &a = all[i];
                    apart = apart && a.x + a.w <= kHudAtlasWidth && a.y + a.h <= kHudAtlasHeight;
                    for (std::size_t j = i + 1; j < all.size(); ++j)
                    {
                        const UiRect &b = all[j];
                        apart = apart && (a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
                    }
                }
                check(apart, "the 640 set's pieces each have their own place in the atlas");
                bool digits = true;
                for (int n = 0; n < 10; ++n)
                    digits = digits && solid(hud640Digit(n)) > 10;
                check(digits && solid(hud640Digit(1)) < solid(hud640Digit(8)),
                      "its digits, 0.4 of the game's 20 by 24, are there");
                bool ammo = true, hurts = true, marks = true;
                for (int n = 0; n < kHudAmmo640Count; ++n)
                    ammo = ammo && solid(hud640Ammo(n)) > 8;
                for (int n = 0; n < 8; ++n)
                    hurts = hurts && solid(hud640Hurt(n)) > 40;
                for (const UiRect &r : kHud640Pain)
                    marks = marks && solid(r) > 40;
                check(ammo && hurts && marks && solid(kHud640Cross) > 20 &&
                          solid(kHud640SuitFull) > solid(kHud640SuitEmpty) && solid(kHud640LampFull) > 10 &&
                          solid(kHud640Crosshair) > 4 && solid(kHud640Solid) == 64,
                      "... and each kind of ammunition, of hurt, the marks of where from, the suit, the lamp, "
                      "the crosshair");
            }
        }
    }

    /// The spray logos: each picture's brightness its four levels, in order,
    /// with its name; nothing the console cannot trust is opened.
    void testLogos()
    {
        std::vector<NamedPicture> pictures(3);
        for (int n = 0; n < 3; ++n)
        {
            RgbaImage &p = pictures[static_cast<std::size_t>(n)].picture;
            p.width = n == 2 ? 50 : 64; // (one of the player's own, of its own size)
            p.height = 64;
            p.rgba.assign(static_cast<std::size_t>(p.width) * p.height * 4, 0);
            for (int y = 0; y < p.height; ++y)
            {
                for (int x = 0; x < p.width; ++x)
                {
                    // 0: its left half white; 1: grey all over; 2: black.
                    const std::uint8_t v = n == 0 ? (x < p.width / 2 ? 255 : 0) : (n == 1 ? 120 : 0);
                    std::uint8_t *c = p.at(x, y);
                    c[0] = c[1] = c[2] = v;
                    c[3] = 255;
                }
            }
        }
        pictures[0].name = "lambda";
        pictures[1].name = "a_name_far_too_long_for_it";
        pictures[2].name = "black";
        const std::vector<std::uint8_t> cooked = logoAtlas(pictures);
        std::vector<std::uint32_t> aligned((cooked.size() + 3) / 4);
        std::memcpy(aligned.data(), cooked.data(), cooked.size());
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(aligned.data());
        LogosView view;
        check(view.open(bytes, cooked.size()) && view.count() == 3 && view.icons.width() == kLogoSize,
              "the spray logos open, three of them, each the logos' size");
        check(view.level(0, 0, 5) == 3 && view.level(0, kLogoSize - 1, 5) == 0 && view.level(1, 10, 10) == 2 &&
                  view.level(2, 10, 10) == 0,
              "... each as bright as its picture was: white solid, grey half, black nothing");
        check(std::strcmp(view.name(0), "lambda") == 0 && std::strlen(view.name(1)) == kLogoNameBytes - 1 &&
                  std::strcmp(view.name(3), "") == 0,
              "... named, a long name cut to fit with its end, and one there is not none");
        check(!view.open(bytes, cooked.size() - 1), "a logos file short of its names is not opened");
        std::vector<std::uint32_t> spoilt = aligned;
        auto *b = reinterpret_cast<std::uint8_t *>(spoilt.data());
        std::memset(b + cooked.size() - kLogoNameBytes, 'x', kLogoNameBytes);
        check(!view.open(b, cooked.size()) && view.count() == 0, "nor one with a name that never ends");
        IconsView icons;
        check(!icons.open(bytes, cooked.size()), "nor is it taken for the HUD's icons, which are smaller");
    }

    /// A slider slid: heard once where it rests, not at each step; let go
    /// before it rests, heard then; let go where it was heard, not again.
    void testSettle()
    {
        UiSettle s;
        std::uint32_t blank = 100;
        s.start(40, blank);
        int heard = 0;
        // Swept from 40 to 80, a step a blank: nothing on the way.
        for (int v = 41; v <= 80; ++v)
            heard += s.step(v, ++blank) ? 1 : 0;
        check(heard == 0, "a slider swept across is not heard at each step");
        for (std::uint32_t n = 0; n < kUiSettle + 2; ++n)
            heard += s.step(80, ++blank) ? 1 : 0;
        check(heard == 1 && !s.lift(), "... heard once where it rests, and not again when let go");
        for (int v = 79; v >= 60; --v)
            s.step(v, ++blank);
        check(s.lift() && !s.lift(), "let go while still moving: heard once, then");
        s.start(10, ++blank);
        check(!s.step(10, blank + 50) && !s.lift(), "touched and let go where it was: nothing more");
    }
} // namespace

int main()
{
    testSettle();
    testFont();
    testCanvas();
    testPlain();
    testTopLayout();
    testSettings();
    testSettingsFromV021();
    testGraphics();
    testLayout();
    testLamp();
    testLampPatch();
    testLampGrid();
    testLampGridRandom();
    testLampPull();
    testLampStream();
    testLogos();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
        testGame(dir);
    else
        std::printf("test_ui: HLDS_VALVE_DIR not set, the game's own were not read\n");
    if (g_failures == 0)
        std::printf("test_ui: ok\n");
    return g_failures == 0 ? 0 : 1;
}
