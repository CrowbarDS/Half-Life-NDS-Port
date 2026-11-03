#include "render/ModelDraw.hpp"
#include "render/EffectDraw.hpp"

#include <nds.h>

#include <NEAMain.h>

#include "Global.hpp"
#include "HlDsp.hpp"
#include "HlSprite.hpp"
#include "HlUi.hpp"
#include "HlPolygonIds.hpp"
#include "data/TextureCache.hpp"
#include "data/TextureStream.hpp"
#include "render/HudDraw.hpp"
#include "data/SpriteCache.hpp"
#include "game/Game.hpp"
#include "game/Player.hpp"
#include "render/Camera.hpp"
#include "render/WorldDraw.hpp"
#include "HlNetGame.hpp"
#include "HlSettings.hpp"
#include "net/NetGame.hpp"

namespace hlrt
{
    EffectDraw effectDraw;

    namespace
    {
        using namespace hldata;

        /// A corner, in the world (Q12 units), as the level's vertices are
        /// sent: quarter units.
        void corner(const fx *p)
        {
            GFX_VERTEX16 = ((static_cast<std::uint32_t>(p[1] >> 10) & 0xFFFF) << 16) |
                           (static_cast<std::uint32_t>(p[0] >> 10) & 0xFFFF);
            GFX_VERTEX16 = static_cast<std::uint32_t>(p[2] >> 10) & 0xFFFF;
        }

        /// A square of half-width `r` units at `c`, across the directions
        /// `u` and `v` (Q12, unit).
        void square(const fx *c, const std::int32_t *u, const std::int32_t *v, int r)
        {
            fx p[4][3];
            for (int i = 0; i < 3; ++i)
            {
                p[0][i] = c[i] - u[i] * r - v[i] * r;
                p[1][i] = c[i] + u[i] * r - v[i] * r;
                p[2][i] = c[i] + u[i] * r + v[i] * r;
                p[3][i] = c[i] - u[i] * r + v[i] * r;
            }
            GFX_BEGIN = GL_QUADS;
            for (const fx *point : p)
                corner(point);
        }

        /// A strip `w` units either side of the line from `a` to `b`, across
        /// the direction `u`.
        void strip(const fx *a, const fx *b, const std::int32_t *u, int w)
        {
            fx p[4][3];
            for (int i = 0; i < 3; ++i)
            {
                p[0][i] = a[i] - u[i] * w;
                p[1][i] = a[i] + u[i] * w;
                p[2][i] = b[i] + u[i] * w;
                p[3][i] = b[i] - u[i] * w;
            }
            GFX_BEGIN = GL_QUADS;
            for (const fx *point : p)
                corner(point);
        }

        void look(int alpha, int id, std::uint16_t colour)
        {
            NEA_PolyFormat(alpha, id, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE,
                           worldDraw.polygonMode());
            GFX_COLOR = colour;
        }

        /// The number the thing being drawn is drawn under: two see-through
        /// polygons of the same number do not blend over one another, and
        /// these are meant to. The caller hands them out, one to each thing
        /// in the frame's one list of what is see-through.
        int currentId = 1;
        int freshId()
        {
            return currentId;
        }

        /// Bind one of the map's sprites, to be drawn `alpha` solid (1..31)
        /// and tinted `colour`. Null if the map has not got it: then nothing
        /// is bound, and the caller draws a plain shape or nothing.
        const SpriteRef *sprite(unsigned index, int alpha, std::uint16_t colour)
        {
            const SpriteRef *ref = spriteCache.ref(index);
            if (ref == nullptr)
            {
                GFX_TEX_FORMAT = 0;
                return nullptr;
            }
            NEA_MaterialUse(spriteCache.material(index));
            look(alpha < 1 ? 1 : (alpha > 31 ? 31 : alpha), freshId(), colour);
            return ref;
        }
        const SpriteRef *sprite(CommonSprite which, int alpha, std::uint16_t colour = 0x7FFF)
        {
            return sprite(static_cast<unsigned>(which), alpha, colour);
        }

        /// The corners of frame `frame` in the sprite's atlas, in texels.
        void cell(const SpriteRef &ref, int frame, int &s0, int &t0, int &s1, int &t1)
        {
            frame = frame < 0 ? 0 : (frame >= ref.frames ? ref.frames - 1 : frame);
            s0 = (frame % ref.columns) * ref.cellWidth;
            t0 = (frame / ref.columns) * ref.cellHeight;
            s1 = s0 + ref.cellWidth;
            t1 = t0 + ref.cellHeight;
        }

        /// Frame `frame` of the bound sprite on a rectangle at `c`, `halfU`
        /// units either way along `u` and `halfV` along `v` (Q12, unit; `v`
        /// is the picture's "up").
        void picture(const SpriteRef &ref, int frame, const fx *c, const std::int32_t *u,
                     const std::int32_t *v, int halfU, int halfV)
        {
            int s0, t0, s1, t1;
            cell(ref, frame, s0, t0, s1, t1);
            const int st[4][2] = {{s0, t1}, {s1, t1}, {s1, t0}, {s0, t0}};
            const int sign[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            GFX_BEGIN = GL_QUADS;
            for (int k = 0; k < 4; ++k)
            {
                fx p[3];
                for (int i = 0; i < 3; ++i)
                    p[i] = c[i] + u[i] * halfU * sign[k][0] + v[i] * halfV * sign[k][1];
                GFX_TEX_COORD = TEXTURE_PACK(st[k][0] << 4, st[k][1] << 4);
                corner(p);
            }
        }

        /// The direction across a strip from `a` to `b` that turns it to
        /// the eye (Q12, unit).
        void across(const fx *a, const fx *b, std::int32_t *out)
        {
            const fx d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const fx e[3] = {camera.pos[0] - a[0], camera.pos[1] - a[1], camera.pos[2] - a[2]};
            // (In whole units: the products of two Q12 lengths are too long.)
            const std::int32_t dx = d[0] >> 12, dy = d[1] >> 12, dz = d[2] >> 12;
            const std::int32_t ex = e[0] >> 12, ey = e[1] >> 12, ez = e[2] >> 12;
            fx n[3] = {(dy * ez - dz * ey) << 4, (dz * ex - dx * ez) << 4, (dx * ey - dy * ex) << 4};
            const fx length = fxLength(n);
            if (length < 16)
            {
                out[0] = camera.up[0];
                out[1] = camera.up[1];
                out[2] = camera.up[2];
                return;
            }
            for (int i = 0; i < 3; ++i)
                out[i] = static_cast<std::int32_t>((static_cast<std::int64_t>(n[i]) << 12) / length);
        }

        /// The bound sprite's frame `frame` as a strip from `a` to `b`, `w`
        /// sixteenths of a unit either side, turned to the eye: its picture
        /// runs along the strip.
        void beam(const SpriteRef &ref, int frame, const fx *a, const fx *b, int w)
        {
            std::int32_t u[3];
            across(a, b, u);
            int s0, t0, s1, t1;
            cell(ref, frame, s0, t0, s1, t1);
            fx p[4][3];
            for (int i = 0; i < 3; ++i)
            {
                const fx side = (u[i] * w) >> 4;
                p[0][i] = a[i] - side;
                p[1][i] = a[i] + side;
                p[2][i] = b[i] + side;
                p[3][i] = b[i] - side;
            }
            const int st[4][2] = {{s0, t0}, {s1, t0}, {s1, t1}, {s0, t1}};
            GFX_BEGIN = GL_QUADS;
            for (int k = 0; k < 4; ++k)
            {
                GFX_TEX_COORD = TEXTURE_PACK(st[k][0] << 4, st[k][1] << 4);
                corner(p[k]);
            }
        }

        std::uint32_t jitterSeed = 1;
        int jitter(int range)
        {
            jitterSeed = jitterSeed * 1664525u + 1013904223u;
            return static_cast<int>((jitterSeed >> 16) % (2 * range + 1)) - range;
        }

        /// A strip that is not straight: lightning, in four pieces, each
        /// joint thrown `noise` units off the line, differently every frame.
        void lightning(const SpriteRef &ref, const fx *a, const fx *b, int w, int noise)
        {
            fx from[3] = {a[0], a[1], a[2]};
            const int frame = ref.frames > 1 ? static_cast<int>(Global::drawClock() / 3 % ref.frames) : 0;
            for (int piece = 1; piece <= 4; ++piece)
            {
                fx to[3];
                for (int i = 0; i < 3; ++i)
                {
                    to[i] = a[i] + ((b[i] - a[i]) >> 2) * piece;
                    if (piece < 4)
                        to[i] += fxFromInt(jitter(noise));
                }
                beam(ref, frame, from, to, w);
                for (int i = 0; i < 3; ++i)
                    from[i] = to[i];
            }
        }
    } // namespace

    void EffectDraw::glow(const fx *at, int alpha, std::uint16_t colour, int half)
    {
        if (glowCount_ >= kMaxGlows || alpha < 1)
            return;
        Glow &g = glows_[glowCount_++];
        for (int k = 0; k < 3; ++k)
            g.at[k] = at[k];
        g.colour = colour;
        g.alpha = static_cast<std::uint8_t>(alpha > 31 ? 31 : alpha);
        g.half = static_cast<std::uint8_t>(half);
    }

    // Everything here is see-through, and what is see-through has to be
    // drawn far to near to look right -- with the glass, which is the
    // brushes' (WorldDraw). So it is done in two steps. gather(), once the
    // parts of the map in view are known and before the world is chosen,
    // lists what there is to draw, farthest first, and says what it will
    // cost: the world gives up that room. Then WorldDraw draws its glass and
    // these in one order, asking for each of these by its place in the list.

    void EffectDraw::gather(int &polygons, int &vertices)
    {
        count_ = 0;
        refused_ = 0;
        if (player.noclip)
            return;
        const ActorWorld &a = game.actors;
        const LogicWorld &logic = game.logic;
        const int detail = worldDraw.detailRatio();
        // `size` is about how big it is (units): under pressure what is
        // small and far is left out, as with everything else.
        auto add = [&](std::uint8_t type, int index, const fx *at, int size, int quads)
        {
            const std::int32_t d[3] = {fxToInt(at[0] - camera.pos[0]), fxToInt(at[1] - camera.pos[1]),
                                       fxToInt(at[2] - camera.pos[2])};
            if (detail != 0)
            {
                const std::int32_t depth =
                    (d[0] * camera.forward[0] + d[1] * camera.forward[1] + d[2] * camera.forward[2]) >> 12;
                if (depth > size * detail)
                    return;
            }
            if (count_ == kMaxItems)
            {
                ++refused_;
                return;
            }
            // (The same measure the glass is sorted by.)
            const std::int32_t distance = (d[0] >> 1) * (d[0] >> 1) + (d[1] >> 1) * (d[1] >> 1) + (d[2] >> 1) * (d[2] >> 1);
            int slot = count_++;
            while (slot > 0 && items_[slot - 1].distance < distance)
            {
                items_[slot] = items_[slot - 1];
                --slot;
            }
            items_[slot] = Item{distance, static_cast<std::uint16_t>(index), type};
            polygons += quads;
            vertices += quads * 4;
        };
        // Blood, a gun's flash, a burst: what tells the player what is
        // happening to whom comes first (the list has a limit, and what
        // misses it is the map's decoration, not these), and is not left out
        // for being small and far. Only what is in view, though.
        for (int n = 0; n < kMaxEffects; ++n)
        {
            const Effect &effect = a.effects[n];
            if (effect.kind == 0 || effect.wholeMs <= 0)
                continue;
            const bool line = effect.kind == kEffectBolt || effect.kind == kEffectTracer;
            const fx middle[3] = {line ? (effect.a[0] + effect.b[0]) / 2 : effect.a[0],
                                  line ? (effect.a[1] + effect.b[1]) / 2 : effect.a[1],
                                  line ? (effect.a[2] + effect.b[2]) / 2 : effect.a[2]};
            const std::int16_t a16[3] = {static_cast<std::int16_t>(fxToInt(effect.a[0])),
                                         static_cast<std::int16_t>(fxToInt(effect.a[1])),
                                         static_cast<std::int16_t>(fxToInt(effect.a[2]))};
            const int reach = effect.size > 16 ? effect.size : 16;
            if (line)
            {
                const std::int16_t b16[3] = {static_cast<std::int16_t>(fxToInt(effect.b[0])),
                                             static_cast<std::int16_t>(fxToInt(effect.b[1])),
                                             static_cast<std::int16_t>(fxToInt(effect.b[2]))};
                if (!worldDraw.ballVisible(a16, 32) && !worldDraw.ballVisible(b16, 32))
                    continue;
            }
            else if (!worldDraw.ballVisible(a16, reach))
            {
                continue;
            }
            add(kEffect, n, middle, 1 << 14, effect.kind == kEffectBolt ? 4 : 1);
        }
        for (int i = 0; i < logic.recordCount; ++i)
        {
            const LogicRecord &record = logic.records[i];
            if ((record.kind != static_cast<std::uint8_t>(LogicKind::Sprite) &&
                 record.kind != static_cast<std::uint8_t>(LogicKind::Beam)) ||
                !effectSeen(record, logic.states[i]))
                continue;
            if (record.kind == static_cast<std::uint8_t>(LogicKind::Sprite))
            {
                const SpriteRef *known = spriteCache.ref(record.extra[0]);
                if (known == nullptr)
                    continue;
                const std::int16_t at[3] = {static_cast<std::int16_t>(fxToInt(record.move[0])),
                                            static_cast<std::int16_t>(fxToInt(record.move[1])),
                                            static_cast<std::int16_t>(fxToInt(record.move[2]))};
                const int scale = static_cast<int>(record.extra[2] >> 24); // Q4
                const int half = (known->worldWidth > known->worldHeight ? known->worldWidth : known->worldHeight) *
                                 scale / 32;
                if (worldDraw.ballVisible(at, half))
                    add(kRecord, i, record.move, half, 1);
            }
            else
            {
                fx from[3], to[3];
                beamEnds(logic, i, from, to);
                const std::int16_t a16[3] = {static_cast<std::int16_t>(fxToInt(from[0])),
                                             static_cast<std::int16_t>(fxToInt(from[1])),
                                             static_cast<std::int16_t>(fxToInt(from[2]))};
                const std::int16_t b16[3] = {static_cast<std::int16_t>(fxToInt(to[0])),
                                             static_cast<std::int16_t>(fxToInt(to[1])),
                                             static_cast<std::int16_t>(fxToInt(to[2]))};
                if (!worldDraw.ballVisible(a16, 32) && !worldDraw.ballVisible(b16, 32))
                    continue;
                const fx middle[3] = {(from[0] + to[0]) / 2, (from[1] + to[1]) / 2, (from[2] + to[2]) / 2};
                const fx span[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
                add(kRecord, i, middle, fxToInt(fxLength(span)) / 2 + 8,
                    (record.maxs[1] & kBeamRing) ? 12 : ((record.extra[1] >> 16) != 0 ? 4 : 1));
            }
        }
        for (int n = 0; n < kMaxProjectiles; ++n)
        {
            if (!a.shots[n].active)
                continue;
            const std::int16_t at[3] = {static_cast<std::int16_t>(fxToInt(a.shots[n].pos[0])),
                                        static_cast<std::int16_t>(fxToInt(a.shots[n].pos[1])),
                                        static_cast<std::int16_t>(fxToInt(a.shots[n].pos[2]))};
            if (worldDraw.ballVisible(at, 16))
                add(kShot, n, a.shots[n].pos, 8, 1);
        }
        // The beams of the tripmines that are armed.
        for (int i = 0; i < a.count; ++i)
        {
            const ActorState &mine = a.states[i];
            if (!mine.active || !isMine(mine.kind) || mine.ai != kMineArmed)
                continue;
            const std::int16_t a16[3] = {static_cast<std::int16_t>(fxToInt(mine.pos[0])),
                                         static_cast<std::int16_t>(fxToInt(mine.pos[1])),
                                         static_cast<std::int16_t>(fxToInt(mine.pos[2]))};
            const std::int16_t b16[3] = {static_cast<std::int16_t>(fxToInt(mine.goal[0])),
                                         static_cast<std::int16_t>(fxToInt(mine.goal[1])),
                                         static_cast<std::int16_t>(fxToInt(mine.goal[2]))};
            if (!worldDraw.ballVisible(a16, 32) && !worldDraw.ballVisible(b16, 32))
                continue;
            const fx middle[3] = {(mine.pos[0] + mine.goal[0]) / 2, (mine.pos[1] + mine.goal[1]) / 2,
                                  (mine.pos[2] + mine.goal[2]) / 2};
            const fx span[3] = {mine.goal[0] - mine.pos[0], mine.goal[1] - mine.pos[1], mine.goal[2] - mine.pos[2]};
            add(kMine, i, middle, fxToInt(fxLength(span)) / 2 + 8, 1);
        }
        // The small things that are see-through (broken glass): they have
        // to take their turn with everything else that is. (The solid ones
        // need no turn: drawSolid.)
        for (int n = 0; n < kMaxPieces; ++n)
        {
            const Piece &piece = a.pieces[n];
            if (piece.kind != kPieceShard || piece.alpha >= 31 || !pieceSeen(piece))
                continue;
            const std::int16_t at[3] = {static_cast<std::int16_t>(fxToInt(piece.pos[0])),
                                        static_cast<std::int16_t>(fxToInt(piece.pos[1])),
                                        static_cast<std::int16_t>(fxToInt(piece.pos[2]))};
            if (worldDraw.ballVisible(at, 16))
                add(kPiece, n, piece.pos, piece.half * 40, 1);
        }
    }

    int EffectDraw::begin()
    {
        if (player.noclip)
        {
            glowCount_ = 0;
            count_ = 0;
            return 0;
        }
        // What the models drawn since gather() said glows (a turret's eye)
        // joins the list, each in its place.
        for (int n = 0; n < glowCount_ && count_ < kMaxItems; ++n)
        {
            const Glow &g = glows_[n];
            const std::int32_t d[3] = {fxToInt(g.at[0] - camera.pos[0]), fxToInt(g.at[1] - camera.pos[1]),
                                       fxToInt(g.at[2] - camera.pos[2])};
            const std::int32_t distance = (d[0] >> 1) * (d[0] >> 1) + (d[1] >> 1) * (d[1] >> 1) + (d[2] >> 1) * (d[2] >> 1);
            int slot = count_++;
            while (slot > 0 && items_[slot - 1].distance < distance)
            {
                items_[slot] = items_[slot - 1];
                --slot;
            }
            items_[slot] = Item{distance, static_cast<std::uint16_t>(n), kGlow};
        }
        jitterSeed = Global::drawClock() * 2654435761u + 1;
        return count_;
    }

    void EffectDraw::drawItem(int n, int id)
    {
        // (Whatever was drawn before -- a pane of glass, as like as not --
        // may have left the hardware set for itself.)
        camera.apply();
        currentId = id;
        const Item &item = items_[n];
        switch (item.type)
        {
        case kRecord: drawRecord(item.index); break;
        case kShot:   drawShot(item.index); break;
        case kEffect: drawEffect(item.index); break;
        case kMine:   drawMine(item.index); break;
        case kPiece:  drawPiece(item.index); break;
        default:      drawGlow(item.index); break;
        }
    }

    /// Whether a small thing is near enough to be worth a polygon: the
    /// nearer, the heavier the frame is.
    bool EffectDraw::pieceSeen(const Piece &piece) const
    {
        // (An enemy that had to be made room for counts as a step more.)
        const int pressure = worldDraw.pressure() + (modelDraw.strained() ? 1 : 0);
        if (piece.kind == kPieceSpeck && pressure > 0)
            return false;
        const std::int32_t d[3] = {fxToInt(piece.pos[0] - camera.pos[0]), fxToInt(piece.pos[1] - camera.pos[1]),
                                   fxToInt(piece.pos[2] - camera.pos[2])};
        const std::int32_t depth = (d[0] * camera.forward[0] + d[1] * camera.forward[1] + d[2] * camera.forward[2]) >> 12;
        const int reach = pressure >= 2 ? 150 : (pressure == 1 || piece.kind != kPieceShard ? 300 : 700);
        return depth >= 4 && depth <= reach;
    }

    /// A small thing's three corners. It is one triangle (a shard is; a
    /// speck and a casing are too small to tell): turned to the eye, spun
    /// about the view and tipped edge-on and back as it tumbles. Textured
    /// with a patch of its texture, if it has one.
    void EffectDraw::pieceCorners(const Piece &piece, bool textured)
    {
        fx sine, cosine, tip, unused;
        fxSinCos(piece.spin, sine, cosine);
        // (Lying, it is flat to the eye; flying, it shows its edge and its
        // face by turns.)
        fxSinCos(static_cast<std::uint16_t>(piece.spin * 3), unused, tip);
        // (Sixteenths of a unit. A casing is smaller than a unit.)
        const bool casing = piece.kind == kPieceShell || piece.kind == kPieceShotShell || piece.kind == kPieceCan;
        const int wide = casing ? 6 : piece.half * 16;
        int high = piece.bounces >= 2 ? wide : fxMulShort(wide, tip < 0 ? -tip : tip); // (both 16-bit)
        if (casing)
            high = wide * 3;
        high = high < 4 ? 4 : high;
        const int s0 = piece.s * 8, t0 = piece.t * 8;
        const int st[3][2] = {{s0, t0 + 16}, {s0 + 16, t0 + 16}, {s0 + 8, t0}};
        const int across[3] = {-1, 1, 0}, up[3] = {-1, -1, 1};
        for (int k = 0; k < 3; ++k)
        {
            fx p[3];
            for (int i = 0; i < 3; ++i)
            {
                const std::int32_t u = (camera.right[i] * cosine + camera.up[i] * sine) >> 12;
                const std::int32_t v = (camera.up[i] * cosine - camera.right[i] * sine) >> 12;
                p[i] = piece.pos[i] + ((u * wide * across[k] + v * high * up[k]) >> 4);
            }
            if (textured)
                GFX_TEX_COORD = TEXTURE_PACK(st[k][0] << 4, st[k][1] << 4);
            corner(p);
        }
    }

    /// One that is see-through (in its turn among what else is).
    void EffectDraw::drawPiece(int n)
    {
        const Piece &piece = game.actors.pieces[n];
        if (piece.kind == 0 || modelDraw.pieceModelled(n))
            return;
        // (In its last third of a second it thins away.)
        int alpha = piece.alpha;
        if (piece.leftMs < 320)
            alpha = alpha * piece.leftMs / 320;
        alpha = alpha < 1 ? 1 : (alpha > 31 ? 31 : alpha);
        bool textured = false;
        if (piece.texture != 0xFFFF)
        {
            noteTextureDrawn(piece.texture);
            textured = textureCache.use(piece.texture);
        }
        else
            GFX_TEX_FORMAT = 0;
        look(alpha, freshId(), piece.colour);
        GFX_BEGIN = GL_TRIANGLES;
        pieceCorners(piece, textured);
    }

    /// The small things that are solid -- specks, casings, the pieces of
    /// what is not glass -- which are most of them. Solid things need no
    /// turn in the far-to-near list and no polygon number, so they are sent
    /// in runs that share everything: the plain ones in one, then those of
    /// each texture together. Returns how many triangles that was.
    int EffectDraw::drawSolid()
    {
        const ActorWorld &a = game.actors;
        if (player.noclip)
            return 0;
        int drawn = 0;
        bool begun = false;
        auto start = [&]()
        {
            if (!begun)
            {
                begun = true;
                camera.apply();
                NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE, worldDraw.polygonMode());
            }
        };
        // The plain ones.
        bool plain = false;
        for (int n = 0; n < kMaxPieces; ++n)
        {
            const Piece &piece = a.pieces[n];
            // (A casing drawn as its model is not drawn again.)
            if (piece.kind == 0 || piece.alpha < 31 || !pieceSeen(piece) || modelDraw.pieceModelled(n))
                continue;
            if (piece.kind == kPieceShard && piece.texture != 0xFFFF && textureCache.present(piece.texture))
                continue;
            if (!plain)
            {
                plain = true;
                start();
                GFX_TEX_FORMAT = 0;
                GFX_BEGIN = GL_TRIANGLES;
            }
            GFX_COLOR = piece.colour;
            pieceCorners(piece, false);
            ++drawn;
        }
        // Those with a texture: each texture's together. (What breaks has a
        // handful of textures at most: the pieces are gone through once
        // for each that is met.)
        std::uint16_t done[8];
        int doneCount = 0;
        for (const Piece &first : a.pieces)
        {
            if (first.kind != kPieceShard || first.alpha < 31 || first.texture == 0xFFFF)
                continue;
            bool seen = false;
            for (int k = 0; k < doneCount; ++k)
                seen = seen || done[k] == first.texture;
            if (seen || !textureCache.present(first.texture) || doneCount == 8)
                continue;
            done[doneCount++] = first.texture;
            bool bound = false;
            for (const Piece &piece : a.pieces)
            {
                if (piece.kind != kPieceShard || piece.alpha < 31 || piece.texture != first.texture || !pieceSeen(piece))
                    continue;
                if (!bound)
                {
                    bound = true;
                    start();
                    noteTextureDrawn(first.texture);
                    textureCache.use(first.texture);
                    GFX_COLOR = 0x7FFF;
                    GFX_BEGIN = GL_TRIANGLES;
                }
                pieceCorners(piece, true);
                ++drawn;
            }
        }
        if (begun)
        {
            GFX_TEX_FORMAT = 0;
            NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), WorldDraw::kCull, worldDraw.polygonMode());
            GFX_COLOR = 0x7FFF;
        }
        return drawn;
    }

    /// What the marks and the solid small things may cost this pass: what
    /// there is of them, no more (and no more than the most ever drawn).
    void EffectDraw::reserve(int &polygons, int &vertices) const
    {
        const ActorWorld &a = game.actors;
        int marks = 0, pieces = 0;
        for (const Decal &decal : a.decals)
            marks += decal.active;
        // (Pieces: those that will be drawn -- seen, and in view.)
        for (const Piece &piece : a.pieces)
        {
            if (piece.kind == 0 || !pieceSeen(piece))
                continue;
            const std::int16_t at[3] = {static_cast<std::int16_t>(fxToInt(piece.pos[0])),
                                        static_cast<std::int16_t>(fxToInt(piece.pos[1])),
                                        static_cast<std::int16_t>(fxToInt(piece.pos[2]))};
            pieces += worldDraw.ballVisible(at, 16) ? 1 : 0;
        }
        const int most = isDSiMode() ? kDecalPolygons : kDecalPolygons / 2;
        marks = marks > most ? most : marks;
        // (And the sprays of a match: one each, at most.)
        if (netGame.playing())
        {
            for (const NetSprayMark &mark : netGame.match().sprays)
                marks += mark.active ? 1 : 0;
        }
        polygons += marks + pieces;
        vertices += marks * 4 + pieces * 3;
    }

    int EffectDraw::drawDecals()
    {
        NEA_Material *atlas = static_cast<NEA_Material *>(hudDraw.decalMaterial());
        ActorWorld &a = game.actors;
        if (atlas == nullptr || player.noclip)
            return 0;
        // The hardware's "draw where the depth is the same" (a polygon
        // attribute NEA has no name for): a mark lies in its wall's plane,
        // and is drawn where the wall is and nowhere else.
        constexpr int kDepthEqual = 1 << 14;
        // (How much of a mark is drawn over its wall at most, of 31: the
        // rest is the wall's own light through it -- a mark in a dark
        // corridor is dark, not as bright as it would be lit.)
        constexpr int kDecalAlpha = 22;
        // No more of them than this much of the screen between them (in
        // pixels, as they are seen: a stain on the floor far ahead covers a
        // sliver of what it would face on): see-through things one on
        // another are what the hardware runs out of time on.
        // (A quarter less of both when an enemy has had to be made room for
        // this frame: the stains give way before it does.)
        // (... eased into and out of, an eighth of it a pass.)
        decalYield_ += modelDraw.strained() ? (decalYield_ < 8 ? 1 : 0) : (decalYield_ > 0 ? -1 : 0);
        int kMostArea = isDSiMode() ? 30000 : 18000;
        int most = isDSiMode() ? kDecalPolygons : kDecalPolygons / 2;
        kMostArea -= kMostArea * decalYield_ / 32;
        most -= most * decalYield_ / 32;
        std::uint64_t shown = 0;
        // (The last fifth of the room is drawn thinning out, so that a mark
        // the room runs out for goes over a few frames, not at once.)
        const int softArea = kMostArea - kMostArea / 5;
        const BrushState *brushes = game.brushStates();
        int drawn = 0, area = 0;
        bool bound = false;
        // The nearest first: when not all of them are drawn, it is the far
        // ones that are not.
        std::uint8_t order[kMaxDecals];
        std::int32_t away[kMaxDecals];
        int listed = 0;
        for (int i = 0; i < kMaxDecals; ++i)
        {
            const Decal &one = a.decals[i];
            if (!one.active)
                continue;
            const std::int32_t dx = fxToInt(one.at[0] - camera.pos[0]), dy = fxToInt(one.at[1] - camera.pos[1]),
                               dz = fxToInt(one.at[2] - camera.pos[2]);
            const std::int32_t far = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + (dz < 0 ? -dz : dz);
            int at = listed++;
            while (at > 0 && away[at - 1] > far)
            {
                order[at] = order[at - 1];
                away[at] = away[at - 1];
                --at;
            }
            order[at] = static_cast<std::uint8_t>(i);
            away[at] = far;
        }
        for (int next = 0; next < listed; ++next)
        {
            Decal &decal = a.decals[order[next]];
            const std::uint64_t bit = std::uint64_t(1) << order[next];
            // (One shown the pass before keeps its place to the edge of the
            // room; one new comes in only with a tenth of it to spare: at the
            // edge, a mark does not come and go as the view turns.)
            const int roomFor = (decalShown_ & bit) ? kMostArea : kMostArea - kMostArea / 10;
            if (!decal.active || drawn >= most || area > roomFor)
                continue;
            fx at[3] = {decal.at[0], decal.at[1], decal.at[2]};
            if (decal.brush >= 0)
            {
                // On something that moves: where that is now. (Gone, if it
                // is.)
                if (brushes == nullptr || !brushes[decal.brush].active)
                {
                    decal.active = false;
                    continue;
                }
                for (int i = 0; i < 3; ++i)
                    at[i] += brushes[decal.brush].pos[i];
            }
            // Facing the eye, near enough to be seen, and in the view.
            const std::int32_t d[3] = {fxToInt(camera.pos[0] - at[0]), fxToInt(camera.pos[1] - at[1]),
                                       fxToInt(camera.pos[2] - at[2])};
            const std::int32_t facing = d[0] * decal.normal[0] + d[1] * decal.normal[1] + d[2] * decal.normal[2];
            const std::int32_t far = decal.half * 60 + 200;
            if (facing <= 0 || d[0] > far || d[0] < -far || d[1] > far || d[1] < -far || d[2] > far || d[2] < -far)
                continue;
            // (How far, as the limit just above measures it: in the last
            // quarter of the way to it, it thins out.)
            std::int32_t away = d[0] < 0 ? -d[0] : d[0];
            away = (d[1] < 0 ? -d[1] : d[1]) > away ? (d[1] < 0 ? -d[1] : d[1]) : away;
            away = (d[2] < 0 ? -d[2] : d[2]) > away ? (d[2] < 0 ? -d[2] : d[2]) : away;
            const std::int16_t centre[3] = {static_cast<std::int16_t>(fxToInt(at[0] + decal.normal[0] * 2)),
                                            static_cast<std::int16_t>(fxToInt(at[1] + decal.normal[1] * 2)),
                                            static_cast<std::int16_t>(fxToInt(at[2] + decal.normal[2] * 2))};
            if (!worldDraw.ballVisible(centre, decal.half + 2))
                continue;
            if (!bound)
            {
                bound = true;
                camera.apply();
                NEA_MaterialUse(atlas);
            }
            // (About how much of the screen it is: its width in pixels --
            // 128 pixels to a unit at a unit's distance -- by its height, which
            // is that as much less as it is seen edge-on; neither more than
            // the screen.)
            {
                const std::int32_t depth = -(d[0] * camera.forward[0] + d[1] * camera.forward[1] + d[2] * camera.forward[2]) >> 12;
                const int across = (decal.half * 2 * 128) / (depth < 8 ? 8 : depth);
                const fx toEye[3] = {fxFromInt(d[0]), fxFromInt(d[1]), fxFromInt(d[2])};
                const std::int32_t length = fxToInt(fxLength(toEye));
                // (`facing` is Q12 units: over the length, the cosine in 4096ths.)
                const int cosine = length > 0 ? static_cast<int>(facing / length) : 4096;
                const int wide = across > 256 ? 256 : across;
                int high = static_cast<int>((static_cast<std::int64_t>(across) * cosine) >> 12);
                high = high > 192 ? 192 : (high < 1 ? 1 : high);
                area += wide * high;
            }
            // Its last three seconds it fades; and as it nears where it is no
            // longer drawn, by distance or for want of room.
            int alpha = decal.leftMs >= 3000 ? kDecalAlpha : 1 + (decal.leftMs * (kDecalAlpha - 1)) / 3000;
            const std::int32_t thinFrom = far - far / 4;
            if (away > thinFrom)
                alpha = alpha * (far - away) / (far - thinFrom);
            if (area > softArea)
                alpha = alpha * (kMostArea - area) / (kMostArea - softArea);
            alpha = alpha < 1 ? 1 : (alpha > 31 ? 31 : alpha);
            NEA_PolyFormat(alpha, kIdMarksFirst + order[next] % kIdMarksCount, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE,
                           static_cast<NEA_OtherFormatEnum>(static_cast<int>(worldDraw.polygonMode()) | kDepthEqual));
            // Two directions across the wall.
            const fx *n = decal.normal;
            const fx size[3] = {n[0] < 0 ? -n[0] : n[0], n[1] < 0 ? -n[1] : n[1], n[2] < 0 ? -n[2] : n[2]};
            const int least = size[0] <= size[1] && size[0] <= size[2] ? 0 : (size[1] <= size[2] ? 1 : 2);
            fx axis[3] = {0, 0, 0};
            axis[least] = kFxOne;
            fx u[3] = {(n[1] * axis[2] - n[2] * axis[1]) >> 12, (n[2] * axis[0] - n[0] * axis[2]) >> 12,
                       (n[0] * axis[1] - n[1] * axis[0]) >> 12};
            const fx length = fxLength(u);
            if (length < 64)
                continue;
            for (fx &c : u)
                c = static_cast<fx>((static_cast<std::int64_t>(c) << 12) / length);
            const fx v[3] = {(n[1] * u[2] - n[2] * u[1]) >> 12, (n[2] * u[0] - n[0] * u[2]) >> 12,
                             (n[0] * u[1] - n[1] * u[0]) >> 12};
            const UiRect r = decalRect(decal.picture);
            const int st[4][2] = {{r.x, r.y + r.h}, {r.x + r.w, r.y + r.h}, {r.x + r.w, r.y}, {r.x, r.y}};
            const int sign[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            GFX_COLOR = decal.colour;
            GFX_BEGIN = GL_QUADS;
            for (int k = 0; k < 4; ++k)
            {
                fx p[3];
                for (int i = 0; i < 3; ++i)
                    p[i] = at[i] + u[i] * decal.half * sign[k][0] + v[i] * decal.half * sign[k][1];
                // (Turned a quarter, a half or three quarters: the same
                // picture, another mark.)
                const int *coordinate = st[(k + decal.turn) & 3];
                GFX_TEX_COORD = TEXTURE_PACK(coordinate[0] << 4, coordinate[1] << 4);
                corner(p);
            }
            shown |= bit;
            ++drawn;
        }
        decalShown_ = shown;
        if (bound)
        {
            GFX_TEX_FORMAT = 0;
            NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), WorldDraw::kCull, worldDraw.polygonMode());
            GFX_COLOR = 0x7FFF;
        }
        return drawn;
    }

    void EffectDraw::loadSprays(const NetMatch &match)
    {
        static_assert(kMaxSprays >= kNetPlayers);
        dropSprays();
        for (int p = 0; p < kNetPlayers; ++p)
        {
            const NetProfile &profile = match.profiles[p];
            if (!profile.sprayWhole())
                continue;
            // How solid the paint is (the logo's four levels: nothing, a
            // third, two thirds, all of it), in one colour: A3I5, all its
            // texels the palette's first.
            static std::uint8_t texels[32 * 32];
            static constexpr std::uint8_t kAlpha[4] = {0, 3 << 5, 5 << 5, 7 << 5};
            for (int y = 0; y < 32; ++y)
            {
                for (int x = 0; x < 32; ++x)
                {
                    // (Its rows as the logos' are: HlUi.hpp's IconsView.)
                    const std::uint8_t byte = profile.spray[y * 8 + (x >> 2)];
                    texels[y * 32 + x] = kAlpha[(byte >> ((x & 3) * 2)) & 3];
                }
            }
            const std::uint8_t *rgb = kSprayColour[profile.sprayColour % kSprayColours];
            std::uint16_t colours[32];
            for (std::uint16_t &c : colours)
                c = static_cast<std::uint16_t>((rgb[0] >> 3) | (rgb[1] >> 3) << 5 | (rgb[2] >> 3) << 10);
            NEA_Material *material = NEA_MaterialCreate();
            NEA_Palette *palette = NEA_PaletteCreate();
            if (material != nullptr && palette != nullptr &&
                NEA_MaterialTexLoad(material, NEA_A3PAL32, 32, 32, NEA_TEXGEN_TEXCOORD, texels) == 1 &&
                NEA_PaletteLoad(palette, colours, 32, NEA_A3PAL32) == 1)
            {
                NEA_MaterialSetPalette(material, palette);
                sprayMaterials_[p] = material;
                sprayPalettes_[p] = palette;
                continue;
            }
            // (No room: that one is not seen.)
            if (material != nullptr)
                NEA_MaterialDelete(material);
            if (palette != nullptr)
                NEA_PaletteDelete(palette);
        }
    }

    void EffectDraw::dropSprays()
    {
        for (int p = 0; p < kMaxSprays; ++p)
        {
            if (sprayMaterials_[p] != nullptr)
                NEA_MaterialDelete(static_cast<NEA_Material *>(sprayMaterials_[p]));
            if (sprayPalettes_[p] != nullptr)
                NEA_PaletteDelete(static_cast<NEA_Palette *>(sprayPalettes_[p]));
            sprayMaterials_[p] = sprayPalettes_[p] = nullptr;
        }
    }

    int EffectDraw::drawSprays()
    {
        if (!netGame.playing() || player.noclip)
            return 0;
        const NetMatch &match = netGame.match();
        constexpr int kDepthEqual = 1 << 14; // (as the marks: only where the wall is)
        int drawn = 0;
        for (int p = 0; p < kNetPlayers; ++p)
        {
            const NetSprayMark &mark = match.sprays[p];
            if (!mark.active || sprayMaterials_[p] == nullptr)
                continue;
            const fx *n = mark.normal;
            const std::int32_t d[3] = {fxToInt(camera.pos[0] - mark.at[0]), fxToInt(camera.pos[1] - mark.at[1]),
                                       fxToInt(camera.pos[2] - mark.at[2])};
            const std::int32_t facing = d[0] * n[0] + d[1] * n[1] + d[2] * n[2];
            const std::int16_t centre[3] = {static_cast<std::int16_t>(fxToInt(mark.at[0])),
                                            static_cast<std::int16_t>(fxToInt(mark.at[1])),
                                            static_cast<std::int16_t>(fxToInt(mark.at[2]))};
            if (facing <= 0 || !worldDraw.ballVisible(centre, kNetSprayHalf + 2))
                continue;
            // Upright on a wall, as the game sprays it: across, its right
            // as one looks at it; up, the world's up in its plane. (On a
            // floor or a ceiling, across as the view looks.)
            fx u[3] = {-n[1], n[0], 0};
            fx length = fxLength(u);
            if (length < 1024)
            {
                u[0] = camera.forward[1];
                u[1] = -camera.forward[0];
                u[2] = 0;
                length = fxLength(u);
                if (length < 64)
                    continue;
            }
            for (fx &c : u)
                c = static_cast<fx>((static_cast<std::int64_t>(c) << 12) / length);
            const fx v[3] = {(n[1] * u[2] - n[2] * u[1]) >> 12, (n[2] * u[0] - n[0] * u[2]) >> 12,
                             (n[0] * u[1] - n[1] * u[0]) >> 12};
            if (drawn == 0)
                camera.apply();
            NEA_MaterialUse(static_cast<NEA_Material *>(sprayMaterials_[p]));
            NEA_PolyFormat(31, kIdMarks, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE,
                           static_cast<NEA_OtherFormatEnum>(static_cast<int>(worldDraw.polygonMode()) | kDepthEqual));
            GFX_COLOR = 0x7FFF;
            static constexpr int kSign[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
            static constexpr int kTexel[4][2] = {{0, 0}, {32, 0}, {32, 32}, {0, 32}};
            GFX_BEGIN = GL_QUADS;
            for (int k = 0; k < 4; ++k)
            {
                fx corner3[3];
                for (int i = 0; i < 3; ++i)
                    corner3[i] = mark.at[i] + u[i] * kNetSprayHalf * kSign[k][0] + v[i] * kNetSprayHalf * kSign[k][1];
                GFX_TEX_COORD = TEXTURE_PACK(kTexel[k][0] << 4, kTexel[k][1] << 4);
                corner(corner3);
            }
            ++drawn;
        }
        if (drawn > 0)
        {
            GFX_TEX_FORMAT = 0;
            NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), WorldDraw::kCull, worldDraw.polygonMode());
        }
        return drawn;
    }

    void EffectDraw::end()
    {
        glowCount_ = 0;
        count_ = 0;
        GFX_TEX_FORMAT = 0;
        NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), WorldDraw::kCull, worldDraw.polygonMode());
        GFX_COLOR = 0x7FFF;
    }

    /// One of the map's own: a sprite or a glow, a beam or a laser
    /// (HlEffects.hpp says which are on).
    void EffectDraw::drawRecord(int i)
    {
        const LogicWorld &logic = game.logic;
        const LogicRecord &record = logic.records[i];
        if ((record.kind != static_cast<std::uint8_t>(LogicKind::Sprite) &&
             record.kind != static_cast<std::uint8_t>(LogicKind::Beam)) ||
            !effectSeen(record, logic.states[i]))
            return;
        const std::int16_t at[3] = {static_cast<std::int16_t>(fxToInt(record.move[0])),
                                    static_cast<std::int16_t>(fxToInt(record.move[1])),
                                    static_cast<std::int16_t>(fxToInt(record.move[2]))};
        if (record.kind == static_cast<std::uint8_t>(LogicKind::Sprite))
        {
            const SpriteRef *known = spriteCache.ref(record.extra[0]);
            if (known == nullptr)
                return;
            const int scale = static_cast<int>(record.extra[2] >> 24); // Q4
            int halfWide = known->worldWidth * scale / 32;
            int halfHigh = known->worldHeight * scale / 32;
            int alpha = effectAlpha(record);
            // A glow that dissipates (HlSprite.hpp): its size kept on the
            // screen, fainter with distance.
            if ((record.extra[3] & 2) && !(record.extra[3] & 8))
            {
                const std::int32_t d[3] = {fxToInt(record.move[0] - camera.pos[0]), fxToInt(record.move[1] - camera.pos[1]),
                                           fxToInt(record.move[2] - camera.pos[2])};
                const int dist = static_cast<int>(isqrt64(static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(d[0]) * d[0] + static_cast<std::int64_t>(d[1]) * d[1] +
                    static_cast<std::int64_t>(d[2]) * d[2])));
                int grow, bright;
                glowFalloff(dist, grow, bright);
                halfWide = (halfWide * grow) >> 8;
                halfHigh = (halfHigh * grow) >> 8;
                halfWide = halfWide < 1 ? 1 : halfWide;
                halfHigh = halfHigh < 1 ? 1 : halfHigh;
                alpha = (alpha * bright + 128) >> 8;
                if (alpha < 1)
                    return;
            }
            if (!worldDraw.ballVisible(at, halfWide > halfHigh ? halfWide : halfHigh))
                return;
            const SpriteRef *ref = sprite(record.extra[0], alpha, effectColour(record));
            // (Its frame of the game's own count, as the atlas has it.)
            const int source = ref->sourceFrames ? ref->sourceFrames : 1;
            const int frame = ((logic.states[i].phase >> 8) % source) * ref->frames / source;
            // A glow sits in its lamp, and the game draws it over the
            // lamp: here it is brought a little towards the eye, clear
            // of the glass.
            fx at3[3] = {record.move[0], record.move[1], record.move[2]};
            if (record.extra[3] & 2)
            {
                for (int k = 0; k < 3; ++k)
                    at3[k] -= camera.forward[k] * 14;
            }
            if (ref->kind == kSpriteUpright)
            {
                static const std::int32_t kUp[3] = {0, 0, kFxOne};
                picture(*ref, frame, at3, camera.right, kUp, halfWide, halfHigh);
            }
            else
            {
                picture(*ref, frame, at3, camera.right, camera.up, halfWide, halfHigh);
            }
        }
        else
        {
            // Where its ends are now (they may follow something, or be
            // where this strike was sent).
            fx from[3], to[3];
            beamEnds(logic, i, from, to);
            const std::int16_t a16[3] = {static_cast<std::int16_t>(fxToInt(from[0])),
                                         static_cast<std::int16_t>(fxToInt(from[1])),
                                         static_cast<std::int16_t>(fxToInt(from[2]))};
            const std::int16_t b16[3] = {static_cast<std::int16_t>(fxToInt(to[0])),
                                         static_cast<std::int16_t>(fxToInt(to[1])),
                                         static_cast<std::int16_t>(fxToInt(to[2]))};
            if (!worldDraw.ballVisible(a16, 32) && !worldDraw.ballVisible(b16, 32))
                return;
            const SpriteRef *ref = sprite(record.extra[0], effectAlpha(record), effectColour(record));
            if (ref == nullptr)
                return;
            // (Its width is in tenths of a unit, across; a strip takes
            // sixteenths, either side.)
            int width = static_cast<int>(record.extra[1] & 0xFFFF) * 16 / 20;
            width = width < 8 ? 8 : width;
            const int noise = static_cast<int>(record.extra[1] >> 16);
            if (record.maxs[1] & kBeamRing)
            {
                // A ring: a circle that has its two ends across it,
                // lying flat, in twelve strips.
                const fx middle[3] = {(from[0] + to[0]) / 2, (from[1] + to[1]) / 2, (from[2] + to[2]) / 2};
                const fx across3[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
                const fx radius = fxLength(across3) / 2;
                fx last[3] = {middle[0] + radius, middle[1], middle[2]};
                for (int piece = 1; piece <= 12; ++piece)
                {
                    fx sine, cosine;
                    fxSinCos(static_cast<std::uint16_t>(piece * 65536 / 12), sine, cosine);
                    const fx next[3] = {middle[0] + fxMul(radius, cosine), middle[1] + fxMul(radius, sine),
                                        middle[2]};
                    beam(*ref, 0, last, next, width);
                    for (int k = 0; k < 3; ++k)
                        last[k] = next[k];
                }
            }
            else if (noise != 0)
                lightning(*ref, from, to, width, noise / 12 + 1);
            else
                beam(*ref, 0, from, to, width);
        }
    
    }

    /// A tripmine's beam: the game's blue-green, thin and faint.
    void EffectDraw::drawMine(int i)
    {
        const ActorState &mine = game.actors.states[i];
        if (const SpriteRef *ref = sprite(CommonSprite::Beam, 14, RGB15(0, 26, 24)))
            beam(*ref, 0, mine.pos, mine.goal, 12);
    }

    void EffectDraw::drawGlow(int n)
    {
        // Brought a little towards the eye, clear of the model.
        const Glow &g = glows_[n];
        if (const SpriteRef *ref = sprite(CommonSprite::Glow, g.alpha, g.colour))
        {
            const fx at[3] = {g.at[0] - camera.forward[0] * 4, g.at[1] - camera.forward[1] * 4,
                              g.at[2] - camera.forward[2] * 4};
            picture(*ref, 0, at, camera.right, camera.up, g.half, g.half);
        }
    
    }

    void EffectDraw::drawShot(int n)
    {
        const Projectile &shot = game.actors.shots[n];
        static const std::int32_t kEast[3] = {kFxOne, 0, 0}, kNorth[3] = {0, kFxOne, 0};
        (void)kEast;
        (void)kNorth;
        if (!shot.active)
            return;
        // (Drawn as its model, where the map has one and it is in memory:
        // ModelDraw. A tripmine's beam is drawn here all the same.)
        const int model = game.thrownModel(shot.kind);
        const bool modelled = model >= 0 && model < game.actors.modelCount && game.actors.models[model].header != nullptr;
        if (shot.kind == kShotSpit || shot.kind == kShotMommaSpit)
        {
            // (Big momma's is a glob of it, five times the size.)
            const int size = shot.kind == kShotMommaSpit ? 24 : 7;
            if (const SpriteRef *ref = sprite(CommonSprite::Spit, 31))
                picture(*ref, static_cast<int>(Global::drawClock() / 4 % ref->frames), shot.pos, camera.right,
                        camera.up, size, size);
            else
            {
                look(31, 0, RGB15(12, 28, 6));
                square(shot.pos, camera.right, camera.up, 4);
            }
        }
        else if (shot.kind == kShotGib && shot.sprite != 0xFFFF)
        {
            // Something thrown out that is a sprite (sparks, embers).
            if (const SpriteRef *ref = sprite(shot.sprite, 31, 0x7FFF))
                picture(*ref, static_cast<int>(Global::drawClock() / 4 % ref->frames), shot.pos, camera.right,
                        camera.up, ref->worldWidth / 4 + 1, ref->worldHeight / 4 + 1);
        }
        else if (shot.kind == kShotBolt)
        {
            if (modelled)
                return;
            // A crossbow's bolt: a short dark line along the way it goes.
            const fx speed = fxLength(shot.velocity);
            if (speed > 0)
            {
                fx tail[3];
                for (int i = 0; i < 3; ++i)
                    tail[i] = shot.pos[i] - static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * 16) / (speed >> kFxShift > 0 ? speed >> kFxShift : 1));
                if (const SpriteRef *ref = sprite(CommonSprite::Beam, 31, RGB15(12, 8, 4)))
                    beam(*ref, 0, tail, shot.pos, 2);
            }
        }
        else if (shot.kind == kShotHeadBall || shot.kind == kShotZapBall)
        {
            // A controller's ball of energy: a glow, the head ball the
            // larger (HlAliens.hpp).
            const int size = shot.kind == kShotHeadBall ? 12 : 8;
            if (const SpriteRef *ref = sprite(CommonSprite::Glow, 31, RGB15(31, 24, 12)))
                picture(*ref, static_cast<int>(Global::drawClock() / 4 % ref->frames), shot.pos, camera.right,
                        camera.up, size, size);
            else
            {
                GFX_TEX_FORMAT = 0;
                look(31, 0, RGB15(31, 24, 12));
                square(shot.pos, camera.right, camera.up, size / 2);
            }
        }
        else if (shot.kind == kShotMine)
        {
            // A tripmine the player put on a wall, and its beam once it is on.
            if (!modelled)
            {
                GFX_TEX_FORMAT = 0;
                look(31, 0, RGB15(8, 10, 9));
                square(shot.pos, camera.right, camera.up, 3);
            }
            if (shot.sprite == 1)
            {
                fx end[3];
                for (int i = 0; i < 3; ++i)
                    end[i] = shot.pos[i] + static_cast<fx>((static_cast<std::int64_t>(shot.velocity[i]) * kMineRange *
                                                            shot.leftMs) >> kFractionShift);
                if (const SpriteRef *ref = sprite(CommonSprite::Beam, 14, RGB15(0, 26, 24)))
                    beam(*ref, 0, shot.pos, end, 12);
            }
        }
        else
        {
            // A grenade, a satchel charge, a snark; or something thrown out,
            // a dark scrap of it.
            if (modelled)
                return;
            GFX_TEX_FORMAT = 0;
            const std::uint16_t colour = shot.kind == kShotGib       ? RGB15(7, 6, 5)
                                         : shot.kind == kShotSatchel ? RGB15(9, 7, 4)
                                         : shot.kind == kShotSnark || shot.kind == kShotSnarkFoe ? RGB15(16, 6, 4)
                                                                     : RGB15(6, 9, 5);
            look(31, 0, colour);
            square(shot.pos, camera.right, camera.up,
                   shot.kind == kShotSatchel || shot.kind == kShotSnark || shot.kind == kShotSnarkFoe ? 4 : 3);
        }
    
    }

    void EffectDraw::drawEffect(int n)
    {
        const Effect &effect = game.actors.effects[n];
        static const std::int32_t kEast[3] = {kFxOne, 0, 0}, kNorth[3] = {0, kFxOne, 0};
        if (effect.kind == 0 || effect.wholeMs <= 0)
            return;
        // How far through its moment it is, in 32nds, and how much of it
        // is left to see (the hardware's alpha: 1 to 31).
        const int gone = ((effect.wholeMs - effect.leftMs) * 32) / effect.wholeMs;
        const int left = 31 - gone > 1 ? 31 - gone : 1;
        switch (effect.kind)
        {
        case kEffectBolt:
            if (const SpriteRef *ref = sprite(CommonSprite::Lightning, 31, RGB15(14, 31, 14)))
            {
                lightning(*ref, effect.a, effect.b, 5 * 16, 6);
            }
            else
            {
                // Two strips crossed, so that it has width from anywhere.
                look(31, 0, RGB15(14, 31, 14));
                strip(effect.a, effect.b, camera.up, 2);
                strip(effect.a, effect.b, camera.right, 2);
            }
            break;
        case kEffectTracer:
            if (const SpriteRef *ref = sprite(CommonSprite::Beam, 20, RGB15(31, 28, 16)))
                beam(*ref, 0, effect.a, effect.b, 20);
            break;
        case kEffectBeam:
            // The gauss's and the egon's: in their colours, fading out.
            if (const SpriteRef *ref = sprite(CommonSprite::Beam, left, effect.colour))
                beam(*ref, static_cast<int>(Global::drawClock() / 2 % (ref->frames > 0 ? ref->frames : 1)), effect.a,
                     effect.b, effect.size * 16);
            break;
        case kEffectMuzzle:
            if (const SpriteRef *ref = sprite(CommonSprite::Muzzle, 31))
                picture(*ref, jitter(8) & 1, effect.a, camera.right, camera.up, 9, 9);
            break;
        case kEffectBurst:
            if (const SpriteRef *ref = sprite(CommonSprite::Explosion, 31))
            {
                // (The fireball rises a little as it burns out.)
                const fx at[3] = {effect.a[0], effect.a[1], effect.a[2] + fxFromInt(8 + gone / 2)};
                const int half = effect.size / 2 + (effect.size * gone) / 128;
                picture(*ref, (ref->frames * gone) / 32, at, camera.right, camera.up, half, half);
            }
            else
            {
                look(left > 24 ? 24 : left, 40, RGB15(31, 24, 8));
                square(effect.a, camera.right, camera.up, 16 + (effect.size * gone) / 32);
            }
            break;
        case kEffectRing:
            // Flat on the ground, spreading.
            if (const SpriteRef *ref = sprite(CommonSprite::Shock, left, RGB15(18, 24, 31)))
            {
                const int half = 16 + (effect.size * gone) / 32;
                picture(*ref, 0, effect.a, kEast, kNorth, half, half);
            }
            else
            {
                look(left > 16 ? 16 : left, 41, RGB15(16, 22, 31));
                square(effect.a, kEast, kNorth, 16 + (effect.size * gone) / 32);
            }
            break;
        case kEffectSplat:
            if (const SpriteRef *ref = sprite(CommonSprite::Spit, left))
                picture(*ref, ref->frames - 1, effect.a, camera.right, camera.up, effect.size / 2 + 4,
                        effect.size / 2 + 4);
            break;
        case kEffectBlood:
            if (const SpriteRef *ref =
                    sprite(CommonSprite::Blood, 31, effect.size ? RGB15(24, 26, 4) : RGB15(31, 3, 3)))
            {
                // (The red a little larger: the sprite is grey, and red
                // through grey is a third as bright as yellow through it.)
                const int half = effect.size ? 10 : 13;
                picture(*ref, (ref->frames * gone) / 32, effect.a, camera.right, camera.up, half, half);
            }
            break;
        case kEffectSprite:
            if (const SpriteRef *ref = sprite(effect.sprite, 31, effect.colour))
            {
                const int half = effect.size >= 0 ? effect.size / 2 : (ref->worldWidth * -effect.size) / 32;
                const fx at[3] = {effect.a[0] + (effect.b[0] - effect.a[0]) / 32 * gone,
                                  effect.a[1] + (effect.b[1] - effect.a[1]) / 32 * gone,
                                  effect.a[2] + (effect.b[2] - effect.a[2]) / 32 * gone};
                picture(*ref, (ref->frames * gone) / 32, at, camera.right, camera.up, half,
                        half * ref->worldHeight / (ref->worldWidth ? ref->worldWidth : 1));
            }
            break;
        default:
            break;
        }
    
    }
} // namespace hlrt
