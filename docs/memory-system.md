# Memory System

Complete documentation of Genix's memory management subsystems.

For the memory maps showing physical layout on each platform, see
[architecture.md](architecture.md).

---

## Overview

Genix has two memory allocators serving different pools:

1. **Kernel heap** (`kmalloc`/`kfree`) — for kernel data structures
   (proc table, buffer cache, open files, inodes). Uses a first-fit
   free list with coalescing. Pool: `pal_mem_start()` to `USER_BASE`.

2. **User memory** (`umem_alloc`/`umem_free`) — for process data
   (`.data` + `.bss` + heap + stack). Uses proc-table-scanned
   first-fit. Pool: `USER_BASE` to `USER_TOP`.

Both pools are initialized at boot and never overlap.

```
                    Kernel Heap                  User Memory
pal_mem_start() ─────────────── USER_BASE ─────────────── USER_TOP
                  kmalloc pool                umem pool
                  (kernel data)               (process data)
```

---

## Kernel Heap (kmalloc)

### Algorithm

Classic first-fit free list with inline headers and coalescing.

Each block (free or allocated) has an 8-byte header:
```c
struct mem_block {
    uint32_t size;          /* total block size including header */
    struct mem_block *next; /* next free block (only used when free) */
};
```

**Allocation** (`kmalloc(size)`):
1. Walk the free list looking for the first block >= `size + 8`
2. If the block is large enough to split (>= `size + 8 + 16`), split it
3. Return pointer past the header (`block + 8`)

**Deallocation** (`kfree(ptr)`):
1. Find the header at `ptr - 8`
2. Insert into free list in address order
3. Coalesce with adjacent free blocks (both next and previous)

### Typical usage

- Process table: `MAXPROC × sizeof(struct proc)` (~16 × ~300 B = ~4.8 KB)
- Buffer cache: 16 × 1 KB blocks + headers
- Open file table, inode cache

### Statistics

`kmem_stats()` reports total, free, and largest-free-block sizes.
Exposed to userspace via `SYS_MEMINFO`.

---

## User Memory (umem_alloc)

### Design: proc table as allocation metadata

Instead of maintaining a separate free list, the user memory allocator
scans the process table (`proctab[]`) to find which regions are in use.
Every active process has `mem_base` and `mem_size` fields that define
its allocated region. The gaps between allocated regions are free space.

This design was chosen because:
- **No extra data structures** — proctab already has all the info
- **No inline headers** — headers would be overwritten by process data
- **At most 16 processes** (MAXPROC) — linear scan is trivial
- **Automatic coalescing** — when a process exits and `mem_base` is
  cleared, the gap merges with adjacent gaps automatically

### Algorithm

**Initialization** (`umem_init()`):
- Records USER_BASE and USER_TOP. No other setup needed.

**Allocation** (`umem_alloc(need)`):
1. Scan all proctab entries, collect `{base, base+size}` for entries
   with `mem_base != 0`
2. Sort by base address (insertion sort, ≤16 elements)
3. Walk the gaps: before first region, between adjacent regions, and
   after last region to USER_TOP
4. Return the base of the first gap ≥ `need` (first-fit)
5. Return 0 if no gap is large enough

**Deallocation** (`umem_free(proc)`):
- Set `proc->mem_base = 0`. The gap appears automatically on the next
  `umem_alloc` scan.

### Region sizing

When a process is loaded, the kernel computes the region size from the
binary header:

- **Non-XIP**: `load_size + effective_bss + heap + stack`
- **XIP**: `data_size + bss_size + heap + stack` (text stays in ROM)

Where `stack` is `max(header.stack_size, USER_STACK_DEFAULT)` and
`heap` is `USER_HEAP_DEFAULT` (4 KB). The heap headroom ensures every
process can call `malloc`/`sbrk` — without it, brk starts exactly at
the stack reservation boundary and `sbrk` always fails (see HISTORY.md
Bug 18).

The process layout within its region:

```
mem_base                                        mem_base + mem_size
├── .data ── .bss ── heap→    ....    ←stack ──┤
                     ^brk                      ^stack_top
```

`sbrk_proc()` grows the heap (brk) upward. Stack grows down from
`mem_base + mem_size`. They share the gap between brk and stack_top,
minus `USER_STACK_DEFAULT` reserved for stack growth.

### Example: Mega Drive pipeline

```
USER_BASE (0xFF9000)                                    USER_TOP (0xFFFE00)
├── dash XIP (5,100) ──├── ls XIP (400) ──├── more XIP (650) ──├── free ──┤
```

Total used: ~6,150 B. Free: ~21,350 B. Compare with the old fixed-slot
allocator: 2 × 13,750 B slots, maximum 2 processes, 97% waste for `ls`.

### Fragmentation

With at most 3-4 concurrent processes on Mega Drive and mostly-LIFO
exit order (pipeline children exit, then parent spawns the next
command), fragmentation is negligible. Even in the worst case
(checkerboard of small allocations), the allocator behaves no worse
than fixed slots — a gap too small for the next process returns ENOMEM.

---

## Process Memory Lifecycle

### exec (synchronous, `do_exec`)

1. Read binary header to compute `need` (data + bss + stack)
2. `umem_alloc(need)` → `data_addr`
3. Set `curproc->mem_base = data_addr`, `curproc->mem_size = need`
4. Load binary into region (XIP or contiguous)
5. Run program synchronously (`exec_enter`)
6. On exit: `umem_free()` clears `mem_base`

### spawn (async, `do_spawn`)

1. `alloc_pid()` → child proc entry
2. Read binary header to compute `need`
3. `umem_alloc(need)` → `data_addr`
4. Set `child->mem_base = data_addr`, `child->mem_size = need`
5. Load binary into child's region
6. Build kstack, mark P_READY
7. On exit: `do_exit` calls `umem_free()`

### sbrk

`sbrk_proc(incr)` extends the heap within the process's allocated
region. Bounded by `mem_base + mem_size - USER_STACK_DEFAULT`.

---

## Memory Info Syscall (SYS_MEMINFO)

Reports kernel heap stats and per-process user memory info. Used by
the `meminfo` utility.

The `struct meminfo` contains:
- Kernel heap: total, free, largest block
- User memory: USER_BASE, USER_TOP
- Per-region info for each active process: base, size, pid, data+bss,
  brk, text_size (XIP)

---

## History: Fixed Slots → Variable-Size Allocation

The original Phase 6 implementation used a fixed-slot allocator that
divided user memory into equal-size slots (2 × 13,750 B on Mega Drive).
Fixed slots were a deliberate design choice — each process gets a
predictable data address, which maps cleanly onto EverDrive Pro's banked
PSRAM model where each 512 KB bank is also a fixed-size region.

In practice, the dominant workload is shell pipelines with many small
utilities on 27.5 KB of Mega Drive RAM. Fixed slots meant `ls` (400 B)
wasted 97% of its 13,750 B slot, and 3-process pipelines like
`ls | more` failed with ENOMEM because only 2 slots existed.

The replacement (`umem_alloc`) allocates exactly what each process needs
using the proc table as implicit metadata. The future EverDrive Pro bank
allocator will be a separate subsystem for 512 KB text banks — it
doesn't need the main-RAM allocator to mirror its structure.

See [decisions.md](plans/decisions.md) "Fixed-Slot → Variable-Size
Allocator" for the full rationale.
