#pragma once

// Voice (online, TALK held: source/audio/Voice.cpp): what the microphone
// hears, a packet at a time, in the form the DS's sound hardware plays --
// IMA-ADPCM as GBATEK has the hardware do it, a four-byte head (the first
// sample, and where in the steps it starts) and then four bits a sample, the
// low half of each byte first. A packet is a clip of its own, played by a
// channel pointed at it (Arm7Audio's Play and Chain): one lost costs its 40 ms
// and nothing after it.
//
// The encoder works each sample out as the hardware will, from what it
// will have, not from what was heard, so the two never drift apart.
//
// std-only: the host tests run it.

#include <cstdint>

namespace hldata
{
    /// A packet's samples (40 ms at 8 kHz), and its bytes as ADPCM.
    inline constexpr int kVoiceSamples = 320;
    inline constexpr int kVoiceAdpcmBytes = 4 + kVoiceSamples / 2;

    namespace detail
    {
        inline constexpr std::uint16_t kAdpcmSteps[89] = {
            0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x0010, 0x0011, 0x0013, 0x0015, 0x0017,
            0x0019, 0x001C, 0x001F, 0x0022, 0x0025, 0x0029, 0x002D, 0x0032, 0x0037, 0x003C, 0x0042, 0x0049, 0x0050,
            0x0058, 0x0061, 0x006B, 0x0076, 0x0082, 0x008F, 0x009D, 0x00AD, 0x00BE, 0x00D1, 0x00E6, 0x00FD, 0x0117,
            0x0133, 0x0151, 0x0173, 0x0198, 0x01C1, 0x01EE, 0x0220, 0x0256, 0x0292, 0x02D4, 0x031C, 0x036C, 0x03C3,
            0x0424, 0x048E, 0x0502, 0x0583, 0x0610, 0x06AB, 0x0756, 0x0812, 0x08E0, 0x09C3, 0x0ABD, 0x0BD0, 0x0CFF,
            0x0E4C, 0x0FBA, 0x114C, 0x1307, 0x14EE, 0x1706, 0x1954, 0x1BDC, 0x1EA5, 0x21B6, 0x2515, 0x28CA, 0x2CDF,
            0x315B, 0x364B, 0x3BB9, 0x41B2, 0x4844, 0x4F7E, 0x5771, 0x602F, 0x69CE, 0x7462, 0x7FFF};
        inline constexpr std::int8_t kAdpcmIndex[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

        /// One sample of the hardware's: from `pcm` and `index`, by `code`.
        inline void adpcmStep(int &pcm, int &index, int code)
        {
            const int step = kAdpcmSteps[index];
            int diff = step >> 3;
            if (code & 1)
                diff += step >> 2;
            if (code & 2)
                diff += step >> 1;
            if (code & 4)
                diff += step;
            if (code & 8)
                pcm = pcm - diff < -0x7FFF ? -0x7FFF : pcm - diff;
            else
                pcm = pcm + diff > 0x7FFF ? 0x7FFF : pcm + diff;
            index += kAdpcmIndex[code & 7];
            index = index < 0 ? 0 : (index > 88 ? 88 : index);
        }
    } // namespace detail

    /// Where the encoder is, from one packet to the next (each packet's head
    /// says it again, so that a packet is whole by itself). Begun as
    /// `VoiceEncoder{}`. (Nothing to build: it is kept in a DSi's own memory,
    /// which a DS has none of, and builds nothing at start-up.)
    struct VoiceEncoder
    {
        int pcm;
        int index;
    };

    /// `count` samples (even) into `out` (4 + count / 2 bytes).
    inline void voiceEncode(VoiceEncoder &enc, const std::int16_t *samples, int count, std::uint8_t *out)
    {
        out[0] = static_cast<std::uint8_t>(enc.pcm & 0xFF);
        out[1] = static_cast<std::uint8_t>((enc.pcm >> 8) & 0xFF);
        out[2] = static_cast<std::uint8_t>(enc.index);
        out[3] = 0;
        for (int i = 0; i < count; ++i)
        {
            const int step = detail::kAdpcmSteps[enc.index];
            int diff = samples[i] - enc.pcm;
            int code = 0;
            if (diff < 0)
            {
                code = 8;
                diff = -diff;
            }
            // (Each bit as the hardware adds it: step, a half, a quarter.)
            if (diff >= step)
            {
                code |= 4;
                diff -= step;
            }
            if (diff >= step >> 1)
            {
                code |= 2;
                diff -= step >> 1;
            }
            if (diff >= step >> 2)
                code |= 1;
            detail::adpcmStep(enc.pcm, enc.index, code);
            if (i & 1)
                out[4 + i / 2] = static_cast<std::uint8_t>(out[4 + i / 2] | code << 4);
            else
                out[4 + i / 2] = static_cast<std::uint8_t>(code);
        }
    }

    /// What the hardware plays of a packet: `count` samples into `out`. False
    /// if its head is no head (a step out of range).
    inline bool voiceDecode(const std::uint8_t *in, int count, std::int16_t *out)
    {
        int pcm = static_cast<std::int16_t>(in[0] | in[1] << 8);
        int index = in[2];
        if (index > 88 || in[3] != 0)
            return false;
        for (int i = 0; i < count; ++i)
        {
            const int code = (in[4 + i / 2] >> ((i & 1) * 4)) & 0xF;
            detail::adpcmStep(pcm, index, code);
            out[i] = static_cast<std::int16_t>(pcm);
        }
        return true;
    }
} // namespace hldata
