#include "hlds/SpriteCook.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

#include "HlSprite.hpp"
#include "HlUi.hpp"

namespace hlds
{
    namespace
    {
        struct Reader
        {
            const std::vector<std::uint8_t> &bytes;
            std::size_t at = 0;
            bool ok = true;

            std::int32_t i32()
            {
                if (at + 4 > bytes.size())
                {
                    ok = false;
                    return 0;
                }
                std::uint32_t v;
                std::memcpy(&v, bytes.data() + at, 4);
                at += 4;
                return static_cast<std::int32_t>(v);
            }
            int u16()
            {
                if (at + 2 > bytes.size())
                {
                    ok = false;
                    return 0;
                }
                const int v = bytes[at] | (bytes[at + 1] << 8);
                at += 2;
                return v;
            }
        };

        int powerOfTwoUpTo(int value, int most)
        {
            int p = 1;
            while (p * 2 <= value && p * 2 <= most)
                p *= 2;
            return p;
        }
        int powerOfTwoOver(int value)
        {
            int p = 1;
            while (p < value)
                p *= 2;
            return p;
        }
    } // namespace

    bool SprFile::load(const std::filesystem::path &path, std::string &error)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            error = path.filename().string() + " cannot be read";
            return false;
        }
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!parse(bytes, error))
        {
            error = path.filename().string() + ": " + error;
            return false;
        }
        return true;
    }

    bool SprFile::parse(const std::vector<std::uint8_t> &bytes, std::string &error)
    {
        *this = SprFile();
        if (bytes.size() < 42 || std::memcmp(bytes.data(), "IDSP", 4) != 0)
        {
            error = "not a sprite";
            return false;
        }
        Reader r{bytes};
        r.at = 4;
        if (r.i32() != 2)
        {
            error = "not a version 2 sprite";
            return false;
        }
        type = r.i32();
        format = r.i32();
        r.i32(); // its bounding radius
        width = r.i32();
        height = r.i32();
        const int frameCount = r.i32();
        r.i32(); // beam length
        r.i32(); // how its animation is timed
        const int colours = r.u16();
        if (!r.ok || width <= 0 || height <= 0 || width > 1024 || height > 1024 || frameCount <= 0 ||
            frameCount > 1024 || colours <= 0 || colours > 256 || r.at + colours * 3u > bytes.size())
        {
            error = "an impossible header";
            return false;
        }
        palette.resize(256, {0, 0, 0});
        for (int c = 0; c < colours; ++c)
        {
            palette[c] = {bytes[r.at], bytes[r.at + 1], bytes[r.at + 2]};
            r.at += 3;
        }
        for (int f = 0; f < frameCount; ++f)
        {
            // (A group of frames with their own timings: nothing in the game
            // has one. Its first frame is taken as the frame.)
            const int group = r.i32();
            int inGroup = 1;
            if (group != 0)
            {
                inGroup = r.i32();
                if (!r.ok || inGroup <= 0 || inGroup > 256)
                {
                    error = "an impossible group of frames";
                    return false;
                }
                r.at += static_cast<std::size_t>(inGroup) * 4; // their intervals
            }
            for (int g = 0; g < inGroup; ++g)
            {
                SprFrame frame;
                frame.originX = r.i32();
                frame.originY = r.i32();
                frame.width = r.i32();
                frame.height = r.i32();
                if (!r.ok || frame.width <= 0 || frame.height <= 0 || frame.width > 1024 ||
                    frame.height > 1024 ||
                    r.at + static_cast<std::size_t>(frame.width) * frame.height > bytes.size())
                {
                    error = "a frame runs off the end of the file";
                    return false;
                }
                frame.pixels.assign(bytes.begin() + r.at, bytes.begin() + r.at + frame.width * frame.height);
                r.at += static_cast<std::size_t>(frame.width) * frame.height;
                if (g == 0)
                    frames.push_back(std::move(frame));
            }
        }
        return true;
    }

    RgbaImage spriteFrameImage(const SprFile &spr, int index, bool light)
    {
        RgbaImage image;
        image.width = spr.width;
        image.height = spr.height;
        image.rgba.assign(static_cast<std::size_t>(spr.width) * spr.height * 4, 0);
        const SprFrame &frame = spr.frames[index];
        const int format = light && spr.format != kSprIndexAlpha ? kSprAdditive : spr.format;
        const int left = spr.width / 2 + frame.originX;
        const int top = spr.height / 2 - frame.originY;
        for (int y = 0; y < frame.height; ++y)
        {
            for (int x = 0; x < frame.width; ++x)
            {
                const int px = left + x, py = top + y;
                if (px < 0 || py < 0 || px >= spr.width || py >= spr.height)
                    continue;
                const int i = frame.pixels[y * frame.width + x];
                int r = spr.palette[i][0], g = spr.palette[i][1], b = spr.palette[i][2], a = 255;
                if (format == kSprAdditive)
                {
                    // How bright it is becomes how solid it is, and it is
                    // made as bright as its colour goes (HlSprite.hpp).
                    a = std::max(r, std::max(g, b));
                    if (a > 0)
                    {
                        r = r * 255 / a;
                        g = g * 255 / a;
                        b = b * 255 / a;
                    }
                }
                else if (format == kSprIndexAlpha)
                {
                    a = i;
                    r = spr.palette[255][0];
                    g = spr.palette[255][1];
                    b = spr.palette[255][2];
                }
                else if (format == kSprAlphaTest && i == 255)
                {
                    a = 0;
                }
                std::uint8_t *out = image.at(px, py);
                out[0] = static_cast<std::uint8_t>(r);
                out[1] = static_cast<std::uint8_t>(g);
                out[2] = static_cast<std::uint8_t>(b);
                out[3] = static_cast<std::uint8_t>(a);
            }
        }
        return image;
    }

    namespace
    {
        /// Shrink to `width` by `height`, each texel the average of the
        /// pixels it covers: colour weighed by how solid each was, so that
        /// what is see-through does not darken what is not.
        RgbaImage shrink(const RgbaImage &image, int width, int height)
        {
            RgbaImage out;
            out.width = width;
            out.height = height;
            out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
            for (int y = 0; y < height; ++y)
            {
                const int y0 = y * image.height / height;
                const int y1 = std::max(y0 + 1, (y + 1) * image.height / height);
                for (int x = 0; x < width; ++x)
                {
                    const int x0 = x * image.width / width;
                    const int x1 = std::max(x0 + 1, (x + 1) * image.width / width);
                    long sum[3] = {0, 0, 0}, alpha = 0, count = 0;
                    for (int sy = y0; sy < y1 && sy < image.height; ++sy)
                    {
                        for (int sx = x0; sx < x1 && sx < image.width; ++sx)
                        {
                            const std::uint8_t *p = image.at(sx, sy);
                            for (int c = 0; c < 3; ++c)
                                sum[c] += static_cast<long>(p[c]) * p[3];
                            alpha += p[3];
                            ++count;
                        }
                    }
                    std::uint8_t *o = out.at(x, y);
                    if (alpha > 0)
                    {
                        for (int c = 0; c < 3; ++c)
                            o[c] = static_cast<std::uint8_t>(sum[c] / alpha);
                    }
                    o[3] = static_cast<std::uint8_t>(count > 0 ? alpha / count : 0);
                }
            }
            return out;
        }
    } // namespace

    bool cookSprite(const SprFile &spr, int maxCell, int maxFrames, bool light, CookedSprite &out,
                    std::string &error)
    {
        out = CookedSprite();
        if (spr.frames.empty())
        {
            error = "a sprite with no frames";
            return false;
        }
        maxCell = std::clamp(maxCell, 8, 64);
        // A frame's cell: powers of two, the longer side at most maxCell,
        // the other in proportion (a beam's picture is a strip).
        const int longest = std::max(spr.width, spr.height);
        const int scaled = powerOfTwoUpTo(longest, maxCell);
        int cellWidth = std::max(4, powerOfTwoUpTo(std::max(1, spr.width * scaled / longest), maxCell));
        int cellHeight = std::max(4, powerOfTwoUpTo(std::max(1, spr.height * scaled / longest), maxCell));
        const int frames = std::clamp(std::min<int>(maxFrames, static_cast<int>(spr.frames.size())), 1, 64);
        // As many to a row as an atlas 128 wide takes, and no more rows than
        // one 128 high.
        int columns = std::min(powerOfTwoOver(frames), 128 / cellWidth);
        while ((frames + columns - 1) / columns * cellHeight > 128 && cellHeight > 4)
        {
            cellHeight /= 2;
            if (cellWidth > 4)
                cellWidth /= 2;
            columns = std::min(powerOfTwoOver(frames), 128 / cellWidth);
        }
        const int rows = (frames + columns - 1) / columns;
        RgbaImage atlas;
        atlas.width = std::max(16, powerOfTwoOver(columns * cellWidth));
        atlas.height = std::max(16, powerOfTwoOver(rows * cellHeight));
        atlas.rgba.assign(static_cast<std::size_t>(atlas.width) * atlas.height * 4, 0);
        for (int f = 0; f < frames; ++f)
        {
            // Evenly through the animation, the first and the last kept.
            const int source = frames == 1 ? 0
                                           : static_cast<int>((static_cast<long>(f) * (spr.frames.size() - 1) +
                                                               (frames - 1) / 2) /
                                                              (frames - 1));
            const RgbaImage cell = shrink(spriteFrameImage(spr, source, light), cellWidth, cellHeight);
            const int ox = (f % columns) * cellWidth, oy = (f / columns) * cellHeight;
            for (int y = 0; y < cellHeight; ++y)
                std::memcpy(atlas.at(ox, oy + y), cell.at(0, y), static_cast<std::size_t>(cellWidth) * 4);
        }
        // A faint one (the game has glows that are barely there, and makes
        // them more or less so by how it draws them) is brought up to full
        // strength: with eight levels of see-through it would otherwise be
        // nothing at all. How faint it is drawn is the map's to say.
        int most = 0;
        for (std::size_t i = 3; i < atlas.rgba.size(); i += 4)
            most = std::max<int>(most, atlas.rgba[i]);
        if (most == 0)
        {
            // (The game has sprites that are all black: placeholders.)
            error = "there is nothing in it to see";
            return false;
        }
        if (most > 0 && most < 224)
        {
            for (std::size_t i = 3; i < atlas.rgba.size(); i += 4)
                atlas.rgba[i] = static_cast<std::uint8_t>(atlas.rgba[i] * 255 / most);
        }
        out.atlas = std::move(atlas);
        out.ref.width = static_cast<std::uint16_t>(out.atlas.width);
        out.ref.height = static_cast<std::uint16_t>(out.atlas.height);
        out.ref.cellWidth = static_cast<std::uint8_t>(cellWidth);
        out.ref.cellHeight = static_cast<std::uint8_t>(cellHeight);
        out.ref.columns = static_cast<std::uint8_t>(columns);
        out.ref.frames = static_cast<std::uint8_t>(frames);
        out.ref.sourceFrames = static_cast<std::uint8_t>(std::min<std::size_t>(spr.frames.size(), 255));
        out.ref.worldWidth = static_cast<std::uint16_t>(spr.width);
        out.ref.worldHeight = static_cast<std::uint16_t>(spr.height);
        // The game's types: 0 and 2 are turned to the eye (2 by far the
        // most); 1 keeps upright; 3 and 4 lie as their entity is turned.
        out.ref.kind = spr.type == 1 ? hldata::kSpriteUpright
                                     : (spr.type == 3 || spr.type == 4 ? hldata::kSpriteOriented
                                                                       : hldata::kSpriteFacesEye);
        return true;
    }

    std::string spriteName(const std::string &path)
    {
        const std::size_t slash = path.find_last_of("/\\");
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        const std::size_t dot = name.rfind('.');
        if (dot != std::string::npos)
            name.erase(dot);
        for (char &c : name)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
                c = '_';
        }
        if (name.size() > 12)
            name.resize(12);
        return name;
    }

    bool signSprite(const IndexedImage &picture, SprFile &out)
    {
        out = SprFile();
        if (picture.width == 0 || picture.height == 0 || picture.width > 1024 || picture.height > 1024 ||
            picture.pixels.size() != static_cast<std::size_t>(picture.width) * picture.height ||
            picture.palette.size() < 256 * 3)
            return false;
        out.type = 3; // (oriented: it lies as its wall does)
        out.format = kSprIndexAlpha;
        out.width = static_cast<int>(picture.width);
        out.height = static_cast<int>(picture.height);
        out.palette.resize(256);
        for (int c = 0; c < 256; ++c)
            out.palette[c] = {picture.palette[c * 3], picture.palette[c * 3 + 1], picture.palette[c * 3 + 2]};
        SprFrame frame;
        frame.width = out.width;
        frame.height = out.height;
        frame.originX = -out.width / 2;
        frame.originY = out.height / 2;
        frame.pixels = picture.pixels;
        out.frames.push_back(std::move(frame));
        return true;
    }

    std::string signSpriteName(const std::string &path)
    {
        const std::string sign = hldata::kDecalSignPath;
        if (path.rfind(sign, 0) != 0)
            return spriteName(path);
        std::string name = "d-";
        for (std::size_t i = sign.size(); i < path.size() && path[i] != '.'; ++i)
        {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(path[i])));
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-')
                name += c;
        }
        if (name.size() > 20)
            name.resize(20);
        return name;
    }
} // namespace hlds
