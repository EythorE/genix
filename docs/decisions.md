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

## ROM XIP (Phase 5) — Complete

**Status:** Implemented via romfix (Strategy A)

`romfix` post-processes the Mega Drive ROM at build time, resolving
text references to absolute ROM addresses and data references to
USER_BASE. The kernel's `load_binary_xip()` detects XIP-flagged
binaries and executes text directly from ROM — only .data is copied
to RAM.

The split XIP engine (`apply_relocations_xip()` and `load_binary_xip()`
with separate text/data base pointers) also exists in the kernel for
future EverDrive Pro bank-swapping (Phase 8). See
[relocation-implementation-plan.md](relocation-implementation-plan.md)
Phase 7.

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

## Async Exec for Vfork Children

**Status:** Implemented (March 2026)

When a vfork child calls `execve()`, the kernel detects the parent in
`P_VFORK` state and converts the synchronous `do_exec` into an async
process setup. The child gets its own memory slot and kstack frame (via
`proc_setup_kstack`), is marked `P_READY`, and the parent is woken via
`vfork_restore`. This enables concurrent parent+child execution needed
for shell command execution and pipelines.

**Why not always async?** The synchronous `exec_enter` path is still used
by autotest and the builtin shell (process 0), which have no vfork parent.
The vfork detection only triggers when the parent is in `P_VFORK` state.

**Bug fix included:** `do_vfork` now sets `child->mem_slot = -1` after
copying parent state. Previously `*child = *parent` copied the parent's
slot index, so a child calling `_exit()` without exec would free the
parent's memory slot.

---

## Printf System — Two Implementations, One Direct-Write

**Status:** Active — resolved (March 2026)

The libc has two printf implementations with different tradeoffs:

### 1. printf() / fprintf() — Direct-write, common format specifiers

`libc/stdio.c`. Shared `do_printf()` engine that writes directly to
the fd via `write()` syscalls. Supports `%s`, `%d`, `%u`, `%x`, `%c`,
`%%`, plus `l` modifier (`%ld`, `%lu`, `%lx` — no-op on 68000 where
int == long == 32 bits).

**Strengths:**
- No buffer limit — output of any length works
- No extra code pulled in — zero cost if the app doesn't use vsnprintf
- Direct-write means no stack buffer (saves RAM on 512-byte kstacks)
- printf and fprintf share one `do_printf()` — no code duplication

**Limitation:** Varargs via `(const char **)(&fmt + 1)` stack-casting.
Works on 68000 (all args are 32-bit on stack) but is technically
undefined behavior per the C standard.

### 2. vsnprintf() / snprintf() / sprintf() — Full format support

`libc/sprintf.c`. Two sub-implementations:
- `do_vsnprintf()` (line 48): old `const char **args` stack-casting,
  supports `%s`, `%d`, `%u`, `%x`, `%c`, `%%`, `%ld`, `%lu`, `%lx`
- `vsnprintf()` (line 137): proper `va_list`, full format support
  including width, padding, precision, `%o`, `%X`, `%p`, `%lld`,
  64-bit without 68020 instructions (manual hi:lo division)

### Rejected approach: replace printf with vsnprintf wrapper

A branch attempted to rewrite `printf()` to delegate to `vsnprintf()`
into a 128-byte stack buffer. This was rejected because:
1. **Binary size explosion** — vsnprintf is ~5 KB of code. Every app
   that calls printf (most of them) would link it in, nearly doubling
   many binaries (e.g., `cp` from 6 KB to 12 KB, `uname` from 6 KB
   to 11 KB).
2. **Disk overflow** — the 512-block filesystem couldn't fit all 48
   apps with the larger binaries.
3. **128-byte truncation** — the stack buffer silently truncates
   output longer than 127 bytes. The direct-write version has no limit.
4. **Stack pressure** — 128 bytes on the stack in every printf call
   is significant when kstacks are 512 bytes.

The correct fix was adding `%u`/`%x` directly to the existing
direct-write printf (~40 lines, zero binary size impact).

---

## Known Limitations

1. ~~**Single user memory space**~~ Resolved: variable-size user memory
   allocator replaces fixed slots. Multiple processes coexist; pipelines
   run concurrently. See "Fixed-Slot Allocator Oversight" above.

2. **Sequential pipelines** — the 512-byte pipe buffer limits output.
   Real concurrent pipelines need multiple processes in memory.

3. **Shell features** — no glob expansion, no environment variable
   substitution, no background jobs.

4. **kstack overflow has no guard** — the 512-byte kstack grows into
   proc struct fields. Consider a canary word for debug builds.

5. **Levee too large for Mega Drive** — 44 KB binary vs ~28 KB user
   space. ROM XIP helps (text in ROM, only data in RAM), but levee's
   data+bss still exceeds a single slot. Phase 8 (PSRAM) fully solves this.

6. **No SA_RESTART** — signal delivery interrupts blocking syscalls.
   User programs must retry on -EINTR.

7. **Environment variables are process-local** — each exec'd process
   starts with a fresh environment. True environment passing would
   require kernel support to forward envp through exec().

---

## Fixed-Slot Allocator Oversight (Phase 6)

**Date:** March 2026
**Status:** Replaced with variable-size user memory allocator

The Phase 6 slot allocator divided USER_BASE..USER_TOP into fixed-size
equal slots (e.g., 2 × 13,750 bytes on Mega Drive). This was a huge
oversight in the initial implementation plan: **fixed slots waste the
majority of user RAM for small programs and prevent large programs from
using available memory.**

**The problem:**
- `ls` (400 B data+bss) was allocated 13,750 B — wasting 97% of its slot.
- A `ls | more` pipeline needs 3 concurrent processes (dash + ls + more),
  but only 2 slots existed on Mega Drive. The pipeline fails with ENOMEM.
- Larger programs that could fit in 17+ KB are artificially capped at
  13,750 B per slot.

**Root cause:** The slot allocator was modeled as the simplest possible
approach during Phase 6 (concurrent processes) without considering that
the use case (pipelines with small utilities) demands variable-size
regions. The `slot_size()` constant baked a worst-case assumption into
every allocation.

**Fix:** Replace with a proc-table-scanned first-fit allocator. The proc
table already stores `mem_base` and `mem_size` for every active process —
this IS the allocation metadata. No new data structures needed. The
allocator scans proctab to find gaps in user memory, returning a
region of exactly the requested size. Coalescing is automatic: when a
process exits, its `mem_base` is cleared, and the gap appears on the
next scan.

**Lesson:** On a system with 27.5 KB of user RAM and programs ranging
from 400 B to 15 KB, a fixed-size allocator is fundamentally wrong.
The "simplest" approach (equal slots) was actually the most wasteful.
The truly simple approach is to allocate what's needed — which requires
fewer lines of code (no slot arrays, no slot indices) and works better.

See [docs/memory-system.md](memory-system.md) for the complete memory
system documentation.

---

## Open Questions

1. ~~**brk() vs mmap()?**~~ Resolved: brk() bounded by per-process
   `mem_size`. With variable allocation, each process gets exactly
   the RAM it needs — sbrk grows within that region.

2. ~~**Shell: Fuzix sh or custom?**~~ Resolved: dash (Debian Almquist
   Shell). See [docs/shell-research.md](shell-research.md) for the full
   analysis of 8 candidates. [docs/shell-plan.md](shell-plan.md) for
   the implementation plan.
