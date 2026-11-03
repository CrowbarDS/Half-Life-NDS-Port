#pragma once

#include <cstdint>

/// How many of the twelve instructions' answers differ from their plain
/// arithmetic over n operand triples; the first op that did, or -1.
int wrongAnswers(const std::int32_t *a, const std::int32_t *b, const std::int32_t *c, int n, int *firstOp);

std::int32_t scaleWide(const std::int32_t *v, const std::int32_t *dt, int n);
std::int32_t scaleDsp(const std::int32_t *v, const std::int32_t *dt, int n);
std::int32_t dotWide(const std::int32_t *a, const std::int32_t *b, int n);
std::int32_t dotNarrow(const std::int32_t *a, const std::int32_t *b, int n);
std::int32_t dotDsp(const std::int32_t *a, const std::int32_t *b, int n);
