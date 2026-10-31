#include "global_header.hpp"

#include "ui/TouchCanvas.hpp"

#include "Global.hpp"
#include "HlRemap.hpp"

namespace hlrt
{
    TouchCanvas touchCanvas;

    namespace
    {
        constexpr int kFirstTile = 64; // (the map is where tiles 0 to 63 would be)
        std::uint32_t plainWords[hldata::kPlainFontWords];
        std::uint32_t tilesCopy[768 * 8];
        /// The rows of the map a page's tints are made in.
        std::uint16_t mapCopy[24 * 32];
    } // namespace

    void TouchCanvas::create()
    {
        if (created_)
            return;
        created_ = true;
        videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE);
        vramSetBankH(VRAM_H_SUB_BG);
        REG_BG0CNT_SUB = BG_32x32 | BG_COLOR_16 | BG_MAP_BASE(0) | BG_TILE_BASE(0) | BG_PRIORITY(0);
        REG_BG0HOFS_SUB = 0;
        REG_BG0VOFS_SUB = 0;
        for (int i = 0; i < 16; ++i)
            BG_PALETTE_SUB[i] = hldata::uiColour(i);
        canvas_.tiles = reinterpret_cast<std::uint32_t *>(reinterpret_cast<std::uint8_t *>(BG_TILE_RAM_SUB(0)) +
                                                          kFirstTile * 32);
        screen_ = canvas_.tiles;
        canvas_.width = 256;
        canvas_.height = 192;
        canvas_.clear();
        std::uint16_t *map = BG_MAP_RAM_SUB(0);
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 32; ++x)
                map[y * 32 + x] = static_cast<std::uint16_t>(kFirstTile + (y < 24 ? y * 32 + x : 0));
        }
        loadFonts();
    }

    void TouchCanvas::tint(int x, int y, int w, int h, const std::uint16_t *three, int bank)
    {
        // (Another sixteen colours: the first, three of them changed.)
        bank = bank < 1 ? 1 : (bank > 15 ? 15 : bank);
        if (three != nullptr)
        {
            for (int i = 0; i < 16; ++i)
                BG_PALETTE_SUB[bank * 16 + i] = i >= 1 && i <= 3 ? three[i - 1] : hldata::uiColour(i);
        }
        std::uint16_t *map = pageDepth_ > 0 ? mapCopy : BG_MAP_RAM_SUB(0);
        for (int cy = y; cy < y + h && cy < 24; ++cy)
        {
            for (int cx = x; cx < x + w && cx < 32; ++cx)
                map[cy * 32 + cx] = static_cast<std::uint16_t>((kFirstTile + cy * 32 + cx) | (three != nullptr ? bank << 12 : 0));
        }
    }

    void TouchCanvas::tintWhole(int x, int y, int w, int h, const std::uint16_t *sixteen, int bank)
    {
        for (int i = 0; i < 16; ++i)
            BG_PALETTE_SUB[bank * 16 + i] = sixteen[i];
        std::uint16_t *map = pageDepth_ > 0 ? mapCopy : BG_MAP_RAM_SUB(0);
        for (int cy = y; cy < y + h && cy < 24; ++cy)
        {
            for (int cx = x; cx < x + w && cx < 32; ++cx)
                map[cy * 32 + cx] = static_cast<std::uint16_t>((kFirstTile + cy * 32 + cx) | (bank << 12));
        }
    }

    /// A player's colours, as their swatch shows them: a hue each, strong
    /// and bright (a hue is laid over a colour keeping how strong it is:
    /// a grey's would stay grey).
    std::uint16_t hueColour(int hue)
    {
        std::uint8_t c[1][3] = {{230, 60, 50}};
        hldata::hueReplace(c, hue, 0, 0);
        return static_cast<std::uint16_t>((c[0][0] >> 3) | (c[0][1] >> 3) << 5 | (c[0][2] >> 3) << 10);
    }

    std::uint32_t *TouchCanvas::copyTiles() { return tilesCopy; }

    void TouchCanvas::pageOff()
    {
        if (pageDepth_++ > 0)
            return;
        create();
        const volatile std::uint32_t *from = screen_;
        for (int i = 0; i < 768 * 8; ++i)
            tilesCopy[i] = from[i];
        const volatile std::uint16_t *map = BG_MAP_RAM_SUB(0);
        for (int i = 0; i < 24 * 32; ++i)
            mapCopy[i] = map[i];
        canvas_.tiles = tilesCopy;
    }

    void TouchCanvas::pageOn()
    {
        if (pageDepth_ == 0 || --pageDepth_ > 0)
            return;
        canvas_.tiles = screen_;
        volatile std::uint32_t *to = screen_;
        for (int i = 0; i < 768 * 8; ++i)
        {
            if (to[i] != tilesCopy[i])
                to[i] = tilesCopy[i];
        }
        volatile std::uint16_t *map = BG_MAP_RAM_SUB(0);
        for (int i = 0; i < 24 * 32; ++i)
        {
            if (map[i] != mapCopy[i])
                map[i] = mapCopy[i];
        }
    }

    void TouchCanvas::loadFonts()
    {
        if (loaded_)
            return;
        // The console's letters are a bit a pixel, eight bytes a letter,
        // from the space on.
        const ConsoleFont &own = consoleGetDefault()->font;
        if (!plain_.ok() && own.gfx != nullptr && own.bpp == 1 && own.numChars >= 96)
        {
            hldata::plainFont(static_cast<const std::uint8_t *>(own.gfx), false, plainWords);
            plain_.open(reinterpret_cast<const std::uint8_t *>(plainWords), sizeof(plainWords));
        }
        // (Not yet: a console sent the game has the game's own letters once
        // its host has sent them.)
        if (!global.hasData)
            return;
        loaded_ = true;
        for (int n = 0; n < hldata::kFontCount; ++n)
        {
            std::FILE *file = std::fopen(global.commonPath(hldata::kFontFiles[n]).c_str(), "rb");
            if (file == nullptr)
                continue;
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            // (The size is the card's word: bounded before it is believed.)
            std::uint8_t *block = size > 0 && size < 64 * 1024 ? static_cast<std::uint8_t *>(std::malloc(size)) : nullptr;
            const bool got = block != nullptr &&
                             std::fread(block, 1, static_cast<std::size_t>(size), file) == static_cast<std::size_t>(size);
            std::fclose(file);
            // (Kept for good if it is a font: these are for as long as the
            // game runs.)
            if (!got || !fonts_[n].open(block, static_cast<std::size_t>(size)))
                std::free(block);
        }
    }

    const hldata::FontView &TouchCanvas::font(int which)
    {
        loadFonts();
        if (which >= 0 && which < hldata::kFontCount && fonts_[which].ok())
            return fonts_[which];
        return plain_;
    }

    const hldata::FontView &TouchCanvas::plain()
    {
        loadFonts();
        return plain_;
    }
} // namespace hlrt
