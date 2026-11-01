#pragma once

// A demo sent map by map (SEND DEMO): the host serves its Hazard Course or
// Uplink to consoles in DSi mode that have no data, each at its own pace. Here
// is what can be worked out without a radio or a screen.
//
//  - DemoArena, a receiver's: the streams it holds, whole or arriving, packed,
//    in places of one size. The map played; the one it will go to next; the
//    one it came from, for going back and for dying in. (Nothing it has put
//    into video memory or a block can be read twice, and a map is read again
//    every time the player dies in it.)
//  - netDemoPrefetch, a receiver's: which map to ask for while it plays.
//  - DemoServer, the host's: who wants what, and which stream's piece to send
//    next. As many streams at once as a host can keep files open for; what is
//    wanted now before what is wanted for later.
//
// std-only: the host tests run it, over a radio that loses frames.

#include <cstdint>

#include "HlNetAir.hpp"

namespace hldata
{
    /// Why a console cannot be sent a demo; null if it can. (A DS has no room
    /// for a map's stream beside the map, so it is told before it tries.)
    inline const char *netDemoRefusal(bool dsiMode)
    {
        return dsiMode ? nullptr : "Needs a DSi or 3DS";
    }

    /// The demos (HlManifest.hpp's AirGroup): their maps, as the game goes
    /// through them, the first where it begins.
    inline constexpr int kDemoHazard = 2, kDemoUplink = 3, kDemoMapsMost = 7;
    inline constexpr const char *const kDemoHazardMaps[kDemoMapsMost] = {"t0a0", "t0a0a", "t0a0b", "t0a0b1", "t0a0b2", "t0a0c", "t0a0d"};
    inline constexpr const char *const kDemoUplinkMaps[kDemoMapsMost] = {"hldemo1", "hldemo2", "hldemo3", nullptr, nullptr, nullptr, nullptr};
    inline const char *const *netDemoMaps(int group)
    {
        return group == kDemoUplink ? kDemoUplinkMaps : kDemoHazardMaps;
    }
    /// What a beacon's `mode` says of a demo.
    inline constexpr std::uint8_t kNetModeDemoBase = 2;
    inline const char *netDemoTitle(int group) { return group == kDemoUplink ? "UPLINK" : "HAZARD COURSE"; }

    // ---------------------------------------------------------------- the arena

    struct DemoArena
    {
        static constexpr int kPlaces = 3;
        enum class Role : std::uint8_t
        {
            Free,
            Spare,    ///< held, and wanted for nothing just now
            Previous, ///< the map come from
            Next,     ///< the map it is thought it will go to
            Current,  ///< the map played: never given up
        };
        struct Place
        {
            Role role = Role::Free;
            bool whole = false;
            std::uint32_t map = 0;    ///< nameHash
            std::uint32_t length = 0;
        };
        Place places[kPlaces];
        int count = 0;                ///< how many it has room for
        std::uint32_t placeBytes = 0;
        std::uint32_t firstByte = 0;  ///< where the places begin (a folder before them)

        /// `bytes` of memory, `kept` of it for something else, for streams of
        /// at most `largest` bytes. False if not even one fits.
        bool plan(std::uint32_t bytes, std::uint32_t kept, std::uint32_t largest)
        {
            *this = DemoArena();
            if (largest == 0 || bytes <= kept)
                return false;
            const std::uint32_t fits = (bytes - kept) / largest;
            count = fits > kPlaces ? kPlaces : static_cast<int>(fits);
            placeBytes = largest;
            firstByte = kept;
            return count > 0;
        }
        std::uint32_t offsetOf(int place) const { return firstByte + static_cast<std::uint32_t>(place) * placeBytes; }

        int find(std::uint32_t map) const
        {
            for (int n = 0; n < count; ++n)
            {
                if (places[n].role != Role::Free && places[n].map == map)
                    return n;
            }
            return -1;
        }
        bool isWhole(std::uint32_t map) const
        {
            const int n = find(map);
            return n >= 0 && places[n].whole;
        }

        /// Room for `map` in the role `role`: what is free, then what is wanted
        /// least -- and nothing wanted as much as it is, nor the map played.
        /// Where (it may be there already); -1: no room.
        int take(std::uint32_t map, std::uint32_t length, Role role)
        {
            const int held = find(map);
            if (held >= 0)
            {
                if (places[held].role < role)
                    places[held].role = role;
                return held;
            }
            if (length > placeBytes)
                return -1;
            int best = -1;
            for (int n = 0; n < count; ++n)
            {
                const Role has = places[n].role;
                if (has == Role::Current || (has >= role && has != Role::Free))
                    continue;
                if (best < 0 || has < places[best].role)
                    best = n;
            }
            if (best >= 0)
            {
                places[best] = Place();
                places[best].role = role;
                places[best].map = map;
                places[best].length = length;
            }
            return best;
        }

        void arrived(std::uint32_t map)
        {
            const int n = find(map);
            if (n >= 0)
                places[n].whole = true;
        }
        /// Its stream came wrong: to be had again.
        void spoiled(std::uint32_t map)
        {
            const int n = find(map);
            if (n >= 0)
                places[n].whole = false;
        }

        /// The player goes to `map`: the map played becomes the one come from.
        /// Where it now is; -1: there was no room (never, with one to spare
        /// from what was the map played).
        int changeTo(std::uint32_t map, std::uint32_t length)
        {
            for (int n = 0; n < count; ++n)
            {
                if (places[n].role == Role::Previous || places[n].role == Role::Next)
                    places[n].role = places[n].map == map ? places[n].role : Role::Spare;
            }
            for (int n = 0; n < count; ++n)
            {
                if (places[n].role == Role::Current && places[n].map != map)
                    places[n].role = Role::Previous;
            }
            return take(map, length, Role::Current);
        }
    };

    /// Which map to ask for now, while `current` is played: the first of those
    /// its ways out lead to that is not held (its `links`, as the level has
    /// them). 0: none.
    inline std::uint32_t netDemoPrefetch(const DemoArena &arena, const std::uint32_t *links, int count)
    {
        for (int n = 0; n < count; ++n)
        {
            if (links[n] != 0 && arena.find(links[n]) < 0)
                return links[n];
        }
        return 0;
    }

    // --------------------------------------------------------------- the host

    struct DemoServer
    {
        static constexpr int kStreams = 3;
        static constexpr int kReceivers = kNetPlayers;
        struct Receiver
        {
            bool on = false;
            std::uint16_t token = 0; ///< who they are, whichever seat they come back on
            int seat = 0;
            bool live = false;       ///< wanted now, not for later
            std::uint32_t map = 0;   ///< what they want (0: nothing)
            std::uint32_t from = 0;  ///< the first piece they lack
            int stream = -1;         ///< which of ours has them; -1: they wait
            // What the stream is, for when it is begun.
            std::uint32_t length = 0, crc = 0;
            std::uint16_t serve = 0;
            char name[kAirNameBytes] = {};
        };
        struct Stream
        {
            bool on = false;
            std::uint32_t map = 0;
            AirSender air;
        };
        Receiver receivers[kReceivers];
        Stream streams[kStreams];
        int nextStream = 0;

        Receiver *receiverOf(std::uint16_t token)
        {
            for (Receiver &r : receivers)
            {
                if (r.on && r.token == token)
                    return &r;
            }
            return nullptr;
        }

        /// Whether stream `s` has anyone who wants it now.
        bool streamIsLive(int s) const
        {
            for (const Receiver &r : receivers)
            {
                if (r.on && r.stream == s && r.live)
                    return true;
            }
            return false;
        }

        /// Player `seat`, called `token` (it may have been on another seat when
        /// the link was lost), wants `map`'s stream from piece `from`.
        void want(std::uint16_t token, int seat, std::uint32_t map, bool live, std::uint32_t from, const char *name,
                  std::uint32_t length, std::uint32_t crc, std::uint16_t serve)
        {
            Receiver *r = receiverOf(token);
            if (r == nullptr)
            {
                for (Receiver &free : receivers)
                {
                    if (!free.on)
                    {
                        r = &free;
                        break;
                    }
                }
            }
            if (r == nullptr)
                return;
            // (Said again: nothing to begin over. What it has come to know of
            // what the console holds would be lost, and all of it sent again.)
            if (r->on && r->map == map && r->stream >= 0 && r->seat == seat)
            {
                r->live = live;
                return;
            }
            release(*r);
            r->on = true;
            r->token = token;
            r->seat = seat;
            r->live = live;
            r->map = map;
            r->from = from;
            r->length = length;
            r->crc = crc;
            r->serve = serve;
            std::strncpy(r->name, name, kAirNameBytes - 1);
            r->name[kAirNameBytes - 1] = 0;
            attach(*r);
        }

        /// Gone for good: nothing kept of them.
        void leave(std::uint16_t token)
        {
            if (Receiver *r = receiverOf(token))
            {
                release(*r);
                *r = Receiver();
            }
        }
        /// Their link is lost: not sent to, but kept for when they come back.
        void lost(std::uint16_t token)
        {
            if (Receiver *r = receiverOf(token))
            {
                release(*r);
                r->map = 0;
            }
        }

        /// A receiver's answer, from seat `who`.
        void heard(int who, const std::uint8_t *bytes, int size)
        {
            for (Stream &s : streams)
            {
                if (s.on)
                    s.air.asked(who, bytes, size);
            }
        }

        /// Once a frame: who has all of theirs is done with; streams nobody
        /// wants any more stop, and those who waited take their places.
        void frame()
        {
            for (int s = 0; s < kStreams; ++s)
            {
                if (!streams[s].on)
                    continue;
                streams[s].air.frame();
                bool wanted = false;
                for (Receiver &r : receivers)
                {
                    if (!r.on || r.stream != s)
                        continue;
                    const AirSender::Want &want = streams[s].air.wants[r.seat];
                    if (!want.on || want.done)
                    {
                        r.stream = -1;
                        r.map = 0;
                    }
                    else
                        wanted = true;
                }
                if (!wanted)
                    streams[s] = Stream();
            }
            // (Those who want it now first.)
            for (int pass = 0; pass < 2; ++pass)
            {
                for (Receiver &r : receivers)
                {
                    if (r.on && r.map != 0 && r.stream < 0 && r.live == (pass == 0))
                        attach(r);
                }
            }
        }

        /// The next piece to send: of what is wanted now, round the streams;
        /// else of what is wanted for later. False: nothing can be sent.
        bool next(int &stream, std::uint32_t &index)
        {
            for (int pass = 0; pass < 2; ++pass)
            {
                for (int n = 0; n < kStreams; ++n)
                {
                    const int s = (nextStream + n) % kStreams;
                    if (streams[s].on && streamIsLive(s) == (pass == 0) && streams[s].air.next(index))
                    {
                        stream = s;
                        nextStream = (s + 1) % kStreams;
                        return true;
                    }
                }
            }
            return false;
        }

        /// How far the receiver has got, for the host's page: 0..100; -1: not
        /// being sent anything.
        int percentOf(std::uint16_t token)
        {
            Receiver *r = receiverOf(token);
            if (r == nullptr || r->stream < 0)
                return -1;
            const AirSender &air = streams[r->stream].air;
            const std::uint32_t all = air.pieces();
            return all == 0 ? 100 : static_cast<int>(static_cast<std::uint64_t>(air.wants[r->seat].base) * 100 / all);
        }

    private:
        void release(Receiver &r)
        {
            if (r.stream >= 0)
            {
                streams[r.stream].air.drop(r.seat);
                r.stream = -1;
            }
        }

        void join(Receiver &r, int s)
        {
            streams[s].air.addFrom(r.seat, r.from);
            r.stream = s;
        }

        void attach(Receiver &r)
        {
            // One being sent already: heard by all, so shared.
            for (int s = 0; s < kStreams; ++s)
            {
                if (streams[s].on && streams[s].map == r.map)
                {
                    join(r, s);
                    return;
                }
            }
            int free = -1;
            for (int s = 0; s < kStreams && free < 0; ++s)
                free = streams[s].on ? -1 : s;
            // None free: what is wanted now takes the place of what is wanted
            // for later. (What has come of that is kept, and it goes on later
            // from where it stopped.)
            for (int s = 0; s < kStreams && free < 0 && r.live; ++s)
            {
                if (!streamIsLive(s))
                {
                    for (Receiver &other : receivers)
                    {
                        if (other.on && other.stream == s)
                        {
                            other.from = streams[s].air.wants[other.seat].base;
                            other.stream = -1;
                        }
                    }
                    free = s;
                }
            }
            if (free < 0)
                return;
            streams[free] = Stream();
            streams[free].on = true;
            streams[free].map = r.map;
            streams[free].air.begin(AirStream::MapFull, r.name, r.length, r.serve, 0);
            streams[free].air.crc = r.crc;
            join(r, free);
        }
    };
} // namespace hldata
