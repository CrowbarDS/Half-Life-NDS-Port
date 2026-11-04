#pragma once

// Who is whose enemy: the game's classes and how each feels about each
// (dlls/cbase.h CLASS_*, dlls/monsters.h R_*, and the table in
// dlls/monsters.cpp, CBaseMonster::IRelationship), kept as the game has them.
// A monster goes for the player AND for whatever else it hates or dislikes:
// a bullsquid for a headcrab, the aliens for the soldiers and the people.
//
// std-only and inline: the host tests run it.

#include <cstdint>

#include "HlLevel.hpp"

namespace hldata
{
    // The classes (CLASS_*).
    inline constexpr std::uint8_t kClassNone = 0;
    inline constexpr std::uint8_t kClassMachine = 1;
    inline constexpr std::uint8_t kClassPlayer = 2;
    inline constexpr std::uint8_t kClassHumanPassive = 3;
    inline constexpr std::uint8_t kClassHumanMilitary = 4;
    inline constexpr std::uint8_t kClassAlienMilitary = 5;
    inline constexpr std::uint8_t kClassAlienPassive = 6;
    inline constexpr std::uint8_t kClassAlienMonster = 7;
    inline constexpr std::uint8_t kClassAlienPrey = 8;
    inline constexpr std::uint8_t kClassAlienPredator = 9;
    inline constexpr std::uint8_t kClassInsect = 10;
    inline constexpr std::uint8_t kClassPlayerAlly = 11;
    inline constexpr std::uint8_t kClassPlayerBioweapon = 12; ///< a snark the player let go
    inline constexpr std::uint8_t kClassAlienBioweapon = 13;  ///< a hornet
    inline constexpr std::uint8_t kClassBarnacle = 99;        ///< (not in the table: special-cased)

    // How one feels about another (R_*): the higher, the more it wants it
    // dead; below none, it runs from it or is on its side.
    inline constexpr int kRelAlly = -2;    ///< R_AL
    inline constexpr int kRelFear = -1;    ///< R_FR
    inline constexpr int kRelNone = 0;     ///< R_NO
    inline constexpr int kRelDislike = 1;  ///< R_DL
    inline constexpr int kRelHate = 2;     ///< R_HT
    inline constexpr int kRelNemesis = 3;  ///< R_NM

    /// The game's table: [self][target], over the first fourteen classes.
    inline int classRelationship(std::uint8_t self, std::uint8_t target)
    {
        constexpr int AL = kRelAlly, FR = kRelFear, NO = kRelNone, DL = kRelDislike, HT = kRelHate;
        static const std::int8_t kTable[14][14] = {
            //      NONE MACH PLYR HPASS HMIL AMIL APASS AMONST APREY APRED INSECT PLRALY PBWPN ABWPN
            /*NONE*/ {NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO},
            /*MACH*/ {NO, NO, DL, DL, NO, DL, DL, DL, DL, DL, NO, DL, DL, DL},
            /*PLYR*/ {NO, DL, NO, NO, DL, DL, DL, DL, DL, DL, NO, NO, DL, DL},
            /*HPAS*/ {NO, NO, AL, AL, HT, FR, NO, HT, DL, FR, NO, AL, NO, NO},
            /*HMIL*/ {NO, NO, HT, DL, NO, HT, DL, DL, DL, DL, NO, HT, NO, NO},
            /*AMIL*/ {NO, DL, HT, DL, HT, NO, NO, NO, NO, NO, NO, DL, NO, NO},
            /*APAS*/ {NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO},
            /*AMON*/ {NO, DL, DL, DL, DL, NO, NO, NO, NO, NO, NO, DL, NO, NO},
            /*APRY*/ {NO, NO, DL, DL, DL, NO, NO, NO, NO, FR, NO, DL, NO, NO},
            /*APRD*/ {NO, NO, DL, DL, DL, NO, NO, NO, HT, DL, NO, DL, NO, NO},
            /*INSC*/ {FR, FR, FR, FR, FR, NO, FR, FR, FR, FR, NO, FR, NO, NO},
            /*PALY*/ {NO, DL, AL, AL, DL, DL, DL, DL, DL, DL, NO, NO, NO, NO},
            /*PBWP*/ {NO, NO, DL, DL, DL, DL, DL, DL, DL, DL, NO, DL, NO, DL},
            /*ABWP*/ {NO, NO, DL, DL, DL, AL, NO, DL, DL, NO, NO, DL, DL, NO},
        };
        // (The game reads the table with the barnacle's class too, past its
        // end: a barnacle looks to the table as nothing at all, and nothing
        // is anything to it -- what it eats it finds by its own rule.)
        if (self >= 14 || target >= 14)
            return kRelNone;
        (void)FR;
        return kTable[self][target];
    }

    /// The class of each kind as its own code has it (Classify), before
    /// anything about the one actor: a turret that is off is nothing, and
    /// the barnacle's own: classOf in HlAi.hpp has those.
    inline std::uint8_t kindClass(std::uint8_t kind)
    {
        switch (static_cast<ActorKind>(kind))
        {
        case ActorKind::Scientist:
        case ActorKind::Sitter:
            return kClassHumanPassive;
        case ActorKind::Guard:
        case ActorKind::Generic: // (monster_generic: the game's PLAYER_ALLY)
            return kClassPlayerAlly;
        case ActorKind::Headcrab:
        case ActorKind::BabyCrab:
            return kClassAlienPrey;
        case ActorKind::Zombie:
        case ActorKind::Houndeye:
        case ActorKind::Barnacle:
        case ActorKind::Gargantua:
        case ActorKind::Ichthyosaur:
        case ActorKind::Tentacle:
        case ActorKind::BigMomma:
            return kClassAlienMonster;
        case ActorKind::Slave:
        case ActorKind::AlienGrunt:
        case ActorKind::Controller:
        case ActorKind::Nihilanth:
            return kClassAlienMilitary;
        case ActorKind::Cockroach:
        case ActorKind::Leech:
        case ActorKind::Boid:
            return kClassInsect;
        case ActorKind::Snark: // (a maker's: the player's own is a shot, not an actor)
            return kClassAlienBioweapon;
        case ActorKind::Bullsquid:
            return kClassAlienPredator;
        case ActorKind::Grunt:
        case ActorKind::Assassin:
        case ActorKind::Apache: // (one of the soldiers, as the game has it; the Osprey is a machine)
            return kClassHumanMilitary;
        case ActorKind::Miniturret:
        case ActorKind::Sentry:
        case ActorKind::Turret:
        case ActorKind::Osprey:
            return kClassMachine;
        case ActorKind::Player:
            return kClassPlayer;
        default:
            return kClassNone; // the man in the suit, furniture, poses, items
        }
    }
} // namespace hldata
