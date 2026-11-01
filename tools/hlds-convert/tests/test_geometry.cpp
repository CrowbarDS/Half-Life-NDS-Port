// Faces to geometry-engine commands: the packer, the decoder that checks it,
// and the way a polygon is cut into quads and triangles.
//
// The cut is where a mistake costs the most and shows the least. A piece with
// the wrong winding is simply not drawn; a sliver with no area is a wasted
// polygon out of 2048; a polygon that is not fully covered is a hole in a
// wall. So every polygon here is checked the same way: decode what was packed,
// and require that the pieces face the right way and add up to the whole.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "TestBspBuilder.hpp"
#include "hlds/FaceBuilder.hpp"
#include "hlds/GxStream.hpp"
#include "hlds/LightBake.hpp"
#include "hlds/TextureSize.hpp"

using namespace hlds;
using namespace hldstest;

namespace
{
    /// Which vertex command each vertex was sent with, in order.
    std::vector<int> vertexCommands(const std::vector<std::uint32_t> &words)
    {
        std::vector<int> codes;
        std::size_t pos = 0;
        while (pos < words.size())
        {
            const std::uint32_t header = words[pos++];
            for (int k = 0; k < 4; ++k)
            {
                const int code = (header >> (8 * k)) & 0xFF;
                if (code == 0)
                    break;
                pos += code == 0x23 ? 2 : 1;
                if (code >= 0x23 && code <= 0x28)
                    codes.push_back(code);
            }
        }
        return codes;
    }

    /// Every one-word vertex form, each chosen only where it is exact, and all
    /// of them decoding back to the vertex that went in.
    void testVertexForms()
    {
        const std::int16_t points[][3] = {
            {1000, -2000, 3001},   // first: absolute, not multiples of 64 -> VTX_16
            {1500, -2600, 3001},   // z repeats -> VTX_XY
            {1500, 9000, -7000},   // x repeats -> VTX_YZ
            {-31000, 9000, 30000}, // y repeats -> VTX_XZ
            {-31512, 9511, 29999}, // all three within -512..511 -> VTX_DIFF
            {-31513, 10023, 29487}, // dy = 512 is one too many: not DIFF -> VTX_16
            {6400, -12800, 64},    // far, but multiples of 64 -> VTX_10
            {6401, -12863, 0},     // dx 1, dy -63, dz -64 -> VTX_DIFF again
        };
        const int expected[] = {0x23, 0x25, 0x27, 0x26, 0x28, 0x23, 0x24, 0x28};

        GxPacker packer;
        packer.begin(GxPrimitive::Quads);
        std::vector<GxVertex> in;
        for (const auto &p : points)
        {
            GxVertex v;
            v.x = p[0];
            v.y = p[1];
            v.z = p[2];
            v.colour = 0x7FFF;
            in.push_back(v);
            packer.vertex(v);
        }
        const std::vector<std::uint32_t> words = packer.finish();
        const std::vector<int> codes = vertexCommands(words);
        check(codes.size() == 8, "eight vertex commands");
        for (std::size_t i = 0; i < codes.size() && i < 8; ++i)
            check(codes[i] == expected[i], "each vertex takes the shortest exact form");

        std::vector<GxRun> runs;
        std::string error;
        check(decodeGxStream(words.data(), words.size(), runs, error), "the mixed forms decode");
        check(runs.size() == 1 && runs[0].vertices == in, "to exactly the vertices that went in");

        // A first vertex on multiples of 64 may be VTX_10; nothing relative
        // may ever come first.
        GxVertex round;
        round.x = -32768;
        round.y = 32704;
        round.z = 0;
        round.colour = 1;
        packer.begin(GxPrimitive::Triangles);
        packer.vertex(round);
        packer.vertex(round);
        packer.vertex(round);
        const auto roundWords = packer.finish();
        check(vertexCommands(roundWords)[0] == 0x24, "an absolute VTX_10 may lead a stream");
        check(decodeGxStream(roundWords.data(), roundWords.size(), runs, error) &&
                  runs[0].vertices[0] == round,
              "and holds the extremes of the range");

        std::vector<std::uint32_t> bad = {0x00252040, 0, 1, 0}; // begin, colour, VTX_XY first
        check(!decodeGxStream(bad.data(), bad.size(), runs, error),
              "a relative vertex with nothing before it is refused");
    }

    void testPacker()
    {
        GxPacker packer;
        packer.begin(GxPrimitive::Quads);
        GxVertex v[4];
        for (int i = 0; i < 4; ++i)
        {
            v[i].x = static_cast<std::int16_t>(-100 * i);
            v[i].y = static_cast<std::int16_t>(i);
            v[i].z = static_cast<std::int16_t>(-32768 + i);
            v[i].s = static_cast<std::int16_t>(-16 * i);
            v[i].t = static_cast<std::int16_t>(32767 - i);
            v[i].colour = 0x1234; // the same for all four
            packer.vertex(v[i]);
        }
        const std::vector<std::uint32_t> words = packer.finish();
        // BEGIN + one COLOR + 4 x (TEXCOORD, vertex) = 10 commands in 3 header
        // words. Parameters: 1 + 1 + 4, and ONE word for every vertex -- the
        // first is (0, 0, -32768), all multiples of 64, so it goes as VTX_10;
        // the other three are small steps from their predecessor (VTX_DIFF).
        // With VTX_16 throughout this was 17.
        check(words.size() == 3 + 6 + 4, "a single-colour quad packs into 13 words");
        check((words[0] & 0xFF) == 0x40, "the stream starts with BEGIN_VTXS");
        check(packer.finish().empty(), "the packer is empty after finish");

        std::vector<GxRun> runs;
        std::string error;
        check(decodeGxStream(words.data(), words.size(), runs, error), "the stream decodes");
        check(runs.size() == 1 && runs[0].type == GxPrimitive::Quads &&
                  runs[0].vertices.size() == 4,
              "one quad");
        for (int i = 0; i < 4 && runs.size() == 1; ++i)
            check(runs[0].vertices[i] == v[i], "negative and extreme values survive");
        check(runs[0].polygons() == 1, "one polygon");

        // Refusals: what the console would choke on must not pass `verify`.
        std::vector<std::uint32_t> bad = words;
        bad.pop_back();
        check(!decodeGxStream(bad.data(), bad.size(), runs, error), "a truncated stream");
        bad = words;
        bad[0] = (bad[0] & ~0xFFu) | 0x41; // END_VTXS: never emitted
        check(!decodeGxStream(bad.data(), bad.size(), runs, error), "an unknown command");
        bad = {0x00000023, 0, 0};
        check(!decodeGxStream(bad.data(), bad.size(), runs, error), "a vertex before begin");
        bad = {0x00002340, 1, 0, 0}; // begin quads, then a vertex with no colour set
        check(!decodeGxStream(bad.data(), bad.size(), runs, error), "a vertex before a colour");

        testVertexForms();

        GxRun run;
        run.type = GxPrimitive::QuadStrip;
        run.vertices.resize(8);
        check(run.polygons() == 3, "an 8-vertex quad strip is 3 quads");
        run.vertices.resize(7);
        check(run.polygons() == -1, "a 7-vertex quad strip is malformed");
    }

    // ---- polygon cutting --------------------------------------------------

    struct P2
    {
        double x, y;
    };

    /// A polygon in the plane z = 5, in the order given.
    std::vector<FacePoint> flat(const std::vector<P2> &points)
    {
        std::vector<FacePoint> out;
        for (const P2 &p : points)
        {
            FacePoint f{};
            f.pos[0] = p.x;
            f.pos[1] = p.y;
            f.pos[2] = 5;
            f.s = p.x;
            f.t = p.y;
            f.colour = 0x7FFF;
            out.push_back(f);
        }
        return out;
    }

    /// Twice the signed area of (a, b, c) seen from +z: positive when
    /// counter-clockwise.
    double signed2(const GxVertex &a, const GxVertex &b, const GxVertex &c)
    {
        return static_cast<double>(b.x - a.x) * (c.y - a.y) -
               static_cast<double>(c.x - a.x) * (b.y - a.y);
    }

    /// Decode a surface and check its pieces: the counts it claims, every piece
    /// counter-clockwise from +z, no piece empty, and the total area equal to
    /// `expectedArea` (in units^2).
    void checkPieces(const BuiltSurface &built, double expectedArea, const char *name)
    {
        std::vector<GxRun> runs;
        std::string error;
        if (!decodeGxStream(built.words.data(), built.words.size(), runs, error))
        {
            check(false, name);
            return;
        }
        int polygons = 0;
        int vertices = 0;
        double area2 = 0;
        bool windingOk = true;
        bool noSliver = true;
        auto piece = [&](std::initializer_list<const GxVertex *> ring)
        {
            // A fan from the first vertex; every triangle of a convex
            // counter-clockwise piece must itself be counter-clockwise or flat.
            const std::vector<const GxVertex *> r(ring);
            double total = 0;
            for (std::size_t i = 1; i + 1 < r.size(); ++i)
            {
                const double a = signed2(*r[0], *r[i], *r[i + 1]);
                if (a < 0)
                    windingOk = false;
                total += a;
            }
            if (total <= 0)
                noSliver = false;
            area2 += total;
        };
        for (const GxRun &run : runs)
        {
            polygons += run.polygons();
            vertices += static_cast<int>(run.vertices.size());
            const auto &v = run.vertices;
            if (run.type == GxPrimitive::Triangles)
                for (std::size_t i = 0; i + 2 < v.size(); i += 3)
                    piece({&v[i], &v[i + 1], &v[i + 2]});
            else if (run.type == GxPrimitive::Quads)
                for (std::size_t i = 0; i + 3 < v.size(); i += 4)
                    piece({&v[i], &v[i + 1], &v[i + 2], &v[i + 3]});
            else if (run.type == GxPrimitive::QuadStrip)
                for (std::size_t i = 0; i + 3 < v.size(); i += 2)
                    piece({&v[i], &v[i + 1], &v[i + 3], &v[i + 2]}); // the hardware's order
            else
                check(false, "the builder does not emit triangle strips");
        }
        check(polygons == built.polygons && vertices == built.vertices, name);
        check(windingOk, name);
        check(noSliver, name);
        // Vertices are quarter units, so decoded areas are 16x.
        check(std::fabs(area2 / 2 / 16 - expectedArea) < 1e-6, name);
    }

    void testCutting()
    {
        const TextureSize size{64, 64};

        // All polygons are given CLOCKWISE seen from +z, as the BSP would for a
        // face looked at from +z; the builder must hand back counter-clockwise.
        BuiltSurface b = buildSurface(flat({{0, 0}, {0, 10}, {10, 0}}), size, 64, 64);
        check(b.polygons == 1 && b.vertices == 3, "triangle: 1 polygon, 3 vertices");
        checkPieces(b, 50, "triangle");

        b = buildSurface(flat({{0, 0}, {0, 10}, {10, 10}, {10, 0}}), size, 64, 64);
        check(b.polygons == 1 && b.vertices == 4, "quad: 1 polygon, 4 vertices");
        checkPieces(b, 100, "quad");

        b = buildSurface(flat({{0, 0}, {0, 10}, {5, 14}, {10, 10}, {10, 0}}), size, 64, 64);
        check(b.polygons == 2 && b.vertices == 7, "pentagon: quad + triangle");
        checkPieces(b, 120, "pentagon");

        b = buildSurface(flat({{0, 0}, {-4, 5}, {0, 10}, {10, 10}, {14, 5}, {10, 0}}), size, 64, 64);
        check(b.polygons == 2 && b.vertices == 6, "hexagon: one strip of 2 quads, 6 vertices");
        checkPieces(b, 140, "hexagon");

        b = buildSurface(flat({{0, 0}, {-4, 5}, {0, 10}, {5, 13}, {10, 10}, {14, 5}, {10, 0}}),
                         size, 64, 64);
        check(b.polygons == 3 && b.vertices == 9, "heptagon: strip of 2 quads + triangle");
        checkPieces(b, 155, "heptagon");

        // A square whose bottom edge the compiler split twice: (2,0) and (6,0)
        // lie on the edge from (10,0) to (0,0). Starting the zig-zag at the
        // wrong vertex makes a piece out of three collinear points.
        const auto split = flat({{0, 0}, {0, 10}, {10, 10}, {10, 0}, {6, 0}, {2, 0}});
        b = buildSurface(split, size, 64, 64);
        check(b.polygons == 2 && b.vertices == 6, "split square: 2 quads");
        checkPieces(b, 100, "split square has no empty piece");

        // One extra point on an edge: a quad plus a triangle, neither empty.
        b = buildSurface(flat({{0, 0}, {0, 10}, {10, 10}, {10, 0}, {5, 0}}), size, 64, 64);
        checkPieces(b, 100, "square with one edge point");
    }

    void testTexCoordsAndBounds()
    {
        // A 64-texel source texture cooked at 32: coordinates halve. The face
        // sits 1000 texels from the texture origin, which must be folded back
        // to within one repeat rather than overflow 12.4.
        const TextureSize size{32, 32};
        auto poly = flat({{0, 0}, {0, 64}, {64, 64}, {64, 0}});
        for (FacePoint &p : poly)
        {
            p.s += 6400; // 100 repeats of 64
            p.t -= 640;  // -10 repeats
        }
        const BuiltSurface b = buildSurface(poly, size, 64, 64);
        std::vector<GxRun> runs;
        std::string error;
        check(decodeGxStream(b.words.data(), b.words.size(), runs, error), "decodes");
        int minS = 1 << 30, maxS = -(1 << 30), minT = 1 << 30, maxT = -(1 << 30);
        for (const GxVertex &v : runs[0].vertices)
        {
            minS = std::min<int>(minS, v.s);
            maxS = std::max<int>(maxS, v.s);
            minT = std::min<int>(minT, v.t);
            maxT = std::max<int>(maxT, v.t);
        }
        check(minS == 0 && maxS == 32 * 16, "s spans one cooked repeat from zero, in 12.4");
        check(minT == 0 && maxT == 32 * 16, "t likewise, from a negative origin");
        check(!b.texCoordsClamped && !b.outOfRange, "nothing clamped");

        check(b.center[0] == 32 && b.center[1] == 32 && b.center[2] == 5, "centre");
        check(b.radius == 46, "radius is the half diagonal, rounded up");
        for (const GxVertex &v : runs[0].vertices)
            check(v.z == 20, "vertices are quarter units");

        const BuiltSurface far =
            buildSurface(flat({{0, 0}, {0, 10}, {9000, 10}, {9000, 0}}), size, 64, 64);
        check(far.outOfRange, "a vertex beyond +-8191 units is reported");

        check(defaultTextureSize(128, 128).width == 64, "128 halves to 64");
        check(defaultTextureSize(96, 96).width == 64, "96 halves to 48, nearest power is 64");
        check(defaultTextureSize(80, 80).width == 32, "80 halves to 40, nearest power is 32");
        check(defaultTextureSize(16, 16).width == 16, "small textures stop at 16");
        check(defaultTextureSize(512, 64).width == 128 && defaultTextureSize(512, 64).height == 32,
              "large textures stop at 128, axes independent");
    }

    void testLight()
    {
        BspFile bsp;
        std::string error;
        check(bsp.parse(buildBoxRoom(), error), "box room parses");
        std::vector<Entity> ents;
        parseEntities(bsp.entities, ents, error);

        // Gamma 1 so that expected values are the lightmap's own.
        const LightBake flatLight(bsp, ents, 1.0, 1.0);
        const BspFace &wall = bsp.faces[0]; // the x = -128 wall; s is y, t is z
        const LightExtents e = flatLight.extents(wall);
        check(e.minS == -8 && e.width == 17 && e.minT == 0 && e.height == 9,
              "lightmap extents: a sample every 16 units, corners included");
        Rgb c = flatLight.sample(wall, 0, 64);
        check(c.r == 40 && c.g == 40 && c.b == 40, "wall 0 is the builder's grey 40");
        c = flatLight.sample(bsp.faces[3], 0, 0);
        check(c.r == 130, "wall 3 is the builder's grey 130: each face reads its own samples");

        // Paint a gradient into wall 0's lightmap and read it back between and
        // on the samples.
        for (int t = 0; t < e.height; ++t)
            for (int s = 0; s < e.width; ++s)
                for (int ch = 0; ch < 3; ++ch)
                    bsp.lighting[wall.lightOfs + (t * e.width + s) * 3 + ch] =
                        static_cast<std::uint8_t>(s * 10 + t);
        check(flatLight.sample(wall, -128, 0).r == 0, "the corner sample");
        check(flatLight.sample(wall, 128, 128).r == 168, "the opposite corner");
        check(flatLight.sample(wall, -120, 0).r == 5, "half way between two samples");
        check(flatLight.sample(wall, -500, -500).r == 0, "outside the lightmap clamps");

        const LightBake scaled(bsp, ents);
        // (168 of 255 to the power 0.8, a third stronger: 241.)
        check(scaled.sample(wall, 128, 128).r == 241, "the game's curve: lifted, and a third stronger");
        check(scaled.sample(wall, -120, 0).r == 14 && scaled.sample(wall, -128, 0).r == 0, "from black, which stays black");
        const LightBake strong(bsp, ents, 2.0, 1.0);
        check(strong.sample(wall, 128, 128).r == 255, "and no more than full");

        // A second layer on a switchable style: counted when the light starts
        // on, not when it starts off.
        BspFace &styled = bsp.faces[0];
        const std::size_t layer = static_cast<std::size_t>(e.width) * e.height * 3;
        styled.lightOfs = static_cast<std::int32_t>(bsp.lighting.size());
        bsp.lighting.insert(bsp.lighting.end(), layer, 20);  // style 0
        bsp.lighting.insert(bsp.lighting.end(), layer, 100); // style 40
        styled.styles[1] = 40;
        parseEntities("{\"classname\" \"light\" \"style\" \"40\" \"spawnflags\" \"1\"}", ents, error);
        check(LightBake(bsp, ents, 1.0, 1.0).sample(styled, 0, 0).r == 20, "a light that starts off");
        parseEntities("{\"classname\" \"light\" \"style\" \"40\"}", ents, error);
        check(LightBake(bsp, ents, 1.0, 1.0).sample(styled, 0, 0).r == 120, "a light that starts on");
        parseEntities("{\"classname\" \"light\" \"style\" \"40\" \"spawnflags\" \"1\"}"
                      "{\"classname\" \"light_spot\" \"style\" \"40\"}",
                      ents, error);
        check(LightBake(bsp, ents, 1.0, 1.0).sample(styled, 0, 0).r == 120,
              "a style shared with a lit light is on");

        styled.lightOfs = -1;
        check(flatLight.sample(styled, 0, 0).r == 0, "no lightmap on a lit map is black");
        bsp.texInfos[styled.texInfo].flags = kTexSpecial;
        check(flatLight.sample(styled, 0, 0).r == 255, "a special face is full bright");
    }
} // namespace

int main()
{
    testPacker();
    testCutting();
    testTexCoordsAndBounds();
    testLight();
    if (g_failures == 0)
        std::printf("test_geometry: ok\n");
    return g_failures == 0 ? 0 : 1;
}
