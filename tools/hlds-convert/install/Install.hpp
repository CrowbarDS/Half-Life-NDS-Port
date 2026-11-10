#pragma once

// What the installer decides, with no window and no Qt: where Half-Life is,
// which volume is the console's card, and how a finished conversion is put on
// it. Everything here is given what it looks at, so it can be tested with
// folders made up for the purpose; what only a running system can say (its
// volumes, its registry) is the application's to find out and hand in.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace hlds::install
{
    // ------------------------------------------------------------- Half-Life

    /// The libraries a Steam folder's steamapps/libraryfolders.vdf names.
    std::vector<std::filesystem::path> steamLibraries(const std::string &vdf);

    /// Where Steam may be for someone with this home folder, on this system.
    enum class System
    {
        Linux,
        Windows,
        Mac,
    };
    std::vector<std::filesystem::path> steamRoots(const std::filesystem::path &home, System system);

    struct GameFolder
    {
        std::filesystem::path valve; ///< the "valve" folder, whichever way it was reached
        bool ok = false;             ///< it is Half-Life
        std::string why;             ///< if not, a sentence for the player
        bool campaign = false;       ///< the whole game, c0a0 to the end
        bool hazardCourse = false;
        bool uplink = false;
        int maps = 0;
    };
    /// What is at `folder`: the valve folder itself, the game's folder above it
    /// ("Half-Life"), or something else. Names are compared without case.
    GameFolder checkGame(const std::filesystem::path &folder);

    /// Every Half-Life that can be found: `roots` (Steam's) and their libraries,
    /// then `others`. The most complete first, each once.
    std::vector<GameFolder> findGames(const std::vector<std::filesystem::path> &roots,
                                      const std::vector<std::filesystem::path> &others);

    // ------------------------------------------------------------- the parts

    enum class Part
    {
        Campaign,
        HazardCourse,
        Uplink,
        Multiplayer,
    };
    /// Which part a map's name makes it: the campaign's are all c<n>a..., and
    /// what is left of the game's folder is for matches.
    Part partOfMap(const std::string &name);

    /// How much a set of `maps` comes to on the card, near enough to warn by.
    std::uint64_t spaceFor(int maps);

    // --------------------------------------------------------------- the card

    struct Volume
    {
        std::filesystem::path root;
        std::string name;
        std::string format;      ///< as the system calls it: "vfat", "FAT32", "exfat"...
        std::uint64_t total = 0, free = 0;
        bool removable = false;
        bool system = false;     ///< holds the running system or the player's home
        bool readOnly = false;
        bool hasNds = false;     ///< an _nds folder at its root
        bool hasTwilight = false;
    };

    enum class Format
    {
        Good,  ///< FAT32
        Warn,  ///< exFAT: homebrew loaders vary
        Other, ///< not FAT: fine if the player knows their loader reads it
    };
    Format formatOf(const std::string &name);

    /// How much like the console's card it looks; below 0: never picked by
    /// itself. The best is the one to preselect.
    int scoreVolume(const Volume &volume, std::uint64_t needed);
    /// `volumes` with those that are never offered gone, the best first.
    std::vector<Volume> rankVolumes(std::vector<Volume> volumes, std::uint64_t needed);
    /// An _nds and TWiLight look, filled in from what is on it.
    void lookAtVolume(Volume &volume);

    // --------------------------------------------------------------- the copy

    struct CopyPlan
    {
        struct Item
        {
            std::filesystem::path from, to;
            std::uint64_t bytes = 0;
        };
        std::vector<Item> copy;    ///< what differs, the manifest last
        std::vector<std::filesystem::path> remove; ///< of ours, no longer wanted
        std::uint64_t bytes = 0;
        int same = 0;              ///< files already there
    };
    /// From `data` (a finished _nds/hl_nds/data), to `card`'s, the ROM (if any)
    /// to its root. The player's saves, settings and achievements are never
    /// part of it.
    CopyPlan planCopy(const std::filesystem::path &data, const std::filesystem::path &rom,
                      const std::filesystem::path &card);

    /// bytes done, of all; the file now.
    using CopyProgress = std::function<void(std::uint64_t, std::uint64_t, const std::string &)>;
    /// Each file to a temporary name, read back, then into place; so a card
    /// pulled out or a run cancelled leaves the last manifest in charge, and
    /// the next run carries on. False, and why, if it did not finish.
    bool runCopy(const CopyPlan &plan, const CopyProgress &progress, const std::atomic<bool> *cancel,
                 std::string &why);
    // ------------------------------------------------------------- online
    /// The project's relay for online play (tools/relay), which a card is
    /// given unless it says another.
    inline constexpr const char *kRelayHost = "164.132.255.209";
    inline constexpr int kRelayPort = 27500;

    enum class IniPut
    {
        Kept,    ///< the card says where its relay is already: left as it is
        Written, ///< there was none: the project's, now
        Failed,
    };
    /// `<card>/_nds/hl_nds/data/server.ini`, saying where the relay is (the
    /// console's ONLINE needs it), unless the card has one: one that says
    /// another relay is the player's own choice, and is never written over.
    IniPut putServerIni(const std::filesystem::path &card, std::string &why);
    // ------------------------------------------------------------- the stage
    /// Where this user's programs keep what they can make again: $XDG_CACHE_HOME
    /// or ~/.cache, ~/Library/Caches on macOS, %LOCALAPPDATA% on Windows.
    /// Empty if the system says none.
    std::filesystem::path userCacheFolder();
    /// Where the converter keeps its own files for the card at `card` (the
    /// stage: what the archives are made from, found again by the next
    /// run), when it is not told: `<card>-stage` if one is there already (a
    /// card converted before carries on where it was; `kept` then true);
    /// else a folder of its own under `cache` (userCacheFolder), named for
    /// the card -- never in the folder it is run from, nor beside a card at
    /// a drive's or a mount's root. With no `cache`, beside the card.
    std::filesystem::path defaultStage(const std::filesystem::path &card, const std::filesystem::path &cache, bool &kept);
} // namespace hlds::install
