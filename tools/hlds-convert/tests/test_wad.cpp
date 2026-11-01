// The WAD reader and the per-map WAD lookup.
//
// The lookup is the part with a way to go quietly wrong: worldspawn's "wad"
// key holds paths from the mapper's own machine, in whatever case they typed,
// and a miss there turns into a map with untextured walls rather than an error.

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "TestBspBuilder.hpp"
#include "hlds/EntityParser.hpp"
#include "hlds/WadFile.hpp"

namespace fs = std::filesystem;
using namespace hlds;
using namespace hldstest;

namespace
{
    /// A WAD3 file holding one 16x8 mip texture, plus one lump of another type
    /// that must be ignored.
    std::vector<std::uint8_t> buildWad(const std::string &texName)
    {
        using detail::append;
        const std::uint32_t w = 16, h = 8;
        std::vector<std::uint8_t> mip(40, 0);
        std::memcpy(mip.data(), texName.c_str(), texName.size());
        std::memcpy(mip.data() + 16, &w, 4);
        std::memcpy(mip.data() + 20, &h, 4);
        std::uint32_t ofs = 40;
        for (int level = 0; level < 4; ++level)
        {
            std::memcpy(mip.data() + 24 + level * 4, &ofs, 4);
            const std::uint32_t n = (w >> level) * (h >> level);
            for (std::uint32_t i = 0; i < n; ++i)
                mip.push_back(static_cast<std::uint8_t>(i & 0xFF));
            ofs += n;
        }
        append(mip, static_cast<std::uint16_t>(256));
        for (int i = 0; i < 256; ++i)
        {
            mip.push_back(static_cast<std::uint8_t>(i));
            mip.push_back(static_cast<std::uint8_t>(255 - i));
            mip.push_back(7);
        }

        std::vector<std::uint8_t> file;
        file.insert(file.end(), {'W', 'A', 'D', '3'});
        append(file, static_cast<std::int32_t>(2));
        append(file, static_cast<std::int32_t>(12 + mip.size()));
        file.insert(file.end(), mip.begin(), mip.end());

        auto dirEntry = [&](char type, const char *name)
        {
            append(file, static_cast<std::int32_t>(12));
            append(file, static_cast<std::int32_t>(mip.size()));
            append(file, static_cast<std::int32_t>(mip.size()));
            file.push_back(static_cast<std::uint8_t>(type));
            file.push_back(0);
            append(file, static_cast<std::int16_t>(0));
            char padded[16] = {};
            std::strncpy(padded, name, 15);
            file.insert(file.end(), padded, padded + 16);
        };
        dirEntry(67, texName.c_str()); // a mip texture
        dirEntry(66, "NOTATEXTURE");   // a picture: not something a face can name
        return file;
    }

    void write(const fs::path &path, const std::vector<std::uint8_t> &bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
} // namespace

int main()
{
    const fs::path dir = fs::temp_directory_path() / "hlds_test_wad";
    fs::remove_all(dir);
    fs::create_directories(dir);
    // Upper case in the archive and a differently-cased file name on disk.
    write(dir / "TESTWAD.wad", buildWad("OUTWALL"));
    write(dir / "notawad.wad", {'P', 'A', 'C', 'K', 0, 0, 0, 0, 0, 0, 0, 0});

    std::string error;
    WadFile wad;
    check(wad.open(dir / "TESTWAD.wad", error), "the WAD opens");
    check(wad.textureCount() == 1, "only the mip texture is indexed");
    check(wad.contains("outwall") && !wad.contains("notatexture"), "names are lower-cased");

    IndexedImage image;
    check(wad.read("outwall", image, error), "the texture reads");
    check(image.width == 16 && image.height == 8 && image.pixels.size() == 128, "size");
    check(image.pixels[5] == 5 && image.pixels[127] == 127, "pixels are mip 0");
    check(image.palette.size() == 768 && image.palette[3] == 1 && image.palette[4] == 254 &&
              image.palette[5] == 7,
          "palette follows the fourth mip");
    check(!wad.read("absent", image, error), "an absent texture is an error");

    WadFile bad;
    check(!bad.open(dir / "notawad.wad", error), "a non-WAD is refused");
    check(!bad.open(dir / "nosuchfile.wad", error), "a missing file is refused");

    const auto names = wadNamesFromKey("\\sierra\\half-life\\valve\\TestWad.WAD;"
                                       "C:/maps/second.wad;;\\x\\testwad.wad");
    check(names.size() == 2 && names[0] == "testwad.wad" && names[1] == "second.wad",
          "wad names: directories stripped, lower-cased, no duplicates");
    check(wadNamesFromKey("").empty(), "an empty key names nothing");

    // The whole path a map takes: its own worldspawn key against the folder.
    hldstest::BoxRoom room;
    room.externalTextureName = "OutWall";
    BspFile bsp;
    check(bsp.parse(buildBoxRoom(room), error), "box room with an external texture parses");
    check(bsp.textures.size() == 2 && !bsp.textures[1].embedded &&
              bsp.textures[1].name == "outwall",
          "the external texture is named, not embedded");
    std::vector<Entity> ents;
    check(parseEntities(bsp.entities, ents, error), "entities parse");
    std::vector<std::string> missing;
    WadSet wads;
    wads.openForMap(dir, ents[0].value("wad"), missing);
    check(wads.wadCount() == 1, "testwad.wad is found despite the case difference");
    check(missing.size() == 1 && missing[0] == "second.wad", "second.wad is reported missing");
    check(wads.contains(bsp.textures[1].name), "the map's external texture resolves");
    check(wads.read("outwall", image, error) && image.width == 16, "and reads through the set");
    check(!wads.read("absent", image, error), "an unresolved texture is an error");

    fs::remove_all(dir);
    if (g_failures == 0)
        std::printf("test_wad: ok\n");
    return g_failures == 0 ? 0 : 1;
}
