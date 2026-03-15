# Genix Forward Plan

What remains to be built. For project history and completed phases,
see [HISTORY.md](HISTORY.md).

Current state: Genix is a working preemptive multitasking OS with 47
user programs (including dash shell and 13 tier-1 utilities),
relocatable binaries, pipes, signals, job control, and a TTY
subsystem. It runs on both the workbench emulator and real Mega Drive
hardware. ROM XIP is working on Mega Drive (text executes from ROM,
only .data copied to RAM). Phase 6 (`-msep-data` + slot allocator) is
complete: multiple processes can reside in memory simultaneously with
shared ROM text and per-process data slots. Pipelines execute
concurrently. Phase A (libc prerequisites), Phase B (kernel
enhancements: fcntl F_DUPFD, waitpid WNOHANG), Phase C (dash shell
port), Phase D (line editing for dash), and Tier 1 apps (cp, mv, rm,
mkdir, touch, kill, which, uname, clear, more, sort, find, xargs) are
complete. The kernel spawns dash as the default interactive shell with
arrow key cursor movement, command history (up/down), and in-line
editing. The next step is Phase 7 (SD card filesystem). See
[docs/apps_to_port.md](docs/apps_to_port.md) for the app porting
roadmap.

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

## Phase 6: Concurrent Multitasking with `-msep-data` — Complete

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

### Outcome

**Implemented:** 2026-03-14. 11 files changed, +378/-81 lines.

All implementation steps completed as planned. The slot allocator,
`-msep-data` compilation, GOT-aware relocation, romfix data-reloc
deferral, and concurrent pipeline execution all work on both platforms.

**Deviations from plan:**

1. **mkbin synthetic GOT relocations:** `--emit-relocs` does not emit
   relocations for linker-generated GOT entries. mkbin was extended to
   scan the `.got` section and add synthetic R_68K_32 relocations for
   each non-zero entry. This was not anticipated in the plan.

2. **GOT offset encoding:** The naive `got_offset=0` encoding was
   ambiguous (0 meant both "no GOT" and "GOT at data start"). Changed
   to offset+1 encoding with `HDR_HAS_GOT`/`HDR_GOT_OFFSET` macros.

3. **Text alignment:** `.text` sections could end at odd sizes, causing
   the data section (and GOT) to start at odd addresses. Added
   `ALIGN(4)` at end of `.text` in the linker script.

**Gotchas:**

- The `-msep-data` flag must NOT be passed to assembly files (crt0.S,
  syscalls.S, divmod.S). Required separate `ASFLAGS` in Makefiles.
- romfix must keep the relocation table intact (don't zero reloc_count)
  for `-msep-data` binaries, since the kernel needs it at runtime.
- `exec_user_a5` global is set by `do_exec()` before `exec_enter()`.
  For spawned processes, a5 is baked into the kstack register frame.

**Update (2026-03-15):** Fixed-slot allocator replaced with variable-size
user memory allocator (`umem_alloc`). The slot allocator's equal-size
division wasted 97% of RAM for small programs and limited Mega Drive to
2 concurrent processes. The new allocator scans proctab to find gaps,
allocating exactly what each process needs. See
[docs/decisions.md](docs/decisions.md) "Fixed-Slot Allocator Oversight"
and [docs/memory-system.md](docs/memory-system.md) for details.

**Known weak spots** (not bugs today, but places to look if issues arise):

1. ~~**`slot_base()` returns 0 on invalid slot index**~~ Resolved: slot
   allocator removed. `umem_alloc` returns 0 on failure, which callers
   check before proceeding.

2. **No duplicate reloc guard in mkbin GOT scanning** (`mkbin.c`). mkbin
   adds synthetic R_68K_32 relocs for each non-zero GOT entry. If a future
   GCC/binutils version starts emitting GOT relocs via `--emit-relocs`,
   the same offset would appear twice in the reloc table. The kernel
   relocator would apply the relocation twice, corrupting the value. A
   dedup check (or sorted-merge) would prevent this.

3. **`exec_user_a5` is a global** (`exec.c`). It's set by `do_exec()`
   and consumed by `exec_enter()` in assembly. This is safe because
   `do_exec()` is synchronous (no preemption between set and use), and
   spawned processes use the kstack register frame path instead. But if
   exec ever becomes reentrant or interruptible between the assignment
   and `exec_enter()`, this would be a race. The spawned-process path
   (`proc_setup_kstack` baking a5 into the kstack frame) is the robust
   pattern.

4. **GOT offset packed into `stack_size` upper 16 bits** (`kernel.h`).
   This limits both stack size hint and GOT offset to 64 KB each. Fine
   for current binaries (largest data section is ~6 KB), but would need
   rethinking if binaries grow past 64 KB data. The binary format header
   has no spare fields, so growing this would require a header version
   bump.

5. ~~**`do_exec` allocates slot before file lookup**~~ Resolved: `do_exec`
   now reads the binary header first (to compute memory needs), then
   allocates. File-not-found returns -ENOENT before any allocation.

**Measured results:**

- Workbench: variable-size regions from 704 KB pool
- Mega Drive: variable-size regions from 27.5 KB pool (XIP: only data+bss+stack in RAM)
- Binary size overhead: ~2-7% (GOT + extra relocs)
- hello: 616 bytes text+data, 3 relocs (was 1 without GOT relocs)
- levee: 46336 bytes text+data, 2560 relocs (was 2533)

---

## Port dash Shell — Complete

**Goal:** Replace the built-in kernel shell with dash (Debian Almquist
Shell), a real POSIX shell with scripting.

**Depends on:** Phase 6 (`-msep-data` — dash is a normal app in its
own slot, no special memory treatment needed).

**Reference:** [docs/shell-research.md](docs/shell-research.md) — full
analysis of 8 shell candidates. [docs/shell-plan.md](docs/shell-plan.md)
— phased implementation plan with outcome report.

### Outcome

**Implemented:** 2026-03-14. dash 0.5.12 ported as a normal userspace
application. Configured with JOBS=0, SMALL=1, no line editing.

**Binary size:** text=91,236 data=4,500 bss=2,296. Data+bss = 6,796
bytes (49% of 14 KB Mega Drive slot). Fits comfortably — the ~6 KB
estimate was accurate.

**RAM budget (measured):**
```
Available per slot (Mega Drive):  ~14 KB
Dash .data + .bss:                6,796 bytes (49%)
Remaining for heap + stack:       ~7 KB
```

**Libc additions:** 7 new headers, 15+ new functions (strtoll, strtoull,
vsnprintf, strtod, abort, strpbrk, stpncpy, strsignal, isblank, isgraph,
ispunct, isxdigit, etc.). The libc grew significantly to support dash.

**Integration:** kernel/main.c spawns `/bin/dash` with respawn loop,
falls back to `builtin_shell()` if not found. dash is in CORE_BINS
(included in both workbench and Mega Drive images).

**Shell selection:** Since dash data+bss is only 6.8 KB (49% of slot),
the configurable shell selection mechanism is not needed for Mega Drive.
Dash fits comfortably alongside child processes. A build-time knob
could be added later if a more constrained target emerges.

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

## Phase D: Line Editing for dash — Complete

**Goal:** Arrow key history and basic line editing for interactive use.

**Depends on:** Phase C (dash port — complete), TTY raw mode (complete).

**Reference:** [docs/shell-plan.md](docs/shell-plan.md) Phase D section.

### What was built

Standalone library `libc/lineedit.c` (~300 lines) with `lineedit.h` API:
- Cursor movement: left/right arrows, Home (^A), End (^E)
- Editing: backspace, delete, kill line (^U), insert anywhere
- History: 16-entry ring buffer, up/down navigation, scratch line save
- Key reader: ANSI escapes (ESC [ A/B/C/D/H/F, ESC [ 3 ~),
  Mega Drive Saturn keyboard raw keycodes, control characters (^A/^B/^E/^F/^H/^U)
- Display: `\b` + character writes only (no ANSI cursor positioning),
  works on both VDP console and UART terminals
- Terminal safety: saves/restores termios, ISIG stays on for ^C/^Z
- ^D: EOF on empty line, delete-forward with content

Integrated into dash via `preadfd()` in `input.c` — interactive input
on fd 0 uses `le_readline()`, piped/file input uses plain `read()`.

### RAM usage (Mega Drive)

```
History buffer:  2,048 bytes (16 x 128)
Line buffer:       256 bytes
Scratch buffer:    256 bytes
Termios save:       16 bytes
Total RAM:       ~2.6 KB
Code (text):      ~2 KB (in ROM via XIP)
```

### Known limitations (v1)

1. ESC alone blocks on workbench (needs VTIME or O_NONBLOCK)
2. No line wrapping past terminal width (content correct, display garbled)
3. Delete = Backspace on workbench (emulator translates 0x7F→0x08)
4. History truncates lines >127 chars (edit buffer allows 255)

---

## Remaining Optional Work

Not prioritized, but would improve the system:

- **Tier 1 remaining**: ed (line editor — only editor if levee too tight)
- **Tier 2 games**: hamurabi, dopewars, startrek, adventure, tetris, snake
- **Tier 3 text processing**: sed, diff, cal, date (needs localtime libc)
- **Tier 4 languages**: BASIC interpreter, Forth, fweep (Z-machine)
- **Development tools**: ar, make, small C compiler (from FUZIX)
- **SA_RESTART**: auto-retry syscalls interrupted by signals

See [docs/apps_to_port.md](docs/apps_to_port.md) for the complete
app porting roadmap with RAM analysis and wave breakdown.

---

## Phase Dependencies

```
Phase 5 (ROM XIP) .............. done
    |
Phase 6 (-msep-data + slots) .. done
    |
Libc prereqs (Phase A) ....... done
    |
Kernel prereqs (Phase B) ..... done
    |
dash Shell Port (Phase C) .... done
    |
Line Editing (Phase D) ....... done
    |
Phase 7 (SD Card) ............. next (independent of D)
    |
Phase 8 (EverDrive Pro PSRAM) . depends on Phase 6 + 7
    |
Phase 9 (Performance) ......... independent, can happen anytime
```

All prerequisites and the dash shell port are complete. Phase 7 (SD
card) is the next major feature. Phase 9 (performance) can happen
anytime.
