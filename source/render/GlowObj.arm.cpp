#include "render/GlowObj.hpp"

#include <nds.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "Global.hpp"
#include "HlSprite.hpp"
#include "data/LevelFile.hpp"
#include "data/TextureCache.hpp"
#include "render/Camera.hpp"
#include "render/ModelDraw.hpp"

namespace hlrt
{
    GlowObj glowObj;

    using namespace hldata;

    namespace
    {
        /// The cell's first tile in the sprites' 2D map: the HUD's sheet
        /// starts at tile 512, 32 tiles to a row (HudDraw's tileOf).
        constexpr int kCellTile = 512 + (GlowObj::kCellY >> 3) * 32 + (GlowObj::kCellX >> 3);
    } // namespace

    void GlowObj::forget()
    {
        sprite_ = -1;
        count_ = banks_ = 0;
        publish(false);
    }

    void GlowObj::load()
    {
        forget();
        if (!kOn)
            return;
        const LevelView &v = level.view;
        if (v.spriteCount == 0)
            return;
        // The glow sprite the map's glows use most (render mode 3: extra[3]
        // bit 1). Only one that is one round picture, turned to the eye.
        int best = -1, bestCount = 0;
        {
            constexpr int kTally = 64;
            int tally[kTally] = {};
            for (std::uint32_t i = 0; i < v.logicCount; ++i)
            {
                const LogicRecord &r = v.logic[i];
                if (r.kind == static_cast<std::uint8_t>(LogicKind::Sprite) && (r.extra[3] & 2) &&
                    r.extra[0] < v.spriteCount && r.extra[0] < static_cast<std::uint32_t>(kTally))
                    ++tally[r.extra[0]];
            }
            for (int s = 0; s < kTally; ++s)
            {
                if (tally[s] > bestCount)
                {
                    best = s;
                    bestCount = tally[s];
                }
            }
        }
        if (best < 0)
            return;
        const SpriteRef &ref = v.sprites[best];
        if (ref.frames == 0 || ref.sourceFrames > 1 || ref.kind != kSpriteFacesEye || ref.cellWidth < 8 ||
            ref.cellHeight != ref.cellWidth)
            return;

        // Its first picture, read (not kept): A3I5, the see-through in the
        // top three bits -- how bright it was (HlSprite.hpp).
        const std::size_t capacity = static_cast<std::size_t>(ref.width) * ref.height + 128;
        auto *body = static_cast<std::uint8_t *>(std::malloc(capacity));
        if (body == nullptr)
            return;
        TextureHeader header;
        const std::string path = global.mapPath("spr/") + spriteFile(ref.name, ref.width, ref.height);
        if (!TextureCache::readFile(path.c_str(), ref.width, ref.height, 1, header, body, capacity))
        {
            std::free(body);
            return;
        }
        // Its rings: how bright each half pixel out from the middle is, on
        // the average. (A glow is round: made again from these, it is
        // smooth, where its eight levels were steps.)
        const int size = ref.cellWidth;
        constexpr int kBins = 64;
        std::int32_t sum[kBins] = {}, many[kBins] = {};
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                // (Twice the distance from the middle, in half pixels.)
                const int dx = 2 * x + 1 - size, dy = 2 * y + 1 - size;
                const int bin = static_cast<int>(isqrt64(static_cast<std::uint64_t>(dx * dx + dy * dy)));
                if (bin < kBins)
                {
                    sum[bin] += body[y * ref.width + x] >> 5;
                    ++many[bin];
                }
            }
        }
        std::free(body);
        // (A ring no pixel's distance rounds to has none: as its neighbours
        // between. Left empty, it was a dark ring of dots in the cone.)
        for (int bin = 0; bin < kBins; ++bin)
        {
            if (many[bin] != 0)
                continue;
            int before = bin - 1, after = bin + 1;
            while (before >= 0 && many[before] == 0)
                --before;
            while (after < kBins && many[after] == 0)
                ++after;
            const int a = before >= 0 ? sum[before] * 64 / many[before] : -1;
            const int b = after < kBins ? sum[after] * 64 / many[after] : -1;
            const int value = a < 0 ? b : (b < 0 ? a : a + (b - a) * (bin - before) / (after - before));
            if (value >= 0)
            {
                sum[bin] = value;
                many[bin] = 64;
            }
        }
        // The cell: 32 by 32, each pixel 0..15 of the cone at its distance
        // (the picture's size squeezed or stretched to it).
        std::memset(cell_, 0, sizeof(cell_));
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                const int dx = 2 * x - 31, dy = 2 * y - 31;
                // Half pixels of the picture, Q4: out of 32 to its `size`.
                const int at16 = static_cast<int>(isqrt64(static_cast<std::uint64_t>(dx * dx + dy * dy) * 256)) * size / 32;
                const int bin = at16 >> 4, part = at16 & 15;
                if (bin + 1 >= kBins || many[bin] == 0)
                    continue;
                const int a = sum[bin] * 64 / many[bin];
                const int b = many[bin + 1] ? sum[bin + 1] * 64 / many[bin + 1] : 0;
                const int level64 = a + (b - a) * part / 16; // of 7 * 64
                const int index = (level64 * 15 + 7 * 32) / (7 * 64);
                const int nibble = index < 0 ? 0 : (index > 15 ? 15 : index);
                const int word = ((y >> 3) * 4 + (x >> 3)) * 8 + (y & 7);
                cell_[word] |= static_cast<std::uint32_t>(nibble) << ((x & 7) * 4);
            }
        }
        sprite_ = best;
    }

    void GlowObj::beginFrame()
    {
        count_ = banks_ = 0;
        // Pixels per unit across, at a unit's depth: 96 over the tangent of
        // half the picture's height (128 across on 4:3, the same; on 16:10
        // the picture is wider, a pixel shows more: 5/6 of it).
        const int high = camera.zoomed ? Camera::kZoomFovDegrees : camera.fovDegrees;
        fx s = 0, c = 0;
        fxSinCos(static_cast<std::uint16_t>(high * 65536 / 720), s, c);
        pixelsUp_ = s > 0 ? static_cast<std::int32_t>((static_cast<std::int64_t>(96) * c << 12) / s) : 0;
        pixelsAcross_ = camera.wide ? pixelsUp_ * 5 / 6 : pixelsUp_;

        // The gun on the screen. Its box's near corners are at the eye, and
        // seen from there would reach anywhere (c1a1's crowbar: its box's
        // near top corner above the screen); but a gun in hand comes up
        // from the screen's bottom. So: the far side of its box (in the
        // camera's own axes already: forward the depth, left the screen's
        // left, up its up), and each of its corners again below the screen
        // -- and the outline round those.
        gunPoints_ = 0;
        std::int16_t mins[3], maxs[3];
        fx push = 0;
        if (pixelsUp_ != 0 && modelDraw.heldBox(mins, maxs, push))
        {
            std::int32_t px[8], py[8];
            const int forward = maxs[0] + fxToInt(push);
            const int z = forward < 4 ? 4 : forward;
            // (One depth for all four: pixels per unit there, once.)
            const std::int32_t across = pixelsAcross_ / z, upward1 = pixelsUp_ / z;
            for (int corner = 0; corner < 4; ++corner)
            {
                const int left = corner & 1 ? maxs[1] : mins[1];
                const int upward = corner & 2 ? maxs[2] : mins[2];
                px[corner] = 128 - ((left * across) >> 12);
                py[corner] = 96 - ((upward * upward1) >> 12);
                px[corner + 4] = px[corner];
                py[corner + 4] = 192 + 8;
            }
            // (The outline: the points in order across, then the lower and
            // the upper halves of it, each turning the one way.)
            int order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
            for (int i = 1; i < 8; ++i)
            {
                for (int j = i; j > 0 && (px[order[j]] < px[order[j - 1]] ||
                                          (px[order[j]] == px[order[j - 1]] && py[order[j]] < py[order[j - 1]]));
                     --j)
                {
                    const int t = order[j];
                    order[j] = order[j - 1];
                    order[j - 1] = t;
                }
            }
            const auto turn = [&](int o, int a, int b)
            {
                return static_cast<std::int64_t>(px[a] - px[o]) * (py[b] - py[o]) -
                       static_cast<std::int64_t>(py[a] - py[o]) * (px[b] - px[o]);
            };
            int hull[16], n = 0;
            for (int i = 0; i < 8; ++i)
            {
                while (n >= 2 && turn(hull[n - 2], hull[n - 1], order[i]) <= 0)
                    --n;
                hull[n++] = order[i];
            }
            for (int i = 6, lower = n + 1; i >= 0; --i)
            {
                while (n >= lower && turn(hull[n - 2], hull[n - 1], order[i]) <= 0)
                    --n;
                hull[n++] = order[i];
            }
            n = n > 1 ? n - 1 : n; // (the last is the first again)
            for (int i = 0; i < n && i < 8; ++i)
            {
                gunX_[i] = px[hull[i]];
                gunY_[i] = py[hull[i]];
            }
            gunPoints_ = n > 8 ? 8 : n;
            for (int i = 0; i < gunPoints_; ++i)
            {
                const int j = i + 1 < gunPoints_ ? i + 1 : 0;
                const std::int64_t dx = gunX_[j] - gunX_[i], dy = gunY_[j] - gunY_[i];
                gunSide_[i] = static_cast<std::int32_t>(isqrt64(static_cast<std::uint64_t>(dx * dx + dy * dy)));
            }
        }
    }

    /// Whether a circle at (x, y), `r` across from its middle, comes within
    /// four pixels (what an animation moves the gun by, about) of the gun's
    /// outline: inside it, or no further out from any side than that.
    bool GlowObj::overGun(int x, int y, int r) const
    {
        if (gunPoints_ < 3)
            return false;
        const std::int64_t reach = r + 4;
        for (int i = 0; i < gunPoints_; ++i)
        {
            const int j = i + 1 < gunPoints_ ? i + 1 : 0;
            // (The outline turns the one way: inside is on the left of each
            // side, where this is positive; out by `reach`, -reach * side.)
            const std::int64_t across = static_cast<std::int64_t>(gunX_[j] - gunX_[i]) * (y - gunY_[i]) -
                                        static_cast<std::int64_t>(gunY_[j] - gunY_[i]) * (x - gunX_[i]);
            if (across < -reach * gunSide_[i])
                return false;
        }
        return true;
    }

    bool GlowObj::place(unsigned sprite, const fx *at, int half, int alpha, std::uint16_t colour)
    {
        if (!kOn || sprite_ < 0 || sprite != static_cast<unsigned>(sprite_) || count_ >= kMax || pixelsUp_ == 0)
            return false;
        // Where it is from the eye, in sixteenths of a unit.
        std::int32_t d[3];
        for (int k = 0; k < 3; ++k)
            d[k] = (at[k] - camera.pos[k]) >> 8;
        const auto along = [&](const std::int32_t *axis)
        {
            return static_cast<std::int32_t>((static_cast<std::int64_t>(d[0]) * axis[0] +
                                              static_cast<std::int64_t>(d[1]) * axis[1] +
                                              static_cast<std::int64_t>(d[2]) * axis[2]) >>
                                             12);
        };
        const std::int32_t z = along(camera.forward);
        // (Close to the eye -- or round it: the 3D quad, which the near
        // plane cuts as it should.)
        if (z < 16 * 16)
            return false;
        const std::int32_t x = along(camera.right), y = along(camera.up);
        // (One divide: 2^30 over the depth, then multiplies.)
        const std::int64_t inverse = (1 << 30) / z;
        const int sx = 128 + static_cast<int>((((static_cast<std::int64_t>(x) * pixelsAcross_) >> 12) * inverse) >> 30);
        const int sy = 96 - static_cast<int>((((static_cast<std::int64_t>(y) * pixelsUp_) >> 12) * inverse) >> 30);
        const int r = static_cast<int>((((static_cast<std::int64_t>(half) * 16 * pixelsUp_) >> 12) * inverse) >> 30);
        // (Bigger than the double-size box holds: the 3D quad.)
        if (r > 32)
            return false;
        if (sx + 32 < 0 || sx - 32 > 255 || sy + 32 < 0 || sy - 32 > 191)
            return true; // (off the screen: nothing to draw)
        // (Over the gun: the 3D quad, behind it.)
        if (overGun(sx, sy, r))
            return false;
        alpha = alpha < 1 ? 1 : (alpha > 31 ? 31 : alpha);
        // Its palette: its colour times how bright it is, 15 steps up.
        const std::uint32_t key = colour | (static_cast<std::uint32_t>(alpha) << 16);
        int bank = 0;
        while (bank < banks_ && bankKey_[bank] != key)
            ++bank;
        if (bank == banks_)
        {
            const int ramp = rampOf(key, colour, alpha);
            if (ramp < 0)
                return true; // (too faint to be seen at all: nothing, as in the game)
            if (banks_ >= kBanks)
                return false;
            bankKey_[bank] = key;
            std::memcpy(palette_[bank], ramp_[ramp], sizeof(palette_[bank]));
            ++banks_;
        }
        const int n = count_++;
        // Affine, double size (a 64x64 box round the 32x32 cell), semi-
        // transparent: added (HudDraw's blend). Under the HUD's sprites,
        // over the 3D picture (priority 1, as the picture's layer).
        attr_[n][0] = static_cast<std::uint16_t>(((sy - 32) & 0xFF) | 0x0300 | 0x0400);
        attr_[n][1] = static_cast<std::uint16_t>(((sx - 32) & 0x1FF) | (n << 9) | 0x8000);
        attr_[n][2] = static_cast<std::uint16_t>(kCellTile | (1 << 10) | ((kFirstBank + bank) << 12));
        // The cell's 16 pixels from the middle shown as r: 8.8.
        scale_[n] = static_cast<std::int16_t>(r > 0 ? (256 * 16) / r : 0x7FFF);
        return true;
    }

    /// The ramp for `key` (its colour and brightness): kept, or made now
    /// in the slot made longest ago. -1: it adds nothing to be seen.
    int GlowObj::rampOf(std::uint32_t key, std::uint16_t colour, int alpha)
    {
        for (int r = 0; r < kRamps; ++r)
        {
            if (rampKey_[r] == key)
                return rampLit_[r] ? r : -1;
        }
        const int r = rampNext_;
        rampNext_ = (rampNext_ + 1) & (kRamps - 1);
        rampKey_[r] = key;
        // Less what the eye does not see of it (kFloor): of its brightest
        // channel, the others in proportion, so its colour holds. In 31 *
        // 15ths; (v + 232) / 465 is (v + 232) * 18041 >> 23 for every v
        // these make (checked to 15000 with integers).
        const int channel[3] = {colour & 31, (colour >> 5) & 31, (colour >> 10) & 31};
        const int brightest = channel[0] > channel[1] ? (channel[0] > channel[2] ? channel[0] : channel[2])
                                                      : (channel[1] > channel[2] ? channel[1] : channel[2]);
        bool lit = false;
        if (brightest > 0)
        {
            int floor[3];
            for (int k = 0; k < 3; ++k)
                floor[k] = kFloor * 31 * 15 * channel[k] / brightest;
            for (int i = 1; i <= 15; ++i)
            {
                int out[3];
                for (int k = 0; k < 3; ++k)
                {
                    const int value = channel[k] * alpha * i - floor[k];
                    out[k] = value <= 0 ? 0 : ((value + 232) * 18041) >> 23;
                    out[k] = out[k] > 31 ? 31 : out[k];
                    lit = lit || out[k] != 0;
                }
                ramp_[r][i - 1] = static_cast<std::uint16_t>(RGB15(out[0], out[1], out[2]));
            }
        }
        rampLit_[r] = lit;
        return lit ? r : -1;
    }

    void GlowObj::publish(bool shown)
    {
        const int state = enterCriticalSection();
        shownCount_ = shown ? count_ : 0;
        shownBanks_ = shown ? banks_ : 0;
        std::memcpy(shownAttr_, attr_, sizeof(shownAttr_));
        std::memcpy(shownScale_, scale_, sizeof(shownScale_));
        std::memcpy(shownPalette_, palette_, sizeof(shownPalette_));
        leaveCriticalSection(state);
    }

    void GlowObj::onVblank()
    {
        auto *oam = reinterpret_cast<volatile std::uint16_t *>(0x07000000);
        for (int n = 0; n < kMax; ++n)
        {
            volatile std::uint16_t *entry = oam + (kFirstOam + n) * 4;
            if (n >= shownCount_)
            {
                entry[0] = 0x0200; // hidden
                continue;
            }
            entry[0] = shownAttr_[n][0];
            entry[1] = shownAttr_[n][1];
            entry[2] = shownAttr_[n][2];
            // Its matrix (group n: the fourth words of entries 4n..4n+3).
            oam[(n * 4 + 0) * 4 + 3] = static_cast<std::uint16_t>(shownScale_[n]);
            oam[(n * 4 + 1) * 4 + 3] = 0;
            oam[(n * 4 + 2) * 4 + 3] = 0;
            oam[(n * 4 + 3) * 4 + 3] = static_cast<std::uint16_t>(shownScale_[n]);
        }
        for (int b = 0; b < shownBanks_; ++b)
        {
            for (int i = 0; i < 15; ++i)
                SPRITE_PALETTE[(kFirstBank + b) * 16 + 1 + i] = shownPalette_[b][i];
        }
    }
} // namespace hlrt
