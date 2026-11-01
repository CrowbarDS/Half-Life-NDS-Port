// Level version 39's entities: trigger_counter, trigger_changetarget,
// player_weaponstrip, player_loadsaved, trigger_endsection,
// trigger_monsterjump, trigger_camera, speaker, func_guntarget, button_target,
// env_blood, cycler_sprite; the three items; and the plants of Xen
// (HlXen.hpp).
//
// Each on a synthetic room, as the converter cooks it and the DS runs it (the
// speaker's lines from a game's folder made up here);
// and with HLDS_VALVE_DIR, on the game's own maps: c4a1's lights hide from
// the player and switch their light off, c4a2's trees strike, t0a0b's
// targets ride, c2a3e's camera looks.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "TestBspBuilder.hpp"
#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    std::string entity(const std::string &keys) { return "{\n" + keys + "}\n"; }

    BrushBox volume(const char *cls, float x0, float y0, float z0, float x1, float y1, float z1, std::string keys)
    {
        BrushBox box;
        box.className = cls;
        box.mins[0] = x0, box.mins[1] = y0, box.mins[2] = z0;
        box.maxs[0] = x1, box.maxs[1] = y1, box.maxs[2] = z1;
        box.keys = std::move(keys);
        return box;
    }

    std::unique_ptr<Game> cook(BoxRoom &room, const char *valve = nullptr)
    {
        auto g = std::make_unique<Game>();
        std::string error;
        g->ok = g->bsp.parse(buildBoxRoom(room), error);
        if (g->ok)
            finish(*g, nullptr, valve);
        return g;
    }

    std::unique_ptr<Game> map(const char *valve, const char *name)
    {
        auto g = std::make_unique<Game>();
        std::string error;
        if (!g->bsp.load(std::string(valve) + "/maps/" + name + ".bsp", error))
        {
            check(false, (std::string(valve) + "/maps/" + name + ".bsp loads").c_str());
            return g;
        }
        finish(*g, nullptr, valve);
        return g;
    }

    /// The record named `name`, of kind `kind`; -1 if none.
    int named(const Game &g, const char *name, LogicKind kind)
    {
        const std::uint32_t hash = nameHash(name);
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(kind) && r.name != 0 && r.name < g.view.nameCount &&
                g.view.nameHashes[r.name] == hash)
                return static_cast<int>(i);
        }
        return -1;
    }

    void use(Game &g, int record, int how = kUseToggle) { logicUse(g.logic, g.player, record, -1, how); }

    void ticks(Game &g, int n)
    {
        for (int i = 0; i < n; ++i)
            g.frame();
    }

    void standAt(Game &g, int x, int y, int z = 0)
    {
        g.player.origin[0] = fxFromInt(x);
        g.player.origin[1] = fxFromInt(y);
        g.player.origin[2] = fxFromInt(z) + kStandingFeet + fxFromInt(1);
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
    }

    void testCounterAndChange()
    {
        // A counter of three that strips the player; a relay whose target
        // a trigger_changetarget turns from one counter to another.
        BoxRoom room;
        room.extraEntities =
            entity("\"classname\" \"trigger_counter\"\n\"targetname\" \"three\"\n\"count\" \"3\"\n"
                   "\"target\" \"strip\"\n") +
            entity("\"classname\" \"player_weaponstrip\"\n\"targetname\" \"strip\"\n") +
            entity("\"classname\" \"trigger_relay\"\n\"targetname\" \"relay\"\n\"target\" \"first\"\n") +
            entity("\"classname\" \"trigger_counter\"\n\"targetname\" \"first\"\n\"count\" \"1\"\n") +
            entity("\"classname\" \"trigger_counter\"\n\"targetname\" \"second\"\n\"count\" \"1\"\n") +
            entity("\"classname\" \"trigger_changetarget\"\n\"targetname\" \"change\"\n\"target\" \"relay\"\n"
                   "\"m_iszNewTarget\" \"second\"\n");
        auto game = cook(room);
        check(game->ok, "the room of counters cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int three = named(g, "three", LogicKind::Counter);
        const int relay = named(g, "relay", LogicKind::TriggerRelay);
        const int first = named(g, "first", LogicKind::Counter), second = named(g, "second", LogicKind::Counter);
        const int change = named(g, "change", LogicKind::ChangeTarget);
        check(three >= 0 && g.view.logic[three].extra[0] == 3 && relay >= 0 && first >= 0 && second >= 0 &&
                  change >= 0 && named(g, "strip", LogicKind::WeaponStrip) >= 0,
              "a counter of 3, a strip, a relay and a change of its target are cooked");
        if (three < 0 || relay < 0 || first < 0 || second < 0 || change < 0)
            return;

        g.player.inv.have = static_cast<std::uint8_t>(1u | (1u << kWeaponGlock) | (1u << kWeaponShotgun));
        g.player.inv.current = kWeaponShotgun;
        g.player.inv.ammo9mm = 50;
        g.player.inv.armor = 40;
        use(g, three);
        use(g, three);
        check(!g.logic.states[three].dead && g.player.inv.ammo9mm == 50, "two uses of a counter of three: nothing");
        use(g, three);
        check(g.logic.states[three].dead, "... the third fires it, and it is spent");
        check(g.player.inv.have == 1u && g.player.inv.current == kWeaponNone && g.player.inv.ammo9mm == 0 &&
                  g.player.inv.armor == 40,
              "the strip takes every weapon and its ammunition, not the suit or its armour");

        use(g, change);
        use(g, relay);
        check(g.logic.states[second].dead && !g.logic.states[first].dead,
              "a relay whose target was changed fires the new one, not the old");
    }

    void testLostAndEnd()
    {
        BoxRoom room;
        room.brushes.push_back(volume("trigger_endsection", -120, -120, 0, -60, 120, 96, "\"section\" \"_oem_end_training\"\n"));
        room.brushes.push_back(volume("trigger_endsection", 60, -120, 0, 120, 120, 96,
                                      "\"targetname\" \"byname\"\n\"spawnflags\" \"1\"\n"));
        room.extraEntities = entity("\"classname\" \"player_loadsaved\"\n\"targetname\" \"lost\"\n"
                                    "\"duration\" \"1.5\"\n\"holdtime\" \"20\"\n\"renderamt\" \"255\"\n"
                                    "\"loadtime\" \"3\"\n");
        auto game = cook(room);
        check(game->ok, "the room of endings cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int lost = named(g, "lost", LogicKind::LoadSaved);
        check(lost >= 0 && g.view.logic[lost].extra[0] == 1500 && g.view.logic[lost].extra[1] == 3000,
              "a player_loadsaved: its fade and when it loads");
        if (lost < 0)
            return;
        standAt(g, 0, 0);
        use(g, lost);
        check(g.logic.fading && g.logic.reloadMs == 3000, "... used, the screen fades and the load is three seconds off");
        ticks(g, 30);
        check(g.logic.reloadMs > 0, "... not before");
        ticks(g, 70);
        check(g.logic.reloadMs == 0, "... and then it is the caller's to load the last save");

        ticks(g, 2);
        check(!g.logic.endSection, "away from the end: nothing");
        standAt(g, 90, 0);
        ticks(g, 2);
        check(!g.logic.endSection, "... nor walking into one that only its name sets off");
        standAt(g, -90, 0);
        ticks(g, 2);
        check(g.logic.endSection, "walking into the end of the training: the game goes back to its title");
    }

    void testCamera()
    {
        // A camera that looks at a wall that its end then switches off.
        BoxRoom room;
        room.mins[0] = -512, room.maxs[0] = 512;
        room.brushes.push_back(volume("func_wall_toggle", 300, -16, 0, 332, 16, 64, "\"targetname\" \"look\"\n"));
        room.extraEntities = entity("\"classname\" \"trigger_camera\"\n\"targetname\" \"cam\"\n\"target\" \"look\"\n"
                                    "\"origin\" \"-300 0 100\"\n\"wait\" \"2\"\n\"spawnflags\" \"4\"\n");
        auto game = cook(room);
        check(game->ok, "the room of the camera cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int cam = named(g, "cam", LogicKind::Camera);
        const int wall = named(g, "look", LogicKind::WallToggle);
        check(cam >= 0 && wall >= 0 && g.view.logic[cam].extra[0] == 4 && g.view.logic[cam].waitMs == 2000,
              "a trigger_camera: its place, what it looks at, for two seconds, the player held");
        if (cam < 0 || wall < 0)
            return;
        standAt(g, 0, 0);
        use(g, cam);
        check(g.logic.camera == cam && g.logic.states[cam].base[0] == fxFromInt(-300) &&
                  g.logic.states[cam].base[2] == fxFromInt(100),
              "... used, the view is from it");
        fx at[3];
        check(placeOfName(g.logic, g.view.logic[cam].target, at) && at[0] > fxFromInt(300),
              "... looking at the wall it names");
        ticks(g, 30);
        check(g.logic.camera == cam, "... for its time");
        const bool wasOn = g.brushStates[g.view.logic[wall].brush].active;
        ticks(g, 40);
        check(g.logic.camera < 0 && g.brushStates[g.view.logic[wall].brush].active != wasOn,
              "... and then the view is the player's again, and what it looked at is fired");
    }

    void testGunAndButtonTargets()
    {
        BoxRoom room;
        room.mins[0] = -512, room.maxs[0] = 512;
        room.brushes.push_back(volume("func_guntarget", -16, -16, 32, 16, 16, 64,
                                      "\"targetname\" \"gun\"\n\"target\" \"c1\"\n\"health\" \"10\"\n"
                                      "\"speed\" \"100\"\n\"message\" \"done\"\n"));
        room.brushes.push_back(volume("button_target", 200, -16, 0, 216, 16, 32, "\"target\" \"pressed\"\n"));
        room.extraEntities =
            entity("\"classname\" \"path_corner\"\n\"targetname\" \"c1\"\n\"origin\" \"0 0 48\"\n\"target\" \"c2\"\n") +
            entity("\"classname\" \"path_corner\"\n\"targetname\" \"c2\"\n\"origin\" \"300 0 48\"\n") +
            entity("\"classname\" \"trigger_counter\"\n\"targetname\" \"done\"\n\"count\" \"1\"\n") +
            entity("\"classname\" \"trigger_counter\"\n\"targetname\" \"pressed\"\n\"count\" \"1\"\n");
        auto game = cook(room);
        check(game->ok, "the room of targets cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int gun = named(g, "gun", LogicKind::GunTarget);
        const int done = named(g, "done", LogicKind::Counter), pressed = named(g, "pressed", LogicKind::Counter);
        const int button = g.recordOfKind(LogicKind::ButtonTarget);
        check(gun >= 0 && done >= 0 && button >= 0 && pressed >= 0 && g.view.logic[gun].extra[0] == 10,
              "a func_guntarget of 10 on a path, and a button_target");
        if (gun < 0 || done < 0 || button < 0 || pressed < 0)
            return;
        standAt(g, -400, 100); // (out of its way: a train that meets the player waits)
        const fx start = g.brushStates[g.view.logic[gun].brush].pos[0];
        logicStrike(g.logic, g.player, gun, 50);
        check(!g.logic.states[done].dead, "a target that is not riding takes no hits");
        use(g, gun);
        ticks(g, 30);
        check(g.brushStates[g.view.logic[gun].brush].pos[0] > start, "... used, it rides its path");
        logicStrike(g.logic, g.player, gun, 6);
        check(g.logic.states[gun].running && !g.logic.states[done].dead, "... hit for 6 of its 10, it rides on");
        logicStrike(g.logic, g.player, gun, 6);
        check(!g.logic.states[gun].running && g.logic.states[done].dead, "... and for 12, it stops and fires its message");

        const BrushState &face = g.brushStates[g.view.logic[button].brush];
        logicStrike(g.logic, g.player, button, 1);
        ticks(g, 1);
        check(face.frame == 1 && g.logic.states[pressed].dead, "a button_target hit shows its other face and fires");
        use(g, button);
        check((g.logic.states[button].bits & 1u) == 1u, "... and only a shot turns it, not a use (no flag 1)");
    }

    void testBloodAndSprite()
    {
        BoxRoom room;
        room.extraEntities = entity("\"classname\" \"env_blood\"\n\"targetname\" \"blood\"\n\"color\" \"1\"\n"
                                    "\"amount\" \"200\"\n\"spawnflags\" \"9\"\n\"origin\" \"0 0 64\"\n");
        auto game = cook(room);
        check(game->ok, "the room of blood cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        const int blood = named(g, "blood", LogicKind::Blood);
        check(blood >= 0 && g.view.logic[blood].extra[0] == 1, "an env_blood, yellow");
        if (blood < 0)
            return;
        use(g, blood);
        int bursts = 0;
        for (const Effect &effect : g.actors.effects)
            bursts += effect.kind == kEffectBlood && effect.size == 1;
        check(bursts > 0, "... used, a burst of yellow blood");
    }

    /// A speaker: `pa`'s announcements, now and then, until switched off.
    void speaks(Game &g)
    {
        const int pa = named(g, "pa", LogicKind::Speaker);
        check(pa >= 0 && g.view.logic[pa].auxCount > 1, "a speaker has the announcements of its group");
        if (pa >= 0)
        {
            ticks(g, 16 * 30);
            const std::size_t first = g.said.size();
            check(first >= 1, "... one is said within the first 15 seconds");
            use(g, pa);
            ticks(g, 140 * 30);
            check(g.said.size() == first, "... and none once it is switched off");
        }
    }

    /// The same, from lists made up here: which lines it has, that they are
    /// what it says, and the ways of naming them. (The real group's size is
    /// the game's, so here it is said exactly.)
    void testSpeaker()
    {
        FakeGame fake("round39",
                      "// a comment\n"
                      "C1A0_0 vox/one two\n"
                      "C1A0_1 vox/three\n"
                      "C1A0_2 vox/four five six\n"
                      "C1A1_ALONE vox/seven\n"
                      "OWN_0 vox/eight\n"
                      "OWN_5 vox/nine\n",
                      "");
        BoxRoom room;
        room.extraEntities = entity("\"classname\" \"speaker\"\n\"targetname\" \"pa\"\n\"preset\" \"1\"\n"
                                    "\"health\" \"5\"\n\"origin\" \"0 0 64\"\n");
        auto game = cook(room, fake.dir.c_str());
        check(game->ok, "the room of the speaker cooks");
        if (!game->ok)
            return;
        speaks(*game);
        Game &g = *game;
        const int pa = named(g, "pa", LogicKind::Speaker);
        if (pa < 0)
            return;
        const LogicRecord &r = g.view.logic[pa];
        check(r.auxCount == 3, "... all three of its group");
        check(r.extra[0] == 64, "... half as loud as its health of 5 says");
        bool its = !g.said.empty();
        for (const std::uint16_t line : g.said)
        {
            bool found = false;
            for (int n = 0; n < r.auxCount; ++n)
                found = found || g.view.logicAux[r.firstAux + n].target == line;
            its = its && found;
        }
        check(its, "... and what it says is of them");
        check(g.view.sentences[g.view.logicAux[r.firstAux].target].wordCount == 2 &&
                  g.view.sentences[g.view.logicAux[r.firstAux + 2].target].wordCount == 3,
              "... each of its own words");

        // One line, after a '!'; a map's own group, past a gap in its numbers;
        // a group nobody has; none said until it is switched on.
        BoxRoom more;
        more.extraEntities = entity("\"classname\" \"speaker\"\n\"targetname\" \"alone\"\n\"message\" \"!C1A1_ALONE\"\n"
                                    "\"spawnflags\" \"1\"\n\"origin\" \"0 0 64\"\n") +
                             entity("\"classname\" \"speaker\"\n\"targetname\" \"own\"\n\"message\" \"OWN_\"\n"
                                    "\"spawnflags\" \"1\"\n\"origin\" \"0 0 64\"\n") +
                             entity("\"classname\" \"speaker\"\n\"targetname\" \"nobody\"\n\"message\" \"NOBODY_\"\n"
                                    "\"origin\" \"0 0 64\"\n");
        auto second = cook(more, fake.dir.c_str());
        check(second->ok, "the room of the other speakers cooks");
        if (!second->ok)
            return;
        Game &h = *second;
        const int alone = named(h, "alone", LogicKind::Speaker), own = named(h, "own", LogicKind::Speaker);
        check(alone >= 0 && h.view.logic[alone].auxCount == 1, "a speaker of one line, after a '!'");
        check(own >= 0 && h.view.logic[own].auxCount == 1, "... of its own group, as far as the numbers run on");
        check(named(h, "nobody", LogicKind::Speaker) < 0, "... and one with no lines is not kept");
        if (alone < 0)
            return;
        ticks(h, 20 * 30);
        check(h.said.empty(), "one that starts off says nothing");
        use(h, alone);
        ticks(h, 10);
        check(h.said.size() == 1 && h.said[0] == h.view.logicAux[h.view.logic[alone].firstAux].target,
              "... and switched on, says its line straight away");
    }

    void testWithTheGame(const char *valve)
    {
        // Speakers: the game's announcements, now and then, until switched off.
        {
            BoxRoom room;
            room.extraEntities = entity("\"classname\" \"speaker\"\n\"targetname\" \"pa\"\n\"preset\" \"1\"\n"
                                        "\"health\" \"10\"\n\"origin\" \"0 0 64\"\n");
            auto game = cook(room, valve);
            if (game->ok)
                speaks(*game);
        }
        // A zombie walks into a trigger_monsterjump: up it goes.
        {
            BoxRoom room;
            room.mins[0] = -512, room.maxs[0] = 512;
            room.brushes.push_back(volume("trigger_monsterjump", -64, -64, 0, 64, 64, 96, "\"angle\" \"0\"\n"));
            room.extraEntities = entity("\"classname\" \"monster_zombie\"\n\"origin\" \"0 0 1\"\n\"angle\" \"0\"\n");
            auto game = cook(room, valve);
            if (game->ok && game->actors.count > 0)
            {
                Game &g = *game;
                standAt(g, 400, 0);
                ticks(g, 6);
                check(g.actors.states[0].velocity[2] > 0 || g.actors.states[0].pos[2] > fxFromInt(4),
                      "a zombie in a trigger_monsterjump is thrown up");
            }
        }
        // The air tank and the antidote.
        {
            BoxRoom room;
            room.extraEntities = entity("\"classname\" \"item_airtank\"\n\"origin\" \"0 0 8\"\n") +
                                 entity("\"classname\" \"item_antidote\"\n\"origin\" \"64 0 8\"\n");
            auto game = cook(room, valve);
            if (game->ok && game->actors.count >= 2)
            {
                Game &g = *game;
                Arsenal more;
                g.player.arsenal = &more;
                int tank = -1, antidote = -1;
                for (int i = 0; i < g.actors.count; ++i)
                {
                    if (g.actors.states[i].item == static_cast<std::uint8_t>(ItemKind::Airtank))
                        tank = i;
                    if (g.actors.states[i].item == static_cast<std::uint8_t>(ItemKind::Antidote))
                        antidote = i;
                }
                check(tank >= 0 && antidote >= 0, "an air tank and an antidote lie in the room");
                if (tank >= 0 && antidote >= 0)
                {
                    g.player.underMs = 9000;
                    check(itemPickUp(g.logic, g.player, tank) && g.player.underMs == 0 && g.actors.states[tank].active,
                          "the air tank gives all the air there is, and stays");
                    g.player.underMs = 5000;
                    check(!itemPickUp(g.logic, g.player, tank), "... not again at once");
                    standAt(g, -100, 0); // (off it: walked over, it is taken again)
                    ticks(g, 31 * 30);
                    check(itemPickUp(g.logic, g.player, tank), "... but half a minute on");
                    check(itemPickUp(g.logic, g.player, antidote) && more.antidotes == 1 &&
                              !g.actors.states[antidote].active,
                          "an antidote is one more, taken");
                }
                g.player.arsenal = nullptr;
            }
        }
        // c4a1: walk into a light, and it hides, and its light goes out; go
        // away, and five seconds on it comes up and lights it again.
        if (auto game = map(valve, "c4a1"); game->ok)
        {
            Game &g = *game;
            int light = -1;
            for (int i = 0; i < g.actors.count && light < 0; ++i)
            {
                const ActorState &a = g.actors.states[i];
                if (a.active && a.kind == static_cast<std::uint8_t>(ActorKind::Xen) &&
                    a.item == static_cast<std::uint8_t>(XenPlant::Light) && a.fires != 0)
                    light = i;
            }
            check(light >= 0, "c4a1 has its lights, each switching a light");
            if (light >= 0)
            {
                const ActorState &plant = g.actors.states[light];
                int lit = -1;
                for (std::uint32_t r = 0; r < g.view.logicCount && lit < 0; ++r)
                {
                    if (g.view.logic[r].name == plant.fires && g.view.logic[r].kind == static_cast<std::uint8_t>(LogicKind::Light))
                        lit = static_cast<int>(r);
                }
                g.player.origin[0] = plant.pos[0] + fxFromInt(40);
                g.player.origin[1] = plant.pos[1];
                g.player.origin[2] = plant.pos[2] + fxFromInt(36);
                for (int i = 0; i < 90 && plant.attack != 2; ++i)
                {
                    logicTick(g.logic, g.player, 33);
                    monstersTick(g.logic, g.player, 33);
                    g.drain();
                }
                check(plant.attack == 2 && plant.reach == 0, "... walked into, it draws in and its glow goes");
                // (A light's bits: 1 dark.)
                check(lit < 0 || (g.logic.states[lit].bits & 1u) == 1u, "... and the light it casts is switched off");
                g.player.origin[0] = plant.pos[0] + fxFromInt(800);
                for (int i = 0; i < 9 * 30 && plant.attack != 0; ++i)
                {
                    logicTick(g.logic, g.player, 33);
                    monstersTick(g.logic, g.player, 33);
                    g.drain();
                }
                check(plant.attack == 0 && plant.reach == 255, "... left alone, it comes up again and shines");
                check(lit < 0 || (g.logic.states[lit].bits & 1u) == 0, "... and its light is on again");
            }
        }
        // c4a2: stand in front of a tree, and it strikes.
        if (auto game = map(valve, "c4a2"); game->ok)
        {
            Game &g = *game;
            int tree = -1;
            for (int i = 0; i < g.actors.count && tree < 0; ++i)
            {
                const ActorState &a = g.actors.states[i];
                if (a.active && a.kind == static_cast<std::uint8_t>(ActorKind::Xen) &&
                    a.item == static_cast<std::uint8_t>(XenPlant::Tree))
                    tree = i;
            }
            check(tree >= 0, "c4a2 has its trees");
            if (tree >= 0)
            {
                const ActorState &plant = g.actors.states[tree];
                fx sine, cosine;
                fxSinCos(plant.yaw, sine, cosine);
                g.player.health = 100;
                for (int i = 0; i < 120 && g.player.health == 100; ++i)
                {
                    g.player.origin[0] = plant.pos[0] + fxMul(cosine, fxFromInt(64));
                    g.player.origin[1] = plant.pos[1] + fxMul(sine, fxFromInt(64));
                    g.player.origin[2] = plant.pos[2] + fxFromInt(36);
                    logicTick(g.logic, g.player, 33);
                    monstersTick(g.logic, g.player, 33);
                    g.drain();
                }
                check(g.player.health == 100 - kXenTreeDamage, "... in front of one, it strikes: 25");
            }
        }
        // What the game's maps hold of the rest.
        struct Expect
        {
            const char *map;
            LogicKind kind;
            int least;
        };
        static const Expect kExpect[] = {
            {"t0a0b", LogicKind::GunTarget, 7}, {"c2a3e", LogicKind::Camera, 1}, {"t0a0d", LogicKind::EndSection, 1},
            {"c2a4e", LogicKind::LoadSaved, 1}, {"c2a3d", LogicKind::WeaponStrip, 1}, {"c2a1", LogicKind::MonsterJump, 2},
            {"c1a3", LogicKind::Speaker, 1},    {"c4a2b", LogicKind::Blood, 1}};
        for (const Expect &want : kExpect)
        {
            auto game = map(valve, want.map);
            int count = 0;
            for (std::uint32_t i = 0; game->ok && i < game->view.logicCount; ++i)
                count += game->view.logic[i].kind == static_cast<std::uint8_t>(want.kind);
            check(count >= want.least, (std::string(want.map) + " has its kinds of round 39").c_str());
        }
        if (auto game = map(valve, "c2a2d"); game->ok)
        {
            int cyclers = 0;
            for (std::uint32_t i = 0; i < game->view.logicCount; ++i)
                cyclers += game->view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Sprite) &&
                           (game->view.logic[i].extra[3] & 4) != 0;
            check(cyclers >= 2, "c2a2d's cycler_sprites are its steam, always there");
        }
    }
} // namespace

int main()
{
    testCounterAndChange();
    testLostAndEnd();
    testCamera();
    testGunAndButtonTargets();
    testBloodAndSprite();
    testSpeaker();
    const char *valve = std::getenv("HLDS_VALVE_DIR");
    if (valve != nullptr && *valve != '\0')
        testWithTheGame(valve);
    else
        std::printf("test_round39: HLDS_VALVE_DIR not set, the game's maps were not read\n");
    if (g_failures == 0)
        std::printf("test_round39: ok\n");
    return g_failures == 0 ? 0 : 1;
}
