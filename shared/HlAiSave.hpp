#pragma once

// What the monsters think (HlAi.hpp's side table), in a save: written after
// everything else a save holds, in its own block that says what it is,
// whose layout it is of, and how long it is -- so that a save made before
// it was (or by a build whose monsters think differently) is still read,
// and its monsters simply think afresh.
//
// Each monster's schedule is kept by its place in the list of every
// schedule there is (aiSchedules), the rest field by field, as the map's
// own state is (HlSaveState.hpp).
//
// Included at the end of HlSaveState.hpp. std-only and inline: the host
// tests run it.

#include <cstring>

namespace hldata
{
    HL_FIELDS(AiSound, 5);
    HL_FIELDS(SoundEnt, 6);
    HL_FIELDS(AiTalk, 10);
    HL_FIELDS(AiMonster, 65);

    /// Every schedule there is, in a fixed order: a schedule in a save is
    /// its place here.
    inline const AiSchedule *const *aiSchedules(int &count)
    {
        static const AiSchedule *const kAll[] = {
            // CBaseMonster's (defaultai.cpp)
            &slFail, &slIdleStand, &slIdleTrigger, &slIdleWalk, &slAmbush, &slActiveIdle, &slWakeAngry,
            &slAlertFace, &slAlertSmallFlinch, &slAlertStand, &slInvestigateSound, &slCombatStand, &slCombatFace,
            &slStandoff, &slArmWeapon, &slReload, &slRangeAttack1, &slRangeAttack2, &slPrimaryMeleeAttack,
            &slSecondaryMeleeAttack, &slSpecialAttack1, &slSpecialAttack2, &slChaseEnemy, &slChaseEnemyFailed,
            &slSmallFlinch, &slDie, &slVictoryDance, &slBarnacleVictimGrab, &slBarnacleVictimChomp, &slError,
            &slCower, &slTakeCoverFromOrigin, &slTakeCoverFromBestSound, &slTakeCoverFromEnemy,
            // the kinds' own
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
            // the people's (talkmonster.cpp, scientist.cpp, barney.cpp)
            &slIdleResponse, &slIdleSpeak, &slIdleSpeakWait, &slIdleHello, &slIdleStopShooting, &slMoveAway,
            &slMoveAwayFail, &slMoveAwayFollow, &slTlkIdleWatchClient, &slTlkIdleWatchClientStare,
            &slTlkIdleEyecontact, &scientist::slFollow, &scientist::slFollowScared, &scientist::slFaceTargetScared,
            &scientist::slStopFollowing, &scientist::slHeal, &scientist::slFaceTarget, &scientist::slSciPanic,
            &scientist::slIdleSciStand, &scientist::slScientistCover, &scientist::slScientistHide,
            &scientist::slScientistStartle, &scientist::slFear, &barney::slBaFollow, &barney::slBarneyEnemyDraw,
            &barney::slBaFaceTarget, &barney::slIdleBaStand,
            // round 3's (HlAliens.hpp: agrunt.cpp, controller.cpp)
            &agrunt::slAGruntFail, &agrunt::slAGruntCombatFail, &agrunt::slAGruntStandoff,
            &agrunt::slAGruntRangeAttack1, &agrunt::slAGruntTakeCoverFromEnemy, &agrunt::slAGruntVictoryDance,
            &agrunt::slAGruntThreatDisplay,
            // round 4's (gargantua.cpp, headcrab.cpp's CBabyCrab, hassassin.cpp)
            &garg::slFlame, &garg::slSwipe, &babycrab::slRangeAttack1Fast, &assassin::slFail, &assassin::slExposed,
            &assassin::slTakeCoverFromEnemy, &assassin::slTakeCoverFromEnemy2, &assassin::slTakeCoverFromBestSound,
            &assassin::slHide, &assassin::slHunt, &assassin::slJump, &assassin::slJumpAttack, &assassin::slJumpLand,
            // round 5's (bigmomma.cpp)
            &momma::slBigNode, &momma::slNodeFail,
        };
        count = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));
        return kAll;
    }

    inline std::int16_t aiScheduleId(const AiSchedule *schedule)
    {
        int count = 0;
        const AiSchedule *const *all = aiSchedules(count);
        for (int i = 0; i < count && schedule != nullptr; ++i)
        {
            if (all[i] == schedule)
                return static_cast<std::int16_t>(i);
        }
        return -1;
    }
    inline const AiSchedule *aiScheduleById(int id)
    {
        int count = 0;
        const AiSchedule *const *all = aiSchedules(count);
        return id >= 0 && id < count ? all[id] : nullptr;
    }

    namespace detail
    {
        /// AIS2 has what is held (AiWorld::held: a barnacle's catch, a soldier
        /// on his rope) after the sounds; AIS1, before it was kept, has not
        /// (and is still read: what it held is let go).
        inline constexpr char kAiStateMagic[4] = {'A', 'I', 'S', '2'};
        inline constexpr char kAiStateMagicOld[4] = {'A', 'I', 'S', '1'};

        struct AiStateHeading
        {
            char magic[4];
            std::uint32_t print;    ///< of the layout of what follows
            std::uint32_t bytes;    ///< after this heading
            std::int32_t monsters;  ///< how many monsters' worth
        };
        static_assert(sizeof(AiStateHeading) == 16);

        /// The side table's whole state, through `a`: the time, the sounds,
        /// and each monster that has a mind (by its actor), `count` of them.
        template <class A>
        void aiState(A &a, AiWorld &ai, ActorWorld &actors, int count, bool held = true)
        {
            stateField(a, ai.now);
            stateField(a, ai.lastCoverSearch);
            stateField(a, ai.talkWait);
            stateField(a, ai.sounds);
            for (int i = 0; held && ai.held != nullptr && i < ai.actorCount; ++i)
                stateField(a, ai.held[i]);
            for (int n = 0; n < count; ++n)
            {
                // (Written: each in use, by its actor, in order. Read: into
                // the place its actor takes.)
                std::int16_t actor = -1;
                AiMonster scratch;
                AiMonster *m = &scratch;
                if constexpr (A::kGathers)
                {
                    int seen = 0;
                    for (int s = 0; s < ai.capacity; ++s)
                    {
                        if (ai.monsters[s].actor < 0)
                            continue;
                        if (seen++ == n)
                        {
                            m = &ai.monsters[s];
                            break;
                        }
                    }
                    actor = m->actor;
                    m->scheduleId = aiScheduleId(m->schedule);
                }
                stateField(a, actor);
                stateField(a, *m);
                if constexpr (A::kReads)
                {
                    if (actor >= 0)
                    {
                        if (AiMonster *place = aiOf(actors, actor))
                        {
                            *place = scratch;
                            place->schedule = aiScheduleById(scratch.scheduleId);
                            place->actor = actor;
                        }
                    }
                }
            }
        }

        inline AiStateHeading aiStateHeading(AiWorld &ai, ActorWorld &actors, int count, bool held = true)
        {
            StateMeasure measure;
            aiState(measure, ai, actors, count, held);
            AiStateHeading h{};
            std::memcpy(h.magic, held ? kAiStateMagic : kAiStateMagicOld, sizeof(h.magic));
            h.print = measure.print;
            h.bytes = measure.bytes;
            h.monsters = count;
            return h;
        }
    } // namespace detail

    /// The monsters' minds, at the end of `bytes` (nothing, if they have no
    /// side table).
    inline void aiAppendState(std::vector<std::uint8_t> &bytes, ActorWorld &actors)
    {
        if (actors.ai == nullptr)
            return;
        AiWorld &ai = *actors.ai;
        int count = 0;
        for (int s = 0; s < ai.capacity; ++s)
            count += ai.monsters[s].actor >= 0 ? 1 : 0;
        const detail::AiStateHeading heading = detail::aiStateHeading(ai, actors, count);
        const std::size_t at = bytes.size();
        bytes.resize(at + sizeof(heading) + heading.bytes);
        std::memcpy(bytes.data() + at, &heading, sizeof(heading));
        detail::StreamOut out = detail::memoryOut(bytes.data() + at + sizeof(heading), heading.bytes);
        detail::aiState(out, ai, actors, count);
    }

    /// How long the block of minds at `bytes` is, heading and all; 0 if it
    /// is not one, or not this build's.
    inline std::size_t aiStateLength(const std::uint8_t *bytes, std::size_t size, ActorWorld &actors)
    {
        detail::AiStateHeading have;
        if (size < sizeof(have) || actors.ai == nullptr)
            return 0;
        std::memcpy(&have, bytes, sizeof(have));
        const bool held = std::memcmp(have.magic, detail::kAiStateMagic, sizeof(have.magic)) == 0;
        if ((!held && std::memcmp(have.magic, detail::kAiStateMagicOld, sizeof(have.magic)) != 0) ||
            have.monsters < 0 || have.monsters > actors.ai->capacity || have.bytes > size - sizeof(have))
            return 0;
        const detail::AiStateHeading want = detail::aiStateHeading(*actors.ai, actors, have.monsters, held);
        if (have.print != want.print || have.bytes != want.bytes)
            return 0;
        return sizeof(have) + have.bytes;
    }

    /// The minds laid back over the map's monsters (which have just been
    /// laid back themselves). False, and they think afresh, if the block is
    /// not one this build can read.
    inline bool aiLayState(const std::uint8_t *bytes, std::size_t size, ActorWorld &actors)
    {
        const std::size_t length = aiStateLength(bytes, size, actors);
        if (length == 0)
            return false;
        detail::AiStateHeading have;
        std::memcpy(&have, bytes, sizeof(have));
        aiRestart(actors);
        detail::StreamIn in = detail::memoryIn(bytes + sizeof(have), length - sizeof(have));
        detail::aiState(in, *actors.ai, actors, have.monsters,
                        std::memcmp(have.magic, detail::kAiStateMagic, sizeof(have.magic)) == 0);
        return true;
    }

    /// How many monsters have minds to be kept.
    inline int aiStateCount(const ActorWorld &actors)
    {
        int count = 0;
        for (int s = 0; actors.ai != nullptr && s < actors.ai->capacity; ++s)
            count += actors.ai->monsters[s].actor >= 0 ? 1 : 0;
        return count;
    }

    /// The minds, heading first, through `out` (aiAppendState's bytes).
    inline bool aiStreamState(detail::StreamOut &out, ActorWorld &actors)
    {
        if (actors.ai == nullptr)
            return out.ok;
        const int count = aiStateCount(actors);
        detail::AiStateHeading heading = detail::aiStateHeading(*actors.ai, actors, count);
        out.put(&heading, sizeof(heading));
        detail::aiState(out, *actors.ai, actors, count);
        return out.ok;
    }

    /// ... laid back from `in`, if they are this build's (AIS1 too). False,
    /// and they think afresh, if not -- or if `in` ran out.
    inline bool aiLayStream(detail::StreamIn &in, ActorWorld &actors)
    {
        if (actors.ai == nullptr)
            return false;
        detail::AiStateHeading have;
        in.get(&have, sizeof(have));
        const bool held = std::memcmp(have.magic, detail::kAiStateMagic, sizeof(have.magic)) == 0;
        if (!in.ok || (!held && std::memcmp(have.magic, detail::kAiStateMagicOld, sizeof(have.magic)) != 0) ||
            have.monsters < 0 || have.monsters > actors.ai->capacity)
            return false;
        const detail::AiStateHeading want = detail::aiStateHeading(*actors.ai, actors, have.monsters, held);
        if (have.print != want.print || have.bytes != want.bytes)
            return false;
        aiRestart(actors);
        detail::aiState(in, *actors.ai, actors, have.monsters, held);
        if (!in.ok)
        {
            aiRestart(actors);
            return false;
        }
        return true;
    }
} // namespace hldata
