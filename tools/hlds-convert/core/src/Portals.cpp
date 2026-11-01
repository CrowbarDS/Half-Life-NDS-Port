#include "hlds/Portals.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "HlPortals.hpp"

namespace hlds
{
    namespace
    {
        using Point = std::array<double, 3>;
        using Winding = std::vector<Point>;

        constexpr double kOn = 0.01;      ///< as near a plane as is on it
        constexpr double kHuge = 65536.0; ///< bigger than any map

        double dot(const Point &a, const double *n) { return a[0] * n[0] + a[1] * n[1] + a[2] * n[2]; }

        /// A square on the plane, far bigger than the map.
        Winding planeWinding(const double *n, double dist)
        {
            // An axis along the plane: the one the normal is least along.
            int least = 0;
            for (int a = 1; a < 3; ++a)
            {
                if (std::fabs(n[a]) < std::fabs(n[least]))
                    least = a;
            }
            double axis[3] = {0, 0, 0};
            axis[least] = 1;
            double u[3] = {n[1] * axis[2] - n[2] * axis[1], n[2] * axis[0] - n[0] * axis[2], n[0] * axis[1] - n[1] * axis[0]};
            const double ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
            for (double &c : u)
                c /= ul;
            const double v[3] = {n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]};
            const Point centre = {n[0] * dist, n[1] * dist, n[2] * dist};
            Winding w;
            const int sign[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (const auto &s : sign)
            {
                Point p;
                for (int a = 0; a < 3; ++a)
                    p[a] = centre[a] + u[a] * kHuge * s[0] + v[a] * kHuge * s[1];
                w.push_back(p);
            }
            return w;
        }

        /// The part of `w` on the front of the plane (n.p >= dist), and the
        /// part behind. A winding that lies on the plane goes to neither.
        void split(const Winding &w, const double *n, double dist, Winding &front, Winding &back)
        {
            front.clear();
            back.clear();
            const std::size_t count = w.size();
            std::vector<double> d(count);
            bool anyFront = false, anyBack = false;
            for (std::size_t i = 0; i < count; ++i)
            {
                d[i] = dot(w[i], n) - dist;
                anyFront = anyFront || d[i] > kOn;
                anyBack = anyBack || d[i] < -kOn;
            }
            if (!anyBack)
            {
                if (anyFront)
                    front = w;
                return;
            }
            if (!anyFront)
            {
                back = w;
                return;
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                const Point &a = w[i];
                const Point &b = w[(i + 1) % count];
                const double da = d[i], db = d[(i + 1) % count];
                if (da >= -kOn)
                    front.push_back(a);
                if (da <= kOn)
                    back.push_back(a);
                if ((da > kOn && db < -kOn) || (da < -kOn && db > kOn))
                {
                    const double t = da / (da - db);
                    Point m;
                    for (int k = 0; k < 3; ++k)
                        m[k] = a[k] + (b[k] - a[k]) * t;
                    front.push_back(m);
                    back.push_back(m);
                }
            }
            if (front.size() < 3)
                front.clear();
            if (back.size() < 3)
                back.clear();
        }

        /// Keep the part of `w` in front of the plane.
        void clip(Winding &w, const double *n, double dist)
        {
            Winding front, back;
            split(w, n, dist, front, back);
            // (On the plane: kept -- it is not behind it.)
            bool on = true;
            for (const Point &p : w)
                on = on && std::fabs(dot(p, n) - dist) <= kOn;
            if (!on)
                w = front;
        }

        double area(const Winding &w)
        {
            double cross[3] = {0, 0, 0};
            for (std::size_t i = 1; i + 1 < w.size(); ++i)
            {
                const double a[3] = {w[i][0] - w[0][0], w[i][1] - w[0][1], w[i][2] - w[0][2]};
                const double b[3] = {w[i + 1][0] - w[0][0], w[i + 1][1] - w[0][1], w[i + 1][2] - w[0][2]};
                cross[0] += a[1] * b[2] - a[2] * b[1];
                cross[1] += a[2] * b[0] - a[0] * b[2];
                cross[2] += a[0] * b[1] - a[1] * b[0];
            }
            return 0.5 * std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
        }

        struct Builder
        {
            const BspFile &bsp;
            std::vector<LeafPortal> &out;
            /// The planes that bound the node now being looked at: each with
            /// the side its space is on.
            struct Bound
            {
                double n[3];
                double dist;
            };
            std::vector<Bound> bounds;

            bool open(int leaf) const
            {
                return leaf > 0 && leaf < static_cast<int>(bsp.leaves.size()) &&
                       bsp.leaves[leaf].contents != hldata::kContentsSolid;
            }

            /// Down `child`'s subtree with `w`: each leaf it reaches, and the
            /// piece of `w` there. `toward` is the side of the node plane the
            /// subtree is on: a piece lying on one of its planes goes the way
            /// a point a hair off the node plane on that side would.
            void filter(const Winding &w, int child, const double *toward,
                        std::vector<std::pair<int, Winding>> &found) const
            {
                if (w.empty())
                    return;
                if (child < 0)
                {
                    found.emplace_back(-(child + 1), w);
                    return;
                }
                const BspNode &node = bsp.nodes[child];
                const BspPlane &plane = bsp.planes[node.plane];
                const double n[3] = {plane.normal[0], plane.normal[1], plane.normal[2]};
                bool on = true;
                for (const Point &p : w)
                    on = on && std::fabs(dot(p, n) - plane.dist) <= kOn;
                if (on)
                {
                    const double along = n[0] * toward[0] + n[1] * toward[1] + n[2] * toward[2];
                    filter(w, node.children[along >= 0 ? 0 : 1], toward, found);
                    return;
                }
                Winding front, back;
                split(w, n, plane.dist, front, back);
                filter(front, node.children[0], toward, found);
                filter(back, node.children[1], toward, found);
            }

            void node(int index)
            {
                const BspNode &node = bsp.nodes[index];
                const BspPlane &plane = bsp.planes[node.plane];
                const double n[3] = {plane.normal[0], plane.normal[1], plane.normal[2]};
                // Its plane, as far as its own space goes.
                Winding w = planeWinding(n, plane.dist);
                for (const Bound &b : bounds)
                {
                    clip(w, b.n, b.dist);
                    if (w.empty())
                        break;
                }
                if (!w.empty())
                {
                    // The leaves on its front that the piece touches, and for
                    // each, those behind.
                    const double toFront[3] = {n[0], n[1], n[2]};
                    const double toBack[3] = {-n[0], -n[1], -n[2]};
                    std::vector<std::pair<int, Winding>> fronts;
                    filter(w, node.children[0], toFront, fronts);
                    for (const auto &[frontLeaf, piece] : fronts)
                    {
                        if (!open(frontLeaf))
                            continue;
                        std::vector<std::pair<int, Winding>> backs;
                        filter(piece, node.children[1], toBack, backs);
                        for (const auto &[backLeaf, hole] : backs)
                        {
                            if (!open(backLeaf) || backLeaf == frontLeaf || area(hole) < 0.5)
                                continue;
                            LeafPortal portal;
                            portal.leaves[0] = frontLeaf;
                            portal.leaves[1] = backLeaf;
                            portal.winding = hole;
                            out.push_back(std::move(portal));
                        }
                    }
                }
                // On down: each child's space is this one's on its side.
                for (int side = 0; side < 2; ++side)
                {
                    if (node.children[side] < 0)
                        continue;
                    Bound b;
                    for (int a = 0; a < 3; ++a)
                        b.n[a] = side == 0 ? n[a] : -n[a];
                    b.dist = side == 0 ? plane.dist : -plane.dist;
                    bounds.push_back(b);
                    this->node(node.children[side]);
                    bounds.pop_back();
                }
            }
        };
    } // namespace

    std::vector<LeafPortal> buildPortals(const BspFile &bsp)
    {
        std::vector<LeafPortal> out;
        if (bsp.models.empty() || bsp.nodes.empty())
            return out;
        const BspModel &world = bsp.models[0];
        Builder builder{bsp, out, {}};
        // The world's box, a little larger, bounds everything.
        for (int a = 0; a < 3; ++a)
        {
            Builder::Bound low{}, high{};
            low.n[a] = 1;
            low.dist = world.mins[a] - 16;
            high.n[a] = -1;
            high.dist = -(world.maxs[a] + 16);
            builder.bounds.push_back(low);
            builder.bounds.push_back(high);
        }
        if (world.headNode[0] >= 0 && world.headNode[0] < static_cast<int>(bsp.nodes.size()))
            builder.node(world.headNode[0]);
        return out;
    }

    bool cookPortals(const std::vector<LeafPortal> &portals, std::size_t leafCount, CookedPortals &cooked,
                     std::string &error)
    {
        std::vector<hldata::Portal> &out = cooked.portals;
        std::vector<std::uint16_t> &refs = cooked.refs;
        std::vector<hldata::LeafPortals> &leaves = cooked.leaves;
        out.clear();
        refs.clear();
        leaves.assign(leafCount, hldata::LeafPortals{0, 0});
        // Each two leaves once: the box round all their holes.
        std::map<std::pair<int, int>, std::array<double, 6>> boxes;
        for (const LeafPortal &portal : portals)
        {
            if (portal.leaves[0] <= 0 || portal.leaves[1] <= 0 || static_cast<std::size_t>(portal.leaves[0]) >= leafCount ||
                static_cast<std::size_t>(portal.leaves[1]) >= leafCount || portal.winding.size() < 3)
                continue;
            const std::pair<int, int> pair = {std::min(portal.leaves[0], portal.leaves[1]),
                                              std::max(portal.leaves[0], portal.leaves[1])};
            auto found = boxes.find(pair);
            if (found == boxes.end())
                found = boxes.insert({pair, {1e30, 1e30, 1e30, -1e30, -1e30, -1e30}}).first;
            for (const Point &p : portal.winding)
            {
                for (int a = 0; a < 3; ++a)
                {
                    found->second[a] = std::min(found->second[a], p[a]);
                    found->second[3 + a] = std::max(found->second[3 + a], p[a]);
                }
            }
        }
        if (boxes.size() > 0xFFFF || boxes.size() * 2 > 0xFFFF)
        {
            error = "the map has more portals than the cooked format holds";
            return false;
        }
        std::vector<std::vector<std::uint16_t>> ofLeaf(leafCount);
        for (const auto &[pair, box] : boxes)
        {
            hldata::Portal cooked{};
            cooked.leaves[0] = static_cast<std::uint16_t>(pair.first);
            cooked.leaves[1] = static_cast<std::uint16_t>(pair.second);
            for (int a = 0; a < 3; ++a)
            {
                cooked.mins[a] = static_cast<std::int16_t>(std::clamp(std::floor(box[a]) - 1, -32768.0, 32767.0));
                cooked.maxs[a] = static_cast<std::int16_t>(std::clamp(std::ceil(box[3 + a]) + 1, -32768.0, 32767.0));
            }
            const auto index = static_cast<std::uint16_t>(out.size());
            out.push_back(cooked);
            ofLeaf[pair.first].push_back(index);
            ofLeaf[pair.second].push_back(index);
        }
        for (std::size_t leaf = 0; leaf < leafCount; ++leaf)
        {
            leaves[leaf].first = static_cast<std::uint16_t>(refs.size());
            leaves[leaf].count = static_cast<std::uint16_t>(ofLeaf[leaf].size());
            refs.insert(refs.end(), ofLeaf[leaf].begin(), ofLeaf[leaf].end());
        }
        return true;
    }

    SeenWorst seenWorst(const BspFile &bsp, const CookedLevel &level, const CookedPortals &portals)
    {
        SeenWorst out;
        const int leafCount = static_cast<int>(level.leaves.size());
        const int visLeaves = level.visLeafCount;
        hldata::PortalSet set;
        set.portals = portals.portals.data();
        set.refs = portals.refs.data();
        set.leaves = portals.leaves.data();
        set.leafCount = portals.leaves.size() == level.leaves.size() ? leafCount : 0;
        std::vector<hldata::PortalRect> rects;
        std::vector<std::uint16_t> slotLeaf, queue, leafMark, leafSlot;
        std::vector<std::uint8_t> queued;
        hldata::PortalSpace space =
            hldata::portalSpaceOf(rects, slotLeaf, queue, queued, leafMark, leafSlot, 768, leafCount);
        std::vector<int> reached, stamp(level.surfaces.size(), 0);
        int stampNow = 0;
        // The camera's own edges, as loose as it has them (Camera.arm.cpp).
        const double h = 47.0 * M_PI / 180.0, v = 39.0 * M_PI / 180.0;
        const double far = 8192.0;
        std::vector<double> leafFar;
        auto leafAt = [&](const double *p)
        {
            int child = bsp.models[0].headNode[0];
            while (child >= 0)
            {
                const BspNode &node = bsp.nodes[child];
                const BspPlane &plane = bsp.planes[node.plane];
                child = node.children[plane.normal[0] * p[0] + plane.normal[1] * p[1] + plane.normal[2] * p[2] -
                                                  plane.dist >=
                                              0
                                          ? 0
                                          : 1];
            }
            return -(child + 1);
        };
        for (int leaf = 1; leaf <= visLeaves && leaf < leafCount; ++leaf)
        {
            const hldata::Leaf &l = level.leaves[leaf];
            if (l.contents == hldata::kContentsSolid || l.contents == hldata::kContentsSky)
                continue;
            const double eyeAt[3] = {(l.mins[0] + l.maxs[0]) * 0.5, (l.mins[1] + l.maxs[1]) * 0.5,
                                     (l.mins[2] + l.maxs[2]) * 0.5};
            if (leafAt(eyeAt) != leaf)
                continue; // (a leaf whose middle is elsewhere: its neighbours stand for it)
            const std::vector<std::uint8_t> row = bsp.decompressVis(leaf);
            auto inPvs = [&](int other)
            {
                return other >= 1 && static_cast<std::size_t>((other - 1) >> 3) < row.size() &&
                       (row[(other - 1) >> 3] & (1 << ((other - 1) & 7))) != 0;
            };
            const std::int32_t at[3] = {static_cast<std::int32_t>(std::lround(eyeAt[0])),
                                        static_cast<std::int32_t>(std::lround(eyeAt[1])),
                                        static_cast<std::int32_t>(std::lround(eyeAt[2]))};
            double farthest = 0;
            for (int pitchStep = -1; pitchStep <= 1; ++pitchStep)
            {
                for (int yawStep = 0; yawStep < 8; ++yawStep)
                {
                    const double yaw = yawStep * M_PI / 4, pitch = pitchStep * M_PI / 6;
                    const double f[3] = {std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch),
                                         std::sin(pitch)};
                    const double r[3] = {std::sin(yaw), -std::cos(yaw), 0};
                    const double u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
                    hldata::PortalEye eye{};
                    for (int a = 0; a < 3; ++a)
                    {
                        eye.at[a] = at[a];
                        eye.forward[a] = static_cast<std::int32_t>(std::lround(f[a] * 4096));
                        eye.right[a] = static_cast<std::int32_t>(std::lround(r[a] * 4096));
                        eye.up[a] = static_cast<std::int32_t>(std::lround(u[a] * 4096));
                    }
                    const std::int32_t sh = static_cast<std::int32_t>(std::lround(std::sin(h) * 4096)),
                                       ch = static_cast<std::int32_t>(std::lround(std::cos(h) * 4096)),
                                       sv = static_cast<std::int32_t>(std::lround(std::sin(v) * 4096)),
                                       cv = static_cast<std::int32_t>(std::lround(std::cos(v) * 4096));
                    eye.left[0] = -sh, eye.left[1] = ch, eye.rightEdge[0] = sh, eye.rightEdge[1] = ch;
                    eye.bottom[0] = -sv, eye.bottom[1] = cv, eye.top[0] = sv, eye.top[1] = cv;
                    reached.clear();
                    const int got = set.present()
                                        ? hldata::portalFlood(set, space, leaf, eye, inPvs,
                                                              [&](int seen) { reached.push_back(seen); })
                                        : -1;
                    if (got < 0)
                    {
                        ++out.givenUp;
                        reached.clear();
                        for (int other = 1; other <= visLeaves; ++other)
                        {
                            if (other == leaf || inPvs(other))
                                reached.push_back(other);
                        }
                    }
                    // What of it the walk sends: facing the eye, in the view,
                    // nearer than the far plane.
                    ++stampNow;
                    int polygons = 0, vertices = 0;
                    // The view's side planes: inside where n.(p - eye) >= -radius.
                    const double sides[4][3] = {
                        {f[0] * std::sin(h) + r[0] * std::cos(h), f[1] * std::sin(h) + r[1] * std::cos(h),
                         f[2] * std::sin(h) + r[2] * std::cos(h)},
                        {f[0] * std::sin(h) - r[0] * std::cos(h), f[1] * std::sin(h) - r[1] * std::cos(h),
                         f[2] * std::sin(h) - r[2] * std::cos(h)},
                        {f[0] * std::sin(v) + u[0] * std::cos(v), f[1] * std::sin(v) + u[1] * std::cos(v),
                         f[2] * std::sin(v) + u[2] * std::cos(v)},
                        {f[0] * std::sin(v) - u[0] * std::cos(v), f[1] * std::sin(v) - u[1] * std::cos(v),
                         f[2] * std::sin(v) - u[2] * std::cos(v)}};
                    for (const int seen : reached)
                    {
                        const hldata::Leaf &sl = level.leaves[seen];
                        for (int c = 0; c < 8; ++c)
                        {
                            const double corner[3] = {double((c & 1) ? sl.maxs[0] : sl.mins[0]), double((c & 2) ? sl.maxs[1] : sl.mins[1]),
                                                      double((c & 4) ? sl.maxs[2] : sl.mins[2])};
                            const double d[3] = {corner[0] - eyeAt[0], corner[1] - eyeAt[1], corner[2] - eyeAt[2]};
                            farthest = std::max(farthest, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
                        }
                        for (int k = 0; k < sl.markCount; ++k)
                        {
                            const int s = level.marks[sl.firstMark + k];
                            if (stamp[s] == stampNow)
                                continue;
                            stamp[s] = stampNow;
                            const hldata::Surface &surface = level.surfaces[s];
                            const hldata::Plane &plane = level.planes[surface.planeRef & hldata::kSurfacePlaneMask];
                            const double side = (plane.normal[0] * eyeAt[0] + plane.normal[1] * eyeAt[1] +
                                                 plane.normal[2] * eyeAt[2]) /
                                                    1073741824.0 -
                                                plane.dist / 4096.0;
                            if ((surface.planeRef & hldata::kSurfacePlaneBack) ? side >= 0 : side <= 0)
                                continue;
                            const double c[3] = {surface.center[0] - eyeAt[0], surface.center[1] - eyeAt[1],
                                                 surface.center[2] - eyeAt[2]};
                            bool in = c[0] * f[0] + c[1] * f[1] + c[2] * f[2] - surface.radius <= far;
                            for (int p = 0; p < 4 && in; ++p)
                                in = c[0] * sides[p][0] + c[1] * sides[p][1] + c[2] * sides[p][2] >= -surface.radius;
                            if (!in)
                                continue;
                            polygons += surface.polygons;
                            vertices += surface.vertices;
                        }
                    }
                    if (polygons > out.polygons)
                        out.polygons = polygons;
                    if (vertices > out.vertices)
                        out.vertices = vertices;
                    // The worst leaves, each once at its worst way.
                    auto worse = [](const SeenWorst::Hotspot &a, const SeenWorst::Hotspot &b) { return a.polygons > b.polygons; };
                    auto found = std::find_if(out.hotspots.begin(), out.hotspots.end(),
                                              [&](const SeenWorst::Hotspot &one) { return one.leaf == leaf; });
                    if (found != out.hotspots.end() && found->polygons >= polygons)
                        continue;
                    if (found != out.hotspots.end())
                        out.hotspots.erase(found);
                    SeenWorst::Hotspot spot;
                    spot.leaf = leaf;
                    for (int a = 0; a < 3; ++a)
                        spot.at[a] = at[a];
                    spot.yaw = yawStep * 45;
                    spot.pitch = pitchStep * 30;
                    spot.polygons = polygons;
                    spot.vertices = vertices;
                    out.hotspots.insert(std::upper_bound(out.hotspots.begin(), out.hotspots.end(), spot, worse), spot);
                    if (out.hotspots.size() > 10)
                        out.hotspots.pop_back();
                }
            }
            leafFar.push_back(farthest);
        }
        // The map's far distance: nineteen leaves in twenty see no farther.
        if (!leafFar.empty())
        {
            std::sort(leafFar.begin(), leafFar.end());
            const double at95 = leafFar[std::min(leafFar.size() - 1, (leafFar.size() * 95) / 100)];
            int units = static_cast<int>(std::ceil(at95 / 256.0)) * 256;
            out.farUnits = std::clamp(units, hldata::kLevelFarLeast, hldata::kLevelFarMost);
        }
        return out;
    }
} // namespace hlds
