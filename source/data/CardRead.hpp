#pragma once

// A read from the card in pieces. On a DSi the card is the ARM7's to read:
// while it reads, the ARM7's sound work waits (arm7/source/Pipeline.arm.cpp),
// and a read of tens of kilobytes at once is long enough to be heard as a
// crackle in the music. In pieces of 8 KB, it is never held up for longer
// than its output ring is ahead of the speakers.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#ifndef HLDS_CHILD
#include <NEAMain.h>
#endif

namespace hlrt
{
    inline constexpr std::size_t kCardPiece = 8 * 1024;

    /// fread's, `bytes` of them, in pieces: how many were read.
    inline std::size_t cardRead(void *to, std::size_t bytes, std::FILE *file)
    {
        auto *at = static_cast<unsigned char *>(to);
        std::size_t done = 0;
        while (done < bytes)
        {
            const std::size_t want = bytes - done < kCardPiece ? bytes - done : kCardPiece;
            const std::size_t got = std::fread(at + done, 1, want, file);
            done += got;
            if (got != want)
                break;
        }
        return done;
    }

#ifndef HLDS_CHILD
    /// Whether NEA_NpacMemberRead can give `member` whole: stored, or packed
    /// in a way it undoes as it reads.
    inline bool cardMemberReadable(const NEA_NpacMember &member)
    {
        return member.method == NEA_NPAC_STORED || member.method == NEA_NPAC_LZ77 ||
               member.method == NEA_NPAC_RLE;
    }

    /// `bytes` of an archive's member from `from`, with no file opened: a
    /// stored one in pieces, as above; a packed one (from 0, whole) as it is
    /// undone, which reads the card 512 bytes at a time.
    inline bool cardReadMember(const NEA_NpacMember &member, std::uint32_t from, void *to, std::uint32_t bytes)
    {
        if (member.method != NEA_NPAC_STORED)
            return NEA_NpacMemberRead(&member, from, to, bytes) == 0;
        auto *at = static_cast<unsigned char *>(to);
        for (std::uint32_t done = 0; done < bytes;)
        {
            const std::uint32_t want = bytes - done < kCardPiece ? bytes - done : kCardPiece;
            if (NEA_NpacMemberRead(&member, from + done, at + done, want) != 0)
                return false;
            done += want;
        }
        return true;
    }
#endif
} // namespace hlrt
