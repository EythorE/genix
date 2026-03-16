# Active Design Decisions

Design decisions that guide current and future Genix development.

For resolved history (bugs, implementation details, phase-by-phase
timeline), see [HISTORY.md](../../HISTORY.md).

---

## Why Genix Exists

FUZIX is a multi-user Unix clone with 30+ platform ports, deep legacy
code, and abstractions for hardware we don't have (banked memory, floppy
disks, RS-232). The Mega Drive port bolts onto this. The result: a
forking bug nobody can find because the interactions between the 68000
platform code and the generic kernel are too numerous and too subtle.

We don't need multi-user, login, file ownership, or 30 platform ports.
We need a small, single-user OS for the 68000 that one person can read
in an afternoon. Writing ~6,500 lines of new kernel code is cheaper than
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
inode table, data blocks. See [filesystem.md](../filesystem.md).

### Binary Format — Genix Relocatable Flat Binary

**Status:** Active — relocatable binaries are the default

32-byte header ("GENX" magic) with relocation support. Programs linked
at address 0 with `--emit-relocs`. mkbin extracts R_68K_32 relocations.
At exec() time, the kernel adds USER_BASE to each relocated 32-bit word.
One binary works on both workbench and Mega Drive.

The header's `text_size` field enables split text/data for ROM XIP.
See [binary-format.md](../binary-format.md) and
[relocatable-binaries.md](../research/relocatable-binaries.md).

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

## Why romfix (Build-Time XIP, Not Runtime)

**Status:** Firm — Strategy A from
[relocatable-binaries.md](../research/relocatable-binaries.md)

The alternative to `romfix` is doing all relocation at runtime in the
kernel. This was investigated and rejected:

1. **ROM is read-only.** Text lives in cartridge ROM. The kernel cannot
   patch absolute addresses in ROM text at load time. Without romfix,
   text would have to be copied to RAM first (defeating XIP entirely)
   or every binary would need PIC/GOT overhead on every data access.

2. **Runtime relocation adds kernel complexity for no gain.** romfix
   resolves text-segment relocations once at build time. Moving this
   into the kernel means the kernel must read relocation tables, resolve
   ROM addresses, and somehow patch read-only ROM — or maintain a
   shadow copy, which wastes the RAM that XIP is designed to save.

3. **romfix is simple.** ~300 lines of host C, runs once after linking.
   The kernel XIP path is just: copy .data to RAM, zero BSS, JMP to
   ROM entry. Zero relocation cost at runtime.

The build-time approach keeps the kernel loader trivial and gives true
zero-cost XIP from ROM.

---

## Fixed-Slot → Variable-Size Allocator (Phase 6)

**Date:** March 2026
**Status:** Replaced with variable-size user memory allocator

The Phase 6 slot allocator divided USER_BASE..USER_TOP into fixed-size
equal slots (e.g., 2 × 13,750 bytes on Mega Drive). This was a
deliberate design choice: fixed slots give each process a predictable
data address, which maps cleanly onto EverDrive Pro's banked PSRAM model
(each 512 KB bank is also a fixed-size region). The fixed-slot allocator
was designed to accommodate both main RAM and future banked SRAM with a
uniform model.

**Why it was replaced:** In practice, the dominant use case is shell
pipelines with small utilities. On 27.5 KB of Mega Drive user RAM:
- `ls` (400 B data+bss) was allocated 13,750 B — wasting 97% of its slot
- `ls | more` needs 3 processes (dash + ls + more), but only 2 slots
  existed — the pipeline fails with ENOMEM
- Larger programs that could fit in 17+ KB are capped at 13,750 B

The banked-PSRAM future was not worth crippling the present. When
EverDrive Pro support is implemented, the bank allocator will be a
separate subsystem anyway (512 KB banks for text, main RAM variable
regions for data). The main-RAM allocator doesn't need to mirror the
bank structure.

**Fix:** Proc-table-scanned first-fit allocator (`umem_alloc`). The
proc table already stores `mem_base` and `mem_size` for every active
process — this IS the allocation metadata. Fewer lines of code than the
slot allocator, no slot arrays or indices, and allocates exactly what
each process needs.

See [memory-system.md](../memory-system.md) for the allocator docs.

---

## Async Exec for Vfork Children

**Status:** Implemented (March 2026)

When a vfork child calls `execve()`, the kernel detects the parent in
`P_VFORK` state and converts the synchronous `do_exec` into an async
process setup. The child gets its own memory region and kstack frame
(via `proc_setup_kstack`), is marked `P_READY`, and the parent is woken
via `vfork_restore`. This enables concurrent parent+child execution
needed for shell command execution and pipelines.

**Why not always async?** The synchronous `exec_enter` path is still used
by autotest and the builtin shell (process 0), which have no vfork parent.
The vfork detection only triggers when the parent is in `P_VFORK` state.

---

## Printf System — Two Implementations, One Direct-Write

**Status:** Firm (March 2026)

The libc has two printf implementations:

1. **printf() / fprintf()** — Direct-write via `write()` syscalls.
   Supports `%s`, `%d`, `%u`, `%x`, `%c`, `%%`, `%ld`/`%lu`/`%lx`.
   No buffer limit, no extra code pulled in.

2. **vsnprintf() / snprintf() / sprintf()** — Full format support
   including width, padding, precision, `%o`, `%X`, `%p`, `%lld`,
   64-bit without 68020 instructions.

### Rejected: replace printf with vsnprintf wrapper

A branch attempted this. Rejected because vsnprintf is ~5 KB of code.
Every app that calls printf would link it in, nearly doubling many
binaries and overflowing the 512-block filesystem. The correct fix was
adding `%u`/`%x` directly to the existing direct-write printf (~40 lines,
zero binary size impact).

---

## Known Limitations

1. **kstack overflow has no guard** — the 512-byte kstack grows into
   proc struct fields. Consider a canary word for debug builds.

2. **Levee too large for Mega Drive** — levee's data+bss exceeds
   available user RAM. Workbench-only until PSRAM support is added.

3. **No SA_RESTART** — signal delivery interrupts blocking syscalls.
   User programs must retry on -EINTR.

4. **Environment variables are process-local** — each exec'd process
   starts with a fresh environment. True environment passing would
   require kernel support to forward envp through exec().
