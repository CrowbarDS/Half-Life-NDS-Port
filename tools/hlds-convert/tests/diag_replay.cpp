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
//
// The monsters (a scenario that keeps them: "# keep ...monster_..."): the
// map's own node graph is the game's (<map>.nod beside the copy, which
// diag.sh takes from the game's run), and their models are cooked from
// HLDS_VALVE_DIR. Each is matched to the game's (mstart rows: its class and
// where it started), its thinks are set to fall in the frames the game's
// did, and the random numbers it draws are the game's, in the order the
// game drew each kind (low, high) of them -- what is left over, or missing,
// is counted at the end.

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "HlCommands.hpp"
#include "HlThink.hpp"
#include "HlFrameClock.hpp"
#include "HlLamp.hpp"
#include "HlView.hpp"
#include "TestGame.hpp"
#include "hlds/SoundCook.hpp"
#include "hlds/AtomicWrite.hpp"

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
    /// rest -- one that turns, by its pivot, its entity's "origin": the level
    /// keeps no number of its own.
    std::vector<std::string> brushNames(const Game &g)
    {
        std::vector<std::string> names(g.view.brushCount);
        // (The entities' models and origins, from the map's text.)
        std::vector<std::pair<std::string, std::array<double, 3>>> pivots;
        {
            const std::string &text = g.bsp.entities;
            std::size_t at = 0;
            while ((at = text.find('{', at)) != std::string::npos)
            {
                const std::size_t end = text.find('}', at);
                const std::string entity = text.substr(at, end - at);
                at = end;
                auto value = [&](const char *key) -> std::string {
                    const std::string quoted = std::string("\"") + key + "\" \"";
                    const std::size_t k = entity.find(quoted);
                    if (k == std::string::npos)
                        return "";
                    const std::size_t from = k + quoted.size();
                    return entity.substr(from, entity.find('"', from) - from);
                };
                const std::string model = value("model"), origin = value("origin");
                std::array<double, 3> o{};
                if (!model.empty() && model[0] == '*' &&
                    std::sscanf(origin.c_str(), "%lf %lf %lf", &o[0], &o[1], &o[2]) == 3)
                    pivots.push_back({model, o});
            }
        }
        for (std::uint32_t b = 0; b < g.view.brushCount; ++b)
        {
            const Brush &brush = g.view.brushes[b];
            names[b] = "?";
            if (brush.flags & kBrushHasPivot)
            {
                for (const auto &[model, o] : pivots)
                {
                    if (std::lround(o[0]) == std::lround(units(brush.origin[0])) &&
                        std::lround(o[1]) == std::lround(units(brush.origin[1])) &&
                        std::lround(o[2]) == std::lround(units(brush.origin[2])))
                    {
                        names[b] = model;
                        break;
                    }
                }
                if (names[b] != "?")
                    continue;
            }
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

    /// The keys of the map's entity whose model is `model` ("*N"), from its
    /// text: what a door is called, its master, its flags.
    std::map<std::string, std::string> entityOfModel(const Game &g, const std::string &model)
    {
        const std::string &text = g.bsp.entities;
        const std::string want = "\"model\" \"" + model + "\"";
        std::size_t at = 0;
        while ((at = text.find('{', at)) != std::string::npos)
        {
            const std::size_t end = text.find('}', at);
            const std::string entity = text.substr(at, end - at);
            at = end;
            if (entity.find(want) == std::string::npos)
                continue;
            std::map<std::string, std::string> keys;
            std::size_t k = 0;
            while ((k = entity.find('"', k)) != std::string::npos)
            {
                const std::size_t ke = entity.find('"', k + 1);
                const std::size_t v = entity.find('"', ke + 1);
                const std::size_t ve = entity.find('"', v + 1);
                if (ke == std::string::npos || v == std::string::npos || ve == std::string::npos)
                    break;
                keys[entity.substr(k + 1, ke - k - 1)] = entity.substr(v + 1, ve - v - 1);
                k = ve + 1;
            }
            return keys;
        }
        return {};
    }

    /// The nodes a walker would go by from where it stands to `goal`, as its
    /// plan (walkPlanInto) takes them: the node nearest it, pathStep after
    /// pathStep, to the node nearest the goal. Each point is "x y z", and
    /// after the first, "/s": how shut the link into it was (kLink*: 0 open,
    /// 1 a door it may open, 2 no way). Empty if there is no way.
    std::string nodeChain(const Game &g, int index, const fx *goal)
    {
        const ActorWorld &a = g.actors;
        if (index < 0 || index >= a.count || a.nodeCount == 0)
            return "";
        const ActorState &me = a.states[index];
        const int hull = monsterHull(me.kind);
        const int mine = pathNearest(a, g.move, me.pos, hull, index);
        const int theirs = pathNearest(a, g.move, goal);
        if (mine < 0 || theirs < 0)
            return "";
        const std::uint8_t size = pathSizeOf(hull);
        const bool opens = opensDoors(me.kind);
        char at[64];
        auto point = [&](int n) {
            const PathNode &node = a.nodes[n];
            std::snprintf(at, sizeof(at), "%.1f %.1f %.1f", units(node.pos[0]), units(node.pos[1]), units(node.pos[2]));
            return std::string(at);
        };
        std::string chain = point(mine);
        int now = mine;
        for (int guard = 0; guard < kMaxPathNodes && now != theirs; ++guard)
        {
            const int next = pathStep(a, now, theirs, size, opens);
            if (next == kNoPathNode)
                return chain + ";none";
            const int link = pathLinkBetween(a, now, next);
            const int shut = link >= 0 && a.linkShut != nullptr ? a.linkShut[link] : 0;
            chain += ";" + point(next) + "/" + std::to_string(shut);
            now = next;
        }
        return chain;
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
    /// A round fired, until its landing is known.
    struct Round
    {
        bool pending = false;
        fx from[3], aim[3], dir[3];
        int range, n, drawX, drawY, cone;
    };

    struct
    {
        FILE *out = nullptr;
        Round round;
        int frame = 0;
        double t = 0;
        std::uint32_t seed = 0;
        const Game *game = nullptr;
        const std::vector<std::string> *names = nullptr;
    } now;

    /// The monsters as the game's log has them, and how they are matched.
    struct GameMonster
    {
        int id = 0;            ///< its entity
        std::string cls;
        double at[3] = {0, 0, 0};
        int firstThink = 0;    ///< the scripted frame of its first think, 0 none
        int actor = -1;        ///< the port's, once matched
        /// Its draws, by kind ("L 0 9", "F 100 400": a float of time in ms).
        std::map<std::string, std::deque<double>> draws;
    };
    struct
    {
        std::vector<GameMonster> game;
        std::map<int, int> byActor; ///< actor -> index into `game`
        int given = 0, own = 0;     ///< draws: the game's, the port's own
    } mons;

    std::string drawKey(bool ms, double low, double high)
    {
        char key[64];
        if (ms)
            std::snprintf(key, sizeof key, "F %ld %ld", std::lround(low * 1000), std::lround(high * 1000));
        else
            std::snprintf(key, sizeof key, "L %ld %ld", std::lround(low), std::lround(high));
        return key;
    }

    /// The map's class name of an actor's kind.
    const char *className(std::uint8_t kind)
    {
        for (const KindName &k : kKindNames)
        {
            if (static_cast<std::uint8_t>(k.kind) == kind && std::strncmp(k.name, "monster_", 8) == 0)
                return k.name;
        }
        // (The people killall does not know, as the game's log names them.)
        switch (static_cast<ActorKind>(kind))
        {
        case ActorKind::Gman: return "monster_gman";
        case ActorKind::Sitter: return "monster_sitting_scientist";
        case ActorKind::Generic: return "monster_generic";
        default: return "-";
        }
    }
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
    std::vector<std::pair<std::string, std::array<double, 6>>> gameTurning;
    // How hard the game was: played the same here. (A log without it was
    // the game's default, easy; with no log at all, the port's, medium.)
    int gameSkill = kSkillMedium;
    if (argc > 5)
    {
        std::ifstream log(argv[5]);
        std::string line;
        std::vector<Order> ran;
        gameSkill = kSkillEasy;
        while (std::getline(log, line))
        {
            if (line.rfind("#skill ", 0) == 0)
            {
                const int level = std::atoi(line.c_str() + 7);
                if (level >= kSkillEasy && level <= kSkillHard)
                    gameSkill = level;
                continue;
            }
            if (line.rfind("rot,1,", 0) == 0)
            {
                // rot,frame,seq,t,id,ax,ay,az,wx,wy,wz: the turning brushes
                // as the game had them before its first frame's physics.
                std::istringstream in(line);
                std::string field[11];
                for (std::string &f : field)
                    std::getline(in, f, ',');
                std::array<double, 6> v{};
                for (int k = 0; k < 6; ++k)
                    v[k] = std::atof(field[5 + k].c_str());
                gameTurning.push_back({field[4], v});
                continue;
            }
            if (line.rfind("mstart,", 0) == 0 || line.rfind("mon,", 0) == 0 || line.rfind("rnd,", 0) == 0)
            {
                std::istringstream in(line);
                std::vector<std::string> field;
                for (std::string f; std::getline(in, f, ',');)
                    field.push_back(f);
                const int id = std::atoi(field[4].c_str());
                auto it = std::find_if(mons.game.begin(), mons.game.end(), [&](const GameMonster &m) { return m.id == id; });
                if (field[0] == "mstart")
                {
                    // mstart,frame,seq,t,id,cls,sx,sy,sz,start,first
                    if (it == mons.game.end())
                        it = mons.game.insert(mons.game.end(), GameMonster{});
                    it->id = id;
                    it->cls = field[5];
                    for (int k = 0; k < 3; ++k)
                        it->at[k] = std::atof(field[6 + k].c_str());
                }
                else if (it != mons.game.end() && field[0] == "mon")
                {
                    const int frame = std::atoi(field[1].c_str());
                    if (frame > 0 && it->firstThink == 0)
                        it->firstThink = frame;
                }
                else if (it != mons.game.end() && std::atoi(field[1].c_str()) > 0)
                {
                    // rnd,frame,seq,t,id,kind,lo,hi,value
                    const bool ms = field[5] == "F";
                    it->draws[drawKey(ms, std::atof(field[6].c_str()), std::atof(field[7].c_str()))].push_back(
                        std::atof(field[8].c_str()));
                }
                continue;
            }
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
    // The game's node graph of the map (diag.sh copies it beside the map),
    // and the monsters' models, from the game's folder.
    {
        std::filesystem::path nod(argv[1]);
        nod.replace_extension(".nod");
        std::vector<std::uint8_t> bytes;
        if (std::filesystem::exists(nod) && hlds::readFile(nod, bytes, error) &&
            !hlds::parseNodGraph(bytes, g.nodGraph, error))
            std::fprintf(stderr, "diag_replay: %s: %s\n", nod.string().c_str(), error.c_str());
    }
    globals.set(kGlobalSkill, gameSkill);
    // DIAG_GAME (diag.sh, a scenario's "# game"): which game it is, as the
    // console's choice sets it -- HLDS_VALVE_DIR is then that game's folder.
    if (const char *name = std::getenv("DIAG_GAME"); name != nullptr && name[0] != '\0')
    {
        const GameId id = gameFromFolder(name);
        if (id == kGameCount)
        {
            std::fprintf(stderr, "diag_replay: no game %s\n", name);
            return 1;
        }
        activeGame = id;
    }
    finish(g, &globals, std::getenv("HLDS_VALVE_DIR"));
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
    // (DIAG_UNARMED, a scenario's "# unarmed": nothing given, as the real
    // game's run gives nothing -- impulse 101 gives the suit.)
    if (std::getenv("DIAG_UNARMED") == nullptr)
    {
        for (const char *thing : kImpulse101)
            give(g.player, thing);
        g.player.inv.current = kWeaponCrowbar;
    }
    g.logic.noAutoaim.value = true;
    bool useWas = false; // (USE held the frame before: a press is its first frame)
    int picked = 0; // (a weapon_<name> asked for, for the next weapons tick)
    LampState lamp;  // (the suit's lamp: "impulse 100" switches it)
    int lampRest = 0;
    // DIAG_DEFERRED: the monsters' long thinking (cover, a way to see the
    // enemy, their walks' plans) asked of a worker and answered between
    // frames, as the console's is -- every question waiting answered after
    // each frame, a worker with all the time it wants: what is measured is
    // how late the asking makes them, not the worker. "dsi": answers taken
    // up the moment they are in (ThinkQueue::answersAtOnce), as a DSi does.
    const char *deferredMode = std::getenv("DIAG_DEFERRED");
    const bool deferred = deferredMode != nullptr && deferredMode[0] != '\0' && std::strcmp(deferredMode, "0") != 0;
    // DIAG_DEFER_EVERY=N, DIAG_DEFER_MOST=M: a slow worker, as a console's
    // can be on a heavy pass -- questions answered only every Nth frame, and
    // no more than M of them then.
    const int deferEvery = std::getenv("DIAG_DEFER_EVERY") != nullptr ? std::max(1, std::atoi(std::getenv("DIAG_DEFER_EVERY"))) : 1;
    const int deferMost = std::getenv("DIAG_DEFER_MOST") != nullptr ? std::max(1, std::atoi(std::getenv("DIAG_DEFER_MOST"))) : 1 << 30;
    std::vector<ThinkSlot> thinkSlots(static_cast<std::size_t>(g.actors.count));
    ThinkQueue thinkQueue;
    if (deferred)
    {
        thinkQueue.slots = thinkSlots.data();
        thinkQueue.count = g.actors.count;
        thinkQueue.deferred = true;
        thinkQueue.answersAtOnce = std::strcmp(deferredMode, "dsi") == 0;
        g.actors.think = &thinkQueue;
        std::printf("diag_replay: thinking deferred%s\n", thinkQueue.answersAtOnce ? ", answers taken at once (a DSi)" : "");
    }
    const auto run = [&](const std::string &text) {
        std::istringstream in(text);
        std::string verb, arg;
        in >> verb >> arg;
        if (verb == "god")
            cheats.god = !cheats.god;
        else if (verb == "give" && give(g.player, arg))
            ;
        else if (verb == "impulse" && arg == "100")
            lampToggle(lamp, true);
        else if (verb == "impulse" && arg == "101")
        {
            for (const char *thing : kImpulse101)
                give(g.player, thing);
        }
        else if (weaponNamed(verb) != kWeaponNone)
            picked = weaponNamed(verb);
        else if (verb == "diag_fire")
        {
            // (A map's target fired as its own trigger would: the fork's
            // diag_fire, dlls/client.cpp.)
            const std::uint16_t id = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash(arg.c_str()));
            if (id != 0)
                logicFire(g.logic, g.player, id, -1, kUseToggle, 0);
            else
                std::fprintf(stderr, "diag_replay: no target '%s'\n", arg.c_str());
        }
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
    std::fprintf(out, "#cols pl frame seq t ox oy oz vx vy vz pitch yaw punchp punchy og duck ground water mt lad lamp bat\n");
    std::fprintf(out, "#cols lamp frame seq t ex ey ez dist hit lx ly lz\n");
    std::fprintf(out, "#cols phys frame seq t sframe ft\n");
    std::fprintf(out, "#cols box frame seq t id cx cy cz vx vy vz og ground water\n");
    std::fprintf(out, "#cols touch frame seq t id who push result tvx tvy bvx0 bvy0 bvx bvy\n");
    std::fprintf(out, "#cols lad frame seq t id hit frac nx ny nz onfloor fwd right\n");
    std::fprintf(out, "#cols ccmd frame seq t text\n");
    std::fprintf(out, "#cols rot frame seq t id ax ay az wx wy wz\n");
    std::fprintf(out, "#cols wpn frame seq t cls clip ammo1 ammo2 next1 next2 idle reload\n");
    std::fprintf(out, "#cols proj frame seq t id cls sub ox oy oz vx vy vz mt og water\n");
    std::fprintf(out, "#cols shot frame seq t n seed sx sy sz ax ay az x y cx cy ex ey ez frac hit\n");
    std::fprintf(out, "#cols boom frame seq t x y z damage radius type by\n");
    std::fprintf(out, "#cols decal frame seq t x y z nx ny nz family name on\n");
    std::fprintf(out, "#cols dmg frame seq t amount type by fx fy fz vx0 vy0 vz0 vx vy vz hp0 hp ap0 ap\n");
    std::fprintf(out, "#cols mstart frame seq t id cls sx sy sz start first\n");
    std::fprintf(out, "#cols mon frame seq t id ox oy oz yaw state ideal sched ti tid cond enemy act seq hp next\n");
    std::fprintf(out, "#cols aiev frame seq t id what name a b\n");
    std::fprintf(out, "#cols say frame seq t id cls name\n");
    std::fprintf(out, "#cols rnd frame seq t id kind lo hi value\n");
    std::fprintf(out, "#cols cam frame seq t name ox oy oz ax ay az wx wy\n");
    // The monsters: each matched to the game's of its class that started
    // nearest (ids in the port's rows are its own actors') -- those there
    // at the start, and those that come later (a monstermaker's, in a place
    // of its own or one a dead child left), as each comes.
    std::vector<char> started(static_cast<std::size_t>(g.actors.count), 0);
    auto startActor = [&](int i, int frame, double t) {
        const ActorState &actor = g.actorStates[i];
        const char *cls = className(actor.kind);
        std::fprintf(out, "mstart,%d,%d,%.4f,%d,%s,%.3f,%.3f,%.3f,0.0000,0.0000\n", frame, frame, t, i, cls,
                     units(actor.pos[0]), units(actor.pos[1]), units(actor.pos[2]));
        started[static_cast<std::size_t>(i)] = 1;
        mons.byActor.erase(i);
        int best = -1;
        double bestD = 64.0 * 64.0;
        for (std::size_t n = 0; n < mons.game.size(); ++n)
        {
            const GameMonster &m = mons.game[n];
            if (m.actor >= 0 || m.cls != cls)
                continue;
            const double dx = m.at[0] - units(actor.pos[0]), dy = m.at[1] - units(actor.pos[1]),
                         dz = m.at[2] - units(actor.pos[2]);
            if (dx * dx + dy * dy + dz * dz < bestD)
            {
                bestD = dx * dx + dy * dy + dz * dz;
                best = static_cast<int>(n);
            }
        }
        if (best >= 0)
        {
            mons.game[best].actor = i;
            mons.byActor[i] = best;
        }
    };
    // (Come into play: active, thinking, not dead. A place a dead one
    // leaves is free to be counted again when someone new is in it.)
    auto comes = [&](int frame, double t) {
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &actor = g.actorStates[i];
            const bool here = actor.active && aiKindOf(actor.kind) != nullptr && actor.ai < kAiDying;
            if (here && !started[static_cast<std::size_t>(i)])
                startActor(i, frame, t);
            else if (!here && started[static_cast<std::size_t>(i)] && (!actor.active || actor.ai >= kAiDead))
                started[static_cast<std::size_t>(i)] = 0;
        }
    };
    comes(0, 0.0);
    // The lines the map's people say, by the game's names: each cooked line
    // known by its words (the game's sentences.txt), as the game's log has
    // them ("say" rows: who, and which line).
    std::vector<std::string> lineName(g.view.sentenceCount, "?");
    {
        auto plain = [](std::string word) {
            for (char &ch : word)
                ch = ch == '/' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            const std::size_t dot = word.find('.');
            return dot == std::string::npos ? word : word.substr(0, dot);
        };
        std::ifstream file(std::string(std::getenv("HLDS_VALVE_DIR") != nullptr ? std::getenv("HLDS_VALVE_DIR") : ".") +
                               "/sound/sentences.txt",
                           std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::map<std::string, std::string> byWords;
        for (const auto &[name, line] : hlds::parseSentences(text).lines)
        {
            std::string key;
            for (const std::string &word : line.words)
                key += plain(word) + " ";
            byWords.emplace(key, name);
        }
        for (std::uint32_t i = 0; i < g.view.sentenceCount; ++i)
        {
            const SentenceRecord &record = g.view.sentences[i];
            std::string key;
            for (int w = 0; w < record.wordCount; ++w)
                key += plain(g.view.sound(g.view.sentenceWords[record.firstWord + w].sound).name) + " ";
            const auto found = byWords.find(key);
            if (found != byWords.end())
                lineName[i] = found->second;
        }
    }
    std::size_t saidSoFar = 0;
    diag::random = [](int actor, diag::RandomKind kind, int low, int high, int drawn) {
        if (now.frame < 1)
            return drawn;
        // (A float's in the game's units: seconds, or units for a Q12.)
        const bool real = kind != diag::kRandomLong;
        const double unit = kind == diag::kRandomMs ? 1000.0 : kind == diag::kRandomFx ? 4096.0 : 1.0;
        int value = drawn;
        bool given = false;
        auto it = mons.byActor.find(actor);
        if (it != mons.byActor.end())
        {
            auto &stream = mons.game[it->second].draws[drawKey(real, low / unit, high / unit)];
            if (!stream.empty())
            {
                value = static_cast<int>(std::lround(stream.front() * unit));
                stream.pop_front();
                given = true;
            }
        }
        ++(given ? mons.given : mons.own);
        if (real)
            std::fprintf(now.out, "rnd,%d,%d,%.4f,%d,F,%.6f,%.6f,%.6f\n", now.frame, now.frame, now.t, actor, low / unit,
                         high / unit, value / unit);
        else
            std::fprintf(now.out, "rnd,%d,%d,%.4f,%d,L,%d,%d,%d\n", now.frame, now.frame, now.t, actor, low, high, value);
        return value;
    };
    diag::monster = [](const diag::Monster &m) {
        if (now.frame < 1)
            return;
        // (A script's rows come every frame it holds the actor: one a tenth
        // of a second is written, as the game's thinks write theirs.)
        static std::map<int, int> scriptRow;
        const std::size_t named = m.schedule != nullptr ? std::strlen(m.schedule) : 0;
        if (named > 6 && std::strcmp(m.schedule + named - 6, "Script") == 0)
        {
            auto last = scriptRow.find(m.index);
            if (last != scriptRow.end() && now.frame - last->second < 6)
                return;
            scriptRow[m.index] = now.frame;
        }
        // (The player is the game's entity 1.)
        const int enemy = m.enemy == kAiPlayer ? 1 : m.enemy;
        std::fprintf(now.out, "mon,%d,%d,%.4f,%d,%.3f,%.3f,%.3f,%.3f,%d,%d,%s,%d,%d,%08x,%d,%d,%d,%.2f,%.4f\n", now.frame,
                     now.frame, now.t, m.index, units(m.pos[0]), units(m.pos[1]), units(m.pos[2]), m.yaw * 360.0 / 65536.0,
                     m.state, m.ideal, m.schedule != nullptr ? m.schedule : "-", m.taskIndex, m.task,
                     static_cast<unsigned>(m.conditions), enemy, m.activity, m.clip, static_cast<double>(m.health),
                     now.t + m.thinkMs / 1000.0);
    };
    // (A route: its goal, as the game's row has it after its first step --
    // the port's has no steps of its own.)
    diag::route = [](int index, const std::int32_t *goal, int goalType) {
        if (now.frame < 1)
            return;
        std::fprintf(now.out, "aiev,%d,%d,%.4f,%d,route,>%.1f %.1f %.1f,0,%d\n", now.frame, now.frame, now.t, index,
                     units(goal[0]), units(goal[1]), units(goal[2]), goalType);
        // (... and the nodes it would go by, as the game's "path" row: the
        // game's are its route's node steps, the port's those its plan
        // takes a step at a time. Whether it walks straight instead is the
        // port's to say: a, 1 if a straight walk would be tried first.)
        const Game &g = *now.game;
        const ActorState &me = g.actors.states[index];
        const fx d[3] = {goal[0] - me.pos[0], goal[1] - me.pos[1], 0};
        const bool shortWay = fxLength(d) < fxFromInt(256);
        const std::string chain = nodeChain(g, index, goal);
        if (!chain.empty())
            std::fprintf(now.out, "aiev,%d,%d,%.4f,%d,path,%s,%d,%d\n", now.frame, now.frame, now.t, index, chain.c_str(),
                         shortWay ? 1 : 0, goalType);
    };
    diag::aiEvent = [](int index, const char *what, const char *name, int a, int b) {
        if (now.frame < 1)
            return;
        std::fprintf(now.out, "aiev,%d,%d,%.4f,%d,%s,%s,%d,%d\n", now.frame, now.frame, now.t, index, what,
                     name != nullptr && name[0] ? name : "-", a, b);
    };
    diag::ladder = [](const diag::Ladder &seen) {
        frameLadder.on = true;
        frameLadder.seen = seen;
    };
    now.out = out;
    now.game = &g;
    now.names = &names;
    // A round: what the gun says as it fires, written once the port's own
    // code has said where it stopped (a monster, a brush, the world).
    diag::shot = [](const diag::Shot &shot) {
        if (now.frame < 1)
            return;
        Round &r = now.round;
        r.pending = true;
        for (int a = 0; a < 3; ++a)
        {
            r.from[a] = shot.from[a];
            r.aim[a] = shot.aim[a];
            r.dir[a] = shot.dir[a];
        }
        r.range = shot.range;
        r.n = shot.n;
        r.drawX = shot.drawX;
        r.drawY = shot.drawY;
        r.cone = shot.cone;
    };
    diag::landed = [](const diag::Landed &landed) {
        Round &r = now.round;
        if (!r.pending || now.frame < 1)
            return;
        r.pending = false;
        const double frac = static_cast<double>(landed.fraction) / kFractionOne;
        double e[3];
        for (int a = 0; a < 3; ++a)
            e[a] = units(r.from[a]) + units(r.dir[a]) * r.range * frac;
        std::string hit = "-";
        if (landed.actor >= 0)
            hit = className(now.game->actorStates[landed.actor].kind);
        else if (frac < 1.0)
            hit = landed.mover >= 0 ? (*now.names)[landed.mover] : "world";
        std::fprintf(now.out, "shot,%d,%d,%.4f,%d,%u,%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.3f,%.3f,%.3f,%.5f,%s\n",
                     now.frame, now.frame, now.t, r.n, now.seed, units(r.from[0]), units(r.from[1]), units(r.from[2]),
                     units(r.aim[0]), units(r.aim[1]), units(r.aim[2]), r.drawX / 65536.0, r.drawY / 65536.0,
                     r.cone / 4096.0, r.cone / 4096.0, e[0], e[1], e[2], frac, hit.c_str());
    };
    diag::boom = [](const diag::Boom &boom) {
        if (now.frame < 1)
            return;
        std::fprintf(now.out, "boom,%d,%d,%.4f,%.3f,%.3f,%.3f,%d.00,%d.00,%u,-\n", now.frame, now.frame, now.t,
                     units(boom.at[0]), units(boom.at[1]), units(boom.at[2]), boom.damage, boom.radius, boom.type);
    };
    // A mark: as the game's row has it, in the world (a brush's own from
    // where the brush is now), its family named as the game's decals are.
    diag::mark = [](const diag::Mark &m) {
        if (now.frame < 1)
            return;
        double at[3];
        for (int a = 0; a < 3; ++a)
            at[a] = units(m.at[a]) + (m.brush >= 0 ? units(now.game->brushStates[m.brush].pos[a]) : 0.0);
        const int family = decalFamily(static_cast<std::uint8_t>(m.picture));
        const char *name = m.picture == kDecalSign ? "sign"
                           : family == 0 ? "shot"
                           : family == 4 ? "bigshot"
                           : family == 5 ? "blood"
                           : family == 8 ? "yblood"
                           : family == 9 ? "break"
                           : family == 10 ? "scorch"
                           : family == 12 ? "spit"
                                         : "wall";
        std::fprintf(now.out, "decal,%d,%d,%.4f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%s,%d%s,%s\n", now.frame, now.frame, now.t,
                     at[0], at[1], at[2], units(m.normal[0]), units(m.normal[1]), units(m.normal[2]), name, m.picture,
                     m.moved ? "m" : "", m.brush >= 0 ? (*now.names)[m.brush].c_str() : "world");
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
    const int linksAt = std::getenv("DIAG_LINKS") != nullptr ? std::atoi(std::getenv("DIAG_LINKS")) : 0;
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
        // DIAG_LINKS=<frame>: the node links that have a brush in them, as
        // the port has them then (after a few of the logic's link looks):
        // the two nodes, the brush's entity, and how shut the link is.
        if (linksAt != 0 && f == linksAt)
        {
            const ActorWorld &a = g.actors;
            for (int n = 0; n < a.nodeCount; ++n)
            {
                const PathNode &node = a.nodes[n];
                for (int l = node.firstLink; l < node.firstLink + node.linkCount && l < a.linkCount; ++l)
                {
                    const PathLink &link = a.links[l];
                    if (link.brush == kNoBrush || link.to < n)
                        continue;
                    const PathNode &to = a.nodes[link.to];
                    const std::string &model = link.brush < names.size() ? names[link.brush] : std::string("?");
                    auto keys = entityOfModel(g, model);
                    std::printf("link %d>%d %.0f %.0f %.0f > %.0f %.0f %.0f size %d brush %s %s name '%s' master '%s' "
                                "flags %s shut %d\n",
                                n, link.to, units(node.pos[0]), units(node.pos[1]), units(node.pos[2]), units(to.pos[0]),
                                units(to.pos[1]), units(to.pos[2]), link.flags, model.c_str(), keys["classname"].c_str(),
                                keys["targetname"].c_str(), keys["master"].c_str(), keys["spawnflags"].c_str(),
                                a.linkShut != nullptr ? a.linkShut[l] : -1);
                }
            }
            // DIAG_ROUTES=1 too: every node, and the first step pathStep
            // takes from each to each, for a person's size, by one that
            // opens doors and by one that does not (tools/ref/nodroutes.py
            // holds them to the game's routing tables).
            if (std::getenv("DIAG_ROUTES") != nullptr)
            {
                for (int n = 0; n < a.nodeCount; ++n)
                    std::printf("node %d %.1f %.1f %.1f\n", n, units(a.nodes[n].pos[0]), units(a.nodes[n].pos[1]),
                                units(a.nodes[n].pos[2]));
                for (int opens = 0; opens < 2; ++opens)
                    for (int from = 0; from < a.nodeCount; ++from)
                        for (int to = 0; to < a.nodeCount; ++to)
                            std::printf("next %d %d %d %d\n", opens, from, to,
                                        from == to ? from : pathStep(a, from, to, kPathLinkHuman, opens != 0));
            }
            for (int i = 0; i < a.count; ++i)
                std::printf("actor %d %s %.0f %.0f %.0f\n", i, className(a.states[i].kind),
                            units(a.states[i].pos[0]), units(a.states[i].pos[1]), units(a.states[i].pos[2]));
        }
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
        // move), USE, the player's move, the weapons, then the monsters.
        // The turning brushes as the game had them at its first frame: they
        // have turned since each map began, from a moment the two sides do
        // not share.
        if (f == 1)
        {
            for (std::uint32_t r = 0; r < g.view.logicCount; ++r)
            {
                const LogicRecord &record = g.view.logic[r];
                if (record.kind != static_cast<std::uint8_t>(LogicKind::Rotating) || record.brush == kNoBrush)
                    continue;
                for (const auto &[id, v] : gameTurning)
                {
                    if (id != names[record.brush])
                        continue;
                    const int axis = static_cast<int>(record.extra[0] % 3);
                    LogicState &state = g.states[r];
                    state.bits = static_cast<std::uint16_t>(std::lround(v[axis] * 65536.0 / 360.0));
                    state.speed = static_cast<fx>(std::lround(v[3 + axis] * 65536.0 / 360.0));
                    g.brushStates[record.brush].angles[axis] = static_cast<std::uint16_t>(state.bits);
                }
            }
        }
        // The monsters' thinks, set to fall in the frames the game's first
        // did (they have thought since the map began, from a moment the two
        // sides do not share).
        if (f == 1 && step == 1)
        {
            for (const GameMonster &gm : mons.game)
            {
                AiMonster *m = gm.actor >= 0 ? aiOf(g.actors, gm.actor) : nullptr;
                if (m == nullptr || !m->started || gm.firstThink < 1)
                    continue;
                // (This frame's tick takes `ms` off, and each after its own:
                // at the first think's frame it comes to 0 or less.)
                FrameClock ahead = clock;
                std::int32_t sum = ms;
                for (int k = 2; k < gm.firstThink; ++k)
                    sum += ahead.ms(1);
                m->thinkMs = sum + 1;
            }
        }
        g.logic.useHeld.value = useHeld;
        itemsTouch(g.logic, g.player);
        logicTick(g.logic, g.player, ms);
        g.logic.viewTurn = 0;
        if (f >= 1)
            comes(f, t);
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
        int pulled = -1; // (pulled after the move, as Game::frame does)
        if (useHeld && g.player.health > 0)
        {
            const int target = logicFindUsable(g.logic, eye, forward);
            const bool box =
                target >= 0 && g.logic.records[target].kind == static_cast<std::uint8_t>(LogicKind::Pushable);
            // The press itself (Game::frame's usePressed): a button, a door,
            // a lift's call, used once; else a scientist or a guard (follow,
            // or stay: personUse); else a box, pulled while it is held.
            if (!useWas && target >= 0 && !box)
                logicUse(g.logic, g.player, target, -1, kUseSet);
            else if (!useWas && personUse(g.logic, g.player, eye, forward))
            {
            }
            else if (box)
                pulled = target;
        }
        useWas = useHeld;
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
        // The weapons, after the move (Game::frame's order, the game's
        // PostThink): fired from where the player now is.
        {
            const fx eyeNow[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
            const bool alive = g.player.health > 0;
            const bool reloadPressed = (span.buttons & kInReload) && !(lastButtons & kInReload);
            lastButtons = span.buttons;
            // (The command's seed: the game's, if its log is given; else
            // counted, as the console does.)
            const std::uint32_t seed = now.seed != 0 ? now.seed : static_cast<std::uint32_t>(frameAll);
            weaponsTick(g.logic, g.player, weapon, eyeNow, forward, right, up, alive && (span.buttons & kInAttack), false,
                        ms, alive && (span.buttons & kInAttack2), alive ? picked : 0, alive && reloadPressed, seed);
            picked = 0;
            weapon.anim = nullptr;
            g.actors.got = 0;
            g.logic.brokeCount = 0;
            g.drain();
        }
        if (f >= 1)
        {
            // What has been thrown or fired, before the frame's physics moves it
            // (as the game's log has it, at its StartFrame): a thing let go this
            // frame where it was let go.
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
                             // (One that moves as a box: where its feet are, the game's origin.)
                             units(shot.pos[2] - (shotMovesAsBox(shot.kind) ? kCrouchedFeet : 0)), units(shot.velocity[0]),
                             units(shot.velocity[1]), units(shot.velocity[2]));
            }
        }
        // The monsters, and what is thrown or fired, after the player's move
        // and weapons (Game::frame's order, the game's: each command, then
        // the entities' thinks).
        monstersTick(g.logic, g.player, ms);
        if (deferred && f % deferEvery == 0)
        {
            for (int n = 0; n < deferMost && thinkRunOne(g.logic); ++n)
            {
            }
        }
        g.drain();
        for (; saidSoFar < g.said.size(); ++saidSoFar)
        {
            const std::uint16_t line = g.said[saidSoFar], by = g.saidBy[saidSoFar];
            const int actor = (by & kSoundTagActor) ? (by & 0x7FFF) : -1;
            if (f >= 1)
                std::fprintf(out, "say,%d,%d,%.4f,%d,%s,%s\n", f, f, t, actor,
                             actor >= 0 && actor < g.actors.count ? className(g.actorStates[actor].kind) : "-",
                             line < lineName.size() ? lineName[line].c_str() : "?");
        }

        lampTick(lamp, ms, &lampRest);
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
        // The lamp: where it shines (Game::frame's). (Its battery is
        // ticked from the first frame settled, as the game's charges then.)
        // (mt: the game's movetype, 5 on a ladder, 3 walking.)
        std::fprintf(out, "pl,%d,%d,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,0,0,%d,%d,%s,%d,%d,%s,%d,%d\n", f, f, t,
                     units(p.origin[0]), units(p.origin[1]), units(p.origin[2]), units(p.velocity[0]),
                     units(p.velocity[1]), units(p.velocity[2]), span.pitch, span.yaw, p.onGround ? 1 : 0,
                     p.ducked ? 1 : 0, groundName(p).c_str(), p.waterLevel, p.onLadder ? 5 : 3, ladder.c_str(),
                     lamp.on ? 1 : 0, lampPercent(lamp));
        if (lamp.on)
        {
            const fx eyeLamp[3] = {p.origin[0], p.origin[1], p.origin[2] + p.viewHeight};
            const LampAim aim = lampAimAll(g.move, g.actors, eyeLamp, forward);
            const double reach = aim.hit ? aim.distance : kLampReach;
            // (Where the line stopped; and the light, there -- or, on a
            // monster, at its origin.)
            double end[3], light[3];
            for (int a = 0; a < 3; ++a)
            {
                end[a] = units(eyeLamp[a]) + units(forward[a]) * reach;
                light[a] = aim.hit ? units(aim.at[a]) : end[a];
            }
            const std::string hit = aim.actor >= 0 ? std::string("models/") + className(g.actorStates[aim.actor].kind) + ".mdl"
                                                   : aim.hit ? "world" : "-";
            std::fprintf(out, "lamp,%d,%d,%.4f,%.3f,%.3f,%.3f,%.3f,%s,%.3f,%.3f,%.3f\n", f, f, t, end[0], end[1], end[2],
                         reach, hit.c_str(), light[0], light[1], light[2]);
        }
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
        // The turning brushes: their angles and speeds, in degrees.
        for (std::uint32_t r = 0; r < g.view.logicCount; ++r)
        {
            const LogicRecord &record = g.view.logic[r];
            if (record.kind != static_cast<std::uint8_t>(LogicKind::Rotating) || record.brush == kNoBrush)
                continue;
            const int axis = static_cast<int>(record.extra[0] % 3);
            double a[3] = {0, 0, 0}, w[3] = {0, 0, 0};
            a[axis] = static_cast<std::uint16_t>(g.states[r].bits) * 360.0 / 65536.0;
            w[axis] = g.states[r].speed * 360.0 / 65536.0;
            std::fprintf(out, "rot,%d,%d,%.4f,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", f, f, t, names[record.brush].c_str(),
                         a[0], a[1], a[2], w[0], w[1], w[2]);
        }
        // The camera looked through (as the game logs a trigger_camera that
        // is thinking): where, and which way -- its pitch down positive, as
        // the game's angles have it; its yaw 0..360. No name: "-".
        if (g.logic.camera >= 0)
        {
            const LogicState &cam = g.states[g.logic.camera];
            const double yaw = (static_cast<std::uint32_t>(cam.travel[0]) >> 16) * 360.0 / 65536.0;
            const double pitch = -(cam.travel[1] / 65536.0) * 360.0 / 65536.0;
            std::fprintf(out, "cam,%d,%d,%.4f,-,%.3f,%.3f,%.3f,%.3f,%.3f,0.000,0.000,0.000\n", f, f, t, units(cam.base[0]),
                         units(cam.base[1]), units(cam.base[2]), pitch, yaw);
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
    }
    std::fprintf(out, "END,%d,%d,%.4f\n", scripted, scripted, timeMs / 1000.0);
    if (!mons.game.empty())
    {
        int matched = 0, left = 0;
        for (const GameMonster &m : mons.game)
        {
            matched += m.actor >= 0 ? 1 : 0;
            for (const auto &[key, stream] : m.draws)
                left += static_cast<int>(stream.size());
        }
        std::printf("monsters: %d of the game's %zu matched; draws: %d the game's, %d the port's own, %d of the game's "
                    "not drawn\n",
                    matched, mons.game.size(), mons.given, mons.own, left);
    }
    std::fclose(out);
    std::printf("%s: %d frames\n", argv[3], scripted);
    if (deferred)
        std::printf("diag_replay: answers taken %u, waited %u frames each, longest %u, dropped %u, gave up %u\n",
                    thinkQueue.taken, thinkQueue.taken != 0 ? thinkQueue.takenFrames / thinkQueue.taken : 0u,
                    thinkQueue.longest, thinkQueue.dropped, thinkQueue.gaveUp);
    return 0;
}
