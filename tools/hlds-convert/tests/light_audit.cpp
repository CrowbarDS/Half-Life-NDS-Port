// light_audit: how well a map's faces are lit by the light their corners
// carry, against the light the map has for them. Not a suite: a tool to find
// where the port's light differs from the game's, and by how much.
//
// The converter lights each corner of a face with the face's lightmap read
// there (LightBake::sample) and the hardware blends those across it. For each
// face this reports:
//
//   fit    how far that blend is from the lightmap at each of its samples
//          inside the face (mean and worst, of 255 after the light curve);
//   dark   corners whose read takes in samples that lie inside the world's
//          solid (a door's edge in its frame, a lift's side in its shaft) --
//          samples the game never shows -- and how much darker they make it;
//   seam   corners a touching face of the same plane shares, lit otherwise
//          by it: the step seen where the two meet.
//
// Each brush entity is summed up on a line of its own; then the world's
// worst faces. Ranked by what is most seen.
//
// usage: light_audit <game folder> <map> [how many of each to list] [before]
//
// The corners are lit as the converter lights them (LightFit: fitted to each
// face's lightmap); "before", as they were before, read where each lies.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "hlds/BspFile.hpp"
#include "hlds/EntityParser.hpp"
#include "hlds/LightBake.hpp"
#include "hlds/LightFit.hpp"

using namespace hlds;

namespace
{
    double luma(const Rgb &c) { return (c.r * 77 + c.g * 150 + c.b * 29) / 256.0; }

    int contentsAt(const BspFile &bsp, const double *p)
    {
        int n = bsp.models[0].headNode[0];
        while (n >= 0)
        {
            const BspNode &node = bsp.nodes[static_cast<std::size_t>(n)];
            const BspPlane &plane = bsp.planes[static_cast<std::size_t>(node.plane)];
            const double d = plane.normal[0] * p[0] + plane.normal[1] * p[1] + plane.normal[2] * p[2] - plane.dist;
            n = node.children[d >= 0 ? 0 : 1];
        }
        return bsp.leaves[static_cast<std::size_t>(-n - 1)].contents;
    }

    /// The point of `face`'s plane (in its model's space) at texture
    /// coordinates (s, t).
    bool pointAt(const BspFile &bsp, const BspFace &face, double s, double t, double *out)
    {
        const BspTexInfo &ti = bsp.texInfos[static_cast<std::size_t>(face.texInfo)];
        const BspPlane &plane = bsp.planes[face.plane];
        const double m[3][3] = {{ti.vecs[0][0], ti.vecs[0][1], ti.vecs[0][2]},
                                {ti.vecs[1][0], ti.vecs[1][1], ti.vecs[1][2]},
                                {plane.normal[0], plane.normal[1], plane.normal[2]}};
        const double r[3] = {s - ti.vecs[0][3], t - ti.vecs[1][3], plane.dist};
        const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        if (std::fabs(det) < 1e-9)
            return false;
        for (int c = 0; c < 3; ++c)
        {
            double k[3][3];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    k[i][j] = j == c ? r[i] : m[i][j];
            out[c] = (k[0][0] * (k[1][1] * k[2][2] - k[1][2] * k[2][1]) - k[0][1] * (k[1][0] * k[2][2] - k[1][2] * k[2][0]) +
                      k[0][2] * (k[1][0] * k[2][1] - k[1][1] * k[2][0])) /
                     det;
        }
        return true;
    }

    struct FaceReport
    {
        int model = 0, face = 0;
        double centre[3] = {0, 0, 0};
        double area = 0;
        double fitMean = 0, fitWorst = 0;
        int samples = 0;
        int corners = 0, darkCorners = 0;
        double darkBy = 0;  ///< the most a corner is made darker by samples in solid
        double seam = 0;    ///< the largest step to a touching face of the same plane
        double light = 0;   ///< mean light of its corners
        double normal[3] = {0, 0, 1};
    };

    /// Where to stand to see a face: in front of it, in the open, facing it
    /// (EMU_START's "x y z yaw"); empty if nowhere near is open.
    std::string viewOf(const BspFile &bsp, const double *centre, const double *normal)
    {
        for (const double back : {160.0, 112.0, 220.0, 72.0})
        {
            double at[3];
            for (int k = 0; k < 3; ++k)
                at[k] = centre[k] + normal[k] * back;
            // (At eye height over a floor, not up in the air: a face below
            // is looked at from beside it.)
            if (std::fabs(normal[2]) > 0.7)
                at[2] = centre[2] + (normal[2] > 0 ? 40.0 : -80.0);
            bool open = true;
            for (const double dz : {-36.0, 0.0, 28.0})
            {
                const double p[3] = {at[0], at[1], at[2] + dz};
                open = open && contentsAt(bsp, p) != kContentsSolid;
            }
            if (!open)
                continue;
            const double yaw = std::atan2(centre[1] - at[1], centre[0] - at[0]) * 180.0 / M_PI;
            char text[96];
            std::snprintf(text, sizeof(text), "%.0f %.0f %.0f %.0f", at[0], at[1], at[2], yaw < 0 ? yaw + 360 : yaw);
            return text;
        }
        return "";
    }

    struct Corner
    {
        int face;
        double light;
        double normal[3];
        double dist;
    };
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: light_audit <game folder> <map> [how many to list]\n");
        return 2;
    }
    const std::string dir = argv[1], map = argv[2];
    const int list = argc > 3 ? std::atoi(argv[3]) : 12;
    const bool before = argc > 4 && std::string(argv[4]) == "before";
    // (LIGHT_AUDIT_MODEL=<n>: every face of brush model n, in detail.)
    const int detailOf = std::getenv("LIGHT_AUDIT_MODEL") != nullptr ? std::atoi(std::getenv("LIGHT_AUDIT_MODEL")) : -1;
    BspFile bsp;
    std::string error;
    if (!bsp.load(dir + "/maps/" + map + ".bsp", error))
    {
        std::fprintf(stderr, "light_audit: %s\n", error.c_str());
        return 1;
    }
    std::vector<Entity> entities;
    if (!parseEntities(bsp.entities, entities, error))
    {
        std::fprintf(stderr, "light_audit: %s\n", error.c_str());
        return 1;
    }
    const LightBake light(bsp, entities);
    // Each brush model's entity: its class, its name, where it was put (the
    // origin of one built round an origin brush), and whether it is lit (one
    // drawn blended or in a flat colour is not).
    struct Owner
    {
        std::string cls, name;
        double origin[3] = {0, 0, 0};
        bool lit = true;
    };
    std::vector<Owner> owners(bsp.models.size());
    owners[0].cls = "worldspawn";
    for (const Entity &e : entities)
    {
        const std::string model(e.value("model"));
        if (model.size() < 2 || model[0] != '*')
            continue;
        const std::size_t m = static_cast<std::size_t>(std::atoi(model.c_str() + 1));
        if (m >= owners.size())
            continue;
        owners[m].cls = std::string(e.className());
        owners[m].name = std::string(e.value("targetname"));
        e.vec3("origin", owners[m].origin);
        const int mode = std::atoi(std::string(e.value("rendermode", "0")).c_str());
        owners[m].lit = mode == 0 || mode == 4;
    }

    // The corners as the converter lights them (LightFit), or as they were
    // read before ("before").
    std::vector<const double *> litOrigins(bsp.models.size(), nullptr);
    for (std::size_t m = 0; m < bsp.models.size(); ++m)
        if (owners[m].lit && (m == 0 || !owners[m].cls.empty()))
            litOrigins[m] = owners[m].origin;
    const LightFit fit(bsp, light, before ? std::vector<const double *>() : litOrigins);

    std::vector<FaceReport> reports;
    std::map<std::tuple<long, long, long>, std::vector<Corner>> shared;
    for (std::size_t m = 0; m < bsp.models.size(); ++m)
    {
        if (!owners[m].lit || (m != 0 && owners[m].cls.empty()))
            continue;
        const BspModel &model = bsp.models[m];
        for (int f = model.firstFace; f < model.firstFace + model.faceCount; ++f)
        {
            const BspFace &face = bsp.faces[static_cast<std::size_t>(f)];
            const BspTexInfo &ti = bsp.texInfos[static_cast<std::size_t>(face.texInfo)];
            const std::string &tex = bsp.textures[static_cast<std::size_t>(ti.miptex)].name;
            if ((ti.flags & kTexSpecial) || face.lightOfs < 0 || tex.rfind("sky", 0) == 0 || tex == "aaatrigger" ||
                tex == "clip" || tex == "null" || tex == "origin")
                continue;
            const int n = face.edgeCount;
            std::vector<double> s(static_cast<std::size_t>(n)), t(static_cast<std::size_t>(n)), l(static_cast<std::size_t>(n));
            FaceReport r;
            r.model = static_cast<int>(m);
            r.face = f;
            const BspPlane &plane = bsp.planes[face.plane];
            const double sign = face.side != 0 ? -1 : 1;
            for (int k = 0; k < 3; ++k)
                r.normal[k] = plane.normal[k] * sign;
            for (int i = 0; i < n; ++i)
            {
                light.texCoords(face, i, s[static_cast<std::size_t>(i)], t[static_cast<std::size_t>(i)]);
                l[static_cast<std::size_t>(i)] = luma(light.sample(face, s[static_cast<std::size_t>(i)], t[static_cast<std::size_t>(i)],
                                                                   before ? nullptr : owners[m].origin));
                const float *p = bsp.faceVertex(face, i).point;
                for (int k = 0; k < 3; ++k)
                    r.centre[k] += (p[k] + owners[m].origin[k]) / n;
                r.light += l[static_cast<std::size_t>(i)] / n;
            }
            {
                r.light = 0;
                for (int i = 0; i < n; ++i)
                {
                    Rgb chosen;
                    if (fit.corner(f, i, chosen))
                        l[static_cast<std::size_t>(i)] = luma(chosen);
                    r.light += l[static_cast<std::size_t>(i)] / n;
                }
            }
            // (Its corners, as lit, for the seams.)
            for (int i = 0; i < n; ++i)
            {
                const float *p = bsp.faceVertex(face, i).point;
                Corner c{f, l[static_cast<std::size_t>(i)], {plane.normal[0] * sign, plane.normal[1] * sign, plane.normal[2] * sign},
                         plane.dist * sign};
                shared[{std::lround((p[0] + owners[m].origin[0]) * 4), std::lround((p[1] + owners[m].origin[1]) * 4),
                        std::lround((p[2] + owners[m].origin[2]) * 4)}]
                    .push_back(c);
            }
            // Its area, in texels (a fan).
            for (int i = 1; i + 1 < n; ++i)
                r.area += std::fabs((s[static_cast<std::size_t>(i)] - s[0]) * (t[static_cast<std::size_t>(i + 1)] - t[0]) -
                                    (s[static_cast<std::size_t>(i + 1)] - s[0]) * (t[static_cast<std::size_t>(i)] - t[0])) /
                          2;
            // Fit: at each lightmap sample inside the face, what the corners'
            // blend gives against what the lightmap has.
            const LightExtents e = light.extents(face);
            for (int j = 0; j < e.height; ++j)
                for (int i = 0; i < e.width; ++i)
                {
                    const double ps = (e.minS + i) * 16.0, pt = (e.minT + j) * 16.0;
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
                        // (A sample in the world's solid is never seen.)
                        double at[3];
                        if (pointAt(bsp, face, ps, pt, at))
                        {
                            for (int q = 0; q < 3; ++q)
                                at[q] += owners[m].origin[q] + plane.normal[q] * sign * 0.5;
                            if (contentsAt(bsp, at) == kContentsSolid)
                                break;
                        }
                        const double blend = wa * l[a] + wb * l[b] + wc * l[c];
                        const double truth = luma(light.sample(face, ps, pt));
                        const double d = std::fabs(blend - truth);
                        r.fitMean += d;
                        r.fitWorst = std::max(r.fitWorst, d);
                        ++r.samples;
                        break;
                    }
                }
            if (r.samples > 0)
                r.fitMean /= r.samples;
            if (static_cast<int>(m) == detailOf)
            {
                double lo = 1e9, hi = -1, sum = 0;
                int count = 0;
                for (int j = 0; j < e.height; ++j)
                    for (int i = 0; i < e.width; ++i)
                    {
                        const double v = luma(light.sample(face, (e.minS + i) * 16.0, (e.minT + j) * 16.0));
                        lo = std::min(lo, v);
                        hi = std::max(hi, v);
                        sum += v;
                        ++count;
                    }
                std::printf("model %d face %d normal %.2f %.2f %.2f centre %.0f %.0f %.0f  %dx%d samples: min %.0f mean %.0f max %.0f; "
                            "corners:",
                            detailOf, f, r.normal[0], r.normal[1], r.normal[2], r.centre[0], r.centre[1], r.centre[2], e.width,
                            e.height, lo, sum / std::max(1, count), hi);
                for (int i = 0; i < n; ++i)
                    std::printf(" %.0f", l[static_cast<std::size_t>(i)]);
                std::printf("  fit %.1f\n", r.fitMean);
                for (int j = 0; j < e.height && e.width <= 16; ++j)
                {
                    std::printf("   ");
                    for (int i = 0; i < e.width; ++i)
                        std::printf(" %3.0f", luma(light.sample(face, (e.minS + i) * 16.0, (e.minT + j) * 16.0)));
                    std::printf("\n");
                }
            }
            // Corners whose read takes samples in the world's solid.
            for (int i = 0; i < n; ++i)
            {
                const double fs = std::clamp(s[static_cast<std::size_t>(i)] / 16.0 - e.minS, 0.0, e.width - 1.0);
                const double ft = std::clamp(t[static_cast<std::size_t>(i)] / 16.0 - e.minT, 0.0, e.height - 1.0);
                const int s0 = static_cast<int>(fs), t0 = static_cast<int>(ft);
                const int s1 = std::min(s0 + 1, e.width - 1), t1 = std::min(t0 + 1, e.height - 1);
                const double ws = fs - s0, wt = ft - t0;
                const int ss[4] = {s0, s1, s0, s1}, tt[4] = {t0, t0, t1, t1};
                const double w[4] = {(1 - ws) * (1 - wt), ws * (1 - wt), (1 - ws) * wt, ws * wt};
                double kept = 0, keptW = 0;
                bool solid = false;
                for (int q = 0; q < 4; ++q)
                {
                    const double ls = (e.minS + ss[q]) * 16.0, lt = (e.minT + tt[q]) * 16.0;
                    double p[3];
                    if (!pointAt(bsp, face, ls, lt, p))
                        continue;
                    // (Half a unit in front of the face, where it is looked at.)
                    for (int k = 0; k < 3; ++k)
                        p[k] += owners[m].origin[k] + plane.normal[k] * sign * 0.5;
                    const double v = luma(light.sample(face, ls, lt));
                    if (contentsAt(bsp, p) == kContentsSolid)
                    {
                        solid = solid || w[q] > 0.01;
                        continue;
                    }
                    kept += w[q] * v;
                    keptW += w[q];
                }
                ++r.corners;
                if (solid && keptW > 0.01)
                {
                    ++r.darkCorners;
                    r.darkBy = std::max(r.darkBy, kept / keptW - l[static_cast<std::size_t>(i)]);
                }
            }
            reports.push_back(r);
        }
    }
    // Seams: corners in one place, of faces of one plane, lit otherwise.
    int seamPairs = 0, seamsSeen = 0;
    double seamSum = 0;
    std::vector<std::size_t> byFace(bsp.faces.size(), static_cast<std::size_t>(-1));
    for (std::size_t i = 0; i < reports.size(); ++i)
        byFace[static_cast<std::size_t>(reports[i].face)] = i;
    for (const auto &entry : shared)
    {
        const std::vector<Corner> &cs = entry.second;
        for (std::size_t a = 0; a < cs.size(); ++a)
            for (std::size_t b = a + 1; b < cs.size(); ++b)
            {
                const double dot = cs[a].normal[0] * cs[b].normal[0] + cs[a].normal[1] * cs[b].normal[1] +
                                   cs[a].normal[2] * cs[b].normal[2];
                if (dot < 0.999 || std::fabs(cs[a].dist - cs[b].dist) > 0.5 || cs[a].face == cs[b].face)
                    continue;
                const double d = std::fabs(cs[a].light - cs[b].light);
                ++seamPairs;
                seamSum += d;
                seamsSeen += d > 24;
                for (const int f : {cs[a].face, cs[b].face})
                {
                    const std::size_t i = byFace[static_cast<std::size_t>(f)];
                    if (i != static_cast<std::size_t>(-1))
                        reports[i].seam = std::max(reports[i].seam, d);
                }
            }
    }

    // The brush entities, each summed up.
    struct Summary
    {
        int model = 0, faces = 0, corners = 0, darkCorners = 0;
        double area = 0, fit = 0, worst = 0, darkBy = 0, seam = 0, light = 0, centre[3] = {0, 0, 0};
        const FaceReport *largest = nullptr;
        double score() const { return fit + darkBy * darkCorners / std::max(1, corners) + seam / 2; }
    };
    std::vector<Summary> sums(bsp.models.size());
    Summary world;
    double worldArea = 0;
    for (const FaceReport &r : reports)
    {
        Summary &s = r.model == 0 ? world : sums[static_cast<std::size_t>(r.model)];
        s.model = r.model;
        ++s.faces;
        s.area += r.area;
        s.fit += r.fitMean * r.area;
        s.light += r.light * r.area;
        s.worst = std::max(s.worst, r.fitWorst);
        s.corners += r.corners;
        s.darkCorners += r.darkCorners;
        s.darkBy = std::max(s.darkBy, r.darkBy);
        s.seam = std::max(s.seam, r.seam);
        for (int k = 0; k < 3; ++k)
            s.centre[k] += r.centre[k] * r.area;
        if (r.model == 0)
            worldArea += r.area;
        if (s.largest == nullptr || r.area > s.largest->area)
            s.largest = &r;
    }
    std::vector<Summary> brushes;
    for (Summary &s : sums)
    {
        if (s.faces == 0 || s.area <= 0)
            continue;
        s.fit /= s.area;
        s.light /= s.area;
        for (double &c : s.centre)
            c /= s.area;
        brushes.push_back(s);
    }
    if (world.area > 0)
    {
        world.fit /= world.area;
        world.light /= world.area;
    }
    std::sort(brushes.begin(), brushes.end(), [](const Summary &a, const Summary &b) { return a.score() > b.score(); });
    int allCorners = 0, allDark = 0, brushCorners = 0, brushDark = 0;
    for (const FaceReport &r : reports)
    {
        allCorners += r.corners;
        allDark += r.darkCorners;
        if (r.model != 0)
        {
            brushCorners += r.corners;
            brushDark += r.darkCorners;
        }
    }
    std::printf("%s: %zu faces lit; world fit %.1f of 255 (worst %.0f); corners in solid: world %d of %d, brushes %d of %d\n",
                map.c_str(), reports.size(), world.fit, world.worst, allDark - brushDark, allCorners - brushCorners, brushDark,
                brushCorners);
    std::printf("seams: %d corners shared on one plane, mean step %.1f, %d over 24\n", seamPairs,
                seamSum / std::max(1, seamPairs), seamsSeen);
    std::printf("brush entities, worst first (fit: mean/worst error of 255; dark: corners darkened by solid, most by; seam: "
                "largest step to a touching face):\n");
    for (int i = 0; i < static_cast<int>(brushes.size()) && i < list; ++i)
    {
        const Summary &s = brushes[static_cast<std::size_t>(i)];
        const Owner &o = owners[static_cast<std::size_t>(s.model)];
        std::printf("  *%-4d %-18s %-16s at %6.0f %6.0f %6.0f  light %5.1f  fit %5.1f/%3.0f  dark %d/%d by %4.0f  seam %4.0f  "
                    "view \"%s\"\n",
                    s.model, o.cls.c_str(), o.name.empty() ? "-" : o.name.c_str(), s.centre[0], s.centre[1], s.centre[2],
                    s.light, s.fit, s.worst, s.darkCorners, s.corners, s.darkBy, s.seam,
                    s.largest != nullptr ? viewOf(bsp, s.largest->centre, s.largest->normal).c_str() : "");
    }
    // The world's faces that differ most from their lightmap, seen large.
    std::vector<const FaceReport *> worldFaces;
    for (const FaceReport &r : reports)
        if (r.model == 0)
            worldFaces.push_back(&r);
    std::sort(worldFaces.begin(), worldFaces.end(), [](const FaceReport *a, const FaceReport *b)
              { return a->fitMean * std::sqrt(a->area) + a->seam > b->fitMean * std::sqrt(b->area) + b->seam; });
    std::printf("world faces, worst first:\n");
    for (int i = 0; i < static_cast<int>(worldFaces.size()) && i < list; ++i)
    {
        const FaceReport &r = *worldFaces[static_cast<std::size_t>(i)];
        const std::string &tex =
            bsp.textures[static_cast<std::size_t>(bsp.texInfos[static_cast<std::size_t>(bsp.faces[static_cast<std::size_t>(r.face)].texInfo)].miptex)]
                .name;
        std::printf("  face %-6d %-16s at %6.0f %6.0f %6.0f  %5.0f texels  light %5.1f  fit %5.1f/%3.0f  dark %d/%d  seam %4.0f  "
                    "view \"%s\"\n",
                    r.face, tex.c_str(), r.centre[0], r.centre[1], r.centre[2], r.area, r.light, r.fitMean, r.fitWorst,
                    r.darkCorners, r.corners, r.seam, viewOf(bsp, r.centre, r.normal).c_str());
    }
    // What splitting the faces that fit worst on the lightmap's grid would
    // cost: a face of W by H samples cut every `cell` samples is that many
    // squares, two triangles each, where it was its corners' fan.
    {
        int polygons = 0;
        for (const FaceReport &r : reports)
            polygons += bsp.faces[static_cast<std::size_t>(r.face)].edgeCount - 2;
        for (const double limit : {8.0, 16.0, 24.0})
            for (const int cell : {1, 2, 4})
            {
                int faces = 0, added = 0;
                for (const FaceReport &r : reports)
                {
                    if (r.fitMean <= limit)
                        continue;
                    const BspFace &face = bsp.faces[static_cast<std::size_t>(r.face)];
                    const LightExtents e = light.extents(face);
                    const int w = std::max(1, (e.width - 1 + cell - 1) / cell), h = std::max(1, (e.height - 1 + cell - 1) / cell);
                    ++faces;
                    added += w * h * 2 - (face.edgeCount - 2);
                }
                std::printf("split where fit > %2.0f, every %d sample%s: %d faces, %+d polygons (of %d: %+.0f%%)\n", limit,
                            cell, cell > 1 ? "s" : "", faces, added, polygons, 100.0 * added / std::max(1, polygons));
            }
    }
    (void)worldArea;
    return 0;
}
