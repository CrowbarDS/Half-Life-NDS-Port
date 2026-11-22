#pragma once

// The suit's lamp, by the game's rules: it wants the suit; its battery lasts
// about two minutes alight and is full again twenty seconds after it is put
// out; at nothing it goes out by itself.
//
// What it does to the picture is the game's too: a light eighty units
// across its half, at the point the middle of the view meets, the same size
// however far off that is; brightest at its middle and fading evenly to its
// edge; as bright as it gets out to five hundred units and fainter beyond.
// The game ADDS it to the wall's own light, so that the wall is seen
// brighter; here the wall is drawn again towards its own colour at full
// light (the lamp pass), which is the nearest this hardware comes to that
// -- and a light of the hardware's own falls on whoever stands in it
// (lampOn).
//
// std-only and inline: the host tests run it.

#include <cstdint>

#include "HlFixed.hpp"
#include "HlMovers.hpp"

namespace hldata
{
    inline constexpr std::int32_t kLampFull = 100000; ///< thousandths of a hundredth
    inline constexpr int kLampReach = 2000;           ///< units: what the view meets farther off than this is not lit
    /// The game's light: a ball this many units from its middle to its
    /// edge, at the point the view meets -- the same size however far off
    /// that is (so smaller on the screen, as a real one is not, and as the
    /// game's is).
    inline constexpr int kLampRadius = 80;
    inline constexpr int kLampNear = 500;  ///< as bright as it gets out to here; fainter beyond
    inline constexpr int kLampFullStrength = 22; ///< of 31, at its middle: what the game's adds, measured beside it (tools/ref)

    struct LampState
    {
        bool on = false;
        /// (The game hands a new player 99 of a hundred.)
        std::int32_t charge = kLampFull - 1000;
    };

    /// 0..100, as the game shows it.
    inline constexpr int lampPercent(const LampState &lamp)
    {
        return (lamp.charge + 999) / 1000;
    }

    /// The switch. False if it stays as it was (no suit). It lights with
    /// whatever is in it, as the game's does: with nothing, it goes out
    /// again at the next tick.
    inline bool lampToggle(LampState &lamp, bool suit)
    {
        if (lamp.on)
        {
            lamp.on = false;
            return true;
        }
        if (!suit)
            return false;
        lamp.on = true;
        return true;
    }

    /// `ms` more milliseconds. True if it went out by itself.
    inline bool lampTick(LampState &lamp, int ms)
    {
        if (lamp.on)
        {
            // (All of it in a hundred and twenty seconds.)
            lamp.charge -= ms * 5 / 6;
            if (lamp.charge <= 0)
            {
                lamp.charge = 0;
                lamp.on = false;
                return true;
            }
            return false;
        }
        // (... and back in twenty.)
        lamp.charge += ms * 5;
        if (lamp.charge > kLampFull)
            lamp.charge = kLampFull;
        return false;
    }

    // What the lamp does to the picture: its light is PROJECTED onto the
    // world. The surfaces in the beam are drawn a second time with one
    // small round picture of light, and the hardware itself works out where
    // on that picture each vertex is, from where the vertex is in the world
    // (its texture matrix, in the mode that takes the vertex: WorldDraw's
    // lampPass). So the light lies on the walls -- round a corner, long on a
    // floor it skims, smaller on the screen the farther off -- as a light
    // does, and is not a shape on the screen.

    /// The light for one frame: seen from `from` along `axis`, with `u` and
    /// `v` across it (all Q12, unit); a ball of `radius` units whose middle
    /// is `distance` units along -- so nothing nearer than `near` or farther
    /// than `reach` along the axis is in it.
    struct LampBeam
    {
        fx from[3] = {0, 0, 0};
        fx axis[3] = {0, 0, 0};
        fx u[3] = {0, 0, 0};
        fx v[3] = {0, 0, 0};
        int radius = 0;
        int near = 0;
        int reach = 0;
        int strength = 0; ///< of 31: how solid the light is drawn, met head-on
    };

    /// How wide the light is where it lands (the same at any distance), and
    /// how strong: full out to kLampNear, then as the square of how much
    /// farther, to nothing at the lamp's reach.
    inline constexpr int lampRadius(int)
    {
        return kLampRadius;
    }
    inline constexpr int lampStrength(int distance)
    {
        if (distance >= kLampReach)
            return 0;
        if (distance <= kLampNear)
            return kLampFullStrength;
        const int fade = (kLampFullStrength * kLampNear / distance) * kLampNear / distance;
        return fade < 1 ? 1 : fade;
    }

    /// Where the lamp shines from an eye at `eye` looking along `forward`
    /// (Q12, unit): on what the line of its reach meets first
    /// (CL_UpdateFlashlight's), `distance` units down the view (at least 8).
    /// Nothing within its reach: not `hit`, and nothing is lit.
    struct LampAim
    {
        bool hit = false;
        fx at[3] = {0, 0, 0};
        int distance = 0;
    };
    inline LampAim lampAim(const MoveWorld &world, const fx *eye, const fx *forward)
    {
        LampAim aim;
        fx end[3], normal[3];
        for (int a = 0; a < 3; ++a)
            end[a] = eye[a] + forward[a] * kLampReach;
        if (!traceShotWall(world, eye, end, aim.at, normal))
            return aim;
        std::int64_t along = 0;
        for (int a = 0; a < 3; ++a)
            along += static_cast<std::int64_t>(aim.at[a] - eye[a]) * forward[a];
        const int distance = static_cast<int>(along >> 24);
        aim.distance = distance < 8 ? 8 : distance;
        aim.hit = true;
        return aim;
    }

    /// Whether a ball (`centre` in whole units, `radius`) is in the light at
    /// all: beside its line within the two radii, and along it no nearer
    /// than the light begins nor farther than it ends.
    inline bool lampTouches(const LampBeam &beam, const std::int32_t *centre, int radius)
    {
        const std::int32_t d[3] = {centre[0] - fxToInt(beam.from[0]), centre[1] - fxToInt(beam.from[1]),
                                   centre[2] - fxToInt(beam.from[2])};
        const std::int32_t along = (d[0] * beam.axis[0] + d[1] * beam.axis[1] + d[2] * beam.axis[2]) >> 12;
        if (along < beam.near - radius || along > beam.reach + radius)
            return false;
        const std::int64_t square = static_cast<std::int64_t>(d[0]) * d[0] + static_cast<std::int64_t>(d[1]) * d[1] +
                                    static_cast<std::int64_t>(d[2]) * d[2] - static_cast<std::int64_t>(along) * along;
        const std::int64_t wide = beam.radius + radius;
        return square < wide * wide;
    }

    /// What the hardware is given to project with, for vertices that are
    /// quarter units (as the level's are) in a space whose origin the lamp
    /// is at `from` in (Q12 units) with `u` and `v` across the beam (Q12):
    /// the six numbers of its texture matrix that matter, and the texture
    /// coordinate every vertex is sent with -- which the hardware adds to
    /// what it works out. `size` is the picture's width in texels.
    struct LampProjection
    {
        std::int32_t s[3], t[3];
        std::uint32_t texCoord;
    };
    inline LampProjection lampProjection(const fx *from, const fx *u, const fx *v, int radius, int size)
    {
        LampProjection out{};
        // A coordinate is sixteenths of a texel; the hardware takes
        // (vertex * matrix) >> 24. Half the picture is `radius` units, a
        // unit four of the vertex's: so 2 * size / radius sixteenths for
        // each of the vertex's steps, times 2^24, over the 4096 of `u`.
        const std::int32_t scale = (8192 * size) / (radius > 0 ? radius : 1);
        std::int64_t s0 = 0, t0 = 0;
        for (int a = 0; a < 3; ++a)
        {
            out.s[a] = u[a] * scale;
            out.t[a] = v[a] * scale;
            // (Where the lamp is, as a vertex would say it.)
            s0 += static_cast<std::int64_t>(from[a] >> 10) * out.s[a];
            t0 += static_cast<std::int64_t>(from[a] >> 10) * out.t[a];
        }
        // The middle of the picture is where the beam's own line is. (In
        // sixteen bits, round and round: what the hardware adds it to goes
        // round with it.)
        const std::uint32_t s = static_cast<std::uint32_t>(8 * size - (s0 >> 24)) & 0xFFFF;
        const std::uint32_t t = static_cast<std::uint32_t>(8 * size - (t0 >> 24)) & 0xFFFF;
        out.texCoord = s | (t << 16);
        return out;
    }

    /// A surface's geometry (the level's packed commands), for the light's
    /// pass: its vertices as they are, every colour `colour` and every
    /// texture coordinate `texCoord`. `emit` is given each word in turn.
    template <class Emit>
    inline void lampStream(const std::uint32_t *words, int count, std::uint32_t texCoord, Emit &&emit,
                           std::uint32_t colour = 0x7FFF)
    {
        int at = 0;
        while (at < count)
        {
            const std::uint32_t header = words[at++];
            emit(header);
            for (int k = 0; k < 4; ++k)
            {
                const std::uint32_t code = (header >> (8 * k)) & 0xFF;
                if (code == 0)
                    break;
                if (code == 0x20) // a colour
                    emit(colour);
                else if (code == 0x22) // a texture coordinate
                    emit(texCoord);
                else
                {
                    // (A vertex of sixteen bits a coordinate is two words;
                    // everything else here is one.)
                    for (int n = code == 0x23 ? 2 : 1; n > 0 && at < count; --n)
                        emit(words[at++]);
                    continue;
                }
                ++at;
            }
        }
    }

    /// How bright the lamp is as a light of the hardware's (0..31) on
    /// something `along` units down the view and `aside` units off its
    /// middle, when the light is `distance` units off: in the ball, by how
    /// near its middle.
    inline constexpr int lampOn(int along, int aside, int distance = kLampNear)
    {
        const int back = along > distance ? along - distance : distance - along;
        if (along <= 0 || back >= kLampRadius || aside >= kLampRadius)
            return 0;
        const int off = back > aside ? back : aside;
        const int light = lampStrength(distance) * 2 * (kLampRadius - off) / kLampRadius;
        return light > 31 ? 31 : light;
    }
} // namespace hldata
