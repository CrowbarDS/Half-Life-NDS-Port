#pragma once

// The people who talk (dlls/talkmonster.cpp's CTalkMonster): what a scientist
// and a guard have in common. One of them at a time says something
// (g_talkWaitTime); he greets the player the first time he sees him, asks a
// friend near him a question that the friend answers, watches the player,
// says when he is hurt, follows the player when USEd -- or says why not --
// and stops when USEd again, steps out of the way when pushed, tells the
// player to stop shooting when a friend is hurt by him, and turns his head
// to whom he talks to. Each kind's own (scientist, guard) is in
// HlAiPeople.hpp, on top of these.
//
// The lines are the map's talk table (Section::Speech, after the soldiers'):
// a few of each of the game's groups, by voice (HlLevel.hpp's kTalk*).
//
// Included at the end of HlMonsters.hpp. std-only and inline: the host
// tests run it.

namespace hldata
{
    namespace detail
    {
        /// Whether the script at `index` would let go of its actor
        /// (CCineMonster::CanInterrupt); and letting it go (CancelScript):
        /// HlLogic.hpp.
        inline bool scriptCanInterrupt(const LogicWorld &w, int index);
        inline void scriptCancel(LogicWorld &w, PlayerState &player, int index);
    } // namespace detail

    // ------------------------------------------------------------- numbers

    // m_bitsSaid
    inline constexpr std::uint8_t kSaidDamageLight = 1;
    inline constexpr std::uint8_t kSaidDamageMedium = 2;
    inline constexpr std::uint8_t kSaidDamageHeavy = 4;
    inline constexpr std::uint8_t kSaidHelloPlayer = 8;
    inline constexpr std::uint8_t kSaidWoundLight = 16;
    inline constexpr std::uint8_t kSaidWoundHeavy = 32;
    inline constexpr std::uint8_t kSaidHeard = 64;
    inline constexpr std::uint8_t kSaidSmelled = 128;

    inline constexpr int kTalkRange = 500; ///< TALKRANGE_MIN: no friend further off is spoken to
    inline constexpr int kStareDist = 128; ///< TLK_STARE_DIST

    inline constexpr std::uint32_t kCondClientPush = kCondSpecial1;   ///< the player pushes him
    inline constexpr std::uint32_t kCondClientUnseen = kCondSpecial2; ///< ... is out of his sight

    // The game's attenuations (ATTN_*), as how far a line carries.
    inline constexpr int kAttnIdle = kRadiusSmall;
    inline constexpr int kAttnStatic = kRadiusMedium;
    inline constexpr int kAttnNorm = kRadiusLarge;

    // Their tasks, after the shared ones.
    enum : std::uint8_t
    {
        kTaskCantFollow = kTaskLastCommon,
        kTaskMoveAwayPath,
        kTaskWalkPathForUnits,
        kTaskTlkRespond,
        kTaskTlkSpeak,
        kTaskTlkHello,
        kTaskTlkHeadReset,
        kTaskTlkStopShooting,
        kTaskTlkStare,
        kTaskTlkLookAtClient,
        kTaskTlkClientStare,
        kTaskTlkEyeContact,
        kTaskTlkIdealYaw,
        kTaskFacePlayer,
        kTaskTlkLast
    };
    // Their schedules, after the shared ones.
    enum : std::uint8_t
    {
        kSchedCantFollow = kSchedLastCommon,
        kSchedMoveAway,
        kSchedMoveAwayFollow,
        kSchedMoveAwayFail,
        kSchedTlkLast
    };

    // ---------------------------------------------------------- schedules

    inline constexpr std::uint32_t kTlkInterruptWatch = kCondNewEnemy | kCondLightDamage | kCondHeavyDamage |
                                                        kCondHearSound | kCondSmell | kCondClientPush |
                                                        kCondClientUnseen | kCondProvoked;

    inline constexpr AiTask tlIdleResponse[] = {
        {kTaskSetActivity, kActIdle}, {kTaskWait, 500},        {kTaskTlkEyeContact, 0},          {kTaskTlkRespond, 0},
        {kTaskTlkIdealYaw, 0},        {kTaskFaceIdeal, 0},     {kTaskSetActivity, kActSignal3}, {kTaskTlkEyeContact, 0},
    };
    inline constexpr AiSchedule slIdleResponse = {tlIdleResponse, 8,
                                                  kCondNewEnemy | kCondLightDamage | kCondHeavyDamage, 0,
                                                  "Idle Response"};

    inline constexpr AiTask tlIdleSpeak[] = {
        {kTaskTlkSpeak, 0},    {kTaskTlkIdealYaw, 0},  {kTaskFaceIdeal, 0},
        {kTaskSetActivity, kActSignal3}, {kTaskTlkEyeContact, 0}, {kTaskWaitRandom, 500},
    };
    inline constexpr AiSchedule slIdleSpeak = {
        tlIdleSpeak, 6, kCondNewEnemy | kCondClientPush | kCondLightDamage | kCondHeavyDamage, 0, "Idle Speak"};

    inline constexpr AiTask tlIdleSpeakWait[] = {
        {kTaskSetActivity, kActSignal3}, {kTaskTlkSpeak, 0}, {kTaskTlkEyeContact, 0}, {kTaskWait, 2000},
    };
    inline constexpr AiSchedule slIdleSpeakWait = {
        tlIdleSpeakWait, 4, kCondNewEnemy | kCondClientPush | kCondLightDamage | kCondHeavyDamage, 0,
        "Idle Speak Wait"};

    inline constexpr AiTask tlIdleHello[] = {
        {kTaskSetActivity, kActSignal3}, {kTaskTlkHello, 0}, {kTaskTlkEyeContact, 0}, {kTaskWait, 500},
        {kTaskTlkHello, 0},              {kTaskTlkEyeContact, 0}, {kTaskWait, 500},   {kTaskTlkHello, 0},
        {kTaskTlkEyeContact, 0},         {kTaskWait, 500},   {kTaskTlkHello, 0},      {kTaskTlkEyeContact, 0},
        {kTaskWait, 500},
    };
    inline constexpr AiSchedule slIdleHello = {
        tlIdleHello, 13,
        kCondNewEnemy | kCondClientPush | kCondLightDamage | kCondHeavyDamage | kCondHearSound | kCondProvoked,
        kSoundCombat, "Idle Hello"};

    inline constexpr AiTask tlIdleStopShooting[] = {{kTaskTlkStopShooting, 0}};
    inline constexpr AiSchedule slIdleStopShooting = {
        tlIdleStopShooting, 1, kCondNewEnemy | kCondLightDamage | kCondHeavyDamage | kCondHearSound, 0,
        "Idle Stop Shooting"};

    inline constexpr AiTask tlMoveAway[] = {
        {kTaskSetFailSchedule, kSchedMoveAwayFail}, {kTaskStoreLastPosition, 0}, {kTaskMoveAwayPath, 100},
        {kTaskWalkPathForUnits, 100},               {kTaskStopMoving, 0},        {kTaskFacePlayer, 500},
    };
    inline constexpr AiSchedule slMoveAway = {tlMoveAway, 6, 0, 0, "MoveAway"};

    inline constexpr AiTask tlMoveAwayFail[] = {{kTaskStopMoving, 0}, {kTaskFacePlayer, 500}};
    inline constexpr AiSchedule slMoveAwayFail = {tlMoveAwayFail, 2, 0, 0, "MoveAwayFail"};

    inline constexpr AiTask tlMoveAwayFollow[] = {
        {kTaskSetFailSchedule, kSchedTargetFace}, {kTaskStoreLastPosition, 0}, {kTaskMoveAwayPath, 100},
        {kTaskWalkPathForUnits, 100},             {kTaskStopMoving, 0},        {kTaskSetSchedule, kSchedTargetFace},
    };
    inline constexpr AiSchedule slMoveAwayFollow = {tlMoveAwayFollow, 6, 0, 0, "MoveAwayFollow"};

    inline constexpr AiTask tlTlkIdleWatchClient[] = {
        {kTaskStopMoving, 0}, {kTaskSetActivity, kActIdle}, {kTaskTlkLookAtClient, 6000}};
    inline constexpr AiTask tlTlkIdleWatchClientStare[] = {
        {kTaskStopMoving, 0},     {kTaskSetActivity, kActIdle}, {kTaskTlkClientStare, 6000},
        {kTaskTlkStare, 0},       {kTaskTlkIdealYaw, 0},        {kTaskFaceIdeal, 0},
        {kTaskSetActivity, kActSignal3}, {kTaskTlkEyeContact, 0},
    };
    inline constexpr AiSchedule slTlkIdleWatchClient = {
        tlTlkIdleWatchClient, 3, kTlkInterruptWatch,
        kSoundCombat | kSoundDanger | kSoundMeat | kSoundCarcass | kSoundGarbage, "TlkIdleWatchClient"};
    inline constexpr AiSchedule slTlkIdleWatchClientStare = {
        tlTlkIdleWatchClientStare, 8, kTlkInterruptWatch,
        kSoundCombat | kSoundDanger | kSoundMeat | kSoundCarcass | kSoundGarbage, "TlkIdleWatchClientStare"};

    inline constexpr AiTask tlTlkIdleEyecontact[] = {
        {kTaskTlkIdealYaw, 0}, {kTaskFaceIdeal, 0}, {kTaskSetActivity, kActSignal3}, {kTaskTlkEyeContact, 0}};
    inline constexpr AiSchedule slTlkIdleEyecontact = {
        tlTlkIdleEyecontact, 4, kCondNewEnemy | kCondClientPush | kCondLightDamage | kCondHeavyDamage, 0,
        "TlkIdleEyecontact"};

    // ------------------------------------------------------------- talking

    namespace talk
    {
        inline bool isTalker(std::uint8_t kind)
        {
            return kind == static_cast<std::uint8_t>(ActorKind::Scientist) ||
                   kind == static_cast<std::uint8_t>(ActorKind::Guard) ||
                   kind == static_cast<std::uint8_t>(ActorKind::Sitter);
        }
        inline int voiceOf(std::uint8_t kind)
        {
            return kind == static_cast<std::uint8_t>(ActorKind::Guard) ? kVoiceBarney : kVoiceScientist;
        }
        inline bool preDisaster(const AiCtx &c)
        {
            return (c.me.item & kPersonPreDisaster) != 0 || c.me.kind == static_cast<std::uint8_t>(ActorKind::Sitter);
        }

        /// IsTalking
        inline bool isTalking(const AiCtx &c) { return c.m.talk.stopTalk > c.ai.now; }
        /// Talk: for how long it is talking (none given: three seconds).
        inline void talkFor(AiCtx &c, int ms) { c.m.talk.stopTalk = c.ai.now + (ms <= 0 ? 3000 : ms); }
        /// GetVoicePitch
        inline int voicePitch(AiCtx &c) { return c.m.talk.voicePitch + aiRandomLong(c.a, 0, 3); }
        /// IsFollowing
        inline bool isFollowing(const AiCtx &c) { return c.m.targetEnt == kAiPlayer; }

        /// A line of the map's talk table for this voice and `slot` (any
        /// one of the group's), or -1.
        inline int lineOf(AiCtx &c, int slot)
        {
            const int at = c.ai.talkAt;
            const int start = at + kTalkHeading;
            if (c.w.speech == nullptr || c.ai.speechCount < start || slot < 0 || slot >= kTalkSlots)
                return -1;
            const int group = voiceOf(c.me.kind) * kTalkSlots + slot;
            const int first = c.w.speech[at + group * 2], count = c.w.speech[at + group * 2 + 1];
            if (count == 0)
                return -1;
            return c.w.speech[start + first + aiRandomLong(c.a, 0, count - 1)];
        }

        /// The line itself said (EMIT_SOUND_DYN on its voice channel): from
        /// its mouth, with its voice.
        inline void say(AiCtx &c, int sentence, int volume, int radius)
        {
            if (sentence < 0 || c.w.heard == nullptr)
                return;
            const fx mouth[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + c.kind.eye};
            c.w.heard->add(static_cast<std::uint16_t>(sentence), radius == kRadiusEverywhere ? nullptr : mouth,
                           volume, radius, kSoundSentence, voicePitch(c));
        }

        /// PlaySentence: one of a group (none: nothing at all -- a voice
        /// that has no such group), and nobody else speaks until two seconds
        /// after it.
        inline void playSentence(AiCtx &c, int slot, int ms, int radius, int volume = 127)
        {
            if (talkGroupName(voiceOf(c.me.kind), slot) == nullptr)
                return;
            talkFor(c, ms);
            c.ai.talkWait = c.ai.now + ms + 2000;
            say(c, lineOf(c, slot), volume, radius);
            c.m.talk.said |= kSaidHelloPlayer;
        }
        /// ... a line given by itself (a scripted_sentence's).
        inline void playSentenceLine(AiCtx &c, int sentence, int ms, int radius, int volume)
        {
            talkFor(c, ms);
            c.ai.talkWait = c.ai.now + ms + 2000;
            say(c, sentence, volume, radius);
            c.m.talk.said |= kSaidHelloPlayer;
        }

        /// The player in what it can see of the map (FIND_CLIENT_IN_PVS).
        inline bool clientInPvs(const AiCtx &c)
        {
            return c.player.health > 0 && aiClientInPvs(c);
        }

        /// FOkToSpeak
        inline bool okToSpeak(AiCtx &c)
        {
            if (c.m.state == kStateProne || c.m.idealState == kStateProne)
                return false;
            if (c.m.deadflag != 0)
                return false;
            if (c.ai.now <= c.ai.talkWait)
                return false;
            if (c.me.flags & kActorGag)
                return false;
            if (!aiAlive(c.a, c.index) || !clientInPvs(c))
                return false;
            if (c.m.enemy != kAiNone)
            {
                AiTarget t;
                if (aiTargetOf(c.w, c.player, c.m.enemy, t) && aiVisible(c, t))
                    return false;
            }
            return true;
        }
        /// CanPlaySentence
        inline bool canPlaySentence(AiCtx &c, bool disregardState)
        {
            if (disregardState)
                return aiAlive(c.a, c.index);
            return okToSpeak(c);
        }

        /// IdleHeadTurn: its head to `at` (its bone controller 0, which
        /// its model keeps within its own reach).
        inline void headTurn(AiCtx &c, const fx *at)
        {
            if (!(c.m.capability & kCapTurnHead))
                return;
            const fx dx = at[0] - c.me.pos[0], dy = at[1] - c.me.pos[1];
            const std::uint16_t yaw = dx == 0 && dy == 0 ? c.me.yaw : fxAtan2Turns(dy, dx);
            c.me.aimYaw = static_cast<std::int16_t>(yaw - c.me.yaw);
        }
        inline void headTurnTo(AiCtx &c, std::int16_t id)
        {
            AiTarget t;
            if (aiTargetOf(c.w, c.player, id, t))
                headTurn(c, t.origin);
        }
        inline void headReset(AiCtx &c) { c.me.aimYaw = 0; }

        /// A friend's mind, for what one does to another.
        inline bool friendCtx(AiCtx &c, std::int16_t id, AiMonster *&m, const AiKind *&kind)
        {
            m = nullptr;
            kind = nullptr;
            if (id < 0 || id >= c.a.count)
                return false;
            m = aiOf(c.a, id);
            kind = aiKindOf(c.a.states[id].kind);
            return m != nullptr && kind != nullptr;
        }

        /// The top of a box (absmax.z), where the game looks from and to.
        inline fx topOf(const ActorState &actor)
        {
            fx mins[3], maxs[3];
            targetBox(actor, mins, maxs);
            return maxs[2];
        }

        /// FindNearestFriend: the nearest friend (or, `player`, the
        /// player) that can be seen from the top of its head to the top of
        /// theirs, within kTalkRange; not dead, not in a script, not in a
        /// barnacle's grip. kAiNone if none.
        inline std::int16_t findNearestFriend(AiCtx &c, bool player)
        {
            const fx start[3] = {c.me.pos[0], c.me.pos[1], topOf(c.me)};
            fx range = fxFromInt(10000);
            std::int16_t nearest = kAiNone;
            auto consider = [&](std::int16_t id, const fx *check)
            {
                const fx d = aiDist(start, check);
                if (d >= range || d >= fxFromInt(kTalkRange))
                    return;
                if (!lineClear(*c.w.world, start, check))
                    return;
                nearest = id;
                range = d;
            };
            if (player)
            {
                if (c.player.health > 0)
                {
                    const fx tall = c.player.ducked ? kCrouchedFeet : kStandingFeet;
                    const fx check[3] = {c.player.origin[0], c.player.origin[1], c.player.origin[2] + tall};
                    consider(kAiPlayer, check);
                }
                return nearest;
            }
            for (int i = 0; i < c.a.count; ++i)
            {
                const ActorState &other = c.a.states[i];
                if (i == c.index || !other.active || !isTalker(other.kind) || !aiAlive(c.a, i) || other.script >= 0 ||
                    aiHeld(c.a, i))
                    continue;
                const fx check[3] = {other.pos[0], other.pos[1], topOf(other)};
                consider(static_cast<std::int16_t>(i), check);
            }
            return nearest;
        }

        /// SetAnswerQuestion: the friend `id` answers whoever asked. (A
        /// sitting scientist, after a moment: HlAiPeople.hpp.)
        inline void setAnswerQuestion(AiCtx &c, std::int16_t id);

        /// IdleRespond
        inline void idleRespond(AiCtx &c)
        {
            playSentence(c, kTalkAnswer, aiRandomMs(c.a, 2800, 3200), kAttnIdle);
        }

        /// FIdleHello
        inline bool idleHello(AiCtx &c)
        {
            if (!okToSpeak(c) || (c.m.talk.said & kSaidHelloPlayer))
                return false;
            const std::int16_t player = findNearestFriend(c, true);
            if (player == kAiNone)
                return false;
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, player, t) || !aiInViewCone(c, t.origin) || !aiVisible(c, t))
                return false;
            c.m.talk.target = player;
            playSentence(c, preDisaster(c) ? kTalkPHello : kTalkHello, aiRandomMs(c.a, 3000, 3500), kAttnIdle);
            c.m.talk.said |= kSaidHelloPlayer;
            return true;
        }

        /// FIdleStare
        inline bool idleStare(AiCtx &c)
        {
            if (!okToSpeak(c))
                return false;
            playSentence(c, kTalkStare, aiRandomMs(c.a, 5000, 7500), kAttnIdle);
            c.m.talk.target = findNearestFriend(c, true);
            return true;
        }

        /// FIdleSpeak
        inline bool idleSpeak(AiCtx &c)
        {
            if (!okToSpeak(c))
                return false;
            const bool before = preDisaster(c);
            const int idleGroup = before ? kTalkPIdle : kTalkIdle;
            const int questionGroup = before ? kTalkPQuestion : kTalkQuestion;
            const int duration = before ? aiRandomMs(c.a, 4800, 5200) : aiRandomMs(c.a, 2800, 3200);
            // The player it follows, alive and hurt?
            if (c.m.targetEnt == kAiPlayer && c.player.health > 0)
            {
                c.m.talk.target = c.m.targetEnt;
                if (!(c.m.talk.said & kSaidDamageHeavy) && c.player.health <= 100 / 8)
                {
                    playSentence(c, kTalkPlHurt3, duration, kAttnIdle);
                    c.m.talk.said |= kSaidDamageHeavy;
                    return true;
                }
                if (!(c.m.talk.said & kSaidDamageMedium) && c.player.health <= 100 / 4)
                {
                    playSentence(c, kTalkPlHurt2, duration, kAttnIdle);
                    c.m.talk.said |= kSaidDamageMedium;
                    return true;
                }
                if (!(c.m.talk.said & kSaidDamageLight) && c.player.health <= 100 / 2)
                {
                    playSentence(c, kTalkPlHurt1, duration, kAttnIdle);
                    c.m.talk.said |= kSaidDamageLight;
                    return true;
                }
            }
            // A friend near to talk to: a question, which he answers.
            const std::int16_t buddy = findNearestFriend(c, false);
            AiMonster *other = buddy != kAiNone ? aiFind(c.a, buddy) : nullptr;
            if (buddy != kAiNone && (other == nullptr || other->movementGoal == kMoveGoalNone) &&
                aiRandomLong(c.a, 0, 99) < 75)
            {
                playSentence(c, questionGroup, duration, kAttnIdle);
                c.m.talk.target = buddy;
                setAnswerQuestion(c, buddy);
                if (AiMonster *answers = aiFind(c.a, buddy))
                    answers->talk.stopTalk = c.m.talk.stopTalk;
                ++c.m.talk.speak;
                return true;
            }
            // Else something to say, to the player.
            if (aiRandomLong(c.a, 0, 1))
            {
                const std::int16_t player = findNearestFriend(c, true);
                if (player != kAiNone)
                {
                    c.m.talk.target = player;
                    playSentence(c, idleGroup, duration, kAttnIdle);
                    ++c.m.talk.speak;
                    return true;
                }
            }
            talkFor(c, 0);
            c.ai.talkWait = 0;
            return false;
        }

        /// TrySmellTalk
        inline void trySmellTalk(AiCtx &c)
        {
            if (!okToSpeak(c))
                return;
            if (c.ai.now > c.m.talk.lastSmelled)
                c.m.talk.said &= static_cast<std::uint8_t>(~kSaidSmelled);
            if (!(c.m.talk.said & kSaidSmelled) && aiHas(c, kCondSmell))
            {
                playSentence(c, kTalkSmell, aiRandomMs(c.a, 2800, 3200), kAttnIdle);
                c.m.talk.lastSmelled = c.ai.now + 60000;
                c.m.talk.said |= kSaidSmelled;
            }
        }

        /// The friends of the map, each (EnumFriends: alive; `trace`: that
        /// can be seen from its feet to the top of theirs).
        template <class F>
        inline void eachFriend(AiCtx &c, bool trace, F &&visit)
        {
            for (int i = 0; i < c.a.count; ++i)
            {
                const ActorState &other = c.a.states[i];
                if (i == c.index || !other.active || !isTalker(other.kind) || !aiAlive(c.a, i))
                    continue;
                if (trace)
                {
                    const fx check[3] = {other.pos[0], other.pos[1], topOf(other)};
                    if (!lineClear(*c.w.world, c.me.pos, check))
                        continue;
                }
                visit(static_cast<std::int16_t>(i));
            }
        }

        /// AlertFriends: every friend in sight is provoked.
        inline void alertFriends(AiCtx &c)
        {
            eachFriend(c, true,
                       [&](std::int16_t id)
                       {
                           if (AiMonster *m = aiOf(c.a, id))
                               m->memory |= kMemoryProvoked;
                       });
        }

        /// ShutUpFriends (SentenceStop): what they were saying is over.
        inline void shutUpFriends(AiCtx &c)
        {
            eachFriend(c, true,
                       [&](std::int16_t id)
                       {
                           if (AiMonster *m = aiFind(c.a, id))
                               m->talk.stopTalk = c.ai.now;
                       });
        }

        inline void stopFollowing(AiCtx &c, bool clearSchedule);

        /// LimitFollowers: of those following the player, all but the
        /// first `most` stop.
        inline void limitFollowers(AiCtx &c, int most)
        {
            int count = 0;
            eachFriend(c, false,
                       [&](std::int16_t id)
                       {
                           AiMonster *m = nullptr;
                           const AiKind *kind = nullptr;
                           if (!friendCtx(c, id, m, kind) || m->targetEnt != kAiPlayer)
                               return;
                           if (++count > most)
                           {
                               AiCtx them{c.w, c.player, c.a, c.ai, id, c.a.states[id], *m, *kind};
                               stopFollowing(them, true);
                           }
                       });
        }

        /// StopFollowing
        inline void stopFollowing(AiCtx &c, bool clearSchedule)
        {
            if (!isFollowing(c))
                return;
            if (!aiHasMemory(c, kMemoryProvoked))
            {
                playSentence(c, kTalkUnuse, aiRandomMs(c.a, 2800, 3200), kAttnIdle);
                c.m.talk.target = c.m.targetEnt;
            }
            if (c.m.movementGoal == kMoveGoalTarget)
                aiRouteClear(c);
            c.m.targetEnt = kAiNone;
            if (c.me.ai < kAiDying)
                c.me.ai = 0; // (kPersonIdle)
            if (clearSchedule)
                aiClearSchedule(c);
            if (c.m.enemy != kAiNone)
                c.m.idealState = kStateCombat;
        }

        /// StartFollowing
        inline void startFollowing(AiCtx &c, std::int16_t leader)
        {
            if (c.me.script >= 0)
            {
                // A scene that was only holding him in its idle, never told
                // to play, is over with: what would start it later (c1a1b's
                // sitting scientist, his trigger a step past where he is
                // USEd) must not take the one now following back to it.
                const int script = c.me.script;
                const bool unplayed = script < c.w.recordCount && !c.w.states[script].running;
                detail::scriptCancel(c.w, c.player, script);
                if (unplayed && c.me.script < 0)
                    c.w.states[script].dead = true;
            }
            if (c.m.enemy != kAiNone)
                c.m.idealState = kStateAlert;
            c.m.targetEnt = leader;
            if (c.me.ai < kAiDying)
                c.me.ai = 1; // (kPersonFollow)
            playSentence(c, kTalkUse, aiRandomMs(c.a, 2800, 3200), kAttnIdle);
            c.m.talk.target = c.m.targetEnt;
            aiClear(c, kCondClientPush);
            aiClearSchedule(c);
        }

        /// CanFollow
        inline bool canFollow(AiCtx &c)
        {
            if (c.m.state == kStateScript || c.m.idealState == kStateScript || c.me.script >= 0)
            {
                if (c.me.script < 0 || !detail::scriptCanInterrupt(c.w, c.me.script))
                    return false;
            }
            if (!aiAlive(c.a, c.index))
                return false;
            return !isFollowing(c);
        }

        /// DeclineFollowing (a scientist's and a guard's own: "not now").
        inline void declineFollowing(AiCtx &c)
        {
            talkFor(c, 10000);
            c.m.talk.target = c.m.enemy;
            playSentence(c, kTalkDecline, 2000, kAttnNorm);
        }

        /// FollowerUse: the player USEs him.
        inline void followerUse(AiCtx &c)
        {
            if (c.m.talk.useTime > c.ai.now)
                return;
            if (preDisaster(c))
            {
                declineFollowing(c);
            }
            else if (canFollow(c))
            {
                limitFollowers(c, 1);
                if (!aiHasMemory(c, kMemoryProvoked))
                {
                    startFollowing(c, kAiPlayer);
                    c.m.talk.said |= kSaidHelloPlayer;
                }
            }
            else
            {
                stopFollowing(c, true);
            }
        }

        /// PlayScriptedSentence: a scripted_sentence's line.
        inline void playScriptedSentence(AiCtx &c, int sentence, int ms, int volume, int radius, bool concurrent,
                                         std::int16_t listener)
        {
            if (!concurrent)
                shutUpFriends(c);
            aiClear(c, kCondClientPush);
            c.m.talk.useTime = c.ai.now + ms;
            playSentenceLine(c, sentence, ms, radius, volume);
            c.m.talk.target = listener;
        }

        /// The end of StartMonster: one who followed the player (as a save
        /// from before, or the map before, has it) follows him still --
        /// not waiting to be triggered first.
        inline void started(AiCtx &c)
        {
            if (c.m.targetEnt == kAiPlayer)
                aiClearSchedule(c);
        }

        // ---------------------------------------- what CTalkMonster overrides

        /// SetActivity: standing while talking is its talking stance, if it
        /// has one.
        inline void setActivity(AiCtx &c, std::uint8_t activity)
        {
            if (activity == kActIdle && isTalking(c))
                activity = kActSignal3;
            if (activity == kActSignal3 && aiLookupActivity(c, kActSignal3) < 0)
                activity = kActIdle;
            aiSetActivityBase(c, activity);
        }

        /// IRelationship: the player who provoked it is hated.
        inline int relationship(AiCtx &c, std::int16_t target, int base)
        {
            if (target == kAiPlayer && aiHasMemory(c, kMemoryProvoked))
                return kRelHate;
            return base;
        }

        /// PrescheduleThink
        inline void prescheduleThink(AiCtx &c)
        {
            if (!aiHas(c, kCondSeeClient))
                aiSet(c, kCondClientUnseen);
        }

        /// TakeDamage (before the base's): a friend tells the player to stop.
        /// Whoever did it is kept (`other`), for Killed.
        inline void takeDamage(AiCtx &c, const AiDamage &d)
        {
            c.m.other = d.attacker;
            if (!aiAlive(c.a, c.index) || d.attacker != kAiPlayer || c.m.state == kStateProne)
                return;
            const std::int16_t buddy = findNearestFriend(c, false);
            AiMonster *m = nullptr;
            const AiKind *kind = nullptr;
            if (buddy != kAiNone && friendCtx(c, buddy, m, kind) && aiAlive(c.a, buddy) &&
                c.a.states[buddy].kind != static_cast<std::uint8_t>(ActorKind::Sitter))
            {
                AiCtx them{c.w, c.player, c.a, c.ai, buddy, c.a.states[buddy], *m, *kind};
                aiChangeSchedule(them, &slIdleStopShooting);
            }
        }

        /// Killed: killed by the player, the friends who see it turn on him.
        inline void killed(AiCtx &c)
        {
            if (c.m.other == kAiPlayer && c.m.state != kStateProne)
            {
                alertFriends(c);
                limitFollowers(c, 0);
            }
            c.m.targetEnt = kAiNone;
            c.m.talk.stopTalk = c.ai.now;
        }

        /// Touch: the player walks into it hard enough -- it is pushed.
        inline void touchTick(AiCtx &c)
        {
            if (c.player.health <= 0 || aiHasMemory(c, kMemoryProvoked) || isTalking(c))
                return;
            fx mins[3], maxs[3];
            targetBox(c.me, mins, maxs);
            if (!detail::playerTouchesBox(c.player, mins, maxs, fxFromInt(2)))
                return;
            const fx speed = (c.player.velocity[0] < 0 ? -c.player.velocity[0] : c.player.velocity[0]) +
                             (c.player.velocity[1] < 0 ? -c.player.velocity[1] : c.player.velocity[1]);
            if (speed > fxFromInt(50))
            {
                aiSet(c, kCondClientPush);
                if (c.m.state != kStateScript)
                    aiMakeIdealYaw(c, c.player.origin);
            }
        }

        /// Each frame: whether the player is pushing it (Touch).
        inline void frame(AiCtx &c, int) { touchTick(c); }

        /// The player's way of looking at `at` (UTIL_DotPoints with their
        /// forward): how straight at it they look.
        inline fx playerLooksAt(const AiCtx &c, const fx *at)
        {
            const fx d[3] = {at[0] - c.player.origin[0], at[1] - c.player.origin[1], 0};
            const fx length = fxLength(d);
            if (length <= 0)
                return kFxOne;
            return static_cast<fx>((static_cast<std::int64_t>(d[0]) * c.player.facingXY[0] +
                                    static_cast<std::int64_t>(d[1]) * c.player.facingXY[1]) /
                                   length);
        }

        /// GetScheduleOfType (CTalkMonster's).
        inline const AiSchedule *scheduleOfType(AiCtx &c, int type)
        {
            switch (type)
            {
            case kSchedMoveAway: return &slMoveAway;
            case kSchedMoveAwayFollow: return &slMoveAwayFollow;
            case kSchedMoveAwayFail: return &slMoveAwayFail;
            case kSchedTargetFace:
                // (Now and then, a word while following.)
                if (aiRandomLong(c.a, 0, 99) < 2)
                    return &slIdleSpeakWait;
                return &slIdleStand;
            case kSchedIdleStand:
            {
                if (!(c.m.talk.said & kSaidHelloPlayer))
                    return &slIdleHello;
                const int most = c.m.maxHealth;
                if (!(c.m.talk.said & kSaidWoundLight) && c.me.health * 4 <= most * 3)
                {
                    playSentence(c, kTalkWound, aiRandomMs(c.a, 2800, 3200), kAttnIdle);
                    c.m.talk.said |= kSaidWoundLight;
                    return &slIdleStand;
                }
                else if (!(c.m.talk.said & kSaidWoundHeavy) && c.me.health * 2 <= most)
                {
                    playSentence(c, kTalkMortal, aiRandomMs(c.a, 2800, 3200), kAttnIdle);
                    c.m.talk.said |= kSaidWoundHeavy;
                    return &slIdleStand;
                }
                if (okToSpeak(c) && aiRandomLong(c.a, 0, c.m.talk.speak * 2) == 0)
                    return &slIdleSpeak;
                if (!isTalking(c) && aiHas(c, kCondSeeClient) && aiRandomLong(c.a, 0, 6) == 0)
                {
                    // Watch the player -- stare back, if he is close and
                    // looking.
                    const fx d[3] = {c.player.origin[0] - c.me.pos[0], c.player.origin[1] - c.me.pos[1], 0};
                    if (fxLength(d) < fxFromInt(kStareDist) && playerLooksAt(c, c.me.pos) >= c.kind.fov)
                        return &slTlkIdleWatchClientStare;
                    return &slTlkIdleWatchClient;
                }
                if (isTalking(c))
                    return &slTlkIdleEyecontact;
                return &slIdleStand;
            }
            default: break;
            }
            return aiBaseScheduleOfType(c, type);
        }

        /// StartTask (CTalkMonster's). True if it was one of these.
        inline bool startTask(AiCtx &c, const AiTask &task)
        {
            switch (task.task)
            {
            case kTaskTlkSpeak:
                idleSpeak(c);
                aiTaskComplete(c);
                return true;
            case kTaskTlkRespond:
                idleRespond(c);
                aiTaskComplete(c);
                return true;
            case kTaskTlkHello:
                idleHello(c);
                aiTaskComplete(c);
                return true;
            case kTaskTlkStare:
                idleStare(c);
                aiTaskComplete(c);
                return true;
            case kTaskFacePlayer:
            case kTaskTlkLookAtClient:
            case kTaskTlkClientStare:
                c.m.waitFinished = c.ai.now + task.data;
                return true;
            case kTaskTlkEyeContact:
                return true;
            case kTaskTlkIdealYaw:
                if (c.m.talk.target != kAiNone)
                {
                    AiTarget t;
                    if (aiTargetOf(c.w, c.player, c.m.talk.target, t))
                    {
                        c.m.yawSpeed = 60;
                        const fx dx = t.origin[0] - c.me.pos[0], dy = t.origin[1] - c.me.pos[1];
                        const int yaw = static_cast<std::int16_t>(fxAtan2Turns(dy, dx) - c.me.yaw);
                        const int quarter = 65536 / 8; // 45 degrees
                        const int turn = yaw < 0 ? (yaw + quarter < 0 ? yaw + quarter : 0)
                                                 : (yaw - quarter > 0 ? yaw - quarter : 0);
                        c.m.idealYaw = static_cast<std::uint16_t>(c.me.yaw + turn);
                    }
                }
                aiTaskComplete(c);
                return true;
            case kTaskTlkHeadReset:
                c.m.talk.target = kAiNone;
                aiTaskComplete(c);
                return true;
            case kTaskTlkStopShooting:
                playSentence(c, kTalkNoShoot, aiRandomMs(c.a, 2800, 3200), kAttnNorm);
                aiTaskComplete(c);
                return true;
            case kTaskCantFollow:
                stopFollowing(c, false);
                playSentence(c, kTalkStop, aiRandomMs(c.a, 2000, 2500), kAttnNorm);
                aiTaskComplete(c);
                return true;
            case kTaskWalkPathForUnits:
                c.m.movementActivity = kActWalk;
                return true;
            case kTaskMoveAwayPath:
            {
                // Back the way it faces from: as far as it can walk, or any
                // cover near.
                fx forward[3];
                aiForward(static_cast<std::uint16_t>(c.m.idealYaw + 32768), forward);
                const fx to[3] = {c.me.pos[0] + forward[0] * task.data, c.me.pos[1] + forward[1] * task.data,
                                  c.me.pos[2]};
                if (walkClear(*c.w.world, &c.a, c.index, aiHull(c), c.me.pos, to, 8))
                {
                    aiMoveToLocation(c, kActWalk, 2000, to);
                    aiTaskComplete(c);
                }
                else if (aiFindCover(c, c.me.pos, c.kind.eye, 0, c.kind.coverRadius))
                {
                    c.m.moveWaitFinished = c.ai.now + 2000;
                    aiTaskComplete(c);
                }
                else
                {
                    aiTaskFail(c);
                }
                return true;
            }
            default:
                return false;
            }
        }

        /// RunTask (CTalkMonster's). True if it saw to it all.
        inline bool runTask(AiCtx &c, const AiTask &task)
        {
            switch (task.task)
            {
            case kTaskTlkClientStare:
            case kTaskTlkLookAtClient:
                if (c.m.state == kStateIdle && !aiIsMoving(c) && !isTalking(c))
                {
                    headTurn(c, c.player.origin);
                }
                else
                {
                    aiTaskFail(c);
                    return true;
                }
                if (task.task == kTaskTlkClientStare)
                {
                    const fx d[3] = {c.player.origin[0] - c.me.pos[0], c.player.origin[1] - c.me.pos[1], 0};
                    if (fxLength(d) > fxFromInt(kStareDist))
                        aiTaskFail(c);
                    if (playerLooksAt(c, c.me.pos) < c.kind.fov)
                        aiTaskFail(c);
                }
                if (c.ai.now > c.m.waitFinished)
                    aiTaskComplete(c);
                return true;
            case kTaskFacePlayer:
            {
                aiMakeIdealYaw(c, c.player.origin);
                aiChangeYaw(c);
                headTurn(c, c.player.origin);
                // (FlYawDiff() < 10: in degrees, signed, as shipped.)
                const int degrees = aiYawDiff(c) * 360 / 65536;
                if (c.ai.now > c.m.waitFinished && degrees < 10)
                    aiTaskComplete(c);
                return true;
            }
            case kTaskTlkEyeContact:
                if (!aiIsMoving(c) && isTalking(c) && c.m.talk.target != kAiNone)
                    headTurnTo(c, c.m.talk.target);
                else
                    aiTaskComplete(c);
                return true;
            case kTaskWalkPathForUnits:
                if (aiDist2D(c.m.lastPosition, c.me.pos) > fxFromInt(task.data) || aiMovementIsComplete(c))
                {
                    aiTaskComplete(c);
                    aiRouteClear(c);
                }
                return true;
            case kTaskWaitForMovement:
                if (isTalking(c) && c.m.talk.target != kAiNone)
                {
                    headTurnTo(c, c.m.talk.target);
                }
                else
                {
                    headReset(c);
                    // (Walking, it may greet the player, or say something.)
                    idleHello(c);
                    if (aiRandomLong(c.a, 0, c.m.talk.speak * 20) == 0)
                        idleSpeak(c);
                }
                aiBaseRunTask(c, task);
                if (aiTaskIsComplete(c))
                    headReset(c);
                return true;
            default:
                if (isTalking(c) && c.m.talk.target != kAiNone)
                    headTurnTo(c, c.m.talk.target);
                else
                    headReset(c);
                return false;
            }
        }
    } // namespace talk
} // namespace hldata
