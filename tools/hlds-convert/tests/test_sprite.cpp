// The game's sprites, read and cooked (core/src/SpriteCook.cpp) for the DS
// (shared/HlSprite.hpp).
//
// A sprite made here by hand is read back as it was written, and cooked: its
// frames in an atlas where the level's record says, light turned into
// see-through. How a glow fades with distance (glowFalloff). Then, with
// HLDS_VALVE_DIR, the game's own -- and which of the maps' glows fade.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <memory>

#include "HlSprite.hpp"
#include "TestGame.hpp"
#include "hlds/SpriteCook.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    void put32(std::vector<std::uint8_t> &out, std::int32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> (i * 8)));
    }

    /// A sprite of `frames` frames, `width` by `height`: frame f is all of
    /// palette index 10 + f, but for its top-left four pixels, which are 255
    /// (white: so that there is something at full strength in it, and the
    /// cooker has no call to bring it up).
    std::vector<std::uint8_t> makeSprite(int format, int width, int height, int frames)
    {
        std::vector<std::uint8_t> out = {'I', 'D', 'S', 'P'};
        put32(out, 2);
        put32(out, 2); // turned to the eye
        put32(out, format);
        put32(out, 0); // radius (a float; nothing reads it)
        put32(out, width);
        put32(out, height);
        put32(out, frames);
        put32(out, 0);
        put32(out, 0);
        out.push_back(0);
        out.push_back(1); // 256 colours
        for (int c = 0; c < 256; ++c)
        {
            // Index 10 + f: a red that gets brighter; 255: white.
            out.push_back(static_cast<std::uint8_t>(c == 255 ? 255 : (c >= 10 && c < 40 ? 40 + (c - 10) * 10 : 0)));
            out.push_back(static_cast<std::uint8_t>(c == 255 ? 255 : 0));
            out.push_back(static_cast<std::uint8_t>(c == 255 ? 255 : 0));
        }
        for (int f = 0; f < frames; ++f)
        {
            put32(out, 0);
            put32(out, -width / 2);
            put32(out, height / 2);
            put32(out, width);
            put32(out, height);
            for (int i = 0; i < width * height; ++i)
                out.push_back(static_cast<std::uint8_t>(i % width < 2 && i / width < 2 ? 255 : 10 + f));
        }
        return out;
    }

    void testMade()
    {
        SprFile spr;
        std::string error;
        check(spr.parse(makeSprite(kSprAdditive, 64, 64, 4), error) && spr.frames.size() == 4 &&
                  spr.width == 64 && spr.format == kSprAdditive && spr.frames[2].pixels[5] == 12,
              "a sprite is read as it was written");
        std::vector<std::uint8_t> cut = makeSprite(kSprAdditive, 64, 64, 4);
        cut.resize(cut.size() - 100);
        SprFile bad;
        check(!bad.parse(cut, error), "one cut short is refused");

        // As light: how bright becomes how solid, the colour made full.
        const RgbaImage frame = spriteFrameImage(spr, 1, false);
        const std::uint8_t *p = frame.at(5, 5);
        check(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 50, "light is cooked as colour, solid by its brightness");
        check(frame.at(0, 0)[3] == 255, "... white wholly so");

        CookedSprite cooked;
        check(cookSprite(spr, 32, 8, false, cooked, error), "it cooks");
        check(cooked.ref.cellWidth == 32 && cooked.ref.cellHeight == 32 && cooked.ref.columns == 4 &&
                  cooked.ref.frames == 4 && cooked.atlas.width == 128 && cooked.atlas.height == 32 &&
                  cooked.ref.worldWidth == 64 && cooked.ref.kind == kSpriteFacesEye,
              "four frames of 64 become four cells of 32 in a row");
        // Frame 3's cell holds frame 3: red at alpha 40 + 3 * 10.
        const std::uint8_t *cell = cooked.atlas.at(3 * 32 + 16, 16);
        check(cell[0] == 255 && cell[3] == 70, "each cell is its frame");

        // More frames than are wanted: taken evenly, the first and last kept.
        SprFile many;
        many.parse(makeSprite(kSprAdditive, 64, 64, 15), error);
        check(cookSprite(many, 32, 8, false, cooked, error) && cooked.ref.frames == 8 &&
                  cooked.ref.columns == 4 && cooked.atlas.width == 128 && cooked.atlas.height == 64,
              "fifteen frames are thinned to eight, in two rows");
        check(cooked.atlas.at(16, 16)[3] == 40 && cooked.atlas.at(3 * 32 + 16, 32 + 16)[3] == 180,
              "... the first and the last among them");

        // A hole, and a strip.
        SprFile holes;
        holes.parse(makeSprite(kSprAlphaTest, 16, 16, 1), error);
        const RgbaImage holed = spriteFrameImage(holes, 0, false);
        check(holed.at(0, 0)[3] == 0 && holed.at(3, 3)[3] == 255 && holed.at(3, 3)[0] == 40,
              "a sprite with holes has them, and is solid elsewhere");
        SprFile strip;
        strip.parse(makeSprite(kSprNormal, 32, 64, 1), error);
        check(cookSprite(strip, 32, 1, true, cooked, error) && cooked.ref.cellWidth == 16 &&
                  cooked.ref.cellHeight == 32 && cooked.atlas.width == 16 && cooked.atlas.height == 32 &&
                  cooked.atlas.at(8, 16)[3] == 40,
              "a beam's strip keeps its shape, and is cooked as light whatever its file says");
        check(spriteName("sprites/Flare1.spr") == "flare1" && spriteName("sprites/zerogxplode.spr") == "zerogxplode",
              "a sprite goes by its file's name");
    }

    void testGame(const char *dir)
    {
        int texels = 0;
        for (int c = 0; c < static_cast<int>(CommonSprite::Count); ++c)
        {
            const char *path = commonSpritePath(static_cast<CommonSprite>(c));
            SprFile spr;
            std::string error;
            CookedSprite cooked;
            const bool ok = spr.load(std::string(dir) + "/" + path, error) &&
                            cookSprite(spr, 32, 8, false, cooked, error);
            if (!ok)
                std::printf("test_sprite: %s: %s\n", path, error.c_str());
            check(ok, "each of the common sprites is in the game and cooks");
            if (!ok)
                continue;
            // Something to see in every frame.
            bool seen = true;
            for (int f = 0; f < cooked.ref.frames; ++f)
            {
                int solid = 0;
                for (int y = 0; y < cooked.ref.cellHeight; ++y)
                    for (int x = 0; x < cooked.ref.cellWidth; ++x)
                        solid += cooked.atlas.at((f % cooked.ref.columns) * cooked.ref.cellWidth + x,
                                                 (f / cooked.ref.columns) * cooked.ref.cellHeight + y)[3] > 32;
                seen = seen && solid > 0;
            }
            check(seen, "... with something to see in each of its frames");
            texels += cooked.atlas.width * cooked.atlas.height;
            std::printf("test_sprite: %s: %dx%d, %d frames (of %zu) of %dx%d in %dx%d\n", path, spr.width,
                        spr.height, cooked.ref.frames, spr.frames.size(), cooked.ref.cellWidth,
                        cooked.ref.cellHeight, cooked.atlas.width, cooked.atlas.height);
        }
        std::printf("test_sprite: the common sprites at their largest: %dK texels\n", texels / 1024);
    }
    /// The engine's glow rule: the size kept on the screen (dist / 200 of
    /// itself), the brightness 19000 / dist^2, between 0.05 and all.
    void testFalloff()
    {
        int scale = 0, bright = 0;
        glowFalloff(50, scale, bright);
        check(scale == 64 && bright == 256, "a glow 50 units off: a quarter of its size, all its brightness");
        glowFalloff(200, scale, bright);
        check(scale == 256 && bright == 121, "200 units off: its own size, 19000/40000 of its brightness");
        glowFalloff(300, scale, bright);
        check(scale == 384 && bright == 54, "300 units off: half as big again, a fifth as bright");
        glowFalloff(1000, scale, bright);
        check(scale == 1280 && bright == 13, "1000 units off: five times as big, never fainter than 0.05");
        glowFalloff(0, scale, bright);
        check(scale >= 1 && bright == 256, "at the eye: tiny, and no divide by nothing");
    }

    /// Which of a map's glows fade (record.extra[3] bit 3: render fx 14 does not).
    void testMapGlows(const char *dir)
    {
        auto glows = [&](const char *map, const char *name, int &fading, int &kept)
        {
            auto g = std::make_unique<Game>();
            std::string error;
            fading = kept = 0;
            if (!g->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
            {
                check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
                return false;
            }
            static Globals globals;
            finish(*g, &globals, dir);
            for (int i = 0; i < g->logic.recordCount; ++i)
            {
                const LogicRecord &r = g->logic.records[i];
                if (r.kind != static_cast<std::uint8_t>(LogicKind::Sprite) || !(r.extra[3] & 2))
                    continue;
                if (name != nullptr && (r.name == 0 || g->logic.nameHashes[r.name] != nameHash(name)))
                    continue;
                (r.extra[3] & 8 ? kept : fading) += 1;
            }
            return true;
        };
        int fading = 0, kept = 0;
        check(glows("c1a0e", "red", fading, kept) && fading == 4 && kept == 0,
              "c1a0e's red alarm lights (render fx 0) fade with distance");
        check(glows("c1a0b", nullptr, fading, kept) && kept >= 3,
              "c1a0b's lamps in the tubes (env_glow, render fx 14) keep their size and brightness");
    }
} // namespace

int main()
{
    testMade();
    testFalloff();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
    {
        testGame(dir);
        testMapGlows(dir);
    }
    else
        std::printf("test_sprite: HLDS_VALVE_DIR not set, the game's sprites were not read\n");
    if (g_failures == 0)
        std::printf("test_sprite: ok\n");
    return g_failures == 0 ? 0 : 1;
}
