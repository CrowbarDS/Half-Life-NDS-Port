#pragma once

// A Half-Life level built in memory, for the tests.
//
// No Valve data is in this repository and none may be added, so the tests
// cannot open a real map. This writes a real BSP30 FILE instead -- header,
// lumps and all -- for the simplest level there is: one hollow box room with
// one embedded texture, a lightmap per wall, clip hulls and a player start.
// Because it goes through the same bytes a retail map does, it tests the parser
// rather than the parser's idea of itself.
//
// The room is 256 x 256 x 128, from (-128, -128, 0) to (128, 128, 128).
//
// It can also hold BRUSH ENTITIES: boxes that are models of their own (*1,
// *2...) with an entity each, the way a door or a grate is. Each has outward
// faces, a render tree and three clip hulls, like the compiler's.
//
// WINDING: GoldSrc faces list their vertices clockwise as seen from the side
// the face is looked at from, so (v1 - v0) x (v2 - v0) points AWAY from the
// viewer, into the wall. Every face here follows that, which is what the
// cooker's winding handling is later tested against.

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "hlds/BspFile.hpp"

namespace hldstest
{
    /// A box-shaped brush entity.
    struct BrushBox
    {
        float mins[3] = {-16, -16, 0};
        float maxs[3] = {16, 16, 64};
        std::string className = "func_wall";
        /// Extra entity keys, as lump text: "\"rendermode\" \"4\"\n".
        std::string keys;
        /// Empty: the room's texture. A name starting with '{' gets pixels of
        /// index 255 in a checker, as a grate has.
        std::string textureName;
    };

    struct BoxRoom
    {
        float mins[3] = {-128, -128, 0};
        float maxs[3] = {128, 128, 128};
        std::string textureName = "testwall";
        /// When non-empty, a second texture that is named but not embedded, so
        /// the WAD lookup path has something to resolve.
        std::string externalTextureName;
        std::string extraEntities;
        std::vector<BrushBox> brushes;
    };

    namespace detail
    {
        template <class T>
        void append(std::vector<std::uint8_t> &out, const T &value)
        {
            const auto *p = reinterpret_cast<const std::uint8_t *>(&value);
            out.insert(out.end(), p, p + sizeof(T));
        }

        template <class T>
        std::vector<std::uint8_t> bytesOf(const std::vector<T> &items)
        {
            std::vector<std::uint8_t> out(items.size() * sizeof(T));
            if (!items.empty())
                std::memcpy(out.data(), items.data(), out.size());
            return out;
        }

        inline std::vector<std::uint8_t> mipTexLump(const std::string &name, bool embedded)
        {
            const std::uint32_t w = 16, h = 16;
            std::vector<std::uint8_t> out(40, 0);
            std::memcpy(out.data(), name.c_str(), name.size() < 15 ? name.size() : 15);
            std::memcpy(out.data() + 16, &w, 4);
            std::memcpy(out.data() + 20, &h, 4);
            if (!embedded)
                return out; // offsets stay 0: "look in a WAD"

            std::uint32_t ofs = 40;
            for (int mip = 0; mip < 4; ++mip)
            {
                std::memcpy(out.data() + 24 + mip * 4, &ofs, 4);
                const std::uint32_t mw = w >> mip, mh = h >> mip;
                // A checker of two colours -- or, for a '{' texture, of colour
                // and hole (index 255).
                const bool keyed = !name.empty() && name[0] == '{';
                for (std::uint32_t y = 0; y < mh; ++y)
                    for (std::uint32_t x = 0; x < mw; ++x)
                    {
                        const int cell = ((x >> 1) ^ (y >> 1)) & 1;
                        out.push_back(static_cast<std::uint8_t>(keyed && cell ? 255 : cell));
                    }
                ofs += mw * mh;
            }
            const std::uint16_t colours = 256;
            append(out, colours);
            for (int i = 0; i < 256; ++i)
            {
                // Index 0 dark, index 1 light: a checker.
                const std::uint8_t v = i == 0 ? 64 : (i == 1 ? 200 : 0);
                out.push_back(v);
                out.push_back(v);
                out.push_back(i == 255 ? 255 : v); // the key colour is blue
            }
            return out;
        }
    } // namespace detail

    /// The bytes of a complete, valid BSP30 file.
    inline std::vector<std::uint8_t> buildBoxRoom(const BoxRoom &room = BoxRoom())
    {
        using namespace hlds;
        using detail::append;
        using detail::bytesOf;

        std::vector<BspPlane> planes;
        std::vector<BspVertex> vertices;
        std::vector<BspEdge> edges;
        std::vector<std::int32_t> surfEdges;
        std::vector<BspTexInfo> texInfos;
        std::vector<BspFace> faces;
        std::vector<std::uint8_t> lighting;
        std::vector<BspNode> nodes;
        std::vector<BspClipNode> clipNodes;
        std::vector<std::uint16_t> markSurfaces;

        edges.push_back(BspEdge{{0, 0}}); // edge 0 is never used: -0 has no sign

        // Render planes 0..5: for each axis, the min wall then the max wall.
        // Every plane's normal is the positive axis, as the compilers emit.
        for (int axis = 0; axis < 3; ++axis)
        {
            for (int side = 0; side < 2; ++side)
            {
                BspPlane plane{};
                plane.normal[axis] = 1.0f;
                plane.dist = side == 0 ? room.mins[axis] : room.maxs[axis];
                plane.type = axis;
                planes.push_back(plane);
            }
        }

        // One texinfo per axis: s and t are the other two axes, one texel a unit.
        for (int axis = 0; axis < 3; ++axis)
        {
            BspTexInfo ti{};
            ti.vecs[0][(axis + 1) % 3] = 1.0f;
            ti.vecs[1][(axis + 2) % 3] = 1.0f;
            ti.miptex = 0;
            texInfos.push_back(ti);
        }

        for (int axis = 0; axis < 3; ++axis)
        {
            const int u = (axis + 1) % 3;
            const int v = (axis + 2) % 3;
            for (int side = 0; side < 2; ++side)
            {
                // The wall at the min side is looked at from +axis (inside the
                // room), so its face normal is the plane's: side 0. The max
                // wall is looked at from -axis: side 1.
                const float inward = side == 0 ? 1.0f : -1.0f;
                float corner[4][3];
                // u x v = +axis. Going (u-, v-), (u-, v+), (u+, v+), (u+, v-)
                // turns from v towards u, whose cross product is -axis: that
                // is clockwise seen from +axis. Reverse it for the other wall.
                const float cu[4] = {room.mins[u], room.mins[u], room.maxs[u], room.maxs[u]};
                const float cv[4] = {room.mins[v], room.maxs[v], room.maxs[v], room.mins[v]};
                for (int i = 0; i < 4; ++i)
                {
                    const int src = inward > 0 ? i : 3 - i;
                    corner[i][axis] = side == 0 ? room.mins[axis] : room.maxs[axis];
                    corner[i][u] = cu[src];
                    corner[i][v] = cv[src];
                }

                BspFace face{};
                face.plane = static_cast<std::uint16_t>(axis * 2 + side);
                face.side = static_cast<std::int16_t>(side);
                face.firstEdge = static_cast<std::int32_t>(surfEdges.size());
                face.edgeCount = 4;
                face.texInfo = static_cast<std::int16_t>(axis);
                face.styles[0] = 0;
                face.styles[1] = face.styles[2] = face.styles[3] = 255;
                face.lightOfs = static_cast<std::int32_t>(lighting.size());

                const auto first = static_cast<std::uint16_t>(vertices.size());
                for (int i = 0; i < 4; ++i)
                {
                    BspVertex vert{};
                    std::memcpy(vert.point, corner[i], sizeof(vert.point));
                    vertices.push_back(vert);
                }
                for (int i = 0; i < 4; ++i)
                {
                    surfEdges.push_back(static_cast<std::int32_t>(edges.size()));
                    edges.push_back(BspEdge{{static_cast<std::uint16_t>(first + i),
                                             static_cast<std::uint16_t>(first + (i + 1) % 4)}});
                }

                // The lightmap: one RGB sample every 16 texels, corners
                // included. Each wall gets its own grey so a bake that reads
                // the wrong face's samples shows up.
                const int ls = static_cast<int>((room.maxs[u] - room.mins[u]) / 16) + 1;
                const int lt = static_cast<int>((room.maxs[v] - room.mins[v]) / 16) + 1;
                const std::uint8_t grey = static_cast<std::uint8_t>(40 + 30 * faces.size());
                lighting.insert(lighting.end(), static_cast<std::size_t>(ls) * lt * 3, grey);

                markSurfaces.push_back(static_cast<std::uint16_t>(faces.size()));
                faces.push_back(face);
            }
        }

        // The render tree: six nodes in a chain. At each wall the outside is
        // the solid leaf 0 and the inside carries on; inside all six is leaf 1.
        auto leafChild = [](int leaf) { return static_cast<std::int16_t>(-(leaf + 1)); };
        for (int i = 0; i < 6; ++i)
        {
            const bool minSide = (i % 2) == 0;
            const std::int16_t inside = i == 5 ? leafChild(1) : static_cast<std::int16_t>(i + 1);
            BspNode node{};
            node.plane = i;
            // children[0] is in front of the plane (the +axis side).
            node.children[0] = minSide ? inside : leafChild(0);
            node.children[1] = minSide ? leafChild(0) : inside;
            for (int a = 0; a < 3; ++a)
            {
                node.mins[a] = static_cast<std::int16_t>(room.mins[a]);
                node.maxs[a] = static_cast<std::int16_t>(room.maxs[a]);
            }
            node.firstFace = static_cast<std::uint16_t>(i);
            node.faceCount = 1;
            nodes.push_back(node);
        }

        // Clip hulls 1..3: the same chain, with the walls moved in by the
        // hull's half-size so a POINT traced through them behaves like the box.
        // Hull 1 standing (16, 16, 36), hull 2 large (32, 32, 32), hull 3
        // crouched (16, 16, 18).
        const float hullHalf[4][3] = {{0, 0, 0}, {16, 16, 36}, {32, 32, 32}, {16, 16, 18}};
        std::int32_t hullRoot[4] = {0, 0, 0, 0};
        for (int hull = 1; hull < 4; ++hull)
        {
            hullRoot[hull] = static_cast<std::int32_t>(clipNodes.size());
            for (int i = 0; i < 6; ++i)
            {
                const int axis = i / 2;
                const bool minSide = (i % 2) == 0;
                BspPlane plane{};
                plane.normal[axis] = 1.0f;
                plane.dist = minSide ? room.mins[axis] + hullHalf[hull][axis]
                                     : room.maxs[axis] - hullHalf[hull][axis];
                plane.type = axis;
                const auto planeIndex = static_cast<std::int32_t>(planes.size());
                planes.push_back(plane);

                const std::int16_t inside =
                    i == 5 ? static_cast<std::int16_t>(kContentsEmpty)
                           : static_cast<std::int16_t>(hullRoot[hull] + i + 1);
                const auto solid = static_cast<std::int16_t>(kContentsSolid);
                BspClipNode clip{};
                clip.plane = planeIndex;
                clip.children[0] = minSide ? inside : solid;
                clip.children[1] = minSide ? solid : inside;
                clipNodes.push_back(clip);
            }
        }

        std::vector<BspLeaf> leaves(2);
        leaves[0] = BspLeaf{};
        leaves[0].contents = kContentsSolid;
        leaves[0].visOfs = -1;
        leaves[1] = BspLeaf{};
        leaves[1].contents = kContentsEmpty;
        leaves[1].visOfs = 0;
        for (int a = 0; a < 3; ++a)
        {
            leaves[1].mins[a] = static_cast<std::int16_t>(room.mins[a]);
            leaves[1].maxs[a] = static_cast<std::int16_t>(room.maxs[a]);
        }
        leaves[1].firstMarkSurface = 0;
        leaves[1].markSurfaceCount = 6;

        // One visible leaf, which sees itself: bit 0 of one byte.
        const std::vector<std::uint8_t> visibility = {0x01};

        BspModel world{};
        for (int a = 0; a < 3; ++a)
        {
            world.mins[a] = room.mins[a];
            world.maxs[a] = room.maxs[a];
        }
        for (int h = 0; h < 4; ++h)
            world.headNode[h] = hullRoot[h];
        world.visLeafs = 1;
        world.firstFace = 0;
        world.faceCount = 6;
        std::vector<BspModel> models = {world};

        // The textures: the room's, the optional external one, then one per
        // distinct brush texture.
        std::vector<std::string> textureNames = {room.textureName};
        std::vector<bool> textureEmbedded = {true};
        if (!room.externalTextureName.empty())
        {
            textureNames.push_back(room.externalTextureName);
            textureEmbedded.push_back(false);
        }

        std::string brushEntities;
        for (const BrushBox &box : room.brushes)
        {
            int texture = 0;
            if (!box.textureName.empty())
            {
                texture = -1;
                for (std::size_t t = 0; t < textureNames.size(); ++t)
                    if (textureNames[t] == box.textureName)
                        texture = static_cast<int>(t);
                if (texture < 0)
                {
                    texture = static_cast<int>(textureNames.size());
                    textureNames.push_back(box.textureName);
                    textureEmbedded.push_back(true);
                }
            }
            const auto firstTexInfo = static_cast<std::int16_t>(texInfos.size());
            for (int axis = 0; axis < 3; ++axis)
            {
                BspTexInfo ti{};
                ti.vecs[0][(axis + 1) % 3] = 1.0f;
                ti.vecs[1][(axis + 2) % 3] = 1.0f;
                ti.miptex = texture;
                texInfos.push_back(ti);
            }

            BspModel model{};
            model.firstFace = static_cast<std::int32_t>(faces.size());
            model.faceCount = 6;
            model.headNode[0] = static_cast<std::int32_t>(nodes.size());
            const auto firstPlane = static_cast<std::int32_t>(planes.size());
            const auto outsideLeaf = static_cast<int>(leaves.size());

            for (int axis = 0; axis < 3; ++axis)
            {
                model.mins[axis] = box.mins[axis];
                model.maxs[axis] = box.maxs[axis];
                const int u = (axis + 1) % 3;
                const int v = (axis + 2) % 3;
                for (int side = 0; side < 2; ++side)
                {
                    BspPlane plane{};
                    plane.normal[axis] = 1.0f;
                    plane.dist = side == 0 ? box.mins[axis] : box.maxs[axis];
                    plane.type = axis;
                    planes.push_back(plane);

                    // Faces look OUTWARDS: the max face along the plane's
                    // normal (side 0), the min face against it (side 1). The
                    // corner order below is clockwise seen from +axis; the min
                    // face is seen from -axis, so it takes it reversed.
                    const float cu[4] = {box.mins[u], box.mins[u], box.maxs[u], box.maxs[u]};
                    const float cv[4] = {box.mins[v], box.maxs[v], box.maxs[v], box.mins[v]};
                    BspFace face{};
                    face.plane = static_cast<std::uint16_t>(firstPlane + axis * 2 + side);
                    face.side = static_cast<std::int16_t>(side == 0 ? 1 : 0);
                    face.firstEdge = static_cast<std::int32_t>(surfEdges.size());
                    face.edgeCount = 4;
                    face.texInfo = static_cast<std::int16_t>(firstTexInfo + axis);
                    face.styles[0] = 0;
                    face.styles[1] = face.styles[2] = face.styles[3] = 255;
                    face.lightOfs = static_cast<std::int32_t>(lighting.size());
                    const auto first = static_cast<std::uint16_t>(vertices.size());
                    for (int i = 0; i < 4; ++i)
                    {
                        const int src = side == 1 ? i : 3 - i;
                        BspVertex vert{};
                        vert.point[axis] = side == 0 ? box.mins[axis] : box.maxs[axis];
                        vert.point[u] = cu[src];
                        vert.point[v] = cv[src];
                        vertices.push_back(vert);
                    }
                    for (int i = 0; i < 4; ++i)
                    {
                        surfEdges.push_back(static_cast<std::int32_t>(edges.size()));
                        edges.push_back(BspEdge{{static_cast<std::uint16_t>(first + i),
                                                 static_cast<std::uint16_t>(first + (i + 1) % 4)}});
                    }
                    // A lightmap big enough for any box used in the tests,
                    // one grey for the whole brush.
                    const int ls = static_cast<int>(std::ceil(box.maxs[u] / 16) -
                                                    std::floor(box.mins[u] / 16)) + 1;
                    const int lt = static_cast<int>(std::ceil(box.maxs[v] / 16) -
                                                    std::floor(box.mins[v] / 16)) + 1;
                    lighting.insert(lighting.end(), static_cast<std::size_t>(ls) * lt * 3, 120);

                    markSurfaces.push_back(static_cast<std::uint16_t>(faces.size()));
                    faces.push_back(face);
                }
            }

            // Render tree: inside all six planes is solid (leaf 0), outside
            // any of them is the brush's own empty leaf.
            const auto outside = static_cast<std::int16_t>(-(outsideLeaf + 1));
            for (int i = 0; i < 6; ++i)
            {
                const bool minSide = (i % 2) == 0;
                const std::int16_t inside =
                    i == 5 ? static_cast<std::int16_t>(-1)
                           : static_cast<std::int16_t>(model.headNode[0] + i + 1);
                BspNode node{};
                node.plane = firstPlane + i;
                node.children[0] = minSide ? inside : outside;
                node.children[1] = minSide ? outside : inside;
                for (int a = 0; a < 3; ++a)
                {
                    node.mins[a] = static_cast<std::int16_t>(box.mins[a]);
                    node.maxs[a] = static_cast<std::int16_t>(box.maxs[a]);
                }
                node.firstFace = static_cast<std::uint16_t>(model.firstFace + i);
                node.faceCount = 1;
                nodes.push_back(node);
            }
            BspLeaf leaf{};
            leaf.contents = kContentsEmpty;
            leaf.visOfs = -1;
            leaf.firstMarkSurface = static_cast<std::uint16_t>(markSurfaces.size() - 6);
            leaf.markSurfaceCount = 6;
            leaves.push_back(leaf);

            // Clip hulls: the box GROWN by the hull's half-size, so that a
            // point traced against it behaves like the player's box.
            for (int hull = 1; hull < 4; ++hull)
            {
                model.headNode[hull] = static_cast<std::int32_t>(clipNodes.size());
                for (int i = 0; i < 6; ++i)
                {
                    const int axis = i / 2;
                    const bool minSide = (i % 2) == 0;
                    BspPlane plane{};
                    plane.normal[axis] = 1.0f;
                    plane.dist = minSide ? box.mins[axis] - hullHalf[hull][axis]
                                         : box.maxs[axis] + hullHalf[hull][axis];
                    plane.type = axis;
                    const auto planeIndex = static_cast<std::int32_t>(planes.size());
                    planes.push_back(plane);
                    const std::int16_t inside =
                        i == 5 ? static_cast<std::int16_t>(kContentsSolid)
                               : static_cast<std::int16_t>(model.headNode[hull] + i + 1);
                    const auto empty = static_cast<std::int16_t>(kContentsEmpty);
                    BspClipNode clip{};
                    clip.plane = planeIndex;
                    clip.children[0] = minSide ? inside : empty;
                    clip.children[1] = minSide ? empty : inside;
                    clipNodes.push_back(clip);
                }
            }

            brushEntities += "{\n\"classname\" \"" + box.className + "\"\n\"model\" \"*" +
                             std::to_string(models.size()) + "\"\n" + box.keys + "}\n";
            models.push_back(model);
        }

        // The texture lump: a count, an offset per texture, then the records.
        std::vector<std::vector<std::uint8_t>> mips;
        for (std::size_t t = 0; t < textureNames.size(); ++t)
            mips.push_back(detail::mipTexLump(textureNames[t], textureEmbedded[t]));
        std::vector<std::uint8_t> textures;
        append(textures, static_cast<std::int32_t>(mips.size()));
        std::int32_t texOfs = static_cast<std::int32_t>(4 + 4 * mips.size());
        for (const auto &mip : mips)
        {
            append(textures, texOfs);
            texOfs += static_cast<std::int32_t>(mip.size());
        }
        for (const auto &mip : mips)
            textures.insert(textures.end(), mip.begin(), mip.end());

        std::string entityText =
            "{\n\"classname\" \"worldspawn\"\n\"wad\" \"\\sierra\\half-life\\valve\\TestWad.WAD;"
            "\\other\\second.wad\"\n}\n"
            "{\n\"classname\" \"info_player_start\"\n\"origin\" \"0 0 40\"\n\"angle\" \"90\"\n}\n";
        entityText += brushEntities;
        entityText += room.extraEntities;
        std::vector<std::uint8_t> entities(entityText.begin(), entityText.end());
        entities.push_back(0);

        // Lump order is fixed by the format.
        const std::vector<std::uint8_t> lumps[15] = {
            entities, bytesOf(planes), textures, bytesOf(vertices), visibility,
            bytesOf(nodes), bytesOf(texInfos), bytesOf(faces), lighting, bytesOf(clipNodes),
            bytesOf(leaves), bytesOf(markSurfaces), bytesOf(edges), bytesOf(surfEdges),
            bytesOf(models)};

        std::vector<std::uint8_t> file;
        append(file, static_cast<std::int32_t>(kBspVersion));
        std::int32_t offset = 4 + 15 * 8;
        for (const auto &lump : lumps)
        {
            append(file, offset);
            append(file, static_cast<std::int32_t>(lump.size()));
            offset += static_cast<std::int32_t>((lump.size() + 3) & ~std::size_t(3));
        }
        for (const auto &lump : lumps)
        {
            file.insert(file.end(), lump.begin(), lump.end());
            while (file.size() % 4 != 0)
                file.push_back(0);
        }
        return file;
    }

    // --- The check() every test file uses.
    inline int g_failures = 0;

    inline void check(bool cond, const char *what)
    {
        if (!cond)
        {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++g_failures;
        }
    }
} // namespace hldstest
