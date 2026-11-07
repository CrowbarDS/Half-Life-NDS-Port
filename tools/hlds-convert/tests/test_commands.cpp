// The COMMAND line (shared/HlCommands.hpp) and the cheats it switches on:
// what a typed line is read as, by the game's own names; and, with
// HLDS_VALVE_DIR set, on the game's maps, that god keeps the player whole,
// that notarget is let alone, that a barnacle grabbing someone with
// barnaclekill on dies of it, that killall kills (each in its own death),
// and that sv_gravity pulls as hard as it says.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "HlCommands.hpp"
#include "TestGame.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    void testParse()
    {
        CommandLine line = parseCommand("  god ");
        check(line.command == Command::God && line.argc == 0, "a word alone, spaces round it");
        line = parseCommand("map c1a0");
        check(line.command == Command::Map && line.argc == 1 && std::strcmp(line.args[0], "c1a0") == 0,
              "a word and what it is given");
        check(parseCommand("changelevel c1a1").command == Command::ChangeLevel, "changelevel is its own");
        line = parseCommand("ChangeLevel C1A1");
        check(line.command == Command::ChangeLevel && std::strcmp(line.args[0], "c1a1") == 0,
              "... in capitals too, and the map's name in small letters");
        check(parseCommand("impulse 101").command == Command::GiveAll, "impulse 101 is everything");
        check(parseCommand("impulse 100").command == Command::Flashlight, "impulse 100 the flashlight");
        check(parseCommand("impulse 7").command == Command::Unknown, "... and no other impulse is anything");
        check(parseCommand("give").command == Command::GiveAll, "give alone gives everything");
        line = parseCommand("give weapon_crossbow");
        check(line.command == Command::Give && itemOfName(line.args[0]) == ItemKind::Crossbow, "give one thing");
        check(parseCommand("GOD").command == Command::God, "capitals are as small letters");
        // Which are cheats: what the game lets through only with sv_cheats.
        {
            const char *const cheats[] = {"god", "notarget", "noclip", "give", "give weapon_crossbow", "impulse 101",
                                          "heal", "health 50", "killall", "barnaclekill", "sv_gravity 400",
                                          "host_timescale 0.5"};
            bool all = true;
            for (const char *typed : cheats)
                all = all && commandIsCheat(parseCommand(typed).command);
            check(all, "god, notarget, noclip, give, impulse 101, heal, health, killall, barnaclekill, "
                       "sv_gravity and host_timescale are cheats");
            const char *const free[] = {"help", "god help", "fov 90", "crosshair 0", "save", "load", "map c1a0",
                                        "changelevel c1a1", "restart", "kill", "impulse 100", "impulse 201"};
            bool none = true;
            for (const char *typed : free)
                none = none && !commandIsCheat(parseCommand(typed).command);
            check(none, "help, fov, crosshair, save, load, map, changelevel, restart, kill and the flashlight are not");
        }
        line = parseCommand("killall   monster_barnacle");
        check(line.command == Command::KillAll && kindOfName(line.args[0]) == ActorKind::Barnacle,
              "killall, by the maps' class name");
        check(kindOfName("barnacle") == ActorKind::Barnacle && kindOfName("zombie") == ActorKind::Zombie &&
                  kindOfName("monster_human_grunt") == ActorKind::Grunt && kindOfName("nothing") == ActorKind::Count,
              "... or a short one; and nothing else");
        // (A weaponbox's word: its kind, and where its contents are.)
        check(itemWordOk(static_cast<std::uint32_t>(ItemKind::Weaponbox) | (5u << kItemStringShift), 10) &&
                  !itemWordOk(static_cast<std::uint32_t>(ItemKind::Weaponbox) | (50u << kItemStringShift), 10) &&
                  !itemWordOk(static_cast<std::uint32_t>(ItemKind::Rpg) | (1u << kItemStringShift), 10) &&
                  itemWordOk(static_cast<std::uint32_t>(ItemKind::Rpg), 0) &&
                  !itemWordOk(static_cast<std::uint32_t>(ItemKind::Count), 10),
              "an item's word: a weaponbox points into the strings, nothing else does");
        check(parseCommand("frobnicate").command == Command::Unknown && parseCommand("").command == Command::Unknown,
              "what is not a command is not one");
        line = parseCommand("a b c d e");
        check(line.argc == 3, "no more than three words after the first");

        int q8 = 0;
        check(parseNumber("800", q8) && q8 == 800 * 256, "a number");
        check(parseNumber("0.5", q8) && q8 == 128, "a fraction");
        check(parseNumber("-3", q8) && q8 == -3 * 256, "below nothing");
        check(parseNumber("1.25", q8) && q8 == 320, "... and both");
        check(!parseNumber("", q8) && !parseNumber("abc", q8) && !parseNumber("1.2.3", q8) && !parseNumber("-", q8),
              "and nothing that is not one");

        // Every thing the maps hold can be given, by its name there.
        const char *const kNames[] = {"weapon_crowbar",  "weapon_9mmhandgun", "weapon_shotgun",  "weapon_357",
                                      "weapon_9mmar",    "weapon_handgrenade", "weapon_crossbow", "weapon_satchel",
                                      "weapon_tripmine", "weapon_snark",      "ammo_9mmclip",    "ammo_buckshot",
                                      "ammo_357",        "ammo_9mmar",        "ammo_argrenades", "ammo_crossbow",
                                      "item_battery",    "item_healthkit",    "item_suit",       "weapon_rpg",
                                      "ammo_rpgclip",    "weapon_gauss",      "ammo_gaussclip",  "weapon_egon",
                                      "weapon_hornetgun"};
        bool all = true;
        for (const char *name : kNames)
            all = all && itemOfName(name) != ItemKind::None;
        check(all && itemOfName("weapon_bfg") == ItemKind::None, "the things there are to give, by their names");
        // Each name help shows is one the line reads.
        bool known = true;
        for (const CommandInfo &info : kCommands)
            known = known && (parseCommand(info.name).command != Command::Unknown || std::strcmp(info.name, "impulse") == 0);
        check(known, "every command help shows is read");
    }

    void testHelpAndLog()
    {
        const CommandLine a = parseCommand("god help"), b = parseCommand("help god"), c = parseCommand("HELP Noclip"),
                          d = parseCommand("impulse help");
        check(a.command == Command::Usage && std::strcmp(a.args[0], "god") == 0 && b.command == Command::Usage &&
                  std::strcmp(b.args[0], "god") == 0 && c.command == Command::Usage &&
                  std::strcmp(c.args[0], "noclip") == 0 && d.command == Command::Usage &&
                  std::strcmp(d.args[0], "impulse") == 0,
              "\"<command> help\" and \"help <command>\" ask how it is used");
        check(parseCommand("help").command == Command::Help && parseCommand("help 2").command == Command::Help &&
                  parseCommand("give help").command == Command::Usage,
              "... and help alone is the list");
        bool said = true;
        for (const CommandInfo &info : kCommands)
            said = said && info.about != nullptr && info.about[0] != '\0' &&
                   std::strlen(info.about) <= static_cast<std::size_t>(ConsoleLog::kColumns) &&
                   commandInfo(info.name) == &info;
        check(said, "every command says what it does, in a line of the console");

        ConsoleLog log;
        log.print("one\ntwo");
        check(log.count() == 2 && std::strcmp(log.line(0), "one") == 0 && std::strcmp(log.line(1), "two") == 0,
              "the console takes a line at each newline");
        log.print("aaaa bbbb cccc dddd eeee ffff gggg hhhh");
        check(log.count() == 4 && std::strcmp(log.line(2), "aaaa bbbb cccc dddd eeee ffff") == 0 &&
                  std::strcmp(log.line(3), "gggg hhhh") == 0,
              "... and breaks a long one at a space");
        log.print("0123456789012345678901234567890123456789");
        check(log.count() == 6 && std::strlen(log.line(4)) == 32 && std::strcmp(log.line(5), "23456789") == 0,
              "... or anywhere, for a word longer than a line");
        for (int n = 0; n < 100; ++n)
        {
            char text[8];
            std::snprintf(text, sizeof(text), "%d", n);
            log.print(text);
        }
        check(log.count() == ConsoleLog::kLines && std::strcmp(log.line(0), "36") == 0 &&
                  std::strcmp(log.line(ConsoleLog::kLines - 1), "99") == 0,
              "... keeping its newest 64");
    }

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

    void tick(Game &g, int frames)
    {
        for (int f = 0; f < frames; ++f)
        {
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
            g.drain();
        }
    }

    int findKind(const Game &g, ActorKind kind, int nth = 0)
    {
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            if (actor.active && actor.kind == static_cast<std::uint8_t>(kind) && actor.script < 0 && nth-- == 0)
                return i;
        }
        return -1;
    }

    /// Whether the monster at `index` is making one of its attacks.
    bool attacking(const Game &g, int index)
    {
        const AiMonster *m = aiFind(g.actors, index);
        return m != nullptr && (m->activity == kActMeleeAttack1 || m->activity == kActMeleeAttack2 ||
                                m->activity == kActRangeAttack1 || m->activity == kActRangeAttack2);
    }

    /// The player held next to `actor`, `distance` units off along x.
    void beside(Game &g, const ActorState &actor, int distance)
    {
        g.player.origin[0] = actor.pos[0] + fxFromInt(distance);
        g.player.origin[1] = actor.pos[1];
        g.player.origin[2] = actor.pos[2] + kStandingFeet;
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        g.player.onGround = true; // (standing: a zombie's slash is for what stands)
    }

    void testCheats(const char *dir)
    {
        Cheats cheats;
        // God and notarget, against a zombie in c1a1a.
        {
            auto game = load(dir, "c1a1a");
            if (!game)
                return;
            Game &g = *game;
            g.player.cheats = &cheats;
            const int zombie = findKind(g, ActorKind::Zombie);
            check(zombie >= 0, "a zombie to stand by");
            if (zombie < 0)
                return;
            for (int i = 0; i < g.actors.count; ++i)
            {
                if (i != zombie && isMonster(g.actors.states[i].kind))
                    g.actors.states[i].active = false;
            }
            ActorState &actor = g.actors.states[zombie];
            const fx home[3] = {actor.pos[0], actor.pos[1], actor.pos[2]};
            auto stay = [&]()
            {
                for (int k = 0; k < 3; ++k)
                    actor.pos[k] = home[k];
                beside(g, actor, 40);
            };
            // (Facing them: the game's monsters see what is in front of them.)
            actor.yaw = 0;
            // Notarget: stood beside it for five seconds, it never turns on them.
            cheats.notarget = true;
            bool after = false;
            for (int f = 0; f < 150; ++f)
            {
                stay();
                tick(g, 1);
                after = after || actor.ai == kAiChase || attacking(g, zombie);
            }
            check(!after && g.player.health == 100, "notarget: a zombie at arm's length lets them be");
            cheats.notarget = false;
            // God: it goes for them, and nothing comes of it.
            cheats.god = true;
            bool struck = false;
            for (int f = 0; f < 300; ++f)
            {
                stay();
                tick(g, 1);
                struck = struck || attacking(g, zombie);
                if (std::getenv("HLDS_AI_TRACE") != nullptr && f % 10 == 0)
                {
                    const AiMonster *m = aiFind(g.actors, zombie);
                    std::printf("  zombie f=%d st=%d sched=%s act=%d enemy=%d cond=%08x ground=%d\n", f, m->state,
                                m->schedule ? m->schedule->name : "-", m->activity, m->enemy, m->conditions, g.player.onGround);
                }
            }
            check(struck && g.player.health == 100 && g.player.inv.armor == 0, "god: it attacks, and does no harm");
            logicHurt(g.player, 40);
            check(g.player.health == 100, "... nor does anything else");
            logicHurt(g.player, -10);
            check(g.player.health == 100, "(... and healing still heals up to the full)");
            cheats.god = false;
            for (int f = 0; f < 300 && g.player.health == 100; ++f)
            {
                stay();
                tick(g, 1);
            }
            check(g.player.health < 100, "without it, it does");
            g.player.health = 100;

            // Killall: dead, each of its own death.
            cheats = Cheats();
            const int killed = killAllOfKind(g.logic, ActorKind::Zombie);
            check(killed == 1 && actor.ai >= kAiDying && actor.active, "killall zombie: it dies, in its own way");
            check(killAllOfKind(g.logic, ActorKind::Zombie) == 0, "... and the dead are not killed again");

            // sv_gravity: the player falls as fast as it says.
            // (From the same place each time, in the open over the zombie's
            // floor, its body moved out of the way.)
            actor.active = false;
            PlayerState start = g.player;
            start.origin[0] = home[0];
            start.origin[1] = home[1];
            start.origin[2] = home[2] + kStandingFeet + fxFromInt(80);
            start.velocity[0] = start.velocity[1] = start.velocity[2] = 0;
            start.onGround = false;
            start.stuck = false;
            auto fall = [&](int gravity)
            {
                cheats.gravity = fxFromInt(gravity);
                g.player = start;
                const fx from = g.player.origin[2];
                for (int n = 0; n < 8; ++n)
                    playerMove(g.player, MoveCommand(), g.move, kPlayerTick);
                return fxToInt(from - g.player.origin[2]);
            };
            const int normal = fall(800), light = fall(200);
            std::printf("test_commands: fallen in 8 ticks: %d units at 800, %d at 200\n", normal, light);
            check(normal > 0 && light > 0 && light * 3 < normal, "sv_gravity: a quarter of the pull, a quarter of the fall");
            cheats = Cheats();
        }
        // Barnacles, in c1a1c: one grabbing with barnaclekill on dies of it.
        {
            auto game = load(dir, "c1a1c");
            if (!game)
                return;
            Game &g = *game;
            g.player.cheats = &cheats;
            const int barnacle = findKind(g, ActorKind::Barnacle);
            check(barnacle >= 0, "a barnacle in c1a1c");
            if (barnacle < 0)
                return;
            ActorState &actor = g.actors.states[barnacle];
            tick(g, 2); // (its first look: its health, how far its tongue hangs)
            auto under = [&]()
            {
                g.player.origin[0] = actor.pos[0];
                g.player.origin[1] = actor.pos[1];
                g.player.origin[2] = actor.pos[2] - fxFromInt(40) - kStandingFeet;
                g.player.velocity[2] = 0;
            };
            // (Its tongue let down first: eight units a tenth of a second.)
            auto grabbed = [&](int index)
            {
                const AiMonster *m = aiFind(g.actors, index);
                return m != nullptr && m->enemy == kAiPlayer;
            };
            int frames = 0;
            for (; frames < 600 && !grabbed(barnacle); ++frames)
            {
                under();
                tick(g, 1);
            }
            check(grabbed(barnacle), "without it, a barnacle grabs whoever is under it");
            // (Let go: it is killed, as it would be to get away.)
            monsterHurt(g.logic, barnacle, 100, false, kHitBody, kDmgClub);
            // Another, with it on.
            const int other = findKind(g, ActorKind::Barnacle, 1);
            check(other >= 0, "a second barnacle");
            if (other < 0)
                return;
            ActorState &next = g.actors.states[other];
            cheats.barnacleKill = true;
            const fx held = next.pos[2] - fxFromInt(40) - kStandingFeet;
            bool lifted = false;
            for (frames = 0; frames < 600 && next.ai < kAiDying; ++frames)
            {
                g.player.origin[0] = next.pos[0];
                g.player.origin[1] = next.pos[1];
                g.player.origin[2] = held;
                tick(g, 1);
                lifted = lifted || g.player.origin[2] > held;
            }
            check(next.ai >= kAiDying && !lifted && g.player.health == 100,
                  "barnaclekill: the one that grabs dies of it, and lifts nobody");
            // And every one there is, at once.
            const int alive = [&]()
            {
                int n = 0;
                for (int i = 0; i < g.actors.count; ++i)
                    n += g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Barnacle) &&
                         g.actors.states[i].ai < kAiDying;
                return n;
            }();
            const int killed = killAllOfKind(g.logic, ActorKind::Barnacle);
            int left = 0;
            for (int i = 0; i < g.actors.count; ++i)
                left += g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Barnacle) &&
                        g.actors.states[i].ai < kAiDying;
            std::printf("test_commands: killall barnacle: %d of %d\n", killed, alive);
            check(killed == alive && alive >= 5 && left == 0, "killall barnacle: every one of them");
            cheats = Cheats();
        }
    }
} // namespace

int main()
{
    testParse();
    testHelpAndLog();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
        testCheats(dir);
    else
        std::printf("test_commands: HLDS_VALVE_DIR not set, the game's maps were not played\n");
    if (g_failures == 0)
        std::printf("test_commands: ok\n");
    return g_failures == 0 ? 0 : 1;
}
