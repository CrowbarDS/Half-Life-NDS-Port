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
#include "hlds/LevelReport.hpp"
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
        bad[4] = 70; // before a track change said what kind it is and how fast it goes
        check(kLevelMinVersion == 71 && view.open(bad.data(), size) == LevelError::BadVersion && view.header == nullptr,
              "a map of format 70 is refused, and nothing of it is read");
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

    /// The level `level` writes, opened: false if either fails.
    bool openLevel(const CookedLevel &level, std::vector<std::uint8_t> &bytes, LevelView &view)
    {
        std::string error;
        return writeLevel(level, bytes, error) &&
               view.open(bytes.data(), static_cast<std::uint32_t>(bytes.size())) == LevelError::Ok;
    }

    void testReportSections()
    {
        std::vector<std::uint8_t> bytes;
        LevelView view;
        check(openLevel(smallLevel(), bytes, view), "the small level opens for its report");
        const LevelReport r = reportLevel(view, 8);
        const auto *dir = reinterpret_cast<const SectionDir *>(bytes.data() + sizeof(LevelHeader));
        bool same = true;
        std::uint32_t padded = kLevelDataStart;
        for (int i = 0; i < kSectionCount; ++i)
        {
            same = same && r.sectionBytes[i] == dir[i].size;
            padded += (r.sectionBytes[i] + 3) & ~3u;
        }
        check(same, "each section's bytes are its directory's");
        check(padded == bytes.size(), "and with their padding they make the file");
        bool named = true;
        for (int i = 0; i < kSectionCount; ++i)
        {
            const char *name = sectionName(static_cast<Section>(i));
            named = named && std::strcmp(name, "?") != 0;
            for (int k = 0; k < i; ++k)
                named = named && std::strcmp(name, sectionName(static_cast<Section>(k))) != 0;
        }
        check(named, "every section has a name of its own");
        check(r.clipHullBytes[0] == sizeof(ClipNode) && r.clipHullBytes[1] == 0 && r.clipHullBytes[2] == 0,
              "the clip hulls are told apart by the models' roots");

        // Pages of 8 bytes: the first surface (12) is bigger than one.
        const GxPaging &p = r.paging;
        check(p.worldBytes == 20 && p.brushBytes == 0, "the world's geometry, and no brush's");
        check(p.oversize == 1 && p.pages == 3, "a surface bigger than a page has pages of its own");
        check(p.worstPages == 3 && p.worstExactBytes == 20 && p.worstLeaf == 1,
              "the one leaf's view needs every page");
        const GxPaging big = reportLevel(view, 4096).paging;
        check(big.pages == 1 && big.worstPages == 1 && big.oversize == 0, "all of it in one page of 4 KB");
        check(big.savingBytes() == 20 - 4096 - 1024, "and paging that costs more than it saves");
    }

    /// Three rooms: leaf 1 sees itself alone; 2 and 3 see each other, under
    /// node 1, whose own surface they both draw.
    void testReportViews()
    {
        CookedLevel level = smallLevel();
        Node root = level.nodes[0];
        root.children[0] = -2; // leaf 1
        root.children[1] = 1;  // node 1
        Node inner = root;
        inner.children[0] = -3; // leaf 2
        inner.children[1] = -4; // leaf 3
        inner.parent = 0;
        level.nodes = {root, inner};
        level.nodeSpans = {NodeSpan{0, 0}, NodeSpan{0, 1}};
        level.nodeMarks = {3};

        Leaf room = level.leaves[1];
        room.markCount = 1;
        Leaf one = room, two = room, three = room;
        one.firstMark = 0;
        one.visOfs = 0;
        two.firstMark = 1;
        two.visOfs = 1;
        two.parent = 1;
        three.firstMark = 2;
        three.visOfs = 1;
        three.parent = 1;
        level.leaves = {level.leaves[0], one, two, three};
        level.marks = {0, 1, 2};
        level.visLeafCount = 3;
        level.vis = {0x01, 0x06};

        Surface s = level.surfaces[0];
        level.surfaces.clear();
        level.gxStream.clear();
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            s.gxOfs = i * 3;
            s.gxWords = 3;
            level.surfaces.push_back(s);
            level.gxStream.insert(level.gxStream.end(), {i, i, i});
        }
        level.models[0].surfaceCount = 4;
        LightPatch patch{};
        patch.word = 4;
        patch.style = kPatchBase;
        level.lightPatches = {patch};

        std::vector<std::uint8_t> bytes;
        LevelView view;
        check(openLevel(level, bytes, view), "the three rooms open");
        const GxPaging p = reportLevel(view, 16).paging;
        // Laid out as first needed: s0 (leaf 1), s3 (node 1), s1, s2.
        check(p.pages == 4 && p.oversize == 0, "a page each");
        check(p.worstPages == 3 && p.worstExactBytes == 36 && p.worstLeaf == 2,
              "leaf 2 sees 2 and 3 and the node above them");
        check(p.meanPageBytes == (1 + 3 + 3) * 16 / 3, "the mean over the three leaves");
        check(p.worldPatchBytes == sizeof(LightPatch), "a light patch in the world's geometry");
    }
} // namespace

int main()
{
    testRoundTrip();
    testFileFaults();
    testIndexFaults();
    testCrc();
    testReportSections();
    testReportViews();
    if (g_failures == 0)
        std::printf("test_level: ok\n");
    return g_failures == 0 ? 0 : 1;
}
