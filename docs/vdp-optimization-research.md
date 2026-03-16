# VDP Terminal Optimization Research

## Background

Genix was created by stripping FUZIX down to its essentials for the Mega
Drive. This document catalogs VDP-related improvements, comparing against
FUZIX's approach and identifying gaps in Genix's current implementation.

**Key discovery:** FUZIX upstream does NOT have a Mega Drive platform.
The Mega Drive port directory (`/home/eythor/FUZIX/Kernel/platform/platform-megadrive/`)
referenced in OPTIMIZATION_PLAN.md was a local fork, not upstream code. The
VDP terminal, graphics driver, and debug overlay are original Genix work.

---

## 1. VDP Terminal: Genix vs FUZIX Approach

### What's Already Optimal

OPTIMIZATION_PLAN.md (lines 436-451) confirms these are already at parity
with FUZIX or better:

| Component | Status |
|-----------|--------|
| VDP text output (`devvt.S`) | Assembly, identical to FUZIX |
| VDP initialization (`vdp.S`) | Assembly, identical to FUZIX |
| Font loading (`vdp.S:120-145`) | Assembly bit rotation, identical |
| Saturn keyboard (`keyboard_read.S`) | Assembly, nearly identical |
| Hardware VSRAM scroll | ~20 cycles vs ~2240 bytes memory copy |

The key optimization — **hardware vertical scroll via VSRAM** instead of
copying nametable data — is already implemented. `scroll_up()` in `devvt.S`
adds 8 to a scroll counter and writes one word to VSRAM. This is ~100x
faster than copying 40×28 tiles.

### Character Output Path

Current path (same as FUZIX):
```
printf → kputc → pal_console_putc → plot_char (devvt.S)
```

`plot_char` writes one VDP control word + one data word per character.
No batching. FUZIX doesn't batch either — character-at-a-time is adequate
for a 40×28 text console at 7.67 MHz.

### VDP Access Serialization

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
(40×28 = 1120 words × ~4 cycles). DMA setup overhead is ~50 cycles but
runs during active display. Only matters for rapid full-screen redraws.

**Recommendation:** Not worth the complexity. Current approach is fine.

---

## 2. Curses Library Assessment

### FUZIX Curses Implementation

FUZIX includes a minimal curses library (`Library/libs/curses/`, 37 files)
that is **termcap-driven**:

- `initscr()` reads `$TERM`, calls `setterm()` to load capabilities
- `setterm()` uses `tgetstr()` to load escape sequences: `cl` (clear),
  `cm` (cursor motion), `so`/`se` (standout), `vi`/`ve` (cursor visibility)
- `poscur(r, c)` calls `tputs(tgoto(cm, c, r), 1, outc)` — generates
  terminal-specific cursor positioning escape sequences
- `doupdate()` uses per-line dirty tracking (`_minchng[]` / `_maxchng[]`)
  to only send changes to the terminal
- Character-level diffing within dirty lines avoids redundant writes

### Requirements to Port Curses to Genix

FUZIX curses needs these capabilities from the terminal:

1. **Cursor positioning** (`cm` capability) — move cursor to arbitrary (row, col)
2. **Clear screen** (`cl` capability)
3. **termcap/terminfo database** — `tgetent()`, `tgetstr()`, `tgoto()`, `tputs()`
4. **TERM environment variable** — terminal type identification
5. **`tcsetattr()` / `tcgetattr()`** — raw mode, echo control (already have this)

### What Genix Lacks

The VDP console has **no ANSI escape sequence processing**. Characters go
directly to `plot_char()`. There's no `\033[H` (cursor home), no
`\033[2J` (clear screen), no `\033[row;colH` (cursor position).

**Gap analysis:**

| Requirement | Genix Status | Effort |
|------------|-------------|--------|
| Cursor positioning | NOT IMPLEMENTED — no escape sequence parser | Medium |
| Clear screen | NOT IMPLEMENTED — no `\033[2J` handling | Small |
| termcap library | NOT IMPLEMENTED — no tgetent/tgetstr | Medium-Large |
| TERM variable | Easy — just set `$TERM` in shell startup | Trivial |
| Raw mode / echo | IMPLEMENTED — termios in tty.c | Done |

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
normally goes through termcap → escape sequence → terminal parser →
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

**Option C: No curses — use lineedit + raw VDP for specific apps**

Keep the current approach: `lineedit.c` for shell line editing, direct
`gfx_*` API for graphics apps. Don't port curses at all.

**Limitation:** Can't run `vi`, `less`, `top`, or any curses-based
application. The app porting roadmap (`docs/apps_to_port.md`) likely
includes some of these.

### Recommendation

**Option A is the right long-term path.** The ANSI escape parser is
small (~200-300 lines), the termcap library is small, and FUZIX's curses
is already designed for constrained systems. The escape parser also
enables programs that use raw ANSI sequences directly (common in Unix
utilities).

**Phasing:**
1. First: ANSI escape parser in `pal_console_putc()` (enables `vi`-style
   apps that write raw ANSI)
2. Second: Port termcap library from FUZIX
3. Third: Port curses library from FUZIX
4. Validate with: `vi` (levee is already ported — does it need curses?)

---

## 3. Graphics Stack Gaps (imshow and Beyond)

### Current imshow Limitations

`apps/imshow.c` is a color bar test pattern, not an image viewer. It:
- Generates solid-color tiles procedurally
- Maps them to screen in hardcoded patterns
- Has no file I/O — can't load images

### What's Needed for Real Image Display

To display actual images on the VDP:

1. **Image file format support** — Need a decoder (PCX is simplest for
   4bpp paletted images: ~200 lines of C). PNG is too complex for 64 KB.
   Raw headerless formats are even simpler.

2. **Palette quantization** — The VDP has 4 palettes × 16 colors.
   An image must be quantized to ≤61 colors (palette 0 is console).
   Median-cut or popularity algorithm, but RAM-constrained.

3. **Tile deduplication** — The VDP displays 8×8 tiles. A 320×224 image
   is 40×28 = 1120 tiles. With 44 KB of user tile space, we have room
   for 1408 tiles (44K / 32 bytes). An image with all-unique tiles fits.
   But deduplication would allow larger virtual images with scrolling.

4. **DMA tile upload** — Current `vdp_do_loadtiles()` uses CPU writes
   (loop writing longwords). For 1120 tiles × 32 bytes = 35,840 bytes,
   this takes ~72,000 cycles. DMA could do it in background during VBlank
   (~17 KB/frame at 60 fps). Not strictly needed but enables smoother
   loading.

5. **Scrolling for oversized images** — The VDP supports hardware scroll.
   Images larger than 320×224 could be scrolled with arrow keys using the
   existing `gfx_scroll()` API.

### Missing VDP Hardware Features

VDP Register 0x0B (mode set 3) is currently 0x00. Unexposed capabilities:

| Feature | Register Bits | Use Case |
|---------|--------------|----------|
| Per-line V-scroll | Reg 0x0B bit 2 | Parallax, wavy effects |
| Per-tile H-scroll | Reg 0x0B bits 0-1 | Column scroll, water effects |
| Window plane | Reg 0x11, 0x12 | Fixed HUD/status bar overlay |
| Interlace | Reg 0x0C bits 1-2 | 320×448 display (double height) |
| Shadow/highlight | Reg 0x0C bit 3 | Transparency effects |
| DMA fill | Reg 0x17 bit 7 | Fast VRAM clear |
| DMA copy | Reg 0x17 bit 6 | VRAM-to-VRAM block transfer |

**Recommendation:** Expose DMA fill (useful for fast clear) and
per-line/per-tile scroll modes via new ioctls. The others are niche
and can wait.

---

## 4. VDP Device Cleanup Bug

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
sys_open("/dev/vdp") → fs_namei() → ofile_alloc() → fd_alloc()
                       ↑ never calls devtab[DEV_VDP].open()!
```

**Current call path for close:**
```
sys_close(fd) → fs_iput(inode)
                ↑ never calls devtab[DEV_VDP].close()!
```

**Current call path for ioctl (works correctly):**
```
sys_ioctl(fd, cmd, arg) → checks FT_DEV → devtab[major].ioctl()  ✓
```

### Impact

1. `vdp_open()` is never called → `vdp_owner` is never set → the exclusive
   access check is **completely bypassed**. Multiple processes can open
   `/dev/vdp` simultaneously.

2. `vdp_close()` is never called → `VDP_reinit()` is never called on close
   → the console is NOT restored when a graphics program exits normally.

3. If a graphics app crashes, both the console state and any cleanup are lost.

**Wait** — let me verify: does `gfx_open()` in libc work around this?

From `libc/gfx.c`, `gfx_open()` calls `open("/dev/vdp", O_RDWR)` which
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

**HIGH** — This is a correctness bug. The exclusive access and cleanup
features of the VDP driver are completely non-functional today.

---

## 5. Ctrl-Z (SIGTSTP) Edge Case

### Current Behavior

When a graphics program holds `/dev/vdp` and the user presses Ctrl-Z:

1. TTY layer generates SIGTSTP → delivered to foreground process
2. Default SIGTSTP action: stop the process (P_STOPPED state)
3. Shell resumes as foreground process
4. **VDP is still in graphics mode** — shell output goes to the text
   console which is hidden behind the graphics plane

The user sees a frozen graphics display with no shell prompt.

### What Should Happen

Two reasonable behaviors:

**Option A: Restore console on stop, re-enter graphics on resume**
- On SIGTSTP delivery: kernel calls `vdp_close()` to restore text console
- On SIGCONT: process must re-open `/dev/vdp` and re-upload its state
- **Problem:** Process loses all VDP state. Must save/restore tiles,
  palette, nametable, scroll position. That's up to ~44 KB of data.

**Option B: Disallow Ctrl-Z for VDP programs**
- VDP programs run in raw mode with ISIG disabled (no signal generation
  from keyboard). Already the case if they call `cfmakeraw()`.
- If not in raw mode, silently ignore SIGTSTP while VDP is held.

**Option C: Save/restore VDP state in kernel**
- On stop: DMA-read VRAM into a shadow buffer, call `VDP_reinit()`
- On continue: DMA-write shadow buffer back, restore VDP registers
- **Problem:** Shadow buffer needs ~8 KB minimum (nametables + palettes +
  sprites + scroll). Full VRAM shadow is 64 KB — impossible in 64 KB RAM.

### Recommendation

**Option B is pragmatic for now.** Graphics programs should set raw mode
(disabling ISIG), which naturally prevents Ctrl-Z. The `gfx_open()` API
could do this automatically.

Long-term, Option A is the Unix-correct behavior, but requires the process
to cooperate by handling SIGTSTP/SIGCONT and saving/restoring its own
state. This is how real Unix graphics programs work (e.g., `vi` saves
terminal state on SIGTSTP).

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

## 6. Plane B Debug Terminal

### Current Implementation

`pal/megadrive/dbg_output.S` implements a kernel debug overlay on Plane B:

- **Toggle:** F12 key (via `dbg_toggle()`)
- **VRAM:** Plane B nametable at 0xE000
- **Palette:** Palette 1 (white background, dark text)
- **Priority:** High-priority tiles (bit 15 set) render above Plane A
- **Features:** `dbg_printf()` with `%c`, `%b`, `%w`, `%l`, `%s` formats
- **Extras:** `dbg_registers()`, `dbg_stack()`, `dbg_memory()` for CPU state dumps

### Gaps and Improvements

1. **No user-accessible debug API** — The debug overlay is kernel-only.
   User programs can't write to Plane B. This is correct for a debug
   terminal but limits usefulness.

2. **No independent scroll** — The debug overlay scrolls with its own
   counter (`dbg_y`, `dbg_x`) but doesn't expose scroll control. When
   output exceeds 20 rows, it wraps. No scroll-back.

3. **Potential: dual-terminal mode** — Plane A for user programs, Plane B
   for kernel log/debug. Currently the debug overlay shows CPU state
   dumps; it could be extended to show `kprintf` output in real-time.

4. **Potential: status bar** — Use the VDP window plane (currently disabled)
   for a fixed status line showing PID, memory usage, etc. The window plane
   renders on top of both Plane A and B and doesn't scroll.

### Recommendation

The debug overlay is adequate for its current purpose. Potential
improvements (priority order):

1. **Route kprintf to Plane B** when debug mode is active — shows kernel
   messages without corrupting the user's Plane A output
2. **Add scroll-back** — store last N lines in a ring buffer, allow
   Page Up/Page Down when debug overlay is visible
3. **Status bar via window plane** — low priority but visually nice

---

## 7. VDP Driver: Single Access Enforcement

### Current State

The VDP driver uses `vdp_owner` for exclusive access, but as documented
in Section 4, **this is currently non-functional** because `sys_open()`
never calls `vdp_open()`.

### Correct Design (After Fix)

Once `sys_open()` dispatches to `devtab[major].open()`:

1. First `open("/dev/vdp")` succeeds, sets `vdp_owner = pid`
2. Second `open("/dev/vdp")` from any process returns `EBUSY`
3. `close(fd)` calls `vdp_close()`, clears `vdp_owner`, calls `VDP_reinit()`
4. Process exit closes all fds, which triggers close handler

### Additional Hardening

1. **ISR-safe VDP access** — Console output (kprintf) goes through
   `devvt.S` which writes VDP directly. If a user program has VDP open
   and the kernel panics, kprintf would write to Plane A while the user
   program's nametable is active. This is actually fine for panic output
   (best-effort), but during normal operation, kprintf should be routed
   to Plane B (debug overlay) when VDP is in graphics mode.

2. **VDP register state tracking** — Currently the kernel doesn't track
   which VDP registers have been modified by user programs. `VDP_reinit()`
   does a full reset, which is safe but slow. A lighter-weight restore
   that only resets changed registers would be faster for rapid
   open/close cycles (e.g., running imshow in a loop for testing).

---

## 8. Summary of Recommended Improvements

### Must Fix (Correctness Bugs)

| # | Issue | Effort | Impact |
|---|-------|--------|--------|
| 1 | Device open/close dispatch in sys_open/sys_close/do_exit | Small | VDP exclusive access and cleanup completely broken |

### Should Do (High Value)

| # | Improvement | Effort | Impact |
|---|------------|--------|--------|
| 2 | ANSI escape sequence parser in pal_console_putc | Medium | Enables curses, vi, less, all screen-oriented apps |
| 3 | Ctrl-Z safety: disable ISIG in gfx_open/gfx_close | Trivial | Prevents frozen-display UX bug |
| 4 | Route kprintf to Plane B when VDP is in graphics mode | Small | Clean separation of kernel output from user graphics |

### Nice to Have (Lower Priority)

| # | Improvement | Effort | Impact |
|---|------------|--------|--------|
| 5 | Port FUZIX termcap + curses library | Medium | Full curses app support |
| 6 | DMA fill for fast VRAM clear | Small | Marginal speed gain for clear operations |
| 7 | DMA tile upload for large images | Medium | Enables smooth image loading |
| 8 | Expose per-line scroll via new ioctl | Small | Parallax effects, advanced graphics |
| 9 | PCX image loader for real imshow | Medium | First real graphics application |
| 10 | Window plane status bar | Medium | Developer convenience |
| 11 | Debug overlay scroll-back | Medium | Better kernel debugging |

### Already Documented in OPTIMIZATION_PLAN.md

These are not VDP-specific but affect overall system performance:

1. Division fast path (DIVU.W) — Priority 1
2. SRAM 16-bit I/O — Priority 2
3. Assembly memcpy/memset — Priority 3
4. Pipe bulk copy — Priority 4
5. SRAM init zeroing — Priority 5
