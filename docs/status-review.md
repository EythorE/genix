# Genix Status Review — Plan vs. Reality (Second Review)

_Review date: 2026-03-10_
_Previous review: 2026-03-09_

This document compares the original PLAN.md (from the FUZIX repo) against
what Genix actually implemented, analyzes FUZIX design decisions vs. Genix's
divergences, identifies testing gaps, and lays out the path to completing
remaining phases and porting apps.

---

## 1. Progress Since Last Review

The last review (2026-03-09) identified three **High** severity gaps:
vfork/spawn, preemptive scheduling, and blocking pipes. It also noted
signals and TTY as not started. All five have been resolved:

| Gap (last review) | Status now | How resolved |
|-------------------|-----------|--------------|
| spawn() instead of vfork() | **Both exist** | Real vfork() implemented (setjmp/longjmp). spawn() kept as convenience. |
| Cooperative scheduling | **Preemptive** | Timer ISR calls schedule()+swtch(). S-bit check prevents kernel preemption. |
| Non-blocking pipes | **Blocking** | pipe_read/write sleep when empty/full, wake on data arrival. |
| Signals (stubs only) | **Complete** | 21 signals, user handlers, signal frames, sigreturn, SIGTSTP/SIGCONT. |
| TTY (not started) | **Complete** | Full line discipline: cooked/raw, echo, erase, kill, signal generation, OPOST. |

Additionally since the last review:
- **Phase 2c** (I/O redirection): Shell pipes (`|`), output redirect (`>`/`>>`), input redirect (`<`)
- **Phase 2d** (Signals): User signal handlers with 84-byte signal frames, process groups
- **Phase 2e** (TTY): 320-line line discipline, 78 host tests
- **22 user programs** (was 8 at last review)
- **615+ host tests** (was 283 at last review)
- **19+ autotest cases** on both platforms

The project has moved dramatically since the last review. The three
interconnected "High" blockers are all resolved.

---

## 2. Plan vs. Reality — Phase by Phase

### Phase 1: Workbench Emulator — COMPLETE, matches plan

The plan called for a Musashi-based 68000 SBC with UART, timer, and disk.
This is exactly what was built. No meaningful divergence.

### Phase 2a: Single-tasking Kernel — COMPLETE, matches plan

Boot, console I/O, minifs filesystem, exec(), built-in shell — all done.

### Phase 2b: Multitasking — COMPLETE, matches plan (now)

**What the plan said:** Process table (8-16 slots), round-robin preemptive
scheduling via timer, vfork()+exec().

**What was built:**
- Process table (16 slots) — matches
- Preemptive timer-driven scheduling — matches (was cooperative, now fixed)
- Per-process kernel stacks (512 bytes) — matches (not in plan but necessary)
- vfork() implemented (setjmp/longjmp) — matches
- spawn() as additional convenience — bonus, not in plan
- Genix flat binary (fixed load address) instead of PIC/bFLT — **divergence**

**Binary format divergence:** The plan called for PIC ELF or bFLT.
Genix uses a custom 32-byte flat binary at a fixed load address. This
means only one user process can be loaded at a time (they all map to
USER_BASE). This is the single biggest remaining architectural limitation.

### Phase 2c: Pipes — COMPLETE, matches plan

pipe() with blocking I/O, 512-byte circular buffer. SIGPIPE on broken
pipes. Shell-level pipe syntax (`|`). Sequential pipeline execution
(no-MMU limitation).

### Phase 2d: Signals — COMPLETE, matches plan

21 signals, user handlers, signal frames on user stack, sigreturn
trampoline, SIGTSTP/SIGCONT for job control, process groups, Ctrl+C
and Ctrl+Z from TTY.

### Phase 2e: TTY Subsystem — COMPLETE, matches plan

Full line discipline (320 lines), cooked/raw modes, echo/erase/kill,
signal generation, OPOST/ONLCR, termios ioctls, /dev/tty and /dev/console
device nodes, 78 host unit tests.

### Phase 2f: Fuzix libc + Utilities — PARTIALLY DONE

**What the plan said:** Use newlib for C library, port GNU tools.

**What was built:** Custom minimal libc (1900+ lines, 14 modules) with
stdio, stdlib, string, ctype, termios, getopt, sprintf, strtol, perror,
dirent, isatty. 22 user programs including levee (vi clone).

**Assessment:** The plan's newlib goal was wrong — newlib is 50-100 KB,
too large for the Mega Drive's 64 KB RAM. The custom libc is the right
choice. It's smaller, we control it, and it's proven (levee works).
Remaining gaps are documented in Section 7.

### Phase 3: Mega Drive Port — COMPLETE, matches plan

VDP text output, Saturn keyboard, SRAM, ROM disk. PAL abstraction works.
BlastEm tests pass. The Mega Drive drivers were reused from FUZIX as planned.

### Phase 4: Polish — PARTIALLY DONE

- /dev/null: **Done**
- Interrupt-driven keyboard: **Not done** (still polling)
- Multi-TTY: **Not done** (NTTY=1, structure supports more)

---

## 3. Divergences From Plan — Impact Assessment

### 3.1 Binary Format (Medium impact, plan to address)

**Plan:** PIC ELF or bFLT with relocation support.
**Reality:** Fixed-address flat binary ("GENX" header).

**Impact:** Only one user process in memory at a time. Pipelines execute
sequentially (echo runs, fills pipe buffer, exits; then cat runs and reads).
Pipelines producing >512 bytes of output lose data.

**Should we switch to Fuzix a.out?** The decisions.md planned this
migration. Analysis:

| Factor | Genix flat binary | Fuzix a.out |
|--------|-------------------|-------------|
| Header size | 32 bytes | 16 bytes |
| Relocation | None | Kernel-applied, ~30 lines C |
| Ecosystem | 22 custom apps | 143+ Fuzix utilities |
| Multiple processes in memory | No | Yes (with relocation) |
| Complexity | Trivial loader | Simple loader + reloc table |

**Recommendation:** Keep Genix flat binary for now. Add relocation support
later when we need concurrent user processes in memory. The 512-byte pipe
buffer limit is the real pain point, and it's a no-MMU fundamental
limitation regardless of binary format — two processes sharing the same
RAM can corrupt each other without an MMU.

For app porting, recompiling Fuzix utility source code against Genix
libc + Genix binary format is simpler than implementing Fuzix a.out
compatibility. The utilities are standalone .c files — just build them
with the Genix toolchain.

### 3.2 C Library (Low impact, divergence is beneficial)

**Plan:** newlib (standard bare-metal C library).
**Reality:** Custom minimal libc.

**This divergence is a win.** newlib would be 50-100 KB — it wouldn't
even fit in Mega Drive RAM. The custom libc is ~5 KB and growing
incrementally as apps need functions. It borrows pure-C functions from
Fuzix libc (getopt, strtol) without taking the syscall layer.

### 3.3 Process Creation Model (Low impact now, was High)

**Plan:** vfork()+exec() as sole model.
**Reality:** Both vfork()+exec() and spawn() exist.

spawn() was implemented first because vfork's return-twice semantics
crashed on the 68000 stack. Real vfork() was added later with per-process
kernel stacks. Both work. spawn() is still useful as a convenience for
the built-in shell. No negative impact.

### 3.4 No GNU Toolchain Target (Expected, not a problem)

**Plan Phase 4** aimed for GCC/binutils/make running on the Mega Drive.
This was always aspirational — GCC needs far more RAM than 64 KB.
Cross-compilation from a Linux host is the practical workflow. The plan
acknowledged this: "Native compilation is a stretch goal."

---

## 4. FUZIX Design Philosophy vs. Genix Divergences

### 4.1 What FUZIX Does That Genix Doesn't (and whether it matters)

| FUZIX feature | Genix status | Impact |
|---------------|-------------|--------|
| Multi-user (UIDs, permissions) | Deliberately omitted | None — single-user system |
| fork() | Deliberately omitted | Correct for no-MMU |
| 32 signals (Linux-compatible) | 21 signals | Sufficient for all current apps |
| CONFIG_LEVEL_2 job control | Basic (SIGTSTP/SIGCONT) | Adequate for single-terminal use |
| swap/banking memory | Not applicable | 68000 Mega Drive has flat memory |
| Multiple filesystem types | minifs only | Sufficient — simpler is better |
| ptys | Not implemented | Not needed (single console) |
| Network stack | Not implemented | Not planned (no hardware) |

**Verdict:** Genix correctly omits FUZIX features that don't apply to a
single-user, single-terminal, no-MMU system. The omissions reduce code
from ~15,000 lines to ~5,400 lines while keeping all features that
matter for running Unix utilities.

### 4.2 What FUZIX Does Better (and what Genix should learn)

**1. Circular buffer optimization (ADOPTED)**

FUZIX uses 256-byte circular buffers with `uint8_t` indices that wrap
naturally — no modulo, no division. Genix's TTY already uses this
pattern. Good.

**2. `tty_inproc()` ISR-safe character injection**

FUZIX's TTY is designed for interrupt-driven input: `tty_inproc()` can
be called from an ISR to inject a character into the input queue. Genix's
TTY also has this architecture (single-byte atomic writes to `inq_head`).
Ready for interrupt-driven keyboard when Phase 4 happens.

**3. Block buffer external allocation (CONFIG_BLKBUF_EXTERNAL)**

FUZIX can allocate block buffers externally to save BSS space. Genix
uses statically allocated buffers (16 blocks × 1024 bytes = 16 KB in
the buffer cache). On the Mega Drive with 64 KB RAM, this is a
significant fraction. Consider reducing MAXBUF or making it configurable
per platform.

**4. Smaller `p_tab` structure**

FUZIX's process table entry is more compact. Genix's `struct proc`
includes a 512-byte kstack inside the struct — 16 procs × 512 bytes =
8 KB just for kernel stacks. FUZIX keeps the kernel stack separate.
On the Mega Drive, 8 KB for kstacks is a real cost. Options:
- Reduce MAXPROC to 8 (saves 4 KB)
- Reduce KSTACK_SIZE to 256 (risky — deepest syscall path was 214 bytes)
- Move kstacks to a separate array (no memory saving, but cleaner)

**5. `doexec` register clearing**

FUZIX's `doexec` clears all user registers (D0-D7, A0-A6) to zero before
entering user mode. This is a minor security/cleanliness measure — prevents
kernel data from leaking into user registers. Genix's `exec_enter` doesn't
clear registers. Not a functional issue but good practice.

**6. Trap handler design — dedicated entry points**

FUZIX uses dedicated entry points for each exception type, pushing a trap
number before branching to common code. This avoids conditional logic in
the handler. Genix uses a single `_vec_syscall` entry. For the current
scope (only TRAP #0), this is fine. If more exception types are added,
FUZIX's approach is cleaner.

### 4.3 What Genix Does Better Than FUZIX

**1. Readability and size**

Genix kernel: ~5,400 lines. FUZIX kernel: ~15,000+ lines (platform-independent)
plus ~2,000 per platform. One person can read the entire Genix kernel in
a sitting. This was the primary goal and it's achieved.

**2. Testing infrastructure**

Genix has 615+ host unit tests covering every subsystem (mem, string,
exec, proc, signal, TTY, redirection, VDP, libc). FUZIX has no
equivalent host-side test suite — testing happens on-target only. Genix's
host tests catch logic bugs before cross-compilation, which is a
significant productivity advantage.

**3. Workbench emulator for rapid iteration**

The Musashi-based emulator gives 2-second edit-compile-run cycles.
FUZIX development on the Mega Drive requires BlastEm with Xvfb, which
is slower and more complex. The workbench was not in FUZIX's design —
it's a Genix innovation.

**4. STRICT_ALIGN emulator mode**

Genix's workbench emulator can catch unaligned accesses that would
fault on real 68000 hardware. FUZIX relies on BlastEm or real hardware
to find alignment bugs. This catches an entire class of bugs earlier.

**5. Clean PAL separation**

Genix has a clean two-directory PAL split (workbench/ and megadrive/)
with identical interfaces. FUZIX's platform code is spread across
`platform-megadrive/`, `cpu-68000/`, and generic kernel code with
`#ifdef` conditionals. Genix's approach is simpler to understand.

**6. Automated testing ladder**

The six-step testing ladder (host tests → cross-compile → workbench
autotest → megadrive build → BlastEm headless → BlastEm autotest) with
`make test-all` is well-documented and enforced. FUZIX has no equivalent
CI-friendly testing pipeline for the Mega Drive.

**7. Custom libc tuned for 68000**

Genix's libc includes `divmod.S` (pure 68000 division) linked before
libgcc, preventing 68020 illegal instructions. FUZIX relies on the
toolchain's libgcc, which can contain BSR.L and other 68020 instructions.

---

## 5. Testing Gaps

### 5.1 Host Test Coverage

| Subsystem | Tests | Lines | Assessment |
|-----------|-------|-------|------------|
| String functions | 237 | test_string.c | Good coverage |
| Memory allocator | 228 | test_mem.c | Good — first-fit, coalescing, fragmentation |
| Exec/binary loading | 355 | test_exec.c | Good — header validation, argv layout |
| Process management | 403 | test_proc.c | Good — spawn, waitpid, process groups |
| Signals | 656 | test_signal.c | Good — handlers, frames, stop/continue |
| I/O redirection | 325 | test_redir.c | Good — pipes, dup/dup2, refcounting |
| TTY line discipline | 970 | test_tty.c | **Excellent** — 78 cases, edge cases |
| VDP graphics | 232 | test_vdp.c | OK — basic validation |
| Libc functions | 336 | test_libc.c | OK — getopt, sprintf, strtol |

**Gaps identified:**

1. **No filesystem host tests.** `fs.c` (633 lines) has zero host test
   coverage. The filesystem is tested only via autotest (guest tests).
   This is a significant gap — inode allocation, directory operations,
   indirect blocks, free list management, rename atomicity, and
   unlink-while-open should all have host tests.

2. **No buffer cache tests.** `buf.c` (69 lines) is untested on host.
   LRU eviction, dirty writeback, and cache pressure scenarios are
   untested.

3. **No kprintf tests.** `kprintf.c` (96 lines) format string handling
   is untested on host.

4. **No context switch tests.** The swtch()/proc_first_run assembly is
   tested only via autotest. Host tests can't easily test assembly, but
   the kstack layout construction (`proc_setup_kstack`) could be tested.

5. **No error path testing for exec.** The autotest tests successful
   exec and one ENOENT case. Missing: corrupt header, oversized binary,
   BSS overflow, entry point outside binary, zero-length file.

6. **No pipe stress tests.** The autotest pipe test writes 5 bytes.
   Missing: full buffer (512 bytes), partial reads, reader close during
   write, writer close during read, SIGPIPE race conditions.

7. **No multi-process autotest.** The autotest spawns individual programs
   but doesn't test scenarios where multiple processes interact: a shell
   pipeline that exercises preemptive scheduling, a process that catches
   signals while doing I/O, etc.

8. **No kstack overflow detection.** The 512-byte kstack has no canary.
   A deeply nested syscall path silently corrupts the proc struct.
   Adding a canary (magic word at kstack[0], checked on syscall return)
   would catch this.

### 5.2 Autotest Coverage

The 19+ autotest cases cover the happy path well but miss error
paths and edge cases. Adding these would strengthen the quality gate:

- exec with corrupt binary (should return -ENOEXEC)
- pipe buffer overflow (512+ bytes)
- signal during blocking read
- Multiple spawns exhausting process table (should return -EAGAIN)
- SRAM read/write validation (on Mega Drive)
- /dev/null behavior (write discards, read returns 0)
- getcwd after chdir
- stat on /dev/tty (should return device type)

### 5.3 Testing Recommendations

**Priority 1 (do now):**
- Add `tests/test_fs.c` — filesystem logic tests on host
- Add kstack canary for debug builds

**Priority 2 (do soon):**
- Add exec error path autotests
- Add pipe stress autotests
- Add multi-process interaction autotests

**Priority 3 (do eventually):**
- Automated pixel comparison for VDP screenshot tests
- Performance benchmarks (context switch time, syscall overhead)
- Memory high-water-mark tracking

---

## 6. Remaining Phases — What's Left

### Phase 2f: Fuzix libc + Utilities — THE MAIN EVENT

This is the last major phase before the project achieves its goal of
running Unix utilities on the Mega Drive. The kernel infrastructure is
complete. What remains is:

1. Filling libc gaps (Section 7)
2. Porting utilities from Fuzix (Section 8)
3. Testing each utility on both platforms

### Phase 4: Polish

| Item | Effort | Value | Priority |
|------|--------|-------|----------|
| Interrupt-driven keyboard | Medium | Eliminates polling overhead | Medium |
| Multi-TTY (NTTY>1) | Low | Infrastructure exists, just add devices | Low |
| kstack canary | Trivial | Catches silent corruption bugs | High |
| Block buffer tuning | Low | Reduce MAXBUF on Mega Drive | Medium |
| SRAM filesystem | Medium | Writable persistent storage | Medium |

---

## 7. Libc Gaps for App Porting

Genix libc has: stdio (FILE*, fopen, fgets, fprintf, puts), stdlib
(malloc/free, atoi, atol, exit, qsort, rand, getenv, strtol, strtoul),
string, ctype, termios, unistd, getopt, sprintf, perror, strerror,
dirent (opendir/readdir/closedir), isatty, gfx (VDP).

**Still missing for porting Fuzix apps:**

| Function | Needed by | Source | Effort |
|----------|-----------|--------|--------|
| `sscanf()` | od, sort, many | Write (extend sprintf parser) | Medium |
| `regex` (re_comp/re_exec) | grep, sed, expr | Copy from Fuzix libc | Medium |
| `glob()` | shell, find | Copy from Fuzix libc | Medium |
| `time()/ctime()` | ls -l, find, make | Write (wrap SYS_TIME) | Low |
| `getenv()/setenv()` | shell, many | Write (environment block) | Medium |
| `mktime()` | Utilities needing timestamps | Write | Low |
| `strftime()` | ls -l timestamp formatting | Write | Low |
| `signal()/sigaction()` wrappers | Ported apps expecting libc API | Write (wrap SYS_SIGNAL) | Low |
| `wait()/waitpid()` wrappers | Shell, make | Write (wrap SYS_WAITPID) | Low |
| `execve()` wrapper | Shell | Write (wrap SYS_EXEC) | Low |
| `vfork()` wrapper | Shell, make | Exists in libc/syscalls.S | Done |

---

## 8. App Porting Strategy

### 8.1 Source: Fuzix Applications

Fuzix organizes apps in `Applications/` with 32 subdirectories:
util/ (core utilities), levee/ (vi clone, already ported), games/,
MWC/ (tools from Mark Williams C), CC/ (C compiler), basic/, etc.

Most utilities in `util/` are standalone .c files that need only
stdio, stdlib, string, and getopt. They compile against Fuzix libc
but the code is portable — changing `#include` paths and linking
against Genix libc is straightforward.

### 8.2 Porting Process

For each Fuzix utility:

1. Copy the .c file(s) to `apps/`
2. Change `#include` paths (Fuzix uses `<` includes that map to its libc)
3. Check for missing libc functions — add to Genix libc if needed
4. Add to `PROGRAMS` list in `apps/Makefile`
5. Build and test on workbench (`make run`)
6. Verify on Mega Drive (`make megadrive`, `make test-md`)
7. Add autotest case if the program is testable non-interactively
8. If binary > 28 KB, mark as workbench-only

### 8.3 Porting Tiers (Updated)

**Tier 0 — Already done (22 programs):**
hello, echo, cat, wc, head, true, false, tail, tee, yes, basename,
dirname, rev, nl, cmp, cut, tr, uniq, imshow, ls, sleep, levee.

**Tier 1 — No kernel changes needed, minimal libc additions:**

| Program | Fuzix source | Libc needed | Notes |
|---------|-------------|-------------|-------|
| od | util/od.c | sscanf | Octal/hex dump |
| strings | util/strings.c | None | Extract printable strings |
| fold | util/fold.c | None | Line folding |
| paste | util/paste.c | None | Merge file lines |
| expand | util/expand.c | None | Tab expansion |
| unexpand | util/unexpand.c | None | Space to tab |
| comm | util/comm.c | None | Compare sorted files |
| env | util/env.c | setenv | Print/set environment |
| seq | Write new | None | Number sequence generator |
| tac | Write new | None | Reverse cat |

**Tier 2 — Needs regex library:**

| Program | Fuzix source | Notes |
|---------|-------------|-------|
| grep | util/grep.c | Core utility, high value |
| sed | MWC/cmd/sed/ | Stream editor |
| expr | MWC/cmd/expr.c | Expression evaluator |

**Tier 3 — Needs shell improvements (vfork+exec from shell):**

| Program | Fuzix source | Notes |
|---------|-------------|-------|
| sh | Write minimal or port | Real shell with job control |
| xargs | util/xargs.c | Needs exec |
| time | util/time.c | Needs vfork+exec+waitpid |
| nohup | util/nohup.c | Needs signals |

**Tier 4 — Larger programs, may not fit on Mega Drive:**

| Program | Fuzix source | Size estimate | Notes |
|---------|-------------|---------------|-------|
| ed | ue/ or MWC/cmd/ed.c | ~15 KB | Line editor |
| diff | util/diff.c | ~10 KB | File comparison |
| sort | util/sort.c | ~8 KB | Needs temp files |
| make | MWC/cmd/make/ | ~20 KB | Build system |

### 8.4 Build Organization

**Keep the flat `apps/` directory.** Each program is one .c file.
The existing build system handles everything:

```makefile
PROGRAMS = hello echo cat wc head true false tail tee yes \
           basename dirname rev nl cmp cut tr uniq ls sleep \
           od strings fold paste expand unexpand comm grep sed ...

# Programs too large for Mega Drive (>28 KB binary)
PROGRAMS_WB_ONLY = levee
```

For multi-file programs (levee, sed), keep a subdirectory with its
own Makefile. The pattern is already established with levee.

---

## 9. Remaining Work — Execution Plan

### Phase 2f: App Porting (the main event)

```
Step 1: Tier 1 utilities (no kernel changes)
  - Port 8-10 simple utilities (od, strings, fold, paste, expand, etc.)
  - Add each to AUTOTEST where possible
  - Verify on Mega Drive
  Estimated: 10-15 new apps in /bin

Step 2: regex library
  - Port Fuzix re_comp/re_exec to Genix libc
  - Port grep (highest-value single utility)
  - Port sed
  Estimated: 3 apps, ~500 lines libc addition

Step 3: Shell improvements
  - The built-in shell already supports pipes and redirects
  - Add glob expansion (*, ?)
  - Add environment variables ($PATH, $HOME)
  - Add command-not-found with PATH search
  - Consider porting Fuzix sh or writing minimal POSIX sh
  Estimated: ~300 lines shell code or port

Step 4: Remaining utilities
  - Port ed, diff, sort, make as demand requires
  - Each one tested on both platforms
  - Large programs marked workbench-only
```

### Phase 4: Polish

```
Step 5: Interrupt-driven keyboard (Mega Drive)
  - Move Saturn keyboard polling from read loop to VBlank ISR
  - ISR calls tty_inproc() to inject characters
  - Already architecturally ready (TTY supports it)

Step 6: kstack canary
  - Add magic word at kstack[0]
  - Check on every syscall return
  - Panic on corruption (debug builds only)

Step 7: Block buffer tuning
  - Reduce MAXBUF from 16 to 8 on Mega Drive (saves 8 KB RAM)
  - Platform-configurable via PAL

Step 8: SRAM persistent filesystem
  - minifs on SRAM for writable storage
  - ROM disk stays read-only for /bin
  - PAL detects SRAM size at boot
```

---

## 10. Summary — Where We Stand

**The kernel is done.** Phases 1 through 2e and Phase 3 are complete.
The remaining work is in userspace: libc enrichment and app porting
(Phase 2f) plus optional polish (Phase 4).

**Key metrics:**

| Metric | Value |
|--------|-------|
| Kernel lines of code | ~5,400 |
| Host unit tests | 615+ |
| Autotest cases | 19+ |
| User programs | 22 |
| Libc modules | 14 |
| Syscalls implemented | 31 |
| Platforms | 2 (workbench + Mega Drive) |

**Biggest remaining risk:** The single user memory space (no relocation)
limits pipelines to 512 bytes of buffered data. This is a no-MMU
fundamental limitation. For the current use case (simple filter programs,
interactive shell), it's adequate. If we ever need true concurrent
pipelines, we'll need relocation.

**What to do next:** Port Tier 1 utilities (Section 8.3). Each one is
a standalone .c file that takes ~30 minutes to port. After 10 more
utilities, Genix becomes a genuinely useful Unix-like environment on the
Mega Drive.
