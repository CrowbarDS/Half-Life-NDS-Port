#include "Relay.hpp"

#include <cstdio>

#include "SipHash.hpp"

using namespace hldata;

namespace hlrelay
{
    std::string Address::text() const
    {
        char line[32];
        std::snprintf(line, sizeof(line), "%u.%u.%u.%u:%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF,
                      ip & 0xFF, port);
        return line;
    }

    void Relay::setSecret(const std::uint8_t *secret)
    {
        std::memcpy(cookieKey_, secret, sizeof(cookieKey_));
        std::memcpy(randomKey_, secret + sizeof(cookieKey_), sizeof(randomKey_));
    }

    std::uint64_t Relay::random()
    {
        std::uint8_t count[8];
        for (int i = 0; i < 8; ++i)
            count[i] = static_cast<std::uint8_t>(randomCount_ >> (8 * i));
        ++randomCount_;
        return sipHash24(randomKey_, count, sizeof(count));
    }

    void Relay::cookie(const Address &at, std::uint32_t nonce, std::uint32_t window, std::uint8_t *out) const
    {
        std::uint8_t what[14];
        relayPut32(what, at.ip);
        what[4] = static_cast<std::uint8_t>(at.port);
        what[5] = static_cast<std::uint8_t>(at.port >> 8);
        relayPut32(what + 6, nonce);
        relayPut32(what + 10, window);
        const std::uint64_t mac = sipHash24(cookieKey_, what, sizeof(what));
        relayPut32(out, window);
        relayPut32(out + 4, static_cast<std::uint32_t>(mac));
        relayPut32(out + 8, static_cast<std::uint32_t>(mac >> 32));
    }

    bool Relay::cookieGood(const Address &at, std::uint32_t nonce, const std::uint8_t *given, std::uint32_t nowMs) const
    {
        const std::uint32_t window = relayGet32(given);
        const std::uint32_t now = nowMs >> kCookieShift;
        if (window != now && window != now - 1)
            return false;
        std::uint8_t made[kRelayCookieBytes];
        cookie(at, nonce, window, made);
        unsigned differ = 0;
        for (int i = 0; i < kRelayCookieBytes; ++i)
            differ |= static_cast<unsigned>(made[i] ^ given[i]);
        return differ == 0;
    }

    void Relay::enter(const Address &from, std::uint32_t nonce, const std::uint8_t *bytes, int size,
                      std::uint32_t nowMs, std::vector<Out> &out)
    {
        const std::uint8_t *given = nullptr, *token = nullptr;
        if (!relayEnterRead(bytes, size, given, token))
        {
            ++drops_.unknown;
            return;
        }
        if (!cookieGood(from, nonce, given, nowMs))
        {
            ++drops_.badCookie;
            return;
        }
        std::uint8_t welcome[kRelayWelcomeBytes];
        Peer *me = peer(from);
        // (Said again, the Welcome lost on the way: welcomed again, the same.)
        if (me != nullptr && me->nonce == nonce)
        {
            me->heardMs = nowMs;
            say(out, *me, welcome, relayWelcomeWrite(welcome, nonce, me->session, me->token));
            return;
        }
        // (Another, from where one was: a console started again there, or
        // another behind the same port. It has shown that it is at that
        // address: the one before is gone.)
        if (me != nullptr)
            drop(me->id, out, 0);
        if (peers_.size() >= kMostPeers)
        {
            ++drops_.tooMany;
            return;
        }
        if (counted(from.ip).peers >= limits.peersPerIp)
        {
            over(from, drops_.capPeers, "consoles at once", nowMs);
            return;
        }
        Peer fresh;
        while (nextPeer_ == 0 || peers_.count(nextPeer_) != 0)
            ++nextPeer_;
        fresh.id = nextPeer_++;
        fresh.at = from;
        fresh.nonce = nonce;
        do
            fresh.session = static_cast<std::uint32_t>(random());
        while (fresh.session == 0);
        const std::uint64_t t = random();
        for (int i = 0; i < kRelayTokenBytes; ++i)
            fresh.token[i] = static_cast<std::uint8_t>(t >> (8 * i));
        fresh.heardMs = nowMs;
        me = &(peers_[fresh.id] = fresh);
        at_[from.key()] = fresh.id;
        ++counted(from.ip).peers;
        note(named(*me) + " came");
        say(out, *me, welcome, relayWelcomeWrite(welcome, nonce, me->session, me->token));
    }

    Relay::IpSlot &Relay::ipSlot(std::uint32_t ip)
    {
        std::uint8_t bytes[4];
        relayPut32(bytes, ip);
        IpSlot &slot = ipSlots_[sipHash24(cookieKey_, bytes, sizeof(bytes)) & (kIpSlots - 1)];
        if (!slot.used || slot.ip != ip)
        {
            slot = IpSlot();
            slot.used = true;
            slot.ip = ip;
        }
        return slot;
    }

    void Relay::uncount(std::uint32_t ip, std::uint32_t IpCount::*what)
    {
        auto it = ipCounts_.find(ip);
        if (it == ipCounts_.end())
            return;
        IpCount &c = it->second;
        if (c.*what > 0)
            --(c.*what);
        if (c.peers == 0 && c.rooms == 0 && c.knocks == 0)
            ipCounts_.erase(it);
    }

    void Relay::clearKnock(Knock &k)
    {
        if (k.peer != 0)
            uncount(k.ip, &IpCount::knocks);
        k = Knock();
    }

    void Relay::over(const Address &at, std::uint64_t &count, const char *why, std::uint32_t nowMs)
    {
        ++count;
        IpSlot &slot = ipSlot(at.ip);
        if (slot.logged && nowMs - slot.loggedMs < kOverLogMs)
            return;
        slot.logged = true;
        slot.loggedMs = nowMs;
        note(shown(at) + " over its limit (" + why + "): what is over is let fall");
    }

    bool Relay::mayAnswer(const Address &from, std::uint32_t nowMs)
    {
        if (nowMs - versionSecondMs_ >= 1000)
        {
            versionSecondMs_ = nowMs;
            versionCount_ = 0;
        }
        if (versionCount_ >= kVersionPerSecond)
            return false;
        std::uint32_t &said = versionSaidMs_[(from.ip * 2654435761u) >> 22];
        if (said != 0 && nowMs + 1 - said < 1000)
            return false;
        said = nowMs + 1;
        ++versionCount_;
        return true;
    }

    void Relay::setLogSalt(const std::uint8_t *salt) { std::memcpy(salt_, salt, sizeof(salt_)); }

    std::string Relay::shown(const Address &at) const
    {
        char line[32];
        switch (ipLog)
        {
        case IpLog::Full:
            return at.text();
        case IpLog::Hash:
        {
            // (The address alone, not its port: the same console the same
            // all day, whichever port its router gives it.)
            const std::uint8_t ip[4] = {static_cast<std::uint8_t>(at.ip >> 24), static_cast<std::uint8_t>(at.ip >> 16),
                                        static_cast<std::uint8_t>(at.ip >> 8), static_cast<std::uint8_t>(at.ip)};
            std::snprintf(line, sizeof(line), "h:%06x",
                          static_cast<unsigned>(sipHash24(salt_, ip, sizeof(ip)) & 0xFFFFFFu));
            return line;
        }
        default:
            std::snprintf(line, sizeof(line), "%u.%u.%u.x", (at.ip >> 24) & 0xFF, (at.ip >> 16) & 0xFF,
                          (at.ip >> 8) & 0xFF);
            return line;
        }
    }

    std::string Relay::named(const Peer &peer) const
    {
        return "#" + std::to_string(peer.id) + " (" + shown(peer.at) + ")";
    }

    int Relay::Room::count() const
    {
        int n = 1;
        for (int c = 1; c < kNetPlayers; ++c)
            n += clients[c] != 0;
        return n;
    }

    unsigned Relay::Room::mask() const
    {
        unsigned bits = 0;
        for (int c = 1; c < kNetPlayers; ++c)
            if (clients[c] != 0)
                bits |= 1u << c;
        return bits;
    }

    void Relay::note(const std::string &line)
    {
        if (log)
            log(line);
    }

    Relay::Peer *Relay::peer(const Address &at)
    {
        auto it = at_.find(at.key());
        return it == at_.end() ? nullptr : byId(it->second);
    }

    Relay::Peer *Relay::byId(std::uint32_t id)
    {
        auto it = id == 0 ? peers_.end() : peers_.find(id);
        return it == peers_.end() ? nullptr : &it->second;
    }

    void Relay::say(std::vector<Out> &out, const Peer &to, const std::uint8_t *bytes, int size)
    {
        say(out, to.at, bytes, size);
    }

    void Relay::say(std::vector<Out> &out, const Address &to, const std::uint8_t *bytes, int size)
    {
        out.push_back({to, std::vector<std::uint8_t>(bytes, bytes + size)});
    }

    void Relay::status(Room &room, std::uint32_t nowMs, std::vector<Out> &out)
    {
        Peer *host = byId(room.host);
        if (host == nullptr)
            return;
        std::uint8_t bytes[kRelayHeadBytes + 1];
        const int size = relaySmallWrite(bytes, RelayKind::Status, host->session, static_cast<int>(room.mask()));
        say(out, *host, bytes, size);
        room.statusMs = nowMs;
    }

    void Relay::knock(const Room &room, const Knock &k, std::vector<Out> &out)
    {
        Peer *host = byId(room.host);
        if (host == nullptr)
            return;
        std::uint8_t bytes[kRelayHeadBytes + 1 + kNetTextBytes];
        say(out, *host, bytes, relayKnockWrite(bytes, host->session, k.ticket, k.name));
    }

    void Relay::unknock(Peer &peer, std::vector<Out> &out, int refused)
    {
        auto it = rooms_.find(peer.knocking);
        peer.knocking = 0;
        if (it != rooms_.end())
        {
            for (Knock &k : it->second.knocks)
                if (k.peer == peer.id)
                    clearKnock(k);
        }
        if (refused > 0)
        {
            std::uint8_t bytes[kRelayHeadBytes + 1];
            say(out, peer, bytes, relaySmallWrite(bytes, RelayKind::Refused, peer.session, refused));
        }
    }

    void Relay::admitInto(Room &room, Peer &peer, std::uint32_t nowMs, std::vector<Out> &out)
    {
        std::uint8_t bytes[kRelayHeadBytes + 1];
        int slot = -1;
        for (int c = 1; c < kNetPlayers && room.admit && room.count() < room.open.most && slot < 0; ++c)
            if (room.clients[c] == 0)
                slot = c;
        if (slot < 0)
        {
            const RelayRefuse why = room.admit ? RelayRefuse::Full : RelayRefuse::Closed;
            say(out, peer, bytes, relaySmallWrite(bytes, RelayKind::Refused, peer.session, static_cast<int>(why)));
            return;
        }
        room.clients[slot] = peer.id;
        peer.room = room.id;
        peer.slot = slot;
        note(named(peer) + " joined room " + std::to_string(room.id) + " as client " + std::to_string(slot));
        say(out, peer, bytes, relaySmallWrite(bytes, RelayKind::Joined, peer.session, slot));
        status(room, nowMs, out);
    }

    void Relay::leave(Peer &peer, std::vector<Out> &out, int why)
    {
        if (peer.knocking != 0)
            unknock(peer, out, 0);
        auto it = rooms_.find(peer.room);
        const int slot = peer.slot;
        peer.room = 0;
        peer.slot = -1;
        std::uint8_t bye[kRelayHeadBytes + 1];
        if (why > 0)
            say(out, peer, bye, relaySmallWrite(bye, RelayKind::Bye, peer.session, why));
        if (it == rooms_.end())
            return;
        Room &room = it->second;
        if (slot == 0)
        {
            std::vector<std::uint32_t> gone;
            for (int c = 1; c < kNetPlayers; ++c)
            {
                Peer *client = byId(room.clients[c]);
                if (client == nullptr)
                    continue;
                client->room = 0;
                client->slot = -1;
                // (They are let go with it: whatever they do next, they come
                // in again.)
                const RelayBye theirs = why == static_cast<int>(RelayBye::Silent) ? RelayBye::HostSilent
                                                                                 : RelayBye::HostLeft;
                say(out, *client, bye,
                    relaySmallWrite(bye, RelayKind::Bye, client->session, static_cast<int>(theirs)));
                gone.push_back(client->id);
            }
            // (Those at its door: there is no room any more.)
            for (Knock &k : room.knocks)
            {
                Peer *waiting = byId(k.peer);
                clearKnock(k);
                if (waiting == nullptr)
                    continue;
                waiting->knocking = 0;
                std::uint8_t no[kRelayHeadBytes + 1];
                say(out, *waiting, no,
                    relaySmallWrite(no, RelayKind::Refused, waiting->session, static_cast<int>(RelayRefuse::NoRoom)));
            }
            uncount(peer.at.ip, &IpCount::rooms);
            note("room " + std::to_string(room.id) + " closed");
            rooms_.erase(it);
            for (std::uint32_t id : gone)
                drop(id, out, 0);
            return;
        }
        if (slot >= 1 && slot < kNetPlayers && room.clients[slot] == peer.id)
        {
            room.clients[slot] = 0;
            note(named(peer) + " left room " + std::to_string(room.id) + " (client " + std::to_string(slot) + ")");
            status(room, room.statusMs, out);
        }
    }

    void Relay::drop(std::uint32_t id, std::vector<Out> &out, int why)
    {
        auto it = peers_.find(id);
        if (it == peers_.end())
            return;
        leave(it->second, out, why);
        uncount(it->second.at.ip, &IpCount::peers);
        auto at = at_.find(it->second.at.key());
        if (at != at_.end() && at->second == id)
            at_.erase(at);
        peers_.erase(it);
    }

    void Relay::receive(const Address &from, const std::uint8_t *bytes, int size, std::uint32_t nowMs,
                        std::vector<Out> &out)
    {
        // Another version: told this one's, now and then (HlRelay.hpp).
        if (relayOtherVersion(bytes, size))
        {
            ++drops_.otherVersion;
            if (mayAnswer(from, nowMs))
            {
                std::uint8_t version[kRelayHeadBytes];
                say(out, from, version, relayVersionWrite(version, relayGet32(bytes + 4)));
            }
            return;
        }
        RelayHead head;
        if (!relayHeadRead(bytes, size, head))
        {
            ++drops_.notOurs;
            return;
        }
        // Coming in: a cookie for a Hello (kept nowhere: it is made again to
        // be checked), and a console known for an Enter that sends it back.
        // (Not too often from one address.)
        if ((head.kind == RelayKind::Hello || head.kind == RelayKind::Enter) &&
            !ipSlot(from.ip).hello.take(1, limits.helloPerSecond, 2 * limits.helloPerSecond, nowMs))
        {
            over(from, drops_.rateHello, "coming in", nowMs);
            return;
        }
        if (head.kind == RelayKind::Hello)
        {
            if (size < kRelayHelloBytes)
            {
                ++drops_.unknown;
                return;
            }
            std::uint8_t made[kRelayCookieBytes], challenge[kRelayChallengeBytes];
            cookie(from, head.session, nowMs >> kCookieShift, made);
            say(out, from, challenge, relayChallengeWrite(challenge, head.session, made));
            return;
        }
        if (head.kind == RelayKind::Enter)
        {
            enter(from, head.session, bytes, size, nowMs, out);
            return;
        }
        // Anything else: only from a console known at that address, in the
        // session it was given. (Another's session, or none: not answered,
        // and the one known there is left as it is.)
        Peer *me = peer(from);
        if (me == nullptr || me->session != head.session)
        {
            // (Told so, now and then: a console that was let go without
            // hearing it, or whose relay was started again, or whose router
            // gave it another port, knows to come in again.)
            ++drops_.unknown;
            if (relayConsoleSays(head.kind) && mayAnswer(from, nowMs))
            {
                std::uint8_t unknown[kRelayHeadBytes];
                say(out, from, unknown, relaySmallWrite(unknown, RelayKind::Unknown, head.session));
            }
            return;
        }
        me->heardMs = nowMs;
        // Not more than a game needs (Limits.hpp).
        if (head.kind == RelayKind::Data)
        {
            const std::uint32_t bytesRate = me->slot == 0 ? limits.hostBytesPerSecond : limits.clientBytesPerSecond;
            if (!me->data.take(1, limits.dataPerSecond, 2 * limits.dataPerSecond, nowMs) ||
                !me->dataBytes.take(static_cast<std::uint32_t>(size), bytesRate, 2 * bytesRate, nowMs))
            {
                over(from, drops_.rateData, "game frames", nowMs);
                return;
            }
        }
        else if (head.kind == RelayKind::Voice)
        {
            if (!me->voice.take(1, limits.voicePerSecond, limits.voicePerSecond, nowMs))
            {
                ++drops_.rateVoice; // (a voice's worth: too common to log)
                return;
            }
        }
        else if (!me->control.take(1, limits.controlPerSecond, 2 * limits.controlPerSecond, nowMs))
        {
            over(from, drops_.rateControl, "control", nowMs);
            return;
        }
        std::uint8_t reply[kRelayRoomsMostBytes > kRelayMostBytes ? kRelayRoomsMostBytes : kRelayMostBytes];

        switch (head.kind)
        {
        case RelayKind::Ping:
        {
            say(out, *me, reply, relaySmallWrite(reply, RelayKind::Pong, me->session));
            break;
        }
        case RelayKind::Open:
        {
            RelayOpen open;
            if (!relayOpenRead(bytes, size, open))
                break;
            auto it = rooms_.find(me->room);
            if (it != rooms_.end() && me->slot != 0)
            {
                leave(*me, out, 0);
                it = rooms_.end();
            }
            if (it == rooms_.end())
            {
                if (rooms_.size() >= kMostRooms)
                    break;
                if (counted(from.ip).rooms >= limits.roomsPerIp)
                {
                    over(from, drops_.capRooms, "rooms at once", nowMs);
                    break;
                }
                ++counted(from.ip).rooms;
                Room room;
                room.id = nextRoom_++;
                if (nextRoom_ == 0)
                    nextRoom_ = 1;
                room.host = me->id;
                it = rooms_.emplace(room.id, room).first;
                me->room = room.id;
                me->slot = 0;
                note(named(*me) + " opened room " + std::to_string(room.id));
            }
            it->second.open = open;
            say(out, *me, reply, relayRoomIdWrite(reply, RelayKind::Opened, me->session, it->second.id));
            status(it->second, nowMs, out);
            break;
        }
        case RelayKind::List:
        {
            RelayRoom list[kRelayListMost];
            int count = 0;
            for (const auto &[id, room] : rooms_)
            {
                if (count == kRelayListMost)
                    break;
                if (!room.admit || room.count() >= room.open.most)
                    continue;
                RelayRoom &entry = list[count++];
                entry.id = id;
                std::memcpy(entry.name, room.open.name, sizeof(entry.name));
                entry.players = static_cast<std::uint8_t>(room.count());
                entry.most = room.open.most;
                entry.open = room.admit;
                std::memcpy(entry.room, room.open.room, sizeof(entry.room));
            }
            say(out, *me, reply, relayRoomsWrite(reply, me->session, list, count));
            break;
        }
        case RelayKind::Join:
        {
            std::uint32_t id = 0;
            if (!relayRoomIdRead(bytes, size, id))
                break;
            // (Asked again, its answer lost: the same answer. Still at the
            // door: still waiting -- the host is asked again by tick.)
            if (me->room == id && me->slot > 0)
            {
                say(out, *me, reply, relaySmallWrite(reply, RelayKind::Joined, me->session, me->slot));
                break;
            }
            if (me->knocking == id)
                break;
            if (me->room != 0)
                leave(*me, out, 0);
            if (me->knocking != 0)
                unknock(*me, out, 0);
            auto it = rooms_.find(id);
            RelayRefuse why = RelayRefuse::NoRoom;
            Knock *free = nullptr;
            if (it != rooms_.end())
            {
                Room &room = it->second;
                why = !room.admit ? RelayRefuse::Closed : RelayRefuse::Full;
                for (Knock &k : room.knocks)
                    if (k.peer == 0 && free == nullptr)
                        free = &k;
                if (!room.admit || room.count() >= room.open.most)
                    free = nullptr;
            }
            if (free != nullptr && counted(from.ip).knocks >= limits.knocksPerIp)
            {
                over(from, drops_.capKnocks, "waiting at doors at once", nowMs);
                free = nullptr;
                why = RelayRefuse::Busy;
            }
            if (free == nullptr)
            {
                say(out, *me, reply,
                    relaySmallWrite(reply, RelayKind::Refused, me->session, static_cast<int>(why)));
                break;
            }
            // At the door: the host is asked.
            Room &room = it->second;
            free->peer = me->id;
            free->ip = from.ip;
            ++counted(from.ip).knocks;
            relayJoinName(bytes, size, free->name);
            free->ticket = room.nextTicket;
            room.nextTicket = static_cast<std::uint8_t>(room.nextTicket == 255 ? 1 : room.nextTicket + 1);
            free->sinceMs = free->saidMs = nowMs;
            me->knocking = id;
            note(named(*me) + " asks to join room " + std::to_string(id));
            knock(room, *free, out);
            break;
        }
        case RelayKind::Answer:
        {
            auto it = rooms_.find(me->room);
            int ticket = 0;
            bool yes = false;
            if (it == rooms_.end() || me->slot != 0 || !relayAnswerRead(bytes, size, ticket, yes))
                break;
            Room &room = it->second;
            for (Knock &k : room.knocks)
            {
                if (k.peer == 0 || k.ticket != ticket)
                    continue;
                Peer *waiting = byId(k.peer);
                clearKnock(k);
                if (waiting == nullptr)
                    break;
                waiting->knocking = 0;
                if (yes)
                    admitInto(room, *waiting, nowMs, out);
                else
                {
                    note(named(*waiting) + " not let into room " + std::to_string(room.id));
                    say(out, *waiting, reply,
                        relaySmallWrite(reply, RelayKind::Refused, waiting->session,
                                        static_cast<int>(RelayRefuse::Declined)));
                }
                break;
            }
            break;
        }
        case RelayKind::Admit:
        {
            auto it = rooms_.find(me->room);
            const int allow = relaySmallRead(bytes, size);
            if (it != rooms_.end() && me->slot == 0 && allow >= 0)
            {
                it->second.admit = (allow & kRelayAdmitIn) != 0;
                it->second.voice = (allow & kRelayAdmitVoice) != 0;
            }
            break;
        }
        case RelayKind::Voice:
        {
            // To everyone else in its room, if its host lets them talk; not
            // more than a voice's worth a second.
            auto it = rooms_.find(me->room);
            RelayVoice voice;
            if (it == rooms_.end() || !it->second.voice || me->slot < 0 || !relayVoiceRead(bytes, size, voice))
                break;
            Room &room = it->second;
            for (int c = 0; c < kNetPlayers; ++c)
            {
                const std::uint32_t id = c == 0 ? room.host : room.clients[c];
                Peer *other = id == me->id ? nullptr : byId(id);
                if (other == nullptr)
                    continue;
                std::memcpy(reply, bytes, static_cast<std::size_t>(size));
                relayPut32(reply + 4, other->session);
                say(out, *other, reply, size);
            }
            break;
        }
        case RelayKind::Kick:
        {
            auto it = rooms_.find(me->room);
            const int slot = relaySmallRead(bytes, size);
            if (it == rooms_.end() || me->slot != 0 || slot < 1 || slot >= kNetPlayers)
                break;
            if (Peer *client = byId(it->second.clients[slot]))
                drop(client->id, out, static_cast<int>(RelayBye::Kicked));
            break;
        }
        case RelayKind::Data:
        {
            auto it = rooms_.find(me->room);
            int who = 0;
            const std::uint8_t *frame = nullptr;
            const int length = relayDataRead(bytes, size, who, frame);
            if (it == rooms_.end() || length <= 0 || length > kNetCmdBytes)
                break;
            Room &room = it->second;
            if (me->slot == 0)
            {
                for (int c = 1; c < kNetPlayers; ++c)
                {
                    if (Peer *client = byId(room.clients[c]))
                        say(out, *client, reply, relayDataWrite(reply, client->session, 0, frame, length));
                }
            }
            else if (length <= kNetReplyBytes)
            {
                if (Peer *host = byId(room.host))
                    say(out, *host, reply, relayDataWrite(reply, host->session, me->slot, frame, length));
            }
            break;
        }
        case RelayKind::Bye:
            drop(me->id, out, 0);
            break;
        default:
            break; // (what only the relay says)
        }
    }

    void Relay::stop(std::vector<Out> &out)
    {
        std::uint8_t bye[kRelayHeadBytes + 1];
        for (const auto &[id, peer] : peers_)
        {
            const int size = relaySmallWrite(bye, RelayKind::Bye, peer.session, static_cast<int>(RelayBye::RelayStopping));
            say(out, peer, bye, size);
            say(out, peer, bye, size);
        }
        note("stopping: " + std::to_string(peers_.size()) + " consoles told");
        peers_.clear();
        at_.clear();
        rooms_.clear();
        ipCounts_.clear();
    }

    void Relay::tick(std::uint32_t nowMs, std::vector<Out> &out)
    {
        std::vector<std::uint32_t> silent;
        for (const auto &[id, peer] : peers_)
            if (nowMs - peer.heardMs > kRelaySilentMs)
                silent.push_back(id);
        for (std::uint32_t id : silent)
        {
            // (A host's clients go with it: one of them may be gone already.)
            const Peer *gone = byId(id);
            if (gone == nullptr)
                continue;
            note(named(*gone) + " went quiet");
            drop(id, out, static_cast<int>(RelayBye::Silent));
        }
        for (auto &[id, room] : rooms_)
        {
            if (nowMs - room.statusMs >= kStatusMs)
                status(room, nowMs, out);
            // At the door: the host asked again now and then (a knock lost on
            // the way is not waited on), until it is too late.
            for (Knock &k : room.knocks)
            {
                if (k.peer == 0)
                    continue;
                Peer *waiting = byId(k.peer);
                if (waiting == nullptr)
                    clearKnock(k);
                else if (nowMs - k.sinceMs > kRelayKnockMs)
                {
                    note(named(*waiting) + " was not answered by room " + std::to_string(id));
                    unknock(*waiting, out, static_cast<int>(RelayRefuse::NoAnswer));
                }
                else if (nowMs - k.saidMs >= kStatusMs)
                {
                    k.saidMs = nowMs;
                    knock(room, k, out);
                }
            }
        }
    }
} // namespace hlrelay
