# Shell Research: Choosing a Shell for Genix

## Context

Genix is a minimal POSIX-enough OS for the Sega Mega Drive (Motorola 68000, 7.67 MHz,
64 KB main RAM). The project is implementing XIP (Execute-in-Place), which means .text
runs from ROM (up to 4 MB), so code size is relatively unconstrained. However, RAM is
still limited to 64 KB total, with approximately 28 KB available for user programs
(.data + .bss + heap + stack all must fit).

### Requirements

1. **Command history with up-arrow navigation** -- essential for usability
2. **Job control** -- background jobs with `&`, Ctrl-Z to suspend, `fg`/`bg`
3. **SIGTSTP/SIGCONT support** -- kernel already implements this
4. **Pipes and I/O redirection** -- kernel already supports `|`, `>`, `>>`, `<`
5. **Reasonable RAM usage** -- .data + .bss must fit in ~28 KB alongside stack/heap
6. **68000 portable** -- no 32-bit division assumptions, no MMU, even alignment
7. **Cross-compilable** with m68k-elf-gcc
8. **POSIX-ish** compatibility

### Current Shell

The current Genix shell is a kernel-mode built-in shell in `kernel/main.c`
(function `builtin_shell()`). It is approximately 170 lines of C and provides:

- A simple read-eval loop via `devtab[DEV_CONSOLE].read()`
- Built-in commands: help, halt, mem, ls, cat, echo, cd, mkdir, write
- External command execution via `do_spawn_fd()` (with implicit exec)
- Pipeline support (sequential execution, up to 8 stages)
- I/O redirection (`<`, `>`, `>>`)
- PATH search (automatic `/bin/` prefix)

**Missing features:** No command history, no line editing (beyond TTY cooked mode),
no job control, no variables, no scripting (if/then/else, loops), no globbing,
no quoting, no aliases, no functions. The shell runs in supervisor mode as process 0,
not as a userspace program.

### Available Genix Libc

The Genix libc (in `libc/`) provides:

- **stdio**: FILE*, fopen/fclose/fgets/fgetc/fputc/fputs/fread/fwrite, printf/fprintf/
  sprintf/snprintf/sscanf, ungetc
- **stdlib**: malloc/free/calloc/realloc, atoi/atol/strtol/strtoul, getenv/setenv/unsetenv,
  qsort/bsearch, rand/srand, exit
- **string**: Full suite (memset/memcpy/memcmp/memmove, strlen/strcmp/strncmp/strcpy/strncpy/
  strcat/strchr/strrchr/strdup/strtok/strstr/strcasecmp/strncasecmp/strcspn/strspn, strerror)
- **ctype**: Character classification
- **unistd**: read/write/open/close/lseek/dup/dup2/pipe/vfork/waitpid/execve/getpid/
  chdir/mkdir/unlink/rename/sbrk/brk/isatty/getcwd/rmdir
- **fcntl**: O_RDONLY/O_WRONLY/O_RDWR/O_CREAT/O_TRUNC/O_APPEND/O_EXCL, fcntl
- **signal**: signal/kill, SIGHUP/SIGINT/SIGQUIT/SIGKILL/SIGPIPE/SIGTERM/SIGCHLD/
  SIGCONT/SIGSTOP/SIGTSTP
- **termios**: termios ioctls (TCGETS/TCSETS/TIOCGWINSZ)
- **dirent**: Directory reading
- **getopt**: Option parsing
- **regex**: Regular expression matching

**Not available:** setjmp/longjmp (needed by some shells for error recovery),
sigaction/sigprocmask (only simple signal() available), select/poll, mmap, fork
(only vfork+exec). These gaps are significant and would need to be filled for
most external shells.

---

## Candidate Shell Analysis

### 1. dash (Debian Almquist Shell)

**Source:** git://git.kernel.org/pub/scm/utils/dash/dash.git
**License:** BSD (3-clause)

| Metric | Value |
|--------|-------|
| Source lines | ~13,000 SLOC |
| Binary size (x86-64) | ~100-134 KB |
| Binary size (m68k est.) | ~80-120 KB .text |
| .data + .bss (est.) | ~4-8 KB |
| History/editing | Optional, requires libedit |
| Job control | Yes (full POSIX) |
| Scripting | Full POSIX (if/then/else, for, case, functions) |

**Strengths:**
- Gold standard for minimal POSIX shell
- Clean, well-maintained codebase (active development since 2002)
- Full job control support
- Full POSIX scripting
- Relatively small .data/.bss footprint -- ash-derived shells use a custom
  stack-based allocator (`STACKALLOC`) that minimizes heap/BSS usage
- No external dependencies beyond libc (libedit is optional)
- BSD licensed

**Weaknesses:**
- History/line editing requires libedit library (an additional ~15 KB .text,
  ~2-3 KB .data/.bss), which would need to be ported to Genix
- Without libedit, no arrow key history -- the shell is basically non-interactive
- Relies on `setjmp`/`longjmp` for error recovery (must be added to Genix libc)
- Uses `fork()` internally for subshells/command substitution -- would need
  conversion to vfork()+exec() or careful stubbing
- Uses `sigaction`/`sigprocmask` which Genix lacks (could be shimmed)
- Moderate porting effort due to libc gaps

**RAM Assessment:** With XIP, .text lives in ROM. The .data + .bss of dash itself
is modest (~4-8 KB), but adding libedit for history pushes it up. Heap usage depends
on script complexity. For interactive use with simple commands, total RAM overhead
would be approximately 8-12 KB, leaving ~16 KB for the command being executed.

---

### 2. mksh (MirBSD Korn Shell)

**Source:** https://github.com/MirBSD/mksh
**License:** MirOS License (permissive, ISC-like)

| Metric | Value |
|--------|-------|
| Source files | ~19 .c files |
| Source lines | ~25,000-30,000 SLOC (estimated) |
| Binary size (x86-64) | ~280 KB (dynamic), ~140-200 KB (static/stripped) |
| Binary size (m68k est.) | ~150-250 KB .text |
| .data + .bss (est.) | ~8-15 KB |
| History/editing | Built-in (emacs and vi modes) |
| Job control | Yes (full) |
| Scripting | Full Korn shell (superset of POSIX) |

**Strengths:**
- **Built-in line editing** with emacs and vi modes -- no external library needed
- **Built-in command history** with arrow key navigation
- Full job control (fg, bg, jobs, &, Ctrl-Z)
- Default shell on Android -- proven on embedded/resource-constrained systems
- Active, mature codebase with good portability focus
- Co-process support (`|&`)
- UTF-8 support (can be disabled)
- Extensively tested and audited

**Weaknesses:**
- Larger codebase than dash (~2x source lines)
- Higher .data/.bss usage due to built-in editing state, history buffers,
  and richer variable/alias tables
- More libc dependencies (needs more POSIX APIs)
- Uses `fork()` for subshells
- Needs `setjmp`/`longjmp`, `sigaction`/`sigprocmask`
- The `mksh-static` variant exists for small footprint but still larger than dash
- More complex to port due to richer feature set

**RAM Assessment:** The built-in editor is a major advantage (no separate libedit),
but mksh's own .data/.bss is larger. History buffer alone (default HISTSIZE=500)
consumes several KB. Estimated total RAM: 12-20 KB for shell state, which is
borderline for 28 KB user space if a non-trivial program also needs to run.

---

### 3. BusyBox ash

**Source:** https://git.busybox.net/busybox/
**License:** GPL v2

| Metric | Value |
|--------|-------|
| Source lines | ~15,000 SLOC (shell/ash.c alone is very large) |
| Binary size addition | ~60 KB (in BusyBox context, x86) |
| .data + .bss (est.) | ~4-8 KB |
| History/editing | Built-in (BusyBox line editing module) |
| Job control | Yes (configurable) |
| Scripting | Full POSIX (configurable) |

**Strengths:**
- Extremely configurable -- every feature can be enabled/disabled at compile time
- Built-in line editing with history (part of BusyBox `lineedit.c`)
- Based on dash/ash lineage (same clean design)
- Job control available as a config option
- Huge embedded user base (default shell in most embedded Linux)
- BusyBox's line editing module is self-contained and battle-tested

**Weaknesses:**
- **GPL v2 license** -- viral license, all linked code must be GPL-compatible
- Deeply integrated with BusyBox framework (`applet` infrastructure, shared
  `libbb` utility library) -- extracting ash standalone is non-trivial
- The standalone extraction loses BusyBox's size advantages
- Same libc gaps as dash (fork, setjmp, sigaction)
- ash.c is a single enormous file (~15K lines) which is hard to maintain
- Extracting the line editing module requires porting `libbb` helpers

**RAM Assessment:** Similar to dash when extracted. BusyBox's lineedit module adds
~3-5 KB for editing state. Configurable features allow trimming. Total: ~8-15 KB.

---

### 4. BusyBox hush

**Source:** https://git.busybox.net/busybox/
**License:** GPL v2

| Metric | Value |
|--------|-------|
| Source lines | ~10,000 SLOC (originally much smaller, grown over time) |
| Binary size addition | ~18 KB (2001), ~30-40 KB (modern, x86) |
| .data + .bss (est.) | ~2-5 KB |
| History/editing | Uses BusyBox lineedit (shared with ash) |
| Job control | Partial (historically buggy) |
| Scripting | Bourne grammar (if/then/else, for, case -- modern versions) |

**Strengths:**
- Smallest BusyBox shell option
- Lower baseline RAM usage than ash
- Modern hush has grown to support most Bourne shell grammar
- Uses recursive descent parser (simpler than ash's yacc-like parser)

**Weaknesses:**
- **GPL v2 license**
- Job control has historically been problematic ("also currently has a problem
  with job control" -- BusyBox docs)
- Same BusyBox extraction problems as ash
- Less POSIX-compliant than ash/dash
- Less mature/tested than ash for complex scripts
- Single global state structure -- fragile for error recovery

**RAM Assessment:** The smallest option: ~5-10 KB total RAM for shell state.
But job control reliability is a concern.

---

### 5. oksh (Portable OpenBSD ksh)

**Source:** https://github.com/ibara/oksh
**License:** Public Domain (core) / BSD-ISC (portability files)

| Metric | Value |
|--------|-------|
| Source lines | ~15,000-20,000 SLOC (estimated) |
| Binary size (x86-64) | ~264 KB |
| Binary size (m68k est.) | ~150-200 KB .text |
| .data + .bss (est.) | ~8-12 KB |
| History/editing | Built-in (emacs and vi modes) |
| Job control | Yes (full) |
| Scripting | Korn shell (POSIX superset) |

**Strengths:**
- Built-in line editing (emacs and vi modes)
- Built-in history with arrow key navigation
- Full job control
- Public domain licensing (maximally permissive)
- Aims for maximal portability
- OpenBSD security auditing heritage
- Self-contained (no ncurses required, though it uses it if found)
- C99 is sufficient to build

**Weaknesses:**
- Larger than dash
- Similar libc dependency issues (fork, sigaction, setjmp)
- Less embedded-focused than mksh (mksh is the actively-developed pdksh successor)
- Fewer embedded system deployments than mksh
- The OpenBSD codebase assumes a more complete POSIX environment

**RAM Assessment:** Similar to mksh. Built-in editing and history take ~8-12 KB.
With XIP, .text in ROM is fine, but RAM is tight at 12-18 KB for shell state.

---

### 6. sash (Stand-Alone Shell)

**Source:** https://github.com/multishell/sash
**License:** Public Domain

| Metric | Value |
|--------|-------|
| Source lines | ~3,000-5,000 SLOC |
| Binary size | Small (designed for rescue/recovery) |
| .data + .bss (est.) | ~2-4 KB |
| History/editing | None |
| Job control | None |
| Scripting | Minimal (no Bourne grammar) |

**Strengths:**
- Very small and self-contained
- Statically linked with built-in utilities
- Public domain
- Minimal dependencies

**Weaknesses:**
- **No command history**
- **No line editing**
- **No job control**
- **No scripting** (no if/then, no loops, no functions)
- Designed as emergency recovery shell, not interactive shell
- Fails to meet almost all requirements
- Last updated long ago, essentially unmaintained

**RAM Assessment:** Very low (~3-5 KB), but missing critical features makes
this irrelevant.

**Verdict:** Does not meet requirements. Eliminated.

---

### 7. FUZIX sh (V7 Bourne Shell port)

**Source:** https://github.com/EtchedPixels/FUZIX/tree/master/Applications/V7/cmd/sh
**License:** Caldera (ancient Unix heritage, open source)

| Metric | Value |
|--------|-------|
| Source files | ~21 .c files |
| Source lines | ~5,000-8,000 SLOC (estimated) |
| Binary size (m68k) | ~20-40 KB .text (already targets 68000) |
| .data + .bss (est.) | ~2-4 KB |
| History/editing | None (separate `fsh` adds editing) |
| Job control | None (V7 Bourne shell predates job control) |
| Scripting | V7 Bourne (if/then/else, for, case, functions) |

**Strengths:**
- **Already targets the 68000** -- has a Makefile.68000, known to build and run
- Proven on extremely constrained systems (Z80 with 32 KB user space)
- Lowest RAM usage of any real shell -- designed for 64 KB total RAM systems
- Custom memory allocator (`stak.c`) avoids malloc entirely
- Minimal libc requirements -- almost everything is self-contained
- No fork dependency -- FUZIX uses a bank-switching exec model similar to vfork
- The codebase is well-understood within the FUZIX ecosystem
- Caldera license is permissive

**Weaknesses:**
- **No command history**
- **No line editing**
- **No job control** (the V7 Bourne shell predates POSIX job control)
- Ancient codebase with archaic C style (pre-ANSI, though "ANSIfied for FUZIX")
- FUZIX has a separate `fsh` ("sh with editing") but it is not well documented
  and may add significant complexity
- No arrow key navigation
- Fails to meet the history and job control requirements

**RAM Assessment:** Excellent -- 3-6 KB total. The best option for pure RAM
efficiency. But missing critical interactive features.

---

### 8. Other Candidates Considered

#### yash (Yet Another Shell)
- Full POSIX with extensions, ~30K SLOC
- Too large, too many dependencies
- Eliminated

#### rc (Plan 9 shell)
- Clean, small (~5K SLOC), but non-POSIX syntax
- No job control in most implementations
- Eliminated (POSIX compatibility requirement)

#### s (suckless shell / execline)
- Extremely minimal, but no interactive features
- Eliminated

#### Custom shell (built from scratch)
- Could be tailored exactly to Genix's constraints
- Would combine FUZIX sh's proven 68000 foundation with a lightweight
  line editing module
- High development effort but perfect fit
- Discussed in recommendation section

---

## Comparative Summary

| Shell | .text (ROM) | .data+.bss (RAM) | History | Job Control | Scripting | License | Porting Effort |
|-------|-------------|-------------------|---------|-------------|-----------|---------|----------------|
| dash | ~100 KB | ~6 KB | libedit only | Full | Full POSIX | BSD | Medium |
| dash + libedit | ~115 KB | ~9 KB | Yes | Full | Full POSIX | BSD | Medium-High |
| mksh | ~200 KB | ~12 KB | Built-in | Full | Korn | MirOS | High |
| BB ash (extracted) | ~100 KB | ~8 KB | Built-in | Full | Full POSIX | GPL v2 | High |
| BB hush (extracted) | ~60 KB | ~5 KB | Built-in | Partial | Bourne | GPL v2 | High |
| oksh | ~180 KB | ~10 KB | Built-in | Full | Korn | PD/BSD | High |
| sash | ~30 KB | ~3 KB | No | No | No | PD | Low |
| FUZIX sh | ~30 KB | ~3 KB | No | No | V7 Bourne | Caldera | Low |
| FUZIX sh + custom edit | ~40 KB | ~5 KB | Yes | No | V7 Bourne | Mixed | Medium |

(All m68k sizes are estimates. With XIP, .text lives in ROM and is effectively free.)

---

## Key Technical Challenges for Any External Shell

### 1. fork() vs vfork()

Every traditional Unix shell uses `fork()` for subshells, command substitution
(`$(cmd)`), and background jobs. Genix has no MMU and no `fork()` -- only
`vfork()` + `exec()`. This is the single largest porting challenge.

**Impact by feature:**
- **Simple command execution:** Uses exec directly, no fork needed. Works now.
- **Pipelines:** Need concurrent processes. Currently Genix runs them sequentially
  (each stage runs to completion). True concurrent pipelines need a way to have
  multiple processes in memory simultaneously, which requires XIP or bank switching.
- **Command substitution** `$(cmd)`: Requires fork+pipe+exec. Can be worked around
  with vfork if the child only does exec.
- **Subshells** `(cmd1; cmd2)`: Traditionally forked. Can be simulated.
- **Background jobs** `cmd &`: Requires concurrent process execution. Possible
  with XIP (multiple processes, .text in ROM, separate .data in RAM).

### 2. Missing libc Functions

Most shells need:
- `setjmp`/`longjmp` -- for error recovery (shell must not exit on errors)
- `sigaction`/`sigprocmask` -- for signal handling during job control
- `fcntl` with `F_DUPFD` -- for file descriptor manipulation
- `tcsetpgrp`/`tcgetpgrp` -- for job control terminal management
- `getpgrp`/`setpgid` -- for process group management

Genix currently lacks all of these except basic `fcntl` (stub returning 0).
Adding setjmp/longjmp is straightforward (FUZIX has a 68000 implementation).
The signal and process group functions require kernel work.

### 3. Terminal Control for Job Control

Full POSIX job control requires:
- Process groups (kernel has `pgrp` field, but `setpgid`/`getpgrp` syscalls
  may need work)
- Foreground process group on the terminal (`tcsetpgrp`/`tcgetpgrp`)
- SIGTTIN/SIGTTOU for background terminal access
- The shell must be the session leader

Genix has partial infrastructure (process groups, SIGTSTP/SIGCONT), but the
terminal-side job control (tcsetpgrp, SIGTTIN/SIGTTOU) needs implementation.

---

## Recommendation

### Primary Recommendation: dash + custom line editing module

**dash** is the best foundation for a Genix shell, for these reasons:

1. **Smallest real POSIX shell.** At ~13K SLOC, it is the most minimal shell
   that still provides full POSIX scripting, job control, and I/O redirection.

2. **Clean, modern codebase.** Actively maintained, well-structured, BSD licensed.
   No GPL concerns.

3. **Proven ash heritage.** The Almquist shell lineage (ash -> dash, BusyBox ash)
   is the dominant shell in embedded Linux. The design has been battle-tested for
   35+ years.

4. **Low RAM usage.** The ash-family stack allocator keeps .data/.bss small.
   With XIP, the ~100 KB .text lives in ROM for free.

5. **Job control is built in.** Unlike FUZIX sh, dash has full POSIX job control.

6. **Modular line editing.** Rather than porting libedit (which has its own
   dependencies), implement a small custom line editing module (~500-1000 lines)
   that provides:
   - Arrow key history navigation (up/down)
   - Basic line editing (left/right, backspace, delete, home/end)
   - A circular history buffer (e.g., 16 entries of 128 bytes = 2 KB RAM)
   - Raw terminal mode via termios (already supported by Genix)

   This is far simpler than porting libedit and gives exactly the features needed.

**Porting plan for dash:**

1. Add `setjmp`/`longjmp` to Genix libc (copy from FUZIX's `setjmp_68000.S`)
2. Add `sigaction` shim (wrapper around `signal()` for basic cases)
3. Add `sigprocmask` stub (Genix is single-tasking from shell's perspective)
4. Replace `fork()` calls with `vfork()`+`exec()` where possible, stub the rest
5. Add `tcsetpgrp`/`tcgetpgrp` syscalls to kernel (needed for job control)
6. Add `setpgid`/`getpgrp` syscalls to kernel
7. Build dash with `--without-libedit`, write custom line editor
8. Test incrementally: first get basic command execution working, then scripting,
   then job control, then line editing

### Alternative Recommendation: mksh

If the porting effort for dash proves too high, or if richer interactive features
are desired (emacs/vi editing modes, tab completion, aliases), **mksh** is the
second choice:

- Built-in line editing eliminates the need for a custom module or libedit
- Default shell on Android proves it works on constrained systems
- MirOS license is permissive
- Larger RAM footprint (~12-15 KB .data/.bss) is the main concern

mksh would require the same kernel additions (setpgid, tcsetpgrp, etc.) plus
more libc functions. The porting effort is higher but the result is a more
feature-rich interactive shell.

### Fallback: Incremental enhancement of current shell

If porting an external shell proves impractical in the short term:

1. Move the current built-in shell to userspace (apps/sh.c)
2. Add a simple line editing module with history (same custom module as above)
3. Add basic variable support ($VAR, export)
4. Add simple scripting (if/then/fi, for/do/done)
5. Add job control incrementally as kernel support matures

This is the lowest-risk path but results in a non-standard shell that requires
ongoing custom development.

### NOT Recommended

- **BusyBox ash/hush**: GPL v2 license is problematic. Extraction from BusyBox
  framework is complex. If you wanted an ash-derived shell, just use dash directly.
- **oksh**: Similar to mksh but less actively maintained and less embedded-focused.
  If you want a Korn shell, mksh is the better choice.
- **sash**: Missing all required interactive features.
- **FUZIX sh**: No history, no job control. Its only advantage (proven 68000
  compatibility) is less relevant now that Genix has a working cross-compilation
  toolchain and libc.

---

## Implementation Priorities

Regardless of which shell is chosen, the kernel and libc need these additions:

### Must Have (for any external shell)

1. `setjmp`/`longjmp` in libc (copy FUZIX's `setjmp_68000.S`)
2. `setpgid`/`getpgrp` syscalls
3. `tcsetpgrp`/`tcgetpgrp` syscalls
4. `SIGTTIN`/`SIGTTOU` signals
5. `sigaction` (at minimum a shim over `signal()`)

### Should Have (for job control)

6. `sigprocmask` (even a basic implementation)
7. Background process support (requires XIP or bank switching so multiple
   processes can coexist in memory)
8. `wait()` (wait for any child, not just specific PID)

### Nice to Have (for full POSIX shell)

9. `fcntl` with `F_DUPFD`, `F_GETFL`, `F_SETFL`
10. `sigsetjmp`/`siglongjmp`
11. Here-documents (require temp files or pipe tricks)

---

## RAM Budget Analysis

With XIP, .text is in ROM. The critical constraint is RAM:

```
Available user RAM:          ~28 KB (28,672 bytes)
Shell .data + .bss (dash):   ~6 KB
Line editor + history:       ~3 KB (16 entries x 128 bytes + state)
Shell heap (runtime):        ~4 KB (parse trees, variables, etc.)
Shell stack:                 ~2 KB
                            ------
Shell total RAM:            ~15 KB

Remaining for child process: ~13 KB (.data + .bss + heap + stack)
```

This is tight but workable. The current Genix apps (hello, echo, cat, etc.)
use very little RAM. More complex programs (grep, levee) need more, but 13 KB
of .data/.bss/.heap/.stack should suffice for most utilities when their .text
is also XIP.

If mksh is chosen instead of dash, the shell takes ~18 KB, leaving only ~10 KB
for child processes. This is marginal.

---

## Conclusion

**dash + a custom ~800-line line editing module** provides the best balance of:
- Full POSIX shell compliance (scripting, job control, redirection)
- Minimal RAM footprint (~15 KB total)
- Clean BSD-licensed codebase
- Proven embedded heritage (ash family)
- Arrow key history navigation (via custom module)
- Reasonable porting effort

The custom line editing module avoids the libedit dependency entirely and can be
tailored to Genix's exact terminal capabilities (VDP console on Mega Drive,
UART on workbench). It is also reusable by other interactive programs.
