/*
 * Simple memory allocator — first-fit free list
 */
#include "kernel.h"

struct mem_block {
    uint32_t size;          /* including header */
    struct mem_block *next;
};

#define BLOCK_HDR_SIZE  ((uint32_t)sizeof(struct mem_block))
#define ALIGN(x)        (((x) + 3) & ~3)

static struct mem_block *free_list;
static uint32_t heap_start, heap_end;

void mem_init(uint32_t start, uint32_t end)
{
    heap_start = ALIGN(start);
    heap_end = end & ~3;

    free_list = (struct mem_block *)heap_start;
    free_list->size = heap_end - heap_start;
    free_list->next = NULL;
}

void *kmalloc(uint32_t size)
{
    size = ALIGN(size + BLOCK_HDR_SIZE);

    struct mem_block **pp = &free_list;
    while (*pp) {
        struct mem_block *p = *pp;
        if (p->size >= size) {
            if (p->size >= size + BLOCK_HDR_SIZE + 16) {
                /* Split */
                struct mem_block *newblk = (struct mem_block *)((char *)p + size);
                newblk->size = p->size - size;
                newblk->next = p->next;
                *pp = newblk;
                p->size = size;
            } else {
                /* Use whole block */
                *pp = p->next;
            }
            p->next = NULL;
            return (void *)((char *)p + BLOCK_HDR_SIZE);
        }
        pp = &p->next;
    }
    return NULL;
}

void kfree(void *ptr)
{
    if (!ptr) return;

    struct mem_block *blk = (struct mem_block *)((char *)ptr - BLOCK_HDR_SIZE);

    /* Insert in address order and coalesce */
    struct mem_block **pp = &free_list;
    while (*pp && *pp < blk)
        pp = &(*pp)->next;

    blk->next = *pp;
    *pp = blk;

    /* Coalesce with next */
    if (blk->next && (char *)blk + blk->size == (char *)blk->next) {
        blk->size += blk->next->size;
        blk->next = blk->next->next;
    }

    /* Coalesce with prev — need to re-find prev */
    if (pp != &free_list) {
        /* Walk from free_list to find prev */
        struct mem_block *prev = free_list;
        while (prev && prev->next != blk)
            prev = prev->next;
        if (prev && (char *)prev + prev->size == (char *)blk) {
            prev->size += blk->size;
            prev->next = blk->next;
        }
    }
}

/* Process brk/sbrk — uses per-process mem_base/mem_size from slot */
void *sbrk_proc(int32_t incr)
{
    if (!curproc || curproc->mem_base == 0)
        return (void *)-1;

    uint32_t old_brk = curproc->brk;
    uint32_t new_brk = old_brk + incr;
    uint32_t mem_top = curproc->mem_base + curproc->mem_size;

    /* Leave at least USER_STACK_DEFAULT bytes for the stack at the top */
    if (new_brk > mem_top - USER_STACK_DEFAULT || new_brk < curproc->mem_base)
        return (void *)-1;

    curproc->brk = new_brk;
    return (void *)old_brk;
}

/* ======== Kernel heap stats ======== */

void kmem_stats(uint32_t *total, uint32_t *free_bytes, uint32_t *largest)
{
    *total = heap_end - heap_start;
    *free_bytes = 0;
    *largest = 0;

    struct mem_block *p = free_list;
    while (p) {
        *free_bytes += p->size;
        if (p->size > *largest)
            *largest = p->size;
        p = p->next;
    }
}

/* ======== User memory allocator ======== */

/*
 * Variable-size first-fit allocator for user process memory.
 *
 * Instead of maintaining a separate free list, this allocator scans the
 * process table (proctab[]) to find used regions. The gaps between them
 * are free space. This works because:
 *   - Every active process already stores mem_base and mem_size
 *   - At most MAXPROC (16) processes exist — linear scan is trivial
 *   - No inline headers needed (process data starts at mem_base)
 *   - Coalescing is automatic: clearing mem_base makes the gap visible
 */

static uint32_t umem_base, umem_top;

void umem_init(void)
{
    umem_base = USER_BASE;
    umem_top = USER_TOP;
    kprintf("[umem] user pool 0x%x-0x%x (%d bytes)\n",
            umem_base, umem_top, umem_top - umem_base);
}

/*
 * Find a free region of at least `need` bytes. Returns base address or 0.
 *
 * Algorithm: collect all used regions from proctab, sort by address,
 * scan gaps for first fit. O(n^2) where n <= MAXPROC = 16.
 *
 * Phase 7/8 note: if SRAM provides a second user memory pool, this
 * function may need a pool parameter to select main RAM vs SRAM.
 */
uint32_t umem_alloc(uint32_t need)
{
    need = (need + 3) & ~3u;  /* 4-byte align */

    /* Collect used regions from proctab */
    uint32_t bases[MAXPROC], sizes[MAXPROC];
    int n = 0;
    for (int i = 0; i < MAXPROC; i++) {
        if (proctab[i].state != P_FREE && proctab[i].mem_base != 0) {
            bases[n] = proctab[i].mem_base;
            sizes[n] = proctab[i].mem_size;
            n++;
        }
    }

    /* Insertion sort by base address */
    for (int i = 1; i < n; i++) {
        uint32_t kb = bases[i], ks = sizes[i];
        int j = i - 1;
        while (j >= 0 && bases[j] > kb) {
            bases[j + 1] = bases[j];
            sizes[j + 1] = sizes[j];
            j--;
        }
        bases[j + 1] = kb;
        sizes[j + 1] = ks;
    }

    /* Scan gaps for first fit */
    uint32_t gap_start = umem_base;
    for (int i = 0; i < n; i++) {
        uint32_t gap = bases[i] - gap_start;
        if (gap >= need)
            return gap_start;
        gap_start = bases[i] + sizes[i];
    }

    /* Check gap after last region */
    if (umem_top - gap_start >= need)
        return gap_start;

    return 0;  /* no space */
}

/*
 * Free a user memory region. Just clears mem_base in the proc struct.
 * The gap becomes visible on the next umem_alloc scan automatically.
 */
void umem_free(uint32_t base)
{
    (void)base;  /* nothing to do — caller clears proc->mem_base */
}

/*
 * Report free space statistics for the user memory pool.
 */
void umem_stats(uint32_t *free_bytes, uint32_t *largest)
{
    *free_bytes = 0;
    *largest = 0;

    /* Same scan as umem_alloc but tracks stats instead of allocating */
    uint32_t bases[MAXPROC], sizes[MAXPROC];
    int n = 0;
    for (int i = 0; i < MAXPROC; i++) {
        if (proctab[i].state != P_FREE && proctab[i].mem_base != 0) {
            bases[n] = proctab[i].mem_base;
            sizes[n] = proctab[i].mem_size;
            n++;
        }
    }

    /* Sort by base */
    for (int i = 1; i < n; i++) {
        uint32_t kb = bases[i], ks = sizes[i];
        int j = i - 1;
        while (j >= 0 && bases[j] > kb) {
            bases[j + 1] = bases[j];
            sizes[j + 1] = sizes[j];
            j--;
        }
        bases[j + 1] = kb;
        sizes[j + 1] = ks;
    }

    uint32_t gap_start = umem_base;
    for (int i = 0; i < n; i++) {
        uint32_t gap = bases[i] - gap_start;
        *free_bytes += gap;
        if (gap > *largest)
            *largest = gap;
        gap_start = bases[i] + sizes[i];
    }

    /* Gap after last region */
    uint32_t gap = umem_top - gap_start;
    *free_bytes += gap;
    if (gap > *largest)
        *largest = gap;
}
