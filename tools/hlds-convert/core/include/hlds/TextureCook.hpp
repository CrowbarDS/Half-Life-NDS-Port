#pragma once

// One map's textures to .htx files on the card.
//
// For each texture a drawn face uses, in each form it is wanted: find its pixels (in the map, or in one
// of the map's WADs), bring them to the planned size, encode, write. A texture
// already on the card from an earlier cook -- same pixels, same size, same
// settings, which the header's hash records -- is left alone, so cooking the
// whole game encodes each texture once however many maps share it.

#include <filesystem>
#include <string>
#include <vector>

#include "HlTexture.hpp"
#include "hlds/BspFile.hpp"
#include "hlds/TextureEncoder.hpp"
#include "hlds/TextureBudget.hpp"
#include "hlds/WadFile.hpp"

namespace hlds
{
    struct TextureCookReport
    {
        int encoded = 0;
        int reused = 0;
        std::vector<std::string> missing; ///< textures found nowhere
    };

    /// The bytes of a .htx file.
    std::vector<std::uint8_t> packTexture(const CookedTexture &texture, std::uint32_t flags,
                                          std::uint32_t sourceHash);
    /// The reverse, with the header checked.
    bool unpackTexture(const std::vector<std::uint8_t> &bytes, CookedTexture &texture,
                       hldata::TextureHeader &header);

    /// A missing texture is reported, not fatal: the DS draws that surface in
    /// its light alone. An encoder or write failure is fatal.
    bool cookTextures(const BspFile &bsp, const WadSet &wads, const TexturePlan &plan,
                      ITextureEncoder &encoder, const std::filesystem::path &texDir,
                      TextureCookReport &report, std::string &error);
    /// The six sides of a sky box, in the order they are cooked and drawn:
    /// the endings of their file names in the game's gfx/env folder.
    inline constexpr const char *kSkySides[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
    inline constexpr int kSkySize = hldata::kSkySide; ///< each side, as cooked

    /// Whether all six pictures of the sky `name` are in `gameDir`, each
    /// a .bmp or a .tga.
    bool skyExists(const std::filesystem::path &gameDir, const std::string &name);

    /// Cook the six sides of `name` into `texDir`, as <name><side>_128x128.htx.
    bool cookSky(const std::filesystem::path &gameDir, const std::string &name,
                 ITextureEncoder &encoder, const std::filesystem::path &texDir, std::string &error);
} // namespace hlds
