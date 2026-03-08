# Kernel Subsystems

The entire kernel lives in `kernel/` with one `.c` file per subsystem
and a single header (`kernel.h`). Total: ~2000 lines of C + ~300 lines
of assembly.

## Memory Allocator (`mem.c`, ~100 lines)

First-fit free list with coalescing.

```c
struct mem_block {
    uint32_t size;          // including this header
    struct mem_block *next;
};
```

- **`mem_init(start, end)`** — creates a single free block spanning the
  entire heap
- **`kmalloc(size)`** — first-fit search; splits blocks if remainder is
  large enough (>= header + 16 bytes)
- **`kfree(ptr)`** — inserts in address order, coalesces with neighbors
- **`sbrk_proc(incr)`** — simple brk for user processes (single-tasking:
  just moves a pointer)

All allocations are 4-byte aligned. The minimum allocation overhead is
8 bytes (one `mem_block` header).

### Design notes

- Global state is fine for a single-user kernel
- No slab allocator or size classes — the heap is small (~200 KB on
  workbench, ~24 KB on Mega Drive) and fragmentation is manageable
- When multitasking is added, per-process memory will be allocated
  as contiguous blocks from kmalloc

## Buffer Cache (`buf.c`)

16 blocks of 1 KB each, statically allocated at boot.

```c
struct buf {
    uint16_t blockno;
    uint8_t  dev;
    uint8_t  dirty;
    uint8_t  valid;
    uint8_t  data[BLOCK_SIZE];  // 1024 bytes
};
```

- **`bread(dev, blockno)`** — returns a cached block, reading from disk
  if not valid
- **`bwrite(b)`** — writes a dirty block to disk
- **`brelse(b)`** — releases a buffer (currently a no-op; no eviction
  pressure with 16 buffers)

The cache is a flat array scanned linearly. With only 16 entries this is
fast enough. LRU eviction will be needed if the buffer count grows.

## Device Table (`dev.c`)

Simple function-pointer table:

```c
struct device {
    int (*open)(int minor);
    int (*close)(int minor);
    int (*read)(int minor, void *buf, int len);
    int (*write)(int minor, const void *buf, int len);
    int (*ioctl)(int minor, int cmd, void *arg);
};

// DEV_CONSOLE = 0, DEV_DISK = 1
struct device devtab[NDEV];
```

Console device delegates to `pal_console_putc/getc`. Disk device is
accessed through the buffer cache, not directly through devtab.

## Process Table (`proc.c`, ~340 lines)

```c
struct proc {
    uint8_t  state;     // P_FREE, P_RUNNING, P_READY, P_SLEEPING, P_ZOMBIE, P_VFORK
    uint8_t  pid;
    uint8_t  ppid;
    int8_t   exitcode;
    uint32_t sp, pc;
    uint32_t regs[16];  // d0-d7, a0-a7
    uint16_t sr;
    uint32_t mem_base, mem_size, brk;
    uint16_t cwd;
    struct ofile *fd[MAXFD];  // 16 file descriptors
};
```

- **MAXPROC = 16** process slots
- **MAXFD = 16** file descriptors per process
- **MAXOPEN = 64** system-wide open file entries

### Current state: single-tasking

Process 0 is the kernel/shell. `exec()` loads a user binary at
`USER_BASE`, transfers control via `exec_enter()`, and blocks until
the program calls `_exit()` which triggers `exec_leave()`.

`do_vfork()` and `do_waitpid()` return `-ENOSYS` / `-ECHILD` — stubs
for future multitasking.

### Syscall dispatch

All syscalls enter through `TRAP #0` (vector 32). The handler in
`crt0.S` saves registers, pushes d0-d4 as C arguments, and calls
`syscall_dispatch()` in `proc.c`. See [syscalls.md](syscalls.md).

## Binary Loader (`exec.c`, ~180 lines)

Loads Genix flat binaries. See [binary-format.md](binary-format.md)
for the format details.

1. Read 32-byte header from filesystem
2. Validate magic, sizes, entry point
3. Load text+data to `USER_BASE`
4. Zero BSS
5. Set up user stack (argc, argv, envp)
6. `exec_enter()` — save kernel context, switch to user stack, `JSR`
   to entry point
7. On `_exit()` → `exec_leave()` — restore kernel context, return
   exit code

The assembly in `exec_asm.S` uses `MOVEM.L` to save/restore 11
callee-saved registers (d2-d7, a2-a6) and the kernel stack pointer.

## Kernel Printf (`kprintf.c`)

Minimal printf supporting `%d`, `%u`, `%x`, `%s`, `%c`, `%%`.
No floating point. Output goes through `pal_console_putc()`.

## String Functions (`string.c`)

Standard implementations of `memset`, `memcpy`, `memcmp`, `strcmp`,
`strncmp`, `strlen`, `strcpy`, `strncpy`, `strchr`, `strrchr`.
These are compiled into the kernel binary directly (not from libc).

## Exception Handling (`crt0.S`)

The vector table at address 0 maps all 256 68000 exception vectors:

| Vector | Handler | Behavior |
|--------|---------|----------|
| 0-1 | SSP, PC | Boot (reset) |
| 2-3 | `_vec_buserr`, `_vec_addrerr` | Save regs, call `panic_exception()`, halt |
| 4-11 | Various | `RTE` (return, ignore) |
| 24 | `_vec_spurious` | `RTE` |
| 30 | `_vec_timer_irq` | Save d0-d1/a0-a1, call `timer_interrupt()`, restore, `RTE` |
| 32 | `_vec_syscall` | Syscall entry (TRAP #0) |

The timer ISR only saves caller-saved registers (4 regs via MOVEM)
because it calls a C function that preserves callee-saved regs per ABI.

## Built-in Shell (`main.c`)

Emergency shell for when no user shell is available. Commands:
`help`, `mem`, `ls [path]`, `cat <file>`, `echo <text>`,
`exec <file>`, `write <path> <text>`, `mkdir <path>`, `halt`.

The built-in shell will be replaced by a proper user-space shell
once the Fuzix libc port is complete.
