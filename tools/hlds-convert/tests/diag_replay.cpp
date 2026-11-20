// The port's side of tools/ref/diag.sh: a scenario played through the game's
// own code (shared/), on the host, in the console's order of things each
// frame, writing the same log the real game's diagnostic build writes.
//
//   diag_replay <map.bsp> <scenario.txt> <out.csv> [settle frames] [game's log]
//
// The map is the copy diag.sh made (start moved, entities stripped), so both
// play the same map. Every frame is a sixtieth of a second, as the real game's
// run is (host_framerate): one vblank, as on the console at full speed. Given
// the real game's log, each scripted frame's move takes its command's own
// length instead (whole milliseconds there: 17, 17, 16; its physics steps a
// sixtieth), so that the two are given the same steps of time as well as the
// same input. Before
// the script, the player stands for `settle` frames (120) to come to rest on
// the floor, as the real game's player has before its script starts.
//
// Rows (and their columns: the "#cols" lines) are the real game's, in its
// units: whole units Q12 -> float, angles in degrees, the game's buttons.
// What the port has no counterpart for is 0.
//
// The player starts as the real game's does in tools/ref/play.sh: nothing
// hurts them (god), they have what impulse 101 gives, the crowbar in hand.
// A scenario's "cmd" lines are run as the real game ran them -- the frame
// its log's ccmd row says, given that log and such a row (the engine's own
// commands have none); else where the line stands: give <thing>,
// weapon_<name>, god, impulse 101.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "HlCommands.hpp"
#include "HlFrameClock.hpp"
#include "HlView.hpp"
#include "TestGame.hpp"

using namespace hldstest;

namespace
{
    // The game's buttons (common/in_buttons.h), for the cmd rows.
    constexpr int kInAttack = 1 << 0, kInJump = 1 << 1, kInDuck = 1 << 2, kInForward = 1 << 3,
                  kInBack = 1 << 4, kInUse = 1 << 5, kInMoveLeft = 1 << 9, kInMoveRight = 1 << 10,
                  kInAttack2 = 1 << 11, kInReload = 1 << 13;

    struct Span
    {
        int frames = 0;
        float forward = 0, side = 0, yaw = 0, pitch = 0;
        int buttons = 0;
    };
    /// A console command, run before the frame `frame`'s move.
    struct Order
    {
        int frame;
        std::string text;
    };

    bool loadScenario(const char *path, std::vector<Span> &spans, std::vector<Order> &orders)
    {
        std::ifstream file(path);
        if (!file)
            return false;
        std::string line;
        int at = 0;
        while (std::getline(file, line))
        {
            const auto hash = line.find('#');
            if (hash != std::string::npos)
                line.resize(hash);
            if (line.rfind("cmd ", 0) == 0)
            {
                orders.push_back({at + 1, line.substr(4)});
                continue;
            }
            std::istringstream in(line);
            Span s;
            std::string buttons;
            if (!(in >> s.frames >> s.forward >> s.side >> s.yaw >> s.pitch) || s.frames <= 0)
                continue;
            in >> buttons;
            for (char c : buttons)
            {
                s.buttons |= c == 'a' ? kInAttack : c == 'b' ? kInAttack2 : c == 'u' ? kInUse
                           : c == 'j' ? kInJump : c == 'd' ? kInDuck : c == 'r' ? kInReload : 0;
            }
            spans.push_back(s);
            at += s.frames;
        }
        return !spans.empty();
    }

    /// The weapons as the real game names them (their classes), by kWeapon*.
    constexpr const char *kWeaponClass[kWeaponCount] = {
        "-",          "weapon_crowbar", "weapon_9mmhandgun", "weapon_shotgun",  "weapon_handgrenade",
        "weapon_9mmAR", "weapon_357",   "weapon_crossbow",   "weapon_satchel",  "weapon_tripmine",
        "weapon_snark", "weapon_rpg",   "weapon_gauss",      "weapon_egon",     "weapon_hornetgun"};
    std::string lower(std::string text)
    {
        for (char &c : text)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }
    /// The weapon called `name` (weapon_9mmAR, ...: the game's aliases
    /// too), or kWeaponNone.
    int weaponNamed(const std::string &name)
    {
        const std::string n = lower(name);
        for (int w = 1; w < kWeaponCount; ++w)
        {
            if (n == lower(kWeaponClass[w]))
                return w;
        }
        return n == "weapon_glock" ? kWeaponGlock : n == "weapon_mp5" ? kWeaponMp5
               : n == "weapon_python" ? kWeaponPython : kWeaponNone;
    }
    /// The real game's impulse 101: these, one after another, as if picked up.
    constexpr const char *kImpulse101[] = {
        "item_suit",       "item_battery",    "weapon_crowbar",    "weapon_9mmhandgun", "ammo_9mmclip",
        "weapon_shotgun",  "ammo_buckshot",   "weapon_9mmar",      "ammo_9mmar",        "ammo_argrenades",
        "weapon_handgrenade", "weapon_tripmine", "weapon_357",     "ammo_357",          "weapon_crossbow",
        "ammo_crossbow",   "weapon_egon",     "weapon_gauss",      "ammo_gaussclip",    "weapon_rpg",
        "ammo_rpgclip",    "weapon_satchel",  "weapon_snark",      "weapon_hornetgun"};
    bool give(PlayerState &player, const std::string &name)
    {
        const ItemKind item = itemOfName(lower(name).c_str());
        std::uint32_t got = 0;
        return item != ItemKind::None && hldata::detail::itemTake(player, static_cast<std::uint8_t>(item), got);
    }

    /// What the player has thrown or fired, as the real game names it:
    /// its class, and how it goes (nullptr: not the player's).
    const char *thrownClass(const Projectile &shot, const char *&sub)
    {
        sub = "-";
        switch (shot.kind)
        {
        case kShotGrenade: sub = "timed"; return "grenade";
        case kShotContact: sub = "contact"; return "grenade";
        case kShotBolt: return "crossbow_bolt";
        case kShotSatchel: return "monster_satchel";
        case kShotMine: return "monster_tripmine";
        case kShotSnark: return "monster_snark";
        case kShotRocket: return "rpg_rocket";
        case kShotHornet: sub = shot.sprite == 1 ? "dart" : "track"; return "hornet";
        default: return nullptr;
        }
    }

    double units(fx v) { return static_cast<double>(v) / kFxOne; }
    std::uint16_t turns(float degrees)
    {
        return static_cast<std::uint16_t>(static_cast<std::int32_t>(std::lround(degrees * 65536.0 / 360.0)));
    }

    /// The BSP's model number ("*N") of each brush, found by its bounds at
    /// rest: the level keeps no number of its own.
    std::vector<std::string> brushNames(const Game &g)
    {
        std::vector<std::string> names(g.view.brushCount);
        for (std::uint32_t b = 0; b < g.view.brushCount; ++b)
        {
            const Brush &brush = g.view.brushes[b];
            names[b] = "?";
            for (std::size_t m = 1; m < g.bsp.models.size(); ++m)
            {
                const BspModel &model = g.bsp.models[m];
                bool same = true;
                for (int a = 0; a < 3 && same; ++a)
                    same = std::lround(model.mins[a]) == brush.mins[a] && std::lround(model.maxs[a]) == brush.maxs[a];
                if (same)
                {
                    names[b] = "*" + std::to_string(m);
                    break;
                }
            }
        }
        return names;
    }

    /// Ladder `i`'s BSP model ("*N"), found by its bounds (the converter's
    /// are the model's, rounded out to whole units).
    std::string ladderName(const Game &g, int i)
    {
        const LadderVolume &ladder = g.move.ladders[i];
        for (std::size_t m = 1; m < g.bsp.models.size(); ++m)
        {
            const BspModel &model = g.bsp.models[m];
            bool same = true;
            for (int a = 0; a < 3 && same; ++a)
            {
                same = static_cast<fx>(std::floor(model.mins[a])) == ladder.mins[a] / kFxOne &&
                       static_cast<fx>(std::ceil(model.maxs[a])) == ladder.maxs[a] / kFxOne;
            }
            if (same)
                return "*" + std::to_string(m);
        }
        return "?";
    }

    /// What the frame's move made of a ladder (hldata::diag::ladder).
    struct
    {
        bool on = false;
        diag::Ladder seen{};
    } frameLadder;

    /// What the hooks hear (shots, blasts, hurts) is written as it happens,
    /// filed under the frame being played.
    struct
    {
        FILE *out = nullptr;
        int frame = 0;
        double t = 0;
        std::uint32_t seed = 0;
        const Game *game = nullptr;
        const std::vector<std::string> *names = nullptr;
    } now;
} // namespace

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: diag_replay <map.bsp> <scenario.txt> <out.csv> [settle frames]\n");
        return 2;
    }
    const int settle = argc > 4 ? std::atoi(argv[4]) : 120;
    std::vector<Span> spans;
    std::vector<Order> orders;
    if (!loadScenario(argv[2], spans, orders))
    {
        std::fprintf(stderr, "diag_replay: no scenario in %s\n", argv[2]);
        return 1;
    }
    // The real game's commands' lengths and seeds, by frame (0: not known),
    // and the console commands it ran, when.
    std::vector<int> gameMs;
    std::vector<std::uint32_t> gameSeed;
    if (argc > 5)
    {
        std::ifstream log(argv[5]);
        std::string line;
        std::vector<Order> ran;
        while (std::getline(log, line))
        {
            const bool command = line.rfind("cmd,", 0) == 0;
            if (line.rfind("ccmd,", 0) == 0)
            {
                // ccmd,frame,seq,t,text: run after that frame's command.
                std::istringstream in(line);
                std::string field[5];
                for (std::string &f : field)
                    std::getline(in, f, ',');
                ran.push_back({std::atoi(field[1].c_str()) + 1, field[4]});
                continue;
            }
            if (!command)
                continue;
            // cmd,frame,seq,t,sframe,msec,fwd,side,up,pitch,yaw,buttons,impulse,seed
            std::istringstream in(line);
            std::string field[14];
            for (std::string &f : field)
                std::getline(in, f, ',');
            const int frame = std::atoi(field[1].c_str()), ms = std::atoi(field[5].c_str());
            if (frame > 0)
            {
                if (static_cast<int>(gameMs.size()) <= frame)
                {
                    gameMs.resize(frame + 1, 0);
                    gameSeed.resize(frame + 1, 0);
                }
                gameMs[frame] += ms;
                gameSeed[frame] = static_cast<std::uint32_t>(std::strtoul(field[13].c_str(), nullptr, 10));
            }
        }
        // Each of the scenario's commands when the game ran it, if its log
        // says (those the game's own code runs, by the first word); the
        // engine's own (god, noclip) are not in it, and stay where the
        // scenario put them.
        std::vector<bool> taken(ran.size(), false);
        for (Order &order : orders)
        {
            const std::string verb = lower(order.text.substr(0, order.text.find(' ')));
            for (std::size_t r = 0; r < ran.size(); ++r)
            {
                if (!taken[r] && lower(ran[r].text.substr(0, ran[r].text.find(' '))) == verb)
                {
                    taken[r] = true;
                    order.frame = ran[r].frame;
                    break;
                }
            }
        }
    }
    Globals globals;
    auto game = std::make_unique<Game>();
    Game &g = *game;
    std::string error;
    if (!g.bsp.load(argv[1], error))
    {
        std::fprintf(stderr, "diag_replay: %s: %s\n", argv[1], error.c_str());
        return 1;
    }
    finish(g, &globals);
    if (!g.ok)
        return 1;
    // As tools/ref/play.sh starts the real game's player: god, impulse 101,
    // the crowbar in hand. (No aim bent to a monster: sv_aim 0.)
    Arsenal arsenal;
    Cheats cheats;
    WeaponState weapon;
    g.player.arsenal = &arsenal;
    g.player.cheats = &cheats;
    cheats.god = true;
    for (const char *thing : kImpulse101)
        give(g.player, thing);
    g.player.inv.current = kWeaponCrowbar;
    g.logic.noAutoaim.value = true;
    int picked = 0; // (a weapon_<name> asked for, for the next weapons tick)
    const auto run = [&](const std::string &text) {
        std::istringstream in(text);
        std::string verb, arg;
        in >> verb >> arg;
        if (verb == "god")
            cheats.god = !cheats.god;
        else if (verb == "give" && give(g.player, arg))
            ;
        else if (verb == "impulse" && arg == "101")
        {
            for (const char *thing : kImpulse101)
                give(g.player, thing);
        }
        else if (weaponNamed(verb) != kWeaponNone)
            picked = weaponNamed(verb);
        else
            std::fprintf(stderr, "diag_replay: '%s' not played on this side\n", text.c_str());
    };
    FILE *out = std::fopen(argv[3], "w");
    if (!out)
    {
        std::fprintf(stderr, "diag_replay: cannot write %s\n", argv[3]);
        return 1;
    }
    const std::vector<std::string> names = brushNames(g);
    std::vector<int> boxes;
    for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
    {
        if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Pushable) && g.view.logic[i].brush != kNoBrush)
            boxes.push_back(static_cast<int>(i));
    }
    std::fprintf(out, "#diag port 1\n");
    std::fprintf(out, "#cols cmd frame seq t sframe msec fwd side up pitch yaw buttons impulse seed\n");
    std::fprintf(out, "#cols pl frame seq t ox oy oz vx vy vz pitch yaw punchp punchy og duck ground water mt lad\n");
    std::fprintf(out, "#cols phys frame seq t sframe ft\n");
    std::fprintf(out, "#cols box frame seq t id cx cy cz vx vy vz og ground water\n");
    std::fprintf(out, "#cols touch frame seq t id who push result tvx tvy bvx0 bvy0 bvx bvy\n");
    std::fprintf(out, "#cols lad frame seq t id hit frac nx ny nz onfloor fwd right\n");
    std::fprintf(out, "#cols ccmd frame seq t text\n");
    std::fprintf(out, "#cols wpn frame seq t cls clip ammo1 ammo2 next1 next2 idle reload\n");
    std::fprintf(out, "#cols proj frame seq t id cls sub ox oy oz vx vy vz mt og water\n");
    std::fprintf(out, "#cols shot frame seq t n seed sx sy sz ax ay az x y cx cy ex ey ez frac hit\n");
    std::fprintf(out, "#cols boom frame seq t x y z damage radius type by\n");
    std::fprintf(out, "#cols dmg frame seq t amount type by fx fy fz vx0 vy0 vz0 vx vy vz hp0 hp ap0 ap\n");
    diag::ladder = [](const diag::Ladder &seen) {
        frameLadder.on = true;
        frameLadder.seen = seen;
    };
    now.out = out;
    now.game = &g;
    now.names = &names;
    diag::shot = [](const diag::Shot &shot) {
        if (now.frame < 1)
            return;
        // Where it goes: the world's and the brushes' (no monsters here).
        fx end[3];
        for (int a = 0; a < 3; ++a)
            end[a] = shot.from[a] + shot.dir[a] * shot.range;
        const Shot met = traceShot(now.game->move, shot.from, end);
        const double frac = static_cast<double>(met.fraction) / kFractionOne;
        double e[3];
        for (int a = 0; a < 3; ++a)
            e[a] = units(shot.from[a]) + (units(end[a]) - units(shot.from[a])) * frac;
        const char *hit = met.fraction >= kFractionOne ? "-"
                          : met.mover >= 0 ? (*now.names)[met.mover].c_str() : "world";
        std::fprintf(now.out, "shot,%d,%d,%.4f,%d,%u,%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.3f,%.3f,%.3f,%.5f,%s\n",
                     now.frame, now.frame, now.t, shot.n, now.seed, units(shot.from[0]), units(shot.from[1]),
                     units(shot.from[2]), units(shot.aim[0]), units(shot.aim[1]), units(shot.aim[2]),
                     shot.drawX / 65536.0, shot.drawY / 65536.0, shot.cone / 4096.0, shot.cone / 4096.0, e[0], e[1],
                     e[2], frac, hit);
    };
    diag::boom = [](const diag::Boom &boom) {
        if (now.frame < 1)
            return;
        std::fprintf(now.out, "boom,%d,%d,%.4f,%.3f,%.3f,%.3f,%d.00,%d.00,%u,-\n", now.frame, now.frame, now.t,
                     units(boom.at[0]), units(boom.at[1]), units(boom.at[2]), boom.damage, boom.radius, boom.type);
    };
    diag::hurt = [](const diag::Hurt &h) {
        if (now.frame < 1)
            return;
        std::fprintf(now.out, "dmg,%d,%d,%.4f,%d.00,%u,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d.00,%d.00,%d.00,%d.00\n",
                     now.frame, now.frame, now.t, h.amount, h.type, h.known ? "?" : "-", units(h.from[0]),
                     units(h.from[1]), units(h.from[2]), units(h.velocity0[0]), units(h.velocity0[1]),
                     units(h.velocity0[2]), units(h.velocity[0]), units(h.velocity[1]), units(h.velocity[2]),
                     h.health0, h.health, h.armor0, h.armor);
    };
    // Each of the projectile slots' the n-th thing in it (a new id each).
    int slotUse[kMaxProjectiles] = {};
    bool slotWas[kMaxProjectiles] = {};
    int lastButtons = 0;

    const auto groundName = [&](const PlayerState &p) -> std::string {
        if (!p.onGround)
            return "-";
        return p.groundMover >= 0 ? names[p.groundMover] : "world";
    };

    FrameClock clock;
    constexpr std::int32_t kVblankTime = 1092; // a sixtieth of a second, Q16 (source/game/Player.arm.cpp)
    long timeMs = 0;
    int frameAll = 0;
    const int scripted = [&] {
        int n = 0;
        for (const Span &s : spans)
            n += s.frames;
        return n;
    }();
    std::size_t spanAt = 0;
    int spanLeft = spans[0].frames;
    // DIAG_VBLANKS=2: the console's pace (about 30 a second), the real
    // game's frames taken two at a time -- their lengths added, their
    // buttons both -- and only every second frame logged.
    const char *pace = std::getenv("DIAG_VBLANKS");
    const int step = pace != nullptr && std::atoi(pace) > 1 ? std::atoi(pace) : 1;
    int heldButtons = 0, heldMs = 0;
    std::int32_t heldDt = 0;
    for (int f = 1 - settle; f <= scripted; ++f, ++frameAll)
    {
        Span span;
        if (f >= 1)
        {
            while (spanLeft == 0)
                spanLeft = spans[++spanAt].frames;
            span = spans[spanAt];
            --spanLeft;
        }
        else
        {
            span.yaw = spans[0].yaw;
            span.pitch = spans[0].pitch;
        }
        // The logic's step: a vblank's, in milliseconds as the console counts
        // them (the real game's physics steps a sixtieth exactly). The
        // player's: the command's length, in the real game whole
        // milliseconds, if known; else a vblank's.
        int ms = clock.ms(1);
        const bool fromGame = f >= 1 && f < static_cast<int>(gameMs.size()) && gameMs[f] > 0;
        std::int32_t dt =
            fromGame ? static_cast<std::int32_t>((static_cast<std::int64_t>(gameMs[f]) << 16) / 1000) : kVblankTime;
        timeMs += ms;
        if (f >= 1 && step > 1)
        {
            heldButtons |= span.buttons;
            heldDt += dt;
            heldMs += ms;
            if (f % step != 0)
                continue;
            span.buttons = heldButtons;
            dt = heldDt;
            ms = heldMs;
            heldButtons = heldMs = 0;
            heldDt = 0;
        }
        const double t = timeMs / 1000.0;
        const bool useHeld = (span.buttons & kInUse) != 0;

        // The view, as the console's camera has it: yaw and pitch in turns,
        // the game's pitch down. (Not the kick: what it does to the aim
        // is the camera's, not the game's.)
        fx forward[3], right[3], up[3];
        viewBasis(turns(span.yaw), turns(-span.pitch), forward, right, up);
        const fx sy = right[0], cy = -right[1];

        int buttons = span.buttons;
        buttons |= span.forward > 0 ? kInForward : span.forward < 0 ? kInBack : 0;
        buttons |= span.side > 0 ? kInMoveRight : span.side < 0 ? kInMoveLeft : 0;
        now.frame = f;
        now.t = t;
        now.seed = f >= 1 && f < static_cast<int>(gameSeed.size()) ? gameSeed[f] : 0;
        if (f >= 1)
        {
            std::fprintf(out, "cmd,%d,%d,%.4f,%d,%d,%.1f,%.1f,0.0,%.4f,%.4f,%d,0,%u\n", f, f, t, frameAll, ms,
                         span.forward, span.side, span.pitch, span.yaw, buttons, now.seed);
            // The console commands due by now (at the console's pace, those
            // of the frames it took together too).
            for (const Order &order : orders)
            {
                if (order.frame <= f && order.frame > f - step)
                {
                    std::fprintf(out, "ccmd,%d,%d,%.4f,%s\n", f - 1, f - 1, t, order.text.c_str());
                    run(order.text);
                }
            }
        }

        // What the player's last move ran into: the box's shove, this frame.
        // (The real game shoves it in that move's touch, and moves it in the
        // physics after: the same place between two moves.)
        std::vector<fx> before;
        for (int r : boxes)
        {
            before.push_back(g.states[r].travel[0]);
            before.push_back(g.states[r].travel[1]);
        }
        const int touched = g.player.touchedMover;
        const bool touchedOnGround = g.player.onGround;
        const bool touchedHolding = g.logic.useHeldAtTouch.value; // (USE as it was at the touch)
        const fx touchV[2] = {g.player.touchVelocity[0], g.player.touchVelocity[1]};

        // Game::frame's order (dll/hl/Game.arm.cpp): the logic (boxes
        // move), USE, the monsters, the weapons, then the player's move.
        g.logic.useHeld.value = useHeld;
        logicTick(g.logic, g.player, ms);
        g.logic.viewTurn = 0;
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
        int pulled = -1; // (pulled after the move, as Game::frame does)
        if (useHeld && g.player.health > 0)
        {
            const int target = logicFindUsable(g.logic, eye, forward);
            if (target >= 0 && g.logic.records[target].kind == static_cast<std::uint8_t>(LogicKind::Pushable))
                pulled = target;
        }
        monstersTick(g.logic, g.player, ms);
        g.drain();
        {
            const bool alive = g.player.health > 0;
            const bool reloadPressed = (span.buttons & kInReload) && !(lastButtons & kInReload);
            lastButtons = span.buttons;
            // (The command's seed: the game's, if its log is given; else
            // counted, as the console does.)
            const std::uint32_t seed = now.seed != 0 ? now.seed : static_cast<std::uint32_t>(frameAll);
            weaponsTick(g.logic, g.player, weapon, eye, forward, right, up, alive && (span.buttons & kInAttack), false,
                        ms, alive && (span.buttons & kInAttack2), alive ? picked : 0, alive && reloadPressed, seed);
            picked = 0;
            weapon.anim = nullptr;
            g.actors.got = 0;
            g.logic.brokeCount = 0;
            g.drain();
        }

        MoveCommand cmd;
        cmd.forward = span.forward > 0 ? 1 : span.forward < 0 ? -1 : 0;
        cmd.side = span.side > 0 ? 1 : span.side < 0 ? -1 : 0;
        // (The console's keys are on or off, at a run or at its walk -- the
        // game's walk, 120: a speed of that or less is the walk.)
        cmd.walk = std::max(std::fabs(span.forward), std::fabs(span.side)) <= 120.0f &&
                   (span.forward != 0 || span.side != 0);
        cmd.jump = (span.buttons & kInJump) != 0;
        cmd.duck = (span.buttons & kInDuck) != 0;
        cmd.use = useHeld;
        cmd.forwardXY[0] = cy, cmd.forwardXY[1] = sy;
        cmd.rightXY[0] = sy, cmd.rightXY[1] = -cy;
        for (int a = 0; a < 3; ++a)
            cmd.viewForward[a] = forward[a];
        for (int a = 0; a < 3; ++a)
            cmd.viewRight[a] = right[a];
        frameLadder.on = false;
        playerMove(g.player, cmd, g.move, dt);
        if (pulled >= 0)
            logicPull(g.logic, g.player, pulled, ms);

        if (f < 1)
            continue;
        for (std::size_t n = 0; n < boxes.size(); ++n)
        {
            const LogicRecord &record = g.view.logic[boxes[n]];
            if (touched != record.brush)
                continue;
            // (Filed under the move that made it, as the real game's are.)
            std::fprintf(out, "touch,%d,%d,%.4f,%s,player,1,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", f - 1, f - 1, t,
                         names[record.brush].c_str(),
                         touchedHolding ? "buttons" : !touchedOnGround && g.player.waterLevel == 0 ? "air" : "ok", units(touchV[0]), units(touchV[1]),
                         units(before[2 * n]), units(before[2 * n + 1]), units(g.states[boxes[n]].travel[0]),
                         units(g.states[boxes[n]].travel[1]));
        }
        const PlayerState &p = g.player;
        const std::string ladder = frameLadder.on ? ladderName(g, frameLadder.seen.index) : "-";
        // (mt: the game's movetype, 5 on a ladder, 3 walking.)
        std::fprintf(out, "pl,%d,%d,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,0,0,%d,%d,%s,%d,%d,%s\n", f, f, t,
                     units(p.origin[0]), units(p.origin[1]), units(p.origin[2]), units(p.velocity[0]),
                     units(p.velocity[1]), units(p.velocity[2]), span.pitch, span.yaw, p.onGround ? 1 : 0,
                     p.ducked ? 1 : 0, groundName(p).c_str(), p.waterLevel, p.onLadder ? 5 : 3, ladder.c_str());
        if (frameLadder.on)
        {
            // (No fraction: the port finds the face without the game's trace.)
            const diag::Ladder &l = frameLadder.seen;
            constexpr double kOne = 1 << 30;
            std::fprintf(out, "lad,%d,%d,%.4f,%s,%d,0,%.4f,%.4f,%.4f,%d,%.1f,%.1f\n", f, f, t, ladder.c_str(),
                         l.hit ? 1 : 0, l.normal[0] / kOne, l.normal[1] / kOne, l.normal[2] / kOne, l.onFloor ? 1 : 0,
                         units(l.forward), units(l.right));
        }
        std::fprintf(out, "phys,%d,%d,%.4f,%d,%.5f\n", f, f, t, frameAll, ms / 1000.0);
        for (int r : boxes)
        {
            const LogicRecord &record = g.view.logic[r];
            const Brush &brush = g.view.brushes[record.brush];
            const fx *pos = g.brushStates[record.brush].pos;
            double c[3];
            for (int a = 0; a < 3; ++a)
                c[a] = units(pos[a]) + (brush.mins[a] + brush.maxs[a]) * 0.5 - units(brush.origin[a]);
            const LogicState &s = g.states[r];
            std::fprintf(out, "box,%d,%d,%.4f,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%s,0\n", f, f, t,
                         names[record.brush].c_str(), c[0], c[1], c[2], units(s.travel[0]), units(s.travel[1]),
                         units(s.travel[2]), s.bits != 0 ? 1 : 0, s.bits == 1 ? "world" : s.bits == 2 ? "mover" : "-");
        }
        // The weapon in hand (no clip: -1, as the game's).
        if (p.inv.current != kWeaponNone)
        {
            PlayerState &me = g.player;
            const std::int16_t *clip = gunClip(me);
            const std::int16_t *spare = gunSpare(me);
            const std::int16_t *stock = weaponStock(me);
            int ammo1 = spare != nullptr ? *spare : stock != nullptr ? *stock : -1;
            int ammo2 = -1;
            if (me.inv.current == kWeaponMp5)
                ammo2 = me.inv.ammoAr;
            else if (me.inv.current == kWeaponRpg)
                ammo1 = arsenal.rockets;
            std::fprintf(out, "wpn,%d,%d,%.4f,%s,%d,%d,%d,%.4f,%.4f,%.4f,%d\n", f, f, t, kWeaponClass[me.inv.current],
                         clip != nullptr ? *clip : me.inv.current == kWeaponRpg ? ((arsenal.have & kArsenalRpgLoaded) ? 1 : 0) : -1,
                         ammo1, ammo2, weapon.cooldownMs / 1000.0, weapon.secondMs / 1000.0, weapon.idleMs / 1000.0,
                         weapon.reloadMs > 0 || weapon.loading != 0 ? 1 : 0);
        }
        // What has been thrown or fired.
        for (int n = 0; n < kMaxProjectiles; ++n)
        {
            const Projectile &shot = g.actors.shots[n];
            const char *sub = "-";
            const char *cls = shot.active ? thrownClass(shot, sub) : nullptr;
            if (cls == nullptr)
            {
                slotWas[n] = false;
                continue;
            }
            if (!slotWas[n])
                ++slotUse[n];
            slotWas[n] = true;
            std::fprintf(out, "proj,%d,%d,%.4f,%d,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,0,0,0\n", f, f, t,
                         1000 + n * 100 + slotUse[n] % 100, cls, sub, units(shot.pos[0]), units(shot.pos[1]),
                         units(shot.pos[2]), units(shot.velocity[0]), units(shot.velocity[1]), units(shot.velocity[2]));
        }
    }
    std::fprintf(out, "END,%d,%d,%.4f\n", scripted, scripted, timeMs / 1000.0);
    std::fclose(out);
    std::printf("%s: %d frames\n", argv[3], scripted);
    return 0;
}
