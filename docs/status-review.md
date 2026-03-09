# Genix Status Review — Plan vs. Reality

_Review date: 2026-03-09_

This document compares the original PLAN.md (from the FUZIX repo) against
what Genix actually implemented, identifies divergences, assesses their
impact on porting Fuzix apps, and lays out the path forward.

---

## 1. Plan vs. Reality — Phase by Phase

### Phase 1: Workbench Emulator — COMPLETE, matches plan

The plan called for a Musashi-based 68000 SBC with UART, timer, and disk.
This is exactly what was built. No meaningful divergence.

### Phase 2a: Single-tasking Kernel — COMPLETE, matches plan

Boot, console I/O, minifs filesystem, exec(), built-in shell — all done.
The kernel is ~3000 lines as planned. minifs matches the planned on-disk
format closely (48-byte inodes instead of 64-byte, but the same concept).

### Phase 2b: Multitasking — COMPLETE, diverged from plan

**What the plan said:**
- Process table (8–16 slots), round-robin preemptive scheduling via timer
- vfork()+exec() as the process creation model
- PIC ELF or bFLT binary format for position-independent loading

**What was actually built:**
- Process table (16 slots) — matches
- `spawn()` (combined vfork+exec) instead of separate vfork()/exec() — **divergence**
- Cooperative scheduling, not preemptive — **divergence**
- Genix flat binary (fixed load address) instead of PIC/bFLT — **divergence**

**Why it diverged:** The return-twice semantics of vfork() crashed on the
68000 stack. `spawn()` was pragmatic — it works for the built-in shell. But
it's not POSIX, so ported programs that do `vfork(); exec()` or
`fork(); exec()` won't work without modification.

**Impact:** Medium. Most Fuzix utilities don't fork — they're simple
filter programs. But `sh`, `make`, and anything that spawns subprocesses
will need the real vfork() or a posix_spawn() wrapper.

### Phase 2c: Pipes — COMPLETE, partially diverged

**What the plan said:** pipe(), with blocking I/O.

**What was built:** pipe() with non-blocking semantics (returns immediately
if empty/full). This works for single-command pipelines in the built-in
shell but won't work for real shell pipelines like `cat file | grep foo`
where the reader must block waiting for the writer.

**Impact:** High for ported apps. Blocking pipes require preemptive
scheduling (Phase 2b divergence compounds here).

### Phase 2d: Signals — NOT STARTED

**What the plan said:** kill(), signal()/sigaction() for basic signal handling.

**What exists:** Stubs only. `SYS_SIGNAL` stores a handler pointer but
never delivers. `SYS_KILL` returns -ENOSYS.

**Impact:** Medium. Most simple utilities don't use signals. But any
program that catches SIGINT (Ctrl+C), or any shell that does job control,
needs this.

### Phase 2e: TTY Subsystem — NOT STARTED

Console I/O works (cooked and raw mode via termios ioctl), but there's no
proper TTY layer. No line discipline, no job control signals, no /dev/ttyN.

**Impact:** Low for simple utilities. Levee already works with what exists.
Needed for proper shell job control and multi-terminal support.

### Phase 2f: Fuzix libc + Utilities — NOT STARTED

This is the app-porting phase. The plan called for using newlib. Genix
instead built a custom minimal libc. This is actually fine — see Section 3.

### Phase 3: Mega Drive Port — COMPLETE, matches plan

VDP text output, Saturn keyboard, SRAM, ROM disk — all working. The PAL
abstraction works exactly as planned. BlastEm tests pass.

### Phase 4: Polish — NOT STARTED

Interrupt-driven keyboard, /dev/null, multi-TTY — all planned, none done.

---

## 2. Key Divergences Summary

| Area | Plan | Reality | Severity |
|------|------|---------|----------|
| Process creation | vfork()+exec() | spawn() only | **High** |
| Scheduling | Preemptive (timer) | Cooperative | **High** |
| Binary format | PIC ELF / bFLT | Fixed-address flat | Medium |
| Pipe blocking | Blocking I/O | Non-blocking | **High** |
| C library | newlib | Custom minimal libc | Low (better) |
| Signals | Basic delivery | Stubs only | Medium |
| TTY | Full subsystem | Minimal cooked/raw | Low |

The three **High** items (vfork, preemptive scheduling, blocking pipes)
are all interconnected. You can't have blocking pipes without preemptive
scheduling, and you can't port programs that spawn children without
vfork()+exec().

---

## 3. Impact on Porting Fuzix Apps

### What CAN be ported today (no kernel changes needed)

Simple filter programs that read stdin, process, write stdout:

- **Already done:** hello, echo, cat, wc, head, true, false, levee
- **Easy ports (~1 hour each):** tail, tee, yes, sleep, basename, dirname,
  od, rev, strings, nl, fold, paste, expand, unexpand, comm, cmp

These programs need only: read/write/open/close/lseek/stat/exit and
basic libc (stdio, string, stdlib). Genix has all of this.

### What CANNOT be ported until kernel work is done

| Program | Missing Feature | Blocker |
|---------|----------------|---------|
| sh (any real shell) | vfork+exec, signals, blocking pipes | Phase 2d work |
| grep | Works, but needs getopt() in libc | Libc gap |
| sort | Large memory, vfork for -o flag | May work without -o |
| find | opendir/readdir (SYS_GETDENTS) | Syscall gap |
| ls -l | stat() works, but needs getdents for dirs | Syscall gap |
| make | vfork+exec, pipes, signals | Phase 2d work |
| sed | regex library | Libc gap |
| ed | signals, terminal control | Phase 2d-2e |
| ps | /proc or kernel query syscall | Needs new syscall |
| kill | SYS_KILL not implemented | Phase 2d work |
| tar | Large memory, may need SRAM | Memory constraint |

### Libc gaps for porting

Genix libc has: stdio (FILE*, fopen, fgets, fprintf, puts), stdlib
(malloc, atoi, exit, qsort), string, ctype, termios, unistd.

**Missing for Fuzix apps:**
- `getopt()` / `getopt_long()` — needed by almost every utility
- `opendir()` / `readdir()` / `closedir()` — needs SYS_GETDENTS
- `sprintf()` / `snprintf()` — currently only fprintf to a FILE*
- `sscanf()` / `scanf()` — not implemented
- `strtol()` / `strtoul()` — needed by many utilities
- `perror()` / `strerror()` — error reporting
- `glob()` — shell globbing
- `regex` — needed by grep, sed
- `time()` / `localtime()` / `strftime()` — timestamps
- `getenv()` / `setenv()` — environment variables (getenv exists but returns NULL)
- `isatty()` — terminal detection

---

## 4. C Library Strategy: Custom libc vs. Newlib vs. Fuzix libc

The plan called for newlib. Genix chose a custom libc. Here's the assessment:

**Newlib** (original plan):
- Pro: Complete stdio, stdlib, string, math — everything "just works"
- Con: Large (~200 KB .text), designed for 32-bit systems with more RAM
- Con: Needs ~15 syscall stubs that must exactly match newlib's expectations
- Verdict: Too big for Mega Drive's 64 KB. Would work on workbench only.

**Fuzix libc** (plan phase 2f):
- Pro: Designed for tiny systems (Z80, 6502, 68000), small footprint
- Pro: Already has 68000 support, already tested with Fuzix apps
- Pro: All Fuzix apps are written against it — zero porting friction
- Con: Tightly coupled to Fuzix syscall numbers and conventions
- Verdict: Best option for porting Fuzix apps, needs a syscall shim.

**Custom Genix libc** (current):
- Pro: Tiny, we control everything, proven to work
- Pro: Already supports levee (non-trivial real app)
- Con: Missing many functions needed by ported apps
- Verdict: Keep as foundation, incrementally add what's needed.

### Recommendation: Grow the Genix libc, borrowing from Fuzix libc

Don't wholesale replace — incrementally pull functions from Fuzix libc
into Genix libc as apps need them. The syscall stubs stay Genix-native.
The library functions (getopt, strtol, regex, etc.) are pure C and can
be copied directly.

This gives us:
1. No ABI/syscall compatibility headaches
2. Incremental, testable growth
3. Each new function is immediately useful for a concrete app port

---

## 5. Plan to Finish Remaining Phases

### Phase 2d-LITE: Minimum viable vfork + signals (prerequisite for apps)

This is the critical blocker. Without vfork()+exec(), we can't port any
program that spawns children (shells, make, find -exec, etc.).

**Step 1: Implement real vfork()**
- Save parent's full register set (d0-d7, a0-a7, sr, pc) in proc table
- Mark parent P_VFORK (sleeping), switch to child
- Child shares parent memory (stack + heap)
- On child's exec() or _exit(), restore parent from saved state
- The vfork stub in libc already handles the return-address trick

**Step 2: Make exec() work from vfork'd child**
- exec() must detect it's in a vfork'd child
- Load new binary, set up new stack, wake parent, jump to new program
- Parent resumes where vfork() returned, with child's PID in d0

**Step 3: Preemptive scheduling (timer-driven)**
- Workbench: use the existing timer interrupt (SIGALRM-based)
- Mega Drive: use VBlank interrupt (50/60 Hz)
- Timer ISR calls scheduler() if current process has run long enough
- This automatically fixes blocking pipes — sleeping processes get woken

**Step 4: Blocking pipe I/O**
- pipe_read(): if buffer empty and writers exist, sleep (P_SLEEPING)
- pipe_write(): if buffer full and readers exist, sleep
- Wake sleeping readers/writers when data arrives or space frees

**Step 5: Basic signals**
- Process flag field for pending signals
- Default handlers: SIGTERM→exit, SIGKILL→exit, SIGINT→exit
- signal() stores user handler; delivery on return from kernel
- kill() sets the flag; scheduler delivers on context switch

### Phase 2e-LITE: Minimal TTY improvements

- Ctrl+C generates SIGINT to foreground process (needs signals first)
- isatty() syscall (or just hardcode fd 0/1/2 as terminal)
- /dev/null (trivially: read returns 0, write discards)

### Phase 2f: App porting (the main event)

See Section 6 below.

---

## 6. App Porting Strategy and Build Organization

### Directory structure

```
genix/
├── apps/                    # Genix-native programs (current)
│   ├── hello.c
│   ├── echo.c
│   ├── cat.c
│   ├── ...
│   ├── crt0.S
│   ├── user.ld
│   ├── user-md.ld
│   └── Makefile
├── apps/ports/              # Ported Fuzix utilities
│   ├── grep.c              # Copied + adapted from Fuzix
│   ├── ls.c
│   ├── sort.c
│   ├── ...
│   └── Makefile            # Same build pattern as apps/
└── libc/                    # Grows as apps need functions
    ├── getopt.c            # Pulled from Fuzix libc
    ├── strtol.c
    ├── ...
```

**Alternative (simpler):** Just put everything in `apps/`. The Fuzix
utilities are simple standalone .c files. There's no real need for a
subdirectory — it's one .c file per program. Add them to the PROGRAMS
list in apps/Makefile and they build automatically.

### Recommendation: Flat apps/ directory

```
apps/Makefile:
PROGRAMS = hello echo cat wc head true false \
           tail tee yes sleep grep sort ls find sed ...
```

Each program is one .c file. The existing build system (crt0 + libc +
mkbin) handles everything. No new Makefiles, no new build steps.

For multi-file programs (like levee), keep a subdirectory with its own
Makefile, as is already done for levee.

### Porting tiers

**Tier 0 — No kernel changes needed, minimal libc additions (do first):**

Add `getopt.c`, `strtol.c`, `perror.c` to libc, then port:

| Program | Source | Notes |
|---------|--------|-------|
| tail | Fuzix util/tail.c | read + lseek |
| tee | Fuzix util/tee.c | read + write to multiple fds |
| yes | Fuzix util/yes.c | trivial |
| sleep | Fuzix util/sleep.c | needs time() or busy-wait |
| basename | Fuzix util/basename.c | pure string manipulation |
| dirname | Fuzix util/dirname.c | pure string manipulation |
| od | Fuzix util/od.c | read + formatted output |
| strings | Fuzix util/strings.c | read + filter |
| rev | Fuzix util/rev.c | line reversal |
| nl | Fuzix util/nl.c | line numbering |
| cmp | Fuzix util/cmp.c | byte-by-byte file compare |
| cut | Fuzix util/cut.c | field extraction |
| tr | Fuzix util/tr.c | character translation |
| uniq | Fuzix util/uniq.c | duplicate line filter |
| paste | Fuzix util/paste.c | merge file lines |
| env | Fuzix util/env.c | print/set environment |

**Tier 1 — Needs SYS_GETDENTS + opendir/readdir in libc:**

| Program | Source | Notes |
|---------|--------|-------|
| ls | Fuzix util/ls.c | Directory listing, stat() |
| find | Fuzix util/find.c | Directory traversal |
| rmdir | Already a kernel builtin | Move to user program |
| du | Fuzix util/du.c | Directory size |
| mkdir | Already a kernel builtin | Move to user program |

**Tier 2 — Needs vfork+exec (Phase 2d-LITE):**

| Program | Source | Notes |
|---------|--------|-------|
| sh | Write minimal or port Fuzix sh | The big one |
| xargs | Fuzix util/xargs.c | Needs exec |
| time | Fuzix util/time.c | Needs vfork+exec+waitpid |
| nohup | Fuzix util/nohup.c | Needs signals |

**Tier 3 — Needs regex library:**

| Program | Source | Notes |
|---------|--------|-------|
| grep | Fuzix util/grep.c | Core utility |
| sed | Fuzix MWC/cmd/sed.c | Stream editor |
| expr | Fuzix MWC/cmd/expr.y | Needs yacc output |

**Tier 4 — Nice to have, bigger programs:**

| Program | Source | Notes |
|---------|--------|-------|
| ed | Fuzix Applications/ue or MWC/cmd/ed.c | Line editor |
| make | Fuzix MWC/cmd/make.c | Build system |
| ar | Fuzix util/ar.c | Archive tool |
| diff | Fuzix util/diff.c | File comparison |
| patch | Large, needs careful porting | |

### Build process changes needed

**Minimal changes to apps/Makefile:**

1. Add ported .c files to PROGRAMS list
2. Some programs need `-I ../libc/include` (already the case)
3. Some multi-file programs (like levee) get their own subdirectory
4. No other build system changes — the existing pattern rules handle it

**Libc additions** (in order of priority):

1. `getopt.c` — almost every utility needs this
2. `strtol.c` / `strtoul.c` / `atol.c` — numeric conversion
3. `perror.c` / `strerror.c` — error messages
4. `opendir.c` / `readdir.c` / `closedir.c` — directory reading (needs SYS_GETDENTS)
5. `sprintf.c` / `snprintf.c` — string formatting (extend existing fprintf)
6. `isatty.c` — terminal detection
7. `regex.c` — regular expressions (for grep, sed)
8. `glob.c` — filename globbing (for shell)
9. `time.c` / `ctime.c` — time formatting

### What to copy from Fuzix vs. write fresh

**Copy from Fuzix libc** (pure C, no Fuzix dependencies):
- getopt.c, strtol.c, regex.c, glob.c, qsort.c — library functions
- Most utility programs themselves — they're standalone .c files

**Write fresh** (Genix-specific):
- opendir/readdir/closedir — wraps SYS_GETDENTS
- isatty — wraps SYS_IOCTL
- Syscall stubs — already Genix-native

**Adapt from Fuzix** (needs minor changes):
- Programs that use Fuzix-specific headers: change `#include` paths
- Programs that call fork(): change to vfork()+exec() or spawn()
- Programs that assume Fuzix errno globals: use Genix return convention

---

## 7. Recommended Execution Order

```
NOW (no kernel changes):
  1. Add getopt.c, strtol.c, perror.c to libc
  2. Port Tier 0 utilities (tail, tee, yes, basename, etc.)
  3. Add to AUTOTEST to validate on Mega Drive
  4. Implement SYS_GETDENTS + opendir/readdir
  5. Port ls, find (Tier 1)

NEXT (kernel work — Phase 2d-LITE):
  6. Implement real vfork() in kernel
  7. Implement preemptive scheduling (timer interrupt)
  8. Make pipes blocking
  9. Basic signals (SIGINT, SIGTERM, SIGKILL)
 10. Port/write a real shell (Tier 2)

THEN (library enrichment):
 11. Add regex library to libc
 12. Port grep, sed (Tier 3)
 13. Add remaining libc functions as apps demand them
 14. Port ed, make, diff (Tier 4)

ONGOING:
 - Every ported app gets a host test if possible
 - Every ported app gets added to AUTOTEST
 - Verify on Mega Drive (make test-md-auto) after each batch
```

---

## 8. Mega Drive Considerations for Ported Apps

The Mega Drive has ~31 KB for user programs (0xFF8000–0xFFFE00). Most
simple Fuzix utilities compile to 4–8 KB, so they fit comfortably. But:

- **levee** (44 KB) already doesn't fit on Mega Drive — workbench only
- **grep with regex** may be tight depending on regex library size
- **make, ed, sed** are borderline — need measurement
- Programs are loaded one-at-a-time from ROM disk, so total ROM size is
  not a constraint (ROM can be up to 4 MB)

**Rule of thumb:** If a program's .text + .data + .bss + stack exceeds
~28 KB, it's workbench-only (or needs SRAM for extended user RAM).

The apps/Makefile already handles separate builds for workbench (linked
at 0x040000) and Mega Drive (linked at 0xFF8000). The PROGRAMS list can
have platform-specific exclusions:

```makefile
PROGRAMS = hello echo cat wc head true false tail tee grep ls ...
# Programs too large for Mega Drive
PROGRAMS_WB_ONLY = levee
```
