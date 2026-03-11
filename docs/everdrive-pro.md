# Mega EverDrive Pro — Hardware Deep Dive

This document covers the Mega EverDrive Pro's internal architecture as
it relates to Genix. All claims are derived from Krikzz's published FPGA
source code (`mega-ed-pub/fpga/`) and the edio-mega SDK.

## Physical Memory Chips

The Pro's PCB carries a Cyclone IV FPGA and four external memory chips.
Pin declarations from `fpga_pro/top.sv`:

| Signal prefix | Type | Address width | Data width | Physical size | Purpose |
|---------------|------|--------------|------------|---------------|---------|
| `PSR0` | PSRAM | 22 bits | 16 bits | **8 MB** | ROM bank 0 |
| `PSR1` | PSRAM | 22 bits | 16 bits | **8 MB** | ROM bank 1 |
| `SRM` | SRAM | 18 bits | 16 bits | **512 KB** | System RAM (fast, 10ns) |
| `BRM` | BRAM | 18 bits | 16 bits | **512 KB** | Battery-backed save RAM |

**Totals: 16 MB PSRAM + 512 KB SRAM + 512 KB BRAM.**

The product page says "1 MB SRAM" — that's the two 512 KB SRAM chips combined.
The "16 MB" is the two PSRAM chips.

From `edio-mega/everdrive.h`:
```c
#define SIZE_ROM    0x1000000   // 16 MB (PSRAM total)
#define SIZE_BRAM   0x80000    // 512 KB (battery-backed)
#define SIZE_SRAM   0x80000    // 512 KB (system fast RAM)
```

PI bus internal address map (`lib_base/pi_map.sv`):
```
0x0000000  PSRAM0       8 MB   (ce_rom0)
0x0800000  PSRAM1       8 MB   (ce_rom1)
0x1000000  SRAM         512 KB (ce_sram)
0x1080000  BRAM         512 KB (ce_bram)
0x1800000  System regs  64 KB  (ce_sys — config, cheats, save state)
0x1810000  FIFO         64 KB  (ce_fifo — CPU↔MCU communication)
0x1830000  Mapper regs  64 KB  (ce_map — mapper config from MCU)
```

Source: `mega-ed-pub/fpga/mapper/lib_base/pi_map.sv`

## BRAM is Always 16-bit on the Pro

The `hwc.sv` hardware config file contains:

```verilog
`define HWC_BRAM_16B
```

This is a **compile-time FPGA define**, not a runtime configuration. The
physical BRAM chip is wired to both halves of the data bus. From
`lib_bram/srm_smd.sv`:

```verilog
`ifdef HWC_BRAM_16B
    assign mem_addr[18:0] = cpu.addr[18:0];      // word-wide addressing
    assign mem_we_hi      = mem_ce & !cpu.we_hi;  // upper byte writes enabled
`endif
```

This means **the ROM header type byte (`0xF8` vs `0xE0`) does not change
how the FPGA accesses BRAM.** The FPGA always does 16-bit access. The
type byte only tells the firmware how to serialize BRAM contents when
saving to SD card (whether to save all bytes or just odd bytes).

Consequence: Genix can do full word/long writes to BRAM on the Pro
regardless of what the ROM header says. The current `0xF820` header
works but wastes half the BRAM capacity because the software only
accesses odd bytes.

## Traditional (SMD) Mapper Mode

When the ROM header system type is `SEGA MEGA DRIVE` (offset 0x100), the
FPGA uses `map_smd.sv` which instantiates `bram_srm_smd` for SRAM access.

### Address Decoding

From `lib_bram/srm_smd.sv`:

```verilog
// Three BRAM address decode modes (set by firmware based on ROM header):
assign mem_ce_2m = mai.cfg.bram_type == `BRAM_SRM   & !cpu.ce_lo & cpu.addr[21];
assign mem_ce_3m = mai.cfg.bram_type == `BRAM_SRM3M & !cpu.ce_lo & cpu.addr[21] & ram_flag;
assign mem_ce_3x = mai.cfg.bram_type == `BRAM_SRM3X & !cpu.ce_lo & cpu.addr[21:20] == 2'b11;
```

| BRAM type | Trigger condition | 68000 address range | Use case |
|-----------|-------------------|---------------------|----------|
| `BRAM_SRM` | `addr[21]` = 1 | 0x200000–0x3FFFFF | ROM < 2 MB (always mapped) |
| `BRAM_SRM3M` | `addr[21]` = 1 + `ram_flag` | 0x200000–0x3FFFFF | ROM < 4 MB (register-controlled) |
| `BRAM_SRM3X` | `addr[21:20]` = 11 | 0x300000–0x3FFFFF | ROM < 4 MB (upper 1 MB only) |

`ram_flag` is the standard Sega mapper register at `0xA130F1` bit 0.

For Genix (ROM ~543 KB < 2 MB): the firmware selects `BRAM_SRM`, and
BRAM is always visible at 0x200000+ with no enable register needed.

### Address Masking and Aliasing

The firmware computes `brm_msk` from the ROM header's SRAM size
declaration (`sys_cfg.sv`):

```verilog
cfg.brm_msk[9:0] = (1'b1 << brm_msk_in[3:0]) - 1;
```

This mask is applied by `mem_ctrl` to the BRAM address, limiting accesses
to the declared size. However, the physical chip address bus is only
18 bits (`BRM_A[17:0]`), so the absolute maximum is 512 KB regardless
of the mask.

**If the ROM header declares 2 MB of SRAM (as FUZIX does):** the mask
allows the full 18-bit range, but the 2 MB address space aliases over
the 512 KB physical chip:

```
0x200000–0x27FFFF → 512 KB BRAM (real)
0x280000–0x2FFFFF → aliases to 0x200000–0x27FFFF
0x300000–0x37FFFF → aliases to 0x200000–0x27FFFF
0x380000–0x3FFFFF → aliases to 0x200000–0x27FFFF
```

### What Genix Gets in Traditional Mode

- **512 KB** of word-wide, battery-backed BRAM at 0x200000
- Saved to SD card automatically on power-off/reset
- 16-bit access works (move.w, move.l) — no odd-byte dance
- But requires changing ROM header from `0xF820` to `0xE020`
  (or just using word access regardless, since the FPGA ignores the
  type byte for access width)

## SSF Mapper Mode — The Big Opportunity

When the ROM header system type is `SEGA SSF` (offset 0x100), the FPGA
uses `map_ssf.sv` which provides bank switching and **writable PSRAM**.

### Bank Registers

Eight 5-bit bank registers at `0xA130F0`–`0xA130FE` (one per 512 KB
slot in the 68000's 4 MB address space):

```verilog
reg [4:0]ssf_bank[8];   // 8 banks × 5-bit physical bank selector (0–31)
```

68000 address space divided into 512 KB banks:

| CPU address range | Bank register | Default |
|-------------------|---------------|---------|
| 0x000000–0x07FFFF | `ssf_bank[0]` (CTRL0) | 0 |
| 0x080000–0x0FFFFF | `ssf_bank[1]` (CTRL1) | 1 |
| 0x100000–0x17FFFF | `ssf_bank[2]` (CTRL2) | 2 |
| 0x180000–0x1FFFFF | `ssf_bank[3]` (CTRL3) | 3 |
| 0x200000–0x27FFFF | `ssf_bank[4]` (CTRL4) | 4 |
| 0x280000–0x2FFFFF | `ssf_bank[5]` (CTRL5) | 5 |
| 0x300000–0x37FFFF | `ssf_bank[6]` (CTRL6) | 6 |
| 0x380000–0x3FFFFF | `ssf_bank[7]` (CTRL7) | 7 |

### Physical Bank Routing

From `map_ssf.sv`:

```verilog
wire [23:0]rom_addr = {ssf_bank[cpu.addr[21:19]][4:0], cpu.addr[18:0]};

wire [1:0]mem_ce =
    rom_addr[23] == 0              ? 0 :   // PSRAM0 (banks 0–15)
    rom_addr[23:19] != 5'b11111    ? 1 :   // PSRAM1 (banks 16–30)
                                     2;    // BRAM   (bank 31)
```

| Physical bank | Chip | Battery-backed |
|---------------|------|----------------|
| 0–15 | PSRAM0 (8 MB) | No |
| 16–30 | PSRAM1 (7.5 MB) | No |
| 31 | BRAM (512 KB) | **Yes** |

### Writable PSRAM

The CTRL0 register (`0xA130F0`) has a special format:

```verilog
// CTRL0 write:
if (cpu.addr[3:0] == 0 & cpu.data[15]) begin
    ssf_bank[0][4:0] <= cpu.data[4:0];   // bank select
    ssf_ctrl[3:0]    <= cpu.data[14:11];  // control bits
end
```

CTRL0 control bits:
| Bit | Name | Function |
|-----|------|----------|
| 11 | `#CART` | Active-low cartridge present signal |
| 12 | W | **Write-enable for all mapped memory** |
| 13 | L | LED control |
| 14 | P | Protection (must be set for CTRL0 write) |
| 15 | — | Must be 1 (gate bit for CTRL0 write) |

```verilog
wire wr_on = ssf_ctrl[2];  // bit 12 of the CTRL0 word = W bit

assign rom0.we_lo = rom_ce & !cpu.we_lo & mem_ce == 0 & wr_on;
assign rom0.we_hi = rom_ce & !cpu.we_hi & mem_ce == 0 & wr_on;
assign rom1.we_lo = rom_ce & !cpu.we_lo & mem_ce == 1 & wr_on;
assign rom1.we_hi = rom_ce & !cpu.we_hi & mem_ce == 1 & wr_on;
assign bram.we_lo = rom_ce & !cpu.we_lo & mem_ce == 2 & wr_on;
assign bram.we_hi = rom_ce & !cpu.we_hi & mem_ce == 2 & wr_on;
```

**When W=1, writes to PSRAM and BRAM are all enabled.** This is the
key to using PSRAM as general-purpose read-write RAM.

### Genix SSF Memory Layout (Proposed)

With the Genix ROM at ~543 KB (fits in bank 0), SSF mode could provide:

```
68000 Address Space:

0x000000 ┌─────────────────┐  Bank 0 → Physical 0 (PSRAM0)
         │ ROM: kernel +   │  Read-only (W=0 for this bank? No,
         │ read-only FS    │  W is global. Just don't write here.)
0x080000 ├─────────────────┤  Bank 1 → Physical 8 (PSRAM0, writable)
         │ User RAM         │  512 KB writable PSRAM
0x100000 ├─────────────────┤  Bank 2 → Physical 9
         │ User RAM         │  512 KB writable PSRAM
0x180000 ├─────────────────┤  Bank 3 → Physical 10
         │ User RAM         │  512 KB writable PSRAM
0x200000 ├─────────────────┤  Bank 4 → Physical 11
         │ User RAM         │  512 KB writable PSRAM
0x280000 ├─────────────────┤  Bank 5 → Physical 12
         │ User RAM         │  512 KB writable PSRAM
0x300000 ├─────────────────┤  Bank 6 → Physical 31 (BRAM)
         │ Persistent FS    │  512 KB battery-backed filesystem
0x380000 ├─────────────────┤  Bank 7 → Physical 13
         │ User RAM         │  512 KB writable PSRAM
0x400000 └─────────────────┘

0xFF0000 ┌─────────────────┐  Main 68000 RAM (always 64 KB)
         │ Kernel BSS/stack │
0xFFFFFF └─────────────────┘
```

This gives:
- **3 MB** writable PSRAM for user programs (volatile)
- **512 KB** battery-backed BRAM for persistent filesystem
- **64 KB** main RAM for kernel
- **~512 KB** ROM for kernel code + read-only filesystem

### SSF Mode Requirements

1. ROM header system type must be `SEGA SSF` (not `SEGA MEGA DRIVE`)
2. CTRL0 must be written with P bit set (bit 14) and W bit (bit 12)
3. The FIFO IO registers (`0xA130D0–0xA130D6`) are only available in
   SSF mode — needed for SD card access via the MCU
4. Standard `0xA130F1` writes will NOT work (different mapper entirely)

```c
/* Initialize SSF mode for Genix */
#define SSF_CTRL0   (*(volatile uint16_t *)0xA130F0)
#define SSF_CTRL(n) (*(volatile uint16_t *)(0xA130F0 + (n)*2))

/* Set CTRL0: P=1, W=1, bank=0 → 0xD000 */
/*   bit 15 = 1 (gate), bit 14 = P, bit 12 = W, bits 4:0 = bank */
SSF_CTRL0 = 0xD000;  /* P + W + bank 0 */

/* Map banks for user RAM */
SSF_CTRL(4) = 8;    /* 0x200000 → physical bank 8 (PSRAM0, writable) */
SSF_CTRL(5) = 9;    /* 0x280000 → physical bank 9 */
SSF_CTRL(6) = 31;   /* 0x300000 → physical bank 31 (BRAM, persistent) */
SSF_CTRL(7) = 10;   /* 0x380000 → physical bank 10 */
```

## FIFO Command Interface (SD Card Access)

The Pro has an onboard MCU that handles SD card and filesystem
operations. The 68000 communicates via a FIFO mailbox. **Only available
in SSF mode** (system type = `SEGA SSF`).

Registers at `0xA130Dx`:

| Register | Address | Access | Function |
|----------|---------|--------|----------|
| `FIFO_DATA` | `0xA130D0` | R/W | Command/data FIFO (16-bit) |
| `FIFO_STAT` | `0xA130D2` | R | FIFO status flags |
| `SYS_STAT` | `0xA130D4` | R | System status (device ID, boot state) |
| `TIMER` | `0xA130D6` | R | Hardware timer |

Device detection:
```c
#define REG_SYS_STAT  (*(volatile uint16_t *)0xA130D4)
bool is_pro = (REG_SYS_STAT & 0xFFF0) == 0x55A0;
```

The MCU provides a high-level file API (no FAT implementation needed on
the 68000 side):

| Command | Function |
|---------|----------|
| `CMD_FILE_OPEN` | Open file by path |
| `CMD_FILE_READ` | Read from open file |
| `CMD_FILE_WRITE` | Write to open file |
| `CMD_FILE_CLOSE` | Close file handle |
| `CMD_FILE_READ_MEM` | Read file directly into cartridge RAM |
| `CMD_MEM_RD/WR` | Read/write cartridge memory regions |
| `CMD_RTC_GET` | Read real-time clock |
| `CMD_REBOOT` | Reboot to menu |

Source: `mega-ed-pub/edio-mega/everdrive.h`, `everdrive.c`

The SDK is GPL-3.0 licensed. See `mega-ed-pub/edio-mega/` for working C
code.

## RAM Cartridge Mode

The FPGA also supports a `BRAM_RCART` mode (`ram_cart.sv`) which maps
SRAM as an 8-bit RAM cartridge at `0x600000–0x6FFFFF` with a write
protection register at `0x700000`. This is used for Sega CD backup RAM
cartridge emulation but could theoretically be repurposed. The size is
128 KB or 256 KB depending on configuration. Not relevant for Genix but
noted for completeness.

## Summary: Maximum Usable Memory

| Mode | Writable RAM | Persistent | Notes |
|------|-------------|------------|-------|
| Traditional (current) | 512 KB BRAM | Yes (battery) | Word-wide, auto-mapped for ROM < 2 MB |
| SSF (BRAM only) | 512 KB BRAM | Yes (battery) | Same chip, different mapper logic |
| SSF (PSRAM + BRAM) | **3+ MB PSRAM + 512 KB BRAM** | PSRAM: no, BRAM: yes | W bit enables PSRAM writes |

## FPGA Source References

All paths relative to `krikzz/mega-ed-pub/`:

| File | What it shows |
|------|--------------|
| `fpga/fpga_pro/top.sv` | Physical pin declarations, chip sizes |
| `fpga/fpga_pro/hwc.sv` | `HWC_BRAM_16B` define |
| `fpga/mapper/lib_base/everdrive.sv` | Top-level mapper routing |
| `fpga/mapper/lib_base/pi_map.sv` | PI bus address decoding |
| `fpga/mapper/lib_base/sys_cfg.sv` | `brm_msk` computation |
| `fpga/mapper/lib_base/defs.sv` | BRAM type constants |
| `fpga/mapper/lib_base/structs.sv` | Bus/memory interface definitions |
| `fpga/mapper/lib_bram/srm_smd.sv` | SMD mapper SRAM decoding |
| `fpga/mapper/lib_bram/ram_cart.sv` | RAM cartridge mode |
| `fpga/mapper/map_smd/map_smd.sv` | Traditional mapper |
| `fpga/mapper/map_ssf/map_ssf.sv` | SSF mapper (bank switching + writable PSRAM) |
| `edio-mega/everdrive.h` | SDK defines, PI addresses, `SIZE_*` constants |

## External References

- [Krikzz mega-ed-pub repository](https://github.com/krikzz/mega-ed-pub) (GPL-3.0)
- [Extended SSF spec](https://krikzz.com/pub/support/mega-everdrive/pro-series/extended-ssf.txt)
- [SRAM & Bank Switching with Everdrives](https://blog.roberthargreaves.com/2025/03/10/sram-and-everdrives) (Robert Hargreaves)
- [Detecting Mega EverDrive Models](https://blog.roberthargreaves.com/2025/02/05/detecting-mega-everdrive-models) (Robert Hargreaves)
- [Mega EverDrive Pro product page](https://krikzz.com/our-products/cartridges/mega-everdrive-pro.html)
