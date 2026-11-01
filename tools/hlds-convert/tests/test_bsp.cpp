// The BSP reader, against a level built in memory (TestBspBuilder.hpp).
//
// Two things are worth more than they look. The reader's promise is that a map
// it accepts can be indexed without checks, so the corrupt-file cases here are
// the ones that keep a damaged download from becoming a crash in the cooker.
// And the PVS polygon count is the number a map is judged for heavy fog use from,
// so it is pinned on a level small enough to count by hand.

#include <cstring>
#include <cstdio>

#include "TestBspBuilder.hpp"
#include "hlds/MapStats.hpp"

using namespace hlds;
using namespace hldstest;

namespace
{
    void testParse()
    {
        BspFile bsp;
        std::string error;
        check(bsp.parse(buildBoxRoom(), error), "the box room parses");
        check(bsp.faces.size() == 6, "six faces");
        check(bsp.vertices.size() == 24, "four vertices a face");
        check(bsp.nodes.size() == 6, "six nodes");
        check(bsp.leaves.size() == 2, "the solid leaf and the room");
        check(bsp.clipNodes.size() == 18, "three clip hulls of six");
        check(bsp.models.size() == 1 && bsp.models[0].faceCount == 6, "one model, six faces");
        check(bsp.textures.size() == 1 && bsp.textures[0].name == "testwall", "texture name");
        check(bsp.textures[0].embedded && bsp.textures[0].pixels.size() == 256 &&
                  bsp.textures[0].palette.size() == 768,
              "embedded texture carries pixels and palette");
        check(bsp.entities.find("info_player_start") != std::string::npos, "entity text");
        check(bsp.entities.find('\0') == std::string::npos, "entity text has no NUL");
    }

    /// Every face must be wound clockwise as seen from inside the room, which
    /// is what faceVertex() and the surfedge sign handling are for.
    void testWinding()
    {
        BspFile bsp;
        std::string error;
        bsp.parse(buildBoxRoom(), error);
        const float centre[3] = {0, 0, 64};
        for (const BspFace &face : bsp.faces)
        {
            const float *a = bsp.faceVertex(face, 0).point;
            const float *b = bsp.faceVertex(face, 1).point;
            const float *c = bsp.faceVertex(face, 2).point;
            const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                e1[0] * e2[1] - e1[1] * e2[0]};
            const float toCentre[3] = {centre[0] - a[0], centre[1] - a[1], centre[2] - a[2]};
            const float d = n[0] * toCentre[0] + n[1] * toCentre[1] + n[2] * toCentre[2];
            check(d < 0, "face is clockwise seen from inside the room");

            // And the plane/side pair must agree with the geometry.
            const BspPlane &plane = bsp.planes[face.plane];
            const float sign = face.side ? -1.0f : 1.0f;
            const float facing = sign * (plane.normal[0] * toCentre[0] +
                                         plane.normal[1] * toCentre[1] +
                                         plane.normal[2] * toCentre[2]);
            check(facing > 0, "face side looks into the room");
        }
    }

    void testVisAndStats()
    {
        BspFile bsp;
        std::string error;
        bsp.parse(buildBoxRoom(), error);
        check(bsp.visLeafCount() == 1, "one visible leaf");
        const auto row = bsp.decompressVis(1);
        check(row.size() == 1 && row[0] == 0x01, "leaf 1 sees leaf 1");

        const MapStats stats = computeMapStats(bsp);
        check(stats.drawnWorldFaces == 6, "six drawn faces");
        check(stats.worst.leaf == 1 && stats.worst.faces == 6, "worst leaf is the room");
        check(stats.worst.cost.triangles == 12, "six quads are twelve triangles");
        check(stats.worst.cost.polygons == 6, "or six polygons as quads");
        check(stats.worst.cost.vertices == 24, "and twenty-four vertices");
        check(stats.classCounts.at("info_player_start") == 1, "entity classes counted");

        check(faceCost(3).polygons == 1 && faceCost(3).vertices == 3, "a triangle");
        check(faceCost(5).polygons == 2 && faceCost(5).vertices == 7, "a pentagon: quad + tri");
        check(faceCost(6).polygons == 2 && faceCost(6).vertices == 8, "a hexagon: two quads");
    }

    /// Zero runs, and a row that runs off the end of the lump.
    void testVisRuns()
    {
        BspFile bsp;
        std::string error;
        bsp.parse(buildBoxRoom(), error);
        bsp.models[0].visLeafs = 40; // five bytes a row
        bsp.leaves.resize(41, bsp.leaves[1]);
        bsp.visibility = {0x81, 0x00, 0x03, 0xFF};
        bsp.leaves[1].visOfs = 0;
        auto row = bsp.decompressVis(1);
        check(row.size() == 5, "row is five bytes");
        check(row[0] == 0x81 && row[1] == 0 && row[2] == 0 && row[3] == 0 && row[4] == 0xFF,
              "a zero byte expands to its run");

        bsp.visibility = {0x81};
        row = bsp.decompressVis(1);
        check(row[0] == 0x81 && row[4] == 0, "a truncated row stays in bounds");

        bsp.leaves[1].visOfs = -1;
        row = bsp.decompressVis(1);
        check(row[0] == 0xFF && row[4] == 0xFF, "no row means everything is visible");
    }

    /// The ZHLT quirk two retail maps ship with: a model with no clip hull has
    /// its roots one past the end of the lump.
    void testEmptyHull()
    {
        BspFile bsp;
        std::string error;
        std::vector<std::uint8_t> bytes = buildBoxRoom();
        std::int32_t modelsOfs = 0;
        std::memcpy(&modelsOfs, bytes.data() + 4 + 14 * 8, 4);
        const std::int32_t pastEnd = 18; // the room has 18 clip nodes
        std::memcpy(bytes.data() + modelsOfs + 36 + 2 * 4, &pastEnd, 4); // headNode[2]
        check(bsp.parse(bytes, error), "a clip root one past the end is accepted");
        check(bsp.clipHullRoot(0, 2) == kContentsEmpty, "and read as an empty hull");
        check(bsp.clipHullRoot(0, 1) == 0 && bsp.clipHullRoot(0, 3) == 12, "real roots pass through");
    }

    void testUndrawn()
    {
        BoxRoom room;
        room.textureName = "sky";
        BspFile bsp;
        std::string error;
        bsp.parse(buildBoxRoom(room), error);
        check(computeMapStats(bsp).drawnWorldFaces == 0, "sky faces are not world polygons");
        check(isUndrawnTexture("aaatrigger") && !isUndrawnTexture("!water"), "tool textures");
    }

    void testRejects()
    {
        BspFile bsp;
        std::string error;
        const std::vector<std::uint8_t> good = buildBoxRoom();

        check(!bsp.parse({1, 2, 3}, error), "a three-byte file is refused");

        std::vector<std::uint8_t> bad = good;
        bad[0] = 29;
        check(!bsp.parse(bad, error) && error.find("version") != std::string::npos,
              "a Quake BSP is refused by version");

        bad = good;
        bad.resize(bad.size() / 2);
        check(!bsp.parse(bad, error), "a truncated file is refused");
        check(bsp.faces.empty(), "and leaves nothing behind");

        // Corrupt one index in each lump that holds one. Offsets come from the
        // header so this does not depend on the builder's layout.
        auto lumpOffset = [&](int lump)
        {
            std::int32_t ofs = 0;
            std::memcpy(&ofs, good.data() + 4 + lump * 8, 4);
            return ofs;
        };
        struct Poke
        {
            int lump;
            int byteOffset;
            const char *what;
        };
        const Poke pokes[] = {
            {7, 0, "a face with a bad plane is refused"},
            {7, 10, "a face with a bad texinfo is refused"},
            {12, 4, "an edge with a bad vertex is refused"},
            {13, 0, "a surfedge with a bad edge is refused"},
            {11, 0, "a marksurface with a bad face is refused"},
            {5, 0, "a node with a bad plane is refused"},
            {5, 4, "a node with a bad child is refused"},
            {9, 0, "a clipnode with a bad plane is refused"},
            {6, 32, "a texinfo with a bad texture is refused"},
        };
        for (const Poke &poke : pokes)
        {
            bad = good;
            bad[lumpOffset(poke.lump) + poke.byteOffset] = 0xF0;
            bad[lumpOffset(poke.lump) + poke.byteOffset + 1] = 0x7F;
            check(!bsp.parse(bad, error), poke.what);
        }
    }
} // namespace

int main()
{
    testParse();
    testWinding();
    testVisAndStats();
    testVisRuns();
    testEmptyHull();
    testUndrawn();
    testRejects();
    if (g_failures == 0)
        std::printf("test_bsp: ok\n");
    return g_failures == 0 ? 0 : 1;
}
