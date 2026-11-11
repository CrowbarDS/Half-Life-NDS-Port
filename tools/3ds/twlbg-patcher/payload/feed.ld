/* Linked into TwlBg's unused code at 0x3018EC..0x301AD4 (488 bytes, in the
   Old 3DS TwlBg the plan was worked out on). The patcher checks the size
   against the room it finds in the TwlBg it patches. */
ENTRY(head)
MEMORY { room (rx) : ORIGIN = 0x3018EC, LENGTH = 488 }
SECTIONS
{
    .text : { KEEP(*(.head)) *(.text*) *(.rodata*) *(.data*) } > room
    /DISCARD/ : { *(.bss*) *(COMMON) *(.ARM.*) *(.comment) }
}
ASSERT(SIZEOF(.text) <= 488, "payload too large for TwlBg's room")
