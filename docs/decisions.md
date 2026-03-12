# Active Design Decisions

Design decisions that guide current and future Genix development.

For resolved history (bugs, implementation details, phase-by-phase
timeline), see [HISTORY.md](../HISTORY.md).

---

## Why Genix Exists

FUZIX is a multi-user Unix clone with 30+ platform ports, deep legacy
code, and abstractions for hardware we don't have (banked memory, floppy
disks, RS-232). The Mega Drive port bolts onto this. The result: a
forking bug nobody can find because the interactions between the 68000
platform code and the generic kernel are too numerous and too subtle.

We don't need multi-user, login, file ownership, or 30 platform ports.
We need a small, single-user OS for the 68000 that one person can read
in an afternoon. Writing ~5,650 lines of new kernel code is cheaper than
understanding 15,000+ lines of someone else's kernel.

---

## Core Design Decisions

### No fork(), vfork()+exec() Only

**Status:** Firm

The 68000 has no MMU. fork() on a no-MMU system means copying the entire
process memory, which is fragile and expensive on a 64 KB system. FUZIX's
fork() on the Mega Drive was the root cause of an unfindable bug.

vfork()+exec() is the proven approach for no-MMU systems (uClinux uses
it). The child shares the parent's address space until exec().

### Custom Filesystem (minifs)

**Status:** Firm

Classic Unix inode filesystem: superblock, inode bitmap, data bitmap,
inode table, data blocks. See [docs/filesystem.md](filesystem.md).

### Binary Format — Genix Relocatable Flat Binary

**Status:** Active — relocatable binaries are the default

32-byte header ("GENX" magic) with relocation support. Programs linked
at address 0 with `--emit-relocs`. mkbin extracts R_68K_32 relocations.
At exec() time, the kernel adds USER_BASE to each relocated 32-bit word.
One binary works on both workbench and Mega Drive.

The header's `text_size` field enables split text/data for future ROM
XIP. See [docs/binary-format.md](binary-format.md) and
[docs/relocatable-binaries.md](relocatable-binaries.md).

### Libc — Custom Instead of Fuzix or newlib

**Status:** Firm

Custom libc (~5 KB, 16 modules) exactly tailored to the Genix syscall
interface. Comparable to Fuzix libc in size but no adaptation layer needed.

newlib (50-100 KB) doesn't fit in 64 KB RAM.

### Mega Drive as Primary Target

**Status:** Firm

Every feature must work on real Mega Drive hardware: 64 KB main RAM,
7.67 MHz, no MMU, optional SRAM. Memory layout must be modular
(PAL-provided, not hardcoded). Always verify both `make run` and
`make megadrive`.

### SRAM Is Optional

**Status:** Firm

The system must boot and run user programs on 64 KB main RAM alone.
SRAM provides persistent storage and extended RAM for larger programs.
Standard Sega mapper (`0xA130F1 = 0x03`) works on all tested hardware.

### Testing Ladder

**Status:** Firm, codified

1. `make test` — Host unit tests (logic)
2. `make kernel` — Cross-compilation (ABI, declarations)
3. `make test-emu` — Workbench autotest (STRICT_ALIGN)
4. `make megadrive` — Mega Drive build (link, memory layout)
5. `make test-md` — Headless BlastEm (crash detection)
6. `make test-md-auto` — BlastEm AUTOTEST (**primary quality gate**)

### Division Rules for 68000

**Status:** Firm

The 68000 has NO 32/32 hardware divide. Rules:
- Powers of 2: use `>> n` and `& (n-1)`
- 16-bit constant divisors: OK (DIVU.W handles them)
- 32-bit runtime divisors in hot paths: avoid
- Use 256-byte circular buffers with uint8_t indices

### Own VDP Driver, Not SGDK

**Status:** Firm

SGDK assumes exclusive ownership of CPU, interrupts, memory. Running it
under an OS would require replacing most of its internals. Instead: a
kernel VDP driver with userspace libgfx library via ioctls.

---

## XIP Relocator (Future)

**Status:** Core engine done, hardware integration pending

`apply_relocations_xip()` handles loading text and data at separate
memory addresses (text in banked SRAM, data in main RAM). The
corresponding loader `load_binary_xip()` is not yet implemented.

**Why separate functions:**
1. Different memory layout (two base pointers vs one)
2. Different loading sequence (two fs_read calls vs one)
3. Hot path stays fast (zero overhead when unused)

Remaining work for full EverDrive Pro bank-swapping:
- Detect Pro mode at boot
- SRAM bank allocator (~40 lines)
- Per-process `sram_bank` field in struct proc
- Context switch writes bank register (~5 lines asm)

See [PLAN.md](../PLAN.md) for the forward-looking plan.

---

## Relocation Code Review Methodology

**Date:** March 2026
**Lesson learned**

Post-merge code review of relocatable binaries found 5 issues that
testing missed (5,123+ tests all passed):

1. Missing runtime bounds/alignment checks in relocator
2. mkbin odd-alignment was warning not error
3. Dangling declaration (load_binary_xip)
4. BSS zeroing didn't cover reloc table footprint
5. Stale documentation

**Lesson:** Functional tests only cover well-formed inputs. Defensive
validation, documentation accuracy, and dead code cleanup require human
review. Always review after merge.

---

## Known Limitations

1. **Single user memory space** — all user programs load at USER_BASE;
   two processes can't coexist in memory. Pipelines execute sequentially.
   Requires ROM XIP or memory partitioning to fix (see PLAN.md).

2. **Sequential pipelines** — the 512-byte pipe buffer limits output.
   Real concurrent pipelines need multiple processes in memory.

3. **Shell features** — no glob expansion, no environment variable
   substitution, no background jobs.

4. **kstack overflow has no guard** — the 512-byte kstack grows into
   proc struct fields. Consider a canary word for debug builds.

5. **Levee too large for Mega Drive** — 44 KB binary vs ~28 KB user
   space. ROM XIP would allow text in ROM, only data in RAM.

6. **No SA_RESTART** — signal delivery interrupts blocking syscalls.
   User programs must retry on -EINTR.

7. **Environment variables are process-local** — each exec'd process
   starts with a fresh environment. True environment passing would
   require kernel support to forward envp through exec().

---

## Open Questions

1. **brk() vs mmap()?** In single-tasking, brk() works fine. In
   multitasking with memory partitioning, it's problematic if another
   process is after ours. Plan: brk() with careful load ordering,
   revisit when memory partitioning is implemented.

2. **Shell: Fuzix sh or custom?** Currently using a minimal built-in
   shell. A real shell running from ROM (see PLAN.md Phase 6) could
   be much more capable.
