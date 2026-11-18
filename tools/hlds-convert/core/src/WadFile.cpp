#include "hlds/WadFile.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace hlds
{
    namespace
    {
        inline constexpr char kTypeMipTex = 67;

#pragma pack(push, 1)
        struct WadHeader
        {
            char ident[4];
            std::int32_t lumpCount;
            std::int32_t dirOffset;
        };

        struct WadLump
        {
            std::int32_t filePos;
            std::int32_t diskSize;
            std::int32_t size;
            char type;
            char compression;
            std::int16_t pad;
            char name[16];
        };

        struct MipTex
        {
            char name[16];
            std::uint32_t width;
            std::uint32_t height;
            std::uint32_t offsets[4];
        };
#pragma pack(pop)
        static_assert(sizeof(WadHeader) == 12);
        static_assert(sizeof(WadLump) == 32);
        static_assert(sizeof(MipTex) == 40);

        std::string lower(std::string_view in)
        {
            std::string out(in);
            for (char &c : out)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }
    } // namespace

    bool WadFile::open(const std::filesystem::path &path, std::string &error)
    {
        path_ = path;
        index_.clear();
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            error = "cannot open " + path.string();
            return false;
        }
        bytes_.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        WadHeader header;
        if (bytes_.size() < sizeof(header))
        {
            error = path.string() + " is too small to be a WAD";
            return false;
        }
        std::memcpy(&header, bytes_.data(), sizeof(header));
        if (std::memcmp(header.ident, "WAD3", 4) != 0)
        {
            error = path.string() + " is not a WAD3 file";
            return false;
        }
        if (header.lumpCount < 0 || header.dirOffset < 0 ||
            static_cast<std::size_t>(header.dirOffset) +
                    static_cast<std::size_t>(header.lumpCount) * sizeof(WadLump) >
                bytes_.size())
        {
            error = path.string() + " has a directory outside the file";
            return false;
        }

        for (std::int32_t i = 0; i < header.lumpCount; ++i)
        {
            WadLump lump;
            std::memcpy(&lump, bytes_.data() + header.dirOffset + i * sizeof(WadLump),
                        sizeof(lump));
            // Only uncompressed mip textures. Everything else in a WAD (fonts,
            // pictures) is not something a map face can name.
            if (lump.type != kTypeMipTex || lump.compression != 0)
                continue;
            if (lump.filePos < 0 || lump.diskSize < static_cast<std::int32_t>(sizeof(MipTex)) ||
                static_cast<std::size_t>(lump.filePos) + static_cast<std::size_t>(lump.diskSize) >
                    bytes_.size())
                continue;
            std::size_t len = 0;
            while (len < sizeof(lump.name) && lump.name[len] != '\0')
                ++len;
            index_[lower(std::string_view(lump.name, len))] =
                Entry{static_cast<std::uint32_t>(lump.filePos),
                      static_cast<std::uint32_t>(lump.diskSize)};
        }
        return true;
    }

    std::vector<std::string> WadFile::names() const
    {
        std::vector<std::string> out;
        for (const auto &entry : index_)
            out.push_back(entry.first);
        std::sort(out.begin(), out.end());
        return out;
    }

    bool WadFile::contains(std::string_view lowerName) const
    {
        return index_.find(std::string(lowerName)) != index_.end();
    }

    bool WadFile::read(std::string_view lowerName, IndexedImage &out, std::string &error) const
    {
        const auto it = index_.find(std::string(lowerName));
        if (it == index_.end())
        {
            error = "texture '" + std::string(lowerName) + "' is not in " + path_.string();
            return false;
        }
        const std::uint8_t *base = bytes_.data() + it->second.offset;
        const std::size_t size = it->second.size;
        MipTex mip;
        std::memcpy(&mip, base, sizeof(mip));
        const std::size_t pixels = static_cast<std::size_t>(mip.width) * mip.height;
        const std::size_t mipBytes = pixels + pixels / 4 + pixels / 16 + pixels / 64;
        const std::size_t palOfs = sizeof(MipTex) + mipBytes + 2;
        if (mip.width == 0 || mip.height == 0 || mip.width > 4096 || mip.height > 4096 ||
            mip.offsets[0] + pixels > size || palOfs + 768 > size)
        {
            error = "texture '" + std::string(lowerName) + "' in " + path_.string() +
                    " is damaged";
            return false;
        }
        out.width = mip.width;
        out.height = mip.height;
        out.pixels.assign(base + mip.offsets[0], base + mip.offsets[0] + pixels);
        out.palette.assign(base + palOfs, base + palOfs + 768);
        return true;
    }

    std::vector<std::string> wadNamesFromKey(std::string_view wadKey)
    {
        std::vector<std::string> names;
        std::size_t pos = 0;
        while (pos <= wadKey.size())
        {
            std::size_t end = wadKey.find(';', pos);
            if (end == std::string_view::npos)
                end = wadKey.size();
            std::string_view item = wadKey.substr(pos, end - pos);
            const std::size_t slash = item.find_last_of("/\\");
            if (slash != std::string_view::npos)
                item = item.substr(slash + 1);
            if (!item.empty())
            {
                std::string name = lower(item);
                if (std::find(names.begin(), names.end(), name) == names.end())
                    names.push_back(std::move(name));
            }
            pos = end + 1;
        }
        return names;
    }

    void WadSet::openForMap(const std::filesystem::path &gameDir, std::string_view wadKey,
                            std::vector<std::string> &missing)
    {
        openForMap(std::vector<std::filesystem::path>{gameDir}, wadKey, missing);
    }

    void WadSet::openForMap(const std::vector<std::filesystem::path> &folders, std::string_view wadKey,
                            std::vector<std::string> &missing)
    {
        wads_.clear();

        // The game folders' files by lower-cased name: the key says
        // "halflife.wad" where the disc may say "HALFLIFE.WAD" (Opposing
        // Force's OPFOR.WAD). The first folder to have one gives it.
        std::unordered_map<std::string, std::filesystem::path> onDisk;
        for (const std::filesystem::path &folder : folders)
        {
            std::error_code ec;
            for (auto it = std::filesystem::directory_iterator(folder, ec);
                 !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
            {
                if (it->is_regular_file(ec))
                    onDisk.emplace(lower(it->path().filename().string()), it->path());
            }
        }

        for (const std::string &name : wadNamesFromKey(wadKey))
        {
            const auto found = onDisk.find(name);
            WadFile wad;
            std::string error;
            if (found == onDisk.end() || !wad.open(found->second, error))
            {
                missing.push_back(name);
                continue;
            }
            wads_.push_back(std::move(wad));
        }
    }

    bool WadSet::contains(std::string_view lowerName) const
    {
        for (const WadFile &wad : wads_)
        {
            if (wad.contains(lowerName))
                return true;
        }
        return false;
    }

    bool WadSet::read(std::string_view lowerName, IndexedImage &out, std::string &error) const
    {
        for (const WadFile &wad : wads_)
        {
            if (wad.contains(lowerName))
                return wad.read(lowerName, out, error);
        }
        error = "texture '" + std::string(lowerName) + "' is in none of the map's WADs";
        return false;
    }
} // namespace hlds
