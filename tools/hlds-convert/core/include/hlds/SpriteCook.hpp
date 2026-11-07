#pragma once

// The game's sprite files (.spr), read; and a sprite cooked to the one
// texture the DS draws it from (shared/HlSprite.hpp).

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "HlLevel.hpp"
#include "hlds/TextureImage.hpp"
#include "hlds/WadFile.hpp"

namespace hlds
{
    // SprFile::format: how the game draws the sprite's texels.
    inline constexpr int kSprNormal = 0;     ///< as they are, solid
    inline constexpr int kSprAdditive = 1;   ///< added to what is behind: light
    inline constexpr int kSprIndexAlpha = 2; ///< the palette's last colour, as solid as the texel's index
    inline constexpr int kSprAlphaTest = 3;  ///< solid, but for index 255, which is a hole

    struct SprFrame
    {
        int width = 0, height = 0;
        /// Where its left and top edges are from the sprite's middle (right
        /// and up are positive: the top is usually half the height).
        int originX = 0, originY = 0;
        std::vector<std::uint8_t> pixels; ///< palette indices, row by row from the top
    };

    struct SprFile
    {
        int type = 0;   ///< the game's orientation types, 0..4
        int format = 0; ///< kSpr*
        int width = 0, height = 0; ///< the box every frame fits in
        std::vector<std::array<std::uint8_t, 3>> palette;
        std::vector<SprFrame> frames;

        bool load(const std::filesystem::path &path, std::string &error);
        bool parse(const std::vector<std::uint8_t> &bytes, std::string &error);
    };

    struct CookedSprite
    {
        hldata::SpriteRef ref{}; ///< all but its name
        RgbaImage atlas;
    };

    /// Cook `spr`: frames no longer than `maxCell` texels a side (a power of
    /// two each way), at most `maxFrames` of them (evenly taken, first and
    /// last kept), side by side in an atlas no wider than 128.
    /// `light`: drawn as light whatever the file says (a beam's picture).
    bool cookSprite(const SprFile &spr, int maxCell, int maxFrames, bool light, CookedSprite &out,
                    std::string &error);

    /// One frame as the DS will blend it: colour, and how solid.
    RgbaImage spriteFrameImage(const SprFile &spr, int frame, bool light);

    /// The name a sprite's files go by: its file's, lower-case, no folder or
    /// extension, cut to what a SpriteRef holds ("sprites/Flare1.spr" ->
    /// "flare1").
    std::string spriteName(const std::string &path);

    /// One of decals.wad's pictures as a sprite of one frame: its colour
    /// the palette's last, as solid as each texel's index (the game's way
    /// with a decal), lying as its wall does. False for an empty picture.
    bool signSprite(const IndexedImage &picture, SprFile &out);
    /// A sprite's name as spriteName has it -- or, for a sign
    /// (hldata::kDecalSignPath), "d-" and the picture's name with only
    /// letters, digits and '-' ("decals/{med#s1.spr" -> "d-meds1").
    std::string signSpriteName(const std::string &path);
} // namespace hlds
