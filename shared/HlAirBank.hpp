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

    /// What is asked for: from where, and what.
    enum class FetchFrom : std::uint8_t
    {
        Map,       ///< air/<the match's map>.bank: a member by its name's hash
        MapFull,   ///< ... .dsi.bank
        People,    ///< air/chr.bank: who a player may be seen as
        PeopleFull,///< air/chr.dsi.bank
        Picture,   ///< the `what`-th map's picture the host has; small
        PictureFull,
        VotePicture, ///< the small picture of the `what`-th map of the vote
    };
    inline constexpr std::uint8_t kNetFrameFetchData = 6;
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
            // Already being sent: it listens in, from where it has got to.
            for (Sending &s : sending)
            {
                if (s.on && s.head.from == from && s.head.what == what)
                {
                    if (!s.wants[who].on)
                    {
                        s.wants[who] = Want();
                        s.wants[who].on = true;
                    }
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
        static bool pick(Sending &s, std::uint32_t &index)
        {
            for (int tries = 0; tries < s.pieces; ++tries)
            {
                const std::uint32_t at = s.cursor;
                s.cursor = static_cast<int>(s.cursor + 1) >= s.pieces ? 0 : s.cursor + 1;
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
            const std::uint32_t index = head.index;
            if (!known || head.id != id || static_cast<int>(index) >= pieces || index < base)
                return;
            const std::uint32_t at = index * kFetchPiece;
            const int expect = packed - at < kFetchPiece ? static_cast<int>(packed - at) : kFetchPiece;
            if (head.count != expect)
                return;
            const std::uint32_t past = index - base;
            if (past > kFetchWindow || (past > 0 && ((have >> (past - 1)) & 1)))
                return;
            std::memcpy(room + at, head.data, static_cast<std::size_t>(expect));
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
