// What the converter writes, and that nothing spoilt gets to the card
// (hlds/AtomicWrite.hpp, UiCook's fontWhole, Pack's common archive): a file
// is whole under its name and no temporary is left; a file written again is
// read again, not taken from what was read before; a font the console would
// not take is caught; and a temporary a run left behind is never packed.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "HlUi.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/NpacWriter.hpp"
#include "hlds/Pack.hpp"
#include "hlds/UiCook.hpp"

namespace fs = std::filesystem;
using namespace hlds;
using namespace hldstest;

namespace
{
    /// A font lump of two letters (as test_ui's).
    std::vector<std::uint8_t> madeFont()
    {
        const int height = 8;
        std::vector<std::uint8_t> lump(16 + 1024 + 256 * height + 2 + 768 + 64, 0);
        auto put32 = [&](std::size_t at, std::uint32_t v)
        {
            for (int k = 0; k < 4; ++k)
                lump[at + k] = static_cast<std::uint8_t>(v >> (8 * k));
        };
        put32(0, 16);
        put32(4, height);
        put32(8, 2);
        put32(12, 4);
        auto glyph = [&](int c, int x, int w)
        {
            lump[16 + 4 * c] = static_cast<std::uint8_t>(x);
            lump[18 + 4 * c] = static_cast<std::uint8_t>(w);
        };
        glyph('A', 0, 3);
        glyph('B', 16, 2);
        std::uint8_t *pixels = lump.data() + 16 + 1024;
        std::memset(pixels, 255, 256 * height);
        pixels[0] = pixels[2] = 32;
        pixels[1] = pixels[16] = pixels[17] = 192;
        std::uint8_t *palette = lump.data() + 16 + 1024 + 256 * height + 2;
        for (int i = 0; i < 256; ++i)
            palette[i * 3] = palette[i * 3 + 1] = palette[i * 3 + 2] = static_cast<std::uint8_t>(i);
        return lump;
    }

    std::vector<std::uint8_t> cookedFont()
    {
        QFont font;
        std::string error;
        parseQFont(madeFont(), font, error);
        return cookFont(font, true);
    }

    std::vector<fs::path> listed(const fs::path &folder)
    {
        std::vector<fs::path> files;
        std::error_code ec;
        for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            files.push_back(it->path());
        return files;
    }

    void testWrite(const fs::path &root)
    {
        const fs::path file = root / "deep" / "er" / "file.bin";
        std::vector<std::uint8_t> bytes(100000);
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<std::uint8_t>(i * 31 + 7);
        std::string error;
        check(writeFileAtomic(file, bytes, error), "a file is written, its folders made");
        std::vector<std::uint8_t> back;
        check(readFile(file, back, error) && back == bytes, "... and reads back as it was");
        const std::vector<fs::path> files = listed(file.parent_path());
        check(files.size() == 1 && files[0].filename() == "file.bin", "... with no temporary left beside it");

        // Written again at once, the same size: what is read is the new.
        std::vector<std::uint8_t> other(bytes.size(), 0x5A);
        check(writeFileAtomic(file, other, error) && readFile(file, back, error) && back == other,
              "written again the same size within a second: read again, not taken from before");

        check(isTempName("font0.hfn.tmp3") && isTempName("font0.hfn.tmp4242-17") && isTempName("x.tmp"),
              "a temporary's name, of this run's form and an older one's");
        check(!isTempName("font0.hfn") && !isTempName("common.npac") && !isTempName("tmp"),
              "... and nothing else");
        check(!processTag().empty() && processTag().find_first_not_of("0123456789") == std::string::npos,
              "a process's tag is its number");
    }

    void testFontWhole()
    {
        const std::vector<std::uint8_t> font = cookedFont();
        check(fontWhole(font), "a cooked font is whole");
        std::vector<std::uint8_t> cut(font.begin(), font.end() - 1);
        check(!fontWhole(cut), "... cut short by a byte, it is not");
        std::vector<std::uint8_t> longer = font;
        longer.push_back(0);
        check(!fontWhole(longer), "... nor with a byte after it");
        std::vector<std::uint8_t> zeros(font.size(), 0);
        check(!fontWhole(zeros), "... nor the right size and all zeros (a card pulled out too soon)");
        std::vector<std::uint8_t> wide = font;
        wide[sizeof(hldata::FontHeader) + ('A' - reinterpret_cast<const hldata::FontHeader *>(font.data())->first)] =
            hldata::kFontMaxWidth + 1;
        check(!fontWhole(wide), "... nor one with a letter wider than the console draws");
    }

    void testPackCommon(const fs::path &root)
    {
        const fs::path stage = root / "stage";
        const fs::path card = root / "card";
        std::string error;
        writeFileAtomic(stage / "ui" / "font0.hfn", cookedFont(), error);
        // What a run cut short leaves: a temporary, never to be packed.
        writeFileAtomic(stage / "ui" / "font1.hfn.tmp3", std::vector<std::uint8_t>(40, 0xEE), error);
        writeFileAtomic(stage / "ui" / "font1.hfn.tmp9999-2", std::vector<std::uint8_t>(40, 0xEE), error);
        PackReport report;
        check(packCommon(stage, card, true, report, error), "the common archive is made from the stage");
        std::vector<std::uint8_t> bytes;
        std::vector<NpacMember> members;
        bool font = false, temporary = false;
        if (readFile(card / "common.npac", bytes, error) &&
            readNpac(bytes, members, error))
        {
            for (const NpacMember &member : members)
            {
                font = font || (member.path == "ui/font0.hfn" && fontWhole(member.data));
                temporary = temporary || member.path.find(".tmp") != std::string::npos;
            }
        }
        check(font, "... with the font in it, whole");
        check(!temporary, "... and none of what a run cut short left");

        // A font spoilt on the disk since: the archive is not made.
        std::vector<std::uint8_t> spoilt = cookedFont();
        std::memset(spoilt.data(), 0, spoilt.size());
        writeFileAtomic(stage / "ui" / "font0.hfn", spoilt, error);
        error.clear();
        check(!packCommon(stage, card, true, report, error) && error.find("font0.hfn") != std::string::npos,
              "a damaged font stops the common archive, and says which");
    }
} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / ("hlds_test_writes_" + processTag());
    std::error_code ec;
    fs::remove_all(root, ec);
    testWrite(root);
    testFontWhole();
    testPackCommon(root);
    fs::remove_all(root, ec);
    std::printf("%s (%d failed)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
