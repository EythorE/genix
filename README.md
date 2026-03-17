# Genix — A Small 68000 OS for Mega Drive

Genix is a minimal, single-user, POSIX-enough operating system for the Motorola 68000,
targeting the Sega Mega Drive. It replaces the FUZIX kernel with ~6500 lines of new code
while reusing proven Mega Drive drivers from
[EythorE/FUZIX](https://github.com/EythorE/FUZIX/tree/megadrive).

## Current Status

Genix boots and runs user programs on both the workbench emulator and real
Mega Drive hardware. See [docs/plans/decisions.md](docs/plans/decisions.md) for design
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
| Phase D | Interactive line editing — arrow keys, cursor movement, command history | **Complete** |
| Tier 1 Apps | Wave 1-2 utilities + find/xargs — 13 new programs ported | **Complete** |
| VDP Terminal | ANSI escape parser, bold palette, curses library (V1-V4) | **Complete** |
| Phase 9 | Performance — assembly memcpy/memset, DIVU.W fast path, pipe bulk copy | **3/5 done** |
| BlastEm Review | Visual validation of VDP terminal + performance optimizations | Next |
| Tier 2 Games | TUI games using curses (hamurabi, dopewars, tetris, snake, etc.) | Planned |

See [PLAN.md](PLAN.md) for the forward roadmap and
[docs/plans/apps_to_port.md](docs/plans/apps_to_port.md) for the app porting roadmap.

### What works today

All of this runs on a 7.67 MHz CPU with 64 KB of RAM and no MMU.

**A real POSIX shell.** Genix boots into [dash](https://en.wikipedia.org/wiki/Almquist_shell)
(the Debian Almquist Shell) with interactive line editing: arrow keys
move the cursor, up/down browse command history, Home/End jump to line
boundaries, and ^U kills the line. Tab through 16 remembered commands.
All the shell features you'd expect: pipes (`|`), I/O redirection
(`>`, `>>`, `<`), variable expansion (`$VAR`, `${VAR}`), command
substitution (`` `cmd` ``), control flow (`if`/`then`/`else`, `for`,
`while`, `case`), functions, traps, and full POSIX scripting.

**48 user programs in `/bin`.** `ls`, `cat`, `grep`, `sort`, `find`,
`xargs`, `cp`, `mv`, `rm`, `mkdir`, `more`, `wc`, `tr`, `cut`,
`uniq`, `comm`, `expr`, `env`, `seq`, `od`, `touch`, `kill`,
`which`, `uname` — plus `levee` (a vi clone) and `dash` itself. Every
binary runs from ROM via execute-in-place (XIP): only `.data` is
copied to RAM, `.text` executes directly from the cartridge. A typical
utility uses ~300 bytes of RAM.

**Preemptive multitasking.** 16 process slots with per-process kernel
stacks. Pipelines run concurrently — `find / -name '*.c' | grep main |
wc -l` spawns three processes that execute in parallel. Signals work:
Ctrl+C sends SIGINT, Ctrl+Z sends SIGTSTP, `kill` delivers any signal,
and user-installed signal handlers fire correctly.

**A proper TTY.** Cooked and raw modes, termios ioctls, ICRNL/ONLCR
translation, echo control, interrupt characters, and a 4-TTY
infrastructure. On the Mega Drive, input comes from an interrupt-driven
Saturn keyboard on controller port 2.

**A Unix filesystem.** minifs: inodes, directories, indirect blocks,
hard links. Create files, make directories, rename, unlink — it's all
there.

**A VDP color terminal.** On the Mega Drive, the VDP console parses
ANSI escape sequences: cursor positioning (`ESC[H`), screen/line
clearing (`ESC[2J`, `ESC[K`), bold text via a bright-white palette,
and cursor show/hide. A minimal curses library (~460 lines) enables
TUI apps like levee and future games.

**Tested.** 7,317+ host test assertions across 17 test files, 31
automated guest tests on the workbench emulator, 15 dash integration
tests, BlastEm Mega Drive autotest, and a 68020 opcode scan that
catches wrong-toolchain bugs at compile time. Every change runs the
full ladder before it ships.

## Getting Started

### 1. Install Prerequisites and Toolchain

```bash
# Host compiler + build tools (Ubuntu/Debian)
sudo apt-get install build-essential

# Fetch pre-built m68k-elf cross-compiler + BlastEm (recommended)
./scripts/fetch-toolchain.sh
export PATH=~/buildtools-m68k-elf/bin:~/blastem:$PATH
```

This downloads the correct `m68k-elf-gcc` (built with `--with-cpu=68000`)
and BlastEm 0.6.3-pre from retrodev.com nightlies. All Makefiles default
to `CROSS=m68k-elf-` — no extra environment variables needed.

**Fallback:** If `fetch-toolchain.sh` doesn't work for your platform,
build `m68k-elf-gcc` from source — see
[docs/toolchain.md](docs/toolchain.md). **Do NOT use the distro package
`gcc-m68k-linux-gnu`** — its `libgcc.a` contains 68020 instructions
that cause silent hangs on the 68000.

### 2. Build and Run

```bash
make run       # Build everything + boot in the workbench emulator
```

This builds the emulator, kernel, user programs, and a filesystem image,
then boots Genix in your terminal. You'll see the dash `#` prompt.

### 3. Try It

```
# echo hello world       # Run a command
# ls /bin                 # List available programs (47 including dash)
# echo hello | cat        # Pipes work
# cat /etc/motd > /tmp/x  # I/O redirection
# wc < /tmp/x             # Input redirection
# ← → Home End           # Arrow keys and cursor movement
# ↑ ↓                    # Browse command history
# find / -name cat        # Real utilities, real pipelines
# exit                    # Exit (dash respawns automatically)
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
├── apps/         # Userspace programs (48 including dash)
├── tools/        # Host tools (mkfs, mkbin)
├── tests/        # Host unit tests (17 test files, 5230+ assertions)
└── docs/         # Technical documentation, plans, and research
    ├── plans/      # Implementation plans (how things will be)
    └── research/   # Research and analysis (what informed decisions)
```

## Design

- **Mega Drive first** — real hardware is the target, emulators are for iteration
- **No fork()** — vfork()+exec() only (no MMU)
- **No multi-user** — no UIDs, permissions, login
- **Custom filesystem (minifs)** — classic Unix inode layout
- **Syscalls via TRAP #0** — number in `d0`, args in `d1-d4`, return in `d0` (negative = -errno)
- **Preemptive scheduling** — timer-driven context switch, 16 process slots
- **Custom libc** — 19 modules including regex, line editing, and POSIX stubs, tuned for 68000 constraints

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
| [Design Decisions](docs/plans/decisions.md) | Active design decisions guiding development |
| [Project History](HISTORY.md) | FUZIX heritage, implementation timeline, bugs, lessons |
| [Forward Plan](PLAN.md) | Roadmap: BlastEm visual review, Tier 2 games |
| [EverDrive Research](docs/research/everdrive-research.md) | SD card access, Pro hardware, SSF mode, bank switching |
| [Apps to Port](docs/plans/apps_to_port.md) | App porting roadmap, tier definitions, RAM analysis |
| [Relocatable Binaries](docs/research/relocatable-binaries.md) | Relocation research, XIP strategies, EverDrive bank-swapping |
| [Shell Research](docs/research/shell-research.md) | Shell candidates for Genix (RAM budget, features, porting effort) |
| [VDP Research](docs/research/vdp-research.md) | VDP terminal, graphics, and curses analysis |
| [Shell Plan](docs/plans/shell-plan.md) | Phased implementation plan for userspace shell + dash port |
| [Relocation Plan](docs/plans/relocation-plan.md) | Relocation phases 1-7, split XIP engine |
| [Optimization Plan](docs/plans/optimization-plan.md) | 68000 performance gaps vs FUZIX |
