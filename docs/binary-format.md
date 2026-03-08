# Genix Binary Format

## Header (32 bytes, big-endian)

```c
struct genix_header {
    uint32_t magic;       // 0x47454E58 "GENX"
    uint32_t load_size;   // bytes to copy from file (text+data)
    uint32_t bss_size;    // bytes to zero after load_size
    uint32_t entry;       // absolute entry point address
    uint32_t stack_size;  // stack size hint (0 = default 4 KB)
    uint32_t flags;       // reserved, must be 0
    uint32_t reserved[2]; // pad to 32 bytes
};
```

The file on disk is: `[32-byte header][load_size bytes of text+data]`.

## Build Flow

```
.c  →  .o             (m68k-linux-gnu-gcc -m68000 -c)
.o  →  .elf           (m68k-linux-gnu-ld -T user.ld)
.elf → genix binary   (tools/mkbin)
```

### mkbin (`tools/mkbin.c`)

Reads a 32-bit big-endian m68k ELF executable, finds all `PT_LOAD`
segments, and produces the Genix flat binary:

1. Validates ELF magic, class (32-bit), endianness (big), machine (68000)
2. Scans `PT_LOAD` segments to find: load base, total file-backed size,
   total memory size (includes BSS)
3. Copies segment data into a contiguous flat buffer
4. Writes the 32-byte Genix header + flat data

The `bss_size` is computed as `total_memsz - total_filesz`.

### User Linker Script (`apps/user.ld`)

Links user programs at `USER_BASE` (0x040000). The entry point is
`_start` from `crt0.S`.

## Loading Process (`kernel/exec.c`)

`do_exec(path, argv)`:

1. Open file, read 32-byte header
2. Validate: magic = "GENX", entry within loaded region, total size
   fits in user memory
3. Copy `load_size` bytes from file (offset 32) to `USER_BASE`
4. Zero `bss_size` bytes after the loaded data
5. Set up user stack at `USER_TOP` with argc, argv, envp
6. `exec_enter()` — save kernel context, switch to user SP, `JSR` to
   entry point

### User Stack Layout

Set up by `exec_setup_stack()`, growing downward from `USER_TOP`:

```
                    ┌──────────────┐
                    │ string data  │  "path\0", "arg1\0", "arg2\0", ...
                    ├──────────────┤  (4-byte aligned)
                    │ NULL (envp)  │
                    │ NULL (argv)  │
                    │ argv[argc-1] │  → pointer to string
                    │ ...          │
                    │ argv[0]      │  → pointer to path
                    │ argc         │  (uint32_t)
               SP → └──────────────┘
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

- **`exec_enter(entry, user_sp)`**: saves d2-d7/a2-a6 and kernel SP,
  switches to `user_sp`, calls entry point via `JSR`
- **`exec_leave()`**: called from `do_exit()` when `exec_active` is set;
  restores kernel SP and saved registers, returns the exit code to
  `exec_enter()`'s caller

This avoids the complexity of a full context switch for single-tasking.

## Future: Fuzix a.out Format

The PLAN.md documents a transition to Fuzix's 16-byte a.out header for
access to 143+ pre-built Fuzix utilities. The current Genix format is
a stepping stone — the loader logic is similar, and switching headers
is straightforward when the Fuzix libc port is ready.

Key differences from Fuzix a.out:
- Genix: 32-byte header, 32-bit size fields, no relocations
- Fuzix: 16-byte header, 16-bit size fields, kernel-applied relocations

The Fuzix format adds relocation support needed for multitasking (loading
programs at dynamic addresses). See [multitasking.md](multitasking.md).
