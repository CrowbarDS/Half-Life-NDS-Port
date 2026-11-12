#pragma once

// Round 3 of the monsters, each from its file in the game's code, kept as
// close to it as the port allows and named as it is, so the two can be read
// side by side:
//
//   cockroach   roach.cpp          its own think: no schedules
//   snark       squeakgrenade.cpp  a monstermaker's: let go as a snark that
//                                  hunts the player (HlMonsters.hpp's shots)
//   alien grunt agrunt.cpp         a squad, the hornet gun, its fists
//   controller  controller.cpp     flies (its Move: aiFlyTick, here); a ball
//                                  that hunts, and a stream of small ones
//   leech       leech.cpp          its own swimming think: no schedules
//
// std-only and inline (HlMonsters.hpp includes it last): the host tests run
// it.

#include <algorithm>

namespace hldata
{
    // ============================================================= cockroach

    namespace roach
    {
        // m_iMode (count1).
        inline constexpr int kIdle = 0;
        inline constexpr int kBored = 1;
        inline constexpr int kScaredByEnt = 2;
        inline constexpr int kScaredByLight = 3;
        inline constexpr int kSmellFood = 4;
        inline constexpr int kEat = 5;
        // Where it runs to: vec1 (m_Route[0]); whether it is going: flag1;
        // when it last thought: time1.

        /// Eat: it will not think of food again for `seconds`.
        inline void eat(AiCtx &c, int seconds)
        {
            c.m.hungryTime = c.ai.now + seconds * 1000;
        }
        inline bool shouldEat(const AiCtx &c)
        {
            return c.m.hungryTime <= c.ai.now;
        }

        /// The food it smells (Listen, for a roach: a body or a gib within
        /// what the sound carries), nearest first; null if none.
        inline const AiSound *smellFood(AiCtx &c)
        {
            const AiSound *best = nullptr;
            fx nearest = 0;
            for (std::int8_t i = c.ai.sounds.active; i != kSoundListEmpty; i = c.ai.sounds.pool[i].next)
            {
                const AiSound &s = c.ai.sounds.pool[i];
                if (!(s.type & (kSoundCarcass | kSoundMeat)))
                    continue;
                const fx far = aiDist(s.origin, c.me.pos);
                if (far > fxFromInt(s.volume) || (best != nullptr && far >= nearest))
                    continue;
                best = &s;
                nearest = far;
            }
            return best;
        }

        /// PickNewDest: food, if that is what it is going for; else
        /// somewhere 256 to 511 away, and at least 128 in the plane.
        inline void pickNewDest(AiCtx &c, int mode)
        {
            c.m.count1 = mode;
            c.m.flag1 = true;
            if (mode == kSmellFood)
            {
                if (const AiSound *food = smellFood(c))
                {
                    c.m.vec1[0] = food->origin[0] + fxFromInt(3 - aiRandomLong(c.a, 0, 5));
                    c.m.vec1[1] = food->origin[1] + fxFromInt(3 - aiRandomLong(c.a, 0, 5));
                    c.m.vec1[2] = food->origin[2];
                    return;
                }
            }
            for (int tries = 0; tries < 8; ++tries)
            {
                const fx dir[2] = {fxFromInt(aiRandomLong(c.a, -1000, 1000)) / 1000,
                                   fxFromInt(aiRandomLong(c.a, -1000, 1000)) / 1000};
                const int far = 256 + aiRandomLong(c.a, 0, 255);
                const fx to[3] = {c.me.pos[0] + dir[0] * far, c.me.pos[1] + dir[1] * far, c.me.pos[2]};
                c.m.vec1[0] = to[0];
                c.m.vec1[1] = to[1];
                c.m.vec1[2] = to[2];
                if (aiDist2D(to, c.me.pos) >= fxFromInt(128))
                    break;
            }
            // Now and then, a skitter as it goes.
            if (aiRandomLong(c.a, 0, 9) == 1)
                detail::aiSay(c, CommonSound::RoachWalk, aiRandomLong(c.a, 80, 119));
        }

        /// WALK_MOVE for something two units across: a step through the
        /// world as a point a few units up -- or up a stair -- then down to
        /// the floor, a stair's height or two below. (The console's smallest
        /// walk is a crouching person's, and the game's roaches are in the
        /// vents and under things that never let one through.)
        inline bool tinyStep(AiCtx &c, fx *feet, fx dx, fx dy)
        {
            for (const int up : {4, 18})
            {
                const fx from[3] = {feet[0], feet[1], feet[2] + fxFromInt(up)};
                const fx to[3] = {feet[0] + dx, feet[1] + dy, feet[2] + fxFromInt(up)};
                if (traceShot(*c.w.world, from, to).fraction < kFractionOne)
                    continue;
                const fx down[3] = {to[0], to[1], to[2] - fxFromInt(up + 36)};
                const Shot floor = traceShot(*c.w.world, to, down);
                if (floor.fraction >= kFractionOne)
                    return false; // (no floor: it does not walk off an edge)
                feet[0] = to[0];
                feet[1] = to[1];
                feet[2] = to[2] + static_cast<fx>((static_cast<std::int64_t>(down[2] - to[2]) * floor.fraction) >> kFractionShift);
                return true;
            }
            return false;
        }

        inline void stop(AiCtx &c)
        {
            c.m.flag1 = false;
            aiSetActivity(c, kActIdle);
            c.m.count1 = c.m.count1 == kSmellFood ? kEat : kIdle;
        }

        /// Move: a step along its way -- it may overshoot -- and a new way
        /// if it is held up.
        inline void move(AiCtx &c, int ms)
        {
            const fx far = aiDist2D(c.m.vec1, c.me.pos);
            c.me.yaw = fxAtan2Turns(c.m.vec1[1] - c.me.pos[1], c.m.vec1[0] - c.me.pos[0]);
            fx forward[3];
            aiForward(c.me.yaw, forward);
            const fx speed = c.m.groundSpeed > 0 ? c.m.groundSpeed : monsterKind(c.me.kind).speed;
            // (Not past where it is going -- the game's may overshoot, into
            // the wall behind -- and a long while unseen goes in tenths.)
            if (ms > 100)
            {
                for (int done = 0; done < ms && c.m.flag1; done += 100)
                    move(c, ms - done > 100 ? 100 : ms - done);
                return;
            }
            fx step = static_cast<fx>((static_cast<std::int64_t>(speed) * ms) / 1000);
            const bool arrives = far <= step;
            if (arrives)
                step = far;
            // (A look for what is in the way only now and then: the game's
            // own load balancing.)
            if (aiRandomLong(c.a, 0, 7) == 1)
            {
                fx probe[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2]};
                if (!tinyStep(c, probe, forward[0] * 4, forward[1] * 4))
                {
                    pickNewDest(c, c.m.count1);
                    return;
                }
            }
            fx feet[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2]};
            const fx dx = static_cast<fx>((static_cast<std::int64_t>(forward[0]) * step) >> kFxShift);
            const fx dy = static_cast<fx>((static_cast<std::int64_t>(forward[1]) * step) >> kFxShift);
            if (tinyStep(c, feet, dx, dy))
            {
                for (int k = 0; k < 3; ++k)
                    c.me.pos[k] = feet[k];
                actorPlaceBox(c.a, c.index);
            }
            if (arrives)
            {
                stop(c);
                return;
            }
            if (aiRandomLong(c.a, 0, 149) == 1 && c.m.count1 != kScaredByLight && c.m.count1 != kSmellFood)
                pickNewDest(c, kIdle);
        }

        /// Look, for a roach: all round, 150 units, at whatever it fears --
        /// only while the player is somewhere it can be seen from.
        inline bool seesFear(AiCtx &c, int distance)
        {
            const fx reach = fxFromInt(distance);
            AiTarget t;
            if (c.player.health > 0 && !unseen(c.player) && aiDist(c.player.origin, c.me.pos) < reach + kStandingFeet &&
                aiTargetOf(c.w, c.player, kAiPlayer, t) && aiRelationship(c, t) == kRelFear)
                return true;
            for (int i = 0; i < c.a.count; ++i)
            {
                const ActorState &other = c.a.states[i];
                if (i == c.index || !other.active || other.ai >= kAiDying || other.kind == c.me.kind ||
                    !canBeHurt(other.kind))
                    continue;
                const fx d[2] = {other.pos[0] - c.me.pos[0], other.pos[1] - c.me.pos[1]};
                if (d[0] > reach || d[0] < -reach || d[1] > reach || d[1] < -reach ||
                    aiDist(other.pos, c.me.pos) >= reach)
                    continue;
                if (aiTargetOf(c.w, c.player, static_cast<std::int16_t>(i), t) && aiRelationship(c, t) == kRelFear)
                    return true;
            }
            return false;
        }

        /// How lit it stands (GETENTITYILLUM), as far as that changes: the
        /// strength now of the switched light the map lights its floor with
        /// most (the converter's, in its record's item, plus one, top 16
        /// bits); -1 for none.
        inline int lightHere(const AiCtx &c)
        {
            if (c.a.records == nullptr || c.index >= c.a.recordCount)
                return -1;
            const int style = static_cast<int>(c.a.records[c.index].item >> kRoachLightShift) - 1;
            if (style < 32)
                return -1;
            // (The tenth of a second it is: a multiply, not a divide.)
            const auto tick = static_cast<std::uint32_t>((static_cast<std::uint64_t>(c.ai.now) * 10486u) >> 20);
            return lightStyleValue(style, tick, c.w.stylesOff, c.w.stylePattern.value.data());
        }

        /// MonsterThink, the roach's: ten times a second where the player
        /// may see it, once a second or so otherwise.
        inline void think(AiCtx &c)
        {
            const int ms = c.m.time1 != 0 && c.ai.now > c.m.time1 ? static_cast<int>(std::min<std::int32_t>(c.ai.now - c.m.time1, 1500)) : 100;
            c.m.time1 = c.ai.now;
            if (c.me.ai >= kAiDying)
                return;
            // A second for the world to be lit, then the light where it
            // stands is taken, once (m_fLightHacked, m_flLastLightLevel:
            // flag2, flag3 and value1).
            if (!c.m.flag2)
            {
                c.m.flag2 = true;
                c.m.thinkMs += 900;
                return;
            }
            if (!c.m.flag3)
            {
                c.m.flag3 = true;
                c.m.value1 = lightHere(c);
            }
            const bool seen = aiClientInPvs(c);
            if (!seen)
                c.m.thinkMs += aiRandomMs(c.a, 900, 1400);
            switch (c.m.count1)
            {
            case kIdle:
            case kEat:
                // Not going anywhere: now and then, a look round.
                if (aiRandomLong(c.a, 0, 3) == 1)
                {
                    if (seen && seesFear(c, 150))
                    {
                        eat(c, 30 + aiRandomLong(c.a, 0, 14));
                        pickNewDest(c, kScaredByEnt);
                        aiSetActivity(c, kActWalk);
                    }
                    else if (aiRandomLong(c.a, 0, 149) == 1)
                    {
                        // (Bored.)
                        pickNewDest(c, kBored);
                        aiSetActivity(c, kActWalk);
                        if (c.m.count1 == kEat)
                            eat(c, 30 + aiRandomLong(c.a, 0, 14));
                    }
                }
                // Someone has put the lights on: away. Otherwise food, if it
                // is hungry and is only standing about.
                if (c.m.count1 == kIdle && lightHere(c) > c.m.value1)
                {
                    pickNewDest(c, kScaredByLight);
                    aiSetActivity(c, kActWalk);
                }
                else if (c.m.count1 == kIdle && shouldEat(c))
                {
                    const AiSound *food = smellFood(c);
                    if (food != nullptr && food->origin[2] - c.me.pos[2] <= fxFromInt(3) &&
                        food->origin[2] - c.me.pos[2] >= -fxFromInt(3))
                    {
                        pickNewDest(c, kSmellFood);
                        aiSetActivity(c, kActWalk);
                    }
                }
                break;
            default:
                break;
            }
            if (c.m.flag1)
                move(c, ms);
        }

        /// Touch: the player walking over it is the end of it.
        inline void frame(AiCtx &c, int)
        {
            if (c.me.ai >= kAiDying || c.player.health <= 0)
                return;
            const fx moving = c.player.velocity[0] | c.player.velocity[1] | c.player.velocity[2];
            if (moving == 0)
                return;
            const fx at[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(2)};
            if (detail::playerTouchesBox(c.player, at, at, fxFromInt(2)))
            {
                markFloor(c.a, *c.w.world, c.me.pos, 24, kDecalAlien2, 6, kMarkAlien);
                monsterHurt(c.w, c.index, c.me.health, false, kHitGeneric, kDmgCrush, kAiPlayer);
            }
        }

        /// Killed: a squash, or now and then its cry; and gone.
        inline bool killed(AiCtx &c)
        {
            // (Its cry at 0.8 of full, its squash at 0.7.)
            const bool cries = aiRandomLong(c.a, 0, 4) == 1;
            if (c.w.heard != nullptr)
                c.w.heard->add(cries ? CommonSound::RoachDie : CommonSound::RoachSmash, c.me.pos, cries ? 102 : 89,
                               kRadiusLarge, aiRandomLong(c.a, 80, 119));
            soundInsert(c.ai.sounds, c.ai.now, kSoundWorld, c.me.pos, 128, 1000);
            c.me.ai = kAiDead;
            c.m.deadflag = 2;
            c.m.state = c.m.idealState = kStateDead;
            c.me.active = false;
            actorPlaceBox(c.a, c.index);
            return true;
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Cockroach);
                k.health = 1;
                k.eye = fxFromInt(1);
                k.fov = fxDot(0.5);
                k.blood = kBloodYellow;
                k.think = think;
                k.frame = frame;
                k.killed = killed;
                return k;
            }();
            return k;
        }
    } // namespace roach

    // =================================================================== rat

    /// CRat: no more than a monster -- the game's own schedules, nothing of
    /// its own; it stands about, looks round at what it hears, and dies.
    namespace rat
    {
        inline int yawSpeed(AiCtx &) { return 45; }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Rat);
                k.health = 8;
                k.eye = fxFromInt(6);
                k.fov = fxDot(0.5);
                k.blood = kBloodRed;
                k.yawSpeed = yawSpeed;
                return k;
            }();
            return k;
        }
    } // namespace rat

    // ================================================================= snark

    namespace snark
    {
        /// A monstermaker's snark: let go as the snark a player throws is
        /// (HlMonsters.hpp: kShotSnarkFoe), after the player and their side,
        /// and the place it was made in is free -- its maker makes as many
        /// as it makes, a few, all told.
        inline void think(AiCtx &c)
        {
            if (!c.me.active)
                return;
            // (A point a few units up, as it is: HlMonsters.hpp. Off toward
            // the player.)
            const fx at[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(4)};
            fx toward[3] = {c.player.origin[0] - at[0], c.player.origin[1] - at[1], 0};
            const fx far = fxLength(toward);
            for (int k = 0; k < 2; ++k)
                toward[k] = far > 0 ? static_cast<fx>((static_cast<std::int64_t>(toward[k]) << kFxShift) / far) : 0;
            const fx velocity[3] = {toward[0] * 120 + fxFromInt(aiRandomLong(c.a, -30, 30)),
                                    toward[1] * 120 + fxFromInt(aiRandomLong(c.a, -30, 30)), fxFromInt(60)};
            Projectile *shot = projectileAdd(c.a, kShotSnarkFoe, at, velocity, 15000);
            if (shot == nullptr)
                return; // (no room yet: again at its next think)
            shot->sprite = 0;
            detail::aiSay(c, CommonSound::SnarkDeploy);
            c.me.ai = kAiDead;
            c.m.state = c.m.idealState = kStateDead;
            c.me.active = false;
            actorPlaceBox(c.a, c.index);
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Snark);
                k.health = 2;
                k.blood = kBloodYellow;
                k.think = think;
                return k;
            }();
            return k;
        }
    } // namespace snark

    // =========================================================== alien grunt

    namespace agrunt
    {
        inline constexpr int kHornet1 = 1, kHornet5 = 5, kLeftPunch = 12, kRightPunch = 13;
        inline constexpr int kMeleeDist = 100; ///< AGRUNT_MELEE_DIST
        inline constexpr int kArmour = 10;     ///< the hit group of its armour
        // m_flNextHornetAttackCheck: time1; m_fCanHornetAttack: flag1;
        // m_flNextSpeakTime, m_flNextWordTime: time2, time3; m_iLastWord:
        // count1; m_flNextPainTime: value2 (ms).

        enum : std::uint8_t
        {
            kSchedThreatDisplay = kSchedLastCommon + 1,
        };

        inline constexpr AiTask tlAGruntFail[] = {
            {kTaskStopMoving, 0}, {kTaskSetActivity, kActIdle}, {kTaskWait, 2000}, {kTaskWaitPvs, 0}};
        inline constexpr AiSchedule slAGruntFail = {tlAGruntFail, 4, kCondCanRangeAttack1 | kCondCanMeleeAttack1, 0,
                                                    "AGrunt Fail"};
        inline constexpr AiTask tlAGruntCombatFail[] = {
            {kTaskStopMoving, 0}, {kTaskSetActivity, kActIdle}, {kTaskWaitFaceEnemy, 2000}, {kTaskWaitPvs, 0}};
        inline constexpr AiSchedule slAGruntCombatFail = {tlAGruntCombatFail, 4,
                                                          kCondCanRangeAttack1 | kCondCanMeleeAttack1, 0,
                                                          "AGrunt Combat Fail"};
        inline constexpr AiTask tlAGruntStandoff[] = {
            {kTaskStopMoving, 0}, {kTaskSetActivity, kActIdle}, {kTaskWaitFaceEnemy, 2000}};
        inline constexpr AiSchedule slAGruntStandoff = {
            tlAGruntStandoff, 3,
            kCondCanRangeAttack1 | kCondCanMeleeAttack1 | kCondSeeEnemy | kCondNewEnemy | kCondHearSound,
            kSoundDanger, "Agrunt Standoff"};
        inline constexpr AiTask tlAGruntRangeAttack1[] = {
            {kTaskStopMoving, 0}, {kTaskFaceEnemy, 0}, {kTaskRangeAttack1, 0}};
        inline constexpr AiSchedule slAGruntRangeAttack1 = {tlAGruntRangeAttack1, 3,
                                                            kCondNewEnemy | kCondEnemyDead | kCondHeavyDamage, 0,
                                                            "AGrunt Range Attack1"};
        inline constexpr AiTask tlAGruntTakeCoverFromEnemy[] = {
            {kTaskStopMoving, 0},     {kTaskWait, 200},
            {kTaskFindCoverFromEnemy, 0}, {kTaskRunPath, 0},
            {kTaskWaitForMovement, 0}, {kTaskRemember, static_cast<std::int32_t>(kMemoryInCover)},
            {kTaskFaceEnemy, 0}};
        inline constexpr AiSchedule slAGruntTakeCoverFromEnemy = {tlAGruntTakeCoverFromEnemy, 7, kCondNewEnemy, 0,
                                                                  "AGruntTakeCoverFromEnemy"};
        inline constexpr AiTask tlAGruntVictoryDance[] = {
            {kTaskStopMoving, 0},
            {kTaskSetFailSchedule, kSchedThreatDisplay},
            {kTaskWait, 200},
            {kTaskGetPathToEnemyCorpse, 0},
            {kTaskWalkPath, 0},
            {kTaskWaitForMovement, 0},
            {kTaskFaceEnemy, 0},
            {kTaskPlaySequence, kActCrouch},
            {kTaskPlaySequence, kActVictoryDance},
            {kTaskPlaySequence, kActVictoryDance},
            {kTaskPlaySequence, kActStand},
            {kTaskPlaySequence, kActThreatDisplay},
            {kTaskPlaySequence, kActCrouch},
            {kTaskPlaySequence, kActVictoryDance},
            {kTaskPlaySequence, kActVictoryDance},
            {kTaskPlaySequence, kActStand}};
        inline constexpr AiSchedule slAGruntVictoryDance = {
            tlAGruntVictoryDance, 16, kCondNewEnemy | kCondLightDamage | kCondHeavyDamage, 0, "AGruntVictoryDance"};
        inline constexpr AiTask tlAGruntThreatDisplay[] = {
            {kTaskStopMoving, 0}, {kTaskFaceEnemy, 0}, {kTaskPlaySequence, kActThreatDisplay}};
        inline constexpr AiSchedule slAGruntThreatDisplay = {
            tlAGruntThreatDisplay, 3, kCondNewEnemy | kCondLightDamage | kCondHeavyDamage,
            kSoundPlayer | kSoundCombat | kSoundWorld, "AGruntThreatDisplay"};

        /// IRelationship: the soldiers are its nemesis.
        inline int relationship(AiCtx &c, std::int16_t target, int base)
        {
            if (target >= 0 && target < c.a.count && isGrunt(c.a.states[target].kind))
                return kRelNemesis;
            return base;
        }

        /// TraceAttack: its armour (hit group 10) turns a bullet, a blade or a
        /// club, all but a little.
        inline bool traceAttack(AiCtx &c, AiDamage &d)
        {
            if (d.group == kArmour && (d.type & (kDmgBullet | kDmgSlash | kDmgClub)))
            {
                if (d.hasFrom)
                    effectAdd(c.a, kEffectMuzzle, d.from, nullptr, 2, 100); // (its ricochet's spark)
                d.amount -= 20;
                if (d.amount <= 0)
                {
                    d.amount = 0;
                    d.graze = true; // (the game's 0.1: felt, nothing taken)
                }
            }
            return true;
        }

        inline void stopTalking(AiCtx &c)
        {
            c.m.time2 = c.m.time3 = c.ai.now + 10000 + aiRandomLong(c.a, 0, 10) * 1000;
        }

        /// PrescheduleThink: the words it says to itself.
        inline void prescheduleThink(AiCtx &c)
        {
            if (c.m.time2 > c.ai.now || c.m.time3 >= c.ai.now)
                return;
            detail::aiSay(c, aiRandomLong(c.a, 0, 1) == 0 ? CommonSound::AgruntIdle1 : CommonSound::AgruntIdle2);
            if (aiRandomLong(c.a, 1, 10) <= 1)
                stopTalking(c);
            else
                c.m.time3 = c.ai.now + aiRandomMs(c.a, 500, 1000);
        }

        inline void sound(AiCtx &c, int which)
        {
            switch (which)
            {
            case kAiSoundAlert:
                stopTalking(c);
                detail::aiSay(c, aiRandomLong(c.a, 0, 1) == 0 ? CommonSound::AgruntAlert1 : CommonSound::AgruntAlert2);
                break;
            case kAiSoundPain:
                if (c.m.value2 > c.ai.now)
                    return;
                c.m.value2 = c.ai.now + 600;
                stopTalking(c);
                detail::aiSay(c, aiRandomLong(c.a, 0, 1) == 0 ? CommonSound::AgruntPain1 : CommonSound::AgruntPain2);
                break;
            case kAiSoundDeath:
                stopTalking(c);
                detail::aiSay(c, aiRandomLong(c.a, 0, 1) == 0 ? CommonSound::AgruntDie1 : CommonSound::AgruntDie2);
                break;
            default:
                break;
            }
        }

        inline int yawSpeed(AiCtx &c)
        {
            return c.m.activity == kActTurnLeft || c.m.activity == kActTurnRight ? 110 : 100;
        }

        /// Where its gun arm is (the model's first attachment, as the game's
        /// m_HackedGunPos has it: 24 to its right, 64 ahead, 48 up -- made
        /// a little nearer, the console's model being drawn whole).
        inline void armPos(const AiCtx &c, fx *at)
        {
            fx forward[3], right[3];
            aiForward(c.me.yaw, forward);
            aiRight(c.me.yaw, right);
            for (int k = 0; k < 2; ++k)
                at[k] = c.me.pos[k] + right[k] * 24 + forward[k] * 32;
            at[2] = c.me.pos[2] + fxFromInt(48);
        }

        inline bool canCheckAttacks(AiCtx &c)
        {
            return !aiHas(c, kCondEnemyTooFar);
        }

        inline bool checkMelee1(AiCtx &c, fx dot, fx dist)
        {
            return aiHas(c, kCondSeeEnemy) && dist <= fxFromInt(kMeleeDist) && dot >= fxDot(0.6) &&
                   c.m.enemy != kAiNone;
        }

        /// NoFriendlyFire (the squad monster's: none of its squad ahead).
        inline bool noFriendlyFire(AiCtx &c)
        {
            if (!aiInSquad(c))
                return true;
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.enemy, t))
                return false;
            const std::uint16_t yaw = fxAtan2Turns(t.center[1] - c.me.pos[1], t.center[0] - c.me.pos[0]);
            fx forward[3], right[3];
            aiForward(yaw, forward);
            aiRight(yaw, right);
            for (int i = 0; i < kMaxSquadMembers; ++i)
            {
                const std::int16_t id = aiSquadMember(c, i);
                if (id == kAiNone || id == c.index)
                    continue;
                const fx *p = c.a.states[id].pos;
                const fx d[2] = {p[0] - c.me.pos[0], p[1] - c.me.pos[1]};
                const std::int64_t ahead = static_cast<std::int64_t>(d[0]) * forward[0] + static_cast<std::int64_t>(d[1]) * forward[1];
                const std::int64_t side = static_cast<std::int64_t>(d[0]) * right[0] + static_cast<std::int64_t>(d[1]) * right[1];
                const std::int64_t width = static_cast<std::int64_t>(fxFromInt(48)) * kFxOne;
                if (ahead > 0 && side < width && side > -width)
                    return false;
            }
            return true;
        }

        /// CheckRangeAttack1: its enemy seen, 100 to 1024 off, more or less
        /// ahead, and a clear line to it from its arm -- looked at every two
        /// to five seconds once it is yes, every fifth of one while no.
        inline bool checkRange1(AiCtx &c, fx dot, fx dist)
        {
            if (c.ai.now < c.m.time1)
                return c.m.flag1;
            if (aiHas(c, kCondSeeEnemy) && dist >= fxFromInt(kMeleeDist) && dist <= fxFromInt(1024) &&
                dot >= fxDot(0.5) && noFriendlyFire(c))
            {
                AiTarget t;
                fx arm[3];
                armPos(c, arm);
                if (aiTargetOf(c.w, c.player, c.m.enemy, t))
                {
                    fx end[3];
                    const fx d[3] = {t.center[0] - arm[0], t.center[1] - arm[1], t.center[2] - arm[2]};
                    const fx far = fxLength(d);
                    fx dir[3] = {0, 0, 0};
                    for (int k = 0; far > 0 && k < 3; ++k)
                        dir[k] = static_cast<fx>((static_cast<std::int64_t>(d[k]) << kFxShift) / far);
                    const std::int16_t hit = aiTraceLine(c, arm, dir, fxToInt(far) + 8, end);
                    if (hit == c.m.enemy || aiDist(end, arm) + fxFromInt(8) >= far)
                    {
                        c.m.time1 = c.ai.now + aiRandomMs(c.a, 2000, 5000);
                        c.m.flag1 = true;
                        return true;
                    }
                }
            }
            c.m.time1 = c.ai.now + 200;
            c.m.flag1 = false;
            return false;
        }

        /// A hornet from its arm, at its enemy where it was last seen.
        inline void shootHornet(AiCtx &c)
        {
            fx arm[3];
            armPos(c, arm);
            fx dir[3];
            if (aiHas(c, kCondSeeEnemy))
            {
                const fx d[3] = {c.m.enemyLkp[0] - c.me.pos[0], c.m.enemyLkp[1] - c.me.pos[1],
                                 c.m.enemyLkp[2] - c.me.pos[2]};
                const fx far = fxLength(d);
                for (int k = 0; k < 3; ++k)
                    dir[k] = far > 0 ? static_cast<fx>((static_cast<std::int64_t>(d[k]) << kFxShift) / far) : 0;
            }
            else
            {
                aiForward(c.me.yaw, dir);
            }
            for (int k = 0; k < 3; ++k)
                arm[k] += dir[k] * 32;
            effectAdd(c.a, kEffectMuzzle, arm, nullptr, 6, 100);
            const fx velocity[3] = {dir[0] * 300, dir[1] * 300, dir[2] * 300};
            if (Projectile *shot = projectileAdd(c.a, kShotHornetFoe, arm, velocity, 5000))
                shot->sprite = aiRandomLong(c.a, 1, 5) <= 2 ? 0 : 2; // (red, or the faster orange)
            detail::aiSay(c, static_cast<CommonSound>(static_cast<int>(CommonSound::HornetFire1) + aiRandomLong(c.a, 0, 2)));
        }

        /// A fist: what is 100 ahead takes it, and the player is thrown to
        /// the side the blow came from.
        inline void punch(AiCtx &c, int side)
        {
            const std::int16_t hit = aiHullAttack(c, kMeleeDist, skillOf(c.w).agruntDmgPunch, kDmgClub);
            if (hit == kAiNone)
            {
                detail::aiSay(c, CommonSound::ClawMiss, aiRandomLong(c.a, 95, 105));
                return;
            }
            detail::aiPunch(c, hit, fxFromInt(8), fxFromInt(25) * side);
            fx right[3];
            aiRight(c.me.yaw, right);
            const fx shove[3] = {right[0] * 250 * side, right[1] * 250 * side, 0};
            detail::aiShove(c, hit, shove);
            detail::aiSay(c, CommonSound::ClawStrike, aiRandomLong(c.a, 95, 105));
        }

        inline void animEvent(AiCtx &c, int event)
        {
            if (event >= kHornet1 && event <= kHornet5)
                shootHornet(c);
            else if (event == kLeftPunch)
                punch(c, -1);
            else if (event == kRightPunch)
                punch(c, 1);
        }

        inline const AiSchedule *getSchedule(AiCtx &c)
        {
            if (aiHas(c, kCondHearSound))
            {
                const AiSound *heard = aiBestSound(c);
                if (heard != nullptr && (heard->type & kSoundDanger))
                    return aiScheduleOfType(c, kSchedTakeCoverFromBestSound);
            }
            if (c.m.state == kStateCombat)
            {
                if (aiHas(c, kCondEnemyDead))
                    return aiBaseGetSchedule(c);
                if (aiHas(c, kCondNewEnemy))
                    return aiScheduleOfType(c, kSchedWakeAngry);
                if (aiHas(c, kCondCanMeleeAttack1))
                {
                    stopTalking(c);
                    detail::aiSay(c, CommonSound::AgruntAttack);
                    return aiScheduleOfType(c, kSchedMeleeAttack1);
                }
                if (aiHas(c, kCondHeavyDamage))
                    return aiScheduleOfType(c, kSchedSmallFlinch);
                if (aiHas(c, kCondCanRangeAttack1) && aiOccupySlot(c, kSlotsAgruntHornet))
                    return aiScheduleOfType(c, kSchedRangeAttack1);
                if (aiOccupySlot(c, kSlotAgruntChase))
                    return aiScheduleOfType(c, kSchedChaseEnemy);
                return aiScheduleOfType(c, kSchedStandoff);
            }
            return aiBaseGetSchedule(c);
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            switch (type)
            {
            case kSchedTakeCoverFromEnemy: return &slAGruntTakeCoverFromEnemy;
            case kSchedRangeAttack1:       return &slAGruntRangeAttack1;
            case kSchedThreatDisplay:      return &slAGruntThreatDisplay;
            case kSchedStandoff:           return &slAGruntStandoff;
            case kSchedVictoryDance:       return &slAGruntVictoryDance;
            case kSchedFail:               return c.m.enemy != kAiNone ? &slAGruntCombatFail : &slAGruntFail;
            default:                       break;
            }
            return aiSquadScheduleOfType(c, type);
        }

        inline void started(AiCtx &c)
        {
            stopTalking(c);
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::AlienGrunt);
                k.health = 90;
                k.eye = fxFromInt(56);
                k.fov = fxDot(0.2);
                k.capability = kCapSquad;
                k.soundMask = kSoundWorld | kSoundCombat | kSoundPlayer | kSoundDanger;
                k.blood = kBloodYellow;
                k.squadMonster = true;
                k.relationship = relationship;
                k.traceAttack = traceAttack;
                k.prescheduleThink = prescheduleThink;
                k.sound = sound;
                k.yawSpeed = yawSpeed;
                k.canCheckAttacks = canCheckAttacks;
                k.checkMelee1 = checkMelee1;
                k.checkRange1 = checkRange1;
                k.animEvent = animEvent;
                k.getSchedule = getSchedule;
                k.scheduleOfType = scheduleOfType;
                k.started = started;
                return k;
            }();
            return k;
        }
    } // namespace agrunt

    // ================================================================ flying

    namespace fly
    {
        /// Where a flier's middle is, for its traces: the standing hull's,
        /// above its feet (the game's controller is a box 64 across; the
        /// console has the person's box to try a way with).
        inline void middle(const fx *feet, fx *at)
        {
            at[0] = feet[0];
            at[1] = feet[1];
            at[2] = feet[2] + kStandingFeet;
        }

        /// A straight flight from `from` to `to` (feet), nothing in the way.
        inline bool clear(AiCtx &c, const fx *from, const fx *to)
        {
            fx a[3], b[3];
            middle(from, a);
            middle(to, b);
            const Trace t = detail::walkerTrace(*c.w.world, &c.a, c.index, false, a, b);
            return !t.startSolid && t.fraction >= kFractionOne;
        }

        /// The node a flier is at, or nearest it can see (the ones in the
        /// air too).
        inline int nearest(AiCtx &c, const fx *at)
        {
            // (Not pathNearest: that is a walker's -- near in the plane, and
            // somewhere it can walk to. A flier's is the nearest it can see,
            // up or down, in the air or on the ground; of the eight nearest.)
            constexpr int kTries = 8;
            int best[kTries];
            std::int64_t bestD[kTries];
            int count = 0;
            for (int n = 0; n < c.a.nodeCount; ++n)
            {
                const fx d[3] = {c.a.nodes[n].pos[0] - at[0], c.a.nodes[n].pos[1] - at[1], c.a.nodes[n].pos[2] - at[2]};
                const std::int64_t far = static_cast<std::int64_t>(fxLength(d)) * fxLength(d);
                if (far > static_cast<std::int64_t>(fxFromInt(2048)) * fxFromInt(2048))
                    continue;
                int k = count < kTries ? count++ : kTries;
                if (k == kTries)
                {
                    if (far >= bestD[kTries - 1])
                        continue;
                    k = kTries - 1;
                }
                for (; k > 0 && bestD[k - 1] > far; --k)
                {
                    best[k] = best[k - 1];
                    bestD[k] = bestD[k - 1];
                }
                best[k] = n;
                bestD[k] = far;
            }
            fx from[3];
            middle(at, from);
            for (int k = 0; k < count; ++k)
            {
                fx to[3];
                middle(c.a.nodes[best[k]].pos, to);
                if (lineClear(*c.w.world, from, to))
                    return best[k];
            }
            return count > 0 ? best[0] : -1;
        }

        /// BuildNearestRoute, for what flies: the first node, in the order
        /// cover is looked for, that `wantSeen` (or not) has a line to the
        /// threat's eyes, `minDist` to `maxDist` off it, that it can fly to;
        /// the way there begun. False if there is none.
        inline bool route(AiCtx &c, const fx *threat, fx viewOfs, int minDist, int maxDist, bool wantSeen)
        {
            const ActorWorld &a = c.a;
            if (a.nodeCount == 0)
                return false;
            const int mine = nearest(c, c.me.pos);
            if (mine < 0)
                return false;
            std::uint8_t reached[kMaxPathNodes];
            pathReach(a, mine, kPathLinkFly, false, reached);
            reached[mine] = 1;
            const fx looker[3] = {threat[0], threat[1], threat[2] + viewOfs};
            int pause = 0;
            for (int i = 0; i < a.nodeCount; ++i)
            {
                thinkPoint(pause, 8);
                const int n = (i + c.ai.lastCoverSearch) % a.nodeCount;
                c.ai.lastCoverSearch = n + 1;
                if (!reached[n])
                    continue;
                const fx *p = a.nodes[n].pos;
                const fx dist = aiDist(threat, p);
                if (dist <= fxFromInt(minDist) || dist >= fxFromInt(maxDist))
                    continue;
                fx from[3];
                middle(p, from);
                if (lineClear(*c.w.world, from, looker) != wantSeen)
                    continue;
                aiRouteNew(c, kMoveGoalLocation, p);
                c.m.movementActivity = kActWalk;
                return true;
            }
            return false;
        }
    } // namespace fly

    /// A frame of flight (the controller's Move and MoveExecute): towards
    /// its goal straight where it can, else by the ways for what flies, a
    /// node at a time; its speed eased, as the game's m_velocity is.
    inline void aiFlyTick(AiCtx &c, int ms)
    {
        if (c.me.ai >= kAiDying || c.m.inAir)
            return;
        fx want[3] = {0, 0, 0};
        const fx speed = fxFromInt(100);
        if (c.m.movementGoal != kMoveGoalNone && c.m.moveWaitFinished <= c.ai.now)
        {
            fx dest[3];
            aiMoveDest(c, dest);
            fx to[3] = {dest[0], dest[1], dest[2]};
            if (aiDist(dest, c.me.pos) < fxFromInt(24))
            {
                aiMovementComplete(c);
            }
            else
            {
                // (A way round, looked for twice a second while it has none
                // straight.)
                if (!fly::clear(c, c.me.pos, dest))
                {
                    c.me.thinkMs -= ms;
                    if (c.me.thinkMs <= 0 || c.me.goalNode < 0)
                    {
                        c.me.thinkMs = 500;
                        const int mine = fly::nearest(c, c.me.pos);
                        const int theirs = fly::nearest(c, dest);
                        const std::uint8_t next = mine >= 0 && theirs >= 0 && mine != theirs
                                                      ? pathStep(c.a, mine, theirs, kPathLinkFly, false)
                                                      : kNoPathNode;
                        c.me.goalNode = static_cast<std::int16_t>(next == kNoPathNode ? (mine >= 0 && aiDist(c.a.nodes[mine].pos, c.me.pos) > fxFromInt(32) ? mine : -1) : next);
                    }
                    if (c.me.goalNode >= 0)
                    {
                        for (int k = 0; k < 3; ++k)
                            to[k] = c.a.nodes[c.me.goalNode].pos[k];
                        if (aiDist(to, c.me.pos) < fxFromInt(24))
                            c.me.thinkMs = 0;
                    }
                }
                const fx d[3] = {to[0] - c.me.pos[0], to[1] - c.me.pos[1], to[2] - c.me.pos[2]};
                const fx far = fxLength(d);
                for (int k = 0; far > 0 && k < 3; ++k)
                    want[k] = static_cast<fx>((static_cast<std::int64_t>(d[k]) * speed) / far);
            }
        }
        // m_velocity = m_velocity * 0.8 + vecDir * speed * 0.2, a tenth of a
        // second at a time.
        const int keep = ms >= 100 ? 205 : 1024 - (ms * 819) / 100;
        for (int k = 0; k < 3; ++k)
            c.me.velocity[k] = static_cast<fx>((static_cast<std::int64_t>(c.me.velocity[k]) * keep +
                                                static_cast<std::int64_t>(want[k]) * (1024 - keep)) >> 10);
        fx step[3], from[3], to[3];
        for (int k = 0; k < 3; ++k)
            step[k] = static_cast<fx>((static_cast<std::int64_t>(c.me.velocity[k]) * ms) / 1000);
        if ((step[0] | step[1] | step[2]) == 0)
            return;
        fly::middle(c.me.pos, from);
        for (int k = 0; k < 3; ++k)
            to[k] = from[k] + step[k];
        const Trace t = detail::walkerTrace(*c.w.world, &c.a, c.index, false, from, to);
        if (t.startSolid)
            return;
        c.me.pos[0] = t.endPos[0];
        c.me.pos[1] = t.endPos[1];
        c.me.pos[2] = t.endPos[2] - kStandingFeet;
        actorPlaceBox(c.a, c.index);
        if (t.fraction < kFractionOne && (c.m.moveBlockedMs += ms) > 2000 && c.m.movementGoal != kMoveGoalNone)
        {
            c.m.moveBlockedMs = 0;
            aiRemember(c, kMemoryMoveFailed);
            aiTaskFail(c);
            aiRouteClear(c);
        }
        else if (t.fraction >= kFractionOne)
        {
            c.m.moveBlockedMs = 0;
        }
    }

    // ============================================================ controller

    namespace controller
    {
        inline constexpr int kHeadOpen = 1, kBallShoot = 2, kSmallShoot = 3, kPowerupFull = 4, kPowerupHalf = 5;
        // m_flShootTime, m_flShootEnd: time1, time2; m_fInCombat: flag1;
        // m_vecEstVelocity: vec1.

        inline constexpr AiTask tlControllerChaseEnemy[] = {{kTaskGetPathToEnemy, 128}, {kTaskWaitForMovement, 0}};
        inline constexpr AiSchedule slControllerChaseEnemy = {tlControllerChaseEnemy, 2,
                                                              kCondNewEnemy | kCondTaskFailed, 0,
                                                              "ControllerChaseEnemy"};
        inline constexpr AiTask tlControllerStrafe[] = {
            {kTaskWait, 200}, {kTaskGetPathToEnemy, 128}, {kTaskWaitForMovement, 0}, {kTaskWait, 1000}};
        inline constexpr AiSchedule slControllerStrafe = {tlControllerStrafe, 4, kCondNewEnemy, 0, "ControllerStrafe"};
        inline constexpr AiTask tlControllerTakeCover[] = {
            {kTaskWait, 200}, {kTaskFindCoverFromEnemy, 0}, {kTaskWaitForMovement, 0}, {kTaskWait, 1000}};
        inline constexpr AiSchedule slControllerTakeCover = {tlControllerTakeCover, 4, kCondNewEnemy, 0,
                                                             "ControllerTakeCover"};
        inline constexpr AiTask tlControllerFail[] = {
            {kTaskStopMoving, 0}, {kTaskSetActivity, kActIdle}, {kTaskWait, 2000}, {kTaskWaitPvs, 0}};
        inline constexpr AiSchedule slControllerFail = {tlControllerFail, 4, 0, 0, "ControllerFail"};

        inline void sound(AiCtx &c, int which)
        {
            switch (which)
            {
            case kAiSoundIdle:  detail::aiSay(c, CommonSound::ControllerIdle, aiRandomLong(c.a, 95, 105)); break;
            case kAiSoundAlert: detail::aiSay(c, CommonSound::ControllerAlert, aiRandomLong(c.a, 95, 105)); break;
            case kAiSoundPain:
                if (aiRandomLong(c.a, 0, 5) < 2)
                    detail::aiSay(c, aiRandomLong(c.a, 0, 1) == 0 ? CommonSound::ControllerPain1 : CommonSound::ControllerPain2,
                                 aiRandomLong(c.a, 95, 105));
                break;
            case kAiSoundDeath: detail::aiSay(c, CommonSound::ControllerDie, aiRandomLong(c.a, 95, 105)); break;
            default:            break;
            }
        }

        inline int yawSpeed(AiCtx &) { return 120; }

        /// Its head, where the big ball comes from; its hands, the small ones.
        inline void headPos(const AiCtx &c, fx *at)
        {
            at[0] = c.me.pos[0];
            at[1] = c.me.pos[1];
            at[2] = c.me.pos[2] + fxFromInt(64);
        }
        inline void handPos(const AiCtx &c, fx *at)
        {
            fx forward[3];
            aiForward(c.me.yaw, forward);
            at[0] = c.me.pos[0] + forward[0] * 32;
            at[1] = c.me.pos[1] + forward[1] * 32;
            at[2] = c.me.pos[2] + fxFromInt(40);
        }

        inline bool checkRange1(AiCtx &, fx dot, fx dist)
        {
            return dot > fxDot(0.5) && dist > fxFromInt(256) && dist <= fxFromInt(2048);
        }
        inline bool checkRange2(AiCtx &, fx dot, fx dist)
        {
            return dot > fxDot(0.5) && dist > fxFromInt(64) && dist <= fxFromInt(2048);
        }
        inline bool checkMelee1(AiCtx &, fx, fx) { return false; }

        inline void animEvent(AiCtx &c, int event)
        {
            switch (event)
            {
            case kHeadOpen:
            {
                fx at[3];
                headPos(c, at);
                effectAdd(c.a, kEffectMuzzle, at, nullptr, 8, 400); // (its head lit)
                break;
            }
            case kBallShoot:
            {
                // The big one, from its head, slowly up and after its enemy.
                fx at[3];
                headPos(c, at);
                const fx velocity[3] = {0, 0, fxFromInt(32)};
                projectileAdd(c.a, kShotHeadBall, at, velocity, 5000);
                break;
            }
            case kSmallShoot:
                // A stream of small ones, five a second, for the next while
                // (the event's own length: a second, as the model has it).
                detail::aiSay(c, CommonSound::ControllerAttack, aiRandomLong(c.a, 95, 105));
                c.m.time1 = c.ai.now;
                c.m.time2 = c.ai.now + 1000;
                break;
            default:
                break;
            }
        }

        /// The small balls owed since it last looked: towards where its enemy
        /// will be, as near as the game's Intersect has it -- here, where it
        /// is, with a little spread.
        inline void shoot(AiCtx &c)
        {
            if (c.m.time2 <= c.ai.now && c.m.time1 >= c.m.time2)
                return;
            AiTarget t;
            const bool target = c.m.enemy != kAiNone && aiTargetOf(c.w, c.player, c.m.enemy, t);
            while (c.m.time1 < c.m.time2 && c.m.time1 < c.ai.now)
            {
                if (target)
                {
                    fx hand[3];
                    handPos(c, hand);
                    fx d[3] = {t.center[0] - hand[0], t.center[1] - hand[1], t.center[2] - hand[2]};
                    const fx far = fxLength(d);
                    fx velocity[3];
                    for (int k = 0; k < 3; ++k)
                    {
                        const fx unit = far > 0 ? static_cast<fx>((static_cast<std::int64_t>(d[k]) << kFxShift) / far) : 0;
                        velocity[k] = unit * skillOf(c.w).controllerSpeedball + fxFromInt(aiRandomLong(c.a, -28, 28));
                    }
                    projectileAdd(c.a, kShotZapBall, hand, velocity, 5000);
                }
                c.m.time1 += 200;
            }
            if (c.m.time1 >= c.m.time2)
                c.m.flag1 = false;
        }

        /// LookupFloat: its float clip by the way it is going.
        inline void floatClip(AiCtx &c)
        {
            const ModelView *model = actorModel(c.a, c.me);
            if (model == nullptr)
                return;
            const fx *v = c.me.velocity;
            const char *name = "up";
            if (fxLength(v) >= fxFromInt(32))
            {
                fx forward[3], right[3];
                aiForward(c.me.yaw, forward);
                aiRight(c.me.yaw, right);
                const std::int64_t x = static_cast<std::int64_t>(forward[0]) * v[0] + static_cast<std::int64_t>(forward[1]) * v[1];
                const std::int64_t y = static_cast<std::int64_t>(right[0]) * v[0] + static_cast<std::int64_t>(right[1]) * v[1];
                const std::int64_t z = static_cast<std::int64_t>(v[2]) * kFxOne;
                const std::int64_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, az = z < 0 ? -z : z;
                if (ax > ay && ax > az)
                    name = x > 0 ? "forward" : "backward";
                else if (ay > az)
                    name = y > 0 ? "right" : "left";
                else
                    name = z > 0 ? "up" : "down";
            }
            int clip = model->findClip(nameHash(name));
            if (clip < 0)
                clip = model->findClip(nameHash("float"));
            if (clip >= 0 && (c.m.seqFinished || c.me.clip != clip))
                aiPlayClip(c, clip, c.me.clip != clip);
        }

        inline bool startTask(AiCtx &c, const AiTask &task)
        {
            switch (task.task)
            {
            case kTaskGetPathToEnemy:
            case kTaskGetPathToEnemyLkp:
            {
                AiTarget t;
                const bool lkp = task.task == kTaskGetPathToEnemyLkp;
                if (!lkp && (c.m.enemy == kAiNone || !aiTargetOf(c.w, c.player, c.m.enemy, t)))
                {
                    aiTaskFail(c);
                    return true;
                }
                fx at[3];
                if (lkp)
                    for (int k = 0; k < 3; ++k)
                        at[k] = c.m.enemyLkp[k];
                else
                    aiFeetOf(t, at);
                const fx viewOfs = lkp ? 0 : t.eye[2] - t.origin[2];
                if (fly::route(c, at, viewOfs, task.data, fxToInt(aiDist(at, c.me.pos)) + 1024, true))
                    aiTaskComplete(c);
                else
                    aiTaskFail(c);
                return true;
            }
            case kTaskFindCoverFromEnemy:
            {
                AiTarget t;
                if (c.m.enemy == kAiNone || !aiTargetOf(c.w, c.player, c.m.enemy, t))
                {
                    aiTaskFail(c);
                    return true;
                }
                fx at[3];
                aiFeetOf(t, at);
                if (fly::route(c, at, t.eye[2] - t.origin[2], 0, c.kind.coverRadius, false))
                    aiTaskComplete(c);
                else
                    aiTaskFail(c);
                return true;
            }
            case kTaskStopMoving:
                aiRouteClear(c);
                aiTaskComplete(c);
                return true;
            default:
                return false;
            }
        }

        /// RunTask: its small balls go out whatever the task; and while it
        /// waits, it faces its enemy and throws what it can.
        inline bool runTask(AiCtx &c, const AiTask &task)
        {
            shoot(c);
            switch (task.task)
            {
            case kTaskWaitForMovement:
            case kTaskWait:
            case kTaskWaitFaceEnemy:
            case kTaskWaitPvs:
            {
                aiMakeIdealYaw(c, c.m.enemyLkp);
                aiChangeYaw(c);
                if (c.m.seqFinished)
                    c.m.flag1 = false;
                if (!c.m.flag1)
                {
                    const std::uint8_t attack = aiHas(c, kCondCanRangeAttack1)   ? kActRangeAttack1
                                                : aiHas(c, kCondCanRangeAttack2) ? kActRangeAttack2
                                                                                 : kActReset;
                    const int clip = attack != kActReset ? aiLookupActivity(c, attack) : -1;
                    if (clip >= 0)
                    {
                        aiPlayClip(c, clip, true);
                        c.m.flag1 = true;
                    }
                    else
                    {
                        floatClip(c);
                    }
                }
                return false; // (and the wait goes on as any monster's)
            }
            default:
                return false;
            }
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            switch (type)
            {
            case kSchedChaseEnemy:         return &slControllerChaseEnemy;
            case kSchedRangeAttack1:       return &slControllerStrafe;
            case kSchedRangeAttack2:
            case kSchedMeleeAttack1:
            case kSchedMeleeAttack2:
            case kSchedTakeCoverFromEnemy: return &slControllerTakeCover;
            case kSchedFail:               return &slControllerFail;
            default:                       break;
            }
            return aiBaseScheduleOfType(c, type);
        }

        /// Killed: down it falls.
        inline bool killed(AiCtx &c)
        {
            c.m.inAir = true;
            c.me.velocity[2] = 0;
            return false;
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Controller);
                k.health = 60;
                k.eye = fxFromInt(40); // (the game's -2 is under its feet: its eyes are its head's)
                k.fov = fxDot(-1.0);   // VIEW_FIELD_FULL
                k.capability = kCapFly | kCapSquad;
                k.blood = kBloodYellow;
                k.squadMonster = true;
                k.sound = sound;
                k.yawSpeed = yawSpeed;
                k.checkRange1 = checkRange1;
                k.checkRange2 = checkRange2;
                k.checkMelee1 = checkMelee1;
                k.animEvent = animEvent;
                k.startTask = startTask;
                k.runTask = runTask;
                k.scheduleOfType = scheduleOfType;
                k.killed = killed;
                return k;
            }();
            return k;
        }
    } // namespace controller

    // ================================================================= leech

    namespace leech
    {
        inline constexpr int kAttack = 1;       ///< LEECH_AE_ATTACK
        inline constexpr int kCheckDist = 45;   ///< LEECH_CHECK_DIST
        inline constexpr int kSwimSpeed = 50;   ///< LEECH_SWIM_SPEED
        inline constexpr int kSwimAccel = 80;   ///< LEECH_SWIM_ACCEL
        inline constexpr int kSwimDecel = 10;   ///< LEECH_SWIM_DECEL
        inline constexpr int kTurnRate = 90;    ///< LEECH_TURN_RATE
        inline constexpr int kSizeX = 10;       ///< LEECH_SIZEX
        // m_top, m_bottom, m_height: vec1; pev->speed: value1 (units/s);
        // m_flTurning: count1 (degrees/s); m_obstacle: value2 (Q12, 1.0 = clear);
        // m_stateTime, m_waterTime, m_zTime: time1..3; m_sideTime:
        // moveWaitFinished; m_attackSoundTime: moveBlockedMs (ms of its
        // clock); pev->oldorigin: lastPosition; m_fPathBlocked: flag1;
        // pitch: aimPitch (degrees Q12).

        inline bool inWater(const AiCtx &c, const fx *at)
        {
            return pointContents(*c.w.world, at) == kContentsWater;
        }

        /// RecalculateWaterlevel: its water's floor and top, a little in from
        /// each, and a height to swim at between them.
        inline void waterLevel(AiCtx &c)
        {
            const fx down[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] - fxFromInt(400)};
            const Shot floor = traceShot(*c.w.world, c.me.pos, down);
            fx bottom = c.me.pos[2] - fxFromInt(400);
            if (floor.fraction < kFractionOne)
                bottom = c.me.pos[2] + static_cast<fx>((static_cast<std::int64_t>(down[2] - c.me.pos[2]) * floor.fraction) >> kFractionShift) + kFxOne;
            // UTIL_WaterLevel: the top, in halvings.
            fx lo = c.me.pos[2], hi = c.me.pos[2] + fxFromInt(400);
            for (int n = 0; n < 9; ++n)
            {
                const fx mid = (lo + hi) / 2;
                const fx at[3] = {c.me.pos[0], c.me.pos[1], mid};
                if (inWater(c, at))
                    lo = mid;
                else
                    hi = mid;
            }
            const fx top = lo - kFxOne;
            c.m.vec1[1] = static_cast<fx>((static_cast<std::int64_t>(bottom) * 4 + top) / 5);
            c.m.vec1[0] = static_cast<fx>((static_cast<std::int64_t>(bottom) + static_cast<std::int64_t>(top) * 4) / 5);
            c.m.vec1[2] = c.m.vec1[1] + static_cast<fx>((static_cast<std::int64_t>(c.m.vec1[0] - c.m.vec1[1]) * aiRandomLong(c.a, 0, 1000)) / 1000);
            c.m.time2 = c.ai.now + aiRandomMs(c.a, 5000, 7000);
        }

        /// SwitchLeechState: from fighting back to swimming about; or, the
        /// player seen in the water, to going for them.
        inline void switchState(AiCtx &c)
        {
            c.m.time1 = c.ai.now + aiRandomMs(c.a, 3000, 6000);
            if (c.m.state == kStateCombat)
            {
                c.m.enemy = kAiNone;
                c.m.state = c.m.idealState = kStateIdle;
                c.m.moveWaitFinished = 0;
                return;
            }
            AiTarget t;
            if (c.player.health > 0 && !unseen(c.player) && c.player.waterLevel != 0 &&
                aiDist(c.player.origin, c.me.pos) < fxFromInt(750) && aiTargetOf(c.w, c.player, kAiPlayer, t) &&
                lineClear(*c.w.world, c.me.pos, t.eye))
            {
                c.m.enemy = kAiPlayer;
                c.m.state = c.m.idealState = kStateCombat;
                c.m.time1 = c.ai.now + aiRandomMs(c.a, 18000, 25000);
                // (At ATTN_NORM * 0.5.)
                if (c.w.heard != nullptr)
                    c.w.heard->add(CommonSound::LeechAlert, c.me.pos, 127, 2500);
            }
        }

        /// The way it faces, as a unit vector: its yaw, and its pitch.
        inline void facing(const AiCtx &c, fx *forward)
        {
            fx sine, cosine, ps, pc;
            fxSinCos(c.me.yaw, sine, cosine);
            fxSinCos(static_cast<std::uint16_t>((static_cast<std::int64_t>(c.me.aimPitch) * 65536) / (360 * kFxOne)), ps, pc);
            forward[0] = fxMulShort(cosine, pc); // (sines and cosines: 16-bit)
            forward[1] = fxMulShort(sine, pc);
            forward[2] = ps;
        }

        /// How much of a feeler's length is clear (Q12), three of them.
        inline fx feeler(AiCtx &c, const fx *offset, const fx *forward)
        {
            const fx to[3] = {c.me.pos[0] + offset[0] + forward[0] * kCheckDist,
                              c.me.pos[1] + offset[1] + forward[1] * kCheckDist,
                              c.me.pos[2] + offset[2] + forward[2] * kCheckDist};
            const Shot shot = traceShot(*c.w.world, c.me.pos, to);
            return static_cast<fx>(shot.fraction >> (kFractionShift - kFxShift));
        }

        /// ObstacleDistance
        inline fx obstacle(AiCtx &c, const fx *forward, bool atTarget)
        {
            const fx zero[3] = {0, 0, 0};
            const fx ahead = feeler(c, zero, forward);
            if (ahead < kFxOne)
            {
                if (!atTarget || c.m.vec1[2] - c.me.pos[2] > fxFromInt(10) || c.m.vec1[2] - c.me.pos[2] < -fxFromInt(10))
                    return ahead;
            }
            if (c.m.moveWaitFinished < c.ai.now)
            {
                fx right[3];
                aiRight(c.me.yaw, right);
                for (int side = 1; side >= -1; side -= 2)
                {
                    const fx off[3] = {right[0] * kSizeX * 2 * side, right[1] * kSizeX * 2 * side, 0};
                    const fx f = feeler(c, off, forward);
                    if (f < kFxOne)
                        return f;
                }
                c.m.moveWaitFinished = c.ai.now + aiRandomMs(c.a, 500, 1000);
            }
            return kFxOne;
        }

        inline fx approach(fx target, fx value, fx speed)
        {
            const fx delta = target - value;
            return delta > speed ? value + speed : (delta < -speed ? value - speed : target);
        }

        /// SwimThink: ten times a second while the player may see it.
        inline void think(AiCtx &c)
        {
            if (c.me.ai >= kAiDying)
                return;
            if (!aiClientInPvs(c))
            {
                c.m.thinkMs += aiRandomMs(c.a, 900, 1400);
                c.me.velocity[0] = c.me.velocity[1] = c.me.velocity[2] = 0;
                return;
            }
            if (c.m.vec1[0] == 0 && c.m.vec1[1] == 0)
                waterLevel(c);
            fx targetSpeed = fxFromInt(kSwimSpeed);
            int targetYaw = 0;
            if (c.m.time2 < c.ai.now)
                waterLevel(c);
            if (c.m.time1 < c.ai.now)
                switchState(c);
            aiClear(c, kCondCanMeleeAttack1);
            bool atTarget = false;
            if (c.m.state == kStateCombat)
            {
                AiTarget t;
                if (c.m.enemy == kAiNone || !aiTargetOf(c.w, c.player, c.m.enemy, t))
                {
                    switchState(c);
                }
                else
                {
                    atTarget = true;
                    c.m.vec1[2] = std::clamp(t.eye[2] - fxFromInt(5), c.m.vec1[1], c.m.vec1[0]);
                    const fx at[3] = {t.eye[0] - c.me.pos[0], t.eye[1] - c.me.pos[1], t.eye[2] - c.me.pos[2]};
                    if (fxLength(at) < fxFromInt(40))
                        aiSet(c, kCondCanMeleeAttack1);
                    const std::uint16_t to = fxAtan2Turns(at[1], at[0]);
                    targetYaw = static_cast<std::int16_t>(to - c.me.yaw) * 360 / 65536;
                    const int most = kTurnRate * 3 / 4;
                    if (targetYaw < -most)
                        targetYaw = -most;
                    else if (targetYaw > most)
                        targetYaw = most;
                    else
                        targetSpeed *= 2;
                }
            }
            else
            {
                if (c.m.time3 < c.ai.now)
                {
                    const fx newHeight = c.m.vec1[1] + static_cast<fx>((static_cast<std::int64_t>(c.m.vec1[0] - c.m.vec1[1]) * aiRandomLong(c.a, 0, 1000)) / 1000);
                    c.m.vec1[2] = c.m.vec1[2] / 2 + newHeight / 2;
                    c.m.time3 = c.ai.now + aiRandomMs(c.a, 1000, 4000);
                }
                if (aiRandomLong(c.a, 0, 100) < 10)
                    targetYaw = aiRandomLong(c.a, -30, 30);
                if (aiDist(c.me.pos, c.m.lastPosition) < kFxOne)
                    c.m.moveWaitFinished = 0;
            }
            fx forward[3];
            facing(c, forward);
            fx obstacleOf = obstacle(c, forward, atTarget);
            for (int k = 0; k < 3; ++k)
                c.m.lastPosition[k] = c.me.pos[k];
            if (obstacleOf < kFxOne / 10)
                obstacleOf = kFxOne / 10;
            // (Its speed and turn as a tenth of a second has them.)
            if (obstacleOf >= kFxOne)
            {
                c.m.count1 = 0;
                c.m.flag1 = false;
                c.m.value1 = approach(targetSpeed, c.m.value1, fxFromInt(kSwimAccel) / 10);
            }
            else
            {
                const fx inverse = static_cast<fx>((static_cast<std::int64_t>(kFxOne) << kFxShift) / obstacleOf);
                c.m.value2 = inverse;
                c.m.flag1 = true;
                if (c.m.count1 == 0)
                {
                    fx right[3];
                    aiRight(c.me.yaw, right);
                    const fx r[3] = {right[0] * kSizeX, right[1] * kSizeX, 0};
                    const fx l[3] = {-r[0], -r[1], 0};
                    const fx delta = feeler(c, r, forward) - feeler(c, l, forward);
                    c.m.count1 = delta > kFxOne / 10 || (delta > -kFxOne / 10 && aiRandomLong(c.a, 0, 100) < 50)
                                     ? -kTurnRate : kTurnRate;
                }
                c.m.value1 = approach(-fxFromInt(kSwimSpeed) / 2, c.m.value1,
                                      static_cast<fx>((static_cast<std::int64_t>(fxFromInt(kSwimDecel)) * inverse) >> kFxShift) / 10);
            }
            // UpdateMotion: its turn, its pitch towards its height, and the
            // bite when it is on what it is after.
            const int idealYaw = c.m.count1 + targetYaw; // degrees a second
            c.m.idealYaw = static_cast<std::uint16_t>(idealYaw * 65536 / 360);
            const fx delta = c.m.vec1[2] - c.me.pos[2];
            const fx targetPitch = delta < -fxFromInt(10) ? -fxFromInt(30) : (delta > fxFromInt(10) ? fxFromInt(30) : 0);
            c.me.aimPitch = approach(targetPitch, c.me.aimPitch, fxFromInt(6));
            std::uint8_t want = kActSwim;
            if (idealYaw * (c.m.flag1 ? fxToInt(c.m.value2) : 1) > 150)
                want = kActTurnLeft;
            else if (idealYaw * (c.m.flag1 ? fxToInt(c.m.value2) : 1) < -150)
                want = kActTurnRight;
            if (c.m.state == kStateCombat && aiHas(c, kCondCanMeleeAttack1))
                want = kActMeleeAttack1;
            if (c.m.activity != want)
                aiSetActivity(c, want);
        }

        /// Each frame: it goes as its speed, turn and pitch have it, through
        /// the water (MOVETYPE_FLY) -- or, out of it, falls.
        inline void frame(AiCtx &c, int ms)
        {
            if (c.me.ai >= kAiDying)
            {
                // DeadThink: it floats up, or falls, slowly.
                if (!c.m.inAir)
                {
                    const fx up[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + static_cast<fx>((static_cast<std::int64_t>(fxFromInt(8)) * ms) / 1000)};
                    if (inWater(c, up) && traceShot(*c.w.world, c.me.pos, up).fraction >= kFractionOne)
                        c.me.pos[2] = up[2];
                }
                return;
            }
            if (!inWater(c, c.me.pos))
            {
                // (Out of the water: down, twitching.)
                if (!c.m.inAir)
                {
                    c.m.inAir = true;
                    c.me.velocity[0] = c.me.velocity[1] = c.me.velocity[2] = 0;
                }
                return;
            }
            c.m.inAir = false;
            const int turn = c.m.count1 + 0; // (degrees a second, as idealYaw has it)
            (void)turn;
            const int yawRate = static_cast<std::int16_t>(c.m.idealYaw) * 360 / 65536;
            const int scaled = c.m.flag1 ? static_cast<int>((static_cast<std::int64_t>(yawRate) * c.m.value2) >> kFxShift) : yawRate;
            c.me.yaw = static_cast<std::uint16_t>(c.me.yaw + scaled * 65536 / 360 * ms / 1000);
            fx forward[3];
            facing(c, forward);
            for (int k = 0; k < 3; ++k)
                c.me.velocity[k] = static_cast<fx>((static_cast<std::int64_t>(forward[k]) * c.m.value1) >> kFxShift);
            fx to[3];
            for (int k = 0; k < 3; ++k)
                to[k] = c.me.pos[k] + static_cast<fx>((static_cast<std::int64_t>(c.me.velocity[k]) * ms) / 1000);
            const Shot shot = traceShot(*c.w.world, c.me.pos, to);
            if (shot.fraction >= kFractionOne && inWater(c, to))
            {
                for (int k = 0; k < 3; ++k)
                    c.me.pos[k] = to[k];
                actorPlaceBox(c.a, c.index);
            }
        }

        inline void animEvent(AiCtx &c, int event)
        {
            if (event != kAttack)
                return;
            if (c.ai.now > c.m.moveBlockedMs)
            {
                detail::aiSay(c, CommonSound::LeechBite);
                c.m.moveBlockedMs = c.ai.now + 500;
            }
            AiTarget t;
            if (c.m.enemy == kAiNone || !aiTargetOf(c.w, c.player, c.m.enemy, t))
                return;
            fx face[3];
            aiForward(c.me.yaw, face);
            fx d[3] = {t.origin[0] - c.me.pos[0], t.origin[1] - c.me.pos[1], 0};
            const fx far = fxLength(d);
            if (far <= 0)
                return;
            const std::int64_t dot = (static_cast<std::int64_t>(d[0]) * face[0] + static_cast<std::int64_t>(d[1]) * face[1]) / far;
            if (dot > fxDot(0.9))
                aiDamageTarget(c, c.m.enemy, skillOf(c.w).leechDmgBite, kDmgSlash);
            c.m.time1 -= 2000;
        }

        /// TakeDamage: knocked back from what hurt it.
        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            c.m.value1 = 0;
            if (d.hasFrom)
            {
                const fx away[3] = {c.me.pos[0] - d.from[0], c.me.pos[1] - d.from[1], c.me.pos[2] - d.from[2]};
                const fx far = fxLength(away);
                for (int k = 0; far > 0 && k < 3; ++k)
                    c.me.velocity[k] = static_cast<fx>((static_cast<std::int64_t>(away[k]) * 25) / (far >> kFxShift > 0 ? far >> kFxShift : 1));
            }
            return true;
        }

        inline bool killed(AiCtx &c)
        {
            c.me.ai = kAiDying;
            c.m.deadflag = 1;
            c.m.state = c.m.idealState = kStateDead;
            c.me.flags &= static_cast<std::uint8_t>(~kActorSolid);
            c.me.aimPitch = 0;
            aiSetActivity(c, inWater(c, c.me.pos) ? kActDieSimple : kActDieForward);
            c.m.inAir = !inWater(c, c.me.pos);
            actorPlaceBox(c.a, c.index);
            return true;
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Leech);
                k.health = 2;
                k.fov = fxDot(-0.5);
                k.blood = kBloodYellow;
                k.think = think;
                k.frame = frame;
                k.animEvent = animEvent;
                k.takeDamage = takeDamage;
                k.killed = killed;
                return k;
            }();
            return k;
        }
    } // namespace leech

    // ================================================================ round 4
    //
    //   gargantua   gargantua.cpp  only what burns, crushes or blows up hurts
    //                              it; swipes up close, a sweep of flame a
    //                              little further off, and from afar a stomp
    //                              that runs along the ground at its enemy
    //                              (HlMonsters.hpp's shots: kShotStomp)
    //   baby crab   headcrab.cpp   (CBabyCrab) a headcrab, smaller, weaker,
    //                              higher-voiced, that leaps from closer
    //   ichthyosaur ichthyosaur.cpp its own swimming think, as the leech's:
    //                              about its water, round what it is after,
    //                              at it, and a bite that shakes

    // ============================================================== gargantua

    namespace garg
    {
        inline constexpr int kAttackDist = 80;   ///< GARG_ATTACKDIST
        inline constexpr int kFlameLength = 330; ///< GARG_FLAME_LENGTH
        inline constexpr int kSlashLeft = 1;     ///< GARG_AE_SLASH_LEFT
        inline constexpr int kLeftFoot = 3;      ///< GARG_AE_LEFT_FOOT
        inline constexpr int kRightFoot = 4;     ///< GARG_AE_RIGHT_FOOT
        inline constexpr int kStomp = 5;         ///< GARG_AE_STOMP
        inline constexpr int kBreathe = 6;       ///< GARG_AE_BREATHE
        // sk_gargantua_health at "normal" (aiInit gives the game's level's).
        inline constexpr int kHealth = 800;
        /// GARG_DAMAGE: all that hurts it (DMG_MORTAR is a blast here).
        inline constexpr std::uint32_t kHurtBy = kDmgEnergyBeam | kDmgCrush | kDmgBlast;
        // m_flameTime: time1; m_seeTime: time2; m_painSoundTime: time3; its
        // flames are on: flag1; m_flameY, m_flameX: aimYaw, aimPitch (65536
        // to the circle, as its bone controllers have them); how many of the
        // blasts it dies in have gone off: count1.

        enum : std::uint8_t
        {
            kTaskSoundAttack = kTaskLastCommon + 1, ///< TASK_SOUND_ATTACK
            kTaskFlameSweep,                        ///< TASK_FLAME_SWEEP
        };

        inline constexpr AiTask tlFlame[] = {
            {kTaskStopMoving, 0},
            {kTaskFaceEnemy, 0},
            {kTaskSoundAttack, 0},
            {kTaskSetActivity, kActMeleeAttack2},
            {kTaskFlameSweep, 4500},
            {kTaskSetActivity, kActIdle},
        };
        inline constexpr AiSchedule slFlame = {tlFlame, 6, 0, 0, "GargFlame"};
        inline constexpr AiTask tlSwipe[] = {
            {kTaskStopMoving, 0},
            {kTaskFaceEnemy, 0},
            {kTaskMeleeAttack1, 0},
        };
        inline constexpr AiSchedule slSwipe = {tlSwipe, 3, kCondCanMeleeAttack2, 0, "GargSwipe"};

        inline int yawSpeed(AiCtx &c)
        {
            return c.m.activity == kActTurnLeft || c.m.activity == kActTurnRight ? 180 : 60;
        }

        /// Where a flame comes out: its two arms. (The game's attachments 1
        /// and 2, on the forearms; this port does not pose a monster to
        /// think, so they are where the arms are held to throw it.)
        inline void nozzle(const AiCtx &c, int side, fx *at)
        {
            fx forward[3], right[3];
            aiForward(c.me.yaw, forward);
            aiRight(c.me.yaw, right);
            for (int k = 0; k < 3; ++k)
                at[k] = c.me.pos[k] + forward[k] * 48 + right[k] * (side == 0 ? 40 : -40);
            at[2] += fxFromInt(100);
        }

        /// FlameDamage: whatever is near the flame's line and that it can
        /// reach, less the further from it -- not the gargantua's own kind.
        inline void flameDamage(AiCtx &c, const fx *start, const fx *end)
        {
            fx mid[3], aim[3];
            for (int k = 0; k < 3; ++k)
            {
                mid[k] = (start[k] + end[k]) / 2;
                aim[k] = end[k] - start[k];
            }
            const fx length = fxLength(aim);
            if (length <= 0)
                return;
            for (int k = 0; k < 3; ++k)
                aim[k] = static_cast<fx>((static_cast<std::int64_t>(aim[k]) << kFxShift) / length);
            const fx radius = length / 2;
            auto burn = [&](std::int16_t id, const fx *spot)
            {
                const fx d[3] = {spot[0] - mid[0], spot[1] - mid[1], spot[2] - mid[2]};
                if (fxLength(d) > radius + fxFromInt(64))
                    return;
                fx along = fxMul(aim[0], d[0]) + fxMul(aim[1], d[1]) + fxMul(aim[2], d[2]);
                along = along > radius ? radius : (along < -radius ? radius : along); // (the game's, both ways)
                fx src[3];
                for (int k = 0; k < 3; ++k)
                    src[k] = mid[k] + fxMul(aim[k], along);
                if (!lineClear(*c.w.world, src, spot))
                    return;
                const fx off[3] = {spot[0] - src[0], spot[1] - src[1], spot[2] - src[2]};
                const int far = fxToInt(fxLength(off));
                const int fire = skillOf(c.w).gargantuaDmgFire;
                const int damage = far > 64 ? fire - (far - 64) * 2 / 5 : fire;
                if (damage > 0)
                    aiDamageTarget(c, id, damage, kDmgBurn);
            };
            if (c.player.health > 0)
                burn(kAiPlayer, c.player.origin);
            for (int i = 0; i < c.a.count; ++i)
            {
                const ActorState &other = c.a.states[i];
                if (i == c.index || !other.active || other.ai >= kAiDying || !canBeHurt(other.kind) ||
                    kindClass(other.kind) == kClassAlienMonster)
                    continue;
                fx lo[3], hi[3];
                targetBox(other, lo, hi);
                const fx spot[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
                burn(static_cast<std::int16_t>(i), spot);
            }
        }

        /// FlameUpdate: each of the two flames to where it is aimed, and what
        /// it reaches burned. (Drawn as the game's two beams each: the long
        /// orange one, and a short blue one in it.)
        inline void flameUpdate(AiCtx &c)
        {
            const std::uint16_t yaw = static_cast<std::uint16_t>(c.me.yaw + c.me.aimYaw);
            fx sine, cosine, ps, pc;
            fxSinCos(yaw, sine, cosine);
            fxSinCos(static_cast<std::uint16_t>(-c.me.aimPitch), ps, pc);
            const fx forward[3] = {fxMulShort(cosine, pc), fxMulShort(sine, pc), ps};
            for (int side = 0; side < 2; ++side)
            {
                fx start[3], end[3];
                nozzle(c, side, start);
                aiTraceLine(c, start, forward, kFlameLength, end);
                Effect &outer = effectAdd(c.a, kEffectBeam, start, end, 12, 150);
                outer.colour = 0x2E1F; // (255, 130, 90)
                fx inner[3];
                for (int k = 0; k < 3; ++k)
                    inner[k] = start[k] + (end[k] - start[k]) * 2 / 5;
                Effect &core = effectAdd(c.a, kEffectBeam, start, inner, 6, 150);
                core.colour = 0x7DE0; // (0, 120, 255)
                flameDamage(c, start, end);
            }
        }

        /// FlameControls: the flames turned towards where it wants them, a
        /// little at a time, within 45 degrees either way.
        inline void flameControls(AiCtx &c, int angleX, int angleY)
        {
            angleY = angleY < -8192 ? -8192 : (angleY > 8192 ? 8192 : angleY);
            auto approach = [](int target, int value, int speed)
            {
                const int d = static_cast<std::int16_t>(target - value);
                return d > speed ? value + speed : (d < -speed ? value - speed : target);
            };
            // (4 and 8 degrees a think.)
            c.me.aimPitch = approach(angleX, c.me.aimPitch, 4 * 65536 / 360);
            c.me.aimYaw = approach(angleY, c.me.aimYaw, 8 * 65536 / 360);
        }

        inline void flameDestroy(AiCtx &c)
        {
            if (c.m.flag1)
                detail::aiSay(c, CommonSound::GargFlameOff);
            c.m.flag1 = false;
        }

        /// StompAttack: a shock that runs along the ground from its feet at
        /// its enemy, as far as the first wall, faster and faster.
        inline void stompAttack(AiCtx &c)
        {
            fx forward[3];
            aiForward(c.me.yaw, forward);
            const fx start[3] = {c.me.pos[0] + forward[0] * 35, c.me.pos[1] + forward[1] * 35, c.me.pos[2] + fxFromInt(60)};
            fx aim[3] = {forward[0], forward[1], 0};
            AiTarget t;
            if (c.m.enemy != kAiNone && aiTargetOf(c.w, c.player, c.m.enemy, t))
            {
                const fx d[3] = {t.center[0] - start[0], t.center[1] - start[1], t.center[2] - start[2]};
                const fx far = fxLength(d);
                for (int k = 0; far > 0 && k < 3; ++k)
                    aim[k] = static_cast<fx>((static_cast<std::int64_t>(d[k]) << kFxShift) / far);
            }
            const fx end[3] = {start[0] + aim[0] * 1024, start[1] + aim[1] * 1024, start[2] + aim[2] * 1024};
            const Shot wall = traceShot(*c.w.world, start, end);
            const int length = static_cast<int>((1024LL * wall.fraction) >> kFractionShift);
            if (Projectile *shot = projectileAdd(c.a, kShotStomp, start, aim, 0))
            {
                shot->sprite = 30;     // (its framerate: how fast it gathers speed)
                shot->leftMs = length; // (and a speed of nothing)
            }
            shakeStart(c.w, fxFromInt(12), 2000);
            detail::aiSay(c, CommonSound::GargStomp, aiRandomLong(c.a, 90, 110));
        }

        inline void animEvent(AiCtx &c, int event)
        {
            switch (event)
            {
            case kSlashLeft:
            {
                const std::int16_t hit = aiHullAttack(c, kAttackDist + 10, skillOf(c.w).gargantuaDmgSlash, kDmgSlash);
                if (hit != kAiNone)
                {
                    detail::aiPunch(c, hit, -fxFromInt(30), fxFromInt(30));
                    fx right[3];
                    aiRight(c.me.yaw, right);
                    const fx shove[3] = {-right[0] * 100, -right[1] * 100, 0};
                    detail::aiShove(c, hit, shove);
                    detail::aiSay(c, CommonSound::ClawStrike, aiRandomLong(c.a, 50, 65));
                }
                else
                {
                    detail::aiSay(c, CommonSound::ClawMiss, aiRandomLong(c.a, 50, 65));
                }
                break;
            }
            case kLeftFoot:
            case kRightFoot:
            {
                const fx d[3] = {c.player.origin[0] - c.me.pos[0], c.player.origin[1] - c.me.pos[1],
                                 c.player.origin[2] - c.me.pos[2]};
                if (fxLength(d) < fxFromInt(750))
                    shakeStart(c.w, fxFromInt(4), 1000);
                detail::aiSay(c, CommonSound::GargStep, aiRandomLong(c.a, 90, 110));
                break;
            }
            case kStomp:
                stompAttack(c);
                c.m.time2 = c.ai.now + 12000;
                break;
            case kBreathe:
                detail::aiSay(c, CommonSound::GargBreathe, aiRandomLong(c.a, 90, 110));
                break;
            default:
                break;
            }
        }

        inline void sound(AiCtx &c, int which)
        {
            switch (which)
            {
            case kAiSoundIdle:  detail::aiSay(c, CommonSound::GargIdle); break;
            case kAiSoundAlert: detail::aiSay(c, CommonSound::GargAlert); break;
            default: break; // (its pain is its TraceAttack's, below)
            }
        }

        inline void prescheduleThink(AiCtx &c)
        {
            if (!aiHas(c, kCondSeeEnemy))
                c.m.time2 = c.ai.now + 5000;
        }

        /// Swipe.
        inline bool checkMelee1(AiCtx &, fx dot, fx dist)
        {
            return dot >= fxDot(0.7) && dist <= fxFromInt(kAttackDist);
        }
        /// Flames.
        inline bool checkMelee2(AiCtx &c, fx dot, fx dist)
        {
            return c.ai.now > c.m.time1 && dot >= fxDot(0.8) && dist > fxFromInt(kAttackDist) &&
                   dist <= fxFromInt(kFlameLength);
        }
        /// Stomp.
        inline bool checkRange1(AiCtx &c, fx dot, fx dist)
        {
            return c.ai.now > c.m.time2 && dot >= fxDot(0.7) && dist > fxFromInt(kAttackDist);
        }

        /// TraceAttack and TakeDamage: what does not burn, crush or blow up
        /// glances off (a hundredth of it, which is nothing); it cries out
        /// at what does.
        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            if (c.me.ai >= kAiDying)
                return true;
            if ((d.type & (kHurtBy | kDmgBlast)) && c.m.time3 < c.ai.now)
            {
                detail::aiSay(c, CommonSound::GargPain);
                c.m.time3 = c.ai.now + aiRandomMs(c.a, 2500, 4000);
            }
            if ((d.type & kHurtBy) == 0)
            {
                d.amount /= 100;
                if (d.type & kDmgBullet)
                {
                    fx spark[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(64)};
                    if (d.hasFrom)
                    {
                        const fx toward[3] = {d.from[0] - spark[0], d.from[1] - spark[1], 0};
                        const fx far = fxLength(toward);
                        for (int k = 0; far > 0 && k < 2; ++k)
                            spark[k] += static_cast<fx>((static_cast<std::int64_t>(toward[k]) * 40) / (far >> kFxShift > 0 ? far >> kFxShift : 1));
                    }
                    effectAdd(c.a, kEffectMuzzle, spark, nullptr, 4, 100); // (UTIL_Ricochet)
                }
            }
            if (d.type & kDmgBlast)
                aiSet(c, kCondLightDamage);
            d.type |= kDmgNeverGib; // (its Killed: GIB_NEVER)
            return true;
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            flameDestroy(c);
            if (type == kSchedMeleeAttack2)
                return &slFlame;
            if (type == kSchedMeleeAttack1)
                return &slSwipe;
            return aiBaseScheduleOfType(c, type);
        }

        inline bool startTask(AiCtx &c, const AiTask &task)
        {
            switch (task.task)
            {
            case kTaskFlameSweep:
                // FlameCreate
                c.m.flag1 = true;
                detail::aiSay(c, CommonSound::GargFlameRun);
                aiSound(c.a, kSoundCombat, c.me.pos, 384, 300);
                c.m.waitFinished = c.ai.now + task.data;
                c.m.time1 = c.ai.now + 6000;
                c.me.aimYaw = c.me.aimPitch = 0;
                return true;
            case kTaskSoundAttack:
                if (aiRandomLong(c.a, 0, 100) < 30)
                    detail::aiSay(c, CommonSound::GargAttack);
                aiTaskComplete(c);
                return true;
            case kTaskDie:
                // DeathEffect: blasts climbing up it (frame, below).
                c.m.waitFinished = c.ai.now + 1600;
                c.m.count1 = 0;
                c.m.time3 = c.ai.now;
                return false; // (and the base's own)
            default:
                return false;
            }
        }

        /// One of the blasts it goes up in, higher and greater each time
        /// (SpawnExplosion's, which hurt nothing).
        inline void deathBlast(AiCtx &c)
        {
            const fx at[3] = {c.me.pos[0] + fxFromInt(aiRandomLong(c.a, -70, 70)),
                              c.me.pos[1] + fxFromInt(aiRandomLong(c.a, -70, 70)),
                              c.me.pos[2] + fxFromInt(32 + c.m.count1 * 30)};
            explodeAt(c.w, c.player, at, 60 + c.m.count1 * 40, true, true, false);
            ++c.m.count1;
        }

        inline bool runTask(AiCtx &c, const AiTask &task)
        {
            if (task.task == kTaskDie)
            {
                if (c.ai.now <= c.m.waitFinished)
                    return false;
                // Gone, in pieces -- and the last of its blasts, which in the
                // game are things of their own and go off after it, now.
                while (c.m.count1 < 4)
                    deathBlast(c);
                c.me.health = -100;
                if (!gibBurst(c.w, c.index))
                {
                    c.me.active = false;
                    actorPlaceBox(c.a, c.index);
                }
                aiDied(c);
                return true;
            }
            if (task.task != kTaskFlameSweep)
                return false;
            if (c.ai.now > c.m.waitFinished)
            {
                flameDestroy(c);
                aiTaskComplete(c);
                c.me.aimYaw = c.me.aimPitch = 0;
                return true;
            }
            bool cancel = false;
            int angleX = 0, angleY = 0;
            flameUpdate(c);
            AiTarget t;
            if (c.m.enemy != kAiNone && aiTargetOf(c.w, c.player, c.m.enemy, t))
            {
                const fx org[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(64)};
                const fx dir[3] = {t.center[0] - org[0], t.center[1] - org[1], t.center[2] - org[2]};
                const fx flat[3] = {dir[0], dir[1], 0};
                angleY = static_cast<std::int16_t>(fxAtan2Turns(dir[1], dir[0]) - c.me.yaw);
                angleX = static_cast<std::int16_t>(fxAtan2Turns(dir[2], fxLength(flat)));
                if (fxLength(dir) > fxFromInt(400))
                    cancel = true;
            }
            if (angleY > 60 * 65536 / 360 || angleY < -60 * 65536 / 360)
                cancel = true;
            if (cancel)
            {
                c.m.waitFinished -= 500;
                c.m.time1 -= 500;
            }
            flameControls(c, angleX, angleY);
            return true;
        }

        /// Each frame, dying: its blasts, one each 0.6 of a second.
        inline void frame(AiCtx &c, int)
        {
            if (c.me.ai < kAiDying || c.m.deadflag == 0 || c.m.count1 >= 4 || c.m.time3 == 0)
                return;
            if (c.ai.now >= c.m.time3 + c.m.count1 * 600)
                deathBlast(c);
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Gargantua);
                k.health = kHealth;
                // (It has no eyes of its own in the game -- they are at its
                // feet -- which a map's bumps would hide everything from.)
                k.eye = fxFromInt(64);
                k.fov = fxDot(-0.2);
                k.blood = kBloodYellow;
                k.capability = kCapDoorsGroup;
                k.yawSpeed = yawSpeed;
                k.sound = sound;
                k.animEvent = animEvent;
                k.prescheduleThink = prescheduleThink;
                k.checkMelee1 = checkMelee1;
                k.checkMelee2 = checkMelee2;
                k.checkRange1 = checkRange1;
                k.takeDamage = takeDamage;
                k.scheduleOfType = scheduleOfType;
                k.startTask = startTask;
                k.runTask = runTask;
                k.frame = frame;
                return k;
            }();
            return k;
        }
    } // namespace garg

    // ============================================================== baby crab

    namespace babycrab
    {
        inline constexpr AiTask tlRangeAttack1Fast[] = {
            {kTaskStopMoving, 0},
            {kTaskFaceIdeal, 0},
            {kTaskRangeAttack1, 0},
            {kTaskSetActivity, kActIdle},
        };
        inline constexpr AiSchedule slRangeAttack1Fast = {tlRangeAttack1Fast, 4, kCondEnemyOccluded | kCondNoAmmoLoaded,
                                                          0, "HCRAFast"};

        inline int yawSpeed(AiCtx &) { return 120; }

        /// Its voice: the headcrab's, higher, and not as loud
        /// (GetVoicePitch, GetSoundVolue).
        inline void say(AiCtx &c, CommonSound sound)
        {
            if (c.w.heard != nullptr)
                c.w.heard->add(sound, c.me.pos, 101, kRadiusSmall, 100 + aiRandomLong(c.a, 40, 50));
        }
        inline void sound(AiCtx &c, int which)
        {
            switch (which)
            {
            case kAiSoundAlert: say(c, CommonSound::HeadcrabAlert); break;
            case kAiSoundPain:  say(c, CommonSound::HeadcrabPain); break;
            case kAiSoundDeath: say(c, CommonSound::HeadcrabDie); break;
            default: break;
            }
        }

        /// LeapTouch, with its bite: three tenths of a grown one's.
        inline void touch(AiCtx &c, std::int16_t other)
        {
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, other, t) || t.cls == aiClassify(c))
                return;
            if (c.m.inAir)
            {
                say(c, CommonSound::HeadcrabBite);
                aiDamageTarget(c, other, skillOf(c.w).headcrabDmgBite * 3 / 10, kDmgSlash);
            }
            c.m.touchArmed = false;
        }

        /// A little less sure of itself, but it jumps from closer.
        inline bool checkRange1(AiCtx &c, fx dot, fx dist)
        {
            return !c.m.inAir && dist <= fxFromInt(180) && dot >= fxDot(0.55);
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            // (If it fails, it tries to jump.)
            if ((type == kSchedFail && c.m.enemy != kAiNone) || type == kSchedRangeAttack1)
                return &slRangeAttack1Fast;
            return headcrab::scheduleOfType(c, type);
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k = headcrab::kind();
                k.kind = static_cast<std::uint8_t>(ActorKind::BabyCrab);
                k.health = 10 / 4; // (a quarter of a grown one's)
                k.eye = fxFromInt(12);
                k.center = fxFromInt(4);
                k.yawSpeed = yawSpeed;
                k.sound = sound;
                k.touch = touch;
                k.checkRange1 = checkRange1;
                k.scheduleOfType = scheduleOfType;
                return k;
            }();
            return k;
        }
    } // namespace babycrab

    // ============================================================ ichthyosaur

    namespace icky
    {
        inline constexpr int kShakeRight = 1; ///< ICHTHYOSAUR_AE_SHAKE_RIGHT
        inline constexpr int kShakeLeft = 2;  ///< ICHTHYOSAUR_AE_SHAKE_LEFT
        inline constexpr int kHealth = 200;   ///< sk_ichthyosaur_health (at "normal")
        inline constexpr int kProbe = 150;    ///< PROBE_LENGTH
        inline constexpr int kHalf = 32;      ///< its box: 64 across, about its middle
        // Its own think, as the leech's (the game's is a CFlyingMonster's
        // schedules over its Swim; what they come to is here, in the order
        // its GetSchedule chooses them). m_SaveVelocity: vec1; m_flightSpeed:
        // value1; m_idealDist: value2; m_flEnemyTouched: time1;
        // m_flNextAlert: time2; m_flBlink: time3; m_bOnAttack: flag1; on its
        // way at its enemy (SCHED_CHASE_ENEMY): flag2; m_flMinSpeed: count1;
        // m_flMaxSpeed, m_flMaxDist: moveGoal[0], moveGoal[1] (it goes by
        // none of the routes those are for); when it last saw its enemy:
        // waitFinished. `pos` is its middle. Its pitch and roll are drawn
        // (65536 to the circle, nose up): aimPitch, aimYaw; the turn of its
        // tail (its bone controller): wantYaw.

        inline bool inWater(const AiCtx &c, const fx *at)
        {
            return pointContents(*c.w.world, at) == kContentsWater;
        }

        inline fx unit(fx *v)
        {
            const fx length = fxLength(v);
            for (int k = 0; length > 0 && k < 3; ++k)
                v[k] = static_cast<fx>((static_cast<std::int64_t>(v[k]) << kFxShift) / length);
            return length;
        }
        inline void cross(const fx *a, const fx *b, fx *out)
        {
            out[0] = fxMul(a[1], b[2]) - fxMul(a[2], b[1]);
            out[1] = fxMul(a[2], b[0]) - fxMul(a[0], b[2]);
            out[2] = fxMul(a[0], b[1]) - fxMul(a[1], b[0]);
        }
        inline fx dot(const fx *a, const fx *b) { return fxMul(a[0], b[0]) + fxMul(a[1], b[1]) + fxMul(a[2], b[2]); }

        /// Its forward, right and up, as its yaw and pitch have them.
        inline void axes(const AiCtx &c, fx *forward, fx *right, fx *up)
        {
            fx sy, cy, sp, cp;
            fxSinCos(c.me.yaw, sy, cy);
            fxSinCos(static_cast<std::uint16_t>(c.me.aimPitch), sp, cp);
            forward[0] = fxMulShort(cp, cy);
            forward[1] = fxMulShort(cp, sy);
            forward[2] = sp;
            right[0] = sy;
            right[1] = -cy;
            right[2] = 0;
            cross(right, forward, up);
        }

        inline void say(AiCtx &c, CommonSound sound)
        {
            if (c.w.heard != nullptr)
                c.w.heard->add(sound, c.me.pos, 127, 1667, aiRandomLong(c.a, 95, 105)); // (attn 0.6)
        }

        /// ProbeZ, then a trace of its hull: how far along the probe it gets
        /// (Q12), and what it meets there -- the top of the water is a wall
        /// to it, whose normal is straight down.
        inline fx probe(AiCtx &c, const fx *to, fx *normal)
        {
            normal[0] = normal[1] = 0;
            normal[2] = -kFxOne;
            fx frac = kFxOne;
            if (!inWater(c, to))
            {
                // (Where the water ends, in halvings.)
                fx lo = 0, hi = kFxOne;
                for (int n = 0; n < 8; ++n)
                {
                    const fx mid = (lo + hi) / 2;
                    const fx at[3] = {c.me.pos[0] + fxMul(to[0] - c.me.pos[0], mid),
                                      c.me.pos[1] + fxMul(to[1] - c.me.pos[1], mid),
                                      c.me.pos[2] + fxMul(to[2] - c.me.pos[2], mid)};
                    if (inWater(c, at))
                        lo = mid;
                    else
                        hi = mid;
                }
                frac = lo;
            }
            const Shot wall = traceShot(*c.w.world, c.me.pos, to);
            const fx hit = static_cast<fx>(wall.fraction >> (kFractionShift - kFxShift));
            if (hit < kFxOne * 99 / 100 && hit < frac)
            {
                frac = hit;
                fx at[3], n[3];
                if (traceShotWall(*c.w.world, c.me.pos, to, at, n))
                    for (int k = 0; k < 3; ++k)
                        normal[k] = n[k];
            }
            return frac;
        }

        /// DoProbe: a push away from what the probe meets, as hard as it
        /// was going at it.
        inline void doProbe(AiCtx &c, const fx *to, fx *steer)
        {
            steer[0] = steer[1] = steer[2] = 0;
            fx normal[3];
            const fx frac = probe(c, to, normal);
            if (frac >= kFxOne)
                return;
            fx dir[3] = {to[0] - c.me.pos[0], to[1] - c.me.pos[1], to[2] - c.me.pos[2]};
            unit(dir);
            fx across[3], push[3];
            cross(dir, normal, across);
            cross(across, dir, push);
            if (unit(push) == 0)
                return;
            fx going[3] = {c.m.vec1[0], c.m.vec1[1], c.m.vec1[2]};
            unit(going);
            fx force = fxMul(fxMul(c.m.value1, kFxOne - frac), dot(normal, going));
            force = force < 0 ? -force : force;
            for (int k = 0; k < 3; ++k)
                steer[k] = fxMul(push[k], force);
        }

        /// Swim: steered off what is ahead, its speed and its turn from
        /// that, ten times a second.
        inline void swim(AiCtx &c)
        {
            if (c.m.flag1 && c.m.value1 < c.m.moveGoal[0])
                c.m.value1 += fxFromInt(40);
            // (Its swim, slow; its thrust, fast: each played at its speed.)
            const std::uint8_t want = c.m.value1 < fxFromInt(180) ? kActWalk : kActRun;
            if ((c.m.activity == kActWalk || c.m.activity == kActRun) && c.m.activity != want)
                aiSetActivity(c, want);
            fx way[3] = {c.m.vec1[0], c.m.vec1[1], c.m.vec1[2]};
            if (unit(way) == 0)
                aiForward(c.me.yaw, way);
            fx sy, cy;
            const fx flat[3] = {way[0], way[1], 0};
            const std::uint16_t wayYaw = fxAtan2Turns(way[1], way[0]);
            fxSinCos(wayYaw, sy, cy);
            const fx right[3] = {sy, -cy, 0};
            fx up[3];
            cross(right, way, up);
            const fx start[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2]};
            fx steer[3] = {0, 0, 0};
            const struct
            {
                int along, side, lift;
            } kProbes[] = {{kProbe, 0, 0}, {kProbe / 3, 1, 0}, {kProbe / 3, -1, 0}, {kProbe / 3, 0, 1}, {kProbe / 3, 0, -1}};
            for (const auto &p : kProbes)
            {
                fx to[3], s[3];
                for (int k = 0; k < 3; ++k)
                    to[k] = start[k] + way[k] * p.along + right[k] * p.side + up[k] * p.lift;
                doProbe(c, to, s);
                for (int k = 0; k < 3; ++k)
                    steer[k] += s[k];
            }
            fx velocity[3];
            for (int k = 0; k < 3; ++k)
                velocity[k] = c.m.vec1[k] + steer[k] / 2;
            unit(velocity);
            fx forward[3], r[3], u[3];
            axes(c, forward, r, u);
            const fx facing = dot(forward, velocity);
            const fx speed = facing > kFxOne / 2 ? c.m.value1
                             : facing > 0         ? fxMul(c.m.value1, facing + kFxOne / 2)
                                                  : fxFromInt(80);
            for (int k = 0; k < 3; ++k)
            {
                c.m.vec1[k] = fxMul(velocity[k], speed);
                c.me.velocity[k] = c.m.vec1[k];
            }
            // Its pitch towards the way it goes, five degrees a think, within
            // 80 either way; its yaw at a tenth of its speed in degrees, and
            // its roll against the turn.
            const std::int32_t pitch = fxAtan2Turns(velocity[2], fxLength(flat));
            const int wantPitch = static_cast<std::int16_t>(pitch);
            const int most = 80 * 65536 / 360, step = 5 * 65536 / 360;
            int p = static_cast<std::int16_t>(c.me.aimPitch);
            p = wantPitch > p + step ? p + step : (wantPitch < p - step ? p - step : wantPitch);
            c.me.aimPitch = p < -most ? -most : (p > most ? most : p);
            const int velYaw = static_cast<std::int16_t>(fxAtan2Turns(velocity[1], velocity[0]) - c.me.yaw);
            const int turnMost = fxToInt(c.m.value1) * 65536 / 3600; // (speed * 0.1 degrees)
            const int turn = velYaw > turnMost ? turnMost : (velYaw < -turnMost ? -turnMost : velYaw);
            c.me.yaw = static_cast<std::uint16_t>(c.me.yaw + turn);
            c.m.idealYaw = c.me.yaw;
            int roll = static_cast<std::int16_t>(c.me.aimYaw) - turn;
            const int rollMost = 20 * 65536 / 360;
            c.me.aimYaw = roll < -rollMost ? -rollMost : (roll > rollMost ? rollMost : roll);
            c.me.wantYaw = c.me.wantYaw * 4 / 5 + turn;
        }

        /// CIRCLE_ENEMY: round what it is after, as far off as it likes to
        /// be, nearer and quicker while it is not looked at; further and
        /// slower while it is.
        inline void circle(AiCtx &c, const AiTarget &t, bool sees)
        {
            if (!sees)
            {
                c.m.time2 = c.ai.now + 200;
                return;
            }
            fx delta[3] = {c.me.pos[0] - t.eye[0], c.me.pos[1] - t.eye[1], c.me.pos[2] - t.eye[2]};
            unit(delta);
            const fx z[3] = {0, 0, kFxOne};
            fx side[3];
            cross(delta, z, side);
            unit(side);
            if (dot(side, c.m.vec1) < 0)
                for (fx &v : side)
                    v = -v;
            fx place[3];
            for (int k = 0; k < 3; ++k)
                place[k] = t.eye[k] + fxMul(delta[k], c.m.value2) + side[k] * 32;
            const Shot room = traceShot(*c.w.world, t.eye, place);
            if (room.fraction > kFractionOne / 2)
                for (int k = 0; k < 3; ++k)
                    place[k] = t.eye[k] + static_cast<fx>((static_cast<std::int64_t>(place[k] - t.eye[k]) * room.fraction) >> kFractionShift);
            fx to[3] = {place[0] - c.me.pos[0], place[1] - c.me.pos[1], place[2] - c.me.pos[2]};
            unit(to);
            for (int k = 0; k < 3; ++k)
                c.m.vec1[k] = fxMul(c.m.vec1[k], kFxOne * 4 / 5) + fxMul(fxMul(to[k], c.m.value1), kFxOne / 5);
            if (aiHas(c, kCondEnemyFacingMe))
            {
                c.m.time2 -= 100;
                if (c.m.value2 < c.m.moveGoal[1])
                    c.m.value2 += fxFromInt(4);
                if (c.m.value1 > fxFromInt(c.m.count1))
                    c.m.value1 -= fxFromInt(2);
                else if (c.m.value1 < fxFromInt(c.m.count1))
                    c.m.value1 += fxFromInt(2);
                if (fxFromInt(c.m.count1) < c.m.moveGoal[0])
                    c.m.count1 += 1; // (half a unit a think, in the game's)
            }
            else
            {
                c.m.time2 += 100;
                if (c.m.value2 > fxFromInt(128))
                    c.m.value2 -= fxFromInt(4);
                if (c.m.value1 < c.m.moveGoal[0])
                    c.m.value1 += fxFromInt(4);
            }
            if (c.m.time2 < c.ai.now)
            {
                say(c, CommonSound::IckyAlert);
                c.m.time2 = c.ai.now + aiRandomMs(c.a, 3000, 5000);
            }
        }

        inline void think(AiCtx &c)
        {
            if (c.m.value1 == 0)
            {
                // Spawn: off the way it faces, at its swimming speed.
                c.m.value2 = fxFromInt(384);
                c.m.count1 = 80;
                c.m.moveGoal[0] = fxFromInt(300);
                c.m.moveGoal[1] = fxFromInt(384);
                c.m.value1 = fxFromInt(150);
                aiForward(c.me.yaw, c.m.vec1);
                for (fx &v : c.m.vec1)
                    v = fxMul(v, c.m.value1);
                aiSetActivity(c, kActWalk);
            }
            if (c.me.ai >= kAiDying)
                return;
            // Blinks (its skins): shut a moment every three or four seconds;
            // mad, on the attack.
            if (c.m.time3 < c.ai.now)
            {
                c.me.skin = 2; // EYE_CLOSED
                if (c.m.time3 + 200 < c.ai.now)
                {
                    c.m.time3 = c.ai.now + aiRandomMs(c.a, 3000, 4000);
                    c.me.skin = c.m.flag1 ? 1 : 0; // EYE_MAD, EYE_BASE
                }
            }
            // What it sees: the player, in the wide sweep of its eyes, with
            // nothing between.
            AiTarget t;
            bool sees = false;
            if (c.player.health > 0 && !unseen(c.player) && aiTargetOf(c.w, c.player, kAiPlayer, t))
            {
                fx forward[3], r[3], u[3];
                axes(c, forward, r, u);
                fx to[3] = {t.center[0] - c.me.pos[0], t.center[1] - c.me.pos[1], t.center[2] - c.me.pos[2]};
                const fx far = unit(to);
                sees = far < fxFromInt(2048) && dot(forward, to) > fxDot(-0.7) && lineClear(*c.w.world, c.me.pos, t.eye);
            }
            if (sees)
            {
                if (c.m.enemy == kAiNone)
                    say(c, CommonSound::IckyAlert);
                c.m.enemy = kAiPlayer;
                c.m.state = kStateCombat;
                c.m.waitFinished = c.ai.now;
            }
            else if (c.m.enemy != kAiNone && c.ai.now > c.m.waitFinished + 5000)
            {
                c.m.enemy = kAiNone;
                c.m.state = kStateAlert;
                c.m.flag2 = false;
            }
            // GetSchedule.
            if (c.m.state != kStateCombat)
            {
                c.m.value1 = fxFromInt(c.m.state == kStateAlert ? 150 : 80);
                if (c.m.activity != kActWalk && c.m.activity != kActRun)
                    aiSetActivity(c, kActWalk);
                if (aiRandomLong(c.a, 0, 99) == 0)
                    say(c, CommonSound::IckyIdle);
            }
            else if (c.m.enemy != kAiNone && aiTargetOf(c.w, c.player, c.m.enemy, t))
            {
                c.m.moveGoal[0] = fxFromInt(400);
                fx forward[3], r[3], u[3];
                axes(c, forward, r, u);
                fx to[3] = {t.center[0] - c.me.pos[0], t.center[1] - c.me.pos[1], t.center[2] - c.me.pos[2]};
                const fx far = unit(to);
                const fx facing = dot(forward, to);
                // (Whether it is in its enemy's own view.)
                if (sees && aiDot2D(t.yaw, t.origin, c.me.pos) > t.fov)
                    aiSet(c, kCondEnemyFacingMe);
                else
                    aiClear(c, kCondEnemyFacingMe);
                if (c.me.health < c.m.maxHealth - 20)
                    c.m.flag1 = true;
                if (facing >= fxDot(0.7) && c.m.time1 > c.ai.now - 200)
                {
                    // MELEE_ATTACK1: its bite (the shakes are its clip's).
                    if (c.m.activity != kActMeleeAttack1)
                        aiSetActivity(c, kActMeleeAttack1);
                }
                else if (facing > fxDot(-0.7) && (c.m.flag1 || (far <= fxFromInt(192) && c.m.value2 <= fxFromInt(192))))
                {
                    // CHASE_ENEMY: straight at it (MoveExecute).
                    if (!c.m.flag2)
                        say(c, CommonSound::IckyAttack);
                    c.m.flag2 = true;
                    for (int k = 0; k < 3; ++k)
                        c.m.vec1[k] = fxMul(to[k], c.m.value1);
                    if (c.m.activity != kActWalk && c.m.activity != kActRun)
                        aiSetActivity(c, kActRun);
                }
                else
                {
                    // STANDOFF: round it.
                    c.m.flag2 = false;
                    if (c.m.activity != kActWalk && c.m.activity != kActRun)
                        aiSetActivity(c, kActWalk);
                    circle(c, t, sees);
                }
            }
            if (c.m.activity == kActMeleeAttack1 && c.m.seqFinished)
                aiSetActivity(c, kActRun);
            swim(c);
        }

        /// Each frame: where its velocity takes it, in the water; touching
        /// what it is after (BiteTouch); dead, it floats belly up to the top.
        inline void frame(AiCtx &c, int ms)
        {
            fx to[3];
            if (c.me.ai >= kAiDying)
            {
                // TASK_ICHTHYOSAUR_FLOAT, once its death is played.
                if (c.m.seqFinished && c.m.deadflag == 1)
                {
                    c.m.deadflag = 2;
                    const ModelView *model = actorModel(c.a, c.me);
                    const int belly = model != nullptr ? model->findClip(nameHash("bellyup")) : -1;
                    if (belly >= 0)
                        aiPlayClip(c, belly, true);
                }
                if (c.m.deadflag == 2)
                {
                    const fx above[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(24)};
                    const bool deep = inWater(c, above);
                    // (Ten times a second in the game's: here as much a frame.)
                    c.me.velocity[0] = c.me.velocity[0] * (1000 - ms * 2) / 1000;
                    c.me.velocity[1] = c.me.velocity[1] * (1000 - ms * 2) / 1000;
                    c.me.velocity[2] += fxFromInt(deep && c.me.velocity[2] < fxFromInt(64) ? 8 : -8) * ms / 100;
                    if (!deep && c.me.velocity[2] < 0 && inWater(c, c.me.pos))
                        c.me.velocity[2] = 0;
                    const int p = static_cast<std::int16_t>(c.me.aimPitch);
                    c.me.aimPitch = p * (1000 - ms * 2) / 1000;
                }
            }
            for (int k = 0; k < 3; ++k)
                to[k] = c.me.pos[k] + static_cast<fx>((static_cast<std::int64_t>(c.me.velocity[k]) * ms) / 1000);
            // (The player is in its way, as a box is in the game's: it stops
            // against them -- and has touched them.)
            bool blocked = false;
            if (c.me.ai < kAiDying && c.player.health > 0)
            {
                const fx lo[3] = {to[0] - fxFromInt(kHalf), to[1] - fxFromInt(kHalf), to[2] - fxFromInt(kHalf)};
                const fx hi[3] = {to[0] + fxFromInt(kHalf), to[1] + fxFromInt(kHalf), to[2] + fxFromInt(kHalf)};
                blocked = detail::playerTouchesBox(c.player, lo, hi, 0);
                if (blocked && c.m.enemy == kAiPlayer)
                {
                    c.m.time1 = c.ai.now;
                    c.m.flag1 = true;
                }
            }
            const Shot wall = traceShot(*c.w.world, c.me.pos, to);
            if (blocked)
            {
            }
            else if (wall.fraction >= kFractionOne && (inWater(c, to) || c.me.ai >= kAiDying))
            {
                for (int k = 0; k < 3; ++k)
                    c.me.pos[k] = to[k];
            }
            else if (c.me.ai < kAiDying)
            {
                // (Into the wall, or out of the water: it turns away.)
                c.me.velocity[0] = -c.me.velocity[0] / 2;
                c.me.velocity[1] = -c.me.velocity[1] / 2;
                c.me.velocity[2] = -c.me.velocity[2] / 2;
                for (int k = 0; k < 3; ++k)
                    c.m.vec1[k] = c.me.velocity[k];
            }
            actorPlaceBox(c.a, c.index);
            if (c.me.ai >= kAiDying || c.m.enemy != kAiPlayer || c.player.health <= 0)
                return;
            fx mins[3], maxs[3];
            targetBox(c.me, mins, maxs);
            if (detail::playerTouchesBox(c.player, mins, maxs, fxFromInt(4)))
            {
                c.m.time1 = c.ai.now;
                c.m.flag1 = true;
            }
        }

        inline void animEvent(AiCtx &c, int event)
        {
            if (event != kShakeRight && event != kShakeLeft)
                return;
            AiTarget t;
            if (c.m.enemy != kAiNone && aiTargetOf(c.w, c.player, c.m.enemy, t) && lineClear(*c.w.world, c.me.pos, t.eye))
            {
                fx to[3] = {t.center[0] - c.me.pos[0], t.center[1] - c.me.pos[1], t.center[2] - c.me.pos[2]};
                const fx far = unit(to);
                fx forward[3], right[3], up[3];
                axes(c, forward, right, up);
                if ((c.m.time1 >= c.ai.now - 200 || far <= fxFromInt(32 + 16 + 32)) && dot(to, forward) > fxDot(0.707))
                {
                    c.m.flag1 = true;
                    detail::aiPunch(c, c.m.enemy, fxFromInt(5), -fxFromInt(18));
                    const fx shove[3] = {-right[0] * 300, -right[1] * 300, 0};
                    detail::aiShove(c, c.m.enemy, shove);
                    aiDamageTarget(c, c.m.enemy, skillOf(c.w).ichthyosaurShake, kDmgSlash);
                }
            }
            say(c, CommonSound::IckyBite);
        }

        /// Hurt: further off, or -- if it was close already -- on the
        /// attack (its SMALL_FLINCH); hurt badly, on the attack.
        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            if (c.me.ai >= kAiDying)
                return true;
            say(c, CommonSound::IckyPain);
            if (c.m.value2 > fxFromInt(128))
                c.m.moveGoal[1] = c.m.value2 = fxFromInt(512);
            else
                c.m.flag1 = true;
            if (d.amount >= 20)
                c.m.flag1 = true;
            if (d.attacker == kAiPlayer)
            {
                c.m.enemy = kAiPlayer;
                c.m.state = kStateCombat;
                c.m.waitFinished = c.ai.now;
            }
            return true;
        }

        inline bool killed(AiCtx &c)
        {
            say(c, CommonSound::IckyDie);
            c.me.ai = kAiDying;
            c.m.deadflag = 1;
            c.m.state = c.m.idealState = kStateDead;
            c.me.velocity[0] = c.me.velocity[1] = c.me.velocity[2] = 0;
            c.me.skin = 0;
            aiSetActivity(c, kActDieSimple);
            c.m.seqFinished = false;
            actorPlaceBox(c.a, c.index);
            return true;
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Ichthyosaur);
                k.health = kHealth;
                k.eye = fxFromInt(16);
                k.fov = fxDot(-0.7); // (VIEW_FIELD_WIDE)
                k.blood = kBloodYellow;
                k.capability = kCapRangeAttack1 | kCapSwim;
                k.think = think;
                k.frame = frame;
                k.animEvent = animEvent;
                k.takeDamage = takeDamage;
                k.killed = killed;
                return k;
            }();
            return k;
        }
    } // namespace icky

    // ================================================================ round 5
    //
    //   tentacle    tentacle.cpp   rises out of its pit to the height of what
    //                              it hears and strikes at it; its own think,
    //                              its clips found by the model's own graph of
    //                              them; it is put down only by the map
    //   big momma   bigmomma.cpp   walks her path of nodes (the map's
    //                              info_bigmomma: LogicKind::MommaNode),
    //                              given new health at each; slashes, spits
    //                              acid from afar, lays baby crabs
    //   nihilanth   nihilanth.cpp  floats between the map's heights, its
    //                              spheres about it healing it, recharged at
    //                              its crystals; zaps, takes the player away,
    //                              calls up friends; hurt to its death only
    //                              in its head, once that is open
    //   boids       aflock.cpp     a flock: its leader steers off walls, the
    //                              rest keep near it and apart

    // =============================================================== tentacle

    namespace tentacle
    {
        // Its activities (ACT_T_*), each and the level it is at.
        inline constexpr int kActIdle = 1010, kActTap = 1020, kActStrike = 1030, kActRearIdle = 1040;
        inline constexpr int kHealth = 75;
        inline constexpr int kLook = 850; ///< as high, and as far, as it ever reaches

        /// tentacle2.mdl's sequences, by name, in its order: what each is for
        /// (its activity and its weight among those that are), and where
        /// it goes in the model's graph of them (entry, exit, and whether it
        /// may be played backwards) -- FindTransition's.
        struct Seq
        {
            const char *name;
            int activity, weight;
            std::uint8_t entry, exit, reverses;
        };
        inline constexpr Seq kSeqs[] = {
            {"pit_idle", 0, 0, 9, 9, 0},            {"rise_to_temp1", 0, 0, 9, 1, 1},
            {"temp1_to_floor", 0, 0, 1, 4, 0},      {"floor_idle", 1010, 5, 4, 4, 0},
            {"floor_fidget_pissed", 1010, 1, 4, 4, 0}, {"floor_fidget_smallrise", 1010, 1, 4, 4, 0},
            {"floor_fidget_wave", 1010, 1, 4, 4, 0}, {"floor_strike", 1030, 1, 4, 4, 0},
            {"floor_tap", 1020, 1, 4, 4, 0},        {"floor_rotate", 0, 0, 4, 12, 1},
            {"floor_rear", 0, 0, 12, 13, 1},        {"floor_rear_idle", 1040, 1, 13, 13, 0},
            {"floor_to_lev1", 0, 0, 4, 1, 0},       {"lev1_idle", 1011, 5, 1, 1, 0},
            {"lev1_fidget_claw", 1011, 1, 1, 1, 0}, {"lev1_fidget_shake", 1011, 1, 1, 1, 0},
            {"lev1_fidget_snap", 1011, 1, 1, 1, 0}, {"lev1_strike", 1031, 1, 1, 1, 0},
            {"lev1_tap", 1021, 1, 1, 1, 0},         {"lev1_rotate", 0, 0, 1, 5, 1},
            {"lev1_rear", 0, 0, 5, 6, 1},           {"lev1_rear_idle", 1041, 1, 6, 6, 0},
            {"lev1_to_lev2", 0, 0, 1, 2, 1},        {"lev2_idle", 1012, 5, 2, 2, 0},
            {"lev2_fidget_shake", 1012, 1, 2, 2, 0}, {"lev2_fidget_swing", 0, 0, 2, 2, 0},
            {"lev2_fidget_tut", 1012, 1, 2, 2, 0},  {"lev2_strike", 1032, 1, 2, 2, 0},
            {"lev2_tap", 1022, 1, 2, 2, 0},         {"lev2_rotate", 0, 0, 2, 7, 1},
            {"lev2_rear", 0, 0, 7, 8, 1},           {"lev2_rear_idle", 1042, 1, 8, 8, 0},
            {"lev2_to_lev3", 0, 0, 2, 3, 1},        {"lev3_idle", 1013, 5, 3, 3, 0},
            {"lev3_fidget_shake", 1013, 1, 3, 3, 0}, {"lev3_fidget_side", 1013, 1, 3, 3, 0},
            {"lev3_fidget_swipe", 0, 0, 0, 0, 0},   {"lev3_strike", 1033, 1, 3, 3, 0},
            {"lev3_tap", 1023, 1, 3, 3, 0},         {"lev3_rotate", 0, 0, 3, 10, 1},
            {"lev3_rear", 0, 0, 10, 11, 1},         {"lev3_rear_idle", 1043, 1, 11, 11, 0},
            {"lev1_door_reach", 0, 0, 0, 0, 0},     {"lev3_to_engine", 0, 0, 11, 14, 1},
            {"engine_idle", 1044, 1, 14, 14, 0},    {"engine_sway", 1044, 1, 14, 14, 0},
            {"engine_swat", 1044, 1, 14, 14, 0},    {"engine_bob", 1044, 1, 14, 14, 0},
            {"engine_death1", 0, 0, 14, 14, 0},     {"engine_death2", 0, 0, 14, 14, 0},
            {"engine_death3", 0, 0, 14, 14, 0},
        };
        inline constexpr int kSeqCount = static_cast<int>(sizeof(kSeqs) / sizeof(kSeqs[0]));
        // TENTACLE_ANIM_*, the ones its code names.
        enum : int
        {
            kPitIdle = 0, kFloorTap = 8, kFloorRear = 10, kFloorRearIdle = 11, kLev1Tap = 18, kLev1Rear = 20,
            kLev1RearIdle = 21, kLev2Tap = 28, kLev2Rear = 30, kLev2RearIdle = 31, kLev3Tap = 38, kLev3Rear = 40,
            kLev3RearIdle = 41, kEngineIdle = 44, kEngineSway = 45, kEngineBob = 47, kEngineDeath1 = 48,
            kEngineDeath3 = 50,
        };
        // m_iGoalAnim: count1 (one of kSeqs); the one playing: other; which
        // way it plays: flag1 (true backwards), its pace: value1 (Q12, 1.0
        // as recorded); m_flSoundYaw: value2 (degrees Q12, from how it was
        // put); m_iSoundLevel: squadMember[0]; m_flSoundTime: time1;
        // m_flNextSong: time2; m_flPrevSoundTime: time3; m_vecPrevSound:
        // vec1; m_flInitialYaw: moveGoal[0] (turns); m_flMaxYaw: moveGoal[1]
        // (degrees Q12); m_flFramerateAdj: moveGoal[2] (Q12); m_iHitDmg:
        // squadMember[1]; m_flHitTime: waitFinished; DieThink: flag2; Start
        // been: flag3. Its frame is its own to step (it plays backwards, and
        // at a pace of its own): the engine is kept off it (`frozen`).

        inline int seqOfClip(const AiCtx &c, int clip)
        {
            const ModelView *model = actorModel(c.a, c.me);
            if (model == nullptr || clip < 0)
                return -1;
            const std::uint32_t name = model->clips[clip].name;
            for (int s = 0; s < kSeqCount; ++s)
            {
                if (nameHash(kSeqs[s].name) == name)
                    return s;
            }
            return -1;
        }
        inline int clipOfSeq(const AiCtx &c, int seq)
        {
            const ModelView *model = actorModel(c.a, c.me);
            return model != nullptr && seq >= 0 && seq < kSeqCount ? model->findClip(nameHash(kSeqs[seq].name)) : -1;
        }

        /// LookupActivity: one of the sequences for `activity`, by their
        /// weights; -1 if none (an activity past its last level).
        inline int lookupActivity(AiCtx &c, int activity)
        {
            int total = 0;
            for (const Seq &s : kSeqs)
                total += s.activity == activity ? s.weight : 0;
            if (total == 0)
                return -1;
            int pick = aiRandomLong(c.a, 0, total - 1);
            for (int s = 0; s < kSeqCount; ++s)
            {
                if (kSeqs[s].activity != activity)
                    continue;
                if ((pick -= kSeqs[s].weight) < 0)
                    return s;
            }
            return -1;
        }

        /// MyLevel: the height it is at, by what it is playing (-1 in its pit).
        inline int myLevel(int seq)
        {
            if (seq < 0 || seq == kPitIdle || seq == 42 || seq >= 43)
                return -1;
            if (seq <= 12)
                return 0;
            if (seq <= 22)
                return 1;
            if (seq <= 32)
                return 2;
            return 3;
        }
        inline int myHeight(int seq)
        {
            static constexpr int kHeights[4] = {0, 256, 448, 640};
            const int level = myLevel(seq);
            return level <= 0 ? 0 : kHeights[level];
        }
        /// Level: the height of something from its root.
        inline int level(fx dz)
        {
            return dz < fxFromInt(216) ? 0 : (dz < fxFromInt(408) ? 1 : (dz < fxFromInt(600) ? 2 : 3));
        }

        /// FindTransition: the next sequence on the way from the one it has
        /// played to `goal`, by the model's graph -- forwards, or one that
        /// may be played backwards -- and which way to play it.
        inline int findTransition(int from, int goal, bool &backwards)
        {
            if (from < 0 || goal < 0 || kSeqs[from].entry == 0 || kSeqs[goal].entry == 0)
            {
                backwards = false;
                return goal;
            }
            const int end = backwards ? kSeqs[from].entry : kSeqs[from].exit;
            const int want = kSeqs[goal].entry;
            if (end == want)
            {
                backwards = false;
                return goal;
            }
            // (The game's table of the next node on the way: worked out here,
            // a search over the graph.)
            int previous[16];
            for (int &p : previous)
                p = -1;
            int queue[16], head = 0, tail = 0;
            queue[tail++] = end;
            previous[end] = end;
            while (head < tail && previous[want] < 0)
            {
                const int node = queue[head++];
                for (const Seq &s : kSeqs)
                {
                    int to = -1;
                    if (s.entry == node && s.exit != node)
                        to = s.exit;
                    else if (s.reverses && s.exit == node && s.entry != node)
                        to = s.entry;
                    if (to > 0 && to < 16 && previous[to] < 0)
                    {
                        previous[to] = node;
                        queue[tail++] = to;
                    }
                }
            }
            if (previous[want] < 0)
            {
                backwards = false;
                return goal;
            }
            int next = want;
            while (previous[next] != end)
                next = previous[next];
            for (int s = 0; s < kSeqCount; ++s)
            {
                if (kSeqs[s].entry == end && kSeqs[s].exit == next)
                {
                    backwards = false;
                    return s;
                }
                if (kSeqs[s].reverses && kSeqs[s].exit == end && kSeqs[s].entry == next)
                {
                    backwards = true;
                    return s;
                }
            }
            backwards = false;
            return goal;
        }

        /// Into `seq`, from its start (or its end, backwards), at its pace.
        inline void play(AiCtx &c, int seq, bool backwards)
        {
            const int clip = clipOfSeq(c, seq);
            if (clip < 0)
                return;
            const ModelView *model = actorModel(c.a, c.me);
            c.me.clip = static_cast<std::int16_t>(clip);
            const int last = model->clips[clip].frameCount > 0 ? model->clips[clip].frameCount - 1 : 0;
            c.me.frame = backwards ? static_cast<std::uint32_t>(last) << 8 : 0;
            c.me.lastFrame = c.me.frame;
            c.me.frozen = true;
            c.me.finished = false;
            c.me.once = true;
            c.m.flag1 = backwards;
            c.m.other = static_cast<std::int16_t>(seq);
            c.m.seqFinished = false;
        }

        inline void say(AiCtx &c, CommonSound sound, int height)
        {
            if (c.w.heard == nullptr)
                return;
            const fx at[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(height)};
            c.w.heard->add(sound, at, 127, kRadiusLarge);
        }

        /// Its loop of flies, or of squirming (the first two of them: Start).
        inline void loop(AiCtx &c, CommonSound sound, bool on)
        {
            if (c.w.heard == nullptr)
                return;
            const auto tag = static_cast<std::uint16_t>(kSoundTagActor | c.index);
            if (on && static_cast<std::uint16_t>(sound) < c.w.heard->commons)
                c.w.heard->add(static_cast<std::uint16_t>(sound), c.me.pos, 127, kRadiusLarge, kSoundLoopOn, 100, tag);
            else if (!on)
                c.w.heard->add(kNoSound, nullptr, 0, 0, kSoundLoopOff, 100, tag);
        }

        /// The best sound it hears from where it is now (its ear is at its
        /// height), twice as far as a monster would (HearingSensitivity 2).
        inline const AiSound *listen(AiCtx &c)
        {
            const fx ear[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(myHeight(c.m.other))};
            const AiSound *best = nullptr;
            fx bestDist = fxFromInt(8192);
            const SoundEnt &sounds = c.ai.sounds;
            for (std::int8_t i = sounds.active; i != kSoundListEmpty; i = sounds.pool[i].next)
            {
                const AiSound &sound = sounds.pool[i];
                const fx d = aiDist(sound.origin, ear);
                if (!(sound.type & kSoundHeard) || d > fxFromInt(sound.volume) * 2 || d >= bestDist)
                    continue;
                best = &sound;
                bestDist = d;
            }
            return best;
        }

        /// HitTouch: something in it, on one of its hit boxes as it is posed
        /// now -- its tip (group 3 and over) hurts as hard as it swings, the
        /// rest 20 -- at most twice a second.
        inline void touchTest(AiCtx &c)
        {
            if (c.ai.now < c.m.waitFinished || c.player.health <= 0)
                return;
            const fx d[3] = {c.player.origin[0] - c.me.pos[0], c.player.origin[1] - c.me.pos[1], 0};
            if (fxLength(d) > fxFromInt(kLook) || c.player.origin[2] < c.me.pos[2] - fxFromInt(64) ||
                c.player.origin[2] > c.me.pos[2] + fxFromInt(kLook + 64))
                return;
            const ModelView *model = actorModel(c.a, c.me);
            if (model == nullptr || model->header->hitboxCount == 0 || c.me.clip < 0)
                return;
            static BoneMatrix bones[kModelMaxBones];
            poseModel(*model, c.me.clip, c.me.frame, bones, nullptr, c.me.blend.value);
            fx sine, cosine;
            fxSinCos(c.me.yaw, sine, cosine);
            // The player's middle, and their box's reach about it, in the
            // model's own space.
            const fx rel[3] = {c.player.origin[0] - c.me.pos[0], c.player.origin[1] - c.me.pos[1],
                               c.player.origin[2] - c.me.pos[2]};
            const fx at[3] = {fxMul(rel[0], cosine) + fxMul(rel[1], sine), fxMul(rel[1], cosine) - fxMul(rel[0], sine),
                              rel[2]};
            int group = -1;
            for (int n = 0; n < model->header->hitboxCount; ++n)
            {
                const ModelHitbox &box = model->hitboxes[n];
                const BoneMatrix &bone = bones[box.bone];
                const fx o[3] = {at[0] - bone.t[0], at[1] - bone.t[1], at[2] - bone.t[2]};
                fx local[3];
                for (int k = 0; k < 3; ++k)
                    local[k] = static_cast<fx>((static_cast<std::int64_t>(bone.r[k]) * o[0] +
                                                static_cast<std::int64_t>(bone.r[3 + k]) * o[1] +
                                                static_cast<std::int64_t>(bone.r[6 + k]) * o[2]) >>
                                               kBoneRotShift);
                // (The player is a box 32 across and 72 high: whichever way
                // the bone lies, as far as 24 from their middle is theirs.)
                bool inside = true;
                for (int k = 0; k < 3 && inside; ++k)
                {
                    const fx lo = (static_cast<fx>(box.mins[k]) << (kFxShift - kHitboxShift)) - fxFromInt(24);
                    const fx hi = (static_cast<fx>(box.maxs[k]) << (kFxShift - kHitboxShift)) + fxFromInt(24);
                    inside = local[k] >= lo && local[k] <= hi;
                }
                if (inside && box.group > group)
                    group = box.group;
            }
            if (group <= 0)
                return;
            aiDamageTarget(c, kAiPlayer, group >= 3 ? c.m.squadMember[1] : 20, kDmgCrush);
            c.m.waitFinished = c.ai.now + 500;
        }

        /// Cycle, ten times a second: what it hears, where to turn, and --
        /// done with what it plays -- what next.
        inline void cycle(AiCtx &c)
        {
            const AiSound *heard = listen(c);
            if (heard != nullptr)
            {
                fx dir[3];
                if (c.ai.now - c.m.time3 < 500)
                {
                    const std::int32_t dt = c.ai.now - c.m.time3 > 0 ? c.ai.now - c.m.time3 : 1;
                    for (int k = 0; k < 3; ++k)
                        dir[k] = heard->origin[k] + static_cast<fx>((static_cast<std::int64_t>(heard->origin[k] - c.m.vec1[k]) * 1000) / dt) -
                                 c.me.pos[k];
                }
                else
                {
                    for (int k = 0; k < 3; ++k)
                        dir[k] = heard->origin[k] - c.me.pos[k];
                }
                c.m.time3 = c.ai.now;
                for (int k = 0; k < 3; ++k)
                    c.m.vec1[k] = heard->origin[k];
                const int yaw = static_cast<std::int16_t>(fxAtan2Turns(dir[1], dir[0]) - static_cast<std::uint16_t>(c.m.moveGoal[0]));
                c.m.value2 = static_cast<fx>((static_cast<std::int64_t>(yaw) * 360 * kFxOne) / 65536);
                c.m.squadMember[0] = static_cast<std::int16_t>(level(dir[2]));
                if (c.m.time1 < c.ai.now && aiRandomLong(c.a, 0, 1) == 0)
                    say(c, CommonSound::TentacleAlert, myHeight(c.m.other));
                c.m.time1 = c.ai.now + aiRandomMs(c.a, 5000, 10000);
            }
            // Where to face: towards it, within its sweep -- and, reared
            // back, not in front.
            const fx most = c.m.moveGoal[1];
            fx dy = c.m.value2;
            const int seq = c.m.other;
            const bool rear = seq == kFloorRear || seq == kFloorRearIdle || seq == kLev1Rear || seq == kLev1RearIdle ||
                              seq == kLev2Rear || seq == kLev2RearIdle || seq == kLev3Rear || seq == kLev3RearIdle;
            if (rear)
            {
                if (dy < 0 && dy > -most)
                    dy = -most;
                if (dy > 0 && dy < most)
                    dy = most;
            }
            else
            {
                dy = dy < -most ? -most : (dy > most ? most : dy);
            }
            c.m.idealYaw = static_cast<std::uint16_t>(static_cast<std::uint16_t>(c.m.moveGoal[0]) +
                                                      static_cast<std::int32_t>((static_cast<std::int64_t>(dy) * 65536) / (360 * kFxOne)));
            if (c.m.seqFinished)
            {
                const int lvl = c.m.squadMember[0];
                if (c.me.health <= 1)
                {
                    // Hurt enough: down into its pit, where it is whole again.
                    c.m.count1 = kPitIdle;
                    if (seq == kPitIdle)
                        c.me.health = kHealth;
                }
                else if (c.m.time1 > c.ai.now)
                {
                    const fx sweep = c.m.value2 < 0 ? -c.m.value2 : c.m.value2;
                    if (sweep <= most + fxFromInt(30))
                        c.m.count1 = lookupActivity(c, kActStrike + lvl);
                    else if (sweep <= most * 2)
                        c.m.count1 = lookupActivity(c, kActTap + lvl);
                    else
                        c.m.count1 = lookupActivity(c, kActRearIdle + lvl);
                }
                else if (seq == kPitIdle)
                {
                    c.m.count1 = kPitIdle;
                }
                else if (seq == c.m.count1)
                {
                    const int mine = myLevel(seq);
                    if (mine >= 0 && c.ai.now < c.m.time1)
                    {
                        c.m.count1 = aiRandomLong(c.a, 0, 9) < (c.m.time1 - c.ai.now) / 1000
                                         ? lookupActivity(c, kActStrike + lvl)
                                         : lookupActivity(c, kActTap + lvl);
                    }
                    else if (mine < 0)
                    {
                        c.m.count1 = lookupActivity(c, kActIdle);
                    }
                    else
                    {
                        if (c.m.time2 < c.ai.now)
                        {
                            say(c, CommonSound::TentacleSing, myHeight(seq));
                            c.m.time2 = c.ai.now + aiRandomMs(c.a, 10000, 20000);
                        }
                        if (aiRandomLong(c.a, 0, 15) == 0)
                            c.m.count1 = lookupActivity(c, kActIdle + aiRandomLong(c.a, 0, 3));
                        else if (aiRandomLong(c.a, 0, 3) == 0)
                            c.m.count1 = lookupActivity(c, kActTap + mine);
                        else
                            c.m.count1 = lookupActivity(c, kActIdle + mine);
                    }
                    c.m.value2 += c.m.value2 < 0 ? fxFromInt(aiRandomLong(c.a, 2, 8)) : -fxFromInt(aiRandomLong(c.a, 2, 8));
                }
                if (c.m.count1 < 0)
                    c.m.count1 = seq >= 0 ? seq : kPitIdle;
                bool backwards = c.m.flag1;
                const int next = findTransition(seq, c.m.count1, backwards);
                play(c, next, backwards);
                c.m.moveGoal[2] = fxFromInt(aiRandomLong(c.a, -200, 200)) / 1000;
                c.m.value1 = kFxOne + c.m.moveGoal[2];
            }
            // Quicker while it has lately heard something.
            if (c.m.time3 + 2000 > c.ai.now)
                c.m.value1 = kFxOne * 3 / 2 + c.m.moveGoal[2];
            else if (c.m.time3 + 5000 > c.ai.now)
                c.m.value1 = kFxOne + static_cast<fx>((static_cast<std::int64_t>(kFxOne) * (5000 - (c.ai.now - c.m.time3))) / 2000) +
                             c.m.moveGoal[2];
            touchTest(c);
        }

        /// DieThink: on to the engine and its swaying (used: the map's
        /// rockets fire), or to a death there that it is gone after.
        inline void dieThink(AiCtx &c)
        {
            c.m.idealYaw = static_cast<std::uint16_t>(c.m.moveGoal[0]);
            if (!c.m.seqFinished)
                return;
            const int seq = c.m.other;
            if (seq == c.m.count1)
            {
                if (seq >= kEngineIdle && seq <= kEngineBob)
                {
                    c.m.count1 = kEngineSway + aiRandomLong(c.a, 0, 2);
                }
                else if (seq >= kEngineDeath1 && seq <= kEngineDeath3)
                {
                    loop(c, CommonSound::Flies, false);
                    c.me.active = false;
                    actorPlaceBox(c.a, c.index);
                    return;
                }
            }
            bool backwards = c.m.flag1;
            const int next = findTransition(seq, c.m.count1, backwards);
            play(c, next, backwards);
            const bool engine = (next >= kFloorRear && next <= kFloorRearIdle) || next == kLev1Rear ||
                                next == kLev1RearIdle || next == kLev2Rear || next == kLev2RearIdle ||
                                next == kLev3Rear || next == kLev3RearIdle || next >= kEngineIdle;
            c.m.value1 = engine ? (backwards ? -kFxOne : kFxOne) + fxFromInt(aiRandomLong(c.a, -200, 200)) / 1000
                                : kFxOne * 3 / 2;
            if (c.m.value1 < 0)
                c.m.value1 = -c.m.value1;
            if (engine)
                c.m.idealYaw = static_cast<std::uint16_t>(static_cast<std::uint16_t>(c.m.moveGoal[0]) + 32768);
        }

        inline void think(AiCtx &c)
        {
            if (!c.m.flag3)
            {
                // Spawn and Start: in its pit, facing as it was put; the
                // first of them buzzes with flies, the second squirms.
                c.m.flag3 = true;
                c.m.moveGoal[0] = c.me.yaw;
                if (c.m.moveGoal[1] == 0)
                    c.m.moveGoal[1] = fxFromInt(65);
                c.m.squadMember[1] = 20;
                c.me.health = kHealth;
                c.m.count1 = kPitIdle;
                play(c, kPitIdle, false);
                c.m.value1 = kFxOne;
                bool flies = false, squirm = false;
                for (int i = 0; i < c.index; ++i)
                {
                    if (c.a.states[i].kind == c.me.kind && c.a.states[i].active)
                        (flies ? squirm : flies) = true;
                }
                if (!flies)
                    loop(c, CommonSound::Flies, true);
                else if (!squirm)
                    loop(c, CommonSound::Squirm, true);
            }
            // Told by name: off (USE_OFF), to its death; toggled, to its
            // engine; on, a noise where whoever told it is.
            if (c.me.switched != 0)
            {
                const int use = c.me.switched - 1;
                c.me.switched = 0;
                if (use == kUseOff)
                {
                    c.m.flag2 = true;
                    c.m.count1 = kEngineDeath1;
                }
                else if (use == kUseToggle)
                {
                    c.m.flag2 = true;
                    c.m.count1 = kEngineIdle;
                }
                else if (use == kUseOn)
                {
                    aiSound(c.a, kSoundWorld, c.player.origin, 1024, 1000);
                }
            }
            if (c.me.script >= 0)
                return;
            if (c.m.flag2)
                dieThink(c);
            else
                cycle(c);
        }

        /// Each frame: its clip stepped at its own pace, forwards or back
        /// (StudioFrameAdvance), the events of what it passes; and its turn
        /// towards where it would face, 180 degrees a second (its yaw_speed
        /// of 18, ten times a second).
        inline void frame(AiCtx &c, int ms)
        {
            if (!c.me.active || c.me.script >= 0)
                return;
            const ModelView *model = actorModel(c.a, c.me);
            if (model != nullptr && c.me.clip >= 0)
            {
                const ModelClip &clip = model->clips[c.me.clip];
                const std::int64_t last = clip.frameCount > 0 ? static_cast<std::int64_t>(clip.frameCount - 1) << 8 : 0;
                const std::int64_t step = (static_cast<std::int64_t>(clip.fps) * c.m.value1 * ms) / (1000LL * kFxOne);
                const std::int64_t was = c.me.frame;
                std::int64_t now = c.m.flag1 ? was - step : was + step;
                bool done = false;
                if (now >= last)
                {
                    now = last;
                    done = true;
                }
                if (now <= 0 && c.m.flag1)
                {
                    now = 0;
                    done = true;
                }
                if (now < 0)
                    now = 0;
                c.me.frame = static_cast<std::uint32_t>(now);
                c.me.frozen = true;
                if (done)
                    c.m.seqFinished = true;
                // (Its events, forwards only, as the game's.)
                if (!c.m.flag1 && c.kind.animEvent != nullptr)
                {
                    for (int e = 0; e < clip.eventCount; ++e)
                    {
                        const ModelEvent &event = model->events[clip.firstEvent + e];
                        const std::int64_t at = static_cast<std::int64_t>(event.frame) << 8;
                        if (event.kind == kEventMonster && ((was < at && at <= now) || (at == 0 && was == 0 && now > 0)))
                            c.kind.animEvent(c, static_cast<int>(event.target));
                    }
                }
                c.me.lastFrame = c.me.frame;
            }
            const int diff = static_cast<std::int16_t>(c.m.idealYaw - c.me.yaw);
            const int most = 180 * 65536 / 360 * ms / 1000;
            c.me.yaw = static_cast<std::uint16_t>(c.me.yaw + (diff > most ? most : (diff < -most ? -most : diff)));
        }

        inline void animEvent(AiCtx &c, int event)
        {
            const int height = myHeight(c.m.other);
            switch (event)
            {
            case 1: // bang
            case 2: // tap scrape
            case 6: // light tap
                say(c, CommonSound::TentacleStrike, height);
                break;
            case 3: // the killing swing begins
                c.m.squadMember[1] = 200;
                break;
            case 4: // ... and ends
                c.m.squadMember[1] = 25;
                break;
            case 7:
                say(c, CommonSound::TentacleRoar, height);
                break;
            case 8:
                say(c, CommonSound::TentacleSearch, height);
                break;
            case 9:
                say(c, CommonSound::TentacleMove, height);
                break;
            default:
                break;
            }
        }

        /// TakeDamage: it is never killed by it -- down to 1, and into its
        /// pit.
        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            c.me.health = static_cast<std::int16_t>(d.amount >= c.me.health ? 1 : c.me.health - d.amount);
            return false;
        }

        inline bool killed(AiCtx &c)
        {
            c.m.count1 = kPitIdle;
            return true;
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Tentacle);
                k.health = kHealth;
                k.blood = kBloodYellow;
                k.think = think;
                k.frame = frame;
                k.animEvent = animEvent;
                k.takeDamage = takeDamage;
                k.killed = killed;
                return k;
            }();
            return k;
        }
    } // namespace tentacle

    // ============================================================ big momma

    namespace momma
    {
        inline constexpr int kAttackDist = 170;  ///< BIG_ATTACKDIST
        inline constexpr int kMortarDist = 800;  ///< BIG_MORTARDIST
        inline constexpr int kChildren = 6;      ///< the places kept after her for her young (the game's 20)
        // BIG_AE_*
        enum : int
        {
            kStep1 = 1, kStep2, kStep3, kStep4, kSack, kDeathSound, kAttackBr = 8, kAttackBl, kAttack1, kMortar,
            kLayCrab, kJumpForward, kScream, kPainSound, kAttackSound, kBirthSound, kEarlyTarget = 50,
        };
        inline constexpr std::uint32_t kMemoryChildPair = kMemoryCustom1;
        inline constexpr std::uint32_t kMemoryAdvanceNode = kMemoryCustom2;
        inline constexpr std::uint32_t kMemoryCompletedNode = kMemoryCustom3;
        inline constexpr std::uint32_t kMemoryFiredNode = kMemoryCustom4;
        enum : std::uint8_t
        {
            kSchedBigNode = kSchedLastCommon + 1, ///< SCHED_BIG_NODE
            kSchedNodeFail,                       ///< SCHED_NODE_FAIL
        };
        enum : std::uint8_t
        {
            kTaskMoveToNodeRange = kTaskLastCommon + 1,
            kTaskFindNode,
            kTaskPlayNodePresequence,
            kTaskPlayNodeSequence,
            kTaskProcessNode,
            kTaskWaitNode,
            kTaskNodeDelay,
            kTaskNodeYaw,
        };
        // m_hTargetEnt: other (the MommaNode record it is making for, or -1);
        // pev->netname: count1 (the name id of the node after it); m_nodeTime:
        // time1; m_crabTime: time2; m_mortarTime: time3; m_painSoundTime:
        // moveWaitFinished's partner, value1 (ms); pev->movedir: vec1. Her
        // young are the places kept after her (kChildren of them).

        inline constexpr AiTask tlBigNode[] = {
            {kTaskSetFailSchedule, kSchedNodeFail},
            {kTaskStopMoving, 0},
            {kTaskFindNode, 0},
            {kTaskPlayNodePresequence, 0},
            {kTaskMoveToNodeRange, 0},
            {kTaskStopMoving, 0},
            {kTaskNodeYaw, 0},
            {kTaskFaceIdeal, 0},
            {kTaskWaitNode, 0},
            {kTaskPlayNodeSequence, 0},
            {kTaskProcessNode, 0},
            {kTaskSetActivity, kActIdle},
        };
        inline constexpr AiSchedule slBigNode = {tlBigNode, 12, 0, 0, "Big Node"};
        inline constexpr AiTask tlNodeFail[] = {
            {kTaskNodeDelay, 10000},
            {kTaskSetActivity, kActIdle},
        };
        inline constexpr AiSchedule slNodeFail = {tlNodeFail, 2, 0, 0, "NodeFail"};

        inline const LogicRecord *node(const AiCtx &c)
        {
            return c.m.other >= 0 && c.m.other < c.w.recordCount ? &c.w.records[c.m.other] : nullptr;
        }

        inline void say(AiCtx &c, CommonSound sound)
        {
            if (c.w.heard != nullptr)
                c.w.heard->add(sound, c.me.pos, 127, kRadiusLarge, 100 + aiRandomLong(c.a, -5, 5));
        }

        /// NodeStart: on to the node of that name; with none, the path is
        /// done.
        inline void nodeStart(AiCtx &c, int name)
        {
            c.m.count1 = name;
            int found = -1;
            for (int r = 0; r < c.w.recordCount && name != 0 && found < 0; ++r)
            {
                if (c.w.records[r].name == name && c.w.records[r].kind == static_cast<std::uint8_t>(LogicKind::MommaNode))
                    found = r;
            }
            if (found < 0)
            {
                aiRemember(c, kMemoryPathFinished);
                return;
            }
            aiRemember(c, kMemoryOnPath);
            c.m.other = static_cast<std::int16_t>(found);
        }

        /// NodeReach: her health is the node's (if it gives any), what it
        /// fires is fired, and she is to go on (if it gives her none).
        inline void nodeReach(AiCtx &c)
        {
            aiForgetMemory(c, kMemoryAdvanceNode);
            const LogicRecord *at = node(c);
            if (at == nullptr)
                return;
            if (at->extra[0] != 0)
                c.me.health = static_cast<std::int16_t>(c.m.maxHealth = static_cast<int>(at->extra[0]) *
                                                                        skillOf(c.w).bigmommaHealthFactorTenths / 10);
            if (!aiHasMemory(c, kMemoryFiredNode) && at->extra[2] != 0)
                logicFire(c.w, c.player, static_cast<std::uint16_t>(at->extra[2]), -1, kUseToggle, 0);
            aiForgetMemory(c, kMemoryFiredNode);
            c.m.count1 = at->target;
            if (at->extra[0] == 0)
                aiRemember(c, kMemoryAdvanceNode);
        }

        /// Her young: a place after her that is free (never used, or whose
        /// last is dead), and how many are about.
        inline int freeChild(const AiCtx &c, int &alive)
        {
            alive = 0;
            int slot = -1;
            for (int n = 1; n <= kChildren && c.index + n < c.a.count; ++n)
            {
                const int i = c.index + n;
                const ActorState &child = c.a.states[i];
                if (c.a.records == nullptr || i >= c.a.recordCount ||
                    c.a.records[i].kind != static_cast<std::uint8_t>(ActorKind::BabyCrab))
                    break;
                if (child.active && child.ai < kAiDying)
                    ++alive;
                else if (slot < 0)
                    slot = i;
            }
            return slot;
        }

        /// CanLayCrab: in a while since the last, with room for one, and
        /// nothing in her way under her.
        inline bool canLayCrab(AiCtx &c)
        {
            int alive = 0;
            if (c.m.time2 >= c.ai.now || freeChild(c, alive) < 0)
                return false;
            for (int i = 0; i < c.a.count; ++i)
            {
                const ActorState &other = c.a.states[i];
                if (i == c.index || !other.active || !isMonster(other.kind))
                    continue;
                const fx d[2] = {other.pos[0] - c.me.pos[0], other.pos[1] - c.me.pos[1]};
                if (d[0] < fxFromInt(32) && d[0] > -fxFromInt(32) && d[1] < fxFromInt(32) && d[1] > -fxFromInt(32))
                    return false;
            }
            return true;
        }

        /// LayHeadcrab
        inline void layHeadcrab(AiCtx &c)
        {
            int alive = 0;
            const int slot = freeChild(c, alive);
            if (slot < 0)
                return;
            actorSetup(c.a, slot, c.a.records[slot], *c.w.world);
            ActorState &child = c.a.states[slot];
            child.flags &= static_cast<std::uint8_t>(~kActorDormant);
            for (int k = 0; k < 3; ++k)
                child.pos[k] = c.me.pos[k];
            child.yaw = c.me.yaw;
            actorToFloorSmall(*c.w.world, child);
            aiForget(c.a, slot);
            actorPlaceBox(c.a, slot);
            if (aiHasMemory(c, kMemoryChildPair))
            {
                c.m.time2 = c.ai.now + aiRandomMs(c.a, 5000, 10000);
                aiForgetMemory(c, kMemoryChildPair);
            }
            else
            {
                c.m.time2 = c.ai.now + aiRandomMs(c.a, 500, 2500);
                aiRemember(c, kMemoryChildPair);
            }
            say(c, CommonSound::MommaBirth);
        }

        /// LaunchMortar: a glob of her acid from her sack, as it was aimed.
        inline void launchMortar(AiCtx &c)
        {
            c.m.time3 = c.ai.now + aiRandomMs(c.a, 2000, 15000);
            const fx start[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(180)};
            say(c, CommonSound::MommaSack);
            projectileAdd(c.a, kShotMommaSpit, start, c.m.vec1, 10000);
        }

        inline void animEvent(AiCtx &c, int event)
        {
            fx forward[3], right[3];
            aiForward(c.me.yaw, forward);
            aiRight(c.me.yaw, right);
            switch (event)
            {
            case kAttackBr:
            case kAttackBl:
            case kAttack1:
            {
                // What is in a box 128 in front of her, 64 about and 64 high.
                const fx center[3] = {c.me.pos[0] + forward[0] * 128, c.me.pos[1] + forward[1] * 128, c.me.pos[2]};
                const fx lo[3] = {center[0] - fxFromInt(64), center[1] - fxFromInt(64), center[2]};
                const fx hi[3] = {center[0] + fxFromInt(64), center[1] + fxFromInt(64), center[2] + fxFromInt(64)};
                std::int16_t hurt = kAiNone;
                if (c.player.health > 0 && detail::playerTouchesBox(c.player, lo, hi, 0))
                    hurt = kAiPlayer;
                for (int i = 0; i < c.a.count && hurt == kAiNone; ++i)
                {
                    const ActorState &other = c.a.states[i];
                    if (i == c.index || !other.active || other.ai >= kAiDying || !canBeHurt(other.kind) ||
                        other.kind == static_cast<std::uint8_t>(ActorKind::BabyCrab))
                        continue;
                    fx mins[3], maxs[3];
                    targetBox(other, mins, maxs);
                    if (mins[0] <= hi[0] && maxs[0] >= lo[0] && mins[1] <= hi[1] && maxs[1] >= lo[1] && mins[2] <= hi[2] &&
                        maxs[2] >= lo[2])
                        hurt = static_cast<std::int16_t>(i);
                }
                if (hurt == kAiNone)
                    break;
                aiDamageTarget(c, hurt, skillOf(c.w).bigmommaDmgSlash, kDmgCrush | kDmgSlash);
                detail::aiPunch(c, hurt, fxFromInt(15), 0);
                const int side = event == kAttackBr ? -200 : (event == kAttackBl ? 200 : 0);
                const int ahead = event == kAttack1 ? 220 : 150;
                const fx shove[3] = {forward[0] * ahead + right[0] * side, forward[1] * ahead + right[1] * side,
                                     fxFromInt(event == kAttack1 ? 200 : 250)};
                detail::aiShove(c, hurt, shove);
                say(c, CommonSound::ClawStrike);
                break;
            }
            case kScream: say(c, CommonSound::MommaAlert); break;
            case kPainSound: say(c, CommonSound::MommaPain); break;
            case kAttackSound: say(c, CommonSound::MommaAttack); break;
            case kBirthSound: say(c, CommonSound::MommaBirth); break;
            case kSack:
                if (aiRandomLong(c.a, 0, 100) < 30)
                    say(c, CommonSound::MommaSack);
                break;
            case kDeathSound: say(c, CommonSound::MommaDie); break;
            case kStep1:
            case kStep2:
            case kStep3:
            case kStep4: say(c, CommonSound::MommaStep); break;
            case kMortar: launchMortar(c); break;
            case kLayCrab: layHeadcrab(c); break;
            case kJumpForward:
            {
                const fx jump[3] = {forward[0] * 200, forward[1] * 200, fxFromInt(500)};
                aiLeap(c, jump);
                break;
            }
            case kEarlyTarget:
                if (const LogicRecord *at = node(c))
                {
                    if (at->extra[2] != 0)
                        logicFire(c.w, c.player, static_cast<std::uint16_t>(at->extra[2]), -1, kUseToggle, 0);
                }
                aiRemember(c, kMemoryFiredNode);
                break;
            default:
                break;
            }
        }

        inline int yawSpeed(AiCtx &c) { return c.m.activity == kActIdle ? 100 : 90; }

        /// TraceAttack, TakeDamage: only her sack (hit group 1) is hurt by
        /// what hits her -- the rest is felt, and glances off; acid does her
        /// no harm; and while there is more of her path, she cannot die of
        /// it -- she goes on to the next node.
        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            if (d.type & kDmgAcid)
                d.amount = 0;
            if ((d.type & kDmgBullet) && d.group != 1)
            {
                d.amount = 0;
                d.graze = true;
            }
            else if (d.group == 1 && c.ai.now > c.m.value1)
            {
                c.m.value1 = c.ai.now + aiRandomMs(c.a, 1000, 3000);
                say(c, CommonSound::MommaPain);
            }
            d.group = kHitGeneric;
            if (!aiHasMemory(c, kMemoryPathFinished) && c.me.health <= d.amount)
            {
                c.me.health = static_cast<std::int16_t>(d.amount + 1);
                aiRemember(c, kMemoryAdvanceNode | kMemoryCompletedNode);
            }
            return true;
        }

        inline bool checkMelee1(AiCtx &, fx dot, fx dist) { return dot >= fxDot(0.7) && dist <= fxFromInt(kAttackDist); }
        inline bool checkMelee2(AiCtx &c, fx, fx) { return canLayCrab(c); }

        /// Her mortar: in range, in a while, and with an arc that reaches.
        inline bool checkRange1(AiCtx &c, fx, fx dist)
        {
            AiTarget t;
            if (dist > fxFromInt(kMortarDist) || c.m.time3 >= c.ai.now || c.m.enemy == kAiNone ||
                !aiTargetOf(c.w, c.player, c.m.enemy, t))
                return false;
            // VecCheckSplatToss: up as far as there is room, at most this
            // high, and down onto it.
            const fx start[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(180)};
            const fx mid[3] = {(start[0] + t.center[0]) / 2, (start[1] + t.center[1]) / 2, (start[2] + t.center[2]) / 2};
            const fx top[3] = {mid[0], mid[1], mid[2] + fxFromInt(aiRandomLong(c.a, 150, 500))};
            const Shot up = traceShot(*c.w.world, mid, top);
            const fx apex[3] = {mid[0], mid[1], mid[2] + static_cast<fx>((static_cast<std::int64_t>(top[2] - mid[2]) * up.fraction) >> kFractionShift)};
            if (!lineClear(*c.w.world, start, apex))
                return false;
            const int gravity = fxToInt(gravityOf(c.player));
            const int height = fxToInt(apex[2] - start[2]) - 15;
            if (gravity <= 0 || height <= 0)
                return false;
            const int speed = static_cast<int>(isqrt64(static_cast<std::uint64_t>(2) * gravity * height));
            const std::int64_t ms = static_cast<std::int64_t>(speed) * 1000 / gravity; // (to the top)
            for (int k = 0; k < 2; ++k)
                c.m.vec1[k] = static_cast<fx>((static_cast<std::int64_t>(t.center[k] - start[k]) * 500) / (ms > 0 ? ms : 1));
            c.m.vec1[2] = fxFromInt(speed);
            return true;
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            if (type == kSchedBigNode)
                return &slBigNode;
            if (type == kSchedNodeFail)
                return &slNodeFail;
            return aiBaseScheduleOfType(c, type);
        }

        inline const AiSchedule *getSchedule(AiCtx &c)
        {
            if (aiHasMemory(c, kMemoryAdvanceNode) && c.m.time1 < c.ai.now)
                return scheduleOfType(c, kSchedBigNode);
            return aiBaseGetSchedule(c);
        }

        /// A clip of her model by name (a node's).
        inline bool playNamed(AiCtx &c, std::uint32_t name)
        {
            const ModelView *model = actorModel(c.a, c.me);
            const int clip = model != nullptr && name != 0 ? model->findClip(name) : -1;
            if (clip < 0)
                return false;
            aiPlayClip(c, clip, true);
            c.m.seqFinished = false;
            return true;
        }

        inline bool startTask(AiCtx &c, const AiTask &task)
        {
            const LogicRecord *at = node(c);
            switch (task.task)
            {
            case kTaskFindNode:
                if (!aiHasMemory(c, kMemoryAdvanceNode) && at != nullptr)
                    c.m.count1 = at->target;
                nodeStart(c, c.m.count1);
                aiTaskComplete(c);
                return true;
            case kTaskNodeDelay:
                c.m.time1 = c.ai.now + task.data;
                aiTaskComplete(c);
                return true;
            case kTaskProcessNode:
                nodeReach(c);
                aiTaskComplete(c);
                return true;
            case kTaskPlayNodePresequence:
            case kTaskPlayNodeSequence:
            {
                const std::uint32_t name =
                    at == nullptr ? 0
                    : task.task == kTaskPlayNodeSequence
                        ? at->extra[3]
                        : (static_cast<std::uint32_t>(at->damage) | (static_cast<std::uint32_t>(at->pad) << 16));
                if (!playNamed(c, name))
                    aiTaskComplete(c);
                return true;
            }
            case kTaskNodeYaw:
                if (at != nullptr && at->maxs[0] != 0)
                    c.m.idealYaw = static_cast<std::uint16_t>(at->maxs[0]);
                else
                    c.m.idealYaw = c.me.yaw;
                aiTaskComplete(c);
                return true;
            case kTaskWaitNode:
                c.m.waitFinished = c.ai.now + (at != nullptr ? at->waitMs : 0);
                return true;
            case kTaskMoveToNodeRange:
            {
                if (at == nullptr)
                {
                    aiTaskFail(c);
                    return true;
                }
                const fx range = at->extra[1] != 0 ? fxFromInt(static_cast<int>(at->extra[1])) : fxFromInt(1000000 / 4096);
                if (aiDist(at->move, c.me.pos) < range)
                {
                    aiTaskComplete(c);
                    return true;
                }
                if (!aiMoveToLocation(c, (at->mins[0] & 1) ? kActRun : kActWalk, 2000, at->move))
                    aiTaskFail(c);
                return true;
            }
            case kTaskMeleeAttack1:
                say(c, CommonSound::MommaAttack);
                return false;
            default:
                return false;
            }
        }

        inline bool runTask(AiCtx &c, const AiTask &task)
        {
            const LogicRecord *at = node(c);
            switch (task.task)
            {
            case kTaskMoveToNodeRange:
            {
                if (at == nullptr)
                {
                    aiTaskFail(c);
                    return true;
                }
                const fx d[3] = {c.m.moveGoal[0] - c.me.pos[0], c.m.moveGoal[1] - c.me.pos[1], 0};
                const fx range = at->extra[1] != 0 ? fxFromInt(static_cast<int>(at->extra[1])) : fxFromInt(100000);
                if (fxLength(d) < range || aiMovementIsComplete(c))
                {
                    aiTaskComplete(c);
                    aiRouteClear(c);
                }
                return true;
            }
            case kTaskWaitNode:
                if (at != nullptr && (at->mins[0] & 2))
                    return true; // (SF_INFOBM_WAIT: for ever)
                if (c.ai.now > c.m.waitFinished)
                    aiTaskComplete(c);
                return true;
            case kTaskPlayNodePresequence:
            case kTaskPlayNodeSequence:
                if (c.m.seqFinished)
                {
                    c.m.activity = kActReset;
                    aiTaskComplete(c);
                }
                return true;
            default:
                return false;
            }
        }

        /// Activate: with no node yet, she is to make for her first.
        inline void started(AiCtx &c)
        {
            if (c.m.other < 0 && c.m.count1 == 0)
            {
                c.m.other = -1;
                // (Her first node: her record's, as the converter found it
                // by her netname.)
                const int first = c.a.records != nullptr && c.index < c.a.recordCount
                                      ? static_cast<int>(c.a.records[c.index].item & 0xFFFF) - 1
                                      : -1;
                c.m.count1 = first >= 0 && first < c.w.recordCount ? c.w.records[first].name : 0;
                aiRemember(c, kMemoryAdvanceNode);
            }
        }

        inline void sound(AiCtx &c, int which)
        {
            if (which == kAiSoundAlert)
                say(c, CommonSound::MommaAlert);
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::BigMomma);
                k.health = 150 * skillData(kSkillMedium).bigmommaHealthFactorTenths / 10; // (aiInit: the game's level's)
                k.eye = fxFromInt(128);
                k.fov = fxDot(0.3);
                k.blood = kBloodYellow;
                k.capability = kCapDoorsGroup;
                k.yawSpeed = yawSpeed;
                k.sound = sound;
                k.animEvent = animEvent;
                k.takeDamage = takeDamage;
                k.checkMelee1 = checkMelee1;
                k.checkMelee2 = checkMelee2;
                k.checkRange1 = checkRange1;
                k.scheduleOfType = scheduleOfType;
                k.getSchedule = getSchedule;
                k.startTask = startTask;
                k.runTask = runTask;
                k.started = started;
                return k;
            }();
            return k;
        }
    } // namespace momma

    // ============================================================ nihilanth

    namespace nihilanth
    {
        inline constexpr int kScale = 15;    ///< N_SCALE
        inline constexpr int kSpheres = 20;  ///< N_SPHERES
        inline constexpr int kHealth = 800;  ///< sk_nihilanth_health
        inline constexpr int kFriends = 6;   ///< the places kept after it: three slaves, three controllers
        // Its own think (HuntThink, DyingThink), as the game's is, ten times
        // a second. m_velocity: me.velocity; m_avelocity: vec1 (degrees a
        // second, Q12); m_posTarget, m_vecDesired, m_posDesired:
        // oldEnemyAt[0..2]; m_flForce: value1 (Q12); m_flMinZ, m_flMaxZ,
        // m_flAdj: moveGoal[0..2]; m_flLastSeen: time2; m_flPrevSeen: time3;
        // m_flShootEnd: time1; m_flShootTime: waitFinished;
        // m_flNextPainSound: moveWaitFinished; m_irritation: count1 (and
        // `mood`, for the renderer: its head's ball); m_iLevel, m_iTeleport,
        // m_iActiveSpheres: squadMember[0..2] (the spheres also in `reach`,
        // for the renderer: it draws them round it); m_hRecharger: other
        // (a Target record; -1 none); dying: flag1; dead: flag2; started:
        // flag3.

        inline fx *posTarget(AiCtx &c) { return c.m.oldEnemyAt[0]; }
        inline fx *vecDesired(AiCtx &c) { return c.m.oldEnemyAt[1]; }
        inline fx *posDesired(AiCtx &c) { return c.m.oldEnemyAt[2]; }

        inline fx unit(fx *v)
        {
            const fx length = fxLength(v);
            for (int k = 0; length > 0 && k < 3; ++k)
                v[k] = static_cast<fx>((static_cast<std::int64_t>(v[k]) << kFxShift) / length);
            return length;
        }
        inline fx dot(const fx *a, const fx *b) { return fxMul(a[0], b[0]) + fxMul(a[1], b[1]) + fxMul(a[2], b[2]); }

        /// The record of a name built from `base` and a number (the game's
        /// sprintf of "%s%d"), or -1; one that is dead (killed) is not there.
        inline int recordNamed(const AiCtx &c, const char *base, int number)
        {
            char text[32];
            int n = 0;
            for (const char *p = base; *p != '\0' && n < 24; ++p)
                text[n++] = *p;
            if (number >= 0)
            {
                char digits[8];
                int d = 0;
                int v = number;
                do
                {
                    digits[d++] = static_cast<char>('0' + v % 10);
                    v /= 10;
                } while (v > 0 && d < 7);
                while (d > 0)
                    text[n++] = digits[--d];
            }
            text[n] = '\0';
            const std::uint32_t hash = nameHash(text);
            for (int r = 0; r < c.w.recordCount && c.w.nameHashes != nullptr; ++r)
            {
                const std::uint16_t name = c.w.records[r].name;
                if (name != 0 && name < c.w.nameCount && c.w.nameHashes[name] == hash && !c.w.states[r].dead)
                    return r;
            }
            return -1;
        }
        /// Where a record is: its place (a Target), or its volume's middle.
        inline void placeOf(const AiCtx &c, int record, fx *at)
        {
            const LogicRecord &r = c.w.records[record];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Target))
            {
                for (int k = 0; k < 3; ++k)
                    at[k] = r.move[k];
                return;
            }
            for (int k = 0; k < 3; ++k)
                at[k] = fxFromInt((r.mins[k] + r.maxs[k]) / 2);
        }

        inline void say(AiCtx &c, CommonSound sound)
        {
            if (c.w.heard != nullptr)
                c.w.heard->add(sound, c.me.pos, 127, sound == CommonSound::NihilDie ? 10000 : 5000); // (attn 0.2; dying 0.1)
        }

        /// Where its hands and its head are (the game's attachments, as the
        /// model is held: no bones are posed to think).
        inline void hand(const AiCtx &c, int side, fx *at)
        {
            fx forward[3], right[3];
            aiForward(c.me.yaw, forward);
            aiRight(c.me.yaw, right);
            for (int k = 0; k < 3; ++k)
                at[k] = c.me.pos[k] + forward[k] * 120 + right[k] * (side == 0 ? -260 : 260);
            at[2] += fxFromInt(200);
        }

        /// Touch the player with a trigger_teleport of the map's (its Touch):
        /// to where it sends them.
        inline void teleport(AiCtx &c, int record)
        {
            if (record < 0)
                return;
            const LogicRecord &r = c.w.records[record];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::TriggerTeleport))
                return;
            for (int k = 0; k < 3; ++k)
            {
                c.player.origin[k] = r.move[k];
                c.player.velocity[k] = 0;
            }
            c.player.onGround = false;
            c.player.groundMover = -1;
            c.w.faceSet = true;
            c.w.faceYaw = static_cast<std::uint16_t>(r.extra[0]);
        }

        /// MakeFriend: an alien slave or a controller of its own, by where a
        /// ball of its landed (from the places kept after it).
        inline void makeFriend(AiCtx &c, const fx *at)
        {
            const bool controller = aiRandomLong(c.a, 0, 1) == 0;
            for (int n = 1; n <= kFriends && c.index + n < c.a.count; ++n)
            {
                const int i = c.index + n;
                if (c.a.records == nullptr || i >= c.a.recordCount)
                    break;
                const ActorRecord &record = c.a.records[i];
                const bool isController = record.kind == static_cast<std::uint8_t>(ActorKind::Controller);
                if (isController != controller ||
                    (record.kind != static_cast<std::uint8_t>(ActorKind::Slave) && !isController))
                    continue;
                const ActorState &there = c.a.states[i];
                if (there.active && there.ai != kAiDead)
                    continue;
                actorSetup(c.a, i, record, *c.w.world);
                ActorState &friendly = c.a.states[i];
                friendly.flags &= static_cast<std::uint8_t>(~kActorDormant);
                for (int k = 0; k < 3; ++k)
                    friendly.pos[k] = at[k];
                if (!controller)
                    actorToFloor(*c.w.world, friendly, fxFromInt(64), fxFromInt(512));
                aiForget(c.a, i);
                actorPlaceBox(c.a, i);
                if (c.w.heard != nullptr)
                    c.w.heard->add(CommonSound::BeamStart, friendly.pos, 127, kRadiusLarge);
                return;
            }
        }

        /// Its balls, a frame on (CNihilanthHVR): the zap, after the player
        /// and faster and faster, a jolt when it is near; the one that takes
        /// the player away (to where the map's teleport for its level sends
        /// them), or calls up a friend where it lands; and those it throws
        /// off dying, which only fly.
        inline void ballsTick(AiCtx &c, int ms)
        {
            for (Projectile &shot : c.a.shots)
            {
                if (!shot.active || (shot.kind != kShotNihilZap && shot.kind != kShotNihilPortal &&
                                     shot.kind != kShotNihilGreen))
                    continue;
                shot.leftMs -= ms;
                const fx target[3] = {c.player.origin[0], c.player.origin[1], c.player.origin[2]};
                const fx off[3] = {target[0] - shot.pos[0], target[1] - shot.pos[1], target[2] - shot.pos[2]};
                const fx far = fxLength(off);
                if (shot.kind == kShotNihilZap)
                {
                    // ZapThink: twenty times a second, 1.2 as fast, to 2000.
                    if (fxLength(shot.velocity) < fxFromInt(2000))
                        for (int k = 0; k < 3; ++k)
                            shot.velocity[k] += static_cast<fx>((static_cast<std::int64_t>(shot.velocity[k]) * 4 * ms) / 1000);
                    if (c.player.health > 0 && far < fxFromInt(256) && lineClear(*c.w.world, shot.pos, target))
                    {
                        aiDamageTarget(c, kAiPlayer, skillOf(c.w).nihilanthZap, kDmgShock);
                        if (c.w.heard != nullptr)
                            c.w.heard->add(CommonSound::Electro4, shot.pos, 64, kRadiusLarge);
                        effectAdd(c.a, kEffectBolt, shot.pos, target, 0, 200);
                        shot.active = false;
                        continue;
                    }
                }
                else if (shot.kind == kShotNihilPortal)
                {
                    // TeleportThink: after the player at 300 more each tenth;
                    // within 128, away they go.
                    if (c.player.health <= 0)
                    {
                        shot.active = false;
                        continue;
                    }
                    if (far < fxFromInt(128))
                    {
                        shot.active = false;
                        const int level = c.m.squadMember[1];
                        const int use = recordNamed(c, "n_leaving", level);
                        if (use >= 0)
                            logicUse(c.w, c.player, use, -1, kUseOn);
                        teleport(c, recordNamed(c, "n_teleport", level));
                        continue;
                    }
                    fx want[3] = {off[0], off[1], off[2]};
                    unit(want);
                    fx speed = fxLength(shot.velocity);
                    if (speed > fxFromInt(300))
                        for (int k = 0; k < 3; ++k)
                            shot.velocity[k] = static_cast<fx>((static_cast<std::int64_t>(shot.velocity[k]) * fxFromInt(300)) / speed);
                    for (int k = 0; k < 3; ++k)
                        shot.velocity[k] += static_cast<fx>((static_cast<std::int64_t>(want[k]) * 300 * ms) / 100);
                }
                fx to[3];
                for (int k = 0; k < 3; ++k)
                    to[k] = shot.pos[k] + static_cast<fx>((static_cast<std::int64_t>(shot.velocity[k]) * ms) / 1000);
                const Shot wall = traceShot(*c.w.world, shot.pos, to);
                if (wall.fraction < kFractionOne || shot.leftMs <= 0)
                {
                    for (int k = 0; k < 3; ++k)
                        shot.pos[k] += static_cast<fx>((static_cast<std::int64_t>(to[k] - shot.pos[k]) * wall.fraction) >> kFractionShift);
                    shot.active = false;
                    if (shot.kind == kShotNihilZap)
                    {
                        // ZapTouch: a jolt about it.
                        radiusDamage(c.w, c.player, shot.pos, 50, 125, c.index, kDmgShock, static_cast<std::int16_t>(c.index));
                        if (c.w.heard != nullptr)
                            c.w.heard->add(CommonSound::Electro4, shot.pos, 127, kRadiusLarge);
                    }
                    else if (shot.kind == kShotNihilPortal && wall.fraction < kFractionOne)
                    {
                        makeFriend(c, shot.pos); // (TeleportTouch, on what is not the player)
                    }
                    continue;
                }
                for (int k = 0; k < 3; ++k)
                    shot.pos[k] = to[k];
            }
        }

        /// A ball of `kind` from `at`, going `velocity`.
        inline void ball(AiCtx &c, std::uint8_t kind, const fx *at, const fx *velocity, int ms)
        {
            projectileAdd(c.a, kind, at, velocity, ms);
        }

        inline void floatSequence(AiCtx &c)
        {
            const char *name = c.m.count1 >= 2           ? "float_open"
                               : c.m.vec1[1] > fxFromInt(30)  ? "walk_r"
                               : c.m.vec1[1] < -fxFromInt(30) ? "walk_l"
                               : c.me.velocity[2] > fxFromInt(30) ? "walk_u"
                               : c.me.velocity[2] < -fxFromInt(30) ? "walk_d"
                                                                    : "float";
            const ModelView *model = actorModel(c.a, c.me);
            const int clip = model != nullptr ? model->findClip(nameHash(name)) : -1;
            if (clip >= 0)
                aiPlayClip(c, clip, true);
        }
        inline void playNamed(AiCtx &c, const char *name)
        {
            const ModelView *model = actorModel(c.a, c.me);
            const int clip = model != nullptr ? model->findClip(nameHash(name)) : -1;
            if (clip >= 0)
                aiPlayClip(c, clip, true);
        }
        inline bool playingNamed(const AiCtx &c, const char *name)
        {
            const ModelView *model = actorModel(c.a, c.me);
            return model != nullptr && c.me.clip >= 0 && model->clips[c.me.clip].name == nameHash(name);
        }

        /// EmitSphere: one more about it, from its recharger; false if it
        /// has them all.
        inline bool emitSphere(AiCtx &c)
        {
            if (c.m.squadMember[2] >= kSpheres)
                return false;
            ++c.m.squadMember[2];
            c.me.reach = c.m.squadMember[2];
            return true;
        }
        /// AbsorbSphere: one of them taken back in, for its health.
        inline bool absorbSphere(AiCtx &c)
        {
            if (c.m.squadMember[2] <= 0)
                return false;
            --c.m.squadMember[2];
            c.me.reach = c.m.squadMember[2];
            return true;
        }

        /// Flight: turned towards where it would go, lifted as far as it
        /// would be, slowing.
        inline void flight(AiCtx &c)
        {
            fx *av = c.m.vec1;
            fx right[3], up[3] = {0, 0, kFxOne};
            const std::uint16_t ahead = static_cast<std::uint16_t>(c.me.yaw + ((static_cast<std::int64_t>(av[1]) << 16) / (360 * kFxOne)));
            aiRight(ahead, right);
            if (dot(vecDesired(c), right) < 0)
            {
                if (av[1] < fxFromInt(180))
                    av[1] += fxFromInt(6);
            }
            else if (av[1] > -fxFromInt(180))
            {
                av[1] -= fxFromInt(6);
            }
            av[1] = fxMul(av[1], kFxOne * 98 / 100);
            fx est[3];
            for (int k = 0; k < 3; ++k)
                est[k] = c.me.pos[k] + c.me.velocity[k] * 2 + fxMul(up[k], c.m.value1) * 20;
            for (int k = 0; k < 3; ++k)
                c.me.velocity[k] += fxMul(up[k], c.m.value1);
            aiRight(c.me.yaw, right);
            for (int k = 0; k < 3; ++k)
            {
                const fx side = right[k] < 0 ? -right[k] : right[k];
                c.me.velocity[k] = fxMul(fxMul(c.me.velocity[k], kFxOne - side / 20), kFxOne * 995 / 1000);
            }
            if (c.m.value1 < fxFromInt(100) && est[2] < posDesired(c)[2])
                c.m.value1 += fxFromInt(10);
            else if (c.m.value1 > -fxFromInt(100) && est[2] > posDesired(c)[2])
                c.m.value1 -= fxFromInt(10);
        }

        /// NextActivity: what to do once what it plays is done.
        inline void nextActivity(AiCtx &c)
        {
            c.me.mood = static_cast<std::uint8_t>(c.m.count1);
            fx forward[3];
            aiForward(c.me.yaw, forward);
            if ((c.me.health < skillOf(c.w).nihilanthHealth / 2 || c.m.squadMember[2] < kSpheres / 2) && c.m.other < 0 && c.m.squadMember[0] <= 9)
            {
                // To the nearest of its rechargers for its level.
                const int recharger = recordNamed(c, "n_recharger", c.m.squadMember[0]);
                if (recharger >= 0)
                {
                    c.m.other = static_cast<std::int16_t>(recharger);
                    fx at[3];
                    placeOf(c, recharger, at);
                    posDesired(c)[0] = c.me.pos[0];
                    posDesired(c)[1] = c.me.pos[1];
                    posDesired(c)[2] = at[2];
                    fx want[3] = {at[0] - c.me.pos[0], at[1] - c.me.pos[1], 0};
                    unit(want);
                    for (int k = 0; k < 3; ++k)
                        vecDesired(c)[k] = want[k];
                }
                else
                {
                    c.m.other = -1;
                    if (++c.m.squadMember[0] > 9)
                        c.m.count1 = 2;
                }
            }
            const fx gone[3] = {posDesired(c)[0] - c.me.pos[0], posDesired(c)[1] - c.me.pos[1], posDesired(c)[2] - c.me.pos[2]};
            const fx dist = fxLength(gone);
            const fx facing = dot(vecDesired(c), forward);
            if (c.m.other >= 0)
            {
                if (dist < fxFromInt(128))
                {
                    if (!playingNamed(c, "recharge"))
                    {
                        const int draw = recordNamed(c, "n_draw", c.m.squadMember[0]);
                        if (draw >= 0)
                            logicUse(c.w, c.player, draw, -1, kUseOn);
                    }
                    playNamed(c, "recharge");
                }
                else
                {
                    floatSequence(c);
                }
                return;
            }
            if (c.m.time2 + 15000 < c.ai.now)
                c.m.enemy = kAiNone;
            if (c.m.enemy == kAiNone && c.player.health > 0 && !unseen(c.player))
            {
                AiTarget t;
                if (aiTargetOf(c.w, c.player, kAiPlayer, t) && aiDist(t.center, c.me.pos) < fxFromInt(4096) &&
                    lineClear(*c.w.world, c.me.pos, t.eye))
                    c.m.enemy = kAiPlayer;
            }
            if (c.m.enemy != kAiNone && c.m.count1 != 0 && c.m.time2 + 5000 > c.ai.now && dist < fxFromInt(256) &&
                facing > 0)
            {
                if (c.m.count1 >= 2 && c.me.health < skillOf(c.w).nihilanthHealth / 2)
                    playNamed(c, "attack1_open");
                else if (aiRandomLong(c.a, 0, 1) == 0)
                    playNamed(c, "attack1"); // (a zap)
                else if (recordNamed(c, "n_teleport", c.m.squadMember[1]) >= 0 ||
                         recordNamed(c, "n_leaving", c.m.squadMember[1]) >= 0)
                    playNamed(c, "attack2"); // (the teleport)
                else
                {
                    ++c.m.squadMember[1];
                    playNamed(c, "attack1");
                }
                return;
            }
            floatSequence(c);
        }

        inline void think(AiCtx &c)
        {
            if (!c.m.flag3)
            {
                // StartupThink: how high and low it goes, its spheres all
                // about it.
                c.m.flag3 = true;
                c.m.count1 = 0;
                c.m.moveGoal[2] = fxFromInt(512);
                const int low = recordNamed(c, "n_min", -1), high = recordNamed(c, "n_max", -1);
                c.m.moveGoal[0] = low >= 0 ? c.w.records[low].move[2] : -fxFromInt(4096);
                c.m.moveGoal[1] = high >= 0 ? c.w.records[high].move[2] : fxFromInt(4096);
                c.m.squadMember[0] = 1;
                c.m.squadMember[1] = 1;
                c.m.squadMember[2] = 0;
                for (int n = 0; n < kSpheres; ++n)
                    emitSphere(c);
                c.m.other = -1;
                vecDesired(c)[0] = kFxOne;
                posDesired(c)[0] = c.me.pos[0];
                posDesired(c)[1] = c.me.pos[1];
                posDesired(c)[2] = fxFromInt(512);
                c.me.health = static_cast<std::int16_t>(c.m.maxHealth = skillOf(c.w).nihilanthHealth);
            }
            // (Its recharger, taken away by the map -- its crystal broken --
            // is nobody's: the game's handle to it is empty.)
            if (c.m.other >= 0 && c.w.states[c.m.other].dead)
                c.m.other = -1;
            // CommandUse: on, it begins; off, the player is taken to the end.
            if (c.me.switched != 0)
            {
                const int use = c.me.switched - 1;
                c.me.switched = 0;
                if (use == kUseOn && c.m.count1 == 0)
                    c.m.count1 = 1;
                else if (use == kUseOff)
                    teleport(c, recordNamed(c, "n_ending", -1));
            }
            fx forward[3], right[3];
            aiForward(c.me.yaw, forward);
            aiRight(c.me.yaw, right);
            if (c.m.flag1)
            {
                // DyingThink: up to the top, the map told, turning about,
                // throwing off balls of light.
                if (!c.m.flag2)
                {
                    flight(c);
                    if (c.me.pos[2] > c.m.moveGoal[1] - fxFromInt(16))
                    {
                        c.me.velocity[0] = c.me.velocity[1] = c.me.velocity[2] = 0;
                        const int dead = recordNamed(c, "n_dead", -1);
                        if (dead >= 0)
                            logicFire(c.w, c.player, c.w.records[dead].name, -1, kUseOn, 0);
                        c.m.flag2 = true;
                    }
                }
                if (c.m.seqFinished)
                {
                    c.m.vec1[1] += fxFromInt(aiRandomLong(c.a, -100, 100));
                    c.m.vec1[1] = c.m.vec1[1] < -fxFromInt(100) ? -fxFromInt(100) : (c.m.vec1[1] > fxFromInt(100) ? fxFromInt(100) : c.m.vec1[1]);
                    playNamed(c, "die1");
                }
                fx head[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(400)};
                const fx velocity[3] = {fxFromInt(aiRandomLong(c.a, -420, 420)), fxFromInt(aiRandomLong(c.a, -420, 420)),
                                        fxFromInt(600)};
                if (aiRandomLong(c.a, 0, 2) == 0)
                    ball(c, kShotNihilGreen, head, velocity, 4000);
                return;
            }
            // HuntThink.
            if (c.me.health <= 0)
            {
                c.m.flag1 = true;
                c.m.seqFinished = true;
                say(c, CommonSound::NihilDie);
                posDesired(c)[2] = c.m.moveGoal[1];
                c.me.ai = kAiDying;
                return;
            }
            // (ShootBalls: two at a time, one from each hand, five a second.)
            while (c.m.time1 > c.ai.now && c.m.waitFinished < c.m.time1 && c.m.waitFinished < c.ai.now)
            {
                if (c.m.enemy != kAiNone)
                {
                    fx dir[3] = {posTarget(c)[0] - c.me.pos[0], posTarget(c)[1] - c.me.pos[1], posTarget(c)[2] - c.me.pos[2]};
                    unit(dir);
                    for (int side = 0; side < 2; ++side)
                    {
                        fx at[3];
                        hand(c, side, at);
                        const fx velocity[3] = {dir[0] * 200, dir[1] * 200, dir[2] * 200};
                        ball(c, kShotNihilZap, at, velocity, 8000);
                    }
                }
                c.m.waitFinished += 200;
            }
            if (c.me.health < skillOf(c.w).nihilanthHealth && absorbSphere(c))
                c.me.health = static_cast<std::int16_t>(c.me.health + skillOf(c.w).nihilanthHealth / kSpheres);
            if (c.m.seqFinished)
                nextActivity(c);
            if (c.m.enemy != kAiNone && c.m.other < 0)
            {
                AiTarget t;
                if (aiTargetOf(c.w, c.player, c.m.enemy, t) && lineClear(*c.w.world, c.me.pos, t.eye))
                {
                    if (c.m.time2 < c.ai.now - 5000)
                        c.m.time3 = c.ai.now;
                    c.m.time2 = c.ai.now;
                    for (int k = 0; k < 3; ++k)
                        posTarget(c)[k] = t.origin[k];
                    fx want[3] = {posTarget(c)[0] - c.me.pos[0], posTarget(c)[1] - c.me.pos[1], posTarget(c)[2] - c.me.pos[2]};
                    unit(want);
                    for (int k = 0; k < 3; ++k)
                        vecDesired(c)[k] = want[k];
                    posDesired(c)[0] = c.me.pos[0];
                    posDesired(c)[1] = c.me.pos[1];
                    posDesired(c)[2] = posTarget(c)[2] + c.m.moveGoal[2];
                }
                else
                {
                    c.m.moveGoal[2] = c.m.moveGoal[2] + fxFromInt(10) > fxFromInt(1000) ? fxFromInt(1000) : c.m.moveGoal[2] + fxFromInt(10);
                }
            }
            posDesired(c)[2] = posDesired(c)[2] > c.m.moveGoal[1] ? c.m.moveGoal[1]
                               : (posDesired(c)[2] < c.m.moveGoal[0] ? c.m.moveGoal[0] : posDesired(c)[2]);
            flight(c);
        }

        /// Each frame: where its velocity and its turning take it (it goes
        /// through what is in its way, as MOVETYPE_FLY's origin set by hand
        /// does), and its balls.
        inline void frame(AiCtx &c, int ms)
        {
            if (!c.me.active)
                return;
            if (c.m.flag3)
            {
                for (int k = 0; k < 3; ++k)
                    c.me.pos[k] += static_cast<fx>((static_cast<std::int64_t>(c.me.velocity[k]) * ms) / 1000);
                c.me.yaw = static_cast<std::uint16_t>(
                    c.me.yaw + static_cast<std::int32_t>((static_cast<std::int64_t>(c.m.vec1[1]) * 65536 * ms) / (360LL * kFxOne * 1000)));
                c.m.idealYaw = c.me.yaw;
                actorPlaceBox(c.a, c.index);
            }
            ballsTick(c, ms);
        }

        inline void animEvent(AiCtx &c, int event)
        {
            switch (event)
            {
            case 2: // zen: balls from its hands, for a second
                if (c.m.enemy != kAiNone)
                {
                    if (aiRandomLong(c.a, 0, 4) == 0)
                        say(c, CommonSound::NihilAttack);
                    say(c, CommonSound::NihilBall);
                    c.m.waitFinished = c.ai.now;
                    c.m.time1 = c.ai.now + 1000;
                }
                break;
            case 3: // prayer: the ball that takes the player away
                if (c.m.enemy != kAiNone)
                {
                    const int level = c.m.squadMember[1];
                    if (recordNamed(c, "n_teleport", level) >= 0 || recordNamed(c, "n_leaving", level) >= 0)
                    {
                        say(c, CommonSound::NihilAttack);
                        fx at[3];
                        hand(c, 0, at);
                        const fx velocity[3] = {(c.me.pos[0] - at[0]), (c.me.pos[1] - at[1]), (c.me.pos[2] - at[2]) / 5};
                        ball(c, kShotNihilPortal, at, velocity, 30000);
                    }
                    else
                    {
                        ++c.m.squadMember[1];
                        say(c, CommonSound::NihilBall);
                        c.m.waitFinished = c.ai.now;
                        c.m.time1 = c.ai.now + 1000;
                    }
                }
                break;
            case 4: // a sphere from its recharger
                if (c.m.other >= 0 && !emitSphere(c))
                    c.m.other = -1;
                break;
            case 5:
                say(c, CommonSound::NihilRecharge);
                break;
            case 6: // a zap, open
                if (c.m.enemy != kAiNone)
                {
                    fx at[3];
                    hand(c, 0, at);
                    fx dir[3] = {c.player.origin[0] - at[0], c.player.origin[1] - at[1], c.player.origin[2] - at[2]};
                    unit(dir);
                    const fx velocity[3] = {dir[0] * 200, dir[1] * 200, dir[2] * 200};
                    ball(c, kShotNihilZap, at, velocity, 8000);
                }
                break;
            default:
                break;
            }
        }

        /// TraceAttack and TakeDamage: its head, open, is where it can be
        /// hurt to its death -- until then it stays at 1; it laughs while it
        /// has much health, cries out when it has not and its head is open.
        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            if (c.m.count1 == 3)
                c.m.count1 = 2;
            if (c.m.count1 == 2 && d.group == 2 && d.amount > 2)
                c.m.count1 = 3;
            if (d.amount >= c.me.health)
            {
                c.me.health = 1;
                if (c.m.count1 != 3)
                    return false;
            }
            if (c.m.moveWaitFinished <= c.ai.now)
            {
                c.m.moveWaitFinished = c.ai.now + aiRandomMs(c.a, 2000, 5000);
                if (c.me.health > skillOf(c.w).nihilanthHealth / 2)
                    say(c, CommonSound::NihilLaugh);
                else if (c.m.count1 >= 2)
                    say(c, CommonSound::NihilPain);
            }
            c.me.health = static_cast<std::int16_t>(c.me.health - d.amount);
            return false;
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Nihilanth);
                k.health = kHealth;
                k.eye = fxFromInt(300);
                k.fov = -kFxOne; // (all the way round)
                k.blood = kBloodYellow;
                k.think = think;
                k.frame = frame;
                k.animEvent = animEvent;
                k.takeDamage = takeDamage;
                return k;
            }();
            return k;
        }
    } // namespace nihilanth

    // ================================================================= boids

    namespace boid
    {
        inline constexpr int kFlySpeed = 125; ///< AFLOCK_FLY_SPEED
        inline constexpr int kTurnRate = 75;  ///< AFLOCK_TURN_RATE
        inline constexpr int kAccelerate = 10;
        inline constexpr int kCheckDist = 192;
        inline constexpr int kTooClose = 100;
        inline constexpr int kTooFar = 256;
        // CFlockingFlyer, its own think. Its flock: the boids cooked with it,
        // the first its leader (its record's item, plus one, on the others;
        // the leader's is 0). pev->speed: value1 (Q12); armorvalue (the speed
        // it flaps to): value2; m_fTurning: flag1; started (Start): flag2;
        // m_flLastBlockedTime: time1; m_flFakeBlockedTime: time2;
        // m_flAlertTime: time3; m_flFlockNextSoundTime: waitFinished;
        // m_flGoalSpeed: moveGoal[0]; pev->avelocity: vec1 (degrees a
        // second); its pitch and roll as drawn: wantPitch, wantYaw.

        /// Its leader now: its own if that is about; else the first of its
        /// flock that is (SquadRemove's new leader) -- itself, if that is it.
        inline int leaderOf(const AiCtx &c)
        {
            const int first = c.a.records != nullptr && c.index < c.a.recordCount
                                  ? static_cast<int>(c.a.records[c.index].item & 0xFFFF) - 1
                                  : -1;
            const int head = first < 0 ? c.index : first;
            for (int i = head; i < c.a.count && i < c.a.recordCount; ++i)
            {
                const ActorRecord &r = c.a.records[i];
                if (r.kind != static_cast<std::uint8_t>(ActorKind::Boid) ||
                    (i != head && static_cast<int>(r.item & 0xFFFF) - 1 != head))
                    break;
                if (c.a.states[i].active && c.a.states[i].ai < kAiDying)
                    return i;
            }
            return c.index;
        }

        inline void axes(const AiCtx &c, fx *forward, fx *right)
        {
            fx sy, cy, sp, cp;
            fxSinCos(c.me.yaw, sy, cy);
            fxSinCos(static_cast<std::uint16_t>(c.me.wantPitch), sp, cp);
            forward[0] = fxMulShort(cp, cy);
            forward[1] = fxMulShort(cp, sy);
            forward[2] = sp;
            right[0] = sy;
            right[1] = -cy;
            right[2] = 0;
        }

        /// BoidAdvanceFrame: its wings by how its speed changes, and its
        /// tipping back towards level.
        inline void advanceFrame(AiCtx &c)
        {
            fx flap = (c.m.value1 - c.m.value2) / kAccelerate;
            c.m.value2 = fxMul(c.m.value2, kFxOne * 8 / 10) + fxMul(c.m.value1, kFxOne * 2 / 10);
            flap = flap < 0 ? -flap : flap;
            flap = flap < kFxOne / 4 ? kFxOne / 4 : (flap > kFxOne * 19 / 10 ? kFxOne * 19 / 10 : flap);
            // (Turns to degrees, Q12: * 360 * 4096 >> 16 is * 45 >> 1, in 32 bits.)
            const fx pitch = (static_cast<std::int16_t>(c.me.wantPitch) * 45) >> 1;
            const fx roll = (static_cast<std::int16_t>(c.me.wantYaw) * 45) >> 1;
            // (The game's pitch is the view's: up is negative there, and
            // positive here.)
            c.m.vec1[0] = -(-pitch + flap * 5);
            c.m.vec1[2] = -(roll + c.m.vec1[1]);
        }

        /// FPathBlocked: three feelers ahead; and, clear a while, a moment
        /// of feigned blocking (it turns now and then for nothing).
        inline bool pathBlocked(AiCtx &c)
        {
            if (c.m.time2 > c.ai.now)
            {
                c.m.time1 = c.ai.now;
                return true;
            }
            fx forward[3], right[3];
            axes(c, forward, right);
            bool blocked = false;
            for (int side = -1; side <= 1; ++side)
            {
                const fx from[3] = {c.me.pos[0] + right[0] * 12 * side, c.me.pos[1] + right[1] * 12 * side, c.me.pos[2]};
                const fx to[3] = {from[0] + forward[0] * kCheckDist, from[1] + forward[1] * kCheckDist,
                                  from[2] + forward[2] * kCheckDist};
                if (traceShot(*c.w.world, from, to).fraction < kFractionOne)
                    blocked = true;
            }
            if (blocked)
                c.m.time1 = c.ai.now;
            else if (c.ai.now - c.m.time1 > 6000)
                c.m.time2 = c.ai.now + aiRandomMs(c.a, 1000, 3000);
            return blocked;
        }

        inline void say(AiCtx &c)
        {
            if (c.w.heard != nullptr)
                c.w.heard->add(c.m.time3 > c.ai.now ? CommonSound::BoidAlert : CommonSound::BoidIdle, c.me.pos, 127,
                               kRadiusLarge);
        }

        inline void leaderThink(AiCtx &c)
        {
            fx forward[3], right[3];
            axes(c, forward, right);
            if (!pathBlocked(c))
            {
                if (c.m.flag1)
                {
                    c.m.flag1 = false;
                    c.m.vec1[1] = 0;
                }
                if (c.m.value1 <= fxFromInt(kFlySpeed))
                    c.m.value1 += fxFromInt(5);
                for (int k = 0; k < 3; ++k)
                    c.me.velocity[k] = fxMul(forward[k], c.m.value1);
                advanceFrame(c);
                return;
            }
            if (!c.m.flag1)
            {
                // Which way is clearer.
                auto room = [&](int side)
                {
                    const fx to[3] = {c.me.pos[0] + right[0] * kCheckDist * side, c.me.pos[1] + right[1] * kCheckDist * side,
                                      c.me.pos[2]};
                    return traceShot(*c.w.world, c.me.pos, to).fraction;
                };
                const std::int64_t rightSide = room(1), leftSide = room(-1);
                c.m.vec1[1] = fxFromInt(rightSide > leftSide    ? -kTurnRate
                                        : leftSide > rightSide  ? kTurnRate
                                        : aiRandomLong(c.a, 0, 1) ? kTurnRate
                                                                  : -kTurnRate);
                c.m.flag1 = true;
            }
            // SpreadFlock: those too near it, turned a little away.
            for (int i = 0; i < c.a.count; ++i)
            {
                ActorState &other = c.a.states[i];
                if (i == c.index || !other.active || other.ai >= kAiDying ||
                    other.kind != static_cast<std::uint8_t>(ActorKind::Boid) || aiDist(other.pos, c.me.pos) > fxFromInt(kTooClose))
                    continue;
                fx away[3] = {other.pos[0] - c.me.pos[0], other.pos[1] - c.me.pos[1], other.pos[2] - c.me.pos[2]};
                const fx a = fxLength(away);
                const fx speed = fxLength(other.velocity);
                for (int k = 0; a > 0 && k < 3; ++k)
                    away[k] = static_cast<fx>((static_cast<std::int64_t>(away[k]) << kFxShift) / a);
                fx v[3] = {other.velocity[0], other.velocity[1], other.velocity[2]};
                const fx vl = fxLength(v);
                for (int k = 0; vl > 0 && k < 3; ++k)
                    v[k] = static_cast<fx>((static_cast<std::int64_t>(v[k]) << kFxShift) / vl);
                for (int k = 0; k < 3; ++k)
                    other.velocity[k] = fxMul((v[k] + away[k]) / 2, speed);
            }
            for (int k = 0; k < 3; ++k)
                c.me.velocity[k] = fxMul(forward[k], c.m.value1);
            const fx below[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] - fxFromInt(16)};
            if (traceShot(*c.w.world, c.me.pos, below).fraction < kFractionOne && c.me.velocity[2] < 0)
                c.me.velocity[2] = 0;
            if (c.m.waitFinished < c.ai.now)
            {
                say(c);
                c.m.waitFinished = c.ai.now + aiRandomMs(c.a, 1000, 3000);
            }
            advanceFrame(c);
        }

        inline void followerThink(AiCtx &c, int leader)
        {
            const ActorState &head = c.a.states[leader];
            fx toLeader[3] = {head.pos[0] - c.me.pos[0], head.pos[1] - c.me.pos[1], head.pos[2] - c.me.pos[2]};
            const fx dist = fxLength(toLeader);
            c.me.yaw = head.yaw;
            c.me.wantPitch = head.wantPitch;
            c.me.wantYaw = head.wantYaw;
            const fx leaderSpeed = fxLength(head.velocity);
            if (aiDot2D(c.me.yaw, c.me.pos, head.pos) > fxDot(0.2))
            {
                if (dist > fxFromInt(kTooFar))
                    c.m.moveGoal[0] = leaderSpeed * 3 / 2;
                else if (dist < fxFromInt(kTooClose))
                    c.m.moveGoal[0] = leaderSpeed / 2;
            }
            else
            {
                c.m.moveGoal[0] = leaderSpeed / 2;
            }
            // SpreadFlock2: away from those too near.
            for (int i = 0; i < c.a.count; ++i)
            {
                const ActorState &other = c.a.states[i];
                if (i == c.index || !other.active || other.kind != static_cast<std::uint8_t>(ActorKind::Boid) ||
                    aiDist(other.pos, c.me.pos) > fxFromInt(kTooClose))
                    continue;
                fx away[3] = {c.me.pos[0] - other.pos[0], c.me.pos[1] - other.pos[1], c.me.pos[2] - other.pos[2]};
                const fx a = fxLength(away);
                for (int k = 0; a > 0 && k < 3; ++k)
                    c.me.velocity[k] += static_cast<fx>((static_cast<std::int64_t>(away[k]) << kFxShift) / a);
            }
            c.m.value1 = fxLength(c.me.velocity);
            fx v[3] = {c.me.velocity[0], c.me.velocity[1], c.me.velocity[2]};
            if (c.m.value1 > 0)
                for (int k = 0; k < 3; ++k)
                    v[k] = static_cast<fx>((static_cast<std::int64_t>(v[k]) << kFxShift) / c.m.value1);
            if (dist > fxFromInt(kTooFar))
            {
                for (int k = 0; dist > 0 && k < 3; ++k)
                    v[k] = (v[k] + static_cast<fx>((static_cast<std::int64_t>(toLeader[k]) << kFxShift) / dist)) / 2;
            }
            if (c.m.moveGoal[0] > fxFromInt(kFlySpeed * 2))
                c.m.moveGoal[0] = fxFromInt(kFlySpeed * 2);
            if (c.m.value1 < c.m.moveGoal[0])
                c.m.value1 += fxFromInt(kAccelerate);
            else if (c.m.value1 > c.m.moveGoal[0])
                c.m.value1 -= fxFromInt(kAccelerate);
            for (int k = 0; k < 3; ++k)
                c.me.velocity[k] = fxMul(v[k], c.m.value1);
            advanceFrame(c);
        }

        inline void think(AiCtx &c)
        {
            if (c.me.ai >= kAiDying)
                return;
            if (!c.m.flag2)
            {
                // IdleThink: waiting for someone to see it.
                if (!aiClientInPvs(c))
                    return;
                c.m.flag2 = true;
                c.m.value1 = fxFromInt(kFlySpeed);
            }
            const int leader = leaderOf(c);
            if (leader == c.index)
                leaderThink(c);
            else
                followerThink(c, leader);
        }

        /// Each frame: where it flies, and how it turns and tips; dead, it
        /// falls, and lies where it lands (FallHack).
        inline void frame(AiCtx &c, int ms)
        {
            if (!c.m.flag2 && c.me.ai < kAiDying)
                return;
            if (c.me.ai >= kAiDying)
                c.me.velocity[2] -= static_cast<fx>((static_cast<std::int64_t>(gravityOf(c.player)) * ms) / 1000);
            fx to[3];
            for (int k = 0; k < 3; ++k)
                to[k] = c.me.pos[k] + static_cast<fx>((static_cast<std::int64_t>(c.me.velocity[k]) * ms) / 1000);
            const Shot wall = traceShot(*c.w.world, c.me.pos, to);
            if (wall.fraction >= kFractionOne)
            {
                for (int k = 0; k < 3; ++k)
                    c.me.pos[k] = to[k];
            }
            else
            {
                // (Up to two units short of what it meets: not on its face.)
                const fx step[3] = {to[0] - c.me.pos[0], to[1] - c.me.pos[1], to[2] - c.me.pos[2]};
                const fx length = fxLength(step);
                const fx reach = static_cast<fx>((static_cast<std::int64_t>(length) * wall.fraction) >> kFractionShift) - fxFromInt(2);
                for (int k = 0; reach > 0 && length > 0 && k < 3; ++k)
                    c.me.pos[k] += static_cast<fx>((static_cast<std::int64_t>(step[k]) * reach) / length);
                c.me.velocity[0] = c.me.velocity[1] = c.me.velocity[2] = 0;
            }
            if (c.me.ai < kAiDying)
            {
                auto turn = [&](fx rate) { return static_cast<std::int32_t>((static_cast<std::int64_t>(rate) * 65536 * ms) / (360LL * kFxOne * 1000)); };
                c.me.yaw = static_cast<std::uint16_t>(c.me.yaw + turn(c.m.vec1[1]));
                c.m.idealYaw = c.me.yaw;
                c.me.wantPitch = static_cast<std::uint16_t>(c.me.wantPitch + turn(c.m.vec1[0]));
                c.me.wantYaw = static_cast<std::uint16_t>(c.me.wantYaw + turn(c.m.vec1[2]));
            }
            actorPlaceBox(c.a, c.index);
        }

        /// Killed: its flock alarmed a while; it falls.
        inline bool killed(AiCtx &c)
        {
            for (int i = 0; i < c.a.count; ++i)
            {
                if (c.a.states[i].active && c.a.states[i].kind == static_cast<std::uint8_t>(ActorKind::Boid))
                    if (AiMonster *m = aiOf(c.a, i))
                        m->time3 = c.ai.now + 15000;
            }
            c.me.ai = kAiDying;
            c.m.deadflag = 2;
            c.m.state = c.m.idealState = kStateDead;
            c.me.flags &= static_cast<std::uint8_t>(~kActorSolid);
            c.me.velocity[0] = c.me.velocity[1] = 0;
            c.me.frozen = true;
            c.me.wantPitch = c.me.wantYaw = 0;
            return true;
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Boid);
                k.health = 10;
                k.fov = fxDot(0.2);
                k.blood = kBloodYellow;
                k.think = think;
                k.frame = frame;
                k.killed = killed;
                return k;
            }();
            return k;
        }
    } // namespace boid

    inline const AiKind *aiMilitaryKindOf(std::uint8_t kind); // (HlMilitary.hpp)

    inline const AiKind *aiAlienKindOf(std::uint8_t kind)
    {
        switch (static_cast<ActorKind>(kind))
        {
        case ActorKind::Cockroach:  return &roach::kind();
        case ActorKind::Snark:      return &snark::kind();
        case ActorKind::AlienGrunt: return &agrunt::kind();
        case ActorKind::Controller: return &controller::kind();
        case ActorKind::Leech:      return &leech::kind();
        case ActorKind::Gargantua:  return &garg::kind();
        case ActorKind::BabyCrab:   return &babycrab::kind();
        case ActorKind::Ichthyosaur: return &icky::kind();
        case ActorKind::Tentacle:   return &tentacle::kind();
        case ActorKind::BigMomma:   return &momma::kind();
        case ActorKind::Nihilanth:  return &nihilanth::kind();
        case ActorKind::Boid:       return &boid::kind();
        case ActorKind::Rat:        return &rat::kind();
        default:                    return aiMilitaryKindOf(kind);
        }
    }
} // namespace hldata
