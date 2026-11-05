#include "data/TextureCache.hpp"
#include "data/TextureStream.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#include <nds.h>

#include "Global.hpp"
#include "data/CardRead.hpp"
#include "HlTexture.hpp"
#include "HlTextureStream.hpp"
#include "data/LevelFile.hpp"

namespace hlrt
{
    TextureCache textureCache;
    std::uint16_t TextureCache::remapBase_[256];
    bool TextureCache::remapReady_ = false;

    void TextureCache::destroy()
    {
        for (int i = 0; i < count_; ++i)
        {
            if (materials_[i] != nullptr)
                NEA_MaterialDelete(materials_[i]);
            if (palettes_[i] != nullptr)
                NEA_PaletteDelete(palettes_[i]);
            if (flatPalettes_ != nullptr && flatPalettes_[i] != nullptr)
                NEA_PaletteDelete(flatPalettes_[i]);
        }
        if (flatTexture_ != nullptr)
            NEA_MaterialDelete(flatTexture_);
        forget();
    }

    void TextureCache::forget()
    {
        std::free(materials_);
        std::free(palettes_);
        std::free(flatPalettes_);
        std::free(streamed_);
        std::free(binds_);
        std::free(tints_);
        binds_ = nullptr;
        tints_ = nullptr;
        materials_ = nullptr;
        palettes_ = nullptr;
        flatPalettes_ = nullptr;
        streamed_ = nullptr;
        flatTexture_ = nullptr;
        streamedCount_ = 0;
        count_ = 0;
        missing_ = 0;
    }

    bool TextureCache::create()
    {
        destroy();
        const int count = static_cast<int>(level.view.textureCount);
        materials_ = static_cast<NEA_Material **>(std::calloc(count + 1, sizeof(NEA_Material *)));
        palettes_ = static_cast<NEA_Palette **>(std::calloc(count + 1, sizeof(NEA_Palette *)));
        binds_ = static_cast<std::uint32_t *>(std::calloc(count + 1, 2 * sizeof(std::uint32_t)));
        tints_ = static_cast<std::uint16_t *>(std::malloc((count + 1) * sizeof(std::uint16_t)));
        flatPalettes_ = static_cast<NEA_Palette **>(std::calloc(count + 1, sizeof(NEA_Palette *)));
        streamed_ = static_cast<std::uint8_t *>(std::calloc(count + 1, 1));
        for (int i = 0; tints_ != nullptr && i <= count; ++i)
            tints_[i] = 0x7FFF;
        if (materials_ == nullptr || palettes_ == nullptr || binds_ == nullptr || tints_ == nullptr ||
            flatPalettes_ == nullptr || streamed_ == nullptr)
        {
            destroy();
            return false;
        }
        count_ = count;
        for (int i = 0; i < count; ++i)
        {
            const hldata::TextureRef &ref = level.view.textures[i];
            // A streamed texture is its flat colour until it is read in.
            // (The planner numbers a map's textures up to a limit; the rest
            // are loaded like any other.)
            // (Sent the game, there is nothing to stream from: what was sent
            // is loaded, every one, as it comes -- but for a console in DSi
            // mode, whose memory it can stream from, which keeps it as it comes.)
#ifdef HLDS_CHILD
            const bool kMayStream = global.twlChild;
#else
            constexpr bool kMayStream = true;
#endif
            if (kMayStream && (ref.flags & hldata::kTextureStreamed) != 0 && i < hldata::kStreamMaxTextures &&
                makeFlat(static_cast<unsigned>(i),
#ifdef HLDS_PROFILE
                         // (A build that measures shows what is not in video
                         // memory in magenta: there is no mistaking it.)
                         static_cast<std::uint16_t>(31 | 31 << 10)
#else
                         ref.average
#endif
                         ))
            {
#ifdef HLDS_CHILD
                textureStream.keepFromStream(i);
#endif
                streamed_[i] = 1;
                ++streamedCount_;
                tints_[i] = ref.average;
                continue;
            }
            if (!loadOne(static_cast<unsigned>(i)))
            {
                ++missing_;
                continue;
            }
            binds_[i * 2] = NEA_MaterialGetTexParam(materials_[i]);
            binds_[i * 2 + 1] = NEA_PaletteGetBase(palettes_[i]);
        }
        return true;
    }

    bool TextureCache::makeFlat(unsigned index, std::uint16_t colour)
    {
        if (flatTexture_ == nullptr)
        {
            // Sixteen bytes of zeros: every texel is colour 0 of whichever
            // palette it is drawn with, and the picture repeats.
            static const std::uint8_t kZeros[16] = {};
            flatTexture_ = NEA_MaterialCreate();
            if (flatTexture_ == nullptr ||
                NEA_MaterialTexLoad(flatTexture_, NEA_PAL4, 8, 8,
                                    static_cast<NEA_TextureFlags>(NEA_TEXTURE_WRAP_S | NEA_TEXTURE_WRAP_T),
                                    kZeros) != 1)
            {
                if (flatTexture_ != nullptr)
                    NEA_MaterialDelete(flatTexture_);
                flatTexture_ = nullptr;
                return false;
            }
        }
        NEA_Palette *palette = NEA_PaletteCreate();
        const std::uint16_t colours[4] = {colour, colour, colour, colour};
        if (palette == nullptr || NEA_PaletteLoad(palette, colours, 4, NEA_PAL4) != 1)
        {
            if (palette != nullptr)
                NEA_PaletteDelete(palette);
            return false;
        }
        flatPalettes_[index] = palette;
        binds_[index * 2] = NEA_MaterialGetTexParam(flatTexture_);
        binds_[index * 2 + 1] = NEA_PaletteGetBase(palette);
        return true;
    }

    bool TextureCache::use(unsigned index) const
    {
        if (streamed_[index] != 0)
        {
            // What NEA_MaterialUse writes, from the words kept for it.
            GFX_PAL_FORMAT = binds_[index * 2 + 1];
            GFX_TEX_FORMAT = binds_[index * 2];
            return true;
        }
        if (materials_[index] != nullptr)
        {
            NEA_MaterialUse(materials_[index]);
            return true;
        }
        GFX_TEX_FORMAT = 0;
        return false;
    }

    void TextureCache::attachStreamed(unsigned index, NEA_Material *material, NEA_Palette *palette)
    {
        if (materials_[index] != nullptr || palettes_[index] != nullptr)
            detachStreamed(index);
        materials_[index] = material;
        palettes_[index] = palette;
        binds_[index * 2] = NEA_MaterialGetTexParam(material);
        binds_[index * 2 + 1] = NEA_PaletteGetBase(palette);
    }

    void TextureCache::detachStreamed(unsigned index)
    {
        // The words come back to the flat colour first: whatever is drawn
        // next must not name memory that is about to be somebody else's.
        binds_[index * 2] = NEA_MaterialGetTexParam(flatTexture_);
        binds_[index * 2 + 1] = NEA_PaletteGetBase(flatPalettes_[index]);
        // (Not freed here: the picture on the screen was drawn with it, and
        // is drawn again until the next one is shown.)
        textureStream.retire(materials_[index], palettes_[index]);
        materials_[index] = nullptr;
        palettes_[index] = nullptr;
    }

    HL_ARM unsigned TextureCache::shown(unsigned index, bool switched) const
    {
        const hldata::TextureRef &ref = level.view.textures[index];
        unsigned first = index, pictures = ref.frames;
        if (switched && ref.other != hldata::kNoTexture)
        {
            first = ref.other;
            pictures = ref.otherFrames;
        }
        return pictures > 1 ? first + tick_ % pictures : first;
    }

    HL_ARM void TextureCache::frame()
    {
        tick_ = Global::drawClock() / 6;
    }

    bool TextureCache::loadOne(unsigned index)
    {
        const hldata::TextureRef &ref = level.view.textures[index];
        char fileName[40];
        const bool a3i5 = (ref.flags & hldata::kTextureA3I5) != 0;
        hldata::textureFileName(ref.name, ref.width, ref.height, a3i5, fileName);
        const std::string path = global.texDir() + fileName;
        // (A liquid's coordinates go through the texture matrix, and a
        // conveyor belt's.)
        return loadFile(path.c_str(), (ref.flags & (hldata::kTextureLiquid | hldata::kTextureScroll)) != 0, ref.width,
                        ref.height, a3i5 ? 1 : 0, materials_[index], palettes_[index], &tints_[index]);
    }

    namespace
    {
        /// A .htx header, checked against the file's size and against what it
        /// was looked up as.
        bool headerFits(const hldata::TextureHeader &header, std::uint32_t size, int expectWidth, int expectHeight,
                        int expectA3I5)
        {
            const bool a3i5 = (header.flags & hldata::kTextureA3I5) != 0;
            return hldata::textureHeaderValid(header, size) &&
                   (expectWidth == 0 || header.width == expectWidth) &&
                   (expectHeight == 0 || header.height == expectHeight) &&
                   (expectA3I5 < 0 || a3i5 == (expectA3I5 != 0));
        }
    } // namespace

    bool TextureCache::readFile(const char *path, int expectWidth, int expectHeight, int expectA3I5,
                                hldata::TextureHeader &header, std::uint8_t *body, std::size_t capacity)
    {
#ifndef HLDS_CHILD
        // In an archive and stored as it is (as a map's textures are): its
        // size from the archive's index, its bytes read straight in, with no
        // file opened.
        NEA_NpacMember member;
        if (NEA_NpacMemberFind(path, &member) == 0 && member.method == NEA_NPAC_STORED)
        {
            const std::uint32_t size = member.size;
            const bool ok = size > sizeof(header) && cardReadMember(member, 0, &header, sizeof(header)) &&
                            headerFits(header, size, expectWidth, expectHeight, expectA3I5);
            const std::uint32_t rest = size - static_cast<std::uint32_t>(sizeof(header));
            return ok && rest <= capacity && cardReadMember(member, sizeof(header), body, rest);
        }
#endif
        std::FILE *file = std::fopen(path, "rb");
        if (file == nullptr)
            return false;
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);

        // The header is checked against the file's real size before anything
        // in it is used to size an allocation or a read.
        bool ok = size > static_cast<long>(sizeof(header)) &&
                  std::fread(&header, sizeof(header), 1, file) == 1 &&
                  headerFits(header, static_cast<std::uint32_t>(size), expectWidth, expectHeight, expectA3I5);
        if (ok)
        {
            const std::size_t rest = static_cast<std::size_t>(size) - sizeof(header);
            ok = rest <= capacity && cardRead(body, rest, file) == rest;
        }
        std::fclose(file);
        return ok;
    }

    bool TextureCache::loadFile(const char *path, bool liquid, int expectWidth, int expectHeight,
                                int expectA3I5, NEA_Material *&outMaterial, NEA_Palette *&outPalette,
                                std::uint16_t *tint, bool ramBacked)
    {
        outMaterial = nullptr;
        outPalette = nullptr;
#ifndef HLDS_CHILD
        // In an archive: its size from the index, then the whole of it read
        // (or undone, if packed) into one buffer, with no file opened.
        NEA_NpacMember member;
        if (NEA_NpacMemberFind(path, &member) == 0 && cardMemberReadable(member))
        {
            if (member.size <= sizeof(hldata::TextureHeader))
                return false;
            auto *data = static_cast<std::uint8_t *>(std::malloc(member.size));
            hldata::TextureHeader header;
            bool ok = data != nullptr && cardReadMember(member, 0, data, member.size);
            if (ok)
                std::memcpy(&header, data, sizeof(header));
            ok = ok && headerFits(header, member.size, expectWidth, expectHeight, expectA3I5) &&
                 create(header, data + sizeof(header), liquid, outMaterial, outPalette, tint, ramBacked);
            std::free(data);
            return ok;
        }
#endif
        std::FILE *file = std::fopen(path, "rb");
        if (file == nullptr)
            return false;
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fclose(file);
        if (size <= static_cast<long>(sizeof(hldata::TextureHeader)))
            return false;
        const std::size_t body = static_cast<std::size_t>(size) - sizeof(hldata::TextureHeader);
        auto *data = static_cast<std::uint8_t *>(std::malloc(body));
        hldata::TextureHeader header;
        bool ok = data != nullptr && readFile(path, expectWidth, expectHeight, expectA3I5, header, data, body) &&
                  create(header, data, liquid, outMaterial, outPalette, tint, ramBacked);
        std::free(data);
        return ok;
    }

    bool TextureCache::create(const hldata::TextureHeader &header, const std::uint8_t *data, bool liquid,
                              NEA_Material *&outMaterial, NEA_Palette *&outPalette, std::uint16_t *tint,
                              bool ramBacked)
    {
        outMaterial = nullptr;
        outPalette = nullptr;
        const bool a3i5 = (header.flags & hldata::kTextureA3I5) != 0;
        NEA_Material *material = NEA_MaterialCreate();
        NEA_Palette *palette = NEA_PaletteCreate();
        if (ramBacked && material != nullptr)
            NEA_MaterialRamInit(material);
        // A liquid's coordinates go through the texture matrix (WorldDraw
        // makes that sway); everything else's are used as they are.
        const auto flags = static_cast<NEA_TextureFlags>(
            NEA_TEXTURE_WRAP_S | NEA_TEXTURE_WRAP_T | (liquid ? NEA_TEXGEN_TEXCOORD : 0));
        // NEA copies the pieces into VRAM, so the buffer is only needed for
        // the length of these calls. The two formats differ in what the pieces
        // are, not in where they sit: texels, then (4x4 only) the index, then
        // the palette.
        const std::uint8_t *paletteData = data + header.texelBytes + header.indexBytes;
        if (tint != nullptr && header.paletteColours != 0)
        {
            // Its colour, taken all together: the mean of its palette.
            std::uint32_t r = 0, g = 0, b = 0;
            for (int n = 0; n < header.paletteColours; ++n)
            {
                const std::uint16_t c = static_cast<std::uint16_t>(paletteData[n * 2] | (paletteData[n * 2 + 1] << 8));
                r += c & 31;
                g += (c >> 5) & 31;
                b += (c >> 10) & 31;
            }
            *tint = static_cast<std::uint16_t>((r / header.paletteColours) | ((g / header.paletteColours) << 5) |
                                               ((b / header.paletteColours) << 10));
        }
        bool ok = material != nullptr && palette != nullptr;
        if (ok && (header.flags & hldata::kTexturePal256))
        {
            ok = NEA_MaterialTexLoad(material, NEA_PAL256, header.width, header.height, flags, data) == 1 &&
                 NEA_PaletteLoad(palette, paletteData, 256, NEA_PAL256) == 1;
            // (Its palette as it came, for the players' own to be made from:
            // the skin is read once.)
            if (ok && (header.flags & hldata::kTextureRemapRuns))
            {
                std::memcpy(remapBase_, paletteData, sizeof(remapBase_));
                remapReady_ = true;
            }
        }
        else if (ok && a3i5)
        {
            ok = NEA_MaterialTexLoad(material, NEA_A3PAL32, header.width, header.height, flags,
                                     data) == 1 &&
                 NEA_PaletteLoad(palette, paletteData, header.paletteColours, NEA_A3PAL32) == 1;
        }
        else if (ok)
        {
            ok = NEA_MaterialTex4x4Load(material, header.width, header.height, flags, data,
                                        data + header.texelBytes) == 1 &&
                 NEA_PaletteLoadSize(palette, paletteData,
                                     static_cast<std::size_t>(header.paletteColours) * 2,
                                     NEA_TEX4X4) == 1;
        }
        if (!ok)
        {
            // Most likely out of texture or palette memory. Give back whatever
            // half of it was taken.
            if (material != nullptr)
                NEA_MaterialDelete(material);
            if (palette != nullptr)
                NEA_PaletteDelete(palette);
            return false;
        }
        NEA_MaterialSetPalette(material, palette);
        outMaterial = material;
        outPalette = palette;
        return true;
    }
} // namespace hlrt
