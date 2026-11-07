#pragma once

// Round 4: the military's machines, each from its file in the game's code,
// kept as close to it as the port allows and named as it is, so the two can
// be read side by side:
//
//   osprey      osprey.cpp     flies its path of corners, a spline between
//                              each two; at a corner of speed 0 it hovers
//                              and lets down soldiers on ropes, one for each
//                              of the map's soldiers who has died
//   assassin    hassassin.cpp  leaps, shoots as it falls, throws a grenade
//                              once it is frustrated, and runs for cover
//   apache      apache.cpp     (round 5) flies by its rotor's lift and its
//                              tipping, along its corners and at the
//                              player; its gun, and rockets in pairs
//
// std-only and inline (HlMonsters.hpp includes it after HlAliens.hpp): the
// host tests run it.

namespace hldata
{
    // ================================================================= ropes

    /// A soldier into place `i` at the top of a rope at `src` (where his
    /// middle is): as the map made him, woken, sliding down (the game's
    /// MOVETYPE_FLY at -128 to -196, and ACT_GLIDE). The osprey's
    /// MakeGrunt, and CHGruntRepel's.
    inline void ropeStart(ActorWorld &a, const MoveWorld &world, int i, const fx *src, std::uint16_t yaw)
    {
        actorSetup(a, i, a.records[i], world);
        ActorState &grunt = a.states[i];
        grunt.flags &= static_cast<std::uint8_t>(~(kActorWaitScript | kActorWaitSeen | kActorDormant));
        aiForget(a, i); // (a new soldier in the old one's place)
        for (int k = 0; k < 3; ++k)
            grunt.pos[k] = src[k];
        grunt.pos[2] -= kStandingFeet;
        grunt.yaw = yaw;
        grunt.velocity[0] = grunt.velocity[1] = 0;
        grunt.velocity[2] = -fxFromInt(aiRandomLong(a, 128, 196));
        actorPlay(a, grunt, nameHash("repel_repel"), false);
        if (a.ai != nullptr)
            a.ai->held[i] |= kHeldRope;
        actorPlaceBox(a, i);
    }

    /// The soldier on the rope in place `i`, a frame down it, until there
    /// is ground under him; the rope seen from `topZ` to his hands. False
    /// once he is off it (down, or gone).
    inline bool ropeDown(ActorWorld &a, const MoveWorld &world, int i, fx topZ, int ms)
    {
        if (a.ai == nullptr || i < 0 || i >= a.count || !(a.ai->held[i] & kHeldRope))
            return false;
        ActorState &grunt = a.states[i];
        if (!grunt.active || grunt.ai >= kAiDying)
        {
            a.ai->held[i] &= static_cast<std::uint8_t>(~kHeldRope);
            return false;
        }
        const fx from[3] = {grunt.pos[0], grunt.pos[1], grunt.pos[2] + kStandingFeet};
        const fx to[3] = {from[0], from[1], from[2] + static_cast<fx>((static_cast<std::int64_t>(grunt.velocity[2]) * ms) / 1000)};
        const Trace t = detail::walkerTrace(world, &a, i, false, from, to);
        if (!t.startSolid)
            grunt.pos[2] = t.endPos[2] - kStandingFeet;
        bool still = true;
        if (t.startSolid || t.fraction < kFractionOne)
        {
            a.ai->held[i] &= static_cast<std::uint8_t>(~kHeldRope);
            grunt.velocity[2] = 0;
            actorPlay(a, grunt, nameHash("repel_land"), true);
            still = false;
        }
        actorPlaceBox(a, i);
        const fx top[3] = {grunt.pos[0], grunt.pos[1], topZ};
        const fx hands[3] = {grunt.pos[0], grunt.pos[1], grunt.pos[2] + fxFromInt(72)};
        Effect &rope = effectAdd(a, kEffectBeam, top, hands, 2, ms + 50);
        rope.colour = 0x7FFF;
        return still;
    }

    namespace detail
    {
        /// A monster_grunt_repel, a frame on (CHGruntRepel): used, its
        /// soldier comes down its rope from where it is, and it is done.
        /// Its state: `running` used and waiting, `phase` 1 while he is on
        /// the rope.
        inline void repelTick(LogicWorld &w, PlayerState &, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            ActorWorld &a = *w.actors;
            const int i = static_cast<int>(record.extra[0]);
            if (state.dead || i < 0 || i >= a.count || i >= a.recordCount)
                return;
            if (state.running && state.phase == 0)
            {
                state.running = false;
                const fx down[3] = {record.move[0], record.move[1], record.move[2] - fxFromInt(4096)};
                if (traceShot(*w.world, record.move, down).fraction >= kFractionOne)
                    return;
                ropeStart(a, *w.world, i, record.move, a.records[i].yaw);
                state.phase = 1;
            }
            if (state.phase == 1 && !ropeDown(a, *w.world, i, record.move[2] + fxFromInt(112), ms))
            {
                state.phase = 2;
                state.dead = true; // (it is gone: UTIL_Remove)
            }
        }
    } // namespace detail

    // ================================================================ osprey

    namespace osprey
    {
        inline constexpr int kHealth = 400;
        inline constexpr std::uint8_t kActorWaitTrigger = kActorCeiling; ///< SF_WAITFORTRIGGER (0x40)
        inline constexpr int kRepels = 4;
        // m_pGoalEnt: count1 (a corner, or -1); m_pos1, m_pos2, m_vel1,
        // m_vel2: oldEnemyAt[0..3] (it has no enemies to remember);
        // m_ang1.y, m_ang2.y: value1, value2 (degrees Q12); m_startTime:
        // time1, m_dTime: time2 (ms); its think: flag2 once FindAllThink has
        // been, flag1 while it hovers (HoverThink); FindAllThink's second:
        // time3; m_hRepel: squadMember (it is not one of a squad);
        // m_flRotortilt: aimPitch (65536 to the circle, as a bone controller
        // has it). The corner it flies to first is its record's (the low 16
        // bits of `item`, plus one).

        inline fx *pos1(AiCtx &c) { return c.m.oldEnemyAt[0]; }
        inline fx *pos2(AiCtx &c) { return c.m.oldEnemyAt[1]; }
        inline fx *vel1(AiCtx &c) { return c.m.oldEnemyAt[2]; }
        inline fx *vel2(AiCtx &c) { return c.m.oldEnemyAt[3]; }

        /// The corner it was given to fly to first, or -1.
        inline int firstCorner(const AiCtx &c)
        {
            if (c.a.records == nullptr || c.index >= c.a.recordCount)
                return -1;
            const int corner = static_cast<int>(c.a.records[c.index].item & 0xFFFF) - 1;
            return corner >= 0 && corner < c.w.cornerCount ? corner : -1;
        }

        /// A corner's yaw, in degrees Q12. (The game's corners have their
        /// own angles; this port's keep none, so it is the way to the corner
        /// after, which is what the maps give them -- or, at the end of the
        /// path, the way it came.)
        inline fx cornerYaw(AiCtx &c, int corner)
        {
            const PathCorner &at = c.w.corners[corner];
            fx d[2];
            if (at.next != kNoCorner && at.next < c.w.cornerCount && at.next != corner)
            {
                d[0] = c.w.corners[at.next].pos[0] - at.pos[0];
                d[1] = c.w.corners[at.next].pos[1] - at.pos[1];
            }
            else
            {
                d[0] = at.pos[0] - pos1(c)[0];
                d[1] = at.pos[1] - pos1(c)[1];
            }
            if (d[0] == 0 && d[1] == 0)
                return c.m.value2;
            return static_cast<fx>((static_cast<std::int64_t>(fxAtan2Turns(d[1], d[0])) * 360 * kFxOne) >> 16);
        }

        inline std::uint16_t turnsOf(fx degrees)
        {
            return static_cast<std::uint16_t>((static_cast<std::int64_t>(degrees) << 16) / (360 * kFxOne));
        }

        /// One of the map's soldiers (not a maker's place kept for one).
        inline bool isSoldier(const AiCtx &c, int i)
        {
            return c.a.states[i].kind == static_cast<std::uint8_t>(ActorKind::Grunt) && c.a.records != nullptr &&
                   i < c.a.recordCount && !(c.a.records[i].flags & kActorDormant);
        }
        inline bool onRope(const AiCtx &c, int i)
        {
            return (c.ai.held[i] & kHeldRope) != 0;
        }

        /// HasDead: one of the soldiers it came to resupply is dead. (The
        /// game keeps a list of them as they were when it set out; here,
        /// they are the map's soldiers.)
        inline bool hasDead(AiCtx &c)
        {
            for (int i = 0; i < c.a.count; ++i)
            {
                if (isSoldier(c, i) && !onRope(c, i) && (!c.a.states[i].active || c.a.states[i].ai == kAiDead))
                    return true;
            }
            return false;
        }

        /// UpdateGoal
        inline void updateGoal(AiCtx &c)
        {
            if (c.m.count1 < 0)
                return;
            const PathCorner &goal = c.w.corners[c.m.count1];
            for (int k = 0; k < 3; ++k)
            {
                pos1(c)[k] = pos2(c)[k];
                vel1(c)[k] = vel2(c)[k];
                pos2(c)[k] = goal.pos[k];
            }
            c.m.value1 = c.m.value2;
            c.m.value2 = cornerYaw(c, c.m.count1);
            fx forward[3];
            aiForward(turnsOf(c.m.value2), forward);
            for (int k = 0; k < 3; ++k)
                vel2(c)[k] = fxMul(forward[k], goal.speed);
            c.m.time1 += c.m.time2;
            const fx far = aiDist(pos1(c), pos2(c));
            fx speeds = fxLength(vel1(c)) + goal.speed;
            if (speeds <= 0)
                speeds = fxFromInt(100); // (the game's would be still for ever)
            c.m.time2 = static_cast<std::int32_t>((static_cast<std::int64_t>(far) * 2000) / speeds);
            if (c.m.value1 - c.m.value2 < -fxFromInt(180))
                c.m.value1 += fxFromInt(360);
            else if (c.m.value1 - c.m.value2 > fxFromInt(180))
                c.m.value1 -= fxFromInt(360);
        }

        /// Flight: where it is along the spline from one corner to the next,
        /// `t` ms into it.
        inline void flight(AiCtx &c)
        {
            if (c.m.time2 <= 0)
                return;
            std::int32_t t = c.ai.now - c.m.time1;
            if (t < 0)
                t = 0;
            const fx ts = static_cast<fx>((static_cast<std::int64_t>(t) << kFxShift) / 1000);
            const fx rest = static_cast<fx>((static_cast<std::int64_t>(c.m.time2 - t) << kFxShift) / 1000);
            // UTIL_SplineFraction
            const fx u = static_cast<fx>((static_cast<std::int64_t>(t) << kFxShift) / c.m.time2);
            const fx u2 = fxMul(u, u);
            const fx f = 3 * u2 - 2 * fxMul(u2, u);
            for (int k = 0; k < 3; ++k)
            {
                const fx from = pos1(c)[k] + fxMul(vel1(c)[k], ts);
                const fx to = pos2(c)[k] - fxMul(vel2(c)[k], rest);
                c.me.pos[k] = from + fxMul(to - from, f);
                c.me.velocity[k] = vel1(c)[k] + fxMul(vel2(c)[k] - vel1(c)[k], f);
            }
            c.me.yaw = turnsOf(c.m.value1 + fxMul(c.m.value2 - c.m.value1, f));
            c.m.idealYaw = c.me.yaw;
        }

        /// MakeGrunt: a soldier down a rope from `src`, in the place of one
        /// who has died; -1 if none has, or there is nothing under it to
        /// stand on.
        inline int makeGrunt(AiCtx &c, const fx *src)
        {
            const fx down[3] = {src[0], src[1], src[2] - fxFromInt(4096)};
            if (traceShot(*c.w.world, src, down).fraction >= kFractionOne)
                return -1;
            for (int i = 0; i < c.a.count; ++i)
            {
                if (!isSoldier(c, i) || onRope(c, i) || (c.a.states[i].active && c.a.states[i].ai != kAiDead))
                    continue;
                ropeStart(c.a, *c.w.world, i, src, c.me.yaw);
                return i;
            }
            return -1;
        }

        /// DeployThink: four soldiers, if four have died, from its sides.
        inline void deploy(AiCtx &c)
        {
            fx forward[3], right[3];
            aiForward(c.me.yaw, forward);
            aiRight(c.me.yaw, right);
            const fx below[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] - fxFromInt(4096)};
            const Shot ground = traceShot(*c.w.world, c.me.pos, below);
            fx landing[3];
            for (int k = 0; k < 3; ++k)
                landing[k] = c.me.pos[k] + static_cast<fx>((static_cast<std::int64_t>(below[k] - c.me.pos[k]) * ground.fraction) >> kFractionShift);
            aiSound(c.a, kSoundDanger, landing, 400, 300);
            static const int kAt[kRepels][2] = {{32, 100}, {-64, 100}, {32, -100}, {-64, -100}};
            for (int r = 0; r < kRepels; ++r)
            {
                fx src[3];
                for (int k = 0; k < 3; ++k)
                    src[k] = c.me.pos[k] + forward[k] * kAt[r][0] + right[k] * kAt[r][1];
                src[2] -= fxFromInt(96);
                c.m.squadMember[r] = static_cast<std::int16_t>(makeGrunt(c, src));
            }
            c.m.flag1 = true;
        }

        /// Each soldier on a rope goes down it, until there is ground under
        /// him (the game's MOVETYPE_FLY, until FL_ONGROUND). The rope is seen
        /// from the osprey to him.
        inline bool repelsTick(AiCtx &c, int ms)
        {
            bool any = false;
            for (int r = 0; r < kRepels; ++r)
            {
                const int i = c.m.squadMember[r];
                if (i < 0 || i >= c.a.count || !onRope(c, i))
                    continue;
                any = true;
                ropeDown(c.a, *c.w.world, i, c.me.pos[2] + fxFromInt(16), ms);
            }
            return any;
        }

        /// The goal after the one it has reached: on past those it need not
        /// slow for, unless it has soldiers to let down.
        inline void nextGoal(AiCtx &c)
        {
            int goal = c.m.count1;
            for (int guard = 0; guard <= c.w.cornerCount; ++guard)
            {
                const std::uint16_t next = c.w.corners[goal].next;
                if (next == kNoCorner || next >= c.w.cornerCount)
                {
                    goal = -1; // (the path ends: it stays where it is)
                    break;
                }
                goal = next;
                if (c.w.corners[goal].speed >= fxFromInt(400) || hasDead(c))
                    break;
            }
            c.m.count1 = goal;
        }

        /// Its rotors, heard from far off for as long as it flies (the game's
        /// ATTN 0.15); on, or off.
        inline void rotors(AiCtx &c, bool on)
        {
            if (c.w.heard == nullptr)
                return;
            const auto tag = static_cast<std::uint16_t>(kSoundTagActor | c.index);
            if (on && static_cast<std::uint16_t>(CommonSound::OspreyRotor) < c.w.heard->commons)
                c.w.heard->add(static_cast<std::uint16_t>(CommonSound::OspreyRotor), c.me.pos, 127, 6667, kSoundLoopOn,
                               100, tag);
            else if (!on)
                c.w.heard->add(kNoSound, nullptr, 0, 0, kSoundLoopOff, 100, tag);
        }

        /// FindAllThink, FlyThink, HoverThink, as it has got to.
        inline void think(AiCtx &c)
        {
            if (c.me.ai >= kAiDying)
                return;
            if (c.m.flag2)
                rotors(c, true);
            if (!c.m.flag2)
            {
                if ((c.me.flags & kActorWaitTrigger) && c.me.switched == 0)
                    return;
                c.me.switched = 0;
                if (c.m.time3 == 0)
                    c.m.time3 = c.ai.now + 1000;
                if (c.ai.now < c.m.time3)
                    return;
                // FindAllThink: there must be soldiers to resupply.
                bool any = false;
                for (int i = 0; i < c.a.count && !any; ++i)
                    any = isSoldier(c, i) && c.a.states[i].active && c.a.states[i].ai < kAiDying;
                if (!any)
                {
                    c.me.active = false; // ("osprey error: no grunts to resupply")
                    actorPlaceBox(c.a, c.index);
                    return;
                }
                c.m.flag2 = true;
                c.m.count1 = -1;
                c.m.time1 = c.ai.now;
                c.m.time2 = 0;
                for (int k = 0; k < 3; ++k)
                {
                    pos2(c)[k] = c.me.pos[k];
                    vel2(c)[k] = 0;
                }
                c.m.value2 = static_cast<fx>((static_cast<std::int64_t>(c.me.yaw) * 360 * kFxOne) >> 16);
                // (Its first goal, as FlyThink finds it.)
                c.m.count1 = firstCorner(c);
                updateGoal(c);
            }
            if (c.m.flag1)
            {
                // HoverThink: until every one of them is down.
                bool roping = false;
                for (int r = 0; r < kRepels; ++r)
                {
                    const int i = c.m.squadMember[r];
                    roping = roping || (i >= 0 && onRope(c, i));
                }
                if (!roping)
                {
                    c.m.flag1 = false;
                    c.m.time1 = c.ai.now;
                }
            }
            else if (c.m.count1 >= 0 && c.ai.now > c.m.time1 + c.m.time2)
            {
                const bool stop = c.w.corners[c.m.count1].speed == 0;
                nextGoal(c);
                updateGoal(c);
                if (stop)
                    deploy(c);
            }
            // The rotors tilt with its speed (Flight).
            fx forward[3];
            aiForward(c.me.yaw, forward);
            const fx speed = fxMul(forward[0], c.me.velocity[0]) + fxMul(forward[1], c.me.velocity[1]);
            const std::int32_t ideal = static_cast<std::int32_t>(
                (static_cast<std::int64_t>(fxFromInt(160) - speed) * 65536) / (10 * 360 * kFxOne));
            const std::int32_t step = 65536 / 720; // (half a degree)
            if (c.me.aimPitch < ideal)
            {
                c.me.aimPitch += step;
                if (c.me.aimPitch > 0)
                    c.me.aimPitch = 0;
            }
            if (c.me.aimPitch > ideal)
            {
                c.me.aimPitch -= step;
                if (c.me.aimPitch < -16384)
                    c.me.aimPitch = -16384;
            }
        }

        /// Each frame: where the spline has it; the soldiers on their ropes;
        /// or, shot down, falling, burning, and at the ground, the end of it
        /// (DyingThink, CrashTouch).
        inline void frame(AiCtx &c, int ms)
        {
            if (c.me.ai >= kAiDying)
            {
                if (!c.me.active)
                    return;
                const fx g = gravityOf(c.player) * 3 / 10;
                c.me.velocity[2] -= static_cast<fx>((static_cast<std::int64_t>(g) * ms) / 1000);
                fx to[3];
                for (int k = 0; k < 3; ++k)
                    to[k] = c.me.pos[k] + static_cast<fx>((static_cast<std::int64_t>(c.me.velocity[k]) * ms) / 1000);
                const Shot shot = traceShot(*c.w.world, c.me.pos, to);
                for (int k = 0; k < 3; ++k)
                    c.me.pos[k] += static_cast<fx>((static_cast<std::int64_t>(to[k] - c.me.pos[k]) * shot.fraction) >> kFractionShift);
                c.me.yaw = static_cast<std::uint16_t>(c.me.yaw + c.m.count1 * ms / 1000);
                // (Explosions about it as it falls, five a second.)
                c.m.time3 -= ms;
                if (c.m.time3 <= 0 && c.ai.now < c.m.time1)
                {
                    c.m.time3 += 200;
                    const fx spot[3] = {c.me.pos[0] + fxFromInt(aiRandomLong(c.a, -150, 150)),
                                        c.me.pos[1] + fxFromInt(aiRandomLong(c.a, -150, 150)),
                                        c.me.pos[2] + fxFromInt(aiRandomLong(c.a, -150, -50))};
                    explodeAt(c.w, c.player, spot, aiRandomLong(c.a, 30, 59), true, false, false);
                }
                if (shot.fraction < kFractionOne || c.ai.now >= c.m.time1 + 6000)
                {
                    // The end: one great blast, and what is left of it.
                    explodeAt(c.w, c.player, c.me.pos, 160, true, false, false);
                    if (c.w.heard != nullptr)
                        c.w.heard->add(CommonSound::MortarHit, c.me.pos, 127, 3000);
                    radiusDamage(c.w, c.player, c.me.pos, 300, 750, c.index);
                    shakeStart(c.w, fxFromInt(8), 1000);
                    c.me.active = false;
                    actorPlaceBox(c.a, c.index);
                }
                return;
            }
            const bool roping = repelsTick(c, ms);
            (void)roping;
            if (c.m.flag2 && !c.m.flag1)
                flight(c);
            actorPlaceBox(c.a, c.index);
        }

        /// TraceAttack: only so much per engine; what is not hard enough,
        /// and lands on neither the cockpit nor an engine, only sparks.
        inline bool traceAttack(AiCtx &c, AiDamage &d)
        {
            (void)c;
            // (Its model's hit groups: 1 the cockpit, 2 and 3 the engines.)
            const bool counts = d.amount > 50 || d.group == 1 || d.group == 2 || d.group == 3 || (d.type & kDmgBlast) != 0;
            d.group = kHitGeneric; // (and no more for the cockpit than for the rest: its own TraceAttack)
            return counts;
        }

        /// Killed: it falls, slowly, turning, for up to four seconds.
        inline bool killed(AiCtx &c)
        {
            rotors(c, false);
            c.me.ai = kAiDying;
            c.m.deadflag = 1;
            c.m.state = c.m.idealState = kStateDead;
            c.me.health = 0;
            c.m.count1 = aiRandomLong(c.a, -50, 50) * 65536 / 360; // (its turn, a second)
            c.m.time1 = c.ai.now + 4000;
            c.m.time3 = 0;
            for (int r = 0; r < kRepels; ++r)
            {
                const int i = c.m.squadMember[r];
                if (i >= 0 && i < c.a.count)
                    c.ai.held[i] &= static_cast<std::uint8_t>(~kHeldRope);
            }
            return true;
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Osprey);
                k.health = kHealth;
                k.fov = 0; // (180 degrees)
                k.blood = kBloodNone;
                k.think = think;
                k.frame = frame;
                k.traceAttack = traceAttack;
                k.killed = killed;
                return k;
            }();
            return k;
        }
    } // namespace osprey

    // ============================================================= assassin

    namespace assassin
    {
        inline constexpr int kShoot = 1;     ///< ASSASSIN_AE_SHOOT1
        inline constexpr int kToss = 2;      ///< ASSASSIN_AE_TOSS1
        inline constexpr int kJump = 3;      ///< ASSASSIN_AE_JUMP
        inline constexpr int kHealth = 50;   ///< sk_hassassin_health at MEDIUM (aiInit: the game's level's)
        inline constexpr std::uint32_t kMemoryBadJump = kMemoryCustom1;
        // m_flLastShot: time1; m_flDiviation: value1 (Q12); m_flNextJump:
        // time2; m_vecJumpVelocity: vec1; m_flNextGrenadeCheck: time3;
        // m_vecTossVelocity: foeAt (a guard's, which it is not);
        // m_fThrowGrenade: flag1; MOVETYPE_TOSS (in the air from a jump):
        // flag2; m_iFrustration: count1. (Its cloak, on DIFFICULT only, is
        // the actor's own `cloak`: runAI.)

        enum : std::uint8_t
        {
            kSchedExposed = kSchedLastCommon + 1, ///< SCHED_ASSASSIN_EXPOSED: its cover blown
            kSchedJump,                           ///< SCHED_ASSASSIN_JUMP
            kSchedJumpAttack,                     ///< SCHED_ASSASSIN_JUMP_ATTACK
            kSchedJumpLand,                       ///< SCHED_ASSASSIN_JUMP_LAND
        };
        enum : std::uint8_t
        {
            kTaskFallToGround = kTaskLastCommon + 1, ///< TASK_ASSASSIN_FALL_TO_GROUND
        };

        inline constexpr AiTask tlFail[] = {
            {kTaskStopMoving, 0},
            {kTaskSetActivity, kActIdle},
            {kTaskWaitFaceEnemy, 2000},
            {kTaskSetSchedule, kSchedChaseEnemy},
        };
        inline constexpr AiSchedule slFail = {tlFail, 4,
                                              kCondLightDamage | kCondHeavyDamage | kCondProvoked | kCondCanRangeAttack1 |
                                                  kCondCanRangeAttack2 | kCondCanMeleeAttack1 | kCondHearSound,
                                              kSoundDanger | kSoundPlayer, "AssassinFail"};
        inline constexpr AiTask tlExposed[] = {
            {kTaskStopMoving, 0},
            {kTaskRangeAttack1, 0},
            {kTaskSetFailSchedule, kSchedJump},
            {kTaskSetSchedule, kSchedTakeCoverFromEnemy},
        };
        inline constexpr AiSchedule slExposed = {tlExposed, 4, kCondCanMeleeAttack1, 0, "AssassinExposed"};
        inline constexpr AiTask tlTakeCoverFromEnemy[] = {
            {kTaskStopMoving, 0},
            {kTaskWait, 200},
            {kTaskSetFailSchedule, kSchedRangeAttack1},
            {kTaskFindCoverFromEnemy, 0},
            {kTaskRunPath, 0},
            {kTaskWaitForMovement, 0},
            {kTaskRemember, static_cast<std::int32_t>(kMemoryInCover)},
            {kTaskFaceEnemy, 0},
        };
        inline constexpr AiSchedule slTakeCoverFromEnemy = {tlTakeCoverFromEnemy, 8,
                                                            kCondNewEnemy | kCondCanMeleeAttack1 | kCondHearSound,
                                                            kSoundDanger, "AssassinTakeCoverFromEnemy"};
        inline constexpr AiTask tlTakeCoverFromEnemy2[] = {
            {kTaskStopMoving, 0},
            {kTaskWait, 200},
            {kTaskFaceEnemy, 0},
            {kTaskRangeAttack1, 0},
            {kTaskSetFailSchedule, kSchedRangeAttack2},
            {kTaskFindFarNodeCoverFromEnemy, 384},
            {kTaskRunPath, 0},
            {kTaskWaitForMovement, 0},
            {kTaskRemember, static_cast<std::int32_t>(kMemoryInCover)},
            {kTaskFaceEnemy, 0},
        };
        inline constexpr AiSchedule slTakeCoverFromEnemy2 = {tlTakeCoverFromEnemy2, 10,
                                                             kCondNewEnemy | kCondCanMeleeAttack2 | kCondHearSound,
                                                             kSoundDanger, "AssassinTakeCoverFromEnemy2"};
        inline constexpr AiTask tlTakeCoverFromBestSound[] = {
            {kTaskSetFailSchedule, kSchedMeleeAttack1},
            {kTaskStopMoving, 0},
            {kTaskFindCoverFromBestSound, 0},
            {kTaskRunPath, 0},
            {kTaskWaitForMovement, 0},
            {kTaskRemember, static_cast<std::int32_t>(kMemoryInCover)},
            {kTaskTurnLeft, 179},
        };
        inline constexpr AiSchedule slTakeCoverFromBestSound = {tlTakeCoverFromBestSound, 7, kCondNewEnemy, 0,
                                                                "AssassinTakeCoverFromBestSound"};
        inline constexpr AiTask tlHide[] = {
            {kTaskStopMoving, 0},
            {kTaskSetActivity, kActIdle},
            {kTaskWait, 2000},
            {kTaskSetSchedule, kSchedChaseEnemy},
        };
        inline constexpr AiSchedule slHide = {tlHide, 4,
                                              kCondNewEnemy | kCondSeeEnemy | kCondSeeFear | kCondLightDamage |
                                                  kCondHeavyDamage | kCondProvoked | kCondHearSound,
                                              kSoundDanger, "AssassinHide"};
        inline constexpr AiTask tlHunt[] = {
            {kTaskGetPathToEnemy, 0},
            {kTaskRunPath, 0},
            {kTaskWaitForMovement, 0},
        };
        inline constexpr AiSchedule slHunt = {tlHunt, 3, kCondNewEnemy | kCondCanRangeAttack1 | kCondHearSound,
                                              kSoundDanger, "AssassinHunt"};
        inline constexpr AiTask tlJump[] = {
            {kTaskStopMoving, 0},
            {kTaskPlaySequence, kActHop},
            {kTaskSetSchedule, kSchedJumpAttack},
        };
        inline constexpr AiSchedule slJump = {tlJump, 3, 0, 0, "AssassinJump"};
        inline constexpr AiTask tlJumpAttack[] = {
            {kTaskSetFailSchedule, kSchedJumpLand},
            {kTaskFallToGround, 0},
        };
        inline constexpr AiSchedule slJumpAttack = {tlJumpAttack, 2, 0, 0, "AssassinJumpAttack"};
        inline constexpr AiTask tlJumpLand[] = {
            {kTaskSetFailSchedule, kSchedExposed},
            {kTaskSetActivity, kActIdle},
            {kTaskRemember, static_cast<std::int32_t>(kMemoryBadJump)},
            {kTaskFindNodeCoverFromEnemy, 0},
            {kTaskRunPath, 0},
            {kTaskForget, static_cast<std::int32_t>(kMemoryBadJump)},
            {kTaskWaitForMovement, 0},
            {kTaskRemember, static_cast<std::int32_t>(kMemoryInCover)},
            {kTaskFaceEnemy, 0},
            {kTaskSetFailSchedule, kSchedRangeAttack1},
        };
        inline constexpr AiSchedule slJumpLand = {tlJumpLand, 10, 0, 0, "AssassinJumpLand"};

        inline int yawSpeed(AiCtx &) { return 360; }

        /// GetGunPosition: m_HackedGunPos, 24 to its right and 48 up.
        inline void gunPosition(const AiCtx &c, fx *at)
        {
            fx right[3];
            aiRight(c.me.yaw, right);
            for (int k = 0; k < 3; ++k)
                at[k] = c.me.pos[k] + right[k] * 24;
            at[2] += fxFromInt(48);
        }

        /// Shoot: a round of its pistol, surer with each that follows soon
        /// after the one before.
        inline void shoot(AiCtx &c)
        {
            if (c.m.enemy == kAiNone)
                return;
            fx gun[3], dir[3];
            gunPosition(c, gun);
            aiShootAtEnemy(c, gun, dir);
            if (c.m.time1 + 2000 < c.ai.now)
                c.m.value1 = fxDot(0.10);
            else if ((c.m.value1 -= fxDot(0.01)) < fxDot(0.02))
                c.m.value1 = fxDot(0.02);
            c.m.time1 = c.ai.now;
            fx aim[3];
            for (int k = 0; k < 3; ++k)
                aim[k] = dir[k] + fxMul(c.m.value1, fxFromInt(aiRandomLong(c.a, -1000, 1000)) / 1000);
            fx end[3];
            int group = kHitBody;
            const std::int16_t hit = aiTraceLine(c, gun, aim, 2048, end, &group);
            effectAdd(c.a, kEffectTracer, gun, end, 0, 60);
            const fx flash[3] = {gun[0] + dir[0] * 16, gun[1] + dir[1] * 16, gun[2] + dir[2] * 16};
            effectAdd(c.a, kEffectMuzzle, flash, nullptr, 0, 50);
            {
                // (Her casing: from her middle, a little ahead.)
                fx forward[3];
                aiForward(c.me.yaw, forward);
                const fx breech[3] = {c.me.pos[0] + forward[0] * 12, c.me.pos[1] + forward[1] * 12,
                                      c.me.pos[2] + fxFromInt(32)};
                aiEjectBrass(c, kPieceShell, breech);
            }
            if (c.w.heard != nullptr)
                c.w.heard->add(aiRandomLong(c.a, 0, 1) ? CommonSound::AssassinShot1 : CommonSound::AssassinShot2,
                               c.me.pos, 70 + aiRandomLong(c.a, 0, 25), kRadiusLarge);
            aiSound(c.a, kSoundCombat, c.me.pos, 384, 300);
            if (hit == kAiNone)
            {
                const fx far[3] = {gun[0] + aim[0] * 2048, gun[1] + aim[1] * 2048, gun[2] + aim[2] * 2048};
                markShot(c.a, *c.w.world, gun, far, false);
                return;
            }
            if (hit >= 0)
                bleed(c.w, hit, end, aim, false);
            aiDamageTarget(c, hit, skillOf(c.w).monster9mmBullet, kDmgBullet | kDmgNeverGib, static_cast<std::uint8_t>(group)); // (BULLET_MONSTER_9MM)
        }

        inline void animEvent(AiCtx &c, int event)
        {
            switch (event)
            {
            case kShoot:
                shoot(c);
                break;
            case kToss:
            {
                fx forward[3];
                aiForward(c.me.yaw, forward);
                const fx at[3] = {c.me.pos[0] + forward[0] * 34, c.me.pos[1] + forward[1] * 34,
                                  c.me.pos[2] + fxFromInt(32)};
                projectileAdd(c.a, kShotGrenade, at, c.me.foeAt, 2000);
                c.m.time3 = c.ai.now + 6000; // (six seconds before it even looks again)
                c.m.flag1 = false;
                break;
            }
            case kJump:
                aiLeap(c, c.m.vec1);
                c.m.flag2 = true;
                c.m.time2 = c.ai.now + 3000;
                break;
            default:
                break;
            }
        }

        /// The jump: somewhere a little to one side, 160 up, if there is
        /// room for it on the way -- near what it is after, or after a jump
        /// that went badly.
        inline bool checkMelee1(AiCtx &c, fx, fx dist)
        {
            if (c.m.time2 >= c.ai.now || (dist > fxFromInt(128) && !aiHasMemory(c, kMemoryBadJump)) ||
                c.m.enemy == kAiNone)
                return false;
            const fx dest[3] = {c.me.pos[0] + fxFromInt(aiRandomLong(c.a, -64, 64)),
                                c.me.pos[1] + fxFromInt(aiRandomLong(c.a, -64, 64)), c.me.pos[2] + fxFromInt(160)};
            const fx from[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + kStandingFeet};
            const fx to[3] = {dest[0], dest[1], dest[2] + kStandingFeet};
            const Trace t = detail::walkerTrace(*c.w.world, &c.a, c.index, false, from, to);
            if (t.startSolid || t.fraction < kFractionOne)
                return false;
            // (Up in the time it takes to fall 160, and across in it.)
            const int gravity = fxToInt(gravityOf(c.player));
            if (gravity <= 0)
                return false;
            const std::int64_t ms = isqrt64(static_cast<std::uint64_t>(160) * 2000000u / static_cast<std::uint32_t>(gravity));
            for (int k = 0; k < 3; ++k)
                c.m.vec1[k] = static_cast<fx>((static_cast<std::int64_t>(dest[k] - c.me.pos[k]) * gravity * ms) / (160 * 1000));
            return true;
        }

        /// Its pistol: anything it can see, not too near.
        inline bool checkRange1(AiCtx &c, fx, fx dist)
        {
            if (aiHas(c, kCondEnemyOccluded) || dist <= fxFromInt(64) || dist > fxFromInt(2048))
                return false;
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.enemy, t))
                return false;
            fx gun[3];
            gunPosition(c, gun);
            fx dir[3] = {t.center[0] - gun[0], t.center[1] - gun[1], t.center[2] - gun[2]};
            const fx far = fxLength(dir);
            if (far <= 0)
                return false;
            for (int k = 0; k < 3; ++k)
                dir[k] = static_cast<fx>((static_cast<std::int64_t>(dir[k]) << kFxShift) / far);
            fx end[3];
            const std::int16_t hit = aiTraceLine(c, gun, dir, fxToInt(far) + 1, end);
            return hit == c.m.enemy || hit == kAiNone;
        }

        /// A grenade, once it has grown frustrated: at what stands within
        /// 512, thrown to get there in a second.
        inline bool checkRange2(AiCtx &c, fx, fx dist)
        {
            c.m.flag1 = false;
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.enemy, t) || !t.onGround || c.m.count1 <= 2)
                return false;
            if (c.m.time3 >= c.ai.now || aiHas(c, kCondEnemyOccluded) || dist > fxFromInt(512))
                return false;
            fx gun[3];
            gunPosition(c, gun);
            fx toss[3];
            if (!grunt::vecCheckThrow(c, gun, t.center, fxToInt(dist) > 0 ? fxToInt(dist) : 1, 50, toss))
                return false;
            for (int k = 0; k < 3; ++k)
                c.me.foeAt[k] = toss[k];
            c.m.flag1 = true;
            return true;
        }

        /// A clip of its model by name, from the start (for its flight,
        /// which is no activity's).
        inline void playNamed(AiCtx &c, const char *name)
        {
            const ModelView *model = actorModel(c.a, c.me);
            const int clip = model != nullptr ? model->findClip(nameHash(name)) : -1;
            if (clip >= 0)
                aiPlayClip(c, clip, true);
        }

        inline bool startTask(AiCtx &c, const AiTask &task)
        {
            if (task.task == kTaskRangeAttack2 && !c.m.flag1)
            {
                aiTaskComplete(c);
                return true;
            }
            return task.task == kTaskFallToGround;
        }

        inline bool runTask(AiCtx &c, const AiTask &task)
        {
            if (task.task != kTaskFallToGround)
                return false;
            aiMakeIdealYaw(c, c.m.enemyLkp);
            aiChangeYaw(c);
            if (c.m.seqFinished)
            {
                if (c.me.velocity[2] > 0)
                    playNamed(c, "fly_up");
                else if (aiHas(c, kCondSeeEnemy))
                    playNamed(c, "fly_attack");
                else
                    playNamed(c, "fly_down");
                c.m.seqFinished = false;
            }
            if (!c.m.inAir)
                aiTaskComplete(c);
            return true;
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            switch (type)
            {
            case kSchedTakeCoverFromEnemy:
                return c.me.health > 30 ? &slTakeCoverFromEnemy : &slTakeCoverFromEnemy2;
            case kSchedTakeCoverFromBestSound:
                return &slTakeCoverFromBestSound;
            case kSchedExposed:
                return &slExposed;
            case kSchedFail:
                if (c.m.state == kStateCombat)
                    return &slFail;
                break;
            case kSchedAlertStand:
                if (c.m.state == kStateCombat)
                    return &slHide;
                break;
            case kSchedChaseEnemy:
                return &slHunt;
            case kSchedMeleeAttack1:
                if (!c.m.inAir)
                    return c.m.time2 > c.ai.now ? &slFail : &slJump;
                return &slJumpAttack;
            case kSchedJump:
            case kSchedJumpAttack:
                return &slJumpAttack;
            case kSchedJumpLand:
                return &slJumpLand;
            default:
                break;
            }
            return aiBaseScheduleOfType(c, type);
        }

        inline const AiSchedule *getSchedule(AiCtx &c)
        {
            switch (c.m.state)
            {
            case kStateIdle:
            case kStateAlert:
                if (aiHas(c, kCondHearSound))
                {
                    const AiSound *heard = aiBestSound(c);
                    if (heard != nullptr && (heard->type & kSoundDanger))
                        return scheduleOfType(c, kSchedTakeCoverFromBestSound);
                    if (heard != nullptr && (heard->type & kSoundCombat))
                        return scheduleOfType(c, kSchedInvestigateSound);
                }
                break;
            case kStateCombat:
            {
                if (aiHas(c, kCondEnemyDead))
                    return aiBaseGetSchedule(c);
                // In the air from a jump: down, or still going.
                if (c.m.flag2)
                {
                    if (!c.m.inAir)
                    {
                        c.m.flag2 = false;
                        return scheduleOfType(c, kSchedJumpLand);
                    }
                    return scheduleOfType(c, kSchedJump);
                }
                if (aiHas(c, kCondHearSound))
                {
                    const AiSound *heard = aiBestSound(c);
                    if (heard != nullptr && (heard->type & kSoundDanger))
                        return scheduleOfType(c, kSchedTakeCoverFromBestSound);
                }
                if (aiHas(c, kCondLightDamage))
                    ++c.m.count1;
                if (aiHas(c, kCondHeavyDamage))
                    ++c.m.count1;
                if (aiHas(c, kCondCanMeleeAttack1))
                    return scheduleOfType(c, kSchedMeleeAttack1);
                if (aiHas(c, kCondCanRangeAttack2))
                    return scheduleOfType(c, kSchedRangeAttack2);
                if (aiHas(c, kCondSeeEnemy) && aiHas(c, kCondEnemyFacingMe))
                {
                    ++c.m.count1;
                    return scheduleOfType(c, kSchedExposed);
                }
                if (aiHas(c, kCondCanRangeAttack1))
                {
                    c.m.count1 = 0;
                    return scheduleOfType(c, kSchedRangeAttack1);
                }
                if (aiHas(c, kCondSeeEnemy))
                    return scheduleOfType(c, kSchedCombatFace);
                if (aiHas(c, kCondNewEnemy))
                    return scheduleOfType(c, kSchedTakeCoverFromEnemy);
                return scheduleOfType(c, kSchedAlertStand);
            }
            default:
                break;
            }
            return aiBaseGetSchedule(c);
        }

        /// CHAssassin::RunAI's cloak: on DIFFICULT, with an enemy, alive, on
        /// the ground and neither walking nor running, it fades -- 50 of 255
        /// a think -- to nearly nothing (20); otherwise back, as fast.
        inline void runAI(AiCtx &c)
        {
            const bool hides = skillLevelOf(c.w) == kSkillHard && c.m.enemy != kAiNone && c.me.ai < kAiDying &&
                               c.m.activity != kActRun && c.m.activity != kActWalk && !c.m.flag2;
            const int amount = 255 - c.me.cloak.value; // (pev->renderamt)
            const int wanted = hides ? 20 : 255;
            const int now = amount > wanted ? (amount - 50 > wanted ? amount - 50 : wanted)
                                            : (amount + 50 < wanted ? amount + 50 : wanted);
            c.me.cloak.value = static_cast<std::uint8_t>(255 - now);
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Assassin);
                k.health = kHealth;
                k.eye = fxFromInt(64); // (VEC_VIEW-ish: the human hull's)
                k.fov = fxDot(-0.7);  // (VIEW_FIELD_WIDE)
                k.blood = kBloodRed;
                k.capability = kCapMeleeAttack1 | kCapDoorsGroup;
                k.soundMask = kSoundWorld | kSoundCombat | kSoundDanger | kSoundPlayer;
                k.yawSpeed = yawSpeed;
                k.animEvent = animEvent;
                k.checkMelee1 = checkMelee1;
                k.checkRange1 = checkRange1;
                k.checkRange2 = checkRange2;
                k.startTask = startTask;
                k.runTask = runTask;
                k.scheduleOfType = scheduleOfType;
                k.getSchedule = getSchedule;
                k.runAI = runAI;
                return k;
            }();
            return k;
        }
    } // namespace assassin

    // ================================================================ apache

    namespace apache
    {
        inline constexpr int kHealth = 250;  ///< sk_apache_health at MEDIUM (aiInit: the game's level's)
        inline constexpr int kRockets = 10;
        inline constexpr std::uint8_t kActorWaitTrigger = kActorCeiling; ///< SF_WAITFORTRIGGER (0x04 | 0x40)
        // Its own think, HuntThink, ten times a second, and its flight a
        // frame at a time (MOVETYPE_FLY). m_posTarget, m_vecDesired,
        // m_posDesired, m_vecGoal: oldEnemyAt[0..3]; pev->avelocity: vec1
        // (degrees a second, Q12: pitch, yaw, roll); its pitch and roll
        // (65536 to the circle, nose up, as drawn): wantPitch, wantYaw;
        // m_angGun: aimYaw, aimPitch (its gun's bone controllers, the same
        // way); m_flForce: value1 (Q12); m_flGoalSpeed: value2 (Q12 units a
        // second); m_flNextRocket: time1; m_flLastSeen: time2;
        // m_flPrevSeen: time3; m_iRockets: count1; m_pGoalEnt: other (a
        // corner, or -1); hunting (HuntThink is its think): flag1; dying:
        // flag2; the side its next rocket is from: flag3.

        inline fx *posTarget(AiCtx &c) { return c.m.oldEnemyAt[0]; }
        inline fx *vecDesired(AiCtx &c) { return c.m.oldEnemyAt[1]; }
        inline fx *posDesired(AiCtx &c) { return c.m.oldEnemyAt[2]; }
        inline fx *vecGoal(AiCtx &c) { return c.m.oldEnemyAt[3]; }

        inline fx dot(const fx *a, const fx *b) { return fxMul(a[0], b[0]) + fxMul(a[1], b[1]) + fxMul(a[2], b[2]); }
        inline fx unit(fx *v)
        {
            const fx length = fxLength(v);
            for (int k = 0; length > 0 && k < 3; ++k)
                v[k] = static_cast<fx>((static_cast<std::int64_t>(v[k]) << kFxShift) / length);
            return length;
        }
        inline std::int32_t turnsOf(fx degrees) { return static_cast<std::int32_t>((static_cast<std::int64_t>(degrees) << 16) / (360 * kFxOne)); }
        /// (* 360 * 4096 >> 16 is * 45 >> 1, in 32 bits.)
        inline fx degreesOf(std::int32_t turns) { return (static_cast<std::int16_t>(turns) * 45) >> 1; }

        /// UTIL_MakeAimVectors of its angles (pitch nose up, yaw, roll, in
        /// degrees Q12, and as much again of each in `add`).
        inline void aimVectors(const AiCtx &c, const fx *add, fx *forward, fx *right, fx *up)
        {
            const fx pitch = degreesOf(c.me.wantPitch) + add[0];
            const fx yaw = degreesOf(c.me.yaw) + add[1];
            const fx roll = degreesOf(c.me.wantYaw) + add[2];
            fx sp, cp, sy, cy, sr, cr;
            // (AngleVectors, of the view's pitch: the model's, turned over.)
            fxSinCos(static_cast<std::uint16_t>(-turnsOf(pitch)), sp, cp);
            fxSinCos(static_cast<std::uint16_t>(turnsOf(yaw)), sy, cy);
            fxSinCos(static_cast<std::uint16_t>(turnsOf(roll)), sr, cr);
            // (Sines and cosines, and products of them: all 16-bit.)
            forward[0] = fxMulShort(cp, cy);
            forward[1] = fxMulShort(cp, sy);
            forward[2] = -sp;
            right[0] = -fxMulShort(fxMulShort(sr, sp), cy) + fxMulShort(cr, sy);
            right[1] = -fxMulShort(fxMulShort(sr, sp), sy) - fxMulShort(cr, cy);
            right[2] = -fxMulShort(sr, cp);
            up[0] = fxMulShort(fxMulShort(cr, sp), cy) + fxMulShort(sr, sy);
            up[1] = fxMulShort(fxMulShort(cr, sp), sy) - fxMulShort(sr, cy);
            up[2] = fxMulShort(cr, cp);
        }

        inline int firstCorner(const AiCtx &c)
        {
            if (c.a.records == nullptr || c.index >= c.a.recordCount)
                return -1;
            const int corner = static_cast<int>(c.a.records[c.index].item & 0xFFFF) - 1;
            return corner >= 0 && corner < c.w.cornerCount ? corner : -1;
        }
        /// Its goal at corner `corner`: there, and facing on along its path.
        inline void goalAt(AiCtx &c, int corner)
        {
            c.m.other = static_cast<std::int16_t>(corner);
            if (corner < 0)
                return;
            const PathCorner &at = c.w.corners[corner];
            for (int k = 0; k < 3; ++k)
                posDesired(c)[k] = at.pos[k];
            fx way[3] = {0, 0, 0};
            if (at.next != kNoCorner && at.next < c.w.cornerCount)
            {
                for (int k = 0; k < 2; ++k)
                    way[k] = c.w.corners[at.next].pos[k] - at.pos[k];
            }
            if (unit(way) == 0)
                aiForward(c.me.yaw, way);
            for (int k = 0; k < 3; ++k)
                vecGoal(c)[k] = way[k];
        }

        inline void rotor(AiCtx &c, bool on)
        {
            if (c.w.heard == nullptr)
                return;
            const auto tag = static_cast<std::uint16_t>(kSoundTagActor | c.index);
            if (on && static_cast<std::uint16_t>(CommonSound::ApacheRotor) < c.w.heard->commons)
                c.w.heard->add(static_cast<std::uint16_t>(CommonSound::ApacheRotor), c.me.pos, 127, 3000, kSoundLoopOn,
                               100, tag);
            else if (!on)
                c.w.heard->add(kNoSound, nullptr, 0, 0, kSoundLoopOff, 100, tag);
        }

        /// Flight: turned and tipped towards where it would be, lifted by its
        /// rotor against the world's pull.
        inline void flight(AiCtx &c)
        {
            fx *av = c.m.vec1;
            fx forward[3], right[3], up[3];
            const fx ahead[3] = {av[0] * 2 + fxFromInt(5), av[1] * 2, av[2] * 2};
            aimVectors(c, ahead, forward, right, up);
            if (dot(vecDesired(c), right) < 0)
            {
                if (av[1] < fxFromInt(60))
                    av[1] += fxFromInt(8);
            }
            else if (av[1] > -fxFromInt(60))
            {
                av[1] -= fxFromInt(8);
            }
            av[1] = fxMul(av[1], kFxOne * 98 / 100);
            const fx once[3] = {av[0] + fxFromInt(5), av[1], av[2]};
            aimVectors(c, once, forward, right, up);
            fx est[3];
            for (int k = 0; k < 3; ++k)
                est[k] = c.me.pos[k] + c.me.velocity[k] * 2 + fxMul(up[k], c.m.value1) * 20;
            est[2] -= fxFromInt(384 * 2);
            const fx tilt[3] = {fxFromInt(5), 0, 0};
            aimVectors(c, tilt, forward, right, up);
            for (int k = 0; k < 3; ++k)
                c.me.velocity[k] += fxMul(up[k], c.m.value1);
            c.me.velocity[2] -= fxDot(38.4);
            fx speed = fxLength(c.me.velocity);
            const fx flatForward[3] = {forward[0], forward[1], 0};
            const fx flatVelocity[3] = {c.me.velocity[0], c.me.velocity[1], 0};
            if (dot(flatForward, flatVelocity) < 0)
                speed = -speed;
            const fx off[3] = {posDesired(c)[0] - est[0], posDesired(c)[1] - est[1], posDesired(c)[2] - est[2]};
            const fx dist = dot(off, forward);
            const fx slip = -dot(off, right);
            const fx roll = degreesOf(c.me.wantYaw), pitch = degreesOf(c.me.wantPitch);
            if (slip > 0)
            {
                if (roll > -fxFromInt(30) && av[2] > -fxFromInt(15))
                    av[2] -= fxFromInt(4);
                else
                    av[2] += fxFromInt(2);
            }
            else
            {
                if (roll < fxFromInt(30) && av[2] < fxFromInt(15))
                    av[2] += fxFromInt(4);
                else
                    av[2] -= fxFromInt(2);
            }
            for (int k = 0; k < 3; ++k)
            {
                const fx side = right[k] < 0 ? -right[k] : right[k];
                c.me.velocity[k] = fxMul(c.me.velocity[k], kFxOne - side / 20);
                c.me.velocity[k] = fxMul(c.me.velocity[k], kFxOne * 995 / 1000);
            }
            if (c.m.value1 < fxFromInt(80) && est[2] < posDesired(c)[2])
                c.m.value1 += fxFromInt(12);
            else if (c.m.value1 > fxFromInt(30) && est[2] > posDesired(c)[2])
                c.m.value1 -= fxFromInt(8);
            // (Its pitch here is the game's angles.x, which is nose up.)
            const fx p = pitch + av[0];
            if (dist > 0 && speed < c.m.value2 && p > -fxFromInt(40))
                av[0] -= fxFromInt(12);
            else if (dist < 0 && speed > -fxFromInt(50) && p < fxFromInt(20))
                av[0] += fxFromInt(12);
            else if (p > 0)
                av[0] -= fxFromInt(4);
            else if (p < 0)
                av[0] += fxFromInt(4);
            rotor(c, true);
        }

        /// FireGun: its gun turned towards what it is after, twelve degrees
        /// a think either way, within its reach; a round, when it is on it.
        inline bool fireGun(AiCtx &c)
        {
            fx forward[3], right[3], up[3];
            const fx none[3] = {0, 0, 0};
            aimVectors(c, none, forward, right, up);
            fx gun[3];
            for (int k = 0; k < 3; ++k)
                gun[k] = c.me.pos[k] + forward[k] * 90 - up[k] * 50; // (under its nose)
            fx to[3] = {posTarget(c)[0] - gun[0], posTarget(c)[1] - gun[1], posTarget(c)[2] - gun[2]};
            unit(to);
            const fx out[3] = {dot(forward, to), -dot(right, to), dot(up, to)};
            const fx flat[3] = {out[0], out[1], 0};
            const int wantYaw = static_cast<std::int16_t>(fxAtan2Turns(out[1], out[0]));
            const int wantPitch = -static_cast<std::int16_t>(fxAtan2Turns(out[2], fxLength(flat)));
            const int step = 12 * 65536 / 360;
            auto toward = [&](int want, int now, int lo, int hi)
            {
                int v = want > now ? (want < now + step ? want : now + step) : (want > now - step ? want : now - step);
                return v < lo ? lo : (v > hi ? hi : v);
            };
            c.me.aimYaw = toward(wantYaw, static_cast<std::int16_t>(c.me.aimYaw), -90 * 65536 / 360, 90 * 65536 / 360);
            c.me.aimPitch = toward(wantPitch, static_cast<std::int16_t>(c.me.aimPitch), -10 * 65536 / 360, 45 * 65536 / 360);
            // Where it points now: its body's forward, turned by the gun.
            fx sy, cy, sp, cp;
            fxSinCos(static_cast<std::uint16_t>(c.me.aimYaw), sy, cy);
            fxSinCos(static_cast<std::uint16_t>(-c.me.aimPitch), sp, cp);
            fx barrel[3];
            for (int k = 0; k < 3; ++k)
                // (aimVectors' parts are at most 8192, its sums 12288: 16-bit.)
                barrel[k] = fxMulShort(fxMulShort(forward[k], cy) - fxMulShort(right[k], sy), cp) + fxMulShort(up[k], sp);
            unit(barrel);
            if (dot(barrel, to) <= fxDot(0.98))
                return false;
            // FireBullets, a 4 degree cone.
            fx aim[3];
            for (int k = 0; k < 3; ++k)
                aim[k] = barrel[k] + fxMul(right[k], fxFromInt(aiRandomLong(c.a, -35, 35)) / 1000) +
                         fxMul(up[k], fxFromInt(aiRandomLong(c.a, -35, 35)) / 1000);
            fx end[3];
            int group = kHitBody;
            const std::int16_t hit = aiTraceLine(c, gun, aim, 8192, end, &group);
            effectAdd(c.a, kEffectTracer, gun, end, 0, 60);
            effectAdd(c.a, kEffectMuzzle, gun, nullptr, 0, 50);
            if (c.w.heard != nullptr)
                c.w.heard->add(CommonSound::TurretFire, c.me.pos, 127, 3000);
            if (hit == kAiNone)
            {
                const fx far[3] = {gun[0] + aim[0] * 8192, gun[1] + aim[1] * 8192, gun[2] + aim[2] * 8192};
                markShot(c.a, *c.w.world, gun, far, false);
            }
            else
            {
                aiDamageTarget(c, hit, skillOf(c.w).monster12mmBullet, kDmgBullet | kDmgNeverGib,
                               static_cast<std::uint8_t>(group)); // (BULLET_MONSTER_12MM)
            }
            return true;
        }

        /// FireRocket: an HVR rocket from under one side or the other (the
        /// port's rocket: it flies as a tank's does).
        inline void fireRocket(AiCtx &c)
        {
            if (c.m.count1 <= 0)
                return;
            fx forward[3], right[3], up[3];
            const fx none[3] = {0, 0, 0};
            aimVectors(c, none, forward, right, up);
            const int side = c.m.flag3 ? -1 : 1;
            fx src[3], velocity[3];
            for (int k = 0; k < 3; ++k)
            {
                src[k] = c.me.pos[k] + (forward[k] * 21 + right[k] * 70 * side - up[k] * 79) * 3 / 2;
                velocity[k] = c.me.velocity[k] + forward[k] * 100;
            }
            projectileAdd(c.a, kShotRocketFoe, src, velocity, 30000);
            if (c.w.heard != nullptr)
                c.w.heard->add(CommonSound::RocketFire, src, 127, kRadiusLarge);
            --c.m.count1;
            c.m.flag3 = !c.m.flag3;
        }

        /// HuntThink.
        inline void hunt(AiCtx &c)
        {
            if (c.m.other < 0 && c.m.time1 == 0)
            {
                goalAt(c, firstCorner(c));
                c.m.time1 = 1; // (looked for, once)
            }
            // What it sees: the player, in its wide sweep, with nothing
            // between (Look, BestVisibleEnemy), within 4092.
            fx forward[3], right[3], up[3];
            const fx none[3] = {0, 0, 0};
            aimVectors(c, none, forward, right, up);
            bool sees = false;
            AiTarget t;
            if (c.player.health > 0 && !unseen(c.player) && aiTargetOf(c.w, c.player, kAiPlayer, t))
            {
                fx to[3] = {t.center[0] - c.me.pos[0], t.center[1] - c.me.pos[1], t.center[2] - c.me.pos[2]};
                const fx far = unit(to);
                sees = far < fxFromInt(4092) && dot(forward, to) > fxDot(-0.707) && lineClear(*c.w.world, c.me.pos, t.center);
            }
            c.m.enemy = sees ? kAiPlayer : kAiNone;
            c.m.state = sees ? kStateCombat : kStateAlert;
            if (c.m.value2 < fxFromInt(800))
                c.m.value2 += fxFromInt(5);
            if (sees)
            {
                if (c.m.time2 < c.ai.now - 5000)
                    c.m.time3 = c.ai.now;
                c.m.time2 = c.ai.now;
                for (int k = 0; k < 3; ++k)
                    posTarget(c)[k] = t.center[k];
            }
            fx target[3] = {posTarget(c)[0] - c.me.pos[0], posTarget(c)[1] - c.me.pos[1], posTarget(c)[2] - c.me.pos[2]};
            unit(target);
            fx off[3] = {c.me.pos[0] - posDesired(c)[0], c.me.pos[1] - posDesired(c)[1], c.me.pos[2] - posDesired(c)[2]};
            fx length = fxLength(off);
            if (c.m.other >= 0)
            {
                if (length < fxFromInt(128))
                {
                    const std::uint16_t next = c.w.corners[c.m.other].next;
                    if (next != kNoCorner && next < c.w.cornerCount)
                    {
                        goalAt(c, next);
                        for (int k = 0; k < 3; ++k)
                            off[k] = c.me.pos[k] - posDesired(c)[k];
                        length = fxLength(off);
                    }
                }
            }
            else
            {
                for (int k = 0; k < 3; ++k)
                    posDesired(c)[k] = c.me.pos[k];
                length = 0;
            }
            if (length > fxFromInt(250))
            {
                fx desired[3] = {posDesired(c)[0] - c.me.pos[0], posDesired(c)[1] - c.me.pos[1], posDesired(c)[2] - c.me.pos[2]};
                unit(desired);
                const bool chase = c.m.time2 + 90000 > c.ai.now && c.m.time2 != 0 && dot(target, desired) > fxDot(0.25);
                for (int k = 0; k < 3; ++k)
                    vecDesired(c)[k] = chase ? target[k] : desired[k];
            }
            else
            {
                for (int k = 0; k < 3; ++k)
                    vecDesired(c)[k] = vecGoal(c)[k];
            }
            flight(c);
            if (c.m.time2 + 1000 > c.ai.now && c.m.time3 + 2000 < c.ai.now && c.m.time2 != 0)
            {
                if (fireGun(c))
                {
                    if (c.m.value2 > fxFromInt(400))
                        c.m.value2 = fxFromInt(400);
                    // (On EASY it does not fire its rockets and its gun
                    // together: m_flNextRocket ten seconds on.)
                    if (skillLevelOf(c.w) == kSkillEasy)
                        c.m.time1 = c.ai.now + 10000;
                }
            }
            // Its rockets: in pairs (an odd count left fires the other at
            // once), when it is nosed down at what it is after.
            fx est[3];
            for (int k = 0; k < 3; ++k)
                est[k] = forward[k] * 800 + c.me.velocity[k];
            unit(est);
            if (c.m.count1 % 2 == 1)
            {
                fireRocket(c);
                c.m.time1 = c.ai.now + 500;
                if (c.m.count1 <= 0)
                {
                    c.m.time1 = c.ai.now + 10000;
                    c.m.count1 = kRockets;
                }
            }
            else if (static_cast<std::int16_t>(c.me.wantPitch) < 0 && dot(c.me.velocity, forward) > -fxFromInt(100) &&
                     c.m.time1 < c.ai.now && c.m.time2 + 60000 > c.ai.now && sees && dot(target, est) > fxDot(0.965))
            {
                const fx far[3] = {c.me.pos[0] + est[0] * 4096, c.me.pos[1] + est[1] * 4096, c.me.pos[2] + est[2] * 4096};
                const Shot line = traceShot(*c.w.world, c.me.pos, far);
                fx end[3];
                for (int k = 0; k < 3; ++k)
                    end[k] = c.me.pos[k] + static_cast<fx>((static_cast<std::int64_t>(far[k] - c.me.pos[k]) * line.fraction) >> kFractionShift);
                if (aiDist(end, posTarget(c)) < fxFromInt(512))
                    fireRocket(c);
            }
        }

        inline void think(AiCtx &c)
        {
            if (c.m.flag2)
                return; // (dying: its frame has it)
            if (!c.m.flag1)
            {
                if ((c.me.flags & kActorWaitTrigger) && c.me.switched == 0)
                    return;
                c.me.switched = 0;
                c.m.flag1 = true;
                c.m.count1 = kRockets;
                c.m.other = -1;
                c.m.time1 = 0;
            }
            hunt(c);
        }

        /// Each frame: where its velocity and its turning take it; off what
        /// it flies into (FlyTouch); dying, down burning, and at the ground
        /// (or in fifteen seconds) the end of it.
        inline void frame(AiCtx &c, int ms)
        {
            if (!c.me.active || !c.m.flag1)
                return;
            fx *av = c.m.vec1;
            if (c.m.flag2)
            {
                c.me.velocity[2] -= static_cast<fx>((static_cast<std::int64_t>(gravityOf(c.player) * 3 / 10) * ms) / 1000);
                for (int k = 0; k < 3; ++k)
                    av[k] += static_cast<fx>((static_cast<std::int64_t>(av[k]) * 2 * ms) / 10000); // (1.02 a tenth)
            }
            c.me.wantPitch = static_cast<std::uint16_t>(c.me.wantPitch + turnsOf(static_cast<fx>((static_cast<std::int64_t>(av[0]) * ms) / 1000)));
            c.me.yaw = static_cast<std::uint16_t>(c.me.yaw + turnsOf(static_cast<fx>((static_cast<std::int64_t>(av[1]) * ms) / 1000)));
            c.me.wantYaw = static_cast<std::uint16_t>(c.me.wantYaw + turnsOf(static_cast<fx>((static_cast<std::int64_t>(av[2]) * ms) / 1000)));
            c.m.idealYaw = c.me.yaw;
            fx to[3];
            for (int k = 0; k < 3; ++k)
                to[k] = c.me.pos[k] + static_cast<fx>((static_cast<std::int64_t>(c.me.velocity[k]) * ms) / 1000);
            fx at[3], normal[3];
            const bool met = traceShotWall(*c.w.world, c.me.pos, to, at, normal);
            if (!met)
            {
                for (int k = 0; k < 3; ++k)
                    c.me.pos[k] = to[k];
            }
            else if (!c.m.flag2)
            {
                const fx speed = fxLength(c.me.velocity) + fxFromInt(200);
                for (int k = 0; k < 3; ++k)
                    c.me.velocity[k] += fxMul(normal[k], speed);
            }
            actorPlaceBox(c.a, c.index);
            if (!c.m.flag2)
                return;
            // DyingThink: blasts about it, five a second, as it comes down.
            c.m.time3 -= ms;
            if (c.m.time3 <= 0 && !met && c.ai.now < c.m.time1)
            {
                c.m.time3 += 200;
                const fx spot[3] = {c.me.pos[0] + fxFromInt(aiRandomLong(c.a, -150, 150)),
                                    c.me.pos[1] + fxFromInt(aiRandomLong(c.a, -150, 150)),
                                    c.me.pos[2] + fxFromInt(aiRandomLong(c.a, -150, -50))};
                explodeAt(c.w, c.player, spot, aiRandomLong(c.a, 30, 59), true, false, false);
            }
            if (met || c.ai.now >= c.m.time1)
            {
                explodeAt(c.w, c.player, c.me.pos, 160, true, false, false);
                if (c.w.heard != nullptr)
                    c.w.heard->add(CommonSound::MortarHit, c.me.pos, 127, 3000);
                radiusDamage(c.w, c.player, c.me.pos, 300, 750, c.index);
                c.me.active = false;
                actorPlaceBox(c.a, c.index);
            }
        }

        /// TraceAttack and TakeDamage: what is not hard enough, and lands on
        /// neither of its two weak places, only sparks; a blast counts twice.
        inline bool traceAttack(AiCtx &c, AiDamage &d)
        {
            (void)c;
            const bool counts = d.amount > 50 || d.group == 1 || d.group == 2 || (d.type & kDmgBlast) != 0;
            if (d.type & kDmgBlast)
                d.amount *= 2;
            d.group = kHitGeneric;
            return counts;
        }

        inline bool killed(AiCtx &c)
        {
            rotor(c, false);
            c.me.ai = kAiDying;
            c.m.deadflag = 1;
            c.m.state = c.m.idealState = kStateDead;
            c.me.health = 0;
            c.m.flag2 = true;
            c.m.time1 = c.ai.now + ((c.a.records != nullptr && c.index < c.a.recordCount &&
                                     (c.a.records[c.index].flags & kActorAuto))
                                        ? 4000
                                        : 15000);
            c.m.time3 = 0;
            return true;
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Apache);
                k.health = kHealth;
                k.fov = fxDot(-0.707);
                k.blood = kBloodNone;
                k.think = think;
                k.frame = frame;
                k.traceAttack = traceAttack;
                k.killed = killed;
                return k;
            }();
            return k;
        }
    } // namespace apache

    inline const AiKind *aiMilitaryKindOf(std::uint8_t kind)
    {
        switch (static_cast<ActorKind>(kind))
        {
        case ActorKind::Osprey:   return &osprey::kind();
        case ActorKind::Assassin: return &assassin::kind();
        case ActorKind::Apache:   return &apache::kind();
        default:                  return nullptr;
        }
    }
} // namespace hldata
