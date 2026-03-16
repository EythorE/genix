# Hardware Phase Plans (SD Card + EverDrive Pro PSRAM)

These phases require real EverDrive hardware for implementation and
testing. Moved out of PLAN.md to keep the forward plan focused on
software work.

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
- **Reference:** [../research/everdrive-research.md](../research/everdrive-research.md)

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
- **Reference:** [../research/everdrive-research.md](../research/everdrive-research.md)

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
   - See [relocation-plan.md](relocation-plan.md) Phase 7 for the
     full split XIP design

### Impact

With 512 KB per bank: a process's text can be up to 512 KB, vastly
exceeding current 27.5 KB limit. Levee (44 KB binary, currently too
large for Mega Drive) could run from PSRAM. Multiple large programs
can coexist with different bank mappings.

### Reference

[../research/everdrive-research.md](../research/everdrive-research.md) — full FPGA memory map,
register definitions, SSF mode details.
