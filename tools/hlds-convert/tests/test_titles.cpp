// What the game writes on the screen (hlds/TitlesFile.hpp, the level's
// Messages, LogicKind::Message): its titles.txt read, a map's env_messages
// cooked with what they say and how, and shown when the map uses them: on a
// room and a titles.txt made up here, and with HLDS_VALVE_DIR set, the first
// map's own: the title, the names of the introduction as the tram goes by them.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "TestGame.hpp"
#include "hlds/TitlesFile.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    void testParse()
    {
        const TitlesBook book = parseTitles("// a comment\r\n"
                                            "$position -1 0.65\r\n"
                                            "$effect 2\r\n"
                                            "$color 100 100 100\r\n"
                                            "$color2 240 110 0\r\n"
                                            "$fadein 0.01\r\n"
                                            "$fxtime 0.25\r\n"
                                            "$holdtime 3.5\r\n"
                                            "$fadeout 1.5\r\n"
                                            "\r\n"
                                            "First\r\n"
                                            "{\r\n"
                                            "BLACK MESA INBOUND\r\n"
                                            "}\r\n"
                                            "$position 0.1 0.8\n"
                                            "$effect 1\n"
                                            "$holdtime 1.2\n"
                                            "Second\n"
                                            "{\n"
                                            "One line\n"
                                            "and another\n"
                                            "}\n"
                                            "// $effect 0\n"
                                            "Third\n{\n\n}\n");
        const Title *first = book.find("FIRST"), *second = book.find("second"), *third = book.find("Third");
        check(book.titles.size() == 3 && first != nullptr && second != nullptr && third != nullptr,
              "every message of the file is found, by its name in any case");
        if (first == nullptr || second == nullptr || third == nullptr)
            return;
        check(first->text == "BLACK MESA INBOUND" && first->x == -1 && first->y == 0.65 && first->effect == 2 &&
                  first->colour2[0] == 240 && first->colour2[1] == 110 && first->fadeIn == 0.01 &&
                  first->fxTime == 0.25 && first->hold == 3.5 && first->fadeOut == 1.5,
              "a message is shown the way the settings stood where it is");
        check(second->text == "One line\nand another" && second->x == 0.1 && second->y == 0.8 &&
                  second->effect == 1 && second->hold == 1.2 && second->fadeOut == 1.5 && second->colour[0] == 100,
              "a setting stands until it is set again; a message's lines are kept apart");
        check(third->text.empty() && third->effect == 1, "an empty message is one; a comment sets nothing");
    }

    std::unique_ptr<Game> load(const char *dir, const char *map)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
        {
            check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
            return nullptr;
        }
        finish(*game, nullptr, dir);
        return game->ok ? std::move(game) : nullptr;
    }

    /// (The lamp's line: what it falls on, and which way that faces.
    /// Straight down from the eyes it is the floor, facing up; straight up,
    /// something facing down.)
    void lampLine(Game &g)
    {
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + fxFromInt(28)};
        const fx down[3] = {eye[0], eye[1], eye[2] - fxFromInt(600)}, up[3] = {eye[0], eye[1], eye[2] + fxFromInt(600)};
        fx at[3], normal[3];
        const bool floor = traceShotWall(g.move, eye, down, at, normal);
        check(floor && normal[2] > 3900 && at[2] < eye[2] && at[2] > eye[2] - fxFromInt(120),
              "a line down from the eyes finds the floor under them, facing up");
        const bool roof = traceShotWall(g.move, eye, up, at, normal);
        check(roof && normal[2] < -3000 && at[2] > eye[2], "... and up, what is overhead, facing down");
        const fx nowhere[3] = {eye[0], eye[1], eye[2] - fxFromInt(4)};
        check(!traceShotWall(g.move, eye, nowhere, at, normal), "a line that reaches nothing finds nothing");
    }

    /// Stuck fast in the floor, deeper than a step gets anyone out of: after
    /// half a second somewhere clear is found, farther off.
    void stuckFast(Game &g)
    {
        for (int n = 0; n < 30; ++n)
            g.frame();
        const fx stood = g.player.origin[2];
        g.player.origin[2] -= fxFromInt(60);
        g.frame();
        const bool held = g.player.stuck;
        for (int n = 0; n < 40; ++n)
            g.frame();
        check(held && !g.player.stuck && g.player.origin[2] > stood - fxFromInt(40),
              "a player stuck fast in the floor is put somewhere clear after half a second");
    }

    void testFirstMap(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c0a0");
        check(game != nullptr, "the first map cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        const LevelView &v = g.view;
        int logo = -1, ted = -1, records = 0;
        for (std::uint32_t i = 0; i < v.messageCount; ++i)
        {
            const MessageRecord &m = v.messages[i];
            if ((m.flags & kMessageLogo) && (m.flags & kMessageAtStart))
                logo = static_cast<int>(i);
            if (std::string(v.strings + m.text) == "Ted Backman")
                ted = static_cast<int>(i);
        }
        for (std::uint32_t i = 0; i < v.logicCount; ++i)
            records += v.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Message);
        std::printf("test_titles: c0a0 has %u messages, %d entities that show one\n", v.messageCount, records);
        check(logo >= 0, "the first map shows the game's title as it is come into");
        check(ted >= 0 && records >= 4, "... and has the names of the introduction, each an entity that shows it");
        if (ted >= 0)
        {
            const MessageRecord &m = v.messages[ted];
            // (titles.txt: "$position 0.1 0.8", "$effect 1", fading in over
            // a second and a half.)
            check(m.x == 26 && m.y == 205 && m.effect == kMessageFlicker && m.fadeInMs == 1500 &&
                      m.fadeOutMs == 500 && m.holdMs == 1200 && !(m.flags & (kMessageLogo | kMessageAtStart)),
                  "a name is where the game's file puts it, shown as it says");
        }
        lampLine(g);
        // The ride: the names come as the tram goes by what shows them.
        std::vector<int> seen;
        int first = -1, last = -1;
        for (int t = 0; t < 120000; t += 33)
        {
            g.frame();
            for (int n = 0; n < g.logic.shownCount; ++n)
            {
                seen.push_back(g.logic.shown[n]);
                if (first < 0)
                    first = t;
                last = t;
            }
            g.logic.shownCount = 0;
        }
        std::printf("test_titles: in the ride %zu messages were shown, from %d s to %d s:", seen.size(), first / 1000,
                    last / 1000);
        for (std::size_t i = 0; i < seen.size() && i < 6; ++i)
            std::printf(" \"%s\"", v.strings + v.messages[seen[i]].text);
        std::printf("\n");
        bool tedShown = false, twice = false;
        for (std::size_t i = 0; i < seen.size(); ++i)
        {
            tedShown = tedShown || seen[i] == ted;
            for (std::size_t k = 0; k < i; ++k)
                twice = twice || seen[k] == seen[i];
        }
        check(seen.size() >= 3 && tedShown, "the names of the introduction are shown along the ride");
        check(!twice, "each once");
    }

    void testChapters(const char *dir)
    {
        // A chapter's name is the world's own key, shown as the map is come into.
        int named = 0;
        for (const char *map : {"c1a0", "c1a1", "c1a2", "c1a3", "c2a1"})
        {
            const std::unique_ptr<Game> game = load(dir, map);
            if (game == nullptr)
                continue;
            for (std::uint32_t i = 0; i < game->view.messageCount; ++i)
            {
                const MessageRecord &m = game->view.messages[i];
                if ((m.flags & kMessageAtStart) && !(m.flags & kMessageLogo))
                {
                    std::printf("test_titles: %s begins \"%s\" (effect %d)\n", map, game->view.strings + m.text, m.effect);
                    ++named;
                }
            }
        }
        check(named >= 3, "the maps that begin a chapter say its name");
        if (const std::unique_ptr<Game> game = load(dir, "c1a0"))
            stuckFast(*game);
    }

    /// The same mechanics on a room and a titles.txt made up here. (A real
    /// ride cannot carry over: here it is a trigger walked into.)
    void testRoom()
    {
        FakeGame fake("titles", "",
                      "$position -1 0.3\n"
                      "$effect 0\n"
                      "$color 100 100 100\n"
                      "$color2 240 110 0\n"
                      "GAMETITLE\n{\nThe Logo\n}\n"
                      "OPENING\n{\nA Chapter\n}\n"
                      "$position 0.1 0.8\n"
                      "$effect 1\n"
                      "$fadein 1.5\n"
                      "$fadeout 0.5\n"
                      "$holdtime 1.2\n"
                      "TED\n{\nA Name\n}\n");
        BoxRoom room;
        room.worldKeys = "\"gametitle\" \"1\"\n\"chaptertitle\" \"OPENING\"\n";
        BrushBox walked;
        walked.className = "trigger_multiple";
        walked.mins[0] = -120, walked.mins[1] = -120, walked.mins[2] = 0;
        walked.maxs[0] = -60, walked.maxs[1] = 120, walked.maxs[2] = 96;
        walked.keys = "\"target\" \"ted\"\n\"wait\" \"0.1\"\n";
        room.brushes.push_back(walked);
        room.extraEntities =
            "{\n\"classname\" \"env_message\"\n\"targetname\" \"ted\"\n\"message\" \"TED\"\n\"spawnflags\" \"1\"\n}\n"
            "{\n\"classname\" \"env_message\"\n\"targetname\" \"unknown\"\n\"message\" \"NOT IN THE FILE\"\n}\n";
        auto game = std::make_unique<Game>();
        std::string error;
        if (game->bsp.parse(buildBoxRoom(room), error))
            finish(*game, nullptr, fake.dir.c_str());
        check(game->ok, "the room of messages cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const LevelView &v = g.view;
        int logo = -1, chapter = -1, ted = -1, unknown = -1, records = 0;
        for (std::uint32_t i = 0; i < v.messageCount; ++i)
        {
            const MessageRecord &m = v.messages[i];
            const std::string text(v.strings + m.text);
            if ((m.flags & kMessageLogo) && (m.flags & kMessageAtStart) && text == "The Logo")
                logo = static_cast<int>(i);
            if ((m.flags & kMessageAtStart) && !(m.flags & kMessageLogo) && text == "A Chapter")
                chapter = static_cast<int>(i);
            if (text == "A Name")
                ted = static_cast<int>(i);
            if (text == "NOT IN THE FILE")
                unknown = static_cast<int>(i);
        }
        for (std::uint32_t i = 0; i < v.logicCount; ++i)
            records += v.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Message);
        check(logo >= 0, "the map says it shows the game's title as it is come into");
        check(chapter >= 0, "... and its chapter's name");
        check(ted >= 0 && records == 2, "... and has its names, each an entity that shows it");
        check(unknown >= 0, "a name the file has not got is shown as it is");
        if (ted < 0)
            return;
        const MessageRecord &m = v.messages[ted];
        check(m.x == 26 && m.y == 205 && m.effect == kMessageFlicker && m.fadeInMs == 1500 && m.fadeOutMs == 500 &&
                  m.holdMs == 1200 && !(m.flags & (kMessageLogo | kMessageAtStart)),
              "a name is where the file puts it, shown as it says");
        check(logo >= 0 && v.messages[logo].y == 77, "... and the title where it says for that");

        lampLine(g);

        // Walked into, shown; a second time, not: it shows once. A place away
        // from it, nothing.
        for (int t = 0; t < 10; ++t)
            g.frame();
        check(g.logic.shownCount == 0, "nothing is shown of itself");
        std::vector<int> seen;
        g.player.origin[0] = fxFromInt(-90);
        for (int t = 0; t < 60; ++t)
        {
            g.frame();
            for (int n = 0; n < g.logic.shownCount; ++n)
                seen.push_back(g.logic.shown[n]);
            g.logic.shownCount = 0;
        }
        check(seen.size() == 1 && seen[0] == ted, "the name is shown as what shows it is used, and once");

        g.player.origin[0] = 0;
        for (int n = 0; n < 5; ++n)
            g.frame();
        stuckFast(g);
    }
} // namespace

int main()
{
    testParse();
    testRoom();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
    {
        testFirstMap(dir);
        testChapters(dir);
    }
    else
    {
        std::printf("test_titles: HLDS_VALVE_DIR not set, the game's own were not read\n");
    }
    if (g_failures == 0)
        std::printf("test_titles: ok\n");
    return g_failures == 0 ? 0 : 1;
}
