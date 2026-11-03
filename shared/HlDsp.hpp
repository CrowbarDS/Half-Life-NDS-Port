#pragma once

// The ARM9's 16-bit multiplies (ARMv5TE, the "DSP" instructions), for fixed
// point whose operands fit 16 bits.
//
//   smulXY(a, b)        a's half X times b's half Y, each sign-extended: 16 x 16 -> 32
//   smulwY(a, b)        a (32 bits) times b's half Y, the product's bits 47..16
//   smlaXY(a, b, acc)   acc + smulXY(a, b)
//   smlawY(a, b, acc)   acc + smulwY(a, b)
//
// with X and Y b for the bottom half (bits 15..0) or t for the top (31..16).
// A sum wraps as an int's would (the instruction only notes it, in the Q bit).
//
// The compiler never picks these itself -- it widens 16-bit operands to 32 and
// multiplies with MUL, or with SMULL and two shifts where the product is
// wanted >> 16 -- and <arm_acle.h> has only the accumulating ones. They take
// fewer cycles on the ARM946E-S; tools/hwtest/dspmath times them against the
// plain arithmetic and checks they give the same bits. (Kuratius pointed us at
// them.)
//
// Thumb code cannot encode them. In a Thumb file and on the host each is its
// plain arithmetic below, which is what the instruction does: the host's tests
// check the same numbers the console makes. So does a call the compiler can
// work out by itself (both operands known), where the instruction would only
// stop it.

#include <cstdint>
#include <type_traits>

#if defined(__arm__) && !defined(__thumb__) && defined(__ARM_FEATURE_DSP)
#define HL_DSP_ASM 1
#endif

namespace hldata
{
    namespace dsp
    {
        constexpr std::int32_t bottom(std::int32_t x) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(x)); }
        constexpr std::int32_t top(std::int32_t x) { return x >> 16; }
        constexpr std::int32_t wrapAdd(std::int32_t a, std::int32_t b)
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
        }
        constexpr std::int32_t wide(std::int32_t a, std::int32_t half)
        {
            return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * half) >> 16);
        }
    } // namespace dsp

#ifdef HL_DSP_ASM
#define HL_DSP_KNOWN(...) (std::is_constant_evaluated() || (__builtin_constant_p(__VA_ARGS__)))
#define HL_DSP_MUL(name, plain)                                                        \
    [[gnu::always_inline]] constexpr inline std::int32_t name(std::int32_t a, std::int32_t b) \
    {                                                                                  \
        if (HL_DSP_KNOWN(a) && HL_DSP_KNOWN(b))                                        \
            return plain;                                                              \
        std::int32_t out;                                                              \
        asm(#name " %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));                         \
        return out;                                                                    \
    }
#define HL_DSP_MLA(name, plain)                                                                       \
    [[gnu::always_inline]] constexpr inline std::int32_t name(std::int32_t a, std::int32_t b, std::int32_t acc) \
    {                                                                                                 \
        if (HL_DSP_KNOWN(a) && HL_DSP_KNOWN(b) && HL_DSP_KNOWN(acc))                                  \
            return plain;                                                                             \
        std::int32_t out;                                                                             \
        asm(#name " %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(acc));                          \
        return out;                                                                                   \
    }
#else
#define HL_DSP_MUL(name, plain) \
    constexpr inline std::int32_t name(std::int32_t a, std::int32_t b) { return plain; }
#define HL_DSP_MLA(name, plain) \
    constexpr inline std::int32_t name(std::int32_t a, std::int32_t b, std::int32_t acc) { return plain; }
#endif

    HL_DSP_MUL(smulbb, dsp::bottom(a) * dsp::bottom(b))
    HL_DSP_MUL(smulbt, dsp::bottom(a) * dsp::top(b))
    HL_DSP_MUL(smultb, dsp::top(a) * dsp::bottom(b))
    HL_DSP_MUL(smultt, dsp::top(a) * dsp::top(b))
    HL_DSP_MUL(smulwb, dsp::wide(a, dsp::bottom(b)))
    HL_DSP_MUL(smulwt, dsp::wide(a, dsp::top(b)))
    HL_DSP_MLA(smlabb, dsp::wrapAdd(acc, dsp::bottom(a) * dsp::bottom(b)))
    HL_DSP_MLA(smlabt, dsp::wrapAdd(acc, dsp::bottom(a) * dsp::top(b)))
    HL_DSP_MLA(smlatb, dsp::wrapAdd(acc, dsp::top(a) * dsp::bottom(b)))
    HL_DSP_MLA(smlatt, dsp::wrapAdd(acc, dsp::top(a) * dsp::top(b)))
    HL_DSP_MLA(smlawb, dsp::wrapAdd(acc, dsp::wide(a, dsp::bottom(b))))
    HL_DSP_MLA(smlawt, dsp::wrapAdd(acc, dsp::wide(a, dsp::top(b))))

#undef HL_DSP_MUL
#undef HL_DSP_MLA
#undef HL_DSP_KNOWN
} // namespace hldata
