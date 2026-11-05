#include "data/TextureStream.hpp"
#include "net/NetGame.hpp"
#include "audio/Sound.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <NEAMain.h>

#include "DebugLog.hpp"
#include "Global.hpp"
#include "Settings.hpp"
#include "HlTexture.hpp"
#include "HlTextureStream.hpp"
#include "data/LevelFile.hpp"
#include "data/ModelCache.hpp"
#include "data/TextureCache.hpp"
#include "data/VramWriter.hpp"
#include "render/WorldDraw.hpp"

namespace hlrt
{
    TextureStream textureStream;
    std::uint32_t *g_textureSeenAt = nullptr;
    std::uint32_t g_textureFrame = 0;

    namespace
    {
        using namespace hldata;

        /// What is read at the end of a frame waits here for the vertical
        /// blank. (The biggest streamed texture is 128 x 128, about 7 KB.)
        constexpr std::uint32_t kStageBytes = 32 * 1024;
        constexpr int kMaxStaged = 16;
        constexpr int kMaxEvicted = 16;
        /// A texture drawn this recently is wanted above everything; one
        /// drawn within kRememberFrames is remembered a little.
        constexpr std::uint32_t kSeenFrames = 6;
        constexpr std::uint32_t kRememberFrames = 120;
        constexpr std::uint32_t kSeenScore = 1u << 20;
        /// The textures every view wants, put there for good: at most this
        /// share of the memory (in hundredths), of those seen from at least
        /// this share of the map's leaves.
        constexpr int kPinnedPercent = 35;
        constexpr int kPinnedShare = 60;
        /// What a cycle that is drawn is kept ahead by: the pictures of the
        /// next two changes of picture (they come ten times a second).
        constexpr std::uint32_t kPictureTicks = 6;
        /// What a picture of a cycle that is not the one shown is wanted
        /// with (if there is room): above what is only remembered.
        constexpr std::uint32_t kSiblingScore = 1500;
        /// What a texture on a surface the eye's leaf can see (its PVS), not
        /// drawn lately, is wanted with: read ahead if there is room, below
        /// anything remembered, and above what cannot be seen from here
        /// (which makes way first).
        constexpr std::uint32_t kPvsScore = 300;
        /// How much of the PVS is walked in a frame, once the eye is in
        /// another leaf: surfaces and leaves.
        constexpr int kPvsMarksPerFrame = 768;
        constexpr int kPvsLeavesPerFrame = 1024;
        /// Of the heap's headroom, as much as a third may be the copies of
        /// textures kept in main memory (the cycles' pictures and the tiled
        /// floors' variants), and this much at the very most.
        constexpr std::size_t kCacheMost = 1024 * 1024;

        TextureStreamer planner;
        PvsTextureWalk pvs;
        std::uint16_t pvsStamp = 0; ///< WorldDraw::visStamp the walk was begun for
        StreamInfo infos[kStreamMaxTextures];
        std::uint32_t seenAt[kStreamMaxTextures];
        alignas(32) std::uint8_t stage[kStageBytes];

        /// What was read and waits to be written, then drawn: several at
        /// once, the plan going on while they wait.
        struct Staged
        {
            int id;
            TextureHeader header;
            std::uint32_t at;          ///< where its body starts in `stage`
            const std::uint8_t *body; ///< or where it is in main memory (then `at` is not used)
            std::uint32_t texelOffset; ///< where the plan put it
            std::uint32_t paletteOffset;
            /// The frame (TextureStream::blanks) from which it may be
            /// written: two after its place was let go by another (the
            /// picture on the screen until then may be drawn from it), at
            /// once if nothing was there.
            std::uint32_t readyBlank;
            std::uint32_t ticket; ///< its last write's (VramWriter), once queued
            bool queued;
            bool drawn;
        };
        /// In the order they were read: `stagedFirst` the oldest. Their
        /// bodies are in `stage` one after another, in a ring: from the
        /// oldest's to `stageHead`, where the next is read.
        Staged staged[kMaxStaged];
        int stagedFirst = 0;
        int stagedCount = 0;
        std::uint32_t stageHead = 0;

        /// For a picture of a cycle (the game's '+' textures, and the other
        /// face's): the cycle's first picture and how many pictures it has.
        /// 1 for anything else.
        std::int16_t groupFirst[kStreamMaxTextures];
        std::uint8_t groupCount[kStreamMaxTextures];
        /// A copy of the texture file's header and body in main memory, for
        /// the ones that are swapped most (cycles' pictures, tiled floors'
        /// variants): nothing is read off the card to bring them back.
        std::uint8_t *ram[kStreamMaxTextures];
        /// (A console in DSi mode:) wanted now and not in `ram`; never to come;
        /// and the one asked for.
        std::uint8_t fetchWanted[kStreamMaxTextures];
        std::uint8_t fetchNever[kStreamMaxTextures];
        int fetchAsked = -1;
        int ramCount = 0;
        std::size_t ramBytes = 0;

        int streamedTotal = 0;
        int pinnedTotal = 0;
        int refusals = 0; ///< textures whose file was not what the plan had them as, or that NEA would not bind
        int flatNow = 0;
        std::uint32_t frameNo = 0;
        std::uint8_t failures[kStreamMaxTextures]; ///< how often each could not be loaded (HLDS_PROFILE's report)
        int refusalsLogged = 0;
        /// Spells of textures in view drawn flat (after a map starts, a
        /// teleport, a turn): the one going on, from when, and the most at
        /// once; and all of them since the map began.
        std::uint32_t flatFrom = 0, flatFromBlank = 0;
        int flatMost = 0;
        struct FlatSpells
        {
            std::uint32_t count, frames, longest, longestBlanks;
            int most;
        };
        FlatSpells flatSpells{}, flatSpellsWritten{};
        int loadsWritten = 0; ///< planner.loads() at the last line of them

        /// The video memory that is the plan's own: the plan's offsets are
        /// places in it. Texels (with their index where NEA says), in up to
        /// kMostRegions pieces that the plan sees one after another, a
        /// barrier between each two (TextureStreamer::addBarrier); and
        /// palettes. The weapons' skin slots are at the top of the first.
        constexpr int kMostRegions = 4;
        /// A piece smaller than this is left to NEA.
        constexpr int kLeastRegion = 1024;
        NEA_TextureRegion regions[kMostRegions]{};
        std::uint32_t regionAt[kMostRegions]; ///< where each starts in the plan's offsets
        std::uint32_t regionBytes[kMostRegions]; ///< how much of it the plan has
        int regionCount = 0;
        int regionTotal = 0; ///< texel bytes taken, skin slots and all
        NEA_PaletteRegion paletteRegion{};
        std::uint32_t skinBytes = 0;
        int skinSlots = 0;
        /// reserveSkinSlots' slots: regions[0] is them, for begin() (0: none set by).
        std::uint32_t earlyBytes = 0;


        /// What was let go, freed two blanks after.
        struct Retired
        {
            NEA_Material *material;
            NEA_Palette *palette;
            std::uint32_t blank;
        };
        constexpr int kMostRetired = 64;
        Retired retired[kMostRetired];
        int retiredCount = 0;

        /// What a streamed texture costs NEA, and what the plan counts. The
        /// palette is a guess from above (the cooker's own rule for how many
        /// colours a texture of that many texels gets: the file has the true
        /// number, and is never bigger).
        StreamInfo infoFor(const TextureRef &ref)
        {
            StreamInfo info;
            const std::uint32_t texels = static_cast<std::uint32_t>(ref.width) * ref.height;
            const std::uint32_t colours = std::min<std::uint32_t>(512, std::max<std::uint32_t>(16, texels / 16)) & ~1u;
            info.texelBytes = texels / 4; // 4x4: the texels (the index goes where they say)
            info.paletteBytes = colours * 2;
            // (What is read: the texels, their index, half as much, and the
            // colours; as it takes up the staging buffer.)
            info.stageBytes = ByteArena<kStreamMaxBlocks>::round(info.texelBytes + info.texelBytes / 2 + info.paletteBytes);
            info.streamed = true;
            return info;
        }

        bool fileFor(int id, TextureHeader &header, std::uint8_t *body, std::size_t room)
        {
            const TextureRef &ref = level.view.textures[id];
            char name[40];
            hldata::textureFileName(ref.name, ref.width, ref.height, false, name);
            const std::string path = global.texDir() + name;
            return TextureCache::readFile(path.c_str(), ref.width, ref.height, 0, header, body, room);
        }
    } // namespace

    namespace
    {
        /// The file is what the plan took it to be: no bigger than its places.
        bool fitsPlan(int id, const TextureHeader &h)
        {
            const TextureRef &ref = level.view.textures[id];
            return h.width == ref.width && h.height == ref.height && h.indexBytes == h.texelBytes / 2 &&
                   h.texelBytes <= infos[id].texelBytes && h.paletteBytes <= ByteArena<kStreamMaxBlocks>::round(infos[id].paletteBytes) &&
                   ((h.texelBytes | h.paletteBytes) & 3) == 0;
        }

        std::uint8_t *texelsAt(std::uint32_t offset)
        {
            int r = 0;
            while (r + 1 < regionCount && offset >= regionAt[r + 1])
                ++r;
            return static_cast<std::uint8_t *>(regions[r].texels) + (offset - regionAt[r]);
        }
        std::uint8_t *paletteAt(std::uint32_t offset) { return static_cast<std::uint8_t *>(paletteRegion.base) + offset; }

        /// A texture that could not be loaded: let go, and tried again later.
        void refuse(int id)
        {
            planner.failed(id);
            failures[id] = static_cast<std::uint8_t>(failures[id] < 255 ? failures[id] + 1 : 255);
            ++refusals;
#ifdef HLDS_PROFILE
            if (refusalsLogged < 24)
            {
                ++refusalsLogged;
                textureStream.report("refused");
            }
#endif
        }

        /// Its pieces are in video memory: NEA is told where, and it is drawn.
        void bind(int id, const TextureHeader &h, std::uint32_t texelOffset, std::uint32_t paletteOffset)
        {
            NEA_Material *material = NEA_MaterialCreate();
            NEA_Palette *palette = NEA_PaletteCreate();
            const bool ok =
                material != nullptr && palette != nullptr &&
                NEA_MaterialTexBindFixed(material, NEA_TEX4X4, h.width, h.height,
                                         static_cast<NEA_TextureFlags>(NEA_TEXTURE_WRAP_S | NEA_TEXTURE_WRAP_T),
                                         texelsAt(texelOffset)) == 1 &&
                NEA_PaletteBindFixed(palette, paletteAt(paletteOffset), h.paletteColours, NEA_TEX4X4) == 1;
            if (!ok)
            {
                if (material != nullptr)
                    NEA_MaterialDelete(material);
                if (palette != nullptr)
                    NEA_PaletteDelete(palette);
                refuse(id);
                return;
            }
            NEA_MaterialSetPalette(material, palette);
            textureCache.attachStreamed(static_cast<unsigned>(id), material, palette);
            planner.loaded(id);
        }

        Staged &stagedAt(int k) { return staged[(stagedFirst + k) % kMaxStaged]; }

        /// How much can be read at `stageHead`, in one piece: what is after
        /// the newest body, or the start of the buffer if that has more
        /// room (then `stageHead` goes there).
        std::uint32_t stageSpan()
        {
            int oldest = -1;
            for (int k = 0; k < stagedCount && oldest < 0; ++k)
                oldest = stagedAt(k).body == nullptr ? k : -1;
            if (oldest < 0)
            {
                stageHead = 0;
                return kStageBytes;
            }
            const std::uint32_t tail = stagedAt(oldest).at;
            if (stageHead < tail)
                return tail - stageHead;
            if (stageHead == tail)
                return 0; // (all of it, the ring gone round)
            const std::uint32_t end = kStageBytes - stageHead;
            if (end >= tail)
                return end;
            stageHead = 0;
            return tail;
        }
    } // namespace

    bool TextureStream::loadNow(int id, std::uint32_t texelOffset, std::uint32_t paletteOffset)
    {
        TextureHeader header;
        const std::uint8_t *body = stage;
        if (ram[id] != nullptr)
        {
            std::memcpy(&header, ram[id], sizeof(header));
            body = ram[id] + sizeof(header);
        }
        else if (!fileFor(id, header, stage, kStageBytes))
        {
            planner.failed(id);
            return false;
        }
        if (!fitsPlan(id, header))
        {
            refuse(id);
            return false;
        }
        // (While a map loads: nothing is being drawn from the region yet.)
        NEA_VramWrite(texelsAt(texelOffset), body, header.texelBytes);
        NEA_VramWrite(NEA_Texture4x4IndexAddress(texelsAt(texelOffset)), body + header.texelBytes, header.indexBytes);
        NEA_VramWrite(paletteAt(paletteOffset), body + header.texelBytes + header.indexBytes, header.paletteBytes);
        bind(id, header, texelOffset, paletteOffset);
        return planner.resident(id);
    }

    void TextureStream::reserveSkinSlots(std::uint32_t bytes)
    {
        end();
        bytes = (bytes + 31u) & ~31u;
        // Both, else one.
        for (int slots = 2; slots >= 1 && regionCount == 0; --slots)
        {
            if (NEA_TextureRegionReserve4x4(static_cast<int>(slots * bytes), &regions[0]) >=
                static_cast<int>(slots * bytes))
            {
                regionTotal = regions[0].texelBytes;
                regionCount = 1;
            }
            else
                NEA_TextureRegionRelease(&regions[0]);
        }
        earlyBytes = regionCount != 0 ? bytes : 0;
    }

    void TextureStream::keepFromStream(int id)
    {
        TextureHeader header;
        if (id < 0 || id >= kStreamMaxTextures || ram[id] != nullptr || !fileFor(id, header, stage, kStageBytes))
            return;
        const std::size_t body = header.texelBytes + header.indexBytes + header.paletteBytes;
        auto *copy = static_cast<std::uint8_t *>(std::malloc(sizeof(header) + body));
        if (copy == nullptr)
            return;
        std::memcpy(copy, &header, sizeof(header));
        std::memcpy(copy + sizeof(header), stage, body);
        ram[id] = copy;
        ramBytes += sizeof(header) + body;
        ++ramCount;
    }

    void TextureStream::fetchTick()
    {
#ifdef HLDS_CHILD
        if (!global.twlChild || !active_ || netGame.fetchBusy())
            return;
        int best = -1, bestScore = -1;
        for (int id = 0; id < planner.count(); ++id)
        {
            if (!infos[id].streamed || ram[id] != nullptr || fetchNever[id] != 0)
                continue;
            const int score = (fetchWanted[id] != 0 ? 1000 : 0) + level.view.textures[id].share;
            if (score > bestScore)
            {
                best = id;
                bestScore = score;
            }
        }
        // (All here: on with what is not wanted yet, while it is quiet. And
        // nothing wanted just now: in turn with the rest of the textures.)
        static bool heardTurn = false;
        heardTurn = !heardTurn;
        if (best < 0 || (bestScore < 1000 && heardTurn && prerolled()))
        {
            if ((sound.fetchAhead() || modelCache.fetchAhead()) || best < 0)
                return;
        }
        const TextureRef &ref = level.view.textures[best];
        char name[48] = "tex/";
        hldata::textureFileName(ref.name, ref.width, ref.height, false, name + 4);
        fetchAsked = best;
        netGame.fetchMap(hldata::nameHash(name));
#endif
    }

    void TextureStream::fetched(std::uint8_t *data, std::uint32_t size)
    {
        if (fetchAsked < 0 || fetchAsked >= kStreamMaxTextures || ram[fetchAsked] != nullptr ||
            size <= sizeof(TextureHeader))
        {
            std::free(data);
            fetchAsked = -1;
            return;
        }
        ram[fetchAsked] = data;
        ramBytes += size;
        ++ramCount;
        fetchWanted[fetchAsked] = 0;
        fetchAsked = -1;
        fetchCame_ = true;
        planner.retryFailed();
    }

    void TextureStream::fetchFailed(bool never)
    {
        if (never && fetchAsked >= 0 && fetchAsked < kStreamMaxTextures)
            fetchNever[fetchAsked] = 1;
        fetchAsked = -1;
    }

    bool TextureStream::prerolled() const
    {
        for (int id = 0; id < planner.count(); ++id)
        {
            if (infos[id].streamed && ram[id] == nullptr && fetchNever[id] == 0 && level.view.textures[id].share >= 20)
                return false;
        }
        return true;
    }

    void TextureStream::begin()
    {
        // (Slots set by before the map's textures: kept.)
        const std::uint32_t early = earlyBytes;
        earlyBytes = 0;
        // (Nor what a stream's textures left in main memory: only the rest.)
        if (early == 0)
            end(true);
        const int count = std::min<int>(level.view.textureCount, kStreamMaxTextures);
        streamedTotal = 0;
        for (int i = 0; i < count; ++i)
        {
            infos[i] = StreamInfo();
            seenAt[i] = 0xFFFFFFFFu; // never
            if (textureCache.streamed(static_cast<unsigned>(i)))
            {
                infos[i] = infoFor(level.view.textures[i]);
                ++streamedTotal;
            }
        }
        // The region: what the streamed textures take, and the slots of the
        // weapons' skins, as far as what video memory has left will go.
        std::uint32_t texelNeed = 0, paletteNeed = 0;
        for (int i = 0; i < count; ++i)
        {
            if (!infos[i].streamed)
                continue;
            texelNeed += ByteArena<kStreamMaxBlocks>::round(infos[i].texelBytes);
            paletteNeed += ByteArena<kStreamMaxBlocks>::round(infos[i].paletteBytes);
        }
        skinBytes = early != 0 ? early : modelCache.largestSkin();
        if (early == 0)
        {
            regionCount = 0;
            regionTotal = 0;
        }
        if (early == 0)
        {
            // The largest places first, until there is room for all.
            // (Less what the models a weapon fires take when it is taken up:
            // they are in video memory, beside it.)
            const int want = std::min(static_cast<int>(texelNeed + 2 * skinBytes),
                                      NEA_TextureFree4x4() - modelCache.lazyTexelBytes() -
                                          static_cast<int>(modelCache.charactersAwaited()));
            while (regionCount < kMostRegions && regionTotal < want)
            {
                NEA_TextureRegion &r = regions[regionCount];
                if (NEA_TextureRegionReserve4x4(want - regionTotal, &r) < kLeastRegion)
                {
                    NEA_TextureRegionRelease(&r);
                    break;
                }
                regionTotal += r.texelBytes;
                ++regionCount;
            }
        }
        // (Palettes: a little is left to NEA, as there always was -- and
        // room for the colours of a weapon the map has not, if it names one.)
        if (streamedTotal != 0)
            NEA_PaletteRegionReserve(std::min(static_cast<int>(paletteNeed),
                                              NEA_PaletteFreeMem() - 512 - modelCache.lazyPaletteBytes()),
                                     &paletteRegion);
        // Two skin slots (the weapon in hand, and the next one written while it
        // is still drawn) where the streamed textures all fit beside them, or
        // the second costs no more than an eighth of the region; else one (a
        // weapon taken up is not drawn for the frames it takes to write). At
        // the top of the first piece, the largest.
        const std::uint32_t first = regionCount > 0 ? static_cast<std::uint32_t>(regions[0].texelBytes) : 0;
        skinSlots = 0;
        if (skinBytes != 0)
        {
            const bool both = first >= 2 * skinBytes &&
                              (static_cast<std::uint32_t>(regionTotal) - 2 * skinBytes >= texelNeed ||
                               skinBytes <= static_cast<std::uint32_t>(regionTotal) / 8);
            skinSlots = both ? 2 : (first >= skinBytes ? 1 : 0);
        }
        {
            const std::uint32_t top = first - static_cast<std::uint32_t>(skinSlots) * skinBytes;
            void *const slots[2] = {static_cast<std::uint8_t *>(regions[0].texels) + top,
                                    static_cast<std::uint8_t *>(regions[0].texels) + top + skinBytes};
            modelCache.setSlots(slots, skinSlots, skinBytes);
        }
        // The plan's offsets: the pieces one after another, a barrier between.
        std::uint32_t room = 0;
        for (int r = 0; r < regionCount; ++r)
        {
            if (r > 0)
                room += TextureStreamer::kBarrierBytes;
            regionAt[r] = room;
            regionBytes[r] = static_cast<std::uint32_t>(regions[r].texelBytes) -
                             (r == 0 ? static_cast<std::uint32_t>(skinSlots) * skinBytes : 0);
            room += regionBytes[r];
        }
        if (streamedTotal == 0)
        {
            report("map loaded");
            return;
        }

        // The cycles: each picture knows its cycle's first and how many there
        // are. (The first picture of a texture says how many it has; the
        // other face's cycle where it starts and how many.)
        for (int i = 0; i < count; ++i)
        {
            groupFirst[i] = static_cast<std::int16_t>(i);
            groupCount[i] = 1;
        }
        auto markCycle = [&](int first, int pictures)
        {
            if (pictures <= 1 || first < 0 || first + pictures > count)
                return;
            for (int k = 0; k < pictures; ++k)
            {
                groupFirst[first + k] = static_cast<std::int16_t>(first);
                groupCount[first + k] = static_cast<std::uint8_t>(pictures);
            }
        };
        for (int i = 0; i < count; ++i)
        {
            const TextureRef &ref = level.view.textures[i];
            if (ref.frames > 1)
                markCycle(i, ref.frames);
            if (ref.other != kNoTexture && ref.otherFrames > 1)
                markCycle(ref.other, ref.otherFrames);
        }
        // Main-memory copies of the pictures of the cycles and of the variants
        // a floor is tiled with (the game's '-' textures), as far as the heap
        // can spare: a cycle changes picture ten times a second, and a tiled
        // floor wears several, and when video memory is short the ones not
        // wanted at the moment are let go and come back from here, not from the
        // card.
        {
            const std::size_t room = std::min<std::size_t>(global.heapHeadroom() / 3, kCacheMost);
            for (int i = 0; i < count; ++i)
            {
                const TextureRef &ref = level.view.textures[i];
                const bool tiled = ref.name[0] == '-' && ref.name[1] >= '0' && ref.name[1] <= '9';
                if (ram[i] != nullptr || !infos[i].streamed || (groupCount[i] <= 1 && !tiled))
                    continue;
                TextureHeader header;
                if (!fileFor(i, header, stage, kStageBytes))
                    continue;
                const std::size_t body = header.texelBytes + header.indexBytes + header.paletteBytes;
                if (ramBytes + sizeof(header) + body > room)
                    continue;
                auto *copy = static_cast<std::uint8_t *>(std::malloc(sizeof(header) + body));
                if (copy == nullptr)
                    continue;
                std::memcpy(copy, &header, sizeof(header));
                std::memcpy(copy + sizeof(header), stage, body);
                ram[i] = copy;
                // (Nothing to read, but as much to write in the blank: it
                // counts against what one blank may take as any other.)
                ramBytes += sizeof(header) + body;
                ++ramCount;
            }
        }

        planner.init(infos, count, room, static_cast<std::uint32_t>(paletteRegion.bytes));
        for (int r = 1; r < regionCount; ++r)
            planner.addBarrier(regionAt[r] - TextureStreamer::kBarrierBytes);
        pinnedTotal = 0;
        refusals = 0;
        refusalsLogged = 0;
        std::memset(failures, 0, sizeof(failures));
        flatNow = 0;
        frameNo = 1;
        g_textureFrame = frameNo;
        flatFrom = 0;
        flatSpells = flatSpellsWritten = FlatSpells{};
        loadsWritten = 0;
        pvs.reset(count);
        pvsStamp = worldDraw.visStamp();

        // (A console in DSi mode has nothing yet: all of it is asked for.)
        std::memset(fetchWanted, 0, sizeof(fetchWanted));
        std::memset(fetchNever, 0, sizeof(fetchNever));
        fetchAsked = -1;
        fetchCame_ = false;
        if (global.twlChild)
        {
        }
        else if (planner.allFit())
        {
            // Nothing to stream: every texture is there for good.
            bool leftOut = false;
            for (int i = 0; i < count; ++i)
            {
                std::uint32_t t, p;
                if (infos[i].streamed && planner.pin(i, t, p))
                {
                    ++pinnedTotal;
                    loadNow(i, t, p);
                }
                else if (infos[i].streamed)
                {
                    leftOut = true;
                }
            }
            // ... unless the sums fit and the pieces of memory did not: then
            // none is there for good, so that the one left out (c0a0e's
            // entrance door) takes the place of one that is not in view the
            // moment it is, instead of being flat for as long as the map is.
            if (leftOut)
            {
                planner.unpinAll();
                pinnedTotal = 0;
            }
        }
        else
        {
            // What most views want is there for good: widest first.
            std::uint32_t pinRoom = room / 100 * kPinnedPercent;
            for (int share = 100; share >= kPinnedShare; --share)
            {
                for (int i = 0; i < count; ++i)
                {
                    if (!infos[i].streamed || groupCount[i] > 1 || level.view.textures[i].share != share ||
                        infos[i].texelBytes > pinRoom)
                        continue;
                    std::uint32_t t, p;
                    if (!planner.pin(i, t, p))
                        continue;
                    pinRoom -= infos[i].texelBytes;
                    ++pinnedTotal;
                    loadNow(i, t, p);
                }
            }
            // The rest by how widely they are wanted, as far as there is room.
            planner.clearScores();
            for (int i = 0; i < count; ++i)
            {
                if (!infos[i].streamed || planner.pinned(i))
                    continue;
                planner.addScore(i, 1u + level.view.textures[i].share);
            }
            planner.rebuild();
            for (int round = 0; round < 64; ++round)
            {
                StreamLoad loads[kMaxStaged];
                int evicted[kMaxEvicted], evictedCount = 0;
                const int made = planner.plan(0xFFFFFFFFu, kStageBytes, loads, kMaxStaged, evicted, kMaxEvicted, evictedCount);
                if (made == 0)
                    break;
                for (int k = 0; k < made; ++k)
                    loadNow(loads[k].id, loads[k].texelOffset, loads[k].paletteOffset);
                planner.rebuild(); // (what did not fit is let alone: Failed)
            }
        }
        // Those that did not fit are tried again once something is seen.
        planner.retryFailed();
        stagedFirst = 0;
        stagedCount = 0;
        stageHead = 0;
        g_textureSeenAt = seenAt;
        active_ = true;
        report("map loaded");
    }

    void TextureStream::retire(NEA_Material *material, NEA_Palette *palette)
    {
        if (material == nullptr && palette == nullptr)
            return;
        if (retiredCount >= kMostRetired)
            freeRetired(true); // (never: but rather a flicker than a leak)
        retired[retiredCount++] = {material, palette, blank_};
    }

    void TextureStream::freeRetired(bool all)
    {
        int kept = 0;
        for (int k = 0; k < retiredCount; ++k)
        {
            const Retired &r = retired[k];
            if (!all && blank_ - r.blank < 2)
            {
                retired[kept++] = r;
                continue;
            }
            if (r.material != nullptr)
                NEA_MaterialDelete(r.material);
            if (r.palette != nullptr)
                NEA_PaletteDelete(r.palette);
        }
        retiredCount = kept;
    }

    void TextureStream::end(bool keepRam)
    {
        // Nothing more is written to the region, and it goes back to NEA
        // (what was bound in it goes with TextureCache and ModelCache).
        vramWriter.clear();
        modelCache.dropSlots();
        for (int r = 0; r < regionCount; ++r)
            NEA_TextureRegionRelease(&regions[r]);
        regionCount = 0;
        regionTotal = 0;
        NEA_PaletteRegionRelease(&paletteRegion);
        skinSlots = 0;
        skinBytes = 0;
        freeRetired(true);
        for (int i = 0; i < kStreamMaxTextures && !keepRam; ++i)
        {
            std::free(ram[i]);
            ram[i] = nullptr;
        }
        if (!keepRam)
        {
            ramCount = 0;
            ramBytes = 0;
        }
        g_textureSeenAt = nullptr;
        active_ = false;
        stagedFirst = 0;
        stagedCount = 0;
        stageHead = 0;
        streamedTotal = 0;
        pinnedTotal = 0;
        // (The materials go with TextureCache.)
    }

    void TextureStream::demand()
    {
        planner.clearScores();
        flatNow = 0;
        for (int id = 0; id < planner.count(); ++id)
        {
            if (!infos[id].streamed || planner.pinned(id))
                continue;
            const std::uint32_t at = seenAt[id];
            const std::uint32_t pvsScore = pvs.wanted()[id] != 0 ? kPvsScore : 0;
            if (at == 0xFFFFFFFFu)
            {
                if (pvsScore != 0)
                    planner.addScore(id, pvsScore);
                continue;
            }
            const std::uint32_t age = frameNo - at;
            std::uint32_t score = pvsScore;
            if (age <= kSeenFrames)
                score = kSeenScore;
            else if (age <= kRememberFrames)
                score += 1000 - age;
            if (score != 0)
                planner.addScore(id, score);
            if (age <= 2)
                planner.setRecent(id);
            if (age <= kSeenFrames && !planner.resident(id))
                ++flatNow;
        }
        // A cycle that is being drawn: the pictures it will show in the next
        // changes are wanted as much as the one shown; its other pictures a
        // little, so that they stay if there is room and go first if not.
        const std::uint32_t blanks = static_cast<std::uint32_t>(Global::vblanks());
        for (int first = 0; first < planner.count(); ++first)
        {
            const int pictures = groupCount[first];
            if (pictures <= 1 || groupFirst[first] != first)
                continue;
            bool drawn = false;
            for (int k = 0; k < pictures && !drawn; ++k)
            {
                const std::uint32_t at = seenAt[first + k];
                drawn = at != 0xFFFFFFFFu && frameNo - at <= kSeenFrames;
            }
            if (!drawn)
                continue;
            for (int k = 0; k < pictures; ++k)
            {
                const int id = first + k;
                if (infos[id].streamed && !planner.pinned(id))
                    planner.addScore(id, kSiblingScore);
            }
            // (The picture of the next frame, and of the two changes after it: a
            // picture comes in at the vertical blank before the frame that
            // shows it.)
            static const std::uint32_t kAhead[3] = {1, 4, 7};
            for (const std::uint32_t ahead : kAhead)
            {
                const int id = first + static_cast<int>(((blanks + ahead) / kPictureTicks) % static_cast<std::uint32_t>(pictures));
                if (infos[id].streamed && !planner.pinned(id))
                    planner.addScore(id, kSeenScore);
            }
        }
    }

    namespace
    {
        /// What may be written by now (`now`: TextureStream::blanks) is
        /// queued to be (VramWriter, in the vertical blanks to come), in the
        /// order it was read; what the queue has no room for, next time.
        void queueReady(std::uint32_t now)
        {
            // (No more waiting in the queue at once than a frame's reading
            // on a DSi: a weapon's skin queued behind it waits no longer.)
            constexpr std::uint32_t kQueuedMost = 8 * 1024;
            auto bytesOf = [](const Staged &s)
            { return s.header.texelBytes + s.header.indexBytes + s.header.paletteBytes; };
            std::uint32_t waiting = 0;
            for (int k = 0; k < stagedCount; ++k)
            {
                const Staged &s = stagedAt(k);
                if (s.queued && !s.drawn && !vramWriter.done(s.ticket))
                    waiting += bytesOf(s);
            }
            for (int k = 0; k < stagedCount && waiting < kQueuedMost; ++k)
            {
                Staged &s = stagedAt(k);
                if (s.queued || static_cast<std::int32_t>(now - s.readyBlank) < 0)
                    continue;
                const std::uint8_t *body = s.body != nullptr ? s.body : stage + s.at;
                std::uint8_t *texels = texelsAt(s.texelOffset);
                const TextureHeader &h = s.header;
                if (vramWriter.write(texels, body, h.texelBytes) == 0)
                    return;
                const std::uint32_t index =
                    vramWriter.write(NEA_Texture4x4IndexAddress(texels), body + h.texelBytes, h.indexBytes);
                const std::uint32_t colours =
                    index != 0 ? vramWriter.write(paletteAt(s.paletteOffset), body + h.texelBytes + h.indexBytes,
                                                  h.paletteBytes)
                               : 0;
                if (colours == 0)
                    return; // (half queued: what was queued is written again with the rest)
                s.ticket = colours;
                s.queued = true;
                waiting += bytesOf(s);
            }
        }
    } // namespace

    void TextureStream::upload()
    {
        ++blank_;
        freeRetired(false);
        if (!active_ || stagedCount == 0)
            return;
        queueReady(blank_);
        // Each one all there: drawn from the next frame on.
        for (int k = 0; k < stagedCount; ++k)
        {
            Staged &s = stagedAt(k);
            if (s.queued && !s.drawn && vramWriter.done(s.ticket))
            {
                bind(s.id, s.header, s.texelOffset, s.paletteOffset);
                s.drawn = true;
            }
        }
        // (The oldest that are drawn give their room in the staging back.)
        while (stagedCount > 0 && stagedAt(0).drawn)
        {
            stagedFirst = (stagedFirst + 1) % kMaxStaged;
            --stagedCount;
        }
    }

    void TextureStream::update(int budgetBytes)
    {
        if (!active_)
            return;
        ++frameNo;
        g_textureFrame = frameNo;
        fetchTick();
        if (perfLogging())
            noteFlatSpell();
        // The eye is in another leaf: what it can see is walked again, a
        // piece a frame. (Nothing, while it stays.)
        if (worldDraw.visStamp() != pvsStamp)
        {
            pvsStamp = worldDraw.visStamp();
            pvs.start(static_cast<int>(level.view.leafCount));
        }
        if (pvs.busy())
            pvs.step(level.view.leaves, level.view.marks, level.view.surfaces,
                     [](int leaf) { return worldDraw.leafVisible(leaf); }, kPvsMarksPerFrame, kPvsLeavesPerFrame);
        demand();
        if (frameNo % 8 == 0)
            planner.retryFailed();
#ifdef HLDS_PROFILE
        if (frameNo % 1024 == 0)
            report("every 1024 frames");
#endif
        if (frameNo % 4 == 0 || frameNo < 16)
            planner.rebuild();

        // More is read while what was read before waits to be written, as
        // far as the staging buffer goes.
        planner.setClock(blank_);
        const std::uint32_t span = stagedCount < kMaxStaged ? stageSpan() : 0;
        if (span == 0)
            return;
        StreamLoad loads[kMaxStaged];
        int evicted[kMaxEvicted], evictedCount = 0;
        const int made = planner.plan(static_cast<std::uint32_t>(budgetBytes), span, loads, kMaxStaged - stagedCount,
                                      evicted, kMaxEvicted, evictedCount);
        // Those that make way are drawn flat from now on; what takes their
        // place is written no sooner than two frames from now, when no
        // picture drawn with them is still being drawn. (What goes where
        // nothing was is written at the next vertical blank.)
        for (int k = 0; k < evictedCount; ++k)
            textureCache.detachStreamed(static_cast<unsigned>(evicted[k]));
        std::uint32_t used = 0;
        for (int k = 0; k < made; ++k)
        {
            Staged &s = stagedAt(stagedCount);
            s.id = loads[k].id;
            s.at = stageHead + used;
            s.body = nullptr;
            s.texelOffset = loads[k].texelOffset;
            s.paletteOffset = loads[k].paletteOffset;
            s.readyBlank = planner.readyAt(loads[k]);
            s.ticket = 0;
            s.queued = false;
            s.drawn = false;
            if (ram[s.id] != nullptr)
            {
                // In main memory already: nothing to read.
                std::memcpy(&s.header, ram[s.id], sizeof(s.header));
                s.body = ram[s.id] + sizeof(s.header);
                if (!fitsPlan(s.id, s.header))
                {
                    refuse(s.id);
                    continue;
                }
                ++stagedCount;
                continue;
            }
            if (!fileFor(s.id, s.header, stage + s.at, span - used))
            {
                planner.failed(s.id);
                fetchWanted[s.id] = 1;
                continue;
            }
            if (!fitsPlan(s.id, s.header))
            {
                refuse(s.id);
                continue;
            }
            const std::uint32_t body = s.header.texelBytes + s.header.indexBytes + s.header.paletteBytes;
            used += (body + 31u) & ~31u;
            ++stagedCount;
        }
        stageHead += used;
        queueReady(blank_);
    }

    void TextureStream::noteFlatSpell()
    {
        // (Counted here, every frame, whatever the plan is doing.)
        int flat = 0;
        for (int id = 0; id < planner.count(); ++id)
        {
            if (infos[id].streamed && seenAt[id] != 0xFFFFFFFFu && frameNo - seenAt[id] <= kSeenFrames &&
                !planner.resident(id))
                ++flat;
        }
        std::FILE *log = nullptr;
        if (flat != 0)
        {
            if (flatFrom == 0)
            {
                flatFrom = frameNo;
                flatFromBlank = Global::vblanks();
                flatMost = 0;
            }
            flatMost = std::max(flatMost, flat);
        }
        else if (flatFrom != 0)
        {
            // A spell ends: counted, and a long one said at once.
            const std::uint32_t frames = frameNo - flatFrom;
            const std::uint32_t blanks = Global::vblanks() - flatFromBlank;
            flatFrom = 0;
            FlatSpells &f = flatSpells;
            ++f.count;
            f.frames += frames;
            f.most = std::max(f.most, flatMost);
            if (frames > f.longest)
            {
                f.longest = frames;
                f.longestBlanks = blanks;
            }
            if (frames >= 30 && (log = std::fopen(global.dataPath("perf.txt").c_str(), "a")) != nullptr)
                std::fprintf(log, "tex flat: from frame %lu, %lu frames (%lu blanks), at most %d in view\n",
                             static_cast<unsigned long>(frameNo - frames), static_cast<unsigned long>(frames),
                             static_cast<unsigned long>(blanks), flatMost);
        }
        // All of them, every ten seconds or so if there were more (or more
        // was read).
        if (frameNo % 512 == 0 && (flatSpells.count != flatSpellsWritten.count || planner.loads() != loadsWritten))
        {
            if (log == nullptr)
                log = std::fopen(global.dataPath("perf.txt").c_str(), "a");
            const FlatSpells &f = flatSpells;
            if (log != nullptr)
            {
                std::fprintf(log, "tex flat spells by frame %lu: %lu, %lu frames in all, the longest %lu (%lu blanks), "
                                  "at most %d in view; %d loads, %d let go\n",
                             static_cast<unsigned long>(frameNo), static_cast<unsigned long>(f.count),
                             static_cast<unsigned long>(f.frames), static_cast<unsigned long>(f.longest),
                             static_cast<unsigned long>(f.longestBlanks), f.most, planner.loads(), planner.evictions());
                flatSpellsWritten = f;
                loadsWritten = planner.loads();
            }
        }
        if (log != nullptr)
            std::fclose(log);
    }

    StreamStats TextureStream::stats() const
    {
        StreamStats s;
        s.streamed = streamedTotal;
        s.resident = planner.residentCount();
        s.pinned = pinnedTotal;
        s.loads = planner.loads();
        s.evictions = planner.evictions();
        s.refused = refusals;
        s.flatNow = flatNow;
        s.copies = ramCount;
        s.copiesKb = static_cast<int>(ramBytes / 1024);
        s.texelKb = static_cast<int>(planner.texelBytesUsed() / 1024);
        s.capacityKb = static_cast<int>(planner.texelCapacity() / 1024);
        const VramWriter::Stats w = vramWriter.stats();
        s.late = static_cast<int>(w.late);
        s.lastLine = w.mostLine;
        s.dropped = planner.dropped();
        for (int i = 0; i < planner.count(); ++i)
            s.attached += infos[i].streamed && textureCache.material(static_cast<unsigned>(i)) != nullptr ? 1 : 0;
        return s;
    }

    void TextureStream::report(const char *why)
    {
#ifdef HLDS_PROFILE
        std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a");
        if (log == nullptr)
            return;
        const StreamStats st = stats();
        const VramWriter::Stats w = vramWriter.stats();
        std::fprintf(log, "tex %s: frame %lu, %d/%d resident (%d attached), %d flat in view, %d/%d KB of %d KB, "
                          "palettes %d/%d, %d refused, %d retired, staged %d\n",
                     why, static_cast<unsigned long>(frameNo), st.resident, st.streamed, st.attached, st.flatNow,
                     static_cast<int>(planner.texelBytesUsed() / 1024), static_cast<int>(planner.texelCapacity() / 1024),
                     regionTotal / 1024, static_cast<int>(planner.paletteBytesUsed()),
                     static_cast<int>(planner.paletteCapacity()), st.refused, retiredCount, stagedCount);
        std::fprintf(log, "  region: %d KB of texels in %d pieces (%d, %d, %d, %d; %d skin slots of %lu), %d of palettes; "
                          "writes: %lu KB in %lu blanks, %lu pieces, %lu blanks ran out of time, the latest line %d "
                          "(the last %d)\n",
                     regionTotal / 1024, regionCount, regions[0].texelBytes, regions[1].texelBytes,
                     regions[2].texelBytes, regions[3].texelBytes, skinSlots, static_cast<unsigned long>(skinBytes),
                     paletteRegion.bytes,
                     static_cast<unsigned long>(w.bytes / 1024), static_cast<unsigned long>(w.blanks),
                     static_cast<unsigned long>(w.pieces), static_cast<unsigned long>(w.late), w.mostLine, w.lastLine);
        NEA_TextureStats t;
        NEA_TextureGetStats(&t);
        NEA_PaletteStats p;
        NEA_PaletteGetStats(&p);
        std::fprintf(log, "  vram: %d free in %d pieces, largest %d (A %d, B1 %d, B2 %d, C %d), slots %d/%d, "
                          "materials %d, leaked %d, dangling %d; palettes: %d free in %d, largest %d, slots %d/%d\n",
                     t.free, t.freeChunks, t.largest, t.largestIn[0], t.largestIn[1], t.largestIn[2], t.largestIn[3],
                     t.slotsUsed, t.slotsMax, t.materials, t.leaked, t.dangling, p.free, p.freeChunks, p.largest,
                     p.slotsUsed, p.slotsMax);
        // Which of what is in view is flat, and why.
        int listed = 0;
        for (int id = 0; id < planner.count() && listed < 16; ++id)
        {
            if (!infos[id].streamed || seenAt[id] == 0xFFFFFFFFu || frameNo - seenAt[id] > kSeenFrames ||
                planner.resident(id))
                continue;
            static const char *const kStates[] = {"absent", "staged", "resident", "failed"};
            const TextureRef &ref = level.view.textures[id];
            std::fprintf(log, "  flat %d %.16s %dx%d %s, failed %d times, %lu+%lu bytes, score %lu%s\n", id, ref.name,
                         ref.width, ref.height, kStates[static_cast<int>(planner.state(id)) & 3], failures[id],
                         static_cast<unsigned long>(infos[id].texelBytes),
                         static_cast<unsigned long>(infos[id].paletteBytes),
                         static_cast<unsigned long>(planner.score(id)), ram[id] != nullptr ? ", in RAM" : "");
            ++listed;
        }
        std::fclose(log);
#else
        (void)why;
#endif
    }
} // namespace hlrt
