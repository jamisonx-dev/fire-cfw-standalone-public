/* Fire app linker script (C runtime version): app in sectors 2-4, 0x08008000-0x0801FFFF.
   Sector 0 = Akai bootloader, sector 1 = settings: never touched.
   The last 16 bytes hold the bootloader's "EndOfFlashToken". */
ENTRY(Reset_Handler)

MEMORY
{
    APP   (rx)  : ORIGIN = 0x08008000, LENGTH = 0x17FF0
    TOKEN (r)   : ORIGIN = 0x0801FFF0, LENGTH = 16
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Stack_Size = 0x1000;

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > APP

    .text : {
        . = ALIGN(4);
        *(.text*) *(.rodata*) *(.glue_7) *(.glue_7t)
        KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4);
    } > APP

    .ARM.exidx : { *(.ARM.exidx*) } > APP

    .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > APP
    .init_array    : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > APP
    .fini_array    : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > APP

    _sidata = LOADADDR(.data);
    .data : {
        . = ALIGN(4); _sdata = .;
        *(.data*)
        . = ALIGN(4); _edata = .;
    } > RAM AT > APP

    .bss (NOLOAD) : {
        . = ALIGN(4); _sbss = .; __bss_start__ = _sbss;
        *(.bss*) *(COMMON)
        . = ALIGN(4); _ebss = .; __bss_end__ = _ebss;
    } > RAM

    /* Kept across a reset (never zeroed, never stack-painted): the stack snapshot of the last crash (main.c). */
    .noinit (NOLOAD) : {
        . = ALIGN(4);
        *(.noinit*)
        . = ALIGN(4); _enoinit = .;
    } > RAM

    end = _enoinit;
    ASSERT(_enoinit + _Min_Stack_Size <= _estack, "RAM overflow")

    .token : { KEEP(*(.token)) } > TOKEN

    /DISCARD/ : { *(.comment) }
}
