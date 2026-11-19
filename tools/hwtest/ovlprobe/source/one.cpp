// Overlay `one` (Thumb): code, constants, data, zeroed data.
#include "probe.hpp"

namespace
{
    const int kTimes[4] = {3, 3, 3, 3};
    int data[4] = {101, 102, 103, 104};
    int counter[4];
#ifdef NEG_CTOR
    // (A negative build: a static constructor in an overlay, which
    // tools/ovl.py check must refuse.)
    struct Made
    {
        Made() { counter[1] = 7; }
    } made;
#endif
#ifdef NEG_BIG
    // (A negative build: larger than the region's sizer, which the link's
    // assertion must refuse.)
    volatile const char kBig[64 * 1024] = {1};
#endif
} // namespace

namespace one
{
    int compute(int x)
    {
#ifdef NEG_BIG
        return x * kTimes[x & 3] + 1 + kBig[x & 1];
#else
        return x * kTimes[x & 3] + 1;
#endif
    }
    int bssCounter() { return counter[0]++; }
    int dataBump() { return data[0]++; }
} // namespace one
