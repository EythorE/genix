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
5. [Background: PIC, GOT, and XIP](#background-pic-got-and-xip)
6. [bFLT: What It Does and What's Worth Stealing](#bflt-what-it-does-and-whats-worth-stealing)
7. [ROM Execute-in-Place Strategies](#rom-execute-in-place-strategies)
8. [Relocation Strategy Options](#relocation-strategy-options)
9. [EverDrive Pro Bank-Swapping](#everdrive-pro-bank-swapping)
10. [Recommended Approach](#recommended-approach)
11. [Implementation Roadmap](#implementation-roadmap)

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

A fourth benefit — **ROM Execute-in-Place (XIP)** — is potentially
transformative for the Mega Drive. Currently exec() copies the entire
binary from ROM to RAM, consuming precious user memory (~27.5 KB) with
code that could execute directly from ROM. With XIP, only the data
segment goes to RAM. A typical program with 4 KB text and 2 KB data
would use 2 KB RAM instead of 6 KB. This document analyzes three
XIP strategies: build-time address resolution (zero runtime cost),
kernel-linked app overlays (simplest implementation), and bFLT-style
GOT/PIC (most flexible but expensive on the 68000).

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

## Background: PIC, GOT, and XIP

These concepts come from the no-MMU embedded world. Understanding them
is essential for evaluating the relocation and ROM execution options below.

### PIC (Position-Independent Code)

PIC is code that works correctly regardless of where it's loaded in memory.
Instead of using absolute addresses, it uses relative references:

- **Code-to-code**: PC-relative branches (`BSR`, `BRA`). The 68000 has
  these natively — `BSR label` encodes the displacement from the current
  PC, so it works at any address. GCC already uses BSR for local function
  calls.

- **Code-to-data**: This is the hard part. A `move.l #global_var, a0`
  instruction encodes an absolute address. PIC replaces this with an
  indirect access through a **Global Offset Table (GOT)** — a table of
  pointers in the data segment. Code loads the GOT base into a register
  and accesses globals through it:

  ```
  ; Without PIC (absolute):
  move.l  #my_global, a0        ; absolute address baked into instruction

  ; With PIC (GOT-relative):
  move.l  (GOT_OFFSET,a5), a0   ; load address from GOT via base register
  ```

  On architectures with PC-relative data loads (x86-64, AArch64), PIC is
  nearly free. The 68000 has **no PC-relative data load** — it can only do
  PC-relative branches (BSR/BRA/Bcc) and PC-relative addressing for LEA
  (but with a 16-bit signed displacement limit). So on the 68000, PIC
  requires dedicating an address register (typically a5) as the GOT base
  and adding an extra indirection on every global access.

**Cost on the 68000:**
- One address register permanently occupied (a5 → GOT base)
- Every global access: `move.l (offset,a5),aN` + `move.l (aN),...`
  instead of `move.l #addr,...` — ~4 extra cycles per access
- Code size increase: 15-30% (GOT entries + indirect loads)
- GCC's m68k PIC support is designed for 68020+ (which has 32-bit
  displacements); 68000 is limited to 16-bit displacements, so GOT
  must be within ±32 KB of the base register

### GOT (Global Offset Table)

The GOT is a table of absolute addresses, one per global variable or
function that needs its address taken. It lives in the **data segment**
(RAM), so it can be patched at load time. Code accesses globals
indirectly through the GOT:

```
GOT (in .data, RAM):
  [0]  &printf      = 0x000428    ← actual ROM address
  [4]  &my_global   = 0xFF9010    ← actual RAM address
  [8]  &stderr      = 0xFF9020    ← actual RAM address
  ...

Code (in .text, ROM):
  lea   _GLOBAL_OFFSET_TABLE_(%pc), a5   ; load GOT base
  move.l (8,a5), a0                      ; a0 = &stderr (from GOT)
  move.l (a0), a1                        ; a1 = stderr value
```

The key property: **only the GOT needs patching at load time**. The
code segment contains no absolute addresses (only PC-relative
references to the GOT), so it can live in ROM untouched.

bFLT exploits this: with `FLAT_FLAG_GOTPIC`, the GOT is at the start
of the data segment. The loader patches only the GOT entries, not the
text. Text stays in ROM — this is how bFLT achieves XIP.

### XIP (Execute in Place)

XIP means executing code directly from ROM without copying it to RAM.
Only the data segment (globals, BSS, stack) goes into RAM.

**Why XIP matters for the Mega Drive:**

| Metric | Without XIP | With XIP |
|--------|------------|----------|
| RAM per program | text + data + BSS + stack | data + BSS + stack only |
| Typical 6 KB program (4 KB text, 2 KB data) | 6 KB RAM | 2 KB RAM |
| Max programs in 27.5 KB | ~4 small | ~12 small |
| Max text size | ~27.5 KB (RAM limit) | ~4 MB (ROM limit) |
| exec() speed | Copy entire binary to RAM | Copy only .data to RAM |
| ROM speed penalty | N/A | None (68000 ROM = same speed as RAM) |

XIP is transformative for the Mega Drive: it effectively makes text
segment size free (it's already in ROM) and reclaims that RAM for data.

**The XIP problem on the 68000:** How do you handle text→data references
(instructions that load the address of a global variable) when text is
in ROM and can't be patched? Three solutions exist:

1. **GOT/PIC** (bFLT approach) — indirect all data access through a table
   in RAM. Works but expensive on 68000 (~15-30% code bloat).

2. **Build-time resolution** — if the data address is known at build time
   (e.g., always USER_BASE), resolve all addresses when building the ROM.
   No runtime patching needed. Zero overhead. Single-tasking only.

3. **Copy text to writable memory** — copy text to SRAM, patch it there.
   This is the bank-swapping approach. Text isn't in ROM anymore, but it's
   out of precious main RAM.

---

## bFLT: What It Does and What's Worth Stealing

### Format Overview

bFLT (Binary Flat) is the standard executable format for uClinux
(no-MMU Linux). Supported targets include m68k/ColdFire, ARM, SuperH,
and others.

**Header (64 bytes):**

```c
struct flat_hdr {
    char     magic[4];      /* "bFLT" */
    uint32_t rev;           /* format version (2 or 4) */
    uint32_t entry;         /* offset of first instruction */
    uint32_t data_start;    /* offset of data segment */
    uint32_t data_end;      /* end of data */
    uint32_t bss_end;       /* end of BSS */
    uint32_t stack_size;    /* minimum stack size */
    uint32_t reloc_start;   /* offset of relocation table */
    uint32_t reloc_count;   /* number of relocation entries */
    uint32_t flags;         /* RAM/GOTPIC/GZIP/GZDATA/KTRACE */
    uint32_t build_date;
    uint32_t filler[5];     /* reserved */
};
```

All fields are big-endian (network byte order).

**Flags:**

| Flag | Value | Meaning |
|------|-------|---------|
| `FLAT_FLAG_RAM` | 0x0001 | Force-load entire binary into RAM |
| `FLAT_FLAG_GOTPIC` | 0x0002 | PIC with GOT — enables XIP |
| `FLAT_FLAG_GZIP` | 0x0004 | Everything after header is gzipped |
| `FLAT_FLAG_GZDATA` | 0x0008 | Only data+relocs gzipped (text stays uncompressed for XIP) |

**Version 4 relocation format:** Each entry is a uint32_t offset to a
word that needs patching. The loader uses `data_start - entry` (text
size) to determine whether a value references text or data. This is
identical to what we've already designed for Genix.

### How bFLT Achieves XIP

With `FLAT_FLAG_GOTPIC` set and compression disabled:

1. Text segment stays in ROM (flash, or the bFLT file in a romfs)
2. Data segment (starting with the GOT) is copied to RAM
3. The loader walks the GOT entries and adds the actual text/data base
   addresses to each entry
4. Code accesses all globals through the GOT (using a dedicated register)
5. Result: text is never modified, executes directly from ROM

**What's genuinely clever:**
- `FLAT_FLAG_GZDATA` compresses only data+relocs, leaving text
  uncompressed. This means XIP works even with compressed binaries —
  decompress data to RAM, leave text in ROM.
- The GOT is at the start of the data segment, so the loader knows
  exactly where it is. GOT entries are terminated by -1.
- Multiple instances of the same program share one text copy in ROM.

### What's Worth Stealing vs. What Isn't

| bFLT idea | Worth it for Genix? | Why |
|-----------|-------------------|-----|
| XIP concept (text in ROM, data in RAM) | **Yes** | Transformative for 27.5 KB RAM |
| GOT/PIC mechanism | **No** | 15-30% code bloat, poor 68000 support |
| Compression | **Maybe later** | Saves ROM space but adds kernel complexity |
| Version 4 relocation format | **Already adopted** | Our reloc format is identical |
| Separate text/data sizes in header | **Already adopted** | `text_size` field |
| `data_start`/`data_end`/`bss_end` offsets | **Useful pattern** | Better than load_size/bss_size? |

**The philosophical insight from bFLT:** Text should be separable from
data so text can live somewhere cheap (ROM, shared memory, banked SRAM)
while data lives in precious RAM. This is worth adopting. The GOT/PIC
mechanism is just bFLT's answer to *how* — and it's the wrong answer for
the 68000 due to the ISA's poor PC-relative data access.

We can achieve the same goal (XIP from ROM) without PIC, by resolving
addresses at build time instead of at load time. See the ROM XIP
strategies below.

---

## ROM Execute-in-Place Strategies

The Mega Drive romdisk lives in cartridge ROM. Every binary in `/bin`
already sits at a known ROM address — we just don't exploit this.
Currently, `exec()` copies the entire binary from ROM to RAM, wasting
precious user memory on text that could execute directly from ROM.

Three strategies can put text back in ROM:

### Strategy A: Build-Time Resolved XIP

**Concept:** Compile apps with base address 0. After mkfs places text
in ROM, a post-processing tool resolves all absolute addresses using the
known ROM offset and the known data address (USER_BASE). The result is a
fully-resolved binary in ROM — zero runtime relocation.

**How it works:**

1. Compile each app with `-Ttext=0 -Tdata=0`, emitting relocation tables
   (mkbin already planned to do this)
2. mkfs creates the filesystem, placing each binary's text+rodata at a
   specific ROM offset
3. After the final kernel+romdisk link, ROM addresses are known. A
   post-link tool (`romfix` or extended mkbin) processes each binary:
   ```
   For each relocation entry at offset `off`:
     value = read32(off)
     if value < text_size:
       write32(off, value + ROM_ADDR)      # text reference → ROM
     else:
       write32(off, value - text_size + USER_BASE)  # data reference → RAM
   ```
4. Write the fully-resolved binary back into the ROM image
5. At exec() time: copy `.data` init from ROM to USER_BASE, zero BSS,
   set up stack, JMP to ROM entry. No relocation engine needed.

**The key insight:** We control the build. We know where text goes in
ROM (determined by mkfs + linker). We know where data goes in RAM
(USER_BASE, a compile-time constant). So ALL addresses are known at
build time. Runtime relocation is unnecessary.

**Pros:**
- True XIP from ROM — zero runtime relocation cost
- No PIC overhead — code is identical to today's absolute binaries
- exec() only copies .data (typically 10-30% of binary), not the full text
- Programs can have arbitrarily large text (limited by ROM, not RAM)
- ROM access is the same speed as RAM on the Mega Drive

**Cons:**
- Multi-pass build (compile → mkfs → link → fixup)
- Data address is fixed at build time — single-tasking only, or requires
  fixed data partitions per process for multitasking
- Separate ROMs for workbench vs Mega Drive (different USER_BASE) — but
  we already have this

**The data address problem:** text→data references are encoded as
immediate values in instructions (`move.l #my_global, a0`). These
instructions are in ROM and can't be patched at load time. So the data
address must be known when the ROM is built. For single-tasking (one
process at a time, always at USER_BASE), this is fine. For multitasking,
text→data references would need to point to different addresses for
different processes — impossible with ROM text unless you use a GOT.

**Multitasking escape hatches:**
- Bank-switch text to SRAM → text becomes writable → can relocate at load time
- Partition RAM into fixed per-process data regions → data address known per slot
- Use the GOT/PIC approach for multitasking-capable programs (accept the overhead)

### Strategy B: Kernel-Linked Apps (Overlay XIP)

**Concept:** Link app `.text` directly into the kernel ROM. Use linker
OVERLAY sections so all apps' `.data` maps to USER_BASE. The linker
resolves everything in one step — no post-processing needed.

**How it works:**

1. Compile each app+libc to a self-contained `.o` (or archive `.a`)
2. The kernel linker script includes all app objects:

   ```ld
   /* App text goes into ROM alongside kernel text */
   .app_text : {
       _app_hello_text = .;
       apps/hello.o(.text .text.* .rodata .rodata.*)
       _app_hello_text_end = .;

       _app_cat_text = .;
       apps/cat.o(.text .text.* .rodata .rodata.*)
       _app_cat_text_end = .;
       /* ... */
   } > rom

   /* App data uses OVERLAY — all apps share the same RAM address */
   OVERLAY USER_BASE : AT(_app_data_load) {
       .hello_data {
           _app_hello_data_load = LOADADDR(.hello_data);
           apps/hello.o(.data .data.*)
           _app_hello_bss_start = .;
           apps/hello.o(.bss .bss.* COMMON)
           _app_hello_end = .;
       }
       .cat_data {
           _app_cat_data_load = LOADADDR(.cat_data);
           apps/cat.o(.data .data.*)
           _app_cat_bss_start = .;
           apps/cat.o(.bss .bss.* COMMON)
           _app_cat_end = .;
       }
       /* ... */
   } > ram
   ```

3. The linker resolves all addresses: text→text gets ROM addresses,
   text→data gets USER_BASE addresses, data→text gets ROM addresses.
4. The filesystem has entries that reference ROM metadata:
   - Text entry point (ROM address)
   - Data initializer location (ROM address, for copying to RAM)
   - Data size, BSS size
5. exec() implementation:
   ```c
   /* Look up app metadata from filesystem/ROM table */
   memcpy(USER_BASE, app->data_load_addr, app->data_size);
   memset(USER_BASE + app->data_size, 0, app->bss_size);
   setup_stack(USER_TOP, path, argv);
   jmp(app->entry);  /* entry is a ROM address */
   ```

**The libc problem:** Each app is statically linked with libc. If we
link all apps into one binary, they'd share libc `.text` (saves ROM!)
but each OVERLAY section needs its own copy of libc `.data`. This
requires compiling each app as a fully-linked relocatable object
(`ld -r -o app_hello_linked.o hello.o libc.a`) before including it in
the kernel link.

**Pros:**
- Simplest path to XIP — the linker does all the work
- No relocation engine, no post-processing tools
- Zero runtime overhead
- Apps share libc text in ROM (one copy of printf, etc.)
- Filesystem entries are just ROM address references
- exec() is trivial: memcpy data, zero BSS, jump

**Cons:**
- Adding/removing apps requires relinking the kernel
- Build system becomes more complex (overlay linker script)
- Can't load programs from SD card (not in the ROM)
- Single-tasking only (same USER_BASE constraint as Strategy A)
- Each app's libc data is a separate overlay (linker script grows with app count)
- Not suitable for dynamically loaded programs

**Best for:** A fixed set of built-in apps that ship with the ROM.
Think of it as the ROM equivalent of busybox — a single binary containing
all tools, with the filesystem providing the command names.

### Strategy C: bFLT-Style GOT/PIC

**Concept:** Compile with `-fPIC`, use a GOT in the data segment.
Text executes from ROM, GOT is patched at load time.

Already analyzed above in the PIC section. The 15-30% code bloat and
poor 68000 support make this the wrong choice unless ROM XIP is needed
for dynamically-loaded programs with multitasking (no fixed data address).

### Strategy Comparison

| Property | A: Build-Time XIP | B: Kernel-Linked | C: GOT/PIC |
|----------|-------------------|------------------|-------------|
| Runtime relocation | None | None | GOT patching |
| Code size overhead | 0% | 0% (shared libc!) | 15-30% |
| Runtime data access cost | 0 | 0 | ~4 cycles/access |
| Build complexity | Medium (post-link fixup) | Medium (overlay LD) | Low (just -fPIC) |
| Add apps without rebuild | Yes (re-run mkfs+fixup) | No (relink kernel) | Yes |
| SD card programs | Yes (with runtime reloc fallback) | No | Yes |
| Multitasking XIP | No (fixed data addr) | No (fixed data addr) | Yes |
| Shared libc text in ROM | No (per-app copy) | Yes | No (per-app copy) |

### Recommended ROM XIP Path

**Phase 1 (now):** Strategy B (kernel-linked) for built-in apps. This
gives XIP with zero runtime cost and the simplest exec() possible.
The 34 apps currently in `/bin` become ROM-resident. This is the
biggest win for the least effort — all addresses resolved by the linker.

**Phase 2 (with relocatable binaries):** Strategy A (build-time XIP)
for the ROM image, with the existing runtime relocator as fallback for
programs loaded from SD card or SRAM. ROM apps get XIP performance,
SD-loaded apps get runtime relocation.

**Phase 3 (multitasking with SRAM):** Bank-switch text to SRAM, relocate
data per-process. No PIC needed — SRAM is writable, so the runtime
relocator can patch text→data references. This combines the best of
all worlds: text out of main RAM, per-process data, no code bloat.

GOT/PIC (Strategy C) remains available as a last resort for scenarios
where ROM XIP + multitasking is needed without SRAM, but this scenario
is unlikely — if you need multitasking, you probably have SRAM.

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

**Format:** Code compiled with `-fPIC`. All global accesses go through
a GOT (see "Background: PIC, GOT, and XIP" section above for how this
works on the 68000).

**Pros:**
- No relocation tables needed in the binary
- Enables XIP from ROM (text has no absolute addresses to patch)
- Instant loading (only GOT needs patching)

**Cons:**
- 68000 PIC is expensive: every global access goes through a GOT
  register (a5), costing ~4 extra cycles per access
- Code size increases 15-30% due to GOT indirection instructions
- Burns one address register (a5) permanently for GOT base pointer
- GCC's 68000 PIC support uses 16-bit displacements (68020 has 32-bit),
  limiting GOT to ±32 KB from the base register
- Runtime overhead on every global variable access compounds in loops

**Verdict:** Wrong tradeoff for the 68000. The 15-30% code bloat and
per-access overhead are too high when we can achieve XIP through
build-time address resolution at zero runtime cost. PIC makes sense
on architectures with cheap PC-relative data access (x86-64, AArch64)
but not on the 68000's limited addressing modes.

### Option 4: bFLT (Binary Flat Format, uClinux-style)

**Format:** 64-byte header, GOT-based relocation, optional compression.
Used by uClinux on m68k/ColdFire, ARM, SuperH, and others.

**Pros:**
- Battle-tested on no-MMU systems including m68k/ColdFire
- XIP support via GOT/PIC (`FLAT_FLAG_GOTPIC`) — text executes from ROM
- Compression support (`FLAT_FLAG_GZIP` / `FLAT_FLAG_GZDATA`)
- Version 4 relocation format is simple: flat list of uint32_t offsets
  (identical to what we're using)
- Mature tooling (elf2flt converter)

**Cons:**
- XIP requires `-fPIC` which is expensive on the 68000 (see PIC section
  above for detailed cost analysis)
- Tooling requires `*-uclinux-*` toolchain — doesn't work with our
  existing `m68k-elf-gcc`
- Larger header (64 bytes vs 32 bytes)
- Compression requires gzip decompressor in kernel (~2-4 KB code)
- No advantage over extending our own header for the relocation format

**Verdict:** bFLT's relocation format (version 4) is identical to ours —
we've effectively adopted it already. bFLT's XIP via GOT/PIC is genuinely
useful but too expensive on the 68000 (see detailed analysis in the
"bFLT: What It Does and What's Worth Stealing" and "ROM Execute-in-Place
Strategies" sections). We achieve XIP more cheaply through build-time
resolution or kernel-linked overlays.

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

### Step 1: ROM XIP via Kernel-Linked Apps (Strategy B)

**The biggest immediate win.** Link app text directly into the kernel
ROM using OVERLAY linker sections. exec() becomes a memcpy of .data +
BSS zero + JMP to ROM. Zero runtime relocation. Reclaims ~70% of
user RAM that was being wasted on code copies.

**Files to modify:**
- `pal/megadrive/megadrive.ld` — add app text sections and OVERLAY
  for app data at USER_BASE
- `apps/Makefile` — build apps as relocatable objects (`ld -r`) instead
  of standalone ELFs, for inclusion in the kernel link
- `kernel/exec.c` — look up ROM metadata instead of reading from
  filesystem; copy .data from ROM init address, zero BSS, JMP
- `tools/mkfs.c` — emit filesystem entries that reference ROM addresses
  (or build a ROM app table)
- `kernel/kernel.h` — ROM app table structure

**Build flow change:**
```
Before: app.c → app.o → app.elf → mkbin → genix binary → mkfs → romdisk
After:  app.c → app.o → ld -r with libc → app_linked.o → kernel link
```

**Testing:**
- `make test-md-auto` — verify apps execute from ROM
- Compare exec() speed (should be significantly faster)
- Verify all 34 apps work from ROM

### Step 2: Extended Header + Relocation Engine

For programs loaded from RAM (workbench) or SD card (future), add
runtime relocation support to the existing binary format.

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

### Step 3: Build-Time Resolved XIP (Strategy A)

For ROM-resident apps that don't use the kernel-linked approach (e.g.,
if the overlay linker script becomes unwieldy with many apps), add a
post-link tool that resolves addresses after mkfs placement.

**Files to modify/add:**
- `tools/romfix.c` — new tool: reads ROM image + relocation tables,
  patches absolute addresses with actual ROM offsets and USER_BASE
- `tools/mkbin.c` — emit relocation tables in the binary
- `Makefile` — add romfix step after kernel link

This is an alternative to Step 1, not a replacement. Use whichever
approach is simpler for the number of apps being built.

### Step 4: Dynamic Load Address (Multitasking)

**Files to modify:**
- `kernel/exec.c` — allocate memory via kmalloc instead of fixed
  USER_BASE, pass actual load address to relocator
- `kernel/proc.c` — per-process `mem_base` already exists

**Testing:**
- Load two programs at different addresses simultaneously
- Verify both can run and be scheduled

Note: ROM XIP programs (Steps 1/3) can't be dynamically relocated
(text is in ROM). For multitasking, either:
- Text stays shared in ROM, each process gets its own data at a
  different RAM address (but text→data refs in ROM still point to
  USER_BASE — only works if data layout is identical at every address)
- Use runtime relocation (Step 2) for multitasking programs,
  loading text into RAM or SRAM

### Step 5: Split Text/Data Support

**Files to modify:**
- `tools/mkbin.c` — emit separate text_size
- `kernel/exec.c` — use split-aware relocator

This step is preparation for bank-swapping but also enables code
sharing (multiple processes running the same binary share one copy
of the text segment, each with their own data).

### Step 6: SD Card Loading

Load binaries from EverDrive Pro/Open EverDrive SD card, relocate to
dynamically allocated RAM, run. Depends on the SD card driver work
(see `docs/everdrive-sd-card.md`). These programs use runtime
relocation (Step 2), not ROM XIP.

### Step 7: Bank-Swapping (Future)

When EverDrive Pro bank-swapping is implemented:

- Load text segment into a banked SRAM window (writable, so patchable)
- Load data segment into main RAM
- Use the split-aware relocator (built in Step 5) to handle
  different text/data base addresses
- Switch SRAM bank on context switch

This gives per-process text isolation without PIC: text is in SRAM
(not ROM), so the relocator can patch text→data references at load
time. Combined with ROM XIP for shared/read-only programs, this covers
all use cases without ever needing GOT/PIC.

---

## Appendix A: Size Estimates

### ROM XIP (Kernel-Linked, Step 1)

| Component | Code Size | RAM Cost |
|-----------|----------|----------|
| Linker script changes | ~50 lines | 0 |
| Makefile changes | ~30 lines | 0 |
| exec.c ROM lookup | ~40 lines | 0 |
| ROM app table | ~20 lines | ~(8 bytes × N apps) |
| **Total kernel addition** | **~60 lines** | **~272 bytes** (34 apps) |

**RAM savings:** ~70% of current user program memory. A typical exec()
that copies 6 KB to RAM now copies only ~1.5 KB (data+BSS).

### Runtime Relocation (Step 2)

| Component | Code Size | RAM Cost |
|-----------|----------|----------|
| Header field rename | ~5 lines | 0 |
| Relocation engine | ~30 lines | 0 (uses BSS) |
| Loader changes | ~50 lines | 0 |
| mkbin relocation extraction | ~100 lines | 0 (host tool) |
| **Total kernel addition** | **~80 lines** | **0 bytes** |

## Appendix B: References

- [bFLT format header (flat.h)](https://github.com/uclinux-dev/elf2flt/blob/main/flat.h)
- [elf2flt converter](https://github.com/uclinux-dev/elf2flt)
- [uClinux flat file format overview](http://myembeddeddev.blogspot.com/2010/02/uclinux-flat-file-format.html)
- [XFLAT FAQ (GOT vs thunk comparison)](https://xflat.sourceforge.net/XFlatFAQ.html)
- [bFLT on MMU systems (LWN)](https://lwn.net/Articles/694386/)
