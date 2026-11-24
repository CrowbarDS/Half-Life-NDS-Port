#include "hlds/GxStream.hpp"

#include <algorithm>
#include <array>

namespace hlds
{
    namespace
    {
        // Geometry command numbers (GBATEK, "DS 3D Geometry Commands").
        constexpr std::uint8_t kCmdColor = 0x20;
        constexpr std::uint8_t kCmdTexCoord = 0x22;
        constexpr std::uint8_t kCmdVtx16 = 0x23;
        constexpr std::uint8_t kCmdVtx10 = 0x24;
        constexpr std::uint8_t kCmdVtxXY = 0x25;
        constexpr std::uint8_t kCmdVtxXZ = 0x26;
        constexpr std::uint8_t kCmdVtxYZ = 0x27;
        constexpr std::uint8_t kCmdVtxDiff = 0x28;

        /// VTX_10 keeps the top 10 of a coordinate's 16 bits.
        constexpr int kVtx10Shift = 6;

        constexpr std::uint32_t lo10(int v)
        {
            return static_cast<std::uint32_t>(v) & 0x3FF;
        }

        constexpr int signExtend10(std::uint32_t v)
        {
            return static_cast<int>((v & 0x3FF) ^ 0x200) - 0x200;
        }
        constexpr std::uint8_t kCmdBeginVtxs = 0x40;

        constexpr std::uint32_t lo16(std::int16_t v)
        {
            return static_cast<std::uint16_t>(v);
        }
    } // namespace

    int GxRun::polygons() const
    {
        const int n = static_cast<int>(vertices.size());
        switch (type)
        {
        case GxPrimitive::Triangles:     return n >= 3 && n % 3 == 0 ? n / 3 : -1;
        case GxPrimitive::Quads:         return n >= 4 && n % 4 == 0 ? n / 4 : -1;
        case GxPrimitive::TriangleStrip: return n >= 3 ? n - 2 : -1;
        case GxPrimitive::QuadStrip:     return n >= 4 && n % 2 == 0 ? n / 2 - 1 : -1;
        }
        return -1;
    }

    void GxPacker::begin(GxPrimitive type)
    {
        commands_.push_back(Command{kCmdBeginVtxs, 1, {static_cast<std::uint32_t>(type), 0}});
    }

    void GxPacker::vertex(const GxVertex &v)
    {
        if (!haveColour_ || colour_ != v.colour || everyColour_)
        {
            commands_.push_back(Command{kCmdColor, 1, {v.colour, 0}});
            haveColour_ = true;
            colour_ = v.colour;
        }
        commands_.push_back(Command{kCmdTexCoord, 1, {lo16(v.s) | (lo16(v.t) << 16), 0}});
        position(v);
    }

    void GxPacker::position(const GxVertex &v)
    {
        const std::int16_t now[3] = {v.x, v.y, v.z};
        const bool tens = (v.x & 63) == 0 && (v.y & 63) == 0 && (v.z & 63) == 0;
        const Command ten{kCmdVtx10,
                          1,
                          {lo10(v.x >> kVtx10Shift) | (lo10(v.y >> kVtx10Shift) << 10) |
                               (lo10(v.z >> kVtx10Shift) << 20),
                           0}};
        Command chosen{kCmdVtx16, 2, {lo16(v.x) | (lo16(v.y) << 16), lo16(v.z)}};

        if (havePrevious_)
        {
            const int dx = v.x - previous_[0];
            const int dy = v.y - previous_[1];
            const int dz = v.z - previous_[2];
            auto fits = [](int d) { return d >= -512 && d <= 511; };
            // All the one-word forms cost the same, so the order here is only
            // a preference; each is used only where it reproduces the vertex
            // exactly.
            if (dz == 0)
                chosen = Command{kCmdVtxXY, 1, {lo16(v.x) | (lo16(v.y) << 16), 0}};
            else if (dy == 0)
                chosen = Command{kCmdVtxXZ, 1, {lo16(v.x) | (lo16(v.z) << 16), 0}};
            else if (dx == 0)
                chosen = Command{kCmdVtxYZ, 1, {lo16(v.y) | (lo16(v.z) << 16), 0}};
            else if (fits(dx) && fits(dy) && fits(dz))
                chosen = Command{kCmdVtxDiff, 1, {lo10(dx) | (lo10(dy) << 10) | (lo10(dz) << 20), 0}};
            else if (tens)
                chosen = ten;
        }
        else if (tens)
        {
            chosen = ten;
        }
        commands_.push_back(chosen);
        havePrevious_ = true;
        for (int i = 0; i < 3; ++i)
            previous_[i] = now[i];
    }

    std::vector<std::uint32_t> GxPacker::finish()
    {
        std::vector<std::uint32_t> words;
        colourWords_.clear();
        for (std::size_t i = 0; i < commands_.size(); i += 4)
        {
            std::uint32_t header = 0;
            for (std::size_t k = 0; k < 4 && i + k < commands_.size(); ++k)
            {
                header |= static_cast<std::uint32_t>(commands_[i + k].code) << (8 * k);
            }
            words.push_back(header);
            for (std::size_t k = 0; k < 4 && i + k < commands_.size(); ++k)
            {
                const Command &c = commands_[i + k];
                if (c.code == kCmdColor)
                    colourWords_.push_back(static_cast<std::uint32_t>(words.size()));
                for (int p = 0; p < c.paramCount; ++p)
                    words.push_back(c.params[p]);
            }
        }
        commands_.clear();
        haveColour_ = false;
        havePrevious_ = false;
        return words;
    }

    bool decodeGxStream(const std::uint32_t *words, std::size_t count, std::vector<GxRun> &out,
                        std::string &error)
    {
        out.clear();
        GxVertex pending;
        bool haveColour = false;
        bool havePrevious = false;
        std::size_t pos = 0;

        auto fail = [&](const std::string &what)
        {
            error = what + " at word " + std::to_string(pos);
            out.clear();
            return false;
        };

        while (pos < count)
        {
            const std::uint32_t header = words[pos++];
            if (header == 0)
                return fail("empty command word");
            for (int k = 0; k < 4; ++k)
            {
                const auto code = static_cast<std::uint8_t>(header >> (8 * k));
                if (code == 0)
                {
                    if ((header >> (8 * k)) != 0)
                        return fail("command after a gap in a packed word");
                    break;
                }
                const int params = code == kCmdVtx16 ? 2 : 1;
                const bool isVertex = code >= kCmdVtx16 && code <= kCmdVtxDiff;
                if (code != kCmdColor && code != kCmdTexCoord && code != kCmdBeginVtxs &&
                    !isVertex)
                    return fail("unexpected command " + std::to_string(code));
                if (pos + params > count)
                    return fail("stream ends inside a command");
                const std::uint32_t p0 = words[pos];
                const std::uint32_t p1 = params == 2 ? words[pos + 1] : 0;
                pos += params;

                if (code == kCmdBeginVtxs)
                {
                    if (p0 > 3)
                        return fail("bad primitive type");
                    GxRun run;
                    run.type = static_cast<GxPrimitive>(p0);
                    out.push_back(run);
                }
                else if (code == kCmdColor)
                {
                    if (p0 > 0x7FFF)
                        return fail("colour is not RGB555");
                    pending.colour = static_cast<std::uint16_t>(p0);
                    haveColour = true;
                }
                else if (code == kCmdTexCoord)
                {
                    pending.s = static_cast<std::int16_t>(p0 & 0xFFFF);
                    pending.t = static_cast<std::int16_t>(p0 >> 16);
                }
                else
                {
                    if (out.empty())
                        return fail("vertex before the first begin");
                    if (!haveColour)
                        return fail("vertex before the first colour");
                    // The forms that lean on the previous vertex are only
                    // meaningful once there has been one IN THIS STREAM: the
                    // console's idea of "previous" at this point is whatever
                    // surface it happened to draw last.
                    if (code != kCmdVtx16 && code != kCmdVtx10 && !havePrevious)
                        return fail("relative vertex with no vertex before it");
                    const auto a = static_cast<std::int16_t>(p0 & 0xFFFF);
                    const auto b = static_cast<std::int16_t>(p0 >> 16);
                    switch (code)
                    {
                    case kCmdVtx16:
                        if ((p1 >> 16) != 0)
                            return fail("garbage in a vertex's high bits");
                        pending.x = a;
                        pending.y = b;
                        pending.z = static_cast<std::int16_t>(p1 & 0xFFFF);
                        break;
                    case kCmdVtx10:
                        if ((p0 >> 30) != 0)
                            return fail("garbage in a vertex's high bits");
                        pending.x = static_cast<std::int16_t>(signExtend10(p0) * 64);
                        pending.y = static_cast<std::int16_t>(signExtend10(p0 >> 10) * 64);
                        pending.z = static_cast<std::int16_t>(signExtend10(p0 >> 20) * 64);
                        break;
                    case kCmdVtxXY:
                        pending.x = a;
                        pending.y = b;
                        break;
                    case kCmdVtxXZ:
                        pending.x = a;
                        pending.z = b;
                        break;
                    case kCmdVtxYZ:
                        pending.y = a;
                        pending.z = b;
                        break;
                    default: // VTX_DIFF
                    {
                        if ((p0 >> 30) != 0)
                            return fail("garbage in a vertex's high bits");
                        const int x = pending.x + signExtend10(p0);
                        const int y = pending.y + signExtend10(p0 >> 10);
                        const int z = pending.z + signExtend10(p0 >> 20);
                        if (x < -32768 || x > 32767 || y < -32768 || y > 32767 || z < -32768 ||
                            z > 32767)
                            return fail("a vertex difference leaves the coordinate range");
                        pending.x = static_cast<std::int16_t>(x);
                        pending.y = static_cast<std::int16_t>(y);
                        pending.z = static_cast<std::int16_t>(z);
                        break;
                    }
                    }
                    havePrevious = true;
                    out.back().vertices.push_back(pending);
                }
            }
        }
        for (const GxRun &run : out)
        {
            if (run.polygons() < 0)
            {
                pos = count;
                return fail("a run's vertex count does not fit its primitive type");
            }
        }
        return true;
    }
    GxShapeAudit &GxShapeAudit::operator+=(const GxShapeAudit &o)
    {
        polygons += o.polygons;
        flatLead += o.flatLead;
        reversed += o.reversed;
        reflex += o.reflex;
        noArea += o.noArea;
        concaveQuads += o.concaveQuads;
        crossedQuads += o.crossedQuads;
        nearQuads += o.nearQuads;
        return *this;
    }

    GxShapeAudit auditGxShapes(const std::vector<GxRun> &runs, const double normal[3])
    {
        GxShapeAudit audit;
        using Vec = std::array<std::int64_t, 3>;
        // (b - a) x (c - a), exact.
        auto cross = [](const GxVertex &a, const GxVertex &b, const GxVertex &c)
        {
            const std::int64_t e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
            const std::int64_t e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
            return Vec{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                       e1[0] * e2[1] - e1[1] * e2[0]};
        };
        auto side = [&](const Vec &v)
        {
            return v[0] * normal[0] + v[1] * normal[1] + v[2] * normal[2];
        };
        // A polygon as its ring, counter-clockwise, and the three vertices the
        // hardware judges it by.
        auto polygon = [&](const GxVertex *const *ring, int n, const GxVertex *const *lead)
        {
            ++audit.polygons;
            Vec area{0, 0, 0};
            for (int i = 1; i + 1 < n; ++i)
            {
                const Vec c = cross(*ring[0], *ring[i], *ring[i + 1]);
                for (int a = 0; a < 3; ++a)
                    area[a] += c[a];
            }
            if (area == Vec{0, 0, 0})
            {
                ++audit.noArea;
                return;
            }
            // "In line" as far as the grid can say: a triangle no more than
            // 1.5 steps high. A polygon that is that thin whichever three of
            // its corners are taken has no better order to be sent in, and is
            // let through.
            auto height2 = [&](const GxVertex &a, const GxVertex &b, const GxVertex &c)
            {
                const Vec t = cross(a, b, c);
                const double area2 = double(t[0]) * t[0] + double(t[1]) * t[1] + double(t[2]) * t[2];
                double longest2 = 0;
                const GxVertex *tri[3] = {&a, &b, &c};
                for (int i = 0; i < 3; ++i)
                {
                    const GxVertex &p = *tri[i], &q = *tri[(i + 1) % 3];
                    longest2 = std::max(longest2, double(q.x - p.x) * (q.x - p.x) +
                                                      double(q.y - p.y) * (q.y - p.y) +
                                                      double(q.z - p.z) * (q.z - p.z));
                }
                return longest2 > 0 ? area2 / longest2 : 0.0;
            };
            bool anyFit = false;
            for (int i = 0; i < n && !anyFit; ++i)
            {
                const GxVertex &a = *ring[i], &b = *ring[(i + 1) % n], &c = *ring[(i + 2) % n];
                anyFit = side(cross(a, b, c)) > 0 && height2(a, b, c) > 1.5 * 1.5;
            }
            const Vec first = cross(*lead[0], *lead[1], *lead[2]);
            if (anyFit && height2(*lead[0], *lead[1], *lead[2]) <= 1.5 * 1.5)
                ++audit.flatLead;
            else if (anyFit && side(first) < 0)
                ++audit.reversed;
            if (n == 4)
            {
                // Each corner, strictly: exactly in line, turning the way the
                // quad faces by more than 1.5 steps, or neither.
                int against[4] = {0, 0, 0, 0};
                bool near = false;
                // (Exactly in one plane on the grid: a corner turning its way
                // at all is convex from every view.)
                const Vec n0 = cross(*ring[0], *ring[1], *ring[2]);
                const bool planar = n0[0] * (ring[3]->x - ring[0]->x) + n0[1] * (ring[3]->y - ring[0]->y) +
                                        n0[2] * (ring[3]->z - ring[0]->z) ==
                                    0;
                for (int i = 0; i < 4; ++i)
                {
                    const GxVertex &a = *ring[(i + 3) % 4], &b = *ring[i], &c = *ring[(i + 1) % 4];
                    const Vec turn = cross(a, b, c);
                    if (turn == Vec{0, 0, 0})
                        continue;
                    if (side(turn) <= 0)
                        against[i] = 1;
                    else if (!planar && height2(a, b, c) <= 1.5 * 1.5)
                        near = true;
                }
                if ((against[0] && against[2]) || (against[1] && against[3]))
                    ++audit.crossedQuads;
                else if (against[0] || against[1] || against[2] || against[3])
                    ++audit.concaveQuads;
                else if (near)
                    ++audit.nearQuads;
            }
            for (int i = 0; i < n; ++i)
            {
                // A corner that is a vertex ON a side, off the line only by
                // the grid's rounding, is not a reflex corner.
                const GxVertex &a = *ring[i], &b = *ring[(i + 1) % n], &c = *ring[(i + 2) % n];
                const Vec turn = cross(a, b, c);
                const double length2 = double(c.x - a.x) * (c.x - a.x) +
                                       double(c.y - a.y) * (c.y - a.y) +
                                       double(c.z - a.z) * (c.z - a.z);
                const double off2 = double(turn[0]) * turn[0] + double(turn[1]) * turn[1] +
                                    double(turn[2]) * turn[2];
                if (side(turn) < 0 && off2 > 1.5 * 1.5 * length2)
                {
                    ++audit.reflex;
                    break;
                }
            }
        };

        for (const GxRun &run : runs)
        {
            const std::vector<GxVertex> &v = run.vertices;
            const int count = run.polygons();
            for (int k = 0; k < count; ++k)
            {
                switch (run.type)
                {
                case GxPrimitive::Triangles:
                {
                    const GxVertex *ring[3] = {&v[3 * k], &v[3 * k + 1], &v[3 * k + 2]};
                    polygon(ring, 3, ring);
                    break;
                }
                case GxPrimitive::Quads:
                {
                    const GxVertex *ring[4] = {&v[4 * k], &v[4 * k + 1], &v[4 * k + 2],
                                               &v[4 * k + 3]};
                    polygon(ring, 4, ring);
                    break;
                }
                case GxPrimitive::QuadStrip:
                {
                    const GxVertex *ring[4] = {&v[2 * k], &v[2 * k + 1], &v[2 * k + 3],
                                               &v[2 * k + 2]};
                    // The hardware swaps the last two before it looks.
                    const GxVertex *lead[3] = {&v[2 * k], &v[2 * k + 1], &v[2 * k + 3]};
                    polygon(ring, 4, lead);
                    break;
                }
                case GxPrimitive::TriangleStrip:
                {
                    // Every other triangle of a strip is sent the other way
                    // round, and the hardware allows for it.
                    const GxVertex *ring[3] = {&v[k], &v[k + 1 + (k & 1)], &v[k + 2 - (k & 1)]};
                    polygon(ring, 3, ring);
                    break;
                }
                }
            }
        }
        return audit;
    }
} // namespace hlds
