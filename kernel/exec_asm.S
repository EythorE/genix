/*
 * exec_asm.S — Assembly support for exec and vfork
 *
 * exec_enter/exec_leave: save/restore kernel context for running user programs
 * vfork_save/vfork_restore: setjmp/longjmp for vfork parent/child
 *
 * User mode architecture:
 *   User programs run in user mode (S=0 in SR). The 68000 has two stack
 *   pointers: USP (user) and SSP (supervisor). TRAP #0 from user mode
 *   automatically switches to SSP and pushes SR+PC there. Each process
 *   has a per-process kernel stack; SSP is set to its top before entering
 *   user mode.
 */

    .text

/*
 * int exec_enter(uint32_t entry_point, uint32_t user_sp, uint32_t kstack_top)
 *
 * Saves callee-saved registers and kernel SP on the main supervisor stack.
 * Switches SSP to kstack_top (the process's kernel stack).
 * Sets USP to user_sp. Enters user mode via RTE.
 *
 * Returns when exec_leave() is called (from do_exit via TRAP #0).
 * Return value in d0 = exit code.
 */
    .globl exec_enter
exec_enter:
    /* Save callee-saved registers (d2-d7, a2-a6 = 11 regs) */
    movem.l %d2-%d7/%a2-%a6, -(%sp)

    /* Save kernel stack pointer (main supervisor stack) */
    move.l  %sp, saved_ksp

    /* Grab arguments before we switch stacks.
     * Stack layout after saves:
     *   SP+0:  saved d2-d7/a2-a6 (44 bytes)
     *   SP+44: return address      (4 bytes)
     *   SP+48: entry_point         (4 bytes)
     *   SP+52: user_sp             (4 bytes)
     *   SP+56: kstack_top          (4 bytes) */
    move.l  48(%sp), %a0        /* entry_point → a0 */
    move.l  52(%sp), %a1        /* user_sp → a1 */
    move.l  56(%sp), %d1        /* kstack_top → d1 (temp) */

    /* Set user stack pointer (USP).
     * USP is only accessible from supervisor mode. */
    move    %a1, %usp

    /* Switch SSP to the process's kernel stack top */
    move.l  %d1, %sp

    /* Build RTE frame on the kernel stack to enter user mode.
     * 68000 exception frame: SP+0 = SR (16-bit), SP+2 = PC (32-bit).
     * SR = 0x0000: S=0 (user mode), IPL=0 (all interrupts enabled). */
    move.l  %a0, -(%sp)         /* push PC (entry_point) */
    move.w  #0x0000, -(%sp)     /* push SR (user mode, interrupts on) */

    /* Enter user mode! RTE pops SR (clears S bit) and PC.
     * After this: A7 = USP (user stack), SSP = kstack top. */
    rte

/*
 * void exec_leave(void)  __attribute__((noreturn))
 *
 * Called from do_exit() to return control to the caller of exec_enter.
 * Runs in supervisor mode (inside TRAP #0 handler). Abandons the
 * process's kernel stack and returns to the main supervisor stack.
 */
    .globl exec_leave
exec_leave:
    /* Load exit code from global */
    move.l  exec_exit_code, %d0

    /* Restore main supervisor stack and callee-saved registers */
    move.l  saved_ksp, %sp
    movem.l (%sp)+, %d2-%d7/%a2-%a6
    rts

/*
 * int vfork_save(uint32_t *regs)
 *
 * Saves callee-saved registers (d2-d7, a2-a6), stack pointer,
 * and return address into regs[]. Returns 0.
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
