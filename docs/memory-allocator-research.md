# User Memory Allocator Research

## Problem Statement

Genix on the Mega Drive has 27.5 KB of user RAM (0xFF9000–0xFFFE00).
The current fixed-slot allocator divides this into 2 × ~13.75 KB slots.
With XIP (text in ROM), only data+bss+stack occupy RAM:

| Program | Text (ROM) | Data+BSS (RAM) | +Stack | Total RAM |
|---------|-----------|----------------|--------|-----------|
| dash    | 88,796 B  | 6,788 B        | 4,096  | 10,884 B  |
| more    | ~2 KB     | 632 B          | 4,096  | 4,728 B   |
| ls      | ~1.5 KB   | ~400 B         | 4,096  | ~4,500 B  |
| grep    | ~3 KB     | ~500 B         | 4,096  | ~4,600 B  |
| levee   | 47,420 B  | 12,888 B       | 4,096  | 16,984 B  |

**The 2-slot limit means `ls | more` is impossible** — it needs 3
concurrent processes (dash + ls + more), but only 2 slots exist.
Total RAM needed is only ~20 KB of 27.5 KB available — the artificial
slot boundary wastes 7 KB.

## Current Design

### How slots work

```
slot_init():
  USER_SIZE < 64 KB → num_slots = 2
  USER_SIZE < 128 KB → num_slots = 4
  USER_SIZE >= 128 KB → num_slots = 6

  slot_sz = (USER_SIZE / num_slots) & ~3

Mega Drive:  2 slots × 13,568 B
Workbench:   6 slots × 117,440 B
```

`slot_alloc()` returns the first free slot index. `slot_base(i)` returns
`USER_BASE + i * slot_sz`. `slot_size()` returns the fixed size.

### What the plan said

PLAN.md line 126-127:
> Start with 2 large slots. Can split into 3-4 smaller slots later
> if needed. Slot sizing is a runtime knob, not a structural decision.

docs/megadrive.md line 698:
> Memory fragmentation — Fixed-size allocations avoid fragmentation entirely

### Why 2 slots were chosen

The Phase 6 plan chose 2 large slots as the simplest starting point:
- dash needs ~10.9 KB → fits in 13.75 KB
- One child process (single commands) → fits in the other slot
- Zero fragmentation — every slot is reusable by any program
- Implementation: ~20 lines (bitmap allocator)

The plan explicitly said to increase the slot count later if needed.
Pipelines weren't tested until after Phase D, by which point the 2-slot
limit was baked in.

## Options Analysis

### Option 1: Increase slot count to 3 or 4

**Change:** One line in `slot_init()` — use `num_slots = 3` on Mega Drive.

| Config   | Slot size | Dash fits? | Slots for children |
|----------|-----------|------------|-------------------|
| 2 slots  | 13,568 B  | Yes (10.9 KB) | 1              |
| 3 slots  | 9,045 B   | Barely — 10,884 > 9,045 ❌ | — |
| 4 slots  | 6,784 B   | No (10.9 KB > 6.8 KB) ❌ | — |

**Problem:** Dash doesn't fit in 3 slots (10,884 B > 9,045 B). The only
way to make 3 slots work is to shrink dash below 9 KB, which requires
cutting le_history from 2048 B to ~512 B (4 entries × 128 B). This is
feasible (see `libc/lineedit.c`) but loses most command history.

With 4 slots, dash's 10.9 KB is far too large. Non-starter without
major dash surgery.

**Verdict:** Works only if we also shrink dash. Fragile — any future
growth in dash BSS breaks 3 slots. And the slot waste problem returns
(more at 632 B gets a 9 KB slot — 93% wasted).

### Option 2: Variable-size allocator in main RAM

**Change:** Replace `slot_alloc/free/base/size` with a first-fit
allocator that hands out exactly the amount each process needs.

```
USER_BASE                                              USER_TOP
├── dash (10,884) ─├── ls (4,500) ─├── more (4,728) ─├── 7,424 free ──┤
```

**Advantages:**
- `ls | more` works (3 processes, 20.1 KB used, 7.4 KB free)
- Larger single programs fit (~16.6 KB max child vs 13.6 KB slot)
- No wasted space — more gets 4.7 KB, not 13.6 KB
- levee (16.9 KB) *might* fit if dash shrinks or if levee replaces dash

**Disadvantages:**
- External fragmentation is possible. Example:
  - dash(10.9 KB) + ls(4.5 KB) + grep(4.6 KB) allocated
  - ls exits → 4.5 KB gap
  - Next command needs 6 KB → doesn't fit in gap, fails with ENOMEM
    even though 11.9 KB is free (4.5 + 7.4)
  - With fixed slots, the freed slot always fits any same-size request
- More complex allocator (~30-50 lines vs ~20 lines)
- Phase 8 (PSRAM) assumes fixed-size slots for data

**Fragmentation reality check:** With ≤3 concurrent processes on Mega
Drive and mostly-LIFO exit order from pipelines (writer exits, then
reader), fragmentation in practice is rare. But "rare" isn't "never."

**Implementation complexity:**
- Approach A: kmalloc-style linked list with 8-byte inline headers.
  Headers eat user data space (8 B per alloc = 24 B for 3 processes).
  But process writes to mem_base would overwrite the header.
- Approach B: Scan proctab to find gaps (no inline metadata). Simple
  but O(n²) where n=16. With ≤16 processes, trivial.
- Approach C: Separate free-list for user memory pool. More code,
  duplicate of kmalloc logic.

### Option 3: Two-tier allocation (main RAM + SRAM)

**The user's proposed approach:** Variable-size in main RAM (64 KB),
fixed-size in SRAM (128 KB or 512 KB). If SRAM is available, each
process that doesn't fit in a main RAM gap gets a whole SRAM region.

**Problem:** This conflates two very different things:

1. **Main RAM user pool (27.5 KB):** Where data+bss+stack live for
   XIP processes. Text is in ROM. Only needs ~5-17 KB per process.
   This is the pool we're trying to subdivide better.

2. **SRAM (128-512 KB):** Currently used for the writable filesystem
   (minifs). Planned for Phase 7 SD card and Phase 8 PSRAM. The
   docs/megadrive.md plan says: "User programs still run in main RAM
   (no relocation yet)" for Phase 1, then "Load user programs into
   cartridge RAM instead of main RAM" for Phase 2.

The problem is: if user programs move to SRAM, they lose XIP.
SRAM-resident processes need text+data+bss+stack in SRAM (no ROM
execution). Dash would need ~99 KB in SRAM, not the ~11 KB it needs
with XIP in main RAM.

The real SRAM story is more nuanced (see Option 5).

### Option 4: Hybrid — 3 fixed small slots + 1 large remainder

**Change:** Give dash a dedicated slot at the bottom, then divide the
rest into smaller slots for children.

```
USER_BASE                                              USER_TOP
├── dash slot (12 KB) ─├── child 0 (5 KB) ─├── child 1 (5 KB) ─├─ child 2 (5 KB) ─┤
```

This is essentially "variable-size allocation with a fixed layout."
Dash always gets the big slot; children get small slots. If a child
needs more than 5 KB, it can't run.

**Problem:** The slot sizes are baked in at compile time. We don't
know at boot which program will be the shell. And if the user runs
levee (16.9 KB) standalone, it needs the big slot — but it's wired
for dash.

**Verdict:** Too rigid. Solves today's problem but breaks on any
future workload change.

### Option 5: The right SRAM integration (what the docs actually plan)

docs/megadrive.md lines 663-686 lay out a phased approach:

**Phase 1 (Open EverDrive, 128 KB SRAM):**
- Switch to 16-bit SRAM access
- SRAM used for writable filesystem (as today, but faster)
- User programs still run in main RAM with XIP
- No change to the allocator

**Phase 2 (relocatable binaries in SRAM):**
- Load user programs into SRAM instead of main RAM
- Non-XIP: text+data+bss+stack all in SRAM
- Main RAM freed entirely for kernel
- 128 KB fits many small programs

**Phase 3 (Pro SSF, 3.5 MB PSRAM):**
- PSRAM banks for per-process text isolation
- Each process gets a 512 KB bank
- Data still in main RAM (via -msep-data)

The key insight: **Phase 2 (SRAM as process memory) is explicitly
non-XIP.** When a process runs from SRAM, its text is not in ROM —
it's loaded from ROM to SRAM with full relocation. This means a
128 KB SRAM can hold maybe 2-3 medium programs (full text+data).

But Phase 3 (PSRAM) restores XIP-like behavior: text in PSRAM bank
(per-process), data in main RAM. This is the real win.

**For main RAM (the no-SRAM case):** The docs don't specify an
allocator change. The assumption was 2 slots → 3-4 slots if needed.

## The Actual Problem We Need to Solve

Let's separate the concerns:

### Concern 1: Pipelines on stock Mega Drive (no SRAM)

We need 3+ concurrent processes in 27.5 KB of main RAM. XIP ensures
most programs need only ~5-11 KB of RAM each. The total fits; the
slot boundaries don't.

### Concern 2: SRAM-resident processes (future, Phase 7-8)

Programs loaded from SD card into SRAM need full relocation. The SRAM
allocator is a separate concern from the main RAM allocator. It can
use fixed slots (SRAM is much larger) or variable sizes.

### Concern 3: Phase 8 PSRAM interaction

Phase 8 gives each process a 512 KB PSRAM bank for text, with data
in main RAM. The main RAM allocator still needs to hand out data+bss
regions. Whether those are fixed or variable doesn't affect PSRAM.

## Recommended Approach

### For main RAM: Variable-size, proctab-scanned allocator

The scan-proctab approach (Option 2B) is the right answer. Here's why:

**Fragmentation is manageable.** On a system with:
- 1 persistent process (dash, always at USER_BASE)
- 1-2 transient processes (pipeline stages, short-lived commands)
- LIFO exit order (writer exits before reader in pipelines)

...the allocation pattern is: dash at bottom (permanent), children
above (allocated and freed in stack-like order). Fragmentation
requires at least 3 allocations with non-LIFO frees, which barely
happens with ≤3 processes.

**The worst case is just ENOMEM.** If fragmentation does occur, the
exec() call returns ENOMEM — the same error as "no free slot." The
user can exit a program and retry. This is the correct behavior for
a memory-constrained system.

**The allocator is trivial.** Scan 16 proctab entries, find gaps,
first-fit. ~25 lines. No inline headers (proctab IS the metadata).
No free list. Coalescing is automatic (freed regions become gaps on
next scan).

**Phase 8 is unaffected.** PSRAM banks are for text, allocated
separately (per-process bank number in struct proc). The main RAM
allocator handles data+bss regions, which are the same whether text
is in ROM (XIP), PSRAM, or RAM. Variable-size data regions work fine
with PSRAM text banks.

### For SRAM: Fixed-size regions (when we get there)

SRAM is much larger (128 KB - 512 KB). Fixed-size regions make sense:
- No fragmentation
- Simple bitmap allocator (like the current slot allocator)
- Can hold full text+data+bss for SD-loaded programs
- Region size = SRAM_SIZE / max_sram_processes

This is a Phase 7-8 concern and should be designed when we implement
SRAM process loading. The main RAM allocator change doesn't affect it.

### For PSRAM: Bank allocator (Phase 8, already planned)

PLAN.md already describes this: track which 512 KB banks are in use,
allocate/free per process, context-switch writes the bank register.
~40 lines. Completely independent of the main RAM allocator.

## Implementation Plan

### Step 1: Variable-size user memory allocator

Replace `slot_init/alloc/free/base/size` in `kernel/mem.c` with:

```c
void umem_init(void);       /* just logs the pool range */

uint32_t umem_alloc(uint32_t need);
    /* Scan proctab for processes with mem_base in [USER_BASE, USER_TOP).
     * Sort by mem_base (insertion sort, ≤16 elements).
     * Walk gaps: [USER_BASE, first region), between regions, after last.
     * Return first gap >= need, or 0 on failure. */

void umem_free(void);
    /* No-op. The caller sets proc->mem_base = 0. The gap appears
     * automatically on next umem_alloc scan. */

void umem_stats(uint32_t *total, uint32_t *used, uint32_t *largest);
    /* Report pool stats for meminfo. */
```

**Critical detail: relocation table overlay.** During exec, the
relocation table is temporarily loaded into the BSS area. If
`reloc_count * 4 > bss_size`, the table extends beyond BSS. The
allocation must account for this:

```c
uint32_t exec_mem_need(struct genix_header *hdr) {
    uint32_t stack = HDR_STACK_SIZE(hdr);
    if (stack == 0) stack = USER_STACK_DEFAULT;
    uint32_t reloc_bytes = hdr->reloc_count * 4;
    uint32_t effective_bss = hdr->bss_size;
    if (reloc_bytes > effective_bss)
        effective_bss = reloc_bytes;   /* reloc table overlays BSS */
    return hdr->load_size + effective_bss + stack;
}

uint32_t exec_mem_need_xip(struct genix_header *hdr) {
    uint32_t stack = HDR_STACK_SIZE(hdr);
    if (stack == 0) stack = USER_STACK_DEFAULT;
    uint32_t data_size = hdr->load_size - hdr->text_size;
    uint32_t reloc_bytes = hdr->reloc_count * 4;
    uint32_t effective_bss = hdr->bss_size;
    if (reloc_bytes > effective_bss)
        effective_bss = reloc_bytes;   /* reloc table overlays BSS */
    return data_size + effective_bss + stack;
}
```

**This is the bug that the other PR missed.** The XIP variant MUST
account for reloc table overflow into BSS, otherwise exact-fit
allocation can undersize the region.

### Step 2: Header-first allocation in exec

Currently, exec allocates a slot THEN reads the header. With variable
allocation, we need the header first to know how much to allocate:

```c
/* In do_exec / do_spawn: */
struct genix_header hdr;
int n = fs_read(ip, &hdr, 0, sizeof(hdr));
/* validate... */
uint32_t need = exec_mem_need(&hdr);  /* or _xip variant */
uint32_t base = umem_alloc(need);
if (base == 0) return -ENOMEM;
```

**Avoid double filesystem lookup.** The inode opened for the header
read should be passed through to load_binary/load_binary_xip, not
re-opened. This requires changing load_binary to accept an inode
instead of a path, or peeking at the header and passing the
pre-validated header through.

Simplest approach: add `exec_peek_header()` that reads and validates
the header, returns the inode (kept open). The caller allocates
memory, then passes the inode to load_binary. load_binary skips the
fs_namei() and header read if the inode is provided.

### Step 3: Remove mem_slot from struct proc

Replace `mem_slot` (int8_t, slot index) with nothing — `mem_base`
and `mem_size` already contain all the information needed. The slot
index was only used as the key for `slot_free()`.

### Step 4: Update sys_meminfo

Change the `meminfo` struct to report:
- Total user pool size
- Used bytes (sum of all proc mem_size values)
- Largest contiguous free region
- Per-process: pid, mem_base, mem_size

### Step 5: Tests

Replace the 8 slot tests in `test_mem.c` with:

- Basic allocation / deallocation
- Multiple allocations fill the pool
- Fragmentation: alloc A, B, C; free B; alloc D < B-size fits in gap
- Fragmentation: alloc A, B, C; free B; alloc D > B-size skips gap
- Exact-fit: allocate entire pool in one request
- Zero-size edge case
- Coalescing: free adjacent regions, re-allocate large block
- Stats accuracy
- Realistic Mega Drive scenario: dash + ls + more
- exec_mem_need and exec_mem_need_xip reloc table overlay

### Step 6: Documentation

- Update docs/memory-system.md (or create if missing)
- Update docs/decisions.md with rationale
- Add HISTORY.md entry
- Update PLAN.md Phase 6 outcome
- Update test-coverage.md

### Step 7: Verify

- `make test` — all host tests pass
- `make kernel` — cross-compilation clean
- `make test-emu` — workbench autotest passes
- `make megadrive` — Mega Drive build clean
- `make test-md-auto` — BlastEm autotest passes

## Kstack Concern

The proctab-scan allocator declares local arrays:

```c
uint32_t bases[16], sizes[16];  /* 128 bytes on stack */
```

Called from do_exec/do_spawn in syscall context where kstack = 512
bytes. The existing kstack usage in do_spawn is already tight. We
must verify that umem_alloc's stack frame + do_spawn's stack frame
fit within 512 bytes.

**Mitigation:** Make the arrays static. Since exec/spawn run with
interrupts off (no preemption during slot allocation), static arrays
are safe. This adds 128 bytes to kernel BSS but keeps kstack usage
constant.

Alternatively, use the proctab array directly instead of copying:

```c
/* Instead of copying to local arrays, scan proctab in-place.
 * Outer loop: find the lowest-base unvisited process.
 * This avoids ALL local array storage. O(n²) but n≤16. */
uint32_t cursor = USER_BASE;
for (int pass = 0; pass < nproc; pass++) {
    uint32_t lowest_base = USER_TOP;
    uint32_t lowest_size = 0;
    for (int i = 0; i < MAXPROC; i++) {
        if (proctab[i].state != P_FREE &&
            proctab[i].mem_base >= cursor &&
            proctab[i].mem_base < lowest_base) {
            lowest_base = proctab[i].mem_base;
            lowest_size = proctab[i].mem_size;
        }
    }
    if (lowest_base == USER_TOP) break;
    /* gap = [cursor, lowest_base) */
    if (lowest_base - cursor >= need) return cursor;
    cursor = lowest_base + lowest_size;
}
/* final gap = [cursor, USER_TOP) */
if (USER_TOP - cursor >= need) return cursor;
return 0;
```

This uses **zero local storage** beyond a few scalars (~20 bytes).
No kstack risk. O(n²) where n is the number of active processes,
but with n ≤ 16, the inner loop body runs at most 256 times total.
On a 7.67 MHz 68000, that's <0.1 ms.

This is the recommended implementation.

## Phase 7/8 Forward Compatibility

### SD card loaded programs (Phase 7)

SD-loaded binaries need runtime relocation (they're not in ROM).
They load into a RAM region — either main RAM or SRAM. The variable-
size allocator handles this: `umem_alloc(exec_mem_need(&hdr))` gives
them exactly the space they need.

If SRAM is available, we can add a second pool:

```c
static uint32_t SRAM_BASE, SRAM_TOP;

uint32_t sram_alloc(uint32_t need);  /* same algorithm, different pool */
```

SD-loaded programs that don't fit in main RAM can overflow to SRAM.
The allocator doesn't care whether the region is main RAM or SRAM —
it's just a contiguous address range.

### PSRAM text banks (Phase 8)

PSRAM banks are independent: each 512 KB bank holds one process's
text. The bank allocator is just:

```c
static uint8_t bank_used;  /* bitmask of 8 banks */

int psram_bank_alloc(void) {
    for (int i = 0; i < 8; i++)
        if (!(bank_used & (1 << i))) { bank_used |= (1 << i); return i; }
    return -1;
}
```

Data+bss still go to main RAM via `umem_alloc`. The two allocators
are completely orthogonal. Variable-size main RAM allocation is
compatible with (and arguably better for) PSRAM banking, because a
process with 512 KB of text in PSRAM needs only ~5 KB in main RAM
for data — variable allocation avoids wasting the rest of a fixed
13 KB slot.

### SRAM as process memory (Phase 7 Phase 2)

When loading programs into SRAM (no XIP — full text+data+bss), the
SRAM allocator can use either fixed or variable sizes. Fixed is
simpler and fine for 128-512 KB:

- 128 KB / 4 = 32 KB fixed slots (fits any current program)
- 512 KB / 8 = 64 KB fixed slots (very comfortable)

Variable-size is also fine for SRAM and may be worth it if we want
to maximize the number of concurrent SRAM processes. But that's a
Phase 7-8 decision — it doesn't affect the main RAM change we're
making now.

## Summary

| Aspect | Fixed slots (current) | Variable-size (proposed) |
|--------|----------------------|--------------------------|
| Pipelines | ❌ 2 slots → no `a\|b` | ✅ 3+ processes fit |
| Slot waste | 93% for small programs | ~0% |
| Max child size | 13.6 KB | ~16.6 KB |
| Fragmentation | None possible | Possible but rare |
| Allocator complexity | ~20 lines bitmap | ~25 lines scan |
| Phase 8 impact | None | None (data only, orthogonal to PSRAM) |
| Kstack safety | N/A | Zero-copy scan, ~20 B stack |

**Recommendation:** Replace the fixed-slot allocator with the proctab-
scanned first-fit allocator. It's simpler (fewer lines), more capable
(pipelines work), forward-compatible with Phase 7-8, and the
fragmentation risk is negligible for ≤3 concurrent processes with
LIFO exit patterns.

The critical implementation detail is the reloc-table overlay in
`exec_mem_need_xip` — without this, exact-fit XIP allocations can be
undersized, causing memory corruption during relocation.
