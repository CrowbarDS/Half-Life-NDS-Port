// The Blast Pit's machinery, on its own maps (c1a4f): the lift whose button is
// inside its car.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
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
    if (g_failures == 0)
        std::printf("test_blastpit: ok\n");
    return g_failures == 0 ? 0 : 1;
}
