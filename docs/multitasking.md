# Multitasking

## Current State: Preemptive Multitasking

Genix implements preemptive multitasking with process groups, job
control, pipes, signals, and I/O redirection. The scheduler is
timer-driven round-robin, switching processes on VBlank (50/60 Hz on
Mega Drive) or the workbench timer interrupt (100 Hz).

### Process States

```
           do_spawn()        schedule()
P_FREE ──────────→ P_READY ←──────────→ P_RUNNING
                     ↑                      │
                     │ wakeup()             │ sleep (pipe/tty/waitpid)
                     │                      ↓
                     └──────────── P_SLEEPING
                                       │
                     P_ZOMBIE ←────────┘ do_exit()
                                       │
                     P_STOPPED ←───────┘ SIGTSTP / SIGSTOP
                         │
                         └──→ P_READY     SIGCONT

                     P_VFORK               parent blocked during vfork
```

### Process Table

From `kernel/kernel.h`:

```c
#define MAXPROC     16
#define KSTACK_SIZE 512
#define KSTACK_WORDS (KSTACK_SIZE / 4)
#define NSIG        21

struct proc {
    uint8_t  state;         /* P_FREE..P_STOPPED */
    uint8_t  pid;
    uint8_t  ppid;
    int8_t   exitcode;
    uint32_t ksp;           /* saved kernel stack pointer (into kstack[]) */
    uint32_t mem_base;      /* start of process memory */
    uint32_t mem_size;      /* size of allocated memory */
    uint32_t brk;           /* current break (top of data) */
    uint32_t vfork_ctx[13]; /* vfork_save context (d2-d7, a2-a6, sp, retaddr) */
    uint16_t cwd;           /* current working directory inode */
    uint8_t  pgrp;          /* process group ID (for job control) */
    uint8_t  _pad;          /* align to even boundary */
    uint32_t sig_pending;   /* bitmask of pending signals */
    uint32_t sig_handler[NSIG]; /* SIG_DFL=0, SIG_IGN=1, or function pointer */
    struct ofile *fd[MAXFD];
    uint32_t kstack[KSTACK_WORDS]; /* per-process kernel stack */
};
```

Each process has its own 512-byte kernel stack embedded in the proc
struct. 16 process slots total, consuming ~8 KB of kernel BSS for
kstacks alone.

## Scheduler

Timer-driven preemptive round-robin. The timer ISR (`_vec_timer_irq`
in `kernel/crt0.S`) checks if user mode was interrupted, and if so
calls `schedule()`.

```c
void schedule(void)
{
    /* Round-robin: scan from curproc+1, wrapping */
    for (int i = 1; i <= MAXPROC; i++) {
        struct proc *p = &proctab[(curproc->pid + i) % MAXPROC];
        if (p->state == P_READY) {
            /* Found next runnable process */
            old->state = P_READY;  /* (if was P_RUNNING) */
            p->state = P_RUNNING;
            curproc = p;
            swtch(&old->ksp, p->ksp);
            return;
        }
    }
    /* No other runnable process — continue current */
}
```

### Context Switch: swtch()

The context switch is a callee-saved register swap, not a full 15-register
save. From `kernel/exec_asm.S`:

```asm
swtch:
    movem.l %d2-%d7/%a2-%a6, -(%sp)    /* save 11 callee-saved regs */
    move.l  %sp, (%a0)                  /* *old_ksp = sp */
    move.l  %d0, %sp                    /* sp = new_ksp */
    movem.l (%sp)+, %d2-%d7/%a2-%a6    /* restore 11 regs */
    rts                                  /* return into new process */
```

This saves only the callee-saved registers (d2-d7, a2-a6 = 11
registers, 44 bytes). The caller-saved registers (d0-d1, a0-a1) are
already saved by the C calling convention. The full user-mode state
(all 15 registers + SR + USP) is saved/restored by the timer ISR
frame, not by swtch.

### First Run: proc_first_run

Brand-new processes have their kstack set up by `proc_setup_kstack()`
with a fake frame. When `swtch()` resumes them, RTS lands at
`proc_first_run`, which:

1. Pops the saved USP and sets it via `MOVE %a0, %usp`
2. Restores 15 user-mode registers via `MOVEM.L`
3. Executes `RTE` to enter user mode at the process's entry point

## Process Creation: do_spawn()

Genix uses `do_spawn()` (asynchronous) rather than the traditional
fork()+exec() or vfork()+exec() for creating new processes:

1. Allocate a free proc slot
2. Call `load_binary()` to load the program at USER_BASE
3. Set up the child's kstack with `proc_setup_kstack()`
4. Mark child P_READY — the scheduler picks it up

`do_spawn_fd()` is the variant that also sets up stdin/stdout/stderr
file descriptors (used for pipeline stages).

`do_vfork()` is also implemented for the vfork()+exec() pattern: the
parent blocks (P_VFORK) while the child shares its address space until
calling exec() or _exit().

## Pipes

```c
#define PIPE_SIZE 256  /* uint8_t indices → free modulo wrap */

struct pipe {
    uint8_t  buf[PIPE_SIZE];
    uint8_t  rpos, wpos;   /* circular buffer indices */
    uint8_t  readers, writers;
    uint8_t  read_waiting;
    uint8_t  write_waiting;
};
```

- `pipe()` creates two file descriptors sharing a kernel `struct pipe`
- Writer sleeps if pipe is full; reader sleeps if empty
- Reader gets 0 (EOF) when no writers remain
- Writer gets `SIGPIPE` when no readers remain
- Blocking uses `P_SLEEPING` + `schedule()` + wakeup

### Sequential Pipeline Execution

Currently, all user programs load at the same USER_BASE address, so
pipelines execute sequentially: the shell runs cmd1, captures output,
then runs cmd2 with that input. The 512-byte pipe buffer limits how
much data can pass between stages. True concurrent pipelines require
Phase 6 (memory partitioning) from [PLAN.md](../PLAN.md).

## Signals

10 signals implemented:

| Signal | # | Default | Use |
|--------|---|---------|-----|
| SIGHUP | 1 | terminate | Terminal hangup |
| SIGINT | 2 | terminate | ^C |
| SIGQUIT | 3 | terminate | ^\ |
| SIGKILL | 9 | terminate (uncatchable) | Force kill |
| SIGPIPE | 13 | terminate | Broken pipe |
| SIGTERM | 15 | terminate | Graceful kill |
| SIGCHLD | 17 | ignore | Child exited/stopped |
| SIGCONT | 18 | continue | Resume stopped process |
| SIGSTOP | 19 | stop (uncatchable) | Force stop |
| SIGTSTP | 20 | stop | ^Z |

### Signal Delivery

Signals are delivered on return to user mode. The timer ISR calls
`signal_deliver_pending()` after `schedule()`, which builds a signal
frame on the user stack:

1. Save original user PC and SR on the user stack
2. Push the signal number as an argument
3. Push a sigreturn trampoline address (points to code on the stack:
   `moveq #SYS_SIGRETURN, %d0; trap #0`)
4. Set user PC to the handler address

When the handler returns (RTS), it hits the trampoline, which calls
`sys_sigreturn()` to restore the original register state.

### Process Groups and Job Control

- Each process has a `pgrp` field
- The TTY tracks the foreground process group
- ^C sends SIGINT to the foreground group
- ^Z sends SIGTSTP to the foreground group
- SIGCONT resumes stopped processes

## Known Limitations

1. **Single user memory space** — all programs load at USER_BASE; two
   processes can't coexist in RAM. Pipelines are sequential. Requires
   ROM XIP + memory partitioning to fix (see [PLAN.md](../PLAN.md)
   Phases 5-6).

2. **512-byte per-process kstack** — no guard page or canary (canary
   is checked in `schedule()` but overflow may corrupt adjacent proc
   struct fields before detection).

3. **No SA_RESTART** — signal delivery interrupts blocking syscalls.
   User programs must retry on -EINTR.

4. **brk() limitations** — only works reliably for the last-loaded
   process in single-tasking mode. Problematic if another process's
   memory follows.

## Future: XIP and Memory Partitioning

See [PLAN.md](../PLAN.md) for the forward plan:

- **Phase 5** links app text into ROM (XIP). Only .data goes to RAM,
  tripling effective user memory.
- **Phase 6** partitions user RAM into fixed slots. Multiple processes
  share ROM text with private data in RAM. True concurrent pipelines
  become possible.
- **Phase 8** uses EverDrive Pro PSRAM banks for per-process text
  isolation, enabling much larger programs.
