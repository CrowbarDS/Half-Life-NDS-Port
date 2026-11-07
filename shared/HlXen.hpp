#pragma once

// The plants of Xen (ActorKind::Xen; which, XenPlant): what the hair and the
// fungi do is sway (HlActors.hpp: xenStart, actorAdvance, their boxes); the
// light and the tree do more, here.
//
// A light (xen_plantlight) is a glow on a stalk. The player coming within
// its reach makes it draw in and go dark -- what it targets (the light it
// casts, a styled light_spot) is switched off with it -- and five seconds
// after the player last came near, it comes up again and lights it. Its
// stage is ActorState::attack, the time to come up again `cooldownMs`, how
// bright its glow is `reach` (ModelDraw draws it at the model's second
// attachment, in the colour the map gave it).
//
// A tree (xen_tree) strikes at whatever comes into the space in front of it,
// or against it: its attack clip, and at the clip's blow (its model's event 1)
// everything in that space takes 25, and is shoved away from it.
//
// Reimplemented from how the game's plants behave; nothing of its code.

#include "HlMonsters.hpp"

namespace hldata
{
    static_assert(kXenIdleActivity == kActIdle, "HlActors.hpp's idle is the AI's");

    inline constexpr int kXenHideMs = 5000; ///< a light stays drawn in this long after the player
    inline constexpr int kXenTreeDamage = 25;

    namespace detail
    {
        enum : std::uint8_t
        {
            kXenUp = 0,    ///< a light shining, a tree still
            kXenDown,      ///< a light drawing in; a tree striking
            kXenHidden,    ///< a light drawn in, dark
            kXenRising,    ///< ... coming up again
        };

        /// Play the clip for `activity`, once through (`once`) or round and
        /// round, at a pace between `lo` and `hi` (Q8) of its own.
        inline void xenPlay(LogicWorld &w, ActorState &actor, std::uint8_t activity, bool once, int lo, int hi)
        {
            const ModelView *model = actorModel(*w.actors, actor);
            if (model == nullptr)
                return;
            const int clip = clipOfActivity(*model, activity);
            if (clip < 0)
                return;
            actor.clip = static_cast<std::int16_t>(clip);
            actor.frame = 0;
            actor.once = once;
            actor.finished = false;
            actor.frozen = false;
            actor.speed = static_cast<fx>(lo + static_cast<int>(logicChance(w) % static_cast<std::uint32_t>(hi - lo + 1)));
        }

        /// The space in front of a tree that it strikes at: 48 wide, as tall
        /// as a man and a half, its middle 64 units ahead.
        inline void xenTreeSpace(const ActorState &tree, fx *mins, fx *maxs)
        {
            fx sine, cosine;
            fxSinCos(tree.yaw, sine, cosine);
            const fx ahead[2] = {tree.pos[0] + fxMul(cosine, fxFromInt(64)), tree.pos[1] + fxMul(sine, fxFromInt(64))};
            for (int k = 0; k < 2; ++k)
            {
                mins[k] = ahead[k] - fxFromInt(24);
                maxs[k] = ahead[k] + fxFromInt(24);
            }
            mins[2] = tree.pos[2];
            maxs[2] = tree.pos[2] + fxFromInt(128);
        }

        /// Whether actor `other` stands in the box (its feet, a body's
        /// width round them).
        inline bool xenActorIn(const ActorState &other, const fx *mins, const fx *maxs)
        {
            const fx half = fxFromInt(16);
            return other.pos[0] + half >= mins[0] && other.pos[0] - half <= maxs[0] &&
                   other.pos[1] + half >= mins[1] && other.pos[1] - half <= maxs[1] &&
                   other.pos[2] + fxFromInt(72) >= mins[2] && other.pos[2] <= maxs[2];
        }

        inline void xenLight(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            ActorState &light = w.actors->states[index];
            const SolidBox &reach = w.actors->boxes[index];
            if (player.health > 0 && playerTouchesBox(player, reach.mins, reach.maxs, 0))
            {
                light.cooldownMs = kXenHideMs;
                if (light.attack == kXenUp || light.attack == kXenRising)
                {
                    light.attack = kXenDown;
                    xenPlay(w, light, kActCrouch, true, 256, 256);
                }
            }
            switch (light.attack)
            {
            case kXenDown:
                if (light.finished)
                {
                    light.attack = kXenHidden;
                    xenPlay(w, light, kActCrouchIdle, false, 256, 256);
                    light.reach = 0;
                    logicFire(w, player, light.fires, -1, kUseOff, 0);
                }
                break;
            case kXenHidden:
                if ((light.cooldownMs -= ms) <= 0)
                {
                    light.attack = kXenRising;
                    xenPlay(w, light, kActStand, true, 256, 256);
                    light.reach = 255;
                    logicFire(w, player, light.fires, -1, kUseOn, 0);
                }
                break;
            case kXenRising:
                if (light.finished)
                {
                    light.attack = kXenUp;
                    xenPlay(w, light, kActIdle, false, 179, 358);
                }
                break;
            default:
                break;
            }
        }

        inline void xenTree(LogicWorld &w, PlayerState &player, int index)
        {
            ActorState &tree = w.actors->states[index];
            if (tree.attack == kXenDown)
            {
                if (tree.finished)
                {
                    tree.attack = kXenUp;
                    xenPlay(w, tree, kActIdle, false, 154, 358);
                }
                return;
            }
            // Something in front of it, or against it: the player, or
            // anything that can be hurt (not big momma, whom the game's
            // trees leave be).
            fx mins[3], maxs[3];
            xenTreeSpace(tree, mins, maxs);
            const SolidBox &body = w.actors->boxes[index];
            bool strike = player.health > 0 && (playerTouchesBox(player, mins, maxs, 0) ||
                                                playerTouchesBox(player, body.mins, body.maxs, fxFromInt(2)));
            for (int i = 0; i < w.actors->count && !strike; ++i)
            {
                const ActorState &other = w.actors->states[i];
                strike = i != index && other.active && other.ai < kAiDying && canBeHurt(other.kind) &&
                         other.kind != static_cast<std::uint8_t>(ActorKind::BigMomma) && xenActorIn(other, mins, maxs);
            }
            if (!strike)
                return;
            tree.attack = kXenDown;
            xenPlay(w, tree, kActMeleeAttack1, true, 256, 358);
            if (w.heard != nullptr)
                w.heard->add(CommonSound::ClawMiss, tree.pos, 100, kRadiusLarge);
        }
    } // namespace detail

    /// A frame of the plants that do something (monstersTick) -- and of the
    /// air tanks, which fill again (HlMonsters.hpp's itemPickUp).
    inline void xenTick(LogicWorld &w, PlayerState &player, int ms)
    {
        if (w.actors == nullptr || w.actors->boxes == nullptr)
            return;
        for (int i = 0; i < w.actors->count; ++i)
        {
            ActorState &actor = w.actors->states[i];
            if (actor.active && actor.kind == static_cast<std::uint8_t>(ActorKind::Item) &&
                actor.item == static_cast<std::uint8_t>(ItemKind::Airtank) && actor.cooldownMs > 0)
                actor.cooldownMs -= ms;
            if (!actor.active || actor.kind != static_cast<std::uint8_t>(ActorKind::Xen))
                continue;
            if (actor.item == static_cast<std::uint8_t>(XenPlant::Light))
                detail::xenLight(w, player, i, ms);
            else if (actor.item == static_cast<std::uint8_t>(XenPlant::Tree))
                detail::xenTree(w, player, i);
        }
    }

    namespace detail
    {
        /// A tree's blow (its model's event 1, as its attack clip passes it):
        /// all in front of it take the blow and are shoved off.
        inline void xenEvent(LogicWorld &w, PlayerState &player, int index, std::uint32_t event)
        {
            ActorState &tree = w.actors->states[index];
            if (event != 1 || tree.item != static_cast<std::uint8_t>(XenPlant::Tree))
                return;
            fx mins[3], maxs[3];
            xenTreeSpace(tree, mins, maxs);
            fx sine, cosine;
            fxSinCos(tree.yaw, sine, cosine);
            const fx shove[2] = {fxMul(cosine, fxFromInt(100)), fxMul(sine, fxFromInt(100))};
            bool hit = false;
            if (player.health > 0 && playerTouchesBox(player, mins, maxs, 0))
            {
                hit = true;
                logicHurt(player, kXenTreeDamage, 0, kDmgCrush | kDmgSlash);
                player.velocity[0] += shove[0];
                player.velocity[1] += shove[1];
            }
            for (int i = 0; i < w.actors->count; ++i)
            {
                ActorState &other = w.actors->states[i];
                if (i == index || !other.active || other.ai >= kAiDying || !canBeHurt(other.kind) ||
                    !xenActorIn(other, mins, maxs))
                    continue;
                hit = true;
                monsterHurt(w, i, kXenTreeDamage, false);
            }
            if (hit && w.heard != nullptr)
                w.heard->add(CommonSound::ClawStrike, tree.pos, 110, kRadiusLarge);
        }
    } // namespace detail
} // namespace hldata
