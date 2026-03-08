# Multitasking

## Current State: Single-Tasking

Genix currently runs one user program at a time. `exec()` loads a
binary, transfers control, and blocks until the program exits. The
kernel shell regains control after each program.

## Planned Architecture

Preemptive multitasking with process groups, job control, pipes, and
I/O redirection — the minimum for a usable interactive Unix shell.

### Process States

```
           vfork()           exec()
P_FREE ──────────→ P_READY ────────→ P_RUNNING
                     ↑                    │
                     │ schedule()         │ sleep/block
                     │                    ↓
                     └──────────── P_SLEEPING
                                        │
                     P_ZOMBIE ←─────────┘ exit()
                                        │
                     P_STOPPED ←────────┘ SIGTSTP (^Z)
                         │
                         └──→ P_READY     SIGCONT (fg)
```

### Process Table (Planned)

```c
struct proc {
    uint8_t  state;         // P_FREE, P_RUNNING, P_READY, P_SLEEPING,
                            // P_ZOMBIE, P_STOPPED, P_VFORK
    uint8_t  pid;
    uint8_t  ppid;
    int8_t   exitcode;

    // Saved CPU state
    uint32_t sp, pc;
    uint32_t regs[16];      // d0-d7, a0-a7
    uint16_t sr;

    // Memory
    uint32_t mem_base;      // start of process memory
    uint32_t mem_size;      // allocated size
    uint32_t brk;           // current break

    // Job control
    uint8_t  pgrp;          // process group ID
    uint8_t  session;       // session ID
    uint8_t  ctty;          // controlling terminal minor

    // Signals
    uint16_t sig_pending;   // bitmask of pending signals
    uint16_t sig_blocked;   // bitmask of blocked signals
    uint32_t sig_handler[16]; // SIG_DFL=0, SIG_IGN=1, or function pointer

    // Filesystem
    uint16_t cwd;
    struct ofile *fd[MAXFD];
};
```

## Scheduler

Timer-driven preemptive round-robin. The VBlank interrupt (50/60 Hz
on Mega Drive) or the workbench timer interrupt (100 Hz) fires, and
the scheduler picks the next READY process.

```c
void schedule(void) {
    struct proc *next = NULL;
    for (int i = 1; i <= MAXPROC; i++) {
        struct proc *p = &proctab[(curproc->pid + i) % MAXPROC];
        if (p->state == P_READY) {
            next = p;
            break;
        }
    }
    if (next == NULL) return;  // only one runnable process
    context_switch(curproc, next);
}
```

### Context Switch (~30 lines of assembly)

```asm
context_switch:
    movem.l d0-d7/a0-a6, -(sp)    ; save 15 registers (one instruction!)
    move    usp, a0                 ; save user SP
    move.l  a0, proc_sp(curproc)
    move.l  sp, proc_ksp(curproc)   ; save kernel SP
    ; ... switch curproc ...
    move.l  proc_ksp(next), sp      ; restore kernel SP
    move.l  proc_sp(next), a0
    move    a0, usp                 ; restore user SP
    movem.l (sp)+, d0-d7/a0-a6     ; restore 15 registers
    rte
```

`MOVEM.L` saves/restores 15 registers in a single instruction — the
fastest possible approach on the 68000.

## vfork() + exec()

No `fork()` — the 68000 has no MMU, so there's no cheap way to
copy-on-write. Instead, `vfork()` creates a child that shares the
parent's address space:

1. Parent's state is saved and set to `P_VFORK` (blocked)
2. Child gets a new process slot, shares parent's memory
3. Child runs on parent's stack (parent is frozen)
4. Child must immediately call `exec()` or `_exit()`
5. `exec()` allocates fresh memory for the child, loads the binary
6. Parent wakes up when child calls `exec()` or `_exit()`

The libc `vfork()` stub saves the return address in a register (not on
the stack, since the child will use the same stack):

```asm
_vfork:
    move.l  (%sp)+, %a1     ; save return address in a1
    moveq   #190, %d0       ; SYS_VFORK
    trap    #0
    move.l  %a1, -(%sp)     ; restore return address
    rts
```

This is the same approach used by uClinux and Fuzix on no-MMU systems.

## Pipes

```c
#define PIPE_SIZE 512

struct pipe {
    uint8_t  buf[PIPE_SIZE];
    uint16_t read_pos, write_pos, count;
    uint8_t  readers, writers;
};
```

- `pipe()` creates two file descriptors sharing a kernel `struct pipe`
- Writer sleeps if pipe is full; reader sleeps if empty
- Reader gets 0 (EOF) when no writers remain
- Writer gets `SIGPIPE` when no readers remain
- ~80 lines of kernel C

## Signals

Minimum set for job control:

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

### Signal Delivery on 68000

Build a trampoline frame on the user stack:

```c
void deliver_signal(struct proc *p, int sig) {
    uint32_t handler = p->sig_handler[sig];
    if (handler == SIG_DFL) { default_action(p, sig); return; }
    if (handler == SIG_IGN) return;

    // Push trampoline on user stack:
    // [sigreturn code] [signal number] [return addr to trampoline]
    // Set PC = handler, SP = adjusted
    // When handler returns, it hits the trampoline which calls sigreturn()
}
```

The trampoline is 4 bytes of 68000 machine code written onto the user
stack: `moveq #SYS_SIGRETURN, d0; trap #0`. This restores the original
register state.

~150 lines total for signal dispatch + sigreturn.

## Implementation Phases

### Phase 2a: Binary Loading + Single-Tasking exec() -- DONE

### Phase 2b: Multitasking

- Preemptive scheduler (~50 lines)
- vfork() (~80 lines)
- waitpid() (~40 lines)
- exit() cleanup (~30 lines)

### Phase 2c: Pipes and Redirection

- pipe() (~80 lines)
- dup2() — already implemented
- Shell pipes and redirection

### Phase 2d: Signals and Job Control

- Signal delivery (~150 lines)
- SIGINT/SIGQUIT/SIGTSTP from tty
- SIGCHLD, SIGCONT, SIGPIPE
- Process groups (~50 lines)
- Shell: fg, bg, jobs commands

## Memory Management for Multitasking

Each process gets a contiguous block from `kmalloc()`:

```
Process memory layout:
┌──────────────────┐ ← mem_base + mem_size
│  Stack (grows ↓) │
├──────────────────┤ ← initial SP
│  BSS (zeroed)    │
├──────────────────┤
│  Data segment    │
├──────────────────┤
│  Text segment    │
└──────────────────┘ ← mem_base (entry point after relocation)
```

On the Mega Drive with 64 KB main RAM, ~24 KB is available for user
processes after the kernel. With 8 KB per process, that's ~3 concurrent
processes. Using cartridge SRAM (512 KB) extends this significantly.

`brk()` only works reliably for the last-loaded process (no way to
grow into another process's memory). This is a known limitation of
no-MMU systems.
