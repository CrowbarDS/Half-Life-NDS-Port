#include "hlds/TextureImage.hpp"

#include <algorithm>
#include <cmath>

#include "HlLevel.hpp"
#include "hlds/AtomicWrite.hpp"

namespace hlds
{
    std::uint8_t brightened(std::uint8_t value)
    {
        return static_cast<std::uint8_t>(std::lround(255.0 * std::pow(value / 255.0, kTextureGamma)));
    }
    void brighten(RgbaImage &image)
    {
        std::uint8_t table[256];
        for (int i = 0; i < 256; ++i)
            table[i] = brightened(static_cast<std::uint8_t>(i));
        for (std::size_t i = 0; i < image.rgba.size(); ++i)
        {
            if ((i & 3) != 3)
                image.rgba[i] = table[image.rgba[i]];
        }
    }

    RgbaImage toRgba(const IndexedImage &image, bool colourKey)
    {
        RgbaImage out;
        out.width = static_cast<int>(image.width);
        out.height = static_cast<int>(image.height);
        out.rgba.resize(image.pixels.size() * 4);
        for (std::size_t i = 0; i < image.pixels.size(); ++i)
        {
            const std::uint8_t index = image.pixels[i];
            const bool hole = colourKey && index == 255;
            out.rgba[i * 4 + 0] = hole ? 0 : image.palette[index * 3 + 0];
            out.rgba[i * 4 + 1] = hole ? 0 : image.palette[index * 3 + 1];
            out.rgba[i * 4 + 2] = hole ? 0 : image.palette[index * 3 + 2];
            out.rgba[i * 4 + 3] = hole ? 0 : 255;
        }
        return out;
    }

    RgbaImage resample(const RgbaImage &image, int width, int height, bool softAlpha)
    {
        RgbaImage out;
        out.width = width;
        out.height = height;
        out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);

        const double sx = static_cast<double>(image.width) / width;
        const double sy = static_cast<double>(image.height) / height;
        for (int y = 0; y < height; ++y)
        {
            const double y0 = y * sy;
            const double y1 = (y + 1) * sy;
            for (int x = 0; x < width; ++x)
            {
                const double x0 = x * sx;
                const double x1 = (x + 1) * sx;
                double sum[3] = {0, 0, 0};
                double opaque = 0;
                double total = 0;
                // Every source pixel the destination pixel's footprint
                // touches, weighted by how much of it is covered. When
                // enlarging, the footprint lies inside one source pixel and
                // this reduces to nearest-neighbour.
                for (int v = static_cast<int>(std::floor(y0));
                     v < std::min(image.height, static_cast<int>(std::ceil(y1))); ++v)
                {
                    const double wy = std::min<double>(y1, v + 1) - std::max<double>(y0, v);
                    for (int u = static_cast<int>(std::floor(x0));
                         u < std::min(image.width, static_cast<int>(std::ceil(x1))); ++u)
                    {
                        const double wx = std::min<double>(x1, u + 1) - std::max<double>(x0, u);
                        const double weight = wx * wy;
                        if (weight <= 0)
                            continue;
                        const std::uint8_t *p = image.at(u, v);
                        total += weight;
                        if (p[3] != 0)
                        {
                            opaque += weight;
                            for (int c = 0; c < 3; ++c)
                                sum[c] += p[c] * weight;
                        }
                    }
                }
                std::uint8_t *dst = out.at(x, y);
                const double coverage = total > 0 ? opaque / total : 0;
                const int alpha = softAlpha ? static_cast<int>(std::lround(coverage * 255))
                                            : (coverage >= 0.5 ? 255 : 0);
                if (alpha > 0 && opaque > 0)
                {
                    for (int c = 0; c < 3; ++c)
                        dst[c] = static_cast<std::uint8_t>(std::lround(sum[c] / opaque));
                    dst[3] = static_cast<std::uint8_t>(alpha);
                }
            }
        }
        return out;
    }

    void lightToAlpha(RgbaImage &image)
    {
        for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4)
        {
            std::uint8_t *p = &image.rgba[i];
            const int most = p[3] == 0 ? 0 : std::max({p[0], p[1], p[2]});
            if (most == 0)
            {
                p[0] = p[1] = p[2] = p[3] = 0;
                continue;
            }
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<std::uint8_t>((p[c] * 255 + most / 2) / most);
            // (Under a texel that was partly there -- an edge made smaller --
            // the share of it that was.)
            p[3] = static_cast<std::uint8_t>((most * p[3] + 127) / 255);
        }
    }

    bool writePng(const std::filesystem::path &path, const RgbaImage &image, std::string &error)
    {
        std::vector<std::uint8_t> file = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        auto be32 = [](std::vector<std::uint8_t> &v, std::uint32_t x)
        {
            for (int s = 24; s >= 0; s -= 8)
                v.push_back(static_cast<std::uint8_t>(x >> s));
        };
        auto chunk = [&](const char *type, const std::vector<std::uint8_t> &data)
        {
            be32(file, static_cast<std::uint32_t>(data.size()));
            std::vector<std::uint8_t> body(type, type + 4);
            body.insert(body.end(), data.begin(), data.end());
            file.insert(file.end(), body.begin(), body.end());
            be32(file, hldata::crc32(body.data(), static_cast<std::uint32_t>(body.size())));
        };

        std::vector<std::uint8_t> header;
        be32(header, static_cast<std::uint32_t>(image.width));
        be32(header, static_cast<std::uint32_t>(image.height));
        header.insert(header.end(), {8, 6, 0, 0, 0}); // 8 bits, RGBA, no interlace
        chunk("IHDR", header);

        // The scanlines, each led by filter type 0, wrapped in a zlib stream
        // of stored (uncompressed) blocks.
        std::vector<std::uint8_t> raw;
        for (int y = 0; y < image.height; ++y)
        {
            raw.push_back(0);
            raw.insert(raw.end(), image.at(0, y), image.at(0, y) + image.width * 4);
        }
        std::vector<std::uint8_t> zlib = {0x78, 0x01};
        std::size_t pos = 0;
        do
        {
            const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
            zlib.push_back(pos + n == raw.size() ? 1 : 0);
            zlib.push_back(static_cast<std::uint8_t>(n));
            zlib.push_back(static_cast<std::uint8_t>(n >> 8));
            zlib.push_back(static_cast<std::uint8_t>(~n));
            zlib.push_back(static_cast<std::uint8_t>(~n >> 8));
            zlib.insert(zlib.end(), raw.begin() + pos, raw.begin() + pos + n);
            pos += n;
        } while (pos < raw.size());
        std::uint32_t a = 1, b = 0; // Adler-32
        for (std::uint8_t byte : raw)
        {
            a = (a + byte) % 65521;
            b = (b + a) % 65521;
        }
        be32(zlib, (b << 16) | a);
        chunk("IDAT", zlib);
        chunk("IEND", {});
        return writeFileAtomic(path, file, error);
    }
} // namespace hlds
