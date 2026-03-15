# Apps to Port — Research & Recommendations

Research into applications worth porting (or rewriting) for Genix on the
Sega Mega Drive. Sources: FUZIX app collection, classic Unix utilities,
retro games, and standalone projects.

## Platform Constraints

| Constraint | Value |
|------------|-------|
| CPU | 68000 @ 7.67 MHz, 16-bit MULU/DIVU |
| Main RAM | 64 KB total, ~40 KB user |
| Per-process RAM slot (MD) | ~14 KB for .data + .bss + heap + stack |
| ROM (code + const data) | Up to 4 MB, free via XIP |
| Display | VDP 40×28 text, tile/sprite graphics |
| Input | Saturn keyboard (50 keys) |
| No fork() | vfork()+exec() only |
| No curses | No termcap database; raw VT100/ANSI or direct VDP |
| No floating point HW | 68000 has no FPU; soft-float is slow |
| No network | No TCP/IP stack |
| Existing apps | 46 coreutils + dash + levee (47 total) |

### What's Free (ROM) vs. What Costs (RAM)

With XIP + `-msep-data`, understanding what goes where is critical:

| Segment | Location | Cost |
|---------|----------|------|
| .text (code) | ROM | **Free** — executes in place |
| .rodata (string literals, const arrays, lookup tables) | ROM | **Free** — read directly from ROM |
| .data (initialized mutable globals) | RAM slot | **Costs RAM** |
| .bss (zero-initialized mutable globals) | RAM slot | **Costs RAM** |
| heap (malloc) | RAM slot | **Costs RAM** — grows up |
| stack | RAM slot | **Costs RAM** — grows down |

**The real constraint is mutable state.** A program with 100 KB of code
and 50 KB of string constants costs zero RAM — it all lives in ROM.
Only mutable globals, heap allocations, and stack frames use the 14 KB
slot.

**Reference point:** dash has 91 KB text but only 6.8 KB data+bss (49%
of slot). Most programs are much smaller. The 14 KB slot is more
spacious than it first appears.

**Key implication for porting:** Make data `const` wherever possible.
String tables, game databases, lookup arrays, format strings — all
should be `const` so they stay in ROM. This is the single most
impactful optimization for fitting programs on the Mega Drive.

---

## Existing Shells

### Dash — Default Interactive Shell

| Property | Value |
|----------|-------|
| Source | `apps/dash/` (Debian Almquist Shell port) |
| Text (ROM) | 88,796 bytes (free — XIP) |
| data+bss | 6,788 bytes (48% of 14 KB slot) |
| Status | **Working** — default shell, spawned at boot |

Dash is a full POSIX shell with **39 builtin commands**:

| Category | Commands |
|----------|----------|
| Shell control | `.` `:` `eval` `exec` `exit` `return` `break` `continue` |
| Variables | `set` `shift` `export` `local` `readonly` `unset` `getopts` |
| Aliases | `alias` `unalias` |
| I/O | `echo` `printf` `read` |
| Directory | `cd` `chdir` `pwd` |
| Commands | `command` `type` `hash` |
| Test | `test` `[` `true` `false` |
| Jobs | `bg` `fg` `jobs` `wait` `kill` |
| Misc | `trap` `times` `umask` `ulimit` |

These builtins mean many common operations (echo, test, printf, cd,
kill, true/false) don't need standalone binaries — they run inside
dash with zero exec overhead. The standalone `kill`, `test`, `true`,
`false`, and `[` in Wave 1 are only needed for scripts that call
them via `exec` or `xargs`.

### Builtin Kernel Shell — Fallback

| Property | Value |
|----------|-------|
| Source | `kernel/main.c` — `builtin_shell()` (~70 lines) |
| Status | **Working** — fallback if `/bin/dash` not found |

A minimal shell that runs in supervisor mode inside the kernel. The
boot sequence tries to spawn dash first; if that fails, it drops to
this shell. Commands:

```
help  halt  mem  ls [path]  cat <file>  echo <text>
cd [dir]  exec <prog> [args] [| prog2] [< in] [> out]
write <path> <text>  mkdir <path>
```

Any unrecognized command is passed to implicit exec (tries to run as
a program). Supports pipes and I/O redirection via `exec`.

**Not a replacement for dash** — no scripting, no variables, no
control flow, no job control. It exists purely for boot resilience
and early development. Cost to keep is ~70 lines of kernel code.

---

## Already Ported — Needs Work

### Levee — vi-like Editor

| Property | Value |
|----------|-------|
| Source | Already in `apps/levee/` (15 .c files) |
| Text (ROM) | 47,420 bytes (free — XIP) |
| .data | 2,620 bytes |
| .bss | 10,268 bytes |
| **data+bss** | **12,888 bytes (12.6 KB)** |
| Slack in 14 KB slot | **~1.1 KB for heap + stack** |
| Status | **Boots OK** (crash fix merged), but very tight on RAM |

Levee boots and starts on the workbench emulator (boot test passes).
The `-msep-data` Makefile fix has been merged and the kernel panic is
resolved.

**The problem is bss.** At 10 KB, levee's bss consumes most of the
14 KB slot, leaving only ~1.1 KB for heap + stack. That's too tight
for real editing — the file buffer, undo state, and stack need more
room.

**Options to make levee usable on Mega Drive:**
1. **Shrink bss** — audit globals in levee for oversized static
   buffers. The `SIZE=2048` build flag controls the edit buffer but
   bss is 10 KB, suggesting other large static arrays. Converting
   some to malloc'd or reducing sizes could reclaim several KB.
2. **Phase 8 (PSRAM)** — with 512 KB per process, levee runs easily.
3. **Workbench** — 117 KB slots, levee works fine right now.

For comparison, dash has only 6.8 KB data+bss. If levee's bss could
be halved (~5 KB), it would leave ~7 KB for heap+stack — usable.

---

## Tier 1: Critical Missing Utilities

These fill gaps that make the system hard to use without them.

### ed — Line Editor

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/ed.c` (3 implementations available) |
| Size | ~1,000 lines C |
| Data+BSS | ~1-2 KB (small static state) |
| Heap | Linked list of line pointers — proportional to file size |
| Needs fork() | No |
| Missing libc | None — uses malloc, stdio, string, regex (all present) |
| Approach | **Port from FUZIX** |

The classic Unix line editor. No screen control needed — works
perfectly on any terminal. With levee broken, this is the only path
to creating/editing files on the system.

Heap usage for lines is the only concern. With ~6-10 KB of heap
available after data+bss, that's enough to edit files up to several
hundred lines — scripts, configs, small programs.

---

### cp, mv, rm — File Management — DONE

| Property | Value |
|----------|-------|
| Size | ~50-150 lines each |
| Data+BSS | <1 KB each |
| Needs fork() | No |
| Approach | **Rewrite** (trivial) |
| Status | **Ported** — all three in `apps/` |

Can't copy, move, or delete files without these. All use existing
syscalls (open/read/write/close, rename, unlink). rm supports `-r`
for recursive directory removal.

---

### more — Pager — DONE

| Property | Value |
|----------|-------|
| Size | ~115 lines |
| Data+BSS | 452+180 bytes |
| Needs fork() | No |
| Missing libc | None (termios raw mode already present) |
| Approach | **Rewrite for 40×28 VDP** |
| Status | **Ported** — `apps/more.c` |

Opens `/dev/tty` for raw key input. Space = next page, Enter = next
line, q = quit. Falls back to cat-through when stdout is not a
terminal.

---

### sort — Sort Lines — DONE

| Property | Value |
|----------|-------|
| Source | Rewrite (~120 lines) |
| Data+BSS | 496+308 bytes |
| Heap | Proportional to input size |
| Needs fork() | No |
| Approach | **Rewrite using qsort()** (already in libc) |
| Status | **Ported** — `apps/sort.c` |

Reads all input into malloc'd buffer, sorts with qsort, outputs.
Supports `-r` (reverse) and `-n` (numeric sort). With ~10 KB heap
available on MD, can sort files up to ~10 KB.

---

### find — File Search — DONE

| Property | Value |
|----------|-------|
| Size | ~130 lines |
| Data+BSS | 500+308 bytes |
| Needs fork() | No (basic); vfork+exec for `-exec` |
| Approach | **Rewrite — minimal version** |
| Status | **Ported** — `apps/find.c` |

Supports `-name PATTERN` (glob with `*` and `?`) and `-type f/d`.
Recursive directory walk. No `-exec` yet (would need vfork).

---

### xargs — Build Commands from stdin — DONE

| Property | Value |
|----------|-------|
| Source | Rewrite (~98 lines) |
| Data+BSS | 460+300 bytes |
| Needs fork() | No — uses vfork()+exec() |
| Approach | **Rewrite** (simpler than porting FUZIX) |
| Status | **Ported** — `apps/xargs.c` |

Reads whitespace-separated arguments from stdin, builds command
lines, executes with vfork()+exec(). 64-argument limit per
invocation.

---

### kill, mkdir, touch, which, uname, clear — ALL DONE

All trivial rewrites, 10-80 lines each, <1 KB data. All ported.

| App | Lines | data+bss | Status |
|-----|-------|----------|--------|
| kill | 60 | 620 B | **Ported** — named signals (HUP, INT, KILL, etc.) |
| mkdir | 20 | 372 B | **Ported** |
| touch | 25 | 368 B | **Ported** (no utimes syscall yet) |
| which | 55 | 460 B | **Ported** — PATH search with access(X_OK) |
| uname | 15 | 376 B | **Ported** — -a for full system info |
| clear | 5 | 284 B | **Ported** |

---

## Tier 2: Games — The Fun Stuff

A Mega Drive running Unix with games is inherently impressive.
All games below use only stdio + stdlib (already present) and have
negligible data+bss. String constants (prompts, descriptions) are
`const` → ROM.

### Trivial Ports (stdio-only, <1 KB data each)

These could all be ported in a single session:

| Game | Source | Lines | Description |
|------|--------|-------|-------------|
| hamurabi | FUZIX `games/hamurabi.c` | ~380 | Ancient city simulation (1968). Manage land, grain, population. |
| dopewars | FUZIX `games/dopewars.c` | ~720 | Trading game. Buy low, sell high, avoid cops. Addictive. |
| wump | FUZIX `V7/games/wump.c` | ~250 | Hunt the Wumpus (1973). Navigate caves, shoot arrows. |
| hangman | FUZIX `V7/games/hangman.c` | ~200 | Word guessing. Needs a compiled-in word list (~3 KB, const → ROM). |
| fish | FUZIX `V7/games/fish.c` | ~200 | Go Fish card game. |
| arithmetic | FUZIX `V7/games/arithmetic.c` | ~150 | Math practice drills. |
| ttt | FUZIX `MWC/cmd/ttt.c` | ~150 | Tic-tac-toe with AI opponent. |
| moo | FUZIX `MWC/cmd/moo.c` | ~100 | Mastermind number-guessing variant. |
| cowsay | FUZIX `games/cowsay.c` | ~100 | ASCII cow says your text. |
| fortune | FUZIX `games/fortune.c` | ~65 | Random quotes. Fortune DB is const → ROM. |

None use fork(). None need terminal control beyond stdio. None have
meaningful data+bss.

### Star Trek — Flagship Text Game

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/games/startrek.c` |
| Size | ~1,400 lines C |
| Data+BSS | ~1-2 KB mutable (galaxy map = 8×8 ints, ship state) |
| Const data (ROM) | ~10-15 KB (all prompt strings, format strings) |
| Needs fork() | No |
| Approach | **Port from FUZIX** |
| Cool factor | ★★★★★ |

THE classic Unix game. FUZIX version was "put on a diet to run in
32K" — and that was without XIP. With string constants in ROM,
mutable state is tiny. The 40×28 display is narrower than 80×24 —
may need minor output reformatting.

### Scott Adams Adventures (14 games)

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/games/adv01.c`-`adv14b.c` |
| Size | Shared engine ~500 lines + data per game |
| Data+BSS | ~1-2 KB mutable per game |
| Const data (ROM) | Game databases — all const → ROM |
| Needs fork() | No |
| Cool factor | ★★★★★ |

14 classic text adventures from one compact engine: Adventureland,
Pirate Adventure, Voodoo Castle, etc. Designed for 8-bit computers
with 16 KB RAM. The game databases are const data → ROM. One engine
in ROM, 14 games essentially free.

### Colossal Cave Adventure

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/cave/` (~2,000 lines, 10 files) |
| Data+BSS | ~1-3 KB mutable (player state, object locations, flags) |
| Const data (ROM) | ~15-20 KB (room descriptions, vocabulary, connections) |
| Needs fork() | No |
| Cool factor | ★★★★★ |

THE original text adventure. Previously classified as "needs PSRAM"
based on total game data (~15 KB). **This was wrong.** The bulk of
that data is room descriptions and vocabulary — string constants that
should be `const` → ROM. Mutable game state is: player location,
~60 object locations, ~30 flag bits, lamp timer, score — probably
~1-3 KB.

**Caveat:** Need to verify that the FUZIX source actually uses `const`
for the data tables. If not, the port must add `const` qualifiers.
This is a mechanical change, not a design problem.

### Tetris

| Property | Value |
|----------|-------|
| Source | Rewrite (~250-400 lines, no curses) |
| Data+BSS | ~1 KB (10×20 board + piece state + score) |
| Needs fork() | No |
| Missing libc | Non-blocking input (termios raw mode — present) |
| Approach | **Rewrite for VDP tiles** |
| Cool factor | ★★★★★ |

Tetris on a Mega Drive launched from `$ tetris` at a shell prompt.
Could use VDP tiles for proper block graphics — would look like a
real Mega Drive game. Needs non-blocking keyboard input + timer for
piece drop (termios raw mode + timeout).

### Snake

| Property | Value |
|----------|-------|
| Source | Rewrite (~200-300 lines) |
| Data+BSS | ~1.2 KB (40×28 board + snake body positions) |
| Approach | **Rewrite for VDP** |
| Cool factor | ★★★★★ |

Action game on a game console. Same input requirements as Tetris.

### 2048

| Property | Value |
|----------|-------|
| Source | mevdschee/2048.c (~350 lines) or rewrite |
| Data+BSS | <1 KB (4×4 grid + score) |
| Approach | **Port or rewrite for VDP** |
| Cool factor | ★★★★☆ |

### Other Games Worth Considering

| Game | Lines | Data+BSS | Notes |
|------|-------|----------|-------|
| Robots | ~250 (rewrite) | ~1 KB | BSD robots — chase/crash grid game |
| Sokoban | ~420 (FUZIX) | ~2 KB | Level maps are const → ROM. Replace termcap I/O with ANSI/VDP. |
| Invaders | ~500 (rewrite) | ~2 KB | Rewrite for VDP sprites — showcase app |

---

## Tier 3: Text Processing & Scripting

### sed — Stream Editor

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/util/sed.c` |
| Size | ~1,400 lines C |
| Data+BSS | ~2-3 KB (pattern/hold space buffers) |
| Needs fork() | No |
| Missing libc | None (regex already present) |
| Approach | **Port from FUZIX** |

sed + grep + sort = real text processing pipelines. Shell scripts
need `s/foo/bar/g`. FUZIX version is designed for small systems.

### diff — File Comparison

| Property | Value |
|----------|-------|
| Source | FUZIX V7 or rewrite (~300-800 lines) |
| Data+BSS | ~1-2 KB |
| Heap | O(n) for file lengths |
| Needs fork() | No |
| Approach | **Port V7 diff or rewrite** |

Useful with ed for verifying edits. Heap usage proportional to file
size — works on files up to ~5-10 KB (the expected use case).

### cal, date — Calendar and Date

| Property | Value |
|----------|-------|
| Source | FUZIX cal.c (~430 lines) / rewrite date |
| Data+BSS | <1 KB each |
| Missing libc | `localtime()`, `strftime()` (~130 lines total) |
| Approach | **Port cal, rewrite date** |

`cal 2026` on a Mega Drive. Needs localtime() libc addition — worth
doing since many programs need time decomposition.

### Other Useful Utilities

| App | Lines | Data+BSS | Notes |
|-----|-------|----------|-------|
| dd | ~200 | <1 KB | Block-level copy. Useful for disk ops. |
| test/[ | ~200 | <1 KB | Condition testing. Dash has built-in but standalone is expected. |
| tar | ~400 | ~1 KB | Archive/extract. Essential once SD card exists (Phase 7). |
| banner | ~150 | <1 KB | Large block letters. Font data is const → ROM. Fun. |
| factor | ~80 | <1 KB | `factor 42` → `2 3 7`. Pure computation. |

---

## Tier 4: Programming Languages

A programming language on the Mega Drive turns it into a home computer.

### BASIC Interpreter

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/basic/` or uBASIC (~700 lines) |
| Text (ROM) | ~20-30 KB (FUZIX) or ~4 KB (uBASIC) |
| Data+BSS | ~2-4 KB |
| Heap | User's BASIC program + variables |
| Needs fork() | No |
| Cool factor | ★★★★★ |

**FUZIX BASIC:** Tokenized interpreter designed for small systems.
Handles string/numeric vars, arrays (4 dimensions), FOR/NEXT, GOSUB,
expressions. Tokenizes at input time for speed. Text is ~20-30 KB
(free in ROM). Data+bss ~2-4 KB. Leaves ~8-10 KB heap for user
programs — enough for 50-100 line programs, authentic to the 8-bit
experience.

**uBASIC** (Adam Dunkels, ~700 lines): Extremely tiny, <1 KB working
memory. Integer variables A-Z only, IF/THEN, FOR/NEXT, GOSUB, PRINT.
Fits anywhere. Good starting point, upgrade to FUZIX BASIC later.

### Forth Interpreter

| Property | Value |
|----------|-------|
| Source | FUZIX `util/fforth.c` (~2,400 lines) or zForth (~1,000 lines) |
| Text (ROM) | ~15-25 KB |
| Data+BSS | ~2-4 KB |
| Heap | Dictionary + stacks (configurable, ~4-8 KB) |
| Needs fork() | No |
| Cool factor | ★★★★★ |

Interactive Forth on a 68000 is historically appropriate — many early
68000 systems ran Forth. fforth passes most ANS conformance tests.
zForth is smaller and designed for "extending embedded applications on
small microprocessors." Dictionary size is configurable.

Could write Forth programs that interact with VDP registers directly
for graphics.

### Fweep — Z-Machine Interpreter

| Property | Value |
|----------|-------|
| Source | FUZIX `Applications/games/fweep.c` (~2,100 lines) |
| Text (ROM) | ~20-25 KB |
| Data+BSS | ~2-4 KB interpreter state |
| Needs fork() | No |
| Cool factor | ★★★★★ |

Runs Infocom games (Zork, Hitchhiker's Guide, Planetfall) and
thousands of free Z-machine games. One interpreter = hundreds of
games.

**Memory model:** Z-machine story files have a "dynamic memory" area
(the first N bytes, must be writable) and "static/high memory"
(read-only, can stay in ROM). For Z-machine v1-3 (original Infocom
games), dynamic memory is typically **4-12 KB**. The rest of the
story file (30-100 KB) can be `const` data in ROM.

**Feasibility:** With ~8-10 KB available in the slot after
interpreter data+bss, most v1-3 games fit. v5+ games have larger
dynamic memory areas and may not fit. Story files go in ROM with only
the dynamic header copied to RAM.

**Previously classified as "needs PSRAM" — this was wrong.** The
misconception was that the entire story file needs to be in RAM.
It doesn't. Only the dynamic memory portion does.

### awk — Pattern Language

| Property | Value |
|----------|-------|
| Source | One True Awk (Kernighan's, ~6,000 lines) |
| Text (ROM) | ~25-40 KB (free) |
| Data+BSS | ~3-6 KB static |
| Heap | Runtime: symbol tables, field buffers, associative arrays |
| Needs fork() | No (except optional system() which isn't essential) |
| Cool factor | ★★★★★ |

**Previously dismissed as "needs PSRAM" — reconsider.** The 25-40 KB
text is free (ROM). Static data+bss is moderate. The real question is
runtime heap: awk programs that accumulate data in associative arrays
will fill the slot, but many useful awk programs are stateless
transformations (`awk '{print $2}'`, `awk -F: '/pattern/{print $1}'`)
that use minimal heap.

**Challenge:** The yacc-generated parser adds to data+bss. Floating
point numbers (awk uses doubles) would need soft-float — slow and adds
to text size. Consider integer-only mode or accept slow math.

**Approach:** Port One True Awk, measure data+bss. If it fits in a
slot with ~4-6 KB to spare for heap, most practical awk one-liners
work. Large awk programs that accumulate state won't fit, but that's
acceptable — the value is in one-liners and simple scripts.

### Stretch: Other Language Runtimes

| Language | Text (ROM) | Data+BSS | Runtime Heap | Feasibility |
|----------|-----------|----------|-------------|-------------|
| PicoC (C interpreter) | ~15-25 KB | ~4-8 KB | ~8-20 KB | **Marginal.** "runs ok in 64KB." Text is free. Tight on heap. |
| Lua (integer-only mode) | ~60-100 KB | ~4-8 KB | ~16-32 KB | **Unlikely on MD.** Even stripped, GC needs 16+ KB. Workbench only. |
| Python/Perl | hundreds of KB | enormous | enormous | **No.** |

PicoC is the most interesting stretch goal — a C interpreter running
on a C-based OS on a 68000 is delightfully recursive.

---

## Tier 5: Visual Demos & VDP Showcase

These use the Mega Drive's VDP hardware for graphics that go beyond
text-mode terminals.

### Mandelbrot — Fractal Renderer

| Property | Value |
|----------|-------|
| Source | Rewrite (~100-200 lines) |
| Data+BSS | <1 KB |
| Approach | **Rewrite with 16.16 fixed-point** |
| Cool factor | ★★★★★ |

Fixed-point 16.16 arithmetic on the 68000: the MULU instruction does
16×16→32 multiply. A 40×28 render at 100 max iterations ≈ 112,000
multiplies at ~50 cycles each ≈ 5.6M cycles ≈ **~0.7 seconds**. Very
feasible — not the "minutes" I initially estimated.

With VDP tiles and the 64-color palette, this produces a colorful
fractal on a game console. THE flagship screenshot for the project.

### Game of Life — Cellular Automaton

| Property | Value |
|----------|-------|
| Source | Rewrite (~100-150 lines) |
| Data+BSS | ~2.2 KB (two 40×28 grids, or bit-packed = ~280 bytes) |
| Cool factor | ★★★★☆ |

VDP tiles for cells. 40×28 grid is a natural Life board. Mesmerizing
to watch. Glider guns, oscillators, etc.

### Space Invaders

| Property | Value |
|----------|-------|
| Source | Rewrite game logic, use VDP sprites/tiles |
| Data+BSS | ~2-3 KB |
| Cool factor | ★★★★★ |

Using the actual Mega Drive VDP hardware (sprites, tiles, scrolling)
this could look and play like a real Mega Drive game — but launched
from `$ invaders` at a shell prompt. Showcase application for the
graphics API.

---

## Not Recommended

### Network utilities
No TCP/IP stack. Not applicable.

### Multi-user utilities (passwd, su, login, who)
Genix is single-user. No purpose.

### Compression (gzip, bzip2)
gzip needs 256 KB working memory, bzip2 needs 64 KB. Out of scope.
**Exception:** `lzw-ab` (embedded LZW) decodes with only **2.4 KB
RAM** using 12-bit symbols. Modest compression ratio but feasible as
a later addition.

### Curses-based apps (unmodified)
No termcap database. Every curses app needs I/O rewritten for
VDP/ANSI. The game logic can be reused but the display layer must
be replaced.

---

## Implementation Waves

### Wave 1: Filesystem Essentials — DONE
```
cp, mv, rm, mkdir, touch, kill, which, uname, clear
```
All rewrites, 10-150 lines each. Completed in a single session.

### Wave 2: Core Text Tools — PARTIAL
```
ed     (port from FUZIX — first editor)       ← NOT YET
more   (rewrite — first pager)                ← DONE
sort   (rewrite — simple qsort version)       ← DONE
```

### Wave 3: Quick-Win Games
```
hamurabi, dopewars, wump, fortune, cowsay
ttt, moo, hangman, fish, arithmetic
```
All trivial stdio-only ports. Single session per batch.

### Wave 4: Fix Levee
```
levee  (update Makefile to -msep-data, fix crash bug)
```
Highest-value single fix — gives the system a real screen editor.

### Wave 5: Scripting & Flagship Games — PARTIAL
```
sed       (port from FUZIX)                   ← NOT YET
find      (rewrite, ~130 lines)               ← DONE
xargs     (rewrite, ~98 lines)               ← DONE
startrek  (port from FUZIX — may need 40-col formatting)
scott_adams (port shared engine + 14 game data files)
```

### Wave 6: Action Games + VDP
```
tetris     (rewrite for VDP tiles)
snake      (rewrite for VDP)
mandelbrot (rewrite, fixed-point — ~0.7s render)
life       (rewrite for VDP tiles)
```

### Wave 7: Programming Languages
```
uBASIC or FUZIX BASIC  (first language — turns MD into home computer)
fforth or zForth        (second language — historically appropriate)
```

### Wave 8: Advanced Ports
```
adventure  (Colossal Cave — const-qualify data tables for ROM)
fweep      (Z-machine — story files in ROM, dynamic mem in RAM)
awk        (One True Awk — measure data+bss, assess fit)
diff       (port or rewrite)
tar        (port — needed once SD card exists)
cal, date  (need localtime() libc addition)
```

### Wave 9: Stretch Goals
```
invaders  (VDP sprites — showcase app)
sokoban   (replace termcap I/O)
PicoC     (C interpreter — if heap fits)
```

---

## Libc Additions Needed

| Function | Needed by | Effort |
|----------|-----------|--------|
| `localtime()` / `gmtime()` | cal, date, many programs | ~50 lines |
| `strftime()` | date | ~80 lines |
| `mkstemp()` | sort (temp files, optional) | ~20 lines |
| `fnmatch()` | find (has built-in glob) | ~40 lines (optional) |
| `ntohs()` / `htons()` | fortune | ~2 lines (macro) |

All are small. `localtime()` is the highest value — many programs
need time decomposition.

---

## Summary: Port vs. Rewrite

| App | Approach | Reason |
|-----|----------|--------|
| levee | Fix existing | Already ported, just needs -msep-data + crash fix |
| ed | Port FUZIX | Battle-tested, correct, already fits |
| sort | ~~Rewrite~~ | **DONE** — qsort-based, -r, -n |
| diff | Either | V7 works but archaic; rewrite is cleaner |
| find | ~~Rewrite~~ | **DONE** — -name glob, -type f/d |
| xargs | ~~Rewrite~~ | **DONE** — vfork()+exec() |
| more | ~~Rewrite~~ | **DONE** — raw mode pager |
| sed | Port FUZIX | Complex enough that correctness matters |
| awk | Port One True Awk | Don't rewrite — language semantics are subtle |
| startrek | Port FUZIX | Already optimized for 32 KB target |
| adventure | Port FUZIX | Const-qualify data tables for ROM placement |
| fweep | Port FUZIX | Story files in ROM, only dynamic mem in RAM |
| hamurabi/dopewars/etc | Port FUZIX | Trivial stdio-only games |
| scott_adams | Port FUZIX | 14 games from one engine, const data → ROM |
| BASIC | Port FUZIX or uBASIC | Tokenized interpreter already memory-optimized |
| fforth | Port FUZIX | ANS Forth, single file, great for small systems |
| tetris/snake | Rewrite | VDP-native, action games on a game console |
| mandelbrot/life | Rewrite | VDP-specific, fixed-point, ~100-200 lines each |
| invaders | Rewrite | VDP sprites — showcase application |
| cp/mv/rm/etc | ~~Rewrite~~ | **DONE** — all 9 Wave 1 apps ported |

---

## Total ROM Budget

| Category | Estimated text (ROM) |
|----------|---------------------|
| Existing 46 apps + dash + levee | ~260 KB (measured) |
| Wave 1 (fs essentials) | ~~~15 KB~~ **DONE** |
| Wave 2 (ed, more, sort) | ~10 KB remaining (ed only) |
| Wave 3 (quick-win games) | ~30 KB |
| Wave 5 (sed, find, startrek, scott_adams) | ~60 KB |
| Wave 6 (tetris, snake, mandelbrot, life) | ~15 KB |
| Wave 7 (BASIC, Forth) | ~40 KB |
| Wave 8 (adventure, fweep, awk, etc) | ~80 KB |
| **Total** | **~460 KB** |

Well within the 4 MB ROM limit. ROM is abundant.

---

## The "Mega Drive Computer" Demo Sequence

```
1.  Boot → dash prompt
2.  fortune displays a quote
3.  cal shows current month
4.  ls /bin shows 50+ programs
5.  echo "hello" | rev | cowsay
6.  mandelbrot renders a color fractal in ~1 second
7.  tetris with VDP tile graphics
8.  basic to write and run a program
9.  ed to write a shell script, then run it
10. adventure — explore Colossal Cave
11. levee to edit a file with vi keybindings
```

This transforms the Mega Drive from a game console into a general-
purpose home computer — which is exactly what every kid in 1993
wished it could be.
