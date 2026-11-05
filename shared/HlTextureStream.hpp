#pragma once

// Streaming a map's textures through a fixed part of video memory.
//
// A texture is in one of four states: ABSENT (drawn as its flat
// colour), STAGED (read from the card, waiting for the next vertical blank to
// be copied in), RESIDENT, and FAILED (could not be placed; it is let alone
// until the next demand pass). Some textures are PINNED: resident for good,
// what every view wants (a map whose textures all fit pins them all, and then
// nothing is ever evicted, which is what keeps a small map exactly as it was).
//
// The rest are wanted by SCORE, set by whoever knows what is in view
// (addScore). rebuild() takes the few that are wanted and not here, best
// first, as a queue; plan() takes them off it a few at a time within a
// budget of bytes. When there is no room, a resident texture makes way if
// its score is less than HALF of the wanted one's: what is nearly as wanted is
// left alone, or two textures would take each other's place for ever. If
// evicting what may be evicted would still not make room, nothing is evicted
// (it would only have to load again).
//
// Two areas of memory, each an arena of its own: the texels (for a 4x4
// compressed texture, with its index words, which sit where the texels' place
// says; the console works that out), and the palettes.
//
// The planner keeps a clock (setClock: the caller's frames) and remembers
// what space was let go when: a texture placed where one was let go less
// than two frames before must not be written yet (a picture still on the
// screen may be drawn from it), one placed in space free for longer may be
// written at once (readyAt).

#include <cstdint>
#include <cstring>

namespace hldata
{
    /// Variable-size blocks in one arena, first fit, 32-byte granularity.
    /// `owner` is the caller's number for what is in the block.
    template <int MaxBlocks>
    struct ByteArena
    {
        struct Block
        {
            std::uint32_t off;
            std::uint32_t size;
            int owner;
        };
        std::uint32_t cap = 0;
        std::uint32_t used = 0;
        int n = 0;
        Block b[MaxBlocks];

        static constexpr std::uint32_t round(std::uint32_t size) { return (size + 31u) & ~31u; }

        void init(std::uint32_t bytes)
        {
            cap = bytes;
            used = 0;
            n = 0;
        }

        /// Whether a block of `size` would be placed (and where), without
        /// placing it: -1 if not.
        long fit(std::uint32_t size) const
        {
            size = round(size);
            if (n >= MaxBlocks || size == 0)
                return -1;
            std::uint32_t prevEnd = 0;
            for (int i = 0; i < n; ++i)
            {
                if (b[i].off - prevEnd >= size)
                    return static_cast<long>(prevEnd);
                prevEnd = b[i].off + b[i].size;
            }
            return cap - prevEnd >= size ? static_cast<long>(prevEnd) : -1;
        }

        /// A block at `off` exactly (the caller knows it is free): for what
        /// is never to be placed over. The index, or -1.
        int reserveAt(std::uint32_t off, std::uint32_t size, int owner)
        {
            size = round(size);
            if (n >= MaxBlocks || size == 0 || off > cap || cap - off < size)
                return -1;
            int at = 0;
            while (at < n && b[at].off < off)
                ++at;
            for (int j = n; j > at; --j)
                b[j] = b[j - 1];
            b[at] = {off, size, owner};
            ++n;
            used += size;
            return at;
        }

        /// The index of the new block, or -1.
        int alloc(std::uint32_t size, int owner)
        {
            size = round(size);
            if (n >= MaxBlocks || size == 0)
                return -1;
            std::uint32_t prevEnd = 0;
            int at = n;
            for (int i = 0; i < n; ++i)
            {
                if (b[i].off - prevEnd >= size)
                {
                    at = i;
                    break;
                }
                prevEnd = b[i].off + b[i].size;
            }
            if (at == n && cap - prevEnd < size)
                return -1;
            for (int j = n; j > at; --j)
                b[j] = b[j - 1];
            b[at] = {prevEnd, size, owner};
            ++n;
            used += size;
            return at;
        }

        void free(int i)
        {
            used -= b[i].size;
            for (int j = i; j < n - 1; ++j)
                b[j] = b[j + 1];
            --n;
        }

        int find(int owner) const
        {
            for (int i = 0; i < n; ++i)
            {
                if (b[i].owner == owner)
                    return i;
            }
            return -1;
        }

        /// Would `size` fit if every block with evictable[i] were gone?
        bool fitsIfEvicted(std::uint32_t size, const bool *evictable) const
        {
            size = round(size);
            std::uint32_t prevEnd = 0;
            for (int i = 0; i < n; ++i)
            {
                if (evictable[i])
                    continue;
                if (b[i].off - prevEnd >= size)
                    return true;
                prevEnd = b[i].off + b[i].size;
            }
            return cap - prevEnd >= size;
        }
    };

    inline constexpr int kStreamMaxTextures = 512;
    inline constexpr int kStreamMaxBlocks = 512;
    /// The queue of what is wanted and not here, best first.
    inline constexpr int kStreamCandidates = 96;

    enum class StreamState : std::uint8_t
    {
        Absent,
        Staged,
        Resident,
        Failed
    };

    /// What the planner needs to know of each texture.
    struct StreamInfo
    {
        std::uint32_t texelBytes = 0;   ///< what it takes of the texel area
        std::uint32_t paletteBytes = 0; ///< what it takes of the palette area
        std::uint32_t stageBytes = 0;   ///< what reading it takes of the staging buffer
        bool streamed = false;          ///< false: not the planner's (always there, or never)
    };

    /// One texture to read and where it goes.
    struct StreamLoad
    {
        int id;
        std::uint32_t texelOffset;
        std::uint32_t paletteOffset;
    };

    class TextureStreamer
    {
    public:
        /// `count` textures, numbered 0..count-1; the areas have these many
        /// bytes. Nothing is resident.
        void init(const StreamInfo *info, int count, std::uint32_t texelBytes, std::uint32_t paletteBytes)
        {
            count_ = count < kStreamMaxTextures ? count : kStreamMaxTextures;
            std::memset(state_, 0, sizeof(state_));
            std::memset(pinned_, 0, sizeof(pinned_));
            std::memset(score_, 0, sizeof(score_));
            std::memset(recent_, 0, sizeof(recent_));
            dropped_ = 0;
            for (int i = 0; i < count_; ++i)
                info_[i] = info[i];
            texels_.init(texelBytes);
            palettes_.init(paletteBytes);
            queueSize_ = queuePos_ = 0;
            resident_ = loads_ = evictions_ = 0;
            freedCount_ = 0;
            freedFloor_ = 0;
            freedFloorSet_ = false;
        }

        /// What was let go this long ago (in setClock's frames) or longer
        /// may be written over.
        static constexpr std::uint32_t kFreedFrames = 2;
        /// How many pieces let go are remembered at once (more than two
        /// frames' worth: the oldest is then folded into one moment, later
        /// than it was, so that nothing is ever written too soon).
        static constexpr int kRecentFrees = 64;

        /// The caller's frame now: space let go from here on is let go at
        /// `now`. (Forgets what was let go long enough ago.)
        void setClock(std::uint32_t now)
        {
            clock_ = now;
            int kept = 0;
            for (int i = 0; i < freedCount_; ++i)
            {
                if (clock_ - freed_[i].at < kFreedFrames)
                    freed_[kept++] = freed_[i];
            }
            freedCount_ = kept;
            if (freedFloorSet_ && clock_ - freedFloor_ >= kFreedFrames)
                freedFloorSet_ = false;
        }
        std::uint32_t clock() const { return clock_; }

        /// The first frame at which `load` may be written: the clock if now,
        /// later if some of its texel or palette space was let go less than
        /// kFreedFrames ago.
        std::uint32_t readyAt(const StreamLoad &load) const
        {
            const std::uint32_t texelEnd =
                load.texelOffset + ByteArena<kStreamMaxBlocks>::round(info_[load.id].texelBytes);
            const std::uint32_t paletteEnd =
                load.paletteOffset + ByteArena<kStreamMaxBlocks>::round(info_[load.id].paletteBytes);
            std::uint32_t wait = 0; // frames from now
            if (freedFloorSet_)
                wait = kFreedFrames - (clock_ - freedFloor_);
            for (int i = 0; i < freedCount_; ++i)
            {
                const Freed &f = freed_[i];
                const bool overlaps = f.palette ? f.off < paletteEnd && load.paletteOffset < f.off + f.size
                                                : f.off < texelEnd && load.texelOffset < f.off + f.size;
                const std::uint32_t w = kFreedFrames - (clock_ - f.at);
                if (overlaps && w > wait)
                    wait = w;
            }
            return clock_ + wait;
        }

        /// The texel area is in pieces of memory one after another: a block
        /// of kBarrierBytes at `offset`, owned by no texture, which nothing is
        /// placed over or makes way for, keeps a texture from lying across
        /// two. (Call after init, before anything is placed.)
        static constexpr std::uint32_t kBarrierBytes = 32;
        bool addBarrier(std::uint32_t offset) { return texels_.reserveAt(offset, kBarrierBytes, kBarrierOwner) >= 0; }

        int count() const { return count_; }
        StreamState state(int id) const { return state_[id]; }
        bool resident(int id) const { return state_[id] == StreamState::Resident; }
        bool pinned(int id) const { return pinned_[id]; }
        std::uint32_t score(int id) const { return score_[id]; }
        int residentCount() const { return resident_; }
        int loads() const { return loads_; }
        int evictions() const { return evictions_; }
        std::uint32_t texelBytesUsed() const { return texels_.used; }
        std::uint32_t paletteBytesUsed() const { return palettes_.used; }
        std::uint32_t texelCapacity() const { return texels_.cap; }
        std::uint32_t paletteCapacity() const { return palettes_.cap; }
        const ByteArena<kStreamMaxBlocks> &texelArena() const { return texels_; }
        const ByteArena<kStreamMaxBlocks> &paletteArena() const { return palettes_; }

        /// Whether every streamed texture would fit at once (blocks and
        /// all): then they are all pinned and nothing ever streams.
        bool allFit() const
        {
            // (Into an empty area blocks go one after another: no need to
            // place them to know.)
            std::uint64_t texelSum = 0, paletteSum = 0;
            int blocks = 0;
            for (int i = 0; i < count_; ++i)
            {
                if (!info_[i].streamed)
                    continue;
                texelSum += ByteArena<kStreamMaxBlocks>::round(info_[i].texelBytes);
                paletteSum += ByteArena<kStreamMaxBlocks>::round(info_[i].paletteBytes);
                ++blocks;
            }
            return blocks <= kStreamMaxBlocks && texelSum <= texels_.cap && paletteSum <= palettes_.cap;
        }

        /// Make `id` resident for good: where it goes, or false if there is
        /// no room. (The caller writes it, and then calls loaded().)
        bool pin(int id, std::uint32_t &texelOffset, std::uint32_t &paletteOffset)
        {
            if (!place(id, texelOffset, paletteOffset))
                return false;
            pinned_[id] = true;
            state_[id] = StreamState::Staged;
            return true;
        }

        /// None is resident for good any more (what is resident stays, and
        /// makes way by its score like the rest): for when pinning them all
        /// left one out -- the sums fit, the pieces did not.
        void unpinAll()
        {
            std::memset(pinned_, 0, sizeof(pinned_[0]) * static_cast<std::size_t>(count_));
        }

        void clearScores()
        {
            std::memset(score_, 0, sizeof(score_[0]) * static_cast<std::size_t>(count_));
            std::memset(recent_, 0, sizeof(recent_[0]) * static_cast<std::size_t>(count_));
        }
        /// `id` is in the picture that is on the screen, or the one before:
        /// whatever else is wanted, it does not make way. (Its memory would
        /// be written while a picture that shows it is still being drawn.)
        void setRecent(int id) { recent_[id] = true; }
        void addScore(int id, std::uint32_t amount)
        {
            const std::uint32_t sum = score_[id] + amount;
            score_[id] = sum < score_[id] ? 0xFFFFFFFFu : sum; // (saturating)
        }

        /// The queue, from the scores: the best few that are wanted, not
        /// here, and the planner's.
        void rebuild()
        {
            int n = 0;
            for (int id = 0; id < count_; ++id)
            {
                const std::uint32_t s = score_[id];
                if (!info_[id].streamed || pinned_[id] || state_[id] != StreamState::Absent || s == 0 ||
                    (n == kStreamCandidates && s <= queueScore_[n - 1]))
                    continue;
                int k = n < kStreamCandidates ? n++ : n - 1;
                while (k > 0 && queueScore_[k - 1] < s)
                {
                    queue_[k] = queue_[k - 1];
                    queueScore_[k] = queueScore_[k - 1];
                    --k;
                }
                queue_[k] = static_cast<std::uint16_t>(id);
                queueScore_[k] = s;
            }
            queueSize_ = n;
            queuePos_ = 0;
        }

        /// Take textures off the queue: at most `maxLoads`, until `budget`
        /// bytes have been asked for, as long as `stageFree` bytes of
        /// staging are left. Fills `loads`; the textures that had to make
        /// way are put in `evicted` (the caller draws them flat from now
        /// on, before their memory is written over). Returns how many loads.
        int plan(std::uint32_t budget, std::uint32_t stageFree, StreamLoad *loads, int maxLoads,
                 int *evicted, int maxEvicted, int &evictedCount)
        {
            evictedCount = 0;
            int made = 0, fails = 0;
            std::uint32_t spent = 0;
            while (spent < budget && made < maxLoads && fails < 2)
            {
                int best = -1;
                while (queuePos_ < queueSize_)
                {
                    const int id = queue_[queuePos_++];
                    if (state_[id] == StreamState::Absent)
                    {
                        best = id;
                        break;
                    }
                }
                if (best < 0)
                    break;
                const StreamInfo &e = info_[best];
                if (e.stageBytes > stageFree)
                {
                    --queuePos_; // keep it at the head of the queue for the next upload
                    break;
                }
                std::uint32_t texelOffset = 0, paletteOffset = 0;
                bool ok = place(best, texelOffset, paletteOffset);
                if (!ok)
                    ok = makeRoom(best, evicted, maxEvicted, evictedCount) && place(best, texelOffset, paletteOffset);
                if (!ok)
                {
                    state_[best] = StreamState::Failed;
                    ++fails;
                    continue;
                }
                loads[made++] = {best, texelOffset, paletteOffset};
                state_[best] = StreamState::Staged;
                stageFree -= e.stageBytes;
                spent += e.stageBytes;
                ++loads_;
            }
            return made;
        }

        /// The texture was written: it is resident.
        void loaded(int id)
        {
            if (state_[id] != StreamState::Resident)
                ++resident_;
            state_[id] = StreamState::Resident;
        }

        /// The read or the write did not work: give the room back.
        void failed(int id)
        {
            release(id);
            state_[id] = StreamState::Failed;
        }

        /// The memory refused a texture the plan thought would fit (it is
        /// not all one piece, or part of it cannot take this kind of
        /// texture): from now on the areas are taken to hold no more than
        /// what is in them now, and the texture is let go (call after
        /// failed()). A resident texture that makes way brings the room
        /// back only up to what was learned here.
        void shrinkCapacity()
        {
            if (texels_.cap > texels_.used)
                texels_.cap = texels_.used;
            if (palettes_.cap > palettes_.used)
                palettes_.cap = palettes_.used;
        }

        /// What the areas really hold now, learned again: never less than
        /// what is in them. (After a refusal the plan takes the areas to be
        /// full; once loads go in again it asks the memory itself what it
        /// has, and the room comes back.)
        void setCapacity(std::uint32_t texelBytes, std::uint32_t paletteBytes)
        {
            texels_.cap = texelBytes > texels_.used ? texelBytes : texels_.used;
            palettes_.cap = paletteBytes > palettes_.used ? paletteBytes : palettes_.used;
        }

        /// How often a plan stopped making way because its list of what
        /// made way was full (the rest waits for the next plan).
        int dropped() const { return dropped_; }

        /// Failed textures are tried again (at the next demand pass).
        void retryFailed()
        {
            for (int i = 0; i < count_; ++i)
            {
                if (state_[i] == StreamState::Failed)
                    state_[i] = StreamState::Absent;
            }
        }

        /// Nothing but the pinned is resident any more (a map is left, or
        /// everything is let go for a test).
        void evictAll()
        {
            for (int i = 0; i < count_; ++i)
            {
                if (pinned_[i] || state_[i] == StreamState::Absent)
                    continue;
                release(i);
                state_[i] = StreamState::Absent;
            }
            queueSize_ = queuePos_ = 0;
        }

    private:
        bool place(int id, std::uint32_t &texelOffset, std::uint32_t &paletteOffset)
        {
            const int t = texels_.alloc(info_[id].texelBytes, id);
            if (t < 0)
                return false;
            const int p = palettes_.alloc(info_[id].paletteBytes, id);
            if (p < 0)
            {
                texels_.free(t);
                return false;
            }
            texelOffset = texels_.b[t].off;
            paletteOffset = palettes_.b[p].off;
            return true;
        }

        void release(int id)
        {
            const int t = texels_.find(id);
            if (t >= 0)
            {
                noteFreed(texels_.b[t].off, texels_.b[t].size, false);
                texels_.free(t);
            }
            const int p = palettes_.find(id);
            if (p >= 0)
            {
                noteFreed(palettes_.b[p].off, palettes_.b[p].size, true);
                palettes_.free(p);
            }
            if (state_[id] == StreamState::Resident)
                --resident_;
        }

        void noteFreed(std::uint32_t off, std::uint32_t size, bool palette)
        {
            if (freedCount_ == kRecentFrees)
            {
                // (Full: the oldest becomes the floor, which holds back every
                // write, not only those over it, until it is old enough.)
                int oldest = 0;
                for (int i = 1; i < freedCount_; ++i)
                {
                    if (clock_ - freed_[i].at > clock_ - freed_[oldest].at)
                        oldest = i;
                }
                if (!freedFloorSet_ || clock_ - freed_[oldest].at < clock_ - freedFloor_)
                    freedFloor_ = freed_[oldest].at;
                freedFloorSet_ = true;
                freed_[oldest] = freed_[--freedCount_];
            }
            freed_[freedCount_++] = {off, size, clock_, palette};
        }

        static constexpr int kBarrierOwner = -1;

        bool evictable(int owner, std::uint32_t wanted) const
        {
            return owner >= 0 && owner < count_ && !pinned_[owner] && !recent_[owner] &&
                   state_[owner] == StreamState::Resident && static_cast<std::uint64_t>(score_[owner]) * 2 < wanted;
        }

        /// Evict what may be, lowest score first, until `id` would fit: or
        /// nothing at all, if it would not fit even so.
        bool makeRoom(int id, int *evicted, int maxEvicted, int &evictedCount)
        {
            const std::uint32_t wanted = score_[id];
            for (int i = 0; i < texels_.n; ++i)
                evictTexel_[i] = evictable(texels_.b[i].owner, wanted);
            for (int i = 0; i < palettes_.n; ++i)
                evictPalette_[i] = evictable(palettes_.b[i].owner, wanted);
            if (!texels_.fitsIfEvicted(info_[id].texelBytes, evictTexel_) ||
                !palettes_.fitsIfEvicted(info_[id].paletteBytes, evictPalette_))
                return false;
            // The cheapest first, in whichever area is short.
            for (int guard = 0; guard < 2 * kStreamMaxBlocks; ++guard)
            {
                const bool texelFits = texelFree(id);
                const bool paletteFits = paletteFree(id);
                if (texelFits && paletteFits)
                    return true;
                int victim = -1;
                std::uint32_t lowest = 0xFFFFFFFFu;
                const auto &arena = texelFits ? palettes_ : texels_;
                for (int i = 0; i < arena.n; ++i)
                {
                    const int owner = arena.b[i].owner;
                    if (evictable(owner, wanted) && score_[owner] < lowest)
                    {
                        lowest = score_[owner];
                        victim = owner;
                    }
                }
                if (victim < 0)
                    return false;
                // (Every texture that makes way is told to the caller, which
                // lets its memory go: one it could not be told of would stay
                // there, the plan counting its room as free. So when the list
                // is full, nothing more is evicted in this plan.)
                if (evictedCount >= maxEvicted)
                {
                    ++dropped_;
                    return false;
                }
                release(victim);
                state_[victim] = StreamState::Absent;
                ++evictions_;
                evicted[evictedCount++] = victim;
            }
            return false;
        }

        bool texelFree(int id) const { return texels_.fit(info_[id].texelBytes) >= 0; }
        bool paletteFree(int id) const { return palettes_.fit(info_[id].paletteBytes) >= 0; }

        int count_ = 0;
        StreamInfo info_[kStreamMaxTextures];
        StreamState state_[kStreamMaxTextures];
        bool pinned_[kStreamMaxTextures];
        std::uint32_t score_[kStreamMaxTextures];
        bool recent_[kStreamMaxTextures];
        int dropped_ = 0;
        ByteArena<kStreamMaxBlocks> texels_, palettes_;
        // (Members, not locals: the console's stack is small.)
        bool evictTexel_[kStreamMaxBlocks];
        bool evictPalette_[kStreamMaxBlocks];
        std::uint16_t queue_[kStreamCandidates];
        std::uint32_t queueScore_[kStreamCandidates];
        int queueSize_ = 0, queuePos_ = 0;
        int resident_ = 0, loads_ = 0, evictions_ = 0;
        /// Space let go in the last kFreedFrames, and when.
        struct Freed
        {
            std::uint32_t off;
            std::uint32_t size;
            std::uint32_t at;
            bool palette;
        };
        Freed freed_[kRecentFrees];
        int freedCount_ = 0;
        std::uint32_t clock_ = 0;
        std::uint32_t freedFloor_ = 0;
        bool freedFloorSet_ = false;
    };
} // namespace hldata
