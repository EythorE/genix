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
    uint32_t stack_size;  /* stack hint (0 = default 4KB) */
    uint32_t flags;       /* reserved, 0 */
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
5. Sorts relocation offsets (cache-friendly for runtime patching)
6. Deduplicates overlapping entries
7. Computes `text_size` from `.text` + `.rodata` section sizes
8. Writes: `[32B header][text+data][reloc table]`

The `bss_size` is computed as `total_memsz - total_filesz`.

## Loading Process (`kernel/exec.c`)

`load_binary(path, argv, &entry, &user_sp)`:

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

## Future Work

- **Phase 6: Dynamic load address** — parameterize `load_binary()` to
  accept a load address, enabling loading programs at different addresses
  for multitasking.
- **Phase 7: Split text/data and XIP** — use `text_size` field for
  execute-in-place from ROM with data in RAM (bank-swapping). See
  [docs/relocatable-binaries.md](relocatable-binaries.md) for the full
  research.
