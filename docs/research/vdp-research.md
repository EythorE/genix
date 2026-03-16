# VDP Research: Terminal, Graphics, and Curses

Research into VDP terminal optimizations, graphics capabilities, and
missing features by comparing Genix's current implementation against
the original FUZIX Mega Drive port
([EythorE/FUZIX megadrive branch](https://github.com/EythorE/FUZIX/tree/megadrive/Kernel/platform/platform-megadrive)).

**Key discovery:** FUZIX upstream does NOT have a Mega Drive platform.
The Mega Drive port directory (`/home/eythor/FUZIX/Kernel/platform/platform-megadrive/`)
referenced in plans/optimization-plan.md was a local fork, not upstream code. The
VDP terminal, graphics driver, and debug overlay are original Genix work.

---

## Table of Contents

1. [VDP Terminal: Code Comparison](#1-vdp-terminal-code-comparison)
2. [FUZIX imshow vs Genix imshow](#2-fuzix-imshow-vs-genix-imshow)
3. [Curses Library Analysis](#3-curses-library-analysis)
4. [VDP Exclusive Access and Cleanup](#4-vdp-exclusive-access-and-cleanup)
5. [VDP Device Cleanup Bug](#5-vdp-device-cleanup-bug)
6. [Ctrl-Z / SIGTSTP Edge Case](#6-ctrl-z--sigtstp-edge-case)
7. [Plane B Debug Terminal](#7-plane-b-debug-terminal)
8. [Proposed Improvements](#8-proposed-improvements)
9. [Implementation Priority](#9-implementation-priority)
10. [Appendix: File Inventory](#appendix-file-inventory)

---

## 1. VDP Terminal: Code Comparison

### Assembly Routines (devvt.S)

Genix's `pal/megadrive/devvt.S` is a direct adaptation of FUZIX's
`Kernel/platform/platform-megadrive/devvt.S`. Side-by-side comparison
shows they are **functionally identical**:

| Routine | FUZIX | Genix | Difference |
|---------|-------|-------|------------|
| `plot_char` | Scroll-adjusted VRAM write | Same | Variable renamed (`scroll_amount` -> `vt_scroll_amount`) |
| `clear_across` | VRAM zeroing loop | Same | None |
| `clear_lines` | Row-by-row clear, 20 longs/row | Same | None |
| `scroll_up` | VSRAM += 8 | Same | None |
| `scroll_down` | VSRAM -= 8 | Same | None |
| `cursor_on` | Sprite 0 positioned at char | Same | None |
| `cursor_off` | No-op (commented out) | No-op | None |
| `read_cursor_char` | Read tile under cursor from VRAM | Same | None |

Both use hardware vertical scrolling via VSRAM (zero-copy, just a
register write). The nametable is treated as a circular buffer of 32
rows; scroll wraps around modulo 32 and the newly exposed line is
cleared.

**Verdict:** No optimization gap in the terminal rendering layer.

### What's Already Optimal

plans/optimization-plan.md (lines 436-451) confirms these are already at parity
with FUZIX or better:

| Component | Status |
|-----------|--------|
| VDP text output (`devvt.S`) | Assembly, identical to FUZIX |
| VDP initialization (`vdp.S`) | Assembly, identical to FUZIX |
| Font loading (`vdp.S:120-145`) | Assembly bit rotation, identical |
| Saturn keyboard (`keyboard_read.S`) | Assembly, nearly identical |
| Hardware VSRAM scroll | ~20 cycles vs ~2240 bytes memory copy |

The key optimization -- **hardware vertical scroll via VSRAM** instead of
copying nametable data -- is already implemented. `scroll_up()` in `devvt.S`
adds 8 to a scroll counter and writes one word to VSRAM. This is ~100x
faster than copying 40x28 tiles.

### VDP Initialization (vdp.S)

Also functionally identical. Both use the same:
- 24 VDP register values (H40 mode, 320x224, display on, DMA enabled)
- VRAM layout (Plane A at 0xC000, Plane B at 0xE000, sprites at 0xF000)
- Font expansion (1bpp -> 4bpp via ROXL bit rotation)
- Palette layout (4 palettes x 16 colors)

Minor differences:
- FUZIX has `VDP_WriteTMSS` (trademark security) -- Genix skips this
  (handled in crt0.S differently)
- FUZIX's `VDP_reinit` calls `dbg_clear` to reset the debug plane;
  Genix's does too (via `VDP_reinit` -> `dbg_clear` call chain)
- FUZIX spells it "Pallete"; Genix uses "Palette"

**Verdict:** No optimization gap.

### Console putc (platform.c vs devtty.c)

FUZIX routes output through its VT layer (`vtoutput()`), which
provides ANSI escape sequence parsing, cursor addressing, attribute
handling, and scrolling. Genix uses a simpler `pal_console_putc()` in
`pal/megadrive/platform.c` that handles `\n`, `\r`, `\t`, `\b`, auto-wrap,
and auto-scroll directly.

FUZIX's `vtoutput()` is a more complete terminal emulator with escape
sequences, while Genix handles escape sequences in the TTY layer
(`kernel/tty.c`). The net result is equivalent for basic terminal
output; neither has a significant performance advantage here since the
bottleneck is VDP write speed, not control character parsing.

**Verdict:** Functionally equivalent for current use. FUZIX's VT layer
is more feature-rich (supports more escape sequences) but Genix's TTY
layer handles the ones needed for dash and levee.

### Character Output Path

Current path (same as FUZIX):
```
printf -> kputc -> pal_console_putc -> plot_char (devvt.S)
```

`plot_char` writes one VDP control word + one data word per character.
No batching. FUZIX doesn't batch either -- character-at-a-time is adequate
for a 40x28 text console at 7.67 MHz.

### VDP Access Serialization (Terminal Layer)

FUZIX relies on the 68000's single-threaded ISR model: all VDP writes
happen in syscall context or ISR context, never concurrently. Genix does
the same. No explicit locks are needed because:

1. Syscalls run with interrupts enabled but are non-preemptive
2. The timer/keyboard ISR runs atomically
3. The VDP driver is ioctl-only (no read/write paths that could race)

**This is correct and sufficient.** No locking overhead needed.

### Potential Terminal Optimization: Batched Line Clear

`clear_lines` in `devvt.S` writes zeros one word at a time. For clearing
multiple lines (e.g., `clear` command), a DMA fill could clear an entire
nametable region in one operation:

```
DMA fill: set VDP register 19/20 (length), register 21/22/23 (source),
trigger via control port. Hardware fills VRAM at ~8 bytes/scanline.
```

**Impact:** Marginal. Full-screen clear takes ~5000 cycles currently
(40x28 = 1120 words x ~4 cycles). DMA setup overhead is ~50 cycles but
runs during active display. Only matters for rapid full-screen redraws.

**Recommendation:** Not worth the complexity. Current approach is fine.

---

## 2. FUZIX imshow vs Genix imshow

### FUZIX imshow -- Full Image Viewer

FUZIX's `imshow.c` is a **real image viewer** that:

1. Opens `/dev/vdp` for exclusive graphics access
2. Reads `.simg` files from disk (a custom binary image format)
3. Supports directory browsing with left/right arrow navigation
4. Uses all 4 VDP planes for display:
   - **4 palettes** (60 usable colors via K-means clustering)
   - **Plane A** -- foreground layer (high priority tiles)
   - **Plane B** -- background layer (palette 1 tiles)
   - **Sprites** -- two overlapping 4x4 sprite grids for additional colors
5. Sets terminal to raw mode for keypress input
6. Handles SIGINT/SIGTERM with a cleanup handler
7. Calls `ioctl(vdpdev, VDPRESET, 0)` on exit to restore console

The `.simg` format (generated by `create_image.py`) contains:
- 4 palettes x 16 colors = 128 bytes of CRAM data
- Plane A tilemap (128x128 pixels = 16x16 tiles)
- Plane B tilemap (16x16 tiles)
- 2 sprite tilemaps (4x4 sprites of 4x4 tiles each)
- Total: ~32 KB per image

The `imshow_routines.S` assembly performs:
- Direct palette upload to CRAM (all 4 palettes)
- Tile data upload to VRAM (starting at tile 128+)
- Nametable mapping for both Plane A and Plane B
- Sprite attribute table setup (two layers of 4x4 grid sprites)

### Genix imshow -- Test Pattern Only

Genix's `imshow.c` is a **test pattern generator** that:
- Generates solid, checkerboard, and gradient tiles procedurally
- Uses only Plane A with palette 0 (16 colors)
- Maps tiles to screen in a color bar pattern
- Supports `-n` for automated testing
- No file I/O -- everything is computed at runtime

### What's Missing in Genix for Full imshow

To port FUZIX's image viewer capability, Genix needs:

1. **Multi-palette support in the driver** -- Currently `gfx_palette()`
   can write any palette, so this already works at the ioctl level.

2. **Plane B nametable access** -- The driver only exposes Plane A
   (`vdp_do_setmap` writes to `VRAM_PLANE_A`). Need a way to specify
   target plane, or add a `VDP_IOC_SETMAP_B` ioctl.

3. **Bulk tile upload** -- The current `VDP_IOC_LOADTILES` works but is
   ioctl-per-batch. FUZIX's `imgInit` uploads 32 KB of tile data in a
   single unrolled DBRA loop directly to VRAM. The ioctl overhead per
   call is non-trivial on a 7.67 MHz CPU. A bulk upload path (or DMA)
   would help.

4. **Image file format support** -- Need either the `.simg` format or a
   simpler format. The Python conversion pipeline
   (`create_image.py`) uses Floyd-Steinberg dithering and K-means
   clustering to reduce images to 60 colors across 4 palettes.

5. **Sprite layer for extra colors** -- FUZIX uses sprites as additional
   overlay layers for more color depth. The Genix gfx API already
   supports sprites, but imshow doesn't use them.

### What's Needed for Real Image Display (General)

To display actual images on the VDP:

1. **Image file format support** -- Need a decoder (PCX is simplest for
   4bpp paletted images: ~200 lines of C). PNG is too complex for 64 KB.
   Raw headerless formats are even simpler.

2. **Palette quantization** -- The VDP has 4 palettes x 16 colors.
   An image must be quantized to <=61 colors (palette 0 is console).
   Median-cut or popularity algorithm, but RAM-constrained.

3. **Tile deduplication** -- The VDP displays 8x8 tiles. A 320x224 image
   is 40x28 = 1120 tiles. With 44 KB of user tile space, we have room
   for 1408 tiles (44K / 32 bytes). An image with all-unique tiles fits.
   But deduplication would allow larger virtual images with scrolling.

4. **DMA tile upload** -- Current `vdp_do_loadtiles()` uses CPU writes
   (loop writing longwords). For 1120 tiles x 32 bytes = 35,840 bytes,
   this takes ~72,000 cycles. DMA could do it in background during VBlank
   (~17 KB/frame at 60 fps). Not strictly needed but enables smoother
   loading.

5. **Scrolling for oversized images** -- The VDP supports hardware scroll.
   Images larger than 320x224 could be scrolled with arrow keys using the
   existing `gfx_scroll()` API.

### Missing VDP Hardware Features

VDP Register 0x0B (mode set 3) is currently 0x00. Unexposed capabilities:

| Feature | Register Bits | Use Case |
|---------|--------------|----------|
| Per-line V-scroll | Reg 0x0B bit 2 | Parallax, wavy effects |
| Per-tile H-scroll | Reg 0x0B bits 0-1 | Column scroll, water effects |
| Window plane | Reg 0x11, 0x12 | Fixed HUD/status bar overlay |
| Interlace | Reg 0x0C bits 1-2 | 320x448 display (double height) |
| Shadow/highlight | Reg 0x0C bit 3 | Transparency effects |
| DMA fill | Reg 0x17 bit 7 | Fast VRAM clear |
| DMA copy | Reg 0x17 bit 6 | VRAM-to-VRAM block transfer |

**Recommendation:** Expose DMA fill (useful for fast clear) and
per-line/per-tile scroll modes via new ioctls. The others are niche
and can wait.

---

## 3. Curses Library Analysis

### FUZIX Curses Implementation

FUZIX builds `curses68000.lib` from a `curses/` directory in its
library tree. The library provides standard curses terminal control:
`initscr()`, `endwin()`, `mvprintw()`, `wrefresh()`, `newwin()`, etc.
It sits on top of FUZIX's termcap library (`termcap68000.lib`) and
uses the VT escape sequences that the FUZIX VT layer interprets.

The architecture:
```
User program -> curses API -> escape sequences -> devtty.c -> vtoutput() -> VDP
```

FUZIX also builds `readline68000.lib` for command-line editing with
history.

FUZIX curses is **termcap-driven**:

- `initscr()` reads `$TERM`, calls `setterm()` to load capabilities
- `setterm()` uses `tgetstr()` to load escape sequences: `cl` (clear),
  `cm` (cursor motion), `so`/`se` (standout), `vi`/`ve` (cursor visibility)
- `poscur(r, c)` calls `tputs(tgoto(cm, c, r), 1, outc)` -- generates
  terminal-specific cursor positioning escape sequences
- `doupdate()` uses per-line dirty tracking (`_minchng[]` / `_maxchng[]`)
  to only send changes to the terminal
- Character-level diffing within dirty lines avoids redundant writes

### Requirements to Port Curses to Genix

FUZIX curses needs these capabilities from the terminal:

1. **Cursor positioning** (`cm` capability) -- move cursor to arbitrary (row, col)
2. **Clear screen** (`cl` capability)
3. **termcap/terminfo database** -- `tgetent()`, `tgetstr()`, `tgoto()`, `tputs()`
4. **TERM environment variable** -- terminal type identification
5. **`tcsetattr()` / `tcgetattr()`** -- raw mode, echo control (already have this)

### What Genix Lacks

The VDP console has **no ANSI escape sequence processing**. Characters go
directly to `plot_char()`. There's no `\033[H` (cursor home), no
`\033[2J` (clear screen), no `\033[row;colH` (cursor position).

**Gap analysis:**

| Requirement | Genix Status | Effort |
|------------|-------------|--------|
| Cursor positioning | NOT IMPLEMENTED -- no escape sequence parser | Medium |
| Clear screen | NOT IMPLEMENTED -- no `\033[2J` handling | Small |
| termcap library | NOT IMPLEMENTED -- no tgetent/tgetstr | Medium-Large |
| TERM variable | Easy -- just set `$TERM` in shell startup | Trivial |
| Raw mode / echo | IMPLEMENTED -- termios in tty.c | Done |

### Would Curses Benefit Genix Now?

**Short answer: Not yet, and probably not worth a full port.**

Reasons:

1. **RAM cost.** A curses library adds significant code size. On the
   Mega Drive with 64 KB RAM and XIP text, the code would live in ROM
   (fine), but curses needs screen buffers (typically 80x24 = 1920
   bytes per window, doubled for current+previous). With only ~27.5 KB
   available for user programs, this is expensive.

2. **Limited benefit for current apps.** The primary interactive
   programs are dash (has its own line editor) and levee (has its own
   screen management). Neither uses curses.

3. **TTY layer already handles escape sequences.** Genix's TTY layer
   processes cursor positioning, line clearing, and basic terminal
   control -- enough for levee and dash's line editor.

4. **What curses would enable:** Games, menu-driven applications, and
   richer TUI programs. These are future possibilities but not current
   priorities.

### Implementation Path for Curses

**Option A: ANSI escape sequence parser + termcap (full curses)**

Add an ANSI escape sequence parser to `pal_console_putc()`:
- Parse `\033[` sequences for cursor motion, clear, attributes
- Implement ~10 key sequences (cursor move, clear line/screen, color)
- Port FUZIX's termcap library (small, ~500 lines)
- Port FUZIX's curses library (37 files, ~2000 lines total)
- Set `TERM=ansi` or `TERM=genix`

This gives full curses support: `vi`, `less`, `top`, screen-oriented apps.

**Estimated size:** ~3-4 KB code in libc + ~500 bytes escape parser in kernel

**Option B: Direct VDP curses (no escape sequences)**

Replace curses' `poscur()` with a direct VDP ioctl that positions the
cursor without escape sequences. Curses calls `poscur(row, col)` which
normally goes through termcap -> escape sequence -> terminal parser ->
hardware. We could short-circuit this:

```c
void poscur(int r, int c) {
    /* Direct kernel call to set VDP cursor position */
    ioctl(1, VDP_IOC_SETCURSOR, &(struct { int r, c; }){r, c});
}
```

**Problem:** This breaks the abstraction. Programs expecting termcap-based
curses won't work. And we'd need to port curses anyway for the windowing
and dirty-tracking logic. Not recommended.

**Option C: No curses -- use lineedit + raw VDP for specific apps**

Keep the current approach: `lineedit.c` for shell line editing, direct
`gfx_*` API for graphics apps. Don't port curses at all.

**Limitation:** Can't run `vi`, `less`, `top`, or any curses-based
application. The app porting roadmap (`docs/apps_to_port.md`) likely
includes some of these.

### Curses Recommendation

**Option A is the right long-term path.** The ANSI escape parser is
small (~200-300 lines), the termcap library is small, and FUZIX's curses
is already designed for constrained systems. The escape parser also
enables programs that use raw ANSI sequences directly (common in Unix
utilities).

If curses is desired sooner, a minimal implementation (just
`initscr`/`endwin`/`mvaddch`/`refresh`/`getch` without window
management) that operates directly on the VDP nametable via escape
sequences would save RAM. Skip full window support. Consider this a
Phase 9+ task at earliest.

**Phasing:**
1. First: ANSI escape parser in `pal_console_putc()` (enables `vi`-style
   apps that write raw ANSI)
2. Second: Port termcap library from FUZIX
3. Third: Port curses library from FUZIX
4. Validate with: `vi` (levee is already ported -- does it need curses?)

---

## 4. VDP Exclusive Access and Cleanup

### FUZIX Approach

FUZIX's `/dev/vdp` driver (`devvdp.c`) is **minimal**:
- `vdp_read()` -- prints args to kprintf and returns (debug leftover)
- `vdp_write()` -- prints args to kprintf and returns
- `vdp_ioctl()` -- supports only `VDPCLEAR` and `VDPRESET`
- **No exclusive access control** -- any process can open `/dev/vdp`
- **No automatic cleanup** -- the user program must call
  `ioctl(vdpdev, VDPRESET, 0)` before closing

The cleanup burden is on the application. FUZIX's imshow registers
`signal(SIGINT, cleanup_handler)` and `signal(SIGTERM, cleanup_handler)`
to handle Ctrl-C and kill, but has **no SIGTSTP handler** -- Ctrl-Z
would leave the VDP in graphics mode with no cleanup.

### Genix Approach (Current)

Genix's `/dev/vdp` driver (`pal/megadrive/dev_vdp.c`) is **much more
robust**:
- **Exclusive access**: `vdp_open()` tracks `vdp_owner` by PID;
  returns `-EBUSY` if already held
- **Automatic cleanup**: `vdp_close()` calls `VDP_reinit()` to
  fully restore the text console (VRAM clear, font reload, palette
  restore, scroll reset)
- **Rich ioctl set**: 7 commands for tiles, nametable, palette,
  sprites, scroll, vsync, and clear
- Read/write return `-EINVAL` (clean error, not debug prints)

### What's Missing

1. **Process exit cleanup**: If a process holding `/dev/vdp` is killed
   (SIGKILL, which can't be caught), the file descriptor should be
   closed automatically. This happens through the kernel's fd cleanup
   in `proc_exit()` -> `fd_close_all()` -> `vdp_close()`. **This
   already works** -- the kernel closes all fds on process exit.

2. **Interrupt safety**: The VBlank ISR calls `pal_keyboard_poll()`
   which writes to the TTY input buffer but does **not** touch the VDP
   data port. However, if a user program is mid-ioctl (writing tiles
   to VRAM) and the VBlank ISR fires, the ISR's VDP control port
   reads (keyboard polling doesn't read VDP, so this is safe) won't
   interfere. The cursor sprite update in `cursor_on()` could race
   with a graphics program's VDP writes if called from the ISR path,
   but currently the cursor is only updated from the console output
   path, not the ISR.

3. **Multi-process awareness**: If process A holds `/dev/vdp` and
   process B tries to write to the console (e.g., a background job
   printing), the console writes go through `pal_console_putc()` which
   writes directly to Plane A's nametable. This would corrupt the
   graphics program's display. **This is a real issue** -- kernel
   `kprintf` and any background process output will write to VDP while
   a graphics program owns it.

### Single Access Enforcement Design

Once `sys_open()` dispatches to `devtab[major].open()`:

1. First `open("/dev/vdp")` succeeds, sets `vdp_owner = pid`
2. Second `open("/dev/vdp")` from any process returns `EBUSY`
3. `close(fd)` calls `vdp_close()`, clears `vdp_owner`, calls `VDP_reinit()`
4. Process exit closes all fds, which triggers close handler

### Additional Hardening

1. **ISR-safe VDP access** -- Console output (kprintf) goes through
   `devvt.S` which writes VDP directly. If a user program has VDP open
   and the kernel panics, kprintf would write to Plane A while the user
   program's nametable is active. This is actually fine for panic output
   (best-effort), but during normal operation, kprintf should be routed
   to Plane B (debug overlay) when VDP is in graphics mode.

2. **VDP register state tracking** -- Currently the kernel doesn't track
   which VDP registers have been modified by user programs. `VDP_reinit()`
   does a full reset, which is safe but slow. A lighter-weight restore
   that only resets changed registers would be faster for rapid
   open/close cycles (e.g., running imshow in a loop for testing).

---

## 5. VDP Device Cleanup Bug

### The Problem

**Critical bug: `vdp_owner` leaks when a process is killed or crashes.**

`sys_close()` (proc.c:845) and `do_exit()` (proc.c:359) close file
descriptors by calling `fs_iput()` on the inode, but **never call
`devtab[major].close()`** for device files. Similarly, `sys_open()`
(proc.c:806) never calls `devtab[major].open()`.

The VDP driver's exclusive access check (`vdp_owner`) is set in
`vdp_open()` and cleared in `vdp_close()`. But neither function is
ever called by the kernel's fd management code.

**Current call path for open:**
```
sys_open("/dev/vdp") -> fs_namei() -> ofile_alloc() -> fd_alloc()
                       ^ never calls devtab[DEV_VDP].open()!
```

**Current call path for close:**
```
sys_close(fd) -> fs_iput(inode)
                ^ never calls devtab[DEV_VDP].close()!
```

**Current call path for ioctl (works correctly):**
```
sys_ioctl(fd, cmd, arg) -> checks FT_DEV -> devtab[major].ioctl()  OK
```

### Impact

1. `vdp_open()` is never called -> `vdp_owner` is never set -> the exclusive
   access check is **completely bypassed**. Multiple processes can open
   `/dev/vdp` simultaneously.

2. `vdp_close()` is never called -> `VDP_reinit()` is never called on close
   -> the console is NOT restored when a graphics program exits normally.

3. If a graphics app crashes, both the console state and any cleanup are lost.

**Note:** `gfx_open()` in libc calls `open("/dev/vdp", O_RDWR)` which
goes through `sys_open()`. The ioctl path works because `sys_ioctl()`
correctly dispatches to `devtab[major].ioctl()`. But open/close don't
dispatch to the device driver.

### Fix Required

Add device open/close dispatch to `sys_open()` and `sys_close()`:

**In sys_open(), after resolving the inode:**
```c
if (ip->type == FT_DEV) {
    int major = ip->dev_major;
    if (major < NDEV) {
        int err = devtab[major].open(ip->dev_minor);
        if (err < 0) {
            fs_iput(ip);
            return err;
        }
    }
}
```

**In sys_close(), when refcount reaches 0:**
```c
if (of->inode && of->inode->type == FT_DEV) {
    int major = of->inode->dev_major;
    if (major < NDEV)
        devtab[major].close(of->inode->dev_minor);
}
```

**In do_exit(), same pattern** (or refactor close logic into a helper).

### Priority

**HIGH** -- This is a correctness bug. The exclusive access and cleanup
features of the VDP driver are completely non-functional today.

---

## 6. Ctrl-Z / SIGTSTP Edge Case

### The Problem

When a graphics program (e.g., imshow) is running and the user presses
Ctrl-Z:

1. The TTY layer generates SIGTSTP -> delivered to the foreground process
2. Default SIGTSTP action: stop the process (move to STOPPED state)
3. The shell resumes as the foreground process
4. **But the VDP is still in graphics mode** -- Plane A has tile data,
   not text. The shell's output goes to `pal_console_putc()` which
   writes character tiles over the graphics nametable, creating
   garbled display.

### FUZIX Behavior

FUZIX has the **same problem** and doesn't address it. The FUZIX
imshow handles SIGINT and SIGTERM but not SIGTSTP. There's no
mechanism to save/restore VDP state on stop/continue.

### Options

**Option A: Restore console on stop, re-enter graphics on resume**
- On SIGTSTP delivery: kernel calls `vdp_close()` to restore text console
- On SIGCONT: process must re-open `/dev/vdp` and re-upload its state
- **Problem:** Process loses all VDP state. Must save/restore tiles,
  palette, nametable, scroll position. That's up to ~44 KB of data.

**Option B: Disallow Ctrl-Z for VDP programs**
- VDP programs run in raw mode with ISIG disabled (no signal generation
  from keyboard). Already the case if they call `cfmakeraw()`.
- If not in raw mode, silently ignore SIGTSTP while VDP is held.
- The `gfx_open()` API could do this automatically.

**Option C: Save/restore VDP state in kernel**
- On stop: DMA-read VRAM into a shadow buffer, call `VDP_reinit()`
- On continue: DMA-write shadow buffer back, restore VDP registers
- **Problem:** Shadow buffer needs ~8 KB minimum (nametables + palettes +
  sprites + scroll). Full VRAM shadow is 64 KB -- impossible in 64 KB RAM.
- CRAM is 128 bytes -- trivial to save
- VSRAM is 80 bytes -- trivial to save
- VDP registers are 24 bytes -- trivial to save
- Nametable is 8 KB -- feasible but tight

### Recommendation

**Option B is pragmatic for now.** Graphics programs should set raw mode
(disabling ISIG), which naturally prevents Ctrl-Z. The `gfx_open()` API
could do this automatically.

Long-term, Option A is the Unix-correct behavior, but requires the process
to cooperate by handling SIGTSTP/SIGCONT and saving/restoring its own
state. This is how real Unix graphics programs work (e.g., `vi` saves
terminal state on SIGTSTP).

For a better experience later: have graphics programs install a
SIGTSTP handler that calls `gfx_close()` and then re-raises SIGTSTP
with `SIG_DFL` to actually stop. On SIGCONT, re-initialize VDP and
re-upload data. This is application-level, not kernel-level.

### Implementation

Add to `gfx_open()`:
```c
/* Disable signal generation from keyboard while in graphics mode.
 * Prevents Ctrl-Z from stopping the process with VDP in graphics state. */
struct termios t;
tcgetattr(0, &t);
t.c_lflag &= ~ISIG;
tcsetattr(0, TCSANOW, &t);
```

And restore in `gfx_close()`:
```c
t.c_lflag |= ISIG;
tcsetattr(0, TCSANOW, &t);
```

---

## 7. Plane B Debug Terminal

### FUZIX Implementation

FUZIX's `dbg_output.S` provides a **complete debug terminal on Plane B**:

- **Separate scroll plane**: Uses Plane B nametable at VRAM 0xE000
  with its own VSRAM entry for independent vertical scrolling
- **20-row window**: `debug_window_size = 20`, wraps around the 32-row
  nametable with scroll
- **High-priority rendering**: Characters written with `0xA000` flag
  (priority bit set, palette 1) so they overlay Plane A
- **Toggle via F12**: `dbg_toggle()` enables/disables Plane B by
  writing register 0x04 (Plane B nametable address) -- setting it to
  0x00 hides the plane, 0x07 shows it at 0xE000
- **Printf-style output**: `dbg_printf()` supports `%c`, `%b`, `%w`,
  `%l`, `%s` format specifiers
- **Diagnostic tools**: `dbg_registers` (all D/A regs), `dbg_memory`
  (hex+ASCII dump), `dbg_stack`, `dbg_rte` (exception frame display)
- **Palette 1**: White background (0x0CCC), dark red foreground (0x022C)
  for contrast against the main console

### Genix Implementation

Genix's `pal/megadrive/dbg_output.S` is a **direct copy** of FUZIX's
debug overlay with identical functionality. The code is essentially
the same with minor naming differences:

| FUZIX | Genix | Notes |
|-------|-------|-------|
| `printChar` | `dbg_printChar` | Prefixed with `dbg_` |
| `printString` | `dbg_printString` | Same |
| `cursor_x/y` | `dbg_cursor_x/y` | Prefixed to avoid collision |
| `scrolling` | `dbg_scrolling` | Same |
| `outchar` | removed | Genix uses `pal_console_putc` |

Both implementations use independent Plane B scrolling and the same
F12 toggle mechanism.

### Gaps and Improvements

1. **No user-accessible debug API** -- The debug overlay is kernel-only.
   User programs can't write to Plane B. This is correct for a debug
   terminal but limits usefulness.

2. **No independent scroll** -- The debug overlay scrolls with its own
   counter (`dbg_y`, `dbg_x`) but doesn't expose scroll control. When
   output exceeds 20 rows, it wraps. No scroll-back.

3. **Potential: dual-terminal mode** -- Plane A for user programs, Plane B
   for kernel log/debug. Currently the debug overlay shows CPU state
   dumps; it could be extended to show `kprintf` output in real-time.

4. **Potential: status bar** -- Use the VDP window plane (currently disabled)
   for a fixed status line showing PID, memory usage, etc. The window plane
   renders on top of both Plane A and B and doesn't scroll.

5. **Debug overlay during graphics mode**: When a user program holds
   `/dev/vdp` and is drawing on Plane A, the F12 debug overlay still
   works because it uses Plane B. However, if the graphics program
   also uses Plane B (FUZIX's imshow does!), the debug output would
   be overwritten. Currently Genix's graphics driver only exposes
   Plane A mapping, so this isn't a problem yet.

6. **ISR-safe debug output**: The debug output routines write directly
   to VDP ports. If called from an ISR while a user program is
   mid-VDP-write, the VDP command state could be corrupted (the VDP
   control port is a two-write state machine). This is a latent bug
   in both FUZIX and Genix, though in practice the only ISR that
   might call debug output is a crash handler.

### Recommendations (Priority Order)

1. **Route kprintf to Plane B** when debug mode is active -- shows kernel
   messages without corrupting the user's Plane A output
2. **Add scroll-back** -- store last N lines in a ring buffer, allow
   Page Up/Page Down when debug overlay is visible
3. **Status bar via window plane** -- low priority but visually nice

---

## 8. Proposed Improvements

### 8.1 VDP Access Serialization (Priority: Medium)

**Problem:** The VDP control port is a two-write state machine. If an
interrupt fires between the two writes of a 32-bit command, the VDP
state is corrupted. Currently safe because:
- VBlank ISR doesn't touch VDP data port (only keyboard polling)
- Debug output from ISR context is rare (only crash handlers)

**But:** If we add DMA transfers, H-interrupt effects, or ISR-based
screen updates, this becomes a real problem.

**Solution:** Add a `vdp_lock`/`vdp_unlock` mechanism:
```c
/* In kernel -- disable interrupts around VDP port sequences */
static inline void vdp_lock(void) {
    __asm__ volatile("ori.w #0x0700, %sr");  /* mask all interrupts */
}
static inline void vdp_unlock(void) {
    __asm__ volatile("andi.w #0xF8FF, %sr"); /* restore interrupts */
}
```

This is the standard approach on Mega Drive -- all VDP command sequences
should be atomic. For userspace ioctls, the kernel already runs with a
consistent interrupt state, but we should verify no interrupt can fire
between the control port write and the first data port write.

**Cost:** ~8 cycles per lock/unlock pair. Negligible.

### 8.2 Console Output Suppression During Graphics Mode (Priority: High)

**Problem:** When a process holds `/dev/vdp`, any console output from
the kernel or other processes goes through `pal_console_putc()` and
writes to Plane A, corrupting the graphics display.

**Solution:** Add a flag checked by `pal_console_putc()`:
```c
extern uint8_t vdp_graphics_mode;  /* set by vdp_open/close */

void pal_console_putc(char c) {
    if (vdp_graphics_mode) {
        /* Redirect to debug plane or discard */
        return;
    }
    /* ... normal console output ... */
}
```

Options for handling suppressed output:
- **Discard**: Simplest, but loses kernel messages
- **Redirect to Plane B debug terminal**: Best -- uses existing
  `dbg_printChar()` infrastructure
- **Buffer**: Complex, needs memory allocation

**Recommendation:** Redirect to Plane B. When `/dev/vdp` is open,
have `pal_console_putc()` call `dbg_printChar()` instead of
`plot_char()`. Kernel panics and warnings become visible on the debug
overlay (toggled with F12) without corrupting the graphics display.

### 8.3 Plane B Access for Graphics Programs (Priority: Medium)

**Problem:** The `VDP_IOC_SETMAP` ioctl only writes to Plane A. FUZIX's
imshow uses both Plane A and Plane B for higher color depth.

**Solution:** Add a `plane` field to `struct vdp_map_arg`, or add a
new `VDP_IOC_SETMAP_B` ioctl. The plane field is cleaner:

```c
struct vdp_map_arg {
    uint16_t x, y, w, h;
    const uint16_t *tiles;
    uint16_t plane;  /* 0 = Plane A (default), 1 = Plane B */
    uint16_t pad;
};
```

The driver would select `VRAM_PLANE_A` or `VRAM_PLANE_B` based on
the plane field.

**Consideration:** If we add console output redirection to Plane B
(8.2), then giving graphics programs Plane B access creates a
conflict. Resolution: when a graphics program has `/dev/vdp` open,
it owns both planes. Console output goes to a kernel buffer or is
discarded. The debug overlay (F12) would need to be disabled or use
a different rendering approach.

### 8.4 Automatic VDP Cleanup on Process Death (Priority: High)

**Current status:** Already works! The kernel's `proc_exit()` calls
`fd_close_all()` which closes `/dev/vdp`, triggering `vdp_close()` ->
`VDP_reinit()`. Verified by code inspection.

**Edge case to verify:** What happens if a process is killed while
in the middle of a VDP ioctl? The ioctl runs in kernel context with
the process's fd table, so the kill signal would be delivered after
the ioctl returns to userspace. The VDP state would be consistent
(the ioctl either completed or didn't start), and `vdp_close()` would
restore the console properly.

### 8.5 DMA for Bulk Tile Upload (Priority: Low)

**Problem:** FUZIX's `imgInit` uploads 32 KB of tile data via an
unrolled CPU loop (~65,000 cycles). The Genix ioctl path has
additional overhead per call (syscall entry/exit, ioctl dispatch).

**DMA approach:** The VDP supports 68K->VRAM DMA transfers. Setup:
1. Write DMA length to registers 0x13-0x14
2. Write source address to registers 0x15-0x17
3. Write destination VRAM command to control port -> DMA starts
4. CPU is halted during DMA (~17 cycles per word transferred)

For 32 KB: 16K words x 17 cycles = ~272K cycles, vs CPU loop at
~520K cycles (32 cycles per longword x 8K longs). DMA is ~2x faster
and frees the CPU.

**However:** DMA setup requires writing VDP registers which must
happen from the kernel (the VDP registers are memory-mapped and not
protected). The current ioctl path would need a DMA mode flag.

**Recommendation:** Low priority. The current CPU-write approach is
fast enough for loading one image. DMA becomes important for
animations or rapid screen updates.

### 8.6 Full imshow Port (Priority: Medium)

Port FUZIX's image viewing capability:

1. **Port the `.simg` file format reader** -- straightforward, the
   format is just concatenated palettes + tilemaps
2. **Port `create_image.py`** -- host-side tool, no porting needed
   (already Python), just needs to be included in the repo
3. **Port `imshow_routines.S`** -- adapt to Genix's ioctl-based
   driver rather than direct VDP writes. Alternatively, implement
   in C using the `gfx_*` API since the performance difference is
   minimal for a one-shot image load.
4. **Add Plane B ioctl support** (see 8.3)
5. **Add sprite setup for multi-layer colors**

The current Genix imshow could be evolved into this:
- Keep the test pattern mode as a fallback (when no image file given)
- Add `.simg` file loading when a filename argument is provided
- Use all 4 palettes and both planes for full 60-color display

### 8.7 VDP Register State Tracking (Priority: Low)

**Problem:** The kernel doesn't track the current VDP register values.
If a graphics program changes VDP registers (e.g., scroll mode, plane
size) via direct hardware access (bypassing the driver), `VDP_reinit()`
restores defaults anyway, so this isn't currently broken.

**But:** If we expose VDP register writes through the driver, we should
track state so that `vdp_close()` can restore exactly the console
configuration. Currently `VDP_reinit()` always loads the full default
register table, which is correct but could be smarter.

**Recommendation:** Keep the current approach. Full register reload
on close is simple and correct. Only change if we add register-level
ioctls.

---

## 9. Implementation Priority

### Must Fix (Correctness Bugs)

| # | Issue | Effort | Impact |
|---|-------|--------|--------|
| 1 | Device open/close dispatch in sys_open/sys_close/do_exit (Section 5) | Small | VDP exclusive access and cleanup completely broken |

### Should Do (High Value)

| # | Improvement | Effort | Impact |
|---|------------|--------|--------|
| 2 | ANSI escape sequence parser in pal_console_putc | Medium | Enables curses, vi, less, all screen-oriented apps |
| 3 | Console output suppression during graphics (8.2) | Small | Prevents garbled display |
| 4 | Ctrl-Z safety: disable ISIG in gfx_open/gfx_close | Trivial | Prevents frozen-display UX bug |
| 5 | Route kprintf to Plane B when VDP is in graphics mode | Small | Clean separation of kernel output from user graphics |
| 6 | Plane B access for graphics programs (8.3) | Small | Enables full imshow |

### Nice to Have (Lower Priority)

| # | Improvement | Effort | Impact |
|---|------------|--------|--------|
| 7 | VDP access serialization (8.1) | Tiny | Prevents latent bugs |
| 8 | Port FUZIX termcap + curses library | Medium-Large | Full curses app support |
| 9 | DMA fill for fast VRAM clear | Small | Marginal speed gain for clear operations |
| 10 | DMA tile upload for large images (8.5) | Medium | Enables smooth image loading |
| 11 | Expose per-line scroll via new ioctl | Small | Parallax effects, advanced graphics |
| 12 | PCX image loader for real imshow | Medium | First real graphics application |
| 13 | Full imshow port (8.6) | Medium | Real image display |
| 14 | Window plane status bar | Medium | Developer convenience |
| 15 | Debug overlay scroll-back | Medium | Better kernel debugging |

### Quick Wins (can be done in a single session)

1. **Console suppression flag** -- add `vdp_graphics_mode` flag, check
   in `pal_console_putc()`, redirect to `dbg_printChar()`. ~20 lines
   of code.

2. **Plane B ioctl** -- add `plane` field to `vdp_map_arg`, select
   nametable base in `vdp_do_setmap()`. ~10 lines of code change.

3. **VDP lock** -- wrap VDP control+data port sequences in interrupt
   disable/enable. ~5 lines per call site, or a macro.

4. **Ctrl-Z safety** -- disable ISIG in `gfx_open()`, restore in
   `gfx_close()`. ~10 lines of code.

### Medium-Term (Phase 9 or dedicated session)

5. **imshow with .simg support** -- port the image loading, add
   `create_image.py` to `tools/`, update imshow to use file arguments.

### Long-Term (Post Phase 9)

6. **Minimal curses** -- only if a TUI application demands it.
7. **DMA engine** -- only if animation/game support is a goal.

### Already Documented in plans/optimization-plan.md

These are not VDP-specific but affect overall system performance:

1. Division fast path (DIVU.W) -- Priority 1
2. SRAM 16-bit I/O -- Priority 2
3. Assembly memcpy/memset -- Priority 3
4. Pipe bulk copy -- Priority 4
5. SRAM init zeroing -- Priority 5

---

## Appendix: File Inventory

### FUZIX VDP Files (EythorE/FUZIX megadrive branch)

| File | Purpose |
|------|---------|
| `Kernel/platform/platform-megadrive/vdp.S` | VDP init, registers, palettes, font |
| `Kernel/platform/platform-megadrive/devvt.S` | Terminal: plot_char, scroll, cursor |
| `Kernel/platform/platform-megadrive/dbg_output.S` | Plane B debug overlay |
| `Kernel/platform/platform-megadrive/devvdp.c` | VDP device driver (minimal) |
| `Kernel/platform/platform-megadrive/devvdp.h` | VDP ioctl definitions |
| `Kernel/platform/platform-megadrive/devtty.c` | TTY driver (routes to vtoutput) |
| `Kernel/platform/platform-megadrive/megadrive.S` | VBlank ISR, interrupt handling |
| `Kernel/platform/platform-megadrive/Applications/imshow.c` | Image viewer (file-based) |
| `Kernel/platform/platform-megadrive/Applications/imshow_routines.S` | Image display assembly |
| `Kernel/platform/platform-megadrive/Applications/image_convert/create_image.py` | Image conversion tool |
| `Library/libs/curses/` | Curses library source |
| `Library/libs/Makefile.68000` | Builds curses68000.lib |

### Genix VDP Files

| File | Purpose |
|------|---------|
| `pal/megadrive/vdp.S` | VDP init (adapted from FUZIX) |
| `pal/megadrive/devvt.S` | Terminal output (adapted from FUZIX) |
| `pal/megadrive/dbg_output.S` | Plane B debug overlay (adapted from FUZIX) |
| `pal/megadrive/dev_vdp.c` | VDP device driver (much more complete than FUZIX) |
| `pal/megadrive/platform.c` | PAL adapter with console putc |
| `kernel/dev_vdp.h` | VDP ioctl definitions and structures |
| `libc/gfx.c` | Userspace graphics library |
| `libc/include/gfx.h` | Graphics API header |
| `apps/imshow.c` | Test pattern display |
| `tests/test_vdp.c` | VDP unit tests |
| `docs/graphics.md` | VDP documentation |
