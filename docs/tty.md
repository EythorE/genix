# TTY & Console

## Current Implementation

Genix has a full TTY line discipline (`kernel/tty.c`) implementing
cooked (canonical) and raw input modes, echo, line editing, signal
generation, and POSIX termios ioctls. The TTY layer sits between
the device table and the PAL console drivers.

### Workbench Console

The workbench emulator maps a UART at `0xF00000`:

```
0xF00000  UART data register (read: RX byte, write: TX byte)
0xF00002  UART status (bit 0: RX ready, bit 1: TX ready)
```

The emulator puts the host terminal in raw mode (`termios` with
`ICANON|ECHO|ISIG` disabled) and connects stdin/stdout to the UART.
Characters flow directly between the host terminal and the kernel's
`pal_console_getc/putc`.

### Mega Drive Console

The Mega Drive PAL (`pal/megadrive/platform.c`) implements console I/O
using the VDP (Video Display Processor) for output and a Saturn keyboard
for input.

#### VDP Text Output

The VDP is configured in text mode: 40 columns by 28 rows, using an
8x8 pixel font. Characters are rendered as tiles in VRAM.

```c
static int cursor_x = 0, cursor_y = 0;
#define COLS 40
#define ROWS 28
```

`pal_console_putc()` tracks cursor position with separate x/y
variables (no division by COLS — see [68000-programming.md](68000-programming.md)):

- Regular characters: `plot_char(y, x, c)`, advance x
- `'\n'`: reset x, advance y
- `'\r'`: reset x
- `'\b'`: decrement x, clear character
- x >= COLS: wrap to next line
- y >= ROWS: `scroll_up()`, `clear_lines(ROWS-1, 1)`, stay on last row

The assembly routines in `pal/megadrive/devvt.S` handle the VDP
hardware:

| Function | Description |
|----------|-------------|
| `plot_char(y, x, c)` | Write a tile at (y, x) in the name table |
| `clear_across(y, x, n)` | Clear n tiles starting at (y, x) |
| `clear_lines(y, n)` | Clear n full rows |
| `scroll_up()` | Scroll the entire screen up by one row |
| `scroll_down()` | Scroll down by one row |
| `cursor_on(y, x)` | Show cursor at position |
| `cursor_off()` | Hide cursor |

These are adapted directly from FUZIX's `platform-megadrive/devvt.S`:
<https://github.com/EythorE/FUZIX/blob/megadrive/Kernel/platform/platform-megadrive/devvt.S>

#### VDP Initialization (`vdp.S`)

Assembly routines for VDP setup:

| Function | Description |
|----------|-------------|
| `VDP_LoadRegisters()` | Write initial register set |
| `VDP_ClearVRAM()` | Zero all 64 KB of VRAM |
| `VDP_writePalette()` | Load 16-color palette |
| `VDP_fontInit()` | Upload 8x8 font tiles to VRAM |
| `VDP_reinit()` | Full init sequence (all of the above) |

Source: adapted from FUZIX's `platform-megadrive/vdp.S`.

#### Font

The 8x8 bitmap font (`pal/megadrive/fontdata_8x8.c`) covers ASCII
32-127, one byte per row, 8 bytes per character. Source:
<https://github.com/EythorE/FUZIX/blob/megadrive/Kernel/font/font8x8.c>

#### Saturn Keyboard Input

The Saturn keyboard connects through the Mega Drive controller port.
The driver is a state machine that handles:

- Key press/release detection
- Modifier keys (Shift, Ctrl, Alt)
- Key repeat
- Scancode-to-ASCII translation

Files adapted from FUZIX:

| File | Description |
|------|-------------|
| `keyboard.c` | State machine, modifier tracking, repeat |
| `keyboard.h` | `keyboard_init()`, `keyboard_read()` prototypes |
| `keyboard_read.S` | Low-level controller port polling (assembly) |
| `keycode.h` | Scancode definitions |
| `control_ports.def` | Controller port register addresses |

Source: <https://github.com/EythorE/FUZIX/tree/megadrive/Kernel/platform/platform-megadrive>

`keyboard_read()` is non-blocking — returns 0 if no key is available.
`pal_console_getc()` polls it in a busy loop.

## TTY Line Discipline (kernel/tty.c)

The TTY subsystem implements a Fuzix-inspired line discipline with:

### Three-Layer Architecture

```
┌─────────────────────────────────────┐
│  User: read() / write() / ioctl()   │
├─────────────────────────────────────┤
│  tty.c: Line discipline             │
│  - Cooked mode (canonical input)    │
│  - Raw mode (pass-through)          │
│  - Echo, erase (^H), kill (^U)     │
│  - Signal generation (^C, ^Z, ^\)  │
│  - termios support                  │
│  - Job control (foreground pgrp)    │
├─────────────────────────────────────┤
│  devtty.c: Platform driver          │
│  - tty_putc() → VDP tile output    │
│  - VBlank ISR → keyboard polling   │
│  - tty_inproc() injection          │
├─────────────────────────────────────┤
│  Hardware: VDP + Saturn keyboard    │
└─────────────────────────────────────┘
```

### TTY Data Structures

```c
struct tty {
    struct termios termios;     // c_iflag, c_oflag, c_cflag, c_lflag, c_cc[]
    uint8_t  inq[256];         // circular input buffer
    uint8_t  inq_head;         // write pointer (ISR writes here)
    uint8_t  inq_tail;         // read pointer (reader reads here)
    uint8_t  fg_pgrp;          // foreground process group
    uint8_t  minor;
    uint8_t  flags;
};
```

The 256-byte circular buffer with `uint8_t` head/tail indices wraps at
256 naturally — **no modulo instruction needed** on the 68000. This is a
deliberate optimization from Fuzix. See [68000-programming.md](68000-programming.md).

### Key Features from Fuzix tty.c

- **Cooked mode**: line buffering with echo, backspace erase, `^U` kill
- **Raw mode**: immediate character delivery
- **Signal generation**: `^C` → SIGINT, `^Z` → SIGTSTP, `^\` → SIGQUIT
- **`^D` on empty line** → EOF
- **ISR-safe input**: `tty_inproc()` designed for interrupt context;
  head/tail buffer writes are single-word (atomic on 68000)
- **termios**: `TCGETS`/`TCSETS` ioctls, `TIOCGWINSZ` (40x28)

### Implementation Status

| Feature | Status | Notes |
|---------|--------|-------|
| Line discipline (tty.c) | **Done** | ~320 lines, cooked/raw/echo/erase/kill |
| `/dev/tty`, `/dev/console` | **Done** | Device nodes created at boot |
| `TIOCGWINSZ` (40x28) | **Done** | Returns Mega Drive VDP dimensions |
| termios ioctls | **Done** | TCGETS, TCSETS, TCSETSW, TCSETSF |
| Signal generation (^C, ^\, ^Z) | **Done** | ISIG with NOFLSH support |
| Output processing (ONLCR) | **Done** | NL→CR-NL in user writes |
| Interrupt-driven keyboard | Planned (Phase 4) | Currently polling via PAL |
| Multiple TTY devices | Planned (Phase 4) | NTTY=1 currently |
| Job control (fg/bg) | Planned | Requires Phase 2d signals |
