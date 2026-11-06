#include "audio/Sound.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

#include <nds.h>


#include "Global.hpp"
#include "net/GuestLoad.hpp"
#include "net/NetGame.hpp"
#ifdef HLDS_CHILD
#include "net/AirDevice.hpp"
#endif
#include "Settings.hpp"
#include "HlRoomPresets.hpp"
#include "HlStereoMusic.hpp"
#include "audio/Arm7Audio.hpp"
#include "audio/DspAudio.hpp"
#include "game/Player.hpp"
#include "data/LevelFile.hpp"
#include "game/Game.hpp"
#include "render/Camera.hpp"

namespace hlrt
{
    Sound sound;

    namespace
    {
        using namespace hldata;

        /// The music's ring: blocks of the track as they are in its file,
        /// which the ARM7 decodes and plays (HlStereoMusic.hpp) and
        /// musicTick keeps filled ahead of it (two thirds of a second: a
        /// frame of the game's held up by the card does not run it dry).
#ifdef HLDS_CHILD
        // (A console sent the game is sent no music: nothing to keep ahead of.)
        constexpr std::uint32_t kMusicRingBlocks = 1;
#else
        constexpr std::uint32_t kMusicRingBlocks = 8;
#endif
        alignas(32) std::uint16_t musicRing[kMusicRingBlocks * StereoMusic::kBlockWords];

        /// The music as loud as the player has it (the track itself is
        /// played at about four fifths of full): 0..256.
        int musicGain()
        {
            return settings.volume[kVolumeMusic] * 205 / 10;
        }
    } // namespace

    void Sound::init()
    {
        soundEnable();
        // A DSi's extras: its speakers' rate (32.7 or 47.6 kHz, as the
        // quality setting has it: sendMode, after the sound is switched on --
        // which puts it back to 32.7), and the DSP, used only for the room
        // effects' Enhanced. Neither exists on a DS.
        const bool dsi = Arm7Audio::dsiSpeakers();
        // (All of the speakers to the ordinary channels, until the DSP has
        // something for them: DspAudio. The rate: sendMode.)
        arm7Audio.setDsiRatio(8);
        // The game's own sound on the ARM7 (arm7/source/Audio.cpp), which
        // plays the clips where they are in memory: nothing is "loaded".
        up_ = arm7Audio.start();
        applyVolumes();
        // The menu's own sounds, for as long as the game runs.
        for (int n = 0; n < kUiSoundCount; ++n)
        {
            if (global.hasData)
                readClip(kUiSoundFiles[n], ui_[n]);
        }
        if (dsi)
            extended_ = settings.roomEffects == kRoomEnhanced && dspAudio.start();
        // The pipeline (the mix captured and played again on the ARM7, the
        // music and the room added) -- unless the DSP has the capture.
        sendMode();
        lastVblank_ = Global::vblanks();
    }

    void Sound::applyVolumes()
    {
        const auto effects = settings.volume[kVolumeEffects];
        mixer_.volume[static_cast<int>(SoundLayer::Player)] = effects;
        mixer_.volume[static_cast<int>(SoundLayer::World)] = effects;
        mixer_.volume[static_cast<int>(SoundLayer::Ui)] = effects;
        mixer_.volume[static_cast<int>(SoundLayer::Voice)] = settings.volume[kVolumeVoices];
        mixer_.volume[static_cast<int>(SoundLayer::Ambience)] = settings.volume[kVolumeAmbience];
        // (The music's is the ARM7's to apply. What is playing in the other
        // layers is told when it is next placed.)
        arm7Audio.musicVolume(musicGain());
        loopsDue_ = 0;
    }

    bool Sound::readClip(const char *name, Clip &clip)
    {
        clip = Clip();
        // The map's own, out of its archive; the menu's, out of the card's.
        const std::string inArchive = std::string("snd/") + name + ".hsn";
        std::FILE *file = std::fopen(global.mapPath(inArchive).c_str(), "rb");
        if (file == nullptr)
            file = std::fopen(global.commonPath(inArchive).c_str(), "rb");
        if (file == nullptr)
            return false;
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        // The header is checked against the file's real size before anything
        // in it sizes an allocation or is handed to the hardware.
        SoundHeader header;
        bool ok = size > static_cast<long>(sizeof(header)) && size < 4 * 1024 * 1024 &&
                  std::fread(&header, sizeof(header), 1, file) == 1 &&
                  soundHeaderValid(header, static_cast<std::uint32_t>(size));
        std::uint8_t *data = nullptr;
        if (ok)
        {
            data = static_cast<std::uint8_t *>(std::malloc(header.dataBytes));
            ok = data != nullptr && std::fread(data, 1, header.dataBytes, file) == header.dataBytes;
        }
        std::fclose(file);
        if (!ok)
        {
            std::free(data);
            return false;
        }
        // The ARM7 plays it out of memory itself: it must be there, not in
        // this processor's cache.
        DC_FlushRange(data, header.dataBytes);
        clip.data = data;
        clip.bytes = header.dataBytes;
        clip.loopWord = header.loopWord;
        clip.rate = header.rate;
        clip.ms = static_cast<std::uint32_t>((static_cast<std::uint64_t>(header.samples) * 1000) / header.rate);
        return true;
    }

#ifdef HLDS_CHILD
    bool Sound::borrowClip(const char *name, Clip &clip)
    {
        clip = Clip();
        const std::uint8_t *block = nullptr;
        std::uint32_t size = 0;
        SoundHeader header;
        if (!guest::borrow((std::string("snd/") + name + ".hsn").c_str(), block, size) || size <= sizeof(header))
            return false;
        std::memcpy(&header, block, sizeof(header));
        if (!soundHeaderValid(header, size))
            return false;
        auto *data = const_cast<std::uint8_t *>(block + sizeof(header));
        DC_FlushRange(data, header.dataBytes);
        clip.data = data;
        clip.shared = true;
        clip.bytes = header.dataBytes;
        clip.loopWord = header.loopWord;
        clip.rate = header.rate;
        clip.ms = static_cast<std::uint32_t>((static_cast<std::uint64_t>(header.samples) * 1000) / header.rate);
        return true;
    }
#endif

    void Sound::load()
    {
        const LevelView &view = level.view;
        const int count = static_cast<int>(view.soundCount) < kMaxSounds ? static_cast<int>(view.soundCount) : kMaxSounds;
        stats_ = LoadStats();
        clips_ = static_cast<Clip *>(std::calloc(count + 1, sizeof(Clip)));
        bool *kept = static_cast<bool *>(std::calloc(view.soundCount + 1, sizeof(bool)));
        if (clips_ == nullptr || kept == nullptr)
        {
            std::free(clips_);
            std::free(kept);
            clips_ = nullptr;
            stats_.tableFailed = true;
            return;
        }
        count_ = count;
        aheadNext_ = 0;
        played_ = static_cast<std::uint32_t *>(std::calloc(view.logicCount / 32 + 1, sizeof(std::uint32_t)));
        soundsKept(view.logic, view.logicCount, view.logicSounds, view.logicSoundCount,
                   view.soundCount, kept, view.modelSounds, view.modelSoundCount, view.sounds);
        lazy_ = static_cast<std::uint8_t *>(std::calloc(count + 1, 1));
        for (int i = 0; lazy_ != nullptr && i < count; ++i)
            lazy_[i] = soundLazy(view.sounds[i]) && view.sounds[i].name[0] != '\0' ? 1 : 0;
        for (int k = 0; k < kLazyClips; ++k)
            lazyClip_[k] = -1;
        // In two goes: everything but the later weapons' sounds, then those.
        // Left with too little to play in (a DS, in a map with every weapon),
        // the map does without the rest rather than without its memory --
        // and the rest is a weapon's noise, not the map's own.
#ifdef HLDS_CHILD
        // Sent the game: every sound the map was sent, in the order it was
        // sent -- those kept, then the rest -- each borrowed from the group's
        // base or read as it comes, straight into its clip.
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i < count; ++i)
            {
                if (kept[i] != (pass == 0) || view.sounds[i].name[0] == '\0')
                    continue;
                // (The later weapons' sounds, read when first played on a card,
                // cannot be here: the map's stream is over by then. Those the
                // base has are borrowed now -- they cost nothing -- and the
                // rest go without.)
                if (lazy_ != nullptr && lazy_[i] && !global.twlChild)
                {
                    if (borrowClip(view.sounds[i].name, clips_[i]))
                    {
                        lazy_[i] = 0;
                        ++loaded_;
                    }
                    continue;
                }
                // (At full quality, in DSi mode: the better copy, if it is next.)
                char twin[sizeof(SoundRef)];
                if ((global.twlChild && soundTwinName(view.sounds[i], twin, sizeof(twin)) && readClip(twin, clips_[i])) ||
                    borrowClip(view.sounds[i].name, clips_[i]) || readClip(view.sounds[i].name, clips_[i]))
                {
                    if (lazy_ != nullptr)
                        lazy_[i] = 0;
                    ++loaded_;
                    bytes_ += static_cast<int>(clips_[i].bytes);
                    if (!clips_[i].shared)
                        guest::count(hldata::GuestKind::Sound, clips_[i].bytes);
                }
                // (In DSi mode what did not come with the map is asked for.)
                else if (lazy_ != nullptr && global.twlChild)
                    lazy_[i] = 1;
            }
        }
        std::free(kept);
        lastVblank_ = Global::vblanks();
        resumeMusic();
        return;
#endif
        const int laterFrom = static_cast<int>(kCommonSoundsBefore30);
        const int laterTo = static_cast<int>(commonsOf(view.header->version));
        for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < count; ++i)
        {
            const bool later = i >= laterFrom && i < laterTo;
            if (later != (pass == 1) || !kept[i] || view.sounds[i].name[0] == '\0')
                continue;
            ++stats_.kept;
            // (Below this, the rest of the pass is not tried: nothing given
            // back in between, it stays below.)
            if (global.heapHeadroom() < 300 * 1024)
            {
                ++stats_.skippedRoom;
                continue;
            }
            // (A DSi has the memory for the better copy, where there is one.)
            char twin[sizeof(SoundRef)];
            if ((global.isDSi && soundTwinName(view.sounds[i], twin, sizeof(twin)) && readClip(twin, clips_[i])) ||
                readClip(view.sounds[i].name, clips_[i]))
            {
                ++loaded_;
                ++stats_.loaded;
                bytes_ += static_cast<int>(clips_[i].bytes);
            }
            else
                ++stats_.failed;
        }
        std::free(kept);
        lastVblank_ = Global::vblanks();
        resumeMusic();
    }

    void Sound::holdMusic()
    {
        // Put down where it is, to be taken up again: where it is is kept in
        // milliseconds.
        if (music_ != nullptr)
        {
            // (What has been handed over, less what the stream still has
            // in hand, less any silence after the end.)
            const std::uint32_t played = musicPlayed();
            const int track = musicTrack_;
            const bool over = arm7Audio.musicOver();
            const std::uint32_t before = musicSkipped_;
            stopMusic();
            if (!over)
            {
                musicHeld_ = true;
                musicTrack_ = track;
                musicHeldMs_ = static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(before + played) * 1000) / musicRate_);
            }
        }
    }

    void Sound::resumeMusic()
    {
        // (With the game standing still it stays put down: setPaused takes
        // it up again.)
        if (!musicHeld_ || paused_)
            return;
        musicHeld_ = false;
        const int track = musicTrack_;
        musicTrack_ = -1;
        // From the pair of the moment (the track's rate: what its stereo
        // file says).
        musicSkipped_ = static_cast<std::uint32_t>((static_cast<std::uint64_t>(musicHeldMs_) * kStereoRate) / 1000);
        startMusic(track);
    }

    bool Sound::setDsp(bool on)
    {
        if (on == extended_)
            return extended_;
        // (The music is the ARM7's either way, and goes on.)
        if (on)
        {
            extended_ = dspAudio.start();
            if (extended_)
                dspAudio.room(room_); // (the room the player is in already)
        }
        else
        {
            dspAudio.stop();
            extended_ = false;
        }
        sendMode();
        return extended_;
    }

    void Sound::holdPipeline(bool hold)
    {
        const int was = held_;
        held_ = hold ? held_ + 1 : (held_ > 0 ? held_ - 1 : 0);
        if ((was == 0) != (held_ == 0))
        {
            sendMode();
            // (Let go of before the card is touched.)
            if (hold)
                arm7Audio.sync();
        }
    }

    void Sound::sendMode()
    {
        // (The DSP's rooms take what the pipeline sends them, at a quarter
        // of 47605 Hz: the pipeline at that rate then.)
        const std::uint8_t effects = extended_                         ? kAudioEffectsEnhanced
                                     : settings.roomEffects == kRoomOff ? kAudioEffectsOff
                                                                       : kAudioEffectsClassic;
#ifdef HLDS_CHILD
        // (Sent the game, the speakers stay at their one rate: dsiSpeakers.)
        const std::uint8_t quality = kQualityStandard;
#else
        const std::uint8_t quality = extended_ ? kQualityHigh : settings.soundQuality;
#endif
        // A DSi's speakers fed at the pipeline's rate: 47.6 kHz at the high
        // quality (and for the DSP, which is made for it), else 32.7.
        const int hz = quality == kQualityHigh ? 47 : 32;
        if (Arm7Audio::dsiSpeakers() && hz != dsiHz_)
        {
            dsiHz_ = hz;
            arm7Audio.setDsiRate(hz);
        }
#ifdef HLDS_CHILD
        // (Sent the game: the plain way, every sound straight to the
        // speakers, as a program's own sound is -- not captured and played
        // again by the ARM7. Started by Download Play on a 3DS, what went
        // through the capture was never heard; and a console sent the game
        // has no music for it to mix. In DSi mode, as on a card.)
        arm7Audio.setMode(global.twlChild && held_ == 0, effects, settings.outputMode, quality);
#else
        arm7Audio.setMode(held_ == 0, effects, settings.outputMode, quality);
#endif
        // (The room the player is in, again: a pipeline started afresh has
        // none.)
        arm7Audio.setRoom(room_);
    }

    void Sound::applySoundMode()
    {
        const bool wantDsp = settings.roomEffects == kRoomEnhanced && DspAudio::available();
        if (wantDsp != extended_)
            setDsp(wantDsp);
        else
            sendMode();
        if (settings.roomEffects == kRoomEnhanced && !extended_)
            settings.roomEffects = kRoomClassic; // (the DSP would not)
    }

    void Sound::unload()
    {
        if (up_)
            arm7Audio.stopAll();
        // (A new map is no room until it says otherwise.)
        if (room_ != 0)
        {
            room_ = 0;
            arm7Audio.setRoom(0);
        }
        fading_[0] = fading_[1] = Fading();
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            mixer_.stop(v);
            voices_[v] = Voice();
        }
        // The music is put down where it is, to be taken up again: a map
        // takes seconds to load, and nothing would feed it.
        holdMusic();
        // (Two blanks, so that the ARM7 has let go of everything before it
        // is freed.)
        swiWaitForVBlank();
        swiWaitForVBlank();
        for (int i = 0; i < count_; ++i)
        {
            if (!clips_[i].shared)
                std::free(clips_[i].data);
        }
        for (Speech &speech : speech_)
        {
            releaseWord(speech, 0);
            releaseWord(speech, 1);
            speech = Speech();
        }
        std::free(played_);
        played_ = nullptr;
        std::free(lazy_);
        lazy_ = nullptr;
        for (int k = 0; k < kLazyClips; ++k)
            lazyClip_[k] = -1;
        std::free(clips_);
        clips_ = nullptr;
        count_ = 0;
        loaded_ = 0;
        bytes_ = 0;
    }

    void Sound::place(const fx *pos, int volume, int radius, int &outVolume, int &outPan) const
    {
        const fx d[3] = {pos[0] - camera.pos[0], pos[1] - camera.pos[1], pos[2] - camera.pos[2]};
        const fx distance = fxLength(d);
        outVolume = soundGain(volume, fxToInt(distance), radius);
        // To one side by how far round it is: straight ahead and straight
        // behind are the middle.
        outPan = 64;
        if (distance > fxFromInt(8))
        {
            const std::int64_t side = static_cast<std::int64_t>(d[0]) * camera.right[0] +
                                      static_cast<std::int64_t>(d[1]) * camera.right[1] +
                                      static_cast<std::int64_t>(d[2]) * camera.right[2];
            outPan = 64 + static_cast<int>((side * 56) / (static_cast<std::int64_t>(distance) << kFxShift));
            outPan = outPan < 0 ? 0 : (outPan > 127 ? 127 : outPan);
        }
    }

    /// Play `clip` in `layer`, `volume` loud (0..127, before the layer's own
    /// volume) and `pan` to one side (0..127). The voice it got, or -1 if it
    /// is not to be heard.
    int Sound::start(SoundLayer layer, const Clip &clip, int volume, int pan, bool loop, int tag, int pitch)
    {
        if (!up_ || (paused_ && layer != SoundLayer::Ui))
            return -1;
        const int gain = mixer_.scaled(layer, volume);
        if (gain < 3)
            return -1;
        const int v = mixer_.take(layer, gain);
        if (v < 0)
            return -1;
        if (mixer_.voices[v].active)
            retire(v);
        // (The unit of speed: 1024 is as recorded.)
        int rate = (1024 * pitch) / 100;
        rate = rate < 64 ? 64 : (rate > 4095 ? 4095 : rate);
        auto playIt = [&]()
        {
            return arm7Audio.play(clip.data, clip.bytes / 4, clip.loopWord, clip.rate, gain * 2, pan * 2, rate,
                                  layer == SoundLayer::Ui);
        };
        std::uint16_t handle = playIt();
        if (handle == 0 && (fading_[0].handle != 0 || fading_[1].handle != 0))
        {
            // No channel: what was on its way out is out now.
            killFading();
            handle = playIt();
        }
        if (handle == 0)
        {
            // The hardware has no channel for it, whatever the count says
            // (something of its own is on one): then what matters least of
            // all that is playing gives its channel up, and it is asked
            // again. A sound is never taken as played that was not.
            const int least = mixer_.weakest();
            if (least < 0 || (layer != SoundLayer::Ui && layer != SoundLayer::Voice &&
                              Mixer::rank(mixer_.voices[least].layer) > Mixer::rank(layer)))
                return -1;
            stop(least);
            handle = playIt();
            if (handle == 0)
                return -1;
        }
        voices_[v] = Voice();
        voices_[v].handle = handle;
        voices_[v].shown = static_cast<std::uint8_t>(gain);
        voices_[v].pan = static_cast<std::uint8_t>(pan);
        voices_[v].rate = static_cast<std::uint16_t>(rate);
        voices_[v].hz = clip.rate;
        // (Over when the ARM7 says it has ended -- frame(); the time, twice
        // what it should take, only in case that is never heard.)
        mixer_.start(v, layer, gain, loop ? -1 : static_cast<std::int32_t>(clip.ms * 100 / pitch) * 2 + 500, tag);
        return v;
    }

    void Sound::stop(int v)
    {
        if (voices_[v].handle != 0)
            arm7Audio.stop(voices_[v].handle);
        voices_[v] = Voice();
        mixer_.stop(v);
    }

    void Sound::retire(int v)
    {
        const std::uint16_t handle = voices_[v].handle;
        const int level = voices_[v].shown;
        voices_[v] = Voice();
        mixer_.stop(v);
        if (handle == 0)
            return;
        // (Too quiet to click, or the game standing still: simply stopped.)
        if (level < 8 || paused_)
        {
            arm7Audio.stop(handle);
            return;
        }
        // A place on the way out; if both are taken, the older has had its
        // time.
        int slot = fading_[0].handle == 0 ? 0 : (fading_[1].handle == 0 ? 1 : -1);
        if (slot < 0)
        {
            arm7Audio.stop(fading_[0].handle);
            fading_[0] = fading_[1];
            slot = 1;
        }
        fading_[slot].handle = handle;
        fading_[slot].level = static_cast<std::uint8_t>(level / 2);
        arm7Audio.setVolume(handle, static_cast<int>(level));
    }

    /// Every frame: what is on its way out is half as loud again, and gone
    /// when there is nothing left of it (three or four frames).
    void Sound::fadeTick()
    {
        for (Fading &fading : fading_)
        {
            if (fading.handle == 0)
                continue;
            fading.level = static_cast<std::uint8_t>(fading.level / 2);
            if (fading.level < 4)
            {
                arm7Audio.stop(fading.handle);
                fading = Fading();
            }
            else
            {
                arm7Audio.setVolume(fading.handle, static_cast<int>(fading.level * 2));
            }
        }
    }

    void Sound::killFading()
    {
        for (Fading &fading : fading_)
        {
            if (fading.handle != 0)
                arm7Audio.stop(fading.handle);
            fading = Fading();
        }
    }

    /// A weapon's sound the map has not, about to be played: read now if it
    /// is not in memory (a frame's wait, once), and said to be played.
    bool Sound::fetchAhead()
    {
#ifdef HLDS_CHILD
        for (; lazy_ != nullptr && aheadNext_ < count_; ++aheadNext_)
        {
            if (lazy_[aheadNext_] == 0)
                continue;
            const std::string path = std::string("snd/") + level.view.sounds[aheadNext_].name + ".hsn";
            if (air::has(path.c_str()))
                continue;
            if (netGame.fetchFile(path.c_str()))
                return true;
        }
#endif
        return false;
    }

    void Sound::askGun(const char *viewModel)
    {
#ifdef HLDS_CHILD
        if (!global.twlChild || lazy_ == nullptr || viewModel == nullptr)
            return;
        for (const GunSounds &gun : kGunSounds)
        {
            if (std::strcmp(gun.viewModel, viewModel) != 0)
                continue;
            for (int i = static_cast<int>(gun.first); i <= static_cast<int>(gun.last) && i < count_; ++i)
            {
                if (lazy_[i] == 0 || clips_[i].data != nullptr)
                    continue;
                const std::string path = std::string("snd/") + level.view.sounds[i].name + ".hsn";
                if (!air::has(path.c_str()))
                    netGame.fetchFile(path.c_str(), true);
            }
        }
#else
        (void)viewModel;
#endif
    }

    void Sound::lazyTouch(int sound)
    {
        if (lazy_ == nullptr || sound < 0 || sound >= count_ || lazy_[sound] == 0)
            return;
        // In DSi mode there is room: what has come stays, for the map.
        if (global.twlChild)
        {
            if (clips_[sound].data == nullptr && !readClip(level.view.sounds[sound].name, clips_[sound]))
                netGame.fetchFile((std::string("snd/") + level.view.sounds[sound].name + ".hsn").c_str(), true);
            return;
        }
        const std::uint32_t now = Global::vblanks();
        for (int k = 0; k < kLazyClips; ++k)
        {
            if (lazyClip_[k] == sound)
            {
                lazyPlayed_[k] = now;
                return;
            }
        }
        // A place: an empty one, or the one played longest ago -- as long as
        // it is over, at the slowest it may be played (twice as long) and a
        // half second more.
        int place = -1;
        for (int k = 0; k < kLazyClips && place < 0; ++k)
            place = lazyClip_[k] < 0 ? k : -1;
        if (place < 0)
        {
            std::uint32_t oldest = 0;
            for (int k = 0; k < kLazyClips; ++k)
            {
                const std::uint32_t ago = now - lazyPlayed_[k];
                const std::uint32_t lasts = clips_[lazyClip_[k]].ms * 2 * 60 / 1000 + 30;
                if (ago > lasts && ago >= oldest)
                {
                    oldest = ago;
                    place = k;
                }
            }
        }
        if (place < 0)
            return;
        if (lazyClip_[place] >= 0)
        {
            const int gone = lazyClip_[place];
            if (!clips_[gone].shared)
                std::free(clips_[gone].data);
            clips_[gone] = Clip();
            lazyClip_[place] = -1;
        }
        if (global.heapHeadroom() < 200 * 1024)
        {
            ++stats_.lateSkippedRoom;
            return;
        }
        if (!readClip(level.view.sounds[sound].name, clips_[sound]))
        {
            ++stats_.lateFailed;
            return;
        }
        lazyClip_[place] = sound;
        lazyPlayed_[place] = now;
    }

    void Sound::playOnce(const SoundEvent &event, int tag)
    {
        lazyTouch(event.sound);
        if (event.sound >= count_ || clips_[event.sound].data == nullptr)
            return;
        const Clip &clip = clips_[event.sound];
        int volume = event.volume, pan = 64;
        const bool placed = !(event.flags & kSoundHere);
        if (placed)
            place(event.pos, event.volume, event.radius, volume, pan);
        const int v = start(event.layer, clip, volume, pan, false, tag, event.pitch);
        if (v < 0 || !placed)
            return;
        Voice &voice = voices_[v];
        voice.placed = true;
        voice.volume = event.volume;
        voice.radius = event.radius;
        for (int k = 0; k < 3; ++k)
            voice.pos[k] = event.pos[k];
    }

    /// What something makes for as long as it moves (a lift, a train, a
    /// door that takes its time): round and round from where it is, until
    /// stopLoop. A sound that does not go round is played once -- in the
    /// place of the last its maker played, and only until stopLoop, as the
    /// game has it (doors/doormove1.wav is five seconds long, and a door
    /// may be open in half of one).
    void Sound::playLoop(const SoundEvent &event)
    {
        lazyTouch(event.sound);
        if (event.sound >= count_ || clips_[event.sound].data == nullptr)
            return;
        const Clip &clip = clips_[event.sound];
        if (!clip.loops())
        {
            stopLoop(event.tag);
            playOnce(event, event.tag);
            return;
        }
        // Already going round: it is where it is now (what moves as it makes
        // it -- an osprey), and it does not start twice.
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            const MixVoice &mix = mixer_.voices[v];
            if (mix.active && mix.layer == event.layer && mix.leftMs < 0 && mix.tag == event.tag)
            {
                for (int k = 0; k < 3; ++k)
                    voices_[v].pos[k] = event.pos[k];
                return;
            }
        }
        int volume = event.volume, pan = 64;
        const bool placed = !(event.flags & kSoundHere);
        if (placed)
            place(event.pos, event.volume, event.radius, volume, pan);
        const int v = start(event.layer, clip, volume, pan, true, event.tag, event.pitch);
        if (v < 0)
            return;
        Voice &voice = voices_[v];
        voice.placed = placed;
        voice.volume = event.volume;
        voice.radius = event.radius;
        for (int k = 0; k < 3; ++k)
            voice.pos[k] = event.pos[k];
    }

    void Sound::stopLoop(int tag)
    {
        const std::uint32_t moving = mixer_.movingOf(tag);
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            if ((moving >> v) & 1)
                retire(v);
        }
    }

    /// What has a place is put there again as the camera moves and turns:
    /// louder and quieter, and from one side to the other.
    void Sound::follow()
    {
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            Voice &voice = voices_[v];
            MixVoice &mix = mixer_.voices[v];
            if (!mix.active || !voice.placed || mix.layer == SoundLayer::Ambience)
                continue;
            int volume, pan;
            place(voice.pos, voice.volume, voice.radius, volume, pan);
            const int gain = mixer_.scaled(mix.layer, volume);
            const int dv = gain - voice.shown, dp = pan - voice.pan;
            if (dv > 2 || dv < -2)
            {
                voice.shown = static_cast<std::uint8_t>(gain);
                mix.gain = voice.shown;
                arm7Audio.setVolume(voice.handle, static_cast<int>(gain * 2));
            }
            if (dp > 3 || dp < -3)
            {
                voice.pan = static_cast<std::uint8_t>(pan);
                arm7Audio.setPanning(voice.handle, static_cast<int>(pan * 2));
            }
        }
    }

    void Sound::releaseWord(Speech &speech, int which)
    {
        std::free(speech.clip[which].data);
        speech.clip[which] = Clip();
    }

    void Sound::say(const SoundEvent &event)
    {
        const LevelView &view = level.view;
        const int sentence = event.sound;
        if (sentence < 0 || sentence >= static_cast<int>(view.sentenceCount))
            return;
        // Whose voice: the one that is already this speaker's (a line cuts
        // in on the same speaker's last, as on the game's own voice
        // channel); else one that is silent; else the one that has been
        // speaking longest. The same speaker is the same place -- or no
        // place at all, which is the voice that is heard everywhere.
        const bool placed = !(event.flags & kSoundHere);
        Speech *speech = nullptr;
        for (Speech &other : speech_)
        {
            if (!other.busy() || other.placed != placed)
                continue;
            const fx d[3] = {other.pos[0] - event.pos[0], other.pos[1] - event.pos[1], other.pos[2] - event.pos[2]};
            if (!placed || fxLength(d) < fxFromInt(96))
                speech = &other;
        }
        for (Speech &other : speech_)
        {
            if (speech == nullptr && !other.busy())
                speech = &other;
        }
        if (speech == nullptr)
            speech = speech_[0].born < speech_[1].born ? &speech_[0] : &speech_[1];

        const SentenceRecord &record = view.sentences[sentence];
        speech->count = 0;
        speech->at = 0;
        for (int w = 0; w < record.wordCount && speech->count < kMaxSpoken; ++w)
            speech->words[speech->count++] = static_cast<std::uint16_t>(record.firstWord + w);
        // (As loud as the line is said and as whoever says it is far off:
        // a line from somewhere comes from there.)
        speech->volume = (record.volume * event.volume) / 127;
        speech->pitch = event.pitch; // (each word has its own on top: SentenceWord)
        speech->placed = placed;
        speech->radius = event.radius;
        for (int k = 0; k < 3; ++k)
            speech->pos[k] = event.pos[k];
        speech->born = ++speechClock_;
        speech->leftMs = 1; // (the word in the air is cut short at the next tick)
        speech->cutMs = 0;
        // (Nor is the old line's next word, queued behind it, begun.)
        if (speech->chained != 0)
            arm7Audio.stop(speech->chained);
        speech->chained = 0;
        speech->chainBuffer = -1;
        // (What was read ahead was the old line's next word.)
        releaseWord(*speech, speech->cur ^ 1);
        speech->ahead = -1;
    }

    /// The word after the one in the air, into the buffer that one is not
    /// in: read while there is time, so that the line does not stop for the
    /// card between two words.
    void Sound::readAhead(Speech &speech, int index)
    {
        const int other = speech.cur ^ 1;
        releaseWord(speech, other);
        speech.ahead = -1;
        if (index >= static_cast<int>(level.view.soundCount) || level.view.sounds[index].name[0] == '\0' ||
            (index < count_ && clips_[index].data != nullptr))
            return;
        if (global.heapHeadroom() < 200 * 1024)
        {
            ++stats_.lateSkippedRoom;
            return;
        }
        if (!readClip(level.view.sounds[index].name, speech.clip[other]))
        {
            ++stats_.lateFailed;
            return;
        }
        if (up_)
            speech.ahead = index;
        else
            releaseWord(speech, other);
    }

    /// While a word is said, the next one queued behind it on its channel,
    /// to begin the moment it ends: no gap between the words of a line.
    /// (Only after a word that is said to its end, and only a word that is,
    /// whose sound is in memory already.)
    void Sound::chainNext(Speech &speech)
    {
        if (speech.chained != 0 || speech.cutMs > 0 || speech.at >= speech.count || speech.voice < 0)
            return;
        const std::uint16_t handle = voices_[speech.voice].handle;
        if (handle == 0 || !arm7Audio.playing(handle) || arm7Audio.queued(handle))
            return;
        const SentenceWord &word = level.view.sentenceWords[speech.words[speech.at]];
        const std::uint16_t index = word.sound;
        if (index >= level.view.soundCount || level.view.sounds[index].name[0] == '\0' ||
            (word.end != 0 && word.end < 100))
            return;
        const bool borrowed = index < count_ && clips_[index].data != nullptr;
        if (!borrowed && speech.ahead != index)
            return;
        const Clip &clip = borrowed ? clips_[index] : speech.clip[speech.cur ^ 1];
        const int pitch = (speech.pitch * (word.pitch != 0 ? word.pitch : 100)) / 100;
        const int loud = (speech.volume * word.volume) / 100;
        int volume = loud, pan = 64;
        if (speech.placed)
            place(speech.pos, loud, speech.radius, volume, pan);
        const int gain = mixer_.scaled(SoundLayer::Voice, volume);
        int rate = (1024 * pitch) / 100;
        rate = rate < 64 ? 64 : (rate > 4095 ? 4095 : rate);
        const std::uint16_t token =
            arm7Audio.chain(handle, clip.data, clip.bytes / 4, clip.loopWord, clip.rate, gain * 2, pan * 2, rate);
        if (token == 0)
            return;
        speech.chained = token;
        speech.chainBuffer = borrowed ? -1 : speech.cur ^ 1;
        speech.chainMs = static_cast<std::int32_t>(clip.ms * 100 / (pitch > 0 ? pitch : 100));
        speech.chainLoud = loud;
        speech.chainRate = static_cast<std::uint16_t>(rate);
        if (!borrowed)
            speech.ahead = -1; // (its buffer is the queued word's now)
        ++speech.at;
    }

    void Sound::speechTick(int ms)
    {
        for (int s = 0; s < kSpeakers; ++s)
        {
            Speech &speech = speech_[s];
            if (!speech.busy() && speech.voice < 0)
                continue;
            if (speech.leftMs > 0)
                speech.leftMs -= ms;
            const bool ours = speech.voice >= 0 && mixer_.voices[speech.voice].active &&
                              mixer_.voices[speech.voice].layer == SoundLayer::Voice &&
                              mixer_.voices[speech.voice].tag == s;
            const std::uint16_t handle = ours ? voices_[speech.voice].handle : 0;

            // The next word has begun behind the last, on its own (the ARM7
            // started it the moment the last ended): it is the word in the
            // air now, and the one after it is fetched.
            if (ours && speech.chained != 0 && !arm7Audio.playing(handle) && arm7Audio.playing(speech.chained) &&
                !arm7Audio.queued(speech.chained))
            {
                releaseWord(speech, speech.cur);
                if (speech.chainBuffer >= 0)
                    speech.cur = speech.chainBuffer;
                Voice &voice = voices_[speech.voice];
                voice.handle = speech.chained;
                voice.rate = speech.chainRate;
                voice.volume = static_cast<std::uint8_t>(speech.chainLoud);
                speech.leftMs = speech.chainMs * 2 + 500; // (only if its end is never heard)
                mixer_.voices[speech.voice].leftMs = speech.leftMs;
                speech.cutMs = 0;
                speech.chained = 0;
                speech.chainBuffer = -1;
                if (speech.at < speech.count)
                    readAhead(speech, level.view.sentenceWords[speech.words[speech.at]].sound);
                continue;
            }

            // Is the word in the air over? Heard to end (the ARM7 says so),
            // cut where the game cuts it, or -- too far off to be heard at
            // all -- its time taken.
            bool over;
            if (speech.voice < 0)
                over = speech.leftMs <= 0;
            else if (!ours)
                over = true;
            else
                over = handle == 0 || (!arm7Audio.playing(handle) && !arm7Audio.queued(speech.chained)) ||
                       speech.leftMs <= 0;
            if (speech.cutMs > 0)
            {
                speech.cutMs -= ms;
                over = over || speech.cutMs <= 0;
            }
            if (!over)
            {
                chainNext(speech);
                continue;
            }

            // Over, and what was queued behind it never begun: it is said
            // the ordinary way below (its buffer is still the one ahead).
            if (speech.chained != 0)
            {
                arm7Audio.stop(speech.chained);
                --speech.at;
                if (speech.chainBuffer >= 0)
                    speech.ahead = level.view.sentenceWords[speech.words[speech.at]].sound;
                speech.chained = 0;
                speech.chainBuffer = -1;
            }
            // The word in the air is over: its voice and its memory can
            // go, and the next is said.
            if (ours)
            {
                // (A word read off the card is played out of memory that is
                // about to be given back: that one is stopped dead. One of
                // the map's own sounds may fade.)
                if (speech.clip[speech.cur].data != nullptr)
                    stop(speech.voice);
                else
                    retire(speech.voice);
            }
            speech.voice = -1;
            speech.cutMs = 0;
            releaseWord(speech, speech.cur);
            bool said = false;
            while (speech.at < speech.count && !said)
            {
                // How this word is said: the game's marks on it.
                const SentenceWord &word = level.view.sentenceWords[speech.words[speech.at++]];
                const std::uint16_t index = word.sound;
                if (index >= level.view.soundCount || level.view.sounds[index].name[0] == '\0')
                    continue;
                const int pitch = (speech.pitch * (word.pitch != 0 ? word.pitch : 100)) / 100;
                const int loud = (speech.volume * word.volume) / 100;
                int volume = loud, pan = 64;
                if (speech.placed)
                    place(speech.pos, loud, speech.radius, volume, pan);
                // (In memory already, if something else of the map plays it.)
                const bool borrowed = index < count_ && clips_[index].data != nullptr;
                int v = -1;
                std::uint32_t lasts = 0;
                if (borrowed)
                {
                    lasts = clips_[index].ms;
                    v = start(SoundLayer::Voice, clips_[index], volume, pan, false, s, pitch);
                }
                else
                {
                    // Read ahead while the last word was said; or read now.
                    if (speech.ahead != index)
                        readAhead(speech, index);
                    if (speech.ahead != index)
                        continue; // (not on the card, or no memory for it)
                    speech.cur ^= 1;
                    speech.ahead = -1;
                    Clip &clip = speech.clip[speech.cur];
                    lasts = clip.ms;
                    v = start(SoundLayer::Voice, clip, volume, pan, false, s, pitch);
                }
                if (v >= 0 && speech.placed)
                {
                    Voice &voice = voices_[v];
                    voice.placed = true;
                    voice.volume = static_cast<std::uint8_t>(loud);
                    voice.radius = speech.radius;
                    for (int k = 0; k < 3; ++k)
                        voice.pos[k] = speech.pos[k];
                }
                speech.voice = v;
                // (Said faster it is over sooner; and a word may be cut off
                // before its end, which is how the game clips its speech.)
                const std::int32_t whole = static_cast<std::int32_t>(lasts * 100 / (pitch > 0 ? pitch : 100));
                const bool cut = word.end != 0 && word.end < 100;
                if (v >= 0)
                {
                    // Heard: over when the ARM7 says it is (or where it is
                    // cut); the time is only in case its end is never heard.
                    speech.cutMs = cut ? whole * word.end / 100 : 0;
                    speech.leftMs = whole * 2 + 500;
                    mixer_.voices[v].leftMs = speech.leftMs;
                }
                else
                {
                    // (Too far off to be heard, it still takes its time.)
                    speech.cutMs = 0;
                    speech.leftMs = cut ? whole * word.end / 100 : whole + 20;
                }
                said = true;
            }
            if (!said)
            {
                speech.count = speech.at = 0;
                releaseWord(speech, speech.cur ^ 1);
                speech.ahead = -1;
                continue;
            }
            // ... and the word after it is fetched while this one is said.
            if (speech.at < speech.count)
                readAhead(speech, level.view.sentenceWords[speech.words[speech.at]].sound);
        }
    }

    namespace
    {
        /// A track train's engine's pitch, in percent (plats.cpp's
        /// UpdateSound: 60 standing, 200 at a thousand units a second).
        int trainPitch(fx speed)
        {
            const int units = fxToInt(speed < 0 ? -speed : speed);
            const int pitch = 60 + units * (200 - 60) / 1000;
            return pitch > 200 ? 200 : pitch;
        }
    } // namespace

    void Sound::loopsTick()
    {
        const LevelView &view = level.view;
        // The loudest of the loops that are on, where the camera is: up to
        // kLoops of them while there are voices for it, and never fewer than
        // the layer's own share (the mixer sees to both).
        int want[kLoops], wantVolume[kLoops], wantPan[kLoops], wantWeight[kLoops];
        int wanted = 0;
        for (std::uint32_t i = 0; i < view.logicCount; ++i)
        {
            const LogicRecord &record = view.logic[i];
            const bool ambient = record.kind == static_cast<std::uint8_t>(LogicKind::Ambient) &&
                                 (record.flags & kLogicToggle);
            // (Something that turns hums for as long as it does; a track
            // train's engine runs for as long as it goes.)
            const bool train = record.kind == static_cast<std::uint8_t>(LogicKind::TrackTrain);
            const bool turning = (record.kind == static_cast<std::uint8_t>(LogicKind::Rotating) || train) &&
                                 view.logicSoundCount > i && view.logicSounds[i].move != kNoSound;
            if (!ambient && !turning)
                continue;
            const std::uint32_t sample = ambient ? record.extra[0] : view.logicSounds[i].move;
            const bool on = (train ? game.logic.states[i].speed != 0 : game.logic.states[i].running) &&
                            !game.logic.states[i].dead;
            if (!on && played_ != nullptr)
                played_[i >> 5] &= ~(1u << (i & 31));
            if (!on || sample >= static_cast<std::uint32_t>(count_) || clips_[sample].data == nullptr)
                continue;
            // Something that does not go round is played once when it is
            // switched on, as the game plays it, and that is all.
            if (!clips_[sample].loops() && played_ != nullptr &&
                (played_[i >> 5] & (1u << (i & 31))))
                continue;
            int volume = ambient ? static_cast<int>(record.extra[2]) : 100, pan = 64;
            if (turning && record.brush != kNoBrush)
            {
                const MoverHull &hull = game.logic.movers[record.brush];
                const fx at[3] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2,
                                  (hull.mins[2] + hull.maxs[2]) / 2};
                place(at, volume, kRadiusLarge, volume, pan);
            }
            else if (ambient && record.extra[1] != 0)
            {
                place(record.move, volume, static_cast<int>(record.extra[1]), volume, pan);
            }
            if (mixer_.scaled(SoundLayer::Ambience, volume) < 3)
                continue;
            // What is playing already is not put out by something only a
            // little louder: two of a loudness would change places ten
            // times a second.
            int weight = volume;
            for (int v = 0; v < Mixer::kVoices; ++v)
            {
                if (mixer_.voices[v].active && mixer_.voices[v].layer == SoundLayer::Ambience &&
                    mixer_.voices[v].tag == static_cast<int>(i))
                    weight += 12;
            }
            int at = wanted < kLoops ? wanted : kLoops - 1;
            if (wanted >= kLoops && weight <= wantWeight[at])
                continue;
            while (at > 0 && wantWeight[at - 1] < weight)
            {
                want[at] = want[at - 1];
                wantVolume[at] = wantVolume[at - 1];
                wantPan[at] = wantPan[at - 1];
                wantWeight[at] = wantWeight[at - 1];
                --at;
            }
            want[at] = static_cast<int>(i);
            wantVolume[at] = volume;
            wantPan[at] = pan;
            wantWeight[at] = weight;
            if (wanted < kLoops)
                ++wanted;
        }
        // Those already playing stay on their voices (and are only told of
        // a change worth telling); the rest stop; the new ones start.
        bool placed[kLoops] = {};
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            MixVoice &mix = mixer_.voices[v];
            if (!mix.active || mix.layer != SoundLayer::Ambience)
                continue;
            int found = -1;
            for (int n = 0; n < wanted; ++n)
            {
                if (want[n] == mix.tag)
                    found = n;
            }
            if (found < 0)
            {
                // (What does not go round is left to finish.)
                if (mix.leftMs < 0)
                    retire(v);
                continue;
            }
            placed[found] = true;
            Voice &voice = voices_[v];
            const int gain = mixer_.scaled(SoundLayer::Ambience, wantVolume[found]);
            const int dv = gain - voice.shown, dp = wantPan[found] - voice.pan;
            if (dv > 2 || dv < -2)
            {
                voice.shown = static_cast<std::uint8_t>(gain);
                mix.gain = voice.shown;
                arm7Audio.setVolume(voice.handle, static_cast<int>(gain * 2));
            }
            if (dp > 3 || dp < -3)
            {
                voice.pan = static_cast<std::uint8_t>(wantPan[found]);
                arm7Audio.setPanning(voice.handle, static_cast<int>(voice.pan * 2));
            }
            // (A train's engine is higher the faster it goes.)
            if (view.logic[want[found]].kind == static_cast<std::uint8_t>(LogicKind::TrackTrain) && !paused_)
            {
                const int rate = (1024 * trainPitch(game.logic.states[want[found]].speed)) / 100;
                if (rate - voice.rate > 16 || voice.rate - rate > 16)
                {
                    voice.rate = static_cast<std::uint16_t>(rate);
                    arm7Audio.setRate(voice.handle, voice.rate);
                }
            }
        }
        for (int n = 0; n < wanted; ++n)
        {
            if (placed[n])
                continue;
            const LogicRecord &record = view.logic[want[n]];
            const bool ambient = record.kind == static_cast<std::uint8_t>(LogicKind::Ambient);
            const int sample = static_cast<int>(ambient ? record.extra[0] : view.logicSounds[want[n]].move);
            const Clip &clip = clips_[sample];
            const bool round = clip.loops();
            const bool train = record.kind == static_cast<std::uint8_t>(LogicKind::TrackTrain);
            const int v = start(SoundLayer::Ambience, clip, wantVolume[n], wantPan[n], round, want[n],
                                train ? trainPitch(game.logic.states[want[n]].speed)
                                      : (ambient && record.pad != 0 ? record.pad : 100));
            if (v >= 0 && !round && played_ != nullptr)
                played_[want[n] >> 5] |= 1u << (want[n] & 31);
        }
        // What something made for as long as it moved (going round, or
        // played once and longer than the move), and is no longer there to
        // say it has stopped -- or has stopped, and its stop was never heard
        // (lost to a full queue, or to the pause).
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            const MixVoice &mix = mixer_.voices[v];
            if (!mix.active || mix.layer != SoundLayer::World || mix.tag < 0 ||
                mix.tag >= static_cast<int>(view.logicCount))
                continue;
            const LogicRecord &record = view.logic[mix.tag];
            const LogicState &state = game.logic.states[mix.tag];
            if (state.dead || (moverLoops(static_cast<LogicKind>(record.kind)) && !moverSounding(record, state)))
                stop(v);
        }
    }

    void Sound::roomTick()
    {
        // What the room sounds like: under water, that; otherwise the
        // nearest of the map's marked places that the player is within.
        int type = 0;
        if (player.state.waterLevel == 3)
        {
            type = kRoomUnderwater; // (the game's waterroom_type)
        }
        else
        {
            fx nearest = 0;
            const LevelView &view = level.view;
            for (std::uint32_t i = 0; i < view.roomCount; ++i)
            {
                const RoomRecord &room = view.rooms[i];
                const fx d[3] = {room.origin[0] - camera.pos[0], room.origin[1] - camera.pos[1],
                                 room.origin[2] - camera.pos[2]};
                const fx distance = fxLength(d);
                if (distance > fxFromInt(room.radius) || (type != 0 && distance >= nearest))
                    continue;
                // (A place marked "normal" counts: walking into one is how a
                // map ends an echo.)
                type = room.type == 0 ? -1 : room.type;
                nearest = distance;
            }
        }
        if (type == 0)
            return; // (in none: the last stays, as the game has it)
        type = type < 0 ? 0 : type;
        // (The DSP's rooms, or the ARM7's: the room kept either way, for
        // the other to be given if the player changes which.)
        if (type != room_)
        {
            room_ = type;
            if (!dspAudio.up())
                arm7Audio.setRoom(type);
        }
        if (dspAudio.up())
            dspAudio.room(type);
    }

#ifdef HLDS_CHILD_VRAMSOUND
    void Sound::vramTest(int rounds)
    {
        const Clip &plain = ui_[0];
        if (plain.data == nullptr)
            return;
        // Into bank D while it is the ARM9's (LCD, at 0x06860000), in words:
        // video memory takes no byte written to it.
        vramSetBankD(VRAM_D_LCD);
        auto *to = reinterpret_cast<volatile std::uint32_t *>(0x06860000);
        const auto *from = reinterpret_cast<const std::uint32_t *>(plain.data);
        for (std::uint32_t i = 0; i < (plain.bytes + 3) / 4; ++i)
            to[i] = from[i];
        // ... then the ARM7's, at its 0x06020000: an address only it reads.
        vramSetBankD(VRAM_D_ARM7_0x06020000);
        Clip inVram = plain;
        inVram.data = reinterpret_cast<std::uint8_t *>(0x06020000);
        inVram.shared = true;
        for (int round = 0; round < rounds; ++round)
        {
            start(SoundLayer::Ui, round % 2 == 0 ? inVram : plain, 127, 64, false);
            for (int blank = 0; blank < 60; ++blank)
            {
                swiWaitForVBlank();
                frame();
            }
        }
    }
#endif

    void Sound::ui(int which, int volume)
    {
        if (which < 0 || which >= kUiSoundCount || ui_[which].data == nullptr)
            return;
        start(SoundLayer::Ui, ui_[which], volume, 64, false);
    }

    void Sound::quit()
    {
        stopMusic();
        dspAudio.stop();
        arm7Audio.shutdown();
    }

    void Sound::loadMatchWarning()
    {
#ifndef HLDS_CHILD
        for (int n = 0; n < kMatchWarningWords; ++n)
        {
            if (global.hasData && warning_[n].data == nullptr)
                readClip(kMatchWarningFiles[n], warning_[n]);
        }
#endif
    }

    void Sound::dropMatchWarning()
    {
        warningAt_ = -1;
        for (Clip &clip : warning_)
        {
            std::free(clip.data);
            clip = Clip();
        }
    }

    void Sound::matchWarning()
    {
        warningAt_ = 0;
        warningMs_ = 0;
    }

    void Sound::warningTick(int ms)
    {
        if (warningAt_ < 0 || (warningMs_ -= ms) > 0)
            return;
        if (warningAt_ >= kMatchWarningWords)
        {
            warningAt_ = -1;
            return;
        }
        const Clip &clip = warning_[warningAt_++];
        // (A word that is not there: on to the next, which will not be either.)
        if (clip.data == nullptr)
            return;
        start(SoundLayer::Ui, clip, 127, 64, false);
        // (ADPCM: two samples to a byte.)
        warningMs_ = clip.rate != 0 ? static_cast<int>(clip.bytes * 2000u / clip.rate) : 0;
    }

    void Sound::setPaused(bool paused)
    {
        if (paused == paused_)
            return;
        killFading();
        lastVblank_ = Global::vblanks();
        // Everything of the game's own stops where it is, and goes on from
        // there: slowed to the slowest a channel goes, and silent, for as
        // long as the game stands still (Sound.hpp).
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            const Voice &voice = voices_[v];
            if (!mixer_.voices[v].active || mixer_.voices[v].layer == SoundLayer::Ui || voice.handle == 0)
                continue;
            if (paused)
            {
                // (A channel's clock is sixteen bits of the 16.7 MHz bus:
                // 256 Hz is as slow as it goes. A little above that.)
                const int slow = (300 * 1024) / (voice.hz > 0 ? voice.hz : 11025) + 1;
                arm7Audio.setVolume(voice.handle, 0);
                arm7Audio.setRate(voice.handle, static_cast<int>(slow));
            }
            else
            {
                arm7Audio.setRate(voice.handle, voice.rate);
                arm7Audio.setVolume(voice.handle, static_cast<int>(voice.shown * 2));
            }
        }
        if (paused)
        {
            // (The music is put down as it is across a load, and taken up
            // from the same moment.)
            holdMusic();
            paused_ = true;
        }
        else
        {
            paused_ = false;
            resumeMusic();
        }
    }

    int Sound::musicTrack() const
    {
        return musicTrack_;
    }

    std::uint32_t Sound::musicMs() const
    {
        if (musicHeld_)
            return musicHeldMs_;
        if (music_ == nullptr)
            return 0;
        const std::uint32_t played = musicPlayed();
        return static_cast<std::uint32_t>((static_cast<std::uint64_t>(musicSkipped_ + played) * 1000) / musicRate_);
    }

    void Sound::setMusic(int track, std::uint32_t ms)
    {
        stopMusic();
        musicHeld_ = false;
        musicTrack_ = -1;
        if (track < 0)
            return;
        // Put down at that moment, and taken up from it -- at once, or when
        // the game next goes on, as any music that was put down is.
        musicHeld_ = true;
        musicTrack_ = track;
        musicHeldMs_ = ms;
        resumeMusic();
    }

    void Sound::startMusic(int track)
    {
        if (track == musicTrack_ && music_ != nullptr)
            return;
        const std::uint32_t skip = musicSkipped_;
        musicSkipped_ = 0;
        stopMusic();
        if (!up_)
            return;
        // The ARM7 plays it, in stereo: the track's stereo file, its blocks
        // into the ring as they are.
        char name[16];
        musicFileName(track, name, sizeof(name));
        const std::string path = global.dataPath("music/") + name;
        std::FILE *file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
            return;
        StereoHeader header;
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        const long blockBytes = StereoMusic::kBlockWords * 2;
        if (std::fread(&header, sizeof(header), 1, file) != 1 || std::memcmp(header.magic, kStereoMagic, 4) != 0 ||
            header.version != kSoundVersion || header.blocks == 0 || header.rate == 0 ||
            size != static_cast<long>(sizeof(header)) + static_cast<long>(header.blocks) * blockBytes)
        {
            std::fclose(file);
            return;
        }
        // Taken up part of the way in, if it was put down there: at the
        // block that holds the moment.
        const std::uint32_t block = skip / StereoMusic::kPairsPerBlock;
        if (block >= header.blocks)
        {
            std::fclose(file); // (it was over already)
            return;
        }
        std::fseek(file, static_cast<long>(sizeof(header)) + static_cast<long>(block) * blockBytes, SEEK_SET);
        // (No buffer of the C library's own: the reads are whole blocks,
        // straight into the ring.)
        std::setvbuf(file, nullptr, _IONBF, 0);
        music_ = file;
        musicTrack_ = track;
        musicRate_ = header.rate;
        musicLeft_ = header.blocks - block;
        musicSkipped_ = block * StereoMusic::kPairsPerBlock;
        const std::uint32_t pairs = header.pairs > musicSkipped_ ? header.pairs - musicSkipped_ : 0;
        // The ring filled first, then the ARM7 told: it begins on music.
        arm7Audio.musicPrepare();
        for (std::uint32_t n = 0; n < kMusicRingBlocks && musicLeft_ != 0; ++n)
        {
            std::uint16_t *slot = musicRing + n * StereoMusic::kBlockWords;
            if (std::fread(slot, 2, StereoMusic::kBlockWords, music_) != StereoMusic::kBlockWords)
            {
                musicLeft_ = 0;
                break;
            }
            DC_FlushRange(slot, StereoMusic::kBlockWords * 2);
            arm7Audio.musicWrote(1);
            --musicLeft_;
        }
        arm7Audio.musicOpen(musicRing, kMusicRingBlocks, header.rate, pairs, musicGain());
    }

    std::uint32_t Sound::musicPlayed() const
    {
        return arm7Audio.musicRead();
    }

    void Sound::stopMusic()
    {
        if (music_ == nullptr)
            return;
        arm7Audio.musicStop();
        std::fclose(music_);
        music_ = nullptr;
        musicTrack_ = -1;
    }

    void Sound::musicTick()
    {
        if (music_ == nullptr)
            return;
        // The ring kept full: a block may be written over once the ARM7 has
        // moved past it.
        const std::uint32_t reading = arm7Audio.musicRead() / StereoMusic::kPairsPerBlock;
        std::uint32_t written = arm7Audio.musicWritten();
        while (musicLeft_ != 0 && written < reading + kMusicRingBlocks)
        {
            std::uint16_t *slot = musicRing + (written % kMusicRingBlocks) * StereoMusic::kBlockWords;
            if (std::fread(slot, 2, StereoMusic::kBlockWords, music_) != StereoMusic::kBlockWords)
            {
                musicLeft_ = 0; // (the file ends short: so does the track)
                break;
            }
            DC_FlushRange(slot, StereoMusic::kBlockWords * 2);
            arm7Audio.musicWrote(1);
            ++written;
            --musicLeft_;
        }
        // The track is over once all of it has been played.
        if (arm7Audio.musicOver())
            stopMusic();
    }

    void Sound::frame()
    {
        const std::uint32_t now = Global::vblanks();
        const int ms = clock_.ms(static_cast<std::int32_t>(now - lastVblank_));
        lastVblank_ = now;
        arm7Audio.frame();
        warningTick(ms);
        musicTick();
        dspAudio.frame();
        // A DSP that has let the game down is done without from here on:
        // the music goes on the DS's way.
        if (extended_ && dspAudio.failed())
        {
            // (Said in the log, so that it is known to have happened.)
            if (std::FILE *log = perfLogging() ? std::fopen(global.dataPath("perf.txt").c_str(), "a") : nullptr)
            {
                std::fprintf(log, "dsp given up: ran short %lu times, %s\n", static_cast<unsigned long>(dspAudio.dry()),
                             dspAudio.up() ? "answering" : "not answering");
                std::fclose(log);
            }
            setDsp(false);
            // (The game's own rooms, then, until it is asked for again.)
            if (settings.roomEffects == kRoomEnhanced)
                settings.roomEffects = kRoomClassic;
        }
        // What has run its time is over: its voice is free again.
        fadeTick();
        const std::uint32_t ended = mixer_.tick(ms, paused_);
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            if (!((ended >> v) & 1))
                continue;
            if (voices_[v].handle != 0)
                arm7Audio.stop(voices_[v].handle);
            voices_[v] = Voice();
        }
        // A sound the ARM7 says has ended gives its voice back at once.
        // (A speaker's own voice is the speaker's to let go: speechTick.)
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            const MixVoice &mix = mixer_.voices[v];
            if (!mix.active || mix.leftMs < 0 || voices_[v].handle == 0 || arm7Audio.playing(voices_[v].handle))
                continue;
            if (mix.layer == SoundLayer::Voice && mix.tag >= 0 && mix.tag < kSpeakers)
                continue;
            voices_[v] = Voice();
            mixer_.stop(v);
        }
        if (clips_ == nullptr)
            return;
        SoundQueue &queue = game.heard;
        if (paused_)
        {
            queue.count = 0;
            return;
        }
        // The room the player is in, looked at four times a second, as the
        // game does: for the DSP's echo, or the ARM7's rooms.
        roomDue_ -= ms;
        if (roomDue_ <= 0)
        {
            roomDue_ = 250;
            roomTick();
        }
        for (int i = 0; i < queue.count; ++i)
        {
            const SoundEvent &event = queue.events[i];
            if (event.flags & kSoundSentence)
                say(event);
            else if (event.flags & kSoundLoopOff)
                stopLoop(event.tag);
            else if (event.flags & kSoundLoopOn)
                playLoop(event);
            else
                playOnce(event);
        }
        queue.count = 0;
        speechTick(ms);
        // The loops, and whatever has a place, are looked at a few times a
        // second: each change is a message to the other processor.
        loopsDue_ -= ms;
        if (loopsDue_ <= 0)
        {
            loopsDue_ = 100;
            loopsTick();
            follow();
        }
        if (game.logic.musicRequest >= 0)
        {
            startMusic(game.logic.musicRequest);
            game.logic.musicRequest = -1;
        }
    }
} // namespace hlrt
