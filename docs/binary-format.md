# Genix Binary Format

## Overview

Genix uses a relocatable flat binary format. Programs are linked at
address 0 with relocation entries preserved. At exec() time, the kernel
loads text+data at USER_BASE and patches all absolute 32-bit addresses
using the relocation table. One binary runs on both workbench
(USER_BASE=0x040000) and Mega Drive (USER_BASE=0xFF9000).

## Header (32 bytes, big-endian)

```c
struct genix_header {
    uint32_t magic;       /* 0x47454E58 "GENX" */
    uint32_t load_size;   /* text+data bytes to load */
    uint32_t bss_size;    /* bytes to zero after load */
    uint32_t entry;       /* entry point offset (0-based) */
    uint32_t stack_size;  /* bits 0-15: stack hint (0=4KB), bits 16-31: GOT offset+1 */
    uint32_t flags;       /* GENIX_FLAG_XIP etc. */
    uint32_t text_size;   /* text segment size (for split reloc) */
    uint32_t reloc_count; /* number of uint32_t relocation entries */
};
```

## Binary File Layout

```
[32-byte header]
[text+data: load_size bytes]
[relocation table: reloc_count * 4 bytes]
```

The relocation table is an array of big-endian uint32_t offsets. Each
offset is relative to the start of the loaded image (offset 0 = first
byte of text segment). At each offset, a 32-bit word contains a
zero-based absolute address that the kernel patches by adding the
actual load address.

## Relocation

### Simple mode (text_size == 0)

Every relocated 32-bit word gets `load_addr` added to it. This is the
common case for contiguous text+data layouts.

### Split mode (text_size > 0)

When text and data may be loaded at different addresses (future XIP):
- Values < text_size reference the text segment -> add text_base
- Values >= text_size reference the data segment -> subtract text_size, add data_base

For contiguous loading, both modes produce identical results.

### BSS trick (zero extra RAM)

The relocation table is loaded into the BSS area temporarily. After
relocations are applied, BSS is zeroed (destroying the table). This
means relocation costs zero extra RAM. The kernel validates that
effective_bss >= reloc_count * 4 at header validation time.

## Build Flow

```
.c  ->  .o             (m68k-elf-gcc -m68000 -c)
.o  ->  .elf           (m68k-elf-ld -T user-reloc.ld --emit-relocs)
.elf -> genix binary   (tools/mkbin)
```

### Linker Script (`apps/user-reloc.ld`)

Links all user programs at address 0 (`. = 0`). The `--emit-relocs`
flag preserves R_68K_32 relocation entries in the ELF for mkbin to
extract. One linker script for both workbench and Mega Drive (replacing
the old separate `user.ld` and `user-md.ld`).

### mkbin (`tools/mkbin.c`)

Reads a 32-bit big-endian m68k ELF executable and produces the Genix
relocatable binary:

1. Validates ELF magic, class (32-bit), endianness (big), machine (68000)
2. Scans `PT_LOAD` segments to find: load base, total file-backed size,
   total memory size (includes BSS)
3. Copies segment data into a contiguous flat buffer
4. Scans `SHT_RELA` sections for relocation entries:
   - `R_68K_32` (type 1): **emitted** — absolute 32-bit address reference
   - `R_68K_16` (type 2): **error** — 16-bit absolute not supported
   - `R_68K_PC*` (types 4-6): **skipped** — PC-relative, self-fixing
   - `R_68K_GOT*`: **skipped** — GOT-relative, link-time resolved
5. Generates synthetic relocations for `.got` entries (see below)
6. Sorts relocation offsets (cache-friendly for runtime patching)
7. Deduplicates overlapping entries
8. Computes `text_size` from `.text` + `.rodata` section sizes
9. Computes `got_offset` from `.got` section VMA relative to data start
10. Writes: `[32B header][text+data][reloc table]`

The `bss_size` is computed as `total_memsz - total_filesz`.

### mkbin Validation Rules

mkbin performs several validation steps to reject malformed ELF inputs
and produce safe relocatable binaries:

**ELF validation:**
- Magic bytes must match `\x7fELF`
- Class must be 32-bit (`ELFCLASS32`)
- Endianness must be big-endian (`ELFDATA2MSB`)
- Machine must be m68k (`EM_68K = 4`)
- At least one `PT_LOAD` segment must exist

**Relocation type handling:**

| Type | Name | Action |
|------|------|--------|
| 1 | `R_68K_32` | **Emit** — absolute 32-bit address reference |
| 2 | `R_68K_16` | **Error** — 16-bit absolute not supported |
| 4 | `R_68K_PC32` | **Skip** — PC-relative, no patching needed |
| 5 | `R_68K_PC16` | **Skip** — PC-relative |
| 6 | `R_68K_PC8` | **Skip** — PC-relative |
| — | `R_68K_GOT*` | **Skip** — GOT-relative, link-time resolved |

**Alignment check:** Relocation offsets must be even. On the 68000,
an odd-aligned 32-bit access is always a fatal bus fault. mkbin
rejects odd offsets as an error (not a warning).

**Non-loaded section filtering:** Only `SHT_RELA` sections targeting
`SHF_ALLOC` sections are processed. This filters out `.rela.debug_*`
and other non-loaded relocation sections.

**Post-processing:**
- Relocation offsets are sorted by value (cache-friendly runtime access)
- Duplicate offsets are removed (can occur with merged sections)

**text_size computation:** Scans section headers for `SHT_PROGBITS`
sections with `SHF_EXECINSTR` flag. The maximum end address of these
sections becomes `text_size`, enabling the split text/data relocator.

### GOT Entry Relocations (`-msep-data`)

When apps are compiled with `-msep-data`, the linker creates a GOT
(Global Offset Table) containing absolute addresses of functions and
data. However, `--emit-relocs` does NOT emit R_68K_32 relocations for
these linker-generated GOT entries.

mkbin compensates by scanning the `.got` section in the flat binary.
Every non-zero 4-byte-aligned entry in `.got` gets a synthetic
relocation added to the relocation table. These synthetic relocs are
merged with the ELF-extracted relocs, sorted, and deduplicated.

### GOT Offset Encoding (`stack_size` Field)

The `stack_size` header field is split:
- **Bits 0-15:** Stack size hint (0 = default 4 KB)
- **Bits 16-31:** GOT offset from data section start, encoded as
  (offset + 1)

Encoding: 0 = no GOT (binary not compiled with `-msep-data`).
N = GOT at byte offset (N-1) from the start of the data section.

The kernel uses macros to decode:
```c
#define HDR_STACK_SIZE(hdr)  ((hdr)->stack_size & 0xFFFF)
#define HDR_HAS_GOT(hdr)    (((hdr)->stack_size >> 16) != 0)
#define HDR_GOT_OFFSET(hdr)  (((hdr)->stack_size >> 16) - 1)
```

The offset+1 encoding avoids ambiguity: GOT at offset 0 (common when
`.data` is empty) encodes as 1, distinguishing it from "no GOT" (0).

### Runtime Bounds Checking

The kernel validates each relocation offset at runtime before
dereferencing it. This protects against corrupt binaries (from SD card,
SRAM, or hand-crafted inputs).

Two checks per relocation entry:
- **Bounds:** `off + 4 > load_size` — offset must not straddle or
  exceed the loaded image. Bad entries are skipped with a kprintf warning.
- **Alignment:** `off & 1` — offset must be even. The 68000 faults on
  word/long access at odd addresses. Bad entries are skipped with a
  kprintf warning.

Both checks are applied in `apply_relocations()` and
`apply_relocations_xip()`.

## Loading Process (`kernel/exec.c`)

`load_binary(path, argv, load_addr, &entry, &user_sp)`:

1. Open file, read 32-byte header
2. Validate: magic = "GENX", entry < load_size (0-based offset),
   text_size <= load_size, total size fits in user memory,
   effective_bss >= reloc table size
3. Copy `load_size` bytes from file (offset 32) to `USER_BASE`
4. If `reloc_count > 0`: load relocation table into BSS area,
   apply relocations (add USER_BASE to each 32-bit word), then
   zero BSS (destroying the reloc table)
5. Set up user stack at `USER_TOP` with argc, argv, envp
6. Compute absolute entry: `USER_BASE + hdr.entry`

### User Stack Layout

Set up by `exec_setup_stack()`, growing downward from `USER_TOP`:

```
                    +----------------+
                    | string data    |  "path\0", "arg1\0", "arg2\0", ...
                    +----------------+  (4-byte aligned)
                    | NULL (envp)    |
                    | NULL (argv)    |
                    | argv[argc-1]   |  -> pointer to string
                    | ...            |
                    | argv[0]        |  -> pointer to path
                    | argc           |  (uint32_t)
               SP -> +----------------+
```

### User crt0.S (`apps/crt0.S`)

```asm
_start:
    move.l  (%sp)+, %d0         // argc
    move.l  %sp, %a0            // argv
    // calculate envp = argv + (argc+1) * 4
    move.l  %d0, %d1
    addq.l  #1, %d1
    lsl.l   #2, %d1
    lea     (%a0,%d1.l), %a1    // envp
    // Call main(argc, argv, envp)
    move.l  %a1, -(%sp)
    move.l  %a0, -(%sp)
    move.l  %d0, -(%sp)
    jsr     main
    lea     12(%sp), %sp
    // exit(return_value)
    move.l  %d0, %d1
    moveq   #1, %d0             // SYS_EXIT
    trap    #0
```

## exec_enter / exec_leave (`kernel/exec_asm.S`)

Single-tasking exec uses a setjmp/longjmp-style mechanism:

- **`exec_enter(entry, user_sp, kstack_top)`**: saves d2-d7/a2-a6 and
  kernel SP, switches to `user_sp`, jumps to entry point via `JMP`
  (not `JSR` — that would corrupt the argc/argv stack layout)
- **`exec_leave()`**: called from `do_exit()` when `exec_active` is set;
  restores kernel SP and saved registers, returns the exit code to
  `exec_enter()`'s caller

This avoids the complexity of a full context switch for single-tasking.

## Status

- **Phase 5: ROM XIP** — **Done.** `load_binary_xip()` executes text
  from ROM, copies only data+BSS to RAM. `romfix` resolves text-segment
  relocations at build time.
- **Phase 6: `-msep-data` + slot allocator** — **Done.** All apps
  compile with `-msep-data`. Slot allocator divides user RAM into
  fixed-size slots. `exec()` allocates a slot per process, sets a5 to
  GOT base. `romfix` defers data-segment relocations for runtime.
  mkbin generates synthetic GOT entry relocations. Concurrent pipelines.
- **Phase 8: EverDrive Pro PSRAM** — **Future.**
  `apply_relocations_xip()` is implemented and tested (11 host tests).
  Hardware integration (SRAM bank detection, per-process bank tracking,
  context switch bank register writes) deferred until EverDrive Pro
  becomes the active target. See
  [docs/relocatable-binaries.md](relocatable-binaries.md) for the full
  research.
