// Brush entities: what each one is, and what the cooker writes for it.
//
// The rules under test are the PC engine's, and they are the kind that fail
// silently: get "render amount 0" wrong and an invisible barrier becomes a
// visible wall in the middle of a corridor; get the '{' rule wrong and a grate
// is a solid sheet of blue. So each rule gets a brush of its own in one small
// room, and the cooked level is checked brush by brush.

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "HlLevelReader.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/BrushInfo.hpp"
#include "hlds/Cooker.hpp"
#include "hlds/GxStream.hpp"
#include "hlds/TextureCook.hpp"
#include "hlds/TextureImage.hpp"

namespace fs = std::filesystem;
using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    BrushBox box(const char *cls, const char *keys, const char *texture, float x)
    {
        BrushBox b;
        b.className = cls;
        b.keys = keys;
        b.textureName = texture;
        b.mins[0] = x;
        b.maxs[0] = x + 16;
        return b;
    }

    /// One brush per rule. Model numbers are 1.. in this order.
    BoxRoom rulesRoom()
    {
        BoxRoom room;
        room.brushes = {
            /* 1 */ box("func_wall", "", "", -112),
            /* 2 */ box("func_wall", "\"rendermode\" \"4\"\n\"renderamt\" \"255\"\n", "{grate", -88),
            /* 3 */ box("func_breakable", "\"rendermode\" \"2\"\n\"renderamt\" \"96\"\n", "glass", -64),
            /* 4 */ box("func_wall", "\"rendermode\" \"2\"\n\"renderamt\" \"0\"\n", "", -40),
            /* 5 */ box("trigger_once", "\"target\" \"door1\"\n", "", -16),
            /* 6 */ box("func_illusionary", "", "", 8),
            /* 7 */ box("func_wall",
                        "\"rendermode\" \"1\"\n\"renderamt\" \"128\"\n\"rendercolor\" \"255 0 0\"\n",
                        "", 32),
            /* 8 */ box("func_wall", "", "{grate", 56),
            /* 9 */ box("func_door_rotating", "\"origin\" \"90 20 8\"\n\"spawnflags\" \"8\"\n", "", -8),
        };
        // The rotating door is built round its pivot: a small box at the
        // model's own origin, placed by the entity's "origin".
        room.brushes[8].mins[0] = -8;
        room.brushes[8].maxs[0] = 8;
        room.brushes[8].mins[1] = -4;
        room.brushes[8].maxs[1] = 4;
        return room;
    }

    void testClassification()
    {
        BspFile bsp;
        std::string error;
        check(bsp.parse(buildBoxRoom(rulesRoom()), error), "the rules room parses");
        check(bsp.models.size() == 10, "the world and nine brush models");
        std::vector<Entity> ents;
        check(parseEntities(bsp.entities, ents, error), "entities parse");
        const std::vector<BrushInfo> b = analyseBrushes(bsp, ents);
        check(b.size() == 9, "nine brush entities");
        if (b.size() != 9)
            return;

        check(b[0].model == 1 && b[0].renderClass == RenderClass::Opaque && b[0].solid,
              "a plain func_wall: opaque and solid");
        check(b[1].renderClass == RenderClass::Cutout && b[1].cutsHoles() && !b[1].fullBright(),
              "render mode 4: cutout, lit");
        check(b[2].renderClass == RenderClass::Blend && b[2].alpha == 12 && b[2].fullBright(),
              "render mode 2 at amount 96: blended at 12 of 31, full bright");
        check(b[3].renderClass == RenderClass::Hidden && b[3].solid && !b[3].drawn(),
              "render mode 2 at amount 0: NOT drawn, still solid");
        check(b[4].renderClass == RenderClass::Hidden && !b[4].solid,
              "a trigger: neither drawn nor solid");
        check(b[5].renderClass == RenderClass::Opaque && !b[5].solid,
              "func_illusionary: drawn, walked through");
        check(b[6].renderClass == RenderClass::Colour && b[6].alpha == 16 &&
                  b[6].colour == rgb555(255, 0, 0),
              "render mode 1: a flat colour at its amount");
        check(b[7].renderClass == RenderClass::Opaque && !b[7].cutsHoles(),
              "a '{' texture in render mode 0 is drawn whole");
        check(b[8].hasPivot && b[8].origin[0] == 90 && b[8].origin[2] == 8 && !b[8].solid,
              "an origin key is a pivot; door flag 8 is passable");

        const auto owner = brushOfModel(bsp, b);
        check(owner[0] == nullptr && owner[3] == &b[2], "models map back to their brushes");

        // Rules not worth a brush each.
        auto classOf = [&](const char *text)
        {
            std::vector<Entity> e;
            std::string err;
            parseEntities(std::string("{\"classname\" \"func_wall\" \"model\" \"*1\" ") + text + "}",
                          e, err);
            return analyseBrushes(bsp, e)[0];
        };
        check(classOf("\"rendermode\" \"0\" \"renderamt\" \"0\"").renderClass == RenderClass::Opaque,
              "mode 0 ignores an amount of 0");
        check(classOf("\"rendermode\" \"4\" \"renderamt\" \"0\"").renderClass == RenderClass::Cutout,
              "mode 4 ignores the amount too");
        check(classOf("\"rendermode\" \"2\" \"renderamt\" \"255\"").renderClass == RenderClass::Cutout,
              "a blend at full strength has nothing left to blend");
        check(classOf("\"rendermode\" \"5\" \"renderamt\" \"180\"").renderClass == RenderClass::Blend,
              "additive is drawn as a blend");
        check(classOf("\"rendermode\" \"1\" \"renderamt\" \"0\"").renderClass == RenderClass::Hidden,
              "a colour at amount 0 is hidden");
        check(classOf("\"rendermode\" \"2\" \"renderamt\" \"3\"").renderClass == RenderClass::Hidden,
              "an amount too small to be one step of alpha is hidden");
        std::vector<Entity> bad;
        parseEntities("{\"classname\" \"func_wall\" \"model\" \"*99\"}"
                      "{\"classname\" \"light\" \"model\" \"sprites/x.spr\"}",
                      bad, error);
        check(analyseBrushes(bsp, bad).empty(), "a model that does not exist, or is not a brush");
    }

    struct Cooked
    {
        std::vector<std::uint8_t> bytes;
        LevelView view;
        CookReport report;
        bool ok = false;
    };

    Cooked cook(const BoxRoom &room)
    {
        Cooked c;
        BspFile bsp;
        std::string error;
        CookedLevel level;
        CookOptions options;
        options.lightScale = 1.0;
        options.lightPower = 1.0;
        c.ok = bsp.parse(buildBoxRoom(room), error) &&
               cookLevel(bsp, options, TexturePlan(), level, c.report, error) &&
               writeLevel(level, c.bytes, error) &&
               c.view.open(c.bytes.data(), static_cast<std::uint32_t>(c.bytes.size())) ==
                   LevelError::Ok;
        if (!c.ok)
            std::fprintf(stderr, "cook failed: %s\n", error.c_str());
        return c;
    }

    const Brush *brushOf(const LevelView &v, int model)
    {
        for (std::uint32_t i = 0; i < v.brushCount; ++i)
            if (v.brushes[i].model == model)
                return &v.brushes[i];
        return nullptr;
    }

    /// The first vertex colour and the texture of a model's first surface.
    bool firstSurface(const LevelView &v, int model, std::uint16_t &texture, std::uint16_t &colour)
    {
        const Model &m = v.models[model];
        if (m.surfaceCount == 0)
            return false;
        const Surface &s = v.surfaces[m.firstSurface];
        std::vector<GxRun> runs;
        std::string error;
        if (!decodeGxStream(v.gxStream + s.gxOfs, s.gxWords, runs, error) || runs.empty())
            return false;
        texture = s.texture;
        colour = runs[0].vertices[0].colour;
        return true;
    }

    void testCookedBrushes()
    {
        const Cooked c = cook(rulesRoom());
        check(c.ok, "the rules room cooks and opens");
        if (!c.ok)
            return;
        const LevelView &v = c.view;

        // Eight records: everything but the trigger, which is neither seen nor
        // in the way.
        check(v.brushCount == 8 && c.report.brushes == 8, "eight brush records");
        check(brushOf(v, 5) == nullptr, "the trigger has none");
        check(c.report.hiddenBrushes == 1, "one of them is solid but never drawn");

        const Brush *wall = brushOf(v, 1);
        check(wall && wall->renderClass == static_cast<std::uint8_t>(RenderClass::Opaque) &&
                  (wall->flags & kBrushSolid) && wall->alpha == 31 && wall->logic == kNoLogic,
              "the plain wall's record");
        check(wall && wall->mins[0] == -112 && wall->maxs[0] == -96 && wall->mins[2] == 0 &&
                  wall->maxs[2] == 64,
              "its bounds in the world");
        check(wall && wall->leafCount == 1 && v.brushLeaves[wall->firstLeaf] == 1,
              "it touches the room's one leaf");

        const Brush *hidden = brushOf(v, 4);
        check(hidden && hidden->renderClass == static_cast<std::uint8_t>(RenderClass::Hidden) &&
                  (hidden->flags & kBrushSolid),
              "the invisible wall is recorded, solid");
        check(v.models[4].surfaceCount == 0 && v.models[5].surfaceCount == 0,
              "nothing is cooked to draw for a hidden brush or a trigger");
        check(v.models[4].clipStanding >= 0, "but the invisible wall keeps its clip hull");

        const Brush *ghost = brushOf(v, 6);
        check(ghost && !(ghost->flags & kBrushSolid), "func_illusionary is not solid");

        const Brush *door = brushOf(v, 9);
        check(door && (door->flags & kBrushHasPivot) && door->origin[0] == fxFromInt(90) &&
                  door->origin[1] == fxFromInt(20) && door->origin[2] == fxFromInt(8),
              "the pivoted door carries its origin");
        check(door && door->mins[0] == 82 && door->maxs[0] == 98 && door->mins[1] == 16 &&
                  door->maxs[1] == 24,
              "and its bounds are the model's moved to it");
        check(v.models[9].mins[0] == -8, "while the model itself stays in its own space");

        // Texture forms and light.
        std::uint16_t tex = 0, colour = 0;
        check(firstSurface(v, 2, tex, colour) && tex != kNoTexture &&
                  (v.textures[tex].flags & kTextureA3I5) &&
                  (v.textures[tex].flags & kTextureColourKey) &&
                  std::strcmp(v.textures[tex].name, "{grate") == 0,
              "the cutout brush wears the A3I5 form of its '{' texture");
        const std::uint16_t alphaForm = tex;
        check(colour == rgb555(120, 120, 120), "and is lit by its lightmap");

        check(firstSurface(v, 8, tex, colour) && tex != alphaForm &&
                  !(v.textures[tex].flags & kTextureA3I5) &&
                  std::strcmp(v.textures[tex].name, "{grate") == 0,
              "the same texture on a render-mode-0 brush is the opaque form: two entries");

        const Brush *glass = brushOf(v, 3);
        check(glass && glass->renderClass == static_cast<std::uint8_t>(RenderClass::Blend) &&
                  glass->alpha == 12,
              "the glass is blended at 12");
        check(firstSurface(v, 3, tex, colour) && colour == 0x7FFF &&
                  !(v.textures[tex].flags & kTextureA3I5),
              "drawn at full brightness, in the ordinary form of its texture");

        const Brush *flat = brushOf(v, 7);
        check(flat && flat->colour == rgb555(255, 0, 0) && flat->alpha == 16, "the colour brush");
        check(firstSurface(v, 7, tex, colour) && tex == kNoTexture && colour == rgb555(255, 0, 0),
              "has no texture and its own colour at every vertex");

        // The world is untouched by all this.
        check(v.models[0].surfaceCount == 6, "the room itself is still six surfaces");
    }

    void testTexturePlanAndFiles()
    {
        BspFile bsp;
        std::string error;
        check(bsp.parse(buildBoxRoom(rulesRoom()), error), "parses");
        std::vector<Entity> ents;
        parseEntities(bsp.entities, ents, error);
        const std::vector<BrushInfo> brushes = analyseBrushes(bsp, ents);
        const TextureNeeds needs = textureNeeds(bsp, brushes, {});

        int grate = -1, glassTex = -1;
        for (std::size_t t = 0; t < bsp.textures.size(); ++t)
        {
            if (bsp.textures[t].name == "{grate")
                grate = static_cast<int>(t);
            if (bsp.textures[t].name == "glass")
                glassTex = static_cast<int>(t);
        }
        check(grate >= 0 && needs.opaque[grate] && needs.alpha[grate],
              "the grate texture is needed in both forms");
        check(glassTex >= 0 && needs.opaque[glassTex] && !needs.alpha[glassTex],
              "glass without a '{' name is only ever the opaque form");
        check(needs.opaque[0] && !needs.alpha[0], "the room's texture: opaque");

        TexturePlan plan = planTextures(bsp, needs);
        check(plan.fits && plan.usedTextures == 4, "four forms planned: wall, glass, grate twice");
        check(plan.alphaSizes[grate].width == 16 && plan.alphaTexels == 256,
              "the A3I5 form is counted a byte a texel");
        check(plan.colours == 16 * 3 + 32, "32 palette colours for the A3I5 form");

        plan = planTextures(bsp, needs, kWorldTexelBudget, kPaletteColourBudget, 100);
        check(!plan.fits, "an A3I5 budget too small even for the minimum size is reported");

        // A brush that is never drawn asks for no texture at all.
        BoxRoom lonely;
        lonely.brushes = {box("func_wall", "\"rendermode\" \"2\"\n\"renderamt\" \"0\"\n", "onlyhere", 0)};
        BspFile bsp2;
        check(bsp2.parse(buildBoxRoom(lonely), error), "parses");
        parseEntities(bsp2.entities, ents, error);
        const TextureNeeds none = textureNeeds(bsp2, analyseBrushes(bsp2, ents), {});
        check(!none.opaque[1] && !none.alpha[1], "a hidden brush's texture is not cooked");

        const fs::path exe = findPtexconv({});
        if (exe.empty())
        {
            std::printf("test_brush: ptexconv not found, texture file checks SKIPPED\n");
            return;
        }
        const fs::path dir = fs::temp_directory_path() / "hlds_test_brush";
        fs::remove_all(dir);
        PtexconvEncoder encoder(exe, dir / "tmp");
        WadSet wads;
        TextureCookReport report;
        plan = planTextures(bsp, needs);
        check(cookTextures(bsp, wads, plan, encoder, dir / "tex", report, error),
              "the rules room's textures cook");
        check(report.encoded == 4 && report.missing.empty(), "four files");
        check(fs::exists(dir / "tex" / "{grate_16x16.htx") &&
                  fs::exists(dir / "tex" / "{grate_16x16a.htx"),
              "the grate in both forms, side by side");

        std::vector<std::uint8_t> bytes;
        CookedTexture tex;
        TextureHeader header;
        check(readFile(dir / "tex" / "{grate_16x16a.htx", bytes, error) &&
                  unpackTexture(bytes, tex, header) && tex.format == TextureFormat::A3I5,
              "the A3I5 file reads back as A3I5");
        RgbaImage image;
        check(decodeTexture(tex, image, error), "and decodes");
        int holes = 0, solid = 0;
        for (int i = 0; i < 16 * 16; ++i)
        {
            holes += image.rgba[i * 4 + 3] == 0;
            solid += image.rgba[i * 4 + 3] == 255;
        }
        check(holes == 128 && solid == 128, "half holes, half solid: the checker survived");

        check(readFile(dir / "tex" / "{grate_16x16.htx", bytes, error) &&
                  unpackTexture(bytes, tex, header) && tex.format == TextureFormat::Tex4x4 &&
                  decodeTexture(tex, image, error),
              "the opaque form reads back as 4x4");
        solid = 0;
        for (int i = 0; i < 16 * 16; ++i)
            solid += image.rgba[i * 4 + 3] == 255;
        check(solid == 256, "and has no holes: on a plain wall the key colour is just a colour");
        fs::remove_all(dir);
    }

    /// The game ADDS a brush in render mode 3 or 5 to what is behind it:
    /// its texture is cooked as light (brightness is alpha), and it is
    /// see-through at any amount -- never a cutout, whose black would show.
    void testAdditiveBrushes()
    {
        BoxRoom room;
        room.brushes = {
            /* 1 */ box("func_illusionary", "\"rendermode\" \"5\"\n\"renderamt\" \"255\"\n", "beam", -40),
            /* 2 */ box("func_wall", "\"rendermode\" \"5\"\n\"renderamt\" \"128\"\n", "beam", -16),
            /* 3 */ box("func_wall", "\"rendermode\" \"2\"\n\"renderamt\" \"255\"\n", "beam", 8),
        };
        BspFile bsp;
        std::string error;
        check(bsp.parse(buildBoxRoom(room), error), "the additive room parses");
        std::vector<Entity> ents;
        parseEntities(bsp.entities, ents, error);
        const std::vector<BrushInfo> b = analyseBrushes(bsp, ents);
        check(b.size() == 3, "three brush entities");
        if (b.size() != 3)
            return;
        check(b[0].additive && b[0].asLight() && b[0].renderClass == RenderClass::Blend && b[0].alpha == 31 &&
                  b[0].fullBright() && !b[0].cutsHoles(),
              "render mode 5 at full amount: blended light, not a cutout");
        check(b[1].additive && b[1].renderClass == RenderClass::Blend && b[1].alpha == 16,
              "render mode 5 at 128: blended at 16");
        check(!b[2].additive && b[2].renderClass == RenderClass::Cutout, "render mode 2 at full amount: as before");

        int beam = -1;
        for (std::size_t t = 0; t < bsp.textures.size(); ++t)
        {
            if (bsp.textures[t].name == "beam")
                beam = static_cast<int>(t);
        }
        const TextureNeeds needs = textureNeeds(bsp, b, {});
        check(beam >= 0 && needs.light[beam] && needs.opaque[beam] && !needs.alpha[beam],
              "the beam texture: light for the additive brushes, opaque for the other");
        const TexturePlan plan = planTextures(bsp, needs);
        check(beam >= 0 && plan.lightSizes[beam].width != 0 && plan.alphaSizes[beam].width == 0,
              "the light form is planned");
        check(beam >= 0 &&
                  plan.alphaTexels == static_cast<int>(plan.lightSizes[beam].width) * plan.lightSizes[beam].height,
              "and counted in the A3I5 budget");

        const Cooked c = cook(room);
        check(c.ok, "the additive room cooks");
        if (!c.ok)
            return;
        std::uint16_t tex = 0, colour = 0, other = 0;
        check(firstSurface(c.view, 1, tex, colour) && tex != kNoTexture &&
                  (c.view.textures[tex].flags & kTextureA3I5) && (c.view.textures[tex].flags & kTextureLight) &&
                  colour == 0x7FFF,
              "the additive brush wears the light form, at full brightness");
        check(firstSurface(c.view, 2, other, colour) && other == tex, "both additive brushes share it");
        check(firstSurface(c.view, 3, other, colour) && other != tex &&
                  !(c.view.textures[other].flags & (kTextureA3I5 | kTextureLight)),
              "the render-mode-2 brush keeps the opaque form");

        // Light into alpha: black is nothing; colour raised to full.
        RgbaImage image;
        image.width = 3;
        image.height = 1;
        image.rgba = {255, 128, 0, 255, 100, 50, 0, 255, 0, 0, 0, 255};
        lightToAlpha(image);
        check(image.rgba[0] == 255 && image.rgba[1] == 128 && image.rgba[2] == 0 && image.rgba[3] == 255,
              "a full-bright texel stays itself, opaque");
        check(image.rgba[4] == 255 && image.rgba[5] == 128 && image.rgba[6] == 0 && image.rgba[7] == 100,
              "a dim one: the same colour at full, its brightness as alpha");
        check(image.rgba[11] == 0, "black: not there at all");
    }

    void testReaderRefusesBadBrushes()
    {
        BspFile bsp;
        std::string error;
        CookedLevel good;
        CookReport report;
        bsp.parse(buildBoxRoom(rulesRoom()), error);
        check(cookLevel(bsp, CookOptions(), TexturePlan(), good, report, error), "cooks");
        std::vector<std::uint8_t> bytes;
        struct Case
        {
            const char *what;
            void (*corrupt)(CookedLevel &);
        };
        const Case cases[] = {
            {"brush model 0 (the world)", [](CookedLevel &l) { l.brushes[0].model = 0; }},
            {"brush model out of range", [](CookedLevel &l) { l.brushes[0].model = 99; }},
            {"brush render class", [](CookedLevel &l) { l.brushes[0].renderClass = 9; }},
            {"brush leaf range", [](CookedLevel &l) { l.brushes[0].leafCount = 999; }},
            {"brush leaf index", [](CookedLevel &l) { l.brushLeaves[0] = 999; }},
        };
        for (const Case &c : cases)
        {
            CookedLevel level = good;
            c.corrupt(level);
            check(!writeLevel(level, bytes, error), c.what);
        }
        check(writeLevel(good, bytes, error), "and the untouched level writes");
    }
} // namespace

int main()
{
    testClassification();
    testCookedBrushes();
    testTexturePlanAndFiles();
    testAdditiveBrushes();
    testReaderRefusesBadBrushes();
    if (g_failures == 0)
        std::printf("test_brush: ok\n");
    return g_failures == 0 ? 0 : 1;
}
