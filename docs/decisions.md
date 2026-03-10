# Design Decisions and Project History

A chronological record of major design decisions, reversals, pain points,
and lessons learned during Genix development.

---

## Why Genix Exists

FUZIX is a multi-user Unix clone with 30+ platform ports, deep legacy
code, and abstractions for hardware we don't have (banked memory, floppy
disks, RS-232). The Mega Drive port bolts onto this. The result: a
forking bug nobody can find because the interactions between the 68000
platform code and the generic kernel are too numerous and too subtle.

We don't need multi-user, login, file ownership, or 30 platform ports.
We need a small, single-user OS for the 68000 that one person can read
in an afternoon. Writing ~3000 lines of new kernel code is cheaper than
understanding 15,000+ lines of someone else's kernel.

---

## Decision: Workbench Emulator First

**Date:** Project start
**Status:** Done, proven correct

Developing directly on the Mega Drive is painful: VDP-only output, no
UART, BlastEm requires Xvfb or a display, every debug cycle involves
screenshots. So we built a trivial 68000 SBC in software using Musashi
(the same CPU core MAME uses).

The workbench gives us terminal I/O, instant startup, and printf
debugging. `make run` goes from source to running kernel in ~2 seconds.

**Lesson:** The workbench cut our iteration time from minutes to seconds.
Worth the ~400 lines of emulator code.

See [docs/emulator.md](emulator.md) for details.

---

## Decision: No fork(), vfork()+exec() Only

**Date:** Project start
**Status:** Firm

The 68000 has no MMU. fork() on a no-MMU system means copying the entire
process memory, which is fragile and expensive on a 64 KB system. FUZIX's
fork() on the Mega Drive was the root cause of an unfindable bug.

vfork()+exec() is the proven approach for no-MMU systems (uClinux uses
it). The child shares the parent's address space until exec() — no copy
needed. The libc vfork stub saves the return address in a register (not
on the stack) because the child would corrupt the parent's stack frame.

**Trade-off:** Programs that depend on fork() semantics (modifying memory
between fork and exec) won't work. In practice, nearly all Unix programs
use the fork→exec pattern without modification, so this is fine.

---

## Decision: Custom Filesystem (minifs)

**Date:** Project start
**Status:** Done, working

We considered using FUZIX's filesystem but it was tightly coupled to the
FUZIX kernel. minifs is a classic Unix inode filesystem: superblock,
inode bitmap, data bitmap, inode table, data blocks. Simple, educational,
and exactly the features we need.

See [docs/filesystem.md](filesystem.md) for the on-disk layout.

---

## Decision: Binary Format — Genix Flat Binary (Stepping Stone)

**Date:** Phase 2a
**Status:** Current, planned migration to Fuzix a.out

We started with a custom 32-byte flat binary header ("GENX" magic) for
single-tasking exec(). It's simple: header + text+data blob loaded at a
fixed address (USER_BASE), BSS zeroed, entry point jumped to.

**Planned migration:** When we add multitasking and relocation, we'll
switch to the Fuzix a.out format (16-byte header with relocation
support). This gives us access to 143+ Fuzix utilities compiled for
68000 systems with 64 KB RAM. The formats are similar enough that the
loader change is straightforward.

**Lesson:** Design choices are cheap. Starting with the simpler format
let us get exec() working fast. Migrating later is a known cost.

See [docs/binary-format.md](binary-format.md) for the current format.

---

## Decision: Fuzix a.out for Multitasking (Planned)

**Date:** Plan phase
**Status:** Not yet implemented

We evaluated four binary formats:

| Format | Ecosystem | Complexity | Fits 64 KB? |
|--------|-----------|------------|-------------|
| Fuzix a.out | 143+ utilities | Low (16-byte header) | Yes |
| bFLT v4 | uClinux apps | Medium (64-byte header) | Barely |
| Raw ELF | Huge (in theory) | High (complex loader) | No |
| Flat binary | Custom only | Trivial | Yes |

Fuzix a.out wins: right-sized header, kernel-applied relocations (~30
lines of C), and the largest collection of Unix programs designed for
exactly this scale.

---

## Decision: Fuzix libc (Planned)

**Date:** Plan phase
**Status:** Not yet implemented (currently using minimal hand-written stubs)

| Library | Size | POSIX Coverage | Fits 64 KB? |
|---------|------|---------------|-------------|
| Fuzix libc | ~5-10 KB | Good enough | Yes |
| newlib | ~50-100 KB | Excellent | Barely |
| uClibc-ng | ~100-200 KB | Near-complete | No |
| Hand-written | ~1 KB | Minimal | Yes |

We're using hand-written stubs now (~15 syscalls). When we port Fuzix
utilities, we'll port the Fuzix libc — it provides stdio, stdlib,
string, ctype, termios, and everything the 143 utilities need at ~5 KB.

---

## Pain Point: Toolchain (68020 Instructions in libgcc)

**Date:** Early development
**Status:** Worked around, documented

The distro `m68k-linux-gnu-gcc` (from apt) defaults to 68020 and its
`libgcc.a` contains 68020-only instructions like `BSR.L`. Code compiles
and links without errors, then hangs on real 68000 hardware when a
division routine in libgcc fires an illegal instruction.

**Workaround:** Pass `-m68000` to all compilations. Provide our own
`divmod.S` with pure 68000 shift-and-subtract division. Don't link the
distro's libgcc.

**Proper fix:** Build `m68k-elf-gcc` from source with `--with-cpu=68000`.
This gives a compiler and libgcc that only emit base 68000 instructions.

**Lesson:** The toolchain bit us hard. We now have extensive
documentation in [docs/toolchain.md](toolchain.md) and warnings in
CLAUDE.md. Every developer should read these before touching compiler
flags.

---

## Pain Point: Declaration Drift

**Date:** Early development
**Status:** Fixed, codified as a rule

We had `pal_halt()` declared in both `kernel.h` and `pal.h` with
slightly different signatures. The code compiled fine but the linker
resolved to the wrong version on one platform.

**Rule (now in CLAUDE.md):** Single source of truth for declarations.
Never duplicate function prototypes across headers. Use `#include`.
Duplications drift apart silently.

---

## Decision: Emulator Exit Mechanism

**Date:** Early development
**Status:** Settled

The journey:

1. **Ctrl+C** — Doesn't work because the terminal is in raw mode
   (ISIG disabled), so SIGINT never fires on the host.
2. **Ctrl+]** — Works (like telnet/QEMU convention), but it's a
   host-side escape that bypasses the kernel.
3. **Power-off register** — Added MMIO register at `0xF30000`. The
   kernel's `pal_halt()` writes to it; the emulator exits gracefully.

**Final design:** Both mechanisms coexist:
- **Ctrl+]** = emergency host escape (always works, even if kernel hangs)
- **`halt` command** = clean shutdown via power-off register
- **EOF on stdin** = clean exit when input is piped

**Lesson:** Proper layering matters. The kernel shouldn't know how the
platform halts — it calls `pal_halt()` and the PAL does the right thing
(power-off register on workbench, `STOP #0x2700` on Mega Drive).

---

## Decision: Mega Drive as Primary Target

**Date:** Formalized during documentation phase
**Status:** Firm

The workbench emulator is convenient but it's not the point. Every
feature must work on real Mega Drive hardware: 64 KB main RAM, 7.67 MHz,
no MMU, optional SRAM.

This means:
- Memory layout must be modular (PAL-provided, not hardcoded)
- Always verify `make megadrive` alongside `make kernel`
- The workbench's 1 MB RAM catches logic bugs but not memory pressure
- Real hardware catches timing bugs that emulators miss

---

## Decision: SRAM Is Optional

**Date:** Documentation phase
**Status:** Firm

The system must boot and run user programs on 64 KB main RAM alone.
Different cartridge configurations (no SRAM, 32 KB, 512 KB) require
different memory layouts. SRAM provides persistent storage and extended
RAM for larger programs, but is not required for basic operation.

**Practical finding:** The standard Sega mapper (`0xA130F1 = 0x03`)
works on real cartridges, all EverDrive models (in traditional mode),
and BlastEm — because the Genix ROM is < 2 MB. No special mapper code
needed.

See [docs/megadrive.md](megadrive.md) for cartridge configurations.

---

## Pain Point: Division on the 68000

**Date:** Ongoing
**Status:** Codified as rules

The 68000 has NO 32/32 hardware divide. Only `DIVU.W` (32/16 -> 16-bit
quotient, 76-136 cycles). Full 32-bit division goes through software
(~300-600 cycles).

Every `/` and `%` in kernel code must be annotated:
- Powers of 2: use `>> n` and `& (n-1)`
- 16-bit constant divisors: OK (DIVU.W handles them)
- 32-bit runtime divisors in hot paths: avoid at all costs

We replaced `cursor / COLS` with separate row/col tracking (addition
instead of division) and use 256-byte circular buffers with `uint8_t`
indices (natural wrap, no modulo).

See [docs/68000-programming.md](68000-programming.md).

---

## Decision: Testing Ladder

**Date:** Developed through experience
**Status:** Firm, codified

We learned the hard way that different build targets catch different
bugs:

1. **`make test`** — Host unit tests. Catches logic bugs. No cross-compiler.
2. **`make kernel`** — Cross-compilation. Catches ABI mismatches, missing declarations.
3. **`make run`** — Workbench emulator. Catches runtime bugs, alignment issues.
4. **`make test-md`** — Headless BlastEm. Catches address errors, illegal instructions.
5. **`make megadrive` + BlastEm** — Interactive. Catches VDP, keyboard, SRAM bugs.
6. **Real hardware** — Catches timing, TMSS, mapper, Z80 bus conflicts.

A change can build fine for workbench but fail for Mega Drive (different
linker scripts, different memory layout). Always run both.

---

## Decision: spawn() Instead of vfork()+exec() (March 2026)

**Date:** Phase 2b implementation
**Status:** Current

The original plan was vfork()+exec() — the standard no-MMU pattern where
the child shares the parent's address space until exec(). We implemented
`vfork_save`/`vfork_restore` (setjmp/longjmp-style assembly) but
discovered a fundamental problem on the 68000:

**The crash:** vfork() returns twice (once in parent, once in child).
The child then calls do_exec(), which pushes stack frames. When
exec_leave() fires (child exits), vfork_restore jumps back to the
parent's saved SP — but the child's exec() stack frames have
**overwritten** the parent's stack. The parent resumes into garbage.

This is the same class of bug that made FUZIX's fork() unfindable.

**Solution:** `do_spawn()` — a combined vfork+exec that never returns
to the child. The parent calls `do_spawn(path, argv)`, which:
1. Allocates a child process entry
2. Copies parent state (FDs, cwd)
3. Switches `curproc` to child
4. Calls `do_exec()` directly (blocks until child exits)
5. Makes child a zombie, switches back to parent
6. Returns child PID (parent reaps with `do_waitpid()`)

This is single-tasking: the parent blocks while the child runs. But it's
safe because there's no "return twice" and no stack overlap. When we add
preemptive scheduling later, spawn() becomes the entry point for creating
new processes — we just won't block the parent.

**Trade-off:** No POSIX vfork() semantics (child can't modify parent's
address space before exec). In practice this doesn't matter — the only
portable use of vfork() is vfork→exec anyway.

---

## Decision: Pipes — Circular Buffer, No Blocking (March 2026)

**Date:** Phase 2c implementation
**Status:** Current

Pipes use a 512-byte circular buffer with `uint8_t` head/tail indices
(natural wrap at 256 — wait, we use `uint16_t` indices with `% PIPE_SIZE`
masking). Up to 4 pipes (`MAXPIPE`).

**Current limitation:** Since the system is single-tasking, pipes can't
block. `pipe_read()` returns 0 (EOF) when the buffer is empty and the
write end is closed. `pipe_write()` fills up to available space.

**Why 512 bytes:** Large enough for shell pipelines (`ls | wc`), small
enough to not eat into the 64 KB main RAM. The pipe table (4 × ~520
bytes) uses ~2 KB total.

**When we add preemptive scheduling:** `pipe_read()` on an empty pipe
with open write end will sleep (put process in P_WAIT). `pipe_write()`
on a full pipe will sleep. This is the standard Unix pipe model.

---

## Decision: Preemptive Round-Robin Scheduler (Planned)

**Date:** Plan phase
**Status:** Not yet implemented

Timer-driven (VBlank at 50/60 Hz on Mega Drive, 100 Hz on workbench).
Context switch via `MOVEM.L` — saves 15 registers in one instruction.
Simple, fair, proven. ~50 lines of C + ~30 lines of assembly.

**Alternative considered:** Cooperative scheduling. Rejected because it
requires every program to yield voluntarily, which breaks POSIX
expectations (a busy loop would hang the system).

**Prerequisite:** Process table and spawn/waitpid are done (Phase 2b).
The scheduler needs per-process saved register state and a ready queue,
both of which exist in the process table.

---

## Decision: Own VDP Driver, Not SGDK (March 2026)

**Date:** March 2026
**Status:** Decided

We evaluated extracting VDP modules from SGDK (Sega Genesis Development
Kit) to provide a userspace graphics library. SGDK is the most capable
Mega Drive SDK, with sprite engines, DMA queues, palette fading, and
a software framebuffer.

**Why we rejected it:** SGDK is a bare-metal framework that assumes
exclusive ownership of the CPU, interrupts, memory, and all hardware.
Running it under an OS would require replacing its interrupt model,
memory allocator, boot sequence, Z80 control, and timer system with
shim layers. The result would be complex, fragile, and contrary to
Genix's "small is beautiful" philosophy.

**What we're doing instead:** A kernel VDP driver that exposes the
hardware through the standard device interface (open/close/read/write/
ioctl). Userspace programs access VDP capabilities through a small C
library (`libgfx`) that wraps the kernel ioctls. This follows the same
pattern Fuzix used: `devvdp.c` in the kernel, userspace apps on top.

The kernel driver provides the minimum abstraction needed to safely
multiplex VDP access between processes (exclusive open, state save/
restore on context switch). The userspace library provides the
programmer-friendly API. This separation keeps the kernel small and
lets the graphics ABI evolve without kernel changes.

---

## Project Status (March 2026)

The project phases are tracked below and in the individual docs.

| Phase | Description | Status |
|-------|-------------|--------|
| **Phase 1** | Workbench emulator (Musashi SBC) | **Complete** |
| **Phase 2a** | Kernel core + binary loading + single-tasking exec | **Complete** |
| **Phase 2b** | Multitasking (spawn, waitpid, process table) | **Complete** |
| **Phase 2c** | Pipes and I/O redirection | **Complete** (pipes done, redirection planned) |
| **Phase 2d** | Signals and job control | **Next** |
| **Phase 2e** | TTY subsystem (port Fuzix tty.c) | Planned |
| **Phase 2f** | Fuzix libc + utilities | Planned |
| **Phase 3** | Mega Drive port (PAL drivers from Fuzix) | **Complete** |
| **Phase 4** | Polish (interrupt keyboard, multi-TTY, /dev/null) | Planned |

**What works today:**
- Kernel boots on workbench emulator and Mega Drive (BlastEm + real hardware)
- Filesystem (minifs) with read/write/create/delete/rename/mkdir/rmdir
- Indirect blocks in both kernel and mkfs (files > 12 KB work)
- exec() loads and runs user programs (hello, echo, cat, wc, head, levee) from disk
- Built-in debug shell with ls, cat, echo, mkdir, mem, help, halt
- Process table (16 slots) with spawn(), waitpid(), exit()
- Pipes (512-byte circular buffer, up to 4 pipes)
- Shell `spawn` and `pipe` commands for launching programs and piping output
- Terminal raw mode via termios (tcgetattr/tcsetattr → ioctl)
- Full libc: stdio (FILE*), stdlib (malloc/free), string, ctype, termios
- Levee (vi clone) runs on workbench emulator with ANSI terminal support
- 283 host tests passing (string, mem, exec, proc), plus automated guest tests
- Both workbench and Mega Drive builds clean
- Saturn keyboard input on Mega Drive, UART on workbench
- SRAM works with standard Sega mapper on all tested targets

**What's next:** Preemptive scheduling (timer-driven context switch) to
allow true multitasking. Then signals and job control, I/O redirection,
and TTY subsystem to get a usable interactive Unix environment.

---

## Bugs and Lessons Learned

A catalog of bugs that cost significant debugging time. Each entry
explains the root cause and how to avoid recurrence.

### Bug: JSR Corrupts User Stack Layout (March 2026)

**Symptom:** `exec /bin/hello` crashed with address error on Mega Drive.
Hello worked once, then the system hung on subsequent exec() calls.

**Root cause:** `exec_asm.S` used `JSR (%a0)` to enter the user program.
JSR pushes a return address onto the user stack, but crt0.S expects the
first value on the stack to be `argc`. The return address overwrites argc,
corrupting the entire argv/envp layout.

**Fix:** Use `JMP (%a0)` instead of `JSR`. The user program never
"returns" — it calls `_exit()` via TRAP #0, which reaches `exec_leave()`
to restore kernel context.

**Lesson:** On the 68000, understand the difference between JMP (pure
jump) and JSR (pushes return address). When transitioning to user mode,
the stack layout is a contract — any extra pushes break it.

### Bug: Unaligned Stack Array on 68000 (March 2026)

**Symptom:** `hello` crashed with address error when accessing a local
`char buf[]` array that happened to end up at an odd address.

**Root cause:** A `char buf[13]` on the stack followed by a `write()`
call. The compiler placed `buf` at an odd stack offset. When `write()`
tried to read from it as a word-aligned address, the 68000 faulted.

**Fix:** Changed the buffer size to an even number (`char buf[14]`)
and added a comment. More generally: always make local buffers
even-sized on the 68000.

**Lesson:** The 68000 faults on word/long access at odd addresses.
This includes compiler-generated code that copies structs or buffers
using word moves. Local arrays should always be even-sized.

### Bug: USER_BASE/USER_TOP Hardcoded (March 2026)

**Symptom:** User programs loaded at workbench addresses (0x040000) on
the Mega Drive, writing to addresses outside the 64 KB main RAM. This
caused silent memory corruption (no bus error because the ROM was mapped
there).

**Root cause:** USER_BASE and USER_TOP were compile-time constants in
`kernel.h`, not platform-provided values. The Mega Drive needs different
addresses (0xFF8000-0xFFFE00) than the workbench (0x040000-0x0F0000).

**Fix:** Made USER_BASE/USER_TOP global variables set from PAL functions
at boot: `pal_user_base()` and `pal_user_top()`.

**Lesson:** Memory layout must be platform-provided, not hardcoded.
Different targets (workbench, Mega Drive, different cartridge configs)
have different address maps. This is what the PAL layer is for.

### Pain Point: BlastEm Version Differences (March 2026)

**Symptom:** `make test-md` passes on some machines but fails with exit
code 1 on others, even when the ROM is correct.

**Root cause:** The `-g` flag passed to BlastEm behaves differently
across versions. Some versions don't support it at all and exit with
error. The test target interprets any non-0/non-124 exit code as failure.

**Fix:** Remove the `-g` flag from test-md (it was unnecessary for
headless testing). Also: when a test fails, the first thing to check
is whether it's BlastEm failing vs. the ROM failing. Adding `2>&1`
capture or checking BlastEm stderr helps distinguish.

**Lesson:** Headless test targets that depend on emulator CLI flags
must be tested across BlastEm versions. Keep the flags minimal.
Document which BlastEm version was tested.

### Decision: BlastEm 0.6.3-pre and Headless `-b` Mode (March 2026)

**Date:** March 2026
**Status:** Current

Upgraded from BlastEm 0.6.2 stable to 0.6.3-pre nightly (`884de5ef1263`,
Feb 26, 2026). The nightly adds VDP FIFO/CRAM accuracy fixes and a
built-in PNG screenshot writer (`save_png` in `png.c`, no libpng dependency).

**Key discovery: `-b N` flag.** BlastEm's `-b N` runs N frames then exits
with no display at all — truly headless, no Xvfb needed. Exit code 0 means
the ROM ran without crashing; nonzero means crash/error. This eliminated
the entire Xvfb + timeout + SIGKILL dance from `test-md` and `test-md-auto`.

**Before:** `Xvfb :57 ... & timeout -k 3 10 blastem rom.bin` — required
Xvfb, headless BlastEm config (`gl off`), SIGTERM/SIGKILL workarounds,
exit code interpretation (124=timeout, 137=SIGKILL, both treated as pass).

**After:** `blastem -b 300 rom.bin` — no dependencies, clean exit code,
~5 seconds at 60fps. Simpler, faster, more reliable.

**Screenshot limitation:** The `-b` flag is truly headless (no window, no
renderer). BlastEm's native screenshot feature (`p` key → `ui.screenshot`
→ `render_save_screenshot()`) requires an SDL window because it captures
the framebuffer during `process_framebuffer()`. We investigated every
approach to trigger it programmatically under Xvfb:

- **XTest** (Python ctypes + libXtst): SDL2 doesn't receive XTest events
- **xdotool** (key, keydown/keyup, type, --window, --clearmodifiers): same
- **LD_PRELOAD shim** to disable XInput2 (force core X11 events): no effect
- **GL on vs off**: no difference
- **BlastEm debugger** (`-d`/`-D`): no screenshot command exists
- **GDB remote** (`-D`): no `qRcmd`/monitor command support

**Root cause:** SDL2 uses XInput2 (`XI_RawKeyPress`) for keyboard input on
X11. XTest and xdotool inject events into the core X11 protocol, which
XInput2's raw event delivery bypasses entirely. This is a well-known SDL2
limitation under Xvfb — there's no way to simulate keypresses from outside.

**Screenshot test strategy:** `test-md-screenshot` uses Xvfb + `scrot`
(X11 screen capture tool) to capture the BlastEm window. This captures
the rendered VDP output at the window's display resolution (not the native
320×224). It works reliably but requires `Xvfb`, `xdotool` (for window
focus), and `scrot`.

**Tested combinations:**
- `blastem -b 300` with good ROM: exit 0
- `blastem -b 60` with bad ROM (random bytes): exit 1
- `blastem -b 600` with AUTOTEST ROM: exit 0

### Bug: libgcc BSR.L in User Programs (March 2026)

**Symptom:** `wc` (which uses `/` and `%` operators) crashed with an
illegal instruction exception. Other programs (hello, echo, true) worked
fine because they don't use division.

**Root cause:** The toolchain's `libgcc.a` contains `__umodsi3` (modulo)
implemented with `BSR.L` (opcode `61FF`) — a **68020-only instruction**.
The kernel had its own safe `divmod.S` with pure 68000 shift-and-subtract
division, but user programs were linking against the system's libgcc.

**Fix:** Added `divmod.S` to `libc/libc.a` via a symlink to
`kernel/divmod.S`. Since libc is linked before libgcc
(`libc.a $(LIBGCC)`), the libc versions of `__udivsi3`, `__umodsi3`,
`__divsi3`, `__modsi3` take priority over the buggy libgcc versions.

**Lesson:** On a 68000 system, **every library** that might contain
division must be checked for 68020 instructions. The `-m68000` flag only
affects code *you* compile — it does NOT fix pre-built libraries like
libgcc. Either provide your own division routines (like divmod.S) or
build the entire toolchain with `--with-cpu=68000`.

**Detection:** The STRICT_ALIGN emulator mode caught the unaligned
accesses, but not the illegal instruction. The crash manifested as
"KERNEL PANIC: exception" with a garbage PC address. When you see
an exception with a PC outside valid memory, suspect:
1. 68020 instructions in libgcc (check with `objdump -d`)
2. Stack corruption (check alignment, buffer sizes)
3. Dangling function pointers

### Bug: Indirect Block bmap() on Big-Endian (March 2026)

**Symptom:** Levee binary (44 KB) loaded only 28640 of 44720 bytes. The
first 12 KB (direct blocks) loaded correctly, then garbage appeared.

**Root cause:** `bmap()` in `kernel/fs.c` read indirect block entries
using byte-level decomposition on a `uint16_t*`:
```c
/* BROKEN — double-indexed into uint16_t array */
uint16_t *ptrs = (uint16_t *)ib->data;
uint16_t blk = (ptrs[bn * 2] << 8) | ptrs[bn * 2 + 1];
```
This treated the `uint16_t*` as a `uint8_t*`, reading 4 bytes per entry
instead of 2. On big-endian 68000, `uint16_t` values in memory are
already in native byte order, so no decomposition is needed.

**Fix:** Simply index the `uint16_t*` directly:
```c
uint16_t *ptrs = (uint16_t *)ib->data;
uint16_t blk = ptrs[bn];
```

**Lesson:** On big-endian systems, don't manually byte-swap data that's
already stored in native order. The byte-swap pattern `(p[0] << 8) | p[1]`
is correct when reading from a `uint8_t*` buffer (as mkfs does on the
little-endian host). But in the kernel, the buffer cache stores data in
the CPU's native byte order (big-endian), so `uint16_t*` indexing works
directly. This is a common trap when porting between host tools and
target kernel code.

### Bug: mkfs Lacked Indirect Block Support (March 2026)

**Symptom:** Files larger than 12 KB (12 direct blocks × 1024 bytes)
were silently truncated in the filesystem image.

**Root cause:** `tools/mkfs.c` only wrote direct block pointers
(`di.direct[0..11]`). There was no code to allocate or populate an
indirect block for data beyond the 12th block.

**Fix:** Added indirect block allocation and big-endian uint16_t
writing to mkfs.c's `add_file()` function. The indirect block stores
block numbers as big-endian uint16_t entries (matching the 68000 kernel's
expectations).

**Lesson:** Test with files larger than 12 KB early. The filesystem
code path for indirect blocks is completely different from direct blocks,
and if it's never exercised, bugs hide indefinitely. Levee (44 KB) was
the first file large enough to trigger this.

### Meta-Lesson: Workbench vs. Mega Drive Divergence

The workbench emulator (Musashi) and the Mega Drive (BlastEm/real HW)
differ in critical ways:

| Behavior | Workbench (Musashi) | Mega Drive (68000) |
|----------|--------------------|--------------------|
| Unaligned access | Silently works | Address error fault |
| Jump to odd address | Silently works | Address error fault |
| Access unmapped memory | Returns 0 / silently writes | Bus error or ROM/RAM overlay |
| Stack at odd address | Silently works | Address error on push/pop |

**Rule:** Always verify `make megadrive` and `make test-md` alongside
workbench testing. The workbench catches logic bugs; the Mega Drive
catches alignment and address bugs that Musashi silently ignores.

**Future improvement:** Add alignment checking to the workbench
emulator (Musashi hooks) so it catches the same bugs the real 68000
does.

---

1. **Fuzix a.out 32-bit sizes?** The 16-byte header has 16-bit size
   fields (max 64 KB per segment). Fine for 64 KB main RAM, but if we
   use SRAM for larger programs (up to 512 KB), we may need 32-bit
   sizes. Current plan: start with 64 KB limit, extend later.

2. **Shell: Fuzix sh or custom?** Currently using a minimal built-in
   shell. Plan: custom minimal shell first, port Fuzix sh when we need
   full job control.

3. **brk() vs mmap()?** In single-tasking, brk() works fine. In
   multitasking, it's problematic if another process is after ours.
   Plan: brk() with careful load ordering, revisit if it breaks.

---

## Levee (vi Clone) Port — Lessons Learned (March 2026)

**Status:** Working on workbench emulator. Too large for Mega Drive
(44 KB binary vs ~31 KB available user space).

Successfully ported levee from
[Fuzix](https://github.com/EythorE/FUZIX/tree/megadrive) to Genix.
This was a significant integration test that exercised the full stack:
libc (FILE*, malloc, termios, string, ctype), kernel (ioctl, sbrk,
indirect blocks), and the binary loader.

### What was needed to make it work

1. **C library additions**: `ctype.c` (isalpha, isdigit, etc.),
   `stdlib.c` (malloc/free via sbrk, atoi, getenv), `stdio.c`
   (FILE*, fopen/fclose/fgets/fprintf), `termios.c` (tcgetattr/
   tcsetattr wrapping ioctl).

2. **Header files**: `<ctype.h>`, `<termios.h>`, `<fcntl.h>`,
   `<sys/stat.h>` — all minimal stubs providing just enough for
   levee's needs.

3. **Kernel termios**: Console raw mode via `con_raw` flag in
   `kernel/dev.c`, toggled by TCGETS/TCSETS ioctl.

4. **Filesystem indirect blocks**: Both in mkfs (host tool) and
   kernel bmap(). Levee at 44 KB needs ~32 indirect blocks.

5. **Missing source file (ucsd.c)**: Not immediately obvious, but
   levee depends on `moveleft()`, `moveright()`, `fillchar()`, and
   `lvscan()` from `ucsd.c`. Missing symbol errors at link time are
   the clue.

### Pain points

- **ucsd.c not obvious**: The Fuzix levee source tree has ucsd.c but
  the Makefile doesn't list it in an obvious SRCS variable. You have
  to trace undefined symbols to find it.

- **Warning-heavy code**: Levee is K&R-style C from the 1980s. Needs
  extensive `-Wno-*` flags to compile cleanly with modern GCC:
  `-Wno-implicit-int`, `-Wno-return-type`, `-Wno-parentheses`,
  `-Wno-implicit-function-declaration`, `-Wno-char-subscripts`.

- **Mega Drive too small**: Levee produces a ~44 KB binary. The Mega
  Drive has ~31 KB of user space (64 KB RAM minus ~25 KB kernel minus
  stack). Levee is workbench-only unless we add SRAM-based program
  loading or significantly reduce binary size.

- **Conflicting open() declarations**: `fcntl.h` declared
  `open(const char *, int, ...)` (variadic) while `unistd.h` had
  `open(const char *, int)` (non-variadic). GCC errors on the
  mismatch. Both must be variadic.

### What this validates

The levee port proves that Genix's libc, kernel, and filesystem are
mature enough to run real Unix software — not just toy programs. The
full stack (raw terminal I/O, file I/O, dynamic memory, ANSI escape
codes) works correctly on the workbench emulator.

---

## Decision: User Mode + Per-Process Kernel Stacks

**Date:** 2026-03-09
**Status:** Done, all tests pass

User programs now run in 68000 user mode (S=0 in SR) with separated
USP/SSP stacks. This is required infrastructure for preemptive
scheduling — the timer ISR needs to know if it interrupted kernel or
user code (by checking the S bit in the saved SR on the exception frame).

### Architecture

- Each process gets a 512-byte kernel stack (`kstack[]` in `struct proc`)
- `exec_enter()` saves callee-saved regs on the main supervisor stack,
  switches SSP to the process's `kstack_top`, sets USP from the argument,
  then enters user mode via RTE with SR=0x0000
- TRAP #0 handler saves all user registers (d0-d7/a0-a6) + USP on the
  kstack, pushes syscall args, calls `syscall_dispatch`, restores USP
  and regs, then RTEs back to user mode
- `exec_leave()` abandons the kstack and longjmps back to `exec_enter`'s
  caller via `saved_ksp`

### Pain points discovered

#### Kernel stack overflow corrupts proc struct (BEWARE)

The kstack is at the END of the proc struct and grows downward. If a
syscall's C call chain is too deep, the stack overflows into `fd[]`,
`cwd`, `vfork_ctx`, etc. — silently corrupting process state.

**Symptom:** `do_pipe()` returned -EMFILE (-24) even though only 3 fds
were open. Debug output showed all 16 fd[] slots contained garbage
values like `0xa4760000`.

**Root cause:** 256 bytes was not enough for the deepest syscall path:
TRAP frame (6 + 60 + 4 = 70 bytes) + syscall args (20) + JSR (4) +
`syscall_dispatch` → `sys_write` → `con_write` → `kputc` →
`pal_console_putc` (6 levels of C calls, ~120 bytes). Total ~214 bytes
leaves only 42 bytes of headroom — not enough for GCC's callee-saved
register saves.

**Fix:** Increased KSTACK_SIZE from 256 to 512 bytes. This costs 4 KB
more RAM for the 16-entry process table (8 KB total for kstacks).

**Lesson:** When embedding a stack inside a struct, overflows corrupt
adjacent fields silently. The 68000 has no stack guard pages. Consider
adding a stack canary (magic word at kstack[0]) for debug builds.

**Future concern:** With preemptive scheduling, a timer ISR can fire
while a syscall is in progress on the kstack, nesting another exception
frame + ISR register saves (~40 bytes). The "don't preempt kernel mode"
rule prevents this. If we ever need to preempt kernel code, kstacks
must grow larger.

#### Mega Drive USER_BASE collision

Adding 512-byte kstacks pushed kernel BSS from ~25 KB to ~35 KB,
past the old USER_BASE of 0xFF8000 (32 KB from RAM start). User
programs loaded at 0xFF8000 would overlay kernel data.

**Fix:** Bumped Mega Drive USER_BASE to 0xFF9000 (36 KB from RAM
start). Updated both `user-md.ld` and `pal_user_base()`. This leaves
~27.5 KB for user programs (was ~31 KB). Also needed `-n` (nmagic)
linker flag to prevent ELF segment page alignment from inflating
binaries.

**Lesson:** `user-md.ld` link address and `pal_user_base()` return
value MUST match exactly. The binary format uses absolute entry points.
Any mismatch causes `exec_validate_header` to reject the binary with
-ENOEXEC. When we add relocation, entry points should become offsets.

### What changed

| File | Change |
|------|--------|
| `kernel/kernel.h` | Added `kstack[128]`, `ksp` to proc; KSTACK_SIZE=512 |
| `kernel/exec_asm.S` | `exec_enter` enters user mode via RTE, sets USP, uses kstack |
| `kernel/exec.c` | Passes `proc_kstack_top(curproc)` to `exec_enter` |
| `kernel/crt0.S` | TRAP #0 saves d0-d7/a0-a6 + USP on kstack |
| `pal/megadrive/crt0.S` | Same TRAP #0 changes as workbench |
| `pal/megadrive/platform.c` | USER_BASE bumped to 0xFF9000 |
| `apps/user-md.ld` | Link address bumped to 0xFF9000 |
| `apps/Makefile` | Added `-n` linker flag to disable page alignment |

---

## Decision: Kernel Context Switch via swtch() + Async Spawn

**Date:** 2026-03-10
**Status:** Done, all tests pass

### Problem

The original `do_spawn()` was synchronous: it called `do_exec()` which
called `exec_enter()`, blocking the parent until the child exited. This
meant no true concurrent execution — the scheduler infrastructure
(preemptive timer ISR, per-process kstacks) existed but was never
actually switching between processes.

For blocking pipes, waitpid, and eventually signals, we need the parent
and child to run concurrently. The parent needs to sleep while waiting,
and the child needs to be independently schedulable.

### Design: Two-Level Context Switching

1. **`swtch(old_ksp, new_ksp)`** — Assembly function that saves
   callee-saved registers (d2-d7, a2-a6) on the current stack, saves
   SP to `*old_ksp`, loads `new_ksp` as SP, restores callee-saved
   registers, and RTS. This is the core kernel context switch — used
   by both preemptive (timer ISR) and voluntary (sleep/waitpid) paths.

2. **`proc_first_run`** — Trampoline for a brand-new process. When
   `swtch()` resumes a process for the first time, RTS lands here.
   It pops the user-mode state frame (USP, d0-d7/a0-a6, SR, PC)
   and does RTE to enter user mode.

3. **`proc_setup_kstack(proc, entry, user_sp)`** — Builds the initial
   kstack frame for a new process:
   ```
   [swtch frame]    d2-d7/a2-a6 (zero), retaddr → proc_first_run
   [user state]     USP, d0-d7/a0-a6 (zero)
   [exception]      SR=0x0000 (user mode), PC=entry
   ```
   Total: 118 bytes. With 512-byte kstacks, 394 bytes remain for
   syscall call chains.

### How It Works

**Timer ISR (preemptive):**
ISR saves user state on kstack → calls `schedule()` → schedule calls
`swtch()` → saves callee-saved + SP → loads new SP → restores
callee-saved → RTS to schedule → schedule returns to ISR → ISR
restores user state → RTE to user mode.

**Voluntary yield (sleep/waitpid):**
Kernel code sets state to P_SLEEPING → calls `schedule()` → schedule
calls `swtch()` → saves kernel state → loads new process → new process
runs. When the sleeping process is woken and scheduled again, swtch
returns → schedule returns → kernel code continues where it left off.

**New process first run:**
swtch loads the crafted kstack → RTS to `proc_first_run` → pops user
state → RTE to user mode at entry point.

### Async do_spawn

`do_spawn()` is now non-blocking:
1. Allocates process slot, copies parent FDs
2. Calls `load_binary()` to load code into USER_BASE
3. Calls `proc_setup_kstack()` to build initial kstack
4. Marks child `P_READY` and returns child PID

The parent continues immediately. The child runs when the scheduler
picks it up (either via timer preemption or when parent calls
`do_waitpid` which sleeps).

### Blocking waitpid

`do_waitpid()` now sleeps when the child hasn't exited:
- Scans for zombie child → if found, reap and return
- If child exists but isn't zombie → set P_SLEEPING, schedule()
- `do_exit()` wakes parent by setting P_SLEEPING → P_READY

### Blocking Pipes

Pipe read/write now block when empty/full:
- `pipe_read`: if pipe empty and writers exist, sleep until data arrives
- `pipe_write`: if pipe full and readers exist, sleep until space freed
- Each side wakes the other after transferring data
- POSIX partial read/write semantics: return once any data transferred

### Pain Points

1. **Blocking pipe + single-threaded test**: The autotest pipe test
   writes 5 bytes then reads 8. With blocking pipes, the reader would
   block forever waiting for 3 more bytes from itself. Fixed by using
   POSIX partial-read semantics: return immediately once any data is
   available, don't try to fill the entire buffer.

2. **Single user memory space**: All user programs load at USER_BASE.
   Two user processes can't coexist in memory. For now, the shell
   (process 0) runs in supervisor mode and doesn't use user memory,
   so shell + one child work. For `prog1 | prog2`, we'd need memory
   partitioning or swapping.

3. **kstack layout must match ISR exactly**: The byte offsets in
   `proc_setup_kstack` must match what `proc_first_run` expects.
   The 68000 exception frame is 6 bytes (2-byte SR + 4-byte PC),
   creating a misalignment with the 4-byte register slots. Used
   byte-level pointer math to build the frame correctly.

### What Changed

| File | Change |
|------|--------|
| `kernel/exec_asm.S` | Added `swtch()` and `proc_first_run` |
| `kernel/exec.c` | Extracted `load_binary()` from `do_exec()` |
| `kernel/proc.c` | Async `do_spawn`, `proc_setup_kstack`, blocking waitpid/pipes |
| `kernel/kernel.h` | Added `swtch`, `proc_first_run`, `load_binary`, pipe wait fields |
| `kernel/crt0.S` | Timer ISR delegates to `schedule()`+`swtch()` (no manual ksp save) |
| `pal/megadrive/crt0.S` | Same VBlank ISR change |
| `tests/test_proc.c` | Added kstack layout tests, updated pipe struct |

---

## Decision: Three-Branch Merge (March 2026)

**Date:** 2026-03-10
**Status:** Done, all tests pass

Three parallel development branches were merged into one:

1. **Track A** (`claude/review-plan-strategy`): Preemptive scheduling —
   per-process kernel stacks, user mode, `swtch()` context switch,
   `proc_first_run` trampoline, async `do_spawn()`, blocking pipes,
   blocking `waitpid`. 3 commits, ~760 lines changed.

2. **Track B** (`claude/prepare-libc-apps`): Libc expansion + 12 new
   apps — `getopt`, `perror`, `sprintf`, `strtol`, `isatty`, plus
   `basename`, `cmp`, `cut`, `dirname`, `nl`, `rev`, `tail`, `tee`,
   `tr`, `uniq`, `yes`. 336-line test suite. ~1560 lines added.

3. **VDP imshow** (`claude/vdp-driver-imshow-port`): VDP device driver
   (`dev_vdp.c`), `libgfx` userspace library, `imshow` app, graphics
   pipeline docs, 232-line test suite. ~1300 lines added.

### Merge Strategy

Track A merged as fast-forward (it was ahead of master). Track B merged
cleanly (no conflicts with Track A — different files). VDP imshow had
5 conflicts, all in list-type files:

| File | Conflict | Resolution |
|------|----------|------------|
| `.gitignore` | Both added app names | Combined both lists |
| `Makefile` | Both added to `CORE_BINS` | Combined both lists |
| `apps/Makefile` | Both added to `PROGRAMS` | Combined both lists |
| `libc/Makefile` | Both added to `OBJS` | Combined both lists |
| `tests/Makefile` | Both added to `TESTS` | Combined both lists |

### Pain Points

1. **Duplicate .gitignore entries**: `apps/levee/levee` appeared twice
   in `.gitignore` after merge. Removed the duplicate. Easy to miss in
   conflict resolution — always review the full file after resolving.

2. **List-format Makefiles invite conflicts**: When every branch adds
   items to the same variable (`PROGRAMS =`, `OBJS =`, `TESTS =`),
   every branch conflicts with every other branch. This is inherent to
   the "one variable, one line" Makefile pattern. Consider using
   `PROGRAMS +=` in separate files or wildcard patterns to reduce
   future conflicts.

3. **No functional conflicts**: The three branches touched orthogonal
   subsystems (kernel scheduling, libc/apps, VDP driver). No semantic
   conflicts — only syntactic list conflicts. This validates the
   modular architecture.

### Test Results After Merge

| Test | Result |
|------|--------|
| `make test` (host) | 391 passed, 0 failed |
| `make kernel` | Clean cross-compilation |
| `make test-emu` | 10 autotest passed, 0 failed |
| `make megadrive` | ROM built (548 KB) |
| `make test-md` | OK (300 frames, no crash) |
| `make test-md-auto` | OK (600 frames, no crash) |

---

## Project Status Update (March 2026 — Post-Merge)

The kernel multitasking infrastructure is now complete:

- **Per-process kernel stacks** (512 bytes each, in proc struct)
- **User mode** execution (S=0, USP/SSP separated)
- **Preemptive timer ISR** (checks S-bit, only preempts user mode)
- **swtch()** context switch (callee-saved regs, stack swap)
- **proc_first_run** trampoline (enters user mode for new processes)
- **Async do_spawn()** (non-blocking, child is P_READY immediately)
- **Blocking waitpid()** (parent sleeps until child exits)
- **Blocking pipes** (reader/writer sleep when empty/full)
- **VDP device driver** with userspace libgfx library
- **19 user programs** in /bin (up from 8)
- **391+ host tests** + automated guest tests on both platforms

### Known Limitations (BEWARE)

1. **Single user memory space**: All user programs load at USER_BASE.
   Two user processes can't coexist in memory simultaneously. The shell
   runs in supervisor mode (no USER_BASE conflict), so shell + one child
   works. For `prog1 | prog2` to work, we need memory partitioning,
   swapping, or relocation.

2. **kstack overflow has no guard**: The 512-byte kstack grows down
   into the proc struct fields. If a syscall's C call chain is too deep
   (or a timer ISR nests on top), it silently corrupts `fd[]`, `cwd`,
   etc. Consider adding a canary word at kstack[0] for debug builds.

3. **No user signal handlers yet**: Signal delivery works for default
   actions (terminate, ignore) and SIG_IGN, but user-defined handlers
   require pushing a signal frame on the user stack and returning to
   the handler address — not yet implemented. See Phase 2d below.

4. **No I/O redirection in shell**: The shell's `exec` command uses
   `do_spawn()` but doesn't support `>`, `<`, or `2>` syntax. Pipes
   work at the kernel level but need shell syntax support.

---

## Decision: Signal Delivery — Default Actions Only (March 2026)

**Date:** 2026-03-10
**Status:** Done (Phase 2d partial)

Implemented kernel signal delivery with default actions only:
- `SYS_SIGNAL(signum, handler)` — set handler, return old handler
- `SYS_KILL(pid, sig)` — send signal to process (sets pending bit)
- `sig_deliver()` — called on return to user mode (both syscall and
  timer ISR paths), processes pending signals

### Design Choices

1. **Bitmask for pending signals**: `uint32_t sig_pending` — one bit per
   signal (1–20). Simple, no queue, no memory allocation. Adequate for
   our 21 signals. Mirrors FUZIX approach.

2. **Default actions only (no user handlers yet)**: Most signals terminate
   the process (exit code 128+signum). SIGCHLD and SIGCONT are ignored by
   default. SIGKILL always terminates (can't be caught/ignored). SIGSTOP
   is acknowledged but currently a no-op (no job control yet).

3. **SIG_IGN works**: Processes can ignore specific signals. The signal
   is cleared from `sig_pending` without action.

4. **sig_deliver in assembly return paths**: Added `jsr sig_deliver` in
   both `crt0.S` files (workbench and Mega Drive) at two points:
   - After `syscall_dispatch` returns, before restoring USP
   - After `schedule()` in timer ISR, before restoring user state
   This ensures signals are checked on every return to user mode.

5. **Ctrl+C and Ctrl+\ in console driver**: `con_read()` generates
   SIGINT (Ctrl+C) and SIGQUIT (Ctrl+\) when ISIG is set in termios.
   Returns `-EINTR` after setting the signal pending bit.

### Why Not User Handlers Yet

Delivering to a user-defined handler requires:
- Saving the interrupted user context (registers, PC, SR)
- Building a signal frame on the user stack
- Setting PC to the handler address
- On handler return (via sigreturn syscall), restoring the saved context

This is ~100 lines of tricky assembly and a new syscall. Not needed for
Ctrl+C (which just terminates) or the current app set. Will implement
when we need it (e.g., for a proper shell with job control).

### Pain Points

1. **ERANGE missing from kernel.h**: `sys_getcwd()` used `ERANGE` but it
   wasn't defined. Cross-compilation caught it (`proc.c:966: error:
   'ERANGE' undeclared`). Host tests didn't catch it because they use
   the host's `<errno.h>`. **Lesson:** Always run `make kernel` after
   adding new errno values — host tests are not sufficient.

2. **dev_init() called before fs_init()**: `dev_create_nodes()` (which
   creates `/dev/null` in the filesystem) was originally inside
   `dev_init()`. But `kmain()` calls `dev_init()` before `fs_init()`,
   so all filesystem operations silently failed. The `/dev/null`
   autotest caught this. **Fix:** Split into `dev_init()` (hardware
   setup, no FS) and `dev_create_nodes()` (called after `fs_init()`).
   **Lesson:** Initialization order dependencies are insidious. The
   silent failure made it hard to diagnose — `fs_namei()` returned NULL
   but didn't print an error because the filesystem wasn't mounted yet.

3. **GETDENTS offset not updated**: `SYS_GETDENTS` was reading directory
   entries but not advancing the file offset in the `ofile` struct.
   This meant `readdir()` in libc would return the same entry forever.
   **Fix:** Added `gof->offset += gn` after successful `fs_getdents()`.
   **Lesson:** Syscalls that advance a file position must update the
   ofile offset — the caller can't do it because only the kernel knows
   how many bytes were actually consumed.

### What Changed

| File | Change |
|------|--------|
| `kernel/kernel.h` | Signal constants (SIGHUP–SIGTSTP), NSIG, SIG_DFL/SIG_IGN, `sig_deliver()`, `sig_pending`/`sig_handler[]` in proc, ERANGE, DEV_NULL, `dev_create_nodes()` |
| `kernel/proc.c` | Full `sys_signal()`, `sys_kill()`, `sig_deliver()`, `sys_getcwd()`, fixed GETDENTS offset |
| `kernel/dev.c` | /dev/null device, Ctrl+C/Ctrl+\ signal generation, `dev_create_nodes()` |
| `kernel/main.c` | Call `dev_create_nodes()` after `fs_init()`, 3 new autotests (ls, /dev/null, signals) |
| `kernel/crt0.S` | `jsr sig_deliver` in syscall return + timer ISR |
| `pal/megadrive/crt0.S` | Same sig_deliver additions |
| `libc/dirent.c` | New: opendir/readdir/closedir using SYS_GETDENTS |
| `libc/include/dirent.h` | New: DIR, struct dirent, function declarations |
| `libc/syscalls.S` | Added getdents, getcwd, rmdir, time stubs |
| `libc/Makefile` | Added dirent.o |
| `apps/ls.c` | New: ls with -l flag, using opendir/readdir |
| `apps/sleep.c` | New: busy-wait sleep using SYS_TIME |
| `apps/Makefile` | Added ls, sleep |
| `Makefile` | Added ls, sleep to CORE_BINS |
| `tests/test_signal.c` | New: 20 signal tests (63 assertions) |
| `tests/Makefile` | Added test_signal |

### Test Results

| Test | Result |
|------|--------|
| `make test` (host) | 454 passed, 0 failed (63 new from test_signal) |
| `make kernel` | Clean cross-compilation |
| `make test-emu` | 13 autotest passed, 0 failed (3 new tests) |
| `make megadrive` | ROM built (549 KB) |
| `make test-md` | OK (300 frames, no crash) |
| `make test-md-auto` | OK (600 frames, no crash) |

---

## Decision: Automated imshow Screenshot Test

**Date:** 2026-03-10
**Status:** Done, screenshot captured successfully

Added `make test-md-imshow` — an automated test that spawns imshow on
the Mega Drive (via BlastEm under Xvfb) and captures a screenshot of
the VDP color bar test pattern. This validates the full graphics stack
end-to-end: kernel VDP driver → ioctl interface → libgfx → userspace.

### How imshow Works

imshow is **not** an image viewer — it's a graphics validation tool
ported from the FUZIX Mega Drive concept. It generates test patterns
dynamically using the VDP's tile-based 4bpp graphics:

1. Opens `/dev/vdp` for exclusive access
2. Loads a 16-color test palette (Mega Drive format: 0000BBB0GGG0RRR0)
3. Generates 18 tiles programmatically: 15 solid colors, 2 checkerboards,
   1 gradient
4. Fills the 40×28 tile screen: color bars (top), alternating solid/checker
   stripes (middle), checkerboard and gradient (bottom)
5. Waits for keypress (or `-n` flag: holds for 120 vsync frames then exits)
6. Closes VDP and restores text console

The original FUZIX source (EythorE/FUZIX, megadrive branch) has `fview`
which is a BMP viewer using a completely different graphics API (GFXIOC).
Genix's imshow uses the VDP ioctl interface directly — tile-based, not
framebuffer-based. The test pattern serves the same purpose: validating
that the graphics stack works.

### Implementation

Three changes make automated testing possible:

1. **imshow `-n` flag** (`apps/imshow.c`): No-wait mode displays the
   pattern for ~2 seconds (120 vsync frames at 60fps) then exits cleanly
   without waiting for a keypress. This is the key enabler — without it,
   imshow blocks on `read(0, ...)` forever in headless mode.

2. **IMSHOW_TEST kernel mode** (`kernel/main.c`): When compiled with
   `-DIMSHOW_TEST`, the kernel spawns `imshow -n` via `do_spawn()` +
   `do_waitpid()` instead of running AUTOTEST or the shell. Prints
   "IMSHOW_TEST PASSED/FAILED" based on exit code.

3. **`make test-md-imshow` target** (`Makefile`): Builds the IMSHOW_TEST
   ROM, boots it in BlastEm under Xvfb (display :59), waits 5 seconds
   for imshow to render, captures via xdotool + scrot, cleans up, and
   restores the normal ROM. Uses display :59 (not :58) to avoid conflict
   with the existing screenshot target.

### Pain Points

1. **gfx_close() doesn't fully restore VDP state**: When imshow exits
   and calls `gfx_close()`, the text font palette is restored but the
   tile data and nametable entries from imshow remain in VRAM. The
   screenshot shows the color bar pattern with kernel text ("IMSHOW_TEST
   PASSED") overlaid — the text console writes on top of the lingering
   graphics tiles. This is cosmetic, not a functional bug, but means the
   screenshot captures a hybrid state (imshow graphics + kernel text).

2. **No automated pixel comparison**: The test captures a screenshot but
   has no reference image to compare against. It's currently visual
   inspection only — you look at `test-md-imshow.png` to verify the
   color bars rendered correctly. Future work: add a reference PNG and
   a pixel-diff tool, or use BlastEm's GDB interface to inspect VRAM
   contents directly.

3. **BlastEm screenshot capture is fragile**: Can't use BlastEm's
   native screenshot key (`p`) because SDL2's XInput2 doesn't receive
   xdotool's synthetic key events under Xvfb. Must use external screen
   capture (scrot) which depends on window focus working correctly.
   Sometimes `xdotool search --name "BlastEm"` returns no window if
   BlastEm exits before the capture delay.

4. **imshow is Mega Drive only**: imshow requires `/dev/vdp` which only
   exists on the Mega Drive platform. The workbench emulator has no VDP,
   so `gfx_open()` returns an error. The IMSHOW_TEST can only run via
   BlastEm, not the workbench emulator. This means the test can't be
   part of the fast `test-emu` iteration cycle.

5. **Slow rebuild cycle**: The test target rebuilds apps (for MD linker
   script), rebuilds the ROM (with IMSHOW_TEST), runs BlastEm under
   Xvfb, captures, then rebuilds everything again (normal ROM + workbench
   apps). Total time ~30 seconds. This is inherent to the "special ROM"
   approach — each test mode requires a separate kernel build.

### What Changed

| File | Change |
|------|--------|
| `apps/imshow.c` | Added `-n` flag (no-wait mode, 120 vsync frames) |
| `kernel/main.c` | Added `IMSHOW_TEST` mode: spawn imshow, report result |
| `Makefile` | Added `test-md-imshow` target with Xvfb + scrot capture |
| `.gitignore` | Added `test-md-imshow*.png` |

---

## Decision: Phase 2c — Shell Pipes and I/O Redirection

**Date:** 2026-03-10
**Status:** Complete

Implemented shell-level pipe (`|`), output redirect (`>`/`>>`), and input
redirect (`<`) support. The shell parses commands with `parse_segment()`,
`parse_redirections()`, and `count_pipes()`, then uses `do_spawn_fd()` to
set up child processes with custom FDs.

### Sequential Pipeline Execution

The most important design decision: pipelines execute **sequentially**, not
concurrently. Because all user processes share `USER_BASE` (no MMU), two
user processes cannot be loaded at the same time — the second would overwrite
the first's code and data. So `echo hello | cat` works as:

1. Create pipe
2. Spawn `echo`, redirect its stdout to pipe write end, wait for it to finish
3. Spawn `cat`, redirect its stdin to pipe read end, wait for it to finish

This works because the pipe buffer (512 bytes) holds the intermediate data.
Pipelines that produce more than 512 bytes of output will lose data. This
is a known limitation of the no-MMU, single-address-space design.

### SIGPIPE

`pipe_write()` generates SIGPIPE when writing to a pipe with no readers.
This is the standard POSIX behavior. Two check points: once at the top of
`pipe_write()` and once inside the write loop (reader may close while
writer is blocked).

### Pain Points

1. **Pipeline data loss**: The 512-byte pipe buffer limits pipelines to
   small outputs. `echo hello | cat` works; `cat /bin/ls | wc` would
   overflow and lose data. Real concurrent pipelines require either an MMU
   or a relocation-capable binary format.

2. **`do_spawn_fd()` is verbose**: The FD replacement logic (decrement
   refcount, handle pipe cleanup, set new FD, increment refcount) is
   repeated 3 times for stdin/stdout/stderr. Could be refactored into a
   helper, but keeping it explicit avoids abstraction for a one-use pattern.

---

## Decision: Phase 2d — User Signal Handlers

**Date:** 2026-03-10
**Status:** Complete

Implemented the complete signal delivery mechanism: user-defined signal
handlers, signal frame on user stack, sigreturn trampoline, SIGTSTP/SIGCONT
for process stop/continue, and process group initialization.

### Signal Frame Architecture

When a user process has a signal handler registered and a signal is pending,
`sig_deliver()` (called on return to user mode from syscall or timer ISR)
builds an 84-byte signal frame on the user stack:

```
Offset  Content
0       return addr → trampoline (at base+8)
4       signal number (handler's first C argument)
8       trampoline: moveq #119,%d0 (0x7077) + trap #0 (0x4E40)
12      saved kstack frame (70 bytes: USP + d0-d7/a0-a6 + SR + PC)
82      padding (2 bytes for even alignment)
```

The handler runs in user mode with the modified USP and PC. When it returns
(RTS), execution jumps to the trampoline which does `TRAP #0` with
`d0 = SYS_SIGRETURN (119)`. The sigreturn handler copies the saved 70-byte
kstack frame back over the current frame, restoring all user registers and
the original return address.

### Key Design Decisions

1. **Frame pointer parameter**: `sig_deliver(uint32_t *frame)` and
   `sys_sigreturn(uint32_t *frame)` both receive a pointer to the saved
   user state on the kstack. The asm code in `crt0.S` passes `%sp` before
   calling these functions. This avoids fragile stack offset calculations
   in C code.

2. **SYS_SIGRETURN handled in asm**: `_vec_syscall` checks for syscall
   number 119 before calling `syscall_dispatch`. This lets `sys_sigreturn`
   directly modify the kstack frame without going through the normal
   syscall return path (which would overwrite d0).

3. **Full frame save/restore**: The signal frame saves all 70 bytes of
   kstack state (USP + 15 registers + SR + PC). This is essential for
   timer-delivered signals where the interrupt can happen at any point
   in user code — all registers must be preserved exactly. A simpler
   approach (saving only d0 and PC) would corrupt state for timer signals.

4. **Trampoline on user stack**: The 4-byte trampoline (`moveq #119,%d0;
   trap #0`) lives in the signal frame on the user stack. The 68000 has
   no NX bit, so executable stack is fine. This avoids needing a fixed
   trampoline address.

5. **One-shot handlers**: `signal()` uses classic one-shot semantics —
   after delivery, the handler resets to SIG_DFL. The process must
   re-register the handler if it wants to catch the signal again. This
   matches traditional Unix `signal()` behavior. Future: add `sigaction()`
   with SA_RESTART if needed.

6. **One handler per sig_deliver call**: When multiple signals are pending,
   `sig_deliver` delivers only one user-handler signal and returns. The
   next signal is delivered when the sigreturn syscall's return path calls
   `sig_deliver` again. This avoids nested signal frames.

### SIGTSTP / SIGCONT / P_STOPPED

- SIGTSTP default action: sets process to `P_STOPPED`, calls `schedule()`
- SIGSTOP: always stops (cannot be caught/ignored), same as SIGTSTP default
- SIGCONT via `kill()`: wakes `P_STOPPED` process → `P_READY`, clears
  pending SIGSTOP/SIGTSTP
- The scheduler skips `P_STOPPED` processes

### Process Groups

Each process gets `pgrp = pid` by default (set in `do_spawn`). Process 0
(kernel/shell) has `pgrp = 0`. The infrastructure is in place for future
shell job control (`kill(-pgrp, sig)` to signal process groups).

### Job Control Limitations (No-MMU)

Full job control (fg/bg/jobs) is limited by the shared USER_BASE design:
- A stopped process's code/data at USER_BASE is intact only until another
  command runs
- Background processes cannot truly run concurrently with the shell
- `fg` can resume a stopped process if no other command ran since the stop

This is a fundamental no-MMU limitation. Real job control requires either
an MMU or process relocation.

### Pain Points

1. **Host test vs target mismatch**: The signal frame logic involves
   writing to user memory addresses (stored as `uint32_t` on 68000).
   Host tests on 64-bit systems can't dereference these truncated pointers.
   Solution: test the logic (handler dispatch, one-shot reset, pending
   bits, stop/continue) on host; test the actual memory layout on the
   68000 target via autotest.

2. **84 bytes per signal frame**: On the Mega Drive with ~28 KB user
   space, each signal delivery consumes 84 bytes of user stack. Deeply
   nested signals (unlikely in practice) could overflow the user stack.

3. **No SA_RESTART**: Signal delivery interrupts blocking syscalls
   (read, write, waitpid). The interrupted syscall returns -EINTR, and
   the user program must retry. Adding SA_RESTART semantics would require
   the kernel to re-enter the syscall after signal handler return.

---

## Decision: Phase 2e — TTY Subsystem (Line Discipline)

**Date:** Phase 2e
**Status:** Complete

### What We Did

Ported a simplified Fuzix-style TTY line discipline as `kernel/tty.c`
(~320 lines). This replaces the ad-hoc console I/O in `kernel/dev.c`
with a proper three-layer architecture:

```
User: read()/write()/ioctl()
  ↓
tty.c: Line discipline (cooked/raw, echo, erase, signals)
  ↓
PAL: pal_console_putc()/pal_console_getc()
```

### Key Design Decisions

1. **256-byte circular buffer with uint8_t head/tail** — wraps at 256
   naturally, no modulo instruction needed on the 68000. This is a
   proven optimization from Fuzix.

2. **Separate canonical buffer** — in cooked mode, characters accumulate
   in `canon_buf[]` where they can be erased/killed before being
   transferred to the input queue. This separates "editing" from
   "available to read", which is how real Unix TTYs work.

3. **Polling-based input** — `tty_read()` polls `pal_console_ready()`
   while waiting for input. This matches the existing architecture
   (no interrupt-driven keyboard yet). When interrupt-driven keyboard
   is added (Phase 4), `tty_inproc()` will be called from the ISR
   instead of the polling loop.

4. **Output processing (OPOST/ONLCR)** — `tty_write()` maps NL to
   CR-NL when OPOST+ONLCR are set. The shell's `kputc()` still goes
   directly to `pal_console_putc()` for kernel output (before TTY
   is initialized), but user writes go through the TTY layer.

5. **Single TTY for now** — `NTTY=1`. The structure supports multiple
   TTYs (Phase 4), but we don't need them yet. Each TTY device has
   its own buffer, termios, and winsize.

6. **Device nodes** — `/dev/tty` and `/dev/console` both point to
   `DEV_CONSOLE` major, minor 0. They're created at boot in
   `dev_create_nodes()`.

### What Works

- Cooked mode: line buffering, echo, backspace erase (^H/DEL),
  kill (^U), ^D EOF (flush or 0-length read)
- Raw mode: immediate character delivery
- Signal generation: ^C→SIGINT, ^\ →SIGQUIT, ^Z→SIGTSTP
- NOFLSH flag (preserve input on signal)
- Echo control: ECHO, ECHOE (BS-SP-BS), ECHOK, ECHONL
- Input mapping: ICRNL, INLCR, IGNCR
- Output processing: OPOST, ONLCR (NL→CR-NL)
- termios ioctls: TCGETS, TCSETS, TCSETSW, TCSETSF
- Window size: TIOCGWINSZ (40x28), TIOCSWINSZ
- Process group: TIOCGPGRP, TIOCSPGRP (stored, not enforced yet)
- `/dev/tty` and `/dev/console` device nodes
- 78 host unit tests covering all line discipline features
- Autotest: 4 TTY tests in both workbench and BlastEm autotests

### Pain Points

1. **Kernel shell now goes through TTY** — the built-in shell's
   `builtin_shell()` used to do its own line editing (backspace, ^C).
   After the TTY change, it reads through `devtab[DEV_CONSOLE].read()`
   which calls `tty_read()` which handles all that. This simplified
   the shell code significantly, but required testing both paths.

2. **Double echo risk** — the old `con_read()` echoed characters itself,
   AND the shell echoed them too. With the TTY layer, echo happens in
   `tty_inproc()` only. Had to make sure the shell doesn't double-echo.

3. **kputc vs tty_write output paths** — kernel diagnostic output
   (`kputs`, `kprintf`) goes through `kputc()→pal_console_putc()`
   directly, bypassing OPOST processing. User writes go through
   `tty_write()` which applies ONLCR. This is correct (kernel output
   shouldn't be processed) but means NL→CRNL handling differs between
   kernel and user output.

4. **Incomplete element type** — tried to add `extern struct tty
   tty_table[]` to `kernel.h` as a forward declaration, but C requires
   complete type for extern arrays. Fixed by keeping the declaration in
   `tty.h` only and having files that need it include `tty.h` directly.
   **Rule:** Never put extern arrays of incomplete types in shared headers.

5. **SIGTSTP delivery** — `^Z` generates SIGTSTP. With Phase 2d now
   complete, SIGTSTP correctly stops the process (P_STOPPED state)
   and SIGCONT resumes it. The TTY layer generates the signal;
   the signal subsystem handles stop/continue semantics.

### What Changed

| File | Change |
|------|--------|
| `kernel/tty.h` | New — TTY structures, constants, interface |
| `kernel/tty.c` | New — Line discipline (~320 lines) |
| `kernel/dev.c` | Console routes through TTY layer; /dev/tty, /dev/console nodes |
| `kernel/main.c` | Shell uses TTY reads; 4 new autotest cases |
| `kernel/kernel.h` | TTY comment (declarations in tty.h) |
| `libc/include/termios.h` | Added OPOST, ONLCR, ECHOE, ECHOK, ECHONL, NOFLSH, TIOCGWINSZ, TIOCSWINSZ, winsize struct |
| `libc/termios.c` | `tcsetattr` now maps TCSADRAIN→TCSETSW, TCSAFLUSH→TCSETSF |
| `kernel/Makefile` | Added tty.c |
| `pal/megadrive/Makefile` | Added tty.c + tty.o build rule |
| `tests/test_tty.c` | New — 78 host unit tests |
| `tests/Makefile` | Added test_tty |
