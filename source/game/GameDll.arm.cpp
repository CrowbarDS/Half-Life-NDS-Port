#include "game/GameLayout.hpp"

#include <cstring>
#include <filesystem.h>
#include <nds.h>

#include "DebugLog.hpp"
#include "Global.hpp"
#include "Guard.hpp"
#include "audio/Sound.hpp"
#include "data/LevelFile.hpp"
#include "data/ModelCache.hpp"
#include "data/TextureStream.hpp"
#include "game/Input.hpp"
#include "game/Player.hpp"
#include "game/Think.hpp"
#include "net/NetGame.hpp"
#include "render/BottomScreen.hpp"
#include "render/Camera.hpp"
#include "render/HudDraw.hpp"
#include "render/ScreenFade.hpp"
#include "render/TopText.hpp"
#include "platform/Rumble.hpp"

// Loading the game library, and the engine's table for it (GameApi.hpp).
// (ARM, the file name says: GCC cannot take the address of the hot code,
// which is ARM, in a Thumb file's constant -- it stops, confused.)

extern "C" const hlrt::EngineFuncs hlEngine = {
    .version = hlrt::kGameApiVersion,

    .game = &hlrt::game,
    .bottomScreen = &hlrt::bottomScreen,
    .camera = &hlrt::camera,
    .hudDraw = &hlrt::hudDraw,
    .input = &hlrt::input,
    .level = &hlrt::level,
    .modelCache = &hlrt::modelCache,
    .netGame = &hlrt::netGame,
    .player = &hlrt::player,
    .screenFade = &hlrt::screenFade,
    .sound = &hlrt::sound,
    .textureStream = &hlrt::textureStream,
    .think = &hlrt::think,
    .topText = &hlrt::topText,

    .bottomSetStatus = [](hlrt::BottomScreen &b, int weapon, int clip, int spare, unsigned have, unsigned empty,
                          bool canReload) { b.setStatus(weapon, clip, spare, have, empty, canReload); },
    .bottomSetLamp = [](hlrt::BottomScreen &b, bool suit, bool on, int percent) { b.setLamp(suit, on, percent); },
    .cameraRefreshProjection = [](hlrt::Camera &c) { c.refreshProjection(); },
    .cameraUpdate = [](hlrt::Camera &c) { c.update(); },
    .cameraClampPitch = [](hlrt::Camera &c) { c.clampPitch(); },
    .hudSetStatus = [](hlrt::HudDraw &h, bool suit, int health, int armor, int weapon, int clip, int spare,
                       int extra) { h.setStatus(suit, health, armor, weapon, clip, spare, extra); },
    .netTick = [](hlrt::NetGame &n, std::uint16_t yaw, std::int32_t pitch, std::uint32_t did, bool lamp, int ms) {
        n.tick(yaw, pitch, did, lamp, ms);
    },
    .netUsed = [](hlrt::NetGame &n, int index) { n.used(index); },
    .netSpray = [](hlrt::NetGame &n) { return n.spray(); },
    .playerFrame = [](hlrt::Player &p, int vblanks) { p.frame(vblanks); },
    .playerTurn = [](hlrt::Player &p) { p.turn(); },
    .fadeSetDim = [](hlrt::ScreenFade &f, bool under) { f.setDim(under); },
    .modelWield = [](hlrt::ModelCache &m, int index) { m.wield(index); },
    .soundUi = [](hlrt::Sound &s, int which, int volume) { s.ui(which, volume); },
    .soundMusicTrack = [](const hlrt::Sound &s) { return s.musicTrack(); },
    .soundMusicMs = [](const hlrt::Sound &s) { return s.musicMs(); },
    .soundSetMusic = [](hlrt::Sound &s, int track, std::uint32_t ms) { s.setMusic(track, ms); },
    .streamReport = [](hlrt::TextureStream &t, const char *why) { t.report(why); },
    .thinkAttach = [](hlrt::Think &t, hldata::LogicWorld &w) { t.attach(w); },
    .thinkDetach = [](hlrt::Think &t) { t.detach(); },
    .topShow = [](hlrt::TopText &t, const hldata::MessageRecord &message, const char *text) { t.show(message, text); },
    .topTick = [](hlrt::TopText &t, int ms) { t.tick(ms); },
    .topClear = [](hlrt::TopText &t) { t.clear(); },
    .rumbleKick = [](int strength, int blanks) { hlrt::rumble::kick(strength, blanks); },
    .vblanks = &Global::vblanks,
#ifdef HLDS_PROFILE
    .perfNow = &hlrt::DebugLog::Perf::now,
    .perfAdd = [](int section, std::uint32_t ticks) {
        hlrt::DebugLog::Perf::add(static_cast<hlrt::DebugLog::Section>(section), ticks);
    },
#else
    .perfNow = nullptr,
    .perfAdd = nullptr,
#endif

    .traceHull = &hldata::traceHull,
    .traceAll = &hldata::traceAll,
    .poseModel = &hldata::poseModel,
    .actorAdvance = &hldata::actorAdvance,
    .walkTick = &hldata::walkTick,
};

// The game's core is linked into the program (HL_STATIC_GAME): its table is
// asked for straight. (Its families are libraries of their own.)
extern "C" bool GetGameAPI(std::uint32_t version, const hlrt::GameLayout &layout, const hlrt::EngineFuncs *engine,
                           hlrt::GameFuncs *game);

namespace hlrt
{
    GameFuncs gameApi;

    bool loadGame(const char **why)
    {
#ifndef HLDS_CHILD
        // The ROM's own file system (its families, and the child's to send:
        // Families.cpp), read through the card's FAT, which Global::Init has
        // brought up. At start, as the whole game's library once was, so
        // that nothing is read with one working folder and then another.
        if (!nitroFSInit(nullptr))
        {
            *why = "The ROM's files cannot be read.";
            return false;
        }
#endif
        if (!GetGameAPI(kGameApiVersion, gameLayout(), &hlEngine, &gameApi))
        {
            *why = "The game linked in is not of this build.";
            return false;
        }
        return true;
    }
} // namespace hlrt

