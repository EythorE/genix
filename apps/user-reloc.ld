/*
 * Linker script for relocatable Genix user programs
 *
 * Links at base address 0 so all absolute addresses are zero-based
 * offsets, ready for relocation at load time. Used with --emit-relocs
 * to preserve relocation entries for mkbin to extract.
 *
 * With -msep-data, GCC generates a5-relative data access through a GOT
 * (Global Offset Table). The .got and .got.plt sections go between
 * .data and .bss so they're part of the data region copied to RAM.
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
        /* Pad to 4-byte boundary so the data section (and thus GOT)
         * starts at an aligned offset. Required because a5 (GOT base)
         * is computed as data_addr + got_offset, and the 68000 faults
         * on word/long access at odd addresses. */
        . = ALIGN(4);
        _text_end = .;
    }

    _text_size = _text_end - _text_start;

    .data : {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    }

    /* GOT for -msep-data: a5-relative data access.
     * Must be in the data region (copied to RAM with .data).
     * ALIGN(4) ensures GOT entries are at even addresses (68000 requires
     * even alignment for word/long access). */
    . = ALIGN(4);
    .got : {
        _got_start = .;
        *(.got .got.plt)
        _got_end = .;
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
