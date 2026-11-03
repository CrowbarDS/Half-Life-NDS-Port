#include "net/NetGame.hpp"
#include "render/DebugOverlay.hpp"

#include <cstdio>

#include "DebugLog.hpp"
#include "Global.hpp"
#include "net/GuestLoad.hpp"
#include "Settings.hpp"
#include "data/ModelCache.hpp"
#include "data/TextureCache.hpp"
#include "data/TextureStream.hpp"
#include "game/Game.hpp"
#include "game/Player.hpp"
#include "render/Camera.hpp"
#include "render/GpuStats.hpp"
#include "audio/Arm7Audio.hpp"
#include "audio/DspAudio.hpp"
#include "audio/Sound.hpp"
#include "render/BottomScreen.hpp"
#include "render/EffectDraw.hpp"
#include "render/ModelDraw.hpp"
#include "render/WorldDraw.hpp"

namespace hlrt
{
    DebugOverlay debugOverlay;

    namespace
    {
#ifdef HLDS_PROFILE
        constexpr int kRows = 10;
#else
        constexpr int kRows = 7;
#endif
    } // namespace

    void DebugOverlay::frame()
    {
        if (!settings.debugReadout || --countdown_ > 0)
            return;
        countdown_ = kInterval;

        const WorldStats &stats = worldDraw.stats();
        // Every row is padded to the screen's 32 columns so that a shorter
        // value leaves nothing of a longer one behind.
        char line[40];
        switch (row_)
        {
        case 0:
            std::snprintf(line, sizeof(line), "%-8s %-3s fps %2d cpu %3d%%", mapName_,
                          global.isDSi ? "DSi" : "DS",
                          static_cast<int>(NEA_GetFPS()), static_cast<int>(NEA_GetCPUPercent()));
            break;
        case 1:
            // CUT marks a frame where the walk ran out of budget and dropped
            // the far end of the view.
            // pN: the pressure the view is under, when it is (WorldDraw).
        {
            // (... and the level of detail each model drawn is at, nearest
            // first: 0 the finest its file has.)
            char levels[12];
            modelDraw.levelsText(levels, sizeof(levels));
            std::snprintf(line, sizeof(line), "leaf %4d surf %3d br %2d %s%.0d %s L%s", stats.leaf,
                          stats.surfaces, stats.brushes, worldDraw.pressure() ? "p" : "",
                          worldDraw.pressure(), stats.budgetHit ? "CUT" : (stats.brushCut ? "cutb" : ""), levels);
        }
            break;
        case 2:
            // "chosen/used": what the level's own counts said, then what the
            // geometry engine reports after clipping.
            // ... and "m": the textures and skins of this map that did not
            // load (none, unless video memory ran out).
            std::snprintf(line, sizeof(line), "poly %4d/%4d vert %4d/%4d m%d", stats.polygons,
                          stats.hwPolygons, stats.vertices, stats.hwVertices,
                          textureCache.missing() + modelCache.missing() + modelCache.bare());
            break;
        case 3:
            // When models in view were left out, or took more than their
            // share: how many of each kind -- A enemies, B people, C things
            // to pick up, D the rest -- and how many polygons over. (A is
            // never to be anything but 0: HlDrawPlan.hpp.) Otherwise where
            // the eye is.
            if (modelDraw.cutOf(0) + modelDraw.cutOf(1) + modelDraw.cutOf(2) + modelDraw.cutOf(3) + modelDraw.grown() != 0)
            {
                std::snprintf(line, sizeof(line), "cut A%d B%d C%d D%d +%d%s e%d", modelDraw.cutOf(0), modelDraw.cutOf(1),
                              modelDraw.cutOf(2), modelDraw.cutOf(3), modelDraw.grown(), modelDraw.strained() ? " full" : "",
                              effectDraw.refused());
                break;
            }
            // (... or the world's: surfaces and brush entities in view with
            // no room for them.)
            if (worldDraw.stats().surfacesCut + worldDraw.stats().brushesDropped != 0)
            {
                std::snprintf(line, sizeof(line), "cut s%d b%d%s", worldDraw.stats().surfacesCut,
                              worldDraw.stats().brushesDropped, worldDraw.stats().brushesOverflow != 0 ? " OVER" : "");
                break;
            }
            // (And how hard the game is: E, M, H.)
            std::snprintf(line, sizeof(line), "pos %5d %5d %5d %s %c",
                          hldata::fxToInt(camera.pos[0]), hldata::fxToInt(camera.pos[1]),
                          hldata::fxToInt(camera.pos[2]),
                          player.noclip ? "fly"
                                        : (player.state.stuck
                                               ? "STUCK"
                                               : (player.state.onGround ? "ground" : "air")),
                          "EMH"[hldata::skillLevelOf(game.logic) - 1]);
            break;
        case 4:
            // ... and the sound: how many of the map's sounds are in memory,
            // and on a DSi whether its DSP is in use ("dsp", with how often
            // it ran short of what to echo, and the loudest it was lately
            // sent -- then the ARM7's figures, as below).
            // (A game between consoles: the radio's account instead.)
#ifdef HLDS_CHILD
            // (Sent the game: what of the memory the map's data takes -- the
            // level, the models and the sounds its stream gave, the group's
            // base -- in KB, beside what is left.)
            if (!netGame.on())
            {
                const guest::Held &held = guest::held();
                std::snprintf(line, sizeof(line), "heap%4uK L%u M%u S%u B%u",
                              static_cast<unsigned>(global.heapHeadroom() / 1024),
                              static_cast<unsigned>(held.mapBytes[static_cast<int>(hldata::GuestKind::Level)] / 1024),
                              static_cast<unsigned>(held.mapBytes[static_cast<int>(hldata::GuestKind::Model)] / 1024),
                              static_cast<unsigned>(held.mapBytes[static_cast<int>(hldata::GuestKind::Sound)] / 1024),
                              static_cast<unsigned>(held.baseBytes / 1024));
                break;
            }
            // (In a match: whether its sound is up, and how the ARM7 keeps
            // up with it -- a console sent the game has nothing else to say
            // why it is silent -- beside the radio's phase.)
            std::snprintf(line, sizeof(line), "snd%d a7 %lu%% %lu clips%d net%d", sound.up() ? 1 : 0,
                          static_cast<unsigned long>(arm7Audio.loadPercent()),
                          static_cast<unsigned long>(arm7Audio.late()), sound.loaded(),
                          static_cast<int>(netGame.phase()));
            break;
#endif
            if (netGame.on())
                netGame.status(line, sizeof(line));
            else if (sound.extended())
                std::snprintf(line, sizeof(line), "dsp %lu in%5d a7 %lu%% %lu",
                              static_cast<unsigned long>(dspAudio.dry()), dspAudio.heard(),
                              static_cast<unsigned long>(arm7Audio.loadPercent()),
                              static_cast<unsigned long>(arm7Audio.late()));
            else
                // (... and how many voices each layer has -- the player's,
                // the world's, the voice, the loops, the menu -- and the
                // ARM7's sound program: how much of its time it takes, the
                // blocks it was late with, and those the music ran dry.)
                std::snprintf(line, sizeof(line), "heap%5uK v%d%d%d%d%d a7 %lu%% %lu/%lu",
                              static_cast<unsigned>(global.heapHeadroom() / 1024),
                              sound.voicesOf(hldata::SoundLayer::Player), sound.voicesOf(hldata::SoundLayer::World),
                              sound.voicesOf(hldata::SoundLayer::Voice), sound.voicesOf(hldata::SoundLayer::Ambience),
                              sound.voicesOf(hldata::SoundLayer::Ui), static_cast<unsigned long>(arm7Audio.loadPercent()),
                              static_cast<unsigned long>(arm7Audio.late()),
                              static_cast<unsigned long>(arm7Audio.musicDry()));
            break;
        case 5:
            // The hardware's own account (GpuStats): the least lead the
            // rasteriser had (46 is idle), passes with lines unfinished and
            // with polygons dropped in this map, and what the budget is
            // holding back because of them.
            // ... and how far off the fog is whole, while a cut is fogged.
            // (A game between consoles: where the first of the others is,
            // as this console has them, and how they stand.)
            if (netGame.playing())
            {
                const hldata::NetMatch &match = netGame.match();
                int other = -1;
                for (int p = 0; p < hldata::kNetPlayers && other < 0; ++p)
                    if (p != match.me && hldata::netHere(match, p))
                        other = p;
                if (other >= 0)
                {
                    const hldata::NetPlayer &them = hldata::netPlayer(match, other);
                    std::snprintf(line, sizeof(line), "p%d %5d %5d %5d f%02x h%d", other, them.pos[0] / 4,
                                  them.pos[1] / 4, them.pos[2] / 4, them.flags, them.health);
                }
                else
                    std::snprintf(line, sizeof(line), "nobody else");
                break;
            }
            std::snprintf(line, sizeof(line), "ln %2d u%3d o%3d %+d fog %d",
                          gpuStats.takeLowLines(), gpuStats.underflows(), gpuStats.overflows(),
                          -worldDraw.heldBack(), worldDraw.fogDistance());
            break;
#ifndef HLDS_PROFILE
        case 6:
        {
            // How many streamed textures are in video memory,
            // what came and went, how many recently visible are drawn flat,
            // how much memory the plan counts in use compared to the total.
            const StreamStats st = textureStream.stats();
            if (textureStream.active())
                // (... then: refusals, blanks that ran out of time, the
                // line the last one ended on, and -- if the two counts of
                // what is in video memory differ -- a leak, shown as "!".)
                std::snprintf(line, sizeof(line), "t%d/%d f%d %d/%dK x%d l%d@%d%s", st.resident, st.streamed,
                              st.flatNow, st.texelKb, st.capacityKb, st.refused, st.late, st.lastLine,
                              st.attached != st.resident ? "!" : "");
            else
                line[0] = '\0';
            break;
        }
#endif
#ifdef HLDS_PROFILE
        case 6:
            std::snprintf(line, sizeof(line), "us frm %5u mov %4u lgc %4u",
                          static_cast<unsigned>(DebugLog::Perf::micros(DebugLog::Section::Frame)),
                          static_cast<unsigned>(DebugLog::Perf::micros(DebugLog::Section::Move)),
                          static_cast<unsigned>(DebugLog::Perf::micros(DebugLog::Section::Logic)));
            break;
        case 7:
            std::snprintf(line, sizeof(line), "us walk  %5u emit %4u",
                          static_cast<unsigned>(DebugLog::Perf::micros(DebugLog::Section::Walk)),
                          static_cast<unsigned>(DebugLog::Perf::micros(DebugLog::Section::Emit)));
            break;
        case 8:
            std::snprintf(line, sizeof(line), "us pose %5u mdl %4u mon %5u",
                          static_cast<unsigned>(DebugLog::Perf::micros(DebugLog::Section::Pose)),
                          static_cast<unsigned>(DebugLog::Perf::micros(DebugLog::Section::Models)),
                          static_cast<unsigned>(DebugLog::Perf::micros(DebugLog::Section::Monsters)));
            break;
        case 9:
            // (Of the monsters': looking, hearing, going about.)
            std::snprintf(line, sizeof(line), "ai look %5u hear %4u go %4u",
                          static_cast<unsigned>(DebugLog::Perf::micros(DebugLog::Section::AiLook)),
                          static_cast<unsigned>(DebugLog::Perf::micros(DebugLog::Section::AiSched)),
                          static_cast<unsigned>(DebugLog::Perf::micros(DebugLog::Section::AiMove)));
            break;
#endif
        default:
            line[0] = '\0';
            break;
        }
        bottomScreen.readout(row_, line);
        row_ = (row_ + 1) % kRows;
    }
} // namespace hlrt
