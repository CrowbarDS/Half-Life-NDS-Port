// The cooked level format: what the converter writes is what the DS reads.
//
// LevelView::open() is compiled here from shared/, the same source the ARM9
// builds, so this is the console's reader run on the converter's writer. The
// second half is the reader's promise -- that a level it accepts can be indexed
// without checks -- tested the only way that means anything: by corrupting
// every kind of index and requiring a refusal.

#include <cstdio>
#include <cstring>

#include "HlLevelReader.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/LevelWriter.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    /// A two-leaf level, small enough that every field is chosen by hand.
    CookedLevel smallLevel()
    {
        CookedLevel level;
        level.mins[0] = -128;
        level.maxs[2] = 128;
        level.visLeafCount = 1;
        level.worstPolygons = 6;
        level.worstVertices = 24;

        Plane plane{};
        plane.normal[0] = kNormalOne;
        plane.dist = fxFromInt(-128);
        level.planes = {plane, plane};
        level.planeTypes = {kPlaneAxisX, kPlaneOther};

        Node node{};
        node.plane = 0;
        node.children[0] = -2; // leaf 1
        node.children[1] = -1; // leaf 0
        node.parent = kNoParent;
        level.nodes = {node};

        Leaf solid{};
        solid.visOfs = -1;
        solid.contents = kContentsSolid;
        solid.parent = 0;
        Leaf room{};
        room.visOfs = 0;
        room.firstMark = 0;
        room.markCount = 2;
        room.contents = kContentsEmpty;
        room.parent = 0;
        level.leaves = {solid, room};

        level.marks = {0, 1};
        level.vis = {0x01}; // an odd size: exercises the padding

        Surface surface{};
        surface.gxOfs = 0;
        surface.gxWords = 3;
        surface.planeRef = 1 | kSurfacePlaneBack;
        surface.texture = 0;
        surface.polygons = 1;
        surface.vertices = 4;
        Surface second = surface;
        second.gxOfs = 3;
        second.gxWords = 2;
        level.surfaces = {surface, second};
        level.gxStream = {0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555};

        Model world{};
        world.headNode = 0;
        world.clipStanding = 0;
        world.clipCrouched = kContentsEmpty;
        world.firstSurface = 0;
        world.surfaceCount = 2;
        level.models = {world};

        ClipNode clip{};
        clip.plane = 1;
        clip.children[0] = kContentsEmpty;
        clip.children[1] = kContentsSolid;
        level.clipNodes = {clip};

        TextureRef tex{};
        std::strcpy(tex.name, "testwall");
        tex.width = 16;
        tex.height = 16;
        tex.sourceWidth = 32;
        tex.sourceHeight = 32;
        tex.frames = 1;
        tex.other = kNoTexture;
        level.textures = {tex};

        EntityRecord start{};
        start.classId = static_cast<std::uint16_t>(EntityClass::InfoPlayerStart);
        start.origin[2] = fxFromInt(40);
        start.angles[1] = angleFromDegrees(90);
        level.entities = {start};
        level.entityText = "{\n\"classname\" \"worldspawn\"\n}\n";
        return level;
    }

    void testRoundTrip()
    {
        const CookedLevel level = smallLevel();
        std::vector<std::uint8_t> bytes;
        std::string error;
        check(writeLevel(level, bytes, error), "the level writes");
        check(bytes.size() % 4 == 0, "the file is a whole number of words");

        LevelView view;
        check(view.open(bytes.data(), static_cast<std::uint32_t>(bytes.size())) ==
                  LevelError::Ok,
              "and the DS reader opens it");
        check(view.header->version == kLevelVersion && view.header->visLeafCount == 1 &&
                  view.header->worstVertices == 24 && view.header->mins[0] == -128 &&
                  view.header->maxs[2] == 128,
              "header fields survive");
        check(view.planeCount == 2 && view.planes[0].normal[0] == kNormalOne &&
                  view.planes[0].dist == fxFromInt(-128) && view.planeTypes[1] == kPlaneOther,
              "planes");
        check(view.nodeCount == 1 && view.nodes[0].children[0] == -2, "nodes");
        check(view.leafCount == 2 && view.leaves[1].markCount == 2 &&
                  view.leaves[1].contents == kContentsEmpty,
              "leaves");
        check(view.markCount == 2 && view.marks[1] == 1, "marks");
        check(view.visSize == 1 && view.vis[0] == 0x01, "vis");
        check(view.surfaceCount == 2 && view.surfaces[1].gxOfs == 3 &&
                  view.surfaces[0].planeRef == (1 | kSurfacePlaneBack),
              "surfaces");
        check(view.gxWordCount == 5 && view.gxStream[4] == 0x55555555, "gx stream");
        check(view.modelCount == 1 && view.models[0].clipCrouched == kContentsEmpty, "models");
        check(view.clipNodeCount == 1 && view.clipNodes[0].children[1] == kContentsSolid,
              "clipnodes");
        check(view.textureCount == 1 && std::strcmp(view.textures[0].name, "testwall") == 0 &&
                  view.textures[0].sourceWidth == 32,
              "textures");
        check(view.entityCount == 1 && view.entities[0].origin[2] == fxFromInt(40) &&
                  view.entities[0].angles[1] == 16384,
              "entities");
        check(view.entityTextSize == 0, "no entity text: that stays on the PC");
    }

    void testFileFaults()
    {
        std::vector<std::uint8_t> good;
        std::string error;
        writeLevel(smallLevel(), good, error);
        const auto size = static_cast<std::uint32_t>(good.size());
        LevelView view;

        check(view.open(good.data(), 10) == LevelError::TooSmall, "a stub is too small");

        auto bad = good;
        bad[0] = 'X';
        check(view.open(bad.data(), size) == LevelError::BadMagic, "wrong magic");

        // Min and target: a file older than this build reads, or one that
        // needs a newer build, is refused; a newer file an old build can
        // read (its minReader is not above this build's) is read.
        bad = good;
        bad[4] = static_cast<std::uint8_t>(kLevelMinVersion - 1);
        bad[5] = 0;
        check(view.open(bad.data(), size) == LevelError::BadVersion, "a version older than this build reads");
        bad[4] = 49; // before the texture records said their palettes' sizes
        check(kLevelMinVersion == 50 && view.open(bad.data(), size) == LevelError::BadVersion && view.header == nullptr,
              "a map of format 49 is refused, and nothing of it is read");
        bad = good;
        bad[38] = static_cast<std::uint8_t>(kLevelVersion + 1); // minReader
        bad[39] = 0;
        check(view.open(bad.data(), size) == LevelError::BadVersion, "a file that needs a newer build");
        bad = good;
        bad[4] = static_cast<std::uint8_t>(kLevelVersion + 1);
        bad[5] = 0;
        check(view.open(bad.data(), size) == LevelError::Ok, "a newer file this build can still read");
        check(view.header != nullptr && view.header->minReader == kLevelMinReader, "the file says who can read it");

        check(view.open(good.data(), size - 4) == LevelError::BadSize, "a truncated file");

        bad = good;
        bad[size - 1] ^= 1;
        check(view.open(bad.data(), size) == LevelError::BadChecksum, "a flipped bit");
        check(view.header == nullptr && view.planes == nullptr, "a refused view is empty");

        // With the checksum out of the way, the layout checks must stand alone:
        // they are what protects the DS from a file whose CRC happens to match.
        bad = good;
        std::uint32_t huge = 0xFFFFFFF0u;
        std::memcpy(bad.data() + sizeof(LevelHeader), &huge, 4); // Planes offset
        check(view.open(bad.data(), size, false) == LevelError::BadLayout,
              "a section offset that would wrap");
        bad = good;
        std::uint32_t odd = kLevelDataStart + 2;
        std::memcpy(bad.data() + sizeof(LevelHeader), &odd, 4);
        check(view.open(bad.data(), size, false) == LevelError::BadLayout,
              "a misaligned section");
        bad = good;
        std::uint32_t count = 99;
        std::memcpy(bad.data() + sizeof(LevelHeader) + 8, &count, 4); // Planes count
        check(view.open(bad.data(), size, false) == LevelError::BadLayout,
              "a count that disagrees with the size");
    }

    void testIndexFaults()
    {
        std::string error;
        std::vector<std::uint8_t> bytes;
        struct Case
        {
            const char *what;
            void (*corrupt)(CookedLevel &);
        };
        const Case cases[] = {
            {"node plane", [](CookedLevel &l) { l.nodes[0].plane = 2; }},
            {"node child node", [](CookedLevel &l) { l.nodes[0].children[0] = 1; }},
            {"node child leaf", [](CookedLevel &l) { l.nodes[0].children[1] = -3; }},
            {"node parent", [](CookedLevel &l) { l.nodes[0].parent = 5; }},
            {"leaf marks", [](CookedLevel &l) { l.leaves[1].markCount = 3; }},
            {"leaf vis offset", [](CookedLevel &l) { l.leaves[1].visOfs = 1; }},
            {"leaf vis offset below -1", [](CookedLevel &l) { l.leaves[1].visOfs = -2; }},
            {"leaf parent", [](CookedLevel &l) { l.leaves[1].parent = 1; }},
            {"mark surface", [](CookedLevel &l) { l.marks[0] = 2; }},
            {"surface stream range", [](CookedLevel &l) { l.surfaces[1].gxWords = 3; }},
            {"surface plane", [](CookedLevel &l) { l.surfaces[0].planeRef = 2; }},
            {"surface texture", [](CookedLevel &l) { l.surfaces[0].texture = 1; }},
            {"clipnode plane", [](CookedLevel &l) { l.clipNodes[0].plane = 2; }},
            {"clipnode child", [](CookedLevel &l) { l.clipNodes[0].children[0] = 1; }},
            {"model head node", [](CookedLevel &l) { l.models[0].headNode = 1; }},
            {"model clip root", [](CookedLevel &l) { l.models[0].clipStanding = 1; }},
            {"model surfaces", [](CookedLevel &l) { l.models[0].surfaceCount = 3; }},
            {"plane type", [](CookedLevel &l) { l.planeTypes[0] = 4; }},
            {"plane type count", [](CookedLevel &l) { l.planeTypes.pop_back(); }},
            {"unterminated texture name",
             [](CookedLevel &l) { std::memset(l.textures[0].name, 'a', 16); }},
            {"entity model", [](CookedLevel &l) { l.entities[0].model = 1; }},
            {"vis leaf count", [](CookedLevel &l) { l.visLeafCount = 2; }},
            {"no models", [](CookedLevel &l) { l.models.clear(); }},
        };
        for (const Case &c : cases)
        {
            CookedLevel level = smallLevel();
            c.corrupt(level);
            // The writer runs the reader, so a refusal surfaces as a failed write.
            check(!writeLevel(level, bytes, error) && bytes.empty(), c.what);
        }
    }

    void testCrc()
    {
        // The standard check value for CRC-32.
        const std::uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        check(crc32(text, 9) == 0xCBF43926u, "crc32 matches the zlib polynomial");
        static_assert(fxFromDouble(-0.5) == -2048);
        static_assert(normalFromDouble(1.0) == kNormalOne);
        static_assert(vertexFromDouble(-128.25) == -513);
        static_assert(angleFromDegrees(-90) == 49152);
        static_assert(rgb555(255, 0, 255) == 0x7C1F);
        static_assert(fxMul(fxFromInt(3), fxFromDouble(0.5)) == fxFromDouble(1.5));
    }
} // namespace

int main()
{
    testRoundTrip();
    testFileFaults();
    testIndexFaults();
    testCrc();
    if (g_failures == 0)
        std::printf("test_level: ok\n");
    return g_failures == 0 ? 0 : 1;
}
