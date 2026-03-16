# Genix Forward Plan

What remains to be built. For project history and completed phases,
see [HISTORY.md](HISTORY.md).

Current state: Genix is a working preemptive multitasking OS with 47
user programs (including dash shell and 13 tier-1 utilities),
relocatable binaries, pipes, signals, job control, and a TTY
subsystem. It runs on both the workbench emulator and real Mega Drive
hardware. ROM XIP is working on Mega Drive (text executes from ROM,
only .data copied to RAM). Phase 6 (`-msep-data` + variable-size user
memory allocator) is complete: multiple processes can reside in memory
simultaneously with shared ROM text and per-process data regions.
Pipelines execute concurrently. Phase A (libc prerequisites), Phase B
(kernel enhancements: fcntl F_DUPFD, waitpid WNOHANG), Phase C (dash
shell port), Phase D (line editing for dash), and Tier 1 apps (cp, mv,
rm, mkdir, touch, kill, which, uname, clear, more, sort, find, xargs)
are complete. The kernel spawns dash as the default interactive shell
with arrow key cursor movement, command history (up/down), and in-line
editing. See [docs/plans/apps_to_port.md](docs/plans/apps_to_port.md) for the app
porting roadmap.

For completed phase outcomes (Phases 5, 6, A, B, C, D), see
[HISTORY.md](HISTORY.md). For hardware plans (SD card, EverDrive Pro
PSRAM), see [docs/plans/](docs/plans/).

---

## Phase 9: Performance Optimizations

**Goal:** Bring 68000-specific assembly optimizations to hot paths.

These are performance gaps identified by comparing Genix's C
implementations against FUZIX's hand-optimized 68000 assembly. None
are correctness issues — Genix works correctly today. These are pure
speed improvements for when performance matters.

### Key Optimizations

| Optimization | Expected Speedup | Lines |
|-------------|-----------------|-------|
| Division fast path (DIVU.W for 16-bit divisors) | 2-5x division | ~20 |
| Assembly memcpy/memset (MOVEM.L bulk) | 4x block ops | ~40 |
| SRAM 16-bit I/O (word writes vs byte writes) | ~20x SRAM | ~10 |
| Pipe bulk copy (replace byte loop) | 2-4x pipe throughput | ~15 |
| VDP DMA for scroll/clear | ~10x scroll | ~30 |

### Approach

Measure first, optimize only hot paths. The workbench emulator
provides cycle counting for profiling. Optimize the inner loops that
show up in traces, leave cold paths in C.

### Reference

[docs/plans/optimization-plan.md](docs/plans/optimization-plan.md) — full analysis with
FUZIX source references, cycle counts, and implementation notes for
each optimization.

---

## VDP Color Terminal + Curses

**Goal:** ANSI color support in the VDP console, and a minimal curses
library for full-screen apps.

**Difficulty:** Moderate (~200 lines kernel, ~300-500 lines curses).
The VDP hardware already supports color — we're just not using it.

### Why

Color makes the terminal usable for real work: syntax-highlighted
editors, colored ls output, shell prompts, status bars, games with
visual distinction. A curses library unlocks full-screen TUI apps
(tetris, snake, adventure, editors with status lines) without every
app reimplementing screen management.

### VDP Color: What's Needed

The nametable entry is a 16-bit word with palette select bits already
present but unused:

```
Bit 15:    Priority
Bit 13-12: Palette select (0-3) ← currently always 0
Bit 11:    V-flip
Bit 10:    H-flip
Bit 9-0:   Tile index
```

**1. Palette data (~16 words in vdp.S):** Fill the 13 unused color
slots in palette 0 with the 8 standard ANSI colors (black, red, green,
yellow, blue, magenta, cyan, white). Use palette 3 (currently reserved)
for the 8 bright/bold variants. That gives 16 foreground colors using
2 palettes.

**2. Attribute tracking (~5 lines in platform.c):** Add a
`current_attr` word that `pal_console_putc()` ORs into the tile index
before calling `plot_char()`. Palette bits encode the current color.

**3. ANSI escape sequence parser (~150-200 lines in tty.c):** The TTY
layer currently has **no escape sequence parsing** — characters go
straight through. Need a state machine that intercepts:
- `ESC[...m` (SGR) — set foreground/background color, bold, reset
- `ESC[...H` (CUP) — cursor positioning (levee already emits these)
- `ESC[J` / `ESC[K` — clear screen / clear line
- `ESC[...A/B/C/D` — cursor movement

This is independently valuable: levee already emits ANSI sequences
for cursor positioning, and they currently go nowhere on the VDP.

**4. Emulator (free):** The workbench emulator uses UART → host
stdout. ANSI escapes pass through to the host terminal, which renders
colors natively. No emulator changes needed.

### Curses Library

A minimal curses implementation for Genix (~300-500 lines in
`libc/curses.c`):

**Core API:**
- `initscr()` / `endwin()` — setup/teardown (raw mode, clear screen)
- `move(y, x)` / `addch(c)` / `addstr(s)` / `mvaddstr()` — output
- `attron()` / `attroff()` / `attrset()` — color/bold attributes
- `clear()` / `clrtoeol()` / `refresh()` — screen management
- `getch()` — input (raw mode, escape sequence decoding)
- `init_pair()` / `COLOR_PAIR()` — color pair management
- `getmaxy()` / `getmaxx()` — terminal size (40×28 on MD, from env)

**What we skip:** Windows/subwindows, scrolling regions, mouse,
wide characters. One stdscr, one physical screen. This is closer
to "minicurses" than full ncurses.

**Implementation approach:** On Mega Drive, curses can either:
- (A) Emit ANSI escapes through the TTY (works on both platforms), or
- (B) Write directly to VDP nametable via `/dev/vdp` ioctls (faster,
  MD-only, would need a fallback path for workbench)

Option A is simpler and portable. Option B avoids the escape
parser overhead for apps that do heavy screen updates (games).
Decision deferred until implementation.

**RAM cost:** Minimal — curses state is ~20 bytes (cursor pos,
current attr, window size). No screen buffer needed if using
option A (emit-as-you-go). With option B (direct VDP), a 40×28
= 1120-byte shadow buffer would enable `refresh()` diffing.

### Palette Budget

```
Palette 0: Console text (currently 3 used, 13 free → ANSI normal)
Palette 1: Debug overlay (3 used, 13 free)
Palette 2: Cursor sprite (3 used, 13 free)
Palette 3: Reserved (16 free → ANSI bright/bold)
```

The font is 1bpp expanded to 4bpp at boot. Each pixel is either
color index 1 (background) or color index 2 (foreground) within
the selected palette. Changing palette select bits per-character
gives different fg/bg color pairs at zero CPU cost — the VDP
hardware does all the work.

**Limitation:** With 2-color font tiles, we get colored foreground
on black background, or inverse (colored background with black
foreground). True arbitrary fg+bg pairs would require multiple
copies of the font with different color indices, eating VRAM. Not
worth it for a text terminal.

### Implementation Order

1. ANSI escape parser in TTY (biggest independent value)
2. Palette data + attribute tracking (enables color on MD)
3. Curses library (enables TUI apps)

### Dependencies

- No hard dependencies on other phases
- Soft dependency: games (Tier 2) and editors benefit from curses
- The escape parser benefits levee immediately (cursor positioning)

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
Phase 9 (Performance) ......... independent, can happen anytime
```

Phase 9 (performance) can happen anytime. VDP Color Terminal + Curses
has no hard dependencies.
