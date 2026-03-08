/*
 * Debug output overlay — renders text on VDP Plane B
 *
 * Provides a toggleable debug overlay (F12) that uses a separate
 * scroll plane for printf-style debug output without disturbing
 * the main console on Plane A.
 *
 * Adapted from FUZIX platform-megadrive
 * Source: https://github.com/EythorE/FUZIX/tree/megadrive/Kernel/platform/platform-megadrive
 */

#include "control_ports.def"

.equ vdp_cmd_vram_write, 0x40000000
.equ vdp_cmd_vsram_write, 0x40020010    /* V-scroll for Plane B */
.equ vram_addr_plane, 0xE000            /* Plane B */
.equ vram_addr_plane_write, 0x60000003  /* Write to Plane B at 0xE000 */
.equ debug_window_size, 20              /* rows */

    .section .data
dbg_plane_enabled:
    .byte 1
dbg_cursor_x:
    .byte 0
dbg_cursor_y:
    .byte 0
dbg_scrolling:
    .byte 0
    .even
dbg_vscroll_amount:
    .word 0

    .section .text
    .globl dbg_toggle, dbg_clear, dbg_printf
    .globl dbg_printChar, dbg_printByte, dbg_printWord, dbg_printLong
    .globl dbg_printString, dbg_newline
    .globl dbg_registers, dbg_memory, dbg_stack, dbg_rte

/*==============================================================
 * dbg_clear — reset debug overlay state
 *==============================================================*/
dbg_clear:
    move.l #vdp_cmd_vsram_write, VDP_CONTROL
    move.w #0,%d0
    move.b %d0,dbg_cursor_x
    move.b %d0,dbg_cursor_y
    move.b %d0,dbg_scrolling
    move.w %d0,dbg_vscroll_amount
    move.w %d0, (VDP_DATA)
    /* Set to on then toggle off to disable the plane */
    move.b #1,dbg_plane_enabled
    bsr dbg_toggle
    rts

/*==============================================================
 * dbg_toggle — toggle Plane B visibility (F12)
 *==============================================================*/
dbg_toggle:
    tst.b   dbg_plane_enabled
    beq     dbg_enable
    /* Disable: set Plane B nametable to 0 (effectively disables it) */
    move.w  #0x8400,(VDP_CONTROL)
    move.b  #0,dbg_plane_enabled
    rts
dbg_enable:
    /* Enable: set Plane B nametable to 0xE000 */
    move.w  #0x8407,(VDP_CONTROL)
    move.b  #1,dbg_plane_enabled
    rts

/*==============================================================
 * dbg_printf — formatted debug output
 *
 * void dbg_printf(char *fmt, ...);
 * Supports: %c (char), %b (byte hex), %w (word hex),
 *           %l (long hex), %s (string)
 *==============================================================*/
dbg_printf:
    movem.l %d0-%d1/%a0-%a1, -(%sp)
    move.l  20(%sp), %a0         /* format string */
    lea     24(%sp), %a1         /* first vararg */

dprintf_loop:
    move.b  (%a0)+, %d0
    beq     dprintf_done
    cmp.b   #'%', %d0
    bne     dprintf_regular

    move.b  (%a0)+, %d0
    beq     dprintf_done

    cmp.b   #'c', %d0
    beq     dprintf_char
    cmp.b   #'b', %d0
    beq     dprintf_byte
    cmp.b   #'w', %d0
    beq     dprintf_word
    cmp.b   #'l', %d0
    beq     dprintf_long
    cmp.b   #'s', %d0
    beq     dprintf_string

    /* Unrecognized: print % and the char */
    move.b  #'%', %d0
    jsr     dbg_printChar
    move.b  -1(%a0), %d0
    jsr     dbg_printChar
    bra     dprintf_loop

dprintf_char:
    move.l  (%a1)+, %d0
dprintf_regular:
    jsr     dbg_printChar
    bra     dprintf_loop

dprintf_byte:
    move.l  (%a1)+, %d0
    jsr     dbg_printByte
    bra     dprintf_loop

dprintf_word:
    move.l  (%a1)+, %d0
    jsr     dbg_printWord
    bra     dprintf_loop

dprintf_long:
    move.l  (%a1)+, %d0
    jsr     dbg_printLong
    bra     dprintf_loop

dprintf_string:
    movem.l %a0, -(%sp)
    move.l  (%a1)+, -(%sp)
    jsr     dbg_printString
    addq.l  #4, %sp
    movem.l (%sp)+, %a0
    bra     dprintf_loop

dprintf_done:
    movem.l (%sp)+, %d0-%d1/%a0-%a1
    rts

/*==============================================================
 * dbg_printString — print zero-terminated string
 *==============================================================*/
dbg_printString:
    move.l 4(%sp), %a0
dbg_printStringA0:
    move.b (%a0)+, %d0
    cmp.b #0, %d0
    beq _dbg_zt
    jsr dbg_printChar
    bra dbg_printStringA0
_dbg_zt:
    rts

/*==============================================================
 * Hex print routines
 *==============================================================*/
dbg_printLong:
    swap     %d0
    jsr dbg_printWord
    swap %d0
dbg_printWord:
    rol.w   #8, %d0
    bsr     dbg_printByte
    rol.w   #8, %d0
dbg_printByte:
    move.b  %d0, %d1
    lsr.b   #4 ,%d0
    bsr     .LdbgprintHex
    move.b  %d1, %d0
.LdbgprintHex:
    and.b   #0x0F, %d0
    or.b    #0x30, %d0
    cmp.b   #0x39, %d0
    ble     dbg_printChar
    add.b   #0x07, %d0

/*==============================================================
 * dbg_printChar — print one character on debug plane
 *==============================================================*/
dbg_printChar:
    movem.l %d0/%d1/%d2/%d4/%d5, -(%sp)
    and.l #0xFF, %d0
    cmp.b #0x0A, %d0
    beq _dbg_newline

    move.l #vram_addr_plane_write, %d5

    move.l #0, %d4
    move.b (dbg_cursor_y), %d4
    swap %d4
    rol.l #7, %d4
    add.l %d4, %d5

    move.b (dbg_cursor_x), %d4
    swap %d4
    rol.l #1, %d4
    add.l %d4, %d5

    /* High priority + palette 1 */
    add.w #0xA000,%d0
    move.l %d5, (VDP_CONTROL)
    move.w %d0, (VDP_DATA)

    addq.b #1, (dbg_cursor_x)
    move.b (dbg_cursor_x), %d0
    cmp.b #39, %d0
    ble _dbg_skipPrintChar

_dbg_newline:
    addq.b #1, (dbg_cursor_y)
    and.b #0x1F, (dbg_cursor_y)
    jsr dbg_clearLine

    cmp.b #1, (dbg_scrolling)
    beq _dbg_scrollPage

    cmp.b #debug_window_size, (dbg_cursor_y)
    blt _dbg_skipPrintChar
    move.b #1, (dbg_scrolling)

_dbg_scrollPage:
    move.b (dbg_cursor_y),%d5
    move.b %d5,%d0
    sub.b #debug_window_size,%d0
    and.b #0x1F,%d0
    move.b %d0,(dbg_cursor_y)
    jsr dbg_clearLine
    move.b %d5,(dbg_cursor_y)

    move.w (dbg_vscroll_amount), %d5
    add.w #8, %d5
    move.w %d5, (dbg_vscroll_amount)
    move.l #vdp_cmd_vsram_write, (VDP_CONTROL)
    move.w %d5, (VDP_DATA)

_dbg_skipPrintChar:
    movem.l (%sp)+, %d0/%d1/%d2/%d4/%d5
    rts

dbg_newline:
    movem.l %d0/%d1/%d2/%d4/%d5, -(%sp)
    bra _dbg_newline

dbg_clearLine:
    movem.l %d0/%d1/%d4/%d5, -(%sp)
    move.l #0, %d0
    move.b %d0,(dbg_cursor_x)

    move.l #vram_addr_plane_write, %d5
    move.l #0, %d4
    move.b (dbg_cursor_y), %d4
    swap %d4
    rol.l #7, %d4
    add.l %d4, %d5
    move.l %d5, (VDP_CONTROL)

    moveq #19, %d1
_dbg_clearChar:
    move.l %d0, (VDP_DATA)
    dbra %d1, _dbg_clearChar
    movem.l (%sp)+, %d0/%d1/%d4/%d5
    rts

/*==============================================================
 * dbg_registers — dump all 68000 registers
 *==============================================================*/
dbg_registers:
    movem.l %d0-%d7/%a0-%a7,-(%sp)
    move.l %sp,%a0
    move.w #'0',%d5

dbg_showNextReg:
    move.w #'D',%d0
    bsr dbg_printChar
    move.w %d5,%d0
    bsr dbg_printChar
    move.w #':',%d0
    bsr dbg_printChar
    move.l (%a0),%d0
    bsr dbg_printLong
    move.w #' ',%d0
    bsr dbg_printChar
    bsr dbg_printChar
    bsr dbg_printChar

    add #8*4,%a0
    move.w #'A',%d0
    bsr dbg_printChar
    move.w %d5,%d0
    bsr dbg_printChar
    move.w #':',%d0
    bsr dbg_printChar
    move.l (%a0),%d0
    bsr dbg_printLong

    sub #8*4,%a0
    add #4,%a0
    bsr dbg_newline

    add #1,%d5
    cmp #'8',%d5
    bne dbg_showNextReg

    add #8*4,%a0
    move.w #'P',%d0
    bsr dbg_printChar
    move.w #'C',%d0
    bsr dbg_printChar
    move.w #':',%d0
    bsr dbg_printChar
    move.l (%a0),%d0
    bsr dbg_printLong
    bsr dbg_newline

    movem.l (%sp)+,%d0-%d7/%a0-%a7
    rts

/*==============================================================
 * dbg_stack — dump stack contents
 *==============================================================*/
dbg_stack:
    movem.l %d0/%a0,-(%sp)
    lea 12(%sp),%a0
    move.l #4,%d0
    jsr dbg_memory
    movem.l (%sp)+,%d0/%a0
    rts

    .section .data
_dbg_S1:    .asciz "RTE sr:"
_dbg_S2:    .asciz " addr:"
    .align 2
    .section .text

/*==============================================================
 * dbg_rte — show RTE frame (sr + return address)
 *==============================================================*/
dbg_rte:
    movem.l %d0/%d1/%a0,-(%sp)
    lea     _dbg_S1, %a0
    bsr     dbg_printStringA0
    move.w 16(%sp),%d0
    bsr dbg_printWord
    lea     _dbg_S2, %a0
    bsr     dbg_printStringA0
    move.l 18(%sp),%d0
    bsr dbg_printLong
    bsr dbg_newline
    movem.l (%sp)+,%d0/%d1/%a0
    rts

/*==============================================================
 * dbg_memory — hex+ASCII memory dump
 * a0 = address, d0 = number of lines
 *==============================================================*/
dbg_memory:
    movem.l %d0-%d7/%a0-%a7,-(%sp)
    movea.l %a0,%a1
    move.w %d0,%d4
    bsr dbg_MemDumpDraw
    movem.l (%sp)+,%d0-%d7/%a0-%a7
    rts

dbg_MemDumpDraw:
    move.l %a1,%d0
    bsr dbg_printLong
    move.b #':',%d0
    bsr dbg_printChar
    bsr dbg_newline

dbg_MemDump_NextLine:
    move.l #7,%d3
dbg_MemDump_NextByte:
    move.b (%a1)+,%d0
    bsr dbg_printByte
    move.b #' ',%d0
    bsr dbg_printChar
    dbra %d3,dbg_MemDump_NextByte

    subq.l #8,%a1
    move.w #7,%d3
dbg_MemDump_NextChar:
    move.b (%a1)+,%d0
    cmp #32,%d0
    blt dbg_MemDump_BadChar
    cmp #128,%d0
    bgt dbg_MemDump_BadChar
    bra dbg_MemDump_OkChar
dbg_MemDump_BadChar:
    move.b #'.',%d0
dbg_MemDump_OkChar:
    bsr dbg_printChar
    dbra %d3,dbg_MemDump_NextChar
    bsr dbg_newline

    sub.b #1,%d4
    bne dbg_MemDump_NextLine
    rts
