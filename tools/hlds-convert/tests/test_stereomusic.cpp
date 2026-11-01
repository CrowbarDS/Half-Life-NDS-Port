// The music in stereo as the ARM7 plays it (shared/HlStereoMusic.hpp)
// (test_sound has it against the converter's own files): played at its own
// rate it is the track as decoded; at the
// console's rate it takes as many of the track's pairs as the time is long;
// a ring the ARM9 is behind with is silence, counted, until it catches up;
// and the end of the track is silence, not counted.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "HlStereoMusic.hpp"
#include "TestBspBuilder.hpp"


using namespace hldata;
using namespace hldstest;

namespace
{
    /// Blocks of made-up music: states and random nibbles.
    std::vector<std::uint16_t> makeBlocks(int count, std::uint32_t seed)
    {
        std::vector<std::uint16_t> out(count * StereoMusic::kBlockWords);
        for (int b = 0; b < count; ++b)
        {
            std::uint16_t *block = out.data() + b * StereoMusic::kBlockWords;
            seed = seed * 1664525u + 1013904223u;
            block[0] = static_cast<std::uint16_t>(static_cast<std::int16_t>((seed >> 16) % 20000 - 10000));
            block[1] = static_cast<std::uint16_t>((seed >> 8) % 60);
            block[2] = static_cast<std::uint16_t>(static_cast<std::int16_t>((seed >> 4) % 20000 - 10000));
            block[3] = static_cast<std::uint16_t>((seed >> 12) % 60);
            for (int w = 0; w < kMusicBlockWords; ++w)
            {
                seed = seed * 1664525u + 1013904223u;
                block[4 + w] = static_cast<std::uint16_t>(seed >> 16);
            }
        }
        return out;
    }

    /// The pairs the blocks hold, decoded one by one.
    void decodeAll(const std::vector<std::uint16_t> &blocks, int count, std::vector<std::int16_t> &l,
                   std::vector<std::int16_t> &r)
    {
        for (int b = 0; b < count; ++b)
        {
            const std::uint16_t *block = blocks.data() + b * StereoMusic::kBlockWords;
            std::int32_t value[2] = {static_cast<std::int16_t>(block[0]), static_cast<std::int16_t>(block[2])};
            std::int32_t index[2] = {static_cast<std::int16_t>(block[1]), static_cast<std::int16_t>(block[3])};
            for (std::uint32_t p = 0; p < StereoMusic::kPairsPerBlock; ++p)
            {
                const unsigned word = block[4 + p / 2];
                const unsigned byte = (p & 1) ? word >> 8 : word & 0xFF;
                StereoMusic::decode(value[0], index[0], byte & 15);
                StereoMusic::decode(value[1], index[1], (byte >> 4) & 15);
                l.push_back(static_cast<std::int16_t>(value[0]));
                r.push_back(static_cast<std::int16_t>(value[1]));
            }
        }
    }
} // namespace

int main()
{
    static StereoMusic music;
    const std::vector<std::uint16_t> blocks = makeBlocks(6, 777);
    std::vector<std::int16_t> wantL, wantR;
    decodeAll(blocks, 6, wantL, wantR);

    // At its own rate: the track, a sample behind (each sample is the last
    // pair, the line to the next not yet begun).
    {
        music.open(blocks.data(), 6, kStereoRate, kStereoRate, 6 * StereoMusic::kPairsPerBlock, 256);
        std::vector<std::int32_t> l(1000, 0), r(1000, 0);
        music.mix(l.data(), r.data(), 1000, 6);
        bool same = true;
        for (int i = 1; i < 1000 && same; ++i)
            same = l[i] == wantL[i - 1] && r[i] == wantR[i - 1];
        check(same && music.dry() == 0, "at its own rate, the track as decoded");
    }

    // At the console's rate: as many pairs as the time is long; across the
    // blocks with no step in it.
    {
        music.open(blocks.data(), 6, kStereoRate, 32728, 6 * StereoMusic::kPairsPerBlock, 256);
        std::vector<std::int32_t> l(16364, 0), r(16364, 0);
        for (int at = 0; at < 16364; at += 64)
            music.mix(l.data() + at, r.data() + at, std::min(64, 16364 - at), 6);
        const long want = 16364L * kStereoRate / 32728;
        check(std::labs(static_cast<long>(music.played()) - want) <= 1, "half a second at 32 kHz: half a second of the track");
        // (Every sample between two neighbours of the track, or one of them.)
        bool between = true;
        std::uint32_t phase = 0;
        const std::uint32_t step = static_cast<std::uint32_t>((static_cast<std::uint64_t>(kStereoRate) << 16) / 32728);
        std::uint32_t p = 0;
        for (int i = 0; i < 16364 && between; ++i)
        {
            phase += step;
            while (phase >= 0x10000u)
            {
                phase -= 0x10000u;
                ++p;
            }
            const int a = p >= 2 ? wantL[p - 2] : 0, b = p >= 1 ? wantL[p - 1] : 0;
            between = l[i] >= std::min(a, b) - 1 && l[i] <= std::max(a, b) + 1;
        }
        check(between, "each sample on the line between two of the track's");
        check(music.reading() == music.played() / StereoMusic::kPairsPerBlock, "the block being read is known");
    }

    // The rate changed under it (the DSi's quality): the track's time kept.
    {
        music.open(blocks.data(), 6, kStereoRate, 32728, 6 * StereoMusic::kPairsPerBlock, 256);
        std::vector<std::int32_t> l(47605, 0), r(47605, 0);
        music.mix(l.data(), r.data(), 8182, 6); // a quarter of a second at 32 kHz
        music.setOutRate(47605);
        music.mix(l.data(), r.data(), 11901, 6); // ... and a quarter at 47 kHz
        check(std::labs(static_cast<long>(music.played()) - kStereoRate / 2) <= 2,
              "a quarter of a second at each rate: half a second of the track");
    }

    // Behind: silence, counted; then caught up, the track again.
    {
        music.open(blocks.data(), 6, kStereoRate, kStereoRate, 6 * StereoMusic::kPairsPerBlock, 256);
        std::vector<std::int32_t> l(5000, 0), r(5000, 0);
        music.mix(l.data(), r.data(), 3000, 1); // (one block written: 2048 pairs)
        check(music.played() == StereoMusic::kPairsPerBlock && music.dry() > 900,
              "the ARM9 behind: the music stops, the time counted");
        const std::uint32_t was = music.played();
        music.mix(l.data() + 3000, r.data() + 3000, 2000, 6);
        check(music.played() == was + 2000 && l[4999] == wantL[was + 1998], "caught up: the next block, from its start");
    }

    // The end: silence, not counted as dry.
    {
        music.open(blocks.data(), 6, kStereoRate, kStereoRate, 3000, 256);
        std::vector<std::int32_t> l(5000, 0), r(5000, 0);
        music.mix(l.data(), r.data(), 5000, 6);
        check(music.over() && music.played() == 3000 && music.dry() == 0 && l[4999] == 0,
              "the end of the track: silence, and not counted as the ARM9's");
    }

    // As loud as the player has it.
    {
        music.open(blocks.data(), 6, kStereoRate, kStereoRate, 6 * StereoMusic::kPairsPerBlock, 128);
        std::vector<std::int32_t> l(100, 0), r(100, 0);
        music.mix(l.data(), r.data(), 100, 6);
        check(l[50] == (wantL[49] * 128) >> 8, "at half volume, half as loud");
    }

    if (g_failures == 0)
        std::printf("test_stereomusic: ok\n");
    return g_failures == 0 ? 0 : 1;
}
