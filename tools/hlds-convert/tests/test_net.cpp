// A game between consoles (shared/HlNet.hpp, HlNetGame.hpp), played here by
// several games in one process joined by a link that loses frames: what is
// said once arrives once, in order, whatever is lost on the way.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

#include "HlGuest.hpp"
#include "HlNet.hpp"
#include "HlNetAir.hpp"
#include "HlNetGame.hpp"
#include "HlPreview.hpp"
#include "HlView.hpp"
#include "TestGame.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    std::uint32_t g_seed = 2024;
    std::uint32_t rnd()
    {
        g_seed = g_seed * 1664525u + 1013904223u;
        return g_seed >> 8;
    }

    /// The air: a frame put in comes out some frames later, or not at all.
    struct Air
    {
        int lossPercent = 20;
        int mostDelay = 5;
        struct Frame
        {
            int due;
            std::vector<std::uint8_t> bytes;
        };
        std::deque<Frame> flying;
        int now = 0;
        void send(const std::uint8_t *bytes, int size)
        {
            if (size <= 0 || static_cast<int>(rnd() % 100) < lossPercent)
                return;
            flying.push_back({now + 1 + static_cast<int>(rnd() % (mostDelay + 1)),
                              std::vector<std::uint8_t>(bytes, bytes + size)});
        }
        /// The frames that land this tick, oldest first (a later one may
        /// overtake an earlier: the radio does not do that, but nothing
        /// here may depend on it).
        std::vector<std::vector<std::uint8_t>> land()
        {
            std::vector<std::vector<std::uint8_t>> out;
            for (auto it = flying.begin(); it != flying.end();)
            {
                if (it->due <= now)
                {
                    out.push_back(std::move(it->bytes));
                    it = flying.erase(it);
                }
                else
                    ++it;
            }
            return out;
        }
    };

    NetEvent numbered(NetEventType type, int from, int n)
    {
        NetEvent e;
        e.type = type;
        e.from = static_cast<std::uint8_t>(from);
        e.v[0] = static_cast<std::int16_t>(n);
        e.v[5] = static_cast<std::int16_t>(n * 7);
        return e;
    }

    void testPacking()
    {
        NetPlayer p;
        p.pos[0] = -12345;
        p.pos[1] = 321;
        p.pos[2] = 16000;
        p.yaw = 54321;
        p.pitch = -100;
        p.velocity[0] = -128;
        p.velocity[2] = 127;
        p.weapon = 5;
        p.flags = kNetHere | kNetAlive | kNetFiring;
        p.health = 87;
        p.spawns = 200;
        std::uint8_t bytes[64];
        hldata::detail::NetOut out{bytes, bytes + sizeof(bytes)};
        hldata::detail::put(out, p);
        check(out.ok && out.at - bytes == kNetPlayerBytes, "a player's state is sixteen bytes");
        NetPlayer q;
        hldata::detail::NetIn in{bytes, out.at};
        hldata::detail::get(in, q);
        check(in.ok && q.pos[0] == p.pos[0] && q.pos[2] == p.pos[2] && q.yaw == p.yaw && q.pitch == p.pitch &&
                  q.velocity[0] == p.velocity[0] && q.velocity[2] == p.velocity[2] && q.weapon == p.weapon &&
                  q.flags == p.flags && q.health == p.health && q.spawns == p.spawns,
              "... and reads back as it was written");

        NetEvent e;
        e.type = NetEventType::Hit;
        e.from = 2;
        e.a = 1;
        e.v[0] = 40;
        e.v[5] = -32768;
        hldata::detail::NetOut eo{bytes, bytes + sizeof(bytes)};
        hldata::detail::put(eo, e);
        check(eo.ok && eo.at - bytes == kNetEventBytes, "an event is fifteen bytes");
        NetEvent f;
        hldata::detail::NetIn ei{bytes, eo.at};
        hldata::detail::get(ei, f);
        check(f.type == e.type && f.from == 2 && f.a == 1 && f.v[0] == 40 && f.v[5] == -32768, "... and reads back");
        NetEvent hello;
        hello.type = NetEventType::Hello;
        std::strncpy(hello.text, "A name far too long for it", kNetTextBytes - 1);
        hldata::detail::NetOut ho{bytes, bytes + sizeof(bytes)};
        hldata::detail::put(ho, hello);
        hldata::detail::NetIn hi{bytes, ho.at};
        hldata::detail::get(hi, f);
        check(ho.at - bytes == kNetEventBytes && std::strcmp(f.text, "A name far ") == 0,
              "a name is eleven letters of it, and ends");

        // A frame cut short, or of something else, is not read.
        NetHost host;
        netHostJoin(host, 1);
        std::uint8_t frame[kNetCmdBytes];
        const int size = netHostWrite(host, p, frame);
        NetClient client;
        client.token = 77;
        host.clients[1].token = 77;
        const int resized = netHostWrite(host, p, frame);
        check(resized == size, "(the host's frame is as long whoever is in it)");
        check(size > 0 && size <= kNetCmdBytes && netClientRead(client, frame, size), "a client reads the host's frame");
        check(!netClientRead(client, frame, 20), "... not one cut short");
        frame[0] ^= 0xFF;
        check(!netClientRead(client, frame, size), "... nor one of another game");
        frame[0] ^= 0xFF;
        frame[2] = kNetVersion + 1;
        check(!netClientRead(client, frame, size), "... nor of another version of this one");
    }

    /// A host and three clients say three hundred things each through bad
    /// air; a fourth of the way in, one client leaves and another comes.
    void testEvents()
    {
        auto host = std::make_unique<NetHost>();
        std::unique_ptr<NetClient> clients[kNetPlayers];
        Air down[kNetPlayers], up[kNetPlayers];
        int hostSaid = 0, clientSaid[kNetPlayers] = {};
        int hostHeardFrom[kNetPlayers] = {};   // the next number the host expects of each
        int clientHeard[kNetPlayers] = {};     // ... and each client of the host
        int clientFirst[kNetPlayers] = {};     // the host's number each began at
        bool wrong = false;
        for (int c = 1; c < kNetPlayers; ++c)
        {
            clients[c] = std::make_unique<NetClient>();
            clients[c]->token = static_cast<std::uint16_t>(100 + c);
            netHostJoin(*host, c);
        }
        const int kSay = 300;
        int quiet = 0;
        for (int tick = 0; tick < 20000 && quiet < 200; ++tick)
        {
            // Client 2 goes at tick 150; someone new takes its place at 400.
            if (tick == 150)
            {
                netHostLeave(*host, 2);
                clients[2].reset();
            }
            if (tick == 400)
            {
                clients[2] = std::make_unique<NetClient>();
                clients[2]->token = 999;
                netHostJoin(*host, 2);
                clientSaid[2] = 0;
                hostHeardFrom[2] = 0;
                clientFirst[2] = clientHeard[2] = hostSaid;
                up[2].flying.clear();
                down[2].flying.clear();
            }
            // Everyone says something now and then.
            if (hostSaid < kSay && rnd() % 3 == 0 && host->out.count < NetOutbox::kMost)
                host->out.add(numbered(NetEventType::Score, 0, hostSaid++));
            for (int c = 1; c < kNetPlayers; ++c)
                if (clients[c] && clientSaid[c] < kSay && rnd() % 3 == 0 && clients[c]->out.count < NetOutbox::kMost)
                    clients[c]->out.add(numbered(NetEventType::Shot, c, clientSaid[c]++));

            std::uint8_t frame[kNetCmdBytes];
            NetPlayer mine;
            mine.pos[0] = static_cast<std::int16_t>(tick);
            const int size = netHostWrite(*host, mine, frame);
            wrong = wrong || size <= 0 || size > kNetCmdBytes;
            for (int c = 1; c < kNetPlayers; ++c)
            {
                down[c].now = up[c].now = tick;
                if (!clients[c])
                    continue;
                down[c].send(frame, size);
                for (const auto &bytes : down[c].land())
                    netClientRead(*clients[c], bytes.data(), static_cast<int>(bytes.size()));
                NetEvent e;
                while (clients[c]->in.take(e))
                {
                    wrong = wrong || e.type != NetEventType::Score || e.v[0] != clientHeard[c] ||
                            e.v[5] != static_cast<std::int16_t>(clientHeard[c] * 7);
                    ++clientHeard[c];
                }
                std::uint8_t reply[kNetReplyBytes];
                NetPlayer theirs;
                theirs.pos[1] = static_cast<std::int16_t>(c * 100 + tick % 50);
                const int length = netClientWrite(*clients[c], theirs, reply);
                wrong = wrong || length <= 0 || length > kNetReplyBytes;
                up[c].send(reply, length);
                for (const auto &bytes : up[c].land())
                    netHostRead(*host, c, bytes.data(), static_cast<int>(bytes.size()));
                while (host->clients[c].in.take(e))
                {
                    wrong = wrong || e.type != NetEventType::Shot || e.from != c || e.v[0] != hostHeardFrom[c];
                    ++hostHeardFrom[c];
                }
            }
            bool all = hostSaid == kSay && tick > 400;
            for (int c = 1; c < kNetPlayers; ++c)
                all = all && clients[c] && clientSaid[c] == kSay && hostHeardFrom[c] == kSay && clientHeard[c] == kSay;
            quiet = all ? quiet + 1 : 0;
            if (all && host->out.count == 0)
                break;
        }
        check(!wrong, "every event arrives in its order, once, through air that loses a fifth of the frames");
        bool whole = hostSaid == kSay;
        for (int c = 1; c < kNetPlayers; ++c)
            whole = whole && hostHeardFrom[c] == kSay && clientHeard[c] == kSay;
        check(whole, "... all of them: three hundred from each to the host and from the host to each");
        check(clientFirst[2] > 0 && clientFirst[2] < kSay,
              "(the one who came late began at what the host was saying then, not at the beginning)");
        check(host->out.count == 0 && host->out.lost == 0, "... and the host keeps nothing that everyone has");
        check(host->players[1].pos[1] / 100 == 1 && host->players[3].pos[1] / 100 == 3 &&
                  (host->players[1].flags & kNetHere),
              "the host knows where each client is");
        check(clients[1]->players[0].pos[0] > 0 && clients[1]->players[3].pos[1] / 100 == 3,
              "... and each client where the host and the other clients are");
    }

    /// A file of 300 KB sent to a client through air of a kind.
    int sendFile(const std::vector<std::uint8_t> &file, int loss, int delay, std::vector<std::uint8_t> &got,
                 FileReceiver &receiver, FileSender &sender)
    {
        Air down, up;
        down.lossPercent = up.lossPercent = loss;
        down.mostDelay = up.mostDelay = delay;
        receiver.open("map:/lvl/crossfire.lvl");
        got.clear();
        int ticks = 0;
        std::uint8_t frame[kNetCmdBytes];
        for (; ticks < 100000 && !receiver.done(); ++ticks)
        {
            down.now = up.now = ticks;
            const int ask = receiver.ask(frame);
            if (ask > 0)
                up.send(frame, ask);
            for (const auto &bytes : up.land())
                sender.asked(2, bytes.data(), static_cast<int>(bytes.size()));
            // The host sends two pieces a frame while someone lacks any.
            std::uint32_t index;
            for (int n = 0; n < 2 && sender.next(static_cast<std::uint32_t>(file.size()), index); ++n)
            {
                const std::size_t at = static_cast<std::size_t>(index) * kNetFilePiece;
                const std::size_t left = file.size() - at;
                const int count = left < kNetFilePiece ? static_cast<int>(left) : kNetFilePiece;
                down.send(frame, sender.piece(static_cast<std::uint32_t>(file.size()), index, file.data() + at,
                                              count, frame));
            }
            for (const auto &bytes : down.land())
            {
                // (The first frame says how long it is: room is made then.)
                if (receiver.known && got.size() != receiver.total)
                    got.resize(receiver.total);
                receiver.piece(2, bytes.data(), static_cast<int>(bytes.size()), got.empty() ? nullptr : got.data(),
                               static_cast<std::uint32_t>(got.size()));
            }
        }
        return ticks;
    }

    void testFile()
    {
        std::vector<std::uint8_t> file(300 * 1024 + 123);
        for (std::size_t i = 0; i < file.size(); ++i)
            file[i] = static_cast<std::uint8_t>(rnd());
        std::vector<std::uint8_t> got;
        FileReceiver receiver;
        FileSender sender;
        const int bad = sendFile(file, 20, 5, got, receiver, sender);
        check(receiver.done() && !receiver.missing && got == file,
              "a file of 300 KB arrives whole, byte for byte, through air that loses a fifth of the frames");
        check(std::strcmp(sender.name, "map:/lvl/crossfire.lvl") == 0 && sender.to == 2,
              "... the host knowing which file, and for whom");
        const int good = sendFile(file, 2, 1, got, receiver, sender);
        check(receiver.done() && got == file, "... and through good air");
        const int perfect = static_cast<int>(file.size()) / (2 * kNetFilePiece);
        check(good < perfect * 11 / 10 && bad < perfect * 3 / 2,
              "losing a frame costs that frame and little more: nothing is sent twice that came");
        std::printf("  (300 KB at two pieces a frame: %d frames with nothing lost, %d through good air, %d through bad)\n",
                    perfect, good, bad);

        // A file there is not.
        std::uint8_t frame[kNetCmdBytes];
        receiver.open("map:/none");
        const int ask = receiver.ask(frame);
        check(ask > 0 && ask <= kNetReplyBytes && sender.asked(2, frame, ask) && sender.fresh,
              "a second file is a new asking");
        receiver.piece(2, frame, sender.piece(0xFFFFFFFFu, 0, nullptr, 0, frame), nullptr, 0);
        check(receiver.done() && receiver.missing, "the host says when there is no such file");
        // A piece meant for someone else, or of an earlier asking.
        std::uint8_t small[16] = {};
        receiver.open("map:/a");
        sender.asked(2, frame, receiver.ask(frame));
        const int other = sender.piece(10, 0, file.data(), 10, frame);
        check(!receiver.piece(3, frame, other, small, sizeof(small)), "a piece for another client is not taken");
        receiver.open("map:/b");
        check(!receiver.piece(2, frame, other, small, sizeof(small)), "... nor one of the file asked for before");
        // An empty file is a file.
        receiver.open("map:/empty");
        sender.asked(2, frame, receiver.ask(frame));
        receiver.piece(2, frame, sender.piece(0, 0, nullptr, 0, frame), nullptr, 0);
        check(receiver.done() && !receiver.missing && receiver.total == 0, "a file of no bytes is there, and done");
    }

    /// One stream sent to guests at once (HlNetAir.hpp), each hearing the
    /// host through air of its own and each taking the pieces on as a
    /// loader would, a few a frame or none. `late`: guest 1 comes when the
    /// others are half way. `quiet`: guest 2 stops answering a third of the
    /// way. Returns the host's frames.
    struct AirGuest
    {
        AirReceiver receiver;
        std::vector<std::uint8_t> ring = std::vector<std::uint8_t>(kAirWindow * kAirPiece);
        std::vector<std::uint8_t> got;
        Air down, up;
        bool here = true;
    };
    int sendAir(const std::vector<std::uint8_t> &stream, int guests, int loss, int delay, bool late, bool quiet,
                std::vector<AirGuest> &out, AirSender &sender)
    {
        out.assign(static_cast<std::size_t>(guests), AirGuest());
        unsigned mask = 0;
        for (int g = 0; g < guests; ++g)
        {
            AirGuest &guest = out[static_cast<std::size_t>(g)];
            guest.receiver.want(AirStream::Map, "", guest.ring.data());
            guest.down.lossPercent = guest.up.lossPercent = loss;
            guest.down.mostDelay = guest.up.mostDelay = delay;
            if (!(late && g == 0))
                mask |= 1u << (g + 1);
        }
        sender.begin(AirStream::Map, "stalkyard", static_cast<std::uint32_t>(stream.size()), 7, mask, 2);
        if (late)
            out[0].here = false;
        std::uint8_t frame[kNetCmdBytes];
        int ticks = 0;
        for (; ticks < 200000; ++ticks)
        {
            bool all = true;
            for (int g = 0; g < guests; ++g)
                all = all && (out[static_cast<std::size_t>(g)].receiver.done() || !out[static_cast<std::size_t>(g)].here);
            if (all && sender.done() && (!late || out[0].here))
                break;
            if (late && !out[0].here && sender.percent() >= 50)
            {
                out[0].here = true;
                sender.add(1);
            }
            for (int g = 0; g < guests; ++g)
            {
                AirGuest &guest = out[static_cast<std::size_t>(g)];
                guest.down.now = guest.up.now = ticks;
                if (!guest.here)
                    continue;
                if (quiet && g == 1 && guest.got.size() > stream.size() / 3)
                    continue; // (gone silent: it hears, it says nothing, it takes nothing)
                const int ask = guest.receiver.ask(frame);
                if (ask > 0)
                    guest.up.send(frame, ask);
                for (const auto &bytes : guest.up.land())
                    sender.asked(g + 1, bytes.data(), static_cast<int>(bytes.size()));
            }
            sender.frame();
            // Two frames of the host's a frame, every guest hearing each.
            std::uint32_t index;
            for (int n = 0; n < 2 && sender.next(index); ++n)
            {
                const int size = sender.piece(index, stream.data() + index * kAirPiece, sender.pieceBytes(index), frame);
                for (AirGuest &guest : out)
                    guest.down.send(frame, size);
            }
            for (int g = 0; g < guests; ++g)
            {
                AirGuest &guest = out[static_cast<std::size_t>(g)];
                for (const auto &bytes : guest.down.land())
                {
                    AirHead head;
                    if (guest.here && airRead(bytes.data(), static_cast<int>(bytes.size()), head))
                        guest.receiver.frame(head);
                }
                if (!guest.here || (quiet && g == 1 && guest.got.size() > stream.size() / 3))
                    continue;
                // The loader: up to six pieces a frame, sometimes none (it
                // is busy with what it read).
                const int take = static_cast<int>(rnd() % 8) - 1;
                const std::uint8_t *data;
                std::size_t size;
                for (int n = 0; n < take && guest.receiver.next(data, size); ++n)
                    guest.got.insert(guest.got.end(), data, data + size);
            }
        }
        // (What is left: the last piece handed on is let go.)
        for (AirGuest &guest : out)
        {
            const std::uint8_t *data;
            std::size_t size;
            while (guest.here && !(quiet && &guest == &out[1]) && guest.receiver.next(data, size))
                guest.got.insert(guest.got.end(), data, data + size);
        }
        return ticks;
    }

    /// A guest's supply, for a GuestCursor: a stream through the air with
    /// one guest, the host sending as it is asked.
    struct AirTestSupply : GuestSupply
    {
        const std::vector<std::uint8_t> *stream = nullptr;
        AirSender sender;
        AirGuest guest;
        int ticks = 0;
        bool more(const std::uint8_t *&data, std::size_t &size) override
        {
            std::uint8_t frame[kNetCmdBytes];
            for (int spins = 0; spins < 100000; ++spins)
            {
                if (guest.receiver.next(data, size))
                    return true;
                if (guest.receiver.done())
                    return false;
                guest.down.now = guest.up.now = ++ticks;
                const int ask = guest.receiver.ask(frame);
                if (ask > 0)
                    guest.up.send(frame, ask);
                for (const auto &bytes : guest.up.land())
                    sender.asked(1, bytes.data(), static_cast<int>(bytes.size()));
                std::uint32_t index;
                for (int n = 0; n < 2 && sender.next(index); ++n)
                    guest.down.send(frame, sender.piece(index, stream->data() + index * kAirPiece,
                                                        sender.pieceBytes(index), frame));
                for (const auto &bytes : guest.down.land())
                {
                    AirHead head;
                    if (airRead(bytes.data(), static_cast<int>(bytes.size()), head))
                        guest.receiver.frame(head);
                }
            }
            return false;
        }
    };

    std::vector<std::uint8_t> readAll(const std::string &path)
    {
        std::vector<std::uint8_t> bytes;
        if (std::FILE *file = std::fopen(path.c_str(), "rb"))
        {
            std::fseek(file, 0, SEEK_END);
            bytes.resize(static_cast<std::size_t>(std::ftell(file)));
            std::fseek(file, 0, SEEK_SET);
            if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
                bytes.clear();
            std::fclose(file);
        }
        return bytes;
    }

    void testAir()
    {
        // The Download Play parameter.
        std::uint8_t param[kNetDlParamBytes];
        NetDlParam dl;
        dl.channel = 6;
        for (int i = 0; i < 6; ++i)
            dl.host[i] = static_cast<std::uint8_t>(0x10 + i);
        netDlWrite(dl, param);
        NetDlParam back;
        check(netDlRead(param, back) && back.channel == 6 && back.host[5] == 0x15,
              "the program's parameter says where its host is");
        std::uint8_t junk[kNetDlParamBytes] = {};
        check(!netDlRead(junk, back), "... and a console started another way finds none");
        param[4] = static_cast<std::uint8_t>(kNetVersion + 1);
        check(!netDlRead(param, back), "... nor one from another version of the game");

        std::vector<std::uint8_t> stream(400 * 1024 + 77);
        for (std::size_t i = 0; i < stream.size(); ++i)
            stream[i] = static_cast<std::uint8_t>(rnd());
        AirSender sender;
        std::vector<AirGuest> guests;
        const int perfect = static_cast<int>(airPieces(static_cast<std::uint32_t>(stream.size()))) / 2;
        int frames[3] = {};
        const int losses[3] = {0, 5, 20};
        for (int l = 0; l < 3; ++l)
        {
            frames[l] = sendAir(stream, 7, losses[l], losses[l] == 0 ? 0 : 4, false, false, guests, sender);
            bool whole = sender.done();
            for (const AirGuest &guest : guests)
                whole = whole && guest.receiver.done() && guest.got == stream;
            check(whole, losses[l] == 0 ? "a stream of 400 KB reaches seven guests at once, byte for byte"
                         : losses[l] == 5 ? "... through air that loses one frame in twenty to each"
                                          : "... and one in five");
        }
        check(guests[0].receiver.skill == 2 && std::strcmp(guests[0].receiver.name, "stalkyard") == 0,
              "... each told which map, and how hard");
        check(frames[0] < perfect * 12 / 10 && frames[1] < perfect * 2,
              "seven hear what is sent once: the frames are those of one guest, and little more");
        std::printf("  (400 KB to seven at two pieces a frame: %d frames at best, %d with nothing lost, %d, %d with 5%%, "
                    "20%%)\n",
                    perfect, frames[0], frames[1], frames[2]);

        sendAir(stream, 4, 5, 3, true, false, guests, sender);
        bool lateWhole = true;
        for (const AirGuest &guest : guests)
            lateWhole = lateWhole && guest.got == stream;
        check(lateWhole && sender.done(), "a guest that comes half way is sent it from the start, and all have it");

        sendAir(stream, 4, 5, 3, false, true, guests, sender);
        check(!sender.wants[2].on && guests[0].got == stream && guests[2].got == stream && guests[3].got == stream &&
                  sender.done(),
              "a guest that goes silent is left out, and the others finish");

        // A guest that has it already (a base it was sent for another map).
        sender.begin(AirStream::Base, "match", 137519, 9, 1u << 1);
        std::uint8_t frame[kNetReplyBytes];
        const int have = airHave(AirStream::Base, 9, 137519, frame);
        check(have > 0 && sender.asked(1, frame, have) && sender.done(), "... and one that has it says so at once");
        check(!sender.asked(1, frame, airHave(AirStream::Base, 8, 137519, frame)),
              "an answer about another sending is not taken");

        // A guest that has lost its place, saying over and over that it has
        // nothing: no sign of life, or the match would wait for it for ever.
        {
            const auto answer = [&](std::uint32_t first)
            {
                hldata::detail::NetOut out{frame, frame + sizeof(frame)};
                out.u16(kNetMagic);
                out.u8(kNetVersion);
                out.u8(kNetFrameAirAsk);
                out.u8(static_cast<std::uint8_t>(AirStream::Map));
                out.u16(5);
                out.u32(first);
                out.u32(0);
                out.u32(0);
                out.u32(first + kAirWindow);
                return static_cast<int>(out.at - frame);
            };
            sender.begin(AirStream::Map, "doublecross", 1000000, 5, 1u << 1);
            check(sender.asked(1, frame, answer(100)) && sender.wants[1].base == 100, "a guest says how far it is");
            bool left = false;
            for (std::uint32_t n = 0; n <= kAirSilentFrames + 1 && !left; ++n)
            {
                sender.asked(1, frame, answer(0));
                left = sender.frame() != 0;
            }
            check(left && sender.done(), "one that goes back to nothing is left out in the end");
        }

        // A map's picture (HlPreview.hpp): whole in one window, so the
        // window's slots are the file, in order, once it has all come.
        {
            std::vector<std::uint8_t> picture(sizeof(PreviewHeader) + kPreviewWidth * kPreviewHeight * 2);
            for (std::uint8_t &b : picture)
                b = static_cast<std::uint8_t>(rnd());
            check(airPieces(static_cast<std::uint32_t>(picture.size())) <= kAirWindow,
                  "a map's picture is fewer pieces than a window");
            AirSender sends;
            sends.begin(AirStream::Preview, "stalkyard", static_cast<std::uint32_t>(picture.size()), 3, 1u << 4);
            AirReceiver takes;
            std::vector<std::uint8_t> ring(airPieces(static_cast<std::uint32_t>(picture.size())) * kAirPiece);
            takes.want(AirStream::Preview, "", ring.data());
            Air down, up;
            down.lossPercent = up.lossPercent = 20;
            std::uint8_t frame[kNetCmdBytes];
            int ticks = 0;
            for (; ticks < 5000 && !(takes.known && takes.base >= takes.pieces()); ++ticks)
            {
                down.now = up.now = ticks;
                up.send(frame, takes.ask(frame));
                for (const auto &bytes : up.land())
                    sends.asked(4, bytes.data(), static_cast<int>(bytes.size()));
                std::uint32_t index;
                if (sends.next(index))
                    down.send(frame, sends.piece(index, picture.data() + index * kAirPiece, sends.pieceBytes(index), frame));
                for (const auto &bytes : down.land())
                {
                    AirHead head;
                    if (airRead(bytes.data(), static_cast<int>(bytes.size()), head))
                        takes.frame(head);
                }
            }
            check(takes.known && takes.base >= takes.pieces() && std::memcmp(ring.data(), picture.data(), picture.size()) == 0 &&
                      std::strcmp(takes.name, "stalkyard") == 0,
                  "a console without a map's picture is sent the host's, byte for byte, named, through lossy air");
            std::printf("  (a map's picture: %d frames at one piece a frame through 20%% loss)\n", ticks);
        }

        // The real streams, as a guest's loaders read them.
        const char *card = std::getenv("HLDS_AIR_DIR");
        if (card == nullptr)
        {
            std::printf("  (HLDS_AIR_DIR not set: the card's streams were not sent)\n");
            return;
        }
        for (const char *name : {"common.hgs", "match.base.hgs", "stalkyard.hgs"})
        {
            const std::vector<std::uint8_t> bytes = readAll(std::string(card) + "/" + name);
            if (bytes.empty())
            {
                std::printf("  (%s is not in %s)\n", name, card);
                continue;
            }
            AirTestSupply supply;
            supply.stream = &bytes;
            supply.guest.down.lossPercent = supply.guest.up.lossPercent = 10;
            supply.guest.receiver.want(AirStream::Map, "", supply.guest.ring.data());
            supply.sender.begin(AirStream::Map, name, static_cast<std::uint32_t>(bytes.size()), 1, 1u << 1);
            auto cursor = std::make_unique<GuestCursor>(supply);
            bool ok = cursor->start();
            std::uint32_t members = 0;
            std::vector<std::uint8_t> member;
            while (ok && cursor->next())
            {
                member.resize(cursor->member().rawSize);
                ok = cursor->read(member.data(), cursor->member().rawSize) == cursor->member().rawSize;
                ++members;
            }
            char what[96];
            std::snprintf(what, sizeof(what), "%s (%u KB) through the air: every member read as a loader reads it",
                          name, static_cast<unsigned>(bytes.size() / 1024));
            check(ok && cursor->ended() && members == cursor->header().members, what);
            std::printf("  (%s: %d frames at two pieces a frame through 10%% loss, %.1f s at 60)\n", name,
                        supply.ticks, supply.ticks / 60.0);
        }
    }

    void testGlide()
    {
        NetGlide glide;
        NetPlayer heard;
        heard.pos[0] = 400; // 100 units
        netGlide(glide, heard, 17);
        check(glide.pos[0] == 100 * 4096, "someone first heard of is put where they are");
        heard.pos[0] = 440; // ten units on
        for (int n = 0; n < 30; ++n)
            netGlide(glide, heard, 17);
        check(glide.pos[0] > 109 * 4096 && glide.pos[0] <= 110 * 4096, "... and then glides to where they are heard to be");
        heard.pos[0] = 4000;
        netGlide(glide, heard, 17);
        check(glide.pos[0] == 1000 * 4096, "... but is put there if it is far");
        heard.pos[0] = 4020;
        ++heard.spawns;
        netGlide(glide, heard, 17);
        check(glide.pos[0] == 1005 * 4096, "... or if they have been put somewhere, however near");
    }

    // ------------------------------------------------------------ a match

    /// One console: a whole game of its own, and its account of the match.
    struct Console
    {
        std::unique_ptr<Game> game;
        std::unique_ptr<NetMatch> match = std::make_unique<NetMatch>();
        std::vector<NetSpawn> spawns;
        std::uint32_t did = 0; ///< what its player's weapon did this tick, as weaponsTick would say
        int model = -1;
        bool gone = false;

        Game &g() { return *game; }
        PlayerState &player() { return game->player; }
        ActorState &other(int p) { return game->actorStates[match->firstActor + p]; }
    };

    std::unique_ptr<Console> console(const char *dir, const char *map, NetMatch::Role role, int me, const char *name)
    {
        auto c = std::make_unique<Console>();
        c->game = std::make_unique<Game>();
        std::string error;
        if (!c->game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
            return nullptr;
        finish(*c->game, nullptr, dir);
        const LevelView &v = c->game->view;
        for (std::uint32_t e = 0; e < v.entityCount; ++e)
        {
            if (v.entities[e].classId != static_cast<std::uint16_t>(EntityClass::InfoPlayerDeathmatch))
                continue;
            NetSpawn spawn;
            for (int i = 0; i < 3; ++i)
                spawn.pos[i] = v.entities[e].origin[i];
            spawn.yaw = v.entities[e].angles[1];
            c->spawns.push_back(spawn);
        }
        int model = -1;
        for (std::uint32_t m = 0; m < v.modelRefCount; ++m)
            if (std::strcmp(v.modelRefs[m].name, "player") == 0)
                model = static_cast<int>(m);
        netBegin(*c->match, role, static_cast<std::uint16_t>(500 + me), name);
        c->model = model;
        if (role == NetMatch::Role::Host)
            netStart(*c->match, c->game->logic, c->game->player, static_cast<int>(v.actorCount), model, c->spawns.data(),
                 static_cast<int>(c->spawns.size()), 77u * static_cast<unsigned>(me + 1));
        return c;
    }

    /// Consoles (three, unless said) and the air between them.
    struct Room
    {
        int count = 3;
        /// More than four: the host's frames every other tick (NetGame's
        /// rate), each client answering what it has.
        bool halfRate = false;
        /// A client's match begins once the host has said which player it
        /// is; or, as on a console, when it is told to (startAll).
        bool startWhenPlaced = true;
        std::unique_ptr<Console> c[kNetPlayers];
        Air down[kNetPlayers], up[kNetPlayers];
        int now = 0;

        void tick(int ms = 33)
        {
            ++now;
            for (int n = 0; n < count; ++n)
            {
                if (c[n]->gone)
                    continue;
                Game &g = c[n]->g();
                g.player.onGround = true; // (nobody is walking: they stand where they are put)
                logicTick(g.logic, g.player, ms);
                monstersTick(g.logic, g.player, ms);
                // (A client's match begins when the host has said which player it is.)
                if (startWhenPlaced && !c[n]->match->playing && netPlaced(*c[n]->match))
                    netStart(*c[n]->match, g.logic, g.player, static_cast<int>(g.view.actorCount), c[n]->model,
                             c[n]->spawns.data(), static_cast<int>(c[n]->spawns.size()), 77u * static_cast<unsigned>(n + 1));
                netTick(*c[n]->match, g.logic, g.player, static_cast<std::uint16_t>(n * 1000), 0, c[n]->did, false, ms);
                c[n]->did = 0;
                g.heard.count = 0;
                g.player.hurt = 0;
            }
            std::uint8_t frame[kNetCmdBytes];
            const int size = halfRate && (now & 1) ? 0 : netWrite(*c[0]->match, frame);
            for (int n = 1; n < count; ++n)
            {
                down[n].now = up[n].now = now;
                if (c[n]->gone)
                    continue;
                down[n].send(frame, size);
                for (const auto &bytes : down[n].land())
                    netRead(*c[n]->match, bytes.data(), static_cast<int>(bytes.size()));
                std::uint8_t reply[kNetReplyBytes];
                up[n].send(reply, netWrite(*c[n]->match, reply));
                for (const auto &bytes : up[n].land())
                    netRead(*c[0]->match, bytes.data(), static_cast<int>(bytes.size()), n);
            }
        }
        void run(int ticks)
        {
            for (int n = 0; n < ticks; ++n)
                tick();
        }
        /// Every console's match begins (the host's again: with the rules
        /// as they are now).
        void startAll()
        {
            for (int n = 0; n < count; ++n)
            {
                Game &g = c[n]->g();
                netStart(*c[n]->match, g.logic, g.player, static_cast<int>(g.view.actorCount), c[n]->model,
                         c[n]->spawns.data(), static_cast<int>(c[n]->spawns.size()), 77u * static_cast<unsigned>(n + 1));
            }
        }
    };

    /// The lobby: three consoles with no map loaded.
    void testLobby()
    {
        std::unique_ptr<NetMatch> m[3];
        Air down[3], up[3];
        static const char *const kNames[] = {"Host", "Anna", "Bob"};
        static LogicWorld none;
        static PlayerState nobody;
        for (int n = 0; n < 3; ++n)
        {
            m[n] = std::make_unique<NetMatch>();
            netBegin(*m[n], n == 0 ? NetMatch::Role::Host : NetMatch::Role::Client, static_cast<std::uint16_t>(40 + n),
                     kNames[n]);
            if (n > 0)
                netHostJoin(m[0]->host, n);
        }
        auto run = [&](int ticks)
        {
            for (int t = 0; t < ticks; ++t)
            {
                std::uint8_t frame[kNetCmdBytes];
                for (int n = 0; n < 3; ++n)
                    netTick(*m[n], none, nobody, 0, 0, 0, false, 17);
                const int size = netWrite(*m[0], frame);
                for (int n = 1; n < 3; ++n)
                {
                    down[n].now = up[n].now = down[n].now + 1;
                    down[n].send(frame, size);
                    for (const auto &bytes : down[n].land())
                        netRead(*m[n], bytes.data(), static_cast<int>(bytes.size()));
                    std::uint8_t reply[kNetReplyBytes];
                    up[n].send(reply, netWrite(*m[n], reply));
                    for (const auto &bytes : up[n].land())
                        netRead(*m[0], bytes.data(), static_cast<int>(bytes.size()), n);
                }
            }
        };
        run(60);
        bool named = true;
        for (int n = 0; n < 3; ++n)
            for (int p = 0; p < 3; ++p)
                named = named && std::strcmp(m[n]->names[p], kNames[p]) == 0 && netHere(*m[n], p);
        check(named && m[1]->me == 1 && m[2]->me == 2,
              "in the lobby, with no map loaded, everyone learns who is there and what they are called");
        check(!netHere(*m[1], 3), "... and that the fourth place is empty");
        netSay(*m[0], netEvent(NetEventType::Start, 0, 0x1234, 0x5678, 20, 10));
        run(30);
        bool started = true;
        for (int n = 1; n < 3; ++n)
            started = started && m[n]->passedCount == 1 && m[n]->passed[0].type == NetEventType::Start &&
                      m[n]->passed[0].v[0] == 0x1234 && m[n]->passed[0].v[2] == 20;
        check(started, "the host's word to start reaches each of them, once, with which map and what limits");

        // Each says when its map is loaded; the host hears who, and says go.
        for (int n = 0; n < 3; ++n)
            m[n]->passedCount = 0;
        netSay(*m[1], netEvent(NetEventType::Ready));
        run(30);
        bool readyHeard = m[0]->passedCount == 1 && m[0]->passed[0].type == NetEventType::Ready && m[0]->passed[0].from == 1;
        check(readyHeard, "a client's word that it is ready reaches the host, from that client, once");
        m[0]->passedCount = 0;
        netSay(*m[2], netEvent(NetEventType::Ready));
        netSay(*m[0], netEvent(NetEventType::Go));
        run(30);
        bool went = m[0]->passedCount == 1 && m[0]->passed[0].from == 2;
        for (int n = 1; n < 3; ++n)
            went = went && m[n]->passedCount == 1 && m[n]->passed[0].type == NetEventType::Go;
        check(went, "... and the host's word to go reaches every client");
        for (int n = 0; n < 3; ++n)
            m[n]->passedCount = 0;
        netSay(*m[0], netEvent(NetEventType::Cancel));
        run(30);
        bool cancelled = true;
        for (int n = 1; n < 3; ++n)
            cancelled = cancelled && m[n]->passedCount == 1 && m[n]->passed[0].type == NetEventType::Cancel;
        check(cancelled, "... as does its word that the match is not to begin after all");
    }

    /// Version 2's frames and what goes in them: eight players and still
    /// room; the room's card, the bars, a wall's facing, there and back.
    void testWire()
    {
        NetHost host;
        for (int c = 1; c < kNetPlayers; ++c)
            netHostJoin(host, c);
        for (int n = 0; n < 100; ++n)
            host.out.add(numbered(NetEventType::Shot, 0, n));
        NetPlayer mine;
        std::uint8_t frame[kNetCmdBytes];
        const int size = netHostWrite(host, mine, frame);
        const int events = frame[kNetHostFixedBytes - 1];
        check(size > 0 && size <= kNetCmdBytes && events >= 30,
              "a host's frame of eight players fits the radio, with thirty events and more in it");
        NetClient client;
        client.token = 9;
        for (int n = 0; n < 40; ++n)
            client.out.add(numbered(NetEventType::Shot, 0, n));
        std::uint8_t reply[kNetReplyBytes];
        const int replySize = netClientWrite(client, mine, reply);
        check(replySize > 0 && replySize <= kNetReplyBytes, "... and a client's, full, fits its own");

        NetRoomInfo room;
        room.mode = kNetModeTeamplay;
        room.most = 8;
        room.guests = true;
        room.mapHash = nameHash("crossfire");
        std::uint8_t card[kNetRoomBytes];
        netRoomWrite(room, card);
        NetRoomInfo back;
        check(netRoomRead(card, kNetRoomBytes, back) && back.version == kNetVersion && back.mode == kNetModeTeamplay &&
                  back.most == 8 && back.guests && back.mapHash == nameHash("crossfire"),
              "what a host's beacon says of its match reads back as it was");
        check(!netRoomRead(card, 0, back) && back.version == 0, "a host that says nothing is one of an older build");
        check(netBars(60) == 4 && netBars(20) == 2 && netBars(0) == 0 && netBars(-50) == 4 && netBars(-95) == 0 &&
                  netShareBars(60) == 4 && netShareBars(30) == 2 && netShareBars(0) == 0,
              "a signal's strength, and a link's answers, as bars");
        // (How many of the host's frames a client answered.)
        NetHost h2;
        netHostJoin(h2, 1);
        std::uint8_t r[kNetReplyBytes];
        NetClient one;
        one.token = 5;
        for (int n = 0; n < 60; ++n)
        {
            netHostWrite(h2, mine, frame);
            if (n % 2 == 0)
                netHostRead(h2, 1, r, netClientWrite(one, mine, r));
        }
        check(netLinkShare(h2, 1) == 30, "the host counts the frames a client answered, of its last sixty");
        const fx facing[3] = {2896, -2896, 0};
        std::int16_t low, high;
        netPackNormal(facing, low, high);
        fx unpacked[3];
        netUnpackNormal(low, high, unpacked);
        bool near = true;
        for (int i = 0; i < 3; ++i)
            near = near && unpacked[i] - facing[i] < 16 && facing[i] - unpacked[i] < 16;
        check(near, "a wall's facing, packed in two numbers, comes back as it was");
        check(fallDamage(700, true) == 10 && fallDamage(500, true) == 0 && fallDamage(700, false) == fallDamage(700) &&
                  fallDamage(1024, true) == 10,
              "a match's falls cost a flat 10 past the safe speed; a realistic one, as the game alone has them");
    }

    /// A lobby of `count` consoles through bad air: the host's rules, and each
    /// one's profile and spray to all.
    struct Lobby
    {
        int count = 0;
        std::unique_ptr<NetMatch> m[kNetPlayers];
        Air down[kNetPlayers], up[kNetPlayers];
        std::uint8_t sprays[kNetPlayers][kNetSprayBytes] = {};
        LogicWorld none;
        PlayerState nobody;

        void add(int n)
        {
            m[n] = std::make_unique<NetMatch>();
            char name[kNetTextBytes];
            std::snprintf(name, sizeof(name), "P%d", n);
            netBegin(*m[n], n == 0 ? NetMatch::Role::Host : NetMatch::Role::Client, static_cast<std::uint16_t>(70 + n),
                     name);
            for (int i = 0; i < kNetSprayBytes; ++i)
                sprays[n][i] = static_cast<std::uint8_t>(rnd());
            NetProfile profile;
            profile.top = static_cast<std::uint8_t>(10 + n * 20);
            profile.bottom = static_cast<std::uint8_t>(200 - n * 10);
            profile.sprayColour = static_cast<std::uint8_t>(n % 9);
            netSetProfile(*m[n], profile, sprays[n]);
            if (n > 0)
                netHostJoin(m[0]->host, n);
            count = n + 1 > count ? n + 1 : count;
        }
        void run(int ticks)
        {
            for (int t = 0; t < ticks; ++t)
            {
                std::uint8_t frame[kNetCmdBytes];
                for (int n = 0; n < count; ++n)
                    if (m[n])
                        netTick(*m[n], none, nobody, 0, 0, 0, false, 17);
                const int size = netWrite(*m[0], frame);
                for (int n = 1; n < count; ++n)
                {
                    if (!m[n])
                        continue;
                    down[n].now = up[n].now = down[n].now + 1;
                    down[n].send(frame, size);
                    for (const auto &bytes : down[n].land())
                        netRead(*m[n], bytes.data(), static_cast<int>(bytes.size()));
                    std::uint8_t reply[kNetReplyBytes];
                    up[n].send(reply, netWrite(*m[n], reply));
                    for (const auto &bytes : up[n].land())
                        netRead(*m[0], bytes.data(), static_cast<int>(bytes.size()), n);
                }
            }
        }
        /// Whether console `n` knows player `p` as `p` made themself.
        bool knows(int n, int p) const
        {
            const NetProfile &profile = m[n]->profiles[p];
            return profile.known && profile.top == 10 + p * 20 && profile.bottom == 200 - p * 10 &&
                   profile.sprayColour == p % 9 && profile.sprayWhole() &&
                   std::memcmp(profile.spray, sprays[p], kNetSprayBytes) == 0;
        }
    };

    void testLobbyOfEight()
    {
        Lobby lobby;
        lobby.add(0);
        NetRules rules;
        rules.mode = kNetModeTeamplay;
        rules.flags = kRuleWeaponsStay | kRuleAutoaim;
        rules.most = kNetPlayers;
        netSetRules(*lobby.m[0], rules);
        for (int n = 1; n < 6; ++n)
            lobby.add(n);
        lobby.run(400);
        bool all = true;
        for (int n = 0; n < 6; ++n)
            for (int p = 0; p < 6; ++p)
                all = all && lobby.knows(n, p);
        check(all, "six in a lobby through bad air: each has everyone's profile and spray, byte for byte");
        // Two come late.
        lobby.add(6);
        lobby.add(7);
        lobby.run(400);
        bool late = true;
        for (int n = 0; n < kNetPlayers; ++n)
            for (int p = 0; p < kNetPlayers; ++p)
                late = late && lobby.knows(n, p);
        check(late, "two more come late: they have everyone's, and everyone theirs -- eight");
        bool lost = lobby.m[0]->host.out.lost == 0;
        for (int n = 1; n < kNetPlayers; ++n)
            lost = lost && lobby.m[n]->client.out.lost == 0;
        check(lost, "... and nothing was dropped for want of room, the sprays and all");

        // The teams: the host's word, the same on every console, no more
        // than one apart.
        bool same = true;
        int sizes[3] = {0, 0, 0};
        for (int p = 0; p < kNetPlayers; ++p)
        {
            const int team = lobby.m[0]->profiles[p].team;
            sizes[team < 3 ? team : 0]++;
            for (int n = 1; n < kNetPlayers; ++n)
                same = same && lobby.m[n]->profiles[p].team == team;
        }
        check(same && sizes[0] == 0 && sizes[1] == 4 && sizes[2] == 4,
              "teamplay: everyone on a team, four and four, and every console agrees");
        // Asking: to the other team is refused while it would make five and three...
        const int was = lobby.m[3]->profiles[3].team;
        netAskTeam(*lobby.m[3], 3 - was);
        lobby.run(100);
        check(lobby.m[3]->profiles[3].team == was && lobby.m[5]->profiles[3].team == was,
              "a player who asks for the other team is refused while it would make them five to three");
        // ... and given when it would not: one of the other team asks first.
        int other = -1;
        for (int p = 1; p < kNetPlayers && other < 0; ++p)
            if (lobby.m[0]->profiles[p].team != was)
                other = p;
        netAskTeam(*lobby.m[other], was);
        lobby.run(100);
        netAskTeam(*lobby.m[3], 3 - was);
        lobby.run(100);
        check(lobby.m[other]->profiles[other].team == 3 - was || lobby.m[3]->profiles[3].team == 3 - was,
              "... and given when the teams stay no more than one apart");
        bool rulesKnown = true;
        for (int n = 1; n < kNetPlayers; ++n)
            rulesKnown = rulesKnown && lobby.m[n]->rules.mode == kNetModeTeamplay &&
                         lobby.m[n]->rules.flags == (kRuleWeaponsStay | kRuleAutoaim) && lobby.m[n]->rules.most == 8;
        check(rulesKnown, "every console has the host's rules");
        // The rules said before the start arrive before it.
        rules.flags = kRuleFriendlyFire;
        netSetRules(*lobby.m[0], rules);
        netSay(*lobby.m[0], netEvent(NetEventType::Start, 0, 1, 2, 20, 10, 2));
        for (int n = 1; n < kNetPlayers; ++n)
            lobby.m[n]->passedCount = 0;
        bool ordered = true;
        for (int t = 0; t < 60; ++t)
        {
            lobby.run(1);
            for (int n = 1; n < kNetPlayers; ++n)
                if (lobby.m[n]->passedCount > 0 && lobby.m[n]->passed[0].type == NetEventType::Start)
                    ordered = ordered && lobby.m[n]->rules.flags == kRuleFriendlyFire;
        }
        bool startedAll = true;
        for (int n = 1; n < kNetPlayers; ++n)
            startedAll = startedAll && lobby.m[n]->rules.flags == kRuleFriendlyFire;
        check(ordered && startedAll, "the rules said before the start are known before the start is");
    }

    /// The host lets in no more than its rules say.
    void testTooMany()
    {
        Lobby lobby;
        lobby.add(0);
        NetRules rules;
        rules.most = 4;
        netSetRules(*lobby.m[0], rules);
        for (int n = 1; n < 5; ++n)
            lobby.add(n);
        lobby.run(200);
        int refused = 0;
        for (int n = 1; n < 5; ++n)
            refused += lobby.m[n]->refused ? 1 : 0;
        // (Which of them is the fifth is whichever the host heard last:
        // through this air, not always the last to begin.)
        check(refused == 1, "with four to play, the fifth the host hears from is told so, and no other");
        bool four = true;
        for (int n = 0; n < 5; ++n)
            for (int p = 0; p < 5; ++p)
                four = four && (n == p || (n > 0 && lobby.m[n]->refused) || (p > 0 && lobby.m[p]->refused) ||
                                lobby.knows(n, p));
        check(four, "... and the four play on, knowing each other");
    }

    /// The scores of a teamplay match, as the host keeps them.
    void testTeamScore()
    {
        NetMatch match;
        netBegin(match, NetMatch::Role::Host, 0, "Host");
        for (int c = 1; c < 4; ++c)
        {
            netHostJoin(match.host, c);
            match.host.players[c].flags = kNetHere;
        }
        match.rules.mode = kNetModeTeamplay;
        const int teams[4] = {1, 2, 1, 2};
        for (int p = 0; p < 4; ++p)
            match.profiles[p].team = static_cast<std::uint8_t>(teams[p]);
        hldata::detail::netScore(match, 1, 0); // 0 kills 1: across
        check(match.frags[0] == 1 && match.teamFrags[1] == 1 && match.teamFrags[2] == 0, "a kill: a frag, for the team");
        hldata::detail::netScore(match, 2, 0); // 0 kills 2: a teammate
        check(match.frags[0] == 0 && match.teamFrags[1] == 0 && match.deaths[2] == 1,
              "a teammate killed: a frag less for the killer, and for the team");
        hldata::detail::netScore(match, 3, 3); // a death by their own hand
        check(match.frags[3] == -1 && match.teamFrags[2] == -1, "a death by one's own hand: a frag less");
        hldata::detail::netScore(match, 3, 1); // 1 kills 3: a teammate
        check(match.frags[1] == -1 && match.teamFrags[2] == -2 && match.deaths[3] == 2, "... the other team's too");
        check(netTeammates(match, 0, 2) && !netTeammates(match, 0, 1) && !netTeammates(match, 0, 0),
              "teammates cannot hurt each other, without friendly fire");
        match.rules.flags |= kRuleFriendlyFire;
        check(!netTeammates(match, 0, 2), "... and can, with it");
        match.rules.mode = kNetModeDeathmatch;
        match.rules.flags = 0;
        check(!netTeammates(match, 0, 2), "... and in a free-for-all there are no teams");
    }

    void testMatchEnd()
    {
        NetMatch match;
        netBegin(match, NetMatch::Role::Host, 0, "Host");
        netHostJoin(match.host, 1);
        match.host.players[1].flags = kNetHere;
        match.playing = true;

        // The last minute: said once, and not for a limit of one minute.
        match.minuteLimit = 1;
        check(!netLastMinute(match), "a limit of a minute has no last minute to warn of");
        match.minuteLimit = 5;
        match.playedMs = 4 * 60000 - 1;
        check(!netLastMinute(match), "not before the last minute");
        match.playedMs = 4 * 60000;
        check(netLastMinute(match), "the last minute begins");
        match.warned = true;
        check(!netLastMinute(match), "... and is said once");
        match.warned = false;

        // A kill: the weapon goes with it.
        match.mine.weapon = kWeaponShotgun;
        hldata::detail::netNotice(match, NetEventType::Died, 1, 0);
        check(match.noticeCount == 1 && match.notices[0].weapon == kWeaponShotgun, "a kill says what did it");
        hldata::detail::netNotice(match, NetEventType::Died, 1, 1);
        check(match.notices[1].weapon == 0, "... a death by one's own hand does not");
        hldata::detail::netNotice(match, NetEventType::Died, 0);
        check(match.notices[2].weapon == 0, "... nor a death by nobody");
        char line[40];
        netKillLine(line, sizeof(line), "Gordon", "Barney", kWeaponShotgun);
        check(std::strcmp(line, "Gordon killed Barney (shotgun)") == 0, "what a kill says");
        netKillLine(line, sizeof(line), "ABCDEFGHIJK", "abcdefghijk", kWeaponCrossbow);
        check(std::strcmp(line, "ABCDEFGHIJ killed abcdefghij (crossbow)") == 0, "the longest names cut; the words stay");
        netKillLine(line, sizeof(line), "A", "B", kWeaponNone);
        check(std::strcmp(line, "A killed B") == 0, "no weapon known: no weapon said");

        // The end: once, to everyone, and the scores stay as they were.
        hldata::detail::netScore(match, 1, 0);
        check(match.frags[0] == 1 && match.deaths[1] == 1, "a kill counts before the end");
        const int before = match.host.out.count;
        netEndMatch(match);
        check(match.over && match.host.out.count == before + 1, "END MATCH: over, and told to everyone");
        netEndMatch(match);
        check(match.host.out.count == before + 1, "... once");
        hldata::detail::netScore(match, 1, 0);
        hldata::detail::netScore(match, 0, 0);
        check(match.frags[0] == 1 && match.deaths[1] == 1 && match.deaths[0] == 0 && match.host.out.count == before + 1,
              "a death after the end changes nothing");
        check(!netLastMinute(match), "no warning once it is over");

        NetMatch client;
        netBegin(client, NetMatch::Role::Client, 7, "Client");
        client.playing = true;
        netEndMatch(client);
        check(!client.over, "only the host ends a match");
    }

    int far(const fx *a, const fx *b)
    {
        fx d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
        return fxToInt(fxLength(d));
    }

    void testMatch(const char *dir)
    {
        Room room;
        static const char *const kNames[] = {"Host", "Anna", "Bob"};
        for (int n = 0; n < room.count; ++n)
        {
            room.c[n] = console(dir, "crossfire", n == 0 ? NetMatch::Role::Host : NetMatch::Role::Client, n, kNames[n]);
            if (!room.c[n])
            {
                check(false, "crossfire loads");
                return;
            }
            if (n > 0)
                netHostJoin(room.c[0]->match->host, n);
        }
        check(room.c[0]->spawns.size() >= 4 && room.c[0]->model >= 0,
              "crossfire has its places to come in at, and the players' model");
        room.run(90);
        check(room.c[1]->match->me == 1 && room.c[2]->match->me == 2 && room.c[1]->match->playing,
              "each client is told by the host which player it is");

        // ---- everyone is seen by everyone, where they are
        bool seen = true, named = true, apart = true;
        for (int n = 0; n < room.count; ++n)
        {
            for (int p = 0; p < room.count; ++p)
            {
                if (p == n)
                    continue;
                const ActorState &actor = room.c[n]->other(p);
                // (Their middle, where the players' model has its origin.)
                const fx *middle = room.c[p]->player().origin;
                seen = seen && actor.active && actor.kind == static_cast<std::uint8_t>(ActorKind::Player) &&
                       far(actor.pos, middle) <= 2 && (actor.flags & kActorSolid) && actor.clip >= 0 &&
                       room.c[n]->g().actorBoxes[room.c[n]->match->firstActor + p].active;
                named = named && std::strcmp(room.c[n]->match->names[p], kNames[p]) == 0;
                apart = apart && far(room.c[n]->player().origin, room.c[p]->player().origin) > 100;
            }
        }
        check(seen, "each console sees the other two where they are, solid, in a clip of the players' model");
        check(named, "... and knows what they are called");
        check(apart, "they came in at different places");
        bool empty = true;
        for (int i = 0; i < room.c[1]->g().actors.count; ++i)
        {
            const ActorState &actor = room.c[1]->g().actorStates[i];
            empty = empty && (!actor.active || actor.kind == static_cast<std::uint8_t>(ActorKind::Item) ||
                              actor.kind == static_cast<std::uint8_t>(ActorKind::Furniture) ||
                              actor.kind == static_cast<std::uint8_t>(ActorKind::Player));
        }
        check(empty, "nobody of the map's own is in the match");

        // ---- a shot at the middle of one, where their body is drawn, hits
        {
            Console &shooter = *room.c[0];
            const int target = 1;
            // (Put where the host has a clear line to them, a hundred units off.)
            for (int k = 0; k < 8; ++k)
            {
                fx sine, cosine;
                fxSinCos(static_cast<std::uint16_t>(k * 8192), sine, cosine);
                const fx to[3] = {shooter.player().origin[0] + cosine * 100, shooter.player().origin[1] + sine * 100,
                                  shooter.player().origin[2]};
                if (!lineClear(shooter.g().logic.world[0], shooter.player().origin, to))
                    continue;
                for (int i = 0; i < 3; ++i)
                    room.c[target]->player().origin[i] = to[i];
                break;
            }
            room.run(30);
            const fx eye[3] = {shooter.player().origin[0], shooter.player().origin[1],
                               shooter.player().origin[2] + fxFromInt(28)};
            const ActorState &seenAt = shooter.other(target);
            fx dir[3] = {seenAt.pos[0] - eye[0], seenAt.pos[1] - eye[1], seenAt.pos[2] - eye[2]};
            const fx length = fxLength(dir);
            for (int i = 0; i < 3 && length > 0; ++i)
                dir[i] = static_cast<fx>((static_cast<std::int64_t>(dir[i]) << kFxShift) / length);
            if (lineClear(shooter.g().logic.world[0], eye, seenAt.pos))
            {
                const int health = room.c[target]->player().health;
                fireLine(shooter.g().logic, shooter.player(), eye, dir, 8192, 8, true, kMarkShot, nullptr);
                room.run(40);
                check(room.c[target]->player().health < health,
                      "a shot at a player's middle, where their body is drawn, hits them");
                room.c[target]->player().health = 100;
            }
            else
                std::printf("test_net: (no clear line from the host to a player: the shot at a middle not tried)\n");
        }

        // ---- a hit: claimed by the shooter, taken by the victim, once
        Console &anna = *room.c[1], &bob = *room.c[2], &host = *room.c[0];
        PlayerState expected = bob.player();
        playerHurt(expected, 30);
        monsterHurt(anna.g().logic, anna.match->firstActor + 2, 30, true, kHitBody);
        room.run(40);
        check(bob.player().health == expected.health && bob.player().health < 100,
              "Anna's shot on Bob, as her console sees him, hurts Bob on his own: once, and as much");
        check(host.player().health == 100 && anna.player().health == 100, "... and nobody else");
        // A blast near him, from her grenade.
        const int before = bob.player().health;
        fx blast[3] = {bob.player().origin[0] + fxFromInt(40), bob.player().origin[1], bob.player().origin[2]};
        radiusDamage(anna.g().logic, anna.player(), blast, 60, 200, -1);
        room.run(40);
        check(bob.player().health < before && bob.player().health > 0, "a blast of hers near him hurts him too");
        // The head: three times a bullet.
        expected = bob.player();
        playerHurt(expected, 10 * 3);
        monsterHurt(anna.g().logic, anna.match->firstActor + 2, 10, true, kHitHead);
        room.run(40);
        check(bob.player().health == expected.health, "a bullet in the head is three");

        // ---- a death: scored by the host, known to all; and back after a while
        const std::uint8_t spawnsBefore = bob.match->spawns;
        monsterHurt(anna.g().logic, anna.match->firstActor + 2, 500, true, kHitBody);
        room.run(45);
        check(bob.player().health <= 0, "enough of it kills him");
        bool scored = true;
        for (int n = 0; n < room.count; ++n)
            scored = scored && room.c[n]->match->frags[1] == 1 && room.c[n]->match->deaths[2] == 1 &&
                     room.c[n]->match->frags[2] == 0 && room.c[n]->match->frags[0] == 0;
        check(scored, "... a frag for Anna and a death for Bob, on every console");
        check(!(anna.other(2).flags & kActorSolid) && anna.match->pose[2] == kPoseDead,
              "... and he lies there, in nobody's way");
        // She goes on firing at him where he fell: none of it is his next life's.
        for (int n = 0; n < 6; ++n)
        {
            fx from[3] = {anna.player().origin[0], anna.player().origin[1], anna.player().origin[2]};
            const ActorState &body = anna.other(2);
            fx at[3] = {body.pos[0], body.pos[1], body.pos[2]};
            fx dir[3] = {at[0] - from[0], at[1] - from[1], at[2] - from[2]};
            const fx length = fxLength(dir);
            for (int i = 0; i < 3 && length > 0; ++i)
                dir[i] = static_cast<fx>((static_cast<std::int64_t>(dir[i]) << kFxShift) / length);
            fireLine(anna.g().logic, anna.player(), from, dir, 8192, 8, true, kMarkShot, nullptr);
            room.run(10);
        }

        bool told = false;
        for (int k = 0; k < host.match->noticeCount; ++k)
            told = told || (host.match->notices[k].type == NetEventType::Died && host.match->notices[k].who == 2 &&
                            host.match->notices[k].other == 1);
        check(told, "... and the host's screen can say who killed whom");
        room.run(120);
        check(bob.player().health == 100 && bob.match->spawns == static_cast<std::uint8_t>(spawnsBefore + 1) &&
                  (bob.player().inv.have & (1u << kWeaponGlock)) && bob.match->respawned,
              "three seconds on he is back, whole, with a pistol");
        check((anna.other(2).flags & kActorSolid) && anna.match->pose[2] != kPoseDead, "... and is seen to be");
        // A claim on his last life that only arrives now: not his to take.
        netSay(*anna.match, netEvent(NetEventType::Hit, 2, 40, spawnsBefore));
        room.run(30);
        check(bob.player().health == 100, "nothing his killer fired at his body, or at his last life, hurts him back here");
        // Falling to his death is nobody's frag.
        room.run(200); // (long past any blame)
        bob.player().health = 0;
        room.run(45);
        bool own = true;
        for (int n = 0; n < room.count; ++n)
            own = own && room.c[n]->match->frags[2] == -1 && room.c[n]->match->deaths[2] == 2 &&
                  room.c[n]->match->frags[1] == 1;
        check(own, "a death by nobody else costs him a frag, and gives nobody one");
        room.run(120);

        // ---- something to pick up, wanted by two at once
        int item = -1;
        for (int i = 0; i < host.g().actors.count && item < 0; ++i)
        {
            const ActorState &actor = host.g().actorStates[i];
            if (actor.active && actor.kind == static_cast<std::uint8_t>(ActorKind::Item) &&
                actor.item == static_cast<std::uint8_t>(ItemKind::Shotgun))
                item = i;
        }
        check(item >= 0, "crossfire has a shotgun lying about");
        if (item >= 0)
        {
            for (Console *who : {&anna, &bob})
            {
                for (int i = 0; i < 3; ++i)
                    who->player().origin[i] = host.g().actorStates[item].pos[i];
                who->player().origin[2] += fxFromInt(36);
            }
            room.run(40);
            const bool hers = (anna.player().inv.have & (1u << kWeaponShotgun)) != 0;
            const bool his = (bob.player().inv.have & (1u << kWeaponShotgun)) != 0;
            check(hers != his, "two reach for one shotgun: one of them has it, and only one");
            bool gone = true;
            for (int n = 0; n < room.count; ++n)
                gone = gone && !room.c[n]->g().actorStates[item].active;
            check(gone, "... and it is gone from where it lay, on every console");
            // (They stand on it still: it is theirs again the moment it is back. Away.)
            anna.player().origin[0] += fxFromInt(200);
            bob.player().origin[0] -= fxFromInt(200);
            room.run(kNetWeaponBackMs / 33 - 60);
            bool still = true;
            for (int n = 0; n < room.count; ++n)
                still = still && !room.c[n]->g().actorStates[item].active;
            check(still, "... for twenty seconds");
            room.run(120);
            bool back = true;
            for (int n = 0; n < room.count; ++n)
                back = back && room.c[n]->g().actorStates[item].active;
            check(back, "... and then it is back, for all of them");
        }

        // ---- the map's machinery: used by one, moves for all
        int door = -1, trigger = -1;
        for (int i = 0; i < host.g().logic.recordCount; ++i)
        {
            const LogicRecord &record = host.g().logic.records[i];
            const auto kind = static_cast<LogicKind>(record.kind);
            if (door < 0 && (kind == LogicKind::Door || kind == LogicKind::Button) && record.brush != kNoBrush &&
                host.g().logic.states[i].move == MoveState::AtBottom && !host.g().logic.states[i].dead)
                door = i;
            if (trigger < 0 && kind == LogicKind::TriggerMultiple && !(record.flags & kLogicNoPlayer) &&
                host.g().logic.states[i].timer < 0 && !host.g().logic.states[i].dead)
                trigger = i;
        }
        check(door >= 0, "crossfire has a door or a button");
        if (door >= 0)
        {
            logicUse(bob.g().logic, bob.player(), door, -1, kUseSet);
            netUsed(*bob.match, door);
            const bool went = bob.g().logic.states[door].move != MoveState::AtBottom;
            room.run(20);
            bool all = went;
            for (int n = 0; n < room.count; ++n)
                all = all && (room.c[n]->g().logic.states[door].move != MoveState::AtBottom ||
                              room.c[n]->g().logic.states[door].timer >= 0);
            check(!went || all, "what Bob uses goes on every console");
        }
        if (trigger >= 0)
        {
            const LogicRecord &record = host.g().logic.records[trigger];
            for (int i = 0; i < 3; ++i)
                anna.player().origin[i] = fxFromInt((record.mins[i] + record.maxs[i]) / 2);
            room.run(12);
            bool all = true;
            for (int n = 0; n < room.count; ++n)
                all = all && (room.c[n]->g().logic.states[trigger].timer >= 0 || room.c[n]->g().logic.states[trigger].dead);
            check(all, "a trigger Anna walks into fires on every console, by where each sees her");
        }

        // ---- someone leaves
        bob.gone = true;
        netHostLeave(host.match->host, 2);
        room.run(60);
        check(!host.other(2).active && !anna.other(2).active && !host.g().actorBoxes[host.match->firstActor + 2].active,
              "Bob leaves: he is gone from the others' maps");
        const int health = anna.player().health;
        monsterHurt(host.g().logic, host.match->firstActor + 1, 20, true, kHitBody);
        room.run(40);
        check(anna.player().health < health, "... and the match goes on between those who stay");
        check(host.match->host.out.lost == 0 && anna.match->client.out.lost == 0, "nothing was ever dropped for want of room");

        // ---- the end
        host.match->fragLimit = 1; // (Anna has one already)
        room.run(40);
        check(host.match->over && anna.match->over, "the frag limit reached, the match is over for everyone");
    }

    /// A teamplay match on crossfire, four of them: the rules said before it
    /// begins, and played.
    void testTeamMatch(const char *dir)
    {
        Room room;
        room.count = 4;
        room.startWhenPlaced = false;
        static const char *const kNames[] = {"Host", "Anna", "Bob", "Cleo"};
        std::uint8_t spray[kNetSprayBytes];
        for (int i = 0; i < kNetSprayBytes; ++i)
            spray[i] = static_cast<std::uint8_t>(i * 7);
        for (int n = 0; n < room.count; ++n)
        {
            room.c[n] = console(dir, "crossfire", n == 0 ? NetMatch::Role::Host : NetMatch::Role::Client, n, kNames[n]);
            if (!room.c[n])
            {
                check(false, "crossfire loads");
                return;
            }
            NetProfile profile;
            profile.top = static_cast<std::uint8_t>(n * 50);
            netSetProfile(*room.c[n]->match, profile, spray);
            if (n == 0)
            {
                NetRules rules;
                rules.mode = kNetModeTeamplay;
                rules.flags = kRuleWeaponsStay | kRuleAutoaim;
                rules.most = 8;
                netSetRules(*room.c[0]->match, rules);
            }
            else
                netHostJoin(room.c[0]->match->host, n);
        }
        room.run(150); // (the lobby: everyone, their profiles, the teams)
        room.startAll();
        room.run(60);
        Console &host = *room.c[0];
        bool rules = true;
        for (int n = 0; n < room.count; ++n)
            rules = rules && room.c[n]->match->playing && room.c[n]->g().logic.weaponsStay.value &&
                    room.c[n]->g().logic.flatFalls.value && room.c[n]->g().logic.lampBanned.value &&
                    room.c[n]->g().logic.matchAutoaim.value == 1;
        check(rules, "every console plays by the host's rules: weapons stay, a flat fall, no lamp, autoaim");
        // Who is with whom.
        int shooter = 1, mate = -1, foe = -1;
        const int team = host.match->profiles[shooter].team;
        for (int p = 0; p < room.count; ++p)
        {
            if (p == shooter)
                continue;
            if (host.match->profiles[p].team == team && mate < 0)
                mate = p;
            else if (host.match->profiles[p].team != team && foe < 0)
                foe = p;
        }
        check(team != 0 && mate >= 0 && foe >= 0, "four on two teams: each has a teammate and two to fight");
        if (mate < 0 || foe < 0)
            return;
        Console &s = *room.c[shooter], &m = *room.c[mate], &f = *room.c[foe];
        monsterHurt(s.g().logic, s.match->firstActor + mate, 30, true, kHitBody);
        room.run(40);
        check(m.player().health == 100, "no friendly fire: a teammate's shot does not hurt");
        monsterHurt(s.g().logic, s.match->firstActor + foe, 30, true, kHitBody);
        room.run(40);
        check(f.player().health < 100, "... one of the other team's does");

        // ---- weapons stay
        int item = -1;
        for (int i = 0; i < host.g().actors.count && item < 0; ++i)
        {
            const ActorState &actor = host.g().actorStates[i];
            if (actor.active && actor.kind == static_cast<std::uint8_t>(ActorKind::Item) &&
                actor.item == static_cast<std::uint8_t>(ItemKind::Shotgun))
                item = i;
        }
        if (item >= 0)
        {
            for (Console *who : {&s, &f})
            {
                for (int i = 0; i < 3; ++i)
                    who->player().origin[i] = host.g().actorStates[item].pos[i];
                who->player().origin[2] += fxFromInt(36);
            }
            room.run(60);
            const bool both = (s.player().inv.have & (1u << kWeaponShotgun)) && (f.player().inv.have & (1u << kWeaponShotgun));
            bool there = true;
            for (int n = 0; n < room.count; ++n)
                there = there && room.c[n]->g().actorStates[item].active;
            check(both && there, "weapons stay: two take one shotgun, both have it, and it is still there for all");
            const int ammo = s.player().inv.ammoBuck;
            room.run(90); // (still standing on it)
            check(s.player().inv.ammoBuck == ammo, "... and who has it does not take it again");
            s.player().origin[0] += fxFromInt(200);
            f.player().origin[0] -= fxFromInt(200);
        }

        // ---- a spray
        const fx eye[3] = {host.player().origin[0], host.player().origin[1], host.player().origin[2] + fxFromInt(28)};
        bool sprayed = false, missed = false, tested = false;
        for (int k = 0; k < 16 && !sprayed; ++k)
        {
            fx sine, cosine;
            fxSinCos(static_cast<std::uint16_t>(k * 4096), sine, cosine);
            const fx forward[3] = {cosine, sine, 0};
            sprayed = netSpray(*host.match, host.g().logic, host.player(), eye, forward);
        }
        if (!sprayed)
        {
            // (Nothing near that way: the floor, under their feet.)
            const fx down[3] = {0, 0, -kFxOne};
            sprayed = netSpray(*host.match, host.g().logic, host.player(), eye, down);
        }
        check(sprayed, "the host sprays on the wall it faces");
        room.run(40);
        bool seen = true;
        for (int n = 1; n < room.count; ++n)
            seen = seen && room.c[n]->match->sprays[0].active &&
                   far(room.c[n]->match->sprays[0].at, host.match->sprays[0].at) <= 1;
        check(seen, "... and every console has it on that wall");
        fx ahead[3] = {kFxOne, 0, 0};
        check(!netSpray(*host.match, host.g().logic, host.player(), eye, ahead), "... and not again so soon");
        room.run(kNetSprayWaitMs / 33 + 10);
        // Into the air: a way with nothing within reach, if there is one here.
        for (int k = 0; k < 16 && !tested; ++k)
        {
            fx sine, cosine;
            fxSinCos(static_cast<std::uint16_t>(k * 4096), sine, cosine);
            const fx forward[3] = {cosine, sine, 0};
            fx to[3];
            for (int i = 0; i < 3; ++i)
                to[i] = eye[i] + forward[i] * kNetSprayReach;
            if (hldata::detail::pointTrace(*host.g().logic.world, eye, to).hit)
                continue;
            tested = true;
            missed = !netSpray(*host.match, host.g().logic, host.player(), eye, forward);
        }
        check(!tested || missed, "nothing is sprayed on the air");
        check(host.match->host.out.lost == 0, "nothing was dropped for want of room");
    }

    /// Six on crossfire, the host's frames every other tick: they still see
    /// each other, and a death is scored on every console.
    void testHalfRate(const char *dir)
    {
        Room room;
        room.count = 6;
        room.halfRate = true;
        for (int n = 0; n < room.count; ++n)
        {
            char name[kNetTextBytes];
            std::snprintf(name, sizeof(name), "P%d", n);
            room.c[n] = console(dir, "crossfire", n == 0 ? NetMatch::Role::Host : NetMatch::Role::Client, n, name);
            if (!room.c[n])
            {
                check(false, "crossfire loads");
                return;
            }
            if (n > 0)
                netHostJoin(room.c[0]->match->host, n);
        }
        room.run(150);
        bool seen = true;
        for (int n = 0; n < room.count; ++n)
            for (int p = 0; p < room.count; ++p)
                seen = seen && (p == n || (room.c[n]->other(p).active &&
                                           far(room.c[n]->other(p).pos, room.c[p]->player().origin) < 60));
        check(seen, "six at half the rate: each sees the other five where they are");
        monsterHurt(room.c[1]->g().logic, room.c[1]->match->firstActor + 5, 500, true, kHitBody);
        room.run(60);
        bool scored = true;
        for (int n = 0; n < room.count; ++n)
            scored = scored && room.c[n]->match->frags[1] == 1 && room.c[n]->match->deaths[5] == 1;
        check(scored, "... and a death is scored on every console");
    }
} // namespace

int main()
{
    testPacking();
    testEvents();
    testFile();
    testAir();
    testGlide();
    testLobby();
    testWire();
    testLobbyOfEight();
    testTooMany();
    testTeamScore();
    testMatchEnd();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr)
        std::printf("test_net: HLDS_VALVE_DIR not set, no match was played\n");
    else
    {
        testMatch(dir);
        testTeamMatch(dir);
        testHalfRate(dir);
    }
    if (g_failures == 0)
        std::printf("test_net: ok\n");
    return g_failures == 0 ? 0 : 1;
}
