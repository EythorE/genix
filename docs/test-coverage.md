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
| `test_mem.c` | kernel/mem.c | 247 | kmalloc/kfree + **umem allocator** (alloc, free, coalesce, fragmentation, variable sizes, gap reuse, pipeline scenario, stats) |
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

**Total: ~5,292 assertions across 17 test files**

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
| User memory allocator (alloc/free/coalesce/fragmentation) | `test_mem.c` umem tests | Replaced slot allocator |
| Duplicate reloc offset | `test_reloc.c` (documents bug) | PLAN.md weak spot 2 |
| Dash boot + commands | `test-dash.sh` | Post-Phase C bugs |
| Redirection no-space parsing | `test_redir.c` | HISTORY.md bug 16 |
| STRICT_ALIGN enforcement | `make test-emu` | HISTORY.md bug 2 |

---

## Shell Piping & Command Test Plan

Comprehensive test suite for shell pipelines, commands, and edge cases.
Added 2026-03-17 after discovering multi-stage pipe corruption
(vfork+forkshell heap corruption — see bugfixes_20260317.md).

### Category 1: Pipelines

| # | Test command | Expected output | What it tests | Priority |
|---|-------------|----------------|---------------|----------|
| P1 | `echo hi \| cat` | `hi` | Basic 2-stage pipe | **High** |
| P2 | `echo hi \| cat \| cat` | `hi` | 3-stage pipe (was broken: vfork heap corruption) | **Critical** |
| P3 | `echo hi \| cat \| grep hi` | `hi` | 3-stage pipe with grep filter | **Critical** |
| P4 | `echo hi \| cat \| cat \| cat` | `hi` | 4-stage pipe (MAXPIPE=4, uses 3 pipes) | **High** |
| P5 | `echo hi \| cat \| cat \| cat \| cat` | `hi` or "Pipe call failed" | 5-stage pipe (needs 4 pipes = MAXPIPE limit) | **High** |
| P6 | `echo hello world \| wc -c` | `12` | Pipe with argument processing | Medium |
| P7 | `echo -e "a\nb\nc" \| grep b` | `b` | Pipe with multi-line data through grep | Medium |
| P8 | `echo -e "a\nb\nc" \| head -1` | `a` | Pipe into head (partial read, SIGPIPE to echo) | Medium |
| P9 | `echo -e "a\nb\nc" \| tail -1` | `c` | Pipe into tail | Medium |
| P10 | `echo -e "a\nb\na" \| sort \| uniq` | `a\nb` | Pipe chain: sort + uniq | Medium |
| P11 | `seq 10 \| wc -l` | `10` | seq piped into wc | Medium |
| P12 | `echo test \| tee /tmp/tee_out \| cat` | `test` + file contains `test` | tee in middle of pipeline | Medium |
| P13 | `echo abc \| tr a-z A-Z` | `ABC` | Pipe into tr | Medium |
| P14 | `echo hello \| rev` | `olleh` | Pipe into rev | Low |
| P15 | `yes \| head -3` | `y\ny\ny` | Infinite producer + early consumer (SIGPIPE) | **High** |

### Category 2: I/O Redirection

| # | Test command | Expected | What it tests | Priority |
|---|-------------|----------|---------------|----------|
| R1 | `echo redir > /tmp/r1; cat /tmp/r1` | `redir` | Output redirection to file | **High** |
| R2 | `echo line1 > /tmp/r2; echo line2 >> /tmp/r2; cat /tmp/r2` | `line1\nline2` | Append redirection | **High** |
| R3 | `cat < /tmp/r1` | `redir` | Input redirection from file | **High** |
| R4 | `cat nonexistent 2>/dev/null` | (no output) | Stderr redirection | **High** |
| R5 | `echo x > /tmp/r5; echo y > /tmp/r5; cat /tmp/r5` | `y` | Truncation on `>` | Medium |
| R6 | `echo pipe_redir \| cat > /tmp/r6; cat /tmp/r6` | `pipe_redir` | Pipe + output redirection combined | **High** |

### Category 3: File Operations & Error Handling

| # | Test command | Expected | What it tests | Priority |
|---|-------------|----------|---------------|----------|
| F1 | `cat nonexistent` | Error message with filename | cat missing file (currently omits filename) | **High** |
| F2 | `rm nonexistent` | `rm: cannot stat 'nonexistent'` | rm non-existent file | Medium |
| F3 | `rm somedir` (where somedir is a dir) | `rm: 'somedir' is a directory` | rm on directory without -r | **High** |
| F4 | `mkdir /tmp/td; rmdir /tmp/td` | Success (no error) | rmdir standalone command (was missing) | **Critical** |
| F5 | `mkdir /tmp/td2; touch /tmp/td2/x; rmdir /tmp/td2` | Error: not empty | rmdir on non-empty directory | **High** |
| F6 | `mkdir /tmp/td3; rm -r /tmp/td3` | Success | Recursive directory removal | Medium |
| F7 | `cp /bin/echo /tmp/cp_test; ls /tmp/cp_test` | `cp_test` listed | cp basic functionality | Medium |
| F8 | `mv /tmp/cp_test /tmp/mv_test; ls /tmp/mv_test` | `mv_test` listed | mv basic functionality | Medium |
| F9 | `touch /tmp/touch_test; ls /tmp/touch_test` | `touch_test` listed | touch creates file | Medium |

### Category 4: Commands Reading stdin (no args)

| # | Test command | Expected | What it tests | Priority |
|---|-------------|----------|---------------|----------|
| S1 | `echo line \| cat` | `line` | cat from stdin via pipe | **High** |
| S2 | `echo line \| more` | `line` | more from stdin via pipe (non-tty = cat mode) | **High** |
| S3 | `echo line \| grep line` | `line` | grep from stdin via pipe | **High** |
| S4 | `echo hello \| wc` | line/word/char counts | wc from stdin | Medium |
| S5 | `echo -e "b\na\nc" \| sort` | `a\nb\nc` | sort from stdin | Medium |
| S6 | `echo hello \| cut -c1-3` | `hel` | cut from stdin | Medium |
| S7 | `echo hello \| fold -w 3` | `hel\nlo` | fold from stdin | Low |

### Category 5: Process & Memory Limits

| # | Test scenario | Expected | What it tests | Priority |
|---|--------------|----------|---------------|----------|
| M1 | Run 5+ sequential commands | All succeed | Process cleanup, no zombie leak | **High** |
| M2 | `meminfo` after pipe commands | Free memory returns to baseline | Memory leak detection | **High** |
| M3 | Run large pipeline + external commands | No "Out of space" | Heap integrity after pipelines | **Critical** |
| M4 | Many sequential `echo x \| cat` | All produce `x` | Pipe slot reuse (MAXPIPE=4) | **High** |
| M5 | `cat /bin/dash > /dev/null` | No error | Large file through cat | Medium |

### Category 6: Levee (Editor)

| # | Test scenario | Expected | What it tests | Priority |
|---|--------------|----------|---------------|----------|
| L1 | Start levee, :q! | Clean exit | Basic startup/quit | **High** |
| L2 | Start levee, insert text, :wq | File saved | Write and quit | **High** |
| L3 | `levee /tmp/ltest` (new file) | Opens empty buffer | New file creation | Medium |
| L4 | Levee after pipe commands | No crash | Heap integrity post-pipeline (vfork bug) | **High** |

### Category 7: Shell Builtins

| # | Test command | Expected | What it tests | Priority |
|---|-------------|----------|---------------|----------|
| B1 | `cd /tmp; pwd` | `/tmp` | cd + pwd | **High** |
| B2 | `X=hello; echo $X` | `hello` | Variable assignment + expansion | **High** |
| B3 | `true; echo $?` | `0` | Exit status of true | Medium |
| B4 | `false; echo $?` | `1` | Exit status of false | Medium |
| B5 | `echo $((2+3))` | `5` | Arithmetic expansion | Medium |
| B6 | `for i in a b c; do echo $i; done` | `a\nb\nc` | For loop | Medium |
| B7 | `if true; then echo yes; fi` | `yes` | If/then/fi | Medium |
| B8 | `test -f /bin/dash; echo $?` | `0` | test builtin | Medium |

### Category 8: Edge Cases & Stress

| # | Test scenario | Expected | What it tests | Priority |
|---|--------------|----------|---------------|----------|
| E1 | Empty pipe: `\| cat` | Error message | Syntax error handling | Low |
| E2 | Pipe to non-existent: `echo x \| nosuchcmd` | Error, not crash | Missing command in pipeline | **High** |
| E3 | `echo "" \| cat` | Empty line | Empty string through pipe | Medium |
| E4 | `echo -e "$(seq 100)" \| wc -l` | `100` | Large data through pipe (>512B PIPE_SIZE) | **High** |
| E5 | Very long command line (~200 chars) | Executes or error | Input buffer limits | Low |
| E6 | `cat /dev/null` | (no output, clean exit) | Read from empty device | Medium |
| E7 | `echo x > /dev/null` | (no output) | Write to null device | Medium |

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
| `do_exec` memory leak on file-not-found | PLAN.md weak spot 5 | Low | `do_exec` now returns -ENOENT before allocating memory (header read first). No leak possible. |
| F_GETFL internal flag leakage | shell-plan.md weak spot B-1 | Easy | Set internal flag below 0x0FFF, verify it leaks through F_GETFL. |
| Environment variables not inherited | plans/decisions.md limitation 7 | Easy | Confirm `export VAR; child` doesn't see VAR (documents limitation). |

### Not feasible as unit tests

| Issue | Why | Mitigation |
|-------|-----|------------|
| Saturn keyboard double-read race | Hardware-only (no emulator model) | Manual testing on real hardware |
| Tab rendering in VDP | Visual-only (UART doesn't show it) | `make test-md-screenshot` |
| `exec_user_a5` global race | Structural property (non-preemptive kernel) | Design review if exec becomes reentrant |
| Emulator vs Mega Drive memory constraints | Emulator has 1 MB, MD has 64 KB | `make test-md-auto` catches most issues |

---

## Known Bugs (Active)

| Bug | Status | Severity | Root cause |
|-----|--------|----------|------------|
| Multi-stage pipes produce garbage | **Fixed 2026-03-17** | Critical | vfork+forkshell: child calls freejob()/forkreset() in parent's address space, corrupting heap. Fix: set `vforked` flag in forkshell() before fork(). |
| `rmdir` not found as command | **Fixed 2026-03-17** | Medium | No standalone `/bin/rmdir` binary existed; only the syscall was implemented. Fix: added `apps/rmdir.c`. |
| `cat` error omits filename | **Fixed 2026-03-17** | Low | `cat` printed generic "cannot open file" without showing which file failed. Fix: include filename in error message. |
| `more` (no args) "Out of space" | **Indirect fix 2026-03-17** | Medium | Triggered by heap corruption from prior vfork bug. Fixing the vfork bug prevents the heap corruption that causes this. |

---

## Test Infrastructure Gaps

| Gap | Notes |
|-----|-------|
| No userspace TRAP #0 test binary | Autotests bypass the real syscall path (supervisor mode). Need `apps/test_syscalls.c`. |
| No Mega Drive-specific autotest for dash | `test-md-auto` runs the builtin autotest, not dash. Dash on MD requires XIP + correct region sizing. |
| **`test-dash` must be in every commit gate** | Bug 18: the variable-size allocator gave dash zero heap and the autotest didn't catch it. Only `test-dash` tests that dash actually boots. `make test-all` includes it; partial test runs do not. |
| No emulator illegal-instruction detection | Musashi silently treats 68020 opcodes as NOPs. `check-opcodes.sh` catches at compile time instead. |
| No CI integration | Tests run manually. A GitHub Actions workflow would catch regressions automatically. |
| No multi-stage pipe test in test-dash.sh | `test-dash.sh` only tests 2-stage pipes (echo\|cat). Should add P2/P3 tests (3-stage pipes). |

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
