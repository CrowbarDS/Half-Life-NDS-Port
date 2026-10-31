#pragma once

// What a host sends the consoles it gave the game to by Download Play
// (HLDS_CHILD): first, in the program's parameter, where to find it; then,
// over the game's own link, the guest streams (HlGuest.hpp) they play from --
// the common one when they come, at a match's start its map's group's base
// (if they have not got it) and the map's own.
//
// One stream at a time goes to all who want it at once: the host's frames
// are heard by every client. Each guest says, in its answer to every frame,
// which pieces it has: the first it lacks, which of the next 64 have come,
// and how far it has room for (it hands them on in order, as its loaders
// read them, and keeps no more than 64). The host sends what any of them
// lacks, lowest first, going round what lies between the lowest first-lacked
// and 64 past it; so a piece lost by one is sent again, and heard again by
// all. A guest that says nothing for a while is left out, and the others go
// on without it.
//
// std-only: the host tests run it, over a radio that loses frames.

#include <cstdint>
#include <cstring>

#include "HlNet.hpp"

namespace hldata
{
    // ----------------------------------------------------- Download Play

    /// The 32 bytes a program sent by Download Play is handed (the loader
    /// leaves them at 0x027FFBE0): whose game it is and where to find it.
    struct NetDlParam
    {
        std::uint8_t version = kNetVersion;
        std::uint8_t channel = 0;
        std::uint8_t host[6] = {}; ///< the host's address
    };
    inline constexpr int kNetDlParamBytes = 32;
    inline constexpr std::uint32_t kNetDlMagic = 0x50444C48; // "HLDP"

    inline void netDlWrite(const NetDlParam &param, std::uint8_t *bytes)
    {
        std::memset(bytes, 0, kNetDlParamBytes);
        detail::NetOut out{bytes, bytes + kNetDlParamBytes};
        out.u32(kNetDlMagic);
        out.u8(param.version);
        out.u8(param.channel);
        out.bytes(param.host, 6);
    }
    /// False if they are not ours (a console started another way: nothing
    /// there, or what was there before), or of another version.
    inline bool netDlRead(const std::uint8_t *bytes, NetDlParam &param)
    {
        detail::NetIn in{bytes, bytes + kNetDlParamBytes};
        if (in.u32() != kNetDlMagic)
            return false;
        param.version = static_cast<std::uint8_t>(in.u8());
        param.channel = static_cast<std::uint8_t>(in.u8());
        in.bytes(param.host, 6);
        return in.ok && param.version == kNetVersion && param.channel >= 1 && param.channel <= 14;
    }

    // ------------------------------------------------------------ streams

    inline constexpr std::uint8_t kNetFrameAirAsk = 4;  ///< a guest: what it has of the stream
    inline constexpr std::uint8_t kNetFrameAirData = 5; ///< the host: a piece of it, to all

    enum class AirStream : std::uint8_t
    {
        None = 0,
        Common, ///< what the game needs before any map: its letters, its pictures
        Base,   ///< a group's (HlManifest.hpp's AirGroup), by its name: "match"
        Map,    ///< a map's own, by its name
        Preview, ///< the picture of the host's map (HlPreview.hpp), by the map's name: to any console without one
        MapFull, ///< a map's own at full quality (<map>.dsi.hgs), to a console in DSi mode: before the others' base and map
        CharacterFull, ///< ... at its finest, to a console in DSi mode (before the others' Character)
        Character, ///< who a player chose to be (HlCharacters.hpp), by its name: after the map, to those with room
    };
    /// A stream's name on the wire.
    inline constexpr int kAirNameBytes = 24;
    /// The pieces a stream goes in (the last, what is left), and how many a
    /// guest keeps account of past the first it lacks.
    inline constexpr int kAirPiece = 600;
    inline constexpr int kAirWindow = 64;
    /// Frames a guest that wants a stream may say nothing before it is left
    /// out: two minutes of the host's (a guest loading a map from it says
    /// nothing while a loader is busy with what it has, and a guest's game
    /// is a long time being set up). One really gone the radio says is gone
    /// sooner; and the host's player can give it up.
    inline constexpr std::uint32_t kAirSilentFrames = 7200;

    /// What a piece's frame says, besides its bytes.
    struct AirHead
    {
        AirStream stream = AirStream::None;
        std::uint16_t serve = 0;  ///< which sending of it (a guest does not mix two)
        std::uint32_t index = 0;
        std::uint32_t length = 0; ///< the stream's, in bytes
        std::uint8_t skill = 0;   ///< a map's: how hard the match will be (kSkill*)
        std::uint32_t crc = 0;    ///< of all of it (0: not said; a demo's map says)
        char name[kAirNameBytes] = {};
        const std::uint8_t *data = nullptr;
        int count = 0;
    };

    inline std::uint32_t airPieces(std::uint32_t length)
    {
        return (length + kAirPiece - 1) / kAirPiece;
    }

    /// A host's frame, if it is a piece of a stream.
    inline bool airRead(const std::uint8_t *bytes, int size, AirHead &head)
    {
        detail::NetIn in{bytes, bytes + size};
        if (in.u16() != kNetMagic || in.u8() != kNetVersion || in.u8() != kNetFrameAirData)
            return false;
        head.stream = static_cast<AirStream>(in.u8());
        head.serve = static_cast<std::uint16_t>(in.u16());
        head.index = in.u32();
        head.length = in.u32();
        head.skill = static_cast<std::uint8_t>(in.u8());
        head.crc = in.u32();
        in.bytes(head.name, kAirNameBytes);
        head.name[kAirNameBytes - 1] = 0;
        head.count = static_cast<int>(in.u16());
        head.data = in.at;
        return in.ok && head.count >= 0 && head.count <= kAirPiece && in.end - in.at >= head.count;
    }

    /// The host's side of one stream being sent.
    struct AirSender
    {
        AirStream stream = AirStream::None;
        char name[kAirNameBytes] = {};
        std::uint16_t serve = 0;
        std::uint32_t length = 0;
        std::uint8_t skill = 0;
        std::uint32_t crc = 0; ///< (whoever has worked it out: 0, not said)
        struct Want
        {
            bool on = false;       ///< waited for
            bool done = false;     ///< has it all
            std::uint32_t base = 0; ///< the first piece it lacks
            std::uint64_t have = 0; ///< bit n: piece base + 1 + n has come
            std::uint32_t room = 0; ///< the first it has no room for yet
            std::uint32_t silent = 0;
        } wants[kNetPlayers];
        std::uint32_t cursor = 0; ///< the next piece to look at

        bool sending() const { return stream != AirStream::None; }
        std::uint32_t pieces() const { return airPieces(length); }

        /// Begin sending `what`, `bytes` long, to the clients of `mask` (bit
        /// n: client n), who are each waited for until they have it.
        void begin(AirStream what, const char *named, std::uint32_t bytes, std::uint16_t number, unsigned mask,
                   std::uint8_t hard = 0)
        {
            *this = AirSender();
            stream = what;
            std::strncpy(name, named, kAirNameBytes - 1);
            length = bytes;
            serve = number;
            skill = hard;
            for (int c = 1; c < kNetPlayers; ++c)
                add(c, mask);
        }
        /// Client `c` (if its bit is in `mask`) wants it too, from the start.
        void add(int c, unsigned mask = ~0u)
        {
            if (c < 1 || c >= kNetPlayers || !((mask >> c) & 1) || wants[c].on)
                return;
            wants[c] = Want();
            wants[c].on = true;
            wants[c].room = kAirWindow;
        }
        /// ... from piece `from`: what comes before it, it has.
        void addFrom(int c, std::uint32_t from)
        {
            add(c);
            if (c >= 1 && c < kNetPlayers && wants[c].on)
            {
                wants[c].base = from;
                wants[c].room = from + kAirWindow;
            }
        }
        /// Client `c` is gone, or is left out.
        void drop(int c)
        {
            if (c >= 1 && c < kNetPlayers)
                wants[c] = Want();
        }
        void stop() { *this = AirSender(); }

        /// A guest's answer, from client `who`. True if it is about this
        /// stream (then in here).
        bool asked(int who, const std::uint8_t *bytes, int size)
        {
            detail::NetIn in{bytes, bytes + size};
            if (in.u16() != kNetMagic || in.u8() != kNetVersion || in.u8() != kNetFrameAirAsk)
                return false;
            const AirStream what = static_cast<AirStream>(in.u8());
            const std::uint16_t number = static_cast<std::uint16_t>(in.u16());
            const std::uint32_t first = in.u32();
            const std::uint64_t low = in.u32();
            const std::uint64_t mask = low | (static_cast<std::uint64_t>(in.u32()) << 32);
            const std::uint32_t upTo = in.u32();
            if (!in.ok || who < 1 || who >= kNetPlayers || !sending() || what != stream || number != serve ||
                !wants[who].on)
                return false;
            Want &want = wants[who];
            // (An answer from before the last: older news -- and no sign of
            // life: one that keeps saying it, having lost its place, is as
            // good as silent, and would be waited for for ever.)
            if (first < want.base)
                return true;
            want.silent = 0;
            want.base = first;
            want.have = mask;
            want.room = upTo;
            want.done = first >= pieces();
            return true;
        }

        /// Once a frame of the host's: who has said nothing for too long is
        /// left out. Returns the bits of those left out now.
        unsigned frame()
        {
            unsigned out = 0;
            for (int c = 1; c < kNetPlayers; ++c)
            {
                if (wants[c].on && !wants[c].done && ++wants[c].silent > kAirSilentFrames)
                {
                    drop(c);
                    out |= 1u << c;
                }
            }
            return out;
        }

        /// Every guest waited for has it all (or there are none left).
        bool done() const
        {
            for (int c = 1; c < kNetPlayers; ++c)
            {
                if (wants[c].on && !wants[c].done)
                    return false;
            }
            return true;
        }
        /// How many are waited for, and how far the slowest is (0..100).
        int waiting() const
        {
            int n = 0;
            for (int c = 1; c < kNetPlayers; ++c)
                n += wants[c].on && !wants[c].done ? 1 : 0;
            return n;
        }
        int percent() const
        {
            const std::uint32_t all = pieces();
            return all == 0 ? 100 : static_cast<int>((static_cast<std::uint64_t>(low()) * 100) / all);
        }
        /// The first piece any guest waited for lacks.
        std::uint32_t low() const
        {
            std::uint32_t lowest = pieces();
            for (int c = 1; c < kNetPlayers; ++c)
            {
                if (wants[c].on && !wants[c].done && wants[c].base < lowest)
                    lowest = wants[c].base;
            }
            return lowest;
        }

        /// Whether guest `c` lacks piece `index`, and has room for it.
        bool lacks(int c, std::uint32_t index) const
        {
            const Want &want = wants[c];
            if (!want.on || want.done || index < want.base || index >= want.room || index > want.base + kAirWindow)
                return false;
            return index == want.base || !((want.have >> (index - want.base - 1)) & 1);
        }

        /// Which piece to send now: one that a guest lacks, lowest first,
        /// going round from the lowest any lacks to 64 past it. False if
        /// nobody lacks any it has room for.
        bool next(std::uint32_t &index)
        {
            const std::uint32_t all = pieces();
            const std::uint32_t from = low();
            if (from >= all)
                return false;
            const std::uint32_t end = from + kAirWindow + 1 < all ? from + kAirWindow + 1 : all;
            if (cursor < from || cursor >= end)
                cursor = from;
            for (std::uint32_t tries = 0; tries < end - from; ++tries)
            {
                const std::uint32_t at = cursor;
                cursor = cursor + 1 >= end ? from : cursor + 1;
                for (int c = 1; c < kNetPlayers; ++c)
                {
                    if (lacks(c, at))
                    {
                        index = at;
                        return true;
                    }
                }
            }
            return false;
        }

        /// The frame of piece `index`: its `count` bytes `data`. Returns
        /// the frame's length.
        int piece(std::uint32_t index, const std::uint8_t *data, int count, std::uint8_t *bytes,
                  int size = kNetCmdBytes) const
        {
            detail::NetOut out{bytes, bytes + size};
            out.u16(kNetMagic);
            out.u8(kNetVersion);
            out.u8(kNetFrameAirData);
            out.u8(static_cast<std::uint8_t>(stream));
            out.u16(serve);
            out.u32(index);
            out.u32(length);
            out.u8(skill);
            out.u32(crc);
            out.bytes(name, kAirNameBytes);
            out.u16(static_cast<std::uint16_t>(count));
            if (count > 0)
                out.bytes(data, count);
            return out.ok ? static_cast<int>(out.at - bytes) : 0;
        }
        /// How many bytes piece `index` has.
        int pieceBytes(std::uint32_t index) const
        {
            const std::uint32_t at = index * kAirPiece;
            return at >= length ? 0 : static_cast<int>(length - at < kAirPiece ? length - at : kAirPiece);
        }
    };

    /// A guest's side of one stream: the pieces as they come, in any order,
    /// handed on in order. `ring` holds kAirWindow pieces (the caller's).
    struct AirReceiver
    {
        AirStream stream = AirStream::None;
        char name[kAirNameBytes] = {}; ///< "" until the host has said (then the one it says)
        std::uint16_t serve = 0;
        bool known = false;        ///< the host has been heard sending it
        std::uint32_t length = 0;
        std::uint8_t skill = 0;
        std::uint32_t crc = 0;
        /// Into memory as long as the stream, each piece at its own place, not
        /// a ring handed on as it is read (a demo's map: nothing to hand on).
        bool direct = false;
        std::uint32_t base = 0;    ///< the first piece not come
        std::uint64_t have = 0;    ///< bit n: piece base + 1 + n has come
        std::uint32_t given = 0;   ///< the pieces handed on
        bool holding = false;      ///< the last handed on is still being read (its slot not free)
        std::uint8_t *ring = nullptr;

        /// Want stream `what` (named `named`, or whichever of it the host
        /// sends: "").
        void want(AirStream what, const char *named, std::uint8_t *room)
        {
            *this = AirReceiver();
            stream = what;
            std::strncpy(name, named, kAirNameBytes - 1);
            ring = room;
        }
        /// ... into `room`, as long as the stream, what comes before piece
        /// `from` being there already.
        void wantDirect(AirStream what, const char *named, std::uint8_t *room, std::uint32_t from = 0)
        {
            want(what, named, room);
            direct = true;
            base = from;
        }
        std::uint32_t pieces() const { return airPieces(length); }
        bool done() const { return known && (direct ? base >= pieces() : given >= pieces() && !holding); }
        int percent() const
        {
            return !known || pieces() == 0 ? 0 : static_cast<int>((static_cast<std::uint64_t>(given) * 100) / pieces());
        }
        /// The first piece there is no room for: a window past the next to
        /// be handed on (or being read), whose slot it would take.
        std::uint32_t room() const { return direct ? base + kAirWindow : given + kAirWindow; }

        /// A host's frame (already read: airRead). True if it was of this
        /// stream.
        bool frame(const AirHead &head)
        {
            if (head.stream != stream || (name[0] != 0 && std::strcmp(head.name, name) != 0))
                return false;
            if (!known)
            {
                // (The first heard of it: this sending is the one followed.)
                known = true;
                serve = head.serve;
                length = head.length;
                skill = head.skill;
                crc = head.crc;
                std::strncpy(name, head.name, kAirNameBytes - 1);
            }
            else if (head.serve != serve || head.length != length)
                return false;
            const std::uint32_t index = head.index;
            if (index >= pieces() || index < base || index >= room() || index > base + kAirWindow)
                return true;
            if (head.count != static_cast<int>(length - index * kAirPiece < kAirPiece ? length - index * kAirPiece
                                                                                         : kAirPiece))
                return true;
            if (index > base && ((have >> (index - base - 1)) & 1))
                return true; // (come already)
            std::memcpy(ring + (direct ? index : index % kAirWindow) * kAirPiece, head.data, static_cast<std::size_t>(head.count));
            if (index > base)
            {
                have |= std::uint64_t(1) << (index - base - 1);
                return true;
            }
            // The first lacking has come: on to the next that has not.
            ++base;
            while (have & 1)
            {
                have >>= 1;
                ++base;
            }
            have >>= 1;
            return true;
        }

        /// The answer to send: what has come -- or, before the host has been
        /// heard sending it, that it is wanted (serve 0: the common stream,
        /// which a guest asks for when it comes).
        int ask(std::uint8_t *bytes, int size = kNetReplyBytes) const
        {
            detail::NetOut out{bytes, bytes + size};
            out.u16(kNetMagic);
            out.u8(kNetVersion);
            out.u8(kNetFrameAirAsk);
            out.u8(static_cast<std::uint8_t>(stream));
            out.u16(serve);
            out.u32(base);
            out.u32(static_cast<std::uint32_t>(have));
            out.u32(static_cast<std::uint32_t>(have >> 32));
            out.u32(room());
            return out.ok ? static_cast<int>(out.at - bytes) : 0;
        }

        /// The next piece in order, if it has come: valid until the next
        /// call (its slot is not written over until then).
        bool next(const std::uint8_t *&data, std::size_t &size)
        {
            if (holding)
            {
                holding = false;
                ++given;
            }
            if (!known || given >= base)
                return false;
            const std::uint32_t at = given * kAirPiece;
            data = ring + (given % kAirWindow) * kAirPiece;
            size = length - at < kAirPiece ? length - at : kAirPiece;
            holding = true;
            return true;
        }
    };

    /// Which stream a guest's answer is about, and of which sending (0: it
    /// is asking for it). False if it is not a guest's answer.
    inline bool airAskOf(const std::uint8_t *bytes, int size, AirStream &stream, std::uint16_t &serve)
    {
        detail::NetIn in{bytes, bytes + size};
        if (in.u16() != kNetMagic || in.u8() != kNetVersion || in.u8() != kNetFrameAirAsk)
            return false;
        stream = static_cast<AirStream>(in.u8());
        serve = static_cast<std::uint16_t>(in.u16());
        return in.ok;
    }

    /// The answer of a guest that has a stream already (a group's base it
    /// was sent for another map): all of it, at once.
    inline int airHave(AirStream stream, std::uint16_t serve, std::uint32_t length, std::uint8_t *bytes,
                       int size = kNetReplyBytes)
    {
        detail::NetOut out{bytes, bytes + size};
        out.u16(kNetMagic);
        out.u8(kNetVersion);
        out.u8(kNetFrameAirAsk);
        out.u8(static_cast<std::uint8_t>(stream));
        out.u16(serve);
        out.u32(airPieces(length));
        out.u32(0);
        out.u32(0);
        out.u32(airPieces(length) + kAirWindow);
        return out.ok ? static_cast<int>(out.at - bytes) : 0;
    }
} // namespace hldata
