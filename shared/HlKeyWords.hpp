#pragma once

// What the map writes of a computer's keys ("PRESS YOUR USE KEY", "AIM WITH
// THE MOUSE": the Hazard Course's lessons, titles.txt) said in the DS's: the
// button the player's layout gives each, or the touch screen's.
//
// The game's lines are kept as they are but for those words; a line the
// change makes too wide is wrapped where it is shown (HlTopLayout.hpp).
//
// std-only and inline.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HlControls.hpp"

namespace hldata
{
    namespace detail
    {
        /// An action's word: its button ("SELECT", "UP"), the shift and a
        /// button under BUTTONS ONLY ("R+UP"), or its touch-screen button
        /// ("TOUCH JUMP").
        inline void keyWord(const ControlSettings &set, int action, char *out, std::size_t size)
        {
            static const char *const kTouch[kActionCount] = {
                "TOUCH FORWARD", "TOUCH BACK", "TOUCH LEFT", "TOUCH RIGHT", "TOUCH FIRE", "TOUCH ALT FIRE",
                "TOUCH JUMP",    "TOUCH DUCK", "TOUCH RELOAD", "TOUCH USE", "TOUCH NEXT", "TOUCH BEFORE",
                "TOUCH LIGHT",   "TOUCH WALK", "TOUCH LOOK UP", "TOUCH LOOK DOWN", "TOUCH TURN", "TOUCH TURN"};
            std::uint16_t keys[kActionCount];
            keysOf(set, keys);
            const char *shift = nullptr;
            std::uint32_t button = keys[action];
            // (BUTTONS ONLY reloads with a tap of its shift.)
            if (button == 0 && set.scheme == kControlsButtons && action == kActionReload)
                button = shiftButton(set.leftHanded);
            if (button == 0 && set.scheme == kControlsButtons)
            {
                std::uint16_t shifted[kActionCount];
                shiftedKeys(set.leftHanded, shifted);
                button = shifted[action];
                shift = button != 0 ? buttonName(shiftButton(set.leftHanded)) : nullptr;
            }
            std::size_t n = 0;
            auto put = [&](const char *word, bool upper)
            {
                for (const char *c = word; *c != '\0' && n + 1 < size; ++c)
                    out[n++] = upper && *c >= 'a' && *c <= 'z' ? static_cast<char>(*c - 'a' + 'A') : *c;
            };
            if (button == 0)
                put(kTouch[action], false);
            else
            {
                if (shift != nullptr)
                {
                    put(shift, true);
                    put("+", false);
                }
            {
                // (Of two, the pad's: CLASSIC's face buttons are a second
                // pad, and its arrows say it better.)
                static const std::uint32_t kOrder[] = {kButtonUp, kButtonDown, kButtonLeft, kButtonRight,
                                                       kButtonA,  kButtonB,    kButtonX,    kButtonY,
                                                       kButtonL,  kButtonR,    kButtonSelect};
                std::uint32_t one = button & (0u - button);
                for (const std::uint32_t b : kOrder)
                {
                    if (button & b)
                    {
                        one = b;
                        break;
                    }
                }
                put(buttonName(one), true);
            }
            }
            if (size > 0)
                out[n] = '\0';
        }
    } // namespace detail

    /// `text` with a computer's key words put in the DS's, into `out` (of
    /// `size` bytes, always ended). What it does not know is copied as it is.
    inline void topKeyWords(const char *text, const ControlSettings &set, char *out, std::size_t size)
    {
        struct Phrase
        {
            const char *pc;
            int first, second; ///< actions; second -1 for one
            const char *lead = ""; ///< what of `pc` stays, before the button
        };
        // (Longest first, so a pair is taken before its halves.)
        static const Phrase kPhrases[] = {
            {"THEN PRESS JUMP", kActionJump, -1, "THEN PRESS "},
            {"THE DUCK KEY", kActionDuck, -1},
            {"USE + BACKWARD KEYS", kActionUse, kActionBack},
            {"FORWARD + DUCK KEYS", kActionForward, kActionDuck},
            {"FORWARD + JUMP", kActionForward, kActionJump},
            {"YOUR FORWARD KEY", kActionForward, -1},
            {"YOUR USE KEY", kActionUse, -1},
            {"FLASHLIGHT KEY", kActionLamp, -1},
            {"BACKWARD KEY", kActionBack, -1},
            {"FORWARD KEY", kActionForward, -1},
            {"ATTACK1 KEY", kActionFire, -1},
            {"ATTACK2 KEY", kActionAltFire, -1},
            {"RELOAD KEY", kActionReload, -1},
            {"JUMP KEY", kActionJump, -1},
            {"DUCK KEY", kActionDuck, -1},
            {"USE KEY", kActionUse, -1},
            {"MOVERIGHT", kActionRight, -1},
            {"MOVELEFT", kActionLeft, -1},
        };
        // The stylus aims, unless BUTTONS ONLY, where the D-pad does.
        const char *aim = set.scheme == kControlsButtons ? "THE D-PAD" : "THE STYLUS";
        std::size_t n = 0;
        auto put = [&](const char *word)
        {
            for (const char *c = word; *c != '\0' && n + 1 < size; ++c)
                out[n++] = *c;
        };
        for (const char *at = text; *at != '\0';)
        {
            bool done = false;
            for (const Phrase &p : kPhrases)
            {
                const std::size_t length = std::strlen(p.pc);
                if (std::strncmp(at, p.pc, length) != 0)
                    continue;
                char word[24];
                put(p.lead);
                detail::keyWord(set, p.first, word, sizeof(word));
                put(word);
                if (p.second >= 0)
                {
                    put(" + ");
                    detail::keyWord(set, p.second, word, sizeof(word));
                    put(word);
                }
                at += length;
                done = true;
                break;
            }
            if (!done && std::strncmp(at, "THE MOUSE", 9) == 0)
            {
                put(aim);
                at += 9;
                done = true;
            }
            if (!done)
            {
                if (n + 1 < size)
                    out[n++] = *at;
                ++at;
            }
        }
        if (size > 0)
            out[n] = '\0';
    }
} // namespace hldata
