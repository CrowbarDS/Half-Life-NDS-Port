// Sounds: reading the game's WAVs, packing them as the DS plays them, and
// the spoken lines of sentences.txt.
//
// The packing is checked the only way that means anything: what the DS's
// chip would make of the packed sound is compared with what went in.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "HlDspRoom.hpp"
#include "HlSound.hpp"
#include "HlStereoMusic.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/SoundCook.hpp"

// The DSP program's arithmetic, as the DSP runs it (tools/dspsim checks that
// the two are the same, in an emulated DSP).
extern "C"
{
#include "dsp/hl_dsp.h"
}

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    /// A WAV file's bytes: 8 or 16 bits, one channel, with a cue point if
    /// `cue` is not negative.
    std::vector<std::uint8_t> makeWav(const std::vector<std::int16_t> &samples, int rate, int bits, int cue)
    {
        std::vector<std::uint8_t> out;
        auto put32 = [&](std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        };
        auto put16 = [&](std::uint16_t v)
        {
            out.push_back(static_cast<std::uint8_t>(v));
            out.push_back(static_cast<std::uint8_t>(v >> 8));
        };
        auto tag = [&](const char *t) { out.insert(out.end(), t, t + 4); };
        const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size() * (bits / 8));
        tag("RIFF");
        put32(0);
        tag("WAVE");
        tag("fmt ");
        put32(16);
        put16(1);
        put16(1);
        put32(static_cast<std::uint32_t>(rate));
        put32(static_cast<std::uint32_t>(rate * (bits / 8)));
        put16(static_cast<std::uint16_t>(bits / 8));
        put16(static_cast<std::uint16_t>(bits));
        if (cue >= 0)
        {
            tag("cue ");
            put32(28);
            put32(1);
            put32(1);
            put32(static_cast<std::uint32_t>(cue));
            tag("data");
            put32(0);
            put32(0);
            put32(static_cast<std::uint32_t>(cue));
        }
        tag("data");
        put32(dataBytes);
        for (std::int16_t s : samples)
        {
            if (bits == 8)
                out.push_back(static_cast<std::uint8_t>((s >> 8) + 128));
            else
                put16(static_cast<std::uint16_t>(s));
        }
        return out;
    }

    std::vector<std::int16_t> tone(int rate, double hz, double seconds, double level)
    {
        std::vector<std::int16_t> out(static_cast<std::size_t>(rate * seconds));
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<std::int16_t>(std::lround(std::sin(i * 2.0 * M_PI * hz / rate) * level));
        return out;
    }

    /// How far `b` is from `a`, as a fraction of how loud `a` is.
    double noise(const std::vector<std::int16_t> &a, const std::vector<std::int16_t> &b)
    {
        double signal = 0, error = 0;
        const std::size_t count = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            signal += static_cast<double>(a[i]) * a[i];
            error += static_cast<double>(a[i] - b[i]) * (a[i] - b[i]);
        }
        return signal > 0 ? std::sqrt(error / signal) : 1.0;
    }

    void testWav()
    {
        const std::vector<std::int16_t> in = tone(11025, 440, 0.25, 12000);
        WavData wav;
        std::string error;
        check(parseWav(makeWav(in, 11025, 16, -1), wav, error) && wav.rate == 11025 &&
                  wav.samples == in && wav.loopStart == -1,
              "a 16-bit WAV is read as it is");
        check(parseWav(makeWav(in, 11025, 8, 1000), wav, error) && wav.samples.size() == in.size() &&
                  wav.loopStart == 1000 && std::abs(wav.samples[10] - in[10]) < 256,
              "an 8-bit WAV is read, and its cue point is where it loops from");
        std::vector<std::uint8_t> junk = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'X'};
        check(!parseWav(junk, wav, error), "what is not a WAV is refused");
        // Cut short in the middle of its samples: read as far as it goes.
        std::vector<std::uint8_t> cut = makeWav(in, 11025, 16, -1);
        cut.resize(cut.size() - 1001);
        check(parseWav(cut, wav, error) && wav.samples.size() == in.size() - 501,
              "a WAV cut short is read as far as it goes");
    }

    void testEncode()
    {
        WavData wav;
        wav.rate = 11025;
        wav.samples = tone(11025, 440, 0.5, 12000);
        const CookedSound sound = encodeSound(wav, 11025, false);
        check(sound.data.size() % 4 == 0 && sound.samples % 8 == 0 &&
                  sound.data.size() == 4 + sound.samples / 2 && sound.loopWord == kSoundNoLoop,
              "a packed sound is whole words: a header and four bits a sample");
        const std::vector<std::int16_t> back = decodeSound(sound);
        const double off = noise(wav.samples, back);
        std::printf("test_sound: a 440 Hz tone comes back %.1f%% off\n", off * 100);
        check(off < 0.05, "a tone comes back within a twentieth of itself");

        // Something like speech: two tones and a burst, quiet then loud.
        std::vector<std::int16_t> mixed = tone(11025, 220, 0.5, 3000);
        const std::vector<std::int16_t> high = tone(11025, 1800, 0.5, 9000);
        for (std::size_t i = mixed.size() / 2; i < mixed.size(); ++i)
            mixed[i] = static_cast<std::int16_t>(mixed[i] + high[i]);
        wav.samples = mixed;
        const double rough = noise(mixed, decodeSound(encodeSound(wav, 11025, false)));
        std::printf("test_sound: a sound with a sudden loud part comes back %.1f%% off\n", rough * 100);
        check(rough < 0.12, "a sound that changes suddenly is still itself");

        // Half the rate: half the samples, the same pitch.
        wav.rate = 22050;
        wav.samples = tone(22050, 440, 0.5, 12000);
        const CookedSound halved = encodeSound(wav, 11025, false);
        check(halved.rate == 11025 && halved.samples >= 5512 && halved.samples <= 5520 &&
                  noise(tone(11025, 440, 0.5, 12000), decodeSound(halved)) < 0.08,
              "a 22 kHz sound is brought down to 11 kHz at the same pitch");

        // Loops: from the cue point, on a word, never the header's.
        wav.rate = 11025;
        wav.samples = tone(11025, 441, 1.0, 8000); // 25 samples a cycle
        wav.loopStart = 4000;
        const CookedSound loop = encodeSound(wav, 11025, true);
        check(loop.loopWord == (4 + 4000 / 2) / 4 && loop.samples % 8 == 0, "a loop starts at its cue point");
        wav.loopStart = -1;
        check(encodeSound(wav, 11025, true).loopWord == hldata::kSoundNoLoop,
              "... and does not go round at all if it has none: it is played through, as the game plays it");

        // The file round trip, and a damaged file.
        const std::vector<std::uint8_t> file = packSound(loop, 1234);
        CookedSound read;
        SoundHeader header;
        check(unpackSound(file, read, header) && read.data == loop.data && read.loopWord == loop.loopWord &&
                  header.sourceHash == 1234 && header.rate == 11025,
              "the file reads back as it was written");
        std::vector<std::uint8_t> bad = file;
        bad.resize(bad.size() - 4);
        check(!unpackSound(bad, read, header), "a file of the wrong length is refused");
        bad = file;
        bad[12] = 0xFF; // the loop point, far past the end
        bad[13] = 0xFF;
        bad[14] = 0x0F;
        check(!unpackSound(bad, read, header), "... and one that loops from beyond its end");

        check(soundCardName("ambience/labdrone2.wav", 11025, true) == "ambience_labdrone2.11l" &&
                  soundCardName("Doors/DoorMove2.wav", 5512, false) == "doors_doormove2.5k",
              "a sound's name on the card says where it came from, its rate, and if it loops");
        check(soundCardName(std::string(80, 'a') + "/x.wav", 11025, false).size() < sizeof(SoundRef::name),
              "... and fits the level's record however long");
    }

    void testStereo()
    {
        // Two different tunes, a little over two blocks of them.
        const int rate = 44100;
        const std::vector<std::int16_t> left = tone(rate, 330, 0.2, 10000);
        const std::vector<std::int16_t> right = tone(rate, 550, 0.2, 7000);
        const std::vector<std::uint8_t> file = encodeMusicStereo(left, right, rate);
        std::vector<std::int16_t> l, r;
        check(decodeMusicStereo(file, 0, l, r), "a stereo track is written as its file");
        const std::vector<std::int16_t> wantL = resample(left, rate, kStereoRate);
        const std::vector<std::int16_t> wantR = resample(right, rate, kStereoRate);
        const double offL = noise(wantL, l), offR = noise(wantR, r);
        std::printf("test_sound: the stereo track comes back %.1f%% and %.1f%% off\n", offL * 100, offR * 100);
        check(l.size() >= wantL.size() && l.size() % (kMusicBlockWords * 2) == 0 && offL < 0.06 && offR < 0.06,
              "... and each channel comes back as itself, in whole blocks");
        // Taken up at the second block, it is the same from there on.
        std::vector<std::int16_t> l2, r2;
        const std::size_t skip = static_cast<std::size_t>(kMusicBlockWords) * 2;
        bool same = decodeMusicStereo(file, 1, l2, r2) && l2.size() + skip == l.size();
        for (std::size_t i = 0; same && i < l2.size(); ++i)
            same = l2[i] == l[i + skip] && r2[i] == r[i + skip];
        check(same, "... and can be taken up at any block");
        std::vector<std::uint8_t> bad = file;
        bad.pop_back();
        check(!decodeMusicStereo(bad, 0, l, r), "a track file of the wrong length is refused");
    }

    void testEcho()
    {
        // The loudest room there is, sent the loudest thing there is, for a
        // second: it must not sit against the stops; and when the sound
        // ends, the echo must die away.
        // (The DSP's rooms are HlDspRoom.hpp's; test_dsproom has them all.)
        std::uint16_t cavern[HL_DSP_ROOM_WORDS];
        hldata::dspRoomBlock(25, cavern);
        hlDspInit();
        for (int i = 0; i < HL_DSP_ROOM_WORDS; ++i)
            hlRoomBlock[i] = cavern[i];
        hlDspCommand(HL_DSP_CMD_ROOM, 0, 0);
        std::uint32_t seed = 5;
        int pinned = 0, loudest = 0;
        double during = 0, after = 0, late = 0;
        const int second = 47605;
        for (int tick = 0; tick < second * 6; ++tick)
        {
            // What is to echo arrives a quarter as often as samples go out.
            if (tick < second && (tick & 3) == 0)
            {
                seed = seed * 1664525u + 1013904223u;
                hlSendRing[hlSendWrite] = static_cast<std::int16_t>((seed >> 16) - 32768);
                hlSendWrite = static_cast<std::uint16_t>((hlSendWrite + 1) % HL_DSP_SEND_WORDS);
            }
            hlDspTick();
            const int out = hlOutL;
            loudest = std::max(loudest, std::abs(out));
            pinned += std::abs(out) >= 32767;
            if (tick < second)
                during += static_cast<double>(out) * out;
            else if (tick < second * 2)
                after += static_cast<double>(out) * out;
            else if (tick >= second * 5)
                late += static_cast<double>(out) * out;
        }
        during = std::sqrt(during / second);
        after = std::sqrt(after / second);
        late = std::sqrt(late / second);
        std::printf("test_sound: echo of a cavern: %.0f while the sound lasts, %.0f in the second after, "
                    "%.1f four seconds on; loudest %d\n",
                    during, after, late, loudest);
        check(during > 500 && pinned < second / 100, "the echo is heard, and not against the stops");
        check(after > during / 20 && after < during, "... it rings on when the sound has stopped");
        check(late < during / 100, "... and dies away");
        // No room: nothing at all.
        hlDspInit();
        hlSendRing[0] = 30000;
        hlSendWrite = 1;
        int any = 0;
        for (int tick = 0; tick < 4000; ++tick)
        {
            hlDspTick();
            any |= hlOutL;
        }
        check(any == 0, "with no room set there is no echo");
    }

    /// The music as the ARM7 plays it (HlStereoMusic.hpp), from the track's
    /// file as the converter writes it: the track, as the converter's own
    /// decoder has it; and a block of rubbish over when the next begins
    /// (each block brings its decoders' states).
    void testMusicBlocks()
    {
        const int rate = 23802;
        const std::vector<std::uint8_t> file =
            encodeMusicStereo(tone(rate, 330, 0.3, 10000), tone(rate, 550, 0.3, 7000), rate);
        std::vector<std::int16_t> wantL, wantR;
        decodeMusicStereo(file, 0, wantL, wantR);
        const std::size_t blockWords = hldata::StereoMusic::kBlockWords;
        auto play = [&](bool spoil, std::vector<std::int32_t> &left)
        {
            static std::vector<std::uint16_t> ring;
            ring.assign(3 * blockWords, 0);
            std::uint32_t seed = 7;
            for (std::size_t w = 0; w < ring.size(); ++w)
            {
                ring[w] = static_cast<std::uint16_t>(file[16 + w * 2] | (file[16 + w * 2 + 1] << 8));
                if (spoil && w >= blockWords + 4 && w < 2 * blockWords)
                {
                    seed = seed * 1664525u + 1013904223u;
                    ring[w] = static_cast<std::uint16_t>(seed >> 12);
                }
            }
            static hldata::StereoMusic music;
            const std::uint32_t pairs = 3 * hldata::StereoMusic::kPairsPerBlock;
            music.open(ring.data(), 3, rate, rate, pairs, 256);
            left.assign(pairs, 0);
            std::vector<std::int32_t> right(pairs, 0);
            music.mix(left.data(), right.data(), static_cast<int>(pairs), 3);
        };
        std::vector<std::int32_t> clean, spoiled;
        play(false, clean);
        // (At its own rate each sample is the pair before: the line to the
        // next not yet begun.)
        bool same = wantL.size() >= clean.size();
        for (std::size_t i = 1; same && i < clean.size(); ++i)
            same = clean[i] == wantL[i - 1];
        check(same, "music played from its blocks is the track, as the converter decodes it");
        play(true, spoiled);
        const std::size_t third = 2 * hldata::StereoMusic::kPairsPerBlock;
        bool healed = true;
        for (std::size_t i = third + 2; i < third + 2000; ++i)
            healed = healed && spoiled[i] == clean[i];
        check(healed, "a block of rubbish is over when the next block begins");
        // Starved: the interrupt's side with nothing to take fades, it does
        // not jump.
        hlDspInit();
        hlPortL = 20000;
        int biggest = 0, last = hlPortL;
        for (int n = 0; n < 40; ++n)
        {
            hlDspPull();
            biggest = std::max(biggest, std::abs(hlPortL - last));
            last = hlPortL;
        }
        check(biggest <= 5000 && std::abs(hlPortL) < 10 && hlStarved == 40,
              "a speaker port left with nothing fades out");
    }

    void testSentences()
    {
        const SentenceBook book = parseSentences(
            "// a comment\n"
            "SC_OK0 scientist/alright\n"
            "C1A0_SCI_GM scientist/c1a0_sci_gm1 c1a0_sci_gm\n"
            "HELLO hello, this(e95) is(p120 t20) a test.\n"
            "\n"
            "EMPTY\n");
        const Sentence *ok = book.find("!sc_ok0");
        check(ok != nullptr && ok->words.size() == 1 && ok->words[0] == "scientist/alright.wav",
              "a line is found by its name, with or without the !, in any case");
        check(book.find("SC_OK") == ok, "a group's name stands for its first line");
        const Sentence *gm = book.find("C1A0_SCI_GM");
        check(gm != nullptr && gm->words.size() == 2 && gm->words[1] == "scientist/c1a0_sci_gm.wav",
              "the folder said with the first word holds for the line");
        const Sentence *hello = book.find("HELLO");
        check(hello != nullptr && hello->words.size() == 7 && hello->words[0] == "vox/hello.wav" &&
                  hello->words[1] == "vox/_comma.wav" && hello->words[3] == "vox/is.wav" &&
                  hello->words[5] == "vox/test.wav" && hello->words[6] == "vox/_period.wav",
              "the announcer's words: no folder, and its commas and full stops are pauses");
        check(hello != nullptr && hello->said.size() == 7 && hello->said[2].end == 95 && hello->said[2].pitch == 100 &&
                  hello->said[3].pitch == 120 && hello->said[4].pitch == 100,
              "a word's own marks are its own: cut short, said higher, and the next word as it was");
        // The game's own lines, as they are written.
        const SentenceBook real = parseSentences(
            "HG_GREN3 hgrunt/(p110 t40) clik(p120) get!(e78) down!(t30) clik\n"
            "HEV_MED0 fvox/(p140) boop, boop, boop, (p100) innsufficient_medical\n"
            "FAR_WAR11 ambience/(v60) distantmortar1 distantmortar1, distantmortar2\n"
            "HG_GREN0 hgrunt/clik(p120) grenade! clik\n"
            "SC_X scientist/hello, there\n");
        const Sentence *gren = real.find("HG_GREN3");
        check(gren != nullptr && gren->words.size() == 4 && gren->words[0] == "hgrunt/clik.wav" &&
                  gren->words[1] == "hgrunt/get!.wav" && gren->said[0].pitch == 120 && gren->said[1].pitch == 110 &&
                  gren->said[1].end == 78 && gren->said[3].pitch == 110,
              "brackets by themselves hold for the words after; a word's own override them for it alone");
        const Sentence *med = real.find("HEV_MED0");
        check(med != nullptr && med->words.size() == 7 && med->said[0].pitch == 140 && med->said[4].pitch == 140 &&
                  med->said[6].pitch == 100 && med->words[1] == "fvox/_comma.wav",
              "the standing pitch changes part of the way through a line");
        const Sentence *war = real.find("FAR_WAR11");
        check(war != nullptr && war->said[0].volume == 60 && war->said.back().volume == 60, "volume holds likewise");
        const Sentence *gren0 = real.find("HG_GREN0");
        check(gren0 != nullptr && gren0->said[0].pitch == 120 && gren0->said[1].pitch == 100,
              "one word's pitch is not the line's");
        const Sentence *sci = real.find("SC_X");
        check(sci != nullptr && sci->words[1] == "scientist/_comma.wav", "a folder's own pause, where it has one");
        check(book.find("EMPTY") == nullptr && book.find("NOPE") == nullptr,
              "a line with no words is no line");
    }

    void testRealSounds(const char *dir)
    {
        namespace fs = std::filesystem;
        // Every sound the common set names is there, and survives packing.
        int found = 0, worstOf = -1;
        double worst = 0;
        for (int c = 0; c < static_cast<int>(CommonSound::Count); ++c)
        {
            const fs::path path = fs::path(dir) / "sound" / commonSoundPath(static_cast<CommonSound>(c));
            WavData wav;
            std::string error;
            if (!readWav(path, wav, error))
            {
                std::printf("test_sound: %s: %s\n", path.string().c_str(), error.c_str());
                continue;
            }
            ++found;
            const CookedSound sound = encodeSound(wav, kSoundRate, false);
            const double off = noise(resample(wav.samples, wav.rate, kSoundRate), decodeSound(sound));
            if (off > worst)
            {
                worst = off;
                worstOf = c;
            }
        }
        std::printf("test_sound: %d of %d common sounds read; the worst comes back %.1f%% off (%s)\n", found,
                    static_cast<int>(CommonSound::Count), worst * 100,
                    worstOf >= 0 ? commonSoundPath(static_cast<CommonSound>(worstOf)) : "-");
        check(found == static_cast<int>(CommonSound::Count), "the common sounds are all in the game");
        // (The worst is a roach's skitter, which is nothing but noise: 38%.)
        check(worst < 0.40, "... and all survive packing (noise is the hardest thing to pack)");

        std::ifstream file(fs::path(dir) / "sound" / "sentences.txt", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const SentenceBook book = parseSentences(text);
        int lines = 0, words = 0, missing = 0;
        for (const auto &entry : book.lines)
        {
            ++lines;
            for (const std::string &word : entry.second.words)
            {
                ++words;
                missing += !fs::exists(fs::path(dir) / "sound" / word);
            }
        }
        std::printf("test_sound: sentences.txt: %d lines, %d words, %d of them not on disk\n", lines,
                    words, missing);
        check(lines > 1000, "the game's sentences are read");
        check(missing * 20 < words, "... and nearly all of their words are sounds that exist");
    }

    // ------------------------------------------------------- a full queue

    /// A frame with more heard in it than the queue holds: what is given up
    /// is a sound heard once, never a loop's stop -- which, lost, would leave
    /// a door's move sound going round for ever.
    void testFullQueue()
    {
        const fx at[3] = {0, 0, 0};
        SoundQueue q;
        for (int i = 0; i < kMaxSoundEvents; ++i)
            q.add(static_cast<std::uint16_t>(i), at, 127, kRadiusLarge);
        q.add(100, at, 127, kRadiusLarge);
        check(q.count == kMaxSoundEvents && q.events[kMaxSoundEvents - 1].sound == kMaxSoundEvents - 1,
              "a full queue drops one more sound heard once");
        q.add(kNoSound, nullptr, 0, 0, kSoundLoopOff, 100, 7);
        check(q.count == kMaxSoundEvents && (q.events[kMaxSoundEvents - 1].flags & kSoundLoopOff) &&
                  q.events[kMaxSoundEvents - 1].tag == 7 && q.events[kMaxSoundEvents - 2].sound == kMaxSoundEvents - 2,
              "... but a loop's stop takes the place of the latest of them");
        q.add(50, at, 127, kRadiusLarge, kSoundLoopOn, 100, 8);
        check(q.count == kMaxSoundEvents && (q.events[kMaxSoundEvents - 1].flags & kSoundLoopOn) &&
                  (q.events[kMaxSoundEvents - 2].flags & kSoundLoopOff) && q.events[0].sound == 0,
              "... and so does a loop's start, the rest kept in their order");
        check(q.dropped == 3, "each sound lost is counted, the one too many and those given up");

        // A start, then the queue filled, then its stop: the stop must still
        // come after the start, or the loop would be started again after it.
        q.count = 0;
        q.add(50, at, 127, kRadiusLarge, kSoundLoopOn, 100, 3);
        for (int i = 1; i < kMaxSoundEvents; ++i)
            q.add(static_cast<std::uint16_t>(i), at, 127, kRadiusLarge);
        q.add(kNoSound, nullptr, 0, 0, kSoundLoopOff, 100, 3);
        int on = -1, off = -1;
        for (int i = 0; i < q.count; ++i)
        {
            if (q.events[i].tag == 3 && (q.events[i].flags & kSoundLoopOn))
                on = i;
            if (q.events[i].tag == 3 && (q.events[i].flags & kSoundLoopOff))
                off = i;
        }
        check(on == 0 && off == kMaxSoundEvents - 1, "a loop's stop comes after its own start");

        // Only spoken lines and loops: a start has nothing to give up, a
        // stop gives up the latest start.
        q.count = 0;
        q.dropped = 0;
        for (int i = 0; i < kMaxSoundEvents - 1; ++i)
            q.add(static_cast<std::uint16_t>(i), at, 127, kRadiusLarge, kSoundSentence);
        q.add(60, at, 127, kRadiusLarge, kSoundLoopOn, 100, 4);
        q.add(61, at, 127, kRadiusLarge, kSoundLoopOn, 100, 5);
        check(q.count == kMaxSoundEvents && q.events[kMaxSoundEvents - 1].tag == 4,
              "a loop's start gives up no spoken line");
        q.add(kNoSound, nullptr, 0, 0, kSoundLoopOff, 100, 9);
        check(q.count == kMaxSoundEvents && q.events[kMaxSoundEvents - 1].tag == 9 &&
                  (q.events[kMaxSoundEvents - 2].flags & kSoundSentence),
              "a loop's stop gives up a start before a spoken line");
        check(q.dropped == 2, "... the start that found no room and the one given up, both counted");
    }
} // namespace

/// A sound's better copy for a DSi is the same name at another rate.
static void testTwin()
{
    hldata::SoundRef ref{};
    char out[48];
    std::strcpy(ref.name, "weapons_hks1.11k");
    hldstest::check(!hldata::soundTwinName(ref, out, sizeof(out)), "a sound with no better copy has no name for one");
    ref.twin = 22;
    hldstest::check(hldata::soundTwinName(ref, out, sizeof(out)) && std::string(out) == "weapons_hks1.22k",
                "a DSi's copy is the same name at its own rate");
    std::strcpy(ref.name, "ambience_wind2.5l");
    ref.twin = 11;
    hldstest::check(hldata::soundTwinName(ref, out, sizeof(out)) && std::string(out) == "ambience_wind2.11l",
                "... a looping one, halved for a DS, too");
    std::strcpy(ref.name, "noextension");
    hldstest::check(!hldata::soundTwinName(ref, out, sizeof(out)), "a name of no such shape has none");
    hldstest::check(hlds::soundCardName("weapons/hks1.wav", 22050, false) == "weapons_hks1.22k",
                "the converter names it the same way");
}

int main()
{
    testTwin();
    testWav();
    testEncode();
    testSentences();
    testStereo();
    testEcho();
    testMusicBlocks();
    testFullQueue();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
        testRealSounds(dir);
    else
        std::printf("test_sound: HLDS_VALVE_DIR not set, the game's sounds were not read\n");
    if (g_failures == 0)
        std::printf("test_sound: ok\n");
    return g_failures == 0 ? 0 : 1;
}
