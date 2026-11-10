#pragma once

// SipHash-2-4 (Aumasson and Bernstein, "SipHash: a fast short-input PRF",
// 2012): a keyed hash for short inputs. The relay uses it where a secret of
// its own must decide the result -- an address put in its log without being
// shown, and (protocol v4) the cookie a console must send back -- so that
// nobody without the key can make or guess one.

#include <cstddef>
#include <cstdint>

namespace hlrelay
{
    inline std::uint64_t sipGet64(const std::uint8_t *in)
    {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<std::uint64_t>(in[i]) << (8 * i);
        return v;
    }

    inline std::uint64_t sipRotate(std::uint64_t v, int by) { return v << by | v >> (64 - by); }

    struct SipState
    {
        std::uint64_t v0, v1, v2, v3;
        void round()
        {
            v0 += v1;
            v1 = sipRotate(v1, 13);
            v1 ^= v0;
            v0 = sipRotate(v0, 32);
            v2 += v3;
            v3 = sipRotate(v3, 16);
            v3 ^= v2;
            v0 += v3;
            v3 = sipRotate(v3, 21);
            v3 ^= v0;
            v2 += v1;
            v1 = sipRotate(v1, 17);
            v1 ^= v2;
            v2 = sipRotate(v2, 32);
        }
        void take(std::uint64_t m)
        {
            v3 ^= m;
            round();
            round();
            v0 ^= m;
        }
    };

    /// SipHash-2-4 of `size` bytes at `in`, under the 16-byte `key`.
    inline std::uint64_t sipHash24(const std::uint8_t *key, const std::uint8_t *in, std::size_t size)
    {
        const std::uint64_t k0 = sipGet64(key), k1 = sipGet64(key + 8);
        SipState s{k0 ^ 0x736f6d6570736575ull, k1 ^ 0x646f72616e646f6dull, k0 ^ 0x6c7967656e657261ull,
                   k1 ^ 0x7465646279746573ull};
        const std::size_t whole = size - size % 8;
        for (std::size_t at = 0; at < whole; at += 8)
            s.take(sipGet64(in + at));
        // The last block: what is left, and the length's low byte at the top.
        std::uint64_t last = static_cast<std::uint64_t>(size & 0xFF) << 56;
        for (std::size_t i = 0; i < size % 8; ++i)
            last |= static_cast<std::uint64_t>(in[whole + i]) << (8 * i);
        s.take(last);
        s.v2 ^= 0xFF;
        for (int i = 0; i < 4; ++i)
            s.round();
        return s.v0 ^ s.v1 ^ s.v2 ^ s.v3;
    }
} // namespace hlrelay
