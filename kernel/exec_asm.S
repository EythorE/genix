/*
 * exec_asm.S — Assembly support for single-tasking exec
 *
 * exec_enter: save kernel context, switch to user stack, call entry point
 * exec_leave: restore kernel context, return to exec_enter's caller
 */

    .text

/*
 * int exec_enter(uint32_t entry_point, uint32_t user_sp)
 *
 * Saves callee-saved registers and kernel SP.
 * Switches to user_sp and calls entry_point via JSR.
 * Returns when exec_leave() is called (or if the entry point returns).
 * Return value in d0 = exit code.
 */
    .globl exec_enter
exec_enter:
    /* Save callee-saved registers (d2-d7, a2-a6 = 11 regs) */
    movem.l %d2-%d7/%a2-%a6, -(%sp)

    /* Save kernel stack pointer */
    move.l  #saved_ksp, %a0
    move.l  %sp, (%a0)

    /* Grab arguments before we lose the stack.
     * Stack layout after saves:
     *   SP+0:  saved d2-d7/a2-a6 (44 bytes)
     *   SP+44: return address      (4 bytes)
     *   SP+48: entry_point          (4 bytes)
     *   SP+52: user_sp              (4 bytes) */
    move.l  48(%sp), %a0        /* entry_point */
    move.l  52(%sp), %sp        /* switch to user stack */

    /* Call the user program */
    jsr     (%a0)

    /* If the user program returns (crt0 should call _exit, but handle it) */
    moveq   #0, %d0
    bra     .Lexec_done

/*
 * void exec_leave(void)  __attribute__((noreturn))
 *
 * Called from do_exit() to return control to the caller of exec_enter.
 * The exit code is read from exec_exit_code (global, set by do_exit).
 */
    .globl exec_leave
exec_leave:
    /* Load exit code from global */
    move.l  exec_exit_code, %d0

.Lexec_done:
    /* Restore kernel SP and callee-saved registers */
    move.l  saved_ksp, %a0
    move.l  %a0, %sp
    movem.l (%sp)+, %d2-%d7/%a2-%a6
    rts

    .bss
    .align  4
saved_ksp:
    .space  4
