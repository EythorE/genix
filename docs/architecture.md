# System Architecture

## Overview

Genix is a minimal, single-user, POSIX-enough operating system for the
Motorola 68000 CPU. It replaces the FUZIX kernel with ~3000 lines of new
code while reusing proven Mega Drive drivers from the
[EythorE/FUZIX megadrive branch](https://github.com/EythorE/FUZIX/tree/megadrive).

```
User Programs (sh, hello, cat ...)  — linked with libc syscall stubs
        │ TRAP #0
Kernel (proc, fs, dev, mem, exec, syscall)
        │
Platform Abstraction Layer (PAL)
        │
Hardware / Emulator
```

## Design Philosophy

- **Readable** — entire kernel fits in your head (~3000-5000 lines)
- **Single-user** — no UIDs, permissions, login (like DOS with a Unix API)
- **POSIX-enough** — enough syscalls for standard C programs and Unix utilities
- **No MMU required** — flat memory, no virtual address translation
- **Small** — measured in thousands of lines, not tens of thousands

## Directory Structure

```
genix/
├── docs/           # This documentation
├── emu/            # Workbench 68000 emulator (Musashi-based, host binary)
│   └── musashi/    # Musashi 68000 CPU core (vendored, MIT license)
├── kernel/         # OS kernel
│   ├── crt0.S      # Boot: vector table, early init, exception handlers
│   ├── main.c      # kmain, built-in shell
│   ├── proc.c      # Process management, syscall dispatch
│   ├── exec.c      # Binary loader, exec()
│   ├── exec_asm.S   # exec_enter/exec_leave (kernel↔user switch)
│   ├── fs.c        # Filesystem (minifs)
│   ├── mem.c       # Memory allocator (first-fit with coalescing)
│   ├── buf.c       # Block buffer cache
│   ├── dev.c       # Device table
│   ├── kprintf.c   # Kernel printf
│   ├── string.c    # memcpy, strcmp, etc.
│   ├── divmod.S    # 32-bit divide/modulo for 68000 (replaces libgcc)
│   └── kernel.h    # All kernel types, constants, prototypes
├── pal/            # Platform Abstraction Layer
│   ├── pal.h       # PAL interface definition
│   ├── workbench/  # Emulated SBC (UART, timer, disk via memory-mapped I/O)
│   └── megadrive/  # Sega Mega Drive (VDP, Saturn keyboard, ROM/SRAM disk)
├── libc/           # Minimal C library for user programs
│   ├── syscalls.S  # TRAP #0 stubs (_exit, read, write, open, close, ...)
│   ├── stdio.c     # puts, printf (minimal)
│   └── string.c    # strlen, strcpy, memcpy, etc.
├── apps/           # User programs
│   ├── crt0.S      # User program entry point (_start → main → _exit)
│   ├── hello.c     # Hello world
│   ├── echo.c      # echo
│   └── cat.c       # cat
├── tools/          # Host tools
│   ├── mkbin.c     # ELF → Genix flat binary converter
│   └── mkfs.c      # minifs filesystem image creator
└── tests/          # Host unit tests (run with just gcc, no cross-compiler)
    ├── test_mem.c   # Memory allocator tests
    ├── test_string.c # String function tests
    └── test_exec.c  # Binary header validation tests
```

## Layered Architecture

### Layer 1: Hardware / Emulator

Two platforms are supported:

**Workbench SBC** (development platform):
- Musashi 68000 emulator running on the host
- Memory-mapped UART (stdin/stdout), timer (100 Hz), disk (host file)
- Runs in any terminal, no X11 needed, instant startup

**Mega Drive** (target hardware):
- Motorola 68000 at 7.67 MHz
- 64 KB main RAM, up to 4 MB ROM, optional 512 KB SRAM
- VDP for text output, Saturn keyboard for input

### Layer 2: Platform Abstraction Layer (PAL)

The PAL provides a uniform interface that the kernel calls:

```c
void     pal_init(void);
void     pal_console_putc(char c);
int      pal_console_getc(void);        // blocking
int      pal_console_ready(void);
void     pal_disk_read(int dev, uint32_t block, void *buf);
void     pal_disk_write(int dev, uint32_t block, void *buf);
uint32_t pal_mem_start(void);
uint32_t pal_mem_end(void);
void     pal_timer_init(int hz);
uint32_t pal_timer_ticks(void);
```

Each platform implements these ~10 functions. The kernel never touches
hardware directly.

### Layer 3: Kernel

One `.c` file per subsystem, all sharing `kernel.h`. No deep call
hierarchies, no dynamic dispatch (except the device table). See
[kernel.md](kernel.md) for details.

### Layer 4: User Programs

Linked with `crt0.S` + `libc.a`, compiled to Genix flat binaries via
`mkbin`. Enter the kernel via `TRAP #0`. See [syscalls.md](syscalls.md)
and [binary-format.md](binary-format.md).

## Memory Maps

### Workbench (1 MB RAM)

```
0x000000  ┌──────────────────┐
          │ Interrupt Vectors │  1 KB (256 × 4-byte entries)
0x000400  ├──────────────────┤
          │ Kernel code+data │  ~16 KB
          ├──────────────────┤
          │ Kernel BSS       │
          ├──────────────────┤  ← pal_mem_start() (after _end)
          │ Kernel heap      │  (kmalloc: proc table, bufs, ofiles, inodes)
          │ ~200 KB          │
0x040000  ├──────────────────┤  ← USER_BASE
          │ User program     │  (text + data + BSS, loaded by exec)
          │                  │
0x0F0000  ├──────────────────┤  ← USER_TOP (user stack grows down from here)
          │ (reserved)       │
0x0FF000  ├──────────────────┤
          │ Kernel stack     │  4 KB (grows down from 0x100000)
0x100000  └──────────────────┘  ← _stack_top

I/O Registers (memory-mapped):
0xF00000  UART data/status
0xF10000  Timer count/control
0xF20000  Disk command/block/status/buffer
```

### Mega Drive

```
ROM (up to 4 MB):
0x000000  ┌──────────────────┐
          │ Vectors + Header │  512 bytes
0x000200  ├──────────────────┤
          │ Kernel code      │  ~16 KB
          ├──────────────────┤
          │ .rodata (font)   │
          ├──────────────────┤
          │ ROM disk         │  ~1.9 MB (read-only filesystem)
0x3FFFFF  └──────────────────┘

Cartridge SRAM (optional, up to 512 KB):
0x200000  ┌──────────────────┐
          │ RAM disk         │  (read-write filesystem)
0x27FFFF  └──────────────────┘

Main RAM (64 KB):
0xFF0000  ┌──────────────────┐
          │ Kernel BSS+data  │  ~4 KB
          ├──────────────────┤
          │ Kernel heap      │  ~24 KB
          ├──────────────────┤
          │ User processes   │  (each ~8-16 KB)
          ├──────────────────┤
          │ Kernel stack     │  ~2 KB
0xFFFFFF  └──────────────────┘
```

## Boot Sequence

1. CPU reads initial SSP from vector 0 (`_stack_top`) and initial PC from
   vector 1 (`_start`)
2. `_start` (kernel/crt0.S): disable interrupts, zero BSS, call `kmain()`
3. `kmain()` (kernel/main.c):
   - `pal_init()` — hardware-specific setup
   - `mem_init()` — initialize kernel heap allocator
   - `buf_init()` — allocate buffer cache (16 × 1 KB blocks)
   - `dev_init()` — register console and disk devices
   - `fs_init()` — mount root filesystem from disk
   - `proc_init()` — initialize process table, set up fd 0/1/2
   - Enable interrupts (`move.w #0x2000, %sr`)
   - Start built-in shell
