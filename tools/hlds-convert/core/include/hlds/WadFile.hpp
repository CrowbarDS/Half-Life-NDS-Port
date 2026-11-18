#pragma once

// WAD3 texture archives, and the set of them one map draws from.
//
// A map names most of its textures instead of carrying them; the pixels are in
// the WADs listed under worldspawn's "wad" key. That key holds the paths the
// mapper's own machine had (\sierra\half-life\valve\halflife.wad;...), so only
// the file names mean anything and they are looked up, without regard to case,
// in the game folder the user points the converter at.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hlds
{
    /// An 8-bit texture as GoldSrc stores it: palette indices plus 256 colours.
    struct IndexedImage
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> pixels;  ///< width*height indices
        std::vector<std::uint8_t> palette; ///< 256 RGB triples
    };

    class WadFile
    {
    public:
        bool open(const std::filesystem::path &path, std::string &error);

        std::size_t textureCount() const { return index_.size(); }
        bool contains(std::string_view lowerName) const;
        /// Read one texture by its lower-cased name.
        bool read(std::string_view lowerName, IndexedImage &out, std::string &error) const;
        const std::filesystem::path &path() const { return path_; }
        /// The textures' names, lower-cased, in order.
        std::vector<std::string> names() const;

    private:
        struct Entry
        {
            std::uint32_t offset;
            std::uint32_t size;
        };

        std::filesystem::path path_;
        std::vector<std::uint8_t> bytes_;
        std::unordered_map<std::string, Entry> index_;
    };

    class WadSet
    {
    public:
        /// Open every WAD named by a worldspawn "wad" value that can be found in
        /// `gameDir`. Missing WADs are recorded in `missing` and are not an
        /// error on their own: a map only fails if a texture it actually uses
        /// cannot be found.
        void openForMap(const std::filesystem::path &gameDir, std::string_view wadKey,
                        std::vector<std::string> &missing);
        /// The same over several folders, the first that has a WAD giving it:
        /// an expansion's own (Blue Shift has its own halflife.wad), then
        /// Half-Life's, as the game itself looks.
        void openForMap(const std::vector<std::filesystem::path> &folders, std::string_view wadKey,
                        std::vector<std::string> &missing);

        bool contains(std::string_view lowerName) const;
        bool read(std::string_view lowerName, IndexedImage &out, std::string &error) const;
        std::size_t wadCount() const { return wads_.size(); }

    private:
        std::vector<WadFile> wads_;
    };

    /// The file names in a worldspawn "wad" value, lower-cased, directories
    /// stripped, in order, without duplicates.
    std::vector<std::string> wadNamesFromKey(std::string_view wadKey);
} // namespace hlds
