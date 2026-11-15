#include "audio/Voice.hpp"

#ifndef HLDS_CHILD

#include <cstring>
#include <nds.h>

#include "audio/Arm7Audio.hpp"
#include "net/MapShare.hpp" // (HL_TWL_CODE)
#include "net/NetRelay.hpp"
#include "Global.hpp"
#include "HlAudioLink.hpp"
#include "HlSettings.hpp"
#include "Settings.hpp"

#ifdef HLDS_VOICE_DUMP
#include <cstdio>
#endif

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
            hldata::VoiceLeveller leveller; ///< its level, brought up (HlVoice.hpp: voiceLevel)
            std::int16_t cut[kVoiceSamples];
            std::uint8_t number; ///< the next packet sent's
        };
        TWL_BSS State s;

        // What others say, heard: two at once at most, each its packets put
        // in order (VoiceJitter) and played one after the other on a
        // channel of the game's sound -- one playing, the next queued behind
        // it to begin with no gap, from buffers of their own (the ARM7 reads
        // them while they play).
        constexpr int kTalkers = 2;
        constexpr int kBuffers = 4;
        /// Silent this long, a talker's place is free for another.
        constexpr std::uint32_t kTalkerGoneMs = 1000;
        /// Heard within this long: shown as speaking.
        constexpr std::uint32_t kSpeakingMs = 300;
        struct Talker
        {
            int player; ///< -1: nobody
            std::uint32_t lastMs;
            std::uint16_t playing, queued; ///< (tokens)
            int nextBuffer;
            hldata::VoiceJitter jitter;
            alignas(32) std::uint8_t buffers[kBuffers][kVoiceAdpcmBytes];
        };
        TWL_BSS Talker talkers[kTalkers];
        TWL_BSS bool heardBegun;
        TWL_BSS std::uint8_t heardPacket[kVoiceAdpcmBytes];

        std::uint32_t nowMs() { return Global::vblanks() * 50u / 3u; }

        hldata::VoiceMicRing &seen() { return static_cast<Ring *>(memUncached(&ring))->r; }

#ifdef HLDS_VOICE_DUMP
        // A test build's record of a talk, to measure on a console: each
        // packet as heard (left) and as sent, levelled (right), 16-bit at the
        // packet's rate, the first 8 s of each talk; written to voice.wav as
        // TALK is let go.
        constexpr int kDumpSamples = 8 * 8192;
        TWL_BSS std::int16_t dumpPairs[kDumpSamples * 2];
        TWL_BSS int dumpCount;
        TWL_BSS std::uint16_t dumpRate;

        HL_TWL_CODE void dumpWrite()
        {
            if (dumpCount == 0)
                return;
            std::FILE *f = std::fopen(global.dataPath("voice.wav").c_str(), "wb");
            if (f == nullptr)
                return;
            const std::uint32_t data = static_cast<std::uint32_t>(dumpCount) * 4;
            const std::uint32_t rate = dumpRate != 0 ? dumpRate : 8182;
            std::uint8_t head[44];
            auto put32 = [&](int at, std::uint32_t v) { for (int i = 0; i < 4; ++i) head[at + i] = static_cast<std::uint8_t>(v >> (8 * i)); };
            auto put16 = [&](int at, std::uint16_t v) { head[at] = static_cast<std::uint8_t>(v); head[at + 1] = static_cast<std::uint8_t>(v >> 8); };
            std::memcpy(head, "RIFF", 4);
            put32(4, 36 + data);
            std::memcpy(head + 8, "WAVEfmt ", 8);
            put32(16, 16);
            put16(20, 1);
            put16(22, 2);
            put32(24, rate);
            put32(28, rate * 4);
            put16(32, 4);
            put16(34, 16);
            std::memcpy(head + 36, "data", 4);
            put32(40, data);
            std::fwrite(head, 1, sizeof(head), f);
            std::fwrite(dumpPairs, 4, static_cast<std::size_t>(dumpCount), f);
            std::fclose(f);
            dumpCount = 0;
        }
#endif

        HL_TWL_CODE void begin()
        {
            hldata::VoiceMicRing &r = seen();
            r.write = 0;
            r.rate = 0;
            s.read = 0;
            s.encoder = hldata::VoiceEncoder{};
            hldata::voiceLevelBegin(s.leveller);
            s.on = true;
            // (The ARM7 powers the microphone itself: libnds's
            // soundMicPowerOn holds its interrupts off for 20 ms, and the
            // sound stutters.)
            arm7Audio.micOn(&ring.r);
        }

        HL_TWL_CODE void end()
        {
            if (!s.on)
                return;
            arm7Audio.micOff();
            s.on = false;
#ifdef HLDS_VOICE_DUMP
            dumpWrite();
#endif
        }

        /// What was heard, a packet at a time. At the DSi's faster output
        /// rate the microphone's 11901 Hz is brought down to two thirds of
        /// it, 7934 Hz (two samples of every three: the first, and the mean
        /// of the other two), so that a voice costs the same whichever rate
        /// the console plays at.
        HL_TWL_CODE void cutPackets()
        {
            hldata::VoiceMicRing &r = seen();
            const std::uint32_t write = r.write;
            const std::uint32_t heardRate = r.rate;
            const bool fast = heardRate > 10000;
            const std::uint32_t need = fast ? kVoiceSamples * 3 / 2 : kVoiceSamples;
            const std::uint16_t rate = static_cast<std::uint16_t>(fast ? heardRate * 2 / 3 : heardRate);
            // (Too far behind: the oldest let go of, what is newest kept.)
            if (write - s.read > kVoiceMicRing - need)
                s.read = write - need;
            while (write - s.read >= need && heardRate != 0)
            {
                auto at = [&](std::uint32_t i) -> int { return r.samples[(s.read + i) % kVoiceMicRing]; };
                if (fast)
                {
                    for (std::uint32_t k = 0; k < kVoiceSamples / 2; ++k)
                    {
                        s.cut[2 * k] = static_cast<std::int16_t>(at(3 * k));
                        s.cut[2 * k + 1] = static_cast<std::int16_t>((at(3 * k + 1) + at(3 * k + 2)) >> 1);
                    }
                }
                else
                {
                    for (std::uint32_t i = 0; i < kVoiceSamples; ++i)
                        s.cut[i] = static_cast<std::int16_t>(at(i));
                }
                s.read += need;
                // (No room: the oldest waiting goes -- what is late is not worth sending.)
                if (s.count == kPackets)
                {
                    s.head = (s.head + 1) % kPackets;
                    --s.count;
                }
                Packet &p = s.packets[(s.head + s.count) % kPackets];
                // (Brought up to be heard: a DSi's microphone gives a voice
                // well under full scale, and nothing after made it louder.)
#ifdef HLDS_VOICE_DUMP
                std::int16_t heard[kVoiceSamples];
                std::memcpy(heard, s.cut, sizeof(heard));
#endif
                hldata::voiceLevel(s.leveller, s.cut, kVoiceSamples);
#ifdef HLDS_VOICE_DUMP
                dumpRate = rate;
                for (int i = 0; i < kVoiceSamples && dumpCount < kDumpSamples; ++i, ++dumpCount)
                {
                    dumpPairs[2 * dumpCount] = heard[i];
                    dumpPairs[2 * dumpCount + 1] = s.cut[i];
                }
#endif
                hldata::voiceEncode(s.encoder, s.cut, kVoiceSamples, p.bytes);
                p.rate = rate;
                ++s.count;
            }
        }

        HL_TWL_CODE void silence(Talker &t)
        {
            if (t.queued != 0)
                arm7Audio.stop(t.queued);
            if (t.playing != 0)
                arm7Audio.stop(t.playing);
            t.playing = t.queued = 0;
            t.player = -1;
            hldata::voiceJitterClear(t.jitter);
        }

        HL_TWL_CODE void silenceAll()
        {
            for (Talker &t : talkers)
                if (t.player >= 0)
                    silence(t);
        }

        /// What has come, each to its talker's place.
        HL_TWL_CODE void hear(int me, std::uint32_t now)
        {
            int player = 0, number = 0, rate = 0;
            while (hlrt::netrelay::takeVoice(player, number, rate, heardPacket))
            {
                if (player == me)
                    continue;
                Talker *place = nullptr;
                for (Talker &t : talkers)
                    if (t.player == player)
                        place = &t;
                for (Talker &t : talkers)
                {
                    if (place != nullptr)
                        break;
                    if (t.player < 0 || (now - t.lastMs > kTalkerGoneMs && t.playing == 0 && t.queued == 0))
                    {
                        if (t.player >= 0)
                            silence(t);
                        place = &t;
                    }
                }
                if (place == nullptr)
                    continue; // (two talk already)
                place->player = player;
                place->lastMs = now;
                hldata::voiceJitterPut(place->jitter, number, rate, heardPacket, now);
            }
        }

        /// A talker's next packet, played (behind the one playing, if one
        /// is). False if there was none, or no channel.
        HL_TWL_CODE bool feed(Talker &t, std::uint32_t now)
        {
            const bool dry = t.playing == 0;
            const hldata::VoiceJitter::Slot *next = hldata::voiceJitterTake(t.jitter, now, dry);
            if (next == nullptr)
                return false;
            std::uint8_t *buffer = t.buffers[t.nextBuffer];
            t.nextBuffer = (t.nextBuffer + 1) % kBuffers;
            std::memcpy(buffer, next->bytes, kVoiceAdpcmBytes);
            DC_FlushRange(buffer, kVoiceAdpcmBytes);
            const int volume = settings.volume[hldata::kVolumeVoices] * 255 / hldata::kVolumeMost;
            constexpr std::uint32_t kWords = kVoiceAdpcmBytes / 4;
            if (dry)
                t.playing = arm7Audio.play(buffer, kWords, hldata::kAudioNoLoop, next->rate, volume, 128, 1024, true);
            else
                t.queued = arm7Audio.chain(t.playing, buffer, kWords, hldata::kAudioNoLoop, next->rate, volume, 128,
                                           1024);
            return true;
        }

        /// Each talker's packets kept going: one playing, the next queued.
        HL_TWL_CODE void play(std::uint32_t now)
        {
            for (Talker &t : talkers)
            {
                if (t.player < 0)
                    continue;
                // (The queued one begun: it is the one playing now. The one
                // playing ended with none behind it: dry.)
                if (t.queued != 0 && !arm7Audio.queued(t.queued))
                {
                    t.playing = t.queued;
                    t.queued = 0;
                }
                if (t.playing != 0 && !arm7Audio.playing(t.playing))
                    t.playing = 0;
                if (t.playing == 0)
                    feed(t, now);
                if (t.playing != 0 && t.queued == 0)
                    feed(t, now);
            }
        }
    } // namespace

    HL_TWL_CODE void frame(bool talking, bool hearing, int me)
    {
        if (!isDSiMode() || !arm7Audio.up())
            return;
        // (The places begun empty: nobody in them -- not player 0.)
        if (!heardBegun)
        {
            heardBegun = true;
            for (Talker &t : talkers)
                t.player = -1;
        }
        talking = talking && hearing;
        if (talking && !s.on)
            begin();
        else if (!talking && s.on)
            end();
        if (s.on)
            cutPackets();
        // (Only the host's word counts with the relay.)
        hlrt::netrelay::allowVoice(hearing);
        if (!hearing)
        {
            s.count = 0;
            s.head = 0;
            silenceAll();
            int player = 0, number = 0, rate = 0;
            while (hlrt::netrelay::takeVoice(player, number, rate, heardPacket))
            {
            }
            return;
        }
        // What was made, sent.
        for (; s.count > 0; --s.count)
        {
            const Packet &p = s.packets[s.head];
            hlrt::netrelay::sendVoice(me, s.number++, p.rate, p.bytes);
            s.head = (s.head + 1) % kPackets;
        }
        const std::uint32_t now = nowMs();
        hear(me, now);
        play(now);
    }

    HL_TWL_CODE int speaking()
    {
        if (!isDSiMode())
            return -1;
        const std::uint32_t now = nowMs();
        for (const Talker &t : talkers)
            if (t.player >= 0 && (t.playing != 0 || now - t.lastMs < kSpeakingMs))
                return t.player;
        return -1;
    }
} // namespace hlrt::voice

#endif
