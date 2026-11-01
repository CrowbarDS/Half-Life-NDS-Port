// The archives the game's data goes on the card in, and the compression
// inside them.
//
// The console does not check any of this: the BIOS inflates what it is given
// and the engine reads the tables as they are. So everything a card could get
// wrong has to be right here -- every stream the converter writes comes back
// as it went in, the container is the one Nitro Engine Advanced reads (its
// own tool is asked, when HLDS_NEA_DIR says where it is), and a damaged
// stream or archive is refused by the converter's reader rather than run
// past the end of anything.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <random>
#include <set>

#include "HlGuest.hpp"
#include "HlLevel.hpp"
#include "HlLevelReader.hpp"
#include "HlLz77Stream.hpp"
#include "HlModel.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/Lz77.hpp"
#include "hlds/NpacWriter.hpp"
#include "hlds/Pack.hpp"

namespace fs = std::filesystem;
using namespace hlds;
using namespace hldstest;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    bool roundTrip(const Bytes &in, std::size_t *packedSize = nullptr)
    {
        const Bytes packed = lz77Compress(in.data(), in.size());
        if (packedSize != nullptr)
            *packedSize = packed.size();
        if (packed.size() < 4 || packed.size() % 4 != 0 || packed[0] != 0x10 ||
            (packed[1] | (packed[2] << 8) | (std::size_t(packed[3]) << 16)) != in.size())
            return false;
        Bytes out;
        std::string error;
        return lz77Decompress(packed.data(), packed.size(), out, error) && out == in;
    }

    Bytes noise(std::size_t size, unsigned seed)
    {
        std::mt19937 random(seed);
        Bytes out(size);
        for (std::uint8_t &b : out)
            b = static_cast<std::uint8_t>(random());
        return out;
    }

    /// Something like the game's data: records that mostly repeat.
    Bytes records(std::size_t size, unsigned seed)
    {
        std::mt19937 random(seed);
        Bytes out;
        while (out.size() < size)
        {
            const std::uint8_t record[12] = {1, 0, 0, 0, static_cast<std::uint8_t>(random() % 7), 0,
                                             0x40, 0, static_cast<std::uint8_t>(random() % 3), 9, 9, 9};
            out.insert(out.end(), record, record + 12);
        }
        out.resize(size);
        return out;
    }

    void testLz77()
    {
        check(roundTrip({}), "nothing at all");
        check(roundTrip({42}), "one byte");
        check(roundTrip({1, 2}), "two bytes");
        check(roundTrip({7, 7, 7}), "three the same");
        std::size_t packed = 0;
        // A run of one byte borrows from one byte back, eighteen at a time.
        check(roundTrip(Bytes(5000, 0xAA), &packed) && packed < 5000 / 7, "a long run of one byte");
        check(roundTrip(noise(10000, 1), &packed) && packed > 10000, "noise (which only grows)");
        check(roundTrip(records(100000, 2), &packed) && packed < 30000, "records shrink well");
        // A repeat from exactly as far back as a run may reach, and one from
        // a byte too far.
        Bytes far = noise(4096, 3);
        Bytes twice = far;
        twice.insert(twice.end(), far.begin(), far.end());
        check(roundTrip(twice, &packed) && packed < 5300, "a repeat from 4096 back is found");
        twice = far;
        twice.push_back(0);
        twice.insert(twice.end(), far.begin(), far.end());
        check(roundTrip(twice, &packed) && packed > 8000, "and from 4097 back is not");
        for (unsigned seed = 10; seed < 40; ++seed)
        {
            Bytes mixed = records(3000 + seed * 37, seed);
            const Bytes more = noise(seed * 11, seed);
            mixed.insert(mixed.begin() + seed * 20, more.begin(), more.end());
            if (!roundTrip(mixed))
            {
                check(false, "mixed data round trip");
                break;
            }
        }
        check(lz77Compress(nullptr, kLz77MaxBytes + 1).empty(), "over 16 MB is not attempted");

        // Damage. None of it may be believed.
        const Bytes in = records(4000, 5);
        const Bytes good = lz77Compress(in.data(), in.size());
        Bytes out;
        std::string error;
        Bytes bad = good;
        bad[0] = 0x11;
        check(!lz77Decompress(bad.data(), bad.size(), out, error), "another method's stream");
        bad = good;
        bad.resize(bad.size() / 2);
        check(!lz77Decompress(bad.data(), bad.size(), out, error), "a stream cut short");
        bad = good;
        bad[2] += 1; // says it is 256 bytes longer than it is
        check(!lz77Decompress(bad.data(), bad.size(), out, error), "a size the data does not reach");
        // A run before anything has been written: flag set on the first item.
        const Bytes early = {0x10, 8, 0, 0, 0x80, 0x50, 0x10, 0};
        check(!lz77Decompress(early.data(), early.size(), out, error), "a run reaching before the start");
        // A run that would write past the stated size.
        const Bytes late = {0x10, 4, 0, 0, 0x40, 1, 0xF0, 0x00};
        check(!lz77Decompress(late.data(), late.size(), out, error), "a run reaching past the end");
        Bytes longer = good;
        longer.insert(longer.end(), 8, 0);
        check(!lz77Decompress(longer.data(), longer.size(), out, error), "bytes after the end");
    }

    std::vector<NpacMember> someMembers()
    {
        std::vector<NpacMember> members;
        auto add = [&](const char *path, Bytes data, bool may = true)
        {
            NpacMember m;
            m.path = path;
            m.data = std::move(data);
            m.mayCompress = may;
            members.push_back(std::move(m));
        };
        add("level.0", records(472, 1));
        add("level.1", records(200000, 2));
        add("level.2", noise(5000, 3));
        add("tex/{fence_64x64a.htx", records(600, 4));   // under a kilobyte: stored
        add("tex/+0button_32x32.htx", records(4000, 5));
        add("tex/~light!_16x16.htx", {});
        add("snd/ambience_drips.11k.hsn", records(9000, 6), false); // told not to
        add("mdl/barney.hmd", records(50001, 7));
        return members;
    }

    void testArchive()
    {
        std::vector<NpacMember> members = someMembers();
        Bytes archive;
        std::string error;
        check(buildNpac(members, true, archive, error), "an archive is built");
        check(archive.size() % 4 == 0 && std::memcmp(archive.data(), "NPAC", 4) == 0, "its header");
        check(members[0].method == kNpacStored && members[1].method == kNpacLz77 &&
                  members[2].method == kNpacStored && members[3].method == kNpacStored &&
                  members[4].method == kNpacLz77 && members[6].method == kNpacStored &&
                  members[7].method == kNpacLz77,
              "compressed where it pays, and only there");
        check(members[1].storedBytes * 100 <= 200000u * kNpacKeepPercent, "and it did pay");

        std::vector<NpacMember> back;
        check(readNpac(archive, back, error), "it reads back");
        bool same = back.size() == members.size();
        for (std::size_t i = 0; same && i < back.size(); ++i)
            same = back[i].path == members[i].path && back[i].data == members[i].data &&
                   back[i].method == members[i].method;
        check(same, "every member, in the order given, as it went in");

        // With compression off there is no compression table at all.
        std::vector<NpacMember> plain = someMembers();
        Bytes stored;
        check(buildNpac(plain, false, stored, error) && stored[14] == 3 && archive[14] == 4,
              "stored only: three chunks, not four");
        check(readNpac(stored, back, error) && back.size() == plain.size() && back[1].data == plain[1].data,
              "and reads back");

        // What cannot be written.
        std::vector<NpacMember> wrong = someMembers();
        wrong[3].path = "tex/a*b.htx";
        check(!buildNpac(wrong, true, stored, error), "a name the table cannot hold");
        wrong = someMembers();
        wrong[4].path = wrong[3].path;
        check(!buildNpac(wrong, true, stored, error), "a name given twice");
        wrong = someMembers();
        std::swap(wrong[2], wrong[5]); // a root file after a folder's
        check(!buildNpac(wrong, true, stored, error), "an order the archive cannot keep");

        // Damage to the tables: refused, never read past.
        for (std::size_t at = 0; at < 200 && at < archive.size(); ++at)
        {
            Bytes bad = archive;
            bad[at] ^= 0xFF;
            std::vector<NpacMember> out;
            std::string why;
            if (readNpac(bad, out, why))
            {
                // Believed: then it must at least hold what it says it holds
                // (a flipped name is still a name).
                if (out.size() != members.size())
                    check(false, "a damaged table changed the member count and was believed");
            }
        }
        Bytes cut = archive;
        cut.resize(cut.size() - 4);
        check(!readNpac(cut, back, error), "a truncated archive");
    }

    /// The engine's own tool, if it is at hand: it must list and unpack what
    /// this writer wrote.
    void testAgainstEngine()
    {
        const char *nea = std::getenv("HLDS_NEA_DIR");
        if (nea == nullptr || !fs::exists(fs::path(nea) / "tools/npac/npac.py"))
        {
            std::printf("test_npac: HLDS_NEA_DIR not set, the engine's tool was not asked\n");
            return;
        }
        const fs::path root = fs::temp_directory_path() / "hlds_test_npac";
        fs::remove_all(root);
        fs::create_directories(root);
        std::vector<NpacMember> members = someMembers();
        Bytes archive;
        std::string error;
        buildNpac(members, true, archive, error);
        writeFileAtomic(root / "a.npac", archive, error);
        const std::string tool = (fs::path(nea) / "tools/npac/npac.py").string();
        const std::string command = "python3 '" + tool + "' extract --input '" + (root / "a.npac").string() +
                                    "' --output '" + (root / "out").string() + "' > /dev/null 2>&1";
        check(std::system(command.c_str()) == 0, "the engine's tool unpacks it");
        bool same = true;
        for (const NpacMember &m : members)
        {
            Bytes got;
            same = same && readFile(root / "out" / m.path, got, error) && got == m.data;
        }
        check(same, "and gets every member as it went in");
        fs::remove_all(root);
    }

    void testChecksum()
    {
        // The table gives what going bit by bit gave.
        const Bytes data = noise(3000, 9);
        std::uint32_t crc = 0xFFFFFFFFu;
        for (const std::uint8_t b : data)
        {
            crc ^= b;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        check(hldata::crc32(data.data(), 3000) == ~crc, "the checksum is what it was");
        const std::uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        check(hldata::crc32(text, 9) == 0xCBF43926u, "and is the standard one");
    }

    /// The decoder a console sent the game undoes it with, as it arrives: in
    /// pieces of any size, straight into where it is to stay, coming back
    /// the same as the PC's own decoder -- and a damaged or mis-sized
    /// stream refused, never run past the end of its buffer.
    void testStreamDecoder()
    {
        std::mt19937 rng(7);
        int good = 0, tried = 0;
        for (int round = 0; round < 60; ++round)
        {
            Bytes in(static_cast<std::size_t>(rng() % 70000));
            // (Half of them with long repeats, as a level's is: the runs
            // that reach back over what they write.)
            for (std::size_t i = 0; i < in.size(); ++i)
                in[i] = round % 2 ? static_cast<std::uint8_t>(rng()) : static_cast<std::uint8_t>((i / (1 + rng() % 9)) & 7);
            const Bytes packed = lz77Compress(in.data(), in.size());
            Bytes out(in.size() + 16, 0xEE);
            hldata::Lz77Stream stream;
            stream.begin(static_cast<std::uint32_t>(in.size()));
            // (In pieces of any size, both ways: what comes, and where what
            // it comes back to goes.)
            std::size_t at = 0, wrote = 0;
            bool ok = true;
            while (ok && !stream.done())
            {
                const std::size_t piece = std::min<std::size_t>(packed.size() - at, 1 + rng() % 700);
                const std::uint8_t *p = packed.data() + at;
                std::size_t left = piece;
                const std::size_t room = std::min<std::size_t>(in.size() - wrote, 1 + rng() % 900);
                wrote += stream.pump(p, left, out.data() + wrote, room);
                at += piece - left;
                ok = !stream.failed() && (at < packed.size() || wrote == in.size() || left == 0);
                if (at >= packed.size() && wrote < in.size() && room == 0)
                    ok = false;
            }
            ++tried;
            if (ok && stream.done() && std::equal(in.begin(), in.end(), out.begin()) && out[in.size()] == 0xEE)
                ++good;
        }
        check(good == tried, "an LZ77 stream fed in pieces of any size comes back whole, into its own buffer");

        const Bytes in(5000, 7);
        Bytes packed = lz77Compress(in.data(), in.size());
        Bytes out(in.size());
        hldata::Lz77Stream stream;
        auto undo = [&](const Bytes &bytes, std::uint32_t expect, std::size_t give)
        {
            stream.begin(expect);
            const std::uint8_t *p = bytes.data();
            std::size_t left = std::min(give, bytes.size());
            stream.pump(p, left, out.data(), out.size());
            return !stream.failed();
        };
        check(!undo(packed, 4999, packed.size()), "a stream of another size than it says is refused");
        // A run from before the start: the first item made a reference.
        const Bytes bad = {0x10, 0x10, 0x00, 0x00, 0x80, 0x00, 0x05};
        check(!undo(bad, 16, bad.size()), "a run reaching before the start is refused");
        check(undo(packed, 5000, packed.size() / 2) && !stream.done(), "half a stream is not a whole one");
    }

    /// What a console with no card is sent of a group of maps (packAir),
    /// from the converter's own stage when HLDS_STAGE_DIR says where it is:
    /// each map's archive and the group's base, together, are all that the
    /// map's card archive has for a DS -- no DSi model or sound, and in a
    /// game between consoles no word of the suit's -- and a model is the
    /// same on every map of the group, so it is sent once, in the base.
    void testAir()
    {
        const char *stage = std::getenv("HLDS_STAGE_DIR");
        if (stage == nullptr || stage[0] == '\0')
        {
            std::printf("test_npac: HLDS_STAGE_DIR not set, the air archives were not made\n");
            return;
        }
        const std::vector<std::string> maps = {"frenzy", "stalkyard", "xen_dm"};
        for (const std::string &map : maps)
        {
            if (!fs::is_regular_file(fs::path(stage) / "maps" / (map + ".hlm")))
            {
                std::printf("test_npac: %s is not staged, the air archives were not made\n", map.c_str());
                return;
            }
        }
        const fs::path card = fs::temp_directory_path() / "hlds_test_air";
        std::error_code ec;
        fs::remove_all(card, ec);
        fs::create_directories(card / "maps");
        AirReport report;
        std::string error;
        check(packAir(stage, card, "match", maps, true, report, error), "the air archives are made");
        if (!error.empty())
            std::printf("test_npac: %s\n", error.c_str());
        // (A guest stream read back with the console's own reader, as it
        // would come: in pieces.)
        auto read = [&](const fs::path &file, std::vector<NpacMember> &members)
        {
            Bytes bytes;
            std::string why;
            if (!readFile(file, bytes, why))
                return false;
            // (In pieces of 600 bytes, as the air brings them.)
            struct Supply : hldata::GuestSupply
            {
                const Bytes *bytes;
                std::size_t at = 0;
                bool more(const std::uint8_t *&data, std::size_t &size) override
                {
                    if (at >= bytes->size())
                        return false;
                    data = bytes->data() + at;
                    size = std::min<std::size_t>(600, bytes->size() - at);
                    at += size;
                    return true;
                }
            } supply;
            supply.bytes = &bytes;
            hldata::GuestCursor cursor(supply);
            if (!cursor.start())
                return false;
            while (cursor.next())
            {
                NpacMember out;
                out.path = cursor.name();
                out.data.resize(cursor.member().rawSize);
                // (Read in odd pieces, as a reader would.)
                std::size_t got = 0;
                while (got < out.data.size())
                {
                    const std::size_t n = cursor.read(out.data.data() + got, std::min<std::size_t>(333, out.data.size() - got));
                    if (n == 0)
                        return false;
                    got += n;
                }
                members.push_back(std::move(out));
            }
            return cursor.ended() && cursor.header().streamBytes == bytes.size();
        };
        std::vector<NpacMember> base;
        check(read(card / "air" / "match.base.hgs", base), "the group's base reads back, a piece at a time");
        int models = 0;
        for (const NpacMember &member : base)
            models += member.path.size() > 4 && member.path.compare(member.path.size() - 4, 4, ".hmd") == 0;
        std::printf("test_npac: the base of %zu maps: %zu files, %d models, %zu KB\n", maps.size(), base.size(),
                    models, report.baseBytes / 1024);
        check(models > 5, "the weapons are in the base");
        for (std::size_t m = 0; m < maps.size(); ++m)
        {
            std::vector<NpacMember> own;
            check(read(card / "air" / (maps[m] + ".hgs"), own), "a map's guest stream reads back, a piece at a time");
            std::set<std::string> sent;
            bool clean = true;
            for (const std::vector<NpacMember> *part : {&base, &own})
            {
                for (const NpacMember &member : *part)
                {
                    clean = clean && member.path.find(".dsi.") == std::string::npos &&
                            member.path.rfind("snd/fvox_", 0) != 0 && sent.insert(member.path).second;
                }
            }
            check(clean, "nothing of a DSi's, no word of the suit's, nothing twice");
            // The guest's level: the host's, index for index, without the
            // large hull.
            {
                Bytes level;
                for (const NpacMember &member : own)
                {
                    if (member.path.rfind("level.", 0) == 0)
                        level.insert(level.end(), member.data.begin(), member.data.end());
                }
                Bytes host;
                std::string why;
                readFile(fs::path(stage) / "maps" / (maps[m] + ".hlm"), host, why);
                hldata::LevelView guest, theirs;
                const bool open = guest.open(level.data(), static_cast<std::uint32_t>(level.size())) ==
                                      hldata::LevelError::Ok &&
                                  theirs.open(host.data(), static_cast<std::uint32_t>(host.size())) == hldata::LevelError::Ok;
                check(open, "the guest's level opens with the console's reader");
                if (open)
                {
                    check(guest.actorCount == theirs.actorCount && guest.logicCount == theirs.logicCount &&
                              guest.brushCount == theirs.brushCount && guest.entityCount == theirs.entityCount &&
                              guest.nameCount == theirs.nameCount && guest.soundCount == theirs.soundCount &&
                              guest.modelRefCount == theirs.modelRefCount && guest.modelCount == theirs.modelCount,
                          "... with every record the host's has, where it has it");
                    check(guest.models[0].clipLarge == hldata::kContentsEmpty && guest.clipNodeCount < theirs.clipNodeCount,
                          "... and without the large hull's nodes");
                    std::printf("test_npac: %s's guest level: %zu KB (the host's %zu)\n", maps[m].c_str(),
                                level.size() / 1024, host.size() / 1024);
                    // Its sounds come in the order the console asks for them.
                    std::uint32_t last = 0;
                    bool ordered = true;
                    for (const NpacMember &member : own)
                    {
                        if (member.path.rfind("snd/", 0) != 0)
                            continue;
                        for (std::uint32_t i = 0; i < guest.soundCount; ++i)
                        {
                            if (member.path == std::string("snd/") + guest.sounds[i].name + ".hsn")
                            {
                                ordered = ordered && i >= last;
                                last = i;
                                break;
                            }
                        }
                    }
                    check(ordered, "... and its sounds come in the level's order");
                }
            }
            // Every model's skin is there with it.
            bool skinned = true;
            for (const std::vector<NpacMember> *part : {&base, &own})
            {
                for (const NpacMember &member : *part)
                {
                    if (member.path.rfind("mdl/", 0) != 0 || member.data.size() < sizeof(hldata::ModelHeader) ||
                        member.path.compare(member.path.size() - 4, 4, ".hmd") != 0)
                        continue;
                    hldata::ModelHeader header;
                    std::memcpy(&header, member.data.data(), sizeof(header));
                    const std::string name = member.path.substr(4, member.path.size() - 8);
                    skinned = skinned && (header.atlasWidth == 0 ||
                                          sent.count("mdl/" + hldata::modelSkinFile(name.c_str(), header.atlasWidth,
                                                                                    header.atlasHeight)) != 0);
                }
            }
            check(skinned, "every model's skin is sent with it");
            // And of what the card's archive has, all a match needs is there:
            // every texture and sprite, every model but the guns' and the
            // player's (which a guest has as its own, lighter) -- the sounds
            // being only those a match plays there.
            PackReport cardReport;
            check(packMap(stage, card, maps[m], false, cardReport, error), "the card archive is made too");
            std::vector<NpacMember> whole;
            {
                Bytes archive;
                std::string why;
                check(readFile(card / "maps" / (maps[m] + ".npac"), archive, why) && readNpac(archive, whole, why) &&
                          !whole.empty(),
                      "the card's archive reads back");
            }
            int lost = 0;
            for (const NpacMember &member : whole)
            {
                const bool needed = member.path.rfind("tex/", 0) == 0 || member.path.rfind("spr/", 0) == 0 ||
                                    member.path.rfind("level.", 0) == 0 ||
                                    (member.path.rfind("mdl/", 0) == 0 && member.path.rfind("mdl/v_", 0) != 0 &&
                                     member.path.rfind("mdl/player", 0) != 0 &&
                                     member.path.find(".dsi.") == std::string::npos &&
                                     member.path.compare(member.path.size() - 4, 4, ".hmd") == 0);
                if (needed && sent.count(member.path) == 0)
                {
                    ++lost;
                    std::printf("test_npac: %s lacks %s\n", maps[m].c_str(), member.path.c_str());
                }
            }
            check(lost == 0, "the guest has every texture, sprite and model a match shows");
            std::printf("test_npac: %s by air: %zu KB of its own, %zu KB with the base\n", maps[m].c_str(),
                        report.maps[m].bytes / 1024, (report.maps[m].bytes + report.baseBytes) / 1024);
        }
        fs::remove_all(card, ec);
    }
} // namespace

int main()
{
    testLz77();
    testArchive();
    testAgainstEngine();
    testChecksum();
    testStreamDecoder();
    testAir();
    if (g_failures == 0)
        std::printf("test_npac: ok\n");
    return g_failures == 0 ? 0 : 1;
}
