# Genix — A Small 68000 OS for Mega Drive

## Project Overview

Genix is a minimal, single-user, POSIX-enough operating system for the Motorola 68000,
targeting the Sega Mega Drive. It replaces the FUZIX kernel with ~3000 lines of new code
while reusing proven Mega Drive drivers.

## Build

```bash
# Host toolchain (emulator)
make emu

# Kernel for workbench emulator
make kernel

# Full system (kernel + apps, run in emulator)
make run

# Mega Drive ROM
make megadrive
```

### Prerequisites

- `gcc` (host, for emulator)
- `m68k-linux-gnu-gcc` (cross compiler for kernel/apps)
- `make`

Install on Ubuntu/Debian:
```bash
sudo apt-get install gcc-m68k-linux-gnu binutils-m68k-linux-gnu
```

## Architecture

```
User Programs (sh, apps)  — linked with newlib stubs
        │ TRAP #0
Kernel (proc, fs, dev, mem, syscall)
        │
Platform Abstraction Layer (PAL)
        │
Hardware / Emulator
```

## Key Design Decisions

- **No fork()** — vfork()+exec() only (no MMU required)
- **No multi-user** — no UIDs, permissions, login
- **Custom filesystem (minifs)** — classic Unix inode layout
- **Single-tasking first** — then add multi-tasking
- **Musashi** 68000 emulator for development

## Directory Structure

```
genix/
├── emu/          # Workbench 68000 emulator (Musashi-based)
├── kernel/       # OS kernel
├── pal/          # Platform Abstraction Layer
│   ├── workbench/  # Emulated SBC
│   └── megadrive/  # Sega Mega Drive
├── libc/         # newlib syscall stubs
├── apps/         # Userspace programs (init, sh)
└── tools/        # Host tools (mkfs, etc.)
```

## Testing

```bash
# Run in workbench emulator
make run

# Run in blastem (Mega Drive)
make blastem
```

## Syscall Convention

- Enter kernel via `TRAP #0`
- Syscall number in `d0`, arguments in `d1-d4`
- Return value in `d0` (negative = -errno)
