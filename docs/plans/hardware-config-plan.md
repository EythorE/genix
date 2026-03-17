# Hardware Configuration & App Profiles Plan

**Goal:** Make Genix transparent and obvious to configure for new hardware
setups. A developer bringing up a new board should find one place that
describes what to set, why, and what the tradeoffs are — not hunt through
Makefiles, kernel headers, PAL files, and linker scripts.

**Status:** Plan (not yet implemented).

---

## Problem Statement

Today, hardware-specific configuration is scattered across five layers:

| What | Where | Example |
|------|-------|---------|
| Memory boundaries | `pal/*/platform.c` | `pal_user_base()` returns 0xFF9000 |
| Kernel tuning knobs | `kernel/kernel.h` | `MAXPROC 16`, `MAXINODE 128` |
| Platform overrides | `pal/megadrive/Makefile` | `-DNBUFS=8` |
| App inclusion | Top-level `Makefile` | `CORE_BINS` vs `APP_BINS` |
| Linker memory map | `kernel/kernel.ld`, `pal/megadrive/megadrive.ld` | RAM origin/length |

A new-hardware developer must read all five to understand why values
are what they are. There is no single document or file that says: "For
a 64 KB target, use these values; for a 1 MB target, use these."

### Specific pain points

1. **NBUFS override is invisible.** The Mega Drive Makefile passes
   `-DNBUFS=8` on the compiler command line. `kernel.h` has `#ifndef
   NBUFS` / `#define NBUFS 16`. A reader of `kernel.h` sees 16 and
   doesn't know it's overridden to 8 for Mega Drive.

2. **App inclusion is a flat list.** `CORE_BINS` is one long line in
   the top-level Makefile. There's no indication which apps are
   essential (dash, ls, cat), which are useful utilities (grep, sort),
   and which are extras (imshow, meminfo). A RAM-constrained target
   can't easily trim the disk image.

3. **Kernel limits are one-size-fits-all.** `MAXPROC=16`,
   `MAXINODE=128`, `MAXOPEN=64` are set for the workbench's 1 MB RAM.
   On Mega Drive with 24 KB kernel heap, these eat most of it. But
   reducing them requires knowing the RAM math, which isn't documented
   near the defines.

4. **No hardware budget sheet.** Someone evaluating "can Genix run on
   my 128 KB board?" has to manually add up kernel heap consumers
   (proc table + buffer cache + inode cache + open file table) and
   compare against available RAM. This arithmetic should be
   pre-computed and visible.

---

## Design: Platform Config Header

### Core idea

Each platform gets a **`config.h`** in its PAL directory that
centralizes every tunable for that hardware target. The kernel's
`kernel.h` reads defaults from a `platform_config.h` that the build
system points to the right platform.

```
pal/workbench/config.h      ← workbench tuning
pal/megadrive/config.h      ← Mega Drive tuning
kernel/platform_config.h    ← symlink or -include from build
```

### What goes in config.h

Every value that a hardware porter might need to change, with a
comment explaining the RAM cost and tradeoff:

```c
/* pal/megadrive/config.h — Mega Drive kernel configuration
 *
 * Main RAM: 64 KB (0xFF0000-0xFFFFFF)
 * Kernel BSS/data: ~4 KB
 * Kernel heap: ~24 KB (from BSS end to USER_BASE)
 * User space: ~27.5 KB (USER_BASE to USER_TOP)
 *
 * Budget for kernel heap consumers:
 *   proc table:  MAXPROC * ~560 B  =  4,480 B  (16 * 280 + 16 * kstack)
 *   buf cache:   NBUFS * 1,036 B   =  8,288 B  (8 * (1024 + 12))
 *   inode cache: MAXINODE * 64 B    =  4,096 B  (64 * 64)
 *   open files:  MAXOPEN * 24 B     =    768 B  (32 * 24)
 *   pipes:       MAXPIPE * 528 B    =  2,112 B  (4 * (512 + 16))
 *   ─────────────────────────────────────────────
 *   Total:                          ~19,744 B
 *   Remaining heap:                  ~4,256 B  (for exec buffers, etc.)
 */

#define CFG_MAXPROC      16
#define CFG_MAXFD        16
#define CFG_MAXOPEN      32    /* workbench uses 64; MD doesn't need that many */
#define CFG_MAXINODE     64    /* workbench uses 128; MD has fewer files */
#define CFG_NBUFS         8    /* workbench uses 16; halved for MD RAM */
#define CFG_MAXPIPE       4
#define CFG_KSTACK_SIZE 512
#define CFG_PIPE_SIZE   512

#define CFG_USER_STACK_DEFAULT  4096
#define CFG_USER_HEAP_DEFAULT   4096
```

The workbench `config.h` would have larger values and a matching
budget comment for 1 MB RAM.

### How kernel.h consumes it

```c
/* kernel.h — use platform config, fall back to safe defaults */
#include "platform_config.h"

#ifndef CFG_MAXPROC
#define CFG_MAXPROC 16
#endif
#define MAXPROC  CFG_MAXPROC

#ifndef CFG_NBUFS
#define CFG_NBUFS 16
#endif
#define NBUFS  CFG_NBUFS

/* ... etc for each tunable ... */
```

This eliminates the `-DNBUFS=8` Makefile hack. All configuration
lives in one file per platform, with the RAM budget math visible
right above the defines.

### Build system integration

The platform Makefile passes `-include ../../pal/<platform>/config.h`
(or the top-level Makefile sets a variable). Two options:

**Option A: -include flag (simpler)**
```makefile
# pal/megadrive/Makefile
CFLAGS += -include config.h
```
All kernel .c files compiled for this platform automatically pick up
the config. No changes to kernel.h needed beyond the `#ifndef`
guards.

**Option B: Generated symlink (more explicit)**
```makefile
# Top-level Makefile
megadrive:
    ln -sf pal/megadrive/config.h kernel/platform_config.h
    $(MAKE) -C pal/megadrive ...
```
Kernel.h does `#include "platform_config.h"`. The symlink points to
the active platform's config.

**Recommendation:** Option A. It's simpler, doesn't require symlink
management, and matches how `-DNBUFS=8` already works — just moves
it from the command line into a file.

---

## Design: App Profiles

### Core idea

Replace the monolithic `CORE_BINS` list with categorized groups that
a platform Makefile can compose. Each category has a clear purpose.

### App categories

```makefile
# apps/profiles.mk — app categories for disk image composition

# BOOT: Minimum viable system. Shell + enough to navigate and debug.
# Without these, the system is unusable.
APPS_BOOT = apps/dash/dash apps/ls apps/cat apps/echo apps/cp apps/mv \
            apps/rm apps/mkdir apps/clear

# COREUTILS: Standard Unix utilities. Expected on any usable system.
APPS_COREUTILS = apps/head apps/tail apps/tee apps/wc apps/grep \
                 apps/sort apps/more apps/find apps/xargs \
                 apps/touch apps/kill apps/which apps/uname \
                 apps/env apps/expr apps/sleep apps/true apps/false

# TEXT: Text processing tools. Useful for scripting and pipelines.
APPS_TEXT = apps/cut apps/tr apps/uniq apps/nl apps/rev \
           apps/basename apps/dirname apps/cmp apps/strings \
           apps/fold apps/expand apps/unexpand apps/paste \
           apps/comm apps/seq apps/tac apps/od apps/yes

# SYSUTIL: System inspection and diagnostics.
APPS_SYSUTIL = apps/meminfo apps/hello

# GRAPHICS: VDP/graphics demos (only meaningful on Mega Drive).
APPS_GRAPHICS = apps/imshow

# EDITORS: Text editors (RAM-hungry, may not fit all targets).
APPS_EDITORS = $(wildcard apps/levee/levee)

# GAMES: Games (future — Wave 3+).
# APPS_GAMES = apps/hamurabi apps/dopewars apps/wump ...

# LANGUAGES: Programming language interpreters (future — Wave 7).
# APPS_LANGUAGES = apps/basic apps/forth ...
```

### Platform composition

```makefile
# Top-level Makefile

include apps/profiles.mk

# Workbench: everything (plenty of RAM and disk space)
APPS_WORKBENCH = $(APPS_BOOT) $(APPS_COREUTILS) $(APPS_TEXT) \
                 $(APPS_SYSUTIL) $(APPS_EDITORS)

# Mega Drive: boot + coreutils + text (no editors — too large)
APPS_MEGADRIVE = $(APPS_BOOT) $(APPS_COREUTILS) $(APPS_TEXT) \
                 $(APPS_SYSUTIL) $(APPS_GRAPHICS)

# Hypothetical 128 KB target: boot + coreutils only
# APPS_MYBOARD = $(APPS_BOOT) $(APPS_COREUTILS)

disk: tools apps
    tools/mkfs.minifs disk.img 512 $(APPS_WORKBENCH)

disk-md: tools apps
    tools/mkfs.minifs disk-md.img 512 $(APPS_MEGADRIVE)
```

A new-hardware developer adds their board name, picks which
categories to include, and the disk image builds with the right set.

### Dash builtins vs standalone binaries

Dash already provides 39 builtin commands (echo, printf, test, [,
true, false, kill, cd, pwd, etc.). On a RAM-constrained target, the
standalone versions of these are technically redundant — they exist
only for `xargs`, `find -exec`, and POSIX compliance in scripts.

The app profiles make this visible: `APPS_BOOT` includes both dash
(with builtins) and standalone echo/true/false. A minimal profile
could drop the standalone versions if disk space matters:

```makefile
# Ultra-minimal: dash builtins cover echo, true, false, test, kill, pwd
APPS_TINY = apps/dash/dash apps/ls apps/cat apps/cp apps/rm apps/mkdir
```

This is a documentation/visibility improvement, not a code change.
The current system already works this way — it's just not obvious.

---

## Design: Hardware Budget Document

### Core idea

A new file `docs/hardware-budgets.md` that shows the concrete RAM
arithmetic for each platform. A developer evaluating Genix for new
hardware reads this first.

### Contents

```markdown
# Hardware RAM Budgets

## How to read this document

Each platform section shows:
1. Total RAM available
2. Kernel static allocation (BSS + data)
3. Kernel heap budget (what kmalloc allocates at boot)
4. User space (what's left for programs)

Tune the kernel config values to fit your heap budget.

## Mega Drive (64 KB)

Total RAM: 65,536 bytes (0xFF0000-0xFFFFFF)
Kernel stack (top of RAM): 512 bytes
Kernel BSS+data: ~4,000 bytes
Kernel heap: ~24,000 bytes (BSS end → USER_BASE)
User space: ~27,500 bytes (USER_BASE → USER_TOP)

### Kernel heap consumers

| Consumer | Formula | Bytes |
|----------|---------|-------|
| Process table | MAXPROC × sizeof(proc) | 16 × 280 = 4,480 |
| Kernel stacks | MAXPROC × KSTACK_SIZE | 16 × 512 = 8,192 |
| Buffer cache | NBUFS × (BLOCK_SIZE + hdr) | 8 × 1,036 = 8,288 |
| Inode cache | MAXINODE × sizeof(inode) | 64 × 64 = 4,096 |
| Open file table | MAXOPEN × sizeof(ofile) | 32 × 24 = 768 |
| Pipe buffers | MAXPIPE × sizeof(pipe) | 4 × 528 = 2,112 |
| **Total** | | **~27,936** |
| **Remaining** | 24,000 - 27,936 | **tight** |

(Note: current MAXINODE=128 and MAXOPEN=64 are workbench values.
MD should use MAXINODE=64 and MAXOPEN=32 to fit.)

## Workbench (1 MB)
... (generous budget, everything fits easily) ...

## Template: New Platform
... (fill in your values) ...
```

---

## Design: New Platform Checklist

A section in the hardware budget doc (or a standalone file) that
walks through bringing up Genix on new hardware.

### Checklist

```
Adding a new platform to Genix
==============================

1. Create pal/<yourplatform>/
   - platform.c: implement all pal_*() functions
   - config.h: kernel tuning (copy from closest existing platform)
   - Makefile: build rules
   - <yourplatform>.ld: linker script with your memory map

2. Fill in config.h
   - Calculate kernel heap size (BSS end → USER_BASE)
   - Use the budget table to choose MAXPROC, NBUFS, MAXINODE, etc.
   - Rule of thumb: kernel heap consumers should use ≤85% of heap

3. Choose app profile
   - 32 KB heap or less: APPS_BOOT only (~9 binaries)
   - 32-128 KB heap: APPS_BOOT + APPS_COREUTILS (~25 binaries)
   - 128 KB+ heap: everything

4. Add make targets
   - Add your platform to the top-level Makefile
   - Add a disk-<platform> target with your app profile

5. Test
   - make test (host tests still pass — kernel logic unchanged)
   - Boot on your hardware or emulator
   - Run meminfo to verify heap/user space sizes match expectations
```

---

## Implementation Plan

### Step 1: Create config.h files (low risk, high value)

1. Create `pal/megadrive/config.h` with all tunable defines and
   the RAM budget comment block.
2. Create `pal/workbench/config.h` with workbench values.
3. Modify `pal/megadrive/Makefile`: replace `-DNBUFS=8` with
   `-include config.h`.
4. Modify `kernel/Makefile`: add `-include ../../pal/workbench/config.h`
   (or equivalent).
5. Modify `kernel/kernel.h`: wrap each limit define in `#ifndef CFG_X`
   / `#define X CFG_X` pattern so config.h values take precedence.
6. Verify: `make test-all` must pass — this is a refactor, no
   behavioral change.

**Tradeoff note:** Currently MAXINODE=128 and MAXOPEN=64 are used on
both platforms. Reducing them for Mega Drive is a separate step that
should be validated (does the autotest still pass with fewer inodes
and open files?). Step 1 only moves existing values into config.h
without changing them.

### Step 2: Create app profiles (low risk, medium value)

1. Create `apps/profiles.mk` with the categorized app lists.
2. Modify top-level Makefile to `include apps/profiles.mk` and
   replace `CORE_BINS` / `APP_BINS` with composed profiles.
3. Verify: `make disk` and `make disk-md` produce identical images
   to before (same app lists, just organized differently).

### Step 3: Write hardware budget documentation (zero risk)

1. Create `docs/hardware-budgets.md` with the RAM budget tables for
   workbench and Mega Drive.
2. Add the new-platform checklist.
3. Add a link from CLAUDE.md's reference docs section.

### Step 4: Tune Mega Drive config (moderate risk)

This is the step that actually changes behavior. With config.h in
place:

1. Reduce `CFG_MAXINODE` from 128 to 64 for Mega Drive.
2. Reduce `CFG_MAXOPEN` from 64 to 32 for Mega Drive.
3. Run `make test-all` — the autotest exercises pipes, processes,
   and file operations enough to catch regressions.
4. Run `meminfo` on Mega Drive to verify heap usage decreased.

**Decision point:** These reductions save ~6 KB of kernel heap on
Mega Drive. Whether to do this now or defer depends on whether the
current heap is actually running out. If it's working fine at
current values, defer.

---

## What This Does NOT Change

- **PAL interface** — `pal_user_base()`, `pal_mem_start()`, etc.
  remain the runtime source of truth for memory boundaries. Config.h
  is for compile-time kernel tunables only.
- **Binary format** — apps are still one format, relocatable, run on
  all platforms.
- **Linker scripts** — still per-platform, still define the memory
  map. Config.h doesn't replace them.
- **Build targets** — `make run`, `make megadrive`, `make test-all`
  work the same way.

---

## Rejected Approaches

### Single config.h with #ifdef per platform

```c
#ifdef PLATFORM_MEGADRIVE
#define NBUFS 8
#else
#define NBUFS 16
#endif
```

Rejected because: scales badly. Every new platform adds another
`#ifdef` branch. Per-platform files are cleaner and self-contained.

### Kconfig-style menu system

A Linux-style `make menuconfig` for selecting kernel options.

Rejected because: massive overkill for a system with two platforms
and ~10 tunables. The config.h file IS the menu — it's 20 lines
with comments. If Genix ever has 10+ platforms, revisit.

### Runtime configuration (environment variables, config file)

Read kernel tunables from a config file at boot.

Rejected because: kernel data structures (proc table, inode cache)
are allocated at boot with fixed sizes. Changing them at runtime
would require dynamic resizing, which adds complexity for no benefit
on a system that boots to a known hardware target.

---

## Future Considerations

- **Phase 7 (SD card):** A new platform variant (EverDrive types)
  might want different disk buffer counts. Config.h handles this
  naturally.
- **Phase 8 (PSRAM):** With 512 KB per process, user stack/heap
  defaults should increase. Config.h is the right place.
- **New boards:** If someone ports Genix to another 68000 board
  (e.g., a homebrew SBC with 512 KB RAM), they create a new PAL
  directory with a config.h tuned to their hardware. The app
  profile system lets them pick which apps to include.
