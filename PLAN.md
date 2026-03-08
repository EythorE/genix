# Genix Development Plan

See EythorE/FUZIX branch claude/create-dev-branch-Jpv7Z for the full design document.

## Phases

1. **Workbench Emulator** — Musashi 68000 + UART + timer + block device
2. **Single-tasking Kernel** — boot, console, minifs, exec, shell
3. **Multi-tasking** — scheduler, vfork+exec, process table
4. **Mega Drive Port** — reuse FUZIX VDP/keyboard/disk drivers
5. **GNU Toolchain** — shell, coreutils, make, binutils, gcc
