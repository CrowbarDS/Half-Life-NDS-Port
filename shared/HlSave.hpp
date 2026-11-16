#pragma once

// The saved game: where the player is, the whole of the map they are in, and
// (from version 9) the maps they have left on the way, as they were left
// (HlMapStore.hpp's files, copied in after the rest: the "visited" trailer).
// For a map left before those were kept, or one cooked again since, what has
// been BROKEN stays broken (a bitset per map visited), and the global
// switches keep their states. Loading a save puts the player where they
// stood, facing as they faced, on the train they were riding if they were
// riding one -- exactly what walking through a level change carries, plus a
// position.
//
// On the card it is the same shape as the settings file: a fixed header with
// a magic, a version, the payload's length and its CRC, then a yas archive.
// Every check that can be made without yas is made before yas runs, because
// yas reports a bad archive by throwing and the DS build has no exceptions.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "HlYas.hpp"

#include "HlLevel.hpp"
#include "HlPlayerMove.hpp"

namespace hldata
{
    /// What is broken in one map: a bit per logic record. And who is gone
    /// from it -- killed, or picked up: a bit per actor.
    struct SaveMapState
    {
        std::uint32_t map = 0; ///< nameHash of the map's name
        std::vector<std::uint8_t> broken;
        std::vector<std::uint8_t> gone;

        template <class Ar>
        void serialize(Ar &ar)
        {
            ar &map &broken &gone;
        }
    };

    /// One map left, kept to come back to (HlMapStore.hpp): what the run's
    /// index has of it, and what a save's trailer lists.
    inline constexpr int kRunVisitors = 4; ///< (kMaxVisitors: HlMapStore.hpp checks they agree)
    inline constexpr int kStoredMapsMost = 12;
    struct RunEntry
    {
        char name[32];          ///< the map, lower case
        std::uint32_t level;    ///< as in its file
        std::uint32_t bytes;    ///< the whole file
        std::uint32_t crc;      ///< its tail's
        std::uint32_t stamp;
        std::int16_t sentAway[kRunVisitors];
        /// Bit v: sentAway[v] did come into the next map (and so is taken
        /// out of this one when it is laid back).
        std::uint8_t landed;
        std::uint8_t spare[3];
    };
    static_assert(sizeof(RunEntry) == 60);

    /// The maps kept, for one run of the game (a new game, or a save
    /// loaded, begins another: `runId`).
    struct RunIndex
    {
        std::uint32_t runId = 0;
        std::uint32_t generation = 0; ///< one more each time the folder changes
        std::int32_t count = 0;
        RunEntry entries[kStoredMapsMost] = {};
    };
    static_assert(sizeof(RunIndex) == 12 + 60 * kStoredMapsMost);

    /// What the console keeps beside the map's state, in a save: how long
    /// has been played, the chapter's title as it was last shown, the
    /// music that was playing.
    struct ConsoleKept
    {
        std::uint64_t playMs = 0;
        std::int32_t music = -1;
        std::uint32_t musicMs = 0; ///< how far into it
        char chapter[44] = {};
        std::uint32_t spare = 0;
    };
    static_assert(sizeof(ConsoleKept) == 64);

    /// A save's trailer (version 9 on), after its payload: this, the table
    /// of the maps it holds, and each map's file as it was on the card, one
    /// after another.
    inline constexpr char kVisitedMagic[4] = {'H', 'L', 'V', 'T'};
    inline constexpr std::uint16_t kVisitedByRef = 1; ///< the files are not in it: the run's folder as it was (runId, generation)
    struct VisitedHead
    {
        char magic[4];
        std::uint16_t flags;
        std::uint16_t count;
        std::uint32_t runId, generation;
        std::uint32_t bytes;    ///< the table and the files after it
        std::uint32_t tableCrc; ///< of the table
    };
    static_assert(sizeof(VisitedHead) == 24);

    struct SaveData
    {
        std::string map; ///< lower-case, no extension
        std::int32_t origin[3] = {0, 0, 0};
        std::int32_t velocity[3] = {0, 0, 0};
        std::uint16_t yaw = 0;
        std::int16_t pitch = 0;
        std::uint8_t ducked = 0;
        std::int32_t health = 100;
        // What they carry (Inventory).
        std::uint8_t weapons = 0;
        std::uint8_t weapon = 0;
        std::int16_t clipGlock = 0;
        std::int16_t clipShotgun = 0;
        std::int16_t ammo9mm = 0;
        std::int16_t ammoBuck = 0;
        std::int16_t armor = 0;
        std::int16_t grenades = 0;
        std::int16_t clipMp5 = 0;
        std::int16_t clipPython = 0;
        std::int16_t ammo357 = 0;
        std::int16_t ammoAr = 0;
        /// The lamp: alight or not, and what is left in it (0..100000).
        std::uint8_t lampOn = 0;
        std::int32_t lampCharge = 99000;

        std::vector<std::uint32_t> globalHash;
        std::vector<std::uint8_t> globalState;

        /// The track train the player is riding (the hash of its global
        /// name), or 0; and where it is.
        std::uint32_t train = 0;
        std::int32_t trainPos[3] = {0, 0, 0};
        std::uint16_t trainYaw = 0;
        std::int32_t trainSpeed = 0;
        std::uint8_t trainSelfDriven = 0;

        std::vector<SaveMapState> maps;

        /// The whole state of the map the player is in, as HlSaveState.hpp
        /// writes it (an archive of its own, with its own checks): every
        /// door, trigger and actor as it was. Empty: none -- the map is
        /// then begun again, with only what is above put right.
        std::vector<std::uint8_t> state;

        /// What they carry of the weapons that came later: after all the rest,
        /// written by writeSave and read by readSave only when it is there (a
        /// save of version 7 or before has none: then nothing of these). Not
        /// part of serialize(), which is the rest, as it always was. (Its
        /// antidotes and security cards after it, from version 9.)
        Arsenal arsenal;

        // Not in the payload: what reading the file found besides.
        std::uint16_t version = 0; ///< of the file it was read from (readSave)
        /// The maps it holds, left on the way (its trailer: saveSlotRead); and
        /// where in the file the first of their files begins -- or, `byRef`,
        /// that it holds none but names the run's folder as it was.
        std::vector<RunEntry> visited;
        std::uint32_t visitedAt = 0;
        bool visitedByRef = false;
        std::uint32_t visitedRunId = 0, visitedGeneration = 0;

        template <class Ar>
        void serialize(Ar &ar)
        {
            ar &map &origin[0] &origin[1] &origin[2] &velocity[0] &velocity[1] &velocity[2] &yaw
                &pitch &ducked &health &weapons &weapon &clipGlock &clipShotgun &ammo9mm &ammoBuck
                &armor &grenades &clipMp5 &clipPython &ammo357 &ammoAr &lampOn &lampCharge &globalHash &globalState &train &trainPos[0] &trainPos[1]
                &trainPos[2] &trainYaw &trainSpeed &trainSelfDriven &maps &state;
        }
    };

    inline constexpr char kSaveMagic[4] = {'H', 'L', 'S', 'G'};
    inline constexpr std::uint16_t kSaveVersion = 9;
    /// The oldest save format this build can read: 6.
    /// Note some fields in save format 6 are unreadable.
    /// kSaveMinReader is the oldest build that can read what this one writes;
    /// it is stored in each save as minReader. Raise both when a change would
    /// make an older reader misread a save; leave them alone when an older
    /// reader would still read it correctly.
    /// 7 -> 8: the Arsenal after the rest (a version 7 save has none, and is
    /// read as before); a build before 8 would not know the weapons it
    /// names, so it is the oldest that reads one.
    /// 8 -> 9: the maps left on the way after the payload (the visited
    /// trailer), the antidotes and security cards after the Arsenal, and what
    /// the console keeps (its weapon, its view, ConsoleKept) given a heading of
    /// its own in the state. A build before would refuse a save so long.
    inline constexpr std::uint16_t kSaveMinVersion = 6;
    inline constexpr std::uint16_t kSaveMinReader = 9;
    /// Whether this build can read a save with these header values: its
    /// version is one we still read, its minReader is not newer than this
    /// build, and the maps it was made against are still compatible
    /// (kLevelSaveSince).
    inline constexpr bool saveVersionOk(std::uint16_t version, std::uint16_t minReader, std::uint16_t levelVersion)
    {
        return version >= kSaveMinVersion && minReader <= kSaveVersion && levelSaveVersionOk(levelVersion);
    }
    /// A save with a map's whole state in it is tens of kilobytes; one that
    /// claims to be more than this is not one. (Its payload: the maps left
    /// that follow it are kept within kStoredBytesMost, HlMapStore.hpp.)
    inline constexpr std::uint32_t kSaveMaxBytes = 256 * 1024;

    // What kind of save a file is.
    inline constexpr std::uint8_t kSaveKindAuto = 0;    ///< the game's own: an autosave point, or arriving in a map
    inline constexpr std::uint8_t kSaveKindQuick = 1;   ///< the menu's SAVE
    inline constexpr std::uint8_t kSaveKindManual = 2;  ///< one of the player's six
    inline constexpr std::uint8_t kSaveKindArrival = 3; ///< on coming into the map: what dying again and again goes back to

    /// What a save says of itself, at its head, where it can be read without
    /// the rest: for the list.
    struct SaveHeading
    {
        std::uint32_t serial = 0;      ///< goes up with every save made: the highest is the most recent
        std::uint32_t playSeconds = 0; ///< how long has been played
        /// When, by the console's clock; year 0 if it could not be read.
        std::uint16_t year = 0;
        std::uint8_t month = 0, day = 0, hour = 0, minute = 0;
        std::uint8_t kind = kSaveKindAuto;
        std::uint8_t health = 0;
        char map[32] = {};     ///< lower-case, no extension
        char chapter[44] = {}; ///< the chapter's title, as the game shows it
    };
    static_assert(sizeof(SaveHeading) == 92); // (no padding in it: it is checksummed as it lies)

    struct SaveHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t levelVersion; ///< a save is of maps cooked in this format
        std::uint32_t payloadBytes;
        std::uint32_t crc32;        ///< of the payload
        std::uint32_t headingCrc;   ///< of the heading, which follows this
        std::uint16_t minReader;    ///< the oldest build that can read it (kSaveMinReader)
        std::uint16_t spare;
    };
    static_assert(sizeof(SaveHeader) == 24);
    inline constexpr std::size_t kSaveHeadBytes = sizeof(SaveHeader) + sizeof(SaveHeading);

    inline std::string saveRefusal(std::uint16_t version, std::uint16_t minReader, std::uint16_t levelVersion)
    {
        char text[64];
        if (version < kSaveMinVersion)
            std::snprintf(text, sizeof(text), "SAVE V%u, NEED V%u+", static_cast<unsigned>(version),
                          static_cast<unsigned>(kSaveMinVersion));
        else if (minReader > kSaveVersion)
            std::snprintf(text, sizeof(text), "SAVE NEEDS V%u, HAVE V%u", static_cast<unsigned>(minReader),
                          static_cast<unsigned>(kSaveVersion));
        else if (levelVersion < kLevelSaveSince)
            std::snprintf(text, sizeof(text), "MAPS L%u, NEED L%u+", static_cast<unsigned>(levelVersion),
                          static_cast<unsigned>(kLevelSaveSince));
        else
            std::snprintf(text, sizeof(text), "MAPS L%u, HAVE L%u", static_cast<unsigned>(levelVersion),
                          static_cast<unsigned>(kLevelVersion));
        return text;
    }

    // What a file turned out to be.
    inline constexpr int kSaveNone = 0;  ///< no save: not there, or not one
    inline constexpr int kSaveGood = 1;
    inline constexpr int kSaveOther = 2; ///< a save, of another version of the game or of its maps
    inline constexpr int kSaveBad = 3;   ///< damaged

    // ------------------------------------------------------------- dying
    //
    // Where a death goes back to. There are two saves: the latest (a
    // trigger of the map's, the menu's SAVE) and the one made on coming
    // into the map. A save made a moment before certain death would
    // otherwise be a loop with no way out: so dying soon after going back,
    // three times running, goes back to the coming-in instead -- and if
    // that is no better either, to the map's own start.

    inline constexpr int kQuickDeathMs = 20000;
    inline constexpr int kBackLatest = 0;
    inline constexpr int kBackArrival = 1;
    inline constexpr int kBackStart = 2;
    struct DeathLoop
    {
        int quick = 0; ///< deaths on end, each soon after going back
    };
    /// A death, `sinceBackMs` after the game last went back (or began).
    inline int deathChoice(DeathLoop &loop, int sinceBackMs)
    {
        loop.quick = sinceBackMs < kQuickDeathMs ? loop.quick + 1 : 0;
        if (loop.quick >= 6)
        {
            loop.quick = 0;
            return kBackStart;
        }
        return loop.quick >= 3 ? kBackArrival : kBackLatest;
    }

    inline std::vector<std::uint8_t> writeSave(const SaveData &data, const SaveHeading &heading = SaveHeading())
    {
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        yas::mem_ostream os;
        yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
        oa &data;
        Arsenal arsenal = data.arsenal;
        oa &arsenal;
        std::int16_t antidotes = data.arsenal.antidotes, cards = data.arsenal.securityCards;
        oa &antidotes &cards;
        const auto buf = os.get_intrusive_buffer();

        SaveHeader header{};
        std::memcpy(header.magic, kSaveMagic, sizeof(header.magic));
        header.version = kSaveVersion;
        header.levelVersion = kLevelVersion;
        header.minReader = kSaveMinReader;
        header.payloadBytes = static_cast<std::uint32_t>(buf.size);
        header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data),
                             static_cast<std::uint32_t>(buf.size));
        header.headingCrc = crc32(reinterpret_cast<const std::uint8_t *>(&heading), sizeof(heading));
        std::vector<std::uint8_t> out(kSaveHeadBytes + buf.size);
        std::memcpy(out.data(), &header, sizeof(header));
        std::memcpy(out.data() + sizeof(header), &heading, sizeof(heading));
        std::memcpy(out.data() + kSaveHeadBytes, buf.data, buf.size);
        return out;
    }

    /// writeSave's file in two parts, for a save whose state has a part of
    /// `middleBytes` more at `middleAt` that is not in `data.state` but
    /// written to the card as it is made (the map's state: SaveWriter's
    /// beginSplit), so that the save never has it in memory whole. `front`
    /// is the file up to that part -- its header, the heading, the payload
    /// up to there, with the state's length counting the part -- and `back`
    /// the payload after it. saveSplitSeal puts the payload's CRC in the
    /// header once the part has been through (saveSplitCrcStart). Together:
    /// the bytes writeSave makes of the whole.
    struct SaveSplit
    {
        std::vector<std::uint8_t> front, back;
        std::uint32_t middleBytes = 0;
    };

    inline SaveSplit writeSaveSplit(const SaveData &data, const SaveHeading &heading, std::size_t middleAt,
                                    std::uint32_t middleBytes)
    {
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        SaveSplit split;
        split.middleBytes = middleBytes;
        if (middleAt > data.state.size())
            return split;
        // The payload as writeSave makes it, of what `data` holds: the state
        // is the last of SaveData's own (serialize), its length an 8-byte
        // word in front of it; what comes after it is written apart.
        std::size_t stateEnd = 0;
        yas::mem_ostream os;
        {
            yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
            oa &data;
            stateEnd = os.get_intrusive_buffer().size;
            Arsenal arsenal = data.arsenal;
            oa &arsenal;
            std::int16_t antidotes = data.arsenal.antidotes, cards = data.arsenal.securityCards;
            oa &antidotes &cards;
        }
        const auto buf = os.get_intrusive_buffer();
        const auto *p = reinterpret_cast<const std::uint8_t *>(buf.data);
        const std::size_t n = data.state.size();
        if (stateEnd < n + 8)
            return split;
        const std::size_t lengthAt = stateEnd - n - 8;
        std::uint64_t said = 0;
        std::memcpy(&said, p + lengthAt, 8);
        if (said != n)
            return split; // (not as written above: none)
        const std::uint64_t whole = n + middleBytes;
        split.front.resize(kSaveHeadBytes + lengthAt + 8 + middleAt);
        std::uint8_t *f = split.front.data() + kSaveHeadBytes;
        std::memcpy(f, p, lengthAt);
        std::memcpy(f + lengthAt, &whole, 8);
        std::memcpy(f + lengthAt + 8, data.state.data(), middleAt);
        split.back.assign(p + lengthAt + 8 + middleAt, p + buf.size);

        SaveHeader header{};
        std::memcpy(header.magic, kSaveMagic, sizeof(header.magic));
        header.version = kSaveVersion;
        header.levelVersion = kLevelVersion;
        header.minReader = kSaveMinReader;
        header.payloadBytes =
            static_cast<std::uint32_t>(split.front.size() - kSaveHeadBytes + middleBytes + split.back.size());
        header.headingCrc = crc32(reinterpret_cast<const std::uint8_t *>(&heading), sizeof(heading));
        std::memcpy(split.front.data(), &header, sizeof(header));
        std::memcpy(split.front.data() + sizeof(header), &heading, sizeof(heading));
        return split;
    }

    /// The CRC the part goes on from: of the payload in `front`.
    inline std::uint32_t saveSplitCrcStart(const SaveSplit &split)
    {
        return split.front.size() < kSaveHeadBytes
                   ? kCrcStart
                   : crc32Update(kCrcStart, split.front.data() + kSaveHeadBytes, split.front.size() - kSaveHeadBytes);
    }

    /// The payload's CRC in the header: `afterMiddle`, the CRC (not
    /// finished) that saveSplitCrcStart's became with the part's bytes.
    inline void saveSplitSeal(SaveSplit &split, std::uint32_t afterMiddle)
    {
        SaveHeader header;
        std::memcpy(&header, split.front.data(), sizeof(header));
        header.crc32 = crc32Finish(crc32Update(afterMiddle, split.back.data(), split.back.size()));
        std::memcpy(split.front.data(), &header, sizeof(header));
    }

    /// What the first bytes of a file say it is (kSave*), and its heading
    /// if it has a whole one. `size` is how many bytes of it were read: at
    /// least kSaveHeadBytes of a save.
    inline int readSaveHeading(const std::uint8_t *bytes, std::size_t size, SaveHeading &out)
    {
        SaveHeader header;
        if (size < sizeof(header))
            return kSaveNone;
        std::memcpy(&header, bytes, sizeof(header));
        if (std::memcmp(header.magic, kSaveMagic, sizeof(header.magic)) != 0)
            return kSaveNone;
        if (!saveVersionOk(header.version, header.minReader, header.levelVersion))
            return kSaveOther;
        if (size < kSaveHeadBytes || header.payloadBytes > kSaveMaxBytes ||
            crc32(bytes + sizeof(header), sizeof(SaveHeading)) != header.headingCrc)
            return kSaveBad;
        std::memcpy(&out, bytes + sizeof(header), sizeof(out));
        // (They are shown: they end where they end.)
        out.map[sizeof(out.map) - 1] = 0;
        out.chapter[sizeof(out.chapter) - 1] = 0;
        return kSaveGood;
    }

    /// Whether these bytes are a whole save of this version: every check
    /// that can be made without reading the archive.
    inline bool saveWhole(const std::uint8_t *bytes, std::size_t size)
    {
        SaveHeading heading;
        SaveHeader header;
        if (readSaveHeading(bytes, size, heading) != kSaveGood)
            return false;
        std::memcpy(&header, bytes, sizeof(header));
        return header.payloadBytes == size - kSaveHeadBytes &&
               crc32(bytes + kSaveHeadBytes, header.payloadBytes) == header.crc32;
    }

    /// False for anything that is not a save of this version, damaged or not;
    /// `out` is then untouched.
    inline bool readSave(const std::uint8_t *bytes, std::size_t size, SaveData &out, SaveHeading *heading = nullptr)
    {
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        if (size > kSaveMaxBytes + kSaveHeadBytes || !saveWhole(bytes, size))
            return false;
        const std::uint32_t payload = static_cast<std::uint32_t>(size - kSaveHeadBytes);

        SaveData read;
        yas::mem_istream is(bytes + kSaveHeadBytes, payload);
        yas::binary_iarchive<yas::mem_istream, flags> ia(is);
        ia &read;
        // (The Arsenal, if the save is of a version that has one; and what
        // came after it.)
        if (!is.empty())
            ia &read.arsenal;
        if (!is.empty())
            ia &read.arsenal.antidotes &read.arsenal.securityCards;
        SaveHeader header;
        std::memcpy(&header, bytes, sizeof(header));
        read.version = header.version;

        // What the archive says is still only what a file says: the map's
        // name becomes a path, and the lists are walked.
        if (read.map.empty() || read.map.size() > 31 ||
            read.globalHash.size() != read.globalState.size() || read.globalHash.size() > 64 ||
            read.maps.size() > 256)
            return false;
        for (const char c : read.map)
        {
            const bool plain = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            if (!plain)
                return false;
        }
        if (heading != nullptr)
            readSaveHeading(bytes, size, *heading);
        out = std::move(read);
        return true;
    }
} // namespace hldata
