# EverDrive Research: SD Card Access and Pro Hardware

Research into EverDrive cartridge hardware for the Sega Mega Drive, covering
the Open EverDrive's SPI interface, the Mega EverDrive Pro's FPGA architecture
and memory system, and SD card access mechanisms across all EverDrive models.

All claims are derived from Krikzz's published source code (open-ed repository,
mega-ed-pub FPGA source, edio-mega SDK) and community documentation.

---

## Open EverDrive — Bit-Bang SPI

The Open EverDrive is a discrete-logic cartridge (no FPGA, no MCU — just 74HC
logic chips, 8 MB NOR flash, 128 KB SRAM, and an SD card slot) that exposes the
SD card's SPI bus directly through a control register in the `$A130xx` range. The
68000 must bit-bang the SPI protocol entirely in software.

### Hardware Interface

The control register responds to any access in `0xA13000-0xA130FF` (the 68000's
`!TIME` signal range). The open-ed menu firmware conventionally uses address
`0xA130E0`. Since the entire `$A130xx` range is decoded to the same register,
this also means the standard Sega mapper register at `0xA130F1` aliases to the
same hardware — on the open-ed, writing `0xA130F1` writes the CTRL_REG, not a
separate mapper. (In `ROM_2M+RAM` mode SRAM is always on, so this is harmless.)

The control register maps individual SPI signals to bits:

**Write bits:**

| Bit | Name     | Function |
|-----|----------|----------|
| 0   | SRM_ON   | SRAM bank select (0=ROM, 1=SRAM at 0x200000) |
| 2   | ROM_BANK | Flash bank select |
| 3   | LED      | Cartridge LED control |
| 4   | EXP_SS   | Expansion SPI chip select (active low) |
| 5   | SDC_SS   | SD card chip select (active low) |
| 6   | SPI_CLK  | SPI clock |
| 7   | SPI_MOSI | SPI data out (master -> SD card) |

**Read bits:**

| Bit | Name     | Function |
|-----|----------|----------|
| 0   | SPI_MISO | SPI data in (SD card -> master) |
| 1-7 | --       | Fixed 0 or open bus |

### Clock Acceleration Trick

The hardware has a useful optimization: when SPI_CLK is written as 0, every
subsequent **read** of the control register automatically generates a positive
clock pulse on the SPI_CLK line. This means you can clock out data by just
reading the register repeatedly instead of toggling clock bits manually. This
roughly doubles the effective SPI throughput.

### Bit-Bang SPI Transfer

From the open-ed menu firmware source (`menu/everdrive.c`):

```c
#define CTRL_REG  *((vu16 *) 0xA130E0)

/* ed_cfg holds the current non-SPI bits (SRM_ON, LED, etc.) */

/* Write one byte over SPI */
void spi_w(u8 val) {
    for (u16 i = 0; i < 8; i++) {
        CTRL_REG = ed_cfg | (val & CTRL_SPI_MOSI);                /* MOSI + CLK low */
        CTRL_REG = ed_cfg | (val & CTRL_SPI_MOSI) | CTRL_SPI_CLK; /* CLK high */
        CTRL_REG = ed_cfg | (val & CTRL_SPI_MOSI);                /* CLK low */
        val <<= 1;
    }
}

/* Read one byte over SPI */
u8 spi_r() {
    u8 ret = 0;
    CTRL_REG = ed_cfg | CTRL_SPI_MOSI;  /* MOSI high (idle) */
    for (u16 i = 0; i < 8; i++) {
        CTRL_REG = ed_cfg | CTRL_SPI_MOSI | CTRL_SPI_CLK;  /* CLK high */
        ret <<= 1;
        ret |= CTRL_REG;                                     /* sample MISO */
        CTRL_REG = ed_cfg | CTRL_SPI_MOSI;                  /* CLK low */
    }
    return ret;
}
```

The menu firmware also has a fast 512-byte read in 68000 assembly (`menu/spi.s`)
that exploits the auto-clock feature: each read of CTRL_REG auto-pulses SPI_CLK
(when the CLK bit is 0), so the inner loop is just `lsl.w #1, d0; or.w (a0), d0`
repeated 8 times per byte, fully unrolled.

### Performance Estimate

Each SPI bit requires ~3 register writes (MOSI + CLK high + CLK low) through
the cartridge bus. Each 16-bit write to the `$A130xx` range takes ~8 68000
cycles (4 cycles for the instruction + wait states):

- **Per byte (write)**: ~24 register accesses = ~192 cycles
- **Per byte (read)**: ~16 register accesses = ~128 cycles
- **Per byte (auto-clock read)**: ~8 register reads = ~64 cycles
- **Per 512-byte sector**: ~32K-98K cycles
- **Theoretical max at 7.67 MHz**: ~78-120 KB/s (auto-clock read)

Real-world performance from the open-ed menu: loading a 4 MB ROM takes 15-35
seconds (multi-block reads with CMD18), giving ~115-270 KB/s effective
throughput. For single-sector reads (CMD17) the overhead per command is higher,
but even 20 KB/s is usable for a command-line OS loading small files.

### SD Card Protocol

The open-ed menu firmware (`menu/disk.c`) implements standard SD card SPI
initialization: CMD0 (reset), CMD8 (voltage check), CMD55+ACMD41 loop
(initialization), CMD58 (SDHC detection). Block reads via CMD17 (single) and
CMD18 (multi-block). This is well-documented protocol that can be implemented
directly.

### Address Space Compatibility

The open-ed's CTRL_REG at `$A130xx` does **not** conflict with the standard SRAM
enable register at `$A130F1` because on the open-ed, they are the **same
register**. The open-ed decodes the entire `$A130xx` range to one 8-bit latch.
This means:

- The existing `*(volatile uint8_t *)0xA130F1 = 0x03` writes to the same
  CTRL_REG -- harmless in `ROM_2M+RAM` mode (SRAM already on)
- The SD card SPI pins (bits 4-7) and SRAM control (bit 0) share one register --
  SD card access code must preserve the SRM_ON bit

On a standard cartridge (no open-ed), the `$A130F0-$A130FF` range is the Sega
mapper and `$A130E0` is open bus. So open-ed-specific SPI writes to `$A130E0`
are harmless on non-open-ed hardware.

### Open EverDrive Source References

- Hardware: [open-ed.v](https://github.com/krikzz/open-ed/blob/master/open-ed.v) (Verilog)
- Menu firmware: [open-ed/menu/](https://github.com/krikzz/open-ed/tree/master/menu) (SPI, SD, FAT code)
- Mapper doc: [open-ed-mapper.txt](https://github.com/krikzz/open-ed/blob/master/open-ed-mapper.txt)
- Schematics: [open-ed repository](https://github.com/krikzz/open-ed) (MIT license)

---

## Mega EverDrive Pro — Physical Memory Chips

The Pro's PCB carries a Cyclone IV FPGA and four external memory chips. Pin
declarations from `fpga_pro/top.sv`:

| Signal prefix | Type | Address width | Data width | Physical size | Purpose |
|---------------|------|--------------|------------|---------------|---------|
| `PSR0` | PSRAM | 22 bits | 16 bits | **8 MB** | ROM bank 0 |
| `PSR1` | PSRAM | 22 bits | 16 bits | **8 MB** | ROM bank 1 |
| `SRM` | SRAM | 18 bits | 16 bits | **512 KB** | System RAM (fast, 10ns) |
| `BRM` | BRAM | 18 bits | 16 bits | **512 KB** | Battery-backed save RAM |

**Totals: 16 MB PSRAM + 512 KB SRAM + 512 KB BRAM.**

The product page says "1 MB SRAM" -- that's the two 512 KB SRAM chips combined.
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
0x1800000  System regs  64 KB  (ce_sys -- config, cheats, save state)
0x1810000  FIFO         64 KB  (ce_fifo -- CPU<->MCU communication)
0x1830000  Mapper regs  64 KB  (ce_map -- mapper config from MCU)
```

Source: `mega-ed-pub/fpga/mapper/lib_base/pi_map.sv`

### BRAM is Always 16-bit on the Pro

The `hwc.sv` hardware config file contains:

```verilog
`define HWC_BRAM_16B
```

This is a **compile-time FPGA define**, not a runtime configuration. The physical
BRAM chip is wired to both halves of the data bus. From `lib_bram/srm_smd.sv`:

```verilog
`ifdef HWC_BRAM_16B
    assign mem_addr[18:0] = cpu.addr[18:0];      // word-wide addressing
    assign mem_we_hi      = mem_ce & !cpu.we_hi;  // upper byte writes enabled
`endif
```

This means **the ROM header type byte (`0xF8` vs `0xE0`) does not change how the
FPGA accesses BRAM.** The FPGA always does 16-bit access. The type byte only
tells the firmware how to serialize BRAM contents when saving to SD card (whether
to save all bytes or just odd bytes).

Consequence: full word/long writes to BRAM work on the Pro regardless of what the
ROM header says. The `0xF820` header works but wastes half the BRAM capacity
because the software only accesses odd bytes.

---

## Mega EverDrive Pro — Traditional (SMD) Mapper Mode

When the ROM header system type is `SEGA MEGA DRIVE` (offset 0x100), the FPGA
uses `map_smd.sv` which instantiates `bram_srm_smd` for SRAM access.

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
| `BRAM_SRM` | `addr[21]` = 1 | 0x200000-0x3FFFFF | ROM < 2 MB (always mapped) |
| `BRAM_SRM3M` | `addr[21]` = 1 + `ram_flag` | 0x200000-0x3FFFFF | ROM < 4 MB (register-controlled) |
| `BRAM_SRM3X` | `addr[21:20]` = 11 | 0x300000-0x3FFFFF | ROM < 4 MB (upper 1 MB only) |

`ram_flag` is the standard Sega mapper register at `0xA130F1` bit 0.

For a small ROM (< 2 MB): the firmware selects `BRAM_SRM`, and BRAM is always
visible at 0x200000+ with no enable register needed.

### Address Masking and Aliasing

The firmware computes `brm_msk` from the ROM header's SRAM size declaration
(`sys_cfg.sv`):

```verilog
cfg.brm_msk[9:0] = (1'b1 << brm_msk_in[3:0]) - 1;
```

This mask is applied by `mem_ctrl` to the BRAM address, limiting accesses to the
declared size. However, the physical chip address bus is only 18 bits
(`BRM_A[17:0]`), so the absolute maximum is 512 KB regardless of the mask.

**If the ROM header declares 2 MB of SRAM** (as FUZIX does): the mask allows the
full 18-bit range, but the 2 MB address space aliases over the 512 KB physical
chip:

```
0x200000-0x27FFFF -> 512 KB BRAM (real)
0x280000-0x2FFFFF -> aliases to 0x200000-0x27FFFF
0x300000-0x37FFFF -> aliases to 0x200000-0x27FFFF
0x380000-0x3FFFFF -> aliases to 0x200000-0x27FFFF
```

### What Traditional Mode Provides

- **512 KB** of word-wide, battery-backed BRAM at 0x200000
- Saved to SD card automatically on power-off/reset
- 16-bit access works (move.w, move.l) -- no odd-byte dance
- But requires changing ROM header from `0xF820` to `0xE020` (or just using word
  access regardless, since the FPGA ignores the type byte for access width)

---

## Mega EverDrive Pro — SSF Mapper Mode

When the ROM header system type is `SEGA SSF` (offset 0x100), the FPGA uses
`map_ssf.sv` which provides bank switching and **writable PSRAM**.

### Bank Registers

Eight 5-bit bank registers at `0xA130F0`-`0xA130FE` (one per 512 KB slot in the
68000's 4 MB address space):

```verilog
reg [4:0]ssf_bank[8];   // 8 banks x 5-bit physical bank selector (0-31)
```

68000 address space divided into 512 KB banks:

| CPU address range | Bank register | Default |
|-------------------|---------------|---------|
| 0x000000-0x07FFFF | `ssf_bank[0]` (CTRL0) | 0 |
| 0x080000-0x0FFFFF | `ssf_bank[1]` (CTRL1) | 1 |
| 0x100000-0x17FFFF | `ssf_bank[2]` (CTRL2) | 2 |
| 0x180000-0x1FFFFF | `ssf_bank[3]` (CTRL3) | 3 |
| 0x200000-0x27FFFF | `ssf_bank[4]` (CTRL4) | 4 |
| 0x280000-0x2FFFFF | `ssf_bank[5]` (CTRL5) | 5 |
| 0x300000-0x37FFFF | `ssf_bank[6]` (CTRL6) | 6 |
| 0x380000-0x3FFFFF | `ssf_bank[7]` (CTRL7) | 7 |

### Physical Bank Routing

From `map_ssf.sv`:

```verilog
wire [23:0]rom_addr = {ssf_bank[cpu.addr[21:19]][4:0], cpu.addr[18:0]};

wire [1:0]mem_ce =
    rom_addr[23] == 0              ? 0 :   // PSRAM0 (banks 0-15)
    rom_addr[23:19] != 5'b11111    ? 1 :   // PSRAM1 (banks 16-30)
                                     2;    // BRAM   (bank 31)
```

| Physical bank | Chip | Battery-backed |
|---------------|------|----------------|
| 0-15 | PSRAM0 (8 MB) | No |
| 16-30 | PSRAM1 (7.5 MB) | No |
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
| 15 | -- | Must be 1 (gate bit for CTRL0 write) |

```verilog
wire wr_on = ssf_ctrl[2];  // bit 12 of the CTRL0 word = W bit

assign rom0.we_lo = rom_ce & !cpu.we_lo & mem_ce == 0 & wr_on;
assign rom0.we_hi = rom_ce & !cpu.we_hi & mem_ce == 0 & wr_on;
assign rom1.we_lo = rom_ce & !cpu.we_lo & mem_ce == 1 & wr_on;
assign rom1.we_hi = rom_ce & !cpu.we_hi & mem_ce == 1 & wr_on;
assign bram.we_lo = rom_ce & !cpu.we_lo & mem_ce == 2 & wr_on;
assign bram.we_hi = rom_ce & !cpu.we_hi & mem_ce == 2 & wr_on;
```

**When W=1, writes to PSRAM and BRAM are all enabled.** This is the key to using
PSRAM as general-purpose read-write RAM.

### SSF Mode Requirements

1. ROM header system type must be `SEGA SSF` (not `SEGA MEGA DRIVE`)
2. CTRL0 must be written with P bit set (bit 14) and W bit (bit 12)
3. The FIFO IO registers (`0xA130D0-0xA130D6`) are only available in SSF mode --
   needed for SD card access via the MCU
4. Standard `0xA130F1` writes will NOT work (different mapper entirely)

### SSF Initialization Example

```c
/* Initialize SSF mode */
#define SSF_CTRL0   (*(volatile uint16_t *)0xA130F0)
#define SSF_CTRL(n) (*(volatile uint16_t *)(0xA130F0 + (n)*2))

/* Set CTRL0: P=1, W=1, bank=0 -> 0xD000 */
/*   bit 15 = 1 (gate), bit 14 = P, bit 12 = W, bits 4:0 = bank */
SSF_CTRL0 = 0xD000;  /* P + W + bank 0 */

/* Map banks for user RAM */
SSF_CTRL(4) = 8;    /* 0x200000 -> physical bank 8 (PSRAM0, writable) */
SSF_CTRL(5) = 9;    /* 0x280000 -> physical bank 9 */
SSF_CTRL(6) = 31;   /* 0x300000 -> physical bank 31 (BRAM, persistent) */
SSF_CTRL(7) = 10;   /* 0x380000 -> physical bank 10 */
```

### SSF Memory Layout (Proposed)

With a small ROM (~543 KB, fits in bank 0), SSF mode could provide:

```
68000 Address Space:

0x000000 +------------------+  Bank 0 -> Physical 0 (PSRAM0)
         | ROM: kernel +    |  Read-only (W is global; just don't write here)
         | read-only FS     |
0x080000 +------------------+  Bank 1 -> Physical 8 (PSRAM0, writable)
         | User RAM          |  512 KB writable PSRAM
0x100000 +------------------+  Bank 2 -> Physical 9
         | User RAM          |  512 KB writable PSRAM
0x180000 +------------------+  Bank 3 -> Physical 10
         | User RAM          |  512 KB writable PSRAM
0x200000 +------------------+  Bank 4 -> Physical 11
         | User RAM          |  512 KB writable PSRAM
0x280000 +------------------+  Bank 5 -> Physical 12
         | User RAM          |  512 KB writable PSRAM
0x300000 +------------------+  Bank 6 -> Physical 31 (BRAM)
         | Persistent FS     |  512 KB battery-backed filesystem
0x380000 +------------------+  Bank 7 -> Physical 13
         | User RAM          |  512 KB writable PSRAM
0x400000 +------------------+

0xFF0000 +------------------+  Main 68000 RAM (always 64 KB)
         | Kernel BSS/stack  |
0xFFFFFF +------------------+
```

This gives:
- **3 MB** writable PSRAM for user programs (volatile)
- **512 KB** battery-backed BRAM for persistent filesystem
- **64 KB** main RAM for kernel
- **~512 KB** ROM for kernel code + read-only filesystem

### RAM Cartridge Mode

The FPGA also supports a `BRAM_RCART` mode (`ram_cart.sv`) which maps SRAM as an
8-bit RAM cartridge at `0x600000-0x6FFFFF` with a write protection register at
`0x700000`. This is used for Sega CD backup RAM cartridge emulation but could
theoretically be repurposed. The size is 128 KB or 256 KB depending on
configuration. Not relevant for Genix but noted for completeness.

### Maximum Usable Memory Summary

| Mode | Writable RAM | Persistent | Notes |
|------|-------------|------------|-------|
| Traditional (current) | 512 KB BRAM | Yes (battery) | Word-wide, auto-mapped for ROM < 2 MB |
| SSF (BRAM only) | 512 KB BRAM | Yes (battery) | Same chip, different mapper logic |
| SSF (PSRAM + BRAM) | **3+ MB PSRAM + 512 KB BRAM** | PSRAM: no, BRAM: yes | W bit enables PSRAM writes |

---

## SD Card Access — Pro FIFO Command Interface

The Mega EverDrive Pro has an onboard MCU that handles all SD card and filesystem
operations. The 68000 communicates with the FPGA through a **FIFO command
mailbox** -- you send high-level commands (open file, read sector, list
directory) and the FPGA/MCU does the actual SD card I/O.

**Only available in SSF mode** (system type = `SEGA SSF`).

### FIFO Registers

Registers at `0xA130Dx`:

| Register | Address    | Access | Function |
|----------|-----------|--------|----------|
| `FIFO_DATA` | `0xA130D0` | R/W | Command/data FIFO (16-bit) |
| `FIFO_STAT` | `0xA130D2` | R   | FIFO status flags |
| `SYS_STAT`  | `0xA130D4` | R   | System status (device ID, boot state) |
| `TIMER`     | `0xA130D6` | R   | Hardware timer |

**FIFO status flags** (at `0xA130D2`):

| Bit | Name | Meaning |
|-----|------|---------|
| 0   | CPU_RD_RDY | Data available for CPU to read from FIFO |
| 1   | CPU_WR_RDY | FIFO ready for CPU to write |
| Others | -- | Configuration/boot status |

### Device Detection

```c
#define REG_SYS_STAT  (*(volatile uint16_t *)0xA130D4)
bool is_pro = (REG_SYS_STAT & 0xFFF0) == 0x55A0;
```

### Command Protocol

The 68000 sends commands by writing to the FIFO. Each command is a 16-bit
command code followed by parameters. The MCU processes the command, performs the
SD/filesystem operation, and returns results through the same FIFO.

**Available commands** (from the [edio-mega SDK](https://github.com/krikzz/mega-ed-pub/tree/master/edio-mega)):

| Command | Function |
|---------|----------|
| `CMD_STATUS` | Query device status |
| `CMD_DISK_INIT` | Initialize SD card |
| `CMD_FILE_OPEN` | Open file by path (supports read/write modes) |
| `CMD_FILE_READ` | Read from open file (512-byte blocks) |
| `CMD_FILE_WRITE` | Write to open file (1024-byte ack blocks) |
| `CMD_FILE_CLOSE` | Close file handle |
| `CMD_FILE_INFO` | Get file size, date, time, attributes |
| `CMD_FILE_READ_MEM` | Read file directly into cartridge RAM |
| `CMD_DIR_LOAD` | Load directory listing |
| `CMD_DIR_GET_RECS` | Get directory record count |
| `CMD_MEM_RD` | Read from cartridge memory (ROM/SRAM) |
| `CMD_MEM_WR` | Write to cartridge memory |
| `CMD_RTC_GET` | Read real-time clock |
| `CMD_REBOOT` | Reboot to menu |

### File Access Example (from SDK)

```c
/* Read a file from SD card */
uint8_t resp;
uint8_t buff[512];

resp = ed_cmd_disk_init();          /* Initialize SD card */
resp = ed_cmd_file_open("/MEGA/bios/readme.txt", FA_READ);
resp = ed_cmd_file_read(buff, 512); /* Read 512 bytes */
resp = ed_cmd_file_close();

/* Read file directly into ROM/PSRAM address space */
resp = ed_cmd_file_open(path, FA_READ);
resp = ed_cmd_file_read_mem(rom_dst, file_size);
resp = ed_cmd_file_close();

/* List directory */
resp = ed_cmd_dir_load("/MEGA", DIR_OPT_SORTED);
uint16_t dir_size;
ed_cmd_dir_get_size(&dir_size);
```

### Activation Requirements

The Pro's IO registers are only accessible when the ROM header declares `SEGA
SSF` as the system type (offset 0x100). In standard `SEGA MEGA DRIVE` mode, the
FIFO registers at `0xA130D0` are not active.

This means a separate build configuration would be needed (or a dual-mode ROM
that detects the hardware and switches behavior).

### Performance

Since the MCU handles all SD/filesystem work:

- **File read**: Limited by FIFO throughput, not SPI bit-bang speed
- **Estimated throughput**: Much faster than the Open EverDrive (the FPGA/MCU has
  a dedicated high-speed SPI controller)
- **No FAT implementation needed on the 68000 side** -- the MCU does it

---

## SD Card Access — X-Series (X3/X5/X7) Hardware SPI

The older X-series EverDrives expose SD card access through dedicated SPI
hardware registers (not bit-bang like Open EverDrive, not FIFO like Pro):

| Register | Address    | Function |
|----------|-----------|----------|
| SD_DATA  | `0xA130E0` | SPI data register (8-bit or 16-bit) |
| USB_IO   | `0xA130E2` | USB data register |
| IO_STAT  | `0xA130E4` | Status: SPI ready, USB ready, SD card type |
| IO_CFG   | `0xA130E6` | Config: SD chip select, 16-bit mode, auto-read |

These registers also require `SEGA SSF` header mode. The 68000 must implement
the SD card command protocol (CMD0, CMD17, etc.) but the actual SPI clocking is
handled by hardware -- much faster than bit-bang.

---

## Comparison

| Feature | Open EverDrive | EverDrive Pro | EverDrive X-series |
|---------|---------------|---------------|-------------------|
| **Price** | ~$30 | ~$200 | $60-130 (discontinued) |
| **SD access method** | Bit-bang SPI | FIFO commands to MCU | Hardware SPI registers |
| **ROM header requirement** | None (standard mode) | `SEGA SSF` | `SEGA SSF` |
| **FAT filesystem** | Must implement on 68000 | Handled by MCU | Must implement on 68000 |
| **Open source** | Yes (MIT) | Partial (SDK is GPL-3) | Partial (SDK) |
| **Throughput** | ~20-270 KB/s | Fast (MCU-limited) | Medium (HW SPI) |
| **Code complexity** | High (SPI + SD + FAT) | Low (file API) | Medium (SD + FAT) |
| **Detection** | SD card responds to CMD0 | `REG_SYS_STAT & 0xFFF0 == 0x55A0` | `IO_STAT` check |

### Dual-Target Detection

A single ROM could support both cartridges:

```c
void sd_init(void) {
    /* Try detecting Mega EverDrive Pro */
    uint16_t sys_stat = *(volatile uint16_t *)0xA130D4;
    if ((sys_stat & 0xFFF0) == 0x55A0) {
        /* Pro detected -- use FIFO command interface */
        sd_driver = &sd_pro_driver;
        return;
    }

    /* Fall back to Open EverDrive SPI bit-bang */
    sd_driver = &sd_spi_driver;
}
```

However, the Pro requires `SEGA SSF` header mode to activate its FIFO registers,
which means runtime detection may not work without the correct header. This needs
further investigation -- it may be possible to detect the Pro even in standard
mode, or the header could be set to SSF with fallback logic for non-Pro hardware.

### Alternative: Skip FAT, Use Raw SD

For the Open EverDrive, instead of implementing FAT16/32, a custom filesystem
could be written directly to the SD card:

- Use a host tool to format the SD card with the custom filesystem
- The SD card driver provides block read/write (CMD17/CMD24)
- The kernel filesystem code handles everything above that
- No FAT code needed at all

This is simpler and more aligned with a minimal OS design. Users would format the
SD card with a host tool, not use it as a generic FAT volume.

---

## MegaSD (Terraonion) — Not Targeted

The MegaSD by Terraonion is a different flash cartridge with an SD card slot, but
it is **not a target** because:

- **Closed source** -- no public register maps, SDKs, or hardware documentation
  for homebrew developers
- **No known homebrew SD access API** -- unlike the EverDrive Pro's documented
  FIFO commands or the Open EverDrive's open-source SPI interface, the MegaSD
  does not expose a documented way for running ROMs to access the SD card
- **Sega CD focus** -- its main feature is Sega CD emulation, which the
  EverDrives do not do; SD access is used internally by the firmware, not exposed
  to user ROMs
- **Cannot test** -- no hardware available for verification

If someone reverse-engineers the MegaSD's register interface in the future,
support could be added then. For now, the three EverDrive paths (Pro FIFO,
X-series SPI controller, Open EverDrive bit-bang) cover the documented and
testable cartridge hardware.

---

## Source References

### Open EverDrive

- Hardware Verilog: [open-ed.v](https://github.com/krikzz/open-ed/blob/master/open-ed.v)
- Menu firmware: [open-ed/menu/](https://github.com/krikzz/open-ed/tree/master/menu) (SPI, SD, FAT code)
- Mapper doc: [open-ed-mapper.txt](https://github.com/krikzz/open-ed/blob/master/open-ed-mapper.txt)
- Schematics: [open-ed repository](https://github.com/krikzz/open-ed) (MIT license)

### Mega EverDrive Pro — FPGA Source

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

### Mega EverDrive Pro — SDK and Documentation

- SDK: [krikzz/mega-ed-pub/edio-mega](https://github.com/krikzz/mega-ed-pub/tree/master/edio-mega) (GPL-3.0)
- SSF spec: [extended-ssf.txt](https://krikzz.com/pub/support/mega-everdrive/pro-series/extended-ssf.txt)
- Detection: [Detecting Mega Everdrive Models](https://blog.roberthargreaves.com/2025/02/05/detecting-mega-everdrive-models)
- SRAM/banking: [SRAM & Bank Switching with Everdrives](https://blog.roberthargreaves.com/2025/03/10/sram-and-everdrives)
- Product page: [Mega EverDrive Pro](https://krikzz.com/our-products/cartridges/mega-everdrive-pro.html)
