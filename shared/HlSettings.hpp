#pragma once

#include "HlRumble.hpp"
#include "HlCharacters.hpp"

// The player's settings: <card>/_nds/hl_nds/data/settings.dat
//
// Written by the game, never by the converter. A yas archive behind the same
// kind of fixed header as the manifest (HlManifest.hpp), for the same reason:
// yas reports a damaged archive by throwing, the ARM9 has no exceptions, so
// everything that can be checked is checked before yas runs. A file that does
// not check out is not an error -- it is a first boot, and the defaults apply.

#include <cstdint>
#include <cstring>
#include <vector>

#include "HlYas.hpp"

#include "HlLevel.hpp"

#include "HlControls.hpp"
#include "HlFog.hpp"
#include "HlLineEdit.hpp"
#include "HlMixer.hpp"

namespace hldata
{
    /// A name with its end (a match's names are of the same room:
    /// HlNet.hpp's kNetTextBytes), and a map's.
    inline constexpr std::size_t kProfileNameBytes = 12;
    inline constexpr std::size_t kHostMapBytes = 16;
    /// The kinds of match: every one for themselves, two teams, and the
    /// story together (which is not built yet).
    inline constexpr std::uint8_t kModeDeathmatch = 0, kModeTeamplay = 1, kModeCoop = 2, kModeChoices = 3;
    inline constexpr std::uint8_t kPlayersLeast = 2, kPlayersMost = 8;
    /// The game's own spray colours (its cl_logocolor): orange, yellow,
    /// blue, light blue, green, red, brown, light grey, dark grey.
    inline constexpr std::uint8_t kSprayColours = 9;
    inline constexpr std::uint8_t kSprayColour[kSprayColours][3] = {
        {255, 120, 24}, {225, 180, 24}, {0, 60, 255}, {0, 167, 255}, {0, 167, 0},
        {255, 43, 0},   {123, 73, 0},   {100, 100, 100}, {36, 36, 36}};

    inline constexpr std::uint8_t kChatLeast = 5, kChatMost = 30, kChatDefault = 10, kVoteDefault = 10, kVoteMost = 150;
    inline constexpr int kRotationMost = 16;
    /// How the next map of a game between consoles is chosen.
    inline constexpr std::uint8_t kNextHost = 0, kNextRotation = 1, kNextVote = 2, kNextWinner = 3, kNextTurns = 4,
                                  kNextChoices = 5;

    /// What a vote's time may be set to.
    inline constexpr std::uint8_t kVoteSteps[] = {5, 10, 15, 20, 30, 45, 60, 90, 120, 150};
    /// How long a player may do nothing in a match hosted before the host
    /// sends them away, in minutes (0: never).
    inline constexpr std::uint8_t kIdleSteps[] = {0, 2, 5, 10};
    inline constexpr bool idleStep(std::uint8_t minutes)
    {
        for (const std::uint8_t step : kIdleSteps)
            if (step == minutes)
                return true;
        return false;
    }

    /// The top screen's shape: as the console shows it (a 3DS with
    /// TWPatch's wide TwlBg stretches it to 16:10), 4:3, or 16:10.
    inline constexpr std::uint8_t kScreenAuto = 0, kScreenStandard = 1, kScreenWide = 2, kScreenChoices = 3;

    struct SettingsData
    {
        /// Deprecated, can be reused for something else later
        std::uint8_t renderMode = 0;
        /// Before version 13: the DSi's DSP on (now roomEffects' Enhanced).
        /// Still written, for a build before to read.
        std::uint8_t dspAudio = 0;
        /// How loud each layer of the sound is, 0 (off) to 10: the music,
        /// the effects (the player's and the world's), the voices, and the
        /// ambience of a place. (A file has always had them: these are a
        /// new one's. Before, 8, 10, 10, 8.)
        std::uint8_t volume[4] = {6, 10, 10, 7};
        /// The debug readout along the top of the touch screen.
        std::uint8_t debugReadout = 0;
        /// The stylus is in the left hand: the touch screen's buttons are
        /// on the other side (HlTouchLayout.hpp).
        std::uint8_t leftHanded = 0;
        /// The view bobs with a walk and leans into a sidestep, as the
        /// game's does. Off unless asked for: on a screen this small, held
        /// this close, it is more than some can look at.
        std::uint8_t viewBob = 0;
        /// The fog, and how much detail is given up for speed (HlFog.hpp).
        GraphicsSettings graphics;
        /// The layout of the buttons (kControls*), and which of the pad and
        /// the stylus turns and which moves (HlControls.hpp).
        std::uint8_t controlScheme = kControlsModern;
        std::uint8_t padTurns = 0;
        std::uint8_t padLooks = 0;
        /// How far the stylus turns the view, across and up and down (10:
        /// a pixel is 1/512 of a turn, 1 to 20), and whether up is down.
        std::uint8_t aimSpeed = kAimSpeedDefault;
        std::uint8_t pitchSpeed = kAimSpeedDefault;
        std::uint8_t invertPitch = 0;
        /// The crouch button toggles (else it is held).
        std::uint8_t duckToggle = 0;
        /// The walk toggles (else it is held), and the jump button walks
        /// (HlControls.hpp).
        std::uint8_t walkToggle = 0;
        std::uint8_t jumpWalks = 0;
        /// Two taps on the aim pad jump (HlControls.hpp's tapJumps: always,
        /// where nothing else would).
        std::uint8_t tapJump = 1;
        /// A quick tap on the aim pad is USE (the modern layout). Off unless
        /// asked for: a touch to aim was too often a USE.
        std::uint8_t tapUse = 0;
        /// How far the music and the ambience are let down while someone
        /// speaks (kFocus*, HlMixer.hpp).
        std::uint8_t dialogueFocus = kFocusLight;
        /// The map's name left off the loading screen, out of a match
        /// (version 23). On unless asked off.
        std::uint8_t hideMapName = 1;
        /// The COMMAND line's cheats let through (version 21). Off: refused.
        std::uint8_t cheats = 0;
        /// Where the touch keyboard's letters are (kKeyboard*, HlLineEdit.hpp;
        /// version 21).
        std::uint8_t keyboardLayout = kKeyboardQwerty;
        /// How strict the stylus is about light touches (0..10), and what a
        /// firm press on the aim pad does (kFirm*): HlControls.hpp. Only
        /// where the console can say how hard it is pressed.
        std::uint8_t firmness = 4;
        std::uint8_t firmPress = kFirmOff;
        /// The CUSTOM layout: each action's buttons (kButton*).
        std::uint16_t keys[kActionCount] = {};
        /// The guns bend their aim towards a monster near it, on EASY and
        /// MEDIUM (the game's sv_aim: HlAutoaim.hpp). Off unless asked for.
        std::uint8_t autoaim = 0;
        /// The game writes its own save at a map's save points and on
        /// arriving in a map. On unless asked off.
        std::uint8_t autosave = 1;
        /// An achievement earned is shown on the top screen as it is
        /// (Achievements.cpp). On unless asked off: off, it is still earned.
        std::uint8_t achievementPopups = 1;
        /// The rooms heard (kRoom*: off, the game's own, or -- on a DSi --
        /// the DSP's richer one); how the sound is laid out (kOutput*: the
        /// console's speakers, headphones, one channel); and, on a DSi, how
        /// fine it is (kQuality*).
        std::uint8_t roomEffects = 1;
        std::uint8_t outputMode = 0;
        std::uint8_t soundQuality = 0;

        /// The player as the others see them in a match: the name (as typed,
        /// any bytes but the last, which is always the end; empty: the
        /// console's owner's), the model, its top and bottom colours (a hue
        /// each, 0..255, as the game's topcolor and bottomcolor), and the
        /// spray (a logo of ui/logos.hic, and its colour, kSprayColour*).
        char name[kProfileNameBytes] = {};
        std::uint8_t playerModel = 0;
        std::uint8_t topColour = 30;
        std::uint8_t bottomColour = 6;
        std::uint8_t spray = 0;
        std::uint8_t sprayColour = 0;
        /// The match this console last hosted, to host it again: the mode
        /// (kMode*), the map's name (empty: the first there is), the
        /// limits (0: none), the game's mp_weaponstay, mp_falldamage,
        /// mp_friendlyfire, mp_flashlight and autoaim, how many may play
        /// (kPlayersLeast..kPlayersMost), and whether consoles without the
        /// game are let in.
        std::uint8_t hostMode = 0;
        char hostMap[kHostMapBytes] = {};
        std::uint8_t fragLimit = 20;
        std::uint8_t minuteLimit = 10;
        std::uint8_t weaponsStay = 0;
        std::uint8_t fallDamage = 0;
        std::uint8_t friendlyFire = 0;
        std::uint8_t flashlight = 0;
        std::uint8_t hostAutoaim = 1;
        std::uint8_t maxPlayers = 4;
        std::uint8_t allowGuests = 1;
        /// How long a match's scores are shown (the game's mp_chattime:
        /// kChatLeast..kChatMost, by fives); whether a lobby's matches are
        /// added up; how the next map is chosen (kNext*), and the maps it goes
        /// round, by name hash (Rotation).
        std::uint8_t chatSeconds = kChatDefault;
        std::uint8_t sessionTotals = 0;
        std::uint8_t nextMode = 0;
        std::uint8_t rotationCount = 0;
        std::uint32_t rotation[kRotationMost] = {};
        /// How long a vote for the next map stays open, in seconds (kVoteSteps).
        std::uint8_t voteSeconds = kVoteDefault;
        /// The top screen's shape (kScreen*).
        std::uint8_t screenShape = kScreenAuto;
        /// A match hosted online: its players may talk (kRuleVoice). Off
        /// unless asked for.
        std::uint8_t hostVoice = 0;
        /// A match hosted: a player who does nothing for this many minutes
        /// is sent away (kIdleSteps; 0: never).
        std::uint8_t hostIdleMinutes = 0;
        /// How hard a Rumble Pak's motor goes (RumbleSetting, HlRumble.hpp;
        /// version 28). Light unless asked otherwise.
        std::uint8_t vibration = 1;

        /// What a file of version 8 held: all but the controls.
        template <class Ar>
        void serializeEight(Ar &ar)
        {
            ar &renderMode &dspAudio &volume[0] &volume[1] &volume[2] &volume[3] &debugReadout &leftHanded &viewBob;
            ar &graphics.fog &graphics.fogStrength &graphics.fogDistance &graphics.fogColour &graphics.modelDetail &
                graphics.worldDetail &graphics.modelLimit &graphics.drawDistance;
        }
        /// ... and of version 9: the layouts, and no more.
        template <class Ar>
        void serializeNine(Ar &ar)
        {
            serializeEight(ar);
            ar &controlScheme &padTurns &padLooks;
        }
        /// ... and of version 10: the controls, and no autoaim.
        template <class Ar>
        void serializeTen(Ar &ar)
        {
            serializeNine(ar);
            ar &aimSpeed &pitchSpeed &invertPitch &duckToggle &firmness &firmPress;
            // (All the actions there were then: a later one is kept later.)
            for (int a = 0; a < kActionsBeforeWalk; ++a)
                ar &keys[a];
        }
        /// ... and of version 11: no achievements' pop-ups to turn off.
        template <class Ar>
        void serializeEleven(Ar &ar)
        {
            serializeTen(ar);
            ar &autoaim;
        }
        /// ... and of version 12: no sound settings but the DSP and the
        /// volumes.
        template <class Ar>
        void serializeTwelve(Ar &ar)
        {
            serializeEleven(ar);
            ar &achievementPopups;
        }
        /// ... and of version 13: no profile, nor a match hosted.
        template <class Ar>
        void serializeThirteen(Ar &ar)
        {
            serializeTwelve(ar);
            ar &roomEffects &outputMode &soundQuality;
        }
        template <class Ar>
        void serialize(Ar &ar)
        {
            // (Version 26's, then what is new, written out rather than
            // called: so shaped, the compiler keeps one copy of it, where
            // the console sent the game has no room for two.)
            serializeTwentyFive(ar);
            for (int a = kActionsBeforeTalk; a < kActionCount; ++a)
                ar &keys[a];
            ar &hostVoice;
#ifndef HLDS_CHILD
            // (A console sent the game has no card: it never reads nor
            // writes a settings file, and hosts nothing -- what it does not
            // read, it has no room for.)
            ar &hostIdleMinutes;
            ar &vibration;
#endif
        }
#ifndef HLDS_CHILD
        /// ... and of version 27: no vibration (read as light, the default).
        template <class Ar>
        void serializeTwentySeven(Ar &ar)
        {
            serializeTwentySix(ar);
            ar &hostIdleMinutes;
        }
#endif
        /// ... and of version 26: idle players never sent away (read as
        /// never, the default).
        template <class Ar>
        void serializeTwentySix(Ar &ar)
        {
            serializeTwentyFive(ar);
            for (int a = kActionsBeforeTalk; a < kActionCount; ++a)
                ar &keys[a];
            ar &hostVoice;
        }
        /// ... and of version 25: no TALK (a custom layout's read with none),
        /// nor talking in a match hosted (read as off, the default).
        template <class Ar>
        void serializeTwentyFive(Ar &ar)
        {
            serializeTwentyFour(ar);
            ar &screenShape;
        }
        /// ... and of version 24: the screen 4:3 (read as auto, the default).
        template <class Ar>
        void serializeTwentyFour(Ar &ar)
        {
            serializeTwentyThree(ar);
            for (int a = kActionsBeforeLook; a < kActionsBeforeTalk; ++a)
                ar &keys[a];
        }
        /// ... and of version 23: no buttons that look (a custom layout's
        /// read with none).
        template <class Ar>
        void serializeTwentyThree(Ar &ar)
        {
            serializeTwentyTwo(ar);
            ar &hideMapName;
        }
        /// ... and of version 22: the map's name always shown while it
        /// loads (read as hidden, the default).
        template <class Ar>
        void serializeTwentyTwo(Ar &ar)
        {
            serializeTwentyOne(ar);
            ar &dialogueFocus;
        }
        /// ... and of version 21: no dialogue focus (read as light, the
        /// default).
        template <class Ar>
        void serializeTwentyOne(Ar &ar)
        {
            serializeTwenty(ar);
            ar &cheats &keyboardLayout;
        }
        /// ... and of version 20: no Cheats switch (every cheat let through), and
        /// the keyboard QWERTY.
        template <class Ar>
        void serializeTwenty(Ar &ar)
        {
            serializeNineteen(ar);
            ar &tapUse;
        }
        /// ... and of version 19: taps were always USE (read as off).
        template <class Ar>
        void serializeNineteen(Ar &ar)
        {
            serializeEighteen(ar);
            ar &tapJump;
        }
        /// ... and of version 18: no choice about the double tap.
        template <class Ar>
        void serializeEighteen(Ar &ar)
        {
            serializeSeventeen(ar);
            ar &walkToggle &jumpWalks &keys[kActionWalk];
        }
        /// ... and of version 17: no walk.
        template <class Ar>
        void serializeSeventeen(Ar &ar)
        {
            serializeSixteen(ar);
            ar &autosave;
        }
        /// ... and of version 16: no choice about the game's own save.
        template <class Ar>
        void serializeSixteen(Ar &ar)
        {
            serializeFifteen(ar);
            ar &voteSeconds;
        }
        /// ... and of version 15: no vote to time.
        template <class Ar>
        void serializeFifteen(Ar &ar)
        {
            serializeFourteen(ar);
            ar &chatSeconds &sessionTotals &nextMode &rotationCount;
            for (std::uint32_t &hash : rotation)
                ar &hash;
        }
        /// ... and of version 14: nothing of what follows a match.
        template <class Ar>
        void serializeFourteen(Ar &ar)
        {
            serializeThirteen(ar);
            for (char &c : name)
                ar &c;
            ar &playerModel &topColour &bottomColour &spray &sprayColour;
            ar &hostMode;
            for (char &c : hostMap)
                ar &c;
            ar &fragLimit &minuteLimit &weaponsStay &fallDamage &friendlyFire &flashlight &hostAutoaim &maxPlayers &
                allowGuests;
        }
    };

    inline constexpr std::uint8_t kRoomOff = 0, kRoomClassic = 1, kRoomEnhanced = 2, kRoomChoices = 3;
    inline constexpr std::uint8_t kOutputSpeakers = 0, kOutputHeadphones = 1, kOutputMono = 2, kOutputChoices = 3;
    inline constexpr std::uint8_t kQualityStandard = 0, kQualityHigh = 1, kQualityChoices = 2;

    inline constexpr char kSettingsMagic[4] = {'H', 'L', 'S', 'T'};
    inline constexpr std::uint16_t kSettingsVersion = 28;
    /// The versions before, which are still read: 27 had no vibration (read
    /// as light, the default); 26 never sent idle players
    /// away (read as never); 25 had no TALK, nor
    /// talking in a match hosted; 24 had no screen shape
    /// (read as auto); 23 had no buttons that
    /// look, nor the BUTTONS ONLY layout; 22 always showed the map's
    /// name while it loaded (read as hidden, the default); 21 had no dialogue focus
    /// (read as light, the default); 19 had taps always USE in
    /// the modern layout (read as off, the default now); 18 had no choice
    /// about the double tap (it jumped); 17 had no walk; 16 had no autosave to turn
    /// off, and its autoaim was on by default, so is read as off; 13 had no profile and no
    /// match hosted; 12 had no room effects,
    /// output or quality (its DSP switch is the room effects' Enhanced);
    /// 11 had no achievements' pop-ups to turn off; 10 had no autoaim; 9 had
    /// the layouts but no aim speed, custom buttons or stylus pressure; 8 no
    /// controls to choose.
    inline constexpr std::uint16_t kSettingsVersionTwentySeven = 27;
    inline constexpr std::uint16_t kSettingsVersionTwentySix = 26;
    inline constexpr std::uint16_t kSettingsVersionTwentyFive = 25;
    inline constexpr std::uint16_t kSettingsVersionTwentyFour = 24;
    inline constexpr std::uint16_t kSettingsVersionTwentyThree = 23;
    inline constexpr std::uint16_t kSettingsVersionTwentyTwo = 22;
    inline constexpr std::uint16_t kSettingsVersionTwentyOne = 21;
    inline constexpr std::uint16_t kSettingsVersionTwenty = 20;
    inline constexpr std::uint16_t kSettingsVersionNineteen = 19;
    inline constexpr std::uint16_t kSettingsVersionEighteen = 18;
    inline constexpr std::uint16_t kSettingsVersionSeventeen = 17;
    inline constexpr std::uint16_t kSettingsVersionSixteen = 16;
    inline constexpr std::uint16_t kSettingsVersionFifteen = 15;
    inline constexpr std::uint16_t kSettingsVersionFourteen = 14;
    inline constexpr std::uint16_t kSettingsVersionThirteen = 13;
    inline constexpr std::uint16_t kSettingsVersionTwelve = 12;
    inline constexpr std::uint16_t kSettingsVersionEleven = 11;
    inline constexpr std::uint16_t kSettingsVersionTen = 10;
    inline constexpr std::uint16_t kSettingsVersionNine = 9;
    inline constexpr std::uint16_t kSettingsVersionBefore = 8;
    /// The most a settings file can be (what the console reads it into).
    inline constexpr std::size_t kSettingsMostBytes = 256;
    inline constexpr int kVolumeMusic = 0;
    inline constexpr int kVolumeEffects = 1;
    inline constexpr int kVolumeVoices = 2;
    inline constexpr int kVolumeAmbience = 3;
    inline constexpr int kVolumeMost = 10;

    struct SettingsHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t payloadBytes;
        std::uint32_t crc32;
    };
    static_assert(sizeof(SettingsHeader) == 12);

    inline std::vector<std::uint8_t> writeSettings(const SettingsData &data)
    {
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        yas::mem_ostream os;
        yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
        oa &data;
        const auto buf = os.get_intrusive_buffer();

        SettingsHeader header{};
        std::memcpy(header.magic, kSettingsMagic, sizeof(header.magic));
        header.version = kSettingsVersion;
        header.payloadBytes = static_cast<std::uint16_t>(buf.size);
        header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data),
                             static_cast<std::uint32_t>(buf.size));
        std::vector<std::uint8_t> out(sizeof(header) + buf.size);
        std::memcpy(out.data(), &header, sizeof(header));
        std::memcpy(out.data() + sizeof(header), buf.data, buf.size);
        return out;
    }

    /// False -- leaving `out` at its defaults -- for anything that is not a
    /// settings file of this version, and for values out of range.
    /// `data` read or written as a file of `version` has it.
    template <class Ar>
    inline void serializeSettings(std::uint16_t version, SettingsData &data, Ar &ar)
    {
        if (version == kSettingsVersionBefore)
            data.serializeEight(ar);
        else if (version == kSettingsVersionNine)
            data.serializeNine(ar);
        else if (version == kSettingsVersionTen)
            data.serializeTen(ar);
        else if (version == kSettingsVersionEleven)
            data.serializeEleven(ar);
        else if (version == kSettingsVersionTwelve)
            data.serializeTwelve(ar);
        else if (version == kSettingsVersionThirteen)
            data.serializeThirteen(ar);
        else if (version == kSettingsVersionFourteen)
            data.serializeFourteen(ar);
        else if (version == kSettingsVersionFifteen)
            data.serializeFifteen(ar);
        else if (version == kSettingsVersionSixteen)
            data.serializeSixteen(ar);
        else if (version == kSettingsVersionSeventeen)
            data.serializeSeventeen(ar);
        else if (version == kSettingsVersionEighteen)
            data.serializeEighteen(ar);
        else if (version == kSettingsVersionNineteen)
            data.serializeNineteen(ar);
        else if (version == kSettingsVersionTwenty)
            data.serializeTwenty(ar);
        else if (version == kSettingsVersionTwentyOne)
            data.serializeTwentyOne(ar);
        else if (version == kSettingsVersionTwentyTwo)
            data.serializeTwentyTwo(ar);
        else if (version == kSettingsVersionTwentyThree)
            data.serializeTwentyThree(ar);
        else if (version == kSettingsVersionTwentyFour)
            data.serializeTwentyFour(ar);
#ifndef HLDS_CHILD
        // (A console sent the game has no card, and never reads a file of the
        // versions before: what it does not read, it has no room for.)
        else if (version == kSettingsVersionTwentyFive)
            data.serializeTwentyFive(ar);
        else if (version == kSettingsVersionTwentySix)
            data.serializeTwentySix(ar);
        else if (version == kSettingsVersionTwentySeven)
            data.serializeTwentySeven(ar);
#endif
        else
            ar &data;
    }

    /// How many bytes a file of `version` holds after its header: every
    /// setting is a fixed size, so one length for each version. (A file of
    /// another length, read, would run out, which stops the console.)
    inline std::size_t settingsPayloadBytes(std::uint16_t version)
    {
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        SettingsData data;
        yas::mem_ostream os;
        yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
        serializeSettings(version, data, oa);
        return os.get_intrusive_buffer().size;
    }

    inline bool readSettings(const std::uint8_t *bytes, std::size_t size, SettingsData &out)
    {
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        SettingsHeader header;
        if (size < sizeof(header))
            return false;
        std::memcpy(&header, bytes, sizeof(header));
        if (std::memcmp(header.magic, kSettingsMagic, sizeof(header.magic)) != 0 ||
            (header.version != kSettingsVersion && header.version != kSettingsVersionTwentySeven &&
             header.version != kSettingsVersionTwentySix &&
             header.version != kSettingsVersionTwentyFive &&
             header.version != kSettingsVersionTwentyFour &&
             header.version != kSettingsVersionTwentyThree &&
             header.version != kSettingsVersionTwentyTwo &&
             header.version != kSettingsVersionTwentyOne &&
             header.version != kSettingsVersionTwenty &&
             header.version != kSettingsVersionNineteen &&
             header.version != kSettingsVersionEighteen &&
             header.version != kSettingsVersionSeventeen &&
             header.version != kSettingsVersionSixteen &&
             header.version != kSettingsVersionFifteen &&
             header.version != kSettingsVersionFourteen &&
             header.version != kSettingsVersionThirteen &&
             header.version != kSettingsVersionTwelve &&
             header.version != kSettingsVersionEleven &&
             header.version != kSettingsVersionTen &&
             header.version != kSettingsVersionNine && header.version != kSettingsVersionBefore) ||
            header.payloadBytes != size - sizeof(header) ||
            header.payloadBytes != settingsPayloadBytes(header.version) ||
            crc32(bytes + sizeof(header), header.payloadBytes) != header.crc32)
            return false;

        SettingsData read;
        yas::mem_istream is(bytes + sizeof(header), header.payloadBytes);
        yas::binary_iarchive<yas::mem_istream, flags> ia(is);
        // (A file from before there was a choice was played the classic way.)
        if (header.version == kSettingsVersionBefore)
            read.controlScheme = kControlsClassic;
        serializeSettings(header.version, read, ia);
        // (Autoaim was on until asked off: off now until asked for, once.)
        if (header.version < kSettingsVersionSeventeen)
            read.autoaim = 0;
        // (Before 13: the DSP switched on is the DSP's rooms; else the
        // game's own.)
        if (header.version < kSettingsVersionThirteen)
            read.roomEffects = read.dspAudio != 0 ? kRoomEnhanced : kRoomClassic;
        if (read.roomEffects >= kRoomChoices || read.outputMode >= kOutputChoices ||
            read.soundQuality >= kQualityChoices || read.dialogueFocus >= kFocusChoices)
            return false;
        if (read.controlScheme >= kControlsSchemes || read.padTurns > 1 || read.padLooks > 1)
            return false;
        if (read.aimSpeed < 1 || read.aimSpeed > kAimSpeedMost || read.pitchSpeed < 1 ||
            read.pitchSpeed > kAimSpeedMost || read.invertPitch > 1 || read.duckToggle > 1 ||
            read.walkToggle > 1 || read.jumpWalks > 1 || read.tapJump > 1 || read.tapUse > 1 || read.firmness > kFirmnessMost || read.firmPress >= kFirmActions)
            return false;
        for (std::uint16_t key : read.keys)
        {
            if (key & ~kAssignable)
                return false;
        }
        if (read.dspAudio > 1 || read.autoaim > 1 || read.autosave > 1 || read.achievementPopups > 1 ||
            read.debugReadout > 1 || read.leftHanded > 1 || read.viewBob > 1 || read.cheats > 1 ||
            read.hideMapName > 1 || read.screenShape >= kScreenChoices ||
            read.keyboardLayout >= kKeyboardLayouts ||
            !read.graphics.valid())
            return false;
        for (std::uint8_t level : read.volume)
        {
            if (level > kVolumeMost)
                return false;
        }
        // (The names are as they were typed, and not judged: only ended.)
        read.name[kProfileNameBytes - 1] = 0;
        read.hostMap[kHostMapBytes - 1] = 0;
        if (read.playerModel >= kCharacterCount)
            read.playerModel = 0;
        if (read.sprayColour >= kSprayColours || read.hostMode >= kModeChoices || read.weaponsStay > 1 ||
            read.fallDamage > 1 || read.friendlyFire > 1 || read.flashlight > 1 || read.hostAutoaim > 1 || read.hostVoice > 1 ||
            read.maxPlayers < kPlayersLeast || read.maxPlayers > kPlayersMost || read.allowGuests > 1 ||
            read.chatSeconds < kChatLeast || read.chatSeconds > kChatMost || read.sessionTotals > 1 ||
            read.nextMode >= kNextChoices || read.rotationCount > kRotationMost ||
            read.voteSeconds < kChatLeast || read.voteSeconds > kVoteMost)
            return false;
#ifndef HLDS_CHILD
        // (A console sent the game hosts nothing, and reads no file: what it
        // does not check, it has no room for.)
        if (!idleStep(read.hostIdleMinutes) || read.vibration >= kRumbleSettings)
            return false;
#endif
        out = read;
        return true;
    }
} // namespace hldata
