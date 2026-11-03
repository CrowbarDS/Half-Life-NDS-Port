#pragma once

// What moves the view besides the stylus, as the game has it: the kick of a
// weapon and of being hit (its "punch angle", which dies away), the bob of a
// walk, the lean into a sidestep, the jolt of a landing, the fall and roll of
// the camera when the player dies. And what being hurt puts on the screen:
// how red, and from which side.
//
// The numbers are the game's own (its view.cpp and player.cpp): cl_bob 0.01,
// cl_bobcycle 0.8, cl_bobup 0.5, cl_rollangle 2, cl_rollspeed 200; a fall is
// safe to 580 units a second and fatal at 1024.
//
// Angles here are degrees in Q12. std-only and inline: the host tests run it.

#include "HlFixed.hpp"

namespace hldata
{
    struct ViewState
    {
        fx punch[3] = {0, 0, 0}; ///< pitch (up is positive), yaw, roll
        int bobMs = 0;
        fx bob = 0;              ///< units (Q12): the eye up or down, the weapon forward
        fx roll = 0;             ///< the lean into a sidestep
        /// How red the screen is with being hurt (0..256), and how strongly
        /// each of the four marks round the crosshair shows it (front,
        /// right, back, left; 0..256).
        int flash = 0;
        int pain[4] = {0, 0, 0, 0};
        /// Since the player died (ms); 0 while alive.
        int deadMs = 0;
        bool walks = true; ///< the bob and the lean are wanted (a setting)
    };

    inline constexpr fx kViewDegree = kFxOne;
    /// Degrees (Q12) as the 65536ths of a turn the camera is turned in.
    inline constexpr int viewTurns(fx degrees)
    {
        return static_cast<int>((static_cast<std::int64_t>(degrees) * 2) / 45);
    }

    /// A kick: added to what is still dying away of the last.
    inline void viewKick(ViewState &view, fx pitch, fx yaw = 0, fx roll = 0)
    {
        view.punch[0] += pitch;
        view.punch[1] += yaw;
        view.punch[2] += roll;
        // (No more than a neck allows.)
        for (fx &angle : view.punch)
            angle = angle > 30 * kViewDegree ? 30 * kViewDegree : (angle < -30 * kViewDegree ? -30 * kViewDegree : angle);
    }

    /// What a landing at `speed` units a second (downwards) costs: nothing
    /// up to 580, everything at 1024, in a straight line between.
    inline constexpr int kFallSafe = 580;
    inline constexpr int kFallFatal = 1024;
    inline constexpr int kFallJolt = 350; ///< from here it is felt, if not paid for
    inline constexpr int fallDamage(int speed)
    {
        return speed <= kFallSafe ? 0 : ((speed - kFallSafe) * 100) / (kFallFatal - kFallSafe);
    }
    /// ... and in a match whose rules make every fall the same (the game's
    /// mp_falldamage 0): 10, past the safe speed.
    inline constexpr int fallDamage(int speed, bool flat)
    {
        return !flat ? fallDamage(speed) : (speed <= kFallSafe ? 0 : 10);
    }

    /// Hurt, by `damage`, from `side` (the direction to what did it, in the
    /// player's own axes: forward and right, any scale; both 0 if it came
    /// from nowhere in particular -- a fall, a blast underfoot).
    inline void viewHurt(ViewState &view, int damage, fx forward, fx right)
    {
        if (damage <= 0)
            return;
        const int more = 50 + damage * 5;
        view.flash = view.flash + more > 170 ? 170 : view.flash + more;
        const fx f = forward < 0 ? -forward : forward, r = right < 0 ? -right : right;
        if (f == 0 && r == 0)
        {
            for (int &mark : view.pain)
                mark = 256;
        }
        else
        {
            // (A side shows if the hit is within about 60 degrees of it.)
            if (f * 2 >= r)
                view.pain[forward > 0 ? 0 : 2] = 256;
            if (r * 2 >= f)
                view.pain[right > 0 ? 1 : 3] = 256;
        }
        // The head goes back, and away from the side it was hit on.
        const fx kick = (damage > 20 ? 4 : 2) * kViewDegree;
        viewKick(view, kick, 0, right > 0 ? -kick / 2 : (right < 0 ? kick / 2 : 0));
    }

    /// A landing, at `speed` units a second: the view dips.
    inline void viewLand(ViewState &view, int speed)
    {
        if (speed < kFallJolt)
            return;
        // (The game's 0.013 of a degree for each unit a second.)
        fx dip = static_cast<fx>((static_cast<std::int64_t>(speed) * 53));
        dip = dip > 8 * kViewDegree ? 8 * kViewDegree : dip;
        viewKick(view, -dip);
    }

    /// `ms` more milliseconds. `speed` is how fast the player is going over
    /// the ground and `side` how fast to their right (Q12 units a second);
    /// `onGround` whether they are on it.
    inline void viewTick(ViewState &view, int ms, fx speed, fx side, bool onGround, bool dead)
    {
        // The kick dies away: ten degrees a second, and half of itself.
        fx length = fxLength(view.punch);
        if (length > 0)
        {
            fx less = static_cast<fx>((static_cast<std::int64_t>(10 * kViewDegree + length / 2) * ms) / 1000);
            less = less > length ? length : less;
            for (fx &angle : view.punch)
                angle -= static_cast<fx>((static_cast<std::int64_t>(angle) * less) / length);
        }
        view.flash -= (ms * 400) / 1000;
        view.flash = view.flash < 0 ? 0 : view.flash;
        for (int &mark : view.pain)
        {
            mark -= (ms * 512) / 1000;
            mark = mark < 0 ? 0 : mark;
        }
        if (dead)
        {
            view.deadMs += ms;
            view.flash = view.flash < 110 ? 110 : view.flash;
            view.bob = 0;
            view.roll = 0;
            return;
        }
        view.deadMs = 0;
        if (!view.walks)
        {
            view.bob = 0;
            view.roll = 0;
            return;
        }
        // The bob: 0.01 of the speed, up for half of each 0.8 s and down for
        // the other half, three tenths of it steady.
        if (onGround)
            view.bobMs = (view.bobMs + ms) % 800;
        std::int32_t sine, cosine;
        fxSinCos(static_cast<std::uint16_t>(view.bobMs < 400 ? (view.bobMs * 32768) / 400
                                                               : 32768 + ((view.bobMs - 400) * 32768) / 400),
                 sine, cosine);
        const fx most = speed / 100;
        // (Each fits 32 bits before its divide, which is then a multiply.)
        fx bob = (most * 3) / 10 + static_cast<fx>((static_cast<std::int64_t>(most) * 7 * sine) >> 12) / 10;
        bob = bob > fxFromInt(4) ? fxFromInt(4) : (bob < -fxFromInt(7) ? -fxFromInt(7) : bob);
        view.bob = onGround ? bob : view.bob - view.bob / 4;
        // The lean: two degrees at 200 units a second sideways, and no more.
        const fx fast = fxFromInt(200);
        fx lean = side > fast ? fast : (side < -fast ? -fast : side);
        view.roll = lean / (fast / (2 * kViewDegree)); // (lean * 2 degrees / fast, in 32 bits)
    }

    /// The camera's fall when the player dies: how far the eye has sunk
    /// towards the floor (0..256 of the way) and how far it has rolled over
    /// (degrees, Q12), `deadMs` after.
    inline constexpr int deathSink(int deadMs)
    {
        return deadMs >= 900 ? 256 : (deadMs * 256) / 900;
    }
    inline constexpr fx deathRoll(int deadMs)
    {
        return deadMs >= 1100 ? 80 * kViewDegree : static_cast<fx>((static_cast<std::int64_t>(deadMs) * 80 * kViewDegree) / 1100);
    }
    /// How long after dying the game goes back.
    inline constexpr int kDeathWaitMs = 2600;

    /// The kick of each weapon when it fires (a kWeapon*), as the game has
    /// it; `chance` any number, for the one that kicks either way.
    inline fx weaponKick(int weapon, std::uint32_t chance)
    {
        switch (weapon)
        {
        case 2:  return 2 * kViewDegree;  // the pistol
        case 3:  return 5 * kViewDegree;  // the shotgun
        case 5:  return static_cast<fx>(static_cast<int>(chance % (4 * 4096 + 1)) - 2 * 4096); // the MP5: two either way
        case 6:  return 10 * kViewDegree; // the .357
        case 7:  return 2 * kViewDegree;  // the crossbow
        case 11: return 5 * kViewDegree;  // the RPG
        case 12: return 2 * kViewDegree;  // the gauss
        case 14: return 1 * kViewDegree;  // the hornetgun
        default: return 0;
        }
    }
} // namespace hldata
