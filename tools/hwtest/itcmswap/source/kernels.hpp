#pragma once
#include <cstdint>

/// `bytes` (a multiple of 32) from `from` to `to`, 32 at a time: an LDM of
/// eight registers and an STM of eight (what an ITCM overlay's copy would be).
void copy32(void *to, const void *from, std::uint32_t bytes);

/// Run ARM code at `code` (a chain of branches ending in a return).
void runChain(const void *code);
