#include "HlSound.hpp"
#include "HlSprite.hpp"
#include "hlds/UiCook.hpp"
#include "hlds/SoundCook.hpp"
#include "hlds/SpriteCook.hpp"
#include "hlds/Converter.hpp"
#include "HlWeaponBundle.hpp"
#include "HlCharacters.hpp"

#include "hlds/AviFile.hpp"
#include "hlds/EntityCook.hpp"
#include "hlds/MdlFile.hpp"
#include "hlds/ModelCook.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <mutex>
#include <thread>
#include <cctype>
#include <set>

#include "HlArchive.hpp"
#include "HlGuest.hpp"
#include "HlLevelReader.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/BrushInfo.hpp"
#include "hlds/BspFile.hpp"
#include "hlds/Cooker.hpp"
#include "hlds/EntityParser.hpp"
#include "hlds/Materials.hpp"
#include "hlds/Pack.hpp"
#include "hlds/TextureCook.hpp"

namespace fs = std::filesystem;

namespace hlds
{
    namespace
    {
        std::string lower(std::string text)
        {
            for (char &c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }
    } // namespace

    Converter::Converter(ConvertOptions options) : options_(std::move(options)) {}

    fs::path Converter::dataDir(const fs::path &cardRoot)
    {
        return cardRoot / "_nds" / "hl_nds" / "data";
    }

    fs::path Converter::stageDir() const
    {
        if (!options_.stageRoot.empty())
            return dataDir(options_.stageRoot);
        // (A card root given with a slash at its end has no last name.)
        fs::path card = options_.cardRoot;
        if (!card.has_filename())
            card = card.parent_path();
        fs::path beside = card;
        beside += "-stage";
        return dataDir(beside);
    }

    void Converter::packInto(MapResult &result) const
    {
        PackReport report;
        std::string why;
        if (!packMap(stageDir(), dataDir(options_.cardRoot), result.name, options_.compress, report, why))
        {
            result.ok = false;
            result.error = "the map's archive was not made: " + why;
            return;
        }
        result.archiveKb = static_cast<int>(report.cardBytes / 1024);
        result.archiveRawKb = static_cast<int>(report.rawBytes / 1024);
        result.archiveMembers = report.members;
        if (report.indexBytes > hldata::kMapIndexMost)
            result.warnings.push_back("its archive's index is " + std::to_string(report.indexBytes) +
                                      " bytes, over the " + std::to_string(hldata::kMapIndexMost) +
                                      " the console keeps it in: it is mounted with its index on the heap");
    }

    void Converter::packAirGroups()
    {
        // The groups a console with no card can be sent: the maps of a game
        // between consoles (those with a place to come into one), the
        // Hazard Course's and Uplink's.
        std::vector<std::string> groups[3];
        std::error_code ec;
        for (auto it = fs::directory_iterator(stageDir() / "maps", ec); !ec && it != fs::directory_iterator();
             it.increment(ec))
        {
            if (it->path().extension() != ".hlm")
                continue;
            const std::string name = it->path().stem().string();
            if (name.rfind("t0a0", 0) == 0)
            {
                groups[1].push_back(name);
                continue;
            }
            if (name.rfind("hldemo", 0) == 0)
            {
                groups[2].push_back(name);
                continue;
            }
            std::vector<std::uint8_t> level;
            std::string ignored;
            hldata::LevelView view;
            if (!readFile(it->path(), level, ignored) ||
                view.open(level.data(), static_cast<std::uint32_t>(level.size())) != hldata::LevelError::Ok)
                continue;
            for (std::uint32_t e = 0; e < view.entityCount; ++e)
            {
                if (view.entities[e].classId == static_cast<std::uint16_t>(hldata::EntityClass::InfoPlayerDeathmatch))
                {
                    groups[0].push_back(name);
                    break;
                }
            }
        }
        {
            PackReport report;
            std::string why;
            if (!packAirCommon(stageDir(), dataDir(options_.cardRoot), report, why))
                common_.warnings.push_back("the air common archive was not made: " + why);
        }
        for (int g = 0; g < 3; ++g)
        {
            std::sort(groups[g].begin(), groups[g].end());
            AirReport report;
            std::string why;
            if (!packAir(stageDir(), dataDir(options_.cardRoot), hldata::kAirGroupNames[g + 1], groups[g], g == 0, report,
                         why))
            {
                common_.warnings.push_back(std::string("the air archives of ") + hldata::kAirGroupNames[g + 1] +
                                           " were not made: " + why);
                continue;
            }
            // What each takes of a guest's memory: its level, models and
            // sounds, and the base's (textures are in video memory).
            for (const std::string &map : groups[g])
            {
                std::size_t bytes = 0, memory = 0, bank = 0;
                std::string failed;
                // (The demos' maps, groups after the first: sent with all of
                // their bank.)
                if (packAirFull(stageDir(), dataDir(options_.cardRoot), map, bytes, memory, failed, g != 0, &bank))
                    common_.guest.push_back("  " + map + " at full quality: " + std::to_string(bytes / 1024) + " KB sent, " +
                                            std::to_string(memory / 1024) + " KB in memory" +
                                            (g != 0 ? ", bank " + std::to_string(bank / 1024) + " KB" : std::string()));
                else
                    common_.warnings.push_back("the full-quality stream of " + map + " was not made: " + failed);
            }
            const auto of = [](const std::size_t *raw, hldata::GuestKind kind) { return raw[static_cast<int>(kind)]; };
            const std::size_t base = of(report.baseRaw, hldata::GuestKind::Model) + of(report.baseRaw, hldata::GuestKind::Sound);
            if (!report.maps.empty())
                common_.guest.push_back(std::string(hldata::kAirGroupNames[g + 1]) + " (base " +
                                        std::to_string(base / 1024) + " KB):");
            for (const AirReport::Map &map : report.maps)
            {
                const std::size_t level = of(map.raw, hldata::GuestKind::Level);
                const std::size_t models = of(map.raw, hldata::GuestKind::Model);
                const std::size_t sounds = of(map.raw, hldata::GuestKind::Sound);
                // (The weapons lying about, kept packed: all of them so, and
                // the one in hand undone besides.)
                const std::size_t packed = map.raw[static_cast<int>(hldata::GuestKind::Count)] +
                                           map.raw[static_cast<int>(hldata::GuestKind::Count) + 1];
                const auto data = static_cast<std::uint32_t>(level + models + sounds + packed + base);
                CommonResult::Guest guest;
                guest.peak = hldata::guestPeak(data);
                guest.fits = hldata::guestFits(data);
                common_.guestByMap[map.name] = guest;
                char line[160];
                std::snprintf(line, sizeof(line),
                              "  %-14s level %4zu  models %3zu  sounds %3zu  weapons %3zu  -> %4u KB of %u, %s (sent: %zu KB)",
                              map.name.c_str(), level / 1024, models / 1024, sounds / 1024, packed / 1024,
                              static_cast<unsigned>(guest.peak / 1024), static_cast<unsigned>(hldata::kGuestRamBytes / 1024),
                              guest.fits ? "fits" : "TOO BIG", map.bytes / 1024);
                common_.guest.push_back(line);
                // (The same, every column whole -- the base's models and sounds
                // counted with the map's -- for a table.)
                const std::size_t baseModels = of(report.baseRaw, hldata::GuestKind::Model);
                const std::size_t baseSounds = of(report.baseRaw, hldata::GuestKind::Sound);
                std::snprintf(line, sizeof(line), "  table %s %zu %zu %zu %zu %u", map.name.c_str(), level,
                              models + baseModels, packed, sounds + baseSounds, static_cast<unsigned>(guest.peak));
                common_.guest.push_back(line);
            }
        }
    }

    std::string Converter::preflight() const
    {
        std::error_code ec;
        if (options_.gameDir.empty() || !fs::is_directory(options_.gameDir / "maps", ec))
            return "The game folder has no 'maps' folder. Choose Half-Life's 'valve' folder.";
        if (listMaps().empty())
            return "The game folder's 'maps' folder holds no .bsp files.";
        if (options_.cardRoot.empty() || !fs::is_directory(options_.cardRoot, ec))
            return "The card folder does not exist.";
        if (options_.textures && findPtexconv(options_.ptexconv).empty())
            return "ptexconv was not found. It comes with BlocksDS; point the converter at it, "
                   "or convert without textures.";
        return {};
    }

    std::vector<std::string> Converter::listMaps() const
    {
        std::vector<std::string> names;
        std::error_code ec;
        for (auto it = fs::directory_iterator(options_.gameDir / "maps", ec);
             !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            if (it->is_regular_file(ec) && lower(it->path().extension().string()) == ".bsp")
                names.push_back(lower(it->path().stem().string()));
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return names;
    }

    bool Converter::findMap(const std::string &name, fs::path &out) const
    {
        // Without regard to case: the game's own files are lower-case, but
        // mods and old discs are not consistent.
        std::string wanted = lower(name);
        if (wanted.size() > 4 && wanted.substr(wanted.size() - 4) == ".bsp")
            wanted.resize(wanted.size() - 4);
        std::error_code ec;
        for (auto it = fs::directory_iterator(options_.gameDir / "maps", ec);
             !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            if (lower(it->path().extension().string()) == ".bsp" &&
                lower(it->path().stem().string()) == wanted)
            {
                out = it->path();
                return true;
            }
        }
        return false;
    }

    namespace
    {
        /// What all of a map's sounds may take in the console's memory.
        constexpr std::size_t kMapSoundBytes = 350 * 1024;
        /// ... and a DSi, which has four times the memory: its sounds at
        /// the rate the game has them, up to this.
        constexpr std::size_t kMapSoundBytesDsi = 2 * 1024 * 1024;
        constexpr int kSoundRateDsi = 22050;

        /// `relative` under `root`, whatever case each part of it is in on
        /// disk (the game's files are named in whatever case their authors
        /// typed, and its maps name them in another). Empty if it is not
        /// there.
        fs::path findNoCase(const fs::path &root, const std::string &relative)
        {
            fs::path at = root;
            std::string rest = relative;
            for (char &c : rest)
                c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (std::size_t from = 0; from <= rest.size();)
            {
                const std::size_t slash = std::min(rest.find('/', from), rest.size());
                const std::string part = rest.substr(from, slash - from);
                from = slash + 1;
                if (part.empty())
                    continue;
                bool found = false;
                std::error_code ec;
                for (const auto &entry : fs::directory_iterator(at, ec))
                {
                    std::string name = entry.path().filename().string();
                    for (char &c : name)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (name == part)
                    {
                        at = entry.path();
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return fs::path();
            }
            return at;
        }

        /// One of the game's files: the chosen language's, if its folder has
        /// it (valve_french's voices and text over valve's), else the game's.
        fs::path findGameFile(const ConvertOptions &options, const std::string &relative)
        {
            if (!options.languageDir.empty())
            {
                const fs::path translated = findNoCase(options.languageDir, relative);
                if (!translated.empty())
                    return translated;
            }
            return findNoCase(options.gameDir, relative);
        }
    } // namespace

    MapResult Converter::cookMap(const std::string &name)
    {
        MapResult result;
        result.name = lower(name);

        fs::path bspPath;
        if (!findMap(name, bspPath))
        {
            result.error = "no such map in " + (options_.gameDir / "maps").string();
            return result;
        }
        result.name = lower(bspPath.stem().string());

        BspFile bsp;
        if (!bsp.load(bspPath, result.error))
            return result;
        std::vector<Entity> entities;
        if (!parseEntities(bsp.entities, entities, result.error))
            return result;
        // (A multiplayer map: what it has of weapons the port has not got
        // yet is given as ones it has.)
        result.stoodIn = standInEntities(entities);
        std::vector<std::string> missingWads;
        WadSet wads;
        wads.openForMap(options_.gameDir, entities.empty() ? "" : entities[0].value("wad"),
                        missingWads);

        // The texture plan comes first: it decides which faces are drawn at
        // all, and geometry is built in cooked texels.
        const std::vector<BrushInfo> brushes = analyseBrushes(bsp, entities);
        // A map with a sky gives up room for its six sides.
        std::string skyName;
        if (!entities.empty())
        {
            skyName = std::string(entities[0].value("skyname"));
            for (char &c : skyName)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            // A map that names no sky but has open sky in it gets the
            // game's own, as the game gives it (crossfire, datacore...).
            if (skyName.empty())
            {
                for (const auto &face : bsp.faces)
                {
                    std::string name = bsp.textures[bsp.texInfos[face.texInfo].miptex].name;
                    for (char &c : name)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (name == "sky")
                    {
                        skyName = "desert";
                        break;
                    }
                }
            }
            if (!skyName.empty() && (skyName.size() > 12 || !skyExists(options_.gameDir, skyName)))
            {
                result.warnings.push_back("its sky '" + skyName + "' was not found: drawn without one");
                skyName.clear();
            }
        }
        // The models its actors are made of, with the clips this map plays.
        // Before the textures are planned: their skins come out of the same
        // video memory.
        struct ModelOut
        {
            std::string name;
            std::vector<std::uint8_t> bytes;
            /// The same with the model as the game has it for its finest
            /// level, for a DSi; empty where that is no more than `bytes`.
            std::vector<std::uint8_t> bytesDsi;
            std::vector<std::string> eventSounds; ///< CookedModel::eventSounds
            RgbaImage atlas;
            CookedModel::IndexedSkin indexed; ///< the 256-colour skin, where a player's colours go over it
            int triangles = 0, fullTriangles = 0; ///< body 0 at its finest, in each
            int far = 0;                          ///< ... and at its coarsest
            bool finer = false; ///< its finest and largest, for a DSi: staged beside the plain one (.dsi.hmd)
            bool lazy = false; ///< a weapon the map has not: in the common archive (kModelRefLazy)
        };
        std::vector<ModelOut> models;
        // The weapons the player cannot have by now in this map: named in it
        // all the same, to be loaded if a cheat gives one (each with every
        // clip the game plays, and a skin of one size: kLazySkinTexels).
        std::vector<ModelNeed> lazyNeeds;
        // Every weapon, as a console sent the map for a game between consoles
        // has them (cooked lighter, kGuestLean, and the same on every map:
        // stage gst/). Only for a map such a game is played on.
        std::vector<ModelNeed> guestWeapons;
        int modelTexels = 0;
        std::vector<ModelNeed> needs = collectModelNeeds(entities);
        // And of those who may walk in from next door: each neighbouring map
        // is looked at for who stands by the door.
        for (const Entity &entity : entities)
        {
            if (entity.className() != "trigger_changelevel")
                continue;
            const fs::path next =
                options_.gameDir / "maps" / (lower(std::string(entity.value("map"))) + ".bsp");
            BspFile other;
            std::vector<Entity> otherEntities;
            std::string ignored;
            if (other.load(next, ignored) && parseEntities(other.entities, otherEntities, ignored))
                addVisitorNeeds(entities, otherEntities, std::string(entity.value("landmark")), needs);
        }
        // What the player may be holding by the time they are in this map.
        // The game's own maps are known: nothing before the crowbar is found
        // (c1a1), the pistol from c1a1f, the shotgun from c1a2. Any other
        // map gets them all.
        {
            const std::string map = lower(name);
            const bool before = map.rfind("c0", 0) == 0 || map.rfind("c1a0", 0) == 0;
            const bool early = map.rfind("c1a1", 0) == 0;
            struct Held
            {
                const char *name;
                bool wanted;
                std::vector<std::string> clips;
            };
            const Held kHeld[] = {
                // (The crowbar's clips are those the game plays: its three
                // swings at the air, the two that land, out and away.)
                {kViewModelName, !before,
                 {"draw", "holster", "attack1miss", "attack2", "attack3", "attack2hit", "attack3hit"}},
                // (The others' likewise, as the game's own weapon code asks
                // for them -- HlMonsters.hpp's weaponsTick: every way each
                // is held still, its shot, its last round, its reloads.
                // The first of each is what it is held as between them.)
                {"v_9mmhandgun", !before && map != "c1a1" && map != "c1a1a",
                 {"idle1", "idle2", "idle3", "shoot", "shoot_empty", "reload", "reload_noshot", "draw"}},
                {"v_shotgun", !before && !early,
                 {"sm_idle", "idle4", "deepidle", "shoot", "shoot_big", "start_reload", "reload", "pump", "draw"}},
                // (Grenades are first found in c1a1b.)
                {"v_grenade", !before && map != "c1a1" && map != "c1a1a" && map != "c1a1f",
                 {"idle", "fidget", "pinpull", "throw1", "throw2", "throw3", "draw"}},
                // (The MP5 is first found in c1a3, the .357 in c1a4g.)
                {"v_9mmar", !before && !early && map.rfind("c1a2", 0) != 0,
                 {"longidle", "idle1", "shoot", "grenade", "reload", "deploy"}},
                {"v_357",
                 !before && !early && map.rfind("c1a2", 0) != 0 && map.rfind("c1a3", 0) != 0 && map != "c1a4" &&
                     map != "c1a4b" && map != "c1a4d" && map != "c1a4e" && map != "c1a4f" && map != "c1a4k" &&
                     map != "c1a4i",
                 {"idle1", "idle2", "idle3", "fidget1", "fire1", "reload", "draw"}},
            };
            // The weapons that came later, from the map each is first found
            // in on: the game's maps are named in the order they are played
            // (c2a2b2 the satchel and the tripmine, c2a3 the crossbow, c2a4e
            // the snark).
            const bool campaign = map.size() >= 4 && map[0] == 'c' && map[2] == 'a';
            auto from = [&](const char *first) { return campaign && map >= first; };
            const Held kLater[] = {
                {"v_crossbow", from("c2a3"),
                 {"idle1", "idle2", "fidget1", "fidget2", "fire1", "fire3", "reload", "draw1"}},
                {"v_satchel", from("c2a2b2"), {"idle1", "fidget1", "draw"}},
                {"v_satchel_radio", from("c2a2b2"), {"idle1", "fidget1", "draw", "fire"}},
                {"v_tripmine", from("c2a2b2"), {"idle1", "fidget", "draw", "ground"}},
                {"v_squeak", from("c2a4e"), {"idle1", "fidgetfit", "fidgetnip", "up", "throw"}},
                // (The gauss from c2a4f, the RPG from c2a5a, the hornetgun
                // from c2a5d, the egon from c3a2.)
                {"v_gauss", from("c2a4f"), {"idle", "idle2", "fidget", "spinup", "spin", "fire", "fire2", "draw"}},
                {"v_rpg", from("c2a5a"), {"idle", "fidget", "reload", "fire", "draw1", "draw2", "idle2", "fidget2"}},
                {"v_hgun", from("c2a5d"), {"idle1", "fidgetsway", "fidgetshake", "up", "shoot"}},
                {"v_egon", from("c3a2"),
                 {"idle1", "fidget1", "fire1", "fire2", "fire3", "fire4", "draw"}},
            };
            auto hold = [&](const Held &one)
            {
                if (!one.wanted)
                {
                    ModelNeed lazy;
                    lazy.name = one.name;
                    lazy.file = std::string("models/") + one.name + ".mdl";
                    lazy.clips = one.clips;
                    lazy.firstSequence = one.clips.empty();
                    lazyNeeds.push_back(lazy);
                    return;
                }
                // (Already wanted for something else -- a tripmine on a wall
                // is the held model's mine alone -- its clips are added to.)
                for (ModelNeed &need : needs)
                {
                    if (need.name != one.name)
                        continue;
                    for (const std::string &clip : one.clips)
                    {
                        if (std::find(need.clips.begin(), need.clips.end(), clip) == need.clips.end())
                            need.clips.push_back(clip);
                    }
                    return;
                }
                ModelNeed held;
                held.name = one.name;
                held.file = std::string("models/") + one.name + ".mdl";
                held.clips = one.clips;
                needs.push_back(held);
            };
            for (const Held &one : kHeld)
                hold(one);
            for (const Held &one : kLater)
                hold(one);
            for (const Held *list : {&kHeld[0], &kLater[0]})
            {
                const std::size_t count = list == &kHeld[0] ? std::size(kHeld) : std::size(kLater);
                for (std::size_t i = 0; i < count; ++i)
                {
                    ModelNeed weapon;
                    weapon.name = list[i].name;
                    weapon.file = std::string("models/") + list[i].name + ".mdl";
                    weapon.clips = list[i].clips;
                    guestWeapons.push_back(weapon);
                }
            }
            // ... and what they leave about: a satchel thrown, a snark let
            // go, a bolt in a wall. (A tripmine on one is v_tripmine's mine,
            // above.)
            const Held kThrown[] = {
                {"w_satchel", from("c2a2b2"), {}},
                {"w_squeak", from("c2a4e"), {"run"}},
                {"crossbow_bolt", from("c2a3"), {}},
                {"rpgrocket", from("c2a5a"), {}},
                {"hornet", from("c2a5d"), {}},
            };
            for (const Held &one : kThrown)
            {
                hold(one);
                for (ModelNeed &need : needs)
                {
                    if (need.name == one.name)
                        need.firstSequence = need.firstSequence || one.clips.empty();
                }
            }
            // ... and the spent casings out of the guns (EjectBrass): wherever
            // the player may have the pistol, the MP5 or the shotgun, or a
            // soldier or an assassin is (or may come along from next door).
            addCasingNeeds(kHeld[1].wanted || kHeld[2].wanted || kHeld[4].wanted, needs);
            addGrenadeNeeds(kHeld[3].wanted, kHeld[4].wanted, needs);
        }
        // A multiplayer map: the others in the game are seen. One model for
        // all of them, the game's own player in the suit, with what a body
        // does whole -- standing, running, crouching, jumping, dying.
        addMultiplayerNeeds(entities, needs);
        // ... each holding what they hold: loaded when someone is seen to.
        for (const char *held : hldata::kHeldModels)
        {
            if (held == nullptr || !isMultiplayerMap(entities))
                continue;
            ModelNeed need;
            need.name = held;
            need.file = std::string("models/") + held + ".mdl";
            need.held = true;
            lazyNeeds.push_back(need);
        }
        // Of the weapons the map has not, and what goes with them, those
        // wanted for something else -- a tripmine on a wall, a soldier's
        // casings, a tank's rocket -- are the map's own.
        lazyNeeds.erase(std::remove_if(lazyNeeds.begin(), lazyNeeds.end(),
                                       [&](const ModelNeed &lazy)
                                       {
                                           for (const ModelNeed &need : needs)
                                           {
                                               if (need.name == lazy.name)
                                                   return true;
                                           }
                                           return false;
                                       }),
                        lazyNeeds.end());
        // One model, with a skin of at most `skinTexels`. False if it cannot
        // be read or cooked (said once, in the warnings).
        auto cookOne = [&](const ModelNeed &need, int skinTexels, ModelOut &out, bool quiet,
                           const ModelLean &lean = ModelLean())
        {
            // The map names the file in whatever case its author typed.
            fs::path file = options_.gameDir;
            bool found = true;
            std::string rest = need.file;
            for (char &c : rest)
                c = c == '\\' ? '/' : c;
            for (std::size_t at = 0; at <= rest.size() && found;)
            {
                const std::size_t slash = std::min(rest.find('/', at), rest.size());
                const std::string part = rest.substr(at, slash - at);
                at = slash + 1;
                if (part.empty())
                    continue;
                found = false;
                std::error_code ec;
                for (const auto &entry : fs::directory_iterator(file, ec))
                {
                    if (lower(entry.path().filename().string()) == part)
                    {
                        file = entry.path();
                        found = true;
                        break;
                    }
                }
            }
            MdlFile mdl;
            std::string why;
            if (!found || need.name.size() + 1 >= sizeof(hldata::ModelRef::name) || !mdl.load(file, why))
            {
                if (!quiet)
                    result.warnings.push_back("model '" + need.file + "' cannot be read" +
                                              (why.empty() ? "" : ": " + why));
                return false;
            }
            if (need.held && !bakeHeldModel(mdl, why))
                return false;
            std::vector<std::string> clips, clipsDsi;
            modelClips(mdl, need, clips, clipsDsi);
            CookedModel cooked;
            ModelCookReport modelReport;
            out = ModelOut();
            out.name = need.name;
            const bool cookedOk = cookModel(mdl, clips, cooked, modelReport, why, skinTexels, false, lean);
            out.eventSounds = cooked.eventSounds;
            if (!cookedOk ||
                !writeModel(cooked, out.bytes, why))
            {
                if (!quiet)
                    result.warnings.push_back("model '" + need.file + "' cannot be cooked: " + why);
                return false;
            }
            // A clip the hand is told to play and its model has not got is
            // a weapon that stands still when it should move: said.
            if (!quiet && need.name.rfind("v_", 0) == 0)
            {
                for (const std::string &missing : modelReport.missingClips)
                    result.warnings.push_back("model '" + need.file + "' has no clip '" + missing + "'");
            }
            out.atlas = std::move(cooked.atlas);
            out.indexed = std::move(cooked.indexed);
            out.triangles = modelReport.triangles;
            out.fullTriangles = modelReport.sourceTriangles;
            out.far = modelReport.levels.empty() ? modelReport.triangles : modelReport.levels.back().triangles;
            // A DSi's file, where the game's own mesh is more than that, or
            // it has more clips.
            if (modelReport.sourceTriangles > modelReport.triangles || clipsDsi.size() > clips.size())
            {
                CookedModel whole;
                ModelCookReport wholeReport;
                std::string ignored;
                if (!cookModel(mdl, clipsDsi, whole, wholeReport, ignored, skinTexels, true) ||
                    !writeModel(whole, out.bytesDsi, ignored))
                    out.bytesDsi.clear();
                // (The sounds of the clips a DS has not are not in the level:
                // they would be in a DS's memory too. Those few steps are
                // silent on a DSi.)
            }
            return true;
        };
        // What lies on the floor is a few pixels on the screen; what is held
        // fills a corner of it; people and monsters are in between.
        std::vector<int> skinOf;
        std::vector<std::size_t> needOf;
        for (std::size_t n = 0; n < needs.size(); ++n)
        {
            const ModelNeed &need = needs[n];
            // (What lies about to be picked up, and the pieces things come
            // apart into, are small on screen: a small skin does.)
            const int skin = need.name.rfind("w_", 0) == 0 || need.name.find("gib") != std::string::npos ||
                                     need.name == "v_tripmine"
                                 ? 64 * 64
                                 : kModelSkinTexels;
            ModelOut out;
            if (!cookOne(need, skin, out, false))
                continue;
            models.push_back(std::move(out));
            skinOf.push_back(skin);
            needOf.push_back(n);
        }
        // What is in video memory together. The weapons' skins (v_*) are
        // kept in main memory until one is taken up (ModelCache), so of those
        // only the largest counts; the rest all do.
        auto heldModel = [&](std::size_t m) { return needs[needOf[m]].name.rfind("v_", 0) == 0; };
        auto largestHeld = [&]()
        {
            std::size_t best = models.size();
            int most = 0;
            for (std::size_t m = 0; m < models.size(); ++m)
            {
                const int texels = models[m].atlas.width * models[m].atlas.height;
                if (heldModel(m) && texels > most)
                {
                    most = texels;
                    best = m;
                }
            }
            return best;
        };
        auto residentTexels = [&]()
        {
            const std::size_t held = largestHeld();
            int sum = 0;
            for (std::size_t m = 0; m < models.size(); ++m)
            {
                if (!heldModel(m) || m == held)
                    sum += models[m].atlas.width * models[m].atlas.height;
            }
            return sum;
        };
        // As cooked at their full size, before any is made smaller: what fitSkins starts from.
        const std::vector<ModelOut> fullModels = models;
        const std::vector<int> fullSkinOf = skinOf;
        // Too much between them for the map's own textures to have room: the
        // largest skin is halved, again and again, until it is not.
        auto fitSkins = [&](int budget)
        {
            models = fullModels;
            skinOf = fullSkinOf;
            modelTexels = residentTexels();
            while (modelTexels > budget)
            {
                std::size_t largest = models.size();
                int most = 64 * 64;
                const std::size_t held = largestHeld();
                for (std::size_t m = 0; m < models.size(); ++m)
                {
                    // (Making a weapon's smaller that is not the largest gives nothing back.)
                    if (heldModel(m) && m != held)
                        continue;
                    const int texels = models[m].atlas.width * models[m].atlas.height;
                    if (texels > most)
                    {
                        most = texels;
                        largest = m;
                    }
                }
                if (largest == models.size())
                    break; // nothing left to take from
                ModelOut smaller;
                if (!cookOne(needs[needOf[largest]], most / 2, smaller, true) ||
                    smaller.atlas.width * smaller.atlas.height >= most)
                {
                    skinOf[largest] = 0;
                    break;
                }
                models[largest] = std::move(smaller);
                modelTexels = residentTexels();
            }
        };
        fitSkins(kMapSkinTexels);
        for (const ModelOut &out : models)
        {
            result.modelKb += static_cast<int>(out.bytes.size() / 1024);
            result.modelKbDsi += static_cast<int>((out.bytesDsi.empty() ? out.bytes : out.bytesDsi).size() / 1024);
            result.modelTriangles += out.triangles;
            result.modelTrianglesFar += out.far;
            result.modelTrianglesFull += out.fullTriangles;
        }
        result.models = static_cast<int>(models.size());
        result.modelTexelKb = modelTexels / 1024;

        const int skyOnlyTexels = skyName.empty() ? 0 : 6 * kSkySize * kSkySize;
        StreamOptions streamOptions;
        streamOptions.enabled = options_.streamTextures;
        const TextureNeeds textureWants = textureNeeds(bsp, brushes, findHiddenTextures(bsp, wads));
        // (... and a palette for a weapon loaded when it is taken up.)
        const int colourBudget = kPaletteColourBudget - (skyName.empty() ? 0 : 6 * 256) -
                                 static_cast<int>(models.size() + (lazyNeeds.empty() ? 0 : hldata::kLazyResident)) * kModelSkinColours -
                                 kSpriteColourBudget;
        // Plan maps to use 3 texture banks, with few exceptions
        auto planFor = [&](int worldBudget)
        {
            StreamOptions options = streamOptions;
            return planTextures(bsp, textureWants, worldBudget - skyOnlyTexels - modelTexels, colourBudget,
                                kAlphaTexelBudget - kSpriteTexelBudget, options);
        };
        TexturePlan plan = planFor(streamOptions.enabled ? kSinglePassTexelBudget : kWorldTexelBudget);
        // With streaming, what the worst view leaves of the room for the
        // streamed textures goes to the skins: larger ones, as long as the
        // textures are no smaller for it.
        if (streamOptions.enabled && plan.streamedTextures != 0 && plan.fits)
        {
            const int spare = plan.regionTexels - plan.worstViewTexels - kSkinSpareMargin;
            int extra = spare < kMaxExtraSkinTexels ? spare : kMaxExtraSkinTexels;
            for (int attempt = 0; attempt < 3 && extra >= kMinExtraSkinTexels; ++attempt, extra /= 2)
            {
                fitSkins(kMapSkinTexels + extra);
                const TexturePlan larger = planFor(kSinglePassTexelBudget);
                if (larger.fits && larger.worstViewTexels >= plan.worstViewTexels && larger.shrunk <= plan.shrunk)
                {
                    plan = larger;
                    break;
                }
                fitSkins(kMapSkinTexels);
            }
        }
        result.modelTexelKb = modelTexels / 1024;
        // A map a console may be sent (a game between consoles, the Hazard
        // Course, Uplink: packAirGroups): there every texture is in video
        // memory at once, none streamed -- beside its models, the sky, and
        // the two slots of the weapon in hand (held at kLazySkinTexels
        // there, the lighter cook) set by first. What does not fit is made
        // smaller for it (guestTexturePlan; the guest's level, Pack).
        bool airMap = result.name.rfind("t0a0", 0) == 0 || result.name.rfind("hldemo", 0) == 0;
        for (const Entity &entity : entities)
            airMap = airMap || entity.className() == "info_player_deathmatch";
        TexturePlan guestPlan;
        if (airMap)
        {
            const std::size_t held = largestHeld();
            const int heldTexels =
                held < models.size() ? models[held].atlas.width * models[held].atlas.height : 0;
            const int guestModels = modelTexels - heldTexels + 2 * hldata::kLazySkinTexels +
                                    (hldata::kLazyResident - 1) * hldata::kLazyPieceSkinTexels;
            guestPlan = guestTexturePlan(bsp, plan,
                                         kSinglePassTexelBudget - kGuestVramMargin - skyOnlyTexels - guestModels);
            result.guestTexelKb = guestPlan.texels / 1024;
            result.guestShrunk = guestPlan.shrunk;
            if (!guestPlan.fits)
                result.warnings.push_back("textures do not fit a console sent the map, even at their smallest");
        }
        auto reportPlan = [&]()
        {
            if (!plan.fits && std::find(result.warnings.begin(),
                    result.warnings.end(), "textures do not fit even at smallest resolution") == result.warnings.end())
            {
                result.warnings.push_back("textures do not fit even at smallest resolution");
            }
            result.texturesUsed = plan.usedTextures;
            result.texelKb = plan.texels / 1024;
            result.alphaTexelKb = plan.alphaTexels / 1024;
            result.streamedTextures = result.streamWorstViewKb = result.streamRegionKb = 0;
            result.streamResidentKb = result.streamTotalKb = 0;
            if (plan.streamedTextures != 0)
            {
                long total = 0;
                for (std::size_t t = 0; t < plan.streamed.size(); ++t)
                {
                    if (plan.streamed[t])
                        total += static_cast<long>(plan.sizes[t].width) * plan.sizes[t].height;
                }
                result.streamedTextures = plan.streamedTextures;
                result.streamWorstViewKb = plan.worstViewTexels / 1024;
                result.streamRegionKb = plan.regionTexels / 1024;
                result.streamResidentKb = plan.residentTexels / 1024;
                result.streamTotalKb = static_cast<int>(total / 1024);
            }
        };
        reportPlan();

        CookOptions cookOptions;
        cookOptions.lightScale = options_.lightScale;
        cookOptions.lightPower = options_.lightPower;
        cookOptions.guardedMerges = options_.guardedMerges;
        cookOptions.groupByLeaf = options_.groupByLeaf;
        cookOptions.skyName = skyName;
        cookOptions.guestPlan = airMap && guestPlan.shrunk != 0 ? &guestPlan : nullptr;
        for (const auto &model : models)
            cookOptions.models.push_back(model.name);
        // The weapons the map has not, cooked as any model is -- but staged
        // apart (wpn/), for the common archive: the same file whichever map
        // cooks it. Not in what the map's memory is reckoned at.
        for (const ModelNeed &need : lazyNeeds)
        {
            ModelOut out;
            if (!cookOne(need, need.name.rfind("v_", 0) == 0 ? hldata::kLazySkinTexels : hldata::kLazyPieceSkinTexels, out,
                         true, need.held ? kGuestLean : ModelLean()))
                continue;
            out.lazy = true;
            cookOptions.lazyModels.push_back(out.name);
            for (const std::string &sound : out.eventSounds)
            {
                if (std::find(cookOptions.lazyModelSounds.begin(), cookOptions.lazyModelSounds.end(), sound) ==
                    cookOptions.lazyModelSounds.end())
                    cookOptions.lazyModelSounds.push_back(sound);
            }
            models.push_back(std::move(out));
        }
        // The guest's weapons (above), where a game between consoles is
        // played on the map: written apart, not the map's.
        std::vector<ModelOut> guestModels;
        bool multiplayer = false;
        for (const Entity &entity : entities)
            multiplayer = multiplayer || entity.className() == "info_player_deathmatch";
        for (const ModelNeed &need : airMap ? guestWeapons : std::vector<ModelNeed>())
        {
            ModelOut out;
            if (cookOne(need, hldata::kLazySkinTexels, out, true, kGuestLean))
                guestModels.push_back(std::move(out));
        }
        // ... and the others in the game as it sees them: the player model,
        // its skin in 256 colours, each player's laid over it by its palette.
        for (const ModelNeed &need : needs)
        {
            ModelOut out;
            ModelOut fine;
            if (multiplayer && need.name == kPlayerModelName && cookOne(need, hldata::kFinePlayerSkinTexels, fine, true))
            {
                fine.finer = true;
                guestModels.push_back(std::move(fine));
            }
            if (multiplayer && need.name == kPlayerModelName && cookOne(need, 128 * 128, out, true))
                guestModels.push_back(std::move(out));
        }
        cookOptions.viewModel = kViewModelName;
        // (What the models sound like as they move: the level has to know
        // where each of those sounds is.)
        for (const ModelOut &model : models)
        {
            for (const std::string &sound : model.eventSounds)
            {
                if (!model.lazy && std::find(cookOptions.modelSounds.begin(), cookOptions.modelSounds.end(), sound) ==
                    cookOptions.modelSounds.end())
                    cookOptions.modelSounds.push_back(sound);
            }
        }
        // What the game's textures are made of, for the sound of a step.
        MaterialBook materials;
        {
            std::vector<std::uint8_t> text;
            std::string ignored;
            const fs::path file = findGameFile(options_, "sound/materials.txt");
            if (!file.empty() && readFile(file, text, ignored))
                materials = parseMaterials(std::string(text.begin(), text.end()));
        }
        cookOptions.materials = &materials;
        // The game's own graph of the map's nodes, where it came with one.
        NodGraph nodGraph;
        {
            std::vector<std::uint8_t> bytes;
            std::string ignored;
            const fs::path file = findNoCase(options_.gameDir / "maps" / "graphs", result.name + ".nod");
            if (!file.empty() && readFile(file, bytes, ignored) && parseNodGraph(bytes, nodGraph, ignored))
                cookOptions.nodGraph = &nodGraph;
        }
        // What the map's people say is looked up in the game's own list.
        SentenceBook book;
        {
            std::vector<std::uint8_t> text;
            std::string ignored;
            const fs::path file = findGameFile(options_, "sound/sentences.txt");
            if (!file.empty() && readFile(file, text, ignored))
                book = parseSentences(std::string(text.begin(), text.end()));
        }
        cookOptions.sentences = &book;
        // ... and what it writes on the screen, in its titles.txt.
        TitlesBook titles;
        {
            std::vector<std::uint8_t> text;
            std::string ignored;
            const fs::path file = findGameFile(options_, "titles.txt");
            if (!file.empty() && readFile(file, text, ignored))
                titles = parseTitles(toCp1252(std::string(text.begin(), text.end())));
        }
        cookOptions.titles = &titles;
        // The chapter this map is part of (asked of all the game's maps,
        // once: each chapter is named by the map that opens it).
        {
            static std::mutex scanLock;
            static std::filesystem::path scanned;
            static std::vector<std::pair<std::string, std::string>> titled;
            const std::lock_guard<std::mutex> lock(scanLock);
            if (scanned != options_.gameDir)
            {
                titled = chapterTitles(options_.gameDir, &titles);
                scanned = options_.gameDir;
            }
            cookOptions.chapter = chapterOf(lower(name), titled);
        }
        CookedLevel level;
        CookReport report;
        std::vector<std::uint8_t> bytes;
        if (!cookLevel(bsp, cookOptions, plan, level, report, result.error))
            return result;

        // Store color such that it can be properly drawn
        // even if the texture is not loaded yet.
        {
            std::map<std::string, std::uint16_t> averages;
            for (hldata::TextureRef &ref : level.textures)
            {
                if ((ref.flags & hldata::kTextureSky) != 0)
                    continue;
                const std::string name(ref.name);
                const auto known = averages.find(name);
                if (known != averages.end())
                {
                    ref.average = known->second;
                    continue;
                }
                IndexedImage image;
                std::string ignored;
                bool have = false;
                for (const BspTexture &t : bsp.textures)
                {
                    if (t.embedded && t.name == name && t.pixels.size() == static_cast<std::size_t>(t.width) * t.height &&
                        t.palette.size() >= 768)
                    {
                        image.width = t.width;
                        image.height = t.height;
                        image.pixels = t.pixels;
                        image.palette = t.palette;
                        have = true;
                        break;
                    }
                }
                have = have || wads.read(name, image, ignored);
                std::uint16_t average = 0x7FFF;
                if (have && !image.pixels.empty() && image.palette.size() >= 768)
                {
                    const bool key = !name.empty() && name[0] == '{';
                    std::uint64_t sum[3] = {0, 0, 0}, count = 0;
                    for (const std::uint8_t index : image.pixels)
                    {
                        if (key && index == 255)
                            continue; // the colour that is a hole
                        for (int c = 0; c < 3; ++c)
                            sum[c] += image.palette[index * 3 + c];
                        ++count;
                    }
                    if (count != 0)
                    {
                        auto five = [&](int c) { return static_cast<std::uint16_t>((sum[c] / count) >> 3); };
                        average = static_cast<std::uint16_t>(five(0) | (five(1) << 5) | (five(2) << 10));
                    }
                }
                averages[name] = average;
                ref.average = average;
            }
        }

        // The map's sounds: each read, brought to the console's rate and
        // packed. All of a map's sounds are in memory while it is played, so
        // they have an allowance; over it, the longest are done again at
        // half the rate (their names say so) until they fit.
        {
            struct One
            {
                std::size_t index = 0;
                WavData wav;
                std::uint32_t sourceHash = 0;
                CookedSound cooked;
                CookedSound twin; ///< a DSi's copy, if it is to have a better one
            };
            std::vector<One> made;
            std::size_t total = 0;
            // (Not the words of spoken lines, which the console reads off
            // the card as they are said: those are not in the allowance,
            // and stay at the full rate.)
            std::vector<char> kept(level.soundPaths.size() + 1, 0);
            hldata::soundsKept(level.logic.data(), static_cast<std::uint32_t>(level.logic.size()),
                               level.logicSounds.data(),
                               static_cast<std::uint32_t>(level.logicSounds.size()),
                               static_cast<std::uint32_t>(level.soundPaths.size()),
                               reinterpret_cast<bool *>(kept.data()), level.modelSounds.data(),
                               static_cast<std::uint32_t>(level.modelSounds.size()), level.sounds.data());
            for (std::size_t n = 0; n < level.soundPaths.size(); ++n)
            {
                if (level.soundPaths[n].empty())
                    continue;
                One one;
                one.index = n;
                std::vector<std::uint8_t> source;
                std::string why;
                const fs::path file = findGameFile(options_, "sound/" + std::string(level.soundPaths[n]));
                if (file.empty() || !readFile(file, source, why) || !parseWav(source, one.wav, why))
                {
                    // Not in the game folder: the map does without it.
                    level.sounds[n] = hldata::SoundRef{};
                    ++result.soundsMissing;
                    continue;
                }
                one.sourceHash = hldata::crc32(source.data(), static_cast<std::uint32_t>(source.size()));
                one.cooked = encodeSound(one.wav, hldata::kSoundRate, level.soundLoops[n]);
                if (kept[n])
                    total += one.cooked.data.size();
                made.push_back(std::move(one));
            }
            std::uint32_t shortest = 8000;
            while (total > kMapSoundBytes)
            {
                // The longest that is still at the full rate and long enough
                // for the loss to be worth it (and, if that is not enough,
                // the shorter ones after all).
                One *longest = nullptr;
                for (One &one : made)
                {
                    if (kept[one.index] && one.cooked.rate == hldata::kSoundRate &&
                        one.cooked.samples > shortest &&
                        (longest == nullptr || one.cooked.samples > longest->cooked.samples))
                        longest = &one;
                }
                if (longest == nullptr && shortest > 2000)
                {
                    shortest = 2000;
                    continue;
                }
                if (longest == nullptr)
                    break;
                total -= longest->cooked.data.size();
                longest->cooked =
                    encodeSound(longest->wav, hldata::kSoundRate / 2, level.soundLoops[longest->index]);
                total += longest->cooked.data.size();
                ++result.soundsHalved;
            }
            if (total > kMapSoundBytes)
                result.warnings.push_back("its sounds are " + std::to_string(total / 1024) +
                                          " KB, over what a DS has room for");
            // A DSi's copies: whatever the game has at a better rate than a
            // DS is given, at that rate (22 kHz at most) -- while they fit
            // what a DSi is allowed, the longest going without first.
            std::size_t totalDsi = total;
            for (One &one : made)
            {
                if (!kept[one.index] || one.wav.rate <= one.cooked.rate)
                    continue;
                one.twin = encodeSound(one.wav, std::min(one.wav.rate, kSoundRateDsi), level.soundLoops[one.index]);
                totalDsi += one.twin.data.size() - one.cooked.data.size();
            }
            while (totalDsi > kMapSoundBytesDsi)
            {
                One *longest = nullptr;
                for (One &one : made)
                {
                    if (!one.twin.data.empty() &&
                        (longest == nullptr || one.twin.data.size() > longest->twin.data.size()))
                        longest = &one;
                }
                if (longest == nullptr)
                    break;
                totalDsi -= longest->twin.data.size() - longest->cooked.data.size();
                longest->twin = CookedSound();
            }
            for (const One &one : made)
            {
                const bool loops = level.soundLoops[one.index];
                const std::string name =
                    soundCardName(level.soundPaths[one.index], one.cooked.rate, loops);
                hldata::SoundRef ref{};
                std::strncpy(ref.name, name.c_str(), sizeof(ref.name) - 1);
                // (Its better copy is written below, the same way. A weapon's
                // the map has not has none, and says it is read when played.)
                const bool twinned = !one.twin.data.empty();
                ref.twin = twinned ? static_cast<std::uint8_t>(one.twin.rate / 1000) : std::uint8_t(0);
                ref.twin |= hldata::soundLazy(level.sounds[one.index]) ? hldata::kSoundLazy : 0;
                level.sounds[one.index] = ref;
                result.soundsTwinned += twinned ? 1 : 0;
                for (int which = 0; which < (twinned ? 2 : 1); ++which)
                {
                    const CookedSound &cooked = which == 0 ? one.cooked : one.twin;
                    const std::string cardName = soundCardName(level.soundPaths[one.index], cooked.rate, loops);
                    // (The hash is of the source and of how it was cooked:
                    // the file is made again only if either changed.)
                    // (The last: which way of deciding what goes round the
                    // file was cooked by -- 2: only what has a cue point.)
                    const std::uint32_t settings[] = {static_cast<std::uint32_t>(cooked.rate), loops ? 1u : 0u,
                                                      hldata::kSoundVersion, 2u};
                    const std::uint32_t hash =
                        one.sourceHash ^
                        hldata::crc32(reinterpret_cast<const std::uint8_t *>(settings), sizeof(settings)) * 131u;
                    const fs::path file = stageDir() / "snd" / (cardName + ".hsn");
                    std::vector<std::uint8_t> existing;
                    std::string why;
                    CookedSound old;
                    hldata::SoundHeader oldHeader;
                    if (readFile(file, existing, why) && unpackSound(existing, old, oldHeader) &&
                        oldHeader.sourceHash == hash)
                        continue;
                    if (!writeFileAtomic(file, packSound(cooked, hash), result.error))
                        return result;
                }
            }
            result.sounds = static_cast<int>(made.size());
            result.soundKb = static_cast<int>(total / 1024);
            result.soundKbDsi = static_cast<int>(totalDsi / 1024);
        }
        // The music the map starts: each track decoded once, to stereo
        // blocks the ARM7 plays as they are read off the card.
        for (const hldata::LogicRecord &record : level.logic)
        {
            if (record.kind != static_cast<std::uint8_t>(hldata::LogicKind::Music))
                continue;
            const int track = static_cast<int>(record.extra[0]);
            if (track < 2 || track > 99)
                continue;
            ++result.musicTracks;
            char cardName[16];
            hldata::musicFileName(track, cardName, sizeof(cardName));
            const fs::path file = dataDir(options_.cardRoot) / "music" / cardName;
            // The game's track N is the media file cdTrackFile names. Made
            // from it, it is not made again -- what it was made from is kept
            // in the stage beside it (a card's tracks made before were each
            // the file numbered N - 1, most of them the wrong music).
            const char *source = cdTrackFile(track);
            if (source == nullptr)
                continue;
            const fs::path from = stageDir() / "music" / (std::string(cardName) + ".src");
            std::error_code ec;
            if (fs::exists(file, ec) && fs::file_size(file, ec) > sizeof(hldata::StereoHeader))
            {
                std::string was;
                std::vector<std::uint8_t> said;
                if (readFile(from, said, was) && std::string(said.begin(), said.end()) == source)
                    continue;
            }
            const fs::path mp3 = findNoCase(options_.gameDir / "media", source);
            std::vector<std::uint8_t> packed;
            WavData music;
            std::vector<std::int16_t> left, right;
            std::string why;
            if (mp3.empty() || !readFile(mp3, packed, why) || !decodeMp3(packed, music, why, &left, &right))
                continue; // a game folder without its music has none
            if (!writeFileAtomic(file, encodeMusicStereo(left, right, music.rate), result.error))
                return result;
            fs::create_directories(from.parent_path(), ec);
            const std::string said(source);
            if (!writeFileAtomic(from, std::vector<std::uint8_t>(said.begin(), said.end()), result.error))
                return result;
        }

        // The sprites: each cooked to its atlas, the common ones as the
        // port's own effects want them, a map's own at a middling size; and
        // while they are more than a map may have, the largest is made
        // smaller. (A sprite the game folder has not got is left with no
        // frames: nothing is drawn for it.)
        struct SpriteOut
        {
            std::size_t index = 0;
            SprFile spr;
            int cell = 32, frames = 8;
            CookedSprite cooked;
            std::uint32_t sourceHash = 0;
        };
        std::vector<SpriteOut> sprites;
        {
            static const int kCommon[][2] = {{32, 8}, {32, 2}, {16, 6}, {32, 1}, {32, 1}, {32, 3}, {16, 4}, {32, 1}, {32, 1}, {16, 4}};
            static_assert(sizeof(kCommon) / sizeof(kCommon[0]) == static_cast<std::size_t>(hldata::CommonSprite::Count));
            int texels = 0;
            // (The map's signs are pictures of decals.wad, made sprites.)
            WadFile decalWad;
            bool decalWadOpen = false, decalWadTried = false;
            const std::string signPath = hldata::kDecalSignPath;
            for (std::size_t n = 0; n < level.spritePaths.size(); ++n)
            {
                SpriteOut one;
                one.index = n;
                std::vector<std::uint8_t> source;
                std::string why;
                const std::string &path = level.spritePaths[n];
                if (path.rfind(signPath, 0) == 0 && path.size() > signPath.size() + 4)
                {
                    if (!decalWadTried)
                    {
                        decalWadTried = true;
                        const fs::path wad = findGameFile(options_, "decals.wad");
                        decalWadOpen = !wad.empty() && decalWad.open(wad, why);
                    }
                    IndexedImage picture;
                    const std::string name = path.substr(signPath.size(), path.size() - signPath.size() - 4);
                    if (!decalWadOpen || !decalWad.read(name, picture, why) || !signSprite(picture, one.spr))
                    {
                        ++result.spritesMissing;
                        continue;
                    }
                    // (Up to 64 texels its longer side: a number is read
                    // from across a room.)
                    one.cell = 64;
                    one.frames = 1;
                    one.sourceHash = hldata::crc32(picture.pixels.data(), static_cast<std::uint32_t>(picture.pixels.size())) ^
                                     hldata::crc32(picture.palette.data(), static_cast<std::uint32_t>(picture.palette.size()));
                    if (!cookSprite(one.spr, one.cell, one.frames, false, one.cooked, why))
                        continue;
                    texels += one.cooked.atlas.width * one.cooked.atlas.height;
                    sprites.push_back(std::move(one));
                    continue;
                }
                const fs::path file = findGameFile(options_, path);
                if (file.empty() || !readFile(file, source, why) || !one.spr.parse(source, why))
                {
                    ++result.spritesMissing;
                    continue;
                }
                if (n < static_cast<std::size_t>(hldata::CommonSprite::Count))
                {
                    one.cell = kCommon[n][0];
                    one.frames = kCommon[n][1];
                }
                one.sourceHash = hldata::crc32(source.data(), static_cast<std::uint32_t>(source.size()));
                if (!cookSprite(one.spr, one.cell, one.frames, level.spriteLights[n], one.cooked, why))
                    continue;
                texels += one.cooked.atlas.width * one.cooked.atlas.height;
                sprites.push_back(std::move(one));
            }
            while (texels > kSpriteTexelBudget)
            {
                SpriteOut *largest = nullptr;
                for (SpriteOut &one : sprites)
                {
                    const int size = one.cooked.atlas.width * one.cooked.atlas.height;
                    if (size > 256 && (one.cell > 8 || one.frames > 1) &&
                        (largest == nullptr ||
                         size > largest->cooked.atlas.width * largest->cooked.atlas.height))
                        largest = &one;
                }
                if (largest == nullptr)
                    break;
                texels -= largest->cooked.atlas.width * largest->cooked.atlas.height;
                // (Fewer frames first, while it has many; then smaller ones.)
                if (largest->frames > 4)
                    largest->frames /= 2;
                else if (largest->cell > 8)
                    largest->cell /= 2;
                else
                    largest->frames = 1;
                std::string why;
                cookSprite(largest->spr, largest->cell, largest->frames, level.spriteLights[largest->index],
                           largest->cooked, why);
                texels += largest->cooked.atlas.width * largest->cooked.atlas.height;
            }
            if (texels > kSpriteTexelBudget)
                result.warnings.push_back("its sprites are " + std::to_string(texels / 1024) +
                                          "K texels, over what is kept for them");
            for (const SpriteOut &one : sprites)
            {
                hldata::SpriteRef ref = one.cooked.ref;
                // (Its name says whether it was cooked as light: the same
                // file may be wanted both ways.)
                const std::string name = signSpriteName(level.spritePaths[one.index]) +
                                         (level.spriteLights[one.index] ? "-l" : "");
                std::strncpy(ref.name, name.c_str(), sizeof(ref.name) - 1);
                level.sprites[one.index] = ref;
            }
            result.sprites = static_cast<int>(sprites.size());
            result.spriteTexelKb = texels / 1024;
        }

        if (!writeLevel(level, bytes, result.error))
            return result;
        result.warnings.insert(result.warnings.end(), report.warnings.begin(),
                               report.warnings.end());

        if (options_.textures)
        {
            const fs::path exe = findPtexconv(options_.ptexconv);
            if (exe.empty())
            {
                result.error = "ptexconv not found";
                return result;
            }
            const fs::path temp = fs::temp_directory_path() / ("hlds-convert-" + processTag() + "-" + result.name);
            PtexconvEncoder encoder(exe, temp, cancel_);
            TextureCookReport texReport;
            // (And a guest's smaller ones, beside them: each size is a file
            // of its own.)
            TextureCookReport guestReport;
            const bool ok = cookTextures(bsp, wads, plan, encoder,
                                         stageDir() / "tex", texReport,
                                         result.error) &&
                            (cookOptions.guestPlan == nullptr ||
                             cookTextures(bsp, wads, guestPlan, encoder, stageDir() / "tex", guestReport,
                                          result.error)) &&
                            (skyName.empty() ||
                             cookSky(options_.gameDir, skyName, encoder,
                                     stageDir() / "tex", result.error));
            // The models' skins: one for each size a model's skin is made
            // in (a map crowded with models has them smaller), shared by
            // every map that uses the model at that size, and encoded again
            // only if the picture has changed.
            for (const auto *list : {&models, &guestModels})
            for (const auto &model : *list)
            {
                if (!ok || model.atlas.width == 0)
                    continue;
                const std::uint32_t settings[] = {kModelSkinColours, encoder.version()};
                const std::uint32_t hash =
                    hldata::crc32(model.atlas.rgba.data(),
                                  static_cast<std::uint32_t>(model.atlas.rgba.size())) ^
                    hldata::crc32(reinterpret_cast<const std::uint8_t *>(settings), sizeof(settings)) * 131u;
                const fs::path skin = stageDir() / "mdl" /
                                      hldata::modelSkinFile(model.name.c_str(), model.atlas.width,
                                                            model.atlas.height);
                std::vector<std::uint8_t> existing;
                std::string ignored;
                CookedTexture old;
                hldata::TextureHeader oldHeader;
                if (readFile(skin, existing, ignored) && unpackTexture(existing, old, oldHeader) &&
                    oldHeader.sourceHash == hash)
                    continue;
                CookedTexture encoded;
                std::string why;
                if (!encoder.encode(model.atlas, kModelSkinColours, TextureFormat::Tex4x4, encoded, why) ||
                    !writeFileAtomic(skin, packTexture(encoded, 0, hash), why))
                    result.warnings.push_back("the skin of model '" + model.name + "' was not made: " + why);
            }
            // The sprites' atlases, likewise.
            for (const SpriteOut &one : sprites)
            {
                if (!ok)
                    break;
                const hldata::SpriteRef &ref = level.sprites[one.index];
                const std::uint32_t settings[] = {static_cast<std::uint32_t>(one.cell),
                                                  static_cast<std::uint32_t>(one.frames),
                                                  level.spriteLights[one.index] ? 1u : 0u, encoder.version(), 1u};
                const std::uint32_t hash =
                    one.sourceHash ^
                    hldata::crc32(reinterpret_cast<const std::uint8_t *>(settings), sizeof(settings)) * 131u;
                const fs::path file = stageDir() / "spr" /
                                      hldata::spriteFile(ref.name, ref.width, ref.height);
                std::vector<std::uint8_t> existing;
                std::string ignored;
                CookedTexture old;
                hldata::TextureHeader oldHeader;
                if (readFile(file, existing, ignored) && unpackTexture(existing, old, oldHeader) &&
                    oldHeader.sourceHash == hash)
                    continue;
                CookedTexture encoded;
                std::string why;
                if (!encoder.encode(one.cooked.atlas, hldata::kA3I5Colours, TextureFormat::A3I5, encoded, why) ||
                    !writeFileAtomic(file, packTexture(encoded, hldata::kTextureA3I5, hash), why))
                    result.warnings.push_back("the sprite '" + std::string(ref.name) + "' was not made: " + why);
            }
            std::error_code ec;
            fs::remove_all(temp, ec);
            if (!ok)
                return result;
            // (A skin not made is only a warning; not when it was stopped, or
            // the map would pass for finished.)
            if (cancel_ != nullptr && cancel_->load())
            {
                result.error = "stopped";
                return result;
            }
            result.texturesEncoded = texReport.encoded;
            for (const std::string &missing : texReport.missing)
                result.warnings.push_back("texture '" + missing + "' was found in no WAD");
        }

        // The models themselves, under the map's name: each holds the clips
        // THIS map plays.
        for (const auto &model : models)
        {
            const fs::path folder = model.lazy ? stageDir() / "wpn" : stageDir() / "mdl" / result.name;
            const fs::path file = folder / (model.name + ".hmd");
            if (!writeFileAtomic(file, model.bytes, result.error))
                return result;
            // (A DSi's, beside it -- and none left over from a cook that
            // had one where this has not.)
            const fs::path dsi = folder / (model.name + ".dsi.hmd");
            std::error_code gone;
            if (model.bytesDsi.empty())
                fs::remove(dsi, gone);
            else if (!writeFileAtomic(dsi, model.bytesDsi, result.error))
                return result;
        }
        for (const auto &model : guestModels)
        {
            if (model.finer ? !writeFileAtomic(stageDir() / "gst" / (model.name + ".dsi.hmd"),
                                               model.bytesDsi.empty() ? model.bytes : model.bytesDsi, result.error)
                            : !writeFileAtomic(stageDir() / "gst" / (model.name + ".hmd"), model.bytes, result.error))
                return result;
            if (model.indexed.width == 0)
                continue;
            // The 256-colour skin, beside it.
            CookedTexture skin;
            skin.width = model.indexed.width;
            skin.height = model.indexed.height;
            skin.texels = model.indexed.texels;
            for (int i = 0; i < 256; ++i)
            {
                const auto *rgb = &model.indexed.palette[static_cast<std::size_t>(i * 3)];
                const std::uint16_t c = static_cast<std::uint16_t>((rgb[0] >> 3) | ((rgb[1] >> 3) << 5) | ((rgb[2] >> 3) << 10));
                skin.palette.push_back(static_cast<std::uint8_t>(c & 0xFF));
                skin.palette.push_back(static_cast<std::uint8_t>(c >> 8));
            }
            const fs::path file = stageDir() / "gst" /
                                  hldata::modelSkinFile(model.name.c_str(), skin.width, skin.height);
            if (!writeFileAtomic(file, packTexture(skin, hldata::kTexturePal256 | hldata::kTextureRemapRuns, 0),
                                 result.error))
                return result;
        }
        result.actors = static_cast<int>(level.actors.size());

        // The level last: it is what makes the map loadable, so it lands only
        // once everything it names is on the card.
        const fs::path outPath = stageDir() / "maps" / (result.name + ".hlm");
        // (Beside it, the map's entity text as the game had it: for whoever
        // reads the stage, not for the console.)
        const std::vector<std::uint8_t> text(level.entityText.begin(), level.entityText.end());
        if (!writeFileAtomic(stageDir() / "maps" / (result.name + ".ent"), text, result.error) ||
            !writeFileAtomic(outPath, bytes, result.error))
            return result;

        result.ok = true;
        result.levelBytes = bytes.size();
        result.surfaces = report.surfaces;
        result.brushes = report.brushes;
        result.logicRecords = report.logicRecords;
        result.pathNodes = report.paths.nodes;
        result.pathLinks = report.paths.links;
        result.pathDoorLinks = report.paths.doorLinks;
        result.pathRefused = report.paths.refused;
        result.pathDrops = report.paths.dropLinks;
        result.pathFromGraph = report.paths.fromGraph;
        result.unsupportedClasses = static_cast<int>(report.unsupported.size());
        for (const auto &entry : report.unsupported)
        {
            result.unsupportedEntities += entry.second;
            result.unsupported.emplace_back(entry.first, entry.second);
        }
        result.faces = report.faces;
        result.faceVertices = report.faceVertices;
        result.vertices = report.vertices;
        result.streamKb = static_cast<int>(level.gxStream.size() * 4 / 1024);
        result.worstPolygons = level.worstPolygons;
        result.worstVertices = level.worstVertices;
        result.pvsWorstPolygons = report.pvsWorstPolygons;
        result.pvsWorstVertices = report.pvsWorstVertices;
        result.farUnits = static_cast<int>(level.farUnits);
        result.portals = report.portals;
        result.surfacesSpanningLeaves = report.surfacesSpanningLeaves;
        result.hotspots = report.hotspots;
        return result;
    }

    CommonResult Converter::cookCommon()
    {
        CommonResult result;
        const fs::path ui = stageDir();
        std::string why;
        // A file is written only if it is not already what it would be.
        auto put = [&](const fs::path &file, const std::vector<std::uint8_t> &bytes) -> bool
        {
            std::vector<std::uint8_t> existing;
            std::string ignored;
            if (readFile(file, existing, ignored) && existing == bytes)
                return true;
            return writeFileAtomic(file, bytes, why);
        };

        // The fonts: the game's two smaller menu faces, the one its credits
        // are in for titles, and its console's, in which what a map writes
        // fits the DS's screen with the game's own lines.
        struct Face
        {
            const char *wad, *lump;
            bool outlined;
            bool accented; ///< its 128..255 are letters (not a console's coloured copies)
        };
        const Face kFaces[hldata::kFontCount] = {
            {"fonts.wad", "FONT0", true, true}, {"fonts.wad", "FONT1", true, true}, {"gfx.wad", "CREDITSFONT", false, true},
            {"gfx.wad", "CONCHARS", false, false}};
        for (int n = 0; n < hldata::kFontCount; ++n)
        {
            std::vector<std::uint8_t> lump;
            QFont font;
            const fs::path wad = findGameFile(options_, kFaces[n].wad);
            std::vector<std::uint8_t> cooked;
            const bool read = !wad.empty() && readWadLump(wad, kFaces[n].lump, lump, why) && parseQFont(lump, font, why);
            if (read)
            {
                cooked = cookFont(font, kFaces[n].outlined, kFaces[n].accented);
                // (One the console would not take is not written: a font
                // that came out wrong once went on to the card.)
                if (!fontWhole(cooked))
                    why = "it came out malformed";
            }
            if (!read || !fontWhole(cooked) || !put(ui / hldata::kFontFiles[n], cooked))
            {
                // (And none of an earlier run's left to be packed instead.)
                std::error_code gone;
                fs::remove(ui / hldata::kFontFiles[n], gone);
                result.warnings.push_back(std::string("the font ") + kFaces[n].lump + " was not made" +
                                          (why.empty() ? "" : ": " + why));
                continue;
            }
            ++result.fonts;
        }

        // The menu's sounds, and the words of a match's last minute.
        for (int n = 0; n < hldata::kUiSoundCount + hldata::kMatchWarningWords; ++n)
        {
            const bool word = n >= hldata::kUiSoundCount;
            const int k = word ? n - hldata::kUiSoundCount : n;
            const char *const from = word ? hldata::kMatchWarningSources[k] : hldata::kUiSoundSources[k];
            const char *const to = word ? hldata::kMatchWarningFiles[k] : hldata::kUiSoundFiles[k];
            std::vector<std::uint8_t> source;
            WavData wav;
            const fs::path file = findGameFile(options_, "sound/" + std::string(from));
            if (file.empty() || !readFile(file, source, why) || !parseWav(source, wav, why))
            {
                result.warnings.push_back(std::string("the sound ") + from + " was not made");
                continue;
            }
            const std::uint32_t hash = hldata::crc32(source.data(), static_cast<std::uint32_t>(source.size())) ^
                                       hldata::kSoundVersion;
            const CookedSound cooked = encodeSound(wav, hldata::kSoundRate, false);
            if (put(ui / "snd" / (std::string(to) + ".hsn"), packSound(cooked, hash)))
                ++result.sounds;
        }

        // The pictures: these go through the texture encoder.
        if (!options_.textures)
            return result;
        const fs::path exe = findPtexconv(options_.ptexconv);
        if (exe.empty())
            return result;
        const fs::path temp = fs::temp_directory_path() / ("hlds-convert-" + processTag() + "-ui");
        PtexconvEncoder encoder(exe, temp, cancel_);
        auto texture = [&](const RgbaImage &image, TextureFormat format, int colours, std::uint32_t flags,
                           const char *name) -> bool
        {
            const std::uint32_t settings[] = {static_cast<std::uint32_t>(colours), flags, encoder.version()};
            const std::uint32_t hash =
                hldata::crc32(image.rgba.data(), static_cast<std::uint32_t>(image.rgba.size())) ^
                hldata::crc32(reinterpret_cast<const std::uint8_t *>(settings), sizeof(settings)) * 131u;
            std::vector<std::uint8_t> existing;
            std::string ignored;
            CookedTexture old;
            hldata::TextureHeader oldHeader;
            if (readFile(ui / name, existing, ignored) && unpackTexture(existing, old, oldHeader) &&
                oldHeader.sourceHash == hash)
                return true;
            CookedTexture encoded;
            return encoder.encode(image, colours, format, encoded, why) &&
                   writeFileAtomic(ui / name, packTexture(encoded, flags, hash), why);
        };
        {
            SprFile hud, crosshairs;
            const fs::path hudFile = findGameFile(options_, "sprites/320hud2.spr");
            const fs::path crossFile = findGameFile(options_, "sprites/crosshairs.spr");
            if (!hudFile.empty() && hud.load(hudFile, why) && !crossFile.empty() && crosshairs.load(crossFile, why) &&
                texture(hudAtlas(hud, crosshairs), TextureFormat::A3I5, hldata::kA3I5Colours, hldata::kTextureA3I5,
                        hldata::kHudAtlasFile))
                result.hud = true;
            else
                result.warnings.push_back("the head-up display's pictures were not made" +
                                          (why.empty() ? std::string() : ": " + why));
            // ... and at the game's 640 by 480 (the ROM takes this one when
            // it is there).
            SprFile hud7, hud8, hud9, pain;
            bool loaded = !crossFile.empty() && !crosshairs.frames.empty();
            for (auto [sprite, name] : {std::pair{&hud7, "sprites/640hud7.spr"}, std::pair{&hud8, "sprites/640hud8.spr"},
                                        std::pair{&hud9, "sprites/640hud9.spr"}, std::pair{&pain, "sprites/640_pain.spr"}})
            {
                const fs::path file = findGameFile(options_, name);
                loaded = loaded && !file.empty() && sprite->load(file, why);
            }
            if (!loaded || !texture(hudAtlas640(hud7, hud8, hud9, pain, crosshairs), TextureFormat::A3I5,
                                    hldata::kA3I5Colours, hldata::kTextureA3I5, hldata::kHud640File))
                result.warnings.push_back("the head-up display's 640 pictures were not made" +
                                          (why.empty() ? std::string() : ": " + why));
            // ... and of a train at the player's controls (both sets show it).
            SprFile train;
            const fs::path trainFile = findGameFile(options_, "sprites/640_train.spr");
            if (trainFile.empty() || !train.load(trainFile, why) ||
                !texture(hudTrainPictures(train), TextureFormat::A3I5, hldata::kA3I5Colours, hldata::kTextureA3I5,
                         hldata::kHudTrainFile))
                result.warnings.push_back("the train's pictures were not made" +
                                          (why.empty() ? std::string() : ": " + why));
        }
        {
            // The weapons' pictures for the touch screen's slots.
            SprFile sheet;
            const fs::path file = findGameFile(options_, "sprites/320hud1.spr");
            if (!file.empty() && sheet.load(file, why) && put(ui / hldata::kWeaponIconsFile, weaponIcons(sheet)))
                result.icons = true;
            else
                result.warnings.push_back("the weapons' pictures were not made" +
                                          (why.empty() ? std::string() : ": " + why));
        }
        {
            // The marks shots and blood leave.
            WadFile decals;
            int missing = 0;
            const fs::path file = findGameFile(options_, "decals.wad");
            if (!file.empty() && decals.open(file, why) &&
                texture(decalAtlas(decals, missing), TextureFormat::A3I5, hldata::kA3I5Colours, hldata::kTextureA3I5,
                        hldata::kDecalAtlasFile) &&
                missing == 0)
                result.decals = true;
            else
                result.warnings.push_back("the marks' pictures were not all made" +
                                          (why.empty() ? std::string() : ": " + why));
        }
        {
            // The game's title, for the first map.
            SprFile sheet;
            const fs::path file = findGameFile(options_, "sprites/320hud3.spr");
            if (!file.empty() && sheet.load(file, why) && put(ui / hldata::kTitleFile, titlePicture(sheet)))
                result.title = true;
            else
                result.warnings.push_back("the game's title was not made" + (why.empty() ? std::string() : ": " + why));
        }
        {
            // The spray logos: the game's logos/ folder (the player's own, as
            // on a PC), then what spraypaint.wad has that it has not.
            std::vector<NamedPicture> logos;
            std::set<std::string> named;
            const fs::path folder = findNoCase(options_.gameDir, "logos");
            std::vector<fs::path> files;
            std::error_code ec;
            for (auto it = fs::directory_iterator(folder, ec); !folder.empty() && !ec && it != fs::directory_iterator();
                 it.increment(ec))
            {
                std::string extension = it->path().extension().string();
                for (char &c : extension)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (it->is_regular_file(ec) && extension == ".bmp")
                    files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            int unread = 0;
            for (const fs::path &file : files)
            {
                std::string name = file.stem().string();
                for (char &c : name)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                NamedPicture logo;
                std::string ignored;
                if (!readBmp8(file, logo.picture, ignored))
                {
                    ++unread;
                    continue;
                }
                if (named.insert(name).second)
                {
                    logo.name = name;
                    logos.push_back(std::move(logo));
                }
            }
            WadFile paint;
            const fs::path wad = findGameFile(options_, "spraypaint.wad");
            if (!wad.empty() && paint.open(wad, why))
            {
                for (const std::string &name : paint.names())
                {
                    IndexedImage image;
                    std::string ignored;
                    if (named.count(name) != 0 || !paint.read(name, image, ignored))
                        continue;
                    named.insert(name);
                    logos.push_back(NamedPicture{name, toRgba(image, false)});
                }
            }
            if (logos.empty() || !put(ui / hldata::kLogosFile, logoAtlas(logos)))
                result.warnings.push_back("the spray logos were not made: neither logos/ nor spraypaint.wad was read");
            if (unread > 0)
                result.warnings.push_back(std::to_string(unread) +
                                          " of the spray logos were not 8-bit bitmaps, and were left out");
            if (static_cast<int>(logos.size()) > hldata::kLogosMost)
                result.warnings.push_back("there are " + std::to_string(logos.size()) + " spray logos: the first " +
                                          std::to_string(hldata::kLogosMost) + " are kept");
        }
        {
            std::vector<std::uint8_t> lump;
            IndexedImage picture;
            const fs::path wad = findGameFile(options_, "cached.wad");
            if (!wad.empty() && readWadLump(wad, "LOADING", lump, why) && parseQPic(lump, picture, why) &&
                texture(loadingTexture(picture), TextureFormat::Tex4x4, 1024, 0, hldata::kLoadingFile))
                result.loading = true;
            else
                result.warnings.push_back("the loading picture was not made" +
                                          (why.empty() ? std::string() : ": " + why));
        }
        // The title screen as the game has it: its menu background, its
        // animated title, and the Valve intro played before it (the film's
        // pictures loose on the card, read as it plays; its sound as the
        // music's track kIntroTrack).
        {
            RgbaImage background;
            const auto find = [&](const std::string &relative) { return findGameFile(options_, relative); };
            // Who else a player may be seen as (HlCharacters.hpp).
            std::vector<std::string> clips;
            {
                std::vector<ModelNeed> one;
                std::vector<Entity> dm(1);
                dm[0].pairs.emplace_back("classname", "info_player_deathmatch");
                addMultiplayerNeeds(dm, one);
                if (!one.empty())
                    clips = one[0].clips;
            }
            // ... each for a console with the game, and for one sent it (a
            // smaller skin); and the suit for a console sent the game to show
            // in its lobby, in a pose of its own.
            auto cookPerson = [&](const std::string &file, const std::vector<std::string> &own, int skinTexels,
                                  const fs::path &modelOut, const fs::path &skinOut, bool finest = false)
            {
                const fs::path found = findGameFile(options_, file);
                MdlFile mdl;
                CookedModel cooked;
                ModelCookReport modelReport;
                std::vector<std::uint8_t> bytes;
                std::string failed;
                if (found.empty() || !mdl.load(found, failed) ||
                    !cookModel(mdl, own, cooked, modelReport, failed, skinTexels, finest, finest ? ModelLean() : kGuestLean) ||
                    !writeModel(cooked, bytes, failed) || cooked.indexed.width == 0)
                    return;
                CookedTexture skin;
                skin.width = cooked.indexed.width;
                skin.height = cooked.indexed.height;
                skin.texels = cooked.indexed.texels;
                for (int i = 0; i < 256; ++i)
                {
                    const auto *rgb = &cooked.indexed.palette[static_cast<std::size_t>(i * 3)];
                    const std::uint16_t colour =
                        static_cast<std::uint16_t>((rgb[0] >> 3) | ((rgb[1] >> 3) << 5) | ((rgb[2] >> 3) << 10));
                    skin.palette.push_back(static_cast<std::uint8_t>(colour & 0xFF));
                    skin.palette.push_back(static_cast<std::uint8_t>(colour >> 8));
                }
                std::error_code made;
                fs::create_directories(modelOut.parent_path(), made);
                writeFileAtomic(modelOut, bytes, failed);
                writeFileAtomic(skinOut, packTexture(skin, hldata::kTexturePal256 | hldata::kTextureRemapRuns, 0), failed);
            };
            for (int c = 1; c < hldata::kCharacterCount; ++c)
            {
                const std::string name = hldata::kCharacterFiles[c];
                const std::string file = "models/player/" + name + "/" + name + ".mdl";
                cookPerson(file, clips, 128 * 128, stageDir() / "chr" / (name + ".hmd"), stageDir() / "chr" / (name + ".htx"));
                cookPerson(file, clips, 64 * 64, stageDir() / "chr" / ("g_" + name + ".hmd"), stageDir() / "chr" / ("g_" + name + ".htx"));
                cookPerson(file, clips, hldata::kFinePlayerSkinTexels, stageDir() / "chr" / (name + ".dsi.hmd"),
                           stageDir() / "chr" / (name + ".dsi.htx"), true);
            }
            cookPerson("models/player/gordon/gordon.mdl", {"look_idle"}, 64 * 64, stageDir() / "gst" / "lobby.hmd",
                       stageDir() / "gst" / "lobby.htx");
            if (menuBackground(find, background, why) &&
                texture(background, TextureFormat::Tex4x4, 1024, 0, hldata::kMenuFile))
                result.menu = true;
            else
                result.warnings.push_back("the menu's background was not made" +
                                          (why.empty() ? std::string() : ": " + why));
        }
        {
            std::vector<std::uint8_t> bytes;
            AviFile avi;
            const fs::path file = findGameFile(options_, "media/logo.avi");
            if (!file.empty() && readFile(file, bytes, why) && avi.load(bytes, why) &&
                put(ui / hldata::kLogoFile, logoFile(avi.frames, avi.frameMicros)))
                result.logo = true;
            else
                result.warnings.push_back("the animated title was not made" +
                                          (why.empty() ? std::string() : ": " + why));
        }
        {
            std::vector<std::uint8_t> bytes;
            const fs::path file = findGameFile(options_, "media/valve.avi");
            const fs::path out = dataDir(options_.cardRoot) / hldata::kIntroFile;
            if (!file.empty() && readFile(file, bytes, why))
            {
                const std::uint32_t source =
                    hldata::crc32(bytes.data(), static_cast<std::uint32_t>(bytes.size())) ^ hldata::kIntroVersion ^
                    encoder.version() * 131u;
                // (Made once: the same film is not made again.)
                std::vector<std::uint8_t> head;
                hldata::IntroHeader old{};
                std::error_code ec;
                bool same = false;
                if (std::FILE *f = std::fopen(out.string().c_str(), "rb"))
                {
                    same = std::fread(&old, sizeof(old), 1, f) == 1 && std::memcmp(old.magic, hldata::kIntroMagic, 4) == 0 &&
                           old.version == hldata::kIntroVersion && old.sourceHash == source;
                    std::fclose(f);
                }
                AviFile avi;
                if (same)
                {
                    result.intro = true;
                }
                else if (avi.load(bytes, why))
                {
                    // Where the film is picture: what is not black in any of
                    // its frames (it plays in the middle of a black frame).
                    int left = avi.width, top = avi.height, right = 0, bottom = 0;
                    for (const RgbaImage &frame : avi.frames)
                        for (int y = 0; y < frame.height; ++y)
                            for (int x = 0; x < frame.width; ++x)
                            {
                                const std::uint8_t *p = frame.at(x, y);
                                if (p[0] > 12 || p[1] > 12 || p[2] > 12)
                                {
                                    left = std::min(left, x);
                                    right = std::max(right, x + 1);
                                    top = std::min(top, y);
                                    bottom = std::max(bottom, y + 1);
                                }
                            }
                    if (right <= left || bottom <= top)
                    {
                        left = top = 0;
                        right = avi.width;
                        bottom = avi.height;
                    }
                    // As big as it is on a PC's 640x480 (two fifths, on the
                    // screen's 256), and no bigger than its texture.
                    const int cropW = right - left, cropH = bottom - top;
                    double scale = 256.0 / avi.width;
                    scale = std::min({scale, static_cast<double>(hldata::kIntroTextureSize) / cropW,
                                      static_cast<double>(hldata::kIntroTextureSize) / cropH});
                    const int pictureW = std::max(4, static_cast<int>(cropW * scale + 0.5));
                    const int pictureH = std::max(4, static_cast<int>(cropH * scale + 0.5));
                    // (Where it is on the screen: where it is in the film.)
                    const double screenScale = 256.0 / avi.width;
                    const int centreX = static_cast<int>((left + right) / 2.0 * screenScale);
                    const int centreY = static_cast<int>((top + bottom) / 2.0 * (192.0 / avi.height));
                    hldata::IntroHeader header{};
                    std::memcpy(header.magic, hldata::kIntroMagic, 4);
                    header.version = hldata::kIntroVersion;
                    header.frames = static_cast<std::uint16_t>(avi.frames.size());
                    header.frameMicros = avi.frameMicros;
                    header.frameBytes = hldata::kIntroFrameBytes;
                    header.paletteColours = hldata::kIntroPaletteColours;
                    header.textureSize = hldata::kIntroTextureSize;
                    header.pictureWidth = static_cast<std::uint16_t>(pictureW);
                    header.pictureHeight = static_cast<std::uint16_t>(pictureH);
                    header.pictureX = static_cast<std::uint16_t>(std::clamp(centreX - pictureW / 2, 0, 256 - pictureW));
                    header.pictureY = static_cast<std::uint16_t>(std::clamp(centreY - pictureH / 2, 0, 192 - pictureH));
                    header.sourceHash = source;
                    std::vector<std::uint8_t> film(sizeof(header) +
                                                   static_cast<std::size_t>(header.frames) * hldata::kIntroFrameBytes);
                    std::memcpy(film.data(), &header, sizeof(header));
                    bool ok = true;
                    for (std::size_t n = 0; n < avi.frames.size() && ok; ++n)
                    {
                        // Its picture, cut out and scaled, in the top left of
                        // a small square texture.
                        const RgbaImage &frame = avi.frames[n];
                        RgbaImage crop;
                        crop.width = cropW;
                        crop.height = cropH;
                        crop.rgba.resize(static_cast<std::size_t>(cropW) * cropH * 4);
                        for (int y = 0; y < cropH; ++y)
                            std::memcpy(crop.at(0, y), frame.at(left, top + y), static_cast<std::size_t>(cropW) * 4);
                        const RgbaImage scaled = resample(crop, pictureW, pictureH);
                        RgbaImage square;
                        square.width = square.height = hldata::kIntroTextureSize;
                        square.rgba.assign(static_cast<std::size_t>(square.width) * square.height * 4, 0);
                        for (std::size_t i = 3; i < square.rgba.size(); i += 4)
                            square.rgba[i] = 255;
                        for (int y = 0; y < pictureH; ++y)
                            std::memcpy(square.at(0, y), scaled.at(0, y), static_cast<std::size_t>(pictureW) * 4);
                        CookedTexture encoded;
                        ok = encoder.encode(square, hldata::kIntroPaletteColours, TextureFormat::Tex4x4, encoded, why) &&
                             encoded.texels.size() == hldata::kIntroTexelBytes &&
                             encoded.index.size() == hldata::kIntroIndexBytes &&
                             encoded.palette.size() <= static_cast<std::size_t>(hldata::kIntroPaletteColours) * 2;
                        if (!ok)
                            break;
                        std::uint8_t *at = film.data() + sizeof(header) + n * hldata::kIntroFrameBytes;
                        std::memcpy(at, encoded.texels.data(), encoded.texels.size());
                        std::memcpy(at + hldata::kIntroTexelBytes, encoded.index.data(), encoded.index.size());
                        std::memcpy(at + hldata::kIntroTexelBytes + hldata::kIntroIndexBytes, encoded.palette.data(),
                                    encoded.palette.size());
                    }
                    // Its sound: the music's way (the same both sides).
                    if (ok && !avi.samples.empty() && avi.sampleRate > 0)
                    {
                        std::vector<std::int16_t> mono = avi.samples;
                        if (avi.channels == 2)
                        {
                            mono.clear();
                            for (std::size_t i = 0; i + 1 < avi.samples.size(); i += 2)
                                mono.push_back(static_cast<std::int16_t>((avi.samples[i] + avi.samples[i + 1]) / 2));
                        }
                        char name[16];
                        hldata::musicFileName(hldata::kIntroTrack, name, sizeof(name));
                        const fs::path music = dataDir(options_.cardRoot) / "music";
                        fs::create_directories(music, ec);
                        ok = writeFileAtomic(music / name, encodeMusicStereo(mono, mono, avi.sampleRate), why);
                    }
                    if (ok && writeFileAtomic(out, film, why))
                        result.intro = true;
                }
            }
            if (!result.intro)
                result.warnings.push_back("the Valve intro was not made" + (why.empty() ? std::string() : ": " + why));
        }
        std::error_code ec;
        fs::remove_all(temp, ec);
        return result;
    }

    int Converter::cookMaps(const std::vector<std::string> &names,
                            const std::function<void(std::size_t, const std::string &)> &progress,
                            const std::function<void(const MapResult &)> &done,
                            const std::atomic<bool> *cancel, std::string &error)
    {
        int failed = 0;
        const auto say = [&](const char *what)
        {
            if (options_.status)
                options_.status(what);
        };
        const auto cancelled = [&]() { return cancel != nullptr && cancel->load(); };
        struct Holding
        {
            const std::atomic<bool> *&held;
            ~Holding() { held = nullptr; }
        } holding{cancel_};
        cancel_ = cancel;
        say("Preparing what all the maps share");
        common_ = cookCommon();
        if (cancelled())
            return failed;
        {
            PackReport report;
            std::string why;
            if (!packCommon(stageDir(), dataDir(options_.cardRoot), options_.compress, report, why))
                common_.warnings.push_back("the common archive was not made: " + why);
        }
        // Process several maps at once. (Fewer in a 32-bit program: a big
        // map takes about 230 MB as it is cooked, and such a program has 2 GB
        // in all on a 32-bit Windows.)
        constexpr unsigned kMostJobs = sizeof(void *) < 8 ? 3u : 6u;
        unsigned jobs = options_.jobs > 0 ? static_cast<unsigned>(options_.jobs)
                                          : std::min(std::max(1u, std::thread::hardware_concurrency()), kMostJobs);
        jobs = std::max(1u, std::min<unsigned>(jobs, static_cast<unsigned>(names.size())));
        std::atomic<std::size_t> next{0};
        std::atomic<int> failedCount{0};
        std::mutex callbacks;
        auto work = [&]()
        {
            for (;;)
            {
                const std::size_t i = next.fetch_add(1);
                if (i >= names.size() || (cancel != nullptr && cancel->load()))
                    break;
                if (progress)
                {
                    const std::lock_guard<std::mutex> lock(callbacks);
                    progress(i, names[i]);
                }
                MapResult result = cookMap(names[i]);
                if (result.ok)
                    packInto(result);
                if (!result.ok)
                    ++failedCount;
                if (done)
                {
                    const std::lock_guard<std::mutex> lock(callbacks);
                    done(result);
                }
            }
        };
        if (jobs <= 1)
            work();
        else
        {
            std::vector<std::thread> pool;
            for (unsigned n = 0; n < jobs; ++n)
                pool.emplace_back(work);
            for (std::thread &thread : pool)
                thread.join();
        }
        failed += failedCount.load();
        if (cancelled())
            return failed;
        say("Packing what all the maps share");
        // (Again, now that the maps are: what their people say is in it.)
        {
            PackReport report;
            std::string why;
            if (!packCommon(stageDir(), dataDir(options_.cardRoot), options_.compress, report, why))
                common_.warnings.push_back("the common archive was not made: " + why);
        }

        say("Packing the maps for Download Play");
        packAirGroups();
        say("Writing the list of maps");
        if (!writeManifest(error))
            return failed + 1;
        return failed;
    }

    int Converter::packAll(const std::function<void(const MapResult &)> &done, std::string &error)
    {
        int failed = 0;
        PackReport commonReport;
        std::string why;
        if (!packCommon(stageDir(), dataDir(options_.cardRoot), options_.compress, commonReport, why))
            common_.warnings.push_back("the common archive was not made: " + why);
        std::vector<std::string> names;
        std::error_code ec;
        for (auto it = fs::directory_iterator(stageDir() / "maps", ec);
             !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            if (it->path().extension() == ".hlm")
                names.push_back(it->path().stem().string());
        }
        std::sort(names.begin(), names.end());
        for (const std::string &name : names)
        {
            MapResult result;
            result.name = name;
            result.ok = true;
            packInto(result);
            if (!result.ok)
                ++failed;
            if (done)
                done(result);
        }
        packAirGroups();
        if (!writeManifest(error))
            return failed + 1;
        return failed;
    }

    bool Converter::writeManifest(std::string &error) const
    {
        // The manifest is rebuilt from what is ACTUALLY on the card, not from
        // what this run did: a run of one map must not forget the hundred an
        // earlier run cooked, and a cancelled run must not list maps it never
        // reached. Each level's own header is the source.
        hldata::Manifest manifest;
        manifest.converterVersion = kConverterVersion;
        const fs::path dir = dataDir(options_.cardRoot);
        std::string previousStart;
        {
            std::vector<std::uint8_t> old;
            std::string ignored;
            hldata::Manifest previous;
            if (readFile(dir / "manifest.bin", old, ignored) &&
                hldata::readManifest(old.data(), old.size(), previous) ==
                    hldata::ManifestError::Ok)
                previousStart = previous.startMap;
        }

        std::error_code ec;
        for (auto it = fs::directory_iterator(dir / "maps", ec);
             !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            if (it->path().extension() != ".npac")
                continue;
            std::vector<std::uint8_t> bytes;
            std::string ignored;
            std::size_t archiveBytes = 0;
            hldata::LevelView view;
            // Every member inflated, the level put together and its
            // checksum taken: an archive that does not come back whole is
            // not listed, so the DS never offers a map it would then refuse.
            if (!unpackLevel(it->path(), bytes, archiveBytes, ignored) ||
                view.open(bytes.data(), static_cast<std::uint32_t>(bytes.size())) !=
                    hldata::LevelError::Ok)
                continue;
            hldata::ManifestMap map;
            map.name = it->path().stem().string();
            map.levelBytes = static_cast<std::uint32_t>(bytes.size());
            map.archiveBytes = static_cast<std::uint32_t>(archiveBytes);
            map.worstPolygons = view.header->worstPolygons;
            map.worstVertices = view.header->worstVertices;
            map.textureCount = static_cast<std::uint16_t>(view.textureCount);
            for (std::uint32_t e = 0; e < view.entityCount; ++e)
            {
                if (view.entities[e].classId == static_cast<std::uint16_t>(hldata::EntityClass::InfoPlayerDeathmatch) &&
                    map.spawns < 255)
                    ++map.spawns;
            }
            // (What it is sent as to a console with no card, if anything.)
            const fs::path air = dir / "air" / (map.name + ".hgs");
            std::error_code missing;
            if (fs::is_regular_file(air, missing))
            {
                map.airBytes = static_cast<std::uint32_t>(fs::file_size(air, missing));
                const auto guest = common_.guestByMap.find(map.name);
                if (guest != common_.guestByMap.end())
                {
                    map.guestPeakBytes = guest->second.peak;
                    map.guestFits = guest->second.fits;
                }
                map.airGroup = static_cast<std::uint8_t>(
                    map.name.rfind("t0a0", 0) == 0 ? hldata::AirGroup::Hazard
                    : map.name.rfind("hldemo", 0) == 0 ? hldata::AirGroup::Uplink
                                                       : hldata::AirGroup::Match);
            }
            manifest.maps.push_back(std::move(map));
        }
        std::sort(manifest.maps.begin(), manifest.maps.end(),
                  [](const hldata::ManifestMap &a, const hldata::ManifestMap &b)
                  { return a.name < b.name; });
        manifest.airBaseBytes.assign(static_cast<std::size_t>(hldata::AirGroup::Count), 0);
        for (int g = 1; g < static_cast<int>(hldata::AirGroup::Count); ++g)
        {
            std::error_code missing;
            const fs::path base = dir / "air" / (std::string(hldata::kAirGroupNames[g]) + ".base.hgs");
            if (fs::is_regular_file(base, missing))
                manifest.airBaseBytes[static_cast<std::size_t>(g)] =
                    static_cast<std::uint32_t>(fs::file_size(base, missing));
        }

        // The start map: what was asked for, else what it was, else the
        // game's first map, else whatever there is.
        for (const std::string &candidate :
             {lower(options_.startMap), previousStart, std::string(kDefaultStartMap)})
        {
            if (!candidate.empty() && manifest.find(candidate) != nullptr)
            {
                manifest.startMap = candidate;
                break;
            }
        }
        if (manifest.startMap.empty() && !manifest.maps.empty())
            manifest.startMap = manifest.maps.front().name;

        if (!manifest.maps.empty() &&
            !writeFileAtomic(dir / "manifest.bin", hldata::writeManifest(manifest), error))
            return false;
        return true;
    }
    const char *cdTrackFile(int track)
    {
        static const char *const kFiles[] = {
            "half-life01.mp3", "prospero01.mp3", "half-life12.mp3", "half-life07.mp3", "half-life10.mp3",  //  2..6
            "suspense01.mp3",  "suspense03.mp3", "half-life09.mp3", "half-life02.mp3", "half-life13.mp3",  //  7..11
            "half-life04.mp3", "half-life15.mp3", "half-life14.mp3", "half-life16.mp3", "suspense02.mp3",  // 12..16
            "half-life03.mp3", "half-life08.mp3", "prospero02.mp3", "half-life05.mp3", "prospero04.mp3",   // 17..21
            "half-life11.mp3", "half-life06.mp3", "prospero03.mp3", "half-life17.mp3", "prospero05.mp3",   // 22..26
            "suspense05.mp3",  "suspense07.mp3"};                                                         // 27..28
        const int at = track - 2;
        return at >= 0 && at < static_cast<int>(sizeof(kFiles) / sizeof(kFiles[0])) ? kFiles[at] : nullptr;
    }

    std::string chapterOf(const std::string &map, const std::vector<std::pair<std::string, std::string>> &titled)
    {
        // (Uplink's maps name no chapter, and the map sorted before them is
        // the game's last chapter's: they are Uplink.)
        if (map.rfind("hldemo", 0) == 0)
            return "UPLINK";
        const std::string head = map.substr(0, 4);
        std::string like, likeFirst, before;
        for (const auto &[name, title] : titled)
        {
            if (name.substr(0, 4) == head)
            {
                if (likeFirst.empty())
                    likeFirst = title;
                if (name <= map)
                    like = title;
            }
            if (name <= map)
                before = title;
        }
        if (!like.empty())
            return like;
        if (!likeFirst.empty())
            return likeFirst;
        if (!before.empty())
            return before;
        // (The game's first maps name none: their chapter's title is shown
        // by the ride itself.)
        return map.rfind("c0a0", 0) == 0 ? "C0A0TITLE" : std::string();
    }

    std::vector<std::string> gameLanguages(const std::filesystem::path &gameDir)
    {
        std::vector<std::string> out;
        const fs::path dir = gameDir.has_filename() ? gameDir : gameDir.parent_path();
        const std::string prefix = lower(dir.filename().string()) + "_";
        std::error_code ec;
        for (auto it = fs::directory_iterator(dir.parent_path(), ec); !ec && it != fs::directory_iterator();
             it.increment(ec))
        {
            const std::string name = it->path().filename().string();
            const std::string low = lower(name);
            if (!it->is_directory(ec) || low.size() <= prefix.size() || low.compare(0, prefix.size(), prefix) != 0)
                continue;
            if (findNoCase(it->path(), "titles.txt").empty() && findNoCase(it->path(), "resource/" + name + ".txt").empty())
                continue;
            out.push_back(low.substr(prefix.size()));
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::filesystem::path languageFolder(const std::filesystem::path &gameDir, const std::string &language)
    {
        const std::string wanted = lower(language);
        for (const std::string &found : gameLanguages(gameDir))
        {
            if (found == wanted)
            {
                const fs::path dir = gameDir.has_filename() ? gameDir : gameDir.parent_path();
                return findNoCase(dir.parent_path(), dir.filename().string() + "_" + found);
            }
        }
        return fs::path();
    }

    std::vector<std::pair<std::string, std::string>> chapterTitles(const std::filesystem::path &gameDir,
                                                                   const TitlesBook *titles)
    {
        std::vector<std::pair<std::string, std::string>> out;
        std::error_code ignored;
        const fs::path maps = findNoCase(gameDir, "maps");
        if (maps.empty())
            return out;
        for (const auto &entry : fs::directory_iterator(maps, ignored))
        {
            if (entry.path().extension() != ".bsp")
                continue;
            // The map's entities are a text at the place its first lump
            // says; the world is the first of them.
            std::vector<std::uint8_t> bytes;
            std::string why;
            if (!readFile(entry.path(), bytes, why) || bytes.size() < 12)
                continue;
            std::uint32_t at = 0, length = 0;
            std::memcpy(&at, bytes.data() + 4, 4);
            std::memcpy(&length, bytes.data() + 8, 4);
            if (at > bytes.size() || length > bytes.size() - at)
                continue;
            const std::string text(bytes.begin() + at, bytes.begin() + at + length);
            const std::size_t end = text.find("}");
            const std::string world = text.substr(0, end);
            const std::size_t key = world.find("\"chaptertitle\"");
            if (key == std::string::npos)
                continue;
            const std::size_t open = world.find('"', key + 14);
            const std::size_t close = open == std::string::npos ? open : world.find('"', open + 1);
            if (close == std::string::npos)
                continue;
            std::string title = world.substr(open + 1, close - open - 1);
            // (One that says nothing opens no chapter.)
            const Title *said = titles != nullptr ? titles->find(title) : nullptr;
            bool blank = said == nullptr;
            if (said != nullptr)
            {
                blank = true;
                for (char c : said->text)
                    blank = blank && (c == ' ' || c == '\n' || c == '\r' || c == '\t');
            }
            if (title.empty() || blank)
                continue;
            std::string name = entry.path().stem().string();
            for (char &c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            out.emplace_back(name, title);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
} // namespace hlds
