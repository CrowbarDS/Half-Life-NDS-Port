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
    void Relay::leave(Peer &peer, std::vector<Out> &out, bool told)
    {
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
            // (Asked again, its answer lost: the same answer.)
            if (me->room == id && me->slot > 0)
            {
                say(out, *me, reply, relaySmallWrite(reply, RelayKind::Joined, me->session, me->slot));
                break;
            }
            if (me->room != 0)
                leave(*me, out, false);
            auto it = rooms_.find(id);
            RelayRefuse why = RelayRefuse::NoRoom;
            int slot = -1;
            if (it != rooms_.end())
            {
                Room &room = it->second;
                why = !room.admit ? RelayRefuse::Closed : RelayRefuse::Full;
                for (int c = 1; c < kNetPlayers && room.admit && room.count() < room.open.most && slot < 0; ++c)
                    if (room.clients[c] == 0)
                        slot = c;
            }
            if (slot < 0)
            {
                say(out, *me, reply,
                    relaySmallWrite(reply, RelayKind::Refused, me->session, static_cast<int>(why)));
                break;
            }
            Room &room = it->second;
            room.clients[slot] = from.key();
            me->room = id;
            me->slot = slot;
            note(from.text() + " joined room " + std::to_string(id) + " as client " + std::to_string(slot));
            say(out, *me, reply, relaySmallWrite(reply, RelayKind::Joined, me->session, slot));
            status(room, nowMs, out);
            break;
        }
        case RelayKind::Admit:
        {
            auto it = rooms_.find(me->room);
            const int allow = relaySmallRead(bytes, size);
            if (it != rooms_.end() && me->slot == 0 && allow >= 0)
                it->second.admit = allow != 0;
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
            if (nowMs - room.statusMs >= kStatusMs)
                status(room, nowMs, out);
    }
} // namespace hlrelay
