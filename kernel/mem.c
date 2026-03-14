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

/* ======== Slot allocator (Phase 6) ======== */

/*
 * Divide user RAM (USER_BASE..USER_TOP) into fixed-size slots.
 * Each process gets one slot for .data + .bss + heap + stack.
 * Text stays in ROM (XIP), shared by all processes.
 *
 * Slot size is USER_SIZE / num_slots, rounded down to 4-byte alignment.
 * Mega Drive: 2 slots × ~13.75 KB each.
 * Workbench: 8 slots × ~88 KB each.
 */

int num_slots;
static uint8_t  slot_used[MAX_SLOTS];
static uint32_t slot_bases[MAX_SLOTS];
static uint32_t slot_sz;

void slot_init(void)
{
    /* Choose slot count based on available user RAM.
     * Mega Drive (~27.5 KB): 2 slots.
     * Workbench (~704 KB): 8 slots. */
    if (USER_SIZE >= 128 * 1024)
        num_slots = MAX_SLOTS;
    else if (USER_SIZE >= 64 * 1024)
        num_slots = 4;
    else
        num_slots = 2;

    slot_sz = (USER_SIZE / num_slots) & ~3u;

    for (int i = 0; i < num_slots; i++) {
        slot_used[i] = 0;
        slot_bases[i] = USER_BASE + (uint32_t)i * slot_sz;
    }

    kprintf("[slot] %d slots, %d bytes each (0x%x-0x%x)\n",
            num_slots, slot_sz, USER_BASE, USER_BASE + num_slots * slot_sz);
}

int slot_alloc(void)
{
    for (int i = 0; i < num_slots; i++) {
        if (!slot_used[i]) {
            slot_used[i] = 1;
            return i;
        }
    }
    return -1;
}

void slot_free(int slot)
{
    if (slot >= 0 && slot < num_slots)
        slot_used[slot] = 0;
}

uint32_t slot_base(int slot)
{
    if (slot >= 0 && slot < num_slots)
        return slot_bases[slot];
    return 0;
}

uint32_t slot_size(void)
{
    return slot_sz;
}
