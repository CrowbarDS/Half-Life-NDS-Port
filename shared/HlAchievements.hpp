#pragma once

// The achievements: the 68 of Half-Life: Legacy (a mod of the game, on
// Steam as app 3619040), earned in the original maps.
//
// Kept on the card as <card>/_nds/hl_nds/data/achievements.dat, one for the
// console and not one per save (as Steam keeps them, for the player):
// written by the game, behind the same kind of fixed header as the settings
// (HlSettings.hpp), and for the same reason.
//
// How they are earned is all here, for the tests to try: the game reports
// what happens (HlAchEvents.hpp) and AchievementTracker decides. The places
// are those of Valve's maps -- the mod changed some of its own -- each found
// in the map's entities (docs/TECHNICAL.md has the list).

#include <cstdint>
#include <cstring>
#include <vector>

#include "HlYas.hpp"

#include "HlAchEvents.hpp"
#include "HlLevel.hpp"

namespace hldata
{
    /// In the order of the mod's own list. A number is what the file keeps:
    /// new ones go at the end, and none is ever taken away.
    enum Achievement : std::uint8_t
    {
        kAchTestAchievement,
        kAchC2a1Complete,
        kAchC4a2Complete,
        kAchUplinkComplete,
        kAchTrainingComplete,
        kAchC0a0Complete,
        kAchC1a0Complete,
        kAchC1a1Complete,
        kAchC1a2Complete,
        kAchC1a3Complete,
        kAchC1a4Complete,
        kAchC2a2Complete,
        kAchC2a3Complete,
        kAchC2a4Complete,
        kAchC2a5Complete,
        kAchC2a5eComplete,
        kAchC3a1Complete,
        kAchC3a2Complete,
        kAchC4a1Complete,
        kAchC4a3Complete,
        kAchC4a4Complete,
        kAchC5a1Failed,
        kAchC5a1Complete,
        kAchC1a1BarnDie,
        kAchC1a2dBarney,
        kAchC1a2Turret,
        kAchC1a2aTurret,
        kAchC1a2Grenades,
        kAch5Barn,
        kAch5Tur,
        kAchC1a0dStopit,
        kAchFridgefreezer,
        kAchHeadhumpers,
        kAchFreed,
        kAchC1a0dAol,
        kAchC1a0dDyson,
        kAchC1a0dToldoff,
        kAchC1a3aConveyorbase,
        kAchFemdead,
        kAchC2a4dPeta,
        kAchC2a4dDeathmachine,
        kAchC2a4fGauss,
        kAchC2a4fSolved,
        kAchC2a5Apache1,
        kAchC2a5aApache2,
        kAchC2a5dLift,
        kAchC2a5gGarg,
        kAchC3a2Entry,
        kAchC3a2aStation1,
        kAchC3a2aStation2,
        kAchC3a2dPeak,
        kAchControlDie,
        kAchInit,
        kAchC2a5aApache3,
        kAchBeverages,
        kAchMarathon,
        kAchC1a0Naughty,
        kAchCheater,
        kAchUplinkExplorer,
        kAchNihilSci,
        kAchRiller,
        kAchLoperGarg,
        kAchLoperEggz,
        kAchC2a2b2Lift,
        kAchC1a4iBarnacle,
        kAchC2a3aPipe,
        kAchC3a1Secret,
        kAchC4a1eSpiral,
        kAchievementCount
    };

    struct AchievementInfo
    {
        const char *title; ///< the player's name for it
        const char *text;  ///< what to do
        bool hidden;       ///< not described until it is earned
        std::uint16_t goal; ///< how many, for one that counts; 0: once is enough
    };

    inline constexpr AchievementInfo kAchievements[kAchievementCount] = {
        {"You're just as bad as them.",
         "Kill a scientist.", false, 0},
        {"Zapped to oblivion",
         "Complete Power Up.", false, 0},
        {"Poor Big Momma...",
         "Complete Gonarch's Lair.", false, 0},
        {"On-the-Line",
         "Complete Uplink.", false, 0},
        {"Settled in!",
         "Complete the Hazard Course.", false, 0},
        {"Morning, Mr. Freeman",
         "Sit through the Tram Ride and complete Black Mesa Inbound.", false, 0},
        {"A small discrepency",
         "Witness (and indirectly cause) the Black Mesa Incident and complete Anomalous Materials.", false, 0},
        {"To the surface",
         "Conform to Eli's mission and complete Unforeseen Consequences.", false, 0},
        {"Bad day to be Dr. Newell",
         "Complete Office Complex.", false, 0},
        {"Bad day to be part of the Science Team",
         "Survive the HECU infiltration and complete \\\"We've Got Hostiles.\\\"", false, 0},
        {"4000 Degrees Kelvin",
         "Complete Blast Pit.", false, 0},
        {"Liftoff",
         "Complete On a Rail.", false, 0},
        {"Nice catch",
         "Complete Apprenhension.", false, 0},
        {"Got enough conveyors?",
         "Complete the labyrinth that is Residue Processing.", false, 0},
        {"You weren't authorized to know about that.",
         "Complete Questionable Ethics.", false, 0},
        {"War Games",
         "Complete Surface Tension.", false, 0},
        {"Descension",
         "Survive the mortars and complete \\\"Forget About Freeman.\\\"", false, 0},
        {"Brave New World",
         "Complete Lambda Core.", false, 0},
        {"So beautiful I could cry",
         "Complete Xen.", false, 0},
        {"Penultimatum",
         "Complete Interloper.", false, 0},
        {"I think I just saved the world...",
         "Complete Nihilanth.", false, 0},
        {"...or I suppose not.",
         "Reject the offer. Goodbye, Dr. Freeman.", true, 0},
        {"...or did I?",
         "Accept the offer.", true, 0},
        {"I'm not following you, you evil person!",
         "In Unforeseen Consequences, kill the security guard fighting the zombie to take his gun.", false, 0},
        {"Explorer",
         "In Office Complex, find Barney's hideout.", false, 0},
        {"Stealth \\u0026 Action",
         "Disengage the first turret in Office Complex.", false, 0},
        {"Explorer II",
         "Find the Maintenance Area in Office Complex.", false, 0},
        {"Explorer III",
         "Find the grenade cache near the end of Office Complex.", false, 0},
        {"No more ropes",
         "Kill 15 barnacles.", false, 15},
        {"Owowowowowow!",
         "Destroy 5 turrets. (excludes ceiling mounted turrets)", false, 5},
        {"Not Magnusson's Meal...",
         "In Anomalous Materials, destroy, uh... you-know-who's lunch.", false, 0},
        {"Got the Chills",
         "In Office Complex, solve the chiller puzzle.", false, 0},
        {"Bye bye headhumpers!",
         "Kill 500 headcrabs.", false, 500},
        {"Freed, in another kind of way",
         "Kill 100 Alien Slaves. Don't worry, you'll come to regret this.", false, 100},
        {"You've got mail",
         "In Anomalous Materials, open the Message Terminal.", false, 0},
        {"Hygiene is important",
         "In Anomalous Materials, dry your hands.", false, 0},
        {"Defiant",
         "In Anomalous Materials, refuse to put on the HEV Suit.", false, 0},
        {"Explorer IV",
         "In We've Got Hostiles, find the base upon the conveyor belts.", false, 0},
        {"Martial hierarchy",
         "Kill an assassin.", false, 0},
        {"Kind-hearted soul",
         "In Questionable Ethics, free the Alien Grunt.", false, 0},
        {"A new toy! Can I play?",
         "In Questionable Ethics, activate the steriliser.", false, 0},
        {"He let it overcharge.",
         "In Questionable Ethics, retrieve the Tau Cannon.", false, 0},
        {"Defeating physics",
         "In Questionable Ethics, use the laser to destroy the wall.", false, 0},
        {"Me and my Tau Cannon",
         "In Surface Tension, destroy the first Apache.", false, 0},
        {"Me and my Tau Cannon II",
         "In Surface Tension, destroy the second Apache after leaving the dam.", false, 0},
        {"Better than a Snake",
         "In Surface Tension, successfully dodge all the tripmines before descending.", false, 0},
        {"Mortar'd to oblivion",
         "In Surface Tension, kill the Gargantua.", false, 0},
        {"2 days have paid off",
         "Reach the bottom of the Lambda Complex.", false, 0},
        {"PUMP STATION 01",
         "In Lambda Core, activate Pump Station 01.", false, 0},
        {"PUMP STATION 02",
         "In Lambda Core, activate Pump Station 02.", false, 0},
        {"All of those puzzles have paid off",
         "Reach Level A in the Lambda Complex.", false, 0},
        {"Boss baby",
         "Kill a Controller.", false, 0},
        {"Not the Only Achievement",
         "Welcome to Half-Life Legacy.", false, 0},
        {"Me and my RPG",
         "In Surface Tension, destroy the third Apache on the cliffside.", false, 0},
        {"Yummers",
         "Heal yourself with a soda.", false, 0},
        {"Marathon",
         "In any desired order, complete the Hazard Course, Uplink, and Half-Life back to back. Doing them in one sitting is NOT required.", false, 0},
        {"Instigator",
         "In Anomalous Materials, sound the alarm at the reception.", false, 0},
        {"Cheater",
         "Enable cheats.", true, 0},
        {"Uplink Explorer",
         "In Uplink, find the shotgun room.", false, 0},
        {"Hello, Gordon!",
         "In Nihilanth, find the one and only living scientist in one of the chambers.", false, 0},
        {"Signature move",
         "In On a Rail, find Dave Riller's signature.", true, 0},
        {"Third time's the charm",
         "In Interloper, kill the Gargantua.", false, 0},
        {"Demolition Man",
         "Break 250 objects (crates, boxes, barrels, etc.)", false, 250},
        {"Explorer V",
         "In On a Rail, find the secret cache aside an elevator.", false, 0},
        {"Explorer VI",
         "In Blast Pit, find the hidden chargers.", false, 0},
        {"Explorer VII",
         "In Apprehension, find the hidden cache.", false, 0},
        {"Explorer VIII",
         "In Forget About Freeman, find the secret room.", false, 0},
        {"Explorer IX",
         "In Interloper, find the spiral.", false, 0},
    };

#if !defined(__NDS__)
    /// Each one's name in the mod (its Steam API name), by row: for the host's
    /// tools and tests alone -- the console never reads it, and has none.
    inline constexpr const char *kAchievementApis[kAchievementCount] = {
        "TEST_ACHIEVEMENT",
        "C2A1_COMPLETE",
        "C4A2_COMPLETE",
        "UPLINK_COMPLETE",
        "TRAINING_COMPLETE",
        "C0A0_COMPLETE",
        "C1A0_COMPLETE",
        "C1A1_COMPLETE",
        "C1A2_COMPLETE",
        "C1A3_COMPLETE",
        "C1A4_COMPLETE",
        "C2A2_COMPLETE",
        "C2A3_COMPLETE",
        "C2A4_COMPLETE",
        "C2A5_COMPLETE",
        "C2A5E_COMPLETE",
        "C3A1_COMPLETE",
        "C3A2_COMPLETE",
        "C4A1_COMPLETE",
        "C4A3_COMPLETE",
        "C4A4_COMPLETE",
        "C5A1_FAILED",
        "C5A1_COMPLETE",
        "C1A1_BARN_DIE",
        "C1A2D_BARNEY",
        "C1A2_TURRET",
        "C1A2A_TURRET",
        "C1A2_GRENADES",
        "5BARN",
        "5TUR",
        "C1A0D_STOPIT",
        "FRIDGEFREEZER",
        "HEADHUMPERS",
        "FREED",
        "C1A0D_AOL",
        "C1A0D_DYSON",
        "C1A0D_TOLDOFF",
        "C1A3A_CONVEYORBASE",
        "FEMDEAD",
        "C2A4D_PETA",
        "C2A4D_DEATHMACHINE",
        "C2A4F_GAUSS",
        "C2A4F_SOLVED",
        "C2A5_APACHE1",
        "C2A5A_APACHE2",
        "C2A5D_LIFT",
        "C2A5G_GARG",
        "C3A2_ENTRY",
        "C3A2A_STATION1",
        "C3A2A_STATION2",
        "C3A2D_PEAK",
        "CONTROL_DIE",
        "INIT",
        "C2A5A_APACHE3",
        "BEVERAGES",
        "MARATHON",
        "C1A0_NAUGHTY",
        "CHEATER",
        "UPLINK_EXPLORER",
        "NIHIL_SCI",
        "RILLER",
        "LOPER_GARG",
        "LOPER_EGGZ",
        "C2A2B2_LIFT",
        "C1A4I_BARNACLE",
        "C2A3A_PIPE",
        "C3A1_SECRET",
        "C4A1E_SPIRAL",
    };
#endif

    // The achievements by chapter, for the list the player reads: the
    // chapters in the order they are played, then Uplink, then those that
    // belong to no one place.
    inline constexpr int kAchChapterCount = 21;
    inline constexpr const char *kAchChapterNames[kAchChapterCount] = {
        "HAZARD COURSE",
        "BLACK MESA INBOUND",
        "ANOMALOUS MATERIALS",
        "UNFORESEEN CONSEQUENCES",
        "OFFICE COMPLEX",
        "WE'VE GOT HOSTILES",
        "BLAST PIT",
        "POWER UP",
        "ON A RAIL",
        "APPREHENSION",
        "RESIDUE PROCESSING",
        "QUESTIONABLE ETHICS",
        "SURFACE TENSION",
        "FORGET ABOUT FREEMAN",
        "LAMBDA CORE",
        "XEN",
        "GONARCH'S LAIR",
        "INTERLOPER",
        "NIHILANTH",
        "UPLINK",
        "GENERAL",
    };
    /// Every achievement once, chapter by chapter ...
    inline constexpr std::uint8_t kAchByChapter[kAchievementCount] = {
        kAchTrainingComplete,
        kAchC0a0Complete,
        kAchC1a0Complete, kAchC1a0dStopit, kAchC1a0Naughty, kAchC1a0dAol, kAchC1a0dDyson, kAchC1a0dToldoff,
        kAchC1a1Complete, kAchC1a1BarnDie,
        kAchC1a2Complete, kAchC1a2Turret, kAchC1a2dBarney, kAchC1a2aTurret, kAchC1a2Grenades, kAchFridgefreezer,
        kAchC1a3Complete, kAchC1a3aConveyorbase,
        kAchC1a4Complete, kAchC1a4iBarnacle,
        kAchC2a1Complete,
        kAchC2a2Complete, kAchC2a2b2Lift, kAchRiller,
        kAchC2a3Complete, kAchC2a3aPipe,
        kAchC2a4Complete,
        kAchC2a5Complete, kAchC2a4dPeta, kAchC2a4dDeathmachine, kAchC2a4fGauss, kAchC2a4fSolved,
        kAchC2a5eComplete, kAchC2a5Apache1, kAchC2a5aApache2, kAchC2a5aApache3, kAchC2a5dLift, kAchC2a5gGarg,
        kAchC3a1Complete, kAchC3a1Secret,
        kAchC3a2Complete, kAchC3a2Entry, kAchC3a2aStation1, kAchC3a2aStation2, kAchC3a2dPeak,
        kAchC4a1Complete,
        kAchC4a2Complete,
        kAchC4a3Complete, kAchLoperGarg, kAchC4a1eSpiral,
        kAchC4a4Complete, kAchNihilSci, kAchC5a1Complete, kAchC5a1Failed,
        kAchUplinkComplete, kAchUplinkExplorer,
        kAchInit, kAchTestAchievement, kAchFemdead, kAchControlDie, kAchBeverages, kAch5Barn, kAch5Tur, kAchHeadhumpers, kAchFreed, kAchLoperEggz, kAchMarathon, kAchCheater,
    };
    /// ... where each chapter's begin in it (and, last, the end).
    inline constexpr std::uint8_t kAchChapterStart[kAchChapterCount + 1] = {0, 1, 2, 8, 10, 16, 18, 20, 21, 24, 26, 27, 32, 38, 40, 45, 46, 47, 50, 54, 56, 68};

    // What is counted towards an achievement, across every game played.
    enum AchCounter : std::uint8_t
    {
        kCountHeadcrabs,
        kCountSlaves,
        kCountBarnacles,
        kCountSentries,
        kCountBroken,
        kAchCounterCount
    };
    inline constexpr std::uint8_t kCounterAchievement[kAchCounterCount] = {
        kAchHeadhumpers, kAchFreed, kAch5Barn, kAch5Tur, kAchLoperEggz};

    struct AchievementData
    {
        /// A bit for each one earned (by Achievement).
        std::uint32_t unlocked[3] = {};
        std::uint16_t counts[kAchCounterCount] = {};

        template <class Ar>
        void serialize(Ar &ar)
        {
            for (std::uint32_t &bits : unlocked)
                ar &bits;
            for (std::uint16_t &count : counts)
                ar &count;
        }
    };
    static_assert(kAchievementCount <= 3 * 32);

    inline constexpr char kAchievementsMagic[4] = {'H', 'L', 'A', 'C'};
    inline constexpr std::uint16_t kAchievementsVersion = 1;
    /// The most an achievements file can be (what the console reads it into).
    inline constexpr std::size_t kAchievementsMostBytes = 64;

    struct AchievementsHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t payloadBytes;
        std::uint32_t crc32;
    };
    static_assert(sizeof(AchievementsHeader) == 12);

    inline std::vector<std::uint8_t> writeAchievements(const AchievementData &data)
    {
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        yas::mem_ostream os;
        yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
        AchievementData copy = data;
        oa &copy;
        const auto buf = os.get_intrusive_buffer();

        AchievementsHeader header{};
        std::memcpy(header.magic, kAchievementsMagic, sizeof(header.magic));
        header.version = kAchievementsVersion;
        header.payloadBytes = static_cast<std::uint16_t>(buf.size);
        header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data),
                             static_cast<std::uint32_t>(buf.size));
        std::vector<std::uint8_t> out(sizeof(header) + buf.size);
        std::memcpy(out.data(), &header, sizeof(header));
        std::memcpy(out.data() + sizeof(header), buf.data, buf.size);
        return out;
    }

    /// False -- leaving `out` as it was -- for anything that is not an
    /// achievements file of this version. Bits past the last achievement
    /// there is are dropped.
    inline bool readAchievements(const std::uint8_t *bytes, std::size_t size, AchievementData &out)
    {
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        AchievementsHeader header;
        if (size < sizeof(header))
            return false;
        std::memcpy(&header, bytes, sizeof(header));
        if (std::memcmp(header.magic, kAchievementsMagic, sizeof(header.magic)) != 0 ||
            header.version != kAchievementsVersion || header.payloadBytes != size - sizeof(header) ||
            header.payloadBytes != 3 * 4 + kAchCounterCount * 2 ||
            crc32(bytes + sizeof(header), header.payloadBytes) != header.crc32)
            return false;
        AchievementData read;
        yas::mem_istream is(bytes + sizeof(header), header.payloadBytes);
        yas::binary_iarchive<yas::mem_istream, flags> ia(is);
        ia &read;
        for (int bit = kAchievementCount; bit < 3 * 32; ++bit)
            read.unlocked[bit / 32] &= ~(1u << (bit % 32));
        out = read;
        return true;
    }

    // ------------------------------------------------------------ the rules

    namespace ach
    {
        /// A chapter is done when the way out of its last map is taken into
        /// the next chapter's first (worldspawn's chaptertitle says which
        /// maps begin one; its message is wrong in places).
        struct ChapterEnd
        {
            std::uint32_t left, entered;
            std::uint8_t id;
        };
        inline constexpr ChapterEnd kChapterEnds[] = {
            {nameHash("c0a0e"), nameHash("c1a0"), kAchC0a0Complete},
            {nameHash("c1a0b"), nameHash("c1a0c"), kAchC1a0Complete},
            {nameHash("c1a0e"), nameHash("c1a0c"), kAchC1a0Complete},
            {nameHash("c1a1c"), nameHash("c1a2"), kAchC1a1Complete},
            {nameHash("c1a2b"), nameHash("c1a3"), kAchC1a2Complete},
            {nameHash("c1a3"), nameHash("c1a4"), kAchC1a3Complete},
            {nameHash("c1a4j"), nameHash("c2a1"), kAchC1a4Complete},
            {nameHash("c2a1"), nameHash("c2a2"), kAchC2a1Complete},
            {nameHash("c2a2g"), nameHash("c2a3"), kAchC2a2Complete},
            {nameHash("c2a3e"), nameHash("c2a4"), kAchC2a3Complete},
            {nameHash("c2a4c"), nameHash("c2a4d"), kAchC2a4Complete},
            {nameHash("c2a4g"), nameHash("c2a5"), kAchC2a5Complete},
            {nameHash("c2a5g"), nameHash("c3a1"), kAchC2a5eComplete},
            {nameHash("c3a1b"), nameHash("c3a2e"), kAchC3a1Complete},
            {nameHash("c3a2d"), nameHash("c4a1"), kAchC3a2Complete},
            {nameHash("c4a1"), nameHash("c4a2"), kAchC4a1Complete},
            {nameHash("c4a2b"), nameHash("c4a1a"), kAchC4a2Complete},
            {nameHash("c4a1f"), nameHash("c4a3"), kAchC4a3Complete},
            {nameHash("c4a3"), nameHash("c5a1"), kAchC4a4Complete},
        };

        /// Something in a map is used: a button, the relay behind it, a
        /// trigger. Only when something there by that name is still there
        /// to answer (the hook is in logicFire).
        struct FireRule
        {
            std::uint32_t map, name;
            std::uint8_t id;
        };
        inline constexpr FireRule kFireRules[] = {
            // The ends of the Hazard Course and of Uplink: their
            // trigger_endsection; and the two ends of the game.
            {nameHash("t0a0d"), nameHash("tr_endchange"), kAchTrainingComplete},
            {nameHash("hldemo1"), nameHash("end"), kAchUplinkComplete},
            {nameHash("c5a1"), nameHash("end_mm"), kAchC5a1Complete},
            {nameHash("c5a1"), nameHash("loser_mm"), kAchC5a1Failed},
            // Anomalous Materials: the casserole (all five buttons of the
            // microwave), the security desk's alarm, the terminal by the
            // start, the hand dryer, and Barney's telling-off for going
            // on without the suit.
            {nameHash("c1a0d"), nameHash("microwavepopmm1"), kAchC1a0dStopit},
            {nameHash("c1a0"), nameHash("buzzerlightsonmm"), kAchC1a0Naughty},
            {nameHash("c1a0"), nameHash("introroomgizmomm"), kAchC1a0dAol},
            {nameHash("c1a0d"), nameHash("hand_dryermm"), kAchC1a0dDyson},
            {nameHash("c1a0d"), nameHash("airlockbarneyleanspeak1"), kAchC1a0dToldoff},
            // Office Complex: the lever that turns the first turret off, and
            // the wheel that opens the freezer.
            {nameHash("c1a2"), nameHash("turret_off"), kAchC1a2Turret},
            {nameHash("c1a2c"), nameHash("freezdor"), kAchFridgefreezer},
            // Questionable Ethics: the alien grunt's glass, the steriliser,
            // the laser through the wall.
            {nameHash("c2a4d"), nameHash("glassbreak_mm"), kAchC2a4dPeta},
            {nameHash("c2a4d"), nameHash("ster1_mm"), kAchC2a4dDeathmachine},
            {nameHash("c2a4f"), nameHash("explode"), kAchC2a4fSolved},
            // Surface Tension: the lift down past the tripmines (only with
            // every one the map laid still there: AchContext).
            {nameHash("c2a5d"), nameHash("lift_1"), kAchC2a5dLift},
            // Lambda Core: the two pumps, and the top of the complex.
            {nameHash("c3a2a"), nameHash("mm1"), kAchC3a2aStation1},
            {nameHash("c3a2a"), nameHash("mm2"), kAchC3a2aStation2},
            {nameHash("c3a2d"), nameHash("heymaster"), kAchC3a2dPeak},
            // Uplink: the room the shotguns fall into.
            {nameHash("hldemo2"), nameHash("shotgun_ceiling"), kAchUplinkExplorer},
        };

        /// A monster dies. `map` 0: anywhere; `name` 0: any of that kind.
        struct KillRule
        {
            std::uint32_t map;
            ActorKind kind;
            std::uint32_t name;
            bool byPlayer; ///< only if the player did it
            std::uint8_t id;
        };
        inline constexpr KillRule kKillRules[] = {
            {0, ActorKind::Scientist, 0, true, kAchTestAchievement},
            {0, ActorKind::Assassin, 0, true, kAchFemdead},
            {0, ActorKind::Controller, 0, true, kAchControlDie},
            // The guard fighting the zombie, early in Unforeseen
            // Consequences.
            {nameHash("c1a1"), ActorKind::Guard, nameHash("fighting_barney"), true, kAchC1a1BarnDie},
            // Surface Tension's three Apaches: by the dam; past it (in
            // either of the two maps that overlap there); on the cliffs.
            {nameHash("c2a5"), ActorKind::Apache, nameHash("apache"), false, kAchC2a5Apache1},
            {nameHash("c2a5w"), ActorKind::Apache, 0, false, kAchC2a5aApache2},
            {nameHash("c2a5x"), ActorKind::Apache, 0, false, kAchC2a5aApache2},
            {nameHash("c2a5a"), ActorKind::Apache, nameHash("apache"), false, kAchC2a5aApache3},
            // The Gargantuas: Surface Tension's, whom the airstrike kills,
            // and Interloper's.
            {nameHash("c2a5g"), ActorKind::Gargantua, nameHash("terror"), false, kAchC2a5gGarg},
            {nameHash("c4a1b"), ActorKind::Gargantua, 0, false, kAchLoperGarg},
        };

        /// What the player's kills count towards.
        struct CountRule
        {
            ActorKind kind;
            std::uint8_t counter;
        };
        inline constexpr CountRule kCountRules[] = {
            {ActorKind::Headcrab, kCountHeadcrabs},
            {ActorKind::BabyCrab, kCountHeadcrabs},
            {ActorKind::Slave, kCountSlaves},
            {ActorKind::Barnacle, kCountBarnacles},
            // The turret on the floor: monster_turret and
            // monster_miniturret are the ones on ceilings.
            {ActorKind::Sentry, kCountSentries},
        };

        /// The player is in a place: a secret, a hidden cache. A box in the
        /// map's units, which the player's own (32 wide, 72 high) has only
        /// to touch.
        struct BoxRule
        {
            std::uint32_t map;
            std::int16_t mins[3], maxs[3];
            std::uint8_t id;
        };
        inline constexpr BoxRule kBoxRules[] = {
            // Office Complex: Barney's room behind the door he opens; the
            // maintenance area's turret room; the grenades by the last
            // health charger.
            {nameHash("c1a2d"), {80, -2370, -580}, {290, -2200, -480}, kAchC1a2dBarney},
            {nameHash("c1a2a"), {1560, -800, 230}, {1760, -660, 340}, kAchC1a2aTurret},
            {nameHash("c1a2b"), {720, -230, 80}, {860, -30, 170}, kAchC1a2Grenades},
            // We've Got Hostiles: the chargers between the conveyors.
            {nameHash("c1a3a"), {-2296, -1848, -128}, {-2136, -1600, -24}, kAchC1a3aConveyorbase},
            // Blast Pit: the two chargers by the barnacle.
            {nameHash("c1a4i"), {-700, -1790, -800}, {-590, -1700, -680}, kAchC1a4iBarnacle},
            // On a Rail: behind the false wall by the lift; and the room under
            // the stairs (a crate, a vent) with RILLER on its wall.
            {nameHash("c2a2b2"), {1280, -1700, 200}, {1600, -1440, 300}, kAchC2a2b2Lift},
            {nameHash("c2a2c"), {-120, -1280, 900}, {30, -1030, 1000}, kAchRiller},
            // Apprehension: past the grate over the pipe.
            {nameHash("c2a3a"), {540, -440, 560}, {860, -270, 660}, kAchC2a3aPipe},
            // Forget About Freeman: behind the snark doorway.
            {nameHash("c3a1"), {-2040, -1420, 370}, {-1850, -1080, 480}, kAchC3a1Secret},
            // Lambda Core: the big lift's lowest stop.
            {nameHash("c3a2e"), {1696, 2848, 100}, {2016, 3168, 260}, kAchC3a2Entry},
            // Interloper: the foot of the pillar the way spirals round.
            {nameHash("c4a1e"), {390, -2130, -816}, {650, -1840, -700}, kAchC4a1eSpiral},
            // Nihilanth: the one scientist left alive, on his pillar.
            {nameHash("c4a3"), {-920, 1990, -2070}, {-620, 2200, -1950}, kAchNihilSci},
        };

        /// The player takes an item, in a map.
        struct PickRule
        {
            std::uint32_t map;
            ItemKind item;
            std::uint8_t id;
        };
        inline constexpr PickRule kPickRules[] = {
            {nameHash("c2a4f"), ItemKind::Gauss, kAchC2a4fGauss},
        };

        inline constexpr std::uint32_t kLiftMap = nameHash("c2a5d");
    } // namespace ach

    /// What the engine knows that the rules need.
    struct AchContext
    {
        /// In c2a5d: every tripmine the map laid is still on its wall.
        bool minesIntact = false;
    };

    /// The names `map`'s rules listen for; how many (at most `most`).
    inline int achievementsWatched(std::uint32_t map, std::uint32_t *out, int most)
    {
        int n = 0;
        for (const ach::FireRule &rule : ach::kFireRules)
        {
            if (rule.map == map && n < most)
                out[n++] = rule.name;
        }
        return n;
    }

    class AchievementTracker
    {
    public:
        AchievementData data;

        bool has(int id) const
        {
            return id >= 0 && id < kAchievementCount && (data.unlocked[id / 32] >> (id % 32) & 1u) != 0;
        }
        int earned() const
        {
            int n = 0;
            for (int i = 0; i < kAchievementCount; ++i)
                n += has(i) ? 1 : 0;
            return n;
        }
        /// How far along one that counts is (0 for the rest).
        int progress(int id) const
        {
            for (int c = 0; c < kAchCounterCount; ++c)
            {
                if (kCounterAchievement[c] == id)
                    return data.counts[c];
            }
            return 0;
        }

        /// Earn it. True if it is new: it is then waiting to be shown
        /// (next()), and the file is due to be written (unsaved).
        bool unlock(int id)
        {
            if (id < 0 || id >= kAchievementCount || has(id))
                return false;
            data.unlocked[id / 32] |= 1u << (id % 32);
            unsaved = true;
            if (freshCount_ < kFreshMost)
                fresh_[freshCount_++] = static_cast<std::uint8_t>(id);
            // Marathon: the Hazard Course, Uplink and the game, each to its
            // end, in whatever order and however far apart.
            if (has(kAchTrainingComplete) && has(kAchUplinkComplete) &&
                (has(kAchC5a1Complete) || has(kAchC5a1Failed)))
                unlock(kAchMarathon);
            return true;
        }
        /// The next one earned and not yet shown; -1: none.
        int next()
        {
            if (freshCount_ == 0)
                return -1;
            const int id = fresh_[0];
            --freshCount_;
            for (int i = 0; i < freshCount_; ++i)
                fresh_[i] = fresh_[i + 1];
            return id;
        }

        /// `id` (from next()) could not be shown yet: it is first again.
        void showLater(int id)
        {
            if (freshCount_ >= kFreshMost)
                --freshCount_;
            for (int i = freshCount_; i > 0; --i)
                fresh_[i] = fresh_[i - 1];
            fresh_[0] = static_cast<std::uint8_t>(id);
            ++freshCount_;
        }

        /// The way out of `left` was taken into `entered`.
        void travelled(std::uint32_t left, std::uint32_t entered)
        {
            for (const ach::ChapterEnd &end : ach::kChapterEnds)
            {
                if (end.left == left && end.entered == entered)
                    unlock(end.id);
            }
        }

        /// What happened in `map` this frame.
        void apply(std::uint32_t map, const AchEvents &events, const AchContext &context)
        {
            for (int i = 0; i < events.count; ++i)
            {
                const AchEvent &e = events.list[i];
                switch (e.type)
                {
                case AchEventType::Fired:
                    for (const ach::FireRule &rule : ach::kFireRules)
                    {
                        if (rule.map == map && rule.name == e.hash &&
                            (map != ach::kLiftMap || context.minesIntact))
                            unlock(rule.id);
                    }
                    break;
                case AchEventType::Killed:
                {
                    const bool byPlayer = (e.flags & kAchByPlayer) != 0;
                    for (const ach::KillRule &rule : ach::kKillRules)
                    {
                        if (static_cast<std::uint8_t>(rule.kind) == e.kind && (rule.map == 0 || rule.map == map) &&
                            (rule.name == 0 || rule.name == e.hash) && (byPlayer || !rule.byPlayer))
                            unlock(rule.id);
                    }
                    for (const ach::CountRule &rule : ach::kCountRules)
                    {
                        if (byPlayer && static_cast<std::uint8_t>(rule.kind) == e.kind)
                            count(rule.counter);
                    }
                    break;
                }
                case AchEventType::Broke:
                    count(kCountBroken);
                    break;
                case AchEventType::Soda:
                    unlock(kAchBeverages);
                    break;
                case AchEventType::Picked:
                    for (const ach::PickRule &rule : ach::kPickRules)
                    {
                        if (rule.map == map && static_cast<std::uint8_t>(rule.item) == e.kind)
                            unlock(rule.id);
                    }
                    break;
                }
            }
        }

        /// Where the player stands in `map` (their origin, the middle of
        /// them).
        void standing(std::uint32_t map, const fx origin[3])
        {
            static constexpr int kHalf[3] = {16, 16, 36};
            for (const ach::BoxRule &rule : ach::kBoxRules)
            {
                if (rule.map != map || has(rule.id))
                    continue;
                bool inside = true;
                for (int k = 0; k < 3 && inside; ++k)
                {
                    inside = origin[k] + fxFromInt(kHalf[k]) >= fxFromInt(rule.mins[k]) &&
                             origin[k] - fxFromInt(kHalf[k]) <= fxFromInt(rule.maxs[k]);
                }
                if (inside)
                    unlock(rule.id);
            }
        }

        /// True while a count has moved since the file was last written
        /// (unsaved, for an achievement earned, is more urgent).
        bool counted = false;
        bool unsaved = false;

    private:
        void count(int counter)
        {
            std::uint16_t &n = data.counts[counter];
            if (n < 0xFFFF)
                ++n;
            counted = true;
            if (n >= kAchievements[kCounterAchievement[counter]].goal)
                unlock(kCounterAchievement[counter]);
        }

        static constexpr int kFreshMost = 8;
        std::uint8_t fresh_[kFreshMost] = {};
        int freshCount_ = 0;
    };
} // namespace hldata
