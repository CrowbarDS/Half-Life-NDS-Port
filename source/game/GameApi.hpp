#pragma once

// The line between the engine and the game, drawn where Half-Life draws it
// between the engine and hl.dll.
//
// The game -- the monsters and their thinking, the weapons, the logic
// entities, a map's saved state, the changes of map, the multiplayer match's
// rules -- is a dynamic library, dsl/hl.dsl in the ROM's NitroFS (built from
// dll/hl/). The engine loads it at startup (GameDll.arm.cpp), and from then on
// the two speak only through two tables:
//
//   GameFuncs    what the engine asks of the game (hl.dll's DLL_FUNCTIONS):
//                set a map up, advance it, save it, change it.
//   EngineFuncs  what the game may ask of the engine (enginefuncs_t): its
//                objects, the few of their functions the game calls, and the
//                hot code that lives in ITCM, where a library cannot go.
//
// The engine owns the memory, as it owns the edicts in Half-Life: `game` and
// all it points to are the engine's, and the renderer and the sound read
// them as they always have. The library has only the code that changes them.
//
// The library binds by address to nothing of the engine but libc's, libnds's
// and the compiler's functions, and this one table, `hlEngine` (it is read
// while the library's own globals are set up, before any call could hand it
// over). Everything else goes through it: tools/arm9-report.sh --check says
// if anything does not. The library is built against this very ELF and
// carried in the same ROM, so the two cannot be of different builds; the
// handshake checks it all the same.

#include <cstdint>
#include <string>

#include "HlNetGame.hpp"
#include "HlThink.hpp"
#include "game/Game.hpp"

namespace hlrt
{
    class BottomScreen;
    class Camera;
    class HudDraw;
    class Input;
    class Level;
    class ModelCache;
    class NetGame;
    class Player;
    class ScreenFade;
    class Sound;
    class TextureStream;
    class Think;
    class TopText;

    /// Changed whenever either table or anything they carry changes shape.
    inline constexpr std::uint32_t kGameApiVersion = 10;

    /// The sizes of what both sides lay out, compared at the handshake: a
    /// library built from other headers, or with other settings, is refused
    /// rather than let loose on the engine's memory. (GameLayout.hpp fills
    /// it in: it needs every class whole.)
    struct GameLayout
    {
        std::uint32_t sizes[24] = {};

        bool operator==(const GameLayout &) const = default;
    };

    /// What the game may ask of the engine.
    struct EngineFuncs
    {
        std::uint32_t version;

        // The engine's objects (HL_ENGINE_OBJECT).
        Game *game;
        BottomScreen *bottomScreen;
        Camera *camera;
        HudDraw *hudDraw;
        Input *input;
        Level *level;
        ModelCache *modelCache;
        NetGame *netGame;
        Player *player;
        ScreenFade *screenFade;
        Sound *sound;
        TextureStream *textureStream;
        Think *think;
        TopText *topText;

        // The few of their functions the game calls (dll/hl/EngineLink.cpp
        // has the library's side of each).
        void (*bottomSetStatus)(BottomScreen &, int weapon, int clip, int spare, unsigned have, unsigned empty,
                                bool canReload);
        void (*bottomSetLamp)(BottomScreen &, bool suit, bool on, int percent);
        void (*cameraRefreshProjection)(Camera &);
        void (*cameraUpdate)(Camera &);
        void (*cameraClampPitch)(Camera &);
        void (*hudSetStatus)(HudDraw &, bool suit, int health, int armor, int weapon, int clip, int spare, int extra);
        void (*netTick)(NetGame &, std::uint16_t yaw, std::int32_t pitch, std::uint32_t did, bool lamp, int ms);
        void (*netUsed)(NetGame &, int index);
        bool (*netSpray)(NetGame &);
        void (*playerFrame)(Player &, int vblanks);
        void (*playerTurn)(Player &);
        void (*fadeSetDim)(ScreenFade &, bool under);
        void (*modelWield)(ModelCache &, int index);
        void (*soundUi)(Sound &, int which, int volume);
        int (*soundMusicTrack)(const Sound &);
        std::uint32_t (*soundMusicMs)(const Sound &);
        void (*soundSetMusic)(Sound &, int track, std::uint32_t ms);
        void (*streamReport)(TextureStream &, const char *why);
        void (*thinkAttach)(Think &, hldata::LogicWorld &);
        void (*thinkDetach)(Think &);
        void (*topShow)(TopText &, const hldata::MessageRecord &, const char *text);
        void (*topTick)(TopText &, int ms);
        void (*topClear)(TopText &);
        /// A Rumble Pak's jolt (platform/Rumble.hpp): a shot.
        void (*rumbleKick)(int strength, int blanks);
        std::uint32_t (*vblanks)();
        // (The profiling build's clock: DebugLog.hpp. Null without it.)
        std::uint32_t (*perfNow)();
        void (*perfAdd)(int section, std::uint32_t ticks);

        // The hot code, in ITCM (HL_HOT): the library's declarations of these
        // are bodiless, and EngineLink.cpp sends each here.
        hldata::Trace (*traceHull)(const hldata::Hull &, const hldata::fx *start, const hldata::fx *end);
        hldata::Trace (*traceAll)(const hldata::MoveWorld &, bool crouched, const hldata::fx *start,
                                  const hldata::fx *end);
        void (*poseModel)(const hldata::ModelView &, int clip, std::uint32_t frame, hldata::BoneMatrix *out,
                          const std::int32_t *controls, int blend);
        std::uint32_t (*actorAdvance)(const hldata::ActorWorld &, hldata::ActorState &, int ms);
        int (*walkTick)(hldata::ActorWorld &, const hldata::MoveWorld &, int index, int small, const hldata::fx *dest,
                        hldata::fx speed, int ms, const hldata::PlayerState *player, hldata::fx half,
                        hldata::fx height, bool still, std::uint16_t *idealYaw);
    };

    /// What the engine asks of the game. The first group is `Game`'s own
    /// functions (Game.hpp says what each does): the engine's Game.cpp hands
    /// each call on to these.
    struct GameFuncs
    {
        bool (*reserve)(Game &);
        bool (*create)(Game &);
        void (*destroy)(Game &);
        void (*frame)(Game &, int vblanks);
        hldata::Arrival (*makeArrival)(const Game &);
        bool (*applyArrival)(Game &, const hldata::Arrival &);
        void (*resetProgress)(Game &);
        void (*rememberMap)(Game &, std::uint32_t mapHash);
        void (*restoreMap)(Game &, std::uint32_t mapHash);
        hldata::SaveData (*snapshot)(Game &, const std::string &map);
        void (*adoptProgress)(Game &, const hldata::SaveData &);
        void (*placeFromSave)(Game &, const hldata::SaveData &);
        bool (*arrivalOf)(const hldata::SaveData &, hldata::Arrival &);
        bool (*layState)(Game &, const hldata::SaveData &);
        hldata::SaveHeading (*heading)(const Game &, const std::string &map);
        std::uint32_t (*storedBytes)(Game &);
        bool (*storeMap)(Game &, hldata::detail::StreamOut &);
        bool (*layStored)(Game &, hldata::detail::StreamIn &, const std::int16_t *sentAway, std::uint8_t landed);

        // ... and what the engine's other parts ask.
        /// The think worker (Think.cpp): one question worked out, `yield`
        /// called now and then to let the frame go on.
        bool (*thinkRunOne)(hldata::LogicWorld &, void (*yield)());
        void (*thinkClear)(hldata::ThinkQueue &);
        /// Damage to the player from outside the logic (a fall).
        void (*hurtPlayer)(hldata::PlayerState &, std::int32_t amount, std::uint32_t type);
        /// The COMMAND line's killall.
        int (*killAllOfKind)(hldata::LogicWorld &, hldata::ActorKind);
        /// The multiplayer match's rules (NetGame.cpp has the wireless).
        void (*netStart)(hldata::NetMatch &, hldata::LogicWorld &, hldata::PlayerState &, int firstActor, int model,
                         const hldata::NetSpawn *spawns, int spawnCount, std::uint32_t seed);
        void (*netTick)(hldata::NetMatch &, hldata::LogicWorld &, hldata::PlayerState &, std::uint16_t yaw,
                        std::int32_t pitch, std::uint32_t did, bool lamp, int ms);
        /// What came from another console, played on the match's world.
        void (*netRead)(hldata::NetMatch &, const std::uint8_t *bytes, int size, int who);
        /// The player used the actor `index`.
        void (*netUsed)(hldata::NetMatch &, int index);
        /// The match's events, before it has a map.
        void (*netLobby)(hldata::NetMatch &, hldata::LogicWorld &none, hldata::PlayerState &nobody);
    };

    /// The game, once loadGame() has it (GameDll.arm.cpp).
    extern GameFuncs gameApi;

    /// Load dsl/hl.dsl and take its table. False, with why, if it cannot be
    /// had or is not of this build.
    bool loadGame(const char **why);
} // namespace hlrt

/// The engine's table, the one thing of the engine's the library takes by
/// its address (see above).
extern "C" const hlrt::EngineFuncs hlEngine;

/// The library's one export: takes the engine's table and fills the game's.
/// False if `version` or `layout` is not its own.
using GetGameApiFn = bool (*)(std::uint32_t version, const hlrt::GameLayout &layout,
                              const hlrt::EngineFuncs *engine, hlrt::GameFuncs *game);
