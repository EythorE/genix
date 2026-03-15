# Genix Project History

A comprehensive record of how Genix evolved from a FUZIX Mega Drive fork
into a standalone 68000 operating system. This document consolidates
project history from multiple sources for anyone who wants to understand
why specific commits were made.

For active design decisions guiding future development, see
[docs/decisions.md](docs/decisions.md). For the forward-looking plan, see
[PLAN.md](PLAN.md).

---

## Table of Contents

1. [Prologue: FUZIX Heritage](#1-prologue-fuzix-heritage)
2. [The Original Plan](#2-the-original-plan)
3. [Implementation Timeline](#3-implementation-timeline)
4. [Bugs and Lessons Learned](#4-bugs-and-lessons-learned)
5. [The Relocation Story](#5-the-relocation-story)
6. [Performance Analysis](#6-performance-analysis)
7. [Plan vs Reality](#7-plan-vs-reality)
8. [Project Metrics](#8-project-metrics)

---

## 1. Prologue: FUZIX Heritage

_What Genix took from FUZIX, what's different, and what was missing._

> **Note (March 2026):** All items listed as "Currently unimplemented"
> in the Missing table below have now been implemented. See Section 3
> for the implementation timeline.

### Background

[FUZIX](https://github.com/EythorE/FUZIX) is a multi-user Unix clone
supporting 30+ platforms. The
[megadrive branch](https://github.com/EythorE/FUZIX/tree/megadrive)
ports it to the Sega Mega Drive. Genix replaces the FUZIX kernel
(15K+ lines of someone else's code) with ~5,400 lines we understand,
while reusing the Mega Drive platform drivers that already work.

### What We Took

#### Mega Drive Drivers (pal/megadrive/)

All files in `pal/megadrive/` are adapted from FUZIX's
`Kernel/platform/platform-megadrive/`:

| Genix File | Fuzix Source | What it does |
|------------|-------------|-------------|
| `crt0.S` | `crt0.S` | Boot: vector table, Z80 halt, VDP init, BSS clear |
| `vdp.S` | `vdp.S` | VDP register setup, VRAM clear, palette, font upload |
| `devvt.S` | `devvt.S` | Text output: plot_char, scroll, cursor, clear |
| `dbg_output.S` | `dbg_output.S` | BlastEm debug channel output |
| `fontdata_8x8.c` | `font/font8x8.c` | 8x8 bitmap font (ASCII 32-127) |
| `keyboard.c` | `keyboard.c` | Saturn keyboard state machine |
| `keyboard.h` | `keyboard.h` | Keyboard API |
| `keyboard_read.S` | `keyboard_read.S` | Controller port polling (assembly) |
| `keycode.h` | `keycode.h` | Scancode definitions |
| `macros.S` | `macros.S` | Assembly helper macros |
| `control_ports.def` | `control_ports.def` | Controller port register addresses |
| `platform.c` | (new) | PAL adapter wrapping the Fuzix drivers |

`platform.c` is new — it implements the Genix PAL interface by calling
the Fuzix assembly routines. The assembly files are adapted with minimal
changes (mostly removing Fuzix-specific includes and adapting to Genix's
linker script).

#### Design Decisions Informed by FUZIX

| Decision | From Fuzix | Genix adaptation |
|----------|-----------|-----------------|
| **vfork() only** | Fuzix uses vfork+exec on no-MMU platforms | Same approach, no fork() |
| **Flat binary format** | Fuzix uses 16-byte a.out header | Genix uses 32-byte header (later extended with relocation) |
| **Preemptive scheduler** | VBlank-driven round-robin | Same timer source, simpler scheduler |
| **ttyinq buffer** | uint8_t head/tail, 256-byte circular buffer | Same design (division-free) |
| **Context switch** | MOVEM.L d0-d7/a0-a6 | Same approach (callee-saved only: d2-d7/a2-a6) |
| **TRAP for syscalls** | TRAP #14 with args in d1-d4/a0-a2 | TRAP #0 with args in d1-d4 only |
| **Block size 1024** | Fuzix uses 512 on some platforms, 1024 on others | 1024 everywhere |
| **Buffer cache** | 16 1KB buffers with dirty tracking | Same (configurable NBUFS) |

### What's Different

#### Genix Kernel vs Fuzix Kernel

| Aspect | Fuzix | Genix |
|--------|-------|-------|
| **Size** | ~15,000 lines (kernel alone) | ~5,650 lines |
| **Multi-user** | Yes (UIDs, permissions, login) | No |
| **Platforms** | 30+ (Z80, 6809, 68000, ARM, ...) | 68000 only |
| **fork()** | Supported on some platforms | Not supported (vfork only) |
| **Filesystem** | Classic Unix FS with dual-indirect | minifs (single-indirect, simpler) |
| **Process table** | Complex with signal masks, timers | 16-slot array with kstacks |
| **TTY** | Full line discipline, cooked/raw, multi-tty | Simplified line discipline, 4 TTYs |
| **Device model** | Major/minor with full dev_t | Simple function-pointer table |
| **Syscall ABI** | TRAP #14, mix of d-regs and a-regs | TRAP #0, d0-d4 only |
| **Binary format** | a.out with relocations | Relocatable flat binary ("GENX" header) |
| **C library** | Full Fuzix libc (~5 KB compiled) | Custom libc (~5 KB, 16 modules) |

#### Syscall Convention

```
Fuzix:  d0=syscall#, d1=arg1, a0=arg2(ptr), a1=arg3(ptr)
Genix:  d0=syscall#, d1=arg1, d2=arg2, d3=arg3, d4=arg4
```

Genix's convention is cleaner — all arguments in data registers.

#### Filesystem

Fuzix uses a classic Unix filesystem with 512-byte blocks and
dual-indirect blocks. Genix's minifs uses 1024-byte blocks and
single-indirect (max 524 KB per file).

### What Was Missing (All Now Implemented)

| Feature | Fuzix status | Genix status at start | Final status |
|---------|-------------|----------------------|-------------|
| Multitasking | Working | Planned | **Done** (Phase 2b) |
| vfork() | Working | Stub | **Done** |
| waitpid() | Working | Stub | **Done** |
| Pipes | Working | Planned | **Done** (Phase 2c) |
| Signals | Working (full POSIX set) | Planned | **Done** (21 signals, Phase 2d) |
| TTY line discipline | Full cooked/raw | Simple console | **Done** (Phase 2e) |
| Job control (^Z, fg, bg) | Working | Planned | **Done** (SIGTSTP/SIGCONT) |
| termios | Full implementation | Not implemented | **Done** |
| Process groups | Working | Planned | **Done** |
| Binary relocations | Working | Not implemented | **Done** (Phase 5-6) |
| /dev/null | Available | Not implemented | **Done** |
| Full libc | ~5 KB, 60+ source files | Minimal stubs | **Done** (16 modules) |
| 143+ Unix utilities | Available | 3 (hello, echo, cat) | **34 apps** |

### Fuzix Features Deliberately Excluded

| Feature | Why |
|---------|-----|
| Multi-user (UIDs, permissions) | Single-user system |
| Login / passwd / groups | No users |
| setuid / setgid | No users |
| File permissions (chmod) | No users |
| Network stack | Not a goal |
| Banked memory support | 68000 has flat memory |
| Floppy / serial / RS-232 drivers | Mega Drive doesn't have these |
| 30+ platform ports | 68000 only |

### Fuzix Source References

For anyone studying the FUZIX implementation:

- **Context switch**: `Kernel/cpu-68000/lowlevel-68000.S`
- **Syscall entry**: `Kernel/cpu-68000/lowlevel-68000.S`
- **Signal delivery**: `Kernel/cpu-68000/lowlevel-68000.S`
- **TTY line discipline**: `Kernel/tty.c`
- **Process management**: `Kernel/process.c`
- **Signals**: `Kernel/signal.c`
- **Pipes**: `Kernel/filesys.c` (pipe section)
- **Platform (Mega Drive)**: `Kernel/platform/platform-megadrive/`

Repository: <https://github.com/EythorE/FUZIX/tree/megadrive>

---

## 2. The Original Plan

_The design document that started the project. See [Section 7](#7-plan-vs-reality)
for what actually happened._

### The Problem

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

### Goals

1. **Readable** — The entire kernel fits in your head. Every line has a reason.
2. **Single-user** — No UIDs, no permissions, no login. Like DOS, but with a
   proper syscall interface.
3. **POSIX-enough** — Enough POSIX syscalls that standard C programs compile
   and Unix utilities run.
4. **No MMU required** — Designed from the start for flat memory.
5. **Portable to Mega Drive** — The Mega Drive is the final target, but we
   develop on something simpler first.
6. **Small** — Measured in thousands of lines, not tens of thousands.

### Architecture

```
User Programs (sh, apps)  — linked with libc syscall stubs
        | TRAP #0
Kernel (proc, fs, dev, mem, exec, syscall)
        |
Platform Abstraction Layer (PAL)
        |
Hardware / Emulator
```

### Planned Phases

| Phase | What | Estimated Scope |
|-------|------|-----------------|
| 1 | Workbench emulator (Musashi + UART + timer + disk) | ~300 lines C |
| 2a | Single-tasking kernel (boot, console, fs, exec) | ~2000 lines C + ~200 asm |
| 2b | Multi-tasking (scheduler, vfork, process table) | ~500 lines C + ~100 asm |
| 3 | Mega Drive PAL (reuse existing drivers) | ~500 lines asm (existing) |
| 4 | Shell + basic coreutils | Port existing code |

**Target: ~3000 lines of new kernel code.**

### Key Original Decisions

| Decision | Choice | Reasoning |
|----------|--------|-----------|
| Start from scratch vs. fix FUZIX | From scratch | 15K+ lines of someone else's kernel. Cheaper to write 3K lines we understand. |
| fork() vs vfork() | vfork only | fork() on no-MMU is the root cause of our problems. Proven by uClinux. |
| Development platform | Musashi-based SBC emulator | Terminal I/O, instant startup, host-side debugging. |
| C library | newlib (later changed to custom) | Standard m68k-elf support. |
| Filesystem | Custom simple fs (minifs) | Educational value, minimal code. |
| Binary format | PIC ELF or bFLT (later changed) | Standard for no-MMU systems. |
| Single-tasking first | Yes | Eliminates entire classes of bugs. |

### Original File Structure (Proposed)

```
megadrive-os/
├── emu/                    # Workbench emulator
│   ├── main.c              # Musashi wrapper
│   └── musashi/            # Musashi 68000 core (vendored, MIT)
├── kernel/
│   ├── crt0.S, trap.S, main.c, proc.c, fs.c, dev.c, mem.c
│   └── kernel.h
├── pal/
│   ├── workbench/          # Workbench SBC platform
│   └── megadrive/          # Mega Drive platform (from FUZIX drivers)
├── libc/                   # C library stubs
├── apps/                   # Userspace programs
└── Makefile
```

---

## 3. Implementation Timeline

_Chronological record of each phase, sourced from commit history and
design decisions._

### Phase 1: Workbench Emulator

**Status:** Complete at project start

Built a trivial 68000 SBC in software using Musashi (the same CPU core
MAME uses). Memory map: 1MB RAM, UART at 0xF00000, timer at 0xF10000,
disk at 0xF20000.

The workbench gives terminal I/O, instant startup, and printf debugging.
`make run` goes from source to running kernel in ~2 seconds.

**Lesson:** The workbench cut iteration time from minutes to seconds.
Worth the ~400 lines of emulator code.

**Emulator exit mechanism:** Ctrl+C doesn't work in raw mode, so the
emulator uses Ctrl+] (like telnet) as a force-quit key. Clean shutdown
goes through `pal_halt()` which writes to a power-off register at
0xF30000. The pal_halt() abstraction ensures both workbench (exit
emulator) and Mega Drive (halt CPU) have a clean shutdown path.

### Phase 2a: Single-Tasking Kernel

**Status:** Complete

- Boot, console I/O, kprintf
- Filesystem (minifs) — read, write, create, delete, rename, mkdir, rmdir
- Memory allocator (first-fit with coalescing)
- Buffer cache (16 blocks)
- Device table (console + disk)
- Basic process structure (16 slots, file descriptors)
- Syscall dispatch via TRAP #0 (~20 syscalls implemented)
- Built-in debug shell
- Binary loader (Genix flat binary, 32-byte header)
- User crt0.S + minimal libc stubs
- exec() with single-tasking semantics
- User programs: hello, echo, cat, wc, head, true, false

### Phase 2b: Multitasking

**Date:** 2026-03-09 to 2026-03-10

**spawn() before vfork():** The original plan was vfork()+exec(). We
implemented `vfork_save`/`vfork_restore` (setjmp/longjmp-style assembly)
but discovered a fundamental problem: vfork() returns twice, and the
child's exec() stack frames overwrite the parent's stack. This is the
same class of bug that made FUZIX's fork() unfindable.

**Solution:** `do_spawn()` — a combined vfork+exec that never returns to
the child. Initially single-tasking (parent blocks while child runs).

**Per-process kernel stacks:** Each process gets a 512-byte kernel stack
embedded in struct proc. User programs run in 68000 user mode (S=0 in SR)
with separated USP/SSP stacks. Architecture: exec_enter() saves callee-saved
regs on supervisor stack, switches SSP to process's kstack_top, sets USP,
enters user mode via RTE with SR=0x0000. TRAP #0 handler saves all user
registers (d0-d7/a0-a6) + USP on kstack, pushes syscall args, calls
syscall_dispatch, restores and RTEs back. exec_leave() abandons kstack
and longjmps back via saved_ksp.

**Two-level context switching:** (1) `swtch(old_ksp, new_ksp)` — assembly
that saves callee-saved registers (d2-d7, a2-a6), swaps stack pointers,
restores, and RTS. Used by both preemptive (timer ISR) and voluntary
(sleep/waitpid) paths. (2) `proc_first_run` trampoline — when swtch()
resumes a brand-new process, RTS lands here, pops user-mode state frame,
RTEs to enter user mode. (3) `proc_setup_kstack(proc, entry, user_sp)` —
builds initial kstack frame: swtch frame (d2-d7/a2-a6 zero, retaddr →
proc_first_run), user state (USP, d0-d7/a0-a6 zero), exception frame
(SR=0x0000, PC=entry). Total: 118 bytes, leaving 394 bytes for syscall
call chains.

**Preemptive scheduling:** Timer ISR saves user state on kstack → calls
schedule() → swtch() → new process runs. Cooperative scheduling was
rejected because it requires every program to yield voluntarily — a busy
loop would hang the system, breaking POSIX expectations.

**ISR nesting safety concern:** With preemptive scheduling, a timer ISR can
fire while a syscall is in progress on the kstack, nesting another exception
frame + ISR register saves (~40 bytes). The "don't preempt kernel mode" rule
prevents this. If kernel preemption is ever needed, kstacks must grow larger.

**Async do_spawn:** Non-blocking — allocates process slot, loads binary,
sets up kstack, marks child P_READY and returns child PID. Parent
continues immediately.

**Blocking waitpid/pipes:** Parent sleeps when child hasn't exited;
pipe read/write sleep when empty/full.

**Multitasking pain points:** Blocking pipe + single-threaded test — autotest
pipe test writes 5 bytes then reads 8; with blocking pipes the reader would
block forever. Fixed with POSIX partial-read semantics (return once any data
available). Single user memory space: all user programs load at USER_BASE, two
can't coexist; shell runs in supervisor mode so shell + one child work. kstack
layout must match ISR exactly — byte offsets in proc_setup_kstack must match
what proc_first_run expects; 68000 exception frame is 6 bytes (2-byte SR +
4-byte PC) creating misalignment with 4-byte register slots.

### Phase 2c: Pipes and I/O Redirection

**Date:** 2026-03-10

Shell-level pipe (`|`), output redirect (`>`/`>>`), and input redirect
(`<`). Pipes use a 512-byte circular buffer.

**Sequential pipeline execution:** Because all user processes share
USER_BASE (no MMU), two user processes cannot be loaded simultaneously.
`echo hello | cat` works as: spawn echo (wait), then spawn cat (wait).
The pipe buffer holds intermediate data.

**SIGPIPE:** Generated when writing to a pipe with no readers.

**Pipeline pain points:** 512-byte pipe buffer limits pipelines to small
outputs; `cat /bin/ls | wc` would overflow and lose data. do_spawn_fd() FD
replacement logic (decrement refcount, handle pipe cleanup, set new FD,
increment refcount) repeated 3 times for stdin/stdout/stderr — kept explicit
to avoid abstraction for a one-use pattern.

### Phase 2d: Signals

**Date:** 2026-03-10

First implemented default actions only (SIGINT terminates, SIGCHLD
ignored, etc.), then added full user signal handlers.

**Signal frame architecture:** When delivering to a user handler,
sig_deliver() builds an 84-byte frame on the user stack containing:
return address to trampoline, signal number, 4-byte trampoline
(moveq + trap), and saved 70-byte kstack frame. The handler runs in
user mode; on return, the trampoline triggers SYS_SIGRETURN which
restores all user registers.

**SIGTSTP/SIGCONT:** Default action sets P_STOPPED, schedule() skips
stopped processes. SIGCONT wakes them to P_READY.

**Process groups:** Each process gets pgrp=pid by default. Infrastructure
for shell job control (kill(-pgrp, sig)).

**Signal design decisions:** (1) Frame pointer parameter: sig_deliver() and
sys_sigreturn() receive pointer to saved user state on kstack from asm code,
avoiding fragile stack offset calculations in C. (2) SYS_SIGRETURN handled
in asm: _vec_syscall checks for syscall 119 before calling syscall_dispatch,
letting sigreturn modify the kstack frame directly without normal return path
overwriting d0. (3) Full 70-byte frame save: essential for timer-delivered
signals where interrupt can happen at any point — all registers must be
preserved exactly (not just d0+PC). (4) Trampoline on user stack: 4-byte
`moveq #119,%d0; trap #0` lives in signal frame; 68000 has no NX bit so
executable stack is fine. (5) One-shot handlers (classic signal() semantics —
handler resets to SIG_DFL after delivery; future: add sigaction() with
SA_RESTART if needed). (6) One handler per sig_deliver call (multiple pending
signals delivered one at a time via nested sigreturn, avoids nested frames).

**Job control limitations (no-MMU):** Full fg/bg/jobs is limited by shared
USER_BASE. A stopped process's code/data at USER_BASE is intact only until
another command runs. Background processes cannot truly run concurrently with
the shell. Real job control requires either an MMU or process relocation.
SYS_SIGRETURN handled in asm before syscall_dispatch to avoid d0 overwrite.
Trampoline lives on user stack (68000 has no NX bit). 84 bytes per signal
frame on user stack.

**Signal pain points:** ERANGE missing from kernel.h — cross-compilation
caught it but host tests didn't (they use host `<errno.h>`). dev_init()
called before fs_init() — dev_create_nodes() tried to create /dev/null
before filesystem was mounted; all fs operations silently failed. Fixed by
splitting into dev_init() (hardware) and dev_create_nodes() (after fs_init).
SYS_GETDENTS didn't advance file offset in ofile struct, causing readdir()
to return the same entry forever. Full job control limited by shared
USER_BASE — stopped process's memory is intact only until another command
runs.

### Phase 2e: TTY Subsystem

**Date:** Phase 2e

Ported a simplified Fuzix-style TTY line discipline as kernel/tty.c
(~320 lines). Three-layer architecture: user read/write/ioctl → tty.c
line discipline → PAL console.

Features: cooked mode (line buffering, echo, erase, kill, ^D EOF), raw
mode (immediate delivery), signal generation (^C→SIGINT, ^\→SIGQUIT,
^Z→SIGTSTP), input/output mapping (ICRNL, ONLCR), termios ioctls
(TCGETS/TCSETS/TIOCGWINSZ), /dev/tty and /dev/console device nodes.

78 host unit tests.

**TTY pain points:** Kernel shell now reads through tty_read() instead of
doing its own line editing — simplified shell code but required testing
both paths. Double echo risk: old con_read() echoed characters AND shell
echoed them too; with TTY layer, echo happens only in tty_inproc(). kputc
vs tty_write output paths: kernel diagnostic output bypasses OPOST (correct
behavior, but means NL→CRNL handling differs between kernel and user
output). Incomplete element type: `extern struct tty tty_table[]` in
kernel.h fails because C requires complete type for extern arrays — keep
declaration in tty.h only. Rule: never put extern arrays of incomplete
types in shared headers.

### Phase 2f: Libc + 34 Utilities

**Date:** 2026-03-10

**Libc decision:** Originally planned to port Fuzix libc. Instead wrote
a custom libc that ended up comparable in size (~5 KB) but exactly
tailored to the Genix syscall interface. 16 modules: stdio, stdlib,
string, ctype, termios, getopt, sprintf/sscanf, strtol, perror, regex,
dirent, isatty, gfx, divmod.S, syscalls.S.

**Why not newlib:** newlib is 50-100 KB — too large for the Mega Drive's
64 KB RAM.

**Tier 1 utilities (8 programs):** strings, fold, expand, unexpand,
paste, comm, seq, tac. All custom implementations under 100 lines each.
Chose to write from scratch rather than port Fuzix source files because
adapting Fuzix headers/calling conventions was more effort than writing
simple tools.

**Tier 2 utilities (4 programs):** grep (with regex), od, env, expr.
Required adding regex engine (~240 lines), sscanf, qsort, bsearch,
environment variables to libc.

**Shell improvements:** PATH search (/bin/ prefix), cd builtin, implicit
exec (no need to type "exec").

**Levee (vi clone):** Successfully ported from Fuzix. Exercises the full
stack: FILE*, malloc, termios, indirect blocks. 44 KB binary — too large
for Mega Drive (~31 KB user space), workbench-only.

**What was needed for levee:** (1) C library additions: ctype.c, stdlib.c
(malloc/free via sbrk, atoi, getenv), stdio.c (FILE*, fopen/fclose/fgets/
fprintf), termios.c (tcgetattr/tcsetattr wrapping ioctl). (2) Header files:
`<ctype.h>`, `<termios.h>`, `<fcntl.h>`, `<sys/stat.h>`. (3) Kernel termios:
console raw mode via `con_raw` flag, toggled by TCGETS/TCSETS ioctl.
(4) Filesystem indirect blocks in both mkfs and kernel bmap(). (5) Missing
source file `ucsd.c`: levee depends on moveleft(), moveright(), fillchar(),
lvscan() from ucsd.c — not obvious from the Makefile, must trace undefined
symbols.

**Levee pain points:** ucsd.c not listed in obvious SRCS variable; K&R-style
C from the 1980s needing extensive `-Wno-*` flags (-Wno-implicit-int,
-Wno-return-type, -Wno-parentheses, -Wno-implicit-function-declaration,
-Wno-char-subscripts); conflicting open() declarations between fcntl.h
(variadic) and unistd.h (non-variadic) — both must be variadic.

**What levee validates:** The full stack (raw terminal I/O, file I/O, dynamic
memory, ANSI escape codes) works correctly. Proves Genix can run real Unix
software, not just toy programs.

**Phase 2f pain points:** Shell sort chosen over quicksort for qsort — on
68000, recursion costs 18 cycles per JSR + register saves, and kernel stacks
are 512 bytes. Shell sort is O(n^1.5) worst case with constant stack usage.
sscanf uses `(&fmt + 1)` to walk stack-passed arguments which doesn't work
on x86-64 host; test sscanf helpers on host, full sscanf only via guest
programs. grep is 7 KB with regex library — fits in MD's ~28 KB user space
but is one of the larger utilities. sed was omitted as requiring significant
regex integration (substitution, addresses, hold/pattern space). Environment
variables are process-local since there's no fork() — each exec'd process
starts fresh.

**Tier 1 pain points:** tac uses a 4 KB static buffer (limits reversible
file size). paste reads one byte at a time (one syscall per byte — simple
and correct but slow). expand uses modulo for tab stops (DIVU.W safe:
small constant divisor).

### Phase 3: Mega Drive Port

**Status:** Complete early (concurrent with Phase 2a)

PAL implementation reuses proven Fuzix drivers: devvt.S (VDP text),
keyboard.c/keyboard_read.S (Saturn keyboard), crt0.S (boot, vectors,
VDP init). The ROM builds, boots in BlastEm, and runs on real hardware
with EverDrive.

**Own VDP driver, not SGDK:** SGDK is the most popular Mega Drive SDK but
assumes exclusive ownership of CPU, interrupts, memory, and all hardware.
Running it under an OS would require replacing most of its internals.
Instead: a kernel VDP driver with userspace libgfx library via ioctls,
following the pattern from Fuzix.

**BlastEm 0.6.3-pre and headless testing:** Extensive investigation of
automated keyboard input in BlastEm: tried XTest, xdotool, LD_PRELOAD shim,
GL on/off, BlastEm debugger, GDB remote — all failed. Root cause: SDL2 uses
XInput2 (XI_RawKeyPress) for keyboard input on X11, which doesn't receive
synthetic events from xdotool. Solution: upgraded to BlastEm 0.6.3-pre
nightly which has `-b N` flag for truly headless runs (run N frames then
exit, exit code 0 = no crash). Screenshot tests use external scrot capture
instead of BlastEm's native screenshot key (also blocked by XInput2).

### Phase 4: Polish

**Date:** 2026-03-10

1. **Configurable buffer cache:** NBUFS is now `#ifndef` (default 16).
   Mega Drive uses -DNBUFS=8, saving 8 KB.

2. **Multi-TTY infrastructure:** NTTY=4. Device nodes /dev/tty0-tty3.
   All share VDP output; keyboard routes to TTY 0.

3. **Interrupt-driven keyboard:** pal_keyboard_poll() called from VBlank
   ISR. Characters fed to tty_inproc(0, key). Replaced polling in
   tty_read(). (See Bug: Saturn Keyboard Double-Read Race below.)

4. **SRAM validation:** Boot-time check for valid minifs magic. Zeroes
   SRAM if invalid. Standard Sega mapper (0xA130F1 = 0x03) works on
   real carts, all EverDrive models, and BlastEm. Key practical finding:
   no bank switching is needed because ROM is < 2 MB, leaving the SRAM
   address space (0x200000) free on all cartridge configurations.

**Phase 4 pain points:** VBlank ISR keyboard vs polling — the polling
approach in pal_console_getc() still exists for the rare case where getc
is called directly; ISR path is primary; both handle F12 filtering; no race
because inq_head write is atomic (uint8_t) on 68000. NBUFS=8 on Mega Drive:
each buffer is 1 KB + header, reducing from 16 to 8 saves ~8 KB; trade-off
is more disk I/O on cache misses but sequential access patterns keep hit rate
acceptable. Multi-TTY is wiring only — actual TTY switching (foreground/
background) needs keyboard shortcuts and VDP context switching. SRAM zeroing
on invalid magic rather than auto-formatting with mkfs — safer than assuming
a specific filesystem layout.

**Automated imshow screenshot test:** Added `make test-md-imshow` — spawns
imshow on Mega Drive (BlastEm under Xvfb) and captures a screenshot of the
VDP color bar test pattern. imshow is not an image viewer — it generates
test patterns dynamically using VDP tile-based 4bpp graphics: 16-color test
palette, 18 tiles (15 solid + 2 checkerboards + 1 gradient), fills 40x28
tile screen. Uses `-n` flag (no-wait mode, 120 vsync frames then exits).
Pain points: gfx_close() doesn't fully restore VDP state (cosmetic); no
automated pixel comparison (visual inspection only); BlastEm screenshot
capture is fragile (can't use native key, must use scrot); imshow is Mega
Drive only (/dev/vdp); slow rebuild cycle (~30s for special ROM approach).

**Test coverage expansion (2026-03-10):** Added test_buf.c (36 assertions,
buffer cache with mock PAL disk layer — cache hits/misses, dirty/clean
eviction, write-back), test_kprintf.c (24 assertions, captures kputc output
via mock — %d, %u, %x, %s, %c, %%, negative numbers, zero, NULL string),
test_pipe.c (2170 assertions, non-blocking pipe reimplementation — exact
fill, overflow, partial reads, EOF, EPIPE, circular wrap, 512 individual
byte writes, alternating patterns). Brought total from 2675 to 4924 host
test assertions across 13 test files (84% increase).

**CI pipeline expansion (2026-03-10):** Expanded from 1 job to 3:
(1) host-tests (`make test`), (2) cross-build (fetches m68k-elf toolchain,
builds kernel + ROM + emulator), (3) emu-tests (workbench autotest, BlastEm
headless boot, BlastEm AUTOTEST — the primary quality gate). Jobs run
sequentially matching the testing ladder order.

### Three-Branch Merge

**Date:** 2026-03-10

Three parallel development branches merged into one:

1. **Track A** (preemptive scheduling): Per-process kstacks, user mode,
   swtch(), proc_first_run, async do_spawn, blocking pipes/waitpid.
   ~760 lines changed.

2. **Track B** (libc + 12 new apps): getopt, perror, sprintf, strtol,
   isatty, plus basename, cmp, cut, dirname, nl, rev, tail, tee, tr,
   uniq, yes. ~1560 lines added.

3. **Track C** (VDP imshow): VDP device driver, libgfx, imshow app,
   232-line test suite. ~1300 lines added.

5 conflicts (all list-type: .gitignore, Makefile CORE_BINS, apps
PROGRAMS, libc OBJS, tests TESTS). No semantic conflicts — the branches
touched orthogonal subsystems.

### CI Pipeline

**Date:** 2026-03-10

Expanded from 1 job (make test) to 3 jobs:
1. host-tests — make test
2. cross-build — fetch toolchain, build kernel + megadrive + emulator
3. emu-tests — workbench autotest, BlastEm headless, BlastEm AUTOTEST

### Relocatable Binaries (March 2026)

Rather than migrating to Fuzix a.out (as the original plan suggested),
we extended the existing Genix header to support relocation directly.
See [Section 5: The Relocation Story](#5-the-relocation-story) for the
full narrative.

### Phase 6: `-msep-data` + Slot Allocator (March 2026)

**Date:** 2026-03-14

Enabled concurrent multitasking with shared ROM text. All user programs
now compile with GCC's `-msep-data` flag, which makes all data access
go through register a5 (GOT-relative). The kernel sets a5 per-process
to point at each process's data slot.

**What was built:**

1. **Build system:** Added `-msep-data` to CFLAGS for all .c files in
   apps/ and libc/. Assembly files (.S) use separate ASFLAGS without
   `-msep-data` since the assembler doesn't understand it.

2. **Linker script (`apps/user-reloc.ld`):** Added `.got` section
   between `.data` and `.bss` (GOT must be in RAM for writes). Added
   `ALIGN(4)` at end of `.text` to ensure text_size is 4-byte aligned
   (68000 faults on odd word/long access).

3. **mkbin (`tools/mkbin.c`):** Encodes GOT offset as (offset+1) in
   upper 16 bits of `stack_size` header field. Generates synthetic
   relocations for GOT entries (see bug below). 0=no GOT, N=GOT at
   offset N-1 from data section start.

4. **romfix (`tools/romfix.c`):** Detects `-msep-data` binaries via
   GOT offset field. Resolves text-segment relocations at build time
   (ROM addresses). Defers data-segment relocations for runtime (slot
   addresses vary per process). Preserves reloc table for kernel use.

5. **Kernel slot allocator (`kernel/mem.c`):** Divides user RAM into
   fixed-size slots. Mega Drive: 2 slots of ~13.75 KB each from 27.5 KB
   user space. Workbench: 8 slots of ~88 KB each from 704 KB.
   `slot_init()`, `slot_alloc()`, `slot_free()`, `slot_base()`,
   `slot_size()`.

6. **Process management (`kernel/proc.c`):** `do_spawn` allocates a
   slot, loads binary into it, sets a5 in the new process's register
   frame. `do_exit` frees the slot. `proc_setup_kstack` takes a
   `user_a5` parameter for a5 at offset 52 in the register block.

7. **Exec (`kernel/exec.c`):** Updated `load_binary` and
   `load_binary_xip` to compute a5 from GOT offset. XIP loader applies
   runtime data-segment relocations (text relocs done by romfix, data
   relocs deferred because slot address varies). Validates against
   `slot_size()` instead of `USER_SIZE`. `do_exec` allocates a
   temporary slot for synchronous exec.

8. **User mode entry (`kernel/exec_asm.S`):** Loads a5 from global
   `exec_user_a5` before RTE to user mode.

9. **Concurrent pipelines (`kernel/main.c`):** Shell now spawns all
   pipeline stages first, then waits for all. Previously sequential.

**Measured results:**

- 11 files changed, 378 insertions, 81 deletions
- Binary sizes slightly larger due to GOT (hello: 616→660 bytes with
  GOT relocs). Reloc counts increased (hello: 1→3, levee: 2533→2560).
- Workbench: 8 slots × 88 KB. Mega Drive: 2 slots × ~13.75 KB.
- All 63 host tests pass, 31/31 workbench autotest pass, Mega Drive
  builds and runs (600 frames, no crash).

### Phase A: Libc Prerequisites (March 2026)

**Date:** 2026-03-14

Added POSIX headers and functions needed by any future userspace
program (prerequisite for dash shell port). 17 files changed,
+504/-43 lines.

**What was built:**

1. **New headers (7):** setjmp.h (jmp_buf, setjmp/longjmp, sigjmp_buf
   aliases, BSD _setjmp/_longjmp aliases), sys/types.h (pid_t, uid_t,
   gid_t, off_t, mode_t, size_t, ssize_t), sys/wait.h (WIFEXITED/
   WEXITSTATUS/WIFSIGNALED/WTERMSIG/WIFSTOPPED macros, WNOHANG,
   WUNTRACED), sys/stat.h (32-byte struct stat, S_ISREG/S_ISDIR/
   S_ISCHR macros, permission bit constants), limits.h (CHAR_BIT,
   INT_MIN/MAX, LONG_MIN/MAX, PATH_MAX, NAME_MAX), paths.h
   (_PATH_BSHELL, _PATH_DEVNULL, _PATH_TTY), time.h (time_t, clock_t,
   struct timeval/timespec).

2. **setjmp_68000.S:** setjmp saves d2-d7/a2-a6 (11 callee-saved
   registers, 44 bytes) plus SP at offset 44 (total jmp_buf = 48
   bytes). longjmp restores registers + SP, enforces val != 0.
   Ported from FUZIX Library/libs/setjmp_68000.S.

3. **signal.c:** sigaction() implemented as a wrapper around the
   kernel's signal() syscall (read old handler, set new handler).
   sigprocmask() is a stub (returns 0, Genix doesn't support signal
   masks yet). raise() calls kill(getpid(), sig).

4. **unistd_stubs.c:** 16 POSIX stub functions. UID/GID functions
   (getuid/geteuid/getgid/getegid/setuid/setgid/seteuid/setegid)
   all return 0 (single-user system). Process group stubs (getpgrp
   returns getpid(), setpgid no-op, getppid returns 1, tcsetpgrp/
   tcgetpgrp). Timer stubs (alarm, sleep). sysconf returns 60 for
   _SC_CLK_TCK (Mega Drive NTSC VBlank rate). access() uses stat()
   to check file existence.

5. **Header updates:** signal.h expanded with NSIG (21), sigset_t
   (unsigned long), sigset macros (sigemptyset/sigfillset/sigaddset/
   sigdelset/sigismember), struct sigaction, SA_RESTART/SA_NOCLDSTOP/
   SA_RESETHAND, SIG_BLOCK/SIG_UNBLOCK/SIG_SETMASK, and missing
   signal numbers (SIGILL/SIGTRAP/SIGABRT/SIGBUS/SIGFPE/SIGUSR1/
   SIGSEGV/SIGUSR2/SIGALRM). fcntl.h expanded with F_DUPFD/F_GETFD/
   F_SETFD/F_GETFL/F_SETFL and FD_CLOEXEC. unistd.h expanded with
   STDIN/STDOUT/STDERR_FILENO and all new function declarations.

6. **Kernel fs_stat() POSIX conversion:** Replaced 12-byte kstat
   struct with 32-byte posix_stat struct. type_to_mode() converts
   FT_FILE→S_IFREG|0755, FT_DIR→S_IFDIR|0755, FT_DEV→S_IFCHR|0666.
   Device files populate st_rdev with (major<<8|minor). All time
   fields set to mtime (minifs doesn't track atime/ctime separately).

7. **Consumer updates:** apps/ls.c now includes sys/stat.h and uses
   S_ISREG/S_ISDIR/S_ISCHR macros instead of local FT_* constants.
   tests/test_fs.c updated to use posix_stat struct and verify
   st_mode = 0040755 for directories.

### Phase B: Kernel Enhancements (March 2026)

**Date:** 2026-03-14

General-purpose kernel improvements needed by any userspace shell.
4 files changed, +301/-25 lines. fs_stat was already done in Phase A
so only two items remained.

**What was built:**

1. **fcntl F_DUPFD:** Replaced the SYS_FCNTL stub (returned 0) with
   a real implementation. F_DUPFD finds the lowest free fd >= arg, dups
   the source ofile, and increments refcount (with rollback on failure).
   F_GETFD/F_SETFD return/accept 0 (no cloexec yet). F_GETFL returns
   open flags masked to exclude internal pipe bits (0x0FFF mask).
   F_SETFL is a stub returning 0. Added fd_alloc_from(of, minfd)
   helper for the "lowest fd >= arg" semantics.

2. **waitpid WNOHANG:** Changed do_waitpid(pid, status) to
   do_waitpid(pid, status, options). After the "no exited child found"
   scan, checks (options & WNOHANG) and returns 0 instead of blocking.
   Updated syscall dispatch to pass a3 as options. Updated all 13
   callers in kernel/main.c to pass 0 for options. The libc waitpid
   stub already passed d3 (options) — no libc changes needed.

3. **Host tests (9 new):** fcntl: F_DUPFD basic dup, skip occupied
   fds, table-full error, F_GETFL flag readback. waitpid: WNOHANG
   with running child (returns 0), WNOHANG with zombie (reaps it),
   no children (-ECHILD), blocking reap, specific PID targeting.

**Deviations from plan:** None. Both items were straightforward.

**Gotcha:** Process 0 has ppid=0, so do_waitpid called by process 0
sees itself as its own child. Not a bug in practice (process 0 never
calls waitpid without real children), but the test had to use a
non-zero PID process to test the "no children" path.

### Phase C: dash Shell Port (2026-03-14)

Ported dash 0.5.12 (Debian Almquist Shell) as a Genix userspace application.
With Phase 6 (`-msep-data`) complete, dash runs as a normal process in its
own RAM slot with text executing from ROM via XIP.

**Binary size:** text=91,236 data=4,500 bss=2,296. Data+bss = 6,796 bytes
(49% of 14 KB Mega Drive slot). Fits comfortably with room for stack and heap.

**Source:** 32 dash source files + 3 bltin files + genix_stubs.c = 36 files
compiled. Downloaded from kernel.org, autotools-generated files produced on
host, then cross-compiled with m68k-elf-gcc + `-msep-data`.

**Configuration:** JOBS=0 (no job control), SMALL=1 (reduced features),
no line editing, no glob/fnmatch (Genix libc doesn't have them).

**New libc infrastructure added for dash:**
- 7 new headers: inttypes.h, pwd.h, sys/ioctl.h, sys/param.h,
  sys/resource.h, sys/time.h, sys/times.h
- 15+ new functions: strtoll, strtoull, strtod (integer-only), abort,
  vsnprintf (full implementation with va_list), strpbrk, stpncpy,
  strsignal (with signal name table), isblank, isgraph, ispunct, isxdigit
- Updated headers: errno.h (+ERANGE, ELOOP, ENAMETOOLONG, EWOULDBLOCK),
  fcntl.h (+O_NONBLOCK, O_NOCTTY), signal.h (+sig_atomic_t, SIGTTIN,
  SIGTTOU, NSIG=23), ctype.h, string.h, stdlib.h, stdio.h (+BUFSIZ,
  vsnprintf), sys/stat.h (+S_ISUID, S_ISGID, S_ISVTX, S_IFLNK, S_ISLNK),
  sys/types.h (+time_t guard)

**Genix compatibility layer (genix_compat.h):**
- `fork()` → `vfork()` (no MMU)
- `wait3()` → `waitpid(-1, ...)`
- `strtoimax/strtoumax` → `strtoll/strtoull`
- `sigsuspend` stub (returns -1)
- `lstat` → `stat` (no symlinks)
- `setpgrp(a,b)` → `setpgid(a,b)`
- `umask` stub (returns 022)
- `alloca` → `__builtin_alloca`

**Integration:** kernel/main.c spawns `/bin/dash` with respawn loop,
falls back to `builtin_shell()` if dash not found. dash added to
CORE_BINS (included in both workbench and Mega Drive disk images).

**All tests pass:** host (63/63), test-emu (31/31), full test-all ladder
including BlastEm headless and autotest.

**Major build issues encountered and fixed (in order):**
1. intmax_t/uintmax_t type conflict (GCC long long vs libc long)
2. Missing stub headers (sys/param.h, sys/resource.h, etc.) — `#include <...>` requires physical files
3. Garbled mkbuiltins output — needed proper autotools generation
4. strchrnul/mempcpy/stpcpy — unset HAVE_ flags so dash's system.c provides them
5. Multiple type redefinition conflicts (struct timeval, mode_t, dirent64)
6. Missing libc functions (strtoll, vsnprintf, strtod, abort, strpbrk, etc.)
7. sys_siglist undefined — provided strsignal with signal name table instead
8. bgcmd/fgcmd/getgroups undefined — added stubs in genix_stubs.c
9. Duplicate fstat definition between genix_stubs.c and libc syscalls.o

---

## 4. Bugs and Lessons Learned

_Every bug that cost significant debugging time. Each entry explains the
root cause and how to prevent recurrence._

### Bug 1: JSR Corrupts User Stack Layout

**Symptom:** `exec /bin/hello` crashed with address error on Mega Drive.

**Root cause:** `exec_asm.S` used `JSR (%a0)` to enter user programs.
JSR pushes a return address onto the user stack, but crt0.S expects the
first value to be `argc`. The return address overwrites argc.

**Fix:** Use `JMP (%a0)` instead of `JSR`. User programs never "return"
— they call `_exit()` via TRAP #0.

**Lesson:** On the 68000, JMP is a pure jump; JSR pushes a return
address. When transitioning to user mode, any extra stack pushes break
the argc/argv contract.

### Bug 2: Unaligned Stack Array on 68000

**Symptom:** `hello` crashed with address error when accessing a local
`char buf[]` array at an odd address.

**Root cause:** `char buf[13]` on the stack followed by a `write()` call.
The compiler placed buf at an odd offset. When write() read it as a
word-aligned address, the 68000 faulted.

**Fix:** Changed buffer size to even (`char buf[14]`).

**Lesson:** The 68000 faults on word/long access at odd addresses. Local
arrays should always be even-sized.

### Bug 3: USER_BASE/USER_TOP Hardcoded

**Symptom:** User programs loaded at workbench addresses (0x040000) on
Mega Drive, writing outside 64 KB main RAM.

**Root cause:** USER_BASE and USER_TOP were compile-time constants, not
platform-provided values.

**Fix:** Made them global variables set from PAL functions at boot.

**Lesson:** Memory layout must be platform-provided (via PAL). Different
targets have different address maps.

### Bug 4: BlastEm Version Differences

**Symptom:** `make test-md` passes on some machines, fails on others.

**Root cause:** The `-g` flag passed to BlastEm behaves differently
across versions. Some versions don't support it.

**Fix:** Removed the flag. Keep headless test flags minimal.

**Related:** Upgraded to BlastEm 0.6.3-pre nightly for `-b N` flag
(truly headless, no Xvfb needed). Exit code 0 = no crash.

### Bug 5: libgcc BSR.L in User Programs

**Symptom:** `wc` (uses `/` and `%`) crashed with illegal instruction.

**Root cause:** The toolchain's `libgcc.a` contains `__umodsi3` with
`BSR.L` (opcode `61FF`) — a **68020-only instruction**. User programs
linked against system libgcc.

**Fix:** Added `divmod.S` to `libc/libc.a`. Since libc is linked before
libgcc, our safe 68000 division routines take priority.

**Lesson:** On a 68000 system, **every library** that might contain
division must be checked for 68020 instructions. `-m68000` only affects
code you compile, NOT pre-built libraries.

### Bug 6: Indirect Block bmap() on Big-Endian

**Symptom:** Levee binary (44 KB) loaded only 28640 of 44720 bytes.
First 12 KB (direct blocks) correct, then garbage.

**Root cause:** `bmap()` read indirect block entries using byte-level
decomposition on a `uint16_t*`, treating it as `uint8_t*` (reading 4
bytes per entry instead of 2).

**Fix:** Simply index `uint16_t*` directly: `ptrs[bn]`.

**Lesson:** On big-endian systems, don't manually byte-swap data already
in native order. The byte-swap pattern is correct for host tools reading
from `uint8_t*` buffers, not for the kernel where data is in native
byte order.

### Bug 7: mkfs Lacked Indirect Block Support

**Symptom:** Files >12 KB silently truncated in filesystem image.

**Root cause:** mkfs only wrote direct block pointers; no code for
indirect blocks.

**Fix:** Added indirect block allocation to mkfs.

**Lesson:** Test with files larger than 12 KB early. Levee (44 KB) was
the first file large enough to trigger this.

### Bug 8: Kernel Stack Overflow Corrupts proc Struct

**Symptom:** `do_pipe()` returned -EMFILE even with only 3 fds open.
fd[] slots contained garbage like `0xa4760000`.

**Root cause:** The kstack is at the END of struct proc and grows
downward. With KSTACK_SIZE=256, the deepest syscall path (TRAP frame +
syscall args + 6 levels of C calls = ~214 bytes) left only 42 bytes of
headroom — not enough for GCC's register saves. The stack overflowed
into fd[], cwd, vfork_ctx.

**Fix:** Increased KSTACK_SIZE from 256 to 512 bytes. Costs 4 KB more
RAM for the 16-entry process table.

**Lesson:** When embedding a stack inside a struct, overflows corrupt
adjacent fields silently. The 68000 has no stack guard pages.

### Bug 9: Mega Drive USER_BASE Collision

**Symptom:** Adding 512-byte kstacks pushed kernel BSS past the old
USER_BASE of 0xFF8000.

**Fix:** Bumped Mega Drive USER_BASE to 0xFF9000. Leaves ~27.5 KB for
user programs (was ~31 KB).

**Later resolved:** Relocatable binaries changed entry points to 0-based
offsets. The loader adds USER_BASE at exec() time, so separate linker
scripts are no longer needed.

### Bug 10: Data Block Deallocation on Inode Free

**Symptom:** Deleted files permanently leaked data blocks. Eventually
ENOSPC on SRAM-backed filesystems.

**Root cause:** fs_iput() had a TODO: when refcount and nlink both
reached 0, the inode type was set to FT_FREE but data blocks were never
returned to the free list.

**Fix:** Added block deallocation to fs_iput(): free direct blocks,
read indirect block and free its data blocks, then free the indirect
block itself.

### Bug 11: Saturn Keyboard Double-Read Race Condition

**Symptom:** Typing fast on Mega Drive produced ghost characters
(especially spaces).

**Root cause:** Two code paths both called keyboard_read():
1. VBlank ISR (pal_keyboard_poll) — the intended interrupt-driven path
2. tty_read() polling loop — the legacy path (pal_console_ready()
   returned 1 unconditionally)

ReadKeyboard() reads a 12-nibble Saturn keyboard packet by toggling the
TH line. If the VBlank ISR fires mid-read, it consumes remaining
nibbles from the in-progress packet. Both callers get garbled scancodes.

**Fix:** Changed Mega Drive pal_console_ready() to return 0, disabling
the polling path entirely. Keyboard input flows exclusively through the
VBlank ISR.

**Lesson:** When adding interrupt-driven I/O, the old polling path must
be disabled. This bug only appears on real hardware, never on the
workbench emulator.

### Bug 12: VDP Console Tab Character as Glyph

**Symptom:** `ls -l` on Mega Drive displayed a strange symbol between
file size and filename.

**Root cause:** pal_console_putc() only handled `\n`, `\r`, `\b`. The
tab character fell through to plot_char(), drawing the CP437 glyph at
font index 9.

**Fix:** Added `\t` handling: advance cursor to next 8-column tab stop.

**Lesson:** Every control character that user programs might print must
be explicitly handled in the VDP console driver. The host terminal hides
these gaps during workbench testing.

### Bug 13: VDP User Tiles Overwrote Console Font VRAM

**Symptom:** Running imshow corrupted the console font.

**Root cause:** vdp_do_loadtiles() computed VRAM address without adding
the VRAM_USER_TILES offset. User tiles wrote into the console font area
(VRAM 0x0000-0x0FFF).

**Fix:** User tiles now map to VDP tile IDs 128+ (VRAM 0x1000+), keeping
the font area intact.

**Lesson:** When two subsystems share VRAM, their tile ranges must be
explicitly partitioned. A defined-but-unused constant is a code smell.

### Bug 14: levee putfile() Missing Return Statement

**Symptom:** `:w test.txt` in levee silently failed to report errors.

**Root cause:** putfile() was declared as returning bool but had no
return statement. On 68000, d0 after write() accidentally contained the
byte count (non-zero), so the caller's check passed by luck.

**Fix:** Added `return n == (endd - start)`.

**Lesson:** Missing return statements in non-void functions are UB. On
68000, the last call's d0 leaks as the return value. `-Wreturn-type`
catches this.

### Bug 15: Declaration Drift

**Root cause:** `pal_halt()` declared in both `kernel.h` and `pal.h`
with slightly different signatures.

**Rule:** Single source of truth for declarations. Never duplicate
function prototypes across headers.

### Bug 16: Redirection Parsing Destroys Adjacent Operators

**Symptom:** `cat <infile>outfile` (redirections without spaces) silently
lost the second operator. Only the input redirect was parsed; the output
file was never opened.

**Root cause:** `parse_redirections()` used a `for` loop with a
`p--`/`p++` pattern to compensate for the loop increment after processing
each redirect. When the filename scanner stopped at another redirect
operator (e.g., `>` immediately after `infile`), the code executed
`*p = '\0'; p++;` — null-terminating the `>` character and advancing past
it — then `p--` moved back to the null, and the loop's `p++` advanced
past where the operator had been. The `>` was destroyed and never parsed.

The bug was masked by the test suite because all existing tests used
spaces around redirect operators (`< infile > outfile`), where the
filename scanner stopped at the space instead of the operator.

**Fix:** Replaced the `for` loop with a `while` loop using explicit
pointer control. Each branch advances `p` only as needed. Spaces after
filenames are null-terminated and skipped; redirect operators are left
intact for the next iteration to process. Added `<` to the infile
scanner's stop set and `>` to the outfile scanner's stop set for
defensive robustness.

**Lesson:** Compensating for a loop increment with `p--` is fragile when
the pointer position depends on what character terminated the inner scan.
Explicit pointer control in a `while` loop is clearer and avoids
off-by-one errors that depend on input formatting.

### Meta-Lesson: Workbench vs Mega Drive Divergence

| Behavior | Workbench (Musashi) | Mega Drive (68000) |
|----------|--------------------|--------------------|
| Unaligned access | Silently works | Address error fault |
| Jump to odd address | Silently works | Address error fault |
| Access unmapped memory | Returns 0 / silently writes | Bus error or ROM/RAM overlay |
| Stack at odd address | Silently works | Address error on push/pop |

**Rule:** Always verify `make megadrive` and `make test-md` alongside
workbench testing.

### `--emit-relocs` Does NOT Emit GOT Entry Relocations (Phase 6)

**Symptom:** Programs compiled with `-msep-data` loaded successfully
but hung immediately. No address error, no crash — just silence.

**Root cause:** When GCC uses `-msep-data`, the linker creates a GOT
(Global Offset Table) with absolute addresses for functions and data
accessed through a5. The linker's `--emit-relocs` flag preserves
relocations from input `.o` files, but the GOT entries are
**linker-generated** — they don't come from input relocations. So
`--emit-relocs` does NOT emit R_68K_32 relocations for them.

Result: mkbin extracted only 1 relocation for hello (the `jsr main` in
crt0), but the 2 GOT entries (addresses of `write` and `msg.0`) were
left at their zero-based values. When the program tried to call `write`
through GOT[0], it jumped to address 0x6e instead of 0x4006e.

**Fix:** mkbin now scans the `.got` section and generates synthetic
R_68K_32 relocations for each non-zero 4-byte entry. These are added
to the relocation table alongside the ELF-extracted relocations,
sorted and deduplicated normally.

**Lesson:** `--emit-relocs` is not a complete solution for
position-independent binaries that use linker-generated sections (GOT,
PLT). Always verify that ALL absolute addresses in the binary have
corresponding relocation entries. A quick check: compare the number of
non-zero 32-bit values in the data section against the relocation
count.

### Odd text_size Causes Address Error in GOT (Phase 6)

**Symptom:** `ADDRESS ERROR: read.L at odd address 0x040005`.

**Root cause:** The `.text` section ended at an odd size (595 bytes for
hello). Since `.data` (containing the GOT) follows immediately, the GOT
started at an odd VMA. The 68000 faults on word/long access at odd
addresses, so reading GOT entries via `move.l (offset, a5)` crashed.

**Fix:** Added `ALIGN(4)` at the end of `.text` in `user-reloc.ld`.
This pads text_size to a 4-byte boundary, ensuring `.data` and `.got`
start at an even address.

**Lesson:** Always ensure section boundaries are at least word-aligned
when the next section contains data accessed as words or longs.

### GOT Offset Zero Ambiguity (Phase 6)

**Symptom:** Programs with GOT at the very start of the data section
(got_offset=0) were treated as having no GOT.

**Root cause:** The header used `got_offset == 0` to mean "no GOT",
but 0 is a valid offset (GOT at data section start). This is common —
most small programs have empty `.data` and the GOT is the first thing
in the data region.

**Fix:** Changed to offset+1 encoding: 0 = no GOT, N = GOT at offset
(N-1). Added `HDR_HAS_GOT()` and `HDR_GOT_OFFSET()` macros to
kernel.h. mkbin encodes as `(got_vma - text_size) + 1`. Kernel
decodes with `(field >> 16) - 1`.

**Lesson:** Classic sentinel-value problem. When zero is a valid value,
use a separate flag or offset-by-one encoding. Document the encoding
prominently since multiple tools (mkbin, romfix, kernel) must agree.

### Synchronous exec blocked vfork+execve (dash hang)

**Date:** March 2026
**Symptom:** Dash shell hung immediately after being spawned as the
default shell. No output, no prompt.

**Root cause:** `do_exec()` was purely synchronous — it loaded a binary,
called `exec_enter()` which blocked until `_exit()` called `exec_leave()`,
then returned the exit code. When a vfork child called `execve()`:

1. `do_exec` ran the program synchronously, waited for it to finish
2. `execve()` returned the exit code to the child (POSIX says execve
   never returns on success)
3. Dash interpreted any return from `execve()` as failure
4. No concurrent execution — parent was frozen in `P_VFORK` the entire time

**Fix:** Added vfork-aware async path in `do_exec()`. After loading the
binary (at the `run:` label), the kernel checks if the current process
has a parent in `P_VFORK` state. If so, instead of calling `exec_enter`:

1. `proc_setup_kstack()` builds a kstack frame for the child's first
   context switch (via `swtch` → `proc_first_run`)
2. Child is marked `P_READY` for the scheduler
3. Parent is woken via `vfork_restore()` (longjmp back to `do_vfork`)

The synchronous `exec_enter` path remains for autotest and the builtin
shell (process 0) which have no vfork parent.

**Secondary fix:** `do_vfork()` now sets `child->mem_slot = -1` after
`*child = *parent`. The struct copy was duplicating the parent's slot
index, meaning a child calling `_exit()` without exec would free the
parent's memory slot via `slot_free()`.

**Lesson:** POSIX `execve()` semantics (never returns on success) are
load-bearing for shell implementations. A synchronous exec that returns
an exit code violates this contract. On no-MMU systems, vfork+exec must
convert to async process creation at the exec boundary.

### libgcc 68020 opcodes in 64-bit arithmetic (dash hang)

**Date:** March 2026
**Symptom:** Dash hung during `init()` at `setvareq(defoptindvar, ...)`.
No crash, no output — just a silent freeze.

**Root cause:** The distro `libgcc.a` (`m68k-linux-gnu-gcc`) contains
68020 `MULU.L` and `DIVU.L` instructions in `__muldi3`, `__divdi3`,
and `__moddi3`. These are illegal on the 68000 and cause the CPU to
enter an exception loop (the Musashi emulator silently hangs instead
of reporting the fault).

The call chain: `setvareq` → `getoptsreset` → `number` → `atomax10` →
`strtoimax` → `strtoll` → `strtoull`. The `strtoull` function did
`result * base` as `unsigned long long` multiplication, pulling in
`__muldi3`. Similarly, `vsnprintf`'s `%d` handler used `long long`
for the accumulator, pulling in `__divdi3`/`__moddi3` for `val % 10`.

**Fix:** Rewrote `strtoull` in `libc/strtol.c` to accumulate using
two 32-bit halves (hi:lo) with 16-bit split multiplication, avoiding
`__muldi3` entirely. Rewrote `vsnprintf` integer formatting in
`libc/sprintf.c` to use `unsigned long` for non-`%lld` formats, and
manual 32-bit division for the `%lld` path.

**Known remaining issue:** `sys_getcwd` in `kernel/proc.c` allocates
a 256-byte `names[8][32]` local array, consuming most of the 512-byte
per-process kstack. This may cause kstack overflow when combined with
the TRAP frame and syscall dispatch overhead, corrupting the proc
struct and causing subsequent syscalls to malfunction. Dash still hangs
after the libgcc fixes due to this kstack pressure during `setpwd` →
`getpwd` → `getcwd` → `savestr` → `malloc` → `sbrk`.

**Lesson:** The existing CLAUDE.md pitfall about libgcc only mentioned
division (`/`, `%`). Multiplication of `long long` values is equally
dangerous. Any `unsigned long long` arithmetic — including `*` — pulls
in 68020-only libgcc functions. Audit all libc functions that touch
64-bit types. Also: 512-byte kstacks are extremely tight; any syscall
with >100 bytes of locals is a risk.

### Libc syscall stubs didn't set errno (dash PATH search broken)

**Date:** March 2026
**Symptom:** Dash printed "Success" after running external commands, and
PATH search didn't work (couldn't find `/bin/echo` without the full path).

**Root cause:** Genix libc syscall stubs returned the raw kernel return
value (e.g., -2 for ENOENT). POSIX requires stubs to detect negative
return values, set the global `errno` to the negated value, and return -1.
Dash's `tryexec()` checked `errno == ENOENT` to continue searching PATH,
but errno was always stale (never set by the stubs). Dash's `waitproc()`
checked `errno == EINTR` to retry, which also never matched.

**Fix:** Added `__set_errno` to libc syscall stubs in `libc/syscalls.S`:
on negative kernel return, negate into the `errno` global and return -1.
Stubs that return pointers (brk, sbrk) or always succeed (getpid, time)
were left unchanged.

**Lesson:** POSIX errno semantics are load-bearing for any real shell.
Every syscall stub that can fail must set errno.

### sys_getcwd kstack overflow (dash hang during setpwd)

**Date:** March 2026
**Symptom:** Dash hung or corrupted state during initialization at
`setpwd` → `getpwd` → `getcwd`.

**Root cause:** `sys_getcwd` in `kernel/proc.c` allocated a 256-byte
`names[8][32]` local array on the 512-byte per-process kstack. Combined
with the TRAP frame (~30 bytes), syscall dispatch, and the getcwd call
chain, this overflowed the kstack into the proc struct fields below.

**Fix:** Moved `names[8][32]` to static storage. Safe because the kernel
is non-preemptive (only one syscall executes at a time).

**Lesson:** Already documented in CLAUDE.md pitfalls (Bug 8), but this
was a second instance. Any syscall with >100 bytes of locals needs
scrutiny. Static storage is the standard fix for non-preemptive kernels.

### romfix/mkfs indirect block support for XIP contiguity

**Date:** March 2026
**Symptom:** Dash binary (88 KB text, ~95 KB total) was too large for
7 direct blocks (7 KB) and needed indirect blocks. romfix couldn't find
its XIP address because it only followed direct block pointers.

**Fix:** Two changes:
1. **mkfs:** Pre-allocate the indirect block *before* data blocks so that
   XIP binaries remain contiguous in ROM. Previously mkfs allocated data
   blocks first, then the indirect block, which could break contiguity.
2. **romfix:** Follow indirect block pointers to verify contiguity and
   resolve XIP addresses for large binaries.

**Lesson:** XIP contiguity requires the filesystem tool to cooperate.
mkfs must lay out blocks in ROM order, and romfix must understand the
full inode addressing scheme (direct + indirect).

### Workbench slots reduced from 8 to 6

**Date:** March 2026
**Context:** With 8 slots of 88 KB each, dash (91 KB text) didn't fit in
a single workbench slot. The workbench user memory region is 720 KB
(0x040000-0x0EFFF8).

**Fix:** Reduced to 6 slots of ~117 KB each. Dash fits comfortably.
Mega Drive was already at 2 slots and unaffected.

**Lesson:** Slot sizing must account for the largest program's needs.
With XIP, text stays in ROM but the slot must still be large enough for
the data+bss+stack. On workbench (without XIP), the full binary must fit.

### vfork TRAP frame corruption on 68000

**Date:** March 2026
**Symptom:** After running an external command (e.g., `ls`), dash was
re-spawned from scratch instead of returning to the prompt. The parent
dash process was getting garbage PC/USP values after the child exited.

**Root cause:** After `vfork()`, the child runs in user mode sharing the
parent's address space. The child's SSP still points to the parent's
kstack. When the child makes a TRAP #0 (e.g., for `execve()`), the 68000
pushes a new exception frame onto the parent's kstack, overwriting the
parent's saved register state (PC, SR, USP). When the parent resumes
after `vfork_restore()`, it restores garbage registers from the
corrupted kstack frame.

**Fix:** After `syscall_dispatch()` returns in `crt0.S`, reload SP from
`curproc`'s kstack via a new `syscall_kstack_frame()` helper function.
This ensures each process always restores from its own copy of the
exception frame. After vfork, the child gets its own copy (from the
proc struct copy), and the parent's frame is preserved.

**Lesson:** On no-MMU systems with vfork, the child sharing the parent's
kstack means any TRAP/exception corrupts the parent's saved state. The
kernel must ensure each process restores from its own state, not from
whatever happens to be on the stack after the child ran.

### dash exit builtin crash (exraise/longjmp corruption)

**Date:** March 2026
**Symptom:** Running `exit` at the dash prompt caused a crash instead of
a clean exit.

**Root cause:** Dash's `exitcmd()` calls `exraise(EXEXIT)` which does a
`longjmp()` back to the main loop's exception handler. On Genix, this
longjmp chain crashed — likely due to `-msep-data` relocation affecting
the `jmp_buf` contents, or `jmp_buf` corruption from the vfork sharing
of address space.

**Fix:** Modified `apps/dash/main.c` to call `_exit()` directly from
`exitcmd()` instead of going through `exraise(EXEXIT)`. The kernel's
`do_exit()` handles all cleanup (fd closing, slot freeing, zombie state),
so the longjmp-based cleanup path is unnecessary.

**Lesson:** On constrained platforms with `-msep-data` and vfork, complex
control flow (longjmp chains through multiple stack frames) is fragile.
When a simpler path exists (direct `_exit()`), prefer it.

### Panic handler enhanced with fault frame decoding

**Date:** March 2026
**Enhancement:** The kernel's `panic_exception()` handler now decodes the
68000 group 0 (bus/address error) exception frame, displaying PC, SR, and
the faulting access address. Previously it just printed "KERNEL PANIC"
with no diagnostic information.

This made all the above dash debugging significantly easier — previously
a fault showed no useful information.

---

## 5. The Relocation Story

_How Genix binaries went from fixed-address to fully relocatable._

### The Problem

Initially, all binaries were linked at a fixed address (USER_BASE).
Workbench used 0x040000, Mega Drive used 0xFF9000. This required
separate linker scripts (`user.ld` and `user-md.ld`) and meant only
one user process could be loaded at a time.

### Research

Four binary format options were evaluated:

| Format | Header | Relocation | Ecosystem |
|--------|--------|------------|-----------|
| **Genix extended** | 32 bytes | Offset array | 34 custom apps |
| Fuzix a.out | 16 bytes | Kernel-applied | 143+ Fuzix utilities |
| bFLT v4 | 64 bytes | Offset list | uClinux apps |
| Raw ELF | ~52+ bytes | Full ELF relocs | Huge but complex |

See [docs/relocatable-binaries.md](docs/relocatable-binaries.md) for the
full 1,128-line research document covering FUZIX reference implementation,
PIC/GOT/XIP background, bFLT analysis, ROM XIP strategies, and the
evaluation.

### Decision: Extend Genix Header

Rather than migrating to Fuzix a.out, we extended the existing 32-byte
header. The `reserved[2]` fields became `text_size` and `reloc_count`.

Key properties:
- Zero extra RAM (relocation table loaded into BSS, processed, BSS zeroed)
- Split text/data aware (future-proofs for bank-swapping)
- ~80 lines of kernel code, ~100 lines of mkbin changes
- Proven mechanism (identical to FUZIX's relocator)

The Fuzix a.out header uses 16-bit size fields which would limit programs
to 64 KB segments. The Genix 32-bit fields future-proof for SRAM-extended
programs.

### Implementation Phases

See [docs/relocation-implementation-plan.md](docs/relocation-implementation-plan.md)
for the full implementation plan with code samples.

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Linker script (link at 0) | **Done** |
| 2 | mkbin relocation extraction | **Done** |
| 3 | Kernel header update | **Done** |
| 4 | Kernel relocation engine | **Done** |
| 5 | End-to-end validation | **Done** |
| 6 | Dynamic load address | **Done** |
| 7 | Split text/data and XIP | **Partial** (relocator done, loader pending) |

**Implementation deviations:**
- `reloc_count` instead of `reloc_size` (eliminates div-by-4 validation)
- No `GENIX_FLAG_RELOC` flag (all binaries are relocatable, flags stays 0)
- mkbin processes SHT_RELA (not SHT_REL) — correct for m68k-elf-ld

### Code Review Findings (March 2026)

A post-merge review found several issues, all now fixed:

1. **Missing runtime validation:** apply_relocations() didn't check
   offsets were within load_size or even-aligned. A corrupt binary could
   crash the kernel. Fixed: added bounds and alignment checks.

2. **mkbin odd-alignment was warning, not error:** On 68000, odd-aligned
   32-bit relocation is always a fatal bus fault. Changed to error.

3. **load_binary_xip() declared but never implemented:** Dangling
   declaration removed. Documentation updated.

4. **BSS zeroing incomplete:** When reloc table > BSS, stale data
   remained. Fixed: zero max(bss_size, reloc_bytes).

5. **Stale documentation:** binary-format.md listed Phase 6 as upcoming
   when it was done.

**Lesson:** Code review after merge catches things that testing misses.
All 5,123 host tests passed — but they only test well-formed binaries.
Defensive validation requires human review.

### Stale Artifacts Cleaned Up

After relocation was complete, old fixed-address artifacts were removed:
- Deleted `apps/user.ld` (workbench fixed-address linker script)
- Deleted `apps/user-md.ld` (Mega Drive fixed-address linker script)
- Removed `apps-md` build target from Makefile
- Updated stale comments referencing old linker scripts

---

## 6. Performance Analysis

_Analysis of optimization gaps between Genix and FUZIX assembly._

See [OPTIMIZATION_PLAN.md](OPTIMIZATION_PLAN.md) for the full document
with FUZIX source code references and implementation plans. None of
these optimizations have been implemented yet — all are future work.

### Summary of Findings

| Priority | Optimization | Estimated Speedup |
|----------|-------------|------------------|
| 1 | divmod.S DIVU.W fast path | 2-5x for all `/` and `%` |
| 2 | SRAM 16-bit I/O + block copy | ~20x for SRAM disk I/O |
| 3 | Assembly memcpy/memset (MOVEM.L) | 4x for 512-byte blocks |
| 4 | Pipe bulk copy (memcpy instead of byte loop) | 2-4x for pipelines |
| 5 | SRAM init zeroing | Boot-only, trivial |

**Already optimal:** VDP text output, VDP initialization, Saturn
keyboard, context switch (swtch), timer ISR, font loading, signed
division, buffer cache scan, inode cache scan.

**FUZIX optimizations not applicable:** swap_blocks (no swapping),
dofork frame building (simpler vfork), A5 register global (measuring
needed), install_vectors (direct in crt0.S), I-cache flush (68000 has
no I-cache).

---

## 7. Plan vs Reality

_Phase-by-phase comparison of what was planned vs what was built.
Originally written as `docs/status-review.md` (2026-03-10)._

### Phase 1: Workbench Emulator — Matches plan

Musashi-based 68000 SBC with UART, timer, and disk. No meaningful
divergence.

### Phase 2a: Single-tasking Kernel — Matches plan

Boot, console I/O, minifs filesystem, exec(), built-in shell.

### Phase 2b: Multitasking — Matches plan (after corrections)

**Plan:** Process table (8-16 slots), round-robin preemptive scheduling
via timer, vfork()+exec().

**Built:** 16-slot process table, preemptive timer-driven scheduling
(was cooperative initially, fixed), per-process kernel stacks (512 bytes),
vfork() implemented, spawn() as additional convenience.

**Binary format divergence:** Plan called for PIC ELF or bFLT. Genix
uses a custom 32-byte flat binary at a fixed load address (later made
relocatable). This means only one user process can be loaded at a time.

### Phase 2c-2e — Match plan

Pipes with blocking I/O, signals with user handlers, TTY line discipline.
All implemented as designed.

### Phase 2f: Libc + Utilities — Diverged beneficially

**Plan:** Use newlib, port Fuzix libc, port Fuzix sh.

**Built:** Custom minimal libc (~5 KB, 16 modules). 34 custom user
programs. Levee (vi clone) from Fuzix.

The plan's newlib goal was wrong — newlib is 50-100 KB, too large for
64 KB RAM. The custom libc is the right choice.

### Phase 3: Mega Drive Port — Matches plan

Reused Fuzix drivers as planned.

### Phase 4: Polish — Matches plan

/dev/null, interrupt keyboard, multi-TTY, NBUFS config, SRAM validation.

### FUZIX Design Philosophy Assessment

**What Genix does better than FUZIX:**
1. Readability: 5,650 lines vs 15,000+
2. Testing: 5,123+ host test assertions (FUZIX has none)
3. Workbench emulator for rapid iteration
4. STRICT_ALIGN emulator mode
5. Clean PAL separation
6. Automated testing ladder
7. Custom libc with safe divmod.S

**What FUZIX does better:**
1. Circular buffer optimization (adopted by Genix)
2. ISR-safe tty input (adopted by Genix)
3. Smaller p_tab structure
4. doexec register clearing (nice to have)

### Remaining Optional Work

| Item | Effort | Priority |
|------|--------|----------|
| Glob expansion in shell | Medium | Low |
| SRAM persistent filesystem | Medium | Low |
| Real shell (sh) | Medium | Low |
| Larger utilities (ed, diff, sort, sed) | High | Low |

---

## 8. Project Metrics

_Final status snapshot (March 2026)._

| Metric | Value |
|--------|-------|
| Kernel lines of code | ~5,650 |
| Host test assertions | 5,123+ |
| Host test files | 14 |
| Autotest cases | 31+ |
| User programs | 35 (including dash) |
| Libc modules | 18+ |
| Syscalls implemented | 32 |
| Platforms | 2 (workbench + Mega Drive) |

### Known Limitations

1. ~~**Single user memory space**~~ — resolved by Phase 6 (`-msep-data`
   + slot allocator). Processes now have independent RAM slots.

2. ~~**Sequential pipelines**~~ — resolved by Phase 6. Concurrent
   pipelines work with multiple processes in memory simultaneously.

3. ~~**Shell features**~~ — resolved by Phase C (dash port). dash provides
   POSIX scripting (if/then/else, for, case, functions), variable
   expansion, command substitution, and pipelines. No glob expansion yet
   (Genix libc lacks fnmatch/glob).

4. **kstack overflow has no guard** — the 512-byte kstack grows into
   proc struct fields. Consider adding a canary word for debug builds.

5. **Levee too large for Mega Drive** — 44 KB binary vs ~28 KB user
   space. Workbench-only until ROM XIP is implemented.

---

_End of project history. For active design decisions, see
[docs/decisions.md](docs/decisions.md). For the forward-looking
development plan, see [PLAN.md](PLAN.md)._
