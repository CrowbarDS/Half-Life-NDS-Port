// What goes on the card: a map's archive, made from the stage.
//
// The console reads a map out of its archive and nothing else, so the
// archive must hold exactly what the level names, under the names the
// console will ask for, with the level's pieces coming back to the level --
// and it must do so in the order of a load, which is the point of it.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "HlLevelReader.hpp"
#include "HlSound.hpp"
#include "HlWeaponBundle.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/BspFile.hpp"
#include "hlds/TextureBudget.hpp"
#include "hlds/Converter.hpp"
#include "hlds/NpacWriter.hpp"
#include "hlds/Pack.hpp"

namespace fs = std::filesystem;
using namespace hldata;
using namespace hldstest;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    void testPieces()
    {
        check(levelPieces(kLevelDataStart) == 1 && levelPieces(kLevelDataStart + 1) == 2 &&
                  levelPieces(kLevelDataStart + kLevelPieceBytes) == 2 &&
                  levelPieces(kLevelDataStart + kLevelPieceBytes + 1) == 3,
              "how many pieces a level is in");
        for (const std::uint32_t size : {kLevelDataStart, kLevelDataStart + 5, 700000u, 6400000u})
        {
            std::uint32_t at = 0;
            bool whole = true;
            for (std::uint32_t piece = 0; piece < levelPieces(size); ++piece)
            {
                whole = whole && levelPieceStart(piece) == at && levelPieceBytes(size, piece) != 0 &&
                        levelPieceBytes(size, piece) <= kLevelPieceBytes;
                at += levelPieceBytes(size, piece);
            }
            check(whole && at == size && levelPieceBytes(size, levelPieces(size)) == 0,
                  "the pieces are the level, end to end, with nothing over");
        }
    }

    /// The archive's members against the stage: each is the staged file, the
    /// level's pieces are the level, and the folders come in load order.
    void checkArchive(const fs::path &stage, const fs::path &card, const std::string &map, bool wantCompressed)
    {
        Bytes archive, level;
        std::string error;
        std::vector<hlds::NpacMember> members;
        check(hlds::readFile(card / "maps" / (map + ".npac"), archive, error) &&
                  hlds::readNpac(archive, members, error),
              "the archive reads");
        hlds::readFile(stage / "maps" / (map + ".hlm"), level, error);

        Bytes joined;
        std::size_t next = 0;
        while (next < members.size() && members[next].path == "level." + std::to_string(next))
        {
            joined.insert(joined.end(), members[next].data.begin(), members[next].data.end());
            ++next;
        }
        check(!level.empty() && joined == level && next == levelPieces(static_cast<std::uint32_t>(level.size())),
              "the level's pieces, first in the archive, are the staged level byte for byte");
        check(members[0].data.size() == kLevelDataStart && members[0].method == hlds::kNpacStored,
              "its first piece is the header and directory, not compressed");

        // Folders in the order of a load, each one's files together.
        static const char *const kOrder[] = {"tex/", "spr/", "mdl/", "snd/"};
        int folder = 0;
        bool ordered = true, same = true, soundsStored = true;
        int compressed = 0;
        for (std::size_t i = next; i < members.size(); ++i)
        {
            const hlds::NpacMember &m = members[i];
            while (folder < 4 && m.path.rfind(kOrder[folder], 0) != 0)
                ++folder;
            ordered = ordered && folder < 4;
            compressed += m.method != hlds::kNpacStored ? 1 : 0;
            if (folder == 3)
                soundsStored = soundsStored && m.method == hlds::kNpacStored;
            // (A model's file is in the stage under its map.)
            fs::path source = stage / m.path;
            if (folder == 2 && m.path.size() > 4 && m.path.substr(m.path.size() - 4) == ".hmd")
                source = stage / "mdl" / map / m.path.substr(4);
            Bytes staged;
            same = same && hlds::readFile(source, staged, error) && staged == m.data;
        }
        check(ordered, "then textures, sprites, models and sounds, in that order");
        check(same, "every member is the staged file");
        check(soundsStored, "sounds are never compressed");
        for (std::size_t i = 0; i < next; ++i)
            compressed += members[i].method != hlds::kNpacStored ? 1 : 0;
        check(wantCompressed ? compressed != 0 || level.size() < 4096 : compressed == 0,
              "compression is used, or not at all when told not to");

        // Everything the level names that the stage has is in the archive.
        LevelView view;
        check(view.open(level.data(), static_cast<std::uint32_t>(level.size())) == LevelError::Ok,
              "the staged level opens");
        auto has = [&](const std::string &path)
        {
            for (const hlds::NpacMember &m : members)
                if (m.path == path)
                    return true;
            return false;
        };
        bool all = true;
        for (std::uint32_t i = 0; i < view.textureCount; ++i)
        {
            char name[40];
            textureFileName(view.textures[i].name, view.textures[i].width, view.textures[i].height,
                            (view.textures[i].flags & kTextureA3I5) != 0, name);
            all = all && (!fs::exists(stage / "tex" / name) || has(std::string("tex/") + name));
        }
        // (The weapons the map has not, and their sounds, are in the common
        // archive, and not in the map's.)
        Bytes commonBytes;
        std::vector<hlds::NpacMember> common;
        // (A game with nothing of its own to share has none.)
        check(!fs::exists(card / "common.npac") ||
                  (hlds::readFile(card / "common.npac", commonBytes, error) &&
                   hlds::readNpac(commonBytes, common, error)),
              "the common archive reads");
        auto inCommon = [&](const std::string &path)
        {
            for (const hlds::NpacMember &m : common)
                if (m.path == path)
                    return true;
            return false;
        };
        bool lazyApart = true;
        int lazyViews = 0, lazySounds = 0;
        bool lazyBundled = true;
        for (std::uint32_t i = 0; i < view.modelRefCount; ++i)
        {
            const std::string name = view.modelRefs[i].name;
            if (modelRefLazy(view.modelRefs[i]))
            {
                lazyViews += name.rfind("v_", 0) == 0 ? 1 : 0;
                lazyBundled = lazyBundled && hldata::bundlesOf(name.c_str()) != 0;
                lazyApart = lazyApart && !has("mdl/" + name + ".hmd") && inCommon("wpn/" + name + ".hmd");
                continue;
            }
            all = all && has("mdl/" + name + ".hmd");
        }
        // (The words of what its people say are in the common archive.)
        std::vector<char> talk;
        hlds::talkWords(view, talk);
        for (std::uint32_t i = 0; i < view.soundCount; ++i)
        {
            const std::string name = view.sounds[i].name;
            if (soundLazy(view.sounds[i]) && !name.empty())
            {
                ++lazySounds;
                lazyApart = lazyApart && !has("snd/" + name + ".hsn") && inCommon("snd/" + name + ".hsn");
                continue;
            }
            all = all && (name.empty() || talk[i] != 0 || !fs::exists(stage / "snd" / (name + ".hsn")) ||
                          has("snd/" + name + ".hsn"));
        }
        check(all, "all that the level names is there, under the console's names");
        check(lazyApart, "the weapons it has not, and their sounds, are in the common archive instead");
        if (map == "c1a0")
        {
            // (Nothing is held in it yet: every weapon is one it has not --
            // the MP5's shot and reload clicks among the sounds.)
            check(lazyViews == 15, "c1a0 names every view model, none of them its own");
            check(lazyBundled, "what it leaves to the common archive is all in the weapons' bundles");
            check(lazySounds > 15, "... and their sounds, the guns' own and their clips'");
        }
    }

    /// Every TEXCOORD a surface's commands give, in order.
    std::vector<std::uint32_t> coordsOf(const LevelView &view, const Surface &surface)
    {
        std::vector<std::uint32_t> coords;
        std::uint32_t at = surface.gxOfs;
        const std::uint32_t end = surface.gxOfs + surface.gxWords;
        while (at < end)
        {
            const std::uint32_t header = view.gxStream[at++];
            for (int k = 0; k < 4 && static_cast<std::uint8_t>(header >> (8 * k)) != 0; ++k)
            {
                const auto code = static_cast<std::uint8_t>(header >> (8 * k));
                if (code == 0x22)
                    coords.push_back(view.gxStream[at]);
                at += code == 0x23 ? 2 : 1;
            }
        }
        return coords;
    }

    /// A guest's level: the size said, the coordinates with it, nothing moved.
    void testGuestTextures(const fs::path &staged)
    {
        Bytes level;
        std::string error;
        check(hlds::readFile(staged, level, error), "the level for a guest is read");
        LevelView view;
        check(view.open(level.data(), static_cast<std::uint32_t>(level.size())) == LevelError::Ok &&
                  view.textureCount != 0,
              "and opens, with a texture");
        const Surface *surface = nullptr;
        for (std::uint32_t i = 0; i < view.surfaceCount && surface == nullptr; ++i)
        {
            if (view.surfaces[i].texture < view.textureCount && !coordsOf(view, view.surfaces[i]).empty())
                surface = &view.surfaces[i];
        }
        check(surface != nullptr, "a surface wears it");
        if (surface == nullptr)
            return;
        const std::size_t index = static_cast<std::size_t>(surface - view.surfaces);
        const TextureRef host = view.textures[surface->texture];
        const std::vector<std::uint32_t> before = coordsOf(view, *surface);
        const std::size_t size = level.size();
        // (One halving across, two down.)
        Bytes guest = level;
        reinterpret_cast<TextureRef *>(guest.data() + (reinterpret_cast<const std::uint8_t *>(&view.textures[surface->texture]) - level.data()))
            ->guestHalvings = 1 | 2 << 4;
        {
            LevelHeader header;
            std::memcpy(&header, guest.data(), sizeof(header));
            header.crc32 = crc32(guest.data() + sizeof(header), static_cast<std::uint32_t>(guest.size() - sizeof(header)));
            std::memcpy(guest.data(), &header, sizeof(header));
        }
        check(hlds::guestTextures(guest) && guest.size() == size, "a guest's level is made, no longer");
        LevelView after;
        check(after.open(guest.data(), static_cast<std::uint32_t>(guest.size())) == LevelError::Ok,
              "and opens, its checksum right");
        if (after.textures == nullptr)
            return;
        const TextureRef &ref = after.textures[surface->texture];
        check(ref.width == host.width / 2 && ref.height == host.height / 4 && ref.guestHalvings == 0 &&
                  (ref.flags & kTextureStreamed) == 0,
              "the texture is its smaller size, and not streamed");
        const std::vector<std::uint32_t> now = coordsOf(after, after.surfaces[index]);
        bool halved = now.size() == before.size();
        for (std::size_t i = 0; halved && i < now.size(); ++i)
        {
            const auto s = static_cast<std::int16_t>(before[i] & 0xFFFF), t = static_cast<std::int16_t>(before[i] >> 16);
            halved = static_cast<std::int16_t>(now[i] & 0xFFFF) == s >> 1 &&
                     static_cast<std::int16_t>(now[i] >> 16) == t >> 2;
        }
        check(halved, "its surface's coordinates go with it (negative ones too)");
    }

    /// What goes with each weapon.
    void testBundles()
    {
        check(bundlesOf("shell") == (1u << kWeaponGlock | 1u << kWeaponMp5), "a casing goes with two guns");
        check(bundlesOf("crossbow_bolt") == 1u << kWeaponCrossbow, "a bolt with the crossbow");
        check(bundlesOf("v_rpg") == 1u << kWeaponRpg && bundlesOf("rpgrocket") == 1u << kWeaponRpg, "a rocket with the RPG");
        check(bundlesOf("v_satchel_radio") == 1u << kWeaponSatchel, "the radio with the satchels");
        check(bundlesOf("hgrunt") == 0, "what is no weapon's, with none");
        check(heldWeaponOf("p_rpg") == kWeaponRpg && heldWeaponOf("p_egon") == kWeaponEgon && heldWeaponOf("v_rpg") == -1,
              "what is held knows whose it is");
        for (const std::uint8_t aim : kAimOf)
            check(aim < sizeof(kAimClips) / sizeof(kAimClips[0]), "every weapon has a pose to hold it in");
        for (const WeaponBundle &bundle : kWeaponBundles)
        {
            int count = 0;
            for (const char *name : bundle.models)
                count += name != nullptr ? 1 : 0;
            check(count <= kLazyResident, "a bundle fits among those in memory at once");
        }
    }

    /// The sizes a guest has: within the budget, the largest gone first.
    void testGuestPlan()
    {
        hlds::BspFile bsp;
        hlds::TexturePlan plan;
        const auto add = [&](const char *name, int width, int height)
        {
            hlds::BspTexture texture;
            texture.name = name;
            bsp.textures.push_back(texture);
            plan.sizes.push_back({static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height)});
        };
        add("wall", 128, 128);   // 16384
        add("small", 32, 32);    // 1024
        add("+0lamp", 64, 64);   // 4096
        add("+1lamp", 64, 64);   // 4096
        add("!water", 128, 128); // 16384, left alone
        add("gone", 0, 0);
        hlds::TexturePlan same = hlds::guestTexturePlan(bsp, plan, 64 * 1024);
        check(same.fits && same.shrunk == 0 && same.texels == 41984, "what fits is left as it is");

        hlds::TexturePlan guest = hlds::guestTexturePlan(bsp, plan, 30000);
        check(guest.fits && guest.texels <= 30000 && guest.shrunk != 0, "too much: made to fit");
        check(guest.sizes[0].width * guest.sizes[0].height < 128 * 128, "the largest first");
        check(guest.sizes[4].width == 128 && guest.sizes[4].height == 128, "liquid as it was");
        check(guest.sizes[5].width == 0, "what is not wanted is not made");

        guest = hlds::guestTexturePlan(bsp, plan, 22000);
        check(guest.sizes[2].width == guest.sizes[3].width && guest.sizes[2].height == guest.sizes[3].height,
              "a cycle's pictures alike");
        check(guest.sizes[2].width < 64 || guest.sizes[2].height < 64, "and made smaller together");

        guest = hlds::guestTexturePlan(bsp, plan, 1000);
        check(!guest.fits, "no room even at the smallest: said");
        for (const hlds::TextureSize &size : guest.sizes)
            check(size.width == 0 || (size.width >= hlds::kMinTextureSide && size.height >= hlds::kMinTextureSide),
                  "never below the smallest side");
    }

    void testBoxRoom()
    {
        const fs::path root = fs::temp_directory_path() / "hlds_test_pack";
        fs::remove_all(root);
        fs::create_directories(root / "valve" / "maps");
        fs::create_directories(root / "card");
        std::string error;
        hlds::writeFileAtomic(root / "valve" / "maps" / "boxroom.bsp", buildBoxRoom(), error);

        hlds::ConvertOptions options;
        options.gameDir = root / "valve";
        options.cardRoot = root / "card";
        options.stageRoot = root / "elsewhere";
        options.textures = false;
        hlds::Converter converter(options);
        check(converter.cookMaps({"boxroom"}, nullptr, nullptr, nullptr, error) == 0, "the box room cooks");
        const fs::path stage = hlds::Converter::dataDir(root / "elsewhere");
        const fs::path card = hlds::Converter::dataDir(root / "card");
        testGuestTextures(stage / "maps" / "boxroom.hlm");
        check(converter.stageDir() == stage && fs::exists(stage / "maps" / "boxroom.hlm"),
              "the stage is where it was said to be");
        check(!fs::exists(card / "tex") && !fs::exists(card / "mdl") && !fs::exists(card / "snd"),
              "no loose files on the card");

        // Things for its archive to hold: its texture, as if cooked, and a
        // file nothing names.
        LevelView view;
        Bytes level;
        hlds::readFile(stage / "maps" / "boxroom.hlm", level, error);
        view.open(level.data(), static_cast<std::uint32_t>(level.size()));
        check(view.textureCount == 1, "the box room has its one texture");
        char name[40];
        textureFileName(view.textures[0].name, view.textures[0].width, view.textures[0].height, false, name);
        hlds::writeFileAtomic(stage / "tex" / name, Bytes(3000, 7), error);
        hlds::writeFileAtomic(stage / "tex" / "unused_16x16.htx", Bytes(64, 1), error);

        int packed = 0;
        check(converter.packAll([&](const hlds::MapResult &r) { packed += r.ok ? 1 : 0; }, error) == 0 &&
                  packed == 1,
              "the card is packed again from the stage alone");
        checkArchive(stage, card, "boxroom", true);
        Bytes archive;
        std::vector<hlds::NpacMember> members;
        hlds::readFile(card / "maps" / "boxroom.npac", archive, error);
        hlds::readNpac(archive, members, error);
        bool texture = false, unused = false;
        for (const hlds::NpacMember &m : members)
        {
            texture = texture || (m.path == std::string("tex/") + name && m.method == hlds::kNpacLz77);
            unused = unused || m.path == "tex/unused_16x16.htx";
        }
        check(texture && !unused, "with the texture the level names, compressed, and not the one it does not");

        options.compress = false;
        hlds::Converter plain(options);
        check(plain.packAll(nullptr, error) == 0, "and packed without compression");
        checkArchive(stage, card, "boxroom", false);

        // A staged level that is damaged is not packed.
        level[level.size() / 2] ^= 0xFF;
        hlds::writeFileAtomic(stage / "maps" / "boxroom.hlm", level, error);
        check(plain.packAll(nullptr, error) != 0, "a damaged level in the stage is refused");
        fs::remove_all(root);
    }

    /// One of the game's own maps, where the game is at hand: models,
    /// sounds, and a level in more than two pieces.
    void testRealMap()
    {
        const char *valve = std::getenv("HLDS_VALVE_DIR");
        if (valve == nullptr || !fs::exists(fs::path(valve) / "maps" / "c1a0.bsp"))
        {
            std::printf("test_pack: HLDS_VALVE_DIR not set, the game's own map was not packed\n");
            return;
        }
        const fs::path root = fs::temp_directory_path() / "hlds_test_pack_real";
        fs::remove_all(root);
        fs::create_directories(root / "card");
        hlds::ConvertOptions options;
        options.gameDir = valve;
        options.cardRoot = root / "card";
        options.textures = false;
        hlds::Converter converter(options);
        std::string error;
        hlds::MapResult result;
        check(converter.cookMaps({"c1a0"}, nullptr, [&](const hlds::MapResult &r) { result = r; }, nullptr,
                                 error) == 0,
              "c1a0 cooks and packs");
        check(result.archiveMembers > 50 && result.archiveKb < result.archiveRawKb * 9 / 10,
              "its archive is smaller than what is in it");
        checkArchive(hlds::Converter::dataDir(root / "card-stage"), hlds::Converter::dataDir(root / "card"),
                     "c1a0", true);
        fs::remove_all(root);
    }
} // namespace

int main()
{
    testPieces();
    testGuestPlan();
    testBundles();
    testBoxRoom();
    testRealMap();
    if (g_failures == 0)
        std::printf("test_pack: ok\n");
    return g_failures == 0 ? 0 : 1;
}
