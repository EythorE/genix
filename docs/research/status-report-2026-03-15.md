# Genix Project Status Report — 2026-03-15

## Executive Summary

Genix is a working, tested, preemptive multitasking OS running a real
POSIX shell (dash) with 47 user programs on a 7.67 MHz 68000 with 64 KB
RAM and no MMU. Phases 1-6 and A-D are complete. The kernel is clean
(zero TODO/FIXME in Genix-authored code), well-tested (5,230+ host
assertions, 8-level automated test ladder), and documented (23 docs
files). The next planned feature is Phase 7 (SD card filesystem).

---

## 1. What Has Been Built

### Kernel (6,447 lines across kernel/ + pal.h)

| Subsystem | File | Lines | Status |
|-----------|------|-------|--------|
| Boot + vectors | crt0.S | 239 | Complete |
| Main + built-in shell | main.c | ~180 | Complete |
| Formatted output | kprintf.c | ~100 | Complete |
| Memory allocator | mem.c | 190 | Complete (+ slot allocator) |
| Buffer cache | buf.c | 69 | Complete |
| Filesystem (minifs) | fs.c | 683 | Complete |
| Devices | dev.c | ~60 | Complete |
| TTY + line discipline | tty.c | 472 | Complete |
| Process table + scheduler | proc.c | 1456 | Complete |
| Binary loader + XIP | exec.c | 606 | Complete |
| Exec entry (asm) | exec_asm.S | 172 | Complete |
| 68000 division | divmod.S | 172 | Complete (unoptimized) |
| String functions | string.c | ~130 | Complete (unoptimized) |
| Kernel header | kernel.h | 497 | 32 syscalls defined |

### Platform Abstraction Layer

| Platform | Files | Lines | Status |
|----------|-------|-------|--------|
| Workbench (emulator) | 1 .c | 135 | Complete |
| Mega Drive | 8 .c/.S + headers + ld | 4,702 | Complete |
| PAL interface | pal.h | 31 | Complete |

### Libc (4,490 lines)

19 modules including: syscall stubs, stdio, stdlib, string, ctype,
regex, termios, dirent, signal, setjmp/longjmp, line editing, graphics
library, POSIX header set. Supports dash shell fully.

### User Programs (47 binaries)

- **dash** — POSIX shell with interactive line editing and history
- **levee** — vi clone (workbench-only, too large for MD without PSRAM)
- **45 utilities** — ls, cat, grep, sort, find, xargs, cp, mv, rm, more,
  wc, tr, cut, uniq, expr, od, and 29 others

### Test Suite (8,791 lines)

- 17 host test files, 5,230+ assertions
- 8-level testing ladder from host unit tests to BlastEm autotest
- CI via GitHub Actions (host tests, cross-build, emu autotest, MD autotest)
- 68020 opcode scanner catches wrong-toolchain bugs at compile time

### Build Tools

- **mkfs.minifs** — filesystem image creator
- **mkbin** — ELF-to-Genix binary converter with relocation extraction
- **romfix** — post-link ROM processor for XIP address resolution

---

## 2. Codebase Metrics

| Component | Lines of Code |
|-----------|---------------|
| Kernel (kernel/) | 6,447 |
| PAL megadrive | 4,702 |
| PAL workbench | 135 |
| Libc | 4,490 |
| Apps (excl. dash/levee) | 3,463 |
| Tests | 8,791 |
| Dash (ported) | ~5,500 |
| Levee (ported) | ~3,000 |
| **Genix-authored total** | **~23,000** |
| **Including ports** | **~31,500** |

---

## 3. Documentation Inconsistencies

### Line Count Drift

The kernel has grown but documentation hasn't kept pace:

| Document | Claims | Actual |
|----------|--------|--------|
| README.md | ~5,400 lines | 6,447 lines (kernel/ only) |
| CLAUDE.md | ~5,400 lines | 6,447 lines |
| HISTORY.md | ~5,650 lines | 6,447 lines |
| decisions.md | ~5,650 lines | 6,447 lines |

The ~5,400 figure dates from before Phases 5-6/A-D added ~800 lines.
Should be updated to ~6,400 everywhere, or qualified as "kernel core"
vs "kernel + PAL."

### PLAN.md Contains Completed Phases

PLAN.md currently contains detailed descriptions of Phases 5, 6, C, and
D including full outcome sections. The file's header says "What remains
to be built" and points to HISTORY.md for completed phases, but then
devotes 235 lines to completed work (Phases 5, 6, dash, Phase D). Only
Phases 7, 8, 9 and "Remaining Optional Work" are forward-looking.

### Stale References

- README line 166: `apps-md` target — this builds apps "linked at
  0xFF8000" but with relocatable binaries (Phase 5), apps are linked at
  address 0. The comment is stale.
- PLAN.md line 470: "Tier 1 remaining: ed (line editor)" — but Tier 1
  is described as complete in the header and in CLAUDE.md. Either ed was
  dropped from Tier 1 or this is stale.
- `test-levee` is marked "KNOWN BROKEN" in the Makefile (line 188) but
  the levee `-msep-data` fix was committed. Is it still broken?

---

## 4. Recommendations

### 4.1 Documentation Consolidation

**Move completed phase details from PLAN.md to HISTORY.md.** PLAN.md
should be the forward roadmap only. The outcome sections for Phases 5,
6, C, and D (with their measured results, deviations, and gotchas) are
valuable history — move them to HISTORY.md under a "Phase Outcomes"
chapter. Leave one-line summaries in PLAN.md's dependency graph.

**Update line counts.** A single pass through README.md, CLAUDE.md,
HISTORY.md, and decisions.md to change ~5,400/~5,650 to ~6,400. Or
better: use a qualifier like "~6,400 lines of kernel code" that's less
likely to go stale quickly.

**Verify and update stale references.** The three items listed in
section 3 above, plus any others found during the consolidation pass.

### 4.2 Platform Configuration System

The current platform setup works but has friction: the Mega Drive build
recompiles the entire kernel from source with different flags, the
workbench build has its own kernel/Makefile, and platform constants are
scattered between Makefiles, linker scripts, and C code.

**Proposed approach: `platforms/` directory with per-platform config files.**

```
platforms/
  workbench.mk      # CFLAGS, memory constants, PAL sources, linker script
  megadrive.mk      # CFLAGS, memory constants, PAL sources, linker script
  # future:
  rosco_m68k.mk     # Another 68000 SBC
  everdrive_pro.mk  # MD + PSRAM extensions
```

Each platform config defines:
```makefile
# platforms/megadrive.mk
PLATFORM_NAME    = megadrive
PLATFORM_DIR     = pal/megadrive
PLATFORM_LD      = pal/megadrive/megadrive.ld
PLATFORM_CFLAGS  = -DNBUFS=8
PLATFORM_ASMSRCS = crt0.S keyboard_read.S vdp.S devvt.S dbg_output.S
PLATFORM_CSRCS   = platform.c keyboard.c fontdata_8x8.c dev_vdp.c
USER_BASE        = 0xFF9000
USER_TOP         = 0xFFFE00
SLOT_COUNT       = 2
SLOT_SIZE        = 14336
POST_BUILD       = romfix
```

Then `make PLATFORM=megadrive` (or just `make megadrive`) includes the
right config file and builds accordingly. The top-level Makefile becomes
a thin driver that includes platform configs and runs the shared build
rules.

**Benefits:**
- Adding a new platform = adding one .mk file (no Makefile surgery)
- Platform constants live in one place (not split across Makefile,
  linker script, and C code)
- `make megadrive` could print: "Built genix-md.bin (ROM size: 384KB).
  Run with: blastem pal/megadrive/genix-md.bin"
- `make workbench` could print: "Built kernel.bin. Run with: make run"
- Future platforms (rosco_m68k, custom SBCs) drop in cleanly

**Migration path:** This is a build system refactor only — no kernel or
PAL code changes needed. The current pal/megadrive/Makefile already has
all the information; it just needs to be extracted into a declarative
config. Do it incrementally: start with megadrive.mk, verify `make
test-all` passes, then do workbench.mk.

**Caution:** The Mega Drive build has complex post-processing (romdisk
embedding, romfix). The platform config should declare that post-build
steps exist; the actual logic can stay in a platform-specific script or
Makefile fragment. Don't try to make the config file handle everything.

### 4.3 Things That Should Have Been Done But Weren't

1. **PLAN.md consolidation was supposed to happen after each phase.**
   CLAUDE.md says: "After completing each phase, add an implementation
   report to the relevant docs/*-plan.md... Also update HISTORY.md,
   PLAN.md, CLAUDE.md... This documentation pass is mandatory." The
   outcome sections exist in PLAN.md but the completed phases were never
   moved out of PLAN.md into HISTORY.md, so PLAN.md is now half
   roadmap, half history.

2. **`test-levee` status is unclear.** The `-msep-data` fix was applied
   but the Makefile still says "KNOWN BROKEN." If it passes now, remove
   the warning. If it still fails, document why.

3. **No Mega Drive dash autotest.** The BlastEm autotest (`test-md-auto`)
   boots and runs for 600 frames but doesn't verify that dash actually
   starts or accepts commands on the MD. The workbench has `test-dash.sh`
   (15 tests), but there's no equivalent for the MD target. Since the
   VDP console and Saturn keyboard have different code paths from UART,
   a dash-specific MD test would catch regressions in the display/input
   stack.

4. **No test for the slot allocator.** Phase 6 added `slot_alloc()` /
   `slot_free()` to mem.c but there's no host unit test exercising the
   allocation, freeing, double-free, and exhaustion paths. This is
   exactly the kind of logic that benefits from unit testing.

5. **No test for romfix.** The romfix tool patches ROM binary contents
   based on relocation tables. A corrupted romfix output would cause
   silent wrong-address execution on the MD. A host test that runs
   romfix on a known input and verifies output bytes would catch
   regressions.

### 4.4 Decisions That May Not Have Been Optimal

These are decisions that still affect current and future work:

1. **Two separate kernel build systems.** The workbench kernel is built
   by `kernel/Makefile`, the Mega Drive kernel by `pal/megadrive/Makefile`.
   They compile the same kernel .c files with different flags and
   different PAL sources. This means:
   - Every new kernel file must be added to both Makefiles
   - Flag consistency between platforms depends on remembering to
     update both
   - The `NBUFS=8` define for MD is in the Makefile, not in a shared
     config
   - This is the root cause of why a platform config system (section
     4.2) would help

2. **Duplicated string.c in kernel and libc.** `kernel/string.c` and
   `libc/string.c` both implement memcpy, memset, strlen, strcmp, etc.
   When these get optimized ([optimization plan](../plans/optimization-plan.md) Priority 3), the assembly
   versions will need to exist in both places (or be shared). Consider
   having the kernel link against a shared `memops.S` at optimization
   time rather than maintaining two copies.

3. **GOT offset packed into stack_size upper 16 bits.** The Genix binary
   header has no spare fields, so the GOT offset was squeezed into the
   high 16 bits of the `stack_size` field. This limits both values to
   64 KB — fine today (largest data is ~6.8 KB), but Phase 8 PSRAM
   binaries loaded from SD could potentially exceed this if a large
   program has >64 KB of data. This would require a header version bump,
   which affects mkbin, the kernel loader, and all existing binaries.
   **Suggestion:** If a v2 header is ever needed for other reasons, add
   a proper `got_offset` field at that time. Don't change it preemptively.

4. **`slot_base()` returns 0 for invalid slots.** Address 0 is the
   68000 reset vector area. If a bug ever passes an unchecked slot index,
   it would silently corrupt vectors. Changing this to `panic()` or
   `(uint32_t)-1` is a 1-line fix that would convert a hard-to-debug
   silent corruption into an immediate, visible crash. **Should fix now.**

5. **`exec_user_a5` global variable.** Set by `do_exec()`, consumed by
   `exec_enter()` in assembly. This works because exec is synchronous
   and non-reentrant today. But it's fragile — an interrupt between the
   assignment and `exec_enter()` that somehow triggers another exec
   would corrupt it. The spawned-process path already uses the robust
   approach (baking a5 into the kstack frame). **Consider migrating the
   primary exec path to use the kstack frame approach too**, eliminating
   the global entirely. Low priority since it works today.

### 4.5 Things That Need Fixing

1. **`slot_base(0)` sentinel value** (see 4.4 item 4). Change to
   `panic("slot_base: invalid slot")`. One line, converts silent
   corruption to loud crash.

2. **No duplicate reloc guard in mkbin GOT scanning.** If a future GCC
   emits GOT relocs via `--emit-relocs` (which current GCC doesn't),
   the same offset would appear twice in the reloc table, and the kernel
   would apply the relocation twice, corrupting the value. Adding a
   dedup check to mkbin is ~10 lines and future-proofs against toolchain
   upgrades. **Fix before upgrading GCC.**

3. **`test-levee` Makefile comment.** Verify whether levee works now
   (after the `-msep-data` fix) and update the "KNOWN BROKEN" comment
   accordingly.

### 4.6 Improvements to What Already Exists

**These do not require real hardware to implement or test:**

1. **Division fast path ([optimization plan](../plans/optimization-plan.md) Priority 1).** Replace the
   shift-and-subtract loop in `divmod.S` with the FUZIX pattern: check
   if the divisor fits in 16 bits, use hardware `DIVU.W` (~150 cycles)
   for the common case. Almost all divisors in Genix are small constants.
   ~20 lines of assembly, 2-5x speedup on every `/` and `%` operation.
   Testable on the workbench emulator with the existing test ladder.
   **Highest-value optimization — do this first.**

2. **Assembly memcpy/memset ([optimization plan](../plans/optimization-plan.md) Priority 3).** The
   byte-at-a-time C implementations are 4x slower than they need to be
   for medium/large transfers. Create `kernel/memops.S` with MOVE.L
   loops for medium sizes and MOVEM.L for large (>64 byte) transfers.
   This is a prerequisite for the pipe bulk copy optimization (Priority
   4). Testable entirely on the host and workbench.

3. **Pipe bulk copy ([optimization plan](../plans/optimization-plan.md) Priority 4).** Replace the
   byte-at-a-time pipe read/write loop with contiguous-chunk memcpy.
   ~15 lines of C, 2-4x throughput improvement for shell pipelines.
   Depends on Priority 3 for maximum benefit. Testable with existing
   `test_pipe.c`.

4. **Host unit tests for slot allocator.** `slot_alloc()`, `slot_free()`,
   exhaustion, double-free, and slot_base() behavior. ~50 lines in a
   new or existing test file. Would have caught the `slot_base(0)`
   sentinel issue before it was written.

5. **Host test for romfix.** Create a minimal test binary, run romfix
   on it, verify the output bytes match expected values. Catches
   regressions in the XIP address resolution logic.

6. **Consolidate Makefile duplication.** The test targets (`test-emu`,
   `test-md-auto`, `test-md-screenshot`, `test-md-imshow`) share a
   pattern: clean, rebuild with special flags, run, capture result,
   rebuild normal. This is ~200 lines of near-identical shell script in
   the Makefile. A helper script (`scripts/run-platform-test.sh`) could
   reduce this to ~50 lines while making each test target a one-liner.

7. **Line editing: ESC timeout.** The line editor blocks on bare ESC
   key on the workbench because there's no timeout to distinguish ESC
   alone from an ANSI escape sequence prefix. Implementing a short
   timeout (100ms via `termios` VTIME) or O_NONBLOCK probe would fix
   this. This is a workbench-only issue (the MD Saturn keyboard sends
   distinct keycodes, not ANSI escapes). Testable on the workbench.

### 4.7 What Affects Future Real-Hardware Work

**Things to do now that make the eventual hardware bring-up smoother:**

1. **The optimization items above (divmod, memcpy, pipe).** On real
   hardware at 7.67 MHz, every cycle counts. These optimizations are
   testable now and will be appreciated when running on real iron. The
   division fast path alone affects every `kprintf` call (base-10
   conversion), every filesystem operation (inode calculations), and
   every pipe transfer.

2. **Romfix test coverage.** Romfix is the tool that makes XIP work on
   the Mega Drive. A subtle bug in romfix would produce a ROM that boots
   but crashes unpredictably on certain programs. Testing romfix with
   known inputs now means one less thing to debug on real hardware.

3. **Slot allocator testing.** On the Mega Drive with only 2 slots,
   allocation bugs would immediately prevent pipelines from working.
   Unit tests now prevent debugging this on real hardware.

4. **Platform config system (4.2).** Phase 7 (SD card) targets two
   different EverDrive cartridges with different hardware interfaces.
   Phase 8 adds PSRAM extensions to the Pro. Having a clean platform
   config system before starting these phases means the new platform
   variants (open-everdrive, everdrive-pro, everdrive-pro-psram) can be
   added as config files rather than Makefile surgery.
