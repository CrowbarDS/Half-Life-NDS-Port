#pragma once

// Blue Shift's own (the family kFamilyShift: dll/family/, loaded with each
// Blue Shift map and with nothing else -- a DS, which plays Half-Life alone,
// never has it). What of it the core reaches is the hooks it sets as it comes
// (expansionHooks, HlLogic.hpp); the core names none of its code, and none of
// it is in the program.
//
// Its armour (item_armorvest, item_helmet) and its batteries' 20; its map
// rules (game_counter, game_counter_set, trigger_playerfreeze -- maprules.cpp
// and triggers.cpp, which Opposing Force has too: its family will set the
// same); its warp balls (env_warpball); its monster_generic that turns its
// head to whom it talks to (spawnflag 8). Rosenberg, a scientist of its own, is the core's:
// a bit of his (kPersonRosenberg) where the scientists' code asks.
//
// Included at the end of HlMonsters.hpp.

#include "HlGame.hpp"

namespace hldata::shift
{
    namespace detail
    {
        /// What it has of items: true if `item` is one (then `took`, whether
        /// it was taken). Armour is the suit's: without one, it is left lying.
        inline bool itemTake(PlayerState &player, std::uint8_t item, std::uint32_t &, const SkillData &, bool &took)
        {
            int amount;
            switch (static_cast<ItemKind>(item))
            {
            case ItemKind::ArmorVest: amount = 60; break;
            case ItemKind::Helmet: amount = 40; break;
            // (Its skill.cfg's one difference from Half-Life's: 20 at every
            // level. Only in a Blue Shift game: a build with every family
            // linked in -- the host's -- has this in Half-Life's too.)
            case ItemKind::Battery:
                if (activeGame != kGameBlueShift)
                    return false;
                amount = 20;
                break;
            default: return false;
            }
            Inventory &inv = player.inv;
            took = (inv.have & kHaveSuit) != 0 && inv.armor < 100;
            if (took)
                inv.armor = static_cast<std::int16_t>(inv.armor + amount > 100 ? 100 : inv.armor + amount);
            return true;
        }

        /// A strike from `from` at random (CLightning::RandomArea): the
        /// world within `reach` units, and not nearer than a tenth of it, in
        /// `out`. False if none was found in ten tries. (As an env_beam's
        /// that strikes at random: beamAim.)
        inline bool warpStrike(LogicWorld &w, const fx *from, int reach, fx *out)
        {
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                w.effectSeed = w.effectSeed * 1664525u + 1013904223u;
                fx sine, cosine, rise, flat;
                fxSinCos(static_cast<std::uint16_t>(w.effectSeed >> 8), sine, cosine);
                w.effectSeed = w.effectSeed * 1664525u + 1013904223u;
                fxSinCos(static_cast<std::uint16_t>((w.effectSeed >> 8) % 32768 - 16384), rise, flat);
                const fx to[3] = {from[0] + fxMulShort(cosine, flat) * reach, from[1] + fxMulShort(sine, flat) * reach,
                                  from[2] + rise * reach};
                const Shot shot = traceShot(*w.world, from, to);
                if (shot.fraction >= kFractionOne || shot.fraction < kFractionOne / 10)
                    continue;
                for (int k = 0; k < 3; ++k)
                    out[k] = from[k] + static_cast<fx>((static_cast<std::int64_t>(to[k] - from[k]) * shot.fraction) >>
                                                       kFractionShift);
                return true;
            }
            return false;
        }

        /// The green of a WarpBall's flare (77, 210, 130) and of its
        /// lightning (0, 255, 0).
        inline constexpr std::uint16_t kWarpFlare = 9 | 26 << 5 | 16 << 10;
        inline constexpr std::uint16_t kWarpBolt = 31 << 5;

        /// A WarpBall's blast: the game's 300 at its very middle, gone by
        /// 48 units out.
        inline void warpBallHurts(LogicWorld &w, PlayerState &player, int index)
        {
            LogicState &state = w.states[index];
            if (w.actors != nullptr && w.world != nullptr)
                radiusDamage(w, player, state.base, 300, 48, -1, kDmgShock);
            state.bits |= 1;
        }

        /// Used: where it is to be, and all of it at once there.
        inline void warpBallUse(LogicWorld &w, PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (state.running)
                return;
            if (!placeOfName(w, record.netName, state.base))
            {
                for (int k = 0; k < 3; ++k)
                    state.base[k] = record.move[k];
            }
            state.running = true;
            state.phase = 0;
            state.timer = 0;
            state.bits = 0;
            if (w.actors != nullptr)
            {
                Effect &flare = effectAdd(*w.actors, kEffectSprite, state.base, nullptr, -19, kWarpBallMs);
                flare.sprite = static_cast<std::uint16_t>(record.extra[0] & 0xFFFF);
                flare.colour = kWarpFlare;
                Effect &glow = effectAdd(*w.actors, kEffectSprite, state.base, nullptr, -16, kWarpBallMs);
                glow.sprite = static_cast<std::uint16_t>(record.extra[0] >> 16);
                glow.colour = kWarpFlare;
            }
            if (w.heard != nullptr && w.sounds != nullptr && w.sounds[index].move != kNoSound)
                w.heard->add(w.sounds[index].move, state.base, 127, kRadiusMedium);
            // (UTIL_ScreenShake: 4 units, felt within 1000.)
            const fx d[3] = {player.origin[0] - state.base[0], player.origin[1] - state.base[1],
                             player.origin[2] - state.base[2]};
            const int far = fxToInt(fxLength(d));
            if (far < 1000)
                shakeStart(w, fxFromInt(4) * (1000 - far) / 1000, 2000);
            if (record.extra[1] == 0)
                warpBallHurts(w, player, index);
        }

        /// Each tick it plays: a strike now and then, the blast if it was
        /// to wait, and out at its end.
        inline void warpBallTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (!state.running)
                return;
            state.phase += ms;
            // (Each strike half a second; the last well before the end.)
            if ((state.timer -= ms) <= 0 && state.phase < kWarpBallMs - 400 && w.actors != nullptr &&
                w.world != nullptr)
            {
                fx to[3];
                if (warpStrike(w, state.base, record.maxs[0], to))
                {
                    Effect &bolt = effectAdd(*w.actors, kEffectBeam, state.base, to, 3, 500);
                    bolt.colour = kWarpBolt;
                }
                state.timer = 150 + static_cast<std::int32_t>(hldata::detail::effectChance(w) % 250u);
            }
            if ((record.extra[2] & 2) && !(state.bits & 1) && state.phase >= static_cast<std::int32_t>(record.extra[1]))
                warpBallHurts(w, player, index);
            if (state.phase >= kWarpBallMs)
            {
                state.running = false;
                if (record.extra[2] & 1)
                    state.dead = true;
            }
        }


        inline void gameCounterReached(LogicWorld &w, PlayerState &player, int index, int depth);

        /// The map's rules (maprules.cpp's game_counter, game_counter_set;
        /// trigger_playerfreeze), used.
        inline void mapRuleUse(LogicWorld &w, PlayerState &player, int index, int useType, int depth)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            switch (static_cast<LogicKind>(record.kind))
            {
            case LogicKind::GameCounter:
            {
                if (!logicMasterAllows(w, index))
                    break;
                state.phase += useType == kUseOff ? -1 : (useType == kUseSet ? 0 : 1);
                gameCounterReached(w, player, index, depth);
                break;
            }

            case LogicKind::GameCounterSet:
                if (!logicMasterAllows(w, index))
                    break;
                // (Straight to the counts: a use carries no number here.)
                for (int i = 0; i < w.recordCount && record.target != 0; ++i)
                {
                    if (w.records[i].name == logicTargetOf(w, index) &&
                        w.records[i].kind == static_cast<std::uint8_t>(LogicKind::GameCounter) && !w.states[i].dead)
                    {
                        w.states[i].phase = static_cast<std::int32_t>(record.extra[0]) -
                                            static_cast<std::int32_t>(w.records[i].extra[1]);
                        gameCounterReached(w, player, i, depth + 1);
                    }
                }
                if (record.extra[2] & 1)
                    state.dead = true;
                break;

            case LogicKind::PlayerFreeze:
            {
                // (The player's, not its own: every one of them says the
                // same, in its `running`, which is saved with the map.)
                const bool now = !state.running;
                for (int i = 0; i < w.recordCount; ++i)
                {
                    if (w.records[i].kind == static_cast<std::uint8_t>(LogicKind::PlayerFreeze))
                        w.states[i].running = now;
                }
                break;
            }

            default:
                break;
            }
        }

        /// A game_counter at its limit: fires, and is done or starts again.
        inline void gameCounterReached(LogicWorld &w, PlayerState &player, int index, int depth)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (static_cast<std::int32_t>(record.extra[1]) + state.phase != static_cast<std::int32_t>(record.extra[0]))
                return;
            logicUseTargets(w, player, index, kUseToggle, depth);
            if (record.extra[2] & 1)
                state.dead = true;
            if (record.extra[2] & 2)
                state.phase = 0;
        }

        /// Its logic kinds used: a warp ball plays, unless it is playing,
        /// and fires its target; the map's rules (mapRuleUse).
        inline void logicUseHook(LogicWorld &w, PlayerState &player, int index, int useType, int depth)
        {
            if (w.records[index].kind != static_cast<std::uint8_t>(LogicKind::WarpBall))
            {
                mapRuleUse(w, player, index, useType, depth);
                return;
            }
            if (w.states[index].running)
                return;
            warpBallUse(w, player, index);
            logicUseTargets(w, player, index, kUseToggle, depth);
        }

        /// Blue Shift's (kActorHeadTurns): while it says a scripted line,
        /// its head to whom it says it to; then back (MonsterThink).
        inline void genericRunAI(AiCtx &c)
        {
            if (!(c.me.flags & kActorHeadTurns))
                return;
            c.m.capability |= kCapTurnHead;
            if (c.m.talk.target == kAiNone)
                return;
            if (c.ai.now > c.m.talk.stopTalk)
            {
                talk::headReset(c);
                c.m.talk.target = kAiNone;
            }
            else
            {
                talk::headTurnTo(c, c.m.talk.target);
            }
        }
    } // namespace detail

    /// The family's hooks set (or, gone, cleared).
    inline void familyRegister(bool on)
    {
        expansionHooks.itemTake = on ? &detail::itemTake : nullptr;
        expansionHooks.logicUse = on ? &detail::logicUseHook : nullptr;
        expansionHooks.logicTick = on ? &detail::warpBallTick : nullptr;
        expansionHooks.genericRunAI = on ? &detail::genericRunAI : nullptr;
    }
} // namespace hldata::shift
