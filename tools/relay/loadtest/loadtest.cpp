// hlds-relay-loadtest: rooms of make-believe consoles playing through a
// relay over real UDP, as the game does, to see what the relay keeps up with.
//
//   hlds-relay-loadtest [--relay HOST:PORT | --spawn PATH] [--rooms N] [--players P]
//                       [--seconds S] [--map] [--flood PPS] [--relay-pid PID]
//                       [--most-loss PERCENT]
//
// Each console has an address of its own on the loopback (127.1.<room>.<n>;
// on Linux all of 127/8 is this machine's), so that the relay's limits for an
// address do not get in the way: it comes in (Hello, Enter), the first of a
// room opens it and lets the others in, and from then on, each 60th of a
// second, the host sends a frame of 720 bytes and each client one of 220 --
// and with --map, the host four pieces of 620 more, as it does sending a map.
// A ping each second. Each frame says when it was sent, and its number: what
// arrives says how late, and what never did.
//
// --flood sends that many datagrams a second more from strangers (Pings in
// sessions made up, Hellos, noise), from 127.2.x.x, to see that the consoles
// play on through it.
//
// --spawn starts the relay itself, on a port of its own, and stops it after
// (for ctest); else --relay says where one runs (127.0.0.1:27500 if not given),
// and --relay-pid lets its CPU be read from /proc.
//
// It ends with what came and went, the loss, how late (half, 99 in 100, the
// most), and the relay's CPU; and fails (exit 1) if more was lost than
// --most-loss (5% if not given).

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "HlRelay.hpp"

using namespace hldata;

namespace
{
    using Clock = std::chrono::steady_clock;
    std::uint64_t nowNs()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
    }

    std::uint64_t g_rnd = 0x2545F4914F6CDD1Dull;
    std::uint32_t rnd()
    {
        g_rnd ^= g_rnd << 13;
        g_rnd ^= g_rnd >> 7;
        g_rnd ^= g_rnd << 17;
        return static_cast<std::uint32_t>(g_rnd >> 16);
    }

    struct Console
    {
        int fd = -1;
        int room = 0, n = 0; ///< n 0: the host
        std::uint32_t nonce = 0, session = 0;
        std::uint8_t cookie[kRelayCookieBytes] = {};
        enum class Stage
        {
            Hello,
            Enter,
            In,
        } stage = Stage::Hello;
        std::uint32_t roomId = 0; ///< (the host's, once opened)
        int slot = -1;            ///< (a client's, once let in)
        bool asked = false;
        std::uint32_t seq = 0;
        // What it heard of the others' frames.
        std::uint32_t lastSeq[kNetPlayers] = {};
        std::uint64_t got = 0, gaps = 0;
        std::uint64_t sentAt = 0;
    };

    struct Totals
    {
        std::uint64_t sent = 0, sentBytes = 0, got = 0, gotBytes = 0, expected = 0;
        std::vector<std::uint32_t> lateUs;
    };

    sockaddr_in g_relay = {};

    void sendTo(int fd, const std::uint8_t *bytes, int size, Totals &t)
    {
        if (::sendto(fd, bytes, static_cast<std::size_t>(size), MSG_DONTWAIT, reinterpret_cast<sockaddr *>(&g_relay),
                     sizeof(g_relay)) == size)
        {
            ++t.sent;
            t.sentBytes += static_cast<std::uint64_t>(size);
        }
    }

    int bindTo(std::uint32_t ip)
    {
        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0)
            return -1;
        sockaddr_in at = {};
        at.sin_family = AF_INET;
        at.sin_addr.s_addr = htonl(ip);
        at.sin_port = 0;
        if (::bind(fd, reinterpret_cast<sockaddr *>(&at), sizeof(at)) != 0)
        {
            ::close(fd);
            return -1;
        }
        const int room = 1 << 20;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &room, sizeof(room));
        return fd;
    }

    /// A game frame: its sender's place, number and when it was sent; the
    /// rest filler, to `size`.
    int frameWrite(std::uint8_t *frame, int size, int who, std::uint32_t seq)
    {
        std::memset(frame, 0xA5, static_cast<std::size_t>(size));
        frame[0] = static_cast<std::uint8_t>(who);
        relayPut32(frame + 1, seq);
        const std::uint64_t t = nowNs();
        relayPut32(frame + 5, static_cast<std::uint32_t>(t));
        relayPut32(frame + 9, static_cast<std::uint32_t>(t >> 32));
        return size;
    }

    double cpuSeconds(int pid)
    {
        if (pid <= 0)
            return -1;
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        std::FILE *f = std::fopen(path, "r");
        if (f == nullptr)
            return -1;
        char line[1024] = {};
        const bool read = std::fgets(line, sizeof(line), f) != nullptr;
        std::fclose(f);
        if (!read)
            return -1;
        // (After the name in brackets: fields 3 on; utime and stime are 14, 15.)
        const char *at = std::strrchr(line, ')');
        if (at == nullptr)
            return -1;
        unsigned long utime = 0, stime = 0;
        if (std::sscanf(at + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &utime, &stime) != 2)
            return -1;
        return static_cast<double>(utime + stime) / static_cast<double>(::sysconf(_SC_CLK_TCK));
    }

    void usage()
    {
        std::fprintf(stderr,
                     "usage: hlds-relay-loadtest [--relay HOST:PORT | --spawn PATH] [--rooms N] [--players P]\n"
                     "                           [--seconds S] [--map] [--flood PPS] [--relay-pid PID]\n"
                     "                           [--most-loss PERCENT]\n");
    }
} // namespace

int main(int argc, char **argv)
{
    std::string relayAt = "127.0.0.1:27500", spawn;
    int rooms = 4, players = 8, seconds = 30, flood = 0, relayPid = 0;
    double mostLoss = 5.0;
    bool map = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        const char *v = nullptr;
        if (arg == "--relay" && (v = next()))
            relayAt = v;
        else if (arg == "--spawn" && (v = next()))
            spawn = v;
        else if (arg == "--rooms" && (v = next()))
            rooms = std::atoi(v);
        else if (arg == "--players" && (v = next()))
            players = std::atoi(v);
        else if (arg == "--seconds" && (v = next()))
            seconds = std::atoi(v);
        else if (arg == "--flood" && (v = next()))
            flood = std::atoi(v);
        else if (arg == "--relay-pid" && (v = next()))
            relayPid = std::atoi(v);
        else if (arg == "--most-loss" && (v = next()))
            mostLoss = std::atof(v);
        else if (arg == "--map")
            map = true;
        else
        {
            usage();
            return 2;
        }
    }
    if (rooms < 1 || rooms > 250 || players < 2 || players > kNetPlayers || seconds < 1)
    {
        usage();
        return 2;
    }

    // The relay: started here, or where it is said to be.
    int port = 27500;
    std::string host = "127.0.0.1";
    if (!spawn.empty())
    {
        port = 30000 + static_cast<int>(::getpid() % 20000);
        relayPid = ::fork();
        if (relayPid == 0)
        {
            const std::string p = std::to_string(port);
            ::execl(spawn.c_str(), spawn.c_str(), "--port", p.c_str(), "--bind", "127.0.0.1", "--quiet",
                    static_cast<char *>(nullptr));
            std::_Exit(127);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    else
    {
        const std::size_t colon = relayAt.rfind(':');
        host = relayAt.substr(0, colon);
        if (colon != std::string::npos)
            port = std::atoi(relayAt.c_str() + colon + 1);
    }
    g_relay.sin_family = AF_INET;
    g_relay.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &g_relay.sin_addr) != 1)
    {
        std::fprintf(stderr, "not an IPv4 address: %s\n", host.c_str());
        return 2;
    }

    std::vector<Console> consoles(static_cast<std::size_t>(rooms * players));
    for (int r = 0; r < rooms; ++r)
    {
        for (int n = 0; n < players; ++n)
        {
            Console &c = consoles[static_cast<std::size_t>(r * players + n)];
            c.room = r;
            c.n = n;
            c.nonce = rnd() | 1u;
            c.fd = bindTo(0x7F010000u | static_cast<std::uint32_t>(r) << 8 | static_cast<std::uint32_t>(n + 2));
            if (c.fd < 0)
            {
                std::perror("bind 127.1.x.x");
                return 1;
            }
        }
    }
    std::vector<int> strangers;
    for (int k = 0; flood > 0 && k < 64; ++k)
        strangers.push_back(bindTo(0x7F020000u | static_cast<std::uint32_t>(rnd() & 0xFFFF)));

    std::vector<pollfd> fds;
    for (const Console &c : consoles)
        fds.push_back({c.fd, POLLIN, 0});

    Totals t;
    const double cpuBefore = cpuSeconds(relayPid);
    const std::uint64_t start = nowNs();
    const std::uint64_t frameNs = 1000000000ull / 60;
    const std::uint64_t setupNs = 3000000000ull; // (coming in and joining: not counted)
    std::uint64_t nextFrame = start, lastPing = 0, floodDue = start;
    std::uint64_t frames = 0;
    std::uint8_t bytes[2048], frame[kNetCmdBytes];
    bool counting = false;
    std::uint64_t countFrom = 0;

    while (nowNs() - start < setupNs + static_cast<std::uint64_t>(seconds) * 1000000000ull)
    {
        const std::uint64_t now = nowNs();
        if (!counting && now - start >= setupNs)
        {
            counting = true;
            countFrom = now;
            t = Totals();
            for (Console &c : consoles)
            {
                c.got = c.gaps = 0;
                std::memset(c.lastSeq, 0, sizeof(c.lastSeq));
            }
        }
        if (now >= nextFrame)
        {
            nextFrame += frameNs;
            ++frames;
            const bool ping = now - lastPing >= 1000000000ull;
            if (ping)
                lastPing = now;
            for (Console &c : consoles)
            {
                if (c.stage == Console::Stage::Hello && frames % 30 == 1)
                    sendTo(c.fd, bytes, relayHelloWrite(bytes, c.nonce), t);
                else if (c.stage == Console::Stage::Enter && frames % 30 == 1)
                    sendTo(c.fd, bytes, relayEnterWrite(bytes, c.nonce, c.cookie, nullptr), t);
                if (c.stage != Console::Stage::In)
                    continue;
                if (ping)
                    sendTo(c.fd, bytes, relaySmallWrite(bytes, RelayKind::Ping, c.session), t);
                if (c.n == 0)
                {
                    if (c.roomId == 0 && frames % 30 == 1)
                    {
                        RelayOpen open;
                        std::snprintf(open.name, sizeof(open.name), "load %d", c.room);
                        open.most = static_cast<std::uint8_t>(players);
                        sendTo(c.fd, bytes, relayOpenWrite(bytes, c.session, open), t);
                    }
                    if (c.roomId == 0)
                        continue;
                    sendTo(c.fd, bytes, relayDataWrite(bytes, c.session, 0, frame, frameWrite(frame, kNetCmdBytes, 0, ++c.seq)), t);
                    for (int k = 0; map && k < 4; ++k)
                        sendTo(c.fd, bytes, relayDataWrite(bytes, c.session, 0, frame, frameWrite(frame, 620, 0, ++c.seq)), t);
                }
                else
                {
                    const Console &h = consoles[static_cast<std::size_t>(c.room * players)];
                    if (c.slot < 0)
                    {
                        if (h.roomId != 0 && frames % 30 == 1)
                            sendTo(c.fd, bytes, relayJoinWrite(bytes, c.session, h.roomId, "load"), t);
                        continue;
                    }
                    sendTo(c.fd, bytes,
                           relayDataWrite(bytes, c.session, c.slot, frame, frameWrite(frame, kNetReplyBytes, c.slot, ++c.seq)), t);
                }
            }
        }
        // Strangers.
        while (flood > 0 && now >= floodDue)
        {
            floodDue += 1000000000ull / static_cast<std::uint64_t>(flood);
            const int fd = strangers[rnd() % strangers.size()];
            int size = 0;
            switch (rnd() % 3)
            {
            case 0: size = relaySmallWrite(bytes, RelayKind::Ping, rnd()); break;
            case 1: size = relayHelloWrite(bytes, rnd()); break;
            default:
                size = static_cast<int>(rnd() % 700);
                for (int i = 0; i < size; ++i)
                    bytes[i] = static_cast<std::uint8_t>(rnd());
                break;
            }
            ::sendto(fd, bytes, static_cast<std::size_t>(size), MSG_DONTWAIT, reinterpret_cast<sockaddr *>(&g_relay),
                     sizeof(g_relay));
        }

        // What has come.
        // (The clock read once: read twice, it may pass the frame between,
        // and the wait come out as all but for ever.)
        const std::uint64_t then = nowNs();
        const int wait = nextFrame > then ? static_cast<int>(std::min<std::uint64_t>((nextFrame - then) / 1000000ull, 50)) : 0;
        ::poll(fds.data(), fds.size(), wait);
        for (std::size_t i = 0; i < fds.size(); ++i)
        {
            if (!(fds[i].revents & POLLIN))
                continue;
            Console &c = consoles[i];
            for (;;)
            {
                const ssize_t got = ::recv(c.fd, bytes, sizeof(bytes), MSG_DONTWAIT);
                if (got <= 0)
                    break;
                const int size = static_cast<int>(got);
                RelayHead head;
                if (!relayHeadRead(bytes, size, head))
                    continue;
                if (head.kind == RelayKind::Challenge && c.stage == Console::Stage::Hello && head.session == c.nonce &&
                    relayChallengeRead(bytes, size, c.cookie))
                {
                    c.stage = Console::Stage::Enter;
                    sendTo(c.fd, bytes, relayEnterWrite(bytes, c.nonce, c.cookie, nullptr), t);
                }
                else if (head.kind == RelayKind::Welcome && c.stage == Console::Stage::Enter && head.session == c.nonce)
                {
                    std::uint8_t token[kRelayTokenBytes];
                    if (relayWelcomeRead(bytes, size, c.session, token))
                        c.stage = Console::Stage::In;
                }
                else if (head.session != c.session)
                    continue;
                else if (head.kind == RelayKind::Opened)
                    relayRoomIdRead(bytes, size, c.roomId);
                else if (head.kind == RelayKind::Knock)
                {
                    int ticket = 0;
                    char name[kNetTextBytes];
                    if (relayKnockRead(bytes, size, ticket, name))
                        sendTo(c.fd, bytes, relayAnswerWrite(bytes, c.session, ticket, true), t);
                }
                else if (head.kind == RelayKind::Joined && c.slot < 0)
                    c.slot = relaySmallRead(bytes, size);
                else if (head.kind == RelayKind::Data && counting)
                {
                    int who = 0;
                    const std::uint8_t *in = nullptr;
                    const int length = relayDataRead(bytes, size, who, in);
                    if (length < 13 || who < 0 || who >= kNetPlayers)
                        continue;
                    ++t.got;
                    t.gotBytes += static_cast<std::uint64_t>(size);
                    const std::uint32_t seq = relayGet32(in + 1);
                    const std::uint64_t sent = relayGet32(in + 5) | static_cast<std::uint64_t>(relayGet32(in + 9)) << 32;
                    if (t.lateUs.size() < 4000000)
                        t.lateUs.push_back(static_cast<std::uint32_t>((nowNs() - sent) / 1000));
                    if (c.lastSeq[who] != 0 && seq > c.lastSeq[who] + 1)
                        c.gaps += seq - c.lastSeq[who] - 1;
                    if (seq > c.lastSeq[who])
                        c.lastSeq[who] = seq;
                    ++c.got;
                }
            }
        }
    }
    const double elapsed = static_cast<double>(nowNs() - countFrom) / 1e9;
    const double cpuAfter = cpuSeconds(relayPid);

    // What should have come: each client every host frame (and pieces);
    // each host every client's frame -- as counted by the numbers' gaps.
    std::uint64_t gaps = 0, got = 0;
    int in = 0, placed = 0;
    for (const Console &c : consoles)
    {
        gaps += c.gaps;
        got += c.got;
        in += c.stage == Console::Stage::In;
        placed += c.n == 0 ? c.roomId != 0 : c.slot > 0;
    }
    std::sort(t.lateUs.begin(), t.lateUs.end());
    auto at = [&](double q) -> std::uint32_t
    { return t.lateUs.empty() ? 0 : t.lateUs[std::min(t.lateUs.size() - 1, static_cast<std::size_t>(q * t.lateUs.size()))]; };
    const double loss = got + gaps == 0 ? 100.0 : 100.0 * static_cast<double>(gaps) / static_cast<double>(got + gaps);

    std::printf("rooms %d x %d consoles%s%s: %d of %zu came in, %d of %zu in their places\n", rooms, players,
                map ? ", hosts sending a map" : "", flood > 0 ? (", " + std::to_string(flood) + " strangers' datagrams a second").c_str() : "",
                in, consoles.size(), placed, consoles.size());
    std::printf("over %.1f s: sent %llu datagrams (%.0f KB/s), frames heard %llu (%.0f KB/s), lost %llu (%.2f%%)\n",
                elapsed, static_cast<unsigned long long>(t.sent), static_cast<double>(t.sentBytes) / 1024.0 / elapsed,
                static_cast<unsigned long long>(t.got), static_cast<double>(t.gotBytes) / 1024.0 / elapsed,
                static_cast<unsigned long long>(gaps), loss);
    std::printf("how late, through the relay: half under %.2f ms, 99 in 100 under %.2f ms, the most %.2f ms\n",
                at(0.5) / 1000.0, at(0.99) / 1000.0, t.lateUs.empty() ? 0.0 : t.lateUs.back() / 1000.0);
    if (cpuBefore >= 0 && cpuAfter >= 0)
        std::printf("relay CPU: %.1f%% of one core\n", 100.0 * (cpuAfter - cpuBefore) / elapsed);

    if (!spawn.empty() && relayPid > 0)
    {
        ::kill(relayPid, SIGTERM);
        int status = 0;
        ::waitpid(relayPid, &status, 0);
    }
    for (const Console &c : consoles)
        ::close(c.fd);
    const bool ok = placed == static_cast<int>(consoles.size()) && loss <= mostLoss;
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
