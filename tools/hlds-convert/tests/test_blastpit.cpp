// The Blast Pit's machinery, on its own maps (c1a4f): the lift whose button is
// inside its car, and the bucket that swings across the shaft with the player
// standing on it; and c1a4i's rocket button, unlocked by what is switched on
// in other maps.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "HlSaveState.hpp"
#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    std::unique_ptr<Game> load(const char *dir, const char *map, Globals *globals = nullptr)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
        {
            check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
            return nullptr;
        }
        finish(*game, globals, dir);
        return game->ok ? std::move(game) : nullptr;
    }

    int nameOf(const Game &g, const char *name)
    {
        const std::uint32_t hash = nameHash(name);
        for (std::uint32_t i = 1; i < g.view.nameCount; ++i)
        {
            if (g.view.nameHashes[i] == hash)
                return static_cast<int>(i);
        }
        return 0;
    }

    /// The first brush entity's record called `name` (or, with `byTarget`,
    /// aimed at it), of `kind`.
    int recordNamed(const Game &g, const char *name, LogicKind kind, bool byTarget = false)
    {
        const int id = nameOf(g, name);
        for (std::uint32_t i = 0; i < g.view.logicCount && id != 0; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.brush != kNoBrush && r.kind == static_cast<std::uint8_t>(kind) &&
                (byTarget ? r.target : r.name) == id)
                return static_cast<int>(i);
        }
        return -1;
    }

    /// The middle of a brush's box, in whole units.
    void middleOf(const Game &g, int record, int out[3])
    {
        const MoverHull &m = g.movers[g.view.logic[record].brush];
        for (int a = 0; a < 3; ++a)
            out[a] = fxToInt((m.mins[a] + m.maxs[a]) / 2);
    }

    void unit(fx *v)
    {
        const fx length = fxLength(v);
        for (int a = 0; a < 3; ++a)
            v[a] = static_cast<fx>((static_cast<std::int64_t>(v[a]) << kFxShift) / length);
    }

    void run(Game &g, int ms)
    {
        for (int t = 0; t < ms; t += 33)
            g.frame();
    }

    // c1a4f's lift: its call button (*22) turns the shaft's lights on and
    // brings the car (lightele, *94) down from the top to the hall; inside
    // it, a button the size of a thumb (elev_start_down, *149: a door that
    // opens by USE, passes for nothing and is not drawn) sends it on down,
    // where it jams halfway (ele_halt_mm).
    void testLift(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a4f");
        check(game != nullptr, "c1a4f cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        const int call = recordNamed(g, "lightson", LogicKind::Button, true);
        const int car = recordNamed(g, "lightele", LogicKind::Train);
        const int inside = recordNamed(g, "elev_start_down", LogicKind::Door);
        if (call < 0 || car < 0 || inside < 0)
            return;
        int at[3];
        middleOf(g, car, at);
        std::printf("test_blastpit: the car starts at z %d\n", at[2]);

        logicUse(g.logic, g.player, call, -1, kUseSet);
        run(g, 25000);
        middleOf(g, car, at);
        std::printf("test_blastpit: called, the car is at z %d (%s)\n", at[2],
                    g.logic.states[car].move == MoveState::AtTop ? "stopped" : "moving");
        check(at[2] > 1100 && at[2] < 1116, "called, the car comes down to the hall (z 1108) and waits");

        // In the car, looking at the button.
        g.player.origin[0] = fxFromInt(590);
        g.player.origin[1] = fxFromInt(-300);
        g.player.origin[2] = fxFromInt(1100);
        run(g, 1000);
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
        int button[3];
        middleOf(g, inside, button);
        fx forward[3] = {fxFromInt(button[0]) - eye[0], fxFromInt(button[1]) - eye[1], fxFromInt(button[2]) - eye[2]};
        unit(forward);
        const int used = logicFindUsable(g.logic, eye, forward);
        std::printf("test_blastpit: standing at %d %d %d, USE finds %d (the button in the car is %d, the call button %d)\n",
                    fxToInt(g.player.origin[0]), fxToInt(g.player.origin[1]), fxToInt(g.player.origin[2]), used,
                    inside, call);
        check(used == inside, "in the car, USE at its button finds it");
        if (used < 0)
            return;
        logicUse(g.logic, g.player, used, -1, kUseSet);
        run(g, 15000);
        middleOf(g, car, at);
        std::printf("test_blastpit: pressed, the button is %s, the car at z %d\n",
                    g.logic.states[inside].dead ? "gone" : "STILL THERE", at[2]);
        check(g.logic.states[inside].dead, "pressed, the button is gone (it kills itself)");
        check(at[2] < 700, "... and the car has gone on down to where it jams (z 588)");
    }

    // c1a4f's bucket (a door on a hinge, half a turn about the shaft): the
    // player stands on it by the button at one end and is swung across to
    // the other, as the game's SV_PushRotate takes whoever stands on what
    // turns. (It used to turn out from under them, and down they went.)
    void testBucket(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a4f");
        check(game != nullptr, "c1a4f cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        const int bucket = recordNamed(g, "bucket", LogicKind::DoorRotating);
        check(bucket >= 0, "c1a4f has the bucket");
        if (bucket < 0)
            return;
        const int brush = g.view.logic[bucket].brush;
        // On it, near its end by the button at x -530.
        g.player.origin[0] = fxFromInt(-490);
        g.player.origin[1] = fxFromInt(928);
        g.player.origin[2] = fxFromInt(-890);
        run(g, 1000);
        std::printf("test_blastpit: on the bucket at %d %d %d, %s it\n", fxToInt(g.player.origin[0]),
                    fxToInt(g.player.origin[1]), fxToInt(g.player.origin[2]),
                    g.player.onGround && g.player.groundMover == brush ? "standing on" : "NOT on");
        check(g.player.onGround && g.player.groundMover == brush, "the player stands on the bucket");
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
        fx forward[3] = {fxFromInt(-530) - eye[0], fxFromInt(928) - eye[1], fxFromInt(-910) - eye[2]};
        unit(forward);
        const int button = logicFindUsable(g.logic, eye, forward);
        check(button >= 0 && g.view.logic[button].target == nameOf(g, "bucket"), "USE finds its button");
        if (button < 0)
            return;
        logicUse(g.logic, g.player, button, -1, kUseSet);
        int turned = 0;
        for (int t = 0; t < 8000; t += 33)
        {
            // (Game::frame, with the view's turn kept.)
            logicTick(g.logic, g.player, 33);
            turned += static_cast<std::int16_t>(g.logic.viewTurn);
            g.logic.viewTurn = 0;
            monstersTick(g.logic, g.player, 33);
            playerMove(g.player, MoveCommand(), g.move, kPlayerTick);
            g.drain();
        }
        std::printf("test_blastpit: swung, the player is at %d %d %d, %s, the bucket %s\n",
                    fxToInt(g.player.origin[0]), fxToInt(g.player.origin[1]), fxToInt(g.player.origin[2]),
                    g.player.onGround && g.player.groundMover == brush ? "on it" : "NOT ON IT",
                    g.logic.states[bucket].move == MoveState::AtTop ? "across" : "not across");
        check(g.logic.states[bucket].move == MoveState::AtTop, "the bucket has swung across");
        check(fxToInt(g.player.origin[0]) < -1300 && fxToInt(g.player.origin[0]) > -1350 &&
                  fxToInt(g.player.origin[2]) > -930,
              "... with the player on it, carried to the far end");
        check(g.player.onGround && g.player.groundMover == brush, "... still standing on it");
        const int half = turned < 0 ? -turned : turned;
        std::printf("test_blastpit: the view turned %d (half a turn is 32768)\n", turned);
        check(half > 32000 && half < 33500, "... and their view turned with it, half a turn");
    }

    // What the cook makes of the Blast Pit's odd actors: c1a4f's barnacle
    // buried in its ceiling is left out (the game never shows it), its
    // three others kept; c1a4d's bubbles in their pipes are turned over
    // ("angles" 180 0 0), drawn as a turret under a ceiling is.
    void testActors(const char *dir)
    {
        {
            const std::unique_ptr<Game> game = load(dir, "c1a4f");
            check(game != nullptr, "c1a4f cooks");
            if (game == nullptr)
                return;
            int barnacles = 0;
            bool buried = false;
            for (std::uint32_t i = 0; i < game->view.actorCount; ++i)
            {
                const ActorRecord &a = game->view.actors[i];
                if (a.kind != static_cast<std::uint8_t>(ActorKind::Barnacle))
                    continue;
                ++barnacles;
                buried = buried || (fxToInt(a.origin[0]) == 438 && fxToInt(a.origin[1]) == 266);
            }
            std::printf("test_blastpit: c1a4f has %d barnacles%s\n", barnacles, buried ? ", the buried one too" : "");
            check(barnacles == 3 && !buried, "c1a4f's barnacle in the ceiling is left out, the other three kept");
        }
        const std::unique_ptr<Game> game = load(dir, "c1a4d");
        check(game != nullptr, "c1a4d cooks");
        if (game == nullptr)
            return;
        int over = 0, cyclers = 0;
        for (std::uint32_t i = 0; i < game->view.actorCount; ++i)
        {
            const ActorRecord &a = game->view.actors[i];
            if (a.kind != static_cast<std::uint8_t>(ActorKind::Cycler))
                continue;
            ++cyclers;
            // (A half turn of pitch: turned over, and faced the other way.)
            over += actorHungOver(a.kind, a.flags) && a.yaw == 32768;
        }
        std::printf("test_blastpit: c1a4d has %d cyclers, %d turned over\n", cyclers, over);
        check(cyclers == 2 && over == 2, "c1a4d's two bubble pipes are drawn turned over");
    }
    /// The first record called `name`, of `kind`, brush or not.
    int anyNamed(const Game &g, const char *name, LogicKind kind)
    {
        const int id = nameOf(g, name);
        for (std::uint32_t i = 0; i < g.view.logicCount && id != 0; ++i)
        {
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(kind) && g.view.logic[i].name == id)
                return static_cast<int>(i);
        }
        return -1;
    }

    void switchOn(Globals &globals)
    {
        globals.set(nameHash("c1a4fpower"), kGlobalOn);
        globals.set(nameHash("c1a4doxy"), kGlobalOn);
        globals.set(nameHash("c1a4dfuel"), kGlobalOn);
    }

    /// Whether c1a4i's rocket button lets itself be pressed, and, pressed,
    /// sets the test fire going.
    bool rocketFires(Game &g, const char *what)
    {
        const int button = recordNamed(g, "init_rocket_fire", LogicKind::Button, true);
        const int manager = anyNamed(g, "init_rocket_fire", LogicKind::MultiManager);
        const int master = anyNamed(g, "testfire_ms", LogicKind::MultiSource);
        check(button >= 0 && manager >= 0 && master >= 0, "c1a4i has its rocket button, manager and master");
        if (button < 0 || manager < 0 || master < 0)
            return false;
        const bool allows = logicMasterAllows(g.logic, button);
        logicUse(g.logic, g.player, button, -1, kUseSet);
        run(g, 200);
        const bool going = g.logic.states[manager].running;
        std::printf("test_blastpit: %s: testfire_ms bits %x of %d, the button %s, the fire %s\n", what,
                    static_cast<unsigned>(g.logic.states[master].bits), g.view.logic[master].auxCount,
                    allows ? "unlocked" : "LOCKED", going ? "going" : "NOT GOING");
        return allows && going;
    }

    /// c1a4i's gated trigger_autos (the three into testfire_ms, and those
    /// that light its dials).
    template <class F> void eachGatedAuto(Game &g, F f)
    {
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::TriggerAuto) &&
                g.view.logic[i].globalState != 0)
                f(static_cast<int>(i));
        }
    }

    // c1a4i's rocket button has master testfire_ms, whose three inputs are
    // trigger_autos that fire once, each held back by a global switched on
    // in another map (c1a4f's power, c1a4d's oxygen and fuel). The player is
    // in c1a4i before any is on, and comes back after: the trigger_autos
    // must wait, and fire as the map is come back to (issue #13).
    void testRocketMaster(const char *dir)
    {
        // 1. First visit, nothing on; the state kept as the map is left.
        std::vector<std::uint8_t> kept, keptOld;
        {
            Globals globals;
            const std::unique_ptr<Game> game = load(dir, "c1a4i", &globals);
            check(game != nullptr, "c1a4i cooks");
            if (game == nullptr)
                return;
            Game &g = *game;
            run(g, 1000);
            int gated = 0, gone = 0;
            eachGatedAuto(g, [&](int i) {
                ++gated;
                gone += g.logic.states[i].dead;
            });
            std::printf("test_blastpit: c1a4i, nothing on: %d gated trigger_autos, %d gone\n", gated, gone);
            check(gated >= 3 && gone == 0, "nothing on: c1a4i's gated trigger_autos wait, unfired");
            check(!logicMasterAllows(g.logic, recordNamed(g, "init_rocket_fire", LogicKind::Button, true)),
                  "nothing on: the rocket button is locked");
            kept = takeMapState(g.logic, g.player, g.view.header->crc32);
            // (As v0.4.2 left them: gone, unfired.)
            eachGatedAuto(g, [&](int i) { g.logic.states[i].dead = true; });
            keptOld = takeMapState(g.logic, g.player, g.view.header->crc32);
        }
        // 2. Come back with all three on.
        for (int old = 0; old < 2; ++old)
        {
            Globals globals;
            switchOn(globals);
            const std::unique_ptr<Game> game = load(dir, "c1a4i", &globals);
            if (game == nullptr)
                return;
            Game &g = *game;
            const std::vector<std::uint8_t> &bytes = old ? keptOld : kept;
            const bool laid = layMapState(bytes.data(), bytes.size(), g.logic, g.player, g.view.header->crc32);
            check(laid, "c1a4i's state lays back");
            run(g, 1000);
            check(rocketFires(g, old ? "come back, an old save" : "come back"),
                  old ? "come back to with all on, a save from before unlocks the rocket button too"
                      : "come back to with all on, the rocket button is unlocked and fires");
        }
        // 3. All on at the first visit: fired then, laid back, not fired
        //    again (a multisource's input flips at each use).
        {
            Globals globals;
            switchOn(globals);
            std::vector<std::uint8_t> fired;
            SaveMapState slot;
            {
                const std::unique_ptr<Game> game = load(dir, "c1a4i", &globals);
                if (game == nullptr)
                    return;
                Game &g = *game;
                run(g, 1000);
                fired = takeMapState(g.logic, g.player, g.view.header->crc32);
                mapRemember(slot, g.logic, g.actors);
            }
            {
                const std::unique_ptr<Game> game = load(dir, "c1a4i", &globals);
                if (game == nullptr)
                    return;
                Game &g = *game;
                layMapState(fired.data(), fired.size(), g.logic, g.player, g.view.header->crc32);
                run(g, 1000);
                check(rocketFires(g, "fired at the first visit, laid back"),
                      "fired at the first visit and laid back, the rocket button stays unlocked");
            }
            // ... and with no state kept, only what was used up (the map
            // begun afresh, its multisources all off).
            {
                const std::unique_ptr<Game> game = load(dir, "c1a4i", &globals);
                if (game == nullptr)
                    return;
                Game &g = *game;
                mapRestore(slot, g.logic, g.actors);
                run(g, 1000);
                check(rocketFires(g, "begun afresh over what was used up"),
                      "begun afresh over what was used up, the rocket button is unlocked");
            }
        }
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || dir[0] == '\0')
    {
        std::printf("test_blastpit: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 77;
    }
    testLift(dir);
    testBucket(dir);
    testActors(dir);
    testRocketMaster(dir);
    if (g_failures == 0)
        std::printf("test_blastpit: ok\n");
    return g_failures == 0 ? 0 : 1;
}
