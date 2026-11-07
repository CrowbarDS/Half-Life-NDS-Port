// What fights, and what it is fought with (shared/HlMonsters.hpp).
//
// The sums that need no map first: armour, what is picked up, the weapons'
// clips and reloads. Then, with HLDS_VALVE_DIR set, the game's own maps: the
// path nodes are cooked and joined up; a headcrab finds the player in the
// open and round corners, leaps and bites, and is never once inside a wall
// whatever the frame rate; a zombie walks up and hits; both die of being
// shot; a monstermaker makes.

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    double toUnits(fx v) { return v / 4096.0; }

    /// The player's hurt, as CBasePlayer::TakeDamage has it: the armour
    /// takes four fifths of what is not a fall or drowning, and is worn by
    /// four tenths of it; health loses the rest, rounded down; the suit says
    /// what it was; and below -40 the player is in pieces.
    void testPlayerDamage()
    {
        auto fresh = [](int health, int armor)
        {
            PlayerState p;
            p.health = health;
            p.inv.armor = static_cast<std::int16_t>(armor);
            p.inv.have = 1; // (the suit)
            return p;
        };
        PlayerState p = fresh(100, 50);
        playerTakeDamage(p, 30, kDmgCrush);
        check(p.health == 94 && p.inv.armor == 38, "armour takes four fifths of a crush, and is worn by twelve");
        p = fresh(100, 50);
        logicHurt(p, 30, 0, kDmgGeneric);
        check(p.health == 94 && p.inv.armor == 38, "... and of what the world does (a trigger_hurt's)");
        p = fresh(100, 50);
        playerTakeDamage(p, 30, kDmgFall);
        check(p.health == 70 && p.inv.armor == 50, "a fall goes past the armour");
        p = fresh(100, 50);
        playerTakeDamage(p, 30, kDmgDrown);
        check(p.health == 70 && p.inv.armor == 50, "... and so does drowning");
        p = fresh(100, 50);
        playerTakeDamage(p, 2, kDmgCrush);
        check(p.health == 100, "what is left to health is rounded down: two through armour is nothing");
        playerTakeDamage(p, 2, kDmgCrush);
        playerTakeDamage(p, 2, kDmgCrush);
        check(p.inv.armor == 47 && p.armorTenths.value == 6, "... and the armour is worn in tenths");
        p = fresh(100, 4);
        playerTakeDamage(p, 30, kDmgCrush);
        check(p.health == 78 && p.inv.armor == 0, "too little armour: it takes twice itself, the rest is health");
        // The suit's word for it.
        p = fresh(60, 0);
        playerTakeDamage(p, 30, kDmgCrush);
        check(p.suit.value.queue[0] == kSuitDmg5, "a crush of 30 is a major fracture");
        p = fresh(60, 0);
        playerTakeDamage(p, 10, kDmgFall);
        check(p.suit.value.queue[0] == kSuitDmg4, "... a fall of 10, a minor one");
        {
            // Said at the game's suitvolume, a quarter of full, from the
            // player; half the time a little higher (EMIT_SOUND_SUIT).
            LogicWorld w;
            SoundQueue heard;
            w.heard = &heard;
            int quietest = 127, loudest = 0, higher = 0, said = 0;
            for (int n = 0; n < 40; ++n)
            {
                p.suit.value.queue[0] = kSuitDmg4;
                p.suit.value.waitMs = 0;
                w.effectSeed = static_cast<std::uint32_t>(n) * 7919u; // (as the effects move it between lines)
                heard.count = 0;
                hldata::detail::suitTick(w, p, 33);
                for (int i = 0; i < heard.count; ++i)
                {
                    const SoundEvent &e = heard.events[i];
                    quietest = std::min(quietest, static_cast<int>(e.volume));
                    loudest = std::max(loudest, static_cast<int>(e.volume));
                    higher += e.pitch >= 98 && e.pitch <= 104 && e.pitch != 100;
                    said += (e.flags & kSoundSentence) && (e.flags & kSoundHere) && (e.pitch == 100 || (e.pitch >= 98 && e.pitch <= 104));
                }
            }
            check(said == 40 && quietest == 32 && loudest == 32, "the suit speaks at a quarter of full, at the player");
            check(higher > 5 && higher < 35, "... and about half the time a little higher");
        }
        p = fresh(100, 0);
        playerTakeDamage(p, 10, kDmgCrush);
        check(p.suit.value.queue[0] < 0, "... and nothing is said of a hurt with health still over 75");
        p = fresh(100, 0);
        p.health = 90;
        playerTakeDamage(p, 30, kDmgCrush);
        playerTakeDamage(p, 30, kDmgCrush);
        int majors = 0;
        for (std::int8_t line : p.suit.value.queue)
            majors += line == kSuitDmg5;
        check(majors == 1, "... nor said again within thirty seconds");
        // In pieces below -40.
        p = fresh(10, 0);
        playerTakeDamage(p, 51, kDmgGeneric);
        check(p.health == -41 && p.gib.value == 1, "killed to -41, the player is in pieces");
        p = fresh(10, 0);
        playerTakeDamage(p, 49, kDmgGeneric);
        check(p.health == -39 && p.gib.value == 0, "... to -39, not");
        p = fresh(10, 0);
        playerTakeDamage(p, 11, kDmgAlwaysGib);
        check(p.gib.value == 1, "... and always, by what always does it (a barnacle's)");
    }

    void testSums()
    {
        PlayerState player;
        playerHurt(player, 20);
        check(player.health == 80, "with no armour a blow is taken whole");
        player.health = 100;
        player.inv.armor = 100;
        playerHurt(player, 20);
        check(player.health == 96 && player.inv.armor == 92,
              "armour takes four fifths, and loses half of what it takes");
        player.inv.armor = 3;
        player.health = 100;
        playerHurt(player, 20);
        check(player.inv.armor == 0 && player.health == 86, "armour that runs out takes what it can");

        // Picking up.
        player = PlayerState();
        std::uint32_t got = 0;
        check(hldata::detail::itemTake(player, static_cast<std::uint8_t>(ItemKind::Glock), got) &&
                  player.inv.current == kWeaponGlock && player.inv.clipGlock == 17 &&
                  (got & kGotWeapon),
              "a pistol picked up is loaded and in hand");
        check(hldata::detail::itemTake(player, static_cast<std::uint8_t>(ItemKind::Glock), got) &&
                  player.inv.ammo9mm == 17,
              "a second pistol is its rounds");
        check(!hldata::detail::itemTake(player, static_cast<std::uint8_t>(ItemKind::HealthKit), got),
              "a health kit is left lying by someone in full health");
        player.health = 90;
        check(hldata::detail::itemTake(player, static_cast<std::uint8_t>(ItemKind::HealthKit), got) &&
                  player.health == 100,
              "... and taken by someone who is not, no further than full");
        check(hldata::detail::itemTake(player, static_cast<std::uint8_t>(ItemKind::Crowbar), got) &&
                  weaponAfter(player.inv) == kWeaponGlock,
              "weapons come round in turn");

        // Firing, with nothing to hit: a world of nothing at all.
        LogicWorld logic;
        MoveWorld world;
        logic.world = &world;
        WeaponState weapon;
        player.inv.current = kWeaponGlock;
        const fx eye[3] = {0, 0, 0};
        const fx forward[3] = {kFxOne, 0, 0}, right[3] = {0, -kFxOne, 0}, up[3] = {0, 0, kFxOne};
        int shots = 0, reloads = 0;
        for (int ms = 0; ms < 16000; ms += 33)
        {
            const std::uint32_t did =
                weaponsTick(logic, player, weapon, eye, forward, right, up, true, false, 33);
            shots += (did & kWeaponFired) ? 1 : 0;
            reloads += (did & kWeaponReload) ? 1 : 0;
        }
        check(shots == 34 && reloads == 1 && player.inv.clipGlock == 0 && player.inv.ammo9mm == 0,
              "a pistol fires its clip, reloads once from what is carried, and is empty");
    }

    std::unique_ptr<Game> load(const char *dir, const char *map)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
        {
            check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
            return nullptr;
        }
        finish(*game, nullptr, dir);
        if (!game->ok)
            return nullptr;
        return game;
    }

    int findKind(const Game &g, ActorKind kind, int nth = 0)
    {
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            if (actor.active && actor.kind == static_cast<std::uint8_t>(kind) && actor.script < 0 &&
                nth-- == 0)
                return i;
        }
        return -1;
    }

    bool inSolid(const Game &g, const ActorState &actor)
    {
        const bool small = monsterKind(actor.kind).small;
        const fx at[3] = {actor.pos[0], actor.pos[1],
                          actor.pos[2] + (small ? kCrouchedFeet : kStandingFeet)};
        const Hull &hull = small ? g.move.crouched : g.move.standing;
        return hullPointContents(hull, hull.root, at) == kContentsSolid;
    }

    /// Stand the player somewhere `actor` can walk to in a straight line,
    /// about `distance` units off -- and turn it to face them (the game's
    /// monsters see only what is in front of them). False if there is no
    /// such place.
    bool standNear(Game &g, int actor, int distance)
    {
        ActorState &a = g.actors.states[actor];
        const bool small = monsterKind(a.kind).small;
        for (int turn = 0; turn < 16; ++turn)
        {
            fx sine, cosine;
            fxSinCos(static_cast<std::uint16_t>(turn * 4096), sine, cosine);
            ActorState probe;
            probe.pos[0] = a.pos[0] + cosine * distance;
            probe.pos[1] = a.pos[1] + sine * distance;
            probe.pos[2] = a.pos[2] + fxFromInt(8);
            if (!actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(64)) ||
                !walkClear(g.move, &g.actors, actor, small, a.pos, probe.pos, distance / 16 + 4))
                continue;
            g.player.origin[0] = probe.pos[0];
            g.player.origin[1] = probe.pos[1];
            g.player.origin[2] = probe.pos[2] + kStandingFeet;
            g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
            a.yaw = static_cast<std::uint16_t>(turn * 4096);
            // (... and the player looking at it: some wait to be seen.)
            g.player.facingXY[0] = -cosine;
            g.player.facingXY[1] = -sine;
            return true;
        }
        return false;
    }

    /// Whether the monster at `index` is making one of its attacks (its
    /// clip is one for an attack: HlAi.hpp's activities).
    bool attacking(const Game &g, int index)
    {
        const AiMonster *m = aiFind(g.actors, index);
        if (m == nullptr)
            return false;
        switch (m->activity)
        {
        case kActRangeAttack1:
        case kActRangeAttack2:
        case kActMeleeAttack1:
        case kActMeleeAttack2:
        case kActSpecialAttack1:
            return true;
        default:
            return false;
        }
    }

    void tick(Game &g, int vblanks)
    {
        const int ms = (vblanks * 1000 + 30) / 60;
        logicTick(g.logic, g.player, ms);
        g.logic.viewTurn = 0;
        monstersTick(g.logic, g.player, ms);
        for (int n = 0; n < vblanks; n += 2)
            playerMove(g.player, MoveCommand(), g.move, kPlayerTick);
        g.drain();
    }

    // ------------------------------------------------------------- turrets

    /// The game by itself with the player held where they are put (a turret
    /// is looked at from wherever it can be seen, floor or no floor).
    void tickStill(Game &g, int ms = 33)
    {
        logicTick(g.logic, g.player, ms);
        g.logic.viewTurn = 0;
        monstersTick(g.logic, g.player, ms);
        g.drain();
    }

    /// Put the player somewhere the turret can see, a couple of hundred
    /// units off. False if there is nowhere.
    bool standInSight(Game &g, int turret)
    {
        const ActorState &actor = g.actors.states[turret];
        fx eye[3];
        hldata::detail::turretEye(actor, eye);
        for (int distance = 160; distance <= 480; distance += 80)
        {
            for (int drop : {0, -48, -96, -160, 48, 96})
            {
                for (int turn = 0; turn < 16; ++turn)
                {
                    fx sine, cosine;
                    fxSinCos(static_cast<std::uint16_t>(turn * 4096), sine, cosine);
                    const fx at[3] = {eye[0] + cosine * distance, eye[1] + sine * distance,
                                      eye[2] + fxFromInt(drop)};
                    if (hullPointContents(g.move.standing, g.move.standing.root, at) == kContentsSolid ||
                        !lineClear(g.move, eye, at))
                        continue;
                    for (int k = 0; k < 3; ++k)
                    {
                        g.player.origin[k] = at[k];
                        g.player.velocity[k] = 0;
                    }
                    std::printf("test_monsters: (a turret at %d %d %d is seen from %d %d %d)\n",
                                fxToInt(actor.pos[0]), fxToInt(actor.pos[1]), fxToInt(actor.pos[2]),
                                fxToInt(at[0]), fxToInt(at[1]), fxToInt(at[2]));
                    return true;
                }
            }
        }
        return false;
    }

    void outOfSight(Game &g)
    {
        g.player.origin[2] += fxFromInt(30000);
    }
    /// c1a2's ladder up through the hatch: its foot is 64 above the floor,
    /// and the box (*4) is what the game has the player push under it. On
    /// the box, facing it and looking up, forward climbs it.
    void testHatchLadder(const char *dir)
    {
        auto game = load(dir, "c1a2");
        if (!game)
            return;
        Game &g = *game;
        int ladder = -1;
        for (int i = 0; i < g.move.ladderCount; ++i)
        {
            const LadderVolume &l = g.ladders[i];
            if (l.active && fxToInt(l.mins[0]) >= 110 && fxToInt(l.maxs[0]) <= 214 && fxToInt(l.mins[1]) < -770 &&
                fxToInt(l.maxs[1]) > -790)
                ladder = i;
        }
        check(ladder >= 0, "c1a2's hatch ladder");
        int box = -1;
        for (std::uint32_t b = 0; b < g.view.brushCount; ++b)
            box = g.view.brushes[b].model == 4 ? static_cast<int>(b) : box;
        check(box >= 0, "... and the box that goes under it");
        if (ladder < 0 || box < 0)
            return;
        // The box pushed under the hatch, as the player would: walked into
        // from its +x side, towards -x, until its middle is under it.
        {
            g.player.origin[0] = fxFromInt(320 + 17);
            g.player.origin[1] = fxFromInt(-801);
            g.player.origin[2] = fxFromInt(-576) + kStandingFeet;
            MoveCommand push;
            push.forwardXY[0] = -kFxOne;
            push.forwardXY[1] = 0;
            push.rightXY[0] = 0;
            push.rightXY[1] = kFxOne;
            push.viewForward[0] = -kFxOne;
            push.viewRight[1] = kFxOne;
            push.forward = 1;
            for (int i = 0; i < 30 * 8; ++i)
            {
                logicTick(g.logic, g.player, 33);
                playerMove(g.player, push, g.move, kPlayerTick);
                const MoverHull &moved = g.movers[box];
                if ((moved.mins[0] + moved.maxs[0]) / 2 <= fxFromInt(162))
                    break;
            }
        }
        const MoverHull &top = g.movers[box];
        std::printf("test_monsters: c1a2: pushed, the box's middle is at %.0f %.0f (under the hatch: 120..204)\n",
                    toUnits((top.mins[0] + top.maxs[0]) / 2), toUnits((top.mins[1] + top.maxs[1]) / 2));
        check((top.mins[0] + top.maxs[0]) / 2 < fxFromInt(200), "the box can be pushed under the hatch");
        g.player.origin[0] = fxFromInt(162);
        g.player.origin[1] = fxFromInt(-801);
        g.player.origin[2] = top.maxs[2] + kStandingFeet + fxFromInt(1);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        g.player.health = 100;
        MoveCommand cmd;
        for (int i = 0; i < 10; ++i)
            playerMove(g.player, cmd, g.move, kPlayerTick);
        std::printf("test_monsters: c1a2: standing on the box, the player's head is at %.2f, the ladder's foot at %.2f\n",
                    toUnits(g.player.origin[2] + kStandingFeet), toUnits(g.ladders[ladder].mins[2]));
        cmd.forwardXY[0] = 0;
        cmd.forwardXY[1] = kFxOne; // facing +y, the ladder
        cmd.rightXY[0] = kFxOne;
        cmd.rightXY[1] = 0;
        cmd.viewForward[0] = 0;
        cmd.viewForward[1] = fxFromDouble(0.5);
        cmd.viewForward[2] = fxFromDouble(0.866); // looking up
        cmd.viewRight[0] = kFxOne;
        cmd.forward = 1;
        bool held = false;
        for (int i = 0; i < 60; ++i)
        {
            playerMove(g.player, cmd, g.move, kPlayerTick);
            held = held || g.player.onLadder;
        }
        std::printf("test_monsters: c1a2: %s the ladder; now at %.0f\n", held ? "took hold of" : "never took", toUnits(g.player.origin[2]));
        check(held, "on the box under the hatch, the player takes hold of its ladder");
        check(g.player.origin[2] - kStandingFeet > fxFromInt(-400), "... and climbs it, up through the hatch");
    }

    /// c1a2b's ladders: walked into from the floor at the foot of each, the
    /// player takes hold of it and climbs.
    void testLadderFeet(const char *dir)
    {
        auto game = load(dir, "c1a2b");
        if (!game)
            return;
        Game &g = *game;
        int tried = 0, climbed = 0;
        for (int i = 0; i < g.move.ladderCount; ++i)
        {
            const LadderVolume &l = g.ladders[i];
            if (!l.active)
                continue;
            // Its thin side is the way in: stand 40 off it, on the floor
            // under its foot, either side, and walk at it.
            const bool thinX = l.maxs[0] - l.mins[0] < l.maxs[1] - l.mins[1];
            bool ok = false;
            // From the floor at its foot, or (a ladder down a shaft) from the
            // floor at its top, walking out over it.
            (void)thinX;
            for (int way = 0; way < 24 && !ok; ++way)
            {
                // (Each side of it -- face on, or along a ledge into its
                // edge, flush with its face, hugging the wall it is on.)
                const int side = way & 1 ? 1 : -1;
                const int axis = (way >> 1) & 1;
                const bool fromTop = ((way >> 2) & 1) != 0;
                const int cross = way >> 3; // 0: in line; 1, 2: out from either face
                fx at[3] = {(l.mins[0] + l.maxs[0]) / 2, (l.mins[1] + l.maxs[1]) / 2,
                            (fromTop ? l.maxs[2] : l.mins[2]) + fxFromInt(40)};
                if (cross != 0)
                {
                    const int other = axis ^ 1;
                    at[other] = cross == 1 ? l.maxs[other] + fxFromInt(16) : l.mins[other] - fxFromInt(16);
                }
                at[axis] = (side < 0 ? l.mins[axis] : l.maxs[axis]) + side * fxFromInt(40);
                ActorState probe;
                for (int k = 0; k < 3; ++k)
                    probe.pos[k] = at[k];
                if (!actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(96)))
                    continue;
                if (!fromTop && probe.pos[2] < l.mins[2] - fxFromInt(48))
                    continue; // (its foot is out of reach from there)
                g.player.origin[0] = probe.pos[0];
                g.player.origin[1] = probe.pos[1];
                g.player.origin[2] = probe.pos[2] + kStandingFeet;
                g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
                g.player.onLadder = false;
                MoveCommand cmd;
                cmd.forwardXY[0] = axis == 0 ? -side * kFxOne : 0;
                cmd.forwardXY[1] = axis == 0 ? 0 : -side * kFxOne;
                cmd.rightXY[0] = cmd.forwardXY[1];
                cmd.rightXY[1] = -cmd.forwardXY[0];
                for (int k = 0; k < 2; ++k)
                {
                    cmd.viewForward[k] = cmd.forwardXY[k];
                    cmd.viewRight[k] = cmd.rightXY[k];
                }
                cmd.viewForward[2] = 0;
                cmd.forward = 1;
                const fx start = g.player.origin[2];
                bool held = false;
                for (int f = 0; f < 60; ++f)
                {
                    // (From the top: looking down, forward goes down it.)
                    if (fromTop && g.player.onLadder)
                    {
                        cmd.viewForward[0] = cmd.viewForward[0] / 2;
                        cmd.viewForward[1] = cmd.viewForward[1] / 2;
                        cmd.viewForward[2] = -fxFromDouble(0.866);
                    }
                    playerMove(g.player, cmd, g.move, kPlayerTick);
                    held = held || g.player.onLadder;
                }
                ok = held && (fromTop ? g.player.origin[2] < start - fxFromInt(64)
                                      : g.player.origin[2] > start + fxFromInt(64));
                if (std::getenv("HLDS_DEBUG_LADDER"))
                    std::printf("  ladder %d way %d from %.0f %.0f %.0f: held %d, ended %.0f %.0f %.0f\n", i, way,
                                toUnits(probe.pos[0]), toUnits(probe.pos[1]), toUnits(start), held,
                                toUnits(g.player.origin[0]), toUnits(g.player.origin[1]), toUnits(g.player.origin[2]));
            }
            ++tried;
            climbed += ok;
            if (!ok)
                std::printf("test_monsters: c1a2b: the ladder at %.0f %.0f %.0f was not climbed\n",
                            toUnits(l.mins[0]), toUnits(l.mins[1]), toUnits(l.mins[2]));
        }
        std::printf("test_monsters: c1a2b: %d of %d ladders climbed\n", climbed, tried);
        check(tried >= 4 && climbed == tried, "every c1a2b ladder is taken hold of and climbed (up from its foot, or down from its top)");
    }

    /// The hazard course's hologram (a monster_generic of holo.mdl): its
    /// origin is its middle, as the game's box for it has it. The one on the
    /// projector is left where the map put it (its drop starts in the
    /// projector), the one by the button is let down to the floor, its
    /// middle 36 above.
    void testHologram(const char *dir)
    {
        auto game = load(dir, "t0a0");
        if (!game)
            return;
        Game &g = *game;
        auto named = [&](const char *name)
        {
            const std::uint16_t id = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash(name));
            for (int i = 0; id != 0 && i < g.actors.count; ++i)
            {
                if (g.actors.states[i].name == id)
                    return i;
            }
            return -1;
        };
        const int intro = named("intromona"), gen = named("genmona");
        check(intro >= 0 && gen >= 0, "t0a0's two holograms");
        if (intro < 0 || gen < 0)
            return;
        // (After a second of the map: nothing that thinks lets them down to
        // where feet would be.)
        for (int n = 0; n < 30; ++n)
            tick(g, 2);
        const double introZ = toUnits(g.actors.states[intro].pos[2]), genZ = toUnits(g.actors.states[gen].pos[2]);
        std::printf("test_monsters: t0a0: the holograms' middles at %.1f (on the projector) and %.1f\n", introZ, genZ);
        check(std::fabs(introZ + 5) < 1.5, "the hologram on the projector is where the map has it");
        check(std::fabs(genZ + 9) < 1.5, "... the other stands on the floor, its middle 36 above it");
        // Drawn as the game's hologram: see-through, fading with distance,
        // flickering.
        const auto lookOf = [&](int actor)
        {
            return actor < static_cast<int>(g.view.actorCount)
                       ? (g.view.actors[actor].item >> kActorLookShift) & kActorLookMask
                       : 0u;
        };
        check(lookOf(intro) == kActorLookHologram && lookOf(gen) == kActorLookHologram,
              "the holograms keep the map's hologram look");
        check(hologramBlend(50, 0) == 180 && hologramBlend(300, 0) == 90 && hologramBlend(500, 0) == 0 &&
                  hologramBlend(-5, 0) == 0 && hologramBlend(50, 31) == 211 && hologramBlend(480, -32) == 0,
              "... all of 180 near, fading to none at 500 ahead, none behind, give or take its flicker");
        // Standing about, as the game's do: in its talking idle, not a walk.
        const ModelView *model = actorModel(g.actors, g.actors.states[intro]);
        const auto clipNamed = [&](int clip)
        {
            if (model == nullptr || clip < 0 || clip >= model->header->clipCount)
                return std::string("?");
            const std::uint32_t name = model->clips[clip].name;
            return name == nameHash("troom_talkidle") ? std::string("troom_talkidle")
                                                      : (name == nameHash("walk") ? std::string("walk") : std::string("other"));
        };
        const std::string introClip = clipNamed(g.actors.states[intro].clip),
                          genClip = clipNamed(g.actors.states[gen].clip);
        std::printf("test_monsters: t0a0: the holograms stand in %s and %s\n", introClip.c_str(), genClip.c_str());
        check(introClip == "troom_talkidle" && genClip == "troom_talkidle", "the holograms stand about, not walking");
        // The one that shows a jump walks to its mark, jumps, and stands
        // about again after.
        const int quad = named("quadtalker");
        const std::uint16_t script = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("quadjump"));
        int scriptRecord = -1;
        for (std::uint32_t i = 0; script != 0 && i < g.view.logicCount && scriptRecord < 0; ++i)
            scriptRecord = g.view.logic[i].name == script ? static_cast<int>(i) : -1;
        check(quad >= 0 && scriptRecord >= 0, "t0a0's jumping hologram and its script");
        if (quad >= 0 && scriptRecord >= 0)
        {
            logicUse(g.logic, g.player, scriptRecord, -1, kUseToggle);
            bool walked = false;
            for (int n = 0; n < 30 * 15; ++n)
            {
                tick(g, 2);
                walked = walked || clipNamed(g.actors.states[quad].clip) == "walk";
            }
            const std::string after = clipNamed(g.actors.states[quad].clip);
            std::printf("test_monsters: t0a0: the jumping hologram walked %d, stands after in %s\n", walked ? 1 : 0,
                        after.c_str());
            check(walked && after == "troom_talkidle", "... and after its show it stands about again");
        }
    }


    /// A round in the head counts three times: a zombie takes 24 of a
    /// pistol's 8 there and 8 anywhere else (the game's 30% for bullets is
    /// never met: a shot is a bullet that never tears apart). The head is
    /// found by its model's own hit box, as it stands.
    void testHeadShot(const char *dir)
    {
        int rounds[2] = {0, 0};
        for (int head = 0; head < 2; ++head)
        {
            auto game = load(dir, "c1a1a");
            if (!game)
                return;
            Game &g = *game;
            const int who = findKind(g, ActorKind::Zombie);
            if (who < 0)
                return;
            ActorState &actor = g.actors.states[who];
            for (int n = 0; n < 4; ++n)
                tickStill(g);
            const ModelView *model = actorModel(g.actors, actor);
            if (head == 0)
                check(model != nullptr && model->header->hitboxCount == 20, "the zombie's model has its hit boxes");
            // From a hundred units in front of it, level: down from over
            // its head until the line is on the head; and at its middle.
            fx sine, cosine;
            fxSinCos(actor.yaw, sine, cosine);
            fx from[3] = {actor.pos[0] + cosine * 100, actor.pos[1] + sine * 100, 0};
            fx to[3] = {actor.pos[0] - cosine * 100, actor.pos[1] - sine * 100, 0};
            int height = -1;
            for (int z = 80; z > 0 && height < 0; --z)
            {
                from[2] = to[2] = actor.pos[2] + fxFromInt(z);
                const int group = hitGroup(g.actors, actor, from, to);
                if (head ? group == kHitHead : (z <= 40 && group != kHitHead && group != kHitBody))
                    height = z;
            }
            std::printf("test_monsters: zombie: %s found %d units up\n", head ? "its head" : "its trunk", height);
            check(height > (head ? 50 : 20), head ? "its head is where a head is" : "its trunk is at its middle");
            if (height < 0)
                return;
            const fx dir3[3] = {-cosine, -sine, 0};
            for (; rounds[head] < 40 && actor.ai < kAiDying; ++rounds[head])
            {
                // (It flinches: the part aimed at is found again each time.)
                int at = -1;
                for (int z = 80; z > 0 && at < 0; --z)
                {
                    from[2] = to[2] = actor.pos[2] + fxFromInt(z);
                    const int group = hitGroup(g.actors, actor, from, to);
                    if (head ? group == kHitHead : (z <= 40 && group != kHitHead && group != kHitBody))
                        at = z;
                }
                if (at < 0)
                    break;
                fireLine(g.logic, g.player, from, dir3, 200, 8, true);
            }
        }
        std::printf("test_monsters: zombie: %d rounds to the head, %d to the trunk\n", rounds[1], rounds[0]);
        check(rounds[1] == 3 && rounds[0] == 7, "a round in the head is three anywhere else");
    }

    /// What is taken far past dead comes apart: the body is gone, the
    /// game's pieces fly, fall and lie, and are gone in their turn.
    void testGibs(const char *dir)
    {
        auto game = load(dir, "c1a1a");
        if (!game)
            return;
        Game &g = *game;
        const int who = findKind(g, ActorKind::Zombie);
        check(who >= 0 && g.actors.alienGibs >= 0 && gibPieces(g.actors, g.actors.alienGibs) == 4,
              "a map with aliens in it has the four pieces they come apart into");
        if (who < 0 || g.actors.alienGibs < 0)
            return;
        ActorState &actor = g.actors.states[who];
        for (int n = 0; n < 4; ++n)
            tickStill(g);
        outOfSight(g);
        // Killed, but not by much: it falls, whole.
        monsterHurt(g.logic, who, 60, false);
        int flying = 0;
        for (const Gib &gib : g.actors.gibs)
            flying += gib.active;
        check(actor.active && actor.ai == kAiDying && flying == 0, "one that is just killed falls whole");
        // The body, blasted: it comes apart.
        const fx at[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(16)};
        radiusDamage(g.logic, g.player, at, 100, 250, -1);
        flying = 0;
        fx highest = actor.pos[2];
        for (const Gib &gib : g.actors.gibs)
        {
            flying += gib.active && gib.model == static_cast<std::uint16_t>(g.actors.alienGibs) && gib.body < 4;
        }
        check(!actor.active && flying == 4 && g.times(CommonSound::BodySplat) + 1 >= 1,
              "a body caught in a blast comes apart: gone, and four pieces in the air");
        g.drain();
        check(g.times(CommonSound::BodySplat) == 1, "... heard");
        // (Torn apart from a body, they go twice as fast as the game's do
        // from someone barely past dead: longer to come to rest.)
        int lying = 0, frames = 0;
        for (; frames < 30 * 5; ++frames)
        {
            tickStill(g);
            for (const Gib &gib : g.actors.gibs)
                highest = gib.active && gib.pos[2] > highest ? gib.pos[2] : highest;
        }
        bool inWorld = true;
        for (const Gib &gib : g.actors.gibs)
        {
            lying += gib.active && gib.resting;
            // (Each where a crouching person's middle could be: on a floor.)
            inWorld = inWorld && (!gib.active ||
                                  hullPointContents(g.move.crouched, g.move.crouched.root, gib.pos) != kContentsSolid);
        }
        std::printf("test_monsters: gibs: %d of 4 lying after 5 s, the highest got %d units up\n", lying,
                    fxToInt(highest - actor.pos[2]));
        // (One thrown from inside a wall the monster stood against may have
        // been given up: three at least are left.)
        check(lying >= 2 && inWorld && highest > actor.pos[2] + fxFromInt(20), "they fly up, fall, bounce and lie");
        for (int n = 0; n < 30 * 12; ++n)
            tickStill(g);
        int left = 0;
        for (const Gib &gib : g.actors.gibs)
            left += gib.active;
        check(left == 0, "... and are gone after a while");
        // One killed outright by far more than it had: straight to pieces.
        const int other = findKind(g, ActorKind::Zombie);
        if (other >= 0)
        {
            monsterHurt(g.logic, other, 200, false);
            flying = 0;
            for (const Gib &gib : g.actors.gibs)
                flying += gib.active;
            check(!g.actors.states[other].active && flying == 4, "one taken far past dead at a blow comes apart at once");
        }
    }

    /// The MP5 with its launcher, and the .357: found where the game leaves
    /// them, and firing by the game's numbers.
    void testGuns(const char *dir)
    {
        auto game = load(dir, "c1a3");
        check(game != nullptr, "c1a3 cooks");
        if (!game)
            return;
        Game &g = *game;
        int guns = 0, clips = 0;
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            guns += actor.active && actor.item == static_cast<std::uint8_t>(ItemKind::Mp5);
            clips += actor.active && actor.item == static_cast<std::uint8_t>(ItemKind::Mp5Clip);
        }
        check(guns == 2 && clips == 5, "c1a3 has its two MP5s and five clips lying about");
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (fights(g.actors.states[i].kind) || g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Tripmine))
                g.actors.states[i].active = false;
        }
        Inventory &inv = g.player.inv;
        inv = Inventory();
        std::uint32_t got = 0;
        check(hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Mp5), got) &&
                  (inv.have & (1u << kWeaponMp5)) && inv.current == kWeaponMp5 && inv.clipMp5 == 25,
              "an MP5 picked up is in hand with its 25 rounds");
        hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Mp5Clip), got);
        hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::ArGrenades), got);
        hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Python), got);
        hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Ammo357), got);
        check(inv.ammo9mm == 50 && inv.ammoAr == 2 && inv.clipPython == 6 && inv.ammo357 == 6 &&
                  inv.current == kWeaponPython,
              "its clip is 50 of 9 mm, its grenades two; a .357 comes loaded, its box is six");

        // Somewhere open, looking along the floor.
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
        fx forward[3] = {kFxOne, 0, 0};
        // (Whichever way has the most room.)
        int best = 0;
        for (int turn = 0; turn < 16; ++turn)
        {
            fx sine, cosine;
            fxSinCos(static_cast<std::uint16_t>(turn * 4096), sine, cosine);
            const fx to[3] = {eye[0] + cosine * 2000, eye[1] + sine * 2000, eye[2]};
            const int room = static_cast<int>((2000ll * traceShot(g.move, eye, to).fraction) >> kFractionShift);
            if (room > best)
            {
                best = room;
                forward[0] = cosine;
                forward[1] = sine;
            }
        }
        const fx right[3] = {forward[1], -forward[0], 0}, up[3] = {0, 0, kFxOne};
        WeaponState weapon;
        auto hold = [&](int ms, bool attack, bool second)
        {
            std::uint32_t did = 0;
            for (int t = 0; t < ms; t += 33)
            {
                did |= weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, attack, false, 33, second);
                monstersTick(g.logic, g.player, 33);
                g.drain();
            }
            return did;
        };
        // The .357: a round each three quarters of a second; six, and it
        // reloads by itself.
        hold(3000, true, false);
        const int after = inv.clipPython;
        check(after == 2 && g.times(CommonSound::PythonFire) + g.times(CommonSound::PythonFire2) == 4,
              "the .357 fires four times in three seconds, with one or other of its two shots");
        hold(1500, true, false);
        const bool emptied = inv.clipPython == 0;
        hold(3300, true, false);
        std::printf("test_monsters: .357: %d left after 3 s; reloaded to %d with %d spare\n", after, inv.clipPython,
                    inv.ammo357);
        // (The sound of the reload is the hand's own: its clip has it.)
        check(emptied && g.times(CommonSound::PythonReload) == 0 && inv.ammo357 == 0 && inv.clipPython >= 4,
              "... empties at six, and reloads by itself from what is carried");
        // The MP5: ten rounds a second.
        inv.current = kWeaponMp5;
        weapon = WeaponState();
        const int before = inv.clipMp5;
        hold(1000, true, false);
        std::printf("test_monsters: MP5: %d rounds in a second\n", before - inv.clipMp5);
        check(before - inv.clipMp5 >= 9 && before - inv.clipMp5 <= 11, "the MP5 fires ten rounds a second");
        // R with rounds missing from the clip and some carried: a reload.
        const std::uint32_t did = hold(33, false, true);
        check(!(did & kWeaponReload) && inv.ammoAr == 1, "R with a grenade to launch launches it, and does not reload");
        bool flying = false;
        for (const Projectile &shot : g.actors.shots)
            flying = flying || (shot.active && shot.kind == kShotContact);
        const int heard = g.times(CommonSound::Explode);
        hold(600, false, true);
        check(flying && inv.ammoAr == 1, "... once a second, however long it is held");
        hold(4000, false, false);
        bool gone = true;
        for (const Projectile &shot : g.actors.shots)
            gone = gone && !(shot.active && shot.kind == kShotContact);
        check(gone && g.times(CommonSound::Explode) == heard + 1, "its grenade goes off at the first thing it meets");
        inv.ammoAr = 0;
        const int missing = inv.clipMp5;
        // With nothing to launch the other trigger clicks, once for the
        // pull; the gun is reloaded by its own button.
        const int clicks = g.times(CommonSound::Click);
        const std::uint32_t nothing = hold(400, false, true);
        check(!(nothing & (kWeaponReload | kWeaponSecondary)) && inv.clipMp5 == missing &&
                  g.times(CommonSound::Click) == clicks + 1,
              "the launcher with nothing in it clicks, once");
        hold(100, false, false);
        const std::uint32_t reload = weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, false, false, 33, false,
                                                 0, true);
        hold(1700, false, false);
        check((reload & kWeaponReload) && inv.clipMp5 == 50 && inv.ammo9mm == 50 - (50 - missing),
              "RELOAD reloads the gun in hand");
    }

    /// The bullsquid is wide (the game moves it as its 64-unit hull): there
    /// are places in its own map a person can step into and it cannot.
    void testWideHull(const char *dir)
    {
        auto game = load(dir, "c1a1c");
        if (!game)
            return;
        Game &g = *game;
        check(g.move.large.nodes != nullptr && g.move.large.root >= 0, "the map's large hull is cooked");
        check(monsterHull(static_cast<std::uint8_t>(ActorKind::Bullsquid)) == kWalkLarge &&
                  monsterHull(static_cast<std::uint8_t>(ActorKind::Zombie)) == kWalkStanding &&
                  monsterHull(static_cast<std::uint8_t>(ActorKind::Headcrab)) == kWalkCrouched,
              "the bullsquid goes as the large hull, the others as they did");
        // From each path node, a step of 16 each way: one a person can make
        // and something wide cannot.
        int narrow = 0, both = 0, steps = 0;
        for (int n = 0; n < g.actors.nodeCount; ++n)
        {
            for (int turn = 0; turn < 8; ++turn)
            {
                fx sine, cosine;
                fxSinCos(static_cast<std::uint16_t>(turn * 8192), sine, cosine);
                // (Out from the node until the walk stops, a person's way.)
                fx person[3] = {g.actors.nodes[n].pos[0], g.actors.nodes[n].pos[1], g.actors.nodes[n].pos[2]};
                ActorState probe;
                for (int k = 0; k < 3; ++k)
                    probe.pos[k] = person[k];
                if (!actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(64)))
                    continue;
                for (int k = 0; k < 3; ++k)
                    person[k] = probe.pos[k];
                fx wide[3] = {person[0], person[1], person[2]};
                bool together = true;
                for (int step = 0; step < 24; ++step)
                {
                    const bool a = walkStep(g.move, nullptr, -1, kWalkStanding, person, cosine * 16, sine * 16);
                    const bool b = together && walkStep(g.move, nullptr, -1, kWalkLarge, wide, cosine * 16, sine * 16);
                    if (!a)
                        break;
                    ++steps;
                    if (together && !b)
                    {
                        ++narrow;
                        together = false;
                    }
                    both += b;
                }
            }
        }
        std::printf("test_monsters: c1a1c: of %d steps a person can take from its nodes, something wide takes %d; "
                    "%d ways are too narrow for it\n",
                    steps, both, narrow);
        check(narrow > 0 && both > steps / 2, "there are ways a person fits and a bullsquid does not; most it does");
    }

    /// The miniturret under the ceiling of c1a2: out at the sight of
    /// someone, onto them, and hurting; switched off and on by name; its
    /// plating; destroyed.
    void testMiniturret(const char *dir)
    {
        auto game = load(dir, "c1a2");
        check(game != nullptr, "c1a2 cooks");
        if (!game)
            return;
        Game &g = *game;
        const int who = findKind(g, ActorKind::Miniturret);
        check(who >= 0, "c1a2 has its miniturret");
        if (who < 0)
            return;
        ActorState &actor = g.actors.states[who];
        check((actor.flags & kActorCeiling) && (actor.flags & kActorAuto) && actor.name != 0,
              "it hangs from the ceiling, looks out for itself, and has a name");
        const ModelView *model = actorModel(g.actors, actor);
        check(model != nullptr && model->header->controllerCount == 2 && model->header->clipCount == 6,
              "its model has the two controllers it aims with, and its six clips");
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (i != who && fights(g.actors.states[i].kind))
                g.actors.states[i].active = false;
        }
        const fx home[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        outOfSight(g);
        for (int n = 0; n < 60; ++n)
            tickStill(g);
        check(actor.ai == kTurretOff && g.times(CommonSound::TurretDeploy) == 0,
              "with nobody to see, it stays folded away");
        if (!standInSight(g, who))
        {
            check(false, "somewhere it can be seen from");
            return;
        }
        int frames = 0, cameOut = -1, fired = -1;
        bool searched = false;
        for (; frames < 30 * 15 && g.player.health == 100; ++frames)
        {
            tickStill(g);
            searched = searched || actor.ai == kTurretSearch;
            if (cameOut < 0 && actor.ai >= kTurretSearch)
                cameOut = frames;
            if (fired < 0 && g.times(CommonSound::SentryFire) > 0)
                fired = frames;
        }
        fx eye[3], forward[3];
        hldata::detail::turretEye(actor, eye);
        hldata::detail::turretForward(actor, forward);
        const fx to[3] = {g.player.origin[0] - eye[0], g.player.origin[1] - eye[1], g.player.origin[2] - eye[2]};
        const double along = ((to[0] / 4096.0) * (forward[0] / 4096.0) + (to[1] / 4096.0) * (forward[1] / 4096.0) +
                              (to[2] / 4096.0) * (forward[2] / 4096.0)) /
                             (fxLength(to) / 4096.0);
        std::printf("test_monsters: miniturret: out after %.1f s, first round at %.1f s, hurts at %.1f s for %d; "
                    "its gun is within %.1f degrees of the player\n",
                    cameOut / 30.0, fired / 30.0, frames / 30.0, 100 - g.player.health,
                    std::acos(std::min(1.0, along)) * 180.0 / M_PI);
        check(searched && g.player.health < 100 && g.times(CommonSound::TurretAlert) == 1 &&
                  g.times(CommonSound::TurretDeploy) == 1,
              "it sees the player, comes out, turns to them and hurts them");
        check(along > 0.99, "... its gun on them when it does");
        check(g.loudestHeard[static_cast<std::size_t>(CommonSound::TurretAlert)] == 64 &&
                  g.loudestHeard[static_cast<std::size_t>(CommonSound::TurretDeploy)] == 64,
              "... its alarm and its opening at half of full, as the game's TURRET_MACHINE_VOLUME");
        bool tracer = false;
        for (const Effect &e : g.actors.effects)
            tracer = tracer || e.kind == kEffectTracer;
        check(tracer, "... and its rounds are there to be seen");
        std::int32_t controls[kModelControls];
        check(actorControls(actor, controls) && (controls[0] != 0 || controls[1] != 0),
              "... and its model is posed with the gun turned");

        // Switched off by name (the door of the room it guards does this):
        // it folds away, and stays so though the player is in plain view.
        logicFire(g.logic, g.player, actor.name, -1, kUseOff, 0);
        const int healthThen = g.player.health;
        int away = 0;
        for (; away < 30 * 12 && actor.ai != kTurretOff; ++away)
            tickStill(g);
        const int healthAway = g.player.health;
        for (int n = 0; n < 30 * 4; ++n)
            tickStill(g);
        std::printf("test_monsters: miniturret: folded away %.1f s after being switched off\n", away / 30.0);
        check(actor.ai == kTurretOff && g.player.health == healthAway && healthAway >= healthThen - 10,
              "switched off by name, it folds away and stays away");
        // Folded away it hardly feels a round; out, its plating turns one
        // and the rest of it does not.
        const int before = actor.health;
        monsterHurt(g.logic, who, 8, true, kHitBody);
        const int folded = before - actor.health;
        logicFire(g.logic, g.player, actor.name, -1, kUseOn, 0);
        for (int n = 0; n < 30 * 3 && actor.ai < kTurretSearch; ++n)
            tickStill(g);
        check(actor.ai >= kTurretSearch && actor.ai <= kTurretActive, "switched on by name, it comes out again");
        const int out = actor.health;
        monsterHurt(g.logic, who, 8, true, kHitArmour);
        const int plated = out - actor.health;
        monsterHurt(g.logic, who, 8, true, kHitBody);
        const int open = out - plated - actor.health;
        check(folded == 8 && plated == 8 && open == 80,
              "a round is a tenth of itself to one folded away or on its plating, and whole elsewhere");
        // Destroyed: it goes wild first (under a quarter of its health),
        // then is done for, and is not solid no more than it was.
        g.player.health = 100;
        int rounds = 0;
        bool wild = false;
        for (; rounds < 20 && actor.ai < kAiDying; ++rounds)
        {
            monsterHurt(g.logic, who, 8, true, kHitBody);
            wild = wild || actor.attack != 0;
        }
        for (int n = 0; n < 30 * 8 && actor.ai != kAiDead; ++n)
            tickStill(g);
        check(wild && actor.ai == kAiDead && g.times(CommonSound::TurretDie) == 1,
              "shot to pieces: wild first, then destroyed");
        const int healthDead = g.player.health;
        for (int n = 0; n < 30 * 3; ++n)
            tickStill(g);
        check(g.player.health == healthDead, "and what is destroyed fires no more");
        (void)home;
    }

    /// The sentries of c1a3: asleep until their tripwire's relay names them;
    /// one woken (or shot) wakes the others of its name, and no others.
    void testSentries(const char *dir)
    {
        auto game = load(dir, "c1a3");
        check(game != nullptr, "c1a3 cooks");
        if (!game)
            return;
        Game &g = *game;
        std::vector<int> sentries;
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Sentry))
                sentries.push_back(i);
        }
        check(sentries.size() == 5, "c1a3 has its five sentries");
        if (sentries.size() != 5)
            return;
        outOfSight(g);
        for (int n = 0; n < 60; ++n)
            tickStill(g);
        bool asleep = true;
        for (int i : sentries)
            asleep = asleep && g.actors.states[i].ai == kTurretOff && !(g.actors.states[i].flags & kActorAuto) &&
                     !(g.actors.states[i].flags & kActorCeiling) && g.actors.states[i].fires == g.actors.states[i].name;
        check(asleep, "they stand asleep, each set to rouse its own name");
        // Two share a name with another; one is alone.
        int pair = -1, other = -1;
        for (int i : sentries)
        {
            for (int k : sentries)
            {
                if (i != k && pair < 0 && g.actors.states[i].name == g.actors.states[k].name)
                {
                    pair = i;
                    other = k;
                }
            }
        }
        check(pair >= 0, "two of them go by one name");
        if (pair < 0)
            return;
        // A round into one of the pair: it wakes, and wakes the other.
        monsterHurt(g.logic, pair, 4, true, kHitBody);
        for (int n = 0; n < 30 * 3; ++n)
            tickStill(g);
        int awake = 0;
        for (int i : sentries)
            awake += g.actors.states[i].ai != kTurretOff;
        check(g.actors.states[pair].ai != kTurretOff && g.actors.states[other].ai != kTurretOff && awake == 2 &&
                  g.actors.states[pair].health == 400 - 40,
              "one that is shot wakes, wakes the other of its name, and nobody else");
        // A sentry does not fold away again, however long nobody comes.
        for (int n = 0; n < 30 * 25; ++n)
            tickStill(g);
        check(g.actors.states[pair].ai == kTurretSearch, "a sentry that is up stays up");
        // The tripwires' relays: fire each, and all five are up.
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::TriggerRelay))
                logicUse(g.logic, g.player, static_cast<int>(i), -1, kUseToggle, 0);
        }
        for (int n = 0; n < 30 * 3; ++n)
            tickStill(g);
        awake = 0;
        for (int i : sentries)
            awake += g.actors.states[i].ai != kTurretOff;
        check(awake == 5, "the tripwires' relays wake the rest");
        // And one of them, with someone to see, shoots them.
        g.player.origin[2] -= fxFromInt(30000);
        g.player.health = 100;
        int who = -1;
        for (int i : sentries)
        {
            if (who < 0 && standInSight(g, i))
                who = i;
        }
        check(who >= 0, "somewhere a sentry can be seen from");
        int frames = 0;
        for (; who >= 0 && frames < 30 * 10 && g.player.health == 100; ++frames)
            tickStill(g);
        std::printf("test_monsters: sentry: hurts after %.1f s\n", frames / 30.0);
        check(g.player.health < 100 && g.times(CommonSound::SentryFire) > 0, "a sentry that sees the player shoots them");
    }

    /// The big turret (the game's only one is in c3a1): switched on by name,
    /// and its barrels have to get going before the first round.
    void testBigTurret(const char *dir)
    {
        auto game = load(dir, "c3a1");
        check(game != nullptr, "c3a1 cooks");
        if (!game)
            return;
        Game &g = *game;
        const int who = findKind(g, ActorKind::Turret);
        check(who >= 0, "c3a1 has the turret");
        if (who < 0)
            return;
        ActorState &actor = g.actors.states[who];
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (i != who && fights(g.actors.states[i].kind))
                g.actors.states[i].active = false;
        }
        if (!standInSight(g, who))
        {
            check(false, "somewhere the turret can be seen from");
            return;
        }
        for (int n = 0; n < 60; ++n)
            tickStill(g);
        check(actor.ai == kTurretOff && !(actor.flags & kActorAuto) && g.player.health == 100,
              "it does not look out for itself: off until it is switched on");
        logicFire(g.logic, g.player, actor.name, -1, kUseToggle, 0);
        int frames = 0, up = -1, first = -1;
        for (; frames < 30 * 20 && g.player.health == 100; ++frames)
        {
            tickStill(g);
            if (up < 0 && actor.ai >= kTurretSearch)
                up = frames;
            if (first < 0 && g.times(CommonSound::TurretFire) > 0)
                first = frames;
        }
        std::printf("test_monsters: turret: up after %.1f s, first round %.1f s after that\n", up / 30.0,
                    (first - up) / 30.0);
        check(up >= 0 && first >= 0 && g.times(CommonSound::TurretSpinUp) == 1 && first - up >= 60,
              "switched on, it comes out, and its barrels take two seconds and more to get going");
        check(g.player.health <= 90, "and a round of its is ten");
    }

    void testNodes(const char *dir)
    {
        auto game = load(dir, "c1a1");
        check(game != nullptr, "c1a1 cooks");
        if (!game)
            return;
        const int n = static_cast<int>(game->view.pathNodeCount);
        const ActorWorld &a = game->actors;
        int joined = 0, ways = 0;
        // Following the search from anywhere to anywhere it says there is a
        // way gets there, by links only.
        bool arrives = true;
        for (int from = 0; from < n; ++from)
        {
            for (int to = 0; to < n; ++to)
            {
                if (from == to)
                    continue;
                const std::uint8_t next = pathStep(a, from, to, kPathLinkHuman, true);
                ways += next != kNoPathNode;
                joined += next == to && pathLinkBetween(a, from, to) >= 0;
                if (next == kNoPathNode)
                    continue;
                int at = from, hops = 0;
                while (at != to && hops++ <= n)
                {
                    const int step = pathStep(a, at, to, kPathLinkHuman, true);
                    if (step == kNoPathNode || pathLinkBetween(a, at, step) < 0)
                        break;
                    at = step;
                }
                arrives = arrives && at == to;
            }
        }
        std::printf("test_monsters: c1a1 has %d nodes, %d links, %d of %d pairs joined (%d bytes)\n", n,
                    static_cast<int>(game->view.pathLinkCount), ways, n * (n - 1),
                    static_cast<int>(game->view.pathLinkCount * sizeof(PathLink)));
        check(n >= 15, "c1a1's path nodes are cooked");
        check(joined >= n, "... and joined to one another");
        check(arrives, "... and the ways found all arrive");
    }

    void testHeadcrab(const char *dir, int vblanks)
    {
        auto game = load(dir, "c1a1f");
        if (!game)
        {
            check(false, "c1a1f cooks");
            return;
        }
        Game &g = *game;
        // Whichever of the map's headcrabs has room about it.
        int crab = -1;
        for (int nth = 0; nth < 8 && crab < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Headcrab, nth);
            if (candidate >= 0 && standNear(g, candidate, 160))
                crab = candidate;
        }
        check(crab >= 0, "a headcrab with room to be met in");
        if (crab < 0)
            return;
        ActorState &actor = g.actors.states[crab];
        // The others are not part of this.
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (i != crab && isMonster(g.actors.states[i].kind))
                g.actors.states[i].active = false;
        }
        bool leapt = false, solid = false;
        int frames = 0;
        for (; frames < 600 && g.player.health == 100; ++frames)
        {
            tick(g, vblanks);
            leapt = leapt || actor.ai == kAiLeap;
            solid = solid || inSolid(g, actor);
        }
        if (std::getenv("HLDS_AI_TRACE") != nullptr)
            std::printf("  crab: leapt %d frames %d health %d alert %d attack %d bite %d\n", leapt, frames, g.player.health,
                        g.times(CommonSound::HeadcrabAlert), g.times(CommonSound::HeadcrabAttack),
                        g.times(CommonSound::HeadcrabBite));
        char what[96];
        std::snprintf(what, sizeof(what), "at %d blanks a frame the headcrab sees, leaps and bites", vblanks);
        check(leapt && g.player.health == 90, what);
        check(g.times(CommonSound::HeadcrabAlert) == 1 && g.times(CommonSound::HeadcrabAttack) >= 1 &&
                  g.times(CommonSound::HeadcrabBite) == 1,
              "... and is heard: seeing them, leaping, biting");
        // It keeps at it; it is never in a wall.
        for (int n = 0; n < 1200 / vblanks; ++n)
        {
            tick(g, vblanks);
            solid = solid || inSolid(g, actor);
        }
        check(!solid, "... and is never inside the world");
        check(g.player.health < 90, "... and bites again");

        // Shot: one round of the pistol is not enough (8 of its 10), two are.
        g.player.health = 100;
        g.player.inv.have = 1u << kWeaponGlock;
        g.player.inv.current = kWeaponGlock;
        g.player.inv.clipGlock = 17;
        WeaponState weapon;
        int rounds = 0;
        for (int n = 0; n < 900 && actor.ai < kAiDying; ++n)
        {
            tick(g, vblanks);
            // Aimed at its middle, as someone good would.
            const fx eye[3] = {g.player.origin[0], g.player.origin[1],
                               g.player.origin[2] + g.player.viewHeight};
            fx to[3] = {actor.pos[0] - eye[0], actor.pos[1] - eye[1],
                        actor.pos[2] + fxFromInt(10) - eye[2]};
            const fx length = fxLength(to);
            if (length == 0)
                continue;
            for (fx &c : to)
                c = static_cast<fx>((static_cast<std::int64_t>(c) << kFxShift) / length);
            const fx right[3] = {to[1], -to[0], 0}, up[3] = {0, 0, kFxOne};
            const std::uint32_t did = weaponsTick(g.logic, g.player, weapon, eye, to, right, up, true,
                                                  false, (vblanks * 1000 + 30) / 60);
            rounds += (did & kWeaponHit) ? 1 : 0;
        }
        g.drain();
        check(actor.ai >= kAiDying && rounds == 2, "two rounds of the pistol kill it");
        for (int n = 0; n < 200; ++n)
            tick(g, vblanks);
        check(g.times(CommonSound::Pistol) >= 2 && g.times(CommonSound::HeadcrabDie) == 1,
              "... the shots are heard, and its death");
        check(actor.ai == kAiDead && !(actor.flags & kActorSolid),
              "... and it lies where it fell, in nobody's way");
    }

    void testRoundTheCorner(const char *dir)
    {
        auto game = load(dir, "c1a1f");
        if (!game)
            return;
        Game &g = *game;
        const int n = g.actors.nodeCount;
        // A headcrab and a node it cannot see but can get to, a few links off.
        int crab = -1, node = -1;
        for (int nth = 0; nth < 8 && node < 0; ++nth)
        {
            crab = findKind(g, ActorKind::Headcrab, nth);
            if (crab < 0)
                break;
            const ActorState &actor = g.actors.states[crab];
            const int mine = pathNearest(g.actors, g.move, actor.pos);
            if (mine < 0 || !walkClear(g.move, &g.actors, crab, true, actor.pos,
                                       g.actors.nodes[mine].pos, 48))
                continue;
            for (int b = 0; b < n && node < 0; ++b)
            {
                int hops = 0, at = mine;
                while (at != b && pathStep(g.actors, at, b, kPathLinkSmall, false) != kNoPathNode && hops < n)
                {
                    at = pathStep(g.actors, at, b, kPathLinkSmall, false);
                    ++hops;
                }
                const fx eye[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(20)};
                const fx there[3] = {g.actors.nodes[b].pos[0], g.actors.nodes[b].pos[1],
                                     g.actors.nodes[b].pos[2] + fxFromInt(64)};
                if (at == b && hops >= 2 && !lineClear(g.move, eye, there))
                    node = b;
            }
        }
        check(node >= 0, "a headcrab with a way to somewhere out of its sight");
        if (node < 0)
            return;
        ActorState &actor = g.actors.states[crab];
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (i != crab && isMonster(g.actors.states[i].kind))
                g.actors.states[i].active = false;
        }
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = g.actors.nodes[node].pos[a];
        g.player.origin[2] += kStandingFeet;
        const fx d0[3] = {g.player.origin[0] - actor.pos[0], g.player.origin[1] - actor.pos[1], 0};
        const double start = toUnits(fxLength(d0));
        // It has not seen them. Stung, it comes looking.
        for (int f = 0; f < 60; ++f)
            tick(g, 2);
        check(actor.ai == kAiIdle, "what it cannot see it does not chase");
        tick(g, 2); // (its first tick has given it its health)
        monsterHurt(g.logic, crab, 1, false);
        bool solid = false;
        int frames = 0;
        // (While it walks it turns at its own yaw speed, yaw_speed * 2
        // degrees a second -- not faster, whatever the way does.)
        int walkedTurn = 0, tooFast = 0;
        for (; frames < 30 * 40 && g.player.health == 100; ++frames)
        {
            const AiMonster *m = aiFind(g.actors, crab);
            const bool walking = m != nullptr && m->movementGoal != kMoveGoalNone && !m->inAir;
            const int most = m != nullptr ? (m->yawSpeed * 2 * 65536 * 33) / (360 * 1000) + 2 : 0;
            const std::uint16_t before = actor.yaw;
            tick(g, 2);
            solid = solid || inSolid(g, actor);
            m = aiFind(g.actors, crab);
            if (walking && m != nullptr && m->movementGoal != kMoveGoalNone && !m->inAir)
            {
                const int turned = static_cast<std::int16_t>(actor.yaw - before);
                walkedTurn += turned < 0 ? -turned : turned;
                tooFast += (turned < 0 ? -turned : turned) > most;
            }
        }
        const fx d1[3] = {g.player.origin[0] - actor.pos[0], g.player.origin[1] - actor.pos[1], 0};
        std::printf("test_monsters: round the corner from %.0f units: bitten after %.1f s (now %.0f off); "
                    "turned %.0f degrees walking, %d frames too fast\n",
                    start, frames / 30.0, toUnits(fxLength(d1)), walkedTurn * 360.0 / 65536, tooFast);
        check(g.player.health < 100 && !solid, "stung, it finds them by the nodes and bites");
        check(walkedTurn >= 8192 && tooFast == 0, "walking round the corner it turns, at its yaw speed");
    }

    void testZombie(const char *dir)
    {
        auto game = load(dir, "c1a1a");
        if (!game)
        {
            check(false, "c1a1a cooks");
            return;
        }
        Game &g = *game;
        int zombie = -1;
        for (int nth = 0; nth < 4 && zombie < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Zombie, nth);
            if (candidate >= 0 && standNear(g, candidate, 128))
                zombie = candidate;
        }
        check(zombie >= 0, "a zombie with room to be met in");
        if (zombie < 0)
            return;
        ActorState &actor = g.actors.states[zombie];
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (i != zombie && isMonster(g.actors.states[i].kind))
                g.actors.states[i].active = false;
        }
        // It faces them, for the test's sake (a zombie with its back turned
        // hears only what is close).
        actor.yaw = fxAtan2Turns(g.player.origin[1] - actor.pos[1], g.player.origin[0] - actor.pos[0]);
        bool swung = false, solid = false;
        int frames = 0;
        for (; frames < 30 * 30 && g.player.health == 100; ++frames)
        {
            tick(g, 2);
            swung = swung || attacking(g, zombie);
            solid = solid || inSolid(g, actor);
        }
        std::printf("test_monsters: the zombie hits after %.1f s, for %d\n", frames / 30.0,
                    100 - g.player.health);
        check(swung && (g.player.health == 80 || g.player.health == 60) && !solid,
              "the zombie walks up and hits, for what a zombie's blow is worth");
        // (Its growl comes with half its swings, as the game's does.)
        check(g.times(CommonSound::ClawStrike) >= 1, "... and its blow is heard");
        // The crowbar, with the button held: a whole blow of ten and then
        // halves, as the game has it -- nine for its fifty; and the hand's
        // two swings that land turn about.
        g.player.health = 100;
        g.player.inv.have = 1u << kWeaponCrowbar;
        g.player.inv.current = kWeaponCrowbar;
        WeaponState weapon;
        int blows = 0, swings = 0;
        bool turnAbout = true;
        std::string lastClip;
        g.drain();
        std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
        for (int n = 0; n < 900 && actor.ai < kAiDying; ++n)
        {
            tick(g, 2);
            g.player.health = 100;
            const fx eye[3] = {g.player.origin[0], g.player.origin[1],
                               g.player.origin[2] + g.player.viewHeight};
            // (Level, as someone standing swings: the game's zombie slashes
            // from as far as 64, where a blow aimed down falls short.)
            fx to[3] = {actor.pos[0] - eye[0], actor.pos[1] - eye[1], 0};
            const fx length = fxLength(to);
            if (length == 0)
                continue;
            for (fx &c : to)
                c = static_cast<fx>((static_cast<std::int64_t>(c) << kFxShift) / length);
            const fx right[3] = {to[1], -to[0], 0}, up[3] = {0, 0, kFxOne};
            const std::uint32_t did =
                weaponsTick(g.logic, g.player, weapon, eye, to, right, up, true, false, 33);
            if (did & kWeaponHit)
            {
                ++blows;
                const std::string clip = weapon.anim != nullptr ? weapon.anim : "";
                turnAbout = turnAbout && (clip == "attack2hit" || clip == "attack3hit") && clip != lastClip;
                lastClip = clip;
            }
            if (did & kWeaponFired)
                ++swings;
            weapon.anim = nullptr;
            if (std::getenv("HLDS_AI_TRACE") != nullptr && (did & kWeaponFired))
            {
                const AiMonster *m = aiFind(g.actors, zombie);
                const fx d[3] = {actor.pos[0] - g.player.origin[0], actor.pos[1] - g.player.origin[1], 0};
                std::printf("  swing n=%d hit=%d st=%d sched=%s task=%d act=%d hp=%d dist=%d yaw=%u\n", n, (did & kWeaponHit) != 0,
                            m->state, m->schedule ? m->schedule->name : "-", m->scheduleIndex, m->activity, actor.health,
                            fxToInt(fxLength(d)), actor.yaw);
            }
        }
        g.drain();
        std::printf("test_monsters: the crowbar: %d blows of %d swings; the zombie at %d (ai %d); flesh %d, metal %d\n",
                    blows, swings, actor.health, actor.ai,
                    g.times(CommonSound::CrowbarFlesh) + g.times(CommonSound::CrowbarFlesh2) + g.times(CommonSound::CrowbarFlesh3),
                    g.times(CommonSound::CrowbarHit) + g.times(CommonSound::CrowbarHit2));
        check(actor.ai >= kAiDying && blows == 9, "a whole blow of the crowbar and eight halves kill it");
        check(turnAbout, "... the hand swinging one way and the other, turn about");
        check(g.times(CommonSound::CrowbarMiss) == swings && swings >= blows,
              "every swing is heard through the air, whether it lands or not");
        check(g.times(CommonSound::CrowbarFlesh) + g.times(CommonSound::CrowbarFlesh2) + g.times(CommonSound::CrowbarFlesh3) == blows &&
                  g.times(CommonSound::CrowbarHit) + g.times(CommonSound::CrowbarHit2) == 0,
              "and a blow on flesh sounds of flesh, not of metal");
        // Swung at the air (straight up, in a room): the three swings in
        // turn, half a second apart, and nothing hit.
        {
            WeaponState air;
            const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
            const fx skyward[3] = {0, 0, kFxOne}, right[3] = {kFxOne, 0, 0}, up[3] = {0, -kFxOne, 0};
            std::string order;
            int landed = 0, between = 0, last = -1;
            for (int n = 0; n < 60; ++n)
            {
                const std::uint32_t did = weaponsTick(g.logic, g.player, air, eye, skyward, right, up, true, false, 33);
                landed += (did & kWeaponHit) ? 1 : 0;
                if (did & kWeaponFired)
                {
                    order += std::string(air.anim != nullptr ? air.anim : "?") + " ";
                    between = last < 0 ? between : n - last;
                    last = n;
                }
                air.anim = nullptr;
            }
            check(order.rfind("attack1miss attack2 attack3 attack1miss ", 0) == 0,
                  "at the air it is the three other swings, in turn");
            check(landed == 0 && between >= 15 && between <= 17, "half a second apart");
        }
        // Swung at the floor from just above it: a blow on a wall is a
        // blow that landed -- a quarter of a second, the ring of it and the
        // sound of what the floor is made of -- and its dent is there a
        // fifth of a second later, not at once.
        {
            WeaponState wall;
            const fx low[3] = {g.player.origin[0], g.player.origin[1],
                               g.player.origin[2] - kStandingFeet + fxFromInt(20)};
            const fx down[3] = {0, 0, -kFxOne}, right[3] = {kFxOne, 0, 0}, up[3] = {0, kFxOne, 0};
            auto dents = [&]()
            {
                int n = 0;
                for (const Decal &decal : g.actors.decals)
                    n += decal.active && decal.picture == kDecalDent;
                return n;
            };
            g.drain();
            std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
            const int before = dents();
            const std::uint32_t did = weaponsTick(g.logic, g.player, wall, low, down, right, up, true, false, 33);
            g.drain();
            check((did & kWeaponHit) && wall.cooldownMs == 250 && wall.anim != nullptr &&
                      std::string(wall.anim) == "attack2hit",
                  "a blow on the floor lands: the swing that lands, and a quarter of a second");
            check(g.times(CommonSound::CrowbarMiss) == 1 &&
                      g.times(CommonSound::CrowbarHit) + g.times(CommonSound::CrowbarHit2) == 1,
                  "it is heard through the air, and it rings");
            check(dents() == before, "its dent is not there yet");
            for (int n = 0; n < 5; ++n)
                weaponsTick(g.logic, g.player, wall, low, down, right, up, false, false, 33);
            check(dents() == before, "... nor after a sixth of a second");
            for (int n = 0; n < 3; ++n)
                weaponsTick(g.logic, g.player, wall, low, down, right, up, false, false, 33);
            check(dents() == before + 1, "... and is a fifth of a second after the blow");
            // On the heels of the first, the next is half a blow.
            wall.anim = nullptr;
            weaponsTick(g.logic, g.player, wall, low, down, right, up, true, false, 33);
            check(wall.blow == kCrowbarBlow / 2 && wall.anim != nullptr && std::string(wall.anim) == "attack3hit",
                  "the blow after it is half as hard, and the hand swings the other way");
            for (int n = 0; n < 45; ++n)
                weaponsTick(g.logic, g.player, wall, low, down, right, up, false, false, 33);
            weaponsTick(g.logic, g.player, wall, low, down, right, up, true, false, 33);
            check(wall.blow == kCrowbarBlow, "after a second's rest it is a whole one again");
        }
    }

    void testMaker(const char *dir)
    {
        auto game = load(dir, "c1a0c");
        if (!game)
        {
            check(false, "c1a0c cooks");
            return;
        }
        Game &g = *game;
        const int maker = g.recordOfKind(LogicKind::Maker);
        check(maker >= 0, "c1a0c has a monstermaker");
        if (maker < 0)
            return;
        const LogicRecord &record = g.view.logic[maker];
        const int first = static_cast<int>(record.extra[0]);
        check(record.extra[1] >= 1 && !g.actors.states[first].active,
              "its children's places are kept, and empty");
        // Somewhere else, so as not to be stood where they are born.
        g.player.origin[0] += fxFromInt(4000);
        logicUse(g.logic, g.player, maker, -1, kUseToggle);
        for (int n = 0; n < 90; ++n)
        {
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
        }
        check(g.actors.states[first].active && isMonster(g.actors.states[first].kind),
              "used, it makes one");
    }

    void testItems(const char *dir)
    {
        auto game = load(dir, "c1a1");
        if (!game)
            return;
        Game &g = *game;
        int crowbar = -1;
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (g.actors.states[i].active &&
                g.actors.states[i].item == static_cast<std::uint8_t>(ItemKind::Crowbar))
                crowbar = i;
        }
        check(crowbar >= 0, "the crowbar lies in c1a1");
        if (crowbar < 0)
            return;
        check(g.player.inv.have == 0, "the player starts with nothing");
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = g.actors.states[crowbar].pos[a];
        g.player.origin[2] += kStandingFeet;
        monstersTick(g.logic, g.player, 33);
        check(!g.actors.states[crowbar].active && g.player.inv.current == kWeaponCrowbar &&
                  (g.actors.got & kGotWeapon),
              "walked over, it is theirs");
    }

    /// One of the newer kinds, met in the open in its own map: it must be
    /// roused, make the attack that is its own (heard as `attackSound`),
    /// hurt the player, never be inside the world, and die of being shot.
    void testKind(const char *dir, const char *map, ActorKind kind, const char *what,
                  CommonSound attackSound, int distance, bool wantShot, bool wantBolt)
    {
        auto game = load(dir, map);
        char text[128];
        std::snprintf(text, sizeof(text), "%s cooks", map);
        check(game != nullptr, text);
        if (!game)
            return;
        Game &g = *game;
        int who = -1;
        for (int nth = 0; nth < 12 && who < 0; ++nth)
        {
            const int candidate = findKind(g, kind, nth);
            // (Not one that waits to be seen: the player here faces one way.)
            if (candidate >= 0 && !(g.actors.states[candidate].flags & kActorWaitSeen) &&
                standNear(g, candidate, distance))
                who = candidate;
        }
        std::snprintf(text, sizeof(text), "%s: one with room to be met in", what);
        check(who >= 0, text);
        if (who < 0)
            return;
        ActorState &actor = g.actors.states[who];
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (i != who && isMonster(g.actors.states[i].kind))
                g.actors.states[i].active = false;
        }
        actor.yaw = fxAtan2Turns(g.player.origin[1] - actor.pos[1], g.player.origin[0] - actor.pos[0]);
        std::printf("test_monsters: (%s at %d %d %d is met from %d %d %d)\n", what, fxToInt(actor.pos[0]),
                    fxToInt(actor.pos[1]), fxToInt(actor.pos[2]), fxToInt(g.player.origin[0]),
                    fxToInt(g.player.origin[1]), fxToInt(g.player.origin[2]));
        bool attacked = false, solid = false, shot = false, bolt = false;
        int frames = 0;
        for (; frames < 30 * 40 && g.player.health == 100; ++frames)
        {
            tick(g, 2);
            attacked = attacked || attacking(g, who);
            if (std::getenv("HLDS_AI_TRACE") != nullptr && frames % 15 == 0)
            {
                const AiMonster *m = aiFind(g.actors, who);
                std::printf("  %s t=%d st=%d sched=%s task=%d act=%d enemy=%d cond=%08x flags=%d\n", what, frames * 33,
                            m->state, m->schedule ? m->schedule->name : "-", m->scheduleIndex, m->activity, m->enemy,
                            m->conditions, actor.flags);
            }
            solid = solid || inSolid(g, actor);
            for (const Projectile &p : g.actors.shots)
                shot = shot || p.active;
            for (const Effect &e : g.actors.effects)
                bolt = bolt || e.kind == kEffectBolt;
        }
        std::printf("test_monsters: %s: hurts after %.1f s, for %d\n", what, frames / 30.0,
                    100 - g.player.health);
        std::snprintf(text, sizeof(text), "%s: is roused, attacks and hurts", what);
        check(attacked && g.player.health < 100 && !solid, text);
        std::snprintf(text, sizeof(text), "%s: its attack is heard", what);
        check(g.times(attackSound) >= 1, text);
        if (wantShot)
            check(shot, "... and what it spat was in the air on the way");
        if (wantBolt)
            check(bolt, "... and its bolt was there to be seen");

        // Shot until it drops: its health in pistol rounds, no more.
        const int needs = (monsterKind(actor.kind).health + 7) / 8;
        g.player.inv.have = 1u << kWeaponGlock;
        g.player.inv.current = kWeaponGlock;
        g.player.inv.clipGlock = 17;
        WeaponState weapon;
        int rounds = 0;
        for (int n = 0; n < 900 && actor.ai < kAiDying; ++n)
        {
            tick(g, 2);
            g.player.health = 100;
            const fx eye[3] = {g.player.origin[0], g.player.origin[1],
                               g.player.origin[2] + g.player.viewHeight};
            fx to[3] = {actor.pos[0] - eye[0], actor.pos[1] - eye[1],
                        actor.pos[2] + monsterKind(actor.kind).height / 2 - eye[2]};
            const fx length = fxLength(to);
            if (length == 0)
                continue;
            for (fx &c : to)
                c = static_cast<fx>((static_cast<std::int64_t>(c) << kFxShift) / length);
            const fx right[3] = {to[1], -to[0], 0}, up[3] = {0, 0, kFxOne};
            const std::uint32_t did =
                weaponsTick(g.logic, g.player, weapon, eye, to, right, up, true, false, 33);
            rounds += (did & kWeaponHit) ? 1 : 0;
        }
        g.drain();
        // (Its health in rounds, or fewer where some were to the head: a
        // slave carries its head low and in front, where its middle is.)
        std::printf("test_monsters: %s: %d rounds of the pistol kill it (%d to the body would)\n", what, rounds,
                    needs);
        std::snprintf(text, sizeof(text), "%s: the pistol kills it, in no more rounds than its health", what);
        check(actor.ai >= kAiDying && rounds <= needs && rounds >= (needs + 2) / 3, text);
    }

    /// The last four, on a zombie in c1a1a; and the weaponbox.
    void testLastWeapons(const char *dir)
    {
        auto game = load(dir, "c1a1a");
        if (!game)
            return;
        Game &g = *game;
        int zombie = -1;
        for (int nth = 0; nth < 4 && zombie < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Zombie, nth);
            if (candidate >= 0 && standNear(g, candidate, 250))
                zombie = candidate;
        }
        check(zombie >= 0, "a zombie for the last weapons");
        if (zombie < 0)
            return;
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (i != zombie && isMonster(g.actors.states[i].kind))
                g.actors.states[i].active = false;
        }
        ActorState &actor = g.actors.states[zombie];
        const fx home[3] = {actor.pos[0], actor.pos[1], actor.pos[2]};
        const fx stand[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        Arsenal ars;
        g.player.arsenal = &ars;
        tick(g, 2);
        WeaponState weapon;
        fx eye[3], forward[3], right[3];
        const fx up[3] = {0, 0, kFxOne};
        int aimHigh = 36; // (where on the zombie: its chest; the RPG its head, over the floor)
        auto aim = [&]()
        {
            for (int k = 0; k < 3; ++k)
                g.player.origin[k] = stand[k];
            eye[0] = g.player.origin[0];
            eye[1] = g.player.origin[1];
            eye[2] = g.player.origin[2] + g.player.viewHeight;
            const fx at[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(aimHigh)};
            fx to[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
            const fx length = fxLength(to);
            for (int k = 0; k < 3; ++k)
                forward[k] = static_cast<fx>((static_cast<std::int64_t>(to[k]) << kFxShift) / length);
            const fx level[3] = {forward[0], forward[1], 0};
            const fx flat = fxLength(level);
            right[0] = static_cast<fx>((static_cast<std::int64_t>(forward[1]) << kFxShift) / flat);
            right[1] = static_cast<fx>((static_cast<std::int64_t>(-forward[0]) << kFxShift) / flat);
            right[2] = 0;
        };
        auto hold = [&]()
        {
            for (int k = 0; k < 3; ++k)
                actor.pos[k] = home[k];
            g.player.health = 100;
        };
        auto fresh = [&](int health)
        {
            actor.active = true;
            actor.ai = 0;
            actor.health = health;
            aiForget(g.actors, zombie); // (a new one: it thinks afresh)
            for (Projectile &p : g.actors.shots)
                p.active = false;
            weapon = WeaponState();
        };
        auto use = [&](bool attack, bool second = false)
        {
            return weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, attack, false, 33, second);
        };
        std::uint32_t got = 0;

        // The pickups, by the game's amounts.
        check(hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Rpg), got) &&
                  hasWeapon(g.player, kWeaponRpg) && (ars.have & kArsenalRpgLoaded) && ars.rockets == 0,
              "the RPG comes with its rocket in it");
        check(hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::RpgClip), got) && ars.rockets == 1,
              "... a box of rockets is one more");
        check(hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Gauss), got) &&
                  hasWeapon(g.player, kWeaponGauss) && ars.uranium == 20,
              "the gauss comes with twenty uranium");
        check(hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Egon), got) &&
                  hasWeapon(g.player, kWeaponEgon) && ars.uranium == 40,
              "... the egon with twenty more, the same store");
        check(hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Hornetgun), got) &&
                  hasWeapon(g.player, kWeaponHornetgun) && ars.hornets == 8,
              "the hornetgun comes full");
        check(!hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Hornetgun), got),
              "... and another is no use: it grows its own");
        // Slot 4: the game's heavy weapons, in its order.
        g.player.inv.current = kWeaponCrowbar;
        check(weaponOfCategory(g.player, 4) == kWeaponRpg, "slot 4: the RPG first");
        g.player.inv.current = kWeaponRpg;
        check(weaponOfCategory(g.player, 4) == kWeaponGauss, "... then the gauss");
        g.player.inv.current = kWeaponGauss;
        check(weaponOfCategory(g.player, 4) == kWeaponEgon, "... the egon");
        g.player.inv.current = kWeaponEgon;
        check(weaponOfCategory(g.player, 4) == kWeaponHornetgun, "... the hornetgun, and round again");

        // The RPG: a rocket that is slow, then fast, and goes off at the
        // zombie; then two seconds and another is in.
        fresh(monsterKind(actor.kind).health); // (a zombie as it comes)
        g.player.inv.current = kWeaponRpg;
        aimHigh = 64;
        aim();
        std::uint32_t did = use(true);
        bool flying = false;
        for (const Projectile &p : g.actors.shots)
            flying = flying || (p.active && p.kind == kShotRocket);
        check((did & kWeaponFired) && flying && !(ars.have & kArsenalRpgLoaded), "the RPG fires its rocket");
        int frames = 0;
        for (; frames < 90 && actor.ai < kAiDying; ++frames)
        {
            hold();
            aim();
            use(false);
            tick(g, 2);
        }
        std::printf("test_monsters: the rocket gets to a zombie 250 off in %d frames\n", frames);
        check(actor.ai >= kAiDying && g.times(CommonSound::Explode) >= 1, "... which goes off at the zombie and kills it");
        // (A second and a half before it may be fired again, and then two
        // to put the next in.)
        for (int t = 0; t < 3600; t += 33)
            use(false);
        check((ars.have & kArsenalRpgLoaded) && ars.rockets == 0, "two seconds on, the next is in");
        did = use(false, true);
        check((ars.have & kArsenalLaserOff), "the other trigger puts its laser out");
        weapon.secondMs = 0;
        use(false, true);
        check(!(ars.have & kArsenalLaserOff), "... and on again");

        // The gauss: twenty a shot, two uranium; charged, more, and a kick.
        aimHigh = 36;
        fresh(400);
        g.player.inv.current = kWeaponGauss;
        aim();
        ars.uranium = 100;
        did = use(true);
        check((did & kWeaponFired) && ars.uranium == 98 && actor.health == 380, "the gauss: twenty, for two uranium");
        weapon.cooldownMs = 0;
        int frame = 0;
        for (; frame < 4200 / 33; ++frame)
            use(false, true);
        const int spent = 98 - ars.uranium;
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        did = use(false, false);
        const fx kick[3] = {g.player.velocity[0], g.player.velocity[1], g.player.velocity[2]};
        std::printf("test_monsters: a full gauss charge: %d uranium, the zombie at %d\n", spent, actor.health);
        check((did & kWeaponSecondary) && spent >= 13 && spent <= 15 && actor.health <= 380 - 190,
              "charged four seconds: about two hundred, for a uranium every third of a second");
        check(fxToInt(fxLength(kick)) >= 900, "... and the player is thrown back by it");
        // Held past ten seconds: in the hand.
        fresh(400);
        weapon.cooldownMs = 0;
        bool burnt = false;
        for (frame = 0; frame < 10200 / 33 && !burnt; ++frame)
        {
            g.player.health = 100;
            use(false, true);
            burnt = g.player.health < 100;
        }
        check(burnt && weapon.loading == 0 && frame >= 9900 / 33, "held ten seconds, it goes off in the hand");
        g.player.health = 100;

        // The egon: fourteen and a uranium every tenth of a second.
        fresh(1000);
        g.player.inv.current = kWeaponEgon;
        aim();
        ars.uranium = 100;
        for (frame = 0; frame < 30; ++frame) // (a second)
            use(true);
        const int used = 100 - ars.uranium, hurt = 1000 - actor.health;
        std::printf("test_monsters: a second of the egon: %d uranium, %d to the zombie\n", used, hurt);
        check(used >= 9 && used <= 11 && hurt >= 9 * 14 && hurt <= 11 * 14, "the egon: 140 a second, ten uranium");
        use(false);
        check(weapon.loading == 0, "... and let go, it stops");

        // The hornetgun: a homing hornet stings; it grows them back.
        fresh(200);
        g.player.inv.current = kWeaponHornetgun;
        aim();
        ars.hornets = 8;
        did = use(true);
        check((did & kWeaponFired) && ars.hornets == 7, "a hornet");
        for (frame = 0; frame < 60 && actor.health == 200; ++frame)
        {
            hold();
            tick(g, 2);
        }
        check(actor.health == 193, "... which stings for seven");
        ars.hornets = 0;
        for (int t = 0; t < 4100; t += 33)
            use(false);
        check(ars.hornets == 8, "and it grows them back, eight in four seconds");

        // The weaponbox: all of it, each up to what is carried.
        ars = Arsenal();
        g.player.inv = Inventory();
        got = 0;
        check(hldata::detail::weaponboxTake(g.player, "uranium=60 tripmines=7 satchels=5 9mm=50 rockets=8", got) &&
                  ars.uranium == 60 && ars.tripmines == 5 && ars.satchels == 5 && g.player.inv.ammo9mm == 50 &&
                  ars.rockets == 5 && hasWeapon(g.player, kWeaponTripmine) && hasWeapon(g.player, kWeaponSatchel),
              "a weaponbox: everything in it, up to what may be carried; the mines and charges as weapons");
        check(!hldata::detail::weaponboxTake(g.player, "", got), "... and an empty one is nothing");
        g.player.arsenal = nullptr;
    }

    // ------------------------------------------- the weapons that came later

    /// The crossbow, the snark, the tripmine and the satchel, on a zombie in
    /// c1a1a, by the game's numbers.
    void testLaterWeapons(const char *dir)
    {
        auto game = load(dir, "c1a1a");
        if (!game)
            return;
        Game &g = *game;
        int zombie = -1;
        for (int nth = 0; nth < 4 && zombie < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Zombie, nth);
            if (candidate >= 0 && standNear(g, candidate, 200))
                zombie = candidate;
        }
        check(zombie >= 0, "a zombie for the later weapons");
        if (zombie < 0)
            return;
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (i != zombie && isMonster(g.actors.states[i].kind))
                g.actors.states[i].active = false;
        }
        ActorState &actor = g.actors.states[zombie];
        const fx home[3] = {actor.pos[0], actor.pos[1], actor.pos[2]};
        Arsenal ars;
        g.player.arsenal = &ars;
        std::uint32_t got = 0;
        check(!hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Bolts), got) || ars.bolts == 5,
              "bolts with no crossbow are only bolts");
        ars = Arsenal();
        check(hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Crossbow), got) &&
                  hasWeapon(g.player, kWeaponCrossbow) && g.player.inv.current == kWeaponCrossbow &&
                  ars.boltsClip == 5 && ars.bolts == 0,
              "the crossbow comes loaded with five");
        check(hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Crossbow), got) && ars.bolts == 5,
              "... another is five more bolts");
        tick(g, 2);
        WeaponState weapon;
        auto aimAt = [&](const fx *at, fx *eye, fx *forward, fx *right)
        {
            eye[0] = g.player.origin[0];
            eye[1] = g.player.origin[1];
            eye[2] = g.player.origin[2] + g.player.viewHeight;
            fx to[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
            const fx length = fxLength(to);
            for (int k = 0; k < 3; ++k)
                forward[k] = static_cast<fx>((static_cast<std::int64_t>(to[k]) << kFxShift) / length);
            const fx level[3] = {forward[0], forward[1], 0};
            const fx flat = fxLength(level);
            right[0] = static_cast<fx>((static_cast<std::int64_t>(forward[1]) << kFxShift) / flat);
            right[1] = static_cast<fx>((static_cast<std::int64_t>(-forward[0]) << kFxShift) / flat);
            right[2] = 0;
        };
        const fx up[3] = {0, 0, kFxOne};
        auto hold = [&]()
        {
            for (int k = 0; k < 3; ++k)
                actor.pos[k] = home[k];
            g.player.health = 100;
        };
        fx eye[3], forward[3], right[3];

        // The crossbow: a bolt that flies (it is not a hitscan) and hurts
        // fifty, once, in flesh.
        actor.health = 200;
        {
            const fx chest[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36)};
            aimAt(chest, eye, forward, right);
        }
        std::uint32_t did = weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, true, false, 33);
        int bolts = 0;
        for (const Projectile &p : g.actors.shots)
            bolts += p.active && p.kind == kShotBolt;
        check((did & kWeaponFired) && ars.boltsClip == 4 && bolts == 1 && actor.health == 200,
              "the crossbow looses a bolt, which has not yet arrived");
        int frames = 0;
        for (; frames < 30 && actor.health == 200; ++frames)
        {
            hold();
            tick(g, 2);
        }
        bolts = 0;
        for (const Projectile &p : g.actors.shots)
            bolts += p.active && p.kind == kShotBolt;
        std::printf("test_monsters: a bolt reaches the zombie 200 units off in %d frames\n", frames);
        check(actor.health == 150 && bolts == 0 && frames >= 2 && frames <= 6 && g.times(CommonSound::XbowHitBody) == 1,
              "... a few frames on: fifty, and it is gone into the zombie");
        // Its sight, its refire, its reload.
        weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, false, false, 33, true);
        check(ars.zoomed == 1, "the other trigger looks down its sight");
        weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, false, false, 33, true);
        check(ars.zoomed == 1, "... held, it stays there");
        int fired = 0;
        for (int t = 0; t < 3000; t += 33)
            fired += weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, true, false, 33) & kWeaponFired
                         ? 1
                         : 0;
        check(fired == 4 && ars.boltsClip == 0, "a bolt each three quarters of a second, five in the crossbow");
        for (Projectile &p : g.actors.shots)
            p.active = false;
        std::uint32_t reloaded = 0;
        for (int t = 0; t < 1000; t += 33)
            reloaded |= weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, true, false, 33);
        check((reloaded & kWeaponReload) && ars.zoomed == 0, "empty, the trigger reloads it, which takes the eye off the sight");
        for (int t = 0; t < 5000; t += 33)
            weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, false, false, 33);
        check(ars.boltsClip == 5 && ars.bolts == 0, "... four and a half seconds, five bolts from what is carried");
        // Into the floor: it stays there, and is heard.
        {
            const fx floor[3] = {g.player.origin[0] + fxFromInt(40), g.player.origin[1],
                                 g.player.origin[2] - fxFromInt(100)};
            aimAt(floor, eye, forward, right);
        }
        weapon = WeaponState();
        weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, true, false, 33);
        for (int frame = 0; frame < 10; ++frame)
        {
            hold();
            tick(g, 2);
        }
        bool stuck = false, solid = false;
        for (const Projectile &p : g.actors.shots)
        {
            stuck = stuck || (p.active && p.kind == kShotBolt && p.sprite == 1);
            solid = solid || (p.active && pointContents(g.move, p.pos) == kContentsSolid);
        }
        check(stuck && !solid && g.times(CommonSound::XbowHit) == 1, "a bolt in the floor stays, out of the floor");
        for (Projectile &p : g.actors.shots)
            p.active = false;

        // The snark: let go, it finds the zombie and bites, ten a time.
        actor.health = 200;
        ars.snarks = 2;
        giveWeapon(g.player, kWeaponSnark);
        g.player.inv.current = kWeaponSnark;
        weapon = WeaponState();
        {
            const fx chest[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36)};
            aimAt(chest, eye, forward, right);
        }
        did = weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, true, false, 33);
        check((did & kWeaponFired) && ars.snarks == 1, "a snark is let go");
        int bitten = -1;
        for (int frame = 0; frame < 30 * 6 && bitten < 0; ++frame)
        {
            hold();
            tick(g, 2);
            if (actor.health < 200)
                bitten = frame;
        }
        std::printf("test_monsters: the snark bites %.1f s after it is let go\n", bitten / 30.0);
        check(bitten >= 0 && (200 - actor.health) % kSnarkBite == 0, "... and bites the zombie, ten at a time");
        // Fifteen seconds and it bursts.
        bool burst = false;
        for (int frame = 0; frame < 30 * 16 && !burst; ++frame)
        {
            hold();
            tick(g, 2);
            burst = g.times(CommonSound::SnarkBlast) > 0;
        }
        bool left = false;
        for (const Projectile &p : g.actors.shots)
            left = left || (p.active && p.kind == kShotSnark);
        check(burst && !left, "... until it bursts, fifteen seconds after");
        // The last one thrown, the hand is empty of snarks.
        weapon = WeaponState();
        did = weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, true, false, 33);
        check((did & kWeaponFired) && ars.snarks == 0 && !hasWeapon(g.player, kWeaponSnark) &&
                  g.player.inv.current != kWeaponSnark,
              "the last snark gone, it is not held");
        for (Projectile &p : g.actors.shots)
            p.active = false;
        actor.active = true;
        actor.health = 200;
        actor.ai = 0;
        aiForget(g.actors, zombie); // (a new one: it thinks afresh)

        // The tripmine: on the floor at the player's feet, its beam straight
        // up; two and a half seconds and it is set; the zombie across it,
        // and it goes.
        ars.tripmines = 1;
        giveWeapon(g.player, kWeaponTripmine);
        g.player.inv.current = kWeaponTripmine;
        weapon = WeaponState();
        {
            const fx floor[3] = {g.player.origin[0] + fxFromInt(4), g.player.origin[1],
                                 g.player.origin[2] - fxFromInt(100)};
            aimAt(floor, eye, forward, right);
        }
        did = weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, true, false, 33);
        g.drain();
        const Projectile *mine = nullptr;
        for (const Projectile &p : g.actors.shots)
            mine = p.active && p.kind == kShotMine ? &p : mine;
        check((did & kWeaponFired) && mine != nullptr && ars.tripmines == 0 && !hasWeapon(g.player, kWeaponTripmine) &&
                  g.times(CommonSound::MineDeploy) == 1,
              "a tripmine is put down; the last, so none is held");
        if (mine == nullptr)
            return;
        // (The player out of its way.)
        const fx mineAt[3] = {mine->pos[0], mine->pos[1], mine->pos[2]};
        g.player.origin[0] += fxFromInt(48);
        for (int frame = 0; frame < 30 * 3; ++frame)
        {
            hold();
            tick(g, 2);
        }
        check(mine->active && mine->sprite == 1 && g.times(CommonSound::MineActivate) == 1, "... it is set");
        for (int frame = 0; frame < 15; ++frame)
        {
            hold();
            tick(g, 2);
        }
        check(mine->active && mine->sprite == 1, "... and stays set with nothing across it");
        int blown = -1;
        for (int frame = 0; frame < 30 && blown < 0; ++frame)
        {
            actor.pos[0] = mineAt[0];
            actor.pos[1] = mineAt[1];
            actor.pos[2] = mineAt[2] + fxFromInt(4);
            g.player.health = 100;
            tick(g, 2);
            if (!mine->active)
                blown = frame;
        }
        check(blown >= 0 && actor.health < 200 && g.times(CommonSound::Explode) >= 1,
              "the zombie across its beam, it goes off and hurts it");
        actor.active = true;
        actor.health = 400;
        actor.ai = 0;
        aiForget(g.actors, zombie); // (a new one: it thinks afresh)

        // The satchel: thrown, then set off by the radio.
        ars.satchels = 2;
        giveWeapon(g.player, kWeaponSatchel);
        g.player.inv.current = kWeaponSatchel;
        weapon = WeaponState();
        hold();
        {
            const fx feet[3] = {actor.pos[0], actor.pos[1], actor.pos[2]};
            aimAt(feet, eye, forward, right);
        }
        const int blasts = g.times(CommonSound::Explode);
        did = weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, true, false, 33);
        check((did & kWeaponFired) && ars.satchels == 1, "a satchel is thrown");
        for (int frame = 0; frame < 30 * 2; ++frame)
        {
            hold();
            tick(g, 2);
        }
        bool lying = false;
        for (const Projectile &p : g.actors.shots)
            lying = lying || (p.active && p.kind == kShotSatchel && pointContents(g.move, p.pos) != kContentsSolid);
        check(lying && g.times(CommonSound::Explode) == blasts, "... and lies there, waiting");
        for (int t = 0; t < 1000; t += 33)
            weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, false, false, 33);
        const int healthBefore = actor.health;
        did = weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, true, false, 33);
        hold();
        tick(g, 2);
        lying = false;
        for (const Projectile &p : g.actors.shots)
            lying = lying || (p.active && p.kind == kShotSatchel);
        check((did & kWeaponFired) && !lying && ars.satchels == 1 && g.times(CommonSound::Explode) == blasts + 1 &&
                  actor.health < healthBefore,
              "the trigger again sets it off, which the zombie feels");
        g.player.arsenal = nullptr;
    }

    void testGrenade(const char *dir)
    {
        auto game = load(dir, "c1a1a");
        if (!game)
            return;
        Game &g = *game;
        int zombie = -1;
        for (int nth = 0; nth < 4 && zombie < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Zombie, nth);
            if (candidate >= 0 && standNear(g, candidate, 200))
                zombie = candidate;
        }
        check(zombie >= 0, "a zombie to throw a grenade at");
        if (zombie < 0)
            return;
        ActorState &actor = g.actors.states[zombie];
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (i != zombie && isMonster(g.actors.states[i].kind))
                g.actors.states[i].active = false;
        }
        std::uint32_t got = 0;
        check(hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Grenades), got) &&
                  g.player.inv.grenades == 5 && (g.player.inv.have & (1u << kWeaponGrenade)),
              "grenades are picked up five at a time");
        g.player.inv.current = kWeaponGrenade;
        tick(g, 2); // (its first tick gives the zombie its health)
        // Thrown at its feet from where they stand; then they stand there.
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
        fx to[3] = {actor.pos[0] - eye[0], actor.pos[1] - eye[1], 0};
        const fx length = fxLength(to);
        for (fx &c : to)
            c = static_cast<fx>((static_cast<std::int64_t>(c) << kFxShift) / length);
        const fx right[3] = {to[1], -to[0], 0}, up[3] = {0, 0, kFxOne};
        WeaponState weapon;
        // The pin first; it is thrown when the trigger is let go, and not
        // before the pin is out.
        std::uint32_t did = weaponsTick(g.logic, g.player, weapon, eye, to, right, up, true, false, 33);
        check(!(did & kWeaponFired) && g.player.inv.grenades == 5 && weapon.anim != nullptr &&
                  std::string(weapon.anim) == "pinpull",
              "the trigger pulls the pin");
        weapon.anim = nullptr;
        did = weaponsTick(g.logic, g.player, weapon, eye, to, right, up, false, false, 33);
        check(!(did & kWeaponFired), "... and letting go at once does not throw it yet");
        for (int t = 66; t < 700 && !(did & kWeaponFired); t += 33)
            did = weaponsTick(g.logic, g.player, weapon, eye, to, right, up, false, false, 33);
        check((did & kWeaponFired) && g.player.inv.grenades == 4 && weapon.anim != nullptr &&
                  std::string(weapon.anim) == "throw2",
              "half a second after the pin, one is thrown: aimed level, the middle throw");
        weapon.anim = nullptr;
        // The zombie is held where it is -- and, once the grenade has come
        // to rest, stood sixty units from it: this is about the grenade, and
        // where one lands after its bounces is not for a test to say.
        fx there[3] = {actor.pos[0], actor.pos[1], actor.pos[2]};
        bool flew = false, burst = false, solid = false;
        int burstFrame = -1;
        for (int frame = 0; frame < 30 * 5; ++frame)
        {
            g.player.health = 100;
            if (frame == 65)
            {
                for (const Projectile &p : g.actors.shots)
                {
                    if (!p.active)
                        continue;
                    there[0] = p.pos[0] + fxFromInt(60);
                    there[1] = p.pos[1];
                    there[2] = p.pos[2] - kCrouchedFeet;
                }
            }
            for (int a = 0; a < 3; ++a)
                actor.pos[a] = there[a];
            const int healthBefore = actor.health;
            tick(g, 2);
            for (const Projectile &p : g.actors.shots)
            {
                if (!p.active)
                    continue;
                flew = true;
                solid = solid || pointContents(g.move, p.pos) == kContentsSolid;
            }
            for (const Effect &e : g.actors.effects)
            {
                if (e.kind == kEffectBurst && burstFrame < 0)
                    burstFrame = frame;
            }
            burst = burst || (burstFrame >= 0 && actor.health < healthBefore) || actor.ai >= kAiDying;
        }
        fx burstAt[3] = {0, 0, 0};
        for (const Effect &e : g.actors.effects)
        {
            if (e.wholeMs == 350)
                for (int k = 0; k < 3; ++k)
                    burstAt[k] = e.a[k];
        }
        const fx off[3] = {burstAt[0] - there[0], burstAt[1] - there[1], burstAt[2] - there[2]};
        std::printf("test_monsters: the grenade bursts %.1f s after it is thrown, %d units from the zombie "
                    "(health %d, heard %d)\n",
                    burstFrame / 30.0, fxToInt(fxLength(off)), actor.health, g.times(CommonSound::Explode));
        check(flew && !solid, "it flies and bounces, and is never inside the world");
        // (Three seconds from the PIN, half a second of which went before
        // it left the hand.)
        check(burstFrame >= 70 && burstFrame <= 80, "... and bursts three seconds after the pin was pulled");
        check(burst && g.times(CommonSound::Explode) == 1, "... which is heard, and hurts what is near");
    }

    void testSuit(const char *dir)
    {
        auto game = load(dir, "c1a0d");
        check(game != nullptr, "c1a0d cooks");
        if (!game)
            return;
        Game &g = *game;
        int suit = -1;
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (g.actors.states[i].active &&
                g.actors.states[i].item == static_cast<std::uint8_t>(ItemKind::Suit))
                suit = i;
        }
        check(suit >= 0, "the suit stands in c1a0d");
        if (suit < 0)
            return;
        // What it is the key to: a multisource, off until it is taken.
        int master = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::MultiSource) && r.name != 0 &&
                g.view.nameHashes[r.name] == nameHash("hevmaster1"))
                master = static_cast<int>(i);
        }
        check(master >= 0 && g.view.logic[master].auxCount >= 1, "... and something waits on it");
        if (master < 0)
            return;
        const std::uint32_t all = (1u << g.view.logic[master].auxCount) - 1u;
        check(g.logic.states[master].bits != all, "... which is off");
        // A battery does nothing for someone with no suit.
        std::uint32_t got = 0;
        check(!hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Battery), got),
              "a battery is no use without the suit");
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = g.actors.states[suit].pos[a];
        g.player.origin[2] += kStandingFeet;
        for (int n = 0; n < 5; ++n)
        {
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
        }
        check(!g.actors.states[suit].active && (g.player.inv.have & kHaveSuit), "walked into, it is worn");
        check(g.logic.states[master].bits == all, "... and what waited on it is on");
        check(hldata::detail::itemTake(g.player, static_cast<std::uint8_t>(ItemKind::Battery), got) &&
                  g.player.inv.armor == 15,
              "... and a battery charges it");
    }
} // namespace

/// A weapon asked for by name (the touch screen's slots): taken if it is
/// carried, and only then.
// The guns and the grenade by the game's own rules (its weapons' source):
// what each trigger does and how often, which of the hand's clips it asks
// for, what is heard and at what pitch. No map: nothing is hit.
static void testGunRules()
{
    LogicWorld logic{};
    MoveWorld world; // (a world of nothing at all)
    logic.world = &world;
    SoundQueue heard;
    logic.heard = &heard;
    PlayerState player{};
    WeaponState weapon;
    Inventory &inv = player.inv;
    inv.have = static_cast<std::uint8_t>(1u << kWeaponGlock | 1u << kWeaponShotgun | 1u << kWeaponMp5 |
                                         1u << kWeaponPython | 1u << kWeaponGrenade);
    const fx eye[3] = {0, 0, 0}, forward[3] = {kFxOne, 0, 0}, right[3] = {0, -kFxOne, 0}, up[3] = {0, 0, kFxOne};
    int sounds[static_cast<int>(CommonSound::Count)] = {};
    int lowPitch = 255, highPitch = 0;
    std::string clips; // every clip the hand was told to play, in order
    auto step = [&](bool attack, bool second = false, bool reload = false, const fx *aim = nullptr)
    {
        const std::uint32_t did =
            weaponsTick(logic, player, weapon, eye, aim ? aim : forward, right, up, attack, false, 33, second, 0, reload);
        for (int i = 0; i < heard.count; ++i)
        {
            if (heard.events[i].sound < static_cast<int>(CommonSound::Count))
                ++sounds[heard.events[i].sound];
            lowPitch = std::min<int>(lowPitch, heard.events[i].pitch);
            highPitch = std::max<int>(highPitch, heard.events[i].pitch);
        }
        heard.count = 0;
        if (weapon.anim != nullptr)
            clips += std::string(weapon.anim) + " ";
        weapon.anim = nullptr;
        return did;
    };
    auto hold = [&](int ms, bool attack, bool second = false)
    {
        std::uint32_t did = 0;
        for (int t = 0; t < ms; t += 33)
            did |= step(attack, second);
        return did;
    };
    auto fresh = [&](std::uint8_t which)
    {
        weapon = WeaponState();
        weapon.idleMs = 100000; // (the idles have their own check)
        inv.current = which;
        for (int &n : sounds)
            n = 0;
        clips.clear();
        lowPitch = 255;
        highPitch = 0;
        heard.count = 0;
    };
    auto heardOf = [&](CommonSound sound) { return sounds[static_cast<int>(sound)]; };
    auto count = [&](const char *clip)
    {
        int n = 0;
        const std::string word = std::string(clip) + " ";
        for (std::size_t at = clips.find(word); at != std::string::npos; at = clips.find(word, at + 1))
            n += at == 0 || clips[at - 1] == ' ' ? 1 : 0;
        return n;
    };

    // The pistol: a round every 0.3 s on the trigger, every 0.2 s on the
    // other; the last round in it has a clip of its own.
    fresh(kWeaponGlock);
    inv.clipGlock = 17;
    inv.ammo9mm = 0;
    hold(3000, true);
    check(inv.clipGlock == 7 && heardOf(CommonSound::Pistol) == 10 && count("shoot") == 10,
          "the pistol: ten rounds in three seconds");
    check(lowPitch >= 98 && highPitch <= 101, "... each at the game's pitch, 98 to 101");
    hold(100, false);
    hold(1000, false, true);
    check(inv.clipGlock == 2, "... and five a second on the other trigger");
    hold(100, false);
    hold(600, true);
    check(inv.clipGlock == 0 && count("shoot_empty") == 1, "the last round has a clip of its own");
    // Empty, and nothing to put in: a click, once for the pull.
    hold(100, false);
    const int before = heardOf(CommonSound::Click);
    hold(1500, true);
    check(heardOf(CommonSound::Click) == before + 1, "the trigger on an empty gun clicks once, however long it is held");
    hold(100, false);
    hold(100, true);
    check(heardOf(CommonSound::Click) == before + 2, "... and again when it is pulled again");
    // Its two reloads: one with a round still in it, one without.
    fresh(kWeaponGlock);
    inv.clipGlock = 0;
    inv.ammo9mm = 40;
    const std::uint32_t began = hold(33, true);
    check((began & kWeaponReload) && count("reload") == 1 && heardOf(CommonSound::Reload) == 0,
          "an empty pistol reloads at the trigger: the empty gun's clip, whose sounds are its own");
    hold(1600, false);
    check(inv.clipGlock == 17 && inv.ammo9mm == 23, "... a second and a half");
    inv.clipGlock = 9;
    clips.clear();
    step(false, false, true);
    check(count("reload_noshot") == 1, "with a round still in it, the other reload");
    // The wild trigger is wild: a tenth either way, where the careful one
    // is a hundredth. (Counted in an empty world by what the cone would
    // allow: the two cones themselves.)
    check(kConePistol == 41 && kConePistolFast == 410 && kCone1 == 36 && kCone3 == 107 && kCone10 == 357,
          "the game's cones");

    // Under water only the pistol fires.
    player.waterLevel = 3;
    fresh(kWeaponGlock);
    inv.clipGlock = 5;
    hold(100, true);
    check(inv.clipGlock == 4, "the pistol fires under water");
    for (std::uint8_t which : {kWeaponPython, kWeaponMp5, kWeaponShotgun})
    {
        fresh(which);
        inv.clipPython = 6;
        inv.clipMp5 = 50;
        inv.clipShotgun = 8;
        hold(500, true);
        check(inv.clipPython == 6 && inv.clipMp5 == 50 && inv.clipShotgun == 8 && heardOf(CommonSound::Click) == 1,
              "the others click under water, and keep their rounds");
    }
    player.waterLevel = 0;

    // The shotgun: a shot, and half a second on its pump -- which is when
    // the spent case comes out.
    fresh(kWeaponShotgun);
    inv.clipShotgun = 8;
    inv.ammoBuck = 20;
    std::uint32_t did = step(true);
    check((did & kWeaponFired) && !(did & kWeaponShell) && inv.clipShotgun == 7 && count("shoot") == 1 &&
              heardOf(CommonSound::Shotgun) == 1 && heardOf(CommonSound::ShotgunPump) == 0,
          "the shotgun fires: no case yet, no pump yet");
    int pumped = -1;
    for (int t = 33; t < 1000 && pumped < 0; t += 33)
    {
        if (step(false) & kWeaponShell)
            pumped = t;
    }
    check(pumped >= 462 && pumped <= 528 && weapon.shells == 1 && heardOf(CommonSound::ShotgunPump) == 1,
          "half a second on it is pumped, and one case comes out");
    hold(400, false);
    // Both barrels on the other trigger.
    did = step(false, true);
    check((did & kWeaponSecondary) && inv.clipShotgun == 5 && count("shoot_big") == 1 &&
              heardOf(CommonSound::ShotgunDouble) == 1 && weapon.cooldownMs > 1400,
          "the other trigger fires both barrels: two shells, a second and a half");
    pumped = -1;
    for (int t = 33; t < 1500 && pumped < 0; t += 33)
    {
        if (step(false) & kWeaponShell)
            pumped = t;
    }
    check(pumped >= 924 && pumped <= 990 && weapon.shells == 2, "... pumped nearly a second on, two cases out");
    hold(700, false);
    // Loaded a shell at a time: the start, then half a second a shell,
    // then the pump.
    fresh(kWeaponShotgun);
    inv.clipShotgun = 5;
    inv.ammoBuck = 20;
    did = step(false, false, true);
    check((did & kWeaponReload) && count("start_reload") == 1 && inv.clipShotgun == 5, "the shotgun's reload begins");
    hold(600 + 3 * 500 + 200, false);
    check(inv.clipShotgun == 8 && inv.ammoBuck == 17 && count("reload") == 3 && count("pump") == 1 &&
              heardOf(CommonSound::ShotgunReload) + heardOf(CommonSound::ShotgunReload3) == 3 &&
              heardOf(CommonSound::ShotgunPump) == 1 && weapon.loading == 0,
          "three shells in, one at a time, each heard; then pumped");
    check(lowPitch >= 85 && highPitch <= 126, "... at the game's pitches");
    // Broken off by the trigger, with what is in it by then.
    fresh(kWeaponShotgun);
    inv.clipShotgun = 2;
    inv.ammoBuck = 20;
    step(false, false, true);
    hold(600 + 500 + 300, false); // (the start, one shell, part of the next)
    const int loaded = inv.clipShotgun;
    did = hold(66, true);
    check(loaded >= 3 && loaded <= 4 && (did & kWeaponFired) && weapon.loading == 0 && inv.clipShotgun == loaded - 1,
          "the trigger breaks the loading off, and fires what is in it");
    // One shell and the other trigger: more shells first.
    fresh(kWeaponShotgun);
    inv.clipShotgun = 1;
    inv.ammoBuck = 5;
    did = step(false, true);
    check((did & kWeaponReload) && !(did & kWeaponFired), "both barrels with one shell loads instead");

    // The MP5's launcher has two sounds; the gun two; a case for each round.
    fresh(kWeaponMp5);
    inv.clipMp5 = 50;
    inv.ammo9mm = 0;
    int cases = 0;
    for (int t = 0; t < 2000; t += 33)
        cases += step(true) & kWeaponShell ? 1 : 0;
    check(inv.clipMp5 <= 31 && inv.clipMp5 >= 29 && cases == 50 - inv.clipMp5 &&
              heardOf(CommonSound::Mp5Fire) > 2 && heardOf(CommonSound::Mp5Fire1) > 2 &&
              heardOf(CommonSound::Mp5Fire) + heardOf(CommonSound::Mp5Fire1) == cases,
          "the MP5: ten a second, one or other of its two shots, a case each");
    check(lowPitch >= 94 && highPitch <= 109, "... pitched as the game pitches them");

    // The .357: no case; the other button reloads it.
    fresh(kWeaponPython);
    inv.clipPython = 3;
    inv.ammo357 = 12;
    did = step(true);
    check((did & kWeaponFired) && !(did & kWeaponShell) && count("fire1") == 1, "the .357 throws no case");
    hold(800, false);
    did = step(false, true);
    check((did & kWeaponReload) && count("reload") == 1, "its other button reloads it");
    hold(2100, false);
    check(inv.clipPython == 6 && inv.ammo357 == 8, "... two seconds");

    // The grenade: harder the higher it is aimed, by the game's sum; held
    // three seconds it goes off in the hand; and with its pin out it is
    // not put down for another weapon.
    ActorWorld actors;
    logic.actors = &actors;
    auto thrown = [&](const fx *aim, int holdMs) -> int
    {
        for (Projectile &shot : actors.shots)
            shot.active = false;
        fresh(kWeaponGrenade);
        inv.have |= static_cast<std::uint8_t>(1u << kWeaponGrenade);
        inv.grenades = 5;
        for (int t = 0; t < holdMs; t += 33)
            step(true, false, false, aim);
        for (int t = 0; t < 200; t += 33)
            step(false, false, false, aim);
        for (const Projectile &shot : actors.shots)
        {
            if (shot.active)
                return fxToInt(fxLength(shot.velocity));
        }
        return -1;
    };
    const int level = thrown(forward, 600);
    check(level >= 630 && level <= 670 && count("pinpull") == 1 && count("throw2") == 1,
          "aimed level it leaves at 650 units a second: the middle throw");
    fx sine = 0, cosine = 0;
    fxSinCos(static_cast<std::uint16_t>(65536 * 60 / 360), sine, cosine);
    const fx low[3] = {cosine, 0, -sine};
    // (Only nearly straight up is it thrown as hard as it goes: at sixty
    // degrees the game's sum is 996.)
    fxSinCos(static_cast<std::uint16_t>(65536 * 80 / 360), sine, cosine);
    const fx high[3] = {cosine, 0, sine};
    const int far = thrown(high, 600);
    check(far >= 990 && far <= 1000 && count("throw3") == 1, "aimed high, as hard as it goes: the long throw");
    const int near = thrown(low, 600);
    check(near >= 200 && near <= 240 && count("throw1") == 1, "aimed at the feet it is dropped: the short throw");
    // (How long is left of its three seconds when it goes.)
    thrown(forward, 1500);
    int left = 0;
    for (const Projectile &shot : actors.shots)
        left = shot.active ? shot.leftMs : left;
    check(left >= 1400 && left <= 1500, "the three seconds run from the pin, not from the throw");
    const int held = thrown(forward, 3100);
    left = -1;
    for (const Projectile &shot : actors.shots)
        left = shot.active ? shot.leftMs : left;
    check(held == 0 && left <= 33 && inv.grenades == 4, "held three seconds, it goes off where it is");
    fresh(kWeaponGrenade);
    inv.grenades = 3;
    step(true);
    weaponsTick(logic, player, weapon, eye, forward, right, up, true, false, 33, false, kWeaponGlock);
    check(inv.current == kWeaponGrenade && weapon.pinMs > 0, "with its pin out it is not put down");
    logic.actors = nullptr;

    // Left alone, the hand does something of its own: each weapon its own
    // clips, in the game's proportions.
    for (std::uint8_t which : {kWeaponGlock, kWeaponPython, kWeaponShotgun})
    {
        fresh(which);
        inv.clipGlock = 5;
        weapon.idleMs = 1000;
        hold(300000, false);
        if (which == kWeaponGlock)
            check(count("idle1") > 5 && count("idle2") > 5 && count("idle3") > 5 && count("shoot") == 0,
                  "the pistol's three ways of being held");
        else if (which == kWeaponPython)
            check(count("idle1") > count("idle2") && count("idle2") > 3 && count("idle3") > 3 && count("fidget1") > 1,
                  "the .357's: three, and now and then a spin of the cylinder");
        else
            check(count("deepidle") > count("sm_idle") && count("sm_idle") > count("idle4"),
                  "the shotgun's: mostly at rest");
    }
    fresh(kWeaponGlock);
    inv.clipGlock = 0;
    inv.ammo9mm = 0;
    weapon.idleMs = 1000;
    hold(20000, false);
    check(clips.empty(), "an empty pistol is held still: its slide is back");
}

static void testPick()
{
    LogicWorld logic{};
    PlayerState player{};
    WeaponState weapon;
    Inventory &inv = player.inv;
    inv.have = static_cast<std::uint8_t>(1u << kWeaponCrowbar | 1u << kWeaponGlock | 1u << kWeaponPython);
    inv.current = kWeaponCrowbar;
    const fx eye[3] = {0, 0, 0}, forward[3] = {kFxOne, 0, 0}, right[3] = {0, -kFxOne, 0}, up[3] = {0, 0, kFxOne};
    auto tick = [&](int pick) { return weaponsTick(logic, player, weapon, eye, forward, right, up, false, false, 33, false, pick); };
    // The crowbar is put away before another weapon comes out: half a
    // second of it, as in the game.
    std::uint32_t did = tick(kWeaponGlock);
    check(!(did & kWeaponDrawn) && inv.current == kWeaponCrowbar && weapon.anim != nullptr &&
              std::string(weapon.anim) == "holster",
          "the crowbar is put away first");
    weapon.anim = nullptr;
    int waited = 33;
    for (; waited < 2000 && !(did & kWeaponDrawn); waited += 33)
        did = tick(0);
    check((did & kWeaponDrawn) && inv.current == kWeaponGlock && waited >= 500 && waited <= 566,
          "... and half a second on the other is in hand");
    weapon = WeaponState();
    did = tick(kWeaponPython);
    check((did & kWeaponDrawn) && inv.current == kWeaponPython && weapon.cooldownMs == 500,
          "a weapon asked for by name is in hand at once, drawn as the next one would be");
    did = tick(kWeaponPython);
    check(!(did & kWeaponDrawn) && inv.current == kWeaponPython, "asked for again, it is in hand already");
    did = tick(kWeaponShotgun);
    check(!(did & kWeaponDrawn) && inv.current == kWeaponPython, "one not carried is not taken");
    did = tick(kWeaponCount) | tick(-3) | tick(0);
    check(!(did & kWeaponDrawn) && inv.current == kWeaponPython, "nor anything that is no weapon");
    weapon.reloadMs = 900;
    weapon.cooldownMs = 0;
    did = tick(kWeaponGlock);
    check((did & kWeaponDrawn) && inv.current == kWeaponGlock && weapon.reloadMs == 0,
          "taking another weapon gives up a reload begun");

    // A reload asked for: by RELOAD, or by a touch on the weapon in hand.
    auto ask = [&](int pick, bool reload, bool second = false)
    { return weaponsTick(logic, player, weapon, eye, forward, right, up, false, false, 33, second, pick, reload); };
    weapon = WeaponState();
    inv.clipGlock = static_cast<std::int16_t>(gunKind(kWeaponGlock).holds);
    inv.ammo9mm = 50;
    check(!(ask(0, true) & kWeaponReload) && weapon.reloadMs == 0, "a full gun is not reloaded");
    inv.clipGlock = 5;
    check((ask(0, true) & kWeaponReload) && weapon.reloadMs > 0, "RELOAD reloads a gun that is not full");
    weapon = WeaponState();
    check((ask(kWeaponGlock, false) & kWeaponReload) && weapon.reloadMs > 0 && inv.current == kWeaponGlock,
          "... and so does a touch on the weapon in hand");
    for (int t = 0; t < 4000 && weapon.reloadMs > 0; t += 33)
        ask(0, false);
    check(inv.clipGlock == gunKind(kWeaponGlock).holds && inv.ammo9mm == 50 - (gunKind(kWeaponGlock).holds - 5),
          "the rounds come out of what is carried");
    inv.clipGlock = 5;
    inv.ammo9mm = 0;
    check(!(ask(0, true) & kWeaponReload), "with nothing carried there is nothing to reload with");
    inv.current = kWeaponCrowbar;
    weapon = WeaponState();
    check(!(ask(0, true) & kWeaponReload) && !(ask(kWeaponCrowbar, false) & kWeaponReload), "a crowbar is not reloaded");
    // The MP5: the other button launches while there is a grenade to
    // launch, so only this reloads it.
    inv.have |= static_cast<std::uint8_t>(1u << kWeaponMp5);
    inv.current = kWeaponMp5;
    inv.clipMp5 = 10;
    inv.ammo9mm = 100;
    inv.ammoAr = 2;
    weapon = WeaponState();
    const std::uint32_t mp5 = ask(0, true, true);
    check((mp5 & kWeaponReload) && !(mp5 & kWeaponSecondary) && inv.ammoAr == 2,
          "RELOAD reloads an MP5 with grenades to launch, and launches none");

    // The touch screen's five slots are the game's five kinds of weapon: a
    // touch takes the first of them carried, another the next, round again.
    Arsenal ars;
    player.arsenal = &ars;
    inv.have = static_cast<std::uint8_t>(1u << kWeaponCrowbar | 1u << kWeaponGlock | 1u << kWeaponPython |
                                         1u << kWeaponMp5 | 1u << kWeaponGrenade);
    giveWeapon(player, kWeaponCrossbow);
    giveWeapon(player, kWeaponSnark);
    inv.current = kWeaponCrowbar;
    check(weaponOfCategory(player, 1) == kWeaponCrowbar, "the first slot: the crowbar, the only one");
    check(weaponOfCategory(player, 2) == kWeaponGlock, "the second: the pistol first");
    inv.current = kWeaponGlock;
    check(weaponOfCategory(player, 2) == kWeaponPython, "... then the .357");
    inv.current = kWeaponPython;
    check(weaponOfCategory(player, 2) == kWeaponGlock, "... then round again");
    inv.current = kWeaponMp5;
    check(weaponOfCategory(player, 3) == kWeaponCrossbow, "the third: past the shotgun not carried, the crossbow");
    check(weaponOfCategory(player, 4) == kWeaponNone, "the fourth: nothing yet");
    inv.current = kWeaponGrenade;
    check(weaponOfCategory(player, 5) == kWeaponSnark, "the fifth: past the satchel and tripmine not carried, the snark");
    check(weaponCategory(kWeaponSatchel) == 5 && weaponCategory(kWeaponCrossbow) == 3 && weaponCategory(kWeaponPython) == 2,
          "each weapon in its kind");
    // Stepping goes through all of them, the later ones too.
    inv.current = kWeaponGrenade;
    check(weaponAfter(player) != kWeaponGrenade && hasWeapon(player, weaponAfter(player)),
          "the next weapon after the grenade is one carried");
    std::uint8_t seen = 0;
    for (int n = 0; n < 12; ++n)
    {
        inv.current = weaponAfter(player);
        seen |= inv.current == kWeaponCrossbow ? 1 : (inv.current == kWeaponSnark ? 2 : 0);
    }
    check(seen == 3, "... and round them all, the crossbow and the snark among them");
    takeWeapon(player, kWeaponSnark);
    check(!hasWeapon(player, kWeaponSnark) && hasWeapon(player, kWeaponCrossbow), "one taken, the others kept");
    player.arsenal = nullptr;
    check(!hasWeapon(player, kWeaponCrossbow), "with no arsenal, none of the later ones");
}

int main()
{
    testPick();
    testGunRules();
    testSums();
    testPlayerDamage();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
    {
        testNodes(dir);
        for (int vblanks = 1; vblanks <= 4; ++vblanks)
            testHeadcrab(dir, vblanks);
        testRoundTheCorner(dir);
        testHatchLadder(dir);
        testLadderFeet(dir);
        testHologram(dir);
        testZombie(dir);
        testMaker(dir);
        testItems(dir);
        testSuit(dir);
        testKind(dir, "c1a1b", ActorKind::Houndeye, "houndeye", CommonSound::HoundBlast, 160, false, false);
        testKind(dir, "c1a2a", ActorKind::Slave, "alien slave", CommonSound::SlaveZap, 300, false, true);
        testKind(dir, "c1a1c", ActorKind::Bullsquid, "bullsquid", CommonSound::SquidSpit, 320, true, false);
        testGrenade(dir);
        testLaterWeapons(dir);
        testLastWeapons(dir);
        testHeadShot(dir);
        testGuns(dir);
        testGibs(dir);
        testWideHull(dir);
        testMiniturret(dir);
        testSentries(dir);
        testBigTurret(dir);
    }
    else
    {
        std::printf("test_monsters: HLDS_VALVE_DIR not set, the maps were not played\n");
    }
    if (g_failures == 0)
        std::printf("test_monsters: ok\n");
    return g_failures == 0 ? 0 : 1;
}
