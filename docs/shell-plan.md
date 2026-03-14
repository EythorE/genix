# Shell Implementation Plan

## Context

Genix has a builtin kernel-mode shell (`kernel/main.c`, `builtin_shell()`)
with basic command execution, pipes, and I/O redirection. The next step is
moving to a userspace shell and eventually porting dash for POSIX scripting.

**Problem:** All user programs share a single USER_BASE address. When the
shell spawns a child via exec, the child binary overwrites the shell's memory.

**Solution: SHELL_BASE memory partitioning.** Split user RAM into two zones:
- Shell zone (upper): shell loads at SHELL_BASE, text in ROM (XIP on MD)
- Child zone (lower): child programs load at USER_BASE

The shell's data survives child exec because they're at different addresses.
After `do_exec()` loads a child binary, it wakes the vfork parent (the shell),
and both run concurrently via the scheduler.

**Memory layout (Mega Drive):**
```
0xFF9000 — 0xFFBFFF  (12 KB)    Child zone (USER_BASE → SHELL_BASE)
0xFFC000 — 0xFFFDFF  (15.5 KB)  Shell zone (SHELL_BASE → USER_TOP)
0xFFFE00 — 0xFFFFFF             Kernel stack
```

**Memory layout (Workbench):**
```
0x040000 — 0x0BFFFF  (512 KB)   Child zone (USER_BASE → SHELL_BASE)
0x0C0000 — 0x0EFFFF  (192 KB)   Shell zone (SHELL_BASE → USER_TOP)
0x0F0000 — 0x0FFFFF             Kernel stack
```

**Design principle:** Each phase below is independently valuable, separately
shippable, and leaves the tree buildable + testable. The riskiest kernel
change (vfork-exec-wake) is validated with a tiny known-working shell before
attempting a 13K SLOC dash port.

**Reference:** [docs/shell-research.md](shell-research.md) — full analysis
of 8 shell candidates and why dash was chosen.

---

## Phase A: Libc Prerequisites

**Goal:** Add POSIX headers and functions needed by any future userspace
program, not just dash. This is foundational libc work.

### New headers to create

| File | Contents |
|------|----------|
| `libc/include/setjmp.h` | `jmp_buf[12]`, setjmp/longjmp, sigjmp_buf aliases |
| `libc/include/sys/types.h` | pid_t, uid_t, gid_t, off_t, mode_t, size_t, ssize_t |
| `libc/include/sys/wait.h` | WIFEXITED/WEXITSTATUS/etc macros, WNOHANG, WUNTRACED |
| `libc/include/sys/stat.h` | struct stat, S_ISREG/S_ISDIR/etc macros, stat/fstat decls |
| `libc/include/limits.h` | CHAR_BIT, INT_MIN/MAX, LONG_MIN/MAX, PATH_MAX, NAME_MAX |
| `libc/include/paths.h` | _PATH_BSHELL, _PATH_DEVNULL, _PATH_TTY |
| `libc/include/time.h` | time_t, struct timeval (stubs) |

### New source files

| File | Contents |
|------|----------|
| `libc/setjmp_68000.S` | setjmp/longjmp for 68000, ported from FUZIX |
| `libc/signal.c` | sigaction() wrapper, sigprocmask() stub, raise() |
| `libc/unistd_stubs.c` | getuid/setuid/getpgrp/setpgid/alarm etc. (all return 0) |

### Headers to update

| File | Changes |
|------|---------|
| `libc/include/signal.h` | Add NSIG, sigset_t, struct sigaction, SA_RESTART, sigset ops |
| `libc/include/fcntl.h` | Add F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL, FD_CLOEXEC |
| `libc/include/unistd.h` | Add getuid/gid/euid/egid, setuid/gid, getpgrp, setpgid, alarm |
| `libc/Makefile` | Add setjmp_68000.o, signal.o, unistd_stubs.o to OBJS |

### setjmp_68000.S details

Port from FUZIX's `Library/libs/setjmp_68000.S`:
- `setjmp`: `movem.l %d2-%d7/%a2-%a6, (%a0)` saves 11 regs (44 bytes).
  Save SP at offset 44 (env[11]). Return 0. The return address stays on
  the stack — longjmp restores SP which puts it back.
- `longjmp`: Restore regs, restore SP, if val==0 set val=1 (POSIX). RTS
  returns to original setjmp call site.

### sys/stat.h — kernel alignment needed

The kernel's `fs_stat()` (fs.c:644) currently fills a 12-byte `struct kstat`:
```c
struct kstat { uint16_t inum; uint8_t type; uint8_t nlink; uint32_t size; uint32_t mtime; };
```

Dash (and POSIX programs) expect `st_mode` with S_ISREG/S_ISDIR macros.
**Update `fs_stat()` to fill a POSIX-compatible struct**, converting the
internal type field:
- FT_FILE (1) → S_IFREG | 0755
- FT_DIR (2) → S_IFDIR | 0755
- FT_DEV (3) → S_IFCHR | 0666

### Verification

- `make test` — all existing + new tests pass
- `make kernel` — cross-compilation succeeds
- `make megadrive` — Mega Drive build succeeds

---

## Phase B: Kernel Enhancements

**Goal:** General-purpose kernel improvements needed by any userspace shell.

### fcntl F_DUPFD (proc.c)

Replace the SYS_FCNTL stub (currently returns 0) with a real implementation:
- F_DUPFD (0): Find lowest free fd >= arg, dup source fd, refcount++
- F_GETFD (1) / F_SETFD (2): Return/accept 0 (no cloexec yet)
- F_GETFL (3): Return fd flags
- F_SETFL (4): Stub returning 0

### waitpid WNOHANG (proc.c)

Change `do_waitpid(int pid, int *status)` → `do_waitpid(int pid, int *status, int options)`.
After the "no exited child found" point, check `if (options & WNOHANG) return 0`.
Update SYS_WAITPID dispatch to pass a3 as options. Update ALL callers to pass 0.

### fs_stat POSIX struct (fs.c)

Update `fs_stat()` to write the new POSIX-compatible struct stat layout from
Phase A. Convert type field to st_mode.

### Verification

- `make test` — updated tests pass
- `make test-all` — full ladder passes

---

## Phase C: SHELL_BASE Memory Zones + vfork-exec-wake

**Goal:** Kernel infrastructure for running a shell at SHELL_BASE while
children load at USER_BASE. This is the riskiest change — vfork-exec-wake
fundamentally alters exec behavior.

### PAL additions

- `pal/pal.h`: Add `uint32_t pal_shell_base(void)`
- `pal/workbench/platform.c`: Return 0x0C0000
- `pal/megadrive/platform.c`: Return 0xFFC000

### Kernel globals (kernel.h, main.c)

Add `SHELL_BASE`, `SHELL_SIZE`, `CHILD_SIZE` globals. Initialize in kmain()
from `pal_shell_base()`.

### zone_size parameter (exec.c)

Add `zone_size` parameter to:
- `exec_validate_header(hdr, zone_size)` — replace `USER_SIZE` check
- `exec_validate_header_xip(hdr, zone_size)` — same
- `load_binary(path, argv, load_addr, zone_size, ...)` — use for stack_top, mem_size
- `load_binary_xip(path, argv, text_addr, data_addr, zone_size, ...)` — same

### do_exec uses curproc->mem_base (exec.c)

Change hardcoded `USER_BASE` → `curproc->mem_base` in both load calls.
Pass `curproc->mem_size` as zone_size. This is the key change that makes
the shell load at SHELL_BASE.

### do_spawn/do_vfork child zone (proc.c)

- `do_spawn()`: Set `child->mem_base = USER_BASE; child->mem_size = CHILD_SIZE`
- `do_spawn_fd()`: Same
- `do_vfork()`: After `*child = *parent`, override mem_base/mem_size to USER_BASE/CHILD_SIZE

### vfork-exec-wake (exec.c — KEY CHANGE)

In `do_exec()`, after successful binary loading, before `exec_enter()`:
if curproc's parent is in P_VFORK state, set up child kstack, mark child
P_READY, wake parent via `vfork_restore()`. Parent resumes from vfork with
child PID. Non-vfork path (synchronous exec) continues unchanged.

### Process 0 initialization (proc.c)

In `proc_init()`, set `curproc->mem_base = USER_BASE; curproc->mem_size = USER_SIZE`.

### Host test updates (test_exec.c)

Update `exec_validate_header()` in tests to take zone_size. Add tests for
CHILD_SIZE and SHELL_SIZE validation.

### Test with minimal program, NOT dash

Write a tiny test: `apps/testsh.c` — a 20-line C program that does
`vfork()` + `execve("/bin/echo", ...)` in a loop. Load it at SHELL_BASE.
If this works, the mechanism is sound. If not, debug with known-simple code.

### Verification

- `make test` — updated exec tests pass
- `make test-emu` — workbench boots, builtin_shell works (still kernel-mode)
- `make megadrive` + `make test-md-auto` — Mega Drive works

---

## Phase D: Userspace Builtin Shell

**Goal:** Move the existing builtin_shell() to userspace as `apps/sh.c`.
This validates the entire SHELL_BASE + vfork-exec-wake pipeline with
known-working, well-understood code (~200 lines) before attempting dash.

### apps/sh.c

Extract `builtin_shell()` from kernel/main.c into a standalone C program.
Replace kernel-internal calls with libc equivalents:
- `devtab[DEV_CONSOLE].read()` → `read(0, buf, n)`
- `kprintf()` → `printf()` / `write()`
- `do_spawn_fd()` → `vfork()` + fd setup + `execve()`
- `do_waitpid()` → `waitpid()`
- `fs_namei()` → `stat()`

Keep all existing features: pipes, redirects, PATH search, cd, help, etc.

### do_spawn_at() (proc.c)

Add `do_spawn_at(path, argv, load_addr, zone_size)` — like `do_spawn()` but
takes explicit address/size. `alloc_pid()` is static in proc.c so this must
live there too.

### launch_shell() (main.c)

Call `do_spawn_at("/bin/sh", argv, SHELL_BASE, SHELL_SIZE)` then `do_waitpid`.
Fall back to builtin_shell() if /bin/sh not found.

### romfix SHELL_BASE support (tools/romfix.c)

Add `--shell-base <addr> --shell <name>` arguments. In `process_binary()`,
use shell_base instead of user_base for the named binary's data references.
Update `pal/megadrive/Makefile` to pass `--shell-base 0xFFC000 --shell sh`.

### Disk image

Add `apps/sh` to CORE_BINS in the top-level Makefile.

### AUTOTEST

Keep AUTOTEST using builtin_shell initially (via the existing `#ifdef AUTOTEST`
path). Add a separate test that execs /bin/sh with `-c` to verify it works.

### Verification

- `make run` — boots, `/bin/sh` launches, interactive shell works
- Pipes: `echo hello | cat`
- Redirects: `echo hello > /tmp/test`
- PATH search: `hello` runs `/bin/hello`
- `make test-all` — full ladder passes
- **This is the milestone:** a real userspace shell on Genix.

---

## Phase E: Port dash

**Goal:** Replace the simple userspace shell with dash for POSIX scripting.
The kernel infrastructure is now proven by Phase D.

### Download and configure dash

Download dash 0.5.12. Run autotools on host to generate:
builtins.{c,h}, init.c, nodes.{c,h}, syntax.{c,h}, token.h, token_vars.h.
Copy ~30 source files to `apps/dash/`.

### Configuration

Create `apps/dash/config.h`: JOBS=0, SMALL=1, BSD=0, WITH_LINEEDIT=0,
HAVE_FNMATCH=0, HAVE_GLOB=0, HAVE_DEV_FD=0, HAVE_ALLOCA=0, etc.

Create `apps/dash/genix_compat.h`: `#define fork() vfork()`, killpg stub,
other POSIX shims.

### Makefile

Follow `apps/levee/Makefile` pattern: cross-compile with m68k-elf-gcc,
link with crt0 + libc + libgcc via user-reloc.ld, convert to Genix binary.

### Build fixes (iterative)

Expected issues and fixes:
- `fork()` → already handled by `#define fork() vfork()`
- `_setjmp`/`_longjmp` → map to setjmp/longjmp
- `sysconf()` → return hardcoded values
- `getpwnam()` → return NULL
- `mempcpy()` → `memcpy(d,s,n); return d+n`
- `strsignal()` → return static string
- `strtoimax()`/`strtoumax()` → use strtol/strtoul

### Subshells and command substitution

- `( cmd )` → vfork + exec("dash", "-c", "cmd") — re-exec approach
- `$(cmd)` → vfork + pipe + exec("dash", "-c", "cmd")
- Pipelines execute sequentially (same as Phase D shell)

### Measure binary size early

After first successful compile, check `m68k-elf-size apps/dash/dash.elf`.
If data+bss > 15 KB, lower SHELL_BASE (e.g., 0xFFB000 → 20 KB shell, 8 KB child).

### Integration

- Update romfix `--shell dash` instead of `--shell sh`
- Update launch_shell() path to `/bin/dash`
- Add `apps/dash/dash` to CORE_BINS

### Verification

- `exec /bin/dash -c "echo hello"` → "hello"
- `exec /bin/dash -c "x=world; echo hello $x"` → "hello world"
- `exec /bin/dash -c "if true; then echo yes; fi"` → "yes"
- `make test-all` — full ladder passes

---

## Phase F: Line Editing (separate task)

**Goal:** Arrow key history and basic line editing for interactive use.

Custom ~800-line module:
- Arrow key navigation (left/right)
- History (up/down, 16 entries x 128 bytes = 2 KB)
- Backspace, Home, End
- Terminal escape sequences (ESC [ A/B/C/D)
- Raw terminal mode via termios (already supported)

This replaces libedit and is tailored to Genix's terminal capabilities
(VDP console on MD, UART on workbench). Reusable by other interactive programs.

---

## Key Files Summary

### New files (by phase):

| Phase | File | Purpose |
|-------|------|---------|
| A | `libc/setjmp_68000.S` | setjmp/longjmp for 68000 |
| A | `libc/signal.c` | sigaction/sigprocmask/raise shims |
| A | `libc/unistd_stubs.c` | getuid/setuid/alarm etc. stubs |
| A | `libc/include/setjmp.h` | jmp_buf, setjmp/longjmp |
| A | `libc/include/sys/types.h` | pid_t, off_t, etc. |
| A | `libc/include/sys/wait.h` | W* macros, WNOHANG |
| A | `libc/include/sys/stat.h` | struct stat, S_IS* macros |
| A | `libc/include/limits.h` | INT_MAX, PATH_MAX, etc. |
| A | `libc/include/paths.h` | _PATH_BSHELL, _PATH_DEVNULL |
| A | `libc/include/time.h` | time_t, struct timeval |
| D | `apps/sh.c` | Userspace builtin shell |
| E | `apps/dash/*` | Entire dash port |

### Modified files:

| Phase | File | Changes |
|-------|------|---------|
| A | `libc/include/signal.h` | sigaction, sigprocmask, sigset_t |
| A | `libc/include/fcntl.h` | F_DUPFD, F_GETFD, etc. |
| A | `libc/include/unistd.h` | getuid, setpgid, etc. |
| A | `libc/Makefile` | Add new .o files |
| B | `kernel/proc.c` | fcntl F_DUPFD, waitpid WNOHANG |
| B | `kernel/fs.c` | fs_stat fills POSIX-compatible struct |
| B | `kernel/kernel.h` | do_waitpid signature |
| B | `kernel/main.c` | do_waitpid callers |
| C | `kernel/kernel.h` | SHELL_BASE/SHELL_SIZE/CHILD_SIZE, fn sigs |
| C | `kernel/exec.c` | zone_size params, vfork-exec-wake, mem_base |
| C | `kernel/proc.c` | vfork child zone, proc_init mem_base |
| C | `kernel/main.c` | SHELL_BASE init |
| C | `pal/pal.h` | pal_shell_base() |
| C | `pal/workbench/platform.c` | pal_shell_base() → 0x0C0000 |
| C | `pal/megadrive/platform.c` | pal_shell_base() → 0xFFC000 |
| C | `tests/test_exec.c` | zone_size param |
| D | `kernel/proc.c` | do_spawn_at() |
| D | `kernel/main.c` | launch_shell() |
| D | `tools/romfix.c` | --shell-base/--shell args |
| D | `pal/megadrive/Makefile` | romfix --shell-base |
| D | `Makefile` | Add sh to disk image |
| E | `Makefile` | Add dash to disk image |

---

## Key Risks

1. **vfork-exec-wake correctness** — The riskiest change. Test with a
   minimal 20-line test program first (Phase C), then the 200-line
   userspace shell (Phase D), before attempting 13K SLOC dash (Phase E).

2. **SHELL_BASE halves child RAM on MD** — Children get 12 KB instead
   of ~28 KB. Measure impact on existing apps. If too tight, adjust
   SHELL_BASE (trade shell space for child space).

3. **Dash data+bss too large for MD** — Measure early in Phase E.
   If >15 KB, lower SHELL_BASE or strip dash features.

4. **sys/stat.h kernel mismatch** — Must update kernel fs_stat() to
   write POSIX struct. The current struct kstat is only 12 bytes.

5. **Sequential pipelines** — `cat file | grep foo` runs stages
   one-at-a-time, not concurrently. This is a known limitation until
   Phase 6 (-msep-data + RAM slots). Not a correctness bug, but
   pipelines with large intermediate data won't work as expected.

---

## RAM Budget (Mega Drive)

```
Available user RAM:          ~27.5 KB

With SHELL_BASE partitioning:
  Shell zone:                 15.5 KB (SHELL_BASE → USER_TOP)
  Child zone:                 12 KB   (USER_BASE → SHELL_BASE)

Shell RAM usage (dash):
  .data + .bss:              ~6 KB
  Heap (parse trees, vars):  ~4 KB
  Stack:                     ~2 KB
  Line editor + history:     ~3 KB (Phase F)
  Total:                    ~15 KB   (fits in 15.5 KB zone)

Child RAM usage:
  Available:                  12 KB
  Typical app .data+.bss:    ~1-4 KB (with XIP, text is in ROM)
  Stack + heap:              ~2-4 KB
  → Most apps fit comfortably. Large apps (levee) won't fit.
```
