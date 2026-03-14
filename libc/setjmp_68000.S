/*
 * setjmp_68000.S — setjmp/longjmp for Motorola 68000
 *
 * Ported from FUZIX Library/libs/setjmp_68000.S.
 *
 * jmp_buf layout (12 longs = 48 bytes):
 *   [0..10]  d2-d7, a2-a6  (11 callee-saved registers)
 *   [11]     SP (stack pointer at setjmp call site)
 *
 * The return address stays on the stack. longjmp restores SP
 * which puts the original return address back on top.
 */

    .text

/*
 * int setjmp(jmp_buf env)
 *   env pointer in 4(%sp) — first argument
 *   Save callee-saved regs + SP. Return 0.
 */
    .globl setjmp
setjmp:
    move.l  4(%sp), %a0         /* a0 = env */
    movem.l %d2-%d7/%a2-%a6, (%a0)  /* save 11 regs (44 bytes) */
    move.l  %sp, 44(%a0)        /* save SP at env[11] */
    moveq   #0, %d0             /* return 0 */
    rts

/*
 * void longjmp(jmp_buf env, int val)
 *   env in 4(%sp), val in 8(%sp)
 *   Restore regs + SP. If val==0, set to 1 (POSIX).
 *   RTS returns to the original setjmp call site.
 */
    .globl longjmp
longjmp:
    move.l  4(%sp), %a0         /* a0 = env */
    move.l  8(%sp), %d0         /* d0 = val */
    tst.l   %d0
    bne     1f
    moveq   #1, %d0             /* if val==0, return 1 */
1:
    movem.l (%a0), %d2-%d7/%a2-%a6  /* restore 11 regs */
    move.l  44(%a0), %sp        /* restore SP */
    rts                          /* return to setjmp call site */
