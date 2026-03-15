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

**Total: ~5,130 assertions across 16 test files**

---

## Integration Tests (Levels 3, 5)

| Test | Command | What it tests |
|------|---------|---------------|
| `check-opcodes.sh` | `make test-opcodes` | Scan .elf files for 68020 MULU.L/DIVU.L/EXTB.L/RTD |
| `test-dash.sh` | `make test-dash` | 13 tests: boot, echo, exit status, ls, pipes, redirection, variables, error absence |
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
