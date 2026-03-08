/*
 * Genix Mega Drive linker script
 *
 * ROM: 0x000000 - 0x3FFFFF (up to 4MB cartridge)
 * RAM: 0xFF0000 - 0xFFFFFF (64KB main 68000 RAM)
 */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_md_start)

MEMORY
{
    rom (rx)  : ORIGIN = 0x000000, LENGTH = 4M
    ram (rwx) : ORIGIN = 0xFF0000, LENGTH = 64K
}

SECTIONS
{
    .vectors 0x000000 : {
        *(.vectors)
    } > rom

    .header 0x000100 : {
        *(.header)
    } > rom

    .text 0x000200 : {
        *(.text .text.*)
    } > rom

    .rodata : {
        *(.rodata .rodata.*)
    } > rom

    /* ROM disk image is appended here */
    .romdisk : {
        _rom_disk_start = .;
        *(.romdisk)
        _rom_disk_end = .;
    } > rom

    _rom_end = .;

    /* Data section: stored in ROM, copied to RAM at boot */
    .data 0xFF0000 : AT(_rom_end) {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    } > ram
    _data_load = LOADADDR(.data);

    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    } > ram

    _end = .;

    /* Stack at top of RAM */
    _stack_top = 0x01000000;  /* wraps to 0xFFFFFF */

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
