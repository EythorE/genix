# Genix Forward Plan

What remains to be built. For project history and completed phases,
see [HISTORY.md](HISTORY.md).

Current state: Genix is a working preemptive multitasking OS with 34
user programs, relocatable binaries, pipes, signals, job control, and
a TTY subsystem. It runs on both the workbench emulator and real Mega
Drive hardware. ROM XIP is working on Mega Drive (text executes from
ROM, only .data copied to RAM). The main limitation is that all user
programs load at a single USER_BASE address, so only one can occupy
RAM at a time. Pipelines execute sequentially.

---

## Phase 5: ROM Execute-in-Place (XIP) — Complete

**Goal:** Run program text directly from ROM. Only copy .data to RAM.
Reclaims ~70% of user memory currently wasted on code copies.

**Strategy:** Build-time resolved XIP via `romfix` (Strategy A from
[docs/relocatable-binaries.md](docs/relocatable-binaries.md)). See
the [Strategy Resolution](docs/relocatable-binaries.md#strategy-resolution-march-2026)
appendix for rationale.

### How It Works

1. Apps are compiled as relocatable binaries (linked at address 0)
2. `mkfs` places binaries in the ROM filesystem as normal
3. After the kernel+romdisk link, `romfix` post-processes the ROM:
   - Text references → absolute ROM addresses
   - Data references → USER_BASE addresses
   - Sets `GENIX_FLAG_XIP` flag
4. At exec() time, `load_binary_xip()` detects XIP binaries:

```c
/* Only copy .data to RAM — text stays in ROM */
memcpy(USER_BASE, rom_data_addr, data_size);
memset(USER_BASE + data_size, 0, bss_size);
setup_stack(USER_TOP, path, argv);
jmp(rom_text_entry);  /* entry is a ROM address */
```

### Impact

A typical program with 4 KB text and 2 KB data previously used 6 KB
of RAM. With XIP, only 2 KB goes to RAM. This roughly triples the
effective user memory budget on the Mega Drive (~27.5 KB available).

### Files Implemented

| File | Role |
|------|------|
| `tools/romfix.c` | Post-processes ROM to resolve XIP addresses in-place |
| `kernel/exec.c` | `load_binary_xip()` detects XIP flag, executes from ROM |
| `pal/pal.h` | `pal_rom_file_addr()` interface for ROM file lookup |
| `pal/megadrive/platform.c` | ROM file address lookup implementation |

### Limitation

Data address is fixed at build time (USER_BASE). Only one process can
use RAM data at a time. Addressed in Phase 6 with `-msep-data`.

---

## Phase 6: Concurrent Multitasking with `-msep-data` — Next

**Goal:** Multiple processes in memory simultaneously, sharing ROM
text. True concurrent pipelines.

**Depends on:** Phase 5 (ROM XIP).

**Mechanism:** GCC's `-msep-data` flag — all data access goes through
register a5 as base pointer. The kernel sets a5 to the process's data
slot on context switch. ROM text works for all processes unchanged.
See [docs/relocatable-binaries.md](docs/relocatable-binaries.md)
Strategy C2 analysis (lines 674-685).

### Why `-msep-data`

ROM text contains absolute text-to-data references that can only
point to one address. With multiple processes having data at different
RAM slots, these references break. `-msep-data` (the uClinux XIP
mode) solves this by making all data accesses indirect through a5:

```asm
; Without -msep-data (absolute — breaks with multiple data slots):
move.l  #my_global, a0        ; hardcoded USER_BASE in ROM text

; With -msep-data (a5-relative — works for any slot):
move.l  (offset,a5), a0       ; a5 set per-process on context switch
```

Cost: ~2 extra cycles per data access (one indirection). One register
(a5) is reserved. 5-10% code size overhead. Verified working with
m68k-elf-gcc 14.2.0.

### Impact on romfix

With `-msep-data`, text-to-data references are a5-relative (no
absolute address to patch). romfix simplifies to only patching
text-to-text and data-to-text references (function pointers, jump
tables). Fewer relocation entries overall.

### Memory Partitioning

With text in ROM, each process only needs RAM for .data + .bss +
stack. Divide user RAM into fixed-size slots:

```
Main RAM (27.5 KB user space):
  Slot 0:  0xFF9000 - 0xFFC5FF  (14 KB)
  Slot 1:  0xFFC600 - 0xFFFDFF  (14 KB)
```

Start with 2 large slots. Can split into 3-4 smaller slots later
if needed. Slot sizing is a runtime knob, not a structural decision.

### Implementation

1. Recompile all apps + libc with `-msep-data`
2. Update `apps/crt0.S` — kernel passes data base in a5 before
   jumping to user code; crt0 preserves it
3. romfix update — skip text-to-data reference patching (a5-relative)
4. Kernel slot allocator (~20 lines bitmap in mem.c)
5. exec() allocates slot, copies .data to slot base, sets a5
6. Context switch saves/restores a5 per process (~5 lines asm)
7. `struct proc` gets `mem_slot` field for slot tracking

### True Concurrent Pipelines

With multiple processes in memory, `cmd1 | cmd2` can run both sides
concurrently:
- cmd1 writes to pipe, blocks when full
- cmd2 reads from pipe, blocks when empty
- Scheduler switches between them as they block/wake

This replaces the current sequential pipeline execution.

### Estimated Size

~100-150 lines of kernel C (slot allocator, a5 setup, exec changes,
pipeline changes). Build system change is adding `-msep-data` to
the app and libc CFLAGS.

---

## Port dash Shell

**Goal:** Replace the built-in kernel shell with dash (Debian Almquist
Shell), a real POSIX shell with scripting, job control, and command
history.

**Depends on:** Phase 6 (`-msep-data` — dash is a normal app in its
own slot, no special memory treatment needed).

**Reference:** [docs/shell-research.md](docs/shell-research.md) — full
analysis of 8 shell candidates. [docs/shell-plan.md](docs/shell-plan.md)
— phased implementation plan.

### Why dash

dash is the smallest real POSIX shell (~13K SLOC, BSD licensed). With
XIP, its ~100 KB .text lives in ROM for free. Its .data+.bss is modest
(~6 KB). Full POSIX scripting (if/then/else, for, case, functions) and
job control are built in.

### Prerequisites (libc + kernel)

Before dash can be ported, Genix needs several general-purpose
improvements documented in [docs/shell-plan.md](docs/shell-plan.md):

- **Libc:** setjmp/longjmp, sigaction, POSIX headers (sys/types.h,
  sys/wait.h, sys/stat.h, limits.h, paths.h, time.h)
- **Kernel:** fcntl F_DUPFD, waitpid WNOHANG, POSIX-compatible stat

These are independently valuable regardless of the shell choice.

### RAM Budget

```
Available per slot (Mega Drive):  ~14 KB
Dash .data + .bss:                ~6 KB
Heap (parse trees, vars):         ~4 KB
Stack:                            ~2 KB
Total:                           ~12 KB  (fits in 14 KB slot)
```

### Configurable Shell Selection

Genix will support multiple hardware and emulator targets with different
RAM constraints. Dash (~12 KB data+heap+stack) fits in a 14 KB slot but
leaves only one slot for child programs. On memory-constrained targets,
this may not leave room for larger apps like levee.

The build system should make shell selection a per-target configuration:

- **Full shell (dash):** POSIX scripting, job control. For targets with
  enough RAM (PSRAM, large slots, or workbench).
- **Minimal shell (builtin or apps/sh):** The existing kernel shell
  extracted to userspace. Smaller data footprint, fewer features.

This is a build-time `SHELL=dash` / `SHELL=sh` knob in the Makefile,
not a runtime choice. Each target profile (workbench, Mega Drive bare,
Mega Drive + EverDrive Pro) selects its shell based on available memory.

**Research needed:** Measure dash's actual .data+.bss after porting to
determine exact slot pressure. If dash fits comfortably in 14 KB with
room for child apps, the minimal shell may only be needed for bare
Mega Drive without PSRAM. Defer this research until dash is compiled
and we have real numbers.

---

## Phase 7: SD Card Filesystem

**Goal:** Load programs and data from an SD card at runtime.

Two hardware targets with very different interfaces:

### Open EverDrive — Bit-Bang SPI

The Open EverDrive exposes the SD card's SPI bus directly through a
control register at `0xA130E0`. The 68000 must bit-bang SPI in
software.

- **Interface:** Single 8-bit register maps SPI signals (MOSI, MISO,
  CLK, CS) to individual bits
- **Throughput:** ~20-270 KB/s depending on access pattern
  - Auto-clock trick: reading the register auto-pulses CLK, roughly
    doubling read throughput
  - Multi-block reads (CMD18) are fastest
- **Implementation:** ~200-300 lines of 68000 assembly + C
  1. SPI layer: `spi_xfer()`, `spi_read()`, `spi_write()` bit-bang
  2. SD card init: CMD0, CMD8, ACMD41 (SPI mode)
  3. Block read/write: CMD17/CMD24 (single), CMD18/CMD25 (multi)
  4. FAT16 read support (to access files on standard SD cards)
  5. Integration: `/dev/sd0` block device or mount at `/sd`
- **Reference:** [docs/everdrive-sd-card.md](docs/everdrive-sd-card.md)

### Mega EverDrive Pro — FIFO Command Interface

The Pro has an FPGA with an onboard MCU that handles all SD and
filesystem operations. The 68000 sends high-level commands through a
FIFO mailbox at `0xA130D0`.

- **Interface:** Write command codes + parameters to FIFO, MCU
  executes, read results from FIFO
- **Commands:** `CMD_FILE_OPEN`, `CMD_FILE_READ`, `CMD_FILE_CLOSE`,
  `CMD_DIR_OPEN`, etc.
- **Throughput:** Much faster than bit-bang SPI — the MCU handles the
  low-level protocol
- **Implementation:** ~150-200 lines of C
  1. FIFO driver: read/write with status polling
  2. Pro detection: `(REG_SYS_STAT & 0xFFF0) == 0x55A0`
  3. File operations mapped to Genix VFS
  4. No FAT implementation needed (MCU handles it)
- **Reference:** [docs/everdrive-pro.md](docs/everdrive-pro.md)

### Runtime Relocation for SD-Loaded Binaries

Programs loaded from SD card need runtime relocation (they can't be
linked into the ROM). The relocation engine already exists in the
kernel (`apply_relocations()`), and mkbin already produces relocatable
binaries with relocation tables. SD-loaded programs use the same
Genix binary format as filesystem programs today.

---

## Phase 8: EverDrive Pro Extended Memory

**Goal:** Use the Pro's banked PSRAM for per-process text isolation,
enabling larger programs and more concurrent processes.

**Depends on:** Phase 6 (concurrent multitasking), Phase 7 (Pro FIFO
driver for detection).

### Hardware

The Pro provides 3.5 MB of writable PSRAM visible to the 68000 in
SSF mapper mode. The PSRAM is bank-switchable in 512 KB windows at
`0x200000-0x27FFFF`. Each process can get its own PSRAM bank for text
storage, with the FPGA switching banks on context switch.

### Implementation

1. **Detect Pro mode at boot** (~10 lines)
   - Check `REG_SYS_STAT` for Pro signature
   - Initialize SSF mapper

2. **PSRAM bank allocator** (~40 lines)
   - Track which 512 KB banks are in use
   - Allocate/free banks per process

3. **Per-process bank tracking** (~5 lines)
   - Add `sram_bank` field to `struct proc`
   - Set during exec(), freed during do_exit()

4. **Context switch bank register write** (~5 lines asm)
   - Write `0xA130Fx` with current process's bank before RTE
   - Only needed when switching between processes using different banks

5. **Split relocator for PSRAM text**
   - `apply_relocations_xip()` handles text at PSRAM bank address,
     data at main RAM address
   - Already implemented and tested (11 host tests)
   - See [docs/relocation-implementation-plan.md](docs/relocation-implementation-plan.md)
     Phase 7 for the full split XIP design

### Impact

With 512 KB per bank: a process's text can be up to 512 KB, vastly
exceeding current 27.5 KB limit. Levee (44 KB binary, currently too
large for Mega Drive) could run from PSRAM. Multiple large programs
can coexist with different bank mappings.

### Reference

[docs/everdrive-pro.md](docs/everdrive-pro.md) — full FPGA memory map,
register definitions, SSF mode details.

---

## Phase 9: Performance Optimizations

**Goal:** Bring 68000-specific assembly optimizations to hot paths.

These are performance gaps identified by comparing Genix's C
implementations against FUZIX's hand-optimized 68000 assembly. None
are correctness issues — Genix works correctly today. These are pure
speed improvements for when performance matters.

### Key Optimizations

| Optimization | Expected Speedup | Lines |
|-------------|-----------------|-------|
| Division fast path (DIVU.W for 16-bit divisors) | 2-5x division | ~20 |
| Assembly memcpy/memset (MOVEM.L bulk) | 4x block ops | ~40 |
| SRAM 16-bit I/O (word writes vs byte writes) | ~20x SRAM | ~10 |
| Pipe bulk copy (replace byte loop) | 2-4x pipe throughput | ~15 |
| VDP DMA for scroll/clear | ~10x scroll | ~30 |

### Approach

Measure first, optimize only hot paths. The workbench emulator
provides cycle counting for profiling. Optimize the inner loops that
show up in traces, leave cold paths in C.

### Reference

[OPTIMIZATION_PLAN.md](OPTIMIZATION_PLAN.md) — full analysis with
FUZIX source references, cycle counts, and implementation notes for
each optimization.

---

## Remaining Optional Work

Not prioritized, but would improve the system:

- **Larger programs**: ed (line editor), diff, sort, sed, awk
- **Development tools**: ar, make, small C compiler (from FUZIX)
- **kstack overflow guard**: canary word for debug builds
- **SA_RESTART**: auto-retry syscalls interrupted by signals

---

## Phase Dependencies

```
Phase 5 (ROM XIP) .............. done
    |
Phase 6 (-msep-data + slots) .. next
    |
Libc + kernel prereqs
    |
dash Shell Port
    |
Phase 7 (SD Card) ............. independent, can happen anytime
    |
Phase 8 (EverDrive Pro PSRAM) . depends on Phase 6 + 7
    |
Phase 9 (Performance) ......... independent, can happen anytime
```

Phase 6 is the critical next step. It unlocks concurrent processes
sharing ROM text, true concurrent pipelines, and enables dash as a
normal userspace app. Phase 7 (SD card) is independent.
