#include "game/Player.hpp"

#include "HlLogic.hpp"
#include "Settings.hpp"

#include "Global.hpp"

#include <nds.h>

#include "DebugLog.hpp"
#include "data/LevelFile.hpp"
#include "game/Game.hpp"
#include "game/GameApi.hpp"
#include "game/Input.hpp"
#include "net/NetGame.hpp"
#include "render/Camera.hpp"
#include "render/HudDraw.hpp"

// ARM code (the file name says so), for the movement's 64-bit multiplies; NOT
// in fast memory. The movement runs once a frame, and its ten thousand
// instructions are mostly branches not taken. What it calls thousands of
// times -- the hull trace -- is in fast memory by itself (HL_HOT, in
// HlFixed.hpp), for this and for everything else that traces.

namespace hlrt
{
    Player player;

    namespace
    {
        using namespace hldata;

        /// One sixtieth of a second in Q16, to the nearest unit; four vblanks
        /// are 1/15 s to within one part in 4000.
        constexpr std::int32_t kVblankTime = 1092;
        /// The longest step taken in one go. A frame that took longer than this
        /// (a load, a hitch) is played in slow motion rather than letting the
        /// player cover a quarter of a room between two collision checks.
        constexpr int kMaxVblanks = 4;

        constexpr int kFlySpeed = 320; // units a second
    } // namespace

    void Player::spawn()
    {
        const LevelView &view = level.view;
        state = PlayerState();
        arsenal = Arsenal();
        state.arsenal = &arsenal;
        state.cheats = &game.cheats;
        camera.yaw = 0;
        camera.pitch = 0;
        for (std::uint32_t i = 0; i < view.entityCount; ++i)
        {
            const EntityRecord &entity = view.entities[i];
            if (entity.classId != static_cast<std::uint16_t>(EntityClass::InfoPlayerStart))
                continue;
            for (int a = 0; a < 3; ++a)
                state.origin[a] = entity.origin[a];
            camera.yaw = entity.angles[1];
            break;
        }

        // A development aid: "x y z yaw" in debug_start.txt, in whole units
        // and degrees, starts there instead (tools/emu/play.sh writes it).
        if (std::FILE *file = std::fopen(global.dataPath("debug_start.txt").c_str(), "r"))
        {
            int x, y, z, yaw, track = -1, die = 0, models = 0, weapon = 0, god = 0;
            if (std::fscanf(file, "%d %d %d %d %d %d %d %d %d", &x, &y, &z, &yaw, &track, &die, &models, &weapon,
                            &god) >= 4)
            {
                // (A ninth: god mode, for a measure the player must live
                // through.)
                if (god > 0)
                    game.cheats.god = true;
                // (A fifth number is a track of music to start with; a
                // sixth, the seconds until the player dies -- once: not
                // again when they are back.)
                debugTrack = track;
                static bool died = false;
                if (die > 0 && !died)
                {
                    died = true;
                    debugDieMs = die * 1000;
                }
                state.origin[0] = fxFromInt(x);
                state.origin[1] = fxFromInt(y);
                state.origin[2] = fxFromInt(z);
                camera.yaw = static_cast<std::uint16_t>((yaw * 65536) / 360);
                // ... and armed, wherever that is.
                // (With everything there is to hold: what is tried there is
                // as often a weapon as a place.)
                state.inv.have = kHaveSuit | (1u << kWeaponCrowbar) | (1u << kWeaponGlock) | (1u << kWeaponShotgun) |
                                 (1u << kWeaponGrenade) | (1u << kWeaponMp5) | (1u << kWeaponPython);
                state.inv.grenades = 5;
                state.inv.current = kWeaponCrowbar;
                state.inv.clipGlock = 17;
                state.inv.clipShotgun = 8;
                state.inv.clipMp5 = 50;
                state.inv.clipPython = 6;
                state.inv.ammo9mm = 68;
                state.inv.ammoBuck = 32;
                state.inv.ammo357 = 12;
                state.inv.ammoAr = 3;
                // ... and those that came later. (An eighth number: the one
                // to have in hand.)
                for (int w = kWeaponFirstMore; w < kWeaponCount; ++w)
                    giveWeapon(state, w);
                arsenal.boltsClip = 5;
                arsenal.bolts = 25;
                arsenal.satchels = 5;
                arsenal.tripmines = 5;
                arsenal.snarks = 15;
                arsenal.have = static_cast<std::uint16_t>(arsenal.have | kArsenalRpgLoaded);
                arsenal.rockets = 4;
                arsenal.uranium = 100;
                arsenal.hornets = 8;
                if (weapon > kWeaponNone && weapon < kWeaponCount)
                    state.inv.current = static_cast<std::uint8_t>(weapon);
            }
            std::fclose(file);
        }
    }

    void Player::turn()
    {
        // At the aim speed of 10, one pixel of stylus travel is 1/512 of a
        // turn: the width of the screen is half a turn. (Turned by the
        // stylus's finer reading: sixteenths of a pixel.)
        // (Looking down the crossbow's sight, slower by as much as the
        // picture is narrower, as the game does.)
        const int narrow = camera.zoomed ? Camera::kZoomFovDegrees : camera.fovDegrees;
        const int pitch = hldata::turnOf(input.lookFineY, settings.pitchSpeed) * narrow / camera.fovDegrees;
        const int yaw = hldata::turnOf(input.lookFineX, settings.aimSpeed) * narrow / camera.fovDegrees;
        camera.yaw = static_cast<std::uint16_t>(camera.yaw - yaw);
        camera.pitch = static_cast<std::int16_t>(camera.pitch + (settings.invertPitch ? pitch : -pitch));
        camera.clampPitch();
    }

    void Player::fly(int vblanks)
    {
        camera.update(); // for the view vectors
        const fx step = (fxFromInt(kFlySpeed) / 60) * vblanks;
        for (int i = 0; i < 3; ++i)
        {
            const std::int64_t along =
                static_cast<std::int64_t>(camera.forward[i]) * input.moveForward +
                static_cast<std::int64_t>(camera.right[i]) * input.moveSide;
            state.origin[i] += static_cast<fx>((along * step) >> kFxShift);
            state.velocity[i] = 0;
        }
        state.origin[2] += (input.jump ? step : 0) - (input.duck ? step : 0);
    }

    void Player::frame(int vblanks)
    {
        if (vblanks < 1)
            vblanks = 1;
        if (vblanks > kMaxVblanks)
            vblanks = kMaxVblanks;

        const bool dead = state.health <= 0 && !noclip;
        // (The dead do not look about, or go anywhere but down.)
        if (dead)
        {
            input.moveForward = input.moveSide = 0;
            input.jump = input.duck = false;
        }
        else
        {
            turn();
        }

        if (noclip)
        {
            fly(vblanks);
        }
        else
        {
            const DebugLog::Perf::Scope scope(DebugLog::Section::Move);
            MoveCommand cmd;
            cmd.forward = input.moveForward;
            cmd.side = input.moveSide;
            cmd.jump = input.jump;
            cmd.duck = input.duck;
            cmd.walk = input.walk;
            // (Not in a match: a guest's USE isn't sent as it is held.)
            cmd.use = input.useHeld && !netGame.playing();
            // libnds angles are 32768 to the circle; ours are 65536.
            const fx s = sinLerp(static_cast<s16>(camera.yaw >> 1));
            const fx c = cosLerp(static_cast<s16>(camera.yaw >> 1));
            cmd.forwardXY[0] = c;
            cmd.forwardXY[1] = s;
            cmd.rightXY[0] = s;
            cmd.rightXY[1] = -c;
            // The view's own axes, with pitch, for ladders. The camera's are
            // from the last update, so bring them up to this frame's turn.
            camera.update();
            for (int a = 0; a < 3; ++a)
            {
                cmd.viewForward[a] = camera.forward[a];
                cmd.viewRight[a] = camera.right[a];
            }
            playerMove(state, cmd, game.world, kVblankTime * vblanks);
            if (arsenal.longJumped)
            {
                arsenal.longJumped = false;
                viewKick(view, 5 * kViewDegree);
            }
        }

        // What the move did to the view: a landing jolts it and, hard
        // enough, hurts; a blow reddens the screen, shows where it came
        // from and knocks the head back.
        const int ms = (vblanks * 1000 + 30) / 60;
        if (!noclip)
        {
            if (state.landed > 0 && !dead)
            {
                const int speed = fxToInt(state.landed);
                viewLand(view, speed);
                if (const int cost = fallDamage(speed, game.logic.flatFalls.value))
                {
                    gameApi.hurtPlayer(state, cost, hldata::kDmgFall);
                    game.heard.add(CommonSound::FallPain, nullptr);
                    // (A fall that kills: the game's splat too.)
                    if (state.health <= 0)
                        game.heard.add(CommonSound::BodySplat, nullptr);
                }
                else if (speed > 290)
                {
                    // (Half of what would hurt: the game's thud.)
                    game.heard.add(CommonSound::JumpLand, nullptr, 100);
                }
            }
            if (state.hurt > 0)
            {
                fx forward = 0, right = 0;
                if (state.hurtKnown)
                {
                    const fx d[2] = {state.hurtFrom[0] - state.origin[0], state.hurtFrom[1] - state.origin[1]};
                    const fx c = cosLerp(static_cast<s16>(camera.yaw >> 1)), s = sinLerp(static_cast<s16>(camera.yaw >> 1));
                    forward = static_cast<fx>((static_cast<std::int64_t>(d[0]) * c + static_cast<std::int64_t>(d[1]) * s) >> 12);
                    right = static_cast<fx>((static_cast<std::int64_t>(d[0]) * s - static_cast<std::int64_t>(d[1]) * c) >> 12);
                }
                viewHurt(view, state.hurt, forward, right);
                // What hit them came from somewhere: their own blood, a
                // splash of it in front of them and a stain on whatever
                // is behind.
                if (state.hurtKnown && state.hurt >= 3)
                {
                    fx away[3] = {state.origin[0] - state.hurtFrom[0], state.origin[1] - state.hurtFrom[1], 0};
                    const fx far = fxLength(away);
                    if (far > 0)
                    {
                        for (int k = 0; k < 2; ++k)
                            away[k] = static_cast<fx>((static_cast<std::int64_t>(away[k]) << kFxShift) / far);
                        const fx at[3] = {state.origin[0] - away[0] * 20, state.origin[1] - away[1] * 20,
                                          state.origin[2] + state.viewHeight - fxFromInt(14)};
                        effectAdd(game.actors, kEffectBlood, at, nullptr, 0, 250);
                        markBlood(game.actors, game.world, state.origin, away, false, state.hurt >= 25);
                    }
                }
                state.hurt = 0;
            }
            // The kinds of it, for their pictures (drowning and the like hurt
            // without the rest of this).
            hudDraw.addHurt(state.hurtKinds.value);
            state.hurtKinds.value = 0;
            const fx flat[3] = {state.velocity[0], state.velocity[1], 0};
            const fx s = sinLerp(static_cast<s16>(camera.yaw >> 1)), c = cosLerp(static_cast<s16>(camera.yaw >> 1));
            const fx side = static_cast<fx>((static_cast<std::int64_t>(state.velocity[0]) * s -
                                             static_cast<std::int64_t>(state.velocity[1]) * c) >> 12);
            view.walks = settings.viewBob;
            viewTick(view, ms, fxLength(flat), side, state.onGround, dead);
        }
        else
        {
            view = ViewState();
            state.hurt = 0;
            state.hurtKinds.value = 0;
        }
        camera.kickPitch = static_cast<std::int16_t>(viewTurns(view.punch[0]));
        camera.kickYaw = static_cast<std::int16_t>(viewTurns(view.punch[1]));
        camera.roll = static_cast<std::int16_t>(viewTurns(view.roll + view.punch[2] + deathRoll(view.deadMs)));

        camera.pos[0] = state.origin[0];
        camera.pos[1] = state.origin[1];
        // (Dead, the eye sinks to a hand's breadth off the floor.)
        const fx standing = state.viewHeight + view.bob;
        const fx fallen = -kStandingFeet + fxFromInt(8);
        camera.pos[2] = state.origin[2] + standing + (((fallen - standing) * deathSink(view.deadMs)) >> 8);
        // Something is shaking the view (an explosion, an env_shake): the
        // eye is thrown about, less and less as it dies away.
        const LogicWorld &logic = game.logic;
        if (logic.shakeMs > 0 && logic.shakeWholeMs > 0)
        {
            static std::uint32_t seed = 99;
            const fx reach = static_cast<fx>((static_cast<std::int64_t>(logic.shakeAmplitude) * logic.shakeMs) /
                                             logic.shakeWholeMs);
            for (int a = 0; a < 3; ++a)
            {
                seed = seed * 1664525u + 1013904223u;
                const int part = static_cast<int>((seed >> 12) & 511) - 256; // -256..255
                camera.pos[a] += (reach * part) >> 8;
            }
        }
        camera.update();
    }
} // namespace hlrt
