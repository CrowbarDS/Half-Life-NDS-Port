// Guns on mounts, and mortars on call, on the game's own maps (HlMonsters.hpp's
// tankTick and the rest): c2a2e's guard gun turns to the player and fires;
// c2a2e's big gun is taken at its controls, follows the view and fires at
// the fire button, and is let go of; c1a3b's mortars come down on who calls
// them; c2a5g's wheels turn, move their bars, and aim its mortar table. Every
// map's controls are taken from a pace away, facing them, and only so.
//
// Needs the game: HLDS_VALVE_DIR. Without it, nothing is run.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "TestGame.hpp"

using namespace hldata;
using namespace hlds;
using namespace hldstest;

namespace
{
    std::unique_ptr<Game> load(const char *dir, const char *map)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
        {
            check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
            return nullptr;
        }
        finish(*game, nullptr, dir);
        if (!game->ok)
            return nullptr;
        return game;
    }

    /// The record of `kind` called `name`, or -1.
    int recordNamed(const Game &g, const char *name, LogicKind kind)
    {
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &record = g.view.logic[i];
            if (record.kind == static_cast<std::uint8_t>(kind) && record.name != 0 &&
                g.view.nameHashes[record.name] == nameHash(name))
                return static_cast<int>(i);
        }
        return -1;
    }

    int lost = 0;

    /// A frame, the player held where they are, and kept alive.
    void tickStill(Game &g, int ms = 33)
    {
        const fx at[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        logicTick(g.logic, g.player, ms);
        monstersTick(g.logic, g.player, ms);
        g.drain();
        for (int k = 0; k < 3; ++k)
            g.player.origin[k] = at[k];
        lost += 100 - g.player.health;
        g.player.health = 100;
        g.player.inv.armor = 0;
    }

    /// The player stood `distance` from `at` along `yaw` (turns), on the
    /// floor there; false if there is none.
    bool standOff(Game &g, const fx *at, std::uint16_t yaw, int distance)
    {
        fx sine, cosine;
        fxSinCos(yaw, sine, cosine);
        ActorState probe;
        probe.pos[0] = at[0] + cosine * distance;
        probe.pos[1] = at[1] + sine * distance;
        probe.pos[2] = at[2];
        if (!actorToFloor(g.move, probe, fxFromInt(64), fxFromInt(512)))
            return false;
        g.player.origin[0] = probe.pos[0];
        g.player.origin[1] = probe.pos[1];
        g.player.origin[2] = probe.pos[2] + kStandingFeet;
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        return true;
    }

    void testGuardGun(const char *dir)
    {
        auto game = load(dir, "c2a2e");
        check(game != nullptr, "c2a2e loads");
        if (!game)
            return;
        Game &g = *game;
        const int gun = recordNamed(g, "siloguardgun", LogicKind::Tank);
        check(gun >= 0, "c2a2e's guard gun is a tank");
        if (gun < 0)
            return;
        LogicState &state = g.states[gun];
        check(state.running, "... on from the start");
        const std::uint16_t rest = static_cast<std::uint16_t>(state.bits);
        // In front of it, a little to one side of where it points.
        bool placed = false;
        for (int far : {300, 400, 250, 500})
            placed = placed || standOff(g, state.base, static_cast<std::uint16_t>(rest + 1000), far);
        check(placed, "room in front of the gun");
        if (!placed)
            return;
        lost = 0;
        int flashes = 0, wrong = 0;
        for (int t = 0; t < 30 * 5; ++t)
        {
            tickStill(g);
            for (const Effect &effect : g.actors.effects)
            {
                if (effect.kind == 0 || effect.leftMs <= 0)
                    continue;
                // (One of the map's own, past the common ones; 0.75 of its size.)
                if (effect.kind == kEffectSprite && effect.sprite >= static_cast<int>(CommonSprite::Count) &&
                    effect.sprite < g.view.spriteCount && effect.size == -12)
                    ++flashes;
                else if (effect.kind == kEffectMuzzle)
                    ++wrong;
            }
        }
        std::printf("test_tanks: c2a2e guard gun: %d frames of its own flash, %d of another\n", flashes, wrong);
        check(flashes > 0 && wrong == 0, "... its flash is the map's sprite, at the map's scale");
        const int turned = static_cast<std::int16_t>(static_cast<std::uint16_t>(state.bits) - rest);
        std::printf("test_tanks: c2a2e gun turned %d degrees, the player lost %d\n", turned * 360 / 65536, lost);
        check(turned > 1000, "it turns to the player");
        check(lost > 0, "... and fires at them");
        // Out of its reach behind it: it does not.
        placed = false;
        for (int far : {300, 400, 250})
            placed = placed || standOff(g, state.base, static_cast<std::uint16_t>(rest + 32768), far);
        if (placed)
        {
            for (int t = 0; t < 30 * 4; ++t)
                tickStill(g);
            lost = 0;
            for (int t = 0; t < 30 * 3; ++t)
                tickStill(g);
            std::printf("test_tanks: behind it, the player lost %d\n", lost);
            check(lost == 0, "... and not at them behind it, out of its reach");
        }
    }

    /// Each map's controls: from a pace off, looked at, they are taken; looked
    /// away from, or from further than the game reaches, they aren't.
    void testEveryControl(const char *dir, const char *map)
    {
        auto game = load(dir, map);
        check(game != nullptr, (std::string(map) + " loads").c_str());
        if (!game)
            return;
        Game &g = *game;
        int count = 0;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &volume = g.view.logic[i];
            if (volume.kind != static_cast<std::uint8_t>(LogicKind::TankControls))
                continue;
            ++count;
            fx middle[3];
            for (int a = 0; a < 3; ++a)
                middle[a] = fxFromInt(volume.mins[a] + volume.maxs[a]) / 2;
            bool taken = false, refused = true, far = true;
            for (int way = 0; way < 4 && !taken; ++way)
            {
                const std::uint16_t yaw = static_cast<std::uint16_t>(way * 16384);
                // (On the floor there; or, a tower's narrow walk having none
                // the probe finds, a little above the foot of the controls.)
                if (!standOff(g, middle, yaw, 44))
                {
                    fx sine, cosine;
                    fxSinCos(yaw, sine, cosine);
                    g.player.origin[0] = middle[0] + cosine * 44;
                    g.player.origin[1] = middle[1] + sine * 44;
                    g.player.origin[2] = fxFromInt(volume.mins[2]) + kStandingFeet;
                    for (int up = 0; up < 40 && !hullPositionFree(g.move.standing, g.player.origin); up += 4)
                        g.player.origin[2] += fxFromInt(4);
                }
                if (!hullPositionFree(g.move.standing, g.player.origin))
                    continue;
                g.player.viewHeight = kFxOne * 28;
                fx to[3] = {middle[0] - g.player.origin[0], middle[1] - g.player.origin[1],
                            middle[2] - g.player.origin[2] - g.player.viewHeight};
                const fx length = fxLength(to);
                fx forward[3], away[3];
                for (int a = 0; a < 3; ++a)
                {
                    forward[a] = static_cast<fx>((static_cast<std::int64_t>(to[a]) << kFxShift) / length);
                    away[a] = -forward[a];
                }
                refused = !tankUse(g.logic, g.player, away) && refused;
                taken = tankUse(g.logic, g.player, forward);
                if (taken)
                {
                    check(tankManned(g.logic) >= 0, (std::string(map) + ": its gun is had").c_str());
                    tankRelease(g.logic);
                    if (standOff(g, middle, yaw, 160))
                        far = !tankUse(g.logic, g.player, forward);
                    tankRelease(g.logic);
                }
            }
            std::printf("test_tanks: %s controls %d: %s\n", map, static_cast<int>(i), taken ? "taken" : "NOT TAKEN");
            check(taken, (std::string(map) + ": USE from a pace off, facing its controls, takes them").c_str());
            check(refused, (std::string(map) + ": ... not with its back to them").c_str());
            check(far, (std::string(map) + ": ... nor from out of reach").c_str());
        }
        check(count > 0, (std::string(map) + " has controls").c_str());
    }

    void testBigGun(const char *dir)
    {
        auto game = load(dir, "c2a2e");
        check(game != nullptr, "c2a2e loads (its big gun)");
        if (!game)
            return;
        Game &g = *game;
        const int gun = recordNamed(g, "biggun", LogicKind::Tank);
        int controls = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount && controls < 0; ++i)
        {
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::TankControls))
                controls = static_cast<int>(i);
        }
        check(gun >= 0 && controls >= 0, "c2a2e's big gun and its controls");
        if (gun < 0 || controls < 0)
            return;
        const LogicRecord &volume = g.view.logic[controls];
        // In the middle of its controls, on the floor.
        ActorState probe;
        for (int a = 0; a < 3; ++a)
            probe.pos[a] = fxFromInt((volume.mins[a] + volume.maxs[a]) / 2);
        probe.pos[2] = fxFromInt(volume.maxs[2]);
        actorToFloor(g.move, probe, 0, fxFromInt(256));
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = probe.pos[a];
        g.player.origin[2] += kStandingFeet;
        // (Stood in them, it is down at them they are looked at.)
        const fx ahead[3] = {0, 0, -kFxOne};
        check(tankUse(g.logic, g.player, ahead), "USE at its controls takes them");
        check(tankManned(g.logic) == gun, "... it is the big gun that is had");
        LogicState &state = g.states[gun];
        const std::uint16_t rest = static_cast<std::uint16_t>(state.bits);
        // Looked to the left of how it rests, and fired.
        const std::uint16_t look = static_cast<std::uint16_t>(rest + 40 * 65536 / 360);
        int tracers = 0;
        for (int t = 0; t < 30 * 3; ++t)
        {
            tankControl(g.logic, g.player, look, 0, t > 45, 33);
            tickStill(g);
            for (const Effect &effect : g.actors.effects)
                tracers += effect.kind == kEffectTracer && effect.leftMs > 0 ? 1 : 0;
        }
        const int turned = static_cast<std::int16_t>(static_cast<std::uint16_t>(state.bits) - rest);
        std::printf("test_tanks: c2a2e big gun followed the view %d degrees (of 40); %d frames of tracers\n",
                    turned * 360 / 65536, tracers);
        check(turned > 35 * 65536 / 360 && turned < 45 * 65536 / 360, "the gun follows the player's view");
        check(tracers > 0, "... and fires at the fire button");
        // Walked away from it: let go.
        g.player.origin[0] += fxFromInt(40);
        tankControl(g.logic, g.player, look, 0, false, 33);
        check(tankManned(g.logic) < 0, "... and is let go of when the player walks away");
    }

    void testMortars(const char *dir)
    {
        auto game = load(dir, "c1a3b");
        check(game != nullptr, "c1a3b loads");
        if (!game)
            return;
        Game &g = *game;
        const int field = recordNamed(g, "c1a3_mortar01", LogicKind::MortarField);
        check(field >= 0, "c1a3b's mortar field");
        if (field < 0)
            return;
        const LogicRecord &volume = g.view.logic[field];
        ActorState probe;
        for (int a = 0; a < 3; ++a)
            probe.pos[a] = fxFromInt((volume.mins[a] + volume.maxs[a]) / 2);
        probe.pos[2] = fxFromInt(volume.maxs[2]);
        actorToFloor(g.move, probe, 0, fxFromInt(4096));
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = probe.pos[a];
        g.player.origin[2] += kStandingFeet;
        logicUse(g.logic, g.player, field, -1, kUseToggle);
        lost = 0;
        int bursts = 0, before = 0;
        for (int t = 0; t < 30 * 5; ++t)
        {
            tickStill(g);
            for (const Effect &effect : g.actors.effects)
                bursts += effect.kind == kEffectBurst && effect.leftMs > 400 ? 1 : 0;
            if (t == 30 * 2)
                before = bursts;
        }
        std::printf("test_tanks: c1a3b mortars: %d bursts (%d in the first two seconds), the player lost %d\n",
                    bursts, before, lost);
        check(before == 0, "the shells take their time coming");
        check(bursts >= 3, "... three of them");
        check(lost > 0, "... on whoever called them");
    }

    void testWheels(const char *dir)
    {
        auto game = load(dir, "c2a5g");
        check(game != nullptr, "c2a5g loads");
        if (!game)
            return;
        Game &g = *game;
        const int wheel = recordNamed(g, "xcontroller", LogicKind::MomentaryButton);
        const int bar = recordNamed(g, "bar1", LogicKind::MomentaryDoor);
        const int field = recordNamed(g, "mortars", LogicKind::MortarField);
        check(wheel >= 0 && bar >= 0 && field >= 0, "c2a5g's wheel, its bar and its mortar table");
        if (wheel < 0 || bar < 0 || field < 0)
            return;
        const fx barAt[3] = {g.brushStates[g.view.logic[bar].brush].pos[0], g.brushStates[g.view.logic[bar].brush].pos[1],
                             g.brushStates[g.view.logic[bar].brush].pos[2]};
        logicUse(g.logic, g.player, wheel, -1, kUseSet);
        for (int t = 0; t < 30 * 2; ++t)
            tickStill(g);
        logicUse(g.logic, g.player, wheel, -1, kUseSet); // (let go)
        for (int t = 0; t < 30 * 2; ++t)
            tickStill(g);
        const int round = g.states[wheel].phase;
        const fx *now = g.brushStates[g.view.logic[bar].brush].pos;
        const fx moved[3] = {now[0] - barAt[0], now[1] - barAt[1], now[2] - barAt[2]};
        std::printf("test_tanks: c2a5g wheel %d/65536 round, its bar moved %.1f\n", round, fxLength(moved) / 4096.0);
        check(round > 0 && round < 65536, "the wheel turns while used, and stops when let go");
        check(fxLength(moved) > fxFromInt(4), "... and its bar goes with it");
        // The table's shells fall where the wheel has them.
        const LogicRecord &volume = g.view.logic[field];
        logicUse(g.logic, g.player, field, -1, kUseToggle);
        tickStill(g);
        const fx want = fxFromInt(volume.mins[0]) +
                        static_cast<fx>((static_cast<std::int64_t>(fxFromInt(volume.maxs[0] - volume.mins[0])) * round) >> 16);
        const fx off = g.states[field].base[0] - want;
        check(off < fxFromInt(2) && off > -fxFromInt(2), "... and the mortars fall where it has them");
    }
} // namespace

int main()
{
    const char *valve = std::getenv("HLDS_VALVE_DIR");
    if (valve == nullptr || *valve == '\0')
    {
        std::printf("test_tanks: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 77;
    }
    testGuardGun(valve);
    testBigGun(valve);
    for (const char *map : {"c2a2b1", "c2a2e", "c2a2h", "c2a5e", "c2a5f", "c3a1b", "crossfire", "doublecross",
                            "rocket_frenzy", "stalkyard"})
        testEveryControl(valve, map);
    testMortars(valve);
    testWheels(valve);
    if (g_failures == 0)
        std::printf("test_tanks: ok\n");
    return g_failures == 0 ? 0 : 1;
}
