// The screens' own things (shared/HlUi.hpp, HlFont.hpp, hlds/UiCook.hpp):
// a font read as the game keeps one and cooked to what the console draws
// with; text measured, fitted to a line and drawn on a canvas of tiles; the
// canvas's rectangles. With HLDS_VALVE_DIR set, the game's own fonts and
// pictures.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "HlFont.hpp"
#include "HlLamp.hpp"
#include "HlSettings.hpp"
#include "HlTouchLayout.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/UiCook.hpp"

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
        check(view.width(200) == view.width(' ') && view.glyph(200) == nullptr,
              "a character it has not got moves the pen as a space does");
        check(!view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()), cooked.size() - 8),
              "a font file cut short is refused");
        view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()), cooked.size());

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

    /// The settings the menu changes: there and back, and nothing out of
    /// range believed.
    void testSettings()
    {
        SettingsData data;
        check(data.volume[kVolumeMusic] == 8 && data.volume[kVolumeEffects] == 10 && data.debugReadout == 0,
              "settings start with the music a little under the rest, and no readout");
        data.volume[kVolumeVoices] = 3;
        data.volume[kVolumeAmbience] = 0;
        data.debugReadout = 1;
        data.renderMode = 3;
        std::vector<std::uint8_t> bytes = writeSettings(data);
        SettingsData back;
        check(bytes.size() <= kSettingsMostBytes && readSettings(bytes.data(), bytes.size(), back) && back.volume[kVolumeVoices] == 3 &&
                  back.volume[kVolumeAmbience] == 0 && back.volume[kVolumeMusic] == 8 && back.debugReadout == 1 &&
                  back.renderMode == 3,
              "settings read back as they were written, the volumes and the readout among them");
        data.volume[kVolumeMusic] = 11;
        bytes = writeSettings(data);
        SettingsData untouched;
        check(!readSettings(bytes.data(), bytes.size(), untouched) && untouched.volume[kVolumeMusic] == 8,
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
        check(lampRadius(60) == kLampRadius && lampRadius(1500) == kLampRadius && kLampRadius == 80,
              "the lamp's light is eighty units across its half at any distance");
        check(lampStrength(60) == lampStrength(500) && lampStrength(1000) < lampStrength(500) &&
                  lampStrength(1000) * 3 <= lampStrength(500) && lampStrength(1900) >= 1 && lampStrength(kLampReach) == 0,
              "as bright at five hundred units as at sixty, a quarter of that at a thousand, nothing at its reach");
        check(lampOn(200, 0, 200) > lampOn(200, 40, 200) && lampOn(200, 40, 200) > 0 && lampOn(200, 80, 200) == 0 &&
                  lampOn(100, 0, 200) == 0 && lampOn(150, 0, 200) > 0,
              "on whoever stands in it: by how near its middle, and not at all outside it");
        // The beam: along +X from the origin, a hundred units long.
        LampBeam beam;
        beam.axis[0] = beam.u[1] = beam.v[2] = kFxOne;
        beam.radius = 30;
        beam.near = 0;
        beam.reach = 100;
        const std::int32_t inIt[3] = {50, 10, 0}, beside[3] = {50, 60, 0}, big[3] = {50, 60, 0}, behind[3] = {-40, 0, 0},
                           beyond[3] = {150, 0, 0};
        check(lampTouches(beam, inIt, 5) && !lampTouches(beam, beside, 5) && lampTouches(beam, big, 40) &&
                  !lampTouches(beam, behind, 10) && lampTouches(beam, behind, 50) && !lampTouches(beam, beyond, 10),
              "what is in the beam is what is beside its line within the two radii, not behind it or past its reach");
        // The projection: a vertex on the beam's line is the middle of the
        // picture, one a radius to the side is its edge -- wherever in the
        // map the lamp is (the hardware's sixteen bits go round, and so do
        // these).
        for (const int where : {0, 3000, -7000})
        {
            const fx from[3] = {fxFromInt(where), fxFromInt(where / 2), fxFromInt(40)};
            const LampProjection p = lampProjection(from, beam.u, beam.v, beam.radius, 32);
            auto coordinate = [&](int x, int y, int z, int which)
            {
                // (As the hardware does it: quarter units, times the
                // matrix, sixteen bits kept.)
                const std::int64_t vx = (where + x) * 4, vy = (where / 2 + y) * 4, vz = (40 + z) * 4;
                const std::int32_t *m = which == 0 ? p.s : p.t;
                const std::int64_t made = (vx * m[0] + vy * m[1] + vz * m[2]) >> 24;
                return static_cast<std::int16_t>(made + ((p.texCoord >> (which * 16)) & 0xFFFF));
            };
            const int midS = coordinate(77, 0, 0, 0), midT = coordinate(77, 0, 0, 1);
            const int edgeS = coordinate(77, 30, 0, 0), topT = coordinate(77, 0, -30, 1);
            check(midS >= 254 && midS <= 258 && midT >= 254 && midT <= 258 && edgeS >= 508 && edgeS <= 514 &&
                      topT >= -2 && topT <= 4,
                  "the projection puts the beam's line in the middle of the picture and its radius at the edge");
        }
        // A surface's commands for the light's pass: the vertices as they
        // were, colours white, texture coordinates the projection's.
        {
            // (colour, texture coordinate, a begin, a 16-bit vertex | then a
            // 10-bit vertex and nothing.)
            const std::uint32_t stream[] = {0x23402220u, 0x1234, 0x00050006, 1, 0x00100020, 0x0030,
                                            0x00000024u, 0x0ABCDEF0};
            std::vector<std::uint32_t> out;
            lampStream(stream, 8, 0xAAAA5555u, [&](std::uint32_t word) { out.push_back(word); });
            check(out.size() == 8 && out[0] == stream[0] && out[1] == 0x7FFF && out[2] == 0xAAAA5555u && out[3] == 1 &&
                      out[4] == 0x00100020 && out[5] == 0x0030 && out[6] == stream[6] && out[7] == 0x0ABCDEF0,
                  "the light's pass sends a surface's vertices as they are, white, with the projection's coordinate");
        }
        check(lampOn(100, 0, 100) > lampOn(1200, 0, 1200) && lampOn(100, 0, 100) > lampOn(100, 30, 100) &&
                  lampOn(100, 200, 100) == 0 && lampOn(-10, 0, 100) == 0 && lampOn(kLampReach + 1, 0, kLampReach + 1) == 0 &&
                  lampOn(60, 0, 60) <= 31,
              "what the view meets is lit where it stands: less far off, less away from the light's middle");
    }

    /// The player's say about the picture (HlFog.hpp): kept, checked, and
    /// what each choice stands for.
    void testGraphics()
    {
        SettingsData data;
        check(data.graphics.valid() && data.graphics.fog == kFogWhenNeeded && data.graphics.fogStrength == 7 &&
                  data.graphics.fogColour == 0 && data.graphics.modelDetail == 1 && data.graphics.worldDetail == 1 &&
                  data.graphics.modelLimit == 0 && data.graphics.drawDistance == 0,
              "to begin with: fog only where a view is cut, not quite solid, black; detail as it was");
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
        check(fogAlwaysEnd(0, 0) == 768 && fogAlwaysEnd(10, 0) == 6144 && fogAlwaysEnd(5, 0) > 3000 &&
                  fogAlwaysEnd(10, 2) == drawDistanceUnits(2) && fogAlwaysEnd(0, 2) == 768,
              "a fog that is always there is from 768 to 6144 units off, and never past what is drawn");
        check(fogCutEnd(1600, 0) == 1400 && fogCutEnd(1600, 10) == 1600 && fogCutEnd(1600, 5) == 1500 &&
                  fogCutEnd(100, 5) == 192,
              "one that hides a cut is whole from seven eighths of the way to it to all the way, never nearer than 192");
        check(drawDistanceUnits(0) == 8192 && drawDistanceUnits(1) == 3072 && drawDistanceUnits(2) == 1536,
              "the world is drawn far, less far, or near");
        check(modelDetailWeight(1) == 16 && modelDetailWeight(0) * 2 == 16 && modelDetailWeight(2) == 32,
              "models coarsen where they did, at half the distance, or at twice");
        bool never = true, sooner = true;
        for (int p = 0; p < 4; ++p)
        {
            never = never && worldDetailRatio(2, p) == 0;
            sooner = sooner && worldDetailRatio(0, p) != 0 &&
                     (worldDetailRatio(1, p) == 0 || worldDetailRatio(0, p) < worldDetailRatio(1, p));
        }
        check(never && sooner && worldDetailRatio(1, 0) == 0 && worldDetailRatio(1, 3) == 20,
              "small far things are left out under load as they were, sooner, or never");
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
            return all;
        };
        bool whole = true, apart = true, hits = true;
        for (const TouchLayout &at : {right, left})
        {
            const std::vector<TouchRect> all = rects(at);
            int counts[kTouchSlot + kTouchSlots] = {};
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
                  left.reload.x == 0 && right.reload.x + right.reload.w == 256 && left.use.x == right.use.x &&
                  right.use.x > right.menu.x + right.menu.w && right.use.x + right.use.w < right.reload.x &&
                  right.use.w > right.menu.w && left.pad.w == right.pad.w && left.duck.y == right.duck.y &&
                  right.jump.y < right.lamp.y && right.lamp.y < right.duck.y && right.lamp.x == right.jump.x &&
                  left.lamp.x == left.jump.x,
              "for the left hand the column and the bar's ends change sides; USE is in the middle either way");
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
    testSettings();
    testGraphics();
    testLayout();
    testLamp();
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
