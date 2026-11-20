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
    HL_FIELDS(SoundEnt, 8);
    HL_FIELDS(AiTalk, 10);
    HL_FIELDS(AiMoveRound, 6);
    HL_FIELDS(AiMonster, 73);

    namespace detail
    {
        /// A place in the save's list that is a family's: the family, and the
        /// `index`-th of its own (as it registers them: HlAi.hpp).
        constexpr std::uint8_t familyCode(GameFamily f, int index)
        {
            return static_cast<std::uint8_t>((f << 6) | index);
        }

        /// Every schedule there is, in a fixed order: a schedule in a save is
        /// its place here. (A family's are none while it is not there; a save
        /// with one of them is read with its family loaded first.)
        inline constexpr const AiSchedule *kAiScheduleCore[] = {
            // CBaseMonster's (defaultai.cpp)
            &slFail, &slIdleStand, &slIdleTrigger, &slIdleWalk, &slAmbush, &slActiveIdle, &slWakeAngry,
            &slAlertFace, &slAlertSmallFlinch, &slAlertStand, &slInvestigateSound, &slCombatStand,
            &slCombatFace,
            &slStandoff, &slArmWeapon, &slReload, &slRangeAttack1, &slRangeAttack2, &slPrimaryMeleeAttack,
            &slSecondaryMeleeAttack, &slSpecialAttack1, &slSpecialAttack2, &slChaseEnemy,
            &slChaseEnemyFailed,
            &slSmallFlinch, &slDie, &slVictoryDance, &slBarnacleVictimGrab, &slBarnacleVictimChomp,
            &slError,
            &slCower, &slTakeCoverFromOrigin, &slTakeCoverFromBestSound, &slTakeCoverFromEnemy,
            // the kinds' own
            &headcrab::slRangeAttack1, &slave::slSlaveAttack1, &bullsquid::slRangeAttack1,
            &bullsquid::slChaseEnemy, &bullsquid::slHurtHop, &bullsquid::slSeeCrab, &bullsquid::slEat,
            &bullsquid::slSniffAndEat, &bullsquid::slWallow, &houndeye::slGuardPack, &houndeye::slRangeAttack,
            &houndeye::slSleep, &houndeye::slWakeLazy, &houndeye::slWakeUrgent, &houndeye::slSpecialAttack1,
            &houndeye::slAgitated, &houndeye::slHopRetreat, &houndeye::slCombatFailPvs,
            &houndeye::slCombatFailNoPvs,
            // the soldiers' (military: hgrunt.cpp)
            nullptr, nullptr, nullptr,
            nullptr,
            nullptr, nullptr, nullptr,
            nullptr,
            nullptr, nullptr, nullptr,
            nullptr,
            nullptr, nullptr, nullptr,
            nullptr,
            nullptr, nullptr,
            // the people's (talkmonster.cpp, scientist.cpp, barney.cpp)
            &slIdleResponse, &slIdleSpeak, &slIdleSpeakWait, &slIdleHello, &slIdleStopShooting,
            &slMoveAway,
            &slMoveAwayFail, &slMoveAwayFollow, &slTlkIdleWatchClient, &slTlkIdleWatchClientStare,
            &slTlkIdleEyecontact, &scientist::slFollow, &scientist::slFollowScared,
            &scientist::slFaceTargetScared,
            &scientist::slStopFollowing, &scientist::slHeal, &scientist::slFaceTarget, &scientist::slSciPanic,
            &scientist::slIdleSciStand, &scientist::slScientistCover, &scientist::slScientistHide,
            &scientist::slScientistStartle, &scientist::slFear, &barney::slBaFollow,
            &barney::slBarneyEnemyDraw,
            &barney::slBaFaceTarget, &barney::slIdleBaStand,
            // round 3's (HlAliens.hpp: agrunt.cpp)
            &agrunt::slAGruntFail, &agrunt::slAGruntCombatFail, &agrunt::slAGruntStandoff,
            &agrunt::slAGruntRangeAttack1, &agrunt::slAGruntTakeCoverFromEnemy, &agrunt::slAGruntVictoryDance,
            &agrunt::slAGruntThreatDisplay,
            // round 4's (gargantua.cpp, headcrab.cpp's CBabyCrab: boss;
            // hassassin.cpp: military)
            nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr,
            nullptr,
            nullptr, nullptr, nullptr,
            nullptr,
            nullptr, nullptr,
            // round 5's (bigmomma.cpp: boss)
            nullptr, nullptr,
            // the controller's (controller.cpp), left out until now: added
            // last, so that what is in a save already keeps its place
            &controller::slControllerChaseEnemy, &controller::slControllerStrafe,
            &controller::slControllerTakeCover, &controller::slControllerFail,
            // a soldier's on a rope (hgrunt.cpp: military), likewise
            nullptr, nullptr, nullptr,
        };
        /// ... and where it is a family's: which (the top two bits) and the
        /// how-manyth of its own (the rest); 0 for the core's.
        inline constexpr std::uint8_t kAiScheduleFamily[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, familyCode(kFamilyMilitary, 0),
            familyCode(kFamilyMilitary, 1), familyCode(kFamilyMilitary, 2), familyCode(kFamilyMilitary, 3),
            familyCode(kFamilyMilitary, 4), familyCode(kFamilyMilitary, 5), familyCode(kFamilyMilitary, 6),
            familyCode(kFamilyMilitary, 7), familyCode(kFamilyMilitary, 8), familyCode(kFamilyMilitary, 9),
            familyCode(kFamilyMilitary, 10), familyCode(kFamilyMilitary, 11), familyCode(kFamilyMilitary, 12),
            familyCode(kFamilyMilitary, 13), familyCode(kFamilyMilitary, 14), familyCode(kFamilyMilitary, 15),
            familyCode(kFamilyMilitary, 16), familyCode(kFamilyMilitary, 17), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, familyCode(kFamilyBoss, 0),
            familyCode(kFamilyBoss, 1), familyCode(kFamilyBoss, 2), familyCode(kFamilyMilitary, 18),
            familyCode(kFamilyMilitary, 19), familyCode(kFamilyMilitary, 20), familyCode(kFamilyMilitary, 21),
            familyCode(kFamilyMilitary, 22), familyCode(kFamilyMilitary, 23), familyCode(kFamilyMilitary, 24),
            familyCode(kFamilyMilitary, 25), familyCode(kFamilyMilitary, 26), familyCode(kFamilyMilitary, 27),
            familyCode(kFamilyBoss, 3), familyCode(kFamilyBoss, 4), 0, 0, 0, 0, familyCode(kFamilyMilitary, 28),
            familyCode(kFamilyMilitary, 29), familyCode(kFamilyMilitary, 30),
        };
        static_assert(sizeof(kAiScheduleFamily) == sizeof(kAiScheduleCore) / sizeof(kAiScheduleCore[0]));
        inline constexpr int kAiScheduleCount = static_cast<int>(sizeof(kAiScheduleFamily));
    } // namespace detail

    /// The schedule at a place of the save's list; null if there is none, or
    /// its family is not there. (Out of line: asked from each of the save's
    /// readings and writings.)
    [[gnu::noinline]] inline const AiSchedule *aiScheduleAt(int id)
    {
        if (id < 0 || id >= detail::kAiScheduleCount)
            return nullptr;
        if (const AiSchedule *core = detail::kAiScheduleCore[id])
            return core;
        const int family = detail::kAiScheduleFamily[id] >> 6, index = detail::kAiScheduleFamily[id] & 63;
        aiFamiliesReady();
        const AiSchedule *const *theirs = aiFamilySchedules[family];
        return theirs != nullptr && index < aiFamilyScheduleCount[family] ? theirs[index] : nullptr;
    }

    /// The whole list, as it is now (the tests').
    inline const AiSchedule *const *aiSchedules(int &count)
    {
        static const AiSchedule *resolved[detail::kAiScheduleCount];
        for (int i = 0; i < detail::kAiScheduleCount; ++i)
            resolved[i] = aiScheduleAt(i);
        count = detail::kAiScheduleCount;
        return resolved;
    }

    [[gnu::noinline]] inline std::int16_t aiScheduleId(const AiSchedule *schedule)
    {
        for (int i = 0; i < detail::kAiScheduleCount && schedule != nullptr; ++i)
        {
            if (aiScheduleAt(i) == schedule)
                return static_cast<std::int16_t>(i);
        }
        return -1;
    }
    inline const AiSchedule *aiScheduleById(int id) { return aiScheduleAt(id); }

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
