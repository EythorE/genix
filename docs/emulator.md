# Workbench Emulator

The workbench emulator (`emu/emu68k`) is a minimal 68000 single-board
computer (SBC) emulator built for kernel development. It runs Genix in
a terminal with UART-based I/O — no framebuffer, no VDP, instant
startup, `printf`-debugging works.

## Why Not Develop Directly on the Mega Drive?

The Mega Drive has a VDP (video display processor), no UART, and
requires BlastEm with Xvfb or a display to test. Every debug cycle
involves screenshots or GDB remote sessions. This is miserable for
kernel development.

The workbench gives us:
- **Instant startup** — no ROM loading, no display initialization
- **Terminal I/O** — stdin/stdout mapped to a UART, works in any terminal
- **Host-side debugging** — printf just works, no cross-debugger needed
- **Fast iteration** — `make run` builds and boots in under 2 seconds
- **Piped testing** — stdin can come from a file or pipe for automation

Every feature developed on the workbench must ultimately run on real
Mega Drive hardware. The workbench is for iteration speed, not as an
end target.

## Architecture

The emulator wraps [Musashi](https://github.com/kstenerud/Musashi), a
cycle-accurate Motorola 68000 CPU core written in C. Musashi is the same
core used in MAME for Mega Drive/Genesis emulation. It's MIT-licensed,
battle-tested, and requires only a few callback functions to integrate.

The emulator adds a simple memory map around Musashi:

```
Address         Device          Description
0x000000-0x0FFFFF  RAM (1 MB)     Kernel loaded here at reset
0xF00000-0xF00001  UART data      Read = getchar, Write = putchar
0xF00002-0xF00003  UART status    Bit 0 = RX ready, Bit 1 = TX ready
0xF10000-0xF10003  Timer count    32-bit tick counter (read-only)
0xF10004-0xF10005  Timer control  Write 1 = enable, 0 = disable
0xF20000-0xF20001  Disk command   Write 1 = read, 2 = write
0xF20004-0xF20007  Disk block     32-bit block number
0xF20008-0xF20009  Disk status    0 = busy, 1 = done, 0x80 = error
0xF20010-0xF2040F  Disk buffer    1024-byte data buffer
0xF30000           Power-off      Write any value to quit gracefully
```

### CPU Core (Musashi)

Musashi provides:
- Cycle-accurate 68000 instruction execution
- All addressing modes, all instructions
- Interrupt handling (autovector and user vector)
- Exception handling (bus error, address error, illegal instruction)

Genix calls `m68k_execute(1000)` in a loop, executing 1000 cycles per
batch. Between batches, the emulator checks the timer and fires
interrupts.

### UART

The UART maps stdin/stdout to the 68000 address space. The host
terminal is set to raw mode (no echo, no line buffering, no signal
processing) so every keystroke passes directly to the kernel.

- **Read `0xF00000`**: Returns one byte from stdin (blocking in kernel,
  non-blocking in emulator via `select()`)
- **Write `0xF00000`**: Writes one byte to stdout
- **Read `0xF00002`**: Returns status (bit 0 = character available,
  bit 1 = transmit ready, always set)

### Timer

A 100 Hz timer drives the kernel's preemptive scheduling. The emulator
counts CPU cycles and fires a level-6 autovector interrupt every
`CYCLES_PER_TICK` (76,700 cycles at 7.67 MHz / 100 Hz).

The kernel enables the timer by writing 1 to `0xF10004`. The 32-bit
tick counter at `0xF10000` increments each tick.

### Disk

A block device backed by a host file (the filesystem image). The kernel
writes a block number to `0xF20004`, then writes a command (1=read,
2=write) to `0xF20000`. The operation completes synchronously — the
status register reads 1 (done) or 0x80 (error) immediately.

Data is transferred through the 1024-byte buffer at `0xF20010`. For
reads, the kernel copies from the buffer after the command. For writes,
the kernel fills the buffer before issuing the command.

### Power-Off

Writing any value to `0xF30000` causes the emulator to exit gracefully,
restoring the terminal. The kernel's `pal_halt()` uses this.

## Usage

### Building

```bash
make emu          # Build the emulator (host binary)
```

This compiles `emu/emu68k.c` and the Musashi CPU core into `emu/emu68k`.
Only the host `gcc` is needed — no cross-compiler.

### Running

```bash
make run          # Build everything + run in emulator
```

Or manually:
```bash
emu/emu68k kernel/kernel.bin disk.img
```

The emulator prints startup messages to stderr:
```
[emu] Loaded 28672 bytes from kernel/kernel.bin
[emu] Disk disk.img opened read-write
[emu] Running...
```

Then the Genix kernel boots and you get a shell prompt (`>`).

### Keyboard Controls

| Key | Action |
|-----|--------|
| **Ctrl+]** | Quit the emulator (like telnet/QEMU escape) |
| **Ctrl+C** | Sent to the kernel (will be SIGINT when signals are implemented) |
| All other keys | Passed directly to the kernel via UART |

**Ctrl+]** is the only host-side escape. Everything else goes to Genix.

### Exiting

Three ways to exit the emulator:

1. **`halt`** command in the Genix shell — calls `pal_halt()`, which
   writes to the power-off register
2. **Ctrl+]** — host-side emergency escape (like telnet)
3. **EOF on stdin** — if input is piped, the emulator exits when the
   pipe closes

### Shell Commands

Once booted, the Genix built-in shell supports:

```
> ls                  # List files in current directory
> ls /bin             # List files in /bin
> cat /bin/hello      # Show file contents (raw binary, not useful)
> mkdir /tmp          # Create a directory
> echo hello world    # Print text
> exec /bin/hello     # Load and run a user program
> exec /bin/echo hi   # Run echo with arguments
> mem                 # Show memory allocator state
> help                # Show available commands
> halt                # Shut down (exits emulator)
```

### Disk Image

`make disk` creates `disk.img` using `tools/mkfs.minifs`. This is a
minifs filesystem image containing the user programs from `apps/`:

```bash
# Manually create a disk image (512 blocks of 1024 bytes = 512 KB)
tools/mkfs.minifs disk.img 512 apps/hello apps/echo apps/cat
```

Programs are placed at `/bin/<name>` in the filesystem.

## Source Files

| File | Lines | Description |
|------|-------|-------------|
| `emu/emu68k.c` | ~400 | Main emulator: memory map, I/O devices, main loop |
| `emu/musashi/` | ~15K | Musashi 68000 CPU core (vendored, not modified) |
| `emu/Makefile` | ~20 | Build the emulator |

The entire emulator-specific code is `emu68k.c`. Musashi provides the
CPU; we provide the board.

## How It Connects to the Kernel

The kernel doesn't know it's running in an emulator. It talks to
hardware through the Platform Abstraction Layer (PAL):

```
Kernel                 PAL (workbench)           Emulator
------                 ---------------           --------
kprintf("hello")  -->  pal_console_putc('h') --> write to 0xF00000 --> putchar('h')
pal_console_getc() --> read 0xF00000          --> read(stdin)
pal_disk_read()    --> write DISK_CMD=1       --> fread(disk.img)
pal_halt()         --> write 0xF30000         --> g_quit = 1
```

The same kernel code runs on Mega Drive hardware — only the PAL
implementation changes (VDP output instead of UART, Saturn keyboard
instead of stdin, ROM/SRAM instead of host file).

## Limitations

- **No VDP** — text only, no tile graphics or sprites
- **No Saturn keyboard emulation** — uses host stdin instead
- **No Z80** — the Mega Drive's secondary CPU is not emulated
- **No sound** — no YM2612, no PSG
- **Synchronous disk** — real hardware has DMA latency; the emulator
  completes instantly
- **1 MB RAM** — vs 64 KB on real Mega Drive (catches logic bugs but
  not memory pressure bugs)

These are intentional. The workbench is for kernel logic, not hardware
fidelity. Use BlastEm or real hardware for Mega Drive validation.
