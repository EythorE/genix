/*
 * VDP (Video Display Processor) initialization and low-level routines
 *
 * Handles VDP register setup, VRAM clear, palette loading, and font
 * tile upload. Uses fast assembly loops for performance on real hardware.
 *
 * Adapted from FUZIX platform-megadrive
 * Source: https://github.com/EythorE/FUZIX/tree/megadrive/Kernel/platform/platform-megadrive
 */

#include "control_ports.def"

.equ vdp_cmd_vram_write, 0x40000000
.equ vdp_cmd_vsram_a_write, 0x40000010
.equ vdp_cmd_vsram_b_write, 0x40020010
.equ vdp_cmd_cram_write, 0xC0000000

    .section .text
    .globl VDP_LoadRegisters
    .globl VDP_ClearVRAM
    .globl VDP_writePalette
    .globl VDP_clearPalette
    .globl VDP_fontInit
    .globl VDP_clear
    .globl VDP_reinit

/*==============================================================
 * VDP_clear — clear everything and reload registers
 *==============================================================*/
VDP_clear:
    movem.l %d0/%d1/%a0/%a1, -(%sp)
    bsr VDP_clearPalette
    bsr VDP_ClearVRAM
    bsr VDP_LoadRegisters
    move.l #vdp_cmd_vsram_a_write,(VDP_CONTROL)
    move.w #0,(VDP_DATA)
    move.l #vdp_cmd_vsram_b_write,(VDP_CONTROL)
    move.w #0,(VDP_DATA)
    movem.l (%sp)+, %d0/%d1/%a0/%a1
    rts

/*==============================================================
 * VDP_reinit — full reinit with palette and font
 *==============================================================*/
VDP_reinit:
    movem.l %d0/%d1/%a0/%a1, -(%sp)
    bsr VDP_clearPalette
    bsr VDP_ClearVRAM
    bsr VDP_LoadRegisters
    bsr VDP_writePalette
    move.l #vdp_cmd_vsram_a_write,(VDP_CONTROL)
    move.w #0,(VDP_DATA)
    move.l #vdp_cmd_vsram_b_write,(VDP_CONTROL)
    move.w #0,(VDP_DATA)
    bsr VDP_fontInit
    movem.l (%sp)+, %d0/%d1/%a0/%a1
    rts

/*==============================================================
 * Clear VRAM (64KB)
 *==============================================================*/
.set size_long, 4
.set vram_loop_counter, (0x00010000 / size_long) - 1
VDP_ClearVRAM:
    move.l #vdp_cmd_vram_write, (VDP_CONTROL)
    move.w #vram_loop_counter, %d0
ClrVramLp:
    move.l #0x0, (VDP_DATA)
    dbra %d0, ClrVramLp
    rts

/*==============================================================
 * Write palette to CRAM (colour memory)
 *==============================================================*/
VDP_writePalette:
    move.l #vdp_cmd_cram_write, VDP_CONTROL
    lea Palette, %a0
    move.w #31, %d0
PalLp:
    move.l (%a0)+, VDP_DATA
    dbra %d0, PalLp
    rts

VDP_clearPalette:
    move.l #vdp_cmd_cram_write, VDP_CONTROL
    move.l #31, %d0
    move.l #0, %d1
ClPalLp:
    move.l %d1, VDP_DATA
    dbra %d0, ClPalLp
    rts

/*==============================================================
 * Load VDP register initial values
 *==============================================================
 * To write a register: write a word to control port.
 * Top bit set (0x8000), bits 8-12 = register, bottom byte = value.
 */
VDP_LoadRegisters:
    lea VDPRegisters, %a0
    move.w #0x18-1, %d0       /* 24 registers */
    move.w #0x8000, %d1       /* 'Set register 0' command */
CopyRegLp:
    move.b (%a0)+, %d1
    move.w %d1, (VDP_CONTROL)
    addi.w #0x0100, %d1       /* Next register */
    dbra %d0, (CopyRegLp)
    rts

/*==============================================================
 * Write font glyphs to VRAM
 *==============================================================
 * Expands 1bpp font data to 4bpp tiles for VDP.
 * Each byte of source becomes a longword (8 pixels, 4 bits each).
 * Background nibble is set to 1 (non-transparent).
 */
#define FONTDATAMAX 2048
.equ font_loop_counter, FONTDATAMAX-1

VDP_fontInit:
    move.l  #font_loop_counter, %d2
    lea fontdata_8x8, %a1
    /* Start writes at VRAM address 0 (tile 0) */
    move.l #vdp_cmd_vram_write, VDP_CONTROL
nextFont:
    move.b  (%a1)+, %d0        /* Load font byte */
    clr.l %d1                  /* Clear destination longword */

    /* Rotate first bit into d1 through extend */
    roxl.b  #1, %d0
    roxl.l  #1, %d1

    move.l  #6, %d3            /* Do 7 more times (-1 for dbra) */
rolin:
    rol.l   #3, %d1            /* Shift accumulated bits left by 3 */
    roxl.b  #1, %d0            /* Rotate next source bit into X */
    roxl.l  #1, %d1            /* Rotate X into d1 */
    dbra    %d3, rolin

    /* Add 1 to each nibble so background is non-transparent */
    add.l #0x11111111,%d1

    move.l  %d1, (VDP_DATA)    /* Write longword to VDP */
    dbra    %d2, nextFont
    rts

/*==============================================================
 * DATA
 *==============================================================*/
    .section .rodata
    .globl VDPRegisters
    .globl Palette

/*==============================================================
 * VDP Register initial values (24 registers)
 *==============================================================*/
VDPRegisters:
    .byte 0x14  /* $00: H interrupt on, palettes on */
    .byte 0x74  /* $01: V interrupt on, display on, DMA on, Genesis mode */
    .byte 0x30  /* $02: Plane A at VRAM $C000 */
    .byte 0x00  /* $03: Window Plane disabled */
    .byte 0x07  /* $04: Plane B at VRAM $E000 */
    .byte 0x78  /* $05: Sprite table at VRAM $F000 */
    .byte 0x00  /* $06: Unused */
    .byte 0x00  /* $07: Background colour: palette 0, colour 0 */
    .byte 0x00  /* $08: Unused */
    .byte 0x00  /* $09: Unused */
    .byte 0x08  /* $0A: H-interrupt frequency */
    .byte 0x00  /* $0B: Full screen scroll */
    .byte 0x81  /* $0C: H40 mode (320x224), no interlace */
    .byte 0x3F  /* $0D: H-scroll table at VRAM $FC00 */
    .byte 0x00  /* $0E: Unused */
    .byte 0x02  /* $0F: Auto-increment 2 bytes */
    .byte 0x01  /* $10: Scroll size: 64x32 tiles */
    .byte 0x00  /* $11: Window H pos */
    .byte 0x00  /* $12: Window V pos */
    .byte 0xFF  /* $13: DMA length lo */
    .byte 0xFF  /* $14: DMA length hi */
    .byte 0x00  /* $15: DMA source lo */
    .byte 0x00  /* $16: DMA source mid */
    .byte 0x80  /* $17: DMA source hi, memory-to-VRAM mode */

/*==============================================================
 * Palette data (4 palettes x 16 colours = 64 words)
 *==============================================================
 * Format: 0000 BBB0 GGG0 RRR0
 * Palette 0: console text (black bg, white fg)
 * Palette 1: debug overlay background
 * Palette 2: cursor
 * Palette 3: reserved
 */
Palette:
    /* Palette 0 — main console */
    .word 0x0000  /* Colour 0 = Transparent / background */
    .word 0x0000  /* Colour 1 = Black */
    .word 0x0CCC  /* Colour 2 = White */
    .word 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
    .word 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000

    /* Palette 1 — debug overlay */
    .word 0x0000  /* Transparent */
    .word 0x0CCC  /* Background */
    .word 0x022C  /* Foreground */
    .word 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
    .word 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000

    /* Palette 2 — cursor */
    .word 0x0000
    .word 0x0AAA
    .word 0x0222
    .word 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
    .word 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000

    /* Palette 3 — reserved */
    .word 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
    .word 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
