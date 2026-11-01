// A map's whole state, saved and laid back (shared/HlSaveState.hpp).
//
// The test that matters is one that does not know what there is to forget:
// a real map is played for a while with things made to happen in it, its
// state taken; the same map begun afresh has that state laid over it; and
// then BOTH are played on, the same way, and must be the same to the byte
// when they are done. Whatever was not saved -- a door's timer, a monster's
// grudge, a queue of things due -- shows as the two parting company.

#include <algorithm>
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
    std::unique_ptr<Game> load(const char *dir, const std::string &map, Globals *globals)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
            return nullptr;
        finish(*game, globals, dir);
        return game->ok ? std::move(game) : nullptr;
    }

    /// One tick, with something made to happen every so often -- the same
    /// for whoever is given the same `tick`: the player put at one of the
    /// map's nodes after another (so that what waits for them is walked
    /// into, and what hunts them hunts), and now and then something the
    /// map names set off (a door, a scene, a chain of things).
    void play(Game &g, int tick, WeaponState &weapon)
    {
        std::uint32_t chance = static_cast<std::uint32_t>(tick) * 2654435761u;
        if (tick % 20 == 0 && g.actors.nodeCount > 0)
        {
            const PathNode &node = g.actors.nodes[(tick / 20 * 7) % g.actors.nodeCount];
            g.player.origin[0] = node.pos[0];
            g.player.origin[1] = node.pos[1];
            g.player.origin[2] = node.pos[2] + fxFromInt(37);
            for (fx &v : g.player.velocity)
                v = 0;
        }
        if (tick % 45 == 30 && g.view.nameCount > 1)
        {
            const std::uint16_t name = static_cast<std::uint16_t>(1 + (chance >> 8) % (g.view.nameCount - 1));
            logicFire(g.logic, g.player, name, -1, kUseToggle, 0);
        }
        g.player.health = 100; // (it is about the map: they are not let die)
        // The trigger held now and then: rounds, marks on the walls, things hurt.
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
        fx sine = 0, cosine = 0;
        fxSinCos(static_cast<std::uint16_t>(tick * 911), sine, cosine);
        const fx forward[3] = {cosine, sine, 0}, right[3] = {sine, -cosine, 0}, up[3] = {0, 0, kFxOne};
        g.player.inv.have = 0xFF;
        g.player.inv.current = tick % 400 < 200 ? kWeaponGlock : kWeaponGrenade;
        g.player.inv.clipGlock = 17;
        g.player.inv.grenades = 5;
        weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, tick % 90 < 30, false, 33);
        weapon.anim = nullptr;
        g.frame();
    }

    template <class T>
    std::size_t sizeOf(T &thing)
    {
        return stateBytes(thing);
    }
    /// Which field of `thing` byte `at` of its written form is in.
    template <class T>
    int fieldAt(T &thing, std::size_t at)
    {
        int field = 0, found = -1;
        std::size_t sum = 0;
        eachField(thing, [&](auto &f)
                  {
                      const std::size_t size = stateBytes(f);
                      if (found < 0 && at < sum + size)
                          found = field;
                      sum += size;
                      ++field;
                  });
        return found;
    }
    /// Says where in a map's state byte `at` (past the heading) is.
    void locate(Game &g, std::size_t at)
    {
        at -= sizeof(hldata::detail::StateHeading);
        LogicWorld &w = g.logic;
        std::size_t size = sizeOf(w);
        if (at < size)
        {
            std::printf("  in the logic world's own field %d\n", fieldAt(w, at));
            return;
        }
        at -= size;
        auto part = [&](const char *what, std::size_t each, int count) -> bool
        {
            if (each == 0 || at >= each * static_cast<std::size_t>(count))
            {
                at -= each * static_cast<std::size_t>(count);
                return false;
            }
            std::printf("  in %s %zu, byte %zu of its %zu\n", what, at / each, at % each, each);
            return true;
        };
        if (w.recordCount > 0 && part("logic state", sizeOf(w.states[0]), w.recordCount))
        {
            const std::size_t i = at / sizeOf(w.states[0]);
            std::printf("  (record kind %d, state field %d)\n", g.view.logic[i].kind, fieldAt(w.states[i], at % sizeOf(w.states[0])));
            return;
        }
        if (w.brushCount > 0 && part("brush state", sizeOf(w.brushStates[0]), w.brushCount))
            return;
        if (w.brushCount > 0 && part("mover", sizeOf(w.movers[0]), w.brushCount))
            return;
        if (part("corner state", 1, w.cornerCount))
            return;
        if (w.world->ladderCount > 0 && part("ladder", sizeOf(w.ladders[0]), w.world->ladderCount))
            return;
        size = sizeOf(g.actors);
        if (at < size)
        {
            std::printf("  in the actor world's own field %d\n", fieldAt(g.actors, at));
            return;
        }
        at -= size;
        if (g.actors.count > 0 && part("actor", sizeOf(g.actors.states[0]), g.actors.count))
        {
            const std::size_t i = at / sizeOf(g.actors.states[0]);
            std::printf("  (actor kind %d, field %d)\n", g.actors.states[i].kind,
                        fieldAt(g.actors.states[i], at % sizeOf(g.actors.states[0])));
            return;
        }
        if (part("link", 1, g.actors.linkCount))
            return;
        std::printf("  in the player's field %d\n", fieldAt(g.player, at));
    }

    void roundTrip(const char *dir, const std::string &map, int before, int after)
    {
        Globals globalsA, globalsB;
        std::unique_ptr<Game> a = load(dir, map, &globalsA);
        if (a == nullptr)
        {
            std::printf("test_savestate: %s does not cook here: left out\n", map.c_str());
            return;
        }
        const std::uint32_t level = a->view.header->crc32;
        WeaponState weaponA, weaponB;
        for (int t = 0; t < before; ++t)
            play(*a, t, weaponA);
        std::vector<std::uint8_t> saved = takeMapState(a->logic, a->player, level);
        appendState(saved, weaponA);
        const std::size_t withoutMinds = saved.size();
        aiAppendState(saved, a->actors); // (what the monsters think: HlAiSave.hpp)

        // Begun afresh, and the state laid over it.
        std::unique_ptr<Game> b = load(dir, map, &globalsB);
        check(b != nullptr, "the map cooks a second time");
        if (b == nullptr)
            return;
        globalsB = globalsA; // (the switches that span maps are the save's own business: HlSave.hpp)
        const std::size_t mapBytes = withoutMinds - stateBytes(weaponB);
        const bool laid = layMapState(saved.data(), mapBytes, b->logic, b->player, level);
        std::size_t at = mapBytes;
        const bool rest = readState(saved.data(), saved.size(), at, weaponB) && at == withoutMinds &&
                          aiLayState(saved.data() + at, saved.size() - at, b->actors);
        check(laid && rest, (map + ": its state can be laid over the map begun afresh").c_str());
        if (!laid)
            return;
        std::vector<std::uint8_t> again = takeMapState(b->logic, b->player, level);
        appendState(again, weaponB);
        aiAppendState(again, b->actors);
        check(again == saved, (map + ": laid back, it is what was taken").c_str());

        // Both played on, the same way.
        int parted = -1;
        for (int t = before; t < before + after && parted < 0; ++t)
        {
            play(*a, t, weaponA);
            play(*b, t, weaponB);
            {
                std::vector<std::uint8_t> x = takeMapState(a->logic, a->player, level), y = takeMapState(b->logic, b->player, level);
                aiAppendState(x, a->actors);
                aiAppendState(y, b->actors);
                if (x != y)
                    parted = t;
            }
        }
        int moving = 0, alive = 0, queued = 0;
        for (int i = 0; i < a->logic.recordCount; ++i)
            moving += a->logic.states[i].move == MoveState::GoingUp || a->logic.states[i].move == MoveState::GoingDown ||
                              a->logic.states[i].running
                          ? 1
                          : 0;
        for (int i = 0; i < a->actors.count; ++i)
            alive += a->actors.states[i].active ? 1 : 0;
        for (const LogicEvent &event : a->logic.events)
            queued += event.active ? 1 : 0;
        std::printf("test_savestate: %-7s %6zu bytes; at the save: %d things moving or running, %d due, %d actors there%s\n",
                    map.c_str(), saved.size(), moving, queued, alive, parted < 0 ? "" : "  <-- PARTED");
        if (parted >= 0)
        {
            // Where: which part of the state first differs.
            const std::vector<std::uint8_t> x = takeMapState(a->logic, a->player, level);
            const std::vector<std::uint8_t> y = takeMapState(b->logic, b->player, level);
            std::size_t first = 0;
            while (first < x.size() && first < y.size() && x[first] == y[first])
                ++first;
            std::printf("test_savestate: %s parted by tick %d; first difference at byte %zu of %zu\n", map.c_str(), parted,
                        first, x.size());
            locate(*a, first);
        }
        check(parted < 0, (map + ": played on from the save, it goes as it would have gone").c_str());
    }

    void refusals(const char *dir)
    {
        Globals globals;
        std::unique_ptr<Game> a = load(dir, "c1a0", &globals);
        std::unique_ptr<Game> other = load(dir, "c1a1", &globals);
        if (a == nullptr || other == nullptr)
            return;
        const std::uint32_t level = a->view.header->crc32;
        std::vector<std::uint8_t> saved = takeMapState(a->logic, a->player, level);
        check(!layMapState(saved.data(), saved.size(), other->logic, other->player, other->view.header->crc32),
              "a state is not laid over another map");
        check(!layMapState(saved.data(), saved.size(), a->logic, a->player, level ^ 1),
              "... nor over the same map cooked otherwise");
        check(!layMapState(saved.data(), saved.size() - 1, a->logic, a->player, level) &&
                  !layMapState(saved.data(), 10, a->logic, a->player, level),
              "... nor if it is not all there");
        saved[5] ^= 0x40; // (its fingerprint of the layout)
        check(!layMapState(saved.data(), saved.size(), a->logic, a->player, level),
              "... nor if it was written by a build that lays things out otherwise");

        // A state as save version 6 wrote it, without the fields added since
        // for games between consoles: laid over a map begun afresh, with
        // those left as they began.
        std::vector<std::uint8_t> older = takeMapState(a->logic, a->player, level, true);
        std::vector<std::uint8_t> current = takeMapState(a->logic, a->player, level);
        check(older.size() < current.size() && older != current, "a state of save version 6 is shorter");
        std::unique_ptr<Game> fresh = load(dir, "c1a0", &globals);
        if (fresh == nullptr)
            return;
        fresh->logic.sharing = true;
        fresh->logic.struckCount = 3;
        fresh->logic.struck[0] = 7;
        check(layMapState(older.data(), older.size(), fresh->logic, fresh->player, level),
              "a state of save version 6 is laid over the map");
        check(fresh->logic.sharing && fresh->logic.struckCount == 3 && fresh->logic.struck[0] == 7,
              "... and the fields added since keep what they had");
        check(takeMapState(fresh->logic, fresh->player, level, true) == older,
              "... and the rest is what it said");
        older[5] ^= 0x40;
        check(!layMapState(older.data(), older.size(), fresh->logic, fresh->player, level),
              "... but not one with a layout of neither");
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || dir[0] == '\0')
    {
        std::printf("test_savestate: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 0;
    }
    refusals(dir);
    // (HLDS_STATE_MAPS: other maps than these, comma separated.)
    const char *asked = std::getenv("HLDS_STATE_MAPS");
    const std::string list = asked != nullptr ? asked : "c0a0,c1a0,c1a0c,c1a1,c1a1a,c1a2,c1a3,c2a2,c2a5";
    for (std::size_t at = 0; at < list.size();)
    {
        const std::size_t comma = std::min(list.find(',', at), list.size());
        roundTrip(dir, list.substr(at, comma - at), 400, 400);
        at = comma + 1;
    }
    if (g_failures == 0)
        std::printf("test_savestate: ok\n");
    return g_failures == 0 ? 0 : 1;
}
