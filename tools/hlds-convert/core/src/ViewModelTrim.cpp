#include "hlds/ViewModelTrim.hpp"

#include <algorithm>
#include <cmath>

#include "HlView.hpp"
#include "hlds/ModelCook.hpp"
#include "hlds/ModelMesh.hpp"

namespace hlds
{
    ViewCone viewCone(int fovAcross, bool wide, double margin, double push)
    {
        constexpr double kDegree = M_PI / 180;
        // The height of the field of view on 4:3, which a wide screen keeps
        // and sees wider across.
        const double halfHigh = std::atan(std::tan(fovAcross * 0.5 * kDegree) * 3 / 4);
        const double halfAcross =
            wide ? std::atan(std::tan(halfHigh) * hldata::kViewWideAcross / hldata::kViewWideHigh)
                 : fovAcross * 0.5 * kDegree;
        ViewCone view;
        view.tanAcross = std::tan(halfAcross + margin * kDegree);
        view.tanHigh = std::tan(halfHigh + margin * kDegree);
        view.push = hldata::kViewHeldPushMost + push;
        return view;
    }

    ViewCone widestView(double margin, double push)
    {
        return viewCone(hldata::kViewFovMost, true, margin, push);
    }

    ViewCone narrowView(double margin, double push)
    {
        return viewCone(hldata::kViewFovNarrow, false, margin, push);
    }

    bool sequencePoses(const MdlFile &mdl, int sequence, std::vector<std::vector<std::array<double, 12>>> &out,
                       std::string &error)
    {
        out.clear();
        const MdlSequence &seq = mdl.sequences[static_cast<std::size_t>(sequence)];
        std::vector<MdlPose> poses;
        for (int blend = 0; blend < std::max(1, seq.blendCount); ++blend)
        {
            if (!mdl.decode(sequence, poses, error, blend))
                return false;
            for (int f = 0; f < seq.frameCount; ++f)
            {
                out.emplace_back();
                mdlBoneMatrices(mdl, poses.data() + static_cast<std::size_t>(f) * mdl.bones.size(), out.back());
            }
        }
        return true;
    }

    bool viewPoses(const MdlFile &mdl, std::vector<std::vector<std::array<double, 12>>> &out, std::string &error,
                   bool between)
    {
        out.clear();
        const std::size_t bones = mdl.bones.size();
        std::vector<MdlPose> poses;
        for (std::size_t s = 0; s < mdl.sequences.size(); ++s)
        {
            const MdlSequence &sequence = mdl.sequences[s];
            for (int blend = 0; blend < std::max(1, sequence.blendCount); ++blend)
            {
                if (!mdl.decode(static_cast<int>(s), poses, error, blend))
                    return false;
                const std::size_t first = out.size();
                for (int f = 0; f < sequence.frameCount; ++f)
                {
                    out.emplace_back();
                    mdlBoneMatrices(mdl, poses.data() + static_cast<std::size_t>(f) * bones, out.back());
                }
                // (Halfway, near enough: each matrix the mean of the two --
                // every point of the model halfway between its two places.)
                const std::size_t made = out.size();
                for (std::size_t f = first; between && f + 1 < made; ++f)
                {
                    std::vector<std::array<double, 12>> half(bones);
                    for (std::size_t b = 0; b < bones; ++b)
                    {
                        for (int k = 0; k < 12; ++k)
                            half[b][static_cast<std::size_t>(k)] = (out[f][b][static_cast<std::size_t>(k)] +
                                                                    out[f + 1][b][static_cast<std::size_t>(k)]) * 0.5;
                    }
                    out.push_back(std::move(half));
                }
            }
        }
        // (A model with no sequence: as it stands.)
        if (out.empty())
            out.push_back(std::vector<std::array<double, 12>>(bones, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}));
        return true;
    }

    bool triangleOnScreen(const std::array<double, 3> *corners, const ViewCone &view)
    {
        // Outside an edge: |y| or |z| beyond x times the edge's tangent, on
        // the same side for all three corners; or all three behind the eye
        // (where the hardware's near plane is: nearer than that, nothing is
        // drawn).
        int left = 0, right = 0, top = 0, bottom = 0, behind = 0;
        for (int k = 0; k < 3; ++k)
        {
            const double x = corners[k][0] + view.push;
            const double across = x * view.tanAcross, high = x * view.tanHigh;
            left += corners[k][1] > across;
            right += -corners[k][1] > across;
            top += corners[k][2] > high;
            bottom += -corners[k][2] > high;
            behind += x <= 0;
        }
        return left < 3 && right < 3 && top < 3 && bottom < 3 && behind < 3;
    }

    namespace
    {
        /// Where each vertex of `model` is in pose `bones`.
        void placeVertices(const MdlSubModel &model, const std::vector<std::array<double, 12>> &bones,
                           std::vector<std::array<double, 3>> &out)
        {
            out.resize(model.vertices.size());
            for (std::size_t v = 0; v < model.vertices.size(); ++v)
            {
                const std::array<double, 12> &m = bones[model.vertexBone[v]];
                const std::array<float, 3> &at = model.vertices[v];
                for (int a = 0; a < 3; ++a)
                    out[v][static_cast<std::size_t>(a)] =
                        m[static_cast<std::size_t>(a * 4)] * at[0] + m[static_cast<std::size_t>(a * 4 + 1)] * at[1] +
                        m[static_cast<std::size_t>(a * 4 + 2)] * at[2] + m[static_cast<std::size_t>(a * 4 + 3)];
            }
        }

        /// How many of `triangles` are on the screen in pose `bones`,
        /// marking each in `seen`.
        int countSeen(const MdlSubModel &model, const std::vector<MeshTriangle> &triangles,
                      const std::vector<std::array<double, 12>> &bones, const ViewCone &view,
                      std::vector<std::array<double, 3>> &placed, std::vector<char> &seen)
        {
            placeVertices(model, bones, placed);
            int count = 0;
            for (std::size_t t = 0; t < triangles.size(); ++t)
            {
                const std::array<double, 3> corners[3] = {placed[static_cast<std::size_t>(triangles[t].c[0].vertex)],
                                                          placed[static_cast<std::size_t>(triangles[t].c[1].vertex)],
                                                          placed[static_cast<std::size_t>(triangles[t].c[2].vertex)]};
                if (triangleOnScreen(corners, view))
                {
                    seen[t] = 1;
                    ++count;
                }
            }
            return count;
        }

        int bodyZeroTriangles(const MdlFile &mdl)
        {
            int count = 0;
            for (const MdlSubModel *model : mdl.selectBody(0))
                count += model->triangleCount();
            return count;
        }
    } // namespace

    std::vector<char> seenTriangles(const MdlSubModel &model,
                                    const std::vector<std::vector<std::array<double, 12>>> &poses,
                                    const ViewCone &view)
    {
        const std::vector<MeshTriangle> triangles = meshTriangles(model);
        std::vector<char> seen(triangles.size(), 0);
        std::vector<std::array<double, 3>> placed;
        for (const auto &bones : poses)
            countSeen(model, triangles, bones, view, placed, seen);
        return seen;
    }

    int trimViewModel(MdlFile &mdl, const std::vector<std::vector<std::array<double, 12>>> &poses,
                      const ViewCone &view, int &kept)
    {
        const int had = bodyZeroTriangles(mdl);
        for (MdlBodyPart &part : mdl.bodyParts)
        {
            for (MdlSubModel &model : part.models)
            {
                const std::vector<MeshTriangle> triangles = meshTriangles(model);
                const std::vector<char> seen = seenTriangles(model, poses, view);
                // Each triangle kept as a strip of its own, wound as it was:
                // the cook makes its strips afresh (ModelMesh).
                for (MdlMesh &mesh : model.meshes)
                {
                    mesh.strips.clear();
                    mesh.triangles = 0;
                }
                for (std::size_t t = 0; t < triangles.size(); ++t)
                {
                    if (!seen[t])
                        continue;
                    MdlMesh &mesh = model.meshes[static_cast<std::size_t>(triangles[t].mesh)];
                    MdlStrip strip;
                    strip.corners.assign(triangles[t].c, triangles[t].c + 3);
                    mesh.strips.push_back(std::move(strip));
                    ++mesh.triangles;
                }
            }
        }
        kept = bodyZeroTriangles(mdl);
        return had;
    }

    int screenMost(const MdlSubModel &model, const std::vector<std::vector<std::array<double, 12>>> &poses,
                   const ViewCone &view)
    {
        const std::vector<MeshTriangle> triangles = meshTriangles(model);
        std::vector<char> seen(triangles.size(), 0);
        std::vector<std::array<double, 3>> placed;
        int most = 0;
        for (const auto &bones : poses)
            most = std::max(most, countSeen(model, triangles, bones, view, placed, seen));
        return most;
    }
} // namespace hlds
