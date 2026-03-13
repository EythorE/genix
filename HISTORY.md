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
with separated USP/SSP stacks.

**swtch() context switch:** Assembly function that saves callee-saved
registers (d2-d7, a2-a6), swaps stack pointers, and restores. Used by
both preemptive (timer ISR) and voluntary (sleep/waitpid) paths.

**proc_first_run trampoline:** When swtch() resumes a brand-new process
for the first time, RTS lands at proc_first_run, which pops the
user-mode state frame and RTEs to enter user mode.

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

**Signal design decisions:** One-shot handlers (classic signal() semantics —
handler resets to SIG_DFL after delivery). One handler per sig_deliver call
(multiple pending signals delivered one at a time via nested sigreturn).
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
   real carts, all EverDrive models, and BlastEm.

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
| User programs | 34 |
| Libc modules | 16 |
| Syscalls implemented | 32 |
| Platforms | 2 (workbench + Mega Drive) |

### Known Limitations

1. **Single user memory space** — all user programs load at USER_BASE;
   two processes can't coexist in memory. Pipelines execute sequentially.

2. **Sequential pipelines** — the 512-byte pipe buffer limits pipelines.
   Real concurrent pipelines require ROM XIP or memory partitioning.

3. **Shell features** — no glob expansion, no environment variable
   substitution, no background jobs.

4. **kstack overflow has no guard** — the 512-byte kstack grows into
   proc struct fields. Consider adding a canary word for debug builds.

5. **Levee too large for Mega Drive** — 44 KB binary vs ~28 KB user
   space. Workbench-only until ROM XIP is implemented.

---

_End of project history. For active design decisions, see
[docs/decisions.md](docs/decisions.md). For the forward-looking
development plan, see [PLAN.md](PLAN.md)._
