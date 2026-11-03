// What the probe runs, as ARM: shared/HlDsp.hpp's instructions against the
// plain arithmetic they stand for, and the loops it times.

#include <cstdint>

#include "HlDsp.hpp"
#include "kernels.hpp"

using namespace hldata;

int wrongAnswers(const std::int32_t *a, const std::int32_t *b, const std::int32_t *c, int n, int *firstOp)
{
    int wrong = 0;
    *firstOp = -1;
    for (int i = 0; i < n; ++i)
    {
        const std::int32_t x = a[i], y = b[i], z = c[i];
        const std::int32_t got[12] = {smulbb(x, y),    smulbt(x, y),    smultb(x, y),    smultt(x, y),
                                      smulwb(x, y),    smulwt(x, y),    smlabb(x, y, z), smlabt(x, y, z),
                                      smlatb(x, y, z), smlatt(x, y, z), smlawb(x, y, z), smlawt(x, y, z)};
        const std::int32_t want[12] = {
            dsp::bottom(x) * dsp::bottom(y),
            dsp::bottom(x) * dsp::top(y),
            dsp::top(x) * dsp::bottom(y),
            dsp::top(x) * dsp::top(y),
            dsp::wide(x, dsp::bottom(y)),
            dsp::wide(x, dsp::top(y)),
            dsp::wrapAdd(z, dsp::bottom(x) * dsp::bottom(y)),
            dsp::wrapAdd(z, dsp::bottom(x) * dsp::top(y)),
            dsp::wrapAdd(z, dsp::top(x) * dsp::bottom(y)),
            dsp::wrapAdd(z, dsp::top(x) * dsp::top(y)),
            dsp::wrapAdd(z, dsp::wide(x, dsp::bottom(y))),
            dsp::wrapAdd(z, dsp::wide(x, dsp::top(y))),
        };
        for (int op = 0; op < 12; ++op)
        {
            if (got[op] != want[op])
            {
                ++wrong;
                if (*firstOp < 0)
                    *firstOp = op;
            }
        }
    }
    return wrong;
}

// A Q12 value times a Q16 time step, >> 16 (the game's scaleTime).
std::int32_t scaleWide(const std::int32_t *v, const std::int32_t *dt, int n)
{
    std::int32_t sum = 0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<std::int32_t>((static_cast<std::int64_t>(v[i]) * dt[i]) >> 16);
    return sum;
}
std::int32_t scaleDsp(const std::int32_t *v, const std::int32_t *dt, int n)
{
    std::int32_t sum = 0;
    for (int i = 0; i < n; ++i)
        sum += smulwb(v[i], dt[i]);
    return sum;
}

// Three Q14 products summed, as a bone's rotation is made: through 64 bits
// as the game does, in 32 bits as the compiler would, and by SMLABB.
std::int32_t dotWide(const std::int32_t *a, const std::int32_t *b, int n)
{
    std::int32_t sum = 0;
    for (int i = 0; i + 2 < n; i += 3)
        sum += static_cast<std::int32_t>((static_cast<std::int64_t>(a[i]) * b[i] + static_cast<std::int64_t>(a[i + 1]) * b[i + 1] +
                                          static_cast<std::int64_t>(a[i + 2]) * b[i + 2]) >> 14);
    return sum;
}
std::int32_t dotNarrow(const std::int32_t *a, const std::int32_t *b, int n)
{
    std::int32_t sum = 0;
    for (int i = 0; i + 2 < n; i += 3)
        sum += (a[i] * b[i] + a[i + 1] * b[i + 1] + a[i + 2] * b[i + 2]) >> 14;
    return sum;
}
std::int32_t dotDsp(const std::int32_t *a, const std::int32_t *b, int n)
{
    std::int32_t sum = 0;
    for (int i = 0; i + 2 < n; i += 3)
        sum += smlabb(a[i + 2], b[i + 2], smlabb(a[i + 1], b[i + 1], smulbb(a[i], b[i]))) >> 14;
    return sum;
}
