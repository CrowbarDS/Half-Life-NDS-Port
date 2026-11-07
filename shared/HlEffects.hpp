#pragma once

// What a map shows and shakes: its sprites and glows, beams and lasers,
// explosions, sparks, things thrown out, fades -- the env_ entities the
// converter cooks to logic records of the kinds Sprite to Fade (HlLevel.hpp).
//
// Like the rest of the game's rules this is the same code on the host and on
// the console: what is on, what frame it is at, whom a laser burns. Drawing
// is the console's (source/render/EffectDraw.arm.cpp), which reads the same
// records and states.
//
// Included at the end of HlMonsters.hpp: an explosion hurts.

namespace hldata
{
    /// The view shakes, `amplitude` units (Q12) at its worst, for `ms`.
    /// (A harder shake under way is left to finish.)
    inline void shakeStart(LogicWorld &w, fx amplitude, int ms)
    {
        if (w.shakeMs > 0 && w.shakeAmplitude > amplitude)
            return;
        w.shakeAmplitude = amplitude;
        w.shakeMs = w.shakeWholeMs = ms;
    }

    /// An explosion of `size` (the game's magnitude) at `at`: seen, heard,
    /// felt, and -- with `hurts` -- hurting whatever is near.
    inline void explodeAt(LogicWorld &w, PlayerState &player, const fx *at, int size, bool fireball, bool sound,
                          bool hurts)
    {
        if (w.actors != nullptr && fireball)
            effectAdd(*w.actors, kEffectBurst, at, nullptr, size < 48 ? 48 : (size > 160 ? 160 : size), 450);
        if (w.actors != nullptr && w.world != nullptr && fireball)
            markBlast(*w.actors, *w.world, at, size / 2);
        if (sound && w.heard != nullptr)
            w.heard->add(CommonSound::Explode, at, 127, 2000);
        if (w.actors != nullptr && (sound || hurts))
        {
            for (int i = 0; i < 3; ++i)
                w.actors->noise[i] = at[i];
            w.actors->noiseMs = 400;
            // (Heard by the monsters for three seconds: CGrenade::Explode.)
            aiSound(*w.actors, kSoundCombat, at, kNormalExplosionVolume, 3000);
        }
        if (hurts && w.actors != nullptr)
            radiusDamage(w, player, at, size, size * 5 / 2, -1);
        const fx d[3] = {player.origin[0] - at[0], player.origin[1] - at[1], player.origin[2] - at[2]};
        const int far = fxToInt(fxLength(d));
        if (far < 600)
            shakeStart(w, fxFromInt(size > 200 ? 8 : size / 25 + 1) * (600 - far) / 600, 500);
    }

    /// The first of record `index`'s pictures and how solid and what colour
    /// it is drawn (its extra[2]).
    inline std::uint16_t effectColour(const LogicRecord &record)
    {
        return static_cast<std::uint16_t>(record.extra[2] & 0x7FFF);
    }
    inline int effectAlpha(const LogicRecord &record)
    {
        return static_cast<int>((record.extra[2] >> 16) & 31);
    }

    /// Whether a Sprite or Beam record is to be seen now.
    inline bool effectSeen(const LogicRecord &record, const LogicState &state)
    {
        if (state.dead)
            return false;
        if (record.kind == static_cast<std::uint8_t>(LogicKind::Sprite))
            return state.running;
        if (record.kind == static_cast<std::uint8_t>(LogicKind::Beam))
            return (record.flags & kLogicToggle) ? state.running : state.bits != 0;
        return false;
    }

    /// Where the thing called `name` is now: the middle of an actor of that
    /// name, or of a brush that a record of that name moves. False if
    /// nothing of that name is about.
    inline bool placeOfName(const LogicWorld &w, std::uint16_t name, fx *at)
    {
        if (name == 0)
            return false;
        for (int i = 0; w.actors != nullptr && i < w.actors->count; ++i)
        {
            const ActorState &actor = w.actors->states[i];
            if (!actor.active || actor.name != name)
                continue;
            fx mins[3], maxs[3];
            if (fights(actor.kind))
                targetBox(actor, mins, maxs);
            else
                mins[2] = actor.pos[2], maxs[2] = actor.pos[2] + fxFromInt(48);
            at[0] = actor.pos[0];
            at[1] = actor.pos[1];
            at[2] = (mins[2] + maxs[2]) / 2;
            return true;
        }
        for (int i = 0; i < w.recordCount; ++i)
        {
            if (w.records[i].name != name || w.records[i].brush == kNoBrush || w.movers == nullptr)
                continue;
            const MoverHull &hull = w.movers[w.records[i].brush];
            for (int k = 0; k < 3; ++k)
                at[k] = (hull.mins[k] + hull.maxs[k]) / 2;
            return true;
        }
        return false;
    }

    namespace detail
    {
        inline std::uint32_t effectChance(LogicWorld &w)
        {
            w.effectSeed = w.effectSeed * 1664525u + 1013904223u;
            return w.effectSeed >> 8;
        }

        /// Where a Beam record's two ends are aimed: fixed, following
        /// something, or (its end) wherever its present strike was sent.
        inline void beamAimedEnds(const LogicWorld &w, int index, fx *from, fx *to)
        {
            const LogicRecord &record = w.records[index];
            const LogicState &state = w.states[index];
            if (!placeOfName(w, record.target, from))
            {
                for (int k = 0; k < 3; ++k)
                    from[k] = record.move[k];
            }
            if (placeOfName(w, record.netName, to))
                return;
            const bool picked = (record.maxs[1] & kBeamRandom) || record.auxCount > 1;
            for (int k = 0; k < 3; ++k)
                to[k] = picked ? state.travel[k] : fxFromInt(record.mins[k]);
        }
    } // namespace detail

    /// The two ends of a Beam record as they are now: as aimed, or -- one
    /// that hurts -- its end on the first thing in its way (the state's
    /// `base`, while `hurt` is not 0).
    inline void beamEnds(const LogicWorld &w, int index, fx *from, fx *to)
    {
        detail::beamAimedEnds(w, index, from, to);
        const LogicState &state = w.states[index];
        if (state.hurt != 0)
        {
            for (int k = 0; k < 3; ++k)
                to[k] = state.base[k];
        }
    }

    /// A beam that strikes at random, or picks its end from several, does
    /// so now: where this strike goes is kept in its state. False if it
    /// found nowhere to strike (the game's rule: the world within its
    /// reach, and not nearer than a tenth of it).
    inline bool beamAim(LogicWorld &w, int index)
    {
        const LogicRecord &record = w.records[index];
        LogicState &state = w.states[index];
        if (record.auxCount > 1 && w.aux != nullptr)
        {
            const LogicAux &place =
                w.aux[record.firstAux + w.effectSeed / 7 % record.auxCount];
            w.effectSeed = w.effectSeed * 1664525u + 1013904223u;
            state.travel[0] = fxFromInt(static_cast<std::int16_t>(place.target));
            state.travel[1] = fxFromInt(static_cast<std::int16_t>(place.pad));
            state.travel[2] = fxFromInt(static_cast<std::int32_t>(place.delayMs));
            return true;
        }
        if (!(record.maxs[1] & kBeamRandom))
            return true;
        fx from[3];
        if (!placeOfName(w, record.target, from))
        {
            for (int k = 0; k < 3; ++k)
                from[k] = record.move[k];
        }
        const int reach = record.maxs[0];
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            // A direction: any way round, and up or down by as much as not.
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
                state.travel[k] = from[k] + static_cast<fx>((static_cast<std::int64_t>(to[k] - from[k]) *
                                                             shot.fraction) >> kFractionShift);
            return true;
        }
        return false;
    }

    /// One of the effect entities is used (logicUse sends them here).
    inline void effectUse(LogicWorld &w, PlayerState &player, int index, int useType)
    {
        const LogicRecord &record = w.records[index];
        LogicState &state = w.states[index];
        const bool on = useType == kUseToggle ? !state.running : useType != kUseOff;
        switch (static_cast<LogicKind>(record.kind))
        {
        case LogicKind::Sprite:
            // (A cycler_sprite is always there: using it stops its frames,
            // or starts them again.)
            if (record.extra[3] & 4)
            {
                state.bits = useType == kUseToggle ? state.bits ^ 1u : (useType == kUseOff ? 1u : 0u);
                break;
            }
            state.running = on;
            state.phase = 0;
            break;
        case LogicKind::Beam:
            if (record.flags & kLogicToggle)
            {
                // (One with no end of its own finds one as it comes on.)
                state.running = on && beamAim(w, index);
                state.move = MoveState::AtTop;
            }
            else if (useType != kUseOff)
            {
                // It strikes, now -- and again, as the game's does: each
                // strike lasts its time, and the next comes that long after
                // plus the time to strike again. Used once more, it stops.
                state.running = on;
                state.bits = on && beamAim(w, index) ? record.extra[3] & 0xFFFF : 0;
                state.phase = static_cast<std::int32_t>((record.extra[3] & 0xFFFF) + (record.extra[3] >> 16));
            }
            else
            {
                state.running = false;
            }
            break;
        case LogicKind::Explosion:
            explodeAt(w, player, record.move, static_cast<int>(record.extra[0]), !(record.extra[1] & 4),
                      !(record.extra[1] & 64), !(record.extra[1] & 1));
            if (!(record.extra[1] & 2))
                state.dead = true;
            break;
        case LogicKind::Shake:
        {
            const fx d[3] = {player.origin[0] - record.move[0], player.origin[1] - record.move[1],
                             player.origin[2] - record.move[2]};
            if (record.extra[2] == 0 || fxLength(d) < fxFromInt(static_cast<int>(record.extra[2])))
                shakeStart(w, static_cast<fx>(record.extra[0]) << 4, static_cast<int>(record.extra[1]));
            break;
        }
        case LogicKind::Spark:
            // Only one that is switched (kLogicToggle) heeds being used, and
            // every use turns it the other way, whatever it is told. The
            // first spark comes a moment later, as the next would.
            if (!(record.flags & kLogicToggle))
                break;
            state.running = !state.running;
            state.phase = 100 + static_cast<std::int32_t>(detail::effectChance(w) %
                                                         static_cast<std::uint32_t>(record.waitMs > 100 ? record.waitMs : 100));
            break;
        case LogicKind::Shooter:
            state.running = true;
            state.bits = record.extra[0];
            state.phase = 0;
            break;
        case LogicKind::Render:
            // How solid what it names is drawn from now on: not at all, a
            // part, or as it was made.
            for (int i = 0; i < w.recordCount; ++i)
            {
                if (w.records[i].name != record.target || record.target == 0 ||
                    w.records[i].brush == kNoBrush || w.brushStates == nullptr)
                    continue;
                const std::uint32_t amount = record.extra[0];
                w.brushStates[w.records[i].brush].look =
                    amount == 0 ? kBrushUnseen
                                : (amount >= 250 ? 0 : static_cast<std::uint8_t>(1 + amount * 30 / 255));
            }
            break;
        case LogicKind::Fade:
            // extra[3]: bit 0 Fade From; with bit 31, the colour (RGB15) in
            // bits 8-23 (a map cooked before that is black).
            w.fadeFrom = (record.extra[3] & 1) != 0;
            // (Bit 1: held black first -- a map that begins in the dark.)
            w.fadeMs = w.fadeFrom && (record.extra[3] & 2) ? -static_cast<std::int32_t>(record.extra[1]) : 0;
            w.fadeWholeMs = static_cast<std::int32_t>(record.extra[0]);
            w.fadeHoldMs = static_cast<std::int32_t>(record.extra[1]);
            w.fadeDark = static_cast<std::uint8_t>(record.extra[2] > 255 ? 255 : record.extra[2]);
            w.fadeColour.value =
                (record.extra[3] & 0x80000000u) ? static_cast<std::uint16_t>((record.extra[3] >> 8) & 0x7FFF) : 0;
            w.fadeLevel = w.fadeFrom ? w.fadeDark : 0;
            w.fading = true;
            break;
        default:
            break;
        }
    }

    namespace detail
    {
        /// A beam that hurts, for `ms` more: it stops on the first thing in
        /// its way -- the player, someone, a brush, the world -- and burns
        /// that, so much a second (CLaser::StrikeThink, CBeam::BeamDamage):
        /// what breaks breaks, and what it fires goes off. Sparks where it
        /// starts and where it ends, if it was made to throw them.
        inline void beamBurn(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            state.hurt = 0;
            if (!effectSeen(record, state) || w.world == nullptr)
                return;
            fx from[3], to[3];
            beamAimedEnds(w, index, from, to);
            const Shot shot = traceShot(*w.world, from, to);
            // (One that starts inside a wall -- set into it -- is not stopped
            // by that wall.)
            std::int64_t nearest = shot.fraction > 0 ? shot.fraction : kFractionOne;
            // (What it follows is where it ends, not in its way.)
            int mover = shot.fraction > 0 ? shot.mover : -1;
            for (int r = 0; mover >= 0 && record.netName != 0 && r < w.recordCount; ++r)
            {
                if (w.records[r].name == record.netName && w.records[r].brush == static_cast<std::uint16_t>(mover))
                {
                    nearest = kFractionOne;
                    mover = -1;
                }
            }
            int struck = -1;
            const ActorWorld *a = w.actors;
            for (int n = 0; a != nullptr && n < a->count; ++n)
            {
                const ActorState &actor = a->states[n];
                if (!actor.active || !canBeHurt(actor.kind) || actor.ai >= kAiDying ||
                    (actor.kind == static_cast<std::uint8_t>(ActorKind::Generic) && !(actor.flags & kActorSolid)))
                    continue;
                fx mins[3], maxs[3];
                targetBox(actor, mins, maxs);
                std::int64_t enter = 0;
                if (lineEntersBox(from, to, mins, maxs, enter) && enter < nearest)
                {
                    nearest = enter;
                    struck = n;
                }
            }
            bool atPlayer = false;
            if (player.health > 0)
            {
                const fx tall = player.ducked ? kCrouchedFeet : kStandingFeet;
                const fx mins[3] = {player.origin[0] - fxFromInt(16), player.origin[1] - fxFromInt(16),
                                    player.origin[2] - tall};
                const fx maxs[3] = {player.origin[0] + fxFromInt(16), player.origin[1] + fxFromInt(16),
                                    player.origin[2] + tall};
                std::int64_t enter = 0;
                if (lineEntersBox(from, to, mins, maxs, enter) && enter < nearest)
                {
                    nearest = enter;
                    atPlayer = true;
                    struck = -1;
                }
            }
            const bool onBrush = !atPlayer && struck < 0 && mover >= 0 && nearest == shot.fraction;
            if (nearest < kFractionOne)
            {
                // Where it stops. (A brush is met as its clip hull, a hand's
                // breadth out: brought back onto its face where that can be.)
                fx normal[3];
                if (!((onBrush || (struck < 0 && !atPlayer)) && traceShotWall(*w.world, from, to, state.base, normal)))
                {
                    for (int k = 0; k < 3; ++k)
                        state.base[k] = from[k] + static_cast<fx>((static_cast<std::int64_t>(to[k] - from[k]) * nearest) >>
                                                                  kFractionShift);
                }
                state.hurt = 1;
            }
            // (So much a second: the parts of a point are kept.)
            state.speed += static_cast<fx>(record.damage) * ms;
            const int dealt = state.speed / 1000;
            state.speed %= 1000;
            if (dealt > 0 && atPlayer)
            {
                playerHurt(player, dealt, from, kDmgEnergyBeam);
                if (w.actors != nullptr)
                    w.actors->got |= kGotHurt;
            }
            else if (dealt > 0 && struck >= 0)
            {
                monsterHurt(w, struck, dealt, false, kHitBody, kDmgEnergyBeam, kAiNone, from);
            }
            else if (dealt > 0 && onBrush)
            {
                const int taker = blowTaker(w, mover);
                if (taker >= 0)
                {
                    const fx d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
                    const fx length = fxLength(d);
                    for (int k = 0; length > 0 && k < 3; ++k)
                        w.struckFrom[k] = static_cast<fx>((static_cast<std::int64_t>(d[k]) << 12) / length);
                    logicStrike(w, player, taker, dealt, kDmgEnergyBeam);
                }
            }
            // Its sparks: now and then, at either end.
            const bool sparksStart = record.maxs[1] & kBeamSparkStart, sparksEnd = record.maxs[1] & kBeamSparkEnd;
            if (w.actors == nullptr || !(sparksStart || sparksEnd) || (state.timer -= ms) > 0)
                return;
            state.timer = 150 + static_cast<std::int32_t>(effectChance(w) % 250);
            for (int end = 0; end < 2; ++end)
            {
                if (!(end == 0 ? sparksStart : sparksEnd))
                    continue;
                const fx *at = end == 0 ? from : (state.hurt != 0 ? state.base : to);
                Effect &spark = effectAdd(*w.actors, kEffectSprite, at, nullptr, 14, 150);
                spark.sprite = static_cast<std::uint16_t>(CommonSprite::Glow);
                spark.colour = 0x3BFF; // (as an env_spark's)
            }
        }
    } // namespace detail

    /// What the effect entities do by themselves, and the shake and the
    /// fade, for `ms` more milliseconds. (Called by monstersTick.)
    inline void effectsTick(LogicWorld &w, PlayerState &player, int ms)
    {
        if (w.shakeMs > 0)
            w.shakeMs -= ms;
        // The fade: how far it is over the picture now, of 255, as the
        // game's screen fade works it out (alpha = speed * (end - now)).
        // Into the colour over `whole`, held, then let go at once; or from
        // it, lifting from the first moment -- a fade from is not held (the
        // game's hold only keeps it in its list).
        if (w.fading)
        {
            w.fadeMs += ms;
            const std::int32_t whole = w.fadeWholeMs > 0 ? w.fadeWholeMs : 1;
            const std::int32_t along = w.fadeMs >= whole ? whole : (w.fadeMs < 0 ? 0 : w.fadeMs);
            if (w.fadeFrom)
            {
                w.fadeLevel = static_cast<std::uint8_t>(w.fadeDark * (whole - along) / whole);
                if (w.fadeMs >= whole)
                    w.fading = false;
            }
            else
            {
                w.fadeLevel = static_cast<std::uint8_t>(w.fadeDark * along / whole);
                if (w.fadeMs >= whole + w.fadeHoldMs)
                {
                    w.fading = false;
                    w.fadeLevel = 0;
                }
            }
        }
        ActorWorld *a = w.actors;
        for (int i = 0; i < w.recordCount; ++i)
        {
            const LogicRecord &record = w.records[i];
            LogicState &state = w.states[i];
            if (state.dead)
                continue;
            switch (static_cast<LogicKind>(record.kind))
            {
            case LogicKind::Sprite:
            {
                if (!state.running || ((record.extra[3] & 4) && state.bits != 0))
                    break;
                // Its frame, Q8, of the frames the game's sprite has (what
                // is drawn is the nearest the atlas kept). One that plays
                // once goes out at its last.
                state.phase += static_cast<std::int32_t>((record.extra[1] * static_cast<std::uint32_t>(ms)) / 1000);
                int frames = w.sprites != nullptr && record.extra[0] < static_cast<std::uint32_t>(w.spriteCount)
                                 ? w.sprites[record.extra[0]].sourceFrames
                                 : 1;
                frames = frames < 1 ? 1 : frames;
                if ((state.phase >> 8) >= frames)
                {
                    if (record.extra[3] & 1)
                        state.running = false;
                    state.phase %= frames << 8;
                }
                break;
            }
            case LogicKind::Beam:
            {
                // One that was on from the start has still to find where
                // it goes, if that is not given.
                if ((record.flags & kLogicToggle) && state.running && state.move == MoveState::AtBottom)
                {
                    state.move = MoveState::AtTop;
                    state.running = beamAim(w, i);
                }
                if (!(record.flags & kLogicToggle))
                {
                    // A strike's time runs out; and, while it is on, the
                    // time to the next runs down.
                    if (state.bits != 0)
                        state.bits = state.bits > static_cast<std::uint32_t>(ms) ? state.bits - ms : 0;
                    if (state.running && (state.phase -= ms) <= 0)
                    {
                        state.bits = beamAim(w, i) ? record.extra[3] & 0xFFFF : 0;
                        state.phase += static_cast<std::int32_t>((record.extra[3] & 0xFFFF) + (record.extra[3] >> 16));
                    }
                }
                if (record.damage != 0)
                    detail::beamBurn(w, player, i, ms);
                break;
            }
            case LogicKind::Spark:
                if (!state.running || (state.phase -= ms) > 0)
                    break;
                state.phase = 100 + static_cast<std::int32_t>(detail::effectChance(w) %
                                                              static_cast<std::uint32_t>(record.waitMs > 100 ? record.waitMs : 100));
                if (a != nullptr)
                {
                    Effect &spark = effectAdd(*a, kEffectSprite, record.move, nullptr, 14, 150);
                    spark.sprite = static_cast<std::uint16_t>(CommonSprite::Glow);
                    spark.colour = 0x3BFF; // a yellow white
                }
                // One of its six sounds, at 0.10..0.30 of full, carrying as
                // the game's normal attenuation does (DoSpark). A map cooked
                // before has only the one, in `move`.
                if (w.heard != nullptr && record.extra[3] != 0)
                {
                    const std::uint32_t n = detail::effectChance(w) % record.extra[3];
                    w.heard->add(static_cast<std::uint16_t>(record.extra[n / 2] >> (n % 2 * 16)), record.move,
                                 13 + static_cast<int>(detail::effectChance(w) % 26), kRadiusLarge);
                }
                else if (w.heard != nullptr && w.sounds != nullptr && w.sounds[i].move != kNoSound)
                    w.heard->add(w.sounds[i].move, record.move, 90, kRadiusMedium);
                break;
            case LogicKind::Shooter:
                if (!state.running || (state.phase -= ms) > 0)
                    break;
                state.phase = static_cast<std::int32_t>(record.extra[1]);
                if (a != nullptr)
                {
                    // Along its way, a little to every side.
                    const int speed = static_cast<int>(record.extra[2]);
                    fx velocity[3];
                    for (int k = 0; k < 3; ++k)
                        velocity[k] = fxFromInt(speed) / 1000 * record.mins[k] +
                                      fxFromInt(static_cast<int>(detail::effectChance(w) % 61) - 30) *
                                          (speed / 100 + 1) / 3;
                    const int pieces = gibPieces(*a, static_cast<int>(record.damage) - 1);
                    if (pieces != 0)
                    {
                        // A piece of its model, tumbling.
                        Gib &gib = gibAdd(*a, record.damage - 1,
                                          static_cast<int>(detail::effectChance(w) % static_cast<std::uint32_t>(pieces)),
                                          record.move, velocity, static_cast<int>(record.extra[3]));
                        gib.yaw = static_cast<std::uint16_t>(detail::effectChance(w));
                        gib.spin = static_cast<std::int16_t>(30 + detail::effectChance(w) % 60);
                    }
                    else if (projectileAdd(*a, kShotGib, record.move, velocity, static_cast<int>(record.extra[3])) &&
                             record.maxs[0] > 0)
                    {
                        // Its sprite (the newest shot is the one just made).
                        for (Projectile &shot : a->shots)
                        {
                            if (shot.active && shot.kind == kShotGib && shot.sprite == 0xFFFF &&
                                shot.leftMs == static_cast<std::int32_t>(record.extra[3]))
                                shot.sprite = static_cast<std::uint16_t>(record.maxs[0] - 1);
                        }
                    }
                }
                if (state.bits > 0)
                    --state.bits;
                if (state.bits == 0)
                {
                    state.running = false;
                    if (!(record.flags & kLogicToggle))
                        state.dead = true;
                }
                // (With no time between them, all at once.)
                if (record.extra[1] == 0 && state.running)
                    --i;
                break;
            default:
                break;
            }
        }
    }
} // namespace hldata
