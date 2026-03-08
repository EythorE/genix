/*
 * VDP terminal output — character plotting, scrolling, and cursor
 *
 * Provides plot_char, clear_across, clear_lines, scroll_up, scroll_down,
 * and cursor operations for the VT layer. Works with Plane A nametable
 * at VRAM 0xC000 with hardware vertical scrolling.
 *
 * Adapted from FUZIX platform-megadrive
 * Source: https://github.com/EythorE/FUZIX/tree/megadrive/Kernel/platform/platform-megadrive
 */

#include "control_ports.def"

#define VT_HEIGHT   28

.equ vdp_cmd_vram_write, 0x40000000
.equ vram_addr_plane_write, 0x40000003  /* Write to Plane A at 0xC000 */
.equ vram_addr_plane_read, 0x00000003   /* Read from Plane A at 0xC000 */
.equ vdp_cmd_vsram_write, 0x40000010   /* V-scroll for Plane A */
.equ vram_addr_sprite_write, 0x70000003 /* Sprite table at 0xF000 */

    .globl vt_scroll_amount
    .section .data
vt_scroll_amount:
    .word 0
    .align 2

    .globl do_beep, cursor_disable, cursor_off, cursor_on
    .globl clear_across, clear_lines, scroll_up, scroll_down, plot_char
    .section .text

/* void do_beep(void) */
do_beep:
    rts

/* void cursor_disable(void) */
cursor_disable:
    rts

/* void cursor_off(void) */
cursor_off:
    rts

/*--------------------------------------------------------------
 * read_cursor_char — read the tile under the cursor
 * args: y in d0, x in d1
 * returns: tile number in d4
 *--------------------------------------------------------------*/
read_cursor_char:
    movem.l %d2-%d3,-(%sp)
    clr.l %d2
    move.w (vt_scroll_amount), %d2
    lsr.w   #3, %d2              /* pixels to tile rows */
    add.w %d0, %d2
    and.w #0x1F, %d2             /* modulo 32 rows */

    move.l #vram_addr_plane_read, %d3
    swap %d2
    rol.l #7, %d2
    add.l %d2, %d3

    move.b %d1, %d2
    swap %d2
    rol.l #1, %d2
    add.l %d2, %d3

    move.l %d3, (VDP_CONTROL)
    move.w (VDP_DATA), %d4
    movem.l (%sp)+, %d2-%d3
    rts

/*--------------------------------------------------------------
 * cursor_sprite — position sprite for cursor display
 * y in d0, x in d1, tile in d4
 *--------------------------------------------------------------*/
cursor_sprite:
    move.l #vram_addr_sprite_write, (VDP_CONTROL)
    lsl.w   #3, %d0              /* y*8 pixels */
    lsl.w   #3, %d1              /* x*8 pixels */
    add.l #128, %d0              /* sprite offset */
    add.l #128, %d1

    swap %d0
    swap %d1
    move.w #0b0100000000000000,%d1  /* priority, palette 2 */
    move.b %d4, %d1
    swap %d1

    move.l %d0, (VDP_DATA)
    move.l %d1, (VDP_DATA)
    rts

/* void cursor_on(int8_t y, int8_t x) */
cursor_on:
    movem.l %d0-%d1, -(%sp)
    move.l 12(%sp),%d0
    move.l 16(%sp),%d1
    bsr read_cursor_char
    bsr cursor_sprite
    movem.l (%sp)+, %d0-%d1
    rts

/*--------------------------------------------------------------
 * void clear_across(int8_t y, int8_t x, int16_t num)
 *--------------------------------------------------------------*/
clear_across:
    move.l #vram_addr_plane_write, %d1

    clr.l %d0
    move.w (vt_scroll_amount), %d0
    lsr.w   #3, %d0
    add.w 6(%sp),%d0             /* y + scroll offset */
    and.w #0x1F, %d0

    swap %d0
    rol.l #7, %d0
    add.l %d0, %d1

    move.b 11(%sp), %d0          /* x */
    swap %d0
    rol.l #1, %d0
    add.l %d0, %d1

    move.l %d1, (VDP_CONTROL)
    clr.l %d0

    move.l 12(%sp), %d1          /* num */
    bra   _clr_loop
clr_loop:
    move.w %d0, (VDP_DATA)
_clr_loop:
    dbra.w    %d1, clr_loop
    rts

/*--------------------------------------------------------------
 * void clear_lines(int8_t y, int8_t num)
 *--------------------------------------------------------------*/
clear_lines:
    movem.l %d0-%d5, -(%sp)
    move.w (vt_scroll_amount), %d1
    move.w 30(%sp), %d2          /* y (+24 bytes pushed) */
    move.w 34(%sp), %d3          /* num */
    move.l #vram_addr_plane_write,%d4
    clr.l %d5

    bra   _clrline_loop
clrline_loop:
    clr.l %d0
    move.w %d1, %d0
    lsr.w   #3, %d0
    add.w %d2,%d0
    and.w #0x1F, %d0

    lsl.w #7, %d0               /* y * 128 bytes per row */
    swap %d0
    or.l %d4,%d0
    move.l %d0, (VDP_CONTROL)

    moveq #19, %d0               /* 40 tiles = 20 longs */
_clearAChar:
    move.l %d5, (VDP_DATA)
    dbra %d0, _clearAChar

    addq.w #1,%d2
_clrline_loop:
    dbra.w    %d3, clrline_loop

    movem.l (%sp)+, %d0-%d5
    rts

/*--------------------------------------------------------------
 * void scroll_up(void) — scroll display up one line (8 pixels)
 *--------------------------------------------------------------*/
scroll_up:
    move.w (vt_scroll_amount), %d0
    add.w #8, %d0
    move.w %d0, (vt_scroll_amount)
    move.l #vdp_cmd_vsram_write, VDP_CONTROL
    move.w %d0, (VDP_DATA)
    rts

/*--------------------------------------------------------------
 * void scroll_down(void) — scroll display down one line
 *--------------------------------------------------------------*/
scroll_down:
    move.w (vt_scroll_amount), %d0
    sub.w #8, %d0
    move.w %d0, (vt_scroll_amount)
    move.l #vdp_cmd_vsram_write, VDP_CONTROL
    move.w %d0, (VDP_DATA)
    rts

/*--------------------------------------------------------------
 * void plot_char(int8_t y, int8_t x, uint16_t c)
 * Write a character tile to the nametable
 *--------------------------------------------------------------*/
plot_char:
    clr.l %d0
    move.w (vt_scroll_amount), %d0
    lsr.w   #3, %d0
    add.w 6(%sp),%d0             /* y + scroll offset */
    and.w #0x1F, %d0

    move.l #vram_addr_plane_write, %d1
    swap %d0
    rol.l #7, %d0
    add.l %d0, %d1

    move.b 11(%sp), %d0          /* x */
    swap %d0
    rol.l #1, %d0
    add.l %d0, %d1

    move.l %d1, (VDP_CONTROL)
    move.w 14(%sp), %d0          /* character/tile index */
    move.w %d0, (VDP_DATA)

    clr.l %d0
    move.l %d0,%d1
    rts
