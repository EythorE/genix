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
the framebuffer during `process_framebuffer()`. We investigated injecting
key events via XTest (Python ctypes + libXtst), but SDL2 doesn't pick up
XTest synthetic events reliably under Xvfb with software rendering.

**Screenshot test strategy:** `test-md-screenshot` still uses Xvfb and
tries `xdotool key p` to trigger BlastEm's native PNG screenshot (320×224,
no window chrome). Falls back to `scrot` if native screenshot fails.

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
