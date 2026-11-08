// The installer's decisions, on folders made up for the purpose.

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "BundledRom.hpp"
#include "Install.hpp"

namespace fs = std::filesystem;
using namespace hlds::install;

namespace
{
    int failures = 0;
    void check(bool ok, const char *what)
    {
        if (!ok)
        {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    }
    void put(const fs::path &path, const std::string &text = "x")
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    std::string read(const fs::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    void makeGame(const fs::path &valve, bool upper = false)
    {
        for (const char *map : {"c0a0", "c1a0", "c2a1", "c3a1", "c4a1", "c5a1", "t0a0", "t0a0d", "hldemo1", "hldemo3", "stalkyard"})
        {
            std::string file = std::string(map) + ".bsp";
            if (upper)
                for (char &c : file)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            put(valve / (upper ? "MAPS" : "maps") / file);
        }
        put(valve / "liblist.gam");
    }

    void testVdf()
    {
        const auto libraries = steamLibraries(R"("libraryfolders"
{
    "0"
    {
        "path"      "C:\\Program Files (x86)\\Steam"
        "label"     ""
    }
    "1"
    {
        "Path"      "/mnt/games/SteamLibrary"
    }
})");
        check(libraries.size() == 2, "two libraries read");
        check(libraries.size() == 2 && libraries[0] == fs::path("C:\\Program Files (x86)\\Steam"), "backslashes undone");
        check(libraries.size() == 2 && libraries[1] == fs::path("/mnt/games/SteamLibrary"), "any case of its key");
        check(steamLibraries("rubbish").empty(), "no libraries in rubbish");
        check(steamLibraries("\"path\"").empty(), "cut short: none, no crash");
    }

    void testGame(const fs::path &root)
    {
        const fs::path valve = root / "Steam/steamapps/common/Half-Life/valve";
        makeGame(valve);
        GameFolder game = checkGame(valve);
        check(game.ok && game.campaign && game.hazardCourse && game.uplink, "the valve folder recognised");
        game = checkGame(valve.parent_path());
        check(game.ok && game.valve.filename() == "valve", "Half-Life/ is taken for its valve/");
        game = checkGame(valve.parent_path().parent_path());
        check(game.ok, "common/ too");

        makeGame(root / "odd/VALVE", true);
        game = checkGame(root / "odd");
        check(game.ok && game.campaign, "any case of the names");

        check(!checkGame(root / "nowhere").ok, "a folder that is not there");
        put(root / "other/readme.txt");
        check(!checkGame(root / "other").ok && !checkGame(root / "other").why.empty(), "another folder, with a reason");
        put(root / "mod/maps/mymap.bsp");
        put(root / "mod/liblist.gam");
        check(!checkGame(root / "mod").ok, "a mod's maps are not Half-Life");

        put(root / "partial/maps/t0a0.bsp");
        put(root / "partial/maps/t0a0d.bsp");
        put(root / "partial/liblist.gam");
        game = checkGame(root / "partial");
        check(game.ok && !game.campaign && game.hazardCourse, "what is there, of what is not whole");
    }

    void testFind(const fs::path &root)
    {
        // Steam here, its game in a library elsewhere.
        put(root / "s1/steamapps/libraryfolders.vdf",
            "\"path\" \"" + (root / "lib").generic_string() + "\"");
        makeGame(root / "lib/steamapps/common/Half-Life/valve");
        // ... and a part of one, found first but worth less.
        put(root / "s0/steamapps/common/Half-Life/valve/maps/t0a0.bsp");
        put(root / "s0/steamapps/common/Half-Life/valve/maps/t0a0d.bsp");
        put(root / "s0/steamapps/common/Half-Life/valve/liblist.gam");
        const auto found = findGames({root / "s0", root / "s1", root / "s1"}, {root / "lib/steamapps/common/Half-Life"});
        check(found.size() == 2, "each once");
        check(!found.empty() && found[0].campaign, "the most complete first");
        check(findGames({root / "none"}, {}).empty(), "none found");
    }

    void testVolumes()
    {
        const std::uint64_t needed = 700;
        Volume card;
        card.root = "/media/sd";
        card.format = "vfat";
        card.removable = true;
        card.free = 4000;
        Volume system = card;
        system.root = "/";
        system.system = true;
        system.format = "ext4";
        Volume windows = card;
        windows.root = "C:/";
        windows.system = true;
        windows.format = "FAT32";
        Volume data = card;
        data.root = "/mnt/data";
        data.removable = false;
        Volume ntfs = card;
        ntfs.root = "/media/usb";
        ntfs.format = "ntfs";
        Volume ro = card;
        ro.root = "/media/ro";
        ro.readOnly = true;
        Volume full = card;
        full.root = "/media/full";
        full.free = 10;
        Volume usedCard = card;
        usedCard.root = "/media/ds";
        usedCard.hasNds = true;
        usedCard.hasTwilight = true;
        Volume exfat = card;
        exfat.root = "/media/ex";
        exfat.format = "exfat";

        check(scoreVolume(system, needed) < 0, "the system disk is never picked");
        check(scoreVolume(windows, needed) < 0, "... even in the right format");
        check(scoreVolume(data, needed) < 0, "nor a fixed data disk");
        check(scoreVolume(ntfs, needed) >= 0, "any format may be picked");
        check(scoreVolume(ro, needed) < 0, "nor what cannot be written");
        const auto ranked = rankVolumes({system, data, full, exfat, card, usedCard, ntfs}, needed);
        check(ranked.size() == 5, "those fit to offer");
        check(!ranked.empty() && ranked[0].root == usedCard.root, "a card already used in a console first");
        check(ranked.size() == 5 && ranked[1].root == card.root, "then a plain card");
        check(ranked.size() == 5 && ranked[2].root == exfat.root, "then exFAT");
        check(ranked.size() == 5 && ranked[3].root == ntfs.root, "then another format");
        check(ranked.size() == 5 && ranked[4].root == full.root, "one too full last");
        check(formatOf("VFAT") == Format::Good && formatOf("FAT32") == Format::Good && formatOf("exFAT") == Format::Warn &&
                  formatOf("NTFS") == Format::Other && formatOf("ext4") == Format::Other && formatOf("") == Format::Other &&
                  formatOf("fuseblk") == Format::Other,
              "the formats");
    }

    void testLook(const fs::path &root)
    {
        Volume volume;
        volume.root = root / "looked";
        put(volume.root / "_NDS/TWiLightMenu/settings.ini");
        lookAtVolume(volume);
        check(volume.hasNds && volume.hasTwilight, "a console's card is seen for what it is");
    }

    void testCopy(const fs::path &root)
    {
        const fs::path data = root / "cache/_nds/hl_nds/data";
        const fs::path card = root / "card";
        const fs::path out = card / "_nds/hl_nds/data";
        put(data / "manifest.bin", "new list");
        put(data / "common.npac", "common");
        put(data / "maps/a.npac", "aaaa");
        put(data / "air/a.hgs", "stream");
        put(data / "saves/stray.sav", "never");
        put(data / "settings.dat", "never");
        put(data / "maps/b.npac.tmp12", "half written");
        put(root / "half-life-nds-port.nds", "rom");
        // What the player has, and an old install.
        put(out / "saves/auto.sav", "mine");
        put(out / "settings.dat", "my settings");
        put(out / "achievements.dat", "mine too");
        put(out / "manifest.bin", "old list");
        put(out / "maps/old.npac", "old map");
        put(out / "common.npac", "common");

        CopyPlan plan = planCopy(data, root / "half-life-nds-port.nds", card);
        check(plan.same == 1, "what is there already is not copied");
        check(!plan.copy.empty() && plan.copy.back().to.filename() == "manifest.bin", "the list of maps last");
        check(plan.remove.size() == 1 && plan.remove[0].filename() == "old.npac", "a map no longer wanted goes");

        // Cancelled at once: nothing in place, and the old list still rules.
        std::atomic<bool> stop{true};
        std::string why;
        check(!runCopy(plan, nullptr, &stop, why), "a cancelled copy says so");
        check(read(out / "manifest.bin") == "old list", "... the old list still there");

        plan = planCopy(data, root / "half-life-nds-port.nds", card);
        std::uint64_t seen = 0;
        check(runCopy(plan, [&](std::uint64_t done, std::uint64_t, const std::string &) { seen = done; }, nullptr, why), "it finishes");
        check(seen == plan.bytes, "every byte said");
        check(read(out / "manifest.bin") == "new list", "the new list");
        check(read(out / "maps/a.npac") == "aaaa" && read(out / "air/a.hgs") == "stream", "the files");
        check(read(card / "half-life-nds-port.nds") == "rom", "the program at the card's root");
        check(!fs::exists(out / "maps/old.npac"), "the old map gone");
        check(read(out / "saves/auto.sav") == "mine", "the player's save kept");
        check(read(out / "settings.dat") == "my settings" && read(out / "achievements.dat") == "mine too", "... and settings");
        check(!fs::exists(out / "saves/stray.sav"), "none of the cache's");
        check(!fs::exists(out / "manifest.bin.new"), "no temporary left");
        check(!fs::exists(out / "maps/b.npac.tmp12"), "nor one a run left behind");

        plan = planCopy(data, root / "half-life-nds-port.nds", card);
        check(plan.copy.empty() && plan.remove.empty(), "the same again: nothing to do");

        // No program to put there: the rest still goes.
        plan = planCopy(data, root / "not-there.nds", root / "card2");
        check(runCopy(plan, nullptr, nullptr, why) && fs::exists(root / "card2/_nds/hl_nds/data/manifest.bin"), "without the program");
        // A card that takes nothing.
        put(root / "blocked", "a file, not a folder");
        plan = planCopy(data, {}, root / "blocked");
        check(!runCopy(plan, nullptr, nullptr, why) && !why.empty(), "a card that cannot be written says why");
    }

    /// The program built into the converter, put on a card: when it has
    /// none, or another; not again when it has this one.
    void testRomPut(const fs::path &root)
    {
        const std::uint8_t mine[] = {'r', 'o', 'm', 0, 255, 1};
        const std::uint8_t newer[] = {'r', 'o', 'm', 0, 255, 2};
        const fs::path file = root / "card" / kRomName;
        std::string why;
        check(putRom(mine, file, why) == RomPut::Written && read(file) == std::string("rom\0\xff\1", 6),
              "a card with no program is given it");
        const auto when = fs::last_write_time(file);
        check(putRom(mine, file, why) == RomPut::Same && fs::last_write_time(file) == when,
              "... and not again when it has it");
        check(putRom(newer, file, why) == RomPut::Written && read(file) == std::string("rom\0\xff\2", 6),
              "another (an older one) is replaced");
        put(file, "short");
        check(putRom(mine, file, why) == RomPut::Written && read(file).size() == 6, "... whatever its size");
        check(!fs::exists(root / "card" / (std::string(kRomName) + ".new")), "no temporary left");
        put(root / "blocked", "a file, not a folder");
        check(putRom(mine, root / "blocked" / kRomName, why) == RomPut::Failed && !why.empty(),
              "a card that cannot be written says why");
    }

    void testParts()
    {
        check(partOfMap("t0a0b1") == Part::HazardCourse, "Hazard Course");
        check(partOfMap("hldemo2") == Part::Uplink, "Uplink");
        check(partOfMap("stalkyard") == Part::Multiplayer && partOfMap("crossfire") == Part::Multiplayer, "multiplayer");
        check(partOfMap("c1a0") == Part::Campaign && partOfMap("c0a0e") == Part::Campaign, "campaign");
        const std::uint64_t mb = 1024 * 1024;
        check(spaceFor(125) > 600 * mb && spaceFor(125) < 800 * mb, "the whole game's size");
    }
} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "hlds_test_install";
    std::error_code ec;
    fs::remove_all(root, ec);
    testVdf();
    testGame(root / "g");
    testFind(root / "f");
    testVolumes();
    testLook(root);
    testCopy(root / "c");
    testRomPut(root / "r");
    testParts();
    fs::remove_all(root, ec);
    if (failures == 0)
        std::printf("test_install: ok\n");
    return failures == 0 ? 0 : 1;
}
