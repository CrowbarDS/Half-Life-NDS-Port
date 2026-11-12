#pragma once

// The COMMAND line: what is typed on the pause menu's keyboard, read as the
// game's own console reads it -- a word, then what it is given, with the
// game's names and aliases ("impulse 101" is everything). This only reads
// the line; source/main.cpp does what it says.
//
// std-only and inline: the host tests run it (test_commands).

#include <cstdint>
#include <cstring>

#include "HlLevel.hpp"

namespace hldata
{
    enum class Command : std::uint8_t
    {
        Unknown,
        Help,
        Map,         ///< map <name>: the map anew, what the player had gone
        ChangeLevel, ///< changelevel <name>: the map, with what the player carries
        GiveAll,     ///< give (alone), impulse 101
        Give,        ///< give <item>
        Heal,
        Health,      ///< health <n>
        Kill,        ///< the player dies
        God,
        Notarget,
        Noclip,
        Save,        ///< quick save
        Load,        ///< quick load
        Restart,     ///< the map again from its start
        Flashlight,  ///< impulse 100
        Spray,       ///< impulse 201: a match's spray on the wall ahead
        Fov,         ///< fov <n>
        Crosshair,   ///< crosshair 0|1
        Gravity,     ///< sv_gravity <n>
        Timescale,   ///< host_timescale <x>
        KillAll,     ///< killall <class>
        BarnacleKill, ///< barnaclekill: a barnacle that grabs the player dies of it
        Usage,        ///< <name> help, help <name>: how a command is written (args[0])
        Count
    };

    struct CommandInfo
    {
        const char *name;
        Command command;
        const char *usage; ///< for help: how it is written
        const char *about; ///< ... and what it does (a console line: 32 letters at most)
    };
    /// Every name the line knows (aliases too), in the order help shows them.
    inline constexpr CommandInfo kCommands[] = {
        {"help", Command::Help, "help [command]", "the commands, or how to use one"},
        {"god", Command::God, "god", "nothing hurts you (again: off)"},
        {"notarget", Command::Notarget, "notarget", "monsters do not see you"},
        {"noclip", Command::Noclip, "noclip", "fly through walls (again: off)"},
        {"give", Command::GiveAll, "give [item]", "everything, or one weapon_/ammo_"},
        {"impulse", Command::Unknown, "impulse 101/100/201", "everything, flashlight, spray"},
        {"heal", Command::Heal, "heal", "health back to 100"},
        {"health", Command::Health, "health <n>", "health set to n (1..999)"},
        {"kill", Command::Kill, "kill", "you die"},
        {"killall", Command::KillAll, "killall <monster>", "kills a kind, or all: monsters"},
        {"barnaclekill", Command::BarnacleKill, "barnaclekill", "a barnacle that grabs you dies"},
        {"map", Command::Map, "map <name>", "starts a map anew (c1a0...)"},
        {"changelevel", Command::ChangeLevel, "changelevel <name>", "a map, keeping what you carry"},
        {"restart", Command::Restart, "restart", "this map again from its start"},
        {"save", Command::Save, "save", "quick save"},
        {"load", Command::Load, "load", "loads the quick save"},
        {"fov", Command::Fov, "fov <60..110>", "how wide the view is, degrees"},
        {"crosshair", Command::Crosshair, "crosshair 0/1", "the crosshair off or on"},
        {"sv_gravity", Command::Gravity, "sv_gravity <n>", "gravity (800 is the game's)"},
        {"host_timescale", Command::Timescale, "host_timescale <x>", "game speed (0.25..2)"},
    };

    /// Whether `command` is a cheat: what the game would only let through
    /// with sv_cheats on, and what the Cheats setting lets through here.
    /// (impulse 101 is everything, a cheat; impulse 100 the flashlight, not.)
    inline bool commandIsCheat(Command command)
    {
        switch (command)
        {
        case Command::GiveAll:
        case Command::Give:
        case Command::Heal:
        case Command::Health:
        case Command::God:
        case Command::Notarget:
        case Command::Noclip:
        case Command::BarnacleKill:
        case Command::KillAll:
        case Command::Gravity:
        case Command::Timescale:
            return true;
        default:
            return false;
        }
    }

    /// The entry of the command called `name`, or null.
    inline const CommandInfo *commandInfo(const char *name)
    {
        for (const CommandInfo &info : kCommands)
        {
            if (std::strcmp(info.name, name) == 0)
                return &info;
        }
        return nullptr;
    }
    inline constexpr int kCommandNames = static_cast<int>(sizeof(kCommands) / sizeof(kCommands[0]));

    /// A line, read: what it asks for, and up to three words after it.
    struct CommandLine
    {
        Command command = Command::Unknown;
        char verb[24] = {};
        char args[3][24] = {};
        int argc = 0;
    };

    /// Read `line`: lower case, split on spaces (a word too long is cut).
    inline CommandLine parseCommand(const char *line)
    {
        CommandLine out;
        int word = -1, length = 0;
        bool inWord = false;
        for (const char *p = line; p != nullptr && *p != '\0'; ++p)
        {
            char ch = *p;
            if (ch == ' ' || ch == '\t')
            {
                inWord = false;
                continue;
            }
            if (!inWord)
            {
                inWord = true;
                ++word;
                length = 0;
                if (word > 3)
                    break;
                out.argc = word;
            }
            if (ch >= 'A' && ch <= 'Z')
                ch = static_cast<char>(ch - 'A' + 'a');
            char *into = word == 0 ? out.verb : out.args[word - 1];
            if (length < 23)
            {
                into[length++] = ch;
                into[length] = '\0';
            }
        }
        for (const CommandInfo &info : kCommands)
        {
            if (std::strcmp(info.name, out.verb) == 0)
                out.command = info.command;
        }
        // The game's aliases and numbered impulses.
        if (std::strcmp(out.verb, "impulse") == 0)
        {
            if (std::strcmp(out.args[0], "101") == 0)
                out.command = Command::GiveAll;
            else if (std::strcmp(out.args[0], "100") == 0)
                out.command = Command::Flashlight;
            else if (std::strcmp(out.args[0], "201") == 0)
                out.command = Command::Spray;
        }
        if (out.command == Command::GiveAll && out.argc > 0 && std::strcmp(out.verb, "give") == 0)
            out.command = Command::Give;
        // How one is used: "god help" (the console's way), or "help god".
        if (std::strcmp(out.verb, "help") == 0 && out.argc > 0 && commandInfo(out.args[0]) != nullptr)
            out.command = Command::Usage;
        else if (out.argc > 0 && std::strcmp(out.args[0], "help") == 0 && std::strcmp(out.verb, "help") != 0)
        {
            out.command = Command::Usage;
            std::strcpy(out.args[0], out.verb);
        }
        return out;
    }

    /// What the console has said: its last kLines lines, each at most
    /// kColumns letters (the plain 8x8 letters across the touch screen).
    struct ConsoleLog
    {
        static constexpr int kLines = 64;
        static constexpr int kColumns = 32;
        char lines[kLines][kColumns + 1] = {};
        int first = 0; ///< where the oldest is
        int total = 0;

        int count() const { return total; }
        /// Line `i`, 0 the oldest kept.
        const char *line(int i) const { return lines[(first + i) % kLines]; }
        void add(const char *text, int length)
        {
            char *into = lines[(first + total) % kLines];
            if (total == kLines)
                first = (first + 1) % kLines;
            else
                ++total;
            length = length > kColumns ? kColumns : length;
            std::memcpy(into, text, static_cast<std::size_t>(length));
            into[length] = '\0';
        }
        /// `text`, a line at each '\n', and those longer than a line broken
        /// at a space (or anywhere, if a word is longer).
        void print(const char *text)
        {
            while (true)
            {
                const char *end = std::strchr(text, '\n');
                int length = end != nullptr ? static_cast<int>(end - text) : static_cast<int>(std::strlen(text));
                while (length > kColumns)
                {
                    int cut = kColumns;
                    while (cut > 0 && text[cut] != ' ')
                        --cut;
                    add(text, cut > 0 ? cut : kColumns);
                    const int skip = cut > 0 ? cut + 1 : kColumns;
                    text += skip;
                    length -= skip;
                }
                add(text, length);
                if (end == nullptr)
                    return;
                text = end + 1;
            }
        }
    };

    /// A number as the console writes it ("800", "-3", "0.5"), times 256.
    /// False if it is not one.
    inline bool parseNumber(const char *text, int &q8)
    {
        bool negative = false, digits = false, point = false;
        std::int64_t whole = 0, frac = 0, scale = 1;
        const char *p = text;
        if (*p == '-' || *p == '+')
            negative = *p++ == '-';
        for (; *p != '\0'; ++p)
        {
            if (*p == '.' && !point)
            {
                point = true;
                continue;
            }
            if (*p < '0' || *p > '9')
                return false;
            digits = true;
            if (point)
            {
                if (scale < 100000)
                {
                    frac = frac * 10 + (*p - '0');
                    scale *= 10;
                }
            }
            else if (whole < 1000000)
            {
                whole = whole * 10 + (*p - '0');
            }
        }
        if (!digits)
            return false;
        const std::int64_t value = whole * 256 + (frac * 256 + scale / 2) / scale;
        q8 = static_cast<int>(negative ? -value : value);
        return true;
    }

    /// The things that can be given, by the names the maps give them.
    struct ItemName
    {
        const char *name;
        ItemKind item;
    };
    inline constexpr ItemName kItemNames[] = {
        {"weapon_crowbar", ItemKind::Crowbar},       {"weapon_glock", ItemKind::Glock},
        {"weapon_9mmhandgun", ItemKind::Glock},      {"weapon_shotgun", ItemKind::Shotgun},
        {"ammo_glockclip", ItemKind::GlockClip},     {"ammo_9mmclip", ItemKind::GlockClip},
        {"ammo_buckshot", ItemKind::Buckshot},       {"item_healthkit", ItemKind::HealthKit},
        {"item_battery", ItemKind::Battery},         {"item_suit", ItemKind::Suit},
        {"weapon_handgrenade", ItemKind::Grenades},  {"weapon_9mmar", ItemKind::Mp5},
        {"weapon_mp5", ItemKind::Mp5},               {"weapon_357", ItemKind::Python},
        {"weapon_python", ItemKind::Python},         {"ammo_9mmar", ItemKind::Mp5Clip},
        {"ammo_mp5clip", ItemKind::Mp5Clip},         {"ammo_9mmbox", ItemKind::AmmoBox},
        {"ammo_argrenades", ItemKind::ArGrenades},   {"ammo_mp5grenades", ItemKind::ArGrenades},
        {"ammo_357", ItemKind::Ammo357},             {"weapon_crossbow", ItemKind::Crossbow},
        {"ammo_crossbow", ItemKind::Bolts},          {"weapon_satchel", ItemKind::Satchel},
        {"weapon_tripmine", ItemKind::Tripmine},     {"weapon_snark", ItemKind::Snark},
        {"weapon_rpg", ItemKind::Rpg},               {"ammo_rpgclip", ItemKind::RpgClip},
        {"weapon_gauss", ItemKind::Gauss},           {"ammo_gaussclip", ItemKind::GaussClip},
        {"weapon_egon", ItemKind::Egon},             {"weapon_hornetgun", ItemKind::Hornetgun},
    };
    /// ItemKind::None if there is no such thing.
    inline ItemKind itemOfName(const char *name)
    {
        for (const ItemName &item : kItemNames)
        {
            if (std::strcmp(item.name, name) == 0)
                return item.item;
        }
        return ItemKind::None;
    }

    /// The monsters killall knows: the maps' class names, and short ones.
    struct KindName
    {
        const char *name;
        ActorKind kind;
    };
    inline constexpr KindName kKindNames[] = {
        {"monster_headcrab", ActorKind::Headcrab},      {"headcrab", ActorKind::Headcrab},
        {"monster_zombie", ActorKind::Zombie},          {"zombie", ActorKind::Zombie},
        {"monster_barnacle", ActorKind::Barnacle},      {"barnacle", ActorKind::Barnacle},
        {"monster_alien_slave", ActorKind::Slave},      {"slave", ActorKind::Slave},
        {"vortigaunt", ActorKind::Slave},               {"monster_bullchicken", ActorKind::Bullsquid},
        {"bullsquid", ActorKind::Bullsquid},            {"monster_houndeye", ActorKind::Houndeye},
        {"houndeye", ActorKind::Houndeye},              {"monster_human_grunt", ActorKind::Grunt},
        {"grunt", ActorKind::Grunt},                    {"monster_miniturret", ActorKind::Miniturret},
        {"miniturret", ActorKind::Miniturret},          {"monster_sentry", ActorKind::Sentry},
        {"sentry", ActorKind::Sentry},                  {"monster_turret", ActorKind::Turret},
        {"turret", ActorKind::Turret},                  {"monster_tripmine", ActorKind::Tripmine},
        {"tripmine", ActorKind::Tripmine},              {"monster_scientist", ActorKind::Scientist},
        {"scientist", ActorKind::Scientist},            {"monster_barney", ActorKind::Guard},
        {"barney", ActorKind::Guard},                   {"guard", ActorKind::Guard},
        {"monster_cockroach", ActorKind::Cockroach},    {"roach", ActorKind::Cockroach},
        {"monster_leech", ActorKind::Leech},            {"leech", ActorKind::Leech},
        {"monster_alien_grunt", ActorKind::AlienGrunt}, {"agrunt", ActorKind::AlienGrunt},
        {"monster_alien_controller", ActorKind::Controller}, {"controller", ActorKind::Controller},
        {"monster_snark", ActorKind::Snark},            {"snark", ActorKind::Snark},
        {"monster_osprey", ActorKind::Osprey},          {"osprey", ActorKind::Osprey},
        {"monster_gargantua", ActorKind::Gargantua},    {"garg", ActorKind::Gargantua},
        {"monster_babycrab", ActorKind::BabyCrab},      {"babycrab", ActorKind::BabyCrab},
        {"monster_human_assassin", ActorKind::Assassin}, {"assassin", ActorKind::Assassin},
        {"monster_ichthyosaur", ActorKind::Ichthyosaur}, {"icky", ActorKind::Ichthyosaur},
        {"monster_tentacle", ActorKind::Tentacle},      {"tentacle", ActorKind::Tentacle},
        {"monster_apache", ActorKind::Apache},          {"apache", ActorKind::Apache},
        {"monster_bigmomma", ActorKind::BigMomma},      {"bigmomma", ActorKind::BigMomma},
        {"monster_nihilanth", ActorKind::Nihilanth},    {"nihilanth", ActorKind::Nihilanth},
        {"monster_flyer", ActorKind::Boid},             {"boid", ActorKind::Boid},
        {"monster_rat", ActorKind::Rat},                {"rat", ActorKind::Rat},
        {"monster_vortigaunt", ActorKind::Slave},       {"vortigaunt", ActorKind::Slave},
    };
    /// ActorKind::Count if there is no such monster.
    inline ActorKind kindOfName(const char *name)
    {
        for (const KindName &kind : kKindNames)
        {
            if (std::strcmp(kind.name, name) == 0)
                return kind.kind;
        }
        return ActorKind::Count;
    }
} // namespace hldata
