#pragma once

// What is cooked once for the card, not for a map (shared/HlUi.hpp): the
// game's fonts, the pictures of its head-up display, its loading picture.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "HlUi.hpp"
#include "hlds/SpriteCook.hpp"
#include "hlds/TextureImage.hpp"
#include "hlds/WadFile.hpp"

namespace hlds
{
    /// One lump of a WAD by its name (any case), whatever it is.
    bool readWadLump(const std::filesystem::path &wad, const std::string &name, std::vector<std::uint8_t> &out,
                     std::string &error);

    /// A font as the game keeps one: a picture 256 wide, and for each of 256
    /// characters where in it its glyph starts and how wide it is.
    struct QFont
    {
        int rowHeight = 0;
        int imageHeight = 0;
        struct Glyph
        {
            std::uint16_t start = 0; ///< y * 256 + x
            std::uint16_t width = 0;
        } glyphs[256];
        std::vector<std::uint8_t> pixels;  ///< 256 * imageHeight indices
        std::vector<std::uint8_t> palette; ///< 256 RGB triples
    };
    bool parseQFont(const std::vector<std::uint8_t> &lump, QFont &out, std::string &error);

    /// The file the console reads (HlUi.hpp: FontHeader and what follows),
    /// for characters 32 to 127. `outlined`: the game's menu fonts, which are
    /// a light letter with a dark edge; otherwise how bright a pixel is is
    /// how solid.
    std::vector<std::uint8_t> cookFont(const QFont &font, bool outlined, bool accented = true);

    /// Whether `bytes` are a font the console takes whole: it opens as the
    /// console opens one (HlUi.hpp, FontView), and is exactly as long as
    /// its header says -- nothing cut off, nothing after.
    bool fontWhole(const std::vector<std::uint8_t> &bytes);

    /// A plain picture lump (the game's "qpic").
    bool parseQPic(const std::vector<std::uint8_t> &lump, IndexedImage &out, std::string &error);

    /// The head-up display's texture, laid out as HlUi.hpp says, from the
    /// game's 320-wide set (sprites/320hud2.spr and crosshairs.spr).
    RgbaImage hudAtlas(const SprFile &hud, const SprFile &crosshairs);

    /// ... and the one of the 640-wide set (HlUi.hpp: kHud640File), each
    /// piece averaged down to 0.4 of its size: `hud7` .. `hud9` are
    /// sprites/640hud7..9.spr, `pain` 640_pain.spr.
    RgbaImage hudAtlas640(const SprFile &hud7, const SprFile &hud8, const SprFile &hud9, const SprFile &pain,
                          const SprFile &crosshairs);

    /// The pictures of how a train the player drives is going (HlUi.hpp:
    /// kHudTrainFile), from sprites/640_train.spr.
    RgbaImage hudTrainPictures(const SprFile &train);

    /// The weapons' pictures for the touch screen's slots (HlUi.hpp:
    /// IconsHeader and what follows), from the game's 320-wide set
    /// (sprites/320hud1.spr): what each picture fills of its box, fitted
    /// into `width` by `height` by averaging (never enlarged), how bright
    /// it was becoming how solid it is, in four steps.
    std::vector<std::uint8_t> weaponIcons(const SprFile &hud, int width = hldata::kWeaponIconWidth,
                                          int height = hldata::kWeaponIconHeight);

    /// A picture and what it is called.
    struct NamedPicture
    {
        std::string name;
        RgbaImage picture;
    };
    /// The spray logos (HlUi.hpp: kLogosFile, LogosView), of `pictures` in
    /// their order (no more than kLogosMost; names cut to fit): each made
    /// kLogoSize square by averaging, how bright it is becoming how solid.
    std::vector<std::uint8_t> logoAtlas(const std::vector<NamedPicture> &pictures);

    /// The game's title for the top screen (HlUi.hpp: kTitleFile): the two
    /// halves of it in the 320-wide set (sprites/320hud3.spr), side by
    /// side, as one picture of the icons' form.
    std::vector<std::uint8_t> titlePicture(const SprFile &hud);

    /// The marks' texture (HlUi.hpp: kDecalAtlasFile), from the game's
    /// decals.wad: each picture made the size of its cell, how dark the
    /// game has a pixel becoming how solid it is. `missing` is how many
    /// the WAD has not got.
    RgbaImage decalAtlas(const WadFile &decals, int &missing);

    /// The loading picture as a texture: scaled to the screen, in the top
    /// rows of a square.
    RgbaImage loadingTexture(const IndexedImage &picture);

    /// A TGA picture (uncompressed or run-length, 24 or 32 bits). False --
    /// with `error` -- for anything else.
    bool readTga(const std::vector<std::uint8_t> &bytes, RgbaImage &out, std::string &error);

    /// The game's menu background as a texture like the loading picture's:
    /// the HD layout (resource/HD_BackgroundLayout.txt) put together from
    /// its tiles, a 4:3 piece of it around the lambda, scaled to the screen;
    /// failing that, the 800x600 layout's, whole. False if neither is there.
    /// (`find`: a path of the game folder's, found whatever its case; empty
    /// if it is not there.)
    bool menuBackground(const std::function<std::filesystem::path(const std::string &)> &find, RgbaImage &out,
                        std::string &error);

    /// The animated title as the console has it (HlUi.hpp, kLogoFile):
    /// `frames` scaled to kLogoWidth x kLogoHeight, how bright each texel is
    /// as its alpha and its grey.
    std::vector<std::uint8_t> logoFile(const std::vector<RgbaImage> &frames, std::uint32_t frameMicros);
} // namespace hlds
