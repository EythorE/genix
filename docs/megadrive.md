# Mega Drive Target

The Sega Mega Drive is the primary hardware target for Genix. The workbench
emulator exists only to accelerate development — every feature must ultimately
work on real Mega Drive hardware.

## Hardware Summary

| Component | Specification |
|-----------|---------------|
| CPU | Motorola 68000 @ 7.67 MHz (NTSC) / 7.60 MHz (PAL) |
| Main RAM | 64 KB (0xFF0000–0xFFFFFF) |
| ROM | Up to 4 MB cartridge (0x000000–0x3FFFFF) |
| SRAM | Optional, cartridge-dependent (0x200000 range) |
| Video | VDP, 40×28 text mode via 8×8 font tiles |
| Input | Saturn keyboard on controller port 2 |
| Sound CPU | Z80 @ 3.58 MHz (halted by Genix, bus released) |

## Memory Map

### Address Space Overview

```
0x000000 ┌──────────────────────┐
         │ ROM: Vectors+Header  │ 512 bytes
0x000200 ├──────────────────────┤
         │ ROM: Kernel .text    │ ~15 KB
         │ ROM: .rodata (font)  │ ~3 KB
         ├──────────────────────┤
         │ ROM: Embedded FS     │ .romdisk (read-only filesystem)
         │ (size varies)        │
         └──────────────────────┘

0x200000 ┌──────────────────────┐  ← Cartridge SRAM (if present)
         │ SRAM disk and/or     │  Size varies: 8KB, 32KB, 64KB,
         │ extended user RAM    │  256KB, 512KB, 1MB, 2MB
0x2XXXXX └──────────────────────┘

0xFF0000 ┌──────────────────────┐  ← Main 68000 RAM (always 64 KB)
         │ Kernel .data         │  80 bytes (copied from ROM at boot)
         │ Kernel .bss          │  ~25 KB (static tables)
         ├──────────────────────┤  ← _end (~0xFF62CC)
         │ Kernel heap          │  ~1 KB (kmalloc)
         ├──────────────────────┤  ← USER_BASE (~0xFF6800)
         │ User program         │  text + data + BSS (loaded by exec)
         │ (~32 KB available)   │
         ├──────────────────────┤
         │ User stack           │  grows down from USER_TOP
         ├──────────────────────┤  ← USER_TOP (~0xFFFE00)
         │ Kernel stack         │  ~512 bytes (grows down)
0xFFFFFF └──────────────────────┘
```

### Current RAM Budget (64 KB)

Measured from the genix-md.elf build:

| Region | Size | Address Range |
|--------|------|---------------|
| Kernel .data | 80 B | 0xFF0000–0xFF004F |
| Kernel .bss | 25,212 B | 0xFF0050–0xFF62CB |
| **Total kernel** | **25,292 B** | |
| **Free for heap + user** | **~40 KB** | 0xFF62CC–0xFFFFFF |

BSS breakdown (largest consumers):

| Structure | Size | Purpose |
|-----------|------|---------|
| `bufs[16]` | 16,480 B | Block buffer cache (16 × 1 KB + headers) |
| `inode_cache[128]` | 5,376 B | In-memory inode table |
| `proctab[16]` | 2,496 B | Process table |
| `ofile_table[64]` | 768 B | Open file descriptors |

**Key insight:** With 40 KB free, user programs can run entirely in main
RAM without requiring SRAM. This means SRAM is optional — it provides
persistent storage and extra RAM but is not required for basic operation.

## Cartridge Configurations

Genix must run on different cartridge types. The memory layout is
configurable via platform constants to support various hardware.

### Configuration A: ROM-only (no SRAM)

Simplest cartridge. ROM filesystem is read-only.

```
ROM:  Kernel + read-only filesystem
RAM:  Kernel BSS + heap + user programs
SRAM: None
```

- User programs: loaded into main RAM (~32 KB available)
- Filesystem: read-only (embedded in ROM)
- No persistent storage — useful for demos, fixed installations

### Configuration B: ROM + Small SRAM (8–64 KB)

Standard game cartridge with battery-backed save RAM.

```
ROM:  Kernel + read-only filesystem
RAM:  Kernel BSS + heap + user programs
SRAM: Read-write filesystem (small)
```

- User programs: loaded into main RAM
- SRAM used for writable filesystem (scratch files, config)
- SRAM access: byte-at-odd-addresses (hardware quirk)

### Configuration C: ROM + Large SRAM (256 KB–2 MB)

Flash cartridge or custom PCB with large SRAM. This is what FUZIX uses.

```
ROM:  Kernel code + read-only filesystem
RAM:  Kernel BSS + heap
SRAM: User programs + read-write filesystem
```

- With large SRAM, user programs can load into SRAM instead of main RAM
- More room for larger programs
- SRAM filesystem can hold the full Unix directory tree

### Configuration D: Open EverDrive

The [Open EverDrive](https://github.com/krikzz/open-ed) is a budget,
fully open-source (MIT) flash cartridge by Krikzz (~$30). No FPGA or
CPU — just discrete 74HC logic, 8 MB NOR flash, 128 KB battery-backed
SRAM, and an SD card slot.

```
ROM:  Up to 4 MB (kernel + read-only filesystem)
RAM:  Kernel BSS + heap + user programs (64 KB main RAM)
SRAM: 128 KB at 0x200000–0x3FFFFF (battery-backed, persistent)
```

- **Mapper: NOT standard Sega.** The Open EverDrive has its own mapper
  with a 16-bit control register at `0xA13000` (see
  [open-ed-mapper.txt](https://github.com/krikzz/open-ed/blob/master/open-ed-mapper.txt)).
- CTRL_REG at `0xA13000` (16-bit write):
  - Bit 0 (`SRM_ON`): `0` → ROM visible at 0x200000, `1` → SRAM visible
  - Bit 2: `ROM_BANK` (flash bank select)
  - Bit 3: LED, Bits 4-7: SPI/SD card interface
- Three mapper modes (selected by the open-ed menu firmware):
  - `ROM_4M`: No SRAM, SRM_ON=0, no register management needed
  - `ROM_2M+RAM`: SRAM always on at 0x200000, no register management needed
  - `ROM_4M+RAM`: Software-controlled via CTRL_REG (Beyond Oasis, Sonic 3 style)
- **16-bit SRAM** — the Open EverDrive wires SRAM to both data bus halves,
  so full word/long access works. No odd-byte quirk.
- 128 KB physical = 128 KB usable (unlike 8-bit SRAM where half is wasted)
- This is the same SRAM wiring FUZIX targets (ROM header type `0xE020`,
  range `0x200000-0x3FFFFF`)
- SD card slot available for future larger storage

**SRAM enable for Genix on Open EverDrive:**
```c
/* Open EverDrive: set SRM_ON bit in CTRL_REG */
*(volatile uint16_t *)0xA13000 = 0x0001;
```

**Hardware reference:** Schematics, PCB layout, Gerbers, and firmware
source are all in the [open-ed repository](https://github.com/krikzz/open-ed).

### Configuration E: Mega EverDrive Pro

The [Mega EverDrive Pro](https://krikzz.com/our-products/cartridges/mega-everdrive-pro.html)
is an FPGA-based flash cartridge (Cyclone IV FPGA, 16 MB PSRAM, 1 MB SRAM)
with extensive mapper emulation.

```
ROM:  Up to 16 MB (loaded from SD card into FPGA PSRAM)
RAM:  Kernel BSS + heap + user programs (64 KB main RAM)
SRAM: Up to 512 KB at 0x200000 (FPGA-backed, saved to SD on power-off)
```

The Pro supports **two mapper modes**, selected by the ROM header system
type field at offset 0x100:

**Traditional mode** (system type = `SEGA MEGA DRIVE` or `SEGA GENESIS`):
- Behaves like a real cartridge with standard Sega mapper
- For ROMs < 2 MB: SRAM is always mapped at 0x200000 — no enable needed
- For ROMs >= 2 MB: enable SRAM via standard `0xA130F1 = 0x01`
- 8-bit odd-byte access, up to 64 KB address space (32 KB usable)
- **This is what Genix uses** — the ROM is ~543 KB, so SRAM just works

**SSF mode** (system type = `SEGA SSF`):
- Extended mapper with 8 bank registers at `0xA130F0`–`0xA130FE`
- Each register maps a 512 KB bank into the address space
- SRAM lives at bank 31 — write `0x001F` to CTRL4 (`0xA130F8`)
  to map it at 0x200000
- Word-wide access (no odd-byte quirk), up to 512 KB
- CTRL0 (`0xA130F0`) format: P=protection bit (must be set),
  W=write-enable, L=LED, C=#CART
- Must disable interrupts and halt Z80 before bank switching
- See [Krikzz extended SSF spec](https://krikzz.com/pub/support/mega-everdrive/pro-series/extended-ssf.txt)

```c
/* SSF mode: unlock + map SRAM bank */
*(volatile uint16_t *)0xA130F0 = 0xA000;  /* P + W bits (unlock, write-enable) */
*(volatile uint16_t *)0xA130F8 = 0x001F;  /* CTRL4 = bank 31 (SRAM) at 0x200000 */
/* Now 0x200000-0x27FFFF is 512 KB word-wide SRAM */
```

**For Genix, SSF mode is not needed** — traditional mode with the standard
Sega header works. SSF would only be useful if we needed >32 KB of SRAM
or word-wide access on the Pro.

### Configuration F: Mega EverDrive X-series (X3/X5/X7)

The older X-series EverDrives use DRAM-based save RAM:

- 64 KB DRAM mapped at 0x200000 for save RAM
- Saved to SD card (EDMD/SAVE/ folder) — battery backup only on X7
- Same SSF bank registers as the Pro, but bank 31 gives only 256 KB
- Standard `0xA130F1 = 0x01` works for ROMs < 2 MB (same as Pro)
- **USB loading on X7 breaks SRAM** — must load ROMs from SD card

### SRAM Enable Summary

Each cartridge type requires a different enable sequence:

| Target | Register | Write | Notes |
|--------|----------|-------|-------|
| **Standard cart** | `0xA130F1` (8-bit) | `0x03` | Bit 0=mapped, bit 1=write-enable |
| **Open EverDrive** | `0xA13000` (16-bit) | `0x0001` | Bit 0=SRM_ON |
| **EverDrive Pro (traditional)** | `0xA130F1` (8-bit) | `0x01` | Same as standard; auto-mapped for ROM < 2 MB |
| **EverDrive Pro (SSF)** | `0xA130F0` (16-bit) | `0xA000` + CTRL4=31 | Extended mapper; needs `SEGA SSF` header |
| **BlastEm** | Any / none | — | Auto-detects from ROM header |

**Good news for Genix:** The current standard mapper code (`0xA130F1 = 0x03`)
works on real cartridges, all EverDrives (in traditional mode), and BlastEm.
Since the Genix ROM is < 2 MB, SRAM is auto-mapped on EverDrive hardware
and the register write is harmless.

The only target that needs different code is the **Open EverDrive in
`ROM_4M+RAM` mode** (register `0xA13000`). In `ROM_2M+RAM` mode (the
likely auto-selected mode for Genix), SRAM is always on and the standard
write is harmless.

## Platform Constants

The memory layout is controlled by constants that the PAL layer defines.
These must be adjusted per cartridge configuration:

```c
/* In pal/megadrive/platform.c or via build flags */

/* Where user programs are loaded */
#define USER_BASE    <platform-specific>
#define USER_TOP     <platform-specific>

/* SRAM configuration */
#define SRAM_BASE    0x200000
#define SRAM_SIZE    <cartridge-dependent>
```

For the current default (user programs in main RAM):
```
USER_BASE = _end rounded up (after kernel BSS)
USER_TOP  = 0xFFFE00 (leave room for kernel stack)
```

For large-SRAM configurations (user programs in SRAM):
```
USER_BASE = 0x200000 (start of SRAM)
USER_TOP  = 0x200000 + SRAM_SIZE
```

## SRAM Hardware Details

### 8-bit vs 16-bit SRAM

Cartridge SRAM comes in two wiring configurations that fundamentally
change how the CPU accesses data:

**8-bit SRAM (standard game cartridges):**

The SRAM chip connects to only one half of the 68000's 16-bit data bus
(typically the low byte, accessible at odd addresses). To read/write N
bytes of data, you must access 2N addresses:

```c
volatile uint8_t *sram = (volatile uint8_t *)SRAM_BASE;

/* Reading: skip even bytes */
for (int i = 0; i < len; i++)
    dst[i] = sram[i * 2 + 1];

/* Writing: skip even bytes */
for (int i = 0; i < len; i++)
    sram[i * 2 + 1] = src[i];
```

This means 64 KB of address space provides only 32 KB of usable storage.
The ROM header declares this as type `0xF820` (battery-backed, 8-bit, odd
addresses). This is what the current Genix code uses.

**16-bit SRAM (Open EverDrive, FUZIX target):**

The SRAM chip connects to both halves of the data bus. Full word and long
access works normally — no odd-byte dance needed. `memcpy()` works directly.

```c
/* 16-bit SRAM: direct access, no byte skipping */
memcpy(dst, (void *)SRAM_BASE + offset, len);
```

128 KB of physical SRAM = 128 KB usable. The ROM header declares this
as type `0xE020` (battery-backed, 16-bit). This is what FUZIX uses for
the Open EverDrive.

**Summary:**

| Property | 8-bit SRAM | 16-bit SRAM |
|----------|-----------|------------|
| ROM header type | `0xF820` | `0xE020` |
| ROM header range | `0x200001-0x20FFFF` | `0x200000-0x3FFFFF` |
| Access pattern | Odd bytes only | Normal word/long |
| 64 KB address space yields | 32 KB usable | 64 KB usable |
| Cartridge examples | Standard game carts | Open EverDrive |
| Genix code | `platform.c` (current) | Needs separate path |

**The code must handle both cases.** The current `pal_disk_read()`/
`pal_disk_write()` in `platform.c` use 8-bit odd-byte access. Supporting
16-bit SRAM (Open EverDrive) requires a build-time or runtime switch.

### SRAM Enable Sequences

See the [SRAM Enable Summary](#sram-enable-summary) table above for
per-cartridge register writes. The standard mapper sequence:

```c
/* Standard mapper (0xA130F1) */
volatile uint8_t *sram_reg = (volatile uint8_t *)0xA130F1;
*sram_reg = 0x03;  /* bit 0: SRAM mapped, bit 1: write-enable */

/* To write-protect: */
*sram_reg = 0x01;  /* mapped but read-only */

/* To unmap (restore ROM access at 0x200000): */
*sram_reg = 0x00;
```

## Boot Sequence

1. CPU reads SSP from vector 0 and PC from vector 1
2. `_md_start` (crt0.S):
   - Disable interrupts (`move.w #0x2700, %sr`)
   - TMSS handshake (write "SEGA" to 0xA14000)
   - Set stack pointer
   - Halt Z80 and release bus
   - Copy .data from ROM to RAM
   - Clear BSS
   - Jump to `kmain()`
3. `kmain()` initializes all subsystems, drops into shell

### Real Hardware Considerations

Things that emulators forgive but real hardware does not:

| Issue | Emulator | Real Hardware |
|-------|----------|---------------|
| Z80 not halted | Works fine | Bus conflicts, random crashes |
| SRAM not enabled | BlastEm auto-enables | No SRAM access |
| I/O ports not initialized | Works | Keyboard/controller fails |
| ROM checksum wrong | Ignored | Some consoles reject ROM |
| Odd-address word access | May work | Instant address error |
| TMSS handshake skipped | Works | Black screen on Model 1+ |

## Testing

### Host Tests

```bash
make test    # Logic tests, no cross-compiler needed
```

### Workbench Emulator

```bash
make run     # Full system in terminal emulator
```

The workbench emulator is for rapid iteration. It tests kernel logic
and syscall behavior but NOT:
- VDP rendering
- Keyboard input handling
- SRAM access patterns
- 68000 timing
- Real interrupt behavior

### BlastEm (Mega Drive Emulator)

BlastEm is the most accurate open-source Mega Drive emulator and should
be used to validate the ROM before testing on hardware.

```bash
make megadrive
blastem pal/megadrive/genix-md.bin
```

#### Saturn Keyboard Setup

Genix requires a Saturn keyboard on controller port 2. BlastEm emulates
this — host keypresses are forwarded as Saturn keyboard scancodes.

**One-time setup (BlastEm GUI):**
1. Launch BlastEm with any ROM
2. Press **Escape** to open the menu
3. Go to **Settings → System**
4. Set **IO Port 2** to **Saturn Keyboard**
5. Exit settings (saved automatically)

**Or edit the config file directly** (`~/.config/blastem/blastem.cfg`):
```
io {
    devices {
        1 gamepad6.1
        2 saturn keyboard
    }
}
```

**Using the keyboard:** Press **Right Ctrl** to toggle keyboard capture.
When captured, all keypresses go to the emulated Saturn keyboard instead
of BlastEm's own hotkeys. Press Right Ctrl again to release.

#### Headless Testing (`make test-md`)

BlastEm 0.6.3+ has a built-in headless mode:

```bash
blastem -b FRAMES rom.bin    # run FRAMES frames, then exit (no window)
```

The top-level Makefile provides:

```bash
make test-md                 # build ROM + boot headless for ~5s
```

The `test-md` target auto-detects BlastEm capabilities: uses `-b 300`
(~5 seconds at 60 fps) if available, otherwise falls back to Xvfb.
Catches address errors, illegal instructions, and bus faults that only
appear in the Mega Drive build.

Useful BlastEm flags:

| Flag | Purpose |
|------|---------|
| `-b N` | Headless: run N frames then exit (no display) |
| `-D` | GDB remote debugging (pipe mode) |
| `-d` | Enter integrated debugger |
| `-g` | Disable OpenGL |
| `-r (J\|U\|E)` | Force region (NTSC-J, NTSC-U, PAL) |
| `-n` | Disable Z80 |
| `-e FILE` | Write hardware event log |

#### BlastEm Configuration

Config file location: `~/.config/blastem/blastem.cfg` (or
`/etc/blastem/default.cfg` for system-wide defaults).

```
io {
    devices {
        1 gamepad6.1
        2 saturn keyboard
    }
}

video {
    gl off
    vsync off
}
```

Disable GL if running in a terminal-only environment without `-b`.

#### BlastEm SRAM Handling

BlastEm reads the ROM header SRAM fields at offset `0x1B0` to
auto-detect SRAM presence, type, and address range. No explicit SRAM
enable register write is needed — BlastEm maps SRAM automatically
(unlike real hardware where `0xA130F1` must be written).

**Save file location:**
`~/.local/share/blastem/<ROMNAME>/save.sram`

**Format:** Raw binary dump of the full SRAM address range. BlastEm
saves both even and odd bytes (word-wide), even for 8-bit SRAM ROMs.
This differs from most other emulators and flash carts which only save
the accessible bytes.

**Cross-compatibility:** BlastEm `.sram` files may not transfer
directly to EverDrive or other emulators. For 8-bit SRAM ROMs, other
tools expect only the odd bytes. Use the `ucp -b` tool (from FUZIX) to
create byte-reversed filesystem images for BlastEm if needed.

#### BlastEm GDB Debugging

```bash
m68k-linux-gnu-gdb -q --tui \
    -ex "target remote | blastem -D pal/megadrive/genix-md.bin" \
    pal/megadrive/genix-md.elf
```

### Real Hardware

#### Open EverDrive

1. Copy `pal/megadrive/genix-md.bin` to the SD card
2. Insert SD card into the Open EverDrive cartridge
3. Boot — the open-ed menu loads, select the ROM
4. The menu selects the mapper mode based on ROM header. For Genix
   (ROM < 2 MB + SRAM declared), it should select `ROM_2M+RAM` mode
   which makes SRAM always visible at 0x200000.
5. Connect a Saturn keyboard to controller port 2

**Note:** The current Genix SRAM enable code writes to `0xA130F1`
(standard Sega mapper), which is a different register than the Open
EverDrive's `0xA13000`. In `ROM_2M+RAM` mode SRAM is always on, so the
standard write is harmless (it hits an unrelated address). But for
`ROM_4M+RAM` mode, the code would need to write to `0xA13000` instead.

#### Mega EverDrive Pro

1. Copy `pal/megadrive/genix-md.bin` to the SD card
2. Insert SD card into the Mega EverDrive Pro
3. Boot and select the ROM from the menu
4. Connect a Saturn keyboard to controller port 2

The Pro reads the ROM header system type (`SEGA MEGA DRIVE` at offset
0x100) and uses **traditional mapper mode**. Since the Genix ROM is
< 2 MB, SRAM at 0x200000 is auto-mapped — the standard `0xA130F1`
write in `platform.c` is compatible. SRAM is saved to the SD card
automatically when you power off or press reset.

#### What to Verify on Real Hardware

Things that emulators forgive but real hardware does not:
- TMSS handshake works (no black screen on Model 1+)
- Z80 halt/release sequence is correct (bus conflicts crash)
- SRAM enable works with the specific cartridge mapper
- Saturn keyboard reads work at hardware timing
- VBlank timing is correct for NTSC vs PAL
- ROM checksum (currently 0 — some consoles may reject)

## ROM Header

The ROM header at offset 0x100 identifies the cartridge:

```
0x100: "SEGA MEGA DRIVE "  (system type, 16 bytes)
0x110: "(C)2025 GENIX   "  (copyright, 16 bytes)
0x120: "GENIX OS        "  (domestic name, 48 bytes)
0x150: "GENIX OS        "  (overseas name, 48 bytes)
0x180: "GM 00000000-00"    (serial number)
0x190: checksum
0x1A0: I/O support, ROM/RAM addresses
0x1B0: SRAM declaration (see below)
0x1F0: "JUE"               (region: Japan, USA, Europe)
```

### SRAM Header Fields (offset 0x1B0–0x1BB)

```
0x1B0: "RA"          Magic (2 bytes) — external RAM present
0x1B2: type byte     Persistence + bus width (see table)
0x1B3: 0x20          Always 0x20
0x1B4: start addr    SRAM start (4 bytes, big-endian)
0x1B8: end addr      SRAM end (4 bytes, big-endian)
```

**Type byte at 0x1B2:**

| Value | Persistent | Access | Use case |
|-------|-----------|--------|----------|
| `0xF8` | Yes (battery) | 8-bit, odd addresses | Standard game carts |
| `0xF0` | Yes (battery) | 8-bit, even addresses | Rare |
| `0xE0` | Yes (battery) | 16-bit (word-wide) | Open EverDrive, FUZIX |
| `0xB8` | No | 8-bit, odd | Volatile scratch RAM |
| `0xA0` | No | 16-bit | Volatile scratch RAM |

**Current Genix ROM header** (in `crt0.S`):
```
"RA", 0xF820, start=0x200000, end=0x20FFFF
```
This declares 8-bit odd-byte battery-backed SRAM, 64 KB address space
(32 KB usable). Correct for standard cartridges and BlastEm auto-detection.

**FUZIX ROM header** (for Open EverDrive):
```
"RA", 0xE020, start=0x200000, end=0x3FFFFF
```
This declares 16-bit word-wide battery-backed SRAM, 2 MB address space.
BlastEm and the Open EverDrive both honor this.

## Files

| File | Purpose |
|------|---------|
| `pal/megadrive/Makefile` | Build rules for Mega Drive ROM |
| `pal/megadrive/megadrive.ld` | Linker script (ROM/RAM layout) |
| `pal/megadrive/crt0.S` | Boot code, vector table, TMSS, ROM header |
| `pal/megadrive/platform.c` | PAL implementation (console, disk, memory) |
| `pal/megadrive/keyboard.c` | Saturn keyboard scancode translation |
| `pal/megadrive/keyboard_read.S` | Saturn keyboard protocol (port 2) |
| `pal/megadrive/vdp.S` | VDP initialization, VRAM operations |
| `pal/megadrive/devvt.S` | VT text output (character plot, scroll, cursor) |
| `pal/megadrive/dbg_output.S` | Debug overlay (Plane B, F12 toggle) |
| `pal/megadrive/fontdata_8x8.c` | 256-glyph 8×8 pixel font |
