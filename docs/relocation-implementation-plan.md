# Relocation Table Implementation Plan

Concrete implementation plan for adding relocatable binaries to Genix.
Based on the research in `docs/relocatable-binaries.md` (which is the
canonical reference — do NOT delete or overwrite that document).

**Date:** 2026-03-11

---

## Design Decision Summary

After evaluating four options (extended Genix header, FUZIX a.out, PIC/GOT,
bFLT), the choice is **Option 1: extend the existing Genix header**. The
rationale is documented in `docs/relocatable-binaries.md` §8 and §10.

Key properties of the chosen approach:
- Backward-compatible (old `flags=0` binaries load unchanged)
- Zero extra RAM (relocation table loaded into BSS, processed, BSS zeroed)
- Split text/data aware (future-proofs for bank-swapping)
- ~80 lines of kernel code, ~100 lines of mkbin changes
- Proven mechanism (identical to FUZIX's relocator)

---

## Phase 1: Linker Script + mkbin — Link at Address 0

**Goal:** Produce binaries linked at base address 0, so all absolute
addresses are zero-based offsets ready for relocation.

### 1.1 New Linker Script: `apps/user-reloc.ld`

```ld
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

SECTIONS
{
    . = 0;

    .text : {
        _text_start = .;
        *(.text .text.*)
        *(.rodata .rodata.*)
        _text_end = .;
    }

    .data : {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    }

    .bss : {
        _bss_start = .;
        *(.bss .bss.* COMMON)
        _bss_end = .;
    }

    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
```

Key change: `. = 0` instead of `0x040000` or `0xFF9000`. The linker
produces an ELF with all addresses starting from 0. The `--emit-relocs`
flag (passed via `LDFLAGS`) preserves relocation entries in the ELF for
mkbin to extract.

### 1.2 Build Flag Changes (`apps/Makefile`)

Add `--emit-relocs` to the linker flags so the ELF retains relocation
information that mkbin can extract. Continue building the existing
fixed-address binaries in parallel until relocation is validated.

```makefile
# Relocatable build (new)
RELOC_LDFLAGS = -T user-reloc.ld --emit-relocs
```

### 1.3 Validation

- Verify ELF entry point is 0 (or small offset)
- Verify `readelf -r` shows relocation entries
- Verify all absolute address references are zero-based
- Existing fixed-address builds continue to work

### Files Modified
- `apps/user-reloc.ld` (new)
- `apps/Makefile` (add relocatable build target)

---

## Phase 2: mkbin Relocation Extraction

**Goal:** Extend mkbin to read ELF relocation entries and emit them as
a simple uint32_t offset array appended after the data segment.

### 2.1 ELF Relocation Parsing

Add ELF section header and relocation entry parsing to mkbin. The
relevant ELF structures:

```c
/* Section header */
struct elf32_shdr {
    uint32_t sh_name;
    uint32_t sh_type;      /* SHT_REL=9, SHT_RELA=4 */
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addend;
    uint32_t sh_entsize;
};

/* Relocation entry (REL — no addend, used by m68k) */
struct elf32_rel {
    uint32_t r_offset;     /* offset from section start */
    uint32_t r_info;       /* symbol + type */
};

/* Relocation entry (RELA — with addend) */
struct elf32_rela {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
};
```

For `--emit-relocs` ELF files, m68k-elf-ld produces `SHT_RELA` sections
(type 4). Each entry's `r_offset` is the offset into the section being
relocated where a 32-bit absolute address lives.

### 2.2 Relocation Filtering

Only `R_68K_32` (type 1) relocations need processing — these are
absolute 32-bit address references. Other relocation types:

| Type | Name | Action |
|------|------|--------|
| 1 | `R_68K_32` | **Emit** — absolute 32-bit address |
| 2 | `R_68K_16` | **Error** — 16-bit absolute (shouldn't appear) |
| 4 | `R_68K_PC32` | **Skip** — PC-relative, no patching needed |
| 5 | `R_68K_PC16` | **Skip** — PC-relative |
| 6 | `R_68K_PC8` | **Skip** — PC-relative |

mkbin should warn on any unexpected relocation type and fail if it
encounters `R_68K_16` (16-bit absolute — would require a different
relocation entry format to handle).

### 2.3 Output Format

The output binary becomes:

```
[32-byte Genix header]
[text + data segments]        (load_size bytes)
[relocation table]            (reloc_size bytes)
```

The relocation table is an array of big-endian uint32_t offsets. Each
offset is relative to the start of the loaded image (offset 0 = first
byte of text segment).

### 2.4 Header Update

mkbin sets the new header fields:

```c
put32(hdr + 20, 1);           /* flags: bit 0 = relocatable */
put32(hdr + 24, text_size);   /* text segment size */
put32(hdr + 28, reloc_count * 4);  /* relocation table size */
```

`text_size` is computed from the ELF's `.text` + `.rodata` sections
(everything before `.data`). This enables the split-aware relocator to
distinguish text references from data references.

### 2.5 mkbin Implementation Outline

```c
/* After existing PT_LOAD segment processing: */

/* 1. Find .rela.text and .rela.data sections */
/* 2. For each RELA entry with type R_68K_32: */
/*    - Compute file offset = section_base + r_offset */
/*    - Convert to flat-binary offset (relative to load_base) */
/*    - Add to relocation offset array */
/* 3. Sort offsets (for cache-friendly access during relocation) */
/* 4. Append relocation table after flat binary data */
/* 5. Set flags, text_size, reloc_size in header */
```

### 2.6 Validation

- Build `hello` with `user-reloc.ld` + `--emit-relocs`
- Run `mkbin` and verify relocation table output
- Verify relocation count matches `readelf -r` filtered to R_68K_32
- Verify each offset points to a 32-bit word containing a zero-based address
- Add `mkbin --dump-relocs` flag for debugging

### Files Modified
- `tools/mkbin.c` (add ELF section/relocation parsing, new output format)

---

## Phase 3: Kernel Header Update

**Goal:** Update the kernel's `genix_header` struct and validation logic
to recognize the new fields.

### 3.1 Header Struct Change

In `kernel/kernel.h`:

```c
struct genix_header {
    uint32_t magic;       /* 0x47454E58 "GENX" */
    uint32_t load_size;   /* text+data bytes to load */
    uint32_t bss_size;    /* bytes to zero */
    uint32_t entry;       /* entry offset (0-based when relocatable) */
    uint32_t stack_size;  /* stack hint (0 = default 4 KB) */
    uint32_t flags;       /* bit 0: relocatable */
    uint32_t text_size;   /* text segment size (0 = no split) */
    uint32_t reloc_size;  /* relocation table size in bytes */
};

#define GENIX_FLAG_RELOC  0x01
```

### 3.2 Validation Update (`exec_validate_header`)

- Accept `flags & GENIX_FLAG_RELOC` (don't reject non-zero flags)
- For relocatable binaries: `entry` is a zero-based offset, not an
  absolute address. Validate `entry < load_size` (not `entry >= USER_BASE`)
- Validate `reloc_size` is a multiple of 4
- Validate `reloc_size / 4` relocation entries fit (each offset < load_size)
- Validate `text_size <= load_size`
- Total memory check: `load_size + bss_size + stack >= reloc_size`
  (BSS must be large enough to temporarily hold the relocation table)

### 3.3 Backward Compatibility

Old binaries with `flags=0` load exactly as before. The `reserved[2]`
fields were always zero, so `text_size=0` and `reloc_size=0` is the
default — no relocation applied.

### 3.4 Host Test Updates

Update `tests/test_exec.c` (or create `tests/test_reloc.c`):

- Test header validation with `flags=0` (backward compat)
- Test header validation with `flags=1, reloc_size=N`
- Test rejection of `reloc_size` not multiple of 4
- Test rejection of reloc offset > load_size
- Test rejection of entry >= load_size for relocatable binary
- Test BSS-too-small-for-relocs rejection

### Files Modified
- `kernel/kernel.h` (struct rename)
- `kernel/exec.c` (validation update)
- `tools/mkbin.c` (update local header struct copy)
- `tests/test_exec.c` or `tests/test_reloc.c` (new tests)

---

## Phase 4: Kernel Relocation Engine

**Goal:** Implement the relocator that patches absolute addresses at
load time, using the BSS-trick for zero extra RAM.

### 4.1 Loading Sequence (Relocatable Binary)

```
1. Read 32-byte header, validate
2. Read text+data (load_size bytes) into USER_BASE
3. Read relocation table (reloc_size bytes) into USER_BASE + load_size
   (this is the BSS area — guaranteed large enough, validated in 3.2)
4. Apply relocations: for each offset in the table, patch the 32-bit
   word at USER_BASE + offset
5. Zero BSS (destroys the relocation table, which is no longer needed)
6. Set up stack, jump to entry
```

### 4.2 Relocator Function

```c
/*
 * Apply relocations to a loaded binary.
 *
 * For non-split binaries (text_size == 0): simple delta addition.
 *   Every relocated value gets load_addr added.
 *
 * For split binaries (text_size > 0): segment-aware relocation.
 *   Values < text_size reference text → add text_base.
 *   Values >= text_size reference data → subtract text_size, add data_base.
 *
 * In the common case (contiguous load), text_base == data_base - text_size,
 * so both paths produce the same result. The split path exists for future
 * bank-swapping where text and data live at different addresses.
 */
static void apply_relocations(uint8_t *base, uint32_t load_addr,
                               uint32_t text_size, uint32_t load_size,
                               const uint32_t *relocs, uint32_t nrelocs)
{
    if (text_size == 0) {
        /* Simple: all in one segment, just add delta */
        for (uint32_t i = 0; i < nrelocs; i++) {
            uint32_t off = relocs[i];  /* big-endian on 68000 = native */
            uint32_t *ptr = (uint32_t *)(base + off);
            *ptr += load_addr;
        }
    } else {
        /* Split-aware: determine which segment each value references */
        uint32_t text_base = load_addr;
        uint32_t data_base = load_addr + text_size;
        for (uint32_t i = 0; i < nrelocs; i++) {
            uint32_t off = relocs[i];
            uint32_t *ptr = (uint32_t *)(base + off);
            uint32_t val = *ptr;
            if (val < text_size)
                *ptr = val + text_base;
            else
                *ptr = (val - text_size) + data_base;
        }
    }
}
```

### 4.3 Integration into `load_binary()`

```c
int load_binary(const char *path, const char **argv,
                uint32_t *entry_out, uint32_t *user_sp_out)
{
    /* ... existing header read and validation ... */

    int is_reloc = (hdr.flags & GENIX_FLAG_RELOC);

    /* Load text+data */
    fs_read(ip, (void *)USER_BASE, GENIX_HDR_SIZE, hdr.load_size);

    if (is_reloc && hdr.reloc_size > 0) {
        /* Load relocation table into BSS area (temporary) */
        uint32_t reloc_off = GENIX_HDR_SIZE + hdr.load_size;
        uint8_t *reloc_buf = (uint8_t *)(USER_BASE + hdr.load_size);
        fs_read(ip, reloc_buf, reloc_off, hdr.reloc_size);

        /* Apply relocations */
        uint32_t nrelocs = hdr.reloc_size / 4;
        apply_relocations((uint8_t *)USER_BASE, USER_BASE,
                          hdr.text_size, hdr.load_size,
                          (const uint32_t *)reloc_buf, nrelocs);
    }

    /* Zero BSS (destroys relocation table) */
    if (hdr.bss_size > 0)
        memset((void *)(USER_BASE + hdr.load_size), 0, hdr.bss_size);

    /* Compute absolute entry point */
    uint32_t entry;
    if (is_reloc)
        entry = USER_BASE + hdr.entry;  /* entry is 0-based offset */
    else
        entry = hdr.entry;              /* entry is absolute (legacy) */

    /* ... rest unchanged ... */
}
```

### 4.4 Host Tests for the Relocator

Create `tests/test_reloc.c`:

```
Test cases:
1. Simple relocation (text_size=0):
   - Single reloc at offset 0, value 0x100 → becomes 0x100 + load_addr
   - Multiple relocs at various offsets
   - Zero relocs (empty table)
   - Edge: reloc at last possible offset (load_size - 4)

2. Split relocation (text_size > 0):
   - Value < text_size → gets text_base added
   - Value >= text_size → gets (value - text_size) + data_base
   - Mixed: some text refs, some data refs
   - Edge: value == text_size (first data byte)
   - Edge: value == text_size - 1 (would be within text, but that's
     a byte offset — should never appear since relocs point to 32-bit
     words. Validate alignment.)

3. BSS trick validation:
   - Verify BSS area is large enough for reloc table
   - Verify reloc table is destroyed after zeroing BSS

4. Backward compatibility:
   - flags=0 binary loads identically to current behavior
   - entry is absolute for flags=0, offset for flags=1
```

### Files Modified
- `kernel/exec.c` (add `apply_relocations`, modify `load_binary`)
- `tests/test_reloc.c` (new file)

---

## Phase 5: End-to-End Validation

**Goal:** Build a relocatable binary, load it, and verify it runs
correctly on both workbench and Mega Drive.

### 5.1 Test Program

Start with `hello.c` — simplest program, has string literals (which
generate relocations for the pointer to "Hello, world!\n").

```bash
# Build relocatable hello
m68k-elf-gcc -m68000 -Os -c -o hello.o apps/hello.c
m68k-elf-ld -T apps/user-reloc.ld --emit-relocs \
    -o hello-reloc.elf apps/crt0.o hello.o libc/libc.a
tools/mkbin hello-reloc.elf hello-reloc.bin
# Inspect
tools/mkbin --dump-relocs hello-reloc.elf  # show reloc table
```

### 5.2 Workbench Test

Load the relocatable binary in the workbench emulator at USER_BASE.
Since delta = USER_BASE - 0 = USER_BASE, all addresses get USER_BASE
added. The result should be identical to the current fixed-address binary.

Verify by comparing:
- `hexdump` of the relocated-at-USER_BASE image vs the fixed-address image
- Output of running both versions

### 5.3 Mega Drive Test

Same binary, loaded at the Mega Drive's USER_BASE (0xFF9000). This is
the key test — one binary works on both platforms.

### 5.4 Unified Disk Image

Once validated, switch the default build to produce relocatable binaries.
Remove the separate `user.ld` / `user-md.ld` and use `user-reloc.ld`
for everything. One set of binaries in the filesystem image.

### 5.5 Testing Ladder

```bash
make test           # Host tests including test_reloc.c
make kernel         # Cross-compilation check
make test-emu       # Workbench autotest with relocatable binaries
make megadrive      # Mega Drive build
make test-md-auto   # BlastEm autotest (primary quality gate)
```

### Files Modified
- `apps/Makefile` (switch default build to relocatable)
- `Makefile` (integration)
- Various test scripts

---

## Phase 6: Dynamic Load Address (Multitasking Prep)

**Goal:** Load binaries at addresses other than USER_BASE, proving the
relocator works with arbitrary load addresses.

### 6.1 Changes

- `load_binary()` accepts a `load_addr` parameter instead of using
  `USER_BASE` hardcoded
- The current call sites pass `USER_BASE` (no behavior change)
- Future multitasking code passes `kmalloc()`-returned addresses

### 6.2 Test

- Load `hello` at USER_BASE + 0x1000 on workbench (plenty of room)
- Verify it runs correctly
- Load two programs at different addresses simultaneously (prep for
  multitasking scheduler)

### Files Modified
- `kernel/exec.c` (parameterize load address)

---

## Phase 7: Split Text/Data and XIP (Future)

These are documented in `docs/relocatable-binaries.md` §7 and §11 as
Steps 1, 3, 5, 7. They build on the relocation infrastructure from
Phases 1-6 but are not needed for the initial implementation.

---

## Implementation Order and Dependencies

```
Phase 1 (linker script)      ← no dependencies
Phase 2 (mkbin extraction)   ← depends on Phase 1
Phase 3 (kernel header)      ← no dependencies (can parallel Phase 1-2)
Phase 4 (kernel relocator)   ← depends on Phase 3
Phase 5 (end-to-end)         ← depends on Phases 2 + 4
Phase 6 (dynamic address)    ← depends on Phase 5
```

Phases 1+3 can be developed in parallel. Phase 2 and Phase 4 can be
developed in parallel once their prerequisites are done. The critical
path is: Phase 1 → Phase 2 → Phase 5.

## Size Budget

| Component | Lines of Code | RAM Cost |
|-----------|--------------|----------|
| `user-reloc.ld` | ~25 | 0 |
| mkbin relocation extraction | ~120 | 0 (host tool) |
| Header struct rename | ~5 | 0 |
| Validation update | ~20 | 0 |
| `apply_relocations()` | ~30 | 0 (uses BSS) |
| `load_binary()` changes | ~25 | 0 |
| Host tests | ~200 | 0 |
| **Total kernel addition** | **~80 lines** | **0 bytes RAM** |
| **Total including tools** | **~400 lines** | — |

## Risk Mitigation

### Risk: Relocation types beyond R_68K_32

m68k-elf-gcc with `-m68000` should only produce `R_68K_32` for absolute
addresses and `R_68K_PC*` for branches (which don't need relocation).
However:
- Switch jump tables may use `R_68K_32` (good — we handle these) or a
  custom table format (investigate with actual compiler output)
- Mitigation: build all 34 apps with `--emit-relocs`, inspect relocation
  types, verify only `R_68K_32` and `R_68K_PC*` appear

### Risk: BSS too small for relocation table

For programs with minimal BSS and many relocations, BSS might be smaller
than the relocation table. Mitigation:
- `mkbin` checks and warns if `reloc_size > bss_size`
- If this occurs, pad BSS in the linker script or add a minimum BSS size
- In practice, programs with many relocations (many globals, strings)
  also tend to have substantial BSS

### Risk: Alignment issues

Relocation offsets must point to 32-bit-aligned addresses (the 68000
faults on unaligned long access). Mitigation:
- `mkbin` validates every relocation offset is 4-byte aligned
- The linker naturally aligns `.data` and `.text` sections

### Risk: Breaking existing (non-relocatable) binaries

The header change uses fields that were previously `reserved[2]` = 0.
Old binaries have `flags=0, text_size=0, reloc_size=0` — the loader
treats them exactly as before (no relocation applied). Mitigation:
- Explicit backward-compatibility test in the test suite
- Both code paths (relocatable and fixed) exercised in CI

---

## Protecting Existing Documentation

**IMPORTANT:** `docs/relocatable-binaries.md` is the canonical research
document. It contains 1128 lines of carefully researched analysis
covering FUZIX reference implementation, PIC/GOT/XIP background, bFLT
analysis, ROM XIP strategies, relocation strategy evaluation, EverDrive
Pro bank-swapping implications, and the recommended approach.

Any PR that deletes or substantially rewrites `docs/relocatable-binaries.md`
should be **rejected** unless it clearly improves the document. The
implementation plan (this file) is a companion to that research, not a
replacement.

If implementation reveals that the design in `docs/relocatable-binaries.md`
needs changes, update both documents together with clear commit messages
explaining what changed and why.
