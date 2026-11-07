#pragma once

// The game's monster mind, rebuilt from its own code (dlls/monsters.cpp,
// monsterstate.cpp, schedule.cpp, defaultai.cpp, combat.cpp,
// squadmonster.cpp), with its names, so that the two can be read side by side.
//
// A monster is in a STATE -- idle, alert, in combat, dead -- and runs a
// SCHEDULE: a list of TASKS (stop, face the enemy, attack; or find a path,
// run it, wait for the movement to end). Ten times a second it looks and
// listens (Look, Listen), which sets CONDITIONS (sees an enemy, hears a
// sound, can make a melee attack...); a condition in the schedule's
// interrupt mask, or the schedule's end, or a change of state, has it choose
// another (GetSchedule). What each kind does differently is its own
// functions on top of these (AiKind), as the game's are its classes'.
//
// What it thinks lives in a side table (AiWorld, reached by a pointer in
// ActorWorld): not saved. A monster in a loaded game starts from its saved
// health, place and state (ActorState::ai), and chooses afresh.
//
// Moving is the port's own (HlWalk.hpp, walkTick, from node to node); the
// tasks that make a route say where to, and the walking is done each frame.
// Clips are chosen by what they are for (the model's activities, as the
// game's LookupActivity does); a model cooked before they were kept falls
// back on the names each kind gives.
//
// std-only and inline: the host tests run it. Included at the end of
// HlMonsters.hpp.

#include <cstdint>

#include "HlPlayerMove.hpp"
#include "HlRelation.hpp"
#include "HlSoundEnt.hpp"

namespace hldata
{
    // ----------------------------------------------------------- the numbers

    // MONSTERSTATE_*
    inline constexpr std::uint8_t kStateNone = 0;
    inline constexpr std::uint8_t kStateIdle = 1;
    inline constexpr std::uint8_t kStateCombat = 2;
    inline constexpr std::uint8_t kStateAlert = 3;
    inline constexpr std::uint8_t kStateHunt = 4;
    inline constexpr std::uint8_t kStateProne = 5;
    inline constexpr std::uint8_t kStateScript = 6;
    inline constexpr std::uint8_t kStatePlayDead = 7;
    inline constexpr std::uint8_t kStateDead = 8;

    // bits_COND_*
    inline constexpr std::uint32_t kCondNoAmmoLoaded = 1u << 0;
    inline constexpr std::uint32_t kCondSeeHate = 1u << 1;
    inline constexpr std::uint32_t kCondSeeFear = 1u << 2;
    inline constexpr std::uint32_t kCondSeeDislike = 1u << 3;
    inline constexpr std::uint32_t kCondSeeEnemy = 1u << 4;
    inline constexpr std::uint32_t kCondEnemyOccluded = 1u << 5;
    inline constexpr std::uint32_t kCondSmellFood = 1u << 6;
    inline constexpr std::uint32_t kCondEnemyTooFar = 1u << 7;
    inline constexpr std::uint32_t kCondLightDamage = 1u << 8;
    inline constexpr std::uint32_t kCondHeavyDamage = 1u << 9;
    inline constexpr std::uint32_t kCondCanRangeAttack1 = 1u << 10;
    inline constexpr std::uint32_t kCondCanMeleeAttack1 = 1u << 11;
    inline constexpr std::uint32_t kCondCanRangeAttack2 = 1u << 12;
    inline constexpr std::uint32_t kCondCanMeleeAttack2 = 1u << 13;
    inline constexpr std::uint32_t kCondProvoked = 1u << 15;
    inline constexpr std::uint32_t kCondNewEnemy = 1u << 16;
    inline constexpr std::uint32_t kCondHearSound = 1u << 17;
    inline constexpr std::uint32_t kCondSmell = 1u << 18;
    inline constexpr std::uint32_t kCondEnemyFacingMe = 1u << 19;
    inline constexpr std::uint32_t kCondEnemyDead = 1u << 20;
    inline constexpr std::uint32_t kCondSeeClient = 1u << 21;
    inline constexpr std::uint32_t kCondSeeNemesis = 1u << 22;
    inline constexpr std::uint32_t kCondSpecial1 = 1u << 28;
    inline constexpr std::uint32_t kCondSpecial2 = 1u << 29;
    inline constexpr std::uint32_t kCondTaskFailed = 1u << 30;
    inline constexpr std::uint32_t kCondScheduleDone = 1u << 31;
    inline constexpr std::uint32_t kCondCanAttack =
        kCondCanRangeAttack1 | kCondCanMeleeAttack1 | kCondCanRangeAttack2 | kCondCanMeleeAttack2;

    // bits_MEMORY_*
    inline constexpr std::uint32_t kMemoryProvoked = 1u << 0;
    inline constexpr std::uint32_t kMemoryInCover = 1u << 1;
    inline constexpr std::uint32_t kMemorySuspicious = 1u << 2;
    inline constexpr std::uint32_t kMemoryPathFinished = 1u << 3;
    inline constexpr std::uint32_t kMemoryOnPath = 1u << 4;
    inline constexpr std::uint32_t kMemoryMoveFailed = 1u << 5;
    inline constexpr std::uint32_t kMemoryFlinched = 1u << 6;
    inline constexpr std::uint32_t kMemoryKilled = 1u << 7;
    inline constexpr std::uint32_t kMemoryCustom4 = 1u << 28;
    inline constexpr std::uint32_t kMemoryCustom3 = 1u << 29;
    inline constexpr std::uint32_t kMemoryCustom2 = 1u << 30;
    inline constexpr std::uint32_t kMemoryCustom1 = 1u << 31;

    // bits_CAP_*
    inline constexpr std::uint32_t kCapDuck = 1u << 0;
    inline constexpr std::uint32_t kCapJump = 1u << 1;
    inline constexpr std::uint32_t kCapStrafe = 1u << 2;
    inline constexpr std::uint32_t kCapSquad = 1u << 3;
    inline constexpr std::uint32_t kCapSwim = 1u << 4;
    inline constexpr std::uint32_t kCapClimb = 1u << 5;
    inline constexpr std::uint32_t kCapUse = 1u << 6;
    inline constexpr std::uint32_t kCapHear = 1u << 7;
    inline constexpr std::uint32_t kCapAutoDoors = 1u << 8;
    inline constexpr std::uint32_t kCapOpenDoors = 1u << 9;
    inline constexpr std::uint32_t kCapTurnHead = 1u << 10;
    inline constexpr std::uint32_t kCapRangeAttack1 = 1u << 11;
    inline constexpr std::uint32_t kCapRangeAttack2 = 1u << 12;
    inline constexpr std::uint32_t kCapMeleeAttack1 = 1u << 13;
    inline constexpr std::uint32_t kCapMeleeAttack2 = 1u << 14;
    inline constexpr std::uint32_t kCapFly = 1u << 15;
    inline constexpr std::uint32_t kCapDoorsGroup = kCapUse | kCapAutoDoors | kCapOpenDoors;

    // (DMG_*, the kinds of hurt: HlPlayerMove.hpp, which the player's own
    // hurt needs too.)

    // Activities (dlls/activity.h): what a clip is for. The numbers are the
    // game's, as the models have them.
    inline constexpr std::uint8_t kActReset = 0;
    inline constexpr std::uint8_t kActIdle = 1;
    inline constexpr std::uint8_t kActGuard = 2;
    inline constexpr std::uint8_t kActWalk = 3;
    inline constexpr std::uint8_t kActRun = 4;
    inline constexpr std::uint8_t kActFly = 5;
    inline constexpr std::uint8_t kActSwim = 6;
    inline constexpr std::uint8_t kActHop = 7;
    inline constexpr std::uint8_t kActLeap = 8;
    inline constexpr std::uint8_t kActFall = 9;
    inline constexpr std::uint8_t kActLand = 10;
    inline constexpr std::uint8_t kActStrafeLeft = 11;
    inline constexpr std::uint8_t kActStrafeRight = 12;
    inline constexpr std::uint8_t kActTurnLeft = 15;
    inline constexpr std::uint8_t kActTurnRight = 16;
    inline constexpr std::uint8_t kActCrouch = 17;
    inline constexpr std::uint8_t kActThreatDisplay = 57;
    inline constexpr std::uint8_t kActCrouchIdle = 18;
    inline constexpr std::uint8_t kActStand = 19;
    inline constexpr std::uint8_t kActSignal1 = 21;
    inline constexpr std::uint8_t kActSignal2 = 22;
    inline constexpr std::uint8_t kActSignal3 = 23;
    inline constexpr std::uint8_t kActCower = 25;
    inline constexpr std::uint8_t kActSmallFlinch = 26;
    inline constexpr std::uint8_t kActBigFlinch = 27;
    inline constexpr std::uint8_t kActRangeAttack1 = 28;
    inline constexpr std::uint8_t kActRangeAttack2 = 29;
    inline constexpr std::uint8_t kActMeleeAttack1 = 30;
    inline constexpr std::uint8_t kActMeleeAttack2 = 31;
    inline constexpr std::uint8_t kActReload = 32;
    inline constexpr std::uint8_t kActArm = 33;
    inline constexpr std::uint8_t kActEat = 35;
    inline constexpr std::uint8_t kActDieSimple = 36;
    inline constexpr std::uint8_t kActDieBackward = 37;
    inline constexpr std::uint8_t kActDieForward = 38;
    inline constexpr std::uint8_t kActBarnacleHit = 40;
    inline constexpr std::uint8_t kActBarnaclePull = 41;
    inline constexpr std::uint8_t kActBarnacleChomp = 42;
    inline constexpr std::uint8_t kActBarnacleChew = 43;
    inline constexpr std::uint8_t kActInspectFloor = 45;
    inline constexpr std::uint8_t kActInspectWall = 46;
    inline constexpr std::uint8_t kActIdleAngry = 47;
    inline constexpr std::uint8_t kActWalkHurt = 48;
    inline constexpr std::uint8_t kActRunHurt = 49;
    inline constexpr std::uint8_t kActHover = 50;
    inline constexpr std::uint8_t kActGlide = 51;
    inline constexpr std::uint8_t kActDetectScent = 54;
    inline constexpr std::uint8_t kActDisarm = 34;
    inline constexpr std::uint8_t kActFearDisplay = 58;
    inline constexpr std::uint8_t kActExcited = 59;
    inline constexpr std::uint8_t kActWalkScared = 63;
    inline constexpr std::uint8_t kActRunScared = 64;
    inline constexpr std::uint8_t kActSpecialAttack1 = 60;
    inline constexpr std::uint8_t kActSpecialAttack2 = 61;
    inline constexpr std::uint8_t kActVictoryDance = 65;
    inline constexpr std::uint8_t kActDieHeadshot = 66;
    inline constexpr std::uint8_t kActDieChestshot = 67;
    inline constexpr std::uint8_t kActDieGutshot = 68;
    inline constexpr std::uint8_t kActDieBackshot = 69;
    inline constexpr std::uint8_t kActFlinchHead = 70;
    inline constexpr std::uint8_t kActFlinchChest = 71;
    inline constexpr std::uint8_t kActFlinchStomach = 72;
    inline constexpr std::uint8_t kActFlinchLeftArm = 73;
    inline constexpr std::uint8_t kActFlinchRightArm = 74;
    inline constexpr std::uint8_t kActFlinchLeftLeg = 75;
    inline constexpr std::uint8_t kActFlinchRightLeg = 76;

    // The shared tasks (SHARED_TASKS), in the game's order. A kind's own go
    // on from kTaskLastCommon.
    enum : std::uint8_t
    {
        kTaskInvalid = 0,
        kTaskWait,
        kTaskWaitFaceEnemy,
        kTaskWaitPvs,
        kTaskSuggestState,
        kTaskWalkToTarget,
        kTaskRunToTarget,
        kTaskMoveToTargetRange,
        kTaskGetPathToEnemy,
        kTaskGetPathToEnemyLkp,
        kTaskGetPathToEnemyCorpse,
        kTaskGetPathToLeader,
        kTaskGetPathToSpot,
        kTaskGetPathToTarget,
        kTaskGetPathToHintNode,
        kTaskGetPathToLastPosition,
        kTaskGetPathToBestSound,
        kTaskGetPathToBestScent,
        kTaskRunPath,
        kTaskWalkPath,
        kTaskStrafePath,
        kTaskClearMoveWait,
        kTaskStoreLastPosition,
        kTaskClearLastPosition,
        kTaskPlayActiveIdle,
        kTaskFindHintNode,
        kTaskClearHintNode,
        kTaskSmallFlinch,
        kTaskFaceIdeal,
        kTaskFaceRoute,
        kTaskFaceEnemy,
        kTaskFaceHintNode,
        kTaskFaceTarget,
        kTaskFaceLastPosition,
        kTaskRangeAttack1,
        kTaskRangeAttack2,
        kTaskMeleeAttack1,
        kTaskMeleeAttack2,
        kTaskReload,
        kTaskRangeAttack1NoTurn,
        kTaskRangeAttack2NoTurn,
        kTaskMeleeAttack1NoTurn,
        kTaskMeleeAttack2NoTurn,
        kTaskReloadNoTurn,
        kTaskSpecialAttack1,
        kTaskSpecialAttack2,
        kTaskCrouch,
        kTaskStand,
        kTaskGuard,
        kTaskStepLeft,
        kTaskStepRight,
        kTaskStepForward,
        kTaskStepBack,
        kTaskDodgeLeft,
        kTaskDodgeRight,
        kTaskSoundAngry,
        kTaskSoundDeath,
        kTaskSetActivity,
        kTaskSetSchedule,
        kTaskSetFailSchedule,
        kTaskClearFailSchedule,
        kTaskPlaySequence,
        kTaskPlaySequenceFaceEnemy,
        kTaskPlaySequenceFaceTarget,
        kTaskSoundIdle,
        kTaskSoundWake,
        kTaskSoundPain,
        kTaskSoundDie,
        kTaskFindCoverFromBestSound,
        kTaskFindCoverFromEnemy,
        kTaskFindLateralCoverFromEnemy,
        kTaskFindNodeCoverFromEnemy,
        kTaskFindNearNodeCoverFromEnemy,
        kTaskFindFarNodeCoverFromEnemy,
        kTaskFindCoverFromOrigin,
        kTaskEat,
        kTaskDie,
        kTaskWaitForScript,
        kTaskPlayScript,
        kTaskEnableScript,
        kTaskPlantOnScript,
        kTaskFaceScript,
        kTaskWaitRandom,
        kTaskWaitIndefinite,
        kTaskStopMoving,
        kTaskTurnLeft,
        kTaskTurnRight,
        kTaskRemember,
        kTaskForget,
        kTaskWaitForMovement,
        kTaskLastCommon
    };

    // The schedule types (SCHEDULE_TYPE), in the game's order. A kind's own
    // go on from kSchedLastCommon.
    enum : std::uint8_t
    {
        kSchedNone = 0,
        kSchedIdleStand,
        kSchedIdleWalk,
        kSchedWakeAngry,
        kSchedWakeCalled,
        kSchedAlertFace,
        kSchedAlertSmallFlinch,
        kSchedAlertBigFlinch,
        kSchedAlertStand,
        kSchedInvestigateSound,
        kSchedCombatFace,
        kSchedCombatStand,
        kSchedChaseEnemy,
        kSchedChaseEnemyFailed,
        kSchedVictoryDance,
        kSchedTargetFace,
        kSchedTargetChase,
        kSchedSmallFlinch,
        kSchedTakeCoverFromEnemy,
        kSchedTakeCoverFromBestSound,
        kSchedTakeCoverFromOrigin,
        kSchedCower,
        kSchedMeleeAttack1,
        kSchedMeleeAttack2,
        kSchedRangeAttack1,
        kSchedRangeAttack2,
        kSchedSpecialAttack1,
        kSchedSpecialAttack2,
        kSchedStandoff,
        kSchedArmWeapon,
        kSchedReload,
        kSchedGuard,
        kSchedAmbush,
        kSchedDie,
        kSchedWaitTrigger,
        kSchedFollow,
        kSchedSleep,
        kSchedWake,
        kSchedBarnacleVictimGrab,
        kSchedBarnacleVictimChomp,
        kSchedAiScript,
        kSchedFail,
        kSchedLastCommon
    };

    // TASKSTATUS_*
    inline constexpr std::uint8_t kTaskStatusNew = 0;
    inline constexpr std::uint8_t kTaskStatusRunning = 1;
    inline constexpr std::uint8_t kTaskStatusRunningMovement = 2;
    inline constexpr std::uint8_t kTaskStatusRunningTask = 3;
    inline constexpr std::uint8_t kTaskStatusComplete = 4;

    // Where a monster is going (MOVEGOAL_*).
    inline constexpr std::uint8_t kMoveGoalNone = 0;
    inline constexpr std::uint8_t kMoveGoalEnemy = 1;
    inline constexpr std::uint8_t kMoveGoalLocation = 2;
    inline constexpr std::uint8_t kMoveGoalTarget = 3; ///< MOVEGOAL_TARGETENT: after m_hTargetEnt

    // A squad's slots (squadmonster.h bits_SLOT_*).
    inline constexpr std::uint16_t kSlotNone = 0;
    inline constexpr std::uint16_t kSlotGruntEngage1 = 1u << 0;
    inline constexpr std::uint16_t kSlotGruntEngage2 = 1u << 1;
    inline constexpr std::uint16_t kSlotsGruntEngage = kSlotGruntEngage1 | kSlotGruntEngage2;
    inline constexpr std::uint16_t kSlotGruntGrenade1 = 1u << 2;
    inline constexpr std::uint16_t kSlotGruntGrenade2 = 1u << 3;
    inline constexpr std::uint16_t kSlotsGruntGrenade = kSlotGruntGrenade1 | kSlotGruntGrenade2;
    inline constexpr std::uint16_t kSlotHoundAttack1 = 1u << 7;
    inline constexpr std::uint16_t kSlotHoundAttack2 = 1u << 8;
    inline constexpr std::uint16_t kSlotHoundAttack3 = 1u << 9;
    inline constexpr std::uint16_t kSlotsHoundAttack = kSlotHoundAttack1 | kSlotHoundAttack2 | kSlotHoundAttack3;
    inline constexpr std::uint16_t kSlotAgruntHornet1 = 1u << 4;
    inline constexpr std::uint16_t kSlotAgruntHornet2 = 1u << 5;
    inline constexpr std::uint16_t kSlotsAgruntHornet = kSlotAgruntHornet1 | kSlotAgruntHornet2;
    inline constexpr std::uint16_t kSlotAgruntChase = 1u << 6;
    inline constexpr std::uint16_t kSlotSquadSplit = 1u << 10;
    inline constexpr int kSquadSlotCount = 11;
    inline constexpr int kMaxSquadMembers = 5;

    // The hit groups (HITGROUP_*), as monsterHurt is given them.
    inline constexpr std::uint8_t kHitGeneric = 0;

    // What a kind says (its IdleSound, AlertSound, PainSound, DeathSound).
    inline constexpr int kAiSoundIdle = 0;
    inline constexpr int kAiSoundAlert = 1;
    inline constexpr int kAiSoundPain = 2;
    inline constexpr int kAiSoundDeath = 3;

    /// Who: an actor's index, or the player, or nobody.
    inline constexpr std::int16_t kAiNone = -1;
    inline constexpr std::int16_t kAiPlayer = -2;

    /// A Q12 dot product, as the game's floats.
    inline constexpr fx fxDot(double value) { return static_cast<fx>(value * kFxOne + (value < 0 ? -0.5 : 0.5)); }

    inline constexpr int kAiThinkMs = 100; ///< pev->nextthink = time + 0.1
    inline constexpr int kAiOldEnemies = 4;  ///< MAX_OLD_ENEMIES
    inline constexpr int kAiSeen = 8;        ///< how many it can have in sight at once (m_pLink)
    inline constexpr int kAiAudible = 12;

    // ------------------------------------------------------------- the tables

    struct AiTask
    {
        std::uint8_t task;
        std::int32_t data; ///< the game's flData: an activity, a schedule, a memory bit -- or a time, in ms
    };

    struct AiSchedule
    {
        const AiTask *tasks;
        std::uint8_t count;
        std::uint32_t interruptMask;
        std::uint8_t soundMask;
        const char *name;
    };

    /// A blow (TakeDamage's arguments).
    struct AiDamage
    {
        int amount = 0;
        std::uint32_t type = kDmgGeneric;
        std::int16_t attacker = kAiNone; ///< who did it: an actor, the player, or nothing (the world)
        bool hasFrom = false;            ///< where it came from (the inflictor's middle)
        fx from[3] = {0, 0, 0};
        std::uint8_t group = kHitGeneric;
        /// Nothing taken, but felt (a grunt's helmet: the game's 0.01).
        bool graze = false;
    };

    /// What one of the people thinks besides (CTalkMonster's fields, and a
    /// few of its kinds').
    struct AiTalk
    {
        std::uint8_t said = 0;       ///< m_bitsSaid (kSaid*)
        std::uint8_t speak = 0;      ///< m_nSpeak: how many times it has started talk
        std::uint8_t voicePitch = 100;
        std::int16_t target = -1;    ///< m_hTalkTarget: who it talks to (kAiNone: nobody)
        std::int32_t useTime = 0;    ///< m_useTime: until then, USE does nothing (a scripted line)
        std::int32_t stopTalk = 0;   ///< m_flStopTalkTime: until then, it is talking
        std::int32_t lastSmelled = 0; ///< m_flLastSaidSmelled
        std::int32_t painTime = 0;   ///< m_painTime
        std::int32_t fearTime = 0;   ///< a scientist's m_fearTime
        std::int32_t healTime = 0;   ///< ... and m_healTime
    };

    /// How a monster gets past what is in its way (Move): how long it waits
    /// for something that moves (m_moveWaitTime, set by each MoveTo), and
    /// the way round it, while it has one (a detour waypoint). Saved, so
    /// that a game loaded goes on as it would have (the game itself does not
    /// keep m_moveWaitTime).
    struct AiMoveRound
    {
        std::int32_t waitMs = 0;
        fx apex[3] = {0, 0, 0};
        bool detouring = false; ///< walking to `apex` first
        bool asking = false;    ///< a way round is being worked out
        std::int32_t lookMs = 0; ///< until it next looks along its way for others in it
        fx checked = 0;          ///< how far on its way was clear when it was held up (flDist)
    };

    /// What one monster thinks (the CBaseMonster fields that are not its
    /// entity's). Not saved.
    struct AiMonster
    {
        std::int16_t actor = -1;
        bool started = false;      ///< StartMonster has been
        bool scripted = false;     ///< a script had it when it last thought
        std::uint8_t state = kStateNone;
        std::uint8_t idealState = kStateIdle;
        std::uint8_t deadflag = 0; ///< DEAD_NO, DEAD_DYING, DEAD_DEAD
        std::uint32_t conditions = 0;
        std::uint32_t memory = 0;
        std::uint32_t capability = 0;
        const AiSchedule *schedule = nullptr;
        std::int16_t scheduleId = -1; ///< (`schedule` as a save keeps it: HlAiSave.hpp)
        std::uint8_t scheduleIndex = 0;
        std::uint8_t taskStatus = kTaskStatusNew;
        std::uint8_t failSchedule = kSchedNone;
        std::uint8_t activity = kActReset;
        std::uint8_t idealActivity = kActIdle;
        std::uint8_t movementActivity = kActIdle;
        bool seqFinished = false;   ///< m_fSequenceFinished
        std::int16_t enemy = kAiNone;
        fx enemyLkp[3] = {0, 0, 0};
        std::int16_t oldEnemy[kAiOldEnemies] = {kAiNone, kAiNone, kAiNone, kAiNone};
        fx oldEnemyAt[kAiOldEnemies][3] = {};
        std::int16_t seen[kAiSeen] = {}; ///< what it saw this look (the game's m_pLink chain)
        std::uint8_t seenCount = 0;
        std::int8_t audible[kAiAudible] = {};
        std::uint8_t audibleCount = 0;
        std::uint8_t soundTypes = 0; ///< m_afSoundTypes
        fx lastPosition[3] = {0, 0, 0};
        // Moving.
        std::uint8_t movementGoal = kMoveGoalNone;
        fx moveGoal[3] = {0, 0, 0};
        std::int32_t moveBlockedMs = 0;
        std::int32_t moveWaitFinished = 0;
        bool moveAsked = false; ///< the first plan of this move has been asked for
        AiMoveRound round;
        fx groundSpeed = 0;
        // Turning.
        std::uint16_t idealYaw = 0;
        bool turning = false; ///< a task is turning it to idealYaw
        int yawSpeed = 0;     ///< pev->yaw_speed, degrees
        // Time.
        std::int32_t thinkMs = 0;
        std::int32_t waitFinished = 0;
        std::int32_t hungryTime = 0;
        std::uint8_t lastHitGroup = kHitGeneric;
        fx attackDir[3] = {0, 0, 0}; ///< g_vecAttackDir, as it was for the last blow
        int maxHealth = 0;
        std::int16_t leaf = 0; ///< where its eyes are, for the PVS
        // In the air (a leap, a hop): its velocity is ActorState's.
        bool inAir = false;
        bool touchArmed = false; ///< a headcrab's LeapTouch is set
        // A squad (CSquadMonster).
        std::int16_t squadLeader = kAiNone;
        std::int16_t squadMember[kMaxSquadMembers - 1] = {kAiNone, kAiNone, kAiNone, kAiNone};
        std::uint16_t squadSlots = 0; ///< the leader's m_afSquadSlots
        std::uint16_t mySlot = kSlotNone;
        bool enemyEluded = false;
        std::int32_t lastEnemySightTime = 0;
        // A kind's own (its own fields in the game).
        std::int32_t time1 = 0, time2 = 0, time3 = 0;
        bool flag1 = false, flag2 = false, flag3 = false;
        int count1 = 0;
        fx vec1[3] = {0, 0, 0};
        fx value1 = 0, value2 = 0;
        std::int16_t other = kAiNone;
        /// m_hTargetEnt: who it follows (one of the people's leader).
        std::int16_t targetEnt = kAiNone;
        AiTalk talk;
    };

    struct AiWorld
    {
        AiMonster *monsters = nullptr;
        int capacity = 0;
        int used = 0;
        /// Each actor's monster (into `monsters`), or -1.
        std::int16_t *slotOf = nullptr;
        /// Each actor: held by a barnacle (bit 0).
        std::uint8_t *held = nullptr;
        int actorCount = 0;
        SoundEnt sounds;
        std::int32_t now = 0; ///< gpGlobals->time, in ms
        std::int16_t playerLeaf = 0;
        std::int16_t playerEyeLeaf = 0; ///< ... and where their eyes are (not saved: each frame's)
        int lastCoverSearch = 0; ///< WorldGraph.m_iLastCoverSearch
        /// What a monster's blow did to the player's view (the game's
        /// punchangle), degrees Q12, for the game to kick it by; it clears it.
        fx punch[3] = {0, 0, 0};
        /// The player, as of the last tick (for a blow that comes from
        /// somewhere else in the game: its TakeDamage wants them).
        PlayerState *player = nullptr;
        /// g_talkWaitTime: until then, none of the people starts to talk.
        std::int32_t talkWait = 0;
        /// How long the map's Section::Speech is (LogicWorld::speech): past
        /// the soldiers' table, what the people say. The map's, not saved.
        int speechCount = 0;
    };

    struct AiCtx;

    /// What a kind does differently (the game's virtual functions). Null:
    /// CBaseMonster's own.
    struct AiKind
    {
        std::uint8_t kind = 0;
        int health = 0;
        fx eye = 0;              ///< pev->view_ofs.z: the model's own eye position
        fx fov = 0;              ///< m_flFieldOfView (a dot product, Q12)
        std::uint32_t capability = 0; ///< what Spawn() sets; StartMonster adds the attacks the model has
        std::uint8_t soundMask = kSoundWorld | kSoundCombat | kSoundPlayer; ///< ISoundMask
        int blood = 0;
        const AiSchedule *(*getSchedule)(AiCtx &) = nullptr;
        const AiSchedule *(*scheduleOfType)(AiCtx &, int type) = nullptr;
        bool (*startTask)(AiCtx &, const AiTask &) = nullptr; ///< true: done here
        bool (*runTask)(AiCtx &, const AiTask &) = nullptr;
        void (*animEvent)(AiCtx &, int event) = nullptr;
        bool (*checkRange1)(AiCtx &, fx dot, fx dist) = nullptr;
        bool (*checkRange2)(AiCtx &, fx dot, fx dist) = nullptr;
        bool (*checkMelee1)(AiCtx &, fx dot, fx dist) = nullptr;
        bool (*checkMelee2)(AiCtx &, fx dot, fx dist) = nullptr;
        int (*relationship)(AiCtx &, std::int16_t target, int base) = nullptr;
        std::uint32_t (*ignoreConditions)(AiCtx &) = nullptr;
        void (*prescheduleThink)(AiCtx &) = nullptr;
        void (*runAI)(AiCtx &) = nullptr; ///< after CBaseMonster::RunAI
        int (*yawSpeed)(AiCtx &) = nullptr;
        void (*sound)(AiCtx &, int which) = nullptr;
        /// Before CBaseMonster::TakeDamage: may change the blow; false: it
        /// does nothing.
        bool (*takeDamage)(AiCtx &, AiDamage &) = nullptr;
        bool (*traceAttack)(AiCtx &, AiDamage &) = nullptr;
        void (*scheduleChange)(AiCtx &) = nullptr;
        std::uint8_t (*idealState)(AiCtx &) = nullptr;
        /// Killed: true if it has seen to all of it itself (a barnacle).
        bool (*killed)(AiCtx &) = nullptr;
        /// Its own think, in place of MonsterThink (a barnacle's).
        void (*think)(AiCtx &) = nullptr;
        /// Each frame, before anything else of it (a barnacle holding on).
        void (*frame)(AiCtx &, int ms) = nullptr;
        /// LeapTouch: it touched `other` while its touch was set.
        void (*touch)(AiCtx &, std::int16_t other) = nullptr;
        /// CheckAmmo (NO_AMMO_LOADED when its gun is empty).
        void (*checkAmmo)(AiCtx &) = nullptr;
        /// FCanCheckAttacks (the base: it sees its enemy, not too far off).
        bool (*canCheckAttacks)(AiCtx &) = nullptr;
        /// The end of StartMonster: what a kind does once its squad is made.
        void (*started)(AiCtx &) = nullptr;
        /// A CSquadMonster (its GetScheduleOfType and the rest), squad or not.
        bool squadMonster = false;
        /// Center(), above its feet, if not its box's middle (a headcrab's).
        fx center = 0;
        void (*setActivity)(AiCtx &, std::uint8_t activity) = nullptr; ///< (a houndeye's mad idle)
        /// GetStoppedActivity (the base: standing).
        std::uint8_t (*stoppedActivity)(AiCtx &) = nullptr;
        /// CoverRadius: how far it looks for cover.
        int coverRadius = 784;
        bool (*canActiveIdle)(AiCtx &) = nullptr;
        /// For a model cooked before clips said what they are for: which
        /// clip, by name, is each activity ("idle1" for kActIdle, ...),
        /// and its events ({clip, frame, event}).
        const struct AiClipName *clipNames = nullptr;
        const struct AiClipEvent *clipEvents = nullptr;
    };

    struct AiClipName
    {
        std::uint8_t activity; ///< kActReset ends the list
        const char *clip;
    };
    struct AiClipEvent
    {
        const char *clip; ///< null ends the list
        int frame;
        int event;
    };

    struct AiCtx
    {
        LogicWorld &w;
        PlayerState &player;
        ActorWorld &a;
        AiWorld &ai;
        int index;
        ActorState &me;
        AiMonster &m;
        const AiKind &kind;
    };

    inline const AiKind *aiKindOf(std::uint8_t kind); // HlAiMonsters.hpp

    /// Where the monsters' thinking goes, in a build that measures it: the
    /// game sets the clock, and reads (and clears) what each part took --
    /// looking; listening, and looking to its enemy; going about.
    inline std::uint32_t (*aiClock)() = nullptr;
    inline std::uint32_t aiSpent[3] = {};
    struct AiSpentScope
    {
        int part;
        std::uint32_t start;
        explicit AiSpentScope(int p) : part(p), start(aiClock != nullptr ? aiClock() : 0) {}
        ~AiSpentScope()
        {
            if (aiClock != nullptr)
                aiSpent[part] += aiClock() - start;
        }
    };

    // ------------------------------------------------------------- the world

    /// The side table, set up for a map's actors: `storage` for at most
    /// `capacity` monsters, `slotOf` and `held` for each of `actorCount`.
    inline void aiAttach(ActorWorld &a, AiWorld &ai, AiMonster *storage, int capacity, std::int16_t *slotOf,
                         std::uint8_t *held, int actorCount)
    {
        ai.monsters = storage;
        ai.capacity = capacity;
        ai.used = 0;
        ai.slotOf = slotOf;
        ai.held = held;
        ai.actorCount = actorCount;
        for (int i = 0; i < actorCount; ++i)
        {
            slotOf[i] = -1;
            held[i] = 0;
        }
        for (int i = 0; i < capacity; ++i)
            storage[i] = AiMonster();
        soundEntInit(ai.sounds);
        ai.now = 1000; // (the server's clock starts a map at one second)
        ai.lastCoverSearch = 0;
        ai.punch[0] = ai.punch[1] = ai.punch[2] = 0;
        ai.talkWait = 0;
        a.ai = &ai;
    }

    /// Everything forgotten, on the same storage (a saved state laid over
    /// the map: each monster chooses afresh from where it is).
    inline void aiRestart(ActorWorld &a)
    {
        if (a.ai == nullptr)
            return;
        AiWorld &ai = *a.ai;
        aiAttach(a, ai, ai.monsters, ai.capacity, ai.slotOf, ai.held, ai.actorCount);
    }

    /// How many monsters a map's actors need thinking for.
    inline int aiNeeded(const ActorWorld &a)
    {
        int n = 0;
        for (int i = 0; i < a.count; ++i)
            n += aiKindOf(a.states[i].kind) != nullptr ? 1 : 0;
        // (And the ones a monstermaker may yet make: kept places, dormant.)
        return n;
    }

    /// The monster of actor `index`, made on first asking; null if it is
    /// not a kind that thinks this way, or there is no room.
    inline AiMonster *aiOf(ActorWorld &a, int index)
    {
        if (a.ai == nullptr || index < 0 || index >= a.ai->actorCount || index >= a.count)
            return nullptr;
        AiWorld &ai = *a.ai;
        if (ai.slotOf[index] >= 0)
            return &ai.monsters[ai.slotOf[index]];
        if (aiKindOf(a.states[index].kind) == nullptr)
            return nullptr;
        for (int s = 0; s < ai.capacity; ++s)
        {
            if (ai.monsters[s].actor >= 0)
                continue;
            ai.monsters[s] = AiMonster();
            ai.monsters[s].actor = static_cast<std::int16_t>(index);
            ai.slotOf[index] = static_cast<std::int16_t>(s);
            ++ai.used;
            return &ai.monsters[s];
        }
        return nullptr;
    }

    /// Actor `index`'s monster if it has one already.
    inline AiMonster *aiFind(const ActorWorld &a, int index)
    {
        if (a.ai == nullptr || index < 0 || index >= a.ai->actorCount || a.ai->slotOf[index] < 0)
            return nullptr;
        return &a.ai->monsters[a.ai->slotOf[index]];
    }

    /// SquadRemove (asked of the leader)
    inline void aiSquadRemove(ActorWorld &a, int leaderIndex, int removeIndex)
    {
        AiMonster *leader = aiFind(a, leaderIndex);
        AiMonster *gone = aiFind(a, removeIndex);
        if (leader == nullptr || gone == nullptr)
            return;
        if (removeIndex == leaderIndex)
        {
            for (std::int16_t &member : leader->squadMember)
            {
                if (AiMonster *m = aiFind(a, member))
                    m->squadLeader = kAiNone;
                member = kAiNone;
            }
        }
        else
        {
            for (std::int16_t &member : leader->squadMember)
            {
                if (member == removeIndex)
                {
                    member = kAiNone;
                    break;
                }
            }
        }
        gone->squadLeader = kAiNone;
    }

    /// Out of its slot and out of its squad (VacateSlot and SquadRemove),
    /// if it is in one: whatever takes a monster away -- a death, a
    /// killtarget, a level change, its place given to another -- does this,
    /// or the squad keeps a slot taken and a member who is not there.
    inline void aiSquadLeave(ActorWorld &a, int index)
    {
        AiMonster *m = aiFind(a, index);
        if (m == nullptr || m->squadLeader == kAiNone)
            return;
        if (m->mySlot != kSlotNone)
        {
            AiMonster *leader = aiFind(a, m->squadLeader);
            (leader != nullptr ? leader : m)->squadSlots &= static_cast<std::uint16_t>(~m->mySlot);
            m->mySlot = kSlotNone;
        }
        aiSquadRemove(a, m->squadLeader, index);
    }

    /// Its monster forgotten (a monstermaker makes a new one in its place).
    inline void aiForget(ActorWorld &a, int index)
    {
        if (a.ai == nullptr || index < 0 || index >= a.ai->actorCount || a.ai->slotOf[index] < 0)
            return;
        aiSquadLeave(a, index);
        a.ai->monsters[a.ai->slotOf[index]] = AiMonster();
        a.ai->slotOf[index] = -1;
        --a.ai->used;
        a.ai->held[index] = 0;
    }

    /// A sound into the monsters' world (CSoundEnt::InsertSound); nothing if
    /// there is no side table.
    inline void aiSound(ActorWorld &a, std::uint8_t type, const fx *origin, int volume, int durationMs)
    {
        if (a.ai != nullptr)
            soundInsert(a.ai->sounds, a.ai->now, type, origin, volume, durationMs);
    }
    inline void aiSound(LogicWorld &w, std::uint8_t type, const fx *origin, int volume, int durationMs)
    {
        if (w.actors != nullptr)
            aiSound(*w.actors, type, origin, volume, durationMs);
    }
    /// The player's gun was this loud (m_pPlayer->m_iWeaponVolume = ...).
    inline void aiWeaponVolume(LogicWorld &w, int volume)
    {
        if (w.actors != nullptr && w.actors->ai != nullptr)
            playerWeaponVolume(w.actors->ai->sounds, volume);
    }
    /// In AiWorld::held: a soldier let down on a rope by an osprey
    /// (HlMilitary.hpp): he does nothing until he is down.
    inline constexpr std::uint8_t kHeldRope = 2;
    /// Whether a barnacle has hold of actor `index`, or a rope.
    inline bool aiHeld(const ActorWorld &a, int index)
    {
        return a.ai != nullptr && index >= 0 && index < a.ai->actorCount && (a.ai->held[index] & (1 | kHeldRope)) != 0;
    }

    inline std::uint32_t aiRandom(ActorWorld &a)
    {
        a.seed = a.seed * 1664525u + 1013904223u;
        return a.seed >> 8;
    }
    /// RANDOM_LONG(low, high)
    inline int aiRandomLong(ActorWorld &a, int low, int high)
    {
        if (high <= low)
            return low;
        return low + static_cast<int>(aiRandom(a) % static_cast<std::uint32_t>(high - low + 1));
    }
    /// RANDOM_FLOAT(low, high), for times: in ms.
    inline int aiRandomMs(ActorWorld &a, int lowMs, int highMs)
    {
        return aiRandomLong(a, lowMs, highMs);
    }
} // namespace hldata
