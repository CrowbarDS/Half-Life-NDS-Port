#pragma once

// A cooked texture: <card>/_nds/hl_nds/data/tex/<name>_<w>x<h>[a].htx
//
// Stored exactly as the hardware samples it, in one of two formats.
//
// 4x4 COMPRESSED, for everything opaque. The richest format the DS has for its
// size -- 3 bits a texel with a palette that can run to thousands of colours --
// in the three pieces the hardware wants in three different places:
//
//   texels   2 bits a texel, one 32-bit word per 4x4 block   -> texture slot 0 or 2
//   index    one 16-bit word per block: palette offset+mode  -> texture slot 1
//   palette  RGB555 colours                                  -> texture palette
//
// A3I5, for textures with holes in them (grates, fences, railings: the names
// that start with '{'). One byte a texel: 5 bits of colour index into a
// 32-colour palette and 3 bits of ALPHA, so the edge of a hole can be soft
// where 4x4 compression only has "there" and "not there". It costs 8 bits a
// texel against 3, which is why only the textures that need it get it.
//
// THE SIZE AND THE FORMAT ARE PART OF THE NAME. How big a texture is cooked
// depends on the map (the budget pass shrinks textures in maps that have too
// many), and a '{' texture is wanted in both formats when one map uses it on a
// plain wall and another as a see-through grate. All of those must be able to
// sit in the one tex/ folder that all maps share.

#include <cstdint>
#include <cstring>

namespace hldata
{
    inline constexpr char kTextureMagic[4] = {'H', 'L', 'T', 'X'};
    inline constexpr std::uint16_t kTextureVersion = 3;

    // TextureHeader::flags, and TextureRef::flags in a level.
    inline constexpr std::uint32_t kTextureColourKey = 1; ///< cooked from a '{' texture
    inline constexpr std::uint32_t kTextureA3I5 = 2;      ///< A3I5, not 4x4 compressed
    /// A liquid's surface: its coordinates go through the texture matrix, so
    /// that the DS can make it sway.
    inline constexpr std::uint32_t kTextureLiquid = 4;
    /// One side of the map's sky box, not something on a surface.
    inline constexpr std::uint32_t kTextureSky = 8;
    /// A conveyor's belt: its coordinates go through the texture matrix
    /// too, which slides them along at the belt's speed.
    inline constexpr std::uint32_t kTextureScroll = 16;
    /// A streamed texture the console may keep out of video memory,
    /// plain and opaque, drawn as the texture's flat color until the texture has been read in.
    /// Everything else is resident in video memory for the lifespan of the map.
    inline constexpr std::uint32_t kTextureStreamed = 32;
    /// 256 colours, a byte a texel (the hardware's 256-colour form): a skin
    /// a player's colours are laid over by changing its palette alone -- the
    /// player model a console sent a game between consoles draws the others
    /// with (HlRemap.hpp).
    inline constexpr std::uint32_t kTexturePal256 = 64;
    /// ... whose palette has the game's two runs of a player's colours where
    /// "DM_Base" has them: 160..191 the top, 192..223 the bottom.
    inline constexpr std::uint32_t kTextureRemapRuns = 128;
    /// What it is made of, for how a step on it sounds: bits 8..11 hold a
    /// Material (the game's sound/materials.txt, by the texture's name).
    inline constexpr int kTextureMaterialShift = 8;
    inline constexpr std::uint32_t kTextureMaterialMask = 0xFu << kTextureMaterialShift;
    /// With kTextureA3I5: cooked as light, its alpha the brightness of each
    /// texel and its colour that brightness raised to full -- the picture of
    /// a brush the game adds to what is behind it (rendermode 3 or 5), whose
    /// black is nothing at all.
    inline constexpr std::uint32_t kTextureLight = 1u << 12;
    enum class Material : std::uint8_t
    {
        Concrete, ///< and whatever the game's list does not name
        Metal,
        Dirt,
        Vent,
        Grate,
        Tile,
        Slosh,
        Wood,     ///< (these three sound as concrete underfoot, as in the game)
        Computer,
        Glass,
        Flesh,    ///< (none in the game's own list: a mod's)
        Count
    };
    inline Material textureMaterial(std::uint32_t flags)
    {
        const std::uint32_t m = (flags & kTextureMaterialMask) >> kTextureMaterialShift;
        return m < static_cast<std::uint32_t>(Material::Count) ? static_cast<Material>(m) : Material::Concrete;
    }
    /// The letter the game's list has for each, and back.
    inline Material materialOfLetter(char letter)
    {
        switch (letter)
        {
        case 'M': return Material::Metal;
        case 'D': return Material::Dirt;
        case 'V': return Material::Vent;
        case 'G': return Material::Grate;
        case 'T': return Material::Tile;
        case 'S': return Material::Slosh;
        case 'W': return Material::Wood;
        case 'P': return Material::Computer;
        case 'Y': return Material::Glass;
        case 'F': return Material::Flesh;
        default:  return Material::Concrete;
        }
    }
    /// A texture's name as the game looks it up in its list: without the
    /// two characters of a "-0" or "+0" in front, without one of { ! ~ or a
    /// space after that, twelve characters at most, in lower case. `out`
    /// holds 13.
    inline void materialKey(const char *name, char *out)
    {
        if (name[0] == '-' || name[0] == '+')
            name += name[1] != '\0' ? 2 : 1;
        if (name[0] == '{' || name[0] == '!' || name[0] == '~' || name[0] == ' ')
            ++name;
        int n = 0;
        for (; n < 12 && name[n] != '\0'; ++n)
            out[n] = name[n] >= 'A' && name[n] <= 'Z' ? static_cast<char>(name[n] + 32) : name[n];
        out[n] = '\0';
    }
    inline constexpr int kSkySide = 128; ///< texels along each side of a sky picture
    inline constexpr int kA3I5Colours = 32;

    struct TextureHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t width;  ///< a power of two, 16..1024
        std::uint16_t height;
        std::uint16_t paletteColours;
        std::uint32_t texelBytes;   ///< 4x4: width * height / 4. A3I5: width * height
        std::uint32_t indexBytes;   ///< 4x4: width * height / 8. A3I5: 0
        std::uint32_t paletteBytes; ///< paletteColours * 2, rounded up to 4
        std::uint32_t flags;
        /// Of the source pixels and the settings they were cooked with: lets
        /// the converter skip a texture it has already made.
        std::uint32_t sourceHash;
    };
    static_assert(sizeof(TextureHeader) == 32);

    /// Check a .htx file's header against its size. After this the three
    /// pieces lie at fixed offsets: texels after the header, then the index,
    /// then the palette.
    inline bool textureHeaderValid(const TextureHeader &h, std::uint32_t fileSize)
    {
        if (std::memcmp(h.magic, kTextureMagic, sizeof(kTextureMagic)) != 0 ||
            h.version != kTextureVersion)
            return false;
        const bool pow2 = h.width >= 16 && h.width <= 1024 && (h.width & (h.width - 1)) == 0 &&
                          h.height >= 16 && h.height <= 1024 && (h.height & (h.height - 1)) == 0;
        if (!pow2)
            return false;
        const std::uint32_t texels = static_cast<std::uint32_t>(h.width) * h.height;
        const bool a3i5 = (h.flags & kTextureA3I5) != 0;
        const bool pal256 = (h.flags & kTexturePal256) != 0;
        const bool pieces = pal256 ? (h.texelBytes == texels && h.indexBytes == 0 && h.paletteColours == 256)
                            : a3i5 ? (h.texelBytes == texels && h.indexBytes == 0 &&
                                      h.paletteColours <= kA3I5Colours)
                                   : (h.texelBytes == texels / 4 && h.indexBytes == texels / 8);
        return pieces &&
               h.paletteBytes == ((static_cast<std::uint32_t>(h.paletteColours) * 2 + 3) & ~3u) &&
               h.paletteColours != 0 &&
               fileSize == sizeof(TextureHeader) + h.texelBytes + h.indexBytes + h.paletteBytes;
    }

    /// "<name>_<w>x<h>.htx", or "...a.htx" for the A3I5 form, "...l.htx" for
    /// the A3I5 form cooked as light (kTextureLight), into `out` (at least 40
    /// bytes). Characters a FAT file name cannot hold are replaced, the same
    /// way on both sides.
    inline void textureFileName(const char *name, unsigned width, unsigned height, bool a3i5,
                                char *out, bool light = false)
    {
        int n = 0;
        for (; name[n] != '\0' && n < 16; ++n)
        {
            const char c = name[n];
            const bool bad = c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                             c == '|' || c == ':' || c == '/' || c == '\\' ||
                             static_cast<unsigned char>(c) < 32;
            out[n] = bad ? '_' : c;
        }
        // Decimal by hand: no <cstdio> in a header the ITCM code may include.
        auto number = [&](unsigned v)
        {
            char digits[5];
            int count = 0;
            do
            {
                digits[count++] = static_cast<char>('0' + v % 10);
                v /= 10;
            } while (v != 0 && count < 5);
            while (count > 0)
                out[n++] = digits[--count];
        };
        out[n++] = '_';
        number(width);
        out[n++] = 'x';
        number(height);
        if (a3i5)
            out[n++] = light ? 'l' : 'a';
        std::memcpy(out + n, ".htx", 5);
    }
} // namespace hldata
