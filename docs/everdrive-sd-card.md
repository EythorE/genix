# EverDrive SD Card Access Research

Research into whether Genix can access the SD card on Open EverDrive and
Mega EverDrive Pro cartridges while a ROM is running.

**TL;DR: Yes, both cartridges expose SD card access to the running 68000
CPU, but through very different mechanisms.**

## Open EverDrive — Bit-Bang SPI

The Open EverDrive is a discrete-logic cartridge (no FPGA, no MCU —
just 74HC logic chips, 8 MB NOR flash, 128 KB SRAM, and an SD card
slot) that exposes the SD card's SPI bus directly through a control
register in the `$A130xx` range. The 68000 must bit-bang the SPI
protocol entirely in software.

### Hardware Interface

The control register responds to any access in `0xA13000-0xA130FF`
(the 68000's `!TIME` signal range). The open-ed menu firmware
conventionally uses address `0xA130E0`. Since the entire `$A130xx`
range is decoded to the same register, this also means the standard
Sega mapper register at `0xA130F1` aliases to the same hardware — on
the open-ed, writing `0xA130F1` writes the CTRL_REG, not a separate
mapper. (In `ROM_2M+RAM` mode SRAM is always on, so this is harmless.)

The control register maps individual SPI signals to bits:

**Write bits:**

| Bit | Name     | Function |
|-----|----------|----------|
| 0   | SRM_ON   | SRAM bank select (0=ROM, 1=SRAM at 0x200000) |
| 2   | ROM_BANK | Flash bank select |
| 3   | LED      | Cartridge LED control |
| 4   | EXP_SS   | Expansion SPI chip select (active low, active low) |
| 5   | SDC_SS   | SD card chip select (active low) |
| 6   | SPI_CLK  | SPI clock |
| 7   | SPI_MOSI | SPI data out (master → SD card) |

**Read bits:**

| Bit | Name     | Function |
|-----|----------|----------|
| 0   | SPI_MISO | SPI data in (SD card → master) |
| 1-7 | —        | Fixed 0 or open bus |

### Clock Acceleration Trick

The hardware has a useful optimization: when SPI_CLK is written as 0,
every subsequent **read** of the control register automatically generates
a positive clock pulse on the SPI_CLK line. This means you can clock out
data by just reading the register repeatedly instead of toggling clock
bits manually. This roughly doubles the effective SPI throughput.

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

The menu firmware also has a fast 512-byte read in 68000 assembly
(`menu/spi.s`) that exploits the auto-clock feature: each read of
CTRL_REG auto-pulses SPI_CLK (when the CLK bit is 0), so the inner
loop is just `lsl.w #1, d0; or.w (a0), d0` repeated 8 times per byte,
fully unrolled.

### Performance Estimate

Each SPI bit requires ~3 register writes (MOSI + CLK high + CLK low)
through the cartridge bus. Each 16-bit write to the `$A130xx` range
takes ~8 68000 cycles (4 cycles for the instruction + wait states):

- **Per byte (write)**: ~24 register accesses = ~192 cycles
- **Per byte (read)**: ~16 register accesses = ~128 cycles
- **Per byte (auto-clock read)**: ~8 register reads = ~64 cycles
- **Per 512-byte sector**: ~32K-98K cycles
- **Theoretical max at 7.67 MHz**: ~78-120 KB/s (auto-clock read)

Real-world performance from the open-ed menu: loading a 4 MB ROM
takes 15-35 seconds (multi-block reads with CMD18), giving ~115-270
KB/s effective throughput. For single-sector reads (CMD17) the
overhead per command is higher, but even 20 KB/s is usable for a
command-line OS loading small files.

### What You Need to Implement

1. **SPI layer**: `spi_xfer()`, `spi_read()`, `spi_write()` bit-bang
2. **SD card initialization**: CMD0, CMD8, ACMD41 (SPI mode init sequence)
3. **SD card block read**: CMD17 (read single block), CMD18 (read multiple)
4. **SD card block write**: CMD24 (write single block), CMD25 (write multiple)
5. **FAT16/FAT32 filesystem**: To read files from the SD card
6. **Integration with Genix**: `/dev/sd0` block device or mount at `/sd`

### Address Space Compatibility

The open-ed's CTRL_REG at `$A130xx` does **not** conflict with
Genix's SRAM enable register at `$A130F1` because on the open-ed,
they are the **same register**. The open-ed decodes the entire
`$A130xx` range to one 8-bit latch. This means:

- Genix's existing `*(volatile uint8_t *)0xA130F1 = 0x03` writes to
  the same CTRL_REG — harmless in `ROM_2M+RAM` mode (SRAM already on)
- The SD card SPI pins (bits 4-7) and SRAM control (bit 0) share one
  register — SD card access code must preserve the SRM_ON bit

On a standard cartridge (no open-ed), the `$A130F0-$A130FF` range is
the Sega mapper and `$A130E0` is open bus. So open-ed-specific SPI
writes to `$A130E0` are harmless on non-open-ed hardware.

### SD Card Protocol

The open-ed menu firmware (`menu/disk.c`) implements standard SD card
SPI initialization: CMD0 (reset), CMD8 (voltage check), CMD55+ACMD41
loop (initialization), CMD58 (SDHC detection). Block reads via CMD17
(single) and CMD18 (multi-block). This is well-documented protocol
that Genix can implement directly.

### Source References

- Hardware: [open-ed.v](https://github.com/krikzz/open-ed/blob/master/open-ed.v) (Verilog)
- Menu firmware: [open-ed/menu/](https://github.com/krikzz/open-ed/tree/master/menu) (SPI, SD, FAT code)
- Mapper doc: [open-ed-mapper.txt](https://github.com/krikzz/open-ed/blob/master/open-ed-mapper.txt)
- Schematics: [open-ed repository](https://github.com/krikzz/open-ed) (MIT license)

## Mega EverDrive Pro — FPGA Command Interface (FIFO)

The Mega EverDrive Pro takes a completely different approach. It has a
Cyclone IV FPGA with an onboard MCU that handles all SD card and
filesystem operations. The 68000 communicates with the FPGA through a
**FIFO command mailbox** — you send high-level commands (open file, read
sector, list directory) and the FPGA/MCU does the actual SD card I/O.

### Hardware Interface

The Pro uses SSF-mode registers at `0xA130Dx`:

| Register | Address    | Access | Function |
|----------|-----------|--------|----------|
| FIFO_DATA | `0xA130D0` | R/W | Command/data FIFO (16-bit) |
| FIFO_STAT | `0xA130D2` | R   | FIFO status flags |
| SYS_STAT  | `0xA130D4` | R   | System status (device ID, boot state) |
| TIMER     | `0xA130D6` | R   | Hardware timer |

**FIFO status flags** (at `0xA130D2`):

| Bit | Name | Meaning |
|-----|------|---------|
| 0   | CPU_RD_RDY | Data available for CPU to read from FIFO |
| 1   | CPU_WR_RDY | FIFO ready for CPU to write |
| Others | — | Configuration/boot status |

**Device detection:**
```c
#define REG_SYS_STAT  (*(volatile uint16_t *)0xA130D4)
bool is_pro = (REG_SYS_STAT & 0xFFF0) == 0x55A0;
```

### Command Protocol

The 68000 sends commands by writing to the FIFO. Each command is a
16-bit command code followed by parameters. The MCU processes the
command, performs the SD/filesystem operation, and returns results
through the same FIFO.

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

The Pro's IO registers are only accessible when the ROM header declares
`SEGA SSF` as the system type (offset 0x100). In standard `SEGA MEGA
DRIVE` mode, the FIFO registers at `0xA130D0` are not active.

This means Genix would need a separate build configuration (or a
dual-mode ROM that detects the hardware and switches behavior).

### Performance

Since the MCU handles all SD/filesystem work:

- **File read**: Limited by FIFO throughput, not SPI bit-bang speed
- **Estimated throughput**: Much faster than the Open EverDrive
  (the FPGA/MCU has a dedicated high-speed SPI controller)
- **No FAT implementation needed on the 68000 side** — the MCU does it

### Source References

- SDK: [krikzz/mega-ed-pub/edio-mega](https://github.com/krikzz/mega-ed-pub/tree/master/edio-mega) (GPL-3.0)
- SSF spec: [extended-ssf.txt](https://krikzz.com/pub/support/mega-everdrive/pro-series/extended-ssf.txt)
- Detection: [Detecting Mega Everdrive Models](https://blog.roberthargreaves.com/2025/02/05/detecting-mega-everdrive-models)
- SRAM/banking: [SRAM & Bank Switching with Everdrives](https://blog.roberthargreaves.com/2025/03/10/sram-and-everdrives)

## Mega EverDrive X-Series (X3/X5/X7) — Hardware SPI Registers

The older X-series EverDrives expose SD card access through dedicated
SPI hardware registers (not bit-bang like Open EverDrive, not FIFO like
Pro):

| Register | Address    | Function |
|----------|-----------|----------|
| SD_DATA  | `0xA130E0` | SPI data register (8-bit or 16-bit) |
| USB_IO   | `0xA130E2` | USB data register |
| IO_STAT  | `0xA130E4` | Status: SPI ready, USB ready, SD card type |
| IO_CFG   | `0xA130E6` | Config: SD chip select, 16-bit mode, auto-read |

These registers also require `SEGA SSF` header mode. The 68000 must
implement the SD card command protocol (CMD0, CMD17, etc.) but the
actual SPI clocking is handled by hardware — much faster than bit-bang.

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

## Recommendations for Genix

### Phase 1: Open EverDrive SD Driver (recommended first target)

The Open EverDrive is the best first target because:

1. **Fully open source** — schematics, Verilog, mapper docs all MIT licensed
2. **No special ROM header needed** — works in standard mapper mode
3. **Compatible with current Genix build** — no header changes required
4. **Same hardware Genix already runs on** — documented in megadrive.md
5. **Educational value** — implementing SPI + SD card is instructive

The downsides are real but manageable:
- Slow throughput (~2-8 KB/s) — acceptable for a command-line OS
- Must implement SD card protocol from scratch (CMD0/CMD8/ACMD41/CMD17)
- Must implement FAT16 or FAT32 filesystem (or use a simpler custom FS)
- Uses ~1-2 KB of kernel code

**Suggested implementation:**

```
pal/megadrive/sd_spi.c    — SPI bit-bang layer (open-ed registers)
pal/megadrive/sd_card.c   — SD card init + block read/write
kernel/fatfs.c            — FAT16/32 read-only filesystem (or custom FS)
```

### Phase 2: Mega EverDrive Pro SD Driver

The Pro is the second target because:

1. **Much faster** — the MCU handles all low-level work
2. **Full file API** — open/read/write/close with paths, no FAT needed
3. **SDK available** — [edio-mega](https://github.com/krikzz/mega-ed-pub/tree/master/edio-mega) has working C code

But it requires:
- `SEGA SSF` ROM header — needs a separate build or runtime detection
- Separate driver code path (FIFO protocol vs SPI bit-bang)
- GPL-3.0 licensed SDK code (Genix would need compatible licensing)

### Alternative: Skip FAT, Use Raw SD

For the Open EverDrive, instead of implementing FAT16/32, Genix could
write its own minifs filesystem directly to the SD card:

- Use `tools/mkfs` to format the SD card with minifs
- The SD card driver provides block read/write (CMD17/CMD24)
- The existing kernel filesystem code handles everything above that
- No FAT code needed at all

This is simpler and more aligned with Genix's design. Users would
format the SD card with a host tool, not use it as a generic FAT volume.

### Dual-Target Detection

A single ROM could support both cartridges:

```c
void sd_init(void) {
    /* Try detecting Mega EverDrive Pro */
    uint16_t sys_stat = *(volatile uint16_t *)0xA130D4;
    if ((sys_stat & 0xFFF0) == 0x55A0) {
        /* Pro detected — use FIFO command interface */
        sd_driver = &sd_pro_driver;
        return;
    }

    /* Fall back to Open EverDrive SPI bit-bang */
    sd_driver = &sd_spi_driver;
}
```

However, the Pro requires `SEGA SSF` header mode to activate its FIFO
registers, which means runtime detection may not work without the
correct header. This needs further investigation — it may be possible
to detect the Pro even in standard mode, or the header could be set to
SSF with fallback logic for non-Pro hardware.
