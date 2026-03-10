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

/*
 * void swtch(uint32_t *old_ksp, uint32_t new_ksp)
 *
 * Kernel-level context switch. Saves callee-saved registers and SP
 * to *old_ksp, loads new_ksp, restores callee-saved registers, and
 * returns (RTS) to wherever the new stack's return address points.
 *
 * For a previously-yielded process: RTS returns into schedule(),
 * which returns to whatever kernel code called it.
 *
 * For a brand-new process: RTS goes to proc_first_run, which pops
 * the user-mode state frame and does RTE to enter user mode.
 */
    .globl swtch
swtch:
    /* Save callee-saved registers on current stack */
    movem.l %d2-%d7/%a2-%a6, -(%sp)    /* 11 regs, 44 bytes */

    /* Save current SP to *old_ksp.
     * Args shifted by 44 bytes of saved regs:
     *   SP+44: return address
     *   SP+48: old_ksp pointer
     *   SP+52: new_ksp */
    move.l  48(%sp), %a0        /* &old->ksp */
    move.l  %sp, (%a0)          /* save current SP */

    /* Load new stack pointer */
    move.l  52(%sp), %sp        /* switch stacks */

    /* Restore callee-saved registers from new stack */
    movem.l (%sp)+, %d2-%d7/%a2-%a6
    rts

/*
 * proc_first_run — trampoline for a process's first context switch.
 *
 * When swtch() resumes a brand-new process, RTS lands here.
 * The kstack below has a pre-built user-mode state frame:
 *   SP+0:  USP (4 bytes)
 *   SP+4:  d0-d7, a0-a6 (60 bytes, all zero)
 *   SP+64: SR (2 bytes, 0x0000 = user mode)
 *   SP+66: PC (4 bytes, entry point)
 *
 * This is the same layout the timer ISR's restore path expects.
 */
    .globl proc_first_run
proc_first_run:
    move.l  (%sp)+, %a0         /* pop saved USP */
    move    %a0, %usp           /* set user stack pointer */
    movem.l (%sp)+, %d0-%d7/%a0-%a6  /* restore user registers */
    rte                          /* pop SR+PC, enter user mode */

    .bss
    .align  4
saved_ksp:
    .space  4
