#include "audio/Voice.hpp"

#ifndef HLDS_CHILD

#include <cstring>
#include <nds.h>

#include "audio/Arm7Audio.hpp"
#include "net/MapShare.hpp" // (HL_TWL_CODE)
#include "HlAudioLink.hpp"

namespace hlrt::voice
{
    namespace
    {
        using hldata::kVoiceAdpcmBytes;
        using hldata::kVoiceMicRing;
        using hldata::kVoiceSamples;

        // What the ARM7 writes (its own address given to it; read here
        // through the mirror the cache has nothing of), and how far it has
        // been read; what was made of it, waiting to be sent.
        // (Whole cache lines of its own: a neighbour written through the
        // cache would otherwise write back what it holds of the ring, over
        // what the ARM7 wrote since.)
        struct alignas(32) Ring
        {
            hldata::VoiceMicRing r;
        };
        TWL_BSS Ring ring;
        constexpr int kPackets = 6;
        struct Packet
        {
            std::uint8_t bytes[kVoiceAdpcmBytes];
            std::uint16_t rate;
        };
        struct State
        {
            bool on;
            std::uint32_t read;
            int head, count; ///< (the waiting packets: oldest at head)
            Packet packets[kPackets];
            hldata::VoiceEncoder encoder;
            std::int16_t cut[kVoiceSamples];
        };
        TWL_BSS State s;

        hldata::VoiceMicRing &seen() { return static_cast<Ring *>(memUncached(&ring))->r; }

        HL_TWL_CODE void begin()
        {
            hldata::VoiceMicRing &r = seen();
            r.write = 0;
            r.rate = 0;
            s.read = 0;
            s.encoder = hldata::VoiceEncoder{};
            s.on = true;
            soundMicPowerOn();
            arm7Audio.micOn(&ring.r);
        }

        HL_TWL_CODE void end()
        {
            if (!s.on)
                return;
            arm7Audio.micOff();
            soundMicPowerOff();
            s.on = false;
        }

        /// What was heard, a packet at a time.
        HL_TWL_CODE void cutPackets()
        {
            hldata::VoiceMicRing &r = seen();
            const std::uint32_t write = r.write;
            // (Too far behind: the oldest let go of, what is newest kept.)
            if (write - s.read > kVoiceMicRing - kVoiceSamples)
                s.read = write - kVoiceSamples;
            while (write - s.read >= static_cast<std::uint32_t>(kVoiceSamples) && r.rate != 0)
            {
                for (int i = 0; i < kVoiceSamples; ++i)
                    s.cut[i] = r.samples[(s.read + static_cast<std::uint32_t>(i)) % kVoiceMicRing];
                s.read += kVoiceSamples;
                // (No room: the oldest waiting goes -- what is late is not worth sending.)
                if (s.count == kPackets)
                {
                    s.head = (s.head + 1) % kPackets;
                    --s.count;
                }
                Packet &p = s.packets[(s.head + s.count) % kPackets];
                hldata::voiceEncode(s.encoder, s.cut, kVoiceSamples, p.bytes);
                p.rate = static_cast<std::uint16_t>(r.rate);
                ++s.count;
            }
        }
    } // namespace

    HL_TWL_CODE void frame(bool talking)
    {
        if (!isDSiMode() || !arm7Audio.up())
            return;
        if (talking && !s.on)
            begin();
        else if (!talking && s.on)
            end();
        if (s.on)
            cutPackets();
    }

    HL_TWL_CODE bool take(std::uint8_t *packet, std::uint16_t &rate)
    {
        if (!isDSiMode() || s.count == 0)
            return false;
        const Packet &p = s.packets[s.head];
        std::memcpy(packet, p.bytes, kVoiceAdpcmBytes);
        rate = p.rate;
        s.head = (s.head + 1) % kPackets;
        --s.count;
        return true;
    }

    HL_TWL_CODE void stop()
    {
        if (!isDSiMode())
            return;
        end();
        s.count = 0;
        s.head = 0;
    }

    bool on() { return isDSiMode() && s.on; }
} // namespace hlrt::voice

#endif
