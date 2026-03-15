# Apps to Port — Research & Recommendations

Research into applications worth porting (or rewriting) for Genix on the
Sega Mega Drive. Sources: FUZIX app collection, classic Unix utilities,
retro games, and standalone projects.

## Platform Constraints Recap

| Constraint | Value |
|------------|-------|
| CPU | 68000 @ 7.67 MHz |
| Main RAM | 64 KB total, ~40 KB user |
| Per-process data slot (MD) | ~14 KB (.data + .bss + heap + stack) |
| Text (code) | Executes from ROM (XIP), up to 4 MB |
| Display | VDP 40×28 text, tile/sprite graphics |
| Input | Saturn keyboard (50 keys) |
| No fork() | vfork()+exec() only |
| No curses | No termcap database; raw VT100/ANSI or direct VDP |
| No floating point | 68000 has no FPU; soft-float is very slow |
| No network | No TCP/IP stack |
| Existing apps | 33 coreutils + dash shell |

**Key bottleneck:** The 14 KB data slot. Text size (code) is essentially
free since it lives in ROM. But .data + .bss + heap + stack must all fit
in 14 KB per process on the Mega Drive. (Workbench has ~117 KB slots.)
Phase 8 (EverDrive Pro PSRAM) would give 512 KB per process but is not
yet implemented.

---

## Tier 1: High Priority — Maximum Impact

These fill critical gaps in the system's usability. Port or rewrite these
first.

### ed — Line Editor

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/ed.c` |
| Size | ~1,000 lines C |
| Text (ROM) | ~8-12 KB estimated |
| Data+BSS | ~1-2 KB (small static state) |
| RAM needs | Dynamic: linked list of lines via malloc |
| Needs fork() | No |
| Missing libc | None significant — uses malloc, stdio, string |
| Approach | **Port from FUZIX** |
| Value | ★★★★★ |

**Why:** The only way to create/edit files on the system. `ed` is the
classic Unix line editor — small, no screen control needed, perfect for
a 40×28 display. FUZIX's implementation is clean and portable.

**Risk:** RAM for the line buffer. Each line is a malloc'd node in a
linked list. On the Mega Drive's 14 KB slot, editing large files will
hit the heap limit quickly. But for config files, scripts, and small
programs, it's fine. Could add a line count/memory limit warning.

**Rewrite?** No. FUZIX ed is already small and battle-tested.

---

### sort — Sort Lines

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/sort.c` |
| Size | ~1,000 lines C |
| Text (ROM) | ~8-10 KB |
| Data+BSS | ~2-4 KB static buffers |
| RAM needs | Reads file into memory; falls back to temp files |
| Needs fork() | No |
| Missing libc | `mkstemp()` or temp file creation |
| Approach | **Port from FUZIX, reduce default buffer** |
| Value | ★★★★★ |

**Why:** Essential Unix tool. Shell scripts and pipelines need `sort`.
Combined with `uniq` (already present), enables real data processing.

**Risk:** FUZIX sort tries to allocate ~40 KB for sorting. Must reduce
to fit in 14 KB slot. The external-merge fallback (temp files) needs
a writable filesystem. On ROM-only Mega Drive without SD card, sorting
is limited to what fits in RAM.

**Rewrite?** Consider a simpler rewrite that reads stdin into a
fixed-size buffer, sorts in-place with qsort (already in libc), and
outputs. ~100-150 lines. Loses multi-file merge and field keys but
handles 90% of use cases.

---

### diff — File Comparison

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/V7/cmd/diff.c` or write new |
| Size | ~800-1,200 lines (V7 diff) |
| Text (ROM) | ~8-12 KB |
| Data+BSS | ~2-4 KB |
| RAM needs | O(n) for file lengths (edit script) |
| Needs fork() | No (core algorithm doesn't) |
| Missing libc | None significant |
| Approach | **Port V7 diff or rewrite minimal version** |
| Value | ★★★★☆ |

**Why:** Comparing files is fundamental. Also useful for verifying edits
made with `ed`. The V7 version is the classic minimal implementation.

**Risk:** RAM for the edit distance matrix. V7 diff uses Hunt-McIlwain
which is O(n) auxiliary space. Large files won't fit. Acceptable — diff
on small files (scripts, configs) is the expected use case.

**Rewrite?** A minimal diff (~300-400 lines, simple LCS) might be
cleaner than porting V7's archaic C style. Tradeoff: less code to
maintain vs. proven correctness.

---

### find — File Search

| Property | Value |
|----------|-------|
| Source | Write new (FUZIX version is complex) |
| Size | ~200-400 lines |
| Text (ROM) | ~4-6 KB |
| Data+BSS | ~1 KB |
| RAM needs | Stack depth proportional to directory nesting |
| Needs fork() | No (for basic find; `-exec` needs vfork+exec) |
| Missing libc | `fnmatch()` or simple glob matching |
| Approach | **Rewrite — minimal version** |
| Value | ★★★★☆ |

**Why:** Finding files by name is basic OS functionality. Pairs with
`xargs` for batch operations. The filesystem is small, so a simple
recursive walk with `-name` and `-type` filters covers 90% of use.

**Rewrite?** Yes. A full POSIX find is overkill. Write a ~200-line
version supporting `-name PATTERN`, `-type f/d`, and printing paths.
Add `-exec` later (needs vfork+exec, not fork).

---

### xargs — Build Commands from stdin

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/xargs.c` |
| Size | ~250 lines C |
| Text (ROM) | ~3-4 KB |
| Data+BSS | <1 KB |
| RAM needs | Minimal (reads stdin, builds argv) |
| Needs fork() | Uses fork() — **must change to vfork()** |
| Missing libc | None |
| Approach | **Port from FUZIX, replace fork→vfork** |
| Value | ★★★★☆ |

**Why:** Essential complement to `find` and pipes. `find / -name '*.c'
| xargs grep TODO` is a classic Unix pattern.

**Risk:** FUZIX version uses fork(). Easy fix — it's a trivial
fork+exec pattern that maps directly to vfork+exec. ~5 lines to change.

---

### more — Pager

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/more.c` or `less.c` |
| Size | ~300-500 lines |
| Text (ROM) | ~4-6 KB |
| Data+BSS | ~1-2 KB |
| RAM needs | Line buffer; can page from file without loading all |
| Needs fork() | No |
| Missing libc | Terminal size query (TIOCGWINSZ or hardcode 40×28) |
| Approach | **Rewrite — simple pager for VDP** |
| Value | ★★★★☆ |

**Why:** Without a pager, any output longer than 28 lines scrolls off
screen. Essential for usability. `ls -l`, `cat` of any file, `help`
output — all need paging.

**Rewrite?** Yes. FUZIX less.c uses termcap. A Genix-specific pager
that knows the display is 40×28, uses raw terminal mode, and shows
"--more--" at the bottom is ~100-150 lines. Much simpler than porting
less. Could even use VDP scroll register for smooth scrolling.

---

## Tier 2: High Value — Fun & Impressive

These make the system feel alive. A Mega Drive running Unix with games
is inherently impressive.

### startrek — Star Trek Game

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/games/startrek.c` |
| Size | ~1,400 lines C |
| Text (ROM) | ~15-20 KB |
| Data+BSS | ~4-8 KB (galaxy map, ship state) |
| RAM needs | Static arrays for 8×8 galaxy + quadrant map |
| Needs fork() | No |
| Missing libc | `time()` for RNG seed (already present) |
| Approach | **Port from FUZIX** |
| Value | ★★★★★ |
| Cool factor | ★★★★★ |

**Why:** THE classic Unix game. Navigating the Enterprise on a Sega
Mega Drive is exactly the kind of thing that makes people say "that's
amazing." FUZIX's version was explicitly "put on a diet to run in 32K."

**Risk:** Data+BSS may be tight at ~4-8 KB in a 14 KB slot. Needs
measurement after cross-compilation. The 40×28 display is narrower
than the usual 80×24 — may need minor output reformatting.

**Rewrite?** No. The FUZIX version is already optimized for small
systems. May need display width adjustments.

---

### hamurabi — Ancient City Simulation

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/games/hamurabi.c` |
| Size | ~380 lines C |
| Text (ROM) | ~4-5 KB |
| Data+BSS | <1 KB |
| RAM needs | Minimal — a handful of integers |
| Needs fork() | No |
| Missing libc | None |
| Approach | **Port from FUZIX** (trivial) |
| Value | ★★★★☆ |
| Cool factor | ★★★★☆ |

**Why:** Classic 1968 strategy game. Manage land, grain, and population
for 10 turns. Tiny, fun, historically significant. The OG resource
management game.

**Risk:** None. It's tiny. Will definitely fit.

---

### dopewars — Trading Game

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/games/dopewars.c` |
| Size | ~720 lines C |
| Text (ROM) | ~8-10 KB |
| Data+BSS | <1 KB |
| RAM needs | Minimal static state |
| Needs fork() | No |
| Missing libc | `getuid()` for RNG seed (can substitute getpid) |
| Approach | **Port from FUZIX** |
| Value | ★★★☆☆ |
| Cool factor | ★★★★☆ |

**Why:** Addictive trading game. Buy low, sell high, avoid cops, pay
off debt. Simple text UI, runs perfectly in 40×28. The kind of game
you play "one more turn" for 30 minutes.

**Risk:** None. Tiny data footprint.

---

### adventure — Colossal Cave

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/cave/` |
| Size | ~2,000+ lines across multiple files |
| Text (ROM) | ~20-30 KB |
| Data+BSS | ~8-15 KB (game database, room/object state) |
| RAM needs | Large — room descriptions, parser, object graph |
| Needs fork() | No |
| Missing libc | None significant |
| Approach | **Port from FUZIX** (needs data size audit) |
| Value | ★★★★☆ |
| Cool factor | ★★★★★ |

**Why:** THE original text adventure. "You are in a maze of twisty
little passages, all alike." Running Colossal Cave on a Mega Drive
is legendary. FUZIX has a complete port.

**Risk:** Data size is the blocker. The game database (room
descriptions, connections, object properties) could easily exceed
14 KB. Need to measure after cross-compilation. **May require Phase 8
(PSRAM)** on Mega Drive, but would work fine on workbench. Could also
store room strings in ROM (const data trick).

**Rewrite?** No — the game data is the bulk, and it needs to be
faithful. Consider a smaller adventure (Scott Adams engine, below).

---

### Scott Adams Adventures (adv01-adv14b)

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/games/` |
| Size | ~500-800 lines (shared engine + data files) |
| Text (ROM) | ~8-12 KB |
| Data+BSS | ~2-4 KB per game |
| RAM needs | Much smaller than Colossal Cave |
| Needs fork() | No |
| Missing libc | None |
| Approach | **Port from FUZIX** |
| Value | ★★★★☆ |
| Cool factor | ★★★★★ |

**Why:** 14 classic text adventures with a shared compact engine.
"Pirate Adventure," "Adventureland," "Voodoo Castle," etc. These
were designed for 8-bit computers with 16-32 KB RAM — they'll fit
easily. One engine, 14 games in ROM.

**Risk:** Low. These were literally built for systems smaller than ours.

---

### Hunt the Wumpus — Classic Cave Game

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/V7/games/wump.c` |
| Size | ~200-300 lines C |
| Text (ROM) | ~3-4 KB |
| Data+BSS | <1 KB |
| Needs fork() | No |
| Missing libc | None |
| Approach | **Port from FUZIX** (trivial) |
| Value | ★★★☆☆ |
| Cool factor | ★★★★☆ |

**Why:** One of the earliest computer games (1973). Navigate a
dodecahedral cave system, shoot arrows at the Wumpus, avoid pits and
bats. Tiny, fun, historically significant.

---

### V7 Mini-Games Collection

| Game | Source | Size | Description |
|------|--------|------|-------------|
| hangman | `V7/games/hangman.c` | ~200 lines | Word guessing (needs word list) |
| fish | `V7/games/fish.c` | ~200 lines | Go Fish card game |
| arithmetic | `V7/games/arithmetic.c` | ~150 lines | Math practice drills |
| ttt | `MWC/cmd/ttt.c` | ~150 lines | Tic-tac-toe with AI |
| moo | `MWC/cmd/moo.c` | ~100 lines | Mastermind number variant |
| cowsay | `games/cowsay.c` | ~100 lines | ASCII cow says your text |

All are trivial ports: stdio only, no fork, no terminal control, <1 KB
data each. Could port the entire collection in one session.

---

### fortune — Random Quotes

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/games/fortune.c` |
| Size | ~65 lines C + fortune database |
| Text (ROM) | ~1-2 KB |
| Data+BSS | <1 KB |
| RAM needs | Minimal — reads one fortune from file |
| Needs fork() | No |
| Missing libc | `ntohs()` (trivial macro) |
| Approach | **Port from FUZIX + create fortune.dat** |
| Value | ★★★☆☆ |
| Cool factor | ★★★☆☆ |

**Why:** Login message, shell prompt decoration, just fun to have.
Makes the system feel like a real Unix box. The fortune database
lives in ROM.

**Risk:** Need to create/curate fortune.dat. The program itself is
trivial. Could include programming quotes, Unix history, Mega Drive
trivia.

---

### Tetris — The Classic

| Property | Value |
|----------|-------|
| Source | Rewrite (~230-400 lines, no curses) |
| Text (ROM) | ~3-5 KB |
| Data+BSS | ~1-2 KB (10×20 board + piece shapes) |
| RAM needs | ~500 bytes game state |
| Needs fork() | No |
| Missing libc | Non-blocking input (termios raw — already present) |
| Approach | **Rewrite for VDP** |
| Value | ★★★★★ |
| Cool factor | ★★★★★ |

**Why:** Tetris on a Mega Drive running under a Unix shell. The 40×28
display is plenty for a 10×20 board. Could use VDP tiles for proper
block graphics — this would look like a real Mega Drive game but
launched from `$ tetris` at a shell prompt.

**Challenge:** Non-blocking keyboard input + timer for piece drop.
Need termios raw mode + poll/timeout. The Saturn keyboard latency
will be the limiting factor, not CPU.

---

### Snake — Action Game

| Property | Value |
|----------|-------|
| Source | Rewrite (~200-300 lines) |
| Text (ROM) | ~2-4 KB |
| Data+BSS | ~1-2 KB (40×28 board + snake body) |
| RAM needs | ~1.2 KB |
| Needs fork() | No |
| Missing libc | Non-blocking input, timer/delay |
| Approach | **Rewrite for VDP** |
| Value | ★★★★☆ |
| Cool factor | ★★★★★ |

**Why:** Action game on a game console! Uses ANSI escapes or VDP
tiles for real-time gameplay. The 40×28 grid is a natural play field.

**Challenge:** Same as Tetris — non-blocking input + game timer.

---

### 2048 — Tile Puzzle

| Property | Value |
|----------|-------|
| Source | mevdschee/2048.c (~350 lines, single file) |
| Text (ROM) | ~2-3 KB |
| Data+BSS | <1 KB (4×4 grid + score) |
| RAM needs | ~200 bytes |
| Needs fork() | No |
| Missing libc | Raw terminal input (termios — already present) |
| Approach | **Port/adapt for Genix** |
| Value | ★★★☆☆ |
| Cool factor | ★★★★☆ |

**Why:** Modern classic, simple rules, addictive. The mevdschee
version is already minimal. Could use VDP tiles for a nice visual.

---

### Robots — Classic BSD Game

| Property | Value |
|----------|-------|
| Source | Rewrite (~200-300 lines, no curses) |
| Text (ROM) | ~3-4 KB |
| Data+BSS | ~1 KB |
| Needs fork() | No |
| Approach | **Rewrite** |
| Value | ★★☆☆☆ |
| Cool factor | ★★★☆☆ |

Player moves on grid, robots chase, robots crash into each other.
Simple turn-based logic, satisfying gameplay.

---

## Tier 3: Useful Utilities — Nice to Have

These round out the system. Less critical than Tier 1 but each adds
real capability.

### sed — Stream Editor

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/sed.c` |
| Size | ~1,400 lines C |
| Text (ROM) | ~12-16 KB |
| Data+BSS | ~2-4 KB (pattern/hold space, command buffer) |
| RAM needs | Pattern space + hold space (line-at-a-time) |
| Needs fork() | No |
| Missing libc | Regex (already present — `regcomp`/`regexec`) |
| Approach | **Port from FUZIX** |
| Value | ★★★★☆ |

**Why:** sed + grep + sort = real text processing pipeline. Shell
scripts need sed for substitution. `s/foo/bar/g` is muscle memory
for any Unix user.

**Risk:** Pattern/hold space buffers need sizing for 14 KB slot.
FUZIX version should be manageable — it's designed for small systems.

---

### cal — Calendar

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/cal.c` |
| Size | ~430 lines C |
| Text (ROM) | ~4-5 KB |
| Data+BSS | <1 KB |
| RAM needs | Minimal |
| Needs fork() | No |
| Missing libc | `time()` (present), `localtime()` (**missing**) |
| Approach | **Port from FUZIX, add minimal localtime()** |
| Value | ★★★☆☆ |

**Why:** Classic Unix utility. `cal 2026` on a Mega Drive. Small,
self-contained. Makes the system feel complete.

**Risk:** Needs `localtime()` or `gmtime()` to get the current
month/year from `time()`. This is a ~50-line libc addition (epoch
seconds → year/month/day/etc). Worth adding anyway — other apps
will use it.

---

### date — Print Date/Time

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/date.c` or rewrite |
| Size | ~100-200 lines |
| Text (ROM) | ~2-3 KB |
| Data+BSS | <1 KB |
| Needs fork() | No |
| Missing libc | `localtime()`, `strftime()` |
| Approach | **Rewrite** (tiny) |
| Value | ★★★☆☆ |

**Why:** Pairs with `cal`. Shows the system has a sense of time.

---

### cp — Copy Files

| Property | Value |
|----------|-------|
| Source | Rewrite |
| Size | ~80-150 lines |
| Text (ROM) | ~2-3 KB |
| Data+BSS | <1 KB + copy buffer |
| Needs fork() | No |
| Missing libc | None |
| Approach | **Rewrite** |
| Value | ★★★★☆ |

**Why:** Can't copy files without `cp`. Basic filesystem operation.
Trivial to implement: open src, open dst, read/write loop, close.

---

### mv — Move/Rename Files

| Property | Value |
|----------|-------|
| Source | Rewrite |
| Size | ~50-80 lines |
| Text (ROM) | ~1-2 KB |
| Data+BSS | <1 KB |
| Needs fork() | No |
| Missing libc | None (rename syscall exists) |
| Approach | **Rewrite** |
| Value | ★★★★☆ |

**Why:** Pairs with cp. Uses the existing rename() syscall for
same-filesystem moves, or cp+unlink for cross-filesystem.

---

### rm — Remove Files

| Property | Value |
|----------|-------|
| Source | Rewrite |
| Size | ~60-100 lines |
| Text (ROM) | ~1-2 KB |
| Data+BSS | <1 KB |
| Needs fork() | No |
| Missing libc | None (unlink syscall exists) |
| Approach | **Rewrite** |
| Value | ★★★★☆ |

**Why:** Can't delete files without rm. Uses unlink(). Add `-r` for
recursive directory removal later.

---

### touch — Create/Update Files

| Property | Value |
|----------|-------|
| Source | Rewrite |
| Size | ~30-50 lines |
| Text (ROM) | ~1 KB |
| Data+BSS | <1 KB |
| Needs fork() | No |
| Missing libc | `utime()` or just open+close |
| Approach | **Rewrite** |
| Value | ★★★☆☆ |

---

### mkdir (standalone) — Create Directories

Already exists as a syscall but no standalone `mkdir` command yet.
~20 lines.

---

### which — Find Command in PATH

| Property | Value |
|----------|-------|
| Source | Rewrite |
| Size | ~40-60 lines |
| Needs fork() | No |
| Approach | **Rewrite** |
| Value | ★★★☆☆ |

---

### uname — System Info

| Property | Value |
|----------|-------|
| Source | Rewrite |
| Size | ~20-30 lines |
| Needs fork() | No |
| Approach | **Rewrite** |
| Value | ★★☆☆☆ |

Prints "Genix 68000 megadrive". Tiny but gives the system identity.

---

### kill — Send Signals

| Property | Value |
|----------|-------|
| Source | Rewrite |
| Size | ~50-80 lines |
| Needs fork() | No |
| Missing libc | `atoi()` (present), `kill()` syscall (present) |
| Approach | **Rewrite** |
| Value | ★★★★☆ |

**Why:** Job control needs `kill`. `kill %1`, `kill -9 PID`. Essential
for managing background processes.

---

### test / [ — Condition Testing

| Property | Value |
|----------|-------|
| Source | Rewrite or port V7 |
| Size | ~200-300 lines |
| Needs fork() | No |
| Approach | **Rewrite** |
| Value | ★★★★☆ |

**Why:** Shell scripts need `test -f file`, `[ "$x" = "y" ]`, etc.
dash has a built-in `test` but a standalone `/bin/test` (symlinked as
`[`) is expected by some scripts.

Note: dash already has `test` as a built-in, so this is lower priority.

---

### dd — Data Copy

| Property | Value |
|----------|-------|
| Source | FUZIX or rewrite |
| Size | ~200-400 lines |
| Needs fork() | No |
| Approach | **Rewrite minimal version** |
| Value | ★★★☆☆ |

**Why:** Block-level copy. Useful for disk operations, creating images,
raw device access. The `dd if=/dev/disk of=backup bs=1024` pattern.

---

## Tier 4: Aspirational — Cool But Challenging

These push the platform's limits. Some may require Phase 8 (PSRAM) on
Mega Drive but work on the workbench immediately.

### BASIC Interpreter (fforth or fuzixbasic)

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/basic/` or `Applications/util/fforth.c` |
| Size | ~2,000-2,400 lines C |
| Text (ROM) | ~20-30 KB |
| Data+BSS | ~4-8 KB (variable/stack space) |
| RAM needs | Program storage + variables in heap |
| Needs fork() | No |
| Missing libc | Minimal |
| Approach | **Port FUZIX BASIC** or **rewrite minimal Forth** |
| Value | ★★★★☆ |
| Cool factor | ★★★★★ |

**Why:** A programming language ON the Mega Drive. Write BASIC programs
on the system itself. This is what 1980s home computers did — turning
the Mega Drive into one is deeply satisfying.

FUZIX BASIC is tokenized (faster execution, less RAM) and designed for
small systems. Forth (fforth) is even more memory-efficient but less
accessible.

**Recommendation:** Port FUZIX BASIC first (more familiar to users),
then Forth if there's interest.

**Smaller alternatives:**
- **uBASIC** (Adam Dunkels, ~700 lines) — extremely tiny, runs in
  <1 KB working memory. Only integer variables (A-Z), but supports
  IF/THEN, FOR/NEXT, GOSUB, PRINT. Perfect for 14 KB slot.
- **zForth** (~3-4 KB compiled kernel) — designed for "extending
  embedded applications on small microprocessors." Even smaller than
  fforth, configurable dictionary size.

**Risk:** Heap space for the BASIC program and variables. A 14 KB slot
minus ~4-8 KB BSS leaves ~6-10 KB for programs. Enough for small
programs (10-50 lines), which is authentic to the 8-bit experience.
uBASIC sidesteps this entirely with its <1 KB footprint.

**Rewrite?** For BASIC, no — FUZIX's or uBASIC are already optimized.
For Forth, zForth is purpose-built for this; prefer it over fforth.

---

### ue — Micro Screen Editor

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/ue/` (ue.c + term.c) |
| Size | ~800-1,000 lines C (2 files) |
| Text (ROM) | ~5-7 KB estimated |
| Data+BSS | ~2-4 KB |
| RAM needs | File buffer in heap |
| Needs fork() | No |
| Missing libc | Terminal raw mode (already have termios) |
| Approach | **Port from FUZIX** |
| Value | ★★★★★ |
| Cool factor | ★★★★★ |

**Why:** A tiny screen editor that "uses no libraries except clib."
FUZIX already has a platform-specific terminal backend for it. This
is the sweet spot between ed (line-mode only) and levee (too large):
a real full-screen editor that might actually fit in 14 KB on the
Mega Drive. Has a 68000 Makefile in FUZIX already.

**Risk:** Data+BSS ~2-4 KB is manageable. The file buffer is heap-
allocated, so editing capacity depends on remaining slot space
(~8-10 KB for text). Enough for scripts and config files.

**Rewrite?** No. It's already minimal and designed for small systems.
May need terminal output adapted to VDP/ANSI sequences.

---

### Levee — vi-like Editor

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/levee/` |
| Size | ~3,000+ lines across multiple files |
| Text (ROM) | ~40-45 KB |
| Data+BSS | ~6-8 KB |
| RAM needs | File buffer in heap |
| Needs fork() | Probably (shell escape feature) |
| Missing libc | termcap/terminfo (or VT100 hardcode) |
| Approach | **Port after Phase 8 (PSRAM)** |
| Value | ★★★★★ |
| Cool factor | ★★★★★ |

**Why:** A vi clone on the Mega Drive. Full-screen editing, modal
interface, the real Unix editing experience. Currently too large for
the 14 KB Mega Drive slot — needs Phase 8 PSRAM (512 KB per process).

Works immediately on the workbench emulator (117 KB slots).

**Risk:** ~6-8 KB data+BSS plus file buffer heap. Tight even with
PSRAM for the file buffer. Terminal control needs termcap stubs or
VT100 hardcoding.

**Alternative:** fleamacs (FUZIX's minimal emacs) is similarly sized.
Or vile (VI Like Editor, ~1,700 lines) — needs investigation for data
size.

**Rewrite?** Consider a minimal screen editor (~500-800 lines) that
handles insert/delete/navigate/save with hardcoded VDP output instead
of terminal escapes. Not vi-compatible but much more feasible.

---

### Fweep — Z-Machine Interpreter

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/games/fweep.c` |
| Size | ~2,100 lines C |
| Text (ROM) | ~20-25 KB |
| Data+BSS | ~4-8 KB |
| RAM needs | Z-machine memory (varies by story file, 32-128 KB) |
| Needs fork() | No |
| Missing libc | Minimal |
| Approach | **Port from FUZIX — needs story files in ROM** |
| Value | ★★★★☆ |
| Cool factor | ★★★★★ |

**Why:** Runs Infocom games (Zork, Hitchhiker's Guide, etc.) and
modern IF. Thousands of free Z-machine games exist. One interpreter
= hundreds of games.

**Risk:** Z-machine story files need 32-128 KB of addressable memory.
Story text can live in ROM but the dynamic memory portion must be in
RAM. Versions 1-3 (original Infocom) need ~32 KB dynamic — might
work. Later versions need more. **May require Phase 8 (PSRAM).**

**Rewrite?** No — Z-machine is a complex VM. Porting fweep is the
right approach. The Scott Adams engine (above) is the memory-safe
alternative.

---

### clear — Clear Screen

| Property | Value |
|----------|-------|
| Source | Rewrite |
| Size | ~5-10 lines |
| Approach | **Rewrite** |
| Value | ★★★☆☆ |

One escape sequence: `\033[2J\033[H`. Trivial but useful.

---

### banner — Large Text

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/banner.c` |
| Size | ~100-200 lines + font data |
| Approach | **Port from FUZIX** |
| Value | ★★☆☆☆ |
| Cool factor | ★★★☆☆ |

Prints text in large block letters. Fun visual utility.

---

### factor — Prime Factorization

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/factor.c` |
| Size | ~50-100 lines |
| Approach | **Port from FUZIX** |
| Value | ★★☆☆☆ |

Pure computation. `factor 42` → `2 3 7`. Tiny, educational.

---

### bc — Calculator

| Property | Value |
|----------|-------|
| Source | Various minimal implementations |
| Size | ~500-1,000 lines |
| Text (ROM) | ~8-12 KB |
| Data+BSS | ~2-4 KB |
| Needs fork() | Traditional bc pipes to dc — avoid this |
| Missing libc | Possibly arbitrary precision math |
| Approach | **Rewrite — standalone integer calculator** |
| Value | ★★★☆☆ |
| Cool factor | ★★★☆☆ |

**Why:** Interactive calculator. Useful for quick math, hex conversion.

**Rewrite?** Yes. Traditional bc/dc uses pipes (bc→dc). Write a
standalone calculator that handles integer arithmetic, hex/dec/oct
conversion, and basic operations. ~200-300 lines. Skip arbitrary
precision (no FPU, slow soft-float). Or port `dc` alone — it's a
simpler stack-based calculator.

---

### tar — Archive Files

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/tar.c` |
| Size | ~500-800 lines |
| Text (ROM) | ~6-10 KB |
| Data+BSS | ~1-2 KB |
| RAM needs | 512-byte block buffer |
| Needs fork() | No (for create/extract) |
| Approach | **Port from FUZIX** |
| Value | ★★★☆☆ |

**Why:** Archive/extract files. Essential once SD card support exists
(Phase 7). Load software bundles from SD card as tar files.

---

### Invaders — Space Invaders

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/cursesgames/invaders.c` |
| Size | ~500-800 lines |
| Text (ROM) | ~6-10 KB |
| Data+BSS | ~2-4 KB |
| Needs fork() | No |
| Missing libc | curses — **must replace with VDP graphics** |
| Approach | **Rewrite using VDP sprites/tiles** |
| Value | ★★★☆☆ |
| Cool factor | ★★★★★ |

**Why:** Space Invaders on a Mega Drive using the actual VDP hardware
(sprites, tiles, scrolling). This could look and play like a real
Mega Drive game, not just a text-mode curiosity.

**Rewrite?** Absolutely. The FUZIX curses version is a starting point
for game logic, but the VDP version would use the Genix graphics API
(gfx_tiles, gfx_sprite, gfx_scroll) for real arcade-style visuals.
This is a showcase application.

---

### Sokoban — Puzzle Game

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/games/sok.c` + level maps |
| Size | ~420 lines C |
| Text (ROM) | ~4-6 KB |
| Data+BSS | ~2-4 KB (level map) |
| Needs fork() | Yes (uses fork+pipe for termcap) — **must rewrite I/O** |
| Missing libc | termcap — replace with VDP/ANSI |
| Approach | **Rewrite I/O layer, keep game logic** |
| Value | ★★★☆☆ |
| Cool factor | ★★★★☆ |

**Why:** Classic box-pushing puzzle. Could look great with VDP tiles.

---

### Game of Life — Cellular Automaton

| Property | Value |
|----------|-------|
| Source | Rewrite |
| Size | ~100-200 lines |
| Text (ROM) | ~2-3 KB |
| Data+BSS | ~2-3 KB (40×28 grid = 1,120 bytes × 2) |
| Needs fork() | No |
| Approach | **Rewrite for VDP** |
| Value | ★★☆☆☆ |
| Cool factor | ★★★★☆ |

**Why:** Visually compelling on the VDP. Could use tile graphics for
cells. The 40×28 grid is a natural Life board. Simple to implement,
mesmerizing to watch.

---

### Mandelbrot — Fractal Renderer

| Property | Value |
|----------|-------|
| Source | Rewrite |
| Size | ~100-200 lines |
| Text (ROM) | ~2-4 KB |
| Data+BSS | <1 KB |
| Needs fork() | No |
| Approach | **Rewrite with fixed-point math** |
| Value | ★★☆☆☆ |
| Cool factor | ★★★★★ |

**Why:** Fractals on a Mega Drive. Visual demo of the system's
capability. Must use fixed-point arithmetic (no FPU). At 7.67 MHz
it will be slow (~minutes for a full frame) but that's part of the
charm — watching it render line by line.

Could use the VDP palette (64 colors in mode 4) for color depth.
A dedicated VDP-aware renderer would look stunning.

---

## Tier 5: Not Recommended

These were considered and rejected, with reasons.

### awk — Pattern Language

~3,000-5,000 lines. Massive data+BSS. The RAM required for awk's
symbol table, field splitting, and string operations would consume
the entire 14 KB slot just for the interpreter state. **Wait for
Phase 8.** On workbench it would work fine. Note: One True Awk
(Brian Kernighan's) is ~25-40 KB text (free in ROM) but needs
~8-15 KB data — so it's marginal even with PSRAM.

### Colossal Cave (full)

The full `cave/` from FUZIX has ~15 KB of game state. Exceeds 14 KB
slot. The Scott Adams adventures are the memory-safe alternative. Could
work after Phase 8.

### Any curses-based app (unmodified)

No termcap/terminfo database. Every curses app needs its terminal I/O
rewritten for VDP or hardcoded VT100 sequences. Not impossible but adds
work to every port.

### Network utilities

No TCP/IP stack. No serial modem. Not applicable.

### Multi-user utilities (passwd, su, login, who)

Genix is explicitly single-user. These serve no purpose.

### Compression (gzip, compress)

gzip: ~100 KB code + 256 KB working memory. Completely out.
bzip2: 64 KB dictionary minimum. Out.
Classic `compress`: 64 KB hash table. Out.
**However:** `lzw-ab` (embedded-friendly LZW) needs only **2.4 KB for
decode** with 12-bit symbols. Modest compression ratio but feasible.
Could be a Wave 6+ item if there's interest.

### Full vi/vim

Way too large. Even levee (the minimal vi) is 45 KB text + 6-8 KB
data. Need Phase 8.

### lua/python/perl

Lua: ~60-100 KB text + 16-32 KB data minimum. Marginal even with PSRAM.
Python/Perl: hundreds of KB. Not feasible.
**PicoC** (C interpreter, ~15-25 KB text, 8-20 KB data) is the most
feasible language interpreter in this class — "runs ok in 64KB." Still
needs Phase 8 for the Mega Drive.

---

## Implementation Priority & Dependency Map

### Wave 1: Filesystem Essentials (no new libc needed)
```
cp, mv, rm, mkdir, touch, kill, which, uname, clear
```
All are rewrites, ~20-100 lines each. Could be done in a single session.

### Wave 2: Text Processing (minor libc additions)
```
sort (rewrite, ~150 lines)
more (rewrite, ~150 lines)
ed   (port from FUZIX, ~1,000 lines)
```
Needs: possibly `mkstemp()` for sort, raw terminal mode for more.

### Wave 3: Shell Scripting Support
```
sed   (port from FUZIX, ~1,400 lines)
find  (rewrite, ~200 lines)
xargs (port from FUZIX, ~250 lines, fork→vfork fix)
test  (rewrite, ~200 lines)  [lower priority — dash has built-in]
```
Needs: nothing new if regex already works.

### Wave 4: Games — Quick Wins
```
hamurabi    (port, trivial)
dopewars    (port, easy)
fortune     (port + create fortune.dat)
wump        (port, trivial — Hunt the Wumpus)
cowsay      (port, trivial)
ttt         (port, trivial — tic-tac-toe)
moo         (port, trivial — Mastermind numbers)
hangman     (port, needs word list)
fish        (port, trivial — Go Fish)
arithmetic  (port, trivial — math drills)
```
Needs: nothing new. Games only use stdio + stdlib.

### Wave 5: Games — Flagship & Action
```
startrek    (port, may need display width tweaks)
2048        (rewrite/port for VDP)
scott_adams (port shared engine + 14 game data files)
tetris      (rewrite for VDP tiles — killer demo)
snake       (rewrite for VDP — action game on a game console)
```

### Wave 6: Screen Editor + Advanced Tools
```
ue         (port from FUZIX — tiny screen editor, huge value)
diff       (port or rewrite)
BASIC      (port FUZIX BASIC)
fforth     (port from FUZIX — Forth interpreter)
banner     (port, fun)
factor     (port, tiny)
```

### Wave 7: VDP Showcase Apps
```
Game of Life (rewrite for VDP tiles)
Mandelbrot   (rewrite with fixed-point)
Invaders     (rewrite for VDP sprites)
```

### Wave 8: Phase 8 Dependent (needs PSRAM)
```
levee/vile (screen editor — needs PSRAM)
fweep      (Z-machine — needs PSRAM for story files)
awk        (needs PSRAM for interpreter state)
adventure  (Colossal Cave — needs PSRAM for game DB)
```

---

## Libc Additions Needed

| Function | Needed by | Effort |
|----------|-----------|--------|
| `localtime()` / `gmtime()` | cal, date | ~50 lines |
| `strftime()` | date | ~80 lines |
| `mkstemp()` | sort (temp files) | ~20 lines |
| `fnmatch()` | find | ~40 lines |
| `strlcpy()` | some FUZIX ports | ~10 lines (or just use strncpy) |

All are small additions. `localtime()` is the most useful because
many programs need time decomposition.

---

## Summary: Port vs. Rewrite Recommendations

| App | Approach | Reason |
|-----|----------|--------|
| ed | Port FUZIX | Battle-tested, correct, right size |
| ue | Port FUZIX | Tiny screen editor, already has 68000 Makefile |
| sort | Rewrite | FUZIX version too RAM-hungry, simple qsort version is 90% |
| diff | Either | V7 version works but archaic; rewrite is cleaner |
| find | Rewrite | Full POSIX find is overkill; 200-line version is enough |
| xargs | Port FUZIX | Small, just fix fork→vfork |
| more | Rewrite | Hardcode 40×28 VDP, much simpler than porting less |
| sed | Port FUZIX | Complex enough that correctness matters |
| startrek | Port FUZIX | Already optimized for 32 KB target |
| hamurabi | Port FUZIX | Trivial, no changes needed |
| dopewars | Port FUZIX | Easy, maybe change getuid→getpid |
| wump/ttt/moo/etc | Port FUZIX | Trivial V7 games, stdio-only |
| 2048 | Rewrite | Trivial, target VDP display |
| invaders | Rewrite | Use VDP sprites instead of curses |
| BASIC | Port FUZIX | Tokenized interpreter already memory-optimized |
| fforth | Port FUZIX | ANS Forth, single file, great for small systems |
| mandelbrot | Rewrite | Fixed-point 68000-specific, ~100 lines |
| life | Rewrite | VDP-specific, ~100 lines |
| fortune | Port FUZIX | Trivial + curate fortune database |
| cowsay | Port FUZIX | Trivial, fun |
| cp/mv/rm/etc | Rewrite | Trivial utilities, <100 lines each |

---

## Total ROM Budget Estimate

If all Tier 1-3 apps were included:

| Category | Estimated text (ROM) |
|----------|---------------------|
| Existing 33 apps + dash | ~150 KB |
| Wave 1 (fs essentials) | ~15 KB |
| Wave 2 (sort, more, ed) | ~25 KB |
| Wave 3 (sed, find, xargs) | ~25 KB |
| Wave 4 (games) | ~50 KB |
| Wave 5 (BASIC, demos) | ~35 KB |
| **Total** | **~300 KB** |

Well within the 4 MB ROM limit. Text size is not a constraint.
The constraint is always RAM (14 KB data slot per process).

---

## The "Mega Drive Computer" Demo Sequence

The most compelling demo showing what all these apps enable:

```
1.  Boot → dash prompt
2.  fortune displays a quote
3.  cal shows current month
4.  ls /bin shows 40+ programs
5.  echo "hello" | rev demonstrates pipes
6.  mandelbrot renders a fractal (VDP color version)
7.  tetris or snake for gaming
8.  basic to program interactively
9.  ed to write a shell script
10. adventure for the ultimate nostalgia
```

This transforms the Mega Drive from a game console into a general-
purpose home computer — which is exactly what every kid in 1993
wished it could be.
