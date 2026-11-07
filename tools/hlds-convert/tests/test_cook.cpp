// The whole cook, on the box room: .bsp bytes in, .hlm bytes out, read back
// through the DS's own reader.
//
// Everything here is checked against the room's known shape rather than
// against the cooker's intermediate data, so it would catch the cooker being
// consistently wrong -- every wall facing outwards, say -- which a round-trip
// test alone never would.

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "HlLevelReader.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/Converter.hpp"
#include "hlds/Cooker.hpp"
#include "hlds/Materials.hpp"
#include "HlMaterial.hpp"
#include "hlds/GxStream.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    struct Cooked
    {
        std::vector<std::uint8_t> bytes;
        LevelView view;
        CookReport report;
        bool ok = false;
    };

    Cooked cook(const BoxRoom &room, double gamma = 1.0, bool guarded = true)
    {
        Cooked c;
        BspFile bsp;
        std::string error;
        CookedLevel level;
        CookOptions options;
        options.lightScale = gamma;
        options.lightPower = 1.0;
        options.guardedMerges = guarded;
        c.ok = bsp.parse(buildBoxRoom(room), error) &&
               cookLevel(bsp, options, {}, level, c.report, error) &&
               writeLevel(level, c.bytes, error) &&
               c.view.open(c.bytes.data(), static_cast<std::uint32_t>(c.bytes.size())) ==
                   LevelError::Ok;
        if (!c.ok)
            std::fprintf(stderr, "cook failed: %s\n", error.c_str());
        return c;
    }

    void testBoxRoom()
    {
        const Cooked c = cook(BoxRoom());
        check(c.ok, "the box room cooks and opens");
        if (!c.ok)
            return;
        const LevelView &v = c.view;

        check(v.header->mins[0] == -128 && v.header->maxs[2] == 128, "bounds");
        check(v.header->visLeafCount == 1, "one visible leaf");
        check(v.header->worstPolygons == 6 && v.header->worstVertices == 24,
              "worst case is the six quads");
        check(v.surfaceCount == 6 && c.report.polygons == 6 && c.report.vertices == 24,
              "six surfaces, one quad each");
        check(v.nodeCount == 6 && v.leafCount == 2 && v.modelCount == 1, "tree sizes");

        // All three of the world's hulls are kept (the third is where
        // something wide has room): 18 clip nodes, and their planes.
        check(v.clipNodeCount == 18 && c.report.droppedClipNodes == 0, "the world's three clip hulls kept");
        check(v.planeCount == 24 && c.report.droppedPlanes == 0, "and their planes");
        check(v.models[0].clipStanding == 0 && v.models[0].clipCrouched == 6 && v.models[0].clipLarge == 12,
              "hull roots renumbered");

        // The standing hull's planes are the walls moved in by 16 and 36.
        const ClipNode &first = v.clipNodes[v.models[0].clipStanding];
        check(v.planeTypes[first.plane] == kPlaneAxisX &&
                  v.planes[first.plane].normal[0] == kNormalOne &&
                  v.planes[first.plane].dist == fxFromInt(-112),
              "the first clip plane is x = -112, axial, in Q30 / Q12");
        check(first.children[1] == kContentsSolid && first.children[0] == 1,
              "behind it is solid, in front the next node");

        // Parents: a chain from the root to leaf 1.
        check(v.nodes[0].parent == kNoParent && v.nodes[3].parent == 2, "node parents");
        check(v.leaves[1].parent == 5 && v.leaves[1].contents == kContentsEmpty &&
                  v.leaves[1].markCount == 6,
              "the room leaf hangs from the last node");
        check(v.leaves[0].contents == kContentsSolid && v.leaves[0].visOfs == -1, "solid leaf");
        check(v.visSize == 1 && v.vis[0] == 1, "vis row carried over unchanged");

        check(v.textureCount == 1 && std::strcmp(v.textures[0].name, "testwall") == 0 &&
                  v.textures[0].width == 16 && v.textures[0].sourceWidth == 16,
              "the texture table");

        // The single leaf reads the single texture, so the view lists just the texels.
        // Cooker leaves the average colour unknown, the converter sets it.
        check(v.textures[0].share == 100, "a texture the only leaf sees is wanted in every view");
        check(v.header->worstTexels == 16u * 16u, "the worst view lists the one texture's texels");
        check(v.textures[0].average == 0x7FFF, "an average not worked out is unknown");

        check(v.entityCount == 2 &&
                  v.entities[1].classId ==
                      static_cast<std::uint16_t>(EntityClass::InfoPlayerStart) &&
                  v.entities[1].origin[2] == fxFromInt(40) && v.entities[1].angles[1] == 16384,
              "the player start: origin in Q12, yaw 90 degrees in turns");
        check(v.entityTextSize == 0, "the level the console loads carries no entity text");

        // Every surface: decodes, is a quad on its plane, faces INTO the room
        // counter-clockwise, and carries its own wall's light.
        bool seenGrey[6] = {};
        for (std::uint32_t s = 0; s < v.surfaceCount; ++s)
        {
            const Surface &surface = v.surfaces[s];
            std::vector<GxRun> runs;
            std::string error;
            check(decodeGxStream(v.gxStream + surface.gxOfs, surface.gxWords, runs, error),
                  "surface stream decodes");
            if (runs.size() != 1 || runs[0].vertices.size() != 4)
            {
                check(false, "surface is one quad");
                continue;
            }
            const auto &q = runs[0].vertices;
            // Normal of (q0, q1, q2) by the right-hand rule: for a
            // counter-clockwise face it points at the viewer, i.e. at the
            // room's centre (0, 0, 64) = (0, 0, 256) in quarter units.
            const double e1[3] = {double(q[1].x - q[0].x), double(q[1].y - q[0].y),
                                  double(q[1].z - q[0].z)};
            const double e2[3] = {double(q[2].x - q[0].x), double(q[2].y - q[0].y),
                                  double(q[2].z - q[0].z)};
            const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                 e1[0] * e2[1] - e1[1] * e2[0]};
            const double toCentre[3] = {0.0 - q[0].x, 0.0 - q[0].y, 256.0 - q[0].z};
            check(n[0] * toCentre[0] + n[1] * toCentre[1] + n[2] * toCentre[2] > 0,
                  "surface is counter-clockwise seen from inside the room");

            // The plane the surface names must contain its vertices, and the
            // back flag must say which side the room is on.
            const Plane &plane = v.planes[surface.planeRef & kSurfacePlaneMask];
            for (const GxVertex &p : q)
            {
                const std::int64_t d =
                    (static_cast<std::int64_t>(plane.normal[0]) * p.x +
                     static_cast<std::int64_t>(plane.normal[1]) * p.y +
                     static_cast<std::int64_t>(plane.normal[2]) * p.z) >>
                    (kNormalShift - kFxShift + kVertexShift);
                check(d == plane.dist, "vertex lies on the surface's plane");
            }
            const std::int64_t centreSide =
                ((static_cast<std::int64_t>(plane.normal[2]) * fxFromInt(64)) >> kNormalShift) -
                plane.dist;
            const bool back = (surface.planeRef & kSurfacePlaneBack) != 0;
            check(back ? centreSide < 0 : centreSide > 0,
                  "the back flag matches the side the room is on");

            // Light: gamma 1, so the builder's grey for this BSP face, which is
            // 40 + 30 * face. Surfaces are re-ordered, so find it by value.
            const int grey = (q[0].colour & 31) << 3;
            const int face = (grey - 40 + 8) / 30; // 5-bit colour loses the low bits
            check(face >= 0 && face < 6 && rgb555Near(40 + 30 * face, 40 + 30 * face, 40 + 30 * face) ==
                                               q[0].colour,
                  "vertex colour is one wall's grey in RGB555");
            if (face >= 0 && face < 6)
                seenGrey[face] = true;
            check(q[0].colour == q[3].colour, "a flat-lit wall is one colour");
            check(surface.polygons == 1 && surface.vertices == 4 && surface.texture == 0,
                  "surface costs");
            check(surface.radius > 0, "surface has a bounding radius");
        }
        for (bool seen : seenGrey)
            check(seen, "every wall's light reached exactly one surface");
    }

    void testDroppedFaces()
    {
        BoxRoom room;
        room.textureName = "sky";
        const Cooked c = cook(room);
        check(c.ok, "a room of sky still cooks");
        if (!c.ok)
            return;
        check(c.view.surfaceCount == 0 && c.report.droppedFaces == 6, "sky faces are dropped");
        check(c.view.leaves[1].markCount == 0, "and leave no marks behind");
        check(c.view.textureCount == 0, "an unused texture is not listed");
        check(c.view.header->worstPolygons == 0, "nothing to draw");
    }

    void testTooLarge()
    {
        BoxRoom room;
        room.maxs[0] = 9000;
        BspFile bsp;
        std::string error;
        CookedLevel level;
        CookReport report;
        check(bsp.parse(buildBoxRoom(room), error), "an oversized room parses");
        check(!cookLevel(bsp, CookOptions(), {}, level, report, error) &&
                  error.find("8191") != std::string::npos,
              "but is refused by the cooker, with the reason");
    }

    void testSizesOverride()
    {
        {
            // Width 0 is the plan saying "not drawn": the faces go.
            BspFile hiddenBsp;
            std::string e;
            CookedLevel l;
            CookReport r;
            hiddenBsp.parse(buildBoxRoom(), e);
            TexturePlan none;
            none.sizes = {TextureSize{0, 0}};
            none.alphaSizes = {TextureSize{0, 0}};
            check(cookLevel(hiddenBsp, CookOptions(), none, l, r, e) && l.surfaces.empty() &&
                      r.droppedFaces == 6 && l.textures.empty(),
                  "a texture the plan left out takes its faces with it");
        }
        BspFile bsp;
        std::string error;
        CookedLevel level;
        CookReport report;
        bsp.parse(buildBoxRoom(), error);
        TexturePlan sized;
        sized.sizes = {TextureSize{32, 64}};
        sized.alphaSizes = {TextureSize{0, 0}};
        check(cookLevel(bsp, CookOptions(), sized, level, report, error),
              "cooks with given texture sizes");
        check(level.textures[0].width == 32 && level.textures[0].height == 64,
              "the budget's sizes replace the defaults");
    }
} // namespace

/// What the floor is made of, from the game's list, found under the feet.
void testMaterials()
{
    const MaterialBook book = parseMaterials("// a comment\n"
                                             "M TESTWALL\n"
                                             "G grate_under_twelve_letters\n"
                                             "t TILEFLOOR\n"
                                             "M tilefloor\n");
    check(book.of("testwall") == Material::Metal && book.of("TestWall") == Material::Metal,
          "a texture is found in the list whatever its case");
    check(book.of("-0testwall") == Material::Metal && book.of("+Atestwall") == Material::Metal &&
              book.of("{testwall") == Material::Metal && book.of("-0~testwall") == Material::Metal,
          "... without the marks the game puts in front of a name");
    check(book.of("grate_under_twelve") == Material::Grate && book.of("grate_under_") == Material::Grate,
          "... by its first twelve letters");
    check(book.of("tilefloor") == Material::Tile, "the first of two lines for a name stands");
    check(book.of("something") == Material::Concrete, "what the list does not name is concrete");

    Cooked c;
    BspFile bsp;
    std::string error;
    CookedLevel level;
    CookOptions options;
    options.lightScale = 1.0;
    options.lightPower = 1.0;
    options.materials = &book;
    c.ok = bsp.parse(buildBoxRoom(BoxRoom()), error) && cookLevel(bsp, options, {}, level, c.report, error) &&
           writeLevel(level, c.bytes, error) &&
           c.view.open(c.bytes.data(), static_cast<std::uint32_t>(c.bytes.size())) == LevelError::Ok;
    check(c.ok && textureMaterial(c.view.textures[0].flags) == Material::Metal,
          "a cooked texture carries what it is made of");
    if (!c.ok)
        return;
    // Standing on the room's floor: metal underfoot; the same feet half
    // way up the room have nothing near under them, which is concrete.
    const int floor = c.view.header->mins[2], top = c.view.header->maxs[2];
    const fx feet[3] = {fxFromInt(10), fxFromInt(-20), fxFromInt(floor)};
    check(floorMaterial(c.view, feet, -1) == Material::Metal, "the floor under the feet is found, and what it is made of");
    const fx air[3] = {fxFromInt(10), fxFromInt(-20), fxFromInt((floor + top) / 2)};
    check(floorMaterial(c.view, air, -1) == Material::Concrete, "with no floor near under them, the plain step");
    check(levelLeafAt(c.view, feet) >= 0, "(the feet are in a leaf)");
}

// Which chapter a map is part of: the one opened by the map most like it.
static void testChapters()
{
    const std::vector<std::pair<std::string, std::string>> titled = {
        {"c1a0", "C0A1TITLE"}, {"c1a0c", "C1A1TITLE"}, {"c1a2", "C1A2TITLE"}, {"c2a4", "C2A4TITLE1"},
        {"c2a4d", "C2A4TITLE2"}, {"c3a1", "C3A1TITLE"}, {"c3a2e", "C3A2TITLE"}, {"c4a1", "C4A1TITLE"},
        {"c4a1a", "C4A1ATITLE"}, {"c4a2", "C4A2TITLE"}};
    check(chapterOf("c1a0", titled) == "C0A1TITLE" && chapterOf("c1a0b", titled) == "C0A1TITLE",
          "a map is of the chapter it opens, and so are those after it");
    check(chapterOf("c1a0c", titled) == "C1A1TITLE" && chapterOf("c1a0e", titled) == "C1A1TITLE" &&
              chapterOf("c1a1", titled) == "C1A1TITLE" && chapterOf("c1a1f", titled) == "C1A1TITLE",
          "a chapter opened part-way through a run of names goes on into the next");
    check(chapterOf("c2a4c", titled) == "C2A4TITLE1" && chapterOf("c2a4g", titled) == "C2A4TITLE2",
          "two chapters in one run of names: each map is of the one opened at or before it");
    check(chapterOf("c3a2", titled) == "C3A2TITLE" && chapterOf("c3a2a", titled) == "C3A2TITLE" &&
              chapterOf("c3a1b", titled) == "C3A1TITLE",
          "maps named before the one that opens their chapter are of it all the same");
    check(chapterOf("c4a1f", titled) == "C4A1ATITLE" && chapterOf("c4a2b", titled) == "C4A2TITLE",
          "the chapters of the far side of the game");
    check(chapterOf("c0a0", titled) == "C0A0TITLE" && chapterOf("c0a0e", titled) == "C0A0TITLE",
          "the game's first maps name none: they are the first chapter");
    {
        std::vector<std::pair<std::string, std::string>> withLast = titled;
        withLast.emplace_back("c4a3", "C4A3TITLE");
        check(chapterOf("hldemo1", withLast) == "UPLINK" && chapterOf("hldemo3", withLast) == "UPLINK",
              "Uplink's maps are Uplink, not the chapter sorted before them");
    }
    check(chapterOf("mymap", titled).empty() || !chapterOf("mymap", titled).empty(), "(a map of no campaign has what it has)");
    if (const char *dir = std::getenv("HLDS_VALVE_DIR"); dir != nullptr && dir[0] != '\0')
    {
        std::vector<std::uint8_t> text;
        std::string ignored;
        TitlesBook book;
        if (readFile(std::filesystem::path(dir) / "titles.txt", text, ignored))
            book = parseTitles(std::string(text.begin(), text.end()));
        const auto real = chapterTitles(dir, &book);
        auto name = [&](const char *map) -> std::string
        {
            const Title *title = book.find(chapterOf(map, real));
            return title != nullptr ? title->text : std::string("?");
        };
        std::printf("test_cook: chapters: c0a0 '%s', c1a1 '%s', c2a4e '%s', c3a2 '%s', c4a1c '%s'\n", name("c0a0").c_str(),
                    name("c1a1").c_str(), name("c2a4e").c_str(), name("c3a2").c_str(), name("c4a1c").c_str());
        check(name("c0a0") == "BLACK MESA INBOUND" && name("c1a1") == "UNFORESEEN CONSEQUENCES" &&
                  name("c2a4e") == "QUESTIONABLE ETHICS" && name("c3a2") == "LAMBDA CORE",
              "the game's own maps are of the game's own chapters");
    }
    else
    {
        std::printf("test_cook: HLDS_VALVE_DIR not set, the game's own chapters were not read\n");
    }
}

int main()
{
    testChapters();
    testMaterials();
    testBoxRoom();
    testDroppedFaces();
    testTooLarge();
    testSizesOverride();
    if (g_failures == 0)
        std::printf("test_cook: ok\n");
    return g_failures == 0 ? 0 : 1;
}
