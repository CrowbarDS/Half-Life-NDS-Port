#pragma once

// The two overlays of the probe, in one region (overlays.json): one in
// Thumb, one in ARM, each with code, constants, data and zeroed data.

namespace one
{
    int compute(int x);  ///< x * 3 + 1, by its constant table
    int bssCounter();    ///< its zeroed counter, then one more
    int dataBump();      ///< its first data word, then one more
} // namespace one

namespace two
{
    int compute(int x);  ///< x * 7 + 2
    int bssCounter();
    int dataBump();
} // namespace two
