// The relay fed a million datagrams, most of them wrong: consoles playing as
// they should, the same datagrams with bits flipped, cut short or run on,
// strangers saying anything in sessions they make up, and noise -- and after
// each, what must always hold:
//
//   - one not known is answered only where it is, and never with more than
//     it sent (nothing can be bounced off the relay at another, or made
//     bigger by it);
//   - everything the relay sends is a datagram of its own, no bigger than
//     one may be;
//   - what it keeps agrees with itself (Relay::consistent), and no more
//     than it may keep.
//
// The same each run (a seed of its own). Run under the sanitizers in CI, a
// read out of bounds stops it.

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>

#include "HlRelay.hpp"
#include "Relay.hpp"

using namespace hldata;
using hlrelay::Address;
using hlrelay::Out;
using hlrelay::Relay;

namespace
{
    std::uint64_t g_state = 0x9E3779B97F4A7C15ull;
    std::uint32_t rnd()
    {
        g_state ^= g_state << 13;
        g_state ^= g_state >> 7;
        g_state ^= g_state << 17;
        return static_cast<std::uint32_t>(g_state >> 16);
    }
    std::uint32_t below(std::uint32_t n) { return n == 0 ? 0 : rnd() % n; }

    int g_failures = 0;
    void fail(long step, const char *what, const std::string &more = "")
    {
        if (g_failures < 20)
            std::printf("FAIL at step %ld: %s %s\n", step, what, more.c_str());
        ++g_failures;
    }

    /// A console that plays as source/net/NetRelay.cpp does (near enough).
    struct Console
    {
        Address at;
        std::uint32_t nonce = 0, session = 0;
        std::uint8_t cookie[kRelayCookieBytes] = {};
        std::uint8_t token[kRelayTokenBytes] = {};
        bool hasToken = false;
        int ticket = 0; ///< a knock its room was asked, to answer
    };

    constexpr int kConsoles = 8;
    constexpr int kStrangers = 48;
    constexpr long kSteps = 1000000;
} // namespace

int main()
{
    Relay relay;
    std::uint8_t secret[Relay::kSecretBytes];
    for (std::uint8_t &b : secret)
        b = static_cast<std::uint8_t>(rnd());
    relay.setSecret(secret);
    // (Room enough that the consoles' own play is mostly let through; the
    // limits have tests of their own.)
    relay.limits.controlPerSecond = 2000;
    relay.limits.dataPerSecond = 2000;
    relay.limits.helloPerSecond = 2000;

    Console consoles[kConsoles];
    for (int n = 0; n < kConsoles; ++n)
        consoles[n].at = {0x0A010000u + static_cast<std::uint32_t>(n / 2), static_cast<std::uint16_t>(5000 + n)};
    Address strangers[kStrangers];
    for (Address &a : strangers)
        a = {rnd() | 1u, static_cast<std::uint16_t>(rnd())};

    std::vector<std::uint32_t> rooms; // (numbers seen in Opened and Rooms)
    std::vector<Out> out;
    std::uint8_t bytes[1600];
    std::uint32_t now = 1000, lastTick = now;
    long peersMost = 0, roomsMost = 0, welcomes = 0, datas = 0;

    /// What the relay said to console `c`: kept, as a console would.
    auto hear = [&](Console &c, const Out &one)
    {
        RelayHead head;
        const int size = static_cast<int>(one.bytes.size());
        if (!relayHeadRead(one.bytes.data(), size, head))
            return;
        if (head.kind == RelayKind::Challenge && head.session == c.nonce)
            relayChallengeRead(one.bytes.data(), size, c.cookie);
        else if (head.kind == RelayKind::Welcome && head.session == c.nonce)
        {
            std::uint32_t session = 0;
            if (relayWelcomeRead(one.bytes.data(), size, session, c.token))
            {
                c.session = session;
                c.hasToken = true;
                ++welcomes;
            }
        }
        else if (head.kind == RelayKind::Opened)
        {
            std::uint32_t id = 0;
            // (The last few opened: a client asks into one there may still be.)
            if (relayRoomIdRead(one.bytes.data(), size, id) && std::find(rooms.begin(), rooms.end(), id) == rooms.end())
            {
                rooms.push_back(id);
                if (rooms.size() > 8)
                    rooms.erase(rooms.begin());
            }
        }
        else if (head.kind == RelayKind::Knock)
        {
            char name[kNetTextBytes];
            relayKnockRead(one.bytes.data(), size, c.ticket, name);
        }
        else if (head.kind == RelayKind::Data)
            ++datas;
    };

    /// A datagram console `c` would send, into `bytes`: its size. Mostly
    /// what a game says (frames, pings; a host its room and answers, a
    /// client asking in), now and then the rest (goodbye, coming in again,
    /// another port).
    auto make = [&](Console &c, bool host) -> int
    {
        const std::uint32_t s = c.session;
        const std::uint32_t room = rooms.empty() ? rnd() : rooms[below(static_cast<std::uint32_t>(rooms.size()))];
        const std::uint32_t roll = below(100);
        if (roll < 45)
        {
            std::uint8_t frame[kNetCmdBytes];
            const int size = static_cast<int>(1 + below(host ? kNetCmdBytes : kNetReplyBytes));
            for (int i = 0; i < size; ++i)
                frame[i] = static_cast<std::uint8_t>(rnd());
            return relayDataWrite(bytes, s, static_cast<int>(below(9)), frame, size);
        }
        if (roll < 55)
            return relaySmallWrite(bytes, RelayKind::Ping, s);
        if (roll < 65)
        {
            if (host)
            {
                RelayOpen o;
                o.most = static_cast<std::uint8_t>(2 + below(7));
                return relayOpenWrite(bytes, s, o);
            }
            return relayJoinWrite(bytes, s, room, "fuzz");
        }
        if (roll < 72)
        {
            if (host)
                return relayAnswerWrite(bytes, s, c.ticket != 0 ? c.ticket : static_cast<int>(below(256)), below(5) != 0);
            return relaySmallWrite(bytes, RelayKind::List, s);
        }
        if (roll < 78)
            return relaySmallWrite(bytes, RelayKind::Admit, s, static_cast<int>(below(4) ? 3 : below(4)));
        if (roll < 84)
        {
            std::uint8_t packet[kVoiceAdpcmBytes];
            for (std::uint8_t &b : packet)
                b = static_cast<std::uint8_t>(rnd());
            return relayVoiceWrite(bytes, s, static_cast<int>(below(9)), static_cast<int>(rnd()),
                                   below(4) ? 8182 : static_cast<int>(below(20000)), packet);
        }
        if (roll < 86)
            return relaySmallWrite(bytes, RelayKind::Kick, s, static_cast<int>(below(9)));
        if (roll < 87)
            return below(4) == 0 ? relaySmallWrite(bytes, RelayKind::Bye, s) : relaySmallWrite(bytes, RelayKind::Ping, s);
        if (roll < 93)
        {
            // (Coming in again, mostly taking its session up: its place kept.)
            if (below(4) == 0)
            {
                c.nonce = rnd();
                return relayHelloWrite(bytes, c.nonce);
            }
            return relayEnterWrite(bytes, c.nonce, c.cookie, c.hasToken && below(8) ? c.token : nullptr);
        }
        if (roll < 99)
            return relaySmallWrite(bytes, RelayKind::Ping, s);
        // (One its router moved to another port.)
        c.at.port = static_cast<std::uint16_t>(5000 + below(4000));
        return relaySmallWrite(bytes, RelayKind::Ping, s);
    };

    /// Bits flipped, cut short, or run on with noise.
    auto mutate = [&](int size) -> int
    {
        switch (below(4))
        {
        case 0:
            for (std::uint32_t k = 1 + below(4); k > 0 && size > 0; --k)
                bytes[below(static_cast<std::uint32_t>(size))] ^= static_cast<std::uint8_t>(1u << below(8));
            return size;
        case 1: return static_cast<int>(below(static_cast<std::uint32_t>(size) + 1));
        case 2:
        {
            const int more = static_cast<int>(below(static_cast<std::uint32_t>(sizeof(bytes) - size)));
            for (int i = size; i < size + more; ++i)
                bytes[i] = static_cast<std::uint8_t>(rnd());
            return size + more;
        }
        default:
            // (The head kept, what follows any.)
            for (int i = kRelayHeadBytes; i < size; ++i)
                bytes[i] = static_cast<std::uint8_t>(rnd());
            return size;
        }
    };

    for (long step = 0; step < kSteps; ++step)
    {
        now += below(30);
        if (below(20000) == 0)
            now += below(120000); // (a long silence: consoles let go, rooms closed)
        if (now - lastTick >= 100)
        {
            out.clear();
            relay.tick(now, out);
            lastTick = now;
            for (const Out &one : out)
            {
                if (one.bytes.size() < static_cast<std::size_t>(kRelayHeadBytes) ||
                    one.bytes.size() > static_cast<std::size_t>(kRelayMostBytes))
                    fail(step, "the relay's clock said something not of the protocol's size");
                for (Console &c : consoles)
                    if (c.at == one.to)
                        hear(c, one);
            }
        }

        // Who, and what.
        Address from;
        int size = 0;
        Console *me = nullptr;
        const std::uint32_t pick = below(100);
        if (pick < 55)
        {
            const int n = static_cast<int>(below(kConsoles));
            me = &consoles[n];
            size = make(*me, n % 4 == 0); // (consoles 0 and 4 host)
            from = me->at;
            if (pick >= 40)
                size = mutate(size);
        }
        else if (pick < 85)
        {
            from = strangers[below(kStrangers)];
            Console pretend;
            pretend.session = below(3) ? rnd() : consoles[below(kConsoles)].session; // (another's session, from elsewhere)
            pretend.nonce = rnd();
            for (std::uint8_t &b : pretend.cookie)
                b = static_cast<std::uint8_t>(rnd());
            if (below(3) == 0)
                std::memcpy(pretend.token, consoles[below(kConsoles)].token, kRelayTokenBytes); // (another's token)
            pretend.hasToken = true;
            pretend.at = from;
            size = make(pretend, below(2) != 0);
            from = pretend.at;
            if (below(2))
                size = mutate(size);
            if (below(10) == 0 && size >= kRelayHeadBytes)
                bytes[2] = static_cast<std::uint8_t>(rnd()); // (another version)
        }
        else
        {
            from = below(2) ? strangers[below(kStrangers)] : consoles[below(kConsoles)].at;
            size = static_cast<int>(below(static_cast<std::uint32_t>(sizeof(bytes))));
            for (int i = 0; i < size; ++i)
                bytes[i] = static_cast<std::uint8_t>(rnd());
            if (below(2) && size >= 4)
            {
                bytes[0] = static_cast<std::uint8_t>(kRelayMagic & 0xFF);
                bytes[1] = static_cast<std::uint8_t>(kRelayMagic >> 8);
                bytes[2] = below(4) ? kRelayVersion : static_cast<std::uint8_t>(rnd());
            }
        }

        const bool knew = relay.knows(from);
        out.clear();
        relay.receive(from, bytes, size, now, out);

        long toThem = 0;
        for (const Out &one : out)
        {
            if (one.bytes.size() < static_cast<std::size_t>(kRelayHeadBytes) ||
                one.bytes.size() > static_cast<std::size_t>(kRelayMostBytes) ||
                one.bytes[0] != (kRelayMagic & 0xFF) || one.bytes[1] != (kRelayMagic >> 8))
                fail(step, "the relay sent something not a datagram of its own");
            if (!knew && !(one.to == from))
                fail(step, "one not known made the relay send to another");
            if (one.to == from)
                toThem += static_cast<long>(one.bytes.size());
            for (Console &c : consoles)
                if (c.at == one.to)
                    hear(c, one);
        }
        if (!knew && toThem > size)
            fail(step, "one not known was answered with more than it sent");

        peersMost = relay.peers() > static_cast<std::size_t>(peersMost) ? static_cast<long>(relay.peers()) : peersMost;
        roomsMost = relay.rooms() > static_cast<std::size_t>(roomsMost) ? static_cast<long>(relay.rooms()) : roomsMost;
        if (relay.peers() > Relay::kMostPeers || relay.rooms() > Relay::kMostRooms)
            fail(step, "more kept than it may");
        if (step % 1000 == 0)
        {
            const std::string why = relay.consistent();
            if (!why.empty())
                fail(step, "what the relay keeps does not agree:", why);
        }
    }

    const std::string why = relay.consistent();
    if (!why.empty())
        fail(kSteps, "at the end, what the relay keeps does not agree:", why);
    out.clear();
    relay.stop(out);
    if (relay.peers() != 0 || relay.rooms() != 0 || !relay.consistent().empty())
        fail(kSteps, "stopped, it still keeps something");

    // (That it was played, not only survived: consoles let in, rooms opened,
    // frames passed on.)
    std::printf("fuzz_relay: %ld datagrams; welcomes %ld, frames passed %ld; most consoles %ld, rooms %ld\n", kSteps,
                welcomes, datas, peersMost, roomsMost);
    if (welcomes < 1000 || datas < 1000 || roomsMost < 1)
        fail(kSteps, "the consoles hardly played: the fuzz says little");
    std::printf("%s (%d failed)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
