// Models made fewer and made into strips (hlds/ModelMesh.hpp), on the game's
// own models: HLDS_VALVE_DIR says where.
//
// Every level keeps only what the model had -- each corner one of its own,
// on its own bone, at its own texel; the levels fall in triangles and rise
// in error; a model's outline is kept; and no model is sent more vertices
// for its triangles than its own strips sent.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "TestBspBuilder.hpp"
#include "hlds/ModelCook.hpp"
#include "hlds/ModelMesh.hpp"

using namespace hlds;
using namespace hldstest;

namespace
{
    void testModel(const char *dir, const char *name, bool thinnedAtAll)
    {
        MdlFile mdl;
        std::string error;
        const bool loaded = mdl.load(std::string(dir) + "/models/" + name + ".mdl", error);
        char text[160];
        std::snprintf(text, sizeof(text), "%s reads", name);
        check(loaded, text);
        if (!loaded)
            return;
        std::vector<std::array<double, 12>> standing(mdl.bones.size(), {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
        if (!mdl.sequences.empty())
        {
            std::vector<MdlPose> poses;
            if (mdl.decode(0, poses, error))
                mdlBoneMatrices(mdl, poses.data(), standing);
        }
        // Body 0, its sub-models one after another, to the targets the
        // cooker gives them.
        int source = 0;
        for (const MdlSubModel *model : mdl.selectBody(0))
            source += model->triangleCount();
        const int allowed = source > kModelTriangleTarget ? std::max(kModelTriangleTarget, source / 4) : source;
        int had = 0, sentOwn = 0, sentNew = 0;
        int count[3] = {0, 0, 0}, sent[3] = {0, 0, 0};
        double worstError[3] = {0, 0, 0};
        float sourceCoverage[3] = {0, 0, 0}, levelCoverage[3][3] = {};
        bool honest = true, falling = true;
        int parts = 0;
        for (const MdlSubModel *model : mdl.selectBody(0))
        {
            std::vector<std::array<double, 3>> rest;
            const MeshInput input = meshInput(mdl, *model, standing, rest);
            const std::vector<MeshTriangle> triangles = meshTriangles(*model);
            const int mine = static_cast<int>(triangles.size());
            had += mine;
            ++parts;

            // Strips alone: the same triangles, corner for corner.
            const MdlSubModel again = restripped(*model);
            sentOwn += sentVertices(*model);
            sentNew += sentVertices(again);
            auto keyed = [](const std::vector<MeshTriangle> &list)
            {
                std::multiset<std::array<int, 13>> out;
                for (const MeshTriangle &tri : list)
                {
                    // (Whichever corner comes first: a strip may start a
                    // triangle anywhere.)
                    std::array<std::array<int, 4>, 3> c;
                    for (int k = 0; k < 3; ++k)
                        c[k] = {tri.c[k].vertex, tri.c[k].normal, tri.c[k].s, tri.c[k].t};
                    std::sort(c.begin(), c.end());
                    std::array<int, 13> key{};
                    key[0] = tri.mesh;
                    for (int k = 0; k < 3; ++k)
                    {
                        for (int j = 0; j < 4; ++j)
                            key[1 + k * 4 + j] = c[k][j];
                    }
                    out.insert(key);
                }
                return out;
            };
            honest = honest && keyed(meshTriangles(again)) == keyed(triangles);

            const std::vector<int> targets = {mine * allowed / source, mine * allowed / source / 2,
                                              mine * allowed / source * 35 / 100};
            const std::vector<MeshLevel> levels = meshLevels(input, targets);
            // What every corner of the model is: a level may use no other.
            std::set<std::array<int, 4>> own;
            for (const MeshTriangle &tri : triangles)
            {
                for (const MdlCorner &corner : tri.c)
                    own.insert({tri.mesh, corner.vertex, corner.s, corner.t});
            }
            for (std::size_t l = 0; l < levels.size() && l < 3; ++l)
            {
                const std::vector<MeshTriangle> thin = meshTriangles(levels[l].model);
                count[l] += static_cast<int>(thin.size());
                sent[l] += sentVertices(levels[l].model);
                worstError[l] = std::max(worstError[l], levels[l].error);
                honest = honest && static_cast<int>(thin.size()) == levels[l].triangles;
                for (const MeshTriangle &tri : thin)
                {
                    for (const MdlCorner &corner : tri.c)
                        honest = honest && own.count({tri.mesh, corner.vertex, corner.s, corner.t}) == 1;
                }
                falling = falling && (l == 0 || levels[l].triangles <= levels[l - 1].triangles) &&
                          (l == 0 || levels[l].error >= levels[l - 1].error);
                if (parts == 1)
                {
                    meshCoverage(triangles, rest, sourceCoverage);
                    meshCoverage(thin, rest, levelCoverage[l]);
                }
            }
        }
        std::printf("test_lod: %-10s %4d triangles, %4d vertices sent as it is, %4d in new strips (%.2f a triangle)\n",
                    name, had, sentOwn, sentNew, had > 0 ? static_cast<double>(sentNew) / had : 0.0);
        std::printf("          levels %d / %d / %d triangles, %d / %d / %d vertices, off by %.2f / %.2f / %.2f units\n",
                    count[0], count[1], count[2], sent[0], sent[1], sent[2], worstError[0], worstError[1], worstError[2]);
        // How much of its outline each level keeps, of its largest part
        // seen from three sides: the least of the three.
        double outline[3] = {1, 1, 1};
        for (int l = 0; l < 3; ++l)
        {
            for (int k = 0; k < 3; ++k)
            {
                if (sourceCoverage[k] > 0)
                    outline[l] = std::min(outline[l], static_cast<double>(levelCoverage[l][k]) / sourceCoverage[k]);
            }
        }
        std::printf("          outline kept: %.0f%% / %.0f%% / %.0f%%\n", outline[0] * 100, outline[1] * 100, outline[2] * 100);
        std::snprintf(text, sizeof(text), "%s: its outline is kept: nine tenths at the first level, half at the last", name);
        check(outline[0] >= 0.88 && outline[1] >= 0.70 && outline[2] >= 0.50, text);
        std::snprintf(text, sizeof(text), "%s: every level is made of the model's own corners, and counts what it has", name);
        check(honest, text);
        std::snprintf(text, sizeof(text), "%s: its levels fall in triangles and rise in error", name);
        check(falling, text);
        std::snprintf(text, sizeof(text), "%s: new strips send no more vertices than its own", name);
        check(sentNew <= sentOwn, text);
        if (thinnedAtAll)
        {
            std::snprintf(text, sizeof(text), "%s: the first level is at its target, or near", name);
            check(count[0] <= allowed + allowed / 8 + 8, text);
            // (The plain stripper this replaced sent about 1.72 for a thinned
            // person.)
            std::snprintf(text, sizeof(text), "%s: ... in no more than 1.8 vertices a triangle", name);
            check(sent[0] * 100 <= count[0] * 180, text);
            std::snprintf(text, sizeof(text), "%s: nearly all of it is within 6 units of the first level, 9 of the second", name);
            check(worstError[0] < 6.0 && worstError[1] < 9.0, text);
        }
    }
    /// What a batch's command stream really sends: vertices, and the
    /// triangles its strips and loose triangles make of them. -1 for a
    /// stream that is not one.
    bool countStream(const std::uint32_t *words, int count, int &vertices, int &polygons)
    {
        vertices = polygons = 0;
        int kind = -1, run = 0;
        auto close = [&]()
        {
            if (kind == 2 && run >= 3)
                polygons += run - 2;
            else if (kind == 0)
                polygons += run / 3;
            run = 0;
        };
        for (int at = 0; at < count;)
        {
            const std::uint32_t head = words[at++];
            for (int k = 0; k < 4; ++k)
            {
                const std::uint32_t code = (head >> (8 * k)) & 0xFF;
                if (code == 0)
                    continue;
                const int params = code == hldata::kGxVtx16 ? 2 : 1;
                if (at + params > count)
                    return false;
                if (code == hldata::kGxBegin)
                {
                    close();
                    kind = static_cast<int>(words[at] & 3);
                }
                else if (code == hldata::kGxVtx16)
                {
                    ++vertices;
                    ++run;
                }
                else if (code != hldata::kGxMtxRestore && code != hldata::kGxNormal && code != hldata::kGxTexCoord)
                {
                    return false;
                }
                at += params;
            }
        }
        close();
        return true;
    }

    /// The cooked file: its levels, each batch honest about what it sends,
    /// the same bytes every time; and a DSi's, with the game's own mesh on
    /// top.
    void testCooked(const char *dir, const char *name, int levelsWanted, bool thinned)
    {
        MdlFile mdl;
        std::string error;
        if (!mdl.load(std::string(dir) + "/models/" + name + ".mdl", error))
        {
            check(false, (std::string(dir) + "/models/" + name + ".mdl loads").c_str());
            return;
        }
        const std::vector<std::string> clips = {mdl.sequences.empty() ? std::string() : mdl.sequences[0].label};
        char text[160];
        std::vector<std::uint8_t> files[2];
        for (int full = 0; full < 2; ++full)
        {
            CookedModel cooked;
            ModelCookReport report;
            std::vector<std::uint8_t> &bytes = files[full];
            const bool made = cookModel(mdl, clips, cooked, report, error, kModelSkinTexels, full != 0) &&
                              writeModel(cooked, bytes, error);
            std::snprintf(text, sizeof(text), "%s cooks%s", name, full ? " with its own mesh on top" : "");
            check(made, text);
            if (!made)
                return;
            std::vector<std::uint32_t> aligned((bytes.size() + 3) / 4);
            std::memcpy(aligned.data(), bytes.data(), bytes.size());
            hldata::ModelView view;
            if (!view.open(reinterpret_cast<const std::uint8_t *>(aligned.data()), static_cast<std::uint32_t>(bytes.size())))
            {
                check(false, "the cooked model opens");
                return;
            }
            const int levels = view.levelCount();
            const int expected = levelsWanted + (full && thinned ? 1 : 0);
            std::printf("test_lod: %s%s: %d levels:", name, full ? " (a DSi's)" : "", levels);
            bool honest = levels == static_cast<int>(report.levels.size()), falls = true, counted = true;
            int before = 1 << 30, beforeError = -1;
            for (int level = 0; level < levels; ++level)
            {
                int polygons = 0, vertices = 0;
                for (int b = 0; b < view.header->batchCount; ++b)
                {
                    const hldata::ModelBatch &batch = view.batches[b];
                    int sent = 0, drawn = 0;
                    counted = counted && countStream(view.gx + batch.gxOfs, batch.gxWords, sent, drawn) &&
                              sent == batch.vertices && drawn == batch.polygons && batch.boneCount <= hldata::kModelBatchBones;
                    if (view.batchInBody(batch, 0, 0, level))
                    {
                        polygons += batch.polygons;
                        vertices += batch.vertices;
                    }
                }
                const int off = view.header->levelError[level];
                std::printf(" %d triangles in %d vertices, off by %.2f;", polygons, vertices, off / 16.0);
                honest = honest && level < static_cast<int>(report.levels.size()) &&
                         polygons == report.levels[level].triangles && vertices == report.levels[level].vertices;
                falls = falls && polygons < before && off >= beforeError;
                before = polygons;
                beforeError = off;
            }
            std::printf(" %zu KB\n", bytes.size() / 1024);
            std::snprintf(text, sizeof(text), "%s: the file has the levels it should", name);
            check(levels == expected, text);
            std::snprintf(text, sizeof(text), "%s: each level is what the cook said of it", name);
            check(honest, text);
            std::snprintf(text, sizeof(text), "%s: each batch sends exactly what it says, on no more bones than there are matrices", name);
            check(counted, text);
            std::snprintf(text, sizeof(text), "%s: each level is fewer triangles than the one before, and further off", name);
            check(falls, text);
            if (full && thinned)
            {
                int whole = 0;
                for (const MdlSubModel *model : mdl.selectBody(0))
                    whole += model->triangleCount();
                std::snprintf(text, sizeof(text), "%s: a DSi's finest level is the game's own mesh, every triangle", name);
                check(report.levels[0].triangles == whole && view.header->levelError[0] == 0, text);
            }
            // Again: the same bytes.
            CookedModel again;
            ModelCookReport ignored;
            std::vector<std::uint8_t> second;
            std::snprintf(text, sizeof(text), "%s: cooked twice, it is the same file", name);
            check(cookModel(mdl, clips, again, ignored, error, kModelSkinTexels, full != 0) &&
                      writeModel(again, second, error) && second == bytes,
                  text);
        }
        std::snprintf(text, sizeof(text), "%s: a DSi's file differs only where the model was thinned", name);
        check((files[0] == files[1]) == !thinned, text);
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
    {
        for (const char *name : {"scientist", "barney", "zombie", "hgrunt", "islave", "gman", "bullsquid"})
            testModel(dir, name, true);
        for (const char *name : {"headcrab", "v_crowbar", "houndeye", "w_9mmar", "v_9mmar", "hgibs"})
            testModel(dir, name, false);
        testCooked(dir, "scientist", 3, true);
        testCooked(dir, "hgrunt", 3, true);
        testCooked(dir, "zombie", 3, true);
        testCooked(dir, "houndeye", 3, false);
        testCooked(dir, "hgibs", 1, false);
        testCooked(dir, "w_9mmar", 3, false);
    }
    else
    {
        std::printf("test_lod: HLDS_VALVE_DIR not set, the game's models were not read\n");
        return 77;
    }
    if (g_failures == 0)
        std::printf("test_lod: ok\n");
    return g_failures == 0 ? 0 : 1;
}
