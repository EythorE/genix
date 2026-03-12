# Genix Forward Plan

What remains to be built. For project history and completed phases,
see [HISTORY.md](HISTORY.md).

Current state: Genix is a working preemptive multitasking OS with 34
user programs, relocatable binaries, pipes, signals, job control, and
a TTY subsystem. It runs on both the workbench emulator and real Mega
Drive hardware. The main limitation is that all user programs load at
a single USER_BASE address, so only one can occupy RAM at a time.
Pipelines execute sequentially.

---

## Phase 5: ROM Execute-in-Place (XIP)

**Goal:** Run program text directly from ROM. Only copy .data to RAM.
Reclaims ~70% of user memory currently wasted on code copies.

**Strategy:** Kernel-Linked Overlays (Strategy B from
[docs/relocatable-binaries.md](docs/relocatable-binaries.md)).

### How It Works

1. Compile each app+libc to a fully-linked relocatable object
   (`ld -r -o app_hello.o hello.o libc.a`)
2. The kernel linker script places all app `.text` and `.rodata` into
   ROM alongside the kernel:

```ld
.app_text : {
    _app_hello_text = .;
    apps/hello.o(.text .text.* .rodata .rodata.*)
    _app_hello_text_end = .;
    /* ... more apps ... */
} > rom

OVERLAY USER_BASE : AT(_app_data_load) {
    .hello_data {
        _app_hello_data_load = LOADADDR(.hello_data);
        apps/hello.o(.data .data.*)
        _app_hello_bss_start = .;
        apps/hello.o(.bss .bss.* COMMON)
        _app_hello_end = .;
    }
    /* ... more apps ... */
} > ram
```

3. The linker resolves all cross-references at link time: text-to-text
   gets ROM addresses, text-to-data gets USER_BASE addresses. No
   runtime relocation needed.
4. exec() becomes trivial:

```c
memcpy(USER_BASE, app->data_rom_addr, app->data_size);
memset(USER_BASE + app->data_size, 0, app->bss_size);
setup_stack(USER_TOP, path, argv);
jmp(app->entry);  /* entry is a ROM address */
```

### Impact

A typical program with 4 KB text and 2 KB data currently uses 6 KB of
RAM. With XIP, only 2 KB goes to RAM. This roughly triples the
effective user memory budget on the Mega Drive (~27.5 KB available).

### Files to Modify

| File | Change |
|------|--------|
| `pal/megadrive/megadrive.ld` | Add `.app_text` and OVERLAY sections |
| `apps/Makefile` | Build apps as relocatable objects (`ld -r`) |
| `kernel/exec.c` | Look up ROM metadata, memcpy .data, zero BSS, JMP |
| `kernel/kernel.h` | ROM app table structure |
| `kernel/main.c` | Register ROM app table at boot |

### Estimated Size

~60 lines of kernel C, ~50 lines of linker script, ~30 lines of
Makefile changes. Zero additional RAM cost beyond a small ROM app
table (~8 bytes per app, ~272 bytes for 34 apps).

### Build Flow Change

```
Before: app.c → app.o → app.elf → mkbin → genix binary → mkfs → romdisk
After:  app.c → app.o → ld -r with libc → app_linked.o → kernel link
```

### Limitations

- Adding or removing apps requires relinking the kernel ROM
- Can't load programs from SD card (they aren't in ROM)
- Still single USER_BASE for .data — one process at a time

### Testing

- All 34 apps must pass `make test-md-auto` from ROM
- Compare exec() speed (memcpy data vs full binary load)
- Verify levee still works on workbench (too large for MD ROM)

---

## Phase 6: Concurrent Multitasking with Shared ROM Text

**Goal:** Multiple processes in memory simultaneously, sharing ROM
text. True concurrent pipelines.

**Depends on:** Phase 5 (ROM XIP).

### Memory Partitioning

With text in ROM, each process only needs RAM for .data + .bss +
stack. Divide user RAM into fixed-size slots:

```
Main RAM (27.5 KB user space):
  Slot 0:  0xFF9000 - 0xFFABFF  (7 KB)
  Slot 1:  0xFFAC00 - 0xFFC7FF  (7 KB)
  Slot 2:  0xFFC800 - 0xFFE3FF  (7 KB)
  Slot 3:  0xFFE400 - 0xFFFDFF  (6.5 KB — last slot before stack)
```

Each process gets one slot for its .data + .bss. All processes share
the same ROM text. This enables 3-4 concurrent processes on the Mega
Drive with no SRAM.

### Implementation

- Add `mem_slot` field to `struct proc`
- Bitmap allocator for slots (~20 lines)
- exec() allocates a slot, copies .data to slot base, zeros BSS
- Relocatable .data: since each slot has a different base address,
  data-to-data references need runtime relocation. Use the existing
  relocation engine (`apply_relocations()`) for the .data segment
  only, with text references resolved at link time
- Stack per process within the slot (or at slot top, growing down)

### True Concurrent Pipelines

With multiple processes in memory, `cmd1 | cmd2` can run both sides
concurrently:
- cmd1 writes to pipe, blocks when full
- cmd2 reads from pipe, blocks when empty
- Scheduler switches between them as they block/wake

This replaces the current sequential pipeline execution.

### Shell as ROM Program

The built-in kernel shell could be replaced with a real shell running
from ROM. With text in ROM, the shell uses only ~1-2 KB of RAM for
its data segment, freeing user memory for actual commands. A ROM shell
can be much more capable (glob expansion, environment variables,
background jobs) without the RAM penalty.

### Estimated Size

~100-150 lines of kernel C (slot allocator, exec changes, pipeline
changes). The relocation engine already exists.

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

5. **Split relocator becomes critical**
   - `apply_relocations_xip()` handles text at PSRAM bank address,
     data at main RAM address
   - Already implemented and tested (11 host tests)

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

- **Glob expansion** in shell (`*`, `?`, `[...]`)
- **Environment variable substitution** (`$HOME`, `$PATH`)
- **Background jobs** (`&`, `jobs`, `fg`, `bg`)
- **Larger programs**: ed (line editor), diff, sort, sed, awk
- **Development tools**: ar, make, small C compiler (from FUZIX)
- **kstack overflow guard**: canary word for debug builds
- **SA_RESTART**: auto-retry syscalls interrupted by signals

---

## Phase Dependencies

```
Phase 5 (ROM XIP)
    ↓
Phase 6 (Concurrent Multitasking)  ←  Phase 7 (SD Card)
    ↓                                      ↓
Phase 8 (EverDrive Pro PSRAM)        (runtime relocation)
    ↓
Phase 9 (Performance) — independent, can happen anytime
```

Phase 5 is the critical next step. It unlocks Phase 6 (concurrent
processes sharing ROM text) and dramatically improves the Mega Drive
user experience by tripling available RAM. Phase 7 (SD card) is
independent and can proceed in parallel.
