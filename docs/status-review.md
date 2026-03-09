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

## 10. VDP Graphics Driver — /dev/vdp

### Current state

Genix uses the VDP exclusively for text console output (40×28 tiles,
8×8 font). The entire graphics stack is buried in assembly (vdp.S,
devvt.S, dbg_output.S) with no userspace API. There's no /dev/vdp
device.

The FUZIX Mega Drive branch has a working `devvdp.c` that exposes a
minimal device driver (open/write/ioctl with VDPCLEAR and VDPRESET),
plus a working `imshow` app that displays images using direct VDP
register writes from userspace assembly routines.

### Design goals

1. **POSIX-ish device interface** — programs open /dev/vdp, use
   write() and ioctl() for VDP operations
2. **Safe multiplexing** — kernel mediates VDP access so text console
   and graphics programs don't step on each other
3. **SGDK compatibility layer** — provide enough API surface that SGDK
   demos can be ported with minimal changes
4. **Framebuffer mode** — software framebuffer (like SGDK's BMP engine)
   for pixel-level drawing

### VDP hardware summary

```
Ports:
  VDP_DATA    = 0xC00000  (read/write tile, palette, sprite data)
  VDP_CONTROL = 0xC00004  (commands, register writes, status)

VRAM (64 KB video RAM):
  0x0000–0xBFFF  Tile patterns (font + graphics tiles)
  0xC000–0xDFFF  Plane A nametable (main console / game background)
  0xE000–0xEFFF  Plane B nametable (debug overlay / parallax)
  0xF000–0xF7FF  Sprite attribute table (SAT)
  0xFC00–0xFDFF  H-scroll table

CRAM (128 bytes): 4 palettes × 16 colors (9-bit RGB)
VSRAM (80 bytes): per-column vertical scroll offsets

Display: 320×224 pixels (H40 mode), 40×28 tiles of 8×8 pixels
Sprites: 80 max, 8×8 to 32×32 pixels, 4 palettes, priority, H/V flip
DMA: bulk transfer from 68000 RAM/ROM → VRAM/CRAM/VSRAM
```

### Proposed /dev/vdp interface

**Device model:** /dev/vdp is a character device. Opening it switches
the VDP from text console mode to graphics mode. Closing it restores
the text console. Only one process can have /dev/vdp open at a time.

**ioctl commands:**

```c
/* Mode control */
#define VDP_IOC_SETMODE     1  /* Switch text/tile/bitmap mode */
#define VDP_IOC_GETMODE     2

/* Palette */
#define VDP_IOC_SETPAL      10  /* Set palette: {pal_num, count, colors[]} */
#define VDP_IOC_GETPAL      11

/* Tiles */
#define VDP_IOC_LOADTILES   20  /* Load tile data to VRAM: {dest, count, data} */
#define VDP_IOC_SETMAP      21  /* Set nametable entries: {plane, x, y, w, h, data} */
#define VDP_IOC_SCROLL      22  /* Set scroll position: {plane, hscroll, vscroll} */

/* Sprites */
#define VDP_IOC_SETSPRITE   30  /* Set sprite attributes: {id, x, y, tile, flags} */
#define VDP_IOC_MOVESPRITE  31  /* Move sprite: {id, x, y} */

/* DMA */
#define VDP_IOC_DMA         40  /* DMA transfer: {src, dst, len, type} */

/* Framebuffer (bitmap mode) */
#define VDP_IOC_FLIP        50  /* Swap double buffers */
#define VDP_IOC_CLEAR       51  /* Clear framebuffer */

/* VBlank sync */
#define VDP_IOC_WAITVBLANK  60  /* Block until next VBlank */

/* Raw register access (for advanced/SGDK use) */
#define VDP_IOC_SETREG      70  /* Write VDP register: {reg, value} */
#define VDP_IOC_GETREG      71
```

**write() on /dev/vdp:** In bitmap mode, write pixel data directly to
the software framebuffer. In tile mode, write nametable entries. This
gives a simple `write(fd, pixels, size)` path for image display.

**read() on /dev/vdp:** Read VDP status, current mode, screen dimensions.

### Framebuffer / bitmap mode

SGDK's BMP engine proves this works: fake a 256×160 framebuffer using
a 32×20 tile grid. Each frame, DMA the tile data from RAM to VRAM during
VBlank. Double-buffered for flicker-free display.

**Memory cost:** ~41 KB (two 20 KB buffers + overhead). This only fits
with SRAM — the framebuffer lives in SRAM while the kernel and DMA
engine stay in main RAM.

**Without SRAM:** Single-buffered 256×160 = ~20 KB. Possible in main RAM
if the program is small, but tight. Tile mode (no framebuffer) uses
almost no extra RAM and is the better fit for most graphics programs.

### Three VDP modes

| Mode | Resolution | RAM cost | Best for |
|------|-----------|----------|----------|
| **Text** | 40×28 chars | 0 (uses font tiles) | Console, shell |
| **Tile** | 320×224 (40×28 tiles) | ~2 KB nametable | Games, imshow |
| **Bitmap** | 256×160 pixels | 20–41 KB | Drawing, demos |

### SGDK integration strategy

SGDK is a bare-metal framework — it owns the CPU, interrupts, and all
hardware. We can't use it as-is under Genix. But we can extract its
**VDP helper functions** as a userspace library:

**What to extract from SGDK (pure VDP logic, no OS dependencies):**
- `vdp.c` — register configuration, mode setup, scroll control
- `vdp_bg.c` — background plane/tilemap management
- `vdp_tile.c` / `vdp_tile_a.s` — tile loading and manipulation
- `vdp_spr.c` — sprite engine (attribute table management)
- `pal.c` — palette fade, cycling effects
- `dma.c` / `dma_a.s` — DMA queue and transfer management
- `bmp.c` / `bmp_a.s` — software framebuffer engine
- `sprite_eng.c` — high-level sprite management

**What we must replace (SGDK bare-metal assumptions):**
- `sys.c` / `sys_a.s` — interrupt handlers, VDP init → use kernel
- `memory.c` / `memory_a.s` — RAM allocator → use Genix malloc
- `joy.c` — controller input → use Genix /dev/input or stdin
- `z80_ctrl.c` — Z80 audio control → kernel manages Z80
- `timer.c` — VBlank timing → use kernel VDP_IOC_WAITVBLANK
- Boot code — Genix owns boot

**The shim layer (libsgdk.a for Genix):**

```c
/* sgdk_shim.h — Maps SGDK hardware access to Genix /dev/vdp ioctls */

static int vdp_fd = -1;

static inline void VDP_setReg(u16 reg, u8 value) {
    struct vdp_reg r = { reg, value };
    ioctl(vdp_fd, VDP_IOC_SETREG, &r);
}

static inline void VDP_waitVSync(void) {
    ioctl(vdp_fd, VDP_IOC_WAITVBLANK, 0);
}

/* DMA queued through kernel to avoid race conditions */
static inline void DMA_queueDma(u8 type, u32 from, u16 to, u16 len, u16 step) {
    struct vdp_dma d = { from, to, len, type };
    ioctl(vdp_fd, VDP_IOC_DMA, &d);
}
```

This lets SGDK demo code compile against Genix with:
1. `#include "sgdk_shim.h"` instead of `#include <genesis.h>`
2. Link with `libsgdk.a` (extracted + adapted SGDK functions)
3. Open /dev/vdp at program start, close on exit

### SGDK demos worth porting

| Demo | Complexity | Exercises |
|------|-----------|-----------|
| Sprite demo | Low | Sprite engine, palette, VBlank sync |
| Scroll demo | Low | Tile planes, H/V scroll |
| BMP (3D wireframe) | Medium | Framebuffer mode, line drawing |
| Image display | Low | Tile loading, palette (like FUZIX imshow) |
| DMA stress test | Medium | DMA queue, timing |

### Implementation phases

**Phase A — Minimal /dev/vdp (kernel side):**
1. Add DEV_VDP to device table (kernel/dev.c)
2. Implement open (exclusive access, switch from text mode)
3. Implement ioctl for SETPAL, LOADTILES, SETMAP, WAITVBLANK
4. Implement close (restore text console)
5. Port imshow from FUZIX as first graphics app

**Phase B — Sprite and scroll support:**
6. Add SETSPRITE, MOVESPRITE ioctls
7. Add SCROLL ioctl (per-plane H/V scroll)
8. Add DMA ioctl (queued, executed during VBlank)
9. Write a simple sprite demo

**Phase C — Framebuffer mode (needs SRAM):**
10. Implement bitmap mode (BMP engine adapted from SGDK)
11. Software framebuffer in SRAM, DMA to VRAM on VBlank
12. write() path for direct pixel data
13. Port SGDK 3D wireframe demo

**Phase D — SGDK compatibility library:**
14. Extract SGDK VDP/sprite/palette/DMA functions
15. Build libsgdk.a with Genix shim layer
16. Port 2-3 SGDK sample programs
17. Document the API for Genix graphics programming

---

## 11. Updated Master Plan

Incorporating VDP graphics, SRAM expansion, and app porting into the
overall project timeline:

```
PHASE 2d-LITE — Core kernel completion (prerequisite for everything):
  1. Preemptive scheduling (timer/VBlank interrupt)
  2. Real vfork() + exec()
  3. Blocking pipes
  4. Basic signals (SIGINT, SIGTERM, SIGKILL)
  5. SYS_GETDENTS + opendir/readdir

PHASE 2f — App porting wave 1 (simple filters):
  6. Add getopt, strtol, perror, strerror to libc
  7. Port Tier 0 utilities (tail, tee, grep, sort, cut, tr, etc.)
  8. Port/write a real shell
  9. Add ported apps to AUTOTEST

PHASE 3b — SRAM expansion:
 10. SRAM size detection at boot (probe write/readback)
 11. USER_BASE/USER_TOP in SRAM when available
 12. Detect 8-bit vs 16-bit SRAM access mode
 13. Update PAL to expose SRAM config to kernel
 14. Test levee on Mega Drive with SRAM

PHASE 3c — VDP graphics driver:
 15. /dev/vdp device with exclusive open + mode switch
 16. Palette, tile loading, nametable ioctls
 17. VBlank sync ioctl
 18. Port imshow from FUZIX
 19. Sprite and scroll ioctls
 20. DMA ioctl (queued, VBlank-safe)

PHASE 3d — Framebuffer + SGDK (needs SRAM):
 21. Software framebuffer in SRAM
 22. BMP engine (adapted from SGDK)
 23. Extract SGDK VDP functions → libsgdk.a
 24. Port SGDK demos (sprites, scrolling, 3D wireframe)

PHASE 2e — TTY polish (as needed):
 25. Ctrl+C → SIGINT delivery
 26. /dev/null
 27. isatty()

PHASE 4 — Stretch goals:
 28. Interrupt-driven keyboard (not polling)
 29. Multi-TTY (text + graphics coexistence)
 30. Sound driver (Z80 + YM2612/PSG)
 31. More SGDK integration (resource compiler, map engine)
```

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

- **VDP graphics driver** — The Mega Drive is a game console. Text-only
  is useful but graphics unlock the platform's real potential.
- **SRAM as extended RAM** — The 64 KB limit is the #1 constraint.
  SRAM (especially Everdrive Pro's 64 KB) changes what's possible.
- **SGDK integration** — Standing on the shoulders of the best Mega Drive
  SDK makes graphical programs practical without reinventing everything.
