#pragma once

// The manifest: <card>/_nds/hl_nds/data/manifest.bin
//
// What the converter put on the card -- which maps, made by which version --
// and which map the game starts in. It is the first thing the DS reads, and
// it is how the game tells "nothing has been converted" from "something is
// missing" without probing the card file by file.
//
// This is the one file that is a yas archive rather than a table of records:
// it is a list of strings, which is what yas is for and what a POD table is
// bad at.
//
// yas archives are not self-describing and yas reports a malformed one by
// throwing, which the ARM9 build (-fno-exceptions) cannot survive. So the
// payload is wrapped in a small fixed header -- magic, version, length,
// checksum -- and readManifest() refuses anything that does not match before
// yas sees a byte of it.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "HlYas.hpp"

#include "HlLevel.hpp"

namespace hldata
{
    inline constexpr char kManifestMagic[4] = {'H', 'L', 'M', 'F'};
    inline constexpr std::uint16_t kManifestVersion = 6;

    /// The groups of maps a console with no card can be sent (air/ on the
    /// card): 0 is none. Each has its base, air/<name>.base.npac, which is
    /// sent once and kept from map to map.
    enum class AirGroup : std::uint8_t
    {
        None,
        Match,  ///< the maps of a game between consoles
        Hazard, ///< the Hazard Course
        Uplink,
        Count
    };
    inline constexpr const char *kAirGroupNames[] = {"", "match", "hazard", "uplink"};
    /// The same on both sides, or nothing reads back.
    inline constexpr std::size_t kYasFlags = yas::mem | yas::binary | yas::no_header;

    struct ManifestHeader
    {
        char magic[4];
        std::uint16_t version;
        /// The level format the listed maps were cooked in. A DS build with a
        /// different one says "convert again" once, here, instead of once per
        /// map.
        std::uint16_t levelVersion;
        std::uint32_t payloadBytes;
        std::uint32_t crc32; ///< of the payload
    };
    static_assert(sizeof(ManifestHeader) == 16);

    struct ManifestMap
    {
        std::string name; ///< lower-case, no extension: maps/<name>.npac
        std::uint32_t levelBytes = 0;
        std::uint32_t archiveBytes = 0; ///< the map's whole archive, as it is on the card
        std::uint16_t worstPolygons = 0;
        std::uint16_t worstVertices = 0;
        std::uint16_t textureCount = 0;
        /// How many places it has for a player to come into a game between
        /// consoles; 0 for a map that is not a multiplayer map.
        std::uint8_t spawns = 0;
        /// Its group of what can be sent to a console with no card (AirGroup),
        /// and the size of its own air archive, without the group's base.
        std::uint8_t airGroup = 0;
        std::uint32_t airBytes = 0;
        /// Sent to a console with no card: the most of its memory the map
        /// takes (HlGuest.hpp, guestPeak), and whether that leaves it enough
        /// to play in (guestFits). 0 / false for a map not sent.
        std::uint32_t guestPeakBytes = 0;
        bool guestFits = false;
        /// Whether a DS (not a DSi) has the memory for it, with all it gives
        /// up (HlDsRoom.hpp): a DS offers no game between consoles on a map
        /// it would then not load.
        bool dsFits = true;

        template <class Ar>
        void serialize(Ar &ar)
        {
            ar &name &levelBytes &archiveBytes &worstPolygons &worstVertices &textureCount &spawns &airGroup &airBytes
                &guestPeakBytes &guestFits &dsFits;
        }
    };

    struct Manifest
    {
        std::string converterVersion;
        std::string startMap;
        std::vector<ManifestMap> maps;
        /// The size of each AirGroup's base archive (0: there is none).
        std::vector<std::uint32_t> airBaseBytes;

        template <class Ar>
        void serialize(Ar &ar)
        {
            ar &converterVersion &startMap &maps &airBaseBytes;
        }

        const ManifestMap *find(const std::string &name) const
        {
            for (const ManifestMap &map : maps)
            {
                if (map.name == name)
                    return &map;
            }
            return nullptr;
        }
    };

    inline std::vector<std::uint8_t> writeManifest(const Manifest &manifest)
    {
        yas::mem_ostream os;
        yas::binary_oarchive<yas::mem_ostream, kYasFlags> oa(os);
        oa &manifest;
        const auto buf = os.get_intrusive_buffer();

        ManifestHeader header{};
        std::memcpy(header.magic, kManifestMagic, sizeof(header.magic));
        header.version = kManifestVersion;
        header.levelVersion = kLevelVersion;
        header.payloadBytes = static_cast<std::uint32_t>(buf.size);
        header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data),
                             static_cast<std::uint32_t>(buf.size));
        std::vector<std::uint8_t> out(sizeof(header) + buf.size);
        std::memcpy(out.data(), &header, sizeof(header));
        std::memcpy(out.data() + sizeof(header), buf.data, buf.size);
        return out;
    }

    enum class ManifestError
    {
        Ok,
        NotAManifest, ///< too small, wrong magic, wrong length or bad checksum
        OtherVersion, ///< written by a converter this build does not match
    };

    /// What is wrong with a manifest that readManifest called OtherVersion,
    /// for the screen that says the game cannot start: the two numbers that
    /// decided it. Empty for one that is not.
    inline std::string manifestRefusal(const std::uint8_t *data, std::size_t size)
    {
        ManifestHeader header;
        if (size < sizeof(header))
            return std::string();
        std::memcpy(&header, data, sizeof(header));
        char text[128];
        if (header.version != kManifestVersion)
            std::snprintf(text, sizeof(text), "The card's manifest.bin is format %u; this version needs %u.",
                          static_cast<unsigned>(header.version), static_cast<unsigned>(kManifestVersion));
        else if (header.levelVersion < kLevelMinVersion)
            std::snprintf(text, sizeof(text), "The card's maps are format %u; this version needs %u or newer.",
                          static_cast<unsigned>(header.levelVersion), static_cast<unsigned>(kLevelMinVersion));
        else if (header.levelVersion > kLevelVersion)
            std::snprintf(text, sizeof(text), "The card's maps are format %u; this version reads up to %u.",
                          static_cast<unsigned>(header.levelVersion), static_cast<unsigned>(kLevelVersion));
        else
            return std::string();
        return text;
    }

    /// Every check that can be made without yas is made before yas runs.
    inline ManifestError readManifest(const std::uint8_t *data, std::size_t size, Manifest &out)
    {
        ManifestHeader header;
        if (size < sizeof(header))
            return ManifestError::NotAManifest;
        std::memcpy(&header, data, sizeof(header));
        if (std::memcmp(header.magic, kManifestMagic, sizeof(header.magic)) != 0)
            return ManifestError::NotAManifest;
        if (header.version != kManifestVersion ||
            header.levelVersion < kLevelMinVersion || header.levelVersion > kLevelVersion)
            return ManifestError::OtherVersion;
        if (header.payloadBytes != size - sizeof(header) ||
            crc32(data + sizeof(header), header.payloadBytes) != header.crc32)
            return ManifestError::NotAManifest;

        yas::mem_istream is(data + sizeof(header), header.payloadBytes);
        yas::binary_iarchive<yas::mem_istream, kYasFlags> ia(is);
        ia &out;
        return ManifestError::Ok;
    }
} // namespace hldata
