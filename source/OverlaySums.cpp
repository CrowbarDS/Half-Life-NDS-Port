#include "Overlay.hpp"

#if HL_OVL_COUNT > 0 && defined(HL_OVL_FROM_ROM)
// Each overlay's sum, in the manifest's order (0 for one with nothing on the
// card): tools/ovl.py stamp writes them over this placeholder once the
// program is linked, before its ID (tools/arm9_post_link.py). A file read
// from the ROM's overlay table is checked against it (Overlay.arm.cpp).
// Defined here alone, and read elsewhere only through its declaration, so
// that nothing is compiled with the placeholder's values in place of it.
// (Not zero: in the program's constants, where the stamp can reach it, not
// its zeroed data.)
extern "C" __attribute__((used)) const std::uint32_t hlOvlSums[HL_OVL_COUNT] = {
#define HL_OVL_SUM_PLACEHOLDER(I, name, R, loads) 0x4D55534Fu,
    HL_OVL_OVERLAYS(HL_OVL_SUM_PLACEHOLDER)
#undef HL_OVL_SUM_PLACEHOLDER
};
#endif
