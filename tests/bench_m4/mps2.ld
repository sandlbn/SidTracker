/* Linker script for tests/audio_bench.cpp on QEMU mps2-an386 (Cortex-M4):
   code in SSRAM1 (4 MB at 0), data/bss/heap/stack in SSRAM2/3 (4 MB at
   0x20000000), big benchmark buffers (.psram) in the 16 MB PSRAM at 0x21000000. */
MEMORY
{
    CODE  (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
    PSRAM (rw)  : ORIGIN = 0x21000000, LENGTH = 16M
}
ENTRY(Reset_Handler)
SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        *(.text*)
        KEEP(*(.init)) KEEP(*(.fini))
        *(.rodata*)
        . = ALIGN(4);
        __preinit_array_start = .; KEEP(*(.preinit_array)) __preinit_array_end = .;
        __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
        __fini_array_start = .; KEEP(*(.fini_array)) __fini_array_end = .;
    } > CODE
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > CODE
    __data_load = LOADADDR(.data);
    .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > RAM AT > CODE
    .bss (NOLOAD) : { __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(8); __bss_end__ = .; } > RAM
    end = .;
    _end = .;
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    .psram (NOLOAD) : { *(.psram*) } > PSRAM
}
