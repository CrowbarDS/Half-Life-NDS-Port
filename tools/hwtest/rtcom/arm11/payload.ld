/* The probe's ARM11 program: one flat image at the address TWPatch's rtcom
   loads it to, its entry first, its variables inside the image (it is sent
   as bytes; nothing clears memory for it). */
OUTPUT_ARCH(arm)
ENTRY(payloadEntry)

SECTIONS
{
    . = 0x00201000;
    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        *(.data .data.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
    }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
