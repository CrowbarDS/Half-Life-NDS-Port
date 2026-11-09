#pragma once

// A map sent from the host's card to a client's (HlAirBank.hpp's
// FetchFrom::MapCard): for a client without the host's map, or with another
// version of it. What is sent is the map's name (mapCardHead), then its
// archive as the card has it; its checksum is what the host says of its map
// (MapSum), and what the client checks its own -- or what it was sent --
// against.
//
// What a client is sent goes in maps/net/<map>-<crc>.npac, never over the
// player's own maps/<map>.npac: it is played from there (Global::setMapFile)
// and kept for next time.
//
// A DSi's alone (the DSi's memory: a DS has none to spare), and not a
// console sent the game, which has no card. A DS says whether it has the map
// by its name, as before.

#include <cstdint>
#include <cstdio>

#include <nds/ndstypes.h>

#ifndef HLDS_CHILD
/// DSi-only code (a DS has none of this), and never folded into a caller
/// that is not: the compiler pays no heed to where a function it inlines
/// was to go.
#define HL_TWL_CODE TWL_CODE __attribute__((noinline))
#endif

namespace hlrt
{
#ifndef HLDS_CHILD
    /// What a map's archive sends as comes to -- its crc32 and length --
    /// worked out a slice a frame.
    class MapSum
    {
    public:
        /// The archive at `path`, sent as `name`: false if it is not there.
        /// `membersOf` (the host's map, by its name's hash): each member's
        /// crc32 too, and the archive's index kept -- what MapIndex sends
        /// (mapshare::indexFor).
        bool begin(const char *path, const char *name, std::uint32_t membersOf = 0);
        /// A slice of it: true once done (`good` then says whether all of it
        /// was read).
        bool step();
        void stop();
        bool busy() const { return file_ != nullptr; }

        std::uint32_t crc = 0, bytes = 0;
        bool good = false;

    private:
        std::FILE *file_ = nullptr;
        std::uint32_t running_ = 0;
        std::uint32_t membersOf_ = 0;
        void members(const std::uint8_t *data, std::uint32_t at, std::uint32_t bytes);
    };

    /// A map's archive put together on this card (in mapshare::partPath):
    /// from the host's index of it (what MapIndex sends), what this card has
    /// already -- the same member, by its path, length and packing, in any
    /// archive of its own -- is copied, and the rest asked of the host, its
    /// pieces (MapCard's) written where they go. Or, `whole`, all of it from
    /// the host. One at a time; its tables are in the DSi's memory.
    class MapBuild
    {
    public:
        enum class Stage : std::uint8_t
        {
            Off,
            Finding,  ///< reading the card's archives' indexes, a few a frame
            Copying,  ///< what was found, into the file, a slice a frame
            Fetching, ///< the rest, as the host sends it
            Failed,   ///< the card would not take it (no room), or the index was no archive
        };
        /// `index`: what MapIndex sent (the name, then the archive's start up
        /// to its members), `indexBytes` long; `sent`: what MapCard sends as
        /// (the name, then the file). False if it is not an archive.
        bool begin(const std::uint8_t *index, std::uint32_t indexBytes, std::uint32_t sent);
        /// All of it from the host.
        bool beginWhole(std::uint32_t sent);
        /// A frame's work, finding or copying.
        Stage step();
        Stage stage() const { return stage_; }
        /// Piece `index` of the host's (`count` bytes): put where it goes, if
        /// it is wanted. False if the card would not take it.
        bool piece(std::uint32_t index, const std::uint8_t *data, int count);
        /// What FetchHave says: the first piece lacking, and of the window
        /// past it, which are here (copied, or come).
        void have(std::uint32_t &base, std::uint64_t &bits);
        bool complete() const { return lacking_ == 0; }
        /// The file closed, to be checked (false: it would not close); stop:
        /// closed, and gone.
        bool close();
        void stop();
        /// Its name, once known (kMapCardHeadBytes): from the index, or (all
        /// of it sent) from its first piece.
        bool name(char *out) const;
        /// How far: bytes to come from the host, come; the step's percent.
        std::uint32_t toCome() const { return toCome_; }
        std::uint32_t come() const { return come_; }
        int percent() const;

    private:
        Stage stage_ = Stage::Off;
        std::FILE *file_ = nullptr, *source_ = nullptr;
        int sourceAt_ = -1;            ///< which of the found archives source_ is
        std::uint32_t sent_ = 0, pieces_ = 0, lacking_ = 0, first_ = 0, pos_ = 0;
        std::uint32_t toCome_ = 0, come_ = 0, copied_ = 0, toCopy_ = 0;
        int member_ = 0;                ///< (copying: the next)
        std::uint32_t memberDone_ = 0;  ///< ... and how much of it
        void *dir_ = nullptr;           ///< (finding: the maps folder, open)
        int archives_ = 0;              ///< (finding: read so far)
        bool common_ = false;           ///< (finding: common.npac read)
        bool own_ = false;              ///< (finding: this card's own of the map's name read)
        std::uint32_t memberCrc_ = 0;   ///< (copying: what the member comes to so far)
        bool want(std::uint32_t piece) const;
        void need(std::uint32_t from, std::uint32_t bytes);
        void readArchive(const char *path);
        bool copySlice();
        bool writeAt(std::uint32_t at, const std::uint8_t *data, std::uint32_t bytes);
    };

    namespace mapshare
    {
        /// Where a map sent to this card is kept, as it is called there.
        void receivedPath(char *out, int room, const char *name, std::uint32_t crc);
        /// Where one being sent is put meanwhile.
        void partPath(char *out, int room);
        /// A map this card was sent, whose name has `hash` and whose sending
        /// came to `crc`: its name, into `name` (kMapCardHeadBytes).
        bool received(std::uint32_t hash, std::uint32_t crc, char *name);
        /// What was just sent, checked: kept as `name`'s, and any other of
        /// `name`'s let go of.
        bool keep(const char *name, std::uint32_t crc);
        /// What maps/<name>.npac of `bytes` was found to come to, this time
        /// the game was started.
        bool knownSum(std::uint32_t hash, std::uint32_t bytes, std::uint32_t &crc);
        void rememberSum(std::uint32_t hash, std::uint32_t bytes, std::uint32_t crc);
        /// The host: whether the members of its map of `hash` are known
        /// (MapSum's membersOf, done); what MapIndex sends of it -- the name,
        /// the archive up to its members, each member's crc32 -- into `out`.
        /// Its length; 0 if it does not fit `room`, or is not known.
        bool indexFor(std::uint32_t hash);
        std::uint32_t indexBlob(std::uint8_t *out, std::uint32_t room, const char *name);
        /// Whether the card has room for `bytes` more (true if it cannot say).
        bool room(std::uint32_t bytes);
        /// How long a file is (0: not there).
        std::uint32_t fileBytes(const char *path);
    } // namespace mapshare
#endif
} // namespace hlrt
