# Genix — A Small 68000 OS for Mega Drive

Genix is a minimal, single-user, POSIX-enough operating system for the Motorola 68000,
targeting the Sega Mega Drive. ~5,650 lines of kernel code, readable in an afternoon.

Built from scratch after hitting an unfindable fork() bug in
[FUZIX](https://github.com/EythorE/FUZIX/tree/megadrive). Reuses proven
Mega Drive drivers (VDP, Saturn keyboard, SRAM) from that port.

## What Works

Genix boots on both the workbench emulator and real Mega Drive hardware:

- **34 user programs** — grep, levee (vi clone), od, expr, cat, wc, ls, and standard Unix utilities
- **Shell** with pipes (`|`), I/O redirection (`>`, `>>`, `<`), PATH search, cd
- **Preemptive multitasking** — 16 process slots, timer-driven context switch
- **Signals** — user handlers, SIGTSTP/SIGCONT, Ctrl+C/Ctrl+Z, SIGPIPE
- **TTY subsystem** — line discipline, cooked/raw modes, termios ioctls
- **Relocatable binaries** — linked at address 0, relocated at exec() time, one binary runs on both platforms
- **minifs** — classic Unix inode filesystem with indirect blocks
- **Custom libc** — 16 modules including stdio, regex, termios (~5 KB)
- **Interrupt-driven Saturn keyboard**, SRAM with boot-time validation, multi-TTY (4 TTYs)
- **5,100+ host test assertions** across 13 test files, 31+ automated guest tests on both platforms

## Quick Start

```bash
# Install host build tools
sudo apt-get install build-essential

# Fetch cross-compiler + BlastEm
./scripts/fetch-toolchain.sh
export PATH=~/buildtools-m68k-elf/bin:~/blastem:$PATH
export CROSS=m68k-elf-

# Build and run
make run           # Workbench emulator — you'll see a > prompt
make megadrive     # Build Mega Drive ROM (pal/megadrive/genix-md.bin)
make test-all      # Full testing ladder
```

Try it in the emulator:
```
> ls /bin
> echo hello | cat
> exec /bin/grep -i hello /bin/hello
> help
> halt
```

Press **Ctrl+]** to force-quit. See [docs/emulator.md](docs/emulator.md) for details.

**Mega Drive:** Test in BlastEm (`blastem pal/megadrive/genix-md.bin`).
On real hardware, connect a Saturn keyboard to controller port 2.
See [docs/megadrive.md](docs/megadrive.md).

**Fallback toolchain:** `sudo apt-get install gcc-m68k-linux-gnu binutils-m68k-linux-gnu`
with `CROSS=m68k-linux-gnu-`. See [docs/toolchain.md](docs/toolchain.md).

## Architecture

```
User Programs (34 apps)  — linked with libc, syscall stubs
        │ TRAP #0
Kernel (~5,650 lines)    — proc, fs, dev, mem, exec
        │
Platform Abstraction Layer (PAL)
   ├── Workbench (Musashi 68000 SBC, 1 MB RAM, for development)
   └── Mega Drive (VDP, Saturn keyboard, 64 KB RAM, optional SRAM)
```

**Design:** No fork() (vfork+exec only, no MMU), no multi-user, custom
minifs filesystem, relocatable flat binaries, SRAM optional. See
[docs/decisions.md](docs/decisions.md).

## Build Targets

```bash
make run             # Build all + boot workbench emulator
make megadrive       # Build Mega Drive ROM
make test            # Host unit tests (no cross-compiler needed)
make test-emu        # Workbench autotest (STRICT_ALIGN + AUTOTEST)
make test-md-auto    # BlastEm AUTOTEST ROM (primary quality gate)
make test-all        # Full ladder: test → kernel → test-emu → megadrive → test-md → test-md-auto
make clean           # Remove all build artifacts
```

## Roadmap

Current limitation: all user programs load at a single address, so
pipelines execute sequentially. See [PLAN.md](PLAN.md) for details.

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
| Phase 5 | **ROM Execute-in-Place** — run text from ROM, only copy .data to RAM. Triples usable memory. | Planned |
| Phase 6 | **Concurrent multitasking** — fixed RAM slots for .data, shared ROM text, true concurrent pipes | Planned |
| Phase 7 | **SD card** — load programs at runtime (Open EverDrive SPI + Mega EverDrive Pro FIFO) | Planned |
| Phase 8 | **EverDrive Pro PSRAM** — banked 512 KB per process, enables large programs like levee on MD | Planned |
| Phase 9 | **Performance** — assembly memcpy/memset, DIVU.W fast path, VDP DMA scroll | Anytime |

## Documentation

| Document | Description |
|----------|-------------|
| [PLAN.md](PLAN.md) | Forward plan with implementation details |
| [HISTORY.md](HISTORY.md) | Project history, FUZIX heritage, bugs, lessons |
| [docs/decisions.md](docs/decisions.md) | Active design decisions |
| [docs/architecture.md](docs/architecture.md) | Memory maps, layers, binary format |
| [docs/kernel.md](docs/kernel.md) | Kernel subsystems |
| [docs/megadrive.md](docs/megadrive.md) | Mega Drive target, cartridges, SRAM |
| [docs/emulator.md](docs/emulator.md) | Workbench emulator usage |
| [docs/toolchain.md](docs/toolchain.md) | Cross-compiler setup |
| [docs/automated-testing.md](docs/automated-testing.md) | Testing ladder, AUTOTEST |
| [docs/68000-programming.md](docs/68000-programming.md) | ISA constraints, ABI |
| [OPTIMIZATION_PLAN.md](OPTIMIZATION_PLAN.md) | Performance gaps vs FUZIX |
