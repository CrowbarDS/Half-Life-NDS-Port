#pragma once

// What a console sent the game asks for when it wants it, over the radio
// (a texture as it comes into view, a weapon taken up, a map's picture, who
// another player chose to be) -- where a stream (HlNetAir.hpp) is one thing
// for everyone, from its start to its end.
//
// A bank is a file of the host's: a table, then its members, each packed on
// its own, so that any one can be taken alone. The table says what each
// comes to and a checksum of what it is sent as, which the console tries it
// against: nothing that comes over the air here is taken on trust.
//
//   The console asks by a Fetch event (of the game's: resent until heard).
//   The host sends it round to whoever wants it, a few pieces a frame, after
//   the game's own frames; all hear them, so two who want one thing share it.
//   The console says how far it is, a few times a second, by FetchHave;
//   FetchHave at the end says it has it all.
//
// std-only, and no file: the host tests it with pieces lost and pieces bad.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HlNet.hpp"

namespace hldata
{
    inline constexpr char kAirBankMagic[4] = {'H', 'L', 'A', 'B'};
    inline constexpr std::uint16_t kAirBankVersion = 1;

    struct AirBankHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t count;
    };
    static_assert(sizeof(AirBankHeader) == 8);
    /// Sorted by `name`.
    struct AirBankEntry
    {
        std::uint32_t name;   ///< nameHash of its path
        std::uint32_t offset; ///< from the file's start
        std::uint32_t packed; ///< as it is sent
        std::uint32_t raw;    ///< undone (== packed: it is not packed)
        std::uint32_t crc;    ///< crc32 of what is sent
    };
    static_assert(sizeof(AirBankEntry) == 20);

    /// A bank in memory (`size` bytes at `bank`, 4-aligned): the member whose
    /// path hashes to `name`, if the table has it and it lies inside. Nothing
    /// in a bank is trusted: one cut short or wrong finds nothing.
    inline bool airBankFind(const std::uint8_t *bank, std::uint32_t size, std::uint32_t name, AirBankEntry &out)
    {
        if (bank == nullptr || size < sizeof(AirBankHeader))
            return false;
        AirBankHeader header;
        std::memcpy(&header, bank, sizeof(header));
        if (std::memcmp(header.magic, kAirBankMagic, 4) != 0 || header.version != kAirBankVersion ||
            sizeof(AirBankHeader) + static_cast<std::uint32_t>(header.count) * sizeof(AirBankEntry) > size)
            return false;
        const std::uint8_t *table = bank + sizeof(AirBankHeader);
        int low = 0, high = static_cast<int>(header.count) - 1;
        while (low <= high)
        {
            const int middle = (low + high) / 2;
            AirBankEntry entry;
            std::memcpy(&entry, table + static_cast<std::size_t>(middle) * sizeof(AirBankEntry), sizeof(entry));
            if (entry.name == name)
            {
                if (entry.offset > size || entry.packed > size - entry.offset || entry.raw < entry.packed)
                    return false;
                out = entry;
                return true;
            }
            if (entry.name < name)
                low = middle + 1;
            else
                high = middle - 1;
        }
        return false;
    }

    /// The tracks a demo's map plays, as its bank has them (kDemoTracksFile):
    /// one byte each, the game's numbers.
    inline constexpr const char *kDemoTracksFile = "music/tracks";
    inline constexpr int kDemoTracksMost = 4;

    /// What is asked for: from where, and what.
    enum class FetchFrom : std::uint8_t
    {
        Map,       ///< air/<the match's map>.bank: a member by its name's hash
        MapFull,   ///< ... .dsi.bank
        People,    ///< air/chr.bank: who a player may be seen as
        PeopleFull,///< air/chr.dsi.bank
        Picture,   ///< the `what`-th map's picture the host has; small
        PictureFull,
        DemoSave,  ///< the file of this console's demo progress (HlDemoSave.hpp)
        VotePicture, ///< the small picture of the `what`-th map of the vote
        MapCard,   ///< a map's archive as a card has it (maps/<map>.npac), by its name's hash: mapCardHead, then the file
        MapIndex,  ///< ... its start: mapCardHead, then the archive up to its members' data (HlNpacIndex.hpp)
    };
    /// What a MapCard sending begins with: the map's name, nul-padded, so a
    /// console that has not got the map learns what it is called.
    inline constexpr int kMapCardHeadBytes = 24;
    inline void mapCardHead(const char *name, std::uint8_t *out)
    {
        std::memset(out, 0, kMapCardHeadBytes);
        for (int i = 0; i < kMapCardHeadBytes - 1 && name[i] != 0; ++i)
            out[i] = static_cast<std::uint8_t>(name[i]);
    }
    /// The name a MapCard head says, if it is one a file may be called on a
    /// card (a..z, 0..9, _ and -; nul-ended): else false.
    inline bool mapCardName(const std::uint8_t *head, char *out)
    {
        int length = 0;
        while (length < kMapCardHeadBytes && head[length] != 0)
        {
            const char c = static_cast<char>(head[length]);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
                return false;
            out[length] = c;
            ++length;
        }
        if (length == 0 || length >= kMapCardHeadBytes)
            return false;
        out[length] = 0;
        return true;
    }
    inline constexpr std::uint8_t kNetFrameFetchData = 6;
    /// A client: of sending `id`, the first piece it lacks and the bits of
    /// those past it (as FetchHave says) -- a frame of its own, in place of
    /// the game's, while a map's archive comes: each says all, so one lost
    /// or late is nothing, and none waits behind the game's events.
    inline constexpr std::uint8_t kNetFrameFetchHave = 9;
    inline constexpr int kFetchPiece = 600;
    /// Pieces a FetchHave can say of past the first it lacks.
    inline constexpr int kFetchWindow = 64;
    inline constexpr int kFetchHeadBytes = 30;
    /// Frames of the host's with no word from one who asked, before it is
    /// let go of. (It asks again, if it still wants it.)
    inline constexpr int kFetchSilentFrames = 300;

    /// A fact of what is being sent, as every piece says it.
    struct FetchHead
    {
        std::uint8_t id = 0;     ///< which sending (FetchHave says)
        FetchFrom from = FetchFrom::Map;
        std::uint32_t what = 0;  ///< as it was asked for
        std::uint32_t name = 0;  ///< what it is (a picture: its map's hash)
        std::uint32_t packed = 0;
        std::uint32_t raw = 0;
        std::uint32_t crc = 0;
        std::uint16_t index = 0;
        int count = 0;
        const std::uint8_t *data = nullptr;
    };
    inline int fetchPieces(std::uint32_t packed) { return static_cast<int>((packed + kFetchPiece - 1) / kFetchPiece); }

    inline int fetchWrite(const FetchHead &head, const std::uint8_t *data, std::uint8_t *bytes, int size = kNetCmdBytes)
    {
        detail::NetOut out{bytes, bytes + size};
        out.u16(kNetMagic);
        out.u8(kNetVersion);
        out.u8(kNetFrameFetchData);
        out.u8(head.id);
        out.u8(static_cast<std::uint8_t>(head.from));
        out.u32(head.what);
        out.u32(head.name);
        out.u32(head.packed);
        out.u32(head.raw);
        out.u32(head.crc);
        out.u16(head.index);
        out.u16(static_cast<std::uint32_t>(head.count));
        out.bytes(data, head.count);
        return out.ok ? static_cast<int>(out.at - bytes) : 0;
    }
    inline bool fetchRead(const std::uint8_t *bytes, int size, FetchHead &head)
    {
        detail::NetIn in{bytes, bytes + size};
        if (in.u16() != kNetMagic || in.u8() != kNetVersion || in.u8() != kNetFrameFetchData)
            return false;
        head.id = static_cast<std::uint8_t>(in.u8());
        head.from = static_cast<FetchFrom>(in.u8());
        head.what = in.u32();
        head.name = in.u32();
        head.packed = in.u32();
        head.raw = in.u32();
        head.crc = in.u32();
        head.index = static_cast<std::uint16_t>(in.u16());
        head.count = static_cast<int>(in.u16());
        if (!in.ok || head.count <= 0 || head.count > kFetchPiece || in.at + head.count > in.end)
            return false;
        head.data = in.at;
        return true;
    }

    inline int fetchHaveWrite(std::uint8_t id, std::uint32_t base, std::uint64_t bits, std::uint8_t *bytes,
                              int size = kNetReplyBytes)
    {
        detail::NetOut out{bytes, bytes + size};
        out.u16(kNetMagic);
        out.u8(kNetVersion);
        out.u8(kNetFrameFetchHave);
        out.u8(id);
        out.u32(base);
        out.u32(static_cast<std::uint32_t>(bits));
        out.u32(static_cast<std::uint32_t>(bits >> 32));
        return out.ok ? static_cast<int>(out.at - bytes) : 0;
    }
    inline bool fetchHaveRead(const std::uint8_t *bytes, int size, std::uint8_t &id, std::uint32_t &base, std::uint64_t &bits)
    {
        detail::NetIn in{bytes, bytes + size};
        if (in.u16() != kNetMagic || in.u8() != kNetVersion || in.u8() != kNetFrameFetchHave)
            return false;
        id = static_cast<std::uint8_t>(in.u8());
        base = in.u32();
        bits = in.u32();
        bits |= static_cast<std::uint64_t>(in.u32()) << 32;
        return in.ok;
    }

    /// The host's side: who wants what. It finds what is asked for, and reads
    /// the pieces this names.
    struct FetchHost
    {
        static constexpr int kSending = 2;   ///< at once
        static constexpr int kAsked = 4;     ///< waiting, each player
        static constexpr int kPlayers = kNetPlayers;
        struct Asked
        {
            bool on = false;
            FetchFrom from = FetchFrom::Map;
            std::uint32_t what = 0;
        };
        struct Want
        {
            bool on = false;
            std::uint32_t base = 0;
            std::uint64_t have = 0;
            int silent = 0;
        };
        struct Sending
        {
            bool on = false;
            FetchHead head;
            int pieces = 0;
            std::uint32_t cursor = 0;
            Want wants[kPlayers];
            std::uint32_t offset = 0; ///< (the caller's: where in its file)
        };
        Sending sending[kSending];
        Asked asked[kPlayers][kAsked];
        int nextPlayer = 1;
        std::uint8_t nextId = 1;

        /// `who` asks. (False: its list is full: it asks again.)
        bool ask(int who, FetchFrom from, std::uint32_t what)
        {
            if (who < 1 || who >= kPlayers)
                return false;
            // Already being sent: it listens in. (From the start: one that
            // asks again has begun again -- from where it had got to, its
            // next FetchHave says, and that is believed. Kept where it was, a
            // console that had begun again would never be sent its start.)
            for (Sending &s : sending)
            {
                if (s.on && s.head.from == from && s.head.what == what)
                {
                    s.wants[who] = Want();
                    s.wants[who].on = true;
                    return true;
                }
            }
            for (Asked &a : asked[who])
            {
                if (a.on && a.from == from && a.what == what)
                    return true;
            }
            for (Asked &a : asked[who])
            {
                if (!a.on)
                {
                    a.on = true;
                    a.from = from;
                    a.what = what;
                    return true;
                }
            }
            return false;
        }
        /// `who` is gone, or wants no more of anything.
        void forget(int who)
        {
            if (who < 1 || who >= kPlayers)
                return;
            for (Asked &a : asked[who])
                a.on = false;
            for (Sending &s : sending)
            {
                s.wants[who] = Want();
                settle(s);
            }
        }
        void stop() { *this = FetchHost(); }

        /// `who`'s word of sending `id`: the first it lacks, what it has past
        /// that. (Its first past the end: it has it all.)
        void have(int who, std::uint8_t id, std::uint32_t base, std::uint64_t bits)
        {
            if (who < 1 || who >= kPlayers)
                return;
            for (Sending &s : sending)
            {
                if (!s.on || s.head.id != id || !s.wants[who].on)
                    continue;
                Want &w = s.wants[who];
                w.silent = 0;
                if (base < w.base)
                    return; // (older news)
                w.base = base;
                w.have = bits;
                if (static_cast<int>(base) >= s.pieces)
                    w = Want();
                settle(s);
            }
        }
        /// `who` found what it was sent wrong: all of it, again.
        void again(int who, std::uint8_t id)
        {
            if (who < 1 || who >= kPlayers)
                return;
            for (Sending &s : sending)
            {
                if (s.on && s.head.id == id && s.wants[who].on)
                {
                    s.wants[who].base = 0;
                    s.wants[who].have = 0;
                    s.wants[who].silent = 0;
                }
            }
        }
        /// Once a frame: who has said nothing for too long is let go of.
        void frame()
        {
            for (Sending &s : sending)
            {
                if (!s.on)
                    continue;
                for (Want &w : s.wants)
                {
                    if (w.on && ++w.silent > kFetchSilentFrames)
                        w = Want();
                }
                settle(s);
            }
        }
        /// What should be begun next, if a place is free, in turn by player:
        /// the caller finds it and calls begin(), or refuse().
        bool nextAsked(int &who, Asked &out)
        {
            bool free = false;
            for (const Sending &s : sending)
                free = free || !s.on;
            if (!free)
                return false;
            for (int n = 0; n < kPlayers; ++n)
            {
                const int p = 1 + (nextPlayer - 1 + n) % (kPlayers - 1);
                for (Asked &a : asked[p])
                {
                    if (!a.on)
                        continue;
                    a.on = false;
                    who = p;
                    out = a;
                    nextPlayer = p + 1;
                    return true;
                }
            }
            return false;
        }
        /// Begin what `who` asked for; others who asked it too are in it.
        void begin(int who, const Asked &asked_, std::uint32_t name, std::uint32_t packed, std::uint32_t raw, std::uint32_t crc,
                   std::uint32_t offset)
        {
            for (Sending &s : sending)
            {
                if (s.on)
                    continue;
                s = Sending();
                s.on = true;
                s.head.id = nextId;
                nextId = static_cast<std::uint8_t>(nextId == 255 ? 1 : nextId + 1);
                s.head.from = asked_.from;
                s.head.what = asked_.what;
                s.head.name = name;
                s.head.packed = packed;
                s.head.raw = raw;
                s.head.crc = crc;
                s.offset = offset;
                s.pieces = fetchPieces(packed);
                s.wants[who].on = true;
                for (int p = 1; p < kPlayers; ++p)
                {
                    for (Asked &a : asked[p])
                    {
                        if (a.on && a.from == asked_.from && a.what == asked_.what)
                        {
                            a.on = false;
                            s.wants[p].on = true;
                        }
                    }
                }
                return;
            }
        }
        /// The next piece anyone lacks, from either sending in turn. False:
        /// nothing to send.
        bool next(Sending *&out, std::uint32_t &index)
        {
            for (int n = 0; n < kSending; ++n)
            {
                Sending &s = sending[(turn_ + n) % kSending];
                if (!s.on || !pick(s, index))
                    continue;
                turn_ = (turn_ + n + 1) % kSending;
                out = &s;
                return true;
            }
            return false;
        }
        bool busy() const
        {
            for (const Sending &s : sending)
            {
                if (s.on)
                    return true;
            }
            return false;
        }

    private:
        int turn_ = 0;
        static bool lacks(const Want &w, std::uint32_t index)
        {
            if (!w.on || index < w.base)
                return false;
            if (index == w.base)
                return true;
            const std::uint32_t past = index - w.base - 1;
            return past < kFetchWindow && !((w.have >> past) & 1);
        }
        /// One someone lacks, going round from the lowest any lacks to the
        /// window past it (not round all of it: what is past the window is
        /// wanted by nobody yet, and before it, had).
        static bool pick(Sending &s, std::uint32_t &index)
        {
            std::uint32_t from = static_cast<std::uint32_t>(s.pieces);
            for (const Want &w : s.wants)
            {
                if (w.on && w.base < from)
                    from = w.base;
            }
            const std::uint32_t all = static_cast<std::uint32_t>(s.pieces);
            if (from >= all)
                return false;
            const std::uint32_t end = from + kFetchWindow + 1 < all ? from + kFetchWindow + 1 : all;
            if (s.cursor < from || s.cursor >= end)
                s.cursor = from;
            for (std::uint32_t tries = 0; tries < end - from; ++tries)
            {
                const std::uint32_t at = s.cursor;
                s.cursor = at + 1 >= end ? from : at + 1;
                for (const Want &w : s.wants)
                {
                    if (lacks(w, at))
                    {
                        index = at;
                        return true;
                    }
                }
            }
            return false;
        }
        static void settle(Sending &s)
        {
            if (!s.on)
                return;
            for (const Want &w : s.wants)
            {
                if (w.on)
                    return;
            }
            s = Sending();
        }
    };

    /// The console's side of one thing it is taking: the pieces as they come,
    /// into `room` (the caller's, of head.packed bytes once it knows it).
    struct FetchTaking
    {
        bool known = false;
        std::uint8_t id = 0;
        FetchFrom from = FetchFrom::Map;
        std::uint32_t what = 0;
        std::uint32_t name = 0, packed = 0, raw = 0, crc = 0;
        std::uint32_t base = 0;
        std::uint64_t have = 0;
        int pieces = 0;

        void want(FetchFrom asked, std::uint32_t thing)
        {
            *this = FetchTaking();
            from = asked;
            what = thing;
        }
        bool isFor(const FetchHead &head) const { return head.from == from && head.what == what; }
        /// A first piece of a sending, or of another one of it (the host let
        /// it go, and began again): from nothing. Its room is `packed` long.
        bool needsRoom(const FetchHead &head) const { return isFor(head) && (!known || head.id != id); }
        void begin(const FetchHead &head)
        {
            known = true;
            id = head.id;
            name = head.name;
            packed = head.packed;
            raw = head.raw;
            crc = head.crc;
            pieces = fetchPieces(packed);
            base = 0;
            have = 0;
        }
        /// A piece, into `room`.
        void piece(const FetchHead &head, std::uint8_t *room)
        {
            std::uint32_t at = 0;
            if (fits(head, at))
            {
                std::memcpy(room + at, head.data, static_cast<std::size_t>(head.count));
                mark(head);
            }
        }
        /// A piece wanted and as long as it should be: where it goes (from
        /// the start of what is sent). The caller puts it there, then marks
        /// it -- if it could (a file that would not take it: not marked, and
        /// it comes again).
        bool fits(const FetchHead &head, std::uint32_t &at) const
        {
            const std::uint32_t index = head.index;
            if (!known || head.id != id || static_cast<int>(index) >= pieces || index < base)
                return false;
            at = index * kFetchPiece;
            const int expect = packed - at < kFetchPiece ? static_cast<int>(packed - at) : kFetchPiece;
            if (head.count != expect)
                return false;
            const std::uint32_t past = index - base;
            return past <= kFetchWindow && (past == 0 || !((have >> (past - 1)) & 1));
        }
        void mark(const FetchHead &head)
        {
            const std::uint32_t past = head.index - base;
            if (past > 0)
            {
                have |= std::uint64_t(1) << (past - 1);
                return;
            }
            ++base;
            while (have & 1)
            {
                have >>= 1;
                ++base;
            }
            have >>= 1;
        }
        bool whole() const { return known && static_cast<int>(base) >= pieces; }
    };
} // namespace hldata
