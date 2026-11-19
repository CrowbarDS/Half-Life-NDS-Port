#pragma once

// Texture pixels between the game's format and the encoder's.
//
// A GoldSrc texture is 8-bit indices and a 256-colour palette, any size that is
// a multiple of 16. The DS wants a power of two, usually a smaller one, so
// every texture goes through true colour and a resample on its way.
//
// COLOUR KEY. A texture whose name starts with '{' (grates, ladders, fences)
// has holes: palette index 255 is not a colour but "nothing here". That
// becomes alpha, and stays a hard edge through the resample -- a texel is
// either there or not, as it will be on the DS.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hlds/WadFile.hpp"

namespace hlds
{
    struct RgbaImage
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> rgba; ///< 4 bytes a pixel

        const std::uint8_t *at(int x, int y) const { return &rgba[(y * width + x) * 4]; }
        std::uint8_t *at(int x, int y) { return &rgba[(y * width + x) * 4]; }
    };

    RgbaImage toRgba(const IndexedImage &image, bool colourKey);

    /// The game shows its pictures lighter than they are in its files: each
    /// colour to the power 0.8 (its "texture gamma" of 2 on a screen of
    /// 2.5, the settings it ships with).
    inline constexpr double kTextureGamma = 0.8;
    std::uint8_t brightened(std::uint8_t value);
    void brighten(RgbaImage &image);

    /// Resize by area averaging. Colour is averaged over the opaque source
    /// pixels only, so a hole's key colour never bleeds into its edge.
    ///
    /// Alpha comes out one of two ways. HARD (the default, for 4x4 textures,
    /// which can only say "there" or "not there"): opaque where at least half
    /// the area was. SOFT (for A3I5): the share of the area that was opaque,
    /// so that the edge of a hole shrunk to a quarter of its size is a
    /// gradient rather than a staircase.
    RgbaImage resample(const RgbaImage &image, int width, int height, bool softAlpha = false);

    /// A picture the game ADDS to what is behind it, made into one the DS
    /// can blend: each texel's alpha its brightest channel, its colour raised
    /// so that that channel is full. Blended over black it is what the game
    /// adds; black itself is nothing at all, as it is in the game.
    void lightToAlpha(RgbaImage &image);

    /// A minimal PNG writer (8-bit RGBA, no compression), enough to hand an
    /// image to the encoder and to look at one.
    /// An 8-bit, uncompressed Windows bitmap (the game's skies, its spray
    /// logos), solid.
    bool readBmp8(const std::filesystem::path &path, RgbaImage &out, std::string &error);

    bool writePng(const std::filesystem::path &path, const RgbaImage &image, std::string &error);
} // namespace hlds
