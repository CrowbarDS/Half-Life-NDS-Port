#pragma once

// How big each of a map's textures may be, so that all of them fit.
//
// Two things on the DS are finite and shared by every texture of a map:
//
//  * TEXELS. 4x4 compressed data goes in a bank pair that holds 524,288
//    texels. World textures are kept to one pair so the other is free for
//    models and weapons.
//  * PALETTE. A compressed texture's palette offset is 14 bits counting pairs
//    of colours: 32,768 colours for everything, which is also what the 64 KB
//    palette bank holds.
//
// Every texture starts at its default size (TextureSize.hpp). If the map is
// over on texels, the texture that is doing the LEAST with its memory -- the
// fewest square units of wall per texel -- is halved along its longer side,
// and that repeats until the map fits. A texture covering one small sign gives
// way before one covering every corridor.
//
// The palette is then shared out by size: more texels, more colours, scaled
// back for the whole map if the total would not fit.
//
// A3I5 textures (the '{' textures of brushes drawn with holes) have a budget
// of their own, 8 bits a texel: the upper half of bank B.
// That half is the only texture memory 4x4 data cannot use, so it costs the
// opaque textures nothing; go past it and every byte comes out of theirs.

#include <string>
#include <vector>

#include "hlds/BrushInfo.hpp"
#include "hlds/BspFile.hpp"
#include "hlds/TextureSize.hpp"
#include "hlds/WadFile.hpp"

namespace hlds
{
    inline constexpr int kWorldTexelBudget = 480 * 1024;
    inline constexpr int kSinglePassTexelBudget = 720 * 1024;
    inline constexpr int kPaletteColourBudget = 30000;
    /// Of kSinglePassTexelBudget, what a console sent the map keeps free
    /// besides (guestTexturePlan): what is in video memory from one map
    /// to the next -- the marks, the lamp, the sprays -- and NEA's own.
    inline constexpr int kGuestVramMargin = 16 * 1024;
    /// (Of the 64 KB that half holds, twelve are there in every map: eight
    /// the head-up display's, four the marks shots and blood leave --
    /// HlUi.hpp.)
    inline constexpr int kUiTexelBudget = 12 * 1024;
    inline constexpr int kAlphaTexelBudget = 48 * 1024;
    /// Of that, what a map's sprites may have between them (they are A3I5
    /// too: HlSprite.hpp), and the colours kept for their palettes.
    inline constexpr int kSpriteTexelBudget = 24 * 1024;
    inline constexpr int kSpriteColourBudget = 32 * 32;

    /// Which form each texture is wanted in, by texture lump index. Both can be
    /// set: a '{' texture on a plain wall here and on a grate there.
    struct TextureNeeds
    {
        std::vector<char> opaque; ///< 4x4 compressed
        std::vector<char> alpha;  ///< A3I5
        std::vector<char> light;  ///< A3I5 cooked as light (kTextureLight)
        /// Square units of drawn face wearing each form: what the budget pass
        /// weighs a texture's memory against.
        std::vector<double> opaqueArea;
        std::vector<double> alphaArea;
        std::vector<double> lightArea;
    };

    /// Plain opaque textures are sized for what one view of the map lists,
    /// everything else is allocated as before, within a share of the budget.
    struct StreamOptions
    {
        bool enabled = false;
        int residentPercent = 40; //how much of the budget resident textures get
        bool families = true;
    };

    /// `hidden` (findHiddenTextures) may be empty.
    TextureNeeds textureNeeds(const BspFile &bsp, const std::vector<BrushInfo> &brushes,
                              const std::vector<char> &hidden);

    struct TexturePlan
    {
        /// By texture lump index. Width 0 for a texture not wanted in that
        /// form.
        std::vector<TextureSize> sizes;      ///< 4x4 compressed
        std::vector<TextureSize> alphaSizes; ///< A3I5
        /// A3I5 cooked as light (kTextureLight), out of the A3I5 budget too.
        /// May be empty (a plan made by hand): none.
        std::vector<TextureSize> lightSizes;
        std::vector<int> paletteColours;     ///< of the 4x4 form; A3I5 is always 32
        int usedTextures = 0;  ///< forms, not names: a texture wanted both ways is two
        int texels = 0;
        int alphaTexels = 0;
        int colours = 0;
        int shrunk = 0;  ///< textures made smaller than their default
        bool fits = true; ///< false if even the smallest sizes are too much
        std::vector<char> streamed; ///< empty when not using streaming
        int streamedTextures = 0;
        int residentTexels = 0;   ///< of the 4x4 textures that are not streamed
        int regionTexels = 0;     ///< what the streamed ones may take of one view
        int worstViewTexels = 0;  ///< what the worst view lists of them
    };

    /// Textures that are nothing but holes: a colour-keyed ('{') texture whose
    /// every pixel is the key. Mappers use them for surfaces that must exist
    /// but not be seen -- the faces of a ladder brush, for one. A face wearing
    /// one can never show anything, so it is not drawn, which costs no
    /// polygons; and such a texture must never reach the encoder, which has
    /// nothing to encode. By texture lump index, kHiddenEverywhere.
    ///
    /// And textures dark all over, kHiddenAsLight: on a brush the game adds
    /// to what is behind it (BrushInfo::additive) they add nothing, so faces
    /// wearing them there are not drawn either.
    inline constexpr char kHiddenEverywhere = 1;
    inline constexpr char kHiddenAsLight = 2;
    std::vector<char> findHiddenTextures(const BspFile &bsp, const WadSet &wads);

    TexturePlan planTextures(const BspFile &bsp, const TextureNeeds &needs,
                             int texelBudget = kWorldTexelBudget,
                             int colourBudget = kPaletteColourBudget,
                             int alphaBudget = kAlphaTexelBudget,
                             const StreamOptions &stream = StreamOptions());

    /// The sizes a console sent the map has its textures at, from the host's
    /// `plan`: every one of them in video memory at once (nothing is
    /// streamed on it), in `texelBudget`. The largest are made smaller --
    /// a side halved at a time, never below kMinTextureSide, never one
    /// wider than the host's -- until they fit; the pictures of one cycle
    /// ('+' and '-' textures) all alike, as one surface shows each in
    /// turn; liquid left as it is. A3I5 textures are as the host has them.
    /// `fits` false: even that is not enough.
    TexturePlan guestTexturePlan(const BspFile &bsp, const TexturePlan &plan, int texelBudget);

    /// For a map taken as plain geometry -- every model opaque, no entities
    /// consulted. `hidden` marks textures to leave out (findHiddenTextures).
    TexturePlan planTextures(const BspFile &bsp, int texelBudget = kWorldTexelBudget,
                             int colourBudget = kPaletteColourBudget,
                             const std::vector<char> &hidden = {});
} // namespace hlds
