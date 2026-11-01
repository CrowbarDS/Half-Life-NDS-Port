// Textures that are more than one picture (hlds/TextureFamily.hpp, the
// level's TextureRef, HlLogic's brush faces and conveyors), on the game's own
// maps: a cycle's pictures in the level one after another and all one size,
// a switched texture's other face, '-' textures varied face by face, which
// face a brush shows as it is used, and a conveyor carrying and sliding.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
            return nullptr;
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

    void testCycles(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a0");
        check(game != nullptr, "the map cooks");
        if (game == nullptr)
            return;
        const LevelView &v = game->view;
        // A screen of four pictures.
        const int screen = textureNamed(v, "+0~lab_crt2");
        check(screen >= 0, "a cycling texture the map uses is in the level by its first picture's name");
        if (screen >= 0)
        {
            const TextureRef &first = v.textures[screen];
            bool inOrder = first.frames == 4, oneSize = true;
            for (int n = 0; inOrder && n < first.frames; ++n)
            {
                const TextureRef &t = v.textures[screen + n];
                char name[16];
                std::snprintf(name, sizeof(name), "+%d~lab_crt2", n);
                inOrder = std::strcmp(t.name, name) == 0 && (n == 0 || t.frames == 1);
                oneSize = oneSize && t.width == first.width && t.height == first.height;
            }
            check(inOrder && oneSize && first.other == kNoTexture,
                  "its four pictures are one after another in the level, all one size; it has no other face");
        }
        // Something with another face.
        const int kit = textureNamed(v, "+0medkit");
        check(kit >= 0, "a switched texture is there too");
        if (kit >= 0)
        {
            const TextureRef &t = v.textures[kit];
            check(t.frames == 1 && t.other != kNoTexture && t.otherFrames == 1 &&
                      std::strcmp(v.textures[t.other].name, "+amedkit") == 0 &&
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
        std::vector<std::uint8_t> bad = game->bytes;
        LevelView broken;
        auto *refs = const_cast<TextureRef *>(v.textures);
        const std::size_t at = reinterpret_cast<const std::uint8_t *>(&refs[v.textureCount - 1]) - game->bytes.data();
        TextureRef last;
        std::memcpy(&last, bad.data() + at, sizeof(last));
        last.frames = 2;
        std::memcpy(bad.data() + at, &last, sizeof(last));
        check(broken.open(bad.data(), static_cast<std::uint32_t>(bad.size())) != LevelError::Ok,
              "a level whose cycle runs off the end of its textures is refused");
    }

    void testRandom(const char *dir)
    {
        BspFile bsp;
        std::string error;
        if (!bsp.load(std::string(dir) + "/maps/c2a4f.bsp", error))
            return;
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
        std::printf("test_families: c2a4f has %d '-' families on its faces, %d of them worn in more than one variant; "
                    "%d faces changed\n",
                    families, spread, varied);
        check(families >= 5 && spread >= families / 2 && varied > 20,
              "the '-' textures are varied from face to face");
        setRandomTiling(false);
        bool plain = true;
        for (const BspFace &face : bsp.faces)
            plain = plain && wornTexture(bsp, face) == bsp.texInfos[face.texInfo].miptex;
        setRandomTiling(true);
        check(plain, "... unless that is switched off");
    }

    void testFaces(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a0");
        if (game == nullptr)
            return;
        Game &g = *game;
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

    void testConveyor(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c2a4c");
        check(game != nullptr, "the map with the conveyors cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        const LevelView &v = g.view;
        int belts = 0, sliding = 0;
        for (std::uint32_t i = 0; i < v.logicCount; ++i)
            belts += v.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Conveyor);
        for (std::uint32_t i = 0; i < v.textureCount; ++i)
            sliding += (v.textures[i].flags & kTextureScroll) != 0;
        std::printf("test_families: c2a4c has %d conveyors, %d sliding textures\n", belts, sliding);
        check(belts >= 20 && sliding >= 1, "its conveyors are in the level, and their belts' textures slide");
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
} // namespace

int main()
{
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
