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

## Phase 9: Performance Optimizations — mostly done

**Goal:** Bring 68000-specific assembly optimizations to hot paths.

**Status (2026-03-17):** 3 of 5 optimizations complete. Division fast
path (DIVU.W), assembly memcpy/memset/memmove (MOVEM.L), and pipe bulk
copy are implemented and tested. SRAM 16-bit I/O and VDP DMA clear are
deferred to hardware testing.

| Optimization | Status | Lines |
|-------------|--------|-------|
| Division fast path (DIVU.W for 16-bit divisors) | **Done** | ~26 |
| Assembly memcpy/memset (MOVEM.L bulk) | **Done** | ~205 |
| Pipe bulk copy (replace byte loop) | **Done** | ~15 |
| SRAM 16-bit I/O (word writes vs byte writes) | Deferred | — |
| VDP DMA for scroll/clear | Deferred | — |

### Reference

[docs/plans/optimization-plan.md](docs/plans/optimization-plan.md) — full analysis with
FUZIX source references, cycle counts, and implementation notes for
each optimization.

---

## VDP Color Terminal + Curses — done

**Status (2026-03-17):** Complete. ANSI escape parser, bold palette,
and curses library are implemented and tested. See
[docs/plans/vdp-terminal-plan.md](docs/plans/vdp-terminal-plan.md)
for the full plan and outcome.

**What shipped:**
- ANSI escape parser in pal/megadrive/platform.c (~200 lines)
- VDP palette 3 for bold text (bright white on black, 0 VRAM cost)
- Minimal curses library: libc/curses.c (~460 lines) + curses.h
- Device open/close dispatch, console suppression, FD_CLOEXEC
- 108 ANSI parser tests, 36+ curses tests, 523 pipe bulk assertions

**What's deferred:**
- V3b: 8-color support (requires multiple font copies in VRAM)
- VDP DMA clear and SRAM 16-bit I/O (need hardware testing)
- curses getch() timeout (VTIME support in tty_read)

TUI apps (games, editors with status bars) can now use the curses API.
Levee cursor positioning works on Mega Drive.

## Remaining Optional Work

Not prioritized, but would improve the system:

- **Tier 2 games**: hamurabi, dopewars, startrek, adventure, tetris, snake
- **Tier 3 text processing**: sed, diff, cal, date (needs localtime libc), ed (likely skip — levee covers the editor use case)
- **Tier 4 languages**: BASIC interpreter, Forth, fweep (Z-machine)
- **Development tools**: ar, make, small C compiler (from FUZIX)
- **SA_RESTART**: auto-retry syscalls interrupted by signals

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
VDP Color Terminal + Curses .. done (V1-V4 complete)
```

Remaining work: SRAM 16-bit I/O and VDP DMA clear (need hardware),
V3b 8-color support (needs VRAM font copies), and Tier 2 TUI games
(now unblocked by curses).
