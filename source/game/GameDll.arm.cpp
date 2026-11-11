#include "game/GameLayout.hpp"

#include <cstring>
#include <dlfcn.h>
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

#ifdef HLDS_CHILD
// (A console that was sent the game has no ROM files to load it from: the
// game is linked into the program, and its table asked for straight.)
extern "C" bool GetGameAPI(std::uint32_t version, const hlrt::GameLayout &layout, const hlrt::EngineFuncs *engine,
                           hlrt::GameFuncs *game);

namespace hlrt
{
    GameFuncs gameApi;

    bool loadGame(const char **why)
    {
        if (!GetGameAPI(kGameApiVersion, gameLayout(), &hlEngine, &gameApi))
        {
            *why = "The game linked in is not of this build.";
            return false;
        }
        return true;
    }
} // namespace hlrt
#else
namespace hlrt
{
    GameFuncs gameApi;

    namespace
    {
        constexpr std::uint32_t kItcm = 0x01000000;
        constexpr std::uint32_t kItcmSize = 32 * 1024;
        /// The last 32 KB below main RAM: one of ITCM's mirrors.
        constexpr std::uint32_t kItcmNear = 0x02000000 - kItcmSize;

        /// What the library calls in ITCM -- the hardware divide that stands
        /// in for the compiler's (HwMath.s) -- is called at the mirror next
        /// to main RAM. The loader's branches reach only 8 MB either way
        /// (libnds dlfcn.c: it checks a byte offset against a word one's
        /// limit), and the library sits at 0x021xxxxx, 17 MB from 0x0100xxxx
        /// where ITCM is linked; 1 MB from its last mirror.
        bool nearItcm(const char *name, std::uint32_t *value, std::uint32_t attributes)
        {
            const bool engines = (attributes & (DSL_SYMBOL_MAIN_BINARY | DSL_SYMBOL_UNRESOLVED)) != 0;
            if (engines && *value >= kItcm && *value < kItcm + kItcmSize)
                *value = kItcmNear | (*value & (kItcmSize - 1));
            // The heap, as the program has it: the guarded one (Guard.hpp).
            // (The library's own malloc would be the allocator's, under
            // it: a block from one given back to the other.)
            if (engines && name != nullptr)
            {
                struct Wrapped
                {
                    const char *name;
                    void *to;
                };
                static const Wrapped kWrapped[] = {
                    {"malloc", reinterpret_cast<void *>(__wrap_malloc)},
                    {"calloc", reinterpret_cast<void *>(__wrap_calloc)},
                    {"realloc", reinterpret_cast<void *>(__wrap_realloc)},
                    {"free", reinterpret_cast<void *>(__wrap_free)},
                    {"memalign", reinterpret_cast<void *>(__wrap_memalign)},
                    {"aligned_alloc", reinterpret_cast<void *>(__wrap_aligned_alloc)},
                    {"posix_memalign", reinterpret_cast<void *>(__wrap_posix_memalign)},
                };
                for (const Wrapped &w : kWrapped)
                {
                    if (std::strcmp(name, w.name) == 0)
                        *value = reinterpret_cast<std::uint32_t>(w.to);
                }
            }
            return true;
        }
    } // namespace

    bool loadGame(const char **why)
    {
        // (The ROM's own file system, read through the card's FAT, which
        // Global::Init has brought up.)
        if (!nitroFSInit(nullptr))
        {
            *why = "The ROM's files cannot be read.";
            return false;
        }
        // ITCM is mirrored every 32 KB up to 0x02000000, but the protection
        // unit lets only its first 32 KB be run: open it over the mirrors
        // (nothing else is there) for nearItcm's addresses.
        CP15_SetRegion4(kItcm | CP15_REGION_SIZE_16MB | CP15_CONFIG_REGION_ENABLE);
        dsl_set_symbol_resolver(nearItcm);

        // Loaded before anything else takes the heap, so that it sits next
        // to the program (see nearItcm), and never let go.
        void *library = dlopen("dsl/hl.dsl", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr)
        {
            *why = dlerror();
            return false;
        }
        // Its code and constants, summed (Guard.hpp): from where it was
        // put to where its data begins.
        const auto *base = static_cast<const std::uint8_t *>(dlmembase(library));
        const auto *data = static_cast<const std::uint8_t *>(dlsym(library, "hlDslData"));
        if (base != nullptr && data != nullptr && data > base)
            guard::noteLibrary(base, static_cast<std::uint32_t>(data - base), static_cast<std::uint32_t>(data - base));
        const auto api = reinterpret_cast<GetGameApiFn>(dlsym(library, "GetGameAPI"));
        if (api == nullptr || !api(kGameApiVersion, gameLayout(), &hlEngine, &gameApi))
        {
            *why = "dsl/hl.dsl is not of this build.";
            dlclose(library);
            return false;
        }
        return true;
    }
} // namespace hlrt
#endif
