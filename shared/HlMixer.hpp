#pragma once

// Who gets a voice. The console can play fourteen things at once (sixteen
// channels, less the music's and the one whose clock a DSi's sound capture
// keeps time by). Thirteen of them are voices; the fourteenth is kept back
// for a voice that is being faded out, so that one sound can give way to
// another without a click. What there is to play is of five kinds -- layers
// (HlSound.hpp) -- that are not equally worth hearing. These are the rules;
// what actually makes the noise is the caller's.
//
//  * Each layer has a SHARE of the voices that is its own: the player's own
//    sounds 3, the world's 4, the voice 2 (two people may speak at once),
//    the loops of a place 3, the menu 1. A layer is never left with fewer
//    than its share because another wanted more.
//  * While voices are free, any layer may have them, over its share or not.
//  * With none free, a layer still under its share takes one back from a
//    layer that is over its own: the least of them first (ambience, then the
//    world, the player, the voice, the menu), and of that layer's the
//    quietest, the oldest among equals.
//  * A layer at its share (or over) has only its own to take: the quietest,
//    if the new sound is at least as loud; otherwise the new sound is not
//    heard. The voice and the menu always take their own: a new line cuts in
//    on the last (which of two lines gives way is the speaker's business:
//    whoever plays them stops that one first).
//
// Each layer has a volume of its own, 0 to 10, set by the player.
//
// std-only and inline: the host tests run it.

#include "HlSound.hpp"

namespace hldata
{
    struct MixVoice
    {
        bool active = false;
        SoundLayer layer = SoundLayer::World;
        std::uint8_t gain = 0;    ///< how loud it is where the listener is, 0..127: what is weighed
        std::int32_t leftMs = 0;  ///< until it is over; negative: it goes round until it is stopped
        std::uint32_t born = 0;
        std::int32_t tag = -1;    ///< the caller's (which loop of the map it is)
    };

    /// Dialogue focus: while someone speaks, the music and the ambience
    /// are let down -- never the effects -- so that what is said is heard
    /// over them, and brought back up once it is over. `kFocusFloor`: how
    /// far down, of 256, for each choice (off; light, 60 %; strong, 35 %).
    inline constexpr std::uint8_t kFocusOff = 0, kFocusLight = 1, kFocusStrong = 2, kFocusChoices = 3;
    inline constexpr int kFocusFloor[kFocusChoices] = {256, 154, 90};
    /// How long the whole way down or up takes; how long after the last
    /// word it stays down (a pause between two lines does not bring it up).
    inline constexpr int kFocusRampMs = 250;
    inline constexpr int kFocusHoldMs = 300;
    /// How far each choice moves in a millisecond, in 256ths of its 256:
    /// its whole way in kFocusRampMs (off: back up from wherever it was, at
    /// the strong one's pace).
    inline constexpr int kFocusStep[kFocusChoices] = {((256 - 90) << 8) / kFocusRampMs,
                                                      ((256 - 154) << 8) / kFocusRampMs,
                                                      ((256 - 90) << 8) / kFocusRampMs};

    struct DialogueFocus
    {
        int level = 256 << 8; ///< where it is, of 256, in 256ths
        int holdMs = 0;

        /// `ms` on, with someone speaking or not, for the choice `mode`
        /// (kFocus*): how loud the music and the ambience are now, of 256.
        int tick(int ms, bool speaking, std::uint8_t mode)
        {
            if (mode >= kFocusChoices)
                mode = kFocusOff;
            const int floor = kFocusFloor[mode];
            if (speaking)
                holdMs = kFocusHoldMs;
            else if (holdMs > 0)
                holdMs -= ms;
            const int want = (holdMs > 0 ? floor : 256) << 8;
            const int step = ms * kFocusStep[mode];
            if (level > want)
                level = level - step > want ? level - step : want;
            else if (level < want)
                level = level + step < want ? level + step : want;
            return level >> 8;
        }
    };

    struct Mixer
    {
        static constexpr int kVoices = 13;
        MixVoice voices[kVoices];
        std::uint32_t clock = 0;
        std::uint8_t volume[static_cast<int>(SoundLayer::Count)] = {10, 10, 10, 10, 10};
        /// How far the ambience is let down while someone speaks, of 256
        /// (DialogueFocus): 256, not at all.
        int focus = 256;

        static constexpr int share(SoundLayer layer)
        {
            constexpr int kShares[] = {3, 4, 2, 3, 1};
            return kShares[static_cast<int>(layer)];
        }
        /// Who gives way first: the lowest.
        static constexpr int rank(SoundLayer layer)
        {
            constexpr int kRanks[] = {2, 1, 3, 0, 4};
            return kRanks[static_cast<int>(layer)];
        }

        int count(SoundLayer layer) const
        {
            int n = 0;
            for (const MixVoice &voice : voices)
                n += voice.active && voice.layer == layer;
            return n;
        }

        /// `gain` (0..127) as loud as the layer's volume has it (and the
        /// ambience, as far down as the focus has it).
        int scaled(SoundLayer layer, int gain) const
        {
            const int loud = (gain * volume[static_cast<int>(layer)]) / 10;
            return layer == SoundLayer::Ambience ? (loud * focus) >> 8 : loud;
        }

        /// The voice a new sound of `layer`, `gain` loud, is to have: a free
        /// one, or one that is to give way to it (still marked active: the
        /// caller silences it first). -1: it is not to be heard.
        int take(SoundLayer layer, int gain) const
        {
            for (int v = 0; v < kVoices; ++v)
            {
                if (!voices[v].active)
                    return v;
            }
            auto least = [&](SoundLayer of) -> int
            {
                int best = -1;
                for (int v = 0; v < kVoices; ++v)
                {
                    const MixVoice &voice = voices[v];
                    if (voice.layer != of)
                        continue;
                    if (best < 0 || voice.gain < voices[best].gain ||
                        (voice.gain == voices[best].gain && voice.born < voices[best].born))
                        best = v;
                }
                return best;
            };
            if (count(layer) < share(layer))
            {
                // One back from whoever has more than theirs.
                int from = -1;
                for (int l = 0; l < static_cast<int>(SoundLayer::Count); ++l)
                {
                    const auto other = static_cast<SoundLayer>(l);
                    if (count(other) > share(other) &&
                        (from < 0 || rank(other) < rank(static_cast<SoundLayer>(from))))
                        from = l;
                }
                if (from >= 0)
                    return least(static_cast<SoundLayer>(from));
            }
            const int own = least(layer);
            if (own < 0)
                return -1;
            const bool always = layer == SoundLayer::Voice || layer == SoundLayer::Ui;
            return always || voices[own].gain <= gain ? own : -1;
        }

        void start(int v, SoundLayer layer, int gain, std::int32_t ms, std::int32_t tag = -1)
        {
            MixVoice &voice = voices[v];
            voice.active = true;
            voice.layer = layer;
            voice.gain = static_cast<std::uint8_t>(gain < 0 ? 0 : (gain > 127 ? 127 : gain));
            voice.leftMs = ms;
            voice.born = ++clock;
            voice.tag = tag;
        }
        void stop(int v) { voices[v] = MixVoice(); }

        /// The voices with what `tag` makes as it moves -- going round, or
        /// played once (a door's move sound that does not loop, which the
        /// game cuts when the door stops, and puts in the place of the last
        /// when it moves again): a mask, one bit a voice. Not the loops of a
        /// place nor what is spoken, which have tags of their own kind.
        std::uint32_t movingOf(std::int32_t tag) const
        {
            std::uint32_t mask = 0;
            for (int v = 0; v < kVoices; ++v)
            {
                const MixVoice &voice = voices[v];
                if (voice.active && voice.tag == tag && tag >= 0 && voice.layer != SoundLayer::Ambience &&
                    voice.layer != SoundLayer::Voice)
                    mask |= 1u << v;
            }
            return mask;
        }

        /// The voice that matters least of all that are playing: of the
        /// layer that gives way first, the quietest, the oldest among
        /// equals; the menu's never. For when the hardware itself has no
        /// room (something of its own is on a channel): -1 if there is
        /// nothing to give up.
        int weakest() const
        {
            int best = -1;
            for (int v = 0; v < kVoices; ++v)
            {
                const MixVoice &voice = voices[v];
                if (!voice.active || voice.layer == SoundLayer::Ui)
                    continue;
                if (best < 0 || rank(voice.layer) < rank(voices[best].layer) ||
                    (voice.layer == voices[best].layer &&
                     (voice.gain < voices[best].gain ||
                      (voice.gain == voices[best].gain && voice.born < voices[best].born))))
                    best = v;
            }
            return best;
        }

        /// `ms` more: what has run its time is over. Returns a bit for each
        /// voice that ended (the caller lets go of it). `frozen`: the game
        /// stands still, and only the menu's sounds run on.
        std::uint32_t tick(int ms, bool frozen = false)
        {
            std::uint32_t ended = 0;
            for (int v = 0; v < kVoices; ++v)
            {
                MixVoice &voice = voices[v];
                if (!voice.active || voice.leftMs < 0 || (frozen && voice.layer != SoundLayer::Ui))
                    continue;
                voice.leftMs -= ms;
                if (voice.leftMs <= 0)
                {
                    voice = MixVoice();
                    ended |= 1u << v;
                }
            }
            return ended;
        }
    };
} // namespace hldata
