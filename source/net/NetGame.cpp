#include "net/NetGame.hpp"
#include "HlNetNext.hpp"
#include "net/AirDevice.hpp"
#include "data/TextureStream.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include <nds.h>

#include "Global.hpp"
#include "Settings.hpp"
#include "data/LevelFile.hpp"
#include "game/Game.hpp"
#include "game/GameApi.hpp"
#include "game/Player.hpp"
#include "render/BottomScreen.hpp"
#include "ui/TouchCanvas.hpp"
#include "render/Camera.hpp"
#include "net/NetLink.hpp"
#include "net/NetRelay.hpp"
#include "HlManifest.hpp"
#include "HlUi.hpp"
#include "audio/Sound.hpp"
#include "data/ModelCache.hpp"
#include "render/EffectDraw.hpp"
#include "render/MapCard.hpp"
#include "HlPreview.hpp"
#include "HlAirBank.hpp"
#include "HlLz77Stream.hpp"
#ifndef HLDS_CHILD
#define HL_NPAC_CODE TWL_CODE __attribute__((noinline))
#endif
#include "HlNpacIndex.hpp"
#include <dirent.h>
#include <sys/stat.h>
#ifdef HLDS_CHILD
#include "net/GuestLoad.hpp"
#endif

namespace hlrt
{
    using namespace hldata;

    NetGame netGame;

    namespace
    {
        /// The pieces of a stream sent a frame while a match waits for it.
        constexpr int kAirServeFrames = 4;
        /// What is asked for (HlAirBank.hpp), the pieces a frame online:
        /// a datagram each, through the relay to every client of the room.
        constexpr int kOnlineFetchPieces = 4;
        /// A client that has said nothing of the host's map for this long
        /// (ten seconds) is not waited for: it has it, or it never will.
        constexpr std::uint32_t kMapQuietVblanks = 600;
        /// (mapHave_: nothing heard yet of this map.)
        constexpr std::uint8_t kMapUnheard = 0xFF;
#ifndef HLDS_CHILD
        /// The host: a block of each map's archive being sent (fetchPiece).
        struct MapReadBlock
        {
            bool valid;
            std::uint32_t start, bytes;
            std::uint8_t data[16384];
        };
        TWL_BSS MapReadBlock mapReadBlocks[hldata::FetchHost::kSending];
        // (What the lobby says of the host's map: the DSi's memory too.)
        TWL_DATA const char kMapText[] = "%s";
        TWL_DATA const char kMapNoRoom[] = "There is not enough room on the card for the host's map.";
        TWL_DATA const char kMapCannot[] = "The host's map could not be had.";
        TWL_DATA const char kMapGetting[] = "Getting the host's map: %d%%. The match begins when it is here.";
        TWL_DATA const char kMapAsking[] = "Asking the host for its map...";
        TWL_DATA const char kMapChecking[] = "Checking the map...";
        TWL_DATA const char kMapLater[] = "This card has not the host's map: it is sent when the match begins.";
#endif
        /// Frames a client may be out of touch with its host before it is
        /// taken for lost: two seconds. (A console loading a map says
        /// nothing for longer than that, but stays the host's client all
        /// the while: it is the radio that is asked, not the frames.)
        constexpr int kLostFrames = 120;

        std::uint16_t tokenOfThisConsole()
        {
            std::uint8_t mac[6];
            netlink::address(mac);
            const std::uint16_t token =
                static_cast<std::uint16_t>((mac[4] << 8 | mac[5]) ^ (mac[2] << 8 | mac[3]) ^ (Global::vblanks() * 40503u));
            return token != 0 ? token : 1;
        }

        /// This console's spray: the profile's logo of the card's (HlUi.hpp:
        /// ui/logos.hic), its rows as they are (kNetSprayBytes). False if
        /// the card has none.
        bool ownSpray(std::uint8_t *out)
        {
            std::FILE *file = std::fopen(global.commonPath(kLogosFile).c_str(), "rb");
            if (file == nullptr)
                return false;
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            const long most = static_cast<long>(sizeof(IconsHeader) +
                                                kLogosMost * (kLogoSize * iconRowWords(kLogoSize) * 4 + kLogoNameBytes));
            auto *words = size > 0 && size <= most ? static_cast<std::uint32_t *>(std::malloc(static_cast<std::size_t>(size)))
                                                   : nullptr;
            const bool got = words != nullptr &&
                             std::fread(words, 1, static_cast<std::size_t>(size), file) == static_cast<std::size_t>(size);
            std::fclose(file);
            LogosView logos;
            const bool ok = got && logos.open(reinterpret_cast<const std::uint8_t *>(words), static_cast<std::size_t>(size));
            static_assert(kLogoSize * iconRowWords(kLogoSize) * 4 == kNetSprayBytes, "a spray is a logo's rows");
            if (ok)
            {
                const int logo = settings.spray % logos.count();
                std::memcpy(out, logos.icons.rows + static_cast<std::size_t>(logo) * kLogoSize * iconRowWords(kLogoSize),
                            kNetSprayBytes);
            }
            std::free(words);
            return ok;
        }

        /// The settings' map, if a match can be played on it; else the first.
        const std::string *hostedMap(const std::vector<std::string> &maps)
        {
            for (const std::string &map : maps)
            {
                if (map == settings.hostMap)
                    return &map;
            }
            return maps.empty() ? nullptr : &maps[0];
        }
    } // namespace

    // (The settings' modes and the match's: the same numbers.)
    static_assert(kModeDeathmatch == kNetModeDeathmatch && kModeTeamplay == kNetModeTeamplay);

    void NetGame::sayProfile()
    {
        NetProfile profile;
        profile.model = settings.playerModel;
        profile.top = settings.topColour;
        profile.bottom = settings.bottomColour;
        profile.asked = match_.myProfile.asked;
        profile.sprayColour = settings.sprayColour;
        std::uint8_t spray[kNetSprayBytes];
        netSetProfile(match_, profile, ownSpray(spray) ? spray : nullptr);
    }

    void NetGame::sayRules()
    {
        NetRules rules;
        // (Co-op is not played yet: a free-for-all.)
        rules.mode = settings.hostMode == kModeTeamplay ? kNetModeTeamplay : kNetModeDeathmatch;
        rules.flags = static_cast<std::uint16_t>(
            (settings.weaponsStay ? kRuleWeaponsStay : 0) | (settings.fallDamage != 0 ? kRuleRealisticFall : 0) |
            (settings.friendlyFire ? kRuleFriendlyFire : 0) | (settings.flashlight ? kRuleFlashlight : 0) |
            (settings.hostAutoaim ? kRuleAutoaim : 0) | (guestsOffered() ? kRuleGuests : 0));
        rules.most = settings.maxPlayers;
        rules.frags = static_cast<std::uint8_t>(settings.fragLimit);
        rules.minutes = static_cast<std::uint8_t>(settings.minuteLimit);
        rules.chatSeconds = settings.chatSeconds;
        rules.nextMode = settings.nextMode;
        rules.totals = settings.sessionTotals != 0;
        rules.voteSeconds = settings.voteSeconds;
        rules.demo = match_.rules.demo;
        rules.demoLongest = match_.rules.demoLongest;
        rules.demoMusic = match_.rules.demoMusic;
        const std::string *hosted = hostedMap(maps_);
        // (The map coming next, chosen when the last match ended, is not the
        // settings': they keep the host's own choice.)
#ifndef HLDS_CHILD
        if (!nextMap_.empty())
            hosted = &nextMap_;
#endif
        rules.mapHash = hosted != nullptr ? nameHash(hosted->c_str()) : 0;
        netSetRules(match_, rules);
        // What the beacon says of it.
        NetRoomInfo room;
        room.mode = rules.mode;
        room.most = match_.rules.most;
        room.guests = guestsOffered();
        if (match_.rules.demo != 0)
        {
            room.mode = static_cast<std::uint8_t>(kNetModeDemoBase + match_.rules.demo - kDemoHazard);
            room.guests = true;
        }
        const std::string *map = hostedMap(maps_);
        // (A demo: nothing of the host's to show, which plays nothing.)
        room.mapHash = map != nullptr && match_.rules.demo == 0 ? nameHash(map->c_str()) : 0;
        std::uint8_t bytes[kNetRoomBytes];
        netRoomWrite(room, bytes);
        netlink::hostRoom(bytes, kNetRoomBytes);
    }

    void NetGame::hostChanged()
    {
        if (phase_ == Phase::Hosting)
            sayRules();
    }

    void NetGame::askTeam(int team)
    {
        if (on())
            netAskTeam(match_, team);
    }

    int NetGame::bars(int p) const
    {
        if (p < 0 || p >= kNetPlayers || !on())
            return -1;
        if (isHost())
            return p == 0 ? 4 : netShareBars(netLinkShare(match_.host, p));
        return p == 0 ? netBars(netlink::strength()) : -1;
    }

    /// (For the menu, which says why hosting could not begin.)
    int netlinkFailStage()
    {
        return netlink::failStage();
    }

    // (The profile's name is a match's: the same room.)
    static_assert(hldata::kProfileNameBytes == static_cast<std::size_t>(kNetTextBytes));

    void NetGame::setOnline(bool on)
    {
        netlink::useRelay(on);
    }

#ifndef HLDS_CHILD
    bool NetGame::online() const
    {
        return netlink::usingRelay();
    }
#endif

    const char *NetGame::onlineUnavailable() const
    {
        return netrelay::unavailable();
    }

    const char *NetGame::onlineWhy() const
    {
        return netrelay::why();
    }

#ifndef HLDS_CHILD
    void NetGame::connectBegin()
    {
        netrelay::connectBegin();
    }

    // (A DSi's alone, in its memory: NetRelay.hpp.)
    TWL_CODE int NetGame::connectStep(const char *&step)
    {
        using netrelay::Connect;
        const Connect phase = netrelay::connectStep();
        step = netrelay::connectText();
        return phase == Connect::Ready ? 1 : (phase == Connect::Failed || phase == Connect::Off ? -1 : 0);
    }

    void NetGame::connectCancel()
    {
        netrelay::connectCancel();
    }

    bool NetGame::onlineReady() const
    {
        return netrelay::ready();
    }

    TWL_CODE bool NetGame::knock(int &ticket, char *name)
    {
        return online() && netrelay::knock(ticket, name);
    }

    TWL_CODE void NetGame::answerKnock(int ticket, bool yes)
    {
        netrelay::answer(ticket, yes);
    }
#endif

    bool NetGame::guestsOffered() const
    {
        return settings.allowGuests && !online();
    }

    bool NetGame::localBlocked() const
    {
        return netrelay::holdsWifi();
    }

    bool NetGame::host()
    {
#ifndef HLDS_CHILD
        turn_ = -1;
#endif
        char name[kNetTextBytes];
        std::snprintf(name, sizeof(name), "%s", settings.playerName());
        // (The radio for as many as a match can have: how many may play is the
        // rules', which can change in the lobby, and the host refuses the
        // rest -- the radio's own count could not change without everyone
        // being let go.)
        // (Starting the radio keeps the ARM7 busy: what it plays again would
        // run dry, and go round and round.)
        sound.holdPipeline(true);
        const bool up = netlink::host(name, kNetPlayers);
        sound.holdPipeline(false);
        if (!up)
            return false;
        netBegin(match_, NetMatch::Role::Host, 0, name);
#ifndef HLDS_CHILD
        airEnd();
        guests_ = 0;
#endif
        sayRules();
        sayProfile();
        profilesSeen_ = 0;
        phase_ = Phase::Hosting;
        clientsWere_ = 0;
        starting_ = goAsked_ = cancelled_ = false;
        for (std::uint32_t &room : rooms_)
            room = 0;
        ended_ = "";
        startPending_ = false;
        return true;
    }

    bool NetGame::scan()
    {
        sound.holdPipeline(true);
        const bool up = netlink::scan();
        sound.holdPipeline(false);
        if (!up)
            return false;
        hldata::renew(match_);
        phase_ = Phase::Scanning;
        ended_ = "";
        startPending_ = false;
        return true;
    }

    int NetGame::found(Found *out, int most)
    {
        int count = 0;
        const int all = phase_ == Phase::Scanning ? netlink::found() : 0;
        for (int i = 0; i < all && count < most; ++i)
        {
            Found &one = out[count];
            int players = 0, room = 0;
            if (!netlink::hostAt(i, one.name, sizeof(one.name), &players, &room))
                continue;
            std::uint8_t bytes[16]; // (the radio's most: DSWIFI_BEACON_GAME_DATA_MAX)
            int size = 0, rssi = 0;
            netlink::hostRoomAt(i, bytes, size, rssi);
            netRoomRead(bytes, size, one.room);
            one.map = nullptr;
            for (const std::string &map : maps_)
            {
                if (one.room.mapHash != 0 && nameHash(map.c_str()) == one.room.mapHash)
                    one.map = map.c_str();
            }
            one.players = players;
            one.most = room;
            one.bars = netBars(rssi);
            std::snprintf(one.line, sizeof(one.line), "%s  %d/%d", one.name[0] ? one.name : "A game", players, room);
            if (!one.name[0])
                std::snprintf(one.name, sizeof(one.name), "A game");
            one.index = i;
            ++count;
        }
        return count;
    }

    const char *NetGame::lobbyMap() const
    {
        if (isHost())
        {
            const std::string *map = hostedMap(maps_);
            return map != nullptr ? map->c_str() : "";
        }
        // (What the host's rules say now; else what its beacon said; else,
        // a map this card has not got, what the host's picture of it said.)
        for (const std::string &map : maps_)
        {
            if (match_.rules.mapHash != 0 && nameHash(map.c_str()) == match_.rules.mapHash)
                return map.c_str();
        }
#ifndef HLDS_CHILD
        if (mapKnown_ && mapHash_ == match_.rules.mapHash && mapName_[0] != 0)
            return mapName_;
#endif
        if (const char *pictured = mapCard.pictureName(match_.rules.mapHash))
            return pictured;
        return lobbyMap_.c_str();
    }

    // ---------------------------------------------- what is asked for (HlAirBank.hpp)

    void NetGame::fetchDrop()
    {
        std::free(takingRoom_);
        takingRoom_ = nullptr;
        takingOn_ = false;
    }

    void NetGame::fetchAsk(FetchFrom from, std::uint32_t what)
    {
        fetchDrop();
        taking_.want(from, what);
        takingOn_ = true;
        takingAskedAt_ = takingSaidAt_ = Global::vblanks();
        takingTries_ = 0;
        netSay(match_, netEvent(NetEventType::Fetch, static_cast<int>(from), static_cast<std::int16_t>(what & 0xFFFF),
                                static_cast<std::int16_t>(what >> 16)));
    }

    void NetGame::fetchSayHave()
    {
        takingSaidAt_ = Global::vblanks();
        const std::uint64_t bits = taking_.have;
        netSay(match_, netEvent(NetEventType::FetchHave, taking_.id, static_cast<std::int16_t>(taking_.base),
                                static_cast<std::int16_t>(bits & 0xFFFF), static_cast<std::int16_t>((bits >> 16) & 0xFFFF),
                                static_cast<std::int16_t>((bits >> 32) & 0xFFFF), static_cast<std::int16_t>(bits >> 48)));
    }

    /// Nothing of it to be had, or it would not come: on with the next.
    void NetGame::fetchGaveUp(bool none)
    {
        const FetchFrom from = taking_.from;
        fetchDrop();
#ifdef HLDS_CHILD
        if (from == FetchFrom::DemoSave)
        {
            guest::demoFileCame(nullptr, 0);
            return;
        }
#else
        // (The host has not got it: nothing to be had. No answer: asked again.)
        if (from == FetchFrom::MapCard || from == FetchFrom::MapIndex)
        {
            // (No answer: its index asked for again; or the rest, from where
            // it had got to -- MapBuild keeps it.)
            if (mapStage_ == MapStage::Getting || mapStage_ == MapStage::Index)
                mapStage_ = none ? MapStage::None : mapStage_ == MapStage::Index ? MapStage::Want : MapStage::Build;
            if (none)
            {
                mapBuild_.stop();
                mapSay(kMapHaveCannot);
            }
            return;
        }
#endif
        if (from == FetchFrom::VotePicture)
        {
            // (None there is a card with its name alone; no answer: asked again.)
            if (none && taking_.what < NetVote::kMost)
                voteNone_ |= 1u << taking_.what;
        }
        else if (from == FetchFrom::Picture || from == FetchFrom::PictureFull)
            pictureNext_ = -1;
        else if ((from == FetchFrom::Map || from == FetchFrom::MapFull) && filePath_[0] != '\0')
        {
            // (Nothing there is never asked for again; no answer: it may be.)
            if (none && fileNoneCount_ < static_cast<int>(sizeof(fileNone_) / sizeof(fileNone_[0])))
                fileNone_[fileNoneCount_++] = nameHash(filePath_);
            filePath_[0] = '\0';
        }
        else if (from == FetchFrom::Map || from == FetchFrom::MapFull)
            textureStream.fetchFailed(none); // (no answer: the host may be busy; asked again)
        else
            personWanted_ = 0;
    }

    void NetGame::fetchMap(std::uint32_t name)
    {
        filePath_[0] = '\0';
        fetchAsk(FetchFrom::MapFull, name);
    }

    bool NetGame::fetchFile(const char *path, bool first)
    {
        if (!global.twlChild || std::strlen(path) >= kFilePath)
            return false;
        const std::uint32_t name = nameHash(path);
        for (int i = 0; i < fileNoneCount_; ++i)
        {
            if (fileNone_[i] == name)
                return false;
        }
        if (std::strcmp(filePath_, path) == 0)
            return true;
        for (int i = 0; i < fileCount_; ++i)
        {
            if (std::strcmp(fileAsks_[i], path) != 0)
                continue;
            if (first && i > 0)
            {
                char was[kFilePath];
                std::memcpy(was, fileAsks_[i], kFilePath);
                std::memmove(fileAsks_[1], fileAsks_[0], static_cast<std::size_t>(i) * kFilePath);
                std::memcpy(fileAsks_[0], was, kFilePath);
            }
            return true;
        }
        if (fileCount_ >= kFileAsks)
        {
            if (!first)
                return false;
            --fileCount_; // (the last goes: it will be asked for again)
        }
        const int at = first ? 0 : fileCount_;
        if (first)
            std::memmove(fileAsks_[1], fileAsks_[0], static_cast<std::size_t>(fileCount_) * kFilePath);
        std::strcpy(fileAsks_[at], path);
        ++fileCount_;
        return true;
    }

    void NetGame::fetchFrame(const FetchHead &head)
    {
        if (!takingOn_ || !taking_.isFor(head))
            return;
        takingAskedAt_ = Global::vblanks();
#ifndef HLDS_CHILD
        if (head.from == FetchFrom::MapCard)
        {
            mapFetchFrame(head);
            return;
        }
#endif
        if (taking_.needsRoom(head))
        {
            std::free(takingRoom_);
            takingRoom_ = static_cast<std::uint8_t *>(std::malloc(head.packed > 0 ? head.packed : 1));
            if (takingRoom_ == nullptr)
            {
                fetchGaveUp();
                return;
            }
            taking_.begin(head);
#ifndef HLDS_CHILD
            // (A console with a card keeps its own: only what it has not.)
            if (head.from == FetchFrom::PictureFull)
            {
                for (const std::string &map : maps_)
                {
                    if (nameHash(map.c_str()) == head.name && mapPictured(map))
                    {
                        taking_.base = static_cast<std::uint32_t>(taking_.pieces);
                        fetchSayHave();
                        fetchDrop();
                        ++pictureNext_;
                        return;
                    }
                }
            }
#endif
        }
        taking_.piece(head, takingRoom_);
        if (!taking_.whole())
            return;
        if (crc32(takingRoom_, taking_.packed) != taking_.crc)
        {
            taking_.base = 0;
            taking_.have = 0;
            netSay(match_, netEvent(NetEventType::FetchAgain, taking_.id));
            return;
        }
        fetchSayHave();
        fetchDeliver();
    }

    void NetGame::fetchDeliver()
    {
        // What it comes to, undone. (It was sent as it is if that is no longer.)
        std::uint8_t *data = takingRoom_;
        takingRoom_ = nullptr;
        const std::uint32_t raw = taking_.raw;
        if (raw != taking_.packed)
        {
            auto *undone = static_cast<std::uint8_t *>(std::malloc(raw > 0 ? raw : 1));
            // (Its history is 4 KB: not on the stack, and only for now.)
            void *room = std::malloc(sizeof(Lz77Stream));
            bool good = undone != nullptr && room != nullptr;
            if (good)
            {
                auto *lz = new (room) Lz77Stream();
                lz->begin(raw);
                const std::uint8_t *in = data;
                std::size_t left = taking_.packed;
                good = lz->pump(in, left, undone, raw) == raw && !lz->failed();
            }
            std::free(room);
            std::free(data);
            if (!good)
            {
                std::free(undone);
                fetchGaveUp();
                return;
            }
            data = undone;
        }
        const FetchFrom from = taking_.from;
        takingOn_ = false;
#ifdef HLDS_CHILD
        if (from == FetchFrom::DemoSave)
        {
            guest::demoFileCame(data, raw);
            return;
        }
#else
        if (from == FetchFrom::MapIndex)
        {
            if (isDSiMode())
                mapIndexCame(data, raw);
            std::free(data);
            return;
        }
#endif
        if (from == FetchFrom::Map || from == FetchFrom::MapFull)
        {
            if (filePath_[0] != '\0')
            {
#ifdef HLDS_CHILD
                if (!air::putMapFile(filePath_, data, raw))
#endif
                {
                    // (No place for it: not asked for again and again.)
                    std::free(data);
                    if (fileNoneCount_ < static_cast<int>(sizeof(fileNone_) / sizeof(fileNone_[0])))
                        fileNone_[fileNoneCount_++] = nameHash(filePath_);
                }
                filePath_[0] = '\0';
            }
            else
                textureStream.fetched(data, raw);
            return;
        }
        if (from == FetchFrom::VotePicture)
        {
            const std::uint32_t slot = taking_.what;
            if (slot < NetVote::kMost && match_.vote.count > 1)
                voteKeep(static_cast<int>(slot), data, raw > kAirNameBytes ? raw - kAirNameBytes : 0);
            else
                std::free(data);
            return;
        }
        if (from == FetchFrom::Picture || from == FetchFrom::PictureFull)
        {
            ++pictureNext_;
            // A picture, then the name of its map (kAirNameBytes).
            const std::size_t bytes = raw > kAirNameBytes ? raw - kAirNameBytes : 0;
            char name[kAirNameBytes] = {};
            if (raw > kAirNameBytes)
                std::memcpy(name, data + bytes, kAirNameBytes - 1);
#ifdef HLDS_CHILD
            mapCard.givePicture(name, data, bytes);
#else
            if (previewWidthOf(data, bytes) == kPreviewWidth && name[0] != 0)
            {
                const std::string folder = global.savesDir() + kPreviewFolder;
                mkdir(folder.c_str(), 0777);
                if (std::FILE *file = std::fopen((folder + name + ".hpv").c_str(), "wb"))
                {
                    std::fwrite(data, 1, bytes, file);
                    std::fclose(file);
                }
                mapCard.hide();
            }
            std::free(data);
#endif
            return;
        }
#ifdef HLDS_CHILD
        // Who a player chose to be: a file now, as a console with a card has it.
        const std::string base = std::string("chr/") + kCharacterFiles[personWanted_];
        const bool model = personStage_ == 0;
        if (!air::putMapFile((base + (model ? ".hmd" : ".htx")).c_str(), data, raw))
            std::free(data);
        if (model)
        {
            personStage_ = 1;
            fetchAsk(taking_.from, nameHash((base + ".htx").c_str()));
            return;
        }
        for (int &have : personsHad_)
        {
            if (have == 0)
            {
                have = personWanted_;
                break;
            }
        }
        personWanted_ = 0;
        personStage_ = 0;
#else
        std::free(data);
#endif
    }

    void NetGame::fetchTick()
    {
        // In its lobby, what it has asked for there; with a map (loaded, and
        // waiting to begin, or in play), that map's.
        // (A demo stays in its lobby, and its map is played from there.)
#ifdef HLDS_CHILD
        const bool played = starting_ || (match_.rules.demo != 0 && !guest::demoLoading());
#else
        const bool played = starting_;
#endif
        const bool lobby = phase_ == Phase::Lobby && !played;
        const bool map = phase_ == Phase::Playing || (phase_ == Phase::Lobby && played);
        // (The vote's pictures are had with or without a map.)
        const bool ofMap = taking_.from == FetchFrom::Map || taking_.from == FetchFrom::MapFull;
        if ((!lobby && !map) || (takingOn_ && taking_.from != FetchFrom::VotePicture && taking_.from != FetchFrom::DemoSave &&
                                 taking_.from != FetchFrom::MapCard && taking_.from != FetchFrom::MapIndex && ofMap != map))
        {
            if (takingOn_ && ofMap && filePath_[0] == '\0')
                textureStream.fetchFailed(false);
            filePath_[0] = '\0';
            fetchDrop();
            if (!lobby && !map)
                return;
        }
        if (map && !takingOn_)
        {
#ifndef HLDS_CHILD
            // The match begun, and this card without the host's map: sent now,
            // before it is loaded (the host waits: serveNext).
            if (mapStage_ == MapStage::Want && phase_ == Phase::Lobby)
            {
                mapAsk();
                return;
            }
#endif
            // (Files first, one at a time; else TextureStream asks: fetchMap.)
            if (fileCount_ > 0)
            {
                std::memcpy(filePath_, fileAsks_[0], kFilePath);
                --fileCount_;
                std::memmove(fileAsks_[0], fileAsks_[1], static_cast<std::size_t>(fileCount_) * kFilePath);
                fetchAsk(FetchFrom::MapFull, nameHash(filePath_));
            }
            return;
        }
        const std::uint32_t now = Global::vblanks();
        if (takingOn_)
        {
            // (A map's archive: also in frames of its own, each frame -- frame().)
            if (taking_.known && now - takingSaidAt_ >= 12)
                fetchSayHave();
#ifndef HLDS_CHILD
            if (taking_.from == FetchFrom::MapCard && now - mapSaidAt_ >= 30)
                mapSay(kMapHaveGetting);
#endif
            // (Nothing for a while: asked again; then not.)
            if (now - takingAskedAt_ > 180)
            {
                if (++takingTries_ > 3)
                {
                    fetchGaveUp();
                    return;
                }
                takingAskedAt_ = now;
                netSay(match_, netEvent(NetEventType::Fetch, static_cast<int>(taking_.from),
                                        static_cast<std::int16_t>(taking_.what & 0xFFFF),
                                        static_cast<std::int16_t>(taking_.what >> 16)));
            }
            return;
        }
        // (The vote's own, in its place: voteFrame.)
        if (match_.vote.count > 1)
            return;
#ifdef HLDS_CHILD
        if (match_.rules.demo != 0)
            return; // (nothing but the map's own)
        const bool full = global.twlChild;
        // Who is looked at, first: it is on the screen.
        if (personWanted_ > 0)
        {
            const std::string file = std::string("chr/") + kCharacterFiles[personWanted_] + ".hmd";
            personStage_ = 0;
            fetchAsk(full ? FetchFrom::PeopleFull : FetchFrom::People, nameHash(file.c_str()));
            return;
        }
        // Then every picture of the host's maps, while there is room.
        if (pictureNext_ >= 0 && global.heapHeadroom() > (full ? 2048u : 512u) * 1024u)
            fetchAsk(full ? FetchFrom::PictureFull : FetchFrom::Picture, static_cast<std::uint32_t>(pictureNext_));
#else
        if (pictureNext_ >= 0)
            fetchAsk(FetchFrom::PictureFull, static_cast<std::uint32_t>(pictureNext_));
#endif
    }

    bool NetGame::personHere(int id) const
    {
#ifdef HLDS_CHILD
        for (const int have : personsHad_)
        {
            if (have == id)
                return true;
        }
#else
        (void)id;
#endif
        return false;
    }

    void NetGame::wantPerson(int id)
    {
#ifdef HLDS_CHILD
        if (personWanted_ == id)
            return;
        // (Room for the last two: then they go. Their memory is little, but it
        // is not the lobby's alone.)
        if (personsHad_[0] != 0 && personsHad_[1] != 0)
            dropPeople();
        personWanted_ = id;
        // What it is doing makes way for who is looked at, if it is a picture.
        if (takingOn_ && (taking_.from == FetchFrom::Picture || taking_.from == FetchFrom::PictureFull))
            fetchDrop();
#else
        (void)id;
#endif
    }

    void NetGame::dropPeople()
    {
#ifdef HLDS_CHILD
        air::dropCharacters();
        personsHad_[0] = personsHad_[1] = 0;
#endif
    }

    void NetGame::forMapLoad()
    {
        fetchDrop();
        fileCount_ = 0;
        fileNoneCount_ = 0;
        filePath_[0] = '\0';
        dropPeople();
        personWanted_ = 0;
        mapCard.dropPicture();
        voteLetGo();
    }

#ifndef HLDS_CHILD
    /// The bytes of what is asked for, from wherever it is: false if there is
    /// no such thing.
    bool NetGame::fetchFind(const FetchHost::Asked &asked, std::uint8_t *&data, std::uint32_t &packed, std::uint32_t &raw,
                            std::uint32_t &name)
    {
        if (asked.from == FetchFrom::Picture || asked.from == FetchFrom::PictureFull || asked.from == FetchFrom::VotePicture)
        {
            // The `what`-th map that has one, or of the vote.
            const bool vote = asked.from == FetchFrom::VotePicture;
            if (vote && asked.what >= match_.vote.count)
                return false;
            std::uint32_t n = 0;
            for (const std::string &map : maps_)
            {
                if (vote ? map != voteMaps_[asked.what] : (!mapPictured(map) || n++ != asked.what))
                    continue;
                std::FILE *file = std::fopen((global.savesDir() + kPreviewFolder + map + ".hpv").c_str(), "rb");
                if (file == nullptr)
                    return false;
                const bool small = asked.from != FetchFrom::PictureFull;
                const std::size_t pictureBytes = small ? kPreviewSmallBytes : kPreviewBytes;
                data = static_cast<std::uint8_t *>(std::malloc(pictureBytes + kAirNameBytes));
                auto *whole = data != nullptr ? static_cast<std::uint8_t *>(small ? std::malloc(kPreviewBytes) : data) : nullptr;
                const bool got = whole != nullptr && std::fread(whole, 1, kPreviewBytes, file) == kPreviewBytes &&
                                 previewValid(whole, kPreviewBytes);
                std::fclose(file);
                if (!got)
                {
                    if (small)
                        std::free(whole);
                    std::free(data);
                    data = nullptr;
                    return false;
                }
                if (small)
                {
                    PreviewHeader header;
                    previewHeader(header);
                    header.width = kPreviewSmallWidth;
                    header.height = kPreviewSmallHeight;
                    std::memcpy(data, &header, sizeof(header));
                    const auto *from = reinterpret_cast<const std::uint16_t *>(whole + sizeof(PreviewHeader));
                    auto *to = reinterpret_cast<std::uint16_t *>(data + sizeof(PreviewHeader));
                    for (int y = 0; y < kPreviewSmallHeight; ++y)
                    {
                        for (int x = 0; x < kPreviewSmallWidth; ++x)
                        {
                            const std::uint32_t a = from[y * 2 * kPreviewWidth + x * 2], b = from[y * 2 * kPreviewWidth + x * 2 + 1],
                                                c = from[(y * 2 + 1) * kPreviewWidth + x * 2],
                                                d = from[(y * 2 + 1) * kPreviewWidth + x * 2 + 1];
                            to[y * kPreviewSmallWidth + x] = static_cast<std::uint16_t>(
                                0x8000 | ((((a & 31) + (b & 31) + (c & 31) + (d & 31)) / 4)) |
                                ((((a >> 5 & 31) + (b >> 5 & 31) + (c >> 5 & 31) + (d >> 5 & 31)) / 4) << 5) |
                                ((((a >> 10 & 31) + (b >> 10 & 31) + (c >> 10 & 31) + (d >> 10 & 31)) / 4) << 10));
                        }
                    }
                    std::free(whole);
                }
                std::memset(data + pictureBytes, 0, kAirNameBytes);
                std::memcpy(data + pictureBytes, map.c_str(), map.size() < kAirNameBytes - 1 ? map.size() : kAirNameBytes - 1);
                packed = raw = static_cast<std::uint32_t>(pictureBytes + kAirNameBytes);
                name = nameHash(map.c_str());
                return true;
            }
            return false;
        }
        if (asked.from == FetchFrom::DemoSave)
        {
            std::FILE *file = std::fopen(demoFilePath(fetchWho_, asked.what).c_str(), "rb");
            if (file == nullptr)
                return false;
            std::fseek(file, 0, SEEK_END);
            const long length = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            data = length > 0 ? static_cast<std::uint8_t *>(std::malloc(static_cast<std::size_t>(length))) : nullptr;
            const bool got = data != nullptr && std::fread(data, 1, static_cast<std::size_t>(length), file) ==
                                                    static_cast<std::size_t>(length);
            std::fclose(file);
            if (!got)
            {
                std::free(data);
                data = nullptr;
                return false;
            }
            packed = raw = static_cast<std::uint32_t>(length);
            name = asked.what;
            return true;
        }
        // A member of a bank. (A demo's: of the map that console is playing.)
        const char *demoBank = match_.rules.demo != 0 && demoMapOf_[fetchWho_] != 0
                                   ? netDemoMaps(match_.rules.demo)[demoMapOf_[fetchWho_] - 1]
                                   : nullptr;
        const std::string bank = asked.from == FetchFrom::People       ? std::string("air/chr.bank")
                                 : asked.from == FetchFrom::PeopleFull ? std::string("air/chr.dsi.bank")
                                 : asked.from == FetchFrom::MapFull
                                     ? "air/" + (demoBank != nullptr ? std::string(demoBank) : pendingMap_) + ".dsi.bank"
                                                                       : std::string();
        if (bank.empty())
            return false;
        std::FILE *file = std::fopen(global.dataPath(bank).c_str(), "rb");
        if (file == nullptr)
            return false;
        AirBankHeader header;
        bool found = false;
        AirBankEntry entry{};
        if (std::fread(&header, sizeof(header), 1, file) == 1 && std::memcmp(header.magic, kAirBankMagic, 4) == 0 &&
            header.version == kAirBankVersion)
        {
            for (int i = 0; i < header.count && !found; ++i)
            {
                if (std::fread(&entry, sizeof(entry), 1, file) != 1)
                    break;
                found = entry.name == asked.what;
            }
        }
        data = found ? static_cast<std::uint8_t *>(std::malloc(entry.packed > 0 ? entry.packed : 1)) : nullptr;
        found = data != nullptr && std::fseek(file, static_cast<long>(entry.offset), SEEK_SET) == 0 &&
                std::fread(data, 1, entry.packed, file) == entry.packed;
        std::fclose(file);
        if (!found)
        {
            std::free(data);
            data = nullptr;
            return false;
        }
        packed = entry.packed;
        raw = entry.raw;
        name = entry.name;
        return true;
    }

    int NetGame::fetchHostFrame(int most)
    {
        fetch_.frame();
        // Let go of what nobody wants any more.
        for (int k = 0; k < FetchHost::kSending; ++k)
        {
            if (!fetch_.sending[k].on && fetchData_[k] != nullptr)
            {
                std::free(fetchData_[k]);
                fetchData_[k] = nullptr;
            }
            if (!fetch_.sending[k].on && fetchFile_[k] != nullptr)
            {
                std::fclose(fetchFile_[k]);
                fetchFile_[k] = nullptr;
            }
        }
        // Not while a stream is sent. (A demo's, that someone is waiting for;
        // the guests' while a match waits -- the map's archive comes after.)
        if ((phase_ != Phase::Hosting && phase_ != Phase::Playing && phase_ != Phase::Serving) || demoLiveNow_ ||
            (phase_ == Phase::Serving && air_.sending()))
            return 0;
        int who = 0;
        FetchHost::Asked asked;
        while (fetch_.nextAsked(who, asked))
        {
            std::uint8_t *data = nullptr;
            std::uint32_t packed = 0, raw = 0, name = 0;
            fetchWho_ = who;
            // (The map's archive: from its file, as it is wanted -- a DSi's.)
            const bool mapCard = asked.from == FetchFrom::MapCard;
            const bool found = mapCard ? isDSiMode() && mapServe(who, asked)
                               : asked.from == FetchFrom::MapIndex ? isDSiMode() && mapIndexFind(asked, data, packed, raw, name)
                                                                   : fetchFind(asked, data, packed, raw, name);
            if (!found)
            {
                netSay(match_, netEvent(NetEventType::FetchNone, static_cast<int>(asked.from),
                                        static_cast<std::int16_t>(asked.what & 0xFFFF),
                                        static_cast<std::int16_t>(asked.what >> 16)));
                continue;
            }
            if (mapCard)
                continue;
            const std::uint32_t crc = crc32(data, packed);
            fetch_.begin(who, asked, name, packed, raw, crc, 0);
            for (int k = 0; k < FetchHost::kSending; ++k)
            {
                if (fetch_.sending[k].on && fetch_.sending[k].head.what == asked.what &&
                    fetch_.sending[k].head.from == asked.from && fetchData_[k] == nullptr)
                {
                    fetchData_[k] = data;
                    data = nullptr;
                }
            }
            std::free(data);
        }
        // As the air has room for, after the game's own frame.
        const unsigned present = netlink::clients();
        int clients = 0;
        for (unsigned bits = present; bits != 0; bits &= bits - 1)
            ++clients;
        // (Online, nothing shares the air: as many as the link is found to
        // take -- each is a datagram of its own.)
        // (Serving -- a match waiting for a map -- as fast as the guests' streams.)
        const int pieces = online() ? kOnlineFetchPieces : most > 0 ? most : clients <= 1 ? 3 : (clients <= 3 ? 2 : 1);
        int sent = 0;
        for (int n = 0; n < pieces; ++n)
        {
            FetchHost::Sending *s = nullptr;
            std::uint32_t index = 0;
            if (!fetch_.next(s, index))
                break;
            const int k = static_cast<int>(s - fetch_.sending);
            const std::uint32_t at = index * kFetchPiece;
            FetchHead head = s->head;
            head.index = static_cast<std::uint16_t>(index);
            head.count = s->head.packed - at < kFetchPiece ? static_cast<int>(s->head.packed - at) : kFetchPiece;
            static TWL_BSS std::uint8_t piece[kFetchPiece];
            const std::uint8_t *data = fetchData_[k] != nullptr ? fetchData_[k] + at
                                       : fetchFile_[k] != nullptr && fetchPiece(k, at, piece, head.count) ? piece
                                                                              : nullptr;
            if (data == nullptr)
                break;
            std::uint8_t bytes[kNetCmdBytes];
            const int size = fetchWrite(head, data, bytes);
            if (size > 0)
            {
                netlink::send(bytes, size);
                ++sent_;
                ++sent;
            }
        }
        return sent;
    }

    void NetGame::fetchHostStop()
    {
        fetch_.stop();
        for (std::uint8_t *&data : fetchData_)
        {
            std::free(data);
            data = nullptr;
        }
        for (std::FILE *&file : fetchFile_)
        {
            if (file != nullptr)
                std::fclose(file);
            file = nullptr;
        }
    }

    /// `count` bytes from `at` of what a map's archive sends as: its head,
    /// then its file.
    HL_TWL_CODE bool NetGame::fetchPiece(int k, std::uint32_t at, std::uint8_t *to, int count)
    {
        std::FILE *file = fetchFile_[k];
        if (file == nullptr)
            return false;
        int done = 0;
        for (; done < count && at + static_cast<std::uint32_t>(done) < kMapCardHeadBytes; ++done)
            to[done] = static_cast<std::uint8_t>(fetchName_[k][at + static_cast<std::uint32_t>(done)]);
        if (done == count)
            return true;
        // From the file through a block of it kept here: a read of the card
        // costs far more than its length, and the pieces go mostly in order.
        // (A piece across the block's end: the next block, and the rest.)
        std::uint32_t from = at + static_cast<std::uint32_t>(done) - kMapCardHeadBytes;
        while (done < count)
        {
            MapReadBlock &block = mapReadBlocks[k];
            if (!block.valid || from < block.start || from >= block.start + block.bytes)
            {
                block.valid = false;
                block.start = from - from % sizeof(block.data);
                if (std::fseek(file, static_cast<long>(block.start), SEEK_SET) != 0)
                    return false;
                block.bytes = static_cast<std::uint32_t>(std::fread(block.data, 1, sizeof(block.data), file));
                if (block.bytes == 0 || from >= block.start + block.bytes)
                    return false;
                block.valid = true;
            }
            const std::uint32_t inBlock = from - block.start;
            const std::uint32_t take = block.bytes - inBlock < static_cast<std::uint32_t>(count - done)
                                           ? block.bytes - inBlock
                                           : static_cast<std::uint32_t>(count - done);
            std::memcpy(to + done, block.data + inBlock, take);
            done += static_cast<int>(take);
            from += take;
        }
        return true;
    }

    // ------------------------------------------- the host's map, sent (MapShare.hpp)

    /// The host's map's archive, asked for: begun, from its file. False: not
    /// this lobby's map, or not on the card.
    HL_TWL_CODE bool NetGame::mapServe(int who, const FetchHost::Asked &asked)
    {
        const char *name = nullptr;
        for (const std::string &map : maps_)
        {
            if (nameHash(map.c_str()) == asked.what)
                name = map.c_str();
        }
        if ((phase_ != Phase::Serving && phase_ != Phase::Hosting) || !mapKnown_ || asked.what != mapHash_ || name == nullptr)
            return false;
        char path[160];
        static TWL_DATA const char kArchive[] = "%smaps/%s.npac";
        static TWL_DATA const char kRead[] = "rb";
        std::snprintf(path, sizeof(path), kArchive, global.dataDir().c_str(), name);
        std::FILE *file = std::fopen(path, kRead);
        if (file == nullptr)
            return false;
        std::setvbuf(file, nullptr, _IONBF, 0);
        fetch_.begin(who, asked, asked.what, mapBytes_, mapBytes_, mapCrc_, 0);
        for (int k = 0; k < FetchHost::kSending && file != nullptr; ++k)
        {
            if (fetch_.sending[k].on && fetch_.sending[k].head.what == asked.what && fetch_.sending[k].head.from == asked.from &&
                fetchData_[k] == nullptr && fetchFile_[k] == nullptr)
            {
                fetchFile_[k] = file;
                file = nullptr;
                mapReadBlocks[k].valid = false;
                mapCardHead(name, reinterpret_cast<std::uint8_t *>(fetchName_[k]));
            }
        }
        if (file != nullptr)
            std::fclose(file);
        return true;
    }

    /// The host's map's index, asked for: its name, then the archive up to
    /// its members' data.
    HL_TWL_CODE bool NetGame::mapIndexFind(const FetchHost::Asked &asked, std::uint8_t *&data, std::uint32_t &packed,
                                           std::uint32_t &raw, std::uint32_t &name)
    {
        const char *map = nullptr;
        for (const std::string &one : maps_)
        {
            if (nameHash(one.c_str()) == asked.what)
                map = one.c_str();
        }
        if ((phase_ != Phase::Serving && phase_ != Phase::Hosting) || !mapKnown_ || asked.what != mapHash_ || map == nullptr ||
            !mapshare::indexFor(mapHash_))
            return false;
        constexpr std::uint32_t kMost = 64 * 1024;
        data = static_cast<std::uint8_t *>(std::malloc(kMost));
        const std::uint32_t bytes = data != nullptr ? mapshare::indexBlob(data, kMost, map) : 0;
        if (bytes == 0)
        {
            std::free(data);
            data = nullptr;
            return false;
        }
        packed = raw = bytes;
        name = asked.what;
        return true;
    }

    /// A client's word of a map's archive coming (kNetFrameFetchHave).
    HL_TWL_CODE void NetGame::mapHaveHeard(int from, const std::uint8_t *bytes, int size)
    {
        std::uint8_t id = 0;
        std::uint32_t base = 0;
        std::uint64_t bits = 0;
        if (fetchHaveRead(bytes, size, id, base, bits))
            fetch_.have(from, id, base, bits);
    }

    /// ... and a client's, in place of the game's frame (0: none yet).
    HL_TWL_CODE int NetGame::mapHaveReply(std::uint8_t *reply)
    {
        return taking_.known ? fetchHaveWrite(taking_.id, taking_.base, taking_.have, reply) : 0;
    }

    HL_TWL_CODE void NetGame::mapHeard(const NetEvent &event)
    {
        if (event.type == NetEventType::MapHave && isHost() && event.from > 0 && event.from < kNetPlayers)
        {
            // (Of the map the lobby is for, not one before it.)
            if ((static_cast<std::uint16_t>(event.v[1]) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[2])) << 16) ==
                mapHash_)
            {
                mapHave_[event.from] = event.a;
                mapPermille_[event.from] = static_cast<std::uint16_t>(event.v[0]);
                mapHeardAt_[event.from] = Global::vblanks();
            }
            return;
        }
        if (event.type == NetEventType::MapSum && !isHost())
        {
            // (Heard whenever; seen to in the lobby: mapClientFrame.)
            for (int k = 0; k < 3; ++k)
                mapOffer_[k] = static_cast<std::uint16_t>(event.v[2 * k]) |
                               static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[2 * k + 1])) << 16;
            mapOfferNew_ = true;
        }
    }

    /// A match begins on the map of `hash`: as the host said it was, the
    /// card's own or one it sent (played from where it was put) -- in
    /// pendingMap_. 0: the host said nothing of it; 1: there; 2: not had;
    /// 3: not had, for want of room.
    HL_TWL_CODE int NetGame::mapForStart(std::uint32_t hash)
    {
        if (!mapKnown_ || mapHash_ != hash)
            return 0;
        // (Still finding out, or still to be sent: when it has it.)
        if (mapStage_ != MapStage::None)
            return 4;
        if (mapSays_ != kMapHaveYes || mapName_[0] == 0)
            return mapSays_ == kMapHaveNoRoom ? 3 : 2;
        pendingMap_ = mapName_;
        if (mapSent_)
        {
            char path[160];
            mapshare::receivedPath(path, sizeof(path), mapName_, mapCrc_);
            global.setMapFile(mapName_, path);
        }
        return 1;
    }

    HL_TWL_CODE void NetGame::mapHostFrame()
    {
        const std::uint32_t now = Global::vblanks();
        // The lobby's map: what it comes to, found once, then said.
        const std::uint32_t hash = match_.rules.demo == 0 ? match_.rules.mapHash : 0;
        if (hash != mapHash_)
        {
            mapSum_.stop();
            mapHash_ = hash;
            mapKnown_ = false;
            mapSaidTo_ = 0;
            for (int c = 0; c < kNetPlayers; ++c)
            {
                mapHave_[c] = kMapUnheard;
                mapPermille_[c] = 0;
                mapHeardAt_[c] = now;
            }
            for (const std::string &map : maps_)
            {
                if (hash == 0 || nameHash(map.c_str()) != hash)
                    continue;
                const std::string path = global.dataDir() + "maps/" + map + ".npac";
                const std::uint32_t bytes = mapshare::fileBytes(path.c_str());
                std::uint32_t crc = 0;
                // (Known already, with what each member comes to -- what its
                // index is sent with: else read again.)
                if (bytes != 0 && mapshare::knownSum(hash, bytes, crc) && mapshare::indexFor(hash))
                {
                    mapCrc_ = crc;
                    mapBytes_ = bytes + kMapCardHeadBytes;
                    mapKnown_ = true;
                }
                else if (bytes != 0)
                    mapSum_.begin(path.c_str(), map.c_str(), hash);
            }
        }
        if (mapSum_.busy() && mapSum_.step() && mapSum_.good)
        {
            mapCrc_ = mapSum_.crc;
            mapBytes_ = mapSum_.bytes;
            mapKnown_ = true;
            mapshare::rememberSum(mapHash_, mapBytes_ - kMapCardHeadBytes, mapCrc_);
        }
        if (!mapKnown_)
            return;
        // Said to whoever has not heard it -- of those in the match's own
        // account (clientsWere_: netHostJoin'd), not the link's: online, the
        // relay's word of a newcomer can come in the middle of a frame, and
        // an event said before its stream begins is never seen by it. And
        // again every two seconds, whatever: one lost costs a transfer, a
        // repeat nothing (mapOffered knows it). (Those sent the game have no
        // card: it is nothing to them.)
        const unsigned here = clientsWere_;
        mapSaidTo_ &= here;
        if ((here & ~mapSaidTo_) != 0 || (here != 0 && now - mapSumAt_ >= 120))
        {
            mapSumAt_ = now;
            netSay(match_, netEvent(NetEventType::MapSum, 0, static_cast<std::int16_t>(mapHash_ & 0xFFFF),
                                    static_cast<std::int16_t>(mapHash_ >> 16), static_cast<std::int16_t>(mapCrc_ & 0xFFFF),
                                    static_cast<std::int16_t>(mapCrc_ >> 16), static_cast<std::int16_t>(mapBytes_ & 0xFFFF),
                                    static_cast<std::int16_t>(mapBytes_ >> 16)));
            mapSaidTo_ = here;
        }
    }

    HL_TWL_CODE void NetGame::mapSay(std::uint8_t state)
    {
        mapSays_ = state;
        mapSaidAt_ = Global::vblanks();
        // (How far: of what comes from the host.)
        const int permille = state == kMapHaveGetting && mapStage_ == MapStage::Getting ? mapBuild_.percent() * 10 : 0;
        netSay(match_, netEvent(NetEventType::MapHave, state, static_cast<std::int16_t>(permille),
                                static_cast<std::int16_t>(mapHash_ & 0xFFFF), static_cast<std::int16_t>(mapHash_ >> 16)));
    }

    HL_TWL_CODE void NetGame::mapForget()
    {
        mapSum_.stop();
        if (takingOn_ && (taking_.from == FetchFrom::MapCard || taking_.from == FetchFrom::MapIndex))
            fetchDrop();
        mapBuild_.stop();
        mapWhole_ = false;
        mapStage_ = MapStage::None;
        mapHash_ = mapCrc_ = mapBytes_ = 0;
        mapKnown_ = false;
        mapSaidTo_ = 0;
        mapSays_ = kMapHaveYes;
        mapName_[0] = 0;
        mapSent_ = false;
        mapTries_ = 0;
        mapStartWait_ = false;
    }

    HL_TWL_CODE void NetGame::mapOffered(std::uint32_t hash, std::uint32_t crc, std::uint32_t bytes)
    {
        // (Said again for someone who came since: as it was.)
        if (mapKnown_ && hash == mapHash_ && crc == mapCrc_ && bytes == mapBytes_)
            return;
        mapForget();
        mapHash_ = hash;
        mapCrc_ = crc;
        mapBytes_ = bytes;
        mapKnown_ = true;
        // Sent before: that one.
        if (mapshare::received(hash, crc, mapName_))
        {
            mapSent_ = true;
            mapSay(kMapHaveYes);
            return;
        }
        mapName_[0] = 0;
        // The card's own, if it is the host's: what it comes to, to be sure.
        for (const std::string &map : maps_)
        {
            if (nameHash(map.c_str()) != hash || map.size() >= sizeof(mapName_))
                continue;
            const std::string path = global.dataDir() + "maps/" + map + ".npac";
            const std::uint32_t own = mapshare::fileBytes(path.c_str());
            if (own == 0 || own + kMapCardHeadBytes != bytes)
                break; // (another length: another map)
            std::snprintf(mapName_, sizeof(mapName_), "%s", map.c_str());
            std::uint32_t known = 0;
            if (mapshare::knownSum(hash, own, known))
            {
                if (known == crc)
                {
                    mapSay(kMapHaveYes);
                    return;
                }
                break;
            }
            if (mapSum_.begin(path.c_str(), map.c_str()))
            {
                mapStage_ = MapStage::Own;
                mapSay(kMapHaveChecking);
                return;
            }
            break;
        }
        mapName_[0] = 0;
        // Else, sent when the match begins.
        mapNeed();
    }

    HL_TWL_CODE void NetGame::mapClientFrame()
    {
        if (mapOfferNew_ && !starting_)
        {
            mapOfferNew_ = false;
            mapOffered(mapOffer_[0], mapOffer_[1], mapOffer_[2]);
        }
        // Its own, found to be the host's or not.
        if (mapStage_ == MapStage::Own && mapSum_.step())
        {
            const bool same = mapSum_.good && mapSum_.crc == mapCrc_ && mapSum_.bytes == mapBytes_;
            if (mapSum_.good)
                mapshare::rememberSum(mapHash_, mapSum_.bytes - kMapCardHeadBytes, mapSum_.crc);
            mapStage_ = MapStage::None;
            if (same)
                mapSay(kMapHaveYes);
            else
            {
                // (Another version of it: the host's is sent when the match
                // begins, and kept apart.)
                mapName_[0] = 0;
                mapNeed();
            }
        }
        // What this card has of it, found and copied; then the rest asked for.
        if (mapStage_ == MapStage::Build)
        {
            const MapBuild::Stage build = mapBuild_.step();
            if (build == MapBuild::Stage::Failed)
            {
                mapBuild_.stop();
                mapStage_ = MapStage::None;
                mapSay(kMapHaveNoRoom);
            }
            else if (build == MapBuild::Stage::Fetching && !takingOn_)
            {
                if (mapBuild_.complete())
                    mapCheck();
                else
                {
                    fetchAsk(FetchFrom::MapCard, mapHash_);
                    mapStage_ = MapStage::Getting;
                    mapGotAt_ = Global::vblanks();
                }
            }
        }
        // All of it here, and what it comes to found: kept -- or, wrong, all
        // of it from the host (what was copied may have been another version).
        if (mapStage_ == MapStage::Check && mapSum_.step())
        {
            const bool same = mapSum_.good && mapSum_.crc == mapCrc_ && mapSum_.bytes == mapBytes_;
            mapStage_ = MapStage::None;
            if (same && mapshare::keep(mapName_, mapCrc_))
            {
                mapSent_ = true;
                mapSay(kMapHaveYes);
            }
            else
            {
                mapBuild_.stop();
                mapName_[0] = 0;
                if (!mapWhole_)
                {
                    mapWhole_ = true;
                    mapNeed();
                }
                else if (++mapTries_ < 3)
                    mapNeed();
                else
                    mapSay(kMapHaveCannot);
            }
        }
        // The match begun, waiting for this: on with it, or out.
        if (mapStartWait_ && starting_ && mapStage_ == MapStage::None)
        {
            mapStartWait_ = false;
            const int said = mapForStart(mapHash_);
            if (said == 1)
                startPending_ = true;
            else if (said == 2 || said == 3)
            {
                match_.passedCount = 0;
                leave();
                ended_ = said == 3 ? kMapNoRoom : kMapCannot;
                return;
            }
        }
        // (What it says, again now and then -- every second until it has it,
        // every two once it has: the host may have missed it in a change of
        // map, or (online) before this console was in its account.)
        if (mapKnown_ && mapStage_ != MapStage::Getting &&
            Global::vblanks() - mapSaidAt_ >= (mapSays_ == kMapHaveYes ? 120u : 60u))
            mapSay(mapSays_);
    }

    /// The host's map is not on this card as the host has it: sent when the
    /// match begins, if there is room for it.
    HL_TWL_CODE void NetGame::mapNeed()
    {
        if (!mapshare::room(mapBytes_))
        {
            mapStage_ = MapStage::None;
            mapSay(kMapHaveNoRoom);
            return;
        }
        mapStage_ = MapStage::Want;
        mapSay(kMapHaveGetting);
    }

    /// The host, a match begun: how many consoles with cards are still being
    /// sent its map (or not yet heard from of it), and how far the slowest is.
    HL_TWL_CODE bool NetGame::mapWaiting(int &consoles, int &percent) const
    {
        consoles = 0;
        percent = 100;
        if (match_.rules.demo != 0 || mapHash_ == 0)
            return false;
        const unsigned here = netlink::clients() & ~guests_;
        const std::uint32_t now = Global::vblanks();
        for (int c = 1; c < kNetPlayers; ++c)
        {
            if (!((here >> c) & 1) || now - mapHeardAt_[c] >= kMapQuietVblanks)
                continue;
            const std::uint8_t has = mapHave_[c];
            if (!mapKnown_ || has == kMapHaveGetting || has == kMapHaveChecking || has == kMapUnheard)
            {
                ++consoles;
                const int at = has == kMapHaveGetting ? mapPermille_[c] / 10 : 0;
                percent = at < percent ? at : percent;
            }
        }
        return consoles > 0;
    }

    HL_TWL_CODE void NetGame::mapFetchFrame(const FetchHead &head)
    {
        if (mapStage_ != MapStage::Getting)
            return;
        if (taking_.needsRoom(head))
        {
            // (Of another map than the one said: not this.)
            if (head.packed != mapBytes_ || head.crc != mapCrc_)
                return;
            taking_.begin(head);
        }
        if (head.id != taking_.id)
            return;
        // Where it goes; then what the host is told it has (what was copied,
        // and what has come).
        if (!mapBuild_.piece(head.index, head.data, head.count))
        {
            fetchDrop();
            mapBuild_.stop();
            mapStage_ = MapStage::None;
            mapSay(kMapHaveNoRoom);
            return;
        }
        mapBuild_.have(taking_.base, taking_.have);
        if (!mapBuild_.complete())
            return;
        fetchSayHave();
        takingOn_ = false;
        mapCheck();
    }

    /// The match begun: its index asked for (what of it this card has is
    /// copied, the rest sent) -- or, what was copied having been found
    /// wrong, all of it.
    HL_TWL_CODE void NetGame::mapAsk()
    {
        if (!mapWhole_)
        {
            fetchAsk(FetchFrom::MapIndex, mapHash_);
            mapStage_ = MapStage::Index;
            return;
        }
        if (!mapBuild_.beginWhole(mapBytes_))
        {
            mapStage_ = MapStage::None;
            mapSay(kMapHaveCannot);
            return;
        }
        mapStage_ = MapStage::Build;
    }

    HL_TWL_CODE void NetGame::mapIndexCame(std::uint8_t *data, std::uint32_t bytes)
    {
        if (mapStage_ != MapStage::Index)
            return;
        char name[kMapCardHeadBytes];
        if (!mapCardName(data, name) || nameHash(name) != mapHash_ || !mapBuild_.begin(data, bytes, mapBytes_))
        {
            // (Not an archive to be read: all of it sent, as it is.)
            mapWhole_ = true;
            mapAsk();
            return;
        }
        mapStage_ = MapStage::Build;
    }

    /// All of it here: closed, and what it comes to found.
    HL_TWL_CODE void NetGame::mapCheck()
    {
        char part[160];
        mapshare::partPath(part, sizeof(part));
        if (!mapBuild_.close() || !mapBuild_.name(mapName_) || nameHash(mapName_) != mapHash_ || !mapSum_.begin(part, mapName_))
        {
            mapBuild_.stop();
            mapName_[0] = 0;
            mapStage_ = MapStage::None;
            mapSay(kMapHaveCannot);
            return;
        }
        mapStage_ = MapStage::Check;
        mapSay(kMapHaveChecking);
    }

    HL_TWL_CODE bool NetGame::mapReceivingTwl(int &percent, char *step, char *note) const
    {
        if (!mapStartWait_ || !starting_ || mapStage_ == MapStage::None)
            return false;
        static TWL_DATA const char kAsking[] = "Asking the host for its map", kOwn[] = "Checking this card's map",
                                   kFinding[] = "Looking for %s on this card", kCopying[] = "Copying %s from this card",
                                   kReceiving[] = "Receiving %s", kChecking[] = "Checking %s", kFrom[] = "%lu KB from the host",
                                   kFromRate[] = "%lu KB from the host, %d KB a second", kHosts[] = "the host's map";
        char name[kMapCardHeadBytes];
        const char *named = mapBuild_.name(name) ? name : kHosts;
        percent = 0;
        note[0] = 0;
        const MapBuild::Stage build = mapBuild_.stage();
        if (mapStage_ == MapStage::Own)
            std::snprintf(step, 64, kOwn);
        else if (mapStage_ == MapStage::Want || mapStage_ == MapStage::Index)
            std::snprintf(step, 64, kAsking);
        else if (mapStage_ == MapStage::Check)
        {
            std::snprintf(step, 64, kChecking, named);
            percent = mapBytes_ == 0 ? 0 : static_cast<int>(static_cast<std::uint64_t>(mapSum_.bytes) * 100u / mapBytes_);
        }
        else if (mapStage_ == MapStage::Build && build == MapBuild::Stage::Finding)
        {
            std::snprintf(step, 64, kFinding, named);
            percent = mapBuild_.percent();
        }
        else if (mapStage_ == MapStage::Build && build == MapBuild::Stage::Copying)
        {
            std::snprintf(step, 64, kCopying, named);
            percent = mapBuild_.percent();
        }
        else
        {
            std::snprintf(step, 64, kReceiving, named);
            percent = mapBuild_.percent();
        }
        // What comes from the host, and how fast.
        if (mapStage_ == MapStage::Getting || (mapStage_ == MapStage::Build && build != MapBuild::Stage::Finding))
        {
            const unsigned long kb = (mapBuild_.toCome() + 1023) / 1024;
            const std::uint32_t frames = Global::vblanks() - mapGotAt_;
            const int rate = mapStage_ == MapStage::Getting && frames >= 60
                                 ? static_cast<int>(static_cast<std::uint64_t>(mapBuild_.come()) * 60u / 1024u / frames)
                                 : 0;
            if (rate > 0)
                std::snprintf(note, 64, kFromRate, kb, rate);
            else
                std::snprintf(note, 64, kFrom, kb);
        }
        return true;
    }

    bool NetGame::mapLine(char *line, int room) const
    {
        return isDSiMode() && mapLineTwl(line, room);
    }

    bool NetGame::mapReceiving(int &percent, char *step, char *note) const
    {
        return isDSiMode() && mapReceivingTwl(percent, step, note);
    }

    HL_TWL_CODE bool NetGame::mapLineTwl(char *line, int room) const
    {
        // (A client's: the host says what it is sending on its STARTING page.)
        if (phase_ != Phase::Lobby || !mapKnown_ || mapSays_ == kMapHaveYes)
            return false;
        if (mapSays_ == kMapHaveNoRoom)
            std::snprintf(line, static_cast<std::size_t>(room), kMapText, kMapNoRoom);
        else if (mapSays_ == kMapHaveCannot)
            std::snprintf(line, static_cast<std::size_t>(room), kMapText, kMapCannot);
        else if (mapStage_ == MapStage::Getting)
            std::snprintf(line, static_cast<std::size_t>(room), kMapGetting, mapBuild_.percent());
        else if (mapStage_ == MapStage::Index || mapStage_ == MapStage::Build || (mapStage_ == MapStage::Want && starting_))
            std::snprintf(line, static_cast<std::size_t>(room), kMapText, kMapAsking);
        else if (mapStage_ == MapStage::Want)
            std::snprintf(line, static_cast<std::size_t>(room), kMapText, kMapLater);
        else
            std::snprintf(line, static_cast<std::size_t>(room), kMapText, kMapChecking);
        return true;
    }
#endif

    bool NetGame::join(int index)
    {
        // (What its beacon says of its map, kept for the lobby's card.)
        lobbyMap_.clear();
        {
            std::uint8_t bytes[16];
            int size = 0, rssi = 0;
            NetRoomInfo room;
            if (phase_ == Phase::Scanning && netlink::hostRoomAt(index, bytes, size, rssi) && netRoomRead(bytes, size, room))
            {
                for (const std::string &map : maps_)
                {
                    if (room.mapHash != 0 && nameHash(map.c_str()) == room.mapHash)
                        lobbyMap_ = map;
                }
            }
        }
        char name[kNetTextBytes];
        std::snprintf(name, sizeof(name), "%s", settings.playerName());
#ifndef HLDS_CHILD
        // (Online, the host is asked: by this name.)
        if (online())
            netrelay::setName(name);
#endif
        if (phase_ != Phase::Scanning || !netlink::join(index))
            return false;
        netBegin(match_, NetMatch::Role::Client, tokenOfThisConsole(), name);
        sayProfile();
        profilesSeen_ = 0;
        phase_ = Phase::Joining;
        lostFrames_ = 0;
        joinFrames_ = 0;
        return true;
    }

    bool NetGame::joinStarter()
    {
        // What the loader that started this program left: who sent it.
        std::uint8_t address[6];
        // (Said by the host itself where its word got here; else the loader's.)
        NetDlParam param;
#ifdef HLDS_CHILD
        if (guest::bootParam(param))
            std::memcpy(address, param.host, sizeof(address));
        else
#endif
            std::memcpy(address, reinterpret_cast<const void *>(0x027FFC46), sizeof(address));
        if (!netlink::joinDirect(address, phase_ == Phase::Joining))
            return false;
        if (phase_ != Phase::Joining)
        {
            char name[kNetTextBytes];
            std::snprintf(name, sizeof(name), "%s", settings.playerName());
            netBegin(match_, NetMatch::Role::Client, tokenOfThisConsole(), name);
            sayProfile();
            profilesSeen_ = 0;
        }
        phase_ = Phase::Joining;
        lostFrames_ = 0;
        joinFrames_ = 0;
        return true;
    }

    void NetGame::leave()
    {
        silentFrames_ = 0;
        if (phase_ == Phase::Off)
            return;
        // A goodbye, said a few times over: there is no waiting to hear
        // that it arrived.
        if (netPlaced(match_) && (phase_ == Phase::Hosting || phase_ == Phase::Serving || phase_ == Phase::Lobby ||
                                  phase_ == Phase::Playing))
        {
            netSay(match_, netEvent(NetEventType::Bye));
            for (int n = 0; n < 8; ++n)
            {
                send(true);
                swiWaitForVBlank();
            }
        }
        netlink::powerDown();
        effectDraw.dropSprays();
        // (A vote left part-way: its pictures and the screen's tint go.)
        voteLetGo();
        hldata::renew(match_);
        phase_ = Phase::Off;
        startPending_ = false;
        starting_ = goAsked_ = cancelled_ = false;
        forgetGuest();
#ifndef HLDS_CHILD
        airEnd();
        guests_ = 0;
        demoStop();
#else
        guest::demoEnd();
        demoKnown_ = -1;
        demoChoice_ = 0;
#endif
    }

    void NetGame::forgetGuest()
    {
        forMapLoad();
        pictureNext_ = 0;
#ifndef HLDS_CHILD
        fetchHostStop();
        // (Out of the game: the host's map, and where a map sent was played from.)
        if (isDSiMode())
            mapForget();
        mapOfferNew_ = false;
        global.clearMapFile();
#endif
        for (std::uint32_t &at : kickAt_)
            at = 0;
#ifdef HLDS_CHILD
        airPending_ = false;
        airReplySize_ = 0;
        guest::forgetTaken();
#endif
    }

    void NetGame::dropLate(int p)
    {
        if (!isHost() || p < 1 || p >= kNetPlayers || !netHere(match_, p) || kickAt_[p] != 0)
            return;
        match_.host.out.add(netEvent(NetEventType::Bye, p, kNetRefuseLate));
        kickAt_[p] = Global::vblanks() + 60;
    }

    void NetGame::kick(int p)
    {
        if (!isHost() || p < 1 || p >= kNetPlayers || !netHere(match_, p) || kickAt_[p] != 0)
            return;
        for (std::uint16_t &token : match_.banned)
        {
            if (token == 0)
            {
                token = match_.host.clients[p].token;
                break;
            }
        }
        match_.host.out.add(netEvent(NetEventType::Bye, p, kNetRefuseKicked));
        // (A second for it to hear why, then the link.)
        kickAt_[p] = Global::vblanks() + 60;
    }

    void NetGame::abortMatch()
    {
        if (!isHost())
            return;
        NetEvent over = netEvent(NetEventType::End);
        over.v[0] = 1;
        netSay(match_, over);
        netSay(match_, netEvent(NetEventType::Cancel));
        match_.over = true;
        starting_ = goAsked_ = false;
        lobbyAsked_ = true;
    }

    void NetGame::endMatch()
    {
        if (phase_ == Phase::Playing)
            netEndMatch(match_);
    }

    void NetGame::toLobbyFromLoad()
    {
        match_.over = false;
        lobbyAsked_ = false;
        phase_ = Phase::Hosting;
        netlink::admit(true);
    }

    void NetGame::sayBackSoon()
    {
        if (!isHost())
            return;
        match_.host.out.add(netEvent(NetEventType::Bye, 0, kNetByeBackSoon));
        for (int n = 0; n < 30; ++n)
        {
            send();
            swiWaitForVBlank();
        }
    }

    const char *NetGame::backSoon()
    {
        // (Three minutes is long enough to send the game to anyone.)
        if (backSoon_[0] != 0 && Global::vblanks() - backSoonAt_ > 3 * 60 * 60)
            backSoon_[0] = 0;
        return backSoon_;
    }

    void NetGame::lost(const char *why)
    {
#ifndef HLDS_CHILD
        airEnd();
        guests_ = 0;
#endif
        netlink::powerDown();
        effectDraw.dropSprays();
        voteLetGo();
        hldata::renew(match_);
        phase_ = Phase::Off;
        startPending_ = false;
        starting_ = goAsked_ = cancelled_ = false;
        forgetGuest();
        silentFrames_ = 0;
        ended_ = why;
    }

#ifndef HLDS_CHILD
    void NetGame::start(const std::string &map, int fragLimit, int minuteLimit, int skill)
    {
        if (phase_ != Phase::Hosting)
            return;
        // Every console told at once, and each loads its map now -- those
        // with the game from their cards, those without it as this one sends
        // it -- and says when it has (Ready). This one loads its own last:
        // it cannot send and load at once. (Nobody new meanwhile.)
        // (The rules as they are now, before the start: in that order on
        // every console.)
        sayRules();
        const std::uint32_t hash = nameHash(map.c_str());
        // (How hard: every console runs its own monsters and chargers.)
        netSay(match_, netEvent(NetEventType::Start, 0, static_cast<std::int16_t>(hash & 0xFFFF),
                                static_cast<std::int16_t>(hash >> 16), fragLimit, minuteLimit, skill));
        pendingSkill_ = skill;
        match_.fragLimit = fragLimit;
        match_.minuteLimit = minuteLimit;
        netlink::admit(false);
        pendingMap_ = map;
        serveSkill_ = skill;
        serveStage_ = 0;
        serveCharacter_ = 1;
        fullTried_ = false;
        fetchHostStop();
        serveCharacters_ = 0;
        starting_ = true;
        ready_ = 0;
        goAsked_ = false;
        phase_ = Phase::Serving;
        // (Said before anything else: a console sent the game is told before
        // its stream begins, and the others load at once.)
        for (int n = 0; n < 6; ++n)
        {
            frame();
            swiWaitForVBlank();
        }
        // (A console still being sent the game itself: that first. A map's
        // picture is given up.)
        if (air_.sending() && air_.stream == AirStream::Preview)
            airEnd();
        if (!air_.sending())
            serveNext();
    }

    void NetGame::cancelStart()
    {
        if (!starting_ || !isHost() || phase_ == Phase::Playing)
            return;
        netSay(match_, netEvent(NetEventType::Cancel));
        airEnd();
        starting_ = false;
        startPending_ = false;
        phase_ = Phase::Hosting;
        netlink::admit(true);
    }

    void NetGame::serveNext()
    {
        unsigned mask = guests_ & netlink::clients();
        // Those in DSi mode first, the map at full quality, all of it in one
        // stream (if this card has it); the rest of what follows is the others'.
        if (serveStage_ == 0 && !fullTried_)
        {
            fullTried_ = true;
            fullSent_ = 0;
            unsigned dsi = 0;
            for (int c = 1; c < kNetPlayers; ++c)
                dsi |= tiers_[c] != 0 ? 1u << c : 0u;
            dsi &= mask;
            if (fullGuests_ && dsi != 0 && airBegin(AirStream::MapFull, pendingMap_, "air/" + pendingMap_ + ".dsi.hgs", dsi, serveSkill_))
            {
                fullSent_ = dsi;
                return;
            }
        }
        const unsigned everyone = mask;
        mask &= ~fullSent_;
        const NetMapInfo *info = nullptr;
        for (const NetMapInfo &one : mapInfo_)
        {
            if (one.name == pendingMap_)
                info = &one;
        }
        // The group's base (those who have it say so at once), then the map.
        if (serveStage_ == 0 && mask != 0)
        {
            serveStage_ = 1;
            const int group = info != nullptr ? info->airGroup : 0;
            if (group > 0 && group < static_cast<int>(sizeof(kAirGroupNames) / sizeof(kAirGroupNames[0])) &&
                airBegin(AirStream::Base, kAirGroupNames[group], std::string("air/") + kAirGroupNames[group] + ".base.hgs",
                         mask))
                return;
        }
        if (serveStage_ <= 1 && mask != 0)
        {
            serveStage_ = 2;
            if (airBegin(AirStream::Map, pendingMap_, "air/" + pendingMap_ + ".hgs", mask, serveSkill_))
                return;
            // (Not on this card to be sent: they cannot play it.)
            for (int c = 1; c < kNetPlayers; ++c)
            {
                if ((mask >> c) & 1)
                    netlink::kick(c);
            }
        }
        // Who they chose to be, for those sent the game (a guest takes what it
        // has room for: HlCharacters.hpp).
        mask = everyone;
        for (; mask != 0 && serveCharacter_ < kCharacterCount && serveCharacters_ < kCharacterSlots; ++serveCharacter_)
        {
            bool wanted = false;
            for (int p = 0; p < kNetPlayers; ++p)
                wanted = wanted || (netHere(match_, p) && match_.profiles[p].known && match_.profiles[p].model == serveCharacter_);
            const std::string name = kCharacterFiles[serveCharacter_];
            // Those in DSi mode first, at its finest; then the others, once.
            if (wanted && fullSent_ != 0 && !characterFullDone_ &&
                airBegin(AirStream::CharacterFull, name, "air/chr_" + name + ".dsi.hgs", fullSent_))
            {
                characterFullDone_ = true;
                return;
            }
            characterFullDone_ = false;
            const unsigned others = mask & ~fullSent_;
            if (wanted && others != 0 && airBegin(AirStream::Character, name, "air/chr_" + name + ".hgs", others))
            {
                ++serveCharacter_;
                ++serveCharacters_;
                return;
            }
            serveCharacters_ += wanted ? 1 : 0;
        }
        // Sent (or nobody to send it to): those with cards that have not the
        // map are sent it next (the frame sees to it), then this console's own.
        serveStage_ = 3;
        serveCards();
    }

    void NetGame::serveCards()
    {
        int consoles = 0, percent = 0;
        if (phase_ != Phase::Serving || serveStage_ != 3 || air_.sending() || (isDSiMode() && mapWaiting(consoles, percent)))
            return;
        serveStage_ = 4;
        phase_ = Phase::Hosting;
        startPending_ = true;
    }

    bool NetGame::airBegin(AirStream stream, const std::string &name, const std::string &path, unsigned mask, int skill)
    {
        airEnd();
        airFile_ = std::fopen(global.dataPath(path).c_str(), "rb");
        if (airFile_ == nullptr)
            return false;
        std::fseek(airFile_, 0, SEEK_END);
        const long length = std::ftell(airFile_);
        if (length <= 0)
        {
            airEnd();
            return false;
        }
        serves_ = static_cast<std::uint16_t>(serves_ + 1 == 0 ? 1 : serves_ + 1);
        air_.begin(stream, name.c_str(), static_cast<std::uint32_t>(length), serves_, mask,
                   static_cast<std::uint8_t>(skill));
        return true;
    }

    void NetGame::airEnd()
    {
        if (airFile_ != nullptr)
            std::fclose(airFile_);
        airFile_ = nullptr;
        air_.stop();
    }

    int NetGame::airSend(int frames)
    {
        std::uint8_t piece[kAirPiece];
        std::uint8_t bytes[kNetCmdBytes];
        std::uint32_t index;
        int n = 0;
        for (; n < frames && airFile_ != nullptr && air_.next(index); ++n)
        {
            const int count = air_.pieceBytes(index);
            if (std::fseek(airFile_, static_cast<long>(index) * kAirPiece, SEEK_SET) != 0 ||
                std::fread(piece, 1, static_cast<std::size_t>(count), airFile_) != static_cast<std::size_t>(count))
                break;
            const int size = air_.piece(index, piece, count, bytes);
            if (size > 0)
            {
                netlink::send(bytes, size);
                ++sent_;
            }
        }
        return n;
    }

    void NetGame::airAsk(int from, const std::uint8_t *bytes, int size)
    {
        AirStream stream;
        std::uint16_t serve;
        if (!airAskOf(bytes, size, stream, serve) || from < 1 || from >= kNetPlayers)
            return;
        // (A console that asks for any of the game is one sent it; asking
        // for a map's picture, any console does.)
        if (stream != AirStream::Preview)
            guests_ |= 1u << from;
        if (air_.asked(from, bytes, size))
            return;
        if (match_.rules.demo != 0 && stream == AirStream::MapFull)
        {
            demoServer_.heard(from, bytes, size);
            return;
        }
        // A console that has just come asks for the game itself.
        if (stream == AirStream::Common && serve == 0)
        {
            if (air_.sending() && air_.stream == AirStream::Common)
                air_.add(from);
            else if (!air_.sending())
                airBegin(AirStream::Common, "common", "air/common.hgs", 1u << from);
            // (Else it waits: what is being sent is sent first. It asks again.)
        }
    }

    bool NetGame::sending(const char *&what, int &consoles, int &percent) const
    {
        // (The host's map, to the consoles with cards that have not got it.)
        if (!air_.sending() && phase_ == Phase::Serving && serveStage_ == 3 && isDSiMode() && mapWaiting(consoles, percent))
        {
            what = pendingMap_.c_str();
            return true;
        }
        if (!air_.sending())
            return false;
        // (A map's picture is not worth saying: it is a second's.)
        if (air_.stream == AirStream::Preview)
            return false;
        what = air_.stream == AirStream::Common ? "the game" : air_.name;
        consoles = air_.waiting();
        percent = air_.percent();
        return true;
    }

    void NetGame::radioLine(char *line, int room)
    {
        // (Counted once a second; the counts wrap.)
        static netlink::DlCounts was;
        static unsigned refusedWas = 0;
        static std::uint32_t at = 0;
        static char said[64] = "";
        if (Global::vblanks() - at >= 60)
        {
            const netlink::DlCounts now = netlink::dlCounts();
            const unsigned refused = netlink::txRefused();
            std::snprintf(said, sizeof(said), "sent %u/s, done %u, again %u, full %u",
                          static_cast<unsigned>(static_cast<std::uint16_t>(now.armed - was.armed)),
                          static_cast<unsigned>(static_cast<std::uint16_t>(now.done - was.done)),
                          static_cast<unsigned>(static_cast<std::uint16_t>(now.retried - was.retried)),
                          refused - refusedWas);
            was = now;
            refusedWas = refused;
            at = Global::vblanks();
        }
        std::snprintf(line, static_cast<std::size_t>(room), "%s", said);
    }

    int NetGame::guests() const
    {
        int n = 0;
        for (int c = 1; c < kNetPlayers; ++c)
            n += ((guests_ & netlink::clients()) >> c) & 1;
        return n;
    }

    int NetGame::startStatus(int p, char *line, int room) const
    {
        // 0: nothing to say of p (not there); 1: still on its way; 2: ready.
        if (p < 0 || p >= kNetPlayers || !netHere(match_, p))
            return 0;
        if (p == 0)
        {
            std::snprintf(line, static_cast<std::size_t>(room), "%s", air_.sending() ? "sending" : "loading");
            return 1;
        }
        if ((ready_ >> p) & 1)
        {
            std::snprintf(line, static_cast<std::size_t>(room), "ready");
            return 2;
        }
        const AirSender::Want &want = air_.wants[p];
        if (phase_ == Phase::Serving && !air_.sending() && mapHave_[p] == kMapHaveGetting)
            std::snprintf(line, static_cast<std::size_t>(room), "receiving %d%%", mapPermille_[p] / 10);
        else if (air_.sending() && want.on && !want.done)
            std::snprintf(line, static_cast<std::size_t>(room), "receiving %d%%",
                          air_.pieces() == 0 ? 0 : static_cast<int>(want.base * 100 / air_.pieces()));
        else
            std::snprintf(line, static_cast<std::size_t>(room), "loading");
        return 1;
    }
#else
    // (A console that was sent the game is never a host.)
    void NetGame::start(const std::string &, int, int, int) {}
    void NetGame::cancelStart() {}
    int NetGame::startStatus(int, char *, int) const { return 0; }
    bool NetGame::mapLine(char *, int) const { return false; }
    bool NetGame::mapReceiving(int &, char *, char *) const { return false; }
    bool NetGame::sending(const char *&, int &, int &) const { return false; }
    int NetGame::guests() const { return 0; }
    void NetGame::radioLine(char *line, int room) { std::snprintf(line, static_cast<std::size_t>(room), "%s", ""); }

    bool NetGame::airAsked(AirHead &head)
    {
        if (!airPending_)
            return false;
        airPending_ = false;
        head = airAsked_;
        return true;
    }
#endif

    std::uint32_t NetGame::guestRoom() const
    {
        std::uint32_t least = 0;
        for (int c = 1; c < kNetPlayers; ++c)
        {
            if (rooms_[c] != 0 && netHere(match_, c) && (least == 0 || rooms_[c] < least))
                least = rooms_[c];
        }
        return least;
    }

    std::uint32_t NetGame::roomOf(int p) const
    {
        return p > 0 && p < kNetPlayers && netHere(match_, p) ? rooms_[p] : 0;
    }

    std::uint32_t NetGame::freeOf(int p) const
    {
        return roomOf(p) != 0 ? frees_[p] : 0;
    }

    void NetGame::sayRoom(std::uint32_t top, std::uint32_t free)
    {
        if (on() && !isHost())
            netSay(match_, netEvent(NetEventType::Room, 0, static_cast<std::int16_t>(top & 0xFFFF),
                                    static_cast<std::int16_t>(top >> 16), static_cast<std::int16_t>(free & 0xFFFF),
                                    static_cast<std::int16_t>(free >> 16), global.twlChild ? 1 : 0));
    }

    void NetGame::loadedMap()
    {
        // A client says so; the host gives the others a while from now.
        if (isHost())
            loadedAt_ = Global::vblanks();
        else
            netSay(match_, netEvent(NetEventType::Ready));
    }

    bool NetGame::goAsked()
    {
        if (!starting_)
            return false;
        if (isHost())
        {
            // Everyone there ready -- or thirty seconds since this console
            // was: those not yet are let go, and the rest begin.
            const unsigned here = netlink::clients();
            if ((ready_ & here) != here && Global::vblanks() - loadedAt_ < kReadyVblanks)
                return false;
            for (int c = 1; c < kNetPlayers; ++c)
            {
                if ((here >> c) & 1 && !((ready_ >> c) & 1))
                    dropLate(c);
            }
            netSay(match_, netEvent(NetEventType::Go));
            goAsked_ = true;
        }
        if (!goAsked_)
            return false;
        starting_ = false;
        goAsked_ = false;
        return true;
    }

    bool NetGame::cancelled()
    {
        const bool was = cancelled_;
        cancelled_ = false;
        return was;
    }

    bool NetGame::startAsked(std::string &map, int &skill)
    {
        if (!startPending_)
            return false;
        startPending_ = false;
        map = pendingMap_;
        skill = pendingSkill_;
        return true;
    }

    void NetGame::mapLoaded()
    {
        profilesSeen_ = 0; // (who they chose to be: what the map has now loaded)
        if (match_.minuteLimit >= 2)
            sound.loadMatchWarning();
        const LevelView &view = level.view;
        spawns_.clear();
        for (std::uint32_t e = 0; e < view.entityCount; ++e)
        {
            const EntityRecord &entity = view.entities[e];
            if (entity.classId != static_cast<std::uint16_t>(EntityClass::InfoPlayerDeathmatch))
                continue;
            NetSpawn spawn;
            for (int i = 0; i < 3; ++i)
                spawn.pos[i] = entity.origin[i];
            spawn.yaw = entity.angles[1];
            spawns_.push_back(spawn);
        }
        // (A map with none: where a game alone starts.)
        if (spawns_.empty())
        {
            NetSpawn spawn;
            for (int i = 0; i < 3; ++i)
                spawn.pos[i] = player.state.origin[i];
            spawn.yaw = 0;
            spawns_.push_back(spawn);
        }
        int model = -1;
        for (std::uint32_t m = 0; m < view.modelRefCount; ++m)
        {
            if (std::strcmp(view.modelRefs[m].name, "player") == 0)
                model = static_cast<int>(m);
        }
        gameApi.netStart(match_, game.logic, player.state, static_cast<int>(view.actorCount), model, spawns_.data(),
                 static_cast<int>(spawns_.size()), tokenOfThisConsole());
        phase_ = Phase::Playing;
        // Everyone's spray, made into its picture now, while the map is
        // being put up (nobody comes once the match has begun).
        effectDraw.loadSprays(match_);
        profilesSeen_ = 0;
        overMs_ = 0;
        lobbyAsked_ = false;

        // A development aid (tools/emu/play.sh with two consoles): "x y z
        // yaw" in debug_net.txt, whole units and degrees, puts the host
        // there and every client 160 units in front of it, facing it -- so
        // that the two see each other from the first frame.
        if (std::FILE *file = std::fopen(global.dataPath("debug_net.txt").c_str(), "r"))
        {
            int x, y, z, yaw;
            if (std::fscanf(file, "%d %d %d %d", &x, &y, &z, &yaw) == 4)
            {
                const std::uint16_t turn = static_cast<std::uint16_t>(yaw * 65536 / 360);
                fx sine, cosine;
                fxSinCos(turn, sine, cosine);
                const int far = match_.me == 0 ? 0 : 160 * match_.me;
                player.state.origin[0] = fxFromInt(x) + far * cosine;
                player.state.origin[1] = fxFromInt(y) + far * sine;
                player.state.origin[2] = fxFromInt(z);
                match_.respawned = false;
                camera.yaw = static_cast<std::uint16_t>(match_.me == 0 ? turn : turn + 32768);
                camera.pitch = 0;
            }
            std::fclose(file);
        }
    }

    void NetGame::send(bool always)
    {
        // A client answers the host's frames, one for each it has heard: a
        // client running faster than its host would queue more than the
        // radio can carry away, until nothing moves at all.
        if (!isHost() && !always)
        {
            if (heard_ == answered_)
                return;
            answered_ = heard_;
        }
        std::uint8_t bytes[kNetCmdBytes];
        const int size = netWrite(match_, bytes);
        if (size > 0)
        {
            netlink::send(bytes, size);
            ++sent_;
        }
    }

    void NetGame::frame()
    {
        if (phase_ == Phase::Off || phase_ == Phase::Scanning)
            return;
        if (phase_ == Phase::Joining)
        {
            if (netlink::joined())
            {
                phase_ = Phase::Lobby;
                lostFrames_ = 0;
            }
            else if (netlink::refused())
            {
#ifndef HLDS_CHILD
                if (online())
                {
                    lost(netrelay::refusedText());
                    return;
                }
#endif
                lost("That game is full, or a match is on.");
                return;
            }
            else
            {
                // (Online, the host is asked first, and may take its time: the
                // relay gives up on it after half a minute.)
                if (++joinFrames_ >= (online() ? 35 : 10) * 60)
                    lost("That game did not answer.");
                return;
            }
        }

        // Who is there.
        if (isHost())
        {
            for (int c = 1; c < kNetPlayers; ++c)
            {
                if (kickAt_[c] != 0 && static_cast<std::int32_t>(Global::vblanks() - kickAt_[c]) >= 0)
                {
                    kickAt_[c] = 0;
                    netlink::kick(c);
                }
            }
            const unsigned now = netlink::clients();
            for (int c = 1; c < kNetPlayers; ++c)
            {
                const unsigned bit = 1u << c;
                if ((now & bit) && !(clientsWere_ & bit))
                {
                    netHostJoin(match_.host, c);
#ifndef HLDS_CHILD
                    mapHave_[c] = kMapUnheard;
                    mapHeardAt_[c] = Global::vblanks();
#endif
                }
                else if (!(now & bit) && (clientsWere_ & bit))
                {
#ifndef HLDS_CHILD
                    air_.drop(c);
                    fetch_.forget(c);
                    guests_ &= ~bit;
#endif
                    rooms_[c] = 0;
                    tiers_[c] = 0;
                    netHostLeave(match_.host, c);
                    match_.names[c][0] = 0;
                    NetEvent gone;
                    gone.type = NetEventType::Roster;
                    gone.a = static_cast<std::uint8_t>(c);
                    match_.host.out.add(gone);
                    if (match_.noticeCount < NetMatch::kMostNotices)
                    {
                        match_.notices[match_.noticeCount].type = NetEventType::Bye;
                        match_.notices[match_.noticeCount++].who = static_cast<std::uint8_t>(c);
                    }
                }
            }
            clientsWere_ = now;
        }
        else if (!netlink::joined())
        {
            if (++lostFrames_ >= kLostFrames)
            {
                lost("The link to the host was lost.");
                return;
            }
        }
        else
        {
            lostFrames_ = 0;
            // (The radio can go on saying it is joined to a console switched
            // off. Nothing heard: ten seconds in a match; while the host may
            // be loading, a minute.)
            if (heard_ != heardWas_)
            {
                heardWas_ = heard_;
                silentFrames_ = 0;
            }
            else if (++silentFrames_ >= (phase_ == Phase::Playing ? 600 : 3600))
            {
                lost("The host has gone quiet.");
                return;
            }
        }

        // What has come: the game's, and what is said of the streams sent
        // to consoles that were given the game (HlNetAir.hpp).
        std::uint8_t bytes[kNetCmdBytes];
        int from = 0;
        for (int size; (size = netlink::take(bytes, from)) > 0;)
        {
            ++heard_;
#ifndef HLDS_CHILD
            if (size > 3 && bytes[3] == kNetFrameAirAsk)
            {
                if (isHost())
                    airAsk(from, bytes, size);
                continue;
            }
#endif
#ifndef HLDS_CHILD
            if (size > 3 && bytes[3] == kNetFrameDemoUp)
            {
                if (isHost())
                    demoUpHeard(from, bytes, size);
                continue;
            }
#else
            if (size > 3 && bytes[3] == kNetFrameDemoAck)
            {
                guest::demoAck(bytes, size);
                continue;
            }
#endif
#ifndef HLDS_CHILD
            if (size > 3 && bytes[3] == kNetFrameFetchHave)
            {
                if (isHost() && isDSiMode())
                    mapHaveHeard(from, bytes, size);
                continue;
            }
#endif
            if (size > 3 && bytes[3] == kNetFrameFetchData)
            {
                FetchHead head;
                if (!isHost() && fetchRead(bytes, size, head))
                    fetchFrame(head);
                continue;
            }
            if (size > 3 && bytes[3] == kNetFrameAirData)
            {
                AirHead head;
#ifndef HLDS_CHILD
                continue;
#else
                int reply = 0;
                if (guest::airFrame(bytes, size, airReply_, reply, head))
                {
                    airAsked_ = head;
                    airPending_ = true;
                }
                if (reply > 0)
                    airReplySize_ = reply;
                continue;
#endif
            }
            gameApi.netRead(match_, bytes, size, from);
        }
#ifndef HLDS_CHILD
        // Who is left out of a stream for saying nothing is let go; a
        // stream that all have is done with, and the next sent.
        if (air_.sending())
        {
            // (Not a console that stopped asking for a picture: only one
            // the match, or the game itself, is waiting for.)
            const unsigned out = air_.frame();
            for (int c = 1; c < kNetPlayers && air_.stream != AirStream::Preview; ++c)
            {
                if ((out >> c) & 1)
                {
                    netlink::kick(c);
                    guests_ &= ~(1u << c);
                }
            }
            if (air_.done())
            {
                airEnd();
                if (phase_ == Phase::Serving)
                    serveNext();
            }
        }
#endif

        // The lobby's part of it; with a map loaded, the game's tick does
        // the rest.
        if (!match_.playing)
        {
            static LogicWorld none;
            static PlayerState nobody;
            gameApi.netLobby(match_, none, nobody);
            voteFrame(17); // (a vote before the first match)
            if (isHost())
            {
#ifndef HLDS_CHILD
                // A stream being sent: as fast as the air takes it while a
                // match waits for it (the game's frames now and then, for
                // those who have the game), every other frame in the lobby.
                // (A frame of some kind every frame, whatever: a client
                // answers only a frame of the host's -- one that has nothing
                // to say for itself would wait for nothing.)
                // (A console just come, waiting for the game itself: the same.)
                if (match_.rules.demo != 0 && !air_.sending())
                {
                    // (A frame of the game's now and then, as below; and one
                    // every frame with nothing else to send, to be answered.)
                    if ((++ticks_ & 7) == 0)
                        send();
                    if (demoFrame() == 0 && (ticks_ & 7) != 0)
                        send();
                }
                else if ((phase_ == Phase::Serving || air_.stream == AirStream::Common) && air_.sending())
                {
                    // (Four a frame: queued, they go back to back -- the radio
                    // starts the next as each exchange ends.)
                    if ((++ticks_ & 7) == 0)
                        send();
                    if (airSend((ticks_ & 7) == 0 ? kAirServeFrames - 1 : kAirServeFrames) == 0 && (ticks_ & 7) != 0)
                        send();
                }
                else if (phase_ == Phase::Serving && serveStage_ == 3)
                {
                    // The map's archive, to cards without it, as the guests'
                    // streams are sent: the game's frame now and then, and
                    // one whenever there is nothing else, to be answered.
                    if ((++ticks_ & 7) == 0)
                        send();
                    if (fetchHostFrame((ticks_ & 7) == 0 ? kAirServeFrames - 1 : kAirServeFrames) == 0 && (ticks_ & 7) != 0)
                        send();
                }
                else if (!(air_.sending() && (++ticks_ & 1) && airSend(1) > 0))
                    send();
                if (!(phase_ == Phase::Serving && serveStage_ == 3))
                    fetchHostFrame();
                if ((phase_ == Phase::Hosting || phase_ == Phase::Serving) && isDSiMode())
                    mapHostFrame();
                serveCards();
#endif
            }
            else
            {
                // A client: what a stream's frame asked to be answered, in
                // place of the game's; and on with what it asks for.
#ifndef HLDS_CHILD
                if (phase_ == Phase::Lobby && isDSiMode())
                    mapClientFrame();
#endif
                fetchTick();
                std::uint8_t reply[kNetReplyBytes];
                int size = 0;
#ifdef HLDS_CHILD
                if (airReplySize_ > 0)
                {
                    std::memcpy(reply, airReply_, static_cast<std::size_t>(airReplySize_));
                    size = airReplySize_;
                    airReplySize_ = 0;
                }
                // (A demo's map: how far it has got, and now and then what the
                // game says -- which is how what is asked for gets through.)
                else if (match_.rules.demo != 0 && (++ticks_ & 7) != 0)
                    size = guest::demoAnswer(reply);
#endif
#ifndef HLDS_CHILD
                // A map's archive coming: what it has, a frame of its own (the
                // game's every eighth frame, for its events).
                if (takingOn_ && taking_.from == FetchFrom::MapCard && (++ticks_ & 7) != 0)
                    size = mapHaveReply(reply);
#endif
                if (size > 0)
                    netlink::send(reply, size);
                else
                    send();
            }
        }

        // Everyone in their own colours, as their profiles say.
        if (match_.profileStamp != profilesSeen_)
        {
            profilesSeen_ = match_.profileStamp;
            for (int p = 0; p < kNetPlayers; ++p)
            {
                const NetProfile &profile = match_.profiles[p];
                if (!profile.known)
                    continue;
                const int slot = modelCache.characterSlot(profile.model);
                match_.characterModel[p] = static_cast<std::uint16_t>(slot >= 0 ? slot : 0);
                modelCache.setPlayerCharacter(p, slot);
                modelCache.setPlayerColours(p, profile.top, profile.bottom);
            }
        }
        if (match_.refused)
        {
            match_.passedCount = 0;
            lost(match_.refused == kNetRefuseKicked ? "The host has let you go."
                 : match_.refused == kNetRefuseLate ? "This console took too long to load the map."
                 : match_.refused == kNetRefuseNoRoom ? netDemoRefusal(false)
                                                    : "The game is full.");
            return;
        }

        // What the match leaves to whoever runs the screens.
        for (int n = 0; n < match_.passedCount; ++n)
        {
            const NetEvent &event = match_.passed[n];
            if (event.type == NetEventType::Ready && isHost() && event.from > 0 && event.from < kNetPlayers)
            {
                ready_ |= 1u << event.from;
                continue;
            }
            if (event.type == NetEventType::Room && isHost() && event.from > 0 && event.from < kNetPlayers)
            {
                rooms_[event.from] = static_cast<std::uint16_t>(event.v[0]) |
                                     static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[1])) << 16;
                frees_[event.from] = static_cast<std::uint16_t>(event.v[2]) |
                                     static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[3])) << 16;
                tiers_[event.from] = static_cast<std::uint8_t>(event.v[4]);
#ifndef HLDS_CHILD
                // (Only a console sent the game says this: one come back has
                // asked for nothing, and is to be sent the maps all the same.)
                guests_ |= 1u << event.from;
                // A demo: for a console in DSi mode alone, and told why not.
                if (match_.rules.demo != 0 && tiers_[event.from] == 0)
                {
                    match_.host.out.add(netEvent(NetEventType::Bye, event.from, kNetRefuseNoRoom));
                    kickAt_[event.from] = Global::vblanks() + 60;
                }
#endif
                continue;
            }
            if (event.type == NetEventType::End && !isHost() && event.v[0] == 1)
            {
                lobbyAsked_ = true;
                continue;
            }
#ifdef HLDS_CHILD
            if (event.type == NetEventType::DemoHave)
            {
                if (event.a == match_.me)
                    demoKnown_ = event.v[0] != 0 ? 1 : 0;
                continue;
            }
#endif
#ifndef HLDS_CHILD
            if (event.type == NetEventType::DemoWant && isHost())
            {
                demoHeard(event);
                continue;
            }
            if (event.type == NetEventType::DemoHello && isHost())
            {
                demoHello(event);
                continue;
            }
            if (event.type == NetEventType::Fetch && isHost())
            {
                fetch_.ask(event.from, static_cast<FetchFrom>(event.a),
                           static_cast<std::uint16_t>(event.v[0]) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[1])) << 16);
                continue;
            }
            if (event.type == NetEventType::FetchHave && isHost())
            {
                std::uint64_t bits = 0;
                for (int k = 0; k < 4; ++k)
                    bits |= static_cast<std::uint64_t>(static_cast<std::uint16_t>(event.v[1 + k])) << (16 * k);
                fetch_.have(event.from, event.a, static_cast<std::uint16_t>(event.v[0]), bits);
                continue;
            }
            if (event.type == NetEventType::FetchAgain && isHost())
            {
                fetch_.again(event.from, event.a);
                continue;
            }
            if (event.type == NetEventType::MapHave || event.type == NetEventType::MapSum)
            {
                if (isDSiMode())
                    mapHeard(event);
                else if (event.type == NetEventType::MapSum && !isHost())
                {
                    // (A DS: nothing is sent to it -- that is the DSi's memory.
                    // It has the map by its name, or not: said, so that it is
                    // not waited for.)
                    const std::uint32_t hash = static_cast<std::uint16_t>(event.v[0]) |
                                               static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[1])) << 16;
                    bool own = false;
                    for (const std::string &map : maps_)
                        own = own || nameHash(map.c_str()) == hash;
                    netSay(match_, netEvent(NetEventType::MapHave, own ? kMapHaveYes : kMapHaveCannot, 0, event.v[0], event.v[1]));
                }
                continue;
            }
#endif
            if (event.type == NetEventType::FetchNone && !isHost())
            {
                if (takingOn_ && static_cast<FetchFrom>(event.a) == taking_.from &&
                    (static_cast<std::uint16_t>(event.v[0]) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[1])) << 16) == taking_.what)
                    fetchGaveUp(true);
                continue;
            }
            if (event.type == NetEventType::Go && !isHost())
            {
                goAsked_ = true;
                continue;
            }
            if (event.type == NetEventType::Cancel && !isHost())
            {
                starting_ = goAsked_ = startPending_ = false;
                cancelled_ = true;
                continue;
            }
            if (event.type == NetEventType::Start && !isHost())
            {
                match_.vote = NetVote();
                voteLetGo();
#ifdef HLDS_CHILD
                guest::forgetTaken(); // (a new match: nothing of the last is held)
#endif
                starting_ = true;
                goAsked_ = cancelled_ = false;
                const std::uint32_t hash = static_cast<std::uint16_t>(event.v[0]) |
                                           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[1])) << 16);
                pendingMap_.clear();
                match_.fragLimit = event.v[2];
                match_.minuteLimit = event.v[3];
                // (A host from before there was a choice says 0: MEDIUM.)
                pendingSkill_ = event.v[4] >= 1 && event.v[4] <= 3 ? event.v[4] : 2;
#ifdef HLDS_CHILD
                // (Sent the game: its map comes as the host sends it -- airAsked.)
                (void)hash;
                continue;
#endif
#ifndef HLDS_CHILD
                // The host's map, as it said it was (a DSi's: mapForStart); else
                // the card's of its name.
                global.clearMapFile();
                const int said = isDSiMode() ? mapForStart(hash) : 0;
                // (Still being checked, or to be sent: loaded once it is here --
                // mapClientFrame.)
                mapStartWait_ = said == 4;
                if (said == 4)
                    continue;
                for (const std::string &map : maps_)
                {
                    if (said == 0 && nameHash(map.c_str()) == hash)
                        pendingMap_ = map;
                }
                if (pendingMap_.empty())
                {
                    match_.passedCount = 0;
                    leave();
                    ended_ = said == 3 ? "There is not enough room on the card for the host's map."
                                       : "This card has not the host's map.";
                    return;
                }
#endif
                startPending_ = true;
            }
            else if (event.type == NetEventType::Bye && !isHost() && event.from == 0)
            {
                match_.passedCount = 0;
                if (event.v[0] == kNetByeBackSoon)
                {
                    std::snprintf(backSoon_, sizeof(backSoon_), "%s", match_.names[0]);
                    backSoonAt_ = Global::vblanks();
                    lost("The host is sending the game: back soon.");
                    return;
                }
                lost("The host has left.");
                return;
            }
        }
        match_.passedCount = 0;
    }

    void NetGame::tick(std::uint16_t yaw, std::int32_t pitch, std::uint32_t did, bool lamp, int ms)
    {
        if (phase_ != Phase::Playing)
            return;
        gameApi.netTick(match_, game.logic, player.state, yaw, pitch, did, lamp, ms);
        // The match over: its scores for a while, then the lobby -- when the
        // host says, so that nobody is still here when the next map is sent.
        if (match_.over && !lobbyAsked_)
        {
            overMs_ += ms;
#ifndef HLDS_CHILD
            if (isHost())
            {
                if (!nextChosen_)
                    chooseNext();
                // (A vote keeps its own time: voteFrame.)
                if (match_.vote.count <= 1 && overMs_ >= match_.rules.chatSeconds * 1000)
                    goBackToLobby();
                // (A console come for the game itself, meanwhile.)
                if (air_.sending() && air_.stream == AirStream::Common)
                    airSend(1);
            }
#endif
            if (!isHost() && overMs_ >= (match_.rules.chatSeconds + match_.rules.voteSeconds + 30) * 1000)
                lobbyAsked_ = true;
            voteFrame(ms);
        }
        // (More than four: the host's frames every other frame, so that the
        // air has room for everyone's answers. Each glides between.)
        int here = 0;
        for (int p = 0; p < kNetPlayers; ++p)
            here += netHere(match_, p) ? 1 : 0;
        if (!isHost() || here <= 4 || (++ticks_ & 1) == 0)
            send();
        // What is asked for goes on through a match, after the game's own.
        if (isHost())
        {
#ifndef HLDS_CHILD
            fetchHostFrame();
#endif
        }
        else
            fetchTick();
    }

    const char *NetGame::nextName() const
    {
#ifndef HLDS_CHILD
        if (isHost())
            return nextMap_.c_str();
#endif
        return match_.nextName;
    }

#ifndef HLDS_CHILD
    // ---------------------------------------------------------- serving a demo

    bool NetGame::demoThere(int group)
    {
        const char *const *maps = netDemoMaps(group);
        const auto there = [](const std::string &path)
        {
            std::FILE *file = std::fopen(global.dataPath(path).c_str(), "rb");
            if (file != nullptr)
                std::fclose(file);
            return file != nullptr;
        };
        for (int m = 0; m < kDemoMapsMost && maps[m] != nullptr; ++m)
        {
            if (!there(std::string("air/") + maps[m] + ".dsi.hgs") || !there(std::string("air/") + maps[m] + ".dsi.bank"))
                return false;
        }
        return true;
    }

    namespace
    {
        std::uint32_t demoFileLength(const std::string &path)
        {
            std::uint32_t length = 0;
            if (std::FILE *file = std::fopen(path.c_str(), "rb"))
            {
                std::fseek(file, 0, SEEK_END);
                const long end = std::ftell(file);
                length = end > 0 ? static_cast<std::uint32_t>(end) : 0;
                std::fclose(file);
            }
            return length;
        }
        std::string demoTrackPath(int track)
        {
            char name[16];
            musicFileName(track, name, sizeof(name));
            return global.dataPath(std::string("music/") + name);
        }
        /// The tracks a demo's map plays, as its bank says (kDemoTracksFile).
        int demoBankTracks(const std::string &bank, int *tracks)
        {
            std::FILE *file = std::fopen(bank.c_str(), "rb");
            if (file == nullptr)
                return 0;
            int count = 0;
            AirBankHeader header;
            const std::uint32_t want = nameHash(kDemoTracksFile);
            if (std::fread(&header, sizeof(header), 1, file) == 1 && std::memcmp(header.magic, kAirBankMagic, 4) == 0 &&
                header.version == kAirBankVersion)
            {
                for (int i = 0; i < header.count; ++i)
                {
                    AirBankEntry entry;
                    if (std::fread(&entry, sizeof(entry), 1, file) != 1)
                        break;
                    if (entry.name != want)
                        continue;
                    std::uint8_t numbers[kDemoTracksMost];
                    const std::uint32_t take = entry.raw < kDemoTracksMost ? entry.raw : kDemoTracksMost;
                    if (entry.packed == entry.raw && std::fseek(file, static_cast<long>(entry.offset), SEEK_SET) == 0 &&
                        std::fread(numbers, 1, take, file) == take)
                    {
                        for (std::uint32_t n = 0; n < take; ++n)
                        {
                            if (numbers[n] >= 2 && numbers[n] <= 99)
                                tracks[count++] = numbers[n];
                        }
                    }
                    break;
                }
            }
            std::fclose(file);
            return count;
        }
        /// The checksum of what is sent of a stream: `first`, then (if any)
        /// `second` at demoBankAt.
        std::uint32_t demoStreamCrc(const std::string &first, const std::string &second)
        {
            static std::uint8_t chunk[4096];
            std::uint32_t crc = kCrcStart, length = 0;
            if (std::FILE *file = std::fopen(first.c_str(), "rb"))
            {
                for (std::size_t got; (got = std::fread(chunk, 1, sizeof(chunk), file)) > 0;)
                {
                    crc = crc32Update(crc, chunk, got);
                    length += static_cast<std::uint32_t>(got);
                }
                std::fclose(file);
            }
            if (!second.empty())
            {
                std::memset(chunk, 0, 32);
                crc = crc32Update(crc, chunk, demoBankAt(length) - length);
                if (std::FILE *file = std::fopen(second.c_str(), "rb"))
                {
                    for (std::size_t got; (got = std::fread(chunk, 1, sizeof(chunk), file)) > 0;)
                        crc = crc32Update(crc, chunk, got);
                    std::fclose(file);
                }
            }
            return crc32Finish(crc);
        }
    } // namespace

    bool NetGame::demoHost(int group)
    {
        // How long the longest is: what a receiver makes its room by. A map's
        // stream is all of it: its .dsi.hgs, then its bank.
        const char *const *maps = netDemoMaps(group);
        std::uint32_t longest = 0, music = 0;
        for (DemoTrack &track : demoTracks_)
            track = DemoTrack();
        for (int m = 0; m < kDemoMapsMost && maps[m] != nullptr; ++m)
        {
            demoLength_[m] = demoCrc_[m] = demoGuest_[m] = 0;
            const std::string air = std::string("air/") + maps[m];
            const std::uint32_t guest = demoFileLength(global.dataPath(air + ".dsi.hgs"));
            const std::uint32_t bank = demoFileLength(global.dataPath(air + ".dsi.bank"));
            if (guest == 0 || bank == 0)
                continue;
            demoGuest_[m] = guest;
            demoLength_[m] = demoBankAt(guest) + bank;
            longest = demoLength_[m] > longest ? demoLength_[m] : longest;
            // ... and the music it plays, all held at once (and what is held
            // from the map before may stay: the room is the most any map's
            // tracks come to).
            int tracks[kDemoTracksMost];
            const int count = demoBankTracks(global.dataPath(air + ".dsi.bank"), tracks);
            std::uint32_t mapMusic = 0;
            for (int t = 0; t < count; ++t)
            {
                const std::uint32_t length = demoFileLength(demoTrackPath(tracks[t]));
                mapMusic += (length + 31u) & ~31u;
                for (DemoTrack &track : demoTracks_)
                {
                    if (track.track == tracks[t] || track.track == 0)
                    {
                        track.track = tracks[t];
                        track.length = length;
                        break;
                    }
                }
            }
            music = mapMusic > music ? mapMusic : music;
        }
        if (longest == 0 || !host())
            return false;
        match_.rules.demo = static_cast<std::uint8_t>(group);
        match_.rules.demoLongest = static_cast<std::uint16_t>((longest + 16383) / 16384);
        const std::uint32_t musicUnits = (music + 65535) / 65536;
        match_.rules.demoMusic = static_cast<std::uint8_t>(musicUnits > 255 ? 0 : musicUnits);
        demoServer_ = DemoServer();
        for (int c = 0; c < kNetPlayers; ++c)
        {
            demoTokens_[c] = 0;
            demoMapOf_[c] = 0;
            demoIds_[c] = 0;
        }
        sayRules();
        return true;
    }

    void NetGame::demoStop()
    {
        for (int s = 0; s < DemoServer::kStreams; ++s)
            demoClose(s);
        demoServer_ = DemoServer();
        demoLiveNow_ = false;
        for (DemoUpTaker &taker : demoTakers_)
        {
            std::free(taker.room);
            taker = DemoUpTaker();
        }
    }

    bool NetGame::demoRow(int row, char *line, int room, int &bars)
    {
        const char *const *maps = netDemoMaps(match_.rules.demo);
        for (int c = 1; c < kNetPlayers; ++c)
        {
            if (!netHere(match_, c) || row-- != 0)
                continue;
            bars = this->bars(c);
            const char *map = demoMapOf_[c] != 0 ? maps[demoMapOf_[c] - 1] : "";
            const int percent = demoTokens_[c] != 0 ? demoServer_.percentOf(demoTokens_[c]) : -1;
            if (demoTokens_[c] != 0 && percent < 0)
                std::snprintf(line, static_cast<std::size_t>(room), "%.11s  %s  playing", match_.names[c], map);
            else if (percent >= 0)
                std::snprintf(line, static_cast<std::size_t>(room), "%.11s  %s  %d%%", match_.names[c], map, percent);
            else
                std::snprintf(line, static_cast<std::size_t>(room), "%.11s  joining", match_.names[c]);
            return true;
        }
        return false;
    }

    std::string NetGame::demoFolder(int c) const
    {
        char name[48];
        std::snprintf(name, sizeof(name), "demo/%s/%08lx/", kAirGroupNames[match_.rules.demo],
                      static_cast<unsigned long>(demoIds_[c]));
        return global.savesDir() + name;
    }

    std::string NetGame::demoFilePath(int c, std::uint32_t what) const
    {
        char name[16];
        std::snprintf(name, sizeof(name), "%08lx", static_cast<unsigned long>(what));
        return demoFolder(c) + name;
    }

    void NetGame::demoHello(const NetEvent &event)
    {
        const int c = event.from;
        if (c < 1 || c >= kNetPlayers)
            return;
        demoIds_[c] = static_cast<std::uint16_t>(event.v[0]) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[1])) << 16;
        // Its progress let go of: at the end of the demo, or to begin again.
        if (event.a != 0)
        {
            const std::string folder = demoFolder(c);
            if (DIR *dir = opendir(folder.c_str()))
            {
                while (const dirent *entry = readdir(dir))
                {
                    if (entry->d_name[0] != '.')
                        std::remove((folder + entry->d_name).c_str());
                }
                closedir(dir);
            }
            return;
        }
        std::FILE *file = std::fopen(demoFilePath(c, kDemoFileAuto).c_str(), "rb");
        if (file != nullptr)
            std::fclose(file);
        match_.host.out.add(netEvent(NetEventType::DemoHave, c, file != nullptr ? 1 : 0));
    }

    void NetGame::demoUpHeard(int from, const std::uint8_t *bytes, int size)
    {
        DemoUpHead head;
        if (from < 1 || from >= kNetPlayers || demoIds_[from] == 0 || !demoUpRead(bytes, size, head) ||
            head.total > kDemoFileMost)
            return;
        DemoUpTaker &taker = demoTakers_[from];
        if (!taker.taking(head))
        {
            std::free(taker.room);
            taker = DemoUpTaker();
            auto *room = static_cast<std::uint8_t *>(std::malloc(head.total > 0 ? head.total : 1));
            if (room == nullptr)
                return;
            taker.open(head, room);
        }
        if (taker.take(head))
        {
            // Whole and checked: it takes the place of the last, or it is not
            // said to have come, and comes again.
            std::string path = demoFilePath(from, head.what);
            const std::string fresh = path + ".new";
            const std::string folder = demoFolder(from);
            mkdir((global.savesDir() + "demo").c_str(), 0777);
            mkdir((global.savesDir() + "demo/" + kAirGroupNames[match_.rules.demo]).c_str(), 0777);
            mkdir(folder.substr(0, folder.size() - 1).c_str(), 0777);
            bool wrote = false;
            if (std::FILE *file = std::fopen(fresh.c_str(), "wb"))
            {
                wrote = std::fwrite(taker.room, 1, taker.total, file) == taker.total;
                wrote = std::fclose(file) == 0 && wrote;
            }
            if (wrote)
            {
                std::remove(path.c_str());
                wrote = std::rename(fresh.c_str(), path.c_str()) == 0;
            }
            if (!wrote)
            {
                std::remove(fresh.c_str());
                std::free(taker.room);
                taker = DemoUpTaker();
                return;
            }
            std::free(taker.room);
            taker.room = nullptr;
            taker.what = head.what;
            taker.total = head.total;
            taker.crc = head.crc;
            taker.upTo = head.total;
            taker.whole = true;
        }
        std::uint8_t ack[32];
        const std::uint32_t upTo = taker.whole && taker.what == head.what ? head.total : taker.upTo;
        const int length = demoAckWrite(from, head.what, head.crc, upTo, ack, sizeof(ack));
        if (length > 0)
            netlink::send(ack, length);
    }

    void NetGame::demoHeard(const NetEvent &event)
    {
        const int c = event.from;
        if (c < 1 || c >= kNetPlayers)
            return;
        const std::uint32_t map = static_cast<std::uint16_t>(event.v[0]) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[1])) << 16;
        const std::uint32_t from = static_cast<std::uint16_t>(event.v[2]) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[3])) << 16;
        // A track of the music: one of those the demo's maps play.
        if (event.v[4] != 0)
        {
            for (DemoTrack &track : demoTracks_)
            {
                if (track.track != event.v[4] || track.length == 0)
                    continue;
                char name[kAirNameBytes];
                demoTrackName(track.track, name, sizeof(name));
                if (nameHash(name) != map)
                    return;
                if (track.crc == 0)
                    track.crc = demoStreamCrc(demoTrackPath(track.track), std::string());
                demoTokens_[c] = match_.host.clients[c].token;
                if (from == 0xFFFFFFFFu)
                {
                    demoServer_.lost(demoTokens_[c]);
                    return;
                }
                demoServer_.want(demoTokens_[c], c, map, event.a != 0, from, name, track.length, track.crc,
                                 static_cast<std::uint16_t>(0x200 | track.track), AirStream::Music);
                return;
            }
            return;
        }
        const char *const *maps = netDemoMaps(match_.rules.demo);
        for (int m = 0; m < kDemoMapsMost && maps[m] != nullptr; ++m)
        {
            if (nameHash(maps[m]) != map)
                continue;
            // What it is said to come to, worked out the first time it is asked for.
            if (demoCrc_[m] == 0 && demoLength_[m] != 0)
            {
                const std::string air = std::string("air/") + maps[m];
                demoCrc_[m] = demoStreamCrc(global.dataPath(air + ".dsi.hgs"), global.dataPath(air + ".dsi.bank"));
            }
            demoTokens_[c] = match_.host.clients[c].token;
            // (What it is playing: whose textures it will ask for. All of it
            // held already: that is all it says.)
            if (event.a != 0)
                demoMapOf_[c] = static_cast<std::uint8_t>(m + 1);
            if (from == 0xFFFFFFFFu)
            {
                demoServer_.lost(demoTokens_[c]);
                return;
            }
            demoServer_.want(demoTokens_[c], c, map, event.a != 0, from, maps[m], demoLength_[m], demoCrc_[m],
                             static_cast<std::uint16_t>(m + 1));
            return;
        }
    }

    void NetGame::demoClose(int stream)
    {
        if (demoFile_[stream] != nullptr)
            std::fclose(demoFile_[stream]);
        if (demoBank_[stream] != nullptr)
            std::fclose(demoBank_[stream]);
        demoFile_[stream] = demoBank_[stream] = nullptr;
        demoFileMap_[stream] = 0;
        demoFileGuest_[stream] = 0;
    }

    bool NetGame::demoRead(int stream, std::uint32_t at, std::uint8_t *to, int count)
    {
        // The first file; the gap to the bank, nothing; then the bank.
        const std::uint32_t guest = demoFileGuest_[stream];
        const std::uint32_t bankAt = demoBankAt(guest);
        while (count > 0)
        {
            std::FILE *file = nullptr;
            std::uint32_t inFile = 0, left = 0;
            if (at < guest)
            {
                file = demoFile_[stream];
                inFile = at;
                left = guest - at;
            }
            else if (at < bankAt || demoBank_[stream] == nullptr)
            {
                if (demoBank_[stream] == nullptr)
                    return false; // (past the end of a track)
                const std::uint32_t gap = bankAt - at;
                const int zeros = static_cast<int>(gap) < count ? static_cast<int>(gap) : count;
                std::memset(to, 0, static_cast<std::size_t>(zeros));
                to += zeros;
                at += static_cast<std::uint32_t>(zeros);
                count -= zeros;
                continue;
            }
            else
            {
                file = demoBank_[stream];
                inFile = at - bankAt;
                left = 0x7FFFFFFF;
            }
            const int step = static_cast<int>(left) < count ? static_cast<int>(left) : count;
            if (std::fseek(file, static_cast<long>(inFile), SEEK_SET) != 0 ||
                std::fread(to, 1, static_cast<std::size_t>(step), file) != static_cast<std::size_t>(step))
                return false;
            to += step;
            at += static_cast<std::uint32_t>(step);
            count -= step;
        }
        return true;
    }

    int NetGame::demoFrame()
    {
        // Who has gone is kept for when they come back, and not sent to.
        const unsigned present = netlink::clients();
        for (int c = 1; c < kNetPlayers; ++c)
        {
            if (!((present >> c) & 1) && demoTokens_[c] != 0)
            {
                demoServer_.lost(demoTokens_[c]);
                demoTokens_[c] = 0;
                demoMapOf_[c] = 0;
            }
        }
        demoServer_.frame();
        if (air_.sending())
            return 0; // (the lobby's frames send it)
        int sent = 0;
        demoLiveNow_ = false;
        std::uint8_t piece[kAirPiece];
        std::uint8_t bytes[kNetCmdBytes];
        int stream;
        std::uint32_t index;
        bool textures = false;
        for (const FetchHost::Sending &sending : fetch_.sending)
            textures = textures || sending.on;
        for (int n = 0; n < kAirServeFrames && demoServer_.next(stream, index); ++n)
        {
            // (What is only being got ready waits for what is being looked at.)
            const bool live = demoServer_.streamIsLive(stream);
            if (textures && !live)
                break;
            demoLiveNow_ = demoLiveNow_ || live;
            const AirSender &air = demoServer_.streams[stream].air;
            if (demoFileMap_[stream] != demoServer_.streams[stream].map)
            {
                demoClose(stream);
                demoFileMap_[stream] = demoServer_.streams[stream].map;
                if (air.stream == AirStream::Music)
                {
                    const int track = (air.name[5] - '0') * 10 + (air.name[6] - '0');
                    demoFile_[stream] = std::fopen(demoTrackPath(track).c_str(), "rb");
                    demoFileGuest_[stream] = air.length;
                }
                else
                {
                    const std::string path = global.dataPath(std::string("air/") + air.name);
                    demoFile_[stream] = std::fopen((path + ".dsi.hgs").c_str(), "rb");
                    demoBank_[stream] = std::fopen((path + ".dsi.bank").c_str(), "rb");
                    demoFileGuest_[stream] = demoFileLength(path + ".dsi.hgs");
                    if (demoBank_[stream] == nullptr)
                        demoClose(stream);
                }
            }
            if (demoFile_[stream] == nullptr)
                break;
            const int count = air.pieceBytes(index);
            if (!demoRead(stream, index * kAirPiece, piece, count))
                break;
            const int size = air.piece(index, piece, count, bytes);
            if (size > 0)
            {
                netlink::send(bytes, size);
                ++sent_;
                ++sent;
            }
        }
        return sent;
    }
#else
    void NetGame::demoHello(bool letGo)
    {
        std::uint8_t mac[6];
        netlink::address(mac);
        netSay(match_, netEvent(NetEventType::DemoHello, letGo ? 1 : 0, static_cast<std::int16_t>(mac[2] | mac[3] << 8),
                                static_cast<std::int16_t>(mac[4] | mac[5] << 8)));
    }

    void NetGame::demoFile(std::uint32_t what)
    {
        const bool textures = takingOn_ && (taking_.from == FetchFrom::Map || taking_.from == FetchFrom::MapFull);
        const bool file = filePath_[0] != '\0';
        if (textures && !file)
            textureStream.fetchFailed(false);
        filePath_[0] = '\0';
        fetchAsk(FetchFrom::DemoSave, what);
    }

    void NetGame::demoAsk(std::uint32_t map, bool now, std::uint32_t from, int track)
    {
        netSay(match_, netEvent(NetEventType::DemoWant, now ? 1 : 0, static_cast<std::int16_t>(map & 0xFFFF),
                                static_cast<std::int16_t>(map >> 16), static_cast<std::int16_t>(from & 0xFFFF),
                                static_cast<std::int16_t>(from >> 16), track));
    }
#endif

    bool NetGame::votePage() const
    {
        return match_.vote.count > 1 && match_.vote.open;
    }

    bool NetGame::voteOn() const
    {
        return !match_.playing && match_.vote.count > 1;
    }

    void NetGame::voteRedrawn()
    {
        for (int &drawn : voteRowDrawn_)
            drawn = -1;
    }

    int NetGame::voteLastsMs() const
    {
        return match_.rules.voteSeconds * 1000;
    }

    int NetGame::voteSecondsLeft() const
    {
        const int left = voteOpenedAt_ + voteLastsMs() - voteMs_;
        return left > 0 ? (left + 999) / 1000 : 0;
    }

    void NetGame::voteLetGo()
    {
        // (Its pictures' colours are not for what comes next on those cells.)
        touchCanvas.listOff();
        if (voteShown_)
            touchCanvas.untintAll();
        voteShown_ = false;
        for (int n = 0; n < NetVotePictures::kPlaces; ++n)
        {
            std::free(voteData_[n]);
            voteData_[n] = nullptr;
            voteBytes_[n] = 0;
        }
        votePictures_ = NetVotePictures();
        voteNone_ = 0;
        voteRedrawn();
        voteSent_ = 0;
        voteMs_ = voteOpenedAt_ = 0;
    }

    void NetGame::voteKeep(int map, std::uint8_t *data, std::uint32_t bytes)
    {
        int shown[kNetVoteRows];
        for (int r = 0; r < kNetVoteRows; ++r)
            shown[r] = bottomScreen.voteRowMap(r);
        const int place = votePictures_.room(shown, kNetVoteRows);
        if (place < 0 || votePictures_.find(map) >= 0)
        {
            std::free(data);
            return;
        }
        std::free(voteData_[place]);
        voteData_[place] = data;
        voteBytes_[place] = bytes;
        votePictures_.slot[place] = static_cast<std::int8_t>(map);
        votePictures_.touch(place);
    }

    void NetGame::voteFrame(int ms)
    {
        NetVote &vote = match_.vote;
        if (vote.count <= 1)
            return;
        voteMs_ += ms;
        if (!isHost() && !vote.open)
            voteOpenedAt_ = voteMs_;
        // The pictures of what is on the screen, and of what is a slide away:
        // asked for until they come, or the host says there is none.
        if (vote.open && voteShown_)
        {
            int shown[kNetVoteRows];
            std::uint32_t known = voteNone_;
            for (int r = 0; r < kNetVoteRows; ++r)
                shown[r] = bottomScreen.voteRowMap(r);
            for (int n = 0; n < NetVotePictures::kPlaces; ++n)
                known |= votePictures_.slot[n] >= 0 ? 1u << votePictures_.slot[n] : 0u;
            const int wanted = netVoteWanted(shown, kNetVoteRows, bottomScreen.voteTop(), vote.count, known);
            if (wanted >= 0)
            {
#ifndef HLDS_CHILD
                if (isHost())
                {
                    FetchHost::Asked asked;
                    asked.from = FetchFrom::VotePicture;
                    asked.what = static_cast<std::uint32_t>(wanted);
                    std::uint8_t *data = nullptr;
                    std::uint32_t packed = 0, raw = 0, name = 0;
                    if (fetchFind(asked, data, packed, raw, name))
                        voteKeep(wanted, data, raw > kAirNameBytes ? raw - kAirNameBytes : 0);
                    else
                        voteNone_ |= 1u << wanted;
                }
                else
#endif
                if (!fetchBusy())
                    fetchAsk(FetchFrom::VotePicture, static_cast<std::uint32_t>(wanted));
            }
            // What has come, on the screen: one a frame.
            for (int r = 0; r < kNetVoteRows; ++r)
            {
                const int map = shown[r];
                const int place = map >= 0 ? votePictures_.find(map) : -1;
                if (place >= 0 && voteRowDrawn_[r] != map)
                {
                    voteRowDrawn_[r] = map;
                    votePictures_.touch(place);
                    bottomScreen.voteCard(r, voteData_[place], voteBytes_[place]);
                    break;
                }
            }
        }
#ifndef HLDS_CHILD
        if (!isHost())
            return;
        // The maps' names, a few a frame: the last first, so that who hears
        // the first knows how many there are.
        if (voteSent_ == 0 && vote.chooser >= 0)
            netSay(match_, netEvent(NetEventType::NextTurn, vote.chooser));
        for (int n = 0; n < 4 && voteSent_ < vote.count; ++n, ++voteSent_)
        {
            const int s = vote.count - 1 - voteSent_;
            NetEvent offered = netEvent(NetEventType::NextMap, s);
            std::memcpy(offered.text, vote.names[s], kNetTextBytes - 1);
            netSay(match_, offered);
            if (vote.names[s][kNetTextBytes - 1] != 0)
            {
                NetEvent rest = netEvent(NetEventType::NextMap, s | kNetVoteNameRest);
                std::memcpy(rest.text, vote.names[s] + kNetTextBytes - 1, kNetTextBytes - 1);
                netSay(match_, rest);
            }
        }
        // Who has gone has no vote.
        for (int p = 0; p < kNetPlayers; ++p)
        {
            if (((vote.voters >> p) & 1) && !netHere(match_, p))
                hldata::detail::netSayTally(match_, vote.drop(p));
        }
        const int scores = match_.playing ? match_.rules.chatSeconds * 1000 : 0;
        if (!vote.open && voteMs_ >= scores && voteSent_ >= vote.count)
        {
            vote.open = true;
            ++vote.stamp;
            voteOpenedAt_ = voteMs_;
            netSay(match_, netEvent(NetEventType::NextOpen));
        }
        if (!vote.open)
            return;
        // (The one who chooses gone: a draw, at once.)
        if (vote.decided < 0 && (voteMs_ >= voteOpenedAt_ + voteLastsMs() || vote.allVoted() || vote.voters == 0))
        {
            decideVote();
            voteDecidedAt_ = voteMs_;
        }
        if (vote.decided >= 0 && voteMs_ >= voteDecidedAt_ + kNetVoteResultMs)
        {
            if (match_.playing)
                goBackToLobby();
            else
            {
                // (Nobody has a map to leave: on to it at once.)
                nextAt_ = Global::vblanks() - kNextWaitVblanks;
                vote = NetVote();
                voteLetGo();
            }
        }
#endif
    }

    void NetGame::vote(int slot)
    {
        netVotePick(match_, slot);
    }

#ifndef HLDS_CHILD
    void NetGame::goBackToLobby()
    {
        NetEvent over = netEvent(NetEventType::End);
        over.v[0] = 1;
        netSay(match_, over);
        lobbyAsked_ = true;
    }

    void NetGame::sendAllToLobby()
    {
        if (!isHost() || lobbyAsked_)
            return;
        nextMap_.clear();
        if (match_.over)
        {
            goBackToLobby();
            return;
        }
        // (A vote before the first match: given up.)
        if (voteOn())
        {
            netSay(match_, netEvent(NetEventType::NextMap, kNetVoteCancel));
            match_.vote = NetVote();
            voteLetGo();
        }
    }

    void NetGame::sayNext(const std::string &map, int slot)
    {
        nextMap_ = map;
        NetEvent said = netEvent(NetEventType::NextDecided, slot + 1);
        std::snprintf(said.text, sizeof(said.text), "%s", nextMap_.c_str());
        netSay(match_, said);
        std::snprintf(match_.nextName, sizeof(match_.nextName), "%s", map.c_str());
        ++match_.final.stamp;
    }

    void NetGame::decideVote()
    {
        const int slot = match_.vote.winner(match_.endDraw * 2654435761u + Global::vblanks());
        if (slot < 0)
            return;
        match_.vote.decided = static_cast<std::int8_t>(slot);
        ++match_.vote.stamp;
        sayNext(voteMaps_[slot], slot);
        std::snprintf(match_.nextName, sizeof(match_.nextName), "%s", voteMaps_[slot].c_str());
    }

    void NetGame::startFirstVote(int skill)
    {
        if (phase_ != Phase::Hosting || voteOn())
            return;
        pendingSkill_ = skill;
        nextMap_.clear();
        match_.endDraw = static_cast<std::uint16_t>(Global::vblanks());
        pendingMap_.clear();
        startVote(-1);
        // (Only the one map it could be: at once.)
        nextAt_ = Global::vblanks() - kNextWaitVblanks;
    }

    void NetGame::startVote(int chooser)
    {
        voteLetGo();
        int candidates[64];
        const int count = netNextCandidates(mapInfo_, nameHash(pendingMap_.c_str()), guestsOffered(), guestRoom(),
                                            candidates, 64);
        if (count == 0)
        {
            nextNone_ = true;
            return;
        }
        // (Nothing to choose between: that one.)
        if (count == 1)
        {
            sayNext(mapInfo_[static_cast<std::size_t>(candidates[0])].name, -1);
            return;
        }
        NetVote &vote = match_.vote;
        vote = NetVote();
        // (Every map there is -- more than it can hold, as many drawn at random.)
        vote.count = static_cast<std::uint8_t>(count <= NetVote::kMost
                                                   ? count
                                                   : netDrawSome(candidates, count, match_.endDraw * 40503u + 1u, NetVote::kMost));
        vote.chooser = static_cast<std::int8_t>(chooser);
        for (int p = 0; p < kNetPlayers; ++p)
            vote.voters = static_cast<std::uint8_t>(vote.voters | (netHere(match_, p) && (chooser < 0 || p == chooser) ? 1u << p : 0u));
        for (int s = 0; s < vote.count; ++s)
        {
            voteMaps_[s] = mapInfo_[static_cast<std::size_t>(candidates[s])].name;
            std::snprintf(vote.names[s], sizeof(vote.names[s]), "%s", voteMaps_[s].c_str());
        }
        ++vote.stamp;
    }

    void NetGame::chooseNext()
    {
        nextChosen_ = true;
        nextMap_.clear();
        // (Who comes now waits, and plays the next one.)
        netlink::admit(true);
        // (Alone, there is nobody to vote with or to take turns with: the lobby.)
        int here = 0;
        for (int p = 0; p < kNetPlayers; ++p)
            here += netHere(match_, p) ? 1 : 0;
        if (here <= 1 && match_.rules.nextMode != kNextRotation)
            return;
        if (match_.rules.nextMode == kNextVote)
        {
            startVote(-1);
            return;
        }
        if (match_.rules.nextMode == kNextWinner)
        {
            startVote(netWinnerChooser(match_.final, match_.rules.teams()));
            return;
        }
        if (match_.rules.nextMode == kNextTurns)
        {
            unsigned here = 0;
            for (int p = 0; p < kNetPlayers; ++p)
                here |= netHere(match_, p) ? 1u << p : 0u;
            turn_ = netNextSeat(turn_, here);
            startVote(turn_);
            return;
        }
        if (match_.rules.nextMode != kNextRotation)
            return;
        const int at = netNextRotation(mapInfo_, settings.rotation, settings.rotationCount, nameHash(pendingMap_.c_str()),
                                       guestsOffered(), guestRoom());
        if (at < 0)
        {
            nextNone_ = true;
            return;
        }
        sayNext(mapInfo_[static_cast<std::size_t>(at)].name, -1);
    }

    bool NetGame::nextDue(std::string &map)
    {
        if (phase_ != Phase::Hosting || nextMap_.empty() || Global::vblanks() - nextAt_ < kNextWaitVblanks)
            return false;
        map = nextMap_;
        return true;
    }

    void NetGame::startNext(const std::string &map, int fragLimit, int minuteLimit)
    {
        match_.vote = NetVote();
        voteLetGo();
        start(map, fragLimit, minuteLimit, pendingSkill_);
        nextMap_.clear();
    }

    void NetGame::dropNext()
    {
        nextMap_.clear();
        hostChanged();
    }

    bool NetGame::nextNone()
    {
        const bool was = nextNone_;
        nextNone_ = false;
        return was;
    }
#endif

    bool NetGame::lobbyAsked()
    {
        if (phase_ != Phase::Playing || !lobbyAsked_)
            return false;
        lobbyAsked_ = false;
        overMs_ = 0;
        // (Asked once: the next is the next match's.)
        match_.over = false;
        return true;
    }

    void NetGame::toLobby()
    {
        if (phase_ != Phase::Playing)
            return;
        effectDraw.dropSprays();
        starting_ = goAsked_ = cancelled_ = lobbyAsked_ = false;
        match_.vote = NetVote();
        voteLetGo();
        match_.playing = false;
        match_.over = false;
        match_.firstActor = -1;
        match_.passedCount = 0;
        phase_ = isHost() ? Phase::Hosting : Phase::Lobby;
        // (The pictures asked for again: a console sent the game let go of
        // them for the match.)
        pictureNext_ = 0;
        // (The host takes newcomers again, between matches.)
        if (isHost())
            netlink::admit(true);
#ifndef HLDS_CHILD
        nextChosen_ = false;
        nextAt_ = Global::vblanks();
#endif
    }

    bool NetGame::spray()
    {
        if (phase_ != Phase::Playing)
            return false;
        if (!netSpray(match_, game.logic, player.state, camera.pos, camera.forward))
            return false;
        sound.ui(kUiSoundSpray, 127);
        return true;
    }

    void NetGame::used(int index)
    {
        if (phase_ == Phase::Playing)
            gameApi.netUsed(match_, index);
    }

    bool NetGame::notice(char *line, int room)
    {
        if (match_.warnDue)
        {
            match_.warnDue = false;
            sound.matchWarning();
            std::snprintf(line, static_cast<std::size_t>(room), "1 MINUTE REMAINING");
            return true;
        }
        if (match_.noticeCount == 0)
            return false;
        const NetNotice notice = match_.notices[0];
        for (int n = 1; n < match_.noticeCount; ++n)
            match_.notices[n - 1] = match_.notices[n];
        --match_.noticeCount;
        auto name = [this](int p) { return p < kNetPlayers && match_.names[p][0] ? match_.names[p] : "Someone"; };
        switch (notice.type)
        {
        case NetEventType::Died:
            if (notice.other < kNetPlayers && notice.other != notice.who)
                netKillLine(line, room, name(notice.other), name(notice.who), notice.weapon);
            else
                std::snprintf(line, static_cast<std::size_t>(room), "%s died", name(notice.who));
            break;
        case NetEventType::Hello:
            std::snprintf(line, static_cast<std::size_t>(room), "%s is here", name(notice.who));
            break;
        case NetEventType::Bye:
            std::snprintf(line, static_cast<std::size_t>(room), "A player has left");
            break;
        case NetEventType::End:
            std::snprintf(line, static_cast<std::size_t>(room), "THE MATCH IS OVER");
            break;
        default:
            line[0] = 0;
            break;
        }
        return true;
    }

    const char *NetGame::ended()
    {
        const char *why = ended_;
        ended_ = "";
        return why;
    }

    void NetGame::status(char *line, int room) const
    {
        std::snprintf(line, static_cast<std::size_t>(room), "net%d me%d o%lu i%lu x%u q%d",
                      static_cast<int>(phase_), match_.me, static_cast<unsigned long>(sent_),
                      static_cast<unsigned long>(heard_), netlink::dropped(),
                      isHost() ? match_.host.out.count : match_.client.out.count);
    }
} // namespace hlrt
