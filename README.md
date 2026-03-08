# Genix — A Small 68000 OS for Mega Drive

Genix is a minimal, single-user, POSIX-enough operating system for the Motorola 68000,
targeting the Sega Mega Drive. It replaces the FUZIX kernel with ~3000 lines of new code
while reusing proven Mega Drive drivers from
[EythorE/FUZIX](https://github.com/EythorE/FUZIX/tree/megadrive).

## Current Status

Genix boots and runs user programs on both the workbench emulator and real
Mega Drive hardware. See [PLAN.md](PLAN.md) for the full roadmap and
[docs/decisions.md](docs/decisions.md) for design history.

| Phase | Description | Status |
|-------|-------------|--------|
| Phase 1 | Workbench emulator (Musashi SBC) | **Complete** |
| Phase 2a | Kernel core + binary loading + single-tasking exec | **Complete** |
| Phase 2b | Multitasking (vfork, scheduler, waitpid) | **Next** |
| Phase 3 | Mega Drive port (PAL drivers from Fuzix) | **Complete** |

**What works today:** filesystem (minifs) with read/write/create/delete/rename,
exec() loads and runs user programs from disk, built-in debug shell, Saturn
keyboard on Mega Drive, SRAM with standard Sega mapper, 34 host tests passing.

**What's next:** vfork() + waitpid() for a proper shell, then preemptive
scheduling, pipes, and signals.

## Getting Started

### 1. Install Prerequisites

You need a host C compiler and a 68000 cross-compiler.

```bash
# Host compiler + build tools (Ubuntu/Debian)
sudo apt-get install build-essential

# 68000 cross-compiler (quick start — works with Genix's workarounds)
sudo apt-get install gcc-m68k-linux-gnu binutils-m68k-linux-gnu
```

The distro cross-compiler defaults to 68020, but Genix works around this
by passing `-m68000` and providing its own division routines. For a fully
correct 68000 toolchain, build from source — see
[docs/toolchain.md](docs/toolchain.md) for details.

### 2. Build and Run

```bash
make run       # Build everything + boot in the workbench emulator
```

This builds the emulator, kernel, user programs, and a filesystem image,
then boots Genix in your terminal. You'll see a `>` prompt.

### 3. Try It

```
> ls /bin           # List available programs
> exec /bin/hello   # Run the hello world program
> exec /bin/echo hello world
> mem               # Show memory allocator state
> help              # List all built-in commands
> halt              # Shut down (exits the emulator)
```

Press **Ctrl+]** to force-quit the emulator at any time (like telnet).

See [docs/emulator.md](docs/emulator.md) for full emulator documentation.

### 4. Build the Mega Drive ROM

```bash
make megadrive     # Produces pal/megadrive/genix-md.bin
```

Test in [BlastEm](https://www.retrodev.com/blastem/):
```bash
blastem pal/megadrive/genix-md.bin
```

On the Mega Drive, Genix uses the VDP for text output and reads input
from a Saturn keyboard connected to controller port 2.

**BlastEm keyboard setup (one-time):** Press Escape → Settings → System →
set IO Port 2 to "Saturn Keyboard". Press **Right Ctrl** to toggle keyboard
capture in the emulator. See [docs/megadrive.md](docs/megadrive.md) for
full details including real hardware setup.

### 5. Run Tests

```bash
make test          # Host unit tests (no cross-compiler needed)
make test-md       # Headless BlastEm boot (~5s smoke test, needs blastem)
```

## All Build Targets

```bash
make emu           # Build workbench emulator only (host binary)
make kernel        # Build kernel only (needs cross-compiler)
make apps          # Build user programs (needs kernel built first)
make disk          # Create filesystem image
make run           # Build all + run in emulator
make megadrive     # Build Mega Drive ROM
make test          # Host unit tests
make test-md       # Headless BlastEm smoke test
make clean         # Remove all build artifacts
```

## Architecture

```
User Programs (sh, apps)  — linked with libc syscall stubs
        │ TRAP #0
Kernel (proc, fs, dev, mem, exec, syscall)
        │
Platform Abstraction Layer (PAL)
        │
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
├── libc/         # Minimal C library + syscall stubs
├── apps/         # Userspace programs (hello, echo, cat)
├── tools/        # Host tools (mkfs, mkbin)
├── tests/        # Host unit tests
└── docs/         # Technical documentation
```

## Design

- **Mega Drive first** — real hardware is the target, emulators are for iteration
- **No fork()** — vfork()+exec() only (no MMU)
- **No multi-user** — no UIDs, permissions, login
- **Custom filesystem (minifs)** — classic Unix inode layout
- **Syscalls via TRAP #0** — number in `d0`, args in `d1-d4`, return in `d0` (negative = -errno)

## Documentation

See [docs/](docs/) for detailed technical documentation:

| Document | Description |
|----------|-------------|
| [Architecture](docs/architecture.md) | System architecture, memory maps, layers |
| [Kernel](docs/kernel.md) | Kernel subsystems: memory, buffer cache, devices, processes |
| [Syscalls](docs/syscalls.md) | Syscall convention, dispatch, libc stubs |
| [Filesystem](docs/filesystem.md) | minifs on-disk layout, inode structure |
| [Binary Format](docs/binary-format.md) | Genix flat binary header and loader |
| [Emulator](docs/emulator.md) | Workbench emulator usage and keyboard |
| [Mega Drive](docs/megadrive.md) | Mega Drive target: cartridges, SRAM, real hardware |
| [Toolchain](docs/toolchain.md) | Cross-compiler setup (apt + build from source) |
| [Multitasking](docs/multitasking.md) | Process model, vfork/exec, scheduling |
| [TTY & Console](docs/tty.md) | VDP text output, keyboard input |
| [68000 Programming](docs/68000-programming.md) | ISA constraints, division, ABI |
| [Design Decisions](docs/decisions.md) | Project history, choices, reversals |
| [Fuzix Heritage](docs/fuzix-heritage.md) | What we took from Fuzix, what's different |
