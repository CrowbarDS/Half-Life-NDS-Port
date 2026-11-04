// Who gets a voice (shared/HlMixer.hpp): a free one while there are any; a
// layer's own share whatever the others want; the quietest and oldest giving
// way; a new line cutting in on the last; each layer's own volume.

#include <cstdio>

#include "HlMixer.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    int play(Mixer &mixer, SoundLayer layer, int gain, int ms = 1000)
    {
        const int v = mixer.take(layer, gain);
        if (v >= 0)
            mixer.start(v, layer, gain, ms);
        return v;
    }
} // namespace

int main()
{
    {
        int shares = 0;
        for (int l = 0; l < static_cast<int>(SoundLayer::Count); ++l)
            shares += Mixer::share(static_cast<SoundLayer>(l));
        check(shares == Mixer::kVoices, "the layers' shares are all the voices there are");
    }
    {
        // While voices are free anything may have them.
        Mixer mixer;
        bool all = true;
        for (int n = 0; n < Mixer::kVoices; ++n)
            all = all && play(mixer, SoundLayer::World, 50 + n) >= 0;
        check(all && mixer.count(SoundLayer::World) == Mixer::kVoices, "with voices free a layer may have more than its share");
        // The player's gun now: one back from the world, its quietest.
        const int v = mixer.take(SoundLayer::Player, 10);
        check(v >= 0 && mixer.voices[v].layer == SoundLayer::World && mixer.voices[v].gain == 50,
              "a layer under its share takes one back from a layer over its own: the quietest of it");
        mixer.start(v, SoundLayer::Player, 10, 1000);
        play(mixer, SoundLayer::Player, 10);
        play(mixer, SoundLayer::Player, 10);
        check(mixer.count(SoundLayer::Player) == 3 && mixer.count(SoundLayer::World) == Mixer::kVoices - 3,
              "... as often as it takes to have its share");
        // At its share, with the world still over: it has only its own.
        const int own = mixer.take(SoundLayer::Player, 5);
        check(own < 0, "at its share, a sound quieter than all it has is not heard");
        const int louder = mixer.take(SoundLayer::Player, 60);
        check(louder >= 0 && mixer.voices[louder].layer == SoundLayer::Player,
              "... and a louder one takes the place of its own quietest, nobody else's");
        // Two spoken lines and the menu get theirs too; ambience its three.
        check(play(mixer, SoundLayer::Voice, 1) >= 0 && play(mixer, SoundLayer::Voice, 1) >= 0 &&
                  play(mixer, SoundLayer::Ui, 1) >= 0,
              "two voices may speak at once, and the menu has its one");
        for (int n = 0; n < 3; ++n)
            play(mixer, SoundLayer::Ambience, 20);
        check(mixer.count(SoundLayer::Ambience) == 3 && mixer.count(SoundLayer::World) == 4 &&
                  mixer.count(SoundLayer::Player) == 3 && mixer.count(SoundLayer::Voice) == 2,
              "with every layer wanting all it can have, each has exactly its share");
        // Now nobody is over: the world's own quietest gives way to louder.
        int quietest = 127;
        for (const MixVoice &voice : mixer.voices)
            quietest = voice.layer == SoundLayer::World && voice.gain < quietest ? voice.gain : quietest;
        const int v2 = mixer.take(SoundLayer::World, 127);
        check(v2 >= 0 && mixer.voices[v2].layer == SoundLayer::World && mixer.voices[v2].gain == quietest,
              "the world's loudest newcomer takes the world's quietest");
        check(mixer.take(SoundLayer::World, quietest - 1) < 0, "... and one quieter than all of them, none");
        check(mixer.take(SoundLayer::Ambience, 126) >= 0 &&
                  mixer.voices[mixer.take(SoundLayer::Ambience, 126)].layer == SoundLayer::Ambience,
              "nothing is taken from a layer that has only its share");
    }
    {
        // A new line cuts in on the last, however loud that was.
        Mixer mixer;
        for (int n = 0; n < Mixer::kVoices; ++n)
            play(mixer, n < 2 ? SoundLayer::Voice : SoundLayer::World, 100);
        const int v = mixer.take(SoundLayer::Voice, 1);
        check(v >= 0 && mixer.voices[v].layer == SoundLayer::Voice,
              "with two speaking, a new line takes the voice from one of them");
        // When the hardware itself has no room: what matters least of all.
        const int least = mixer.weakest();
        check(least >= 0 && mixer.voices[least].layer == SoundLayer::World,
              "what gives way to the hardware is of the layer that gives way first");
        Mixer menu;
        play(menu, SoundLayer::Ui, 1);
        check(menu.weakest() < 0, "... and never the menu's");
        // The game standing still: only the menu's sounds run their time.
        Mixer still;
        play(still, SoundLayer::World, 50, 100);
        play(still, SoundLayer::Ui, 50, 100);
        still.tick(5000, true);
        check(still.count(SoundLayer::World) == 1 && still.count(SoundLayer::Ui) == 0,
              "while the game stands still a sound of its own does not run out");
    }
    {
        // Among equals the oldest goes; what has run its time is free.
        Mixer mixer;
        int first = -1;
        for (int n = 0; n < Mixer::kVoices; ++n)
        {
            const int v = play(mixer, SoundLayer::World, 40, n == 3 ? 100 : 5000);
            first = n == 0 ? v : first;
        }
        for (int n = 0; n < 3; ++n)
            mixer.start(mixer.take(SoundLayer::Player, 1), SoundLayer::Player, 1, 5000);
        mixer.start(mixer.take(SoundLayer::Voice, 1), SoundLayer::Voice, 1, 5000);
        mixer.start(mixer.take(SoundLayer::Voice, 1), SoundLayer::Voice, 1, 5000);
        mixer.start(mixer.take(SoundLayer::Ui, 1), SoundLayer::Ui, 1, 5000);
        for (int n = 0; n < 3; ++n)
            mixer.start(mixer.take(SoundLayer::Ambience, 1), SoundLayer::Ambience, 1, -1);
        check(mixer.count(SoundLayer::World) == 4, "(the world is back to its four)");
        const std::uint32_t ended = mixer.tick(150);
        int gone = 0;
        for (int v = 0; v < Mixer::kVoices; ++v)
            gone += (ended >> v) & 1;
        check(gone <= 1 && mixer.count(SoundLayer::Ambience) == 3, "a sound that has run its time is over; a loop goes on");
        mixer.tick(100000);
        check(mixer.count(SoundLayer::World) == 0 && mixer.count(SoundLayer::Ambience) == 3,
              "... however long: only stopping it stops it");
    }
    {
        Mixer mixer;
        mixer.volume[static_cast<int>(SoundLayer::Voice)] = 5;
        mixer.volume[static_cast<int>(SoundLayer::Ambience)] = 0;
        check(mixer.scaled(SoundLayer::World, 100) == 100 && mixer.scaled(SoundLayer::Voice, 100) == 50 &&
                  mixer.scaled(SoundLayer::Ambience, 100) == 0,
              "each layer is as loud as its own volume has it");
        SoundQueue queue;
        const fx at[3] = {0, 0, 0};
        queue.add(CommonSound::Pistol, nullptr);
        queue.add(CommonSound::Explode, at);
        queue.add(std::uint16_t(3), at, 100, 500, kSoundSentence);
        check(queue.events[0].layer == SoundLayer::Player && queue.events[1].layer == SoundLayer::World &&
                  queue.events[2].layer == SoundLayer::Voice,
              "what is heard knows its layer: the listener's own, the world's, a voice");
    }
    {
        // A door's move sound that does not loop is still the door's: its
        // stop cuts it as it cuts one that does (doors/doormove1.wav is five
        // seconds long; the game cuts it when the door stops).
        Mixer mixer;
        mixer.start(0, SoundLayer::World, 80, 5000, 7);  // played once, the door's
        mixer.start(1, SoundLayer::World, 80, -1, 7);    // going round, the door's
        mixer.start(2, SoundLayer::World, 80, 5000);     // played once, nobody's
        mixer.start(3, SoundLayer::World, 80, 5000, 8);  // another door's
        mixer.start(4, SoundLayer::Ambience, 80, -1, 7); // the loop of a place: record 7 too
        mixer.start(5, SoundLayer::Voice, 80, 3000, 7);  // a line said by speaker 7
        mixer.start(6, SoundLayer::Player, 80, 5000, 7); // heard at the listener, the door's
        check(mixer.movingOf(7) == ((1u << 0) | (1u << 1) | (1u << 6)),
              "a mover's sounds are its own, played once or going round, and nothing else");
        check(mixer.movingOf(-1) == 0, "what has no maker is nobody's to stop");
    }
    if (g_failures == 0)
        std::printf("test_mixer: ok\n");
    return g_failures == 0 ? 0 : 1;
}
