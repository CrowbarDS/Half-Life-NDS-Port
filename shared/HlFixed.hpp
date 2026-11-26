#pragma once

// The fixed-point formats of the port, in one place.
//
// The DS has no FPU, so nothing compiled for it uses float or double. Every
// quantity has ONE format, named here, and the converter (which does have
// floats) converts into it with the helpers at the bottom of this file. Both
// sides include this header, so the two cannot drift apart.
//
//   quantity            type      format   one unit is
//   ------------------  --------  -------  -----------------------------------
//   position, velocity  int32     Q19.12   1/4096 Half-Life unit (range +-524k)
//   plane distance      int32     Q19.12   same
//   plane normal        int32     Q1.30    2^-30
//   trace fraction      int32     Q30      2^30 = the whole move
//   time step           int32     Q16      65536 = one second
//   render vertex       int16     Q13.2    1/4 Half-Life unit (range +-8191)
//   angle               uint16    turns    65536 = 360 degrees
//   colour              uint16    RGB555
//
// WHY Q30 NORMALS rather than the 16-bit normals a PS1 port can live with: a
// plane's distance error is the normal's error times how far from the origin
// the point is. Maps reach 4096 units out, where a Q14 normal is good to about
// a fifth of a unit -- enough to make the collision hull disagree visibly with
// the wall it stands for. The ARM9 multiplies 32x32 into 64 in one instruction
// (SMULL), so the wider normal costs memory (4 bytes a component) but no time.
//
// WHY RENDER VERTICES ARE NOT Q12: the geometry engine's VTX_16 command takes
// a 16-bit coordinate, so a vertex is whatever fits in 16 bits. A quarter unit
// covers every retail map with room to spare; the view matrix carries the
// scale.
//
// This header is std-only on purpose: no <nds.h>, so the host tests compile the
// same arithmetic the console runs.

#include <cstdint>
#include <type_traits>

#include "HlDsp.hpp" // (the 16-bit multiplies, fxMulShort)

#ifdef HL_HW_MATH
// source/HwMath.s. (const: the same value, the same root, whatever else: the
// compiler may take one root for several of one value.)
extern "C" std::uint32_t hlHwSqrt64(std::uint64_t value) __attribute__((const));
#endif

// HL_HOT marks the functions that run many times a frame: the hull trace
// and what it calls, the posing of a model's bones, a monster's step, the
// sines and the lengths. They are inline like everything else here; on the
// console they are, besides, ARM code in the 32 KB of fast memory (ITCM) --
// whichever file's copy the linker keeps, it is that one, and everything
// that traces (the player, every monster, every shot) runs it from there.
//
// The 32 KB are shared with the world's drawing, the libraries' interrupt
// code and the hardware's divide (HwMath.s), and the link fails when they
// are full: what goes in is chosen by how much of the frame it is, not by
// how important it sounds. tools/arm9-report.sh lists what is there, byte by
// byte, and with --check says if one of these is not.
#ifndef HL_HOT
// (Not in the game library, HL_DSL: a library has no ITCM of its own.)
#if defined(__arm__) && defined(HL_HW_MATH) && !defined(HL_DSL)
// (A section each, by a number: two kinds of function -- one the linker
// merges, one it does not -- cannot share one.)
#define HL_HOT_TEXT2(n) #n
#define HL_HOT_TEXT(n) HL_HOT_TEXT2(n)
#ifdef HLDS_ITCM_OFF
// (A build to measure ITCM with, build.py: ARM code as ever, in main memory.)
#define HL_HOT inline __attribute__((target("arm")))
#else
#define HL_HOT inline __attribute__((target("arm"), section(".itcm.text.hl_hot." HL_HOT_TEXT(__COUNTER__))))
#endif
#else
#define HL_HOT inline
#endif
#endif
// HL_WARM: ARM code as HL_HOT is, for its multiplies, but in main memory:
// what runs now and then in a frame (a monster's thinking), not for every
// vertex, bone or trace, and does not earn its room in ITCM.
#ifndef HL_WARM
#if defined(__arm__) && defined(HL_HW_MATH)
#define HL_WARM inline __attribute__((target("arm")))
#else
#define HL_WARM inline
#endif
#endif
// HL_COLD: Thumb code in main memory, a third smaller than ARM: what runs
// seldom and for few (a monster in the air), in a file built as ARM.
#ifndef HL_COLD
#if defined(__arm__) && defined(HL_HW_MATH)
#define HL_COLD inline __attribute__((target("thumb")))
#else
#define HL_COLD inline
#endif
#endif

namespace hldata
{
    using fx = std::int32_t; ///< Q19.12

    inline constexpr int kFxShift = 12;
    inline constexpr fx kFxOne = 1 << kFxShift;

    inline constexpr int kNormalShift = 30;
    inline constexpr std::int32_t kNormalOne = 1 << kNormalShift;

    /// Render vertices: quarter units.
    inline constexpr int kVertexShift = 2;

    /// Trace fractions. Q30 and not Q12 because a fraction places a point along
    /// a segment that may be thousands of units long, and the point has to land
    /// within 1/32 of a unit of a wall.
    inline constexpr int kFractionShift = 30;
    inline constexpr std::int32_t kFractionOne = 1 << kFractionShift;

    /// Seconds, for time steps.
    inline constexpr int kTimeShift = 16;

    constexpr fx fxFromInt(int units) { return static_cast<fx>(units) << kFxShift; }
    /// Rounds towards minus infinity, like the shift it is.
    constexpr int fxToInt(fx value) { return value >> kFxShift; }

    /// How far `perSecond` goes in `ms` milliseconds: perSecond * ms / 1000,
    /// as the 64-bit sum gives it, rounded towards zero. Where the product
    /// fits 32 bits (nearly always: a speed of a few thousand units a second
    /// over a frame) it is divided in 32 bits, which the compiler makes a
    /// multiply, not a trip to the divider; else in 64. The same answer
    /// either way (test_divfree). One copy, out of line: inline at each of
    /// its forty-odd places it cost the program more than the divide's call.
    HL_HOT __attribute__((noinline)) constexpr fx fxTimesMs32(std::int32_t perSecond, int ms)
    {
        const std::int64_t product = static_cast<std::int64_t>(perSecond) * ms;
        if (product >= INT32_MIN && product <= INT32_MAX)
            return static_cast<fx>(static_cast<std::int32_t>(product) / 1000);
        return static_cast<fx>(product / 1000);
    }
    /// ... for any whole number that a 32-bit signed one holds as it is:
    /// nothing wider, and no unsigned 32-bit one, which would not.
    template <class T> constexpr fx fxTimesMs(T perSecond, int ms)
    {
        static_assert(std::is_integral_v<T> && (sizeof(T) < 4 || (sizeof(T) == 4 && std::is_signed_v<T>)),
                      "fxTimesMs: a number a 32-bit signed one holds");
        return fxTimesMs32(static_cast<std::int32_t>(perSecond), ms);
    }

    /// Q12 * Q12 -> Q12, through 64 bits.
    constexpr fx fxMul(fx a, fx b)
    {
        return static_cast<fx>((static_cast<std::int64_t>(a) * b) >> kFxShift);
    }

    /// Integer square root of a 64-bit value, rounded down. Bit by bit, with no
    /// division and no table: the same answer on the host and on the console.
    /// (Where the console's build says it has one -- HL_HW_MATH -- its own
    /// square-root unit gives that answer in a fraction of the time.)
    constexpr std::uint32_t isqrt64(std::uint64_t value)
    {
#ifdef HL_HW_MATH
        // (The console's unit is exact over the whole range: measured,
        // tools/hwtest/divcontract. melonDS's gets about a third of the roots
        // from 2^62 up one too high, where the game has no numbers.)
        if (!std::is_constant_evaluated())
            return hlHwSqrt64(value);
#endif
        std::uint64_t result = 0;
        std::uint64_t bit = std::uint64_t(1) << 62;
        while (bit > value)
            bit >>= 2;
        while (bit != 0)
        {
            if (value >= result + bit)
            {
                value -= result + bit;
                result = (result >> 1) + bit;
            }
            else
            {
                result >>= 1;
            }
            bit >>= 2;
        }
        return static_cast<std::uint32_t>(result);
    }

    /// Length of a Q12 vector, Q12.
    HL_WARM constexpr fx fxLength(const fx *v)
    {
        const std::int64_t sum = static_cast<std::int64_t>(v[0]) * v[0] +
                                 static_cast<std::int64_t>(v[1]) * v[1] +
                                 static_cast<std::int64_t>(v[2]) * v[2];
        return static_cast<fx>(isqrt64(static_cast<std::uint64_t>(sum)));
    }

    /// The square of a Q12 vector's length, Q24: what fxLength takes the
    /// root of.
    constexpr std::int64_t fxLengthSquared(const fx *v)
    {
        return static_cast<std::int64_t>(v[0]) * v[0] + static_cast<std::int64_t>(v[1]) * v[1] +
               static_cast<std::int64_t>(v[2]) * v[2];
    }

    /// fxLength(v) < r, without the root, and always the same answer: the
    /// root rounded down is under r exactly when the square is under r^2.
    /// (fxLength(v) >= r is its opposite.)
    constexpr bool fxLengthBelow(const fx *v, fx r)
    {
        return r > 0 && fxLengthSquared(v) < static_cast<std::int64_t>(r) * r;
    }

    /// fxLength(v) <= r, likewise: when the square is under (r + 1)^2.
    /// (fxLength(v) > r is its opposite.)
    constexpr bool fxLengthAtMost(const fx *v, fx r)
    {
        return r >= 0 && fxLengthSquared(v) < (static_cast<std::int64_t>(r) + 1) * (static_cast<std::int64_t>(r) + 1);
    }

    namespace detail
    {
        // fxSinCos's quarter wave at 14 bits, two more than the answer has, so
        // that the interpolation is rounded once, at the end, instead of being
        // cut short twice. (Out here, not in the function: a table local to a
        // function is copied onto the stack at every call.)
        inline constexpr std::int16_t kSineQuarter[65] = {
            0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981,
            4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005, 7366, 7723, 8076,
            8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585,
            11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256,
            14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557, 15679, 15791, 15893,
            15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};
    } // namespace detail

    /// Sine and cosine of an angle in turns (65536 to the circle), Q12.
    ///
    /// A quarter wave of 64 steps with straight lines between them, good to
    /// under one unit in the last place. It is here, rather than
    /// borrowed from libnds, because the code that turns doors is shared with
    /// the host tests, and both must turn them by exactly the same amount.
    HL_HOT constexpr void fxSinCos(std::uint16_t angle, fx &sine, fx &cosine)
    {
        const auto quarterSine = [](std::uint32_t a) -> fx // a in 0..16384
        {
            const std::int16_t *quarter = detail::kSineQuarter;
            const std::uint32_t index = a >> 8;
            const std::int32_t frac = static_cast<std::int32_t>(a & 255);
            std::int32_t fine = quarter[index >= 64 ? 64 : index];
            // A step is at most 402 and frac at most 255: both 16-bit.
            if (index < 64)
                fine += smlabb(quarter[index + 1] - quarter[index], frac, 128) >> 8;
            return (fine + 2) >> 2;
        };
        const auto wave = [quarterSine](std::uint16_t a) -> fx
        {
            const std::uint32_t quadrant = a >> 14;
            const std::uint32_t within = a & 16383;
            switch (quadrant)
            {
            case 0:  return quarterSine(within);
            case 1:  return quarterSine(16384 - within);
            case 2:  return -quarterSine(within);
            default: return -quarterSine(16384 - within);
            }
        };
        sine = wave(angle);
        cosine = wave(static_cast<std::uint16_t>(angle + 16384));
    }

    // --- Host-side conversions. constexpr and double-based, so they vanish from
    // any translation unit that never calls them; the ARM9 never does.
    constexpr std::int32_t roundToInt(double v)
    {
        return static_cast<std::int32_t>(v < 0 ? v - 0.5 : v + 0.5);
    }
    constexpr fx fxFromDouble(double units) { return roundToInt(units * kFxOne); }
    constexpr std::int32_t normalFromDouble(double n) { return roundToInt(n * kNormalOne); }
    constexpr std::int16_t vertexFromDouble(double units)
    {
        return static_cast<std::int16_t>(roundToInt(units * (1 << kVertexShift)));
    }
    /// Degrees to turns, wrapping.
    constexpr std::uint16_t angleFromDegrees(double degrees)
    {
        return static_cast<std::uint16_t>(roundToInt(degrees * (65536.0 / 360.0)) & 0xFFFF);
    }
    constexpr std::uint16_t rgb555(int r, int g, int b)
    {
        return static_cast<std::uint16_t>((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
    }
    /// The same to the nearest of the 32, not the one below: for light,
    /// where the one below is a shade darker over a whole wall.
    constexpr std::uint16_t rgb555Near(int r, int g, int b)
    {
        const int r5 = (r * 31 + 127) / 255, g5 = (g * 31 + 127) / 255, b5 = (b * 31 + 127) / 255;
        return static_cast<std::uint16_t>(r5 | (g5 << 5) | (b5 << 10));
    }
    /// The direction of (x, y) as an angle in turns (65536 to the circle;
    /// 0 along +x, a quarter turn along +y). Good to a tenth of a degree,
    /// which is what steering a train along its track needs; (0, 0) gives 0.
    HL_WARM constexpr std::uint16_t fxAtan2Turns(fx y, fx x)
    {
        const std::int64_t ax = x < 0 ? -static_cast<std::int64_t>(x) : x;
        const std::int64_t ay = y < 0 ? -static_cast<std::int64_t>(y) : y;
        if (ax == 0 && ay == 0)
            return 0;
        // The angle within the first eighth of a turn, from the ratio of the
        // smaller to the larger: atan(z) = (pi/4) z - z (z - 1) (0.2447 +
        // 0.0663 z), with everything already in turns * 65536 and z in Q15.
        // (z is 0..32768, so these fit 32 bits; z and 32768 - z are 16-bit
        // except where the other is 0, so SMULBB's product is right there
        // too. bend is at most 8192 and the bracket 3244: 16-bit.)
        const auto z = static_cast<std::int32_t>(ax >= ay ? (ay << 15) / ax : (ax << 15) / ay);
        const std::int32_t bend = smulbb(z, 32768 - z) >> 15;
        std::int64_t angle = (z >> 2) + (smulbb(bend, 2552 + ((692 * z) >> 15)) >> 15);
        if (ay > ax)
            angle = 16384 - angle;
        if (x < 0)
            angle = 32768 - angle;
        if (y < 0)
            angle = 65536 - angle;
        return static_cast<std::uint16_t>(angle);
    }
    /// Sine and cosine of an angle in turns, in Q30: for turning things that
    /// are a long way from what they turn about.
    ///
    /// fxSinCos is good to a part in four thousand, and a matrix made of such
    /// values stretches or shrinks what it turns by as much -- a twentieth of
    /// a unit at a hundred units from the pivot, which is more than the gap
    /// the collision keeps between the player and a wall. Riding in a tram
    /// that turns needs better: this is good to a part in ten million.
    HL_HOT constexpr void fxSinCosFine(std::uint16_t angle, std::int32_t &sine, std::int32_t &cosine)
    {
        // Fold into the first eighth of a turn, where the series below needs
        // few terms.
        const std::uint32_t quadrant = angle >> 14;
        std::uint32_t within = angle & 16383;       // 0..16383 of a quarter turn
        const bool mirrored = within > 8192;        // past 45 degrees: swap the two
        if (mirrored)
            within = 16384 - within;
        // Radians in Q30: 2 pi / 65536 * 2^30 = 102943.7086..., taken as
        // 6588397 / 64 to keep the last digits.
        const std::int64_t t = (static_cast<std::int64_t>(within) * 6588397) >> 6;
        // (t2 is under 2^30 and never negative: divided as 32 bits, by a
        // constant, it is a multiply, and the same quotient. As 64 bits each
        // was a trip to the divider.)
        const std::uint32_t t2 = static_cast<std::uint32_t>((t * t) >> 30);
        // sin t = t (1 - t2/6 (1 - t2/20 (1 - t2/42 (1 - t2/72))))
        // cos t = 1 - t2/2 (1 - t2/12 (1 - t2/30 (1 - t2/56 (1 - t2/90))))
        constexpr std::int64_t one = std::int64_t(1) << 30;
        std::int64_t s = one - t2 / 72;
        s = one - (std::int64_t(t2 / 42) * s >> 30);
        s = one - (std::int64_t(t2 / 20) * s >> 30);
        s = one - (std::int64_t(t2 / 6) * s >> 30);
        s = (t * s) >> 30;
        std::int64_t c = one - t2 / 90;
        c = one - (std::int64_t(t2 / 56) * c >> 30);
        c = one - (std::int64_t(t2 / 30) * c >> 30);
        c = one - (std::int64_t(t2 / 12) * c >> 30);
        c = one - (std::int64_t(t2 / 2) * c >> 30);
        if (mirrored)
        {
            const std::int64_t swap = s;
            s = c;
            c = swap;
        }
        switch (quadrant)
        {
        case 0:  sine = static_cast<std::int32_t>(s);  cosine = static_cast<std::int32_t>(c);  break;
        case 1:  sine = static_cast<std::int32_t>(c);  cosine = static_cast<std::int32_t>(-s); break;
        case 2:  sine = static_cast<std::int32_t>(-s); cosine = static_cast<std::int32_t>(-c); break;
        default: sine = static_cast<std::int32_t>(-c); cosine = static_cast<std::int32_t>(s);  break;
        }
    }

    namespace detail
    {
        /// The sine of each whole degree of a quarter turn, Q30: the lines
        /// fxVecToYawTurns holds a way against, exactly.
        inline constexpr std::int32_t kSineDegree[91] = {
            0, 18739379, 37473049, 56195305, 74900443, 93582766, 112236583, 130856211,
            149435979, 167970228, 186453311, 204879599, 223243478, 241539355, 259761657, 277904834,
            295963357, 313931728, 331804471, 349576144, 367241333, 384794656, 402230767, 419544355,
            436730145, 453782903, 470697435, 487468587, 504091252, 520560366, 536870912, 553017922,
            568996477, 584801711, 600428808, 615873009, 631129609, 646193961, 661061475, 675727625,
            690187940, 704438018, 718473518, 732290163, 745883746, 759250125, 772385229, 785285058,
            797945680, 810363241, 822533958, 834454122, 846120104, 857528349, 868675383, 879557810,
            890172315, 900515665, 910584710, 920376381, 929887697, 939115760, 948057759, 956710970,
            965072759, 973140576, 980911966, 988384560, 995556083, 1002424350, 1008987269, 1015242840,
            1021189159, 1026824413, 1032146887, 1037154959, 1041847103, 1046221891, 1050277989, 1054014162,
            1057429273, 1060522280, 1063292242, 1065738315, 1067859754, 1069655912, 1071126243, 1072270298,
            1073087729, 1073578288, 1073741824,
        };
    } // namespace detail

    /// The engine's VecToYaw: the way (y, x) points as atan2's degrees made
    /// an int -- cut toward zero, a turn added below 0 -- in 65536ths; (0, 0)
    /// gives 0. A monster's ideal yaw is whole degrees in the game: this.
    ///
    /// fxAtan2Turns is good to a tenth of a degree, so the whole degree is
    /// read from it unless it lies within 0.15 of one; there, which side of
    /// that degree's line (y, x) is on is told exactly, by the sign of a
    /// cross product with it (kSineDegree). (Called, not inlined: the
    /// walkers that ask for it are in ITCM, which has no room for it.)
    HL_WARM __attribute__((noinline)) constexpr std::uint16_t fxVecToYawTurns(fx y, fx x)
    {
        if (x == 0 && y == 0)
            return 0;
        // (Cut toward zero: below 180 the degree under it, above it the
        // degree over it. q is the angle in degrees, Q16.)
        const bool up = y >= 0;
        const std::uint32_t q = static_cast<std::uint32_t>(fxAtan2Turns(y, x)) * 360u;
        const int whole = static_cast<int>(q >> 16);
        const std::uint32_t part = q & 0xFFFFu;
        constexpr std::uint32_t kDoubt = 9830; // 0.15 degree, Q16
        // Which side of the line of degree b (y, x) is: above 0 anticlockwise
        // of it, below 0 clockwise, 0 on it.
        const auto side = [&](int b) {
            const int at = ((b % 360) + 360) % 360;
            const int within = at % 90; // (the quarter's sine and cosine, turned)
            const std::int64_t s0 = detail::kSineDegree[within], c0 = detail::kSineDegree[90 - within];
            const int quarter = at / 90;
            const std::int64_t sine = quarter == 0 ? s0 : quarter == 1 ? c0 : quarter == 2 ? -s0 : -c0;
            const std::int64_t cosine = quarter == 0 ? c0 : quarter == 1 ? -s0 : quarter == 2 ? -c0 : s0;
            return cosine * y - sine * x;
        };
        int deg;
        if (part > kDoubt && part < 65536u - kDoubt)
            deg = up ? whole : whole + 1;
        else
        {
            // (The degree in doubt: `whole` near the bottom of one, `whole + 1`
            // near its top.)
            const int b = part <= kDoubt ? whole : whole + 1;
            const std::int64_t at = side(b);
            deg = up ? (at >= 0 ? b : b - 1) : (at <= 0 ? b : b + 1);
        }
        deg = ((deg % 360) + 360) % 360;
        return static_cast<std::uint16_t>((deg * 65536 + 180) / 360);
    }
} // namespace hldata
