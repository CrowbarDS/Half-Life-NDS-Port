// A voice as it is sent (shared/HlVoice.hpp): what the hardware will play of a
// packet stays near what the microphone heard -- a tone, speech-like noise,
// a shout at the top of the range -- from one packet to the next, each
// packet whole by itself; and a packet with no head is refused. And what is
// heard: packets put back in order, held a moment, the late let fall.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "HlVoice.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    /// The signal to noise ratio, in dB, of what the hardware plays of
    /// `in` sent a packet at a time.
    double roundTrip(const std::vector<std::int16_t> &in)
    {
        VoiceEncoder enc{};
        double signal = 0, noise = 0;
        std::uint8_t packet[kVoiceAdpcmBytes];
        std::int16_t out[kVoiceSamples];
        for (std::size_t at = 0; at + kVoiceSamples <= in.size(); at += kVoiceSamples)
        {
            voiceEncode(enc, in.data() + at, kVoiceSamples, packet);
            if (!voiceDecode(packet, kVoiceSamples, out))
                return -1;
            for (int i = 0; i < kVoiceSamples; ++i)
            {
                const double s = in[at + static_cast<std::size_t>(i)], e = s - out[i];
                signal += s * s;
                noise += e * e;
            }
        }
        return noise == 0 ? 99 : 10 * std::log10(signal / noise);
    }

    /// A packet whose bytes say its number (to see which was played).
    void put(VoiceJitter &j, int number, std::uint32_t now, bool *kept = nullptr)
    {
        std::uint8_t packet[kVoiceAdpcmBytes] = {};
        packet[4] = static_cast<std::uint8_t>(number);
        const bool ok = voiceJitterPut(j, number, 8182, packet, now);
        if (kept != nullptr)
            *kept = ok;
    }
    /// The number of the packet played now; -1 for none.
    int take(VoiceJitter &j, std::uint32_t now, bool dry = true)
    {
        const VoiceJitter::Slot *slot = voiceJitterTake(j, now, dry);
        return slot == nullptr ? -1 : slot->bytes[4];
    }

    void testJitter()
    {
        VoiceJitter j{};
        check(take(j, 0) == -1, "nothing heard: nothing played");
        put(j, 10, 0);
        put(j, 11, 40);
        check(take(j, 50) == -1, "two held: not yet begun");
        put(j, 12, 80);
        check(take(j, 90) == 10 && take(j, 130) == 11 && take(j, 170) == 12, "three held: played in order");
        check(take(j, 205, false) == -1, "none held while one still plays: nothing to queue");
        put(j, 13, 208);
        check(take(j, 210, false) == 13, "... and one come in time is played at once, not held");
        check(take(j, 250) == -1, "none left and what played has run out: it stops");
        put(j, 14, 260);
        check(take(j, 270) == -1, "... and waits for enough again");

        // Out of order, the same one twice, one late.
        voiceJitterClear(j);
        put(j, 20, 0);
        put(j, 22, 10);
        bool kept = true;
        put(j, 22, 15, &kept);
        check(!kept, "the same packet twice: the second let fall");
        put(j, 21, 20);
        check(take(j, 30) == 20 && take(j, 70) == 21 && take(j, 110) == 22, "come out of order: played in order");
        put(j, 21, 120, &kept);
        check(!kept, "one come after its time: let fall");

        // One lost: passed over when its turn comes.
        voiceJitterClear(j);
        put(j, 30, 0);
        put(j, 32, 10);
        put(j, 33, 20);
        check(take(j, 30) == 30 && take(j, 70) == 32 && take(j, 110) == 33, "one lost: the next played in its place");

        // The last words: fewer than enough, played once nothing more comes.
        voiceJitterClear(j);
        put(j, 40, 0);
        check(take(j, kVoiceJitterWaitMs - 1) == -1 && take(j, kVoiceJitterWaitMs) == 40,
              "the last packet of what was said is played after a moment");

        // Round from 255 to 0, and a jump far ahead.
        voiceJitterClear(j);
        put(j, 254, 0);
        put(j, 255, 0);
        put(j, 0, 0);
        check(take(j, 10) == 254 && take(j, 10) == 255 && take(j, 10) == 0, "numbers go round from 255 to 0");
        put(j, 1, 20);
        put(j, 100, 30);
        put(j, 101, 30);
        check(take(j, 40) == 100 && take(j, 40) == 101, "a jump far ahead: what was before it let go");
    }
} // namespace

int main()
{
    const double kPi = 3.14159265358979;
    // A tone, 440 Hz at 8182 Hz, a quarter of the range.
    std::vector<std::int16_t> tone(kVoiceSamples * 25);
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] = static_cast<std::int16_t>(8000 * std::sin(2 * kPi * 440 * static_cast<double>(i) / 8182));
    const double toneSnr = roundTrip(tone);
    check(toneSnr > 20, "a tone comes through clearly (more than 20 dB)");

    // Something like speech: noise, filtered, rising and falling.
    std::vector<std::int16_t> speech(kVoiceSamples * 25);
    std::srand(5);
    double low = 0;
    for (std::size_t i = 0; i < speech.size(); ++i)
    {
        const double white = (std::rand() % 2001 - 1000) / 1000.0;
        low = low * 0.85 + white * 0.15;
        const double envelope = 0.5 + 0.5 * std::sin(2 * kPi * 3 * static_cast<double>(i) / 8182);
        speech[i] = static_cast<std::int16_t>(20000 * low * envelope);
    }
    const double speechSnr = roundTrip(speech);
    check(speechSnr > 12, "speech-like noise comes through (more than 12 dB)");

    // At the top of the range: clipped, not wrapped round.
    std::vector<std::int16_t> shout(kVoiceSamples * 4);
    for (std::size_t i = 0; i < shout.size(); ++i)
        shout[i] = static_cast<std::int16_t>((i / 40) % 2 ? 32767 : -32767);
    check(roundTrip(shout) > 6, "a shout at the top of the range stays a shout");

    // Each packet is whole by itself: the second decoded alone is what it
    // was decoded after the first.
    VoiceEncoder enc{};
    std::uint8_t first[kVoiceAdpcmBytes], second[kVoiceAdpcmBytes];
    voiceEncode(enc, tone.data(), kVoiceSamples, first);
    voiceEncode(enc, tone.data() + kVoiceSamples, kVoiceSamples, second);
    std::int16_t alone[kVoiceSamples];
    check(voiceDecode(second, kVoiceSamples, alone), "the second packet decodes by itself");
    double worst = 0;
    for (int i = 0; i < kVoiceSamples; ++i)
        worst = std::fmax(worst, std::fabs(alone[i] - tone[static_cast<std::size_t>(kVoiceSamples + i)]));
    check(worst < 2000, "... and as near what was heard as any other");

    // Silence costs nothing: it stays silence.
    std::vector<std::int16_t> quiet(kVoiceSamples, 0);
    VoiceEncoder fresh{};
    std::uint8_t packet[kVoiceAdpcmBytes];
    voiceEncode(fresh, quiet.data(), kVoiceSamples, packet);
    std::int16_t back[kVoiceSamples];
    voiceDecode(packet, kVoiceSamples, back);
    int loudest = 0;
    for (std::int16_t s : back)
        loudest = std::abs(s) > loudest ? std::abs(s) : loudest;
    check(loudest < 16, "silence is played as silence");

    // A head that is no head: refused.
    packet[2] = 100;
    check(!voiceDecode(packet, kVoiceSamples, back), "a packet whose step is out of range is refused");

    testJitter();

    std::printf("test_voice: tone %.1f dB, speech %.1f dB\n", toneSnr, speechSnr);
    if (g_failures == 0)
        std::printf("test_voice: ok\n");
    return g_failures == 0 ? 0 : 1;
}
