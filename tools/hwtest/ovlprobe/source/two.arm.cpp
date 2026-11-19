// Overlay `two` (ARM): code, constants, data, zeroed data -- and more of it
// than `one`: it is the region's sizer.
#include "probe.hpp"

namespace
{
    const int kTimes[8] = {7, 7, 7, 7, 7, 7, 7, 7};
    // (Kept whole, and read: it makes `two` the larger of the two.)
    volatile const char kPad[2048] = {2};
    int data[4] = {201, 202, 203, 204};
    int counter[64];
} // namespace

namespace two
{
    int compute(int x) { return x * kTimes[x & 7] + kPad[0]; }
    int bssCounter() { return counter[0]++; }
    int dataBump() { return data[0]++; }
} // namespace two
