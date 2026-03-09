/*
 * exec_asm.S — Assembly support for exec and vfork
 *
 * exec_enter/exec_leave: save/restore kernel context for running user programs
 * vfork_save/vfork_restore: setjmp/longjmp for vfork parent/child
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

    /* Jump to the user program — do NOT use JSR here.
     * JSR would push a return address onto the user stack,
     * corrupting the argc/argv layout that crt0 expects.
     * crt0 calls _exit() via TRAP #0, which reaches exec_leave()
     * to return control to our caller. */
    jmp     (%a0)

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

/*
 * int vfork_save(uint32_t *regs)
 *
 * Saves callee-saved registers (d2-d7, a2-a6), stack pointer,
 * and return address into regs[]. Returns 0.
 *
 * regs layout (13 longs = 52 bytes):
 *   [0..5]   d2-d7         (6 regs, 24 bytes)
 *   [6..10]  a2-a6         (5 regs, 20 bytes)
 *   [11]     sp            (4 bytes)
 *   [12]     return addr   (4 bytes)
 */
    .globl vfork_save
vfork_save:
    move.l  4(%sp), %a0         /* regs pointer */
    movem.l %d2-%d7/%a2-%a6, (%a0) /* save 11 callee-saved regs */
    move.l  %sp, 44(%a0)        /* save stack pointer */
    move.l  (%sp), 48(%a0)      /* save return address */
    moveq   #0, %d0
    rts

/*
 * void vfork_restore(uint32_t *regs, int retval) __attribute__((noreturn))
 *
 * Restores context saved by vfork_save() and "returns" retval
 * from the original vfork_save() call site. Like longjmp.
 */
    .globl vfork_restore
vfork_restore:
    move.l  4(%sp), %a0         /* regs pointer */
    move.l  8(%sp), %d0         /* retval */
    movem.l (%a0), %d2-%d7/%a2-%a6 /* restore 11 callee-saved regs */
    move.l  44(%a0), %sp        /* restore stack pointer */
    move.l  48(%a0), (%sp)      /* restore return address on stack */
    rts                          /* "returns" to vfork_save's caller */

    .bss
    .align  4
saved_ksp:
    .space  4
