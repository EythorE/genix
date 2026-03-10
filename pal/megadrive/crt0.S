/*
 * Mega Drive boot code and vector table
 *
 * Based on FUZIX Mega Drive crt0.S — proven to work on blastem.
 * Handles TMSS, VDP init, then jumps to generic kernel startup.
 */

    .section .vectors, "a"
    .globl _vectors

_vectors:
    .long   0x00000000          /* 0: Initial SSP (set by ROM header) */
    .long   _md_start           /* 1: Initial PC */
    .long   _vec_buserr         /* 2: Bus error */
    .long   _vec_addrerr        /* 3: Address error */
    .long   _vec_illegal        /* 4: Illegal instruction */
    .long   _vec_divzero        /* 5: Zero divide */
    .long   _vec_chk            /* 6: CHK */
    .long   _vec_trapv          /* 7: TRAPV */
    .long   _vec_priv           /* 8: Privilege violation */
    .long   _vec_trace          /* 9: Trace */
    .long   _vec_unimpa         /* 10: Line 1010 */
    .long   _vec_unimpf         /* 11: Line 1111 */
    .rept   3
    .long   _vec_reserved       /* 12-14: Reserved */
    .endr
    .long   _vec_uninit         /* 15: Uninitialized interrupt */
    .rept   8
    .long   _vec_reserved       /* 16-23: Reserved */
    .endr
    .long   _vec_spurious       /* 24: Spurious interrupt */
    .long   _vec_autovec1       /* 25: Level 1 (external) */
    .long   _vec_autovec2       /* 26: Level 2 (external) */
    .long   _vec_autovec3       /* 27: Level 3 (external) */
    .long   _vec_hblank         /* 28: Level 4 (H-Blank) */
    .long   _vec_autovec5       /* 29: Level 5 (external) */
    .long   _vec_vblank         /* 30: Level 6 (V-Blank) */
    .long   _vec_autovec7       /* 31: Level 7 (NMI) */

    /* TRAP vectors */
    .long   _vec_syscall        /* 32: TRAP #0 — syscall */
    .long   _vec_trap1          /* 33: TRAP #1 */
    .rept   14
    .long   _vec_reserved       /* 34-47: TRAP #2-#15 */
    .endr

    /* Fill to vector 64 */
    .rept   16
    .long   _vec_reserved
    .endr

/* ============================================================ */
/* Sega Mega Drive ROM header (0x100-0x1FF)                     */
/* ============================================================ */

    .section .header, "a"

    .ascii  "SEGA MEGA DRIVE "          /* Console name (16 bytes) */
    .ascii  "(C)2025 GENIX   "          /* Copyright (16 bytes) */
    .ascii  "GENIX OS                                        "  /* Domestic name (48) */
    .ascii  "GENIX OS                                        "  /* Overseas name (48) */
    .ascii  "GM 00000000-00"            /* Serial number (14) */
    .word   0x0000                      /* Checksum */
    .ascii  "J               "          /* I/O support (16) */
    .long   0x00000000                  /* ROM start */
    .long   _rom_end                    /* ROM end */
    .long   0x00FF0000                  /* RAM start */
    .long   0x00FFFFFF                  /* RAM end */
    /* SRAM info */
    .ascii  "RA"                        /* SRAM magic */
    .word   0xF820                      /* SRAM type: backup, odd bytes */
    .long   0x00200000                  /* SRAM start */
    .long   0x0020FFFF                  /* SRAM end */
    .ascii  "            "              /* Modem (12) */
    .ascii  "                                        "  /* Memo (40) */
    .ascii  "JUE             "          /* Region (16) */

/* ============================================================ */
/* Entry point                                                   */
/* ============================================================ */

    .section .text
    .globl _md_start

_md_start:
    /* Disable interrupts */
    move.w  #0x2700, %sr

    /* TMSS handshake (required for Model 1+) */
    move.b  0xA10001, %d0       /* Hardware version */
    andi.b  #0x0F, %d0
    beq     1f                  /* Skip for Model 1 */
    move.l  #0x53454741, 0xA14000  /* Write "SEGA" */
1:

    /* Set up stack at top of 68000 RAM */
    move.l  #0x01000000, %sp    /* 0xFFFFFF + 1 wraps to top */

    /* Copy .data from ROM to RAM if needed */
    lea     _data_load, %a0
    lea     _data_start, %a1
    lea     _data_end, %a2
    bra     3f
2:  move.l  (%a0)+, (%a1)+
3:  cmp.l   %a2, %a1
    blt     2b

    /* Clear BSS */
    lea     _bss_start, %a0
    lea     _bss_end, %a1
    bra     5f
4:  clr.l   (%a0)+
5:  cmp.l   %a1, %a0
    blt     4b

    /* Jump to kernel main */
    jsr     kmain

    /* Halt */
6:  stop    #0x2700
    bra     6b

/* ============================================================ */
/* Interrupt handlers                                            */
/* ============================================================ */

_vec_buserr:
_vec_addrerr:
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    move.l  %sp, -(%sp)
    jsr     panic_exception
    /* never returns */

_vec_illegal:
_vec_divzero:
_vec_chk:
_vec_trapv:
_vec_priv:
_vec_trace:
_vec_unimpa:
_vec_unimpf:
_vec_uninit:
_vec_spurious:
_vec_reserved:
_vec_autovec1:
_vec_autovec2:
_vec_autovec3:
_vec_autovec5:
_vec_autovec7:
_vec_trap1:
    rte

/* H-Blank interrupt */
_vec_hblank:
    rte

/* V-Blank interrupt — drives the system timer and preemption.
 *
 * If we interrupted user mode (S=0), save full state for possible
 * context switch. If supervisor mode (S=1), minimal save only.
 * See kernel/crt0.S for detailed comments on the preemption design.
 *
 * Context switching is done via swtch() inside schedule(). */
    .globl _vec_vblank
_vec_vblank:
    btst    #5, (%sp)           /* test S bit in saved SR */
    bne     .Lvbl_super         /* S=1: supervisor mode, no preemption */

    /* Interrupted user mode */
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    move    %usp, %a0
    move.l  %a0, -(%sp)

    jsr     md_vblank_handler
    jsr     timer_interrupt
    jsr     schedule            /* may swtch to different process */
    jsr     sig_deliver         /* deliver pending signals */

    move.l  (%sp)+, %a0
    move    %a0, %usp
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rte

.Lvbl_super:
    movem.l %d0-%d1/%a0-%a1, -(%sp)
    jsr     md_vblank_handler
    jsr     timer_interrupt
    movem.l (%sp)+, %d0-%d1/%a0-%a1
    rte

/* Syscall entry (TRAP #0)
 *
 * TRAP from user mode: 68000 switches to supervisor mode, A7=SSP
 * (process kernel stack), SR+PC pushed on SSP automatically. */
    .globl _vec_syscall
_vec_syscall:
    /* Save all user registers on the kernel stack */
    movem.l %d0-%d7/%a0-%a6, -(%sp)   /* 15 regs, 60 bytes */

    /* Save user stack pointer */
    move    %usp, %a0
    move.l  %a0, -(%sp)               /* push USP, 4 bytes */

    /* Push args for C: syscall_dispatch(num, a1, a2, a3, a4)
     * Stack: SP+0=USP, SP+4=d0, SP+8=d1, SP+12=d2, SP+16=d3, SP+20=d4 */
    move.l  20(%sp), -(%sp)     /* d4 = arg4 */
    move.l  20(%sp), -(%sp)     /* d3 = arg3 */
    move.l  20(%sp), -(%sp)     /* d2 = arg2 */
    move.l  20(%sp), -(%sp)     /* d1 = arg1 */
    move.l  20(%sp), -(%sp)     /* d0 = syscall# */
    jsr     syscall_dispatch
    add.l   #20, %sp            /* pop 5 args */

    /* Store return value in saved d0 slot */
    move.l  %d0, 4(%sp)

    /* Deliver pending signals before returning to user mode */
    jsr     sig_deliver

    /* Restore user stack pointer */
    move.l  (%sp)+, %a0
    move    %a0, %usp

    /* Restore all user registers */
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rte
