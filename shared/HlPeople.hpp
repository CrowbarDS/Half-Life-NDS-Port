#pragma once

// The people of the place: who the player USEs. What they do then -- and the
// rest of what they do -- is the game's own talkmonster (HlAiTalk.hpp,
// HlAiPeople.hpp): he follows, or says why not, or stays.
//
// Included at the end of HlMonsters.hpp. std-only and inline: the host
// tests run it.

namespace hldata
{
    // ActorState::ai, for a person (5 and up is dying and dead, as for
    // everything else): whether he follows the player, as saves have kept it.
    inline constexpr std::uint8_t kPersonIdle = 0;
    inline constexpr std::uint8_t kPersonFollow = 1;
    inline constexpr std::uint8_t kPersonFlee = 2; ///< (saves from before: taken as idle)

    /// USE, from `eye` along `forward` (Q12, unit): the scientist or guard
    /// the player would USE is USEd (FollowerUse). True if there was one.
    /// As PlayerUse picks it: one whose box comes within 64 of the player
    /// (PLAYER_SEARCH_RADIUS), the line from the eye to its middle -- less
    /// its own half size, each way (UTIL_ClampVectorToBox) -- within the
    /// narrow view (0.7), and of those the most nearly ahead.
    inline bool personUse(LogicWorld &w, PlayerState &player, const fx *eye, const fx *forward)
    {
        if (w.actors == nullptr)
            return false;
        ActorWorld &a = *w.actors;
        int best = -1;
        fx bestDot = fxFromDouble(0.7);
        for (int i = 0; i < a.count; ++i)
        {
            const ActorState &actor = a.states[i];
            if (!actor.active || actor.ai >= kAiDying ||
                (actor.kind != static_cast<std::uint8_t>(ActorKind::Scientist) &&
                 actor.kind != static_cast<std::uint8_t>(ActorKind::Guard)))
                continue;
            fx lo[3], hi[3];
            targetBox(actor, lo, hi);
            // (In the sphere: its box's nearest point to the player.)
            fx nearest[3];
            for (int k = 0; k < 3; ++k)
            {
                const fx p = player.origin[k];
                nearest[k] = (p < lo[k] ? lo[k] - p : (p > hi[k] ? p - hi[k] : 0));
            }
            if (!fxLengthAtMost(nearest, fxFromInt(64)))
                continue;
            fx los[3];
            for (int k = 0; k < 3; ++k)
            {
                const fx half = (hi[k] - lo[k]) / 2;
                const fx v = (lo[k] + hi[k]) / 2 - eye[k];
                los[k] = v > half ? v - half : (v < -half ? v + half : 0);
            }
            const fx length = fxLength(los);
            if (length <= 0)
                continue; // (the eye within its box: nothing to say which way)
            const fx dot = static_cast<fx>((static_cast<std::int64_t>(los[0]) * forward[0] +
                                            static_cast<std::int64_t>(los[1]) * forward[1] +
                                            static_cast<std::int64_t>(los[2]) * forward[2]) /
                                           length);
            if (dot > bestDot)
            {
                best = i;
                bestDot = dot;
            }
        }
        if (best < 0)
            return false;
        AiMonster *m = aiOf(a, best);
        const AiKind *kind = aiKindOf(a.states[best].kind);
        if (m == nullptr || kind == nullptr || a.ai == nullptr)
            return true;
        AiCtx c = aiCtx(w, player, best, *m, *kind);
        if (m->maxHealth == 0)
            aiInit(c);
        talk::followerUse(c);
        return true;
    }
} // namespace hldata
