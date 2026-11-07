#pragma once

// The monsters, each from its own file of the game's (dlls/headcrab.cpp,
// zombie.cpp, islave.cpp, bullsquid.cpp, houndeye.cpp, barnacle.cpp): what
// it does that CBaseMonster (HlAiCore.hpp) does not. The numbers are the
// game's at its middle difficulty (skill.cfg's "2"s), and the code is the
// code as it shipped -- a headcrab leaps again as soon as it lands, bullets
// never take a zombie's 30%, a barnacle kills whoever it pulls up.
//
// Included at the end of HlMonsters.hpp. std-only and inline: the host
// tests run it.

namespace hldata
{
    namespace detail
    {
        inline void aiSay(AiCtx &c, CommonSound sound, int pitch = 100)
        {
            monsterSound(c.w, c.me, sound, pitch);
        }
        /// ... at `volume` of 127.
        inline void aiSayAt(AiCtx &c, CommonSound sound, int volume)
        {
            if (c.w.heard != nullptr)
                c.w.heard->add(sound, c.me.pos, volume, kRadiusLarge);
        }
        /// ... heard only near by (ATTN_IDLE).
        inline void aiSayNear(AiCtx &c, CommonSound sound)
        {
            if (c.w.heard != nullptr)
                c.w.heard->add(sound, c.me.pos, 127, kRadiusSmall);
        }
        /// What a blow of a claw or a tail does to the player's view
        /// (punchangle) -- set, as the game's is.
        inline void aiPunch(AiCtx &c, std::int16_t hit, fx pitch, fx roll)
        {
            if (hit != kAiPlayer)
                return;
            c.ai.punch[0] = pitch;
            c.ai.punch[2] = roll;
        }
        /// ... and what it does to their going: `along` (units a second, in
        /// the world's axes) added.
        inline void aiShove(AiCtx &c, std::int16_t hit, const fx *along)
        {
            if (hit != kAiPlayer)
                return;
            for (int k = 0; k < 3; ++k)
                c.player.velocity[k] += along[k];
            if (along[2] > 0)
                c.player.onGround = false;
        }
        inline bool aiIsKind(const AiCtx &c, std::int16_t id, ActorKind kind)
        {
            return id >= 0 && id < c.a.count && c.a.states[id].kind == static_cast<std::uint8_t>(kind);
        }

        /// CGib::SpawnRandomGibs(pev, count, human): pieces of a person from
        /// its box (what a barnacle has eaten, coming up).
        inline void aiGibThrow(AiCtx &c, int count)
        {
            const int pieces = gibPieces(c.a, c.a.humanGibs);
            if (pieces == 0)
                return;
            fx mins[3], maxs[3];
            targetBox(c.me, mins, maxs);
            for (int n = 0; n < count; ++n)
            {
                fx at[3], velocity[3];
                for (int k = 0; k < 3; ++k)
                {
                    const fx span = maxs[k] - mins[k];
                    at[k] = mins[k] + static_cast<fx>((static_cast<std::int64_t>(span) * (aiRandom(c.a) & 255)) >> 8);
                    velocity[k] = fxFromInt(aiRandomLong(c.a, -100, 100)) * 7 / 10;
                }
                velocity[2] = fxFromInt(aiRandomLong(c.a, 200, 300)) * 7 / 10;
                Gib &gib = gibAdd(c.a, c.a.humanGibs, 1 + aiRandomLong(c.a, 0, pieces - 2 > 0 ? pieces - 2 : 0), at,
                                  velocity, (6000 + aiRandomLong(c.a, 0, 2000)) * c.a.lifePercent / 100);
                gib.yaw = static_cast<std::uint16_t>(aiRandom(c.a));
                gib.spin = static_cast<std::int16_t>(40 + aiRandomLong(c.a, 0, 59));
                effectAdd(c.a, kEffectBlood, at, nullptr, 0, 300);
            }
        }
    } // namespace detail

    // ================================================================ headcrab

    namespace headcrab
    {
        inline constexpr int kJumpAttack = 2; ///< HC_AE_JUMPATTACK

        inline constexpr AiTask tlRangeAttack1[] = {
            {kTaskStopMoving, 0},
            {kTaskFaceIdeal, 0},
            {kTaskRangeAttack1, 0},
            {kTaskSetActivity, kActIdle},
            {kTaskFaceIdeal, 0},
            {kTaskWaitRandom, 500},
        };
        inline constexpr AiSchedule slRangeAttack1 = {tlRangeAttack1, 6, kCondEnemyOccluded | kCondNoAmmoLoaded, 0,
                                                      "HCRangeAttack1"};

        inline int yawSpeed(AiCtx &c)
        {
            switch (c.m.activity)
            {
            case kActIdle: return 30;
            case kActRun:
            case kActWalk: return 20;
            case kActTurnLeft:
            case kActTurnRight: return 60;
            default: return 30;
            }
        }

        inline void sound(AiCtx &c, int which)
        {
            switch (which)
            {
            case kAiSoundAlert: detail::aiSayNear(c, CommonSound::HeadcrabAlert); break;
            case kAiSoundPain:  detail::aiSayNear(c, CommonSound::HeadcrabPain); break;
            case kAiSoundDeath: detail::aiSayNear(c, CommonSound::HeadcrabDie); break;
            default: break;
            }
        }

        inline void animEvent(AiCtx &c, int event)
        {
            if (event != kJumpAttack)
                return;
            fx jump[3];
            AiTarget t;
            if (aiTargetOf(c.w, c.player, c.m.enemy, t))
            {
                // Up fast enough to reach its eyes (16 at the least), and
                // across in the time that takes; no more than 650 in all.
                int gravity = fxToInt(gravityOf(c.player));
                if (gravity <= 1)
                    gravity = 1;
                int height = fxToInt(t.eye[2] - c.me.pos[2]);
                if (height < 16)
                    height = 16;
                const int speed = static_cast<int>(isqrt64(static_cast<std::uint64_t>(2) * gravity * height));
                for (int k = 0; k < 2; ++k)
                    jump[k] = static_cast<fx>((static_cast<std::int64_t>(t.eye[k] - c.me.pos[k]) * gravity) /
                                              (speed > 0 ? speed : 1));
                jump[2] = fxFromInt(speed);
                const fx length = fxLength(jump);
                if (length > fxFromInt(650))
                {
                    for (fx &v : jump)
                        v = static_cast<fx>((static_cast<std::int64_t>(v) * fxFromInt(650)) / length);
                }
            }
            else
            {
                fx forward[3];
                aiForward(c.me.yaw, forward);
                jump[0] = forward[0] * 350;
                jump[1] = forward[1] * 350;
                jump[2] = fxFromInt(350);
            }
            if (aiRandomLong(c.a, 0, 2) != 0)
                detail::aiSayNear(c, CommonSound::HeadcrabAttack);
            aiLeap(c, jump);
            c.m.time1 = c.ai.now + 2000; // m_flNextAttack (read by nothing)
        }

        /// LeapTouch: what it lands on is bitten, if it is still in the air.
        inline void touch(AiCtx &c, std::int16_t other)
        {
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, other, t) || t.cls == aiClassify(c))
                return;
            if (c.m.inAir)
            {
                detail::aiSayNear(c, CommonSound::HeadcrabBite);
                aiDamageTarget(c, other, skillOf(c.w).headcrabDmgBite, kDmgSlash);
            }
            c.m.touchArmed = false;
        }

        inline bool startTask(AiCtx &c, const AiTask &task)
        {
            if (task.task != kTaskRangeAttack1)
                return false;
            detail::aiSayNear(c, CommonSound::HeadcrabAttack);
            c.m.idealActivity = kActRangeAttack1;
            c.m.touchArmed = true;
            return true;
        }

        inline bool runTask(AiCtx &c, const AiTask &task)
        {
            if (task.task != kTaskRangeAttack1 && task.task != kTaskRangeAttack2)
                return false;
            if (c.m.seqFinished)
            {
                aiTaskComplete(c);
                c.m.touchArmed = false;
                c.m.idealActivity = kActIdle;
            }
            return true;
        }

        inline void prescheduleThink(AiCtx &c)
        {
            // (A fiftieth of a chance each think, in a fight: RANDOM_FLOAT(0,
            // 5) < 0.1.)
            if (c.m.state == kStateCombat && aiRandomLong(c.a, 0, 49) == 0)
                sound(c, kAiSoundIdle);
        }

        inline bool checkRange1(AiCtx &c, fx dot, fx dist)
        {
            return !c.m.inAir && dist <= fxFromInt(256) && dot >= fxDot(0.65);
        }
        inline bool checkRange2(AiCtx &, fx, fx) { return false; }

        inline bool takeDamage(AiCtx &, AiDamage &d)
        {
            if (d.type & kDmgAcid)
                d.amount = 0;
            return true;
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            if (type == kSchedRangeAttack1)
                return &slRangeAttack1;
            return aiBaseScheduleOfType(c, type);
        }

        inline constexpr AiClipName kClips[] = {
            {kActIdle, "idle1"},     {kActRun, "run"},           {kActWalk, "walk"},
            {kActRangeAttack1, "jump"}, {kActSmallFlinch, "flinch"}, {kActDieSimple, "dieback"},
            {kActReset, nullptr},
        };
        inline constexpr AiClipEvent kEvents[] = {{"jump", 0, kJumpAttack}, {nullptr, 0, 0}};

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Headcrab);
                k.health = 10;
                k.eye = fxFromInt(20);
                k.fov = fxDot(0.5);
                k.center = fxFromInt(6);
                k.yawSpeed = yawSpeed;
                k.sound = sound;
                k.animEvent = animEvent;
                k.touch = touch;
                k.startTask = startTask;
                k.runTask = runTask;
                k.prescheduleThink = prescheduleThink;
                k.checkRange1 = checkRange1;
                k.checkRange2 = checkRange2;
                k.takeDamage = takeDamage;
                k.scheduleOfType = scheduleOfType;
                k.clipNames = kClips;
                k.clipEvents = kEvents;
                return k;
            }();
            return k;
        }
    } // namespace headcrab

    // ================================================================== zombie

    namespace zombie
    {
        inline constexpr int kAttackRight = 1, kAttackLeft = 2, kAttackBoth = 3;
        inline constexpr int kFlinchDelay = 2000; ///< at most one flinch every two seconds

        inline int yawSpeed(AiCtx &) { return 120; }

        inline void sound(AiCtx &c, int which)
        {
            switch (which)
            {
            case kAiSoundAlert: detail::aiSay(c, CommonSound::ZombieAlert, aiRandomLong(c.a, 95, 104)); break;
            case kAiSoundPain:
                // (Two times in six.)
                if (aiRandomLong(c.a, 0, 5) < 2)
                    detail::aiSay(c, CommonSound::ZombiePain, aiRandomLong(c.a, 95, 104));
                break;
            default: break;
            }
        }

        inline void animEvent(AiCtx &c, int event)
        {
            if (event != kAttackRight && event != kAttackLeft && event != kAttackBoth)
                return;
            const bool both = event == kAttackBoth;
            const std::int16_t hit = aiHullAttack(c, 70, both ? skillOf(c.w).zombieDmgBothSlash : skillOf(c.w).zombieDmgOneSlash, kDmgSlash);
            if (hit != kAiNone)
            {
                fx right[3], forward[3];
                aiRight(c.me.yaw, right);
                aiForward(c.me.yaw, forward);
                if (event == kAttackRight)
                {
                    detail::aiPunch(c, hit, fxFromInt(5), -fxFromInt(18));
                    const fx shove[3] = {-right[0] * 100, -right[1] * 100, 0};
                    detail::aiShove(c, hit, shove);
                }
                else if (event == kAttackLeft)
                {
                    detail::aiPunch(c, hit, fxFromInt(5), fxFromInt(18));
                    const fx shove[3] = {right[0] * 100, right[1] * 100, 0};
                    detail::aiShove(c, hit, shove);
                }
                else
                {
                    detail::aiPunch(c, hit, fxFromInt(5), 0);
                    const fx shove[3] = {-forward[0] * 100, -forward[1] * 100, 0};
                    detail::aiShove(c, hit, shove);
                }
                detail::aiSay(c, CommonSound::ClawStrike, aiRandomLong(c.a, 95, 105));
            }
            else
            {
                detail::aiSay(c, CommonSound::ClawMiss, aiRandomLong(c.a, 95, 105));
            }
            if (aiRandomLong(c.a, 0, 1))
                detail::aiSay(c, CommonSound::ZombieAttack, aiRandomLong(c.a, 95, 104));
        }

        /// IgnoreConditions: in its swing, a blow is not felt until two
        /// seconds after its last flinch began. (The game tests the first
        /// melee attack twice: the second, never.)
        inline std::uint32_t ignoreConditions(AiCtx &c)
        {
            std::uint32_t ignore = aiBaseIgnoreConditions(c);
            if (c.m.activity == kActMeleeAttack1 && c.m.time1 >= c.ai.now)
                ignore |= kCondLightDamage | kCondHeavyDamage;
            if ((c.m.activity == kActSmallFlinch || c.m.activity == kActBigFlinch) && c.m.time1 < c.ai.now)
                c.m.time1 = c.ai.now + kFlinchDelay;
            return ignore;
        }

        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            // Only a blow that is a bullet and nothing more: shots come as
            // bullets that never tear apart, so this never is.
            if (d.type == kDmgBullet)
                d.amount = (d.amount * 3) / 10;
            if (aiAlive(c.a, c.index))
                sound(c, kAiSoundPain);
            return true;
        }

        inline bool checkRange(AiCtx &, fx, fx) { return false; }

        inline constexpr AiClipName kClips[] = {
            {kActIdle, "idle1"}, {kActWalk, "walk"}, {kActMeleeAttack1, "attack1"},
            {kActSmallFlinch, "flinchsmall"}, {kActDieSimple, "diesimple"}, {kActReset, nullptr},
        };
        inline constexpr AiClipEvent kEvents[] = {
            {"attack1", 10, kAttackRight}, {"attack1", 19, kAttackLeft}, {"attack2", 6, kAttackBoth}, {nullptr, 0, 0}};

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Zombie);
                k.health = 50;
                k.eye = fxFromInt(55);
                k.fov = fxDot(0.5);
                k.capability = kCapDoorsGroup;
                k.yawSpeed = yawSpeed;
                k.sound = sound;
                k.animEvent = animEvent;
                k.ignoreConditions = ignoreConditions;
                k.takeDamage = takeDamage;
                k.checkRange1 = checkRange;
                k.checkRange2 = checkRange;
                k.clipNames = kClips;
                k.clipEvents = kEvents;
                return k;
            }();
            return k;
        }
    } // namespace zombie

    // ============================================================ alien slave

    namespace slave
    {
        inline constexpr int kClaw = 1, kClawRake = 2, kZapPowerUp = 3, kZapShoot = 4, kZapDone = 5;
        /// Its own map flag (spawnflag 64): it goes for the player only once
        /// provoked (kept where a turret keeps its ceiling).
        inline constexpr std::uint8_t kActorWaitProvoked = kActorCeiling;

        inline constexpr AiTask tlSlaveAttack1[] = {
            {kTaskStopMoving, 0},
            {kTaskFaceIdeal, 0},
            {kTaskRangeAttack1, 0},
        };
        inline constexpr AiSchedule slSlaveAttack1 = {
            tlSlaveAttack1, 3, kCondCanMeleeAttack1 | kCondHearSound | kCondHeavyDamage, kSoundDanger,
            "Slave Range Attack1"};

        inline int relationship(AiCtx &c, std::int16_t target, int base)
        {
            if (target == kAiPlayer && (c.me.flags & kActorWaitProvoked) && !aiHasMemory(c, kMemoryProvoked))
                return kRelNone;
            return base;
        }

        /// CallForHelp: those of its name within `dist` are provoked, and
        /// have its enemy for theirs.
        inline void callForHelp(AiCtx &c, int dist)
        {
            const std::uint32_t name = aiSquadName(c.a, c.index);
            if (name == 0)
                return;
            for (int i = 0; i < c.a.count; ++i)
            {
                if (aiSquadName(c.a, i) != name || !c.a.states[i].active ||
                    aiDist(c.a.states[i].pos, c.me.pos) >= fxFromInt(dist))
                    continue;
                AiMonster *other = aiOf(c.a, i);
                const AiKind *kind = aiKindOf(c.a.states[i].kind);
                if (other == nullptr || kind == nullptr)
                    continue;
                other->memory |= kMemoryProvoked;
                AiCtx them{c.w, c.player, c.a, c.ai, i, c.a.states[i], *other, *kind};
                aiPushEnemy(them, c.m.enemy, c.m.enemyLkp);
            }
        }

        inline void sound(AiCtx &c, int which)
        {
            switch (which)
            {
            case kAiSoundAlert:
                if (c.m.enemy != kAiNone)
                {
                    detail::aiSay(c, CommonSound::SlaveAlert);
                    callForHelp(c, 512);
                }
                break;
            case kAiSoundPain:
                if (aiRandomLong(c.a, 0, 2) == 0)
                    detail::aiSay(c, CommonSound::SlavePain, c.m.talk.voicePitch);
                break;
            case kAiSoundDeath:
                detail::aiSay(c, CommonSound::SlaveDie, c.m.talk.voicePitch);
                break;
            default:
                break;
            }
        }

        inline int yawSpeed(AiCtx &c)
        {
            switch (c.m.activity)
            {
            case kActWalk: return 50;
            case kActRun:  return 70;
            case kActIdle: return 50;
            default:       return 90;
            }
        }

        /// Its hands, where the beams come from: either side of its chest.
        inline void hand(AiCtx &c, int side, fx *at)
        {
            fx forward[3], right[3];
            aiForward(c.me.yaw, forward);
            aiRight(c.me.yaw, right);
            for (int k = 0; k < 3; ++k)
                at[k] = c.me.pos[k] + right[k] * 16 * side + forward[k] * 32;
            at[2] = c.me.pos[2] + fxFromInt(36);
        }

        /// ArmBeam: the charge running over its arms to whatever is nearest
        /// of three tries, up to 512 away.
        inline void armBeam(AiCtx &c, int side)
        {
            fx src[3];
            hand(c, side, src);
            fx right[3];
            aiRight(c.me.yaw, right);
            fx best[3] = {0, 0, 0};
            std::int32_t bestFraction = kFractionOne;
            for (int i = 0; i < 3; ++i)
            {
                const int r = aiRandomLong(c.a, 0, 1000), u = aiRandomLong(c.a, -1000, 1000);
                fx to[3];
                for (int k = 0; k < 3; ++k)
                    to[k] = src[k] + static_cast<fx>((static_cast<std::int64_t>(right[k]) * side * r * 512) / 1000);
                to[2] += fxFromInt(512) * u / 1000;
                const Shot shot = traceShot(*c.w.world, src, to);
                if (shot.fraction < bestFraction)
                {
                    bestFraction = shot.fraction;
                    for (int k = 0; k < 3; ++k)
                        best[k] = src[k] + static_cast<fx>((static_cast<std::int64_t>(to[k] - src[k]) * shot.fraction) >>
                                                           kFractionShift);
                }
            }
            if (bestFraction < kFractionOne)
                effectAdd(c.a, kEffectBolt, src, best, 0, 300);
        }

        /// ZapBeam: from its hand at the enemy, a little off; what it hits
        /// is hurt. Who was hit, and how much, for the two beams to add up.
        inline std::int16_t zapBeam(AiCtx &c, int side)
        {
            const fx src[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(36)};
            fx aim[3];
            aiShootAtEnemy(c, src, aim);
            fx right[3];
            aiRight(c.me.yaw, right);
            const int across = aiRandomLong(c.a, 0, 41), up = aiRandomLong(c.a, -41, 41); // 0.01 of the way
            for (int k = 0; k < 3; ++k)
                aim[k] += static_cast<fx>((static_cast<std::int64_t>(right[k]) * side * across) >> kFxShift);
            aim[2] += up;
            fx end[3];
            const std::int16_t hit = aiTraceLine(c, src, aim, 1024, end);
            fx from[3];
            hand(c, side, from);
            effectAdd(c.a, kEffectBolt, from, end, 0, 300);
            return hit;
        }

        inline void animEvent(AiCtx &c, int event)
        {
            switch (event)
            {
            case kClaw:
            case kClawRake:
            {
                const std::int16_t hit = aiHullAttack(c, 70, event == kClaw ? skillOf(c.w).islaveDmgClaw : skillOf(c.w).islaveDmgClawrake, kDmgSlash);
                if (hit != kAiNone)
                {
                    detail::aiPunch(c, hit, fxFromInt(5), -fxFromInt(18));
                    detail::aiSay(c, CommonSound::ClawStrike, c.m.talk.voicePitch);
                }
                else
                {
                    detail::aiSay(c, CommonSound::ClawMiss, c.m.talk.voicePitch);
                }
                break;
            }
            case kZapPowerUp:
                // (On DIFFICULT the rest of the attack goes half again as
                // fast: ISLAVE_AE_ZAP_POWERUP's framerate 1.5.)
                if (skillLevelOf(c.w) == kSkillHard)
                    c.me.quickClip.value = static_cast<std::int16_t>(c.me.clip + 1);
                armBeam(c, -1);
                armBeam(c, 1);
                c.me.skin = static_cast<std::uint8_t>(c.m.count1 / 2 > 2 ? 2 : c.m.count1 / 2);
                c.m.count1 += 2;
                break;
            case kZapShoot:
            {
                c.m.count1 = 0;
                c.me.skin = 0;
                // (Both beams, and what they hit, hurt as one blow.)
                const std::int16_t first = zapBeam(c, -1);
                const std::int16_t second = zapBeam(c, 1);
                detail::aiSay(c, CommonSound::SlaveZap);
                if (first != kAiNone && first == second)
                {
                    aiDamageTarget(c, first, skillOf(c.w).islaveDmgZap * 2, kDmgShock);
                }
                else
                {
                    if (first != kAiNone)
                        aiDamageTarget(c, first, skillOf(c.w).islaveDmgZap, kDmgShock);
                    if (second != kAiNone)
                        aiDamageTarget(c, second, skillOf(c.w).islaveDmgZap, kDmgShock);
                }
                c.m.time1 = c.ai.now + aiRandomMs(c.a, 500, 4000); // m_flNextAttack
                break;
            }
            case kZapDone:
                c.m.count1 = 0;
                c.me.skin = 0;
                break;
            default:
                break;
            }
        }

        inline bool checkRange1(AiCtx &c, fx dot, fx dist)
        {
            if (c.m.time1 > c.ai.now)
                return false;
            return aiBaseCheckRange1(c, dot, dist);
        }
        /// (Bringing the dead back up was never finished: it is always no.)
        inline bool checkRange2(AiCtx &, fx, fx) { return false; }

        /// Its own voice, 85 to 110, for its cries and its blows (Spawn's
        /// m_voicePitch): from which it is, not a draw of the fight's.
        inline void started(AiCtx &c)
        {
            const std::uint32_t who = static_cast<std::uint32_t>(c.index + 1) * 2654435761u;
            c.m.talk.voicePitch = static_cast<std::uint8_t>(85 + (who >> 16) % 26);
        }

        inline void clearBeams(AiCtx &c)
        {
            c.m.count1 = 0;
            c.me.skin = 0;
        }

        inline bool startTask(AiCtx &c, const AiTask &)
        {
            clearBeams(c);
            return false;
        }

        inline bool traceAttack(AiCtx &, AiDamage &d)
        {
            return !(d.type & kDmgShock);
        }

        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            // (A slash from what it is not against does nothing: its own.)
            if ((d.type & kDmgSlash) && d.attacker != kAiNone && aiRelationship(c, d.attacker) < kRelDislike)
                return false;
            aiRemember(c, kMemoryProvoked);
            return true;
        }

        inline bool killed(AiCtx &c)
        {
            clearBeams(c);
            return false;
        }

        inline const AiSchedule *getSchedule(AiCtx &c)
        {
            clearBeams(c);
            if (aiHas(c, kCondHearSound))
            {
                if (const AiSound *heard = aiBestSound(c))
                {
                    if (heard->type & kSoundDanger)
                        return aiScheduleOfType(c, kSchedTakeCoverFromBestSound);
                    if (heard->type & kSoundCombat)
                        aiRemember(c, kMemoryProvoked);
                }
            }
            if (c.m.state == kStateCombat)
            {
                if (aiHas(c, kCondEnemyDead))
                    return aiBaseGetSchedule(c);
                // (m_iBravery never goes below nothing: health alone.)
                if (c.me.health < 20 && !aiHas(c, kCondCanMeleeAttack1))
                {
                    c.m.failSchedule = kSchedChaseEnemy;
                    if (aiHas(c, kCondLightDamage | kCondHeavyDamage))
                        return aiScheduleOfType(c, kSchedTakeCoverFromEnemy);
                    if (aiHas(c, kCondSeeEnemy) && aiHas(c, kCondEnemyFacingMe))
                        return aiScheduleOfType(c, kSchedTakeCoverFromEnemy);
                }
            }
            return aiBaseGetSchedule(c);
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            switch (type)
            {
            case kSchedFail:
                if (aiHas(c, kCondCanMeleeAttack1))
                    return aiSquadScheduleOfType(c, kSchedMeleeAttack1);
                break;
            case kSchedRangeAttack1:
            case kSchedRangeAttack2:
                return &slSlaveAttack1;
            default:
                break;
            }
            return aiSquadScheduleOfType(c, type);
        }

        inline constexpr AiClipName kClips[] = {
            {kActIdle, "idle1"},          {kActWalk, "walk1"},        {kActMeleeAttack1, "attack1"},
            {kActRangeAttack1, "zapattack1"}, {kActSmallFlinch, "flinch2"}, {kActDieSimple, "diesimple"},
            {kActReset, nullptr},
        };
        inline constexpr AiClipEvent kEvents[] = {
            {"attack1", 7, kClaw},          {"attack1", 13, kClaw},         {"attack1", 21, kClaw},
            {"zapattack1", 0, kZapPowerUp}, {"zapattack1", 4, kZapPowerUp}, {"zapattack1", 10, kZapPowerUp},
            {"zapattack1", 15, kZapPowerUp}, {"zapattack1", 24, kZapShoot}, {"zapattack1", 29, kZapDone},
            {nullptr, 0, 0}};

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Slave);
                k.health = 30;
                k.eye = fxFromInt(43);
                k.fov = fxDot(-0.7); // VIEW_FIELD_WIDE
                k.capability = kCapHear | kCapTurnHead | kCapRangeAttack2 | kCapDoorsGroup;
                k.soundMask = kSoundWorld | kSoundCombat | kSoundDanger | kSoundPlayer;
                k.squadMonster = true;
                k.relationship = relationship;
                k.sound = sound;
                k.yawSpeed = yawSpeed;
                k.animEvent = animEvent;
                k.checkRange1 = checkRange1;
                k.checkRange2 = checkRange2;
                k.startTask = startTask;
                k.traceAttack = traceAttack;
                k.takeDamage = takeDamage;
                k.killed = killed;
                k.getSchedule = getSchedule;
                k.scheduleOfType = scheduleOfType;
                k.scheduleChange = clearBeams;
                k.started = started;
                k.clipNames = kClips;
                k.clipEvents = kEvents;
                return k;
            }();
            return k;
        }
    } // namespace slave

    // ============================================================== bullsquid

    namespace bullsquid
    {
        inline constexpr int kSpit = 1, kBite = 2, kBlink = 3, kTailWhip = 4, kHop = 5, kThrow = 6;
        inline constexpr int kSprintDist = 256; ///< SQUID_SPRINT_DIST

        enum : std::uint8_t
        {
            kSchedHurtHop = kSchedLastCommon + 1,
            kSchedSmellFood,
            kSchedSeeCrab,
            kSchedEat,
            kSchedSniffAndEat,
            kSchedWallow
        };
        inline constexpr std::uint8_t kTaskHopTurn = kTaskLastCommon + 1;

        // m_flLastHurtTime, m_flNextSpitTime, m_fCanThreatDisplay: its time1,
        // time2, flag1 (flag1 set: it has already done the headcrab dance).

        inline constexpr AiTask tlRangeAttack1[] = {
            {kTaskStopMoving, 0},
            {kTaskFaceIdeal, 0},
            {kTaskRangeAttack1, 0},
            {kTaskSetActivity, kActIdle},
        };
        inline constexpr AiSchedule slRangeAttack1 = {
            tlRangeAttack1, 4,
            kCondNewEnemy | kCondEnemyDead | kCondHeavyDamage | kCondEnemyOccluded | kCondNoAmmoLoaded, 0,
            "Squid Range Attack1"};

        inline constexpr AiTask tlChaseEnemy1[] = {
            {kTaskSetFailSchedule, kSchedRangeAttack1}, // (stops the squid's swinging from one to the other)
            {kTaskGetPathToEnemy, 0},
            {kTaskRunPath, 0},
            {kTaskWaitForMovement, 0},
        };
        inline constexpr AiSchedule slChaseEnemy = {
            tlChaseEnemy1, 4,
            kCondNewEnemy | kCondEnemyDead | kCondSmellFood | kCondCanRangeAttack1 | kCondCanMeleeAttack1 |
                kCondCanMeleeAttack2 | kCondTaskFailed | kCondHearSound,
            kSoundDanger | kSoundMeat, "Squid Chase Enemy"};

        inline constexpr AiTask tlHurtHop[] = {
            {kTaskStopMoving, 0},
            {kTaskSoundWake, 0},
            {kTaskHopTurn, 0},
            {kTaskFaceEnemy, 0},
        };
        inline constexpr AiSchedule slHurtHop = {tlHurtHop, 4, 0, 0, "SquidHurtHop"};

        inline constexpr AiTask tlSeeCrab[] = {
            {kTaskStopMoving, 0},
            {kTaskSoundWake, 0},
            {kTaskPlaySequence, kActExcited},
            {kTaskFaceEnemy, 0},
        };
        inline constexpr AiSchedule slSeeCrab = {tlSeeCrab, 4, kCondLightDamage | kCondHeavyDamage, 0, "SquidSeeCrab"};

        inline constexpr AiTask tlEat[] = {
            {kTaskStopMoving, 0},        {kTaskEat, 10000},          {kTaskStoreLastPosition, 0},
            {kTaskGetPathToBestScent, 0}, {kTaskWalkPath, 0},        {kTaskWaitForMovement, 0},
            {kTaskPlaySequence, kActEat}, {kTaskPlaySequence, kActEat}, {kTaskPlaySequence, kActEat},
            {kTaskEat, 50000},           {kTaskGetPathToLastPosition, 0}, {kTaskWalkPath, 0},
            {kTaskWaitForMovement, 0},   {kTaskClearLastPosition, 0},
        };
        inline constexpr AiSchedule slEat = {tlEat, 14, kCondLightDamage | kCondHeavyDamage | kCondNewEnemy,
                                             kSoundMeat | kSoundCarcass, "SquidEat"};

        inline constexpr AiTask tlSniffAndEat[] = {
            {kTaskStopMoving, 0},             {kTaskEat, 10000},          {kTaskPlaySequence, kActDetectScent},
            {kTaskStoreLastPosition, 0},      {kTaskGetPathToBestScent, 0}, {kTaskWalkPath, 0},
            {kTaskWaitForMovement, 0},        {kTaskPlaySequence, kActEat}, {kTaskPlaySequence, kActEat},
            {kTaskPlaySequence, kActEat},     {kTaskEat, 50000},          {kTaskGetPathToLastPosition, 0},
            {kTaskWalkPath, 0},               {kTaskWaitForMovement, 0},  {kTaskClearLastPosition, 0},
        };
        inline constexpr AiSchedule slSniffAndEat = {tlSniffAndEat, 15,
                                                     kCondLightDamage | kCondHeavyDamage | kCondNewEnemy,
                                                     kSoundMeat | kSoundCarcass, "SquidSniffAndEat"};

        inline constexpr AiTask tlWallow[] = {
            {kTaskStopMoving, 0},             {kTaskEat, 10000},           {kTaskStoreLastPosition, 0},
            {kTaskGetPathToBestScent, 0},     {kTaskWalkPath, 0},          {kTaskWaitForMovement, 0},
            {kTaskPlaySequence, kActInspectFloor}, {kTaskEat, 50000},     {kTaskGetPathToLastPosition, 0},
            {kTaskWalkPath, 0},               {kTaskWaitForMovement, 0},   {kTaskClearLastPosition, 0},
        };
        inline constexpr AiSchedule slWallow = {tlWallow, 12, kCondLightDamage | kCondHeavyDamage | kCondNewEnemy,
                                                kSoundGarbage, "SquidWallow"};

        inline bool isHeadcrab(const AiCtx &c, std::int16_t id)
        {
            return detail::aiIsKind(c, id, ActorKind::Headcrab);
        }

        inline std::uint32_t ignoreConditions(AiCtx &c)
        {
            std::uint32_t ignore = aiBaseIgnoreConditions(c);
            // (Hurt in the last twenty seconds, or after a headcrab: no
            // nose for food. The game sets these over the base's, not with.)
            if (c.ai.now - c.m.time1 <= 20000)
                ignore = kCondSmell | kCondSmellFood;
            if (c.m.enemy != kAiNone && isHeadcrab(c, c.m.enemy))
                ignore = kCondSmell | kCondSmellFood;
            return ignore;
        }

        inline int relationship(AiCtx &c, std::int16_t target, int base)
        {
            // (Hurt in the last five seconds: no time for headcrabs.)
            if (c.ai.now - c.m.time1 < 5000 && isHeadcrab(c, target))
                return kRelNone;
            return base;
        }

        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            // Shot by what it is running at, from far enough: it swerves --
            // half way to where it is going, off to one side.
            if (c.m.enemy != kAiNone && aiIsMoving(c) && d.attacker == c.m.enemy && c.ai.now - c.m.time1 > 3000)
            {
                AiTarget t;
                if (aiTargetOf(c.w, c.player, c.m.enemy, t) && aiDist2D(c.me.pos, t.origin) > fxFromInt(kSprintDist) &&
                    c.me.hasGoal)
                {
                    fx right[3];
                    aiRight(c.me.yaw, right);
                    const fx half = aiDist2D(c.me.pos, c.me.goal) / 2;
                    const int side = aiRandomLong(c.a, 0, 1) ? 1 : -1;
                    fx apex[3];
                    for (int k = 0; k < 3; ++k)
                        apex[k] = (c.me.pos[k] + c.me.goal[k]) / 2 +
                                  static_cast<fx>((static_cast<std::int64_t>(right[k]) * half * side) >> kFxShift);
                    apex[2] = c.me.pos[2];
                    if (walkClear(*c.w.world, &c.a, c.index, aiHull(c), c.me.pos, apex, 32))
                    {
                        for (int k = 0; k < 3; ++k)
                            c.me.goal[k] = apex[k];
                        c.me.goalNode = c.me.fromNode = -1;
                        c.me.thinkMs = 700; // (until it plans again)
                    }
                }
            }
            if (!isHeadcrab(c, d.attacker))
                c.m.time1 = c.ai.now;
            return true;
        }

        inline bool checkRange1(AiCtx &c, fx dot, fx dist)
        {
            if (aiIsMoving(c) && dist >= fxFromInt(512))
                return false;
            if (dist > fxFromInt(64) && dist <= fxFromInt(784) && dot >= fxDot(0.5) && c.ai.now >= c.m.time2)
            {
                AiTarget t;
                if (aiTargetOf(c.w, c.player, c.m.enemy, t))
                {
                    const fx dz = c.me.pos[2] - t.origin[2];
                    if (dz > fxFromInt(256) || dz < -fxFromInt(256))
                        return false;
                }
                c.m.time2 = c.ai.now + (aiIsMoving(c) ? 5000 : 500);
                return true;
            }
            return false;
        }

        /// The whip: only for what has 35 or less left (a finishing blow).
        inline bool checkMelee1(AiCtx &c, fx dot, fx dist)
        {
            AiTarget t;
            return aiTargetOf(c.w, c.player, c.m.enemy, t) && t.health <= skillOf(c.w).bullsquidDmgWhip && dist <= fxFromInt(85) &&
                   dot >= fxDot(0.7);
        }
        /// The bite: anything else up close.
        inline bool checkMelee2(AiCtx &c, fx dot, fx dist)
        {
            return dist <= fxFromInt(85) && dot >= fxDot(0.7) && !aiHas(c, kCondCanMeleeAttack1);
        }

        inline void sound(AiCtx &c, int which)
        {
            switch (which)
            {
            case kAiSoundAlert: detail::aiSay(c, CommonSound::SquidAlert, aiRandomLong(c.a, 140, 160)); break;
            case kAiSoundPain:  detail::aiSay(c, CommonSound::SquidPain, aiRandomLong(c.a, 85, 120)); break;
            case kAiSoundDeath: detail::aiSay(c, CommonSound::SquidDie); break;
            default: break;
            }
        }

        inline int yawSpeed(AiCtx &) { return 90; }

        inline void animEvent(AiCtx &c, int event)
        {
            fx forward[3], right[3];
            aiForward(c.me.yaw, forward);
            aiRight(c.me.yaw, right);
            switch (event)
            {
            case kSpit:
            {
                AiTarget t;
                if (!aiTargetOf(c.w, c.player, c.m.enemy, t))
                    return;
                fx from[3];
                for (int k = 0; k < 3; ++k)
                    from[k] = c.me.pos[k] + right[k] * 8 + forward[k] * 37;
                from[2] = c.me.pos[2] + fxFromInt(23);
                fx dir[3] = {t.eye[0] - from[0], t.eye[1] - from[1], t.eye[2] - from[2]};
                const fx length = fxLength(dir);
                for (fx &v : dir)
                    v = length > 0 ? static_cast<fx>((static_cast<std::int64_t>(v) << kFxShift) / length) : 0;
                // (Not quite true: up to a twentieth off either way, and
                // only ever down.)
                dir[0] += aiRandomLong(c.a, -205, 205);
                dir[1] += aiRandomLong(c.a, -205, 205);
                dir[2] += aiRandomLong(c.a, -205, 0);
                detail::aiSay(c, CommonSound::SquidSpit);
                const fx velocity[3] = {dir[0] * 900, dir[1] * 900, dir[2] * 900};
                if (Projectile *spit = projectileAdd(c.a, kShotSpit, from, velocity, 5000))
                    spit->sprite = static_cast<std::uint16_t>(c.index);
                break;
            }
            case kBite:
            {
                const std::int16_t hit = aiHullAttack(c, 70, skillOf(c.w).bullsquidDmgBite, kDmgSlash);
                detail::aiSay(c, CommonSound::SquidBite, aiRandomLong(c.a, 90, 110));
                const fx shove[3] = {-forward[0] * 100, -forward[1] * 100, fxFromInt(100)};
                if (hit != kAiNone)
                    detail::aiShove(c, hit, shove);
                break;
            }
            case kTailWhip:
            {
                const std::int16_t hit = aiHullAttack(c, 70, skillOf(c.w).bullsquidDmgWhip, kDmgClub | kDmgAlwaysGib);
                if (hit != kAiNone)
                {
                    detail::aiPunch(c, hit, fxFromInt(20), -fxFromInt(20));
                    const fx shove[3] = {right[0] * 200, right[1] * 200, fxFromInt(100)};
                    detail::aiShove(c, hit, shove);
                }
                break;
            }
            case kBlink:
                c.me.skin = 1;
                break;
            case kHop:
            {
                // (Up by 0.625 of the world's pull, halved.)
                const fx up = static_cast<fx>((static_cast<std::int64_t>(gravityOf(c.player)) * 625) / 2000);
                const fx hop[3] = {c.me.velocity[0], c.me.velocity[1], c.me.velocity[2] + up};
                aiLeap(c, hop);
                break;
            }
            case kThrow:
            {
                const std::int16_t hit = aiHullAttack(c, 70, 0, 0);
                if (hit != kAiNone)
                {
                    shakeStart(c.w, fxFromInt(25), 1500);
                    if (hit == kAiPlayer)
                    {
                        const fx shove[3] = {forward[0] * 300, forward[1] * 300, fxFromInt(300)};
                        detail::aiShove(c, hit, shove);
                    }
                }
                break;
            }
            default:
                break;
            }
        }

        inline void runAI(AiCtx &c)
        {
            // Its eye: open, and now and then (one think in forty) shut.
            if (c.me.skin != 0)
                c.me.skin = 0;
            if (aiRandomLong(c.a, 0, 39) == 0)
                c.me.skin = 1;
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            switch (type)
            {
            case kSchedRangeAttack1: return &slRangeAttack1;
            case kSchedHurtHop: return &slHurtHop;
            case kSchedSeeCrab: return &slSeeCrab;
            case kSchedEat: return &slEat;
            case kSchedSniffAndEat: return &slSniffAndEat;
            case kSchedWallow: return &slWallow;
            case kSchedChaseEnemy: return &slChaseEnemy;
            default: return aiBaseScheduleOfType(c, type);
            }
        }

        /// Food smelt: sniffed out if it is not in sight, else gone for.
        inline const AiSchedule *eatSchedule(AiCtx &c)
        {
            const AiSound *scent = aiBestScent(c);
            if (scent != nullptr && (!aiInViewCone(c, scent->origin) || !aiVisible(c, scent->origin)))
                return aiScheduleOfType(c, kSchedSniffAndEat);
            return aiScheduleOfType(c, kSchedEat);
        }

        inline const AiSchedule *getSchedule(AiCtx &c)
        {
            switch (c.m.state)
            {
            case kStateAlert:
                if (aiHas(c, kCondLightDamage | kCondHeavyDamage))
                    return aiScheduleOfType(c, kSchedHurtHop);
                if (aiHas(c, kCondSmellFood))
                    return eatSchedule(c);
                if (aiHas(c, kCondSmell) && aiBestScent(c) != nullptr)
                    return aiScheduleOfType(c, kSchedWallow);
                break;
            case kStateCombat:
                if (aiHas(c, kCondEnemyDead))
                    return aiBaseGetSchedule(c);
                if (aiHas(c, kCondNewEnemy))
                {
                    if (!c.m.flag1 && aiRelationship(c, c.m.enemy) == kRelHate)
                    {
                        c.m.flag1 = true; // (the headcrab dance: once in its life)
                        return aiScheduleOfType(c, kSchedSeeCrab);
                    }
                    return aiScheduleOfType(c, kSchedWakeAngry);
                }
                if (aiHas(c, kCondSmellFood))
                    return eatSchedule(c);
                if (aiHas(c, kCondCanRangeAttack1))
                    return aiScheduleOfType(c, kSchedRangeAttack1);
                if (aiHas(c, kCondCanMeleeAttack1))
                    return aiScheduleOfType(c, kSchedMeleeAttack1);
                if (aiHas(c, kCondCanMeleeAttack2))
                    return aiScheduleOfType(c, kSchedMeleeAttack2);
                return aiScheduleOfType(c, kSchedChaseEnemy);
            default:
                break;
            }
            return aiBaseGetSchedule(c);
        }

        inline bool startTask(AiCtx &c, const AiTask &task)
        {
            switch (task.task)
            {
            case kTaskMeleeAttack2:
                detail::aiSay(c, CommonSound::SquidAlert); // (its growl, before the bite)
                return false;
            case kTaskHopTurn:
                aiSetActivity(c, kActHop);
                aiMakeIdealYaw(c, c.m.enemyLkp);
                return true;
            case kTaskGetPathToEnemy:
            {
                // (Straight at it, or not at all: no nearest route.)
                AiTarget t;
                if (!aiTargetOf(c.w, c.player, c.m.enemy, t))
                {
                    aiTaskFail(c);
                    return true;
                }
                fx feet[3] = {t.origin[0], t.origin[1], t.origin[2]};
                if (t.isPlayer)
                    feet[2] -= t.height / 2;
                if (aiBuildRoute(c, kMoveGoalEnemy, feet))
                    c.m.taskStatus = kTaskStatusComplete;
                else
                    aiTaskFail(c);
                return true;
            }
            default:
                return false;
            }
        }

        inline bool runTask(AiCtx &c, const AiTask &task)
        {
            if (task.task != kTaskHopTurn)
                return false;
            aiMakeIdealYaw(c, c.m.enemyLkp);
            aiChangeYaw(c);
            if (c.m.seqFinished)
                c.m.taskStatus = kTaskStatusComplete;
            return true;
        }

        inline std::uint8_t idealState(AiCtx &c)
        {
            const std::uint32_t conditions = aiScheduleFlags(c);
            // Hurt while after a headcrab: it forgets the crab.
            if (c.m.state == kStateCombat && c.m.enemy != kAiNone &&
                (conditions & (kCondLightDamage | kCondHeavyDamage)) && isHeadcrab(c, c.m.enemy))
            {
                c.m.enemy = kAiNone;
                c.m.idealState = kStateAlert;
            }
            return aiBaseIdealState(c);
        }

        inline constexpr AiClipName kClips[] = {
            {kActIdle, "idle"},          {kActRun, "run"},             {kActRangeAttack1, "range"},
            {kActMeleeAttack2, "bite"},  {kActMeleeAttack1, "whip"},   {kActSmallFlinch, "flinchs"},
            {kActDieSimple, "die1"},     {kActReset, nullptr},
        };
        inline constexpr AiClipEvent kEvents[] = {
            {"range", 11, kSpit}, {"bite", 7, kBite}, {"bite", 10, kThrow}, {"whip", 23, kTailWhip}, {nullptr, 0, 0}};

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Bullsquid);
                k.health = 40;
                k.eye = fxFromInt(28);
                k.fov = fxDot(0.2);
                k.soundMask = kSoundWorld | kSoundCombat | kSoundCarcass | kSoundMeat | kSoundGarbage | kSoundPlayer;
                k.ignoreConditions = ignoreConditions;
                k.relationship = relationship;
                k.takeDamage = takeDamage;
                k.checkRange1 = checkRange1;
                k.checkMelee1 = checkMelee1;
                k.checkMelee2 = checkMelee2;
                k.sound = sound;
                k.yawSpeed = yawSpeed;
                k.animEvent = animEvent;
                k.runAI = runAI;
                k.getSchedule = getSchedule;
                k.scheduleOfType = scheduleOfType;
                k.startTask = startTask;
                k.runTask = runTask;
                k.idealState = idealState;
                k.clipNames = kClips;
                k.clipEvents = kEvents;
                return k;
            }();
            return k;
        }
    } // namespace bullsquid

    // =============================================================== houndeye

    namespace houndeye
    {
        inline constexpr int kMaxSquadSize = 4;
        inline constexpr int kMaxAttackRadius = 384;
        inline constexpr int kSquadBonus = 110; ///< HOUNDEYE_SQUAD_BONUS, in hundredths
        inline constexpr int kEyeFrames = 3;
        inline constexpr int kStartleVolume = 128;

        inline constexpr int kWarn = 1, kStartAttack = 2, kThump = 3, kAngerSound1 = 4, kAngerSound2 = 5,
                             kHopBack = 6, kCloseEye = 7;

        enum : std::uint8_t
        {
            kTaskCloseEye = kTaskLastCommon + 1,
            kTaskOpenEye,
            kTaskThreatDisplay,
            kTaskFallAsleep,
            kTaskWakeUp,
            kTaskHopBack
        };
        enum : std::uint8_t
        {
            kSchedAgitated = kSchedLastCommon + 1,
            kSchedHopRetreat,
            kSchedHoundFail
        };

        // m_fAsleep, m_fDontBlink, m_vecPackCenter: its flag1, flag2, vec1.

        inline constexpr AiTask tlGuardPack[] = {
            {kTaskStopMoving, 0},
            {kTaskGuard, 0},
        };
        inline constexpr AiSchedule slGuardPack = {
            tlGuardPack, 2, kCondSeeHate | kCondLightDamage | kCondHeavyDamage | kCondProvoked | kCondHearSound,
            kSoundCombat | kSoundWorld | kSoundMeat | kSoundPlayer, "GuardPack"};

        inline constexpr AiTask tlYell1[] = {
            {kTaskStopMoving, 0},
            {kTaskFaceIdeal, 0},
            {kTaskRangeAttack1, 0},
            {kTaskSetSchedule, kSchedAgitated},
        };
        inline constexpr AiSchedule slRangeAttack = {tlYell1, 4, kCondLightDamage | kCondHeavyDamage, 0,
                                                     "HoundRangeAttack1"};

        inline constexpr AiTask tlSleep[] = {
            {kTaskStopMoving, 0},          {kTaskSetActivity, kActIdle},       {kTaskWaitRandom, 5000},
            {kTaskPlaySequence, kActCrouch}, {kTaskSetActivity, kActCrouchIdle}, {kTaskFallAsleep, 0},
            {kTaskWaitRandom, 25000},      {kTaskCloseEye, 0},                 {kTaskWaitIndefinite, 0},
        };
        inline constexpr AiSchedule slSleep = {tlSleep, 9,
                                               kCondHearSound | kCondLightDamage | kCondHeavyDamage | kCondNewEnemy,
                                               kSoundCombat | kSoundPlayer | kSoundWorld, "Hound Sleep"};

        inline constexpr AiTask tlWakeLazy[] = {
            {kTaskStopMoving, 0},  {kTaskOpenEye, 0}, {kTaskWaitRandom, 2500},
            {kTaskPlaySequence, kActStand}, {kTaskWakeUp, 0},
        };
        inline constexpr AiSchedule slWakeLazy = {tlWakeLazy, 5, 0, 0, "WakeLazy"};

        inline constexpr AiTask tlWakeUrgent[] = {
            {kTaskOpenEye, 0},
            {kTaskPlaySequence, kActHop},
            {kTaskFaceIdeal, 0},
            {kTaskWakeUp, 0},
        };
        inline constexpr AiSchedule slWakeUrgent = {tlWakeUrgent, 4, 0, 0, "WakeUrgent"};

        inline constexpr AiTask tlSpecialAttack1[] = {
            {kTaskStopMoving, 0},
            {kTaskFaceIdeal, 0},
            {kTaskSpecialAttack1, 0},
            {kTaskPlaySequence, kActIdleAngry},
        };
        inline constexpr AiSchedule slSpecialAttack1 = {
            tlSpecialAttack1, 4, kCondNewEnemy | kCondLightDamage | kCondHeavyDamage | kCondEnemyOccluded, 0,
            "Hound Special Attack1"};

        inline constexpr AiTask tlAgitated[] = {
            {kTaskStopMoving, 0},
            {kTaskThreatDisplay, 0},
        };
        inline constexpr AiSchedule slAgitated = {tlAgitated, 2, kCondNewEnemy | kCondLightDamage | kCondHeavyDamage,
                                                  0, "Hound Agitated"};

        inline constexpr AiTask tlHopRetreat[] = {
            {kTaskStopMoving, 0},
            {kTaskHopBack, 0},
            {kTaskSetSchedule, kSchedTakeCoverFromEnemy},
        };
        inline constexpr AiSchedule slHopRetreat = {tlHopRetreat, 3, 0, 0, "Hound Hop Retreat"};

        inline constexpr AiTask tlCombatFailPvs[] = {
            {kTaskStopMoving, 0},
            {kTaskThreatDisplay, 0},
            {kTaskWaitFaceEnemy, 1000},
        };
        inline constexpr AiSchedule slCombatFailPvs = {
            tlCombatFailPvs, 3, kCondNewEnemy | kCondLightDamage | kCondHeavyDamage, 0, "HoundCombatFailPVS"};

        inline constexpr AiTask tlCombatFailNoPvs[] = {
            {kTaskStopMoving, 0},     {kTaskThreatDisplay, 0},       {kTaskWaitFaceEnemy, 2000},
            {kTaskSetActivity, kActIdle}, {kTaskWaitPvs, 0},
        };
        inline constexpr AiSchedule slCombatFailNoPvs = {
            tlCombatFailNoPvs, 5, kCondNewEnemy | kCondLightDamage | kCondHeavyDamage, 0, "HoundCombatFailNoPVS"};

        inline bool canActiveIdle(AiCtx &)
        {
            // (No squad member is at a hint node: there are none.)
            return true;
        }

        inline bool checkRange1(AiCtx &, fx dot, fx dist)
        {
            return dist <= fxFromInt(kMaxAttackRadius / 2) && dot >= fxDot(0.3);
        }

        inline int yawSpeed(AiCtx &c)
        {
            switch (c.m.activity)
            {
            case kActCrouchIdle: return 0; // (asleep)
            case kActIdle:       return 60;
            default:             return 90;
            }
        }

        /// SetActivity: in a fight, half its idling is its mad idle.
        inline void setActivity(AiCtx &c, std::uint8_t activity)
        {
            if (activity == c.m.activity)
                return;
            if (c.m.state == kStateCombat && activity == kActIdle && aiRandomLong(c.a, 0, 1))
            {
                c.m.activity = activity;
                c.m.idealActivity = activity;
                const ModelView *model = actorModel(c.a, c.me);
                const int clip = model != nullptr ? model->findClip(nameHash("madidle")) : -1;
                if (clip >= 0)
                {
                    aiPlayClip(c, clip, true);
                    aiSetYawSpeed(c);
                }
                return;
            }
            aiSetActivityBase(c, activity);
        }

        inline void sound(AiCtx &c, int which)
        {
            switch (which)
            {
            case kAiSoundAlert:
                // (Only the leader of a pack.)
                if (aiInSquad(c) && !aiIsLeader(c))
                    return;
                detail::aiSay(c, CommonSound::HoundAlert);
                break;
            case kAiSoundPain:  detail::aiSay(c, CommonSound::HoundPain); break;
            case kAiSoundDeath: detail::aiSay(c, CommonSound::HoundDie); break;
            default: break;
            }
        }

        /// SonicAttack: everything within 384 that can be hurt, but other
        /// houndeyes -- the more of the pack, the harder; less the further
        /// off; half for a player out of sight, nothing for anything else.
        inline void sonicAttack(AiCtx &c)
        {
            detail::aiSay(c, CommonSound::HoundBlast);
            const fx middle[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(16)};
            effectAdd(c.a, kEffectRing, middle, nullptr, kMaxAttackRadius, 300);
            const int squad = aiSquadCount(c);
            const int blast = skillOf(c.w).houndeyeDmgBlast;
            const int full = squad > 1 ? blast + (blast * kSquadBonus * (squad - 1)) / 100 : blast;
            auto hurt = [&](std::int16_t id, const fx *center)
            {
                const int dist = fxToInt(aiDist(center, c.me.pos));
                if (dist >= kMaxAttackRadius)
                    return;
                int damage = full - (dist * full) / kMaxAttackRadius;
                AiTarget t;
                if (!aiTargetOf(c.w, c.player, id, t))
                    return;
                if (!aiVisible(c, t.eye))
                    damage = t.isPlayer ? damage / 2 : 0;
                if (damage > 0)
                    aiDamageTarget(c, id, damage, kDmgSonic | kDmgAlwaysGib);
            };
            if (c.player.health > 0)
                hurt(kAiPlayer, c.player.origin);
            for (int i = 0; i < c.a.count; ++i)
            {
                const ActorState &other = c.a.states[i];
                if (i == c.index || !other.active || !canBeHurt(other.kind) ||
                    other.kind == static_cast<std::uint8_t>(ActorKind::Houndeye))
                    continue;
                fx lo[3], hi[3];
                targetBox(other, lo, hi);
                const fx center[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
                hurt(static_cast<std::int16_t>(i), center);
            }
            // (And what breaks: seen or not.)
            for (int r = 0; r < c.w.recordCount; ++r)
            {
                const LogicRecord &record = c.w.records[r];
                if (c.w.states[r].dead || record.brush == kNoBrush || !detail::takesBlows(record))
                    continue;
                const MoverHull &hull = c.w.movers[record.brush];
                const fx center[3] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2,
                                      (hull.mins[2] + hull.maxs[2]) / 2};
                const int dist = fxToInt(aiDist(center, c.me.pos));
                if (dist < kMaxAttackRadius)
                    logicStrike(c.w, c.player, r, full - (dist * full) / kMaxAttackRadius, kDmgSonic);
            }
        }

        inline void animEvent(AiCtx &c, int event)
        {
            switch (event)
            {
            case kWarn:
            case kStartAttack:
                detail::aiSayAt(c, CommonSound::HoundAttack, 89); // (0.7: WarmUpSound)
                break;
            case kHopBack:
            {
                fx forward[3];
                aiForward(c.me.yaw, forward);
                const fx up = static_cast<fx>((static_cast<std::int64_t>(gravityOf(c.player)) * 6) / 20);
                const fx hop[3] = {forward[0] * -200, forward[1] * -200, c.me.velocity[2] + up};
                aiLeap(c, hop);
                break;
            }
            case kThump:
                sonicAttack(c);
                break;
            case kCloseEye:
                if (!c.m.flag2)
                    c.me.skin = kEyeFrames - 1;
                break;
            default:
                break;
            }
        }

        inline bool startTask(AiCtx &c, const AiTask &task)
        {
            c.m.taskStatus = kTaskStatusRunning;
            switch (task.task)
            {
            case kTaskFallAsleep:
                c.m.flag1 = true;
                c.m.taskStatus = kTaskStatusComplete;
                return true;
            case kTaskWakeUp:
                c.m.flag1 = false;
                c.m.taskStatus = kTaskStatusComplete;
                return true;
            case kTaskOpenEye:
                c.m.flag2 = false;
                c.m.taskStatus = kTaskStatusComplete;
                return true;
            case kTaskCloseEye:
                c.me.skin = 0;
                c.m.flag2 = true;
                return true;
            case kTaskThreatDisplay:
                c.m.idealActivity = kActIdleAngry;
                return true;
            case kTaskHopBack:
                c.m.idealActivity = kActLeap;
                return true;
            case kTaskRangeAttack1:
                c.m.idealActivity = kActRangeAttack1;
                return true;
            case kTaskSpecialAttack1:
                c.m.idealActivity = kActSpecialAttack1;
                return true;
            case kTaskGuard:
                c.m.idealActivity = kActGuard;
                return true;
            default:
                return false;
            }
        }

        inline bool runTask(AiCtx &c, const AiTask &task)
        {
            switch (task.task)
            {
            case kTaskThreatDisplay:
                aiMakeIdealYaw(c, c.m.enemyLkp);
                aiChangeYaw(c);
                if (c.m.seqFinished)
                    aiTaskComplete(c);
                return true;
            case kTaskCloseEye:
                if (c.me.skin < kEyeFrames - 1)
                    ++c.me.skin;
                if (c.me.skin == kEyeFrames - 1)
                    aiTaskComplete(c);
                return true;
            case kTaskHopBack:
                if (c.m.seqFinished)
                    aiTaskComplete(c);
                return true;
            case kTaskSpecialAttack1:
                c.me.skin = static_cast<std::uint8_t>(aiRandomLong(c.a, 0, kEyeFrames - 1));
                aiMakeIdealYaw(c, c.m.enemyLkp);
                aiChangeYaw(c);
                if (c.m.seqFinished)
                {
                    sonicAttack(c);
                    aiTaskComplete(c);
                }
                return true;
            default:
                return false;
            }
        }

        inline void prescheduleThink(AiCtx &c)
        {
            // (A warning, a fifth of the time, running into a fight.)
            if (c.m.state == kStateCombat && c.m.activity == kActRun && aiRandomLong(c.a, 0, 4) == 0)
                detail::aiSayAt(c, CommonSound::HoundAttack, 89); // (0.7: WarmUpSound)
            // Its eye blinks, unless something has it shut.
            if (!c.m.flag2)
            {
                if (c.me.skin == 0 && aiRandomLong(c.a, 0, 0x7F) == 0)
                    c.me.skin = kEyeFrames - 1;
                else if (c.me.skin != 0)
                    --c.me.skin;
            }
            // The leader keeps the middle of the pack (adding to what it had,
            // as the game does, before it divides).
            if (aiIsLeader(c))
            {
                int count = 0;
                for (int i = 0; i < kMaxSquadMembers; ++i)
                {
                    const std::int16_t member = aiSquadMember(c, i);
                    if (member == kAiNone)
                        continue;
                    ++count;
                    for (int k = 0; k < 3; ++k)
                        c.m.vec1[k] += c.a.states[member].pos[k];
                }
                for (int k = 0; count > 0 && k < 3; ++k)
                    c.m.vec1[k] /= count;
            }
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            if (c.m.flag1)
            {
                // Asleep: woken in a hurry by something loud enough or an
                // enemy, else lazily.
                if (aiHas(c, kCondHearSound))
                {
                    if (const AiSound *wake = aiBestSound(c))
                    {
                        aiMakeIdealYaw(c, wake->origin);
                        if (aiSoundVolume(c, *wake) >= kStartleVolume)
                            return &slWakeUrgent;
                    }
                    return &slWakeLazy;
                }
                if (aiHas(c, kCondNewEnemy))
                    return &slWakeUrgent;
                return &slWakeLazy;
            }
            switch (type)
            {
            case kSchedIdleStand:
                if (aiInSquad(c) && !aiIsLeader(c) && !c.m.flag1 && aiRandomLong(c.a, 0, 29) < 1)
                    return &slSleep;
                return aiSquadScheduleOfType(c, type);
            case kSchedRangeAttack1:
                return &slRangeAttack;
            case kSchedSpecialAttack1:
                return &slSpecialAttack1;
            case kSchedGuard:
                return &slGuardPack;
            case kSchedAgitated:
                return &slAgitated;
            case kSchedHopRetreat:
                return &slHopRetreat;
            case kSchedFail:
                if (c.m.state == kStateCombat)
                    return aiClientInPvs(c) ? &slCombatFailPvs : &slCombatFailNoPvs;
                return aiSquadScheduleOfType(c, type);
            default:
                return aiSquadScheduleOfType(c, type);
            }
        }

        inline const AiSchedule *getSchedule(AiCtx &c)
        {
            if (c.m.state == kStateCombat)
            {
                if (aiHas(c, kCondEnemyDead))
                    return aiBaseGetSchedule(c);
                if (aiHas(c, kCondLightDamage | kCondHeavyDamage))
                {
                    // Two times in five, a hop back, if there is room behind.
                    if (aiRandomLong(c.a, 0, 999) < 400)
                    {
                        fx forward[3];
                        aiForward(c.me.yaw, forward);
                        const fx back[3] = {c.me.pos[0] - forward[0] * 128, c.me.pos[1] - forward[1] * 128, c.me.pos[2]};
                        if (walkClear(*c.w.world, &c.a, c.index, kWalkCrouched, c.me.pos, back, 9))
                            return aiScheduleOfType(c, kSchedHopRetreat);
                    }
                    return aiScheduleOfType(c, kSchedTakeCoverFromEnemy);
                }
                if (aiHas(c, kCondCanRangeAttack1))
                {
                    if (aiOccupySlot(c, kSlotsHoundAttack))
                        return aiScheduleOfType(c, kSchedRangeAttack1);
                    return aiScheduleOfType(c, kSchedAgitated);
                }
            }
            return aiBaseGetSchedule(c);
        }

        inline constexpr AiClipName kClips[] = {
            {kActIdle, "idle1"},     {kActRun, "run"},         {kActRangeAttack1, "attack"},
            {kActSmallFlinch, "flinch_small"}, {kActDieSimple, "die"}, {kActReset, nullptr},
        };
        inline constexpr AiClipEvent kEvents[] = {{"attack", 5, kStartAttack}, {"attack", 59, kThump}, {nullptr, 0, 0}};

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Houndeye);
                k.health = 20;
                k.eye = fxFromInt(27);
                k.fov = fxDot(0.5);
                k.capability = kCapSquad;
                k.squadMonster = true;
                k.canActiveIdle = canActiveIdle;
                k.checkRange1 = checkRange1;
                k.yawSpeed = yawSpeed;
                k.setActivity = setActivity;
                k.sound = sound;
                k.animEvent = animEvent;
                k.startTask = startTask;
                k.runTask = runTask;
                k.prescheduleThink = prescheduleThink;
                k.scheduleOfType = scheduleOfType;
                k.getSchedule = getSchedule;
                k.clipNames = kClips;
                k.clipEvents = kEvents;
                return k;
            }();
            return k;
        }
    } // namespace houndeye

    // ================================================================ barnacle

    namespace barnacle
    {
        inline constexpr int kBodyHeight = 44;   ///< BARNACLE_BODY_HEIGHT
        inline constexpr int kPullSpeed = 8;     ///< BARNACLE_PULL_SPEED, a think
        inline constexpr int kPukeGib = 2;       ///< BARNACLE_AE_PUKEGIB
        inline constexpr int kCheckSpacing = 8;  ///< BARNACLE_CHECK_SPACING

        // m_flAltitude, m_flTongueAdj: its value1, value2. m_flKillVictimTime:
        // time1 (-1: not yet). m_cGibs: count1. m_fTongueExtended,
        // m_fLiftingPrey: flag1, flag2. Its victim is its enemy.

        /// Its tongue, for the model to draw: as far as the bones hang with
        /// the slide let out to the altitude and the adjustment.
        inline void tongue(AiCtx &c)
        {
            const fx slide = c.m.value1 + c.m.value2;
            c.me.reach = (slide > 0 ? slide : 0) + kBarnacleTongueBones;
        }

        /// Its victim's eyes, above its origin (EyePosition - origin).
        inline fx victimEye(const AiTarget &t)
        {
            return t.eye[2] - t.origin[2];
        }

        /// TongueTouchEnt: the first thing in its reach that it is against
        /// and that is not dead; and how far the floor is.
        inline std::int16_t tongueTouch(AiCtx &c, fx &length)
        {
            // (From just under the mouth: the mouth itself is the ceiling's.)
            const fx from[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] - fxFromInt(4)};
            const fx down[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] - fxFromInt(2048)};
            const Shot shot = traceShot(*c.w.world, from, down);
            length = fxFromInt(4) +
                     static_cast<fx>((static_cast<std::int64_t>(fxFromInt(2044)) * shot.fraction) >> kFractionShift);
            const fx mins[3] = {c.me.pos[0] - fxFromInt(kCheckSpacing), c.me.pos[1] - fxFromInt(kCheckSpacing),
                                c.me.pos[2] - length};
            const fx maxs[3] = {c.me.pos[0] + fxFromInt(kCheckSpacing), c.me.pos[1] + fxFromInt(kCheckSpacing),
                                c.me.pos[2]};
            auto inBox = [&](const fx *lo, const fx *hi)
            {
                for (int k = 0; k < 3; ++k)
                {
                    if (hi[k] < mins[k] || lo[k] > maxs[k])
                        return false;
                }
                return true;
            };
            AiTarget t;
            if (c.player.health > 0 && aiTargetOf(c.w, c.player, kAiPlayer, t))
            {
                const fx tall = c.player.ducked ? kCrouchedFeet : kStandingFeet;
                const fx lo[3] = {c.player.origin[0] - fxFromInt(16), c.player.origin[1] - fxFromInt(16), c.player.origin[2] - tall};
                const fx hi[3] = {c.player.origin[0] + fxFromInt(16), c.player.origin[1] + fxFromInt(16), c.player.origin[2] + tall};
                if (inBox(lo, hi) && aiRelationship(c, t) > kRelNone)
                    return kAiPlayer;
            }
            for (int i = 0; i < c.a.count; ++i)
            {
                if (i == c.index || !aiTargetOf(c.w, c.player, static_cast<std::int16_t>(i), t) || !t.monster || !t.alive)
                    continue;
                fx lo[3], hi[3];
                targetBox(c.a.states[i], lo, hi);
                if (inBox(lo, hi) && aiRelationship(c, t) > kRelNone)
                    return static_cast<std::int16_t>(i);
            }
            return kAiNone;
        }

        inline void chew(AiCtx &c)
        {
            detail::aiSay(c, CommonSound::BarnacleChew);
        }

        /// BarnacleVictimBitten: the player is dead of it (all their health
        /// and armour, through their armour); a monster is chomped on.
        inline void victimBitten(AiCtx &c, std::int16_t victim)
        {
            if (victim == kAiPlayer)
            {
                aiDamageTarget(c, kAiPlayer, c.player.health + c.player.inv.armor, kDmgSlash | kDmgAlwaysGib);
                return;
            }
            AiMonster *m = aiFind(c.a, victim);
            const AiKind *kind = aiKindOf(c.a.states[victim].kind);
            if (m != nullptr && kind != nullptr)
            {
                AiCtx them{c.w, c.player, c.a, c.ai, victim, c.a.states[victim], *m, *kind};
                aiChangeSchedule(them, aiScheduleOfType(them, kSchedBarnacleVictimChomp));
                return;
            }
            // (One of the people, or a soldier: chomped on, as their model shows.)
            actorPlayActivity(c.a, c.a.states[victim], kActBarnacleChomp, true);
        }

        /// BarnacleVictimReleased
        inline void victimReleased(AiCtx &c, std::int16_t victim)
        {
            if (victim == kAiPlayer || victim < 0)
                return;
            c.ai.held[victim] = 0;
            ActorState &held = c.a.states[victim];
            if (AiMonster *m = aiFind(c.a, victim))
            {
                if (m->idealState != kStateDead)
                    m->idealState = kStateIdle;
                held.velocity[0] = held.velocity[1] = held.velocity[2] = 0;
                m->inAir = true;
                return;
            }
            held.velocity[0] = held.velocity[1] = held.velocity[2] = 0;
            held.airborne = true;
            held.groundMover = -1;
        }

        /// FBecomeProne
        inline void becomeProne(AiCtx &c, std::int16_t victim)
        {
            if (victim == kAiPlayer)
                return;
            c.ai.held[victim] = 1;
            ActorState &held = c.a.states[victim];
            if (AiMonster *m = aiFind(c.a, victim))
            {
                m->inAir = false;
                m->idealState = kStateProne;
                return;
            }
            actorPlayActivity(c.a, held, kActBarnacleHit, true);
        }

        inline bool victimAlive(AiCtx &c, std::int16_t victim)
        {
            AiTarget t;
            return aiTargetOf(c.w, c.player, victim, t) && t.alive;
        }

        /// Each frame: whoever it has is where it has them -- not walking,
        /// not falling -- and shows it.
        inline void frame(AiCtx &c, int)
        {
            if (c.m.enemy == kAiPlayer)
            {
                for (int k = 0; k < 3; ++k)
                {
                    c.player.origin[k] = c.m.vec1[k];
                    c.player.velocity[k] = 0;
                }
                c.player.onGround = false;
                return;
            }
            if (c.m.enemy < 0 || c.m.enemy >= c.a.count)
                return;
            ActorState &held = c.a.states[c.m.enemy];
            for (int k = 0; k < 3; ++k)
            {
                held.pos[k] = c.m.vec1[k];
                held.velocity[k] = 0;
            }
            if (aiFind(c.a, c.m.enemy) == nullptr && held.finished)
            {
                // (One of the people: the bite's hit, then hanging; the
                // chomp, then chewed.)
                const std::uint8_t now = actorActivity(c.a, held);
                if (now == kActBarnacleHit)
                    actorPlayActivity(c.a, held, kActBarnaclePull, false);
                else if (now == kActBarnacleChomp)
                    actorPlayActivity(c.a, held, kActBarnacleChew, false);
            }
        }

        /// BarnacleThink
        inline void think(AiCtx &c)
        {
            // (The COMMAND line's barnaclekill: it dies of whoever it has hold
            // of, as shot.)
            if (c.m.enemy != kAiNone)
            {
                if (!victimAlive(c, c.m.enemy))
                {
                    victimReleased(c, c.m.enemy);
                    c.m.flag2 = false;
                    c.m.enemy = kAiNone;
                    return;
                }
                if (c.m.flag2)
                {
                    // Up it comes: eight units a think, under the mouth (a
                    // little behind where it faces).
                    AiTarget t;
                    aiTargetOf(c.w, c.player, c.m.enemy, t);
                    fx sine, cosine;
                    fxSinCos(t.yaw, sine, cosine);
                    fx at[3] = {c.me.pos[0] - cosine * 6, c.me.pos[1] - sine * 6, t.origin[2]};
                    c.m.value1 -= fxFromInt(kPullSpeed);
                    at[2] += fxFromInt(kPullSpeed);
                    const fx gap = c.me.pos[2] - (at[2] + victimEye(t) - fxFromInt(8));
                    if (gap < fxFromInt(kBodyHeight) && gap > -fxFromInt(kBodyHeight))
                    {
                        // At the mouth: bitten, and in ten seconds eaten.
                        c.m.flag2 = false;
                        detail::aiSay(c, CommonSound::BarnacleChew);
                        c.m.time1 = c.ai.now + 10000;
                        victimBitten(c, c.m.enemy);
                        if (c.m.enemy != kAiPlayer)
                            aiSetActivity(c, kActEat);
                    }
                    for (int k = 0; k < 3; ++k)
                        c.m.vec1[k] = at[k];
                }
                else
                {
                    if (c.m.time1 != -1 && c.ai.now > c.m.time1)
                    {
                        // Eaten (in pieces), and what it ate comes up later.
                        AiTarget t;
                        if (aiTargetOf(c.w, c.player, c.m.enemy, t))
                            aiDamageTarget(c, c.m.enemy, t.health > 0 ? t.health : 1, kDmgSlash | kDmgAlwaysGib);
                        c.m.count1 = 3;
                        return;
                    }
                    if (aiRandomLong(c.a, 0, 49) == 0)
                    {
                        chew(c);
                        victimBitten(c, c.m.enemy);
                    }
                }
            }
            else
            {
                if (c.m.seqFinished)
                {
                    aiSetActivity(c, kActIdle);
                    c.m.value2 = -fxFromInt(100);
                }
                if (c.m.count1 > 0 && aiRandomLong(c.a, 0, 99) == 1)
                {
                    detail::aiGibThrow(c, 1);
                    --c.m.count1;
                    chew(c);
                }
                fx length;
                const std::int16_t touched = tongueTouch(c, length);
                if (touched != kAiNone && c.m.flag1)
                {
                    // (The COMMAND line's barnaclekill: it dies of what it
                    // touches, and lets nobody up.)
                    if (touched == kAiPlayer && c.player.cheats != nullptr && c.player.cheats->barnacleKill)
                    {
                        monsterHurt(c.w, c.index, c.me.health + 1, false);
                        return;
                    }
                    detail::aiSay(c, CommonSound::BarnacleAlert);
                    aiSetSequenceByName(c, "attack1");
                    c.m.activity = kActReset;
                    c.m.value2 = -fxFromInt(20);
                    c.m.enemy = touched;
                    becomeProne(c, touched);
                    c.m.flag2 = true;
                    c.m.time1 = -1;
                    AiTarget t;
                    aiTargetOf(c.w, c.player, touched, t);
                    c.m.value1 = c.me.pos[2] - t.eye[2];
                    c.m.vec1[0] = c.me.pos[0];
                    c.m.vec1[1] = c.me.pos[1];
                    c.m.vec1[2] = t.origin[2];
                }
                else
                {
                    // Let down again, eight units a think, to the floor.
                    if (c.m.value1 < length)
                    {
                        c.m.value1 += fxFromInt(kPullSpeed);
                        c.m.flag1 = false;
                    }
                    else
                    {
                        c.m.value1 = length;
                        c.m.flag1 = true;
                    }
                }
            }
            tongue(c);
        }

        /// One crowbar blow is all it takes.
        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            if (d.type & kDmgClub)
                d.amount = c.me.health;
            return true;
        }

        /// Killed: whoever it held is let go; it dies where it hangs.
        inline bool killed(AiCtx &c)
        {
            if (c.m.enemy != kAiNone)
                victimReleased(c, c.m.enemy);
            c.m.enemy = kAiNone;
            c.m.flag2 = false;
            detail::aiSay(c, CommonSound::BarnacleDie);
            c.me.ai = kAiDying;
            c.m.deadflag = 1;
            c.m.idealState = c.m.state = kStateDead;
            c.me.flags &= static_cast<std::uint8_t>(~kActorSolid);
            aiSetActivity(c, kActDieSimple);
            c.m.value1 = 0;
            c.m.value2 = 0;
            c.me.reach = 0;
            actorPlaceBox(c.a, c.index);
            return true;
        }

        inline void animEvent(AiCtx &c, int event)
        {
            if (event == kPukeGib)
                detail::aiGibThrow(c, 1);
        }

        /// Its own think: ten times a second (less when the player is not in
        /// what it can see of the map), and once dead, its death played out.
        inline void barnacleThink(AiCtx &c)
        {
            if (c.me.ai >= kAiDying)
            {
                if (c.m.seqFinished)
                    c.me.ai = kAiDead;
                return;
            }
            if (c.m.enemy == kAiNone && !aiClientInPvs(c))
                c.m.thinkMs += aiRandomMs(c.a, 900, 1400);
            think(c);
        }

        inline constexpr AiClipName kClips[] = {
            {kActIdle, "idle1"}, {kActEat, "chewcycle"}, {kActSmallFlinch, "flinch_sm"}, {kActDieSimple, "death"},
            {kActReset, nullptr},
        };
        inline constexpr AiClipEvent kEvents[] = {
            {"death", 11, kPukeGib}, {"death", 29, kPukeGib}, {"death", 32, kPukeGib}, {"death", 36, kPukeGib},
            {nullptr, 0, 0}};

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Barnacle);
                k.health = 25;
                k.eye = 0;
                k.fov = fxDot(0.5);
                k.think = barnacleThink;
                k.frame = frame;
                k.takeDamage = takeDamage;
                k.killed = killed;
                k.animEvent = animEvent;
                k.clipNames = kClips;
                k.clipEvents = kEvents;
                return k;
            }();
            return k;
        }
    } // namespace barnacle

    namespace grunt
    {
        inline const AiKind &kind(); // (HlGrunt.hpp)
    }
    // (HlAiPeople.hpp)
    namespace scientist
    {
        inline const AiKind &kind();
    }
    namespace barney
    {
        inline const AiKind &kind();
    }
    namespace sitter
    {
        inline const AiKind &kind();
    }
    namespace gman
    {
        inline const AiKind &kind();
    }
    namespace generic
    {
        inline const AiKind &kind();
    }
    inline const AiKind *aiAlienKindOf(std::uint8_t kind); // (HlAliens.hpp)

    inline const AiKind *aiKindOf(std::uint8_t kind)
    {
        switch (static_cast<ActorKind>(kind))
        {
        case ActorKind::Headcrab:  return &headcrab::kind();
        case ActorKind::Zombie:    return &zombie::kind();
        case ActorKind::Slave:     return &slave::kind();
        case ActorKind::Bullsquid: return &bullsquid::kind();
        case ActorKind::Houndeye:  return &houndeye::kind();
        case ActorKind::Barnacle:  return &barnacle::kind();
        case ActorKind::Grunt:     return &grunt::kind();
        case ActorKind::Scientist: return &scientist::kind();
        case ActorKind::Guard:     return &barney::kind();
        case ActorKind::Sitter:    return &sitter::kind();
        case ActorKind::Gman:      return &gman::kind();
        case ActorKind::Generic:   return &generic::kind();
        default:                   return aiAlienKindOf(kind);
        }
    }
} // namespace hldata
