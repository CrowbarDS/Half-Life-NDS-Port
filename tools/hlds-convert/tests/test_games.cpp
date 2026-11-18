// The games a card can hold: Half-Life and its expansions (HlGame.hpp).
//
// An expansion's folder is read over Half-Life's, as the game itself reads
// it: its own files first, then valve's. Its maps go to a folder of its own
// on the card, and its manifest says which game it is. Half-Life's own look
// -- its colours, its folders -- must come out as it always was.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "HlFont.hpp"
#include "HlGame.hpp"
#include "HlManifest.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/Converter.hpp"
#include "hlds/WadFile.hpp"

namespace fs = std::filesystem;
using namespace hldata;
using namespace hldstest;

namespace
{
    /// A WAD3 file holding one 16x8 mip texture named `texName`.
    std::vector<std::uint8_t> buildWad(const std::string &texName)
    {
        using hldstest::detail::append;
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
        for (int i = 0; i < 256 * 3; ++i)
            mip.push_back(static_cast<std::uint8_t>(i));
        std::vector<std::uint8_t> file;
        file.insert(file.end(), {'W', 'A', 'D', '3'});
        append(file, static_cast<std::int32_t>(1));
        append(file, static_cast<std::int32_t>(12 + mip.size()));
        file.insert(file.end(), mip.begin(), mip.end());
        append(file, static_cast<std::int32_t>(12));
        append(file, static_cast<std::int32_t>(mip.size()));
        append(file, static_cast<std::int32_t>(mip.size()));
        file.push_back(67);
        file.push_back(0);
        append(file, static_cast<std::int16_t>(0));
        char padded[16] = {};
        std::strncpy(padded, texName.c_str(), 15);
        file.insert(file.end(), padded, padded + 16);
        return file;
    }

    void write(const fs::path &path, const std::vector<std::uint8_t> &bytes)
    {
        fs::create_directories(path.parent_path());
        std::string error;
        hlds::writeFileAtomic(path, bytes, error);
    }
    void writeText(const fs::path &path, const std::string &text)
    {
        write(path, std::vector<std::uint8_t>(text.begin(), text.end()));
    }
} // namespace

int main()
{
    // The table.
    check(gameFromFolder("valve") == kGameHalfLife && gameFromFolder("BShift") == kGameBlueShift &&
              gameFromFolder("GEARBOX") == kGameOpFor && gameFromFolder("cstrike") == kGameCount &&
              gameFromFolder("valve_french") == kGameCount,
          "a game is told by its folder's name, in any case");
    check(std::strcmp(kGames[kGameHalfLife].cardDir, "") == 0, "Half-Life's card folder is data/ itself, as it was");
    check(!kGames[kGameHalfLife].dsiOnly && kGames[kGameBlueShift].dsiOnly && kGames[kGameOpFor].dsiOnly,
          "the expansions are a DSi's only");
    check(kGames[kGameHalfLife].weaponSlots == 5 && kGames[kGameBlueShift].weaponSlots == 5 &&
              kGames[kGameOpFor].weaponSlots == 7,
          "Opposing Force has seven slots of weapons, the others five");
    check(kGames[kGameHalfLife].achievements && !kGames[kGameBlueShift].achievements &&
              !kGames[kGameOpFor].achievements && kGames[kGameHalfLife].multiplayer &&
              !kGames[kGameBlueShift].multiplayer && !kGames[kGameOpFor].multiplayer,
          "achievements and matches are Half-Life's");

    // Half-Life's colours are what they were before there were games: the
    // display's (HudDraw) and the menus' (uiColour).
    check(kGames[kGameHalfLife].hud == (31 | 20 << 5 | 2 << 10) && kGames[kGameHalfLife].hudDim == (14 | 9 << 5 | 1 << 10) &&
              kGames[kGameHalfLife].hud640 == (31 | 20 << 5),
          "Half-Life's display is orange as it was");
    {
        static constexpr std::uint16_t kBefore[16] = {
            2 | 2 << 5 | 3 << 10,    5 | 5 << 5 | 7 << 10,    12 | 8 << 5 | 2 << 10,   14 | 13 << 5 | 12 << 10,
            0,                       10 | 10 << 5 | 10 << 10, 20 | 20 << 5 | 20 << 10, 31 | 31 << 5 | 31 << 10,
            10 | 5 << 5 | 0 << 10,   20 | 11 << 5 | 0 << 10,  31 | 20 << 5 | 2 << 10,  5 | 5 << 5 | 6 << 10,
            9 | 9 << 5 | 10 << 10,   15 | 15 << 5 | 16 << 10, 31 | 4 << 5 | 3 << 10,   3 | 3 << 5 | 4 << 10};
        activeGame = kGameHalfLife;
        bool same = true;
        for (int i = 0; i < 16; ++i)
            same = same && uiColour(i) == kBefore[i];
        check(same, "Half-Life's menus are in the colours they were");
        activeGame = kGameBlueShift;
        check(uiColour(kUiOrange + 2) == kGames[kGameBlueShift].accent[2] && uiColour(kUiWhite + 2) == kBefore[kUiWhite + 2],
              "Blue Shift's accent is its own; the rest is the same");
        uiNeutral = true;
        check(uiColour(kUiOrange + 2) == kBefore[kUiOrange + 2], "... and CHOOSE GAME's is no game's (Half-Life's)");
        uiNeutral = false;
        activeGame = kGameHalfLife;
        // (Each accent brighter step by step: the three levels of a letter.)
        for (const GameInfo &game : kGames)
        {
            auto sum = [](std::uint16_t c) { return (c & 31) + (c >> 5 & 31) + (c >> 10 & 31); };
            check(sum(game.accent[0]) < sum(game.accent[1]) && sum(game.accent[1]) < sum(game.accent[2]),
                  "each game's accent goes from faint to full");
        }
    }

    // The folders an expansion is read from, and its WADs.
    const fs::path root = fs::temp_directory_path() / "hlds_test_games";
    fs::remove_all(root);
    write(root / "valve" / "shared.wad", buildWad("valvetex"));
    write(root / "valve" / "onlyvalve.wad", buildWad("basetex"));
    write(root / "bshift" / "SHARED.WAD", buildWad("shifttex"));
    write(root / "valve" / "maps" / "c0a0.bsp", buildBoxRoom());
    write(root / "bshift" / "maps" / "ba_tram1.bsp", buildBoxRoom());
    write(root / "bshift" / "maps" / "Ba_Hazard1.BSP", buildBoxRoom());
    writeText(root / "bshift" / "liblist.gam", "// Blue Shift\ngame \"Blue Shift\"\nstartmap \"ba_tram1\"\n");
    fs::create_directories(root / "card");

    check(hlds::gameOfFolder(root / "bshift") == kGameBlueShift && hlds::gameOfFolder(root / "valve") == kGameHalfLife,
          "the converter tells the game by its folder");
    check(hlds::baseFolder(root / "bshift") == root / "valve", "and finds Half-Life's beside an expansion");
    check(hlds::gameTitle(root / "bshift") == "Blue Shift" && hlds::gameTitle(root / "valve").empty(),
          "its name, from its liblist.gam");
    {
        hlds::ConvertOptions options;
        options.gameDir = root / "bshift";
        options.baseDir = root / "valve";
        options.languageDir = root / "bshift_french";
        const auto folders = hlds::gameFolders(options);
        check(folders.size() == 3 && folders[0] == options.languageDir && folders[1] == options.gameDir &&
                  folders[2] == options.baseDir,
              "looked for: the translation's, the game's, then Half-Life's");
        hlds::WadSet wads;
        std::vector<std::string> missing;
        wads.openForMap(std::vector<fs::path>{root / "bshift", root / "valve"}, "\\hl\\valve\\shared.wad;onlyvalve.wad;gone.wad",
                        missing);
        check(wads.contains("shifttex") && !wads.contains("valvetex"),
              "a WAD both have is the expansion's own (any case)");
        check(wads.contains("basetex"), "one only Half-Life has is Half-Life's");
        check(missing.size() == 1 && missing[0] == "gone.wad", "and one neither has is missing");
    }

    // An expansion cooked: its maps (its own, never Half-Life's), in its own
    // folder on the card and in the stage; its manifest naming it.
    {
        hlds::ConvertOptions options;
        options.gameDir = root / "bshift";
        options.cardRoot = root / "card";
        options.textures = false;
        hlds::Converter converter(options);
        check(converter.game() == kGameBlueShift, "the converter cooks Blue Shift");
        check(converter.preflight().empty(), "with Half-Life's folder beside it, all is well");
        const auto maps = converter.listMaps();
        check(maps.size() == 2 && maps[0] == "ba_hazard1" && maps[1] == "ba_tram1", "its own maps only");
        std::string error;
        const int failed = converter.cookMaps({"ba_tram1"}, nullptr, nullptr, nullptr, error);
        check(failed == 0, "a map of it cooks");
        const fs::path data = hlds::Converter::dataDir(root / "card");
        check(converter.cardDir() == data / "bshift" && fs::exists(data / "bshift" / "maps" / "ba_tram1.npac") &&
                  !fs::exists(data / "maps"),
              "into data/bshift/, Half-Life's folders left alone");
        check(fs::exists(hlds::Converter::gameDataDir(root / "card-stage", kGameBlueShift) / "maps" / "ba_tram1.hlm"),
              "and a stage of its own");
        std::vector<std::uint8_t> bytes;
        Manifest back;
        check(hlds::readFile(data / "bshift" / "manifest.bin", bytes, error) &&
                  readManifest(bytes.data(), bytes.size(), back) == ManifestError::Ok && back.game == kGameBlueShift &&
                  back.title == "Blue Shift" && back.startMap == "ba_tram1",
              "its manifest names the game and starts in its first map");

        hlds::ConvertOptions alone = options;
        alone.gameDir = root / "lonely" / "bshift";
        fs::create_directories(alone.gameDir / "maps");
        write(alone.gameDir / "maps" / "ba_tram1.bsp", buildBoxRoom());
        check(!hlds::Converter(alone).preflight().empty(), "an expansion with no Half-Life beside it is refused");
    }
    activeGame = kGameHalfLife;

    fs::remove_all(root);
    if (g_failures == 0)
        std::printf("test_games: ok\n");
    return g_failures == 0 ? 0 : 1;
}
