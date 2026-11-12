#pragma once

// The game's monster mind, its working (HlAi.hpp has its numbers and its
// side table): what a monster sees and hears, its schedules and their
// tasks, its blows and its death, its squad -- dlls/monsters.cpp,
// monsterstate.cpp, schedule.cpp, combat.cpp and squadmonster.cpp, function
// by function, under the game's names.
//
// Included at the end of HlMonsters.hpp. std-only and inline: the host
// tests run it.

namespace hldata
{
    namespace detail
    {
        inline bool scriptCanInterrupt(const LogicWorld &w, int index);              // HlLogic.hpp
        inline void scriptCancel(LogicWorld &w, PlayerState &player, int index); // HlLogic.hpp
    } // namespace detail

    // ---------------------------------------------------- whoever it is about

    /// Something a monster may see, go for, or be hurt by: the player or an
    /// actor, as the game's entity would answer for it.
    struct AiTarget
    {
        std::int16_t id = kAiNone;
        bool valid = false;
        bool isPlayer = false;
        std::uint8_t kind = 0;
        std::uint8_t cls = kClassNone;
        fx origin[3] = {0, 0, 0}; ///< pev->origin: a player's middle, a monster's feet
        fx eye[3] = {0, 0, 0};    ///< EyePosition()
        fx center[3] = {0, 0, 0}; ///< Center()
        fx height = 0;            ///< pev->size.z
        fx half = 0;
        fx velocity[3] = {0, 0, 0};
        std::uint16_t yaw = 0;
        fx fov = fxDot(0.5);
        bool onGround = true;
        int health = 0;
        bool alive = false;     ///< IsAlive()
        bool notarget = false;  ///< FL_NOTARGET
        bool prisoner = false;  ///< SF_MONSTER_PRISONER
        bool monster = false;   ///< FL_MONSTER or FL_CLIENT: something Look() looks at
    };

    /// A kind's eye, above its feet (the models' own eye positions).
    inline fx aiEyeOf(std::uint8_t kind)
    {
        if (const AiKind *k = aiKindOf(kind))
            return k->eye;
        switch (static_cast<ActorKind>(kind))
        {
        case ActorKind::Scientist: return fxFromInt(60);
        case ActorKind::Sitter:    return fxFromInt(40);
        case ActorKind::Guard:     return fxFromInt(63);
        case ActorKind::Grunt:     return fxFromInt(73);
        case ActorKind::Player:    return fxFromInt(64);
        default:                   return fxFromInt(16);
        }
    }

    inline bool aiAlive(const ActorWorld &a, int index);

    inline bool aiTargetOf(LogicWorld &w, const PlayerState &player, std::int16_t id, AiTarget &t)
    {
        t = AiTarget();
        t.id = id;
        if (id == kAiPlayer)
        {
            t.valid = true;
            t.isPlayer = true;
            t.monster = true;
            t.cls = kClassPlayer;
            const fx tall = player.ducked ? kCrouchedFeet : kStandingFeet;
            for (int k = 0; k < 3; ++k)
            {
                t.origin[k] = player.origin[k];
                t.center[k] = player.origin[k];
                t.eye[k] = player.origin[k];
                t.velocity[k] = player.velocity[k];
            }
            t.eye[2] += player.viewHeight;
            t.height = tall * 2;
            t.half = fxFromInt(16);
            t.yaw = fxAtan2Turns(player.facingXY[1], player.facingXY[0]);
            t.onGround = player.onGround;
            t.health = player.health;
            t.alive = player.health > 0;
            t.notarget = unseen(player);
            return true;
        }
        if (w.actors == nullptr || id < 0 || id >= w.actors->count)
            return false;
        const ActorState &actor = w.actors->states[id];
        if (!actor.active)
            return false;
        t.valid = true;
        t.kind = actor.kind;
        t.monster = isMonster(actor.kind) || isGrunt(actor.kind) || isPerson(actor.kind) || isTurret(actor.kind) ||
                    actor.kind == static_cast<std::uint8_t>(ActorKind::Player);
        t.cls = kindClass(actor.kind);
        // (A turret that is off is nothing to anyone: the game's m_iOn.)
        if (isTurret(actor.kind) && actor.ai == kAiIdle)
            t.cls = kClassNone;
        t.prisoner = (actor.flags & kActorPrisoner) != 0;
        fx mins[3], maxs[3];
        targetBox(actor, mins, maxs);
        for (int k = 0; k < 3; ++k)
        {
            t.origin[k] = actor.pos[k];
            t.center[k] = (mins[k] + maxs[k]) / 2;
            t.eye[k] = actor.pos[k];
            t.velocity[k] = actor.velocity[k];
        }
        t.height = maxs[2] - mins[2];
        t.half = (maxs[0] - mins[0]) / 2;
        t.eye[2] += actor.kind == static_cast<std::uint8_t>(ActorKind::Barnacle) ? 0 : aiEyeOf(actor.kind);
        if (isTurret(actor.kind))
            t.eye[2] = t.center[2];
        t.yaw = actor.yaw;
        t.alive = aiAlive(*w.actors, id);
        t.onGround = !actor.airborne;
        if (const AiMonster *m = aiFind(*w.actors, id))
        {
            t.onGround = !m->inAir;
            if (const AiKind *k = aiKindOf(actor.kind))
                t.fov = k->fov;
        }
        t.health = actor.health;
        // (One of the people not yet thinking, as a save from before kept
        // them: what they have lost of their health.)
        if (isPerson(actor.kind) && actor.health <= 0)
        {
            const AiMonster *m = aiFind(*w.actors, id);
            if (m == nullptr || m->maxHealth == 0)
                t.health = personHealth(actor.kind) + actor.health;
        }
        return true;
    }

    /// IsAlive(): not dead or dying, and something left of it.
    inline bool aiAlive(const ActorWorld &a, int index)
    {
        const ActorState &actor = a.states[index];
        if (!actor.active || actor.ai >= kAiDying)
            return false;
        if (const AiMonster *m = aiFind(a, index))
            return m->deadflag == 0 && (actor.health > 0 || !m->started);
        return true;
    }

    /// The class it is now (Classify()).
    inline std::uint8_t aiClassify(const AiCtx &c)
    {
        return kindClass(c.me.kind);
    }

    /// How it feels about `target` (IRelationship).
    inline int aiRelationship(AiCtx &c, const AiTarget &target)
    {
        const int base = classRelationship(aiClassify(c), target.cls);
        return c.kind.relationship != nullptr ? c.kind.relationship(c, target.id, base) : base;
    }
    inline int aiRelationship(AiCtx &c, std::int16_t id)
    {
        AiTarget t;
        if (!aiTargetOf(c.w, c.player, id, t))
            return kRelNone;
        return aiRelationship(c, t);
    }

    // ------------------------------------------------------------ geometry

    inline fx aiDist2D(const fx *a, const fx *b)
    {
        const fx d[3] = {a[0] - b[0], a[1] - b[1], 0};
        return fxLength(d);
    }
    inline fx aiDist(const fx *a, const fx *b)
    {
        const fx d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
        return fxLength(d);
    }
    inline void aiForward(std::uint16_t yaw, fx *forward)
    {
        fx sine, cosine;
        fxSinCos(yaw, sine, cosine);
        forward[0] = cosine;
        forward[1] = sine;
        forward[2] = 0;
    }
    inline void aiRight(std::uint16_t yaw, fx *right)
    {
        fx sine, cosine;
        fxSinCos(yaw, sine, cosine);
        right[0] = sine;
        right[1] = -cosine;
        right[2] = 0;
    }
    /// The 2D dot of the way from `from` to `to` with `yaw`'s forward (Q12).
    inline fx aiDot2D(std::uint16_t yaw, const fx *from, const fx *to)
    {
        const fx d[3] = {to[0] - from[0], to[1] - from[1], 0};
        const fx length = fxLength(d);
        if (length <= 0)
            return 0;
        fx sine, cosine;
        fxSinCos(yaw, sine, cosine);
        return static_cast<fx>((static_cast<std::int64_t>(d[0]) * cosine + static_cast<std::int64_t>(d[1]) * sine) /
                               length);
    }
    inline void aiEye(const AiCtx &c, fx *eye)
    {
        eye[0] = c.me.pos[0];
        eye[1] = c.me.pos[1];
        eye[2] = c.me.pos[2] + c.kind.eye;
    }
    /// Center(): the middle of its box.
    inline void aiCenter(const AiCtx &c, fx *center)
    {
        fx mins[3], maxs[3];
        targetBox(c.me, mins, maxs);
        for (int k = 0; k < 3; ++k)
            center[k] = (mins[k] + maxs[k]) / 2;
        if (c.kind.center != 0)
            center[2] = c.me.pos[2] + c.kind.center;
    }

    /// FInViewCone: whether `at` is within its field of view (2D).
    inline bool aiInViewCone(const AiCtx &c, const fx *at)
    {
        return aiDot2D(c.me.yaw, c.me.pos, at) > c.kind.fov;
    }
    /// FVisible: a line from its eyes to `to` that nothing of the world
    /// stops.
    inline bool aiVisible(const AiCtx &c, const fx *to)
    {
        fx eye[3];
        aiEye(c, eye);
        return lineClear(*c.w.world, eye, to);
    }
    inline bool aiVisible(const AiCtx &c, const AiTarget &t)
    {
        if (t.notarget)
            return false;
        // (By the map's own visibility first: what is not in the same
        // sight-set of leaves cannot be in a line of sight.)
        if (c.w.world->vis != nullptr)
        {
            // (Where their eyes are: the player's and a monster's own, as
            // worked out once this frame, or this think; else now.)
            int theirs = -1;
            if (t.isPlayer)
                theirs = c.ai.playerEyeLeaf;
            else if (const AiMonster *m = aiFind(c.a, t.id); m != nullptr && m->started)
                theirs = m->leaf;
            if (theirs < 0)
                theirs = worldLeaf(*c.w.world, t.eye);
            if (!leafSees(*c.w.world, c.m.leaf, theirs))
                return false;
        }
        return aiVisible(c, t.eye);
    }

    /// MakeIdealYaw
    inline void aiMakeIdealYaw(AiCtx &c, const fx *target)
    {
        const fx dx = target[0] - c.me.pos[0], dy = target[1] - c.me.pos[1];
        if (dx == 0 && dy == 0)
        {
            c.m.idealYaw = c.me.yaw;
            return;
        }
        c.m.idealYaw = fxAtan2Turns(dy, dx);
    }
    /// FlYawDiff, in 65536ths of a turn.
    inline int aiYawDiff(const AiCtx &c)
    {
        return static_cast<std::int16_t>(c.m.idealYaw - c.me.yaw);
    }
    /// FacingIdeal
    inline bool aiFacingIdeal(const AiCtx &c)
    {
        const int d = aiYawDiff(c);
        return d <= 1 && d >= -1;
    }
    /// ChangeYaw: it turns, at its yaw speed, until the next think (done a
    /// frame at a time: aiTurnTick).
    inline void aiChangeYaw(AiCtx &c)
    {
        c.m.turning = true;
    }
    inline void aiTurnTick(AiCtx &c, int ms)
    {
        if (!c.m.turning)
            return;
        const int diff = aiYawDiff(c);
        if (diff == 0)
            return;
        // (The game's monsteryawspeedfix: yaw_speed * 2 degrees a second.)
        const int most = static_cast<int>((static_cast<std::int64_t>(c.m.yawSpeed) * 2 * 65536 * ms) / (360 * 1000));
        const int turn = diff > most ? most : (diff < -most ? -most : diff);
        c.me.yaw = static_cast<std::uint16_t>(c.me.yaw + turn);
    }

    // ------------------------------------------------------------ the clips


    /// Whether the model says what its clips are for.
    inline bool aiModelHasActivities(const ModelView &model)
    {
        for (int i = 0; i < model.header->clipCount; ++i)
        {
            if (clipActivity(model.clips[i]) != kActReset)
                return true;
        }
        return false;
    }

    /// LookupActivity: a clip for `activity`, chosen by the weights among
    /// those for it; -1 if none. `heaviest`: LookupActivityHeaviest.
    inline int aiLookupActivity(AiCtx &c, std::uint8_t activity, bool heaviest = false)
    {
        const ModelView *model = actorModel(c.a, c.me);
        if (model == nullptr || activity == kActReset)
            return -1;
        if (aiModelHasActivities(*model))
        {
            int total = 0, found = -1, best = -1;
            for (int i = 0; i < model->header->clipCount; ++i)
            {
                const ModelClip &clip = model->clips[i];
                if (clipActivity(clip) != activity)
                    continue;
                const int weight = clipWeight(clip);
                if (heaviest)
                {
                    if (weight > best)
                    {
                        best = weight;
                        found = i;
                    }
                    continue;
                }
                total += weight;
                if (total == 0 || aiRandomLong(c.a, 0, total - 1) < weight)
                    found = i;
            }
            return found;
        }
        // (Cooked before: by the kind's own names.)
        for (const AiClipName *n = c.kind.clipNames; n != nullptr && n->activity != kActReset; ++n)
        {
            if (n->activity == activity)
                return model->findClip(nameHash(n->clip));
        }
        return -1;
    }

    /// The clip `index`, from its start if `fromStart` (ResetSequenceInfo).
    inline void aiPlayClip(AiCtx &c, int index, bool fromStart)
    {
        const ModelView *model = actorModel(c.a, c.me);
        if (model == nullptr || index < 0)
            return;
        const bool loops = (model->clips[index].flags & kClipLoops) != 0;
        if (fromStart)
            c.me.frame = 0;
        c.me.clip = static_cast<std::int16_t>(index);
        c.me.once = !loops;
        c.me.finished = false;
        c.me.frozen = false;
        c.me.lastFrame = c.me.frame;
        c.m.seqFinished = false;
        // (m_flGroundSpeed: how fast its own stride carries it.)
        const ModelClip &clip = model->clips[index];
        const fx stride = fxLength(clip.travel);
        c.m.groundSpeed = stride > 0 && clip.frameCount >= 2
                              ? static_cast<fx>((static_cast<std::int64_t>(stride) * clip.fps) / ((clip.frameCount - 1) * 256))
                              : 0;
    }

    inline void aiSetYawSpeed(AiCtx &c)
    {
        c.m.yawSpeed = c.kind.yawSpeed != nullptr ? c.kind.yawSpeed(c) : 90;
    }

    /// CBaseMonster::SetActivity
    inline void aiSetActivityBase(AiCtx &c, std::uint8_t activity)
    {
        const int clip = aiLookupActivity(c, activity);
        const std::uint8_t old = c.m.activity;
        c.m.activity = activity;
        c.m.idealActivity = activity;
        if (clip >= 0)
        {
            // (From its start -- unless only going from a walk to a run, or
            // back, or on with the same clip that goes round.)
            const ModelView *model = actorModel(c.a, c.me);
            const bool loops = c.me.clip >= 0 && (model->clips[c.me.clip].flags & kClipLoops) != 0;
            const bool moving = (old == kActWalk || old == kActRun) && (activity == kActWalk || activity == kActRun);
            const bool restart = (c.me.clip != clip || !loops) && !moving;
            aiPlayClip(c, clip, restart);
            aiSetYawSpeed(c);
        }
    }
    inline void aiSetActivity(AiCtx &c, std::uint8_t activity)
    {
        if (c.kind.setActivity != nullptr)
            c.kind.setActivity(c, activity);
        else
            aiSetActivityBase(c, activity);
    }
    /// A clip by its name (SetSequenceByName).
    inline void aiSetSequenceByName(AiCtx &c, const char *name)
    {
        const ModelView *model = actorModel(c.a, c.me);
        if (model == nullptr)
            return;
        aiPlayClip(c, model->findClip(nameHash(name)), true);
    }

    inline void aiSound(AiCtx &c, int which)
    {
        if (c.kind.sound != nullptr)
            c.kind.sound(c, which);
    }

    // ------------------------------------------------- conditions, memory

    inline bool aiHas(const AiCtx &c, std::uint32_t conditions) { return (c.m.conditions & conditions) != 0; }
    inline bool aiHasAll(const AiCtx &c, std::uint32_t conditions) { return (c.m.conditions & conditions) == conditions; }
    inline void aiSet(AiCtx &c, std::uint32_t conditions) { c.m.conditions |= conditions; }
    inline void aiClear(AiCtx &c, std::uint32_t conditions) { c.m.conditions &= ~conditions; }
    inline bool aiHasMemory(const AiCtx &c, std::uint32_t memory) { return (c.m.memory & memory) != 0; }
    inline void aiRemember(AiCtx &c, std::uint32_t memory) { c.m.memory |= memory; }
    inline void aiForgetMemory(AiCtx &c, std::uint32_t memory) { c.m.memory &= ~memory; }

    inline void aiTaskComplete(AiCtx &c)
    {
        if (c.m.schedule != nullptr)
            c.m.taskStatus = kTaskStatusComplete;
    }
    inline void aiTaskFail(AiCtx &c) { aiSet(c, kCondTaskFailed); }
    inline bool aiTaskIsComplete(const AiCtx &c) { return c.m.taskStatus == kTaskStatusComplete; }
    inline bool aiTaskIsRunning(const AiCtx &c)
    {
        return c.m.taskStatus != kTaskStatusComplete && c.m.taskStatus != kTaskStatusRunningMovement;
    }
    inline bool aiMovementIsComplete(const AiCtx &c) { return c.m.movementGoal == kMoveGoalNone; }
    inline bool aiIsMoving(const AiCtx &c) { return c.m.movementGoal != kMoveGoalNone; }

    // ------------------------------------------------------------ moving

    /// RouteClear
    inline void aiRouteClear(AiCtx &c)
    {
        c.m.movementGoal = kMoveGoalNone;
        c.m.movementActivity = kActIdle;
        c.m.round.detouring = c.m.round.asking = false;
        aiForgetMemory(c, kMemoryMoveFailed);
    }
    inline bool aiRouteClearNow(const AiCtx &c) { return c.m.movementGoal == kMoveGoalNone; }

    /// A new way to go: the walker starts its plan afresh.
    inline void aiRouteNew(AiCtx &c, std::uint8_t goal, const fx *to)
    {
        c.m.movementGoal = goal;
        if (to != nullptr)
        {
            for (int k = 0; k < 3; ++k)
                c.m.moveGoal[k] = to[k];
        }
        c.m.moveBlockedMs = 0;
        c.m.moveAsked = false;
        c.m.round.detouring = c.m.round.asking = false;
        c.me.thinkMs = 0;
        c.me.hasGoal = false;
        c.me.goalNode = c.me.fromNode = -1;
        c.me.stuckMs = 0;
    }

    /// Where it is walking to just now: a place, or its enemy's feet.
    inline bool aiTargetOf(LogicWorld &w, const PlayerState &player, std::int16_t id, AiTarget &t);

    /// Where something's feet are (a player's origin is their middle).
    inline void aiFeetOf(const AiTarget &t, fx *feet)
    {
        for (int k = 0; k < 3; ++k)
            feet[k] = t.origin[k];
        if (t.isPlayer)
            feet[2] -= t.height / 2;
    }

    inline void aiMoveDest(const AiCtx &c, fx *dest)
    {
        if (c.m.movementGoal == kMoveGoalTarget)
        {
            AiTarget t;
            if (aiTargetOf(c.w, c.player, c.m.targetEnt, t))
            {
                aiFeetOf(t, dest);
                return;
            }
        }
        if (c.m.movementGoal == kMoveGoalEnemy)
        {
            for (int k = 0; k < 3; ++k)
                dest[k] = c.m.enemyLkp[k];
            // (A player's origin is their middle.)
            if (c.m.enemy == kAiPlayer)
                dest[2] -= c.player.ducked ? kCrouchedFeet : kStandingFeet;
            return;
        }
        for (int k = 0; k < 3; ++k)
            dest[k] = c.m.moveGoal[k];
    }

    /// MovementComplete
    inline void aiMovementComplete(AiCtx &c)
    {
        switch (c.m.taskStatus)
        {
        case kTaskStatusNew:
        case kTaskStatusRunning:
            c.m.taskStatus = kTaskStatusRunningTask;
            break;
        case kTaskStatusRunningMovement:
            aiTaskComplete(c);
            break;
        default:
            break;
        }
        c.m.movementGoal = kMoveGoalNone;
    }

    /// Close enough to `t` to count as there: what is held up by it this
    /// near has arrived.
    inline bool aiTargetInReach(const AiCtx &c, const AiTarget &t)
    {
        return aiDist2D(t.origin, c.me.pos) < monsterKind(c.me.kind).half + t.half + fxFromInt(24);
    }

    /// Stop: no more walking; the clip of standing.
    inline void aiStop(AiCtx &c)
    {
        c.m.idealActivity = c.kind.stoppedActivity != nullptr ? c.kind.stoppedActivity(c) : kActIdle;
    }

    inline bool aiMoveFrom(AiCtx &c, std::uint8_t activity, const fx *goal)
    {
        c.m.movementActivity = activity;
        aiRouteNew(c, kMoveGoalLocation, goal);
        return true;
    }
    /// MoveToLocation
    inline bool aiMoveToLocation(AiCtx &c, std::uint8_t activity, int waitMs, const fx *goal)
    {
        c.m.round.waitMs = waitMs;
        return aiMoveFrom(c, activity, goal);
    }

    // ------------------------------------------------------------- senses

    /// Look: what it can see of what it has feelings about, within
    /// `distance` (m_flDistLook), sets SEE_* (and remembers what it saw for
    /// BestVisibleEnemy).
    inline void aiLook(AiCtx &c, int distance)
    {
        aiClear(c, kCondSeeHate | kCondSeeDislike | kCondSeeEnemy | kCondSeeFear | kCondSeeNemesis | kCondSeeClient);
        c.m.seenCount = 0;
        if (c.me.flags & kActorPrisoner)
            return;
        std::uint32_t sighted = 0;
        const fx reach = fxFromInt(distance);
        auto consider = [&](std::int16_t id)
        {
            if (id == c.index)
                return;
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, id, t) || !t.monster || t.prisoner || !t.alive)
                return;
            for (int k = 0; k < 3; ++k)
            {
                const fx d = t.origin[k] - c.me.pos[k];
                if (d > reach || d < -reach)
                    return;
            }
            const int rel = aiRelationship(c, t);
            // (An ally other than the player is nothing Look sets a condition
            // for, nor anything BestVisibleEnemy would take: not looked for --
            // a line of sight the less, and no place in the list taken.)
            if (rel == kRelNone || (rel == kRelAlly && !t.isPlayer) || !aiInViewCone(c, t.origin) || t.notarget ||
                !aiVisible(c, t))
                return;
            if (t.isPlayer)
            {
                if (c.me.flags & kActorWaitSeen)
                {
                    // (Until the player has it in view: then it is free to.)
                    if (aiDot2D(t.yaw, t.origin, c.me.pos) <= t.fov)
                        return;
                    c.me.flags &= static_cast<std::uint8_t>(~kActorWaitSeen);
                }
                sighted |= kCondSeeClient;
            }
            if (c.m.seenCount < kAiSeen)
                c.m.seen[c.m.seenCount++] = id;
            if (id == c.m.enemy)
                sighted |= kCondSeeEnemy;
            switch (rel)
            {
            case kRelNemesis: sighted |= kCondSeeNemesis; break;
            case kRelHate:    sighted |= kCondSeeHate; break;
            case kRelDislike: sighted |= kCondSeeDislike; break;
            case kRelFear:    sighted |= kCondSeeFear; break;
            default:          break;
            }
        };
        consider(kAiPlayer);
        for (int i = 0; i < c.a.count; ++i)
        {
            // (What is cheap to rule out first: what is not there, not
            // something that is looked at, or out of reach.)
            const ActorState &other = c.a.states[i];
            if (!other.active || other.ai >= kAiDying || i == c.index ||
                !(isMonster(other.kind) || isGrunt(other.kind) || isPerson(other.kind) || isTurret(other.kind) ||
                  other.kind == static_cast<std::uint8_t>(ActorKind::Player)))
                continue;
            const fx dx = other.pos[0] - c.me.pos[0], dy = other.pos[1] - c.me.pos[1];
            if (dx > reach || dx < -reach || dy > reach || dy < -reach)
                continue;
            // (Nothing it has feelings about by the table -- which a kind
            // only ever makes less, but for the player: not looked at.)
            const int table = classRelationship(aiClassify(c), kindClass(other.kind));
            if (table == kRelNone || table == kRelAlly)
                continue;
            consider(static_cast<std::int16_t>(i));
        }
        aiSet(c, sighted);
    }

    /// ISoundMask, and what the schedule cares for.
    inline void aiListen(AiCtx &c)
    {
        c.m.audibleCount = 0;
        aiClear(c, kCondHearSound | kCondSmell | kCondSmellFood);
        c.m.soundTypes = 0;
        std::uint8_t mine = c.kind.soundMask;
        if (c.m.schedule != nullptr)
            mine &= c.m.schedule->soundMask;
        fx ear[3];
        aiEye(c, ear);
        const SoundEnt &sounds = c.ai.sounds;
        for (std::int8_t i = sounds.active; i != kSoundListEmpty; i = sounds.pool[i].next)
        {
            const AiSound &sound = sounds.pool[i];
            if (!(sound.type & mine) || aiDist(sound.origin, ear) > fxFromInt(sound.volume))
                continue;
            if (sound.type & kSoundHeard)
                aiSet(c, kCondHearSound);
            else if (sound.type & (kSoundMeat | kSoundCarcass))
                aiSet(c, kCondSmellFood | kCondSmell);
            else
                aiSet(c, kCondSmell);
            c.m.soundTypes |= sound.type;
            if (c.m.audibleCount < kAiAudible)
                c.m.audible[c.m.audibleCount++] = i;
        }
    }

    /// PBestSound: the nearest sound heard (not smelt); null if none.
    inline const AiSound *aiBestSound(AiCtx &c)
    {
        fx ear[3];
        aiEye(c, ear);
        const AiSound *best = nullptr;
        fx bestDist = fxFromInt(8192);
        // (The game's audible list is built newest last: walked back.)
        for (int n = c.m.audibleCount - 1; n >= 0; --n)
        {
            const AiSound &sound = c.ai.sounds.pool[c.m.audible[n]];
            if (!(sound.type & kSoundHeard))
                continue;
            const fx d = aiDist(sound.origin, ear);
            if (d < bestDist)
            {
                best = &sound;
                bestDist = d;
            }
        }
        return best;
    }
    /// PBestScent
    inline const AiSound *aiBestScent(AiCtx &c)
    {
        const AiSound *best = nullptr;
        fx bestDist = fxFromInt(8192);
        for (int n = c.m.audibleCount - 1; n >= 0; --n)
        {
            const AiSound &sound = c.ai.sounds.pool[c.m.audible[n]];
            if (!(sound.type & kSoundScent))
                continue;
            const fx d = aiDist(sound.origin, c.me.pos);
            if (d < bestDist)
            {
                best = &sound;
                bestDist = d;
            }
        }
        return best;
    }
    /// FLSoundVolume
    inline int aiSoundVolume(const AiCtx &c, const AiSound &sound)
    {
        return sound.volume - fxToInt(aiDist(sound.origin, c.me.pos));
    }

    inline bool aiEnemyAlive(AiCtx &c, std::int16_t id)
    {
        AiTarget t;
        return aiTargetOf(c.w, c.player, id, t) && t.alive;
    }

    /// PushEnemy
    inline void aiPushEnemy(AiCtx &c, std::int16_t enemy, const fx *lkp)
    {
        if (enemy == kAiNone)
            return;
        int i = 0;
        for (; i < kAiOldEnemies; ++i)
        {
            if (c.m.oldEnemy[i] == enemy)
                return;
            if (c.m.oldEnemy[i] == kAiNone)
                break;
        }
        if (i >= kAiOldEnemies)
            return;
        c.m.oldEnemy[i] = enemy;
        for (int k = 0; k < 3; ++k)
            c.m.oldEnemyAt[i][k] = lkp[k];
    }
    /// PopEnemy
    inline bool aiPopEnemy(AiCtx &c)
    {
        for (int i = kAiOldEnemies - 1; i >= 0; --i)
        {
            if (c.m.oldEnemy[i] == kAiNone)
                continue;
            if (aiEnemyAlive(c, c.m.oldEnemy[i]))
            {
                c.m.enemy = c.m.oldEnemy[i];
                for (int k = 0; k < 3; ++k)
                    c.m.enemyLkp[k] = c.m.oldEnemyAt[i][k];
                return true;
            }
            c.m.oldEnemy[i] = kAiNone;
        }
        return false;
    }

    /// BestVisibleEnemy: of what it saw, what it feels most strongly
    /// about, the nearest of those.
    inline std::int16_t aiBestVisibleEnemy(AiCtx &c)
    {
        int nearest = 8192, bestRel = kRelNone;
        std::int16_t found = kAiNone;
        for (int n = 0; n < c.m.seenCount; ++n)
        {
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.seen[n], t) || !t.alive)
                continue;
            const int rel = aiRelationship(c, t);
            const int dist = fxToInt(aiDist(t.origin, c.me.pos));
            if (rel > bestRel)
            {
                bestRel = rel;
                nearest = dist;
                found = t.id;
            }
            else if (rel == bestRel && found != kAiNone && dist <= nearest)
            {
                nearest = dist;
                found = t.id;
            }
        }
        return found;
    }

    /// GetEnemy
    inline bool aiGetEnemy(AiCtx &c)
    {
        if (aiHas(c, kCondSeeHate | kCondSeeDislike | kCondSeeNemesis))
        {
            const std::int16_t fresh = aiBestVisibleEnemy(c);
            if (fresh != c.m.enemy && fresh != kAiNone && c.m.schedule != nullptr &&
                (c.m.schedule->interruptMask & kCondNewEnemy))
            {
                aiPushEnemy(c, c.m.enemy, c.m.enemyLkp);
                aiSet(c, kCondNewEnemy);
                c.m.enemy = fresh;
                AiTarget t;
                if (aiTargetOf(c.w, c.player, fresh, t))
                {
                    for (int k = 0; k < 3; ++k)
                        c.m.enemyLkp[k] = t.origin[k];
                }
            }
        }
        if (c.m.enemy == kAiNone && aiPopEnemy(c))
        {
            if (c.m.schedule != nullptr && (c.m.schedule->interruptMask & kCondNewEnemy))
                aiSet(c, kCondNewEnemy);
        }
        return c.m.enemy != kAiNone;
    }

    /// The attack checks, as CBaseMonster has them.
    inline bool aiBaseCheckRange1(AiCtx &, fx dot, fx dist)
    {
        return dist > fxFromInt(64) && dist <= fxFromInt(784) && dot >= fxDot(0.5);
    }
    inline bool aiBaseCheckRange2(AiCtx &, fx dot, fx dist)
    {
        return dist > fxFromInt(64) && dist <= fxFromInt(512) && dot >= fxDot(0.5);
    }
    inline bool aiBaseCheckMelee1(AiCtx &c, fx dot, fx dist)
    {
        AiTarget t;
        return dist <= fxFromInt(64) && dot >= fxDot(0.7) && aiTargetOf(c.w, c.player, c.m.enemy, t) && t.onGround;
    }
    inline bool aiBaseCheckMelee2(AiCtx &, fx dot, fx dist)
    {
        return dist <= fxFromInt(64) && dot >= fxDot(0.7);
    }

    /// CheckAttacks
    inline void aiCheckAttacks(AiCtx &c, const AiTarget &target, fx dist)
    {
        const fx dot = aiDot2D(c.me.yaw, c.me.pos, target.origin);
        aiClear(c, kCondCanAttack);
        if ((c.m.capability & kCapRangeAttack1) &&
            (c.kind.checkRange1 != nullptr ? c.kind.checkRange1(c, dot, dist) : aiBaseCheckRange1(c, dot, dist)))
            aiSet(c, kCondCanRangeAttack1);
        if ((c.m.capability & kCapRangeAttack2) &&
            (c.kind.checkRange2 != nullptr ? c.kind.checkRange2(c, dot, dist) : aiBaseCheckRange2(c, dot, dist)))
            aiSet(c, kCondCanRangeAttack2);
        if ((c.m.capability & kCapMeleeAttack1) &&
            (c.kind.checkMelee1 != nullptr ? c.kind.checkMelee1(c, dot, dist) : aiBaseCheckMelee1(c, dot, dist)))
            aiSet(c, kCondCanMeleeAttack1);
        if ((c.m.capability & kCapMeleeAttack2) &&
            (c.kind.checkMelee2 != nullptr ? c.kind.checkMelee2(c, dot, dist) : aiBaseCheckMelee2(c, dot, dist)))
            aiSet(c, kCondCanMeleeAttack2);
    }

    /// CheckEnemy: where it is, whether it is still there, whether it can be
    /// attacked from here. True if the last known place was brought up to
    /// date.
    inline bool aiCheckEnemyBase(AiCtx &c)
    {
        bool updated = false;
        aiClear(c, kCondEnemyFacingMe);
        AiTarget t;
        if (!aiTargetOf(c.w, c.player, c.m.enemy, t))
        {
            aiSet(c, kCondEnemyDead);
            aiClear(c, kCondSeeEnemy | kCondEnemyOccluded);
            return false;
        }
        if (!aiVisible(c, t))
            aiSet(c, kCondEnemyOccluded);
        else
            aiClear(c, kCondEnemyOccluded);
        if (!t.alive)
        {
            aiSet(c, kCondEnemyDead);
            aiClear(c, kCondSeeEnemy | kCondEnemyOccluded);
            return false;
        }
        // (The nearest of its origin, its middle and its feet.)
        fx dist = aiDist(t.origin, c.me.pos);
        fx at[3] = {t.origin[0], t.origin[1], t.origin[2] + t.height / 2};
        fx other = aiDist(at, c.me.pos);
        if (other < dist)
            dist = other;
        else
        {
            at[2] -= t.height;
            other = aiDist(at, c.me.pos);
            if (other < dist)
                dist = other;
        }
        if (aiHas(c, kCondSeeEnemy))
        {
            updated = true;
            for (int k = 0; k < 3; ++k)
                c.m.enemyLkp[k] = t.origin[k];
            // (Whether it has us in its own view.)
            if (aiDot2D(t.yaw, t.origin, c.me.pos) > t.fov)
                aiSet(c, kCondEnemyFacingMe);
            else
                aiClear(c, kCondEnemyFacingMe);
            // (A little ahead of where it is going.)
            const int lead = aiRandomLong(c.a, 0, 50); // RANDOM_FLOAT(-0.05, 0), in thousandths
            for (int k = 0; k < 3; ++k)
                c.m.enemyLkp[k] += static_cast<fx>((static_cast<std::int64_t>(t.velocity[k]) * lead) / 1000);
        }
        else if (!aiHas(c, kCondEnemyOccluded | kCondSeeEnemy) && dist <= fxFromInt(256))
        {
            updated = true;
            for (int k = 0; k < 3; ++k)
                c.m.enemyLkp[k] = t.origin[k];
        }
        if (dist >= fxFromInt(1024)) // m_flDistTooFar
            aiSet(c, kCondEnemyTooFar);
        else
            aiClear(c, kCondEnemyTooFar);
        const bool canCheck = c.kind.canCheckAttacks != nullptr ? c.kind.canCheckAttacks(c)
                                                                : aiHas(c, kCondSeeEnemy) && !aiHas(c, kCondEnemyTooFar);
        if (canCheck)
            aiCheckAttacks(c, t, dist);
        return updated;
    }

    // ------------------------------------------------------------- squads

    inline bool aiInSquad(const AiCtx &c) { return c.m.squadLeader != kAiNone; }
    inline bool aiIsLeader(const AiCtx &c) { return c.m.squadLeader == c.index; }
    inline AiMonster *aiSquadLeader(AiCtx &c)
    {
        if (c.m.squadLeader == kAiNone)
            return &c.m;
        AiMonster *leader = aiFind(c.a, c.m.squadLeader);
        return leader != nullptr ? leader : &c.m;
    }
    /// The leader's members and the leader itself (MySquadMember 0..4),
    /// as actor indices; kAiNone for an empty place.
    inline std::int16_t aiSquadMember(AiCtx &c, int i)
    {
        AiMonster *leader = aiSquadLeader(c);
        const std::int16_t id = i >= kMaxSquadMembers - 1 ? leader->actor : leader->squadMember[i];
        // (One taken out of the world is no longer anyone: the game's handle
        // to it is null.)
        if (id < 0 || id >= c.a.count || !c.a.states[id].active)
            return kAiNone;
        return id;
    }
    inline int aiSquadCount(AiCtx &c)
    {
        if (!aiInSquad(c))
            return 0;
        int n = 0;
        for (int i = 0; i < kMaxSquadMembers; ++i)
            n += aiSquadMember(c, i) != kAiNone ? 1 : 0;
        return n;
    }

    /// SquadEnemySplit
    inline bool aiSquadEnemySplit(AiCtx &c)
    {
        if (!aiInSquad(c))
            return false;
        AiMonster *leader = aiSquadLeader(c);
        const std::int16_t enemy = leader->enemy;
        for (int i = 0; i < kMaxSquadMembers; ++i)
        {
            const AiMonster *member = aiFind(c.a, aiSquadMember(c, i));
            if (member != nullptr && member->enemy != kAiNone && member->enemy != enemy)
                return true;
        }
        return false;
    }

    /// OccupySlot -- with the game's test, which lets nobody in when what is
    /// wanted is exactly what is taken.
    inline bool aiOccupySlot(AiCtx &c, std::uint16_t desired)
    {
        if (!aiInSquad(c))
            return true;
        if (aiSquadEnemySplit(c))
        {
            c.m.mySlot = kSlotSquadSplit;
            return true;
        }
        AiMonster *leader = aiSquadLeader(c);
        if (!(desired ^ leader->squadSlots))
            return false;
        for (int i = 0; i < kSquadSlotCount; ++i)
        {
            const std::uint16_t mask = static_cast<std::uint16_t>(1u << i);
            if ((desired & mask) && !(leader->squadSlots & mask))
            {
                leader->squadSlots |= mask;
                c.m.mySlot = mask;
                return true;
            }
        }
        return false;
    }
    /// VacateSlot
    inline void aiVacateSlot(AiCtx &c)
    {
        if (c.m.mySlot != kSlotNone && aiInSquad(c))
        {
            aiSquadLeader(c)->squadSlots &= static_cast<std::uint16_t>(~c.m.mySlot);
            c.m.mySlot = kSlotNone;
        }
    }

    /// SquadAdd
    inline bool aiSquadAdd(ActorWorld &a, int leaderIndex, int addIndex)
    {
        AiMonster *leader = aiFind(a, leaderIndex);
        AiMonster *add = aiFind(a, addIndex);
        if (leader == nullptr || add == nullptr)
            return false;
        for (std::int16_t &member : leader->squadMember)
        {
            if (member == kAiNone)
            {
                member = static_cast<std::int16_t>(addIndex);
                add->squadLeader = static_cast<std::int16_t>(leaderIndex);
                return true;
            }
        }
        return false;
    }

    /// The squad's name (the map's netname), or 0.
    inline std::uint32_t aiSquadName(const ActorWorld &a, int index)
    {
        return index < a.recordCount && a.records != nullptr ? a.records[index].item >> kActorSquadShift : 0u;
    }

    /// SquadRecruit
    inline int aiSquadRecruit(AiCtx &c, int searchRadius, int maxMembers)
    {
        if (aiInSquad(c) || maxMembers < 2)
            return 0;
        const std::uint8_t myClass = aiClassify(c);
        c.m.squadLeader = static_cast<std::int16_t>(c.index);
        int count = 1;
        const std::uint32_t name = aiSquadName(c.a, c.index);
        fx eye[3];
        aiEye(c, eye);
        for (int i = 0; i < c.a.count; ++i)
        {
            if (i == c.index)
                continue;
            const ActorState &other = c.a.states[i];
            if (!other.active || aiKindOf(other.kind) == nullptr)
                continue;
            AiMonster *recruit = aiOf(c.a, i);
            if (recruit == nullptr || recruit->squadLeader != kAiNone || kindClass(other.kind) != myClass)
                continue;
            if (name != 0)
            {
                if (aiSquadName(c.a, i) != name)
                    continue;
            }
            else
            {
                if (!aiAlive(c.a, i) || other.script >= 0 || aiSquadName(c.a, i) != 0)
                    continue;
                if (myClass == kClassAlienMonster && other.kind != c.me.kind)
                    continue;
                if (aiDist(other.pos, c.me.pos) > fxFromInt(searchRadius))
                    continue;
                const fx theirs[3] = {other.pos[0], other.pos[1], other.pos[2] + c.kind.eye};
                if (!lineClear(*c.w.world, eye, theirs))
                    continue;
            }
            if (!aiSquadAdd(c.a, c.index, i))
                break;
            ++count;
        }
        if (count == 1)
            c.m.squadLeader = kAiNone;
        return count;
    }

    /// SquadMakeEnemy
    inline void aiSquadMakeEnemy(AiCtx &c, std::int16_t enemy);

    /// SquadMemberInRange
    inline bool aiSquadMemberInRange(AiCtx &c, const fx *at, fx dist)
    {
        if (!aiInSquad(c))
            return false;
        for (int i = 0; i < kMaxSquadMembers; ++i)
        {
            const std::int16_t member = aiSquadMember(c, i);
            if (member != kAiNone && aiDist2D(at, c.a.states[member].pos) <= dist)
                return true;
        }
        return false;
    }
    /// FValidateCover (a squad member's: not where another of the squad is)
    inline bool aiValidateCover(AiCtx &c, const fx *at)
    {
        if (!c.kind.squadMonster || !aiInSquad(c))
            return true;
        return !aiSquadMemberInRange(c, at, fxFromInt(128));
    }

    // ------------------------------------------------------------- cover

    /// Every node's distance along the links from node `from` (0xFFFF: no
    /// way), for something of `size` that does or does not open doors (the
    /// game's PathLength, for all of them at once).
    inline void aiPathDistances(const ActorWorld &a, int from, std::uint8_t size, bool opens, std::uint16_t *far)
    {
        for (int n = 0; n < a.nodeCount; ++n)
            far[n] = 0xFFFF;
        if (from < 0 || from >= a.nodeCount || a.links == nullptr)
            return;
        std::uint8_t done[kMaxPathNodes] = {};
        far[from] = 0;
        int pause = 0;
        for (;;)
        {
            thinkPoint(pause, 24);
            int at = -1;
            for (int n = 0; n < a.nodeCount; ++n)
            {
                if (!done[n] && far[n] != 0xFFFF && (at < 0 || far[n] < far[at]))
                    at = n;
            }
            if (at < 0)
                return;
            done[at] = 1;
            const PathNode &node = a.nodes[at];
            for (int l = node.firstLink; l < node.firstLink + node.linkCount && l < a.linkCount; ++l)
            {
                const PathLink &link = a.links[l];
                if (!(link.flags & size) || done[link.to])
                    continue;
                const std::uint8_t shut = a.linkShut != nullptr ? a.linkShut[l] : kLinkOpen;
                if (shut == kLinkNoWay || (shut == kLinkDoor && !opens))
                    continue;
                const std::uint32_t through = far[at] + link.length + 1u;
                if (through < far[link.to])
                    far[link.to] = static_cast<std::uint16_t>(through > 0xFFFE ? 0xFFFE : through);
            }
        }
    }

    inline int aiHull(const AiCtx &c) { return monsterHull(c.me.kind); }

    /// FindCover: a node between `minDist` and `maxDist` off that the
    /// threat (at `threat`, with its eyes `viewOfs` above) has no line to,
    /// and that it is no further from along the ways than the threat is --
    /// the first such, searching from where the last search left off.
    inline bool aiFindCover(AiCtx &c, const fx *threat, fx viewOfs, int minDist, int maxDist)
    {
        if (maxDist == 0)
            maxDist = 784;
        if (minDist > maxDist / 2)
            minDist = maxDist / 2;
        const ActorWorld &a = c.a;
        if (a.nodeCount == 0)
            return false;
        const int hull = aiHull(c);
        const int mine = pathNearest(a, *c.w.world, c.me.pos, hull, c.index);
        if (mine < 0)
            return false;
        int theirs = pathNearest(a, *c.w.world, threat);
        if (theirs < 0)
            theirs = mine;
        const fx looker[3] = {threat[0], threat[1], threat[2] + viewOfs};
        std::uint16_t fromMe[kMaxPathNodes], fromThem[kMaxPathNodes];
        const std::uint8_t size = pathSizeOf(hull);
        const bool opens = opensDoors(c.me.kind);
        aiPathDistances(a, mine, size, opens, fromMe);
        if (theirs != mine)
            aiPathDistances(a, theirs, size, opens, fromThem);
        int pause = 0;
        for (int i = 0; i < a.nodeCount; ++i)
        {
            thinkPoint(pause, 8);
            const int n = (i + c.ai.lastCoverSearch) % a.nodeCount;
            c.ai.lastCoverSearch = n + 1;
            const fx *p = a.nodes[n].pos;
            const fx dist = aiDist(c.me.pos, p);
            if (dist < fxFromInt(minDist) || dist >= fxFromInt(maxDist))
                continue;
            const fx from[3] = {p[0], p[1], p[2] + viewOfs};
            if (lineClear(*c.w.world, from, looker))
                continue;
            if (fromMe[n] == 0xFFFF)
                continue;
            if (theirs != mine && fromMe[n] > fromThem[n])
                continue;
            if (!aiValidateCover(c, p))
                continue;
            aiMoveToLocation(c, kActRun, 0, p);
            return true;
        }
        return false;
    }

    /// FindLateralCover: up to five steps of 48 to either side, the first
    /// that the threat cannot see and can be walked to.
    inline bool aiFindLateralCover(AiCtx &c, const fx *threat, fx viewOfs)
    {
        fx right[3];
        aiRight(c.me.yaw, right);
        const fx looker[3] = {threat[0], threat[1], threat[2] + viewOfs};
        for (int i = 1; i <= 5; ++i)
        {
            for (int side = -1; side <= 1; side += 2)
            {
                const fx test[3] = {c.me.pos[0] + right[0] * 48 * i * side, c.me.pos[1] + right[1] * 48 * i * side,
                                    c.me.pos[2]};
                const fx eye[3] = {test[0], test[1], test[2] + c.kind.eye};
                if (lineClear(*c.w.world, looker, eye))
                    continue;
                if (!aiValidateCover(c, test) ||
                    !walkClear(*c.w.world, &c.a, c.index, aiHull(c), c.me.pos, test, i * 3 + 1))
                    continue;
                aiMoveToLocation(c, kActRun, 0, test);
                return true;
            }
        }
        return false;
    }

    /// BuildNearestRoute: a node from which the threat can be seen, between
    /// `minDist` and `maxDist` of it, that it can get to.
    inline bool aiBuildNearestRoute(AiCtx &c, const fx *threat, fx viewOfs, int minDist, int maxDist)
    {
        if (maxDist == 0)
            maxDist = 784;
        if (minDist > maxDist / 2)
            minDist = maxDist / 2;
        const ActorWorld &a = c.a;
        if (a.nodeCount == 0)
            return false;
        const int hull = aiHull(c);
        const int mine = pathNearest(a, *c.w.world, c.me.pos, hull, c.index);
        if (mine < 0)
            return false;
        std::uint8_t reached[kMaxPathNodes];
        pathReach(a, mine, pathSizeOf(hull), opensDoors(c.me.kind), reached);
        const fx looker[3] = {threat[0], threat[1], threat[2] + viewOfs};
        int pause = 0;
        for (int i = 0; i < a.nodeCount; ++i)
        {
            thinkPoint(pause, 8);
            const int n = (i + c.ai.lastCoverSearch) % a.nodeCount;
            c.ai.lastCoverSearch = n + 1;
            if (n == mine || !reached[n])
                continue;
            const fx *p = a.nodes[n].pos;
            const fx dist = aiDist(threat, p);
            if (dist <= fxFromInt(minDist) || dist >= fxFromInt(maxDist))
                continue;
            const fx from[3] = {p[0], p[1], p[2] + c.kind.eye};
            if (!lineClear(*c.w.world, from, looker))
                continue;
            aiMoveToLocation(c, c.m.movementActivity, 0, p);
            return true;
        }
        return false;
    }

    /// A route to a place: straight if that is a walk, else by the nodes
    /// (BuildRoute). False if there is no way at all. `waitMs`: for a
    /// MoveTo of the game's, how long it is to wait for something that
    /// moves out of its way (-1: BuildRoute, which leaves that as it was).
    inline bool aiBuildRoute(AiCtx &c, std::uint8_t goal, const fx *to, int waitMs = -1)
    {
        const fx d[3] = {to[0] - c.me.pos[0], to[1] - c.me.pos[1], 0};
        const fx far = fxLength(d);
        auto straight = [&]()
        { return far < fxFromInt(1024) && walkClear(*c.w.world, &c.a, c.index, aiHull(c), c.me.pos, to, 64); };
        auto byNodes = [&]()
        {
            if (c.a.nodeCount == 0)
                return false;
            const int hull = aiHull(c);
            const int mine = pathNearest(c.a, *c.w.world, c.me.pos, hull, c.index);
            const int theirs = pathNearest(c.a, *c.w.world, to);
            if (mine < 0 || theirs < 0)
                return false;
            return mine == theirs || pathStep(c.a, mine, theirs, pathSizeOf(hull), opensDoors(c.me.kind)) != kNoPathNode;
        };
        // (Either way will do. A straight walk is tried a step at a time --
        // up to sixty-four, each a trace, in the frame: a short one first,
        // a long one only if the ways between the nodes say no.)
        const bool ok = far < fxFromInt(256) ? (straight() || byNodes()) : (byNodes() || straight());
        if (!ok)
            return false;
        if (waitMs >= 0)
            c.m.round.waitMs = waitMs;
        aiRouteNew(c, goal, to);
        return true;
    }

    // ---------------------------------------------------- hurting others

    /// TakeDamage, for anything: the player through their armour; an actor
    /// by its own rules (monsterHurt). `from`: where the blow came from.
    inline void aiDamageTarget(AiCtx &c, std::int16_t id, int amount, std::uint32_t type, std::uint8_t group = kHitGeneric);

    /// CheckTraceHullAttack: a reach of `dist` straight ahead from its
    /// middle, a head's size across; the first thing in it is hurt by
    /// `damage` (none: only found). What was hit, or kAiNone.
    inline std::int16_t aiHullAttack(AiCtx &c, int dist, int damage, std::uint32_t type)
    {
        fx forward[3];
        aiForward(c.me.yaw, forward);
        fx mins[3], maxs[3];
        targetBox(c.me, mins, maxs);
        const fx start[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + (maxs[2] - mins[2]) / 2};
        const fx end[3] = {start[0] + forward[0] * dist, start[1] + forward[1] * dist, start[2]};
        // (What the world lets it reach.)
        const Shot wall = traceShot(*c.w.world, start, end);
        std::int64_t best = wall.fraction;
        std::int16_t hit = kAiNone;
        auto tryBox = [&](std::int16_t id, const fx *lo, const fx *hi)
        {
            // (The head hull: 16 out to the sides, 18 up and down.)
            const fx bl[3] = {lo[0] - fxFromInt(16), lo[1] - fxFromInt(16), lo[2] - fxFromInt(18)};
            const fx bh[3] = {hi[0] + fxFromInt(16), hi[1] + fxFromInt(16), hi[2] + fxFromInt(18)};
            std::int64_t enter = 0;
            if (lineEntersBox(start, end, bl, bh, enter) && enter <= best)
            {
                best = enter;
                hit = id;
            }
        };
        if (c.player.health > 0)
        {
            const fx tall = c.player.ducked ? kCrouchedFeet : kStandingFeet;
            const fx lo[3] = {c.player.origin[0] - fxFromInt(16), c.player.origin[1] - fxFromInt(16), c.player.origin[2] - tall};
            const fx hi[3] = {c.player.origin[0] + fxFromInt(16), c.player.origin[1] + fxFromInt(16), c.player.origin[2] + tall};
            tryBox(kAiPlayer, lo, hi);
        }
        for (int i = 0; i < c.a.count; ++i)
        {
            const ActorState &other = c.a.states[i];
            if (i == c.index || !other.active || !(other.flags & kActorSolid) || !canBeHurt(other.kind))
                continue;
            fx lo[3], hi[3];
            targetBox(other, lo, hi);
            tryBox(static_cast<std::int16_t>(i), lo, hi);
        }
        if (hit != kAiNone && damage > 0)
            aiDamageTarget(c, hit, damage, type);
        return hit;
    }

    /// A line from `src` along `dir` (unit, Q12) for `range` units that
    /// hits the first thing in its way, or the world. The end, and what was
    /// hit (kAiNone: the world, or nothing).
    ///
    /// With `group`, it is a bullet: an actor is struck only through its hit
    /// boxes (lineHitsHitbox), and the part struck is put there; a line that
    /// passes between them goes on, to what is behind. (The player is
    /// their box.)
    inline std::int16_t aiTraceLine(AiCtx &c, const fx *src, const fx *dir, int range, fx *end,
                                    int *group = nullptr)
    {
        if (group != nullptr)
            *group = kHitBody;
        const fx to[3] = {src[0] + dir[0] * range, src[1] + dir[1] * range, src[2] + dir[2] * range};
        const Shot wall = traceShot(*c.w.world, src, to);
        std::int64_t best = wall.fraction;
        std::int16_t hit = kAiNone;
        if (c.player.health > 0)
        {
            const fx tall = c.player.ducked ? kCrouchedFeet : kStandingFeet;
            const fx lo[3] = {c.player.origin[0] - fxFromInt(16), c.player.origin[1] - fxFromInt(16), c.player.origin[2] - tall};
            const fx hi[3] = {c.player.origin[0] + fxFromInt(16), c.player.origin[1] + fxFromInt(16), c.player.origin[2] + tall};
            std::int64_t enter = 0;
            if (lineEntersBox(src, to, lo, hi, enter) && enter < best)
            {
                best = enter;
                hit = kAiPlayer;
            }
        }
        for (int i = 0; i < c.a.count; ++i)
        {
            const ActorState &other = c.a.states[i];
            if (i == c.index || !other.active || !(other.flags & kActorSolid) || !canBeHurt(other.kind))
                continue;
            fx lo[3], hi[3];
            targetBox(other, lo, hi);
            std::int64_t enter = 0;
            if (!lineEntersBox(src, to, lo, hi, enter) || enter >= best)
                continue;
            int part = kHitBody;
            if (group != nullptr && !lineHitsHitbox(c.a, other, src, to, enter, part))
                continue;
            if (enter < best)
            {
                best = enter;
                hit = static_cast<std::int16_t>(i);
                if (group != nullptr)
                    *group = part;
            }
        }
        for (int k = 0; k < 3; ++k)
            end[k] = src[k] + static_cast<fx>((static_cast<std::int64_t>(to[k] - src[k]) * best) >> kFractionShift);
        return hit;
    }

    /// ShootAtEnemy: the way from `src` to where its enemy's body was last
    /// known to be (unit, Q12).
    inline void aiShootAtEnemy(AiCtx &c, const fx *src, fx *dir)
    {
        AiTarget t;
        if (!aiTargetOf(c.w, c.player, c.m.enemy, t))
        {
            aiForward(c.me.yaw, dir);
            return;
        }
        // (BodyTarget - origin + LKP: its middle, at where it is thought to be.)
        fx to[3];
        for (int k = 0; k < 3; ++k)
            to[k] = t.center[k] - t.origin[k] + c.m.enemyLkp[k] - src[k];
        const fx length = fxLength(to);
        for (int k = 0; k < 3; ++k)
            dir[k] = length > 0 ? static_cast<fx>((static_cast<std::int64_t>(to[k]) << kFxShift) / length) : 0;
    }

    /// SetBlending(0, pitch of `dir`): its clip, if it blends by the aim,
    /// drawn aimed along `dir` (unit, Q12) -- up or down at what it shoots.
    inline void aiSetBlending(AiCtx &c, const fx *dir)
    {
        const ModelView *model = actorModel(c.a, c.me);
        if (model == nullptr || c.me.clip < 0)
            return;
        const fx flat[3] = {dir[0], dir[1], 0};
        const auto pitch = static_cast<std::int16_t>(fxAtan2Turns(dir[2], fxLength(flat)));
        c.me.blend.value = static_cast<std::uint8_t>(clipBlend(model->clips[c.me.clip], pitch));
    }

    /// A spent casing out of a monster's gun (EjectBrass, as hgrunt.cpp and
    /// hassassin.cpp throw it): from `from`, out to its right, up, and a
    /// little forward or back, by the way it faces. `kind`: kPieceShell or
    /// kPieceShotShell.
    inline void aiEjectBrass(AiCtx &c, std::uint8_t kind, const fx *from)
    {
        fx forward[3];
        aiForward(c.me.yaw, forward);
        const fx right[2] = {forward[1], -forward[0]};
        const int out = aiRandomLong(c.a, 40, 90), up = aiRandomLong(c.a, 75, 200), ahead = aiRandomLong(c.a, -40, 40);
        const fx velocity[3] = {right[0] * out + forward[0] * ahead, right[1] * out + forward[1] * ahead,
                                fxFromInt(up)};
        shellEject(c.a, kind, from, velocity);
    }

    // --------------------------------------------------- the default AI

    inline const AiSchedule *aiScheduleOfType(AiCtx &c, int type);
    inline const AiSchedule *aiGetSchedule(AiCtx &c);
    inline void aiChangeSchedule(AiCtx &c, const AiSchedule *schedule);
} // namespace hldata

#include "HlAiSchedules.hpp"

namespace hldata
{
    /// ClearSchedule
    inline void aiClearSchedule(AiCtx &c)
    {
        c.m.taskStatus = kTaskStatusNew;
        c.m.schedule = nullptr;
        c.m.scheduleIndex = 0;
    }

    /// ChangeSchedule
    inline void aiChangeSchedule(AiCtx &c, const AiSchedule *schedule)
    {
        c.m.schedule = schedule;
        c.m.scheduleIndex = 0;
        c.m.taskStatus = kTaskStatusNew;
        c.m.conditions = 0;
        c.m.failSchedule = kSchedNone;
    }

    inline const AiTask *aiGetTask(const AiCtx &c)
    {
        if (c.m.schedule == nullptr || c.m.scheduleIndex >= c.m.schedule->count)
            return nullptr;
        return &c.m.schedule->tasks[c.m.scheduleIndex];
    }

    /// NextScheduledTask
    inline void aiNextScheduledTask(AiCtx &c)
    {
        c.m.taskStatus = kTaskStatusNew;
        ++c.m.scheduleIndex;
        if (c.m.schedule != nullptr && c.m.scheduleIndex >= c.m.schedule->count)
            aiSet(c, kCondScheduleDone);
    }

    /// IScheduleFlags
    inline std::uint32_t aiScheduleFlags(const AiCtx &c)
    {
        return c.m.schedule != nullptr ? c.m.conditions & c.m.schedule->interruptMask : 0;
    }

    /// FScheduleValid
    inline bool aiScheduleValid(const AiCtx &c)
    {
        if (c.m.schedule == nullptr)
            return false;
        return !aiHas(c, c.m.schedule->interruptMask | kCondScheduleDone | kCondTaskFailed);
    }

    /// SetState
    inline void aiSetState(AiCtx &c, std::uint8_t state)
    {
        if (state == kStateIdle)
            c.m.enemy = kAiNone;
        c.m.state = state;
        c.m.idealState = state;
        // (What the rest of the game sees of it: dead, dying, or about --
        // and of one of the people, whether he follows the player, as saves
        // before kept it.)
        if (c.me.ai < kAiDying)
            c.me.ai = isPerson(c.me.kind) ? (c.m.targetEnt == kAiPlayer ? 1 : 0)
                                          : (state == kStateIdle ? kAiIdle : kAiChase);
    }

    /// CBaseMonster::GetIdealState
    inline std::uint8_t aiBaseIdealState(AiCtx &c)
    {
        const std::uint32_t conditions = aiScheduleFlags(c);
        switch (c.m.state)
        {
        case kStateIdle:
            if (conditions & kCondNewEnemy)
                c.m.idealState = kStateCombat;
            else if (conditions & (kCondLightDamage | kCondHeavyDamage))
            {
                aiMakeIdealYaw(c, c.m.enemyLkp);
                c.m.idealState = kStateAlert;
            }
            else if (conditions & kCondHearSound)
            {
                if (const AiSound *sound = aiBestSound(c))
                {
                    aiMakeIdealYaw(c, sound->origin);
                    if (sound->type & (kSoundCombat | kSoundDanger))
                        c.m.idealState = kStateAlert;
                }
            }
            else if (conditions & (kCondSmell | kCondSmellFood))
                c.m.idealState = kStateAlert;
            break;
        case kStateAlert:
            if (conditions & (kCondNewEnemy | kCondSeeEnemy))
                c.m.idealState = kStateCombat;
            else if (conditions & kCondHearSound)
            {
                c.m.idealState = kStateAlert;
                if (const AiSound *sound = aiBestSound(c))
                    aiMakeIdealYaw(c, sound->origin);
            }
            break;
        case kStateCombat:
            if (c.m.enemy == kAiNone)
                c.m.idealState = kStateAlert;
            break;
        case kStateDead:
            c.m.idealState = kStateDead;
            break;
        default:
            break;
        }
        return c.m.idealState;
    }

    /// CSquadMonster::GetIdealState
    inline std::uint8_t aiSquadIdealState(AiCtx &c)
    {
        if ((c.m.state == kStateIdle || c.m.state == kStateAlert) && aiHas(c, kCondNewEnemy) && aiInSquad(c))
            aiSquadMakeEnemy(c, c.m.enemy);
        return aiBaseIdealState(c);
    }

    inline std::uint8_t aiGetIdealState(AiCtx &c)
    {
        if (c.kind.idealState != nullptr)
            return c.kind.idealState(c);
        if (c.kind.squadMonster)
            return aiSquadIdealState(c);
        return aiBaseIdealState(c);
    }

    /// SquadMakeEnemy
    inline void aiSquadMakeEnemy(AiCtx &c, std::int16_t enemy)
    {
        if (!aiInSquad(c) || enemy == kAiNone)
            return;
        AiTarget t;
        if (!aiTargetOf(c.w, c.player, enemy, t))
            return;
        for (int i = 0; i < kMaxSquadMembers; ++i)
        {
            const std::int16_t id = aiSquadMember(c, i);
            AiMonster *member = aiFind(c.a, id);
            if (member == nullptr || member->enemy == enemy || (member->conditions & kCondSeeEnemy) ||
                member->schedule == nullptr || !(member->schedule->interruptMask & kCondNewEnemy))
                continue;
            const AiKind *kind = aiKindOf(c.a.states[id].kind);
            if (kind == nullptr)
                continue;
            AiCtx other{c.w, c.player, c.a, c.ai, id, c.a.states[id], *member, *kind};
            if (aiRelationship(other, t) < kRelDislike)
                continue;
            if (member->enemy != kAiNone)
                aiPushEnemy(other, member->enemy, member->enemyLkp);
            member->enemy = enemy;
            for (int k = 0; k < 3; ++k)
                member->enemyLkp[k] = t.origin[k];
            member->conditions |= kCondNewEnemy;
        }
    }

    /// CBaseMonster::GetScheduleOfType
    inline const AiSchedule *aiBaseScheduleOfType(AiCtx &c, int type)
    {
        switch (type)
        {
        case kSchedIdleStand:
            // (FCanActiveIdle is FALSE for the base: never the active idle.)
            if (aiRandomLong(c.a, 0, 14) == 0 && c.kind.canActiveIdle != nullptr && c.kind.canActiveIdle(c))
                return &slActiveIdle;
            return &slIdleStand;
        case kSchedIdleWalk: return &slIdleWalk;
        case kSchedWaitTrigger: return &slIdleTrigger;
        case kSchedWakeAngry: return &slWakeAngry;
        case kSchedAlertFace: return &slAlertFace;
        case kSchedAlertStand: return &slAlertStand;
        case kSchedCombatStand: return &slCombatStand;
        case kSchedCombatFace: return &slCombatFace;
        case kSchedChaseEnemy: return &slChaseEnemy;
        case kSchedChaseEnemyFailed: return &slFail;
        case kSchedSmallFlinch: return &slSmallFlinch;
        case kSchedAlertSmallFlinch: return &slAlertSmallFlinch;
        case kSchedReload: return &slReload;
        case kSchedArmWeapon: return &slArmWeapon;
        case kSchedStandoff: return &slStandoff;
        case kSchedRangeAttack1: return &slRangeAttack1;
        case kSchedRangeAttack2: return &slRangeAttack2;
        case kSchedMeleeAttack1: return &slPrimaryMeleeAttack;
        case kSchedMeleeAttack2: return &slSecondaryMeleeAttack;
        case kSchedSpecialAttack1: return &slSpecialAttack1;
        case kSchedSpecialAttack2: return &slSpecialAttack2;
        case kSchedTakeCoverFromBestSound: return &slTakeCoverFromBestSound;
        case kSchedTakeCoverFromEnemy: return &slTakeCoverFromEnemy;
        case kSchedCower: return &slCower;
        case kSchedAmbush: return &slAmbush;
        case kSchedBarnacleVictimGrab: return &slBarnacleVictimGrab;
        case kSchedBarnacleVictimChomp: return &slBarnacleVictimChomp;
        case kSchedInvestigateSound: return &slInvestigateSound;
        case kSchedDie: return &slDie;
        case kSchedTakeCoverFromOrigin: return &slTakeCoverFromOrigin;
        case kSchedVictoryDance: return &slVictoryDance;
        case kSchedFail: return &slFail;
        default: return &slIdleStand;
        }
    }

    /// CSquadMonster::GetScheduleOfType
    inline const AiSchedule *aiSquadScheduleOfType(AiCtx &c, int type)
    {
        if (type == kSchedChaseEnemyFailed)
            return &slChaseEnemyFailed;
        return aiBaseScheduleOfType(c, type);
    }

    inline const AiSchedule *aiScheduleOfType(AiCtx &c, int type)
    {
        if (c.kind.scheduleOfType != nullptr)
            return c.kind.scheduleOfType(c, type);
        if (c.kind.squadMonster)
            return aiSquadScheduleOfType(c, type);
        return aiBaseScheduleOfType(c, type);
    }

    /// CBaseMonster::GetSchedule
    inline const AiSchedule *aiBaseGetSchedule(AiCtx &c)
    {
        switch (c.m.state)
        {
        case kStateProne:
            return aiScheduleOfType(c, kSchedBarnacleVictimGrab);
        case kStateIdle:
            if (aiHas(c, kCondHearSound))
                return aiScheduleOfType(c, kSchedAlertFace);
            if (aiRouteClearNow(c))
                return aiScheduleOfType(c, kSchedIdleStand);
            return aiScheduleOfType(c, kSchedIdleWalk);
        case kStateAlert:
            if (aiHas(c, kCondEnemyDead) && aiLookupActivity(c, kActVictoryDance) >= 0)
                return aiScheduleOfType(c, kSchedVictoryDance);
            if (aiHas(c, kCondLightDamage | kCondHeavyDamage))
            {
                // (Roughly the right way: (1 - fov) * 60 degrees.)
                const int diff = aiYawDiff(c);
                const int within = static_cast<int>((static_cast<std::int64_t>(kFxOne - c.kind.fov) * 60 * 65536) /
                                                    (static_cast<std::int64_t>(kFxOne) * 360));
                if ((diff < 0 ? -diff : diff) < within)
                    return aiScheduleOfType(c, kSchedTakeCoverFromOrigin);
                return aiScheduleOfType(c, kSchedAlertSmallFlinch);
            }
            if (aiHas(c, kCondHearSound))
                return aiScheduleOfType(c, kSchedAlertFace);
            return aiScheduleOfType(c, kSchedAlertStand);
        case kStateCombat:
            if (aiHas(c, kCondEnemyDead))
            {
                c.m.enemy = kAiNone;
                if (aiGetEnemy(c))
                {
                    aiClear(c, kCondEnemyDead);
                    return aiGetSchedule(c);
                }
                aiSetState(c, kStateAlert);
                return aiGetSchedule(c);
            }
            if (aiHas(c, kCondNewEnemy))
                return aiScheduleOfType(c, kSchedWakeAngry);
            if (aiHas(c, kCondLightDamage) && !aiHasMemory(c, kMemoryFlinched))
                return aiScheduleOfType(c, kSchedSmallFlinch);
            if (!aiHas(c, kCondSeeEnemy))
            {
                if (!aiHas(c, kCondEnemyOccluded))
                    return aiScheduleOfType(c, kSchedCombatFace);
                return aiScheduleOfType(c, kSchedChaseEnemy);
            }
            if (aiHas(c, kCondCanRangeAttack1))
                return aiScheduleOfType(c, kSchedRangeAttack1);
            if (aiHas(c, kCondCanRangeAttack2))
                return aiScheduleOfType(c, kSchedRangeAttack2);
            if (aiHas(c, kCondCanMeleeAttack1))
                return aiScheduleOfType(c, kSchedMeleeAttack1);
            if (aiHas(c, kCondCanMeleeAttack2))
                return aiScheduleOfType(c, kSchedMeleeAttack2);
            if (!aiHas(c, kCondCanRangeAttack1 | kCondCanMeleeAttack1))
                return aiScheduleOfType(c, kSchedChaseEnemy);
            if (!aiFacingIdeal(c))
                return aiScheduleOfType(c, kSchedCombatFace);
            break;
        case kStateDead:
            return aiScheduleOfType(c, kSchedDie);
        default:
            break;
        }
        return &slError;
    }

    inline const AiSchedule *aiGetSchedule(AiCtx &c)
    {
        if (c.kind.getSchedule != nullptr)
            return c.kind.getSchedule(c);
        return aiBaseGetSchedule(c);
    }

    // ------------------------------------------------------- deaths, flinches

    /// GetDeathActivity: by where it was hit, and from which side -- with
    /// room behind or in front to fall that way.
    inline std::uint8_t aiDeathActivity(AiCtx &c)
    {
        if (c.m.deadflag != 0)
            return c.m.idealActivity;
        fx forward[3];
        aiForward(c.me.yaw, forward);
        // (The blow's way, back towards where it came from.)
        // (Two unit vectors in Q12: 16-bit parts.)
        const fx back[3] = {-c.m.attackDir[0], -c.m.attackDir[1], -c.m.attackDir[2]};
        const fx dot = dotShort(forward, 1, back, 1) >> kFxShift;
        std::uint8_t death = kActDieSimple;
        bool triedDirection = false;
        switch (c.m.lastHitGroup)
        {
        case kHitHead:
            death = kActDieHeadshot;
            break;
        case 3: // HITGROUP_STOMACH
            death = kActDieGutshot;
            break;
        default:
            triedDirection = true;
            if (dot > fxDot(0.3))
                death = kActDieForward;
            else if (dot <= fxDot(-0.3))
                death = kActDieBackward;
            break;
        }
        if (aiLookupActivity(c, death, true) < 0)
        {
            if (triedDirection)
                death = kActDieSimple;
            else if (dot > fxDot(0.3))
                death = kActDieForward;
            else if (dot <= fxDot(-0.3))
                death = kActDieBackward;
        }
        if (aiLookupActivity(c, death, true) < 0)
            death = kActDieSimple;
        if (death == kActDieForward || death == kActDieBackward)
        {
            // (Room to fall: a head's size, 64 units that way.)
            fx center[3];
            aiCenter(c, center);
            const int sign = death == kActDieForward ? 1 : -1;
            const fx to[3] = {center[0] + forward[0] * 64 * sign, center[1] + forward[1] * 64 * sign, center[2]};
            if (!walkClear(*c.w.world, &c.a, c.index, kWalkCrouched, c.me.pos, to, 5))
                death = kActDieSimple;
        }
        return death;
    }

    /// GetSmallFlinchActivity
    inline std::uint8_t aiSmallFlinchActivity(AiCtx &c)
    {
        std::uint8_t flinch = kActSmallFlinch;
        switch (c.m.lastHitGroup)
        {
        case kHitHead: flinch = kActFlinchHead; break;
        case 3:        flinch = kActFlinchStomach; break;
        case 4:        flinch = kActFlinchLeftArm; break;
        case 5:        flinch = kActFlinchRightArm; break;
        case 6:        flinch = kActFlinchLeftLeg; break;
        case 7:        flinch = kActFlinchRightLeg; break;
        default:       break;
        }
        if (aiLookupActivity(c, flinch) < 0)
            flinch = kActSmallFlinch;
        return flinch;
    }

    // ------------------------------------------------------------- the tasks

    /// SetTurnActivity
    inline void aiSetTurnActivity(AiCtx &c)
    {
        const int diff = aiYawDiff(c);
        if (diff <= -8192 && aiLookupActivity(c, kActTurnRight) >= 0)
            c.m.idealActivity = kActTurnRight;
        else if (diff > 8192 && aiLookupActivity(c, kActTurnLeft) >= 0)
            c.m.idealActivity = kActTurnLeft;
    }

    inline void aiFindCoverTask(AiCtx &c, bool ok, int waitMs)
    {
        if (ok)
        {
            c.m.moveWaitFinished = c.ai.now + waitMs;
            aiTaskComplete(c);
        }
        else
        {
            aiTaskFail(c);
        }
    }

    /// The threat's place and eyes: its enemy, or itself.
    inline void aiEnemyEyes(AiCtx &c, fx *at, fx &viewOfs)
    {
        AiTarget t;
        if (aiTargetOf(c.w, c.player, c.m.enemy, t))
        {
            for (int k = 0; k < 3; ++k)
                at[k] = t.origin[k];
            viewOfs = t.eye[2] - t.origin[2];
            return;
        }
        for (int k = 0; k < 3; ++k)
            at[k] = c.me.pos[k];
        viewOfs = c.kind.eye;
    }

    /// CBaseMonster::StartTask
    inline void aiBaseStartTask(AiCtx &c, const AiTask &task)
    {
        switch (task.task)
        {
        case kTaskTurnRight:
        case kTaskTurnLeft:
        {
            const int turn = static_cast<int>((static_cast<std::int64_t>(task.data) * 65536) / 360);
            c.m.idealYaw = static_cast<std::uint16_t>(c.me.yaw + (task.task == kTaskTurnLeft ? turn : -turn));
            aiSetTurnActivity(c);
            break;
        }
        case kTaskRemember:
            aiRemember(c, static_cast<std::uint32_t>(task.data));
            aiTaskComplete(c);
            break;
        case kTaskForget:
            aiForgetMemory(c, static_cast<std::uint32_t>(task.data));
            aiTaskComplete(c);
            break;
        case kTaskFindHintNode:
            // (No hint nodes are kept: none is ever found.)
            aiTaskFail(c);
            break;
        case kTaskStoreLastPosition:
            for (int k = 0; k < 3; ++k)
                c.m.lastPosition[k] = c.me.pos[k];
            aiTaskComplete(c);
            break;
        case kTaskClearLastPosition:
            c.m.lastPosition[0] = c.m.lastPosition[1] = c.m.lastPosition[2] = 0;
            aiTaskComplete(c);
            break;
        case kTaskClearHintNode:
            aiTaskComplete(c);
            break;
        case kTaskStopMoving:
            if (c.m.idealActivity == c.m.movementActivity)
                c.m.idealActivity = c.kind.stoppedActivity != nullptr ? c.kind.stoppedActivity(c) : kActIdle;
            aiRouteClear(c);
            aiTaskComplete(c);
            break;
        case kTaskPlaySequenceFaceEnemy:
        case kTaskPlaySequenceFaceTarget:
        case kTaskPlaySequence:
            c.m.idealActivity = static_cast<std::uint8_t>(task.data);
            break;
        case kTaskPlayActiveIdle:
            aiTaskComplete(c);
            break;
        case kTaskSetSchedule:
        {
            const AiSchedule *next = aiScheduleOfType(c, task.data);
            if (next != nullptr)
                aiChangeSchedule(c, next);
            else
                aiTaskFail(c);
            break;
        }
        case kTaskFindNearNodeCoverFromEnemy:
        case kTaskFindFarNodeCoverFromEnemy:
        case kTaskFindNodeCoverFromEnemy:
        {
            if (c.m.enemy == kAiNone)
            {
                aiTaskFail(c);
                return;
            }
            fx at[3], viewOfs;
            aiEnemyEyes(c, at, viewOfs);
            const int least = task.task == kTaskFindFarNodeCoverFromEnemy ? task.data : 0;
            const int most = task.task == kTaskFindNearNodeCoverFromEnemy ? task.data : 784;
            if (aiFindCover(c, at, viewOfs, least, most))
                aiTaskComplete(c);
            else
                aiTaskFail(c);
            break;
        }
        case kTaskFindCoverFromEnemy:
        {
            fx at[3], viewOfs;
            aiEnemyEyes(c, at, viewOfs);
            const bool found = aiFindLateralCover(c, at, viewOfs) || aiFindCover(c, at, viewOfs, 0, 784);
            aiFindCoverTask(c, found, task.data);
            break;
        }
        case kTaskFindCoverFromOrigin:
            aiFindCoverTask(c, aiFindCover(c, c.me.pos, c.kind.eye, 0, 784), task.data);
            break;
        case kTaskFindCoverFromBestSound:
        {
            const AiSound *sound = aiBestSound(c);
            aiFindCoverTask(c, sound != nullptr && aiFindCover(c, sound->origin, 0, sound->volume, 784), task.data);
            break;
        }
        case kTaskFaceHintNode:
            aiSetTurnActivity(c);
            break;
        case kTaskFaceLastPosition:
            aiMakeIdealYaw(c, c.m.lastPosition);
            aiSetTurnActivity(c);
            break;
        case kTaskFaceTarget:
        {
            AiTarget t;
            if (aiTargetOf(c.w, c.player, c.m.targetEnt, t))
            {
                aiMakeIdealYaw(c, t.origin);
                aiSetTurnActivity(c);
            }
            else
            {
                aiTaskFail(c);
            }
            break;
        }
        case kTaskMoveToTargetRange:
        {
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.targetEnt, t))
            {
                aiTaskFail(c);
                break;
            }
            fx feet[3];
            aiFeetOf(t, feet);
            if (aiDist(feet, c.me.pos) < fxFromInt(1))
            {
                aiTaskComplete(c);
                break;
            }
            // MoveToTarget(ACT_WALK, 2)
            c.m.movementActivity = kActWalk;
            if (!aiBuildRoute(c, kMoveGoalTarget, feet, 2000))
                aiTaskFail(c);
            break;
        }
        case kTaskGetPathToTarget:
        {
            AiTarget t;
            fx feet[3];
            if (aiTargetOf(c.w, c.player, c.m.targetEnt, t))
                aiFeetOf(t, feet);
            // (MoveToTarget(m_movementActivity, 1))
            if (t.valid && aiBuildRoute(c, kMoveGoalTarget, feet, 1000))
                aiTaskComplete(c);
            else
                aiTaskFail(c);
            break;
        }
        case kTaskFaceEnemy:
            aiMakeIdealYaw(c, c.m.enemyLkp);
            aiSetTurnActivity(c);
            break;
        case kTaskFaceIdeal:
            aiSetTurnActivity(c);
            break;
        case kTaskFaceRoute:
            if (aiRouteClearNow(c))
            {
                aiTaskFail(c);
            }
            else
            {
                fx dest[3];
                aiMoveDest(c, dest);
                aiMakeIdealYaw(c, dest);
                aiSetTurnActivity(c);
            }
            break;
        case kTaskWaitPvs:
        case kTaskWaitIndefinite:
            break;
        case kTaskWait:
        case kTaskWaitFaceEnemy:
            c.m.waitFinished = c.ai.now + task.data;
            break;
        case kTaskWaitRandom:
            c.m.waitFinished = c.ai.now + aiRandomMs(c.a, 100, task.data);
            break;
        case kTaskClearMoveWait:
            c.m.moveWaitFinished = c.ai.now;
            aiTaskComplete(c);
            break;
        case kTaskMeleeAttack1NoTurn:
        case kTaskMeleeAttack1:
            c.m.idealActivity = kActMeleeAttack1;
            break;
        case kTaskMeleeAttack2NoTurn:
        case kTaskMeleeAttack2:
            c.m.idealActivity = kActMeleeAttack2;
            break;
        case kTaskRangeAttack1NoTurn:
        case kTaskRangeAttack1:
            c.m.idealActivity = kActRangeAttack1;
            break;
        case kTaskRangeAttack2NoTurn:
        case kTaskRangeAttack2:
            c.m.idealActivity = kActRangeAttack2;
            break;
        case kTaskReloadNoTurn:
        case kTaskReload:
            c.m.idealActivity = kActReload;
            break;
        case kTaskSpecialAttack1:
            c.m.idealActivity = kActSpecialAttack1;
            break;
        case kTaskSpecialAttack2:
            c.m.idealActivity = kActSpecialAttack2;
            break;
        case kTaskSetActivity:
            c.m.idealActivity = static_cast<std::uint8_t>(task.data);
            aiTaskComplete(c);
            break;
        case kTaskGetPathToEnemyLkp:
        {
            fx at[3];
            for (int k = 0; k < 3; ++k)
                at[k] = c.m.enemyLkp[k];
            if (c.m.enemy == kAiPlayer)
                at[2] -= c.player.ducked ? kCrouchedFeet : kStandingFeet;
            if (aiBuildRoute(c, kMoveGoalLocation, at) ||
                aiBuildNearestRoute(c, c.m.enemyLkp, c.kind.eye, 0, fxToInt(aiDist(c.m.enemyLkp, c.me.pos))))
                aiTaskComplete(c);
            else
                aiTaskFail(c);
            break;
        }
        case kTaskGetPathToEnemy:
        {
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.enemy, t))
            {
                aiTaskFail(c);
                return;
            }
            fx feet[3] = {t.origin[0], t.origin[1], t.origin[2]};
            if (t.isPlayer)
                feet[2] -= t.height / 2;
            if (aiBuildRoute(c, kMoveGoalEnemy, feet) ||
                aiBuildNearestRoute(c, t.origin, t.eye[2] - t.origin[2], 0, fxToInt(aiDist(t.origin, c.me.pos))))
                aiTaskComplete(c);
            else
                aiTaskFail(c);
            break;
        }
        case kTaskGetPathToEnemyCorpse:
        {
            fx forward[3];
            aiForward(c.me.yaw, forward);
            const fx at[3] = {c.m.enemyLkp[0] - forward[0] * 64, c.m.enemyLkp[1] - forward[1] * 64, c.m.enemyLkp[2]};
            if (aiBuildRoute(c, kMoveGoalLocation, at))
                aiTaskComplete(c);
            else
                aiTaskFail(c);
            break;
        }
        case kTaskGetPathToSpot:
            if (aiBuildRoute(c, kMoveGoalLocation, c.m.moveGoal))
                aiTaskComplete(c);
            else
                aiTaskFail(c);
            break;
        case kTaskGetPathToLastPosition:
        {
            const fx at[3] = {c.m.lastPosition[0], c.m.lastPosition[1], c.m.lastPosition[2]};
            if (aiMoveToLocation(c, c.m.movementActivity, 2000, at))
                aiTaskComplete(c);
            else
                aiTaskFail(c);
            break;
        }
        case kTaskGetPathToBestSound:
        {
            const AiSound *sound = aiBestSound(c);
            if (sound != nullptr && aiBuildRoute(c, kMoveGoalLocation, sound->origin, 2000))
                aiTaskComplete(c);
            else
                aiTaskFail(c);
            break;
        }
        case kTaskGetPathToBestScent:
        {
            const AiSound *scent = aiBestScent(c);
            if (scent != nullptr && aiBuildRoute(c, kMoveGoalLocation, scent->origin, 2000))
                aiTaskComplete(c);
            else
                aiTaskFail(c);
            break;
        }
        case kTaskRunPath:
            c.m.movementActivity = aiLookupActivity(c, kActRun) >= 0 ? kActRun : kActWalk;
            aiTaskComplete(c);
            break;
        case kTaskWalkPath:
            c.m.movementActivity = aiLookupActivity(c, kActWalk) >= 0 ? kActWalk : kActRun;
            aiTaskComplete(c);
            break;
        case kTaskStrafePath:
        {
            fx right[3], dest[3];
            aiRight(c.me.yaw, right);
            aiMoveDest(c, dest);
            const fx side = static_cast<fx>((static_cast<std::int64_t>(dest[0] - c.me.pos[0]) * right[0] +
                                             static_cast<std::int64_t>(dest[1] - c.me.pos[1]) * right[1]) >> kFxShift);
            c.m.movementActivity = side > 0 ? kActStrafeRight : kActStrafeLeft;
            aiTaskComplete(c);
            break;
        }
        case kTaskWaitForMovement:
            if (aiRouteClearNow(c))
                aiTaskComplete(c);
            break;
        case kTaskEat:
            c.m.hungryTime = c.ai.now + task.data;
            aiTaskComplete(c);
            break;
        case kTaskSmallFlinch:
            c.m.idealActivity = aiSmallFlinchActivity(c);
            break;
        case kTaskDie:
            aiRouteClear(c);
            c.m.idealActivity = aiDeathActivity(c);
            c.m.deadflag = 1; // DEAD_DYING
            break;
        case kTaskSoundWake:
            aiSound(c, kAiSoundAlert);
            aiTaskComplete(c);
            break;
        case kTaskSoundDie:
        case kTaskSoundDeath:
            aiSound(c, kAiSoundDeath);
            aiTaskComplete(c);
            break;
        case kTaskSoundIdle:
            aiSound(c, kAiSoundIdle);
            aiTaskComplete(c);
            break;
        case kTaskSoundPain:
            aiSound(c, kAiSoundPain);
            aiTaskComplete(c);
            break;
        case kTaskSoundAngry:
            aiTaskComplete(c);
            break;
        case kTaskSuggestState:
            c.m.idealState = static_cast<std::uint8_t>(task.data);
            aiTaskComplete(c);
            break;
        case kTaskSetFailSchedule:
            c.m.failSchedule = static_cast<std::uint8_t>(task.data);
            aiTaskComplete(c);
            break;
        case kTaskClearFailSchedule:
            c.m.failSchedule = kSchedNone;
            aiTaskComplete(c);
            break;
        default:
            break;
        }
    }

    inline void aiStartTask(AiCtx &c, const AiTask &task)
    {
        if (c.kind.startTask != nullptr && c.kind.startTask(c, task))
            return;
        aiBaseStartTask(c, task);
    }

    inline void aiDied(AiCtx &c); // the body lies: TASK_DIE done

    /// CBaseMonster::RunTask
    inline void aiBaseRunTask(AiCtx &c, const AiTask &task)
    {
        switch (task.task)
        {
        case kTaskTurnRight:
        case kTaskTurnLeft:
        case kTaskFaceHintNode:
        case kTaskFaceLastPosition:
        case kTaskFaceTarget:
        case kTaskFaceIdeal:
        case kTaskFaceRoute:
            aiChangeYaw(c);
            if (aiFacingIdeal(c))
                aiTaskComplete(c);
            break;
        case kTaskPlaySequenceFaceEnemy:
        case kTaskPlaySequenceFaceTarget:
        {
            const std::int16_t whom = task.task == kTaskPlaySequenceFaceEnemy ? c.m.enemy : c.m.targetEnt;
            AiTarget t;
            if (whom != kAiNone && aiTargetOf(c.w, c.player, whom, t))
            {
                aiMakeIdealYaw(c, t.origin);
                aiChangeYaw(c);
            }
            if (c.m.seqFinished)
                aiTaskComplete(c);
            break;
        }
        case kTaskMoveToTargetRange:
        {
            AiTarget t;
            if (!aiTargetOf(c.w, c.player, c.m.targetEnt, t))
            {
                aiTaskFail(c);
                break;
            }
            fx feet[3];
            aiFeetOf(t, feet);
            const fx range = fxFromInt(task.data);
            fx distance = aiDist2D(c.m.moveGoal, c.me.pos);
            // (Gone a good way since the route was made: made again.)
            if (distance < range || aiDist(c.m.moveGoal, feet) > range / 2)
            {
                for (int k = 0; k < 3; ++k)
                    c.m.moveGoal[k] = feet[k];
                distance = aiDist2D(c.m.moveGoal, c.me.pos);
                if (distance >= range && c.m.movementGoal != kMoveGoalNone)
                    aiBuildRoute(c, kMoveGoalTarget, feet);
            }
            // Its move over, short of the range: held up by the target
            // itself, as good as there, which will do; otherwise after it
            // again, or failed if there is no way. (Never left waiting on a
            // move that is not coming.)
            if (distance >= range && c.m.movementGoal == kMoveGoalNone)
            {
                if (aiTargetInReach(c, t))
                    distance = 0;
                else if (!aiBuildRoute(c, kMoveGoalTarget, feet))
                {
                    aiTaskFail(c);
                    break;
                }
            }
            if (distance < range)
            {
                aiTaskComplete(c);
                aiRouteClear(c);
            }
            else if (distance < fxFromInt(190) && c.m.movementActivity != kActWalk)
                c.m.movementActivity = kActWalk;
            else if (distance >= fxFromInt(270) && c.m.movementActivity != kActRun)
                c.m.movementActivity = kActRun;
            break;
        }
        case kTaskPlaySequence:
        case kTaskPlayActiveIdle:
            if (c.m.seqFinished)
                aiTaskComplete(c);
            break;
        case kTaskFaceEnemy:
            aiMakeIdealYaw(c, c.m.enemyLkp);
            aiChangeYaw(c);
            if (aiFacingIdeal(c))
                aiTaskComplete(c);
            break;
        case kTaskWaitPvs:
            if (c.w.world->vis == nullptr || leafSees(*c.w.world, c.m.leaf, c.ai.playerLeaf))
                aiTaskComplete(c);
            break;
        case kTaskWaitIndefinite:
            break;
        case kTaskWait:
        case kTaskWaitRandom:
            if (c.ai.now >= c.m.waitFinished)
                aiTaskComplete(c);
            break;
        case kTaskWaitFaceEnemy:
            aiMakeIdealYaw(c, c.m.enemyLkp);
            aiChangeYaw(c);
            if (c.ai.now >= c.m.waitFinished)
                aiTaskComplete(c);
            break;
        case kTaskWaitForMovement:
            if (aiMovementIsComplete(c))
            {
                aiTaskComplete(c);
                aiRouteClear(c);
            }
            break;
        case kTaskDie:
            if (c.m.seqFinished && c.me.finished)
                aiDied(c);
            break;
        case kTaskRangeAttack1NoTurn:
        case kTaskMeleeAttack1NoTurn:
        case kTaskMeleeAttack2NoTurn:
        case kTaskRangeAttack2NoTurn:
        case kTaskReloadNoTurn:
            if (c.m.seqFinished)
            {
                c.m.activity = kActReset;
                aiTaskComplete(c);
            }
            break;
        case kTaskRangeAttack1:
        case kTaskMeleeAttack1:
        case kTaskMeleeAttack2:
        case kTaskRangeAttack2:
        case kTaskSpecialAttack1:
        case kTaskSpecialAttack2:
        case kTaskReload:
            aiMakeIdealYaw(c, c.m.enemyLkp);
            aiChangeYaw(c);
            if (c.m.seqFinished)
            {
                c.m.activity = kActReset;
                aiTaskComplete(c);
            }
            break;
        case kTaskSmallFlinch:
            if (c.m.seqFinished)
                aiTaskComplete(c);
            break;
        default:
            break;
        }
    }

    inline void aiRunTask(AiCtx &c, const AiTask &task)
    {
        if (c.kind.runTask != nullptr && c.kind.runTask(c, task))
            return;
        aiBaseRunTask(c, task);
    }

    /// ScheduleChange: what a kind lets go of when its schedule changes (a
    /// squad's slot; a slave's beams).
    inline void aiScheduleChange(AiCtx &c)
    {
        if (c.kind.squadMonster)
            aiVacateSlot(c);
        if (c.kind.scheduleChange != nullptr)
            c.kind.scheduleChange(c);
    }

    /// MaintainSchedule: keeps a valid schedule, starting and running its
    /// tasks.
    inline void aiMaintainSchedule(AiCtx &c)
    {
        for (int i = 0; i < 10; ++i)
        {
            if (c.m.schedule != nullptr && aiTaskIsComplete(c))
                aiNextScheduledTask(c);
            if (!aiScheduleValid(c) || c.m.state != c.m.idealState)
            {
                aiScheduleChange(c);
                if (c.m.idealState != kStateDead &&
                    (c.m.idealState != kStateScript || c.m.idealState == c.m.state))
                {
                    if ((c.m.conditions != 0 && !aiHas(c, kCondScheduleDone)) ||
                        (c.m.schedule != nullptr && (c.m.schedule->interruptMask & kCondScheduleDone)) ||
                        (c.m.state == kStateCombat && c.m.enemy == kAiNone))
                        aiGetIdealState(c);
                }
                const AiSchedule *next;
                if (aiHas(c, kCondTaskFailed) && c.m.state == c.m.idealState)
                {
                    next = aiScheduleOfType(c, c.m.failSchedule != kSchedNone ? c.m.failSchedule : kSchedFail);
                }
                else
                {
                    aiSetState(c, c.m.idealState);
                    next = (c.m.state == kStateScript || c.m.state == kStateDead) ? aiBaseGetSchedule(c)
                                                                                    : aiGetSchedule(c);
                }
                aiChangeSchedule(c, next);
            }
            if (c.m.taskStatus == kTaskStatusNew)
            {
                const AiTask *task = aiGetTask(c);
                if (task == nullptr)
                    break;
                c.m.taskStatus = kTaskStatusRunning; // TaskBegin
                aiStartTask(c, *task);
            }
            if (c.m.activity != c.m.idealActivity)
                aiSetActivity(c, c.m.idealActivity);
            if (!aiTaskIsComplete(c) && c.m.taskStatus != kTaskStatusNew)
                break;
        }
        if (aiTaskIsRunning(c))
        {
            if (const AiTask *task = aiGetTask(c))
                aiRunTask(c, *task);
        }
        if (c.m.activity != c.m.idealActivity)
            aiSetActivity(c, c.m.idealActivity);
    }

    /// IgnoreConditions
    inline std::uint32_t aiBaseIgnoreConditions(AiCtx &c)
    {
        std::uint32_t ignore = 0;
        if (c.m.hungryTime > c.ai.now) // !FShouldEat
            ignore |= kCondSmellFood;
        return ignore;
    }

    /// Whether the player is in what it can see of the map (FIND_CLIENT_IN_PVS).
    inline bool aiClientInPvs(const AiCtx &c)
    {
        return c.w.world->vis == nullptr || leafSees(*c.w.world, c.m.leaf, c.ai.playerLeaf);
    }

    /// CSquadMonster::CheckEnemy
    inline bool aiCheckEnemy(AiCtx &c)
    {
        const bool updated = aiCheckEnemyBase(c);
        if (c.kind.squadMonster && aiInSquad(c))
        {
            AiMonster *leader = aiSquadLeader(c);
            if (c.m.enemy == leader->enemy)
            {
                if (updated)
                {
                    for (int k = 0; k < 3; ++k)
                        leader->enemyLkp[k] = c.m.enemyLkp[k];
                }
                else
                {
                    for (int k = 0; k < 3; ++k)
                        c.m.enemyLkp[k] = leader->enemyLkp[k];
                }
            }
        }
        return updated;
    }

    /// FCheckAITrigger: the map's TriggerCondition met, its TriggerTarget is
    /// set off -- once.
    inline bool aiCheckTrigger(AiCtx &c)
    {
        const int condition = c.me.item >> kGruntTriggerShift;
        if (condition == 0 || c.me.fires == 0)
            return false;
        bool fire = false;
        switch (condition)
        {
        case 1: // AITRIGGER_SEEPLAYER_ANGRY_AT_PLAYER
            fire = c.m.enemy == kAiPlayer && aiHas(c, kCondSeeEnemy);
            break;
        case 10: // AITRIGGER_SEEPLAYER_UNCONDITIONAL
            fire = aiHas(c, kCondSeeClient);
            break;
        case 11: // AITRIGGER_SEEPLAYER_NOT_IN_COMBAT
            fire = aiHas(c, kCondSeeClient) && c.m.state != kStateCombat && c.m.state != kStateProne &&
                   c.m.state != kStateScript;
            break;
        case 2: // AITRIGGER_TAKEDAMAGE
            fire = aiHas(c, kCondLightDamage | kCondHeavyDamage);
            break;
        case 4: // AITRIGGER_DEATH
            fire = c.m.deadflag != 0 || c.me.ai >= kAiDying;
            break;
        case 3: // AITRIGGER_HALFHEALTH
            fire = aiAlive(c.a, c.index) && c.me.health <= c.m.maxHealth / 2;
            break;
        case 7: // AITRIGGER_HEARWORLD
            fire = aiHas(c, kCondHearSound) && (c.m.soundTypes & kSoundWorld);
            break;
        case 8: // AITRIGGER_HEARPLAYER
            fire = aiHas(c, kCondHearSound) && (c.m.soundTypes & kSoundPlayer);
            break;
        case 9: // AITRIGGER_HEARCOMBAT
            fire = aiHas(c, kCondHearSound) && (c.m.soundTypes & kSoundCombat);
            break;
        default:
            break;
        }
        if (!fire)
            return false;
        logicEnqueue(c.w, c.me.fires, 0, kUseToggle, -1, 1);
        c.me.item &= static_cast<std::uint8_t>((1u << kGruntTriggerShift) - 1);
        return true;
    }

    /// CBaseMonster::RunAI
    inline void aiRunAI(AiCtx &c)
    {
        if ((c.m.state == kStateIdle || c.m.state == kStateAlert) && aiRandomLong(c.a, 0, 99) == 0 &&
            !(c.me.flags & kActorGag))
            aiSound(c, kAiSoundIdle);
        if (c.m.state != kStateNone && c.m.state != kStateProne && c.m.state != kStateDead)
        {
            if (aiClientInPvs(c) || c.m.state == kStateCombat)
            {
                {
                    const AiSpentScope spent(0);
                    aiLook(c, 2048);
                }
                const AiSpentScope spent(1);
                aiListen(c);
                aiClear(c, c.kind.ignoreConditions != nullptr ? c.kind.ignoreConditions(c) : aiBaseIgnoreConditions(c));
                aiGetEnemy(c);
            }
            if (c.m.enemy != kAiNone)
            {
                const AiSpentScope spent(1);
                aiCheckEnemy(c);
            }
            if (c.kind.checkAmmo != nullptr)
                c.kind.checkAmmo(c);
        }
        aiCheckTrigger(c);
        if (c.kind.prescheduleThink != nullptr)
            c.kind.prescheduleThink(c);
        aiMaintainSchedule(c);
        c.m.conditions &= ~(kCondLightDamage | kCondHeavyDamage);
    }

    // ------------------------------------------------------------- damage

    /// The body lies where it fell (TASK_DIE's end): no more thinking, and a
    /// carcass to be smelt for half a minute.
    inline void aiDied(AiCtx &c)
    {
        c.m.deadflag = 2; // DEAD_DEAD
        c.me.ai = kAiDead;
        aiSound(c.a, kSoundCarcass, c.me.pos, 384, 30000);
        // (One the map marks to fade does: ShouldFadeOnDeath. A maker's
        // children are its to fade: makerTick.)
        if (c.a.records != nullptr && c.index < c.a.recordCount &&
            (c.a.records[c.index].light & kActorLightFadeCorpse))
            corpseFadeStart(c.me);
    }

    /// Killed
    inline void aiKilled(AiCtx &c, bool gib, bool neverGib)
    {
        const bool shouldGib = !neverGib && (gib || c.me.health < -30);
        if (aiHasMemory(c, kMemoryKilled))
        {
            if (shouldGib)
                gibBurst(c.w, c.index);
            return;
        }
        aiRemember(c, kMemoryKilled);
        c.m.idealState = kStateDead;
        aiSet(c, kCondLightDamage);
        // (What it sets off when it dies: now, whether or not a body is left.)
        if ((c.me.item >> kGruntTriggerShift) == 4 && c.me.fires != 0)
        {
            logicEnqueue(c.w, c.me.fires, 0, kUseToggle, -1, 1);
            c.me.item &= static_cast<std::uint8_t>((1u << kGruntTriggerShift) - 1);
        }
        if (c.kind.killed != nullptr && c.kind.killed(c))
            return;
        // A squad has lost one.
        if (c.kind.squadMonster)
            aiSquadLeave(c.a, c.index);
        // A script that had it has lost it.
        if (c.me.script >= 0 && c.me.script < c.w.recordCount)
        {
            c.w.states[c.me.script].dead = true;
            c.me.script = -1;
            c.me.scriptPhase = kActorFree;
        }
        if (shouldGib && gibBurst(c.w, c.index))
            return;
        // BecomeDead: what is left of it is a body, to be blown apart.
        c.me.health = static_cast<std::int16_t>(c.m.maxHealth / 2);
        c.me.ai = kAiDying;
        c.me.flags &= static_cast<std::uint8_t>(~kActorSolid);
        c.me.velocity[0] = c.me.velocity[1] = 0;
        actorPlaceBox(c.a, c.index);
    }

    /// TakeDamage. True if it took it.
    inline bool aiTakeDamage(AiCtx &c, AiDamage d)
    {
        if (c.kind.traceAttack != nullptr && !c.kind.traceAttack(c, d))
            return false;
        // TraceAttack: where it was hit (sk_monster_head 3, the rest 1).
        if (d.group != kHitGeneric || (d.type & kDmgBullet))
            c.m.lastHitGroup = d.group;
        if (d.group == kHitHead)
            d.amount *= 3;
        if (c.kind.takeDamage != nullptr && !c.kind.takeDamage(c, d))
            return false;
        // (The way the blow went: from its maker's middle towards ours.)
        if (d.hasFrom)
        {
            fx center[3];
            aiCenter(c, center);
            fx dir[3] = {d.from[0] - center[0], d.from[1] - center[1], d.from[2] - fxFromInt(10) - center[2]};
            const fx length = fxLength(dir);
            for (int k = 0; k < 3; ++k)
                c.m.attackDir[k] = length > 0 ? static_cast<fx>((static_cast<std::int64_t>(dir[k]) << kFxShift) / length) : 0;
        }
        if (c.me.ai >= kAiDying || c.m.deadflag != 0 || (aiHasMemory(c, kMemoryKilled) && c.me.health <= 0))
        {
            // DeadTakeDamage: only what tears a body apart counts.
            if ((d.type & kDmgGibCorpse) && c.me.health <= d.amount)
            {
                c.me.health = -50;
                gibBurst(c.w, c.index);
            }
            else if (d.type & kDmgGibCorpse)
            {
                c.me.health = static_cast<std::int16_t>(c.me.health - d.amount / 10);
            }
            return false;
        }
        if (c.m.deadflag == 0)
            aiSound(c, kAiSoundPain);
        c.me.health = static_cast<std::int16_t>(c.me.health - d.amount);
        // In a scene: felt, but not died of -- the scene is broken off if it
        // may be (ExitScriptedSequence, CancelScript), and it is free again;
        // if it may not, it dies when the scene is over (CineCleanup).
        if (c.me.script >= 0 || c.m.state == kStateScript)
        {
            aiSet(c, kCondLightDamage);
            if (c.me.script < 0 || !detail::scriptCanInterrupt(c.w, c.me.script))
                return true;
            detail::scriptCancel(c.w, c.player, c.me.script);
            if (c.me.script >= 0)
                return true; // (an aiscripted_sequence's: as shipped, nothing breaks it off)
        }
        if (c.me.health <= 0)
        {
            if (c.w.achievements != nullptr && !aiHasMemory(c, kMemoryKilled))
                c.w.achievements->push(AchEventType::Killed,
                                       c.w.nameHashes != nullptr && c.me.name < c.w.nameCount ? c.w.nameHashes[c.me.name] : 0,
                                       c.me.kind, d.attacker == kAiPlayer ? kAchByPlayer : 0);
            aiKilled(c, (d.type & kDmgAlwaysGib) != 0, (d.type & kDmgNeverGib) != 0);
            return true;
        }
        // Who did it, if someone did: where they were, and which way to look.
        if (d.attacker != kAiNone)
        {
            if (d.hasFrom)
            {
                if (c.m.enemy == kAiNone || d.attacker == c.m.enemy || !aiHas(c, kCondSeeEnemy))
                {
                    for (int k = 0; k < 3; ++k)
                        c.m.enemyLkp[k] = d.from[k];
                }
            }
            else
            {
                for (int k = 0; k < 3; ++k)
                    c.m.enemyLkp[k] = c.me.pos[k] + c.m.attackDir[k] * 64;
            }
            aiMakeIdealYaw(c, c.m.enemyLkp);
            if (d.amount > 0 || d.graze)
                aiSet(c, kCondLightDamage);
            if (d.amount >= 20)
                aiSet(c, kCondHeavyDamage);
        }
        return true;
    }

    // ------------------------------------------------------------- squads (2)

    /// StartMonster: its attacks from what clips the model has; a squad if
    /// it forms them; and, named and not a script's, it waits to be
    /// triggered.
    inline void aiStartMonster(AiCtx &c)
    {
        c.m.started = true;
        if (aiLookupActivity(c, kActRangeAttack1, true) >= 0)
            c.m.capability |= kCapRangeAttack1;
        if (aiLookupActivity(c, kActRangeAttack2, true) >= 0)
            c.m.capability |= kCapRangeAttack2;
        if (aiLookupActivity(c, kActMeleeAttack1, true) >= 0)
            c.m.capability |= kCapMeleeAttack1;
        if (aiLookupActivity(c, kActMeleeAttack2, true) >= 0)
            c.m.capability |= kCapMeleeAttack2;
        // (Thinking spread over the ticks.)
        c.m.thinkMs = aiRandomMs(c.a, 100, 400);
        if ((c.m.capability & kCapSquad) && !aiInSquad(c))
        {
            const bool named = aiSquadName(c.a, c.index) != 0;
            if (!named || (c.me.flags & kActorLeader))
                aiSquadRecruit(c, 1024, 4);
        }
        if (c.me.name != 0 && c.me.script < 0)
        {
            aiSetState(c, kStateIdle);
            aiSetActivity(c, kActIdle);
            aiChangeSchedule(c, aiScheduleOfType(c, kSchedWaitTrigger));
        }
        if (c.kind.started != nullptr)
            c.kind.started(c);
    }

    /// MonsterInit, for an actor: its health and the rest of what Spawn
    /// sets -- or, for one in a loaded game, as the game left it.
    inline void aiInit(AiCtx &c)
    {
        c.m.capability = c.kind.capability;
        // (As much as its kind has at the game's difficulty.)
        const int health = skillHealth(skillOf(c.w), c.me.kind, c.kind.health);
        c.m.maxHealth = health;
        c.m.idealState = kStateIdle;
        c.m.idealActivity = kActIdle;
        c.m.idealYaw = c.me.yaw;
        c.m.yawSpeed = 90;
        c.m.thinkMs = kAiThinkMs;
        if (c.me.ai >= kAiDying)
        {
            // (Dead, or dying, in a loaded game: a body.)
            c.m.state = c.m.idealState = kStateDead;
            c.m.deadflag = c.me.ai == kAiDead ? 2 : 1;
            c.m.memory |= kMemoryKilled;
            c.m.started = true;
            return;
        }
        // (One of the people who followed the player, as a save or the map
        // before has it: he follows him still.)
        if (isPerson(c.me.kind) && c.me.ai == 1 && c.m.targetEnt == kAiNone)
            c.m.targetEnt = kAiPlayer;
        // (One of the people, as a save from before kept them: health is
        // what they have lost of it, from 0.)
        if (isPerson(c.me.kind) && c.me.health <= 0)
            c.me.health = static_cast<std::int16_t>(personHealth(c.me.kind) + c.me.health);
        if (c.me.health <= 0)
            c.me.health = static_cast<std::int16_t>(health);
        else if (c.me.ai != kAiIdle && !isPerson(c.me.kind))
            c.m.idealState = kStateAlert;
        // With nothing under it, it falls (MOVETYPE_STEP: the game's
        // monsters are let fall from where they are put -- a monstermaker's
        // over a pit, SF_MONSTER_FALL_TO_GROUND). Not what flies, swims,
        // hangs or is fixed in place, nor one of the people (peopleGround),
        // nor what a scene holds.
        if (!(c.m.capability & kCapFly) && !isPerson(c.me.kind) && !actorStaysPut(c.me.kind) && !actorCentred(c.me) &&
            c.me.script < 0 &&
            !aiHeld(c.a, c.index) && !(c.me.flags & kActorCeiling) && c.w.world != nullptr)
        {
            const bool small = monsterKind(c.me.kind).small;
            const fx lift = small ? kCrouchedFeet : kStandingFeet;
            const fx from[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + lift + fxFromInt(1)};
            const fx to[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + lift - fxFromInt(2)};
            const Trace under = traceAll(*c.w.world, small, from, to);
            // (Nothing, or only someone, under it: someone is no floor.)
            if (!under.startSolid && (under.fraction >= kFractionOne || under.box >= 0))
            {
                c.m.inAir = true;
                c.me.velocity[0] = c.me.velocity[1] = c.me.velocity[2] = 0;
            }
        }
    }

    // ------------------------------------------------------------- the tick

    /// Any actor on a clip its model has for `activity` (LookupActivity,
    /// for those that do not think this way: a barnacle's victim). False if
    /// it has none.
    inline bool actorPlayActivity(ActorWorld &a, ActorState &actor, std::uint8_t activity, bool once)
    {
        const ModelView *model = actorModel(a, actor);
        if (model == nullptr)
            return false;
        int found = -1, total = 0;
        for (int i = 0; i < model->header->clipCount; ++i)
        {
            const ModelClip &clip = model->clips[i];
            if (clipActivity(clip) != activity)
                continue;
            const int weight = clipWeight(clip);
            total += weight;
            if (total == 0 || aiRandomLong(a, 0, total - 1) < weight)
                found = i;
        }
        if (found < 0)
            return false;
        actor.clip = static_cast<std::int16_t>(found);
        actor.frame = 0;
        actor.once = once;
        actor.finished = false;
        actor.frozen = false;
        return true;
    }
    /// The activity of the clip it is on (kActReset if the model does not say).
    inline std::uint8_t actorActivity(const ActorWorld &a, const ActorState &actor)
    {
        const ModelView *model = actorModel(a, actor);
        if (model == nullptr || actor.clip < 0)
            return kActReset;
        return clipActivity(model->clips[actor.clip]);
    }

    inline AiCtx aiCtx(LogicWorld &w, PlayerState &player, int index, AiMonster &m, const AiKind &kind)
    {
        return AiCtx{w, player, *w.actors, *w.actors->ai, index, w.actors->states[index], m, kind};
    }

    inline void aiDamageTarget(AiCtx &c, std::int16_t id, int amount, std::uint32_t type, std::uint8_t group)
    {
        if (amount <= 0)
            return;
        if (id == kAiPlayer)
        {
            if (c.player.health <= 0)
                return;
            playerHurt(c.player, amount, c.me.pos, type);
            c.a.got |= kGotHurt;
            return;
        }
        fx center[3];
        aiCenter(c, center);
        monsterHurt(c.w, id, amount, false, group, type, static_cast<std::int16_t>(c.index), center);
    }

    /// A blow to a monster that thinks this way (from monsterHurt).
    inline void aiActorHurt(LogicWorld &w, PlayerState *player, int index, const AiDamage &d)
    {
        ActorWorld &a = *w.actors;
        AiMonster *m = aiOf(a, index);
        const AiKind *kind = aiKindOf(a.states[index].kind);
        if (m == nullptr || kind == nullptr)
            return;
        // (Whoever is about: the blow's own rules need no player but its
        // place, and a stand-in will do when none is given.)
        static PlayerState nobody;
        AiCtx c = aiCtx(w, player != nullptr ? *player : nobody, index, *m, *kind);
        if (!m->started && m->maxHealth == 0)
            aiInit(c);
        aiTakeDamage(c, d);
    }

    /// Something called it by name (MonsterUse): roused, if it was idle.
    inline void aiMonsterUse(AiCtx &c)
    {
        if (c.m.state == kStateIdle)
            c.m.idealState = kStateAlert;
    }

    /// A script has let it go: it thinks again from scratch, idle
    /// (CineCleanup).
    inline void aiScriptReleased(AiCtx &c)
    {
        aiClearSchedule(c);
        aiRouteClear(c);
        c.m.idealState = kStateIdle;
        c.m.state = kStateIdle;
        c.m.enemy = kAiNone;
        c.m.activity = kActReset;
        c.m.idealActivity = kActIdle;
    }

    /// The model's own events for its clip passed this tick: HandleAnimEvent.
    inline void aiAnimEvents(AiCtx &c)
    {
        const ModelView *model = actorModel(c.a, c.me);
        if (model == nullptr || c.me.clip < 0 || c.kind.animEvent == nullptr)
            return;
        const ModelClip &clip = model->clips[c.me.clip];
        const std::uint32_t before = c.me.lastFrame, now = c.me.frame;
        const bool wrapped = now < before;
        auto passed = [&](int frame)
        {
            const std::uint32_t at = static_cast<std::uint32_t>(frame) << 8;
            return wrapped ? (at > before || at <= now)
                           : ((before < at && at <= now) || (at == 0 && before == 0 && now > 0));
        };
        bool own = false;
        for (int e = 0; e < clip.eventCount; ++e)
        {
            const ModelEvent &event = model->events[clip.firstEvent + e];
            if (event.kind != kEventMonster)
                continue;
            own = true;
            if (passed(event.frame))
                c.kind.animEvent(c, static_cast<int>(event.target));
        }
        // (A model cooked before its own events were kept: the kind's list.)
        if (!own && !aiModelHasActivities(*model))
        {
            for (const AiClipEvent *e = c.kind.clipEvents; e != nullptr && e->clip != nullptr; ++e)
            {
                if (nameHash(e->clip) == clip.name && passed(e->frame))
                    c.kind.animEvent(c, e->event);
            }
        }
    }

    /// In the air (a leap, a hop): it falls, and lands; and a touch that is
    /// set finds what it runs into.
    inline void aiAirTick(AiCtx &c, int ms)
    {
        const bool small = monsterHull(c.me.kind) == kWalkCrouched;
        const fx feet = small ? kCrouchedFeet : kStandingFeet;
        const fx from[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + feet};
        fx to[3];
        for (int k = 0; k < 3; ++k)
            to[k] = from[k] + static_cast<fx>((static_cast<std::int64_t>(c.me.velocity[k]) * ms) / 1000);
        c.me.velocity[2] -= static_cast<fx>((static_cast<std::int64_t>(gravityOf(c.player)) * ms) / 1000);
        const Trace t = detail::walkerTrace(*c.w.world, &c.a, c.index, small, from, to);
        bool landed = false;
        const fx was[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2]};
        if (!t.startSolid)
        {
            c.me.pos[0] = t.endPos[0];
            c.me.pos[1] = t.endPos[1];
            c.me.pos[2] = t.endPos[2] - feet;
        }
        // (The player's box stops it too, as the engine's does: no further
        // across, and down it comes.)
        {
            fx lo[3], hi[3];
            targetBox(c.me, lo, hi);
            if (c.player.health > 0 && detail::playerTouchesBox(c.player, lo, hi, 0))
            {
                if (c.m.touchArmed && c.kind.touch != nullptr)
                    c.kind.touch(c, kAiPlayer);
                c.me.pos[0] = was[0];
                c.me.pos[1] = was[1];
                c.me.velocity[0] = c.me.velocity[1] = 0;
                if (c.me.velocity[2] > 0)
                    c.me.velocity[2] = 0;
            }
        }
#ifndef HLDS_CHILD
        if (!t.startSolid && t.fraction < kFractionOne && t.box >= 0 && t.normal[2] > kFloorNormalZ &&
            t.box < c.w.world->boxCount)
        {
            // On someone's head: not a floor (the game's SV_CheckBottom asks
            // the world alone). Off it, outwards from their middle, and on
            // down.
            const SolidBox &under = c.w.world->boxes[t.box];
            fx out[3] = {c.me.pos[0] - (under.mins[0] + under.maxs[0]) / 2,
                         c.me.pos[1] - (under.mins[1] + under.maxs[1]) / 2, 0};
            const fx length = fxLength(out);
            if (length < fxFromInt(1))
            {
                out[0] = kFxOne;
                out[1] = 0;
            }
            else
            {
                out[0] = static_cast<fx>((static_cast<std::int64_t>(out[0]) << kFxShift) / length);
                out[1] = static_cast<fx>((static_cast<std::int64_t>(out[1]) << kFxShift) / length);
            }
            c.me.velocity[0] = out[0] * 120;
            c.me.velocity[1] = out[1] * 120;
            if (c.me.velocity[2] < 0)
                c.me.velocity[2] = 0;
        }
        else
#endif
        if (t.startSolid || t.fraction < kFractionOne)
        {
            if (t.startSolid || t.normal[2] > kFloorNormalZ)
            {
                landed = c.me.velocity[2] <= 0 || t.startSolid;
            }
            else
            {
                c.me.velocity[0] = c.me.velocity[1] = 0;
                if (c.me.velocity[2] > 0)
                    c.me.velocity[2] = 0;
            }
        }
        if (c.m.touchArmed && c.kind.touch != nullptr)
        {
            fx mins[3], maxs[3];
            targetBox(c.me, mins, maxs);
            if (c.player.health > 0 && detail::playerTouchesBox(c.player, mins, maxs, fxFromInt(2)))
                c.kind.touch(c, kAiPlayer);
            for (int i = 0; c.m.touchArmed && i < c.a.count; ++i)
            {
                const ActorState &other = c.a.states[i];
                if (i == c.index || !other.active || !(other.flags & kActorSolid) || !canBeHurt(other.kind))
                    continue;
                fx lo[3], hi[3];
                targetBox(other, lo, hi);
                if (lo[0] <= maxs[0] + fxFromInt(2) && hi[0] >= mins[0] - fxFromInt(2) && lo[1] <= maxs[1] + fxFromInt(2) &&
                    hi[1] >= mins[1] - fxFromInt(2) && lo[2] <= maxs[2] + fxFromInt(2) && hi[2] >= mins[2] - fxFromInt(2))
                    c.kind.touch(c, static_cast<std::int16_t>(i));
            }
        }
        if (landed)
        {
            c.m.inAir = false;
            c.me.velocity[0] = c.me.velocity[1] = c.me.velocity[2] = 0;
            if (c.me.ai == kAiLeap)
                c.me.ai = kAiChase;
        }
    }

    /// Off the ground, at `velocity` (FL_ONGROUND cleared).
    inline void aiLeap(AiCtx &c, const fx *velocity)
    {
        for (int k = 0; k < 3; ++k)
            c.me.velocity[k] = velocity[k];
        c.m.inAir = true;
        if (c.me.ai < kAiDying)
            c.me.ai = kAiLeap;
    }

    inline void aiFlyTick(AiCtx &c, int ms); // (HlAliens.hpp: what flies)

    /// What a walker held up is held up by: the actor whose box its own
    /// would be in a step on along the way it faces, or the player
    /// (kAiPlayer); kAiNone for the world (the game's trace_ent).
    inline std::int16_t aiBlocker(const AiCtx &c)
    {
        const MonsterKind &kind = monsterKind(c.me.kind);
        fx sine, cosine;
        fxSinCos(c.m.idealYaw, sine, cosine);
        const fx at[3] = {c.me.pos[0] + fxMul(cosine, kWalkStep), c.me.pos[1] + fxMul(sine, kWalkStep), c.me.pos[2]};
        std::int16_t found = kAiNone;
        fx nearest = 0;
        if (c.player.health > 0 && detail::boxInPlayer(c.player, at, kind.half, kind.height))
        {
            found = kAiPlayer;
            nearest = aiDist2D(c.player.origin, c.me.pos);
        }
        for (int i = 0; i < c.a.count; ++i)
        {
            const ActorState &other = c.a.states[i];
            if (i == c.index || !other.active || !(other.flags & kActorSolid))
                continue;
            fx lo[3], hi[3];
            targetBox(other, lo, hi);
            if (lo[0] >= at[0] + kind.half || hi[0] <= at[0] - kind.half || lo[1] >= at[1] + kind.half ||
                hi[1] <= at[1] - kind.half || lo[2] >= at[2] + kind.height || hi[2] <= at[2])
                continue;
            const fx far = aiDist2D(other.pos, c.me.pos);
            if (found == kAiNone || far < nearest)
            {
                found = static_cast<std::int16_t>(i);
                nearest = far;
            }
        }
        return found;
    }

    /// IsMoving, of what is in the way: a monster on a route, or an actor
    /// walking to a script's mark.
    inline bool aiBlockerMoving(const AiCtx &c, std::int16_t id)
    {
        if (id < 0 || id >= c.a.count)
            return false;
        const AiMonster *other = aiFind(c.a, id);
        return (other != nullptr && other->movementGoal != kMoveGoalNone) ||
               c.a.states[id].scriptPhase == kActorMoving;
    }

    /// FRefreshRoute, as the walker has it: its way planned afresh from
    /// where it stands (no way at all fails the move: kWalkNoWay).
    inline void aiReplan(AiCtx &c)
    {
        c.m.moveAsked = false;
        c.me.thinkMs = 0;
        c.me.hasGoal = false;
        c.me.goalNode = c.me.fromNode = -1;
        c.me.stuckMs = 0;
    }

    /// No way round what is in the way (Move, FTriangulate failed): stopped,
    /// and the route made again once, a tenth of a second on -- twice in a
    /// row and that is remembered; then the task fails.
    inline void aiMoveNoWayRound(AiCtx &c)
    {
        aiStop(c);
        if (c.m.round.waitMs > 0 && !aiHasMemory(c, kMemoryMoveFailed))
        {
            aiReplan(c);
            if (c.ai.now - c.m.moveWaitFinished < 200)
                aiRemember(c, kMemoryMoveFailed);
            c.m.moveWaitFinished = c.ai.now + 100;
            return;
        }
        aiTaskFail(c);
        aiRouteClear(c);
    }

    /// A way round asked for (FTriangulate), towards `far`: worked out
    /// outside the frame where there is a worker to do it, here otherwise.
    /// True once it is answered (into round.apex; `found`).
    inline bool aiTriangulate(AiCtx &c, const fx *far, bool &found)
    {
        AiMoveRound &round = c.m.round;
        const MonsterKind &kind = monsterKind(c.me.kind);
        ThinkQueue *q = c.a.think;
        ThinkSlot *slot = q != nullptr && q->deferred && c.index < q->count ? &q->slots[c.index] : nullptr;
        if (slot == nullptr)
        {
            round.asking = false;
            found = walkTriangulate(*c.w.world, &c.a, c.index, aiHull(c), c.me.pos, far, kind.half * 2,
                                    round.checked, round.apex);
            return true;
        }
        // (Asked before a game was saved: the question was not kept with
        // it, and is asked again.)
        if (round.asking && slot->state == kThinkFree)
            round.asking = false;
        if (round.asking)
        {
            if (slot->kind != kThinkDetour || slot->state != kThinkDone)
                return false;
            slot->state = kThinkFree;
            round.asking = false;
            found = slot->found;
            for (int k = 0; k < 3; ++k)
                round.apex[k] = slot->out[k];
            return true;
        }
        // (Asked when the slot is not busy with a plan of the walker's own.)
        if (slot->state == kThinkAsked || slot->state == kThinkWorking)
            return false;
        slot->kind = kThinkDetour;
        slot->state = kThinkAsked;
        for (int k = 0; k < 3; ++k)
            slot->at[k] = far[k];
        slot->small = static_cast<std::int16_t>(aiHull(c));
        slot->reach = static_cast<std::int16_t>(fxToInt(kind.half * 2));
        slot->least = static_cast<std::int16_t>(fxToInt(round.checked));
        slot->basis = -1;
        round.asking = true;
        return false;
    }

    /// CheckLocalMove, for the others: the first actor whose box the
    /// walker's own would meet on the next `look` units towards `aim`, and
    /// how far on that is (`checked`). Not the one it is going for (a move
    /// into it is the move it means). Not the world, which its way is
    /// planned round; nor the player, whom it walks up to and stops in front
    /// of (walkTick). kAiNone if none.
    inline std::int16_t aiLookAhead(const AiCtx &c, const fx *aim, fx look, fx &checked)
    {
        const fx d[3] = {aim[0] - c.me.pos[0], aim[1] - c.me.pos[1], 0};
        const fx length = fxLength(d);
        if (length <= 0)
            return kAiNone;
        if (look > length)
            look = length;
        const MonsterKind &kind = monsterKind(c.me.kind);
        const fx middle = kind.height / 2;
        const fx from[3] = {c.me.pos[0], c.me.pos[1], c.me.pos[2] + middle};
        const fx to[3] = {from[0] + static_cast<fx>((static_cast<std::int64_t>(d[0]) * look) / length),
                          from[1] + static_cast<fx>((static_cast<std::int64_t>(d[1]) * look) / length), from[2]};
        const std::int16_t target = c.m.movementGoal == kMoveGoalEnemy    ? c.m.enemy
                                    : c.m.movementGoal == kMoveGoalTarget ? c.m.targetEnt
                                                                          : kAiNone;
        std::int16_t found = kAiNone;
        std::int64_t best = kFractionOne + 1;
        auto tryBox = [&](std::int16_t id, const fx *lo, const fx *hi)
        {
            // (Grown by the walker's own size: its middle against that.)
            const fx bl[3] = {lo[0] - kind.half, lo[1] - kind.half, lo[2] - middle};
            const fx bh[3] = {hi[0] + kind.half, hi[1] + kind.half, hi[2] + middle};
            std::int64_t enter = 0;
            if (lineEntersBox(from, to, bl, bh, enter) && enter < best)
            {
                best = enter;
                found = id;
            }
        };
        for (int i = 0; i < c.a.count; ++i)
        {
            const ActorState &other = c.a.states[i];
            if (i == c.index || i == target || !other.active || !(other.flags & kActorSolid))
                continue;
            fx lo[3], hi[3];
            targetBox(other, lo, hi);
            tryBox(static_cast<std::int16_t>(i), lo, hi);
        }
        if (found != kAiNone)
            checked = static_cast<fx>((static_cast<std::int64_t>(look) * best) >> kFractionShift);
        return found;
    }

    /// Move, its way blocked by `blocker` `checked` units on: it stops;
    /// waits for one that is on its way somewhere (not twice within three
    /// seconds); otherwise goes round it to `aim` (FTriangulate), or if
    /// there is no way round, makes its route again. False while it waits
    /// or works out the way round: no step this frame.
    inline bool aiHeldUp(AiCtx &c, std::int16_t blocker, fx checked, const fx *aim)
    {
        AiMoveRound &round = c.m.round;
        aiStop(c);
        if (c.m.activity != c.m.idealActivity)
            aiSetActivity(c, c.m.idealActivity);
        if (blocker != kAiPlayer && round.waitMs > 0 && aiBlockerMoving(c, blocker) &&
            c.ai.now - c.m.moveWaitFinished > 3000)
        {
            c.m.moveWaitFinished = c.ai.now + round.waitMs;
            return false;
        }
        // (Held up at its nose by the world or the player: a way round is
        // looked for as it stops, not every frame it stands there.)
        if (blocker < 0 && c.m.moveBlockedMs > kAiThinkMs)
        {
            if (c.m.moveBlockedMs > 2000)
            {
                aiRemember(c, kMemoryMoveFailed);
                aiTaskFail(c);
                aiRouteClear(c);
            }
            return false;
        }
        for (int k = 0; k < 3; ++k)
            round.apex[k] = aim[k];
        round.checked = checked;
        bool found = false;
        if (!aiTriangulate(c, aim, found))
            return false;
        if (!found)
        {
            // (The game's look ahead finds walls far off and plans round
            // them; what the port's walker meets at its nose is its own
            // planning's to mend -- walkTick plans again after a second of
            // getting nowhere -- and the player steps aside. Only another
            // in the way fails the move at once.)
            if (blocker >= 0)
            {
                aiMoveNoWayRound(c);
            }
            else if (c.m.moveBlockedMs > 2000)
            {
                aiRemember(c, kMemoryMoveFailed);
                aiTaskFail(c);
                aiRouteClear(c);
            }
            return false;
        }
        round.detouring = true;
        aiReplan(c);
        return true;
    }

    /// Its route walked, a frame of it (Move).
    inline void aiMoveTick(AiCtx &c, int ms)
    {
        if (c.m.capability & kCapFly)
        {
            aiFlyTick(c, ms);
            return;
        }
        if (c.m.movementGoal == kMoveGoalNone || c.m.moveWaitFinished > c.ai.now || c.m.inAir)
            return;
        AiMoveRound &round = c.m.round;
        fx dest[3];
        aiMoveDest(c, dest);
        fx speed = c.m.groundSpeed > 0 ? c.m.groundSpeed : fxFromInt(100);
        const fx far = aiDist2D(dest, c.me.pos);
        // At the goal: what a fifth of a second of its pace leaves.
        fx near = speed / 5;
        if (near < fxFromInt(8))
            near = fxFromInt(8);
        const fx dz = dest[2] - c.me.pos[2];
        if (far < near && dz < fxFromInt(72) && dz > -fxFromInt(72))
        {
            aiMovementComplete(c);
            return;
        }
        // A way round being worked out: it stands, stopped, until it is.
        if (round.asking)
        {
            bool found = false;
            if (!aiTriangulate(c, round.apex, found))
                return;
            if (!found)
            {
                aiMoveNoWayRound(c);
                return;
            }
            round.detouring = true;
            aiReplan(c);
        }
        // On a detour: its waypoint first (MONSTER_CUT_CORNER_DIST), then
        // on with the route from there.
        if (round.detouring)
        {
            if (aiDist2D(round.apex, c.me.pos) <= fxFromInt(8))
            {
                round.detouring = false;
                aiReplan(c);
            }
            else
            {
                for (int k = 0; k < 3; ++k)
                    dest[k] = round.apex[k];
            }
        }
        // Ten times a second, as the game moves it: a look along its way
        // for anyone in it (DIST_TO_CHECK).
        round.lookMs -= ms;
        if (round.lookMs <= 0)
        {
            round.lookMs = kAiThinkMs;
            const fx *aim = c.me.hasGoal ? c.me.goal : dest;
            fx checked = 0;
            const std::int16_t blocker = aiLookAhead(c, aim, fxFromInt(200), checked);
            if (blocker != kAiNone && !aiHeldUp(c, blocker, checked, aim))
                return;
        }
        const MonsterKind &kind = monsterKind(c.me.kind);
        // (Move: MakeIdealYaw to the way it is going, and ChangeYaw at its
        // own yaw speed -- aiTurnTick, after this.)
        const int went = walkTick(c.a, *c.w.world, c.index, aiHull(c), dest, speed, ms, &c.player, kind.half,
                                  kind.height, round.detouring || c.m.movementGoal == kMoveGoalLocation,
                                  &c.m.idealYaw);
        if (went != kWalkNoWay)
            aiChangeYaw(c);
        if (went == kWalkMoved)
        {
            // (MoveExecute: the clip of going, once it goes.)
            c.m.moveBlockedMs = 0;
            if (c.m.idealActivity != c.m.movementActivity)
                c.m.idealActivity = c.m.movementActivity;
            if (c.m.activity != c.m.idealActivity)
                aiSetActivity(c, c.m.idealActivity);
            return;
        }
        if (went == kWalkNoWay)
        {
            // (Its way still being worked out, outside the frame.)
            const ThinkQueue *q = c.a.think;
            if (q != nullptr && q->deferred && c.index < q->count && q->slots[c.index].state != kThinkFree &&
                q->slots[c.index].kind == kThinkPlan)
                return;
            if (!c.m.moveAsked)
            {
                c.m.moveAsked = true;
                return;
            }
            aiTaskFail(c);
            aiRouteClear(c);
            return;
        }
        // Held up where it stands (what it ran into stepped into its way).
        c.m.moveBlockedMs += ms;
        const std::int16_t blocker = aiBlocker(c);
        // By what it is going for, which is as good as there.
        if (c.m.movementGoal == kMoveGoalEnemy || c.m.movementGoal == kMoveGoalTarget)
        {
            const std::int16_t goal = c.m.movementGoal == kMoveGoalEnemy ? c.m.enemy : c.m.targetEnt;
            AiTarget t;
            if (aiTargetOf(c.w, c.player, goal, t) && (blocker == goal || aiTargetInReach(c, t)))
            {
                aiMovementComplete(c);
                return;
            }
        }
        aiHeldUp(c, blocker, 0, c.me.hasGoal ? c.me.goal : dest);
    }

    /// MonsterThink: ten times a second.
    inline void aiMonsterThink(AiCtx &c)
    {
        c.m.turning = false;
        // (Where its eyes are: what it can see of the map, and whoever
        // looks at it, go by that.)
        {
            fx eye[3];
            aiEye(c, eye);
            c.m.leaf = static_cast<std::int16_t>(c.w.world->nodes != nullptr ? worldLeaf(*c.w.world, eye) : 0);
        }
        if (c.kind.think != nullptr)
        {
            c.kind.think(c);
            return;
        }
        if (c.me.switched != 0)
        {
            c.me.switched = 0;
            // (One of the people's Use is his FollowerUse: only the player's
            // counts, and that is USE -- HlPeople.hpp.)
            if (!isPerson(c.me.kind))
                aiMonsterUse(c);
        }
        aiRunAI(c);
        if (c.kind.runAI != nullptr)
            c.kind.runAI(c);
        // Idle and through its clip: another way of being idle.
        if (c.m.state != kStateScript && c.m.state != kStateDead && c.m.activity == kActIdle && c.m.seqFinished)
        {
            const ModelView *model = actorModel(c.a, c.me);
            const bool loops = model != nullptr && c.me.clip >= 0 && (model->clips[c.me.clip].flags & kClipLoops);
            const int clip = aiLookupActivity(c, kActIdle, !loops);
            if (clip >= 0)
                aiPlayClip(c, clip, true);
        }
    }

    /// One frame of a monster that thinks this way.
    inline void aiTick(LogicWorld &w, PlayerState &player, int index, int ms)
    {
        ActorWorld &a = *w.actors;
        AiMonster *m = aiOf(a, index);
        const AiKind *kind = aiKindOf(a.states[index].kind);
        if (m == nullptr || kind == nullptr)
            return;
        AiCtx c = aiCtx(w, player, index, *m, *kind);
        if (m->maxHealth == 0)
            aiInit(c);
        if (c.me.ai == kAiDead && !c.m.inAir)
            return;
        if (!m->started)
        {
            // (MonsterInitThink: a tenth of a second after it is there --
            // a script's or not.)
            m->thinkMs -= ms;
            if (m->thinkMs > 0)
                return;
            aiStartMonster(c);
        }
        // A script's while it has it (scripted_sequence: HlLogic.hpp) --
        // but what the map set it to set off is looked for all the same, as
        // RunAI does in SCRIPT state: c1a1f's freak zombie, held by a script
        // nothing breaks, is ended by its own half-health trigger.
        if (c.me.script >= 0)
        {
            m->scripted = true;
            aiCheckTrigger(c);
            return;
        }
        if (m->scripted)
        {
            m->scripted = false;
            aiScriptReleased(c);
        }
        // What its clip has done: through it, or round it.
        const bool wrapped = c.me.frame < c.me.lastFrame;
        if (c.me.finished || wrapped)
            m->seqFinished = true;
        aiAnimEvents(c);
        if (kind->frame != nullptr)
            kind->frame(c, ms);
        // (A soldier on a rope thinks -- he looks out, turns and shoots --
        // but the rope moves him: HlMilitary.hpp's ropeDown.)
        const bool roped = aiHeld(a, index) && !(a.ai->held[index] & 1);
        if (aiHeld(a, index) && !roped)
            return; // (a barnacle's: it is where the barnacle has it)
        {
            const AiSpentScope spent(2);
            if (m->inAir && !roped)
                aiAirTick(c, ms);
            // (Turned after it moves: a think clears `turning`, and the
            // frame after one would otherwise not turn while it walks.)
            if (!roped)
                aiMoveTick(c, ms);
            aiTurnTick(c, ms);
        }
        m->thinkMs -= ms;
        if (m->thinkMs <= 0)
        {
            m->thinkMs += kAiThinkMs;
            if (m->thinkMs <= 0)
                m->thinkMs = kAiThinkMs;
            aiMonsterThink(c);
        }
    }

    /// The whole of the monsters' world, a frame on: the time, the sounds,
    /// and what of the player is heard.
    inline void aiWorldTick(LogicWorld &w, PlayerState &player, int ms)
    {
        if (w.actors == nullptr || w.actors->ai == nullptr)
            return;
        AiWorld &ai = *w.actors->ai;
        ai.now += ms;
        ai.player = &player;
        {
            const fx eye[3] = {player.origin[0], player.origin[1], player.origin[2] + player.viewHeight};
            ai.playerEyeLeaf = static_cast<std::int16_t>(w.world != nullptr && w.world->nodes != nullptr
                                                             ? worldLeaf(*w.world, eye)
                                                             : 0);
        }
        ai.playerLeaf = static_cast<std::int16_t>(w.world != nullptr && w.world->nodes != nullptr
                                                      ? worldLeaf(*w.world, player.origin)
                                                      : 0);
        soundEntTick(ai.sounds, ai.now, ms);
        const fx flat[3] = {player.velocity[0], player.velocity[1], 0};
        // (Jump held: a hundred more, on the ground or not -- the game's
        // IN_JUMP.)
        playerSoundTick(ai.sounds, player.origin, player.onGround ? fxToInt(fxLength(flat)) : -1, player.jumpHeld,
                        player.health <= 0 || unseen(player), ms);
        // A landing from a fall is heard as loud as the fall was fast, for a
        // fifth of a second (CBasePlayer::PostThink).
        if (player.landed > fxFromInt(64) && player.health > 0 && !unseen(player))
            soundInsert(ai.sounds, ai.now, kSoundPlayer, player.origin, fxToInt(player.landed), 200);
    }
} // namespace hldata
