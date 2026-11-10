// A voice as it is sent (shared/HlVoice.hpp): what the hardware will play of a
// packet stays near what the microphone heard -- a tone, speech-like noise,
// a shout at the top of the range -- from one packet to the next, each
// packet whole by itself; and a packet with no head is refused.

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

    std::printf("test_voice: tone %.1f dB, speech %.1f dB\n", toneSnr, speechSnr);
    if (g_failures == 0)
        std::printf("test_voice: ok\n");
    return g_failures == 0 ? 0 : 1;
}
