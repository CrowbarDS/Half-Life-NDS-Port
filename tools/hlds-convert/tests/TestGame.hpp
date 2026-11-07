#pragma once

// A cooked map set up the way the DS sets it up, for the tests that play one:
// its collision, its logic, its models and its actors.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "HlLevelReader.hpp"
#include "HlMonsters.hpp"
#include "HlTransition.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/NodGraph.hpp"
#include "hlds/Cooker.hpp"
#include "hlds/EntityCook.hpp"
#include "hlds/EntityParser.hpp"
#include "hlds/MdlFile.hpp"
#include "hlds/ModelCook.hpp"

namespace hldstest
{
    using namespace hlds;
    using namespace hldata;

    /// A cooked map with the logic set up the way the DS sets it up.
    struct Game
    {
        BspFile bsp;
        std::vector<std::uint8_t> bytes;
        LevelView view;
        MoveWorld move;
        std::vector<MoverHull> movers;
        std::vector<MoverReach> reach; ///< MoveWorld::reach
        std::vector<BrushState> brushStates;
        std::vector<LogicState> states;
        std::vector<LadderVolume> ladders;
        std::vector<std::uint8_t> cornerState;
        // The map's models, cooked as the converter cooks them, and its actors.
        std::vector<std::vector<std::uint32_t>> modelBlocks;
        /// Models to cook as if they had been, with no file behind them: what
        /// the level keeps for a scientist is there without a scientist's model.
        std::vector<std::string> pretendModels;
        std::vector<ModelView> modelViews;
        std::vector<ActorState> actorStates;
        std::vector<SolidBox> actorBoxes;
        std::vector<std::uint8_t> linkShut; ///< ActorWorld::linkShut
        /// What the monsters think and hear (HlAi.hpp), as the DS keeps it.
        AiWorld ai;
        std::vector<AiMonster> aiMonsters;
        std::vector<std::int16_t> aiSlots;
        std::vector<std::uint8_t> aiHeld;
        void attachAi()
        {
            const int thinkers = aiNeeded(actors);
            aiMonsters.assign(thinkers > 0 ? thinkers : 1, AiMonster());
            aiSlots.assign(actors.count > 0 ? actors.count : 1, -1);
            aiHeld.assign(actors.count > 0 ? actors.count : 1, 0);
            aiAttach(actors, ai, aiMonsters.data(), thinkers, aiSlots.data(), aiHeld.data(), actors.count);
        }
        /// The game's own graph of the map's nodes, if the test read one
        /// before finish(): the ways between them are then taken from it.
        hlds::NodGraph nodGraph;
        ActorWorld actors;
        LogicWorld logic;
        PlayerState player;
        bool ok = false;
        /// What the map has made heard: the queue the logic fills, and how
        /// often each sound (by its index in the level's Sounds) has been in
        /// it since the game began. drain() moves one to the other.
        SoundQueue heard;
        std::vector<int> timesHeard;
        /// ... the loudest each has been put in (0..127), and of the lines
        /// said by someone somewhere: heard less the further off (not the
        /// suit's, at the player, nor a speaker's, heard everywhere).
        std::vector<int> loudestHeard;
        int sentencesHeard = 0, loudestSentence = 0;
        /// Every line said (its index in the level's Sentences), in order.
        std::vector<std::uint16_t> said;
        void drain()
        {
            for (int i = 0; i < heard.count; ++i)
            {
                const SoundEvent &event = heard.events[i];
                if (event.flags & kSoundSentence)
                {
                    ++sentencesHeard;
                    said.push_back(event.sound);
                    if (!(event.flags & kSoundHere) && event.radius != kRadiusEverywhere)
                        loudestSentence = std::max(loudestSentence, static_cast<int>(event.volume));
                }
                else if (event.sound < timesHeard.size())
                {
                    ++timesHeard[event.sound];
                    loudestHeard[event.sound] = std::max(loudestHeard[event.sound], static_cast<int>(event.volume));
                }
            }
            heard.count = 0;
        }
        int times(CommonSound sound) const
        {
            return static_cast<std::size_t>(sound) < timesHeard.size()
                       ? timesHeard[static_cast<std::size_t>(sound)]
                       : 0;
        }

        int recordOfKind(LogicKind kind, int nth = 0) const
        {
            for (std::uint32_t i = 0; i < view.logicCount; ++i)
            {
                if (view.logic[i].kind == static_cast<std::uint8_t>(kind) && nth-- == 0)
                    return static_cast<int>(i);
            }
            return -1;
        }
        /// The tram: the track train that is known by name across maps (the
        /// forklifts and the flatbed of the ride are track trains too).
        int tram() const
        {
            for (std::uint32_t i = 0; i < view.logicCount; ++i)
            {
                if (view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::TrackTrain) &&
                    view.logic[i].extra[2] != 0)
                    return static_cast<int>(i);
            }
            return -1;
        }
        void frame()
        {
            logicTick(logic, player, 33);
            logic.viewTurn = 0;
            monstersTick(logic, player, 33);
            playerMove(player, MoveCommand(), move, kPlayerTick);
            drain();
        }
    };

    /// A game's folder of a test's own, there for as long as this is: the two
    /// lists the cook reads, made up by the test (no Valve data is in this
    /// repository), and no models -- cookModels passes over what is missing.
    /// Give finish() its dir as modelsFrom.
    struct FakeGame
    {
        std::filesystem::path root;
        std::string dir;
        FakeGame(const char *name, const std::string &sentences, const std::string &titles)
            : root(std::filesystem::temp_directory_path() / (std::string("hlds_fake_game_") + name)), dir(root.string())
        {
            std::filesystem::remove_all(root);
            std::filesystem::create_directories(root / "sound");
            std::ofstream(root / "sound" / "sentences.txt", std::ios::binary) << sentences;
            std::ofstream(root / "titles.txt", std::ios::binary) << titles;
        }
        ~FakeGame()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    };

    /// Cook the models `g`'s map needs, the way the converter does: its own
    /// actors' and those of whoever may walk in from next door.
    inline void cookModels(Game &g, const char *dir, CookOptions &options)
    {
        std::vector<Entity> entities;
        std::string error;
        if (dir == nullptr || !parseEntities(g.bsp.entities, entities, error))
            return;
        standInEntities(entities);
        std::vector<ModelNeed> needs = collectModelNeeds(entities);
        addMultiplayerNeeds(entities, needs);
        for (const Entity &entity : entities)
        {
            if (entity.className() != "trigger_changelevel")
                continue;
            BspFile other;
            std::vector<Entity> otherEntities;
            std::string map(entity.value("map"));
            for (char &c : map)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (other.load(std::string(dir) + "/maps/" + map + ".bsp", error) &&
                parseEntities(other.entities, otherEntities, error))
                addVisitorNeeds(entities, otherEntities, std::string(entity.value("landmark")), needs);
        }
        // (The casings out of the monsters' guns; the player's weapons are
        // the converter's to know.)
        addCasingNeeds(false, needs);
        addGrenadeNeeds(false, false, needs);
        for (const ModelNeed &need : needs)
        {
            MdlFile mdl;
            CookedModel cooked;
            ModelCookReport report;
            std::vector<std::uint8_t> bytes;
            std::vector<std::string> clips, clipsDsi;
            if (!mdl.load(std::string(dir) + "/" + need.file, error))
                continue;
            // (The DS's clips; HLDS_TEST_DSI=1: a DSi's.)
            modelClips(mdl, need, clips, clipsDsi);
            if (std::getenv("HLDS_TEST_DSI") != nullptr)
                clips = clipsDsi;
            if (!cookModel(mdl, clips, cooked, report, error) || !writeModel(cooked, bytes, error))
                continue;
            g.modelBlocks.emplace_back((bytes.size() + 3) / 4);
            std::memcpy(g.modelBlocks.back().data(), bytes.data(), bytes.size());
            ModelView view;
            view.open(reinterpret_cast<const std::uint8_t *>(g.modelBlocks.back().data()),
                      static_cast<std::uint32_t>(bytes.size()));
            g.modelViews.push_back(view);
            options.models.push_back(need.name);
        }
    }

    inline void finish(Game &g, Globals *globals, const char *modelsFrom = nullptr)
    {
        std::string error;
        CookedLevel level;
        CookReport report;
        CookOptions cookOptions;
        cookModels(g, modelsFrom, cookOptions);
        cookOptions.models.insert(cookOptions.models.end(), g.pretendModels.begin(), g.pretendModels.end());
        // What the map's people say, if the game's list is there to be read.
        // (Once for each folder: a made-up game's must not stand for the real
        // one's in the same run.)
        static SentenceBook book;
        static std::string bookFrom;
        if (modelsFrom != nullptr && bookFrom != modelsFrom)
        {
            bookFrom = modelsFrom;
            std::ifstream file(std::string(modelsFrom) + "/sound/sentences.txt", std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
            book = parseSentences(text);
        }
        cookOptions.sentences = modelsFrom != nullptr ? &book : nullptr;
        if (!g.nodGraph.nodes.empty())
            cookOptions.nodGraph = &g.nodGraph;
        // ... and what it writes on the screen.
        static TitlesBook titles;
        static std::string titlesFrom;
        if (modelsFrom != nullptr && titlesFrom != modelsFrom)
        {
            titlesFrom = modelsFrom;
            std::ifstream file(std::string(modelsFrom) + "/titles.txt", std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
            titles = parseTitles(text);
        }
        cookOptions.titles = modelsFrom != nullptr ? &titles : nullptr;
        g.ok = cookLevel(g.bsp, cookOptions, TexturePlan(), level, report, error) &&
               writeLevel(level, g.bytes, error) &&
               g.view.open(g.bytes.data(), static_cast<std::uint32_t>(g.bytes.size())) ==
                   LevelError::Ok;
        if (!g.ok)
        {
            std::fprintf(stderr, "game failed: %s\n", error.c_str());
            return;
        }
        const LevelView &v = g.view;
        g.move.standing = Hull{v.clipNodes, v.planes, v.planeTypes, v.models[0].clipStanding};
        g.move.crouched = Hull{v.clipNodes, v.planes, v.planeTypes, v.models[0].clipCrouched};
        g.move.large = Hull{v.clipNodes, v.planes, v.planeTypes, v.models[0].clipLarge};
        g.movers.resize(v.brushCount);
        g.reach.assign(v.brushCount + 1, MoverReach());
        g.brushStates.resize(v.brushCount);
        g.states.resize(v.logicCount);
        g.cornerState.resize(v.cornerCount + 1);
        for (std::uint32_t i = 0; i < v.brushCount; ++i)
        {
            const Model &model = v.models[v.brushes[i].model];
            g.movers[i].standing = Hull{v.clipNodes, v.planes, v.planeTypes, model.clipStanding};
            g.movers[i].crouched = Hull{v.clipNodes, v.planes, v.planeTypes, model.clipCrouched};
        }
        g.move.nodes = v.nodes;
        g.move.leaves = v.leaves;
        g.move.headNode = v.models[0].headNode;
        g.move.vis = v.vis;
        g.move.visSize = v.visSize;
        g.move.visLeafCount = v.header->visLeafCount;
        g.move.movers = g.movers.data();
        g.move.reach = g.reach.data();
        g.move.moverCount = static_cast<int>(g.movers.size());
        int ladderCount = 0;
        for (std::uint32_t i = 0; i < v.logicCount; ++i)
            ladderCount += v.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Ladder);
        g.ladders.resize(ladderCount + 1);
        g.move.ladders = g.ladders.data();
        g.move.ladderCount = ladderCount;

        g.logic.records = v.logic;
        g.logic.strings.value = v.strings;
        g.logic.recordCount = static_cast<int>(v.logicCount);
        g.logic.aux = v.logicAux;
        g.logic.corners = v.corners;
        g.logic.brushes = v.brushes;
        g.logic.brushCount = static_cast<int>(v.brushCount);
        g.logic.models.value = v.models;
        g.logic.modelCount.value = static_cast<int>(v.modelCount);
        g.logic.states = g.states.data();
        g.logic.brushStates = g.brushStates.data();
        g.logic.movers = g.movers.data();
        g.logic.world = &g.move;
        g.logic.ladders = g.ladders.data();
        g.logic.globals = globals;
        g.logic.sounds = v.logicSoundCount != 0 ? v.logicSounds : nullptr;
        g.logic.heard = &g.heard;
        g.logic.level = &v;
        g.logic.modelSounds = v.modelSoundCount != 0 ? v.modelSounds : nullptr;
        g.logic.modelSoundCount = static_cast<int>(v.modelSoundCount);
        g.timesHeard.assign(v.soundCount + 1, 0);
        g.loudestHeard.assign(v.soundCount + 1, 0);
        g.logic.cornerState = g.cornerState.data();
        g.logic.cornerCount = static_cast<int>(v.cornerCount);
        g.actorStates.assign(v.actorCount + spareActorsOf(v), ActorState());
        g.actorBoxes.assign(v.actorCount + spareActorsOf(v), SolidBox());
        g.actors.states = g.actorStates.data();
        g.actors.count = static_cast<int>(g.actorStates.size());
        g.actors.models = g.modelViews.data();
        g.actors.modelCount = static_cast<int>(g.modelViews.size());
        gibModelsFind(g.actors, v.modelRefs, static_cast<int>(v.modelRefCount));
        g.actors.boxes = g.actorBoxes.data();
        g.move.boxes = g.actorBoxes.data();
        g.move.boxCount = static_cast<int>(g.actorBoxes.size());
        g.actors.nodes = v.pathNodes;
        g.actors.links = v.pathLinks;
        g.actors.nodeCount = static_cast<int>(v.pathNodeCount);
        g.actors.linkCount = static_cast<int>(v.pathLinkCount);
        g.linkShut.assign(v.pathLinkCount + 1, 0);
        g.actors.linkShut = g.linkShut.data();
        g.logic.actors = &g.actors;
        g.logic.nameHashes = v.nameHashes;
        g.logic.sprites = v.sprites;
        g.logic.speech = v.speechCount != 0 ? v.speech : nullptr;
        g.logic.spriteCount = static_cast<int>(v.spriteCount);
        g.logic.nameCount = static_cast<int>(v.nameCount);
        logicInit(g.logic);
        moverReachAll(g.move);
        actorsInit(g.actors, v.actors, static_cast<int>(v.actorCount), v.nameHashes, g.move);
        g.attachAi();
        g.ai.speechCount = static_cast<int>(v.speechCount);

        g.player = PlayerState();
        for (std::uint32_t i = 0; i < v.entityCount; ++i)
        {
            if (v.entities[i].classId != static_cast<std::uint16_t>(EntityClass::InfoPlayerStart))
                continue;
            for (int a = 0; a < 3; ++a)
                g.player.origin[a] = v.entities[i].origin[a];
            break;
        }
    }
} // namespace hldstest
