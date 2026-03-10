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
- **Phase 2f** (libc + apps): 34 user programs, 15 libc modules incl. regex
- **Phase 4** (polish): Interrupt keyboard, multi-TTY, NBUFS config, SRAM validation
- **34 user programs** (was 8 at first review, 22 at last review)
- **4924+ host test assertions** across 13 test files (was 283 at first review)
- **31+ autotest cases** on both platforms
- **CI pipeline** enforces full testing ladder (host → cross → emu → BlastEm)

All project phases are now complete.

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

### Phase 2f: Fuzix libc + Utilities — COMPLETE

**What the plan said:** Use newlib for C library, port GNU tools.

**What was built:** Custom minimal libc (~2500 lines, 15 modules including
regex) with stdio, stdlib, string, ctype, termios, getopt, sprintf,
sscanf, strtol, perror, dirent, isatty, regex, gfx. 34 user programs
including levee (vi clone), grep, od, env, expr, and 12 additional
Tier 1 utilities (strings, fold, expand, unexpand, paste, comm, seq,
tac, cut, tr, uniq, rev).

**Assessment:** The plan's newlib goal was wrong — newlib is 50-100 KB,
too large for the Mega Drive's 64 KB RAM. The custom libc is the right
choice. It's smaller, we control it, and it's proven (levee works).
All Tier 1 and Tier 2 utilities from the original porting plan have
been completed.

### Phase 3: Mega Drive Port — COMPLETE, matches plan

VDP text output, Saturn keyboard, SRAM, ROM disk. PAL abstraction works.
BlastEm tests pass. The Mega Drive drivers were reused from FUZIX as planned.

### Phase 4: Polish — COMPLETE

- /dev/null: **Done**
- Interrupt-driven keyboard: **Done** (VBlank ISR calls pal_keyboard_poll)
- Multi-TTY: **Done** (NTTY=4, /dev/tty0-tty3 device nodes)
- Configurable buffer cache: **Done** (NBUFS=16 workbench, NBUFS=8 Mega Drive)
- SRAM validation: **Done** (boot-time magic check, zero on invalid)

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

Genix has 4924+ host test assertions across 13 test files covering every
subsystem (mem, string, exec, proc, signal, TTY, redirection, VDP, libc,
filesystem, buffer cache, kprintf, pipes). FUZIX has no equivalent
host-side test suite — testing happens on-target only. Genix's host tests
catch logic bugs before cross-compilation, which is a significant
productivity advantage.

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
| Libc functions | 336 | test_libc.c | OK — getopt, sprintf, strtol, qsort, sscanf |
| Filesystem | 118 | test_fs.c | Good — inode alloc, directory ops, block deallocation |
| Buffer cache | 36 | test_buf.c | Good — LRU eviction, dirty writeback, cache pressure |
| kprintf | 24 | test_kprintf.c | Good — format specifiers, edge cases |
| Pipe stress | 2170 | test_pipe.c | **Excellent** — full buffer, wrap, partial reads |

**Resolved gaps (since initial review):**

1. ~~No filesystem host tests~~ — **Resolved.** `test_fs.c` added with
   118 assertions covering inode allocation, directory operations,
   indirect blocks, and block deallocation on inode free.

2. ~~No buffer cache tests~~ — **Resolved.** `test_buf.c` added with
   36 assertions covering cache hits/misses, LRU eviction, dirty
   writeback, and multi-device scenarios.

3. ~~No kprintf tests~~ — **Resolved.** `test_kprintf.c` added with
   24 assertions covering all format specifiers and edge cases.

4. ~~No pipe stress tests~~ — **Resolved.** `test_pipe.c` added with
   2170 assertions covering full buffer, circular wrap, partial reads,
   EOF/EPIPE, and alternating read/write patterns.

**Remaining gaps:**

1. **No context switch tests.** The swtch()/proc_first_run assembly is
   tested only via autotest. Host tests can't easily test assembly, but
   the kstack layout construction (`proc_setup_kstack`) could be tested.

2. **No multi-process autotest.** The autotest spawns individual programs
   but doesn't test scenarios where multiple processes interact: a shell
   pipeline that exercises preemptive scheduling, a process that catches
   signals while doing I/O, etc.

3. **No kstack overflow detection.** The 512-byte kstack has no canary.
   A deeply nested syscall path silently corrupts the proc struct.
   Adding a canary (magic word at kstack[0], checked on syscall return)
   would catch this.

### 5.2 Autotest Coverage

The 31+ autotest cases cover the happy path well. Potential additions
to strengthen the quality gate:

- Multiple spawns exhausting process table (should return -EAGAIN)
- Signal during blocking read
- Performance benchmarks (context switch time, syscall overhead)

### 5.3 Testing Recommendations

**Priority 1 (high value):**
- Add kstack canary for debug builds
- Add multi-process interaction autotests

**Priority 2 (nice to have):**
- Automated pixel comparison for VDP screenshot tests
- Performance benchmarks (context switch time, syscall overhead)
- Memory high-water-mark tracking

---

## 6. Completed Phases — Final Status

### Phase 2f: Fuzix libc + Utilities — COMPLETE

All planned tiers have been completed:
- **Tier 0** (original 22 programs): Done
- **Tier 1** (10 utilities: od, strings, fold, paste, expand, unexpand,
  comm, env, seq, tac): Done
- **Tier 2** (regex-dependent: grep, expr): Done (regex engine + sscanf
  added to libc)
- 34 total user programs in /bin

### Phase 4: Polish — COMPLETE

All Phase 4 items have been resolved:
- Interrupt-driven keyboard: **Done** (VBlank ISR)
- Multi-TTY (NTTY=4): **Done** (/dev/tty0-tty3)
- Block buffer tuning: **Done** (NBUFS configurable, 16 workbench / 8 Mega Drive)
- SRAM validation: **Done** (boot-time magic check)

### Remaining Optional Work

| Item | Effort | Value | Priority |
|------|--------|-------|----------|
| kstack canary | Trivial | Catches silent corruption bugs | Medium |
| glob expansion in shell | Medium | Enables `*.c` patterns | Low |
| SRAM persistent filesystem | Medium | Writable persistent storage | Low |
| Tier 3 utilities (sh, xargs) | Medium | Real shell with job control | Low |
| Tier 4 utilities (ed, diff, sort) | High | Larger programs, may not fit on MD | Low |

---

## 7. Libc Status

Genix libc has 15 modules: stdio (FILE*, fopen, fgets, fprintf, puts),
stdlib (malloc/free, atoi, atol, exit, qsort, bsearch, rand, getenv,
strtol, strtoul), string (strstr, strcasecmp, strcspn, strspn), ctype,
termios, unistd, getopt, sprintf/sscanf, perror, strerror, dirent
(opendir/readdir/closedir), isatty, regex (regcomp/regexec/regfree),
gfx (VDP), divmod.S (68000 division), syscalls.S.

**Previously missing, now implemented:**

| Function | Status | Added for |
|----------|--------|-----------|
| `sscanf()` | **Done** | od, general parsing |
| `regex` (regcomp/regexec) | **Done** | grep, expr |
| `getenv()/setenv()` | **Done** | env, shell |
| `qsort()/bsearch()` | **Done** | general use |
| `vfork()` wrapper | **Done** | shell, process creation |

**Still missing (needed only for future Tier 3/4 apps):**

| Function | Needed by | Effort |
|----------|-----------|--------|
| `glob()` | shell, find | Medium |
| `time()/ctime()` | ls -l, find, make | Low |
| `mktime()/strftime()` | timestamp formatting | Low |
| `signal()/sigaction()` wrappers | ported apps expecting libc API | Low |

---

## 8. App Porting — Final Status

### 8.1 Completed Apps (34 programs)

hello, echo, cat, wc, head, true, false, tail, tee, yes, basename,
dirname, rev, nl, cmp, cut, tr, uniq, imshow, ls, sleep, strings,
fold, expand, unexpand, paste, comm, seq, tac, grep, od, env, expr,
levee.

### 8.2 Remaining Porting Candidates

**Tier 3 — Needs shell improvements:**

| Program | Notes |
|---------|-------|
| sh | Real shell with job control |
| xargs | Needs exec |
| time | Needs vfork+exec+waitpid |
| nohup | Needs signals |

**Tier 4 — Larger programs, may not fit on Mega Drive:**

| Program | Size estimate | Notes |
|---------|---------------|-------|
| ed | ~15 KB | Line editor |
| diff | ~10 KB | File comparison |
| sort | ~8 KB | Needs temp files |
| sed | ~10 KB | Stream editor (multi-file) |
| make | ~20 KB | Build system |

---

## 9. Summary — Where We Stand

**The project is feature-complete.** All planned phases (1 through 4)
are done. The kernel, libc, app suite, and Mega Drive port all work.
The CI pipeline enforces the full testing ladder on every push.

**Key metrics:**

| Metric | Value |
|--------|-------|
| Kernel lines of code | ~5,650 |
| Host test assertions | 4,924+ |
| Host test files | 13 |
| Autotest cases | 31+ |
| User programs | 34 |
| Libc modules | 15 |
| Syscalls implemented | 32 |
| Platforms | 2 (workbench + Mega Drive) |

**Known limitations (by design):**

1. **Single user memory space** — all user programs load at USER_BASE;
   two processes can't coexist in memory. Pipelines execute sequentially.
   Fundamental no-MMU limitation.

2. **Shell features** — no glob expansion, no environment variable
   substitution, no background jobs. The built-in shell is adequate
   for the current single-user, single-terminal use case.

**Possible future work:** Port a real shell (Tier 3), add glob expansion,
port larger utilities like ed/diff/sort (Tier 4), add kstack canary for
debug builds. None of these are blockers — the system is usable as-is.
