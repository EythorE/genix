/*
 * Genix kernel linker script for 68000
 * Workbench SBC: RAM at 0x000000 - 0x0FFFFF
 */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY
{
    ram (rwx) : ORIGIN = 0x000000, LENGTH = 1M
}

_stack_top = 0x100000;  /* Top of 1MB RAM */

SECTIONS
{
    .vectors 0x000000 : {
        *(.vectors)
    } > ram

    .text : {
        *(.text .text.*)
    } > ram

    .rodata : {
        *(.rodata .rodata.*)
    } > ram

    .data : {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    } > ram

    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    } > ram

    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
