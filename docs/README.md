# Genix Documentation

Technical documentation for Genix, a minimal single-user POSIX-enough OS
for the Motorola 68000, targeting the Sega Mega Drive.

## Getting Started

See the [main CLAUDE.md](../CLAUDE.md) for setup instructions and quick start.

## Contents

### Using Genix

| Document | Description |
|----------|-------------|
| [Emulator](emulator.md) | Workbench emulator: how it works, keyboard, shell commands |
| [Toolchain](toolchain.md) | Cross-compiler setup: apt quick-start + build from source |
| [Mega Drive](megadrive.md) | Mega Drive target: memory map, cartridges, SRAM, real hardware |

### System Design

| Document | Description |
|----------|-------------|
| [Architecture](architecture.md) | System architecture, memory maps, layers |
| [Kernel](kernel.md) | Kernel subsystems: memory, buffer cache, devices, processes |
| [Syscalls](syscalls.md) | Syscall convention, dispatch, libc stubs |
| [Filesystem](filesystem.md) | minifs on-disk layout, inode structure, operations |
| [Binary Format](binary-format.md) | Genix flat binary header, loader, mkbin tool |
| [TTY & Console](tty.md) | VDP text output, keyboard input, line discipline |
| [Multitasking](multitasking.md) | Process model, vfork/exec, scheduling, signals, pipes |

### Reference

| Document | Description |
|----------|-------------|
| [68000 Programming](68000-programming.md) | ISA constraints, division, ABI, alignment |
| [Project History](../HISTORY.md) | FUZIX heritage, implementation timeline, bugs, lessons |
| [Design Decisions](decisions.md) | Active design decisions guiding development |

## Quick Reference

```bash
# Build
make emu             # Build workbench emulator (host binary)
make kernel          # Build kernel (needs m68k cross-compiler)
make tools           # Build host tools (mkfs, mkbin)
make libc            # Build C library for user programs
make apps            # Build user programs (workbench)
make apps-md         # Build user programs (Mega Drive)
make disk            # Create filesystem image (workbench)
make disk-md         # Create filesystem image (Mega Drive)
make run             # Build all + run in emulator
make megadrive       # Build Mega Drive ROM

# Testing ladder (run in order, all must pass)
make test            # 1. Host unit tests (no cross-compiler needed)
make kernel          # 2. Cross-compilation check
make test-emu        # 3. Workbench autotest (STRICT_ALIGN + AUTOTEST)
make megadrive       # 4. Mega Drive build
make test-md         # 5. Headless BlastEm boot (~5s smoke test)
make test-md-auto    # 6. BlastEm autotest (PRIMARY QUALITY GATE)
make test-all        # Run the full ladder at once

# Extras
make test-md-screenshot  # Visual VDP test (saves test-md-screenshot.png)
make clean               # Remove all build artifacts
```
