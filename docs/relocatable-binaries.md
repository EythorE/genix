# Relocatable Binaries and FUZIX App Compatibility

Deep analysis of relocatable binary support for Genix, FUZIX application
porting feasibility, and the EverDrive Pro's role in enabling a richer
software ecosystem.

**Date:** 2026-03-11

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Why Relocatable Binaries Matter](#why-relocatable-binaries-matter)
3. [Current Genix Binary Format](#current-genix-binary-format)
4. [FUZIX Binary Format for 68000](#fuzix-binary-format-for-68000)
5. [How FUZIX Handles Relocations](#how-fuzix-handles-relocations)
6. [FUZIX kdata, Stack, and Process Model](#fuzix-kdata-stack-and-process-model)
7. [Available FUZIX Applications](#available-fuzix-applications)
8. [Kernel Changes for FUZIX App Compatibility](#kernel-changes-for-fuzix-app-compatibility)
9. [EverDrive Pro and SD Card Impact](#everdrive-pro-and-sd-card-impact)
10. [Relocation Strategy Options](#relocation-strategy-options)
11. [Recommended Approach](#recommended-approach)
12. [Implementation Roadmap](#implementation-roadmap)

---

## Executive Summary

Relocatable binaries would give Genix three major benefits:

1. **Multitasking with multiple processes in RAM** — today all processes
   load at the same fixed `USER_BASE`, so only one can be in memory.
   Relocation lets each process load at a different address.

2. **Access to 200+ FUZIX applications** — FUZIX has a mature collection
   of Unix utilities (sed, sort, ed, less, more, diff, dc, sh, etc.)
   already built for the 68000. Adopting FUZIX's a.out format lets us
   run these with minimal porting effort.

3. **SD card filesystems on EverDrive hardware** — with the EverDrive
   Pro's FIFO file API or the Open EverDrive's SPI interface, programs
   can live on the SD card instead of being baked into ROM. Relocatable
   binaries mean programs loaded from SD don't need to know their load
   address at compile time.

The cost is modest: ~150 lines of kernel code for the relocation engine,
a new `elf2aout` host tool (borrowed from FUZIX), and syscall
compatibility work. The FUZIX approach (a.out header + appended
relocation table) is battle-tested on real 68000 hardware and is the
right model for Genix.

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
run multiple processes, but they must all have been loaded at the same
base. In practice, each new `exec()` overwrites the previous program.

With relocation, each process can be loaded at whatever address `kmalloc`
returns. On the workbench (704 KB user space), we could have 10+ small
processes in memory simultaneously. On the Mega Drive (27.5 KB user
space), 2-3 small processes. With SRAM (128 KB on Open EverDrive, up to
512 KB on EverDrive Pro), many more.

**2. Two separate builds are required.**

Programs must be compiled twice — once for workbench (`user.ld` at
0x040000) and once for Mega Drive (`user-md.ld` at 0xFF9000). A
relocatable binary works on both platforms with the same binary file.

**3. External programs (from SD card or SRAM) can't be loaded.**

A program on an SD card doesn't know in advance where free memory will
be. Without relocation, it can only run if it happens to be linked for
exactly the right address. With relocation, the kernel adjusts all
absolute addresses at load time.

### Who Else Does This?

- **FUZIX** — uses a.out with appended relocation tables on 68000
- **uClinux** — uses bFLT (binary flat) format with relocations for
  no-MMU ARM and m68k
- **ELKS** — uses a.out with relocations for 8086
- **Minix** — similar approach for 68000 (Atari ST, Amiga)

All of these are no-MMU systems that face the same constraint: multiple
processes in a flat address space require load-time relocation.

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
.c → .o (m68k-elf-gcc -m68000)
.o → .elf (m68k-elf-ld -T user.ld)
.elf → genix binary (tools/mkbin)
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

## FUZIX Binary Format for 68000

### a.out Header (64 bytes)

FUZIX uses a standard Unix a.out format for 32-bit targets (68000,
NS32K, ARM). Defined in `Kernel/include/a.out.h`:

```c
struct exec {
    uint32_t a_midmag;    /* flags<<26 | mid<<16 | magic */
    uint32_t a_text;      /* text segment size */
    uint32_t a_data;      /* data segment size */
    uint32_t a_bss;       /* BSS size */
    uint32_t a_syms;      /* symbol table size (0 for stripped) */
    uint32_t a_entry;     /* entry point (offset from text base) */
    uint32_t a_trsize;    /* text relocation table size in bytes */
    uint32_t a_drsize;    /* data relocation table size in bytes */
    uint32_t stacksize;   /* stack size request */
    uint32_t unused[7];   /* pad to 64 bytes */
};
```

**Key fields:**

| Field | Purpose |
|-------|---------|
| `a_midmag` | `MID_FUZIX68000 (0x03D0)` + `NMAGIC (0410)` |
| `a_text` | Size of code segment |
| `a_data` | Size of initialized data |
| `a_bss` | Size of uninitialized data |
| `a_entry` | Entry point offset (relative to text base) |
| `a_trsize` | Bytes of text relocations (4 bytes each) |
| `a_drsize` | Bytes of data relocations (4 bytes each) |
| `stacksize` | Stack allocation request |

**On-disk layout:**

```
[64-byte header][text][data][text relocations][data relocations]
```

### Build Pipeline

```
.c → .o (m68k-elf-gcc -m68000 -fno-PIC)
.o → .elf (m68k-elf-ld)
.elf → FUZIX a.out (elf2aout tool)
```

The `elf2aout` tool (in FUZIX's `Standalone/` directory):
1. Reads ELF section headers
2. Identifies text (SHF_EXECINSTR), data, and BSS sections
3. Processes ELF relocations (R_68K_32, R_68K_PC32)
4. Converts to simple 4-byte relocation entries
5. Outputs the a.out header + segments + relocation tables

### Comparison with Genix

| Feature | Genix (current) | FUZIX a.out |
|---------|----------------|-------------|
| Header size | 32 bytes | 64 bytes |
| Relocations | None | Appended after data |
| Entry point | Absolute address | Offset from base |
| Text/data split | Combined | Separate sizes |
| Stack hint | In header | In header |
| Platform-independent | No (fixed address) | Yes (relocatable) |
| Relocation cost | 0 | ~100-400 cycles per entry |

---

## How FUZIX Handles Relocations

### Relocation Table Format

Each relocation entry is a simple **4-byte big-endian offset** into the
loaded program. The offset points to a 32-bit word that contains an
absolute address which must be adjusted.

```
Relocation entry: uint32_t offset_from_text_base
```

For a program with `a_trsize = 24` (6 text relocations) and
`a_drsize = 8` (2 data relocations):

```
[header 64B][text][data][6×4B text relocs][2×4B data relocs]
```

### How Relocations Are Applied

At load time in `syscall_exec32.c`:

1. Kernel decides where to load the program (call it `load_addr`)
2. Binary was compiled assuming base address 0 (or some link base)
3. The difference `delta = load_addr - link_base` is the relocation
   offset
4. For each relocation entry at offset `off`:
   - Read the 32-bit word at `load_addr + off`
   - Add `delta` to it
   - Write it back
5. Done — all absolute pointers now point to the right addresses

```c
/* Pseudocode for relocation */
void relocate(uint8_t *base, uint32_t *relocs, int nrelocs,
              int32_t delta) {
    for (int i = 0; i < nrelocs; i++) {
        uint32_t off = relocs[i];
        uint32_t *ptr = (uint32_t *)(base + off);
        *ptr += delta;  /* adjust absolute address */
    }
}
```

### What Gets Relocated

The compiler generates absolute references for:

- **Global variable addresses** — `static int x; ... &x`
- **String literal pointers** — `const char *s = "hello"`
- **Function pointers** — `void (*fp)(void) = my_func`
- **Switch jump tables** — GCC's computed goto tables
- **vtable-like structures** — arrays of function pointers

PC-relative references (branches, short calls) do NOT need relocation
because they're position-independent by nature. On the 68000, `BSR`,
`BRA`, and `Bcc` instructions use PC-relative addressing and don't
appear in the relocation table.

### Performance Cost

For a typical 4 KB program:
- ~50-100 relocation entries (200-400 bytes)
- Each entry: one memory read + one add + one memory write = ~30 cycles
- Total: ~1,500-3,000 cycles = ~0.2-0.4 ms at 7.67 MHz
- **Negligible** — program loading from disk is the bottleneck

For a larger 16 KB program: ~200-400 entries, ~1-2 ms. Still negligible.

### Size Overhead

Relocation tables add to the binary size:
- Small program (2 KB): +100-200 bytes (~5-10% overhead)
- Medium program (8 KB): +400-800 bytes (~5-10%)
- Large program (20 KB): +800-1600 bytes (~4-8%)

On ROM this is acceptable. On the Mega Drive's 64 KB RAM, only the
program's text+data+bss is loaded — relocation tables are read from
ROM/disk, applied, and discarded. They don't consume RAM.

---

## FUZIX kdata, Stack, and Process Model

Understanding FUZIX's internals helps us decide what to adopt and what
to skip.

### udata (Per-Process Kernel Data)

FUZIX uses a `struct u_data` (historically called "udata" or "u area")
for each process. This is the 68000 equivalent of Unix's `struct user`:

```c
/* Simplified — FUZIX's actual struct is ~1024 bytes */
struct u_data {
    /* Process identity */
    struct p_tab *u_ptab;     /* back-pointer to process table */
    uint16_t u_pid;

    /* Memory layout */
    uint32_t u_codebase;      /* where this process's code is loaded */
    uint32_t u_database;      /* data segment base */
    uint32_t u_break;         /* current brk */
    uint32_t u_sp;            /* saved user stack pointer */

    /* Syscall state */
    uint16_t u_callno;        /* current syscall number */
    int32_t  u_retval;        /* syscall return value */
    int32_t  u_error;         /* errno */
    uint32_t u_argn;          /* syscall arguments */
    uint32_t u_argn1, u_argn2, u_argn3;

    /* File descriptors */
    struct oft *u_files[UFTSIZE];  /* per-process fd table */
    uint16_t u_cwd;           /* current directory inode */

    /* Signals */
    uint32_t u_sigvec[NSIG];  /* signal handlers */

    /* Exec info */
    char u_name[8];           /* program name */
};
```

**Key difference from Genix:** FUZIX keeps `udata` as a separate 1024-byte
block per process, swapped in/out with the process. Genix embeds all
per-process state in `struct proc` (which includes the 512-byte kernel
stack). Genix's approach is simpler and doesn't need swapping.

### Stack Management

**FUZIX on 68000:**
- Each process gets a fixed allocation: text + data + BSS + stack + udata
- User stack grows down from the top of the allocation
- Kernel stack: FUZIX uses the SSP (supervisor stack pointer) which
  switches automatically on TRAP. Each process has its own kernel stack
  area within its udata block.
- `CONFIG_STACKSIZE = 1024` on the megadrive platform

**Genix:**
- Each process has a 512-byte kernel stack embedded in `struct proc`
- User stack grows down from `USER_TOP`
- SSP/USP separation handled by the 68000's supervisor/user mode

### Process Model

**FUZIX megadrive:**
- `CONFIG_FLAT` memory model — single flat address space
- `PTABSIZE = 32` process table entries
- Processes allocated from a flat heap via `kmemaddblk()`
- RAM split into blocks registered with the memory allocator
- `PROGBASE = udata.u_codebase` — each process has its own base
- The kernel loads the 32-byte VDSO (virtual DSO) at each process's
  codebase, containing syscall trampoline code

**Key insight:** FUZIX on megadrive does NOT swap processes to disk.
With CONFIG_FLAT, all processes that fit in RAM stay resident. If RAM
is exhausted, new processes simply fail to start. This is exactly how
Genix works too.

### What Genix Should NOT Copy from FUZIX

1. **The udata swap mechanism** — Genix doesn't need it. Our `struct proc`
   already holds everything per-process. No need for a separate udata
   block.

2. **Multi-user support** — UIDs, permissions, setuid/setgid. Genix is
   single-user by design.

3. **The VDSO** — FUZIX installs a 32-byte trampoline at each process's
   codebase for syscall entry. Genix uses TRAP #0 directly from libc
   stubs, which is simpler and doesn't waste 32 bytes per process.

4. **fork()** — FUZIX's 68000 port has a problematic fork()
   implementation (this was one reason Genix was created). Genix's
   vfork()+exec() model is correct for no-MMU.

### What Genix SHOULD Copy from FUZIX

1. **The a.out header format** — it's a standard, well-understood format
   with good tooling.

2. **The relocation table format** — simple, efficient, proven.

3. **The `elf2aout` tool** — converts standard ELF output to the a.out
   format with relocations. We'd adapt this as `tools/elf2aout.c`.

4. **Per-process `codebase`** — store the load address in `struct proc`
   (we already have `mem_base`).

5. **The exec flow** — load header, allocate memory, load segments,
   apply relocations, set up stack, enter user mode.

---

## Available FUZIX Applications

FUZIX has a large collection of applications already compiled for the
68000. Here's what's available and what would be most valuable for Genix:

### Applications/util — POSIX Utilities (~120 programs)

**Already in Genix (34):** basename, cat, cmp, comm, cut, dirname, echo,
env, expand, expr, false, fold, grep, head, ls, nl, od, paste, rev, seq,
sleep, strings, tac, tail, tee, tr, true, unexpand, uniq, wc, yes, imshow

**High-value additions from FUZIX (not in Genix):**

| Program | Lines | Value | Notes |
|---------|-------|-------|-------|
| **sed** | ~1500 | Critical | Stream editor, used in scripts |
| **sort** | ~800 | Critical | Sort lines, fundamental utility |
| **ed** | ~1200 | High | Line editor, the standard Unix editor |
| **diff** | ~600 | High | File comparison |
| **less/more** | ~400 | High | Pager, much better than cat |
| **cp** | ~200 | High | File copy |
| **mv** | ~150 | High | File move/rename |
| **rm** | ~100 | High | File remove |
| **mkdir** | ~50 | Medium | Already a shell builtin |
| **chmod** | ~100 | Medium | Permissions (less useful single-user) |
| **dd** | ~300 | Medium | Block-level copy |
| **du/df** | ~200 | Medium | Disk usage |
| **tar** | ~500 | Medium | Archive tool |
| **touch** | ~50 | Medium | Update timestamps |
| **date** | ~100 | Medium | Show/set date |
| **kill** | ~50 | Medium | Already a syscall, but useful as command |
| **ps** | ~150 | Medium | Process listing |
| **stty** | ~200 | Medium | Terminal settings |
| **xargs** | ~200 | Medium | Build command lines from stdin |
| **cal** | ~150 | Low | Calendar |
| **factor** | ~100 | Low | Prime factorization |
| **id/who** | ~100 | Low | User identity (less useful single-user) |

### Applications/V7 — V7 Unix Commands

| Program | Value | Notes |
|---------|-------|-------|
| **sh** | Critical | The Bourne shell (the big prize) |
| **dc** | Medium | Desk calculator |
| **col** | Medium | Column filter |
| **join** | Medium | Relational join |
| **pr** | Medium | Format for printing |
| **split** | Medium | Split files |
| **test** | High | Conditional evaluation (for shell scripts) |
| **time** | Low | Time a command |

### Applications/levee — Vi Clone

Already partially ported to Genix. FUZIX has a complete build with
68000-specific Makefile.

### Applications/games

startrek, fortune, 2048, sokoban, Infocom Z-machine interpreter (z1-z5),
Scott Adams adventures, cowsay. Fun but low priority.

### Total Count

| Category | Programs | Already in Genix |
|----------|----------|-----------------|
| POSIX utilities | ~120 | 34 |
| V7 commands | ~40 | 0 |
| Editors | 3 (levee, ed, ue) | 1 (levee) |
| Games | ~30 | 0 |
| Dev tools | ~10 (as, ld, cc, yacc) | 0 |
| **Total** | **~200+** | **35** |

---

## Kernel Changes for FUZIX App Compatibility

To run FUZIX applications on Genix with **no or trivial source edits**,
we need compatibility at three levels: binary format, syscall ABI, and
libc.

### Level 1: Binary Format (Kernel Change)

**Change: Support FUZIX a.out header in the exec loader.**

The kernel needs to recognize both formats:

```c
int load_binary(const char *path, ...) {
    /* Read first 4 bytes to detect format */
    if (magic == GENIX_MAGIC)
        return load_genix_binary(...);  /* existing path */
    if (is_fuzix_aout(header))
        return load_aout_binary(...);   /* new path */
    return -ENOEXEC;
}
```

The a.out loader:
1. Read 64-byte header, validate `a_midmag`
2. Allocate `a_text + a_data + a_bss + stacksize` bytes via `kmalloc`
3. Read text segment into allocated memory
4. Read data segment after text
5. Read relocation tables into a temporary buffer (or into BSS area)
6. Apply relocations: for each 4-byte entry, add `delta` to the word
   at that offset
7. Zero BSS
8. Set up stack, enter user mode

**Estimated size:** ~100-150 lines of C.

### Level 2: Syscall ABI Compatibility

FUZIX and Genix use different syscall conventions:

| Aspect | Genix | FUZIX |
|--------|-------|-------|
| Trap | TRAP #0 | TRAP #14 |
| Syscall number | d0 | d0 |
| Arguments | d1-d4 | d1-d4 |
| Return | d0 | d0 |

**The syscall number register and argument registers are identical.**
The only difference is the trap vector number.

**Options:**

**Option A: Dual trap handler (recommended)**
Install a handler on both TRAP #0 and TRAP #14 that dispatches to the
same `syscall_dispatch()`. Cost: ~5 lines of assembly. FUZIX binaries
work unmodified.

**Option B: Recompile FUZIX apps with TRAP #0**
Change one line in FUZIX's `crt0_68000.S`. Requires rebuilding all apps
but no kernel change.

Option A is better — it lets us run pre-built FUZIX binaries without
recompilation.

### Level 3: Syscall Number Compatibility

FUZIX and Genix use different syscall numbers. We need to map them.

**Syscalls that match (same number):**

| Syscall | Genix # | FUZIX # | Match? |
|---------|---------|---------|--------|
| exit | 1 | 0 | No |
| read | 3 | 7 | No |
| write | 4 | 8 | No |
| open | 5 | 1 | No |
| close | 6 | 2 | No |

Unfortunately, almost no syscall numbers match between Genix and FUZIX.

**Solution: Syscall translation table.**

When a TRAP #14 arrives (FUZIX binary), use a different dispatch table
that maps FUZIX syscall numbers to Genix implementations:

```c
/* FUZIX syscall numbers → Genix handlers */
static const syscall_fn fuzix_syscall_table[] = {
    [0]  = sys_exit,      /* __exit */
    [1]  = sys_open,      /* open */
    [2]  = sys_close,     /* close */
    [3]  = sys_rename,    /* rename */
    [5]  = sys_link,      /* link (stub) */
    [6]  = sys_unlink,    /* unlink */
    [7]  = sys_read,      /* read */
    [8]  = sys_write,     /* write */
    [10] = sys_lseek,     /* lseek */
    [11] = sys_chdir,     /* chdir */
    [14] = sys_stat,      /* stat */
    [15] = sys_fstat,     /* fstat */
    [17] = sys_dup,       /* dup */
    [18] = sys_getpid,    /* getpid */
    [24] = sys_ioctl,     /* ioctl */
    [26] = sys_signal,    /* signal */
    [29] = sys_kill,      /* kill */
    [30] = sys_pipe,      /* pipe */
    [33] = sys_exec,      /* execve */
    [35] = sys_brk,       /* brk */
    [39] = sys_mkdir,     /* mkdir */
    [41] = sys_waitpid,   /* waitpid */
    [43] = sys_dup2,      /* dup2 */
    [46] = sys_chroot,    /* chroot (stub) */
    [47] = sys_fcntl,     /* fcntl */
    [52] = sys_getcwd,    /* _getdirent */
    [56] = sys_vfork,     /* vfork */
    /* ... etc */
};
```

**Estimated size:** ~50-80 lines of C (the table + dispatch wrapper).

### Level 4: Syscall Semantics Differences

Some syscalls behave differently between FUZIX and Genix:

| Syscall | Difference | Impact |
|---------|-----------|--------|
| `open` | FUZIX uses different O_* flag values | Need translation layer |
| `stat` | FUZIX's `struct stat` layout differs | Need translation or matching struct |
| `ioctl` | FUZIX has different ioctl numbers for termios | Need translation |
| `signal` | FUZIX uses different signal numbers | Minor mapping needed |
| `brk/sbrk` | FUZIX uses `brk(addr)`, Genix uses `sbrk(incr)` | Implement both |
| `getdirent` | FUZIX uses custom format, Genix uses `getdents` | Translation needed |
| `_getfsys` | FUZIX-specific | Return -ENOSYS |
| `setuid/getuid` | FUZIX multi-user | Return 0 (we're always root) |

**The critical path:** Most FUZIX utilities only use read, write, open,
close, lseek, exit, brk, and stat. Getting these right covers 90% of
programs. The remaining 10% (ed, sed, sh) need ioctl/termios and signal
handling, which Genix already supports.

### Level 5: Libc Compatibility

FUZIX apps link against FUZIX's libc, which contains:
- stdio (printf, scanf, fopen, fread, etc.)
- stdlib (malloc, atoi, strtol, etc.)
- string (standard functions)
- ctype, getopt, regex, termios, etc.

**Two approaches:**

**Approach A: Build FUZIX apps against Genix libc**
Recompile FUZIX app sources with Genix's libc. The apps' `.c` files are
standard C — they should compile with trivial or no changes. The main
work is ensuring Genix's libc provides all needed functions.

**Approach B: Run pre-built FUZIX binaries**
The FUZIX binary already contains its libc statically linked. The kernel
just needs to handle the syscall ABI correctly. This works for programs
that only use basic syscalls but may fail for programs that depend on
FUZIX-specific libc internals.

**Recommendation:** Approach A (recompile) for apps we actively want.
It gives us control over the binary format and catches compatibility
issues at compile time. Approach B as a stretch goal for running
pre-built binaries.

---

## EverDrive Pro and SD Card Impact

### How SD Access Changes the Equation

Without SD card access, all programs must fit in ROM. The Genix ROM is
currently ~550 KB, with the romdisk filesystem taking most of the
remaining ~3.5 MB of cartridge space. With 34 programs already in ROM,
there's room for maybe 50-100 more (depending on size).

With SD card access (either EverDrive Pro FIFO or Open EverDrive SPI):

- Programs live on the SD card filesystem (FAT or minifs)
- Only the currently running program(s) need to be in RAM
- The full 200+ FUZIX utility collection could be available
- Users can add their own programs without reflashing the ROM
- Programs can read/write files on the SD card

### EverDrive Pro Advantages

The Pro's FIFO command interface (see `docs/everdrive-sd-card.md`) is
particularly valuable for relocatable binaries:

1. **`CMD_FILE_OPEN` + `CMD_FILE_READ`**: Load a binary from SD into RAM
   at whatever address kmalloc provides. The kernel applies relocations
   and the program runs.

2. **High throughput**: The Pro's MCU handles SPI, so file reads are
   fast — programs load in milliseconds.

3. **FAT filesystem for free**: The Pro's MCU handles FAT16/FAT32, so
   Genix just opens files by path. No FAT code in the kernel.

4. **Large SRAM in SSF mode**: 512 KB of word-wide SRAM, enough for
   dozens of small processes or a few large ones.

### Open EverDrive Advantages

The Open EverDrive's bit-bang SPI is slower but fully open-source:

1. **128 KB SRAM**: Enough for 5-10 small processes
2. **SD card access**: At ~20-100 KB/s, loading a 4 KB program takes
   40-200 ms. Acceptable for a CLI OS.
3. **No special ROM header**: Works with standard `SEGA MEGA DRIVE` header
4. **minifs on SD**: Format the SD card with Genix's own filesystem,
   avoiding the need for FAT code

### Relocatable Binaries + SD Card = Full Unix Experience

The combination enables a workflow like:

```
> ls /sd/bin         # List programs on SD card
> /sd/bin/sed 's/foo/bar/' file.txt
> /sd/bin/sort < data.txt | /sd/bin/uniq
> /sd/bin/diff file1.txt file2.txt
```

Programs load from SD into RAM at dynamic addresses, run, and exit.
Multiple programs can be in RAM for pipelines. This is the full Unix
experience on a Sega Mega Drive.

---

## Relocation Strategy Options

### Option 1: FUZIX a.out with Relocation Tables (Recommended)

**Format:** Standard a.out header + text + data + relocation entries.

**Pros:**
- Battle-tested on real 68000 hardware (FUZIX, uClinux heritage)
- Simple implementation (~100-150 lines)
- Direct access to 200+ FUZIX applications
- Relocation tables don't consume RAM (read from disk, apply, discard)
- Standard tooling (`elf2aout` already exists)
- Separate text/data sizes enable future read-only text optimization

**Cons:**
- 64-byte header is larger than Genix's 32-byte header
- Relocation tables add 5-10% to binary file size on disk
- Must port/adapt the `elf2aout` tool

**Load-time cost:** ~0.2-2 ms per program. Negligible.

### Option 2: Position-Independent Code (PIC / -fPIC)

**Format:** No relocations needed — all code uses PC-relative addressing.

**Pros:**
- No relocation tables, smaller binaries
- Instant loading (no fixup pass)

**Cons:**
- 68000 PIC is expensive: every global access goes through a GOT
  (Global Offset Table) loaded via `lea (PC,xxx),Ax`
- Code size increases 15-30% due to indirect addressing
- Runtime overhead on every global variable access (~4 extra cycles)
- No existing tooling or app ecosystem
- GCC's 68000 PIC support is poor (originally designed for 68020+)
- Not compatible with FUZIX binaries

**Verdict:** Wrong for 68000. PIC was designed for systems with good
PC-relative addressing (ARM, x86-64). The 68000's limited PC-relative
modes make it expensive.

### Option 3: bFLT (Binary Flat Format, uClinux-style)

**Format:** Flat binary with GOT-based relocations or direct relocations.

**Pros:**
- Used by uClinux on 68000 (ColdFire)
- Well-documented format

**Cons:**
- Different from FUZIX's format — no app compatibility
- More complex than a.out (multiple relocation types)
- uClinux tooling is heavy (elf2flt has many dependencies)
- Designed for ARM/ColdFire more than classic 68000

**Verdict:** Overkill for Genix. The a.out approach is simpler and gives
us FUZIX compatibility.

### Option 4: Keep Absolute Binaries, Use SRAM Banking

**Format:** Current Genix format, but load programs into different SRAM
banks (Open EverDrive, Pro SSF mode).

**Pros:**
- No changes to binary format
- Simpler kernel (no relocation engine)

**Cons:**
- Only works on EverDrive hardware with large SRAM
- Doesn't help workbench or standard cartridges
- Still need separate builds per load address
- Can't load programs from SD to arbitrary RAM addresses
- SRAM banking requires mapper register manipulation

**Verdict:** Not a general solution. Banking is useful as an additional
memory source but doesn't replace relocation.

### Option 5: Hybrid — Support Both Formats

**Format:** Accept both Genix flat binaries (for backward compatibility)
and FUZIX a.out (for relocatable binaries).

**Pros:**
- Existing programs keep working
- New programs get relocation support
- Gradual migration path
- Can run both Genix-native and FUZIX-ported applications

**Cons:**
- Two code paths in the loader (minor complexity)

**Verdict:** This is what we should actually do. Detect the format from
the magic number and dispatch to the right loader.

---

## Recommended Approach

### Adopt FUZIX a.out with Backward-Compatible Genix Header Support

**Phase 1: Dual-format loader** (~150 lines of kernel C)
- Detect magic number: `GENX` → existing loader, NMAGIC a.out → new loader
- Implement relocation engine (the inner loop is ~20 lines)
- Port FUZIX's `elf2aout` to `tools/elf2aout.c`
- Keep existing `mkbin` for backward compatibility

**Phase 2: TRAP #14 handler** (~10 lines of assembly)
- Install handler for FUZIX's TRAP #14 alongside TRAP #0
- Both dispatch to `syscall_dispatch()` with appropriate number mapping

**Phase 3: Syscall compatibility layer** (~80 lines of C)
- FUZIX syscall number → Genix syscall number translation table
- O_* flag translation for open()
- struct stat translation
- Stub out multi-user syscalls (getuid → 0, etc.)

**Phase 4: Port high-value FUZIX apps** (no kernel changes)
- Recompile FUZIX sources against Genix libc
- Priority: sed, sort, ed, diff, less, cp, mv, rm, sh
- Each app is a simple `apps/foo.c` addition

**Phase 5: SD card loading** (depends on SD card driver)
- Load binaries from SD card filesystem
- Relocate to dynamically allocated RAM
- Full Unix-like experience

### Why This Is the Right Approach for Genix

1. **Minimal kernel complexity** — the relocation engine is ~20 lines.
   The header parsing is ~50 lines. The syscall table is ~50 lines.
   Total: ~150 lines, well within Genix's "keep it small" philosophy.

2. **Proven format** — FUZIX has been running a.out on 68000 hardware
   for years. No need to invent a new format.

3. **Unlocks 200+ applications** — the real win is access to FUZIX's
   tested utility collection. sed, sort, ed, diff, sh — these are the
   programs that make an OS *usable*.

4. **Enables true multitasking** — relocatable binaries are a
   prerequisite for having multiple processes in memory at different
   addresses. Without them, Genix's multitasking is limited to
   sequential execution (one process at a time in user memory).

5. **Future-proof** — SD card access (Phase 5) transforms the Mega Drive
   from a fixed-ROM system into one where users can install software.
   This requires relocatable binaries.

---

## Implementation Roadmap

### Step 1: Relocation Engine + a.out Loader

**Files to add/modify:**
- `kernel/exec.c` — add `load_aout_binary()` alongside existing `load_binary()`
- `kernel/kernel.h` — add a.out header struct, NMAGIC/MID constants
- `tools/elf2aout.c` — new tool, adapted from FUZIX's `Standalone/elf2aout.c`
- `apps/Makefile` — option to build relocatable a.out binaries

**Testing:**
- Host tests: validate a.out header parsing and relocation math
- Workbench: load a relocatable hello-world at different addresses
- BlastEm: same test on Mega Drive

### Step 2: Dual TRAP Handler

**Files to modify:**
- `kernel/crt0.S` (workbench) — add TRAP #14 vector
- `pal/megadrive/crt0.S` — add TRAP #14 vector
- Both point to the same handler as TRAP #0

### Step 3: Syscall Translation

**Files to add/modify:**
- `kernel/proc.c` — add `fuzix_syscall_dispatch()` with number translation
- `kernel/compat.h` — FUZIX syscall numbers and flag definitions

### Step 4: Port FUZIX Apps

**Priority order (by user value):**
1. sed, sort, ed — core text processing
2. diff, less, more — file viewing/comparison
3. cp, mv, rm, mkdir, touch — file management
4. sh (V7 Bourne shell) — replaces built-in shell
5. dc, test, time — scripting support
6. ps, kill, stty — process/terminal management
7. Games (fortune, 2048) — fun

### Step 5: Multi-Address Loading

**Files to modify:**
- `kernel/exec.c` — use `kmalloc()` for process memory instead of fixed
  `USER_BASE`
- `kernel/proc.c` — per-process `mem_base` already exists in `struct proc`
- `kernel/mem.c` — ensure allocator handles multiple concurrent allocations

### Step 6: SD Card Integration

**Files to add:**
- `pal/megadrive/sd_*.c` — SD card drivers (per `docs/everdrive-sd-card.md`)
- `kernel/fatfs.c` — or use minifs on raw SD

This step depends on the SD card driver work documented in
`docs/everdrive-sd-card.md` and is independent of the relocation work.

---

## Appendix A: FUZIX Syscall Numbers (68000)

For reference, the FUZIX syscall numbers that must be mapped:

| # | Name | Genix equivalent |
|---|------|-----------------|
| 0 | _exit | SYS_EXIT (1) |
| 1 | open | SYS_OPEN (5) |
| 2 | close | SYS_CLOSE (6) |
| 3 | rename | SYS_RENAME (38) |
| 5 | link | stub (return -ENOSYS) |
| 6 | unlink | SYS_UNLINK (10) |
| 7 | read | SYS_READ (3) |
| 8 | write | SYS_WRITE (4) |
| 9 | _creat | open with O_CREAT|O_TRUNC |
| 10 | lseek | SYS_LSEEK (19) |
| 11 | chdir | SYS_CHDIR (12) |
| 12 | sync | stub (sync buffers) |
| 14 | stat | SYS_STAT (106) |
| 15 | fstat | SYS_FSTAT (108) |
| 16 | _access | stub (return 0, single-user) |
| 17 | dup | SYS_DUP (41) |
| 18 | getpid | SYS_GETPID (20) |
| 24 | ioctl | SYS_IOCTL (54) |
| 25 | _getfsys | stub (return -ENOSYS) |
| 26 | signal | SYS_SIGNAL (48) |
| 29 | kill | SYS_KILL (37) |
| 30 | pipe | SYS_PIPE (42) |
| 33 | execve | SYS_EXEC (11) |
| 34 | time | SYS_TIME (13) |
| 35 | brk | SYS_BRK (45) |
| 38 | getuid/getgid | return 0 |
| 39 | mkdir | SYS_MKDIR (39) |
| 41 | waitpid | SYS_WAITPID (7) |
| 43 | dup2 | SYS_DUP2 (63) |
| 47 | fcntl | SYS_FCNTL (55) |
| 56 | vfork | SYS_VFORK (190) |
| 57 | getdirent | SYS_GETDENTS (141) — needs format translation |
| 58 | read_all | read (convenience wrapper in libc) |
| 59 | write_all | write (convenience wrapper in libc) |

## Appendix B: Size Estimates

| Component | Code Size | RAM Cost |
|-----------|----------|----------|
| a.out header struct | ~20 lines | 0 (code only) |
| Header validation | ~50 lines | 0 |
| Relocation engine | ~30 lines | 0 (temp buffer) |
| a.out loader | ~80 lines | 0 |
| TRAP #14 handler | ~10 lines asm | 0 |
| Syscall translation table | ~60 lines | ~200 bytes |
| `elf2aout` host tool | ~400 lines | 0 (host only) |
| **Total kernel addition** | **~250 lines** | **~200 bytes** |

This is well within Genix's target of keeping the kernel under 5000
lines total. The current kernel is ~3000-5000 lines, so this would be
a ~5% increase.
