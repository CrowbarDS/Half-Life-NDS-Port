// (A console that was sent the game only: HLDS_CHILD.)
#ifdef HLDS_CHILD
#include "net/GuestLoad.hpp"

#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>

#include <nds.h>

#include "Global.hpp"
#include "HlLevelReader.hpp"
#include "net/AirDevice.hpp"
#include "net/NetGame.hpp"
#include "net/NetLink.hpp"

namespace hlrt::guest
{
    namespace
    {
        struct Kept
        {
            char name[hldata::kGuestNameMost + 1];
            std::uint8_t *data;
            std::uint32_t size;
        };
        constexpr int kBaseMost = 160;
        Kept base[kBaseMost];
        int baseCount = 0;

        struct Offer
        {
            std::string map;
            const std::uint8_t *data = nullptr;
            std::uint32_t size = 0;
            std::string path; ///< or, the file on a card it is in
            bool air = false; ///< or, the host is sending it
            bool arena = false; ///< or, it is coming to the demo's memory
        };
        constexpr int kOffersMost = 4;
        Offer offers[kOffersMost];
        int offerCount = 0;

        // The map's stream, while it is being read: one at a time.
        MemorySupply mapSupply;
        FileSupply mapFile;
        hldata::GuestCursor *mapCursor = nullptr;
        alignas(4) std::uint8_t cursorRoom[sizeof(hldata::GuestCursor)];
        Held heldNow;

        // The stream being taken from the air: its pieces in a window of
        // their own, on the heap only while one is (38 KB).
        hldata::AirReceiver receiver;
        bool airLost = false;
        bool mapOnAir = false; ///< the map's cursor reads from the air
        char heldBase[hldata::kAirNameBytes] = {};
        /// The map last taken whole (to say so, if the host is still
        /// sending it to others), and its sending.
        char takenMap[hldata::kAirNameBytes] = {};
        std::uint16_t takenCharacter = 0;
        bool takenFull = false; ///< (a console in DSi mode) the match's map came at full quality
        std::uint16_t takenServe = 0;
        std::uint32_t takenLength = 0;
        std::uint8_t takenSkill = 2;
        /// How long (vblanks) a guest waits for the host to say anything.
        constexpr std::uint32_t kAirQuietVblanks = 600;
        constexpr std::uint32_t kAirStoppedVblanks = 240;
        std::uint32_t pumpedAt = 0; ///< when the radio was last listened to
        /// ... and how many times it says it has all of a stream once it
        /// has, before it goes back to the game's frames (one lost answer
        /// would otherwise leave the host waiting for it).
        constexpr int kAirLingerVblanks = 30;

        /// When the stream was last heard of, and when the host was last
        /// answered.
        volatile std::uint32_t quietSince = 0;
        std::uint32_t askedAt = 0;
        void (*mapProgress)(int) = nullptr;
        std::uint32_t streamSince = 0; ///< (when the stream being taken began)

        /// (The receiver is the interrupt's as well as this code's -- sinkTake
        /// -- so what is done to it here is done with interrupts held.)
        struct Hold
        {
            int ime = enterCriticalSection();
            ~Hold() { leaveCriticalSection(ime); }
        };

        volatile bool heardInIrq = false; ///< a piece taken by sinkTake since pump last looked

        /// What the radio's interrupt does with a frame of the host's: a
        /// piece of the stream being taken goes where it belongs, at once
        /// (a loader busy for a few frames would otherwise see the radio's
        /// ring overflow, and the piece sent again). Anything else -- the
        /// game's, another stream's, this one's before the host has been
        /// heard sending it -- to the ring, for whoever reads it.
        bool sinkTake(const std::uint8_t *bytes, int size)
        {
            if (receiver.ring == nullptr || !receiver.known || size < 4 || bytes[3] != hldata::kNetFrameAirData)
                return false;
            hldata::AirHead head;
            if (!hldata::airRead(bytes, size, head) || head.stream != receiver.stream || head.serve != receiver.serve)
                return false;
            receiver.frame(head);
            quietSince = Global::vblanks();
            heardInIrq = true;
            return true;
        }

        bool ringOn(hldata::AirStream stream, const char *name)
        {
            streamSince = Global::vblanks();
            auto *ring = static_cast<std::uint8_t *>(std::malloc(hldata::kAirWindow * hldata::kAirPiece));
            std::uint8_t *was;
            {
                const Hold hold;
                was = receiver.direct ? nullptr : receiver.ring;
                receiver.want(stream, name, ring);
            }
            std::free(was);
            airLost = false;
            netlink::setSink(sinkTake);
            return ring != nullptr;
        }
        void ringOff()
        {
            std::uint8_t *was;
            {
                const Hold hold;
                was = receiver.direct ? nullptr : receiver.ring;
                receiver.ring = nullptr;
            }
            std::free(was);
        }

        /// The host's frames, as many as have come: the stream's pieces
        /// kept, the rest let go (the game's: they are sent again). An
        /// answer once a vblank. False once the host has been quiet too long.
        bool pump()
        {
            // (Quiet is counted only while listening: a console busy elsewhere
            // -- a map's game being set up -- has its window full, and the
            // host has nothing to send it until it reads on.)
            const std::uint32_t now = Global::vblanks();
            if (now - pumpedAt > 2)
            {
                // (No later than now: heard while busy, it is sooner than the gap.)
                const std::uint32_t since = now - quietSince, busy = now - pumpedAt;
                quietSince = since > busy ? quietSince + busy : now;
            }
            pumpedAt = now;
            std::uint8_t frame[hldata::kNetCmdBytes];
            int from = 0;
            bool heard = false;
            for (int size; (size = netlink::take(frame, from)) > 0;)
            {
                heard = true;
                hldata::AirHead head;
                const Hold hold;
                if (hldata::airRead(frame, size, head) && receiver.frame(head))
                    quietSince = Global::vblanks();
            }
            heard = heard || heardInIrq;
            heardInIrq = false;
            // (An answer for a frame of the host's that has come, the latest
            // there is: answers sent with nothing to answer wait in a queue,
            // and what reaches the host is then old news.)
            if (heard && askedAt != Global::vblanks())
            {
                askedAt = Global::vblanks();
                std::uint8_t ask[hldata::kNetReplyBytes];
                int size;
                {
                    const Hold hold;
                    size = receiver.ask(ask);
                }
                if (size > 0)
                    netlink::send(ask, size);
            }
            // (Quiet too long -- or the host heard, but no longer sending: it
            // gave up, and said so in what is not read here.)
            const std::uint32_t quiet = Global::vblanks() - quietSince;
            if (quiet > kAirQuietVblanks || (heard && receiver.known && quiet > kAirStoppedVblanks))
                airLost = true;
            return !airLost;
        }
        /// It is all here: said for a while.
        void linger()
        {
            for (int n = 0; n < kAirLingerVblanks; ++n)
            {
                pump();
                swiWaitForVBlank();
            }
        }

        /// The stream from the air, for a GuestCursor: waits (the radio
        /// heard all the while) for the next piece.
        struct AirSupply : hldata::GuestSupply
        {
            void (*progress)(int) = nullptr;
            int shown = -1;
            std::uint32_t shownAt = 0;
            bool more(const std::uint8_t *&out, std::size_t &length) override
            {
                // (Heard and answered once a vblank as the loaders read, not
                // only when they are waiting: a loader that has a window's
                // worth to go through would otherwise say nothing meanwhile,
                // and be left out.)
                if (askedAt != Global::vblanks() && !pump())
                    return false;
                while (true)
                {
                    // (How far, said as it changes -- no more than ten times
                    // a second, and the end always: the page is drawn again
                    // each time, and that is not free.)
                    const int percent = receiver.percent();
                    if (progress != nullptr && percent != shown &&
                        (shown < 0 || percent >= 100 || Global::vblanks() - shownAt >= 6))
                    {
                        shownAt = Global::vblanks();
                        progress(shown = percent);
                    }
                    bool got;
                    {
                        const Hold hold;
                        got = receiver.next(out, length);
                    }
                    if (got)
                        return true;
                    if (receiver.done() || !pump())
                        return false;
                    swiWaitForVBlank();
                }
            }
        };
        AirSupply airSupply;

        // A demo (HlDemo.hpp): one block, had once before its first map and
        // kept until it ends. The stream comes to it, wherever the game is.
        constexpr std::uint32_t kDemoFolderBytes = 256 * 1024;
        constexpr std::uint32_t kDemoCrcBytes = 8 * 1024; ///< checked, of a stream come, as a frame goes
        constexpr std::uint32_t kDemoAskVblanks = 120;
        constexpr std::uint32_t kDemoGiveUpVblanks = 3600;
        /// What a full map needs beside its stream: its own blocks, and the
        /// textures that come as they are looked at.
        constexpr std::uint32_t kDemoPlayBytes = 5 * 1024 * 1024;
        constexpr int kDemoLinksMost = 8;
        std::uint8_t *demoRoom = nullptr;
        std::uint32_t demoRoomBytes = 0;
        bool demoOn = false;
        hldata::DemoArena demoArena;
        std::uint32_t demoMap = 0;    ///< the map played
        std::uint32_t demoTaking = 0; ///< the map the receiver is on
        std::uint32_t demoAskedAt = 0;
        std::uint32_t demoCrcAt = 0, demoCrcSoFar = 0;
        int demoSaidDone = 0;
        std::uint32_t demoLinks[kDemoLinksMost];
        int demoLinkCount = 0;
        bool demoLoadingNow = false;
        /// How far each place got, if it was left for something more urgent.
        std::uint32_t demoFrom[hldata::DemoArena::kPlaces] = {};
        std::uint32_t demoFromMap[hldata::DemoArena::kPlaces] = {};

        // What is on its way to the host: in the block's first part, one after
        // the other, the first of them going. No heap: this is during play.
        struct DemoOut
        {
            std::uint32_t what, offset, size;
        };
        constexpr int kDemoOutMost = 8;
        DemoOut demoOut[kDemoOutMost];
        int demoOutCount = 0;
        std::uint32_t demoOutEnd = 0;
        hldata::DemoUpSender demoUp;
        bool demoFlip = false;
        std::uint8_t *demoCame = nullptr;
        std::uint32_t demoCameSize = 0;
        bool demoCameDone = false;

        void demoOutNext()
        {
            if (demoOutCount > 0 && !demoUp.on)
                demoUp.begin(demoOut[0].what, demoRoom + demoOut[0].offset, demoOut[0].size);
        }

        const char *demoName(std::uint32_t map)
        {
            const char *const *maps = hldata::netDemoMaps(netGame.demo());
            for (int m = 0; m < hldata::kDemoMapsMost && maps[m] != nullptr; ++m)
            {
                if (hldata::nameHash(maps[m]) == map)
                    return maps[m];
            }
            return nullptr;
        }
        std::uint8_t *demoPlaceOf(std::uint32_t map)
        {
            const int place = demoArena.find(map);
            return place < 0 ? nullptr : demoRoom + demoArena.offsetOf(place);
        }
        /// The receiver on `map`, wanted now or for later, from as far as it got.
        void demoWant(std::uint32_t map, bool live)
        {
            const char *name = demoName(map);
            const int place = name == nullptr ? -1 : demoArena.take(map, 0, live ? hldata::DemoArena::Role::Current : hldata::DemoArena::Role::Next);
            if (place < 0)
                return;
            const int left = demoTaking != 0 ? demoArena.find(demoTaking) : -1;
            std::uint32_t from;
            {
                const Hold hold;
                if (left >= 0)
                {
                    demoFrom[left] = receiver.base;
                    demoFromMap[left] = demoTaking;
                }
                from = demoFromMap[place] == map ? demoFrom[place] : 0;
                receiver.wantDirect(hldata::AirStream::MapFull, name, demoRoom + demoArena.offsetOf(place), from);
            }
            demoTaking = map;
            streamSince = Global::vblanks();
            demoCrcAt = 0;
            demoCrcSoFar = hldata::kCrcStart;
            demoSaidDone = 0;
            quietSince = Global::vblanks();
            netlink::setSink(sinkTake);
            netGame.demoAsk(map, live, from);
            demoAskedAt = Global::vblanks();
        }

        /// The demo's map from its memory: what has come so far, and the radio
        /// heard while it waits for the rest.
        struct ArenaSupply : hldata::GuestSupply
        {
            void (*progress)(int) = nullptr;
            const std::uint8_t *data = nullptr;
            std::uint32_t map = 0;
            std::uint32_t at = 0;
            int shown = -1;
            std::uint32_t heardAt = 0;
            std::uint32_t since = 0;
            bool more(const std::uint8_t *&out, std::size_t &length) override
            {
                since = Global::vblanks();
                while (true)
                {
                    std::uint32_t come = 0, all = 0;
                    {
                        const Hold hold;
                        const int place = demoArena.find(map);
                        if (demoArena.isWhole(map))
                            come = all = demoArena.places[place].length;
                        else if (demoTaking == map && receiver.known)
                        {
                            all = receiver.length;
                            come = receiver.base * hldata::kAirPiece;
                        }
                    }
                    come = come > all ? all : come;
                    if (Global::vblanks() != heardAt)
                    {
                        heardAt = Global::vblanks();
                        netGame.frame();
                    }
                    if (!netGame.on() || (all != 0 && at >= all))
                        return false;
                    if (at < come)
                    {
                        out = data + at;
                        length = come - at < hldata::kAirPiece ? come - at : hldata::kAirPiece;
                        at += static_cast<std::uint32_t>(length);
                        const int percent = static_cast<int>(static_cast<std::uint64_t>(at) * 100 / all);
                        if (progress != nullptr && percent != shown)
                            progress(shown = percent);
                        return true;
                    }
                    if (Global::vblanks() - since > kDemoGiveUpVblanks)
                        return false;
                    swiWaitForVBlank();
                }
            }
        };
        ArenaSupply arenaSupply;

        /// Every member of a stream, each into a block of its own, handed to
        /// `keep` (which owns it from then on, or frees it). (Its cursor in
        /// the map's room, not on the stack: its window is 4 KB. No map's
        /// stream is being read while one of these is.)
        template <class Keep>
        bool receiveWhole(hldata::GuestSupply &supply, Keep keep)
        {
            endMap();
            hldata::GuestCursor &cursor = *new (cursorRoom) hldata::GuestCursor(supply);
            bool ok = cursor.start();
            while (ok && cursor.next())
            {
                const std::uint32_t size = cursor.member().rawSize;
                auto *block = static_cast<std::uint8_t *>(std::malloc(size > 0 ? size : 1));
                if (block == nullptr || cursor.read(block, size) != size)
                {
                    std::free(block);
                    ok = false;
                    break;
                }
                keep(cursor.name(), block, size);
            }
            ok = ok && cursor.ended();
            cursor.~GuestCursor();
            return ok;
        }
    } // namespace

    bool FileSupply::more(const std::uint8_t *&out, std::size_t &length)
    {
        if (file == nullptr)
            return false;
        length = std::fread(piece, 1, sizeof(piece), file);
        out = piece;
        return length > 0;
    }

    bool MemorySupply::more(const std::uint8_t *&out, std::size_t &length)
    {
        if (at >= size)
            return false;
        out = data + at;
        length = size - at < piece ? size - at : piece;
        at += static_cast<std::uint32_t>(length);
        return true;
    }

    bool receiveCommon(hldata::GuestSupply &supply)
    {
        return receiveWhole(supply, [](const char *name, std::uint8_t *block, std::uint32_t size)
                            {
                                if (!air::putOwned(name, block, size))
                                    std::free(block);
                            });
    }

    void dropCommon() { air::dropOwned(); }

    bool receiveBase(hldata::GuestSupply &supply)
    {
        return receiveWhole(supply, [](const char *name, std::uint8_t *block, std::uint32_t size)
                            {
                                if (baseCount == kBaseMost)
                                {
                                    std::free(block);
                                    return;
                                }
                                Kept &kept = base[baseCount++];
                                std::strncpy(kept.name, name, sizeof(kept.name) - 1);
                                kept.name[sizeof(kept.name) - 1] = '\0';
                                kept.data = block;
                                kept.size = size;
                                heldNow.baseBytes += size;
                            });
    }

    bool borrow(const char *name, const std::uint8_t *&data, std::uint32_t &size)
    {
        for (int i = 0; i < baseCount; ++i)
        {
            if (std::strcmp(base[i].name, name) == 0)
            {
                data = base[i].data;
                size = base[i].size;
                return true;
            }
        }
        return false;
    }

    void offerMap(const std::string &map, const std::uint8_t *data, std::uint32_t size)
    {
        if (offerCount < kOffersMost)
            offers[offerCount++] = Offer{map, data, size, std::string()};
    }

    void offerMapFile(const std::string &map, const std::string &path)
    {
        if (offerCount < kOffersMost)
            offers[offerCount++] = Offer{map, nullptr, 0, path};
    }

    bool offered(const std::string &map)
    {
        if (demoOn)
            return demoName(hldata::nameHash(map.c_str())) != nullptr;
        for (int i = 0; i < offerCount; ++i)
        {
            if (offers[i].map == map)
                return true;
        }
        return false;
    }

    int offeredCount() { return offerCount; }
    std::string offeredName(int index) { return index >= 0 && index < offerCount ? offers[index].map : std::string(); }

    bool beginMap(const std::string &map)
    {
        endMap();
        // (The last map's weapons, kept packed, go with it.)
        air::dropMapFiles();
        if (demoOn && offerCount > 0)
            offers[0].map = map;
        for (int i = 0; i < offerCount; ++i)
        {
            if (offers[i].map != map)
                continue;
            hldata::GuestSupply *supply = &mapSupply;
            if (offers[i].arena)
            {
                const std::uint32_t hash = hldata::nameHash(map.c_str());
                demoArena.changeTo(hash, 0);
                demoMap = hash;
                demoLinkCount = 0;
                demoLoadingNow = true;
                if (!demoArena.isWhole(hash))
                {
                    // (On its way for later already: now said to be wanted now.)
                    if (demoTaking == hash)
                    {
                        netGame.demoAsk(hash, true, receiver.base);
                        demoAskedAt = Global::vblanks();
                    }
                    else
                        demoWant(hash, true);
                }
                else
                    netGame.demoAsk(hash, true, 0xFFFFFFFFu); // (for whose textures)
                arenaSupply = ArenaSupply();
                arenaSupply.progress = mapProgress;
                arenaSupply.map = hash;
                arenaSupply.data = demoPlaceOf(hash);
                if (arenaSupply.data == nullptr)
                    return false;
                supply = &arenaSupply;
            }
            else if (offers[i].air)
            {
                // (Begun when the host's first frame of it was heard:
                // airFrame.)
                if ((receiver.stream != hldata::AirStream::Map && receiver.stream != hldata::AirStream::MapFull) ||
                    receiver.ring == nullptr ||
                    map != receiver.name)
                    return false;
                airSupply.progress = mapProgress;
                airSupply.shown = -1;
                supply = &airSupply;
                mapOnAir = true;
            }
            else if (!offers[i].path.empty())
            {
                mapFile.file = std::fopen(offers[i].path.c_str(), "rb");
                if (mapFile.file == nullptr)
                    return false;
                supply = &mapFile;
            }
            else
            {
                mapSupply = MemorySupply();
                mapSupply.data = offers[i].data;
                mapSupply.size = offers[i].size;
            }
            mapCursor = new (cursorRoom) hldata::GuestCursor(*supply);
            if (!mapCursor->start())
            {
                endMap();
                return false;
            }
            for (std::uint32_t &bytes : heldNow.mapBytes)
                bytes = 0;
            air::stream(mapCursor);
            return true;
        }
        return false;
    }

    void endMap()
    {
        air::drain();
        air::stream(nullptr);
        if (mapCursor != nullptr)
            mapCursor->~GuestCursor();
        mapCursor = nullptr;
        if (mapFile.file != nullptr)
            std::fclose(mapFile.file);
        mapFile.file = nullptr;
        // A map from the air: all of it here, which the host is told. (Not
        // one only begun to be heard: the map about to be read from it lets
        // go of whatever was mounted first.)
        if (mapOnAir && (receiver.stream == hldata::AirStream::Map || receiver.stream == hldata::AirStream::MapFull) &&
            receiver.ring != nullptr)
        {
            // (The last piece read let go of: then it is all handed on --
            // and what the cursor did not ask for, there being no more of
            // it, waited for, as receiveAir does. Left unheard, the host would
            // go on sending it for ever.)
            const std::uint8_t *rest;
            std::size_t size;
            while (!receiver.done() && airSupply.more(rest, size))
            {
            }
            if (receiver.done())
            {
                std::strncpy(takenMap, receiver.name, sizeof(takenMap) - 1);
                takenServe = receiver.serve;
                takenFull = receiver.stream == hldata::AirStream::MapFull;
                takenLength = receiver.length;
                takenSkill = receiver.skill;
                linger();
            }
            ringOff();
            receiver.stream = hldata::AirStream::None;
        }
        mapOnAir = false;
    }

    void airWant(hldata::AirStream stream)
    {
        ringOn(stream, "");
        quietSince = Global::vblanks();
    }

    bool airFrame(const std::uint8_t *bytes, int size, std::uint8_t *reply, int &replySize, hldata::AirHead &head)
    {
        replySize = 0;
        if (!hldata::airRead(bytes, size, head))
            return false;
        // A demo's map: where it belongs (the first heard of it, sinkTake from
        // then on), and never a match's to load.
        if (netGame.demo() != 0 && (head.stream == hldata::AirStream::Map || head.stream == hldata::AirStream::MapFull))
        {
            const Hold hold;
            if (receiver.direct && receiver.frame(head))
                quietSince = Global::vblanks();
            return false;
        }
        // At full quality: for a console in DSi mode alone. It has all it
        // needs of the map then, and the others' base and map go by.
        if (head.stream == hldata::AirStream::MapFull && !global.twlChild)
            return false;
        if (takenFull && (head.stream == hldata::AirStream::Base || head.stream == hldata::AirStream::Map))
            return false;
        if (receiver.stream == hldata::AirStream::MapFull && receiver.ring != nullptr &&
            (head.stream == hldata::AirStream::Base || head.stream == hldata::AirStream::Map))
            return false;
        if (head.stream == hldata::AirStream::MapFull && std::strcmp(head.name, takenMap) == 0 && head.serve == takenServe)
        {
            replySize = hldata::airHave(head.stream, head.serve, head.length, reply);
            return false;
        }
        // What is held already: said so, and nothing taken.
        if (head.stream == hldata::AirStream::Base && std::strcmp(head.name, heldBase) == 0)
        {
            replySize = hldata::airHave(head.stream, head.serve, head.length, reply);
            return false;
        }
        if (head.stream == hldata::AirStream::Map && std::strcmp(head.name, takenMap) == 0 && head.serve == takenServe)
        {
            replySize = hldata::airHave(head.stream, head.serve, head.length, reply);
            return false;
        }
        // Who another chose to be: taken only if there is room for it, twice
        // over for the moment it is read -- else said to be held, and they
        // stay the suit here. (Taken already: the same.) At its finest, for a
        // console in DSi mode alone; the others', once that has it, go by.
        if (head.stream == hldata::AirStream::CharacterFull && !global.twlChild)
            return false;
        if (head.stream == hldata::AirStream::Character && takenFull)
            return false;
        if (head.stream == hldata::AirStream::Character || head.stream == hldata::AirStream::CharacterFull)
        {
            if (head.serve == takenCharacter || global.heapHeadroom() < head.length * 2 + hldata::kGuestPlayBytes)
            {
                replySize = hldata::airHave(head.stream, head.serve, head.length, reply);
                return false;
            }
        }
        // (The common stream, to a console that came later: this one has it.)
        else if (head.stream != hldata::AirStream::Base && head.stream != hldata::AirStream::Map &&
                 head.stream != hldata::AirStream::MapFull)
            return false;
        if (!ringOn(head.stream, head.name))
            return false;
        {
            const Hold hold;
            receiver.frame(head);
        }
        quietSince = Global::vblanks();
        return true;
    }

    bool receiveAir(void (*progress)(int percent))
    {
        if (receiver.ring == nullptr)
            return false;
        airSupply.progress = progress;
        airSupply.shown = -1;
        bool ok = false;
        if (receiver.stream == hldata::AirStream::Common)
            ok = receiveCommon(airSupply);
        else if (receiver.stream == hldata::AirStream::Character || receiver.stream == hldata::AirStream::CharacterFull)
        {
            takenCharacter = receiver.serve;
            ok = receiveWhole(airSupply, [](const char *name, std::uint8_t *block, std::uint32_t size)
                              {
                                  if (!air::putMapFile(name, block, size))
                                      std::free(block);
                              });
        }
        else if (receiver.stream == hldata::AirStream::Base)
        {
            // (One base at a time: the last group's goes.)
            for (int i = 0; i < baseCount; ++i)
                std::free(base[i].data);
            baseCount = 0;
            heldBase[0] = '\0';
            heldNow.baseBytes = 0;
            ok = receiveBase(airSupply);
            if (ok)
                std::strncpy(heldBase, receiver.name, sizeof(heldBase) - 1);
        }
        // (What the cursor did not ask for, there being no more of it.)
        const std::uint8_t *rest;
        std::size_t size;
        while (ok && !receiver.done() && airSupply.more(rest, size))
        {
        }
        ok = ok && receiver.done();
        if (ok)
            linger();
        ringOff();
        if (ok)
            receiver.stream = hldata::AirStream::None;
        return ok;
    }

    bool demoBegin(std::uint32_t longest, const char *map)
    {
        // (This one and the next, if there is the room: else only this one,
        // and the next comes as it is gone to. Kept once had: a block this
        // size let go of may not be had again, and it is wanted again by a
        // console that comes back.)
        for (std::uint32_t count = 2; count >= 1 && !demoOn; --count)
        {
            const std::uint32_t bytes = kDemoFolderBytes + count * longest;
            if (demoRoom != nullptr && demoRoomBytes >= bytes)
            {
                demoArena.plan(demoRoomBytes, kDemoFolderBytes, longest);
                demoOn = true;
                break;
            }
            std::free(demoRoom);
            demoRoom = nullptr;
            demoRoomBytes = 0;
            // (What was let go of counts as well as what was never had.)
            const struct mallinfo info = mallinfo();
            if (global.heapHeadroom() + info.fordblks < bytes + kDemoPlayBytes)
                continue;
            demoRoom = static_cast<std::uint8_t *>(std::malloc(bytes));
            if (demoRoom != nullptr)
            {
                demoRoomBytes = bytes;
                demoArena.plan(bytes, kDemoFolderBytes, longest);
                demoOn = true;
            }
        }
        if (!demoOn)
            return false;
        for (std::uint32_t &at : demoFromMap)
            at = 0;
        demoUp = hldata::DemoUpSender();
        demoOutCount = 0;
        demoOutEnd = 0;
        demoTaking = demoMap = 0;
        demoLinkCount = 0;
        offerCount = 0;
        offers[offerCount++] = Offer{map, nullptr, 0, std::string(), false, true};
        return true;
    }

    void demoEnd()
    {
        if (!demoOn)
            return;
        {
            const Hold hold;
            receiver = hldata::AirReceiver();
        }
        demoOn = false;
        demoUp = hldata::DemoUpSender();
        demoOutCount = 0;
        demoOutEnd = 0;
        demoArena = hldata::DemoArena();
        demoTaking = demoMap = 0;
        demoLinkCount = 0;
        demoLoadingNow = false;
        offerCount = 0;
    }

    bool demoPlaying() { return demoOn; }

    std::uint8_t *demoSendRoom(std::uint32_t size)
    {
        if (!demoOn || size > kDemoFolderBytes)
            return nullptr;
        // (All sent: from the beginning again. Too full: what is going goes.)
        const std::uint32_t since = Global::vblanks();
        while (demoOutCount > 0 && demoOutEnd + size > kDemoFolderBytes)
        {
            if (Global::vblanks() - since > 30 * 60 || !netGame.on())
                return nullptr;
            netGame.frame();
            swiWaitForVBlank();
        }
        if (demoOutCount == 0)
            demoOutEnd = 0;
        return demoRoom + demoOutEnd;
    }

    void demoSendReady(std::uint32_t what, std::uint8_t *at, std::uint32_t size)
    {
        // (A later one of what has not begun to go: only the later is sent.)
        for (int n = 1; n < demoOutCount; ++n)
        {
            if (demoOut[n].what == what)
            {
                for (int m = n; m + 1 < demoOutCount; ++m)
                    demoOut[m] = demoOut[m + 1];
                --demoOutCount;
                break;
            }
        }
        if (demoOutCount == kDemoOutMost)
            return;
        demoOut[demoOutCount++] = DemoOut{what, static_cast<std::uint32_t>(at - demoRoom), size};
        demoOutEnd = static_cast<std::uint32_t>(at - demoRoom) + size;
        demoOutNext();
    }

    void demoAck(const std::uint8_t *bytes, int size)
    {
        if (demoUp.heard(bytes, size, netGame.match().me))
        {
            for (int m = 0; m + 1 < demoOutCount; ++m)
                demoOut[m] = demoOut[m + 1];
            --demoOutCount;
            demoOutNext();
        }
    }

    void demoSent()
    {
        const std::uint32_t since = Global::vblanks();
        while (demoOutCount > 0 && netGame.on() && Global::vblanks() - since < 10 * 60)
        {
            netGame.frame();
            swiWaitForVBlank();
        }
    }

    void demoFileCame(std::uint8_t *data, std::uint32_t size)
    {
        demoCame = data;
        demoCameSize = size;
        demoCameDone = true;
    }

    bool demoFetch(std::uint32_t what, std::uint8_t *&data, std::uint32_t &size)
    {
        data = nullptr;
        size = 0;
        const std::uint32_t since = Global::vblanks();
        const auto waited = [&]() { return Global::vblanks() - since > 30 * 60 || !netGame.on(); };
        while (demoOutCount > 0)
        {
            if (waited())
                return false;
            netGame.frame();
            swiWaitForVBlank();
        }
        demoCameDone = false;
        netGame.demoFile(what);
        while (!demoCameDone)
        {
            if (waited())
                return false;
            netGame.frame();
            swiWaitForVBlank();
        }
        data = demoCame;
        size = demoCameSize;
        demoCame = nullptr;
        return data != nullptr;
    }

    bool demoMapHere() { return demoArena.isWhole(demoMap) || (demoTaking == demoMap && receiver.done()); }

    bool demoLoading() { return demoLoadingNow; }

    void demoLoaded(const hldata::LevelView &view)
    {
        demoLoadingNow = false;
        // Where its ways out lead, those further on in the demo first.
        const char *const *maps = hldata::netDemoMaps(netGame.demo());
        int here = 0;
        for (int m = 0; m < hldata::kDemoMapsMost && maps[m] != nullptr; ++m)
            here = hldata::nameHash(maps[m]) == demoMap ? m : here;
        demoLinkCount = 0;
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int m = 0; m < hldata::kDemoMapsMost && maps[m] != nullptr; ++m)
            {
                if ((pass == 0) != (m > here) || m == here)
                    continue;
                const std::uint32_t hash = hldata::nameHash(maps[m]);
                for (std::uint32_t i = 0; i < view.logicCount && demoLinkCount < kDemoLinksMost; ++i)
                {
                    if (static_cast<hldata::LogicKind>(view.logic[i].kind) == hldata::LogicKind::ChangeLevel &&
                        hldata::nameHash(view.strings + view.logic[i].extra[0]) == hash)
                    {
                        demoLinks[demoLinkCount++] = hash;
                        break;
                    }
                }
            }
        }
    }

    int demoAnswer(std::uint8_t *reply)
    {
        if (!demoOn)
            return 0;
        // (Its progress, in turn with how the map is coming.)
        demoFlip = !demoFlip;
        if (demoUp.on && (demoFlip || demoTaking == 0))
        {
            const int size = demoUp.frame(reply, hldata::kNetReplyBytes);
            if (size > 0)
                return size;
        }
        if (demoTaking == 0)
            return 0;
        bool done;
        {
            const Hold hold;
            done = receiver.done();
        }
        if (!done)
        {
            // (Nothing coming: the host has not heard, or has lost track of
            // this console, and hears again.)
            const std::uint32_t now = Global::vblanks();
            if (now - quietSince >= kDemoAskVblanks && now - demoAskedAt >= kDemoAskVblanks)
            {
                demoAskedAt = now;
                netGame.demoAsk(demoTaking, demoTaking == demoMap, receiver.base);
                return 0;
            }
            // (An answer for a frame that has come, as pump's: said with
            // nothing to answer, it waits in a queue and reaches the host as
            // old news, and what has come is sent again.)
            if (!heardInIrq)
                return 0;
            heardInIrq = false;
            const Hold hold;
            return receiver.known ? receiver.ask(reply) : 0;
        }
        // All here: is it what was sent? A little at a time: the game is on.
        if (!demoArena.isWhole(demoTaking))
        {
            const std::uint8_t *data = demoPlaceOf(demoTaking);
            const std::uint32_t rest = receiver.length - demoCrcAt;
            const std::uint32_t step = rest < kDemoCrcBytes ? rest : kDemoCrcBytes;
            demoCrcSoFar = hldata::crc32Update(demoCrcSoFar, data + demoCrcAt, step);
            demoCrcAt += step;
            if (demoCrcAt >= receiver.length)
            {
                if (hldata::crc32Finish(demoCrcSoFar) == receiver.crc)
                {
                    demoArena.places[demoArena.find(demoTaking)].length = receiver.length;
                    demoArena.arrived(demoTaking);
                }
                else
                {
                    const std::uint32_t map = demoTaking;
                    const int place = demoArena.find(map);
                    demoFromMap[place] = 0;
                    demoTaking = 0;
                    demoArena.spoiled(map);
                    demoWant(map, map == demoMap);
                    return 0;
                }
            }
        }
        // (Said for a while: one lost answer would keep the host sending.)
        if (demoSaidDone < kAirLingerVblanks)
        {
            ++demoSaidDone;
            const Hold hold;
            return receiver.ask(reply);
        }
        // Then what is wanted next: what comes after this map, or the rest of
        // one left for it.
        if (demoArena.isWhole(demoTaking))
        {
            for (int n = 0; n < demoLinkCount; ++n)
            {
                if (!demoArena.isWhole(demoLinks[n]) && demoArena.find(demoLinks[n]) >= 0)
                {
                    demoWant(demoLinks[n], false);
                    return 0;
                }
            }
            if (const std::uint32_t next = hldata::netDemoPrefetch(demoArena, demoLinks, demoLinkCount))
                demoWant(next, false);
        }
        return 0;
    }

    void offerAir(const std::string &map)
    {
        for (int i = 0; i < offerCount; ++i)
        {
            if (offers[i].air)
            {
                offers[i].map = map;
                return;
            }
        }
        if (offerCount < kOffersMost)
            offers[offerCount++] = Offer{map, nullptr, 0, std::string(), true};
    }

    void onMapProgress(void (*progress)(int percent)) { mapProgress = progress; }

    bool bootParam(hldata::NetDlParam &param)
    {
        static hldata::NetDlParam kept;
        static int known = -1;
        if (known < 0)
        {
            known = 0;
            // (The same place of the memory the loader wrote in: where that is
            // depends on how much of it was mapped then, and is now.)
            for (const std::uintptr_t at : {std::uintptr_t(0x027FFBE0), std::uintptr_t(0x02FFFBE0), std::uintptr_t(0x023FFBE0)})
            {
                if (hldata::netDlRead(reinterpret_cast<const std::uint8_t *>(at), kept))
                {
                    known = 1;
                    break;
                }
            }
        }
        param = kept;
        return known == 1;
    }

    void forgetTaken()
    {
        takenMap[0] = 0;
        takenCharacter = 0;
        takenFull = false;
    }

    void hear()
    {
        if (receiver.direct)
        {
            netGame.frame();
            return;
        }
        if (mapOnAir && receiver.ring != nullptr)
            pump();
    }

    std::uint32_t airLength() { return receiver.known ? receiver.length : 0; }

    int airRate()
    {
        const std::uint32_t frames = Global::vblanks() - streamSince;
        return frames < 30 ? 0 : static_cast<int>(static_cast<std::uint64_t>(receiver.direct ? receiver.base : receiver.given) * hldata::kAirPiece * 60 / 1024 / frames);
    }

    const char *baseGroup() { return heldBase; }

    void airStatus(char *line, int room)
    {
        std::snprintf(line, static_cast<std::size_t>(room), "%s %lu/%lu of %lu%s", receiver.known ? "had" : "heard none",
                      static_cast<unsigned long>(receiver.given), static_cast<unsigned long>(receiver.base),
                      static_cast<unsigned long>(receiver.pieces()), airLost ? ", host quiet" : "");
    }
    const char *airMap() { return takenMap; }
    int airSkill() { return takenSkill; }

    void count(hldata::GuestKind kind, std::uint32_t bytes) { heldNow.mapBytes[static_cast<int>(kind)] += bytes; }

    const Held &held() { return heldNow; }
} // namespace hlrt::guest
#endif
