# Genix Forward Plan

What remains to be built. For project history and completed phases,
see [HISTORY.md](HISTORY.md).

Current state: Genix is a working preemptive multitasking OS with 47
user programs (including dash shell and 13 tier-1 utilities),
relocatable binaries, pipes, signals, job control, and a TTY
subsystem with ANSI escape sequence support and a minimal curses
library for TUI apps. It runs on both the workbench emulator and real
Mega Drive hardware. ROM XIP is working on Mega Drive (text executes
from ROM, only .data copied to RAM). Phase 6 (`-msep-data` +
variable-size user memory allocator) is complete. Pipelines execute
concurrently with bulk memcpy optimization. Phases A-D (libc, kernel
enhancements, dash shell, line editing), Tier 1 apps, VDP color
terminal (V1-V4), and core performance optimizations (Phase 9) are
complete. The kernel spawns dash as the default interactive shell with
arrow key cursor movement, command history, and in-line editing. The
VDP console supports bold text, cursor positioning, and screen
clearing via ANSI escapes. See
[docs/plans/apps_to_port.md](docs/plans/apps_to_port.md) for the app
porting roadmap.

For completed phase outcomes (Phases 5, 6, A, B, C, D), see
[HISTORY.md](HISTORY.md). For hardware plans (SD card, EverDrive Pro
PSRAM), see [docs/plans/](docs/plans/).

---

## NEXT: BlastEm Visual Review & Hardware Validation

**Priority:** CRITICAL — must be done before any new feature work.
**Status:** Not started.

The VDP terminal (ANSI parser, bold palette, curses) and performance
optimizations (DIVU.W, assembly memcpy, pipe bulk copy) were all
validated via host unit tests only. **None of this has been visually
verified in BlastEm.** The host tests prove logic correctness but
cannot detect:

- VDP nametable palette bit corruption (wrong colors)
- ANSI cursor positioning rendering incorrectly
- Bold text visually indistinguishable from normal
- Scroll/clear leaving artifacts
- Assembly memcpy/memset causing alignment faults on 68000
- Division fast path DIVU.W edge cases on real CPU
- Pipe bulk copy corrupting data through memcpy alignment issues

### Step 1: Create `apps/vdptest.c` — VDP terminal test program

A dedicated test app that exercises every ANSI sequence and curses
feature, then holds the display for screenshot capture. Pattern after
`apps/imshow.c` (use `-n` flag for no-wait automated mode).

**What it must test (visually):**
1. Normal text output — "Hello World" at default position
2. Bold text — `ESC[1m` should render in bright white (palette 3)
3. Bold reset — `ESC[0m` back to normal gray (palette 0)
4. Cursor positioning — `ESC[5;10H` place text at specific row/col
5. Screen clear — `ESC[2J` should blank the entire display
6. Line clear — `ESC[K` should clear to end of current line
7. Cursor movement — `ESC[A/B/C/D` should move in 4 directions
8. Scroll — fill screen with text, verify scroll behavior
9. Cursor show/hide — `ESC[?25l` / `ESC[?25h`
10. Word wrap — long lines should wrap at column 40

**Implementation notes:**
- Create `apps/vdptest.c` with `-n` flag (automated: hold ~2s then exit)
- Add to `apps/Makefile` PROGRAMS list and top-level `CORE_BINS`
- Add `apps/vdptest` to `.gitignore`
- Add `VDP_TEST` kernel mode (like `IMSHOW_TEST`) that spawns the test
- Add `make test-md-vdptest` target that captures screenshot

**Where to look:**
- apps/imshow.c — reference for `-n` flag pattern
- kernel/main.c lines 813-839 — IMSHOW_TEST pattern
- Makefile lines 142-177 — test-md-imshow screenshot capture pattern
- pal/megadrive/platform.c lines 76-400 — the ANSI parser being tested
- pal/megadrive/vdp.S — palette data (verify palette 3 = bright white)

### Step 2: Run BlastEm autotest

```bash
make test-md-auto          # Crash check (existing 28 tests)
make test-md-screenshot    # Visual: autotest output on VDP
make test-md-vdptest       # Visual: VDP terminal test patterns
make test-md-imshow        # Visual: graphics stack (existing)
```

Inspect each screenshot for rendering correctness.

### Step 3: Validate assembly optimizations

The autotest already exercises pipes, exec, and filesystem — all of
which use the new assembly memcpy and DIVU.W fast path. If autotest
passes, the optimizations are correct on 68000.

Additional validation:
- Run `cat /bin/hello | wc` in dash to exercise pipe bulk copy
- Run `ls /bin` to exercise kprintf division (formats numbers)
- Run levee to exercise ANSI cursor positioning end-to-end

### Step 4: Review and fix any issues found

Document findings in the vdp-terminal-plan.md Outcome section.

---

## Phase 9: Performance Optimizations — mostly done

**Goal:** Bring 68000-specific assembly optimizations to hot paths.

**Status (2026-03-17):** 3 of 5 optimizations complete. Division fast
path (DIVU.W), assembly memcpy/memset/memmove (MOVEM.L), and pipe bulk
copy are implemented and tested. SRAM 16-bit I/O and VDP DMA clear are
deferred to hardware testing.

| Optimization | Status | Lines |
|-------------|--------|-------|
| Division fast path (DIVU.W for 16-bit divisors) | **Done** | ~26 |
| Assembly memcpy/memset (MOVEM.L bulk) | **Done** | ~192 |
| Pipe bulk copy (replace byte loop) | **Done** | ~15 |
| SRAM 16-bit I/O (word writes vs byte writes) | Deferred | — |
| VDP DMA for scroll/clear | Deferred | — |

### Reference

[docs/plans/optimization-plan.md](docs/plans/optimization-plan.md) — full analysis with
FUZIX source references, cycle counts, implementation report, and
lessons learned.

---

## VDP Color Terminal + Curses — done

**Status (2026-03-17):** Complete. ANSI escape parser, bold palette,
and curses library are implemented and tested on host. **Awaiting
BlastEm visual validation** (see "NEXT" section above).

**What shipped:**
- ANSI escape parser in pal/megadrive/platform.c (~200 lines, 4-state machine)
- VDP palette 3 for bold text (bright white on black, 0 VRAM cost)
- Minimal curses library: libc/curses.c (~460 lines) + curses.h (~119 lines)
- Device open/close dispatch, console suppression, FD_CLOEXEC
- 108 ANSI parser tests, 36+ curses tests, 523 pipe bulk assertions, 120 syscall tests

**What's deferred:**
- V3b: 8-color support (requires multiple font copies in VRAM, ~21 KB)
- VDP DMA clear and SRAM 16-bit I/O (need hardware testing)
- curses getch() timeout (VTIME support in tty_read)

### Reference

[docs/plans/vdp-terminal-plan.md](docs/plans/vdp-terminal-plan.md) — full plan,
implementation report, deviations, and remaining work.

---

## Remaining Optional Work

Not prioritized, but would improve the system:

- **Tier 2 games**: hamurabi, dopewars, startrek, adventure, tetris, snake (curses ready)
- **Tier 3 text processing**: sed, diff, cal, date (needs localtime libc), ed (likely skip — levee covers the editor use case)
- **Tier 4 languages**: BASIC interpreter, Forth, fweep (Z-machine)
- **Development tools**: ar, make, small C compiler (from FUZIX)
- **SA_RESTART**: auto-retry syscalls interrupted by signals
- **V3b 8-color terminal**: 7 extra font copies for ANSI color (stubs in SGR handler ready)
- **VDP DMA clear**: ~10x screen clear speedup (deferred to hardware)
- **SRAM 16-bit I/O**: ~20x SRAM disk speedup (deferred to hardware)

See [docs/plans/apps_to_port.md](docs/plans/apps_to_port.md) for the complete
app porting roadmap with RAM analysis and wave breakdown.

---

## Phase Dependencies

```
Phase 5 (ROM XIP) .............. done
    |
Phase 6 (-msep-data + alloc) .. done
    |
Libc prereqs (Phase A) ....... done
    |
Kernel prereqs (Phase B) ..... done
    |
dash Shell Port (Phase C) .... done
    |
Line Editing (Phase D) ....... done
    |
Phase 9 (Performance) ......... mostly done (3/5 optimizations)
    |
VDP Color Terminal + Curses .. done (V1-V4 complete, host-tested)
    |
BlastEm Visual Review ....... ← NEXT (critical before new features)
    |
Tier 2 TUI Games ............ unblocked (curses ready)
```
