#include "hlds/TextureCook.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>

#include "HlLevel.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/UiCook.hpp"

namespace hlds
{
    using hldata::TextureHeader;

    std::vector<std::uint8_t> packTexture(const CookedTexture &texture, std::uint32_t flags,
                                          std::uint32_t sourceHash)
    {
        TextureHeader header{};
        std::memcpy(header.magic, hldata::kTextureMagic, sizeof(header.magic));
        header.version = hldata::kTextureVersion;
        header.width = static_cast<std::uint16_t>(texture.width);
        header.height = static_cast<std::uint16_t>(texture.height);
        header.paletteColours = static_cast<std::uint16_t>(texture.paletteColours());
        header.texelBytes = static_cast<std::uint32_t>(texture.texels.size());
        header.indexBytes = static_cast<std::uint32_t>(texture.index.size());
        header.paletteBytes = (static_cast<std::uint32_t>(texture.palette.size()) + 3) & ~3u;
        // The format is the texture's own; a caller cannot contradict it.
        header.flags = (flags & ~hldata::kTextureA3I5) |
                       (texture.format == TextureFormat::A3I5 ? hldata::kTextureA3I5 : 0u);
        header.sourceHash = sourceHash;

        std::vector<std::uint8_t> out(sizeof(header));
        std::memcpy(out.data(), &header, sizeof(header));
        out.insert(out.end(), texture.texels.begin(), texture.texels.end());
        out.insert(out.end(), texture.index.begin(), texture.index.end());
        out.insert(out.end(), texture.palette.begin(), texture.palette.end());
        out.resize(sizeof(header) + header.texelBytes + header.indexBytes + header.paletteBytes, 0);
        return out;
    }

    bool unpackTexture(const std::vector<std::uint8_t> &bytes, CookedTexture &texture,
                       TextureHeader &header)
    {
        if (bytes.size() < sizeof(header))
            return false;
        std::memcpy(&header, bytes.data(), sizeof(header));
        if (!hldata::textureHeaderValid(header, static_cast<std::uint32_t>(bytes.size())))
            return false;
        const std::uint8_t *p = bytes.data() + sizeof(header);
        texture.width = header.width;
        texture.height = header.height;
        texture.format = (header.flags & hldata::kTextureA3I5) ? TextureFormat::A3I5
                                                               : TextureFormat::Tex4x4;
        texture.texels.assign(p, p + header.texelBytes);
        p += header.texelBytes;
        texture.index.assign(p, p + header.indexBytes);
        p += header.indexBytes;
        texture.palette.assign(p, p + header.paletteColours * 2);
        return true;
    }

    namespace
    {
        struct Job
        {
            std::string name;
            std::filesystem::path path;
            RgbaImage image;
            int colours = 0;
            TextureFormat format = TextureFormat::Tex4x4;
            std::uint32_t flags = 0;
            std::uint32_t hash = 0;
            std::string error;
            bool ok = false;
        };
    } // namespace

    bool cookTextures(const BspFile &bsp, const WadSet &wads, const TexturePlan &plan,
                      ITextureEncoder &encoder, const std::filesystem::path &texDir,
                      TextureCookReport &report, std::string &error)
    {
        report = TextureCookReport();

        // First, in order and on this thread: find each texture's pixels and
        // decide whether the card already has it. What is left is a list of
        // independent jobs.
        std::vector<Job> jobs;
        for (std::size_t t = 0; t < bsp.textures.size(); ++t)
        {
            if (plan.sizes[t].width == 0 && plan.alphaSizes[t].width == 0)
                continue;
            const BspTexture &tex = bsp.textures[t];

            IndexedImage source;
            if (tex.embedded)
            {
                source.width = tex.width;
                source.height = tex.height;
                source.pixels = tex.pixels;
                source.palette = tex.palette;
            }
            else
            {
                std::string ignored;
                if (!wads.read(tex.name, source, ignored))
                {
                    report.missing.push_back(tex.name);
                    continue;
                }
            }

            const bool keyed = !tex.name.empty() && tex.name[0] == '{';
            // Each form the plan asks for. In the opaque form a '{' texture's
            // index 255 is just a colour, as it is on a wall in the game; in
            // the A3I5 form it is a hole.
            for (int form = 0; form < 2; ++form)
            {
                const bool a3i5 = form == 1;
                const TextureSize size = a3i5 ? plan.alphaSizes[t] : plan.sizes[t];
                if (size.width == 0)
                    continue;
                const int colours = a3i5 ? hldata::kA3I5Colours : plan.paletteColours[t];

                // Everything the output depends on.
                std::uint32_t hash = hldata::crc32(
                    source.pixels.data(), static_cast<std::uint32_t>(source.pixels.size()));
                hash ^= hldata::crc32(source.palette.data(),
                                      static_cast<std::uint32_t>(source.palette.size())) *
                        31u;
                const std::uint32_t settings[] = {source.width,
                                                  source.height,
                                                  size.width,
                                                  size.height,
                                                  static_cast<std::uint32_t>(colours),
                                                  a3i5 ? 1u : 0u,
                                                  encoder.version()};
                hash ^= hldata::crc32(reinterpret_cast<const std::uint8_t *>(settings),
                                      sizeof(settings)) *
                        131u;

                char fileName[40];
                hldata::textureFileName(tex.name.c_str(), size.width, size.height, a3i5,
                                        fileName);
                const std::filesystem::path path = texDir / fileName;

                std::vector<std::uint8_t> existing;
                std::string ignored;
                CookedTexture old;
                TextureHeader oldHeader;
                if (readFile(path, existing, ignored) &&
                    unpackTexture(existing, old, oldHeader) && oldHeader.sourceHash == hash)
                {
                    ++report.reused;
                    continue;
                }

                Job job;
                job.name = tex.name;
                job.path = path;
                job.image = resample(toRgba(source, a3i5), size.width, size.height, a3i5);
                brighten(job.image);
                job.colours = colours;
                job.format = a3i5 ? TextureFormat::A3I5 : TextureFormat::Tex4x4;
                job.flags = (keyed ? hldata::kTextureColourKey : 0u) |
                            (a3i5 ? hldata::kTextureA3I5 : 0u);
                job.hash = hash;
                jobs.push_back(std::move(job));
            }
        }

        // Then the encoding, which is nearly all of a cook's time and is one
        // external process per texture: spread over the machine's cores. Each
        // worker takes the next job off a shared counter.
        std::atomic<std::size_t> next{0};
        auto worker = [&]()
        {
            for (;;)
            {
                const std::size_t i = next.fetch_add(1);
                if (i >= jobs.size())
                    return;
                Job &job = jobs[i];
                CookedTexture encoded;
                if (!encoder.encode(job.image, job.colours, job.format, encoded, job.error))
                    continue;
                job.ok = writeFileAtomic(job.path, packTexture(encoded, job.flags, job.hash),
                                         job.error);
            }
        };
        // (No more than may encode at once: a thread more would only wait.)
        const unsigned threads =
            std::max(1u, std::min<unsigned>(encodeThreads(), static_cast<unsigned>(jobs.size())));
        std::vector<std::thread> pool;
        for (unsigned i = 1; i < threads; ++i)
            pool.emplace_back(worker);
        worker();
        for (std::thread &thread : pool)
            thread.join();

        for (const Job &job : jobs)
        {
            if (!job.ok)
            {
                error = "texture '" + job.name + "': " + job.error;
                return false;
            }
            ++report.encoded;
        }
        return true;
    }

    /// An 8-bit, uncompressed Windows bitmap: what the game's sky
    /// pictures are.
    bool readBmp8(const std::filesystem::path &path, RgbaImage &out, std::string &error)
    {
        std::vector<std::uint8_t> bytes;
        if (!readFile(path, bytes, error))
            return false;
        auto u32 = [&](std::size_t at) -> std::uint32_t
        {
            return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) |
                   (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
        };
        if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M')
        {
            error = path.filename().string() + " is not a bitmap";
            return false;
        }
        const std::uint32_t pixels = u32(10);
        const std::uint32_t headerSize = u32(14);
        const auto width = static_cast<std::int32_t>(u32(18));
        const auto rawHeight = static_cast<std::int32_t>(u32(22));
        const int height = rawHeight < 0 ? -rawHeight : rawHeight;
        const int bits = bytes[28] | (bytes[29] << 8);
        const std::uint32_t compression = u32(30);
        const std::size_t palette = 14 + headerSize;
        const std::size_t stride = (static_cast<std::size_t>(width) + 3) & ~std::size_t(3);
        if (bits != 8 || compression != 0 || width <= 0 || height <= 0 || width > 1024 ||
            height > 1024 || palette + 1024 > bytes.size() ||
            pixels + stride * height > bytes.size())
        {
            error = path.filename().string() + " is not an 8-bit uncompressed bitmap";
            return false;
        }
        out.width = width;
        out.height = height;
        out.rgba.resize(static_cast<std::size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
        {
            // Rows are stored bottom to top unless the height is negative.
            const std::size_t row = pixels + stride * (rawHeight < 0 ? y : height - 1 - y);
            for (int x = 0; x < width; ++x)
            {
                const std::uint8_t *colour = &bytes[palette + bytes[row + x] * 4];
                std::uint8_t *to = out.at(x, y);
                to[0] = colour[2];
                to[1] = colour[1];
                to[2] = colour[0];
                to[3] = 255;
            }
        }
        return true;
    }

    namespace
    {
        /// A side's picture: the 8-bit bitmap most skies have, else the TGA
        /// some have only (xen8, xen10...), as the game looks for both.
        std::filesystem::path skyPath(const std::filesystem::path &gameDir, const std::string &name,
                                      const char *side)
        {
            const std::filesystem::path folder = gameDir / "gfx" / "env";
            const std::filesystem::path bmp = folder / (name + side + ".bmp");
            std::error_code ec;
            if (std::filesystem::is_regular_file(bmp, ec))
                return bmp;
            return folder / (name + side + ".tga");
        }

        bool readSkySide(const std::filesystem::path &path, RgbaImage &image, std::string &error)
        {
            if (path.extension() != ".tga")
                return readBmp8(path, image, error);
            std::vector<std::uint8_t> bytes;
            if (!readFile(path, bytes, error))
                return false;
            if (!readTga(bytes, image, error))
            {
                error = path.filename().string() + ": " + error;
                return false;
            }
            return true;
        }
    } // namespace

    bool skyExists(const std::filesystem::path &gameDir, const std::string &name)
    {
        if (name.empty())
            return false;
        for (const char *side : kSkySides)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(skyPath(gameDir, name, side), ec))
                return false;
        }
        return true;
    }

    bool cookSky(const std::filesystem::path &gameDir, const std::string &name,
                 ITextureEncoder &encoder, const std::filesystem::path &texDir, std::string &error)
    {
        for (const char *side : kSkySides)
        {
            RgbaImage image;
            if (!readSkySide(skyPath(gameDir, name, side), image, error))
                return false;
            const std::uint32_t settings[] = {static_cast<std::uint32_t>(kSkySize),
                                              encoder.version()};
            const std::uint32_t hash =
                hldata::crc32(image.rgba.data(), static_cast<std::uint32_t>(image.rgba.size())) ^
                hldata::crc32(reinterpret_cast<const std::uint8_t *>(settings), sizeof(settings)) *
                    131u;
            char fileName[40];
            hldata::textureFileName((name + side).c_str(), kSkySize, kSkySize, false, fileName);
            const std::filesystem::path path = texDir / fileName;

            std::vector<std::uint8_t> existing;
            std::string ignored;
            CookedTexture old;
            TextureHeader oldHeader;
            if (readFile(path, existing, ignored) && unpackTexture(existing, old, oldHeader) &&
                oldHeader.sourceHash == hash)
                continue;

            CookedTexture encoded;
            if (!encoder.encode(resample(image, kSkySize, kSkySize), 256, TextureFormat::Tex4x4,
                                encoded, error) ||
                !writeFileAtomic(path, packTexture(encoded, hldata::kTextureSky, hash), error))
                return false;
        }
        return true;
    }
} // namespace hlds
