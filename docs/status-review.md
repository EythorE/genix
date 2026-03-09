# Genix Status Review — Plan vs. Reality

_Review date: 2026-03-09_

This document compares the original PLAN.md (from the FUZIX repo) against
what Genix actually implemented, identifies divergences, assesses their
impact on porting Fuzix apps, and lays out the path forward.

---

## 1. Plan vs. Reality — Phase by Phase

### Phase 1: Workbench Emulator — COMPLETE, matches plan

The plan called for a Musashi-based 68000 SBC with UART, timer, and disk.
This is exactly what was built. No meaningful divergence.

### Phase 2a: Single-tasking Kernel — COMPLETE, matches plan

Boot, console I/O, minifs filesystem, exec(), built-in shell — all done.
The kernel is ~3000 lines as planned. minifs matches the planned on-disk
format closely (48-byte inodes instead of 64-byte, but the same concept).

### Phase 2b: Multitasking — COMPLETE, diverged from plan

**What the plan said:**
- Process table (8–16 slots), round-robin preemptive scheduling via timer
- vfork()+exec() as the process creation model
- PIC ELF or bFLT binary format for position-independent loading

**What was actually built:**
- Process table (16 slots) — matches
- `spawn()` (combined vfork+exec) instead of separate vfork()/exec() — **divergence**
- Cooperative scheduling, not preemptive — **divergence**
- Genix flat binary (fixed load address) instead of PIC/bFLT — **divergence**

**Why it diverged:** The return-twice semantics of vfork() crashed on the
68000 stack. `spawn()` was pragmatic — it works for the built-in shell. But
it's not POSIX, so ported programs that do `vfork(); exec()` or
`fork(); exec()` won't work without modification.

**Impact:** Medium. Most Fuzix utilities don't fork — they're simple
filter programs. But `sh`, `make`, and anything that spawns subprocesses
will need the real vfork() or a posix_spawn() wrapper.

### Phase 2c: Pipes — COMPLETE, partially diverged

**What the plan said:** pipe(), with blocking I/O.

**What was built:** pipe() with non-blocking semantics (returns immediately
if empty/full). This works for single-command pipelines in the built-in
shell but won't work for real shell pipelines like `cat file | grep foo`
where the reader must block waiting for the writer.

**Impact:** High for ported apps. Blocking pipes require preemptive
scheduling (Phase 2b divergence compounds here).

### Phase 2d: Signals — NOT STARTED

**What the plan said:** kill(), signal()/sigaction() for basic signal handling.

**What exists:** Stubs only. `SYS_SIGNAL` stores a handler pointer but
never delivers. `SYS_KILL` returns -ENOSYS.

**Impact:** Medium. Most simple utilities don't use signals. But any
program that catches SIGINT (Ctrl+C), or any shell that does job control,
needs this.

### Phase 2e: TTY Subsystem — NOT STARTED

Console I/O works (cooked and raw mode via termios ioctl), but there's no
proper TTY layer. No line discipline, no job control signals, no /dev/ttyN.

**Impact:** Low for simple utilities. Levee already works with what exists.
Needed for proper shell job control and multi-terminal support.

### Phase 2f: Fuzix libc + Utilities — NOT STARTED

This is the app-porting phase. The plan called for using newlib. Genix
instead built a custom minimal libc. This is actually fine — see Section 3.

### Phase 3: Mega Drive Port — COMPLETE, matches plan

VDP text output, Saturn keyboard, SRAM, ROM disk — all working. The PAL
abstraction works exactly as planned. BlastEm tests pass.

### Phase 4: Polish — NOT STARTED

Interrupt-driven keyboard, /dev/null, multi-TTY — all planned, none done.

---

## 2. Key Divergences Summary

| Area | Plan | Reality | Severity |
|------|------|---------|----------|
| Process creation | vfork()+exec() | spawn() only | **High** |
| Scheduling | Preemptive (timer) | Cooperative | **High** |
| Binary format | PIC ELF / bFLT | Fixed-address flat | Medium |
| Pipe blocking | Blocking I/O | Non-blocking | **High** |
| C library | newlib | Custom minimal libc | Low (better) |
| Signals | Basic delivery | Stubs only | Medium |
| TTY | Full subsystem | Minimal cooked/raw | Low |

The three **High** items (vfork, preemptive scheduling, blocking pipes)
are all interconnected. You can't have blocking pipes without preemptive
scheduling, and you can't port programs that spawn children without
vfork()+exec().

---

## 3. Impact on Porting Fuzix Apps

### What CAN be ported today (no kernel changes needed)

Simple filter programs that read stdin, process, write stdout:

- **Already done:** hello, echo, cat, wc, head, true, false, levee
- **Easy ports (~1 hour each):** tail, tee, yes, sleep, basename, dirname,
  od, rev, strings, nl, fold, paste, expand, unexpand, comm, cmp

These programs need only: read/write/open/close/lseek/stat/exit and
basic libc (stdio, string, stdlib). Genix has all of this.

### What CANNOT be ported until kernel work is done

| Program | Missing Feature | Blocker |
|---------|----------------|---------|
| sh (any real shell) | vfork+exec, signals, blocking pipes | Phase 2d work |
| grep | Works, but needs getopt() in libc | Libc gap |
| sort | Large memory, vfork for -o flag | May work without -o |
| find | opendir/readdir (SYS_GETDENTS) | Syscall gap |
| ls -l | stat() works, but needs getdents for dirs | Syscall gap |
| make | vfork+exec, pipes, signals | Phase 2d work |
| sed | regex library | Libc gap |
| ed | signals, terminal control | Phase 2d-2e |
| ps | /proc or kernel query syscall | Needs new syscall |
| kill | SYS_KILL not implemented | Phase 2d work |
| tar | Large memory, may need SRAM | Memory constraint |

### Libc gaps for porting

Genix libc has: stdio (FILE*, fopen, fgets, fprintf, puts), stdlib
(malloc, atoi, exit, qsort), string, ctype, termios, unistd.

**Missing for Fuzix apps:**
- `getopt()` / `getopt_long()` — needed by almost every utility
- `opendir()` / `readdir()` / `closedir()` — needs SYS_GETDENTS
- `sprintf()` / `snprintf()` — currently only fprintf to a FILE*
- `sscanf()` / `scanf()` — not implemented
- `strtol()` / `strtoul()` — needed by many utilities
- `perror()` / `strerror()` — error reporting
- `glob()` — shell globbing
- `regex` — needed by grep, sed
- `time()` / `localtime()` / `strftime()` — timestamps
- `getenv()` / `setenv()` — environment variables (getenv exists but returns NULL)
- `isatty()` — terminal detection

---

## 4. C Library Strategy: Custom libc vs. Newlib vs. Fuzix libc

The plan called for newlib. Genix chose a custom libc. Here's the assessment:

**Newlib** (original plan):
- Pro: Complete stdio, stdlib, string, math — everything "just works"
- Con: Large (~200 KB .text), designed for 32-bit systems with more RAM
- Con: Needs ~15 syscall stubs that must exactly match newlib's expectations
- Verdict: Too big for Mega Drive's 64 KB. Would work on workbench only.

**Fuzix libc** (plan phase 2f):
- Pro: Designed for tiny systems (Z80, 6502, 68000), small footprint
- Pro: Already has 68000 support, already tested with Fuzix apps
- Pro: All Fuzix apps are written against it — zero porting friction
- Con: Tightly coupled to Fuzix syscall numbers and conventions
- Verdict: Best option for porting Fuzix apps, needs a syscall shim.

**Custom Genix libc** (current):
- Pro: Tiny, we control everything, proven to work
- Pro: Already supports levee (non-trivial real app)
- Con: Missing many functions needed by ported apps
- Verdict: Keep as foundation, incrementally add what's needed.

### Recommendation: Grow the Genix libc, borrowing from Fuzix libc

Don't wholesale replace — incrementally pull functions from Fuzix libc
into Genix libc as apps need them. The syscall stubs stay Genix-native.
The library functions (getopt, strtol, regex, etc.) are pure C and can
be copied directly.

This gives us:
1. No ABI/syscall compatibility headaches
2. Incremental, testable growth
3. Each new function is immediately useful for a concrete app port

---

## 5. Plan to Finish Remaining Phases

### Phase 2d-LITE: Minimum viable vfork + signals (prerequisite for apps)

This is the critical blocker. Without vfork()+exec(), we can't port any
program that spawns children (shells, make, find -exec, etc.).

**Step 1: Implement real vfork()**
- Save parent's full register set (d0-d7, a0-a7, sr, pc) in proc table
- Mark parent P_VFORK (sleeping), switch to child
- Child shares parent memory (stack + heap)
- On child's exec() or _exit(), restore parent from saved state
- The vfork stub in libc already handles the return-address trick

**Step 2: Make exec() work from vfork'd child**
- exec() must detect it's in a vfork'd child
- Load new binary, set up new stack, wake parent, jump to new program
- Parent resumes where vfork() returned, with child's PID in d0

**Step 3: Preemptive scheduling (timer-driven)**
- Workbench: use the existing timer interrupt (SIGALRM-based)
- Mega Drive: use VBlank interrupt (50/60 Hz)
- Timer ISR calls scheduler() if current process has run long enough
- This automatically fixes blocking pipes — sleeping processes get woken

**Step 4: Blocking pipe I/O**
- pipe_read(): if buffer empty and writers exist, sleep (P_SLEEPING)
- pipe_write(): if buffer full and readers exist, sleep
- Wake sleeping readers/writers when data arrives or space frees

**Step 5: Basic signals**
- Process flag field for pending signals
- Default handlers: SIGTERM→exit, SIGKILL→exit, SIGINT→exit
- signal() stores user handler; delivery on return from kernel
- kill() sets the flag; scheduler delivers on context switch

### Phase 2e-LITE: Minimal TTY improvements

- Ctrl+C generates SIGINT to foreground process (needs signals first)
- isatty() syscall (or just hardcode fd 0/1/2 as terminal)
- /dev/null (trivially: read returns 0, write discards)

### Phase 2f: App porting (the main event)

See Section 6 below.

---

## 6. App Porting Strategy and Build Organization

### Directory structure

```
genix/
├── apps/                    # Genix-native programs (current)
│   ├── hello.c
│   ├── echo.c
│   ├── cat.c
│   ├── ...
│   ├── crt0.S
│   ├── user.ld
│   ├── user-md.ld
│   └── Makefile
├── apps/ports/              # Ported Fuzix utilities
│   ├── grep.c              # Copied + adapted from Fuzix
│   ├── ls.c
│   ├── sort.c
│   ├── ...
│   └── Makefile            # Same build pattern as apps/
└── libc/                    # Grows as apps need functions
    ├── getopt.c            # Pulled from Fuzix libc
    ├── strtol.c
    ├── ...
```

**Alternative (simpler):** Just put everything in `apps/`. The Fuzix
utilities are simple standalone .c files. There's no real need for a
subdirectory — it's one .c file per program. Add them to the PROGRAMS
list in apps/Makefile and they build automatically.

### Recommendation: Flat apps/ directory

```
apps/Makefile:
PROGRAMS = hello echo cat wc head true false \
           tail tee yes sleep grep sort ls find sed ...
```

Each program is one .c file. The existing build system (crt0 + libc +
mkbin) handles everything. No new Makefiles, no new build steps.

For multi-file programs (like levee), keep a subdirectory with its own
Makefile, as is already done for levee.

### Porting tiers

**Tier 0 — No kernel changes needed, minimal libc additions (do first):**

Add `getopt.c`, `strtol.c`, `perror.c` to libc, then port:

| Program | Source | Notes |
|---------|--------|-------|
| tail | Fuzix util/tail.c | read + lseek |
| tee | Fuzix util/tee.c | read + write to multiple fds |
| yes | Fuzix util/yes.c | trivial |
| sleep | Fuzix util/sleep.c | needs time() or busy-wait |
| basename | Fuzix util/basename.c | pure string manipulation |
| dirname | Fuzix util/dirname.c | pure string manipulation |
| od | Fuzix util/od.c | read + formatted output |
| strings | Fuzix util/strings.c | read + filter |
| rev | Fuzix util/rev.c | line reversal |
| nl | Fuzix util/nl.c | line numbering |
| cmp | Fuzix util/cmp.c | byte-by-byte file compare |
| cut | Fuzix util/cut.c | field extraction |
| tr | Fuzix util/tr.c | character translation |
| uniq | Fuzix util/uniq.c | duplicate line filter |
| paste | Fuzix util/paste.c | merge file lines |
| env | Fuzix util/env.c | print/set environment |

**Tier 1 — Needs SYS_GETDENTS + opendir/readdir in libc:**

| Program | Source | Notes |
|---------|--------|-------|
| ls | Fuzix util/ls.c | Directory listing, stat() |
| find | Fuzix util/find.c | Directory traversal |
| rmdir | Already a kernel builtin | Move to user program |
| du | Fuzix util/du.c | Directory size |
| mkdir | Already a kernel builtin | Move to user program |

**Tier 2 — Needs vfork+exec (Phase 2d-LITE):**

| Program | Source | Notes |
|---------|--------|-------|
| sh | Write minimal or port Fuzix sh | The big one |
| xargs | Fuzix util/xargs.c | Needs exec |
| time | Fuzix util/time.c | Needs vfork+exec+waitpid |
| nohup | Fuzix util/nohup.c | Needs signals |

**Tier 3 — Needs regex library:**

| Program | Source | Notes |
|---------|--------|-------|
| grep | Fuzix util/grep.c | Core utility |
| sed | Fuzix MWC/cmd/sed.c | Stream editor |
| expr | Fuzix MWC/cmd/expr.y | Needs yacc output |

**Tier 4 — Nice to have, bigger programs:**

| Program | Source | Notes |
|---------|--------|-------|
| ed | Fuzix Applications/ue or MWC/cmd/ed.c | Line editor |
| make | Fuzix MWC/cmd/make.c | Build system |
| ar | Fuzix util/ar.c | Archive tool |
| diff | Fuzix util/diff.c | File comparison |
| patch | Large, needs careful porting | |

### Build process changes needed

**Minimal changes to apps/Makefile:**

1. Add ported .c files to PROGRAMS list
2. Some programs need `-I ../libc/include` (already the case)
3. Some multi-file programs (like levee) get their own subdirectory
4. No other build system changes — the existing pattern rules handle it

**Libc additions** (in order of priority):

1. `getopt.c` — almost every utility needs this
2. `strtol.c` / `strtoul.c` / `atol.c` — numeric conversion
3. `perror.c` / `strerror.c` — error messages
4. `opendir.c` / `readdir.c` / `closedir.c` — directory reading (needs SYS_GETDENTS)
5. `sprintf.c` / `snprintf.c` — string formatting (extend existing fprintf)
6. `isatty.c` — terminal detection
7. `regex.c` — regular expressions (for grep, sed)
8. `glob.c` — filename globbing (for shell)
9. `time.c` / `ctime.c` — time formatting

### What to copy from Fuzix vs. write fresh

**Copy from Fuzix libc** (pure C, no Fuzix dependencies):
- getopt.c, strtol.c, regex.c, glob.c, qsort.c — library functions
- Most utility programs themselves — they're standalone .c files

**Write fresh** (Genix-specific):
- opendir/readdir/closedir — wraps SYS_GETDENTS
- isatty — wraps SYS_IOCTL
- Syscall stubs — already Genix-native

**Adapt from Fuzix** (needs minor changes):
- Programs that use Fuzix-specific headers: change `#include` paths
- Programs that call fork(): change to vfork()+exec() or spawn()
- Programs that assume Fuzix errno globals: use Genix return convention

---

## 7. Recommended Execution Order

```
NOW (no kernel changes):
  1. Add getopt.c, strtol.c, perror.c to libc
  2. Port Tier 0 utilities (tail, tee, yes, basename, etc.)
  3. Add to AUTOTEST to validate on Mega Drive
  4. Implement SYS_GETDENTS + opendir/readdir
  5. Port ls, find (Tier 1)

NEXT (kernel work — Phase 2d-LITE):
  6. Implement real vfork() in kernel
  7. Implement preemptive scheduling (timer interrupt)
  8. Make pipes blocking
  9. Basic signals (SIGINT, SIGTERM, SIGKILL)
 10. Port/write a real shell (Tier 2)

THEN (library enrichment):
 11. Add regex library to libc
 12. Port grep, sed (Tier 3)
 13. Add remaining libc functions as apps demand them
 14. Port ed, make, diff (Tier 4)

ONGOING:
 - Every ported app gets a host test if possible
 - Every ported app gets added to AUTOTEST
 - Verify on Mega Drive (make test-md-auto) after each batch
```

---

## 8. Mega Drive Considerations for Ported Apps

The Mega Drive has ~31 KB for user programs (0xFF8000–0xFFFE00). Most
simple Fuzix utilities compile to 4–8 KB, so they fit comfortably. But:

- **levee** (44 KB) already doesn't fit on Mega Drive — workbench only
- **grep with regex** may be tight depending on regex library size
- **make, ed, sed** are borderline — need measurement
- Programs are loaded one-at-a-time from ROM disk, so total ROM size is
  not a constraint (ROM can be up to 4 MB)

**Rule of thumb:** If a program's .text + .data + .bss + stack exceeds
~28 KB, it's workbench-only (or needs SRAM for extended user RAM).

The apps/Makefile already handles separate builds for workbench (linked
at 0x040000) and Mega Drive (linked at 0xFF8000). The PROGRAMS list can
have platform-specific exclusions:

```makefile
PROGRAMS = hello echo cat wc head true false tail tee grep ls ...
# Programs too large for Mega Drive
PROGRAMS_WB_ONLY = levee
```

---

## 9. SRAM as Extended RAM

The Mega Drive's 64 KB main RAM is the single biggest constraint. With
SRAM, we can dramatically expand what's possible.

### Current SRAM state

Genix already enables SRAM via the standard Sega mapper (write 0x03 to
0xA130F1). The Everdrive Pro has **64 KB SRAM** at 0x200000–0x20FFFF
(though only odd bytes are accessible on standard mappers, giving 32 KB
usable). Some Everdrive configurations provide full 16-bit access.

### SRAM usage plan

**Tier 1 — Persistent filesystem (already partially working):**
- minifs on SRAM for writable storage (config files, user data)
- ROM disk stays read-only for /bin, /usr

**Tier 2 — Extended user program memory:**
- Move USER_BASE to SRAM: programs load at 0x200000 instead of 0xFF8000
- This gives ~32–64 KB for user programs (vs. ~31 KB in main RAM)
- Main RAM freed up for kernel heap, buffers, process tables
- PAL already abstracts USER_BASE/USER_TOP — this is a config change

**Tier 3 — SRAM as general-purpose heap extension:**
- User malloc() allocates from SRAM when main RAM is exhausted
- sbrk() can be taught to switch to SRAM region after main RAM fills
- Allows programs like levee (44 KB) to run on Mega Drive

### Implementation approach

```
Main RAM (64 KB):
0xFF0000  Kernel .data + .bss (~25 KB)
~0xFF6300 Kernel heap (~1 KB)
~0xFF6800 Small scratch / stack area
0xFFFFFF  Kernel stack

SRAM (32–64 KB, Everdrive Pro):
0x200000  USER_BASE — user programs load here
~0x20F000 USER_TOP — stack starts here (grows down)
```

The PAL layer detects SRAM size at boot (probe write/readback) and
sets USER_BASE/USER_TOP accordingly. If no SRAM, fall back to main RAM
layout. This is fully backward-compatible.

**SRAM access quirk:** On standard Sega mappers, SRAM is 8-bit (odd bytes
only). The Everdrive Pro mapper can provide 16-bit access. Genix should
detect which mode is available and adjust:
- 8-bit mode: byte-at-a-time access, no word/long loads (slow but works)
- 16-bit mode: normal access, full speed

### Memory budget with SRAM

| Config | User program space | Can run |
|--------|-------------------|---------|
| No SRAM (64 KB only) | ~31 KB | hello, cat, wc, small utils |
| 32 KB SRAM (odd-byte) | ~28 KB usable | Same + medium utils |
| 64 KB SRAM (16-bit) | ~56 KB usable | levee, grep, sed, ed |

---

## 10. VDP Graphics — A Thin, Fun API (Not SGDK)

### Why not SGDK?

SGDK is a bare-metal game engine — it owns interrupts, memory, boot,
and all hardware. Extracting its VDP functions into a userspace library
under an OS means building a shim layer that fights SGDK's design at
every turn: replacing its interrupt model, memory allocator, boot
sequence, Z80 control, and timer system. That's weeks of adaptation
work for questionable gain.

More importantly: **SGDK hides the hardware behind abstractions.** For a
learning project, that defeats the purpose. We want to understand the
VDP, not wrap it in someone else's engine.

The right approach is a thin API — ~10 functions, ~200 lines — where
each function is 2-5 VDP register writes. You read the implementation
and you **see** what the hardware does. This is how Fuzix handled
graphics on the Mega Drive: a minimal `/dev/vdp` device with a few
ioctls, and an `imshow` app that drove VDP registers directly.

### What Fuzix did (our starting point)

The FUZIX megadrive branch had:

- **`devvdp.c`** — a character device driver for `/dev/vdp` with two
  ioctls: `VDPCLEAR` (reset the display) and `VDPRESET` (restore text
  console). Minimal by design.
- **`imshow`** — a userspace program that displayed tile-based images
  by writing VDP register commands through assembly helper routines.
  It loaded tile patterns into VRAM, set up palettes, and mapped tiles
  to the nametable. No framebuffer, no abstractions — just the hardware.
- **VDP text driver** (`devvt.S`, `vdp.S`) — the same assembly routines
  Genix already uses, adapted directly from Fuzix. These handle
  `plot_char`, `scroll_up`, `cursor_on/off`, `VDP_reinit`, etc.

Reference: <https://github.com/EythorE/FUZIX/tree/megadrive/Kernel/platform/platform-megadrive>

Genix already has all the low-level VDP assembly from Fuzix. What's
missing is the userspace interface.

### The Genix graphics API: `<gfx.h>`

A fantasy-console-inspired API that maps directly to VDP hardware.
Think TIC-80/PICO-8 simplicity, but each function is a real VDP
operation you can read and understand.

```c
/* gfx.h — Genix graphics library (libc, ~200 lines) */

/* Setup */
int  gfx_open(void);                         /* open /dev/vdp, enter tile mode */
void gfx_close(void);                        /* restore text console */

/* Tiles (maps to VRAM pattern + nametable writes) */
void gfx_tiles(int id, const uint8_t *data, int count); /* upload tile patterns */
void gfx_map(int x, int y, int tile);                   /* place tile on screen */
void gfx_fill(int x, int y, int w, int h, int tile);    /* fill rect with tile */

/* Sprites (maps to Sprite Attribute Table writes) */
void gfx_sprite(int id, int x, int y, int tile, int flags); /* set sprite */
void gfx_sprite_move(int id, int x, int y);                  /* move sprite */
void gfx_sprite_hide(int id);                                /* hide sprite */

/* Color (maps to CRAM writes) */
void gfx_palette(int pal, int idx, uint16_t color);    /* set one color */
void gfx_palette_load(int pal, const uint16_t *colors, int count); /* bulk */

/* Scrolling (maps to VSRAM / VDP scroll registers) */
void gfx_scroll(int plane, int sx, int sy);  /* scroll background plane */

/* Sync */
void gfx_vsync(void);                        /* wait for VBlank */

/* Text (convenience — uses font tiles already in VRAM) */
void gfx_print(int x, int y, const char *s); /* draw text at tile position */
void gfx_cls(void);                          /* clear all tiles + sprites */
```

**That's 15 functions.** Each one is a thin wrapper around a kernel
ioctl or a direct VDP port write. No DMA queues, no sprite caches,
no resource compilers. A student can read `gfx_sprite()` and see the
exact bytes being written to the Sprite Attribute Table.

### How it maps to VDP hardware

```
gfx_tiles()        →  write tile patterns to VRAM 0x0000+
gfx_map()          →  write nametable entry at VRAM 0xC000+ (Plane A)
gfx_sprite()       →  write to SAT at VRAM 0xF000+
gfx_palette()      →  write to CRAM (color RAM, 128 bytes)
gfx_scroll()       →  write to VSRAM (vertical) / VDP reg (horizontal)
gfx_vsync()        →  block on VBlank interrupt (VDP status register)
gfx_print()        →  same as gfx_map() using font tile indices
gfx_cls()          →  zero nametable + SAT (same as VDP_ClearVRAM partial)
```

Each function is 2–5 register writes. No layers. No magic.

### Kernel side: /dev/vdp device

The kernel provides a character device with exclusive open (only one
process can do graphics at a time) and a handful of ioctls:

```c
/* Kernel /dev/vdp ioctls — minimal set */
#define VDP_IOC_LOADTILES   1  /* Upload tile patterns to VRAM */
#define VDP_IOC_SETMAP      2  /* Set nametable entries */
#define VDP_IOC_SETSPRITE   3  /* Set sprite attributes */
#define VDP_IOC_SETPAL      4  /* Set palette entries */
#define VDP_IOC_SCROLL      5  /* Set scroll position */
#define VDP_IOC_WAITVBLANK  6  /* Block until VBlank (for vsync) */
#define VDP_IOC_CLEAR       7  /* Clear screen (tiles + sprites) */
```

**Seven ioctls.** That's the whole kernel interface. The userspace
`gfx.h` library wraps these into the friendly API above.

**Why ioctls instead of direct hardware access?** Because this is a
multitasking OS with job control. The kernel needs to know who owns
the VDP so it can save/restore state when processes are suspended.
See Section 10a below.

### VDP ownership and job control (the key insight)

The VDP is like a terminal — **the foreground process owns it.**

When you press Ctrl+Z on a graphics program:

```
1. Keyboard generates SIGTSTP → delivered to foreground process group
2. Graphics program stops (P_STOPPED)
3. Kernel saves VDP state:
   - 24 VDP registers (24 bytes)
   - CRAM palette (128 bytes)
   - VSRAM scroll (80 bytes)
   - Sprite Attribute Table (640 bytes, 80 sprites × 8 bytes)
   Total: ~872 bytes — fits easily in the proc struct or a small alloc
   (Nametable and tile pattern data stay in VRAM — only the graphics
    process was writing to them, and text mode uses different VRAM regions)
4. Kernel restores text console (VDP_reinit + redraw)
5. Shell prompt appears — user can do other work

When the user types `fg %1`:
1. Kernel restores saved VDP state (registers, palette, sprites, scroll)
2. Graphics program receives SIGCONT, resumes from where it stopped
3. Display returns to the graphics program's last frame
```

This is exactly how Unix manages terminal ownership — the foreground
process group gets the terminal, background processes get SIGTTOU if
they try to write. Same concept, just tiles instead of text.

**What goes in the proc struct:**

```c
/* VDP state — saved on SIGTSTP, restored on SIGCONT */
struct vdp_state {
    uint8_t  regs[24];        /* VDP registers */
    uint16_t cram[64];        /* Color RAM (4 palettes × 16 colors) */
    uint16_t vsram[40];       /* Vertical scroll */
    uint8_t  sat[640];        /* Sprite Attribute Table */
    uint8_t  mode;            /* 0=text, 1=tile, 2=bitmap */
};  /* Total: ~872 bytes */

struct proc {
    /* ... existing fields ... */
    struct vdp_state *vdp;    /* NULL if process doesn't use graphics */
};
```

The `vdp_state` is allocated from kernel heap only when a process
opens `/dev/vdp`. Most processes don't use graphics and pay zero cost.

### How Fuzix solved this (reference)

Fuzix's approach on constrained platforms (Z80, 6809, 68000):

- **Text console first, graphics optional.** Every platform starts with
  a working text console. Graphics is a separate device (`/dev/vdp`),
  never mixed into the TTY path.
- **Assembly for VDP operations.** All per-platform I/O is in tight
  assembly loops (16-bit writes, DBRA counters). Genix already has
  these from Fuziz's `vdp.S` and `devvt.S`.
- **Line discipline independent of display.** Fuzix's `tty.c` handles
  cooked/raw mode, echo, signals — and calls down to platform-specific
  `tty_putc()` for output. The TTY layer doesn't know about VDP tiles.
- **Minimal /dev/vdp.** Fuzix's `devvdp.c` had two ioctls. That was
  enough to build `imshow` and graphics demos. Start minimal, grow.

### Implementation plan

**Phase A — Kernel /dev/vdp + imshow (first graphics app):**
1. Add DEV_VDP to device table (`kernel/dev.c`), exclusive open
2. Implement LOADTILES, SETMAP, SETPAL, WAITVBLANK ioctls
3. Implement close (restore text console via `VDP_reinit`)
4. Write `gfx.h` userspace library (wraps ioctls)
5. Port `imshow` from Fuzix — display a tile image from ROM
6. Host test for gfx_* API (mock VDP, test tile math)

**Phase B — Sprites, scrolling, demo apps:**
7. Implement SETSPRITE, SCROLL ioctls
8. Write a sprite demo (bouncing sprites with scrolling background)
9. Write `gfx_print()` — text on graphics screen (uses existing font)

**Phase C — VDP save/restore for job control:**
10. Add `struct vdp_state` and save/restore on SIGTSTP/SIGCONT
11. Ctrl+Z a graphics program → text console resumes
12. `fg` → graphics state restored, program continues

**Phase D — More apps (see Section 12):**
13. Tiny BASIC with graphics commands
14. Simple game port (sprite-based)
15. MicroPython (stretch, needs large SRAM)

---

## 11. Updated Master Plan

This is a **multitasking OS with real job control.** You can run a
graphics demo, press Ctrl+Z, do some work in the shell, and `fg` to
resume the demo. That's the north star — everything below serves it.

The phases are ordered by dependency, not by difficulty:

```
PHASE 2d — Signals & job control (THE critical path):
  1. P_STOPPED state + SIGTSTP/SIGCONT handling
  2. Process groups (pgrp field in proc struct)
  3. Signal delivery mechanism (trampoline on user stack)
  4. Basic signals: SIGINT(^C), SIGTSTP(^Z), SIGCONT, SIGKILL, SIGTERM
  5. Preemptive scheduling (VBlank on MD, timer on workbench)
  6. Blocking pipe I/O (readers/writers sleep instead of returning 0)
  7. Real vfork() (parent freezes, child shares memory until exec)

  → After this: Ctrl+C kills, Ctrl+Z stops, `fg`/`bg`/`jobs` work

PHASE 2e — TTY subsystem (connects keyboard to job control):
  8. Port Fuzix tty.c line discipline (cooked/raw, echo, erase)
  9. Signal generation in TTY layer (^C→SIGINT, ^Z→SIGTSTP to fg pgrp)
 10. Interrupt-driven keyboard via VBlank (not polling busy-loop)
 11. /dev/null, /dev/tty, isatty()

  → After this: proper terminal behavior, programs can catch signals

PHASE 2f — Libc + app porting wave 1:
 12. Add getopt, strtol, perror, strerror, opendir/readdir to libc
 13. Port Tier 0 utilities (tail, tee, cut, tr, uniq, etc.)
 14. Port/write a real shell with job control (fg, bg, jobs, &)
 15. Add ported apps to AUTOTEST

PHASE 3b — VDP graphics + imshow:
 16. /dev/vdp device with exclusive open (kernel/dev.c)
 17. gfx.h userspace library (~15 functions, ~200 lines)
 18. imshow — first graphics app (tile image display from ROM)
 19. VDP save/restore on SIGTSTP/SIGCONT (job control for graphics)
 20. Sprites, scrolling, vsync — demo apps

PHASE 3c — Fun apps (the reward):
 21. Tiny BASIC with GFX commands (SPRITE, TILE, PALETTE, SCROLL)
 22. Port a simple sprite-based game (see Section 12)
 23. MicroPython on large SRAM flash carts (stretch goal)

PHASE 3d — SRAM expansion:
 24. SRAM size detection at boot (probe write/readback)
 25. USER_BASE/USER_TOP in SRAM when available
 26. Detect 8-bit vs 16-bit SRAM access mode
 27. Test levee + larger programs on Mega Drive with SRAM

PHASE 4 — Polish:
 28. Multi-TTY (text + graphics on different planes)
 29. Sound driver (Z80 + YM2612/PSG, needs SRAM for sample storage)
 30. More app ports (ed, grep with regex, make)
```

### The dependency chain

```
Signals (2d) → TTY (2e) → Shell with job control (2f)
                              ↓
                    VDP graphics (3b) → Fun apps (3c)
                              ↓
                    VDP save/restore (3b.19) depends on signals (2d)
```

Signals must come first because **everything depends on them**: job
control needs SIGTSTP/SIGCONT, the TTY layer needs to generate
SIGINT/SIGQUIT, pipes need SIGPIPE, and VDP save/restore needs
signal delivery to work. This matches the Fuzix development order.

### How Fuzix structured this (reference)

Fuzix's development on the 68000 followed the same order:

1. **Process management** (`process.c`) — fork/exec/wait/exit
2. **Signals** (`signal.c`) — delivery, trampoline, sigreturn
3. **TTY** (`tty.c`) — line discipline, signal generation, termios
4. **Platform drivers** — VDP, keyboard, SRAM
5. **Userspace** — shell, utilities, applications

Source references for implementing each:
- Context switch: `Kernel/cpu-68000/lowlevel-68000.S` (MOVEM.L save/restore, USP handling, RTE)
- Signal delivery: `Kernel/cpu-68000/lowlevel-68000.S` (trampoline frame on user stack)
- TTY: `Kernel/tty.c` (line discipline, ~500 lines of battle-tested code)
- Process groups: `Kernel/process.c` (pgrp, session, setpgrp())
- VDP graphics: `Kernel/platform/platform-megadrive/devvdp.c`

See `docs/fuzix-heritage.md` for the full source reference map.

### Job control: what it looks like when it's working

```
> exec /bin/demo        # Launch a sprite demo
  [demo runs, sprites bounce around the screen]
  ^Z                    # User presses Ctrl+Z
[1] Stopped  /bin/demo
> exec /bin/wc /etc/motd  # Do some other work
  3  12  84 /etc/motd
> jobs
[1] Stopped  /bin/demo
> fg %1                 # Resume the demo
  [VDP state restored, demo continues exactly where it was]
```

Behind the scenes:
1. ^Z → TTY layer generates SIGTSTP → delivered to demo's process group
2. Kernel saves VDP state (872 bytes: registers + palette + sprites + scroll)
3. Kernel calls `VDP_reinit()` + redraws text console
4. Shell runs, accepts commands, launches other programs
5. `fg %1` → kernel restores VDP state → delivers SIGCONT → demo resumes

### What the original plan got right

- 3000-line kernel target — achieved (3023 lines)
- Musashi workbench emulator — works exactly as planned
- Mega Drive port reusing FUZIX drivers — done
- minifs filesystem — done and working
- No fork(), vfork-based model — right call (even if spawn() was needed)

### What diverged and why it's OK

- **spawn() instead of vfork()**: Pragmatic. vfork() can be added now
  that the kernel is stable. spawn() proved the concept.
- **Cooperative scheduling**: Got multitasking working fast. Preemptive
  scheduling is the natural next step using VBlank interrupt.
- **Custom libc instead of newlib**: Better for Mega Drive. Newlib is
  too big. Growing the custom libc incrementally is the right approach.
- **No PIC/bFLT binaries**: Fixed-address flat binaries work for
  single-tasking. Relocation can be added when needed.

### What wasn't in the original plan but should have been

- **Job control** — A multitasking OS without Ctrl+Z is frustrating.
  This should have been in the plan from day one.
- **VDP graphics API** — The Mega Drive is a game console. Text-only
  is useful but graphics unlock the platform's real potential.
- **Fun apps** — Tiny BASIC, games, `imshow`. This is a learning
  project and the fun applications are the reward for building the OS.
- **SRAM as extended RAM** — The 64 KB limit is the #1 constraint.
  SRAM (especially Everdrive Pro's 64 KB) changes what's possible.

---

## 12. Fun Apps — The Reason We're Building This

This is a learning project. The kernel and OS infrastructure exist to
make fun things possible. These are the apps that make it worth it.

### imshow — Display images on the Mega Drive

The simplest possible graphics program: load tile data and palette
into VRAM, map tiles to the screen, done. This is what Fuzix's `imshow`
did, and it's the first program we should port.

```c
/* imshow.c — display a tile image */
#include <gfx.h>

extern const uint8_t image_tiles[];   /* tile pattern data (in ROM) */
extern const uint16_t image_pal[];    /* 16-color palette */

int main(void) {
    gfx_open();
    gfx_palette_load(0, image_pal, 16);
    gfx_tiles(0, image_tiles, 40 * 28);   /* 1120 tiles fill the screen */
    for (int y = 0; y < 28; y++)
        for (int x = 0; x < 40; x++)
            gfx_map(x, y, y * 40 + x);
    gfx_vsync();
    /* wait for keypress */
    getchar();
    gfx_close();
    return 0;
}
```

That's 15 lines. A student can read it and understand exactly what
the VDP is doing. The image data comes from a host tool that converts
PNG → tile patterns + palette (4bpp, Mega Drive format).

**Converter tool:** `tools/png2tiles` — takes a PNG image, quantizes
to 16 colors, splits into 8×8 tiles, outputs C arrays. Simple Python
script or small C program.

### Tiny BASIC with graphics

A BASIC interpreter that runs on the Mega Drive and can do graphics.
This is the killer app — type BASIC commands on the Saturn keyboard
and see sprites move on screen.

**Target:** [TinyBASIC](https://en.wikipedia.org/wiki/Tiny_BASIC) is
~2-3 KB of C code. Several public-domain implementations exist:
- **Mike Field's Tiny BASIC** (~800 lines C, public domain)
- **Scott Lawrence's TinyBASIC Plus** (~2000 lines, MIT license)
- **Robin Edwards' Arduino BASIC** (~1500 lines, MIT license)

All of these fit comfortably in 31 KB (Mega Drive user RAM without
SRAM). The interpreter runs in ~4 KB, leaving ~24 KB for BASIC
program text and variables.

**Graphics extensions — map directly to gfx.h:**

```basic
CLS                          ' clear screen (gfx_cls)
PALETTE 0, 1, 0x0E0          ' set color (gfx_palette)
TILE 10, 5, 42               ' place tile (gfx_map)
TILES 0, data, 16            ' upload tile data (gfx_tiles)
SPRITE 1, 100, 80, 5, 0      ' set sprite (gfx_sprite)
MOVE 1, 120, 90              ' move sprite (gfx_sprite_move)
SCROLL 0, 2, 0               ' scroll plane (gfx_scroll)
VSYNC                        ' wait for vblank (gfx_vsync)
K = INKEY()                  ' read keyboard (non-blocking)
```

Each BASIC graphics command is one `gfx_*()` call. No translation
layer needed. The BASIC interpreter dispatches to the same library
user C programs link against.

**Example — bouncing sprite in BASIC:**

```basic
10 SPRITE 0, 100, 100, 1, 0
20 DX = 2: DY = 1
30 X = 100: Y = 100
40 X = X + DX: Y = Y + DY
50 IF X > 300 OR X < 10 THEN DX = -DX
60 IF Y > 200 OR Y < 10 THEN DY = -DY
70 MOVE 0, X, Y
80 VSYNC
90 GOTO 40
```

This is fun. You type it in on the Saturn keyboard, hit RUN, and
sprites bounce around the screen. Press Ctrl+Z, you're back in the
Genix shell. Type `fg`, back to BASIC.

**Implementation plan:**
1. Pick a Tiny BASIC implementation (Mike Field's is smallest)
2. Add it to `apps/basic.c`
3. Add graphics commands that call `gfx_*()` functions
4. Add SAVE/LOAD commands that write/read BASIC source to SRAM
5. Total: ~1500 lines of C, fits in any Mega Drive config

### Sprite-based game ports

The `gfx.h` API maps directly to how old-school C sprite games work.
Many public-domain or liberally-licensed games use exactly this pattern:

```c
while (running) {
    read_input();
    update_positions();
    for (int i = 0; i < num_sprites; i++)
        gfx_sprite(i, sprites[i].x, sprites[i].y, sprites[i].tile, 0);
    gfx_scroll(0, bg_x++, 0);
    gfx_vsync();
}
```

**Candidate games to port (public domain / MIT / educational):**

| Game | Size | Description | Exercises |
|------|------|-------------|-----------|
| Pong | ~200 lines | Classic, two sprites + ball | Sprites, input, collision |
| Snake | ~300 lines | Tile-based movement | Tilemap, growth logic |
| Breakout | ~400 lines | Ball + paddle + blocks | Sprites, tile collision |
| Space Invaders | ~500 lines | Grid of enemies, player | Sprites, patterns |
| Flappy Bird | ~300 lines | Scrolling pipes, gravity | Scroll, sprites, physics |

All of these fit in 4-8 KB compiled. They run on any Mega Drive
config (no SRAM needed). They're simple enough to type in or study,
and they demonstrate the VDP doing what it was designed for: games.

**Where to find source:** GBA homebrew tutorials (Tonc, GBDK examples),
retro game jam entries, and DOS game tutorials all use this exact
sprite+tile pattern. The `gfx.h` API is intentionally similar to
what these communities use.

### MicroPython (stretch goal — needs large SRAM)

MicroPython's core interpreter is ~256 KB. This doesn't fit in 64 KB
main RAM, but with a large SRAM flash cart (Everdrive Pro, Mega
Everdrive Pro), it could run from SRAM:

```
ROM: MicroPython interpreter code + frozen modules
SRAM (256 KB+): Heap, Python objects, GC arena
Main RAM: Kernel + stack + I/O buffers
```

**Requirements:**
- Flash cart with 256 KB+ SRAM (Everdrive Pro in 16-bit SRAM mode)
- MicroPython compiled with `m68k-elf-gcc -m68000`
- Custom `mphal` port (console I/O via Genix syscalls)
- VDP bindings via `gfx` module wrapping `gfx.h`

**Why it might work:** MicroPython targets microcontrollers with
similar constraints (ESP32: 520 KB RAM, STM32: 256 KB). The 68000
at 7.67 MHz is slower but has more RAM than many MCU targets.

**Why it might not:** The 68000 lacks hardware multiply for 32-bit
values (software `__mulsi3`), which Python uses heavily for object
hashing and integer arithmetic. Performance may be too slow for
interactive use. Worth trying, but set expectations accordingly.

**If it works:**
```python
import gfx

gfx.open()
gfx.palette(0, 1, 0x0E0)  # green
gfx.sprite(0, 160, 112, 1, 0)

while True:
    gfx.sprite_move(0, x, y)
    x += dx
    gfx.vsync()
```

Python on a Sega Mega Drive. That's a flex.

### Old-school C graphics API comparison

The `gfx.h` API is intentionally similar to popular retro sprite APIs.
Here's how it maps to well-known frameworks:

| Operation | Genix gfx.h | GBA (Tonc) | SGDK | TIC-80 |
|-----------|-------------|------------|------|--------|
| Load tiles | `gfx_tiles()` | `memcpy(tile_mem, ...)` | `VDP_loadTileData()` | built-in |
| Place tile | `gfx_map()` | `se_mem[y][x] = tile` | `VDP_setTileMapXY()` | `mset()` |
| Set sprite | `gfx_sprite()` | `obj_set_attr()` | `VDP_setSprite()` | `spr()` |
| Move sprite | `gfx_sprite_move()` | `obj_set_pos()` | `VDP_setSpritePosition()` | `spr()` |
| Set color | `gfx_palette()` | `pal_bg_mem[n]` | `PAL_setColor()` | `poke(0x3FC0+n)` |
| Scroll | `gfx_scroll()` | `REG_BGxHOFS` | `VDP_setHorizontalScroll()` | `scroll()` |
| VSync | `gfx_vsync()` | `vid_vsync()` | `SYS_doVBlankProcess()` | automatic |
| Clear | `gfx_cls()` | custom | `VDP_clearPlane()` | `cls()` |

Someone who's written GBA homebrew or TIC-80 games can pick up
`gfx.h` immediately. That's the point — the API is the lingua franca
of tile+sprite hardware.

### Priority order

```
1. imshow          — proves /dev/vdp works, shows the hardware is fun
2. Bouncing sprite — proves sprites + vsync work, first "animation"
3. Tiny BASIC      — interactive, educational, the killer app
4. Pong            — first real game, proves the API is practical
5. Snake/Breakout  — builds confidence, good demos for show
6. MicroPython     — stretch goal, needs SRAM, ultimate flex
```
