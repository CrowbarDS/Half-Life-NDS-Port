#include "data/VramWriter.hpp"

#include <nds.h>

#include <NEAMain.h>

#include "Hot.hpp"

namespace hlrt
{
    VramWriter vramWriter;

    namespace
    {
        struct Job
        {
            std::uint8_t *dst;
            const std::uint8_t *src;
            std::uint32_t left;
        };
        constexpr std::uint32_t kJobs = 64;
        Job jobs[kJobs];
        /// Tickets handed out (the main thread's) and written (the
        /// interrupt's); job n is jobs[(n - 1) % kJobs].
        volatile std::uint32_t queued = 0;
        volatile std::uint32_t written = 0;
        volatile VramWriter::Stats seen;
    } // namespace

    std::uint32_t VramWriter::write(void *dst, const void *src, std::uint32_t bytes)
    {
        const std::uint32_t ticket = queued + 1;
        if (ticket - written > kJobs)
            return 0;
        jobs[(ticket - 1) % kJobs] = {static_cast<std::uint8_t *>(dst), static_cast<const std::uint8_t *>(src),
                                      bytes & ~3u};
        // (The job is all there before the interrupt can see it.)
        asm volatile("" ::: "memory");
        queued = ticket;
        return ticket;
    }

    bool VramWriter::done(std::uint32_t ticket) const
    {
        return static_cast<std::int32_t>(written - ticket) >= 0;
    }

    bool VramWriter::idle() const
    {
        return written == queued;
    }

    void VramWriter::clear()
    {
        const int state = enterCriticalSection();
        written = queued;
        leaveCriticalSection(state);
    }

    HL_ARM HL_ITCM_IRQ void VramWriter::onVblank()
    {
        if (written == queued)
            return;
        std::uint32_t at = written;
        bool wrote = false;
        int line = REG_VCOUNT;
        while (at != queued && line >= 192 && line < kLastLine)
        {
            Job &job = jobs[at % kJobs];
            const std::uint32_t piece = job.left < kPiece ? job.left : kPiece;
            if (piece != 0)
                NEA_VramWrite(job.dst, job.src, piece);
            job.dst += piece;
            job.src += piece;
            job.left -= piece;
            if (job.left == 0)
                ++at;
            wrote = true;
            line = REG_VCOUNT;
            seen.bytes = seen.bytes + piece;
            seen.pieces = seen.pieces + 1;
            if (line > seen.mostLine)
                seen.mostLine = line;
        }
        written = at;
        if (wrote)
        {
            seen.blanks = seen.blanks + 1;
            seen.lastLine = line;
        }
        if (at != queued)
            seen.late = seen.late + 1;
    }

    VramWriter::Stats VramWriter::stats() const
    {
        Stats s;
        s.bytes = seen.bytes;
        s.pieces = seen.pieces;
        s.blanks = seen.blanks;
        s.late = seen.late;
        s.mostLine = seen.mostLine;
        s.lastLine = seen.lastLine;
        return s;
    }
} // namespace hlrt
