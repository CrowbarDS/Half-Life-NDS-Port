#pragma once

// From the converter's own files to what goes on the card.
//
// Cooking writes loose files -- levels, textures, models, sounds -- into a
// stage folder on the PC, where a texture or a sound shared by fifty maps is
// made once and found again by the next run. The console is given something
// else: one archive per map, holding all that the map loads in the order it
// loads it (NpacWriter.hpp), and one more for what belongs to no map. On a
// card, opening a file is the slow part; a map used to be two hundred of
// them.
//
// What a map needs is read out of its level, with the very functions the
// console names its files by, so the two cannot disagree about a name.

#include <filesystem>
#include <string>
#include <vector>

namespace hldata
{
    struct LevelView;
}

namespace hlds
{
    struct PackReport
    {
        int members = 0;
        int compressed = 0;
        int missing = 0;            ///< named by the level, not in the stage
        std::size_t rawBytes = 0;   ///< what the members come to
        std::size_t cardBytes = 0;  ///< the archive
        std::uint32_t indexBytes = 0; ///< what its index takes on the console (HlArchive.hpp)
    };

    /// <stage>/maps/<map>.hlm and all it names -> <card>/maps/<map>.npac.
    /// `stage` and `card` are the two data folders (Converter::dataDir).
    bool packMap(const std::filesystem::path &stage, const std::filesystem::path &card,
                 const std::string &map, bool compress, PackReport &report, std::string &error);

    struct AirReport
    {
        struct Map
        {
            std::string name;
            std::size_t bytes = 0; ///< its own stream, without the base
            /// What it holds of each kind (hldata::GuestKind), undone; then
            /// what of it is kept packed (the weapons lying about), and the
            /// biggest of that undone.
            std::size_t raw[10] = {};
            int missing = 0;
        };
        std::size_t baseBytes = 0;
        std::size_t baseRaw[10] = {};
        std::vector<Map> maps;
    };

    /// What a console that has no card is sent of a group of maps (the
    /// game between consoles' maps, the Hazard Course's, Uplink's), as a DS
    /// reads them, as guest streams (HlGuest.hpp): <card>/air/<group>.base.hgs,
    /// the models and sounds all of them share, kept from one map to the
    /// next; and <card>/air/<map>.hgs, the rest of each.
    /// `multiplayer`: the maps of a game between consoles, where the suit
    /// says nothing (its words are left out).
    bool packAir(const std::filesystem::path &stage, const std::filesystem::path &card, const std::string &group,
                 const std::vector<std::string> &maps, bool multiplayer, AirReport &report, std::string &error);

    /// The common stream of a console with no card: <card>/air/common.hgs,
    /// what of <card>/common.npac it uses (none of the title's pictures, no
    /// weapon from another map, no word of what the people say).
    bool packAirCommon(const std::filesystem::path &stage, const std::filesystem::path &card, PackReport &report,
                       std::string &error);

    /// <stage>/ui/* and the menu's sounds -> <card>/common.npac.
    /// Which of a level's sounds are only words of what its people say
    /// (its talk table): those are in the common archive, not the map's.
    void talkWords(const hldata::LevelView &view, std::vector<char> &word);

    bool packCommon(const std::filesystem::path &stage, const std::filesystem::path &card,
                    bool compress, PackReport &report, std::string &error);

    /// <card>/air/<map>.dsi.hgs: the whole of a map at the quality this card
    /// has it, for a console sent the game that runs in DSi mode with its
    /// 16 MB: the host's level as it is, the finer models, the better sounds,
    /// the weapons it has not, in the order that console's loaders ask for
    /// them. `bytes`: how big, `memory`: what it comes to in that memory
    /// (all but its textures' video-memory part).
    ///
    /// `demo`: a demo's map (HlDemo.hpp), whose console is sent all of its
    /// bank before it plays: every sound goes to the bank, kept as it is and
    /// on a 4-byte boundary, to be played where it lies; and the bank names
    /// the tracks the map plays (kDemoTracksFile). `bankBytes`: the bank's size.
    bool packAirFull(const std::filesystem::path &stage, const std::filesystem::path &card, const std::string &map,
                     std::size_t &bytes, std::size_t &memory, std::string &error, bool demo = false,
                     std::size_t *bankBytes = nullptr);

    /// A level as a console sent the map has it: each texture made as much
    /// smaller as its TextureRef::guestHalvings say (the converter cooked it
    /// that size too), the coordinates its surfaces give it with it, and
    /// none of them streamed. Every record where it was, every stream as
    /// long. False if the level does not open (or not after).
    bool guestTextures(std::vector<std::uint8_t> &level);

    /// A map's archive read back: every member inflated and the level put
    /// together again, as the console will. False if anything is amiss.
    bool unpackLevel(const std::filesystem::path &archive, std::vector<std::uint8_t> &level,
                     std::size_t &archiveBytes, std::string &error);
} // namespace hlds
