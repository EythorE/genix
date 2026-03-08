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

/* Process brk/sbrk — for single-tasking, just allocate from heap */
void *sbrk_proc(int32_t incr)
{
    static uint32_t cur_brk = 0;
    if (cur_brk == 0)
        cur_brk = heap_start + (heap_end - heap_start) / 2;

    uint32_t old_brk = cur_brk;
    uint32_t new_brk = cur_brk + incr;

    if (new_brk > heap_end || new_brk < heap_start)
        return (void *)-1;

    cur_brk = new_brk;
    return (void *)old_brk;
}
