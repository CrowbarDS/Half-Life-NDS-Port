// The maps the player has left, kept to come back to (HlMapStore.hpp): a
// map's state streamed to the card a piece at a time is the very state a
// save holds; laid back over the map begun afresh, it is the map as it was
// left -- a door opened is open, a monster killed lies where it fell; a map
// cooked again since is not laid back over; the oldest let go to make room;
// who went along with the player is not left behind as well.
//
// With HLDS_VALVE_DIR (the game's maps); without it, nothing is run.

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "HlMapStore.hpp"
#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    /// A folder of files in memory.
    struct MemoryCard : SaveCard
    {
        std::map<std::string, std::vector<std::uint8_t>> files;
        bool read(const char *name, std::vector<std::uint8_t> &out, std::size_t most) override
        {
            const auto at = files.find(name);
            if (at == files.end())
                return false;
            out.assign(at->second.begin(), at->second.begin() + std::min(most, at->second.size()));
            return true;
        }
        bool write(const char *name, const std::uint8_t *bytes, std::size_t size) override
        {
            files[name].assign(bytes, bytes + size);
            return true;
        }
        bool remove(const char *name) override { return files.erase(name) != 0; }
        bool rename(const char *from, const char *to) override
        {
            const auto at = files.find(from);
            if (at == files.end())
                return false;
            files[to] = at->second;
            files.erase(at);
            return true;
        }
        bool append(const char *name, const std::uint8_t *bytes, std::size_t size, bool first) override
        {
            if (first)
                files[name].clear();
            files[name].insert(files[name].end(), bytes, bytes + size);
            return true;
        }
        bool readAt(const char *name, std::size_t offset, std::size_t size, std::vector<std::uint8_t> &out) override
        {
            out.clear();
            const auto at = files.find(name);
            if (at == files.end())
                return false;
            if (offset < at->second.size())
                out.assign(at->second.begin() + static_cast<std::ptrdiff_t>(offset),
                           at->second.begin() + static_cast<std::ptrdiff_t>(std::min(offset + size, at->second.size())));
            return true;
        }
    };

    std::unique_ptr<Game> map(const char *valve, const char *name)
    {
        auto g = std::make_unique<Game>();
        std::string error;
        if (g->bsp.load(std::string(valve) + "/maps/" + name + ".bsp", error))
            finish(*g, nullptr, valve);
        else
            check(false, (std::string(name) + " loads").c_str());
        return g;
    }

    /// A while of the map going on, with the player about it.
    void play(Game &g, int ticks)
    {
        for (int tick = 0; tick < ticks; ++tick)
        {
            if (tick % 20 == 0 && g.actors.nodeCount > 0)
            {
                const PathNode &node = g.actors.nodes[(tick / 20 * 7) % g.actors.nodeCount];
                g.player.origin[0] = node.pos[0];
                g.player.origin[1] = node.pos[1];
                g.player.origin[2] = node.pos[2] + fxFromInt(37);
            }
            g.player.health = 100;
            g.frame();
        }
    }

    std::vector<std::uint8_t> everything(Game &g)
    {
        std::vector<std::uint8_t> bytes = takeMapState(g.logic, g.player, g.view.header->crc32);
        aiAppendState(bytes, g.actors);
        return bytes;
    }

    struct VectorSink
    {
        std::vector<std::uint8_t> bytes;
        static bool put(void *context, const std::uint8_t *data, std::size_t size)
        {
            auto *sink = static_cast<VectorSink *>(context);
            sink->bytes.insert(sink->bytes.end(), data, data + size);
            return true;
        }
    };
    struct VectorSource
    {
        const std::vector<std::uint8_t> *bytes;
        std::size_t at = 0;
        static std::size_t get(void *context, std::uint8_t *data, std::size_t most)
        {
            auto *source = static_cast<VectorSource *>(context);
            const std::size_t n = std::min(most, source->bytes->size() - source->at);
            std::memcpy(data, source->bytes->data() + source->at, n);
            source->at += n;
            return n;
        }
    };

    std::uint32_t storedBytes(Game &g)
    {
        const std::uint32_t map = sizeof(hldata::detail::StateHeading) +
                                  hldata::detail::stateHeading(g.logic, g.player, g.view.header->crc32).bytes;
        const std::uint32_t minds = sizeof(hldata::detail::AiStateHeading) +
                                    hldata::detail::aiStateHeading(*g.actors.ai, g.actors, aiStateCount(g.actors)).bytes;
        return map + minds;
    }

    void testStreamIsTheState(const char *valve, const char *name)
    {
        auto game = map(valve, name);
        if (!game->ok)
            return;
        Game &g = *game;
        play(g, 300);
        const std::vector<std::uint8_t> memory = everything(g);
        std::uint8_t buffer[kMapStorePiece];
        VectorSink sink;
        hldata::detail::StreamOut out;
        out.sink = VectorSink::put;
        out.context = &sink;
        out.buffer = buffer;
        out.capacity = sizeof(buffer);
        streamMapState(out, g.logic, g.player, g.view.header->crc32);
        aiStreamState(out, g.actors);
        out.flush();
        check(sink.bytes == memory && sink.bytes.size() == storedBytes(g),
              (std::string(name) + ": a state streamed a piece at a time is the state a save holds").c_str());

        auto fresh = map(valve, name);
        VectorSource source{&sink.bytes};
        hldata::detail::StreamIn in;
        in.source = VectorSource::get;
        in.context = &source;
        in.buffer = buffer;
        in.capacity = sizeof(buffer);
        PlayerState left = fresh->player;
        const bool laid = layMapStateStream(in, fresh->logic, left, fresh->view.header->crc32) &&
                          aiLayStream(in, fresh->actors);
        fresh->player = left;
        check(laid && everything(*fresh) == memory,
              (std::string(name) + ": ... and laid back from the stream, the map is as it was").c_str());
    }

    void testComeBack(const char *valve)
    {
        auto game = map(valve, "c1a1");
        if (!game->ok)
            return;
        Game &g = *game;
        // A door opened, a monster dead, a while gone by.
        int door = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount && door < 0; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Door) && r.brush != kNoBrush &&
                !(r.flags & kLogicStartOpen) && r.waitMs < 0)
                door = static_cast<int>(i);
        }
        int monster = -1;
        for (int i = 0; i < g.actors.count && monster < 0; ++i)
        {
            const ActorState &a = g.actors.states[i];
            if (a.active && fights(a.kind) && canBeHurt(a.kind))
                monster = i;
        }
        check(door >= 0 && monster >= 0, "c1a1 has a door that stays open and a monster");
        if (door < 0 || monster < 0)
            return;
        g.player.origin[0] = fxFromInt(-4000); // (out of everyone's way)
        logicUse(g.logic, g.player, door, -1, kUseToggle);
        // (Shot down, a few rounds at a time: a body, not gibs.)
        for (int n = 0; n < 40 && g.actors.states[monster].ai < kAiDying; ++n)
        {
            monsterHurt(g.logic, monster, 8, true);
            g.frame();
        }
        play(g, 200);
        const fx doorAt[3] = {g.brushStates[g.view.logic[door].brush].pos[0],
                              g.brushStates[g.view.logic[door].brush].pos[1],
                              g.brushStates[g.view.logic[door].brush].pos[2]};
        const ActorState body = g.actors.states[monster];
        check(g.logic.states[door].move == MoveState::AtTop && body.ai >= kAiDying, "... the door is open, the monster down");

        // Left: kept on the card, with someone who went along.
        MemoryCard card;
        RunIndex run;
        std::uint8_t buffer[kMapStorePiece];
        const std::int16_t went[kMaxVisitors] = {static_cast<std::int16_t>(monster == 0 ? 1 : 0), -1, -1, -1};
        const bool stored = runStore(card, run, "c1a1", g.view.header->crc32, storedBytes(g), went, buffer,
                                     [&](hldata::detail::StreamOut &out)
                                     {
                                         streamMapState(out, g.logic, g.player, g.view.header->crc32);
                                         aiStreamState(out, g.actors);
                                     });
        check(stored && run.count == 1 && card.files.count("run/c1a1.mst") == 1 && card.files.count(kRunIndexFile) == 1,
              "left, the map is kept on the card, and the run's index with it");
        RunIndex again;
        check(runIndexRead(card, again) && again.count == 1 && std::strcmp(again.entries[0].name, "c1a1") == 0,
              "... the index reads back");

        // Come back to: begun afresh, then laid over; the one who went along
        // (and got there) is not in it.
        auto back = map(valve, "c1a1");
        std::vector<std::uint8_t> scratch;
        run.entries[0].landed = 1;
        check(runCheck(card, run.entries[0], back->view.header->crc32, scratch), "... whole, and of this map");
        const int other = went[0];
        const bool wasThere = back->actors.states[other].active;
        const bool laid = runLay(card, run.entries[0], scratch, buffer,
                                 [&](hldata::detail::StreamIn &in)
                                 {
                                     PlayerState left = back->player;
                                     const bool ok = layMapStateStream(in, back->logic, left, back->view.header->crc32);
                                     aiLayStream(in, back->actors);
                                     runSendAway(back->actors, run.entries[0].sentAway, run.entries[0].landed);
                                     return ok;
                                 });
        check(laid, "... and laid back over the map begun afresh");
        const BrushState &doorBack = back->brushStates[back->view.logic[door].brush];
        check(doorBack.pos[0] == doorAt[0] && doorBack.pos[1] == doorAt[1] && doorBack.pos[2] == doorAt[2] &&
                  back->logic.states[door].move == MoveState::AtTop,
              "come back to: the door opened is open");
        const ActorState &bodyBack = back->actors.states[monster];
        check(bodyBack.active && bodyBack.ai >= kAiDying && bodyBack.pos[0] == body.pos[0] && bodyBack.pos[1] == body.pos[1],
              "... the monster killed lies where it fell");
        check(!wasThere || !back->actors.states[other].active, "... and who went along with the player is not here");

        // A save made now has the map left in it; loaded on a card whose
        // folder was emptied since, the folder is as the save had it.
        {
            SaveIndex list;
            SaveData save;
            save.map = "c1a1a";
            SaveHeading heading;
            check(saveSlotWrite(card, list, kSlotQuick, save, heading, &run, false),
                  "a save is made with the map left in it");
            SaveData read;
            check(saveSlotRead(card, kSlotQuick, read) && read.visited.size() == 1 && !read.visitedByRef &&
                      std::strcmp(read.visited[0].name, "c1a1") == 0,
                  "... read back, it lists it");
            check(saveFileWhole(card, saveSlotFile(kSlotQuick)), "... and the file is whole, the map's file in it too");
            RunIndex emptied;
            runClear(card, emptied, 5);
            runFromSave(card, kSlotQuick, read, emptied, 6, scratch);
            check(emptied.count == 1 && emptied.runId == 6 && runCheck(card, emptied.entries[0], back->view.header->crc32, scratch),
                  "... and loaded, the map left is back in the run's folder, whole");
            // The one made coming into a map only names the folder.
            check(saveSlotWrite(card, list, kSlotArrival, save, heading, &emptied, true), "the arrival save names the folder");
            SaveData arrival;
            check(saveSlotRead(card, kSlotArrival, arrival) && arrival.visitedByRef && arrival.visitedRunId == 6,
                  "... read back, by reference");
            runFromSave(card, kSlotArrival, arrival, emptied, 7, scratch);
            check(emptied.count == 1, "... and loaded on the same run, the folder is kept as it is");
            // The game's own save names the folder too, and can be let go of
            // though it is not the player's to delete.
            check(saveSlotWrite(card, list, kSlotAuto, save, heading, &emptied, true), "the game's own save names the folder");
            SaveData own;
            check(saveSlotRead(card, kSlotAuto, own) && own.visitedByRef && own.visitedRunId == emptied.runId,
                  "... read back, by reference");
            const std::size_t ownBytes = card.files.at(saveSlotFile(kSlotAuto)).size();
            check(saveSlotWrite(card, list, kSlotManual, save, heading, &emptied) &&
                      card.files.at(saveSlotFile(kSlotManual)).size() > ownBytes,
                  "... smaller than a save holding the maps left");
            RunIndex same = emptied;
            runFromSave(card, kSlotAuto, own, same, 77, scratch);
            check(same.count == emptied.count && same.runId == emptied.runId, "... loaded on the same folder, it stays");
            check(!saveSlotDelete(card, list, kSlotAuto), "the player cannot delete it");
            check(saveSlotForget(card, list, kSlotAuto) && list.slots[kSlotAuto].status != kSaveGood &&
                      card.files.count(saveSlotFile(kSlotAuto)) == 0,
                  "... the game can");
            RunIndex other = emptied;
            other.runId = 99;
            runFromSave(card, kSlotArrival, arrival, other, 8, scratch);
            check(other.count == 0, "... on another run, nothing of it is");
            // (... which let go of the folder's files: back from the save.)
            RunIndex restored;
            runFromSave(card, kSlotQuick, read, restored, 9, scratch);
            check(restored.count == 1 && card.files.count("run/c1a1.mst") == 1,
                  "... and back again from the save that holds them");
            run = restored;
            // A map file spoilt on the card spoils a save made with it: the
            // old one is kept.
            card.files["run/c1a1.mst"][200] ^= 0x33;
            check(!saveSlotWrite(card, list, kSlotQuick, save, heading, &run, false) &&
                      saveFileWhole(card, saveSlotFile(kSlotQuick)),
                  "a save of a spoilt map file fails, and the save before it stays");
            card.files["run/c1a1.mst"][200] ^= 0x33;
        }

        // A map cooked again since: not laid over.
        check(!runCheck(card, run.entries[0], back->view.header->crc32 ^ 1u, scratch),
              "a map cooked again since is not laid back over");
        // A file spoilt on the card: not either.
        card.files["run/c1a1.mst"][100] ^= 0x55;
        check(!runCheck(card, run.entries[0], back->view.header->crc32, scratch), "... nor a file spoilt on the card");

        // The oldest let go to make room.
        for (int n = 0; n < kStoredMapsMost + 2; ++n)
        {
            char name[16];
            std::snprintf(name, sizeof(name), "m%d", n);
            runStore(card, run, name, 1, 100, nullptr, buffer,
                     [](hldata::detail::StreamOut &out)
                     {
                         std::uint8_t zero[100] = {};
                         out.put(zero, sizeof(zero));
                     });
        }
        check(run.count == kStoredMapsMost && runFind(run, "c1a1") < 0 && runFind(run, "m0") < 0 && runFind(run, "m1") < 0 &&
                  runFind(run, "m13") >= 0 && card.files.count("run/m0.mst") == 0,
              "more maps left than are kept: the oldest are let go, files and all");
        runClear(card, run, 77);
        check(run.count == 0 && run.runId == 77 && card.files.count("run/m13.mst") == 0, "a new run keeps nothing");
    }
} // namespace

int main()
{
    const char *valve = std::getenv("HLDS_VALVE_DIR");
    if (valve == nullptr || *valve == '\0')
    {
        std::printf("test_revisit: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 77;
    }
    for (const char *name : {"c1a0", "c1a1", "c2a2d", "c4a1"})
        testStreamIsTheState(valve, name);
    testComeBack(valve);
    if (g_failures == 0)
        std::printf("test_revisit: ok\n");
    return g_failures == 0 ? 0 : 1;
}
