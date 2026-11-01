#pragma once

// A demo's progress is kept by the host, not by the console playing it, which
// has no card to keep it on and better things to do with its memory. A file
// goes up a small frame at a time, in order, the host saying how far it has
// got; the whole of it is checked before it takes the place of the last. (It
// comes back down the way a texture does: FetchFrom::DemoSave.)

#include <cstdint>
#include <cstring>

#include "HlLevel.hpp"
#include "HlNet.hpp"

namespace hldata
{
    inline constexpr std::uint8_t kNetFrameDemoUp = 7;  ///< a client: a piece of a file
    inline constexpr std::uint8_t kNetFrameDemoAck = 8; ///< the host: how far it has got
    /// What a frame carries: a client's are small (HlNet.hpp, kNetClientEvents).
    inline constexpr int kDemoUpChunk = 170;
    inline constexpr std::uint32_t kDemoUpWindow = 8 * kDemoUpChunk;
    inline constexpr int kDemoUpRetryFrames = 45;
    inline constexpr std::uint32_t kDemoFileMost = 256 * 1024;
    /// The files: these two, or the map's own, by the hash of its name.
    inline constexpr std::uint32_t kDemoFileAuto = 1, kDemoFileArrival = 2;

    struct DemoUpHead
    {
        std::uint32_t what = 0, total = 0, crc = 0, at = 0;
        int count = 0;
        const std::uint8_t *data = nullptr;
    };

    inline bool demoUpRead(const std::uint8_t *bytes, int size, DemoUpHead &head)
    {
        detail::NetIn in{bytes, bytes + size};
        if (in.u16() != kNetMagic || in.u8() != kNetVersion || in.u8() != kNetFrameDemoUp)
            return false;
        head.what = in.u32();
        head.total = in.u32();
        head.crc = in.u32();
        head.at = in.u32();
        head.count = static_cast<int>(in.u16());
        head.data = in.at;
        return in.ok && head.count >= 0 && head.count <= kDemoUpChunk && in.end - in.at >= head.count;
    }

    inline int demoAckWrite(int seat, std::uint32_t what, std::uint32_t crc, std::uint32_t upTo, std::uint8_t *bytes,
                            int size)
    {
        detail::NetOut out{bytes, bytes + size};
        out.u16(kNetMagic);
        out.u8(kNetVersion);
        out.u8(kNetFrameDemoAck);
        out.u8(static_cast<std::uint8_t>(seat));
        out.u32(what);
        out.u32(crc);
        out.u32(upTo);
        return out.ok ? static_cast<int>(out.at - bytes) : 0;
    }

    /// The console's side, of one file.
    struct DemoUpSender
    {
        const std::uint8_t *data = nullptr;
        std::uint32_t what = 0, total = 0, crc = 0;
        std::uint32_t acked = 0, next = 0;
        int quiet = 0;
        bool on = false;

        void begin(std::uint32_t file, const std::uint8_t *bytes, std::uint32_t size)
        {
            *this = DemoUpSender();
            data = bytes;
            what = file;
            total = size;
            crc = crc32Finish(crc32Update(kCrcStart, bytes, size));
            on = true;
        }
        /// The next frame to send; 0: none just now.
        int frame(std::uint8_t *bytes, int size)
        {
            if (!on)
                return 0;
            // (Nothing said for a while: what was lost is sent again.)
            if (++quiet > kDemoUpRetryFrames)
            {
                quiet = 0;
                next = acked;
            }
            if (next >= total || next - acked >= kDemoUpWindow)
                return 0;
            const std::uint32_t rest = total - next;
            const int count = rest < kDemoUpChunk ? static_cast<int>(rest) : kDemoUpChunk;
            detail::NetOut out{bytes, bytes + size};
            out.u16(kNetMagic);
            out.u8(kNetVersion);
            out.u8(kNetFrameDemoUp);
            out.u32(what);
            out.u32(total);
            out.u32(crc);
            out.u32(next);
            out.u16(static_cast<std::uint16_t>(count));
            if (count > 0)
                out.bytes(data + next, count);
            if (!out.ok)
                return 0;
            next += static_cast<std::uint32_t>(count);
            return static_cast<int>(out.at - bytes);
        }
        /// The host's answer. True once it has the whole file.
        bool heard(const std::uint8_t *bytes, int size, int me)
        {
            detail::NetIn in{bytes, bytes + size};
            if (!on || in.u16() != kNetMagic || in.u8() != kNetVersion || in.u8() != kNetFrameDemoAck ||
                in.u8() != me)
                return false;
            const std::uint32_t file = in.u32(), said = in.u32(), upTo = in.u32();
            if (!in.ok || file != what || said != crc)
                return false;
            quiet = 0;
            // (It went wrong, and it starts again.)
            if (upTo < acked)
                next = upTo;
            acked = upTo;
            if (next < acked)
                next = acked;
            if (acked >= total)
            {
                on = false;
                return true;
            }
            return false;
        }
    };

    /// The host's side, of one console's file, into the caller's memory.
    struct DemoUpTaker
    {
        std::uint8_t *room = nullptr;
        std::uint32_t what = 0, total = 0, crc = 0, upTo = 0;
        bool whole = false;

        /// A piece of a file it is already taking (else open it, as big as
        /// head.total, and call this after). True if it has just become whole:
        /// room's `total` bytes, checked.
        bool take(const DemoUpHead &head)
        {
            if (room == nullptr || head.what != what || head.total != total || head.crc != crc || whole)
                return false;
            if (head.at != upTo || head.at + static_cast<std::uint32_t>(head.count) > total)
                return false;
            if (head.count > 0)
                std::memcpy(room + upTo, head.data, static_cast<std::size_t>(head.count));
            upTo += static_cast<std::uint32_t>(head.count);
            if (upTo < total)
                return false;
            if (crc32Finish(crc32Update(kCrcStart, room, total)) != crc)
            {
                upTo = 0; // (all of it again)
                return false;
            }
            whole = true;
            return true;
        }
        bool taking(const DemoUpHead &head) const
        {
            // (Whole, and let go of: what is said again of it is only answered.)
            return (room != nullptr || whole) && head.what == what && head.total == total && head.crc == crc;
        }
        void open(const DemoUpHead &head, std::uint8_t *memory)
        {
            room = memory;
            what = head.what;
            total = head.total;
            crc = head.crc;
            upTo = 0;
            whole = false;
        }
    };
} // namespace hldata
