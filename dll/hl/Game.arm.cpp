#include "data/TextureStream.hpp"
#include "net/NetGame.hpp"
#include "game/Think.hpp"
#include "game/Game.hpp"

#include "HlMaterial.hpp"
#include "HlSaveState.hpp"
#include "HlMapStore.hpp"
#include "HlRenew.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

#include "DebugLog.hpp"
#include "Global.hpp"
#include "data/LevelFile.hpp"
#include "data/ModelCache.hpp"
#include "game/Input.hpp"
#include "game/Player.hpp"
#include "audio/Sound.hpp"
#include "render/HudDraw.hpp"
#include "render/ScreenFade.hpp"
#include "render/TopText.hpp"
#include "render/BottomScreen.hpp"
#include "render/Camera.hpp"

// The game's half of Game (game/Game.hpp), in the game library: the engine's
// Game.cpp hands each call on to these through the GameFuncs table
// (GameApi.cpp). ARM code (the file name says so): the logic is inline in
// shared/ and is instantiated here, with its 64-bit arithmetic.

namespace hlrt
{
    namespace
    {
        using namespace hldata;

        /// The longest stretch of time advanced in one go. A frame that took
        /// longer -- a load, a hitch -- is played in slow motion rather than
        /// letting a door cover half its travel between two collision checks.
        constexpr int kMaxVblanks = 4;
    } // namespace

    void Game::destroy()
    {
        hlrt::think.detach(); // (before what the worker reads goes)
        actors.ai = nullptr;
        std::free(aiMonsters_);
        std::free(aiSlots_);
        std::free(aiHeld_);
        aiMonsters_ = nullptr;
        aiSlots_ = nullptr;
        aiHeld_ = nullptr;
        std::free(linkShut_);
        linkShut_ = nullptr;
        actors.linkShut = nullptr;
        std::free(brushStates_);
        std::free(movers_);
        std::free(reach_);
        std::free(states_);
        std::free(ladders_);
        std::free(cornerState_);
        cornerState_ = nullptr;
        std::free(actorStates_);
        std::free(actorBoxes_);
        actorStates_ = nullptr;
        actorBoxes_ = nullptr;
        renew(actors);
        ladders_ = nullptr;
        brushStates_ = nullptr;
        movers_ = nullptr;
        reach_ = nullptr;
        states_ = nullptr;
        renew(world);
        renew(logic);
        topText.clear();
        // (The lamp comes along as it is, as in the game: alight or out,
        // with what is left in it.)
    }

    bool Game::create()
    {
        destroy();
        // (Nothing of the changing lights has been worked out yet.)
        std::memset(styleValues_, 0xFF, sizeof(styleValues_));
        // The chapter's name, half a second into the map (not from a save:
        // placeFromSave says so).
        startMessagesMs_ = 500;
        const LevelView &view = level.view;
        // (Every map says which chapter it is part of -- the converter saw
        // to it -- for whoever lists the saves.)
        for (std::uint32_t i = 0; i < view.messageCount; ++i)
        {
            if (!(view.messages[i].flags & kMessageChapter))
                continue;
            std::snprintf(chapter_, sizeof(chapter_), "%s", view.strings + view.messages[i].text);
            for (char &c : chapter_)
                c = c == '\n' || c == '\r' ? ' ' : c;
            break;
        }

        // + 1: a level with no brushes or no logic still gets a valid pointer.
        brushStates_ =
            static_cast<BrushState *>(std::calloc(view.brushCount + 1, sizeof(BrushState)));
        movers_ = static_cast<MoverHull *>(std::calloc(view.brushCount + 1, sizeof(MoverHull)));
        reach_ = static_cast<MoverReach *>(std::calloc(view.brushCount + 1, sizeof(MoverReach)));
        states_ = static_cast<LogicState *>(std::calloc(view.logicCount + 1, sizeof(LogicState)));
        int ladderCount = 0;
        for (std::uint32_t i = 0; i < view.logicCount; ++i)
            ladderCount += view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Ladder);
        ladders_ = static_cast<LadderVolume *>(std::calloc(ladderCount + 1, sizeof(LadderVolume)));
        cornerState_ = static_cast<std::uint8_t *>(std::calloc(view.cornerCount + 1, 1));
        // (On a multiplayer map, room for a match's players instead.)
        const int actorCount = static_cast<int>(view.actorCount) + spareActorsOf(view);
        actorStates_ = static_cast<ActorState *>(std::calloc(actorCount, sizeof(ActorState)));
        actorBoxes_ = static_cast<SolidBox *>(std::calloc(actorCount, sizeof(SolidBox)));
        if (brushStates_ == nullptr || movers_ == nullptr || reach_ == nullptr || states_ == nullptr ||
            ladders_ == nullptr || cornerState_ == nullptr || actorStates_ == nullptr ||
            actorBoxes_ == nullptr)
        {
            destroy();
            return false;
        }
        for (std::uint32_t i = 0; i < view.brushCount; ++i)
        {
            const Model &model = view.models[view.brushes[i].model];
            new (&movers_[i]) MoverHull();
            movers_[i].standing =
                Hull{view.clipNodes, view.planes, view.planeTypes, model.clipStanding};
            movers_[i].crouched =
                Hull{view.clipNodes, view.planes, view.planeTypes, model.clipCrouched};
        }

        world.standing =
            Hull{view.clipNodes, view.planes, view.planeTypes, view.models[0].clipStanding};
        world.crouched =
            Hull{view.clipNodes, view.planes, view.planeTypes, view.models[0].clipCrouched};
        world.large = Hull{view.clipNodes, view.planes, view.planeTypes, view.models[0].clipLarge};
        world.movers = movers_;
        world.reach = reach_;
        world.moverCount = static_cast<int>(view.brushCount);
        world.nodes = view.nodes;
        world.leaves = view.leaves;
        world.headNode = view.models[0].headNode;
        world.vis = view.vis;
        world.visSize = view.visSize;
        world.visLeafCount = view.header->visLeafCount;
        world.ladders = ladders_;
        world.ladderCount = ladderCount;

        logic.records = view.logic;
        logic.strings.value = view.strings;
        logic.recordCount = static_cast<int>(view.logicCount);
        logic.aux = view.logicAux;
        logic.corners = view.corners;
        logic.brushes = view.brushes;
        logic.brushCount = static_cast<int>(view.brushCount);
        logic.models.value = view.models;
        logic.modelCount.value = static_cast<int>(view.modelCount);
        logic.states = states_;
        logic.brushStates = brushStates_;
        logic.movers = movers_;
        logic.ladders = ladders_;
        logic.world = &world;
        logic.globals = &globals;
        // The models must be loaded by now (main loads them before this): an
        // actor is set up from its model's clips.
        actors.states = actorStates_;
        actors.count = actorCount;
        actors.models = modelCache.views();
        actors.modelCount = modelCache.count();
        gibModelsFind(actors, view.modelRefs, static_cast<int>(view.modelRefCount));
        {
            // (In the order of the kinds: the bolt, the satchel, the player's
            // tripmine -- the held model's mine -- the snark, the rocket and
            // the hornet.)
            static const char *const kThrown[6] = {"crossbow_bolt", "w_satchel", "v_tripmine",
                                                   "w_squeak",      "rpgrocket", "hornet"};
            for (int k = 0; k < 6; ++k)
            {
                thrown_[k] = -1;
                for (std::uint32_t i = 0; i < view.modelRefCount; ++i)
                {
                    if (std::strcmp(view.modelRefs[i].name, kThrown[k]) == 0)
                        thrown_[k] = static_cast<std::int16_t>(i);
                }
            }
            // (... and the casings out of the guns: brass, a shotgun's.)
            static const char *const kShells[2] = {"shell", "shotgunshell"};
            // (... and the grenades: a hand grenade, the launcher's.)
            static const char *const kGrenades[2] = {"w_grenade", "grenade"};
            for (int k = 0; k < 2; ++k)
            {
                shell_[k] = grenade_[k] = -1;
                for (std::uint32_t i = 0; i < view.modelRefCount; ++i)
                {
                    if (std::strcmp(view.modelRefs[i].name, kShells[k]) == 0)
                        shell_[k] = static_cast<std::int16_t>(i);
                    if (std::strcmp(view.modelRefs[i].name, kGrenades[k]) == 0)
                        grenade_[k] = static_cast<std::int16_t>(i);
                }
            }
        }
        // (A DSi has the memory for twice as much lying about, and going on.)
        consoleLimits(actors, logic, isDSiMode());
        actors.boxes = actorBoxes_;
        world.boxes = actorBoxes_;
        world.boxCount = actorCount;
        logic.actors = &actors;
        logic.nameHashes = view.nameHashes;
        logic.sprites = view.sprites;
        logic.speech = view.speechCount != 0 ? view.speech : nullptr;
        logic.spriteCount = static_cast<int>(view.spriteCount);
        logic.nameCount = static_cast<int>(view.nameCount);
        logic.sounds = view.logicSoundCount != 0 ? view.logicSounds : nullptr;
        logic.heard = &heard;
        logic.level = &view;
        logic.modelSounds = view.modelSoundCount != 0 ? view.modelSounds : nullptr;
        logic.modelSoundCount = static_cast<int>(view.modelSoundCount);
        heard.count = 0;
        heard.dropped = 0;
        heard.commons = commonsOf(view.header->version);
        logic.cornerState = cornerState_;
        logic.cornerCount = static_cast<int>(view.cornerCount);
        // Places every brush where the map starts it, for the renderer and
        // the collision alike.
        logicInit(logic);
        moverReachAll(world);
        actorsInit(actors, view.actors, static_cast<int>(view.actorCount), view.nameHashes, world);
        actors.nodes = view.pathNodes;
        actors.links = view.pathLinks;
        actors.nodeCount = static_cast<int>(view.pathNodeCount);
        actors.linkCount = static_cast<int>(view.pathLinkCount);
        // (How each way between two nodes is just now: the logic keeps it.)
        std::free(linkShut_);
        linkShut_ = static_cast<std::uint8_t *>(std::calloc(view.pathLinkCount + 1, 1));
        actors.linkShut = linkShut_;
        actors.doorsWanted = 0;
        weapon_ = WeaponState();
        held = ActorState();
        inHand_ = -1;
        // (What the monsters think and hear: HlAi.hpp.)
        {
            const int thinkers = aiNeeded(actors);
            std::free(aiMonsters_);
            std::free(aiSlots_);
            std::free(aiHeld_);
            aiMonsters_ = static_cast<AiMonster *>(std::malloc(sizeof(AiMonster) * (thinkers > 0 ? thinkers : 1)));
            aiSlots_ = static_cast<std::int16_t *>(std::malloc(sizeof(std::int16_t) * (actors.count > 0 ? actors.count : 1)));
            aiHeld_ = static_cast<std::uint8_t *>(std::malloc(actors.count > 0 ? actors.count : 1));
            if (aiMonsters_ != nullptr && aiSlots_ != nullptr && aiHeld_ != nullptr)
                aiAttach(actors, ai_, aiMonsters_, thinkers, aiSlots_, aiHeld_, actors.count);
            ai_.speechCount = static_cast<int>(view.speechCount);
        }
        // (Their long thinking, outside the frame: game/Think.cpp.)
        hlrt::think.attach(logic);
        return true;
    }

    void Game::takeInHand()
    {
        static const char *const kModels[] = {nullptr,      "v_crowbar", "v_9mmhandgun", "v_shotgun",
                                              "v_grenade",  "v_9mmar",   "v_357",        "v_crossbow",
                                              "v_satchel",  "v_tripmine", "v_squeak",    "v_rpg",
                                              "v_gauss",    "v_egon",    "v_hgun"};
        static const char *const kIdle[] = {nullptr, "idle1", "idle1", "sm_idle", "idle", "longidle", "idle1",
                                            "idle1", "idle1", "idle1", "idle1", "idle", "idle", "idle1", "idle1"};
        // (How each comes out: most say "draw"; the crossbow and the RPG
        // "draw1", the MP5 "deploy", a snark and the hornetgun "up".)
        static const char *const kDraw[] = {nullptr, "draw", "draw", "draw", "draw", "deploy", "draw",
                                            "draw1", "draw", "draw", "up",   "draw1", "draw", "draw", "up"};
        static_assert(sizeof(kModels) / sizeof(kModels[0]) == kWeaponCount);
        const LevelView &view = level.view;
        const int weapon = player.state.inv.current;
        inHand_ = weapon;
        // (The satchel's hand is the radio while there are charges out.)
        radio_ = weapon == kWeaponSatchel && satchelsOut();
        held = ActorState();
        modelCache.wield(-1);
        if (weapon <= kWeaponNone || weapon >= kWeaponCount)
            return;
        // By name: a map has the models of what can be held in it.
        for (std::uint32_t m = 0; m < view.modelRefCount; ++m)
        {
            if (std::strcmp(view.modelRefs[m].name, radio_ ? "v_satchel_radio" : kModels[weapon]) != 0)
                continue;
            held.active = true;
            held.model = static_cast<std::uint16_t>(m);
            modelCache.wield(static_cast<int>(m)); // (its skin is put into video memory after the next blank)
            held.kind = static_cast<std::uint8_t>(ActorKind::Generic);
            held.light = 28;
            const ModelView *model = actorModel(actors, held);
            if (model == nullptr)
            {
                held.active = false;
                return;
            }
            if (model->header->clipCount != 0)
            {
                const int idle = model->findClip(nameHash(kIdle[weapon]));
                held.restClip = static_cast<std::int16_t>(idle < 0 ? 0 : idle);
            }
            actorRest(held);
            if (!actorPlay(actors, held, nameHash(kDraw[weapon]), true))
                actorPlay(actors, held, nameHash("draw"), true);
            return;
        }
    }

    Arrival Game::makeArrival() const
    {
        const LevelView &view = level.view;
        Arrival arrival = hldata::makeArrival(logic, view.landmarks, view.landmarkCount,
                                              player.state, logic.pendingChange, view.modelRefs, leftWith_);
        arrival.yaw = camera.yaw;
        arrival.pitch = camera.pitch;
        trainCarried_ = arrival.train != 0 && trainDriven(logic) >= 0;
        return arrival;
    }

    bool Game::applyArrival(const Arrival &arrival)
    {
        const LevelView &view = level.view;
        came_ = arrival; // (for a save of this map: who came along is in it)
        if (!hldata::applyArrival(logic, view.landmarks, view.landmarkCount, view.nameHashes,
                                  view.nameCount, player.state, arrival, view.modelRefs, &placed_))
            return false;
        camera.yaw = arrival.yaw;
        camera.pitch = arrival.pitch;
        if (trainCarried_)
            arrivalDriving(logic, arrival);
        trainCarried_ = false;
        return true;
    }

    void Game::resetProgress()
    {
        globals = Globals();
        visited_.clear();
        playMs_ = 0;
        chapter_[0] = 0;
        came_ = Arrival();
        lamp = LampState();
    }

    void Game::rememberMap(std::uint32_t mapHash)
    {
        if (states_ == nullptr)
            return;
        SaveMapState *slot = nullptr;
        for (SaveMapState &map : visited_)
        {
            if (map.map == mapHash)
                slot = &map;
        }
        if (slot == nullptr)
        {
            visited_.emplace_back();
            slot = &visited_.back();
            slot->map = mapHash;
        }
        mapRemember(*slot, logic, actors);
    }

    void Game::restoreMap(std::uint32_t mapHash)
    {
        for (const SaveMapState &map : visited_)
        {
            if (map.map == mapHash)
                mapRestore(map, logic, actors);
        }
    }

    SaveData Game::snapshot(const std::string &map)
    {
        rememberMap(nameHash(map.c_str()));
        SaveData save;
        save.map = map;
        for (int a = 0; a < 3; ++a)
        {
            save.origin[a] = player.state.origin[a];
            save.velocity[a] = player.state.velocity[a];
        }
        save.yaw = camera.yaw;
        save.pitch = camera.pitch;
        save.ducked = player.state.ducked ? 1 : 0;
        save.health = player.state.health;
        const Inventory &inv = player.state.inv;
        save.weapons = inv.have;
        save.weapon = inv.current;
        save.clipGlock = inv.clipGlock;
        save.clipShotgun = inv.clipShotgun;
        save.ammo9mm = inv.ammo9mm;
        save.ammoBuck = inv.ammoBuck;
        save.armor = inv.armor;
        save.grenades = inv.grenades;
        save.clipMp5 = inv.clipMp5;
        save.clipPython = inv.clipPython;
        save.ammo357 = inv.ammo357;
        save.ammoAr = inv.ammoAr;
        save.arsenal = player.arsenal;
        save.lampOn = lamp.on ? 1 : 0;
        save.lampCharge = lamp.charge;
        for (int i = 0; i < globals.count; ++i)
        {
            save.globalHash.push_back(globals.hash[i]);
            save.globalState.push_back(globals.state[i]);
        }
        for (int i = 0; i < logic.recordCount; ++i)
        {
            const LogicRecord &train = logic.records[i];
            if (train.kind != static_cast<std::uint8_t>(LogicKind::TrackTrain) ||
                train.extra[2] == 0 || train.brush == kNoBrush ||
                !detail::playerAboard(logic, player.state, train.brush))
                continue;
            save.train = train.extra[2];
            for (int a = 0; a < 3; ++a)
                save.trainPos[a] = brushStates_[train.brush].pos[a];
            save.trainYaw = brushStates_[train.brush].angles[kYaw];
            save.trainSpeed = states_[i].speed;
            // (Its second bit: the player has its controls.)
            save.trainSelfDriven = static_cast<std::uint8_t>((states_[i].running ? 1 : 0) |
                                                             ((states_[i].phase & detail::kTrainDriven) ? 2 : 0));
            break;
        }
        save.maps = visited_;

        // The whole of this map, as it is (HlSaveState.hpp); before it, how
        // the player came into it; after it, what is the console's own.
        hldata::appendState(save.state, came_);
        const std::vector<std::uint8_t> state = takeMapState(logic, player.state, level.view.header->crc32);
        save.state.insert(save.state.end(), state.begin(), state.end());
        ConsoleKept kept{};
        kept.playMs = playMs_;
        kept.music = sound.musicTrack();
        kept.musicMs = sound.musicMs();
        std::memcpy(kept.chapter, chapter_, sizeof(kept.chapter));
        // (Under a heading of its own: another build's is passed over.)
        const ConsoleHeading console = consoleHeading(weapon_, player.view, kept);
        save.state.insert(save.state.end(), reinterpret_cast<const std::uint8_t *>(&console),
                          reinterpret_cast<const std::uint8_t *>(&console) + sizeof(console));
        hldata::appendState(save.state, weapon_);
        hldata::appendState(save.state, player.view);
        hldata::appendState(save.state, kept);
        // ... and what the monsters think (HlAiSave.hpp): a save without it
        // is read all the same.
        aiAppendState(save.state, actors);
        return save;
    }

    bool Game::arrivalOf(const SaveData &save, Arrival &out)
    {
        std::size_t at = 0;
        Arrival read;
        if (!hldata::readState(save.state.data(), save.state.size(), at, read) || !read.valid)
            return false;
        out = read;
        return true;
    }

    SaveHeading Game::heading(const std::string &map) const
    {
        SaveHeading h;
        std::snprintf(h.map, sizeof(h.map), "%s", map.c_str());
        std::snprintf(h.chapter, sizeof(h.chapter), "%s", chapter_);
        h.playSeconds = playSeconds();
        h.health = static_cast<std::uint8_t>(player.state.health < 0 ? 0 : (player.state.health > 255 ? 255 : player.state.health));
        return h;
    }

    bool Game::layState(const SaveData &save)
    {
        const std::uint8_t *bytes = save.state.data();
        const std::size_t size = save.state.size();
        std::size_t at = 0;
        Arrival came;
        if (!hldata::readState(bytes, size, at, came))
            return false;
        const std::size_t length = hldata::mapStateLength(bytes + at, size - at);
        // (All of it or none: the map's state, then the console's own, to
        // the last byte.) The console's: under a heading of its own from
        // version 9 -- another build's passed over, and the defaults kept --
        // and before that as it lay.
        WeaponState weapon;
        ViewState view;
        ConsoleKept kept;
        const ConsoleHeading want = consoleHeading(weapon, view, kept);
        ConsoleHeading have{};
        bool framed = save.version >= 9;
        if (framed && length != 0 && size - at >= length + sizeof(have))
            std::memcpy(&have, bytes + at + length, sizeof(have));
        const std::size_t own =
            framed ? sizeof(have) + have.bytes
                   : hldata::stateBytes(weapon) + hldata::stateBytes(view) + sizeof(ConsoleKept);
        // (What the monsters think may follow, in a block of its own: a save
        // written before, or by another build, has none, or one not read.)
        const std::size_t minds = length != 0 && size - at >= length + own
                                      ? size - at - length - own
                                      : 0;
        // (A block that is not this build's is left: they think afresh.)
        if (length == 0 || size - at < length + own ||
            (framed && std::memcmp(have.magic, kConsoleMagic, sizeof(have.magic)) != 0) ||
            !layMapState(bytes + at, length, logic, player.state, level.view.header->crc32))
            return false;
        at += length;
        weapon_ = WeaponState();
        player.view = ViewState();
        if (!framed)
        {
            hldata::readState(bytes, size, at, weapon_);
            hldata::readState(bytes, size, at, player.view);
            std::memcpy(&kept, bytes + at, sizeof(kept));
            at += sizeof(kept);
        }
        else if (have.print == want.print && have.bytes == want.bytes)
        {
            at += sizeof(have);
            hldata::readState(bytes, size, at, weapon_);
            hldata::readState(bytes, size, at, player.view);
            hldata::readState(bytes, size, at, kept);
        }
        else
        {
            at += own;
        }
        weapon_.anim = nullptr;
        playMs_ = kept.playMs;
        std::memcpy(chapter_, kept.chapter, sizeof(chapter_));
        chapter_[sizeof(chapter_) - 1] = 0;
        if (minds != 0)
            hldata::aiLayState(bytes + at, minds, actors);
        came_ = came;
        afterLay();
        // The music that was playing, from where it was.
        sound.setMusic(kept.music, kept.musicMs);
        lastHealth_ = player.state.health;
        return true;
    }

    void Game::afterLay()
    {
        // What the map did as it began afresh is not what is to be heard
        // or shown now: what was going on when it was left is.
        heard.count = 0;
        logic.shownCount = 0;
        logic.saveRequested = false;
        logic.pendingChange = -1;
        logic.musicRequest = -1;
        logic.reloadMs = -1;
        logic.endSection = false;
        logic.camera = -1;
        cameraWas_ = -1;
        startMessagesMs_ = -1;
        topText.clear();
        // (What was on its way somewhere goes on sounding as it goes.)
        for (int i = 0; i < logic.recordCount; ++i)
        {
            const MoveState move = states_[i].move;
            if (!states_[i].dead && (move == MoveState::GoingUp || move == MoveState::GoingDown))
                detail::recordSound(logic, i, false);
        }
        // The lights that change are worked out again, and the weapon in
        // hand taken up again; and what lies about kept to what this console
        // has room for (a state from the other kind of console brings its
        // own).
        std::memset(styleValues_, 0xFF, sizeof(styleValues_));
        inHand_ = -1;
        consoleLimits(actors, logic, isDSiMode());
    }

    std::uint32_t Game::storedBytes()
    {
        const std::uint32_t map =
            static_cast<std::uint32_t>(sizeof(detail::StateHeading)) +
            detail::stateHeading(logic, player.state, level.view.header->crc32).bytes;
        const std::uint32_t minds =
            actors.ai != nullptr
                ? static_cast<std::uint32_t>(sizeof(detail::AiStateHeading)) +
                      detail::aiStateHeading(*actors.ai, actors, aiStateCount(actors)).bytes
                : 0u;
        return map + minds;
    }

    bool Game::storeMap(detail::StreamOut &out)
    {
        // As it is left: not on its way out of itself, nor asking anything
        // of the console.
        logic.pendingChange = -1;
        logic.saveRequested = false;
        logic.reloadMs = -1;
        logic.endSection = false;
        logic.camera = -1;
        return streamMapState(out, logic, player.state, level.view.header->crc32) && aiStreamState(out, actors);
    }

    bool Game::layStored(detail::StreamIn &in, const std::int16_t *sentAway, std::uint8_t landed)
    {
        // (The player is the Arrival's, not who left: what the map kept of
        // them goes nowhere.)
        PlayerState left = player.state;
        if (!layMapStateStream(in, logic, left, level.view.header->crc32))
            return false;
        aiLayStream(in, actors);
        // Those who went along, and came into the map after: not here.
        runSendAway(actors, sentAway, landed);
        afterLay();
        return true;
    }

    void Game::adoptProgress(const SaveData &save)
    {
        globals = Globals();
        for (std::size_t i = 0; i < save.globalHash.size(); ++i)
            globals.set(save.globalHash[i], save.globalState[i] > kGlobalDead && save.globalHash[i] != kGlobalPlayerGravity &&
                                                    save.globalHash[i] != kGlobalSkill
                                                ? kGlobalOff
                                                : save.globalState[i]);
        visited_ = save.maps;
    }

    void Game::placeFromSave(const SaveData &save)
    {
        startMessagesMs_ = -1;
        for (int a = 0; a < 3; ++a)
        {
            player.state.origin[a] = save.origin[a];
            player.state.velocity[a] = save.velocity[a];
        }
        player.state.ducked = save.ducked != 0;
        player.state.viewHeight = player.state.ducked ? kDuckViewHeight : kViewHeight;
        player.state.health = save.health > 0 && save.health <= 100 ? save.health : 100;
        // What a file says they carry is kept within what can be carried.
        auto within = [](int value, int most)
        { return static_cast<std::int16_t>(value < 0 ? 0 : (value > most ? most : value)); };
        Inventory &inv = player.state.inv;
        inv.have = save.weapons & ((1u << kWeaponFirstMore) - 1u); // (bit 0 is the suit)
        inv.current = save.weapon < kWeaponCount ? save.weapon : kWeaponNone;
        // The later weapons (none, from a save made before they were).
        player.arsenal = save.arsenal;
        player.arsenal.have &= kArsenalKept; // (the weapons there are, and the RPG's state)
        player.arsenal.boltsClip = within(save.arsenal.boltsClip, 5);
        player.arsenal.bolts = within(save.arsenal.bolts, 50);
        player.arsenal.satchels = within(save.arsenal.satchels, 5);
        player.arsenal.tripmines = within(save.arsenal.tripmines, 5);
        player.arsenal.snarks = within(save.arsenal.snarks, 15);
        player.arsenal.rockets = within(save.arsenal.rockets, 5);
        player.arsenal.uranium = within(save.arsenal.uranium, 100);
        player.arsenal.hornets = within(save.arsenal.hornets, 8);
        player.state.arsenal = &player.arsenal;
        player.state.cheats = &cheats;
        inv.clipGlock = within(save.clipGlock, 17);
        inv.clipShotgun = within(save.clipShotgun, 8);
        inv.ammo9mm = within(save.ammo9mm, 250);
        inv.ammoBuck = within(save.ammoBuck, 125);
        inv.armor = within(save.armor, 100);
        inv.grenades = within(save.grenades, 10);
        inv.clipMp5 = within(save.clipMp5, 50);
        inv.clipPython = within(save.clipPython, 6);
        inv.ammo357 = within(save.ammo357, 36);
        inv.ammoAr = within(save.ammoAr, 10);
        lamp.on = save.lampOn != 0;
        lamp.charge = save.lampCharge < 0 ? 0 : (save.lampCharge > kLampFull ? kLampFull : save.lampCharge);
        if (inv.grenades == 0)
            inv.have &= static_cast<std::uint8_t>(~(1u << kWeaponGrenade));
        camera.yaw = save.yaw;
        camera.pitch = save.pitch;
        camera.clampPitch();
        for (int i = 0; i < logic.recordCount && save.train != 0; ++i)
        {
            const LogicRecord &train = logic.records[i];
            if (train.kind != static_cast<std::uint8_t>(LogicKind::TrackTrain) ||
                train.extra[2] != save.train || train.brush == kNoBrush)
                continue;
            logicPlaceBrush(logic, train.brush, save.trainPos, save.trainYaw);
            states_[i].speed = save.trainSpeed;
            states_[i].running = (save.trainSelfDriven & 1) != 0;
            if (save.trainSelfDriven & 2)
                states_[i].phase |= detail::kTrainDriven;
            const std::uint16_t node =
                detail::trackNearest(logic, save.trainPos, static_cast<fx>(train.extra[1]));
            if (node != kNoCorner)
                states_[i].bits = node;
            player.state.onGround = true;
            player.state.groundMover = static_cast<std::int16_t>(train.brush);
            break;
        }
    }

    void Game::cameraView()
    {
        const int index = logic.camera;
        if (index < 0)
        {
            if (cameraWas_ >= 0)
            {
                camera.yaw = ownYaw_;
                camera.pitch = ownPitch_;
                cameraWas_ = -1;
                camera.update();
            }
            return;
        }
        if (cameraWas_ < 0)
        {
            ownYaw_ = camera.yaw;
            ownPitch_ = camera.pitch;
        }
        cameraWas_ = index;
        const LogicRecord &record = logic.records[index];
        const LogicState &state = logic.states[index];
        for (int a = 0; a < 3; ++a)
            camera.pos[a] = state.base[a];
        // At the player's eye, or at what it names (wherever that has got
        // to); with neither, straight on as the map turned it.
        fx at[3];
        bool aimed = false;
        if (record.extra[0] & 2)
        {
            for (int a = 0; a < 3; ++a)
                at[a] = player.state.origin[a];
            at[2] += player.state.viewHeight;
            aimed = true;
        }
        else
        {
            aimed = placeOfName(logic, record.target, at);
        }
        if (aimed)
        {
            const fx d[3] = {at[0] - camera.pos[0], at[1] - camera.pos[1], 0};
            camera.yaw = fxAtan2Turns(d[1], d[0]);
            camera.pitch = static_cast<std::int16_t>(fxAtan2Turns(at[2] - camera.pos[2], fxLength(d)));
        }
        else
        {
            camera.yaw = static_cast<std::uint16_t>(record.extra[1]);
            camera.pitch = 0;
        }
        camera.kickPitch = camera.kickYaw = camera.roll = 0;
        camera.update();
    }

    void Game::frame(int vblanks)
    {
        // Time slowed or hurried (host_timescale): the sixtieths run are the
        // scaled ones, a part left over carried to the next frame. A frame
        // with none to run still turns the view.
        if (cheats.timescale != 256 && !netGame.playing())
        {
            const int total = vblanks * cheats.timescale + slowCarry_;
            slowCarry_ = total & 255;
            vblanks = total >> 8;
            if (vblanks < 1)
            {
                player.turn();
                camera.update();
                return;
            }
        }
        else
        {
            slowCarry_ = 0;
        }
        if (vblanks < 1)
            vblanks = 1;
        if (vblanks > kMaxVblanks)
            vblanks = kMaxVblanks;
        // The frame in milliseconds, for everything that counts them: what
        // is left of a millisecond carried to the next frame, so the game
        // keeps the same time as the player's movement (HlFrameClock.hpp).
        const int ms = clock_.ms(vblanks);
        playMs_ += static_cast<std::uint64_t>(ms);
        // A view from a camera that takes the controls (trigger_camera's
        // flag 4): the player can do nothing meanwhile.
        if (logic.camera >= 0 && (logic.records[logic.camera].extra[0] & 4))
        {
            input.moveForward = input.moveSide = 0;
            input.jump = input.duck = input.usePressed = input.useHeld = input.attack = input.second = false;
            input.reloadPressed = input.lampPressed = input.sprayPressed = false;
            input.weaponPick = input.weaponStep = input.weaponChoose = 0;
            input.lookDx = input.lookDy = input.lookFineX = input.lookFineY = 0;
        }
        std::uint32_t netDid = 0; // what the weapon did this frame, for a game between consoles
        // (A development aid: debug_start.txt's sixth number, the seconds
        // after which the player dies, once -- for seeing what dying goes
        // back to.)
        if (player.debugDieMs > 0 && (player.debugDieMs -= ms) <= 0)
        {
            player.debugDieMs = -1;
            logicHurt(player.state, 1000);
        }

        if (!player.noclip)
        {
            const DebugLog::Perf::Scope scope(DebugLog::Section::Logic);
            // (A box isn't pushed by someone holding USE, who is pulling it.
            // Not in a match: a guest's USE isn't sent as it is held.)
            logic.useHeld.value = input.useHeld && !netGame.playing();
            logicTick(logic, player.state, ms);
            // What the player rides has turned: so has their view.
            camera.yaw = static_cast<std::uint16_t>(camera.yaw + logic.viewTurn);
            logic.viewTurn = 0;
            if (logic.faceSet)
            {
                camera.yaw = logic.faceYaw;
                camera.pitch = 0;
                logic.faceSet = false;
            }

            if (input.usePressed)
            {
                // From the eyes, along the view as it was drawn last frame.
                const fx eye[3] = {player.state.origin[0], player.state.origin[1],
                                   player.state.origin[2] + player.state.viewHeight};
                // (At a train's controls or a gun's, USE takes them, or lets
                // go: a train's first, as the game's PlayerUse has it.)
                const int target = trainUse(logic, player.state) || tankUse(logic, player.state, camera.forward)
                                       ? -2
                                       : logicFindUsable(logic, eye, camera.forward);
                // (The game's PlayerUse says so: something used, its
                // wpn_select; nothing there to use, wpn_denyselect -- each
                // at four tenths.)
                constexpr int kUseVolume = 51;
                const bool box = target >= 0 && logic.records[target].kind == static_cast<std::uint8_t>(LogicKind::Pushable);
                if (target == -2)
                {
                }
                else if (target >= 0 && !box)
                {
                    logicUse(logic, player.state, target, -1, kUseSet);
                    netGame.used(target); // (a game between consoles: they are told)
                    sound.ui(kUiSoundWeapon, kUseVolume);
                }
                else if (personUse(logic, player.state, eye, camera.forward)) // (a scientist or a guard: follow, or stay)
                    sound.ui(kUiSoundWeapon, kUseVolume);
                else if (box)
                    sound.ui(kUiSoundWeapon, kUseVolume);
                else
                    sound.ui(kUiSoundDeny, kUseVolume);
            }
            else if (input.useHeld && player.state.health > 0)
            {
                // Held: what goes on being used while it is (FCAP_CONTINUOUS_USE:
                // a wall charger) is used again, each frame -- whatever is
                // in view now, as the game's PlayerUse looks again.
                const fx eye[3] = {player.state.origin[0], player.state.origin[1],
                                   player.state.origin[2] + player.state.viewHeight};
                const int target = logicFindUsable(logic, eye, camera.forward);
                if (target >= 0 && logicContinuousUse(logic.records[target]))
                    logicUse(logic, player.state, target, -1, kUseSet);
                else if (target >= 0 && logic.useHeld.value && logic.records[target].kind == static_cast<std::uint8_t>(LogicKind::Pushable))
                    logicPull(logic, player.state, target, ms);
            }
        }
        // What fights, what lies about, and what is in hand.

        // The lights that change: when any is not as strong as it was (a
        // switch, or a tenth of a second of a flicker), the colours of the
        // vertices they reach are worked out again, in the level's own
        // stream -- which is what is drawn from.
        if (level.view.lightPatchCount != 0 &&
            lightStyleValues(Global::vblanks() / 6, logic.stylesOff, styleValues_, logic.stylePattern.value.data()))
        {
            lightPatchApply(level.view.lightPatches, level.view.lightPatchCount, styleValues_,
                            const_cast<std::uint32_t *>(level.view.gxStream));
        }

        // What the map writes on the screen: as it is come into, and when
        // something in it says.
        {
            const LevelView &view = level.view;
            if (startMessagesMs_ >= 0 && (startMessagesMs_ -= ms) < 0)
            {
                for (std::uint32_t i = 0; i < view.messageCount; ++i)
                {
                    if (view.messages[i].flags & kMessageAtStart)
                        topText.show(view.messages[i], view.strings + view.messages[i].text);
                }
            }
            for (int n = 0; n < logic.shownCount; ++n)
            {
                if (logic.shown[n] < view.messageCount)
                    topText.show(view.messages[logic.shown[n]], view.strings + view.messages[logic.shown[n]].text);
            }
            logic.shownCount = 0;
            topText.tick(ms);
        }

        // The lamp: its switch and its battery. (What it lights is the
        // renderer's business: HudDraw::drawLamp, ModelDraw.)
        {
            // (A match whose rules have no lamp: never lit.)
            const bool suit = (player.state.inv.have & kHaveSuit) != 0 && !logic.lampBanned.value;
            if (logic.lampBanned.value)
                lamp.on = false;
            // (A match's SPRAY: the game's impulse 201.)
            if (input.sprayPressed)
                netGame.spray();
            if (input.lampPressed && player.state.health > 0 && lampToggle(lamp, suit))
            {
                sound.ui(kUiSoundLamp);
                // (A build that measures says how the textures stood at each
                // switch of the lamp: the lamp has been seen to leave places
                // flat on a console.)
                textureStream.report(lamp.on ? "lamp on" : "lamp off");
            }
            if (lampTick(lamp, ms))
                sound.ui(kUiSoundLamp);
            lampLit = false;
            if (lamp.on && !player.noclip)
            {
                // Where it shines: on what the middle of the view meets --
                // and with nothing within its reach, on nothing.
                const fx eye[3] = {player.state.origin[0], player.state.origin[1],
                                   player.state.origin[2] + player.state.viewHeight};
                fx end[3], at[3], normal[3];
                for (int a = 0; a < 3; ++a)
                    end[a] = eye[a] + camera.forward[a] * kLampReach;
                if (traceShotWall(world, eye, end, at, normal))
                {
                    std::int64_t along = 0;
                    for (int a = 0; a < 3; ++a)
                        along += static_cast<std::int64_t>(at[a] - eye[a]) * camera.forward[a];
                    int distance = static_cast<int>(along >> 24);
                    distance = distance < 8 ? 8 : distance;
                    LampBeam &beam = lampBeam;
                    for (int a = 0; a < 3; ++a)
                    {
                        beam.from[a] = eye[a];
                        beam.axis[a] = camera.forward[a];
                        beam.u[a] = camera.right[a];
                        beam.v[a] = camera.up[a];
                    }
                    beam.radius = lampRadius(distance);
                    beam.near = distance - beam.radius;
                    beam.reach = distance + beam.radius;
                    beam.strength = lampStrength(distance);
                    lampDistance = distance;
                    lampLit = beam.strength > 0;
                }
            }
            bottomScreen.setLamp(suit, lamp.on, lampPercent(lamp));
            hudDraw.setLamp(lamp.on, lampPercent(lamp), suit);
            hudDraw.setHurt(player.view.flash, player.view.pain);
        }
        if (!player.noclip)
        {
            const DebugLog::Perf::Scope scope(DebugLog::Section::Logic);
            {
                const DebugLog::Perf::Scope monsters(DebugLog::Section::Monsters);
#ifdef HLDS_PROFILE
                hldata::aiClock = DebugLog::Perf::now;
#endif
                monstersTick(logic, player.state, ms);
#ifdef HLDS_PROFILE
                DebugLog::Perf::add(DebugLog::Section::AiLook, hldata::aiSpent[0]);
                DebugLog::Perf::add(DebugLog::Section::AiSched, hldata::aiSpent[1]);
                DebugLog::Perf::add(DebugLog::Section::AiMove, hldata::aiSpent[2]);
                hldata::aiSpent[0] = hldata::aiSpent[1] = hldata::aiSpent[2] = 0;
#endif
            }
            // A monster's blow to the player's view (its punchangle: the
            // game's pitch is down, the view's up).
            if (ai_.punch[0] != 0 || ai_.punch[1] != 0 || ai_.punch[2] != 0)
            {
                viewKick(player.view, -ai_.punch[0], ai_.punch[1], ai_.punch[2]);
                ai_.punch[0] = ai_.punch[1] = ai_.punch[2] = 0;
            }
            const fx eye[3] = {player.state.origin[0], player.state.origin[1],
                               player.state.origin[2] + player.state.viewHeight};
            const bool alive = player.state.health > 0;
            // The crossbow's sight: the picture narrows while it is up.
            {
                const bool zoom = player.arsenal.zoomed != 0 && player.state.inv.current == kWeaponCrossbow;
                if (zoom != camera.zoomed)
                {
                    camera.zoomed = zoom;
                    camera.refreshProjection();
                }
            }
            // (A slot touched -- the next of its kind of weapon -- or a step
            // to the weapon before or after.)
            // (A weapon taken from a slot's list on the touch screen is that
            // weapon: HlWeaponList.hpp.)
            const int pick = input.weaponChoose != 0 ? input.weaponChoose
                             : input.weaponPick != 0 ? weaponOfCategory(player.state, input.weaponPick)
                                                   : weaponStepped(player.state, input.weaponStep);
            // At a gun's controls: the fire button is the gun's, and what is
            // in hand is put away (the game's holstered weapon).
            const bool manning = tankManned(logic) >= 0;
            // (In a match, the host's to say.)
            logic.noAutoaim.value = logic.matchAutoaim.value >= 0 ? logic.matchAutoaim.value == 0 : !autoaim;
            if (manning)
                tankControl(logic, player.state, camera.yaw, camera.pitch, alive && input.attack, ms);
            const std::uint32_t did = netDid = weaponsTick(logic, player.state, weapon_, eye, camera.forward,
                                                  camera.right, camera.up, alive && input.attack && !manning,
                                                  false, ms, alive && input.second && !manning, alive && !manning ? pick : 0,
                                                  alive && input.reloadPressed && !manning);
            // What has broken: its pieces, each a patch of the thing's
            // own texture, thrown from where it was.
            for (int n = 0; n < logic.brokeCount; ++n)
            {
                const LevelView &view = level.view;
                const LogicRecord &record = view.logic[logic.broke[n]];
                if (record.brush == kNoBrush || record.brush >= view.brushCount)
                    continue;
                const Brush &brush = view.brushes[record.brush];
                const Model &model = view.models[brush.model];
                const fx *pos = brushStates_[record.brush].pos;
                fx mins[3], maxs[3];
                for (int a = 0; a < 3; ++a)
                {
                    mins[a] = fxFromInt(brush.mins[a]) + pos[a] - brush.origin[a];
                    maxs[a] = fxFromInt(brush.maxs[a]) + pos[a] - brush.origin[a];
                }
                std::uint16_t textures[8];
                int textureCount = 0;
                for (int s = 0; s < model.surfaceCount && textureCount < 8; ++s)
                {
                    const std::uint16_t texture = view.surfaces[model.firstSurface + s].texture;
                    if (texture != kNoTexture)
                        textures[textureCount++] = texture;
                }
                piecesBurst(actors, mins, maxs, static_cast<int>(record.extra[1]), textures, textureCount);
                // (The marks that were on it go with it.)
                for (Decal &decal : actors.decals)
                {
                    if (decal.active && decal.brush == static_cast<std::int16_t>(record.brush))
                        decal.active = false;
                }
            }
            logic.brokeCount = 0;
            // A spent case out of the breech: to the right, up a little,
            // and it falls. (When the weapon says: at the shot for the
            // pistol and the MP5, at the pump for the shotgun -- both
            // barrels' at once.) Where and how fast, the game's
            // EV_GetDefaultShellInfo: 20 ahead, 4 right and 12 down of the
            // eye (the shotgun's 32 and 6), out at 50 to 70 to the right, 100
            // to 150 up and 25 ahead.
            const bool shotgun = inHand_ == kWeaponShotgun;
            for (int n = 0; (did & kWeaponShell) && n < weapon_.shells; ++n)
            {
                static std::uint32_t toss = 3;
                toss = toss * 1664525u + 1013904223u;
                const int ahead = shotgun ? 32 : 20, aside = shotgun ? 6 : 4;
                fx from[3], velocity[3];
                for (int a = 0; a < 3; ++a)
                {
                    from[a] = eye[a] + camera.forward[a] * ahead + camera.right[a] * aside - camera.up[a] * 12;
                    velocity[a] = player.state.velocity[a] + camera.right[a] * (50 + static_cast<int>((toss >> 9) % 21)) +
                                  camera.up[a] * (100 + static_cast<int>((toss >> 15) % 51)) + camera.forward[a] * 25;
                }
                shellEject(actors, shotgun ? kPieceShotShell : kPieceShell, from, velocity);
            }
            // A shot kicks the view, each weapon its own way.
            if (did & (kWeaponFired | kWeaponSecondary))
            {
                static std::uint32_t chance = 7;
                chance = chance * 1664525u + 1013904223u;
                viewKick(player.view, (did & kWeaponSecondary) ? 10 * kViewDegree : weaponKick(inHand_, chance >> 8));
            }
            // (The game's wpn_select is the player's choice of a weapon, heard
            // as it is asked for -- Input -- and not when one is drawn: what
            // is drawn because the last grenade has gone is silent.)
            if (player.state.inv.current != inHand_ ||
                (inHand_ == kWeaponSatchel && radio_ != satchelsOut()) ||
                (!held.active && modelCache.wantedHere()))
                takeInHand();
            // The RPG is held as it is: with its rocket in, or without.
            if (inHand_ == kWeaponRpg && held.active)
            {
                if (const ModelView *model = actorModel(actors, held))
                {
                    const bool loaded = (player.arsenal.have & kArsenalRpgLoaded) != 0;
                    const int idle = model->findClip(nameHash(loaded ? "idle" : "idle2"));
                    if (idle >= 0)
                        held.restClip = static_cast<std::int16_t>(idle);
                }
            }
            if (held.active)
            {
                // What the weapon's own code says the hand does.
                // (All of it is the weapon's own code's to say: which shot,
                // which reload, the pump, the pin, how it is held still.)
                if (weapon_.anim != nullptr)
                    actorPlay(actors, held, nameHash(weapon_.anim), true);
            }
            weapon_.anim = nullptr;
            actors.got = 0;
        }
        // At a train's controls: a press of forward or back is its speed, and
        // the player's feet stay where they are. Stepping aside or jumping
        // lets go (trainControl).
        {
            const int forward = player.noclip ? 0 : input.moveForward;
            const int step = forward != 0 && forward != trainForward_ ? forward : 0;
            trainForward_ = forward;
            if (trainDriven(logic) >= 0)
            {
                trainControl(logic, player.state, step, input.moveSide != 0, input.jump);
                if (trainDriven(logic) >= 0)
                {
                    input.moveForward = 0;
                    player.state.velocity[0] = player.state.velocity[1] = 0;
                }
            }
        }
        player.frame(vblanks);
        cameraView();
        // A game between consoles: what this player did goes out, what the
        // others did comes in (HlNetGame.hpp). And a player just put back
        // into the map looks where that place faces, with a weapon as it
        // is first taken out.
        if (netGame.playing())
        {
            netGame.tick(camera.yaw, camera.pitch, netDid, lamp.on, ms);
            NetMatch &match = netGame.match();
            if (match.respawned)
            {
                match.respawned = false;
                camera.yaw = fxAtan2Turns(player.state.facingXY[1], player.state.facingXY[0]);
                camera.pitch = 0;
                weapon_ = WeaponState();
                weapon_.seed = 777u + static_cast<std::uint32_t>(match.spawns) * 7919u;
                player.view = ViewState();
                lamp = LampState();
                lastHealth_ = player.state.health;
            }
        }
        // Their own footsteps, and what a blow sounds like from inside.
        if (!player.noclip)
        {
            // (A footstep, a rung, a slosh or a wade: HlPlayerMove.hpp.)
            const StepsHeard steps = playerSteps(player.state, ms);
            int floorLouder = 0;
            static constexpr CommonSound kFirst[] = {CommonSound::Step1, CommonSound::Ladder1, CommonSound::Slosh1,
                                                     CommonSound::Wade1};
            CommonSound first = kFirst[static_cast<int>(steps.kind)];
            if (steps.count > 0 && steps.kind == StepKind::Floor)
            {
                // What the floor is made of (looked for once a step, not
                // once a frame: HlMaterial.hpp).
                const fx feet[3] = {player.state.origin[0], player.state.origin[1],
                                    player.state.origin[2] - (player.state.ducked ? kCrouchedFeet : kStandingFeet)};
                const int on = player.state.groundMover;
                switch (floorMaterial(level.view, feet, on, on >= 0 ? logic.brushStates[on].pos : nullptr))
                {
                case Material::Metal: first = CommonSound::Metal1; break;
                case Material::Dirt:
                    first = CommonSound::Dirt1;
                    floorLouder = 5;
                    break;
                case Material::Vent:
                    first = CommonSound::Duct1;
                    floorLouder = 20;
                    break;
                case Material::Grate: first = CommonSound::Grate1; break;
                case Material::Tile:  first = CommonSound::Tile1; break;
                case Material::Slosh: first = CommonSound::Slosh1; break;
                default: break;
                }
                // (A map cooked without that step has the plain one.)
                if (static_cast<std::uint32_t>(first) >= level.view.soundCount ||
                    level.view.sounds[static_cast<int>(first)].name[0] == '\0')
                    first = CommonSound::Step1;
            }
            for (int n = steps.count; n > 0; --n)
                heard.add(static_cast<std::uint16_t>(static_cast<int>(first) + (stepCount_++ & 3)), nullptr,
                          stepVolume(steps, floorLouder), 0);
            // Into the water to the waist, or out of it: heard.
            const bool deep = player.state.waterLevel >= 2;
            if (deep != wasDeep_)
                heard.add(static_cast<std::uint16_t>(static_cast<int>(CommonSound::Wade1) + (stepCount_++ & 3)),
                          nullptr, 110, 0);
            wasDeep_ = deep;
            if (player.state.health < lastHealth_ && player.state.health > 0)
                heard.add(CommonSound::Pain, nullptr, 110);
        }
        lastHealth_ = player.state.health;
        // What is held: its clip moves on, and a shot or a reload that is over
        // goes back to being held still.
        if (held.active)
        {
            // (... with the sounds its clip has in it: a magazine out and
            // in, where the hand does it.)
            weaponHandTick(logic, held, ms);
            // (The crowbar has no way of being held still but as its last
            // swing left it, or its being taken out: the game never plays
            // it another.)
            if (held.once && held.finished && inHand_ != kWeaponCrowbar)
                actorRest(held);
        }
        {
            Inventory &inv = player.state.inv;
            // (What is thrown or put down is a count of itself; a gun, what
            // is in it and what is carried for it.)
            const std::int16_t *stock = weaponStock(player.state);
            const std::int16_t *clip = gunClip(player.state), *spare = gunSpare(player.state);
            int first = stock != nullptr ? *stock : (clip != nullptr ? *clip : 0);
            int second = stock != nullptr ? 0 : (spare != nullptr ? *spare : 0);
            // (The RPG: the rocket in it, and those carried.)
            if (inv.current == kWeaponRpg)
            {
                first = (player.arsenal.have & kArsenalRpgLoaded) != 0 ? 1 : 0;
                second = player.arsenal.rockets;
            }
            // (Every weapon's bit: the first six's, then the Arsenal's.)
            const unsigned have = inv.have | (static_cast<unsigned>(player.arsenal.have & kArsenalWeapons) << kWeaponFirstMore);
            unsigned dry = 0;
            for (int w = 1; w < kWeaponCount; ++w)
                dry |= (have & (1u << w)) && weaponDry(player.state, w) ? 1u << w : 0u;
            bottomScreen.setStatus(inv.current, first, second, have, dry,
                                   clip != nullptr && spare != nullptr && *clip < gunKind(inv.current).holds &&
                                       *spare > 0);
            hudDraw.setStatus((inv.have & kHaveSuit) != 0, player.state.health, inv.armor, inv.current, first, second,
                              inv.current == kWeaponMp5 ? inv.ammoAr : -1);
        }
        // With the eyes under, the picture dims: what there is of "under
        // water" until there is fog.
        const bool under = player.state.waterLevel == 3 && !player.noclip;
        screenFade.setDim(under);
    }
} // namespace hlrt
