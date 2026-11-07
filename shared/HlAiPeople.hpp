#pragma once

// The people, each from its own file of the game's: the scientist and the
// one at his desk (dlls/scientist.cpp), the guard (barney.cpp), the man in
// the suit (gman.cpp) and whatever a map makes of a monster_generic
// (genericmonster.cpp) -- what each does on top of CTalkMonster
// (HlAiTalk.hpp) and CBaseMonster (HlAiCore.hpp). The numbers are the
// game's at its middle difficulty.
//
// Included at the end of HlMonsters.hpp. std-only and inline: the host
// tests run it.

namespace hldata
{
    namespace detail
    {
        /// One of the people's own cry (pain, death), with his voice.
        inline void personSay(AiCtx &c, CommonSound sound)
        {
            if (c.w.heard != nullptr)
                c.w.heard->add(sound, c.me.pos, 127, kAttnNorm, talk::voicePitch(c));
        }
    } // namespace detail

    // ============================================================ scientist

    namespace scientist
    {
        inline constexpr int kHealWaitMs = 60000; ///< ... once a minute
        // His animation events.
        inline constexpr int kAeHeal = 1, kAeNeedleOn = 2, kAeNeedleOff = 3;

        enum : std::uint8_t
        {
            kTaskSayHeal = kTaskTlkLast,
            kTaskHeal,
            kTaskSayFear,
            kTaskRunPathScared,
            kTaskScream,
            kTaskRandomScream,
            kTaskMoveToTargetRangeScared,
        };
        enum : std::uint8_t
        {
            kSchedHide = kSchedTlkLast,
            kSchedFear,
            kSchedPanic,
            kSchedStartle,
            kSchedTargetChaseScared,
            kSchedTargetFaceScared,
        };

        inline constexpr AiTask tlFollow[] = {{kTaskSetFailSchedule, kSchedCantFollow}, {kTaskMoveToTargetRange, 128}};
        inline constexpr AiSchedule slFollow = {tlFollow, 2,
                                                kCondNewEnemy | kCondLightDamage | kCondHeavyDamage | kCondHearSound,
                                                kSoundCombat | kSoundDanger, "Follow"};

        inline constexpr AiTask tlFollowScared[] = {{kTaskSetFailSchedule, kSchedTargetChase},
                                                    {kTaskMoveToTargetRangeScared, 128}};
        inline constexpr AiSchedule slFollowScared = {
            tlFollowScared, 2, kCondNewEnemy | kCondHearSound | kCondLightDamage | kCondHeavyDamage, kSoundDanger,
            "FollowScared"};

        inline constexpr AiTask tlFaceTargetScared[] = {
            {kTaskFaceTarget, 0}, {kTaskSetActivity, kActCrouchIdle}, {kTaskSetSchedule, kSchedTargetChaseScared}};
        inline constexpr AiSchedule slFaceTargetScared = {tlFaceTargetScared, 3, kCondHearSound | kCondNewEnemy,
                                                          kSoundDanger, "FaceTargetScared"};

        inline constexpr AiTask tlStopFollowing[] = {{kTaskCantFollow, 0}};
        inline constexpr AiSchedule slStopFollowing = {tlStopFollowing, 1, 0, 0, "StopFollowing"};

        inline constexpr AiTask tlHeal[] = {
            {kTaskMoveToTargetRange, 50},           {kTaskSetFailSchedule, kSchedTargetChase},
            {kTaskFaceIdeal, 0},                    {kTaskSayHeal, 0},
            {kTaskPlaySequenceFaceTarget, kActArm}, {kTaskHeal, 0},
            {kTaskPlaySequenceFaceTarget, kActDisarm},
        };
        inline constexpr AiSchedule slHeal = {tlHeal, 7, 0, 0, "Heal"};

        inline constexpr AiTask tlFaceTarget[] = {
            {kTaskStopMoving, 0}, {kTaskFaceTarget, 0}, {kTaskSetActivity, kActIdle}, {kTaskSetSchedule, kSchedTargetChase}};
        inline constexpr AiSchedule slFaceTarget = {tlFaceTarget, 4, kCondClientPush | kCondNewEnemy | kCondHearSound,
                                                    kSoundCombat | kSoundDanger, "FaceTarget"};

        inline constexpr AiTask tlSciPanic[] = {
            {kTaskStopMoving, 0}, {kTaskFaceEnemy, 0}, {kTaskScream, 0},
            {kTaskPlaySequenceFaceEnemy, kActExcited}, {kTaskSetActivity, kActIdle}};
        inline constexpr AiSchedule slSciPanic = {tlSciPanic, 5, 0, 0, "SciPanic"};

        inline constexpr AiTask tlIdleSciStand[] = {
            {kTaskStopMoving, 0}, {kTaskSetActivity, kActIdle}, {kTaskWait, 2000}, {kTaskTlkHeadReset, 0}};
        inline constexpr AiSchedule slIdleSciStand = {
            tlIdleSciStand, 4,
            kCondNewEnemy | kCondLightDamage | kCondHeavyDamage | kCondHearSound | kCondSmell | kCondClientPush |
                kCondProvoked,
            kSoundCombat | kSoundDanger | kSoundMeat | kSoundCarcass | kSoundGarbage, "IdleSciStand"};

        inline constexpr AiTask tlScientistCover[] = {
            {kTaskSetFailSchedule, kSchedPanic}, {kTaskStopMoving, 0}, {kTaskFindCoverFromEnemy, 0},
            {kTaskRunPathScared, 0},             {kTaskTurnLeft, 179}, {kTaskSetSchedule, kSchedHide}};
        inline constexpr AiSchedule slScientistCover = {tlScientistCover, 6, kCondNewEnemy, 0, "ScientistCover"};

        inline constexpr AiTask tlScientistHide[] = {
            {kTaskSetFailSchedule, kSchedPanic}, {kTaskStopMoving, 0}, {kTaskPlaySequence, kActCrouch},
            {kTaskSetActivity, kActCrouchIdle},  {kTaskWaitRandom, 10000}};
        inline constexpr AiSchedule slScientistHide = {
            tlScientistHide, 5,
            kCondNewEnemy | kCondHearSound | kCondSeeEnemy | kCondSeeHate | kCondSeeFear | kCondSeeDislike, kSoundDanger,
            "ScientistHide"};

        inline constexpr AiTask tlScientistStartle[] = {
            {kTaskSetFailSchedule, kSchedPanic},     {kTaskRandomScream, 300}, {kTaskStopMoving, 0},
            {kTaskPlaySequenceFaceEnemy, kActCrouch}, {kTaskRandomScream, 100},
            {kTaskPlaySequenceFaceEnemy, kActCrouchIdle}, {kTaskWaitRandom, 1000}};
        inline constexpr AiSchedule slScientistStartle = {
            tlScientistStartle, 7, kCondNewEnemy | kCondSeeEnemy | kCondSeeHate | kCondSeeFear | kCondSeeDislike, 0,
            "ScientistStartle"};

        inline constexpr AiTask tlFear[] = {{kTaskStopMoving, 0}, {kTaskFaceEnemy, 0}, {kTaskSayFear, 0}};
        inline constexpr AiSchedule slFear = {tlFear, 3, kCondNewEnemy, 0, "Fear"};

        /// DisregardEnemy: dead, or not seen for fifteen seconds.
        inline bool disregardEnemy(AiCtx &c)
        {
            return !aiEnemyAlive(c, c.m.enemy) || c.ai.now - c.m.talk.fearTime > 15000;
        }

        /// TargetDistance: to whom it follows (1e6 if none, or dead).
        inline fx targetDistance(AiCtx &c)
        {
            AiTarget t;
            if (c.m.targetEnt == kAiNone || !aiTargetOf(c.w, c.player, c.m.targetEnt, t) || !t.alive)
                return fxFromInt(30000);
            return aiDist(t.origin, c.me.pos);
        }

        /// CanHeal
        inline bool canHeal(AiCtx &c)
        {
            if (c.m.talk.healTime > c.ai.now || c.m.targetEnt == kAiNone)
                return false;
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.targetEnt, t))
                return false;
            const int most = t.isPlayer ? 100 : (aiFind(c.a, t.id) != nullptr ? aiFind(c.a, t.id)->maxHealth : 0);
            return t.health * 2 <= most;
        }
        /// Heal: the needle in, if close enough.
        inline void heal(AiCtx &c)
        {
            if (!canHeal(c))
                return;
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.targetEnt, t) || aiDist(t.origin, c.me.pos) > fxFromInt(100))
                return;
            const int heal = skillOf(c.w).scientistHeal;
            if (t.isPlayer)
            {
                c.player.health = c.player.health + heal > 100 ? 100 : c.player.health + heal;
            }
            else if (AiMonster *m = aiFind(c.a, t.id))
            {
                ActorState &them = c.a.states[t.id];
                them.health = static_cast<std::int16_t>(them.health + heal > m->maxHealth ? m->maxHealth
                                                                                         : them.health + heal);
            }
            c.m.talk.healTime = c.ai.now + kHealWaitMs;
        }

        /// Scream
        inline void scream(AiCtx &c)
        {
            if (talk::okToSpeak(c))
            {
                talk::talkFor(c, 10000);
                c.m.talk.target = c.m.enemy;
                talk::playSentence(c, kTalkScream, aiRandomMs(c.a, 3000, 6000), kAttnNorm);
            }
        }

        inline void sound(AiCtx &c, int which)
        {
            if (which != kAiSoundPain && which != kAiSoundDeath)
                return;
            // PainSound (and DeathSound, which is the same).
            if (c.ai.now < c.m.talk.painTime)
                return;
            c.m.talk.painTime = c.ai.now + aiRandomMs(c.a, 500, 750);
            static const CommonSound kPain[] = {CommonSound::ScientistPain, CommonSound::ScientistPain2,
                                                CommonSound::ScientistPain3, CommonSound::ScientistPain4,
                                                CommonSound::ScientistPain5};
            detail::personSay(c, kPain[aiRandomLong(c.a, 0, 4)]);
        }

        inline int yawSpeed(AiCtx &c)
        {
            switch (c.m.activity)
            {
            case kActIdle: return 120;
            case kActWalk: return 180;
            case kActRun:  return 150;
            case kActTurnLeft:
            case kActTurnRight: return 120;
            default: return 90;
            }
        }

        inline std::uint8_t stoppedActivity(AiCtx &c)
        {
            return c.m.enemy != kAiNone ? kActExcited : kActIdle;
        }

        /// SetActivity: one his model has not got, he stands.
        inline void setActivity(AiCtx &c, std::uint8_t activity)
        {
            if (aiLookupActivity(c, activity) < 0)
                activity = kActIdle;
            talk::setActivity(c, activity);
        }

        inline void animEvent(AiCtx &c, int event)
        {
            switch (event)
            {
            case kAeHeal: heal(c); break;
            case kAeNeedleOn: c.me.body = static_cast<std::uint8_t>(c.me.body % 4 + 4); break;
            case kAeNeedleOff: c.me.body = static_cast<std::uint8_t>(c.me.body % 4); break;
            default: break;
            }
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            switch (type)
            {
            case kSchedTargetFace:
            {
                const AiSchedule *s = talk::scheduleOfType(c, type);
                return s == &slIdleStand ? &slFaceTarget : s;
            }
            case kSchedTargetChase: return &slFollow;
            case kSchedCantFollow: return &slStopFollowing;
            case kSchedPanic: return &slSciPanic;
            case kSchedTargetChaseScared: return &slFollowScared;
            case kSchedTargetFaceScared: return &slFaceTargetScared;
            case kSchedIdleStand:
            {
                const AiSchedule *s = talk::scheduleOfType(c, type);
                return s == &slIdleStand ? &slIdleSciStand : s;
            }
            case kSchedHide: return &slScientistHide;
            case kSchedStartle: return &slScientistStartle;
            case kSchedFear: return &slFear;
            default: return talk::scheduleOfType(c, type);
            }
        }

        inline const AiSchedule *getSchedule(AiCtx &c)
        {
            if (aiHas(c, kCondHearSound))
            {
                const AiSound *sound = aiBestSound(c);
                if (sound != nullptr && (sound->type & kSoundDanger))
                    return aiScheduleOfType(c, kSchedTakeCoverFromBestSound);
            }
            switch (c.m.state)
            {
            case kStateAlert:
            case kStateIdle:
            {
                if (c.m.enemy != kAiNone)
                {
                    if (aiHas(c, kCondSeeEnemy))
                        c.m.talk.fearTime = c.ai.now;
                    else if (disregardEnemy(c))
                        c.m.enemy = kAiNone;
                }
                if (aiHas(c, kCondLightDamage | kCondHeavyDamage))
                    return aiScheduleOfType(c, kSchedSmallFlinch);
                // Something frightening heard: he ducks (not more than every
                // three seconds).
                if (aiHas(c, kCondHearSound))
                {
                    const AiSound *sound = aiBestSound(c);
                    if (sound != nullptr && (sound->type & (kSoundDanger | kSoundCombat)) &&
                        c.ai.now - c.m.talk.fearTime > 3000)
                    {
                        c.m.talk.fearTime = c.ai.now;
                        return aiScheduleOfType(c, kSchedStartle);
                    }
                }
                if (talk::isFollowing(c))
                {
                    if (c.player.health <= 0)
                    {
                        talk::stopFollowing(c, false);
                        break;
                    }
                    const int relationship = c.m.enemy != kAiNone ? aiRelationship(c, c.m.enemy) : kRelNone;
                    if (relationship != kRelDislike && relationship != kRelHate)
                    {
                        if (targetDistance(c) <= fxFromInt(128))
                        {
                            if (canHeal(c))
                                return &slHeal;
                            if (aiHas(c, kCondClientPush))
                                return aiScheduleOfType(c, kSchedMoveAwayFollow);
                        }
                        return aiScheduleOfType(c, kSchedTargetFace);
                    }
                    if (aiHas(c, kCondNewEnemy))
                        return aiScheduleOfType(c, kSchedFear);
                    return aiScheduleOfType(c, kSchedTargetFaceScared);
                }
                if (aiHas(c, kCondClientPush))
                    return aiScheduleOfType(c, kSchedMoveAway);
                talk::trySmellTalk(c);
                break;
            }
            case kStateCombat:
                if (aiHas(c, kCondNewEnemy))
                    return &slFear;
                if (aiHas(c, kCondSeeEnemy))
                    return &slScientistCover;
                if (aiHas(c, kCondHearSound))
                    return &slTakeCoverFromBestSound;
                return &slScientistCover;
            default:
                break;
            }
            return aiBaseGetSchedule(c);
        }

        inline std::uint8_t idealState(AiCtx &c)
        {
            switch (c.m.state)
            {
            case kStateAlert:
            case kStateIdle:
                if (aiHas(c, kCondNewEnemy))
                {
                    if (talk::isFollowing(c))
                    {
                        const int relationship = aiRelationship(c, c.m.enemy);
                        // (As shipped: anything but fear keeps him with the
                        // player -- and so does fear, unhurt.)
                        if (relationship != kRelFear ||
                            (relationship != kRelHate && !aiHas(c, kCondLightDamage | kCondHeavyDamage)))
                        {
                            c.m.idealState = kStateAlert;
                            return c.m.idealState;
                        }
                        talk::stopFollowing(c, true);
                    }
                }
                else if (aiHas(c, kCondLightDamage | kCondHeavyDamage))
                {
                    if (talk::isFollowing(c))
                        talk::stopFollowing(c, true);
                }
                break;
            case kStateCombat:
                if (c.m.enemy != kAiNone)
                {
                    if (disregardEnemy(c))
                    {
                        c.m.idealState = kStateAlert;
                        c.m.enemy = kAiNone;
                        return c.m.idealState;
                    }
                    if (c.m.targetEnt != kAiNone)
                    {
                        c.m.idealState = kStateAlert;
                        return c.m.idealState;
                    }
                    if (aiHas(c, kCondSeeEnemy))
                    {
                        c.m.talk.fearTime = c.ai.now;
                        c.m.idealState = kStateCombat;
                        return c.m.idealState;
                    }
                }
                break;
            default:
                break;
            }
            return aiBaseIdealState(c);
        }

        inline bool startTask(AiCtx &c, const AiTask &task)
        {
            switch (task.task)
            {
            case kTaskSayHeal:
                talk::talkFor(c, 2000);
                c.m.talk.target = c.m.targetEnt;
                talk::playSentence(c, kTalkHeal, 2000, kAttnIdle);
                aiTaskComplete(c);
                return true;
            case kTaskScream:
                scream(c);
                aiTaskComplete(c);
                return true;
            case kTaskRandomScream:
                if (aiRandomLong(c.a, 0, 999) < task.data)
                    scream(c);
                aiTaskComplete(c);
                return true;
            case kTaskSayFear:
                if (talk::okToSpeak(c))
                {
                    talk::talkFor(c, 2000);
                    c.m.talk.target = c.m.enemy;
                    talk::playSentence(c, c.m.enemy == kAiPlayer ? kTalkPlFear : kTalkFear, 5000, kAttnNorm);
                }
                aiTaskComplete(c);
                return true;
            case kTaskHeal:
                c.m.idealActivity = kActMeleeAttack1;
                return true;
            case kTaskRunPathScared:
                c.m.movementActivity = kActRunScared;
                return true;
            case kTaskMoveToTargetRangeScared:
            {
                AiTarget t;
                if (!aiTargetOf(c.w, c.player, c.m.targetEnt, t))
                {
                    aiTaskFail(c);
                    return true;
                }
                fx feet[3];
                aiFeetOf(t, feet);
                if (aiDist(feet, c.me.pos) < fxFromInt(1))
                {
                    aiTaskComplete(c);
                    return true;
                }
                c.m.movementActivity = kActWalkScared;
                // (MoveToTarget(ACT_WALK_SCARED, 0.5))
                if (!aiBuildRoute(c, kMoveGoalTarget, feet, 500))
                    aiTaskFail(c);
                return true;
            }
            default:
                return talk::startTask(c, task);
            }
        }

        inline bool runTask(AiCtx &c, const AiTask &task)
        {
            switch (task.task)
            {
            case kTaskRunPathScared:
                if (aiMovementIsComplete(c))
                    aiTaskComplete(c);
                if (aiRandomLong(c.a, 0, 31) < 8)
                    scream(c);
                return true;
            case kTaskMoveToTargetRangeScared:
            {
                if (aiRandomLong(c.a, 0, 63) < 8)
                    scream(c);
                AiTarget t;
                if (c.m.enemy == kAiNone || !aiTargetOf(c.w, c.player, c.m.targetEnt, t))
                {
                    aiTaskFail(c);
                    return true;
                }
                fx feet[3];
                aiFeetOf(t, feet);
                const fx range = fxFromInt(task.data);
                fx distance = aiDist2D(c.m.moveGoal, c.me.pos);
                if (distance < range || aiDist(c.m.moveGoal, feet) > range / 2)
                {
                    for (int k = 0; k < 3; ++k)
                        c.m.moveGoal[k] = feet[k];
                    distance = aiDist2D(c.m.moveGoal, c.me.pos);
                    if (distance >= range && c.m.movementGoal != kMoveGoalNone)
                        aiBuildRoute(c, kMoveGoalTarget, feet);
                }
                if (distance < range)
                {
                    aiTaskComplete(c);
                    aiRouteClear(c);
                }
                else if (distance < fxFromInt(190) && c.m.movementActivity != kActWalkScared)
                    c.m.movementActivity = kActWalkScared;
                else if (distance >= fxFromInt(270) && c.m.movementActivity != kActRunScared)
                    c.m.movementActivity = kActRunScared;
                return true;
            }
            case kTaskHeal:
                if (c.m.seqFinished)
                {
                    aiTaskComplete(c);
                }
                else
                {
                    if (targetDistance(c) > fxFromInt(90))
                        aiTaskComplete(c);
                    AiTarget t;
                    if (aiTargetOf(c.w, c.player, c.m.targetEnt, t))
                    {
                        aiMakeIdealYaw(c, t.origin);
                        aiChangeYaw(c);
                    }
                }
                return true;
            default:
                return talk::runTask(c, task);
            }
        }

        /// TakeDamage: the player who hurts him (by his own hand, not a
        /// blast's) provokes him, and he follows no more.
        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            if (d.attacker == kAiPlayer && !(d.type & kDmgBlast))
            {
                aiRemember(c, kMemoryProvoked);
                talk::stopFollowing(c, true);
            }
            talk::takeDamage(c, d);
            return true;
        }

        inline bool killed(AiCtx &c)
        {
            talk::killed(c);
            return false;
        }

        inline void started(AiCtx &c)
        {
            // His voice by his head (TalkInit).
            static const std::uint8_t kPitch[4] = {105, 100, 95, 100};
            c.m.talk.voicePitch = kPitch[c.me.body % 4];
            talk::started(c);
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Scientist);
                k.health = 20;
                k.eye = fxFromInt(50);
                k.fov = fxDot(-0.7); // VIEW_FIELD_WIDE
                k.capability = kCapHear | kCapTurnHead | kCapOpenDoors | kCapAutoDoors | kCapUse;
                k.soundMask = kSoundWorld | kSoundCombat | kSoundCarcass | kSoundMeat | kSoundGarbage | kSoundDanger |
                              kSoundPlayer;
                k.blood = kBloodRed;
                k.getSchedule = getSchedule;
                k.scheduleOfType = scheduleOfType;
                k.startTask = startTask;
                k.runTask = runTask;
                k.animEvent = animEvent;
                k.relationship = talk::relationship;
                k.prescheduleThink = talk::prescheduleThink;
                k.yawSpeed = yawSpeed;
                k.sound = sound;
                k.takeDamage = takeDamage;
                k.idealState = idealState;
                k.killed = killed;
                k.setActivity = setActivity;
                k.stoppedActivity = stoppedActivity;
                k.started = started;
                k.frame = talk::frame;
                k.coverRadius = 1200;
                return k;
            }();
            return k;
        }
    } // namespace scientist

    // =============================================================== barney

    namespace barney
    {
        inline constexpr int kAeDraw = 2, kAeShoot = 3, kAeHolster = 4;
        inline constexpr int kBodyHolstered = 0, kBodyDrawn = 1, kBodyGone = 2;

        inline constexpr AiTask tlBaFollow[] = {{kTaskMoveToTargetRange, 128}, {kTaskSetSchedule, kSchedTargetFace}};
        inline constexpr AiSchedule slBaFollow = {
            tlBaFollow, 2, kCondNewEnemy | kCondLightDamage | kCondHeavyDamage | kCondHearSound | kCondProvoked,
            kSoundDanger, "Follow"};

        inline constexpr AiTask tlBarneyEnemyDraw[] = {
            {kTaskStopMoving, 0}, {kTaskFaceEnemy, 0}, {kTaskPlaySequenceFaceEnemy, kActArm}};
        inline constexpr AiSchedule slBarneyEnemyDraw = {tlBarneyEnemyDraw, 3, 0, 0, "Barney Enemy Draw"};

        inline constexpr AiTask tlBaFaceTarget[] = {
            {kTaskSetActivity, kActIdle}, {kTaskFaceTarget, 0}, {kTaskSetActivity, kActIdle},
            {kTaskSetSchedule, kSchedTargetChase}};
        inline constexpr AiSchedule slBaFaceTarget = {
            tlBaFaceTarget, 4,
            kCondClientPush | kCondNewEnemy | kCondLightDamage | kCondHeavyDamage | kCondHearSound | kCondProvoked,
            kSoundDanger, "FaceTarget"};

        inline constexpr AiTask tlIdleBaStand[] = {
            {kTaskStopMoving, 0}, {kTaskSetActivity, kActIdle}, {kTaskWait, 2000}, {kTaskTlkHeadReset, 0}};
        inline constexpr AiSchedule slIdleBaStand = {
            tlIdleBaStand, 4,
            kCondNewEnemy | kCondLightDamage | kCondHeavyDamage | kCondHearSound | kCondSmell | kCondProvoked,
            kSoundCombat | kSoundDanger | kSoundMeat | kSoundCarcass | kSoundGarbage, "IdleStand"};

        /// m_fGunDrawn (kept where the port's guard kept his: `gun`, 2 out).
        inline bool gunDrawn(const AiCtx &c) { return c.me.gun == 2; }

        inline void sound(AiCtx &c, int which)
        {
            switch (which)
            {
            case kAiSoundAlert:
                // AlertSound: "Freeze!"
                if (c.m.enemy != kAiNone && talk::okToSpeak(c))
                    talk::playSentence(c, kTalkAttack, aiRandomMs(c.a, 2800, 3200), kAttnIdle);
                break;
            case kAiSoundPain:
            {
                if (c.ai.now < c.m.talk.painTime)
                    return;
                c.m.talk.painTime = c.ai.now + aiRandomMs(c.a, 500, 750);
                static const CommonSound kPain[] = {CommonSound::GuardPain, CommonSound::GuardPain2,
                                                    CommonSound::GuardPain3};
                detail::personSay(c, kPain[aiRandomLong(c.a, 0, 2)]);
                break;
            }
            case kAiSoundDeath:
            {
                static const CommonSound kDie[] = {CommonSound::GuardDie, CommonSound::GuardDie2,
                                                   CommonSound::GuardDie3};
                detail::personSay(c, kDie[aiRandomLong(c.a, 0, 2)]);
                break;
            }
            default:
                break;
            }
        }

        inline int yawSpeed(AiCtx &c) { return c.m.activity == kActRun ? 90 : 70; }

        /// CheckRangeAttack1: within 1024, faced, and a line to it -- looked
        /// at once every second and a half.
        inline bool checkRange1(AiCtx &c, fx dot, fx dist)
        {
            if (dist > fxFromInt(1024) || dot < fxDot(0.5))
                return false;
            if (c.ai.now > c.m.time1)
            {
                AiTarget t;
                bool clear = false;
                if (aiTargetOf(c.w, c.player, c.m.enemy, t))
                {
                    const fx from[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(55)};
                    fx to[3], dir[3];
                    for (int k = 0; k < 3; ++k)
                        to[k] = t.center[k] - t.origin[k] + c.m.enemyLkp[k];
                    const fx d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
                    const fx length = fxLength(d);
                    for (int k = 0; k < 3; ++k)
                        dir[k] = length > 0 ? static_cast<fx>((static_cast<std::int64_t>(d[k]) << kFxShift) / length) : 0;
                    fx end[3];
                    const std::int16_t hit = aiTraceLine(c, from, dir, fxToInt(length) + 1, end);
                    clear = hit == c.m.enemy ||
                            (hit == kAiNone && aiDist(end, to) < fxFromInt(2));
                }
                c.m.flag1 = clear;               // m_lastAttackCheck
                c.m.time1 = c.ai.now + 1500;     // m_checkAttackTime (+1.0, then +1.5: as shipped)
            }
            return c.m.flag1;
        }

        /// BarneyFirePistol: a round at his enemy, two degrees loose.
        inline void firePistol(AiCtx &c)
        {
            const fx gun[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + fxFromInt(55)};
            fx dir[3];
            aiShootAtEnemy(c, gun, dir);
            aiSetBlending(c, dir); // (his arm up or down at it)
            // (VECTOR_CONE_2DEGREES: its sine, Q12.)
            const fx cone = 71;
            fx right[3], up[3];
            const fx flat[3] = {dir[0], dir[1], 0};
            const fx across = fxLength(flat);
            right[0] = across > 0 ? static_cast<fx>((static_cast<std::int64_t>(dir[1]) << kFxShift) / across) : kFxOne;
            right[1] = across > 0 ? static_cast<fx>((static_cast<std::int64_t>(-dir[0]) << kFxShift) / across) : 0;
            right[2] = 0;
            // (Unit vectors in Q12: 16-bit parts.)
            up[0] = fxMulShort(right[1], dir[2]);
            up[1] = -fxMulShort(right[0], dir[2]);
            up[2] = fxMulShort(right[0], dir[1]) - fxMulShort(right[1], dir[0]);
            const int x = aiRandomLong(c.a, -2048, 2048) + aiRandomLong(c.a, -2048, 2048);
            const int y = aiRandomLong(c.a, -2048, 2048) + aiRandomLong(c.a, -2048, 2048);
            fx aim[3];
            for (int k = 0; k < 3; ++k)
                aim[k] = dir[k] + static_cast<fx>((static_cast<std::int64_t>(right[k]) * x * cone +
                                                   static_cast<std::int64_t>(up[k]) * y * cone) >>
                                                  (2 * kFxShift));
            fx end[3];
            const std::int16_t hit = aiTraceLine(c, gun, aim, 1024, end);
            effectAdd(c.a, kEffectTracer, gun, end, 0, 60);
            if (hit == kAiNone)
            {
                const fx far[3] = {gun[0] + aim[0] * 1024, gun[1] + aim[1] * 1024, gun[2] + aim[2] * 1024};
                markShot(c.a, *c.w.world, gun, far, false);
            }
            else
            {
                const int group = hit >= 0 ? hitGroup(c.a, c.a.states[hit], gun, end) : kHitBody;
                if (hit >= 0)
                    bleed(c.w, hit, end, aim, false);
                aiDamageTarget(c, hit, skillOf(c.w).monster9mmBullet, kDmgBullet | kDmgNeverGib, static_cast<std::uint8_t>(group)); // (BULLET_MONSTER_9MM)
            }
            // The flash at his gun's muzzle, where his hand has it (the
            // clip's 5001 event: attachment 0); the round left from his
            // middle, as the game's does.
            fx flash[3] = {gun[0] + dir[0] * 24, gun[1] + dir[1] * 24, gun[2] + dir[2] * 24};
            actorAttachment(c.a, c.me, 0, flash);
            effectAdd(c.a, kEffectMuzzle, flash, nullptr, 0, 50);
            int shift = aiRandomLong(c.a, 0, 20);
            shift = shift > 10 ? 0 : shift - 5;
            if (c.w.heard != nullptr)
                c.w.heard->add(CommonSound::GuardShot, c.me.pos, 127, kAttnNorm, 100 + shift);
            aiSound(c.a, kSoundCombat, c.me.pos, 384, 300);
        }

        inline void animEvent(AiCtx &c, int event)
        {
            switch (event)
            {
            case kAeShoot: firePistol(c); break;
            case kAeDraw:
                c.me.body = kBodyDrawn;
                c.me.gun = 2;
                break;
            case kAeHolster:
                c.me.body = kBodyHolstered;
                c.me.gun = 0;
                break;
            default: break;
            }
        }

        /// His pistol, to the floor before him: the item the converter left
        /// where he stands (DropItem "weapon_9mmhandgun").
        inline void dropGun(AiCtx &c)
        {
            if (c.me.body >= kBodyGone || c.index >= c.a.recordCount || c.a.records == nullptr)
                return;
            c.me.body = kBodyGone;
            const ActorRecord &mine = c.a.records[c.index];
            fx forward[3];
            aiForward(c.me.yaw, forward);
            for (int slot = 0; slot < c.a.recordCount && slot < c.a.count; ++slot)
            {
                const ActorRecord &record = c.a.records[slot];
                if (record.kind != static_cast<std::uint8_t>(ActorKind::Item) || !(record.flags & kActorDormant) ||
                    record.item != static_cast<std::uint32_t>(ItemKind::Glock) || c.a.states[slot].active ||
                    record.origin[0] != mine.origin[0] || record.origin[1] != mine.origin[1] ||
                    record.origin[2] != mine.origin[2])
                    continue;
                actorSetup(c.a, slot, record, *c.w.world);
                ActorState &item = c.a.states[slot];
                item.flags &= static_cast<std::uint8_t>(~kActorDormant);
                item.pos[0] = c.me.pos[0] + forward[0] * 24;
                item.pos[1] = c.me.pos[1] + forward[1] * 24;
                item.pos[2] = c.me.pos[2] + fxFromInt(36);
                pointToFloor(*c.w.world, item.pos, fxFromInt(128));
                actorPlaceBox(c.a, slot);
                break;
            }
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            switch (type)
            {
            case kSchedArmWeapon:
                if (c.m.enemy != kAiNone)
                    return &slBarneyEnemyDraw;
                break;
            case kSchedTargetFace:
            {
                const AiSchedule *s = talk::scheduleOfType(c, type);
                return s == &slIdleStand ? &slBaFaceTarget : s;
            }
            case kSchedTargetChase: return &slBaFollow;
            case kSchedIdleStand:
            {
                const AiSchedule *s = talk::scheduleOfType(c, type);
                return s == &slIdleStand ? &slIdleBaStand : s;
            }
            default: break;
            }
            return talk::scheduleOfType(c, type);
        }

        inline const AiSchedule *getSchedule(AiCtx &c)
        {
            if (aiHas(c, kCondHearSound))
            {
                const AiSound *sound = aiBestSound(c);
                if (sound != nullptr && (sound->type & kSoundDanger))
                    return aiScheduleOfType(c, kSchedTakeCoverFromBestSound);
            }
            if (aiHas(c, kCondEnemyDead) && talk::okToSpeak(c))
                talk::playSentence(c, kTalkKill, 4000, kAttnNorm);
            switch (c.m.state)
            {
            case kStateCombat:
                if (aiHas(c, kCondEnemyDead))
                    return aiBaseGetSchedule(c);
                if (aiHas(c, kCondNewEnemy) && aiHas(c, kCondLightDamage))
                    return aiScheduleOfType(c, kSchedSmallFlinch);
                if (!gunDrawn(c))
                    return aiScheduleOfType(c, kSchedArmWeapon);
                if (aiHas(c, kCondHeavyDamage))
                    return aiScheduleOfType(c, kSchedTakeCoverFromEnemy);
                break;
            case kStateAlert:
            case kStateIdle:
                if (aiHas(c, kCondLightDamage | kCondHeavyDamage))
                    return aiScheduleOfType(c, kSchedSmallFlinch);
                if (c.m.enemy == kAiNone && talk::isFollowing(c))
                {
                    if (c.player.health <= 0)
                    {
                        talk::stopFollowing(c, false);
                        break;
                    }
                    if (aiHas(c, kCondClientPush))
                        return aiScheduleOfType(c, kSchedMoveAwayFollow);
                    return aiScheduleOfType(c, kSchedTargetFace);
                }
                if (aiHas(c, kCondClientPush))
                    return aiScheduleOfType(c, kSchedMoveAway);
                talk::trySmellTalk(c);
                break;
            default:
                break;
            }
            return aiBaseGetSchedule(c);
        }

        /// TraceAttack: a vest takes half of a bullet, a cut or a blast; his
        /// helmet twenty of a bullet, a cut or a club -- all of it, and it
        /// rings off -- and is the head for all that.
        inline bool traceAttack(AiCtx &c, AiDamage &d)
        {
            switch (d.group)
            {
            case 2: // HITGROUP_CHEST
            case kHitStomach:
                if (d.type & (kDmgBullet | kDmgSlash | kDmgBlast))
                    d.amount /= 2;
                break;
            case 10: // his helmet
                if (d.type & (kDmgBullet | kDmgSlash | kDmgClub))
                {
                    d.amount -= 20;
                    if (d.amount <= 0)
                    {
                        detail::aiSay(c, aiRandomLong(c.a, 0, 1) ? CommonSound::Ricochet1 : CommonSound::Ricochet2);
                        d.amount = 0;
                        d.graze = true;
                    }
                }
                d.group = kHitHead;
                break;
            default:
                break;
            }
            return true;
        }

        /// TakeDamage: hurt by the player -- with nothing else to fight,
        /// "watch it" the first time unless he was plainly aimed at, then
        /// he is provoked.
        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            talk::takeDamage(c, d);
            const bool survives = c.me.health - d.amount * (d.group == kHitHead ? 3 : 1) > 0;
            if (!aiAlive(c.a, c.index) || !survives || c.m.state == kStateProne || d.attacker != kAiPlayer)
                return true;
            if (c.m.enemy == kAiNone)
            {
                // IsFacing: the player looks within fifteen degrees of him.
                const bool facing = talk::playerLooksAt(c, c.me.pos) > fxDot(0.96);
                if (aiHasMemory(c, kMemorySuspicious) || facing)
                {
                    talk::playSentence(c, kTalkMad, 4000, kAttnNorm);
                    aiRemember(c, kMemoryProvoked);
                    talk::stopFollowing(c, true);
                }
                else
                {
                    talk::playSentence(c, kTalkShot, 4000, kAttnNorm);
                    aiRemember(c, kMemorySuspicious);
                }
            }
            else if (c.m.enemy != kAiPlayer && c.m.deadflag == 0)
            {
                talk::playSentence(c, kTalkShot, 4000, kAttnNorm);
            }
            return true;
        }

        inline bool killed(AiCtx &c)
        {
            dropGun(c);
            talk::killed(c);
            return false;
        }

        inline void started(AiCtx &c)
        {
            c.m.talk.voicePitch = 100;
            // (His gun as a save from before left it.)
            if (c.me.body == kBodyDrawn)
                c.me.gun = 2;
            talk::started(c);
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Guard);
                k.health = 35;
                k.eye = fxFromInt(50);
                k.fov = fxDot(-0.7);
                k.capability = kCapHear | kCapTurnHead | kCapDoorsGroup;
                k.soundMask = kSoundWorld | kSoundCombat | kSoundCarcass | kSoundMeat | kSoundGarbage | kSoundDanger |
                              kSoundPlayer;
                k.blood = kBloodRed;
                k.getSchedule = getSchedule;
                k.scheduleOfType = scheduleOfType;
                k.startTask = talk::startTask;
                k.runTask = talk::runTask;
                k.animEvent = animEvent;
                k.checkRange1 = checkRange1;
                k.relationship = talk::relationship;
                k.prescheduleThink = talk::prescheduleThink;
                k.yawSpeed = yawSpeed;
                k.sound = sound;
                k.traceAttack = traceAttack;
                k.takeDamage = takeDamage;
                k.killed = killed;
                k.setActivity = talk::setActivity;
                k.started = started;
                k.frame = talk::frame;
                return k;
            }();
            return k;
        }
    } // namespace barney

    // ====================================================== sitting scientist

    namespace sitter
    {
        // His ways of sitting (m_baseSequence + ...).
        inline constexpr const char *kLookLeft = "sitlookleft", *kLookRight = "sitlookright", *kScared = "sitscared",
                                    *kSitting2 = "sitting2", *kSitting3 = "sitting3";

        inline void sit(AiCtx &c, const char *clip)
        {
            aiSetSequenceByName(c, clip);
            c.me.once = true;
        }

        /// To his left or his right, as `id` is (a greeting, a word).
        inline void lookAt(AiCtx &c, std::int16_t id)
        {
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, id, t))
                return;
            const fx dx = t.origin[0] - c.me.pos[0], dy = t.origin[1] - c.me.pos[1];
            const int yaw = static_cast<std::int16_t>(fxAtan2Turns(dy, dx) - c.me.yaw);
            sit(c, yaw > 0 ? kLookLeft : kLookRight);
        }

        /// His own FIdleSpeak: to a friend near, or to nobody.
        inline bool idleSpeak(AiCtx &c)
        {
            if (!talk::okToSpeak(c))
                return false;
            c.ai.talkWait = c.ai.now + aiRandomMs(c.a, 4800, 5200);
            const std::int16_t buddy = talk::findNearestFriend(c, false);
            if (buddy != kAiNone && aiRandomLong(c.a, 0, 1))
            {
                talk::setAnswerQuestion(c, buddy);
                talk::headTurnTo(c, buddy);
                talk::say(c, talk::lineOf(c, kTalkPQuestion), 127, kAttnIdle);
                c.ai.talkWait = c.ai.now + aiRandomMs(c.a, 4800, 5200);
                return true;
            }
            if (aiRandomLong(c.a, 0, 1))
            {
                talk::say(c, talk::lineOf(c, kTalkPIdle), 127, kAttnIdle);
                c.ai.talkWait = c.ai.now + aiRandomMs(c.a, 4800, 5200);
                return true;
            }
            c.ai.talkWait = 0;
            return false;
        }

        /// SittingThink, ten times a second.
        inline void think(AiCtx &c)
        {
            if (c.me.script >= 0)
                return;
            if (talk::idleHello(c))
            {
                const std::int16_t player = talk::findNearestFriend(c, true);
                if (player != kAiNone)
                {
                    lookAt(c, player);
                    c.me.aimYaw = 0;
                }
            }
            else if (c.m.seqFinished || c.me.finished)
            {
                const int i = aiRandomLong(c.a, 0, 99);
                int headTurn = 0;
                const char *clip = kSitting3;
                if (c.m.time1 != 0 && c.ai.now > c.m.time1)
                {
                    // Answering what he was asked.
                    talk::idleRespond(c);
                    clip = kScared;
                    c.m.time1 = 0;
                }
                else if (i < 30)
                {
                    const std::int16_t whom = !(c.m.talk.said & kSaidHelloPlayer) ? talk::findNearestFriend(c, true)
                                                                                    : talk::findNearestFriend(c, false);
                    if (!idleSpeak(c) || whom == kAiNone)
                    {
                        headTurn = aiRandomLong(c.a, 0, 8) * 10 - 40;
                    }
                    else
                    {
                        lookAt(c, whom);
                        clip = nullptr;
                    }
                }
                else if (i < 60)
                {
                    headTurn = aiRandomLong(c.a, 0, 8) * 10 - 40;
                    if (aiRandomLong(c.a, 0, 99) < 5)
                        idleSpeak(c);
                }
                else if (i < 80)
                {
                    clip = kSitting2;
                }
                else
                {
                    clip = kScared;
                }
                if (clip != nullptr)
                    sit(c, clip);
                c.me.frame = 0;
                c.me.aimYaw = headTurn * 65536 / 360;
            }
        }

        /// No damage at all: he was never made a monster.
        inline bool takeDamage(AiCtx &, AiDamage &) { return false; }

        inline void started(AiCtx &c)
        {
            static const std::uint8_t kPitch[4] = {105, 100, 95, 100};
            c.m.talk.voicePitch = kPitch[c.me.body % 4];
            c.m.state = c.m.idealState = kStateNone;
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Sitter);
                k.health = 50;
                k.eye = fxFromInt(36);
                k.fov = fxDot(-0.7);
                k.capability = kCapHear | kCapTurnHead;
                k.blood = kBloodRed;
                k.think = think;
                k.takeDamage = takeDamage;
                k.traceAttack = takeDamage;
                k.started = started;
                return k;
            }();
            return k;
        }
    } // namespace sitter

    namespace talk
    {
        inline void setAnswerQuestion(AiCtx &c, std::int16_t id)
        {
            AiMonster *m = nullptr;
            const AiKind *kind = nullptr;
            if (!friendCtx(c, id, m, kind))
                return;
            ActorState &them = c.a.states[id];
            if (them.kind == static_cast<std::uint8_t>(ActorKind::Sitter))
            {
                // (A moment, and then he answers: SittingThink.)
                m->time1 = c.ai.now + aiRandomMs(c.a, 3000, 4000);
                m->talk.target = static_cast<std::int16_t>(c.index);
                return;
            }
            if (them.script < 0)
            {
                AiCtx other{c.w, c.player, c.a, c.ai, id, them, *m, *kind};
                aiChangeSchedule(other, &slIdleResponse);
            }
            m->talk.target = static_cast<std::int16_t>(c.index);
        }
    } // namespace talk

    // ================================================================ g-man

    namespace gman
    {
        /// Waiting, he looks at whom he talks to, or at the player.
        inline bool runTask(AiCtx &c, const AiTask &task)
        {
            if (task.task == kTaskWait)
            {
                if (talk::isTalking(c) && c.m.talk.target != kAiNone)
                    talk::headTurnTo(c, c.m.talk.target);
                else if (c.player.health > 0 && c.me.clip == aiLookupActivity(c, kActIdle, true))
                    talk::headTurn(c, c.player.origin);
                else
                    talk::headReset(c);
            }
            else
            {
                talk::headReset(c);
            }
            return false;
        }

        /// He cannot be hurt: half his health, always, and a spark where it
        /// rang off him.
        inline bool takeDamage(AiCtx &c, AiDamage &d)
        {
            c.me.health = static_cast<std::int16_t>(c.m.maxHealth / 2);
            if (d.amount > 0)
                aiSet(c, kCondLightDamage);
            if (d.amount >= 20)
                aiSet(c, kCondHeavyDamage);
            detail::aiSay(c, aiRandomLong(c.a, 0, 1) ? CommonSound::Ricochet1 : CommonSound::Ricochet2);
            return false;
        }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Gman);
                k.health = 100;
                k.eye = fxFromInt(64);
                k.fov = fxDot(0.5);
                k.capability = kCapTurnHead;
                k.soundMask = 0;
                k.runTask = runTask;
                k.takeDamage = takeDamage;
                return k;
            }();
            return k;
        }
    } // namespace gman

    // ======================================================= monster_generic

    namespace generic
    {
        /// Spawnflag 4: not solid, and nothing hurts it.
        inline bool takeDamage(AiCtx &c, AiDamage &) { return (c.me.flags & kActorSolid) != 0; }

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Generic);
                k.health = 8;
                k.eye = fxFromInt(64);
                k.fov = fxDot(0.5);
                k.soundMask = 0;
                k.blood = kBloodRed;
                k.takeDamage = takeDamage;
                return k;
            }();
            return k;
        }
    } // namespace generic

    // ================================================= scripts and sentences

    namespace detail
    {
        /// The mind of actor `index`, with whoever the player is (a stand-in
        /// if the side table has not met them yet). False if it has none.
        inline bool aiCtxOf(LogicWorld &w, int index, AiMonster *&m, const AiKind *&kind, PlayerState *&player)
        {
            static PlayerState nobody;
            if (w.actors == nullptr || w.actors->ai == nullptr || index < 0 || index >= w.actors->count)
                return false;
            m = aiOf(*w.actors, index);
            kind = aiKindOf(w.actors->states[index].kind);
            player = w.actors->ai->player != nullptr ? w.actors->ai->player : &nobody;
            return m != nullptr && kind != nullptr;
        }

        inline bool actorCanPlaySequence(LogicWorld &w, int actor, bool overrideState, int interruptLevel)
        {
            const ActorState &a = w.actors->states[actor];
            if (!a.active || a.ai >= kAiDying || aiHeld(*w.actors, actor))
                return false;
            const AiMonster *m = aiFind(*w.actors, actor);
            if (m == nullptr || !m->started)
                return true; // (not thinking yet: as idle as it gets)
            if (m->deadflag != 0 || m->state == kStateProne)
                return false;
            if (overrideState)
                return true;
            if (m->state == kStateNone || m->state == kStateIdle || m->idealState == kStateIdle)
                return true;
            return m->state == kStateAlert && interruptLevel >= kInterruptByName;
        }

        inline void scriptRouse(LogicWorld &w, int actorIndex, bool ambush)
        {
            AiMonster *m = nullptr;
            const AiKind *kind = nullptr;
            PlayerState *player = nullptr;
            if (!aiCtxOf(w, actorIndex, m, kind, player) || !m->started)
                return;
            AiCtx c = aiCtx(w, *player, actorIndex, *m, *kind);
            m->scripted = false;
            aiScriptReleased(c);
            // (Hurt past its health while it could not be stopped: it dies
            // now.)
            if (c.me.health <= 0 && c.me.ai < kAiDying)
            {
                aiKilled(c, false, false);
                return;
            }
            if (ambush)
                aiChangeSchedule(c, &slAmbush);
        }

        inline bool sentenceSpeak(LogicWorld &w, PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            if (w.actors == nullptr || w.actors->ai == nullptr)
                return false;
            ActorWorld &a = *w.actors;
            const std::int16_t flags = record.maxs[0];
            // AcceptableSpeaker
            auto acceptable = [&](int i) -> bool
            {
                AiMonster *m = aiOf(a, i);
                const AiKind *kind = aiKindOf(a.states[i].kind);
                if (m == nullptr || kind == nullptr || !a.states[i].active)
                    return false;
                AiCtx c = aiCtx(w, player, i, *m, *kind);
                if (m->maxHealth == 0)
                    aiInit(c);
                if ((flags & kSentenceFollowers) && m->targetEnt != kAiPlayer)
                    return false;
                if (talk::isTalker(a.states[i].kind) && a.states[i].kind != static_cast<std::uint8_t>(ActorKind::Sitter))
                    return talk::canPlaySentence(c, (flags & kSentenceInterrupt) != 0);
                return aiAlive(a, i);
            };
            int speaker = -1;
            if (record.netName != 0)
            {
                for (int i = 0; i < a.count && speaker < 0; ++i)
                {
                    if (a.states[i].active && a.states[i].name == record.netName && acceptable(i))
                        speaker = i;
                }
            }
            if (speaker < 0 && record.damage != 0)
            {
                const fx radius = fxFromInt(record.waitMs);
                for (int i = 0; i < a.count && speaker < 0; ++i)
                {
                    const ActorState &actor = a.states[i];
                    if (!actor.active || actor.kind != record.damage - 1 || aiDist(actor.pos, record.move) > radius)
                        continue;
                    if (acceptable(i))
                        speaker = i;
                }
            }
            if (speaker < 0)
                return false;
            // To whom (UTIL_FindEntityGeneric): the player, or one by name.
            std::int16_t listener = kAiNone;
            if (record.mins[2] == 1)
                listener = kAiPlayer;
            else if (record.mins[1] != 0)
            {
                for (int i = 0; i < a.count && listener == kAiNone; ++i)
                {
                    if (a.states[i].active && a.states[i].name == static_cast<std::uint16_t>(record.mins[1]))
                        listener = static_cast<std::int16_t>(i);
                }
            }
            AiMonster *m = aiOf(a, speaker);
            AiCtx c = aiCtx(w, player, speaker, *m, *aiKindOf(a.states[speaker].kind));
            const int sentence = static_cast<int>(record.extra[0]);
            const int ms = static_cast<int>(record.extra[1]);
            const int volume = static_cast<int>(record.extra[2]);
            const int radius = record.mins[0];
            // (The game's flag, as shipped: without it the others are NOT
            // silenced.)
            const bool concurrent = !(flags & kSentenceConcurrent);
            if (talk::isTalker(a.states[speaker].kind))
            {
                talk::playScriptedSentence(c, sentence, ms, volume, radius, concurrent, listener);
            }
            else
            {
                // CBaseMonster's (and the G-Man's: he looks at whom he
                // talks to while he does).
                talk::say(c, sentence, volume, radius);
                c.m.talk.stopTalk = c.ai.now + ms;
                c.m.talk.target = listener;
            }
            logicUseTargets(w, player, index, kUseToggle, 0);
            return true;
        }
    } // namespace detail
} // namespace hldata
