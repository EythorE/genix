# Test Coverage

Tracks what is tested, what isn't, and what should be.

For the testing ladder and procedures, see
[docs/automated-testing.md](automated-testing.md).

---

## Test Ladder Summary

| Level | Target | Command | What it catches |
|-------|--------|---------|----------------|
| 1 | Host unit tests | `make test` | Logic bugs in kernel subsystems |
| 2 | Cross-compilation | `make kernel` | ABI mismatches, declarations |
| 3 | 68020 opcode scan | `make test-opcodes` | Wrong toolchain (distro libgcc) |
| 4 | Workbench autotest | `make test-emu` | Runtime bugs, STRICT_ALIGN |
| 5 | Dash boot test | `make test-dash` | Shell startup, commands, pipes |
| 6 | Mega Drive build | `make megadrive` | Linker scripts, memory layout |
| 7 | BlastEm headless | `make test-md` | Boot crash on real hardware model |
| 8 | BlastEm autotest | `make test-md-auto` | Full quality gate |

`make test-all` runs levels 1-8 in order.

---

## Host Unit Tests (Level 1)

| File | Subsystem | Assertions | Coverage notes |
|------|-----------|-----------|----------------|
| `test_string.c` | kernel/string.c | 60 | memset, memcpy, strcmp, strchr, etc. |
| `test_mem.c` | kernel/mem.c | 185 | kmalloc/kfree + **slot allocator** (alloc, free, exhaustion, invalid index, sizing) |
| `test_exec.c` | kernel/exec.c | 33 | Header validation, stack setup |
| `test_proc.c` | kernel/proc.c | 2001 | Pipes, kstack, PID alloc, zombies, waitpid, fcntl |
| `test_libc.c` | libc/*.c | 71 | strtol, getopt, strerror, vsnprintf, sscanf, qsort |
| `test_vdp.c` | VDP driver | 63 | VRAM/CRAM addresses, sprite layout, tab stops |
| `test_signal.c` | Signals | 94 | Handlers, delivery, SIGTSTP/SIGCONT, pgrp |
| `test_redir.c` | Shell redir | 61 | Parse, redirect, pipes, SIGPIPE |
| `test_tty.c` | TTY subsystem | 78 | Canon/raw mode, termios, ICRNL, signal chars |
| `test_fs.c` | Filesystem | 118 | Inodes, bmap, read/write, namei, mkdir, indirect blocks |
| `test_buf.c` | Buffer cache | 36 | bread/bwrite/brelse, eviction, dirty writeback |
| `test_kprintf.c` | kprintf | 24 | Format strings, hex, char, percent |
| `test_pipe.c` | Pipes | 2170 | Fill, wrap, overflow, EOF, stress |
| `test_reloc.c` | Relocations | 64 | Simple/split reloc, XIP, **duplicate offset (known bug)** |
| `test_dash.c` | Dash semantics | 44 | errno conversion, waitpid, execve, PATH search |
| `test_abi.c` | ABI compat | 28 | **struct stat layout match** (kernel vs libc), field alignment |
| `test_lineedit.c` | libc/lineedit.c | 102 | Edit ops (insert/delete/move/kill), history ring, key parsing (ANSI, MD, ctrl) |

**Total: ~5,230 assertions across 17 test files**

---

## Integration Tests (Levels 3, 5)

| Test | Command | What it tests |
|------|---------|---------------|
| `check-opcodes.sh` | `make test-opcodes` | Scan .elf files for 68020 MULU.L/DIVU.L/EXTB.L/RTD |
| `test-dash.sh` | `make test-dash` | 15 tests: boot, echo, exit status, ls, pipes, redirection, variables, non-interactive bypass, error absence |
| `test-levee.sh` | `make test-levee` | Smoke test: levee starts, displays editor, accepts :q! |

---

## Covered — Tests Exist

These documented issues have test coverage:

| Issue | Test | Source |
|-------|------|--------|
| 68020 libgcc opcodes | `check-opcodes.sh` | CLAUDE.md pitfall |
| errno not set by syscall stubs | `test_dash.c` errno tests | HISTORY.md bug |
| vfork TRAP frame corruption | `test_dash.c` vfork semantics | HISTORY.md bug |
| async exec for vfork children | `test_dash.c` + autotest spawn | HISTORY.md bug |
| kstack canary overflow detection | autotest test 23 | HISTORY.md bug 8 |
| Indirect block bmap | `test_fs.c` bmap_indirect | HISTORY.md bug 6 |
| Inode block deallocation | `test_fs.c` iput_frees_blocks | HISTORY.md bug 10 |
| struct stat layout (kernel = libc) | `test_abi.c` | shell-plan.md weak spot A-2 |
| Slot allocator (alloc/free/invalid) | `test_mem.c` slot tests | PLAN.md weak spot 1 |
| Duplicate reloc offset | `test_reloc.c` (documents bug) | PLAN.md weak spot 2 |
| Dash boot + commands | `test-dash.sh` | Post-Phase C bugs |
| Redirection no-space parsing | `test_redir.c` | HISTORY.md bug 16 |
| STRICT_ALIGN enforcement | `make test-emu` | HISTORY.md bug 2 |

---

## Not Covered — Tests Needed (TODO)

### High priority

| Issue | Where documented | Difficulty | Notes |
|-------|-----------------|------------|-------|
| Full TRAP #0 syscall path | automated-testing.md | Medium | Autotests call kernel directly in supervisor mode. Need a userspace `apps/test_syscalls.c` that exercises the real TRAP → libc stub → kernel path. |
| `sigaction()` read-restore correctness | shell-plan.md weak spot A-1 | Easy | Verify `sigaction(sig, NULL, &oact)` reads old handler without corrupting it. |
| FD_CLOEXEC silently ignored | proc.c:1374 | Easy | Dash sets it but kernel doesn't honor it. Test should document this gap. |
| Levee crash root cause | test-levee.sh | **Fixed** | Was missing `-msep-data` in levee Makefile. a5 (GOT pointer) was clobbered by compiler using it as scratch register. |

### Medium priority

| Issue | Where documented | Difficulty | Notes |
|-------|-----------------|------------|-------|
| GOT offset near 64 KB boundary | PLAN.md weak spot 4 | Easy | Synthetic header with got_offset=0xFFFE. Low risk (no binary is close). |
| `do_exec` slot leak on file-not-found | PLAN.md weak spot 5 | Medium | Call `do_exec` on nonexistent path, verify `slot_alloc()` still succeeds after. |
| F_GETFL internal flag leakage | shell-plan.md weak spot B-1 | Easy | Set internal flag below 0x0FFF, verify it leaks through F_GETFL. |
| Environment variables not inherited | decisions.md limitation 7 | Easy | Confirm `export VAR; child` doesn't see VAR (documents limitation). |

### Not feasible as unit tests

| Issue | Why | Mitigation |
|-------|-----|------------|
| Saturn keyboard double-read race | Hardware-only (no emulator model) | Manual testing on real hardware |
| Tab rendering in VDP | Visual-only (UART doesn't show it) | `make test-md-screenshot` |
| `exec_user_a5` global race | Structural property (non-preemptive kernel) | Design review if exec becomes reentrant |
| Emulator vs Mega Drive memory constraints | Emulator has 1 MB, MD has 64 KB | `make test-md-auto` catches most issues |

---

## Known Broken Programs

(None — all programs are working.)

---

## Test Infrastructure Gaps

| Gap | Notes |
|-----|-------|
| No userspace TRAP #0 test binary | Autotests bypass the real syscall path (supervisor mode). Need `apps/test_syscalls.c`. |
| No Mega Drive-specific autotest for dash | `test-md-auto` runs the builtin autotest, not dash. Dash on MD requires XIP + correct slot sizing. |
| No emulator illegal-instruction detection | Musashi silently treats 68020 opcodes as NOPs. `check-opcodes.sh` catches at compile time instead. |
| No CI integration | Tests run manually. A GitHub Actions workflow would catch regressions automatically. |

---

## Phase 7 / Phase 8 Testing Plan (SD Card + PSRAM)

### What BlastEm can test (no hardware needed)

| Feature | How to test | Notes |
|---------|------------|-------|
| SSF2 bank switching (ROM page select) | Autotest: write bank regs at 0xA130F0-FE, verify reads from banked regions | BlastEm supports SSF2 mapper when ROM header contains `"SEGA SSF"` at offset 0x100. Added in BlastEm 0.5.0. |
| SSF bank allocator logic | Host unit test | Allocate/free/track PSRAM banks — pure logic, same pattern as slot allocator |
| Context switch bank register write | Autotest: spawn two processes, verify each gets its bank | The asm that writes 0xA130Fx on context switch is testable if BlastEm's mapper responds |
| Standard SRAM at 0x200000 | Already works | ROM header declares SRAM, BlastEm auto-maps. Current Genix build uses this. |
| Pro detection (safe probe) | Autotest: read 0xA130D4 | Returns open bus in BlastEm → `(val & 0xFFF0) == 0x55A0` correctly returns false. Code can probe without crashing. |
| Block device / VFS layer | Host unit test | Test the filesystem abstraction layer with a mock block device (no real SD card needed) |
| FAT16 read-only parser | Host unit test | Feed a synthetic FAT16 image to the parser, verify file listing and reads |

### What requires real EverDrive Pro hardware

| Feature | Why | Test approach |
|---------|-----|---------------|
| FIFO command protocol (0xA130D0) | Not emulated in any emulator | Manual test on Pro hardware |
| SD card read/write (both SPI and FIFO) | No emulator supports either interface | Real hardware only |
| PSRAM writes via SSF extended W bit | BlastEm's SSF mapper is read-only ROM banking | Real Pro hardware |
| Bank 31 = BRAM (persistent battery-backed) | Pro-specific bank routing | Real Pro hardware |

### What requires real Open EverDrive hardware

| Feature | Why | Test approach |
|---------|-----|---------------|
| SPI bit-bang via 0xA130E0 | Not emulated | Real Open EverDrive only |
| SD card init (CMD0/CMD8/ACMD41) | SPI protocol | Real hardware only |

### BlastEm SSF mapper limitations

BlastEm emulates **standard SSF2** bank switching, NOT the EverDrive Pro
extended SSF mapper. Key differences:

| Feature | BlastEm | EverDrive Pro |
|---------|---------|---------------|
| Bank page select (0xA130F2-FE) | Yes | Yes |
| SRAM enable/disable (0xA130F0 bit 0) | Yes | Yes |
| CTRL0 W bit (global write enable for PSRAM) | **No** | Yes |
| CTRL0 P bit (protection) | **No** | Yes |
| Bank 31 → BRAM routing | **No** | Yes |
| Writable mapped memory | **No** (ROM only) | Yes (PSRAM) |

**Implication for Phase 8:** The PSRAM bank allocator and context-switch
bank register writes can be tested in BlastEm (the register writes work,
just the memory isn't writable). The actual PSRAM read/write and BRAM
persistence require real Pro hardware.

### Future prospect: EverDrive Pro USB serial testing

The EverDrive Pro has a USB port. If the Pro's USB exposes a serial
interface accessible from the 68000 side (via FIFO or a dedicated
register), it could enable an automated real-hardware test loop:

1. Flash test ROM via USB from host
2. Boot Genix on real hardware
3. Kernel outputs test results via USB serial
4. Host script captures output and checks results

**Status:** Not researched. The USB port is known to be used for ROM
flashing by the krikzz firmware tool, but whether it can act as a
bidirectional serial bridge to the 68000 is unknown. This would be
the most impactful test infrastructure investment for Phase 7/8 work
since it removes the "works in emulator, breaks on hardware" gap.

BlastEm also has undocumented serial IO via Unix domain sockets
(controller port configured as `"serial"`, data via Genesis serial
registers at 0xA1000F/0xA10013, max 4800 bps). This could serve as
a bridge until real hardware serial is available.

### Recommended testing order for Phase 7

1. **Host unit tests first:** FAT16 parser, block device abstraction,
   bank allocator logic — all testable without any emulator.
2. **BlastEm autotest:** SSF bank switching, Pro detection guard,
   verify kernel doesn't crash when Pro is absent.
3. **Real hardware (manual):** SD card init, file read/write, FIFO
   protocol — test on actual EverDrive Pro.
4. **Real hardware (automated, future):** USB serial test loop if
   the Pro's USB supports bidirectional communication.
