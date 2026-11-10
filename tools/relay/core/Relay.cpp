#include "Relay.hpp"

#include <cstdio>

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
        auto it = peers_.find(at.key());
        return it == peers_.end() ? nullptr : &it->second;
    }

    void Relay::say(std::vector<Out> &out, const Peer &to, const std::uint8_t *bytes, int size)
    {
        out.push_back({to.at, std::vector<std::uint8_t>(bytes, bytes + size)});
    }

    void Relay::status(Room &room, std::uint32_t nowMs, std::vector<Out> &out)
    {
        auto host = peers_.find(room.host);
        if (host == peers_.end())
            return;
        std::uint8_t bytes[kRelayHeadBytes + 1];
        const int size = relaySmallWrite(bytes, RelayKind::Status, host->second.session, static_cast<int>(room.mask()));
        say(out, host->second, bytes, size);
        room.statusMs = nowMs;
    }

    /// `peer` out of its room (as a host: the room closed, its clients told);
    /// `told`: it is told so itself.
    void Relay::knock(const Room &room, const Knock &k, std::vector<Out> &out)
    {
        auto host = peers_.find(room.host);
        if (host == peers_.end())
            return;
        std::uint8_t bytes[kRelayHeadBytes + 1 + kNetTextBytes];
        say(out, host->second, bytes, relayKnockWrite(bytes, host->second.session, k.ticket, k.name));
    }

    void Relay::unknock(Peer &peer, std::vector<Out> &out, int refused)
    {
        auto it = rooms_.find(peer.knocking);
        peer.knocking = 0;
        if (it != rooms_.end())
        {
            for (Knock &k : it->second.knocks)
                if (k.peer == peer.at.key())
                    k = Knock();
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
        room.clients[slot] = peer.at.key();
        peer.room = room.id;
        peer.slot = slot;
        note(peer.at.text() + " joined room " + std::to_string(room.id) + " as client " + std::to_string(slot));
        say(out, peer, bytes, relaySmallWrite(bytes, RelayKind::Joined, peer.session, slot));
        status(room, nowMs, out);
    }

    void Relay::leave(Peer &peer, std::vector<Out> &out, bool told)
    {
        if (peer.knocking != 0)
            unknock(peer, out, 0);
        auto it = rooms_.find(peer.room);
        const int slot = peer.slot;
        peer.room = 0;
        peer.slot = -1;
        std::uint8_t bye[kRelayHeadBytes];
        if (told)
            say(out, peer, bye, relaySmallWrite(bye, RelayKind::Bye, peer.session));
        if (it == rooms_.end())
            return;
        Room &room = it->second;
        if (slot == 0)
        {
            for (int c = 1; c < kNetPlayers; ++c)
            {
                auto client = peers_.find(room.clients[c]);
                if (client == peers_.end())
                    continue;
                client->second.room = 0;
                client->second.slot = -1;
                say(out, client->second, bye, relaySmallWrite(bye, RelayKind::Bye, client->second.session));
            }
            // (Those at its door: there is no room any more.)
            for (const Knock &k : room.knocks)
            {
                auto waiting = peers_.find(k.peer);
                if (k.peer == 0 || waiting == peers_.end())
                    continue;
                waiting->second.knocking = 0;
                std::uint8_t no[kRelayHeadBytes + 1];
                say(out, waiting->second, no,
                    relaySmallWrite(no, RelayKind::Refused, waiting->second.session,
                                    static_cast<int>(RelayRefuse::NoRoom)));
            }
            note("room " + std::to_string(room.id) + " closed");
            rooms_.erase(it);
            return;
        }
        if (slot >= 1 && slot < kNetPlayers && room.clients[slot] == peer.at.key())
        {
            room.clients[slot] = 0;
            note(peer.at.text() + " left room " + std::to_string(room.id) + " (client " + std::to_string(slot) + ")");
            status(room, room.statusMs, out);
        }
    }

    void Relay::drop(std::uint64_t key, std::vector<Out> &out, bool told)
    {
        auto it = peers_.find(key);
        if (it == peers_.end())
            return;
        leave(it->second, out, told);
        peers_.erase(it);
    }

    void Relay::receive(const Address &from, const std::uint8_t *bytes, int size, std::uint32_t nowMs,
                        std::vector<Out> &out)
    {
        RelayHead head;
        if (!relayHeadRead(bytes, size, head))
            return;
        Peer *me = peer(from);
        // (The same address with another session: a console started again,
        // or another behind the same port. The one before is gone.)
        if (me != nullptr && me->session != head.session)
        {
            drop(from.key(), out, false);
            me = nullptr;
        }
        if (me == nullptr)
        {
            if (head.kind == RelayKind::Bye || peers_.size() >= kMostPeers)
                return;
            Peer fresh;
            fresh.at = from;
            fresh.session = head.session;
            me = &(peers_[from.key()] = fresh);
            note(from.text() + " came");
        }
        me->heardMs = nowMs;
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
                leave(*me, out, false);
                it = rooms_.end();
            }
            if (it == rooms_.end())
            {
                if (rooms_.size() >= kMostRooms)
                    break;
                Room room;
                room.id = nextRoom_++;
                if (nextRoom_ == 0)
                    nextRoom_ = 1;
                room.host = from.key();
                it = rooms_.emplace(room.id, room).first;
                me->room = room.id;
                me->slot = 0;
                note(from.text() + " opened room " + std::to_string(room.id) + " \"" + open.name + "\"");
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
                leave(*me, out, false);
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
            if (free == nullptr)
            {
                say(out, *me, reply,
                    relaySmallWrite(reply, RelayKind::Refused, me->session, static_cast<int>(why)));
                break;
            }
            // At the door: the host is asked.
            Room &room = it->second;
            free->peer = from.key();
            relayJoinName(bytes, size, free->name);
            free->ticket = room.nextTicket;
            room.nextTicket = static_cast<std::uint8_t>(room.nextTicket == 255 ? 1 : room.nextTicket + 1);
            free->sinceMs = free->saidMs = nowMs;
            me->knocking = id;
            note(from.text() + " (\"" + free->name + "\") asks to join room " + std::to_string(id));
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
                auto waiting = peers_.find(k.peer);
                k = Knock();
                if (waiting == peers_.end())
                    break;
                waiting->second.knocking = 0;
                if (yes)
                    admitInto(room, waiting->second, nowMs, out);
                else
                {
                    note(waiting->second.at.text() + " not let into room " + std::to_string(room.id));
                    say(out, waiting->second, reply,
                        relaySmallWrite(reply, RelayKind::Refused, waiting->second.session,
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
            if (nowMs - me->voiceSinceMs >= 1000)
            {
                me->voiceSinceMs = nowMs;
                me->voiceCount = 0;
            }
            if (++me->voiceCount > kRelayVoicePerSecond)
                break;
            Room &room = it->second;
            for (int c = 0; c < kNetPlayers; ++c)
            {
                const std::uint64_t key = c == 0 ? room.host : room.clients[c];
                auto other = peers_.find(key);
                if (key == 0 || key == from.key() || other == peers_.end())
                    continue;
                std::memcpy(reply, bytes, static_cast<std::size_t>(size));
                relayPut32(reply + 4, other->second.session);
                say(out, other->second, reply, size);
            }
            break;
        }
        case RelayKind::Kick:
        {
            auto it = rooms_.find(me->room);
            const int slot = relaySmallRead(bytes, size);
            if (it == rooms_.end() || me->slot != 0 || slot < 1 || slot >= kNetPlayers)
                break;
            auto client = peers_.find(it->second.clients[slot]);
            if (client != peers_.end())
                leave(client->second, out, true);
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
                    auto client = peers_.find(room.clients[c]);
                    if (client != peers_.end())
                        say(out, client->second, reply,
                            relayDataWrite(reply, client->second.session, 0, frame, length));
                }
            }
            else if (length <= kNetReplyBytes)
            {
                auto host = peers_.find(room.host);
                if (host != peers_.end())
                    say(out, host->second, reply, relayDataWrite(reply, host->second.session, me->slot, frame, length));
            }
            break;
        }
        case RelayKind::Bye:
            drop(from.key(), out, false);
            break;
        default:
            break; // (what only the relay says)
        }
    }

    void Relay::tick(std::uint32_t nowMs, std::vector<Out> &out)
    {
        std::vector<std::uint64_t> silent;
        for (const auto &[key, peer] : peers_)
            if (nowMs - peer.heardMs > kRelaySilentMs)
                silent.push_back(key);
        for (std::uint64_t key : silent)
        {
            note(peers_[key].at.text() + " went quiet");
            drop(key, out, true);
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
                auto waiting = peers_.find(k.peer);
                if (waiting == peers_.end())
                    k = Knock();
                else if (nowMs - k.sinceMs > kRelayKnockMs)
                {
                    note(waiting->second.at.text() + " was not answered by room " + std::to_string(id));
                    unknock(waiting->second, out, static_cast<int>(RelayRefuse::NoAnswer));
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
