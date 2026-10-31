#include "render/MapCard.hpp"
#include "data/LevelFile.hpp"

#include <nds.h>

#include <NEAMain.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Global.hpp"
#include "HlFont.hpp"
#include "HlPreview.hpp"
#include "HlNetGame.hpp"
#include "net/NetGame.hpp"
#include "ui/TouchCanvas.hpp"

namespace hlrt
{
    MapCard mapCard;

    namespace
    {
        using namespace hldata;

        // Where it is on the top screen: the picture, then the words.
        constexpr int kTop = 4;
        constexpr int kPictureX = 6, kPictureW = 96, kPictureH = 72;
        constexpr int kTextX = 6, kTextTop = kTop + kPictureH + 4, kTextW = 128, kTextH = 104;
        constexpr int kTextureH = 128; ///< (a texture's height is a power of two: the words in its top rows)
    } // namespace

    bool mapPictureCurrent(const std::string &map, std::uint32_t levelCrc)
    {
#ifdef HLDS_CHILD
        (void)map;
        (void)levelCrc;
        return false;
#else
        if (!global.hasFat)
            return false;
        std::FILE *file = std::fopen((global.savesDir() + kPreviewFolder + map + ".hpv").c_str(), "rb");
        if (file == nullptr)
            return false;
        PreviewHeader header;
        const bool got = std::fread(&header, sizeof(header), 1, file) == 1;
        std::fclose(file);
        return got && previewOfMap(header, levelCrc);
#endif
    }

    bool mapPictureCurrent(const std::string &map)
    {
#ifdef HLDS_CHILD
        (void)map;
        return false;
#else
        LevelHeader header{};
        return Level::peekHeader(map, header) && mapPictureCurrent(map, header.crc32);
#endif
    }

    bool mapPictured(const std::string &map)
    {
        if (!global.hasFat)
            return false;
        std::FILE *file = std::fopen((global.savesDir() + kPreviewFolder + map + ".hpv").c_str(), "rb");
        if (file != nullptr)
            std::fclose(file);
        return file != nullptr;
    }

    void MapCard::release()
    {
        if (picture_ != nullptr)
            NEA_MaterialDelete(static_cast<NEA_Material *>(picture_));
        if (text_ != nullptr)
            NEA_MaterialDelete(static_cast<NEA_Material *>(text_));
        if (textPalette_ != nullptr)
            NEA_PaletteDelete(static_cast<NEA_Palette *>(textPalette_));
        picture_ = text_ = textPalette_ = nullptr;
    }

    void MapCard::givePicture(const char *name, std::uint8_t *file, std::size_t size)
    {
        if (previewWidthOf(file, size) == 0)
        {
            std::free(file);
            return;
        }
        Given *place = nullptr;
        for (Given &one : given_)
        {
            if (one.data != nullptr && std::strcmp(one.name, name) == 0)
                place = &one;
        }
        for (Given &one : given_)
        {
            if (place == nullptr && one.data == nullptr)
                place = &one;
        }
        if (place == nullptr)
        {
            std::free(file);
            return;
        }
        std::free(place->data);
        place->data = file;
        place->size = static_cast<std::uint32_t>(size);
        std::snprintf(place->name, sizeof(place->name), "%s", name);
        // (Its card again, with it.)
        if (shownName_ == name)
        {
            release();
            shownName_.clear();
        }
    }

    void MapCard::dropPicture()
    {
        for (Given &one : given_)
        {
            std::free(one.data);
            one = Given();
        }
    }

    const char *MapCard::pictureName(std::uint32_t hash) const
    {
        for (const Given &one : given_)
        {
            if (one.data != nullptr && nameHash(one.name) == hash)
                return one.name;
        }
        return nullptr;
    }

    void MapCard::show(const NetMapInfo *map, const char *elsewhere)
    {
        const std::string name = map != nullptr ? map->name : (elsewhere != nullptr ? elsewhere : "");
        if (name == shownName_ && (shown() || name.empty()))
            return;
        release();
        shownName_ = name;
        if (name.empty())
            return;

        // The picture: the one the host sent, or the one this console took.
        bool pictured = false;
        const Given *given = nullptr;
        for (const Given &one : given_)
        {
            if (one.data != nullptr && name == one.name)
                given = &one;
        }
        std::FILE *file = !given && map != nullptr && global.hasFat
                              ? std::fopen((global.savesDir() + kPreviewFolder + map->name + ".hpv").c_str(), "rb")
                              : nullptr;
        if (given != nullptr || file != nullptr)
        {
            const std::size_t length = given != nullptr ? given->size : kPreviewBytes;
            auto *bytes = static_cast<std::uint8_t *>(std::malloc(length));
            // (A texture is 128 square: the picture in its top 96 rows.)
            auto *texels = static_cast<std::uint16_t *>(std::calloc(128 * 128, 2));
            bool got = bytes != nullptr;
            if (got && given != nullptr)
                std::memcpy(bytes, given->data, length);
            else if (got)
                got = std::fread(bytes, 1, kPreviewBytes, file) == kPreviewBytes;
            const int width = got ? previewWidthOf(bytes, length) : 0;
            if (texels != nullptr && width != 0)
            {
                // (A small one: row by row into a texture as wide as ever.)
                pictureW_ = width;
                pictureH_ = width == kPreviewWidth ? kPreviewHeight : kPreviewSmallHeight;
                for (int y = 0; y < pictureH_; ++y)
                    std::memcpy(texels + y * 128, bytes + sizeof(PreviewHeader) + static_cast<std::size_t>(y) * pictureW_ * 2,
                                static_cast<std::size_t>(pictureW_) * 2);
                NEA_Material *material = NEA_MaterialCreate();
                if (material != nullptr &&
                    NEA_MaterialTexLoad(material, NEA_A1RGB5, 128, 128, NEA_TEXGEN_TEXCOORD, texels) == 1)
                {
                    picture_ = material;
                    pictured = true;
                }
                else if (material != nullptr)
                    NEA_MaterialDelete(material);
            }
            std::free(bytes);
            std::free(texels);
            if (file != nullptr)
                std::fclose(file);
        }

        // The words: drawn as on the touch screen, into a canvas of their
        // own, then made the texture's (A3I5: the UI's sixteen colours, the
        // background see-through by half). (Both in blocks of their own for
        // as long as that takes: 22 KB a console sent the game cannot keep.)
        constexpr std::size_t kTileBytes = (kTextW / 8) * (kTextH / 8) * 8 * sizeof(std::uint32_t);
        auto *tiles = static_cast<std::uint32_t *>(std::malloc(kTileBytes));
        auto *texels = static_cast<std::uint8_t *>(std::calloc(kTextW * kTextureH, 1));
        if (tiles == nullptr || texels == nullptr)
        {
            std::free(tiles);
            std::free(texels);
            return;
        }
        Canvas canvas;
        canvas.tiles = tiles;
        canvas.width = kTextW;
        canvas.height = kTextH;
        canvas.clear(kUiBack);
        canvas.frame(0, 0, kTextW, kTextH, kUiEdge);
        const FontView &medium = touchCanvas.font(kFontMedium);
        const FontView &small = touchCanvas.font(kFontSmall);
        char line[48];
        int y = 4;
        std::snprintf(line, sizeof(line), "%s", name.c_str());
        for (char &c : line)
            c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        drawText(canvas, medium, 6, y, line, inkFor(medium, kUiOrange));
        y += medium.height() + 3;
        auto say = [&](const char *text, int ramp)
        {
            drawText(canvas, small, 6, y, text, inkFor(small, ramp));
            y += small.height() + 2;
        };
        if (map != nullptr)
        {
            std::snprintf(line, sizeof(line), "For up to %d players", netMapPlayers(*map));
            say(line, kUiWhite);
            if (room_ != 0)
            {
                // (Consoles sent the game here: whether it fits them all.)
                if (netMapFits(*map, room_))
                    say("Fits every console", kUiWhite);
                else
                    say("Too big for a guest", kUiGrey);
            }
            else if (map->guestFits)
                say("Fits a guest", kUiWhite);
        }
        else
#ifdef HLDS_CHILD
            say("The host's map", kUiGrey);
#else
            say("Not on this card", kUiGrey);
#endif
        // How the match is set up, as the host says it (HOST's and a lobby's).
        if (showRules_)
        {
            const NetRules &rules = netGame.match().rules;
            std::snprintf(line, sizeof(line), "%s, %d frags", rules.teams() ? "Teamplay" : "Deathmatch", rules.frags);
            say(line, kUiWhite);
            std::snprintf(line, sizeof(line), "%d minutes", rules.minutes);
            say(line, kUiWhite);
            std::snprintf(line, sizeof(line), "Weapons %s; %s", rules.has(kRuleWeaponsStay) ? "stay" : "go",
                          rules.has(kRuleFlashlight) ? "lamp" : "no lamp");
            say(line, kUiGrey);
        }
        for (int py = 0; py < kTextH; ++py)
        {
            for (int px = 0; px < kTextW; ++px)
            {
                const int c = canvas.at(px, py);
                texels[py * kTextW + px] = static_cast<std::uint8_t>((c == kUiBack ? 5 << 5 : 7 << 5) | c);
            }
        }
        std::uint16_t colours[32];
        for (int i = 0; i < 32; ++i)
            colours[i] = uiColour(i & 15);
        NEA_Material *material = NEA_MaterialCreate();
        NEA_Palette *palette = NEA_PaletteCreate();
        if (material != nullptr && palette != nullptr &&
            NEA_MaterialTexLoad(material, NEA_A3PAL32, kTextW, kTextureH, NEA_TEXGEN_TEXCOORD, texels) == 1 &&
            NEA_PaletteLoad(palette, colours, 32, NEA_A3PAL32) == 1)
        {
            NEA_MaterialSetPalette(material, palette);
            text_ = material;
            textPalette_ = palette;
        }
        else
        {
            if (material != nullptr)
                NEA_MaterialDelete(material);
            if (palette != nullptr)
                NEA_PaletteDelete(palette);
        }
        std::free(tiles);
        std::free(texels);
    }

    void MapCard::draw(void (*corner)(int sx, int sy, int s, int t))
    {
        if (text_ == nullptr)
            return;
        // Over the title's picture, at its depth, as the title's own logo is
        // -- and as it is, see-through (by a hair): an opaque polygon drawn
        // where another is, at that depth, was not seen.
        NEA_PolyFormat(30, 2, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE, NEA_DEPTH_TEST_EQUAL);
        GFX_COLOR = 0x7FFF;
        if (picture_ != nullptr)
        {
            NEA_MaterialUse(static_cast<const NEA_Material *>(picture_));
        }
        else
        {
            // (None yet: a plain dark plate where it would be.)
            GFX_TEX_FORMAT = 0;
            GFX_COLOR = RGB15(7, 7, 8);
        }
        GFX_BEGIN = GL_QUADS;
        corner(kPictureX, kTop, 0, 0);
        corner(kPictureX, kTop + kPictureH, 0, pictureH_);
        corner(kPictureX + kPictureW, kTop + kPictureH, pictureW_, pictureH_);
        corner(kPictureX + kPictureW, kTop, pictureW_, 0);
        GFX_COLOR = 0x7FFF;
        NEA_PolyFormat(31, 3, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE, NEA_DEPTH_TEST_EQUAL);
        NEA_MaterialUse(static_cast<const NEA_Material *>(text_));
        GFX_BEGIN = GL_QUADS;
        corner(kTextX, kTextTop, 0, 0);
        corner(kTextX, kTextTop + kTextH, 0, kTextH);
        corner(kTextX + kTextW, kTextTop + kTextH, kTextW, kTextH);
        corner(kTextX + kTextW, kTextTop, kTextW, 0);
    }
} // namespace hlrt
