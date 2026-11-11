// A test payload that does nothing but what TwlBg did: call its per-frame
// input routine. Built like feed.c (same head) to tell whether code in the
// room runs at all.

#include <stdint.h>

typedef void (*Routine)(void);

static void passEntry(void);

struct Head
{
    uint32_t unused;
    Routine frame;
    const Routine *table;
    const uint16_t *pad;
    void *state;
    uint8_t yawAxis, pitchAxis, unused2[2];
};

__attribute__((section(".head"), used)) struct Head head = {0, passEntry, 0, 0, 0, 1, 0, {0, 0}};

static void passEntry(void)
{
    head.table[1]();
}
