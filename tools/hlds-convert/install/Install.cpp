#include "Install.hpp"
#include "BundledRom.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace hlds::install
{
    namespace
    {
        std::string lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        /// `name` in `folder`, whatever its case; empty if there is none.
        fs::path findNoCase(const fs::path &folder, const std::string &name)
        {
            std::error_code ec;
            const std::string wanted = lower(name);
            for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            {
                if (lower(it->path().filename().string()) == wanted)
                    return it->path();
            }
            return {};
        }

        /// The maps of the campaign that say it is all there: its first, one
        /// from each stretch, and its last.
        const char *const kCampaignMarks[] = {"c0a0", "c1a0", "c2a1", "c3a1", "c4a1", "c5a1"};
        const char *const kHazardMarks[] = {"t0a0", "t0a0d"};
        const char *const kUplinkMarks[] = {"hldemo1", "hldemo3"};

        std::uint64_t sizeOf(const fs::path &path)
        {
            std::error_code ec;
            const std::uint64_t size = fs::file_size(path, ec);
            return ec ? 0 : size;
        }

        /// A file just written, onto the card itself: the system keeps what is
        /// written for a while, and a card taken out before it is gone there
        /// holds files of the right size with nothing in them. False if the
        /// card would not say it has it.
        bool flushToDisk(const fs::path &path)
        {
#ifdef _WIN32
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                return false;
            const bool ok = FlushFileBuffers(file);
            return CloseHandle(file) && ok;
#else
            const int file = ::open(path.c_str(), O_WRONLY);
            if (file < 0)
                return false;
            const bool ok = ::fsync(file) == 0;
            return ::close(file) == 0 && ok;
#endif
        }

        /// Whether the two hold the same bytes. Anything that cannot be read
        /// is not the same: a file that would not open (another program
        /// holding it) used to count as already copied, and was never mended.
        bool sameFile(const fs::path &a, const fs::path &b)
        {
            std::error_code ec;
            if (!fs::exists(b, ec) || sizeOf(a) != sizeOf(b))
                return false;
            std::ifstream one(a, std::ios::binary), two(b, std::ios::binary);
            if (!one || !two)
                return false;
            auto bufferOne = std::make_unique<char[]>(65536);
            auto bufferTwo = std::make_unique<char[]>(65536);
            std::uint64_t compared = 0;
            while (one && two)
            {
                one.read(bufferOne.get(), 65536);
                two.read(bufferTwo.get(), 65536);
                if (one.gcount() != two.gcount() ||
                    std::char_traits<char>::compare(bufferOne.get(), bufferTwo.get(), static_cast<std::size_t>(one.gcount())) != 0)
                    return false;
                compared += static_cast<std::uint64_t>(one.gcount());
            }
            return (one.eof() && two.eof()) && compared == sizeOf(a);
        }

        /// What the player made, and the converter never does. (server.ini:
        /// where the relay for online play is, written by the player.)
        bool playersOwn(const std::string &relative)
        {
            return relative == "settings.dat" || relative == "achievements.dat" || relative == "perf.txt" ||
                   relative == "server.ini" ||
                   relative.rfind("saves", 0) == 0 || relative.rfind("debug_", 0) == 0;
        }
    } // namespace

    std::vector<fs::path> steamLibraries(const std::string &vdf)
    {
        // "path"		"C:\\Games\\Steam": the only thing wanted of it.
        std::vector<fs::path> out;
        std::size_t at = 0;
        const std::string lowered = lower(vdf);
        while ((at = lowered.find("\"path\"", at)) != std::string::npos)
        {
            at += 6;
            const std::size_t open = vdf.find('"', at);
            if (open == std::string::npos)
                break;
            std::string path;
            std::size_t c = open + 1;
            for (; c < vdf.size() && vdf[c] != '"'; ++c)
            {
                if (vdf[c] == '\\' && c + 1 < vdf.size())
                    ++c;
                path.push_back(vdf[c]);
            }
            if (!path.empty())
                out.emplace_back(path);
            at = c;
        }
        return out;
    }

    std::vector<fs::path> steamRoots(const fs::path &home, System system)
    {
        switch (system)
        {
        case System::Windows:
            return {"C:/Program Files (x86)/Steam", "C:/Program Files/Steam", "D:/Steam", "D:/SteamLibrary"};
        case System::Mac:
            return {home / "Library/Application Support/Steam"};
        case System::Linux:
            break;
        }
        return {home / ".steam/steam",
                home / ".steam/root",
                home / ".steam/debian-installation",
                home / ".local/share/Steam",
                home / ".var/app/com.valvesoftware.Steam/.local/share/Steam",
                home / "snap/steam/common/.local/share/Steam"};
    }

    GameFolder checkGame(const fs::path &folder)
    {
        GameFolder game;
        game.valve = folder;
        std::error_code ec;
        if (folder.empty() || !fs::is_directory(folder, ec))
        {
            game.why = "That folder does not exist.";
            return game;
        }
        // Its maps here? Else the game's own folder, with "valve" in it; or
        // Steam's "steamapps/common" style, a level above that.
        fs::path maps = findNoCase(folder, "maps");
        if (maps.empty())
        {
            if (fs::path valve = findNoCase(folder, "valve"); !valve.empty())
                return checkGame(valve);
            if (fs::path half = findNoCase(folder, "Half-Life"); !half.empty())
                return checkGame(half);
            game.why = "No Half-Life here: this folder has no \"valve\" folder inside it.";
            return game;
        }
        std::map<std::string, bool> have;
        for (auto it = fs::directory_iterator(maps, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            if (lower(it->path().extension().string()) == ".bsp")
            {
                have[lower(it->path().stem().string())] = true;
                ++game.maps;
            }
        }
        const auto all = [&](const auto &marks)
        {
            for (const char *mark : marks)
            {
                if (!have.count(mark))
                    return false;
            }
            return true;
        };
        game.campaign = all(kCampaignMarks);
        game.hazardCourse = all(kHazardMarks);
        game.uplink = all(kUplinkMarks);
        // (Half-Life's mark on a folder of maps: a mod's has its own wads.)
        const bool isHalfLife = !findNoCase(folder, "liblist.gam").empty() || !findNoCase(folder, "halflife.wad").empty();
        if (game.maps == 0)
            game.why = "This looks like Half-Life, but its maps folder is empty.";
        else if (!game.campaign && !game.hazardCourse && !game.uplink)
            game.why = "This folder has maps, but not Half-Life's.";
        else if (!isHalfLife)
            game.why = "This folder has Half-Life's maps, but not the rest of the game.";
        game.ok = game.why.empty();
        return game;
    }

    std::vector<GameFolder> findGames(const std::vector<fs::path> &roots, const std::vector<fs::path> &others)
    {
        std::vector<fs::path> places;
        const auto addLibrary = [&](const fs::path &library)
        {
            const fs::path common = library / "steamapps" / "common" / "Half-Life";
            places.push_back(common);
        };
        for (const fs::path &root : roots)
        {
            addLibrary(root);
            std::ifstream file(root / "steamapps" / "libraryfolders.vdf");
            if (file)
            {
                std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                for (const fs::path &library : steamLibraries(text))
                    addLibrary(library);
            }
        }
        places.insert(places.end(), others.begin(), others.end());

        std::vector<GameFolder> found;
        std::vector<fs::path> seen;
        for (const fs::path &place : places)
        {
            GameFolder game = checkGame(place);
            if (!game.ok)
                continue;
            std::error_code ec;
            fs::path real = fs::weakly_canonical(game.valve, ec);
            if (ec)
                real = game.valve;
            if (std::find(seen.begin(), seen.end(), real) != seen.end())
                continue;
            seen.push_back(real);
            found.push_back(std::move(game));
        }
        const auto worth = [](const GameFolder &game)
        { return (game.campaign ? 4 : 0) + (game.hazardCourse ? 2 : 0) + (game.uplink ? 1 : 0); };
        std::stable_sort(found.begin(), found.end(), [&](const GameFolder &a, const GameFolder &b) { return worth(a) > worth(b); });
        return found;
    }

    Part partOfMap(const std::string &name)
    {
        if (name.rfind("t0a0", 0) == 0)
            return Part::HazardCourse;
        if (name.rfind("hldemo", 0) == 0)
            return Part::Uplink;
        const bool campaign = name.size() >= 3 && name[0] == 'c' && std::isdigit(static_cast<unsigned char>(name[1])) && name[2] == 'a';
        return campaign ? Part::Campaign : Part::Multiplayer;
    }

    std::uint64_t spaceFor(int maps)
    {
        // Measured on a whole card: 640 MB for 125 maps, 50 MB of it shared.
        const std::uint64_t mb = 1024 * 1024;
        return 50 * mb + 5 * mb * static_cast<std::uint64_t>(maps > 0 ? maps : 0);
    }

    Format formatOf(const std::string &name)
    {
        const std::string format = lower(name);
        if (format == "vfat" || format == "fat32" || format == "fat" || format == "msdos" || format == "msdosfs")
            return Format::Good;
        if (format == "exfat")
            return Format::Warn;
        return Format::Other;
    }

    int scoreVolume(const Volume &volume, std::uint64_t needed)
    {
        if (volume.system || volume.readOnly)
            return -1;
        int score = 0;
        score += volume.removable ? 100 : 0;
        score += volume.hasNds ? 50 : 0;
        score += volume.hasTwilight ? 25 : 0;
        const Format format = formatOf(volume.format);
        score += format == Format::Good ? 10 : format == Format::Warn ? 5 : 0;
        score += volume.free >= needed ? 5 : -20;
        // (Not removable, and nothing of a console's on it: a data disk.)
        return !volume.removable && !volume.hasNds ? -1 : score;
    }

    std::vector<Volume> rankVolumes(std::vector<Volume> volumes, std::uint64_t needed)
    {
        volumes.erase(std::remove_if(volumes.begin(), volumes.end(),
                                     [&](const Volume &volume) { return scoreVolume(volume, needed) < 0; }),
                      volumes.end());
        std::stable_sort(volumes.begin(), volumes.end(), [&](const Volume &a, const Volume &b)
                         { return scoreVolume(a, needed) > scoreVolume(b, needed); });
        return volumes;
    }

    void lookAtVolume(Volume &volume)
    {
        const fs::path nds = findNoCase(volume.root, "_nds");
        volume.hasNds = !nds.empty();
        volume.hasTwilight = !findNoCase(volume.root, "BOOT.NDS").empty() ||
                             (!nds.empty() && !findNoCase(nds, "TWiLightMenu").empty());
    }

    CopyPlan planCopy(const fs::path &data, const fs::path &rom, const fs::path &card)
    {
        CopyPlan plan;
        const fs::path target = card / "_nds" / "hl_nds" / "data";
        std::vector<CopyPlan::Item> items;
        std::vector<std::string> wanted;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(data, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (!it->is_regular_file(ec))
                continue;
            const std::string relative = fs::relative(it->path(), data, ec).generic_string();
            if (playersOwn(relative))
                continue;
            // (A file half written when the computer was turned off.)
            if (it->path().extension().string().rfind(".tmp", 0) == 0)
                continue;
            wanted.push_back(relative);
            items.push_back({it->path(), target / relative, sizeOf(it->path())});
        }
        if (!rom.empty() && fs::exists(rom, ec))
            items.push_back({rom, card / rom.filename(), sizeOf(rom)});
        // The list of what is there goes last: until then, the last one holds.
        std::stable_partition(items.begin(), items.end(), [](const CopyPlan::Item &item)
                              { return item.to.filename() != "manifest.bin"; });
        for (CopyPlan::Item &item : items)
        {
            if (sameFile(item.from, item.to))
            {
                ++plan.same;
                continue;
            }
            plan.bytes += item.bytes;
            plan.copy.push_back(std::move(item));
        }
        // Of ours, on the card, and no longer in what is being installed.
        for (const char *folder : {"maps", "air", "music"})
        {
            for (auto it = fs::recursive_directory_iterator(target / folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (!it->is_regular_file(ec))
                    continue;
                const std::string relative = fs::relative(it->path(), target, ec).generic_string();
                if (std::find(wanted.begin(), wanted.end(), relative) == wanted.end())
                    plan.remove.push_back(it->path());
            }
            ec.clear();
        }
        return plan;
    }

    bool runCopy(const CopyPlan &plan, const CopyProgress &progress, const std::atomic<bool> *cancel, std::string &why)
    {
        auto buffer = std::make_unique<char[]>(256 * 1024);
        std::uint64_t done = 0;
        std::error_code ec;
        for (const fs::path &gone : plan.remove)
            fs::remove(gone, ec);
        for (const CopyPlan::Item &item : plan.copy)
        {
            if (cancel != nullptr && cancel->load())
            {
                why = "Stopped.";
                return false;
            }
            const std::string name = item.to.filename().string();
            fs::create_directories(item.to.parent_path(), ec);
            fs::path temp = item.to;
            temp += ".new";
            {
                std::ifstream in(item.from, std::ios::binary);
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                if (!in || !out)
                {
                    why = "Cannot write " + name + " on the card. Is it locked, or full?";
                    return false;
                }
                while (in)
                {
                    in.read(buffer.get(), 256 * 1024);
                    const std::streamsize got = in.gcount();
                    if (got > 0 && !out.write(buffer.get(), got))
                    {
                        out.close();
                        fs::remove(temp, ec);
                        why = "The card stopped taking " + name + ". It may be full.";
                        return false;
                    }
                    done += static_cast<std::uint64_t>(got);
                    if (progress)
                        progress(done, plan.bytes, name);
                    if (cancel != nullptr && cancel->load())
                    {
                        out.close();
                        fs::remove(temp, ec);
                        why = "Stopped.";
                        return false;
                    }
                }
                out.flush();
                out.close();
                if (!out || !flushToDisk(temp))
                {
                    fs::remove(temp, ec);
                    why = "The card stopped taking " + name + ". It may be full.";
                    return false;
                }
            }
            // What is on the card is what was meant to be.
            if (!sameFile(item.from, temp))
            {
                fs::remove(temp, ec);
                why = name + " came out different on the card. The card may be failing.";
                return false;
            }
            // (FAT will not rename over a file.)
            fs::remove(item.to, ec);
            ec.clear();
            fs::rename(temp, item.to, ec);
            if (ec)
            {
                why = "Cannot put " + name + " in place on the card: " + ec.message();
                return false;
            }
        }
        return true;
    }
    RomPut putRom(std::span<const std::uint8_t> bytes, const fs::path &file, std::string &why)
    {
        std::error_code ec;
        if (fs::exists(file, ec) && sizeOf(file) == bytes.size())
        {
            std::ifstream in(file, std::ios::binary);
            std::vector<char> had(bytes.size());
            if (in.read(had.data(), static_cast<std::streamsize>(had.size())) &&
                std::equal(had.begin(), had.end(), bytes.begin(),
                           [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; }))
                return RomPut::Same;
        }
        fs::create_directories(file.parent_path(), ec);
        fs::path temporary = file;
        temporary += ".new";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out.flush())
            {
                why = "cannot write " + temporary.string();
                fs::remove(temporary, ec);
                return RomPut::Failed;
            }
        }
        fs::rename(temporary, file, ec);
        if (ec)
        {
            // (A system that does not put one over another.)
            ec.clear();
            fs::remove(file, ec);
            fs::rename(temporary, file, ec);
        }
        if (ec)
        {
            why = "cannot put " + file.string() + " in place: " + ec.message();
            fs::remove(temporary, ec);
            return RomPut::Failed;
        }
        return RomPut::Written;
    }
} // namespace hlds::install
