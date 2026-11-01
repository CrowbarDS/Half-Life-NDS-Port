// Textures that are more than one picture (hlds/TextureFamily.hpp, the
// level's TextureRef, HlLogic's brush faces and conveyors), on rooms made here
// and on the game's own maps (HLDS_VALVE_DIR): a cycle's pictures in the level one after another and all one size,
// a switched texture's other face, '-' textures varied face by face, which
// face a brush shows as it is used, and a conveyor carrying and sliding.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "TestGame.hpp"
#include "hlds/TextureFamily.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
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

    int textureNamed(const LevelView &v, const char *name)
    {
        for (std::uint32_t i = 0; i < v.textureCount; ++i)
        {
            if (std::strcmp(v.textures[i].name, name) == 0)
                return static_cast<int>(i);
        }
        return -1;
    }

    /// `screen` is the stem of a cycle of four, `switched` of a texture with
    /// another face.
    void cycles(Game &game, const char *screenStem, const char *switched)
    {
        const LevelView &v = game.view;
        // A screen of four pictures.
        const int screen = textureNamed(v, (std::string("+0") + screenStem).c_str());
        check(screen >= 0, "a cycling texture the map uses is in the level by its first picture's name");
        if (screen >= 0)
        {
            const TextureRef &first = v.textures[screen];
            bool inOrder = first.frames == 4, oneSize = true;
            for (int n = 0; inOrder && n < first.frames; ++n)
            {
                const TextureRef &t = v.textures[screen + n];
                char name[16];
                std::snprintf(name, sizeof(name), "+%d%s", n, screenStem);
                inOrder = std::strcmp(t.name, name) == 0 && (n == 0 || t.frames == 1);
                oneSize = oneSize && t.width == first.width && t.height == first.height;
            }
            check(inOrder && oneSize && first.other == kNoTexture,
                  "its four pictures are one after another in the level, all one size; it has no other face");
        }
        // Something with another face.
        const int kit = textureNamed(v, (std::string("+0") + switched).c_str());
        check(kit >= 0, "a switched texture is there too");
        if (kit >= 0)
        {
            const TextureRef &t = v.textures[kit];
            check(t.frames == 1 && t.other != kNoTexture && t.otherFrames == 1 &&
                      std::string(v.textures[t.other].name) == std::string("+a") + switched &&
                      v.textures[t.other].width == t.width && v.textures[t.other].height == t.height,
                  "... with its other face, of the same size");
        }
        // No surface names anything but a first picture.
        bool firsts = true;
        std::set<int> later;
        for (std::uint32_t i = 0; i < v.textureCount; ++i)
        {
            for (int n = 1; n < v.textures[i].frames; ++n)
                later.insert(static_cast<int>(i) + n);
            if (v.textures[i].other != kNoTexture)
            {
                for (int n = 0; n < v.textures[i].otherFrames; ++n)
                    later.insert(v.textures[i].other + n);
            }
        }
        for (std::uint32_t s = 0; s < v.surfaceCount; ++s)
            firsts = firsts && (v.surfaces[s].texture == kNoTexture || later.count(v.surfaces[s].texture) == 0);
        check(firsts && !later.empty(), "surfaces name first pictures only: which is shown is the console's to say");

        // The reader refuses a cycle that runs off the table.
        std::vector<std::uint8_t> bad = game.bytes;
        LevelView broken;
        auto *refs = const_cast<TextureRef *>(v.textures);
        const std::size_t at = reinterpret_cast<const std::uint8_t *>(&refs[v.textureCount - 1]) - game.bytes.data();
        TextureRef last;
        std::memcpy(&last, bad.data() + at, sizeof(last));
        last.frames = 2;
        std::memcpy(bad.data() + at, &last, sizeof(last));
        check(broken.open(bad.data(), static_cast<std::uint32_t>(bad.size())) != LevelError::Ok,
              "a level whose cycle runs off the end of its textures is refused");
    }

    void testCycles(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a0");
        check(game != nullptr, "the map cooks");
        if (game == nullptr)
            return;
        cycles(*game, "~lab_crt2", "medkit");
    }

    /// `label` for what it prints; at least `leastFamilies` of them and
    /// `leastVaried` faces changed for it to be called varied.
    void variety(BspFile &bsp, const char *label, int leastFamilies, int leastVaried)
    {
        // Per family: which variants its faces wear.
        std::map<std::string, std::set<int>> worn;
        int varied = 0;
        for (const BspFace &face : bsp.faces)
        {
            const int named = bsp.texInfos[face.texInfo].miptex;
            const std::string &name = bsp.textures[named].name;
            if (name.size() < 3 || name[0] != '-')
                continue;
            const int wears = wornTexture(bsp, face);
            check(bsp.textures[wears].name.substr(2) == name.substr(2) && bsp.textures[wears].name[0] == '-',
                  "a face wears a variant of its own texture");
            worn[name.substr(2)].insert(wears);
            varied += wears != named;
            if (wears != wornTexture(bsp, face))
                check(false, "the same face is given the same variant every time");
        }
        int families = 0, spread = 0;
        for (const auto &family : worn)
        {
            ++families;
            spread += family.second.size() >= 2;
        }
        std::printf("test_families: %s has %d '-' families on its faces, %d of them worn in more than one variant; "
                    "%d faces changed\n",
                    label, families, spread, varied);
        check(families >= leastFamilies && spread >= families / 2 && varied > leastVaried,
              "the '-' textures are varied from face to face");
        setRandomTiling(false);
        bool plain = true;
        for (const BspFace &face : bsp.faces)
            plain = plain && wornTexture(bsp, face) == bsp.texInfos[face.texInfo].miptex;
        setRandomTiling(true);
        check(plain, "... unless that is switched off");
    }

    void testRandom(const char *dir)
    {
        BspFile bsp;
        std::string error;
        if (!bsp.load(std::string(dir) + "/maps/c2a4f.bsp", error))
        {
            check(false, (std::string(dir) + "/maps/c2a4f.bsp loads").c_str());
            return;
        }
        variety(bsp, "c2a4f", 5, 20);
    }

    void faces(Game &g)
    {
        const LevelView &v = g.view;
        for (int n = 0; n < 10; ++n)
            g.frame();
        // A charger: its own face while it has something, the other when it
        // has nothing.
        const int charger = g.recordOfKind(LogicKind::HealthCharger);
        check(charger >= 0 && v.logic[charger].brush != kNoBrush, "the map has a health charger");
        if (charger >= 0 && v.logic[charger].brush != kNoBrush)
        {
            const BrushState &brush = g.brushStates[v.logic[charger].brush];
            const bool full = brush.frame == 0;
            g.states[charger].bits = v.logic[charger].extra[0];
            g.frame();
            check(full && brush.frame == 1, "a charger shows its other face once it is empty");
        }
        // A named wall (a light fixture): the other face each time it is used.
        int wall = -1;
        for (std::uint32_t i = 0; i < v.logicCount && wall < 0; ++i)
        {
            if (v.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Static) && v.logic[i].brush != kNoBrush)
                wall = static_cast<int>(i);
        }
        if (wall >= 0)
        {
            // (The map may have switched it already as it started.)
            const BrushState &brush = g.brushStates[v.logic[wall].brush];
            const int was = brush.frame;
            logicUse(g.logic, g.player, wall, -1, kUseToggle);
            g.frame();
            const int then = brush.frame;
            logicUse(g.logic, g.player, wall, -1, kUseToggle);
            g.frame();
            check(was <= 1 && then == 1 - was && brush.frame == was,
                  "a wall that is switched shows its other face, and the first again when switched back");
        }
        // A button: its other face while it is in.
        int button = -1;
        for (std::uint32_t i = 0; i < v.logicCount && button < 0; ++i)
        {
            if (v.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Button) && v.logic[i].brush != kNoBrush &&
                v.logic[i].master == 0)
                button = static_cast<int>(i);
        }
        if (button >= 0)
        {
            const BrushState &brush = g.brushStates[v.logic[button].brush];
            const bool out = brush.frame == 0;
            logicUse(g.logic, g.player, button, -1, kUseSet);
            g.frame();
            check(out && brush.frame == 1, "a button shows its other face once it is pressed");
        }
    }

    void testFaces(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a0");
        if (game == nullptr)
            return;
        faces(*game);
    }

    void conveyor(Game &g, const char *label, int leastBelts)
    {
        const LevelView &v = g.view;
        int belts = 0, sliding = 0;
        for (std::uint32_t i = 0; i < v.logicCount; ++i)
            belts += v.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Conveyor);
        for (std::uint32_t i = 0; i < v.textureCount; ++i)
            sliding += (v.textures[i].flags & kTextureScroll) != 0;
        std::printf("test_families: %s has %d conveyors, %d sliding textures\n", label, belts, sliding);
        check(belts >= leastBelts && sliding >= 1, "its conveyors are in the level, and their belts' textures slide");
        const int belt = g.recordOfKind(LogicKind::Conveyor);
        if (belt < 0)
            return;
        const LogicRecord &record = v.logic[belt];
        BrushState &brush = g.brushStates[record.brush];
        // The belt slides whether anyone is on it or not.
        const fx before = brush.scroll;
        for (int n = 0; n < 30; ++n)
            logicTick(g.logic, g.player, 33);
        const fx slid = (brush.scroll - before) & (fxFromInt(1024) - 1);
        const fx expected = static_cast<fx>((static_cast<std::int64_t>(record.speed) * 990) / 1000);
        check(slid > expected - fxFromInt(2) && slid < expected + fxFromInt(2),
              "a belt's texture slides its speed in texels a second");
        // Someone on it goes with it.
        const MoverHull &mover = g.movers[record.brush];
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = (mover.mins[a] + mover.maxs[a]) / 2;
        g.player.origin[2] = mover.maxs[2] + fxFromInt(37);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        g.player.onGround = true;
        g.player.groundMover = static_cast<std::int16_t>(record.brush);
        const fx from[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        for (int n = 0; n < 10; ++n)
        {
            g.player.onGround = true;
            g.player.groundMover = static_cast<std::int16_t>(record.brush);
            logicTick(g.logic, g.player, 33);
        }
        std::int64_t along = 0, speed2 = 0;
        for (int a = 0; a < 3; ++a)
        {
            along += static_cast<std::int64_t>(g.player.origin[a] - from[a]) * record.move[a];
            speed2 += static_cast<std::int64_t>(record.move[a]) * record.move[a];
        }
        // (A third of a second at its speed, along its way.)
        const double went = speed2 > 0 ? double(along) / std::sqrt(double(speed2)) / 4096.0 : 0;
        const double speed = std::sqrt(double(speed2)) / 4096.0;
        std::printf("test_families: a conveyor of %.0f units a second carried the player %.1f units in a third of a second\n",
                    speed, went);
        check(speed > 10 && went > speed * 0.25 && went < speed * 0.4, "whoever stands on a conveyor is carried along it");
        // Used, it runs the other way.
        logicUse(g.logic, g.player, belt, -1, kUseToggle);
        const fx there[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        for (int n = 0; n < 5; ++n)
        {
            g.player.onGround = true;
            g.player.groundMover = static_cast<std::int16_t>(record.brush);
            logicTick(g.logic, g.player, 33);
        }
        along = 0;
        for (int a = 0; a < 3; ++a)
            along += static_cast<std::int64_t>(g.player.origin[a] - there[a]) * record.move[a];
        check(along < 0, "used, it runs the other way");
    }

    void testConveyor(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c2a4c");
        check(game != nullptr, "the map with the conveyors cooks");
        if (game == nullptr)
            return;
        conveyor(*game, "c2a4c", 20);
    }

    BrushBox box(const char *cls, float x, float y, float size, const char *texture, std::string keys = "")
    {
        BrushBox made;
        made.className = cls;
        made.mins[0] = x, made.mins[1] = y, made.mins[2] = 0;
        made.maxs[0] = x + size, made.maxs[1] = y + size, made.maxs[2] = size;
        made.textureName = texture;
        made.keys = std::move(keys);
        return made;
    }

    std::unique_ptr<Game> cook(const BoxRoom &room)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.parse(buildBoxRoom(room), error))
            return nullptr;
        finish(*game, nullptr);
        return game->ok ? std::move(game) : nullptr;
    }

    /// The same on rooms made here. (Their textures are all of one size:
    /// that a cycle's pictures are is carried over, but cannot fail here.)
    void testRooms()
    {
        // A screen of four whose faces wear its third picture; a switched
        // kit; one cycle that does not start at its first picture (no +0),
        // which is only itself; a texture nothing wears.
        BoxRoom room;
        room.mins[0] = room.mins[1] = -512;
        room.maxs[0] = room.maxs[1] = 512;
        room.extraTextures = {"+0screen", "+1screen", "+3screen", "+amedkit", "+1broken", "unworn"};
        room.brushes.push_back(box("func_wall", -300, 0, 32, "+2screen"));
        room.brushes.push_back(box("func_wall", -200, 0, 32, "+0medkit"));
        room.brushes.push_back(box("func_wall", -100, 0, 32, "+1broken"));
        std::unique_ptr<Game> game = cook(room);
        check(game != nullptr, "the room of cycles cooks");
        if (game != nullptr)
        {
            cycles(*game, "screen", "medkit");
            const LevelView &v = game->view;
            const int broken = textureNamed(v, "+1broken");
            check(broken >= 0 && v.textures[broken].frames == 1 && v.textures[broken].other == kNoTexture,
                  "a cycle without its first picture is the texture as it is");
            check(textureNamed(v, "unworn") < 0, "a texture no face wears is not in the level");
        }

        // Variants: two families, on many faces.
        BoxRoom tiled;
        tiled.mins[0] = tiled.mins[1] = -512;
        tiled.maxs[0] = tiled.maxs[1] = 512;
        tiled.extraTextures = {"-1tile", "-2tile", "-1slab"};
        for (int n = 0; n < 12; ++n)
        {
            tiled.brushes.push_back(box("func_wall", -480.0f + n * 72, -400, 40, "-0tile"));
            tiled.brushes.push_back(box("func_wall", -480.0f + n * 72, 300, 40, "-0slab"));
        }
        BspFile bsp;
        std::string error;
        check(bsp.parse(buildBoxRoom(tiled), error), "the room of variants parses");
        variety(bsp, "a room", 2, 20);

        // A charger, a named wall and a button, each wearing what has
        // another face.
        BoxRoom used;
        used.mins[0] = used.mins[1] = -512;
        used.maxs[0] = used.maxs[1] = 512;
        used.extraTextures = {"+akit", "+alamp", "+apush"};
        used.brushes.push_back(box("func_healthcharger", -300, 200, 32, "+0kit"));
        used.brushes.push_back(box("func_wall", -200, 200, 32, "+0lamp", "\"targetname\" \"lamp\"\n"));
        used.brushes.push_back(box("func_button", -100, 200, 32, "+0push", "\"wait\" \"-1\"\n\"angles\" \"0 90 0\"\n"));
        game = cook(used);
        check(game != nullptr, "the room of switched faces cooks");
        if (game != nullptr)
        {
            check(game->recordOfKind(LogicKind::Static) >= 0 && game->recordOfKind(LogicKind::Button) >= 0,
                  "it has its wall and its button");
            faces(*game);
        }

        // A belt to stand on, that goes along x; one that says it pushes
        // nobody; a scroll texture on something else, which stays still.
        BoxRoom belts;
        belts.mins[0] = belts.mins[1] = -1024;
        belts.maxs[0] = belts.maxs[1] = 1024;
        belts.maxs[2] = 512;
        belts.brushes.push_back(box("func_conveyor", -256, -256, 256, "scrollbelt", "\"speed\" \"120\"\n\"angles\" \"0 0 0\"\n"));
        belts.brushes.push_back(box("func_conveyor", 300, 300, 256, "scrollbelt", "\"speed\" \"120\"\n\"angles\" \"0 0 0\"\n\"spawnflags\" \"1\"\n"));
        belts.brushes.push_back(box("func_wall", 600, -300, 64, "scrollstill"));
        game = cook(belts);
        check(game != nullptr, "the room of belts cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        conveyor(g, "a room", 2);
        const int still = textureNamed(g.view, "scrollstill");
        check(still >= 0 && !(g.view.textures[still].flags & kTextureScroll),
              "a scroll texture on anything but a belt stays still");
        const int idle = g.recordOfKind(LogicKind::Conveyor, 1);
        if (idle >= 0)
        {
            const LogicRecord &record = g.view.logic[idle];
            const MoverHull &mover = g.movers[record.brush];
            const fx before = g.brushStates[record.brush].scroll;
            g.player.origin[0] = (mover.mins[0] + mover.maxs[0]) / 2;
            g.player.origin[1] = (mover.mins[1] + mover.maxs[1]) / 2;
            g.player.origin[2] = mover.maxs[2] + fxFromInt(37);
            const fx from = g.player.origin[0];
            for (int n = 0; n < 10; ++n)
            {
                g.player.onGround = true;
                g.player.groundMover = static_cast<std::int16_t>(record.brush);
                logicTick(g.logic, g.player, 33);
            }
            check(g.player.origin[0] == from && g.brushStates[record.brush].scroll != before,
                  "one that pushes nobody carries nobody, and still slides");
        }
    }
} // namespace

int main()
{
    testRooms();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
    {
        testCycles(dir);
        testRandom(dir);
        testFaces(dir);
        testConveyor(dir);
    }
    else
    {
        std::printf("test_families: HLDS_VALVE_DIR not set, the game's maps were not read\n");
    }
    if (g_failures == 0)
        std::printf("test_families: ok\n");
    return g_failures == 0 ? 0 : 1;
}
