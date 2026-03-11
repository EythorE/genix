# Relocatable Binaries — Implementation Plan

Concrete implementation plan for adding relocatable binaries to Genix.
Covers both concurrent processes (bg+fg, pipes) and SD card loading.

**Date:** 2026-03-11

---

## Design Decisions

### Why NOT `-msep-data` (for now)

On m68k, `-msep-data` makes a5 the data base register. Every global
variable access becomes `move.l (offset,a5), d0` instead of
`move.l var.l, d0`. This enables XIP from ROM (shared text, per-process
data) but at significant cost:

- **Burns a5 permanently** — one fewer address register for all code
- **One extra indirection per global access** — adds ~4 cycles each
- **Code size increase** — extra addressing mode bytes per reference
- **All code must be compiled with it** — libc, crt0, every app
- **16-bit displacement limit** — data segment must be < 32 KB from a5

For the immediate goal (one bg + one fg, SD card binaries), runtime
relocation with text in RAM is sufficient and much simpler. Two small
programs (10-12 KB each) fit easily in 27.5 KB user RAM.

### Why runtime relocation is enough

The user's stated goal: one foreground process + one background process,
plus SD card binaries. Let's check the RAM budget:

**Two concurrent processes (bg + fg):**
```
cat:     ~3 KB text + 1 KB data + 2 KB stack = ~6 KB
grep:    ~8 KB text + 2 KB data + 2 KB stack = ~12 KB
                                         Total: ~18 KB
Available: 27.5 KB → 9.5 KB headroom
```

**Three-stage pipe (ambitious but doable):**
```
cat:     ~6 KB
grep:    ~12 KB
wc:      ~6 KB
                Total: ~24 KB (tight but fits in 27.5 KB)
```

### When XIP becomes necessary

If pipes of 4+ stages or larger programs are needed, XIP saves ~60-70%
of per-process RAM by keeping text in ROM. At that point, two options:

1. **SRAM bank-switching** (EverDrive Pro): text in banked SRAM
   (writable, so relocatable without PIC). Per the existing roadmap.

2. **`-msep-data`**: XIP from ROM with a5 as data base. Accept the
   overhead. This is the escape hatch if SRAM isn't available.

Both use the same binary format — the relocation table and `text_size`
field support split text/data from day one.

---

## Binary Format Change

Extend the existing 32-byte header using the reserved fields:

```c
struct genix_header {
    uint32_t magic;       /* 0x47454E58 "GENX" */
    uint32_t load_size;   /* text+data bytes to load */
    uint32_t bss_size;    /* bytes to zero after load */
    uint32_t entry;       /* offset from start (0-based when relocatable) */
    uint32_t stack_size;  /* stack hint (0 = default 4 KB) */
    uint32_t flags;       /* bit 0: GENX_RELOC (relocatable) */
    uint32_t text_size;   /* text segment size (0 = no text/data split) */
    uint32_t reloc_size;  /* relocation table size in bytes */
};

#define GENX_RELOC  0x01  /* binary has relocation table */
```

**On-disk layout:**
```
┌──────────────────────┐  offset 0
│ Header (32 bytes)    │
├──────────────────────┤  offset 32
│ .text (text_size)    │
├──────────────────────┤  offset 32 + text_size
│ .data (load_size     │
│        - text_size)  │
├──────────────────────┤  offset 32 + load_size
│ .reloc (reloc_size)  │  array of uint32_t offsets
└──────────────────────┘
```

**Backward compatibility:** Old binaries have `flags=0`, `text_size=0`,
`reloc_size=0` (the old reserved fields were required to be zero).
The loader treats them exactly as before — loaded at USER_BASE with
no relocation.

**Relocation entries:** Each entry is a big-endian uint32_t byte offset
into the flat binary (text+data). At that offset is a 32-bit value
that must have the load base address added to it.

For split text/data (future XIP): the relocator uses `text_size` to
determine whether each referenced value points into text or data,
and adds the appropriate base address. When `text_size=0`, text and
data are contiguous and a single base suffices.

---

## Implementation Steps

### Step 1: mkbin — Extract and Emit Relocations

**File:** `tools/mkbin.c`

**Change:** Link apps with `ld -q` (`--emit-relocs`). This preserves
`.rel.*` sections in the output ELF even though symbols are resolved.
mkbin scans these sections for `R_68K_32` entries (32-bit absolute
address relocations) and emits them as a uint32_t offset array.

**What mkbin does now:**
1. Read ELF, find PT_LOAD segments
2. Flatten to contiguous binary
3. Write 32-byte header + flat binary

**What mkbin will do:**
1. Read ELF, find PT_LOAD segments (unchanged)
2. Flatten to contiguous binary (unchanged)
3. Scan SHT_REL sections for R_68K_32 entries
4. For each R_68K_32: record `r_offset - load_base` as a reloc entry
5. Convert entry point: `entry = elf_entry - load_base` (0-based)
6. Set `flags = GENX_RELOC`, `text_size` = .text section size,
   `reloc_size` = nrelocs * 4
7. Write header + flat binary + relocation table

**Linker change** (`apps/Makefile`):
```makefile
# Add -q to preserve relocations for mkbin
%.elf: %.o $(CRT0) $(LIBC) $(LDSCRIPT)
    $(LD) -q -n -T $(LDSCRIPT) -o $@ $(CRT0) $< $(LIBC) $(LIBGCC)
```

**ELF relocation types to handle:**
- `R_68K_32` (type 1): 32-bit absolute — **this is the one we need**
- `R_68K_PC32` (type 4): PC-relative — skip (no relocation needed)
- `R_68K_PC16` (type 5): PC-relative 16-bit — skip
- Others: warn and skip (shouldn't appear in simple code)

**Size of reloc table:** A typical small program (cat, echo) might
have 20-50 relocations = 80-200 bytes. grep with regex might have
100-200 relocations = 400-800 bytes. Negligible compared to text size.

**Testing:**
- Host test: mkbin on a simple ELF, verify reloc table is correct
- Verify old-format binaries (flags=0) are still produced when
  linking without `-q` (or when no R_68K_32 relocs exist)

### Step 2: Kernel — Relocation Engine

**File:** `kernel/exec.c`

**New function (~30 lines):**
```c
/*
 * Apply relocations to a loaded binary.
 * base: actual load address
 * relocs: array of uint32_t offsets into the binary
 * nrelocs: number of relocation entries
 */
static void relocate(uint8_t *base, const uint32_t *relocs, int nrelocs)
{
    for (int i = 0; i < nrelocs; i++) {
        uint32_t off = relocs[i];
        uint32_t *ptr = (uint32_t *)(base + off);
        *ptr += (uint32_t)base;  /* add load address */
    }
}
```

**Change to `load_binary()`:**
```
1. Read 32-byte header
2. Validate (unchanged, but entry is now 0-based for relocatable)
3. Allocate memory for this process (see Step 3)
4. Read text+data into allocated memory
5. If flags & GENX_RELOC:
   a. Read reloc table into BSS area (temporary, will be zeroed)
   b. Byte-swap reloc entries (big-endian on disk → native)
   c. Call relocate(load_addr, relocs, nrelocs)
6. Zero BSS
7. Set up user stack
8. entry_out = load_addr + header.entry
```

**The BSS trick for zero-cost reloc loading:**
The reloc table is loaded into what will become the BSS region. Since
BSS gets zeroed after relocation, this costs zero extra RAM:

```
Memory during relocation:
  [text] [data] [reloc_table_in_BSS_area] [... free ...]

After zeroing BSS:
  [text] [data] [BSS (zeroed)]            [... free ...]
```

This only works if `reloc_size <= bss_size`. For the rare case where
it doesn't (very small BSS, many relocs), allocate a temporary buffer
from the kernel heap.

**Testing:**
- Host test: relocate() with known inputs
- Workbench: load relocatable hello at USER_BASE (delta=0, same
  behavior as today, validates format)
- Workbench: load at USER_BASE + 0x1000 to test actual relocation

### Step 3: Per-Process User Memory Allocation

**File:** `kernel/mem.c` (new function) and `kernel/exec.c`

Currently all processes load at the fixed `USER_BASE`. For multiple
concurrent processes, each needs its own memory region.

**Simple approach — arena allocator for user RAM:**

```c
/* User memory arena: USER_BASE to USER_TOP */
static uint32_t user_arena_ptr;  /* next free address (bump allocator) */

void user_mem_init(void)
{
    user_arena_ptr = USER_BASE;
}

/* Allocate a chunk of user memory. Returns base address or 0. */
uint32_t user_mem_alloc(uint32_t size)
{
    size = (size + 3) & ~3;  /* align to 4 bytes */
    if (user_arena_ptr + size > USER_TOP)
        return 0;  /* out of memory */
    uint32_t base = user_arena_ptr;
    user_arena_ptr += size;
    return base;
}

/* Free user memory. For now, only frees if it's the top allocation
 * (stack-like). Full free-list comes later if needed. */
void user_mem_free(uint32_t base, uint32_t size)
{
    size = (size + 3) & ~3;
    if (base + size == user_arena_ptr)
        user_arena_ptr = base;
    /* else: leaked until all higher allocations are freed */
}
```

A bump allocator is fine for the initial use case (one bg + one fg).
When the bg process exits, its memory returns to the pool. A full
free-list allocator can replace this later if fragmentation matters.

**Change to `load_binary()`:**
```c
/* Instead of always using USER_BASE: */
uint32_t total = hdr.load_size + hdr.bss_size + stack;
uint32_t load_addr;

if (hdr.flags & GENX_RELOC) {
    load_addr = user_mem_alloc(total);
    if (!load_addr) return -ENOMEM;
} else {
    load_addr = USER_BASE;  /* old-style fixed binary */
}
```

**Process cleanup (`do_exit`):**
```c
if (curproc->mem_base != USER_BASE) {
    user_mem_free(curproc->mem_base, curproc->mem_size);
}
```

**Testing:**
- Host test: arena allocator alloc/free patterns
- Workbench: spawn two processes, verify different load addresses
- Workbench: verify memory freed on exit

### Step 4: Linker Script for Relocatable Binaries

**File:** `apps/user-reloc.ld` (new)

For relocatable binaries, link at address 0. The actual load address
is determined at runtime.

```ld
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

SECTIONS
{
    . = 0;

    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    _text_size = .;

    .data : {
        *(.data .data.*)
    }

    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    }

    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
```

Linking at address 0 means every absolute reference in the binary has
value = offset from start. The relocator adds the actual load address.

The same linker script works for both workbench and Mega Drive — no
more separate user.ld and user-md.ld for relocatable binaries.

**Testing:**
- Build hello with user-reloc.ld, verify mkbin produces valid reloc table
- Load on workbench (USER_BASE=0x040000) and Mega Drive (USER_BASE=0xFF9000)
  with the same binary file

### Step 5: Header Update in kernel.h

**File:** `kernel/kernel.h`

```c
/* Genix binary format header (32 bytes, big-endian on disk) */
#define GENIX_MAGIC     0x47454E58  /* "GENX" */
#define GENIX_HDR_SIZE  32

/* Header flags */
#define GENX_RELOC      0x01  /* has relocation table */

struct genix_header {
    uint32_t magic;       /* 0x47454E58 "GENX" */
    uint32_t load_size;   /* text+data bytes to load */
    uint32_t bss_size;    /* bytes to zero after load */
    uint32_t entry;       /* entry offset (0-based when GENX_RELOC) */
    uint32_t stack_size;  /* stack hint (0 = default 4 KB) */
    uint32_t flags;       /* GENX_RELOC, etc. */
    uint32_t text_size;   /* text segment size (for future split reloc) */
    uint32_t reloc_size;  /* relocation table size in bytes */
};
```

### Step 6: Host Tests

**File:** `tests/test_reloc.c` (new)

Test cases:
1. **relocate() basic**: known binary with 3 relocs, verify patched values
2. **relocate() with zero delta**: load at address 0, no change
3. **relocate() at high address**: load at 0xFF9000, verify 32-bit wraparound OK
4. **Header validation**: GENX_RELOC flag accepted, entry is 0-based offset
5. **Header backward compat**: flags=0 binary still validates correctly
6. **User arena allocator**: alloc, free, alloc-after-free, OOM
7. **BSS trick**: verify reloc table fits in BSS area calculation
8. **Split reloc (future-proofing)**: text_size > 0, dual-base relocation

### Step 7: Build System Integration

**File:** `apps/Makefile`

```makefile
# Relocatable build (default for new binaries)
LDSCRIPT_RELOC = user-reloc.ld
LDFLAGS_RELOC = -q -n -T $(LDSCRIPT_RELOC)

# Legacy fixed-address build (for testing/comparison)
LDSCRIPT = user.ld
LDFLAGS_FIXED = -n -T $(LDSCRIPT)

%.elf: %.o $(CRT0) $(LIBC) $(LDSCRIPT_RELOC)
	$(LD) $(LDFLAGS_RELOC) -o $@ $(CRT0) $< $(LIBC) $(LIBGCC)
```

The Mega Drive build no longer needs a separate `user-md.ld` — the
same relocatable binary works on both platforms. The `apps-md` target
can be eliminated.

**File:** `Makefile` (top-level)

The `disk` and `disk-md` targets both use the same relocatable binaries.
`mkfs` includes the reloc table in the filesystem image.

---

## Execution Order

```
1. kernel/kernel.h     — header struct + flags          (~10 lines changed)
2. tools/mkbin.c       — ELF reloc extraction           (~120 lines added)
3. tests/test_reloc.c  — host tests for relocator       (~200 lines, new file)
4. kernel/exec.c       — relocate() + loader changes    (~60 lines added)
5. kernel/mem.c        — user arena allocator            (~30 lines added)
6. apps/user-reloc.ld  — new linker script               (~30 lines, new file)
7. apps/Makefile        — link with -q + user-reloc.ld   (~5 lines changed)
8. apps/crt0.S         — no changes needed (0-based entry works)
```

**Total kernel code addition:** ~90 lines
**Total tooling addition:** ~120 lines
**Total test addition:** ~200 lines

### Verification at each step

After step 2: `make test` passes, mkbin produces valid reloc binaries
After step 4: `make test` passes (host tests verify relocation math)
After step 7: `make run` boots, apps work (delta=0 on workbench verifies
format without changing behavior). `make test-emu` passes.
Final: `make test-all` passes (full testing ladder including BlastEm)

---

## What This Enables

### Immediate (this implementation)

- **One bg + one fg**: Two processes in user RAM, each at different addresses
- **SD card binaries**: Load from SD card at whatever address is free
- **Unified binaries**: Same binary file works on workbench and Mega Drive
- **Short pipes**: 2-3 stage pipes of small utilities fit in 27.5 KB

### Future (uses same format, no format change needed)

- **ROM XIP via `-msep-data`**: Set `GENX_SEPDATA` flag, a5 = data base,
  text stays in ROM. Only data+bss+stack in RAM per process. Enables
  5+ stage pipes. Same reloc table, just applied only to data section.

- **ROM XIP via SRAM banking**: Text in banked SRAM, data in main RAM.
  Split relocation uses `text_size` to apply different bases. No PIC
  needed — SRAM is writable.

- **Build-time XIP**: `romfix` tool resolves relocs at build time for
  ROM-resident apps. Zero runtime cost. Reloc table consumed and
  discarded by the tool.

---

## Risk Assessment

| Risk | Level | Mitigation |
|------|-------|-----------|
| `--emit-relocs` unsupported by toolchain | LOW | Standard GNU ld feature since 2.15 |
| R_68K_32 not the only abs reloc type | LOW | Warn on unexpected types; m68k uses R_68K_32 for all 32-bit absolute |
| Reloc table larger than BSS | LOW | Fallback to kernel heap temp buffer; rare in practice |
| User arena fragmentation | MEDIUM | Bump allocator sufficient for 2-3 processes; upgrade to free-list if needed |
| Alignment issues at non-standard load addresses | MEDIUM | Link at 0, loader enforces 4-byte alignment; test on BlastEm |
| Performance regression from relocation | LOW | Relocation runs once at exec() time; ~100 entries × 3 cycles = 300 cycles total |

---

## Files Changed Summary

| File | Change | Lines |
|------|--------|-------|
| `kernel/kernel.h` | Header struct, flags | ~10 |
| `kernel/exec.c` | relocate(), loader changes | ~60 |
| `kernel/mem.c` | user_mem_alloc/free | ~30 |
| `tools/mkbin.c` | ELF reloc extraction | ~120 |
| `apps/Makefile` | Link flags | ~5 |
| `apps/user-reloc.ld` | New linker script | ~30 (new) |
| `tests/test_reloc.c` | Host tests | ~200 (new) |
| **Total** | | **~455 lines** |
