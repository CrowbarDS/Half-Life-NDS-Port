#pragma once

#include "EngineObject.hpp"

// What is heard: the map's sounds in memory, played by the game's own sound
// program on the ARM7 (arm7/source/Audio.cpp, by way of Arm7Audio), in layers.
//
// A layer (HlSound.hpp) is a kind of sound: the player's own, the world's,
// the voice, the loops of a place, the menu. Each has its share of the
// thirteen voices and gives way in its turn (HlMixer.hpp has the rules), and
// each has a volume the player sets. The music is beside them: stereo, a
// ring the ARM7 plays round, read off the card as it plays. The room the
// player is in is the ARM7's (or, Enhanced, the DSi's DSP's: DspAudio).
//
// The clips are the map's cooked sounds where they already are in memory:
// the ARM7 is told where each is, and the hardware plays the ADPCM itself,
// loops and all. A sound is known by a token (a "handle").
//
// The game's shared code says what was heard and where (a SoundQueue); this
// turns each into a volume and a side by where the camera is, plays it, and
// goes on placing it as the camera moves. A map's sounds are read into
// memory when it loads, except the words of its spoken lines: those are
// long, said once, and read off the card as they are spoken.
//
// Sound data is played by the ARM7 straight out of main memory, so nothing
// here may be freed while a voice is still on it: unload() silences
// everything first.

#include <cstdint>
#include <cstdio>

#include "HlFrameClock.hpp"
#include "HlMixer.hpp"
#include "HlUi.hpp"

namespace hlrt
{
    class Sound
    {
    public:
        /// Once, at start-up, before any map is loaded.
        void init();
        /// Read the loaded level's sounds. Call once the game is up.
        void load();
        /// Silence everything but the music, and let the level's sounds go.
        /// The music is held where it is until load() is called again.
        void unload();
        /// Every pass of the main loop: plays what the game has queued, keeps
        /// the loops and the spoken line going, and feeds the music.
        void frame();
        /// The program is leaving: nothing of the sound left running.
        void quit();
        /// The words of a match's last minute: read as a match with a time
        /// limit begins (if they are on the card; a console sent the game
        /// never has them), said, and let go of as it is left.
        void loadMatchWarning();
        void dropMatchWarning();
        void matchWarning();
        /// One of the menu's own sounds (hldata::kUiSound*).
        /// `volume` of 127 (the menu's own: 110, a little under the game).
        void ui(int which, int volume = 110);
        /// The volume settings have changed.
        void applyVolumes();
        /// The game stands still (the menu): the world, the voice, the loops
        /// and the music stop where they are, and go on from there when it
        /// does. The menu's own sounds are heard meanwhile.
        ///
        /// (A DS's sound channel cannot be stopped and taken up again: it
        /// is slowed to the slowest the hardware has, and silenced. Ten
        /// seconds of menu cost a sound a quarter of a second.)
        void setPaused(bool paused);
        /// The track that is playing (or held, while a map loads); -1: none.
        int musicTrack() const;
        /// How far into it the music is, in milliseconds.
        std::uint32_t musicMs() const;
        /// The music put at `ms` into `track` (a save's: what was playing,
        /// from where it was); -1 for none.
        void setMusic(int track, std::uint32_t ms);

        /// On a DSi: the DSP is in use (the room, Enhanced).
        /// Whether the ARM7 took the sound on at the start (none plays if not).
        bool up() const { return up_; }
        bool extended() const { return extended_; }
        /// Switch the DSP's part on or off. Returns whether it is on (it
        /// cannot be, where there is no DSP).
        bool setDsp(bool on);
        /// The sound settings (Settings::roomEffects, outputMode) applied:
        /// the DSi's DSP for its rooms, or the ARM7's pipeline with the
        /// game's own (or none). Enhanced that the DSP cannot give falls
        /// back to the game's own.
        void applySoundMode();
        /// While the card is written (a save): on a DSi the ARM7 does the
        /// writing, and is held up by it -- so the sound plays straight from
        /// its channels, without the pipeline (no room, no music) until it
        /// is let go. Nested holds count.
        void holdPipeline(bool hold);

        int loaded() const { return loaded_; }
        /// How many voices each layer has just now (for the readout).
        int voicesOf(hldata::SoundLayer layer) const { return mixer_.count(layer); }
        int bytes() const { return bytes_; }

        /// Pairs of the track played since it started (or was taken up).
        std::uint32_t musicPlayed() const;

    private:
        struct Clip
        {
            std::uint8_t *data = nullptr; ///< the ADPCM; null if not in memory
            /// Another's memory (a console sent the game: the group's base),
            /// not freed with the map.
            bool shared = false;
            std::uint32_t bytes = 0;
            std::uint32_t loopWord = hldata::kSoundNoLoop;
            std::uint16_t rate = 0;
            std::uint32_t ms = 0; ///< how long it lasts
            /// Whether it goes round (from loopWord: a channel's loop start
            /// is sixteen bits of words).
            bool loops() const
            {
                return data != nullptr && loopWord != hldata::kSoundNoLoop && loopWord < bytes / 4 &&
                       loopWord < 0xFFFF;
            }
        };
        /// A voice of the mixer's, as it is being played.
        struct Voice
        {
            std::uint16_t handle = 0;
            bool placed = false;      ///< it has a place: it is put there again as the camera moves
            hldata::fx pos[3] = {0, 0, 0};
            std::uint8_t volume = 0;  ///< as loud as it is at its source
            std::int16_t radius = 0;
            std::uint8_t shown = 0;   ///< what the ARM7 was last told: how loud, and where
            std::uint8_t pan = 64;
            std::uint16_t rate = 1024; ///< how fast it is played (1024 is as recorded)
            std::uint16_t hz = 11025;  ///< what it was recorded at
        };
        /// Someone speaking: the words of their line still to come, the one
        /// in the air, and the next, read off the card while that one is
        /// said. Two may speak at once.
        struct Speech
        {
            std::uint16_t words[32]; ///< into the level's SentenceWords
            int count = 0;
            int at = 0;
            int volume = 127;
            int pitch = 100;
            bool placed = false;
            hldata::fx pos[3] = {0, 0, 0};
            std::int16_t radius = 0;
            Clip clip[2];            ///< read off the card: [cur] in the air, the other the next
            int cur = 0;
            int ahead = -1;          ///< the sound the other one holds, or -1
            int voice = -1;
            std::int32_t leftMs = 0;  ///< the word's time; heard, only in case its end is never heard
            std::int32_t cutMs = 0;   ///< > 0: the word is cut off this much on (SentenceWord::end)
            std::uint32_t born = 0;
            // The next word, queued behind this one on its channel
            // (Arm7Audio::chain) to begin the moment it ends.
            std::uint16_t chained = 0;
            int chainBuffer = -1;     ///< its clip buffer (-1: one of the map's own)
            std::int32_t chainMs = 0;
            int chainLoud = 0;
            std::uint16_t chainRate = 1024;
            bool busy() const { return leftMs > 0 || at < count; }
        };
        static constexpr int kSpeakers = 2;
        static constexpr int kMaxSounds = 1012; ///< of a map's, at most (no map has near as many)
        static constexpr int kMaxSpoken = 32;
        static constexpr int kLoops = 8; ///< loops of a place at once, where there are voices

        bool readClip(const char *name, Clip &clip);
#ifdef HLDS_CHILD
        /// The group's base's copy, if it has one (net/GuestLoad.hpp).
        bool borrowClip(const char *name, Clip &clip);
#endif
    public:
#ifdef HLDS_CHILD_VRAMSOUND
        /// A test (HLDS_CHILD_VRAMSOUND): whether the ARM7's channels play a
        /// sound out of video memory given to the ARM7 (bank D, at its
        /// 0x06020000) as they do out of main memory. The menu's first sound
        /// is copied there, and the two are played in turn, a second apart,
        /// `rounds` times: a recording of the sound says if they differ.
        void vramTest(int rounds);
#endif
    private:
        void place(const hldata::fx *pos, int volume, int radius, int &outVolume, int &outPan) const;
        int start(hldata::SoundLayer layer, const Clip &clip, int volume, int pan, bool loop, int tag = -1,
                  int pitch = 100);
        /// Let a voice go without a click: it is taken off the mixer at
        /// once and brought to silence over a few frames on the channel
        /// kept back for that (HlMixer.hpp).
        void retire(int voice);
        void fadeTick();
        void killFading();
        void playLoop(const hldata::SoundEvent &event);
        void stopLoop(int tag);
        void releaseWord(Speech &speech, int which);
        void readAhead(Speech &speech, int index);
        void chainNext(Speech &speech);
        void stop(int voice);
        /// `tag`: whose move sound it is (stopLoop cuts it), or -1.
        void playOnce(const hldata::SoundEvent &event, int tag = -1);
        void say(const hldata::SoundEvent &event);
        void speechTick(int ms);
        void loopsTick();
        void follow();
        void startMusic(int track);
        void stopMusic();
        void musicTick();

        bool up_ = false;
        bool paused_ = false;
        Clip *clips_ = nullptr;
        int count_ = 0;
        /// The sounds of weapons the map has not (kSoundLazy): a byte a
        /// sound, 1 for those. Each is read out of the common archive when
        /// it is first played, into one of a few places, the one played
        /// longest ago (and over) given up for it.
        std::uint8_t *lazy_ = nullptr;
        static constexpr int kLazyClips = 4;
        int lazyClip_[kLazyClips] = {-1, -1, -1, -1};
        std::uint32_t lazyPlayed_[kLazyClips] = {};
        void lazyTouch(int sound);
        public:
        /// A console in DSi mode, nothing else to ask for: the next it has not.
        bool fetchAhead();
        /// ... and now what goes with a gun (its view model's name): one
        /// picked up, or in someone's hand.
        void askGun(const char *viewModel);
        private:
        int aheadNext_ = 0;
        int loaded_ = 0;
        int bytes_ = 0;
        Clip ui_[hldata::kUiSoundCount];
        Clip warning_[hldata::kMatchWarningWords];
        int warningAt_ = -1;  ///< the word to say next; -1: none
        int warningMs_ = 0;   ///< until then
        void warningTick(int ms);
        void roomTick();
        void holdMusic();
        void resumeMusic();
        bool extended_ = false;
        int roomDue_ = 0;
        int room_ = 0; ///< the room the ARM7 was last told
        int held_ = 0; ///< holdPipeline's count
        int dsiHz_ = 0; ///< the DSi's speakers' rate, in kHz (0: not yet set)
        void sendMode();
        hldata::Mixer mixer_;
        Voice voices_[hldata::Mixer::kVoices];
        std::uint32_t lastVblank_ = 0;
        /// The time between frames, exact over any run of them (a vblank is
        /// 16 2/3 ms: rounded each frame on its own, sound ran 2% fast).
        hldata::FrameClock clock_;
        int loopsDue_ = 0;

        /// Voices on their way out: their tokens, and how loud they still
        /// are.
        struct Fading
        {
            std::uint16_t handle = 0;
            std::uint8_t level = 0;
        };
        static constexpr int kFading = 2;
        Fading fading_[kFading];
        Speech speech_[kSpeakers];
        std::uint32_t speechClock_ = 0;
        /// A bit a logic record: a sound that is switched on but does not go
        /// round has been played, and is not played again until it has been
        /// switched off.
        std::uint32_t *played_ = nullptr;

        // The music (the ARM7's way: its stereo file, in blocks).
        std::FILE *music_ = nullptr;
        std::uint32_t musicLeft_ = 0;    ///< blocks of the file not yet read
        std::uint32_t musicRate_ = 23802; ///< the track's (its stereo file's) rate
        int musicTrack_ = -1;
        std::uint32_t musicHeldMs_ = 0;  ///< how far in it was when it was put down
        std::uint32_t musicSkipped_ = 0; ///< pairs of the track before where it was taken up
        bool musicHeld_ = false;
    };

    HL_ENGINE_OBJECT(Sound, sound);
} // namespace hlrt
