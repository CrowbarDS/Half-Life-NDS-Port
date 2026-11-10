// The relay under libFuzzer (clang's): each input is a run of datagrams, the
// relay new for each input, and what fuzz_relay.cpp checks holds after each.
// Built only with -DHLDS_RELAY_LIBFUZZER=ON and clang (tools/relay/README.md).
//
// An input, record after record:
//   u8   who: 0..7 an address of eight a console may be at, else another
//   u8   time: milliseconds since the one before (x 64 if its top bit is set)
//   u16  length (little-endian), then that many bytes: the datagram
//
// A console comes in properly first (Hello, Enter) at each of the eight, so
// that what follows reaches what a known console reaches, not only the door.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "HlRelay.hpp"
#include "Relay.hpp"

using namespace hldata;
using hlrelay::Address;
using hlrelay::Out;
using hlrelay::Relay;

namespace
{
    Address where(std::uint8_t who)
    {
        if (who < 8)
            return {0x0A010000u + (who >> 1), static_cast<std::uint16_t>(5000 + who)};
        return {0xC0000000u + who, static_cast<std::uint16_t>(who * 131)};
    }

    void check(bool ok)
    {
        if (!ok)
            std::abort();
    }
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    Relay relay;
    std::vector<Out> out;
    std::uint32_t now = 1000;
    std::uint8_t bytes[64];

    // Eight consoles in.
    for (std::uint8_t who = 0; who < 8; ++who)
    {
        out.clear();
        relay.receive(where(who), bytes, relayHelloWrite(bytes, who + 1u), now, out);
        if (out.size() != 1)
            continue;
        std::uint8_t cookie[kRelayCookieBytes];
        relayChallengeRead(out[0].bytes.data(), static_cast<int>(out[0].bytes.size()), cookie);
        out.clear();
        relay.receive(where(who), bytes, relayEnterWrite(bytes, who + 1u, cookie, nullptr), now, out);
    }

    std::size_t at = 0;
    while (at + 4 <= size)
    {
        const Address from = where(data[at]);
        const std::uint8_t dt = data[at + 1];
        now += dt & 0x80 ? (dt & 0x7F) * 64u : dt;
        std::size_t length = data[at + 2] | static_cast<std::size_t>(data[at + 3]) << 8;
        at += 4;
        if (length > size - at)
            length = size - at;
        const bool knew = relay.knows(from);
        out.clear();
        relay.receive(from, data + at, static_cast<int>(length), now, out);
        at += length;
        std::size_t toThem = 0;
        for (const Out &one : out)
        {
            check(one.bytes.size() >= static_cast<std::size_t>(kRelayHeadBytes) &&
                  one.bytes.size() <= static_cast<std::size_t>(kRelayMostBytes));
            check(knew || one.to == from);
            if (one.to == from)
                toThem += one.bytes.size();
        }
        check(knew || toThem <= length);
        out.clear();
        relay.tick(now, out);
    }
    check(relay.consistent().empty());
    return 0;
}
