// The game's sound on the ARM7: what the ARM9 asks for through the shared
// block (shared/HlAudioLink.hpp), done once a block -- 64 samples of the
// hardware's own rate, 2 ms, a timer interrupt locked to the sound clock
// (tools/hwtest/routing: never late on a DS or a DSi over minutes).
//
// Each sound is played on a hardware channel of its own, which decodes the
// clip's IMA-ADPCM, keeps its pitch and pans it; this program finds the
// channel, starts it, changes it, stops it, and says when it has ended -- to
// the block, by the token the ARM9 gave it -- so the ARM9 knows when a voice
// is free and a word has been said, and never guesses by the clock.
//
// The channels: 1 and 3 are the pipeline's (Pipeline.arm.cpp: the mix
// captured and played again, with the music added) -- or, with the pipeline
// off, 1's clock is what the DSi's capture for the DSP's echo keeps time by
// (libnds's sound messages, from the ARM9); the other fourteen are the
// sounds'.

#include <nds.h>

#include "Audio.hpp"
#include "HlAudioLink.hpp"
#include "Pipeline.hpp"

using namespace hldata;

namespace hlaudio
{
    namespace
    {
        constexpr int kChannelCount = 14;
        constexpr std::uint8_t kChannels[kChannelCount] = {0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

        struct Channel
        {
            std::uint16_t token = 0; ///< 0: free
            bool ui = false;
            std::uint8_t volume = 0, pan = 64;
            std::uint16_t rate = 0, pitch = 1024;
            /// What begins on this channel the moment this ends (a Chain):
            /// its token (0: nothing) and its Play.
            std::uint16_t nextToken = 0;
            AudioPlay next;
        };

        AudioLink *volatile link = nullptr;
        Channel channels[kChannelCount];
        volatile std::uint32_t pending = 0; ///< blocks the timer has counted, not yet done
        bool paused = false;


        void linkHandler(u32 value, void *)
        {
            link = reinterpret_cast<AudioLink *>(value);
        }

        void blockTick()
        {
            pending = pending + 1;
        }

        /// The bus clock, in 64s of its ticks: timer 1 alone (a 16-bit count
        /// round in 125 ms, longer than any block takes). Not timer 2: the
        /// DSi's wireless takes it (LIBNDS_DEFAULT_TIMER_WIFI, its update
        /// every millisecond) once it is started.
        constexpr std::uint32_t kStampTicks = 64;
        std::uint16_t stamp() { return TIMER_DATA(1); }

        int channelOf(std::uint16_t token)
        {
            for (int c = 0; c < kChannelCount; ++c)
            {
                if (channels[c].token == token && token != 0)
                    return c;
            }
            return -1;
        }

        /// The channel whose sound `token` will begin when its own ends.
        int queuedOn(std::uint16_t token)
        {
            for (int c = 0; c < kChannelCount; ++c)
            {
                if (channels[c].nextToken == token && token != 0)
                    return c;
            }
            return -1;
        }

        void release(int c, bool stopped)
        {
            const int n = kChannels[c];
            if (stopped)
                SCHANNEL_CR(n) = 0;
            audioEnd(*link, static_cast<std::uint8_t>(n), channels[c].token, stopped);
            // (What was to follow it does not.)
            if (channels[c].nextToken != 0)
                audioEnd(*link, static_cast<std::uint8_t>(n), channels[c].nextToken, true);
            link->voiceToken[n] = 0;
            channels[c] = Channel();
        }

        /// What the channel's timer is now: its pitch, or as slow as the
        /// hardware goes while the game stands still (the menu's own play on).
        std::uint16_t timerOf(const Channel &ch)
        {
            return paused && !ch.ui ? 0 : audioTimer(ch.rate, ch.pitch);
        }
        std::uint8_t volumeOf(const Channel &ch)
        {
            return paused && !ch.ui ? 0 : ch.volume;
        }

        void begin(int c, std::uint16_t token, const AudioPlay &p);

        void play(const AudioCommand &command)
        {
            const AudioPlay p = audioPlayOf(command);
            int c = -1;
            for (int k = 0; k < kChannelCount && c < 0; ++k)
                c = channels[k].token == 0 ? k : -1;
            if (c < 0)
            {
                // (The ARM9 never asks for more than there are; if it does,
                // the first that is not the menu's gives way.)
                for (int k = 0; k < kChannelCount && c < 0; ++k)
                    c = channels[k].ui ? -1 : k;
                if (c < 0)
                    c = 0;
                release(c, true);
            }
            begin(c, command.token, p);
        }

        /// A sound begun on a channel (free, or whose last has just ended).
        void begin(int c, std::uint16_t token, const AudioPlay &p)
        {
            if (p.words < 4 || p.address == 0)
            {
                audioEnd(*link, static_cast<std::uint8_t>(kChannels[c]), token, false);
                channels[c].token = 0;
                link->voiceToken[kChannels[c]] = 0;
                return;
            }
            Channel &ch = channels[c];
            ch.token = token;
            ch.ui = (p.flags & kAudioPlayUi) != 0;
            ch.volume = p.volume;
            ch.pan = p.pan;
            ch.rate = p.rate;
            ch.pitch = p.pitch;
            const int n = kChannels[c];
            const bool loops = p.loopWord != kAudioNoLoop && p.loopWord < p.words;
            SCHANNEL_CR(n) = 0;
            SCHANNEL_SOURCE(n) = p.address;
            SCHANNEL_TIMER(n) = timerOf(ch);
            SCHANNEL_REPEAT_POINT(n) = loops ? p.loopWord : 0;
            SCHANNEL_LENGTH(n) = loops ? p.words - p.loopWord : p.words;
            SCHANNEL_CR(n) = static_cast<std::uint32_t>(volumeOf(ch)) | static_cast<std::uint32_t>(ch.pan) << 16 |
                             (loops ? 1u : 2u) << 27 | 2u << 29 | 1u << 31;
            link->voiceToken[n] = token;
        }

        /// A sound to follow `command.e` on its channel; at once if that
        /// has ended already.
        void chain(const AudioCommand &command)
        {
            const int c = channelOf(static_cast<std::uint16_t>(command.e));
            if (c < 0)
            {
                play(command);
                return;
            }
            Channel &ch = channels[c];
            if (ch.nextToken != 0)
                audioEnd(*link, static_cast<std::uint8_t>(kChannels[c]), ch.nextToken, true);
            ch.nextToken = command.token;
            ch.next = audioPlayOf(command);
        }

        void set(const AudioCommand &command)
        {
            const int c = channelOf(command.token);
            if (c < 0)
            {
                // (One yet to begin: what it will begin with.)
                const int q = queuedOn(command.token);
                if (q < 0)
                    return;
                AudioPlay &p = channels[q].next;
                const std::uint32_t what = command.a >> 16;
                if (what & kAudioSetVolume)
                    p.volume = static_cast<std::uint8_t>(command.a & 127);
                if (what & kAudioSetPan)
                    p.pan = static_cast<std::uint8_t>((command.a >> 8) & 127);
                if (what & kAudioSetPitch)
                    p.pitch = static_cast<std::uint16_t>(command.b);
                return;
            }
            Channel &ch = channels[c];
            const int n = kChannels[c];
            const std::uint32_t what = command.a >> 16;
            if (what & kAudioSetVolume)
            {
                ch.volume = static_cast<std::uint8_t>(command.a & 127);
                SCHANNEL_VOL(n) = volumeOf(ch);
            }
            if (what & kAudioSetPan)
            {
                ch.pan = static_cast<std::uint8_t>((command.a >> 8) & 127);
                SCHANNEL_PAN(n) = ch.pan;
            }
            if (what & kAudioSetPitch)
            {
                ch.pitch = static_cast<std::uint16_t>(command.b);
                SCHANNEL_TIMER(n) = timerOf(ch);
            }
        }

        void pause(bool on)
        {
            paused = on;
            for (int c = 0; c < kChannelCount; ++c)
            {
                if (channels[c].token == 0 || channels[c].ui)
                    continue;
                const int n = kChannels[c];
                SCHANNEL_VOL(n) = volumeOf(channels[c]);
                SCHANNEL_TIMER(n) = timerOf(channels[c]);
            }
        }

        void run(std::uint32_t blocks)
        {
            AudioLink &l = *link;
            AudioCommand command;
            bool restarted = false;
            while (audioPop(l, command))
            {
                switch (command.op)
                {
                case AudioOp::Play:
                    play(command);
                    break;
                case AudioOp::Set:
                    set(command);
                    break;
                case AudioOp::Chain:
                    chain(command);
                    break;
                case AudioOp::Stop:
                {
                    const int c = channelOf(command.token);
                    if (c >= 0)
                    {
                        release(c, true);
                        break;
                    }
                    const int q = queuedOn(command.token);
                    if (q >= 0)
                    {
                        audioEnd(l, static_cast<std::uint8_t>(kChannels[q]), command.token, true);
                        channels[q].nextToken = 0;
                    }
                    break;
                }
                case AudioOp::Pause:
                    pause(command.a != 0);
                    break;
                case AudioOp::Master:
                    REG_SOUNDCNT = static_cast<std::uint16_t>((REG_SOUNDCNT & ~0x7F) | (command.a & 0x7F));
                    break;
                case AudioOp::Room:
                    pipelineRoom(static_cast<int>(command.a));
                    break;
                case AudioOp::Mode:
                {
                    const std::uint8_t quality = static_cast<std::uint8_t>((command.a >> 16) & 0xFF);
                    // (Another rate: set going again at it.)
                    if (pipelineOn() && pipelineRateChanges(quality))
                        pipelineStop(l);
                    pipelineMode(static_cast<std::uint8_t>(command.a & 0xFF),
                                 static_cast<std::uint8_t>((command.a >> 8) & 0xFF), quality);
                    if (command.a & kAudioPipeline)
                    {
                        if (!pipelineOn())
                        {
                            pipelineStart(l);
                            // (The blocks counted so far were before it.)
                            restarted = true;
                            const int ime = enterCriticalSection();
                            pending = 0;
                            leaveCriticalSection(ime);
                        }
                    }
                    else
                    {
                        pipelineStop(l);
                    }
                    break;
                }
                case AudioOp::MusicOpen:
                    musicOpen(l, command);
                    break;
                case AudioOp::MusicStop:
                    musicStop(l);
                    break;
                case AudioOp::MusicVolume:
                    musicVolume(static_cast<int>(command.a));
                    break;
                case AudioOp::Shutdown:
                    for (int c = 0; c < kChannelCount; ++c)
                    {
                        if (channels[c].token != 0)
                            release(c, true);
                    }
                    pipelineStop(l);
                    musicStop(l);
                    REG_SNDCAP0CNT = 0;
                    REG_SNDCAP1CNT = 0;
                    for (int n = 0; n < 16; ++n)
                        SCHANNEL_CR(n) = 0;
                    TIMER_CR(0) = 0;
                    TIMER_CR(1) = 0;
                    break;
                case AudioOp::StopAll:
                    for (int c = 0; c < kChannelCount; ++c)
                    {
                        if (channels[c].token != 0)
                            release(c, true);
                    }
                    break;
                default:
                    break;
                }
            }
            // The sounds that ended on their own -- and, where one was to
            // follow, it begun there and then.
            for (int c = 0; c < kChannelCount; ++c)
            {
                Channel &ch = channels[c];
                if (ch.token == 0 || (SCHANNEL_CR(kChannels[c]) & (1u << 31)))
                    continue;
                if (ch.nextToken == 0)
                {
                    release(c, false);
                    continue;
                }
                audioEnd(l, static_cast<std::uint8_t>(kChannels[c]), ch.token, false);
                const std::uint16_t token = ch.nextToken;
                const AudioPlay next = ch.next;
                ch.nextToken = 0;
                begin(c, token, next);
            }
            if (!restarted)
                pipelineRun(l, blocks);
        }
    } // namespace

    void install()
    {
        fifoSetValue32Handler(FIFO_USER_01, linkHandler, nullptr);
        // The bus clock, for the load figures.
        TIMER_CR(1) = 0;
        TIMER_DATA(1) = 0;
        TIMER_CR(1) = TIMER_ENABLE | TIMER_DIV_64;
        // A block every 65536 bus ticks: 64 samples at 32728 Hz.
        irqSet(IRQ_TIMER0, blockTick);
        irqEnable(IRQ_TIMER0);
        TIMER_DATA(0) = 0;
        TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1 | TIMER_IRQ_REQ;
    }

    void mark(unsigned what)
    {
        AudioLink *l = link;
        if (l != nullptr)
            l->where = what;
    }

    unsigned marked()
    {
        AudioLink *l = link;
        return l != nullptr ? l->where : 0;
    }

    void waitAndRun()
    {
        while (pending == 0)
            swiIntrWait(1, IRQ_TIMER0);
        const int ime = enterCriticalSection();
        const std::uint32_t blocks = pending;
        pending = 0;
        leaveCriticalSection(ime);
        AudioLink *l = link;
        if (l == nullptr)
            return;
        const std::uint16_t began = stamp();
        const std::uint32_t was = l->where;
        l->where = 3;
        run(blocks);
        l->where = was;
        const std::uint32_t took = static_cast<std::uint32_t>(static_cast<std::uint16_t>(stamp() - began)) * kStampTicks;
        l->blocks = l->blocks + blocks;
        if (blocks > 1)
            l->late = l->late + (blocks - 1);
        l->cpuAvg = l->cpuAvg - (l->cpuAvg >> 4) + (took >> 4);
        if (took > l->cpuMost)
            l->cpuMost = took;
    }
} // namespace hlaudio
