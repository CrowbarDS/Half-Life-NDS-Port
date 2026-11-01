#pragma once

// What fights, what it is fought with, and what is picked up on the way.
//
// MONSTERS are actors (HlActors.hpp) with a little mind. Each is a state --
// idle, chasing, attacking, in the air, flinching, dying -- and moves by the
// game's own rule for things that walk: a step forward is allowed if, lifted
// a stair's height, nothing is in the way, and there is floor under where it
// ends. They find the player in a straight line when one can be walked, and
// otherwise from path node to path node, by a table the converter worked out
// (PathNext): there is no searching on the console.
//
//   headcrab  runs at the player and leaps from 256 units; it hurts what it
//             lands on.
//   zombie    walks up and swings; each swing hurts if the player is still
//             in reach when it lands.
//   barnacle  hangs, with a tongue down to the floor; what walks into the
//             tongue is pulled up and bitten.
//
// A scripted sequence that has taken a monster has it: it does not think
// until the script lets it go.
//
// WEAPONS are the crowbar, the pistol and the shotgun, with the game's
// numbers at its middle difficulty. A shot is a line: through the world's
// own tree (not the clip hulls, which are the world grown by the player's
// size), against the doors and boxes, and against the box each monster
// stands in.
//
// ITEMS are actors that do nothing but lie there until walked over.
//
// std-only and inline: the host tests run it.

#include <cstdlib>
#include <cstring>

#include "HlLogic.hpp"
#include "HlMaterial.hpp"
#include "HlAi.hpp"

namespace hldata
{
    inline void aiTick(LogicWorld &w, PlayerState &player, int index, int ms);
    inline void aiWorldTick(LogicWorld &w, PlayerState &player, int ms);
} // namespace hldata

namespace hldata
{
    // What happened to the player's things (ActorWorld::got).
    inline constexpr std::uint32_t kGotItem = 1;   ///< something was picked up
    inline constexpr int kAirtankRechargeMs = 30000; ///< before an air tank gives again
    inline constexpr std::uint32_t kGotWeapon = 2; ///< ... and it was a weapon not had before
    inline constexpr std::uint32_t kGotHurt = 4;   ///< a monster hurt the player

    // What a weapon did this tick (weaponsTick's result).
    inline constexpr std::uint32_t kWeaponFired = 1;  ///< a shot, or a swing that missed
    inline constexpr std::uint32_t kWeaponHit = 2;    ///< ... that landed on something
    inline constexpr std::uint32_t kWeaponReload = 4; ///< began to reload
    inline constexpr std::uint32_t kWeaponDrawn = 8;  ///< another weapon is in hand

    inline constexpr std::uint8_t kHaveSuit = 1; ///< in Inventory::have

    inline bool isMonster(std::uint8_t kind)
    {
        return kind == static_cast<std::uint8_t>(ActorKind::Headcrab) ||
               kind == static_cast<std::uint8_t>(ActorKind::Zombie) ||
               kind == static_cast<std::uint8_t>(ActorKind::Barnacle) ||
               kind == static_cast<std::uint8_t>(ActorKind::Slave) ||
               kind == static_cast<std::uint8_t>(ActorKind::Bullsquid) ||
               kind == static_cast<std::uint8_t>(ActorKind::Houndeye) ||
               kind == static_cast<std::uint8_t>(ActorKind::Cockroach) ||
               kind == static_cast<std::uint8_t>(ActorKind::Leech) ||
               kind == static_cast<std::uint8_t>(ActorKind::AlienGrunt) ||
               kind == static_cast<std::uint8_t>(ActorKind::Controller) ||
               kind == static_cast<std::uint8_t>(ActorKind::Snark) ||
               kind == static_cast<std::uint8_t>(ActorKind::Gargantua) ||
               kind == static_cast<std::uint8_t>(ActorKind::BabyCrab) ||
               kind == static_cast<std::uint8_t>(ActorKind::Ichthyosaur) ||
               kind == static_cast<std::uint8_t>(ActorKind::Tentacle) ||
               kind == static_cast<std::uint8_t>(ActorKind::BigMomma) ||
               kind == static_cast<std::uint8_t>(ActorKind::Nihilanth) ||
               kind == static_cast<std::uint8_t>(ActorKind::Boid);
    }

    inline bool isGrunt(std::uint8_t kind)
    {
        return kind == static_cast<std::uint8_t>(ActorKind::Grunt);
    }
    /// The people of the place, who can be hurt (the man in the suit
    /// cannot).
    inline bool isPerson(std::uint8_t kind)
    {
        return kind == static_cast<std::uint8_t>(ActorKind::Scientist) ||
               kind == static_cast<std::uint8_t>(ActorKind::Guard) ||
               kind == static_cast<std::uint8_t>(ActorKind::Sitter);
    }
    /// The military's machines (HlMilitary.hpp): they fight, and do not
    /// bleed.
    inline bool isMachine(std::uint8_t kind)
    {
        return kind == static_cast<std::uint8_t>(ActorKind::Osprey) ||
               kind == static_cast<std::uint8_t>(ActorKind::Apache);
    }
    /// The military's assassins: they fight, and bleed red.
    inline bool isAssassin(std::uint8_t kind)
    {
        return kind == static_cast<std::uint8_t>(ActorKind::Assassin);
    }
    /// What comes out of something that is hit, as the game has it: red
    /// from people (and from the barnacle, which is made of what it has
    /// eaten), yellow from the aliens, nothing from a machine.
    inline constexpr int kBloodNone = 0;
    inline constexpr int kBloodRed = 1;
    inline constexpr int kBloodYellow = 2;
    inline int bloodOf(std::uint8_t kind)
    {
        if (isGrunt(kind) || isPerson(kind) || isAssassin(kind) || kind == static_cast<std::uint8_t>(ActorKind::Barnacle) ||
            kind == static_cast<std::uint8_t>(ActorKind::Player))
            return kBloodRed;
        return isMonster(kind) ? kBloodYellow : kBloodNone;
    }
    /// How much a person takes before they are dead (the game's, at its
    /// middle difficulty).
    inline int personHealth(std::uint8_t kind)
    {
        return kind == static_cast<std::uint8_t>(ActorKind::Guard) ? 35 : 20;
    }
    /// How much health a monster of `kind` is made with, at the game's
    /// difficulty (its Spawn's gSkillData.*Health); `fallback` for a kind
    /// whose health is the same at every level.
    inline int skillHealth(const SkillData &s, std::uint8_t kind, int fallback)
    {
        switch (static_cast<ActorKind>(kind))
        {
        case ActorKind::Headcrab:    return s.headcrabHealth;
        case ActorKind::BabyCrab:    return s.headcrabHealth / 4; // (CBabyCrab::Spawn: a quarter of one)
        case ActorKind::Zombie:      return s.zombieHealth;
        case ActorKind::Slave:       return s.islaveHealth;
        case ActorKind::Bullsquid:   return s.bullsquidHealth;
        case ActorKind::Houndeye:    return s.houndeyeHealth;
        case ActorKind::Grunt:       return s.hgruntHealth;
        case ActorKind::AlienGrunt:  return s.agruntHealth;
        case ActorKind::Controller:  return s.controllerHealth;
        case ActorKind::Gargantua:   return s.gargantuaHealth;
        case ActorKind::Assassin:    return s.hassassinHealth;
        case ActorKind::Ichthyosaur: return s.ichthyosaurHealth;
        case ActorKind::Apache:      return s.apacheHealth;
        case ActorKind::Nihilanth:   return s.nihilanthHealth;
        case ActorKind::Leech:       return s.leechHealth;
        case ActorKind::Snark:       return s.snarkHealth;
        case ActorKind::Scientist:   return s.scientistHealth;
        case ActorKind::Guard:       return s.barneyHealth;
        case ActorKind::BigMomma:    return 150 * s.bigmommaHealthFactorTenths / 10;
        case ActorKind::Miniturret:  return s.miniturretHealth;
        case ActorKind::Sentry:      return s.sentryHealth;
        case ActorKind::Turret:      return s.turretHealth;
        default:                     return fallback;
        }
    }
    inline bool isMine(std::uint8_t kind)
    {
        return kind == static_cast<std::uint8_t>(ActorKind::Tripmine);
    }

    /// What each kind of monster is, in numbers. The game's, at "normal".
    struct MonsterKind
    {
        int health;
        bool small;  ///< moves as the crouched hull (32 x 32 x 36), not the standing one
        fx half;     ///< half its width, for what hits it
        fx height;
        fx speed;    ///< units a second, if its clip does not say
        const char *idle;
        const char *move;
        const char *flinch;
        const char *die;
    };

    inline const MonsterKind &monsterKind(std::uint8_t kind)
    {
        static const MonsterKind kHeadcrab = {10, true, fxFromInt(12), fxFromInt(24), fxFromInt(140),
                                              "idle1", "run", "flinch", "dieback"};
        static const MonsterKind kZombie = {50, false, fxFromInt(16), fxFromInt(72), fxFromInt(45),
                                            "idle1", "walk", "flinchsmall", "diesimple"};
        static const MonsterKind kBarnacle = {25, true, fxFromInt(16), fxFromInt(32), 0,
                                              "idle1", "idle1", "flinch_sm", "death"};
        static const MonsterKind kSlave = {30, false, fxFromInt(16), fxFromInt(64), fxFromInt(90),
                                           "idle1", "walk1", "flinch2", "diesimple"};
        // (It goes only where something 64 units across has room:
        // monsterHull.)
        static const MonsterKind kBullsquid = {40, false, fxFromInt(20), fxFromInt(48), fxFromInt(110),
                                               "idle", "run", "flinchs", "die1"};
        static const MonsterKind kHoundeye = {20, true, fxFromInt(16), fxFromInt(36), fxFromInt(160),
                                              "idle1", "run", "flinch_small", "die"};
        // (A soldier: HlGrunt.hpp has the rest of him.)
        static const MonsterKind kGrunt = {50, false, fxFromInt(16), fxFromInt(72), fxFromInt(170),
                                           "idle1", "run", "smflinch", "diesimple"};
        // (The people, and whatever stands as one: the human hull.)
        static const MonsterKind kPerson = {20, false, fxFromInt(16), fxFromInt(72), fxFromInt(150),
                                            "idle1", "run", "flinch", "diesimple"};
        // Round 3 (HlAliens.hpp). (The roach, the leech and the snark are a
        // few units across in the game -- too small to be shot at all on a
        // console's screen: a little more here.)
        // (Both of the roach's clips are called "run" in its model: it plays
        // them by what they are for, idle and walk.)
        static const MonsterKind kCockroach = {1, true, fxFromInt(4), fxFromInt(4), fxFromInt(60),
                                               "run", "run", "run", "run"};
        static const MonsterKind kLeech = {2, true, fxFromInt(6), fxFromInt(6), fxFromInt(100),
                                           "swim", "swim", "swim", "death"};
        static const MonsterKind kAlienGrunt = {90, false, fxFromInt(32), fxFromInt(64), fxFromInt(150),
                                                "idle1", "run", "smallflinch", "diesimple"};
        static const MonsterKind kController = {60, false, fxFromInt(32), fxFromInt(64), fxFromInt(150),
                                                "float", "run", "flinch1", "die1"};
        static const MonsterKind kSnark = {2, true, fxFromInt(6), fxFromInt(8), fxFromInt(300),
                                           "run", "run", "run", "run"};
        // Round 4. (The osprey's box is the game's height, -100 to 32 about
        // where it is (targetBox), and less wide than its 800: that is its
        // rotors' reach, which shots go through.)
        static const MonsterKind kOsprey = {400, false, fxFromInt(200), fxFromInt(132), 0,
                                            "rotor", "rotor", "rotor", "rotor"};
        // (The gargantua walks as something 64 across, as the game's does
        // (monsterHull); what is shot at is more of it, though not the
        // game's whole 214 -- its swipe comes from the middle of this.)
        static const MonsterKind kGargantua = {800, false, fxFromInt(48), fxFromInt(128), fxFromInt(100),
                                               "idle1", "walk", "Flinchlight", "die"};
        static const MonsterKind kBabyCrab = {2, true, fxFromInt(12), fxFromInt(24), fxFromInt(140),
                                              "idle1", "run", "flinch", "dieback"};
        static const MonsterKind kAssassin = {50, false, fxFromInt(16), fxFromInt(72), fxFromInt(200),
                                              "idle1", "run", "idle1", "die_simple"};
        // (Its box is about its middle: targetBox.)
        static const MonsterKind kIchthyosaur = {200, false, fxFromInt(32), fxFromInt(64), fxFromInt(150),
                                                 "swim", "swim", "smflinch", "die1"};
        // (A tentacle is shot at up its stalk, as high as it reaches; its
        // root is the game's box, 64 across.)
        static const MonsterKind kTentacle = {75, false, fxFromInt(32), fxFromInt(640), 0,
                                              "pit_idle", "pit_idle", "pit_idle", "pit_idle"};
        // (The apache's box is the game's, 64 across and below where it is:
        // targetBox.)
        static const MonsterKind kApache = {250, false, fxFromInt(32), fxFromInt(64), 0, "idle", "idle", "idle", "idle"};
        // (Big momma is shot at as the game's box, 190 across and high; she
        // walks as something 64 across: monsterHull.)
        static const MonsterKind kBigMomma = {225, false, fxFromInt(95), fxFromInt(190), fxFromInt(80),
                                              "idle1", "walk", "flinch", "death"};
        // (The nihilanth's box is the game's: 240 about, from 720 below
        // where it is to 420 above -- targetBox.)
        static const MonsterKind kNihilanth = {800, false, fxFromInt(240), fxFromInt(1140), 0, "float", "float",
                                               "flinch1", "die1"};
        // (A boid is a few units across in the game: a little more here,
        // as the roach is.)
        static const MonsterKind kBoid = {10, true, fxFromInt(8), fxFromInt(8), fxFromInt(125), "idle", "idle", "idle", "idle"};
        switch (static_cast<ActorKind>(kind))
        {
        case ActorKind::Cockroach:  return kCockroach;
        case ActorKind::Leech:      return kLeech;
        case ActorKind::AlienGrunt: return kAlienGrunt;
        case ActorKind::Controller: return kController;
        case ActorKind::Snark:      return kSnark;
        case ActorKind::Osprey:     return kOsprey;
        case ActorKind::Gargantua:  return kGargantua;
        case ActorKind::BabyCrab:   return kBabyCrab;
        case ActorKind::Assassin:   return kAssassin;
        case ActorKind::Ichthyosaur: return kIchthyosaur;
        case ActorKind::Tentacle:   return kTentacle;
        case ActorKind::Apache:     return kApache;
        case ActorKind::BigMomma:   return kBigMomma;
        case ActorKind::Nihilanth:  return kNihilanth;
        case ActorKind::Boid:       return kBoid;
        default:                    break;
        }
        if (kind == static_cast<std::uint8_t>(ActorKind::Grunt))
            return kGrunt;
        if (isPerson(kind) || kind == static_cast<std::uint8_t>(ActorKind::Gman) ||
            kind == static_cast<std::uint8_t>(ActorKind::Generic))
            return kPerson;
        if (kind == static_cast<std::uint8_t>(ActorKind::Slave))
            return kSlave;
        if (kind == static_cast<std::uint8_t>(ActorKind::Bullsquid))
            return kBullsquid;
        if (kind == static_cast<std::uint8_t>(ActorKind::Houndeye))
            return kHoundeye;
        if (kind == static_cast<std::uint8_t>(ActorKind::Zombie))
            return kZombie;
        if (kind == static_cast<std::uint8_t>(ActorKind::Barnacle))
            return kBarnacle;
        return kHeadcrab;
    }

    // The weapons, as the game's skill settings have them (the same at
    // every level: HlSkill.hpp).
    inline constexpr int kBoltDamage = skillData(kSkillMedium).plrXbowBoltMonster; ///< a crossbow bolt, into a monster
    inline constexpr int kSatchelBlast = skillData(kSkillMedium).plrSatchel; ///< (to two and a half times as far)
    inline constexpr int kSnarkBite = skillData(kSkillMedium).snarkDmgBite;
    inline constexpr int kSnarkPop = skillData(kSkillMedium).snarkDmgPop; ///< its burst, and as much again for each bite
    inline constexpr int kMineBlast = skillData(kSkillMedium).plrTripmine; ///< a tripmine (HlMonsters.hpp's mines)
    inline constexpr int kMineRange = 2048;
    inline constexpr int kGrenadeBlast = skillData(kSkillMedium).plrHandGrenade;
    inline constexpr int kGrenadeRadius = 250;
    inline constexpr fx kSpitSpeed = fxFromInt(900);

    inline constexpr fx kSightRange = fxFromInt(1024);
    /// The size a kind walks about as (HlWalk.hpp kWalk*).
    inline int monsterHull(std::uint8_t kind)
    {
        // (The alien grunt and the controller are as wide as the squid, the
        // game's 64.)
        if (kind == static_cast<std::uint8_t>(ActorKind::Bullsquid) ||
            kind == static_cast<std::uint8_t>(ActorKind::AlienGrunt) ||
            kind == static_cast<std::uint8_t>(ActorKind::Controller) ||
            kind == static_cast<std::uint8_t>(ActorKind::Gargantua) ||
            kind == static_cast<std::uint8_t>(ActorKind::BigMomma))
            return kWalkLarge;
        return monsterKind(kind).small ? kWalkCrouched : kWalkStanding;
    }

    inline constexpr fx kLeapRange = fxFromInt(256);
    inline constexpr fx kZombieReach = fxFromInt(70);

    // ------------------------------------------------------------- the player

    /// A monster's blow (`type`: what kind, kDmg*): playerTakeDamage's.
    /// `from`: where it came from, if it came from somewhere.
    inline void playerHurt(PlayerState &player, int damage, const fx *from = nullptr,
                           std::uint32_t type = kDmgGeneric)
    {
        playerTakeDamage(player, damage, type, from);
    }

    namespace detail
    {
        /// Something heard from where a monster is.
        inline void monsterSound(LogicWorld &w, const ActorState &actor, CommonSound sound)
        {
            if (w.heard != nullptr)
                w.heard->add(sound, actor.pos, 127, kRadiusLarge);
        }
        /// ... the one that belongs to its kind: `which` is 0 for being
        /// roused, 1 for being hurt, 2 for dying.
        inline void monsterVoice(LogicWorld &w, const ActorState &actor, int which)
        {
            static const CommonSound kVoices[][3] = {
                {CommonSound::HeadcrabAlert, CommonSound::HeadcrabPain, CommonSound::HeadcrabDie},
                {CommonSound::ZombieAlert, CommonSound::ZombiePain, CommonSound::ZombiePain},
                {CommonSound::BarnacleAlert, CommonSound::BarnacleChew, CommonSound::BarnacleDie},
                {CommonSound::SlaveAlert, CommonSound::SlavePain, CommonSound::SlaveDie},
                {CommonSound::SquidAlert, CommonSound::SquidPain, CommonSound::SquidDie},
                {CommonSound::HoundAlert, CommonSound::HoundPain, CommonSound::HoundDie},
            };
            int row = 0;
            switch (static_cast<ActorKind>(actor.kind))
            {
            case ActorKind::Zombie:    row = 1; break;
            case ActorKind::Barnacle:  row = 2; break;
            case ActorKind::Slave:     row = 3; break;
            case ActorKind::Bullsquid: row = 4; break;
            case ActorKind::Houndeye:  row = 5; break;
            default:                   row = 0; break;
            }
            monsterSound(w, actor, kVoices[row][which]);
        }

        /// A line through the world and its brushes as a Trace: where it got
        /// to (a sixteenth of a unit short of what it met), and which way
        /// that faces.
        inline Trace pointTrace(const MoveWorld &world, const fx *from, const fx *to)
        {
            Trace t;
            t.allSolid = false;
            const Shot shot = traceShot(world, from, to);
            t.fraction = shot.fraction;
            const fx d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
            const fx far = fxLength(d);
            for (int i = 0; i < 3; ++i)
            {
                t.endPos[i] = from[i] + static_cast<fx>((static_cast<std::int64_t>(d[i]) * shot.fraction) >> kFractionShift);
                if (shot.fraction < kFractionOne && far > 0)
                    t.endPos[i] -= static_cast<fx>((static_cast<std::int64_t>(d[i]) * (kFxOne / 16)) / far);
            }
            if (shot.fraction < kFractionOne)
            {
                fx at[3], normal[3];
                t.hit = traceShotWall(world, from, to, at, normal);
                for (int i = 0; t.hit && i < 3; ++i)
                    t.normal[i] = normal[i] << (kNormalShift - kFxShift);
            }
            return t;
        }

        inline std::uint32_t chance(ActorWorld &a)
        {
            a.seed = a.seed * 1664525u + 1013904223u;
            return a.seed >> 8;
        }

        inline fx playerFeet(const PlayerState &player)
        {
            return player.origin[2] - (player.ducked ? kCrouchedFeet : kStandingFeet);
        }

        /// Whether a box with its feet at `feet` is in the player.
        inline bool touchesPlayer(const PlayerState &player, const fx *feet, fx half, fx height)
        {
            const fx mins[3] = {feet[0] - half, feet[1] - half, feet[2]};
            const fx maxs[3] = {feet[0] + half, feet[1] + half, feet[2] + height};
            return playerTouchesBox(player, mins, maxs, 0);
        }

    } // namespace detail

    // ------------------------------------------------------------- monsters

    namespace detail
    {
        inline void monsterClip(ActorWorld &a, ActorState &actor, const char *clip, bool once)
        {
            const ModelView *model = actorModel(a, actor);
            const int index = model != nullptr ? model->findClip(nameHash(clip)) : -1;
            // (Already in it, and it loops: let it run on.)
            if (index >= 0 && index == actor.clip && !once && !actor.once)
                return;
            if (!actorPlay(a, actor, nameHash(clip), once) && once)
            {
                // A clip this map's model was not given: it is over at once.
                actor.once = true;
                actor.finished = true;
            }
            actor.lastFrame = 0;
        }

        /// Whether the frame `at` of the actor's clip went by this tick.
        inline bool passedFrame(const ActorState &actor, int at)
        {
            const std::uint32_t frame = static_cast<std::uint32_t>(at) << 8;
            return actor.lastFrame < frame && frame <= actor.frame;
        }
    } // namespace detail

    // ------------------------------------------------ things seen, things thrown

    inline void effectsTick(LogicWorld &w, PlayerState &player, int ms);
    inline bool gibBurst(LogicWorld &w, int index);
    inline bool lineEntersBox(const fx *from, const fx *to, const fx *mins, const fx *maxs, std::int64_t &enter);
    inline void shakeStart(LogicWorld &w, fx amplitude, int ms);
    inline void explodeAt(LogicWorld &w, PlayerState &player, const fx *at, int size, bool fireball, bool sound,
                          bool hurts);
    inline void monsterHurt(LogicWorld &w, int index, int damage, bool bullet, int group = kHitBody,
                            std::uint32_t type = kDmgUnset, std::int16_t attacker = kAiPlayer,
                            const fx *from = nullptr);
    inline void aiActorHurt(LogicWorld &w, PlayerState *player, int index, const AiDamage &d);
    inline void turretHurt(LogicWorld &w, int index, int damage, bool bullet, int group);
    inline void mineHurt(LogicWorld &w, int index);
    namespace detail
    {
        inline void mineTick(LogicWorld &w, PlayerState &player, int index, int ms);
    }
    inline bool fights(std::uint8_t kind);
    /// What a shot, a blow or a blast can hurt: what fights, and people.
    inline bool canBeHurt(std::uint8_t kind);
    inline void targetBox(const ActorState &actor, fx *mins, fx *maxs);
    /// Blood where something that bleeds was hurt at `at`: a puff of its
    /// colour, and a mark on the wall or floor behind it along `dir` (null:
    /// on the floor under it) -- more of it, and a pool, for the blow that
    /// killed.
    inline void bleed(LogicWorld &w, int index, const fx *at, const fx *dir, bool heavy);

    /// A blast at `origin`: hurts the player and every monster within
    /// `radius` units that it can reach in a straight line, less with
    /// distance (all of `damage` at the middle, none at the edge), and breaks
    /// what breaks. `except` is a monster it leaves alone (its own maker),
    /// or -1.
    inline void radiusDamage(LogicWorld &w, PlayerState &player, const fx *origin, int damage, int radius,
                             int except, std::uint32_t type = kDmgBlast, std::int16_t attacker = kAiPlayer)
    {
        ActorWorld &a = *w.actors;
        auto share = [&](const fx *at) -> int
        {
            const fx d[3] = {at[0] - origin[0], at[1] - origin[1], at[2] - origin[2]};
            const int distance = fxToInt(fxLength(d));
            if (distance >= radius || !lineClear(*w.world, origin, at))
                return 0;
            return (damage * (radius - distance)) / radius;
        };
        // A tripmine the player put down, caught in it, goes too (a moment
        // after, as any mine does).
        for (Projectile &shot : a.shots)
        {
            if (shot.active && shot.kind == kShotMine && shot.sprite != 2 && share(shot.pos) > 0)
            {
                shot.sprite = 2;
                shot.leftMs = 100 + static_cast<int>(detail::chance(a) % 200);
            }
        }
        if (player.health > 0)
        {
            const int hurt = share(player.origin);
            if (hurt > 0)
            {
                playerHurt(player, hurt, nullptr, kDmgBlast);
                a.got |= kGotHurt;
            }
        }
        for (int i = 0; i < a.count; ++i)
        {
            const ActorState &actor = a.states[i];
            if (i == except || !actor.active || !canBeHurt(actor.kind))
                continue;
            fx mins[3], maxs[3];
            targetBox(actor, mins, maxs);
            const fx middle[3] = {actor.pos[0], actor.pos[1], (mins[2] + maxs[2]) / 2};
            if (actor.ai >= kAiDying)
            {
                // Something lying dead, caught in it: it comes apart (the
                // game's rule: a blast of more than what is left of it).
                if (actorThinks(actor.kind))
                {
                    const int hurt = share(middle);
                    if (hurt > 0)
                        monsterHurt(w, i, hurt, false, kHitBody, type, attacker, origin);
                }
                else if ((isGrunt(actor.kind) || isPerson(actor.kind)) && share(middle) >= 40)
                {
                    gibBurst(w, i);
                }
                continue;
            }
            const int hurt = share(middle);
            if (hurt > 0)
            {
                monsterHurt(w, i, hurt, false, kHitBody, type, attacker, origin);
                // (Away from the blast.)
                fx away[3] = {middle[0] - origin[0], middle[1] - origin[1], middle[2] - origin[2]};
                const fx far = fxLength(away);
                for (int k = 0; k < 3 && far > 0; ++k)
                    away[k] = static_cast<fx>((static_cast<std::int64_t>(away[k]) << kFxShift) / far);
                bleed(w, i, middle, far > 0 ? away : nullptr, a.states[i].ai >= kAiDying || !a.states[i].active);
            }
        }
        for (int i = 0; i < w.recordCount; ++i)
        {
            const LogicRecord &record = w.records[i];
            if (w.states[i].dead || record.brush == kNoBrush || !detail::takesBlows(record))
                continue;
            // (RadiusDamage's: as far as where the line to its middle meets
            // it -- a long pipe is near all along -- and only if that line
            // gets there.)
            const MoverHull &hull = w.movers[record.brush];
            const fx middle[3] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2,
                                  (hull.mins[2] + hull.maxs[2]) / 2};
            const fx d[3] = {middle[0] - origin[0], middle[1] - origin[1], middle[2] - origin[2]};
            const fx whole = fxLength(d);
            std::int64_t enter = 0;
            const fx reach = lineEntersBox(origin, middle, hull.mins, hull.maxs, enter)
                                 ? static_cast<fx>((static_cast<std::int64_t>(whole) * enter) >> kFractionShift)
                                 : whole;
            const int distance = fxToInt(reach);
            if (distance >= radius)
                continue;
            // (Something else in the way -- a wall, another brush -- well
            // short of it. A blast set into a wall is not stopped by it.)
            const Shot shot = traceShot(*w.world, origin, middle);
            const fx stopped = static_cast<fx>((static_cast<std::int64_t>(whole) * shot.fraction) >> kFractionShift);
            if (shot.fraction > 0 && shot.mover != static_cast<std::int16_t>(record.brush) &&
                stopped < reach - fxFromInt(8))
                continue;
            logicStrike(w, player, i, (damage * (radius - distance)) / radius, kDmgBlast);
        }
    }

    /// The monster at `index` comes apart: its body is gone, and the game's
    /// pieces fly -- an alien's four; a person's skull and four of the five
    /// others -- from inside its box, faster the further past dead it was
    /// taken. False (and nothing done) if the map has not got the pieces.
    inline bool gibBurst(LogicWorld &w, int index)
    {
        ActorWorld &a = *w.actors;
        ActorState &actor = a.states[index];
        const bool human = isGrunt(actor.kind) || isPerson(actor.kind) || isAssassin(actor.kind);
        const int model = human ? a.humanGibs : a.alienGibs;
        const int pieces = gibPieces(a, model);
        if (pieces == 0)
            return false;
        fx mins[3], maxs[3];
        targetBox(actor, mins, maxs);
        // (The game's: seven tenths as fast for something only just far
        // enough gone, twice for something well past, four times beyond.)
        const int scale = actor.health > -50 ? 7 : (actor.health > -200 ? 20 : 40);
        const int count = human ? 5 : 4;
        for (int n = 0; n < count; ++n)
        {
            // A person's first piece is the skull, from where the head was.
            const int body = human ? (n == 0 ? 0 : 1 + static_cast<int>(detail::chance(a) % 5)) % pieces
                                   : static_cast<int>(detail::chance(a) % static_cast<std::uint32_t>(pieces));
            fx at[3], velocity[3];
            for (int k = 0; k < 3; ++k)
            {
                const fx span = maxs[k] - mins[k];
                at[k] = mins[k] + static_cast<fx>((static_cast<std::int64_t>(span) * (detail::chance(a) & 255)) >> 8);
                velocity[k] = fxFromInt(static_cast<int>(detail::chance(a) % 201) - 100) * scale / 10;
            }
            if (human && n == 0)
                at[2] = maxs[2];
            // (A piece is where a crouching person's middle would be -- its
            // trace is theirs: from lower in the body it starts in the floor
            // and is given up.)
            if (at[2] < mins[2] + kCrouchedFeet + fxFromInt(1))
                at[2] = mins[2] + kCrouchedFeet + fxFromInt(1);
            velocity[2] = fxFromInt(200 + static_cast<int>(detail::chance(a) % 101)) * scale / 10;
            for (int k = 0; k < 3; ++k)
                velocity[k] = velocity[k] > fxFromInt(1500) ? fxFromInt(1500)
                                                           : (velocity[k] < -fxFromInt(1500) ? -fxFromInt(1500) : velocity[k]);
            Gib &gib = gibAdd(a, model, body, at, velocity,
                              (6000 + static_cast<int>(detail::chance(a) % 2000)) * a.lifePercent / 100);
            gib.yaw = static_cast<std::uint16_t>(detail::chance(a));
            gib.spin = static_cast<std::int16_t>(40 + detail::chance(a) % 60);
            effectAdd(a, kEffectBlood, at, nullptr, human ? 0 : 1, 300);
        }
        if (w.heard != nullptr)
            w.heard->add(CommonSound::BodySplat, actor.pos, 127, kRadiusLarge);
        if (actor.script >= 0 && actor.script < w.recordCount)
        {
            w.states[actor.script].dead = true;
            actor.script = -1;
            actor.scriptPhase = kActorFree;
        }
        actor.ai = kAiDead;
        actor.active = false;
        actor.flags &= static_cast<std::uint8_t>(~kActorSolid);
        actorPlaceBox(a, index);
        return true;
    }

    /// Hurt the monster at `index`. `bullet`: by a bullet, which a zombie
    /// hardly feels (three tenths, as in the game).
    /// `group`: the part of it that was hit (kHit*).
    inline void monsterHurt(LogicWorld &w, int index, int damage, bool bullet, int group, std::uint32_t type,
                            std::int16_t attacker, const fx *from)
    {
        ActorWorld &a = *w.actors;
        ActorState &actor = a.states[index];
        if (actor.active && actor.kind == static_cast<std::uint8_t>(ActorKind::Player))
        {
            // Another player: what they take is theirs to take, on their
            // own console. Here it is only counted, off a health nobody
            // dies of (HlNetGame.hpp reads it and tells them). A bullet in
            // the head is three, as the game has it for players.
            actor.health = static_cast<std::int16_t>(actor.health - (bullet && group == kHitHead ? damage * 3 : damage));
            return;
        }
        if (actor.active && isTurret(actor.kind))
        {
            const bool standing = actor.ai < kAiDying;
            turretHurt(w, index, damage, bullet, group);
            if (standing && actor.ai >= kAiDying && w.achievements != nullptr)
                w.achievements->push(AchEventType::Killed, 0, actor.kind, attacker == kAiPlayer ? kAchByPlayer : 0);
            return;
        }
        if (actor.active && isMine(actor.kind))
        {
            mineHurt(w, index);
            return;
        }
        if (!actor.active || !actorThinks(actor.kind))
            return;
        // The monsters that think as the game's do: its TakeDamage. (A shot
        // is a bullet that never tears apart: the game's multidamage.)
        AiDamage d;
        d.amount = damage;
        d.type = type != kDmgUnset ? type : (bullet ? kDmgBullet | kDmgNeverGib : kDmgGeneric);
        d.attacker = attacker;
        d.group = static_cast<std::uint8_t>(group);
        if (from != nullptr)
        {
            d.hasFrom = true;
            for (int k = 0; k < 3; ++k)
                d.from[k] = from[k];
        }
        else if (attacker == kAiPlayer && w.actors->ai != nullptr && w.actors->ai->player != nullptr)
        {
            d.hasFrom = true;
            for (int k = 0; k < 3; ++k)
                d.from[k] = w.actors->ai->player->origin[k];
        }
        aiActorHurt(w, w.actors->ai != nullptr ? w.actors->ai->player : nullptr, index, d);
    }

    /// Every one of `kind` there is, dead of it at once -- each as it would
    /// die shot, its own death, its own cry (the COMMAND line's killall).
    /// How many there were.
    inline int killAllOfKind(LogicWorld &w, ActorKind kind)
    {
        if (w.actors == nullptr)
            return 0;
        ActorWorld &a = *w.actors;
        int killed = 0;
        // (What a cheat kills counts for nothing.)
        if (w.achievements != nullptr)
            ++w.achievements->muted;
        for (int i = 0; i < a.count; ++i)
        {
            const ActorState &actor = a.states[i];
            if (!actor.active || actor.kind != static_cast<std::uint8_t>(kind) || actor.ai >= kAiDying ||
                !canBeHurt(actor.kind))
                continue;
            // (Just enough each time, so that it dies and is not blown
            // apart; one not yet given its health -- it has not had a first
            // look -- may take another.)
            for (int tries = 0; tries < 3 && a.states[i].active && a.states[i].ai < kAiDying; ++tries)
            {
                const int health = a.states[i].health > 0 ? a.states[i].health : monsterKind(actor.kind).health;
                monsterHurt(w, i, (health > 0 ? health : 20) + 1, false);
            }
            ++killed;
        }
        if (w.achievements != nullptr)
            --w.achievements->muted;
        return killed;
    }

    namespace detail
    {
        inline void peopleGround(LogicWorld &w, ActorState &actor, int ms);

        /// A scene has let a monster go: it thinks again by itself, idle (or
        /// lying in wait, `ambush`) -- now, not at its next think, so that
        /// whatever follows has its mind as it is (CineCleanup).
        inline void scriptRouse(LogicWorld &w, int actorIndex, bool ambush);

        // ----------------------------------------------------------- items

        /// The player walks over an item: true if it was taken. (`skill`:
        /// the game's level's numbers -- how much a kit or a battery gives.)
        inline bool itemTake(PlayerState &player, std::uint8_t item, std::uint32_t &got,
                             const SkillData &skill = skillData(kSkillMedium))
        {
            Inventory &inv = player.inv;
            auto add = [](std::int16_t &what, int amount, int most)
            {
                if (what >= most)
                    return false;
                what = static_cast<std::int16_t>(what + amount > most ? most : what + amount);
                return true;
            };
            auto weapon = [&](std::uint8_t which)
            {
                if (inv.have & (1u << which))
                    return false;
                inv.have |= static_cast<std::uint8_t>(1u << which);
                inv.current = which;
                got |= kGotWeapon;
                return true;
            };
            switch (static_cast<ItemKind>(item))
            {
            case ItemKind::Crowbar:
                return weapon(kWeaponCrowbar);
            case ItemKind::Glock:
                // A new one comes loaded; another is its rounds.
                if (weapon(kWeaponGlock))
                {
                    inv.clipGlock = 17;
                    return true;
                }
                return add(inv.ammo9mm, 17, 250);
            case ItemKind::Shotgun:
                if (weapon(kWeaponShotgun))
                {
                    inv.clipShotgun = 8;
                    add(inv.ammoBuck, 4, 125);
                    return true;
                }
                return add(inv.ammoBuck, 12, 125);
            case ItemKind::GlockClip:
                return add(inv.ammo9mm, 17, 250);
            case ItemKind::Mp5:
                // (Twenty-five rounds with it, as the game gives.)
                if (weapon(kWeaponMp5))
                {
                    inv.clipMp5 = 25;
                    return true;
                }
                return add(inv.ammo9mm, 25, 250);
            case ItemKind::Python:
                if (weapon(kWeaponPython))
                {
                    inv.clipPython = 6;
                    return true;
                }
                return add(inv.ammo357, 6, 36);
            case ItemKind::Mp5Clip:
                return add(inv.ammo9mm, 50, 250);
            case ItemKind::AmmoBox:
                return add(inv.ammo9mm, 200, 250);
            case ItemKind::ArGrenades:
                return add(inv.ammoAr, 2, 10);
            case ItemKind::Ammo357:
                return add(inv.ammo357, 6, 36);
            case ItemKind::Buckshot:
                return add(inv.ammoBuck, 12, 125);
            case ItemKind::HealthKit:
                if (player.health >= 100)
                    return false;
            {
                const int heal = skill.healthkit;
                player.health = player.health + heal > 100 ? 100 : player.health + heal;
                return true;
            }
            case ItemKind::Battery:
                // Armour is the suit's: without one a battery is left lying.
                return (inv.have & kHaveSuit) != 0 && add(inv.armor, skill.battery, 100);
            case ItemKind::Grenades:
                if (!add(inv.grenades, 5, 10))
                    return false;
                if (!(inv.have & (1u << kWeaponGrenade)))
                {
                    inv.have |= static_cast<std::uint8_t>(1u << kWeaponGrenade);
                    got |= kGotWeapon;
                }
                return true;
            case ItemKind::Suit:
                if (inv.have & kHaveSuit)
                    return false;
                inv.have |= kHaveSuit;
                return true;
            default:
                break;
            }
            // The weapons that came later: the Arsenal's (none without one).
            Arsenal *ars = player.arsenal;
            if (ars == nullptr)
                return false;
            auto later = [&](std::uint8_t which)
            {
                if (hasWeapon(player, which))
                    return false;
                giveWeapon(player, which);
                inv.current = which;
                got |= kGotWeapon;
                return true;
            };
            switch (static_cast<ItemKind>(item))
            {
            case ItemKind::Crossbow:
                // A new one comes loaded; another is its bolts.
                if (later(kWeaponCrossbow))
                {
                    ars->boltsClip = 5;
                    return true;
                }
                return add(ars->bolts, 5, 50);
            case ItemKind::Bolts:
                return add(ars->bolts, 5, 50);
            case ItemKind::Satchel:
            case ItemKind::Tripmine:
            case ItemKind::Snark:
            {
                const bool satchel = static_cast<ItemKind>(item) == ItemKind::Satchel;
                const bool snark = static_cast<ItemKind>(item) == ItemKind::Snark;
                std::int16_t &count = satchel ? ars->satchels : (snark ? ars->snarks : ars->tripmines);
                if (!add(count, snark ? 5 : 1, snark ? 15 : 5))
                    return false;
                const std::uint8_t which = satchel ? kWeaponSatchel : (snark ? kWeaponSnark : kWeaponTripmine);
                if (!hasWeapon(player, which))
                {
                    giveWeapon(player, which);
                    got |= kGotWeapon;
                }
                return true;
            }
            // The last four, by the game's single-player amounts: the RPG
            // comes with its rocket in it; a box of rockets is one more.
            case ItemKind::Rpg:
                if (later(kWeaponRpg))
                {
                    ars->have = static_cast<std::uint16_t>(ars->have | kArsenalRpgLoaded);
                    return true;
                }
                return add(ars->rockets, 1, 5);
            case ItemKind::RpgClip:
                return add(ars->rockets, 1, 5);
            case ItemKind::Gauss:
            case ItemKind::Egon:
            {
                const std::uint8_t which = static_cast<ItemKind>(item) == ItemKind::Gauss ? kWeaponGauss : kWeaponEgon;
                if (later(which))
                {
                    add(ars->uranium, 20, 100);
                    return true;
                }
                return add(ars->uranium, 20, 100);
            }
            case ItemKind::GaussClip:
                return add(ars->uranium, 20, 100);
            case ItemKind::Hornetgun:
                // (It grows its own hornets: another is no more of them.)
                if (later(kWeaponHornetgun))
                {
                    ars->hornets = 8;
                    return true;
                }
                return false;
            case ItemKind::Antidote:
                return add(ars->antidotes, 1, 99);
            case ItemKind::Security:
                return add(ars->securityCards, 1, 99);
            default:
                return false;
            }
        }

        /// What is in a weaponbox (`contents`: "uranium=60 rockets=8", as
        /// the level writes it), all of it at once, each up to what may be
        /// carried; and the grenades, satchels and tripmines in it are their
        /// weapons too, as picking those up is. True if there was anything.
        inline bool weaponboxTake(PlayerState &player, const char *contents, std::uint32_t &got)
        {
            if (contents == nullptr)
                return false;
            Inventory &inv = player.inv;
            Arsenal *ars = player.arsenal;
            bool any = false;
            auto add = [&](std::int16_t &what, int amount, int most)
            {
                if (amount > 0 && what < most)
                    what = static_cast<std::int16_t>(what + amount > most ? most : what + amount);
                any = any || amount > 0;
            };
            auto arm = [&](std::uint8_t which)
            {
                if (!hasWeapon(player, which))
                {
                    giveWeapon(player, which);
                    got |= kGotWeapon;
                }
            };
            for (const char *p = contents; *p != '\0';)
            {
                while (*p == ' ')
                    ++p;
                char key[16];
                int n = 0;
                while (*p != '\0' && *p != '=' && *p != ' ' && n < 15)
                    key[n++] = *p++;
                key[n] = '\0';
                int amount = 0;
                if (*p == '=')
                {
                    ++p;
                    while (*p >= '0' && *p <= '9')
                        amount = amount * 10 + (*p++ - '0');
                }
                while (*p != '\0' && *p != ' ')
                    ++p;
                if (amount <= 0)
                    continue;
                if (std::strcmp(key, "9mm") == 0)
                    add(inv.ammo9mm, amount, 250);
                else if (std::strcmp(key, "357") == 0)
                    add(inv.ammo357, amount, 36);
                else if (std::strcmp(key, "buckshot") == 0)
                    add(inv.ammoBuck, amount, 125);
                else if (std::strcmp(key, "argrenades") == 0)
                    add(inv.ammoAr, amount, 10);
                else if (std::strcmp(key, "grenades") == 0)
                {
                    add(inv.grenades, amount, 10);
                    arm(kWeaponGrenade);
                }
                else if (ars == nullptr)
                    continue;
                else if (std::strcmp(key, "bolts") == 0)
                    add(ars->bolts, amount, 50);
                else if (std::strcmp(key, "rockets") == 0)
                    add(ars->rockets, amount, 5);
                else if (std::strcmp(key, "uranium") == 0)
                    add(ars->uranium, amount, 100);
                else if (std::strcmp(key, "satchels") == 0)
                {
                    add(ars->satchels, amount, 5);
                    arm(kWeaponSatchel);
                }
                else if (std::strcmp(key, "tripmines") == 0)
                {
                    add(ars->tripmines, amount, 5);
                    arm(kWeaponTripmine);
                }
                else if (std::strcmp(key, "snarks") == 0)
                {
                    add(ars->snarks, amount, 15);
                    arm(kWeaponSnark);
                }
            }
            return any;
        }

        /// What a weaponbox actor has in it (the level's text for it), or null.
        inline const char *weaponboxContents(const LogicWorld &w, int index)
        {
            if (w.actors == nullptr || w.level == nullptr || w.level->strings == nullptr ||
                index >= w.actors->recordCount || w.actors->records == nullptr)
                return nullptr;
            const std::uint32_t at = w.actors->records[index].item >> kItemStringShift;
            return at < w.level->stringsSize ? w.level->strings + at : nullptr;
        }
        /// An item actor taken: a weaponbox by what is in it, anything else by
        /// its kind.
        inline bool itemTakeAt(const LogicWorld &w, PlayerState &player, int index, std::uint32_t &got)
        {
            const ActorState &actor = w.actors->states[index];
            if (actor.item == static_cast<std::uint8_t>(ItemKind::Weaponbox))
                return weaponboxTake(player, weaponboxContents(w, index), got);
            return itemTake(player, actor.item, got, skillOf(w));
        }

        // --------------------------------------------- makers and chargers

        inline void makerTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            ActorWorld &a = *w.actors;
            // Its children who have fallen fade away (ShouldFadeOnDeath: one
            // that came from a spawner does), and their places are free once
            // they are gone.
            for (int n = 0; n < static_cast<int>(record.extra[1]); ++n)
            {
                const int child = static_cast<int>(record.extra[0]) + n;
                if (child < a.count && a.states[child].active && a.states[child].ai == kAiDead)
                    corpseFadeStart(a.states[child]);
            }
            if (state.dead)
                return;
            bool make = false;
            if (record.flags & kLogicToggle)
            {
                make = state.phase > 0;
            }
            else if (state.running)
            {
                state.speed -= ms;
                make = state.speed <= 0;
            }
            if (!make)
                return;
            // A place of its own that is free: never used, or whose last
            // tenant is dead and has lain a while.
            const int first = static_cast<int>(record.extra[0]);
            const int count = static_cast<int>(record.extra[1]);
            int slot = -1;
            for (int n = 0; n < count && first + n < a.recordCount; ++n)
            {
                const ActorState &there = a.states[first + n];
                if (there.active && there.ai != kAiDead)
                    continue;
                if (!there.active)
                {
                    slot = first + n;
                    break;
                }
                // (A body still fading: its place, if no other is free --
                // the game makes the next while the last is going.)
                if (slot < 0 && there.ai == kAiDead)
                    slot = first + n;
            }
            if (slot < 0)
            {
                // All of its children are about: it waits for one to die.
                state.speed = 500;
                return;
            }
            // Not on top of the player -- nor of anyone in the column under
            // it, down to the floor (MakeMonster: "don't build a stack of
            // monsters", 34 out each way).
            const ActorRecord &born = a.records[slot];
            const MonsterKind &kind = monsterKind(born.kind);
            if (touchesPlayer(player, born.origin, kind.half + fxFromInt(8), kind.height))
            {
                state.speed = 500;
                return;
            }
            {
                const fx down[3] = {born.origin[0], born.origin[1], born.origin[2] - fxFromInt(2048)};
                const Shot floor = traceShot(*w.world, born.origin, down);
                const fx ground = born.origin[2] -
                                  static_cast<fx>((static_cast<std::int64_t>(fxFromInt(2048)) * floor.fraction) >> kFractionShift);
                const fx lo[3] = {born.origin[0] - fxFromInt(34), born.origin[1] - fxFromInt(34), ground};
                const fx hi[3] = {born.origin[0] + fxFromInt(34), born.origin[1] + fxFromInt(34), born.origin[2]};
                bool stacked = playerTouchesBox(player, lo, hi, 0);
                for (int n = 0; n < a.count && !stacked; ++n)
                {
                    const ActorState &other = a.states[n];
                    if (!other.active || !isMonster(other.kind))
                        continue;
                    fx olo[3], ohi[3];
                    targetBox(other, olo, ohi);
                    stacked = ohi[0] >= lo[0] && olo[0] <= hi[0] && ohi[1] >= lo[1] && olo[1] <= hi[1] &&
                              ohi[2] >= lo[2] && olo[2] <= hi[2];
                }
                if (stacked)
                {
                    state.speed = 500;
                    return;
                }
            }
            actorSetup(a, slot, born, *w.world);
            a.states[slot].flags &= static_cast<std::uint8_t>(~kActorDormant);
            aiForget(a, slot); // (a new monster in the old one's place: it thinks afresh)
            actorPlaceBox(a, slot);
            if (state.phase > 0)
                --state.phase;
            state.speed = record.waitMs > 0 ? record.waitMs : 1000;
            ++state.bits;
            logicUseTargets(w, player, index, kUseToggle, 0);
            if (record.extra[2] != 0 && state.bits >= record.extra[2])
                state.dead = true;
        }

        // A wall charger (CWallHealth, healthkit.cpp; CRecharge, h_battery.cpp),
        // used each frame USE is held on it. Its state: `bits` how much it
        // has given; `phase` the game's m_iOn (0 off, 1 its first sound
        // heard, 2 its loop going round); `hurt` the ms until its next
        // sound may be (m_flSoundTime); `speed` the ms until its next point
        // (m_flNextCharge); `travel[0]` the ms until it is off, if it is
        // not used again (its Off think, a quarter of a second on).
        inline void chargerSound(LogicWorld &w, int index, std::uint16_t sound, std::uint8_t flags)
        {
            const LogicRecord &record = w.records[index];
            if (w.heard == nullptr || record.brush == kNoBrush || w.movers == nullptr)
                return;
            const MoverHull &hull = w.movers[record.brush];
            const fx at[3] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2,
                              (hull.mins[2] + hull.maxs[2]) / 2};
            // (The suit's at 0.85 of the health's: h_battery.cpp's.)
            const int volume = record.kind == static_cast<std::uint8_t>(LogicKind::SuitCharger) ? 108 : 127;
            w.heard->add(sound, at, volume, kRadiusLarge, flags, 100, static_cast<std::uint16_t>(index));
        }

        /// Off: its loop stopped, ready to start again (Off()).
        inline void chargerOff(LogicWorld &w, int index)
        {
            LogicState &state = w.states[index];
            if (state.phase > 1 && w.heard != nullptr)
                w.heard->add(kNoSound, nullptr, 0, 0, kSoundLoopOff, 100, static_cast<std::uint16_t>(index));
            state.phase = 0;
            state.running = false;
        }

        inline void chargerUse(LogicWorld &w, PlayerState &player, int index)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            const bool suit = record.kind == static_cast<std::uint8_t>(LogicKind::SuitCharger);
            const LogicSound none = {kNoSound, kNoSound, kNoSound, kNoSound, kNoSound, kNoSound};
            const LogicSound &sounds = w.sounds != nullptr ? w.sounds[index] : none;
            // Empty, or a player without the suit (either kind wants it):
            // its refusal, no more often than it takes to be heard.
            const bool empty = static_cast<int>(state.bits) >= chargerCapacity(w, record);
            if (empty && state.running)
                chargerOff(w, index);
            if (empty || !(player.inv.have & kHaveSuit))
            {
                if (state.hurt <= 0)
                {
                    state.hurt = 620;
                    chargerSound(w, index, sounds.locked, 0);
                }
                return;
            }
            // Someone with no room for it: nothing, not a sound -- and, one
            // just filled, its loop stops.
            if (suit ? player.inv.armor >= 100 : player.health >= 100)
            {
                if (state.running)
                    chargerOff(w, index);
                return;
            }
            state.running = true;
            state.travel[0] = 250;
            if (state.speed > 0)
                return;
            // On: its first sound; then, still held when that is done, its
            // loop.
            if (state.phase == 0)
            {
                state.phase = 1;
                if (sounds.unlocked != kNoSound)
                    chargerSound(w, index, sounds.unlocked, 0);
                else if (w.heard != nullptr) // (a map cooked before it had its own)
                    w.heard->add(CommonSound::Charge, nullptr, 100);
                state.hurt = 560;
            }
            if (state.phase == 1 && state.hurt <= 0)
            {
                state.phase = 2;
                chargerSound(w, index, sounds.move, kSoundLoopOn);
            }
            // A point.
            if (suit)
                player.inv.armor = static_cast<std::int16_t>(player.inv.armor + 1);
            else
                ++player.health;
            ++state.bits;
            state.speed = 100;
        }

        inline void chargerTick(LogicWorld &w, PlayerState &, int index, int ms)
        {
            LogicState &state = w.states[index];
            // (Its clocks run down whether or not it is on: a refusal is
            // timed as much as a charge.)
            if (state.hurt > -1000)
                state.hurt -= ms;
            if (state.speed > -1000)
                state.speed -= ms;
            if (state.running && (state.travel[0] -= ms) <= 0)
                chargerOff(w, index);
        }
    } // namespace detail

    namespace detail
    {
        /// How far along a tripmine's beam from `from` to `end` (Q30) the first
        /// thing is: the world and what moves in it, the player, anyone who
        /// fights or is one of the people. (`skip`: an actor not to count, the
        /// mine itself.)
        inline std::int32_t beamReach(const LogicWorld &w, const PlayerState &player, const fx *from, const fx *end,
                                      int skip)
        {
            const ActorWorld &a = *w.actors;
            std::int64_t reach = traceShot(*w.world, from, end).fraction;
            std::int64_t enter;
            if (player.health > 0)
            {
                const fx tall = player.ducked ? kCrouchedFeet : kStandingFeet;
                const fx mins[3] = {player.origin[0] - fxFromInt(16), player.origin[1] - fxFromInt(16),
                                    player.origin[2] - tall};
                const fx maxs[3] = {player.origin[0] + fxFromInt(16), player.origin[1] + fxFromInt(16),
                                    player.origin[2] + tall};
                if (lineEntersBox(from, end, mins, maxs, enter) && enter < reach)
                    reach = enter;
            }
            for (int i = 0; i < a.count; ++i)
            {
                const ActorState &actor = a.states[i];
                // (Anyone at all breaks the beam: a scientist running from
                // the silo's glass, as much as a soldier.)
                if (i == skip || !actor.active || !(fights(actor.kind) || isPerson(actor.kind)) || isMine(actor.kind) ||
                    actor.ai >= kAiDying)
                    continue;
                fx mins[3], maxs[3];
                targetBox(actor, mins, maxs);
                if (lineEntersBox(from, end, mins, maxs, enter) && enter < reach)
                    reach = enter;
            }
            return static_cast<std::int32_t>(reach);
        }

        /// A snark's next bite and how many it has had (Projectile::sprite).
        inline int snarkBites(const Projectile &shot) { return shot.sprite & 0xFF; }

        /// A gargantua's stomp, a frame on (CStomp::Think, in the frame's
        /// time rather than tenths of a second): it gathers speed, and its
        /// speed gathers speed; whatever is in its way just ahead is hurt
        /// (the player once); it leaves sparks on the ground where it has
        /// been, and is gone at the wall.
        inline void stompTick(LogicWorld &w, PlayerState &player, Projectile &shot, int ms)
        {
            ActorWorld &a = *w.actors;
            const int stomp = skillOf(w).gargantuaDmgStomp;
            std::int32_t length = shot.leftMs & kStompLengthMask;
            std::int64_t speed = shot.leftMs >> kStompSpeedShift; // (sixteenths)
            const int framerate = shot.sprite & ~kStompHitPlayer;
            // (deltaTime, a quarter of the time: the game's.)
            speed += static_cast<std::int64_t>(framerate) * 16 * ms / 4000;
            const int faster = framerate + 1500 * ms / 4000;
            const fx step = static_cast<fx>((speed * kFxOne * ms) / (16 * 1000));
            // In its way: the head hull's reach ahead of it, 30 up.
            const fx from[3] = {shot.pos[0], shot.pos[1], shot.pos[2] + fxFromInt(30)};
            const fx reach = step + fxFromInt(16);
            const fx to[3] = {from[0] + fxMul(shot.velocity[0], reach), from[1] + fxMul(shot.velocity[1], reach),
                              from[2] + fxMul(shot.velocity[2], reach)};
            std::uint16_t hit = shot.sprite & kStompHitPlayer;
            if (!hit && player.health > 0)
            {
                const fx tall = player.ducked ? kCrouchedFeet : kStandingFeet;
                const fx lo[3] = {player.origin[0] - fxFromInt(32), player.origin[1] - fxFromInt(32), player.origin[2] - tall - fxFromInt(18)};
                const fx hi[3] = {player.origin[0] + fxFromInt(32), player.origin[1] + fxFromInt(32), player.origin[2] + tall + fxFromInt(18)};
                std::int64_t enter = 0;
                if (lineEntersBox(from, to, lo, hi, enter))
                {
                    playerHurt(player, stomp, shot.pos, kDmgSonic);
                    a.got |= kGotHurt;
                    hit = kStompHitPlayer;
                }
            }
            for (int i = 0; i < a.count; ++i)
            {
                const ActorState &other = a.states[i];
                if (!other.active || other.ai >= kAiDying || !canBeHurt(other.kind) ||
                    other.kind == static_cast<std::uint8_t>(ActorKind::Gargantua))
                    continue;
                fx lo[3], hi[3];
                targetBox(other, lo, hi);
                std::int64_t enter = 0;
                if (lineEntersBox(from, to, lo, hi, enter))
                    monsterHurt(w, i, stomp, false, kHitGeneric, kDmgSonic, kAiNone, shot.pos);
            }
            // On, and the sparks it leaves on the ground under it (two each
            // fortieth of a second in the game's; here one each 32 units it
            // goes, which is as many as there is room for).
            for (int k = 0; k < 3; ++k)
                shot.pos[k] += fxMul(shot.velocity[k], step);
            const std::int32_t was = length;
            length -= fxToInt(step) > 0 ? fxToInt(step) : 0;
            if (was / 32 != length / 32)
            {
                const fx down[3] = {shot.pos[0], shot.pos[1], shot.pos[2] - fxFromInt(500)};
                const Shot ground = traceShot(*w.world, shot.pos, down);
                fx spark[3];
                for (int k = 0; k < 3; ++k)
                    spark[k] = shot.pos[k] + static_cast<fx>((static_cast<std::int64_t>(down[k] - shot.pos[k]) * ground.fraction) >> kFractionShift);
                effectAdd(a, kEffectBurst, spark, nullptr, 24, 300);
            }
            if (length <= 0)
            {
                shot.active = false;
                return;
            }
            const std::int64_t most = (std::int64_t(1) << (31 - kStompSpeedShift)) - 1;
            shot.leftMs = static_cast<std::int32_t>(((speed > most ? most : speed) << kStompSpeedShift) | length);
            shot.sprite = static_cast<std::uint16_t>((faster > 0x7FFF ? 0x7FFF : faster) | hit);
        }

        /// Big momma's mortar (CBMortar), a frame on: thrown, it falls; where
        /// it meets the world, the player or anyone, it bursts, its acid
        /// hurting all about (sk_bigmomma_dmg_blast, _radius_blast).
        inline void mommaSpitTick(LogicWorld &w, PlayerState &player, Projectile &shot, int ms)
        {
            ActorWorld &a = *w.actors;
            shot.leftMs -= ms;
            shot.velocity[2] -= static_cast<fx>((static_cast<std::int64_t>(gravityOf(player)) * ms) / 1000);
            fx to[3];
            for (int i = 0; i < 3; ++i)
                to[i] = shot.pos[i] + static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * ms) / 1000);
            const Shot line = traceShot(*w.world, shot.pos, to);
            std::int64_t reach = line.fraction;
            bool met = line.fraction < kFractionOne;
            if (player.health > 0)
            {
                const fx tall = player.ducked ? kCrouchedFeet : kStandingFeet;
                const fx lo[3] = {player.origin[0] - fxFromInt(16), player.origin[1] - fxFromInt(16), player.origin[2] - tall};
                const fx hi[3] = {player.origin[0] + fxFromInt(16), player.origin[1] + fxFromInt(16), player.origin[2] + tall};
                std::int64_t enter;
                if (lineEntersBox(shot.pos, to, lo, hi, enter) && enter < reach)
                {
                    reach = enter;
                    met = true;
                }
            }
            for (int i = 0; i < a.count; ++i)
            {
                const ActorState &actor = a.states[i];
                if (!actor.active || actor.ai >= kAiDying || !canBeHurt(actor.kind) ||
                    actor.kind == static_cast<std::uint8_t>(ActorKind::BigMomma) ||
                    actor.kind == static_cast<std::uint8_t>(ActorKind::BabyCrab))
                    continue;
                fx lo[3], hi[3];
                targetBox(actor, lo, hi);
                std::int64_t enter;
                if (lineEntersBox(shot.pos, to, lo, hi, enter) && enter < reach)
                {
                    reach = enter;
                    met = true;
                }
            }
            for (int i = 0; i < 3; ++i)
                shot.pos[i] += static_cast<fx>((static_cast<std::int64_t>(to[i] - shot.pos[i]) * reach) >> kFractionShift);
            if (!met && shot.leftMs > 0)
                return;
            shot.active = false;
            effectAdd(a, kEffectSplat, shot.pos, nullptr, 48, 500);
            if (w.heard != nullptr)
                w.heard->add(CommonSound::SpitHit, shot.pos, 127, kRadiusMedium);
            radiusDamage(w, player, shot.pos, skillOf(w).bigmommaDmgBlast, skillOf(w).bigmommaRadiusBlast, -1, kDmgAcid, kAiNone);
        }

        inline void projectilesTick(LogicWorld &w, PlayerState &player, int ms)
        {
            ActorWorld &a = *w.actors;
            for (Effect &effect : a.effects)
            {
                if (effect.kind == 0)
                    continue;
                effect.leftMs = static_cast<std::int16_t>(effect.leftMs - ms);
                if (effect.leftMs <= 0)
                    effect.kind = 0;
            }
            // The pieces of what has come apart: they fall, bounce (losing
            // nearly half their speed each time), turn while they fly, lie
            // where they stop, and are gone when their time is up.
            for (Gib &gib : a.gibs)
            {
                if (!gib.active)
                    continue;
                if ((gib.leftMs -= ms) <= 0)
                {
                    gib.active = false;
                    continue;
                }
                if (gib.resting)
                    continue;
                gib.velocity[2] -= static_cast<fx>((static_cast<std::int64_t>(gravityOf(player)) * ms) / 1000);
                fx to[3];
                for (int i = 0; i < 3; ++i)
                    to[i] = gib.pos[i] + static_cast<fx>((static_cast<std::int64_t>(gib.velocity[i]) * ms) / 1000);
                gib.yaw = static_cast<std::uint16_t>(gib.yaw + gib.spin * ms);
                gib.pitch = static_cast<std::uint16_t>(gib.pitch + (gib.spin / 2) * ms);
                const Trace t = traceAll(*w.world, true, gib.pos, to);
                if (t.startSolid)
                {
                    // Thrown from inside something (a monster hard against a
                    // wall): moved towards the open a little at a time, and
                    // given up if it never gets there.
                    gib.pos[2] += fxFromInt(4);
                    if (gib.leftMs > 600)
                        gib.leftMs -= 2000;
                    continue;
                }
                if (t.fraction < kFractionOne)
                {
                    std::int64_t into = 0;
                    for (int i = 0; i < 3; ++i)
                        into += static_cast<std::int64_t>(gib.velocity[i]) * t.normal[i];
                    into >>= kNormalShift;
                    for (int i = 0; i < 3; ++i)
                    {
                        const fx off = static_cast<fx>((into * t.normal[i] * 2) >> kNormalShift);
                        gib.velocity[i] = ((gib.velocity[i] - off) * 11) / 20;
                        to[i] = t.endPos[i];
                    }
                    gib.spin = static_cast<std::int16_t>(gib.spin / 2);
                    // On something flat enough to lie on, and slow: it lies.
                    if (t.normal[2] > kNormalOne / 10 * 7 && fxLength(gib.velocity) < fxFromInt(60))
                    {
                        gib.resting = true;
                        gib.pitch = 0;
                        // (Where a piece of someone lands, it leaves some.)
                        const bool alien = static_cast<int>(gib.model) == a.alienGibs;
                        if (static_cast<int>(gib.model) == a.humanGibs || alien)
                        {
                            markFloor(a, *w.world, gib.pos, 24, alien ? kDecalAlien2 : kDecalBlood, 13,
                                      alien ? kMarkAlien : kMarkBlood);
                            // (Meat, to whatever eats it: CGib::WaitTillLand.)
                            aiSound(a, kSoundMeat, gib.pos, 384, 25000);
                        }
                    }
                }
                for (int i = 0; i < 3; ++i)
                    gib.pos[i] = to[i];
            }
            for (Projectile &shot : a.shots)
            {
                if (!shot.active)
                    continue;
                if (shot.kind == kShotStomp)
                {
                    stompTick(w, player, shot, ms);
                    continue;
                }
                if (shot.kind == kShotMommaSpit)
                {
                    mommaSpitTick(w, player, shot, ms);
                    continue;
                }
                if (shot.kind == kShotNihilZap || shot.kind == kShotNihilPortal || shot.kind == kShotNihilGreen)
                    continue; // (the nihilanth's own: HlAliens.hpp)
                // (An armed mine keeps its beam's length there, not a time.)
                if (!(shot.kind == kShotMine && shot.sprite == 1))
                    shot.leftMs -= ms;
                const bool gib = shot.kind == kShotGib;
                if (shot.kind == kShotBolt)
                {
                    // In a wall: there a while, and then gone.
                    if (shot.sprite == 1)
                    {
                        if (shot.leftMs <= 0)
                            shot.active = false;
                        continue;
                    }
                    fx to[3];
                    for (int i = 0; i < 3; ++i)
                        to[i] = shot.pos[i] + static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * ms) / 1000);
                    const Shot line = traceShot(*w.world, shot.pos, to);
                    std::int64_t reach = line.fraction;
                    int struck = -1;
                    for (int i = 0; i < a.count; ++i)
                    {
                        const ActorState &actor = a.states[i];
                        if (!actor.active || !canBeHurt(actor.kind) || actor.ai >= kAiDying)
                            continue;
                        fx mins[3], maxs[3];
                        targetBox(actor, mins, maxs);
                        std::int64_t enter;
                        if (lineEntersBox(shot.pos, to, mins, maxs, enter) && enter < reach)
                        {
                            reach = enter;
                            struck = i;
                        }
                    }
                    for (int i = 0; i < 3; ++i)
                        shot.pos[i] += static_cast<fx>((static_cast<std::int64_t>(to[i] - shot.pos[i]) * reach) >>
                                                       kFractionShift);
                    if (struck >= 0)
                    {
                        // Into flesh (the game's: never in pieces).
                        shot.active = false;
                        monsterHurt(w, struck, kBoltDamage, false);
                        if (w.heard != nullptr)
                            w.heard->add(CommonSound::XbowHitBody, shot.pos, 127, kRadiusMedium);
                    }
                    else if (line.fraction < kFractionOne)
                    {
                        // Into the wall, and stays there.
                        shot.sprite = 1;
                        shot.leftMs = 10000;
                        if (w.heard != nullptr)
                            w.heard->add(CommonSound::XbowHit, shot.pos, 127, kRadiusMedium);
                    }
                    else if (shot.leftMs <= 0)
                    {
                        shot.active = false;
                    }
                    continue;
                }
                if (shot.kind == kShotMine)
                {
                    const fx end[3] = {shot.pos[0] + shot.velocity[0] * kMineRange,
                                       shot.pos[1] + shot.velocity[1] * kMineRange,
                                       shot.pos[2] + shot.velocity[2] * kMineRange};
                    if (shot.sprite == 2)
                    {
                        // Set off: it goes a moment after.
                        if (shot.leftMs > 0)
                            continue;
                        shot.active = false;
                        const fx at[3] = {shot.pos[0] + shot.velocity[0] * 8, shot.pos[1] + shot.velocity[1] * 8,
                                          shot.pos[2] + shot.velocity[2] * 8};
                        explodeAt(w, player, at, kMineBlast, true, true, true);
                        continue;
                    }
                    if (shot.sprite == 0)
                    {
                        // Charging: then its beam comes on, as long as it is.
                        if (shot.leftMs > 0)
                            continue;
                        shot.sprite = 1;
                        shot.leftMs = beamReach(w, player, shot.pos, end, -1);
                        if (w.heard != nullptr)
                            w.heard->add(CommonSound::MineActivate, shot.pos, 127, kRadiusMedium);
                        continue;
                    }
                    // Armed: anything across its beam, and it goes.
                    const std::int32_t reach = beamReach(w, player, shot.pos, end, -1);
                    const std::int32_t off = reach > shot.leftMs ? reach - shot.leftMs : shot.leftMs - reach;
                    if (off > kFractionOne / 1000)
                    {
                        shot.sprite = 2;
                        shot.leftMs = 100 + static_cast<int>(chance(a) % 200);
                    }
                    continue;
                }
                if (shot.kind == kShotSnark || shot.kind == kShotSnarkFoe)
                {
                    // (A monstermaker's is after the player and their side
                    // from the first; the player's, after what fights.)
                    const bool foe = shot.kind == kShotSnarkFoe;
                    // Its time is up: it bursts, the harder for each bite.
                    if (shot.leftMs <= 0)
                    {
                        shot.active = false;
                        const int damage = kSnarkPop + kSnarkPop * snarkBites(shot);
                        effectAdd(a, kEffectSplat, shot.pos, nullptr, 16, 300);
                        if (w.heard != nullptr)
                        {
                            w.heard->add(CommonSound::SnarkBlast, shot.pos, 127, kRadiusMedium);
                            // (... and the game's splat with it, high.)
                            w.heard->add(CommonSound::BodySplat, shot.pos, 95, kRadiusMedium, 200);
                        }
                        radiusDamage(w, player, shot.pos, damage, damage * 5 / 2, -1);
                        continue;
                    }
                    // (Its wait between bites, in hundredths of a second.)
                    int wait = shot.sprite >> 8;
                    wait = wait > ms / 10 ? wait - ms / 10 : 0;
                    // What it is after: the nearest that fights within 512 that
                    // it can see; or, a moment after it was let go and with
                    // nothing else, the player.
                    int target = -1;
                    fx goal[3] = {0, 0, 0};
                    std::int64_t best = static_cast<std::int64_t>(fxFromInt(512)) * fxFromInt(512);
                    for (int i = 0; i < a.count; ++i)
                    {
                        const ActorState &actor = a.states[i];
                        if (!actor.active || actor.ai >= kAiDying ||
                            (foe ? !(isPerson(actor.kind) || isGrunt(actor.kind))
                                 : (!fights(actor.kind) || isMine(actor.kind) || isTurret(actor.kind))))
                            continue;
                        const fx d[3] = {actor.pos[0] - shot.pos[0], actor.pos[1] - shot.pos[1],
                                         actor.pos[2] + fxFromInt(16) - shot.pos[2]};
                        const std::int64_t far = static_cast<std::int64_t>(fxLength(d)) * fxLength(d);
                        if (far >= best)
                            continue;
                        const fx at[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(16)};
                        if (traceShot(*w.world, shot.pos, at).fraction < kFractionOne)
                            continue;
                        best = far;
                        target = i;
                        for (int k = 0; k < 3; ++k)
                            goal[k] = at[k];
                    }
                    bool atPlayer = false;
                    if ((target < 0 || foe) && (foe || shot.leftMs < 13500) && player.health > 0 && !unseen(player))
                    {
                        const fx d[3] = {player.origin[0] - shot.pos[0], player.origin[1] - shot.pos[1],
                                         player.origin[2] - shot.pos[2]};
                        if (static_cast<std::int64_t>(fxLength(d)) * fxLength(d) < best &&
                            traceShot(*w.world, shot.pos, player.origin).fraction >= kFractionOne)
                        {
                            atPlayer = true;
                            target = -1; // (the nearer of the two)
                            for (int k = 0; k < 3; ++k)
                                goal[k] = player.origin[k];
                        }
                    }
                    // A bite, if it is on what it is after.
                    if (wait == 0 && (target >= 0 || atPlayer))
                    {
                        fx mins[3], maxs[3];
                        bool on = false;
                        if (target >= 0)
                        {
                            // (On it: as near as it can come, its middle being
                            // a crouched hull's half width off what it runs
                            // into, and that the monster's solid box.)
                            targetBox(a.states[target], mins, maxs);
                            for (int k = 0; k < 3; ++k)
                            {
                                mins[k] -= fxFromInt(40);
                                maxs[k] += fxFromInt(40);
                            }
                            on = shot.pos[0] >= mins[0] && shot.pos[0] <= maxs[0] && shot.pos[1] >= mins[1] &&
                                 shot.pos[1] <= maxs[1] && shot.pos[2] >= mins[2] && shot.pos[2] <= maxs[2];
                        }
                        else
                        {
                            on = playerTouchesBox(player, shot.pos, shot.pos, fxFromInt(12));
                        }
                        if (on)
                        {
                            if (target >= 0)
                                monsterHurt(w, target, kSnarkBite, false);
                            else
                            {
                                playerHurt(player, kSnarkBite, shot.pos, kDmgSlash);
                                a.got |= kGotHurt;
                            }
                            if (w.heard != nullptr)
                                w.heard->add(CommonSound::SnarkDeploy, shot.pos, 127, kRadiusMedium);
                            const int bites = snarkBites(shot) < 255 ? snarkBites(shot) + 1 : 255;
                            wait = 50; // (half a second)
                            shot.sprite = static_cast<std::uint16_t>(bites | (wait << 8));
                            for (int k = 0; k < 3; ++k)
                                shot.velocity[k] = -shot.velocity[k] / 3;
                            continue;
                        }
                    }
                    shot.sprite = static_cast<std::uint16_t>(snarkBites(shot) | (wait << 8));
                    // It falls at half the world's pull, and where it lands it
                    // hops at what it is after.
                    shot.velocity[2] -= static_cast<fx>((static_cast<std::int64_t>(gravityOf(player) / 2) * ms) / 1000);
                    fx to[3];
                    for (int i = 0; i < 3; ++i)
                        to[i] = shot.pos[i] + static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * ms) / 1000);
                    // (A monstermaker's goes as the game's does: a few units
                    // across, a point -- out of the vents a person's box never
                    // fits; the player's, as a crouching person's box.)
                    const Trace t = foe ? detail::pointTrace(*w.world, shot.pos, to) : traceAll(*w.world, true, shot.pos, to);
                    if (!t.startSolid && t.fraction < kFractionOne)
                    {
                        for (int i = 0; i < 3; ++i)
                            to[i] = t.endPos[i];
                        if (t.normal[2] > kNormalOne / 10 * 7 && (target >= 0 || atPlayer))
                        {
                            // The game's hop: what it had, less the faster it
                            // went, and 300 at what it is after.
                            fx dir[3] = {goal[0] - to[0], goal[1] - to[1], goal[2] - to[2]};
                            const fx far = fxLength(dir);
                            const fx speed = fxLength(shot.velocity);
                            const int adjust = static_cast<int>((static_cast<std::int64_t>(fxFromInt(50)) * 1024) /
                                                                (speed + fxFromInt(10)));
                            const int keep = adjust > 1228 ? 1228 : adjust; // (1.2, in 1024ths)
                            for (int i = 0; i < 3; ++i)
                            {
                                const fx unit = far > 0 ? static_cast<fx>((static_cast<std::int64_t>(dir[i]) << kFxShift) / far) : 0;
                                shot.velocity[i] = static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * keep) >> 10) +
                                                   unit * 300;
                            }
                            shot.velocity[2] += fxFromInt(120);
                            if (w.heard != nullptr && (chance(a) & 3) == 0)
                            {
                                // (Higher the nearer its end.)
                                const int pitch = 155 - shot.leftMs * 75 / 15000;
                                const int which = static_cast<int>(chance(a) % 3);
                                w.heard->add(which == 0 ? CommonSound::SnarkHunt1
                                                        : (which == 1 ? CommonSound::SnarkHunt2 : CommonSound::SnarkHunt3),
                                             shot.pos, 100, kRadiusMedium, pitch);
                            }
                        }
                        else
                        {
                            // Off a wall, or with nothing to go for: a bounce.
                            std::int64_t into = 0;
                            for (int i = 0; i < 3; ++i)
                                into += static_cast<std::int64_t>(shot.velocity[i]) * t.normal[i];
                            into >>= kNormalShift;
                            for (int i = 0; i < 3; ++i)
                            {
                                const fx off = static_cast<fx>((into * t.normal[i] * 2) >> kNormalShift);
                                shot.velocity[i] = ((shot.velocity[i] - off) * 2) / 3;
                            }
                        }
                    }
                    for (int i = 0; i < 3; ++i)
                        shot.pos[i] = t.startSolid ? shot.pos[i] : to[i];
                    continue;
                }
                if (shot.kind == kShotRocket || shot.kind == kShotRocketFoe || shot.kind == kShotHornet ||
                    shot.kind == kShotHornetFoe)
                {
                    const bool rocket = shot.kind == kShotRocket || shot.kind == kShotRocketFoe;
                    // (An alien grunt's: always homing, at 600 -- 800 for
                    // the orange kind, `sprite` 2 -- after the player and
                    // their side, never the aliens.)
                    const bool foe = shot.kind == kShotHornetFoe;
                    auto prey = [&](const ActorState &actor)
                    {
                        return actor.active && actor.ai < kAiDying &&
                               (foe ? (isPerson(actor.kind) || isGrunt(actor.kind))
                                    : (fights(actor.kind) && !isTurret(actor.kind) && !isMine(actor.kind)));
                    };
                    // The rocket: a moment at half the world's pull, then lit
                    // -- and faster, up to 2000, along where it goes. (Its
                    // steering is the RPG's, while it is in hand: weaponsTick.)
                    if (rocket)
                    {
                        if (shot.sprite == 0)
                        {
                            shot.velocity[2] -= static_cast<fx>((static_cast<std::int64_t>(gravityOf(player) / 2) * ms) / 1000);
                            if (shot.leftMs <= 30000 - 400)
                            {
                                shot.sprite = 1;
                                if (w.heard != nullptr)
                                    w.heard->add(CommonSound::RocketFly, shot.pos, 110, kRadiusMedium);
                            }
                        }
                        else
                        {
                            const fx speed = fxLength(shot.velocity);
                            if (speed > 0 && speed < fxFromInt(2000))
                            {
                                const fx faster = speed + static_cast<fx>((static_cast<std::int64_t>(fxFromInt(1500)) * ms) / 1000);
                                const fx to = faster > fxFromInt(2000) ? fxFromInt(2000) : faster;
                                for (int i = 0; i < 3; ++i)
                                    shot.velocity[i] = static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * to) / speed);
                            }
                        }
                    }
                    else if (shot.sprite == 0 || foe)
                    {
                        // A homing hornet: after the nearest that fights within
                        // 512 that it can see, at 600; and buzzing.
                        int best = -1;
                        const fx sight = fxFromInt(foe ? 1024 : 512);
                        std::int64_t nearest = static_cast<std::int64_t>(sight) * sight;
                        fx goal[3] = {0, 0, 0};
                        if (foe && player.health > 0 && !unseen(player))
                        {
                            const fx d[3] = {player.origin[0] - shot.pos[0], player.origin[1] - shot.pos[1],
                                             player.origin[2] - shot.pos[2]};
                            const std::int64_t far = static_cast<std::int64_t>(fxLength(d)) * fxLength(d);
                            if (far < nearest && traceShot(*w.world, shot.pos, player.origin).fraction >= kFractionOne)
                            {
                                nearest = far;
                                best = a.count; // (the player)
                                for (int k = 0; k < 3; ++k)
                                    goal[k] = player.origin[k];
                            }
                        }
                        for (int i = 0; i < a.count; ++i)
                        {
                            const ActorState &actor = a.states[i];
                            if (!prey(actor))
                                continue;
                            const fx at[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(24)};
                            const fx d[3] = {at[0] - shot.pos[0], at[1] - shot.pos[1], at[2] - shot.pos[2]};
                            const std::int64_t far = static_cast<std::int64_t>(fxLength(d)) * fxLength(d);
                            if (far >= nearest || traceShot(*w.world, shot.pos, at).fraction < kFractionOne)
                                continue;
                            nearest = far;
                            best = i;
                            for (int k = 0; k < 3; ++k)
                                goal[k] = at[k];
                        }
                        if (best >= 0)
                        {
                            const fx d[3] = {goal[0] - shot.pos[0], goal[1] - shot.pos[1], goal[2] - shot.pos[2]};
                            const fx far = fxLength(d);
                            // (Turned most of the way a tenth of a second.)
                            const int keep = ms >= 100 ? 0 : 1024 - (ms * 819) / 100;
                            const int speed = foe && shot.sprite == 2 ? 800 : 600;
                            for (int i = 0; far > 0 && i < 3; ++i)
                            {
                                const fx want = static_cast<fx>((static_cast<std::int64_t>(d[i]) * speed) / (far >> kFxShift > 0 ? far >> kFxShift : 1));
                                shot.velocity[i] = static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * keep +
                                                                    static_cast<std::int64_t>(want) * (1024 - keep)) >> 10);
                            }
                        }
                        if (w.heard != nullptr && (chance(a) % 40) == 0)
                            w.heard->add(static_cast<CommonSound>(static_cast<int>(CommonSound::HornetBuzz1) + static_cast<int>(chance(a) % 3)),
                                         shot.pos, 70, kRadiusSmall);
                    }
                    fx to[3];
                    for (int i = 0; i < 3; ++i)
                        to[i] = shot.pos[i] + static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * ms) / 1000);
                    const Shot line = traceShot(*w.world, shot.pos, to);
                    std::int64_t reach = line.fraction;
                    int struck = -1;
                    for (int i = 0; i < a.count; ++i)
                    {
                        const ActorState &actor = a.states[i];
                        if (!actor.active || !canBeHurt(actor.kind) || actor.ai >= kAiDying ||
                            (foe && !(isPerson(actor.kind) || isGrunt(actor.kind))))
                            continue;
                        fx mins[3], maxs[3];
                        targetBox(actor, mins, maxs);
                        std::int64_t enter;
                        if (lineEntersBox(shot.pos, to, mins, maxs, enter) && enter < reach)
                        {
                            reach = enter;
                            struck = i;
                        }
                    }
                    if (shot.kind == kShotRocketFoe && player.health > 0)
                    {
                        // (A tank's: the player is in its way too.)
                        const fx tall = player.ducked ? kCrouchedFeet : kStandingFeet;
                        const fx lo[3] = {player.origin[0] - fxFromInt(16), player.origin[1] - fxFromInt(16), player.origin[2] - tall};
                        const fx hi[3] = {player.origin[0] + fxFromInt(16), player.origin[1] + fxFromInt(16), player.origin[2] + tall};
                        std::int64_t enter;
                        if (lineEntersBox(shot.pos, to, lo, hi, enter) && enter < reach)
                        {
                            reach = enter;
                            struck = a.count; // (the player: no actor of that number)
                        }
                    }
                    if (foe && player.health > 0 && playerTouchesBox(player, to, to, fxFromInt(4)))
                    {
                        // (The player stung: hornet.cpp's monDmgHornet.)
                        shot.active = false;
                        playerHurt(player, skillOf(w).hornetDmg, shot.pos, kDmgBullet);
                        a.got |= kGotHurt;
                        if (w.heard != nullptr)
                            w.heard->add(static_cast<CommonSound>(static_cast<int>(CommonSound::HornetHit1) + static_cast<int>(chance(a) % 3)),
                                         shot.pos, 100, kRadiusSmall);
                        continue;
                    }
                    for (int i = 0; i < 3; ++i)
                        shot.pos[i] += static_cast<fx>((static_cast<std::int64_t>(to[i] - shot.pos[i]) * reach) >> kFractionShift);
                    const bool met = struck >= 0 || line.fraction < kFractionOne;
                    if (rocket && (met || shot.leftMs <= 0))
                    {
                        // Off, where it met something: 100, to 250 units.
                        shot.active = false;
                        explodeAt(w, player, shot.pos, 100, true, true, true);
                        continue;
                    }
                    if (!rocket && struck >= 0)
                    {
                        // A sting.
                        shot.active = false;
                        monsterHurt(w, struck, foe ? skillOf(w).hornetDmg : 7, false); // (plrDmgHornet: 7 at every level)
                        if (w.heard != nullptr)
                            w.heard->add(static_cast<CommonSound>(static_cast<int>(CommonSound::HornetHit1) + static_cast<int>(chance(a) % 3)),
                                         shot.pos, 100, kRadiusSmall);
                        continue;
                    }
                    if (!rocket && (line.fraction < kFractionOne || shot.leftMs <= 0))
                        shot.active = false; // (into a wall, or its time up)
                    continue;
                }
                if (shot.kind == kShotHeadBall || shot.kind == kShotZapBall)
                {
                    // A controller's (HlAliens.hpp): after the player and
                    // their side -- the nearest, within 1024, that it can see.
                    const bool head = shot.kind == kShotHeadBall;
                    int target = -1; // (a.count: the player)
                    fx goal[3] = {0, 0, 0};
                    std::int64_t nearest = static_cast<std::int64_t>(fxFromInt(1024)) * fxFromInt(1024);
                    if (player.health > 0 && !unseen(player))
                    {
                        const fx d[3] = {player.origin[0] - shot.pos[0], player.origin[1] - shot.pos[1],
                                         player.origin[2] - shot.pos[2]};
                        const std::int64_t far = static_cast<std::int64_t>(fxLength(d)) * fxLength(d);
                        if (far < nearest)
                        {
                            nearest = far;
                            target = a.count;
                            for (int k = 0; k < 3; ++k)
                                goal[k] = player.origin[k];
                        }
                    }
                    for (int i = 0; head && i < a.count; ++i)
                    {
                        const ActorState &actor = a.states[i];
                        if (!actor.active || actor.ai >= kAiDying || !(isPerson(actor.kind) || isGrunt(actor.kind)))
                            continue;
                        const fx at[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36)};
                        const fx d[3] = {at[0] - shot.pos[0], at[1] - shot.pos[1], at[2] - shot.pos[2]};
                        const std::int64_t far = static_cast<std::int64_t>(fxLength(d)) * fxLength(d);
                        if (far >= nearest)
                            continue;
                        nearest = far;
                        target = i;
                        for (int k = 0; k < 3; ++k)
                            goal[k] = at[k];
                    }
                    auto jolt = [&](int whom, int damage, std::uint32_t type)
                    {
                        if (whom == a.count)
                        {
                            playerHurt(player, damage, shot.pos, type);
                            a.got |= kGotHurt;
                        }
                        else if (whom >= 0)
                        {
                            monsterHurt(w, whom, damage, false, kHitGeneric, type, kAiNone, shot.pos);
                        }
                        if (w.heard != nullptr)
                            w.heard->add(CommonSound::Electro4, shot.pos, head ? 64 : 40, kRadiusSmall);
                    };
                    if (head)
                    {
                        // MovetoTarget: what it had, at most 400, and a
                        // hundred more towards what it is after, ten times a
                        // second; within 64, a jolt, and it is done.
                        if (target >= 0)
                        {
                            const fx d[3] = {goal[0] - shot.pos[0], goal[1] - shot.pos[1], goal[2] - shot.pos[2]};
                            const fx far = fxLength(d);
                            if (far < fxFromInt(64))
                            {
                                effectAdd(a, kEffectBolt, shot.pos, goal, 0, 300);
                                jolt(target, skillOf(w).controllerDmgzap, kDmgShock);
                                shot.active = false;
                                continue;
                            }
                            const fx speed = fxLength(shot.velocity);
                            if (speed > fxFromInt(400))
                                for (int k = 0; k < 3; ++k)
                                    shot.velocity[k] = static_cast<fx>((static_cast<std::int64_t>(shot.velocity[k]) * fxFromInt(400)) / speed);
                            for (int k = 0; far > 0 && k < 3; ++k)
                                shot.velocity[k] += static_cast<fx>((static_cast<std::int64_t>(d[k]) * 100 * ms) / (100 * (far >> kFxShift > 0 ? far >> kFxShift : 1)));
                        }
                        else if (shot.leftMs > 300)
                        {
                            shot.leftMs = 300; // (nothing to go for: it fades)
                        }
                    }
                    if (shot.leftMs <= 0)
                    {
                        shot.active = false;
                        continue;
                    }
                    fx to[3];
                    for (int i = 0; i < 3; ++i)
                        to[i] = shot.pos[i] + static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * ms) / 1000);
                    if (!head)
                    {
                        // ExplodeTouch: what it runs into, a little hurt.
                        int struck = -1;
                        const Shot line = traceShot(*w.world, shot.pos, to);
                        std::int64_t reach = line.fraction;
                        for (int i = 0; i < a.count; ++i)
                        {
                            const ActorState &actor = a.states[i];
                            if (!actor.active || actor.ai >= kAiDying || !(isPerson(actor.kind) || isGrunt(actor.kind)))
                                continue;
                            fx mins[3], maxs[3];
                            targetBox(actor, mins, maxs);
                            std::int64_t enter;
                            if (lineEntersBox(shot.pos, to, mins, maxs, enter) && enter < reach)
                            {
                                reach = enter;
                                struck = i;
                            }
                        }
                        if (player.health > 0 && playerTouchesBox(player, to, to, fxFromInt(2)))
                            struck = a.count;
                        if (struck >= 0 || line.fraction < kFractionOne)
                        {
                            if (struck >= 0)
                                jolt(struck, skillOf(w).controllerDmgball, kDmgEnergyBeam);
                            shot.active = false;
                            continue;
                        }
                        for (int i = 0; i < 3; ++i)
                            shot.pos[i] = to[i];
                        continue;
                    }
                    // (The head ball off a wall: BounceTouch.)
                    const Trace t = detail::pointTrace(*w.world, shot.pos, to);
                    if (t.fraction < kFractionOne && t.hit)
                    {
                        std::int64_t into = 0;
                        for (int i = 0; i < 3; ++i)
                            into += static_cast<std::int64_t>(shot.velocity[i]) * t.normal[i];
                        into >>= kNormalShift;
                        for (int i = 0; i < 3; ++i)
                            shot.velocity[i] -= static_cast<fx>((into * t.normal[i] * 2) >> kNormalShift);
                    }
                    for (int i = 0; i < 3; ++i)
                        shot.pos[i] = t.endPos[i];
                    continue;
                }
                if (shot.kind == kShotContact)
                {
                    // A launcher's grenade: an arc at half gravity, and off
                    // at the first thing it meets -- the world, or anything
                    // that fights, or (a soldier's) the player.
                    shot.velocity[2] -= static_cast<fx>((static_cast<std::int64_t>(gravityOf(player) / 2) * ms) / 1000);
                    fx to[3];
                    for (int i = 0; i < 3; ++i)
                        to[i] = shot.pos[i] + static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * ms) / 1000);
                    const Shot line = traceShot(*w.world, shot.pos, to);
                    std::int64_t reach = line.fraction;
                    bool met = line.fraction < kFractionOne || shot.leftMs <= 0;
                    for (int i = 0; i < a.count; ++i)
                    {
                        const ActorState &actor = a.states[i];
                        if (!actor.active || !fights(actor.kind) || actor.ai >= kAiDying)
                            continue;
                        fx mins[3], maxs[3];
                        targetBox(actor, mins, maxs);
                        std::int64_t enter;
                        if (lineEntersBox(shot.pos, to, mins, maxs, enter) && enter < reach && shot.leftMs < 9900)
                        {
                            reach = enter;
                            met = true;
                        }
                    }
                    if (shot.sprite == 1 && player.health > 0 && playerTouchesBox(player, to, to, fxFromInt(8)))
                        met = true;
                    for (int i = 0; i < 3; ++i)
                        shot.pos[i] += static_cast<fx>((static_cast<std::int64_t>(to[i] - shot.pos[i]) * reach) >>
                                                       kFractionShift);
                    if (met)
                    {
                        shot.active = false;
                        explodeAt(w, player, shot.pos, kGrenadeBlast, true, true, true);
                    }
                    else if ((shot.leftMs + ms) / 200 != shot.leftMs / 200)
                    {
                        // (Danger, five times a second, half a second ahead,
                        // as loud as it is fast: DangerSoundThink.)
                        const fx ahead[3] = {shot.pos[0] + shot.velocity[0] / 2, shot.pos[1] + shot.velocity[1] / 2,
                                             shot.pos[2] + shot.velocity[2] / 2};
                        aiSound(a, kSoundDanger, ahead, fxToInt(fxLength(shot.velocity)), 200);
                    }
                    continue;
                }
                const bool satchel = shot.kind == kShotSatchel;
                const bool grenade = shot.kind == kShotGrenade || gib || satchel;
                if (grenade)
                    shot.velocity[2] -= static_cast<fx>((static_cast<std::int64_t>(gravityOf(player) / 2) * ms) / 1000);
                fx to[3];
                for (int i = 0; i < 3; ++i)
                    to[i] = shot.pos[i] + static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * ms) / 1000);
                if (grenade)
                {
                    // It bounces: off what a crouched person could not pass
                    // (near enough for something the size of a fist), losing
                    // a third of its speed each time, until its fuse is out.
                    const Trace t = traceAll(*w.world, true, shot.pos, to);
                    if (!t.startSolid && t.fraction < kFractionOne)
                    {
                        std::int64_t into = 0;
                        for (int i = 0; i < 3; ++i)
                            into += static_cast<std::int64_t>(shot.velocity[i]) * t.normal[i];
                        into >>= kNormalShift;
                        for (int i = 0; i < 3; ++i)
                        {
                            const fx off = static_cast<fx>((into * t.normal[i] * 2) >> kNormalShift);
                            shot.velocity[i] = ((shot.velocity[i] - off) * 2) / 3;
                            to[i] = t.endPos[i];
                        }
                        if (!gib && w.heard != nullptr && fxLength(shot.velocity) > fxFromInt(60))
                        {
                            // (One of its three knocks, and quiet: the
                            // game's are a quarter of full.)
                            w.actors->seed = w.actors->seed * 1664525u + 1013904223u;
                            const int which = static_cast<int>((w.actors->seed >> 12) % 3);
                            w.heard->add(satchel      ? CommonSound::SatchelBounce
                                         : which == 0 ? CommonSound::GrenadeBounce
                                                      : (which == 1 ? CommonSound::GrenadeBounce2 : CommonSound::GrenadeBounce3),
                                         shot.pos, 48, kRadiusMedium);
                        }
                    }
                    for (int i = 0; i < 3; ++i)
                        shot.pos[i] = t.startSolid ? shot.pos[i] : to[i];
                    if (shot.kind == kShotGrenade && shot.leftMs > 0)
                    {
                        // What the monsters hear of it: once, louder than its
                        // blast, as soon as it all but stops; and ten times a
                        // second in its last second, where it will be
                        // (TumbleThink, BounceTouch).
                        const fx slow[3] = {shot.velocity[0], shot.velocity[1], shot.velocity[2] * 45 / 100};
                        if (shot.sprite == 0xFFFF && fxLength(slow) <= fxFromInt(60))
                        {
                            shot.sprite = 0xFFFE;
                            aiSound(a, kSoundDanger, shot.pos, kGrenadeBlast * 10 / 4, 300);
                        }
                        if (shot.leftMs < 1000 && (shot.leftMs + ms) / 100 != shot.leftMs / 100)
                        {
                            fx ahead[3];
                            for (int i = 0; i < 3; ++i)
                                ahead[i] = shot.pos[i] +
                                           static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * shot.leftMs) / 1000);
                            aiSound(a, kSoundDanger, ahead, 400, 100);
                        }
                    }
                    if (satchel)
                    {
                        // A satchel goes off when it is told to (the radio):
                        // 150, to 375 units.
                        if (shot.sprite == 1)
                        {
                            shot.active = false;
                            explodeAt(w, player, shot.pos, kSatchelBlast, true, true, true);
                        }
                        continue;
                    }
                    if (shot.leftMs <= 0)
                    {
                        shot.active = false;
                        if (gib)
                            continue; // (what was thrown out is simply gone)
                        effectAdd(a, kEffectBurst, shot.pos, nullptr, 96, 450);
                        markBlast(a, *w.world, shot.pos, 48);
                        aiSound(a, kSoundCombat, shot.pos, kNormalExplosionVolume, 3000);
                        if (w.heard != nullptr)
                            w.heard->add(CommonSound::Explode, shot.pos, 127, 2000);
                        radiusDamage(w, player, shot.pos, kGrenadeBlast, kGrenadeRadius, -1);
                        // (Felt, near to.)
                        const fx d[3] = {player.origin[0] - shot.pos[0], player.origin[1] - shot.pos[1],
                                         player.origin[2] - shot.pos[2]};
                        const int far = fxToInt(fxLength(d));
                        if (far < 500)
                            shakeStart(w, fxFromInt(5) * (500 - far) / 500, 450);
                    }
                    continue;
                }
                // Spit: straight on until it meets the world or the player.
                const Shot line = traceShot(*w.world, shot.pos, to);
                fx end[3];
                for (int i = 0; i < 3; ++i)
                    end[i] = shot.pos[i] + static_cast<fx>((static_cast<std::int64_t>(to[i] - shot.pos[i]) *
                                                            line.fraction) >> kFractionShift);
                // (The player is a box; the spit a point taken at the middle
                // and the end of this tick's flight.)
                const fx middle[3] = {(shot.pos[0] + end[0]) / 2, (shot.pos[1] + end[1]) / 2,
                                      (shot.pos[2] + end[2]) / 2};
                bool hits = player.health > 0 &&
                            (playerTouchesBox(player, middle, middle, fxFromInt(4)) ||
                             playerTouchesBox(player, end, end, fxFromInt(4)));
                if (hits)
                {
                    playerHurt(player, skillOf(w).bullsquidDmgSpit, shot.pos);
                    a.got |= kGotHurt;
                }
                else
                {
                    // ... or anything else that can be hurt (CSquidSpit::Touch),
                    // but whoever spat it.
                    for (int i = 0; i < a.count && !hits; ++i)
                    {
                        const ActorState &other = a.states[i];
                        if (i == shot.sprite || !other.active || !(other.flags & kActorSolid) || !canBeHurt(other.kind))
                            continue;
                        fx lo[3], hi[3];
                        targetBox(other, lo, hi);
                        std::int64_t enter = 0;
                        if (lineEntersBox(shot.pos, end, lo, hi, enter))
                        {
                            hits = true;
                            monsterHurt(w, i, skillOf(w).bullsquidDmgSpit, false, kHitBody, kDmgGeneric,
                                        shot.sprite < a.count ? static_cast<std::int16_t>(shot.sprite) : kAiNone,
                                        shot.pos);
                        }
                    }
                }
                if (hits || line.fraction < kFractionOne || shot.leftMs <= 0)
                {
                    shot.active = false;
                    effectAdd(a, kEffectSplat, end, nullptr, 12, 200);
                    if (w.heard != nullptr)
                        w.heard->add(CommonSound::SpitHit, end, 110, kRadiusMedium);
                    continue;
                }
                for (int i = 0; i < 3; ++i)
                    shot.pos[i] = end[i];
            }
        }
    } // namespace detail


    // ------------------------------------------------------------- turrets
    //
    // The guns that mind a place: the game's turret.cpp, think for think
    // (ten times a second). Folded away until one sees someone (if it is the
    // kind that looks) or is switched on by name; then out, sweeping round
    // and pinging; on whoever it sees, turning to them and firing a round a
    // think while they are within thirty degrees of where it points; folded
    // away again when it has seen nobody for a while. Angles are 65536 to
    // the circle.

    inline constexpr std::uint8_t kTurretOff = kAiIdle;
    inline constexpr std::uint8_t kTurretDeploy = 1;
    inline constexpr std::uint8_t kTurretSearch = 2;
    inline constexpr std::uint8_t kTurretActive = 3;
    inline constexpr std::uint8_t kTurretRetire = 4;

    /// What each kind of turret is, in numbers: the game's, at "normal".
    struct TurretKind
    {
        int health;
        int half;     ///< of its box, across
        int offHigh;  ///< ... and up and down from where it is fixed, folded away
        int onHigh;   ///< ... and out
        int eye;      ///< how far its gun is from where it is fixed
        int minPitch; ///< how far down it can point, in degrees (a negative number)
        int maxSpinMs; ///< how long its barrels turn with nobody to fire at (0: for good)
        int maxWaitMs; ///< how long it looks for someone before it folds away (0: for good)
        int damage;   ///< of a round
        CommonSound fire;
        bool spinsUp; ///< its barrels have to get going before it fires
        bool tough;   ///< armoured; shrugs off a tenth while folded away; goes wild when nearly done for
    };

    inline const TurretKind &turretKind(std::uint8_t kind)
    {
        static const TurretKind kMini = {40, 16, 16, 32, 13, -15, 0, 15000, 5, CommonSound::SentryFire, false, true};
        static const TurretKind kSentry = {40, 16, 64, 64, 48, -60, 0, 0, 4, CommonSound::SentryFire, false, false};
        static const TurretKind kBig = {50, 32, 16, 32, 13, -15, 5000, 15000, 10, CommonSound::TurretFire, true, true};
        if (kind == static_cast<std::uint8_t>(ActorKind::Sentry))
            return kSentry;
        return kind == static_cast<std::uint8_t>(ActorKind::Turret) ? kBig : kMini;
    }

    inline constexpr int kTurretRange = 1200;
    inline constexpr int kTurretTurnRate = 30; ///< degrees a second, to begin with
    inline constexpr std::int32_t kDegree = 65536 / 360;

    /// The box a shot at a turret is tried against (and a sentry's touch).
    inline void turretBox(const ActorState &actor, fx *mins, fx *maxs)
    {
        const TurretKind &kind = turretKind(actor.kind);
        const fx high = fxFromInt(actor.ai == kTurretOff ? kind.offHigh : kind.onHigh);
        for (int i = 0; i < 2; ++i)
        {
            mins[i] = actor.pos[i] - fxFromInt(kind.half);
            maxs[i] = actor.pos[i] + fxFromInt(kind.half);
        }
        mins[2] = actor.pos[2] - high;
        maxs[2] = actor.pos[2] + high;
    }

    /// Where a line from `from` to `to` enters the box, as a fraction of the
    /// way (Q30); false if it does not.
    inline bool lineEntersBox(const fx *from, const fx *to, const fx *mins, const fx *maxs,
                              std::int64_t &enter)
    {
        std::int64_t leave = kFractionOne;
        enter = 0;
        for (int k = 0; k < 3; ++k)
        {
            const fx d = to[k] - from[k];
            if (d == 0)
            {
                if (from[k] < mins[k] || from[k] > maxs[k])
                    return false;
                continue;
            }
            std::int64_t t0 = (static_cast<std::int64_t>(mins[k] - from[k]) << kFractionShift) / d;
            std::int64_t t1 = (static_cast<std::int64_t>(maxs[k] - from[k]) << kFractionShift) / d;
            if (t0 > t1)
            {
                const std::int64_t swap = t0;
                t0 = t1;
                t1 = swap;
            }
            enter = t0 > enter ? t0 : enter;
            leave = t1 < leave ? t1 : leave;
            if (enter > leave)
                return false;
        }
        return true;
    }

    namespace detail
    {
        inline void turretEye(const ActorState &actor, fx *eye)
        {
            const fx up = fxFromInt(turretKind(actor.kind).eye);
            eye[0] = actor.pos[0];
            eye[1] = actor.pos[1];
            eye[2] = actor.pos[2] + ((actor.flags & kActorCeiling) ? -up : up);
        }

        /// The way the gun points (Q12, unit).
        inline void turretForward(const ActorState &actor, fx *forward)
        {
            fx sinYaw, cosYaw, sinPitch, cosPitch;
            fxSinCos(static_cast<std::uint16_t>(actor.aimYaw), sinYaw, cosYaw);
            fxSinCos(static_cast<std::uint16_t>(actor.aimPitch), sinPitch, cosPitch);
            forward[0] = fxMul(cosPitch, cosYaw);
            forward[1] = fxMul(cosPitch, sinYaw);
            forward[2] = sinPitch;
        }

        inline bool turretSees(const LogicWorld &w, const PlayerState &player, const fx *eye)
        {
            if ((player.health <= 0 || unseen(player)))
                return false;
            const fx d[3] = {player.origin[0] - eye[0], player.origin[1] - eye[1], player.origin[2] - eye[2]};
            if (fxLength(d) > fxFromInt(kTurretRange))
                return false;
            const fx head[3] = {player.origin[0], player.origin[1], player.origin[2] + player.viewHeight};
            return lineClear(*w.world, eye, player.origin) || lineClear(*w.world, eye, head);
        }

        /// One think's turning towards where it is wanted. The game's rule:
        /// up and down at the rate it has; round by the shorter way, the
        /// rate building while there is far to go (to ten times what it
        /// began at) and easing off close to. False when it is there.
        inline bool turretTurn(ActorState &actor)
        {
            bool moved = false;
            if (actor.aimPitch != actor.wantPitch)
            {
                const std::int32_t step = actor.turnRate * kDegree / 10;
                const std::int32_t left = actor.wantPitch - actor.aimPitch;
                actor.aimPitch += left > 0 ? (left < step ? left : step) : (-left < step ? left : -step);
                moved = true;
            }
            const std::int32_t left = static_cast<std::int16_t>(static_cast<std::uint32_t>(actor.wantYaw - actor.aimYaw));
            if (left != 0)
            {
                const std::int32_t far = left < 0 ? -left : left;
                if (far > 30 * kDegree)
                {
                    if (actor.turnRate < kTurretTurnRate * 10)
                        actor.turnRate += kTurretTurnRate;
                }
                else if (actor.turnRate > 45)
                {
                    actor.turnRate -= kTurretTurnRate;
                }
                else
                {
                    actor.turnRate += kTurretTurnRate;
                }
                const std::int32_t step = actor.turnRate * kDegree / 10;
                // (There, or as good as: a degree and a half.)
                if (step >= far || far < kTurretTurnRate * kDegree / 20)
                    actor.aimYaw = actor.wantYaw;
                else
                    actor.aimYaw += left > 0 ? step : -step;
                actor.aimYaw &= 0xFFFF;
                actor.wantYaw &= 0xFFFF;
                moved = true;
            }
            if (!moved)
                actor.turnRate = kTurretTurnRate;
            return moved;
        }

        /// Its barrels: got going (the big one takes two seconds and more
        /// over it; the others are ready at once) ...
        inline void turretSpinUp(LogicWorld &w, ActorState &actor, const TurretKind &kind)
        {
            if (actor.struck)
                return;
            if (!kind.spinsUp)
            {
                actor.struck = 1;
                return;
            }
            if (actor.speed == 0)
            {
                monsterSound(w, actor, CommonSound::TurretSpinUp);
                actor.speed = kFxOne / 10;
                actor.thinkMs -= 900; // (it waits a second over the first of it)
                return;
            }
            actor.speed += kFxOne * 3 / 40;
            if (actor.speed >= kFxOne)
            {
                actor.speed = kFxOne;
                actor.struck = 1;
            }
        }
        /// ... and let run down (five seconds, for the big one).
        inline void turretSpinDown(LogicWorld &w, ActorState &actor, const TurretKind &kind)
        {
            if (!actor.struck && actor.speed == 0)
                return;
            if (!kind.spinsUp)
            {
                actor.struck = 0;
                return;
            }
            if (actor.speed >= kFxOne)
                monsterSound(w, actor, CommonSound::TurretSpinDown);
            actor.speed -= kFxOne / 50;
            if (actor.speed <= 0)
            {
                actor.speed = 0;
                actor.struck = 0;
            }
        }

        /// A round, along the way the gun points: it hurts the player if
        /// that line meets them before it meets anything else.
        inline void turretShoot(LogicWorld &w, PlayerState &player, ActorState &actor, const TurretKind &kind,
                                const fx *eye, const fx *forward)
        {
            ActorWorld &a = *w.actors;
            monsterSound(w, actor, kind.fire);
            const fx end[3] = {eye[0] + forward[0] * kTurretRange * 2, eye[1] + forward[1] * kTurretRange * 2,
                               eye[2] + forward[2] * kTurretRange * 2};
            const Shot shot = traceShot(*w.world, eye, end);
            std::int64_t reach = shot.fraction;
            const fx tall = player.ducked ? kCrouchedFeet : kStandingFeet;
            const fx mins[3] = {player.origin[0] - fxFromInt(16), player.origin[1] - fxFromInt(16),
                                player.origin[2] - tall};
            const fx maxs[3] = {player.origin[0] + fxFromInt(16), player.origin[1] + fxFromInt(16),
                                player.origin[2] + tall};
            std::int64_t enter;
            if (player.health > 0 && lineEntersBox(eye, end, mins, maxs, enter) && enter < reach)
            {
                reach = enter;
                // (Its round: BULLET_MONSTER_9MM, _MP5, _12MM, at the game's level.)
                const SkillData &s = skillOf(w);
                const int round = actor.kind == static_cast<std::uint8_t>(ActorKind::Turret) ? s.monster12mmBullet
                                  : actor.kind == static_cast<std::uint8_t>(ActorKind::Sentry)
                                      ? s.monster9mmARBullet
                                      : s.monster9mmBullet;
                playerHurt(player, round > 0 ? round : kind.damage, eye, kDmgBullet);
                a.got |= kGotHurt;
            }
            fx at[3];
            for (int i = 0; i < 3; ++i)
                at[i] = eye[i] + static_cast<fx>((static_cast<std::int64_t>(end[i] - eye[i]) * reach) >> kFractionShift);
            effectAdd(a, kEffectTracer, eye, at, 0, 60);
            effectAdd(a, kEffectMuzzle, eye, nullptr, 0, 50);
            // (What misses marks the wall behind.)
            if (reach == shot.fraction && shot.fraction < kFractionOne)
                markShot(a, *w.world, eye, end, false);
        }

        inline void turretDeploy(LogicWorld &w, PlayerState &player, ActorState &actor, bool alarmed)
        {
            ActorWorld &a = *w.actors;
            actor.ai = kTurretDeploy;
            monsterClip(a, actor, "deploy", true);
            if (alarmed)
                monsterSound(w, actor, CommonSound::TurretAlert);
            monsterSound(w, actor, CommonSound::TurretDeploy);
            // What it sets off as it comes out (a tripped sentry wakes the
            // others of its name this way).
            if (actor.fires != 0)
                logicFire(w, player, actor.fires, -1, kUseOn, 0);
        }

        inline void turretThink(LogicWorld &w, PlayerState &player, int index)
        {
            ActorWorld &a = *w.actors;
            ActorState &actor = a.states[index];
            const TurretKind &kind = turretKind(actor.kind);
            const bool ceiling = (actor.flags & kActorCeiling) != 0;
            fx eye[3];
            turretEye(actor, eye);
            // The big turret's eye (ActorState::reach, 0..255): lit at each
            // ping, it goes out again over the next second.
            actor.reach = actor.reach > 30 ? actor.reach - 30 : 0;

            // Switched by name: on if it is off, off if it is on (unless it
            // was told which, and is that already).
            if (actor.switched != 0)
            {
                const int how = actor.switched - 1;
                actor.switched = 0;
                const bool on = actor.ai != kTurretOff;
                if (how == kUseToggle || how == kUseSet || (how == kUseOn) != on)
                {
                    if (on && actor.ai != kTurretRetire)
                    {
                        actor.hasGoal = false; // told to stop: it does not come out by itself again
                        actor.ai = kTurretRetire;
                    }
                    else if (!on)
                    {
                        actor.hasGoal = (actor.flags & kActorAuto) != 0;
                        turretDeploy(w, player, actor, false);
                        return;
                    }
                }
            }

            switch (actor.ai)
            {
            case kTurretOff:
            {
                // One that looks out for itself comes out at the sight of
                // someone; a sentry at a touch.
                fx mins[3], maxs[3];
                turretBox(actor, mins, maxs);
                const bool touched = actor.kind == static_cast<std::uint8_t>(ActorKind::Sentry) &&
                                     player.health > 0 && playerTouchesBox(player, mins, maxs, fxFromInt(2));
                if (touched || (actor.hasGoal && turretSees(w, player, eye)))
                    turretDeploy(w, player, actor, true);
                break;
            }
            case kTurretDeploy:
                if (!actor.finished)
                    break;
                actor.aimYaw = actor.wantYaw = actor.yaw;
                actor.aimPitch = actor.wantPitch = 0;
                actor.turnRate = kTurretTurnRate;
                actor.stuckMs = kind.maxWaitMs;
                actor.goal[0] = kind.maxSpinMs;
                actor.moveMs = 0;
                actor.cooldownMs = 0;
                actor.ai = kTurretSearch;
                monsterClip(a, actor, "spin", false);
                break;
            case kTurretSearch:
                monsterClip(a, actor, "spin", false);
                actor.cooldownMs -= 100;
                if (actor.cooldownMs <= 0)
                {
                    actor.cooldownMs = 1000;
                    monsterSound(w, actor, CommonSound::TurretPing);
                    actor.reach = 255; // (the big one's eye lights with it)
                }
                if (turretSees(w, player, eye))
                {
                    actor.moveMs = 0;
                    actor.goal[0] = kind.maxSpinMs;
                    actor.ai = kTurretActive;
                    break;
                }
                if (kind.maxWaitMs != 0 && (actor.stuckMs -= 100) <= 0)
                {
                    actor.ai = kTurretRetire;
                    break;
                }
                if (kind.maxSpinMs != 0 && (actor.goal[0] -= 100) <= 0)
                    turretSpinDown(w, actor, kind);
                // Round and round, looking.
                actor.wantYaw = (actor.wantYaw + actor.turnRate * kDegree / 10) & 0xFFFF;
                turretTurn(actor);
                break;
            case kTurretActive:
            {
                const bool visible = turretSees(w, player, eye);
                if (!visible)
                {
                    // Half a second more of it, then back to looking.
                    if (actor.moveMs == 0)
                    {
                        actor.moveMs = 500;
                    }
                    else if ((actor.moveMs -= 100) <= 0)
                    {
                        actor.moveMs = 0;
                        actor.stuckMs = kind.maxWaitMs;
                        actor.ai = kTurretSearch;
                        break;
                    }
                }
                else
                {
                    actor.moveMs = 0;
                }
                fx forward[3];
                turretForward(actor, forward);
                fx to[3] = {player.origin[0] - eye[0], player.origin[1] - eye[1], player.origin[2] - eye[2]};
                const fx far = fxLength(to);
                // Within thirty degrees of where it points.
                const bool onTarget =
                    far > 0 && fxMul(to[0], forward[0]) + fxMul(to[1], forward[1]) + fxMul(to[2], forward[2]) >
                                   fxMul(far, fxFromDouble(0.866));
                if (actor.struck && (onTarget || actor.attack))
                {
                    monsterClip(a, actor, "fire", false);
                    turretShoot(w, player, actor, kind, eye, forward);
                }
                else
                {
                    monsterClip(a, actor, "spin", false);
                }
                if (actor.attack)
                {
                    // Nearly done for, and wild: now and then it swings off
                    // somewhere, and it does not last.
                    if (chance(a) % 10 == 0)
                    {
                        actor.wantYaw = static_cast<std::int32_t>(chance(a) & 0xFFFF);
                        actor.wantPitch = static_cast<std::int32_t>(chance(a) % 90) * kDegree * (ceiling ? -1 : 1);
                        actor.health = static_cast<std::int16_t>(actor.health - 10);
                        if (actor.health <= 0)
                        {
                            actor.ai = kAiDying;
                            actor.switched = 0xFF;
                        }
                        break;
                    }
                }
                else if (visible)
                {
                    const fx flat[3] = {to[0], to[1], 0};
                    actor.wantYaw = fxAtan2Turns(to[1], to[0]);
                    std::int32_t pitch = static_cast<std::int16_t>(fxAtan2Turns(to[2], fxLength(flat)));
                    // As far up and down as it is built to point (upside
                    // down, the other way up).
                    const std::int32_t low = (ceiling ? -90 : kind.minPitch) * kDegree;
                    const std::int32_t high = (ceiling ? -kind.minPitch : 90) * kDegree;
                    actor.wantPitch = pitch < low ? low : (pitch > high ? high : pitch);
                }
                turretSpinUp(w, actor, kind);
                turretTurn(actor);
                break;
            }
            case kTurretRetire:
            {
                // Gun back to where it started; barrels stopped; then away.
                actor.wantPitch = 0;
                actor.wantYaw = actor.yaw;
                const ModelView *model = actorModel(a, actor);
                const int retire = model != nullptr ? model->findClip(nameHash("retire")) : -1;
                if (turretTurn(actor))
                {
                    monsterClip(a, actor, "spin", false);
                }
                else if (actor.struck || actor.speed != 0)
                {
                    turretSpinDown(w, actor, kind);
                }
                else if (retire >= 0 && actor.clip != retire)
                {
                    // (The game plays it backwards: turretTick runs it.)
                    actor.clip = static_cast<std::int16_t>(retire);
                    actor.frame = (model->clips[retire].frameCount - 1u) << 8;
                    actor.frozen = true;
                    actor.once = true;
                    actor.finished = false;
                    monsterSound(w, actor, CommonSound::TurretDeploy);
                    if (actor.fires != 0)
                        logicFire(w, player, actor.fires, -1, kUseOff, 0);
                }
                else if (retire < 0 || actor.frame == 0)
                {
                    actor.ai = kTurretOff;
                    actor.attack = 0;
                    monsterClip(a, actor, "idle_off", false);
                }
                break;
            }
            default:
                break;
            }
        }

        inline void turretTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            ActorWorld &a = *w.actors;
            ActorState &actor = a.states[index];
            const TurretKind &kind = turretKind(actor.kind);
            if (actor.health == 0 && actor.ai == kTurretOff && actor.switched != 0xFF)
            {
                // Its first tick. (Health in tenths: see turretHurt.)
                actor.health = static_cast<std::int16_t>(skillHealth(skillOf(w), actor.kind, kind.health) * 10);
                actor.hasGoal = (actor.flags & kActorAuto) != 0;
                actor.aimYaw = actor.wantYaw = actor.yaw;
                actor.turnRate = kTurretTurnRate;
                actor.thinkMs = -200 - (index * 37) % 100;
            }
            if (actor.ai == kAiDead)
                return;
            if (actor.ai == kAiDying)
            {
                if (actor.switched == 0xFF)
                {
                    // Destroyed, a moment ago: the gun droops (a sentry's
                    // falls straight), and what it was to set off goes off.
                    actor.switched = 0;
                    actor.reach = 255; // (its eye, a last time)
                    monsterSound(w, actor, CommonSound::TurretDie);
                    monsterClip(a, actor, "die", true);
                    actor.struck = 0;
                    actor.speed = 0;
                    if (actor.kind == static_cast<std::uint8_t>(ActorKind::Sentry))
                    {
                        actor.aimYaw = actor.wantYaw = actor.yaw;
                        actor.aimPitch = actor.wantPitch = 0;
                        actor.flags &= static_cast<std::uint8_t>(~kActorSolid);
                    }
                    else
                    {
                        actor.wantPitch = ((actor.flags & kActorCeiling) ? -90 : -15) * kDegree;
                    }
                    if (actor.fires != 0)
                        logicFire(w, player, actor.fires, -1, kUseOn, 0);
                    return;
                }
                if ((actor.thinkMs += ms) >= 100)
                {
                    actor.thinkMs -= 100;
                    actor.reach = actor.reach > 30 ? actor.reach - 30 : 0;
                    actor.wantYaw = actor.aimYaw;
                    if (!turretTurn(actor) && actor.finished)
                        actor.ai = kAiDead;
                }
                return;
            }
            // Folding away is its coming out, run backwards.
            if (actor.ai == kTurretRetire && actor.frozen)
            {
                const ModelView *model = actorModel(a, actor);
                const std::uint32_t back =
                    model != nullptr && actor.clip >= 0
                        ? static_cast<std::uint32_t>(model->clips[actor.clip].fps) * static_cast<std::uint32_t>(ms) / 1000u
                        : actor.frame;
                actor.frame = actor.frame > back ? actor.frame - back : 0;
            }
            actor.thinkMs += ms;
            // (Not more than two thinks a tick, however long the tick.)
            for (int n = 0; n < 2 && actor.thinkMs >= 100 && actor.ai < kAiDying; ++n)
            {
                actor.thinkMs -= 100;
                turretThink(w, player, index);
            }
            if (actor.thinkMs > 200)
                actor.thinkMs = 200;
        }
    } // namespace detail

    /// Hurt the turret at `index`, with `damage` to a hit box of `group`.
    /// The game's rules: its plating turns a bullet (a tenth gets through);
    /// folded away it feels a tenth of anything (not a sentry, which is
    /// woken by it instead); nearly done for, and out, it goes wild. Its
    /// health is kept in tenths, for the tenths.
    inline void turretHurt(LogicWorld &w, int index, int damage, bool bullet, int group)
    {
        ActorState &actor = w.actors->states[index];
        if (!actor.active || actor.ai >= kAiDying)
            return;
        const TurretKind &kind = turretKind(actor.kind);
        int tenths = damage * 10;
        if (bullet && group == kHitArmour && kind.tough)
            tenths /= 10;
        if (kind.tough && actor.ai == kTurretOff)
            tenths /= 10;
        if (tenths <= 0)
            return;
        actor.health = static_cast<std::int16_t>(actor.health - tenths);
        if (actor.health <= 0)
        {
            actor.health = 0;
            actor.ai = kAiDying;
            actor.switched = 0xFF; // (turretTick says so, and sets off what it is to)
            actor.thinkMs = 0;
            return;
        }
        if (!kind.tough)
        {
            if (actor.ai == kTurretOff)
                actor.switched = static_cast<std::uint8_t>(1 + kUseOn);
        }
        else if (actor.health <= 100 && actor.ai != kTurretOff && actor.ai != kTurretDeploy)
        {
            actor.attack = 1;
            actor.ai = kTurretSearch;
        }
    }

    // ------------------------------------------------------------- tripmines
    //
    // The game's tripmine.cpp. A mine on a wall: a while after the map
    // starts (two and a half seconds; one, if the map says so) its beam
    // comes on, out along the way it points to the first thing in the way
    // within 2048 units. Ten times a second the beam is measured again --
    // against the world and what moves in it, the player, and anything that
    // fights -- and if it has got any shorter or longer, or the mine is hurt
    // by anything at all, the mine goes off a moment later: 150, to 375
    // units.

    inline constexpr std::uint8_t kMineArmed = 1; ///< ActorState::ai, between kAiIdle and kAiDying

    namespace detail
    {
        /// How far along its beam (Q30, of kMineRange) the first thing is.
        inline std::int32_t mineReach(const LogicWorld &w, const PlayerState &player, int index, const fx *end)
        {
            return beamReach(w, player, w.actors->states[index].pos, end, index);
        }

        inline void mineTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            ActorWorld &a = *w.actors;
            ActorState &mine = a.states[index];
            if (mine.ai == kAiDead)
                return;
            fx forward[3];
            turretForward(mine, forward);
            if (mine.ai == kAiDying)
            {
                // Set off: it goes a moment after.
                if ((mine.thinkMs -= ms) > 0)
                    return;
                mine.ai = kAiDead;
                mine.active = false;
                actorPlaceBox(a, index);
                const fx at[3] = {mine.pos[0] + forward[0] * 8, mine.pos[1] + forward[1] * 8,
                                  mine.pos[2] + forward[2] * 8};
                explodeAt(w, player, at, kMineBlast, true, true, true);
                return;
            }
            const fx end[3] = {mine.pos[0] + forward[0] * kMineRange, mine.pos[1] + forward[1] * kMineRange,
                               mine.pos[2] + forward[2] * kMineRange};
            if (mine.ai != kMineArmed)
            {
                if (mine.health == 0)
                {
                    mine.health = 1;
                    mine.thinkMs = (mine.flags & kActorQuick) ? 1000 : 2500;
                }
                if ((mine.thinkMs -= ms) > 0)
                    return;
                // Its beam comes on: as long as it is now is as long as it
                // has to stay. (ActorState::reach: Q30 of the range, not
                // Q12; ::goal: where it ends.)
                mine.reach = mineReach(w, player, index, end);
                for (int k = 0; k < 3; ++k)
                    mine.goal[k] = mine.pos[k] + static_cast<fx>((static_cast<std::int64_t>(end[k] - mine.pos[k]) *
                                                                   mine.reach) >> kFractionShift);
                mine.ai = kMineArmed;
                mine.thinkMs = 0;
                monsterSound(w, mine, CommonSound::MineActivate);
                return;
            }
            if ((mine.thinkMs += ms) < 100)
                return;
            mine.thinkMs = mine.thinkMs > 200 ? 0 : mine.thinkMs - 100;
            const std::int32_t reach = mineReach(w, player, index, end);
            const std::int32_t off = reach > mine.reach ? reach - mine.reach : mine.reach - reach;
            // (A thousandth of the range, as the game: two units.)
            if (off > kFractionOne / 1000)
                mineHurt(w, index);
        }
    } // namespace detail

    /// Anything at all sets a mine off: a tenth to three tenths of a second
    /// later.
    inline void mineHurt(LogicWorld &w, int index)
    {
        ActorWorld &a = *w.actors;
        ActorState &mine = a.states[index];
        if (!mine.active || mine.ai >= kAiDying)
            return;
        mine.ai = kAiDying;
        mine.thinkMs = 100 + static_cast<int>(detail::chance(a) % 200);
    }

    /// What can be shot, and stays destroyed: the monsters, the turrets,
    /// the soldiers and the mines.
    inline bool fights(std::uint8_t kind)
    {
        return isMonster(kind) || isTurret(kind) || isGrunt(kind) || isMine(kind) || isMachine(kind) ||
               isAssassin(kind);
    }
    inline bool canBeHurt(std::uint8_t kind)
    {
        // (The man in the suit takes a shot, and shrugs it off; whatever a
        // map makes of a monster_generic dies of a few.)
        return fights(kind) || isPerson(kind) || kind == static_cast<std::uint8_t>(ActorKind::Player) ||
               kind == static_cast<std::uint8_t>(ActorKind::Gman) || kind == static_cast<std::uint8_t>(ActorKind::Generic);
    }

    inline void bleed(LogicWorld &w, int index, const fx *at, const fx *dir, bool heavy)
    {
        if (w.actors == nullptr || w.world == nullptr)
            return;
        const int blood = bloodOf(w.actors->states[index].kind);
        if (blood == kBloodNone)
            return;
        const bool alien = blood == kBloodYellow;
        effectAdd(*w.actors, kEffectBlood, at, nullptr, alien ? 1 : 0, 250);
        static const fx kDown[3] = {0, 0, -kFxOne};
        markBlood(*w.actors, *w.world, at, dir != nullptr ? dir : kDown, alien, heavy);
        if (heavy)
            markFloor(*w.actors, *w.world, at, 96, alien ? kDecalAlien : kDecalBlood, 22,
                      alien ? kMarkAlien : kMarkBlood);
    }

    /// The box a shot at the actor is first tried against.
    inline void targetBox(const ActorState &actor, fx *mins, fx *maxs)
    {
        if (isTurret(actor.kind))
        {
            turretBox(actor, mins, maxs);
            return;
        }
        if (isMine(actor.kind))
        {
            for (int i = 0; i < 3; ++i)
            {
                mins[i] = actor.pos[i] - fxFromInt(8);
                maxs[i] = actor.pos[i] + fxFromInt(8);
            }
            return;
        }
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Player))
        {
            // (The player's own size; `mood` says crouched.)
            for (int i = 0; i < 2; ++i)
            {
                mins[i] = actor.pos[i] - fxFromInt(16);
                maxs[i] = actor.pos[i] + fxFromInt(16);
            }
            // (Their origin is their middle: the game's player's box about it.)
            const fx half = fxFromInt(actor.mood != 0 ? 18 : 36);
            mins[2] = actor.pos[2] - half;
            maxs[2] = actor.pos[2] + half;
            return;
        }
        if (isPerson(actor.kind))
        {
            // (Seated, a scientist is half the height.)
            const bool seated = actor.kind == static_cast<std::uint8_t>(ActorKind::Sitter);
            for (int i = 0; i < 2; ++i)
            {
                mins[i] = actor.pos[i] - fxFromInt(14);
                maxs[i] = actor.pos[i] + fxFromInt(14);
            }
            mins[2] = actor.pos[2];
            maxs[2] = actor.pos[2] + fxFromInt(seated ? 48 : 70);
            return;
        }
        const MonsterKind &kind = monsterKind(actor.kind);
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Osprey))
        {
            for (int i = 0; i < 2; ++i)
            {
                mins[i] = actor.pos[i] - kind.half;
                maxs[i] = actor.pos[i] + kind.half;
            }
            mins[2] = actor.pos[2] - fxFromInt(100);
            maxs[2] = actor.pos[2] + fxFromInt(32);
            return;
        }
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Nihilanth))
        {
            for (int i = 0; i < 2; ++i)
            {
                mins[i] = actor.pos[i] - kind.half;
                maxs[i] = actor.pos[i] + kind.half;
            }
            mins[2] = actor.pos[2] - fxFromInt(720);
            maxs[2] = actor.pos[2] + fxFromInt(420);
            return;
        }
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Apache))
        {
            for (int i = 0; i < 2; ++i)
            {
                mins[i] = actor.pos[i] - kind.half;
                maxs[i] = actor.pos[i] + kind.half;
            }
            mins[2] = actor.pos[2] - kind.height;
            maxs[2] = actor.pos[2];
            return;
        }
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Ichthyosaur))
        {
            for (int i = 0; i < 3; ++i)
            {
                mins[i] = actor.pos[i] - kind.half;
                maxs[i] = actor.pos[i] + kind.half;
            }
            return;
        }
        // (A barnacle hangs from its "feet", the mouth on the ceiling: its
        // body is below them, as the game's box is, -32 to 0.)
        const bool hangs = actor.kind == static_cast<std::uint8_t>(ActorKind::Barnacle);
        for (int i = 0; i < 2; ++i)
        {
            mins[i] = actor.pos[i] - kind.half;
            maxs[i] = actor.pos[i] + kind.half;
        }
        mins[2] = hangs ? actor.pos[2] - kind.height : actor.pos[2];
        maxs[2] = hangs ? actor.pos[2] : actor.pos[2] + kind.height;
    }

    /// Everything in this file that happens by itself, for `ms` more
    /// milliseconds. After logicTick (which moves the actors' clips on).
    namespace detail
    {
        /// One of the people and what is under them. They stand on brushes
        /// as on the world -- a lift's floor, a pane -- go where a brush
        /// they stand on goes, and FALL when it breaks or is no longer
        /// there, as far as there is to fall. (Someone the map stood where
        /// nothing at all is under them is left there, as before: only
        /// whoever had ground can lose it.)
        inline void peopleGround(LogicWorld &w, ActorState &actor, int ms)
        {
            const MoveWorld &world = *w.world;
            if (actor.groundMover == -2)
            {
                const fx from[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36 + 1)};
                const fx to[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36 - 24)};
                const Trace tr = traceAll(world, false, from, to);
                actor.groundMover = -1;
                if (!tr.startSolid && !tr.allSolid && tr.fraction < kFractionOne && tr.mover >= 0)
                {
                    actor.groundMover = tr.mover;
                    actor.pos[2] = tr.endPos[2] - fxFromInt(36);
                    for (int k = 0; k < 3; ++k)
                        actor.groundAt[k] = w.brushStates[tr.mover].pos[k];
                }
            }
            if (actor.airborne)
            {
                // (The game's gravity, 800 units a second each second.)
                actor.velocity[2] -= fxFromInt(800) * ms / 1000;
                const fx step = static_cast<fx>((static_cast<std::int64_t>(actor.velocity[2]) * ms) / 1000);
                const fx from[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36)};
                const fx to[3] = {from[0], from[1], from[2] + step};
                const Trace tr = traceAll(world, false, from, to);
                if (tr.startSolid || tr.allSolid)
                {
                    actor.airborne = false; // (nowhere to fall: it stays)
                    actor.velocity[2] = 0;
                    actor.groundMover = -1;
                }
                else if (tr.fraction < kFractionOne)
                {
                    actor.pos[2] = tr.endPos[2] - fxFromInt(36);
                    actor.airborne = false;
                    actor.velocity[2] = 0;
                    actor.groundMover = tr.mover;
                    if (tr.mover >= 0)
                    {
                        for (int k = 0; k < 3; ++k)
                            actor.groundAt[k] = w.brushStates[tr.mover].pos[k];
                    }
                }
                else
                {
                    actor.pos[2] += step;
                }
                return;
            }
            if (actor.groundMover < 0 || actor.groundMover >= w.brushCount)
                return;
            const BrushState &under = w.brushStates[actor.groundMover];
            if (!under.active || !w.movers[actor.groundMover].solid)
            {
                // What they stood on is gone.
                actor.groundMover = -1;
                actor.airborne = true;
                actor.velocity[2] = 0;
                return;
            }
            // ... or has moved: so have they.
            for (int k = 0; k < 3; ++k)
            {
                actor.pos[k] += under.pos[k] - actor.groundAt[k];
                actor.groundAt[k] = under.pos[k];
            }
        }

        /// The places that hurt (trigger_hurt) hurt whoever is in them, not
        /// the player alone: half a second's worth every half second.
        inline void hurtPlaces(LogicWorld &w)
        {
            ActorWorld &a = *w.actors;
            for (int r = 0; r < w.recordCount; ++r)
            {
                const LogicRecord &record = w.records[r];
                if (record.kind != static_cast<std::uint8_t>(LogicKind::TriggerHurt))
                    continue;
                const LogicState &state = w.states[r];
                if (state.dead || state.running || !logicMasterAllows(w, r))
                    continue;
                const int damage = static_cast<std::int32_t>(record.extra[0]) / 2;
                if (damage <= 0)
                    continue;
                for (int i = 0; i < a.count; ++i)
                {
                    ActorState &actor = a.states[i];
                    if (!actor.active || actor.hurtMs > 0 || actor.ai >= kAiDying || actor.script >= 0 ||
                        !canBeHurt(actor.kind))
                        continue;
                    // (Its box against the place's: about a person's.)
                    const int x = fxToInt(actor.pos[0]), y = fxToInt(actor.pos[1]), z = fxToInt(actor.pos[2]);
                    if (x + 16 < record.mins[0] || x - 16 > record.maxs[0] || y + 16 < record.mins[1] ||
                        y - 16 > record.maxs[1] || z + 72 < record.mins[2] || z > record.maxs[2])
                        continue;
                    actor.hurtMs = 500;
                    monsterHurt(w, i, damage, false);
                }
            }
        }

        /// A door or a train that hurts (its "dmg") and is on the move hurts
        /// whoever it has come into -- not whoever rides it -- half a
        /// second's worth every half second: it does not push them, as the
        /// game's would, so it is never held up by them either.
        inline void crushPlaces(LogicWorld &w)
        {
            ActorWorld &a = *w.actors;
            for (int r = 0; r < w.recordCount && w.movers != nullptr; ++r)
            {
                const LogicRecord &record = w.records[r];
                const LogicState &state = w.states[r];
                const auto kind = static_cast<LogicKind>(record.kind);
                if (record.damage == 0 || record.brush == kNoBrush || state.dead ||
                    (kind != LogicKind::Door && kind != LogicKind::DoorRotating && kind != LogicKind::Train) ||
                    (state.move != MoveState::GoingUp && state.move != MoveState::GoingDown))
                    continue;
                const MoverHull &mover = w.movers[record.brush];
                if (!mover.solid || !w.brushStates[record.brush].active)
                    continue;
                for (int i = 0; i < a.count; ++i)
                {
                    ActorState &actor = a.states[i];
                    if (!actor.active || actor.hurtMs > 0 || actor.ai >= kAiDying || !canBeHurt(actor.kind) ||
                        actor.groundMover == static_cast<std::int16_t>(record.brush))
                        continue;
                    const fx middle[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36)};
                    if (!nearMover(mover, false, middle, middle))
                        continue;
                    fx local[3];
                    toMoverSpace(mover, middle, local);
                    if (hullPositionFree(mover.standing, local))
                        continue;
                    actor.hurtMs = 500;
                    monsterHurt(w, i, record.damage, false, kHitGeneric, kDmgCrush, kAiNone);
                }
            }
        }

        /// What fights and comes into a trigger_monsterjump, on its feet, is
        /// thrown: the volume's velocity, as the AI's own leap does it.
        inline void monsterJumps(LogicWorld &w)
        {
            ActorWorld &a = *w.actors;
            for (int r = 0; r < w.recordCount; ++r)
            {
                const LogicRecord &record = w.records[r];
                if (record.kind != static_cast<std::uint8_t>(LogicKind::MonsterJump) || w.states[r].dead ||
                    !w.states[r].running)
                    continue;
                for (int i = 0; i < a.count; ++i)
                {
                    ActorState &actor = a.states[i];
                    if (!actor.active || actor.ai >= kAiDying || actor.script >= 0 || !actorThinks(actor.kind) ||
                        isPerson(actor.kind))
                        continue;
                    AiMonster *mind = aiOf(a, i);
                    if (mind == nullptr || mind->inAir)
                        continue;
                    const int x = fxToInt(actor.pos[0]), y = fxToInt(actor.pos[1]), z = fxToInt(actor.pos[2]);
                    if (x + 16 < record.mins[0] || x - 16 > record.maxs[0] || y + 16 < record.mins[1] ||
                        y - 16 > record.maxs[1] || z + 72 < record.mins[2] || z > record.maxs[2])
                        continue;
                    for (int k = 0; k < 3; ++k)
                        actor.velocity[k] = record.move[k];
                    mind->inAir = true;
                    actor.ai = kAiLeap;
                }
            }
        }
    } // namespace detail

    /// The player takes what lies at actor `index`, if it is of any use to
    /// them: it is theirs, it is heard, it is gone from where it lay, and
    /// what taking it sets off is set off. False if it was of no use.
    inline bool itemPickUp(LogicWorld &w, PlayerState &player, int index)
    {
        ActorWorld &a = *w.actors;
        ActorState &actor = a.states[index];
        // An air tank stays where it is: it gives the player all the air
        // there is (the game's twelve seconds), and again half a minute on.
        if (actor.item == static_cast<std::uint8_t>(ItemKind::Airtank))
        {
            if (actor.cooldownMs > 0)
                return false;
            player.underMs = 0;
            actor.cooldownMs = kAirtankRechargeMs;
            if (w.heard != nullptr)
                w.heard->add(CommonSound::GunPickup, nullptr);
            if (actor.fires != 0)
                logicFire(w, player, actor.fires, -1, kUseToggle, 0);
            return true;
        }
        if (!detail::itemTakeAt(w, player, index, a.got))
            return false;
        a.got |= kGotItem;
        if (w.achievements != nullptr)
            w.achievements->push(AchEventType::Picked, 0, actor.item);
        if (w.heard != nullptr)
        {
            const auto item = static_cast<ItemKind>(actor.item);
            w.heard->add(item == ItemKind::HealthKit ? CommonSound::MedKit
                         : (item == ItemKind::GlockClip || item == ItemKind::Buckshot ||
                            item == ItemKind::Mp5Clip || item == ItemKind::AmmoBox ||
                            item == ItemKind::ArGrenades || item == ItemKind::Ammo357 || item == ItemKind::Bolts ||
                            item == ItemKind::RpgClip || item == ItemKind::GaussClip || item == ItemKind::Weaponbox)
                             ? CommonSound::AmmoPickup
                             : CommonSound::GunPickup,
                         nullptr);
        }
        actor.active = false;
        actorPlaceBox(a, index);
        // What taking it sets off (the suit opens the way on).
        if (actor.fires != 0)
            logicFire(w, player, actor.fires, -1, kUseToggle, 0);
        return true;
    }

    namespace detail
    {
        inline void tankTick(LogicWorld &w, PlayerState &player, int index, int ms);
        inline void mortarTick(LogicWorld &w, PlayerState &player, int index, int ms);
        inline void repelTick(LogicWorld &w, PlayerState &player, int index, int ms); // (HlMilitary.hpp)
    }

    inline void xenTick(LogicWorld &w, PlayerState &player, int ms); // (HlXen.hpp)

    namespace detail
    {
        /// CheckSuitUpdate: the suit's next line, once the last has had its
        /// three and a half seconds (SUITUPDATETIME). The lines are the map's
        /// first sentences (SuitLine).
        inline void suitTick(LogicWorld &w, PlayerState &player, int ms)
        {
            SuitVoice &suit = player.suit.value;
            for (std::int32_t &again : suit.againMs)
                again = again > ms ? again - ms : 0;
            if (suit.waitMs > 0)
                suit.waitMs -= ms;
            if (suit.waitMs > 0 || suit.queue[0] < 0 || player.health <= 0)
                return;
            if (w.heard != nullptr)
                w.heard->add(static_cast<std::uint16_t>(suit.queue[0]), nullptr, 64, kRadiusEverywhere,
                             kSoundSentence);
            for (int i = 0; i < 3; ++i)
                suit.queue[i] = suit.queue[i + 1];
            suit.queue[3] = -1;
            suit.waitMs = 3500;
        }

        /// The player in pieces (GibMonster: CGib::SpawnRandomGibs from
        /// their box, a head among them), and the sound of it.
        inline void playerGibs(LogicWorld &w, PlayerState &player)
        {
            player.gib.value = 0;
            ActorWorld &a = *w.actors;
            if (w.heard != nullptr)
                w.heard->add(CommonSound::BodySplat, player.origin);
            const int pieces = gibPieces(a, a.humanGibs);
            if (pieces == 0)
                return;
            for (int n = 0; n < 4; ++n)
            {
                fx at[3], velocity[3];
                for (int k = 0; k < 3; ++k)
                {
                    const fx span = k == 2 ? fxFromInt(72) : fxFromInt(32);
                    at[k] = player.origin[k] - span / 2 + static_cast<fx>((static_cast<std::int64_t>(span) * (aiRandom(a) & 255)) >> 8);
                    velocity[k] = fxFromInt(aiRandomLong(a, -100, 100)) * 7 / 10;
                }
                velocity[2] = fxFromInt(aiRandomLong(a, 200, 300)) * 7 / 10;
                // (The first is the head: body 0 of the gibs.)
                Gib &gib = gibAdd(a, a.humanGibs, n == 0 ? 0 : 1 + aiRandomLong(a, 0, pieces > 2 ? pieces - 2 : 0), at,
                                  velocity, (6000 + aiRandomLong(a, 0, 2000)) * a.lifePercent / 100);
                gib.yaw = static_cast<std::uint16_t>(aiRandom(a));
                gib.spin = static_cast<std::int16_t>(40 + aiRandomLong(a, 0, 59));
                effectAdd(a, kEffectBlood, at, nullptr, 0, 300);
            }
        }

        /// CTriggerPush::Touch, for the monsters: a push that stays on
        /// carries whoever stands in it along the ground at its speed (their
        /// base velocity) -- c1a1c's crabs out from under the monstermakers
        /// that drop them. (Upwards, and the pushes that go once, are the
        /// player's.)
        inline void monsterPushes(LogicWorld &w, int ms)
        {
            ActorWorld &a = *w.actors;
            for (int i = 0; i < w.recordCount; ++i)
            {
                const LogicRecord &record = w.records[i];
                const LogicState &state = w.states[i];
                if (record.kind != static_cast<std::uint8_t>(LogicKind::TriggerPush) || state.running || state.dead ||
                    (record.flags & kLogicFireOnce) || !logicMasterAllows(w, i))
                    continue;
                const fx dx = static_cast<fx>((static_cast<std::int64_t>(record.move[0]) * ms) / 1000);
                const fx dy = static_cast<fx>((static_cast<std::int64_t>(record.move[1]) * ms) / 1000);
                if (dx == 0 && dy == 0)
                    continue;
                for (int n = 0; n < a.count; ++n)
                {
                    ActorState &actor = a.states[n];
                    if (!actor.active || !isMonster(actor.kind) || actor.ai >= kAiDying || actor.script >= 0 ||
                        isTurret(actor.kind))
                        continue;
                    fx lo[3], hi[3];
                    targetBox(actor, lo, hi);
                    if (hi[0] < fxFromInt(record.mins[0]) || lo[0] > fxFromInt(record.maxs[0]) ||
                        hi[1] < fxFromInt(record.mins[1]) || lo[1] > fxFromInt(record.maxs[1]) ||
                        hi[2] < fxFromInt(record.mins[2]) || lo[2] > fxFromInt(record.maxs[2]))
                        continue;
                    fx feet[3] = {actor.pos[0], actor.pos[1], actor.pos[2]};
                    if (walkStep(*w.world, &a, n, monsterHull(actor.kind), feet, dx, dy))
                    {
                        for (int k = 0; k < 3; ++k)
                            actor.pos[k] = feet[k];
                        actorPlaceBox(a, n);
                    }
                }
            }
        }
    } // namespace detail

    inline void monstersTick(LogicWorld &w, PlayerState &player, int ms)
    {
        if (w.actors == nullptr || w.world == nullptr)
            return;
        ActorWorld &a = *w.actors;
        detail::suitTick(w, player, ms);
        if (player.gib.value != 0)
            detail::playerGibs(w, player);
        if (w.records != nullptr && w.states != nullptr)
            detail::monsterPushes(w, ms);
        // (Looked at eight times a second: nothing stays in such a place
        // for less.)
        a.hurtLookMs -= ms;
        if (a.hurtLookMs <= 0 && w.records != nullptr && w.states != nullptr)
        {
            a.hurtLookMs = 125;
            detail::hurtPlaces(w);
            detail::monsterJumps(w);
        }
        // (Each frame: a door can be through someone in less than an
        // eighth of a second.)
        if (w.records != nullptr && w.states != nullptr)
            detail::crushPlaces(w);
        xenTick(w, player, ms);
        for (int k = 0; k < 3; ++k)
            a.foe[k] = player.origin[k];
        // The bodies that fade: on towards nothing, and gone at the end.
        for (int i = 0; i < a.count; ++i)
        {
            ActorState &body = a.states[i];
            const int left = body.active ? corpseFadeLeft(body) : -1;
            if (left < 0)
                continue;
            if (left <= ms)
            {
                body.active = false;
                body.flags &= static_cast<std::uint8_t>(~kActorSolid);
                actorPlaceBox(a, i);
            }
            else
            {
                body.cooldownMs += ms;
            }
        }
        aiWorldTick(w, player, ms);
        if (a.noiseMs > 0)
            a.noiseMs -= ms;
        if (a.speakMs > 0)
            a.speakMs -= ms;
        // The small things in flight; a casing rings as it lands (brass, or
        // a shotgun's shell: TE_BOUNCE_SHELL and TE_BOUNCE_SHOTSHELL).
        const int rang = piecesTick(a, *w.world, ms);
        if ((rang & 1) && w.heard != nullptr)
            w.heard->add(CommonSound::Shell, nullptr, 70, kRadiusSmall);
        if ((rang & 2) && w.heard != nullptr)
            w.heard->add(CommonSound::ShotShell, nullptr, 70, kRadiusSmall);
        decalsTick(a, ms);
        for (int i = 0; i < w.recordCount; ++i)
        {
            const auto kind = static_cast<LogicKind>(w.records[i].kind);
            if (kind == LogicKind::Maker)
                detail::makerTick(w, player, i, ms);
            else if (kind == LogicKind::HealthCharger || kind == LogicKind::SuitCharger)
                detail::chargerTick(w, player, i, ms);
            else if (kind == LogicKind::Tank)
                detail::tankTick(w, player, i, ms);
            else if (kind == LogicKind::MortarField)
                detail::mortarTick(w, player, i, ms);
            else if (kind == LogicKind::Repel)
                detail::repelTick(w, player, i, ms);
        }
        for (int i = 0; i < a.count; ++i)
        {
            ActorState &actor = a.states[i];
            if (!actor.active)
                continue;
            // (The places that hurt and the movers that crush wait this
            // long before they hurt them again: whoever they are.)
            if (actor.hurtMs > 0)
                actor.hurtMs -= ms;
            if (actor.kind == static_cast<std::uint8_t>(ActorKind::Item))
            {
                if (player.health > 0 && detail::touchesPlayer(player, actor.pos, fxFromInt(12), fxFromInt(24)))
                {
                    if (a.shared)
                    {
                        // Everybody's: wanted, if it would be of any use
                        // (tried on a copy), and whoever runs the game says
                        // who has it (itemPickUp, then).
                        // (On a copy of what they carry, the Arsenal too.)
                        PlayerState trial = player;
                        Arsenal trialArsenal = player.arsenal != nullptr ? *player.arsenal : Arsenal();
                        trial.arsenal = player.arsenal != nullptr ? &trialArsenal : nullptr;
                        std::uint32_t got = 0;
                        // (Weapons stay: a weapon only for who has not got
                        // it -- what is with it is not taken by who has.)
                        const bool weapon = itemViewModel(static_cast<ItemKind>(actor.item)) != nullptr;
                        if (a.wanted == -1 && detail::itemTakeAt(w, trial, i, got) &&
                            (!w.weaponsStay.value || !weapon || (got & kGotWeapon)))
                            a.wanted = static_cast<std::int16_t>(i);
                    }
                    else
                        itemPickUp(w, player, i);
                }
                continue;
            }
            if (isTurret(actor.kind))
            {
                detail::turretTick(w, player, i, ms);
                actor.lastFrame = actor.frame;
                actorPlaceBox(a, i);
                continue;
            }
            if (isMine(actor.kind))
            {
                detail::mineTick(w, player, i, ms);
                continue;
            }
            // (Whoever a barnacle has hold of is where it has them.)
            if (aiHeld(a, i) && !actorThinks(actor.kind))
            {
                actor.lastFrame = actor.frame;
                actorPlaceBox(a, i);
                continue;
            }
            if (isPerson(actor.kind) || actor.kind == static_cast<std::uint8_t>(ActorKind::Gman))
            {
                // (No scene has them: they stand on what is under them -- a
                // lift, a train -- and go where it goes, or fall.)
                if (actor.script < 0 && actor.kind != static_cast<std::uint8_t>(ActorKind::Sitter) &&
                    w.brushStates != nullptr && w.movers != nullptr && !aiHeld(a, i))
                {
                    detail::peopleGround(w, actor, ms);
                    if (actor.airborne)
                    {
                        actorPlaceBox(a, i);
                        continue;
                    }
                }
            }
            if (!actorThinks(actor.kind))
                continue;
            aiTick(w, player, i, ms);
            actor.lastFrame = actor.frame;
            actorPlaceBox(a, i);
        }
        detail::projectilesTick(w, player, ms);
        effectsTick(w, player, ms);
    }

    // ------------------------------------------------------------- weapons

    /// What each gun is, in numbers: the game's (its weapons' own source,
    /// dlls/*.cpp and cl_dll/ev_hldm.cpp).
    struct GunKind
    {
        int holds;     ///< rounds in a clip
        int reloadMs;  ///< (the shotgun's is by the shell: see weaponsTick)
        int refireMs;
        int damage;    ///< of a round, or of a pellet
        int pellets;
        int cone;      ///< how far a round may go off line, in 4096ths: the game's cone
        int range;
        bool wet;      ///< fires under water (the pistol alone does)
        int emptyMs;   ///< the wait after a click
        CommonSound fire[2]; ///< one or the other
        int pitch, pitchWide; ///< of the shot: from `pitch` percent, `pitchWide` steps up
        const char *shoot;    ///< the hand's clip for a shot
    };
    // The game's cones, in 4096ths: 0.01 and 0.1 (the pistol's two), one
    // degree, three, ten.
    inline constexpr int kConePistol = 41, kConePistolFast = 410, kCone1 = 36, kCone3 = 107, kCone10 = 357;
    inline const GunKind &gunKind(std::uint8_t weapon)
    {
        static const GunKind kGlock = {17,  1500, 300, 8, 1, kConePistol, 4096, true, 200,
                                       {CommonSound::Pistol, CommonSound::Pistol}, 98, 4, "shoot"};
        static const GunKind kShotgun = {8,   500, 750, 5, 6, kCone10, 2048, false, 150,
                                         {CommonSound::Shotgun, CommonSound::Shotgun}, 93, 32, "shoot"};
        static const GunKind kMp5 = {50,  1500, 100, 5, 1, kCone3, 4096, false, 150,
                                     {CommonSound::Mp5Fire1, CommonSound::Mp5Fire}, 94, 16, "shoot"};
        static const GunKind kPython = {6,   2000, 750, 40, 1, kCone1, 4096, false, 150,
                                        {CommonSound::PythonFire, CommonSound::PythonFire2}, 100, 1, "fire1"};
        // (A bolt, not a round: the damage is the bolt's, kBoltDamage. Under
        // water it flies, slower.)
        static const GunKind kCrossbow = {5,   4500, 750, 50, 1, 0, 8192, true, 150,
                                          {CommonSound::XbowFire, CommonSound::XbowFire}, 93, 16, "fire1"};
        return weapon == kWeaponShotgun    ? kShotgun
               : weapon == kWeaponMp5      ? kMp5
               : weapon == kWeaponPython   ? kPython
               : weapon == kWeaponCrossbow ? kCrossbow
                                           : kGlock;
    }
    /// The clip of the gun in hand, and what it is refilled from; null for
    /// what is not a gun.
    inline std::int16_t *gunClip(Inventory &inv)
    {
        return inv.current == kWeaponGlock ? &inv.clipGlock
               : inv.current == kWeaponShotgun ? &inv.clipShotgun
               : inv.current == kWeaponMp5 ? &inv.clipMp5
               : inv.current == kWeaponPython ? &inv.clipPython : nullptr;
    }
    inline std::int16_t *gunSpare(Inventory &inv)
    {
        return inv.current == kWeaponGlock || inv.current == kWeaponMp5 ? &inv.ammo9mm
               : inv.current == kWeaponShotgun ? &inv.ammoBuck
               : inv.current == kWeaponPython ? &inv.ammo357 : nullptr;
    }
    /// The same, of all the guns: the crossbow's are the Arsenal's.
    inline std::int16_t *gunClip(PlayerState &player)
    {
        if (player.inv.current == kWeaponCrossbow)
            return player.arsenal != nullptr ? &player.arsenal->boltsClip : nullptr;
        return gunClip(player.inv);
    }
    inline std::int16_t *gunSpare(PlayerState &player)
    {
        if (player.inv.current == kWeaponCrossbow)
            return player.arsenal != nullptr ? &player.arsenal->bolts : nullptr;
        return gunSpare(player.inv);
    }
    /// Whether a weapon carried has nothing at all to fire (for the touch
    /// screen's list of a slot's weapons: such a one is shown grey). The
    /// crowbar and the hivehand, which grows its own, never.
    inline bool weaponDry(const PlayerState &player, int weapon)
    {
        const Inventory &inv = player.inv;
        const Arsenal *ars = player.arsenal;
        switch (weapon)
        {
        case kWeaponGlock: return inv.clipGlock + inv.ammo9mm <= 0;
        case kWeaponMp5: return inv.clipMp5 + inv.ammo9mm <= 0;
        case kWeaponShotgun: return inv.clipShotgun + inv.ammoBuck <= 0;
        case kWeaponPython: return inv.clipPython + inv.ammo357 <= 0;
        case kWeaponGrenade: return inv.grenades <= 0;
        case kWeaponCrossbow: return ars != nullptr && ars->boltsClip + ars->bolts <= 0;
        case kWeaponRpg: return ars != nullptr && !(ars->have & kArsenalRpgLoaded) && ars->rockets <= 0;
        case kWeaponGauss:
        case kWeaponEgon: return ars != nullptr && ars->uranium <= 0;
        case kWeaponSatchel: return ars != nullptr && ars->satchels <= 0;
        case kWeaponTripmine: return ars != nullptr && ars->tripmines <= 0;
        case kWeaponSnark: return ars != nullptr && ars->snarks <= 0;
        default: return false;
        }
    }

    /// What the weapon in hand that is not a gun has left: the grenades,
    /// satchels, tripmines, snarks (null: nothing counted).
    inline std::int16_t *weaponStock(PlayerState &player)
    {
        Arsenal *ars = player.arsenal;
        switch (player.inv.current)
        {
        case kWeaponGrenade:
            return &player.inv.grenades;
        case kWeaponSatchel:
            return ars != nullptr ? &ars->satchels : nullptr;
        case kWeaponTripmine:
            return ars != nullptr ? &ars->tripmines : nullptr;
        case kWeaponSnark:
            return ars != nullptr ? &ars->snarks : nullptr;
        case kWeaponGauss:
        case kWeaponEgon:
            return ars != nullptr ? &ars->uranium : nullptr;
        case kWeaponHornetgun:
            return ars != nullptr ? &ars->hornets : nullptr;
        default:
            return nullptr;
        }
    }
    inline constexpr std::uint32_t kWeaponSecondary = 16; ///< weaponsTick: its other fire (the launcher, both barrels)
    /// A spent case leaves the gun now (WeaponState::shells of them): at
    /// the shot for the pistol and the MP5, at the pump for the shotgun.
    inline constexpr std::uint32_t kWeaponShell = 32;

    struct WeaponState
    {
        std::int32_t cooldownMs = 0; ///< until it can be used again
        std::int32_t secondMs = 0;   ///< ... and its other fire
        std::int32_t reloadMs = 0;   ///< left of a reload under way
        std::uint32_t seed = 777;    ///< where the shotgun's pellets go
        /// The clip the hand should start, said by weaponsTick as it
        /// happens (null: none this tick). Whoever draws the hand plays it
        /// and sets this back to null.
        const char *anim = nullptr;
        // The crowbar, as the game keeps it:
        std::uint8_t swingMiss = 0;  ///< which of its three swings at the air is next
        std::uint8_t swingHit = 0;   ///< ... and of its two that land
        std::int32_t readyMs = 1000; ///< how long it has been ready: a blow after a second's rest is a whole one
        std::int32_t retryMs = 0;    ///< a swing that met nothing is tried again when this runs out
        std::uint8_t blow = 10;      ///< what the swing under way does
        std::int32_t dentMs = 0;     ///< until the mark of the last blow on a wall is made
        fx dentAt[3] = {0, 0, 0};
        fx dentNormal[3] = {0, 0, 0};
        std::int32_t holsterMs = 0;  ///< while it is being put away
        std::uint8_t taking = 0;     ///< ... for this weapon
        // The guns and the grenade, as the game keeps them:
        std::uint8_t shells = 0;     ///< how many spent cases kWeaponShell is for
        std::int32_t pumpMs = 0;     ///< the shotgun: until it is pumped after a shot
        std::uint8_t pumpShells = 0; ///< ... and how many cases come out then
        /// The shotgun's loading, a shell at a time: 0 not loading; 1
        /// between shells (the next goes in when `loadMs` runs out, or the
        /// pump ends it); 2 a shell is going in.
        std::uint8_t loading = 0;
        std::int32_t loadMs = 0;
        /// The grenade: how long ago its pin was pulled (-1: it is not).
        /// It goes off three seconds after, in the hand or out of it.
        std::int32_t pinMs = -1;
        bool redraw = false;         ///< the next grenade is to be taken out
        std::int32_t idleMs = 1000;  ///< until the hand next does something of its own
        bool clicked = false;        ///< the click has been heard for this pull of the trigger
        /// The weapon these belong to (a kWeapon*): another in hand, by
        /// whatever means, and they start afresh.
        std::uint8_t minded = 0;
    };
    inline constexpr int kGrenadeFuse = 3000;  ///< from the pin, as the game has it
    inline constexpr int kGrenadePull = 500;   ///< the pin takes this long: no throw sooner
    inline constexpr int kCrowbarReach = 32;  ///< the game's: units along the view
    inline constexpr int kCrowbarBlow = 10;   ///< a whole blow; a follow-up is half
    // What a swing of the crowbar met (detail::crowbarBlow).
    inline constexpr int kMetNothing = 0;
    inline constexpr int kMetFlesh = 1;   ///< something that bleeds
    inline constexpr int kMetThing = 2;   ///< a machine, or something that breaks
    inline constexpr int kMetWall = 3;
    inline constexpr int kMetKilled = 4;  ///< flesh, and that was the end of it

    /// Which part of the actor a line from `from` to `to` strikes first
    /// (kHit*): the model is posed as the actor stands, and the line tried
    /// against each of its hit boxes, in the box's bone's own space. A line
    /// that goes between them all, or a model that has none, is a hit to
    /// the body: whoever fired was near enough.
    inline int hitGroup(const ActorWorld &a, const ActorState &actor, const fx *from, const fx *to)
    {
        const ModelView *model = actorModel(a, actor);
        if (model == nullptr || model->header->hitboxCount == 0 || actor.clip < 0)
            return kHitBody;
        static BoneMatrix bones[kModelMaxBones];
        std::int32_t controls[kModelControls];
        const bool controlled = actorControls(actor, controls);
        poseModel(*model, actor.clip, actor.frame, bones, controlled ? controls : nullptr, actor.blend.value);
        // Into the model's space: back through the way the actor is turned
        // (a turret under a ceiling is turned over as well: ModelDraw).
        fx sine, cosine;
        fxSinCos(actor.yaw, sine, cosine);
        const bool over = isTurret(actor.kind) && (actor.flags & kActorCeiling) != 0;
        fx ends[2][3];
        for (int e = 0; e < 2; ++e)
        {
            const fx *p = e == 0 ? from : to;
            const fx d[3] = {p[0] - actor.pos[0], p[1] - actor.pos[1], p[2] - actor.pos[2]};
            ends[e][0] = fxMul(d[0], cosine) + fxMul(d[1], sine);
            ends[e][1] = over ? fxMul(d[0], sine) - fxMul(d[1], cosine) : fxMul(d[1], cosine) - fxMul(d[0], sine);
            ends[e][2] = over ? -d[2] : d[2];
        }
        int group = kHitBody;
        std::int64_t nearest = kFractionOne + 1;
        for (int n = 0; n < model->header->hitboxCount; ++n)
        {
            const ModelHitbox &box = model->hitboxes[n];
            const BoneMatrix &bone = bones[box.bone];
            fx local[2][3];
            for (int e = 0; e < 2; ++e)
            {
                const fx d[3] = {ends[e][0] - bone.t[0], ends[e][1] - bone.t[1], ends[e][2] - bone.t[2]};
                for (int k = 0; k < 3; ++k)
                    local[e][k] = static_cast<fx>((static_cast<std::int64_t>(bone.r[k]) * d[0] +
                                                   static_cast<std::int64_t>(bone.r[3 + k]) * d[1] +
                                                   static_cast<std::int64_t>(bone.r[6 + k]) * d[2]) >>
                                                  kBoneRotShift);
            }
            fx mins[3], maxs[3];
            for (int k = 0; k < 3; ++k)
            {
                mins[k] = static_cast<fx>(box.mins[k]) << (kFxShift - kHitboxShift);
                maxs[k] = static_cast<fx>(box.maxs[k]) << (kFxShift - kHitboxShift);
            }
            std::int64_t enter;
            if (lineEntersBox(local[0], local[1], mins, maxs, enter) && enter < nearest)
            {
                nearest = enter;
                group = box.group;
            }
        }
        return group;
    }

    /// Where the actor's model's attachment `which` is in the world, posed as
    /// it is drawn (the game's GetAttachment): a gun's muzzle, in the hand
    /// that holds it. False if its model has no such.
    inline bool actorAttachment(const ActorWorld &a, const ActorState &actor, int which, fx *out)
    {
        const ModelView *model = actorModel(a, actor);
        if (model == nullptr || actor.clip < 0 || which >= model->header->attachmentCount)
            return false;
        static BoneMatrix bones[kModelMaxBones];
        std::int32_t controls[kModelControls];
        const bool controlled = actorControls(actor, controls);
        poseModel(*model, actor.clip, actor.frame, bones, controlled ? controls : nullptr, actor.blend.value);
        fx at[3];
        if (!attachmentPoint(*model, bones, which, at))
            return false;
        fx sine, cosine;
        fxSinCos(actor.yaw, sine, cosine);
        out[0] = actor.pos[0] + fxMul(at[0], cosine) - fxMul(at[1], sine);
        out[1] = actor.pos[1] + fxMul(at[0], sine) + fxMul(at[1], cosine);
        out[2] = actor.pos[2] + at[2];
        return true;
    }

    /// What a shot along `dir` (Q12, unit) from `eye` lands on within
    /// `range` units, and hurts: a monster, or something that breaks. True
    /// if it hurt anything.
    inline bool fireLine(LogicWorld &w, PlayerState &player, const fx *eye, const fx *dir, int range,
                         int damage, bool bullet, bool mark = true, int *met = nullptr, int from = -1,
                         fx *reached = nullptr, std::uint32_t type = kDmgGeneric)
    {
        if (type == kDmgGeneric && bullet)
            type = kDmgBullet;
        if (met != nullptr)
            *met = kMetNothing;
        const fx end[3] = {eye[0] + dir[0] * range, eye[1] + dir[1] * range, eye[2] + dir[2] * range};
        const Shot shot = traceShot(*w.world, eye, end);
        // The nearest monster the line goes through before that.
        int struck = -1;
        std::int64_t nearest = shot.fraction;
        const ActorWorld *a = w.actors;
        for (int i = 0; a != nullptr && i < a->count; ++i)
        {
            const ActorState &actor = a->states[i];
            // (`from`: the actor whose shot it is, if it is not the
            // player's -- who does not shoot himself.)
            if (!actor.active || !canBeHurt(actor.kind) || actor.ai >= kAiDying || i == from ||
                (actor.kind == static_cast<std::uint8_t>(ActorKind::Generic) && !(actor.flags & kActorSolid)))
                continue;
            fx mins[3], maxs[3];
            targetBox(actor, mins, maxs);
            std::int64_t enter = 0;
            const bool miss = !lineEntersBox(eye, end, mins, maxs, enter);
            if (!miss && enter < nearest)
            {
                nearest = enter;
                struck = i;
            }
        }
        // (Where it got to, for whoever draws it: a beam's end.)
        for (int i = 0; reached != nullptr && i < 3; ++i)
            reached[i] = eye[i] + static_cast<fx>((static_cast<std::int64_t>(end[i] - eye[i]) * nearest) >> kFractionShift);
        if (struck >= 0)
        {
            // Where it landed, what bleeds bleeds (people red, the aliens
            // yellow) -- and on the wall or the floor behind it, if there
            // is one near: more of it for the blow that kills.
            const std::uint8_t kind = a->states[struck].kind;
            if (bloodOf(kind) != kBloodNone)
            {
                fx at[3];
                for (int i = 0; i < 3; ++i)
                    at[i] = eye[i] + static_cast<fx>((static_cast<std::int64_t>(end[i] - eye[i]) * nearest) >>
                                                     kFractionShift);
                if (bullet && w.heard != nullptr)
                    w.heard->add(CommonSound::BulletFlesh, at, 90, kRadiusSmall);
                monsterHurt(w, struck, damage, bullet, hitGroup(*a, a->states[struck], eye, end), kDmgUnset,
                            from >= 0 ? static_cast<std::int16_t>(from) : kAiPlayer,
                            from >= 0 ? a->states[from].pos : nullptr);
                const bool killed = a->states[struck].ai >= kAiDying || !a->states[struck].active;
                if (met != nullptr)
                    *met = killed ? kMetKilled : kMetFlesh;
                bleed(w, struck, at, dir, killed || damage >= 40);
                return true;
            }
            monsterHurt(w, struck, damage, bullet, hitGroup(*a, a->states[struck], eye, end), kDmgUnset,
                        from >= 0 ? static_cast<std::int16_t>(from) : kAiPlayer,
                        from >= 0 ? a->states[from].pos : nullptr);
            if (met != nullptr)
                *met = kMetThing;
            return true;
        }
        const int taker = shot.mover >= 0 ? detail::blowTaker(w, shot.mover) : -1;
        if (taker >= 0)
        {
            w.struckFrom[0] = dir[0];
            w.struckFrom[1] = dir[1];
            w.struckFrom[2] = dir[2];
            logicStrike(w, player, taker, damage, type, from < 0);
            if (met != nullptr)
                *met = kMetThing;
            // Glass that takes it and stands (the marks are the
            // brush's: they go with it). Glass that will break is
            // cracked where it was hit -- one crack to a pane, moved
            // by the next hit. Glass that never breaks is not: a
            // shot leaves its small mark on it, a blow nothing but
            // dust.
            const std::uint32_t made = w.records[taker].extra[1];
            fx at[3], normal[3];
            if (w.actors != nullptr && !w.states[taker].dead && (made == 0 || made == 7) &&
                w.brushStates != nullptr && traceShotWall(*w.world, eye, end, at, normal))
            {
                fx local[3];
                for (int k = 0; k < 3; ++k)
                    local[k] = at[k] - w.brushStates[shot.mover].pos[k];
                if (made == 0)
                    decalAdd(*w.actors, kDecalCrack, local, normal, 20, kMarkCrack, shot.mover);
                else if (bullet)
                    decalAdd(*w.actors, static_cast<std::uint8_t>(kDecalShot + detail::chance(*w.actors) % kDecalShots),
                             local, normal, 4, kMarkCrack, shot.mover);
                specksAdd(*w.actors, at, normal, 2, static_cast<std::uint16_t>(24 | 26 << 5 | 27 << 10), 60);
            }
            return true;
        }
        // On the world, or on something that does not break: its mark, its
        // dust, and now and then the whine of it glancing off.
        if (met != nullptr && shot.fraction < kFractionOne)
            *met = kMetWall;
        if (mark && w.actors != nullptr && shot.fraction < kFractionOne && markShot(*w.actors, *w.world, eye, end, !bullet) &&
            bullet && w.heard != nullptr && detail::chance(*w.actors) % 3 == 0)
        {
            fx at[3];
            for (int i = 0; i < 3; ++i)
                at[i] = eye[i] + static_cast<fx>((static_cast<std::int64_t>(end[i] - eye[i]) * shot.fraction) >> kFractionShift);
            w.heard->add(detail::chance(*w.actors) & 1 ? CommonSound::Ricochet1 : CommonSound::Ricochet2, at, 100,
                         kRadiusMedium);
        }
        return false;
    }

    // ------------------------------------------------------- guns on mounts
    //
    // func_tank and its kinds (func_tank.cpp), the controls a player takes
    // them by, and func_mortar_field (mortar.cpp). The gun is a brush that
    // turns about its origin: LogicKind::Tank says what of the map's keys it
    // keeps where. Its state: `bits` how it is turned now (yaw, and pitch <<
    // 16, the brush's way: positive tipped down); `base` where it is;
    // `travel` where it last saw what it aims at (m_sightOrigin) -- or, at a
    // player's hands, the yaw and pitch they look at; `timer` the ms gathered
    // towards its next round (-1: not firing -- the game's m_fireLast 0);
    // `speed` the ms since it last saw its target (m_lastSightTime);
    // `running` whether it is on; `phase` 1 while a player has it; `move`
    // whether its turning sound is on. A TankControls' `bits` is the tank it
    // drives, plus one, while it is taken, and its `base` where the player
    // took it from.

    namespace detail
    {
        inline std::uint16_t tankYaw(const LogicState &state) { return static_cast<std::uint16_t>(state.bits); }
        inline std::uint16_t tankPitch(const LogicState &state) { return static_cast<std::uint16_t>(state.bits >> 16); }

        /// Its forward, right and up, as it is turned.
        inline void tankAxes(const LogicState &state, fx *forward, fx *right, fx *up)
        {
            fx sy, cy, sp, cp;
            fxSinCos(tankYaw(state), sy, cy);
            fxSinCos(tankPitch(state), sp, cp);
            forward[0] = fxMul(cp, cy);
            forward[1] = fxMul(cp, sy);
            forward[2] = -sp; // (tipped down is positive)
            right[0] = sy;
            right[1] = -cy;
            right[2] = 0;
            up[0] = fxMul(sp, cy);
            up[1] = fxMul(sp, sy);
            up[2] = cp;
        }

        /// BarrelPosition: the end of its barrel, as it is turned.
        inline void tankBarrel(const LogicWorld &w, int index, fx *at)
        {
            const LogicRecord &record = w.records[index];
            const LogicState &state = w.states[index];
            fx forward[3], right[3], up[3];
            tankAxes(state, forward, right, up);
            for (int a = 0; a < 3; ++a)
                at[a] = state.base[a] + fxMul(forward[a], record.move[0]) + fxMul(right[a], record.move[1]) +
                        fxMul(up[a], record.move[2]);
        }

        /// A round's damage by the kind of bullet (the game's monster
        /// rounds, at its level), unless the map gives its own.
        inline int tankDamage(const LogicWorld &w, const LogicRecord &record)
        {
            if (record.damage != 0)
                return record.damage;
            switch ((record.extra[2] >> 24) & 3)
            {
            case kTankBullet9mm: return skillOf(w).monster9mmBullet;
            case kTankBulletMp5: return skillOf(w).monster9mmARBullet;
            case kTankBullet12mm: return skillOf(w).monster12mmBullet;
            default: return 0;
            }
        }

        /// TankTrace's spread: the game's cones (none, then 0.025, 0.05,
        /// 0.1 and 0.25), two throws added either way.
        inline void tankAim(ActorWorld &a, const LogicRecord &record, const LogicState &state, fx *dir)
        {
            static constexpr fx kCones[5] = {0, 102, 205, 410, 1024};
            const fx cone = kCones[((record.extra[2] >> 26) & 7) % 5];
            fx forward[3], right[3], up[3];
            tankAxes(state, forward, right, up);
            const int x = static_cast<int>(chance(a) % 2049) - 1024 + static_cast<int>(chance(a) % 2049) - 1024;
            const int y = static_cast<int>(chance(a) % 2049) - 1024 + static_cast<int>(chance(a) % 2049) - 1024;
            for (int k = 0; k < 3; ++k)
                dir[k] = forward[k] + static_cast<fx>((static_cast<std::int64_t>(right[k]) * x * cone +
                                                       static_cast<std::int64_t>(up[k]) * y * cone) >> 22);
            const fx length = fxLength(dir);
            for (int k = 0; length > 0 && k < 3; ++k)
                dir[k] = static_cast<fx>((static_cast<std::int64_t>(dir[k]) << kFxShift) / length);
        }

        /// One round of what it fires, from `barrel`, along where it points
        /// (by its spread): a player's at its controls (`manned`) hurts
        /// whatever it meets as the player's own shots do; one of its own
        /// is after the player.
        inline void tankShoot(LogicWorld &w, PlayerState &player, int index, const fx *barrel, bool manned)
        {
            const LogicRecord &record = w.records[index];
            const LogicState &state = w.states[index];
            ActorWorld &a = *w.actors;
            const int variant = static_cast<int>((record.extra[2] >> 29) & 3);
            fx dir[3];
            tankAim(a, record, state, dir);
            const fx far[3] = {barrel[0] + dir[0] * 4096, barrel[1] + dir[1] * 4096, barrel[2] + dir[2] * 4096};
            if (variant == kTankRocket)
            {
                // rpg_rocket, as the gun points.
                fx forward[3], right[3], up[3];
                tankAxes(state, forward, right, up);
                const fx velocity[3] = {forward[0] * 250, forward[1] * 250, forward[2] * 250};
                projectileAdd(a, manned ? kShotRocket : kShotRocketFoe, barrel, velocity, 30000);
                if (w.heard != nullptr)
                    w.heard->add(CommonSound::RocketFire, barrel, 127, kRadiusLarge);
                return;
            }
            // Where the line stops: the world, or the player.
            const Shot wall = traceShot(*w.world, barrel, far);
            std::int64_t reach = wall.fraction;
            bool atPlayer = false;
            if (!manned && player.health > 0)
            {
                const fx tall = player.ducked ? kCrouchedFeet : kStandingFeet;
                const fx lo[3] = {player.origin[0] - fxFromInt(16), player.origin[1] - fxFromInt(16), player.origin[2] - tall};
                const fx hi[3] = {player.origin[0] + fxFromInt(16), player.origin[1] + fxFromInt(16), player.origin[2] + tall};
                std::int64_t enter;
                if (lineEntersBox(barrel, far, lo, hi, enter) && enter < reach)
                {
                    reach = enter;
                    atPlayer = true;
                }
            }
            fx end[3];
            for (int k = 0; k < 3; ++k)
                end[k] = barrel[k] + static_cast<fx>((static_cast<std::int64_t>(far[k] - barrel[k]) * reach) >> kFractionShift);
            const fx gone[3] = {end[0] - barrel[0], end[1] - barrel[1], end[2] - barrel[2]};
            const int range = fxToInt(fxLength(gone)) + 1;
            if (variant == kTankMortar)
            {
                // A shell where the line meets something: the map's blast.
                explodeAt(w, player, end, record.mins[2] > 0 ? record.mins[2] : 100, true, true, true);
                return;
            }
            int damage = tankDamage(w, record);
            std::uint16_t colour = 0;
            if (variant == kTankLaser)
            {
                // Its env_laser, fired at where the line stops: as much as
                // that does in a second, at once (FireAtPoint).
                damage = 0;
                for (int r = 0; r < w.recordCount && record.netName != 0; ++r)
                {
                    if (w.records[r].name == record.netName && w.records[r].kind == static_cast<std::uint8_t>(LogicKind::Beam))
                    {
                        damage = w.records[r].damage;
                        colour = static_cast<std::uint16_t>(w.records[r].extra[2] & 0x7FFF); // (effectColour)
                        break;
                    }
                }
                Effect &beam = effectAdd(a, kEffectBeam, barrel, end, 4, 150);
                beam.colour = colour != 0 ? colour : 0x7C1F;
            }
            else
            {
                effectAdd(a, kEffectTracer, barrel, end, 0, 60);
            }
            if (atPlayer)
            {
                if (damage > 0)
                {
                    playerHurt(player, damage, barrel, kDmgBullet);
                    a.got |= kGotHurt;
                }
                return;
            }
            // Short of the player: whatever is in the way, or the wall.
            if (damage > 0)
                fireLine(w, player, barrel, dir, range + 8, damage, variant == kTankGun);
        }

        /// Fire: the rounds its rate has gathered since it began, each the
        /// map's target (its sound) with it. The first frame of firing is
        /// only a start (the game's m_fireLast 0).
        inline void tankFire(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            const int rate = static_cast<int>(record.extra[2] & 0xFFFF); // (sixteenths a second)
            if (state.timer < 0)
            {
                state.timer = 0;
                return;
            }
            state.timer += ms;
            const int each = rate > 0 ? 16000 / rate : 1000;
            if (state.timer < each)
                return;
            fx barrel[3];
            tankBarrel(w, index, barrel);
            for (; state.timer >= each; state.timer -= each)
                tankShoot(w, player, index, barrel, false);
            effectAdd(*w.actors, kEffectMuzzle, barrel, nullptr, 0, 50);
            logicUseTargets(w, player, index, kUseToggle, 0);
        }

        /// AdjustAnglesForBarrel: a barrel off to one side or up aims for
        /// where its end will be.
        inline void tankBarrelAdjust(const LogicRecord &record, fx distance, int &yaw, int &pitchUp)
        {
            const fx by = record.move[1], bz = record.move[2];
            if (by == 0 && bz == 0)
                return;
            const fx d = distance - bz;
            if (by != 0 && d > (by < 0 ? -by : by))
            {
                const std::int64_t rest = static_cast<std::int64_t>(d) * d - static_cast<std::int64_t>(by) * by;
                yaw += static_cast<std::int16_t>(fxAtan2Turns(by, static_cast<fx>(isqrt64(static_cast<std::uint64_t>(rest)))));
            }
            if (bz != 0 && d > (bz < 0 ? -bz : bz))
            {
                const std::int64_t rest = static_cast<std::int64_t>(d) * d - static_cast<std::int64_t>(bz) * bz;
                pitchUp += static_cast<std::int16_t>(fxAtan2Turns(-bz, static_cast<fx>(isqrt64(static_cast<std::uint64_t>(rest)))));
            }
        }

        /// TrackTarget, each frame: turned towards the player while it is on
        /// (or where the player at its controls looks), within how far it
        /// goes and as fast as it turns; and, on target, firing.
        inline void tankTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            if (record.brush == kNoBrush || state.dead)
                return;
            const bool manned = state.phase == 1;
            auto rotSound = [&](bool on)
            {
                const bool was = state.move == MoveState::GoingUp;
                if (on == was)
                    return;
                state.move = on ? MoveState::GoingUp : MoveState::AtBottom;
                if (w.heard == nullptr || w.sounds == nullptr || w.sounds[index].move == kNoSound)
                    return;
                if (on)
                    w.heard->add(w.sounds[index].move, state.base, 108, kRadiusMedium, kSoundLoopOn, 100,
                                 static_cast<std::uint16_t>(index));
                else
                    w.heard->add(kNoSound, nullptr, 0, 0, kSoundLoopOff, 100, static_cast<std::uint16_t>(index));
            };
            if (!manned && !state.running)
            {
                rotSound(false);
                return;
            }
            const auto restYaw = static_cast<std::uint16_t>(record.extra[0]);
            const auto restPitch = static_cast<std::uint16_t>(record.extra[0] >> 16);
            auto degrees = [](std::uint32_t byte) { return static_cast<int>(byte & 255) * 65536 / 360; };
            const int yawRange = degrees(record.extra[1]), pitchRange = degrees(record.extra[1] >> 8);
            const int yawTolerance = degrees(record.extra[1] >> 16), pitchTolerance = degrees(record.extra[1] >> 24);
            const int yawRate = static_cast<int>((static_cast<std::int64_t>(record.speed) * 65536) / (360 * kFxOne));
            const int pitchRate = degrees(record.extra[2] >> 16);
            int wantYaw = 0, wantPitch = 0;
            bool updateTime = false;
            fx barrel[3];
            tankBarrel(w, index, barrel);
            fx distance = 0;
            if (manned)
            {
                wantYaw = static_cast<std::uint16_t>(state.travel[0]);
                wantPitch = static_cast<std::int16_t>(state.travel[1]);
            }
            else
            {
                if (player.health <= 0 || unseen(player))
                {
                    rotSound(false);
                    state.timer = -1;
                    return;
                }
                const fx eye[3] = {player.origin[0], player.origin[1], player.origin[2] + player.viewHeight};
                const fx apart[3] = {eye[0] - barrel[0], eye[1] - barrel[1], eye[2] - barrel[2]};
                const fx range = fxLength(apart);
                if (range < fxFromInt(record.mins[0]) || (record.mins[1] > 0 && range > fxFromInt(record.mins[1])))
                {
                    rotSound(false);
                    return;
                }
                if (traceShot(*w.world, barrel, eye).fraction >= kFractionOne)
                {
                    updateTime = true;
                    for (int k = 0; k < 3; ++k)
                        state.travel[k] = player.origin[k];
                }
                const fx d[3] = {state.travel[0] - state.base[0], state.travel[1] - state.base[1],
                                 state.travel[2] - state.base[2]};
                const fx flat[3] = {d[0], d[1], 0};
                distance = fxLength(d);
                wantYaw = fxAtan2Turns(d[1], d[0]);
                int pitchUp = static_cast<std::int16_t>(fxAtan2Turns(d[2], fxLength(flat)));
                tankBarrelAdjust(record, distance, wantYaw, pitchUp);
                wantPitch = -pitchUp;
            }
            // Within its reach either way of how it rests.
            int fromRest = static_cast<std::int16_t>(wantYaw - restYaw);
            if (fromRest > yawRange || fromRest < -yawRange)
            {
                fromRest = fromRest > 0 ? yawRange : -yawRange;
                updateTime = false; // (seen, but out of its reach)
            }
            if (updateTime)
                state.speed = 0;
            else if (state.speed < fxFromInt(30000))
                state.speed += ms;
            int pitchFromRest = static_cast<std::int16_t>(wantPitch - restPitch);
            pitchFromRest = pitchFromRest > pitchRange ? pitchRange : (pitchFromRest < -pitchRange ? -pitchRange : pitchFromRest);
            // Ten times the way still to go, a second, up to its rates.
            const int distY = static_cast<std::int16_t>(restYaw + fromRest - tankYaw(state));
            const int distX = static_cast<std::int16_t>(restPitch + pitchFromRest - tankPitch(state));
            auto turn = [&](int dist, int rate)
            {
                std::int64_t v = static_cast<std::int64_t>(dist) * 10;
                v = v > rate ? rate : (v < -rate ? -rate : v);
                const std::int64_t step = v * ms / 1000;
                return static_cast<int>(step == 0 && dist != 0 ? (dist > 0 ? 1 : -1) * (std::abs(dist) < 2 ? std::abs(dist) : 1) : step);
            };
            const int turnY = turn(distY, yawRate), turnX = turn(distX, pitchRate);
            if (turnY != 0 || turnX != 0)
            {
                const std::uint16_t yaw = static_cast<std::uint16_t>(tankYaw(state) + turnY);
                const std::uint16_t pitch = static_cast<std::uint16_t>(tankPitch(state) + turnX);
                state.bits = yaw | (static_cast<std::uint32_t>(pitch) << 16);
                const std::uint16_t angles[3] = {pitch, yaw, 0};
                logicPlaceBrushTurned(w, record.brush, state.base, angles);
            }
            // (Its sound while it turns more than a degree a second.)
            rotSound(std::abs(turnY) * 1000 > 182 * ms || std::abs(turnX) * 1000 > 182 * ms);
            if (manned)
                return;
            // On target -- or, for one that fires at whatever it can see,
            // at it -- for as long as it has seen it lately.
            const bool canFire = state.speed < static_cast<fx>(record.extra[3]);
            const bool los = (record.maxs[0] & kTankLineOfSight) != 0;
            const bool onTarget = std::abs(distX) < pitchTolerance && std::abs(distY) < yawTolerance;
            if (!canFire || !(onTarget || los))
            {
                state.timer = -1;
                return;
            }
            bool fire = true;
            if (los)
            {
                fx forward[3], right[3], up[3];
                tankAxes(state, forward, right, up);
                const fx reach = distance + fxFromInt(16);
                const fx to[3] = {barrel[0] + fxMul(forward[0], reach), barrel[1] + fxMul(forward[1], reach),
                                  barrel[2] + fxMul(forward[2], reach)};
                const fx tall = player.ducked ? kCrouchedFeet : kStandingFeet;
                const fx lo[3] = {player.origin[0] - fxFromInt(16), player.origin[1] - fxFromInt(16), player.origin[2] - tall};
                const fx hi[3] = {player.origin[0] + fxFromInt(16), player.origin[1] + fxFromInt(16), player.origin[2] + tall};
                std::int64_t enter;
                const Shot wall = traceShot(*w.world, barrel, to);
                fire = lineEntersBox(barrel, to, lo, hi, enter) && enter <= wall.fraction;
            }
            if (fire)
                tankFire(w, player, index, ms);
            else
                state.timer = -1;
        }

        /// A mortar field's shells, a frame on (CFuncMortarField::FieldUse
        /// and CMortar): called in, the spot is chosen and the whistle heard;
        /// two and a half seconds on, a shell at a time, each a moment after
        /// the one before, each about the spot as far as its spread.
        inline void mortarTick(LogicWorld &w, PlayerState &player, int index, int ms)
        {
            const LogicRecord &record = w.records[index];
            LogicState &state = w.states[index];
            ActorWorld &a = *w.actors;
            auto random = [&](int lo, int hi) { return hi <= lo ? lo : lo + static_cast<int>(chance(a) % static_cast<std::uint32_t>(hi - lo + 1)); };
            if (state.running)
            {
                state.running = false;
                // Where: by chance within it; on who called it; or by its
                // two wheels.
                fx spot[3] = {fxFromInt(random(record.mins[0], record.maxs[0])),
                              fxFromInt(random(record.mins[1], record.maxs[1])), fxFromInt(record.maxs[2])};
                if (record.extra[2] == 1)
                {
                    spot[0] = state.base[0];
                    spot[1] = state.base[1];
                }
                else if (record.extra[2] == 2)
                {
                    const std::uint16_t wheels[2] = {static_cast<std::uint16_t>(record.extra[3]),
                                                     static_cast<std::uint16_t>(record.extra[3] >> 16)};
                    for (int axis = 0; axis < 2; ++axis)
                    {
                        for (int r = 0; r < w.recordCount && wheels[axis] != 0; ++r)
                        {
                            if (w.records[r].name != wheels[axis])
                                continue;
                            const std::int64_t size = record.maxs[axis] - record.mins[axis];
                            spot[axis] = fxFromInt(record.mins[axis]) +
                                         static_cast<fx>((fxFromInt(1) * size * w.states[r].phase) >> 16);
                            break;
                        }
                    }
                }
                for (int k = 0; k < 3; ++k)
                    state.base[k] = spot[k];
                state.phase = static_cast<std::int32_t>(record.extra[0]);
                state.timer = 2500;
                if (w.heard != nullptr && w.sounds != nullptr && w.sounds[index].move != kNoSound)
                    w.heard->add(w.sounds[index].move, nullptr, 127, kRadiusEverywhere, 0, random(95, 124));
                const fx below[3] = {spot[0], spot[1], spot[2] - fxFromInt(4096)};
                const Shot ground = traceShot(*w.world, spot, below);
                fx landing[3];
                for (int k = 0; k < 3; ++k)
                    landing[k] = spot[k] + static_cast<fx>((static_cast<std::int64_t>(below[k] - spot[k]) * ground.fraction) >> kFractionShift);
                aiSound(a, kSoundDanger, landing, 400, 300);
            }
            if (state.phase <= 0)
                return;
            state.timer -= ms;
            while (state.timer <= 0 && state.phase > 0)
            {
                const int spread = static_cast<int>(record.extra[1]);
                const fx from[3] = {state.base[0] + fxFromInt(random(-spread, spread)),
                                    state.base[1] + fxFromInt(random(-spread, spread)), state.base[2]};
                const fx below[3] = {from[0], from[1], from[2] - fxFromInt(4096)};
                const Shot ground = traceShot(*w.world, from, below);
                fx at[3];
                for (int k = 0; k < 3; ++k)
                    at[k] = from[k] + static_cast<fx>((static_cast<std::int64_t>(below[k] - from[k]) * ground.fraction) >> kFractionShift);
                // MortarExplode: its streak of light from the sky, and a
                // blast of 200.
                const fx sky[3] = {at[0], at[1], at[2] + fxFromInt(1024)};
                Effect &streak = effectAdd(a, kEffectBeam, at, sky, 4, 120);
                streak.colour = 0x3A9F; // (255, 160, 100)
                at[2] += fxFromInt(8);
                explodeAt(w, player, at, 200, true, true, true);
                if (w.heard != nullptr && w.sounds != nullptr && w.sounds[index].stop != kNoSound)
                    w.heard->add(w.sounds[index].stop, at, 127, kRadiusLarge, 0, random(95, 124));
                --state.phase;
                state.timer += random(200, 500);
            }
        }
    } // namespace detail

    /// The tank a player has at its controls, or -1.
    inline int tankManned(const LogicWorld &w)
    {
        for (int i = 0; i < w.recordCount; ++i)
        {
            if (w.records[i].kind == static_cast<std::uint8_t>(LogicKind::TankControls) && w.states[i].bits != 0)
                return static_cast<int>(w.states[i].bits) - 1;
        }
        return -1;
    }

    /// Let go of the controls, if a player has any.
    inline void tankRelease(LogicWorld &w)
    {
        for (int i = 0; i < w.recordCount; ++i)
        {
            LogicState &controls = w.states[i];
            if (w.records[i].kind != static_cast<std::uint8_t>(LogicKind::TankControls) || controls.bits == 0)
                continue;
            const int tank = static_cast<int>(controls.bits) - 1;
            controls.bits = 0;
            if (tank >= 0 && tank < w.recordCount)
            {
                w.states[tank].phase = 0;
                w.states[tank].timer = -1;
            }
        }
    }

    /// USE: at a tank's controls, take them (StartControl) -- or, having
    /// them, let go (StopControl). False if neither: USE is something
    /// else's.
    inline bool tankUse(LogicWorld &w, const PlayerState &player)
    {
        if (tankManned(w) >= 0)
        {
            tankRelease(w);
            return true;
        }
        for (int i = 0; i < w.recordCount; ++i)
        {
            const LogicRecord &record = w.records[i];
            if (record.kind != static_cast<std::uint8_t>(LogicKind::TankControls) || w.states[i].dead)
                continue;
            const fx mins[3] = {fxFromInt(record.mins[0]), fxFromInt(record.mins[1]), fxFromInt(record.mins[2])};
            const fx maxs[3] = {fxFromInt(record.maxs[0]), fxFromInt(record.maxs[1]), fxFromInt(record.maxs[2])};
            if (!detail::playerTouchesBox(player, mins, maxs, 0))
                continue;
            for (int t = 0; t < w.recordCount && record.target != 0; ++t)
            {
                const LogicRecord &tank = w.records[t];
                if (tank.name != record.target || tank.kind != static_cast<std::uint8_t>(LogicKind::Tank) ||
                    !(tank.maxs[0] & kTankCanControl) || w.states[t].dead || !logicMasterAllows(w, t))
                    continue;
                w.states[i].bits = static_cast<std::uint32_t>(t) + 1;
                for (int a = 0; a < 3; ++a)
                    w.states[i].base[a] = player.origin[a];
                LogicState &state = w.states[t];
                state.phase = 1;
                state.timer = 0;
                state.travel[0] = static_cast<fx>(detail::tankYaw(state));
                state.travel[1] = static_cast<std::int16_t>(detail::tankPitch(state));
                return true;
            }
        }
        return false;
    }

    /// A frame of the player at a tank's controls (ControllerPostFrame,
    /// and TrackTarget's part of it): it turns to where they look, and fires
    /// while they hold fire, at its rate. They let go of it by going more
    /// than 30 units from where they took it, or by dying.
    inline void tankControl(LogicWorld &w, PlayerState &player, std::uint16_t viewYaw, std::int16_t viewPitch,
                            bool attack, int ms)
    {
        int controls = -1;
        for (int i = 0; i < w.recordCount && controls < 0; ++i)
        {
            if (w.records[i].kind == static_cast<std::uint8_t>(LogicKind::TankControls) && w.states[i].bits != 0)
                controls = i;
        }
        if (controls < 0)
            return;
        const int tank = static_cast<int>(w.states[controls].bits) - 1;
        const fx moved[3] = {player.origin[0] - w.states[controls].base[0], player.origin[1] - w.states[controls].base[1],
                             player.origin[2] - w.states[controls].base[2]};
        if (player.health <= 0 || fxLength(moved) >= fxFromInt(30) || tank < 0 || tank >= w.recordCount ||
            w.states[tank].dead)
        {
            tankRelease(w);
            return;
        }
        LogicState &state = w.states[tank];
        state.travel[0] = viewYaw;
        state.travel[1] = -viewPitch; // (the view looks up positive; the gun tips down positive)
        if (state.timer > 0)
            state.timer -= ms;
        if (!attack || state.timer > 0)
            return;
        const LogicRecord &record = w.records[tank];
        const int rate = static_cast<int>(record.extra[2] & 0xFFFF);
        state.timer = rate > 0 ? 16000 / rate : 1000;
        fx barrel[3];
        detail::tankBarrel(w, tank, barrel);
        detail::tankShoot(w, player, tank, barrel, true);
        effectAdd(*w.actors, kEffectMuzzle, barrel, nullptr, 0, 50);
        aiWeaponVolume(w, kLoudGunVolume);
        logicUseTargets(w, player, tank, kUseToggle, 0);
    }

    /// The next weapon the player has after the one in hand; kWeaponNone if
    /// they have none.
    inline std::uint8_t weaponAfter(const PlayerState &player)
    {
        for (int n = 1; n <= kWeaponCount; ++n)
        {
            const auto which = static_cast<std::uint8_t>((player.inv.current + n) % kWeaponCount);
            if (which != kWeaponNone && hasWeapon(player, which))
                return which;
        }
        return kWeaponNone;
    }
    inline std::uint8_t weaponAfter(const Inventory &inv)
    {
        PlayerState player;
        player.inv = inv;
        return weaponAfter(player);
    }

    /// The weapon `step` places on from the one in hand (1 the next, -1 the
    /// one before), among those the player has, going round: what to ask
    /// weaponsTick to take out. kWeaponNone for no step, or nothing else to
    /// take.
    inline std::uint8_t weaponStepped(const PlayerState &player, int step)
    {
        if (step == 0)
            return kWeaponNone;
        for (int n = 1; n < kWeaponCount; ++n)
        {
            const int at = (player.inv.current + (step > 0 ? n : kWeaponCount - n)) % kWeaponCount;
            if (at != kWeaponNone && at != player.inv.current && hasWeapon(player, at))
                return static_cast<std::uint8_t>(at);
        }
        return kWeaponNone;
    }
    inline std::uint8_t weaponStepped(const Inventory &inv, int step)
    {
        PlayerState player;
        player.inv = inv;
        return weaponStepped(player, step);
    }

    namespace detail
    {
        /// The crowbar's blow, as the game makes it: along the view for
        /// kCrowbarReach units; and if that meets nothing, a box the size of
        /// someone crouching swept the same way, which is what lets a swing
        /// land on what is beside the line and not on it. Hurts what it
        /// meets by `damage`; sounds as what it met sounds; a wall's dent is
        /// left for a fifth of a second later (WeaponState::dentMs). Returns
        /// a kMet*.
        inline int crowbarBlow(LogicWorld &w, PlayerState &player, WeaponState &state, const fx *eye,
                               const fx *forward, int damage)
        {
            int met = kMetNothing;
            fireLine(w, player, eye, forward, kCrowbarReach, damage, false, false, &met, -1, nullptr, kDmgClub);
            const fx end[3] = {eye[0] + forward[0] * kCrowbarReach, eye[1] + forward[1] * kCrowbarReach,
                               eye[2] + forward[2] * kCrowbarReach};
            fx probe[3] = {eye[0] + forward[0] * (kCrowbarReach + 16), eye[1] + forward[1] * (kCrowbarReach + 16),
                           eye[2] + forward[2] * (kCrowbarReach + 16)};
            bool breaks = false;
            if (met == kMetNothing && w.actors != nullptr)
            {
                // The box: whoever stands within half its width of the line.
                ActorWorld &a = *w.actors;
                int best = -1;
                std::int64_t nearest = kFractionOne;
                for (int i = 0; i < a.count; ++i)
                {
                    const ActorState &actor = a.states[i];
                    if (!actor.active || !canBeHurt(actor.kind) || actor.ai >= kAiDying)
                        continue;
                    fx mins[3], maxs[3];
                    targetBox(actor, mins, maxs);
                    for (int k = 0; k < 3; ++k)
                    {
                        mins[k] -= fxFromInt(k == 2 ? 18 : 16);
                        maxs[k] += fxFromInt(k == 2 ? 18 : 16);
                    }
                    std::int64_t enter = 0;
                    if (lineEntersBox(eye, end, mins, maxs, enter) && enter < nearest)
                    {
                        nearest = enter;
                        best = i;
                    }
                }
                if (best >= 0)
                {
                    monsterHurt(w, best, damage, false);
                    const ActorState &struck = a.states[best];
                    fx mins[3], maxs[3];
                    targetBox(struck, mins, maxs);
                    const fx at[3] = {struck.pos[0], struck.pos[1], (mins[2] + maxs[2]) / 2};
                    const bool killed = struck.ai >= kAiDying || !struck.active;
                    bleed(w, best, at, forward, killed);
                    met = bloodOf(struck.kind) == kBloodNone ? kMetThing : (killed ? kMetKilled : kMetFlesh);
                }
            }
            if (met == kMetNothing)
            {
                // ... and the world, and what stands in it.
                const Trace sweep = traceAll(*w.world, true, eye, end);
                if (!sweep.startSolid && sweep.fraction < kFractionOne)
                {
                    met = kMetWall;
                    // (Where on it: on from where the box stopped, into
                    // what stopped it.)
                    for (int k = 0; k < 3; ++k)
                        probe[k] = sweep.endPos[k] - static_cast<fx>((static_cast<std::int64_t>(sweep.normal[k]) * 28) >> 18);
                    if (sweep.mover >= 0)
                    {
                        for (int i = 0; i < w.recordCount; ++i)
                        {
                            if (w.records[i].brush == static_cast<std::uint16_t>(sweep.mover) && !w.states[i].dead &&
                                takesBlows(w.records[i]))
                            {
                                w.struckFrom[0] = forward[0];
                                w.struckFrom[1] = forward[1];
                                w.struckFrom[2] = forward[2];
                                logicStrike(w, player, i, damage, kDmgClub, true);
                                met = kMetThing;
                            }
                        }
                    }
                }
            }
            else if (met == kMetThing)
            {
                breaks = true;
            }
            if (w.heard == nullptr || met == kMetNothing)
                return met;

            if (met == kMetFlesh || met == kMetKilled)
            {
                // One of its three thuds.
                static constexpr CommonSound kThuds[] = {CommonSound::CrowbarFlesh, CommonSound::CrowbarFlesh2,
                                                         CommonSound::CrowbarFlesh3};
                w.heard->add(kThuds[w.actors != nullptr ? chance(*w.actors) % 3 : 0], nullptr, 127, 0);
                return met;
            }
            // A wall, or a thing: where, and what it is made of. Each
            // material has its own sound, as loud as the game has it, and
            // lets the crowbar's own ring more or less.
            fx at[3] = {end[0], end[1], end[2]}, normal[3] = {0, 0, 0};
            const bool found = traceShotWall(*w.world, eye, probe, at, normal);
            Material made = Material::Concrete;
            if (found && w.level != nullptr && met == kMetWall)
                made = wallMaterial(*w.level, at, normal);
            static constexpr struct
            {
                CommonSound first;
                std::uint8_t count;
                std::uint8_t step; ///< (the grate's two are its first and its fourth)
                std::uint8_t loud; ///< of 127: the material's own sound
                std::uint8_t ring; ///< of 127: the crowbar's
            } kStruck[static_cast<int>(Material::Count)] = {
                {CommonSound::Step1, 2, 1, 114, 76},     // concrete
                {CommonSound::Metal1, 2, 1, 114, 38},    // metal
                {CommonSound::Dirt1, 3, 1, 114, 13},     // dirt
                {CommonSound::Duct1, 1, 1, 64, 38},      // vent
                {CommonSound::Grate1, 2, 3, 114, 64},    // grate
                {CommonSound::Tile1, 4, 1, 102, 25},     // tile
                {CommonSound::Slosh1, 4, 1, 114, 0},     // slosh
                {CommonSound::WoodHit1, 3, 1, 114, 25},  // wood
                {CommonSound::GlassHit1, 3, 1, 102, 25}, // computer
                {CommonSound::GlassHit1, 3, 1, 102, 25}, // glass
            };
            const auto &struck = kStruck[static_cast<int>(made)];
            std::uint32_t roll = w.actors != nullptr ? chance(*w.actors) : 0;
            // (On something that breaks, both are quieter.)
            const int loud = breaks ? struck.loud * 2 / 3 : struck.loud;
            const int ring = breaks ? struck.ring / 2 : struck.ring;
            w.heard->add(static_cast<std::uint16_t>(static_cast<int>(struck.first) + static_cast<int>(roll % struck.count) * struck.step),
                         at, loud, kRadiusMedium, 0, 96 + static_cast<int>((roll >> 8) & 15));
            if (ring > 0)
                w.heard->add((roll >> 16) & 1 ? CommonSound::CrowbarHit : CommonSound::CrowbarHit2, nullptr, ring, 0);
            if (ring > 0 && w.heard->count > 0)
                w.heard->events[w.heard->count - 1].pitch = static_cast<std::uint8_t>(98 + ((roll >> 20) & 3));
            // The dent, a moment later -- on a wall; a thing that breaks has
            // its own marks.
            if (found && met == kMetWall)
            {
                state.dentMs = 200;
                for (int k = 0; k < 3; ++k)
                {
                    state.dentAt[k] = at[k];
                    state.dentNormal[k] = normal[k];
                }
            }
            // Loud enough to turn a head.
            if (w.actors != nullptr)
            {
                for (int k = 0; k < 3; ++k)
                    w.actors->noise[k] = player.origin[k];
                w.actors->noiseMs = 600;
            }
            return met;
        }

        /// A swing of the crowbar has landed: the hand's other swing (the
        /// game has two for a blow that lands, turn about), and a quarter
        /// of a second -- unless it killed, which in the game ends the
        /// matter there and then.
        inline void crowbarLanded(WeaponState &state, int met)
        {
            static const char *const kHits[] = {"attack2hit", "attack3hit"};
            state.anim = kHits[state.swingHit++ % 2];
            state.retryMs = 0;
            if (met != kMetKilled)
                state.cooldownMs = 250;
        }
    } // namespace detail

    /// The weapon in hand, for `ms` more milliseconds: `attack` is the
    /// trigger held, `next` a press of "the other weapon", `second` the
    /// other button held: the MP5's launcher, and for any other gun
    /// "reload"; `pick` a weapon asked for by name (a kWeapon*; 0 none),
    /// taken if it is carried -- and if it is the one in hand already,
    /// reloaded; `reload` a reload asked for outright, of any gun (the
    /// MP5 too, which the other button cannot reload while it has
    /// grenades to launch). `forward`, `right` and `up` are the view's (Q12). Returns
    /// kWeapon* for what it did, for whoever draws it.
    /// Where the RPG's laser falls: the first thing along the view.
    inline void laserDot(const MoveWorld &world, const fx *eye, const fx *forward, fx *dot)
    {
        const fx end[3] = {eye[0] + forward[0] * 8192, eye[1] + forward[1] * 8192, eye[2] + forward[2] * 8192};
        const Shot shot = traceShot(world, eye, end);
        for (int i = 0; i < 3; ++i)
            dot[i] = eye[i] + static_cast<fx>((static_cast<std::int64_t>(end[i] - eye[i]) * shot.fraction) >> kFractionShift);
    }

    // ------------------------------------------------------------- autoaim
    //
    // The game's GetAutoaimVector and AutoaimDeflection (player.cpp): on
    // EASY and MEDIUM a gun's round goes to the monster nearest the line of
    // sight, within that gun's cone, instead of straight on -- a third of
    // the way there on EASY, nine tenths on MEDIUM (the game clears what it
    // kept every time: "always use non-sticky autoaim"), never more than 12
    // degrees across or 25 up and down; on DIFFICULT, straight on.

    /// The cone a gun aims within, as its sine (Q12): the pistol's first
    /// fire and the python 10 degrees, the MP5's first fire and the shotgun
    /// 5; 0 for what does not aim (the pistol's quick fire, the launcher,
    /// the crossbow, what is thrown).
    inline fx autoaimCone(std::uint8_t weapon, bool secondary)
    {
        switch (weapon)
        {
        case kWeaponGlock:   return secondary ? 0 : 711;  // (AUTOAIM_10DEGREES)
        case kWeaponPython:  return 711;
        case kWeaponMp5:     return secondary ? 0 : 357;  // (AUTOAIM_5DEGREES)
        case kWeaponShotgun: return 357;
        default:             return 0;
        }
    }

    namespace detail
    {
        /// A direction's yaw and pitch, in turns (65536 a circle; up is
        /// positive).
        inline void autoaimAngles(const fx *dir, std::int32_t &yaw, std::int32_t &pitch)
        {
            const fx flat = static_cast<fx>(isqrt64(static_cast<std::uint64_t>(static_cast<std::int64_t>(dir[0]) * dir[0] +
                                                                              static_cast<std::int64_t>(dir[1]) * dir[1])));
            yaw = static_cast<std::int16_t>(fxAtan2Turns(dir[1], dir[0]));
            pitch = static_cast<std::int16_t>(fxAtan2Turns(dir[2], flat));
        }
    } // namespace detail

    /// Where a gun with `cone` (autoaimCone) fires, from `eye` looking along
    /// `forward` (with its `right` and `up`, Q12): into `aimed`. What it
    /// found is the player's `autoAim` (for the crosshair: where it is put,
    /// and whether it is on something).
    inline void autoaim(LogicWorld &w, PlayerState &player, const fx *eye, const fx *forward, const fx *right,
                        const fx *up, fx cone, fx *aimed)
    {
        for (int i = 0; i < 3; ++i)
            aimed[i] = forward[i];
        AutoAim &aim = player.autoAim.value;
        aim = AutoAim();
        const int level = skillLevelOf(w);
        if (level == kSkillHard || cone <= 0 || w.noAutoaim.value || w.actors == nullptr || w.world == nullptr)
            return;
        const ActorWorld &a = *w.actors;
        // Straight on already meets something that can be hurt: so be it
        // (and the crosshair is on target if it is a monster).
        const fx far[3] = {eye[0] + forward[0] * 8192, eye[1] + forward[1] * 8192, eye[2] + forward[2] * 8192};
        const Shot wall = traceShot(*w.world, eye, far);
        std::int64_t nearest = wall.fraction;
        bool onActor = false;
        for (int i = 0; i < a.count; ++i)
        {
            const ActorState &actor = a.states[i];
            if (!actor.active || actor.ai >= kAiDying || !canBeHurt(actor.kind))
                continue;
            fx mins[3], maxs[3];
            targetBox(actor, mins, maxs);
            std::int64_t enter = 0;
            if (lineEntersBox(eye, far, mins, maxs, enter) && enter < nearest)
            {
                nearest = enter;
                onActor = true;
            }
        }
        if (onActor || (wall.mover >= 0 && detail::blowTaker(w, wall.mover) >= 0))
        {
            aim.onTarget = onActor;
            return;
        }
        // The one that takes least turning to: in front, within the cone
        // (across, and half as much up and down), a little further for
        // being further off, in sight -- and not a friend.
        fx best[3] = {0, 0, 0};
        std::int64_t bestDot = cone;
        bool found = false;
        for (int i = 0; i < a.count; ++i)
        {
            const ActorState &actor = a.states[i];
            if (!actor.active || actor.ai >= kAiDying || !canBeHurt(actor.kind) || !fights(actor.kind) ||
                isPerson(actor.kind))
                continue;
            fx mins[3], maxs[3];
            targetBox(actor, mins, maxs);
            const fx center[3] = {(mins[0] + maxs[0]) / 2, (mins[1] + maxs[1]) / 2, (mins[2] + maxs[2]) / 2};
            const fx d[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
            const fx length = fxLength(d);
            if (length <= 0)
                continue;
            fx dir[3];
            for (int k = 0; k < 3; ++k)
                dir[k] = static_cast<fx>((static_cast<std::int64_t>(d[k]) << kFxShift) / length);
            if (fxMul(dir[0], forward[0]) + fxMul(dir[1], forward[1]) + fxMul(dir[2], forward[2]) < 0)
                continue;
            const fx across = fxMul(dir[0], right[0]) + fxMul(dir[1], right[1]) + fxMul(dir[2], right[2]);
            const fx upward = fxMul(dir[0], up[0]) + fxMul(dir[1], up[1]) + fxMul(dir[2], up[2]);
            std::int64_t dot = (across < 0 ? -across : across) + (upward < 0 ? -upward : upward) / 2;
            dot += dot * fxToInt(length) / (5 * 8192); // (1 + 0.2 of how far, of 8192)
            if (dot > bestDot)
                continue;
            if (traceShot(*w.world, eye, center).fraction < kFractionOne)
                continue;
            bestDot = dot;
            found = true;
            for (int k = 0; k < 3; ++k)
                best[k] = dir[k];
        }
        if (!found)
            return;
        std::int32_t viewYaw, viewPitch, toYaw, toPitch;
        detail::autoaimAngles(forward, viewYaw, viewPitch);
        detail::autoaimAngles(best, toYaw, toPitch);
        std::int32_t yaw = static_cast<std::int16_t>(toYaw - viewYaw);
        std::int32_t pitch = static_cast<std::int16_t>(toPitch - viewPitch);
        constexpr std::int32_t kMostYaw = 12 * 65536 / 360, kMostPitch = 25 * 65536 / 360;
        yaw = yaw > kMostYaw ? kMostYaw : (yaw < -kMostYaw ? -kMostYaw : yaw);
        pitch = pitch > kMostPitch ? kMostPitch : (pitch < -kMostPitch ? -kMostPitch : pitch);
        // (EASY: a third of it; MEDIUM: nine tenths.)
        const int share = level == kSkillEasy ? 33 : 90;
        yaw = yaw * share / 100;
        pitch = pitch * share / 100;
        aim.yaw = static_cast<std::int16_t>(yaw);
        aim.pitch = static_cast<std::int16_t>(pitch);
        aim.onTarget = true;
        fx ys, yc, ps, pc;
        fxSinCos(static_cast<std::uint16_t>(viewYaw + yaw), ys, yc);
        fxSinCos(static_cast<std::uint16_t>(viewPitch + pitch), ps, pc);
        aimed[0] = fxMul(pc, yc);
        aimed[1] = fxMul(pc, ys);
        aimed[2] = ps;
    }

    namespace detail
    {
        inline std::uint32_t weaponsTickBody(LogicWorld &w, PlayerState &player, WeaponState &state,
                                             const fx *eye, const fx *forward, const fx *right, const fx *up,
                                             bool attack, bool next, int ms, bool second, int pick, bool reload);
    }

    /// How loud each gun is to the monsters when it fires (the game's
    /// m_iWeaponVolume, weapon by weapon); `secondary`: its other fire.
    inline int weaponVolume(std::uint8_t weapon, bool secondary)
    {
        switch (weapon)
        {
        case kWeaponGlock:
        case kWeaponMp5:       return kNormalGunVolume;
        case kWeaponShotgun:
        case kWeaponPython:
        case kWeaponRpg:       return kLoudGunVolume;
        case kWeaponCrossbow:
        case kWeaponSnark:     return kQuietGunVolume;
        case kWeaponGauss:     return kGaussFireVolume;
        case kWeaponEgon:      return kEgonVolume;
        case kWeaponHornetgun: return secondary ? kNormalGunVolume : kQuietGunVolume;
        default:               return 0; // (the crowbar's is by what it hits; what is thrown, none)
        }
    }

    inline std::uint32_t weaponsTick(LogicWorld &w, PlayerState &player, WeaponState &state,
                                     const fx *eye, const fx *forward, const fx *right, const fx *up,
                                     bool attack, bool next, int ms, bool second = false, int pick = 0,
                                     bool reload = false)
    {
        const std::uint8_t held = player.inv.current;
        const std::uint32_t did =
            detail::weaponsTickBody(w, player, state, eye, forward, right, up, attack, next, ms, second, pick, reload);
        // (Not fired: where it would go, for the crosshair -- as the game's
        // guns look at each idle moment.)
        if (!(did & kWeaponFired))
        {
            fx aimed[3];
            autoaim(w, player, eye, forward, right, up, autoaimCone(player.inv.current, false), aimed);
        }
        if ((did & kWeaponFired) && held != kWeaponCrowbar)
        {
            const int volume = weaponVolume(held, (did & kWeaponSecondary) != 0);
            if (volume > 0)
                aiWeaponVolume(w, volume);
        }
        return did;
    }

    inline std::uint32_t detail::weaponsTickBody(LogicWorld &w, PlayerState &player, WeaponState &state,
                                                 const fx *eye, const fx *forward, const fx *right, const fx *up,
                                                 bool attack, bool next, int ms, bool second, int pick, bool reload)
    {
        Inventory &inv = player.inv;
        std::uint32_t did = 0;
        if (state.cooldownMs > 0)
            state.cooldownMs -= ms;
        else if (state.readyMs < 100000)
            state.readyMs += ms; // (how long it has been ready: the crowbar's whole blow)
        if (state.secondMs > 0)
            state.secondMs -= ms;
        // The mark of the crowbar's last blow on a wall, a moment after it.
        if (state.dentMs > 0 && (state.dentMs -= ms) <= 0 && w.actors != nullptr)
            decalAdd(*w.actors, kDecalDent, state.dentAt, state.dentNormal, 8, kMarkShot);
        // A weapon being put away: when it is, the other is taken out.
        if (state.holsterMs > 0)
        {
            state.holsterMs -= ms;
            if (state.holsterMs > 0)
                return did;
            if (state.taking != kWeaponNone && hasWeapon(player, state.taking))
                inv.current = state.taking;
            state.taking = kWeaponNone;
            state.cooldownMs = 500;
            state.reloadMs = 0;
            return did | kWeaponDrawn;
        }
        // A swing of the crowbar that met nothing is made again a tenth of
        // a second on, without a sound: it may land then.
        if (state.retryMs > 0 && (state.retryMs -= ms) <= 0 && inv.current == kWeaponCrowbar)
        {
            const int met = detail::crowbarBlow(w, player, state, eye, forward, state.blow);
            if (met != kMetNothing)
            {
                detail::crowbarLanded(state, met);
                aiWeaponVolume(w, met == kMetFlesh || met == kMetKilled ? kCrowbarBodyHitVolume : kCrowbarWallHitVolume);
                did |= kWeaponHit;
            }
        }
        // Something not held is not in hand (a save from before it was).
        if (inv.current != kWeaponNone && !hasWeapon(player, inv.current))
            inv.current = weaponAfter(player);

        std::int16_t *clip = gunClip(player);
        std::int16_t *spare = gunSpare(player);
        Arsenal *ars = player.arsenal;
        const GunKind &gun = gunKind(inv.current);
        const int holds = gun.holds;
        const bool picked = pick > kWeaponNone && pick < kWeaponCount && pick != inv.current && hasWeapon(player, pick);
        reload = reload || (pick > kWeaponNone && pick == inv.current);

        if (state.reloadMs > 0)
        {
            state.reloadMs -= ms;
            if (state.reloadMs > 0 && !next && !picked)
                return did;
            if (state.reloadMs <= 0 && clip != nullptr)
            {
                const int take = holds - *clip < *spare ? holds - *clip : *spare;
                *clip = static_cast<std::int16_t>(*clip + take);
                *spare = static_cast<std::int16_t>(*spare - take);
            }
            state.reloadMs = 0;
        }
        // (A grenade with its pin out is not put down.)
        if ((next || picked) && state.pinMs < 0)
        {
            const std::uint8_t other = picked ? static_cast<std::uint8_t>(pick) : weaponAfter(player);
            if (other != inv.current)
            {
                state.retryMs = 0;
                if (inv.current == kWeaponCrowbar)
                {
                    // Put away first, as the game puts it away: half a
                    // second, and then the other comes out.
                    state.anim = "holster";
                    state.holsterMs = 500;
                    state.taking = other;
                    state.reloadMs = 0;
                    return did;
                }
                inv.current = other;
                state.cooldownMs = 500;
                state.reloadMs = 0;
                return did | kWeaponDrawn;
            }
        }
        // ---- the guns and the grenade, as the game has them ----
        if (state.minded != inv.current)
        {
            // Another weapon in hand, however it got there.
            state.minded = inv.current;
            state.pumpMs = 0;
            state.loading = 0;
            state.loadMs = 0;
            state.pinMs = -1;
            state.redraw = false;
            state.idleMs = 1000;
            state.clicked = false;
            if (ars != nullptr)
                ars->zoomed = 0; // (the crossbow's sight is down when it is put away)
        }
        auto chance = [&]() -> std::uint32_t
        {
            state.seed = state.seed * 1664525u + 1013904223u;
            return state.seed >> 8;
        };
        auto say = [&](CommonSound sound, int pitch = 100, int volume = 127)
        {
            if (w.heard != nullptr)
                w.heard->add(sound, nullptr, volume, kRadiusLarge, pitch);
        };
        // (Once for each pull of the trigger, as the game's is.)
        auto click = [&]()
        {
            if (!state.clicked)
                say(CommonSound::Click, 100, 100);
            state.clicked = true;
        };
        if (!attack && !second)
            state.clicked = false;
        const bool under = player.waterLevel == 3;
        const bool isGun = clip != nullptr;

        // The shotgun is pumped a moment after it fires: that is when the
        // spent case comes out.
        if (state.pumpMs > 0 && (state.pumpMs -= ms) <= 0)
        {
            state.pumpMs = 0;
            say(CommonSound::ShotgunPump, 95 + static_cast<int>(chance() % 32));
            state.shells = state.pumpShells;
            did |= kWeaponShell;
        }

        // A reload begun: the shotgun's a shell at a time, the others' all
        // at once when the hand has done.
        auto startReload = [&]() -> bool
        {
            if (!isGun || *clip >= holds || *spare <= 0 || state.cooldownMs > 0)
                return false;
            if (inv.current == kWeaponShotgun)
            {
                state.anim = "start_reload";
                state.loading = 1;
                state.loadMs = 600;
                state.cooldownMs = 1000;
            }
            else
            {
                // (The sounds of it are the hand's own: its clip has them
                // where the magazine comes out and goes in. The crossbow's
                // is heard as it begins, and its sight comes down.)
                state.anim = inv.current == kWeaponGlock && *clip > 0 ? "reload_noshot" : "reload";
                state.reloadMs = gun.reloadMs;
                if (inv.current == kWeaponCrossbow)
                {
                    if (ars != nullptr)
                        ars->zoomed = 0;
                    say(CommonSound::XbowReload, 93 + static_cast<int>(chance() % 16));
                }
            }
            state.idleMs = 3000;
            did |= kWeaponReload;
            return true;
        };

        // (The shotgun's loading, a shell at a time. `loading` is the RPG's,
        // the gauss's and the egon's own too, which are no guns here.)
        if (state.loading != 0 && inv.current == kWeaponShotgun && clip != nullptr)
        {
            // Firing breaks it off, with what is in it.
            if ((attack || second) && *clip > 0 && state.cooldownMs <= 0)
            {
                state.loading = 0;
            }
            else
            {
                state.loadMs -= ms;
                while (state.loading != 0 && state.loadMs <= 0)
                {
                    if (state.loading == 2)
                    {
                        *clip = static_cast<std::int16_t>(*clip + 1);
                        *spare = static_cast<std::int16_t>(*spare - 1);
                        state.loading = 1;
                    }
                    if (*clip < holds && *spare > 0)
                    {
                        state.anim = "reload";
                        say(chance() & 1 ? CommonSound::ShotgunReload : CommonSound::ShotgunReload3,
                            85 + static_cast<int>(chance() % 32));
                        state.loading = 2;
                        state.loadMs += 500;
                    }
                    else
                    {
                        // Full, or nothing more to put in: pumped, and done.
                        state.anim = "pump";
                        say(CommonSound::ShotgunPump, 95 + static_cast<int>(chance() % 32));
                        state.loading = 0;
                        state.loadMs = 0;
                        state.idleMs = 1500;
                    }
                }
                return did;
            }
        }

        if (inv.current == kWeaponGrenade)
        {
            if (state.pinMs < 0)
            {
                if (state.redraw && state.cooldownMs <= 0)
                {
                    // The next one out of the pocket.
                    state.redraw = false;
                    state.anim = "draw";
                    state.idleMs = 10000 + static_cast<int>(chance() % 5000);
                }
                if (attack && state.cooldownMs <= 0 && inv.grenades > 0 && w.actors != nullptr)
                {
                    state.pinMs = 0;
                    state.anim = "pinpull";
                    return did;
                }
            }
            else
            {
                state.pinMs += ms;
                const bool gone = state.pinMs >= kGrenadeFuse;
                if (gone || (!attack && state.pinMs >= kGrenadePull))
                {
                    // How it is thrown, by how high it is aimed: the
                    // game's own sum. (Looking down is a positive angle
                    // there; ten degrees are added upward, and the range
                    // above and below squeezed to fit.)
                    const fx flat[3] = {forward[0], forward[1], 0};
                    const fx along = fxLength(flat);
                    const int upTurns = static_cast<std::int16_t>(fxAtan2Turns(forward[2], along));
                    const int down = -upTurns * 360 * 16 / 65536; // sixteenths of a degree
                    const int aimed = -10 * 16 + (down < 0 ? down * 80 / 90 : down * 100 / 90);
                    int speed = (90 * 16 - aimed) * 13 / 32; // ... times six and a half
                    speed = speed > 1000 ? 1000 : speed;
                    fx sine = 0, cosine = 0;
                    fxSinCos(static_cast<std::uint16_t>(-aimed * 65536 / (360 * 16)), sine, cosine);
                    fx dir[3] = {0, 0, sine};
                    if (along > 0)
                    {
                        dir[0] = static_cast<fx>((static_cast<std::int64_t>(forward[0]) * cosine) / along);
                        dir[1] = static_cast<fx>((static_cast<std::int64_t>(forward[1]) * cosine) / along);
                    }
                    fx from[3], velocity[3];
                    for (int i = 0; i < 3; ++i)
                    {
                        from[i] = eye[i] + dir[i] * 16;
                        velocity[i] = gone ? 0 : dir[i] * speed + player.velocity[i];
                    }
                    const int left = kGrenadeFuse - state.pinMs;
                    state.pinMs = -1;
                    if (w.actors == nullptr || !projectileAdd(*w.actors, kShotGrenade, gone ? eye : from, velocity, left < 1 ? 1 : left))
                        return did;
                    inv.grenades = static_cast<std::int16_t>(inv.grenades - 1);
                    state.anim = speed < 500 ? "throw1" : (speed < 1000 ? "throw2" : "throw3");
                    state.cooldownMs = 500;
                    state.redraw = inv.grenades > 0;
                    did |= kWeaponFired;
                    if (inv.grenades == 0)
                    {
                        // The last one: nothing left to hold.
                        takeWeapon(player, kWeaponGrenade);
                        inv.current = weaponAfter(player);
                        did |= kWeaponDrawn;
                    }
                    return did;
                }
                return did;
            }
            // Nothing doing: now and then it is turned over in the hand.
            if ((state.idleMs -= ms) <= 0 && state.cooldownMs <= 0 && !state.redraw)
            {
                const bool fidget = chance() % 4 == 3;
                state.anim = fidget ? "fidget" : "idle";
                state.idleMs = fidget ? 2500 : 10000 + static_cast<int>(chance() % 5000);
            }
            return did;
        }

        if (inv.current == kWeaponCrowbar)
        {
            if (!attack || state.cooldownMs > 0)
                return did;
            // A swing, in the game's order: the swish and one of the three
            // swings at the air, always; then the blow -- a whole one after
            // a second's rest, half of one on the heels of the last -- and
            // if it lands, one of the two swings that land takes the
            // other's place.
            static const char *const kMisses[] = {"attack1miss", "attack2", "attack3"};
            state.anim = kMisses[state.swingMiss++ % 3];
            state.blow = static_cast<std::uint8_t>(state.readyMs >= 1000 ? kCrowbarBlow : kCrowbarBlow / 2);
            state.readyMs = 0;
            if (w.heard != nullptr)
                w.heard->add(CommonSound::CrowbarMiss, nullptr);
            did |= kWeaponFired;
            const int met = detail::crowbarBlow(w, player, state, eye, forward, state.blow);
            if (met == kMetNothing)
            {
                state.cooldownMs = 500;
                state.retryMs = 100;
                return did;
            }
            detail::crowbarLanded(state, met);
            aiWeaponVolume(w, met == kMetFlesh || met == kMetKilled ? kCrowbarBodyHitVolume : kCrowbarWallHitVolume);
            return did | kWeaponHit;
        }

        // ---- the weapons that came after the first six (Arsenal) ----
        // Their projectiles keep their state in `sprite`, which starts at none.
        auto launch = [&](std::uint8_t kind, const fx *at, const fx *velocity, int lifeMs)
        {
            Projectile *shot = projectileAdd(*w.actors, kind, at, velocity, lifeMs);
            if (shot != nullptr)
                shot->sprite = 0;
            return shot != nullptr;
        };
        // The last of something thrown or put down: nothing left to hold.
        auto spent = [&](std::uint8_t which)
        {
            takeWeapon(player, which);
            inv.current = weaponAfter(player);
            did |= kWeaponDrawn;
        };
        // (The hand now and then does something of its own: `a` or `b`.)
        auto idle = [&](const char *a, const char *b, int aMs, int bMs)
        {
            if (attack || second || state.cooldownMs > 0 || (state.idleMs -= ms) > 0)
                return;
            const bool other = chance() % 4 == 3;
            state.anim = other ? b : a;
            state.idleMs = other ? bMs : aMs;
        };
        if (inv.current == kWeaponSatchel && ars != nullptr)
        {
            // The game's satchel: thrown, then set off -- all of them at once
            // -- by the radio the hand holds while there are any out.
            int out = 0;
            for (int n = 0; w.actors != nullptr && n < kMaxProjectiles; ++n)
                out += w.actors->shots[n].active && w.actors->shots[n].kind == kShotSatchel;
            if (state.cooldownMs <= 0 && attack && out > 0)
            {
                for (Projectile &shot : w.actors->shots)
                {
                    if (shot.active && shot.kind == kShotSatchel)
                        shot.sprite = 1;
                }
                state.anim = "fire";
                state.cooldownMs = 500;
                state.idleMs = 3000;
                return did | kWeaponFired;
            }
            if (state.cooldownMs <= 0 && ((attack && out == 0) || second) && ars->satchels > 0 && w.actors != nullptr)
            {
                fx from[3], velocity[3];
                for (int i = 0; i < 3; ++i)
                {
                    from[i] = eye[i] + forward[i] * 16;
                    velocity[i] = forward[i] * 274 + player.velocity[i];
                }
                if (!launch(kShotSatchel, from, velocity, 0x3FFFFFFF))
                    return did;
                ars->satchels = static_cast<std::int16_t>(ars->satchels - 1);
                state.cooldownMs = 1000;
                state.idleMs = 3000;
                // (The radio comes out: Game, by what is out.)
                return did | kWeaponFired | kWeaponDrawn;
            }
            if (out == 0 && ars->satchels <= 0 && state.cooldownMs <= 0)
                spent(kWeaponSatchel);
            idle("idle1", "fidget1", 4000, 3000);
            return did;
        }
        if (inv.current == kWeaponTripmine && ars != nullptr)
        {
            // The game's tripmine: put on the wall in front, within 128 units.
            if (attack && state.cooldownMs <= 0 && ars->tripmines > 0 && w.actors != nullptr && w.world != nullptr)
            {
                state.cooldownMs = 300;
                const fx to[3] = {eye[0] + forward[0] * 128, eye[1] + forward[1] * 128, eye[2] + forward[2] * 128};
                // (A line, not the player's size: on the face itself.)
                fx at[3], dir[3];
                if (!traceShotWall(*w.world, eye, to, at, dir))
                    return did;
                for (int i = 0; i < 3; ++i)
                    at[i] += dir[i] * 4;
                if (!launch(kShotMine, at, dir, 2500))
                    return did;
                ars->tripmines = static_cast<std::int16_t>(ars->tripmines - 1);
                if (w.heard != nullptr)
                    w.heard->add(CommonSound::MineDeploy, at, 127, kRadiusMedium);
                state.anim = "draw";
                state.idleMs = 3000;
                did |= kWeaponFired;
                if (ars->tripmines <= 0)
                    spent(kWeaponTripmine);
                return did;
            }
            idle("idle1", "fidget", 3000, 2000);
            return did;
        }
        if (inv.current == kWeaponSnark && ars != nullptr)
        {
            // The game's snark: let go a little way in front, at a fifth of a
            // throw, and off it goes after whatever it finds.
            if (attack && state.cooldownMs <= 0 && ars->snarks > 0 && w.actors != nullptr && w.world != nullptr)
            {
                state.cooldownMs = 300;
                fx at[3], velocity[3];
                for (int i = 0; i < 3; ++i)
                {
                    at[i] = eye[i] + forward[i] * 20 - up[i] * 8;
                    velocity[i] = forward[i] * 200 + player.velocity[i];
                }
                const Trace t = traceAll(*w.world, true, eye, at);
                if (t.startSolid || t.fraction < kFractionOne)
                    return did; // (no room in front)
                if (!launch(kShotSnark, at, velocity, 15000))
                    return did;
                ars->snarks = static_cast<std::int16_t>(ars->snarks - 1);
                say(CommonSound::SnarkHunt1, 105);
                state.anim = "throw";
                state.idleMs = 3000;
                did |= kWeaponFired;
                if (ars->snarks <= 0)
                    spent(kWeaponSnark);
                return did;
            }
            idle("idle1", "fidgetnip", 3000, 2500);
            return did;
        }
        // Where a beam starts: ahead of the eye, to the right and a little
        // down, where the gun's end is.
        fx muzzle[3];
        for (int i = 0; i < 3; ++i)
            muzzle[i] = eye[i] + forward[i] * 16 + right[i] * 5 - up[i] * 5;
        if (inv.current == kWeaponRpg && ars != nullptr)
        {
            // The game's RPG: one rocket in it, two seconds to put in the
            // next; the other trigger switches its laser, whose dot the
            // rocket steers for.
            const bool loaded = (ars->have & kArsenalRpgLoaded) != 0;
            if (second && state.secondMs <= 0)
            {
                ars->have = static_cast<std::uint16_t>(ars->have ^ kArsenalLaserOff);
                state.secondMs = 500;
                say(CommonSound::Click, 100, 80);
            }
            // The rockets in flight, while it is in hand and the laser on:
            // turned towards the dot -- most of the way each tenth of a second.
            if ((ars->have & kArsenalLaserOff) == 0 && w.actors != nullptr && w.world != nullptr)
            {
                fx dot[3];
                laserDot(*w.world, eye, forward, dot);
                for (Projectile &shot : w.actors->shots)
                {
                    if (!shot.active || shot.kind != kShotRocket || shot.sprite != 1)
                        continue;
                    fx to[3] = {dot[0] - shot.pos[0], dot[1] - shot.pos[1], dot[2] - shot.pos[2]};
                    const fx far = fxLength(to);
                    const fx speed = fxLength(shot.velocity);
                    if (far <= 0)
                        continue;
                    const int keep = ms >= 100 ? 0 : 1024 - (ms * 819) / 100; // (of 1024: 0.2 left a tenth on)
                    for (int i = 0; i < 3; ++i)
                    {
                        const fx want = static_cast<fx>((static_cast<std::int64_t>(to[i]) * speed) / far);
                        shot.velocity[i] = static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * keep +
                                                            static_cast<std::int64_t>(want) * (1024 - keep)) >> 10);
                    }
                }
            }
            // A rocket put in, when the hand has done.
            if (state.loading == 1)
            {
                if ((state.loadMs -= ms) > 0)
                    return did;
                state.loading = 0;
                ars->have = static_cast<std::uint16_t>(ars->have | kArsenalRpgLoaded);
                ars->rockets = static_cast<std::int16_t>(ars->rockets - 1);
                return did;
            }
            if (!loaded && ars->rockets > 0 && state.cooldownMs <= 0)
            {
                state.loading = 1;
                state.loadMs = 2000;
                state.anim = "reload";
                state.idleMs = 3000;
                return did | kWeaponReload;
            }
            if (attack && state.cooldownMs <= 0)
            {
                if (!loaded)
                {
                    click();
                    state.cooldownMs = 200;
                    return did;
                }
                if (w.actors == nullptr)
                    return did;
                fx from[3], velocity[3];
                for (int i = 0; i < 3; ++i)
                {
                    from[i] = eye[i] + forward[i] * 16 + right[i] * 8 - up[i] * 8;
                    velocity[i] = forward[i] * 250 + player.velocity[i];
                }
                if (!launch(kShotRocket, from, velocity, 30000))
                    return did;
                ars->have = static_cast<std::uint16_t>(ars->have & ~kArsenalRpgLoaded);
                say(CommonSound::RocketFire, 95 + static_cast<int>(chance() % 10));
                state.anim = "fire";
                state.cooldownMs = 1500;
                state.idleMs = 5000;
                if (w.actors != nullptr)
                {
                    for (int i = 0; i < 3; ++i)
                        w.actors->noise[i] = eye[i];
                    w.actors->noiseMs = 400;
                }
                return did | kWeaponFired;
            }
            // (Empty, it is held as the game's empty one: idle2, fidget2.)
            idle(loaded ? "idle" : "idle2", loaded ? "fidget" : "fidget2", 4000, 3000);
            return did;
        }
        if (inv.current == kWeaponGauss && ars != nullptr)
        {
            // The game's gauss: a shot at the trigger; or, held on the other,
            // a charge -- up to four seconds of it, a uranium every third of
            // one -- let go as a beam through thin walls that throws the one
            // who fires it back. Held past ten seconds it goes off in the hand.
            auto beam = [&](const fx *from, const fx *to, std::uint16_t colour, int width)
            {
                if (w.actors == nullptr)
                    return;
                Effect &e = effectAdd(*w.actors, kEffectBeam, from, to, width, 200);
                e.colour = colour;
            };
            const bool charging = state.loading == 1;
            if (charging)
            {
                const int before = state.loadMs;
                state.loadMs += ms;
                // (A uranium a third of a second, until it is full.)
                if (before < 4000 && state.loadMs / 300 > before / 300 && ars->uranium > 0)
                    ars->uranium = static_cast<std::int16_t>(ars->uranium - 1);
                if (state.loadMs >= 10000)
                {
                    // Too long: it goes off in the hand.
                    state.loading = 0;
                    state.loadMs = 0;
                    playerHurt(player, 50, nullptr, kDmgBlast);
                    if (w.actors != nullptr)
                        w.actors->got |= kGotHurt;
                    say(static_cast<CommonSound>(static_cast<int>(CommonSound::Electro4) + static_cast<int>(chance() % 3)));
                    state.anim = "idle";
                    state.cooldownMs = 1000;
                    return did;
                }
                if (second && ars->uranium > 0)
                {
                    state.anim = state.loadMs < 500 ? nullptr : "spin";
                    return did;
                }
                // Let go (or nothing left to charge with): the beam.
                state.loading = 0;
                const int charge = state.loadMs > 4000 ? 4000 : state.loadMs;
                state.loadMs = 0;
                const int damage = 200 * charge / 4000 > 20 ? 200 * charge / 4000 : 20;
                fx reached[3];
                int met = kMetNothing;
                fireLine(w, player, eye, forward, 8192, damage, false, true, &met, -1, reached, kDmgEnergyBeam);
                beam(muzzle, reached, static_cast<std::uint16_t>(31 | 31 << 5 | 28 << 10), 6);
                // Through a wall no thicker than what is left of it: a blast
                // on the far side, the less for the thickness.
                if (met == kMetWall && w.world != nullptr)
                {
                    for (int depth = 8; depth < damage; depth += 8)
                    {
                        const fx at[3] = {reached[0] + forward[0] * depth, reached[1] + forward[1] * depth,
                                          reached[2] + forward[2] * depth};
                        if (pointContents(*w.world, at) == kContentsSolid)
                            continue;
                        if (w.actors != nullptr)
                            radiusDamage(w, player, at, damage - depth, (damage - depth) * 5 / 2, -1);
                        break;
                    }
                }
                // ... and the gun's kick, which the player feels.
                for (int i = 0; i < 3; ++i)
                    player.velocity[i] -= forward[i] * damage * 5;
                say(CommonSound::GaussFire, 85 + static_cast<int>(chance() % 30));
                state.anim = "fire";
                state.cooldownMs = 1000;
                state.idleMs = 5000;
                return did | kWeaponFired | kWeaponSecondary;
            }
            if (second && state.cooldownMs <= 0 && ars->uranium > 0)
            {
                state.loading = 1;
                state.loadMs = 0;
                ars->uranium = static_cast<std::int16_t>(ars->uranium - 1);
                say(CommonSound::GaussCharge, 110);
                state.anim = "spinup";
                return did;
            }
            if (attack && state.cooldownMs <= 0)
            {
                if (ars->uranium < 2)
                {
                    click();
                    state.cooldownMs = 500;
                    return did;
                }
                ars->uranium = static_cast<std::int16_t>(ars->uranium - 2);
                // A shot: off a wall met at a slant (the game's: within sixty
                // degrees of along it), what is left of it goes on.
                fx from[3] = {eye[0], eye[1], eye[2]}, dir[3] = {forward[0], forward[1], forward[2]};
                const fx *start = muzzle;
                int damage = 20;
                for (int bounce = 0; bounce < 3 && damage > 0; ++bounce)
                {
                    fx reached[3];
                    int met = kMetNothing;
                    fireLine(w, player, from, dir, 8192, damage, false, true, &met, -1, reached, kDmgEnergyBeam);
                    beam(start, reached, static_cast<std::uint16_t>(31 | 26 << 5 | 6 << 10), 3);
                    fx at[3], normal[3];
                    const fx past[3] = {reached[0] + dir[0] * 2, reached[1] + dir[1] * 2, reached[2] + dir[2] * 2};
                    if (met != kMetWall || w.world == nullptr || !traceShotWall(*w.world, from, past, at, normal))
                        break;
                    const fx along = static_cast<fx>((static_cast<std::int64_t>(dir[0]) * normal[0] +
                                                      static_cast<std::int64_t>(dir[1]) * normal[1] +
                                                      static_cast<std::int64_t>(dir[2]) * normal[2]) >> kFxShift);
                    if (along < -kFxOne / 2)
                        break; // (met square on: it stops there)
                    for (int i = 0; i < 3; ++i)
                    {
                        dir[i] = dir[i] - static_cast<fx>((static_cast<std::int64_t>(normal[i]) * along * 2) >> kFxShift);
                        from[i] = at[i] + normal[i] / 2;
                    }
                    damage = damage * (kFxOne + along) / kFxOne;
                    start = from;
                }
                say(CommonSound::GaussFire, 85 + static_cast<int>(chance() % 30), 100);
                state.anim = "fire2";
                state.cooldownMs = 200;
                state.idleMs = 5000;
                if (w.actors != nullptr)
                {
                    for (int i = 0; i < 3; ++i)
                        w.actors->noise[i] = eye[i];
                    w.actors->noiseMs = 400;
                }
                return did | kWeaponFired;
            }
            idle("idle", "fidget", 4000, 3000);
            return did;
        }
        if (inv.current == kWeaponEgon && ars != nullptr)
        {
            // The game's egon: a beam for as long as the trigger is held and
            // there is uranium -- 14 to what it meets and a uranium every
            // tenth of a second.
            const bool running = state.loading == 2;
            if (attack && ars->uranium > 0 && state.cooldownMs <= 0)
            {
                if (!running)
                {
                    state.loading = 2;
                    state.loadMs = 0;
                    state.secondMs = 0;
                    say(CommonSound::EgonWindup, 100);
                    state.anim = "altfireon";
                }
                // (Its hum, again and again while it runs.)
                if (state.secondMs <= 0)
                {
                    if (running)
                        say(CommonSound::EgonRun, 100, 110);
                    state.secondMs = 1000;
                }
                if ((state.loadMs -= ms) <= 0)
                {
                    state.loadMs += 100;
                    if (state.loadMs <= 0)
                        state.loadMs = 100;
                    ars->uranium = static_cast<std::int16_t>(ars->uranium - 1);
                    fx reached[3];
                    fireLine(w, player, eye, forward, 2048, 14, false, false, nullptr, -1, reached, kDmgEnergyBeam);
                    if (w.actors != nullptr)
                    {
                        Effect &e = effectAdd(*w.actors, kEffectBeam, muzzle, reached, 5, 120);
                        e.colour = static_cast<std::uint16_t>(18 | 10 << 5 | 31 << 10);
                        for (int i = 0; i < 3; ++i)
                            w.actors->noise[i] = eye[i];
                        w.actors->noiseMs = 200;
                    }
                    did |= kWeaponFired;
                }
                return did;
            }
            if (running)
            {
                // Let go, or empty: off.
                state.loading = 0;
                state.loadMs = 0;
                say(CommonSound::EgonOff, 100);
                state.anim = "altfireoff";
                state.cooldownMs = 500;
                state.idleMs = 3000;
                return did;
            }
            if (attack && ars->uranium <= 0 && state.cooldownMs <= 0)
            {
                click();
                state.cooldownMs = 500;
            }
            idle("idle1", "fidget1", 4000, 3000);
            return did;
        }
        if (inv.current == kWeaponHornetgun && ars != nullptr)
        {
            // The game's hornetgun: it grows its own hornets, one each half
            // second up to eight; a homing one four times a second, or the
            // fast straight kind ten times a second.
            if (ars->hornets < 8)
            {
                if ((state.loadMs -= ms) <= 0)
                {
                    ars->hornets = static_cast<std::int16_t>(ars->hornets + 1);
                    state.loadMs += 500; // (what the tick ran over by counts towards the next)
                }
            }
            else
            {
                state.loadMs = 500;
            }
            if ((attack || second) && state.cooldownMs <= 0 && ars->hornets > 0 && w.actors != nullptr)
            {
                const bool fast = !attack;
                fx from[3], velocity[3];
                for (int i = 0; i < 3; ++i)
                {
                    from[i] = eye[i] + forward[i] * 16 + right[i] * 8 - up[i] * 12;
                    velocity[i] = forward[i] * (fast ? 1200 : 300);
                }
                Projectile *shot = projectileAdd(*w.actors, kShotHornet, from, velocity, 4000);
                if (shot == nullptr)
                    return did;
                shot->sprite = fast ? 1 : 0;
                ars->hornets = static_cast<std::int16_t>(ars->hornets - 1);
                state.loadMs = 500;
                say(static_cast<CommonSound>(static_cast<int>(CommonSound::HornetFire1) + static_cast<int>(chance() % 3)),
                    100, 110);
                state.anim = "shoot";
                state.cooldownMs = fast ? 100 : 250;
                state.idleMs = 3000;
                return did | kWeaponFired | (fast ? kWeaponSecondary : 0u);
            }
            idle("idle1", "fidgetsway", 4000, 3000);
            return did;
        }
        if (!isGun)
            return did;

        // The crossbow's sight: up, and down, with the other trigger.
        if (inv.current == kWeaponCrossbow && second && ars != nullptr)
        {
            if (state.secondMs <= 0)
            {
                ars->zoomed = ars->zoomed ? 0 : 1;
                state.secondMs = 1000;
            }
            if (!attack)
                return did;
        }

        // A reload asked for outright. (The other button too, for the .357,
        // which has nothing else to do with it.)
        if ((reload || (second && inv.current == kWeaponPython)) && startReload())
            return did;

        // Rounds out of the gun: `rounds` of them, `pellets` in all, each
        // off line by the game's cone -- the sum of two draws either way,
        // so that most go near the middle.
        auto fire = [&](int rounds, int pellets, int cone, int wait, CommonSound sound, int pitch, const char *anim)
        {
            *clip = static_cast<std::int16_t>(*clip - rounds);
            say(sound, pitch);
            if (inv.current == kWeaponCrossbow)
            {
                // A bolt, not a round: from a little under the eye, at 2000
                // (half that under water). No flash.
                state.cooldownMs = wait + (state.cooldownMs > -ms ? state.cooldownMs : -ms);
                state.anim = *clip > 0 ? anim : "fire3";
                state.idleMs = 10000 + static_cast<int>(chance() % 5000);
                if (w.actors != nullptr)
                {
                    fx from[3], velocity[3];
                    const int speed = under ? 1000 : 2000;
                    for (int i = 0; i < 3; ++i)
                    {
                        from[i] = eye[i] + forward[i] * 8 - up[i] * 2;
                        velocity[i] = forward[i] * speed;
                    }
                    launch(kShotBolt, from, velocity, 10000);
                    for (int i = 0; i < 3; ++i)
                        w.actors->noise[i] = eye[i];
                    w.actors->noiseMs = 200;
                }
                did |= kWeaponFired;
                return;
            }
            if (w.actors != nullptr)
            {
                // (Loud enough to turn a head.)
                for (int i = 0; i < 3; ++i)
                    w.actors->noise[i] = eye[i];
                w.actors->noiseMs = 400;
                // The flash at the gun's end: ahead of the eye, to the
                // right and a little down, where the gun is held.
                fx at[3];
                for (int i = 0; i < 3; ++i)
                    at[i] = eye[i] + forward[i] * 30 + right[i] * 6 - up[i] * 5;
                effectAdd(*w.actors, kEffectMuzzle, at, nullptr, 0, 50);
            }
            // Where it goes: as aimed -- or bent, on EASY and MEDIUM, to the
            // monster nearest that (the pistol's quick fire is not).
            fx aimed[3];
            autoaim(w, player, eye, forward, right, up,
                    autoaimCone(inv.current, inv.current == kWeaponGlock && second && !attack), aimed);
            bool hit = false;
            for (int n = 0; n < pellets; ++n)
            {
                fx dir[3] = {aimed[0], aimed[1], aimed[2]};
                if (cone != 0)
                {
                    const int x = (static_cast<int>(chance() % 4097) + static_cast<int>(chance() % 4097) - 4096) * cone / 4096;
                    const int y = (static_cast<int>(chance() % 4097) + static_cast<int>(chance() % 4097) - 4096) * cone / 4096;
                    for (int i = 0; i < 3; ++i)
                        dir[i] = aimed[i] + ((right[i] * x + up[i] * y) >> kFxShift);
                }
                hit = fireLine(w, player, eye, dir, gun.range, gun.damage, true) || hit;
            }
            // (What the wait ran over by comes off the next: a gun that
            // fires ten times a second does, whatever the length of a tick.)
            state.cooldownMs = wait + (state.cooldownMs > -ms ? state.cooldownMs : -ms);
            state.anim = anim;
            state.idleMs = 10000 + static_cast<int>(chance() % 5000);
            did |= kWeaponFired | (hit ? kWeaponHit : 0);
        };

        const bool fires = attack || (second && (inv.current == kWeaponGlock || inv.current == kWeaponShotgun ||
                                                 inv.current == kWeaponMp5));
        if (fires && state.cooldownMs <= 0 && !(second && inv.current == kWeaponMp5 && !attack && state.secondMs > 0))
        {
            // Under water all but the pistol only click.
            if (under && !gun.wet)
            {
                click();
                state.cooldownMs = gun.emptyMs;
                return did;
            }
            // The other trigger first, as the game looks at it first.
            if (second && inv.current == kWeaponMp5)
            {
                // The launcher: a grenade that goes off at what it touches,
                // once a second.
                if (state.secondMs > 0)
                    return did;
                if (inv.ammoAr <= 0)
                {
                    click();
                    return did;
                }
                if (w.actors == nullptr)
                    return did;
                fx from[3], velocity[3];
                for (int i = 0; i < 3; ++i)
                {
                    from[i] = eye[i] + forward[i] * 16;
                    velocity[i] = forward[i] * 800;
                }
                if (!projectileAdd(*w.actors, kShotContact, from, velocity, 10000))
                    return did;
                inv.ammoAr = static_cast<std::int16_t>(inv.ammoAr - 1);
                state.secondMs = 1000;
                state.cooldownMs = state.cooldownMs > 1000 ? state.cooldownMs : 1000;
                say(chance() & 1 ? CommonSound::Launcher : CommonSound::Launcher2, 94 + static_cast<int>(chance() % 16));
                state.anim = "grenade";
                state.idleMs = 5000;
                return did | kWeaponSecondary;
            }
            if (second && inv.current == kWeaponShotgun)
            {
                // Both barrels: two shells, twelve pellets -- or, with one
                // shell or none, more shells first.
                if (*clip <= 1)
                {
                    if (!startReload())
                    {
                        click();
                        state.cooldownMs = gun.emptyMs;
                    }
                    return did;
                }
                fire(2, gun.pellets * 2, gun.cone, 1500, CommonSound::ShotgunDouble, 85 + static_cast<int>(chance() % 32),
                     "shoot_big");
                state.pumpMs = 950;
                state.pumpShells = 2;
                state.idleMs = *clip > 0 ? 6000 : 1500;
                return did | kWeaponSecondary;
            }
            if (*clip <= 0)
            {
                // Nothing in it: more put in, if there is more; or the click.
                if (!startReload())
                {
                    click();
                    state.cooldownMs = gun.emptyMs;
                }
                return did;
            }
            const bool fast = second && !attack && inv.current == kWeaponGlock;
            const char *anim = inv.current == kWeaponGlock && *clip == 1 && !fast ? "shoot_empty" : gun.shoot;
            fire(1, gun.pellets, fast ? kConePistolFast : gun.cone, fast ? 200 : gun.refireMs,
                 gun.fire[chance() & 1], gun.pitch + static_cast<int>(chance() % gun.pitchWide), anim);
            if (inv.current == kWeaponShotgun)
            {
                state.pumpMs = 500;
                state.pumpShells = 1;
                state.idleMs = *clip > 0 ? 5000 : 750;
            }
            else if (inv.current != kWeaponPython && inv.current != kWeaponCrossbow)
            {
                state.shells = 1;
                did |= kWeaponShell;
            }
            return did;
        }

        // Nothing doing: the hand does something of its own now and then,
        // each weapon by its own odds (the game's).
        if (!attack && !second && state.cooldownMs <= 0 && (state.idleMs -= ms) <= 0)
        {
            const int roll = static_cast<int>(chance() % 100);
            switch (inv.current)
            {
            case kWeaponGlock:
                // (Not with nothing in it: its slide is back.)
                if (*clip != 0)
                {
                    state.anim = roll < 30 ? "idle3" : (roll < 60 ? "idle1" : "idle2");
                    state.idleMs = roll < 30 ? 3060 : (roll < 60 ? 3750 : 2500);
                }
                else
                {
                    state.idleMs = 1000;
                }
                break;
            case kWeaponPython:
                state.anim = roll < 50 ? "idle1" : (roll < 70 ? "idle2" : (roll < 90 ? "idle3" : "fidget1"));
                state.idleMs = roll < 50 ? 2330 : (roll < 70 ? 2000 : (roll < 90 ? 2930 : 5670));
                break;
            case kWeaponShotgun:
                state.anim = roll < 80 ? "deepidle" : (roll < 95 ? "sm_idle" : "idle4");
                state.idleMs = roll < 80 ? 5000 : 2220;
                break;
            case kWeaponCrossbow:
                // (Loaded, or not: the bolt in it shows.)
                state.anim = roll < 75 ? (*clip != 0 ? "idle1" : "idle2") : (*clip != 0 ? "fidget1" : "fidget2");
                state.idleMs = roll < 75 ? 3000 : 3000;
                break;
            default: // the MP5
                state.anim = roll < 50 ? "longidle" : "idle1";
                state.idleMs = 10000 + static_cast<int>(chance() % 5000);
                break;
            }
        }
        return did;
    }

    /// The weapon in hand's own clip moved on by `ms`, and the sounds its
    /// clip has in it played as they are passed: the game keeps a reload's
    /// sounds in the animation, where the magazine comes out and goes in.
    inline void weaponHandTick(LogicWorld &w, ActorState &held, int ms)
    {
        if (w.actors == nullptr || !held.active)
            return;
        const std::int16_t clipBefore = held.clip;
        const std::uint32_t before = actorAdvance(*w.actors, held, ms);
        const ModelView *model = actorModel(*w.actors, held);
        if (model == nullptr || held.clip < 0 || held.clip != clipBefore || w.heard == nullptr || w.modelSounds == nullptr)
            return;
        const ModelClip &clip = model->clips[held.clip];
        const bool wrapped = held.frame < before;
        for (int e = 0; e < clip.eventCount; ++e)
        {
            const ModelEvent &event = model->events[clip.firstEvent + e];
            if (event.kind != kEventSound)
                continue;
            const std::uint32_t at = static_cast<std::uint32_t>(event.frame) << 8;
            const bool passed = wrapped ? (at > before || at <= held.frame)
                                        : ((before < at && at <= held.frame) || (at == 0 && before == 0 && held.frame > 0));
            if (!passed)
                continue;
            for (std::uint32_t n = 0; n < w.modelSoundCount; ++n)
            {
                if (w.modelSounds[n].hash == event.target)
                {
                    w.heard->add(w.modelSounds[n].sound, nullptr, 127, kRadiusLarge);
                    break;
                }
            }
        }
    }
} // namespace hldata

#include "HlEffects.hpp"
#include "HlAiCore.hpp"
#include "HlAiMonsters.hpp"
#include "HlGrunt.hpp"
#include "HlAiTalk.hpp"
#include "HlAiPeople.hpp"
#include "HlAliens.hpp"
#include "HlMilitary.hpp"
#include "HlPeople.hpp"
#include "HlXen.hpp"
