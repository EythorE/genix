# Genix — A Small 68000 OS for Mega Drive

Genix is a minimal, single-user, POSIX-enough operating system for the Motorola 68000,
targeting the Sega Mega Drive. It replaces the FUZIX kernel with ~3000 lines of new code
while reusing proven Mega Drive drivers.

## Prerequisites

- `gcc` (host, for emulator and tests)
- `m68k-linux-gnu-gcc` (cross compiler for kernel/apps)
- `make`

Install on Ubuntu/Debian:
```bash
sudo apt-get install gcc-m68k-linux-gnu binutils-m68k-linux-gnu
```

## Build

```bash
# Everything (emulator + kernel + tools + disk image)
make

# Run in the workbench emulator
make run

# Build a Mega Drive ROM
make megadrive
```

Individual targets:
```bash
make emu       # Host emulator only (Musashi-based 68000 SBC)
make kernel    # Cross-compile the kernel
make tools     # Host tools (mkfs.minifs)
make disk      # Create a minifs disk image
```

## Testing

```bash
# Run host unit tests (no cross-compiler needed)
make test
```

Tests cover `kernel/string.c` and `kernel/mem.c` — pure logic that runs natively on the host.

CI runs automatically via GitHub Actions on push to main and on pull requests.

## Architecture

```
User Programs (sh, apps)  — linked with newlib stubs
        | TRAP #0
Kernel (proc, fs, dev, mem, syscall)
        |
Platform Abstraction Layer (PAL)
        |
Hardware / Emulator
```

Two platform targets:
- **Workbench** (`pal/workbench/`) — emulated SBC via Musashi, for development
- **Mega Drive** (`pal/megadrive/`) — real hardware with VDP, Saturn keyboard, SRAM

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
├── tools/        # Host tools (mkfs, etc.)
└── tests/        # Host unit tests
```

## Design

- **No fork()** — vfork()+exec() only (no MMU)
- **No multi-user** — no UIDs, permissions, login
- **Custom filesystem (minifs)** — classic Unix inode layout
- **Syscalls via TRAP #0** — number in `d0`, args in `d1-d4`, return in `d0` (negative = -errno)

See [PLAN.md](PLAN.md) for the full design document.
