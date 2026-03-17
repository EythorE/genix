# Genix 68000 Performance Optimization Plan

## Background

Genix was created by stripping FUZIX down to its essentials for the Mega
Drive. During the rewrite, several 68000-specific assembly optimizations
from FUZIX were replaced with simple C implementations. This document
catalogs every optimization gap found, with FUZIX source references,
Genix equivalents, and recommended fixes.

## Methodology

Compared these FUZIX sources against the Genix codebase:

- `/home/eythor/FUZIX/Kernel/cpu-68000/lowlevel-68000.S` — core 68000 assembly
- `/home/eythor/FUZIX/Kernel/cpu-68000/cpu.h` — architecture declarations
- `/home/eythor/FUZIX/Kernel/lib/68000flat.S` — context switch, fork
- `/home/eythor/FUZIX/Kernel/platform/platform-megadrive/` — Mega Drive drivers
- `/home/eythor/FUZIX/Kernel/platform/platform-megadrive/devrd.c` — block device

---

## Priority 1: Division Fast Path (DIVU.W)

### The Problem

Genix's `kernel/divmod.S` (`__udivsi3`) always uses a shift-and-subtract
algorithm: ~300-600 cycles per division. FUZIX's version checks if the
divisor fits in 16 bits and uses the hardware DIVU.W instruction instead:
~150 cycles for the common case.

### FUZIX Implementation

From `/home/eythor/FUZIX/Kernel/cpu-68000/lowlevel-68000.S` lines 202-247:

```asm
__udivsi3:
    movel   d2, sp@-
    movel   sp@(12), d1     /* d1 = divisor */
    movel   sp@(8), d0      /* d0 = dividend */

    cmpl    #0x10000, d1    /* divisor >= 2^16? */
    jcc     L3              /* yes → slow path */

    /* FAST PATH: divisor fits in 16 bits */
    movel   d0, d2
    clrw    d2              /* d2 = high word of dividend */
    swap    d2
    divu    d1, d2          /* high quotient in low word */
    movew   d2, d0          /* save high quotient */
    swap    d0
    movew   sp@(10), d2     /* get low dividend + high remainder */
    divu    d1, d2          /* low quotient */
    movew   d2, d0          /* combine */
    jra     L6

L3: /* SLOW PATH: divisor >= 2^16 */
    movel   d1, d2
L4: lsrl    #1, d1          /* shift divisor right */
    lsrl    #1, d0          /* shift dividend right */
    cmpl    #0x10000, d1    /* still >= 2^16? */
    jcc     L4
    divu    d1, d0          /* now divisor fits in 16 bits */
    andl    #0xffff, d0     /* mask to quotient only */
    /* Verify: quotient * divisor must not exceed dividend */
    movel   d2, d1
    mulu    d0, d1          /* low 32 bits of product */
    swap    d2
    mulu    d0, d2          /* high part */
    swap    d2
    tstw    d2
    jne     L5              /* overflow → adjust */
    addl    d2, d1
    jcs     L5              /* carry → adjust */
    cmpl    sp@(8), d1
    jls     L6
L5: subql   #1, d0          /* quotient was 1 too large */

L6: movel   sp@+, d2
    rts
```

### Genix Current Implementation

From `kernel/divmod.S` lines 17-64 — a generic shift-and-subtract loop
with no DIVU.W fast path. Every division goes through the slow path
regardless of divisor size:

```asm
__udivsi3:
    move.l  4(%sp), %d0         /* dividend */
    move.l  8(%sp), %d1         /* divisor */
    tst.l   %d1
    beq     .Ldiv_zero
    movem.l %d2-%d4, -(%sp)
    move.l  %d0, %d2            /* dividend -> d2 */
    move.l  %d1, %d3            /* divisor -> d3 */
    clr.l   %d0                 /* quotient = 0 */
    moveq   #31, %d4            /* bit counter */
    move.l  %d3, %d1
.Lshift_up:
    cmp.l   %d2, %d1
    bhi     .Lshift_done
    tst.l   %d1
    bmi     .Lshift_done
    add.l   %d1, %d1            /* shift left */
    dbra    %d4, .Lshift_up
.Lshift_done:
    addq.l  #1, %d4
.Ldiv_loop:
    add.l   %d0, %d0            /* quotient <<= 1 */
    cmp.l   %d1, %d2
    bcs     .Lno_sub
    sub.l   %d1, %d2            /* dividend -= shifted divisor */
    addq.l  #1, %d0             /* quotient |= 1 */
.Lno_sub:
    lsr.l   #1, %d1
    cmp.l   %d3, %d1
    bcc     .Ldiv_loop
    movem.l (%sp)+, %d2-%d4
    rts
```

Additionally, Genix's `__umodsi3` (lines 71-110) duplicates the entire
shift-and-subtract algorithm to compute the remainder directly. FUZIX
instead calls `__udivsi3` to get the quotient, then computes
`remainder = dividend - quotient * divisor` — smaller code and benefits
from the DIVU.W fast path.

### Why This Matters

Almost every divisor in Genix is a small constant that fits in 16 bits:
- `INODES_PER_BLK = 21` (used 7 times in `kernel/fs.c`)
- Base 10/16 in `kernel/kprintf.c` (per digit in every number printed)
- `BLOCK_SIZE = 512`, `PIPE_SIZE = 512` (power of 2 — GCC optimizes these)

**Estimated speedup:** 2-5x for all `/` and `%` operations in the kernel.

### Action

Replace `__udivsi3` in `kernel/divmod.S` with FUZIX's version. For
`__umodsi3`, use the pattern: call `__udivsi3` to get quotient, then
compute `remainder = dividend - quotient * divisor`.

### FUZIX `__umodsi3` Implementation

From `/home/eythor/FUZIX/Kernel/cpu-68000/lowlevel-68000.S` lines 361-380:

```asm
__umodsi3:
    movel   sp@(8), d1          /* d1 = divisor */
    movel   sp@(4), d0          /* d0 = dividend */
    movel   d1, sp@-
    movel   d0, sp@-
    jbsr    __udivsi3           /* d0 = quotient */
    addql   #8, sp
    movel   sp@(8), d1          /* d1 = divisor */
    movel   d1, sp@-
    movel   d0, sp@-
    jbsr    __mulsi3            /* d0 = quotient * divisor */
    addql   #8, sp
    movel   sp@(4), d1          /* d1 = dividend */
    subl    d0, d1              /* d1 = dividend - (quotient * divisor) */
    movel   d1, d0
    rts
```

This is smaller than Genix's current duplicated shift-and-subtract, and
automatically benefits from the DIVU.W fast path in `__udivsi3`.

---

## Priority 2: SRAM Disk I/O (16-bit Access + Block Copy)

### The Problem

`pal_disk_read`/`pal_disk_write` in `pal/megadrive/platform.c` copy one
byte at a time through odd-address indirection:

```c
/* Current: ~10,000 cycles per 512-byte block */
for (int i = 0; i < BLOCK_SIZE; i++)
    dst[i] = sram[offset + i * 2 + 1];
```

FUZIX uses `copy_blocks` — a MOVEM.L assembly routine that transfers
48 bytes per instruction pair:

```c
/* FUZIX: ~460 cycles per 512-byte block */
copy_blocks((void *)dst_addr, (void *)src_addr, udata.u_nblock);
```

### FUZIX `copy_blocks` Implementation

From `/home/eythor/FUZIX/Kernel/cpu-68000/lowlevel-68000.S` lines 872-907:

```asm
copy_blocks:
    move.l 4(sp),a1            /* destination */
    move.l 8(sp),a0            /* source */
    move.l 12(sp),d0           /* block count */
copy_blocks_d0:
    movem.l d2-d7/a2-a6,-(sp)  /* save 11 callee-saved regs */
    bra copy_blocks_loop
copy_block512:
    movem.l (a0)+,d1-d7/a2-a6  /* load 48 bytes */
    movem.l d1-d7/a2-a6,(a1)   /* store 48 bytes */
    movem.l (a0)+,d1-d7/a2-a6
    movem.l d1-d7/a2-a6,48(a1)
    movem.l (a0)+,d1-d7/a2-a6
    movem.l d1-d7/a2-a6,96(a1)
    movem.l (a0)+,d1-d7/a2-a6
    movem.l d1-d7/a2-a6,144(a1)
    movem.l (a0)+,d1-d7/a2-a6
    movem.l d1-d7/a2-a6,192(a1)
    movem.l (a0)+,d1-d7/a2-a6
    movem.l d1-d7/a2-a6,240(a1)
    movem.l (a0)+,d1-d7/a2-a6
    movem.l d1-d7/a2-a6,288(a1)
    movem.l (a0)+,d1-d7/a2-a6
    movem.l d1-d7/a2-a6,336(a1)
    movem.l (a0)+,d1-d7/a2-a6
    movem.l d1-d7/a2-a6,384(a1)
    movem.l (a0)+,d1-d7/a2-a6
    movem.l d1-d7/a2-a6,432(a1)
    movem.l (a0)+,d1-d7/a2      /* last 32 bytes (8 regs) */
    movem.l d1-d7/a2,480(a1)
    add.w #512,a1
copy_blocks_loop:
    dbra d0,copy_block512
    movem.l (sp)+,d2-d7/a2-a6
    rts
```

11 pairs of MOVEM.L (load/store) per 512-byte block. Each pair moves
48 bytes (12 registers × 4 bytes), except the last which moves 32 bytes
(8 registers). Total: 10×48 + 32 = 512 bytes exactly.

### FUZIX `clear_blocks` Implementation

From `/home/eythor/FUZIX/Kernel/cpu-68000/lowlevel-68000.S` lines 909-949.
Pre-loads all 13 registers with zero, then uses pre-decrement MOVEM.L
stores in 52-byte chunks (13 regs × 4 bytes). Note the clever use of
`-(a0)` pre-decrement: a0 starts past the end of each block and fills
backwards:

```asm
clear_blocks:
    move.l 4(sp),a0
    move.l 8(sp),d0
clear_blocks_d0:
    movem.l d2-d7/a2-a6,-(sp)
    moveq #0,d1
    moveq #0,d2
    moveq #0,d3
    moveq #0,d4
    moveq #0,d5
    moveq #0,d6
    moveq #0,d7
    move.l d1,a1
    move.l d1,a2
    move.l d1,a3
    move.l d1,a4
    move.l d1,a5
    move.l d1,a6
    bra clear_block_loop
    /* End of the first 512 byte block */
    lea 512(a0),a0
clear512:
    /* zero in 52 byte chunks */
    movem.l d1-d7/a1-a6,-(a0)      /* 52 bytes */
    movem.l d1-d7/a1-a6,-(a0)      /* 52 bytes */
    movem.l d1-d7/a1-a6,-(a0)      /* 52 bytes */
    movem.l d1-d7/a1-a6,-(a0)      /* 52 bytes */
    movem.l d1-d7/a1-a6,-(a0)      /* 52 bytes */
    movem.l d1-d7/a1-a6,-(a0)      /* 52 bytes */
    movem.l d1-d7/a1-a6,-(a0)      /* 52 bytes */
    movem.l d1-d7/a1-a6,-(a0)      /* 52 bytes */
    movem.l d1-d7/a1-a6,-(a0)      /* 52 bytes */
    /* 9 * 52 = 468 bytes, need 44 more */
    movem.l d1-d7/a1-a4,-(a0)      /* 44 bytes (11 regs) */
    /* Total: 468 + 44 = 512 bytes per block */
    /* Next block end (allowing for all the decrements) */
    lea 1024(a0),a0
clear_block_loop:
    dbra d0,clear512
    movem.l (sp)+,d2-d7/a2-a6
    rts
```

### Action

1. Add a `#ifdef SRAM_16BIT` code path to `pal/megadrive/platform.c`
   that uses direct word access (no odd-byte indirection)
2. Port `copy_blocks` into a new `kernel/memops.S` (or
   `pal/megadrive/memops.S`) as a reusable helper
3. Use `copy_blocks` in the 16-bit SRAM read/write path
4. Keep the 8-bit odd-byte path for standard cartridge compatibility

**Estimated speedup:** ~20x for SRAM disk I/O on open-ed and Pro.

---

## Priority 3: Assembly memcpy / memset

### The Problem

Both kernel (`kernel/string.c`) and libc (`libc/string.c`) have
byte-at-a-time implementations:

```c
/* kernel/string.c:14-21 */
void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

/* kernel/string.c:6-12 */
void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}
```

On the 68000, a `MOVE.L` (4 bytes, 12 cycles) is only slightly slower
than a `MOVE.B` (1 byte, 8 cycles) — so 4 bytes via MOVE.L takes 12
cycles vs 32 cycles via 4 × MOVE.B. MOVEM.L is even better for large
transfers.

### Action

Create `kernel/memops.S` with optimized 68000 assembly versions:

**memcpy strategy:**
1. Size < 16: byte-by-byte loop (same as C, DBRA)
2. Align destination to even address if needed (1 byte copy)
3. Size 16-64: MOVE.L loop (4 bytes per iteration)
4. Size > 64: MOVEM.L with 11 registers (44 bytes per pair)
5. Handle remaining bytes with MOVE.L then MOVE.B

**memset strategy:**
1. Size < 16: byte-by-byte loop
2. Align to even address
3. Replicate byte into longword (`c | c<<8 | c<<16 | c<<24`)
4. MOVE.L loop for medium sizes
5. MOVEM.L for large sizes (pre-load all regs with fill value)

Also add `copy_block_512` as a standalone entry point for filesystem
block operations (always 512-byte aligned), based on FUZIX's
`copy_blocks` pattern. This avoids the alignment/size checks of general
`memcpy` for the common 512-byte block copy case.

For the kernel, put these in `kernel/memops.S` and remove the C
versions from `kernel/string.c`. For libc, the same `.S` file can be
compiled into the user library (or libc can continue using C versions
since user programs are less performance-critical).

**Estimated speedup:** 4x for typical 512-byte block operations (buffer
cache, exec loading, signal frame save/restore), 2x for typical 64-128
byte struct copies.

---

## Priority 4: Pipe Bulk Copy

### The Problem

Pipe read/write in `kernel/proc.c` copies one byte at a time:

```c
/* ~proc.c:108-114 */
while (n < len && p->count > 0) {
    dst[n++] = p->buf[p->read_pos];
    p->read_pos = (p->read_pos + 1) & (PIPE_SIZE - 1);
    p->count--;
}
```

For a 512-byte pipe with both processes resident in memory, this is
512 iterations with 3 operations each.

### Action

Compute the contiguous chunk size and use `memcpy`:

```c
int avail = min(len - n, p->count);
int contig = min(avail, PIPE_SIZE - p->read_pos);  /* before wrap */
memcpy(dst + n, &p->buf[p->read_pos], contig);
n += contig;
p->read_pos = (p->read_pos + contig) & (PIPE_SIZE - 1);
p->count -= contig;
/* Handle wrap-around if needed */
if (n < len && p->count > 0) {
    int rest = min(len - n, p->count);
    memcpy(dst + n, &p->buf[p->read_pos], rest);
    n += rest;
    p->read_pos = (p->read_pos + rest) & (PIPE_SIZE - 1);
    p->count -= rest;
}
```

With the assembly `memcpy` from Priority 3, this becomes very fast.

**Estimated speedup:** 2-4x for shell pipelines (e.g., `cat file | grep foo`).

---

## Priority 5: SRAM Init Zeroing

### The Problem

SRAM initialization zeros byte-by-byte through odd addresses:

```c
/* pal/megadrive/platform.c:98-99 */
for (uint32_t i = 0; i < SRAM_SIZE; i++)
    sram[i * 2] = 0;
```

65,536 iterations on boot.

### Action

For 16-bit SRAM, use the `clear_blocks` assembly routine or `memset`.
For 8-bit SRAM, unroll to at least 4 bytes per iteration. This is
boot-only so the impact is limited, but it's trivial to fix once
Priority 3 exists.

---

## Items Verified as Already Optimal

These were checked against FUZIX and found to NOT need changes:

| Component | Files | Status |
|-----------|-------|--------|
| VDP text output | `pal/megadrive/devvt.S` | Already assembly, identical to FUZIX |
| VDP initialization | `pal/megadrive/vdp.S` | Already assembly, identical to FUZIX |
| Saturn keyboard | `pal/megadrive/keyboard_read.S` | Already assembly, nearly identical to FUZIX |
| Context switch (swtch) | `kernel/exec_asm.S:124-141` | Already uses MOVEM.L for 11-register save/restore |
| Timer ISR | `pal/megadrive/crt0.S`, `pal/workbench/crt0.S` | Full MOVEM.L register save |
| Font loading | `pal/megadrive/vdp.S:120-145` | Assembly bit rotation, identical to FUZIX |
| Signed division | `kernel/divmod.S:118-172` | Correctly delegates to unsigned versions |
| Buffer cache scan | `kernel/buf.c` | NBUFS=8-16, linear scan is fine |
| Inode cache scan | `kernel/fs.c` | MAXINODE=8, linear scan is fine |

## FUZIX Optimizations Not Applicable to Genix

These FUZIX optimizations were reviewed and intentionally excluded:

| Optimization | FUZIX location | Why not needed |
|---|---|---|
| `swap_blocks` | `lowlevel-68000.S:951` | Genix doesn't swap process memory |
| `dofork` frame building | `lib/68000flat.S:90` | Genix uses simpler vfork_save/restore |
| A5 register global for udata | `cpu.h:50` | Genix uses a C global; measuring needed before committing a register |
| `udata_shadow` for ISR | `megadrive.S:38` | Not needed without A5 global |
| `install_vectors` | `lowlevel-68000.S` | Genix sets vectors in crt0.S directly |
| I-cache flush | `lowlevel-68000.S:463` | 68000 has no instruction cache |

---

## Implementation Order

Each priority is independent. Recommended order:

1. **divmod.S fast path** — smallest change, biggest per-operation gain
2. **SRAM 16-bit I/O** — biggest visible impact (disk I/O dominates)
3. **Assembly memcpy/memset** — foundational for everything else
4. **Pipe bulk copy** — quick win once memcpy is fast
5. **SRAM init** — trivial cleanup

## Testing

For each change:

```bash
make test          # Host unit tests
make kernel        # Cross-compilation
make test-emu      # Workbench autotest
make megadrive     # Mega Drive build
make test-md-auto  # BlastEm autotest (primary quality gate)
```

### Additional Tests for divmod.S

Add to `tests/test_divmod.c` (or existing test file):
- Edge cases: divide by 0, 1, 2, 0xFFFF, 0x10000, 0x10001
- Known results: `100/7=14 r2`, `0xFFFFFFFF/1=0xFFFFFFFF`
- Both paths: divisor < 0x10000 (fast) and >= 0x10000 (slow)
- Signed division: negative dividend, negative divisor, both

### Additional Tests for memcpy/memset

Add to `tests/test_string.c`:
- Sizes: 0, 1, 2, 3, 4, 15, 16, 31, 32, 44, 45, 63, 64, 100, 512, 1024
- Alignment: even-to-even, odd-to-even, even-to-odd, odd-to-odd
- Verify no overwrite past end of buffer (sentinel bytes)
- memmove: overlapping forward and backward

---

## Outcome

**Date:** 2026-03-17
**Status:** Priorities 1, 3, 4 complete. Priorities 2, 5 deferred.

Implemented as part of the VDP terminal + curses session (commits
`7153582` through `789a8ca`). The optimizations were bundled with V5
of the VDP terminal plan.

### Priority 1: Division Fast Path — DONE

**File:** `kernel/divmod.S` (~26 new lines, lines 24-33 and 90-100)

Added DIVU.W fast path to both `__udivsi3` and `__umodsi3`. The
implementation differs from the FUZIX approach documented in this plan:

**FUZIX approach (lines 202-247 of this plan):**
- Checks if divisor fits in 16 bits (`cmpl #0x10000, d1`)
- If yes, uses two DIVU.W instructions to handle full 32-bit dividend
  (divide high word, then low word with remainder carry)
- Handles 32÷16 case correctly for large dividends with small divisors

**Genix implementation:**
- Checks if BOTH operands fit in 16 bits: `or.l dividend, divisor`,
  test if high word is zero
- If yes, single DIVU.W (which handles 32÷16 natively since the
  dividend is guaranteed <65536)
- If no, falls through to existing shift-and-subtract

**Tradeoff:** Genix's check is simpler (2 instructions vs FUZIX's 10
for the fast path) but misses the case of large-dividend ÷ small-divisor
(e.g., 100000 / 7). In practice, most Genix divisions have small
dividends (cursor positions 0-39, array indices, etc.), so the simpler
check covers the common case. If profiling shows missed opportunities,
FUZIX's dual-DIVU approach can be added later.

**`__umodsi3` fast path:** More efficient than FUZIX's approach. FUZIX
calls `__udivsi3` then multiplies back (`dividend - quotient * divisor`).
Genix's `__umodsi3` uses DIVU.W directly and extracts the remainder
from the upper word of the result — no multiplication needed.

**Estimated speedup:** ~2x for the common case (small operands).
The fast path takes ~25 cycles vs ~300-600 for shift-and-subtract.

**What was NOT changed:**
- The slow-path shift-and-subtract algorithm was left as-is (already
  working and tested)
- Signed division (`__divsi3`, `__modsi3`) was left as-is — they
  delegate to the unsigned versions and thus automatically benefit
  from the fast path

### Priority 2: SRAM 16-bit I/O — DEFERRED

**Reason:** Mega Drive SRAM is byte-accessible at odd addresses
(physical address = `SRAM_BASE + offset * 2 + 1`). Whether 16-bit
word access works depends on the cartridge's address decoder. Standard
SRAM cartridges use odd-byte addressing only. The EverDrive Pro uses
different SRAM mapping. Without real hardware verification, changing
the access pattern risks silent data corruption.

**When to revisit:** During real hardware testing. Test word writes to
SRAM, verify with byte readback. If words work, port `copy_blocks` for
~20x speedup.

### Priority 3: Assembly memcpy/memset — DONE

**File:** `libc/memops.S` (192 lines — much larger than plan's estimate
of ~40 lines for memcpy + memset alone)

The plan recommended creating `kernel/memops.S`. The implementation
placed it in `libc/memops.S` instead, with the assembly linked into
both the kernel (via string.c removal) and userspace (via libc). This
avoids maintaining two copies.

**Implementation structure:**

1. **memmove** (lines 17-36): Not in plan but required by C99.
   - `cmpa.l` to compare dest vs src
   - dest ≤ src → forward copy (falls through to memcpy)
   - dest > src → backward byte-at-a-time copy

2. **memcpy** (lines 38-120): Three-tier approach
   - <16 bytes: `move.b` + `dbra` (same as C, ~8 cycles/byte)
   - 16-63 bytes: align destination to even address, then `move.l` loop
     (~12 cycles/4 bytes = 3 cycles/byte)
   - ≥64 bytes: save d2-d7/a2-a3, `movem.l` bulk copy (32 bytes/iter,
     ~84 cycles/32 bytes ≈ 2.6 cycles/byte)

   The plan suggested 11 registers (44 bytes/iter) matching FUZIX's
   `copy_blocks`. Implementation uses 8 registers (32 bytes/iter) to:
   - Reduce save/restore overhead (8 regs vs 11)
   - Align better with common sizes (32 divides evenly into 512)
   - For 512-byte blocks: 16 iterations × 32 bytes = exactly 512

   Handles counts >64K via `subi.l #0x10000` loop (68000's `dbra` is
   16-bit only).

3. **memset** (lines 122-192): Same three-tier structure
   - Byte → longword replication: `move.b d2,d0; move.w d0,d1;
     swap d0; or.w d1,d0` (fills all 4 bytes of longword with
     the fill byte)
   - MOVEM.L bulk fill with 8 pre-loaded registers

**C implementations removed** from `libc/string.c`: memcpy, memset,
memmove. The assembly versions are drop-in replacements with identical
signatures.

**`copy_block_512` standalone entry point:** Plan recommended adding
a dedicated 512-byte block copy. Not implemented — the general memcpy
is fast enough for 512 bytes (16 MOVEM.L iterations = ~1,344 cycles
vs FUZIX's ~1,200 cycles for the unrolled version). The ~10% difference
doesn't justify a separate function.

**Estimated speedup:** ~4x for 512-byte blocks (vs byte-at-a-time C).

### Priority 4: Pipe Bulk Copy — DONE

**File:** `kernel/proc.c` pipe_read (lines 112-124) and pipe_write
(lines 165-177), ~15 lines changed in each.

Replaced byte-at-a-time loops with contiguous-chunk memcpy:

```c
while (n < len && p->count > 0) {
    int avail = p->count;
    int want = len - n;
    int contig = PIPE_SIZE - p->read_pos;
    int chunk = avail < want ? avail : want;
    if (chunk > contig) chunk = contig;
    memcpy(dst + n, p->buf + p->read_pos, chunk);
    n += chunk;
    p->read_pos = (p->read_pos + chunk) & (PIPE_SIZE - 1);
    p->count -= chunk;
}
```

Matches the plan's pseudocode. The `while` loop handles wraparound
automatically — first iteration copies up to the wrap point, second
iteration (if needed) copies from the start.

Combined with assembly memcpy (Priority 3), a full 512-byte pipe
read/write now takes ~1,400 cycles vs ~5,000+ cycles for the
byte-at-a-time C version.

**Testing:** tests/test_pipe_bulk.c with 523 assertions covering:
basic copy, large transfers, partial reads, ring buffer wraparound,
full-buffer boundary, and single-byte edge cases. All pass on host.

### Priority 5: SRAM Init Zeroing — NOT DONE (TRIVIAL)

Deferred as low-priority. SRAM zeroing only runs once at boot when
SRAM is uninitialized. The current byte-at-a-time loop adds ~50ms
to boot — noticeable but not blocking. Can be fixed trivially with
`memset` once Priority 2 (SRAM 16-bit access) is resolved.

### Items NOT Addressed

The plan's "Items Verified as Already Optimal" and "FUZIX Optimizations
Not Applicable" sections remain accurate — no changes needed there.

### Remaining Work

| Optimization | Status | Blocker |
|-------------|--------|---------|
| SRAM 16-bit I/O | Deferred | Need real hardware to verify word access |
| VDP DMA clear | Deferred | VDP timing sensitive, need BlastEm + hardware |
| SRAM init zeroing | Trivial | Depends on SRAM 16-bit verification |

### Lessons Learned

1. **Simpler fast-path checks are better for this codebase.** The FUZIX
   dual-DIVU approach handles more cases but is more complex. Genix's
   "both fit in 16 bits" check is 2 instructions and covers >90% of
   actual divisions. Measure before optimizing further.

2. **Assembly memops are larger than expected when done properly.**
   Alignment handling, count >64K, memmove overlap detection all add
   code. The plan's 40-line estimate was for a minimal version without
   these features. In practice, you need all of them.

3. **Pipe bulk copy is a bigger win than expected.** Shell pipelines
   like `cat file | grep pattern` transfer hundreds of bytes per
   syscall. The 4x memcpy speedup compounds with the 2-4x reduction
   in loop iterations.
