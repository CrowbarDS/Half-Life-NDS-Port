#include "data/KeyCache.hpp"
#include "DebugLog.hpp"

#ifndef HLDS_CHILD
#include <cstdlib>
#include <cstring>
#include <string>

#include "Global.hpp"
#include "HlAnim.hpp"
#include "HlDsRoom.hpp"
#include "data/CardRead.hpp"
#include "data/LevelFile.hpp"
#endif

namespace hlrt
{
    KeyCache keyCache;

#ifndef HLDS_CHILD
    namespace
    {
        const hldata::ModelKey *askCache(const hldata::ModelView &model, int clip)
        {
            return keyCache.keysOf(model, clip);
        }

        /// A clip's keys as the block holds them: whole words.
        std::uint32_t heldBytes(std::uint32_t keyCount) { return (keyCount * sizeof(hldata::ModelKey) + 3u) & ~3u; }
    } // namespace

    bool KeyCache::create(const hldata::ModelView *views, int count)
    {
        destroy();
        hldata::modelKeysOf = &askCache;
        // Which have their keys apart, and how many clips they have.
        int models = 0;
        std::uint32_t clips = 0;
        for (int i = 0; i < count; ++i)
        {
            if (views[i].header != nullptr && (views[i].header->flags & hldata::kModelKeysApart) != 0)
            {
                ++models;
                clips += views[i].header->clipCount;
            }
        }
        if (models == 0)
            return true;
        models_ = static_cast<Model *>(std::calloc(static_cast<std::size_t>(models), sizeof(Model)));
        clips_ = static_cast<Clip *>(std::calloc(clips != 0 ? clips : 1, sizeof(Clip)));
        if (models_ == nullptr || clips_ == nullptr)
        {
            destroy();
            failed_ = 1;
            return false;
        }
        // Each clip's run of keys, and the largest of them.
        std::size_t allKeys = 0, largest = 0;
        int m = 0;
        std::uint32_t c = 0;
        for (int i = 0; i < count; ++i)
        {
            const hldata::ModelView &view = views[i];
            if (view.header == nullptr || (view.header->flags & hldata::kModelKeysApart) == 0)
                continue;
            Model &model = models_[m++];
            model.firstClip = c;
            const std::uint32_t keyBytes = view.header->keyCount * sizeof(hldata::ModelKey);
            allKeys += keyBytes;
            for (int k = 0; k < view.header->clipCount; ++k, ++c)
            {
                Clip &clip = clips_[c];
                view.clipKeys(k, clip.firstKey, clip.keyCount);
                clip.at = -1;
                const std::size_t bytes = clip.keyCount * sizeof(hldata::ModelKey);
                largest = bytes > largest ? bytes : largest;
            }
            // Its keys, beside it: the right size, and readable a clip at a time.
            const std::string path = global.mapPath("mdl/") + level.view.modelRefs[i].name + hldata::kModelKeysFile;
            if (NEA_NpacMemberFind(path.c_str(), &model.member) == 0 && model.member.method == NEA_NPAC_STORED &&
                model.member.size == keyBytes)
                model.header = view.header;
            else
                ++failed_;
        }
        modelCount_ = models;
        clipCount_ = clips;
        blockBytes_ = static_cast<std::uint32_t>(hldata::dsKeyCacheBytes(largest, allKeys));
        block_ = static_cast<std::uint8_t *>(std::malloc(blockBytes_ != 0 ? blockBytes_ : 4));
        if (block_ == nullptr)
        {
            destroy();
            failed_ = 1;
            return false;
        }
        bytes_ = blockBytes_ + static_cast<std::uint32_t>(models * sizeof(Model) + clips * sizeof(Clip));
        return true;
    }

    void KeyCache::destroy()
    {
        std::free(models_);
        std::free(clips_);
        std::free(block_);
        models_ = nullptr;
        clips_ = nullptr;
        block_ = nullptr;
        modelCount_ = 0;
        clipCount_ = 0;
        blockBytes_ = head_ = 0;
        last_ = -1;
        bytes_ = reads_ = readBytes_ = failed_ = 0;
    }

    const hldata::ModelKey *KeyCache::keysOf(const hldata::ModelView &view, int clip)
    {
        // (The same model as last time, nearly always: a pose, then its
        // hit boxes, then the next actor of the same kind.)
        int m = last_;
        if (m < 0 || models_[m].header != view.header)
        {
            m = -1;
            for (int i = 0; i < modelCount_; ++i)
            {
                if (models_[i].header == view.header)
                {
                    m = i;
                    break;
                }
            }
            if (m < 0)
                return nullptr;
            last_ = m;
        }
        Model &model = models_[m];
        Clip &c = clips_[model.firstClip + static_cast<std::uint32_t>(clip)];
        if (c.at < 0 && !load(model, c))
            return nullptr;
        // (Indexed by the file's own numbering: the block's copy, less where
        // the clip's first key was in the file.)
        return reinterpret_cast<const hldata::ModelKey *>(reinterpret_cast<std::uintptr_t>(block_ + c.at) -
                                                          c.firstKey * sizeof(hldata::ModelKey));
    }

    bool KeyCache::load(Model &model, Clip &clip)
    {
        const std::uint32_t bytes = heldBytes(clip.keyCount);
        if (bytes > blockBytes_ || clip.keyCount == 0)
        {
            ++failed_;
            return false;
        }
        // Round the block: from the start again where it would not fit.
        if (head_ + bytes > blockBytes_)
            head_ = 0;
        const std::uint32_t from = head_, to = head_ + bytes;
        // What was there is let go of.
        for (std::uint32_t i = 0; i < clipCount_; ++i)
        {
            Clip &other = clips_[i];
            if (other.at >= 0 && static_cast<std::uint32_t>(other.at) < to &&
                static_cast<std::uint32_t>(other.at) + heldBytes(other.keyCount) > from)
                other.at = -1;
        }
        const std::uint32_t keyBytes = clip.keyCount * sizeof(hldata::ModelKey);
#ifdef HLDS_PROFILE
        const std::uint32_t readFrom = DebugLog::Perf::now();
#endif
        const bool got = cardReadMember(model.member, clip.firstKey * sizeof(hldata::ModelKey), block_ + from, keyBytes);
#ifdef HLDS_PROFILE
        DebugLog::Perf::noteKeyRead(DebugLog::Perf::now() - readFrom);
#endif
        if (!got)
        {
            ++failed_;
            return false;
        }
        clip.at = static_cast<std::int32_t>(from);
        head_ = to;
        ++reads_;
        readBytes_ += keyBytes;
        return true;
    }
#else
    bool KeyCache::create(const hldata::ModelView *, int) { return true; }
    void KeyCache::destroy() {}
    const hldata::ModelKey *KeyCache::keysOf(const hldata::ModelView &, int) { return nullptr; }
#endif
} // namespace hlrt
