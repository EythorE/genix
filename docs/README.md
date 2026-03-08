# Genix Documentation

Technical documentation for Genix, a minimal single-user POSIX-enough OS
for the Motorola 68000, targeting the Sega Mega Drive.

## Contents

| Document | Description |
|----------|-------------|
| [Architecture](architecture.md) | System architecture, memory maps, layers |
| [Kernel](kernel.md) | Kernel subsystems: memory, buffer cache, devices, processes |
| [Syscalls](syscalls.md) | Syscall convention, dispatch, libc stubs |
| [Filesystem](filesystem.md) | minifs on-disk layout, inode structure, operations |
| [Binary Format](binary-format.md) | Genix flat binary header, loader, mkbin tool |
| [TTY & Console](tty.md) | VDP text output, keyboard input, line discipline |
| [Multitasking](multitasking.md) | Process model, vfork/exec, scheduling, signals, pipes |
| [Toolchain](toolchain.md) | Which compiler, why, build flags, libgcc |
| [68000 Programming](68000-programming.md) | ISA constraints, division, ABI, alignment |
| [Fuzix Heritage](fuzix-heritage.md) | What we took from Fuzix, what's different, what's missing |

## Quick Reference

```bash
make emu        # Build workbench emulator (host)
make kernel     # Build kernel (needs m68k cross-compiler)
make apps       # Build user programs
make run        # Full system: build + run in emulator
make megadrive  # Build Mega Drive ROM
make test       # Host unit tests (no cross-compiler needed)
```
