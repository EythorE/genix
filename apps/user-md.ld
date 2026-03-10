/*
 * Linker script for Genix user programs — Mega Drive target
 * Programs are loaded at USER_BASE (0xFF9000) in 68000 main RAM.
 * Must match pal_user_base() in pal/megadrive/platform.c.
 * Bumped from 0xFF8000 because per-process kstacks push _end past it.
 */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

SECTIONS
{
    . = 0xFF9000;

    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    }

    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
