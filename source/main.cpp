#include <ctime>

#include "HlManifest.hpp"
#include "net/NetGame.hpp"
#include "global_header.hpp"

#include <cstdlib>
#include <malloc.h>

#include "Achievements.hpp"
#include <sys/stat.h>

#include "DebugLog.hpp"
#include "Global.hpp"
#include "Guard.hpp"
#include "HlCommands.hpp"
#include "HlPreview.hpp"
#include "net/GuestLoad.hpp"
#ifdef HLDS_CHILD
#include "net/AirDevice.hpp"
#endif
#include "Settings.hpp"
#include "data/LevelFile.hpp"
#include "data/ModelCache.hpp"
#include "data/SpriteCache.hpp"
#include "data/TextureCache.hpp"
#include "data/TextureStream.hpp"
#include "game/Game.hpp"
#include "game/GameApi.hpp"
#include "game/Input.hpp"
#include "game/Player.hpp"
#include "game/SaveGame.hpp"
#include "render/BottomScreen.hpp"
#include "render/Camera.hpp"
#include "render/DebugOverlay.hpp"
#include "render/WorldDraw.hpp"
#include "audio/DspAudio.hpp"
#include "audio/Sound.hpp"
#include "render/GpuStats.hpp"
#include "render/EffectDraw.hpp"
#include "render/MapCard.hpp"
#include "render/ProfileView.hpp"
#include "render/ModelDraw.hpp"
#include "render/HudDraw.hpp"
#include "render/FrameFreeze.hpp"
#include "render/ScreenFade.hpp"
#include "render/TopText.hpp"
#include "ui/Menu.hpp"
#include "ui/TouchCanvas.hpp"

namespace hlrt
{
    int hwMathCheck(int count); // HwMathCheck.cpp
}

namespace
{
    /// Show why the game cannot start, and stop. Everything past this point
    /// reads data that is not there, so this is a dead end on purpose.
    [[noreturn]] void haltWith(const char *l1, const char *l2, const char *l3)
    {
        // (The touch screen may not have been set up yet: this does it.)
        hlrt::bottomScreen.showFatal(l1, l2, l3);
        while (true)
            swiWaitForVBlank();
    }

    std::string currentMap;
    /// A map loaded only to be photographed (takeAllPreviews): nothing of it
    /// is shown -- the curtain stays down, the touch screen says what is
    /// being done.
    bool quietLoad = false;
    /// A match's map: loaded as any, but not shown once it is -- the curtain
    /// stays down until every console has it (waitForGo).
    bool holdLoad = false;

#ifdef HLDS_CHILD
    /// Sent the game: what is said while there is nothing else to show.
    void guestSay(const char *l1, const char *l2)
    {
        // (Which it is running as: all that can be seen of it from here.)
        hlrt::bottomScreen.showProgress(global.twlChild ? "DSi mode, 16 MB" : "DS mode", l1, l2, 0, 0, false);
    }
    /// What is being taken from the host, how big and how fast, how far.
    void guestPage(const char *title, const char *step, int percent)
    {
        char note[64];
        const std::uint32_t length = hlrt::guest::airLength();
        const int rate = hlrt::guest::airRate();
        if (length == 0)
            std::snprintf(note, sizeof(note), "From the host");
        else if (rate == 0)
            std::snprintf(note, sizeof(note), "%lu KB from the host", static_cast<unsigned long>(length / 1024));
        else
            std::snprintf(note, sizeof(note), "%lu KB from the host, %d KB a second",
                          static_cast<unsigned long>(length / 1024), rate);
        hlrt::bottomScreen.showProgress(title, step, note, percent, 100, false, true);
    }
    void guestProgress(int percent) { guestPage("HALF-LIFE DS", "Receiving the game", percent); }
    void guestBaseProgress(int percent) { guestPage("JOINING THE MATCH", "Receiving the map's models and sounds", percent); }
    std::string guestMapName;
    /// What the host is told of this console's memory (KB): how far up it
    /// the heap reaches -- what the converter's guestPeak is measured
    /// against, which a DS's 4 MB and a DSi's 8 or 16 differ in -- and what
    /// of it is free now, in the lobby (said on the host's PLAYERS page).
    void guestSayRoom()
    {
        const struct mallinfo info = mallinfo();
        const std::uint32_t top = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(getHeapLimit()) - 0x02000000u);
        const std::size_t free = global.heapHeadroom() + info.fordblks;
        hlrt::netGame.sayRoom(top / 1024, static_cast<std::uint32_t>(free / 1024));
    }
    void guestMapProgress(int percent)
    {
        const std::string step = "Receiving " + guestMapName;
        const int demo = hlrt::netGame.demo();
        guestPage(demo != 0 ? hldata::netDemoTitle(demo) : "JOINING THE MATCH", step.c_str(), percent);
    }
    /// Waited for, a frame at a time with the radio heard, until the match
    /// says this console is in the host's lobby (or `frames` pass).
    bool guestJoined(int frames)
    {
        for (int n = 0; n < frames; ++n)
        {
            swiWaitForVBlank();
            hlrt::netGame.frame();
            if (hlrt::netGame.phase() == hlrt::NetGame::Phase::Lobby)
                return true;
            if (!hlrt::netGame.on())
                return false;
        }
        return false;
    }

    /// Sent the game: the host found -- the console that sent it, as the
    /// program's parameter says (HlNetAir.hpp); else, started some other
    /// way (an emulator's), any game near that lets consoles without the
    /// game in -- and, the first time, the game itself taken from it: its
    /// letters and pictures. Until then there is nothing else to do.
    void guestArrive(bool withGame)
    {
        hldata::NetDlParam param;
        bool sent = hlrt::guest::bootParam(param);
        // (The one that sent it, for a while: gone, whoever else is near.)
        for (int rounds = 0;; ++rounds)
        {
            sent = sent && rounds < 4;
            guestSay("Waiting for the host...", sent ? "Joining the console that sent the game."
                                                     : "Looking for a game to join.");
            bool in = false;
            if (sent)
            {
                // (Asked again every two seconds, for a while: the host
                // opens its game a moment after it has sent this one.)
                for (int tries = 0; tries < 8 && !in; ++tries)
                    in = hlrt::netGame.joinStarter() && guestJoined(120);
            }
            else if (hlrt::netGame.scan())
            {
                for (int n = 0; n < 120; ++n)
                    swiWaitForVBlank();
                hlrt::NetGame::Found rooms[8];
                const int count = hlrt::netGame.found(rooms, 8);
                int best = -1;
                bool demoOnly = false;
                for (int i = 0; i < count; ++i)
                {
                    const hlrt::NetGame::Found &room = rooms[i];
                    // (A demo: for a console in DSi mode alone.)
                    if (room.room.version == hldata::kNetVersion && room.room.mode >= hldata::kNetModeDemoBase && !global.twlChild)
                    {
                        demoOnly = true;
                        continue;
                    }
                    if (room.room.version == hldata::kNetVersion && room.room.guests && room.players < room.most &&
                        (best < 0 || room.bars > rooms[best].bars))
                        best = i;
                }
                if (best < 0 && demoOnly)
                {
                    guestSay(hldata::netDemoRefusal(false), "The game near is a demo.");
                    for (int n = 0; n < 180; ++n)
                        swiWaitForVBlank();
                }
                in = best >= 0 && hlrt::netGame.join(rooms[best].index) && guestJoined(600);
            }
            if (in && !withGame)
                return;
            if (in)
            {
                hlrt::guest::airWant(hldata::AirStream::Common);
                guestProgress(0);
                if (hlrt::guest::receiveAir(guestProgress))
                {
                    // (The letters at once, while they are there: the common
                    // stream's files go once the rest has read them.)
                    global.hasData = true;
                    hlrt::touchCanvas.loadFonts();
                    return;
                }
            }
            hlrt::netGame.leave();
            const char *why = hlrt::netGame.ended();
            guestSay(in ? "The host has gone quiet." : "No host found.", why[0] != 0 ? why : "Trying again...");
            for (int n = 0; n < 120; ++n)
                swiWaitForVBlank();
        }
    }
#endif

    /// How many frames are handed to the hardware before the curtain goes
    /// up on a map: the first has then been on the screen for one, and the
    /// map has had a few frames to put itself in order. (Seen: the first
    /// map's first frames with a pale band across their top, which a
    /// curtain of two frames let through.)
    constexpr int curtainFrames()
    {
        return 12;
    }

    /// The picture of a multiplayer map for its card (HlPreview.hpp), the
    /// first time this console hosts a match on it: the map from where the
    /// game shows it between matches (its info_intermission; else where a
    /// player first comes in), the 3D alone -- no display, nothing in hand --
    /// drawn for a few frames behind the curtain, so that what is in view
    /// has its textures, then captured, shrunk, and kept in saves/previews/.
    void drawPreviewScene()
    {
        hlrt::worldDraw.applyFog();
        NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), hlrt::WorldDraw::kCull,
                       hlrt::worldDraw.polygonMode());
        hlrt::camera.apply();
        hlrt::worldDraw.draw();
    }
    void takePreview(const std::string &map)
    {
#ifndef HLDS_CHILD
        if (!global.hasFat)
            return;
        const std::string folder = global.savesDir() + hldata::kPreviewFolder;
        const std::string path = folder + map + ".hpv";
        // (Taken already, of this map as it is now: nothing to do.)
        if (hlrt::mapPictureCurrent(map, hlrt::level.view.header->crc32))
            return;
        const hldata::LevelView &view = hlrt::level.view;
        bool spawn = false;
        const hldata::EntityRecord *from = hldata::netIntermissionPoint(view, 0, spawn);
        if (from == nullptr)
            return;
        hlrt::Camera &camera = hlrt::camera;
        const hldata::fx pos[3] = {camera.pos[0], camera.pos[1], camera.pos[2]};
        const std::uint16_t yaw = camera.yaw;
        const std::int16_t pitch = camera.pitch;
        const bool held = hlrt::game.held.active;
        for (int i = 0; i < 3; ++i)
            camera.pos[i] = from->origin[i];
        if (spawn)
            camera.pos[2] += hldata::fxFromInt(28); // (the eye, over the middle)
        camera.yaw = from->angles[1];
        camera.pitch = static_cast<std::int16_t>(-static_cast<std::int16_t>(from->angles[0])); // (an entity's down is the view's up)
        camera.update();
        hlrt::game.held.active = false;
        hlrt::screenFade.curtainDown();
        for (int n = 0; n < 30; ++n)
        {
            NEA_WaitForVBL(NEA_UPDATE_TASKS);
            hlrt::textureStream.upload();
            hlrt::modelCache.apply();
            NEA_Process(drawPreviewScene);
            hlrt::textureStream.update(isDSiMode() ? 8192 : 4096);
        }
        NEA_Process(drawPreviewScene);
        static std::uint16_t pixels[hldata::kPreviewWidth * hldata::kPreviewHeight];
        if (hlrt::frameFreeze.capturePreview(pixels))
        {
            mkdir(global.savesDir().c_str(), 0777);
            mkdir(folder.c_str(), 0777);
            hldata::PreviewHeader header;
            hldata::previewHeader(header);
            header.mapVersion = hldata::previewMapVersion(hlrt::level.view.header->crc32);
            if (std::FILE *file = std::fopen(path.c_str(), "wb"))
            {
                const bool ok = std::fwrite(&header, sizeof(header), 1, file) == 1 &&
                                std::fwrite(pixels, sizeof(pixels), 1, file) == 1;
                if (std::fclose(file) != 0 || !ok)
                    std::remove(path.c_str());
            }
        }
        for (int i = 0; i < 3; ++i)
            camera.pos[i] = pos[i];
        camera.yaw = yaw;
        camera.pitch = pitch;
        camera.update();
        hlrt::game.held.active = held;
        if (!quietLoad && !holdLoad)
            hlrt::screenFade.curtainUp(curtainFrames());
#else
        (void)map;
#endif
    }

    /// A line in perf.txt about the map being played: what the hardware made
    /// of it (GpuStats).
    void logPerformance()
    {
        if (!perfLogging())
            return;
        hlrt::gpuStats.writeLog(currentMap.c_str(), "1P");
        // With the DSi's DSP in use, how it got on over that map.
        if (hlrt::sound.extended())
        {
            if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
            {
                std::fprintf(log, "dsp: ran short %lu\n", static_cast<unsigned long>(hlrt::dspAudio.dry()));
                std::fclose(log);
            }
        }
    }

    /// What play takes of the heap past what a map's load left, at most, on
    /// the maps that take the most (weapons' skins, the people and monsters
    /// loaded when first met, sounds read when first played), and a margin:
    /// kept back from the streamer's copies (TextureStream::makeCopies).
    /// (Measured on a DS, 2026-11-06, walking among the people and taking up
    /// every weapon: 121 KB at most, on maps with room for their sounds,
    /// which are read when first played; 5 KB where there was not.)
    constexpr std::size_t kPlayReserve = 160 * 1024;

    /// At the end of a map's load (perf.txt): how much room the heap has
    /// left, and what of the map's sounds could be had.
    void writeLoadRoom(const char *map)
    {
        const hlrt::guard::HeapRoom room = hlrt::guard::heapRoom();
        const hlrt::StreamStats st = hlrt::textureStream.stats();
        const hlrt::Sound::LoadStats &snd = hlrt::sound.loadStats();
        std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a");
        if (log == nullptr)
            return;
        std::fprintf(log, "room: %s top %lu, largest hole %lu, %lu in holes; copies %d (%d KB); sounds %s%d of %d, "
                          "%d not tried for room, %d failed\n",
                     map, static_cast<unsigned long>(room.top), static_cast<unsigned long>(room.largestHole),
                     static_cast<unsigned long>(room.inHoles), st.copies, st.copiesKb,
                     snd.tableFailed ? "NONE (no room for their list), " : "", snd.loaded, snd.kept,
                     snd.skippedRoom, snd.failed);
        std::fclose(log);
    }

#ifdef HLDS_PROFILE
    /// Where a load went: a line in perf.txt, a stage at a time, in
    /// milliseconds. (The card's share of each can only be judged on a card.)
    struct LoadClock
    {
        static constexpr int kStages = 8;
        const char *names[kStages] = {};
        std::uint32_t ms[kStages] = {};
        int count = 0;
        std::uint32_t last = hlrt::DebugLog::Perf::now();

        void mark(const char *name)
        {
            const std::uint32_t now = hlrt::DebugLog::Perf::now();
            if (count < kStages)
            {
                names[count] = name;
                // (The timer counts the 33.5 MHz bus clock.)
                ms[count++] = static_cast<std::uint32_t>((std::uint64_t(now - last) * 2u) / 67000u);
            }
            last = hlrt::DebugLog::Perf::now();
        }
        void write(const char *map)
        {
            std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a");
            if (log == nullptr)
                return;
            std::uint32_t total = 0;
            std::fprintf(log, "load: %s", map);
            for (int i = 0; i < count; ++i)
            {
                std::fprintf(log, " %s %lu", names[i], static_cast<unsigned long>(ms[i]));
                total += ms[i];
            }
            std::fprintf(log, " = %lu ms (check %lu), heap %lu\n", static_cast<unsigned long>(total),
                         static_cast<unsigned long>(hlrt::level.checkMs),
                         static_cast<unsigned long>(global.heapHeadroom()));
            std::fclose(log);
        }
    };
#else
    struct LoadClock
    {
        void mark(const char *) {}
        void write(const char *) {}
    };
#endif

    /// The match over: the view is where the map is shown from, whatever the
    /// player was doing. Before the frame is drawn.
    void holdIntermissionView()
    {
        if (!hlrt::netGame.playing() || !hlrt::netGame.match().over)
            return;
        bool spawn = false;
        const hldata::EntityRecord *from =
            hldata::netIntermissionPoint(hlrt::level.view, hlrt::netGame.match().endDraw, spawn);
        if (from == nullptr)
            return;
        hlrt::Camera &camera = hlrt::camera;
        for (int i = 0; i < 3; ++i)
            camera.pos[i] = from->origin[i];
        if (spawn)
            camera.pos[2] += hldata::fxFromInt(28);
        camera.yaw = from->angles[1];
        camera.pitch = static_cast<std::int16_t>(-static_cast<std::int16_t>(from->angles[0]));
        camera.update();
    }

    // What is on the touch screen when a match is over, and for a vote before
    // the first: the scores, the vote, and what is touched of them.
    int scoresShown = -1, voteShown = -1;
    bool touchWas = false, voteWaitShown = false, voteResultShown = false;
    void endScreensReset()
    {
        scoresShown = voteShown = -1;
        voteWaitShown = false;
        hlrt::netGame.setVoteShown(false);
    }
    void endScreens()
    {
        const hldata::NetMatch &match = hlrt::netGame.match();
        // The stylus: the host's LOBBY, or what is on the vote's pages.
        const bool touch = (keysHeld() & KEY_TOUCH) != 0;
        touchPosition at{};
        if (touch)
            touchRead(&at);
#ifndef HLDS_CHILD
        if (touch && !touchWas && hlrt::netGame.isHost() && hlrt::bottomScreen.lobbyButtonAt(at.px, at.py))
            hlrt::netGame.sendAllToLobby();
#endif
        touchWas = touch;
        if (hlrt::netGame.votePage())
        {
            if (voteShown == -1)
            {
                hlrt::bottomScreen.showVote();
                voteResultShown = false;
                voteShown = match.vote.stamp;
            }
            else if (match.vote.decided >= 0)
            {
                if (!voteResultShown)
                    hlrt::bottomScreen.showVoteResult();
                voteResultShown = true;
            }
            else
            {
                hlrt::bottomScreen.voteTouch(touch, at.px, at.py);
                if (voteShown != match.vote.stamp)
                {
                    hlrt::bottomScreen.voteUpdate();
                    voteShown = match.vote.stamp;
                }
                hlrt::bottomScreen.voteClock(hlrt::netGame.voteSecondsLeft());
            }
        }
        else if (match.over)
        {
            if (scoresShown != match.final.stamp)
            {
                scoresShown = match.final.stamp;
                hlrt::bottomScreen.showScores();
            }
        }
        else if (!voteWaitShown)
        {
            voteWaitShown = true;
            hlrt::bottomScreen.showMessage("NEXT MAP", "The vote is about to open");
        }
    }

    void drawScene()
    {
        // NEA leaves all four lights on and the modelview at identity. The
        // world is lit by its vertex colours: no lights.
        hlrt::worldDraw.applyFog();
        NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), hlrt::WorldDraw::kCull,
                       hlrt::worldDraw.polygonMode());
        hlrt::camera.apply();
        // (The match over: nothing in hand. It is back with the next map.)
        const bool held = hlrt::game.held.active;
        if (hlrt::netGame.playing() && hlrt::netGame.match().over)
            hlrt::game.held.active = false;
        hlrt::worldDraw.draw();
        hlrt::game.held.active = held;
        // (Nor the display: the map, and the scores below it.)
        if (hlrt::netGame.playing() && hlrt::netGame.match().over)
            hlrt::hudDraw.clear();
        else
            hlrt::hudDraw.draw();
    }

    enum class LoadResult
    {
        Ok,
        NotOnCard, ///< nothing was touched: the game goes on where it is
        Failed,    ///< the old map is gone and the new one is not there
    };

    /// Until what was last said on the touch screen gives way to the pad
    /// again (a blank count; 0: nothing is waiting to).
    std::uint32_t saidUntil = 0;
    void say(const char *line, const char *under = "")
    {
        hlrt::bottomScreen.showMessage(line, under);
        saidUntil = Global::vblanks() + 90;
    }

    /// A save the card would not take: said, with why (where the write
    /// stopped, and what the card said), and a line in perf.txt -- what a
    /// player can report.
    char notSaved[48] = "the card would not take it";
    void sayNotSaved(int slot)
    {
        const char *why = hlrt::SaveGame::whyFailed();
        std::snprintf(notSaved, sizeof(notSaved), "card refused: %s", why);
        say("COULD NOT SAVE", notSaved);
        if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
        {
            const struct mallinfo info = mallinfo();
            std::fprintf(log, "save failed: %s slot %d, %s, heap %lu\n", currentMap.c_str(), slot, why,
                         static_cast<unsigned long>(global.heapHeadroom() + info.fordblks));
            std::fclose(log);
        }
    }

    /// Load `name`, in place of whatever is loaded. With an `arrival` the
    /// player is put where it says; without, at the map's own start.
    ///
    /// The old map is let go BEFORE the new one is read: a DS has 4 MB and a
    /// map is a quarter of that, so two do not fit. That is why the one thing
    /// that can be checked first -- that the file is there and is a level of
    /// this version -- is.
    /// The game the map being left belongs to is over (a new game, a save
    /// loaded, the map begun again): the next loadLevel does not remember
    /// that map in the game's progress -- which is by then another game's.
    bool leavingAbandoned = false;

    /// Everything of the map being played let go: its sounds, its game, and
    /// what it has in video memory.
    void dropMap()
    {
        // Silence first: the sounds are the map's, and are played out of its
        // memory.
        hlrt::sound.unload();
        hlrt::sound.dropMatchWarning();
        hlrt::game.destroy();
        hlrt::worldDraw.destroy();
        hlrt::textureStream.end();
        hlrt::textureCache.destroy();
        hlrt::spriteCache.destroy();
        hlrt::modelCache.destroy();
        hlrt::effectDraw.dropSprays();
        // (All of the map gone: anything that wrote where it should not
        // while it was played is found now, by its block.)
        hlrt::guard::check("map left");
    }

    /// (Sent the game: the map's stream heard and answered between the
    /// load's long steps.)
    void hearStream()
    {
#ifdef HLDS_CHILD
        hlrt::guest::hear();
#endif
    }

#ifndef HLDS_CHILD
    bool saveOnArrival();
#endif

    LoadResult loadLevel(const std::string &name, const hldata::Arrival *arrival,
                         std::string &error)
    {
        hldata::LevelHeader header{};
        LoadClock clock;
        // (A save still being written is of the map being left: finished
        // first, at once.)
        hlrt::SaveGame::finish();
        if (!hlrt::Level::peekHeader(name, header))
        {
            leavingAbandoned = false;
            error = "Map not on the card:";
            return LoadResult::NotOnCard;
        }
        clock.mark("peek");
#ifdef HLDS_CHILD
        // (Sent the game: the map comes as it is read -- how far, as it does.)
        guestMapProgress(0);
#else
        if (!quietLoad)
            hlrt::bottomScreen.showMessage("LOADING", name.c_str());
#endif
        // The last picture of the map being left stays on the screen while
        // this one loads, if there is one (FrameFreeze). Otherwise nothing of
        // the old picture is seen from here on: the hardware would go on
        // drawing it out of video memory, which is about to be written over.
        const bool kept = hlrt::level.loaded() && !hlrt::screenFade.curtained() && hlrt::frameFreeze.freeze();
        if (!kept)
            hlrt::screenFade.curtainDown();

        // What is broken in the map being left is noted first: it is still
        // broken if the player comes back. (Not a map of a game given up.)
        // And by a way out of it, all of it is kept, as it is, to come back
        // to (HlMapStore.hpp) -- not in a game between consoles, whose maps
        // begin afresh.
        const bool changing = hlrt::level.loaded() && !leavingAbandoned && arrival != nullptr;
        const std::string left = currentMap;
        bool stored = false;
        if (hlrt::level.loaded())
        {
            if (!leavingAbandoned)
                hlrt::game.rememberMap(hldata::nameHash(currentMap.c_str()));
            if (changing && !hlrt::netGame.playing())
                stored = hlrt::SaveGame::leaveMap(currentMap, hlrt::level.view.header->crc32);
            logPerformance();
        }
        leavingAbandoned = false;
        hlrt::gpuStats.beginMap();
        dropMap();
        // With no picture kept: the game's loading picture, on the screen
        // and left there while the map is read (video memory is empty just
        // now: there is room). That is the first load, and little else.
        if (!kept && !quietLoad && hlrt::hudDraw.showLoading())
            hlrt::screenFade.curtainUp();

        clock.mark("down");
        // The map's archive in the place of the last one's: everything
        // from here to the end of the load is read out of it.
        if (!global.mountMap(name))
        {
            error = "Map not found on the card.";
            return LoadResult::Failed;
        }
        if (!hlrt::level.load(error))
            return LoadResult::Failed;
        clock.mark("level");
        // Textures before models: if video memory runs short it is a model
        // that goes without its skin, not a wall. Models before the game:
        // actors are set up from their models.
        hlrt::modelDraw.forgetPoses();
#ifdef HLDS_CHILD
        // (Sent the game, every texture is in video memory at once: the
        // weapon in hand has its slots first, or it is the one left bare.
        // A guest's weapons' skins are all of one size.)
        // (Not a map at full quality, which streams its textures as a card's
        // does -- from memory here -- and has its slots among them.)
        {
            bool streams = false;
            for (std::uint32_t t = 0; t < hlrt::level.view.textureCount; ++t)
                streams = streams || (hlrt::level.view.textures[t].flags & hldata::kTextureStreamed) != 0;
            if (!streams)
                hlrt::textureStream.reserveSkinSlots(hldata::kLazySkinTexels / 4);
        }
#endif
        bool made = hlrt::worldDraw.create() && hlrt::textureCache.create();
        clock.mark("tex");
        hearStream();
        made = made && hlrt::spriteCache.create();
        clock.mark("spr");
        made = made && hlrt::modelCache.create();
        if (made)
            hlrt::modelCache.loadCharacters();
        clock.mark("mdl");
        hearStream();
        // What video memory has left is for the textures that stream.
        hlrt::textureStream.begin();
        clock.mark("stream");
        hearStream();
        made = made && hlrt::game.create();
        hearStream();
        if (!made)
        {
            error = "Not enough memory for this map.";
            return LoadResult::Failed;
        }
        hlrt::worldDraw.setBrushStates(hlrt::game.brushStates());
        // Come back to by a way in: as it was left, if it was kept; else as
        // it begins, with what was broken in it broken. (Spoilt half-way by
        // the card: begun again.)
        const int laid = changing && !hlrt::netGame.playing()
                             ? hlrt::SaveGame::arriveMap(name, hlrt::level.view.header->crc32)
                             : 0;
        if (laid < 0)
        {
            hlrt::game.destroy();
            if (!hlrt::game.create())
            {
                error = "Not enough memory for this map.";
                return LoadResult::Failed;
            }
            hlrt::worldDraw.setBrushStates(hlrt::game.brushStates());
        }
        if (laid <= 0)
            hlrt::game.restoreMap(hldata::nameHash(name.c_str()));
#ifdef HLDS_PROFILE
        // (How the maps left are kept: stored as left, laid as come back to.)
        if (changing)
        {
            if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
            {
                std::fprintf(log, "kept: %s %s; %s %s (%d kept)\n", left.c_str(), stored ? "stored" : "not stored",
                             name.c_str(), laid > 0 ? "laid back" : (laid < 0 ? "spoilt" : "fresh"),
                             static_cast<int>(hlrt::SaveGame::runIndex().count));
                std::fclose(log);
            }
        }
#endif
        (void)stored;
        currentMap = name;
        hlrt::debugOverlay.setMapName(currentMap.c_str());
        hlrt::input.dropWeaponList();
        achievements.attach(name);
        if (changing)
        {
            achievements.travelled(left, name);
            achievements.flush();
        }

        // (What they carry of the later weapons comes along as the rest does:
        // it is not in the Arrival, which is as it always was.)
        const hldata::Arsenal carried = hlrt::player.arsenal;
        hlrt::player.spawn();
        if (arrival != nullptr)
        {
            // (Even with no landmark to put them by: what they carry is theirs.)
            hlrt::game.applyArrival(*arrival);
            hlrt::player.arsenal = carried;
            // Those who came along are not in the map they left, if it is
            // come back to.
            if (changing && !hlrt::netGame.playing())
                hlrt::SaveGame::landed(left, hlrt::game.placed());
        }
        clock.mark("game");
        hearStream();
#ifndef HLDS_CHILD
        // Every new map is somewhere to come back to. Written here, with the
        // picture still and no sound yet: play and the music begin once the
        // card has done.
        const bool arrivalSaved = !changing || hlrt::netGame.playing() || saveOnArrival();
        clock.mark("save");
#endif
        // Only a way out takes the music along. (A save puts back its own,
        // after this.)
        if (!changing)
            hlrt::sound.setMusic(-1, 0);
        hlrt::sound.load();
        clock.mark("snd");
        // Last, from what is left past what play takes: the streamer's copies
        // of the pictures that change often. Then what play takes from here
        // is measured.
        hlrt::textureStream.makeCopies(kPlayReserve);
        hlrt::guard::markPlay();
        clock.write(name.c_str());
        if (perfLogging())
            writeLoadRoom(name.c_str());
        if (hlrt::player.debugTrack >= 0)
        {
            hlrt::game.logic.musicRequest = hlrt::player.debugTrack;
            hlrt::player.debugTrack = -1;
        }
        if (!quietLoad && !holdLoad)
            hlrt::bottomScreen.showPad();
#ifndef HLDS_CHILD
        // (Said again: what was said while it was written has just gone.)
        if (!arrivalSaved)
            say("COULD NOT SAVE", notSaved);
#endif
        // Maps converted by an older converter still load, but without what
        // was fixed since in how a map is converted: said, once.
        static bool toldOld = false;
        if (header.version < hldata::kLevelVersion && !toldOld)
        {
            toldOld = true;
            say("OLD MAP DATA", "convert the game again for the latest fixes");
            saidUntil = Global::vblanks() + 300;
        }
        // Down again while the picture gives way to the map's first.
        hlrt::screenFade.curtainDown();
        hlrt::hudDraw.hideLoading();
        hlrt::frameFreeze.thaw();
        if (!quietLoad && !holdLoad)
            hlrt::screenFade.curtainUp(curtainFrames());
#ifdef HLDS_CHILD
        // (Sent the game: the map's stream has given its loaders all it had.
        // Nothing of it is read during play -- the weapons it kept, now
        // there, aside: the one in hand, if it could not be read before.)
        global.unmountMap();
        hlrt::modelCache.rewield();
        if (hlrt::netGame.demo() != 0)
            hlrt::guest::demoLoaded(hlrt::level.view);
#endif
        hlrt::guard::check("map loaded");
        return LoadResult::Ok;
    }

    /// A match's map loaded, on any console: the others told (a client says
    /// it is ready), and the word to begin waited for, behind the curtain,
    /// the radio heard all the while -- then the match begins on all of
    /// them at once. False if it is not to (the host gave it up, or is gone,
    /// or nothing has come for three minutes): the map is to be let go.
    bool waitForGo(const std::string &map)
    {
        // (The host: the map's picture, the first time, while the others
        // are still on their way.)
        if (hlrt::netGame.isHost())
            takePreview(map);
        hlrt::screenFade.curtainDown();
#ifdef HLDS_CHILD
        hlrt::bottomScreen.showProgress("JOINING THE MATCH", "Ready", "Waiting for the host to begin", 100, 100, false,
                                        true);
#else
        hlrt::bottomScreen.showMessage("WAITING", "for the others to load the map");
#endif
        // In DSi mode: the textures most views want asked for, for a few
        // seconds once the host answers (it may still be loading the map, or
        // sending who the others are, which is heard below all the while),
        // before it says it is ready: not to begin flat.
        bool said = !global.twlChild;
        int heard = 0;
        if (said)
            hlrt::netGame.loadedMap();
        for (int n = 0; n < 3 * 60 * 60; ++n)
        {
            swiWaitForVBlank();
            hlrt::sound.frame();
            hlrt::netGame.frame();
            if (!hlrt::netGame.on() || hlrt::netGame.cancelled())
                return false;
#ifdef HLDS_CHILD
            hlrt::textureStream.fetchTick();
            heard += hlrt::textureStream.fetchHeard() ? 1 : 0;
            if (!said && (hlrt::textureStream.prerolled() || heard >= 6 * 60 || n >= 15 * 60))
            {
                said = true;
                hlrt::netGame.loadedMap();
            }
            // Who another chose to be, on its way: taken, read, let go of.
            {
                hldata::AirHead air;
                if (hlrt::netGame.airAsked(air) &&
                    (air.stream == hldata::AirStream::Character || air.stream == hldata::AirStream::CharacterFull))
                {
                    hlrt::bottomScreen.showProgress("JOINING THE MATCH", "Ready", "Hearing who the others are", 100, 100, false, true);
                    if (hlrt::guest::receiveAir(nullptr))
                        hlrt::modelCache.loadCharacters();
                    hlrt::air::dropCharacters();
                    hlrt::bottomScreen.showProgress("JOINING THE MATCH", "Ready", "Waiting for the host to begin", 100, 100, false, true);
                }
            }
#endif
            // (The host may give it up from here too: B.)
            scanKeys();
            if (hlrt::netGame.isHost() && (keysDown() & KEY_B))
            {
                hlrt::netGame.abortMatch();
                hlrt::netGame.toLobbyFromLoad();
                return false;
            }
            if (hlrt::netGame.goAsked())
            {
                hlrt::netGame.mapLoaded();
                hlrt::bottomScreen.showPad();
                hlrt::screenFade.curtainUp(curtainFrames());
                return true;
            }
        }
        hlrt::netGame.leave();
        return false;
    }
#ifdef HLDS_CHILD
    /// A demo's map loaded: its first textures got, behind the curtain, not to
    /// begin flat.
    void demoReady()
    {
        hlrt::bottomScreen.showProgress(hldata::netDemoTitle(hlrt::netGame.demo()), "Ready", "Getting the textures", 100, 100, false, true);
        int heard = 0;
        for (int n = 0; n < 15 * 60 && hlrt::netGame.on(); ++n)
        {
            swiWaitForVBlank();
            hlrt::sound.frame();
            hlrt::netGame.frame();
            hlrt::textureStream.fetchTick();
            heard += hlrt::textureStream.fetchHeard() ? 1 : 0;
            if (hlrt::textureStream.prerolled() || heard >= 6 * 60)
                break;
        }
        hlrt::bottomScreen.showPad();
        hlrt::screenFade.curtainUp(curtainFrames());
    }
#endif

    /// HOST, on a card with multiplayer maps never photographed: each loaded
    /// in turn, behind the curtain, for its picture (takePreview), and let
    /// go -- what the lobby's map card shows, here and to the others. STOP
    /// on the touch screen (or B) ends it after the map being taken.
    void takeAllPreviews(const std::vector<std::string> &maps)
    {
        std::vector<std::string> todo;
        for (const std::string &map : maps)
        {
            if (!hlrt::mapPictureCurrent(map))
                todo.push_back(map);
        }
        std::string error;
        quietLoad = true;
        hlrt::screenFade.curtainDown();
        const int total = static_cast<int>(todo.size());
        for (int n = 0; n < total; ++n)
        {
            char step[64];
            std::snprintf(step, sizeof(step), "Taking %s", todo[n].c_str());
            hlrt::bottomScreen.showProgress("MAP PICTURES", step, "Taken once, for the lobby's map cards", n, total);
            hlrt::game.resetProgress();
            leavingAbandoned = true;
            if (loadLevel(todo[n], nullptr, error) == LoadResult::Ok)
                takePreview(todo[n]);
            else
            {
                // (Said, for a moment: a map this console cannot hold has no
                // picture -- it is asked again the next time.)
                hlrt::bottomScreen.showProgress("MAP PICTURES", todo[n].c_str(), error.c_str(), n + 1, total);
                for (int wait = 0; wait < 60 && !hlrt::BottomScreen::stopTouched(); ++wait)
                    swiWaitForVBlank();
            }
            if (hlrt::level.loaded())
            {
                dropMap();
                hlrt::level.unload();
            }
            // (STOP, or B: those taken are kept, the rest are for next time.)
            scanKeys();
            if (hlrt::BottomScreen::stopTouched() || (keysHeld() & KEY_B))
                break;
        }
        hlrt::BottomScreen::stopDone();
        quietLoad = false;
        leavingAbandoned = false;
        hlrt::game.resetProgress();
    }

    /// The game as it is, into `slot` (a kSlot*). Said on the screen either
    /// way: that it was saved, or that the card would not take it. The
    /// game's own save (not `quiet`) is only begun here, and goes to the card
    /// a little every frame (stepSave) with a small sign in the corner of the
    /// top screen: the game goes on meanwhile. It is never said in the middle
    /// of the screen, even when it has to be written at once.
    bool saveNow(int slot, bool quiet = false)
    {
#ifdef HLDS_CHILD
        if (hlrt::netGame.demo() == 0)
            return false; // (nowhere to keep it)
#endif
        if (!hlrt::level.loaded() || hlrt::player.noclip || hlrt::player.state.health <= 0)
            return false;
        hldata::SaveHeading heading = hlrt::game.heading(currentMap);
        // (When, by the console's clock, if it has the time.)
        const std::time_t now = std::time(nullptr);
        const std::tm *when = std::localtime(&now);
        if (when != nullptr && when->tm_year + 1900 >= 2005)
        {
            heading.year = static_cast<std::uint16_t>(when->tm_year + 1900);
            heading.month = static_cast<std::uint8_t>(when->tm_mon + 1);
            heading.day = static_cast<std::uint8_t>(when->tm_mday);
            heading.hour = static_cast<std::uint8_t>(when->tm_hour);
            heading.minute = static_cast<std::uint8_t>(when->tm_min);
        }
        if (slot == hldata::kSlotAuto && !quiet)
        {
            if (hlrt::SaveGame::begin(hlrt::game.snapshot(currentMap), slot, heading))
            {
                hlrt::topText.notice("saving...");
                return true;
            }
            // (One is being written still: this one waits for nothing, it is
            // written at once.)
        }
        const bool saved = hlrt::SaveGame::write(hlrt::game.snapshot(currentMap), slot, heading);
#ifdef HLDS_CHILD
        // (The host may be out of reach: nothing the player can do about it.)
        if (!saved)
            return false;
#endif
        if (!saved)
        {
            sayNotSaved(slot);
        }
        else if (slot == hldata::kSlotAuto)
        {
            // The game's own save is never said aloud: only ever the small
            // sign in the corner, however it was written.
            if (!quiet)
            {
                hlrt::topText.notice("saved");
                hlrt::topText.endNotice(1000);
            }
        }
        else if (!quiet)
        {
            say("SAVED");
        }
        return saved;
    }
    /// The game's own save, unless the player has turned it off. (Not for a
    /// demo a console was sent: its host keeps it, and it is how it goes on.)
    bool autosaving()
    {
        return settings.autosave || hlrt::netGame.demo() != 0;
    }
#ifndef HLDS_CHILD
    /// The maps left have changed (a way out, a new game, a load), and the
    /// game's own save only names them: made again, or, turned off, let go
    /// of, rather than listed with the maps gone from it.
    bool autosaveAgain()
    {
        if (autosaving())
            return saveNow(hldata::kSlotAuto, true);
        hlrt::SaveGame::dropNamedAuto();
        return true;
    }
    /// The saves of a map just come into, at once.
    bool saveOnArrival()
    {
        // (What saveNow would not save, for reasons of its own, is no failure.)
        if (hlrt::player.noclip || hlrt::player.state.health <= 0)
            return true;
        const bool arrival = saveNow(hldata::kSlotArrival, true);
        return autosaveAgain() && arrival;
    }
#endif
    /// Once a frame: a step of the save being written, if one is.
    void stepSave()
    {
        if (!hlrt::SaveGame::busy())
            return;
        switch (hlrt::SaveGame::step())
        {
        case hldata::SaveWriter::Result::Busy:
            break;
        case hldata::SaveWriter::Result::Done:
            hlrt::topText.notice("saved");
            hlrt::topText.endNotice(1000);
            break;
        case hldata::SaveWriter::Result::Failed:
            hlrt::topText.endNotice(0);
            sayNotSaved(hldata::kSlotAuto);
            break;
        }
    }

    /// What a death goes back to, and when the game last went back.
    hldata::DeathLoop deathLoop;
    std::uint32_t wentBack = 0;

    /// Go to where the save in `slot` says: its map begun, and the whole of
    /// its state laid over it. False -- with nothing changed -- if there is
    /// none, or its map is not on the card.
    bool loadSave(std::string &error, int slot)
    {
        static hldata::SaveData save; // not on the stack: it holds vectors of vectors
        save = hldata::SaveData();
        hldata::LevelHeader header{};
        if (slot < 0 || !hlrt::SaveGame::read(save, slot) ||
            !hlrt::Level::peekHeader(save.map, header))
            return false;
        hlrt::game.adoptProgress(save);
        // (The maps left on the way, as the save had them.)
        const bool folderChanged = hlrt::SaveGame::fromSave(slot, save, Global::vblanks() * 2654435761u + 1u);
        leavingAbandoned = true;
        const std::string map = save.map; // loadLevel changes currentMap, which this may be
        // (Whoever came into that map with the player comes into it again.)
        static hldata::Arrival came;
        const bool arrived = hlrt::Game::arrivalOf(save, came);
        if (loadLevel(map, arrived ? &came : nullptr, error) != LoadResult::Ok)
            haltWith("Cannot go on:", error.c_str(), map.c_str());
        hlrt::game.placeFromSave(save);
        // Every door, trigger and actor as it was -- or, from a save that
        // has not that (or not for this map as it is cooked now), the map
        // as it begins with the player put back.
        if (!hlrt::game.layState(save))
            say("LOADED", "the map from its beginning");
        save = hldata::SaveData(); // (tens of kilobytes: not kept)
#ifndef HLDS_CHILD
        // (The saves that only name the folder named the one that was there.)
        if (folderChanged && !hlrt::netGame.playing())
            saveOnArrival();
#else
        (void)folderChanged;
#endif
        return true;
    }
    /// The most recent save of those the player is shown; -1: none.
    int latestSave()
    {
#ifdef HLDS_CHILD
        return hlrt::netGame.demo() != 0 ? hldata::kSlotAuto : -1;
#else
        return hldata::saveMostRecent(hlrt::SaveGame::index());
#endif
    }

    /// The COMMAND line: a line typed on the pause menu's keyboard, read as
    /// the game's own console reads it (shared/HlCommands.hpp), and done.
    /// Returns what there is to say about it (shown where the pad is, until
    /// the next touch), or an empty string.
    std::string runCommand(const std::string &typed)
    {
        using hldata::Command;
        const hldata::CommandLine line = hldata::parseCommand(typed.c_str());
        hldata::Cheats &cheats = hlrt::game.cheats;
        hldata::PlayerState &state = hlrt::player.state;
        char said[64];
        auto number = [&](int arg, int &out) { return line.argc > arg && hldata::parseNumber(line.args[arg], out); };
        auto onOff = [](bool on, const char *what)
        {
            static char text[48];
            std::snprintf(text, sizeof(text), "%s %s", what, on ? "on" : "off");
            return std::string(text);
        };
        // (Not in a game between consoles: nobody there may cheat.)
        if (hlrt::netGame.playing())
            return "not in a game between consoles";
        // What the game would only let through with sv_cheats on.
        switch (line.command)
        {
        case Command::GiveAll:
        case Command::Give:
        case Command::Heal:
        case Command::Health:
        case Command::God:
        case Command::Notarget:
        case Command::Noclip:
        case Command::BarnacleKill:
        case Command::KillAll:
        case Command::Gravity:
        case Command::Timescale:
            achievements.cheated();
            break;
        default:
            break;
        }
        switch (line.command)
        {
        case Command::Help:
        {
            // Every name, the console wraps them.
            std::string text = "commands (<command> help: how):\n";
            for (int i = 0; i < hldata::kCommandNames; ++i)
                text += std::string(i > 0 ? " " : "") + hldata::kCommands[i].name;
            return text;
        }
        case Command::Usage:
        {
            const hldata::CommandInfo *info = hldata::commandInfo(line.args[0]);
            if (info == nullptr)
                return std::string("unknown: ") + line.args[0] + " (try help)";
            return std::string("usage: ") + info->usage + "\n  " + info->about;
        }
        case Command::Map:
        {
            const std::string name = line.args[0];
            if (name.empty())
                return "map <name>";
            // A map's name becomes a file's: letters, digits and underscores.
            for (const char ch : name)
            {
                if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'))
                    return "not a map's name";
            }
            std::string error;
            switch (loadLevel(name, nullptr, error))
            {
            case LoadResult::Ok:
                return "";
            case LoadResult::NotOnCard:
                return "no such map on the card";
            case LoadResult::Failed:
                haltWith("Cannot go on:", error.c_str(), name.c_str());
            }
            return "";
        }
        case Command::Restart:
        {
            const std::string again = currentMap;
            std::string error;
            // (From its start: not as it is now.)
            leavingAbandoned = true;
            if (loadLevel(again, nullptr, error) == LoadResult::Failed)
                haltWith("Cannot go on:", error.c_str(), again.c_str());
            return "";
        }
        case Command::GiveAll:
        {
            hldata::Inventory &inv = state.inv;
            inv.have = static_cast<std::uint8_t>((1u << hldata::kWeaponFirstMore) - 1u);
            if (inv.current == hldata::kWeaponNone)
                inv.current = hldata::kWeaponCrowbar;
            inv.clipGlock = 17;
            inv.clipShotgun = 8;
            inv.ammo9mm = 250;
            inv.ammoBuck = 125;
            inv.grenades = 10;
            inv.clipMp5 = 50;
            inv.clipPython = 6;
            inv.ammo357 = 36;
            inv.ammoAr = 10;
            inv.armor = 100;
            // ... and the later weapons, with all they hold.
            for (int w = hldata::kWeaponFirstMore; w < hldata::kWeaponCount; ++w)
                hldata::giveWeapon(state, w);
            hldata::Arsenal &more = hlrt::player.arsenal;
            more.boltsClip = 5;
            more.bolts = 50;
            more.satchels = 5;
            more.tripmines = 5;
            more.snarks = 15;
            more.have = static_cast<std::uint16_t>(more.have | hldata::kArsenalRpgLoaded);
            more.rockets = 5;
            more.uranium = 100;
            more.hornets = 8;
            return "given";
        }
        case Command::Give:
        {
            // One thing, as picking it up would give it.
            const hldata::ItemKind item = hldata::itemOfName(line.args[0]);
            if (item == hldata::ItemKind::None)
                return "no such thing to give";
            std::uint32_t got = 0;
            if (!hldata::detail::itemTake(state, static_cast<std::uint8_t>(item), got,
                                          hldata::skillData(hlrt::game.globals.get(hldata::kGlobalSkill))))
                return "no use to you now";
            return std::string("given ") + line.args[0];
        }
        case Command::Heal:
            state.health = 100;
            return "healed";
        case Command::Health:
        {
            int value = 0;
            if (!number(0, value) || (value >> 8) < 1)
                return "health <1..999>";
            state.health = (value >> 8) > 999 ? 999 : (value >> 8);
            std::snprintf(said, sizeof(said), "health %d", static_cast<int>(state.health));
            return said;
        }
        case Command::Kill:
            // (Whatever god says.)
            state.health = 0;
            return "";
        case Command::God:
            cheats.god = !cheats.god;
            return onOff(cheats.god, "god mode");
        case Command::Notarget:
            cheats.notarget = !cheats.notarget;
            return onOff(cheats.notarget, "notarget");
        case Command::Noclip:
            hlrt::player.noclip = !hlrt::player.noclip;
            return hlrt::player.noclip ? "noclip on: flying" : "noclip off: walking";
        case Command::BarnacleKill:
            cheats.barnacleKill = !cheats.barnacleKill;
            return onOff(cheats.barnacleKill, "barnacles die when they grab");
        case Command::KillAll:
        {
            // A kind of monster; or "monsters", every one that fights.
            int killed = 0;
            if (std::strcmp(line.args[0], "monsters") == 0 || std::strcmp(line.args[0], "all") == 0)
            {
                for (int kind = 0; kind < static_cast<int>(hldata::ActorKind::Count); ++kind)
                {
                    if (hldata::fights(static_cast<std::uint8_t>(kind)) &&
                        kind != static_cast<int>(hldata::ActorKind::Player))
                        killed += hlrt::gameApi.killAllOfKind(hlrt::game.logic, static_cast<hldata::ActorKind>(kind));
                }
            }
            else
            {
                const hldata::ActorKind kind = hldata::kindOfName(line.args[0]);
                if (kind == hldata::ActorKind::Count)
                    return "killall <monster> (or monsters)";
                killed = hlrt::gameApi.killAllOfKind(hlrt::game.logic, kind);
            }
            std::snprintf(said, sizeof(said), "%d killed", killed);
            return said;
        }
        case Command::Save:
            return saveNow(hldata::kSlotQuick) ? "saved" : "cannot save now";
        case Command::Load:
        {
            std::string error;
            return loadSave(error, hldata::kSlotQuick) ? "" : "no quick save";
        }
        case Command::Spray:
            if (!hlrt::netGame.playing())
                return "spraying is for a match";
            return hlrt::netGame.spray() ? "" : "no wall near, or too soon";
        case Command::Flashlight:
            if ((state.inv.have & hldata::kHaveSuit) == 0)
                return "no suit, no flashlight";
            if (hlrt::game.logic.lampBanned.value)
                return "no flashlight in this match";
            hlrt::game.lamp.on = !hlrt::game.lamp.on;
            return onOff(hlrt::game.lamp.on, "flashlight");
        case Command::Fov:
        {
            int value = 0;
            if (!number(0, value))
            {
                std::snprintf(said, sizeof(said), "fov is %d (60..110)", hlrt::camera.fovAcross);
                return said;
            }
            hlrt::camera.setFov(value >> 8);
            std::snprintf(said, sizeof(said), "fov %d", hlrt::camera.fovAcross);
            return said;
        }
        case Command::Crosshair:
        {
            int value = 0;
            hlrt::game.crosshair = number(0, value) ? value != 0 : !hlrt::game.crosshair;
            return onOff(hlrt::game.crosshair, "crosshair");
        }
        case Command::Gravity:
        {
            int value = 0;
            if (!number(0, value))
            {
                std::snprintf(said, sizeof(said), "sv_gravity is %d", hldata::fxToInt(cheats.gravity));
                return said;
            }
            value >>= 8;
            value = value < 0 ? 0 : (value > 2000 ? 2000 : value);
            cheats.gravity = hldata::fxFromInt(value);
            std::snprintf(said, sizeof(said), "sv_gravity %d", value);
            return said;
        }
        case Command::Timescale:
        {
            int value = 0;
            if (!number(0, value))
                return "host_timescale <0.25..2>";
            cheats.timescale = value < 64 ? 64 : (value > 512 ? 512 : value);
            std::snprintf(said, sizeof(said), "host_timescale %d.%02d", cheats.timescale >> 8,
                          ((cheats.timescale & 255) * 100 + 128) >> 8);
            return said;
        }
        default:
            break;
        }
        return line.verb[0] == '\0' ? std::string() : std::string("unknown: ") + line.verb + " (try help)";
    }

    /// A line typed at the console: run, and what it said. What takes the
    /// player somewhere else (another map, a save, the start again, death)
    /// closes the console once done.
    hlrt::Menu::ConsoleReply consoleLine(const std::string &typed)
    {
        using hldata::Command;
        hlrt::Menu::ConsoleReply reply;
        reply.text = runCommand(typed);
        const Command command = hldata::parseCommand(typed.c_str()).command;
        reply.close = reply.text.empty() && (command == Command::Map || command == Command::Restart ||
                                             command == Command::Load || command == Command::Kill);
        return reply;
    }

    /// How hard a game begun from debug_start.txt is: its tenth number (1
    /// easy, 2 medium, 3 hard), MEDIUM if it has none.
    int debugSkill = hldata::kSkillMedium;

    /// A fade to look at (tools/emu's EMU_FADE: a line "fade r g b amount
    /// duration-ms hold-ms from after-ms" in debug_start.txt), put on the
    /// screen as an env_fade would be, `afterMs` into the game. -1: none.
    struct DebugFade
    {
        int afterMs = -1;
        int colour = 0, amount = 255, durationMs = 1000, holdMs = 0, from = 1;
    } debugFade;
    std::uint32_t debugFadeStart = 0;
    void debugFadeTick()
    {
        if (debugFade.afterMs < 0)
            return;
        if (debugFadeStart == 0)
            debugFadeStart = Global::vblanks();
        if (static_cast<int>(Global::vblanks() - debugFadeStart) * 1000 / 60 < debugFade.afterMs)
            return;
        // (The fields the env_fade sets: shared/HlEffects.hpp.)
        hldata::LogicWorld &w = hlrt::game.logic;
        w.fadeFrom = debugFade.from != 0;
        w.fadeMs = 0;
        w.fadeWholeMs = debugFade.durationMs;
        w.fadeHoldMs = debugFade.holdMs;
        w.fadeDark = static_cast<std::uint8_t>(debugFade.amount);
        w.fadeColour.value = static_cast<std::uint16_t>(debugFade.colour);
        w.fadeLevel = w.fadeFrom ? w.fadeDark : 0;
        w.fading = true;
        debugFade.afterMs = -1;
    }

    /// A development aid (tools/emu): a card with debug_start.txt on it starts
    /// where that says, save or no save.
    bool debugStart()
    {
        std::FILE *file = std::fopen(global.dataPath("debug_start.txt").c_str(), "r");
        if (file == nullptr)
            return false;
        // (The readout is on: whoever starts this way wants to see it.)
        settings.debugReadout = true;
        // (The sixth number, the seconds until the player dies, is read by
        // Player. The seventh is how many models may be drawn at once.)
        // (The eighth and ninth, a weapon and god mode, are Player's too.
        // The tenth is how hard the game is.)
        int n[10] = {0, 0, 0, 0, -1, 0, 0, 0, 0, hldata::kSkillMedium};
        const int given = std::fscanf(file, "%d %d %d %d %d %d %d %d %d %d", &n[0], &n[1], &n[2], &n[3], &n[4], &n[5],
                                      &n[6], &n[7], &n[8], &n[9]);
        if (given >= 7 && n[6] > 0)
            hlrt::modelDraw.setLimit(n[6]);
        if (given == 10 && n[9] >= hldata::kSkillEasy && n[9] <= hldata::kSkillHard)
            debugSkill = n[9];
        int f[8];
        if (std::fscanf(file, " fade %d %d %d %d %d %d %d %d", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6],
                        &f[7]) == 8)
        {
            debugFade.colour = (f[0] >> 3) | (f[1] >> 3) << 5 | (f[2] >> 3) << 10;
            debugFade.amount = f[3] < 0 ? 0 : (f[3] > 255 ? 255 : f[3]);
            debugFade.durationMs = f[4];
            debugFade.holdMs = f[5];
            debugFade.from = f[6];
            debugFade.afterMs = f[7];
        }
        std::fclose(file);
        return true;
    }
}

int main(int argc, char *argv[])
{

    // A crash says where: the build, the address, the registers, the code
    // on the stack, and whether the heap, the library and the stack are
    // whole (Guard.hpp). A photograph of it, and build/elf/<build>/, say
    // which line.
    hlrt::guard::install();
    global.Init(argc > 0 ? argv[0] : nullptr);
    // The game library, first: before anything else takes the heap, so that
    // it sits next to the program (GameDll.arm.cpp).
    const char *gameWhy = nullptr;
    const bool gameOk = hlrt::loadGame(&gameWhy);
    // (Before the first load, which is timed too.)
    hlrt::DebugLog::Perf::begin();
    hlrt::DebugLog::Perf::markStack();
    hlrt::screenFade.curtainDown();
    // How many models at once: what there is time for. Measured: with five
    // soldiers in sight a DS's frame is already 15 ms of its 16.7, each
    // model costing it about a millisecond; a DSi's is 10. In the game's
    // maps more than eight are almost never in sight together anyway.
    hlrt::modelDraw.setLimit(isDSiMode() ? 12 : 8);

    // The manifest says what the converter put on the card and which map to start in.
    std::string startMap;
    std::vector<hldata::NetMapInfo> multiplayerMaps;
    std::string dataWhy;
    const Global::DataStatus status = global.ReadManifest(startMap, &multiplayerMaps, &dataWhy, &global.dataVersion);
    hlrt::netGame.setMaps(multiplayerMaps);

    settings.load();
    achievements.load();
    achievements.started();
    const bool debugging = debugStart();

    global.InitVideo();
#ifdef HLDS_CHILD
    // Sent the game: nothing of it but the program, until its host is found
    // and has sent the rest.
    guestArrive(true);
#endif

    if (status != Global::DataStatus::Ok)
    {
        // (What did not match, if it is known, before what to do about it.)
        // (Without a card, what the console was like: which way it was
        // started is what tells a loader that leaves the card out.)
        if (status == Global::DataStatus::NoCard)
            dataWhy = global.cardWhy;
        const std::string advice = (dataWhy.empty() ? std::string() : dataWhy + " ") + Global::DataStatusHint(status);
        haltWith("Cannot start:", Global::DataStatusTitle(status), advice.c_str());
    }
    if (!gameOk)
        haltWith("Cannot start:", "The game (dsl/hl.dsl) cannot be loaded.", gameWhy);

    hlrt::bottomScreen.create();
    hlrt::hudDraw.load();
    // (Before any map: a map's sounds are handed to it as they are read.)
    hlrt::sound.init();
#ifdef HLDS_CHILD
    // Sent the game, what the common stream brought is read now, by all that
    // reads it, and let go: none of it stays beside the map.
    hlrt::bottomScreen.loadIcons();
    hlrt::topText.loadTitle();
    hlrt::guest::dropCommon();
#endif
#ifdef HLDS_CHILD_VRAMSOUND
    // (A test of video memory given to the ARM7 holding a sound: Sound.hpp.)
    hlrt::sound.vramTest(10);
#endif
    std::string error;
#ifndef HLDS_CHILD
    hlrt::menu.setSaves(&hlrt::SaveGame::index());
#endif
    // (QUIT only where there is somewhere to go back to: the loader said
    // where the game was started from.)
    hlrt::menu.setQuittable(argc > 0 && argv[0] != nullptr && argv[0][0] != '\0');
    // The title: go on with the last save, load another, begin -- or a game
    // between consoles, which begins when its host says on which map. True
    // if it loaded something; false for a new game.
    auto titleScreen = [&]() -> bool
    {
        // The game's menu background with its animated title (or, on a card
        // without them, the loading picture, still).
        bool moving = hlrt::hudDraw.showMenu();
        if (moving)
        {
            NEA_WaitForVBL(static_cast<NEA_UpdateFlags>(0));
            hlrt::hudDraw.menuFrame();
            hlrt::screenFade.curtainUp();
        }
        else if (hlrt::hudDraw.showLoading())
        {
            hlrt::screenFade.curtainUp();
        }
        // (Before anything is loaded, the picture is kept and its textures go.)
        auto leaving = [&]() { hlrt::hudDraw.hideMenu(); };
        auto back = [&]() { moving = hlrt::hudDraw.showMenu(); };
        // (A match over: its lobby, not the title.)
        if (hlrt::netGame.on())
            hlrt::menu.openLobby();
        else
#ifdef HLDS_CHILD
            hlrt::menu.openJoin(); // (out of the game: there is no title here)
#else
            hlrt::menu.openTitle();
#endif
#ifdef HLDS_CHILD
        // (Sent the game, it has no card to keep this on: asked as it comes.)
        {
            static bool handAsked = false;
            if (!handAsked)
                hlrt::menu.openHand();
            handAsked = true;
        }
        // (Sent the game: the host told what a map may take of it, for the
        // maps it offers -- a DSi's room is not a DS's.)
        guestSayRoom();
        bool saidRoom = true;
#endif
        while (true)
        {
            if (moving)
            {
                NEA_WaitForVBL(static_cast<NEA_UpdateFlags>(0));
                hlrt::hudDraw.menuFrame();
            }
            else
            {
                swiWaitForVBlank();
            }
            hlrt::input.poll(false);
            hlrt::sound.frame();
            hlrt::netGame.frame();
#ifdef HLDS_CHILD
            // Sent the game: out of the host's game (left, or the host
            // gone), a host again -- there is nothing else here.
            if (!hlrt::netGame.on() || hlrt::menu.onJoin())
            {
                saidRoom = false;
                // (Which hand is answered first: it then goes there itself.)
                if (!hlrt::netGame.on() && !hlrt::menu.onJoin() && !hlrt::menu.onHand())
                    hlrt::menu.openJoin();
            }
            else if (!saidRoom && hlrt::netGame.phase() == hlrt::NetGame::Phase::Lobby)
            {
                saidRoom = true;
                guestSayRoom();
            }
            // A demo: once which hand is answered, its base, then its first
            // map, as it comes -- no waiting for anyone to start it.
            const int demo = hlrt::netGame.demo();
            if (demo != 0 && global.twlChild && !hlrt::menu.onHand() && hlrt::netGame.on() &&
                hlrt::netGame.phase() == hlrt::NetGame::Phase::Lobby)
            {
                // (Who it is told to the host; back from where it was left, or
                // from the start, if the host holds its progress.)
                static std::uint32_t saidAt = 0;
                if (hlrt::netGame.demoKnown() < 0)
                {
                    if (Global::vblanks() - saidAt >= 120)
                    {
                        saidAt = Global::vblanks();
                        hlrt::netGame.demoHello(false);
                    }
                }
                else if (hlrt::netGame.demoChoice() == 0)
                {
                    if (hlrt::netGame.demoKnown() == 0)
                        hlrt::netGame.setDemoChoice(1);
                    else if (!hlrt::menu.onDemoChoice())
                        hlrt::menu.openDemoChoice();
                }
                if (hlrt::netGame.demoChoice() != 0)
                {
                    const bool again = hlrt::netGame.demoChoice() == 2;
                    const char *first = hldata::netDemoMaps(demo)[0];
                    leaving();
                    hlrt::netGame.forMapLoad();
                    bool started = hlrt::guest::demoBegin(hlrt::netGame.demoLongest(), first);
                    if (started)
                    {
                        hlrt::game.resetProgress();
                        hlrt::game.globals.set(hldata::kGlobalSkill, hldata::kSkillMedium);
                        hlrt::SaveGame::newRun(Global::vblanks() * 2654435761u + 1u);
                        leavingAbandoned = true;
                        hlrt::game.cheats = hldata::Cheats();
                        hlrt::game.crosshair = true;
                        guestMapName = first;
                        hlrt::guest::onMapProgress(guestMapProgress);
                        holdLoad = true;
                        started = again && loadSave(error, hldata::kSlotAuto);
                        if (!started)
                            started = loadLevel(first, nullptr, error) == LoadResult::Ok;
                        holdLoad = false;
                        // (A stream cut short loads as far as it goes.)
                        if (started && !hlrt::guest::demoMapHere())
                        {
                            started = false;
                            error = "The host has gone quiet";
                        }
                    }
                    else
                        error = "Not enough memory";
                    if (started)
                    {
                        demoReady();
                        saveNow(hldata::kSlotArrival, true);
                        saveNow(hldata::kSlotAuto, true);
                        break;
                    }
                    hlrt::bottomScreen.showMessage("CANNOT LOAD", error.c_str());
                    for (int n = 0; n < 180; ++n)
                        swiWaitForVBlank();
                    if (hlrt::level.loaded())
                    {
                        dropMap();
                        hlrt::level.unload();
                    }
                    back();
                    hlrt::netGame.leave();
                    hlrt::menu.openJoin();
                    continue;
                }
            }
            // A match about to begin: its group's base, and its map as it
            // comes -- then held, under the curtain, until the host starts it.
            hldata::AirHead air;
            if (hlrt::netGame.airAsked(air))
            {
                leaving();
                hlrt::netGame.forMapLoad();
                if (air.stream == hldata::AirStream::Base)
                {
                    guestBaseProgress(0);
                    if (!hlrt::guest::receiveAir(guestBaseProgress))
                    {
                        char why[48];
                        hlrt::guest::airStatus(why, sizeof(why));
                        hlrt::bottomScreen.showMessage("NOT RECEIVED", why);
                        for (int n = 0; n < 180; ++n)
                            swiWaitForVBlank();
                    }
                }
                else if (air.stream == hldata::AirStream::Map || air.stream == hldata::AirStream::MapFull)
                {
                    hlrt::game.resetProgress();
                    hlrt::game.globals.set(hldata::kGlobalSkill, air.skill);
                    hlrt::SaveGame::newRun(Global::vblanks() * 2654435761u + 1u);
                    leavingAbandoned = true;
                    hlrt::guest::offerAir(air.name);
                    guestMapName = air.name;
                    hlrt::guest::onMapProgress(guestMapProgress);
                    holdLoad = true;
                    const LoadResult loaded = loadLevel(air.name, nullptr, error);
                    holdLoad = false;
                    if (loaded == LoadResult::Ok)
                    {
                        if (waitForGo(air.name))
                            break;
                    }
                    else
                    {
                        // (Said, for a while: then the lobby, for the next.)
                        hlrt::bottomScreen.showMessage("CANNOT LOAD", error.c_str());
                        for (int n = 0; n < 180; ++n)
                            swiWaitForVBlank();
                    }
                    // (Not loaded, or the host gone meanwhile.)
                    if (hlrt::level.loaded())
                    {
                        dropMap();
                        hlrt::level.unload();
                    }
                }
                back();
                hlrt::menu.openLobby();
                continue;
            }
#endif
            // A vote before the first match has the touch screen; the lobby
            // again when it is over.
            {
                static bool voteWas = false;
                if (hlrt::netGame.voteOn())
                {
                    voteWas = true;
                    hlrt::mapCard.setHidden(true);
                    hlrt::profileView.setHidden(true);
                    endScreens();
                    continue;
                }
                if (voteWas)
                {
                    voteWas = false;
                    hlrt::netGame.voteLetGo();
                    endScreensReset();
                    if (hlrt::netGame.on())
                        hlrt::menu.openLobby();
                }
            }
            using hlrt::Menu;
            const Menu::Action item = hlrt::menu.frame();
#ifdef HLDS_CHILD
            if (item == Menu::Action::DemoContinue)
                hlrt::netGame.setDemoChoice(2);
            else if (item == Menu::Action::DemoRestart)
            {
                hlrt::netGame.demoHello(true);
                hlrt::netGame.setDemoChoice(1);
            }
#endif
            if (item == Menu::Action::Continue || item == Menu::Action::Load)
            {
                leaving();
                if (loadSave(error, hlrt::menu.slot()))
                    break;
                back();
                hlrt::menu.openTitle(); // (it could not be: the title again)
            }
            else if (item == Menu::Action::TakePreviews)
            {
                // (Before the host's game is opened: nobody waits on it.)
                leaving();
                takeAllPreviews(hlrt::netGame.maps());
                back();
                hlrt::mapCard.hide();
                hlrt::menu.openHost();
                hlrt::screenFade.curtainUp();
            }
            else if (item == Menu::Action::NewGame)
            {
                leaving();
                settings.save();
                return false;
            }
            else if (item == Menu::Action::Quit)
            {
                // Back to whatever started the game (TWiLight Menu++, the
                // console's own menu), the settings and the achievements kept.
                settings.save();
                achievements.flush();
                hlrt::screenFade.curtainDown();
                // (Nothing left running for what is started next: the sound's
                // channels and timers go on by themselves otherwise.)
                hlrt::netGame.leave();
                hlrt::sound.quit();
                std::exit(0);
            }
            // A match: its host has said which map (on the host's own
            // console as on the others).
            std::string map;
            int skill = hldata::kSkillMedium;
            if (hlrt::netGame.startAsked(map, skill))
            {
                leaving();
                hlrt::game.resetProgress();
                hlrt::game.globals.set(hldata::kGlobalSkill, static_cast<std::uint8_t>(skill));
                hlrt::SaveGame::newRun(Global::vblanks() * 2654435761u + 1u);
                leavingAbandoned = true;
                holdLoad = true;
                const LoadResult loaded = loadLevel(map, nullptr, error);
                holdLoad = false;
                if (loaded == LoadResult::Ok && waitForGo(map))
                    break;
                // (Given up: the map goes, and the lobby again -- or, out of
                // the game, the title.)
                if (hlrt::level.loaded())
                {
                    dropMap();
                    hlrt::level.unload();
                }
                back();
                if (loaded != LoadResult::Ok)
                    hlrt::netGame.leave();
                if (hlrt::netGame.on())
                    hlrt::menu.openLobby();
                else
                    hlrt::menu.openTitle();
            }
        }
        settings.save();
        return true;
    };
    // A new game: `chosen` on NEW GAME (its map and how hard, the menu's);
    // else the one the console starts with (the card's test start, if it
    // has one).
    auto newGame = [&](bool chosen)
    {
        const bool test = debugging && !chosen;
        hlrt::game.resetProgress();
        // How hard it is: the first thing the new game knows, before its
        // first map's monsters are made.
        hlrt::game.globals.set(hldata::kGlobalSkill, static_cast<std::uint8_t>(test ? debugSkill : hlrt::menu.skill()));
        hlrt::SaveGame::newRun(Global::vblanks() * 2654435761u + 1u);
        leavingAbandoned = true;
        // (A new game is played straight: the COMMAND line's cheats are off.)
        hlrt::game.cheats = hldata::Cheats();
        hlrt::game.crosshair = true;
        // Where: the game's own start, or the Hazard Course's, or Uplink's
        // (NEW GAME's choice).
        const std::string first = chosen && !hlrt::menu.map().empty() ? hlrt::menu.map() : startMap;
        if (loadLevel(first, nullptr, error) != LoadResult::Ok)
            haltWith("Cannot start:", error.c_str(), first.c_str());
        // (Somewhere to come back to from the first: the game's own save,
        // not one of the player's.)
        if (!test)
        {
#ifdef HLDS_CHILD
            saveNow(hldata::kSlotAuto, true);
            saveNow(hldata::kSlotArrival, true);
#else
            saveOnArrival();
#endif
        }
    };
    // At every start, as the game has it: the Valve intro (a button or the
    // stylus skips it). Not for a card set up to start somewhere for a test,
    // which goes straight there.
    if (!debugging)
        hlrt::hudDraw.playIntro();
    if (debugging)
        newGame(false);
    else if (!titleScreen())
        newGame(true);

#ifdef HLDS_PROFILE
    // (A build that measures also checks the hardware's arithmetic.)
    // (... and says how long the console took to get to the first map.)
    if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
    {
        std::fprintf(log, "boot: the first map was up %lu blanks after the start\n",
                     static_cast<unsigned long>(Global::vblanks()));
        std::fclose(log);
    }
    if (hlrt::hwMathCheck(4096) != 0)
        hlrt::bottomScreen.showMessage("HARDWARE MATHS", "does not agree: see perf.txt");
#endif
    std::uint32_t lastVblank = Global::vblanks();
    std::uint32_t lastPass = lastVblank;
    bool paused = false;
    // Out of a game between consoles, or QUIT TO MAIN MENU: the title again,
    // and whatever is chosen there.
    // The end of a demo, said (anywhere else, nothing).
    const auto demoClosing = []()
    {
#ifdef HLDS_CHILD
        if (hlrt::netGame.demo() == 0)
            return;
        hlrt::bottomScreen.showMessage("END OF THE DEMO", hldata::netDemoTitle(hlrt::netGame.demo()));
        hlrt::netGame.demoHello(true);
        for (int n = 0; n < 300; ++n)
        {
            swiWaitForVBlank();
            hlrt::netGame.frame();
        }
#endif
    };
    auto backToTitle = [&]()
    {
        hlrt::netGame.leave();
        paused = false;
        hlrt::sound.setPaused(false);
        hlrt::screenFade.curtainDown();
        // The map given up goes, all of it, before the title: its textures
        // fill video memory, and the title's animated logo would have no
        // room beside them (and came back still). Nothing of that game is
        // remembered: it is over.
        if (hlrt::level.loaded())
        {
            logPerformance();
            dropMap();
            hlrt::level.unload();
        }
        hlrt::hudDraw.clear();
        leavingAbandoned = false;
        if (!titleScreen())
            newGame(true);
        lastVblank = lastPass = Global::vblanks();
    };

    // ... or a match over: the map goes as it does for the title, the link
    // stays, and the lobby is where the title would be.
    auto backToLobby = [&]()
    {
        paused = false;
        hlrt::sound.setPaused(false);
        hlrt::screenFade.curtainDown();
        if (hlrt::level.loaded())
        {
            logPerformance();
            dropMap();
            hlrt::level.unload();
        }
        hlrt::netGame.toLobby();
        hlrt::hudDraw.clear();
        leavingAbandoned = false;
        if (!titleScreen())
            newGame(true);
        lastVblank = lastPass = Global::vblanks();
    };

    while (true)
    {
        // (The wait is when the actors' thinking is done: game/Think.cpp.)
        NEA_WaitForVBL(NEA_UPDATE_TASKS);
        // What was drawn off the screen last frame, put on it while it is
        // not being shown: the display's numbers, the sliding weapon list.
        hlrt::hudDraw.present();
        hlrt::bottomScreen.present();
        hlrt::guard::frame();
        Global::latchDrawClock();
        // What is to be written to video memory is queued (VramWriter writes
        // it in the vertical blanks), and what is written is drawn.
        hlrt::textureStream.upload();
        hlrt::modelCache.apply();
        stepSave();
        {
            const hlrt::DebugLog::Perf::Scope frame(hlrt::DebugLog::Section::Frame);

            // One step of the game: input, the world, the menu, and whatever
            // changes the map.
            const auto step = [&]()
            {
                {
                    const hlrt::DebugLog::Perf::Scope scope(hlrt::DebugLog::Section::Input);
                    // (The match over: the touch screen is the scores' and the
                    // vote's, and no weapon slot is touched or heard.)
                    hlrt::input.poll(!paused && !(hlrt::netGame.playing() && hlrt::netGame.match().over));
                }
                const std::uint32_t now = Global::vblanks();
                const int elapsed = static_cast<int>(now - lastVblank);
                lastVblank = now;

                // A game between consoles: what has come over the air; and
                // if this console is out of it without having asked to be
                // (the host left, the link went), the title.
                const bool inMatch = hlrt::netGame.playing();
                const bool inGame = inMatch || hlrt::netGame.demo() != 0;
                hlrt::netGame.frame();
                if (inGame && !hlrt::netGame.on())
                {
                    hlrt::bottomScreen.showMessage("OUT OF THE GAME", hlrt::netGame.ended());
                    for (int n = 0; n < 150; ++n)
                        swiWaitForVBlank();
                    backToTitle();
                    return;
                }
                // The match over and its scores shown: the host's lobby, for
                // the next.
                if (inMatch && hlrt::netGame.lobbyAsked())
                {
                    backToLobby();
                    return;
                }
                // The match over: the table, on the touch screen, as it
                // stands, until the lobby.
                {
                    if (paused || !inMatch || !hlrt::netGame.match().over)
                        endScreensReset();
                    else
                        endScreens();
                }
                // Who killed whom, who came and went: a line at the top of
                // the picture for a few seconds (the touch screen is the
                // player's controls, and stays so).
                {
                    char line[40];
                    if (hlrt::netGame.notice(line, sizeof(line)) && line[0] != 0)
                    {
                        hldata::MessageRecord said{};
                        said.x = -1;
                        said.y = 20;
                        said.effect = hldata::kMessageFade;
                        said.colour = 31 | 20 << 5 | 4 << 10; // (the suit's orange)
                        said.fadeInMs = 100;
                        said.holdMs = 2500;
                        said.fadeOutMs = 600;
                        hlrt::topText.show(said, line);
                    }
                }

                if (!paused)
                {
                    if (hlrt::input.startPressed || hlrt::input.menuPressed)
                    {
                        paused = true;
                        // (A game between consoles goes on, and is heard.)
                        if (!inMatch)
                            hlrt::sound.setPaused(true);
                        hlrt::input.dropWeaponList(); // (not to be drawn again stale after it)
                        hlrt::menu.open();
                    }
                    else
                    {
                        hlrt::bottomScreen.setPressed(hlrt::input.touchOn);
                        hlrt::bottomScreen.setWalking(hlrt::input.walk);
                        hlrt::bottomScreen.setLongJump((hlrt::player.arsenal.have & hldata::kArsenalLongJump) != 0);
                        hlrt::bottomScreen.setWeaponList(hlrt::input.weaponList());
                        // (The match over: nobody does anything more.)
                        if (inMatch && hlrt::netGame.match().over)
                            hlrt::input.quiet();
                        hlrt::game.frame(elapsed);
                        achievements.frame();
                        debugFadeTick();
                    }
                }
                else
                {
                    // (A game between consoles does not stand still for
                    // one console's menu: it goes on, with nothing held.)
                    const bool startPressed = hlrt::input.startPressed;
                    if (inMatch)
                    {
                        hlrt::input.quiet();
                        hlrt::game.frame(elapsed);
                        achievements.frame();
                        hlrt::input.startPressed = startPressed;
                    }
                    using hlrt::Menu;
                    const Menu::Action item = hlrt::menu.frame();
                    if (item == Menu::Action::Leave)
                    {
#ifdef HLDS_CHILD
                        // (A demo: where it was left, for when it is come back to.)
                        if (hlrt::netGame.demo() != 0 && saveNow(hldata::kSlotAuto, true))
                        {
                            hlrt::bottomScreen.showMessage("SAVING", "with the host");
                            hlrt::guest::demoSent();
                        }
#endif
                        backToTitle();
                        return;
                    }
                    if (item == Menu::Action::EndMatch)
                        hlrt::netGame.endMatch();
                    if (item == Menu::Action::Save || item == Menu::Action::SaveTo)
                    {
                        // The quick one, or the player's own that was
                        // picked (a new one, or one put in another's place).
                        hlrt::bottomScreen.showPad();
                        saveNow(item == Menu::Action::Save ? hldata::kSlotQuick : hlrt::menu.slot());
                        logPerformance();
                        hlrt::gpuStats.beginMap();
                        paused = false;
                        // (After the card has been written to: the sound
                        // goes on once nothing stands in its way.)
                        hlrt::sound.setPaused(false);
                    }
                    else if (item == Menu::Action::Load)
                    {
                        // (The sound stands still until it is done: what was
                        // playing is not heard again on the way.)
                        paused = false;
                        if (!loadSave(error, hlrt::menu.slot()))
                        {
                            hlrt::bottomScreen.showPad();
                            say("COULD NOT LOAD", "that save cannot be read");
                        }
                        hlrt::sound.setPaused(false);
                        deathLoop = hldata::DeathLoop();
                        lastVblank = Global::vblanks();
                    }
                    else if (item == Menu::Action::NewGame)
                    {
                        // (No save of the player's is touched: only the
                        // game's own, which are this game's from here.)
                        paused = false;
                        newGame(true);
                        hlrt::sound.setPaused(false);
                        deathLoop = hldata::DeathLoop();
                        lastVblank = Global::vblanks();
                    }
#ifndef HLDS_CHILD
                    // (Not on a console sent the game: no room for it there.)
                    else if (item == Menu::Action::Command)
                    {
                        // The console: lines typed and run, and what they
                        // said, until it is closed -- then straight back to
                        // the game. (The sound stands still with the game
                        // for as long as it is up, as on the menu's other
                        // pages; a map a line asks for is loaded with it
                        // still, and heard once the console is closed.)
                        hlrt::menu.console(consoleLine);
                        paused = false;
                        hlrt::sound.setPaused(false);
                        hlrt::bottomScreen.showPad();
                        lastVblank = Global::vblanks();
                    }
#endif
                    else if (item == Menu::Action::Resume || item == Menu::Action::EndMatch || hlrt::input.startPressed)
                    {
                        paused = false;
                        settings.save();
                        achievements.flush();
                        // (Once the card has been written to and the
                        // pictures are back: nothing then stands in the
                        // sound's way.)
                        hlrt::sound.setPaused(false);
                        hlrt::bottomScreen.showPad();
                    }
                    // However the menu was left for the game: the tap
                    // that left it picks no weapon.
                    if (!paused)
                        hlrt::input.holdTouch();
                }

                // Dead: back to the last save, or to the start of the map if
                // there is none.
                if (!paused && !hlrt::netGame.playing() && hlrt::player.state.health <= 0 &&
                    (hlrt::player.noclip || hlrt::player.view.deadMs >= hldata::kDeathWaitMs))
                {
#ifdef HLDS_PROFILE
                    if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
                    {
                        std::fprintf(log, "died: health %d at %d %d %d, %lu blanks in\n",
                                     static_cast<int>(hlrt::player.state.health),
                                     static_cast<int>(hldata::fxToInt(hlrt::player.state.origin[0])),
                                     static_cast<int>(hldata::fxToInt(hlrt::player.state.origin[1])),
                                     static_cast<int>(hldata::fxToInt(hlrt::player.state.origin[2])),
                                     static_cast<unsigned long>(Global::vblanks()));
                        std::fclose(log);
                    }
#endif
                    // (Not to a save that is itself the trouble: HlSave.hpp.)
                    const int back = hldata::deathChoice(
                        deathLoop, static_cast<int>((Global::vblanks() - wentBack) * 1000 / 60));
                    const bool went =
                        back == hldata::kBackStart
                            ? false
                            : (back == hldata::kBackArrival && loadSave(error, hldata::kSlotArrival)) ||
                                  loadSave(error, latestSave()) || loadSave(error, hldata::kSlotArrival);
                    if (!went)
                    {
                        const std::string again = currentMap;
                        if (loadLevel(again, nullptr, error) != LoadResult::Ok)
                            haltWith("Cannot go on:", error.c_str(), again.c_str());
                    }
                    lastVblank = wentBack = Global::vblanks();
                }
                // The game is lost (a player_loadsaved): its fade and words
                // have had their time, and back to the last save it goes --
                // or the start of the map, with none.
                if (hlrt::game.logic.reloadMs == 0)
                {
                    hlrt::game.logic.reloadMs = -1;
                    if (!hlrt::netGame.playing() && !loadSave(error, latestSave()) &&
                        !loadSave(error, hldata::kSlotArrival))
                    {
                        const std::string again = currentMap;
                        if (loadLevel(again, nullptr, error) != LoadResult::Ok)
                            haltWith("Cannot go on:", error.c_str(), again.c_str());
                    }
                    lastVblank = wentBack = Global::vblanks();
                }
                // The end of a part of the game (a trigger_endsection): the
                // title.
                if (hlrt::game.logic.endSection)
                {
                    hlrt::game.logic.endSection = false;
                    if (!hlrt::netGame.playing())
                    {
                        demoClosing();
                        backToTitle();
                        return;
                    }
                }
                // The map asked for a save (a trigger_autosave).
                if (hlrt::game.logic.saveRequested)
                {
                    hlrt::game.logic.saveRequested = false;
                    if (!hlrt::netGame.playing() && autosaving())
                        saveNow(hldata::kSlotAuto);
                }
                // (No way out of a map in a game between consoles.)
                if (hlrt::netGame.playing())
                    hlrt::game.logic.pendingChange = -1;
                // What was said on the touch screen has been read.
                if (saidUntil != 0 && !paused && Global::vblanks() > saidUntil)
                {
                    saidUntil = 0;
                    hlrt::bottomScreen.showPad();
                }

                // The way into the next map was taken this frame. What
                // crosses is packed first, while the old map can still say
                // where its landmark is.
                if (hlrt::game.pendingChange() >= 0)
                {
                    const int record = hlrt::game.pendingChange();
                    const hldata::Arrival arrival = hlrt::game.makeArrival();
                    const std::string next =
                        hlrt::level.view.strings + hlrt::level.view.logic[record].extra[0];
#ifdef HLDS_CHILD
                    // A demo's own way to go on: the textures, as at its start.
                    // Out of its maps: the end of it.
                    const bool inDemo = hlrt::netGame.demo() != 0;
                    if (inDemo && !hlrt::guest::offered(next))
                    {
                        hlrt::game.logic.pendingChange = -1;
                        demoClosing();
                        backToTitle();
                        return;
                    }
                    holdLoad = inDemo;
                    if (inDemo)
                        guestMapName = next;
#endif
                    const LoadResult changed = loadLevel(next, &arrival, error);
#ifdef HLDS_CHILD
                    holdLoad = false;
                    if (inDemo && changed == LoadResult::Ok)
                        demoReady();
#endif
                    switch (changed)
                    {
                    case LoadResult::Ok:
#ifdef HLDS_CHILD
                        // Every new map is somewhere to come back to, for a
                        // demo: with its host. (On a card: loadLevel, as the
                        // map comes up.)
                        saveNow(hldata::kSlotArrival, true);
                        saveNow(hldata::kSlotAuto);
#endif
                        break;
                    case LoadResult::NotOnCard:
                        // Not converted: say so, and do not ask again every
                        // frame the player stands in the doorway.
                        hlrt::game.logic.pendingChange = -1;
                        hlrt::game.logic.states[record].dead = true;
                        hlrt::bottomScreen.showMessage(error.c_str(), next.c_str());
                        break;
                    case LoadResult::Failed:
                        haltWith("Cannot go on:", error.c_str(), next.c_str());
                    }
                    lastVblank = Global::vblanks();
                }
            };

            step();
            holdIntermissionView();
            NEA_Process(drawScene);
            hlrt::sound.frame();
            // What is in view now says what to read for the next frames;
            // a few kilobytes a frame, more where the card is quicker.
            hlrt::textureStream.update(isDSiMode() ? 8192 : 4096);
            // What the hardware made of the pass before this one, and the
            // budget corrected by it.
            {
                const std::uint32_t now = Global::vblanks();
                const hlrt::WorldStats &stats = hlrt::worldDraw.stats();
                const int took = static_cast<int>(now - lastPass);
                hlrt::gpuStats.frame(stats.hwPolygons, stats.hwVertices, took);
                hlrt::gpuStats.view(stats.budgetHit, stats.brushCut, hlrt::worldDraw.pressure() != 0);
                hlrt::gpuStats.losses(hlrt::modelDraw.cutOf(0), hlrt::effectDraw.refused(), hlrt::modelDraw.seenEnemiesCut(),
                                      hlrt::modelDraw.hidden());
                hlrt::gpuStats.clipping(stats.polygons, stats.vertices, stats.hwPolygons, stats.hwVertices,
                                        stats.budgetHit, stats.farPolygons);
                hlrt::gpuStats.world(stats.polygons, stats.surfacesCut, stats.dropped, stats.brushesDropped, stats.brushesOverflow,
                                     static_cast<std::uint32_t>(global.heapHeadroom()));
                lastPass = now;
                // A fade the map has asked for: over the top screen, in its
                // colour.
                hlrt::screenFade.setFade(hlrt::game.logic.fadeLevel, hlrt::game.logic.fadeColour.value);
                hlrt::screenFade.tick();
                const hlrt::GpuStats::Frame &seen = hlrt::gpuStats.last();
                // (More than a second is a load, not a slow pass.)
                hlrt::worldDraw.govern(seen.overflow, seen.underflow, seen.lines,
                                       took > 1 && took < 60);
            }
        }
        hlrt::DebugLog::Perf::endFrame();
        hlrt::DebugLog::Perf::logEvery(currentMap.c_str(), 512);
#ifdef HLDS_PROFILE
        // (... and what the passes had room for, as often: a run that never
        // leaves its map is measured too.)
        static int passesLogged = 0;
        if (++passesLogged >= 512)
        {
            passesLogged = 0;
            logPerformance();
            hlrt::gpuStats.beginMap();
        }
#endif
        hlrt::debugOverlay.frame();
    }
}
