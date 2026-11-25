// Power Up's machinery, on its own maps: c2a1's gate, opened by the power
// switched on in c2a1a, a map the player reaches through c2a1b and comes
// back from.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

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

    /// Every record of `kind` called `name` (or, with `byTarget`, aimed at
    /// it).
    std::vector<int> recordsNamed(const Game &g, const char *name, LogicKind kind, bool byTarget = false)
    {
        std::vector<int> out;
        const int id = nameOf(g, name);
        for (std::uint32_t i = 0; i < g.view.logicCount && id != 0; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(kind) && (byTarget ? r.target : r.name) == id)
                out.push_back(static_cast<int>(i));
        }
        return out;
    }

    int recordNamed(const Game &g, const char *name, LogicKind kind, bool byTarget = false)
    {
        const std::vector<int> all = recordsNamed(g, name, kind, byTarget);
        return all.empty() ? -1 : all[0];
    }

    void run(Game &g, int ms)
    {
        for (int t = 0; t < ms; t += 33)
            g.frame();
    }

    /// c2a1's trigger_autos held back by c2a1_train_power: into the
    /// gargantua's scares, the dying guard's last words, and "restock".
    template <class F> void eachGatedAuto(Game &g, F f)
    {
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::TriggerAuto) &&
                g.view.logic[i].globalState != 0)
                f(static_cast<int>(i));
        }
    }

    /// Whether c2a1's gate (the two func_doors "restock", which slide
    /// apart) has opened, and its monstermaker gone to work.
    bool gateOpen(Game &g, const char *what)
    {
        const std::vector<int> doors = recordsNamed(g, "restock", LogicKind::Door);
        const int maker = recordNamed(g, "restock", LogicKind::Maker);
        check(doors.size() == 2 && maker >= 0, "c2a1 has its gate's two doors and its monstermaker");
        if (doors.size() != 2 || maker < 0)
            return false;
        int open = 0;
        for (const int d : doors)
            open += g.logic.states[d].move != MoveState::AtBottom || g.logic.states[d].phase != 0;
        const LogicState &m = g.logic.states[maker];
        const bool made = m.dead || m.running || m.phase != 0;
        std::printf("test_powerup: %s: %d of the gate's doors open, its monstermaker %s\n", what, open,
                    made ? "set going" : "IDLE");
        return open == 2 && made;
    }

    // c2a1a: the jam (func_breakable *14, aimed at p_mm1: a multisource and
    // a multi_manager of that one name) broken unlocks the generator's
    // button (*102, master p_mm1), which through p_mm2 switches on the
    // global c2a1_train_power (env_global "global1"). Back in c2a1, three
    // trigger_autos held back by it fire, one of them the gate's (issue
    // #18). The player comes to c2a1 first, with the global off.
    void testGate(const char *dir)
    {
        // 1. c2a1, first visit, nothing on; kept as the map is left, and as
        //    v0.4.2 would have kept it (the gated autos gone, unfired).
        std::vector<std::uint8_t> kept, keptOld;
        SaveMapState slot, slotOld;
        {
            Globals globals;
            const std::unique_ptr<Game> game = load(dir, "c2a1", &globals);
            check(game != nullptr, "c2a1 cooks");
            if (game == nullptr)
                return;
            Game &g = *game;
            run(g, 1000);
            int gated = 0, gone = 0;
            eachGatedAuto(g, [&](int i) {
                ++gated;
                gone += g.logic.states[i].dead;
            });
            std::printf("test_powerup: c2a1, no power: %d gated trigger_autos, %d gone\n", gated, gone);
            check(gated == 3 && gone == 0, "no power: c2a1's three gated trigger_autos wait, unfired");
            check(!gateOpen(g, "c2a1, no power"), "no power: c2a1's gate stays shut");
            kept = takeMapState(g.logic, g.player, g.view.header->crc32);
            mapRemember(slot, g.logic, g.actors);
            eachGatedAuto(g, [&](int i) { g.logic.states[i].dead = true; });
            keptOld = takeMapState(g.logic, g.player, g.view.header->crc32);
            mapRemember(slotOld, g.logic, g.actors);
        }
        // 2. c2a1a: the jam broken, left and come back to, the button
        //    pressed.
        Globals globals;
        {
            std::vector<std::uint8_t> broken;
            {
                const std::unique_ptr<Game> game = load(dir, "c2a1a", &globals);
                check(game != nullptr, "c2a1a cooks");
                if (game == nullptr)
                    return;
                Game &g = *game;
                run(g, 1000);
                const int button = recordNamed(g, "p_mm2", LogicKind::Button, true);
                const int jam = recordNamed(g, "p_mm1", LogicKind::Breakable, true);
                check(button >= 0 && jam >= 0, "c2a1a has the generator's button and its jam");
                if (button < 0 || jam < 0)
                    return;
                check(!logicMasterAllows(g.logic, button), "jammed: the generator's button is locked");
                logicStrike(g.logic, g.player, jam, 100000);
                run(g, 500);
                check(logicMasterAllows(g.logic, button), "the jam broken, the generator's button is unlocked");
                broken = takeMapState(g.logic, g.player, g.view.header->crc32);
            }
            const std::unique_ptr<Game> game = load(dir, "c2a1a", &globals);
            if (game == nullptr)
                return;
            Game &g = *game;
            check(layMapState(broken.data(), broken.size(), g.logic, g.player, g.view.header->crc32),
                  "c2a1a's state lays back");
            run(g, 1000);
            const int button = recordNamed(g, "p_mm2", LogicKind::Button, true);
            check(logicMasterAllows(g.logic, button), "left and come back to, the generator's button stays unlocked");
            logicUse(g.logic, g.player, button, -1, kUseSet);
            run(g, 3000);
            const int power = globals.get(nameHash("c2a1_train_power"));
            std::printf("test_powerup: c2a1a, the generator's button pressed: c2a1_train_power %d\n", power);
            check(power == kGlobalOn, "the generator's button switches the power on");
        }
        // 3. c2a1 again, power on: from the state kept, from a v0.4.2 save,
        //    and begun afresh over what was used up (both ways).
        for (int way = 0; way < 4; ++way)
        {
            static const char *const kWays[4] = {"come back", "come back, an old save", "begun afresh",
                                                 "begun afresh, an old save"};
            const std::unique_ptr<Game> game = load(dir, "c2a1", &globals);
            if (game == nullptr)
                return;
            Game &g = *game;
            if (way < 2)
            {
                const std::vector<std::uint8_t> &bytes = way == 0 ? kept : keptOld;
                check(layMapState(bytes.data(), bytes.size(), g.logic, g.player, g.view.header->crc32),
                      "c2a1's state lays back");
            }
            else
            {
                mapRestore(way == 2 ? slot : slotOld, g.logic, g.actors);
            }
            run(g, 6000);
            check(gateOpen(g, kWays[way]), "power on, c2a1's gate opens as the map is come back to");
            int gone = 0;
            eachGatedAuto(g, [&](int i) { gone += g.logic.states[i].dead; });
            check(gone == 3, "... and all three of its gated trigger_autos have fired");
        }
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || dir[0] == '\0')
    {
        std::printf("test_powerup: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 77;
    }
    testGate(dir);
    if (g_failures == 0)
        std::printf("test_powerup: ok\n");
    return g_failures == 0 ? 0 : 1;
}
