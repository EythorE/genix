# Genix Documentation Index

Technical documentation for Genix, a minimal single-user POSIX-enough OS
for the Motorola 68000, targeting the Sega Mega Drive.

Documentation is organized into three directories under `docs/`:
- **docs/** — How things are (current-truth technical documentation)
- **docs/plans/** — How things will be (implementation plans with outcomes)
- **docs/research/** — What informed decisions (analysis and research)

## Getting Started

See the [main README](../README.md) for setup and quick start.

---

## Technical Documentation

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
| [Memory System](memory-system.md) | Kernel heap + user memory allocator |
| [TTY & Console](tty.md) | VDP text output, keyboard input, line discipline |
| [Multitasking](multitasking.md) | Process model, vfork/exec, scheduling, signals, pipes |

### Reference

| Document | Description |
|----------|-------------|
| [68000 Programming](68000-programming.md) | ISA constraints, division, ABI, alignment |
| [Automated Testing](automated-testing.md) | Testing ladder, AUTOTEST, discrepancy procedures |
| [Test Coverage](test-coverage.md) | What is tested, what isn't, and TODOs |

---

## plans/ — Implementation Plans

| Document | Description |
|----------|-------------|
| [Design Decisions](plans/decisions.md) | Active design decisions guiding development |
| [Apps to Port](plans/apps_to_port.md) | App porting roadmap, tier definitions, RAM analysis |
| [Shell Plan](plans/shell-plan.md) | Phased plan: libc prereqs → kernel → dash port → line editing |
| [Relocation Plan](plans/relocation-plan.md) | Relocation phases 1-7, split XIP engine |
| [Optimization Plan](plans/optimization-plan.md) | 68000 performance gaps vs FUZIX with source refs |

---

## research/ — Research & Analysis

| Document | Description |
|----------|-------------|
| [Relocatable Binaries](research/relocatable-binaries.md) | Relocation research, XIP strategies, EverDrive bank-swapping (1128 lines, canonical) |
| [Shell Research](research/shell-research.md) | Shell candidates for Genix (RAM budget, features, porting effort) |
| [VDP Research](research/vdp-research.md) | VDP terminal, graphics, curses analysis (consolidated) |
| [EverDrive Research](research/everdrive-research.md) | SD card access, Pro hardware, SSF mode, bank switching |
| [Memory Allocator Research](research/memory-allocator-research.md) | Variable-size allocator design research |
| [Status Report 2026-03-15](research/status-report-2026-03-15.md) | Project status snapshot |

---

## Top-Level Documents

| Document | Description |
|----------|-------------|
| [PLAN.md](../PLAN.md) | Forward roadmap (Phase 9, VDP color terminal) |
| [HISTORY.md](../HISTORY.md) | Full project timeline, bugs, lessons learned |
| [CLAUDE.md](../CLAUDE.md) | Development guidelines and project instructions |

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
