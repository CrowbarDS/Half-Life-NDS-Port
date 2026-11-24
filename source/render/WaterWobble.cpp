#include "global_header.hpp"

#include "render/WaterWobble.hpp"

#include <cstdint>

#include "Global.hpp"
#include "HlFixed.hpp"

namespace hlrt
{
    WaterWobble waterWobble;

    namespace
    {
        constexpr int kLines = 192;
        constexpr int kChannel = 1;

#ifndef HLDS_CHILD
        /// The two tables (one shown, one being made): each line's scroll,
        /// line 0's first (set in the vertical blank), then one for each
        /// horizontal blank. The DSi's memory.
        TWL_BSS std::uint16_t tables[2][kLines + 1];

        /// Table `n` for now: 65536 a turn, so 512 a line (128 lines round),
        /// and 20 lines a second -- of 60 blanks, 171 a blank; two pixels
        /// either way, rounded.
        HL_TWL_CODE void build(int n)
        {
            std::uint16_t *table = tables[n];
            const std::uint32_t phase = Global::vblanks() * 171u;
            for (int line = 0; line <= kLines; ++line)
            {
                hldata::fx s, c;
                hldata::fxSinCos(static_cast<std::uint16_t>(phase + static_cast<std::uint32_t>(line) * 512u), s, c);
                const int moved = (s * 2 + 2048) >> 12;
                table[line] = static_cast<std::uint16_t>(moved & 511);
            }
            DC_FlushRange(table, sizeof(tables[0]));
        }

        /// A monitor's table: every line off by -1, 0 or 1 pixels at random,
        /// new each frame; and 6 lines torn 3 pixels across, rolling down
        /// the picture once in 2 seconds (96 lines a second: 192 / 120 a
        /// blank, as eighths).
        HL_TWL_CODE void buildNoise(int n)
        {
            std::uint16_t *table = tables[n];
            const std::uint32_t blanks = Global::vblanks();
            std::uint32_t seed = blanks * 2654435761u + 0x9E3779B9u;
            const int band = static_cast<int>((blanks * 13u) >> 3) % kLines;
            for (int line = 0; line <= kLines; ++line)
            {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                int moved = static_cast<int>(seed % 3u) - 1;
                const int into = line - band;
                if (into >= 0 && into < 6)
                    moved += 3;
                table[line] = static_cast<std::uint16_t>(moved & 511);
            }
            DC_FlushRange(table, sizeof(tables[0]));
        }
#endif
    } // namespace

    void WaterWobble::frame(bool under, bool monitor)
    {
#ifdef HLDS_CHILD
        (void)under;
        (void)monitor;
#else
        if ((!under && !monitor) || !isDSiMode())
        {
            fresh_ = 0;
            return;
        }
        // (Into the table not being shown; the next vertical blank takes it.)
        const int next = table_ == 0 ? 1 : 0;
        if (monitor)
            buildNoise(next);
        else
            build(next);
        table_ = next;
        fresh_ = 3;
#endif
    }

    void WaterWobble::onVblank()
    {
#ifndef HLDS_CHILD
        // (fresh_ is only ever set on a DSi: the tables are its memory.)
        if (fresh_ > 0 && table_ >= 0)
        {
            fresh_ = fresh_ - 1;
            const std::uint16_t *table = tables[table_];
            DMA_CR(kChannel) = 0;
            REG_BG0HOFS = table[0];
            DMA_SRC(kChannel) = reinterpret_cast<std::uint32_t>(&table[1]);
            DMA_DEST(kChannel) = reinterpret_cast<std::uint32_t>(&REG_BG0HOFS);
            DMA_CR(kChannel) = DMA_ENABLE | DMA_REPEAT | DMA_START_HBL | DMA_16_BIT | DMA_SRC_INC | DMA_DST_FIX | 1;
            armed_ = true;
            return;
        }
#endif
        if (armed_)
        {
            DMA_CR(kChannel) = 0;
            REG_BG0HOFS = 0;
            armed_ = false;
        }
    }
} // namespace hlrt
