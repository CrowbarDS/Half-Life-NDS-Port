#include "hlds/LightFit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>

namespace hlds
{
    namespace
    {
        constexpr double kSpacing = 16.0; ///< texels between lightmap samples
        /// How much a corner is held to its own read: about what a corner's
        /// share of a face's samples weighs.
        constexpr double kHold = 0.3;
        /// A sample this far (of 255) from the fit counts half.
        constexpr double kOutlier = 24.0;
        constexpr int kPasses = 4;
        constexpr int kSweeps = 30;

        /// One sample of a face: the corners it lies between, its weight
        /// on each, and its light.
        struct Row
        {
            int id[3];
            double w[3];
            double v[3];
        };
    } // namespace

    LightFit::LightFit(const BspFile &bsp, const LightBake &light, const std::vector<const double *> &origins)
    {
        // The unknowns: one for each corner of a fitted face, shared by the
        // faces of one model on one plane (and side) that have it.
        std::map<std::tuple<int, int, int, long, long, long>, int> ids;
        std::vector<Rgb> reads;
        std::vector<Row> rows;
        std::vector<std::pair<int, std::vector<int>>> faces; // face, its corners' unknowns
        for (std::size_t m = 0; m < bsp.models.size() && m < origins.size(); ++m)
        {
            const double *origin = origins[m];
            if (origin == nullptr)
                continue;
            const BspModel &model = bsp.models[m];
            for (int f = model.firstFace; f < model.firstFace + model.faceCount; ++f)
            {
                const BspFace &face = bsp.faces[static_cast<std::size_t>(f)];
                const BspTexInfo &ti = bsp.texInfos[static_cast<std::size_t>(face.texInfo)];
                if ((ti.flags & kTexSpecial) || face.lightOfs < 0 || bsp.lighting.empty() || face.edgeCount < 3 ||
                    light.changingLayers(face) != 0)
                    continue;
                const int n = face.edgeCount;
                std::vector<double> s(static_cast<std::size_t>(n)), t(static_cast<std::size_t>(n));
                std::vector<int> id(static_cast<std::size_t>(n));
                for (int i = 0; i < n; ++i)
                {
                    light.texCoords(face, i, s[static_cast<std::size_t>(i)], t[static_cast<std::size_t>(i)]);
                    const float *p = bsp.faceVertex(face, i).point;
                    const auto key = std::make_tuple(static_cast<int>(m), static_cast<int>(face.plane), face.side != 0 ? 1 : 0,
                                                     std::lround(p[0] * 4), std::lround(p[1] * 4), std::lround(p[2] * 4));
                    auto found = ids.find(key);
                    if (found == ids.end())
                    {
                        found = ids.insert({key, static_cast<int>(reads.size())}).first;
                        reads.push_back(light.sample(face, s[static_cast<std::size_t>(i)], t[static_cast<std::size_t>(i)], origin));
                    }
                    id[static_cast<std::size_t>(i)] = found->second;
                }
                // Each sample that is seen, by the triangle of the face's fan
                // it lies in (the corners the hardware blends there).
                const LightExtents e = light.extents(face);
                for (int j = 0; j < e.height; ++j)
                    for (int i = 0; i < e.width; ++i)
                    {
                        const double ps = (e.minS + i) * kSpacing, pt = (e.minT + j) * kSpacing;
                        for (int k = 1; k + 1 < n; ++k)
                        {
                            const std::size_t a = 0, b = static_cast<std::size_t>(k), c = static_cast<std::size_t>(k + 1);
                            const double den = (t[b] - t[c]) * (s[a] - s[c]) + (s[c] - s[b]) * (t[a] - t[c]);
                            if (std::fabs(den) < 1e-9)
                                continue;
                            const double wa = ((t[b] - t[c]) * (ps - s[c]) + (s[c] - s[b]) * (pt - t[c])) / den;
                            const double wb = ((t[c] - t[a]) * (ps - s[c]) + (s[a] - s[c]) * (pt - t[c])) / den;
                            const double wc = 1 - wa - wb;
                            if (wa < -1e-6 || wb < -1e-6 || wc < -1e-6)
                                continue;
                            if (!light.sampleInSolid(face, ps, pt, origin))
                            {
                                const Rgb v = light.sample(face, ps, pt);
                                rows.push_back({{id[a], id[b], id[c]}, {wa, wb, wc}, {double(v.r), double(v.g), double(v.b)}});
                            }
                            break;
                        }
                    }
                faces.push_back({f, std::move(id)});
            }
        }
        const std::size_t count = reads.size();
        if (count == 0)
            return;
        std::vector<std::vector<std::size_t>> rowsOf(count);
        for (std::size_t r = 0; r < rows.size(); ++r)
            for (int u = 0; u < 3; ++u)
                rowsOf[static_cast<std::size_t>(rows[r].id[u])].push_back(r);

        // Each colour on its own: Gauss-Seidel on the normal equations, then
        // the samples weighed again by how far the fit is from them.
        std::vector<double> x[3];
        for (int c = 0; c < 3; ++c)
        {
            x[c].resize(count);
            for (std::size_t q = 0; q < count; ++q)
                x[c][q] = c == 0 ? reads[q].r : (c == 1 ? reads[q].g : reads[q].b);
        }
        std::vector<double> weight(rows.size(), 1.0);
        for (int pass = 0; pass < kPasses; ++pass)
        {
            for (int c = 0; c < 3; ++c)
            {
                for (int sweep = 0; sweep < kSweeps; ++sweep)
                    for (std::size_t q = 0; q < count; ++q)
                    {
                        const double read = c == 0 ? reads[q].r : (c == 1 ? reads[q].g : reads[q].b);
                        double num = kHold * read, den = kHold;
                        for (const std::size_t r : rowsOf[q])
                        {
                            const Row &row = rows[r];
                            double wq = 0, rest = row.v[c];
                            for (int u = 0; u < 3; ++u)
                            {
                                if (row.id[u] == static_cast<int>(q))
                                    wq += row.w[u];
                                else
                                    rest -= row.w[u] * x[c][static_cast<std::size_t>(row.id[u])];
                            }
                            num += weight[r] * wq * rest;
                            den += weight[r] * wq * wq;
                        }
                        x[c][q] = std::clamp(num / den, 0.0, 255.0);
                    }
            }
            for (std::size_t r = 0; r < rows.size(); ++r)
            {
                const Row &row = rows[r];
                double off = 0;
                for (int c = 0; c < 3; ++c)
                {
                    double v = 0;
                    for (int u = 0; u < 3; ++u)
                        v += row.w[u] * x[c][static_cast<std::size_t>(row.id[u])];
                    off = std::max(off, std::fabs(v - row.v[c]));
                }
                weight[r] = 1.0 / (1.0 + off / kOutlier);
            }
        }
        for (const auto &[f, id] : faces)
            for (std::size_t i = 0; i < id.size(); ++i)
            {
                const std::size_t q = static_cast<std::size_t>(id[i]);
                chosen_[{f, static_cast<int>(i)}] =
                    Rgb{static_cast<std::uint8_t>(std::lround(x[0][q])), static_cast<std::uint8_t>(std::lround(x[1][q])),
                        static_cast<std::uint8_t>(std::lround(x[2][q]))};
            }
    }

    bool LightFit::corner(int face, int corner, Rgb &out) const
    {
        const auto found = chosen_.find({face, corner});
        if (found == chosen_.end())
            return false;
        out = found->second;
        return true;
    }
} // namespace hlds
