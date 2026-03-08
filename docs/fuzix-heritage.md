# Fuzix Heritage

What Genix took from Fuzix, what's different, and what's missing.

## Background

[FUZIX](https://github.com/EythorE/FUZIX) is a multi-user Unix clone
supporting 30+ platforms. The
[megadrive branch](https://github.com/EythorE/FUZIX/tree/megadrive)
ports it to the Sega Mega Drive. Genix replaces the FUZIX kernel
(15K+ lines of someone else's code) with ~3000 lines we understand,
while reusing the Mega Drive platform drivers that already work.

## What We Took

### Mega Drive Drivers (pal/megadrive/)

All files in `pal/megadrive/` are adapted from FUZIX's
`Kernel/platform/platform-megadrive/`:

| Genix File | Fuzix Source | What it does |
|------------|-------------|-------------|
| `crt0.S` | `crt0.S` | Boot: vector table, Z80 halt, VDP init, BSS clear |
| `vdp.S` | `vdp.S` | VDP register setup, VRAM clear, palette, font upload |
| `devvt.S` | `devvt.S` | Text output: plot_char, scroll, cursor, clear |
| `dbg_output.S` | `dbg_output.S` | BlastEm debug channel output |
| `fontdata_8x8.c` | `font/font8x8.c` | 8x8 bitmap font (ASCII 32-127) |
| `keyboard.c` | `keyboard.c` | Saturn keyboard state machine |
| `keyboard.h` | `keyboard.h` | Keyboard API |
| `keyboard_read.S` | `keyboard_read.S` | Controller port polling (assembly) |
| `keycode.h` | `keycode.h` | Scancode definitions |
| `macros.S` | `macros.S` | Assembly helper macros |
| `control_ports.def` | `control_ports.def` | Controller port register addresses |
| `platform.c` | (new) | PAL adapter wrapping the Fuzix drivers |

`platform.c` is new — it implements the Genix PAL interface by calling
the Fuzix assembly routines. The assembly files are adapted with minimal
changes (mostly removing Fuzix-specific includes and adapting to Genix's
linker script).

### Design Decisions

These were informed by Fuzix's experience on the Mega Drive:

| Decision | From Fuzix | Genix adaptation |
|----------|-----------|-----------------|
| **vfork() only** | Fuzix uses vfork+exec on no-MMU platforms | Same approach, no fork() |
| **Flat binary format** | Fuzix uses 16-byte a.out header | Genix uses 32-byte header (simpler, will migrate to Fuzix a.out) |
| **Preemptive scheduler** | VBlank-driven round-robin | Same timer source, simpler scheduler |
| **ttyinq buffer** | uint8_t head/tail, 256-byte circular buffer | Same design (division-free) |
| **Context switch** | MOVEM.L d0-d7/a0-a6 | Same approach |
| **TRAP for syscalls** | TRAP #14 with args in d1-d4/a0-a2 | TRAP #0 with args in d1-d4 only |
| **Block size 1024** | Fuzix uses 512 on some platforms, 1024 on others | 1024 everywhere |
| **Buffer cache** | 16 1KB buffers with dirty tracking | Same |

## What's Different

### Genix Kernel vs Fuzix Kernel

| Aspect | Fuzix | Genix |
|--------|-------|-------|
| **Size** | ~15,000 lines (kernel alone) | ~3,000 lines target |
| **Multi-user** | Yes (UIDs, permissions, login) | No |
| **Platforms** | 30+ (Z80, 6809, 68000, ARM, ...) | 68000 only |
| **fork()** | Supported on some platforms | Not supported (vfork only) |
| **Filesystem** | Classic Unix FS with dual-indirect | minifs (single-indirect, simpler) |
| **Process table** | Complex with signal masks, timers | Minimal 16-slot array |
| **TTY** | Full line discipline, cooked/raw, multi-tty | Simple PAL console (TTY planned) |
| **Device model** | Major/minor with full dev_t | Simple function-pointer table |
| **Syscall ABI** | TRAP #14, mix of d-regs and a-regs | TRAP #0, d0-d4 only |
| **Binary format** | a.out with relocations | Flat binary at fixed address |
| **C library** | Full Fuzix libc (~5 KB compiled) | Minimal stubs (~200 lines) |

### Syscall Convention

Fuzix uses `TRAP #14` and splits arguments between data and address
registers:

```
Fuzix:  d0=syscall#, d1=arg1, a0=arg2(ptr), a1=arg3(ptr)
Genix:  d0=syscall#, d1=arg1, d2=arg2, d3=arg3, d4=arg4
```

Genix's convention is cleaner — all arguments in data registers. Since
we recompile all apps from source, the libc stubs handle the mapping.
Apps never make raw syscalls.

### Filesystem

Fuzix uses a classic Unix filesystem with 512-byte blocks and
dual-indirect blocks. Genix's minifs uses 1024-byte blocks and
single-indirect (max 524 KB per file). This is simpler and sufficient
for the Mega Drive.

### Binary Format

Fuzix uses a 16-byte `a.out` header with 16-bit segment sizes and
kernel-applied relocations. Genix currently uses a 32-byte flat binary
header with no relocations (fixed load address at `USER_BASE`).

The plan is to migrate to Fuzix's a.out format when multitasking is
added, because:
1. 143+ Fuzix utilities are compiled against it
2. Relocations are needed for loading at dynamic addresses
3. The header is proven on this exact hardware

## What's Missing (Fuzix Features Not Yet in Genix)

### Currently unimplemented

| Feature | Fuzix status | Genix status |
|---------|-------------|-------------|
| Multitasking | Working | Planned (Phase 2b) |
| vfork() | Working | Stub (returns -ENOSYS) |
| waitpid() | Working | Stub (returns -ECHILD) |
| Pipes | Working | Planned (Phase 2c) |
| Signals | Working (full POSIX set) | Planned (Phase 2d) |
| TTY line discipline | Full cooked/raw | Simple PAL console |
| Job control (^Z, fg, bg) | Working | Planned (Phase 2d) |
| termios | Full implementation | Not implemented |
| Process groups | Working | Planned (Phase 2d) |
| Binary relocations | Working | Not implemented |
| /dev/null, /dev/zero | Available | Not implemented |
| getcwd() | Available | Not implemented |
| Full libc (stdio, etc.) | ~5 KB, 60+ source files | Minimal stubs only |
| User shell (sh) | Full shell with job control | Built-in kernel shell |
| 143+ Unix utilities | Available | 3 (hello, echo, cat) |

### Fuzix features we deliberately exclude

| Feature | Why |
|---------|-----|
| Multi-user (UIDs, permissions) | Single-user system |
| Login / passwd / groups | No users |
| setuid / setgid | No users |
| File permissions (chmod) | No users |
| Network stack | Not a goal |
| Banked memory support | 68000 has flat memory |
| Floppy / serial / RS-232 drivers | Mega Drive doesn't have these |
| 30+ platform ports | 68000 only |

## Fuzix Source References

For anyone working on Genix's missing features, here are the key Fuzix
source files to study:

### Kernel (68000-specific)

- **Context switch**: `Kernel/cpu-68000/lowlevel-68000.S`
  - MOVEM-based register save/restore, USP handling, RTE
- **Syscall entry**: `Kernel/cpu-68000/lowlevel-68000.S`
  - TRAP #14 handler, argument marshalling
- **Signal delivery**: `Kernel/cpu-68000/lowlevel-68000.S`
  - User-stack trampoline construction

### Kernel (generic)

- **TTY line discipline**: `Kernel/tty.c`
  - Cooked/raw mode, echo, erase, signal generation
- **Process management**: `Kernel/process.c`
  - vfork, exec, exit, wait, scheduler
- **Signals**: `Kernel/signal.c`
  - Signal dispatch, default actions, sigreturn
- **Pipes**: `Kernel/filesys.c` (pipe section)
  - Ring buffer, blocking read/write

### Platform (Mega Drive)

All in `Kernel/platform/platform-megadrive/`:
- `config.h` — platform constants
- `devtty.c` — TTY platform driver (tty_putc, keyboard polling)
- `devrd.c` — ROM disk + SRAM disk driver
- `main.c` — platform init, memory map
- `tricks.S` — vfork/exec context manipulation

### Library (libc)

- `Library/libs/` — 60+ C source files
- `Library/libs/Makefile.68000` — build flags
- `Library/include/` — headers
- Key files: `stdio.c`, `fprintf.c`, `malloc.c`, `string.c`, `ctype.c`,
  `dirent.c`, `termios.c`, `signal.c`

Repository: <https://github.com/EythorE/FUZIX/tree/megadrive>
