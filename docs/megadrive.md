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

- SRAM mapper: **standard Sega** (same as Config B/C)
- Control register at `0xA130E0`, bit 0 (`SRM_ON`):
  - `0` → ROM visible at 0x200000
  - `1` → SRAM visible at 0x200000
- 128 KB is enough for a small minifs filesystem
- User programs run from 64 KB main RAM (no SRAM needed)
- SD card slot available for future larger storage

**Hardware reference:** Schematics, PCB layout, Gerbers, and firmware
source are all in the open-ed repository.

### Configuration E: Mega Everdrive Pro

The Mega Everdrive Pro uses the SSF mapper, which requires a different
SRAM unlock sequence:

```c
/* SSF mapper unlock (Mega Everdrive Pro) */
*(volatile uint16_t *)0xA130F0 = 0x8000;
```

Standard cartridges (including Open EverDrive) use:
```c
/* Standard SRAM enable */
*(volatile uint8_t *)0xA130F1 = 0x03;
```

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

### Byte Access Quirk

On the Mega Drive, cartridge SRAM is wired to the data bus such that
only odd byte addresses are accessible. To read/write N bytes of data,
you must access 2N addresses:

```c
volatile uint8_t *sram = (volatile uint8_t *)SRAM_BASE;

/* Reading: skip even bytes */
for (int i = 0; i < len; i++)
    dst[i] = sram[i * 2 + 1];

/* Writing: skip even bytes */
for (int i = 0; i < len; i++)
    sram[i * 2 + 1] = src[i];
```

This means 64 KB of physical SRAM provides 32 KB of usable storage.
A cartridge advertised as "256 KB SRAM" provides 128 KB usable.

**Exception:** If user programs are loaded into SRAM and execute from
there, the CPU accesses full words/longs normally. The odd-byte quirk
only applies to 8-bit SRAM chips. Some cartridges wire SRAM as 16-bit,
which provides full-width access. The code must handle both cases.

### SRAM Enable Sequence

SRAM must be explicitly enabled via the mapper register:

```c
/* Standard mapper */
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

**Headless testing (`make test-md`):**

BlastEm has a built-in headless mode — no display, no Xvfb needed:

```bash
blastem -b FRAMES rom.bin    # run FRAMES frames, then exit (no window)
```

The top-level Makefile provides:

```bash
make test-md                 # build ROM + boot headless for ~5s
```

This runs `timeout 30 blastem -b 300 pal/megadrive/genix-md.bin`,
which boots the ROM for ~5 seconds (300 frames at 60 fps NTSC) and
exits. Catches address errors, illegal instructions, and bus faults
that only appear in the Mega Drive build.

Useful BlastEm flags for testing:

| Flag | Purpose |
|------|---------|
| `-b N` | Headless: run N frames then exit (no display) |
| `-D` | GDB remote debugging (pipe mode) |
| `-d` | Enter integrated debugger |
| `-g` | Disable OpenGL |
| `-r (J\|U\|E)` | Force region (NTSC-J, NTSC-U, PAL) |
| `-n` | Disable Z80 |
| `-e FILE` | Write hardware event log |

**BlastEm configuration** (`~/.config/blastem/blastem.cfg` or
`/etc/blastem/default.cfg`):

```
video {
    gl off
    vsync off
}
```

Disable GL if running in a terminal-only environment without `-b`.

**Fallback (no `-b` support):** Older BlastEm versions may lack `-b`.
Use Xvfb instead:

```bash
export DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy
Xvfb :99 -screen 0 1024x768x24 &
timeout 30 blastem pal/megadrive/genix-md.bin
```

**BlastEm SRAM notes:**
- BlastEm auto-detects SRAM from the ROM header
- SRAM save files may be byte-swapped depending on version:
  - v0.6.2: byte-swaps SRAM on load/save
  - v0.6.3+: does NOT byte-swap
- To pre-populate SRAM with a filesystem image, use `dd conv=swab`
  for v0.6.2, or copy the image directly for v0.6.3+

**BlastEm GDB debugging:**

```bash
m68k-linux-gnu-gdb -q --tui \
    -ex "target remote | blastem -D pal/megadrive/genix-md.bin" \
    pal/megadrive/genix-md.elf
```

### Real Hardware

Test on real Mega Drive with a flash cartridge (e.g., Mega Everdrive Pro).
Copy `genix-md.bin` to the flash cartridge SD card and boot.

Things to verify on real hardware that emulators miss:
- TMSS handshake works (no black screen)
- Z80 halt/release sequence is correct
- SRAM enable works with the specific cartridge mapper
- Keyboard reads work at hardware timing
- VBlank timing is correct for NTSC vs PAL

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
0x1F0: "JUE"               (region: Japan, USA, Europe)
```

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
