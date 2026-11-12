#pragma once

// LevelView: a cooked level (HlLevel.hpp) in memory, checked and ready to use.
//
// open() is the ONE place a level is validated. It refuses anything that is
// not exactly this build's format, and it range-checks every index one record
// holds into another section. After it returns true the typed pointers below
// can be indexed by any index found in the data without a check -- which the
// renderer and the hull trace, running in ITCM, rely on.
//
// It does not copy: the view points into the block it was given, which must
// outlive it and must be 4-byte aligned (anything from malloc or std::vector
// is).
//
// std-only, no <nds.h>: the converter's `verify` command and the host tests run
// this exact code on the files the converter writes.

#include <cstring>

#include "HlLevel.hpp"

namespace hldata
{
    enum class LevelError
    {
        Ok,
        TooSmall,
        BadMagic,
        BadVersion,  ///< made by a different converter version: cook again
        BadSize,     ///< truncated or padded: an interrupted copy
        BadChecksum,
        BadLayout,   ///< a section lies outside the file or is misaligned
        BadIndex,    ///< a record names something that does not exist
    };

    inline const char *levelErrorText(LevelError error)
    {
        switch (error)
        {
        case LevelError::Ok:          return "ok";
        case LevelError::TooSmall:    return "the file is too small to be a level";
        case LevelError::BadMagic:    return "not a Half-Life DS level";
        case LevelError::BadVersion:  return "made by another converter version";
        case LevelError::BadSize:     return "the file is incomplete";
        case LevelError::BadChecksum: return "the file is damaged";
        case LevelError::BadLayout:   return "the file's layout is invalid";
        case LevelError::BadIndex:    return "the file's contents are inconsistent";
        default:                      return "unknown error";
        }
    }

    struct LevelView
    {
        const LevelHeader *header = nullptr;

        const Plane *planes = nullptr;
        const std::uint8_t *planeTypes = nullptr;
        const Node *nodes = nullptr;
        const Leaf *leaves = nullptr;
        const ModelSound *modelSounds = nullptr;
        std::uint32_t modelSoundCount = 0;
        const std::uint16_t *marks = nullptr;
        const std::uint16_t *nodeMarks = nullptr;
        const NodeSpan *nodeSpans = nullptr;
        const std::uint8_t *vis = nullptr;
        const Surface *surfaces = nullptr;
        const std::uint32_t *gxStream = nullptr;
        const Model *models = nullptr;
        const ClipNode *clipNodes = nullptr;
        const TextureRef *textures = nullptr;
        const EntityRecord *entities = nullptr;
        const char *entityText = nullptr;
        const Brush *brushes = nullptr;
        const std::uint16_t *brushLeaves = nullptr;
        const LogicRecord *logic = nullptr;
        const LogicAux *logicAux = nullptr;
        const PathCorner *corners = nullptr;
        const std::uint32_t *nameHashes = nullptr;
        const Landmark *landmarks = nullptr;
        const char *strings = nullptr;
        const ModelRef *modelRefs = nullptr;
        const ActorRecord *actors = nullptr;
        const PathNode *pathNodes = nullptr;
        const PathLink *pathLinks = nullptr;
        const SoundRef *sounds = nullptr;
        const SpriteRef *sprites = nullptr;
        const LogicSound *logicSounds = nullptr;
        const SentenceRecord *sentences = nullptr;
        const SentenceWord *sentenceWords = nullptr;
        const RoomRecord *rooms = nullptr;
        const std::uint16_t *speech = nullptr;
        std::uint32_t speechCount = 0;
        /// Where the talk table starts in `speech`: the soldiers' table is
        /// a group shorter in a level before version 64 (talkTableAt).
        std::uint32_t talkAt = kTalkTableAt;
        const MessageRecord *messages = nullptr;
        std::uint32_t messageCount = 0;
        const LightPatch *lightPatches = nullptr;
        std::uint32_t lightPatchCount = 0;

        std::uint32_t planeCount = 0;
        std::uint32_t nodeCount = 0;
        std::uint32_t leafCount = 0;
        std::uint32_t markCount = 0;
        std::uint32_t nodeMarkCount = 0, nodeSpanCount = 0;
        std::uint32_t visSize = 0;
        std::uint32_t surfaceCount = 0;
        std::uint32_t gxWordCount = 0;
        std::uint32_t modelCount = 0;
        std::uint32_t clipNodeCount = 0;
        std::uint32_t textureCount = 0;
        std::uint32_t entityCount = 0;
        std::uint32_t entityTextSize = 0;
        std::uint32_t brushCount = 0;
        std::uint32_t brushLeafCount = 0;
        std::uint32_t logicCount = 0;
        std::uint32_t logicAuxCount = 0;
        std::uint32_t cornerCount = 0;
        std::uint32_t nameCount = 0;
        std::uint32_t landmarkCount = 0;
        std::uint32_t stringsSize = 0;
        std::uint32_t modelRefCount = 0;
        std::uint32_t actorCount = 0;
        std::uint32_t pathNodeCount = 0;
        std::uint32_t pathLinkCount = 0;
        std::uint32_t soundCount = 0;
        std::uint32_t logicSoundCount = 0;
        std::uint32_t sentenceCount = 0;
        std::uint32_t sentenceWordCount = 0;
        std::uint32_t roomCount = 0;
        std::uint32_t spriteCount = 0;

        /// Validate `data` and point this view into it. On any failure the view
        /// is left empty. `checkCrc` exists so a caller that has just computed
        /// the checksum some other way need not pay for it twice; pass true
        /// unless you have.
        ///
        /// `elsewhere` (a console with an Expansion Pak, which keeps the
        /// sections read least there): a section whose entry is not null is
        /// at that address, not in `data`, and its offset is not looked at.
        LevelError open(const std::uint8_t *data, std::uint32_t size, bool checkCrc = true,
                        const std::uint8_t *const *elsewhere = nullptr);

    private:
#ifndef HLDS_CHILD
        const std::uint8_t *const *elsewhere_ = nullptr;
#endif

        template <class T>
        const T *section(const std::uint8_t *data, const SectionDir *dir, Section s,
                         std::uint32_t &count) const
        {
            const SectionDir &d = dir[static_cast<int>(s)];
            count = d.count;
#ifndef HLDS_CHILD
            if (elsewhere_ != nullptr && elsewhere_[static_cast<int>(s)] != nullptr)
                return reinterpret_cast<const T *>(elsewhere_[static_cast<int>(s)]);
#endif
            return reinterpret_cast<const T *>(data + d.offset);
        }

        bool validChild(std::int32_t child) const
        {
            return child >= 0 ? static_cast<std::uint32_t>(child) < nodeCount
                              : static_cast<std::uint32_t>(-1 - child) < leafCount;
        }

        LevelError validate() const;
    };

    inline LevelError LevelView::open(const std::uint8_t *data, std::uint32_t size, bool checkCrc,
                                      const std::uint8_t *const *elsewhere)
    {
        *this = LevelView();
#ifndef HLDS_CHILD
        elsewhere_ = elsewhere;
#else
        (void)elsewhere;
#endif

        if (size < kLevelDataStart)
            return LevelError::TooSmall;
        const auto *h = reinterpret_cast<const LevelHeader *>(data);
        if (std::memcmp(h->magic, kLevelMagic, sizeof(kLevelMagic)) != 0)
            return LevelError::BadMagic;
        if (!levelVersionOk(h->version, h->minReader) || h->sectionCount != kSectionCount)
            return LevelError::BadVersion;
        if (h->fileSize != size)
            return LevelError::BadSize;
        if (checkCrc &&
            crc32(data + sizeof(LevelHeader), size - sizeof(LevelHeader)) != h->crc32)
            return LevelError::BadChecksum;

        const auto *dir = reinterpret_cast<const SectionDir *>(data + sizeof(LevelHeader));
        for (int i = 0; i < kSectionCount; ++i)
        {
            const SectionDir &d = dir[i];
            const std::uint32_t record = sectionRecordSize(static_cast<Section>(i));
            // 64-bit sums: a hostile offset must not wrap its way past the check.
#ifndef HLDS_CHILD
            const bool away = elsewhere != nullptr && elsewhere[i] != nullptr;
#else
            constexpr bool away = false;
#endif
            if ((!away && ((d.offset & 3) != 0 || d.offset < kLevelDataStart ||
                           static_cast<std::uint64_t>(d.offset) + d.size > size)) ||
                static_cast<std::uint64_t>(d.count) * record != d.size)
                return LevelError::BadLayout;
        }

        header = h;
        planes = section<Plane>(data, dir, Section::Planes, planeCount);
        std::uint32_t planeTypeCount = 0;
        planeTypes = section<std::uint8_t>(data, dir, Section::PlaneTypes, planeTypeCount);
        nodes = section<Node>(data, dir, Section::Nodes, nodeCount);
        leaves = section<Leaf>(data, dir, Section::Leaves, leafCount);
        marks = section<std::uint16_t>(data, dir, Section::Marks, markCount);
        nodeMarks = section<std::uint16_t>(data, dir, Section::NodeMarks, nodeMarkCount);
        nodeSpans = section<NodeSpan>(data, dir, Section::NodeSpans, nodeSpanCount);
        vis = section<std::uint8_t>(data, dir, Section::Vis, visSize);
        surfaces = section<Surface>(data, dir, Section::Surfaces, surfaceCount);
        gxStream = section<std::uint32_t>(data, dir, Section::GxStream, gxWordCount);
        models = section<Model>(data, dir, Section::Models, modelCount);
        clipNodes = section<ClipNode>(data, dir, Section::ClipNodes, clipNodeCount);
        textures = section<TextureRef>(data, dir, Section::Textures, textureCount);
        entities = section<EntityRecord>(data, dir, Section::Entities, entityCount);
        entityText = section<char>(data, dir, Section::EntityText, entityTextSize);
        brushes = section<Brush>(data, dir, Section::Brushes, brushCount);
        brushLeaves = section<std::uint16_t>(data, dir, Section::BrushLeaves, brushLeafCount);
        logic = section<LogicRecord>(data, dir, Section::Logic, logicCount);
        logicAux = section<LogicAux>(data, dir, Section::LogicAux, logicAuxCount);
        corners = section<PathCorner>(data, dir, Section::PathCorners, cornerCount);
        nameHashes = section<std::uint32_t>(data, dir, Section::NameHashes, nameCount);
        landmarks = section<Landmark>(data, dir, Section::Landmarks, landmarkCount);
        strings = section<char>(data, dir, Section::Strings, stringsSize);
        modelRefs = section<ModelRef>(data, dir, Section::ModelRefs, modelRefCount);
        actors = section<ActorRecord>(data, dir, Section::Actors, actorCount);
        pathNodes = section<PathNode>(data, dir, Section::PathNodes, pathNodeCount);
        pathLinks = section<PathLink>(data, dir, Section::PathNext, pathLinkCount);
        sounds = section<SoundRef>(data, dir, Section::Sounds, soundCount);
        logicSounds = section<LogicSound>(data, dir, Section::LogicSounds, logicSoundCount);
        sentences = section<SentenceRecord>(data, dir, Section::Sentences, sentenceCount);
        sentenceWords = section<SentenceWord>(data, dir, Section::SentenceWords, sentenceWordCount);
        rooms = section<RoomRecord>(data, dir, Section::Rooms, roomCount);
        sprites = section<SpriteRef>(data, dir, Section::Sprites, spriteCount);
        speech = section<std::uint16_t>(data, dir, Section::Speech, speechCount);
        talkAt = static_cast<std::uint32_t>(talkTableAt(h->version));
        messages = section<MessageRecord>(data, dir, Section::Messages, messageCount);
        modelSounds = section<ModelSound>(data, dir, Section::ModelSounds, modelSoundCount);
        lightPatches = section<LightPatch>(data, dir, Section::LightPatch, lightPatchCount);

        LevelError result = planeTypeCount == planeCount ? validate() : LevelError::BadLayout;
        if (result != LevelError::Ok)
            *this = LevelView();
        return result;
    }

    inline LevelError LevelView::validate() const
    {
        if (modelCount == 0 || leafCount == 0 || actorCount > static_cast<std::uint32_t>(kMaxMapActors))
            return LevelError::BadIndex;
        if (header->visLeafCount >= leafCount)
            return LevelError::BadIndex;

        for (std::uint32_t i = 0; i < planeCount; ++i)
        {
            if (planeTypes[i] > kPlaneOther)
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < nodeCount; ++i)
        {
            const Node &n = nodes[i];
            if (n.plane >= planeCount || !validChild(n.children[0]) ||
                !validChild(n.children[1]) || (n.parent != kNoParent && n.parent >= nodeCount))
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < leafCount; ++i)
        {
            const Leaf &l = leaves[i];
            if (static_cast<std::uint32_t>(l.firstMark) + l.markCount > markCount ||
                (l.visOfs >= 0 && static_cast<std::uint32_t>(l.visOfs) >= visSize) ||
                l.visOfs < -1 || (l.parent != kNoParent && l.parent >= nodeCount))
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < markCount; ++i)
        {
            if (marks[i] >= surfaceCount)
                return LevelError::BadIndex;
        }
        // (One span a node, or none at all.)
        if (nodeSpanCount != 0 && nodeSpanCount != nodeCount)
            return LevelError::BadIndex;
        for (std::uint32_t i = 0; i < nodeSpanCount; ++i)
        {
            if (static_cast<std::uint32_t>(nodeSpans[i].first) + nodeSpans[i].count > nodeMarkCount)
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < nodeMarkCount; ++i)
        {
            if (nodeMarks[i] >= surfaceCount)
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < surfaceCount; ++i)
        {
            const Surface &s = surfaces[i];
            if (static_cast<std::uint64_t>(s.gxOfs) + s.gxWords > gxWordCount ||
                static_cast<std::uint32_t>(s.planeRef & kSurfacePlaneMask) >= planeCount ||
                (s.texture != kNoTexture && s.texture >= textureCount))
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < clipNodeCount; ++i)
        {
            const ClipNode &c = clipNodes[i];
            if (c.plane >= planeCount ||
                (c.children[0] >= 0 && static_cast<std::uint32_t>(c.children[0]) >= clipNodeCount) ||
                (c.children[1] >= 0 && static_cast<std::uint32_t>(c.children[1]) >= clipNodeCount))
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < modelCount; ++i)
        {
            const Model &m = models[i];
            if (!validChild(m.headNode) ||
                (m.clipStanding >= 0 && static_cast<std::uint32_t>(m.clipStanding) >= clipNodeCount) ||
                (m.clipCrouched >= 0 && static_cast<std::uint32_t>(m.clipCrouched) >= clipNodeCount) ||
                (m.clipLarge >= 0 && static_cast<std::uint32_t>(m.clipLarge) >= clipNodeCount) ||
                static_cast<std::uint32_t>(m.firstSurface) + m.surfaceCount > surfaceCount)
                return LevelError::BadIndex;
        }
        // A texture's other pictures are in the table: the rest of its
        // cycle straight after it, its other face where it says.
        for (std::uint32_t i = 0; i < textureCount; ++i)
        {
            const TextureRef &t = textures[i];
            if (t.frames == 0 || i + t.frames > textureCount)
                return LevelError::BadIndex;
            if (t.other != kNoTexture && (t.otherFrames == 0 || static_cast<std::uint32_t>(t.other) + t.otherFrames > textureCount))
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < textureCount; ++i)
        {
            // The name becomes a file name on the card: it must end.
            if (textures[i].name[sizeof(textures[i].name) - 1] != '\0')
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < entityCount; ++i)
        {
            if (entities[i].model >= modelCount)
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < brushCount; ++i)
        {
            const Brush &b = brushes[i];
            if (b.model == 0 || b.model >= modelCount ||
                b.renderClass > static_cast<std::uint8_t>(RenderClass::Hidden) ||
                static_cast<std::uint32_t>(b.firstLeaf) + b.leafCount > brushLeafCount ||
                (b.logic != kNoLogic && b.logic >= logicCount))
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < brushLeafCount; ++i)
        {
            if (brushLeaves[i] >= leafCount)
                return LevelError::BadIndex;
        }
        if (logicCount > 0x7FFF || nameCount == 0 || nameCount > 0xFFFF)
            return LevelError::BadIndex;
        for (std::uint32_t i = 0; i < modelRefCount; ++i)
        {
            // The name becomes a file name on the card: it must end (before
            // the last byte, which says whether it is a lazy weapon's).
            const char last = modelRefs[i].name[sizeof(modelRefs[i].name) - 1];
            if (modelRefs[i].name[sizeof(modelRefs[i].name) - 2] != '\0' || (last != '\0' && last != kModelRefLazy))
                return LevelError::BadIndex;
        }
        // (The sky's six pictures, from the one the header names.)
        if (header->skyTexture != 0 && static_cast<std::uint32_t>(header->skyTexture) - 1 + 6 > textureCount)
            return LevelError::BadIndex;
        if (header->viewModel > modelRefCount)
            return LevelError::BadIndex;
        for (std::uint32_t i = 0; i < actorCount; ++i)
        {
            if (actors[i].model >= modelRefCount || actors[i].name >= nameCount ||
                actors[i].kind >= static_cast<std::uint8_t>(ActorKind::Count) ||
                (actors[i].kind == static_cast<std::uint8_t>(ActorKind::Item)
                     ? !itemWordOk(actors[i].item, stringsSize, sentenceCount)
                     : actors[i].kind == static_cast<std::uint8_t>(ActorKind::Xen)
                           ? (actors[i].item & 0xFFu) >= static_cast<std::uint32_t>(XenPlant::Count)
                           : (actors[i].item >> kActorSquadShift) >= nameCount) ||
                actors[i].target >= nameCount)
                return LevelError::BadIndex;
        }
        // What soldiers say: all of the table or none of it, and lines the
        // map has.
        // (Then, from version 32, what the people say: a heading of each
        // group's lines, which are after it.)
        const std::uint32_t soldiers = talkAt;
        const std::uint32_t talkStart = soldiers + static_cast<std::uint32_t>(kTalkHeading);
        if (speechCount != 0 && speechCount != soldiers && speechCount < talkStart)
            return LevelError::BadIndex;
        for (std::uint32_t i = 0; i < speechCount && i < soldiers; ++i)
        {
            if (speech[i] != 0xFFFF && speech[i] >= sentenceCount)
                return LevelError::BadIndex;
        }
        if (speechCount >= talkStart)
        {
            for (int g = 0; g < kTalkGroups; ++g)
            {
                const std::uint32_t first = speech[soldiers + g * 2], count = speech[soldiers + g * 2 + 1];
                if (first + count > speechCount - talkStart)
                    return LevelError::BadIndex;
            }
            for (std::uint32_t i = talkStart; i < speechCount; ++i)
            {
                if (speech[i] >= sentenceCount)
                    return LevelError::BadIndex;
            }
        }
        // Light patches: a word of the stream each, a style there is, and a
        // base before anything is added to it.
        for (std::uint32_t i = 0; i < lightPatchCount; ++i)
        {
            const LightPatch &p = lightPatches[i];
            if (p.word >= gxWordCount || (p.style != kPatchBase && p.style >= kLightStyles) ||
                (i == 0 && p.style != kPatchBase))
                return LevelError::BadIndex;
        }
        // Messages: text that is in Strings (which ends in a NUL: checked
        // with the map names), an effect there is.
        for (std::uint32_t i = 0; i < messageCount; ++i)
        {
            if (messages[i].text >= stringsSize || messages[i].effect > kMessageScan)
                return LevelError::BadIndex;
        }
        // Sprites: a name that ends; an atlas its frames fit in.
        for (std::uint32_t i = 0; i < spriteCount; ++i)
        {
            const SpriteRef &s = sprites[i];
            if (s.name[sizeof(s.name) - 1] != '\0')
                return LevelError::BadIndex;
            if (s.frames == 0)
                continue;
            if (s.columns == 0 || s.cellWidth == 0 || s.cellHeight == 0 ||
                static_cast<std::uint32_t>(s.columns) * s.cellWidth > s.width ||
                static_cast<std::uint32_t>((s.frames + s.columns - 1) / s.columns) * s.cellHeight > s.height)
                return LevelError::BadIndex;
        }
        // Sounds: a name becomes a file name on the card, so it must end;
        // and everything that points into the list must point inside it.
        if (soundCount > 0x7FFF || (logicSoundCount != 0 && logicSoundCount != logicCount))
            return LevelError::BadIndex;
        for (std::uint32_t i = 0; i < soundCount; ++i)
        {
            if (sounds[i].name[sizeof(sounds[i].name) - 1] != '\0')
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < logicSoundCount; ++i)
        {
            if ((logicSounds[i].move != 0xFFFF && logicSounds[i].move >= soundCount) ||
                (logicSounds[i].stop != 0xFFFF && logicSounds[i].stop >= soundCount) ||
                (logicSounds[i].locked != 0xFFFF && logicSounds[i].locked >= soundCount) ||
                (logicSounds[i].lockedLine != 0xFFFF && logicSounds[i].lockedLine >= sentenceCount) ||
                (logicSounds[i].unlocked != 0xFFFF && logicSounds[i].unlocked >= soundCount) ||
                (logicSounds[i].unlockedLine != 0xFFFF && logicSounds[i].unlockedLine >= sentenceCount))
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < modelSoundCount; ++i)
        {
            if (modelSounds[i].sound >= soundCount)
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < sentenceCount; ++i)
        {
            if (static_cast<std::uint32_t>(sentences[i].firstWord) + sentences[i].wordCount >
                sentenceWordCount)
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < sentenceWordCount; ++i)
        {
            if (sentenceWords[i].sound >= soundCount)
                return LevelError::BadIndex;
        }
        if (pathNodeCount > static_cast<std::uint32_t>(kMaxPathNodes) || pathLinkCount > 0xFFFF)
            return LevelError::BadIndex;
        for (std::uint32_t i = 0; i < pathNodeCount; ++i)
        {
            if (static_cast<std::uint32_t>(pathNodes[i].firstLink) + pathNodes[i].linkCount > pathLinkCount)
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < pathLinkCount; ++i)
        {
            if (pathLinks[i].to >= pathNodeCount ||
                (pathLinks[i].brush != kNoBrush && pathLinks[i].brush >= brushCount))
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < cornerCount; ++i)
        {
            if ((corners[i].next != kNoCorner && corners[i].next >= cornerCount) ||
                corners[i].fireTarget >= nameCount || corners[i].netName >= nameCount)
                return LevelError::BadIndex;
        }
        for (std::uint32_t i = 0; i < logicCount; ++i)
        {
            const LogicRecord &r = logic[i];
            if (r.kind >= static_cast<std::uint8_t>(LogicKind::Count) || r.useType > kUseToggle ||
                (r.brush != kNoBrush && r.brush >= brushCount))
                return LevelError::BadIndex;
            // Names are indices into NameHashes.
            if (r.name >= nameCount || r.target >= nameCount || r.killTarget >= nameCount ||
                r.master >= nameCount || r.netName >= nameCount)
                return LevelError::BadIndex;
            if (r.kind == static_cast<std::uint8_t>(LogicKind::ChangeLevel))
            {
                // The map's name becomes a file name: it must be there, and end.
                bool ends = false;
                for (std::uint32_t c = r.extra[0]; c < stringsSize && !ends; ++c)
                    ends = strings[c] == '\0';
                if (!ends)
                    return LevelError::BadIndex;
            }
            // A trigger volume's shape is a model there is.
            if ((r.kind == static_cast<std::uint8_t>(LogicKind::TriggerHurt) ||
                 r.kind == static_cast<std::uint8_t>(LogicKind::TriggerPush) ||
                 r.kind == static_cast<std::uint8_t>(LogicKind::TriggerTeleport) ||
                 r.kind == static_cast<std::uint8_t>(LogicKind::TriggerGravity) ||
                 r.kind == static_cast<std::uint8_t>(LogicKind::Friction)) &&
                r.pad >= modelCount)
                return LevelError::BadIndex;
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Message) && r.extra[0] >= messageCount)
                return LevelError::BadIndex;
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Script) && r.extra[0] >= nameCount)
                return LevelError::BadIndex;
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Ambient) &&
                (r.extra[3] > 1 || (r.extra[3] == 0 && r.extra[0] >= soundCount) ||
                 (r.extra[3] == 1 && r.extra[0] >= sentenceCount)))
                return LevelError::BadIndex;
            if ((r.kind == static_cast<std::uint8_t>(LogicKind::ChangeTarget) && r.extra[0] >= nameCount) ||
                (r.kind == static_cast<std::uint8_t>(LogicKind::Camera) && r.extra[0] > 7))
                return LevelError::BadIndex;
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Maker) &&
                (r.extra[0] > actorCount || r.extra[1] > actorCount - r.extra[0]))
                return LevelError::BadIndex;
            if (r.kind == static_cast<std::uint8_t>(LogicKind::TrackChange) &&
                ((r.extra[1] != kNoCorner && r.extra[1] >= cornerCount) ||
                 (r.extra[2] != kNoCorner && r.extra[2] >= cornerCount) || r.extra[3] >= nameCount))
                return LevelError::BadIndex;
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Train) ||
                r.kind == static_cast<std::uint8_t>(LogicKind::GunTarget) ||
                r.kind == static_cast<std::uint8_t>(LogicKind::TrackTrain))
            {
                // A train's firstAux is its first corner, not a list.
                if (r.firstAux != kNoCorner && r.firstAux >= cornerCount)
                    return LevelError::BadIndex;
                continue;
            }
            if (static_cast<std::uint32_t>(r.firstAux) + r.auxCount > logicAuxCount)
                return LevelError::BadIndex;
            // A speaker's, sentences.
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Speaker))
            {
                for (std::uint32_t a = 0; a < r.auxCount; ++a)
                {
                    if (logicAux[r.firstAux + a].target >= sentenceCount)
                        return LevelError::BadIndex;
                }
            }
            // A multisource's list holds record indices.
            if (r.kind == static_cast<std::uint8_t>(LogicKind::MultiSource))
            {
                for (std::uint32_t a = 0; a < r.auxCount; ++a)
                {
                    if (logicAux[r.firstAux + a].target >= logicCount)
                        return LevelError::BadIndex;
                }
            }
            else if (r.kind != static_cast<std::uint8_t>(LogicKind::Beam) &&
                     r.kind != static_cast<std::uint8_t>(LogicKind::Speaker))
            {
                // Anyone else's holds names (a beam's: places; a speaker's:
                // sentences, above).
                for (std::uint32_t a = 0; a < r.auxCount; ++a)
                {
                    if (logicAux[r.firstAux + a].target >= nameCount)
                        return LevelError::BadIndex;
                }
            }
        }
        return LevelError::Ok;
    }
} // namespace hldata
