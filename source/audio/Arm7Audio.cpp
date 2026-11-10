#include "audio/Arm7Audio.hpp"
#include "Global.hpp"

#include <cstring>
#include <malloc.h>

#include <nds.h>

namespace hlrt
{
    Arm7Audio arm7Audio;

    using namespace hldata;

    bool Arm7Audio::start()
    {
        if (link_ != nullptr)
            return true;
        // (On cache lines of its own; reached only through the uncached
        // mirror from here on, so that what the ARM7 writes is what is read.
        // In DS mode the ARM7 is given that same address, not the block's
        // own: on a DS it is the same memory again -- but a 3DS that has started this by
        // Download Play can leave its RAM laid out as 16 MB, and then the
        // "mirror" is other memory, which the two must then share instead:
        // the ARM7 heard none of what was written there, 2026-10-30.)
        void *block = memalign(32, (sizeof(AudioLink) + 31) & ~31u);
        if (block == nullptr)
            return false;
        DC_FlushRange(block, sizeof(AudioLink));
        AudioLink *link = static_cast<AudioLink *>(memUncached(block));
        std::memset(link, 0, sizeof(AudioLink));
        // (In DSi mode the mirror is at 0x0C000000, there to the ARM9 alone:
        // the ARM7 is given the block's own address, as it always was.)
        fifoSendValue32(FIFO_USER_01, reinterpret_cast<std::uint32_t>(isDSiMode() ? block : static_cast<void *>(link)));
        // The ARM7 answers by counting its blocks (2 ms each).
        for (int frame = 0; frame < 30; ++frame)
        {
            if (link->blocks != 0)
            {
                link_ = link;
                return true;
            }
            swiWaitForVBlank();
        }
        return false;
    }

    void Arm7Audio::push(const AudioCommand &command)
    {
        if (link_ == nullptr)
            return;
        // (Full: the ARM7 takes them every 2 ms; a frame's worth more than
        // fits only in a storm, and then the moment is waited out.)
        for (int spin = 0; spin < 20000 && !audioPush(*link_, command); ++spin)
            ;
    }

    std::uint16_t Arm7Audio::nextToken()
    {
        const std::uint16_t token = next_;
        next_ = static_cast<std::uint16_t>(next_ + 1 == 0 ? 1 : next_ + 1);
        return token;
    }

    AudioPlay Arm7Audio::playOf(const void *data, std::uint32_t words, std::uint32_t loopWord, std::uint16_t rate,
                                int volume, int pan, int pitch, bool menu) const
    {
        AudioPlay p;
        p.address = reinterpret_cast<std::uint32_t>(data);
        p.words = static_cast<std::uint16_t>(words > 0xFFFF ? 0xFFFF : words);
        p.loopWord = loopWord < words && loopWord < kAudioNoLoop ? static_cast<std::uint16_t>(loopWord) : kAudioNoLoop;
        p.rate = rate;
        p.volume = static_cast<std::uint8_t>((volume < 0 ? 0 : (volume > 255 ? 255 : volume)) >> 1);
        p.pan = static_cast<std::uint8_t>((pan < 0 ? 0 : (pan > 255 ? 255 : pan)) >> 1);
        p.pitch = static_cast<std::uint16_t>(pitch < 1 ? 1 : (pitch > 0xFFFF ? 0xFFFF : pitch));
        p.flags = menu ? kAudioPlayUi : 0;
        return p;
    }

#ifndef HLDS_CHILD
    // (A DSi's, in its own memory.)
    TWL_CODE __attribute__((noinline)) void Arm7Audio::micOn(void *ring)
    {
        AudioCommand c;
        c.op = AudioOp::MicOn;
        c.a = reinterpret_cast<std::uint32_t>(ring);
        push(c);
    }

    TWL_CODE __attribute__((noinline)) void Arm7Audio::micOff()
    {
        AudioCommand c;
        c.op = AudioOp::MicOff;
        push(c);
    }
#endif

    std::uint16_t Arm7Audio::play(const void *data, std::uint32_t words, std::uint32_t loopWord, std::uint16_t rate,
                                  int volume, int pan, int pitch, bool menu)
    {
        if (link_ == nullptr || data == nullptr)
            return 0;
        int slot = -1;
        for (int k = 0; k < kChannels && slot < 0; ++k)
            slot = live_[k] == 0 ? k : -1;
        if (slot < 0)
            return 0;
        const std::uint16_t token = nextToken();
        live_[slot] = token;
        push(audioPlay(0, token, playOf(data, words, loopWord, rate, volume, pan, pitch, menu)));
        return token;
    }

    std::uint16_t Arm7Audio::chain(std::uint16_t after, const void *data, std::uint32_t words,
                                   std::uint32_t loopWord, std::uint16_t rate, int volume, int pan, int pitch)
    {
        if (link_ == nullptr || data == nullptr || !playing(after) || queued(after))
            return 0;
        int slot = -1;
        for (int k = 0; k < kQueued && slot < 0; ++k)
            slot = queued_[k].token == 0 ? k : -1;
        for (int k = 0; k < kQueued; ++k)
        {
            if (queued_[k].after == after && queued_[k].token != 0)
                return 0; // (one queued behind it already)
        }
        if (slot < 0)
            return 0;
        const std::uint16_t token = nextToken();
        queued_[slot] = Queued{token, after};
        push(audioChain(token, after, playOf(data, words, loopWord, rate, volume, pan, pitch, false)));
        return token;
    }

    bool Arm7Audio::queued(std::uint16_t token) const
    {
        for (const Queued &q : queued_)
        {
            if (q.token == token && token != 0)
                return true;
        }
        return false;
    }

    bool Arm7Audio::playing(std::uint16_t token) const
    {
        for (std::uint16_t live : live_)
        {
            if (live == token && token != 0)
                return true;
        }
        return queued(token);
    }

    void Arm7Audio::stop(std::uint16_t token)
    {
        if (!playing(token))
            return;
        for (std::uint16_t &live : live_)
        {
            if (live == token)
                live = 0;
        }
        // (What was to follow it does not: the ARM7 drops it too.)
        for (Queued &q : queued_)
        {
            if (q.token == token || q.after == token)
                q = Queued();
        }
        AudioCommand c;
        c.op = AudioOp::Stop;
        c.token = token;
        push(c);
    }

    void Arm7Audio::setVolume(std::uint16_t token, int volume)
    {
        if (!playing(token))
            return;
        AudioCommand c;
        c.op = AudioOp::Set;
        c.token = token;
        c.a = static_cast<std::uint32_t>((volume < 0 ? 0 : (volume > 255 ? 255 : volume)) >> 1) | kAudioSetVolume << 16;
        push(c);
    }

    void Arm7Audio::setPanning(std::uint16_t token, int pan)
    {
        if (!playing(token))
            return;
        AudioCommand c;
        c.op = AudioOp::Set;
        c.token = token;
        c.a = static_cast<std::uint32_t>((pan < 0 ? 0 : (pan > 255 ? 255 : pan)) >> 1) << 8 | kAudioSetPan << 16;
        push(c);
    }

    void Arm7Audio::setRate(std::uint16_t token, int pitch)
    {
        if (!playing(token))
            return;
        AudioCommand c;
        c.op = AudioOp::Set;
        c.token = token;
        c.a = kAudioSetPitch << 16;
        c.b = static_cast<std::uint32_t>(pitch < 1 ? 1 : (pitch > 0xFFFF ? 0xFFFF : pitch));
        push(c);
    }

    void Arm7Audio::shutdown()
    {
        if (link_ == nullptr)
            return;
        AudioCommand c;
        c.op = AudioOp::Shutdown;
        push(c);
        // (Taken within 2 ms; a few frames at the most.)
        for (int frame = 0; frame < 10 && link_->commandRead != link_->commandWrite; ++frame)
            swiWaitForVBlank();
    }

    void Arm7Audio::stopAll()
    {
        for (std::uint16_t &live : live_)
            live = 0;
        for (Queued &q : queued_)
            q = Queued();
        AudioCommand c;
        c.op = AudioOp::StopAll;
        push(c);
    }

    void Arm7Audio::frame()
    {
        if (link_ == nullptr)
            return;
        audioEnds(*link_, endRead_, [&](const AudioEnd &end) {
            int slot = -1;
            for (int k = 0; k < kChannels; ++k)
            {
                if (live_[k] == end.token)
                {
                    live_[k] = 0;
                    slot = k;
                }
            }
            for (Queued &q : queued_)
            {
                if (q.token == end.token)
                    q = Queued(); // (ended -- or dropped -- before it began)
                else if (q.after == end.token && q.token != 0)
                {
                    // What followed it has begun, on its channel.
                    if (!end.stopped && slot >= 0)
                        live_[slot] = q.token;
                    q = Queued();
                }
            }
        });
    }

    void Arm7Audio::setMode(bool pipeline, std::uint8_t effects, std::uint8_t output, std::uint8_t quality)
    {
        AudioCommand c;
        c.op = AudioOp::Mode;
        c.a = effects | static_cast<std::uint32_t>(output) << 8 | static_cast<std::uint32_t>(quality) << 16 |
              (pipeline ? kAudioPipeline : 0);
        push(c);
    }

    void Arm7Audio::setRoom(int room)
    {
        AudioCommand c;
        c.op = AudioOp::Room;
        c.a = static_cast<std::uint32_t>(room);
        push(c);
    }

    void Arm7Audio::sync()
    {
        for (int frame = 0; link_ != nullptr && frame < 10 && link_->commandRead != link_->commandWrite; ++frame)
            swiWaitForVBlank();
    }

    void Arm7Audio::musicOpen(const void *ring, std::uint32_t blocks, std::uint32_t rate, std::uint32_t pairs,
                              int volume)
    {
        if (link_ == nullptr)
            return;
        AudioCommand c;
        c.op = AudioOp::MusicOpen;
        c.a = reinterpret_cast<std::uint32_t>(ring);
        c.b = (blocks & 0xFFFF) | (rate & 0xFFFF) << 16;
        c.c = pairs;
        c.d = static_cast<std::uint32_t>(volume);
        push(c);
        sync(); // (so that what it says of the music is of this track)
    }

    void Arm7Audio::musicPrepare()
    {
        if (link_ == nullptr)
            return;
        // (What was written before is not this track's: counted from here.
        // The ARM7 is told nothing of this track until musicOpen.)
        AudioCommand c;
        c.op = AudioOp::MusicStop;
        push(c);
        sync();
        link_->musicWrite = 0;
    }

    void Arm7Audio::musicVolume(int volume)
    {
        AudioCommand c;
        c.op = AudioOp::MusicVolume;
        c.a = static_cast<std::uint32_t>(volume);
        push(c);
    }

    bool Arm7Audio::musicOver() const
    {
        return link_ == nullptr || link_->musicOver != 0;
    }

    void Arm7Audio::musicStop()
    {
        AudioCommand c;
        c.op = AudioOp::MusicStop;
        push(c);
    }

    std::uint32_t Arm7Audio::musicRead() const
    {
        return link_ != nullptr ? link_->musicRead : 0;
    }
    std::uint32_t Arm7Audio::musicWritten() const
    {
        return link_ != nullptr ? link_->musicWrite : 0;
    }
    void Arm7Audio::musicWrote(std::uint32_t bytes)
    {
        if (link_ != nullptr)
            link_->musicWrite = link_->musicWrite + bytes;
    }

    bool Arm7Audio::dsiSpeakers()
    {
#ifdef HLDS_CHILD
        // (Sent the game, it runs as a DS: what start-up read of a DSi --
        // SCFG left open, and a DSi header in memory that is the Download
        // Play program's, not this one's -- is not to be acted on. In DSi mode
        // too: the codec is left in its DS mode, tools/dlp_twl_child.py.)
        return false;
#else
        return isDSiMode() && (__DSiHeader->appflags & 0x01) != 0;
#endif
    }
    void Arm7Audio::setDsiRate(int khz)
    {
        if (!dsiSpeakers())
            return;
        dsiKhz_ = khz == 47 ? 47 : 32;
        soundExtSetFrequency(dsiKhz_);
        soundExtSetRatio(dsiRatio_);
    }
    void Arm7Audio::setDsiRatio(int ratio)
    {
        if (!dsiSpeakers())
            return;
        dsiRatio_ = ratio < 0 ? 0 : (ratio > 8 ? 8 : ratio);
        soundExtSetFrequency(dsiKhz_);
        soundExtSetRatio(dsiRatio_);
    }

    std::uint32_t Arm7Audio::late() const
    {
        return link_ != nullptr ? link_->late : 0;
    }
    std::uint32_t Arm7Audio::loadPercent() const
    {
        // (Of the bus ticks a block lasts.)
        if (link_ == nullptr)
            return 0;
        const std::uint32_t block = link_->blockTicks != 0 ? link_->blockTicks : 0x10000;
        return link_->cpuAvg * 100 / block;
    }
    std::uint32_t Arm7Audio::musicDry() const
    {
        return link_ != nullptr ? link_->musicDry : 0;
    }
} // namespace hlrt
