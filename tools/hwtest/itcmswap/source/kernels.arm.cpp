// (ARM: the copy's LDM/STM of eight registers, and a call into ARM code.)
#include "kernels.hpp"

void copy32(void *to, const void *from, std::uint32_t bytes)
{
    asm volatile("1:\n"
                 "    ldmia %1!, {r3-r10}\n"
                 "    stmia %0!, {r3-r10}\n"
                 "    subs  %2, %2, #32\n"
                 "    bne   1b\n"
                 : "+r"(to), "+r"(from), "+r"(bytes)
                 :
                 : "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "memory", "cc");
}

void runChain(const void *code)
{
    reinterpret_cast<void (*)()>(const_cast<void *>(code))();
}
