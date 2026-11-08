#pragma once

// Global process/init singleton. Brings up the console hardware (IRQs, DSi
// clock, FAT, Nitro Engine Advanced) and exposes the on-SD data paths the rest
// of the runtime builds on. Same shape as the Riven port's Global.
//
// SCREENS: the 3D world is on the TOP screen and the touch screen is the aim
// pad, the way DS first-person shooters do it.
//
// TWO STEPS. Init() brings up everything that does not depend on the map;
// InitVideo() brings up the 3D engine.
//
// The hardware draws 2048 polygons and 6144 vertices a frame and no more, and
// every map is drawn in ONE pass, at 60 frames a second: what a view asks for
// beyond that is left out from the far end and hidden in fog (WorldDraw).
// Video banks A, B and C are textures; D is kept back for FrameFreeze, which
// captures the last picture of a map into it while the next one loads.

#include "global_header.hpp"

#include "HlNetMaps.hpp"

class Global
{
public:
    const char *fatDevice = nullptr;   ///< libfat default drive, e.g. "fat:" / "sd:"
    std::string fatDeviceCPP;          ///< same as a std::string, for path building
    bool hasFat = false;               ///< FAT init succeeded and a drive is mounted
    /// There is the game's data to read: on the card, or (a console that was
    /// sent the game, HLDS_CHILD) received, on the "air:" drive
    /// (net/AirDevice.hpp). What is WRITTEN -- saves, settings -- is the
    /// card's alone, and goes by hasFat.
    bool hasData = false;
    /// Without a card: what the console was like when it looked for one (the
    /// mode it runs in, its SD switch, the DLDI driver the loader put in, the
    /// path it was started from) -- said on the screen that says so, which is
    /// how a loader that leaves the card out is told from another.
    std::string cardWhy;
    /// Booted in DSi mode: 16 MB of main RAM and the 134 MHz clock are available.
    /// False on a DS, a DS Lite, and on a DSi/3DS running the ROM in DS mode
    /// (most flashcards).
    bool isDSi = false;
    /// Sent the game, and running in DSi mode with its 16 MB: set up by the
    /// start of the program itself (tools/dlp_twl_child.py) -- not told apart
    /// by isDSi alone, which a stale header can fool.
    bool twlChild = false;
    /// The ARM9 is running at 134 MHz. Only ever true when isDSi is.
    bool fastClock = false;
    /// The console shows the top screen at 16:10: a 3DS whose TwlBg is
    /// TWPatch's wide one (stretching 256 by 192 to 384 by 240). What the
    /// screen setting's auto follows.
    bool wideTop = false;

    /// IRQs, clock and FAT: everything but the picture. Call once at startup.
    /// `launchedFrom`: argv[0], if the loader gave one.
    void Init(const char *launchedFrom = nullptr);

    /// The 3D engine. Call after Init().
    void InitVideo();
    bool videoReady() const { return videoReady_; }

    /// One picture of `draw` (NEA_Process).
    void process(void (*draw)());

    /// The moment what is drawn is drawn at: the vertical blanks when the
    /// frame began (latchDrawClock, once a frame), so that everything that
    /// moves with time (an animated texture, a sprite's frame) is at the same
    /// moment in one picture.
    static std::uint32_t drawClock();
    static void latchDrawClock();

    /// Vertical blanks since boot: the game's clock. Sixty to the second.
    static std::uint32_t vblanks();

    /// Bytes the heap can still grow by. Not the same as "free memory" -- blocks
    /// already released back to the allocator are not counted -- but it is the
    /// number that says whether a level of a given size can still be loaded.
    std::size_t heapHeadroom() const;

    // SD data paths (only meaningful when hasFat). Mirrors the layout the asset
    // converter writes: <drive>/_nds/hl_nds/data/.
    //
    // What the game reads is in archives (Nitro Engine Advanced's NPAC, each
    // mounted as a drive of its own): one a map, holding all the map loads
    // in the order it loads it, and one for what belongs to no map. Opening a
    // file on a card is slow; a map used to be two hundred of them. Loose
    // beside the archives: what the game writes (saves, settings) and the
    // music, which is played off the card as it goes.
#ifdef HLDS_CHILD
    // (Sent the game: what it was sent is laid out as on a card.)
    std::string dataDir() const { return "air:/"; }
#else
    std::string dataDir() const { return fatDeviceCPP + "_nds/hl_nds/data/"; }
#endif
    /// A map's archive: maps/<name>.npac.
    std::string mapArchive(const std::string &name) const { return dataDir() + "maps/" + name + ".npac"; }
    /// Inside the archive of the map being played: "level.0", "tex/...",
    /// "spr/...", "mdl/...", "snd/...".
#ifdef HLDS_CHILD
    // (Sent the game, there are no archives: the map's files, and the
    // common ones, are on the "air:" drive as they come -- net/AirDevice.hpp.
    // What the group's base has, the loaders borrow: net/GuestLoad.hpp.)
    std::string mapPath(const std::string &rel) const { return "air:/" + rel; }
    std::string commonPath(const std::string &rel) const { return "air:/" + rel; }
#else
    std::string mapPath(const std::string &rel) const { return "map:/" + rel; }
    /// Inside the card's common archive: "ui/...", "snd/...".
    std::string commonPath(const std::string &rel) const { return "common:/" + rel; }
#endif
    std::string texDir() const { return mapPath("tex/"); }
    /// Written by the game, never by the converter.
    std::string savesDir() const { return dataDir() + "saves/"; }
    std::string dataPath(const std::string &rel) const { return dataDir() + rel; }

    /// Put a map's archive in the place of whichever is mounted. False if
    /// it is not on the card (and then none is).
    bool mountMap(const std::string &name);
    void unmountMap();
    /// The common archive, if it is not mounted already. (The 3D engine
    /// lets go of every archive when it is brought up again: InitVideo.)
    void mountCommon();

    /// Why the game cannot start. Everything below Ok means the card does not
    /// carry a usable conversion, which would otherwise show up as a black
    /// screen rather than as anything the player could act on.
    enum class DataStatus
    {
        Ok,
        NoCard,       ///< no FAT device at all (no SD, or the flashcard's DLDI failed)
        NoData,       ///< the card is readable but holds no manifest: never converted
        OtherVersion, ///< converted by a converter this build does not match
        Damaged,      ///< a manifest that does not check out, or lists no maps
    };

    /// The version of the converter that made the card's data ("": not known).
    std::string dataVersion;

    /// Read the converter's manifest and with it the map to start in. Cheap --
    /// one small file -- and done before the 3D engine is up.
    /// (`multiplayer`, if given: the card's maps that a game between consoles
    /// can be played on -- each with its places to come in at, and whether
    /// it can be sent to a console without the game.)
    /// (`why`, if given: for OtherVersion, the two numbers that did not match.)
    /// (`madeBy`, if given: the version of the converter that made the data.)
    DataStatus ReadManifest(std::string &startMap, std::vector<hldata::NetMapInfo> *multiplayer = nullptr,
                            std::string *why = nullptr, std::string *madeBy = nullptr) const;

    /// Two lines of player-facing explanation for a status: what is wrong and
    /// what to do about it. Never null.
    static const char *DataStatusTitle(DataStatus s);
    static const char *DataStatusHint(DataStatus s);

private:
    bool videoReady_ = false;
    std::string mapMounted_; ///< the map whose archive is the "map" drive, if any
};

extern Global global;
