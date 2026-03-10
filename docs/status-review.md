# Genix Status Review (Third Review)

_Review date: 2026-03-10_
_Previous reviews: 2026-03-10 (second), 2026-03-09 (first)_

This document is a comprehensive status report for the Genix project — a
minimal, single-user, POSIX-enough operating system for the Motorola 68000,
targeting the Sega Mega Drive.

---

## 1. Executive Summary

**All planned phases are complete.** Genix boots and runs on both the
workbench emulator and real Mega Drive hardware (via BlastEm). The kernel
is ~5,650 lines. 34 user programs run on both platforms. A six-step CI
testing ladder enforces quality on every push. 100 commits over 3 days of
development.

---

## 2. Project Metrics


| Metric | Value |
|--------|-------|
| Total project lines (excl. Musashi) | ~80,000 |
| Kernel source (`.c` + `.S`) | 5,025 lines |
| Kernel headers (`.h`) | 628 lines |
| **Kernel total** | **5,653 lines** |
| PAL — workbench | 128 lines |
| PAL — Mega Drive | 4,553 lines |
| Libc | 2,857 lines (15 modules) |
| User programs | 2,393 lines (34 apps) |
| Emulator (incl. Musashi) | 52,095 lines |
| Host tools (mkfs, mkbin) | 656 lines |
| Test code | 5,877 lines |
| Host test assertions | **4,924** (all passing) |
| Host test files | 13 |
| Guest autotest cases | 31 |
| Syscalls implemented | 31 (32 defines, SYS_SIGRETURN is internal) |
| Platforms | 2 (workbench + Mega Drive) |
| Total commits | 100 |
| Documentation files | 17 (in `docs/`) |
| CI pipelines | 2 (`ci.yml`, `toolchain.yml`) |

---

## 3. Phase Completion Status

| Phase | Description | Status |
|-------|-------------|--------|
| Phase 1 | Workbench emulator (Musashi SBC) | **Complete** |
| Phase 2a | Kernel core + binary loading + single-tasking exec | **Complete** |
| Phase 2b | Multitasking (spawn, waitpid, process table, preemptive scheduler) | **Complete** |
| Phase 2c | Pipes and I/O redirection | **Complete** |
| Phase 2d | Signals and job control | **Complete** |
| Phase 2e | TTY subsystem (line discipline, termios) | **Complete** |
| Phase 2f | Libc + utilities | **Complete** |
| Phase 3 | Mega Drive port (PAL drivers from Fuzix) | **Complete** |
| Phase 4 | Polish (interrupt keyboard, multi-TTY, /dev/null) | **Complete** |

---

## 4. Kernel Architecture

The kernel is 10 `.c` files, 3 `.S` files, and 3 `.h` headers:

| File | Lines | Responsibility |
|------|-------|----------------|
| `proc.c` | 1,315 | Process table, scheduler, vfork, spawn, waitpid, signals |
| `main.c` | 1,264 | Boot, shell, autotest, syscall dispatch |
| `fs.c` | 654 | minifs filesystem (inodes, directories, indirect blocks) |
| `tty.c` | 472 | Line discipline, cooked/raw, echo, termios |
| `exec.c` | 211 | Binary loader (GENX header), argv setup |
| `dev.c` | 174 | Device table, open/read/write dispatch |
| `mem.c` | 102 | First-fit heap allocator |
| `string.c` | 101 | kstrcmp, kstrncpy, kstrlen, kmemset, kmemcpy |
| `kprintf.c` | 96 | Kernel printf (%d, %u, %x, %s, %c, %p) |
| `buf.c` | 69 | Block buffer cache (LRU, dirty writeback) |
| `crt0.S` | 230 | Boot code, exception vectors, trap handler |
| `exec_asm.S` | 165 | User mode entry (USP setup, JMP to entry) |
| `divmod.S` | 172 | Software 32-bit division for 68000 |
| `kernel.h` | 418 | All kernel declarations, syscall numbers, structs |
| `tty.h` | 135 | TTY structures, termios definitions |
| `dev_vdp.h` | 75 | VDP register/VRAM definitions |

### Subsystem Highlights

- **Process table**: 16 slots, 512-byte per-process kernel stacks, preemptive
  timer-driven scheduling via `swtch()`/`proc_first_run()`
- **Filesystem**: minifs with 1024-byte blocks, single and double indirect
  blocks, inode cache, directory operations, configurable buffer cache
  (NBUFS=16 workbench, NBUFS=8 Mega Drive)
- **TTY**: Full line discipline (cooked/raw modes, echo, erase, kill, OPOST),
  termios ioctls, 4 TTY slots, /dev/tty and /dev/console device nodes
- **Pipes**: 512-byte circular buffer, blocking read/write, SIGPIPE
- **Signals**: 21 signals, user handlers via signal frames on user stack,
  sigreturn trampoline, SIGTSTP/SIGCONT, process groups

---

## 5. Platform Support

### Workbench Emulator (development)

- Musashi-based 68000 SBC: 1 MB RAM, UART, timer, disk
- 2-second edit-compile-run cycle
- STRICT_ALIGN mode catches unaligned accesses
- Autotest mode for headless CI

### Mega Drive (primary target)

- 64 KB main RAM, 7.67 MHz 68000, VDP text output
- Saturn keyboard input (interrupt-driven via VBlank ISR)
- Optional SRAM with Sega mapper (boot-time validation)
- ROM disk filesystem baked into cartridge
- PAL code: 4,553 lines (VDP driver, keyboard, crt0, debug output)

---

## 6. Syscalls (31 implemented)

```
SYS_EXIT(1)     SYS_READ(3)     SYS_WRITE(4)    SYS_OPEN(5)
SYS_CLOSE(6)    SYS_WAITPID(7)  SYS_UNLINK(10)  SYS_EXEC(11)
SYS_CHDIR(12)   SYS_TIME(13)    SYS_LSEEK(19)   SYS_GETPID(20)
SYS_KILL(37)    SYS_RENAME(38)  SYS_MKDIR(39)    SYS_RMDIR(40)
SYS_DUP(41)     SYS_PIPE(42)    SYS_TIMES(43)   SYS_BRK(45)
SYS_SIGNAL(48)  SYS_IOCTL(54)   SYS_FCNTL(55)   SYS_DUP2(63)
SYS_SBRK(69)    SYS_GETCWD(79)  SYS_STAT(106)   SYS_FSTAT(108)
SYS_SIGRETURN(119) SYS_GETDENTS(141) SYS_VFORK(190)
```

Convention: TRAP #0, syscall number in d0, args in d1-d4, return in d0
(negative = -errno).

---

## 7. User Programs (34)

| Category | Programs |
|----------|----------|
| Core | hello, echo, cat, true, false |
| Text processing | wc, head, tail, tee, rev, nl, cut, tr, uniq, fold, expand, unexpand, grep |
| File utilities | ls, cmp, strings, od, basename, dirname, comm, paste, tac |
| System | sleep, env, expr, seq, yes |
| Graphics | imshow (VDP libgfx) |
| Editor | levee (vi clone, workbench only) |

All programs are standalone C files built against Genix libc + crt0,
producing flat binaries with the 32-byte GENX header.

---

## 8. Libc (15 modules, 2,857 lines)

| Module | Key functions |
|--------|--------------|
| `stdio.c` | FILE*, fopen, fgets, fprintf, puts, fread, fwrite |
| `stdlib.c` | malloc/free, atoi, atol, exit, qsort, bsearch, rand, getenv |
| `string.c` | strstr, strcasecmp, strcspn, strspn, strtok |
| `sprintf.c` | sprintf, snprintf, sscanf |
| `strtol.c` | strtol, strtoul |
| `ctype.c` | isdigit, isalpha, isspace, toupper, tolower |
| `termios.c` | tcgetattr, tcsetattr, cfmakeraw |
| `getopt.c` | getopt (POSIX) |
| `perror.c` | perror, strerror |
| `dirent.c` | opendir, readdir, closedir |
| `isatty.c` | isatty |
| `regex.c` | regcomp, regexec, regfree |
| `gfx.c` | VDP userspace graphics library |
| `divmod.S` | __udivsi3, __umodsi3, __divsi3, __modsi3 (68000 software division) |
| `syscalls.S` | All syscall stubs (TRAP #0 wrappers) |

---

## 9. Testing

### Host Tests (4,924 assertions, 13 files, all passing)

| Test file | Assertions | Coverage |
|-----------|------------|----------|
| `test_pipe.c` | 2,170 | Pipe stress: full buffer, wrap, partial reads, EOF/EPIPE |
| `test_exec.c` | 155 | Header validation, argv layout |
| `test_signal.c` | 118 | User handlers, signal frames, stop/continue |
| `test_proc.c` | 94 | Spawn, waitpid, process groups |
| `test_tty.c` | 78 | Line discipline: cooked, raw, echo, erase, signals |
| `test_libc.c` | 71 | getopt, sprintf, strtol, qsort, sscanf, regex |
| `test_string.c` | 60 | String functions |
| `test_mem.c` | 48 | First-fit allocator, coalescing, fragmentation |
| `test_redir.c` | 52 | Pipes, dup/dup2, refcounting |
| `test_buf.c` | 36 | LRU eviction, dirty writeback, cache pressure |
| `test_fs.c` | 34 | Inode alloc, directories, indirect blocks, deallocation |
| `test_vdp.c` | 1,984 | VDP validation |
| `test_kprintf.c` | 24 | Format specifiers, edge cases |

### Guest Autotest (31 cases)

Runs on both workbench emulator and BlastEm (Mega Drive). Tests exec,
argument passing, exit codes, file I/O, pipes, and signal handling via
predetermined kernel commands.

### CI Pipeline

Two GitHub Actions workflows:

1. **`ci.yml`**: host-tests → cross-build (kernel + megadrive + workbench) → emu-tests
2. **`toolchain.yml`**: Rebuilds m68k-elf cross-compiler when `scripts/build-toolchain.sh` changes

### Testing Ladder

```
make test          →  Host unit tests (no cross-compiler)
make kernel        →  Cross-compilation check
make test-emu      →  Workbench autotest (STRICT_ALIGN + AUTOTEST)
make megadrive     →  Mega Drive ROM build
make test-md       →  Headless BlastEm boot (~5s)
make test-md-auto  →  BlastEm autotest (PRIMARY QUALITY GATE)
make test-all      →  Full ladder in sequence
```

---

## 10. Divergences From Original Plan

### Binary Format (Medium impact)

**Plan:** PIC ELF or bFLT with relocation.
**Reality:** Fixed-address flat binary (GENX header, 32 bytes).

Only one user process can be in memory at a time. Pipelines execute
sequentially. This is the single biggest architectural limitation, but is
acceptable for the single-user, no-MMU target.

### C Library (Beneficial divergence)

**Plan:** newlib.
**Reality:** Custom minimal libc (2,857 lines).

newlib is 50-100 KB — far too large for 64 KB RAM. The custom libc is the
correct choice and is proven (levee vi clone works).

### Process Creation (Resolved)

Both vfork() (setjmp/longjmp) and spawn() coexist. vfork was added after
per-process kernel stacks were implemented. No negative impact.

---

## 11. What Genix Does Better Than FUZIX

1. **Readability**: ~5,650 lines vs. FUZIX's ~15,000+ (readable in one sitting)
2. **Host test suite**: 4,924 assertions vs. FUZIX's zero host tests
3. **Workbench emulator**: 2-second edit-compile-run cycles
4. **STRICT_ALIGN mode**: Catches unaligned accesses before hardware
5. **Clean PAL separation**: Two directories vs. scattered `#ifdef`s
6. **Automated testing ladder**: Six steps with `make test-all`
7. **Safe division**: Custom `divmod.S` prevents 68020 illegal instructions

---

## 12. Known Limitations (by design)

1. **Single user memory space** — all programs load at USER_BASE; no
   concurrent user processes in memory
2. **Sequential pipelines** — producer runs to completion, then consumer
   runs (no-MMU fundamental limitation)
3. **No glob expansion** — shell doesn't support `*.c` patterns
4. **No environment variable substitution** in shell
5. **No background jobs** — single-terminal, single-user design
6. **64 KB RAM constraint** — 35 KB kernel + 28 KB user space on Mega Drive

---

## 13. Remaining Testing Gaps

1. **No context switch tests** — `swtch()`/`proc_first_run()` assembly
   tested only via autotest, not host tests
2. **No multi-process interaction autotest** — no test for concurrent
   processes exercising scheduling, signals during I/O
---

## 14. Possible Future Work

| Item | Effort | Value | Priority |
|------|--------|-------|----------|
| Glob expansion in shell | Medium | Enables `*.c` patterns | Low |
| SRAM persistent filesystem | Medium | Writable persistent storage | Low |
| Real shell (sh) with job control | Medium | Tier 3 utility | Low |
| Larger utilities (ed, diff, sort) | High | May not fit on Mega Drive | Low |
| Binary relocation support | High | Concurrent user processes | Low |

None of these are blockers. The system is usable as-is for its design goals.

---

## 15. File Inventory

### Kernel (16 files, 5,653 lines)

```
kernel/proc.c       1,315    kernel/tty.h          135
kernel/main.c       1,264    kernel/mem.c          102
kernel/fs.c           654    kernel/string.c       101
kernel/tty.c          472    kernel/kprintf.c       96
kernel/kernel.h       418    kernel/dev_vdp.h       75
kernel/crt0.S         230    kernel/buf.c           69
kernel/exec.c         211
kernel/divmod.S       172
kernel/dev.c          174
kernel/exec_asm.S     165
```

### Documentation (17 files)

```
docs/architecture.md       docs/megadrive.md
docs/automated-testing.md  docs/multitasking.md
docs/binary-format.md      docs/status-review.md
docs/decisions.md          docs/syscalls.md
docs/emulator.md           docs/toolchain.md
docs/filesystem.md         docs/tty.md
docs/fuzix-heritage.md     docs/graphics.md
docs/kernel.md             docs/README.md
docs/68000-programming.md
```
