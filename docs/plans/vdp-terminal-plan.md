# VDP Color Terminal, Curses, and Performance Plan

**Status:** Draft — awaiting review
**Scope:** ANSI escape parser, VDP color terminal, minimal curses library,
device driver fixes, and performance optimizations that intersect with VDP work
**Estimated total:** ~600-800 lines of new code across ~10 files

This plan consolidates the VDP Color Terminal section from PLAN.md, the
relevant parts of optimization-plan.md, and the bugs/improvements identified
in docs/research/vdp-research.md into a single actionable implementation plan.

---

## Table of Contents

1. [Current State](#1-current-state)
2. [Goals](#2-goals)
3. [Implementation Phases](#3-implementation-phases)
   - Phase V1: Device driver correctness fixes
   - Phase V2: ANSI escape sequence parser
   - Phase V3: VDP palette and color attribute support
   - Phase V4: Minimal curses library
   - Phase V5: Performance optimizations (VDP + general)
4. [Rejected Approaches](#4-rejected-approaches)
5. [Testing Strategy](#5-testing-strategy)
6. [RAM Budget](#6-ram-budget)
7. [File Inventory](#7-file-inventory)
8. [Open Questions](#8-open-questions)

---

## 1. Current State

### What works

- VDP console: 40×28 text, 8×8 monospace font (256 CP437 glyphs)
- Hardware VSRAM scroll (already optimal, matches FUZIX)
- Control characters: `\n`, `\r`, `\t`, `\b`, auto-wrap, auto-scroll
- VDP device driver: 7 ioctls (tiles, map, palette, sprites, scroll,
  vblank, clear) with exclusive access design
- Debug overlay on Plane B (F12 toggle)
- User graphics library (libc/gfx.c)
- Saturn keyboard input via VBlank ISR

### What's broken

- **Device open/close never dispatched**: `sys_open()` and `sys_close()`
  don't call `devtab[major].open()`/`close()`. `vdp_open()` (exclusive
  access check) and `vdp_close()` (VDP_reinit cleanup) are dead code.
  Multiple processes can open `/dev/vdp` simultaneously, and the console
  is never restored on close. (vdp-research.md §5)

### What's missing

- **No escape sequence parsing**: Characters go straight from `tty_write()`
  → `pal_console_putc()` → `plot_char()`. ANSI sequences (cursor
  positioning, colors, clear) are rendered as garbage. Levee emits
  `ESC[row;colH` for cursor positioning — currently ignored.
- **No color**: VDP has 4 palettes × 16 colors but only 3 colors are
  used (transparent, black, white). 61 color slots wasted.
- **No curses**: Full-screen TUI apps require manual escape sequence
  generation. No standard API for games, editors, or status bars.
- **Console corruption in graphics mode**: kprintf writes to Plane A
  nametable even when a user process owns `/dev/vdp`.
- **Ctrl-Z in graphics mode**: Suspending a graphics app leaves VDP in
  graphics state with no way to recover the console.

---

## 2. Goals

**Primary:** Make the VDP terminal capable enough to run levee with
working cursor positioning, display colored output (ls --color, shell
prompts, syntax highlighting), and support a curses API for TUI apps.

**Secondary:** Fix the device driver bugs so `/dev/vdp` exclusive
access and cleanup actually work. Include the VDP-related performance
optimizations from the optimization plan.

**Non-goals:** Full ncurses compatibility, windows/subwindows, mouse
support, wide characters, termcap/terminfo database. We implement a
minicurses that covers the common subset.

---

## 3. Implementation Phases

### Phase V1: Device Driver Correctness Fixes

**Priority:** Must-fix — correctness bugs
**Effort:** ~50 lines across 2 files
**Dependencies:** None

#### V1.1: Device open/close dispatch

**Problem:** `sys_open()` (proc.c:843) opens device files but never
calls `devtab[major].open()`. Same for `sys_close()` — never calls
`devtab[major].close()`. This means:
- `vdp_open()` exclusive access check is bypassed
- `vdp_close()` VDP_reinit() cleanup never runs
- Any process can open `/dev/vdp` without restriction

**Fix:** After the inode lookup in `sys_open()`, check if
`ip->type == FT_DEV` and call `devtab[ip->dev_major].open(ip->dev_minor)`.
If it returns an error, release the ofile and inode, return the error.

Similarly in `sys_close()`, when `of->refcount` drops to 0 and the
inode is a device, call `devtab[major].close(minor)`.

Also in `do_exit()` (process cleanup), ensure device close is called
for any open device fds.

```c
/* In sys_open(), after inode lookup: */
if (ip->type == FT_DEV) {
    int err = devtab[ip->dev_major].open(ip->dev_minor);
    if (err < 0) {
        fs_iput(ip);
        return err;
    }
}

/* In sys_close(), when refcount hits 0: */
if (of->inode && of->inode->type == FT_DEV) {
    devtab[of->inode->dev_major].close(of->inode->dev_minor);
}
```

**Testing:**
- Open `/dev/vdp` → should succeed
- Open `/dev/vdp` again (different fd) → should return -EBUSY
- Close the fd → VDP should reinit
- Kill a process that has `/dev/vdp` open → VDP should reinit

#### V1.2: Console output suppression during graphics mode

**Problem:** When a user process owns `/dev/vdp`, kernel kprintf and
background job output still writes to Plane A via `pal_console_putc()`,
corrupting the user's display.

**Fix:** Add a `vdp_graphics_mode` flag in platform.c. Set it in
`vdp_open()`, clear it in `vdp_close()`. Guard `pal_console_putc()`:

```c
static int vdp_graphics_mode = 0;

void pal_console_putc(char c)
{
    if (vdp_graphics_mode)
        return;  /* suppress console output in graphics mode */
    /* ... existing code ... */
}
```

Future improvement: route suppressed output to Plane B debug overlay
instead of dropping it. Not in scope for V1.

#### V1.3: Ctrl-Z safety in graphics mode

**Problem:** If a user suspends a graphics app with Ctrl-Z, the VDP
stays in graphics mode and the shell can't display.

**Fix:** In `gfx_open()` (libc/gfx.c), disable ISIG via termios so
Ctrl-Z/Ctrl-C don't generate signals. Restore in `gfx_close()`. This
matches FUZIX behavior (graphics apps always set raw mode).

```c
/* In gfx_open(): */
struct termios t;
tcgetattr(0, &saved_termios);
t = saved_termios;
t.c_lflag &= ~ISIG;
tcsetattr(0, TCSANOW, &t);

/* In gfx_close(): */
tcsetattr(0, TCSANOW, &saved_termios);
```

~10 lines in libc/gfx.c.

---

### Phase V2: ANSI Escape Sequence Parser

**Priority:** High — biggest independent value, prerequisite for V3 and V4
**Effort:** ~200-250 lines in tty.c
**Dependencies:** None (V1 is independent)

#### Design

Add a state machine to `tty_write()` that intercepts escape sequences
before they reach `pal_console_putc()`. The parser handles output
processing, not input — it sits in the write path.

**State machine:**

```
NORMAL → ESC received → ESC_SEEN
ESC_SEEN → '[' → CSI_PARAM (collecting numeric parameters)
ESC_SEEN → other → emit ESC + char, back to NORMAL
CSI_PARAM → digit → accumulate parameter
CSI_PARAM → ';' → next parameter
CSI_PARAM → letter → execute CSI command, back to NORMAL
```

**Supported sequences (minimum viable):**

| Sequence | Name | Action |
|----------|------|--------|
| `ESC[H` / `ESC[row;colH` | CUP | Move cursor to (row,col). Default (1,1). |
| `ESC[A` / `ESC[nA` | CUU | Cursor up n rows (default 1) |
| `ESC[B` / `ESC[nB` | CUD | Cursor down n rows |
| `ESC[C` / `ESC[nC` | CUF | Cursor forward n cols |
| `ESC[D` / `ESC[nD` | CUB | Cursor back n cols |
| `ESC[J` | ED | Clear screen (0=below, 1=above, 2=entire) |
| `ESC[K` | EL | Clear line (0=right, 1=left, 2=entire) |
| `ESC[m` / `ESC[...m` | SGR | Set graphic rendition (colors, bold, reset) |
| `ESC[6n` | DSR | Device status report (cursor position) |
| `ESC[s` | SCP | Save cursor position |
| `ESC[u` | RCP | Restore cursor position |
| `ESC[?25h` | DECTCEM | Show cursor |
| `ESC[?25l` | DECTCEM | Hide cursor |

**SGR parameters (for `ESC[...m`):**

| Code | Meaning |
|------|---------|
| 0 | Reset all attributes |
| 1 | Bold (use bright palette) |
| 7 | Reverse video |
| 22 | Normal intensity (cancel bold) |
| 27 | Cancel reverse |
| 30-37 | Set foreground color (standard 8) |
| 39 | Default foreground |
| 40-47 | Set background color (standard 8) |
| 49 | Default background |
| 90-97 | Set bright foreground |

#### Implementation location

The parser lives in `tty_write()` in kernel/tty.c because:
- It must work on both platforms (workbench passes escapes to host
  terminal natively, but the parser still needs to track cursor state)
- Actually, on workbench, ANSI escapes pass through to the host terminal
  and work natively. The parser only needs to intercept on Mega Drive.

**Decision point:** Parser in tty.c (portable) vs pal_console_putc()
(MD-only)?

**Recommendation: Parser in pal_console_putc() (Mega Drive only).**
Rationale:
- On workbench, the host terminal already handles ANSI escapes natively.
  Parsing them in the kernel would be wasted work.
- The parser needs to call MD-specific functions (plot_char with palette
  bits, clear_across, clear_lines, cursor positioning). Putting it in
  the PAL layer keeps these calls local.
- Cursor state (cursor_x, cursor_y) is already in platform.c.

**Alternative considered:** Parser in tty_write() with a
`pal_console_supports_ansi()` flag. This would let the parser track
cursor position on both platforms, which is useful if curses needs to
query cursor position. However, the host terminal handles this via
DSR/CPR already, and the added complexity isn't justified.

#### State storage

```c
/* In pal/megadrive/platform.c */
static uint8_t  esc_state;          /* 0=normal, 1=ESC, 2=CSI */
static uint8_t  esc_params[4];      /* up to 4 numeric params */
static uint8_t  esc_nparam;         /* number of params collected */
static uint8_t  esc_partial;        /* partial number being collected */
static uint16_t current_attr;       /* palette bits for plot_char */
static uint8_t  saved_x, saved_y;   /* saved cursor position */
```

12 bytes of state. No dynamic allocation.

#### Changes to pal_console_putc()

Replace the current simple control-character handler with the state
machine. Normal characters (state=0) go through the existing path
with `current_attr` ORed into the tile index. ESC and CSI states
accumulate parameters and dispatch on the final character.

The key change to plot_char: currently `plot_char(y, x, c)` writes
tile index `c` directly. We need to OR in palette bits:

```c
plot_char(cursor_y, cursor_x, (uint16_t)c | current_attr);
```

Where `current_attr` has the palette select bits in bits 13-12 of the
nametable entry word.

#### Changes to devvt.S plot_char

Currently plot_char writes the character value directly as the tile index.
The nametable entry format is:

```
Bit 15:    Priority
Bit 13-12: Palette select (0-3)
Bit 11:    V-flip
Bit 10:    H-flip
Bit 9-0:   Tile index
```

The C caller will pass the full nametable entry word (tile + palette bits),
so plot_char needs no changes — it already writes the 16-bit value from
the stack to VDP_DATA. The caller just needs to pass the right value.

**Wait — verify:** plot_char loads `14(%sp)` as a word and writes it to
VDP_DATA. Currently the caller passes just the ASCII value (0-127). If
we pass `c | (palette << 13)`, the palette bits will be in the upper
byte. This should work because plot_char does `move.w 14(%sp), %d0`
and `move.w %d0, (VDP_DATA)` — it preserves all 16 bits. **Confirmed:
no assembly changes needed.**

However, `clear_across` and `clear_lines` write 0 to the nametable,
which clears both the tile and palette bits. This is correct — cleared
cells should have the default palette (palette 0).

**Also:** `read_cursor_char` reads the full nametable entry including
palette bits. The cursor sprite code uses this to display the character
under the cursor. This should work as-is since cursor_sprite sets its
own palette (palette 2) via the sprite attributes.

---

### Phase V3: VDP Palette and Color Attribute Support

**Priority:** High — enables colored terminal output
**Effort:** ~30 lines (palette data + platform.c changes)
**Dependencies:** V2 (escape parser must be in place to set attributes)

#### Palette layout

The 1bpp→4bpp font expansion uses color indices 1 (background) and 2
(foreground) within each palette. By changing palette select bits per
character, we get different color pairs.

**Palette 0 — default console (currently 3 colors used, 13 free):**

| Index | Color | VDP Value | Purpose |
|-------|-------|-----------|---------|
| 0 | Transparent | 0x0000 | (unused) |
| 1 | Black | 0x0000 | Background (font bit 0) |
| 2 | Light gray | 0x0AAA | Default foreground (font bit 1) |

We keep palette 0 as the default (light gray on black). This matches
standard terminal appearance.

**Palette 1 — debug overlay (keep as-is)**

**Palette 2 — cursor sprite (keep as-is)**

**Palette 3 — bold/bright console colors:**

| Index | Color | VDP Value |
|-------|-------|-----------|
| 0 | Transparent | 0x0000 |
| 1 | Black | 0x0000 |
| 2 | White | 0x0EEE |

#### Color model

With a 1bpp font, each character can only have two colors: the palette's
index 1 (background) and index 2 (foreground). Palette select gives us
4 foreground colors (one per palette).

**This is severely limited.** 4 palettes × 1 foreground color = only 4
possible foreground colors on black background. This is NOT enough for
ANSI's 8+8 color model.

**Better approach — multiple font copies with different color indices:**

To get N foreground colors, we need N copies of the font in VRAM, each
using a different color index for the foreground pixels. With 4 palettes
× 16 color indices, we can encode up to 60 distinct foreground colors
(minus reserved slots).

But 256 glyphs × 32 bytes/glyph = 8 KB per font copy. The full VRAM is
64 KB with ~48 KB available for tiles after nametable/sprite tables.
We could fit 6 font copies = 8 ANSI colors × 1 background, using 48 KB.
That's too much — it leaves no room for user graphics.

**Practical compromise: 2 font copies for 2 foreground colors + palette
rotation for 8 total:**

Actually, let's reconsider. The font expansion in VDP_fontInit uses this
scheme for each pixel bit:
- bit=0 → nibble=1 (color index 1 = background)
- bit=1 → nibble=2 (color index 2 = foreground)

What if we change the foreground color index per-palette? Currently
palette 0 has white at index 2. If we put red at index 2 of palette 0,
green at index 2 of palette 1, blue at index 2 of palette 2, yellow at
index 2 of palette 3, we get 4 foreground colors from the same font
tiles by just changing the palette select bits.

**This gives 4 colors for free (zero VRAM cost).** For 8 ANSI colors
we'd need 2 copies of the font with different color index pairs:
- Font copy A: uses color indices 1 (bg) and 2 (fg) — with palettes
  0-3 gives 4 foreground colors
- Font copy B: uses color indices 1 (bg) and 3 (fg) — with palettes
  0-3 gives 4 more foreground colors
- Total: 8 foreground colors, 16 KB VRAM (2 × 8 KB), 4 palettes

Each nametable entry selects: font copy (via tile index range 0-127 vs
128-255) + palette (via bits 13-12) → 2 × 4 = 8 color combinations.

**RAM/VRAM cost:** +8 KB VRAM for second font copy. No RAM cost.
The user tile area starts at 0x1000 (tile 128). A second font at tiles
128-255 uses 0x1000-0x1FFF. User tiles then start at tile 256 (0x2000).
This still leaves ~40 KB for user tiles — plenty.

#### Recommended palette layout for 8 ANSI colors

Font copy A (tiles 0-127): foreground = color index 2
Font copy B (tiles 128-255): foreground = color index 3

| Palette | Index 1 (bg) | Index 2 (fg-A) | Index 3 (fg-B) | Colors achieved |
|---------|-------------|----------------|----------------|-----------------|
| 0 | Black 0x0000 | Red 0x000E | Dark gray 0x0888 | red, dk-gray |
| 1 | Black 0x0000 | Green 0x00E0 | Blue 0x0E00 | green, blue |
| 2 | Black 0x0000 | Yellow 0x00EE | Magenta 0x0E0E | yellow, magenta |
| 3 | Black 0x0000 | Light gray 0x0AAA | Cyan 0x0EE0 | lt-gray, cyan |

Wait, this conflates the cursor sprite palette (palette 2) and debug
overlay palette (palette 1). We need those.

**Revised approach — use all 4 palettes for console, relocate cursor
and debug overlay:**

Actually, the cursor is a sprite. Sprite palette is stored in the sprite
attribute (bits 13-12 of the tile word in the sprite table), independent
of the nametable palette. But the cursor sprite uses palette 2's colors.
We can put cursor colors in any palette — just set the sprite's palette
bits accordingly.

The debug overlay uses palette 1 via the nametable on Plane B. If we
repurpose palette 1 for console colors, the debug overlay loses its
independent colors. But the debug overlay only needs 2 colors (bg + fg).
If we make debug overlay use the same palette as console (palette 0 or
any palette with black bg + visible fg), it still works.

**Simplest practical approach — stay with palette select only (4 colors),
defer 8-color support:**

Given the complexity of the palette juggling, let's start with the
simplest approach that delivers real value:

1. **4 foreground colors via palette select** (zero VRAM cost):
   - Palette 0: default (light gray on black) — normal text
   - Palette 1: keep for debug overlay
   - Palette 2: keep for cursor sprite
   - Palette 3: bright white on black — bold text

   This gives: normal (palette 0) and bold (palette 3). Two visual
   styles, which is what most terminal apps actually need.

2. **Remap palette 0's unused color slots for extra colors:**
   The font only uses indices 1 and 2. Indices 3-15 are free. But
   the font tiles don't reference these indices, so they're invisible.
   No use.

3. **Full 8-color support** requires either:
   (a) Multiple font copies in VRAM (8 KB each), or
   (b) Modifying VDP_fontInit to generate tiles with different color
       indices per color group

   Option (b) is more VRAM-efficient: instead of duplicating the
   entire 128-glyph font, generate only the printable ASCII range
   (32-126 = 95 glyphs) for each extra color. 95 × 32 = 3040 bytes
   per color variant.

**Decision needed:** Start with 2-palette (normal + bold) or go
straight to 8-color? The escape parser supports full SGR either way.
The difference is only in the palette/VRAM setup.

#### Recommended phasing

**V3a:** 2-color (normal + bold) — change palette 3 colors, add
`current_attr` to platform.c, wire SGR bold/reset to palette select.
~20 lines, 0 VRAM cost. Ships immediately.

**V3b (future):** 8-color via font color variants. Generate 7 extra
font sets (for the 7 non-default ANSI colors) at boot time by running
VDP_fontInit with different color indices. ~21 KB VRAM, ~40 lines of
font init changes. Can be added later without changing the escape
parser.

For this plan, we implement V3a and design the escape parser to accept
full SGR codes. When V3b ships, the parser just needs to map color
codes to the right tile range + palette combo.

#### V3a implementation

In vdp.S, update palette data for palette 3:

```
palette3_color2: .word 0x0EEE   /* bright white foreground */
```

In platform.c:

```c
static uint16_t current_attr = 0;  /* bits 13-12 = palette select */

/* In the escape parser's SGR handler: */
case 0: current_attr = 0; break;                   /* reset */
case 1: current_attr = (3 << 13); break;            /* bold → palette 3 */
case 22: current_attr = 0; break;                   /* normal → palette 0 */

/* In the character output path: */
plot_char(cursor_y, cursor_x, (uint16_t)c | current_attr);
```

---

### Phase V4: Minimal Curses Library

**Priority:** Medium — enables TUI apps (games, editors with status lines)
**Effort:** ~300-400 lines in libc/curses.c + libc/include/curses.h
**Dependencies:** V2 (escape parser), V3a (at least normal+bold)

#### API surface

```c
/* Initialization */
WINDOW *initscr(void);
int endwin(void);

/* Output */
int addch(int c);
int addstr(const char *s);
int mvaddch(int y, int x, int c);
int mvaddstr(int y, int x, const char *s);
int printw(const char *fmt, ...);

/* Cursor */
int move(int y, int x);
int getyx(WINDOW *win, int *y, int *x);

/* Attributes */
int attron(int attrs);
int attroff(int attrs);
int attrset(int attrs);

/* Color */
int start_color(void);
int init_pair(int pair, int fg, int bg);
#define COLOR_PAIR(n) ...

/* Screen management */
int clear(void);
int clrtoeol(void);
int refresh(void);
int erase(void);

/* Input */
int getch(void);
int nodelay(WINDOW *win, int bf);
int raw(void);
int noraw(void);
int cbreak(void);
int nocbreak(void);
int noecho(void);
int echo_curses(void);   /* avoid conflict with termios echo */
int keypad(WINDOW *win, int bf);

/* Terminal info */
int LINES;   /* set by initscr() */
int COLS;    /* set by initscr() */

/* Key constants */
#define KEY_UP    0x101
#define KEY_DOWN  0x102
#define KEY_LEFT  0x103
#define KEY_RIGHT 0x104
/* etc. */
```

#### Implementation approach

**Option A: Emit ANSI escapes through write(1, ...).**
- Works on both platforms (workbench terminal + Mega Drive VDP parser)
- Simple: curses functions just build escape strings and write them
- `refresh()` is a no-op (or just `fsync()`)
- No shadow buffer needed
- Performance: adequate for 40×28 at 7.67 MHz — even full screen
  redraw is ~1120 characters × ~100 cycles per escape = ~112K cycles
  = ~15ms. Well under one frame.

**Option B: Direct VDP nametable writes via `/dev/vdp` ioctls.**
- Faster (no escape parsing overhead)
- MD-only — would need workbench fallback
- Conflicts with exclusive `/dev/vdp` access model
- More complex implementation

**Decision: Option A.** The performance is adequate and portability
across both platforms is valuable. The escape parser overhead is
negligible.

#### WINDOW type

```c
typedef struct {
    int cur_y, cur_x;       /* cursor position */
    int max_y, max_x;       /* dimensions */
    int attrs;              /* current attributes */
    int delay;              /* nodelay flag */
} WINDOW;

extern WINDOW *stdscr;
```

20 bytes. No screen buffer (we emit escapes directly).

If refresh()-based diffing is ever needed (batching updates to reduce
flicker), add a 1120-byte shadow buffer (40×28 × 1 byte char + 1 byte
attr = 2240 bytes, or 1120 bytes char-only). Not in V4 scope.

#### initscr() / endwin()

```c
WINDOW *initscr(void)
{
    /* Query terminal size via TIOCGWINSZ */
    struct winsize ws;
    ioctl(0, TIOCGWINSZ, &ws);
    LINES = ws.ws_row;
    COLS = ws.ws_col;

    /* Set raw mode, no echo */
    tcgetattr(0, &saved_termios);
    struct termios t = saved_termios;
    t.c_lflag &= ~(ICANON | ECHO | ISIG);
    t.c_iflag &= ~ICRNL;
    tcsetattr(0, TCSANOW, &t);

    /* Clear screen, home cursor */
    write(1, "\033[2J\033[H", 7);

    stdscr->cur_y = 0;
    stdscr->cur_x = 0;
    stdscr->max_y = LINES;
    stdscr->max_x = COLS;
    stdscr->attrs = 0;
    return stdscr;
}

int endwin(void)
{
    /* Reset attributes, show cursor */
    write(1, "\033[0m\033[?25h", 10);
    tcsetattr(0, TCSANOW, &saved_termios);
    return OK;
}
```

#### getch() — input with escape sequence decoding

On Mega Drive, the Saturn keyboard delivers keycodes directly via
VBlank ISR → tty_inproc(). Arrow keys are already decoded by the
keyboard driver into raw bytes. On workbench, arrow keys arrive as
ANSI escape sequences (`ESC[A`, etc.).

curses `getch()` must handle both:
1. Read one byte
2. If it's ESC (0x1B), read more with short timeout to decode sequence
3. Return KEY_UP/KEY_DOWN/etc. for recognized sequences

On Mega Drive with raw keyboard, the keyboard driver could deliver
arrow keys as either raw scancodes or pre-translated ANSI sequences.
Currently it delivers ASCII + special keycodes. We'll need to map
these to curses KEY_* constants.

#### Color support in curses

With V3a (2 palettes = normal + bold):
- `COLOR_PAIR(0)` = normal
- `A_BOLD` = bright/bold

With V3b (8 colors, future):
- `init_pair(1, COLOR_RED, COLOR_BLACK)` → maps to the right
  palette + tile range
- `COLOR_PAIR(1)` returns the attr bits

The curses color API abstracts over the underlying VDP palette mapping.
Apps use standard curses color calls regardless of how many colors the
VDP terminal currently supports.

#### Build integration

- Add `libc/curses.c` to libc sources in Makefile
- Add `libc/include/curses.h` header
- Apps that use curses: `#include <curses.h>`, link normally

---

### Phase V5: Performance Optimizations

**Priority:** Medium — pure speed improvements, no correctness impact
**Effort:** ~120 lines of assembly
**Dependencies:** V1-V3 for VDP-related items; division/memcpy are independent

These come from optimization-plan.md. We include the VDP-related ones
here since they affect scroll/clear performance, and the top general
optimizations since they benefit everything including terminal output.

#### V5.1: VDP DMA for scroll clear (~30 lines)

Currently `clear_lines` writes zeros to the nametable in a CPU loop
(~40 words × dbra). VDP DMA fill can zero an entire nametable row in
~10µs vs ~160µs for the CPU loop.

```
; DMA fill: set length, source=0, mode=fill
; Write to VDP control port to initiate
```

Impact: ~10x faster screen clear. Noticeable when scrolling rapidly
(e.g., `cat` of a large file).

#### V5.2: Division fast path (~20 lines)

Replace divmod.S with a version that checks if the divisor fits in
16 bits and uses DIVU.W (~150 cycles vs ~300-600 cycles for the
generic shift-and-subtract). Almost all divisors in Genix are small
(INODES_PER_BLK=21, base 10/16, block size 512).

This is independent of VDP work but included because it speeds up
everything including the escape parser's numeric parameter parsing.

See optimization-plan.md Priority 1 for full FUZIX source reference.

#### V5.3: Assembly memcpy/memset (~40 lines)

MOVEM.L bulk transfers: 4x speedup for 512-byte blocks. Benefits
filesystem operations, pipe throughput, and VDP tile uploads.

See optimization-plan.md Priority 3.

#### V5.4: Pipe bulk copy (~15 lines)

Replace byte-at-a-time pipe copy loop with memcpy of contiguous
chunks. 2-4x speedup for pipeline-heavy workloads.

See optimization-plan.md Priority 4.

#### V5.5: SRAM 16-bit I/O (~10 lines)

Word-width SRAM writes via MOVEM.L. ~20x speedup for SRAM disk.

See optimization-plan.md Priority 2.

---

## 4. Rejected Approaches

### Direct VDP curses (Option B)

Writing directly to VDP nametable from curses via ioctls was rejected
because:
- Conflicts with exclusive `/dev/vdp` access model
- Requires separate workbench fallback code path
- Performance difference is negligible at 40×28 resolution
- The escape parser is needed anyway (levee, other apps emit ANSI)

### Full termcap/terminfo database

FUZIX uses termcap for terminal abstraction. Rejected for Genix because:
- Single terminal type (VDP 40×28) — no need for database
- termcap adds ~500 lines + data tables for zero benefit
- curses can hardcode the escape sequences it emits
- If we ever support multiple terminal types, revisit

### Parser in tty.c (kernel-portable)

Parsing ANSI escapes in the kernel TTY layer was considered but
rejected because:
- Workbench doesn't need it (host terminal handles escapes)
- Would add ~200 lines to the kernel that only run on Mega Drive
- Parser needs to call MD-specific functions (plot_char, clear_across)
- Keeping it in the PAL layer is cleaner

### 8-color via palette-only (no font copies)

With only 4 palettes and a 2-color font (indices 1 and 2), we can only
get 4 foreground colors by varying which color is at index 2 in each
palette. Getting 8 requires either:
- 2 font copies with different color indices (indices 2 and 3), or
- Dynamically rewriting palette colors per-scanline (H-interrupt trick,
  too complex and fragile)

The 2-font approach is practical and deferred to V3b. V3a ships with
2 visual styles (normal + bold) which covers 90% of terminal use cases.

### Shadow buffer for curses refresh()

A 2240-byte shadow buffer (40×28 × 2 bytes for char+attr) would enable
refresh() to diff against previous state and only update changed cells.
Rejected for V4 because:
- 2240 bytes is significant on a 64 KB system (~3.5% of total RAM)
- Emit-as-you-go (no buffer) is fast enough at 40×28
- Can be added later if flicker becomes a problem
- Most curses apps update small regions anyway (status bar, game board)

---

## 5. Testing Strategy

### V1 testing (device driver fixes)

- **Host test:** Add `test_vdp_open_close()` to tests/test_vdp.c
  verifying open/close dispatch logic
- **Workbench autotest:** Verify VDP opens return -ENODEV on workbench
  (no VDP hardware)
- **BlastEm autotest:** Open `/dev/vdp`, verify exclusive access, verify
  cleanup on close

### V2 testing (escape parser)

- **Host test:** New `tests/test_ansi.c` exercising the parser state
  machine with known input sequences and verifying cursor position /
  attribute state changes. The parser can be tested independently of
  VDP hardware by mocking plot_char/clear_across.
- **Workbench:** Verify escapes still pass through to host terminal
  (parser only active on MD)
- **BlastEm:** Visual verification — `echo -e '\033[2J\033[5;10Hhello'`
  should clear screen and print "hello" at row 5, col 10
- **Levee:** Cursor positioning should work correctly in the editor

### V3 testing (colors)

- **BlastEm:** Write a small test program that emits SGR sequences for
  each color and verifies visual output
- **Levee:** Bold/highlighted text should be visibly different

### V4 testing (curses)

- **Host test:** Test curses API functions (move, addch, attribute
  setting) — these just build escape strings
- **BlastEm:** Write a simple curses demo app (hello world with color
  and cursor movement)
- **Validate with existing apps:** Port one simple TUI app (e.g., a
  snake game or tetris) to prove the API works end-to-end

### V5 testing (performance)

- Existing test suites catch correctness regressions
- Workbench cycle counting for before/after measurements
- `make test-all` must pass after each optimization

### Full test ladder (per CLAUDE.md)

Every phase must pass:
1. `make test` — host unit tests
2. `make kernel` — cross-compilation check
3. `make test-emu` — workbench autotest
4. `make megadrive` — Mega Drive build
5. `make test-md-auto` — BlastEm autotest

---

## 6. RAM Budget

All costs are for the Mega Drive target (64 KB main RAM).

| Item | RAM cost | Notes |
|------|---------|-------|
| Escape parser state | 12 bytes | static in platform.c |
| Saved cursor position | 2 bytes | static in platform.c |
| current_attr | 2 bytes | static in platform.c |
| vdp_graphics_mode flag | 1 byte | static in platform.c |
| Curses WINDOW struct | 20 bytes | static in libc |
| Curses saved_termios | ~20 bytes | static in libc |
| **Total kernel** | **~17 bytes** | Negligible |
| **Total libc (curses)** | **~40 bytes** | Per-process, only when using curses |

VRAM costs (Mega Drive VDP, 64 KB):
| Item | VRAM cost | Notes |
|------|-----------|-------|
| V3a (normal+bold) | 0 bytes | Palette data only (in CRAM) |
| V3b (8 colors, future) | ~21 KB | 7 font variants × 3040 bytes |

No additional main RAM for palettes — palette data lives in CRAM
(128 bytes, separate from main RAM) and is loaded at boot from ROM.

---

## 7. File Inventory

### New files

| File | Purpose | Phase |
|------|---------|-------|
| `libc/curses.c` | Minimal curses implementation | V4 |
| `libc/include/curses.h` | Curses public API | V4 |
| `tests/test_ansi.c` | Escape parser unit tests | V2 |

### Modified files

| File | Changes | Phase |
|------|---------|-------|
| `kernel/proc.c` | Device open/close dispatch in sys_open/sys_close | V1 |
| `pal/megadrive/platform.c` | Escape parser, color attrs, graphics mode flag | V1, V2, V3 |
| `pal/megadrive/vdp.S` | Palette data for palette 3, DMA fill | V3, V5 |
| `pal/megadrive/devvt.S` | DMA-based clear_lines (V5 only) | V5 |
| `pal/megadrive/dev_vdp.c` | Set/clear graphics mode flag | V1 |
| `libc/gfx.c` | Ctrl-Z safety (ISIG disable) | V1 |
| `tests/test_vdp.c` | Device open/close tests | V1 |
| `kernel/divmod.S` | Division fast path | V5 |
| `libc/Makefile` | Add curses.c | V4 |
| `apps/Makefile` | Curses demo app (optional) | V4 |

---

## 8. Open Questions

### Q1: 2-color (normal+bold) vs 8-color for initial release?

V3a gives 2 visual styles (normal + bold) with zero VRAM cost.
V3b gives 8 ANSI foreground colors at ~21 KB VRAM cost. This is a
simple-vs-complex choice that affects VRAM budget and boot time
(generating 7 extra font variants). Recommend starting with V3a.

### Q2: Escape parser location — platform.c or new file?

The parser will be ~200 lines. platform.c is currently 308 lines.
Adding 200 more makes it ~500 lines, which is still manageable but
approaching the "readable in one sitting" limit. Could split into
`pal/megadrive/ansi.c`. The `cursor_x`/`cursor_y` state would need
to be shared (extern or passed as arguments).

### Q3: Curses getch() timeout mechanism

For `nodelay()` mode (non-blocking input) and escape sequence timeout
(distinguishing ESC key from ESC sequence), we need a timer. Options:
- (a) Poll `pal_timer_ticks()` for elapsed time — but this is a
  kernel function, not accessible from userspace
- (b) Use `VMIN=0 VTIME=1` in termios raw mode — gives ~100ms timeout
  per read, which is standard for escape sequence detection
- (c) Non-blocking read via O_NONBLOCK on stdin

Option (b) requires VTIME support in tty_read(). Currently tty_read()
ignores VTIME. Implementing VTIME is ~20 lines in tty.c and has
general value beyond curses.

### Q4: Should V5 optimizations be a separate plan?

The optimization-plan.md already exists as a detailed plan. V5 here
references it. The VDP DMA optimization naturally fits with this plan.
The general optimizations (divmod, memcpy, pipe) are included for
convenience but could execute independently.

---

## Implementation Order

```
V1.1 Device open/close dispatch     ← must-fix, do first
V1.2 Console suppression            ← quick win
V1.3 Ctrl-Z safety                  ← quick win
V2   ANSI escape parser             ← biggest value
V3a  Normal + bold palettes         ← enables bold text
V4   Minimal curses                 ← enables TUI apps
V5.1 VDP DMA clear                  ← perf, natural with VDP work
V5.2 Division fast path             ← perf, independent
V5.3 Assembly memcpy/memset         ← perf, independent
V5.4 Pipe bulk copy                 ← perf, depends on V5.3
V5.5 SRAM 16-bit I/O               ← perf, independent
```

V1 and V2 are the critical path. Everything else can be reordered
or deferred without impact.
