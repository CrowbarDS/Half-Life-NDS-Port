#include "hlds/UiCook.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "hlds/AtomicWrite.hpp"

namespace hlds
{
    namespace
    {
        std::uint32_t u32At(const std::vector<std::uint8_t> &bytes, std::size_t at)
        {
            return static_cast<std::uint32_t>(bytes[at]) | static_cast<std::uint32_t>(bytes[at + 1]) << 8 |
                   static_cast<std::uint32_t>(bytes[at + 2]) << 16 | static_cast<std::uint32_t>(bytes[at + 3]) << 24;
        }

        // A frame as it is, how bright a pixel is becoming how solid (the
        // game adds all of these to the picture).
        RgbaImage additiveFrame(const SprFile &spr, int index)
        {
            RgbaImage image;
            if (index >= static_cast<int>(spr.frames.size()))
                return image;
            const SprFrame &f = spr.frames[index];
            image.width = f.width;
            image.height = f.height;
            image.rgba.assign(static_cast<std::size_t>(f.width) * f.height * 4, 0);
            for (int i = 0; i < f.width * f.height; ++i)
            {
                const int c = f.pixels[i];
                if (spr.format == kSprAlphaTest && c == 255)
                    continue;
                int r = spr.palette[c][0], g = spr.palette[c][1], b = spr.palette[c][2];
                const int a = spr.format == kSprIndexAlpha ? c : std::max(r, std::max(g, b));
                if (spr.format == kSprIndexAlpha)
                    r = g = b = 255;
                else if (a > 0)
                {
                    r = r * 255 / a;
                    g = g * 255 / a;
                    b = b * 255 / a;
                }
                std::uint8_t *p = &image.rgba[static_cast<std::size_t>(i) * 4];
                p[0] = static_cast<std::uint8_t>(r);
                p[1] = static_cast<std::uint8_t>(g);
                p[2] = static_cast<std::uint8_t>(b);
                p[3] = static_cast<std::uint8_t>(a);
            }
            return image;
        }

        // The piece of `from` at (sx, sy), `w` by `h`, made the size of `to`:
        // each pixel the average of what it covers (by how much of each
        // source pixel it covers), its colour weighted by how solid.
        // `brightest`: as bright as the brightest of it instead (a picture
        // of lines one pixel wide, which an average would all but lose).
        void shrinkInto(RgbaImage &atlas, const RgbaImage &from, int sx, int sy, int w, int h, hldata::UiRect to,
                        bool brightest = false)
        {
            const double fx = static_cast<double>(w) / to.w, fy = static_cast<double>(h) / to.h;
            for (int y = 0; y < to.h; ++y)
            {
                for (int x = 0; x < to.w; ++x)
                {
                    double sum[4] = {0, 0, 0, 0}, area = 0, most = 0;
                    const double x0 = x * fx, x1 = (x + 1) * fx, y0 = y * fy, y1 = (y + 1) * fy;
                    for (int py = static_cast<int>(y0); py < y1 && py < h; ++py)
                    {
                        const double cy = std::min<double>(py + 1, y1) - std::max<double>(py, y0);
                        for (int px = static_cast<int>(x0); px < x1 && px < w; ++px)
                        {
                            const double cover = cy * (std::min<double>(px + 1, x1) - std::max<double>(px, x0));
                            area += cover;
                            if (sx + px >= from.width || sy + py >= from.height)
                                continue;
                            const std::uint8_t *p = from.at(sx + px, sy + py);
                            most = std::max<double>(most, p[3]);
                            const double a = p[3] * cover;
                            for (int k = 0; k < 3; ++k)
                                sum[k] += p[k] * a;
                            sum[3] += a;
                        }
                    }
                    if (area <= 0 || sum[3] <= 0)
                        continue;
                    std::uint8_t *q = atlas.at(to.x + x, to.y + y);
                    for (int k = 0; k < 3; ++k)
                        q[k] = static_cast<std::uint8_t>(std::lround(sum[k] / sum[3]));
                    q[3] = static_cast<std::uint8_t>(std::lround(brightest ? most : std::min(255.0, sum[3] / area)));
                }
            }
        }
    } // namespace

    bool readWadLump(const std::filesystem::path &wad, const std::string &name, std::vector<std::uint8_t> &out,
                     std::string &error)
    {
        std::vector<std::uint8_t> bytes;
        if (!readFile(wad, bytes, error))
            return false;
        if (bytes.size() < 12 || std::memcmp(bytes.data(), "WAD3", 4) != 0)
        {
            error = wad.filename().string() + " is not a WAD";
            return false;
        }
        const std::uint32_t count = u32At(bytes, 4), dir = u32At(bytes, 8);
        if (dir > bytes.size() || count > (bytes.size() - dir) / 32)
        {
            error = wad.filename().string() + " is cut short";
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const std::size_t at = dir + 32u * i;
            std::string lump;
            for (int c = 0; c < 16 && bytes[at + 16 + c] != 0; ++c)
                lump.push_back(static_cast<char>(std::toupper(bytes[at + 16 + c])));
            std::string wanted;
            for (char c : name)
                wanted.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            if (lump != wanted)
                continue;
            const std::uint32_t pos = u32At(bytes, at), size = u32At(bytes, at + 4);
            if (bytes[at + 13] != 0 || pos > bytes.size() || size > bytes.size() - pos)
            {
                error = name + " in " + wad.filename().string() + " cannot be read";
                return false;
            }
            out.assign(bytes.begin() + pos, bytes.begin() + pos + size);
            return true;
        }
        error = wad.filename().string() + " has no " + name;
        return false;
    }

    bool parseQFont(const std::vector<std::uint8_t> &lump, QFont &out, std::string &error)
    {
        out = QFont();
        if (lump.size() < 16 + 1024)
        {
            error = "the font is cut short";
            return false;
        }
        const std::uint32_t height = u32At(lump, 4), rowHeight = u32At(lump, 12);
        const std::size_t pixels = 16 + 1024;
        if (height == 0 || height > 1024 || rowHeight == 0 || rowHeight > hldata::kFontMaxHeight ||
            lump.size() < pixels + 256u * height + 2 + 768)
        {
            error = "the font is not of a shape this knows";
            return false;
        }
        out.rowHeight = static_cast<int>(rowHeight);
        out.imageHeight = static_cast<int>(height);
        for (int c = 0; c < 256; ++c)
        {
            out.glyphs[c].start = static_cast<std::uint16_t>(lump[16 + 4 * c] | lump[17 + 4 * c] << 8);
            out.glyphs[c].width = static_cast<std::uint16_t>(lump[18 + 4 * c] | lump[19 + 4 * c] << 8);
        }
        out.pixels.assign(lump.begin() + pixels, lump.begin() + pixels + 256u * height);
        const std::size_t palette = pixels + 256u * height + 2;
        out.palette.assign(lump.begin() + palette, lump.begin() + palette + 768);
        return true;
    }

    std::vector<std::uint8_t> cookFont(const QFont &font, bool outlined, bool accented)
    {
        // 32..127; and 128..255 too (accented letters, in Windows-1252, as a
        // translated game has them) if it has any of those -- and they are
        // letters (a console's are its letters again, in other colours).
        const int first = 32;
        bool accents = false;
        for (int c = 128; c < 256 && accented; ++c)
            accents = accents || font.glyphs[c].width != 0;
        const int count = accents ? 224 : 96;
        // What is behind the letters: whichever index most of the picture is.
        std::size_t seen[256] = {};
        for (std::uint8_t p : font.pixels)
            ++seen[p];
        const int back = static_cast<int>(std::max_element(seen, seen + 256) - seen);
        auto level = [&](std::uint8_t index) -> std::uint32_t
        {
            if (index == back || index == 255)
                return 0;
            const int bright = std::max({font.palette[index * 3], font.palette[index * 3 + 1],
                                         font.palette[index * 3 + 2]});
            if (outlined)
                return bright < 112 ? 1 : 3;
            return bright >= 170 ? 3 : (bright >= 85 ? 2 : (bright > 0 ? 1 : 0));
        };
        hldata::FontHeader header{};
        std::memcpy(header.magic, hldata::kFontMagic, 4);
        header.version = hldata::kFontVersion;
        header.height = static_cast<std::uint8_t>(font.rowHeight);
        header.first = first;
        header.count = count;
        header.outlined = outlined ? 1 : 0;
        std::vector<std::uint8_t> out(sizeof(header) + count + static_cast<std::size_t>(count) * font.rowHeight * 4, 0);
        std::memcpy(out.data(), &header, sizeof(header));
        for (int c = 0; c < count; ++c)
        {
            const QFont::Glyph &glyph = font.glyphs[first + c];
            const int width = std::min<int>(glyph.width, hldata::kFontMaxWidth);
            out[sizeof(header) + c] = static_cast<std::uint8_t>(width);
            const int x0 = glyph.start % 256, y0 = glyph.start / 256;
            for (int row = 0; row < font.rowHeight; ++row)
            {
                std::uint32_t bits = 0;
                for (int x = 0; x < width; ++x)
                {
                    const int px = x0 + x, py = y0 + row;
                    if (px < 256 && py < font.imageHeight)
                        bits |= level(font.pixels[py * 256 + px]) << (x * 2);
                }
                const std::size_t at = sizeof(header) + count + (static_cast<std::size_t>(c) * font.rowHeight + row) * 4;
                out[at] = static_cast<std::uint8_t>(bits);
                out[at + 1] = static_cast<std::uint8_t>(bits >> 8);
                out[at + 2] = static_cast<std::uint8_t>(bits >> 16);
                out[at + 3] = static_cast<std::uint8_t>(bits >> 24);
            }
        }
        return out;
    }

    bool parseQPic(const std::vector<std::uint8_t> &lump, IndexedImage &out, std::string &error)
    {
        out = IndexedImage();
        if (lump.size() < 8)
        {
            error = "the picture is cut short";
            return false;
        }
        const std::uint32_t width = u32At(lump, 0), height = u32At(lump, 4);
        if (width == 0 || height == 0 || width > 1024 || height > 1024 ||
            lump.size() < 8 + static_cast<std::size_t>(width) * height + 2 + 768)
        {
            error = "the picture is not of a shape this knows";
            return false;
        }
        out.width = width;
        out.height = height;
        out.pixels.assign(lump.begin() + 8, lump.begin() + 8 + width * height);
        const std::size_t palette = 8 + static_cast<std::size_t>(width) * height + 2;
        out.palette.assign(lump.begin() + palette, lump.begin() + palette + 768);
        return true;
    }

    RgbaImage hudAtlas(const SprFile &hud, const SprFile &crosshairs)
    {
        RgbaImage atlas;
        atlas.width = hldata::kHudAtlasWidth;
        atlas.height = hldata::kHudAtlasHeight;
        atlas.rgba.assign(static_cast<std::size_t>(atlas.width) * atlas.height * 4, 0);
        const RgbaImage sheet = hud.frames.empty() ? RgbaImage() : spriteFrameImage(hud, 0, false);
        const RgbaImage cross = crosshairs.frames.empty() ? RgbaImage() : spriteFrameImage(crosshairs, 0, false);
        auto copy = [&](const RgbaImage &from, int sx, int sy, hldata::UiRect to)
        {
            for (int y = 0; y < to.h; ++y)
            {
                for (int x = 0; x < to.w; ++x)
                {
                    if (sx + x >= from.width || sy + y >= from.height)
                        continue;
                    std::memcpy(atlas.at(to.x + x, to.y + y), from.at(sx + x, sy + y), 4);
                }
            }
        };
        // Where each is in the game's 320 set (its sprites/hud.txt and
        // weapon_*.txt say so).
        for (int n = 0; n < 10; ++n)
            copy(sheet, 12 * n, 0, hldata::hudDigit(n));
        copy(sheet, 0, 72, hldata::kHudCross);
        copy(sheet, 0, 52, hldata::kHudSuitFull);
        copy(sheet, 20, 52, hldata::kHudSuitEmpty);
        copy(sheet, 0, 16, hldata::kHudAmmo9mm);
        copy(sheet, 18, 16, hldata::kHudAmmo357);
        copy(sheet, 54, 16, hldata::kHudAmmoBuck);
        copy(sheet, 36, 34, hldata::kHudAmmoGrenade);
        copy(sheet, 36, 16, hldata::kHudAmmoAr);
        copy(cross, 24, 0, hldata::kHudCrosshair);
        copy(sheet, 34, 72, hldata::kHudLampEmpty);
        copy(sheet, 16, 72, hldata::kHudLampFull);
        copy(sheet, 52, 72, hldata::kHudLampBeam);
        copy(sheet, 72, 16, hldata::kHudAmmoBolt);
        copy(sheet, 54, 34, hldata::kHudAmmoSatchel);
        copy(sheet, 72, 34, hldata::kHudAmmoTripmine);
        copy(sheet, 90, 34, hldata::kHudAmmoSnark);
        copy(sheet, 90, 16, hldata::kHudAmmoRocket);
        copy(sheet, 0, 34, hldata::kHudAmmoUranium);
        copy(sheet, 16, 36, hldata::kHudAmmoHornet);
        return atlas;
    }

    RgbaImage hudTrainPictures(const SprFile &train)
    {
        RgbaImage atlas;
        atlas.width = hldata::kHudTrainWidth;
        atlas.height = hldata::kHudTrainHeight;
        atlas.rgba.assign(static_cast<std::size_t>(atlas.width) * atlas.height * 4, 0);
        // (Its frames are stopped, forward slow to fast, and back -- the
        // display shows frame m_iTrain - 1 -- 96 square, each the 640 set's
        // 72 (hud.txt) at 0.4 here.)
        for (int n = 0; n < 5; ++n)
        {
            const RgbaImage frame = additiveFrame(train, n);
            shrinkInto(atlas, frame, 0, 0, frame.width, frame.height, hldata::hudTrain(n));
        }
        return atlas;
    }

    RgbaImage hudAtlas640(const SprFile &hud7, const SprFile &hud8, const SprFile &hud9, const SprFile &pain,
                          const SprFile &crosshairs)
    {
        RgbaImage atlas;
        atlas.width = hldata::kHudAtlasWidth;
        atlas.height = hldata::kHudAtlasHeight;
        atlas.rgba.assign(static_cast<std::size_t>(atlas.width) * atlas.height * 4, 0);
        auto frame = [](const SprFile &spr, int index) { return additiveFrame(spr, index); };
        const RgbaImage sheet7 = frame(hud7, 0), sheet8 = frame(hud8, 0), sheet9 = frame(hud9, 0),
                        cross = frame(crosshairs, 0);
        auto shrink = [&](const RgbaImage &from, int sx, int sy, int w, int h, hldata::UiRect to,
                          bool brightest = false) { shrinkInto(atlas, from, sx, sy, w, h, to, brightest); };
        // Where each is in the 640 set (sprites/hud.txt, weapon_*.txt).
        for (int n = 0; n < 10; ++n)
            shrink(sheet7, 24 * n, 0, 20, 24, hldata::hud640Digit(n));
        shrink(sheet7, 80, 24, 32, 32, hldata::kHud640Cross);
        shrink(sheet7, 0, 24, 40, 40, hldata::kHud640SuitFull);
        shrink(sheet7, 40, 24, 40, 40, hldata::kHud640SuitEmpty);
        shrink(sheet7, 112, 24, 32, 32, hldata::kHud640LampEmpty);
        shrink(sheet7, 160, 24, 32, 32, hldata::kHud640LampFull);
        shrink(sheet7, 144, 24, 16, 32, hldata::kHud640LampBeam);
        for (int n = 0; n < hldata::kHudAmmo640Count; ++n)
            shrink(sheet7, 24 * (n % 6), 72 + 24 * (n / 6), 24, 24, hldata::hud640Ammo(n));
        shrink(cross, 24, 0, 24, 24, hldata::kHud640Crosshair, true);
        // The kinds of hurt, in kHurt* order: the pictures the game shows for
        // them (health.cpp's, one along from "dmg_bio" in hud.txt).
        const struct
        {
            const RgbaImage *sheet;
            int x;
        } kHurts[8] = {{&sheet8, 128}, {&sheet8, 0}, {&sheet9, 64}, {&sheet8, 64},
                       {&sheet9, 128}, {&sheet9, 0}, {&sheet9, 192}, {&sheet8, 192}};
        for (int k = 0; k < 8; ++k)
            shrink(*kHurts[k].sheet, kHurts[k].x, 0, 64, 64, hldata::hud640Hurt(k));
        for (int side = 0; side < 4; ++side)
        {
            const RgbaImage mark = frame(pain, side);
            shrink(mark, 0, 0, mark.width, mark.height, hldata::kHud640Pain[side]);
        }
        for (int y = 0; y < hldata::kHud640Solid.h; ++y)
        {
            for (int x = 0; x < hldata::kHud640Solid.w; ++x)
                std::memset(atlas.at(hldata::kHud640Solid.x + x, hldata::kHud640Solid.y + y), 255, 4);
        }
        return atlas;
    }

    std::vector<std::uint8_t> weaponIcons(const SprFile &hud, int width, int height)
    {
        // Where each weapon's picture is in the sheet, 80 by 20 (the game's
        // sprites/weapon_*.txt say so), in the order of kWeapon*: the
        // crowbar, the pistol, the shotgun, the grenade, the MP5, the .357;
        // the crossbow, the satchel, the tripmine, the snark; the RPG, the
        // gauss, the egon, the hornetgun.
        constexpr int kFrom[hldata::kWeaponIcons][2] = {{0, 0},   {0, 40},   {0, 160},   {160, 0},  {0, 120},
                                                        {0, 80},  {80, 0},   {160, 40},  {160, 80}, {160, 120},
                                                        {80, 40}, {80, 80},  {80, 120},  {80, 160}};
        constexpr int kWide = 80, kHigh = 20;
        const RgbaImage sheet = hud.frames.empty() ? RgbaImage() : spriteFrameImage(hud, 0, false);
        const int words = hldata::iconRowWords(width);
        std::vector<std::uint8_t> out(sizeof(hldata::IconsHeader) +
                                      static_cast<std::size_t>(hldata::kWeaponIcons) * height * words * 4, 0);
        hldata::IconsHeader header{};
        std::memcpy(header.magic, hldata::kIconsMagic, 4);
        header.version = hldata::kIconsVersion;
        header.count = hldata::kWeaponIcons;
        header.width = static_cast<std::uint8_t>(width);
        header.height = static_cast<std::uint8_t>(height);
        std::memcpy(out.data(), &header, sizeof(header));
        auto solid = [&](int px, int py) -> int
        {
            return px >= 0 && py >= 0 && px < sheet.width && py < sheet.height ? sheet.at(px, py)[3] : 0;
        };
        for (int n = 0; n < hldata::kWeaponIcons; ++n)
        {
            // What of its box the picture fills -- a crowbar is long and
            // thin, a pistol sits in one corner of it -- is what is fitted
            // to the icon, in its middle, and never made larger than it is.
            int x0 = kWide, y0 = kHigh, x1 = -1, y1 = -1;
            for (int y = 0; y < kHigh; ++y)
            {
                for (int x = 0; x < kWide; ++x)
                {
                    if (solid(kFrom[n][0] + x, kFrom[n][1] + y) < 48)
                        continue;
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                    y1 = std::max(y1, y);
                }
            }
            if (x1 < x0)
                continue;
            const int boxW = x1 - x0 + 1, boxH = y1 - y0 + 1;
            const double scale = std::min(1.0, std::min(static_cast<double>(width) / boxW, static_cast<double>(height) / boxH));
            const int wide = std::max(1, static_cast<int>(boxW * scale + 0.5)), high = std::max(1, static_cast<int>(boxH * scale + 0.5));
            const int left = (width - wide) / 2, top = (height - high) / 2;
            for (int y = 0; y < high; ++y)
            {
                for (int x = 0; x < wide; ++x)
                {
                    // The part of the picture this pixel is, averaged.
                    const int sx0 = x0 + x * boxW / wide, sx1 = std::max(sx0 + 1, x0 + (x + 1) * boxW / wide);
                    const int sy0 = y0 + y * boxH / high, sy1 = std::max(sy0 + 1, y0 + (y + 1) * boxH / high);
                    int sum = 0, most = 0;
                    for (int sy = sy0; sy < sy1; ++sy)
                    {
                        for (int sx = sx0; sx < sx1; ++sx)
                        {
                            const int a = solid(kFrom[n][0] + sx, kFrom[n][1] + sy);
                            sum += a;
                            most = std::max(most, a);
                        }
                    }
                    // (Thin lines are most of these pictures: a pixel is
                    // as much its brightest part as its average.)
                    const int value = (sum / ((sx1 - sx0) * (sy1 - sy0)) + most) / 2;
                    const std::uint32_t level = value < 40 ? 0u : (value < 100 ? 1u : (value < 170 ? 2u : 3u));
                    const int dx = left + x, dy = top + y;
                    const std::size_t at = sizeof(header) +
                                           ((static_cast<std::size_t>(n) * height + dy) * words + (dx >> 4)) * 4;
                    std::uint32_t word;
                    std::memcpy(&word, out.data() + at, 4);
                    word |= level << ((dx & 15) * 2);
                    std::memcpy(out.data() + at, &word, 4);
                }
            }
        }
        return out;
    }

    std::vector<std::uint8_t> logoAtlas(const std::vector<NamedPicture> &pictures)
    {
        constexpr int kSize = hldata::kLogoSize;
        const int count = std::min(static_cast<int>(pictures.size()), hldata::kLogosMost);
        const int words = hldata::iconRowWords(kSize);
        const std::size_t rows = sizeof(hldata::IconsHeader) + static_cast<std::size_t>(count) * kSize * words * 4;
        std::vector<std::uint8_t> out(rows + static_cast<std::size_t>(count) * hldata::kLogoNameBytes, 0);
        hldata::IconsHeader header{};
        std::memcpy(header.magic, hldata::kIconsMagic, 4);
        header.version = hldata::kIconsVersion;
        header.count = static_cast<std::uint8_t>(count);
        header.width = static_cast<std::uint8_t>(kSize);
        header.height = static_cast<std::uint8_t>(kSize);
        std::memcpy(out.data(), &header, sizeof(header));
        for (int n = 0; n < count; ++n)
        {
            const RgbaImage &picture = pictures[static_cast<std::size_t>(n)].picture;
            const int wide = picture.width, high = picture.height;
            for (int y = 0; y < kSize && wide > 0 && high > 0; ++y)
            {
                for (int x = 0; x < kSize; ++x)
                {
                    // The part of the picture this pixel is, its brightness
                    // averaged (the game's: as much red as green as blue).
                    const int sx0 = x * wide / kSize, sx1 = std::max(sx0 + 1, (x + 1) * wide / kSize);
                    const int sy0 = y * high / kSize, sy1 = std::max(sy0 + 1, (y + 1) * high / kSize);
                    int sum = 0;
                    for (int sy = sy0; sy < sy1; ++sy)
                    {
                        for (int sx = sx0; sx < sx1; ++sx)
                        {
                            const std::uint8_t *c = picture.at(sx, sy);
                            sum += (c[0] + c[1] + c[2]) / 3;
                        }
                    }
                    const int value = sum / ((sx1 - sx0) * (sy1 - sy0));
                    const std::uint32_t level = value < 40 ? 0u : (value < 100 ? 1u : (value < 170 ? 2u : 3u));
                    const std::size_t at =
                        sizeof(header) + ((static_cast<std::size_t>(n) * kSize + y) * words + (x >> 4)) * 4;
                    std::uint32_t word;
                    std::memcpy(&word, out.data() + at, 4);
                    word |= level << ((x & 15) * 2);
                    std::memcpy(out.data() + at, &word, 4);
                }
            }
            const std::string &name = pictures[static_cast<std::size_t>(n)].name;
            std::memcpy(out.data() + rows + static_cast<std::size_t>(n) * hldata::kLogoNameBytes, name.data(),
                        std::min(name.size(), hldata::kLogoNameBytes - 1));
        }
        return out;
    }

    std::vector<std::uint8_t> titlePicture(const SprFile &hud)
    {
        // (Where the two halves are, the game's sprites/hud.txt says.)
        const RgbaImage sheet = hud.frames.empty() ? RgbaImage() : spriteFrameImage(hud, 0, false);
        const int width = hldata::kTitleWidth, height = hldata::kTitleHeight;
        const int words = hldata::iconRowWords(width);
        std::vector<std::uint8_t> out(sizeof(hldata::IconsHeader) + static_cast<std::size_t>(height) * words * 4, 0);
        hldata::IconsHeader header{};
        std::memcpy(header.magic, hldata::kIconsMagic, 4);
        header.version = hldata::kIconsVersion;
        header.count = 1;
        header.width = static_cast<std::uint8_t>(width);
        header.height = static_cast<std::uint8_t>(height);
        std::memcpy(out.data(), &header, sizeof(header));
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                // "HALF-" is 128 wide at (0, 48), "LIFE" 116 at (128, 48).
                const int px = x, py = 48 + y;
                const int value = px < sheet.width && py < sheet.height ? sheet.at(px, py)[3] : 0;
                const std::uint32_t level = value < 40 ? 0u : (value < 100 ? 1u : (value < 170 ? 2u : 3u));
                const std::size_t at = sizeof(header) + (static_cast<std::size_t>(y) * words + (x >> 4)) * 4;
                std::uint32_t word;
                std::memcpy(&word, out.data() + at, 4);
                word |= level << ((x & 15) * 2);
                std::memcpy(out.data() + at, &word, 4);
            }
        }
        return out;
    }

    RgbaImage decalAtlas(const WadFile &decals, int &missing)
    {
        RgbaImage atlas;
        atlas.width = hldata::kDecalAtlasWidth;
        atlas.height = hldata::kDecalAtlasSize;
        atlas.rgba.assign(static_cast<std::size_t>(atlas.width) * atlas.height * 4, 0);
        missing = 0;
        for (int n = 0; n < hldata::kDecalCount; ++n)
        {
            if (hldata::kDecalSources[n] == nullptr)
                continue;
            IndexedImage picture;
            std::string why;
            if (!decals.read(hldata::kDecalSources[n], picture, why) || picture.width == 0 || picture.height == 0)
            {
                ++missing;
                continue;
            }
            // (A decal's palette runs from nothing to its colour: the index
            // is how much of the colour there is.)
            const hldata::UiRect cell = hldata::decalRect(static_cast<std::uint8_t>(n));
            for (int y = 0; y < cell.h; ++y)
            {
                for (int x = 0; x < cell.w; ++x)
                {
                    const std::uint32_t x0 = x * picture.width / cell.w, x1 = std::max(x0 + 1, (x + 1) * picture.width / cell.w);
                    const std::uint32_t y0 = y * picture.height / cell.h, y1 = std::max(y0 + 1, (y + 1) * picture.height / cell.h);
                    std::uint32_t sum = 0;
                    for (std::uint32_t sy = y0; sy < y1; ++sy)
                    {
                        for (std::uint32_t sx = x0; sx < x1; ++sx)
                            sum += picture.pixels[sy * picture.width + sx];
                    }
                    int solid = static_cast<int>(sum / ((x1 - x0) * (y1 - y0)));
                    // (The texture has eight steps of see-through: the
                    // faint spray round a stain, which is most of how big
                    // it looks, would be under the first of them. It is
                    // made a little more of.)
                    solid = std::min(255, solid * 7 / 4);
                    // (Nothing along a cell's edge: a mark does not run on
                    // into its neighbour's.)
                    if (x == 0 || y == 0 || x == cell.w - 1 || y == cell.h - 1)
                        solid = 0;
                    std::uint8_t *px = atlas.at(cell.x + x, cell.y + y);
                    px[0] = px[1] = px[2] = 255;
                    px[3] = static_cast<std::uint8_t>(solid);
                }
            }
        }
        return atlas;
    }

    RgbaImage loadingTexture(const IndexedImage &picture)
    {
        const RgbaImage scaled = resample(toRgba(picture, false), hldata::kLoadingSize, hldata::kLoadingHeight);
        RgbaImage out;
        out.width = out.height = hldata::kLoadingSize;
        out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
        for (std::size_t i = 3; i < out.rgba.size(); i += 4)
            out.rgba[i] = 255;
        std::memcpy(out.rgba.data(), scaled.rgba.data(), scaled.rgba.size());
        return out;
    }

    bool readTga(const std::vector<std::uint8_t> &bytes, RgbaImage &out, std::string &error)
    {
        if (bytes.size() < 18)
        {
            error = "too short for a TGA picture";
            return false;
        }
        const int idLength = bytes[0], mapType = bytes[1], type = bytes[2];
        const int width = bytes[12] | bytes[13] << 8, height = bytes[14] | bytes[15] << 8;
        const int bits = bytes[16], descriptor = bytes[17];
        if (mapType != 0 || (type != 2 && type != 10) || (bits != 24 && bits != 32) || width <= 0 || height <= 0)
        {
            error = "a TGA picture of a kind not read";
            return false;
        }
        const int bpp = bits / 8;
        out.width = width;
        out.height = height;
        out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);
        std::size_t at = 18 + static_cast<std::size_t>(idLength);
        const bool topFirst = (descriptor & 0x20) != 0;
        const int count = width * height;
        auto store = [&](int n, const std::uint8_t *p)
        {
            const int x = n % width, row = n / width;
            const int y = topFirst ? row : height - 1 - row;
            std::uint8_t *q = out.at(x, y);
            q[0] = p[2];
            q[1] = p[1];
            q[2] = p[0];
            q[3] = bpp == 4 ? p[3] : 255;
        };
        int n = 0;
        while (n < count)
        {
            if (type == 2)
            {
                if (at + static_cast<std::size_t>(bpp) > bytes.size())
                    break;
                store(n++, &bytes[at]);
                at += static_cast<std::size_t>(bpp);
                continue;
            }
            if (at >= bytes.size())
                break;
            const int packet = bytes[at++];
            const int run = (packet & 0x7F) + 1;
            if (packet & 0x80)
            {
                if (at + static_cast<std::size_t>(bpp) > bytes.size())
                    break;
                for (int k = 0; k < run && n < count; ++k)
                    store(n++, &bytes[at]);
                at += static_cast<std::size_t>(bpp);
            }
            else
            {
                for (int k = 0; k < run && n < count; ++k)
                {
                    if (at + static_cast<std::size_t>(bpp) > bytes.size())
                        break;
                    store(n++, &bytes[at]);
                    at += static_cast<std::size_t>(bpp);
                }
            }
        }
        if (n < count)
        {
            error = "a TGA picture cut short";
            return false;
        }
        return true;
    }

    namespace
    {
        // A background layout: "resolution W H", then "file  fit  x  y" a
        // line. The picture it puts together.
        bool layoutPicture(const std::function<std::filesystem::path(const std::string &)> &find,
                           const std::string &layout, RgbaImage &out, std::string &error)
        {
            const std::filesystem::path file = find(layout);
            std::vector<std::uint8_t> text;
            if (file.empty() || !readFile(file, text, error))
            {
                error = "no " + layout;
                return false;
            }
            std::string all(text.begin(), text.end());
            std::size_t start = 0;
            int width = 0, height = 0, tiles = 0;
            while (start < all.size())
            {
                std::size_t end = all.find('\n', start);
                if (end == std::string::npos)
                    end = all.size();
                std::string line = all.substr(start, end - start);
                start = end + 1;
                std::vector<std::string> words;
                std::size_t w = 0;
                while (w < line.size())
                {
                    while (w < line.size() && std::isspace(static_cast<unsigned char>(line[w])))
                        ++w;
                    std::size_t e = w;
                    while (e < line.size() && !std::isspace(static_cast<unsigned char>(line[e])))
                        ++e;
                    if (e > w)
                        words.push_back(line.substr(w, e - w));
                    w = e;
                }
                if (words.size() >= 3 && words[0] == "resolution")
                {
                    width = std::atoi(words[1].c_str());
                    height = std::atoi(words[2].c_str());
                    if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
                        return false;
                    out.width = width;
                    out.height = height;
                    out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
                    for (std::size_t i = 3; i < out.rgba.size(); i += 4)
                        out.rgba[i] = 255;
                    continue;
                }
                if (words.size() < 4 || width == 0)
                    continue;
                const std::filesystem::path tilePath = find(words[0]);
                std::vector<std::uint8_t> bytes;
                RgbaImage tile;
                std::string why;
                if (tilePath.empty() || !readFile(tilePath, bytes, why) || !readTga(bytes, tile, why))
                    continue;
                const int x0 = std::atoi(words[2].c_str()), y0 = std::atoi(words[3].c_str());
                for (int y = 0; y < tile.height; ++y)
                    for (int x = 0; x < tile.width; ++x)
                        if (x0 + x >= 0 && x0 + x < width && y0 + y >= 0 && y0 + y < height)
                            std::memcpy(out.at(x0 + x, y0 + y), tile.at(x, y), 4);
                ++tiles;
            }
            if (tiles == 0)
            {
                error = "no tiles of " + layout;
                return false;
            }
            return true;
        }

        bool allBlack(const RgbaImage &image)
        {
            for (std::size_t i = 0; i < image.rgba.size(); i += 4)
                if (image.rgba[i] | image.rgba[i + 1] | image.rgba[i + 2])
                    return false;
            return true;
        }
    } // namespace

    bool menuBackground(const std::function<std::filesystem::path(const std::string &)> &find, RgbaImage &out,
                        std::string &error)
    {
        RgbaImage whole;
        RgbaImage piece;
        std::string why;
        if (layoutPicture(find, "resource/HD_BackgroundLayout.txt", whole, why) && !allBlack(whole))
        {
            // A 4:3 piece of the wide picture, around the lambda (three
            // fifths of the way across, as the game's art has it).
            const int pieceWidth = std::min(whole.width, whole.height * 4 / 3);
            int x0 = whole.width * 61 / 100 - pieceWidth / 2;
            x0 = std::max(0, std::min(whole.width - pieceWidth, x0));
            piece.width = pieceWidth;
            piece.height = whole.height;
            piece.rgba.resize(static_cast<std::size_t>(pieceWidth) * whole.height * 4);
            for (int y = 0; y < whole.height; ++y)
                std::memcpy(piece.at(0, y), whole.at(x0, y), static_cast<std::size_t>(pieceWidth) * 4);
        }
        else if (layoutPicture(find, "resource/BackgroundLayout.txt", whole, why) && !allBlack(whole))
        {
            piece = whole;
        }
        else
        {
            error = why.empty() ? "the menu's background is all black" : why;
            return false;
        }
        const RgbaImage scaled = resample(piece, hldata::kLoadingSize, hldata::kLoadingHeight);
        out.width = out.height = hldata::kLoadingSize;
        out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
        for (std::size_t i = 3; i < out.rgba.size(); i += 4)
            out.rgba[i] = 255;
        std::memcpy(out.rgba.data(), scaled.rgba.data(), scaled.rgba.size());
        return true;
    }

    std::vector<std::uint8_t> logoFile(const std::vector<RgbaImage> &frames, std::uint32_t frameMicros)
    {
        hldata::LogoHeader header{};
        std::memcpy(header.magic, hldata::kLogoMagic, 4);
        header.version = hldata::kLogoVersion;
        header.frames = static_cast<std::uint16_t>(frames.size());
        header.width = hldata::kLogoWidth;
        header.height = hldata::kLogoHeight;
        header.frameMicros = frameMicros;
        std::vector<std::uint8_t> out(sizeof(header));
        std::memcpy(out.data(), &header, sizeof(header));
        for (const RgbaImage &frame : frames)
        {
            const RgbaImage small = resample(frame, hldata::kLogoWidth, hldata::kLogoHeight);
            for (int y = 0; y < hldata::kLogoHeight; ++y)
            {
                for (int x = 0; x < hldata::kLogoWidth; ++x)
                {
                    const std::uint8_t *p = small.at(x, y);
                    // (How bright: its alpha, three bits, and its grey, five.)
                    const int light = std::max({p[0], p[1], p[2]});
                    out.push_back(static_cast<std::uint8_t>((light >> 5) << 5 | (light >> 3)));
                }
            }
        }
        return out;
    }
} // namespace hlds
