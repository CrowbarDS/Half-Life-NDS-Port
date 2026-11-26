#pragma once

// How far open the mouth of whoever is speaking is: the game's pseudo facial
// animation. Its engine reads the speech sample as it is played
// (SND_MoveMouth) -- every 80-odd samples one, the mean of ten of them, of
// the 8-bit sample's size -- into the speaker's mouthopen, and the model's
// controller 4, its mouth, is turned by mouthopen / 64 between that
// controller's start and end (HlAnim.hpp mouthValue).
//
// Here the word being said is the IMA-ADPCM the hardware plays (HlVoice.hpp:
// a four-byte head, then four bits a sample, the low half of each byte
// first), in memory while it is said: it is decoded forward to where its
// playing has got to, frame by frame, and read the game's way (a 16-bit
// sample >> 8 being its 8-bit one). Nothing is decoded twice; nothing is
// kept of it but where the decoder is.
//
// As the game's, the mouth is read across a whole sentence: the next word
// goes on with the same mean (next), and it shuts when the sentence is over
// (stop), not between its words -- or it snapped shut at every word, and a
// short word never opened it. And the game reads every 80-odd samples of a
// voice made at 11025 a second: a word cooked at another rate is read as
// often in time, not in samples.
//
// std-only: the host tests run it.

#include <cstdint>

#include "HlVoice.hpp"

namespace hldata
{
    struct MouthMeter
    {
        /// The ADPCM being said (null: none, and the mouth is shut).
        const std::uint8_t *data = nullptr;
        std::uint32_t samples = 0; ///< how many it has
        std::uint32_t at = 0;      ///< the next to decode
        std::uint32_t next_ = 0;   ///< the next to be read for the mouth
        int pcm = 0;
        int index = 0;
        int sum = 0;               ///< of the samples read since the last mean
        int taken = 0;
        std::uint32_t scale = 1u << 16; ///< samples a reading, against the game's 11025 a second (Q16)
        std::uint8_t open = 0;     ///< the game's mouthopen: 0 shut, 64 and over as far as it goes

        /// A sentence begins: its first word, `bytes` of ADPCM at `adpcm`,
        /// made at `hz` samples a second.
        void start(const std::uint8_t *adpcm, std::uint32_t bytes, std::uint32_t hz = 11025)
        {
            *this = MouthMeter();
            next(adpcm, bytes, hz);
        }

        /// The sentence's next word: read on with the same mean, the reading
        /// it was waiting for still as far off.
        void next(const std::uint8_t *adpcm, std::uint32_t bytes, std::uint32_t hz = 11025)
        {
            const std::uint32_t wait = data != nullptr && next_ > at ? next_ - at : 0;
            data = nullptr;
            at = 0;
            samples = 0;
            next_ = wait;
            if (adpcm == nullptr || bytes <= 4)
                return;
            data = adpcm;
            samples = (bytes - 4) * 2;
            pcm = static_cast<std::int16_t>(adpcm[0] | (adpcm[1] << 8));
            index = adpcm[2] & 0x7F;
            index = index > 88 ? 88 : index;
            // (Once a word: the game's spacing, in this word's samples.)
            scale = hz == 0 ? 1u << 16 : static_cast<std::uint32_t>((static_cast<std::uint64_t>(hz) << 16) / 11025);
        }

        /// Nothing being said (the sentence is over): the mouth shuts.
        void stop() { *this = MouthMeter(); }

        /// The word has been played up to sample `to`: read it up to there.
        /// (At most kMostAFeed at once: a word cannot get that far ahead in
        /// a frame, and a decoder never spends longer than that on it.)
        static constexpr std::uint32_t kMostAFeed = 8192;
        void feed(std::uint32_t to)
        {
            if (data == nullptr)
                return;
            if (to > samples)
                to = samples;
            if (to > at + kMostAFeed)
                to = at + kMostAFeed;
            for (; at < to; ++at)
            {
                const int code = (data[4 + (at >> 1)] >> ((at & 1) * 4)) & 0xF;
                detail::adpcmStep(pcm, index, code);
                if (at != next_)
                    continue;
                // (The game's: the sample's size, then on 80 and a bit more,
                // by the sample itself -- not every frame the same ones.)
                const int size = (pcm < 0 ? -pcm : pcm) >> 8;
                sum += size;
                const std::uint32_t gap = (80 + static_cast<std::uint32_t>((pcm >> 8) & 0x1F)) * scale >> 16;
                next_ = at + (gap != 0 ? gap : 1);
                if (++taken == 10)
                {
                    // (The mean of the ten: * 205 >> 11 is / 10, to a part in
                    // a thousand.)
                    const int mean = (sum * 205) >> 11;
                    open = static_cast<std::uint8_t>(mean > 255 ? 255 : mean);
                    sum = 0;
                    taken = 0;
                }
            }
            // (Said to its end, the mouth stays as it was: the sentence's
            // next word goes on from it, or the sentence's end shuts it.)
        }
    };
} // namespace hldata
