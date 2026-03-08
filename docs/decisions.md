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

## Decision: Preemptive Round-Robin Scheduler (Planned)

**Date:** Plan phase
**Status:** Not yet implemented

Timer-driven (VBlank at 50/60 Hz on Mega Drive, 100 Hz on workbench).
Context switch via `MOVEM.L` — saves 15 registers in one instruction.
Simple, fair, proven. ~50 lines of C + ~30 lines of assembly.

**Alternative considered:** Cooperative scheduling. Rejected because it
requires every program to yield voluntarily, which breaks POSIX
expectations (a busy loop would hang the system).

---

## Project Status (March 2026)

For the full development plan with phase details, see [PLAN.md](../PLAN.md).

| Phase | Description | Status |
|-------|-------------|--------|
| **Phase 1** | Workbench emulator (Musashi SBC) | **Complete** |
| **Phase 2a** | Kernel core + binary loading + single-tasking exec | **Complete** |
| **Phase 2b** | Multitasking (vfork, scheduler, waitpid) | **Next** |
| **Phase 2c** | Pipes and I/O redirection | Planned |
| **Phase 2d** | Signals and job control | Planned |
| **Phase 2e** | TTY subsystem (port Fuzix tty.c) | Planned |
| **Phase 2f** | Fuzix libc + utilities | Planned |
| **Phase 3** | Mega Drive port (PAL drivers from Fuzix) | **Complete** |
| **Phase 4** | Polish (interrupt keyboard, multi-TTY, /dev/null) | Planned |

**What works today:**
- Kernel boots on workbench emulator and Mega Drive (BlastEm + real hardware)
- Filesystem (minifs) with read/write/create/delete/rename/mkdir/rmdir
- exec() loads and runs user programs (hello, echo, cat) from disk
- Built-in debug shell with ls, cat, echo, mkdir, mem, help, halt
- 34 host tests passing, both workbench and Mega Drive builds clean
- Saturn keyboard input on Mega Drive, UART on workbench
- SRAM works with standard Sega mapper on all tested targets

**What's next:** vfork() + waitpid() to enable a proper shell that can
launch programs and wait for them to finish. Then preemptive scheduling,
pipes, and signals to get a usable interactive Unix environment.

---

## Open Questions

These are decisions we haven't made yet:

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
