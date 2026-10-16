/* Fire app linker script: app lives in sectors 2-4 (0x08008000-0x0801FFFF, 96 KB).
   Sector 0 = Akai bootloader, sector 1 = settings: never touched.
   The last 16 bytes of the region hold the bootloader's "EndOfFlashToken". */
MEMORY
{
    APP   (rx)  : ORIGIN = 0x08008000, LENGTH = 0x17FF0
    TOKEN (r)   : ORIGIN = 0x0801FFF0, LENGTH = 16
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .vectors : { KEEP(*(.vectors)) } > APP
    .text    : { *(.text*) *(.rodata*) } > APP
    .token   : { KEEP(*(.token)) } > TOKEN
    /DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
