#pragma once

// Actors: the people and props of a map that are models, as they are right
// now.
//
// An actor is a place, a way it faces, and a clip of its model at some frame.
// Left alone it stands and loops its idle clip (or holds a pose, or cycles).
// A scripted sequence can take it over -- walk it to a mark, turn it, have it
// act something out -- and that half lives in HlLogic.hpp with the rest of
// what a map's wiring does; this file is the actor itself and the parts of it
// that need no wiring.
//
// There is no thinking here. An actor does not see, hear or decide; it goes
// where a script sends it, in a straight line.
//
// std-only and inline: the host tests run it.

#include <cstring>

#include "HlAnim.hpp"
#include "HlFields.hpp"
#include "HlMovers.hpp"

namespace hldata
{
    // What a monster is doing (ActorState::ai).
    inline constexpr std::uint8_t kAiIdle = 0;
    inline constexpr std::uint8_t kAiChase = 1;
    inline constexpr std::uint8_t kAiAttack = 2;
    inline constexpr std::uint8_t kAiLeap = 3;
    inline constexpr std::uint8_t kAiFlinch = 4;
    inline constexpr std::uint8_t kAiDying = 5;
    inline constexpr std::uint8_t kAiDead = 6;

    /// A body that fades away once it has fallen (the game's FadeMonster:
    /// SUB_StartFadeOut, 255 to nothing at 70 a second), and is then gone:
    /// its time to go is kept in its ActorState::cooldownMs -- which nothing
    /// asks of the dead -- as kCorpseFading less what is left.
    inline constexpr std::int32_t kCorpseFadeMs = 3600;
    inline constexpr std::int32_t kCorpseFading = -1000000;
    /// How much of its fading a body has left (ms), or -1 if it is not fading.
    template <class Actor>
    inline int corpseFadeLeft(const Actor &actor)
    {
        const std::int32_t left = kCorpseFading - actor.cooldownMs;
        return actor.ai == kAiDead && left >= 0 && left <= kCorpseFadeMs ? static_cast<int>(left) : -1;
    }
    template <class Actor>
    inline void corpseFadeStart(Actor &actor)
    {
        if (corpseFadeLeft(actor) < 0)
            actor.cooldownMs = kCorpseFading - kCorpseFadeMs;
    }

    /// Actors that may arrive from the map before, on top of the map's own.
    inline constexpr int kMaxVisitors = 4;
    /// ... and on a multiplayer map, in their place, the players of a match
    /// between consoles (HlNet.hpp's kNetPlayers): a map nobody saves, so
    /// that the saves' count of actors (kMaxVisitors) is not changed.
    inline constexpr int kMaxMatchPlayers = 8;
    inline constexpr int kMaxSpareActors = kMaxMatchPlayers > kMaxVisitors ? kMaxMatchPlayers : kMaxVisitors;

    // What a script is doing with the actor it has (ActorState::scriptPhase).
    inline constexpr std::uint8_t kActorFree = 0;
    inline constexpr std::uint8_t kActorHeld = 1;    ///< taken, waiting to be told to start
    inline constexpr std::uint8_t kActorMoving = 2;  ///< on its way to the mark
    inline constexpr std::uint8_t kActorPlaying = 3; ///< acting it out

    struct ActorState
    {
        bool active = false;
        fx pos[3] = {0, 0, 0}; ///< its feet
        std::uint16_t yaw = 0;
        std::uint16_t model = 0; ///< into the map's ModelRefs
        std::uint16_t name = 0;  ///< name id in THIS map; 0 = none
        std::uint32_t nameHash = 0;
        std::uint8_t body = 0;
        std::uint8_t skin = 0;   ///< which of its model's skins
        std::uint8_t kind = 0;   ///< ActorKind
        std::uint8_t flags = 0;  ///< kActor*
        std::uint8_t light = 24;
        std::int16_t clip = -1;  ///< of its model; -1: the model has none, it stands as built
        std::int16_t restClip = -1; ///< the clip it goes back to when nothing directs it
        std::uint32_t frame = 0; ///< Q8, in the clip's frames
        bool once = false;       ///< the clip is played to its end and held, loop or not
        bool finished = false;   ///< ... and has got there
        bool frozen = false;     ///< held on this frame
        std::int16_t script = -1; ///< the logic record directing it, or -1
        std::uint8_t scriptPhase = kActorFree;
        // While moving to a mark:
        fx target[3] = {0, 0, 0};
        fx speed = 0;        ///< units a second
        std::int32_t moveMs = 0; ///< left before it is simply put there
        // What fights (HlMonsters.hpp):
        std::int16_t health = 0;
        std::uint8_t ai = 0;     ///< kAi*
        std::uint8_t struck = 0; ///< which blows of the attack being played have landed
        std::int32_t thinkMs = 0;    ///< until it next looks about, or plans its way
        std::int32_t cooldownMs = 0; ///< until it may attack again
        std::int32_t stuckMs = 0;    ///< how long it has been getting nowhere
        fx velocity[3] = {0, 0, 0};  ///< in the air
        std::uint32_t lastFrame = 0; ///< of its clip, a tick ago
        std::int16_t goalNode = -1;  ///< the path node it is walking to
        std::int16_t fromNode = -1;  ///< ... and the one it last stood on
        fx goal[3] = {0, 0, 0};
        bool hasGoal = false;
        /// A monster chasing (with `seen` and `lookMs` below, which a
        /// soldier uses too): whether it has seen what it is after at all,
        /// how long ago it last did; and where it was when it set out,
        /// which is where it goes back to when it gives up.
        bool hasSeen = false;
        std::int32_t lostMs = 0;
        fx home[3] = {0, 0, 0};
        bool goingHome = false;
        /// One of the people (HlPeople.hpp): what he is afraid of or
        /// shooting at (an actor, or -1), and whether he was following the
        /// player before he ran.
        std::int16_t foe = -1;
        bool wasFollowing = false;
        /// A guard (HlPeople.hpp): where what he is after was when he last
        /// saw it, whether he sees it now and has a clear shot at it; his
        /// pistol (0 in its holster, 1 coming out, 2 out). (`painMs`, a
        /// soldier's too, is how long until he may cry out again.)
        fx foeAt[3] = {0, 0, 0};
        bool foeSeen = false;
        bool clearShot = false;
        std::uint8_t gun = 0;
        std::int32_t gunMs = 0;
        /// A barnacle's tongue: how far it hangs below the mouth. (The big
        /// turret's eye, too: how bright, 0..255 -- not Q12.)
        fx reach = 0;
        /// What one of the people stands on (HlMonsters.hpp's peopleGround):
        /// a brush, by its number; -1 the world, or nothing the map gave
        /// them; -2 not yet looked at. Where that brush was a tick ago, so
        /// that they go where it goes; and whether they are falling --
        /// because it broke, or went from under them.
        std::int16_t groundMover = -2;
        fx groundAt[3] = {0, 0, 0};
        bool airborne = false;
        std::int32_t hurtMs = 0;     ///< until a place that hurts may hurt them again
        /// How far it has faded from sight, 0 (all there) to 235 (an
        /// assassin's cloak on DIFFICULT: 255 less its renderamt). Worked
        /// out again as it thinks, and not kept in a save, as the game's
        /// is not.
        Unsaved<std::uint8_t> cloak;
        /// The clip (plus one; 0: none) it plays half again as fast, until
        /// that clip is done -- an alien slave's zap on DIFFICULT (its
        /// framerate 1.5, which the next sequence puts back).
        Unsaved<std::int16_t> quickClip;
        /// Where between its clip's two poses it is drawn, for a clip that
        /// blends by the aim's pitch: 0..255, the game's pev->blending[0]
        /// (HlAnim.hpp clipBlend). Set as it shoots; not kept in a save, the
        /// next shot sets it again.
        Unsaved<std::uint8_t> blend;
        std::uint8_t attack = 0;     ///< which of its kind's attacks it is making (kAttack*)
        std::uint8_t item = 0;       ///< ItemKind, for something to pick up
        std::uint16_t fires = 0;     ///< ... and the name id of what taking it fires
        // A turret (HlMonsters.hpp). Angles are 65536 to the circle: which
        // way the gun points in the world, and how far up; and where it is
        // being turned to.
        std::int32_t aimYaw = 0, aimPitch = 0;
        std::int32_t wantYaw = 0, wantPitch = 0;
        std::int32_t turnRate = 0; ///< degrees a second
        /// Something has switched it by name since it last thought: 1 + how
        /// (kUse*). 0: nothing has.
        std::uint8_t switched = 0;
        // A soldier (HlGrunt.hpp). (What he carries is in `item`: kGrunt*.)
        std::uint8_t ammo = 0;      ///< rounds in his gun
        std::uint8_t mood = 0;      ///< kMood*
        std::uint8_t bursts = 0;    ///< left of the volley he is firing
        std::int16_t squad = -1;    ///< the actor who leads his squad (himself, if he does); -1: on his own
        std::int32_t grenadeMs = 0; ///< until he may think of a grenade again
        std::int32_t painMs = 0;    ///< until he may cry out again
        std::int32_t lookMs = 0;    ///< until he next looks about
        std::int32_t coverMs = 0;   ///< until it is worth looking for cover again
        fx seen[3] = {0, 0, 0};     ///< where whoever he is after was last seen
    };

    /// Something in flight: a bullsquid's spit, a hand grenade.
    struct Projectile
    {
        bool active = false;
        std::uint8_t kind = 0; ///< kShot*
        /// For kShotGib: the sprite it is drawn as (a shooter that throws
        /// sprites), or 0xFFFF for a plain scrap.
        std::uint16_t sprite = 0xFFFF;
        fx pos[3] = {0, 0, 0};
        fx velocity[3] = {0, 0, 0};
        std::int32_t leftMs = 0; ///< a grenade's fuse; the longest spit may fly
    };
    inline constexpr std::uint8_t kShotSpit = 1;
    inline constexpr std::uint8_t kShotGrenade = 2;
    inline constexpr std::uint8_t kShotGib = 3; ///< something thrown out (HlEffects.hpp): falls, bounces, goes
    inline constexpr std::uint8_t kShotContact = 4; ///< a launcher's grenade: goes off at what it touches
    /// A barnacle's tongue: as long as its bones make it with the slide at
    /// nothing (barnacle.mdl, Bone16..20), and the most the slide lets out
    /// (its controller's range, 1024) on top of that.
    inline constexpr fx kBarnacleTongueBones = fxFromInt(85);
    inline constexpr fx kBarnacleTongueMost = fxFromInt(1024 + 85);
    /// A crossbow's bolt: straight on, fast; into what it meets, or into the
    /// wall, where it stays a while (`sprite` 1: it is in the wall).
    inline constexpr std::uint8_t kShotBolt = 5;
    /// A satchel charge: thrown, it lies where it stops until it is set off
    /// (`sprite` 1).
    inline constexpr std::uint8_t kShotSatchel = 6;
    /// A tripmine the player put on a wall: `velocity` is the way its beam
    /// goes (unit, Q12). `sprite` 0 while it charges (`leftMs` to go), 1 armed
    /// (`leftMs` then the length its beam was, Q30 of kMineRange), 2 set off
    /// (`leftMs` until it goes).
    inline constexpr std::uint8_t kShotMine = 7;
    /// A snark: hops at whatever it can find to bite, and bursts when its
    /// time is up. `sprite`: how many bites (low byte), and until its next
    /// bite (high byte, in twentieths of a second).
    inline constexpr std::uint8_t kShotSnark = 8;
    /// The RPG's rocket: slow, then faster, steered at the laser's dot while
    /// that is on (`sprite`: 1 once lit), and off at what it touches.
    inline constexpr std::uint8_t kShotRocket = 9;
    /// A hornet from the hornetgun: `sprite` is 1 for the fast straight
    /// kind, else the homing one; it stings what it meets.
    inline constexpr std::uint8_t kShotHornet = 10;
    // ... and the monsters' own (HlAliens.hpp): after the player's, on the
    // player and those on their side.
    /// A snark a monstermaker made: as the player's, but after the player
    /// and the people, from the first.
    inline constexpr std::uint8_t kShotSnarkFoe = 11;
    /// An alien grunt's hornet: always homing, after the player and the
    /// people (not the aliens).
    inline constexpr std::uint8_t kShotHornetFoe = 12;
    /// A controller's head ball (CControllerHeadBall): slow, homing, a jolt
    /// when it reaches what it is after.
    inline constexpr std::uint8_t kShotHeadBall = 13;
    /// ... and its zap ball (CControllerZapBall): straight, and a jolt where
    /// it lands.
    inline constexpr std::uint8_t kShotZapBall = 14;
    /// A gargantua's stomp (CStomp): runs the way it was sent (`velocity`,
    /// a unit vector, Q12), faster and faster, as far as the wall it was
    /// aimed at. `leftMs` is not a time: its low 12 bits are how far it has
    /// still to go, in units, and the rest its speed, in sixteenths of a unit
    /// a second (kStomp*). `sprite` is how fast it gathers speed (the game's
    /// framerate), with kStompHitPlayer once it has hurt the player.
    inline constexpr std::uint8_t kShotStomp = 15;
    /// A rocket a tank fired (func_tankrocket: the game's rpg_rocket):
    /// flies as the player's does, but stops at the player too, and no
    /// laser of the player's steers it.
    inline constexpr std::uint8_t kShotRocketFoe = 16;
    /// Big momma's mortar of acid (CBMortar): thrown, it falls at the
    /// world's pull and bursts where it meets anything.
    inline constexpr std::uint8_t kShotMommaSpit = 17;
    /// The nihilanth's balls (CNihilanthHVR), its own to move
    /// (HlAliens.hpp): the zap, the one that takes the player away, and
    /// those it throws off as it dies.
    inline constexpr std::uint8_t kShotNihilZap = 18;
    inline constexpr std::uint8_t kShotNihilPortal = 19;
    inline constexpr std::uint8_t kShotNihilGreen = 20;
    inline constexpr std::int32_t kStompLengthMask = 0xFFF;
    inline constexpr int kStompSpeedShift = 12;
    inline constexpr std::uint16_t kStompHitPlayer = 0x8000;
    /// How many may be in flight at once: all of these on a DSi, half on a
    /// DS (ActorWorld::shotLimit).
    inline constexpr int kMaxProjectiles = 16;

    /// A piece of something that has come apart: one sub-model of a gib
    /// model (the game's hgibs, agibs, woodgibs ...), thrown, tumbling,
    /// bouncing, lying, and gone after a while. Seen only: it is in nobody's
    /// way.
    struct Gib
    {
        bool active = false;
        bool resting = false;
        std::uint16_t model = 0; ///< into the map's ModelRefs
        std::uint8_t body = 0;   ///< which piece of it
        fx pos[3] = {0, 0, 0};
        fx velocity[3] = {0, 0, 0};
        std::uint16_t yaw = 0, pitch = 0; ///< how it is turned
        std::int16_t spin = 0;            ///< ... and turns, 65536ths of a circle a millisecond
        std::int32_t leftMs = 0;
    };
    /// How many there may be at once: all of these on a DSi, half on a DS
    /// (ActorWorld::gibLimit).
    inline constexpr int kMaxGibs = 32;

    /// A mark left on a wall: a bullet's hole, blood, the scorch of a
    /// blast, the cracks in glass that has been shot (the game's decals:
    /// HlUi.hpp says which picture each is). `at` is in the world, or --
    /// for one on a brush, which moves with it and goes when it does --
    /// from the brush's place.
    struct Decal
    {
        bool active = false;
        std::uint8_t picture = 0;
        std::uint8_t half = 4;  ///< units from its middle to its edge
        std::uint8_t turn = 0;  ///< quarter turns, so that no two look alike (low two bits); kDecalTheMaps
        std::int16_t brush = -1;
        std::uint16_t colour = 0;
        fx at[3] = {0, 0, 0};
        fx normal[3] = {0, 0, kFxOne}; ///< Q12: the way the wall faces
        /// Until it is gone (its last second it fades). A crack lasts as
        /// long as its pane.
        std::int32_t leftMs = 0;
    };
    /// Decal::turn: a mark the map put there (an infodecal), never given up
    /// to one a fight leaves.
    inline constexpr std::uint8_t kDecalTheMaps = 0x80;
    /// Marks of one kind are one family: a new one where one of its
    /// family already is takes that one's place (decalAdd).
    constexpr int decalFamily(std::uint8_t picture)
    {
        // (16 to 20: the hand, the pools and the splashes, which are all
        // blood. 21 and 22: a wall's own damage.)
        return picture < 4 ? 0
               : ((picture >= 5 && picture <= 7) || (picture >= 16 && picture <= 20) ? 5 : (picture == 13 ? 8 : picture));
    }
    /// How long each lasts (ms): holes and dents a third of a minute,
    /// blood and scorches two thirds, a crack for as long as its pane.
    constexpr std::int32_t decalLife(std::uint8_t picture)
    {
        const int family = decalFamily(picture);
        return family == 9 ? 0x3FFFFFFF : (family == 0 || family == 4 ? 20000 : 40000);
    }
    /// (All of these on a DSi; ActorWorld::decalLimit of them on a DS.)
    inline constexpr int kMaxDecals = 64;

    /// A small thing thrown, tumbling, falling and soon gone, that is
    /// nothing but seen: a shard of what has been broken (a patch of its
    /// own texture), a speck off a wall a shot has hit, a spent casing.
    struct Piece
    {
        std::uint8_t kind = 0;   ///< kPiece*; 0: none
        std::uint8_t half = 2;   ///< units from its middle to its edge
        std::uint8_t alpha = 31; ///< how solid it is drawn (glass is not)
        std::uint8_t bounces = 0;
        std::uint16_t texture = 0xFFFF; ///< kPieceShard: into the level's Textures
        std::uint8_t s = 0, t = 0;      ///< ... and where in it its patch is, in eighths
        std::uint16_t colour = 0x7FFF;
        std::uint16_t spin = 0;         ///< how it is turned, and how fast it turns
        std::int16_t spinRate = 0;      ///< 65536ths of a turn a millisecond
        std::int16_t weight = 256;      ///< how gravity takes it (256: as anything; less: it drifts)
        fx pos[3] = {0, 0, 0};
        fx velocity[3] = {0, 0, 0};
        std::int32_t leftMs = 0;
    };
    inline constexpr std::uint8_t kPieceShard = 1;
    inline constexpr std::uint8_t kPieceSpeck = 2; ///< plain, tiny, in `colour`
    inline constexpr std::uint8_t kPieceShell = 3; ///< brass (models/shell.mdl); it rings once as it lands
    inline constexpr std::uint8_t kPieceShotShell = 4; ///< a shotgun's (models/shotgunshell.mdl); it rings too
    inline constexpr std::uint8_t kPieceCan = 5;       ///< a soda can out of its machine (plain, in `colour`)
    inline constexpr int kMaxPieces = 64;

    /// What something breaks into, by what it is made of (the game's
    /// func_breakable materials: 0 glass, 1 wood, 2 metal, 3 flesh, 4 cinder
    /// block, 5 ceiling tile, 6 computer, 7 the glass that never breaks, 8
    /// rock): how many pieces for each 4096 square units of its biggest
    /// face (and no fewer than `least`), how big, how fast they leave, how
    /// they fall, how solid they look and how long they lie.
    struct BreakStyle
    {
        std::uint8_t per, least, most, half, alpha;
        std::int16_t speed, weight;
        std::int16_t lastMs;
    };
    inline constexpr BreakStyle breakStyle(int material)
    {
        switch (material)
        {
        case 0:  return {6, 6, 16, 3, 18, 140, 256, 1200};  // glass: many, light, soon gone
        case 7:  return {6, 6, 16, 3, 18, 140, 256, 1200};
        case 1:  return {4, 5, 12, 4, 31, 170, 256, 1800};  // wood
        case 2:  return {3, 4, 10, 4, 31, 150, 256, 1800};  // metal: few and heavy
        case 5:  return {4, 4, 10, 4, 31, 60, 110, 1800};   // ceiling tiles drift down
        case 6:  return {4, 5, 12, 3, 31, 180, 256, 1800};  // a computer
        default: return {4, 5, 12, 4, 31, 160, 256, 1800};  // cinder block, rock, flesh
        }
    }

    /// Something seen for a moment, that is nothing but seen: an alien
    /// slave's bolt (from `a` to `b`), a burst (at `a`, `size` units across
    /// at its widest). Whoever draws the frame draws these.
    struct Effect
    {
        std::uint8_t kind = 0; ///< kEffect*; 0 = none
        fx a[3] = {0, 0, 0};
        fx b[3] = {0, 0, 0};
        std::int16_t size = 0;
        std::int16_t leftMs = 0;
        std::int16_t wholeMs = 0;
        /// For kEffectSprite: which of the map's sprites (played through
        /// once over its moment), and its colour (RGB555; 0x7FFF: its own).
        std::uint16_t sprite = 0xFFFF;
        std::uint16_t colour = 0x7FFF;
    };
    inline constexpr std::uint8_t kEffectBolt = 1;
    inline constexpr std::uint8_t kEffectBurst = 2; ///< an explosion
    inline constexpr std::uint8_t kEffectRing = 3;  ///< a houndeye's blast
    inline constexpr std::uint8_t kEffectSplat = 4; ///< spit landing
    inline constexpr std::uint8_t kEffectTracer = 5; ///< a round's line, from `a` to `b`
    inline constexpr std::uint8_t kEffectMuzzle = 6; ///< the flash at a gun's end
    inline constexpr std::uint8_t kEffectBlood = 7;  ///< where a shot lands on what bleeds (`size`: 1 if it bleeds yellow)
    /// A sprite of the map's, `size` units across (negative: sixteenths of
    /// the sprite's own size), going from `a` to `b` over its moment.
    inline constexpr std::uint8_t kEffectSprite = 8;
    /// A beam from `a` to `b`, `size` units wide, in `colour` (the gauss's,
    /// the egon's).
    inline constexpr std::uint8_t kEffectBeam = 9;
    /// (All of these on a DSi, half on a DS: ActorWorld::effectLimit.)
    inline constexpr int kMaxEffects = 48;

    // ------------------------------------------------------------- thinking
    //
    // What the actors work out that takes long -- a way to somewhere -- can
    // be worked out outside the frame: asked for in one tick, done while the console would otherwise
    // wait for the screen (a worker of the 3D engine's: source/game/Think.cpp),
    // and taken up in a later tick. HlThink.hpp does the work; here is only
    // where it is asked for and left. Nothing of it is saved: an actor
    // loaded from a save simply asks again.

    inline constexpr std::uint8_t kThinkPlan = 1;  ///< walkPlan: where to make for next

    inline constexpr std::uint8_t kThinkFree = 0;
    inline constexpr std::uint8_t kThinkAsked = 1;
    inline constexpr std::uint8_t kThinkWorking = 2;
    inline constexpr std::uint8_t kThinkDone = 3;

    /// One actor's question, and its answer.
    struct ThinkSlot
    {
        std::uint8_t kind = 0;
        std::uint8_t state = kThinkFree;
        // Asked: where to, or from what; how (crouched or not, how far a
        // straight walk is looked for, how near cover may be); and where the
        // actor was on its way (an answer to an older question is not taken).
        fx at[3] = {0, 0, 0};
        std::int16_t small = 0;
        std::int16_t reach = 0;
        std::int16_t least = 0;
        std::int16_t basis = -1;
        // Found.
        bool found = false;
        fx out[3] = {0, 0, 0};
        std::int16_t goalNode = -1;
        std::int16_t fromNode = -1;
        std::int16_t door = -1; ///< a shut door on the way, to be asked to open (a brush)
    };

    /// The questions of all of them. `deferred`: asked and answered later
    /// (the console); otherwise every question is answered as it is asked,
    /// as it always was (the host's tests, and before the worker is up).
    struct ThinkQueue
    {
        ThinkSlot *slots = nullptr;
        int count = 0;
        bool deferred = false;
        /// Set while a map is being left: whatever is being worked out is
        /// finished at once, without stopping for the frame, and thrown away.
        volatile bool abort = false;
        int next = 0; ///< where the worker looks for the next question
    };

    /// Where long work stops a moment for the frame (the console's worker
    /// sets it; null: never).
    inline void (*thinkYield)() = nullptr;
    /// Called every so often in a long loop: every `every` calls, a stop.
    inline void thinkPoint(int &counter, int every)
    {
        if (thinkYield != nullptr && ++counter >= every)
        {
            counter = 0;
            thinkYield();
        }
    }

    struct AiWorld; // (HlAi.hpp)

    /// The actors of the loaded map and the models they are made of.
    struct ActorWorld
    {
        ActorState *states = nullptr;
        int count = 0;              ///< the map's own, then kMaxVisitors spare
        const ModelView *models = nullptr; ///< by ModelRefs index; header null if not loaded
        int modelCount = 0;
        /// The collision's copy: one box per state (MoveWorld::boxes).
        SolidBox *boxes = nullptr;
        /// How the map starts each of them (the first `recordCount`): what a
        /// monstermaker sets one of its own back to.
        const ActorRecord *records = nullptr;
        int recordCount = 0;
        const std::uint32_t *nameHashes = nullptr;
        /// Where something that walks may stand, and the ways between
        /// (HlLevel.hpp, PathLink). May be none.
        const PathNode *nodes = nullptr;
        const PathLink *links = nullptr;
        int nodeCount = 0;
        int linkCount = 0;
        /// How each link is just now, a byte a link (kLink*), kept by the
        /// logic, which knows the doors; null: all of them are open.
        std::uint8_t *linkShut = nullptr;
        /// What is being thought out for them outside the frame (not saved:
        /// a pointer). Null: everything is worked out as it is asked.
        ThinkQueue *think = nullptr;
        /// What the monsters think, hear and remember (HlAi.hpp; not saved: a
        /// pointer). Null: they do not think.
        AiWorld *ai = nullptr;
        /// The doors those who walk have asked to have opened since the
        /// logic last looked (brush indices): it opens them.
        std::uint16_t doorWanted[4] = {};
        int doorsWanted = 0;
        std::uint32_t seed = 12345; ///< what little chance there is in a fight
        std::int32_t hurtLookMs = 0; ///< until the places that hurt are next looked at (HlMonsters.hpp)
        /// Set by what happens to the player's things this tick (kGot*): the
        /// caller shows it, and clears it.
        std::uint32_t got = 0;
        /// A game between consoles (HlNetGame.hpp): what lies about is
        /// everybody's. Touching something does not take it: it is noted
        /// in `wanted` (the actor; -1 none; -2 one has been asked for and
        /// no other is noted yet), and whoever runs the game says who has
        /// it.
        bool shared = false;
        std::int16_t wanted = -1;
        Projectile shots[kMaxProjectiles];
        Effect effects[kMaxEffects];
        Gib gibs[kMaxGibs];
        Decal decals[kMaxDecals];
        int decalLimit = 24;
        int decalNext = 0; ///< (no longer used: marks give way by worth, decalAdd; kept for saves)
        Piece pieces[kMaxPieces];
        int pieceLimit = 24;
        /// How long the small things and the marks last, of what they are
        /// made to (a hundredth each): a DSi keeps them longer.
        int lifePercent = 100;
        int markLifePercent = 100;
        int gibLimit = kMaxGibs / 2;
        int shotLimit = kMaxProjectiles / 2;
        int effectLimit = kMaxEffects / 2;
        /// Something loud enough to turn a head (a shot, a blast): where,
        /// and for how much longer it counts as just heard.
        fx noise[3] = {0, 0, 0};
        std::int32_t noiseMs = 0;
        /// Where the player is (kept each tick, for what is not told).
        fx foe[3] = {0, 0, 0};
        /// Until any soldier may speak again (they do not talk over each
        /// other), and which question one has asked that wants an answer
        /// (0: none; 1: "check in"; 2: any other).
        std::int32_t speakMs = 0;
        std::uint8_t asked = 0;
        /// The models a person and an alien come apart into (into `models`),
        /// or -1 where the map has not got them: then they do not come apart.
        std::int16_t humanGibs = -1, alienGibs = -1;
    };

    inline const ModelView *actorModel(const ActorWorld &a, const ActorState &actor)
    {
        if (actor.model >= a.modelCount || a.models == nullptr ||
            a.models[actor.model].header == nullptr)
            return nullptr;
        return &a.models[actor.model];
    }

    /// Start `actor` on the clip called `clip` (a nameHash; 0 or unknown:
    /// leave it on what it is doing and say so).
    inline bool actorPlay(const ActorWorld &a, ActorState &actor, std::uint32_t clip, bool once)
    {
        const ModelView *model = actorModel(a, actor);
        const int index = (model != nullptr && clip != 0) ? model->findClip(clip) : -1;
        if (index < 0)
            return false;
        actor.clip = static_cast<std::int16_t>(index);
        actor.frame = 0;
        actor.once = once;
        actor.finished = false;
        actor.frozen = false;
        return true;
    }

    /// What the actor's model has its bone controllers set to (poseModel's
    /// `controls`): how far a barnacle's tongue is out, to the nearest unit;
    /// which way a turret's gun is turned from the way the turret faces, and
    /// how far up (the game's signs: a turret on a ceiling is upside down,
    /// and both go the other way). False if they are all at rest.
    inline bool actorControls(const ActorState &actor, std::int32_t *controls)
    {
        controls[0] = controls[1] = controls[2] = controls[3] = 0;
        // One of the people's head, turned to whom he talks to (HlAiTalk.hpp).
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Scientist) ||
            actor.kind == static_cast<std::uint8_t>(ActorKind::Guard) ||
            actor.kind == static_cast<std::uint8_t>(ActorKind::Sitter) ||
            actor.kind == static_cast<std::uint8_t>(ActorKind::Gman))
        {
            controls[0] = static_cast<std::int16_t>(actor.aimYaw);
            return controls[0] != 0;
        }
        if (isTurret(actor.kind))
        {
            const bool ceiling = (actor.flags & kActorCeiling) != 0;
            controls[0] = ceiling ? actor.yaw - actor.aimYaw : actor.aimYaw - actor.yaw;
            controls[1] = ceiling ? actor.aimPitch : -actor.aimPitch;
            return true;
        }
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Apache))
        {
            // Its gun, turned and tipped (HlMilitary.hpp).
            controls[0] = static_cast<std::int16_t>(actor.aimYaw);
            controls[1] = static_cast<std::int16_t>(actor.aimPitch);
            return controls[0] != 0 || controls[1] != 0;
        }
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Ichthyosaur))
        {
            // Its tail, against its turn (the game's yaw_adj / 4).
            controls[0] = -(static_cast<std::int16_t>(actor.wantYaw) / 4);
            return controls[0] != 0;
        }
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Osprey))
        {
            // Its rotors, tipped from up (0) to forward (a quarter turn
            // down) as it gathers speed.
            controls[0] = actor.aimPitch;
            return controls[0] != 0;
        }
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Barnacle))
        {
            // Its tongue let down to `reach`: the slide is what is added to
            // the length its bones already have (kBarnacleTongueBones -- what
            // the game's -100 makes up for), so that the tip is where it
            // grabs.
            const fx slide = actor.reach > kBarnacleTongueBones ? actor.reach - kBarnacleTongueBones : 0;
            controls[0] = -(slide & ~(kFxOne - 1));
            return true;
        }
        return false;
    }

    /// How it is tipped besides the way it faces, for what swims and flies
    /// that way (65536 to the circle: nose up, and rolled): false for the
    /// rest, which are turned about the upright alone.
    inline bool actorTilt(const ActorState &actor, std::int32_t &pitch, std::int32_t &roll)
    {
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Ichthyosaur))
        {
            pitch = actor.aimPitch;
            roll = actor.aimYaw;
            return true;
        }
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Apache) ||
            actor.kind == static_cast<std::uint8_t>(ActorKind::Boid))
        {
            pitch = actor.wantPitch;
            roll = actor.wantYaw;
            return true;
        }
        return false;
    }

    /// Back to what it does when nothing directs it.
    inline void actorRest(ActorState &actor)
    {
        actor.clip = actor.restClip;
        actor.frame = 0;
        // (Someone sitting plays each of his ways of sitting through, then
        // takes up another: actorAdvance.)
        actor.once = actor.kind == static_cast<std::uint8_t>(ActorKind::Sitter);
        actor.finished = false;
        actor.frozen = actor.kind == static_cast<std::uint8_t>(ActorKind::Pose) ||
                       actor.kind == static_cast<std::uint8_t>(ActorKind::Furniture);
    }

    /// The first clip of `model` that is for `activity` (its ModelClip flags,
    /// kClipActivityShift); -1 if none is.
    inline int clipOfActivity(const ModelView &model, std::uint8_t activity)
    {
        for (int c = 0; c < model.header->clipCount; ++c)
        {
            if (((model.clips[c].flags >> kClipActivityShift) & 0xFF) == activity)
                return c;
        }
        return -1;
    }

    /// The game's ACT_IDLE (HlAi.hpp's kActIdle: HlXen.hpp checks they agree).
    inline constexpr std::uint8_t kXenIdleActivity = 1;

    /// A plant of Xen as the map starts it: somewhere in its sway, at a
    /// pace of its own (0.7 to 1.4 of its clip's), as the game starts them
    /// at random -- the same each time here. A light shines.
    inline void xenStart(ActorState &actor, const ModelView &model, int index)
    {
        std::uint32_t roll = static_cast<std::uint32_t>(index) * 2654435761u ^
                             static_cast<std::uint32_t>(actor.pos[0] ^ (actor.pos[1] << 3));
        roll = roll * 1664525u + 1013904223u;
        const auto plant = static_cast<XenPlant>(actor.item);
        if (plant == XenPlant::Tree || plant == XenPlant::Light)
        {
            const int idle = clipOfActivity(model, kXenIdleActivity);
            if (idle >= 0)
                actor.clip = actor.restClip = static_cast<std::int16_t>(idle);
        }
        actor.frozen = false;
        actor.once = false;
        actor.attack = 0;
        actor.cooldownMs = 0;
        actor.reach = plant == XenPlant::Light ? 255 : 0;
        if (actor.clip >= 0 && actor.clip < model.header->clipCount && model.clips[actor.clip].frameCount > 1)
            actor.frame = ((roll >> 8) % (model.clips[actor.clip].frameCount - 1u)) << 8;
        actor.speed = static_cast<fx>(179 + (roll >> 20) % 180);
    }

    /// A point let fall from `pos` (HlWalk.hpp, which knows about lines):
    /// onto the world or a brush under it, at most `down` lower. False, and
    /// nothing moved, if it starts inside something or nothing is there.
    inline bool pointToFloor(const MoveWorld &world, fx *pos, fx down);

    /// The five ways a scientist sits, in the order the game has them, and
    /// which he takes up after one is done: mostly plain sitting, as there.
    inline std::uint32_t sittingClip(int which)
    {
        static const char *const kNames[] = {"sitlookleft", "sitlookright", "sitscared", "sitting2",
                                             "sitting3"};
        return nameHash(kNames[which < 0 || which > 4 ? 4 : which]);
    }
    /// Which way of sitting comes next for `actor`: by chance, from his
    /// own count of them -- which is his, and goes into a save with him
    /// (nothing the game does by chance is kept anywhere but in its state).
    inline int sittingNext(ActorState &actor)
    {
        ++actor.struck;
        std::uint32_t roll = (static_cast<std::uint32_t>(actor.pos[0]) ^ (static_cast<std::uint32_t>(actor.pos[1]) << 7)) +
                             actor.struck * 2654435761u;
        roll = roll * 1664525u + 1013904223u;
        const int of = static_cast<int>((roll >> 16) % 100);
        return of < 40 ? 4 : (of < 50 ? 0 : (of < 60 ? 1 : (of < 80 ? 3 : 2)));
    }

    /// The box the player cannot walk into, kept where the actor is.
    inline void actorPlaceBox(const ActorWorld &a, int index)
    {
        if (a.boxes == nullptr)
            return;
        const ActorState &actor = a.states[index];
        SolidBox &box = a.boxes[index];
        box.active = actor.active && (actor.flags & kActorSolid) != 0;
        // A person's width and height; a prop's from its model, within
        // reason (a forklift is not a pillar 16 units wide, nor a wall).
        fx half = fxFromInt(16), height = fxFromInt(72);
        const ModelView *model = actorModel(a, actor);
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Xen))
        {
            // The game's own boxes for them (the big fungus's is its cap,
            // up on its legs: under it is walked through).
            static const std::int16_t kBoxes[][3] = {
                {4, 0, 32}, {16, 0, 64}, {40, 0, 120}, {48, 110, 240}, {30, 0, 188}, {80, 0, 32}};
            const int which = actor.item < static_cast<std::uint8_t>(XenPlant::Count) ? actor.item : 0;
            for (int i = 0; i < 2; ++i)
            {
                box.mins[i] = actor.pos[i] - fxFromInt(kBoxes[which][0]);
                box.maxs[i] = actor.pos[i] + fxFromInt(kBoxes[which][0]);
            }
            box.mins[2] = actor.pos[2] + fxFromInt(kBoxes[which][1]);
            box.maxs[2] = actor.pos[2] + fxFromInt(kBoxes[which][2]);
            return;
        }
        if (model != nullptr && actor.kind >= static_cast<std::uint8_t>(ActorKind::Generic))
        {
            int reach = 0;
            for (int i = 0; i < 2; ++i)
            {
                reach = reach > -model->header->mins[i] ? reach : -model->header->mins[i];
                reach = reach > model->header->maxs[i] ? reach : model->header->maxs[i];
            }
            half = fxFromInt(reach < 8 ? 8 : (reach > 48 ? 48 : reach));
            const int top = model->header->maxs[2];
            height = fxFromInt(top < 8 ? 8 : (top > 96 ? 96 : top));
        }
        for (int i = 0; i < 2; ++i)
        {
            box.mins[i] = actor.pos[i] - half;
            box.maxs[i] = actor.pos[i] + half;
        }
        box.mins[2] = actor.pos[2];
        box.maxs[2] = actor.pos[2] + height;
        // (Another player in a match: their origin is their middle, as the
        // game's player's is, and theirs is its box -- crouched, half.)
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Player))
        {
            const fx halfHigh = fxFromInt(actor.mood != 0 ? 18 : 36);
            for (int i = 0; i < 2; ++i)
            {
                box.mins[i] = actor.pos[i] - fxFromInt(16);
                box.maxs[i] = actor.pos[i] + fxFromInt(16);
            }
            box.mins[2] = actor.pos[2] - halfHigh;
            box.maxs[2] = actor.pos[2] + halfHigh;
        }
        // (A turret is where it is fixed, as much below as above; under a
        // ceiling, all below.)
        if (isTurret(actor.kind))
        {
            box.mins[2] = actor.pos[2] - ((actor.flags & kActorCeiling) ? height : fxFromInt(16));
            if (actor.flags & kActorCeiling)
                box.maxs[2] = actor.pos[2] + fxFromInt(16);
        }
    }

    /// Stand an actor on the ground under it: looked for from `up` above its
    /// feet to `down` below them, as something the player's size (whose
    /// middle is 36 units above its feet). The ground is whatever is there --
    /// the world or a brush: c1a0d's scientist at the cart stands on a floor
    /// of grates (func_walls) over a pit. False, with the actor left where it
    /// is, if there is no ground in that reach or it stands where such a
    /// thing does not fit.
    inline bool actorToFloor(const MoveWorld &world, ActorState &actor, fx up, fx down);

    /// What is where it is put, not let down to a floor: a cycler, a Xen
    /// plant, what hangs from a ceiling, a tripmine, what flies and what
    /// swims.
    inline bool actorStaysPut(std::uint8_t kind)
    {
        switch (static_cast<ActorKind>(kind))
        {
        case ActorKind::Cycler:
        case ActorKind::Xen:
        case ActorKind::Barnacle:
        case ActorKind::Tripmine:
        case ActorKind::Controller:
        case ActorKind::Leech:
        case ActorKind::Osprey:
        case ActorKind::Ichthyosaur:
        case ActorKind::Tentacle:
        case ActorKind::Apache:
        case ActorKind::Nihilanth:
        case ActorKind::Boid:
            return true;
        default:
            return isTurret(kind);
        }
    }

    /// Whether the actor's origin is the middle of its body (kActorCentred).
    inline bool actorCentred(const ActorState &actor)
    {
        return actor.kind == static_cast<std::uint8_t>(ActorKind::Generic) && (actor.flags & kActorCentred) != 0;
    }

    /// actorToFloor, for one whose origin may be the middle of it: its feet
    /// 36 below are what come to the floor. (Not dropped -- left where it is
    /// -- if there is no room: the hologram on its projector.)
    inline bool actorToFloorAs(const MoveWorld &world, ActorState &actor, fx up, fx down)
    {
        if (!actorCentred(actor))
            return actorToFloor(world, actor, up, down);
        ActorState feet = actor;
        feet.pos[2] -= fxFromInt(36);
        if (!actorToFloor(world, feet, up, down))
            return false;
        actor.pos[2] = feet.pos[2] + fxFromInt(36);
        return true;
    }

    inline bool actorToFloor(const MoveWorld &world, ActorState &actor, fx up, fx down)
    {
        const fx from[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36) + up};
        const fx to[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(36) - down};
        const Trace tr = traceAll(world, false, from, to);
        if (tr.startSolid || tr.allSolid || tr.fraction >= kFractionOne)
            return false;
        actor.pos[2] = tr.endPos[2] - fxFromInt(36);
        return true;
    }

    /// Let a small thing down onto what is under it -- the world, a brush
    /// (a chair is a func_wall), anyone already placed -- as the game drops
    /// what is 36 units tall or less: in its crouched box, whose middle is 18
    /// above the feet (an item's 16-unit box is given that hull too). From a
    /// unit above its feet to 256 below; left where it is if nothing is there.
    inline void actorToFloorSmall(const MoveWorld &world, ActorState &actor)
    {
        fx from[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(18 + 1)};
        const fx to[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + fxFromInt(18 - 256)};
        Trace tr = traceAll(world, true, from, to);
        // Started inside something (the cubicle round c1a0d's second sitter):
        // the game's trace goes on out of it to what is under it; this one
        // stops where it starts, so it is started again a little lower.
        for (int step = 0; step < 16 && tr.startSolid; ++step)
        {
            from[2] -= fxFromInt(4);
            tr = traceAll(world, true, from, to);
        }
        if (!tr.startSolid && !tr.allSolid && tr.fraction < kFractionOne)
            actor.pos[2] = tr.endPos[2] - fxFromInt(18);
    }

    /// Set one actor to how its record starts it: where it stands (let down
    /// onto the floor under it), which way it faces, in which clip.
    inline void actorSetup(ActorWorld &a, int index, const ActorRecord &record, const MoveWorld &world)
    {
        ActorState &actor = a.states[index];
        actor = ActorState();
        actor.active = true;
        actor.model = record.model;
        actor.name = record.name;
        actor.nameHash = a.nameHashes != nullptr ? a.nameHashes[record.name] : 0;
        actor.yaw = record.yaw;
        actor.body = record.body;
        // (An osprey's `item`, and an apache's, is where it flies: it has
        // the one skin.)
        const bool flies = record.kind == static_cast<std::uint8_t>(ActorKind::Osprey) ||
                           record.kind == static_cast<std::uint8_t>(ActorKind::Apache) ||
                           record.kind == static_cast<std::uint8_t>(ActorKind::BigMomma) || // (hers: her path)
                           record.kind == static_cast<std::uint8_t>(ActorKind::Boid);       // (its: its leader)
        // (A plant of Xen's: which, and a light's colour; one skin.)
        const bool plant = record.kind == static_cast<std::uint8_t>(ActorKind::Xen);
        if (record.kind != static_cast<std::uint8_t>(ActorKind::Item) && !flies && !plant)
            actor.skin = static_cast<std::uint8_t>(record.item >> kActorSkinShift);
        actor.kind = record.kind;
        actor.flags = record.flags;
        actor.light = static_cast<std::uint8_t>(record.light & ~kActorLightFadeCorpse);
        // (An item's kind; a soldier's weapons; and for a soldier or a monster
        // that thinks, when it sets off what it sets off.)
        actor.item = record.kind == static_cast<std::uint8_t>(ActorKind::Item) ||
                             record.kind == static_cast<std::uint8_t>(ActorKind::Grunt) || plant ||
                             (actorThinks(record.kind) && !flies)
                         ? static_cast<std::uint8_t>(record.item)
                         : std::uint8_t(0);
        if (record.kind == static_cast<std::uint8_t>(ActorKind::Tripmine))
        {
            // Which way it points (as a turret's gun: up is positive).
            actor.aimYaw = record.yaw;
            actor.aimPitch = static_cast<std::int32_t>(static_cast<std::int8_t>(record.item & 0xFF)) * 256;
        }
        actor.fires = record.target;
        for (int k = 0; k < 3; ++k)
            actor.pos[k] = record.origin[k];
        // Onto the floor, as the game drops what it spawns -- each kind as
        // the game does it. A body, and a piece of furniture, fall as a
        // point (they have no size there until they have landed); someone
        // sitting, and an item, come down in the low box they are given,
        // onto the chair or the shelf; a cycler and what hangs from a ceiling
        // stay put; the rest come down in the box they walk about in.
        const bool point = record.kind == static_cast<std::uint8_t>(ActorKind::Pose) ||
                           record.kind == static_cast<std::uint8_t>(ActorKind::Furniture);
        const bool stays = actorStaysPut(record.kind);
        if (stays)
        {
        }
        else if (actorCentred(actor))
        {
            // (StartMonster: a unit up and DROP_TO_FLOOR, its box's middle
            // where its origin is.)
            actorToFloorAs(world, actor, fxFromInt(1), fxFromInt(256));
        }
        else if (point)
        {
            pointToFloor(world, actor.pos, fxFromInt(256));
        }
        else if (record.kind == static_cast<std::uint8_t>(ActorKind::Sitter) ||
                 record.kind == static_cast<std::uint8_t>(ActorKind::Item))
        {
            // (Onto the chair, not through it to the floor -- c1a0d's -- and
            // onto the floor of a case too low for the player: its suit.)
            actorToFloorSmall(world, actor);
        }
        else if (!actorToFloor(world, actor, fxFromInt(1), fxFromInt(256)))
        {
            // No room where the map has it (a map's author goes by the
            // model, not by the box it moves in): a stair's height up, or a
            // little to one side, is tried before it is left as it is.
            static const int kTry[][3] = {{0, 0, 18}, {16, 0, 0},  {-16, 0, 0},  {0, 16, 0},
                                          {0, -16, 0}, {16, 0, 18}, {-16, 0, 18}, {0, 16, 18},
                                          {0, -16, 18}, {0, 0, 36}};
            for (const int *offset : kTry)
            {
                ActorState moved = actor;
                for (int k = 0; k < 3; ++k)
                    moved.pos[k] += fxFromInt(offset[k]);
                if (actorToFloor(world, moved, fxFromInt(1), fxFromInt(256)))
                {
                    for (int k = 0; k < 3; ++k)
                        actor.pos[k] = moved.pos[k];
                    break;
                }
            }
        }

        const ModelView *model = actorModel(a, actor);
        if (model != nullptr && model->header->clipCount != 0)
        {
            int clip = record.clip != 0 ? model->findClip(record.clip) : 0;
            // (A monster_generic stands about as the game's does, in its
            // ACT_IDLE, if its model has one: not in its first sequence --
            // the hologram's is a walk.)
            if (record.clip == 0 && actor.kind == static_cast<std::uint8_t>(ActorKind::Generic))
            {
                const int idle = clipOfActivity(*model, kXenIdleActivity);
                clip = idle >= 0 ? idle : clip;
            }
            // (Each of those sitting starts in a way of his own.)
            if (actor.kind == static_cast<std::uint8_t>(ActorKind::Sitter))
            {
                const int own = model->findClip(sittingClip(index % 5));
                clip = own >= 0 ? own : clip;
            }
            actor.restClip = static_cast<std::int16_t>(clip < 0 ? 0 : clip);
        }
        actorRest(actor);
        // A cycler runs its sequence; furniture and poses hold theirs.
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Cycler))
            actor.frozen = false;
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Xen) && model != nullptr)
            xenStart(actor, *model, index);
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Item))
            actor.frozen = true;
    }

    /// Set every actor to how the map starts it.
    inline void actorsInit(ActorWorld &a, const ActorRecord *records, int recordCount,
                           const std::uint32_t *nameHashes, const MoveWorld &world)
    {
        a.records = records;
        a.recordCount = recordCount;
        a.nameHashes = nameHashes;
        a.got = 0;
        a.noiseMs = a.speakMs = 0;
        a.asked = 0;
        for (Projectile &shot : a.shots)
            shot = Projectile();
        for (Effect &effect : a.effects)
            effect = Effect();
        for (Gib &gib : a.gibs)
            gib = Gib();
        for (int i = 0; i < a.count; ++i)
        {
            a.states[i] = ActorState();
            if (i < recordCount)
            {
                actorSetup(a, i, records[i], world);
                // Kept for a monstermaker: not there yet.
                if (records[i].flags & kActorDormant)
                    a.states[i].active = false;
            }
            actorPlaceBox(a, i);
        }
    }

    /// Advance one actor's clip by `ms`. Returns the frame it was at before
    /// (Q8), for whoever watches for the clip's events.
#ifdef HL_DSL
    // (In the game library, the engine's own, in ITCM: game/GameApi.hpp.)
    std::uint32_t actorAdvance(const ActorWorld &a, ActorState &actor, int ms);
#else
    HL_HOT std::uint32_t actorAdvance(const ActorWorld &a, ActorState &actor, int ms)
    {
        const std::uint32_t before = actor.frame;
        const ModelView *model = actorModel(a, actor);
        if (model == nullptr || actor.clip < 0 || actor.frozen || actor.finished)
            return before;
        ModelClip clip = model->clips[actor.clip];
        // A cycler goes round whatever the sequence says; a clip played
        // "once" stops at its end whatever it says.
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Cycler) ||
            actor.kind == static_cast<std::uint8_t>(ActorKind::Xen))
            clip.flags |= kClipLoops;
        if (actor.once)
            clip.flags &= static_cast<std::uint16_t>(~kClipLoops);
        // (A plant sways at a pace of its own: `speed`, Q8.)
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Xen) && actor.speed > 0)
            ms = static_cast<int>((static_cast<std::int64_t>(ms) * actor.speed) >> 8);
        // (Half again as fast: for the rest of this clip.)
        if (actor.quickClip.value != 0 && actor.quickClip.value == actor.clip + 1)
            ms = ms * 3 / 2;
        bool finished = false;
        actor.frame = advanceClip(clip, actor.frame, ms, finished);
        actor.finished = finished;
        if (finished || actor.quickClip.value != actor.clip + 1)
            actor.quickClip.value = 0;
        // Someone sitting, left to himself, has done with one way of
        // sitting: another.
        // (With minds about -- HlAi.hpp -- his own sees to that: SittingThink.)
        if (finished && actor.kind == static_cast<std::uint8_t>(ActorKind::Sitter) &&
            actor.scriptPhase == kActorFree && actor.ai == 0 && a.ai == nullptr)
        {
            const int next = model->findClip(sittingClip(sittingNext(actor)));
            if (next >= 0)
            {
                actor.clip = static_cast<std::int16_t>(next);
                actor.frame = 0;
                actor.finished = false;
            }
        }
        return before;
    }
#endif

    /// How fast `clip` of the actor's model carries it, in Q12 units a
    /// second: its stride over its length. `fallback` if it says nothing.
    inline fx actorClipSpeed(const ActorWorld &a, const ActorState &actor, std::uint32_t clip,
                             fx fallback)
    {
        const ModelView *model = actorModel(a, actor);
        const int index = model != nullptr ? model->findClip(clip) : -1;
        if (index < 0)
            return fallback;
        const ModelClip &c = model->clips[index];
        const fx stride = fxLength(c.travel);
        if (stride <= 0 || c.frameCount < 2)
            return fallback;
        // stride * fps / (frames - 1); fps is Q8.
        return static_cast<fx>((static_cast<std::int64_t>(stride) * c.fps) / ((c.frameCount - 1) * 256));
    }
    /// Something to be seen for `ms` milliseconds (kEffect*). When all the
    /// places are taken the oldest gives way.
    inline Effect &effectAdd(ActorWorld &a, std::uint8_t kind, const fx *from, const fx *to, int size, int ms)
    {
        Effect *slot = &a.effects[0];
        const int limit = a.effectLimit < 1 ? 1 : (a.effectLimit > kMaxEffects ? kMaxEffects : a.effectLimit);
        for (int i = 0; i < limit; ++i)
        {
            Effect &effect = a.effects[i];
            if (effect.kind == 0)
            {
                slot = &effect;
                break;
            }
            if (effect.leftMs < slot->leftMs)
                slot = &effect;
        }
        slot->kind = kind;
        for (int i = 0; i < 3; ++i)
        {
            slot->a[i] = from[i];
            slot->b[i] = to != nullptr ? to[i] : from[i];
        }
        slot->size = static_cast<std::int16_t>(size);
        slot->leftMs = slot->wholeMs = static_cast<std::int16_t>(ms);
        slot->sprite = 0xFFFF;
        slot->colour = 0x7FFF;
        return *slot;
    }

    inline constexpr std::uint8_t kLinkOpen = 0;   ///< nothing in the way
    inline constexpr std::uint8_t kLinkDoor = 1;   ///< a door that is shut, and opens for whoever can open doors
    inline constexpr std::uint8_t kLinkNoWay = 2;  ///< locked, or not broken yet

    /// Leave a mark: `picture` (a kDecal*), `half` units from its middle to
    /// its edge, in `colour`, on the wall at `at` that faces `normal`
    /// (Q12). The oldest gives way.
    /// What a mark is worth keeping, when one has to give way for another
    /// (decalAdd): a bullet's hole or a blow's dent least, then blood and
    /// spit, then a blast's scorch; and what the map put there, or a pane's
    /// crack, most -- a passing shot never takes their place.
    constexpr int decalWorth(const Decal &decal)
    {
        const int family = decalFamily(decal.picture);
        if ((decal.turn & kDecalTheMaps) || family == 9)
            return 3;
        return family == 0 || family == 4 || family >= 21 ? 0 : (family == 10 ? 2 : 1);
    }

    inline Decal &decalAdd(ActorWorld &a, std::uint8_t picture, const fx *at, const fx *normal, int half,
                           std::uint16_t colour, int brush = -1, bool exact = false)
    {
        const int limit = a.decalLimit < 1 ? 1 : (a.decalLimit > kMaxDecals ? kMaxDecals : a.decalLimit);
        const std::int32_t life = decalLife(picture) / 100 * a.markLifePercent;
        half = half < 1 ? 1 : (half > 64 ? 64 : half);
        const int family = decalFamily(picture);
        // (Holes, dents and a wall's damage: blows on one spot. Anything
        // else -- blood, soot, spit -- is a mark where it landed, and only
        // one that lies on top of another of its kind is that one.)
        const bool blows = family == 0 || family == 4 || family >= 21;
        // One of its family there already -- on the same thing, facing the
        // same way, its middle within this one's reach (anywhere on the
        // pane, for a crack) -- is this one: moved to the new place and
        // begun again. Ten blows on one spot are one mark, not ten sheets
        // one on another for the hardware to draw.
        for (int i = 0; i < limit; ++i)
        {
            Decal &old = a.decals[i];
            if (!old.active || old.brush != brush || decalFamily(old.picture) != family)
                continue;
            // (A mark the map put there is itself and no other: it is this
            // one only if it is this picture in this very place. Two of
            // the map's stains side by side stay two.)
            if (exact)
            {
                const fx gap[3] = {old.at[0] - at[0], old.at[1] - at[1], old.at[2] - at[2]};
                if (old.picture != picture || fxLength(gap) > fxFromInt(1))
                    continue;
                old.leftMs = decalLife(picture) / 100 * a.markLifePercent;
                return old;
            }
            if (old.turn & kDecalTheMaps)
                continue; // (... nor is anything else ever it)
            const std::int64_t facing = static_cast<std::int64_t>(old.normal[0]) * normal[0] +
                                        static_cast<std::int64_t>(old.normal[1]) * normal[1] +
                                        static_cast<std::int64_t>(old.normal[2]) * normal[2];
            if (facing < (static_cast<std::int64_t>(kFxOne) * kFxOne * 9) / 10)
                continue;
            const fx d[3] = {old.at[0] - at[0], old.at[1] - at[1], old.at[2] - at[2]};
            const int reach = blows ? (old.half > half ? old.half : half) : (old.half < half ? old.half : half) / 2;
            if (family != 9 && fxLength(d) > fxFromInt(reach))
                continue;
            old.half = static_cast<std::uint8_t>(old.half > half ? old.half : half);
            old.leftMs = life;
            for (int k = 0; k < 3; ++k)
            {
                old.at[k] = at[k];
                old.normal[k] = normal[k];
            }
            return old;
        }
        // A place for it: a free one; else the one least worth keeping
        // (what has least time left, of those), if it is worth no more than
        // this one -- else the least of this one's own worth after all, so
        // that a mark is never refused.
        Decal probe;
        probe.picture = picture;
        probe.turn = exact ? kDecalTheMaps : 0;
        const int worth = decalWorth(probe);
        Decal *slot = nullptr;
        for (int i = 0; i < limit && slot == nullptr; ++i)
        {
            if (!a.decals[i].active)
                slot = &a.decals[i];
        }
        for (int pass = 0; pass < 2 && slot == nullptr; ++pass)
        {
            for (int i = 0; i < limit; ++i)
            {
                Decal &one = a.decals[i];
                const int its = decalWorth(one);
                if (pass == 0 && its > worth)
                    continue;
                if (slot == nullptr || its < decalWorth(*slot) ||
                    (its == decalWorth(*slot) && one.leftMs < slot->leftMs))
                    slot = &one;
            }
        }
        Decal &decal = *slot;
        decal.active = true;
        decal.leftMs = life;
        decal.picture = picture;
        decal.half = static_cast<std::uint8_t>(half);
        a.seed = a.seed * 1664525u + 1013904223u;
        decal.turn = static_cast<std::uint8_t>(((a.seed >> 12) & 3) | (exact ? kDecalTheMaps : 0));
        decal.brush = static_cast<std::int16_t>(brush);
        decal.colour = colour;
        for (int i = 0; i < 3; ++i)
        {
            decal.at[i] = at[i];
            decal.normal[i] = normal[i];
        }
        return decal;
    }

    /// Throw a small thing. The one with least time left gives way.
    inline Piece &pieceAdd(ActorWorld &a, std::uint8_t kind, const fx *pos, const fx *velocity, int ms)
    {
        const int limit = a.pieceLimit < 1 ? 1 : (a.pieceLimit > kMaxPieces ? kMaxPieces : a.pieceLimit);
        Piece *slot = &a.pieces[0];
        for (int i = 0; i < limit; ++i)
        {
            if (a.pieces[i].kind == 0)
            {
                slot = &a.pieces[i];
                break;
            }
            if (a.pieces[i].leftMs < slot->leftMs)
                slot = &a.pieces[i];
        }
        *slot = Piece();
        slot->kind = kind;
        a.seed = a.seed * 1664525u + 1013904223u;
        slot->spin = static_cast<std::uint16_t>(a.seed >> 8);
        slot->spinRate = static_cast<std::int16_t>(static_cast<int>((a.seed >> 12) % 161) - 80);
        for (int i = 0; i < 3; ++i)
        {
            slot->pos[i] = pos[i];
            slot->velocity[i] = velocity[i];
        }
        slot->leftMs = ms * a.lifePercent / 100;
        return *slot;
    }

    /// A spent casing out of a gun (the game's EjectBrass): `kind` is
    /// kPieceShell or kPieceShotShell. It is drawn as the game's model of it
    /// where the map has it and there is room (ModelDraw.arm.cpp), and as a
    /// small triangle in its colour where not.
    inline Piece &shellEject(ActorWorld &a, std::uint8_t kind, const fx *from, const fx *velocity)
    {
        Piece &shell = pieceAdd(a, kind, from, velocity, 1200);
        shell.half = 1;
        shell.colour = static_cast<std::uint16_t>(kind == kPieceShotShell ? (24 | 5 << 5 | 4 << 10) : (27 | 21 << 5 | 6 << 10));
        return shell;
    }

    /// The marks, for `ms` more milliseconds: each goes when its time is
    /// up. Returns how many there are.
    inline int decalsTick(ActorWorld &a, int ms)
    {
        int live = 0;
        for (Decal &decal : a.decals)
        {
            if (!decal.active)
                continue;
            decal.leftMs -= ms;
            if (decal.leftMs <= 0)
                decal.active = false;
            else
                ++live;
        }
        return live;
    }

    /// Something made of `material` and as big as the box `mins`..`maxs`
    /// has broken: its pieces, each a patch of one of `textures` (the
    /// thing's own; none: plain), thrown out from its middle.
    inline int piecesBurst(ActorWorld &a, const fx *mins, const fx *maxs, int material, const std::uint16_t *textures,
                           int textureCount)
    {
        const BreakStyle style = breakStyle(material);
        // Its biggest face, in units of 64 by 64.
        const int size[3] = {fxToInt(maxs[0] - mins[0]), fxToInt(maxs[1] - mins[1]), fxToInt(maxs[2] - mins[2])};
        const int thin = size[0] <= size[1] && size[0] <= size[2] ? 0 : (size[1] <= size[2] ? 1 : 2);
        const int face = (size[(thin + 1) % 3] * size[(thin + 2) % 3]) / 4096;
        int count = style.per * face;
        count = count < style.least ? style.least : (count > style.most ? style.most : count);
        auto chance = [&a]() -> std::uint32_t
        {
            a.seed = a.seed * 1664525u + 1013904223u;
            return a.seed >> 8;
        };
        for (int n = 0; n < count; ++n)
        {
            fx at[3], velocity[3];
            for (int k = 0; k < 3; ++k)
            {
                const fx span = maxs[k] - mins[k];
                at[k] = mins[k] + static_cast<fx>((static_cast<std::int64_t>(span) * (chance() & 255)) >> 8);
                // Out from the middle, and anyhow besides.
                const fx out = at[k] - (mins[k] + span / 2);
                velocity[k] = out * 3 + fxFromInt(static_cast<int>(chance() % static_cast<std::uint32_t>(style.speed * 2 + 1)) - style.speed);
            }
            velocity[2] += fxFromInt(style.speed / 2);
            Piece &piece = pieceAdd(a, kPieceShard, at, velocity, style.lastMs + static_cast<int>(chance() % 400));
            piece.half = static_cast<std::uint8_t>(style.half + chance() % 3);
            piece.alpha = style.alpha;
            piece.weight = style.weight;
            piece.texture = textureCount > 0 ? textures[chance() % static_cast<std::uint32_t>(textureCount)] : 0xFFFF;
            piece.s = static_cast<std::uint8_t>(chance() & 7);
            piece.t = static_cast<std::uint8_t>(chance() & 7);
            piece.colour = textureCount > 0 ? 0x7FFF : static_cast<std::uint16_t>(14 | 14 << 5 | 14 << 10);
        }
        return count;
    }

    /// Something set flying. Null if there is no room for another.
    inline Projectile *projectileAdd(ActorWorld &a, std::uint8_t kind, const fx *pos, const fx *velocity, int ms)
    {
        const int limit = a.shotLimit < 1 ? 1 : (a.shotLimit > kMaxProjectiles ? kMaxProjectiles : a.shotLimit);
        for (int i = 0; i < limit; ++i)
        {
            Projectile &shot = a.shots[i];
            if (shot.active)
                continue;
            shot.active = true;
            shot.kind = kind;
            shot.sprite = 0xFFFF;
            for (int i = 0; i < 3; ++i)
            {
                shot.pos[i] = pos[i];
                shot.velocity[i] = velocity[i];
            }
            shot.leftMs = ms;
            return &shot;
        }
        return nullptr;
    }

    /// Throw a piece. When all the places are taken, the one with least
    /// time left gives way.
    inline Gib &gibAdd(ActorWorld &a, int model, int body, const fx *pos, const fx *velocity, int ms)
    {
        const int limit = a.gibLimit < 1 ? 1 : (a.gibLimit > kMaxGibs ? kMaxGibs : a.gibLimit);
        Gib *slot = &a.gibs[0];
        for (int i = 0; i < limit; ++i)
        {
            if (!a.gibs[i].active)
            {
                slot = &a.gibs[i];
                break;
            }
            if (a.gibs[i].leftMs < slot->leftMs)
                slot = &a.gibs[i];
        }
        *slot = Gib();
        slot->active = true;
        slot->model = static_cast<std::uint16_t>(model);
        slot->body = static_cast<std::uint8_t>(body);
        for (int i = 0; i < 3; ++i)
        {
            slot->pos[i] = pos[i];
            slot->velocity[i] = velocity[i];
        }
        slot->leftMs = ms;
        return *slot;
    }

    /// Find, among the map's models, the two that things come apart into
    /// (by their names: the game's hgibs and agibs).
    inline void gibModelsFind(ActorWorld &a, const ModelRef *refs, int count)
    {
        a.humanGibs = a.alienGibs = -1;
        for (int i = 0; i < count; ++i)
        {
            if (std::strcmp(refs[i].name, "hgibs") == 0)
                a.humanGibs = static_cast<std::int16_t>(i);
            else if (std::strcmp(refs[i].name, "agibs") == 0)
                a.alienGibs = static_cast<std::int16_t>(i);
        }
    }

    /// How many pieces the gib model `model` has (its first body part's
    /// sub-models); 0 if the map has not got it.
    inline int gibPieces(const ActorWorld &a, int model)
    {
        if (model < 0 || model >= a.modelCount || a.models == nullptr || a.models[model].header == nullptr ||
            a.models[model].header->bodyPartCount == 0)
            return 0;
        return a.models[model].bodyParts[0].count;
    }
} // namespace hldata
