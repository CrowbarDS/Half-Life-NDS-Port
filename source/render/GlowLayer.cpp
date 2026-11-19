#include "global_header.hpp"

#include "render/GlowLayer.hpp"

#include <cstring>

#include "HlMonsters.hpp"
#include "HlLevel.hpp"
#include "HlMovers.hpp"
#include "HlSprite.hpp"
#include "data/LevelFile.hpp"
#include "data/SpriteCache.hpp"
#include "game/Game.hpp"
#include "render/Camera.hpp"
#include "render/HudDraw.hpp"
#include "render/ModelDraw.hpp"
#include "render/WorldDraw.hpp"

namespace hlrt
{
    GlowLayer glowLayer;

    namespace
    {
        using namespace hldata;

        /// A pixel of the screen is this many sixteenths of a unit across,
        /// at this many sixteenths in front of the eye (HudDraw's flat
        /// pictures: camera.hudPixel and hudPixelX are the first).
        constexpr int kDepth = 3058;

        bool isGlow(const LogicRecord &record)
        {
            return record.kind == static_cast<std::uint8_t>(LogicKind::Sprite) && (record.extra[3] & 2);
        }

        /// A sprite cooked as light: its name ends in "-l" (Converter.cpp),
        /// its alpha is how bright it is.
        bool cookedAsLight(const SpriteRef &ref)
        {
            std::size_t length = 0;
            while (length < sizeof(ref.name) && ref.name[length] != '\0')
                ++length;
            return length > 2 && ref.name[length - 2] == '-' && ref.name[length - 1] == 'l';
        }

        /// One glow that may be shown this pass.
        struct Candidate
        {
            std::int32_t dist;
            std::int16_t index;
            std::int16_t x, y;   ///< its middle on the screen
            std::int16_t halfX, halfY; ///< pixels either way
            std::uint16_t colour; ///< RGB555, at full
            std::uint8_t alpha;   ///< 1..31
        };
        constexpr int kCandidates = 24;
        /// The nearest this many are tried against the world; the rest are
        /// left to the 3D picture.
        constexpr int kTraced = 12;

        /// One colour's channel times another's (each 0..31).
        int timesChannel(int a, int b) { return (a * b + 15) / 31; }
    } // namespace

    int GlowLayer::chooseSprite()
    {
        forget();
        // The sprite most of the map's glows are -- if it was cooked as light.
        const LevelView &view = level.view;
        const int records = static_cast<int>(view.logicCount);
        int best = -1, bestCount = 0;
        for (int i = 0; i < records; ++i)
        {
            const LogicRecord &record = view.logic[i];
            if (!isGlow(record))
                continue;
            const int sprite = static_cast<int>(record.extra[0]);
            if (sprite == best)
                continue;
            int count = 0;
            for (int k = i; k < records; ++k)
                count += isGlow(view.logic[k]) && static_cast<int>(view.logic[k].extra[0]) == sprite;
            if (count > bestCount)
            {
                best = sprite;
                bestCount = count;
            }
        }
        if (best < 0 || best >= static_cast<int>(level.view.spriteCount) || !cookedAsLight(level.view.sprites[best]))
            return -1;
        sprite_ = best;
        return best;
    }

    void GlowLayer::takePicture(const TextureHeader &header, const std::uint8_t *body, void *context)
    {
        GlowLayer &self = *static_cast<GlowLayer *>(context);
        if (self.sprite_ < 0 || !(header.flags & kTextureA3I5))
            return;
        const SpriteRef &ref = level.view.sprites[self.sprite_];
        const bool a5 = (header.flags & kTextureA5I3) != 0;
        const int width = header.width, cellW = ref.cellWidth, cellH = ref.cellHeight;
        if (cellW < 1 || cellH < 1 || cellW > width || cellH > header.height || header.paletteColours < 1)
            return;
        const std::uint8_t *texels = body;
        const auto *palette = reinterpret_cast<const std::uint16_t *>(body + header.texelBytes + header.indexBytes);
        // How solid, of 31, and which colour: its first frame, top left.
        auto alphaAt = [&](int x, int y)
        {
            const int t = texels[y * width + x];
            return a5 ? t >> 3 : (t >> 5) * 31 / 7;
        };
        // Its colour: the average of its texels', by how bright each is.
        std::uint32_t sum[3] = {0, 0, 0}, weight = 0;
        for (int y = 0; y < cellH; ++y)
        {
            for (int x = 0; x < cellW; ++x)
            {
                const int t = texels[y * width + x];
                const int index = a5 ? t & 7 : t & 31;
                const int a = alphaAt(x, y);
                if (a == 0 || index >= header.paletteColours)
                    continue;
                const std::uint16_t c = palette[index];
                for (int k = 0; k < 3; ++k)
                    sum[k] += static_cast<std::uint32_t>((c >> (5 * k)) & 31) * a;
                weight += static_cast<std::uint32_t>(a);
            }
        }
        if (weight == 0)
            return;
        int channel[3], most = 1;
        for (int k = 0; k < 3; ++k)
        {
            channel[k] = static_cast<int>(sum[k] / weight);
            most = channel[k] > most ? channel[k] : most;
        }
        self.tint_ = 0;
        for (int k = 0; k < 3; ++k)
            self.tint_ = static_cast<std::uint16_t>(self.tint_ | ((channel[k] * 31 / most) << (5 * k)));
        // Its shape: the frame stretched to the picture's 32 by 32, each
        // pixel between the four texels nearest (in 256ths), in 16 levels.
        std::uint8_t levels[kSide * kSide];
        for (int y = 0; y < kSide; ++y)
        {
            int v = ((2 * y + 1) * cellH * 128) / kSide - 128;
            v = v < 0 ? 0 : (v > (cellH - 1) * 256 ? (cellH - 1) * 256 : v);
            const int y0 = v >> 8, y1 = y0 + 1 < cellH ? y0 + 1 : y0, fy = v & 255;
            for (int x = 0; x < kSide; ++x)
            {
                int u = ((2 * x + 1) * cellW * 128) / kSide - 128;
                u = u < 0 ? 0 : (u > (cellW - 1) * 256 ? (cellW - 1) * 256 : u);
                const int x0 = u >> 8, x1 = x0 + 1 < cellW ? x0 + 1 : x0, fx0 = u & 255;
                const int top = alphaAt(x0, y0) * (256 - fx0) + alphaAt(x1, y0) * fx0;
                const int bottom = alphaAt(x0, y1) * (256 - fx0) + alphaAt(x1, y1) * fx0;
                const int a = (top * (256 - fy) + bottom * fy) >> 16; // 0..31
                levels[y * kSide + x] = static_cast<std::uint8_t>((a * 15 + 15) / 31);
            }
        }
        hudDraw.setGlowShape(levels);
        self.picture_ = true;
    }

    void GlowLayer::forget()
    {
        sprite_ = -1;
        picture_ = false;
        tint_ = 0x7FFF;
        takenCount_ = 0;
        shadowCount_ = 0;
        hudDraw.setGlowShape(nullptr);
    }

    bool GlowLayer::taken(int index) const
    {
        for (int k = 0; k < takenCount_; ++k)
        {
            if (taken_[k] == index)
                return true;
        }
        return false;
    }

    void GlowLayer::choose()
    {
        takenCount_ = 0;
        shadowCount_ = 0;
        if (!picture_ || sprite_ < 0 || !hudDraw.spritesLive())
            return;
        const SpriteRef *ref = spriteCache.ref(static_cast<unsigned>(sprite_));
        if (ref == nullptr)
            return;

        // The glows of the picture's sprite on the screen, nearest first.
        const LogicWorld &logic = game.logic;
        Candidate list[kCandidates];
        int count = 0;
        for (int i = 0; i < logic.recordCount; ++i)
        {
            const LogicRecord &record = logic.records[i];
            if (!isGlow(record) || static_cast<int>(record.extra[0]) != sprite_ || !effectSeen(record, logic.states[i]))
                continue;
            const std::int32_t d[3] = {fxToInt(record.move[0] - camera.pos[0]), fxToInt(record.move[1] - camera.pos[1]),
                                       fxToInt(record.move[2] - camera.pos[2])};
            const std::int32_t depth = (d[0] * camera.forward[0] + d[1] * camera.forward[1] + d[2] * camera.forward[2]) >> 12;
            if (depth < 8)
                continue;
            const std::int32_t across = (d[0] * camera.right[0] + d[1] * camera.right[1] + d[2] * camera.right[2]) >> 12;
            const std::int32_t up = (d[0] * camera.up[0] + d[1] * camera.up[1] + d[2] * camera.up[2]) >> 12;
            const std::int32_t perX = camera.hudPixelX * depth, perY = camera.hudPixel * depth;
            const int x = 128 + across * kDepth / perX, y = 96 - up * kDepth / perY;
            // (The game's: nothing of it while its middle is off the screen.)
            if (x < 0 || x >= 256 || y < 0 || y >= 192)
            {
                if (takenCount_ < kTaken)
                    taken_[takenCount_++] = static_cast<std::int16_t>(i);
                continue;
            }
            const int dist = static_cast<int>(isqrt64(static_cast<std::uint64_t>(
                static_cast<std::int64_t>(d[0]) * d[0] + static_cast<std::int64_t>(d[1]) * d[1] +
                static_cast<std::int64_t>(d[2]) * d[2])));
            // Its size and brightness as the 3D glow has them (drawRecord).
            const int scale = static_cast<int>(record.extra[2] >> 24); // Q4
            int halfWide = ref->worldWidth * scale / 32, halfHigh = ref->worldHeight * scale / 32;
            int alpha = effectAlpha(record);
            if (!(record.extra[3] & 8))
            {
                int grow, bright;
                glowFalloff(dist, grow, bright);
                halfWide = (halfWide * grow) >> 8;
                halfHigh = (halfHigh * grow) >> 8;
                alpha = (alpha * bright + 128) >> 8;
            }
            if (alpha < 1)
                continue;
            const int halfX = halfWide * kDepth / perX, halfY = halfHigh * kDepth / perY;
            // (Past 32 pixels either way a sprite's matrix cannot show it
            // whole: the 3D picture's.)
            if (halfX > 32 || halfY > 32 || count == kCandidates)
                continue;
            Candidate c;
            c.dist = dist;
            c.index = static_cast<std::int16_t>(i);
            c.x = static_cast<std::int16_t>(x);
            c.y = static_cast<std::int16_t>(y);
            c.halfX = static_cast<std::int16_t>(halfX < 1 ? 1 : halfX);
            c.halfY = static_cast<std::int16_t>(halfY < 1 ? 1 : halfY);
            c.alpha = static_cast<std::uint8_t>(alpha > 31 ? 31 : alpha);
            const std::uint16_t own = effectColour(record);
            c.colour = 0;
            for (int k = 0; k < 3; ++k)
                c.colour = static_cast<std::uint16_t>(
                    c.colour | timesChannel((own >> (5 * k)) & 31, (tint_ >> (5 * k)) & 31) << (5 * k));
            int at = count++;
            while (at > 0 && list[at - 1].dist > dist)
            {
                list[at] = list[at - 1];
                --at;
            }
            list[at] = c;
        }

        // What is held, where it is on the screen: a glow there would be
        // over it (the 2D engine is in front of all the 3D picture).
        int gun[4];
        const bool held = modelDraw.heldBox(gun);

        // Each, nearest first: seen (or hidden) by the game's rule, then
        // given a sprite and one of the palettes, while there are any.
        std::uint16_t bankColour[kBanks];
        std::uint8_t bankAlpha[kBanks];
        int banks = 0;
        const int traced = count < kTraced ? count : kTraced;
        for (int n = 0; n < traced && takenCount_ < kTaken; ++n)
        {
            const Candidate &c = list[n];
            if (held && c.x + c.halfX > gun[0] && c.x - c.halfX < gun[2] && c.y + c.halfY > gun[1] &&
                c.y - c.halfY < gun[3])
                continue;
            // The game's: hidden if the line from the eye stops more than 8
            // units short of it; glass and models do not stop it.
            const LogicRecord &record = logic.records[c.index];
            bool seen = true;
            if (c.dist > 8)
            {
                fx end[3];
                for (int k = 0; k < 3; ++k)
                {
                    // (In 32 bits: a map is under 16384 units across.)
                    end[k] = record.move[k] + (camera.pos[k] - record.move[k]) * 8 / c.dist;
                }
                seen = worldDraw.sightClear(camera.pos, end);
            }
            if (!seen)
            {
                taken_[takenCount_++] = c.index;
                continue;
            }
            if (shadowCount_ == kSprites)
                continue;
            // A palette: one of the same colour and nearly the same
            // brightness, or a new one.
            int bank = -1;
            for (int b = 0; b < banks && bank < 0; ++b)
            {
                const int apart = bankAlpha[b] > c.alpha ? bankAlpha[b] - c.alpha : c.alpha - bankAlpha[b];
                if (bankColour[b] == c.colour && apart <= 2)
                    bank = b;
            }
            if (bank < 0)
            {
                if (banks == kBanks)
                    continue;
                bank = banks++;
                bankColour[bank] = c.colour;
                bankAlpha[bank] = c.alpha;
                for (int step = 1; step <= 15; ++step)
                {
                    std::uint16_t out = 0;
                    for (int k = 0; k < 3; ++k)
                    {
                        // (channel * alpha / 31 * step / 15: 141 / 65536 is 1 / 465.)
                        int v = (((c.colour >> (5 * k)) & 31) * c.alpha * step * 141) >> 16;
                        v = v > 31 ? 31 : v;
                        out = static_cast<std::uint16_t>(out | (v << (5 * k)));
                    }
                    shadowColour_[bank][step - 1] = out;
                }
            }
            taken_[takenCount_++] = c.index;
            // A 32 by 32 sprite, affine, shown in a box twice its size (so
            // up to twice as big), added to the picture; behind the display.
            const int k = shadowCount_++;
            std::uint16_t *s = shadow_[k];
            s[0] = static_cast<std::uint16_t>(((c.y - kSide) & 0xFF) | 0x0100 | 0x0200 | 0x0400);
            s[1] = static_cast<std::uint16_t>(((c.x - kSide) & 0x1FF) | ((kFirstMatrix + k) << 9) | (2 << 14));
            s[2] = static_cast<std::uint16_t>(HudDraw::kGlowTile | ATTR2_PRIORITY(1) | ((kFirstBank + bank) << 12));
            // (Its matrix: how far through the picture a pixel of the screen
            // steps, 8.8: 32 texels over 2 * half pixels.)
            s[3] = static_cast<std::uint16_t>(4096 / c.halfX);
            matrixY_[k] = static_cast<std::uint16_t>(4096 / c.halfY);
        }
        banks_ = banks;
    }

    void GlowLayer::publish(bool shown)
    {
        const int state = enterCriticalSection();
        shownCount_ = shown ? shadowCount_ : 0;
        std::memcpy(shown_, shadow_, sizeof(shown_));
        std::memcpy(shownMatrixY_, matrixY_, sizeof(shownMatrixY_));
        std::memcpy(shownColour_, shadowColour_, sizeof(shownColour_));
        shownBanks_ = shown ? banks_ : 0;
        leaveCriticalSection(state);
        // (Shown once: a frame that chooses nothing shows nothing.)
        shadowCount_ = 0;
        banks_ = 0;
    }

    void GlowLayer::onVblank()
    {
        auto *oam = reinterpret_cast<volatile std::uint16_t *>(0x07000000);
        for (int k = 0; k < kSprites; ++k)
        {
            volatile std::uint16_t *entry = oam + (kFirstSprite + k) * 4;
            if (k >= shownCount_)
            {
                entry[0] = 0x0200; // (not affine: out of sight)
                continue;
            }
            entry[0] = shown_[k][0];
            entry[1] = shown_[k][1];
            entry[2] = shown_[k][2];
            // The matrix: its four numbers in the fourth word of four
            // sprites running.
            volatile std::uint16_t *matrix = oam + (kFirstMatrix + k) * 16 + 3;
            matrix[0] = shown_[k][3];
            matrix[4] = 0;
            matrix[8] = 0;
            matrix[12] = shownMatrixY_[k];
        }
        for (int b = 0; b < shownBanks_; ++b)
        {
            for (int step = 0; step < 15; ++step)
                SPRITE_PALETTE[(kFirstBank + b) * 16 + 1 + step] = shownColour_[b][step];
        }
    }
} // namespace hlrt
