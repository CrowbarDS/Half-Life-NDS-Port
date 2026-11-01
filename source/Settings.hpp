#pragma once

// The player's settings on the console: loaded once at boot, saved when one
// changes. The format and its checks are in shared/HlSettings.hpp.

#include "HlControls.hpp"
#include "HlSettings.hpp"

class Settings
{
public:
    /// The rooms heard (hldata::kRoom*: off, the game's own, the DSi's
    /// DSP's), how the sound is laid out (kOutput*), and on a DSi how fine
    /// it is (kQuality*).
    std::uint8_t roomEffects = hldata::kRoomClassic;
    std::uint8_t outputMode = hldata::kOutputSpeakers;
    std::uint8_t soundQuality = hldata::kQualityStandard;
    /// How loud each layer of the sound is, 0 to 10 (hldata::kVolume*).
    std::uint8_t volume[4] = {8, 10, 10, 8};
    /// The debug readout along the top of the touch screen.
    bool debugReadout = false;
    /// The stylus is in the left hand: the touch screen is mirrored.
    bool leftHanded = false;
    /// The layout of the buttons (hldata::kControls*), and which of the pad
    /// and the stylus turns and which moves.
    std::uint8_t controlScheme = hldata::kControlsModern;
    bool padTurns = false;
    bool padLooks = false;
    /// How far the stylus turns the view across and up and down (10 as it
    /// always was; 1 to 20), and whether up is down.
    std::uint8_t aimSpeed = hldata::kAimSpeedDefault;
    std::uint8_t pitchSpeed = hldata::kAimSpeedDefault;
    bool invertPitch = false;
    /// The crouch button toggles (else it is held).
    bool duckToggle = false;
    /// How strict the stylus is about light touches, and what a firm press
    /// does (HlControls.hpp).
    std::uint8_t firmness = 4;
    std::uint8_t firmPress = hldata::kFirmOff;
    /// The CUSTOM layout's buttons, each action's.
    std::uint16_t keys[hldata::kActionCount] = {};
    /// The guns' aim bends towards a monster near it (EASY and MEDIUM).
    bool autoaim = true;
    /// An achievement earned is shown as it is (else only listed).
    bool achievementPopups = true;
    /// All of it, for whoever reads the buttons.
    hldata::ControlSettings controls() const
    {
        hldata::ControlSettings set;
        set.scheme = controlScheme;
        set.leftHanded = leftHanded;
        set.padTurns = padTurns;
        set.padLooks = padLooks;
        set.duckToggle = duckToggle;
        set.firmness = firmness;
        set.firmPress = firmPress;
        for (int a = 0; a < hldata::kActionCount; ++a)
            set.keys[a] = keys[a];
        return set;
    }
    /// The layout made CUSTOM, starting from the buttons it has now.
    void makeCustom()
    {
        if (controlScheme == hldata::kControlsCustom)
            return;
        hldata::keysOf(controls(), keys);
        controlScheme = hldata::kControlsCustom;
    }
    /// The view bobs and leans with a walk (off unless asked for).
    bool viewBob = false;
    /// The fog, and how much detail is given up for speed (HlFog.hpp).
    hldata::GraphicsSettings graphics;
    /// The player in a match (HlSettings.hpp): the name (the console
    /// owner's until another is typed), the model and its colours, the spray.
    char name[hldata::kProfileNameBytes] = {};
    std::uint8_t playerModel = 0;
    std::uint8_t topColour = 30;
    std::uint8_t bottomColour = 6;
    std::uint8_t spray = 0;
    std::uint8_t sprayColour = 0;
    /// The match last hosted, as it was set up (HlSettings.hpp).
    std::uint8_t hostMode = hldata::kModeDeathmatch;
    char hostMap[hldata::kHostMapBytes] = {};
    std::uint8_t fragLimit = 20;
    std::uint8_t minuteLimit = 10;
    bool weaponsStay = false;
    std::uint8_t fallDamage = 0;
    bool friendlyFire = false;
    bool flashlight = false;
    bool hostAutoaim = true;
    std::uint8_t maxPlayers = 4;
    bool allowGuests = true;
    /// What follows a match (hldata::SettingsData's).
    std::uint8_t chatSeconds = hldata::kChatDefault;
    bool sessionTotals = false;
    std::uint8_t nextMode = hldata::kNextHost;
    std::uint8_t rotationCount = 0;
    std::uint32_t rotation[hldata::kRotationMost] = {};
    std::uint8_t voteSeconds = hldata::kVoteDefault;

    /// Read settings.dat. An absent or unreadable file is a first boot, not an
    /// error: the defaults stand.
    void load();
    /// Write settings.dat. False if the card cannot be written.
    bool save() const;
    /// The profile's name as it is to be shown: the console owner's while
    /// none was typed.
    const char *playerName() const;
    /// One of OPTIONS' categories (0 SOUND, 1 GRAPHICS, 2 CONTROLS, 3 TOUCH
    /// AIM, 4 BUTTONS, 5 OTHER), or the profile (kProfileCategory), as it is
    /// at first. (Not saved, nor the sound told: the caller's.)
    void reset(int category);
    static constexpr int kProfileCategory = 6;
};

extern Settings settings;
