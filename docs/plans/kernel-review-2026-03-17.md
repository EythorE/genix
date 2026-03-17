# Kernel Review & Improvement Opportunities — 2026-03-17

## Overview

Thorough review of all kernel source files, test coverage, existing plans,
research documents, dev branches, and project documentation. This review
covers: potential bugs, missing tests, code quality issues, stale docs,
and forward planning.

---

## 1. Kernel Bug Findings

### 1.1 CRITICAL: do_spawn_fd PID Cast (proc.c)

```c
struct proc *child = &proctab[(uint8_t)pid];
```

If `do_spawn()` returns a negative error code, the `(uint8_t)` cast wraps
it (e.g., -1 → 255), indexing past the 16-entry proctab.

**Fix:** Check `if (pid < 0) return pid;` before the cast.

### 1.2 HIGH: Memory Leak in do_exec XIP Fallback (exec.c)

When XIP loading fails and a larger region is needed for non-XIP fallback:
```c
curproc->mem_base = 0;
data_addr = umem_alloc(need2);
```
The original `data_addr` allocation is never freed — `curproc->mem_base = 0`
clears the tracking but doesn't reclaim the memory.

**Fix:** Call `umem_free(data_addr)` before reallocating.

### 1.3 HIGH: fs_read/fs_write Integer Overflow (fs.c)

```c
if (off + n > ip->size)
    n = ip->size - off;
```
`off + n` can overflow uint32_t for large values (e.g., off=0xFFFFFFFF, n=1
wraps to 0). Should use subtraction form:
```c
if (n > ip->size - off) n = ip->size - off;
```

### 1.4 MEDIUM: kstack Canary Panic Doesn't Disable Interrupts (proc.c)

```c
if (curproc->kstack[0] != KSTACK_CANARY) {
    kputs("*** PANIC: kstack overflow ...");
    for (;;) ;
}
```
The `for (;;)` loop doesn't disable interrupts. Timer ISR can fire and
access corrupted proc state.

**Fix:** Add `__asm__ volatile("move.w #0x2700,%sr")` before the loop.

### 1.5 MEDIUM: VDP Device Open/Close Never Called

Documented in `docs/research/vdp-research.md` Section 5: `vdp_open()` and
`vdp_close()` are never dispatched by the kernel's `sys_open()`/`sys_close()`.
Exclusive VDP access is completely bypassed. Graphics program crashes leave
the console in graphics mode.

**Fix:** Add device dispatch in sys_open/sys_close/do_exit (~30 lines).

### 1.6 MEDIUM: Hard-coded TTY Window Size (tty.c)

```c
t->winsize.ws_row = 28;   /* Mega Drive VDP */
t->winsize.ws_col = 40;
```
Wrong for workbench (should be 24×80). Should be set by PAL layer.

### 1.7 MEDIUM: F_GETFL Leaks Internal Pipe Flags

Internal pipe endpoint bits (0x1000/0x2000) visible to userspace via
`fcntl(F_GETFL)`. Should be masked before returning.

### 1.8 MEDIUM: FD_CLOEXEC Silently Ignored

dash sets FD_CLOEXEC but the kernel never honors it during exec(). This
is documented in test-coverage.md but not fixed.

### 1.9 LOW: alloc_pid Assumes Power-of-2 MAXPROC

```c
next_pid = (next_pid + 1) & (MAXPROC - 1);
```
No static assertion guards this. If MAXPROC changes to non-power-of-2,
this wraps incorrectly.

**Fix:** Add `_Static_assert((MAXPROC & (MAXPROC - 1)) == 0, ...)`.

### 1.10 LOW: TIMER_HZ Hard-coded in main.c

```c
#define TIMER_HZ 100
```
Should be PAL-provided. Different platforms may need different tick rates.

### 1.11 LOW: Missing User Pointer Validation in Syscalls

Multiple syscall handlers pass user-supplied addresses directly without
validating they're within `USER_BASE..USER_TOP`:
```c
const char *path = (const char *)path_addr;
```
A buggy program could pass kernel addresses. This is a defense-in-depth
concern — no malicious actors exist (single-user system), but it would
catch bugs earlier.

---

## 2. Test Coverage Gaps

### 2.1 Critical Gaps

| Gap | Description | Priority |
|-----|-------------|----------|
| TRAP #0 syscall path | Host tests bypass the real syscall mechanism entirely. Need `apps/test_syscalls.c` | HIGH |
| XIP binary loading | test_exec.c only covers non-XIP headers. No test for load_binary_xip() | HIGH |
| Multi-stage pipes in dash | test-dash.sh only tests 2-stage pipes. 3-stage was broken (Bug 2026-03-17) | HIGH |

### 2.2 Module Coverage

| Module | Coverage | Key Gaps |
|--------|----------|----------|
| mem.c | ~100% | Complete |
| string.c | ~85% | strcat/strncat not tested |
| buf.c | ~100% | Complete for exported functions |
| fs.c | ~85% | namei_parent edge cases, cross-block reads |
| proc.c | ~70% | Syscall dispatch path, vfork races, FD_CLOEXEC |
| exec.c | ~40% | load_binary, XIP, apply_relocations — MAJOR GAP |
| tty.c | ~80% | ECHO toggle, input overflow, ^U kill line |
| kprintf.c | ~95% | Near-complete |
| dev.c | 0% | Device dispatch not tested (by design) |

### 2.3 Missing Test Files

1. **test_exec_xip.c** — Synthetic XIP headers + relocations
2. **apps/test_syscalls.c** — Real TRAP #0 userspace test binary
3. **test_romfix.c** — Host-side romfix tool verification
4. **Mega Drive dash autotest** — Only workbench has test-dash.sh

### 2.4 Recommended New Tests

**For test_proc.c:**
- 5+ stage pipe stress
- FD_CLOEXEC enforcement check
- Signal during blocking syscall
- Pipe closure ordering with multiple readers/writers
- Process cleanup after mid-exec failure

**For test_fs.c:**
- Directory with >63 entries
- Read/write at exact block boundaries
- ialloc exhaustion + rollback

**For test_tty.c:**
- ECHO on/off toggle mid-line
- ^U kill line behavior
- Input queue overflow
- Multiple signal chars in sequence

**For test-dash.sh:**
- 3-stage pipe: `echo hi | cat | cat`
- 4-stage pipe with filter: `echo hi | cat | grep hi | cat`
- Pipe stress loop

---

## 3. Code Quality Observations

### 3.1 Positive Patterns

- **Canary-based kstack overflow detection** — excellent defensive design
- **Refcount-based resource cleanup** — correct approach
- **Clean separation of validation from loading** in exec.c
- **Zero TODO/FIXME in kernel code** — impressive discipline
- **PAL abstraction** for platform independence — well designed

### 3.2 Improvement Opportunities

- **Pipe type discrimination**: Reusing `inode` pointer field to store pipe
  index is fragile. A union or explicit type flag in `struct ofile` would
  be clearer and prevent misinterpretation.

- **Error handling consistency**: Some `fs_write()` calls don't check return
  values (e.g., `dir_unlink`). Either check all returns or document why
  they're safe to ignore.

- **Duplicate computation in exec.c**: `reloc_bytes = hdr.reloc_count * 4`
  computed twice in nearby lines. Minor but indicative of code that evolved
  incrementally.

---

## 4. Curses Assessment

### Is Curses Worth It?

**Yes, but as lightweight ANSI-based, not full ncurses.** The recommended
approach from vdp-research.md:

1. **ANSI escape parser in TTY** (~200 lines) — independently valuable.
   levee already emits ANSI sequences that are currently ignored on VDP.
   This enables color output for all programs for free.

2. **Palette setup** (~20 lines) — fill unused VDP palette slots with ANSI
   colors. 8 normal + 8 bright using two palettes.

3. **Minimal curses.c** (~300-500 lines) — emit ANSI escapes via TTY.
   Portable (works on both workbench and Mega Drive). Enables tetris,
   snake, adventure, and other TUI apps.

**What NOT to do:** Full VDP emulation in curses. We have BlastEm for that.
Curses should be a thin ANSI layer, not a graphics engine.

**RAM cost:** ~20 bytes curses state + ~500 bytes kernel ANSI parser.
Negligible.

---

## 5. BlastEm Configuration & Extended RAM

### Current BlastEm Config

The Makefile defines a headless config for testing:
```makefile
define BLASTEM_HEADLESS_CFG
video {\n\tgl off\n\tvsync off\n}\nsystem {\n\tram_init zero\n}
endef
```

### Extended RAM in BlastEm

BlastEm supports several mapper types relevant to extended RAM:

1. **SRAM emulation** — Already working. 8 KB at 0x200001 (odd bytes),
   controlled by 0xA130F1.

2. **SSF mapper** — Supported in BlastEm. Enables 512 KB+ banked PSRAM
   for EverDrive Pro emulation. This is the path to extended RAM.

3. **Bank switching** — BlastEm supports Sega mapper registers (0xA130Fx).
   Each register maps a 512 KB bank to a ROM window.

### Recommendation: BlastEm Extended RAM Plan

**Goal:** Test Phase 8 (PSRAM) without real hardware.

**Steps:**
1. Create a BlastEm ROM header with `SEGA SSF` identifier
2. Configure bank registers (0xA130F0-0xA130FF) for writable PSRAM
3. Test kernel bank allocator in BlastEm before flashing hardware
4. Document BlastEm-specific config in `docs/blastem-config.md`

**Blocker investigation needed:** Verify BlastEm's SSF mapper fidelity —
does it accurately emulate writable banks? This can be tested with a
small test ROM before committing to the approach.

---

## 6. Stale Documentation Report

See companion document: `docs/stale-documentation-report-2026-03-17.md`

Key items:
- README.md `apps-md` target references stale 0xFF8000 link address
- Makefile `test-levee` marked "KNOWN BROKEN" but `-msep-data` fix merged
- PLAN.md contains completed phases that should move to HISTORY.md
- Line counts in README/CLAUDE.md are slightly stale

---

## 7. PR / Branch Status

### Active Branches

Only `claude/kernel-review-roadmap-EHHKY` (this review) and `main`.

### Recent PRs

- **PR #60** (merged): Fixed vfork+forkshell heap corruption, added rmdir,
  improved cat error messages, comprehensive pipe tests
- Previous PRs: VDP optimization research, documentation reorganization,
  variable-size allocator, Bug 18 fix

### Notable: Zero Open PRs

The project has no pending work-in-progress branches. All prior work is
merged to main.

---

## 8. Organization & Cleanliness Improvements

### 8.1 For Hardware-Specific Changes

When hardware-specific changes land (Phase 7 SD card, Phase 8 PSRAM),
the following organizational prep would help:

1. **PAL interface documentation** — Document the full PAL API contract
   in `docs/architecture.md`. Currently PAL functions are discoverable
   only by reading the source. A clear interface spec makes it obvious
   what new hardware support must implement.

2. **Platform config header** — Consider a `platform_config.h` generated
   by the build system with platform constants (TIMER_HZ, default
   winsize, SRAM base, etc.) instead of scattered #defines.

3. **Device driver registration** — The current device table in dev.c is
   static. When adding SD card as a new device, the pattern should be
   documented. Consider a `docs/adding-devices.md` checklist similar
   to the existing "Adding new syscalls" checklist in CLAUDE.md.

### 8.2 Build System

1. **Verify `-msep-data` everywhere** — Add a Makefile check that greps
   all app Makefiles for `-msep-data` and fails if any are missing.
   This is the most expensive class of bug in the project's history.

2. **BlastEm config documentation** — Document what BlastEm config options
   are used, why `ram_init zero` matters, and how to add mapper support
   for Phase 8 testing.

### 8.3 Test Infrastructure

1. **Add test-dash.sh to make test-all** — If not already included, this
   is the primary way to catch dash regression (Bug 18 wasn't caught by
   autotest alone).

2. **CI pipeline** — Even a simple GitHub Actions workflow running
   `make test` on push would catch host-test regressions.

---

## 9. What We Want To Do (Summary)

### Definitely Do (software-only, high value)

1. Fix the bugs identified in Section 1 (especially 1.1, 1.2, 1.3)
2. Add missing tests (especially TRAP #0 path, XIP, multi-stage pipes)
3. ANSI escape parser in TTY (enables color for all programs)
4. Phase 9 performance optimizations (divmod fast path, memcpy/memset)
5. Clean up stale documentation
6. Minimal curses library (thin ANSI layer)
7. Port Wave 3 games (trivial stdio games, quick wins)

### Plan But Don't Build Yet (needs more investigation)

1. BlastEm SSF mapper testing for Phase 8 PSRAM
2. Device driver open/close dispatch (VDP exclusive access)
3. Build system `-msep-data` enforcement

### Skip (not worth doing now)

1. Complex VDP emulation — BlastEm handles this
2. Full ncurses — too large, ANSI-based curses is sufficient
3. CI pipeline — nice but not blocking anything
4. User pointer validation — defense-in-depth, single-user system
