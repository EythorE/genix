# Genix — A Small 68000 OS for Mega Drive

Genix is a minimal, single-user, POSIX-enough operating system for the Motorola 68000,
targeting the Sega Mega Drive. It replaces the FUZIX kernel with ~5400 lines of new code
while reusing proven Mega Drive drivers from
[EythorE/FUZIX](https://github.com/EythorE/FUZIX/tree/megadrive).

## Current Status

Genix boots and runs user programs on both the workbench emulator and real
Mega Drive hardware. See [docs/decisions.md](docs/decisions.md) for design
history and [HISTORY.md](HISTORY.md) for the full project timeline.

| Phase | Description | Status |
|-------|-------------|--------|
| Phase 1 | Workbench emulator (Musashi SBC) | **Complete** |
| Phase 2a | Kernel core + binary loading + single-tasking exec | **Complete** |
| Phase 2b | Multitasking (spawn, waitpid, preemptive scheduler) | **Complete** |
| Phase 2c | Pipes and I/O redirection | **Complete** |
| Phase 2d | Signals and job control | **Complete** |
| Phase 2e | TTY subsystem (line discipline, termios) | **Complete** |
| Phase 2f | Fuzix libc + utilities | **Complete** |
| Phase 3 | Mega Drive port (PAL drivers from Fuzix) | **Complete** |
| Phase 4 | Polish (interrupt keyboard, multi-TTY, /dev/null) | **Complete** |
| Phase 5 | ROM Execute-in-Place — run text from ROM, only copy .data to RAM | **Complete** |
| Phase 6 | Concurrent multitasking — `-msep-data`, fixed RAM slots, shared ROM text | **Complete** |
| Phase A | Libc prerequisites — POSIX headers, setjmp/longjmp, signal wrappers, stat conversion | **Complete** |
| Phase B | Kernel enhancements — fcntl F_DUPFD, waitpid WNOHANG | **Complete** |
| Phase C | Port dash shell — POSIX scripting, variable expansion, command substitution | **Complete** |
| Phase 7 | SD card — load programs at runtime (Open EverDrive SPI + Mega EverDrive Pro FIFO) | Planned |
| Phase 8 | EverDrive Pro PSRAM — banked 512 KB per process, enables large programs on MD | Planned |
| Phase 9 | Performance — assembly memcpy/memset, DIVU.W fast path, VDP DMA scroll | Anytime |

See [PLAN.md](PLAN.md) for detailed implementation plans.

**What works today:** kernel boots on both workbench and Mega Drive, minifs
filesystem with indirect blocks, exec() loads user programs (35 apps in
/bin including dash shell, grep, levee, od, expr, and standard Unix
utilities), dash POSIX shell with pipes (`|`), I/O redirection (`>`,
`>>`, `<`), variable expansion, command substitution, scripting
(if/then/else, for, case, functions), process table (16 slots) with
preemptive
scheduling, per-process kernel stacks, blocking pipes (512-byte circular
buffer), user signal handlers (SIGTSTP/SIGCONT, Ctrl+C/Ctrl+Z), TTY line
discipline with cooked/raw modes, termios ioctls, interrupt-driven Saturn
keyboard on Mega Drive, SRAM with boot-time validation, configurable
buffer cache, multi-TTY infrastructure (4 TTYs), 4924+ host test
assertions across 13 test files, 31+ automated guest tests on both
platforms, CI pipeline enforcing full testing ladder.

## Getting Started

### 1. Install Prerequisites and Toolchain

```bash
# Host compiler + build tools (Ubuntu/Debian)
sudo apt-get install build-essential

# Fetch pre-built m68k-elf cross-compiler + BlastEm (recommended)
./scripts/fetch-toolchain.sh
export PATH=~/buildtools-m68k-elf/bin:~/blastem:$PATH
export CROSS=m68k-elf-
```

This downloads the correct `m68k-elf-gcc` (built with `--with-cpu=68000`)
and BlastEm 0.6.3-pre from retrodev.com nightlies.

**Fallback:** If the pre-built toolchain doesn't work for your platform,
install the distro compiler (`sudo apt-get install gcc-m68k-linux-gnu
binutils-m68k-linux-gnu`). It defaults to 68020 but Genix works around
this with `-m68000` and its own `divmod.S`. See
[docs/toolchain.md](docs/toolchain.md) for details and building from source.

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
> echo hello | cat  # Pipes work
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
make test-emu      # Workbench autotest (exec, syscalls, STRICT_ALIGN)
make test-md       # Headless BlastEm -b 300 (~5s, no Xvfb needed)
make test-md-auto  # BlastEm -b 600 AUTOTEST ROM (PRIMARY QUALITY GATE)
make test-all      # Full testing ladder (all of the above in order)
```

## All Build Targets

```bash
# Build
make emu             # Build workbench emulator only (host binary)
make kernel          # Build kernel only (needs cross-compiler)
make tools           # Build host tools (mkfs, mkbin)
make libc            # Build C library for user programs
make apps            # Build user programs (workbench, linked at 0x040000)
make apps-md         # Build user programs (Mega Drive, linked at 0xFF8000)
make disk            # Create filesystem image (workbench)
make disk-md         # Create filesystem image (Mega Drive)
make run             # Build all + run in emulator
make megadrive       # Build Mega Drive ROM

# Testing ladder
make test            # 1. Host unit tests
make test-emu        # 2. Workbench autotest (STRICT_ALIGN + AUTOTEST)
make test-md         # 3. Headless BlastEm -b 300 (~5s, no Xvfb)
make test-md-auto    # 4. BlastEm -b 600 AUTOTEST (PRIMARY QUALITY GATE)
make test-all        # Full ladder: test → kernel → test-emu → megadrive → test-md → test-md-auto
make clean           # Remove all build artifacts
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
├── libc/         # Minimal C library + syscall stubs (for user programs)
├── apps/         # Userspace programs (35 including dash)
├── tools/        # Host tools (mkfs, mkbin)
├── tests/        # Host unit tests (13 test files, 4924+ assertions)
└── docs/         # Technical documentation
```

## Design

- **Mega Drive first** — real hardware is the target, emulators are for iteration
- **No fork()** — vfork()+exec() only (no MMU)
- **No multi-user** — no UIDs, permissions, login
- **Custom filesystem (minifs)** — classic Unix inode layout
- **Syscalls via TRAP #0** — number in `d0`, args in `d1-d4`, return in `d0` (negative = -errno)
- **Preemptive scheduling** — timer-driven context switch, 16 process slots
- **Custom libc** — 18 modules including regex and POSIX stubs, tuned for 68000 constraints

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
| [TTY & Console](docs/tty.md) | VDP console and TTY subsystem |
| [Automated Testing](docs/automated-testing.md) | Testing ladder, AUTOTEST, discrepancy procedures |
| [68000 Programming](docs/68000-programming.md) | ISA constraints, division, ABI |
| [Design Decisions](docs/decisions.md) | Active design decisions guiding development |
| [Project History](HISTORY.md) | FUZIX heritage, implementation timeline, bugs, lessons |
| [Forward Plan](PLAN.md) | Roadmap with implementation details for phases 5-9 |
| [Relocatable Binaries](docs/relocatable-binaries.md) | Relocation research, XIP strategies, EverDrive bank-swapping |
| [Relocation Implementation](docs/relocation-implementation-plan.md) | Relocation phases 1-7, split XIP engine |
| [Shell Research](docs/shell-research.md) | Shell candidates for Genix (RAM budget, features, porting effort) |
| [Shell Plan](docs/shell-plan.md) | Phased implementation plan for userspace shell + dash port |
| [EverDrive SD Card](docs/everdrive-sd-card.md) | SD card access on Open EverDrive and Pro cartridges |
| [Optimization Plan](OPTIMIZATION_PLAN.md) | 68000 performance gaps vs FUZIX |
