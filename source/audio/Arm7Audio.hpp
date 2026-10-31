#pragma once

// The ARM9's side of the game's own sound on the ARM7 (arm7/source/Audio.cpp):
// what Sound asks -- a sound played, changed, stopped; the music's ring --
// put into the shared block (shared/HlAudioLink.hpp), and what the ARM7 says
// back -- which sounds have ended -- taken out of it once a frame.
//
// A sound is known by a token: 0 is none. The ARM7
// has fourteen channels for sounds; play() gives 0 when all of them are
// taken (the caller lets something go and asks again), counting a sound as
// taken from when it is asked for until its end is seen.

#include <cstdint>

#include "HlAudioLink.hpp"

namespace hlrt
{
    class Arm7Audio
    {
    public:
        static constexpr int kChannels = 14;

        /// The shared block made and its address sent; false if the ARM7
        /// never answered.
        bool start();
        bool up() const { return link_ != nullptr; }

        /// A clip (DS-hardware IMA-ADPCM, `words` long with its preamble;
        /// looping from `loopWord`, or kAudioNoLoop) at `rate` Hz, `volume`
        /// and `pan` 0..255, `pitch` 1024 as recorded.
        /// `menu`: it plays on while the game is paused. 0: no channel free.
        std::uint16_t play(const void *data, std::uint32_t words, std::uint32_t loopWord, std::uint16_t rate,
                           int volume, int pan, int pitch, bool menu);
        /// The same, to begin on `after`'s channel the moment `after` ends,
        /// with no gap (a line's next word). 0: none queued (nothing to
        /// follow, or already one queued).
        std::uint16_t chain(std::uint16_t after, const void *data, std::uint32_t words, std::uint32_t loopWord,
                            std::uint16_t rate, int volume, int pan, int pitch);
        void stop(std::uint16_t token);
        void setVolume(std::uint16_t token, int volume);
        void setPanning(std::uint16_t token, int pan);
        void setRate(std::uint16_t token, int pitch);
        void stopAll();
        /// Everything silent and stopped, for good: the program is leaving.
        void shutdown();
        /// Whether a sound is still being played (not ended, not stopped),
        /// or queued to be.
        bool playing(std::uint16_t token) const;
        /// Whether it is queued behind another and not begun yet.
        bool queued(std::uint16_t token) const;

        /// Once a frame: the ends the ARM7 has said.
        void frame();

        /// The pipeline (the mix captured and played again by the ARM7, the
        /// music and the room's effect added) on, or the plain mixer -- the
        /// DSi's DSP has the capture then; the room's effect (kAudioEffects*)
        /// and how the sound is laid out (kAudioOutput*) in it. Taken by the
        /// ARM7 within 2 ms.
        void setMode(bool pipeline, std::uint8_t effects, std::uint8_t output, std::uint8_t quality);
        /// The room the player is in (HlRoomPresets.hpp, 0..28).
        void setRoom(int room);
        /// Every command so far taken by the ARM7 (a few ms at most).
        void sync();

        /// The music: a ring of `blocks` of the track's stereo blocks as in
        /// its file (HlStereoMusic.hpp), which the caller keeps filled; the
        /// track `rate` Hz and `pairs` long from where it starts; `volume`
        /// 0..256. Heard while the pipeline is on.
        void musicOpen(const void *ring, std::uint32_t blocks, std::uint32_t rate, std::uint32_t pairs, int volume);
        /// Before musicOpen: the ring's count of blocks written started
        /// again, for the caller to fill it first (so that the ARM7 does not
        /// begin on an empty ring).
        void musicPrepare();
        void musicStop();
        void musicVolume(int volume);
        /// Pairs of the track played, and blocks written, since musicOpen.
        std::uint32_t musicRead() const;
        std::uint32_t musicWritten() const;
        void musicWrote(std::uint32_t blocks);
        bool musicOver() const;

        /// The send to the DSi's DSP (Enhanced): its ring, and how many
        /// samples have been put into it, all told.
        const volatile std::int16_t *send() const { return link_ != nullptr ? link_->send : nullptr; }
        std::uint32_t sendWritten() const { return link_ != nullptr ? link_->sendWrite : 0; }

        /// A DSi's speakers (DSi games only, run as one): whether there are
        /// any; their rate in kHz (32 or 47, changed only while nothing is
        /// heard); and the console's sound's share of them against the
        /// DSP's, in eighths. Both are sent every time, by way of the ARM7's
        /// libnds: soundEnable() puts them back to 32 and 8 behind our back.
        static bool dsiSpeakers();
        void setDsiRate(int khz);
        void setDsiRatio(int ratio);

        /// The ARM7's own figures (the debug readout).
        std::uint32_t late() const;
        std::uint32_t loadPercent() const;
        std::uint32_t musicDry() const;

    private:
        void push(const hldata::AudioCommand &command);
        hldata::AudioPlay playOf(const void *data, std::uint32_t words, std::uint32_t loopWord, std::uint16_t rate,
                                 int volume, int pan, int pitch, bool menu) const;
        std::uint16_t nextToken();

        struct Queued
        {
            std::uint16_t token = 0, after = 0;
        };
        static constexpr int kQueued = 4;
        Queued queued_[kQueued];

        hldata::AudioLink *link_ = nullptr; ///< through the uncached mirror
        std::uint32_t endRead_ = 0;
        std::uint16_t next_ = 1;
        std::uint16_t live_[kChannels] = {};
        int dsiKhz_ = 32, dsiRatio_ = 8;
    };

    extern Arm7Audio arm7Audio;
} // namespace hlrt
