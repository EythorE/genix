# EverDrive SD Card Access Research

Research into whether Genix can access the SD card on Open EverDrive and
Mega EverDrive Pro cartridges while a ROM is running.

**TL;DR: Yes, both cartridges expose SD card access to the running 68000
CPU, but through very different mechanisms.**

## Open EverDrive — Bit-Bang SPI

The Open EverDrive is a discrete-logic cartridge (no FPGA, no MCU) that
exposes the SD card's SPI bus directly through its control register at
`0xA13000`. The 68000 must bit-bang the SPI protocol entirely in
software.

### Hardware Interface

The control register at `0xA13000` maps individual SPI signals to bits:

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

### Bit-Bang SPI Transfer (conceptual)

```c
#define CTRL_REG  (*(volatile uint16_t *)0xA13000)

/* Base state: SRAM on, SD chip selected (active low = 0), clock low */
#define SPI_BASE  (0x0001)  /* SRM_ON=1, SDC_SS=0 (selected) */

/* Send/receive one byte over SPI (mode 0, MSB first) */
static uint8_t spi_xfer(uint8_t out) {
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        uint16_t mosi = (out >> i) & 1 ? 0x0080 : 0x0000;
        /* Clock low, set MOSI */
        CTRL_REG = SPI_BASE | mosi;
        /* Clock high — latch data */
        CTRL_REG = SPI_BASE | mosi | 0x0040;
        /* Read MISO */
        in = (in << 1) | (CTRL_REG & 1);
    }
    /* Clock low */
    CTRL_REG = SPI_BASE;
    return in;
}
```

With the auto-clock trick, the read path can be faster:

```c
/* Fast read: set SPI_CLK=0, then each read auto-pulses clock */
static uint8_t spi_read_fast(void) {
    uint8_t in = 0;
    CTRL_REG = SPI_BASE | 0x0080;  /* MOSI high (0xFF), CLK=0 */
    for (int i = 0; i < 8; i++) {
        in = (in << 1) | (CTRL_REG & 1);  /* read + auto-clock */
    }
    return in;
}
```

### Performance Estimate

Each SPI bit requires at least 2 register accesses (write + read) at
~4-8 68000 cycles each through the cartridge bus. That's roughly:

- **Per byte**: ~64-128 cycles (bit-bang) or ~32-64 cycles (auto-clock read)
- **Per 512-byte sector**: ~16,384-65,536 cycles
- **At 7.67 MHz**: ~2-8 KB/s for bit-bang, potentially faster with auto-clock

This is slow but usable for a simple OS. The SD card itself in SPI mode
is limited to ~400 KB/s theoretical max, and the bus overhead dominates.

### What You Need to Implement

1. **SPI layer**: `spi_xfer()`, `spi_read()`, `spi_write()` bit-bang
2. **SD card initialization**: CMD0, CMD8, ACMD41 (SPI mode init sequence)
3. **SD card block read**: CMD17 (read single block), CMD18 (read multiple)
4. **SD card block write**: CMD24 (write single block), CMD25 (write multiple)
5. **FAT16/FAT32 filesystem**: To read files from the SD card
6. **Integration with Genix**: `/dev/sd0` block device or mount at `/sd`

### Source References

- Hardware: [open-ed.v](https://github.com/krikzz/open-ed/blob/master/open-ed.v) (Verilog)
- Mapper doc: [open-ed-mapper.txt](https://github.com/krikzz/open-ed/blob/master/open-ed-mapper.txt)
- Schematics: [open-ed repository](https://github.com/krikzz/open-ed)

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
| **Throughput** | ~2-8 KB/s | Fast (MCU-limited) | Medium (HW SPI) |
| **Code complexity** | High (SPI + SD + FAT) | Low (file API) | Medium (SD + FAT) |
| **Detection** | Check `0xA13000` | `REG_SYS_STAT & 0xFFF0 == 0x55A0` | `IO_STAT` check |

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
