#pragma once

// What the screens are drawn with that is not a map's: the fonts, the
// pictures of the head-up display, the picture shown while a map loads, and
// the sounds of the menu. The converter cooks them once for the card, from
// the game's own (ui/ and snd/ under the data folder); the console loads them
// by these names. None of it is in the repository or the ROM: a card without
// them shows the built-in 8x8 letters and no pictures.
//
// std-only and inline: the host tests run it.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hldata
{
    // ------------------------------------------------------------- fonts
    //
    // A font file: a FontHeader, `count` widths (a byte each, padded to a
    // multiple of four), then for each glyph `height` rows of 32 bits: two
    // bits a pixel, the leftmost in the lowest bits. 0 is nothing; in an
    // "outlined" font 1 is the dark edge the game draws round its letters
    // and 3 the letter; in any other 1 to 3 are how solid the letter is.

    inline constexpr char kFontMagic[4] = {'H', 'L', 'F', 'N'};
    inline constexpr std::uint16_t kFontVersion = 1;
    inline constexpr int kFontMaxWidth = 16;
    inline constexpr int kFontMaxHeight = 24;

    struct FontHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint8_t height;
        std::uint8_t first;    ///< the character its first glyph is
        std::uint8_t count;
        std::uint8_t outlined;
        std::uint8_t pad[2];
    };
    static_assert(sizeof(FontHeader) == 12);

    inline constexpr int kFontSmall = 0;  ///< the game's 14-pixel letters
    inline constexpr int kFontMedium = 1; ///< ... its 16-pixel ones
    inline constexpr int kFontTitle = 2;  ///< ... and those of its credits
    inline constexpr int kFontConsole = 3; ///< ... and its console's, 12 rows: what the map writes, whole
    inline constexpr int kFontCount = 4;
    inline constexpr const char *const kFontFiles[kFontCount] = {"ui/font0.hfn", "ui/font1.hfn", "ui/font2.hfn",
                                                                 "ui/font3.hfn"};

    /// What a character of 128..255 (Windows-1252, the game's text) is drawn
    /// as in a font that has not got it: its letter without the accent, the
    /// nearest plain mark, or a space.
    inline constexpr char kBaseLetters[129] =
        "E ,f\".++^%S<O Z " // 80..8F
        " ''\"\"*--~Ts>o zY" // 90..9F
        " !cL*Y|S\"Ca\"--R-" // A0..AF
        "o+23'uP.,1o\"   ?" // B0..BF
        "AAAAAAACEEEEIIII" // C0..CF
        "DNOOOOOxOUUUUYPs" // D0..DF
        "aaaaaaaceeeeiiii" // E0..EF
        "dnooooo/ouuuuypy"; // F0..FF
    inline constexpr unsigned char baseLetter(unsigned char c)
    {
        return c < 128 ? c : static_cast<unsigned char>(kBaseLetters[c - 128]);
    }

    struct FontView
    {
        const FontHeader *header = nullptr;
        const std::uint8_t *widths = nullptr;
        const std::uint32_t *rows = nullptr;

        /// `data` must be aligned for 32-bit words and outlive the view.
        bool open(const std::uint8_t *data, std::size_t size)
        {
            header = nullptr;
            if (data == nullptr || size < sizeof(FontHeader))
                return false;
            const auto *h = reinterpret_cast<const FontHeader *>(data);
            if (std::memcmp(h->magic, kFontMagic, 4) != 0 || h->version != kFontVersion || h->height == 0 ||
                h->height > kFontMaxHeight || h->count == 0 ||
                static_cast<unsigned>(h->first) + h->count > 256)
                return false;
            const std::size_t widthBytes = (static_cast<std::size_t>(h->count) + 3) & ~std::size_t(3);
            if (size < sizeof(FontHeader) + widthBytes + static_cast<std::size_t>(h->count) * h->height * 4)
                return false;
            widths = data + sizeof(FontHeader);
            for (int i = 0; i < h->count; ++i)
            {
                if (widths[i] > kFontMaxWidth)
                    return false;
            }
            rows = reinterpret_cast<const std::uint32_t *>(data + sizeof(FontHeader) + widthBytes);
            header = h;
            return true;
        }
        bool ok() const { return header != nullptr; }
        int height() const { return header != nullptr ? header->height : 0; }
        bool has(unsigned char c) const
        {
            return header != nullptr && c >= header->first && c < header->first + header->count;
        }
        /// The character drawn for `c`: itself, or one of 128..255 that the
        /// font has no glyph for, its base letter (é as e).
        unsigned char drawn(unsigned char c) const
        {
            if (c >= 128 && !(has(c) && widths[c - header->first] != 0))
                return baseLetter(c);
            return c;
        }
        /// How far a character moves the pen (one it has not got, nor its
        /// base letter: as a space).
        int width(unsigned char c) const
        {
            if (header == nullptr)
                return 0;
            c = drawn(c);
            if (!has(c))
                return has(' ') ? widths[' ' - header->first] : 0;
            return widths[c - header->first];
        }
        const std::uint32_t *glyph(unsigned char c) const
        {
            if (header == nullptr)
                return nullptr;
            c = drawn(c);
            return has(c) ? rows + static_cast<std::size_t>(c - header->first) * header->height : nullptr;
        }
    };

    // ------------------------------------------------------------- the head-up display
    //
    // One small texture (A3I5: how bright the game's picture was is how
    // solid it is here, and it is drawn in the display's own colour), laid
    // out by the converter exactly so:

    struct UiRect
    {
        std::uint8_t x, y, w, h;
    };
    inline constexpr int kHudAtlasWidth = 128;
    inline constexpr int kHudAtlasHeight = 128;
    inline constexpr const char *kHudAtlasFile = "ui/hud.128x128.htx";
    inline constexpr int kHudDigitWidth = 12;
    inline constexpr int kHudDigitHeight = 16;
    inline constexpr UiRect hudDigit(int n)
    {
        return UiRect{static_cast<std::uint8_t>(12 * n), 0, 12, 16};
    }
    inline constexpr UiRect kHudCross = {0, 16, 16, 16};
    inline constexpr UiRect kHudSuitFull = {16, 16, 20, 20};
    inline constexpr UiRect kHudSuitEmpty = {36, 16, 20, 20};
    inline constexpr UiRect kHudAmmo9mm = {56, 16, 18, 18};
    inline constexpr UiRect kHudAmmo357 = {74, 16, 18, 18};
    inline constexpr UiRect kHudAmmoBuck = {92, 16, 18, 18};
    inline constexpr UiRect kHudAmmoGrenade = {110, 16, 18, 18};
    inline constexpr UiRect kHudAmmoAr = {0, 36, 18, 18};
    inline constexpr UiRect kHudCrosshair = {18, 36, 24, 24};
    inline constexpr UiRect kHudLampEmpty = {70, 38, 18, 16};
    inline constexpr UiRect kHudLampFull = {88, 38, 18, 16};
    inline constexpr UiRect kHudLampBeam = {106, 38, 6, 16};
    // (The later weapons' ammunition: a row of its own.)
    inline constexpr UiRect kHudAmmoBolt = {0, 64, 18, 18};
    inline constexpr UiRect kHudAmmoSatchel = {18, 64, 18, 18};
    inline constexpr UiRect kHudAmmoTripmine = {36, 64, 18, 18};
    inline constexpr UiRect kHudAmmoSnark = {54, 64, 18, 18};
    inline constexpr UiRect kHudAmmoRocket = {72, 64, 18, 18};
    inline constexpr UiRect kHudAmmoUranium = {90, 64, 18, 18};
    inline constexpr UiRect kHudAmmoHornet = {108, 64, 18, 18};

    // ... and as the game draws it at 640 by 480: its 640 set (sprites/
    // 640hud7..9, 640_pain, crosshairs), each piece made 0.4 of its size --
    // what 640 by 480 is on 256 by 192 -- in a texture of its own. A card
    // cooked before it has only the one above, and shows that.
    inline constexpr const char *kHud640File = "ui/hud640.128x128.htx";
    inline constexpr int kHud640Scale10 = 4; ///< tenths: the shrink
    inline constexpr int kHud640DigitWidth = 8;
    inline constexpr int kHud640DigitHeight = 10;
    inline constexpr UiRect hud640Digit(int n)
    {
        return UiRect{static_cast<std::uint8_t>(8 * n), 103, 8, 10};
    }
    /// The pictures of the kinds of hurt (kHurt*, in their order), 26 square.
    inline constexpr UiRect hud640Hurt(int kind)
    {
        return UiRect{static_cast<std::uint8_t>(26 * (kind & 3)), static_cast<std::uint8_t>(26 * (kind >> 2)), 26, 26};
    }
    /// The marks of where a hurt came from: front, right, back, left.
    inline constexpr UiRect kHud640Pain[4] = {{19, 52, 51, 19}, {0, 52, 19, 51}, {19, 71, 51, 19}, {104, 0, 19, 51}};
    inline constexpr UiRect kHud640SuitFull = {70, 52, 16, 16};
    inline constexpr UiRect kHud640SuitEmpty = {86, 52, 16, 16};
    inline constexpr UiRect kHud640Cross = {102, 52, 13, 13};
    inline constexpr UiRect kHud640LampEmpty = {115, 52, 13, 13};
    inline constexpr UiRect kHud640LampFull = {70, 68, 13, 13};
    inline constexpr UiRect kHud640LampBeam = {83, 68, 6, 13};
    inline constexpr UiRect kHud640Crosshair = {89, 68, 10, 10};
    inline constexpr UiRect kHud640Solid = {99, 68, 8, 8}; ///< all bright: the bars
    /// Each kind of ammunition's picture, 10 square, in the order of
    /// kHudAmmo640*.
    enum HudAmmo640 : int
    {
        kHudAmmo640_9mm,
        kHudAmmo640_357,
        kHudAmmo640ArGrenade,
        kHudAmmo640Buckshot,
        kHudAmmo640Bolt,
        kHudAmmo640Rocket,
        kHudAmmo640Uranium,
        kHudAmmo640Hornet,
        kHudAmmo640Grenade,
        kHudAmmo640Satchel,
        kHudAmmo640Snark,
        kHudAmmo640Tripmine,
        kHudAmmo640Count
    };
    inline constexpr UiRect hud640Ammo(int kind)
    {
        return UiRect{static_cast<std::uint8_t>(10 * kind), 113, 10, 10};
    }

    // ... and how a train the player drives is going (the game's
    // sprites/640_train.spr, in its frames' order: stopped, forward slow to
    // fast, back), each 0.4 of its 72 as the 640 set's are, in a texture of
    // their own that both sets use.
    inline constexpr const char *kHudTrainFile = "ui/train.256x32.htx";
    inline constexpr int kHudTrainWidth = 256;
    inline constexpr int kHudTrainHeight = 32;
    inline constexpr int kHudTrainSide = 29;
    inline constexpr UiRect hudTrain(int n)
    {
        return UiRect{static_cast<std::uint8_t>(kHudTrainSide * n), 0, kHudTrainSide, kHudTrainSide};
    }

    // ------------------------------------------------------------- marks
    //
    // What shots, blows, blood and blasts leave on a wall: the game's own
    // decals, sixteen to a cell, in one small texture (A3I5). The game's
    // pictures say how dark each pixel is: here that is how solid, and the
    // colour is whatever the mark is drawn in.

    inline constexpr int kDecalAtlasWidth = 256;
    inline constexpr int kDecalAtlasSize = 64; ///< its height
    inline constexpr int kDecalCell = 32;
    inline constexpr const char *kDecalAtlasFile = "ui/decals.256x64.htx";
    inline constexpr std::uint8_t kDecalShot = 0;    ///< ... and the three after: a bullet's hole
    inline constexpr std::uint8_t kDecalShots = 4;
    inline constexpr std::uint8_t kDecalDent = 4;    ///< a blow's
    inline constexpr std::uint8_t kDecalBlood = 5;   ///< ... and the two after
    inline constexpr std::uint8_t kDecalBloods = 3;
    inline constexpr std::uint8_t kDecalAlien = 8;   ///< an alien's blood (and 13)
    inline constexpr std::uint8_t kDecalCrack = 9;   ///< glass, shot and not yet broken
    inline constexpr std::uint8_t kDecalScorch = 10; ///< a blast's
    inline constexpr std::uint8_t kDecalSpit = 12;
    inline constexpr std::uint8_t kDecalAlien2 = 13;
    /// More of the game's, as the maps' own marks need them: a hand's
    /// print, two pools, two more splashes, and what a wall looks like
    /// cracked and dented.
    inline constexpr std::uint8_t kDecalHand = 16;
    inline constexpr std::uint8_t kDecalPool = 17;   ///< ... and the one after
    inline constexpr std::uint8_t kDecalSplash = 19; ///< {blood6
    inline constexpr std::uint8_t kDecalSplash2 = 20; ///< {blood4
    inline constexpr std::uint8_t kDecalWallCrack = 21;
    inline constexpr std::uint8_t kDecalWallDent = 22;
    inline constexpr std::uint8_t kDecalCount = 23;
    /// A sign the map put there (a number, an arrow, a letter, a stain of
    /// its own: any of decals.wad's pictures but those above), as one of
    /// the map's sprites: a Mark's extra[2] says which, and a Decal's
    /// `colour` holds it (the picture has its own colour).
    inline constexpr std::uint8_t kDecalSign = 0xFF;
    /// Where the converter asks for a sign's picture, as a sprite's path:
    /// this, then its name in decals.wad, then ".spr".
    inline constexpr const char *kDecalSignPath = "decals/";
    /// Where a mark's picture is in the texture: sixteen cells of 32, the
    /// first shared out among the five small ones (three holes -- the
    /// fourth hole is the first again -- and the dent).
    inline constexpr UiRect decalRect(std::uint8_t which)
    {
        constexpr std::int8_t kCellOf[kDecalCount] = {-1, -2, -3, -1, -4, 2,  3,  4, 5, 7, 8, -1,
                                                      1,  6,  -1, -1, 9,  10, 11, 12, 13, 14, 15};
        const int cell = which < kDecalCount ? kCellOf[which] : -1;
        if (cell < 0)
        {
            // (A quarter of the first cell.)
            const int quarter = -cell - 1;
            return UiRect{static_cast<std::uint8_t>((quarter & 1) * 16), static_cast<std::uint8_t>((quarter >> 1) * 16), 16,
                          16};
        }
        return UiRect{static_cast<std::uint8_t>((cell & 7) * kDecalCell), static_cast<std::uint8_t>((cell >> 3) * kDecalCell),
                      kDecalCell, kDecalCell};
    }
    /// The game's name for each (in its decals.wad), for the converter;
    /// null for those that have no picture of their own.
    inline constexpr const char *const kDecalSources[kDecalCount] = {
        "{shot1",  "{shot2",     "{shot3",     nullptr,      "{bigshot1", "{blood1", "{blood2", "{blood3",
        "{yblood1", "{break1",   "{scorch1",   nullptr,      "{spit1",    "{yblood2", nullptr,  nullptr,
        "{bloodhand2", "{bigblood1", "{bigblood2", "{blood6", "{blood4",  "{crack2", "{dent1"};

    // ------------------------------------------------------------- weapon icons
    //
    // The pictures the game has for its weapons, for the touch screen's
    // slots: an IconsHeader, then for each icon `height` rows of
    // iconRowWords(width) words, two bits a pixel as a font's glyphs are
    // (0 nothing, 3 as bright as the picture goes). Icon n is weapon n + 1,
    // as large as it goes in its cell and in the middle of it.

    inline constexpr char kIconsMagic[4] = {'H', 'L', 'I', 'C'};
    inline constexpr std::uint16_t kIconsVersion = 1;
    inline constexpr const char *kWeaponIconsFile = "ui/weapons.hic";
    inline constexpr int kWeaponIcons = 14;
    inline constexpr int kWeaponIconWidth = 38;
    inline constexpr int kWeaponIconHeight = 16;
    inline constexpr int kIconMaxWidth = 80;
    inline constexpr int kIconMaxHeight = 24;
    /// The game's title, as one picture of the same form ("HALF-LIFE", its
    /// two halves side by side).
    inline constexpr const char *kTitleFile = "ui/title.hic";
    inline constexpr int kTitleWidth = 244;
    inline constexpr int kTitleHeight = 16;

    struct IconsHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint8_t count;
        std::uint8_t width;
        std::uint8_t height;
        std::uint8_t pad[3];
    };
    static_assert(sizeof(IconsHeader) == 12);
    inline constexpr int iconRowWords(int width)
    {
        return (width * 2 + 31) / 32;
    }

    struct IconsView
    {
        const IconsHeader *header = nullptr;
        const std::uint32_t *rows = nullptr;

        /// `data` must be aligned for 32-bit words and outlive the view.
        /// `widest`, `tallest`: what the caller has room for.
        bool open(const std::uint8_t *data, std::size_t size, int widest = kIconMaxWidth,
                  int tallest = kIconMaxHeight)
        {
            header = nullptr;
            if (data == nullptr || size < sizeof(IconsHeader))
                return false;
            const auto *h = reinterpret_cast<const IconsHeader *>(data);
            if (std::memcmp(h->magic, kIconsMagic, 4) != 0 || h->version != kIconsVersion || h->count == 0 ||
                h->width == 0 || h->width > widest || h->height == 0 || h->height > tallest)
                return false;
            if (size < sizeof(IconsHeader) +
                           static_cast<std::size_t>(h->count) * h->height * iconRowWords(h->width) * 4)
                return false;
            rows = reinterpret_cast<const std::uint32_t *>(data + sizeof(IconsHeader));
            header = h;
            return true;
        }
        bool ok() const { return header != nullptr; }
        int count() const { return header != nullptr ? header->count : 0; }
        int width() const { return header != nullptr ? header->width : 0; }
        int height() const { return header != nullptr ? header->height : 0; }
        /// 0 to 3; 0 for anything outside.
        int level(int icon, int x, int y) const
        {
            if (icon < 0 || icon >= count() || x < 0 || y < 0 || x >= width() || y >= height())
                return 0;
            const std::uint32_t *row =
                rows + (static_cast<std::size_t>(icon) * header->height + y) * iconRowWords(header->width);
            return static_cast<int>((row[x >> 4] >> ((x & 15) * 2)) & 3);
        }
    };

    // ------------------------------------------------------------- spray logos
    //
    // The pictures a player may spray on a wall in a match: the game's
    // logos/*.bmp (the player's own folder, as on a PC: what is put there is
    // offered) and those of its spraypaint.wad that the folder has not, by
    // name. Icons of the same form as the weapons', kLogoSize square, how
    // bright the picture was becoming how solid the paint is (it is sprayed
    // in one colour, HlSettings.hpp's kSprayColour); then each one's name,
    // kLogoNameBytes with its end. The profile keeps which by its number.

    inline constexpr const char *kLogosFile = "ui/logos.hic";
    inline constexpr int kLogoSize = 32;
    inline constexpr std::size_t kLogoNameBytes = 16;
    inline constexpr int kLogosMost = 128;

    struct LogosView
    {
        IconsView icons;
        const char *names = nullptr;

        /// `data` must be aligned for 32-bit words and outlive the view.
        bool open(const std::uint8_t *data, std::size_t size)
        {
            names = nullptr;
            if (!icons.open(data, size, kLogoSize, kLogoSize) || icons.width() != kLogoSize ||
                icons.height() != kLogoSize || icons.count() > kLogosMost)
            {
                icons.header = nullptr;
                return false;
            }
            const std::size_t rows =
                sizeof(IconsHeader) + static_cast<std::size_t>(icons.count()) * kLogoSize * iconRowWords(kLogoSize) * 4;
            if (size < rows + static_cast<std::size_t>(icons.count()) * kLogoNameBytes)
            {
                icons.header = nullptr;
                return false;
            }
            const char *at = reinterpret_cast<const char *>(data + rows);
            for (int n = 0; n < icons.count(); ++n)
            {
                if (std::memchr(at + n * kLogoNameBytes, 0, kLogoNameBytes) == nullptr)
                {
                    icons.header = nullptr;
                    return false;
                }
            }
            names = at;
            return true;
        }
        int count() const { return names != nullptr ? icons.count() : 0; }
        /// 0 to 3; 0 for anything outside.
        int level(int logo, int x, int y) const { return names != nullptr ? icons.level(logo, x, y) : 0; }
        /// "" for one there is not.
        const char *name(int logo) const
        {
            return logo >= 0 && logo < count() ? names + static_cast<std::size_t>(logo) * kLogoNameBytes : "";
        }
    };

    // ------------------------------------------------------------- loading
    //
    // The game's own loading picture, in the top 192 rows of a texture.

    inline constexpr const char *kLoadingFile = "ui/loading.256x256.htx";
    inline constexpr int kLoadingSize = 256;
    inline constexpr int kLoadingHeight = 192;

    // ------------------------------------------------------------- the title
    //
    // The title screen as the game has it: its menu background (the HD
    // layout's, around the lambda) in the top 192 rows of a texture, as the
    // loading picture is; over it, the game's animated HALF-LIFE (logo.avi);
    // and before it all, at every start, the Valve intro (valve.avi).

    inline constexpr const char *kMenuFile = "ui/menu.256x256.htx";
    /// The player model as the others see it in a match, and its skin in 256
    /// colours (a player's own laid over its runs: HlRemap.hpp): what PROFILE
    /// turns on the top screen. In the common archive where a multiplayer
    /// map was cooked.
    inline constexpr const char *kProfileModelFile = "ply/player.hmd";
    inline constexpr const char *kProfileSkinFile = "ply/player.htx";
    /// ... and finer, for a DSi (one sent the game, in DSi mode, too).
    inline constexpr const char *kProfileFineModelFile = "ply/full.hmd";
    inline constexpr const char *kProfileFineSkinFile = "ply/full.htx";

    /// The animated title: `frames` pictures of `width` x `height` texels, a
    /// byte each, A3I5 -- how bright as both the alpha (top 3 bits) and the
    /// colour (a grey of 32: the low 5) -- one after the other after the
    /// header, each shown `frameMicros`.
    inline constexpr const char *kLogoFile = "ui/logo.hlo";
    inline constexpr char kLogoMagic[4] = {'H', 'L', 'L', 'G'};
    inline constexpr std::uint16_t kLogoVersion = 1;
    inline constexpr int kLogoWidth = 256;
    inline constexpr int kLogoHeight = 40;
    inline constexpr int kLogoTextureHeight = 64; ///< the texture it is drawn from
    struct LogoHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t frames;
        std::uint16_t width;
        std::uint16_t height;
        std::uint32_t frameMicros;
    };
    static_assert(sizeof(LogoHeader) == 16);

    /// The Valve intro, loose in the data folder (read as it plays, not
    /// kept): only the part of the game's film that is picture (its frame is
    /// mostly black), the size it is on the screen of a PC at 640x480 -- a
    /// 4x4 texture `textureSize` square, the picture in its top left
    /// `pictureWidth` x `pictureHeight`, shown at (`pictureX`, `pictureY`) of
    /// the screen, the rest black. Each picture: its texels, then its index,
    /// then its palette (`paletteColours`, the rest zero), in records of
    /// `frameBytes`, each shown `frameMicros`. Its sound is the music's track
    /// kIntroTrack.
    inline constexpr const char *kIntroFile = "intro.hvf";
    inline constexpr char kIntroMagic[4] = {'H', 'L', 'V', 'F'};
    inline constexpr std::uint16_t kIntroVersion = 2;
    inline constexpr int kIntroTrack = 1; ///< (the game's first track was its program: free)
    inline constexpr int kIntroTextureSize = 128;
    inline constexpr int kIntroPaletteColours = 256;
    inline constexpr int kIntroTexelBytes = kIntroTextureSize * kIntroTextureSize / 4;
    inline constexpr int kIntroIndexBytes = kIntroTexelBytes / 2;
    inline constexpr int kIntroFrameBytes = kIntroTexelBytes + kIntroIndexBytes + kIntroPaletteColours * 2;
    struct IntroHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t frames;
        std::uint32_t frameMicros;
        std::uint32_t frameBytes;
        std::uint16_t paletteColours;
        std::uint16_t textureSize;
        std::uint16_t pictureX, pictureY;
        std::uint16_t pictureWidth, pictureHeight;
        std::uint32_t sourceHash; ///< of the film it was made from (a cook again keeps it)
    };
    static_assert(sizeof(IntroHeader) == 32);

    // ------------------------------------------------------------- menu sounds

    inline constexpr int kUiSoundPress = 0;  ///< a button of the menu
    inline constexpr int kUiSoundTick = 1;   ///< a slider moved a notch
    inline constexpr int kUiSoundWeapon = 2; ///< a weapon chosen; something USEd (the game's wpn_select)
    inline constexpr int kUiSoundLamp = 3;   ///< the lamp's switch
    // The game's weapon menu (cl_dll/ammo.cpp): up, along, away; and a USE
    // with nothing to use (dlls/player.cpp's PlayerUse).
    inline constexpr int kUiSoundHudOn = 4;
    inline constexpr int kUiSoundMove = 5;
    inline constexpr int kUiSoundHudOff = 6;
    inline constexpr int kUiSoundDeny = 7;
    /// A spray put on a wall, in a match (the game's player/sprayer.wav).
    inline constexpr int kUiSoundSpray = 8;
    inline constexpr int kUiSoundCount = 9;
    /// Their files under snd/ on the card (without ".hsn"), and where each
    /// is in the game's sound folder.
    inline constexpr const char *const kUiSoundFiles[kUiSoundCount] = {
        "ui_press", "ui_tick", "ui_weapon", "ui_lamp", "ui_hudon", "ui_move", "ui_hudoff", "ui_deny", "ui_spray"};
    inline constexpr const char *const kUiSoundSources[kUiSoundCount] = {
        "common/launch_select2.wav", "common/launch_dnmenu1.wav", "common/wpn_select.wav",
        "items/flashlight1.wav",     "common/wpn_hudon.wav",      "common/wpn_moveselect.wav",
        "common/wpn_hudoff.wav",     "common/wpn_denyselect.wav", "player/sprayer.wav"};

    /// The words of a match's last minute ("sixty seconds remaining": the game's
    /// announcer has no "minute"), said one after another. On a card only, and
    /// read only for a match with a time limit: a console sent the game goes
    /// without, and shows the line alone.
    inline constexpr int kMatchWarningWords = 3;
    inline constexpr const char *const kMatchWarningFiles[kMatchWarningWords] = {"mp_warn0", "mp_warn1", "mp_warn2"};
    inline constexpr const char *const kMatchWarningSources[kMatchWarningWords] = {
        "vox/sixty.wav", "vox/seconds.wav", "vox/remaining.wav"};

    /// Blanks a value moved by the stylus rests before its sound is heard
    /// (a sixth of a second, as the weapon list's rows: HlWeaponList.hpp).
    inline constexpr std::uint32_t kUiSettle = 10;

    /// A value the stylus moves (a slider), heard as the weapon list's rows
    /// are: not for each step it passes, but once where it comes to rest --
    /// and, lifted before that, once then.
    struct UiSettle
    {
        int value = 0;  ///< where it is
        int heard = 0;  ///< where it was last heard
        std::uint32_t since = 0; ///< the blank it came to `value`

        /// The stylus down on it, at `now` (heard then, as a press).
        void start(int now, std::uint32_t blank)
        {
            value = heard = now;
            since = blank;
        }
        /// Each blank while it is held, at `now`: true once it has rested
        /// somewhere new for `settle` blanks.
        bool step(int now, std::uint32_t blank, std::uint32_t settle = kUiSettle)
        {
            if (now != value)
            {
                value = now;
                since = blank;
            }
            if (value == heard || blank - since < settle)
                return false;
            heard = value;
            return true;
        }
        /// Lifted: true if where it was left has not been heard.
        bool lift()
        {
            if (value == heard)
                return false;
            heard = value;
            return true;
        }
    };
} // namespace hldata
