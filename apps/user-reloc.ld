/*
 * Linker script for relocatable Genix user programs
 *
 * Links at base address 0 so all absolute addresses are zero-based
 * offsets, ready for relocation at load time. Used with --emit-relocs
 * to preserve relocation entries for mkbin to extract.
 *
 * Replaces the old user.ld (0x040000) and user-md.ld (0xFF9000) —
 * one binary works on both platforms via runtime relocation.
 */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

SECTIONS
{
    . = 0;

    .text : {
        _text_start = .;
        *(.text .text.*)
        *(.rodata .rodata.*)
        _text_end = .;
    }

    _text_size = _text_end - _text_start;

    .data : {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    }

    .bss : {
        _bss_start = .;
        *(.bss .bss.* COMMON)
        _bss_end = .;
    }

    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
