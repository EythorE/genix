# Shell Implementation Plan

## Context

Genix has a builtin kernel-mode shell (`kernel/main.c`, `builtin_shell()`)
with basic command execution, pipes, and I/O redirection. The goal is to
port dash (Debian Almquist Shell) as a real userspace POSIX shell.

**Depends on:** Phase 6 (`-msep-data` + slot allocator). With `-msep-data`,
each process gets its own RAM slot and accesses data through register a5.
The shell is just another process in its own slot — no special memory
treatment needed.

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

### Outcome

**Implemented:** 2026-03-14. 17 files changed, +504/-43 lines.

All planned items completed as specified. `make test-all` passes.

**New headers created (7):** setjmp.h, sys/types.h, sys/wait.h,
sys/stat.h, limits.h, paths.h, time.h.

**New source files (3):** setjmp_68000.S (setjmp/longjmp saving
d2-d7/a2-a6/SP, ported from FUZIX), signal.c (sigaction wrapper,
sigprocmask stub, raise), unistd_stubs.c (16 POSIX stub functions).

**Updated headers (3):** signal.h (NSIG, sigset_t, struct sigaction,
SA_RESTART, sigset macros), fcntl.h (F_DUPFD/F_GETFD/F_SETFD/F_GETFL/
F_SETFL, FD_CLOEXEC), unistd.h (UID/GID stubs, process groups, alarm,
sleep, sysconf, access, STDIN/STDOUT/STDERR_FILENO).

**Kernel change:** fs_stat() now writes a 32-byte POSIX-compatible
struct stat (was 12-byte kstat). Converts FT_FILE→S_IFREG|0755,
FT_DIR→S_IFDIR|0755, FT_DEV→S_IFCHR|0666. Device files populate
st_rdev with (major<<8|minor). All time fields set to mtime (no
atime/ctime tracking in minifs).

**Updated consumers:** apps/ls.c now uses `<sys/stat.h>` and S_IS*
macros instead of duplicating a local struct stat. tests/test_fs.c
updated for posix_stat struct.

**Deviations from plan:** None. The plan was straightforward and all
items were implemented exactly as specified.

**No new pitfalls discovered.** The implementation was clean — the
existing toolchain and build system handled all new files without
surprises.

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

## Phase C: Port dash

**Goal:** Port dash as a normal userspace application. With Phase 6
(`-msep-data`) complete, dash is just another process in its own slot.

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

Follow `apps/levee/Makefile` pattern: cross-compile with m68k-elf-gcc
+ `-msep-data`, link with crt0 + libc + libgcc via user-reloc.ld,
convert to Genix binary.

### Build fixes (iterative)

Expected issues and fixes:
- `fork()` → `#define fork() vfork()`
- `_setjmp`/`_longjmp` → map to setjmp/longjmp
- `sysconf()` → return hardcoded values
- `getpwnam()` → return NULL
- `mempcpy()` → `memcpy(d,s,n); return d+n`
- `strsignal()` → return static string
- `strtoimax()`/`strtoumax()` → use strtol/strtoul

### Subshells and command substitution

- `( cmd )` → vfork + exec("dash", "-c", "cmd") — re-exec approach
- `$(cmd)` → vfork + pipe + exec("dash", "-c", "cmd")

### Integration

- Add `apps/dash/dash` to CORE_BINS in top-level Makefile
- Update `kernel/main.c` launch_shell() to exec `/bin/dash`
- Fall back to builtin_shell() if dash not found

### Measure binary size early

After first successful compile, check `m68k-elf-size apps/dash/dash.elf`.
With `-msep-data`, data+bss must fit in one slot (~14 KB on MD).

### Verification

- `exec /bin/dash -c "echo hello"` → "hello"
- `exec /bin/dash -c "x=world; echo hello $x"` → "hello world"
- `exec /bin/dash -c "if true; then echo yes; fi"` → "yes"
- `make test-all` — full ladder passes

---

## Phase D: Line Editing (separate task)

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

## RAM Budget (Mega Drive, with -msep-data slots)

```
Available per slot:            ~14 KB (2 slots × 14 KB)

Dash in its slot:
  .data + .bss:                ~6 KB
  Heap (parse trees, vars):    ~4 KB
  Stack:                       ~2 KB
  Total:                      ~12 KB  (fits in 14 KB slot)

Child in its slot:
  Available:                   ~14 KB
  Typical app .data+.bss:     ~1-4 KB (with XIP, text is in ROM)
  Stack + heap:                ~2-4 KB
  → All apps fit comfortably. Large apps (levee) need PSRAM (Phase 8).
```

---

## Rejected Approaches

### SHELL_BASE memory partitioning

The original plan split user RAM into two fixed zones: a shell zone
(upper, 15.5 KB) and a child zone (lower, 12 KB). This was rejected
because:

1. **Temporary hack** — would be replaced by Phase 6 (-msep-data) anyway
2. **Complex kernel changes** — vfork-exec-wake mechanism, zone_size
   parameters throughout exec.c, romfix --shell-base support
3. **Halves child RAM** — children only get 12 KB instead of ~28 KB
4. **Sequential pipelines** — still single-process in the child zone,
   no concurrent pipelines

Doing Phase 6 first eliminates all of these problems and provides a
permanent solution. The builtin kernel shell continues working during
Phase 6 development.

### Compiling dash into the kernel

Considered as an alternative to SHELL_BASE — dash would run in kernel
mode with its data in kernel RAM. Rejected because:

1. **Kernel RAM cost** — dash needs ~6 KB .data+.bss, kernel already
   uses ~35 KB of 64 KB on Mega Drive
2. **Kernel complexity** — dash does malloc/setjmp/signal handling in
   supervisor mode
3. **Kernel size** — ~13K SLOC dash would triple the ~5.4K SLOC kernel
4. **Throwaway** — would move out of kernel once Phase 6 provides slots
