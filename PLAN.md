# Genix Development Plan

> **Status (March 2026):** Phase 1 (emulator) and Phase 2a (binary loading,
> single-tasking exec, user programs) are **complete**. Phase 2b (multitasking)
> is next. The Mega Drive build boots in BlastEm and on real hardware.
>
> **Origin:** Originally created in
> [EythorE/FUZIX](https://github.com/EythorE/FUZIX) and copied here as the
> canonical reference. See [docs/decisions.md](docs/decisions.md) for design
> history and reversals.

---

## The Problem

FUZIX is a multi-user Unix clone with 30+ platform ports, deep legacy code, and
abstractions for hardware we don't have (banked memory, floppy disks, RS-232).
The Mega Drive port bolts onto this. The result: a forking bug nobody can find
because the interactions between the 68000 platform code and the generic kernel
are too numerous and too subtle. The codebase is not ours to understand — it's
someone else's decades of work.

We don't need multi-user. We don't need login. We don't need file ownership or
permissions. We don't need 30 platform ports. We need a small, single-user OS
for the 68000 that can run Unix utilities and is simple enough that one person
can read the entire kernel source in an afternoon.

## Goals

1. **Readable** — The entire kernel fits in your head. Every line has a reason.
2. **Single-user** — No UIDs, no permissions, no login. Like DOS, but with a
   proper syscall interface.
3. **POSIX-enough** — Enough POSIX syscalls that standard C programs compile
   and Unix utilities run.
4. **No MMU required** — Designed from the start for flat memory, no virtual
   address translation.
5. **Portable to Mega Drive** — The Mega Drive is the final target, but we
   develop on something simpler first.
6. **Small** — Measured in thousands of lines, not tens of thousands.
7. **Multitasking** — Preemptive scheduling, job control, pipes, redirection.
8. **Maximum app portability** — Choose formats and APIs that give access to
   the largest pool of portable Unix utilities.

## Non-Goals

- Multi-user, file permissions, setuid
- Virtual memory, demand paging, swap
- Network stack (maybe someday, not now)
- POSIX compliance certification
- Supporting any CPU other than 68000 (initially)

---

## Binary Format Decision

### Current: Genix Flat Binary (32-byte header)

We currently use a custom flat binary format for single-tasking exec().
See [docs/binary-format.md](docs/binary-format.md) for the header layout
and loader details. This is a stepping stone — when we add multitasking
and relocation, we'll migrate to Fuzix a.out.

### Planned: Fuzix-style a.out (for multitasking)

We evaluated four binary format options:

| Format | Header | Relocation | Toolchain | App Ecosystem |
|--------|--------|------------|-----------|---------------|
| **Fuzix a.out** | 16 bytes | Kernel-applied relocs | m68k-linux-gnu-gcc + Fuzix link scripts | 143+ Fuzix utilities, proven on 68000 MD |
| **bFLT v4** | 64 bytes | Offset list | m68k-linux-gnu-gcc + elf2flt | uClinux apps (mostly too large for 64KB) |
| **Raw ELF** | ~52+ bytes | Full ELF relocs | Any m68k gcc | Theoretically huge, but loader is complex |
| **Flat binary** | None | None (fixed address) | Any | None — every app must know its load address |

### Decision: Fuzix a.out

**Fuzix's binary format is the right choice.** Here's why:

1. **143+ utilities ready to compile.** Fuzix has `sh`, `ed`, `vi` (levee),
   `grep`, `sed`, `awk`, `sort`, `ls`, `cp`, `mv`, `rm`, `cat`, `make`,
   `cc` (small C), `tar`, `ar`, `dd`, BASIC, BCPL, games, and more. All
   designed for 68000 systems with 64KB-class RAM.

2. **Proven on this exact hardware.** Fuzix ran on the Mega Drive with
   multitasking, job control, pipes, and a working shell. The binary format
   and loader are known good.

3. **Right-sized for the hardware.** The 16-byte header and simple relocation
   scheme waste almost nothing. bFLT's 64-byte header is overkill. ELF is
   absurdly complex for a 64KB system.

4. **The toolchain exists.** `m68k-linux-gnu-gcc` with Fuzix's crt0 and link
   scripts produces these binaries. No need to build or port `elf2flt`.

5. **Relocation is simple.** Fuzix's 68000 binaries use kernel-applied
   relocations. The loader reads a relocation table and adjusts absolute
   addresses by adding the load base. ~30 lines of C.

### What about uClinux/bFLT apps?

uClinux apps are designed for systems with megabytes of RAM and uClibc
(~100KB+ C library). They're too large for the Mega Drive. The few that would
fit can be recompiled against our libc anyway — the binary format doesn't
matter for source-level ports.

### What about "largest possible app library"?

The limiting factor is **RAM, not binary format.** With 64KB main + 512KB
cart RAM, only programs designed for tiny systems will fit. Fuzix's 143+
utilities are the largest collection of Unix programs designed for exactly
this scale. No other ecosystem comes close.

For larger programs (GCC, GDB, dash), cross-compilation is the answer.
They run on the host and produce Genix binaries.

### Binary Header

```c
/* Fuzix a.out header — 16 bytes */
struct exec {
    uint16_t a_magic;     /* 0x80A8 */
    uint8_t  a_cpu;       /* 9 = 68000 */
    uint8_t  a_cpufeat;   /* CPU feature flags */
    uint8_t  a_base;      /* Load address page */
    uint8_t  a_hints;     /* 0x01=graphics, 0x02=debug */
    uint16_t a_text;      /* Text segment size */
    uint16_t a_data;      /* Data segment size */
    uint16_t a_bss;       /* BSS segment size */
    uint8_t  a_entry;     /* Entry point offset */
    uint8_t  a_size;      /* Memory request (0=all available) */
    uint8_t  a_stack;     /* Stack size hint */
    uint8_t  a_zp;        /* Zero/direct page (unused on 68000) */
};
```

Note: Segment sizes in the header are 16-bit. For 68000 32-bit systems,
Fuzix uses an extended format with 32-bit sizes (the "small extensions to
handle the relocation maps" mentioned in Fuzix's README.binfmt). If
needed, we can use a 32-bit variant or encode sizes in 256-byte pages.

### Memory Layout at exec()

```
                    ┌──────────────────┐  ← load_base + total_size
                    │  Stack (grows ↓) │
                    │  (a_stack pages) │
                    ├──────────────────┤  ← initial SP
                    │  BSS (zeroed)    │
                    │  (a_bss bytes)   │
                    ├──────────────────┤
                    │  Data segment    │
                    │  (a_data bytes)  │
                    ├──────────────────┤
                    │  Text segment    │
                    │  (a_text bytes)  │
                    └──────────────────┘  ← load_base (entry point)
```

### Relocation at Load Time

1. Read binary from filesystem
2. Allocate contiguous memory block (text + data + bss + stack)
3. Copy text + data into the block
4. Zero BSS
5. Walk relocation table: for each entry, add `load_base` to the 32-bit
   word at the given offset
6. Set up stack with `argc`, `argv[]`, `envp[]`, string data
7. Set PC = entry point, SP = top of stack, jump to userspace

### Loader Implementation (~100 lines)

```c
int do_exec(const char *path, const char **argv) {
    struct exec hdr;
    /* Read and validate header */
    /* Calculate sizes: text + data + bss + stack */
    /* Allocate memory via kmalloc */
    /* Read text + data segments */
    /* Zero BSS */
    /* Apply relocations */
    /* Set up user stack: argc, argv, envp */
    /* Set process registers: PC, SP */
    /* If vfork child: wake parent */
    /* Switch to user mode */
}
```

---

## C Library Strategy

### Decision: Fuzix libc (ported to our syscall ABI)

| Option | Size | POSIX Coverage | Fits 64KB? | Apps It Enables |
|--------|------|---------------|------------|-----------------|
| **Fuzix libc** | ~5-10KB | Good enough | Yes | 143+ Fuzix utils |
| newlib | ~50-100KB | Excellent | Barely | Broader POSIX apps |
| uClibc-ng | ~100-200KB | Near-complete | No | Full Linux apps |
| Hand-written stubs | ~1KB | Minimal | Yes | Only custom apps |

**Fuzix libc wins** because:
- It provides `stdio` (printf, fprintf, fopen, fgets), `stdlib` (malloc,
  free, atoi, qsort), `string`, `ctype`, `errno`, `signal`, `termios`,
  `dirent`, and `unistd` — everything the 143 Fuzix utilities need.
- It's ~5KB compiled — fits easily alongside apps in 64KB.
- It was designed for systems exactly like ours.
- The Fuzix apps are already written against it.

### Syscall ABI

Keep our existing convention (already in `crt0.S`):

| Register | Purpose |
|----------|---------|
| `d0` | Syscall number (in), return value (out) |
| `d1` | Argument 1 |
| `d2` | Argument 2 |
| `d3` | Argument 3 |
| `d4` | Argument 4 |

Entry via `TRAP #0`. Return `d0` ≥ 0 = success, negative = `-errno`.

Fuzix uses `TRAP #14` and puts some args in address registers (`a0-a2`).
Since we're recompiling all apps from source, the C library handles the
mapping — apps never make raw syscalls. Our convention is cleaner (all data
registers) and already implemented.

### Syscall Numbers

Keep our existing numbers (they match Linux m68k where possible). The C
library's `_read()`, `_write()`, etc. stubs translate function calls to
`TRAP #0` with the right number. Apps don't care about numbers.

### User-mode crt0.S

```asm
; apps/crt0.S — user program entry point
; Called with: SP pointing to argc
    .globl _start
_start:
    move.l  (%sp)+, %d0     ; argc
    move.l  %sp, %a0        ; argv
    ; Calculate envp = argv + (argc+1)*4
    move.l  %d0, %d1
    addq.l  #1, %d1
    lsl.l   #2, %d1
    lea     (%a0,%d1.l), %a1
    ; Call main(argc, argv, envp)
    move.l  %a1, -(%sp)     ; push envp
    move.l  %a0, -(%sp)     ; push argv
    move.l  %d0, -(%sp)     ; push argc
    jsr     _main
    ; exit(return_value)
    move.l  %d0, %d1        ; exit code
    moveq   #1, %d0         ; SYS_EXIT
    trap    #0
```

---

## Multitasking Architecture

### Overview

Preemptive multitasking with process groups, job control, pipes, and
I/O redirection. This is the minimum required for a usable interactive
Unix shell.

### Process Model

```
Boot → init (pid 1) → shell (pid 2)
  shell: parse command line
    simple:    vfork → exec(program) → waitpid
    pipe:      vfork → pipe → vfork → exec(left) | exec(right)
    background: vfork → exec(program) → shell continues (no wait)
    job ctrl:  ^Z → SIGTSTP → fg/bg → SIGCONT
```

### Process Table (expanded from current)

```c
struct proc {
    uint8_t  state;         /* P_FREE, P_RUNNING, P_READY, P_SLEEPING, P_ZOMBIE, P_STOPPED */
    uint8_t  pid;
    uint8_t  ppid;
    int8_t   exitcode;

    /* Saved CPU state */
    uint32_t sp;
    uint32_t pc;
    uint32_t regs[16];      /* d0-d7, a0-a7 */
    uint16_t sr;

    /* Memory */
    uint32_t mem_base;
    uint32_t mem_size;
    uint32_t brk;

    /* Job control (NEW) */
    uint8_t  pgrp;          /* process group ID */
    uint8_t  session;       /* session ID */
    uint8_t  ctty;          /* controlling terminal minor */

    /* Signals (NEW) */
    uint16_t sig_pending;   /* bitmask of pending signals */
    uint16_t sig_blocked;   /* bitmask of blocked signals */
    uint32_t sig_handler[16]; /* handler addresses (SIG_DFL=0, SIG_IGN=1, or function pointer) */

    /* Filesystem */
    uint16_t cwd;
    struct ofile *fd[MAXFD];
};
```

### Scheduler

Timer-driven preemptive round-robin. The VBlank interrupt (50/60 Hz on
Mega Drive) or the workbench timer interrupt fires, and the scheduler
picks the next READY process.

```c
void schedule(void) {
    struct proc *next = NULL;
    /* Round-robin: scan from curproc+1, wrapping */
    for (int i = 1; i <= MAXPROC; i++) {
        struct proc *p = &proctab[(curproc->pid + i) % MAXPROC];
        if (p->state == P_READY) {
            next = p;
            break;
        }
    }
    if (next == NULL) return;  /* only one runnable process */
    context_switch(curproc, next);  /* save/restore regs, swap stacks */
}
```

Context switch implemented in assembly (~30 lines):
- Save d0-d7/a0-a6 via `MOVEM.L` (one instruction, 15 registers)
- Save/restore USP via `MOVE USP, An` / `MOVE An, USP`
- Save/restore kernel SP in proc struct
- `RTE` to resume

### vfork() Implementation

```c
int do_vfork(void) {
    struct proc *child = proc_alloc();
    if (!child) return -ENOMEM;

    /* Child shares parent's memory — no copy */
    child->mem_base = curproc->mem_base;
    child->mem_size = curproc->mem_size;
    child->brk = curproc->brk;

    /* Copy file descriptors (increment refcounts) */
    for (int i = 0; i < MAXFD; i++) {
        child->fd[i] = curproc->fd[i];
        if (child->fd[i]) child->fd[i]->refcount++;
    }

    /* Copy job control state */
    child->pgrp = curproc->pgrp;
    child->session = curproc->session;
    child->ctty = curproc->ctty;
    child->cwd = curproc->cwd;

    /* Save parent state, block parent */
    curproc->state = P_VFORK;

    /* Child runs on parent's stack */
    child->state = P_RUNNING;
    child->ppid = curproc->pid;

    /* Return 0 to child, child's pid to parent (when it wakes) */
    curproc = child;
    return 0;  /* in child */
}
```

The libc vfork stub must save the return address in a register (not on
the stack) because the child will corrupt the stack:

```asm
; libc/vfork.S
_vfork:
    move.l  (%sp)+, %a1     ; save return address in a1
    moveq   #190, %d0       ; SYS_VFORK
    trap    #0
    move.l  %a1, -(%sp)     ; restore return address
    rts
```

### Pipes

```c
#define PIPE_SIZE 512  /* fits comfortably in RAM */

struct pipe {
    uint8_t  buf[PIPE_SIZE];
    uint16_t read_pos;
    uint16_t write_pos;
    uint16_t count;         /* bytes in buffer */
    uint8_t  readers;       /* number of open read ends */
    uint8_t  writers;       /* number of open write ends */
};
```

`pipe()` creates two file descriptors sharing a kernel `struct pipe`.
- `write()` to pipe: copies data into ring buffer, wakes sleeping reader
- `read()` from pipe: copies data from ring buffer, wakes sleeping writer
- If pipe full: writer sleeps
- If pipe empty: reader sleeps (or returns 0 if no writers remain = EOF)
- If reader closed: writer gets `SIGPIPE`

~80 lines of kernel C.

### Signals

Minimum signal set for job control and shell operation:

| Signal | Number | Default | Use |
|--------|--------|---------|-----|
| SIGHUP | 1 | terminate | Terminal hangup |
| SIGINT | 2 | terminate | ^C |
| SIGQUIT | 3 | terminate | ^\ |
| SIGKILL | 9 | terminate (uncatchable) | Force kill |
| SIGPIPE | 13 | terminate | Broken pipe |
| SIGTERM | 15 | terminate | Graceful kill |
| SIGCHLD | 17 | ignore | Child exited (for waitpid) |
| SIGCONT | 18 | continue | Resume stopped process |
| SIGSTOP | 19 | stop (uncatchable) | Force stop |
| SIGTSTP | 20 | stop | ^Z |

Signal delivery on 68000 — build a trampoline frame on the user stack:

```c
void deliver_signal(struct proc *p, int sig) {
    uint32_t handler = p->sig_handler[sig];
    if (handler == SIG_DFL) { default_action(p, sig); return; }
    if (handler == SIG_IGN) return;

    /* Build signal frame on user stack */
    uint32_t sp = p->regs[15];  /* a7 = user SP */
    sp -= 4;  /* return address: sigreturn trampoline */
    sp -= 4;  /* signal number argument */

    /* Write trampoline: moveq #SYS_SIGRETURN, d0; trap #0 */
    /* This is 4 bytes of 68000 machine code */
    *(uint32_t *)(sp) = sig;
    *(uint32_t *)(sp - 4) = sp + 8;  /* trampoline addr */

    /* The trampoline itself (written to stack): */
    /* 0x70XX4E40 where XX = SYS_SIGRETURN number */

    p->regs[15] = sp;
    p->pc = handler;
}
```

~150 lines total for signal dispatch + sigreturn.

---

## TTY Subsystem

### Architecture

Three layers, following the proven Fuzix design:

```
┌─────────────────────────────────────┐
│  User: read() / write() / ioctl()   │
├─────────────────────────────────────┤
│  tty.c: Line discipline             │
│  • Cooked mode (canonical input)    │
│  • Raw mode (pass-through)          │
│  • Echo, erase, kill                │
│  • Signal generation (^C, ^Z, ^\)   │
│  • termios support                  │
│  • Job control (foreground pgrp)    │
├─────────────────────────────────────┤
│  devtty.c: Platform driver          │
│  • tty_putc() → VDP tile output     │
│  • VBlank ISR → keyboard polling    │
│  • tty_inproc() injection           │
├─────────────────────────────────────┤
│  Hardware: VDP + Saturn keyboard    │
└─────────────────────────────────────┘
```

### Implementation Strategy: Port Fuzix tty.c

The Fuzix `tty.c` is ported nearly as-is. It already works on this hardware.

**Core functions retained:**
- `tty_open(minor)` / `tty_close(minor)` — device lifecycle
- `tty_read(minor, flag)` — blocking canonical/raw read
- `tty_write(minor, flag)` — character output via `tty_putc()`
- `tty_inproc(minor, c)` — ISR-safe character injection
- `tty_ioctl(minor, request, data)` — termios, winsize, pgrp

**Line discipline features:**
- Cooked mode: line buffering, echo, erase (`^H`), kill (`^U`)
- Raw mode: immediate character delivery
- Signal generation: `^C` → SIGINT, `^Z` → SIGTSTP, `^\` → SIGQUIT
- `^D` on empty line → EOF

**Data structures:**
```c
struct tty {
    struct termios termios;     /* c_iflag, c_oflag, c_cflag, c_lflag, c_cc[] */
    uint8_t  inq[256];         /* circular input buffer */
    uint8_t  inq_head;         /* write pointer (ISR writes here) */
    uint8_t  inq_tail;         /* read pointer (reader reads here) */
    uint8_t  fg_pgrp;          /* foreground process group */
    uint8_t  minor;            /* device minor number */
    uint8_t  flags;            /* TTYF_OPEN, etc. */
};
```

The 256-byte circular buffer with `uint8_t` indices gives free modulo
wraparound — no modulo instruction needed on 68000.

### Platform Driver (from Fuzix)

Ported directly from the Fuzix Mega Drive branch:

| File | Function | Status |
|------|----------|--------|
| `devtty.c` | `tty_putc()`, `tty_setup()`, `do_beep()` | Port as-is |
| `devvdp.c` / `devvdp.h` | VDP register access, DMA, tile upload | Port as-is |
| `devvt.S` | `plot_char`, `scroll_up/down`, `clear_*`, cursor | Port as-is |
| `font_init.S` | 8x8 font loading into VRAM | Port as-is |
| `keyboard.c` / `keyboard.h` | Saturn keyboard decode state machine | Port with cleanup |
| `keyboard_read.S` | Controller port polling (asm) | Port as-is |

### TTY Phases

| Phase | Feature | Complexity |
|-------|---------|------------|
| 1 | Port Fuzix tty.c + devtty.c + VDP + keyboard | ~500 lines (mostly ported) |
| 2 | `/dev/tty` alias, `TIOCGWINSZ` (40×28), termios polish | ~50 lines |
| 3 | Interrupt-driven keyboard (TH line on controller port) | ~100 lines asm |
| 4 | Multiple TTY devices (port 2, EXP port) | ~200 lines |
| 5 | Full job control polish, `/dev/null`, `/dev/zero` | ~100 lines |

---

## Fuzix Optimizations We Inherit

By using Fuzix's drivers and porting its tty.c, we get:

1. **VDP tile rendering in 68000 asm** — `MOVEM`-based bulk copies for scroll,
   word-aligned VDP writes, cursor via hardware sprite or tile swap.

2. **Saturn keyboard state machine** — handles key-up/key-down, auto-repeat,
   modifier keys (Shift, Ctrl, Alt), all in tested C code.

3. **ISR-safe tty input** — `tty_inproc()` designed to be called from interrupt
   context. Head/tail buffer updates are single-word writes = atomic on 68000.

4. **Context switch via MOVEM** — saves 15 registers in one instruction. This
   is the fastest possible approach on 68000.

5. **ROM disk + RAM disk drivers** — zero-copy reads from ROM, block-level
   writes to SRAM. Already handles the Mega Drive memory map.

6. **Efficient buffer cache** — 16 1KB buffers with dirty tracking. Minimizes
   disk I/O for the shell's frequent small reads/writes.

---

## Architecture Overview

```
┌─────────────────────────────────────┐
│  User Programs (sh, ed, grep ...)   │
│  Linked with Fuzix libc            │
│  Binary format: Fuzix a.out        │
├─────────────────────────────────────┤
│  TRAP #0 — Syscall Interface        │
├─────────────────────────────────────┤
│  Kernel (~3500 lines)              │
│  ┌──────────┬────────┬──────────┐  │
│  │ Process  │Filesys │ Device   │  │
│  │ (vfork,  │(minifs)│ (console,│  │
│  │  exec,   │        │  disk,   │  │
│  │  sched,  │        │  pipe,   │  │
│  │  signal) │        │  tty)    │  │
│  └──────────┴────────┴──────────┘  │
├─────────────────────────────────────┤
│  Platform Abstraction Layer (PAL)   │
│  (console, timer, disk, memory)     │
├─────────────────────────────────────┤
│  Hardware / Emulator                │
└─────────────────────────────────────┘
```

## Phase 1: The Workbench (Emulated 68000 SBC) -- COMPLETE

Musashi-based 68000 SBC emulator with UART, timer, and disk I/O.
Runs in any terminal, instant startup, printf debugging.

See [docs/emulator.md](docs/emulator.md) for architecture and usage.

**Status: COMPLETE.**

## Phase 2: The Kernel

### Phase 2a: Binary Loading + Single-Tasking exec() -- COMPLETE

**Completed:**
- Boot, console I/O, kprintf
- Filesystem (minifs) — read, write, create, delete, rename, mkdir, rmdir
- Memory allocator (first-fit with coalescing)
- Buffer cache (16 blocks)
- Device table (console + disk)
- Basic process structure (16 slots, file descriptors)
- Syscall dispatch via TRAP #0 (~20 syscalls implemented)
- Built-in debug shell (ls, cat, echo, mkdir, write, mem, etc.)
- Binary loader (Genix flat binary, 32-byte header)
- User crt0.S + minimal libc stubs (15 syscalls)
- exec() with single-tasking semantics
- User programs: hello, echo, cat
- 34 host tests, all passing
- Mega Drive ROM boots in BlastEm and on real hardware

**Status: COMPLETE.** See [docs/binary-format.md](docs/binary-format.md).

**Remaining (in priority order):**

### Phase 2b: Multitasking -- NEXT

7. **Preemptive scheduler** (~50 lines) — Timer interrupt triggers round-robin
   scheduling. Context switch via MOVEM save/restore.
8. **vfork()** (~80 lines) — Parent sleeps, child runs on parent's stack.
   Libc stub saves return address in a1 register.
9. **waitpid()** (~40 lines) — Parent collects zombie children.
10. **exit()** cleanup (~30 lines) — Free memory, close fds, reparent children,
    wake parent.

### Phase 2c: Pipes and Redirection

11. **pipe()** (~80 lines) — 512-byte kernel ring buffer. Blocking read/write
    with sleep/wake.
12. **dup2()** — Already implemented. Used by shell for I/O redirection.
13. **Shell pipes** — Shell creates pipe, vfork twice, exec left and right
    sides, connect via dup2.
14. **Shell redirection** — `>`, `<`, `>>`, `2>` via open() + dup2().

### Phase 2d: Signals and Job Control

15. **Signal delivery** (~150 lines) — Trampoline on user stack, sigreturn
    syscall, handler dispatch.
16. **SIGINT/SIGQUIT/SIGTSTP** — Generated by tty line discipline on ^C/^\/ ^Z.
17. **SIGCHLD** — Sent to parent when child exits or stops.
18. **SIGCONT** — Resume stopped process.
19. **SIGPIPE** — Sent when writing to pipe with no readers.
20. **Process groups** (~50 lines) — `setpgrp()`, foreground/background groups.
21. **Job control in shell** — `fg`, `bg`, `jobs` commands. ^Z stops foreground
    job, shell resumes.

### Phase 2e: TTY Subsystem

22. **Port Fuzix tty.c** — Line discipline, cooked/raw modes, echo, erase.
23. **Port devtty.c** — VDP output, keyboard input via VBlank polling.
24. **Port VDP stack** — devvdp, devvt.S, font_init.S.
25. **Port keyboard driver** — keyboard.c, keyboard_read.S.
26. **termios** — TCGETS/TCSETS ioctls, TIOCGWINSZ (40×28).

### Phase 2f: Fuzix libc + Utilities

27. **Port Fuzix libc** — stdio, stdlib, string, ctype, unistd, dirent,
    termios. Adapt syscall stubs to our TRAP #0 / d1-d4 convention.
28. **Port Fuzix shell** — Job control, pipes, redirection, history.
29. **Port core utilities** — ls, cat, cp, mv, rm, mkdir, rmdir, echo, grep,
    wc, sort, head, tail, tee, chmod (no-op), date, pwd, kill, ps.
30. **Port editors** — ed (line editor), levee (vi clone).
31. **Port development tools** — ar, make, small C compiler.

## Phase 3: Mega Drive Port -- COMPLETE

The PAL implementation reuses proven Fuzix drivers. The Mega Drive ROM
builds, boots in BlastEm, and runs on real hardware with EverDrive.

See [docs/megadrive.md](docs/megadrive.md) for cartridge configurations,
SRAM, and real hardware testing.

**Status: COMPLETE.** Mega Drive PAL drivers ported from Fuzix:
- `devvt.S` — VDP text output
- `keyboard.c` / `keyboard_read.S` — Saturn keyboard
- `devrd.c` — ROM disk + RAM disk
- `crt0.S` — boot code, vector table, VDP init
- `megadrive.S` — interrupt handlers

## Phase 4: Polish and Extended Features

32. **Interrupt-driven keyboard** — TH line interrupt on controller port,
    replacing VBlank polling. Reduces latency from ~20ms to ~microseconds.
33. **Multiple TTY devices** — Port 2, EXP port for second keyboard or serial.
34. **/dev/null, /dev/zero** — Standard special devices.
35. **getcwd()** — Walk `.` and `..` entries to reconstruct path.
36. **More Fuzix apps** — games, BASIC, BCPL, TCL, tar, dd.

---

## Memory Map

### Workbench (1MB RAM)

```
0x000000 ┌──────────────────┐
         │ Interrupt Vectors │ 1KB
0x000400 ├──────────────────┤
         │ Kernel code+data │ ~16KB
0x004400 ├──────────────────┤
         │ Kernel heap       │ (proc table, ofile table, inodes, bufs, pipes)
         │ ~32KB             │
0x00C000 ├──────────────────┤
         │ User processes    │ (allocated by kmalloc per exec)
         │ [proc: shell]    │
         │ [proc: grep]     │
         │ [proc: sort]     │
         │ ...              │
         │ Free memory       │
0x100000 └──────────────────┘
```

### Mega Drive

```
ROM (4MB):
0x000000 ┌──────────────────┐
         │ Vectors + Header │ 512 bytes
0x000200 ├──────────────────┤
         │ Kernel code       │ ~16KB
         ├──────────────────┤
         │ .rodata           │ (font, strings)
         ├──────────────────┤
         │ ROM disk          │ ~1.9MB (read-only filesystem)
0x3FFFFF └──────────────────┘

Cartridge SRAM (512KB):
0x200000 ┌──────────────────┐
         │ RAM disk          │ (read-write filesystem)
0x27FFFF └──────────────────┘

Main RAM (64KB):
0xFF0000 ┌──────────────────┐
         │ Kernel BSS+data  │ ~4KB
         ├──────────────────┤
         │ Kernel heap       │ (proc table, bufs, pipes)
         │ ~24KB             │
         ├──────────────────┤
         │ User processes    │ (each ~8-16KB)
         ├──────────────────┤
         │ Stack (kernel)    │ ~2KB
0xFFFFFF └──────────────────┘
```

---

## Decision Log

| Decision | Choice | Reasoning |
|----------|--------|-----------|
| Start from scratch vs. fix FUZIX | From scratch | FUZIX is 15K+ lines of someone else's kernel. Cheaper to write 3K lines we understand. |
| fork() vs vfork() | vfork only | fork() on no-MMU is the root cause of problems. vfork+exec is proven (uClinux). |
| Development platform | Musashi-based SBC emulator | Terminal I/O, instant startup, host-side debugging. |
| **C library** | **Fuzix libc** | Right size (~5KB), proven on 68000, 143+ apps written against it. newlib is too large. |
| Filesystem | Custom simple fs (minifs) | Educational value, minimal code, exactly the features we need. |
| **Binary format** | **Fuzix a.out** | 16-byte header, simple relocations, 143+ utilities ready to compile. |
| Process limit | 8-16 | 512KB cart RAM + 64KB main. Each process ~8KB min. 16 × 8KB = 128KB. |
| **TTY subsystem** | **Port Fuzix tty.c** | Already working on this hardware. Line discipline, cooked/raw, signals. |
| **Scheduler** | **Preemptive round-robin** | Timer interrupt (VBlank on MD). Simple, fair, proven. |
| **Signals** | **10 signals minimum** | SIGINT, SIGQUIT, SIGTSTP, SIGCONT, SIGCHLD, SIGPIPE, SIGKILL, SIGTERM, SIGHUP, SIGSTOP. |
| **Pipe buffer** | **512 bytes** | Fits in kernel heap. Large enough for most pipeline operations. |
| **Keyboard** | **VBlank polling first, TH interrupt later** | Polling works now (proven). Interrupt is an optimization for Phase 4. |
| **App ecosystem** | **Fuzix utilities first** | 143+ apps designed for 64KB-class systems. Largest available pool for this hardware. |

## Open Questions

1. **Fuzix a.out 32-bit variant?** The 16-byte Fuzix header has 16-bit size
   fields (max 64KB per segment). For the Mega Drive this is fine — 64KB main
   RAM limits process size anyway. But if we want to use cartridge RAM for
   larger programs (up to 512KB), we may need 32-bit sizes. Options:
   a) Interpret sizes as 256-byte pages (gives up to 16MB per segment)
   b) Use a 32-byte extended header for large programs
   c) Accept 64KB limit (probably fine for all practical Mega Drive apps)
   **Recommendation: Start with 64KB limit. Extend later if needed.**

2. **Fuzix libc licensing.** Fuzix libc is GPL-2.0. Our kernel code should be
   compatible. Verify before porting.

3. **Shell: Fuzix sh or custom?** Fuzix's shell has job control, pipes, and
   history. Porting it is faster than writing our own but adds ~2000 lines.
   A minimal custom shell (~500 lines) gets us started; port Fuzix sh when
   we need full job control.
   **Recommendation: Custom minimal shell first, port Fuzix sh for Phase 2d.**

4. **brk() vs mmap() for userspace malloc.** In single-tasking mode, brk()
   works fine — nothing is "after" the process. In multi-tasking mode, brk()
   is problematic if another process is loaded after ours. Options:
   a) Use brk() and always load processes from the top of memory downward
   b) Implement a simple mmap()-like allocator
   c) Accept that brk() only works for the last-loaded process
   **Recommendation: brk() with careful load ordering. Revisit if it breaks.**

## Next Steps

1. ~~Set up repo structure~~ ✓
2. ~~Vendor Musashi, build the workbench emulator~~ ✓
3. ~~Boot to a `>` prompt~~ ✓
4. ~~Implement basic syscalls (read/write/open/close)~~ ✓
5. ~~Implement filesystem (minifs)~~ ✓
6. ~~Implement exec() — binary loader~~ ✓
7. ~~Implement user crt0.S + minimal libc stubs~~ ✓
8. ~~Build and boot on Mega Drive (BlastEm + real hardware)~~ ✓
9. **Implement vfork() + waitpid()** ← next
10. **Implement preemptive scheduler**
11. **Implement pipe()**
12. **Port Fuzix tty.c**
13. **Implement signals**
14. **Migrate binary format to Fuzix a.out** (relocation support)
15. **Port Fuzix libc**
16. **Port Fuzix utilities**
