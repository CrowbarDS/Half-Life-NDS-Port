#include "global_header.hpp"

#include "Settings.hpp"

#include <cstring>

#include "Global.hpp"
#include "net/NetLink.hpp"
#include "Achievements.hpp"
#include "DebugLog.hpp"

Settings settings;

// (Sent the game, a console has no card to keep its settings on: it has
// those it starts with and what is changed in its menus, neither read nor
// written -- and none of the files' readers and writers, about 16 KB of its
// program. A test build that reads the card has them.)
#if defined(HLDS_CHILD) && !defined(HLDS_CHILD_FROMCARD)
#define HLDS_SETTINGS_IN_MEMORY 1
#endif

#ifndef HLDS_SETTINGS_IN_MEMORY
#include <fcntl.h>
#include <unistd.h>

namespace
{
    // What the card's file holds, as last read or written: a save that would
    // write the same is not made. Each write of the file is several of the
    // card's -- some 200 ms each on a DSpico (2026-11-27) -- and the settings
    // are saved on every way out of the pause menu.
    std::uint8_t onCard[hldata::kSettingsMostBytes];
    std::size_t onCardBytes = 0;
    bool onCardKnown = false;
} // namespace
#endif

void Settings::load()
{
#ifndef HLDS_SETTINGS_IN_MEMORY
    if (!global.hasFat)
        return;
    std::FILE *file = std::fopen(global.dataPath("settings.dat").c_str(), "rb");
    if (file == nullptr)
        return;
    std::uint8_t bytes[hldata::kSettingsMostBytes];
    const std::size_t size = std::fread(bytes, 1, sizeof(bytes), file);
    std::fclose(file);
    std::memcpy(onCard, bytes, size);
    onCardBytes = size;
    onCardKnown = true;

    hldata::SettingsData data;
    if (hldata::readSettings(bytes, size, data))
    {
        roomEffects = data.roomEffects;
        outputMode = data.outputMode;
        soundQuality = data.soundQuality;
        for (int i = 0; i < 4; ++i)
            volume[i] = data.volume[i];
        dialogueFocus = data.dialogueFocus;
        debugReadout = data.debugReadout != 0;
        leftHanded = data.leftHanded != 0;
        viewBob = data.viewBob != 0;
        graphics = data.graphics;
        screenShape = data.screenShape;
        controlScheme = data.controlScheme;
        padTurns = data.padTurns != 0;
        padLooks = data.padLooks != 0;
        aimSpeed = data.aimSpeed;
        pitchSpeed = data.pitchSpeed;
        invertPitch = data.invertPitch != 0;
        duckToggle = data.duckToggle != 0;
        walkToggle = data.walkToggle != 0;
        jumpWalks = data.jumpWalks != 0;
        tapJump = data.tapJump != 0;
        tapUse = data.tapUse != 0;
        crouchJump = data.crouchJump != 0;
        firmness = data.firmness;
        firmPress = data.firmPress;
        for (int a = 0; a < hldata::kActionCount; ++a)
            keys[a] = data.keys[a];
        autoaim = data.autoaim != 0;
        autosave = data.autosave != 0;
        achievementPopups = data.achievementPopups != 0;
        hideMapName = data.hideMapName != 0;
        cheats = data.cheats != 0;
        keyboardLayout = data.keyboardLayout;
        std::memcpy(name, data.name, sizeof(name));
        playerModel = data.playerModel;
        topColour = data.topColour;
        bottomColour = data.bottomColour;
        spray = data.spray;
        sprayColour = data.sprayColour;
        hostMode = data.hostMode;
        std::memcpy(hostMap, data.hostMap, sizeof(hostMap));
        fragLimit = data.fragLimit;
        minuteLimit = data.minuteLimit;
        weaponsStay = data.weaponsStay != 0;
        fallDamage = data.fallDamage;
        friendlyFire = data.friendlyFire != 0;
        flashlight = data.flashlight != 0;
        hostAutoaim = data.hostAutoaim != 0;
        hostVoice = data.hostVoice;
#ifndef HLDS_CHILD
        hostIdleMinutes = data.hostIdleMinutes;
        vibration = data.vibration;
        lastGame = data.lastGame;
#endif
        maxPlayers = data.maxPlayers;
        allowGuests = data.allowGuests != 0;
        chatSeconds = data.chatSeconds;
        sessionTotals = data.sessionTotals != 0;
        nextMode = data.nextMode;
        rotationCount = data.rotationCount;
        voteSeconds = data.voteSeconds;
        for (int i = 0; i < hldata::kRotationMost; ++i)
            rotation[i] = data.rotation[i];
    }
#endif
}

const char *Settings::playerName() const
{
    static char owner[hldata::kProfileNameBytes];
    if (name[0] != 0)
        return name;
    if (owner[0] == 0)
        hlrt::netlink::ownerName(owner, sizeof(owner));
    return owner;
}

void Settings::reset(int category)
{
    const Settings first;
    switch (category)
    {
    case 0:
        roomEffects = first.roomEffects;
        outputMode = first.outputMode;
        soundQuality = first.soundQuality;
        for (int i = 0; i < 4; ++i)
            volume[i] = first.volume[i];
        dialogueFocus = first.dialogueFocus;
        break;
    case 1:
        graphics = first.graphics;
        screenShape = first.screenShape;
        break;
    case 2:
        // (The layouts; the buttons of one's own are BUTTONS'.)
        if (controlScheme != hldata::kControlsCustom)
            controlScheme = first.controlScheme;
        leftHanded = first.leftHanded;
        duckToggle = first.duckToggle;
        walkToggle = first.walkToggle;
        jumpWalks = first.jumpWalks;
        tapJump = first.tapJump;
        tapUse = first.tapUse;
        crouchJump = first.crouchJump;
        padTurns = first.padTurns;
        padLooks = first.padLooks;
        autoaim = first.autoaim;
        break;
    case 3:
        aimSpeed = first.aimSpeed;
        pitchSpeed = first.pitchSpeed;
        invertPitch = first.invertPitch;
        firmness = first.firmness;
        firmPress = first.firmPress;
        break;
    case 4:
        // No buttons of one's own: the layout they were made from goes too.
        for (int a = 0; a < hldata::kActionCount; ++a)
            keys[a] = first.keys[a];
        if (controlScheme == hldata::kControlsCustom)
            controlScheme = first.controlScheme;
        break;
    case kProfileCategory:
        std::memcpy(name, first.name, sizeof(name));
        playerModel = first.playerModel;
        topColour = first.topColour;
        bottomColour = first.bottomColour;
        spray = first.spray;
        sprayColour = first.sprayColour;
        break;
    default:
        viewBob = first.viewBob;
        debugReadout = first.debugReadout;
        achievementPopups = first.achievementPopups;
        autosave = first.autosave;
        hideMapName = first.hideMapName;
        vibration = first.vibration;
        if (cheats && !first.cheats)
            achievements.cheatsSwitched(false);
        cheats = first.cheats;
        keyboardLayout = first.keyboardLayout;
        break;
    }
}

bool Settings::save() const
{
    const hlrt::DebugLog::Perf::CardWho cardWho("settings");
#ifdef HLDS_SETTINGS_IN_MEMORY
    return false;
#else
    if (!global.hasFat)
        return false;
    hldata::SettingsData data;
    data.roomEffects = roomEffects;
    data.outputMode = outputMode;
    data.soundQuality = soundQuality;
    // (For a build before, which has only the DSP's switch.)
    data.dspAudio = roomEffects == hldata::kRoomEnhanced ? 1 : 0;
    for (int i = 0; i < 4; ++i)
        data.volume[i] = volume[i];
    data.dialogueFocus = dialogueFocus;
    data.debugReadout = debugReadout ? 1 : 0;
    data.leftHanded = leftHanded ? 1 : 0;
    data.viewBob = viewBob ? 1 : 0;
    data.graphics = graphics;
    data.screenShape = screenShape;
    data.controlScheme = controlScheme;
    data.padTurns = padTurns ? 1 : 0;
    data.padLooks = padLooks ? 1 : 0;
    data.aimSpeed = aimSpeed;
    data.pitchSpeed = pitchSpeed;
    data.invertPitch = invertPitch ? 1 : 0;
    data.duckToggle = duckToggle ? 1 : 0;
    data.walkToggle = walkToggle ? 1 : 0;
    data.jumpWalks = jumpWalks ? 1 : 0;
    data.tapJump = tapJump ? 1 : 0;
    data.tapUse = tapUse ? 1 : 0;
    data.crouchJump = crouchJump ? 1 : 0;
    data.firmness = firmness;
    data.firmPress = firmPress;
    for (int a = 0; a < hldata::kActionCount; ++a)
        data.keys[a] = keys[a];
    data.autoaim = autoaim ? 1 : 0;
    data.autosave = autosave ? 1 : 0;
    data.achievementPopups = achievementPopups ? 1 : 0;
    data.hideMapName = hideMapName ? 1 : 0;
    data.cheats = cheats ? 1 : 0;
    data.keyboardLayout = keyboardLayout;
    std::memcpy(data.name, name, sizeof(name));
    data.playerModel = playerModel;
    data.topColour = topColour;
    data.bottomColour = bottomColour;
    data.spray = spray;
    data.sprayColour = sprayColour;
    data.hostMode = hostMode;
    std::memcpy(data.hostMap, hostMap, sizeof(hostMap));
    data.fragLimit = fragLimit;
    data.minuteLimit = minuteLimit;
    data.weaponsStay = weaponsStay ? 1 : 0;
    data.fallDamage = fallDamage;
    data.friendlyFire = friendlyFire ? 1 : 0;
    data.flashlight = flashlight ? 1 : 0;
    data.hostAutoaim = hostAutoaim ? 1 : 0;
    data.hostVoice = hostVoice;
#ifndef HLDS_CHILD
    data.hostIdleMinutes = hostIdleMinutes;
    data.vibration = vibration;
    data.lastGame = lastGame;
#endif
    data.maxPlayers = maxPlayers;
    data.allowGuests = allowGuests ? 1 : 0;
    data.chatSeconds = chatSeconds;
    data.sessionTotals = sessionTotals ? 1 : 0;
    data.nextMode = nextMode;
    data.rotationCount = rotationCount;
    data.voteSeconds = voteSeconds;
    for (int i = 0; i < hldata::kRotationMost; ++i)
        data.rotation[i] = rotation[i];
    const std::vector<std::uint8_t> bytes = hldata::writeSettings(data);

    if (onCardKnown && bytes.size() == onCardBytes && std::memcmp(bytes.data(), onCard, onCardBytes) == 0)
        return true;
    // (Written over where it is, in one call: made anew, the card's table of
    // its pieces would be written twice more.)
    const std::string path = global.dataPath("settings.dat");
    int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0)
        fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    const long was = ::lseek(fd, 0, SEEK_END);
    bool ok = was >= 0 && ::lseek(fd, 0, SEEK_SET) == 0 &&
              ::write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size());
    if (ok && was > static_cast<long>(bytes.size()))
        ok = ftruncate(fd, static_cast<off_t>(bytes.size())) == 0;
    ok = ::close(fd) == 0 && ok;
    onCardKnown = ok && bytes.size() <= sizeof(onCard);
    if (onCardKnown)
    {
        std::memcpy(onCard, bytes.data(), bytes.size());
        onCardBytes = bytes.size();
    }
    return ok;
#endif
}

