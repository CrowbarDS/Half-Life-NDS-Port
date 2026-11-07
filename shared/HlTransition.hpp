#pragma once

// Going from one map to the next.
//
// A Half-Life chapter is a string of maps joined end to end. Where two meet,
// each has an info_landmark of the same name at what is the same place in
// both, and the player who walks into a trigger_changelevel in one arrives in
// the other standing as far from its landmark as they were from the first's,
// moving the same way and looking the same way.
//
// The DS cannot hold two maps at once, so everything that crosses is put in
// one small struct -- the Arrival -- before the old map is let go, and applied
// once the new one is up. Names mean nothing across maps as ids (those are
// per map), so everything here that names something does so by nameHash().
//
// Also here: the GLOBAL STATES, the few named switches that outlive a map
// (env_global), which travel in the same struct.
//
// std-only and inline: the host tests run it.

#include "HlMonsters.hpp"

namespace hldata
{
    /// What crosses a level change.
    struct Arrival
    {
        bool valid = false;        ///< false: start the map at its own player start
        std::uint32_t landmark = 0; ///< nameHash of the landmark both maps share
        fx offset[3] = {0, 0, 0};  ///< the player's origin, less the old map's landmark
        fx velocity[3] = {0, 0, 0};
        std::uint16_t yaw = 0;
        std::int16_t pitch = 0;
        bool ducked = false;
        std::int32_t health = 100;
        Inventory inv;
        /// A name to fire in the new map, and how long after arriving
        /// ("changetarget" and "changedelay"); 0 = none.
        std::uint32_t changeTarget = 0;
        std::int32_t changeDelayMs = 0;
        /// The track train the player is riding, by its global name's hash;
        /// 0 = none. The next map has a train of the same name, which takes
        /// this one's place: where it is from the landmark, which way it
        /// faces and how fast it is going.
        std::uint32_t train = 0;
        fx trainOffset[3] = {0, 0, 0};
        std::uint16_t trainYaw = 0;
        fx trainSpeed = 0;
        bool trainSelfDriven = false;
        /// The actors that come along: those with a name who stand near the
        /// landmark (the guard who meets the tram is waiting in the map
        /// before the one he opens its door in). Each as where it is from
        /// the landmark and what it is.
        struct Visitor
        {
            std::uint32_t model = 0; ///< nameHash of its model's name; 0 = slot unused
            std::uint32_t name = 0;  ///< nameHash of its own
            fx offset[3] = {0, 0, 0};
            std::uint16_t yaw = 0;
            std::uint8_t body = 0;
            std::uint8_t kind = 0;
            std::uint8_t flags = 0;
            std::uint8_t light = 24;
        };
        Visitor visitors[kMaxVisitors];
    };

    // One of the people who comes along keeps his mind's part (the game
    // carries the whole of him): in what a Visitor has to spare, so that
    // what a save keeps of an Arrival keeps its form. `light` is 0..31
    // (drawn no brighter by any build); above it, whether he follows the
    // player, whether the player provoked him, and that his health is in the
    // low six bits of `yaw` (a third of a degree of it, which a build before
    // turns him by).
    inline constexpr std::uint8_t kVisitorLight = 31;
    inline constexpr std::uint8_t kVisitorFollows = 32;
    inline constexpr std::uint8_t kVisitorProvoked = 64;
    inline constexpr std::uint8_t kVisitorMind = 128;
    inline constexpr std::uint16_t kVisitorHealth = 63;

    /// A visitor slot that is not someone: where a func_train that crosses
    /// is HEADING (the corner it was on its way to), from the landmark; its
    /// name is the train's. No model has this name, so a build that does not
    /// know it lets it be. (In a slot, so that what a save keeps of an
    /// Arrival keeps its form.)
    inline constexpr std::uint32_t kTrainHeadingVisitor = nameHash("#train heading");

    /// What a func_train is known by across maps: its global name; or, in a
    /// map converted before that was kept (extra[3] 0), its own name -- the
    /// lift is called the same on both sides.
    inline std::uint32_t trainCrossName(const LogicWorld &w, const LogicRecord &train)
    {
        if (train.extra[3] != 0)
            return train.extra[3];
        return w.nameHashes != nullptr && train.name != 0 ? w.nameHashes[train.name] : 0;
    }

    /// How near the landmark an actor has to be to come along.
    inline constexpr fx kVisitorRange = fxFromInt(2048);

    /// The landmark called `hash`: the first of that name, as in the game.
    inline const Landmark *findLandmark(const Landmark *landmarks, std::uint32_t count,
                                        std::uint32_t hash)
    {
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (landmarks[i].hash == hash)
                return &landmarks[i];
        }
        return nullptr;
    }

    /// The name id that `hash` has in this map; 0 if nothing here is called
    /// that.
    inline std::uint16_t nameIdOfHash(const std::uint32_t *nameHashes, std::uint32_t count,
                                      std::uint32_t hash)
    {
        if (hash == 0)
            return 0;
        for (std::uint32_t i = 1; i < count; ++i)
        {
            if (nameHashes[i] == hash)
                return static_cast<std::uint16_t>(i);
        }
        return 0;
    }

    /// Where a lift that came from the map before goes on to, when no corner
    /// of this map is where the corner it was heading to was: the two maps'
    /// corners need not line up (c1a3a's lift02c is 24 units higher than
    /// c1a3b's), and in the game the lift goes on along this map's own path.
    /// So: the corner at the far end of the stretch of this map's path the
    /// lift is on, the way it was going (`way`, from its middle `at`, both in
    /// this map); else the one after the corner it is at. kNoCorner if it is
    /// on no path of this map.
    inline std::uint16_t liftCornerAhead(const LogicWorld &w, const fx *at, const fx *way)
    {
        // (In whole units, so that the squares fit 64 bits.)
        std::int64_t p[3], v[3];
        for (int a = 0; a < 3; ++a)
        {
            p[a] = fxToInt(at[a]);
            v[a] = fxToInt(way[a]);
        }
        for (int c = 0; c < w.cornerCount; ++c)
        {
            const std::uint16_t next = w.corners[c].next;
            if (next == kNoCorner || next >= w.cornerCount)
                continue;
            std::int64_t d[3], r[3];
            for (int a = 0; a < 3; ++a)
            {
                d[a] = fxToInt(w.corners[next].pos[a]) - fxToInt(w.corners[c].pos[a]);
                r[a] = p[a] - fxToInt(w.corners[c].pos[a]);
            }
            const std::int64_t dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            const std::int64_t along = r[0] * d[0] + r[1] * d[1] + r[2] * d[2];
            // On this stretch (from its start, not yet at its end), going
            // the way it goes...
            if (dd == 0 || along < 0 || along >= dd || v[0] * d[0] + v[1] * d[1] + v[2] * d[2] <= 0)
                continue;
            // ... and within two units of it.
            const std::int64_t cross[3] = {r[1] * d[2] - r[2] * d[1], r[2] * d[0] - r[0] * d[2],
                                           r[0] * d[1] - r[1] * d[0]};
            if (cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2] <= 4 * dd)
                return next;
        }
        std::uint16_t nearest = kNoCorner;
        std::int64_t best = 8 * 8 + 1;
        for (int c = 0; c < w.cornerCount; ++c)
        {
            std::int64_t sum = 0;
            for (int a = 0; a < 3; ++a)
            {
                const std::int64_t d = fxToInt(w.corners[c].pos[a]) - p[a];
                sum += d * d;
            }
            if (sum < best)
            {
                best = sum;
                nearest = static_cast<std::uint16_t>(c);
            }
        }
        return nearest == kNoCorner ? kNoCorner : w.corners[nearest].next;
    }

    /// What the console's changelevel carries to the map it goes to: what a
    /// level change carries of the player (health, the suit, weapons, ammo
    /// and clips, armour), with no landmark -- the player starts at the
    /// map's own start -- and no one with them, nothing fired, no train.
    inline Arrival consoleArrival(const PlayerState &player)
    {
        Arrival arrival;
        arrival.valid = true;
        arrival.landmark = 0;
        arrival.health = player.health;
        arrival.inv = player.inv;
        return arrival;
    }

    /// Pack what crosses, for the ChangeLevel record `index` of the map being
    /// left. The view angles are the caller's to fill in: they are not part of
    /// the movement state.
    /// `from`, if given (kMaxVisitors of them): which actor each of the
    /// visitors is of this map, -1 for none -- those who go are to be taken
    /// out of it when it is kept for coming back to (HlMapStore.hpp).
    inline Arrival makeArrival(const LogicWorld &w, const Landmark *landmarks,
                               std::uint32_t landmarkCount, const PlayerState &player, int index,
                               const ModelRef *modelRefs = nullptr, std::int16_t *from = nullptr)
    {
        std::int16_t who[kMaxVisitors];
        for (std::int16_t &one : who)
            one = -1;
        const LogicRecord &record = w.records[index];
        Arrival arrival;
        arrival.valid = true;
        arrival.landmark = record.extra[1];
        const Landmark *here = findLandmark(landmarks, landmarkCount, arrival.landmark);
        for (int a = 0; a < 3; ++a)
        {
            arrival.offset[a] = player.origin[a] - (here != nullptr ? here->origin[a] : 0);
            arrival.velocity[a] = player.velocity[a];
        }
        // A map without the landmark it names cannot say where the player is
        // relative to anything: they will start at the next map's own start.
        if (here == nullptr)
            arrival.landmark = 0;
        arrival.ducked = player.ducked;
        arrival.health = player.health;
        arrival.inv = player.inv;
        // The train under the player's feet, if it is one that is known by
        // name in the next map.
        for (int i = 0; i < w.recordCount && here != nullptr; ++i)
        {
            const LogicRecord &train = w.records[i];
            // "Under their feet" is "they are in it": a passenger in the
            // middle of a jump is a passenger still.
            if (train.kind != static_cast<std::uint8_t>(LogicKind::TrackTrain) ||
                train.extra[2] == 0 || train.brush == kNoBrush ||
                !detail::playerAboard(w, player, train.brush))
                continue;
            const BrushState &car = w.brushStates[train.brush];
            arrival.train = train.extra[2];
            for (int a = 0; a < 3; ++a)
                arrival.trainOffset[a] = car.pos[a] - here->origin[a];
            arrival.trainYaw = car.angles[kYaw];
            arrival.trainSpeed = w.states[i].speed;
            arrival.trainSelfDriven = w.states[i].running;
            break;
        }
        if (w.actors != nullptr && here != nullptr && modelRefs != nullptr)
        {
            // The nearest few, if there are more than can come.
            fx far[kMaxVisitors];
            int taken = 0;
            for (int i = 0; i < w.actors->count; ++i)
            {
                const ActorState &actor = w.actors->states[i];
                // (One of the people who follows the player comes, name or
                // none; anyone else by name.)
                const bool person = (actor.kind == static_cast<std::uint8_t>(ActorKind::Scientist) ||
                                     actor.kind == static_cast<std::uint8_t>(ActorKind::Guard)) &&
                                    actor.ai < 5;
                const bool follows = person && actor.ai == 1 && actor.script < 0;
                // (Never one dead or dying: on the other side it would be
                // made again whole, and stand up -- the zombie by c1a1's
                // landmark.)
                const bool minded = actorThinks(actor.kind);
                if (!actor.active || (actor.nameHash == 0 && !follows) || actor.model >= w.actors->modelCount ||
                    (minded && actor.ai >= kAiDying))
                    continue;
                const fx d[3] = {actor.pos[0] - here->origin[0], actor.pos[1] - here->origin[1],
                                 actor.pos[2] - here->origin[2]};
                if (d[0] > kVisitorRange || d[0] < -kVisitorRange || d[1] > kVisitorRange ||
                    d[1] < -kVisitorRange || d[2] > kVisitorRange || d[2] < -kVisitorRange)
                    continue;
                // And only whom the landmark can see, as in the game: someone
                // two rooms away has no place at the same offset next door.
                if (w.world != nullptr)
                {
                    const fx chest[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36)};
                    if (!leafSees(*w.world, worldLeaf(*w.world, here->origin),
                                  worldLeaf(*w.world, chest)))
                        continue;
                }
                // (Those who follow first: as if nearer than anyone.)
                const fx distance = fxLength(d) - (follows ? 4 * kVisitorRange : 0);
                int at = taken < kMaxVisitors ? taken : kMaxVisitors - 1;
                if (taken >= kMaxVisitors && distance >= far[at])
                    continue;
                while (at > 0 && far[at - 1] > distance)
                {
                    arrival.visitors[at] = arrival.visitors[at - 1];
                    far[at] = far[at - 1];
                    who[at] = who[at - 1];
                    --at;
                }
                far[at] = distance;
                who[at] = static_cast<std::int16_t>(i);
                Arrival::Visitor &visitor = arrival.visitors[at];
                visitor.model = nameHash(modelRefs[actor.model].name);
                visitor.name = actor.nameHash;
                for (int a = 0; a < 3; ++a)
                    visitor.offset[a] = d[a];
                visitor.yaw = actor.yaw;
                visitor.body = actor.body;
                visitor.kind = actor.kind;
                visitor.flags = actor.flags;
                visitor.light = static_cast<std::uint8_t>(actor.light > kVisitorLight ? kVisitorLight : actor.light);
                if (minded)
                {
                    // Its health (absolute once it has thought: HlAiCore's
                    // aiInit; before, for one of the people what he has lost
                    // of it, for a monster nothing yet -- whole).
                    int health = actor.health;
                    const AiMonster *mind = w.actors->ai != nullptr ? aiFind(*w.actors, i) : nullptr;
                    if (health <= 0 && (mind == nullptr || mind->maxHealth == 0))
                    {
                        const AiKind *kind = aiKindOf(actor.kind);
                        health = isPerson(actor.kind) ? health + personHealth(actor.kind)
                                                      : (kind != nullptr ? kind->health : 1);
                    }
                    health = health < 1 ? 1 : (health > kVisitorHealth ? kVisitorHealth : health);
                    visitor.yaw = static_cast<std::uint16_t>((actor.yaw & ~kVisitorHealth) | health);
                    visitor.light |= kVisitorMind;
                    if (follows)
                        visitor.light |= kVisitorFollows;
                    if (mind != nullptr && (mind->memory & kMemoryProvoked))
                        visitor.light |= kVisitorProvoked;
                }
                if (taken < kMaxVisitors)
                    ++taken;
            }
        }
        // A lift the player is riding that the next map knows by the same
        // name (func_train with a globalname): where its middle is, how fast
        // it goes, whether it is going, and which corner it is going to.
        for (int i = 0; i < w.recordCount && here != nullptr && arrival.train == 0; ++i)
        {
            const LogicRecord &train = w.records[i];
            if (train.kind != static_cast<std::uint8_t>(LogicKind::Train) || trainCrossName(w, train) == 0 ||
                train.brush == kNoBrush)
                continue;
            // On it: what they stand on, or (in the middle of a jump) just
            // above its top, over it.
            const MoverHull &hull = w.movers[train.brush];
            const fx feet = player.origin[2] - (player.ducked ? fxFromInt(18) : fxFromInt(36));
            const bool over = player.origin[0] >= hull.mins[0] && player.origin[0] <= hull.maxs[0] &&
                              player.origin[1] >= hull.mins[1] && player.origin[1] <= hull.maxs[1] &&
                              feet >= hull.maxs[2] - fxFromInt(2) && feet <= hull.maxs[2] + fxFromInt(72);
            if (player.groundMover != static_cast<std::int16_t>(train.brush) && !over &&
                !detail::playerAboard(w, player, train.brush))
                continue;
            const LogicState &state = w.states[i];
            const BrushState &car = w.brushStates[train.brush];
            arrival.train = trainCrossName(w, train);
            for (int a = 0; a < 3; ++a)
                arrival.trainOffset[a] =
                    car.pos[a] + train.move[a] - w.brushes[train.brush].origin[a] - here->origin[a];
            arrival.trainSpeed = state.speed;
            // (Going: on its way, or at a corner about to go on -- the level
            // change is fired as it comes to one.)
            arrival.trainSelfDriven = state.running;
            const auto at = static_cast<std::uint16_t>(state.bits);
            const std::uint16_t heading =
                !arrival.trainSelfDriven || at == kNoCorner
                    ? kNoCorner
                    : ((state.bits & detail::kTrainHeading) ? at : w.corners[at].next);
            if (heading != kNoCorner)
            {
                int slot = kMaxVisitors - 1;
                for (int v = 0; v < kMaxVisitors; ++v)
                {
                    if (arrival.visitors[v].model == 0)
                    {
                        slot = v;
                        break;
                    }
                }
                Arrival::Visitor &mark = arrival.visitors[slot];
                mark = Arrival::Visitor();
                mark.model = kTrainHeadingVisitor;
                mark.name = arrival.train;
                for (int a = 0; a < 3; ++a)
                    mark.offset[a] = w.corners[heading].pos[a] - here->origin[a];
            }
        }
        arrival.changeTarget = record.extra[2];
        arrival.changeDelayMs = static_cast<std::int32_t>(record.extra[3]);
        if (from != nullptr)
        {
            for (int v = 0; v < kMaxVisitors; ++v)
                from[v] = arrival.visitors[v].model != 0 ? who[v] : -1;
        }
        return arrival;
    }

    /// Put the player where they should be in the map just loaded, and queue
    /// what the old map asked to have fired here. False if this map has no
    /// such landmark: the caller starts the player at the map's own start.
    /// The player left at the controls of the train they came in on: they
    /// have its namesake's here (the game's "on the other side of a level
    /// transition"), after applyArrival. (Kept beside the Arrival, not in
    /// it: an Arrival is saved as it is.)
    inline void arrivalDriving(LogicWorld &w, const Arrival &arrival)
    {
        for (int i = 0; i < w.recordCount && arrival.train != 0; ++i)
        {
            const LogicRecord &train = w.records[i];
            if (train.kind == static_cast<std::uint8_t>(LogicKind::TrackTrain) && train.extra[2] == arrival.train &&
                train.brush != kNoBrush)
            {
                w.states[i].phase |= detail::kTrainDriven;
                detail::trainShow(w, i);
                return;
            }
        }
    }

    /// `placed`, if given: a bit for each of the arrival's visitors who
    /// found a place here (bit v: visitors[v]).
    inline bool applyArrival(LogicWorld &w, const Landmark *landmarks, std::uint32_t landmarkCount,
                             const std::uint32_t *nameHashes, std::uint32_t nameCount,
                             PlayerState &player, const Arrival &arrival,
                             const ModelRef *modelRefs = nullptr, std::uint8_t *placed = nullptr)
    {
        if (placed != nullptr)
            *placed = 0;
        if (!arrival.valid)
            return false;
        player.health = arrival.health;
        player.inv = arrival.inv;
        const std::uint16_t fire = nameIdOfHash(nameHashes, nameCount, arrival.changeTarget);
        if (fire != 0)
            logicEnqueue(w, fire, 0, kUseToggle, -1, arrival.changeDelayMs);

        const Landmark *here = arrival.landmark != 0
                                   ? findLandmark(landmarks, landmarkCount, arrival.landmark)
                                   : nullptr;
        if (here == nullptr)
            return false;
        for (int a = 0; a < 3; ++a)
        {
            player.origin[a] = here->origin[a] + arrival.offset[a];
            player.velocity[a] = arrival.velocity[a];
        }
        player.ducked = arrival.ducked;
        player.viewHeight = arrival.ducked ? kDuckViewHeight : kViewHeight;

        // Whoever came along takes one of the map's spare places -- unless
        // the map has someone of that name already, or was not given the
        // model.
        for (int v = 0; v < kMaxVisitors; ++v)
        {
            const Arrival::Visitor &visitor = arrival.visitors[v];
            if (visitor.model == 0 || w.actors == nullptr || modelRefs == nullptr)
                continue;
            int model = -1, slot = -1;
            bool here2 = false;
            for (int m = 0; m < w.actors->modelCount; ++m)
            {
                if (nameHash(modelRefs[m].name) == visitor.model)
                    model = m;
            }
            // A place: his own, if this map knew him and he is not here
            // (he went, and is back); else one of the places kept for
            // visitors; else one of the map's that is gone for good -- not
            // one that is yet to be (a maker's, a soldier's gun to drop).
            int spare = -1, gone = -1;
            for (int i = 0; i < w.actors->count; ++i)
            {
                const ActorState &other = w.actors->states[i];
                if (other.active && visitor.name != 0 && other.nameHash == visitor.name)
                    here2 = true;
                if (other.active)
                    continue;
                const bool own = i < w.actors->recordCount;
                if (own && visitor.name != 0 && other.nameHash == visitor.name && slot < 0)
                    slot = i;
                else if (!own && spare < 0)
                    spare = i;
                else if (own && gone < 0 && w.actors->records != nullptr &&
                         !(w.actors->records[i].flags & kActorDormant))
                    gone = i;
            }
            slot = slot >= 0 ? slot : (spare >= 0 ? spare : gone);
            if (model < 0 || slot < 0 || here2)
                continue;
            // Where they would stand must be somewhere a person can: on a
            // floor, with room. Otherwise they are left behind.
            if (w.world != nullptr)
            {
                ActorState probe;
                for (int a = 0; a < 3; ++a)
                    probe.pos[a] = here->origin[a] + visitor.offset[a];
                if (!actorToFloor(*w.world, probe, fxFromInt(18), fxFromInt(64)))
                    continue;
            }
            ActorState &actor = w.actors->states[slot];
            actor = ActorState();
            actor.active = true;
            actor.model = static_cast<std::uint16_t>(model);
            actor.nameHash = visitor.name;
            actor.name = nameIdOfHash(nameHashes, nameCount, visitor.name);
            for (int a = 0; a < 3; ++a)
                actor.pos[a] = here->origin[a] + visitor.offset[a];
            actor.yaw = visitor.yaw;
            actor.body = visitor.body;
            actor.kind = visitor.kind;
            actor.flags = visitor.flags;
            actor.light = static_cast<std::uint8_t>(visitor.light & kVisitorLight);
            // One of the people, as he left: his health, whether he follows
            // the player, whether the player provoked him.
            if (visitor.light & kVisitorMind)
            {
                actor.health = static_cast<std::int16_t>(visitor.yaw & kVisitorHealth);
                actor.ai = (visitor.light & kVisitorFollows) ? 1 : 0;
                if (w.actors->ai != nullptr)
                {
                    aiForget(*w.actors, slot);
                    if (AiMonster *mind = aiOf(*w.actors, slot))
                    {
                        if (visitor.light & kVisitorFollows)
                            mind->targetEnt = kAiPlayer;
                        if (visitor.light & kVisitorProvoked)
                            mind->memory |= kMemoryProvoked;
                    }
                }
            }
            const ModelView *view = actorModel(*w.actors, actor);
            if (view != nullptr && view->header->clipCount != 0)
            {
                const int idle = view->findClip(nameHash(
                    visitor.kind == static_cast<std::uint8_t>(ActorKind::Gman) ? "idle01" : "idle1"));
                actor.restClip = static_cast<std::int16_t>(idle < 0 ? 0 : idle);
            }
            actorRest(actor);
            if (w.world != nullptr)
                actorToFloor(*w.world, actor, fxFromInt(18), fxFromInt(64));
            actorPlaceBox(*w.actors, slot);
            if (placed != nullptr)
                *placed = static_cast<std::uint8_t>(*placed | (1u << v));
        }

        // The train they came in on: this map's train of the same name takes
        // up where that one was, on whichever node of its track is nearest.
        for (int i = 0; i < w.recordCount && arrival.train != 0; ++i)
        {
            const LogicRecord &train = w.records[i];
            if (train.kind != static_cast<std::uint8_t>(LogicKind::TrackTrain) ||
                train.extra[2] != arrival.train || train.brush == kNoBrush)
                continue;
            const fx pos[3] = {here->origin[0] + arrival.trainOffset[0],
                               here->origin[1] + arrival.trainOffset[1],
                               here->origin[2] + arrival.trainOffset[2]};
            logicPlaceBrush(w, train.brush, pos, arrival.trainYaw);
            LogicState &state = w.states[i];
            state.speed = arrival.trainSpeed;
            state.running = arrival.trainSelfDriven;
            const std::uint16_t node =
                detail::trackNearest(w, pos, static_cast<fx>(train.extra[1]));
            if (node != kNoCorner)
                state.bits = node;
            // Standing on it from the first instant, so that it carries them
            // from the first instant.
            player.onGround = true;
            player.groundMover = static_cast<std::int16_t>(train.brush);
            break;
        }

        // The lift they came up in (a func_train of the same global name):
        // this map's takes up where that one was, and goes on to the corner
        // it was going to -- found where it is -- as in the game, where a
        // lift carried across keeps its own way.
        for (int i = 0; i < w.recordCount && arrival.train != 0; ++i)
        {
            const LogicRecord &train = w.records[i];
            // (By its global name; or, either side converted before that was
            // kept, by its own.)
            if (train.kind != static_cast<std::uint8_t>(LogicKind::Train) || train.brush == kNoBrush ||
                (train.extra[3] != arrival.train &&
                 !(nameHashes != nullptr && train.name != 0 && train.name < nameCount &&
                   nameHashes[train.name] == arrival.train)))
                continue;
            fx pos[3];
            for (int a = 0; a < 3; ++a)
                pos[a] = here->origin[a] + arrival.trainOffset[a] - train.move[a] + w.brushes[train.brush].origin[a];
            const std::uint16_t turned[3] = {static_cast<std::uint16_t>(train.extra[0]),
                                             static_cast<std::uint16_t>(train.extra[1]),
                                             static_cast<std::uint16_t>(train.extra[2])};
            logicPlaceBrushTurned(w, train.brush, pos, turned);
            LogicState &state = w.states[i];
            if (arrival.trainSpeed > 0)
                state.speed = arrival.trainSpeed;
            state.move = MoveState::AtBottom;
            state.running = false;
            state.timer = -1;
            for (const Arrival::Visitor &mark : arrival.visitors)
            {
                if (mark.model != kTrainHeadingVisitor || mark.name != arrival.train || !arrival.trainSelfDriven)
                    continue;
                // (The corner where it was going: within a unit or two.)
                std::uint16_t heading = kNoCorner;
                for (int c = 0; c < w.cornerCount && heading == kNoCorner; ++c)
                {
                    bool same = true;
                    for (int a = 0; a < 3 && same; ++a)
                    {
                        const fx d = w.corners[c].pos[a] - (here->origin[a] + mark.offset[a]);
                        same = d < fxFromInt(2) && d > -fxFromInt(2);
                    }
                    if (same)
                        heading = static_cast<std::uint16_t>(c);
                }
                if (heading == kNoCorner)
                {
                    fx middle[3], way[3];
                    for (int a = 0; a < 3; ++a)
                    {
                        middle[a] = here->origin[a] + arrival.trainOffset[a];
                        way[a] = mark.offset[a] - arrival.trainOffset[a];
                    }
                    heading = liftCornerAhead(w, middle, way);
                }
                if (heading != kNoCorner)
                {
                    state.bits = heading | detail::kTrainHeading;
                    detail::trainNext(w, player, i);
                }
            }
            player.onGround = true;
            player.groundMover = static_cast<std::int16_t>(train.brush);
            break;
        }
        return true;
    }
} // namespace hldata
