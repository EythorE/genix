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

**Bug fixed post-review:** `access()` in `unistd_stubs.c` used
`char buf[32]` with a forward-declared `stat()` returning into a void
pointer. Two problems: (a) `char buf[32]` can land at an odd stack
address on 68000, causing an address error fault when `fs_stat()`
writes `uint32_t` fields; (b) the magic size `32` was fragile — if
`struct stat` ever grows, the buffer silently overflows. Fixed by
`#include <sys/stat.h>` and using `struct stat st` (compiler-aligned).

**Known weak spots** (not bugs today, but places to look if issues arise):

1. **`sigaction()` read-old-handler race** (`signal.c`). To read the
   current handler, `sigaction()` calls `signal(sig, SIG_DFL)` then
   restores the old handler. Between those two calls, a signal could
   be delivered and would hit `SIG_DFL` (potentially killing the
   process). Safe today because signal delivery is rare and brief,
   but a proper `SYS_SIGACTION` kernel syscall would eliminate this.

2. **Kernel `struct posix_stat` / libc `struct stat` are defined
   independently** (`kernel/fs.c` vs `libc/include/sys/stat.h`).
   Layouts must match exactly but there's no shared header or
   compile-time assertion. If either changes independently,
   `fs_stat()` silently produces garbage. A comment cross-references
   the other definition, but a `_Static_assert` on `sizeof` would
   be more robust.

3. **`test_fs.c` uses `struct posix_stat`** (kernel's internal name)
   rather than `struct stat` from `sys/stat.h`. Host tests compile
   against kernel headers, so this works, but it means the test
   doesn't verify that the libc header layout matches the kernel.

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

### Outcome

**Implemented:** 2026-03-14. 4 files changed, +301/-25 lines.

fs_stat POSIX struct was already completed in Phase A, so only two
items remained: fcntl F_DUPFD and waitpid WNOHANG.

**fcntl:** Replaced the SYS_FCNTL stub with a real implementation.
Added `fd_alloc_from(of, minfd)` helper for F_DUPFD's "lowest fd >=
arg" semantics. F_DUPFD increments refcount before allocating, with
rollback on failure. F_GETFL returns flags masked with 0x0FFF to
exclude internal pipe endpoint bits (OFILE_PIPE_READ/WRITE at 0x1000/
0x2000). F_GETFD/F_SETFD accept but ignore cloexec (no exec-close
support yet). F_SETFL is a no-op stub.

**waitpid:** Changed `do_waitpid(pid, status)` → `do_waitpid(pid,
status, options)`. After scanning all children and finding no zombie,
checks `(options & WNOHANG)` → returns 0. Updated all 13 callers in
kernel/main.c to pass 0. The libc waitpid stub already passed d3
(options) to the kernel, so no assembly changes were needed.

**Host tests (9 new):** 4 fcntl tests (basic dup, skip occupied, table
full, getfl), 5 waitpid tests (WNOHANG no zombie, WNOHANG with zombie,
no children, blocking reap, specific PID).

**Deviations from plan:** None.

**Known weak spots:**

1. **F_GETFL pipe bit leakage.** The 0x0FFF mask in F_GETFL strips
   internal OFILE_PIPE_READ/WRITE bits, but if future internal flags
   are added in the 0x0001-0x0FFF range, they could leak into the
   user-visible flags. Currently safe because the only internal flags
   are above 0x0FFF.

2. **Process 0 self-parenting.** Process 0 has ppid=0, so
   do_waitpid called from process 0 sees itself as its own child.
   Not a bug (process 0 always has real children when it calls
   waitpid), but the test suite had to use a non-zero process to
   test the "no children → -ECHILD" path.

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

### Outcome

**Implemented:** 2026-03-14. dash 0.5.12 ported and integrated.

**Binary size (m68k-elf-size):**
```
   text    data     bss     dec     hex filename
  91236    4500    2296   98032   17ef0 apps/dash/dash.elf
```
Data+bss = 6,796 bytes (49% of 14 KB Mega Drive slot).

**Source files:** 32 dash C files + 3 bltin files (printf.c, test.c,
bltin.h) + genix_stubs.c = 36 compiled objects.

**Configuration:** JOBS=0, SMALL=1, no line editing, no glob/fnmatch.
`config.h` defines feature flags, `genix_compat.h` provides shims
(fork→vfork, wait3→waitpid, strtoimax→strtoll, etc.).

**Libc additions required (significant):**
- 7 new headers: inttypes.h, pwd.h, sys/ioctl.h, sys/param.h,
  sys/resource.h, sys/time.h, sys/times.h
- 15+ new functions: strtoll, strtoull, strtod (integer-only), abort,
  vsnprintf (full format string implementation), strpbrk, stpncpy,
  strsignal (with signal name table for 21 signals), isblank, isgraph,
  ispunct, isxdigit
- Updated headers: errno.h, fcntl.h, signal.h, ctype.h, string.h,
  stdlib.h, stdio.h, sys/stat.h, sys/types.h

**Integration:** kernel/main.c spawns `/bin/dash` in a respawn loop,
falls back to `builtin_shell()`. dash added to CORE_BINS for both
platforms.

**Deviations from plan:**
1. No `_setjmp`/`_longjmp` mapping needed — dash uses `setjmp`/`longjmp`
   directly.
2. `mempcpy`/`stpcpy`/`strchrnul` — not added to libc; instead unset
   the HAVE_ flags so dash's own `system.c` provides implementations.
3. `strsignal` — added to libc with full signal name table rather than
   returning a static string, because dash's fallback uses `sys_siglist`
   which Genix doesn't have.
4. `vsnprintf` — needed a full implementation (~180 lines) with va_list
   support, format flags, and length modifiers. dash's printf builtin
   and error reporting depend on it.
5. Libc grew more than expected — 7 new stub headers + 15 new functions.
   Each was a real dependency that couldn't be stubbed away.

**Gotchas:**
1. GCC's `stdint.h` defines `intmax_t` as `long long` (8 bytes on 68000).
   Initial attempt to typedef as `long` caused conflicts. Must use GCC's
   types and set `SIZEOF_INTMAX_T=8`.
2. `#include <sys/param.h>` requires a physical file — can't be handled
   with header guard tricks in config.h. Same for all the other stub
   headers.
3. mkbuiltins (dash's code generator) produces garbled output if run
   outside the proper autotools build. Must use `make -C src builtins.c
   builtins.h` from a configured dash source tree.
4. Duplicate symbol conflicts between genix_stubs.c and libc (fstat
   defined in both). Must carefully partition which stubs go where.
5. `-DSHELL` must be in CFLAGS — bltin files include dash internal
   headers conditionally on `#ifdef SHELL`.

**All tests pass:** host (63/63), test-emu (31/31), test-all (megadrive +
BlastEm headless + BlastEm autotest).

### Post-Port Bug Fixes (March 2026)

After the initial Phase C completion, several bugs prevented dash from
working correctly as an interactive shell. These were found through
systematic testing (test_dash.c exposed 4 blocking bugs) and fixed in
a series of commits:

1. **vfork+execve semantics** — `do_exec()` was synchronous, returning
   exit codes to the caller. POSIX requires `execve()` to never return
   on success. Added an async exec path: when a vfork child calls
   `execve()`, the kernel sets up a kstack frame, marks the child
   `P_READY`, and wakes the parent via `vfork_restore()`.

2. **errno not set by syscall stubs** — Libc stubs passed raw negative
   kernel return values through without setting `errno`. Dash's PATH
   search, waitpid retry, and error reporting all depend on errno.
   Added `__set_errno` to libc syscall stubs.

3. **libgcc 68020 opcodes** — `strtoull` multiplication and `vsnprintf`
   division pulled in `__muldi3`/`__divdi3`/`__moddi3` containing
   68020-only `MULU.L`/`DIVU.L`. Rewrote both to use 32-bit arithmetic.

4. **sys_getcwd kstack overflow** — 256-byte `names[8][32]` array on the
   512-byte kstack. Moved to static storage.

5. **romfix/mkfs indirect blocks** — Dash binary (95 KB) exceeded 7
   direct blocks. mkfs now pre-allocates indirect blocks before data
   blocks for XIP contiguity; romfix follows indirect pointers.

6. **Workbench slot sizing** — Reduced from 8×88KB to 6×117KB so dash
   (91 KB text) fits in a workbench slot.

7. **vfork TRAP frame corruption** — Child's TRAP #0 overwrote parent's
   saved registers on the shared kstack. Fixed by reloading SP from
   `curproc`'s kstack frame after `syscall_dispatch()`.

8. **dash exit builtin crash** — `exraise(EXEXIT)` longjmp chain crashed
   under `-msep-data`. Changed to call `_exit()` directly.

9. **Panic handler** — Enhanced to decode 68000 group 0 fault frames
   (PC, SR, access address), making all the above debugging feasible.

**Toolchain dependency:** Bug #3 (libgcc 68020 opcodes) is only
triggered when building with the distro `m68k-linux-gnu-gcc`. The
correct toolchain (`m68k-elf-gcc` from `fetch-toolchain.sh`) has a
68000-safe libgcc. The libc workarounds (32-bit strtoull/vsnprintf)
are defense-in-depth but not needed with the right compiler. See
`docs/toolchain.md` for details.

See HISTORY.md section 4 for detailed root cause analysis of each bug.

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
