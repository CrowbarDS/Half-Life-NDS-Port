#pragma once

// The conversion as a whole: a game folder in, a card's worth of files out.
//
// This is the layer the command line and the GUI both drive. It owns the
// decisions that are about a RUN rather than about one file -- where things go
// on the card, which maps exist, what the manifest says afterwards -- and
// reports through plain callbacks, so that it knows nothing about a terminal
// or a window.

#include <utility>
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "hlds/Portals.hpp"
#include "hlds/TitlesFile.hpp"
#include "HlManifest.hpp"
#include "hlds/LightBake.hpp"

namespace hlds
{
    inline constexpr const char *kConverterVersion = "0.1.0";
    /// The map a new game starts in.
    inline constexpr const char *kDefaultStartMap = "c0a0";

    struct ConvertOptions
    {
        std::filesystem::path gameDir;  ///< the game's "valve" folder
        std::filesystem::path cardRoot; ///< the root of the SD card
        /// Where the converter keeps its own files -- levels, textures,
        /// models and sounds one by one, which the card's archives are then
        /// made from, and which the next run finds again instead of making.
        /// Empty: a folder beside the card's, named after it ("<card>-stage").
        std::filesystem::path stageRoot;
        bool compress = true; ///< LZ77 inside the archives, where it pays (NpacWriter.hpp)
        std::filesystem::path ptexconv; ///< empty: look for it
        bool textures = true;
        int jobs = 0; /// maps cooked concurrently, one per thread
        bool streamTextures = true; /// stream the plain textures only worn by world
        double lightScale = LightBake::kDefaultScale;
        double lightPower = LightBake::kDefaultPower;
        bool guardedMerges = true; ///< CookOptions::guardedMerges
        bool groupByLeaf = true;   ///< CookOptions::groupByLeaf
        std::string startMap; ///< empty: keep the manifest's, or the default
        /// Told of the parts of a run that are no map's: a phrase each.
        std::function<void(const std::string &)> status;
    };

    struct MapResult
    {
        std::string name;
        bool ok = false;
        std::string error;
        std::vector<std::string> warnings;
        std::size_t levelBytes = 0;
        int surfaces = 0;
        int faces = 0;        ///< drawn faces before grouping
        int faceVertices = 0; ///< vertices one face at a time would cost
        int vertices = 0;     ///< vertices actually cooked
        int streamKb = 0;
        int worstPolygons = 0;
        int worstVertices = 0;
        int pvsWorstPolygons = 0; ///< CookReport::pvsWorstPolygons
        int pvsWorstVertices = 0;
        int farUnits = 0;
        int portals = 0;
        int surfacesSpanningLeaves = 0;
        std::vector<SeenWorst::Hotspot> hotspots;
        int texturesUsed = 0;
        int texturesEncoded = 0;
        int texelKb = 0;
        int streamedTextures = 0;   ///< (0 when the map's textures are not streamed)
        int streamWorstViewKb = 0;  ///< texels the worst view lists of them
        int streamRegionKb = 0;     ///< what they may take of one view
        int streamResidentKb = 0;   ///< and what the rest of its 4x4 textures take for good
        int streamTotalKb = 0;      ///< all the streamed textures together
        int alphaTexelKb = 0; ///< A3I5 texels, a byte each
        int brushes = 0;
        int logicRecords = 0;
        int unsupportedEntities = 0; ///< entities whose class the port does not handle yet
        int stoodIn = 0; ///< (a multiplayer map) pickups given as a weapon the port has, for one it has not
        int unsupportedClasses = 0;
        /// ... which, and how many of each ("monster_flyer" 3, ...).
        std::vector<std::pair<std::string, int>> unsupported;
        int models = 0;      ///< models cooked for the map's actors
        int actors = 0;
        int modelKb = 0;     ///< their files, skins apart
        int modelKbDsi = 0;  ///< ... as a DSi loads them (with the game's own meshes on top)
        /// Their triangles, one of each, added up: at the finest level a DS
        /// has, at the coarsest, and as the game has them (a DSi's finest).
        int modelTriangles = 0;
        int modelTrianglesFar = 0;
        int modelTrianglesFull = 0;
        int modelTexelKb = 0; ///< their skins, in texels
        /// A map a console may be sent: its textures as that console has
        /// them, all at once (K texels), and how many are smaller there.
        int guestTexelKb = 0;
        int guestShrunk = 0;
        int sounds = 0;       ///< sounds the map plays
        int soundKb = 0;      ///< what they take in memory on the console
        int soundsHalved = 0; ///< of them, cooked at half the rate to fit
        int soundKbDsi = 0;   ///< what they take on a DSi, which has the better copies
        int soundsTwinned = 0; ///< how many have a better copy for a DSi
        int soundsMissing = 0; ///< named by the map, not in the game folder
        int sprites = 0;       ///< sprites the map draws
        int spriteTexelKb = 0; ///< what their atlases take of texture memory
        int spritesMissing = 0;
        int musicTracks = 0;  ///< tracks of music the map starts
        int pathNodes = 0;     ///< where what walks may stand
        int pathLinks = 0;     ///< ... and the ways between
        int pathDoorLinks = 0; ///< of them, through a door or something that breaks
        int pathRefused = 0;   ///< links of the game's graph the console's walk could not make
        int pathDrops = 0;     ///< ways down added
        bool pathFromGraph = false; ///< from the game's own graph (maps/graphs)
        int archiveKb = 0;    ///< the map's archive on the card
        int archiveRawKb = 0; ///< what is in it, inflated
        int archiveMembers = 0;
    };

    /// What was cooked for the card as a whole (UiCook.hpp).
    struct CommonResult
    {
        int fonts = 0;     ///< of the three
        bool hud = false;  ///< the head-up display's pictures
        bool loading = false;
        bool menu = false;  ///< the menu's background
        bool logo = false;  ///< the animated title
        bool intro = false; ///< the Valve intro (its pictures, and its sound)
        bool icons = false; ///< the weapons' pictures for the touch screen
        bool title = false; ///< the game's title
        bool decals = false; ///< the marks shots and blood leave
        int sounds = 0;    ///< the menu's
        std::vector<std::string> warnings;
        /// What each map sent to a console with no card takes of its memory
        /// (HlGuest.hpp): a line each, for the report.
        std::vector<std::string> guest;
        /// ... and the same, by map, for the manifest.
        struct Guest
        {
            std::uint32_t peak = 0;
            bool fits = false;
        };
        std::map<std::string, Guest> guestByMap;
    };

    class Converter
    {
    public:
        explicit Converter(ConvertOptions options);

        /// <card>/_nds/hl_nds/data -- the DS builds the same path
        /// (Global::dataDir).
        static std::filesystem::path dataDir(const std::filesystem::path &cardRoot);

        /// What can be checked before any work is done: the folders exist, the
        /// game folder holds maps, the encoder can be found. Empty when all is
        /// well, else a sentence for the user.
        std::string preflight() const;

        /// The game's maps, lower-case, without extension, sorted.
        std::vector<std::string> listMaps() const;

        /// Cook what belongs to the card, not to a map: the fonts, the
        /// head-up display, the loading picture, the menu's sounds. Whatever
        /// the game folder has not got is said and gone without.
        CommonResult cookCommon();
        /// What the last cookMaps made of that.
        const CommonResult &common() const { return common_; }

        /// The stage's data folder, laid out as the card's used to be.
        std::filesystem::path stageDir() const;

        /// Cook one map and its textures into the stage. Does not make its
        /// archive, nor touch the manifest: cookMaps does both.
        MapResult cookMap(const std::string &name);

        /// Make the card's archives again from what is in the stage, cooking
        /// nothing, and write the manifest. Returns the number that failed.
        int packAll(const std::function<void(const MapResult &)> &done, std::string &error);

        /// Cook several maps and then write the manifest, which lists every
        /// map cooked now or by an earlier run that is still on the card.
        /// `progress` is called before each map with its index; `cancel`, if
        /// given, is checked between maps, and a run cancelled leaves its
        /// archives as they are, with no new manifest. Returns the number
        /// that failed.
        int cookMaps(const std::vector<std::string> &names,
                     const std::function<void(std::size_t, const std::string &)> &progress,
                     const std::function<void(const MapResult &)> &done,
                     const std::atomic<bool> *cancel, std::string &error);

    private:
        bool findMap(const std::string &name, std::filesystem::path &out) const;
        void packInto(MapResult &result) const;
        bool writeManifest(std::string &error) const;
        /// The air archives (Pack.hpp, packAir) of the staged maps, by group.
        void packAirGroups();

        ConvertOptions options_;
        CommonResult common_;
    };
    /// Which chapter `map` is part of: the name, in the game's titles.txt,
    /// of that chapter's title. A map that opens a chapter says so itself
    /// (its worldspawn's "chaptertitle"); the others are of the chapter
    /// opened by the map whose name is most like theirs -- of those that
    /// share their first four letters, the last one at or before them (or
    /// the first, if they all come after: c3a2 is of the chapter c3a2e
    /// opens); failing that, the last one before them of any name. A title
    /// that says nothing (titles.txt has some) opens no chapter. `titled`:
    /// each map that names a title, and the title -- by map name, sorted.
    std::string chapterOf(const std::string &map, const std::vector<std::pair<std::string, std::string>> &titled);
    /// Every map in the game's folder that names a chapter's title, and the
    /// title: read out of each map's own entities.
    std::vector<std::pair<std::string, std::string>> chapterTitles(const std::filesystem::path &gameDir,
                                                                   const TitlesBook *titles);
} // namespace hlds
