#pragma once

// Sound: the file a cooked sound is in, the sounds every map has, and how
// the game says "this was heard, here".
//
// A cooked sound (.hsn, under snd/ on the card) is IMA-ADPCM exactly as the
// DS's sound hardware plays it: a four-byte header (the first sample and the
// step it starts at) and then four bits a sample. The hardware loops it too,
// from a point given in words (four bytes, eight samples).
//
// The game's shared code does not play anything. Where something happens
// that is heard -- a door starts, a shot is fired, a headcrab lands -- it
// puts a SoundEvent on a queue; the console empties the queue each frame
// into its sixteen channels, and the host tests read it to see that what
// should have been heard was.
//
// std-only: shared with the converter and the tests.

#include <cstdint>
#include <cstring>

#include "HlLevel.hpp"

namespace hldata
{
    inline constexpr char kSoundMagic[4] = {'H', 'L', 'S', 'N'};
    inline constexpr std::uint16_t kSoundVersion = 1;
    inline constexpr std::uint32_t kSoundNoLoop = 0xFFFFFFFFu;
    /// What sounds are cooked at, unless a map is short of room for them.
    inline constexpr int kSoundRate = 11025;

    struct SoundHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t rate;        ///< samples a second
        std::uint32_t dataBytes;   ///< the ADPCM, its four-byte header included; a multiple of 4
        std::uint32_t loopWord;    ///< where it loops from, in words of the data; kSoundNoLoop
        std::uint32_t samples;     ///< how long it is
        std::uint32_t sourceHash;  ///< of what it was made from, and how
    };
    static_assert(sizeof(SoundHeader) == 24);

    inline bool soundHeaderValid(const SoundHeader &h, std::uint32_t fileSize)
    {
        return std::memcmp(h.magic, kSoundMagic, 4) == 0 && h.version == kSoundVersion &&
               h.rate >= 4000 && h.rate <= 32768 && h.dataBytes >= 8 && (h.dataBytes & 3) == 0 &&
               h.dataBytes == fileSize - sizeof(SoundHeader) &&
               (h.loopWord == kSoundNoLoop || (h.loopWord >= 1 && h.loopWord < h.dataBytes / 4)) &&
               h.samples <= (h.dataBytes - 4) * 2;
    }

    // Music (.hsd, under music/ on the card, read off it as it plays): two
    // channels of ADPCM at half the DSi's high rate, as the ARM7 unpacks it
    // (HlStereoMusic.hpp), on a DS and a DSi alike. Blocks of
    // kMusicBlockWords words, each after four words that are both decoders'
    // states at its start (left sample, left step, right sample, right step)
    // -- so that a track can be taken up at any block.
    inline constexpr char kStereoMagic[4] = {'H', 'L', 'S', 'D'};
    inline constexpr int kStereoRate = 23802;      ///< 47,605 / 2
    inline constexpr int kMusicBlockWords = 1024;  ///< 2048 pairs: 86 ms
    struct StereoHeader
    {
        char magic[4];
        std::uint16_t version; ///< kSoundVersion
        std::uint16_t rate;
        std::uint32_t blocks;
        std::uint32_t pairs;   ///< samples a channel, in all
    };
    static_assert(sizeof(StereoHeader) == 16);

    /// The file a track of the game's music is in, under music/.
    inline void musicFileName(int track, char *out, std::size_t size)
    {
        // Two digits: tracks are numbered 2 to 28.
        const char name[] = {'t', 'r', 'a', 'c', 'k', static_cast<char>('0' + (track / 10) % 10),
                             static_cast<char>('0' + track % 10), '.', 'h', 's', 'd', '\0'};
        std::size_t i = 0;
        for (; i + 1 < size && name[i] != '\0'; ++i)
            out[i] = name[i];
        out[i] = '\0';
    }

    /// The sounds every map's Sounds section begins with, in this order (a
    /// map that has no use for one leaves its name empty).
    enum class CommonSound : std::uint16_t
    {
        Step1,
        Step2,
        Step3,
        Step4,
        Pain,
        CrowbarMiss,
        CrowbarHit,
        CrowbarFlesh,
        Pistol,
        Shotgun,
        Reload,
        ShotgunReload,
        Click,
        GunPickup,
        AmmoPickup,
        MedKit,
        Charge,
        GrenadeBounce,
        Explode,
        HeadcrabAlert,
        HeadcrabAttack,
        HeadcrabPain,
        HeadcrabDie,
        HeadcrabBite,
        ZombieAlert,
        ZombieAttack,
        ZombiePain,
        ClawStrike,
        ClawMiss,
        BarnacleAlert,
        BarnacleChew,
        BarnacleDie,
        SlaveAlert,
        SlaveZap,
        SlavePain,
        SlaveDie,
        SquidAlert,
        SquidSpit,
        SquidBite,
        SquidPain,
        SquidDie,
        SpitHit,
        HoundAlert,
        HoundAttack,
        HoundBlast,
        HoundPain,
        HoundDie,
        TurretAlert,
        TurretDeploy,
        TurretPing,
        SentryFire,   ///< a miniturret's and a sentry's shot
        TurretDie,
        TurretFire,   ///< the big turret's, and its barrels getting going and stopping
        TurretSpinUp,
        TurretSpinDown,
        BodySplat, ///< something coming apart
        Mp5Fire,   ///< the player's MP5, and its launcher
        Launcher,
        PythonFire, ///< the .357, and its reload
        PythonReload,
        MineActivate, ///< a tripmine's beam coming on
        GruntFire1,   ///< a soldier's gun, his reload, and what he says of being hurt and killed
        GruntFire2,
        GruntReload,
        GruntPain1,
        GruntPain2,
        GruntDie1,
        GruntDie2,
        Ricochet1, ///< a shot glancing off a wall, two ways
        Ricochet2,
        FallPain,  ///< landing too hard
        Shell,     ///< a spent casing on the floor
        BulletFlesh, ///< a round going into something that bleeds
        Ladder1,   ///< the player's hands and feet on a ladder's rungs
        Ladder2,
        Ladder3,
        Ladder4,
        Wade1,     ///< ... through water up to the waist or over it, and going in and out of it
        Wade2,
        Wade3,
        Wade4,
        Slosh1,    ///< ... through water that is only round the feet
        Slosh2,
        Slosh3,
        Slosh4,
        JumpLand,  ///< coming down hard, but not hard enough to be hurt
        ScientistPain, ///< what a scientist and a guard say of being hurt and killed
        ScientistDie,
        GuardPain,
        GuardDie,
        Metal1,    ///< the player's step on metal, dirt, a vent, a grate, tiles (HlMaterial.hpp)
        Metal2,
        Metal3,
        Metal4,
        Dirt1,
        Dirt2,
        Dirt3,
        Dirt4,
        Duct1,
        Duct2,
        Duct3,
        Duct4,
        Grate1,
        Grate2,
        Grate3,
        Grate4,
        Tile1,
        Tile2,
        Tile3,
        Tile4,
        NpcStep1,  ///< somebody else's footsteps (their models say when: HlModel.hpp)
        NpcStep2,
        NpcStep3,
        NpcStep4,
        CrowbarHit2,   ///< the crowbar's other clang, and its other two thuds
        CrowbarFlesh2,
        CrowbarFlesh3,
        WoodHit1,      ///< what wood, and glass, sound like struck
        WoodHit2,
        WoodHit3,
        GlassHit1,
        GlassHit2,
        GlassHit3,
        // The guns' other sounds, as the game has them (where a map has the
        // gun to be held): the shotgun's both barrels, its pump and its
        // other shell going in; the MP5's other shot and other launch; the
        // .357's other shot; a grenade's other two knocks.
        ShotgunDouble,
        ShotgunPump,
        ShotgunReload3,
        Mp5Fire1,
        Launcher2,
        PythonFire2,
        GrenadeBounce2,
        GrenadeBounce3,
        PushBox1, ///< a box pushed along a floor (where a map has one to push)
        PushBox2,
        PushBox3,
        // The weapons that came after the first six (where a map has them):
        // a map converted before these were has none of them (and its
        // sounds after PushBox3 are its own: kCommonSoundsBefore30).
        XbowFire,     ///< the crossbow, its bolt into a wall and into flesh, its reload
        XbowHit,
        XbowHitBody,
        XbowReload,
        MineDeploy,   ///< a tripmine put on a wall, and charging
        MineCharge,
        SatchelBounce, ///< a satchel charge knocking along the floor
        SnarkDeploy,  ///< a snark let go, hunting, and bursting
        SnarkHunt1,
        SnarkHunt2,
        SnarkHunt3,
        SnarkBlast,
        // ... and the last four (from level version 31: kCommonSoundsBefore31).
        RocketFire,   ///< the RPG, and its rocket's flight
        RocketFly,
        GaussFire,    ///< the gauss: a shot, its charging hum, the sparks of a discharge
        GaussCharge,
        Electro4,
        Electro5,
        Electro6,
        EgonWindup,   ///< the egon: starting, running, stopping
        EgonRun,
        EgonOff,
        HornetFire1,  ///< the hornetgun, and its hornets buzzing and stinging
        HornetFire2,
        HornetFire3,
        HornetBuzz1,
        HornetBuzz2,
        HornetBuzz3,
        HornetHit1,
        HornetHit2,
        HornetHit3,
        // ... and the rest of what the people cry out, and a guard's pistol
        // (from level version 32: kCommonSoundsBefore32).
        ScientistPain2,
        ScientistPain3,
        ScientistPain4,
        ScientistPain5,
        GuardPain2,
        GuardPain3,
        GuardDie2,
        GuardDie3,
        GuardShot,
        // ... and round 3's monsters (from level version 33:
        // kCommonSoundsBefore33). (The alien grunt's gun is the hornetgun's,
        // above; the snark's are the snark's.)
        RoachWalk,
        RoachSmash,
        RoachDie,
        LeechBite,
        LeechAlert,
        AgruntIdle1,
        AgruntIdle2,
        AgruntAlert1,
        AgruntAlert2,
        AgruntAttack,
        AgruntPain1,
        AgruntPain2,
        AgruntDie1,
        AgruntDie2,
        ControllerIdle,
        ControllerAlert,
        ControllerAttack,
        ControllerPain1,
        ControllerPain2,
        ControllerDie,
        // ... and round 4's (from level version 34: kCommonSoundsBefore34),
        // each kind's together.
        OspreyRotor,
        MortarHit,
        GargIdle,
        GargAlert,
        GargPain,
        GargAttack,
        GargStep,
        GargBreathe,
        GargStomp,
        GargFlameRun,
        GargFlameOff,
        IckyIdle,
        IckyAlert,
        IckyAttack,
        IckyBite,
        IckyPain,
        IckyDie,
        AssassinShot1,
        AssassinShot2,
        // ... and round 5's (from level version 35: kCommonSoundsBefore35).
        TentacleAlert,
        TentacleSing,
        TentacleStrike,
        TentacleRoar,
        TentacleSearch,
        TentacleMove,
        Flies,
        Squirm,
        ApacheRotor,
        MommaAlert,
        MommaPain,
        MommaAttack,
        MommaBirth,
        MommaSack,
        MommaDie,
        MommaStep,
        BeamStart,
        NihilAttack,
        NihilBall,
        NihilRecharge,
        NihilLaugh,
        NihilPain,
        NihilDie,
        BoidIdle,
        BoidAlert,
        // ... and a shotgun shell's ring (from version 37: kCommonSoundsBefore37).
        ShotShell,
        // ... and a body falling to the floor (from version 43: kCommonSoundsBefore43).
        BodyDrop3,
        BodyDrop4,
        Count
    };
    /// How many of these a map of level version 29 or before has.
    inline constexpr std::uint16_t kCommonSoundsBefore30 = static_cast<std::uint16_t>(CommonSound::XbowFire);

    /// Each gun's own sounds, by the model it is held as: the converter keeps
    /// them for a map where the gun can be held (EntityCook), and leaves them
    /// out of what a console sent the map is given where it cannot (Pack).
    /// `item` (and `other`, its ammunition): what lying about on a map gives
    /// one of the guns that came after the first six; None for those six,
    /// which a map has to be held or not.
    struct GunSounds
    {
        const char *viewModel;
        ItemKind item;
        ItemKind other;
        CommonSound first;
        CommonSound last;
    };
    inline constexpr GunSounds kGunSounds[] = {
        {"v_9mmar", ItemKind::None, ItemKind::None, CommonSound::Mp5Fire, CommonSound::Launcher},
        {"v_9mmar", ItemKind::None, ItemKind::None, CommonSound::Mp5Fire1, CommonSound::Launcher2},
        {"v_357", ItemKind::None, ItemKind::None, CommonSound::PythonFire, CommonSound::PythonReload},
        {"v_357", ItemKind::None, ItemKind::None, CommonSound::PythonFire2, CommonSound::PythonFire2},
        {"v_shotgun", ItemKind::None, ItemKind::None, CommonSound::ShotgunDouble, CommonSound::ShotgunReload3},
        {"v_grenade", ItemKind::None, ItemKind::None, CommonSound::GrenadeBounce2, CommonSound::GrenadeBounce3},
        {"v_crossbow", ItemKind::Crossbow, ItemKind::Bolts, CommonSound::XbowFire, CommonSound::XbowReload},
        {"v_tripmine", ItemKind::Tripmine, ItemKind::None, CommonSound::MineDeploy, CommonSound::MineCharge},
        {"v_tripmine", ItemKind::Tripmine, ItemKind::None, CommonSound::MineActivate, CommonSound::MineActivate},
        {"v_satchel", ItemKind::Satchel, ItemKind::None, CommonSound::SatchelBounce, CommonSound::SatchelBounce},
        {"v_squeak", ItemKind::Snark, ItemKind::None, CommonSound::SnarkDeploy, CommonSound::SnarkBlast},
        {"v_rpg", ItemKind::Rpg, ItemKind::RpgClip, CommonSound::RocketFire, CommonSound::RocketFly},
        {"v_gauss", ItemKind::Gauss, ItemKind::GaussClip, CommonSound::GaussFire, CommonSound::Electro6},
        {"v_egon", ItemKind::Egon, ItemKind::None, CommonSound::EgonWindup, CommonSound::EgonOff},
        {"v_hgun", ItemKind::Hornetgun, ItemKind::None, CommonSound::HornetFire1, CommonSound::HornetHit3},
    };

    /// The model a weapon lying about is held as, once taken up (nothing for
    /// what is only ammunition).
    inline const char *itemViewModel(ItemKind item)
    {
        switch (item)
        {
        case ItemKind::Crowbar: return "v_crowbar";
        case ItemKind::Glock: return "v_9mmhandgun";
        case ItemKind::Shotgun: return "v_shotgun";
        case ItemKind::Grenades: return "v_grenade";
        case ItemKind::Mp5: return "v_9mmar";
        case ItemKind::Python: return "v_357";
        case ItemKind::Crossbow: return "v_crossbow";
        case ItemKind::Satchel: return "v_satchel";
        case ItemKind::Tripmine: return "v_tripmine";
        case ItemKind::Snark: return "v_squeak";
        case ItemKind::Rpg: return "v_rpg";
        case ItemKind::Gauss: return "v_gauss";
        case ItemKind::Egon: return "v_egon";
        case ItemKind::Hornetgun: return "v_hgun";
        default: return nullptr;
        }
    }
    /// ... and of version 30.
    inline constexpr std::uint16_t kCommonSoundsBefore31 = static_cast<std::uint16_t>(CommonSound::RocketFire);
    /// ... and of version 31.
    inline constexpr std::uint16_t kCommonSoundsBefore32 = static_cast<std::uint16_t>(CommonSound::ScientistPain2);
    /// ... and of version 32.
    inline constexpr std::uint16_t kCommonSoundsBefore33 = static_cast<std::uint16_t>(CommonSound::RoachWalk);
    /// ... and of version 33.
    inline constexpr std::uint16_t kCommonSoundsBefore34 = static_cast<std::uint16_t>(CommonSound::OspreyRotor);
    /// ... and of version 34.
    inline constexpr std::uint16_t kCommonSoundsBefore35 = static_cast<std::uint16_t>(CommonSound::TentacleAlert);
    /// ... and of versions 35 and 36.
    inline constexpr std::uint16_t kCommonSoundsBefore37 = static_cast<std::uint16_t>(CommonSound::ShotShell);
    /// ... and of versions 37 to 42.
    inline constexpr std::uint16_t kCommonSoundsBefore43 = static_cast<std::uint16_t>(CommonSound::BodyDrop3);
    /// How many of them a map of level version `version` has (its sounds
    /// after those are its own).
    inline constexpr std::uint16_t commonsOf(std::uint16_t version)
    {
        return version < 30   ? kCommonSoundsBefore30
               : version < 31 ? kCommonSoundsBefore31
               : version < 32 ? kCommonSoundsBefore32
               : version < 33 ? kCommonSoundsBefore33
               : version < 34 ? kCommonSoundsBefore34
               : version < 35 ? kCommonSoundsBefore35
               : version < 37 ? kCommonSoundsBefore37
               : version < 43 ? kCommonSoundsBefore43
                              : static_cast<std::uint16_t>(CommonSound::Count);
    }
    inline constexpr std::uint16_t kNoSound = 0xFFFF;

    /// Where each of them is in the game's sound folder.
    inline const char *commonSoundPath(CommonSound sound)
    {
        static const char *const kPaths[] = {
            "player/pl_step1.wav",    "player/pl_step2.wav",     "player/pl_step3.wav",
            "player/pl_step4.wav",    "player/pl_pain2.wav",     "weapons/cbar_miss1.wav",
            "weapons/cbar_hit1.wav",  "weapons/cbar_hitbod1.wav", "weapons/pl_gun3.wav",
            "weapons/sbarrel1.wav",   "items/9mmclip1.wav",      "weapons/reload1.wav",
            "weapons/357_cock1.wav",  "items/gunpickup2.wav",    "items/9mmclip1.wav",
            "items/smallmedkit1.wav", "items/medshot4.wav",
            "weapons/grenade_hit1.wav", "weapons/explode3.wav",  "headcrab/hc_alert1.wav",
            "headcrab/hc_attack1.wav", "headcrab/hc_pain1.wav",  "headcrab/hc_die1.wav",
            "headcrab/hc_headbite.wav", "zombie/zo_alert10.wav", "zombie/zo_attack1.wav",
            "zombie/zo_pain1.wav",    "zombie/claw_strike1.wav", "zombie/claw_miss1.wav",
            "barnacle/bcl_alert2.wav", "barnacle/bcl_chew1.wav", "barnacle/bcl_die1.wav",
            "aslave/slv_alert1.wav",  "weapons/electro4.wav",    "aslave/slv_pain1.wav",
            "aslave/slv_die1.wav",    "bullchicken/bc_attackgrowl.wav", "bullchicken/bc_attack1.wav",
            "bullchicken/bc_bite1.wav", "bullchicken/bc_pain1.wav", "bullchicken/bc_die1.wav",
            "bullchicken/bc_spithit1.wav", "houndeye/he_alert1.wav", "houndeye/he_attack1.wav",
            "houndeye/he_blast1.wav", "houndeye/he_pain1.wav",   "houndeye/he_die1.wav",
            "turret/tu_alert.wav",    "turret/tu_deploy.wav",    "turret/tu_ping.wav",
            "weapons/hks1.wav",       "turret/tu_die.wav",       "turret/tu_fire1.wav",
            "turret/tu_spinup.wav",   "turret/tu_spindown.wav",  "common/bodysplat.wav",
            "weapons/hks2.wav",       "weapons/glauncher.wav",   "weapons/357_shot1.wav",
            "weapons/357_reload1.wav", "weapons/mine_activate.wav", "hgrunt/gr_mgun1.wav",
            "hgrunt/gr_mgun2.wav",    "hgrunt/gr_reload1.wav",   "hgrunt/gr_pain1.wav",
            "hgrunt/gr_pain3.wav",    "hgrunt/gr_die1.wav",      "hgrunt/gr_die2.wav",
            "weapons/ric1.wav",       "weapons/ric4.wav",        "player/pl_fallpain3.wav",
            "player/pl_shell1.wav",   "weapons/bullet_hit1.wav", "player/pl_ladder1.wav",
            "player/pl_ladder2.wav",  "player/pl_ladder3.wav",   "player/pl_ladder4.wav",
            "player/pl_wade1.wav",    "player/pl_wade2.wav",     "player/pl_wade3.wav",
            "player/pl_wade4.wav",    "player/pl_slosh1.wav",    "player/pl_slosh2.wav",
            "player/pl_slosh3.wav",   "player/pl_slosh4.wav",    "player/pl_jumpland2.wav",
            "scientist/sci_pain1.wav", "scientist/sci_die1.wav", "barney/ba_pain1.wav",
            "barney/ba_die1.wav",     "player/pl_metal1.wav",    "player/pl_metal2.wav",
            "player/pl_metal3.wav",   "player/pl_metal4.wav",    "player/pl_dirt1.wav",
            "player/pl_dirt2.wav",    "player/pl_dirt3.wav",     "player/pl_dirt4.wav",
            "player/pl_duct1.wav",    "player/pl_duct2.wav",     "player/pl_duct3.wav",
            "player/pl_duct4.wav",    "player/pl_grate1.wav",    "player/pl_grate2.wav",
            "player/pl_grate3.wav",   "player/pl_grate4.wav",    "player/pl_tile1.wav",
            "player/pl_tile2.wav",    "player/pl_tile3.wav",     "player/pl_tile4.wav",
            "common/npc_step1.wav",   "common/npc_step2.wav",    "common/npc_step3.wav",
            "common/npc_step4.wav",   "weapons/cbar_hit2.wav",   "weapons/cbar_hitbod2.wav",
            "weapons/cbar_hitbod3.wav", "debris/wood1.wav",      "debris/wood2.wav",
            "debris/wood3.wav",       "debris/glass1.wav",       "debris/glass2.wav",
            "debris/glass3.wav",      "weapons/dbarrel1.wav",    "weapons/scock1.wav",
            "weapons/reload3.wav",    "weapons/hks1.wav",        "weapons/glauncher2.wav",
            "weapons/357_shot2.wav",  "weapons/grenade_hit2.wav", "weapons/grenade_hit3.wav",
            "debris/pushbox1.wav",    "debris/pushbox2.wav",     "debris/pushbox3.wav",
            "weapons/xbow_fire1.wav", "weapons/xbow_hit1.wav",   "weapons/xbow_hitbod1.wav",
            "weapons/xbow_reload1.wav", "weapons/mine_deploy.wav", "weapons/mine_charge.wav",
            "weapons/g_bounce1.wav",  "squeek/sqk_deploy1.wav",  "squeek/sqk_hunt1.wav",
            "squeek/sqk_hunt2.wav",   "squeek/sqk_hunt3.wav",    "squeek/sqk_blast1.wav",
            "weapons/rocketfire1.wav", "weapons/rocket1.wav",    "weapons/gauss2.wav",
            "ambience/pulsemachine.wav", "weapons/electro4.wav", "weapons/electro5.wav",
            "weapons/electro6.wav",   "weapons/egon_windup2.wav", "weapons/egon_run3.wav",
            "weapons/egon_off1.wav",  "agrunt/ag_fire1.wav",     "agrunt/ag_fire2.wav",
            "agrunt/ag_fire3.wav",    "hornet/ag_buzz1.wav",     "hornet/ag_buzz2.wav",
            "hornet/ag_buzz3.wav",    "hornet/ag_hornethit1.wav", "hornet/ag_hornethit2.wav",
            "hornet/ag_hornethit3.wav", "scientist/sci_pain2.wav", "scientist/sci_pain3.wav",
            "scientist/sci_pain4.wav",  "scientist/sci_pain5.wav", "barney/ba_pain2.wav",
            "barney/ba_pain3.wav",      "barney/ba_die2.wav",      "barney/ba_die3.wav",
            "barney/ba_attack2.wav",
            "roach/rch_walk.wav",       "roach/rch_smash.wav",     "roach/rch_die.wav",
            "leech/leech_bite1.wav",    "leech/leech_alert1.wav",  "agrunt/ag_idle1.wav",
            "agrunt/ag_idle3.wav",      "agrunt/ag_alert1.wav",    "agrunt/ag_alert4.wav",
            "agrunt/ag_attack1.wav",    "agrunt/ag_pain1.wav",     "agrunt/ag_pain3.wav",
            "agrunt/ag_die1.wav",       "agrunt/ag_die4.wav",      "controller/con_idle1.wav",
            "controller/con_alert1.wav", "controller/con_attack1.wav", "controller/con_pain1.wav",
            "controller/con_pain2.wav", "controller/con_die1.wav",
            "apache/ap_rotor4.wav",     "weapons/mortarhit.wav",   "garg/gar_idle1.wav",
            "garg/gar_alert1.wav",      "garg/gar_pain1.wav",      "garg/gar_attack1.wav",
            "garg/gar_step1.wav",       "garg/gar_breathe1.wav",   "garg/gar_stomp1.wav",
            "garg/gar_flamerun1.wav",   "garg/gar_flameoff1.wav",  "ichy/ichy_idle1.wav",
            "ichy/ichy_alert2.wav",     "ichy/ichy_attack1.wav",   "ichy/ichy_bite1.wav",
            "ichy/ichy_pain2.wav",      "ichy/ichy_die2.wav",      "weapons/pl_gun1.wav",
            "weapons/pl_gun2.wav",      "tentacle/te_alert1.wav",  "tentacle/te_sing1.wav",
            "tentacle/te_strike1.wav",  "tentacle/te_roar1.wav",   "tentacle/te_search1.wav",
            "tentacle/te_move1.wav",    "ambience/flies.wav",      "ambience/squirm2.wav",
            "apache/ap_rotor2.wav",     "gonarch/gon_alert1.wav",  "gonarch/gon_pain2.wav",
            "gonarch/gon_attack1.wav",  "gonarch/gon_birth1.wav",  "gonarch/gon_sack1.wav",
            "gonarch/gon_die1.wav",     "gonarch/gon_step1.wav",   "debris/beamstart7.wav",
            "x/x_attack1.wav",          "x/x_ballattack1.wav",     "x/x_recharge1.wav",
            "x/x_laugh1.wav",           "x/x_pain1.wav",           "x/x_die1.wav",
            "boid/boid_idle1.wav",      "boid/boid_alert1.wav",
            "weapons/sshell1.wav",      "common/bodydrop3.wav",    "common/bodydrop4.wav"};
        static_assert(sizeof(kPaths) / sizeof(kPaths[0]) == static_cast<std::size_t>(CommonSound::Count));
        return kPaths[static_cast<int>(sound)];
    }

    // How far a sound carries, as the game has it: full volume at the
    // source, nothing at the radius, a straight line between.
    inline constexpr int kRadiusEverywhere = 0; ///< as loud wherever the listener is
    inline constexpr int kRadiusSmall = 500;
    inline constexpr int kRadiusMedium = 800;
    inline constexpr int kRadiusLarge = 1250;

    /// How loud (0..127) something of loudness `volume` (0..127) is, heard
    /// `distance` whole units away, if it carries `radius` units.
    inline int soundGain(int volume, int distance, int radius)
    {
        if (radius <= 0)
            return volume;
        if (distance >= radius)
            return 0;
        return (volume * (radius - distance)) / radius;
    }

    /// Which of a level's sounds are kept in memory while it is played:
    /// `kept[i]` for each of `soundCount`. The common ones, and whatever a
    /// logic record plays. The rest are words of spoken lines, which are
    /// long and said once: those are read off the card as they are spoken.
    inline void soundsKept(const LogicRecord *logic, std::uint32_t logicCount,
                           const LogicSound *logicSounds, std::uint32_t logicSoundCount,
                           std::uint32_t soundCount, bool *kept, const ModelSound *modelSounds = nullptr,
                           std::uint32_t modelSoundCount = 0, const SoundRef *sounds = nullptr)
    {
        for (std::uint32_t i = 0; i < soundCount; ++i)
            kept[i] = false;
        for (std::uint32_t i = 0; i < soundCount && i < static_cast<std::uint32_t>(CommonSound::Count); ++i)
            kept[i] = true;
        for (std::uint32_t i = 0; i < logicCount; ++i)
        {
            if (logic[i].kind == static_cast<std::uint8_t>(LogicKind::Ambient) &&
                logic[i].extra[3] == 0 && logic[i].extra[0] < soundCount)
                kept[logic[i].extra[0]] = true;
        }
        // (What the map's models sound like as they move.)
        for (std::uint32_t i = 0; modelSounds != nullptr && i < modelSoundCount; ++i)
        {
            if (modelSounds[i].sound < soundCount)
                kept[modelSounds[i].sound] = true;
        }
        for (std::uint32_t i = 0; i < logicSoundCount; ++i)
        {
            if (logicSounds[i].move < soundCount)
                kept[logicSounds[i].move] = true;
            if (logicSounds[i].stop < soundCount)
                kept[logicSounds[i].stop] = true;
            if (logicSounds[i].locked < soundCount)
                kept[logicSounds[i].locked] = true;
            if (logicSounds[i].unlocked < soundCount)
                kept[logicSounds[i].unlocked] = true;
        }
        // (Not a weapon's the map has not: read when it is first played.)
        for (std::uint32_t i = 0; sounds != nullptr && i < soundCount; ++i)
        {
            if (soundLazy(sounds[i]))
                kept[i] = false;
        }
    }

    /// What kind of thing a sound is: each kind has its own share of the
    /// voices, its own place in the order of who gives way to whom, and its
    /// own volume (HlMixer.hpp). The music is beside these.
    enum class SoundLayer : std::uint8_t
    {
        Player,   ///< the player's own: their gun, their pain, what they pick up
        World,    ///< monsters, doors, blasts: whatever has a place
        Voice,    ///< spoken lines
        Ambience, ///< the loops of a place
        Ui,       ///< the menu
        Count
    };

    struct SoundEvent
    {
        std::uint16_t sound = kNoSound; ///< into the level's Sounds; or a sentence, with kSoundSentence
        std::uint8_t volume = 127;
        std::uint8_t flags = 0;         ///< kSound*
        std::int16_t radius = kRadiusLarge;
        fx pos[3] = {0, 0, 0};
        SoundLayer layer = SoundLayer::World;
        std::uint8_t pitch = 100; ///< percent of the speed it was recorded at
        std::uint16_t tag = 0;    ///< with kSoundLoopOn / kSoundLoopOff: whose loop it is
    };
    inline constexpr std::uint8_t kSoundHere = 1;     ///< at the listener: no place, no fading
    inline constexpr std::uint8_t kSoundSentence = 2; ///< `sound` is an index into Sentences
    /// What something makes for as long as it moves: goes round (if the
    /// sound is one that does) until kSoundLoopOff is heard with its tag.
    inline constexpr std::uint8_t kSoundLoopOn = 4;
    inline constexpr std::uint8_t kSoundLoopOff = 8; ///< `sound` is not looked at
    /// A loop's tag is its logic record's; one an actor makes as it goes
    /// (an osprey's rotors) is this and the actor's number. Heard again while
    /// it goes round, a loop is moved to where it is now, not started again.
    inline constexpr std::uint16_t kSoundTagActor = 0x8000;

    inline constexpr int kMaxSoundEvents = 24;

    /// What has been heard since the queue was last emptied. More than it
    /// holds in one tick are dropped: nobody hears the twenty-fifth thing.
    /// But a loop's start or stop is never what is dropped while there is
    /// a sound heard once to give up for it: a lost stop goes round for ever.
    struct SoundQueue
    {
        SoundEvent events[kMaxSoundEvents];
        int count = 0;
        /// How many of the CommonSounds the map has (an older map has fewer:
        /// kCommonSoundsBefore30); one it has not is not heard.
        std::uint16_t commons = static_cast<std::uint16_t>(CommonSound::Count);
        /// How many have been lost to a full queue since the map began: the
        /// one too many, or the one given up for a loop's (the readout).
        std::uint32_t dropped = 0;

        void add(std::uint16_t sound, const fx *pos, int volume, int radius, std::uint8_t flags = 0,
                 int pitch = 100, std::uint16_t tag = 0)
        {
            if (sound == kNoSound && !(flags & kSoundLoopOff))
                return;
            if (count >= kMaxSoundEvents)
            {
                ++dropped;
                if (!makeRoom(flags))
                    return;
            }
            SoundEvent &event = events[count++];
            event.sound = sound;
            event.volume = static_cast<std::uint8_t>(volume < 0 ? 0 : (volume > 127 ? 127 : volume));
            event.flags = pos == nullptr ? static_cast<std::uint8_t>(flags | kSoundHere) : flags;
            event.radius = static_cast<std::int16_t>(radius);
            for (int i = 0; i < 3; ++i)
                event.pos[i] = pos != nullptr ? pos[i] : 0;
            // (0: not said, which is as it was recorded.)
            event.pitch = static_cast<std::uint8_t>(pitch <= 0 ? 100 : (pitch > 255 ? 255 : pitch));
            event.tag = tag;
            // What is spoken is a voice; what is heard at the listener is
            // the player's own; the rest is the world's.
            event.layer = (flags & kSoundSentence) ? SoundLayer::Voice
                                                   : (pos == nullptr ? SoundLayer::Player : SoundLayer::World);
        }
        void add(CommonSound sound, const fx *pos, int volume = 127, int radius = kRadiusLarge, int pitch = 100)
        {
            if (static_cast<std::uint16_t>(sound) >= commons)
                return;
            add(static_cast<std::uint16_t>(sound), pos, volume, radius, 0, pitch);
        }
        /// Full, and something with `flags` to be put in: the latest sound
        /// heard once is given up for a loop's start or stop -- or, for a
        /// stop, the latest start if there is nothing else. What comes after
        /// it moves up one, so a stop still comes after its own start.
        bool makeRoom(std::uint8_t flags)
        {
            if (!(flags & (kSoundLoopOn | kSoundLoopOff)))
                return false;
            constexpr std::uint8_t kKept = kSoundLoopOn | kSoundLoopOff | kSoundSentence;
            int gone = count - 1;
            while (gone >= 0 && (events[gone].flags & kKept))
                --gone;
            if (gone < 0 && (flags & kSoundLoopOff))
            {
                gone = count - 1;
                while (gone >= 0 && !(events[gone].flags & kSoundLoopOn))
                    --gone;
            }
            if (gone < 0)
                return false;
            for (int i = gone; i + 1 < count; ++i)
                events[i] = events[i + 1];
            --count;
            return true;
        }
        bool heard(std::uint16_t sound) const
        {
            for (int i = 0; i < count; ++i)
            {
                if (events[i].sound == sound && !(events[i].flags & kSoundSentence))
                    return true;
            }
            return false;
        }
        bool heard(CommonSound sound) const { return heard(static_cast<std::uint16_t>(sound)); }
    };
} // namespace hldata
