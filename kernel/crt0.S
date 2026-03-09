/*
 * Genix kernel startup for 68000
 *
 * Vector table + early init. Works on both the workbench emulator
 * and the Mega Drive (the PAL layer handles HW differences).
 */

    .section .vectors, "a"
    .globl _vectors

_vectors:
    .long   _stack_top          /* 0: Initial SSP */
    .long   _start              /* 1: Initial PC */
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
    .long   _vec_autovec1       /* 25: Level 1 autovector */
    .long   _vec_autovec2       /* 26: Level 2 autovector */
    .long   _vec_autovec3       /* 27: Level 3 autovector */
    .long   _vec_autovec4       /* 28: Level 4 autovector */
    .long   _vec_autovec5       /* 29: Level 5 autovector */
    .long   _vec_timer_irq      /* 30: Level 6 autovector (timer) */
    .long   _vec_autovec7       /* 31: Level 7 autovector (NMI) */

    /* TRAP vectors */
    .long   _vec_syscall        /* 32: TRAP #0 — syscall entry */
    .long   _vec_trap1          /* 33: TRAP #1 */
    .rept   14
    .long   _vec_reserved       /* 34-47: TRAP #2-#15 */
    .endr

    /* Fill to 256 vectors */
    .rept   208
    .long   _vec_reserved
    .endr

/* ============================================================ */
    .section .text
    .globl _start

_start:
    /* Disable interrupts during init */
    move.w  #0x2700, %sr

    /* Clear BSS */
    lea     _bss_start, %a0
    lea     _bss_end, %a1
    bra     2f
1:  clr.l   (%a0)+
2:  cmp.l   %a1, %a0
    blt     1b

    /* Jump to C kernel main */
    jsr     kmain

    /* If kmain returns, halt */
_halt:
    stop    #0x2700
    bra     _halt

/* ============================================================ */
/* Exception/Interrupt handlers                                  */
/* ============================================================ */

_vec_buserr:
_vec_addrerr:
    /* Save regs, print message, halt */
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
_vec_autovec4:
_vec_autovec5:
_vec_autovec7:
_vec_trap1:
    rte

/* Timer interrupt (Level 6)
 *
 * If we interrupted user mode (S=0 in saved SR), this is a preemption
 * point: save full user state, call schedule, and possibly switch to
 * a different process by swapping kernel stacks.
 *
 * If we interrupted supervisor mode (S=1), don't preempt — the kernel
 * might be in the middle of a syscall with state on the kstack. Just
 * advance the timer and return.
 *
 * 68000 exception frame on stack: SP+0 = SR (16-bit), SP+2 = PC (32-bit).
 * SR bit 13 = S (supervisor bit). In the byte at SP+0, S is bit 5. */
    .globl _vec_timer_irq
_vec_timer_irq:
    btst    #5, (%sp)           /* test S bit in saved SR */
    bne     .Ltimer_super       /* S=1: supervisor mode, no preemption */

    /* Interrupted user mode — save full context for possible switch */
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    move    %usp, %a0
    move.l  %a0, -(%sp)

    jsr     timer_interrupt

    /* Save current ksp to curproc->ksp (offset 4 in struct proc) */
    move.l  curproc, %a0
    move.l  %sp, 4(%a0)

    jsr     schedule

    /* Load (possibly new) curproc->ksp */
    move.l  curproc, %a0
    move.l  4(%a0), %sp

    /* Restore user state */
    move.l  (%sp)+, %a0
    move    %a0, %usp
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rte

.Ltimer_super:
    /* Interrupted supervisor mode — minimal save, no preemption */
    movem.l %d0-%d1/%a0-%a1, -(%sp)
    jsr     timer_interrupt
    movem.l (%sp)+, %d0-%d1/%a0-%a1
    rte

/* Syscall entry (TRAP #0)
 *
 * When TRAP #0 fires from user mode:
 *   - 68000 switches to supervisor mode (S=1)
 *   - A7 becomes SSP (process's kernel stack, set by exec_enter)
 *   - SR and PC are pushed onto SSP automatically
 *
 * We save all user registers + USP on the kernel stack, dispatch
 * the syscall, then restore and RTE back to user mode. */
    .globl _vec_syscall
_vec_syscall:
    /* Save all user registers on the kernel stack.
     * d0 has the syscall number, d1-d4 have args. */
    movem.l %d0-%d7/%a0-%a6, -(%sp)   /* 15 regs, 60 bytes */

    /* Save user stack pointer */
    move    %usp, %a0
    move.l  %a0, -(%sp)               /* push USP, 4 bytes */

    /* Stack layout from SP:
     *   SP+0:  USP                 (4 bytes)
     *   SP+4:  d0 (syscall#)       (4 bytes)
     *   SP+8:  d1 (arg1)           (4 bytes)
     *   SP+12: d2 (arg2)           (4 bytes)
     *   SP+16: d3 (arg3)           (4 bytes)
     *   SP+20: d4 (arg4)           (4 bytes)
     *   ...
     *   SP+60: a6                  (4 bytes)
     *   SP+64: SR                  (2 bytes)
     *   SP+66: PC                  (4 bytes) */

    /* Push args for C: syscall_dispatch(num, a1, a2, a3, a4) */
    move.l  20(%sp), -(%sp)     /* d4 = arg4 */
    move.l  20(%sp), -(%sp)     /* d3 = arg3 (shifted by push above) */
    move.l  20(%sp), -(%sp)     /* d2 = arg2 */
    move.l  20(%sp), -(%sp)     /* d1 = arg1 */
    move.l  20(%sp), -(%sp)     /* d0 = syscall# */
    jsr     syscall_dispatch
    add.l   #20, %sp            /* pop 5 args */

    /* Store return value in saved d0 slot so user sees it after RTE */
    move.l  %d0, 4(%sp)

    /* Restore user stack pointer */
    move.l  (%sp)+, %a0
    move    %a0, %usp

    /* Restore all user registers (d0 now has syscall return value) */
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rte
