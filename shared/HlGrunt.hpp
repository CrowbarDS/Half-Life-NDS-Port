#pragma once

// Soldiers: the game's hgrunt.cpp on its monster mind (HlAiCore.hpp) --
// CHGrunt's schedules, tasks, attacks and calls, as they shipped.
//
// A soldier goes about in a squad (CSquadMonster): by name, where the map
// names one and flags its leader, or with whoever of his own kind he can
// see within 1024 units when he starts (four at most). A squad has two
// places for those firing at once and two for a grenade; the rest find
// cover. He fights whatever he hates or dislikes -- the player, their
// friends, the aliens -- runs from the sound of a grenade about to go off,
// drops his gun when he dies, and calls out (Section::Speech's lines) when
// nobody else of his kind has just done so.
//
// What the game's does that this one does not yet: slide down a rope from
// an Osprey (monster_grunt_repel), call out at the sight of an alien (the
// level has no lines for it).
//
// Included at the end of HlMonsters.hpp. std-only and inline.

namespace hldata
{
    // The game's numbers. Those that go with how hard the game is are its
    // skill.cfg's (HlSkill.hpp: what he does uses the game's level); these
    // are them at MEDIUM, for what wants a number to measure by.
    inline constexpr int kGruntHealth = skillData(kSkillMedium).hgruntHealth;
    inline constexpr int kGruntLimp = 20;  ///< HGRUNT_LIMP_HEALTH
    inline constexpr int kGruntClip = 36;  ///< GRUNT_CLIP_SIZE
    inline constexpr int kGruntShells = 8; ///< a shotgun's
    inline constexpr int kGruntRound = skillData(kSkillMedium).monster9mmARBullet; ///< a round of his MP5
    inline constexpr int kGruntPellet = skillData(kSkillMedium).plrBuckshot; ///< each of his shotgun's pellets
    inline constexpr int kGruntPellets = skillData(kSkillMedium).hgruntPellets;
    inline constexpr int kGruntKick = skillData(kSkillMedium).hgruntKick;
    inline constexpr int kGruntGrenadeSpeed = skillData(kSkillMedium).hgruntGspeed;
    inline constexpr std::uint8_t kHitStomach = 3;
    inline constexpr std::uint8_t kHitHelmet = 11;

    // ActorState::mood: what of his the game keeps in CHGrunt's own fields.
    inline constexpr std::uint8_t kMoodStanding = 1; ///< m_fStanding: fires standing, not crouched
    inline constexpr std::uint8_t kMoodMet = 2;      ///< !m_fFirstEncounter: has signalled his squad once

    namespace grunt
    {
        /// How loud a grunt's lines are, of 127: the game's 0.35.
        inline constexpr int kGruntLineVolume = 44;

        inline constexpr int kReload = 2, kKick = 3, kBurst1 = 4, kBurst2 = 5, kBurst3 = 6, kGrenToss = 7,
                             kGrenLaunch = 8, kGrenDrop = 9, kCaughtEnemy = 10, kDropGun = 11;

        enum : std::uint8_t
        {
            kSchedSuppress = kSchedLastCommon + 1,
            kSchedEstablishLineOfFire,
            kSchedCoverAndReload,
            kSchedSweep,
            kSchedFoundEnemy,
            kSchedRepel,
            kSchedRepelAttack,
            kSchedRepelLand,
            kSchedWaitFaceEnemy,
            kSchedTakeCoverFailed,
            kSchedElofFail
        };
        enum : std::uint8_t
        {
            kTaskFaceTossDir = kTaskLastCommon + 1,
            kTaskSpeakSentence,
            kTaskCheckFire
        };
        inline constexpr std::uint32_t kCondNoFire = kCondSpecial1; ///< bits_COND_GRUNT_NOFIRE

        // What he is to say next (m_iSentence): a kSpeak* group, or none.
        inline constexpr int kSentenceNone = -1;

        // His own of the game's fields: m_flNextGrenadeCheck (time1),
        // m_flNextPainTime (time2), m_vecTossVelocity (vec1),
        // m_fThrowGrenade (flag1), m_iSentence (count1).

        inline constexpr AiTask tlFail[] = {
            {kTaskStopMoving, 0},
            {kTaskSetActivity, kActIdle},
            {kTaskWait, 2000},
            {kTaskWaitPvs, 0},
        };
        inline constexpr AiSchedule slFail = {
            tlFail, 4, kCondCanRangeAttack1 | kCondCanRangeAttack2 | kCondCanMeleeAttack1 | kCondCanMeleeAttack2, 0,
            "Grunt Fail"};

        inline constexpr AiTask tlCombatFail[] = {
            {kTaskStopMoving, 0},
            {kTaskSetActivity, kActIdle},
            {kTaskWaitFaceEnemy, 2000},
            {kTaskWaitPvs, 0},
        };
        inline constexpr AiSchedule slCombatFail = {tlCombatFail, 4, kCondCanRangeAttack1 | kCondCanRangeAttack2, 0,
                                                    "Grunt Combat Fail"};

        inline constexpr AiTask tlVictoryDance[] = {
            {kTaskStopMoving, 0},          {kTaskFaceEnemy, 0},       {kTaskWait, 1500},
            {kTaskGetPathToEnemyCorpse, 0}, {kTaskWalkPath, 0},       {kTaskWaitForMovement, 0},
            {kTaskFaceEnemy, 0},           {kTaskPlaySequence, kActVictoryDance},
        };
        inline constexpr AiSchedule slVictoryDance = {tlVictoryDance, 8,
                                                      kCondNewEnemy | kCondLightDamage | kCondHeavyDamage, 0,
                                                      "GruntVictoryDance"};

        inline constexpr AiTask tlEstablishLineOfFire[] = {
            {kTaskSetFailSchedule, kSchedElofFail},
            {kTaskGetPathToEnemy, 0},
            {kTaskSpeakSentence, 0},
            {kTaskRunPath, 0},
            {kTaskWaitForMovement, 0},
        };
        inline constexpr AiSchedule slEstablishLineOfFire = {
            tlEstablishLineOfFire, 5,
            kCondNewEnemy | kCondEnemyDead | kCondCanRangeAttack1 | kCondCanMeleeAttack1 | kCondCanRangeAttack2 |
                kCondCanMeleeAttack2 | kCondHearSound,
            kSoundDanger, "GruntEstablishLineOfFire"};

        inline constexpr AiTask tlFoundEnemy[] = {
            {kTaskStopMoving, 0},
            {kTaskFaceEnemy, 0},
            {kTaskPlaySequenceFaceEnemy, kActSignal1},
        };
        inline constexpr AiSchedule slFoundEnemy = {tlFoundEnemy, 3, kCondHearSound, kSoundDanger, "GruntFoundEnemy"};

        inline constexpr AiTask tlCombatFace1[] = {
            {kTaskStopMoving, 0},
            {kTaskSetActivity, kActIdle},
            {kTaskFaceEnemy, 0},
            {kTaskWait, 1500},
            {kTaskSetSchedule, kSchedSweep},
        };
        inline constexpr AiSchedule slCombatFace = {
            tlCombatFace1, 5, kCondNewEnemy | kCondEnemyDead | kCondCanRangeAttack1 | kCondCanRangeAttack2, 0,
            "Combat Face"};

        inline constexpr AiTask tlSignalSuppress[] = {
            {kTaskStopMoving, 0},   {kTaskFaceIdeal, 0},     {kTaskPlaySequenceFaceEnemy, kActSignal2},
            {kTaskFaceEnemy, 0},    {kTaskCheckFire, 0},     {kTaskRangeAttack1, 0},
            {kTaskFaceEnemy, 0},    {kTaskCheckFire, 0},     {kTaskRangeAttack1, 0},
            {kTaskFaceEnemy, 0},    {kTaskCheckFire, 0},     {kTaskRangeAttack1, 0},
            {kTaskFaceEnemy, 0},    {kTaskCheckFire, 0},     {kTaskRangeAttack1, 0},
            {kTaskFaceEnemy, 0},    {kTaskCheckFire, 0},     {kTaskRangeAttack1, 0},
        };
        inline constexpr AiSchedule slSignalSuppress = {
            tlSignalSuppress, 18,
            kCondEnemyDead | kCondLightDamage | kCondHeavyDamage | kCondHearSound | kCondNoFire | kCondNoAmmoLoaded,
            kSoundDanger, "SignalSuppress"};

        inline constexpr AiTask tlSuppress[] = {
            {kTaskStopMoving, 0}, {kTaskFaceEnemy, 0},  {kTaskCheckFire, 0}, {kTaskRangeAttack1, 0},
            {kTaskFaceEnemy, 0},  {kTaskCheckFire, 0},  {kTaskRangeAttack1, 0}, {kTaskFaceEnemy, 0},
            {kTaskCheckFire, 0},  {kTaskRangeAttack1, 0}, {kTaskFaceEnemy, 0}, {kTaskCheckFire, 0},
            {kTaskRangeAttack1, 0}, {kTaskFaceEnemy, 0}, {kTaskCheckFire, 0}, {kTaskRangeAttack1, 0},
        };
        inline constexpr AiSchedule slSuppress = {
            tlSuppress, 16,
            kCondEnemyDead | kCondLightDamage | kCondHeavyDamage | kCondHearSound | kCondNoFire | kCondNoAmmoLoaded,
            kSoundDanger, "Suppress"};

        inline constexpr AiTask tlWaitInCover[] = {
            {kTaskStopMoving, 0},
            {kTaskSetActivity, kActIdle},
            {kTaskWaitFaceEnemy, 1000},
        };
        inline constexpr AiSchedule slWaitInCover = {
            tlWaitInCover, 3,
            kCondNewEnemy | kCondHearSound | kCondCanRangeAttack1 | kCondCanRangeAttack2 | kCondCanMeleeAttack1 |
                kCondCanMeleeAttack2,
            kSoundDanger, "GruntWaitInCover"};

        inline constexpr AiTask tlTakeCover1[] = {
            {kTaskStopMoving, 0},
            {kTaskSetFailSchedule, kSchedTakeCoverFailed},
            {kTaskWait, 200},
            {kTaskFindCoverFromEnemy, 0},
            {kTaskSpeakSentence, 0},
            {kTaskRunPath, 0},
            {kTaskWaitForMovement, 0},
            {kTaskRemember, static_cast<std::int32_t>(kMemoryInCover)},
            {kTaskSetSchedule, kSchedWaitFaceEnemy},
        };
        inline constexpr AiSchedule slTakeCover = {tlTakeCover1, 9, 0, 0, "TakeCover"};

        inline constexpr AiTask tlGrenadeCover1[] = {
            {kTaskStopMoving, 0},
            {kTaskFindCoverFromEnemy, 99000}, // (99 seconds' wait, as the game's)
            {kTaskFindFarNodeCoverFromEnemy, 384},
            {kTaskPlaySequence, kActSpecialAttack1},
            {kTaskClearMoveWait, 0},
            {kTaskRunPath, 0},
            {kTaskWaitForMovement, 0},
            {kTaskSetSchedule, kSchedWaitFaceEnemy},
        };
        inline constexpr AiSchedule slGrenadeCover = {tlGrenadeCover1, 8, 0, 0, "GrenadeCover"};

        inline constexpr AiTask tlTossGrenadeCover1[] = {
            {kTaskFaceEnemy, 0},
            {kTaskRangeAttack2, 0},
            {kTaskSetSchedule, kSchedTakeCoverFromEnemy},
        };
        inline constexpr AiSchedule slTossGrenadeCover = {tlTossGrenadeCover1, 3, 0, 0, "TossGrenadeCover"};

        inline constexpr AiTask tlTakeCoverFromBestSound[] = {
            {kTaskSetFailSchedule, kSchedCower}, // (duck and cover, if there is nowhere to go)
            {kTaskStopMoving, 0},
            {kTaskFindCoverFromBestSound, 0},
            {kTaskRunPath, 0},
            {kTaskWaitForMovement, 0},
            {kTaskRemember, static_cast<std::int32_t>(kMemoryInCover)},
            {kTaskTurnLeft, 179},
        };
        inline constexpr AiSchedule slTakeCoverFromBestSound = {tlTakeCoverFromBestSound, 7, 0, 0,
                                                                "GruntTakeCoverFromBestSound"};

        inline constexpr AiTask tlHideReload[] = {
            {kTaskStopMoving, 0},
            {kTaskSetFailSchedule, kSchedReload},
            {kTaskFindCoverFromEnemy, 0},
            {kTaskRunPath, 0},
            {kTaskWaitForMovement, 0},
            {kTaskRemember, static_cast<std::int32_t>(kMemoryInCover)},
            {kTaskFaceEnemy, 0},
            {kTaskPlaySequence, kActReload},
        };
        inline constexpr AiSchedule slHideReload = {tlHideReload, 8, kCondHeavyDamage | kCondHearSound, kSoundDanger,
                                                    "GruntHideReload"};

        inline constexpr AiTask tlSweep[] = {
            {kTaskTurnLeft, 179},
            {kTaskWait, 1000},
            {kTaskTurnLeft, 179},
            {kTaskWait, 1000},
        };
        inline constexpr AiSchedule slSweep = {
            tlSweep, 4,
            kCondNewEnemy | kCondLightDamage | kCondHeavyDamage | kCondCanRangeAttack1 | kCondCanRangeAttack2 |
                kCondHearSound,
            kSoundWorld | kSoundDanger | kSoundPlayer, "Grunt Sweep"};

        inline constexpr AiTask tlRangeAttack1A[] = {
            {kTaskStopMoving, 0},   {kTaskPlaySequenceFaceEnemy, kActCrouch}, {kTaskCheckFire, 0},
            {kTaskRangeAttack1, 0}, {kTaskFaceEnemy, 0},                      {kTaskCheckFire, 0},
            {kTaskRangeAttack1, 0}, {kTaskFaceEnemy, 0},                      {kTaskCheckFire, 0},
            {kTaskRangeAttack1, 0}, {kTaskFaceEnemy, 0},                      {kTaskCheckFire, 0},
            {kTaskRangeAttack1, 0},
        };
        inline constexpr AiSchedule slRangeAttack1A = {
            tlRangeAttack1A, 13,
            kCondNewEnemy | kCondEnemyDead | kCondHeavyDamage | kCondEnemyOccluded | kCondHearSound | kCondNoFire |
                kCondNoAmmoLoaded,
            kSoundDanger, "Range Attack1A"};

        inline constexpr AiTask tlRangeAttack1B[] = {
            {kTaskStopMoving, 0},   {kTaskPlaySequenceFaceEnemy, kActIdleAngry}, {kTaskCheckFire, 0},
            {kTaskRangeAttack1, 0}, {kTaskFaceEnemy, 0},                         {kTaskCheckFire, 0},
            {kTaskRangeAttack1, 0}, {kTaskFaceEnemy, 0},                         {kTaskCheckFire, 0},
            {kTaskRangeAttack1, 0}, {kTaskFaceEnemy, 0},                         {kTaskCheckFire, 0},
            {kTaskRangeAttack1, 0},
        };
        inline constexpr AiSchedule slRangeAttack1B = {
            tlRangeAttack1B, 13,
            kCondNewEnemy | kCondEnemyDead | kCondHeavyDamage | kCondEnemyOccluded | kCondNoAmmoLoaded | kCondNoFire |
                kCondHearSound,
            kSoundDanger, "Range Attack1B"};

        inline constexpr AiTask tlRangeAttack2[] = {
            {kTaskStopMoving, 0},
            {kTaskFaceTossDir, 0},
            {kTaskPlaySequence, kActRangeAttack2},
            {kTaskSetSchedule, kSchedWaitFaceEnemy}, // (not to run straight after throwing)
        };
        inline constexpr AiSchedule slRangeAttack2 = {tlRangeAttack2, 4, 0, 0, "RangeAttack2"};

        // ------------------------------------------------------------ him

        inline bool has(const AiCtx &c, std::uint32_t weapons) { return (c.me.item & weapons) != 0; }
        inline int clipSize(const AiCtx &c) { return has(c, kGruntShotgun) ? kGruntShells : kGruntClip; }
        inline bool standing(const AiCtx &c) { return (c.me.mood & kMoodStanding) != 0; }

        /// GetGunPosition
        inline void gunPosition(const AiCtx &c, fx *at)
        {
            at[0] = c.me.pos[0];
            at[1] = c.me.pos[1];
            at[2] = c.me.pos[2] + fxFromInt(standing(c) ? 60 : 48);
        }

        /// FOkToSpeak: nobody of his kind has just spoken; and one who is
        /// gagged speaks only in a fight.
        inline bool okToSpeak(const AiCtx &c)
        {
            if (c.a.speakMs > 0)
                return false;
            if ((c.me.flags & kActorGag) && c.m.state != kStateCombat)
                return false;
            return true;
        }

        /// A line of `group`, and then nobody speaks for a second and a half
        /// or two (SENTENCEG_PlayRndSz, JustSpoke). At the game's 0.35 of
        /// full (HGRUNT_SENTENCE_VOLUME): heard, not over the fight.
        inline void say(AiCtx &c, int group)
        {
            ActorWorld &a = c.a;
            a.speakMs = 1500 + aiRandomLong(a, 0, 500);
            c.m.count1 = kSentenceNone;
            if (c.w.speech == nullptr || c.w.heard == nullptr || group < 0)
                return;
            const std::uint16_t *lines = c.w.speech + group * kSpeechLines;
            int count = 0;
            while (count < kSpeechLines && lines[count] != 0xFFFF)
                ++count;
            if (count != 0)
                c.w.heard->add(lines[aiRandomLong(a, 0, count - 1)], c.me.pos, kGruntLineVolume, kRadiusLarge, kSoundSentence);
        }

        /// SpeakSentence: what he was to say, if he may.
        inline void speakSentence(AiCtx &c)
        {
            if (c.m.count1 == kSentenceNone)
                return;
            if (okToSpeak(c))
                say(c, c.m.count1);
        }

        inline void sound(AiCtx &c, int which)
        {
            switch (which)
            {
            case kAiSoundIdle:
                // Idle talk: a question asked, and answered by the next.
                if (okToSpeak(c) && (c.a.asked != 0 || aiRandomLong(c.a, 0, 1)))
                {
                    if (c.a.asked == 0)
                    {
                        switch (aiRandomLong(c.a, 0, 2))
                        {
                        case 0: say(c, kSpeakCheck); c.a.asked = 1; break;
                        case 1: say(c, kSpeakQuestion); c.a.asked = 2; break;
                        default: say(c, kSpeakIdle); break;
                        }
                    }
                    else
                    {
                        say(c, c.a.asked == 1 ? kSpeakClear : kSpeakAnswer);
                        c.a.asked = 0;
                    }
                }
                break;
            case kAiSoundPain:
                // Five times in seven, and not more than once a second.
                if (c.ai.now > c.m.time2)
                {
                    const int which = aiRandomLong(c.a, 0, 6);
                    if (which <= 4)
                        detail::aiSay(c, which & 1 ? CommonSound::GruntPain2 : CommonSound::GruntPain1);
                    c.m.time2 = c.ai.now + 1000;
                }
                break;
            case kAiSoundDeath:
                detail::aiSay(c, aiRandomLong(c.a, 0, 1) ? CommonSound::GruntDie2 : CommonSound::GruntDie1);
                break;
            default:
                break;
            }
        }

        inline int yawSpeed(AiCtx &c)
        {
            switch (c.m.activity)
            {
            case kActIdle:
            case kActRun: return 150;
            case kActWalk: return 180;
            case kActRangeAttack1:
            case kActRangeAttack2:
            case kActMeleeAttack1:
            case kActMeleeAttack2: return 120;
            case kActTurnLeft:
            case kActTurnRight: return 180;
            case kActGlide:
            case kActFly: return 30;
            default: return 90;
            }
        }

        /// NoFriendlyFire: none of his squad in the way, between two walls
        /// either side of him a box and a half wide and him, towards his
        /// enemy.
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

        inline bool canCheckAttacks(AiCtx &c)
        {
            return !aiHas(c, kCondEnemyTooFar);
        }

        inline bool checkMelee1(AiCtx &c, fx dot, fx dist)
        {
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.enemy, t) || !t.monster)
                return false;
            return dist <= fxFromInt(64) && dot >= fxDot(0.7) && t.cls != kClassAlienBioweapon &&
                   t.cls != kClassPlayerBioweapon;
        }

        inline bool checkRange1(AiCtx &c, fx dot, fx dist)
        {
            if (aiHas(c, kCondEnemyOccluded) || dist > fxFromInt(2048) || dot < fxDot(0.5) || !noFriendlyFire(c))
                return false;
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.enemy, t))
                return false;
            if (!t.isPlayer && dist <= fxFromInt(64))
                return false; // (too close to something that is not the player: kick it)
            fx gun[3];
            gunPosition(c, gun);
            return lineClear(*c.w.world, gun, t.center);
        }

        /// VecCheckToss: up to the top of an arc as high as the ceiling
        /// midway lets it be, and down onto the spot, at `gravity` (the
        /// world's, adjusted); none if it cannot be done.
        inline bool vecCheckToss(AiCtx &c, const fx *from, fx *to, int gravityPercent, fx *velocity)
        {
            const int gravity = fxToInt(gravityOf(c.player)) * gravityPercent / 100;
            if (to[2] - from[2] > fxFromInt(500) || gravity <= 0)
                return false;
            // (Somewhere about the spot: not on their head.)
            fx forward[3], right[3];
            aiForward(c.me.yaw, forward);
            aiRight(c.me.yaw, right);
            const int across = aiRandomLong(c.a, -8, 8) + aiRandomLong(c.a, -16, 16);
            const int along = aiRandomLong(c.a, -8, 8) + aiRandomLong(c.a, -16, 16);
            for (int k = 0; k < 2; ++k)
                to[k] += right[k] * across + forward[k] * along;
            fx mid[3] = {(from[0] + to[0]) / 2, (from[1] + to[1]) / 2, (from[2] + to[2]) / 2};
            const fx above[3] = {mid[0], mid[1], mid[2] + fxFromInt(500)};
            const Shot up = traceShot(*c.w.world, mid, above);
            mid[2] += static_cast<fx>((static_cast<std::int64_t>(fxFromInt(500)) * up.fraction) >> kFractionShift) -
                      fxFromInt(15);
            if (mid[2] < from[2] || mid[2] < to[2])
                return false;
            // t = sqrt(d / (g / 2)), in ms.
            const std::int64_t t1 =
                isqrt64(static_cast<std::uint64_t>(fxToInt(mid[2] - from[2])) * 2000000u / static_cast<std::uint32_t>(gravity));
            const std::int64_t t2 =
                isqrt64(static_cast<std::uint64_t>(fxToInt(mid[2] - to[2])) * 2000000u / static_cast<std::uint32_t>(gravity));
            if (t1 < 100)
                return false;
            for (int k = 0; k < 2; ++k)
                velocity[k] = static_cast<fx>((static_cast<std::int64_t>(to[k] - from[k]) * 1000) / (t1 + t2));
            velocity[2] = static_cast<fx>((static_cast<std::int64_t>(fxFromInt(gravity)) * t1) / 1000);
            const fx apex[3] = {from[0] + static_cast<fx>((static_cast<std::int64_t>(velocity[0]) * t1) / 1000),
                                from[1] + static_cast<fx>((static_cast<std::int64_t>(velocity[1]) * t1) / 1000),
                                mid[2]};
            return lineClear(*c.w.world, from, apex) && lineClear(*c.w.world, to, apex);
        }

        /// VecCheckThrow: straight at the spot at `speed`, lifted for what
        /// the world's pull (adjusted) takes off it on the way.
        inline bool vecCheckThrow(AiCtx &c, const fx *from, const fx *to, int speed, int gravityPercent, fx *velocity)
        {
            const int gravity = fxToInt(gravityOf(c.player)) * gravityPercent / 100;
            const fx d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
            const std::int64_t ms = static_cast<std::int64_t>(fxToInt(fxLength(d))) * 1000 / speed;
            if (ms < 1)
                return false;
            for (int k = 0; k < 3; ++k)
                velocity[k] = static_cast<fx>((static_cast<std::int64_t>(d[k]) * 1000) / ms);
            velocity[2] += static_cast<fx>((static_cast<std::int64_t>(fxFromInt(gravity)) * ms) / 2000);
            const fx apex[3] = {from[0] + d[0] / 2, from[1] + d[1] / 2,
                                from[2] + d[2] / 2 +
                                    static_cast<fx>((static_cast<std::int64_t>(fxFromInt(gravity)) * ms * ms) / 8000000)};
            return lineClear(*c.w.world, from, apex) && lineClear(*c.w.world, to, apex);
        }

        inline bool checkRange2(AiCtx &c, fx, fx)
        {
            if (!has(c, kGruntGrenades | kGruntLauncher))
                return false;
            // (Not while on the move.)
            if (aiIsMoving(c))
            {
                c.m.flag1 = false;
                return false;
            }
            if (c.ai.now < c.m.time1)
                return c.m.flag1;
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.enemy, t))
                return false;
            // Not at something in the air above him.
            fx mins[3], maxs[3];
            targetBox(c.me, mins, maxs);
            if (!t.onGround && !(t.isPlayer && c.player.waterLevel != 0) && c.m.enemyLkp[2] > maxs[2])
            {
                c.m.flag1 = false;
                return false;
            }
            fx target[3];
            if (has(c, kGruntGrenades))
            {
                // At their feet, or where they were last seen.
                if (aiRandomLong(c.a, 0, 1))
                {
                    target[0] = t.origin[0];
                    target[1] = t.origin[1];
                    target[2] = t.isPlayer ? t.origin[2] - t.height / 2 : t.origin[2];
                }
                else
                {
                    for (int k = 0; k < 3; ++k)
                        target[k] = c.m.enemyLkp[k];
                }
            }
            else
            {
                // Its body where it was last seen, and ahead of it, if seen.
                for (int k = 0; k < 3; ++k)
                    target[k] = c.m.enemyLkp[k] + (t.center[k] - t.origin[k]);
                if (aiHas(c, kCondSeeEnemy))
                {
                    const fx d[3] = {target[0] - c.me.pos[0], target[1] - c.me.pos[1], target[2] - c.me.pos[2]};
                    const std::int64_t ms = static_cast<std::int64_t>(fxToInt(fxLength(d))) * 1000 / skillOf(c.w).hgruntGspeed;
                    for (int k = 0; k < 3; ++k)
                        target[k] += static_cast<fx>((static_cast<std::int64_t>(t.velocity[k]) * ms) / 1000);
                }
            }
            // Not onto his own squad, nor at his own feet.
            if (aiInSquad(c) && aiSquadMemberInRange(c, target, fxFromInt(256)))
            {
                c.m.time1 = c.ai.now + 1000;
                c.m.flag1 = false;
            }
            if (aiDist2D(target, c.me.pos) <= fxFromInt(256))
            {
                c.m.time1 = c.ai.now + 1000;
                c.m.flag1 = false;
                return false;
            }
            fx gun[3];
            gunPosition(c, gun);
            if (has(c, kGruntGrenades))
            {
                if (vecCheckToss(c, gun, target, 50, c.m.vec1))
                {
                    c.m.flag1 = true;
                    c.m.time1 = c.ai.now; // (the game's "1/3 second" is now)
                }
                else
                {
                    c.m.flag1 = false;
                    c.m.time1 = c.ai.now + 1000;
                }
            }
            else
            {
                if (vecCheckThrow(c, gun, target, skillOf(c.w).hgruntGspeed, 50, c.m.vec1))
                {
                    c.m.flag1 = true;
                    c.m.time1 = c.ai.now + 300;
                }
                else
                {
                    c.m.flag1 = false;
                    c.m.time1 = c.ai.now + 1000;
                }
            }
            return c.m.flag1;
        }

        inline void checkAmmo(AiCtx &c)
        {
            if (c.me.ammo <= 0)
                aiSet(c, kCondNoAmmoLoaded);
        }

        /// The helmet: a plain soldier's turns 20 of a bullet, a blade, a
        /// blast or a club -- all of it, with a ricochet, if that is all
        /// there was; and it is his head.
        inline bool traceAttack(AiCtx &c, AiDamage &d)
        {
            if (d.group != kHitHelmet)
                return true;
            if ((c.me.body & 3) == 0 && (d.type & (kDmgBullet | kDmgSlash | kDmgBlast | kDmgClub)))
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
            return true;
        }

        inline bool takeDamage(AiCtx &c, AiDamage &)
        {
            aiForgetMemory(c, kMemoryInCover);
            return true;
        }

        /// His gun (and a launcher's grenades) left where he fell -- the
        /// places the map kept for them where he first stood (the converter
        /// puts them there, not yet in the world).
        inline void dropGun(AiCtx &c)
        {
            if ((c.me.body / 4) % 3 == 2 || c.index >= c.a.recordCount || c.a.records == nullptr)
                return; // (none: dropped already)
            c.me.body = static_cast<std::uint8_t>((c.me.body & 3) + 2 * 4);
            const ActorRecord &mine = c.a.records[c.index];
            fx forward[3];
            aiForward(c.me.yaw, forward);
            int n = 0;
            for (int slot = 0; slot < c.a.recordCount && slot < c.a.count; ++slot)
            {
                const ActorRecord &record = c.a.records[slot];
                if (record.kind != static_cast<std::uint8_t>(ActorKind::Item) || !(record.flags & kActorDormant) ||
                    c.a.states[slot].active || record.origin[0] != mine.origin[0] ||
                    record.origin[1] != mine.origin[1] || record.origin[2] != mine.origin[2])
                    continue;
                actorSetup(c.a, slot, record, *c.w.world);
                ActorState &item = c.a.states[slot];
                item.flags &= static_cast<std::uint8_t>(~kActorDormant);
                ++n;
                item.pos[0] = c.me.pos[0] + forward[0] * (16 + 12 * n);
                item.pos[1] = c.me.pos[1] + forward[1] * (16 + 12 * n);
                item.pos[2] = c.me.pos[2] + fxFromInt(36);
                pointToFloor(*c.w.world, item.pos, fxFromInt(128));
                actorPlaceBox(c.a, slot);
            }
        }

        inline bool killed(AiCtx &c)
        {
            // (Torn apart, he still drops it: GibMonster.)
            if (c.me.health < -30)
                dropGun(c);
            return false;
        }

        /// One round of his MP5, or the shotgun's pellets: at his enemy,
        /// as loose as the gun is, each hurting whatever it meets first
        /// (FireBullets: the pellets that hit one thing are one blow).
        inline void fire(AiCtx &c)
        {
            if (c.m.enemy == kAiNone)
                return;
            fx gun[3], dir[3];
            gunPosition(c, gun);
            aiShootAtEnemy(c, gun, dir);
            const bool shotgun = has(c, kGruntShotgun);
            const int shots = shotgun ? skillOf(c.w).hgruntPellets : 1;
            // (VECTOR_CONE_10DEGREES and _15DEGREES: their sines, Q12.)
            const fx cone = shotgun ? 535 : 357;
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
            for (int n = 0; n < shots; ++n)
            {
                // (Two throws added, either way: more of them near the middle.)
                const int x = aiRandomLong(c.a, -2048, 2048) + aiRandomLong(c.a, -2048, 2048);
                const int y = aiRandomLong(c.a, -2048, 2048) + aiRandomLong(c.a, -2048, 2048);
                fx aim[3];
                for (int k = 0; k < 3; ++k)
                    aim[k] = dir[k] + static_cast<fx>((static_cast<std::int64_t>(right[k]) * x * cone +
                                                       static_cast<std::int64_t>(up[k]) * y * cone) >>
                                                      (2 * kFxShift));
                fx end[3];
                int group = kHitBody;
                const std::int16_t hit = aiTraceLine(c, gun, aim, 2048, end, &group);
                if (n == 0)
                    effectAdd(c.a, kEffectTracer, gun, end, 0, 60);
                if (hit == kAiNone)
                {
                    const fx far[3] = {gun[0] + aim[0] * 2048, gun[1] + aim[1] * 2048, gun[2] + aim[2] * 2048};
                    markShot(c.a, *c.w.world, gun, far, false);
                    continue;
                }
                // Where it lands on what it hits (a head is three times:
                // `group`), and its blood.
                if (hit >= 0)
                    bleed(c.w, hit, end, aim, false);
                aiDamageTarget(c, hit, shotgun ? skillOf(c.w).plrBuckshot : skillOf(c.w).monster9mmARBullet, kDmgBullet | kDmgNeverGib,
                               static_cast<std::uint8_t>(group));
            }
            // His arm aimed at it (SetBlending: the game's after the shot,
            // here before, so that the flash is where this aim has it), and
            // the flash at the muzzle, in his hands (attachment 0).
            aiSetBlending(c, dir);
            fx flash[3] = {gun[0] + dir[0] * 24, gun[1] + dir[1] * 24, gun[2] + dir[2] * 24};
            actorAttachment(c.a, c.me, 0, flash);
            effectAdd(c.a, kEffectMuzzle, flash, nullptr, 0, 50);
            // (A casing out to his right, from a little behind the muzzle.)
            const fx breech[3] = {gun[0] - dir[0] * 24, gun[1] - dir[1] * 24, gun[2] - dir[2] * 24};
            aiEjectBrass(c, shotgun ? kPieceShotShell : kPieceShell, breech);
            if (c.me.ammo > 0)
                --c.me.ammo;
        }

        inline void animEvent(AiCtx &c, int event)
        {
            switch (event)
            {
            case kDropGun:
                dropGun(c);
                break;
            case kReload:
                if (c.w.heard != nullptr)
                    c.w.heard->add(CommonSound::GruntReload, c.me.pos, 127, kRadiusMedium);
                c.me.ammo = static_cast<std::uint8_t>(clipSize(c));
                aiClear(c, kCondNoAmmoLoaded);
                break;
            case kGrenToss:
            {
                fx gun[3];
                gunPosition(c, gun);
                if (Projectile *shot = projectileAdd(c.a, kShotGrenade, gun, c.m.vec1, 3500))
                    (void)shot;
                c.m.flag1 = false;
                c.m.time1 = c.ai.now + 6000; // (six seconds before he even looks again)
                break;
            }
            case kGrenLaunch:
            {
                if (c.w.heard != nullptr)
                    c.w.heard->add(CommonSound::Launcher, c.me.pos, 100, kRadiusMedium);
                fx gun[3];
                gunPosition(c, gun);
                if (Projectile *shot = projectileAdd(c.a, kShotContact, gun, c.m.vec1, 10000))
                    shot->sprite = 1; // (a soldier's: it goes off at the player too)
                c.m.flag1 = false;
                // (On DIFFICULT, a while at random before he shoots again;
                // else six seconds before he even looks.)
                c.m.time1 = c.ai.now + (skillLevelOf(c.w) == kSkillHard ? aiRandomLong(c.a, 2000, 5000) : 6000);
                break;
            }
            case kGrenDrop:
            {
                fx forward[3], right[3];
                aiForward(c.me.yaw, forward);
                aiRight(c.me.yaw, right);
                const fx at[3] = {c.me.pos[0] + forward[0] * 17 - right[0] * 27,
                                  c.me.pos[1] + forward[1] * 17 - right[1] * 27, c.me.pos[2] + fxFromInt(6)};
                const fx still[3] = {0, 0, 0};
                projectileAdd(c.a, kShotGrenade, at, still, 3000);
                break;
            }
            case kBurst1:
                fire(c);
                if (c.w.heard != nullptr)
                {
                    if (has(c, kGruntMp5))
                        c.w.heard->add(aiRandomLong(c.a, 0, 1) ? CommonSound::GruntFire1 : CommonSound::GruntFire2,
                                       c.me.pos, 127, kRadiusLarge);
                    else
                        c.w.heard->add(CommonSound::Shotgun, c.me.pos, 127, kRadiusLarge);
                }
                aiSound(c.a, kSoundCombat, c.me.pos, 384, 300);
                break;
            case kBurst2:
            case kBurst3:
                fire(c);
                break;
            case kKick:
            {
                const std::int16_t hit = aiHullAttack(c, 70, 0, 0);
                if (hit != kAiNone)
                {
                    fx forward[3];
                    aiForward(c.me.yaw, forward);
                    detail::aiPunch(c, hit, fxFromInt(15), 0);
                    const fx shove[3] = {forward[0] * 100, forward[1] * 100, fxFromInt(50)};
                    detail::aiShove(c, hit, shove);
                    aiDamageTarget(c, hit, skillOf(c.w).hgruntKick, kDmgClub);
                }
                break;
            }
            case kCaughtEnemy:
                if (okToSpeak(c))
                    say(c, kSpeakAlert);
                break;
            default:
                break;
            }
        }

        /// SetActivity: his own clips for firing and throwing; a limp, hurt;
        /// angry, in a fight.
        inline void setActivity(AiCtx &c, std::uint8_t activity)
        {
            const ModelView *model = actorModel(c.a, c.me);
            int clip = -1;
            auto named = [&](const char *name) { return model != nullptr ? model->findClip(nameHash(name)) : -1; };
            switch (activity)
            {
            case kActRangeAttack1:
                if (has(c, kGruntMp5))
                    clip = named(standing(c) ? "standing_mp5" : "crouching_mp5");
                else
                    clip = named(standing(c) ? "standing_shotgun" : "crouching_shotgun");
                break;
            case kActRangeAttack2:
                clip = named(has(c, kGruntGrenades) ? "throwgrenade" : "launchgrenade");
                break;
            case kActRun:
                clip = aiLookupActivity(c, c.me.health <= kGruntLimp ? kActRunHurt : kActRun);
                break;
            case kActWalk:
                clip = aiLookupActivity(c, c.me.health <= kGruntLimp ? kActWalkHurt : kActWalk);
                break;
            case kActIdle:
                if (c.m.state == kStateCombat)
                    activity = kActIdleAngry;
                clip = aiLookupActivity(c, activity);
                break;
            default:
                clip = aiLookupActivity(c, activity);
                break;
            }
            c.m.activity = activity;
            c.m.idealActivity = activity;
            if (clip >= 0)
            {
                const bool loops = c.me.clip >= 0 && (model->clips[c.me.clip].flags & kClipLoops) != 0;
                aiPlayClip(c, clip, c.me.clip != clip || !loops);
                aiSetYawSpeed(c);
            }
        }

        inline bool startTask(AiCtx &c, const AiTask &task)
        {
            c.m.taskStatus = kTaskStatusRunning;
            switch (task.task)
            {
            case kTaskCheckFire:
                if (!noFriendlyFire(c))
                    aiSet(c, kCondNoFire);
                aiTaskComplete(c);
                return true;
            case kTaskSpeakSentence:
                speakSentence(c);
                aiTaskComplete(c);
                return true;
            case kTaskWalkPath:
            case kTaskRunPath:
                aiForgetMemory(c, kMemoryInCover);
                return false;
            case kTaskReload:
                c.m.idealActivity = kActReload;
                return true;
            case kTaskFaceTossDir:
                return true;
            default:
                return false;
            }
        }

        inline bool runTask(AiCtx &c, const AiTask &task)
        {
            if (task.task != kTaskFaceTossDir)
                return false;
            const fx toward[3] = {c.me.pos[0] + c.m.vec1[0] * 64, c.me.pos[1] + c.m.vec1[1] * 64, c.me.pos[2]};
            aiMakeIdealYaw(c, toward);
            aiChangeYaw(c);
            if (aiFacingIdeal(c))
                c.m.taskStatus = kTaskStatusComplete;
            return true;
        }

        /// PrescheduleThink: the squad keeps when any of it last saw its
        /// enemy; five seconds without, and the enemy has eluded it.
        inline void prescheduleThink(AiCtx &c)
        {
            if (!aiInSquad(c) || c.m.enemy == kAiNone)
                return;
            AiMonster *leader = aiSquadLeader(c);
            if (aiHas(c, kCondSeeEnemy))
                leader->lastEnemySightTime = c.ai.now;
            else if (c.ai.now - leader->lastEnemySightTime > 5000)
                leader->enemyEluded = true;
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type);

        inline const AiSchedule *getSchedule(AiCtx &c)
        {
            c.m.count1 = kSentenceNone;
            if (aiHas(c, kCondHearSound))
            {
                const AiSound *heard = aiBestSound(c);
                if (heard != nullptr && (heard->type & kSoundDanger))
                {
                    if (okToSpeak(c))
                        say(c, kSpeakGrenade);
                    return scheduleOfType(c, kSchedTakeCoverFromBestSound);
                }
            }
            if (c.m.state == kStateCombat)
            {
                if (aiHas(c, kCondEnemyDead))
                    return aiBaseGetSchedule(c);
                if (aiHas(c, kCondNewEnemy))
                {
                    if (aiInSquad(c))
                    {
                        aiSquadLeader(c)->enemyEluded = false;
                        if (!aiIsLeader(c))
                            return scheduleOfType(c, kSchedTakeCoverFromEnemy);
                        if (okToSpeak(c))
                        {
                            AiTarget t;
                            const bool known = aiTargetOf(c.w, c.player, c.m.enemy, t);
                            if (known && t.isPlayer)
                                say(c, kSpeakAlert);
                            else if (known && t.cls != kClassPlayerAlly && t.cls != kClassHumanPassive &&
                                     t.cls != kClassMachine)
                                say(c, -1); // (HG_MONST: the level has no such lines)
                        }
                        if (aiHas(c, kCondCanRangeAttack1))
                            return scheduleOfType(c, kSchedSuppress);
                        return scheduleOfType(c, kSchedEstablishLineOfFire);
                    }
                }
                else if (aiHas(c, kCondNoAmmoLoaded))
                {
                    return scheduleOfType(c, kSchedCoverAndReload);
                }
                else if (aiHas(c, kCondLightDamage))
                {
                    // Nine times in ten (and a bit), for cover; else a flinch.
                    if (aiRandomLong(c.a, 0, 99) <= 90 && c.m.enemy != kAiNone)
                    {
                        if (okToSpeak(c))
                            c.m.count1 = kSpeakCover;
                        return scheduleOfType(c, kSchedTakeCoverFromEnemy);
                    }
                    return scheduleOfType(c, kSchedSmallFlinch);
                }
                else if (aiHas(c, kCondCanMeleeAttack1))
                {
                    return scheduleOfType(c, kSchedMeleeAttack1);
                }
                else if (has(c, kGruntLauncher) && aiHas(c, kCondCanRangeAttack2) && aiOccupySlot(c, kSlotsGruntGrenade))
                {
                    return scheduleOfType(c, kSchedRangeAttack2);
                }
                else if (aiHas(c, kCondCanRangeAttack1))
                {
                    if (aiInSquad(c))
                    {
                        AiMonster *leader = aiSquadLeader(c);
                        if (leader->enemyEluded && !aiHas(c, kCondEnemyFacingMe))
                        {
                            leader->enemyEluded = false;
                            return scheduleOfType(c, kSchedFoundEnemy);
                        }
                    }
                    if (aiOccupySlot(c, kSlotsGruntEngage))
                        return scheduleOfType(c, kSchedRangeAttack1);
                    if (aiHas(c, kCondCanRangeAttack2) && aiOccupySlot(c, kSlotsGruntGrenade))
                        return scheduleOfType(c, kSchedRangeAttack2);
                    return scheduleOfType(c, kSchedTakeCoverFromEnemy);
                }
                else if (aiHas(c, kCondEnemyOccluded))
                {
                    if (aiHas(c, kCondCanRangeAttack2) && aiOccupySlot(c, kSlotsGruntGrenade))
                    {
                        if (okToSpeak(c))
                            say(c, kSpeakThrow);
                        return scheduleOfType(c, kSchedRangeAttack2);
                    }
                    if (aiOccupySlot(c, kSlotsGruntEngage))
                    {
                        if (okToSpeak(c))
                            c.m.count1 = kSpeakCharge;
                        return scheduleOfType(c, kSchedEstablishLineOfFire);
                    }
                    if (okToSpeak(c) && aiRandomLong(c.a, 0, 1))
                        say(c, kSpeakTaunt);
                    return scheduleOfType(c, kSchedStandoff);
                }
                if (aiHas(c, kCondSeeEnemy) && !aiHas(c, kCondCanRangeAttack1))
                    return scheduleOfType(c, kSchedEstablishLineOfFire);
            }
            return aiBaseGetSchedule(c);
        }

        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            switch (type)
            {
            case kSchedTakeCoverFromEnemy:
                if (aiInSquad(c))
                {
                    // (On DIFFICULT one of a squad who can tosses a grenade
                    // as he goes.)
                    if (skillLevelOf(c.w) == kSkillHard && aiHas(c, kCondCanRangeAttack2) &&
                        aiOccupySlot(c, kSlotsGruntGrenade))
                    {
                        if (okToSpeak(c))
                            say(c, kSpeakThrow);
                        return &slTossGrenadeCover;
                    }
                    return &slTakeCover;
                }
                return aiRandomLong(c.a, 0, 1) ? &slTakeCover : &slGrenadeCover;
            case kSchedTakeCoverFromBestSound:
                return &slTakeCoverFromBestSound;
            case kSchedTakeCoverFailed:
                if (aiHas(c, kCondCanRangeAttack1) && aiOccupySlot(c, kSlotsGruntEngage))
                    return scheduleOfType(c, kSchedRangeAttack1);
                return scheduleOfType(c, kSchedFail);
            case kSchedElofFail:
                return scheduleOfType(c, kSchedTakeCoverFromEnemy);
            case kSchedEstablishLineOfFire:
                return &slEstablishLineOfFire;
            case kSchedRangeAttack1:
                // One time in ten, standing or crouched is chosen afresh.
                if (aiRandomLong(c.a, 0, 9) == 0)
                    c.me.mood = static_cast<std::uint8_t>((c.me.mood & ~kMoodStanding) |
                                                          (aiRandomLong(c.a, 0, 1) ? kMoodStanding : 0));
                return standing(c) ? &slRangeAttack1B : &slRangeAttack1A;
            case kSchedRangeAttack2:
                return &slRangeAttack2;
            case kSchedCombatFace:
                return &slCombatFace;
            case kSchedWaitFaceEnemy:
                return &slWaitInCover;
            case kSchedSweep:
                return &slSweep;
            case kSchedCoverAndReload:
                return &slHideReload;
            case kSchedFoundEnemy:
                return &slFoundEnemy;
            case kSchedVictoryDance:
                if (aiInSquad(c) && !aiIsLeader(c))
                    return &slFail;
                return &slVictoryDance;
            case kSchedSuppress:
            {
                AiTarget t;
                if (aiTargetOf(c.w, c.player, c.m.enemy, t) && t.isPlayer && !(c.me.mood & kMoodMet))
                {
                    // (The first time: the leader's signal to his squad.)
                    c.me.mood |= kMoodMet;
                    return &slSignalSuppress;
                }
                return &slSuppress;
            }
            case kSchedFail:
                return c.m.enemy != kAiNone ? &slCombatFail : &slFail;
            default:
                return aiSquadScheduleOfType(c, type);
            }
        }

        /// StartMonster's last word: a squad's leader wears the commander's
        /// head, and is fair.
        inline void started(AiCtx &c)
        {
            if (aiIsLeader(c))
            {
                c.me.body = static_cast<std::uint8_t>((c.me.body & ~3) | 1);
                c.me.skin = 0;
            }
            if (c.me.ammo == 0)
                c.me.ammo = static_cast<std::uint8_t>(clipSize(c));
            c.m.time1 = c.ai.now + 1000; // m_flNextGrenadeCheck
            c.m.count1 = kSentenceNone;
        }

        inline constexpr AiClipName kClips[] = {
            {kActIdle, "idle1"},        {kActIdleAngry, "combatidle"}, {kActRun, "run"},
            {kActRunHurt, "limpingrun"}, {kActSmallFlinch, "smflinch"}, {kActMeleeAttack1, "frontkick"},
            {kActDieSimple, "diesimple"}, {kActDieBackward, "dieback1"}, {kActDieForward, "dieforward"},
            {kActDieHeadshot, "dieheadshot"}, {kActDieGutshot, "diegutshot"}, {kActCower, "cower"},
            {kActVictoryDance, "victorydance"}, {kActSignal1, "advance_signal"}, {kActSignal2, "flank_signal"},
            {kActSpecialAttack1, "drop_grenade"}, {kActReload, "reload_mp5"}, {kActCrouch, "crouching_idle"},
            {kActReset, nullptr},
        };
        inline constexpr AiClipEvent kEvents[] = {
            {"standing_mp5", 0, kBurst1},   {"standing_mp5", 2, kBurst2},   {"standing_mp5", 4, kBurst3},
            {"crouching_mp5", 0, kBurst1},  {"crouching_mp5", 2, kBurst2},  {"crouching_mp5", 4, kBurst3},
            {"standing_shotgun", 0, kBurst1}, {"crouching_shotgun", 0, kBurst1}, {"reload_mp5", 47, kReload},
            {"reload_shotgun", 47, kReload}, {"throwgrenade", 36, kGrenToss}, {"launchgrenade", 16, kGrenLaunch},
            {"drop_grenade", 22, kGrenDrop}, {"frontkick", 14, kKick},     {"advance_signal", 1, kCaughtEnemy},
            {"diesimple", 3, kDropGun},     {"dieback1", 5, kDropGun},      {"diebackwards", 6, kDropGun},
            {"dieforward", 3, kDropGun},    {"dieheadshot", 1, kDropGun},   {"diegutshot", 0, kDropGun},
            {nullptr, 0, 0}};

        inline const AiKind &kind()
        {
            static const AiKind k = []
            {
                AiKind k;
                k.kind = static_cast<std::uint8_t>(ActorKind::Grunt);
                k.health = kGruntHealth;
                k.eye = fxFromInt(73);
                k.fov = fxDot(0.2);
                k.capability = kCapSquad | kCapTurnHead | kCapDoorsGroup;
                k.soundMask = kSoundWorld | kSoundCombat | kSoundPlayer | kSoundDanger;
                k.squadMonster = true;
                k.sound = sound;
                k.yawSpeed = yawSpeed;
                k.canCheckAttacks = canCheckAttacks;
                k.checkMelee1 = checkMelee1;
                k.checkRange1 = checkRange1;
                k.checkRange2 = checkRange2;
                k.checkAmmo = checkAmmo;
                k.traceAttack = traceAttack;
                k.takeDamage = takeDamage;
                k.killed = killed;
                k.animEvent = animEvent;
                k.setActivity = setActivity;
                k.startTask = startTask;
                k.runTask = runTask;
                k.prescheduleThink = prescheduleThink;
                k.getSchedule = getSchedule;
                k.scheduleOfType = scheduleOfType;
                k.started = started;
                k.clipNames = kClips;
                k.clipEvents = kEvents;
                return k;
            }();
            return k;
        }
    } // namespace grunt
} // namespace hldata
