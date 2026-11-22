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
// brighter; here the lit part of the wall is drawn again in its own texture,
// as bright as the game makes it (HlLampPatch.hpp, WorldDraw::lampPass) --
// and whoever stands near where it falls is lit by it as the game lights
// them (ModelDraw::lampShade).
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
    /// `rest` (if given, kept by the caller from one tick to the next):
    /// what is left over of the drain, in sixths -- without it a frame of
    /// 16 ms drains 13 for 13.3, and the battery lasts 2% too long.
    inline bool lampTick(LampState &lamp, int ms, int *rest = nullptr)
    {
        if (lamp.on)
        {
            // (All of it in a hundred and twenty seconds.)
            const int sixths = ms * 5 + (rest != nullptr ? *rest : 0);
            lamp.charge -= sixths / 6;
            if (rest != nullptr)
                *rest = sixths % 6;
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
        int actor = -1; ///< a monster or one of the people it stopped on (lampAimAll), or -1
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

} // namespace hldata
