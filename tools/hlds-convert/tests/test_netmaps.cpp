// What the menus offer for a match (shared/HlNetMaps.hpp), a map's picture
// for its card (HlPreview.hpp), and where that picture is taken from: the
// map's info_intermission, kept by the converter, facing what it looks at.

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "HlNetGame.hpp"
#include "HlNetMaps.hpp"
#include "HlPreview.hpp"
#include "TestGame.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    void testMaps()
    {
        std::vector<NetMapInfo> maps = {{"boot_camp", 17, false}, {"bounce", 10, true}, {"crossfire", 20, true},
                                        {"rustmill", 4, false}};
        check(netMapOffered(maps[0], false) && !netMapOffered(maps[0], true) && netMapOffered(maps[1], true),
              "every map is offered; with consoles without the game, only those that fit them");
        check(netMapStep(maps, 1, 1, true) == 2 && netMapStep(maps, 2, 1, true) == 1 && netMapStep(maps, 1, -1, true) == 2,
              "stepping through them with consoles without the game passes over the rest, round and round");
        check(netMapStep(maps, 3, 1, false) == 0, "... and without, every one");
        check(netMapKept(maps, 0, true) == 1 && netMapKept(maps, 2, true) == 2,
              "a map that does not fit, once they are let in, gives way to the first that does");
        check(netAnyGuestMap(maps), "a card with some that fit has some to offer");
        std::vector<NetMapInfo> none = {{"boot_camp", 17, false}};
        check(!netAnyGuestMap(none) && netMapKept(none, 0, true) == -1 && netMapStep(none, 0, 1, true) == -1,
              "... and one with none has none: nothing to choose");
        check(netMapPlayers(maps[0]) == kNetPlayers && netMapPlayers(maps[3]) == 4 &&
                  netMapPlayers(NetMapInfo{"x", 1, false}) == 2,
              "a map is for as many as it has places, no more than a match has, and two at the least");
        check(netModeOffered(kNetModeDeathmatch, true) && netModeOffered(kNetModeTeamplay, false) && !netModeOffered(2, false),
              "deathmatch and teamplay are played; co-op is not yet");

        // How far up its memory the heap of a console sent the game reaches
        // (the Room event, in KB) is what fits it: a DSi's 8 MB takes the
        // big maps a DS's 4 MB does not; none said, the DS's answer.
        std::vector<NetMapInfo> sized = {{"boot_camp", 17, false, 1, 4400}, {"bounce", 10, true, 1, 3300},
                                         {"crossfire", 20, true, 1, 3600}, {"rustmill", 4, false, 1, 4000},
                                         {"nothing", 4, false, 0, 0}};
        check(netMapOffered(sized[0], true, 7800) && !netMapOffered(sized[0], true, 3700) &&
                  !netMapOffered(sized[0], true, 0) && netMapOffered(sized[1], true, 0),
              "a map fits a console sent the game that has room for it; none said, the DS's answer");
        check(netMapOffered(sized[3], true, 4000) && !netMapOffered(sized[3], true, 3999) && !netMapOffered(sized[4], true, 99999),
              "... exactly as much fits, a byte less does not, and a map never sent fits no room");
        check(netMapStep(sized, 1, 1, true, 3700) == 2 && netMapStep(sized, 2, 1, true, 3700) == 1 &&
                  netMapStep(sized, 1, 1, true, 7800) == 2 && netMapStep(sized, 2, 1, true, 7800) == 3,
              "stepping through the maps passes over what the least room there cannot hold");
        check(netMapKept(sized, 0, true, 3700) == 1 && netMapKept(sized, 0, true, 7800) == 0,
              "a chosen map too big for a console that comes gives way; with the room, it stays");
        check(netAnyGuestMap(sized, 3000) == false && netAnyGuestMap(sized, 3300),
              "... and with too little room for any, there is none to choose");
    }

    void testPreview()
    {
        // A screen of four colours, each 2x2 block one of two: the picture
        // is their average.
        std::vector<std::uint16_t> screen(256 * 192);
        for (int y = 0; y < 192; ++y)
            for (int x = 0; x < 256; ++x)
                screen[y * 256 + x] = static_cast<std::uint16_t>(((x + y) & 1) ? 30 : (10 << 10) | (20 << 5)) | 0x8000;
        std::vector<std::uint16_t> out(kPreviewWidth * kPreviewHeight);
        previewShrink(screen.data(), out.data());
        const std::uint16_t c = out[37 * kPreviewWidth + 50];
        check((c & 31) == 15 && ((c >> 5) & 31) == 10 && ((c >> 10) & 31) == 5 && (c & 0x8000),
              "a picture is the screen shrunk by half: each pixel the average of its four");
        // A half-and-half screen keeps its halves.
        for (int y = 0; y < 192; ++y)
            for (int x = 0; x < 256; ++x)
                screen[y * 256 + x] = static_cast<std::uint16_t>(x < 128 ? 31 : 31 << 10);
        previewShrink(screen.data(), out.data());
        check((out[10 * kPreviewWidth + 63] & 0x7FFF) == 31 && (out[10 * kPreviewWidth + 64] & 0x7FFF) == (31 << 10),
              "... its left half the screen's left, its right the right");
        std::vector<std::uint8_t> file(kPreviewBytes);
        PreviewHeader header;
        previewHeader(header);
        std::memcpy(file.data(), &header, sizeof(header));
        check(previewValid(file.data(), file.size()), "a picture's file reads back as one");
        check(!previewValid(file.data(), file.size() - 2), "... and one short of it does not");
        file[4] = 9;
        check(!previewValid(file.data(), file.size()), "... nor one of another version");
    }

    std::string entity(const std::string &keys) { return "{\n" + keys + "}\n"; }

    std::unique_ptr<Game> cook(const BoxRoom &room)
    {
        auto g = std::make_unique<Game>();
        std::string error;
        g->ok = g->bsp.parse(buildBoxRoom(room), error);
        if (g->ok)
            finish(*g, nullptr);
        return g;
    }

    /// The same on rooms made here: four places to be shown from, each
    /// facing as it says or at what it targets; two to come in at; neither.
    void testRoomIntermission()
    {
        BoxRoom room;
        room.extraEntities =
            entity("\"classname\" \"info_intermission\"\n\"origin\" \"-90 0 64\"\n\"angles\" \"10 90 0\"\n") +
            entity("\"classname\" \"info_intermission\"\n\"origin\" \"-30 0 64\"\n\"angle\" \"180\"\n") +
            entity("\"classname\" \"info_intermission\"\n\"origin\" \"30 0 64\"\n\"angles\" \"33 33 0\"\n"
                   "\"target\" \"look\"\n") +
            entity("\"classname\" \"info_intermission\"\n\"origin\" \"90 0 64\"\n") +
            entity("\"classname\" \"info_target\"\n\"targetname\" \"look\"\n\"origin\" \"30 100 164\"\n");
        auto game = cook(room);
        check(game->ok, "the room of four places cooks");
        if (!game->ok)
            return;
        const LevelView &v = game->view;
        std::vector<const EntityRecord *> kept;
        for (std::uint32_t e = 0; e < v.entityCount; ++e)
        {
            if (v.entities[e].classId == static_cast<std::uint16_t>(EntityClass::InfoIntermission))
                kept.push_back(&v.entities[e]);
        }
        check(kept.size() == 4, "the four places the map shows itself from are kept");
        if (kept.size() != 4)
            return;
        check(kept[0]->origin[0] == fxFromInt(-90) && kept[0]->origin[2] == fxFromInt(64) &&
                  kept[0]->angles[0] == angleFromDegrees(10) && kept[0]->angles[1] == angleFromDegrees(90),
              "... each where it is, facing the way it says");
        check(kept[1]->angles[1] == angleFromDegrees(180), "... or by the older word for it, a yaw");
        // (From 30 0 64 to 30 100 164: round to +y, up by an eighth of a turn.)
        check(kept[2]->angles[1] == angleFromDegrees(90) && kept[2]->angles[0] == angleFromDegrees(-45),
              "... and one that targets something looks at it, whatever its angles say");
        // Which is shown: by the draw every console was told, each in its turn.
        bool spawn = true;
        const EntityRecord *points[4];
        for (std::uint32_t draw = 0; draw < 4; ++draw)
            points[draw] = netIntermissionPoint(v, draw, spawn);
        check(!spawn && points[0] == kept[0] && points[1] == kept[1] && points[2] == kept[2] && points[3] == kept[3],
              "each draw shows another of its four");
        check(netIntermissionPoint(v, 6, spawn) == kept[2] && netIntermissionPoint(v, 0xFFFF, spawn) == kept[3],
              "... the same draw the same one, on every console");

        // A map with none: its places to come in at, the first of them.
        BoxRoom spawns;
        spawns.extraEntities = entity("\"classname\" \"info_player_deathmatch\"\n\"origin\" \"64 0 40\"\n") +
                               entity("\"classname\" \"info_player_deathmatch\"\n\"origin\" \"-64 0 40\"\n");
        auto second = cook(spawns);
        check(second->ok, "the room of places to come in at cooks");
        if (second->ok)
        {
            const EntityRecord *point = netIntermissionPoint(second->view, 1234, spawn);
            check(spawn && point != nullptr &&
                      point->classId == static_cast<std::uint16_t>(EntityClass::InfoPlayerDeathmatch) &&
                      point->origin[0] == fxFromInt(64),
                  "with none, it is shown from where a player comes in");
        }
        // Neither: nothing to show it from.
        auto third = cook(BoxRoom());
        check(third->ok, "the plain room cooks");
        if (third->ok)
        {
            spawn = true;
            check(netIntermissionPoint(third->view, 3, spawn) == nullptr && !spawn, "with neither, there is nowhere");
        }
    }

    void testIntermission(const char *dir)
    {
        auto g = std::make_unique<Game>();
        std::string error;
        if (!g->bsp.load(std::string(dir) + "/maps/stalkyard.bsp", error))
        {
            check(false, "stalkyard loads");
            return;
        }
        finish(*g, nullptr, dir);
        const LevelView &v = g->view;
        int count = 0;
        bool turned = false;
        for (std::uint32_t e = 0; e < v.entityCount; ++e)
        {
            if (v.entities[e].classId != static_cast<std::uint16_t>(EntityClass::InfoIntermission))
                continue;
            ++count;
            turned = turned || v.entities[e].angles[0] != 0 || v.entities[e].angles[1] != 0;
        }
        check(count == 4, "stalkyard's four places the game shows it from are kept");
        check(turned, "... each facing the way it looks");
        // Which is shown: by the draw every console was told, each in its turn.
        {
            bool spawn = true;
            const EntityRecord *points[4];
            for (std::uint32_t draw = 0; draw < 4; ++draw)
                points[draw] = netIntermissionPoint(v, draw, spawn);
            check(!spawn && points[0] != nullptr && points[0] != points[1] && points[1] != points[2] && points[2] != points[3],
                  "each draw shows another of its four");
            check(netIntermissionPoint(v, 6, spawn) == points[2] && netIntermissionPoint(v, 0xFFFF, spawn) == points[3],
                  "... the same draw the same one, on every console");
        }
        // A map with none: its places to come in at, as before.
        auto c = std::make_unique<Game>();
        if (c->bsp.load(std::string(dir) + "/maps/crossfire.bsp", error))
        {
            finish(*c, nullptr, dir);
            int intermissions = 0, spawns = 0;
            for (std::uint32_t e = 0; e < c->view.entityCount; ++e)
            {
                intermissions += c->view.entities[e].classId == static_cast<std::uint16_t>(EntityClass::InfoIntermission);
                spawns += c->view.entities[e].classId == static_cast<std::uint16_t>(EntityClass::InfoPlayerDeathmatch);
            }
            check(intermissions == 0 && spawns > 0, "crossfire has none: its picture is from where a player comes in");
            bool spawn = false;
            const EntityRecord *point = netIntermissionPoint(c->view, 1234, spawn);
            check(spawn && point != nullptr &&
                      point->classId == static_cast<std::uint16_t>(EntityClass::InfoPlayerDeathmatch),
                  "... and is shown from there between matches");
        }
        else
        {
            check(false, "crossfire loads");
        }
    }
} // namespace

int main()
{
    testMaps();
    testPreview();
    testRoomIntermission();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
        testIntermission(dir);
    else
        std::printf("test_netmaps: HLDS_VALVE_DIR not set, the game's maps were not read\n");
    if (g_failures == 0)
        std::printf("test_netmaps: ok\n");
    return g_failures == 0 ? 0 : 1;
}
