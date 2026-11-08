#pragma once

// What a console with no card is sent (the Download Play child): a GUEST
// STREAM, made by the converter (air/ on the host's card) and read as it
// arrives. Each member goes straight to where it is to stay -- the level's
// block, a model's, a sound clip, video memory -- and nothing of the stream
// is kept: there is no archive in a guest's memory.
//
// Three kinds of stream:
//  - the common one, once, at the start: the letters, the display's pictures,
//    the menu's sounds;
//  - a group's base (the maps of a game between consoles, the Hazard Course,
//    Uplink), once: the models and sounds every map of the group has, which
//    stay from one map to the next;
//  - a map's own: its level, its textures and sprites, the skins of all its
//    models (video memory is laid out again with every map), and the models
//    and sounds the base has not.
//
// A stream is a header, then its members in the order the console loads
// them: the level, textures, sprites, models, skins, sounds. A member is a
// record and its bytes: LZ77 (HlLz77Stream.hpp) or as they are.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HlLz77Stream.hpp"

namespace hldata
{
    inline constexpr char kGuestMagic[4] = {'H', 'L', 'G', 'S'};
    inline constexpr std::uint16_t kGuestVersion = 1;

    enum class GuestKind : std::uint8_t
    {
        Level,   ///< "level.N": a piece of the level, at its place in the block
        Texture, ///< "tex/...": into video memory
        Sprite,  ///< "spr/...": into video memory
        Model,   ///< "mdl/<name>.hmd": the model's block
        Skin,    ///< "mdl/<skin>.htx": into video memory (a held weapon's: kept in RAM)
        Sound,   ///< "snd/<name>.hsn": a clip
        Ui,      ///< "ui/...": the display's pictures and letters
        Count
    };

    inline constexpr std::uint8_t kGuestLz77 = 1; ///< the member's bytes are LZ77
    /// Kept as it came, packed, and undone only when it is opened: a weapon
    /// the map has lying about (its model and skin, "wpn/..."), one of which
    /// is in memory whole at a time -- when it is in hand -- as on a card.
    inline constexpr std::uint8_t kGuestKeepPacked = 2;

    struct GuestHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t levelVersion; ///< the level format, as the manifest's
        std::uint32_t members;
        std::uint32_t rawBytes;     ///< what the members come to, undone
        std::uint32_t streamBytes;  ///< the whole stream, header and all
    };
    static_assert(sizeof(GuestHeader) == 20);

    struct GuestMember
    {
        std::uint8_t kind;  ///< GuestKind
        std::uint8_t flags; ///< kGuest*
        std::uint16_t nameBytes;
        std::uint32_t rawSize;
        std::uint32_t packedSize; ///< what follows the name
    };
    static_assert(sizeof(GuestMember) == 12);

    inline constexpr std::uint16_t kGuestNameMost = 63;

    // What a guest has of a DS's memory, and what of it a map may take: the
    // converter's account of each map (Converter.cpp, packAirGroups), checked
    // against the child itself on 29 Oct 2026 (melonDS, DS mode; the readout's
    // "heap L M S B" row, a map's streams read off the card as the air would
    // bring them).
    //
    // The program: its code and data (child/out/hl-child.nds's __end__,
    // 0x02165B6C; tools/child_size.py says if it has grown past this). Raised
    // with eight players, their profiles and sprays (0x0216E19C, 30 Oct 2026):
    // the map nearest the limit, doublecross, was 128 KB under it; and again
    // with the host's and the lobby's pages (0x0217154C): about 96 KB under.
    // Lowered when the child stopped carrying the saves (0x02168D24, 30 Oct
    // 2026: phase 6's second round). Raised 2 KB for the long jump, pulled
    // and floating boxes and the tanks' sprites (3 Nov 2026): subtransit,
    // the map nearest the limit, is then 3 KB under it. Raised 1 KB for
    // wheels turned while USE is held (8 Nov 2026): subtransit was over by
    // then already; crossfire, the nearest that fits, is about 32 KB under.
    inline constexpr std::uint32_t kGuestImageBytes = 0x165B6C + 67 * 1024;
    /// The heap's end: the end of main RAM less what the start-up code keeps.
    inline constexpr std::uint32_t kGuestRamBytes = 0x3F4000;
    /// What the engine holds beside a map's data -- its tables, the world's
    /// drawing lists, the game's state, the weapons' skins kept in memory:
    /// measured 231 KB (frenzy) and 274 KB (datacore); and the common
    /// stream's files kept all along (AirDevice's keptCommon: the title's
    /// background and the logos -- 41 KB -- and the suit for the lobby, 45).
    inline constexpr std::uint32_t kGuestReserveBytes = (288 + 41 + 45) * 1024;
    /// What must still be free once the map is in, for what is made while it
    /// is played.
    inline constexpr std::uint32_t kGuestPlayBytes = 128 * 1024;

    /// A map's peak in a guest's memory: the program, the engine's reserve,
    /// and what the map's data comes to there -- its level, its models and
    /// sounds, and the group's base (models and sounds). Textures are in
    /// video memory and not counted.
    inline constexpr std::uint32_t guestPeak(std::uint32_t mapData)
    {
        return kGuestImageBytes + kGuestReserveBytes + mapData;
    }
    inline constexpr bool guestFits(std::uint32_t mapData)
    {
        return guestPeak(mapData) + kGuestPlayBytes <= kGuestRamBytes;
    }

    /// Where a guest stream's bytes come from: the air, or (a test) a file
    /// in memory. Gives the next bytes there are, waiting for some if it
    /// must; false when there are no more (the stream ended, or was lost).
    struct GuestSupply
    {
        virtual ~GuestSupply() = default;
        virtual bool more(const std::uint8_t *&data, std::size_t &size) = 0;
    };

    /// A guest stream, read in order as it comes: member by member, each
    /// one's bytes undone straight into whatever the reader gives them to,
    /// in pieces of any size. Nothing of the stream is kept but the piece of
    /// it being read and the LZ77 window (HlLz77Stream.hpp).
    class GuestCursor
    {
    public:
        explicit GuestCursor(GuestSupply &supply) : supply_(supply) {}

        /// The stream's header. False if it is not a guest stream of this
        /// version.
        bool start()
        {
            failed_ = !take(&header_, sizeof(header_)) || std::memcmp(header_.magic, kGuestMagic, 4) != 0 ||
                      header_.version != kGuestVersion;
            return !failed_;
        }

        /// On to the next member (what is left of this one passed over).
        /// False at the end, or if the stream is wrong.
        bool next()
        {
            if (failed_ || !skip() || membersRead_ == header_.members)
                return false;
            if (!take(&member_, sizeof(member_)) || member_.kind >= static_cast<std::uint8_t>(GuestKind::Count) ||
                member_.nameBytes == 0 || member_.nameBytes > kGuestNameMost ||
                (!(member_.flags & kGuestLz77) && member_.packedSize != member_.rawSize) ||
                !take(name_, member_.nameBytes))
            {
                failed_ = true;
                return false;
            }
            name_[member_.nameBytes] = '\0';
            ++membersRead_;
            packedLeft_ = member_.packedSize;
            taken_ = 0;
            open_ = true;
            if (member_.flags & kGuestLz77)
                lz_.begin(member_.rawSize);
            return true;
        }

        const GuestMember &member() const { return member_; }
        const char *name() const { return name_; }
        /// Of the member: its bytes undone so far.
        std::uint32_t taken() const { return taken_; }

        /// Up to `want` of the member's bytes, undone, into `out`. What was
        /// given: less than asked only at the member's end, or if the stream
        /// is wrong.
        std::size_t read(std::uint8_t *out, std::size_t want)
        {
            std::size_t got = 0;
            while (open_ && !failed_ && got < want && taken_ < member_.rawSize)
            {
                if (inLeft_ == 0 && !supply_.more(in_, inLeft_))
                {
                    failed_ = true;
                    break;
                }
                std::size_t avail = inLeft_ < packedLeft_ ? inLeft_ : packedLeft_;
                const std::size_t before = avail;
                std::size_t wrote;
                if (member_.flags & kGuestLz77)
                {
                    wrote = lz_.pump(in_, avail, out + got, want - got);
                    if (lz_.failed() || (wrote == 0 && avail == 0 && packedLeft_ == 0))
                        failed_ = true;
                }
                else
                {
                    wrote = want - got < avail ? want - got : avail;
                    std::memcpy(out + got, in_, wrote);
                    in_ += wrote;
                    avail -= wrote;
                }
                const std::size_t used = before - avail;
                inLeft_ -= used;
                packedLeft_ -= static_cast<std::uint32_t>(used);
                got += wrote;
                taken_ += static_cast<std::uint32_t>(wrote);
            }
            return got;
        }

        /// The member's bytes as they came (packed, if they are), all of them,
        /// into `out` (room for packedSize). False if the stream is wrong.
        bool readPacked(std::uint8_t *out)
        {
            if (!open_ || taken_ != 0 || packedLeft_ != member_.packedSize)
                return false;
            const bool got = take(out, member_.packedSize);
            packedLeft_ = 0;
            taken_ = member_.rawSize;
            open_ = false;
            failed_ = failed_ || !got;
            return got;
        }

        /// What is left of the member passed over, unread.
        bool skip()
        {
            if (!open_)
                return !failed_;
            while (packedLeft_ > 0 && !failed_)
            {
                if (inLeft_ == 0 && !supply_.more(in_, inLeft_))
                {
                    failed_ = true;
                    break;
                }
                const std::size_t step = inLeft_ < packedLeft_ ? inLeft_ : packedLeft_;
                in_ += step;
                inLeft_ -= step;
                packedLeft_ -= static_cast<std::uint32_t>(step);
            }
            open_ = false;
            return !failed_;
        }

        bool failed() const { return failed_; }
        /// Every member has been gone past.
        bool ended() const { return !failed_ && membersRead_ == header_.members && !open_; }
        const GuestHeader &header() const { return header_; }

    private:
        bool take(void *to, std::size_t size)
        {
            auto *at = static_cast<std::uint8_t *>(to);
            while (size > 0)
            {
                if (inLeft_ == 0 && !supply_.more(in_, inLeft_))
                    return false;
                const std::size_t step = inLeft_ < size ? inLeft_ : size;
                std::memcpy(at, in_, step);
                at += step;
                in_ += step;
                inLeft_ -= step;
                size -= step;
            }
            return true;
        }

        GuestSupply &supply_;
        GuestHeader header_{};
        GuestMember member_{};
        char name_[kGuestNameMost + 1] = {};
        const std::uint8_t *in_ = nullptr;
        std::size_t inLeft_ = 0;
        std::uint32_t packedLeft_ = 0;
        std::uint32_t taken_ = 0;
        std::uint32_t membersRead_ = 0;
        bool open_ = false;
        bool failed_ = false;
        Lz77Stream lz_;
    };

    /// What a member is, by its name (the converter's and the console's
    /// agree through this).
    inline GuestKind guestKindOf(const char *name)
    {
        auto starts = [&](const char *prefix) { return std::strncmp(name, prefix, std::strlen(prefix)) == 0; };
        const std::size_t length = std::strlen(name);
        if (starts("level."))
            return GuestKind::Level;
        if (starts("tex/"))
            return GuestKind::Texture;
        if (starts("spr/"))
            return GuestKind::Sprite;
        if (starts("mdl/"))
            return length > 4 && std::strcmp(name + length - 4, ".hmd") == 0 ? GuestKind::Model : GuestKind::Skin;
        if (starts("snd/"))
            return GuestKind::Sound;
        return GuestKind::Ui;
    }
} // namespace hldata
