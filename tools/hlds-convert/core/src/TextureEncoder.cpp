#include "hlds/TextureEncoder.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <cstring>

#include "hlds/AtomicWrite.hpp"

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace hlds
{
    namespace
    {
        /// The folder this program is in; empty if it cannot be told.
        std::filesystem::path programFolder()
        {
#if defined(_WIN32)
            wchar_t path[32768];
            const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
            if (length == 0 || length >= 32768)
                return {};
            return std::filesystem::path(path).parent_path();
#elif defined(__APPLE__)
            char path[4096];
            std::uint32_t size = sizeof(path);
            if (_NSGetExecutablePath(path, &size) != 0)
                return {};
            return std::filesystem::path(path).parent_path();
#else
            std::error_code ec;
            return std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
#endif
        }

        /// Multi-threaded encoding
        class EncodeSlots
        {
        public:
            EncodeSlots() : free_(static_cast<int>(std::max(1u, std::thread::hardware_concurrency()))) {}
            void take()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] { return free_ > 0; });
                --free_;
            }
            void give()
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    ++free_;
                }
                cv_.notify_one();
            }

        private:
            std::mutex mutex_;
            std::condition_variable cv_;
            int free_;
        };

        EncodeSlots &encodeSlots()
        {
            static EncodeSlots slots;
            return slots;
        }

        /// Quote a path for the shell. Paths here are ours (a temp directory
        /// and an executable the user pointed at), but a space in a home
        /// directory is common enough.
#ifdef _WIN32
        // (cmd.exe: quotes, and no backslash to double.)
        std::string quoted(const std::filesystem::path &path)
        {
            return "\"" + path.generic_string() + "\"";
        }
#else
        std::string quoted(const std::filesystem::path &path)
        {
            std::string out = "\"";
            for (char c : path.string())
            {
                if (c == '"' || c == '\\' || c == '$' || c == '`')
                    out.push_back('\\');
                out.push_back(c);
            }
            out.push_back('"');
            return out;
        }
#endif
    } // namespace

    PtexconvEncoder::PtexconvEncoder(std::filesystem::path executable,
                                     std::filesystem::path tempDir)
        : executable_(std::move(executable)), tempDir_(std::move(tempDir))
    {
    }

    bool PtexconvEncoder::encode(const RgbaImage &image, int maxColours, TextureFormat format,
                                 CookedTexture &out, std::string &error)
    {
        // ptexconv crashes on an image with nothing in it. The cooker never
        // sends one (TextureBudget's findHiddenTextures), but a crash is not
        // an acceptable way to find out that it did.
        bool anyOpaque = false;
        for (std::size_t i = 3; i < image.rgba.size() && !anyOpaque; i += 4)
            anyOpaque = image.rgba[i] != 0;
        if (!anyOpaque)
        {
            error = "the image is entirely transparent";
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(tempDir_, ec);
        const std::filesystem::path base = tempDir_ / ("t" + std::to_string(counter_++));
        std::filesystem::path png = base;
        png += ".png";
        if (!writePng(png, image, error))
            return false;

        // -gt texture, -ob raw binary outputs, -cm palette cap. Transparency is
        // taken from the PNG's alpha.
        const bool a3i5 = format == TextureFormat::A3I5;
        const std::string formatArgs =
            a3i5 ? std::string(" -f a3i5") : " -f tex4x4 -cm " + std::to_string(maxColours);
        const std::string command = quoted(executable_) + " -gt -ob" + formatArgs + " -o " +
                                    quoted(base) + " " + quoted(png) +
#ifdef _WIN32
                                    " > NUL 2>&1";
#else
                                    " > /dev/null 2>&1";
#endif
        encodeSlots().take();
#ifdef _WIN32
        // (cmd.exe takes off the first and last quote of what it is given.)
        const int status = std::system(("\"" + command + "\"").c_str());
#else
        const int status = std::system(command.c_str());
#endif
        encodeSlots().give();

        auto piece = [&](const char *suffix, std::vector<std::uint8_t> &bytes)
        {
            std::filesystem::path path = base;
            path += suffix;
            std::string ignored;
            const bool ok = readFile(path, bytes, ignored);
            std::filesystem::remove(path, ec);
            return ok;
        };
        out.format = format;
        out.index.clear();
        bool got = piece("_tex.bin", out.texels) & piece("_pal.bin", out.palette);
        if (!a3i5)
            got = piece("_idx.bin", out.index) && got; // only 4x4 has the third piece
        std::filesystem::remove(png, ec);
        if (status != 0 || !got)
        {
            error = "ptexconv failed (" + executable_.string() + ")";
            return false;
        }
        out.width = image.width;
        out.height = image.height;

        RgbaImage decoded;
        return decodeTexture(out, decoded, error); // refuse output the DS would misread
    }

    std::filesystem::path findPtexconv(const std::filesystem::path &explicitPath)
    {
        std::error_code ec;
        if (!explicitPath.empty())
            return std::filesystem::exists(explicitPath, ec) ? explicitPath
                                                             : std::filesystem::path();
#ifdef _WIN32
        const char *const exe = "ptexconv.exe";
        const char separator = ';';
#else
        const char *const exe = "ptexconv";
        const char separator = ':';
#endif
        std::vector<std::filesystem::path> candidates;
        // (The packages have it beside the program: the one they were made with.)
        if (const std::filesystem::path folder = programFolder(); !folder.empty())
            candidates.push_back(folder / exe);
        if (const char *ext = std::getenv("BLOCKSDSEXT"))
            candidates.push_back(std::filesystem::path(ext) / "ptexconv" / exe);
        if (const char *wf = std::getenv("WONDERFUL_TOOLCHAIN"))
            candidates.push_back(std::filesystem::path(wf) / "thirdparty" / "blocksds" /
                                 "external" / "ptexconv" / exe);
        candidates.push_back(std::filesystem::path("/opt/wonderful/thirdparty/blocksds/external") /
                             "ptexconv" / exe);
        if (const char *pathVar = std::getenv("PATH"))
        {
            const std::string paths = pathVar;
            std::size_t pos = 0;
            while (pos <= paths.size())
            {
                std::size_t end = paths.find(separator, pos);
                if (end == std::string::npos)
                    end = paths.size();
                if (end > pos)
                    candidates.push_back(std::filesystem::path(paths.substr(pos, end - pos)) / exe);
                pos = end + 1;
            }
        }
        for (const auto &candidate : candidates)
        {
            if (std::filesystem::is_regular_file(candidate, ec))
                return candidate;
        }
        return {};
    }

    bool decodeTexture(const CookedTexture &texture, RgbaImage &out, std::string &error)
    {
        const int w = texture.width;
        const int h = texture.height;
        if (texture.format == TextureFormat::A3I5)
        {
            const int colours = texture.paletteColours();
            if (w <= 0 || h <= 0 || texture.texels.size() != static_cast<std::size_t>(w) * h ||
                !texture.index.empty() || (texture.palette.size() & 1))
            {
                error = "A3I5 data does not match its size";
                return false;
            }
            out.width = w;
            out.height = h;
            out.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
            for (std::size_t i = 0; i < texture.texels.size(); ++i)
            {
                const int colour = texture.texels[i] & 31;
                const int alpha3 = texture.texels[i] >> 5;
                if (colour >= colours)
                {
                    error = "an A3I5 texel points outside its palette";
                    return false;
                }
                const std::uint16_t c = static_cast<std::uint16_t>(
                    texture.palette[colour * 2] | (texture.palette[colour * 2 + 1] << 8));
                // The hardware widens 3 bits of alpha to 5 as a*4 + a/2.
                const int alpha5 = alpha3 * 4 + alpha3 / 2;
                out.rgba[i * 4 + 0] = static_cast<std::uint8_t>((c & 31) << 3);
                out.rgba[i * 4 + 1] = static_cast<std::uint8_t>(((c >> 5) & 31) << 3);
                out.rgba[i * 4 + 2] = static_cast<std::uint8_t>(((c >> 10) & 31) << 3);
                out.rgba[i * 4 + 3] = static_cast<std::uint8_t>(alpha5 * 255 / 31);
            }
            return true;
        }

        const std::size_t blocks = static_cast<std::size_t>(w / 4) * (h / 4);
        if (w <= 0 || h <= 0 || (w & 3) || (h & 3) || texture.texels.size() != blocks * 4 ||
            texture.index.size() != blocks * 2 || (texture.palette.size() & 1))
        {
            error = "tex4x4 data does not match its size";
            return false;
        }
        const int colours = texture.paletteColours();
        out.width = w;
        out.height = h;
        out.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);

        auto colour = [&](int i, int rgb[3])
        {
            const std::uint16_t c =
                static_cast<std::uint16_t>(texture.palette[i * 2] | (texture.palette[i * 2 + 1] << 8));
            rgb[0] = (c & 31) << 3;
            rgb[1] = ((c >> 5) & 31) << 3;
            rgb[2] = ((c >> 10) & 31) << 3;
        };

        for (std::size_t b = 0; b < blocks; ++b)
        {
            std::uint32_t bits;
            std::memcpy(&bits, &texture.texels[b * 4], 4);
            const std::uint16_t idx =
                static_cast<std::uint16_t>(texture.index[b * 2] | (texture.index[b * 2 + 1] << 8));
            // The low 14 bits count in pairs of colours; the top two choose
            // how the block's four values are made (GBATEK, "DS 3D Texture
            // Formats").
            const int first = (idx & 0x3FFF) * 2;
            const int mode = idx >> 14;
            const int needed = (mode == 2) ? 4 : (mode == 0 ? 3 : 2);
            if (first + needed > colours)
            {
                error = "a tex4x4 block points outside its palette";
                return false;
            }
            int c[4][3] = {};
            bool clear3 = false;
            colour(first, c[0]);
            colour(first + 1, c[1]);
            switch (mode)
            {
            case 0:
                colour(first + 2, c[2]);
                clear3 = true;
                break;
            case 1:
                for (int k = 0; k < 3; ++k)
                    c[2][k] = (c[0][k] + c[1][k]) / 2;
                clear3 = true;
                break;
            case 2:
                colour(first + 2, c[2]);
                colour(first + 3, c[3]);
                break;
            default:
                for (int k = 0; k < 3; ++k)
                {
                    c[2][k] = (c[0][k] * 5 + c[1][k] * 3) / 8;
                    c[3][k] = (c[0][k] * 3 + c[1][k] * 5) / 8;
                }
                break;
            }
            const int bx = static_cast<int>(b % (w / 4)) * 4;
            const int by = static_cast<int>(b / (w / 4)) * 4;
            for (int t = 0; t < 16; ++t)
            {
                const int value = (bits >> (t * 2)) & 3;
                std::uint8_t *p = out.at(bx + (t & 3), by + (t >> 2));
                if (value == 3 && clear3)
                    continue; // stays transparent
                p[0] = static_cast<std::uint8_t>(c[value][0]);
                p[1] = static_cast<std::uint8_t>(c[value][1]);
                p[2] = static_cast<std::uint8_t>(c[value][2]);
                p[3] = 255;
            }
        }
        return true;
    }
} // namespace hlds
