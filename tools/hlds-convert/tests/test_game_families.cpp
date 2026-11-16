// The game's families (HlLevel.hpp's GameFamily): what the core reaches of
// the bosses and the soldiers is what they register, and nothing else -- so
// that each can be a library of its own, loaded with the map that needs it.
// And the save's list of schedules is the same as it always was, place for
// place, whether a family is there or not: an old save reads the same.

#include <cstdio>

#include "HlSaveState.hpp"

using namespace hldata;

namespace
{
    int failures = 0;
    void check(bool ok, const char *what)
    {
        if (!ok)
        {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    }

    /// The save's list as it was before the families (2026-11-16): every
    /// place must hold the same schedule now.
    const AiSchedule *const kBefore[] = {
        &slFail, &slIdleStand, &slIdleTrigger, &slIdleWalk, &slAmbush, &slActiveIdle, &slWakeAngry,
        &slAlertFace, &slAlertSmallFlinch, &slAlertStand, &slInvestigateSound, &slCombatStand, &slCombatFace,
        &slStandoff, &slArmWeapon, &slReload, &slRangeAttack1, &slRangeAttack2, &slPrimaryMeleeAttack,
        &slSecondaryMeleeAttack, &slSpecialAttack1, &slSpecialAttack2, &slChaseEnemy, &slChaseEnemyFailed,
        &slSmallFlinch, &slDie, &slVictoryDance, &slBarnacleVictimGrab, &slBarnacleVictimChomp, &slError,
        &slCower, &slTakeCoverFromOrigin, &slTakeCoverFromBestSound, &slTakeCoverFromEnemy,
        &headcrab::slRangeAttack1, &slave::slSlaveAttack1, &bullsquid::slRangeAttack1,
        &bullsquid::slChaseEnemy, &bullsquid::slHurtHop, &bullsquid::slSeeCrab, &bullsquid::slEat,
        &bullsquid::slSniffAndEat, &bullsquid::slWallow, &houndeye::slGuardPack, &houndeye::slRangeAttack,
        &houndeye::slSleep, &houndeye::slWakeLazy, &houndeye::slWakeUrgent, &houndeye::slSpecialAttack1,
        &houndeye::slAgitated, &houndeye::slHopRetreat, &houndeye::slCombatFailPvs,
        &houndeye::slCombatFailNoPvs, &grunt::slFail, &grunt::slCombatFail, &grunt::slVictoryDance,
        &grunt::slEstablishLineOfFire, &grunt::slFoundEnemy, &grunt::slCombatFace, &grunt::slSignalSuppress,
        &grunt::slSuppress, &grunt::slWaitInCover, &grunt::slTakeCover, &grunt::slGrenadeCover,
        &grunt::slTossGrenadeCover, &grunt::slTakeCoverFromBestSound, &grunt::slHideReload, &grunt::slSweep,
        &grunt::slRangeAttack1A, &grunt::slRangeAttack1B, &grunt::slRangeAttack2,
        &slIdleResponse, &slIdleSpeak, &slIdleSpeakWait, &slIdleHello, &slIdleStopShooting, &slMoveAway,
        &slMoveAwayFail, &slMoveAwayFollow, &slTlkIdleWatchClient, &slTlkIdleWatchClientStare,
        &slTlkIdleEyecontact, &scientist::slFollow, &scientist::slFollowScared, &scientist::slFaceTargetScared,
        &scientist::slStopFollowing, &scientist::slHeal, &scientist::slFaceTarget, &scientist::slSciPanic,
        &scientist::slIdleSciStand, &scientist::slScientistCover, &scientist::slScientistHide,
        &scientist::slScientistStartle, &scientist::slFear, &barney::slBaFollow, &barney::slBarneyEnemyDraw,
        &barney::slBaFaceTarget, &barney::slIdleBaStand,
        &agrunt::slAGruntFail, &agrunt::slAGruntCombatFail, &agrunt::slAGruntStandoff,
        &agrunt::slAGruntRangeAttack1, &agrunt::slAGruntTakeCoverFromEnemy, &agrunt::slAGruntVictoryDance,
        &agrunt::slAGruntThreatDisplay,
        &garg::slFlame, &garg::slSwipe, &babycrab::slRangeAttack1Fast, &assassin::slFail, &assassin::slExposed,
        &assassin::slTakeCoverFromEnemy, &assassin::slTakeCoverFromEnemy2, &assassin::slTakeCoverFromBestSound,
        &assassin::slHide, &assassin::slHunt, &assassin::slJump, &assassin::slJumpAttack, &assassin::slJumpLand,
        &momma::slBigNode, &momma::slNodeFail,
        &controller::slControllerChaseEnemy, &controller::slControllerStrafe,
        &controller::slControllerTakeCover, &controller::slControllerFail,
        &grunt::slRepel, &grunt::slRepelAttack, &grunt::slRepelLand,
    };
    constexpr int kBeforeCount = static_cast<int>(sizeof(kBefore) / sizeof(kBefore[0]));

    bool sameAsBefore()
    {
        int count = 0;
        const AiSchedule *const *all = aiSchedules(count);
        if (count != kBeforeCount)
            return false;
        for (int i = 0; i < count; ++i)
        {
            if (all[i] != kBefore[i])
                return false;
        }
        return true;
    }

    std::uint8_t k(ActorKind kind) { return static_cast<std::uint8_t>(kind); }

    void testSaveList()
    {
        check(sameAsBefore(), "the save's list of schedules is the one it was, place for place");
        check(aiScheduleId(&grunt::slRepelLand) == kBeforeCount - 1 && aiScheduleById(kBeforeCount - 1) == &grunt::slRepelLand,
              "... a soldier's schedule keeps its place, both ways");
        check(aiScheduleId(&momma::slBigNode) >= 0 && aiScheduleById(aiScheduleId(&momma::slBigNode)) == &momma::slBigNode,
              "... a boss's too");
    }

    void testFamilyAway(GameFamily family, ActorKind kind, const AiSchedule *own)
    {
        check(aiKindOf(k(kind)) != nullptr, "a family's kind, while it is there");
        if (family == kFamilyBoss)
            boss::familyRegister(false);
        else
            military::familyRegister(false);
        check(aiKindOf(k(kind)) == nullptr, "... none, once the family is gone");
        check(aiScheduleId(own) < 0, "... its schedules have no place in a save without it");
        int count = 0;
        const AiSchedule *const *all = aiSchedules(count);
        bool coreKept = count == kBeforeCount;
        for (int i = 0; i < count && coreKept; ++i)
            coreKept = all[i] == kBefore[i] || (all[i] == nullptr && familyOfKind(k(kind)) == family);
        check(coreKept, "... the rest of the list keeps its places");
        if (family == kFamilyBoss)
            check(familyHooks.stompTick == nullptr && familyHooks.mommaSpitTick == nullptr, "... nor its stomp or spit");
        else
            check(familyHooks.repelTick == nullptr, "... nor its rope");
        check(aiKindOf(k(ActorKind::Headcrab)) != nullptr && aiKindOf(k(ActorKind::Controller)) != nullptr,
              "... the core's kinds are still there");
        if (family == kFamilyBoss)
            boss::familyRegister(true);
        else
            military::familyRegister(true);
        check(aiKindOf(k(kind)) != nullptr && sameAsBefore(), "... and all back as it was once it comes again");
    }

    void testFamilyOfKind()
    {
        check(familyOfKind(k(ActorKind::Nihilanth)) == kFamilyBoss && familyOfKind(k(ActorKind::BabyCrab)) == kFamilyBoss,
              "the bosses are a family");
        check(familyOfKind(k(ActorKind::Grunt)) == kFamilyMilitary && familyOfKind(k(ActorKind::Apache)) == kFamilyMilitary,
              "the soldiers are a family");
        check(familyOfKind(k(ActorKind::Headcrab)) == kFamilyCore && familyOfKind(k(ActorKind::Controller)) == kFamilyCore &&
                  familyOfKind(k(ActorKind::Scientist)) == kFamilyCore,
              "the rest are the core's");
        // Every kind a family has, it registers.
        for (int kind = 0; kind < static_cast<int>(ActorKind::Count); ++kind)
        {
            const GameFamily f = familyOfKind(static_cast<std::uint8_t>(kind));
            if (f != kFamilyCore && actorThinks(static_cast<std::uint8_t>(kind)))
                check(aiFamilyKind[kind] != nullptr, "a family's thinking kind is registered by it");
        }
    }
} // namespace

int main()
{
    testSaveList();
    testFamilyAway(kFamilyBoss, ActorKind::Gargantua, &garg::slFlame);
    testFamilyAway(kFamilyMilitary, ActorKind::Grunt, &grunt::slTakeCover);
    testFamilyOfKind();
    std::printf("test_game_families: %s\n", failures == 0 ? "passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
