// (Only in the game library, HL_DSL. Linked into the program -- a console
// that was sent the game, HLDS_CHILD -- the game reaches the engine as the
// engine reaches itself, and none of this is wanted.)
#ifdef HL_DSL
#include "game/GameApi.hpp"

#include "DebugLog.hpp"
#include "Global.hpp"
#include "audio/Sound.hpp"
#include "data/LevelFile.hpp"
#include "data/ModelCache.hpp"
#include "data/TextureStream.hpp"
#include "game/Input.hpp"
#include "game/Player.hpp"
#include "game/Think.hpp"
#include "net/NetGame.hpp"
#include "platform/Rumble.hpp"
#include "render/BottomScreen.hpp"
#include "render/Camera.hpp"
#include "render/HudDraw.hpp"
#include "render/ScreenFade.hpp"
#include "render/TopText.hpp"

// The game library's side of the engine's table (GameApi.hpp): its objects,
// as the references HL_ENGINE_OBJECT declares, and the bodies of the engine
// functions the game calls, each going on through the table. The game's own
// code (Game.arm.cpp, the shared headers) is written as if they were the
// engine's: they are, one call away.
//
// ARM, as every file of the library must be (the file name says so): a
// tail call from ARM to Thumb inside it goes through a stub the linker
// writes with an absolute address and no relocation, which the loader then
// cannot move -- the call lands where the library was linked, not where it
// is. tools/arm9-report.sh --check says if one is there.

// Where the library's data begins: everything before it (its code and its
// constants) is the same from the moment it is loaded, and is summed then
// and later (source/Guard.hpp). The first .data of the first file linked:
// tools/arm9-report.sh --check says if it is not.
extern "C" __attribute__((visibility("default"), used, section(".data"))) std::uint32_t hlDslData = 0x44415441;

namespace hlrt
{
    Game &game = *hlEngine.game;
    BottomScreen &bottomScreen = *hlEngine.bottomScreen;
    Camera &camera = *hlEngine.camera;
    HudDraw &hudDraw = *hlEngine.hudDraw;
    Input &input = *hlEngine.input;
    Level &level = *hlEngine.level;
    ModelCache &modelCache = *hlEngine.modelCache;
    NetGame &netGame = *hlEngine.netGame;
    Player &player = *hlEngine.player;
    ScreenFade &screenFade = *hlEngine.screenFade;
    Sound &sound = *hlEngine.sound;
    TextureStream &textureStream = *hlEngine.textureStream;
    Think &think = *hlEngine.think;
    TopText &topText = *hlEngine.topText;

    void BottomScreen::setStatus(int weapon, int clip, int spare, unsigned have, unsigned empty, bool canReload)
    {
        hlEngine.bottomSetStatus(*this, weapon, clip, spare, have, empty, canReload);
    }
    void BottomScreen::setLamp(bool suit, bool on, int percent) { hlEngine.bottomSetLamp(*this, suit, on, percent); }
    void Camera::refreshProjection() { hlEngine.cameraRefreshProjection(*this); }
    void Camera::update() { hlEngine.cameraUpdate(*this); }
    void Camera::clampPitch() { hlEngine.cameraClampPitch(*this); }
    void HudDraw::setStatus(bool suit, int health, int armor, int weapon, int clip, int spare, int extra)
    {
        hlEngine.hudSetStatus(*this, suit, health, armor, weapon, clip, spare, extra);
    }
    void NetGame::tick(std::uint16_t yaw, std::int32_t pitch, std::uint32_t did, bool lamp, int ms)
    {
        hlEngine.netTick(*this, yaw, pitch, did, lamp, ms);
    }
    void NetGame::used(int index) { hlEngine.netUsed(*this, index); }
    bool NetGame::spray() { return hlEngine.netSpray(*this); }
    void Player::frame(int vblanks) { hlEngine.playerFrame(*this, vblanks); }
    void Player::turn() { hlEngine.playerTurn(*this); }
    void ScreenFade::setDim(bool under) { hlEngine.fadeSetDim(*this, under); }
    void ModelCache::wield(int index) { hlEngine.modelWield(*this, index); }
    void Sound::ui(int which, int volume) { hlEngine.soundUi(*this, which, volume); }
    int Sound::musicTrack() const { return hlEngine.soundMusicTrack(*this); }
    std::uint32_t Sound::musicMs() const { return hlEngine.soundMusicMs(*this); }
    void Sound::setMusic(int track, std::uint32_t ms) { hlEngine.soundSetMusic(*this, track, ms); }
    void TextureStream::report(const char *why) { hlEngine.streamReport(*this, why); }
    void Think::attach(hldata::LogicWorld &w) { hlEngine.thinkAttach(*this, w); }
    void Think::detach() { hlEngine.thinkDetach(*this); }
    void TopText::show(const hldata::MessageRecord &message, const char *text) { hlEngine.topShow(*this, message, text); }
    void TopText::tick(int ms) { hlEngine.topTick(*this, ms); }
    void TopText::clear() { hlEngine.topClear(*this); }
    void rumble::kick(int strength, int blanks) { hlEngine.rumbleKick(strength, blanks); }

#ifdef HLDS_PROFILE
    namespace DebugLog::Perf
    {
        std::uint32_t now() { return hlEngine.perfNow(); }
        void add(Section section, std::uint32_t ticks) { hlEngine.perfAdd(static_cast<int>(section), ticks); }
    } // namespace DebugLog::Perf
#endif
} // namespace hlrt

std::uint32_t Global::vblanks() { return hlEngine.vblanks(); }

namespace hldata
{
    Trace traceHull(const Hull &hull, const fx *start, const fx *end) { return hlEngine.traceHull(hull, start, end); }
    Trace traceAll(const MoveWorld &world, bool crouched, const fx *start, const fx *end)
    {
        return hlEngine.traceAll(world, crouched, start, end);
    }
    void poseModel(const ModelView &model, int clip, std::uint32_t frame, BoneMatrix *out,
                   const std::int32_t *controls, int blend)
    {
        hlEngine.poseModel(model, clip, frame, out, controls, blend);
    }
    std::uint32_t actorAdvance(const ActorWorld &a, ActorState &actor, int ms)
    {
        return hlEngine.actorAdvance(a, actor, ms);
    }
    int walkTick(ActorWorld &a, const MoveWorld &world, int index, int small, const fx *dest, fx speed, int ms,
                 const PlayerState *player, fx half, fx height, bool still, std::uint16_t *idealYaw)
    {
        return hlEngine.walkTick(a, world, index, small, dest, speed, ms, player, half, height, still, idealYaw);
    }
} // namespace hldata
#endif
