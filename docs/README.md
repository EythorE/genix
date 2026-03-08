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
| [Fuzix Heritage](fuzix-heritage.md) | What we took from Fuzix, what's different |
| [Design Decisions](decisions.md) | Project history, design choices, reversals, pain points |

## Quick Reference

```bash
make emu        # Build workbench emulator (host)
make kernel     # Build kernel (needs m68k cross-compiler)
make apps       # Build user programs
make run        # Full system: build + run in emulator
make megadrive  # Build Mega Drive ROM
make test       # Host unit tests (no cross-compiler needed)
make test-md    # Headless BlastEm smoke test
```
