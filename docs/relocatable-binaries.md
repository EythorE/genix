# Relocatable Binaries for Genix

Research into relocatable binary support for Genix, covering relocation
formats, the FUZIX reference implementation, EverDrive Pro bank-swapping
as a future prospect, and a recommended approach.

**Date:** 2026-03-11

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Why Relocatable Binaries Matter](#why-relocatable-binaries-matter)
3. [Current Genix Binary Format](#current-genix-binary-format)
4. [FUZIX Relocation Reference](#fuzix-relocation-reference)
5. [Relocation Strategy Options](#relocation-strategy-options)
6. [EverDrive Pro Bank-Swapping](#everdrive-pro-bank-swapping)
7. [Recommended Approach](#recommended-approach)
8. [Implementation Roadmap](#implementation-roadmap)

---

## Executive Summary

Relocatable binaries would give Genix two major benefits:

1. **True multitasking** — today all processes load at the same fixed
   `USER_BASE`, so only one can be in memory. Relocation lets each
   process load at a different address.

2. **SD card program loading** — programs on an SD card don't know
   where free memory will be. Relocation lets the kernel load them
   at whatever address is available.

A third benefit — **unified binaries** — eliminates the need for
separate workbench and Mega Drive builds. A relocatable binary works
on both platforms from the same file.

The cost is modest: ~100 lines of kernel code for the relocation
engine, a small extension to the existing binary header, and a
toolchain change to emit relocation tables.

A future prospect is **EverDrive Pro bank-swapping**, which could
provide up to 512 KB of bankable SRAM. This influences the relocation
design: the format should support split text/data segments so that
code can live in a banked ROM/SRAM window while data stays in main RAM.

---

## Why Relocatable Binaries Matter

### The Problem Today

Genix currently uses **absolute flat binaries** linked at a fixed address:

- **Workbench:** `USER_BASE = 0x040000`
- **Mega Drive:** `USER_BASE = 0xFF9000`

Every user program is linked to one specific address. This creates three
problems:

**1. Only one process can occupy user memory at a time.**

When process A is loaded at `USER_BASE`, there's no way to also load
process B — it would need to go at the same address. The scheduler can
run multiple processes, but each new `exec()` overwrites the previous
program.

With relocation, each process can be loaded at whatever address the
allocator returns. On the workbench (704 KB user space), we could have
10+ small processes simultaneously. On the Mega Drive (27.5 KB user
space), 2-3 small processes. With SRAM, many more.

**2. Two separate builds are required.**

Programs must be compiled twice — once for workbench (`user.ld` at
0x040000) and once for Mega Drive (`user-md.ld` at 0xFF9000). A
relocatable binary works on both platforms from the same file.

**3. Programs from external storage can't be loaded.**

A program on an SD card doesn't know in advance where free memory will
be. Without relocation, it can only run if linked for exactly the right
address. With relocation, the kernel adjusts all absolute addresses at
load time.

---

## Current Genix Binary Format

### Header (32 bytes)

```c
struct genix_header {
    uint32_t magic;       /* 0x47454E58 "GENX" */
    uint32_t load_size;   /* bytes to copy (text+data) */
    uint32_t bss_size;    /* bytes to zero after load */
    uint32_t entry;       /* absolute entry point */
    uint32_t stack_size;  /* stack hint (0 = default 4 KB) */
    uint32_t flags;       /* reserved */
    uint32_t reserved[2]; /* pad to 32 bytes */
};
```

### Build Pipeline

```
.c -> .o (m68k-elf-gcc -m68000)
.o -> .elf (m68k-elf-ld -T user.ld)
.elf -> genix binary (tools/mkbin)
```

### Loading (`kernel/exec.c`)

1. Read 32-byte header, validate magic and sizes
2. Copy `load_size` bytes to `USER_BASE`
3. Zero `bss_size` bytes
4. Set up argc/argv on user stack at `USER_TOP`
5. Jump to `entry` (absolute address, always `USER_BASE + offset`)

### Limitations

- Entry point is absolute — program only works at one address
- No relocation data — binary cannot be moved
- Separate linker scripts for workbench vs Mega Drive
- All pointers in .data/.rodata are absolute (string tables,
  function pointers, switch jump tables)

---

## FUZIX Relocation Reference

FUZIX has a working relocatable binary implementation for the 68000.
Since we have the FUZIX source code and can recompile everything, we
don't need binary compatibility — but FUZIX's approach is a valuable
reference for how to do relocation on the 68000 simply and correctly.

### Relocation Table Format

Each relocation entry is a simple **4-byte big-endian offset** into the
loaded program. The offset points to a 32-bit word that contains an
absolute address which must be adjusted.

```
Relocation entry: uint32_t offset_from_text_base
```

There are no relocation types, symbol references, or GOT/PLT entries.
This is the simplest possible relocation format: a flat list of "patch
this 32-bit word."

### How Relocations Are Applied

The binary is compiled with a load address of 0. Every absolute address
reference (function pointers, global variable addresses, string
literals, jump table entries) starts as a zero-based offset.

At load time (`plt_relocate()` in FUZIX, ~50 lines of C):

1. Kernel decides where to load the program (`load_addr`)
2. For each relocation entry at offset `off`:
   - Determine whether the offset falls in text or data segment
   - Read the 32-bit value at that location
   - Determine whether the value references text or data
   - Add the appropriate segment base address
   - Write the adjusted value back

```c
/* Simplified pseudocode */
void relocate(uint8_t *base, uint32_t *relocs, int nrelocs,
              int32_t delta) {
    for (int i = 0; i < nrelocs; i++) {
        uint32_t off = relocs[i];
        uint32_t *ptr = (uint32_t *)(base + off);
        *ptr += delta;
    }
}
```

### FUZIX's Exec Integration

FUZIX has an elegant trick: the BSS is guaranteed to be at least as
large as the relocation table. During exec:

1. Load text segment into memory
2. Load data segment + relocation table (relocation entries land in
   what will become BSS)
3. Process all relocation entries in place
4. Zero the BSS (destroying the now-consumed relocation data)

This means relocation tables consume **zero extra RAM** — they
temporarily occupy BSS space that gets zeroed anyway.

### What Gets Relocated

The compiler generates absolute references for:

- **Global variable addresses** — `static int x; ... &x`
- **String literal pointers** — `const char *s = "hello"`
- **Function pointers** — `void (*fp)(void) = my_func`
- **Switch jump tables** — GCC's computed goto tables

PC-relative references (branches, short calls) do NOT need relocation.
On the 68000, `BSR`, `BRA`, and `Bcc` use PC-relative addressing and
don't appear in the relocation table.

### Performance and Size Cost

For a typical 4 KB program:
- ~50-100 relocation entries (200-400 bytes on disk)
- Each entry: one read + one add + one write = ~30 cycles
- Total: ~1,500-3,000 cycles = ~0.2-0.4 ms at 7.67 MHz
- **Negligible** compared to disk I/O

Relocation tables add ~5-10% to binary file size on disk but consume
zero RAM (loaded into BSS, processed, then BSS is zeroed).

### FUZIX's Split Text/Data Model

FUZIX supports `CONFIG_SPLIT_ID` (separate instruction and data
segments). With split segments:

- Code block (text) can be **shared** between processes running the
  same binary — loaded once, reference-counted
- Data block (data+bss+stack) is **per-process**
- The relocator handles cross-segment references (code referencing
  data and vice versa)

This is relevant for the EverDrive Pro bank-swapping scenario: if code
lives in a banked SRAM window and data in main RAM, the relocation
format must handle two different base addresses. FUZIX's format already
does this.

---

## Relocation Strategy Options

### Option 1: Extend Genix Header with Relocation Tables

**Format:** Keep the existing 32-byte Genix header, add relocation
fields using the reserved space, append relocation entries after data.

```c
struct genix_header {
    uint32_t magic;       /* 0x47454E58 "GENX" */
    uint32_t load_size;   /* text+data bytes to load */
    uint32_t bss_size;    /* bytes to zero */
    uint32_t entry;       /* entry offset (0-based when relocatable) */
    uint32_t stack_size;  /* stack hint */
    uint32_t flags;       /* bit 0: relocatable */
    uint32_t text_size;   /* text segment size (for split relocation) */
    uint32_t reloc_size;  /* relocation table size in bytes */
};
```

**Pros:**
- No format change — old binaries (flags=0) still work unmodified
- Simple, minimal change to existing toolchain
- `text_size` enables split text/data relocation (needed for banking)
- Fits in the existing 32-byte header (uses reserved fields)

**Cons:**
- Custom format (no existing ecosystem, but we're building everything
  from source anyway)

### Option 2: Adopt FUZIX a.out Format

**Format:** 64-byte a.out header as used by FUZIX.

**Pros:**
- Well-tested on 68000 hardware
- Standard Unix format

**Cons:**
- Larger header (64 vs 32 bytes)
- No benefit over extending our own header since we're recompiling
  everything from source anyway
- Two header formats in the kernel (GENX + a.out) adds complexity
  for no gain

### Option 3: Position-Independent Code (-fPIC)

**Format:** No relocations — all code uses PC-relative addressing.

**Pros:**
- No relocation tables, instant loading

**Cons:**
- 68000 PIC is expensive: every global access goes through a GOT
- Code size increases 15-30% due to indirect addressing
- Runtime overhead on every global variable access
- GCC's 68000 PIC support is poor (designed for 68020+)

**Verdict:** Wrong for 68000. The 68000's limited PC-relative modes
make PIC too expensive.

### Option 4: bFLT (Binary Flat Format, uClinux-style)

**Pros:**
- Used by uClinux on ColdFire (68000 family)

**Cons:**
- More complex than needed (multiple relocation types)
- Heavy tooling (elf2flt has many dependencies)
- Designed for ColdFire/ARM more than classic 68000
- No advantage over a simple relocation table

**Verdict:** Overkill.

---

## EverDrive Pro Bank-Swapping

### The Prospect

The EverDrive Pro provides up to 512 KB of word-wide SRAM in SSF
(Super Street Fighter) mapper mode. This SRAM is bank-switchable — the
68000 sees a window into a larger SRAM space, and software switches
which bank is visible by writing to mapper registers.

This could dramatically expand the memory available for processes:
instead of 27.5 KB of user space in main RAM, processes could use
banked SRAM for code or data.

### How Bank-Swapping Would Work

A plausible model:

1. **Code in banked SRAM** — each process's text segment lives in a
   different SRAM bank. The kernel switches the bank register before
   resuming a process.
2. **Data in main RAM** — data/bss/stack stay in the 64 KB main RAM
   (or in a fixed SRAM window) so that the kernel can always access
   process data without bank switching.
3. **Split relocation** — code references to data must be relocated
   with the data base address; data references to code must be
   relocated with the code base address. This is exactly the split
   text/data model that FUZIX already supports.

### Design Implications for Relocation

Bank-swapping means the relocation format **must support split
text/data segments** with different base addresses:

- Text segment base = SRAM bank window address (e.g., 0x200000)
- Data segment base = main RAM address (e.g., 0xFF9000)
- A pointer-to-function in .data needs the text base added
- A pointer-to-global in .text needs the data base added

The extended Genix header's `text_size` field enables this: the
relocator can determine whether each relocated value references text
or data based on whether the value is less than `text_size`.

### What This Means for the Recommendation

Even without implementing bank-swapping now, the relocation format
should be designed to support it:

- **Include `text_size` in the header** — needed for split relocation
- **The relocator should handle two base addresses** — even if they're
  the same initially (text and data loaded contiguously)
- **Don't assume contiguous text+data** — the relocator should work
  whether text and data are adjacent or separated

This adds minimal complexity now (one extra field in the header, a few
extra lines in the relocator) but avoids a format change later.

### Other EverDrive Pro Capabilities

Beyond bank-swapping, the Pro also provides:

- **SD card access** via FIFO command interface — load binaries from
  SD into RAM at whatever address is free
- **FAT filesystem** handled by the Pro's MCU — no FAT code in kernel
- **High throughput** — programs load in milliseconds

These capabilities combine well with relocatable binaries: load from
SD, relocate to available RAM (or banked SRAM), run.

---

## Recommended Approach

### Extend the Genix Header with Relocation Support

Since all our source code is available and it takes seconds to
recompile, there's no value in adopting a foreign binary format for
compatibility. Instead, extend the existing Genix format:

**Header change:**

```c
struct genix_header {
    uint32_t magic;       /* 0x47454E58 "GENX" */
    uint32_t load_size;   /* text+data bytes */
    uint32_t bss_size;    /* BSS bytes */
    uint32_t entry;       /* entry offset (0-based when relocatable) */
    uint32_t stack_size;  /* stack hint (0 = default 4 KB) */
    uint32_t flags;       /* bit 0: relocatable */
    uint32_t text_size;   /* text segment size (0 = no split) */
    uint32_t reloc_size;  /* relocation table size in bytes */
};
```

The `flags` field (currently reserved/zero) gets bit 0 as the
relocatable flag. Old binaries with `flags=0` work exactly as before.
The two reserved fields become `text_size` and `reloc_size`.

**Relocation format:** Simple array of uint32_t offsets appended after
the data segment (same approach as FUZIX). Each entry points to a
32-bit word that needs its base address added.

**Relocator (split-aware):**

```c
void relocate(uint8_t *text, uint8_t *data,
              uint32_t text_size, uint32_t data_size,
              uint32_t *relocs, int nrelocs) {
    for (int i = 0; i < nrelocs; i++) {
        uint32_t off = relocs[i];
        /* Locate the word to patch */
        uint32_t *ptr;
        if (off < text_size)
            ptr = (uint32_t *)(text + off);
        else
            ptr = (uint32_t *)(data + off - text_size);
        /* Determine what segment the value references */
        uint32_t val = *ptr;
        if (val < text_size)
            *ptr = val + (uint32_t)text;       /* references code */
        else
            *ptr = val - text_size + (uint32_t)data; /* references data */
    }
}
```

When `text_size == 0` (no split), text and data are contiguous and the
relocator simplifies to a single-base delta addition.

**Toolchain change:** Extend `mkbin` to extract ELF relocations and
emit them as the simple uint32_t offset array. No new tool needed.

### Why This Is Right

1. **Minimal change** — extends the existing format, no new headers or
   loaders. Old binaries keep working.

2. **Split-aware from day one** — `text_size` enables future
   bank-swapping where code and data have different base addresses.
   Costs nothing if unused (set to 0).

3. **No foreign format baggage** — we have all the source code, so
   binary compatibility with FUZIX (or anything else) is pointless.
   One format, one toolchain, one loader.

4. **BSS trick for zero RAM cost** — relocation entries are loaded
   into BSS space, processed, then BSS is zeroed. No extra RAM needed.

5. **Proven approach** — the relocation mechanism itself is identical
   to what FUZIX uses successfully on real 68000 hardware. We're just
   using our own header format instead of a.out.

---

## Implementation Roadmap

### Step 1: Extended Header + Relocation Engine

**Files to modify:**
- `kernel/exec.c` — detect `flags & 1`, load relocation entries into
  BSS, apply relocations, zero BSS
- `kernel/kernel.h` — update header struct (rename reserved fields)
- `tools/mkbin.c` — extract ELF relocations, emit relocation table,
  set flags and text_size/reloc_size

**Testing:**
- Host tests: validate relocation math with various segment layouts
- Workbench: load a relocatable hello-world at USER_BASE (same as
  today, delta=0, validates format without changing behavior)
- Then load at a different address to test actual relocation

### Step 2: Dynamic Load Address

**Files to modify:**
- `kernel/exec.c` — allocate memory via kmalloc instead of fixed
  USER_BASE, pass actual load address to relocator
- `kernel/proc.c` — per-process `mem_base` already exists

**Testing:**
- Load two programs at different addresses simultaneously
- Verify both can run and be scheduled

### Step 3: Split Text/Data Support

**Files to modify:**
- `tools/mkbin.c` — emit separate text_size
- `kernel/exec.c` — use split-aware relocator

This step is preparation for bank-swapping but also enables code
sharing (multiple processes running the same binary share one copy
of the text segment, each with their own data).

### Step 4: Port More Apps from FUZIX Source

Since all FUZIX source code is available, porting is just recompiling
against Genix's libc and build system:

1. Copy the `.c` file to `apps/`
2. Add to `PROGRAMS` in `apps/Makefile`
3. Fix any missing libc functions
4. Build and test

**Priority order:**
1. sed, sort — core text processing
2. diff, less — file viewing/comparison
3. cp, mv, rm — file management
4. sh (V7 Bourne shell) — real shell
5. ed, dc — classic Unix tools

### Step 5: SD Card Loading

Load binaries from EverDrive Pro/Open EverDrive SD card, relocate to
dynamically allocated RAM, run. Depends on the SD card driver work
(see `docs/everdrive-sd-card.md`).

### Step 6: Bank-Swapping (Future)

When EverDrive Pro bank-swapping is implemented:

- Load text segment into a banked SRAM window
- Load data segment into main RAM
- Use the split-aware relocator (already built in Step 3) to handle
  different text/data base addresses
- Switch SRAM bank on context switch

The relocation format designed in Step 1 already supports this — no
format change needed.

---

## Appendix: Size Estimates

| Component | Code Size | RAM Cost |
|-----------|----------|----------|
| Header field rename | ~5 lines | 0 |
| Relocation engine | ~30 lines | 0 (uses BSS) |
| Loader changes | ~50 lines | 0 |
| mkbin relocation extraction | ~100 lines | 0 (host tool) |
| **Total kernel addition** | **~80 lines** | **0 bytes** |
