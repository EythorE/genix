/*
 * Unit tests for kernel/mem.c
 *
 * The kernel allocator uses uint32_t for addresses (68000 is 32-bit).
 * On a 64-bit host we re-implement the same algorithm with uintptr_t
 * so the logic is testable. If the algorithm in mem.c changes, update here.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* --- Re-implementation of mem.c with host-compatible types --- */

struct mem_block {
    uintptr_t size;
    struct mem_block *next;
};

#define BLOCK_HDR_SIZE  ((uintptr_t)sizeof(struct mem_block))
#define ALIGN(x)        (((x) + 3) & ~(uintptr_t)3)

static struct mem_block *free_list;
static uintptr_t heap_start, heap_end;

static void mem_init(uintptr_t start, uintptr_t end)
{
    heap_start = ALIGN(start);
    heap_end = end & ~(uintptr_t)3;
    free_list = (struct mem_block *)heap_start;
    free_list->size = heap_end - heap_start;
    free_list->next = NULL;
}

static void *kmalloc(uintptr_t size)
{
    size = ALIGN(size + BLOCK_HDR_SIZE);
    struct mem_block **pp = &free_list;
    while (*pp) {
        struct mem_block *p = *pp;
        if (p->size >= size) {
            if (p->size >= size + BLOCK_HDR_SIZE + 16) {
                struct mem_block *newblk = (struct mem_block *)((char *)p + size);
                newblk->size = p->size - size;
                newblk->next = p->next;
                *pp = newblk;
                p->size = size;
            } else {
                *pp = p->next;
            }
            p->next = NULL;
            return (void *)((char *)p + BLOCK_HDR_SIZE);
        }
        pp = &p->next;
    }
    return NULL;
}

static void kfree(void *ptr)
{
    if (!ptr) return;
    struct mem_block *blk = (struct mem_block *)((char *)ptr - BLOCK_HDR_SIZE);
    struct mem_block **pp = &free_list;
    while (*pp && *pp < blk)
        pp = &(*pp)->next;
    blk->next = *pp;
    *pp = blk;
    if (blk->next && (char *)blk + blk->size == (char *)blk->next) {
        blk->size += blk->next->size;
        blk->next = blk->next->next;
    }
    if (pp != &free_list) {
        struct mem_block *prev = free_list;
        while (prev && prev->next != blk)
            prev = prev->next;
        if (prev && (char *)prev + prev->size == (char *)blk) {
            prev->size += blk->size;
            prev->next = blk->next;
        }
    }
}

/* --- Test infrastructure --- */

static uint8_t test_heap[8192] __attribute__((aligned(16)));

static void setup_heap(void)
{
    memset(test_heap, 0, sizeof(test_heap));
    mem_init((uintptr_t)test_heap, (uintptr_t)test_heap + sizeof(test_heap));
}

/* --- Tests --- */

static void test_init(void)
{
    setup_heap();
    ASSERT_NOT_NULL(free_list);
    ASSERT_NULL(free_list->next);
}

static void test_alloc_basic(void)
{
    setup_heap();
    void *p = kmalloc(64);
    ASSERT_NOT_NULL(p);
    ASSERT((uintptr_t)p >= (uintptr_t)test_heap);
    ASSERT((uintptr_t)p < (uintptr_t)test_heap + sizeof(test_heap));
}

static void test_alloc_aligned(void)
{
    setup_heap();
    void *p = kmalloc(13);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ((uintptr_t)p & 3, 0);
}

static void test_alloc_multiple(void)
{
    setup_heap();
    void *a = kmalloc(100);
    void *b = kmalloc(100);
    void *c = kmalloc(100);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_NOT_NULL(c);
    ASSERT(a != b);
    ASSERT(b != c);
    ASSERT(a != c);
}

static void test_alloc_too_large(void)
{
    setup_heap();
    void *p = kmalloc(sizeof(test_heap) + 1);
    ASSERT_NULL(p);
}

static void test_free_null(void)
{
    setup_heap();
    kfree(NULL);
    ASSERT(1);
}

static void test_free_reuse(void)
{
    setup_heap();
    void *a = kmalloc(64);
    ASSERT_NOT_NULL(a);
    kfree(a);
    void *b = kmalloc(64);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ((uintptr_t)a, (uintptr_t)b);
}

static void test_free_coalesce(void)
{
    setup_heap();
    void *a = kmalloc(64);
    void *b = kmalloc(64);
    void *c = kmalloc(64);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_NOT_NULL(c);

    kfree(a);
    kfree(b);
    kfree(c);

    /* After coalescing, should be able to allocate most of the heap */
    void *big = kmalloc(sizeof(test_heap) - 256);
    ASSERT_NOT_NULL(big);
}

static void test_exhaust_and_free(void)
{
    setup_heap();
    void *ptrs[128];
    int count = 0;
    for (int i = 0; i < 128; i++) {
        ptrs[i] = kmalloc(32);
        if (!ptrs[i]) break;
        count++;
    }
    ASSERT(count > 0);

    for (int i = 0; i < count; i++)
        kfree(ptrs[i]);

    void *p = kmalloc(32);
    ASSERT_NOT_NULL(p);
}

static void test_write_to_allocated(void)
{
    setup_heap();
    char *p = kmalloc(128);
    ASSERT_NOT_NULL(p);
    /* Should be able to write without corrupting allocator */
    memset(p, 'X', 128);
    char *q = kmalloc(128);
    ASSERT_NOT_NULL(q);
    ASSERT(p != q);
    /* First allocation still intact */
    for (int i = 0; i < 128; i++)
        ASSERT_EQ(p[i], 'X');
}

/* --- kmem_stats tests --- */

static void kmem_stats(uintptr_t *total, uintptr_t *free_bytes, uintptr_t *largest)
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

static void test_kmem_stats_initial(void)
{
    setup_heap();
    uintptr_t total, free_bytes, largest;
    kmem_stats(&total, &free_bytes, &largest);
    /* Initially all memory is free (one big block) */
    ASSERT_EQ(total, free_bytes);
    ASSERT_EQ(total, largest);
    ASSERT(total > 0);
}

static void test_kmem_stats_after_alloc(void)
{
    setup_heap();
    uintptr_t total, free_bytes, largest;
    kmem_stats(&total, &free_bytes, &largest);
    uintptr_t initial_free = free_bytes;

    void *p = kmalloc(100);
    ASSERT_NOT_NULL(p);

    kmem_stats(&total, &free_bytes, &largest);
    /* Free should have decreased */
    ASSERT(free_bytes < initial_free);
    /* Used = total - free should be >= 100 (plus header + alignment) */
    ASSERT(total - free_bytes >= 100);
}

static void test_kmem_stats_after_free(void)
{
    setup_heap();
    void *p = kmalloc(100);
    ASSERT_NOT_NULL(p);
    kfree(p);

    uintptr_t total, free_bytes, largest;
    kmem_stats(&total, &free_bytes, &largest);
    /* After freeing, should coalesce back to full heap */
    ASSERT_EQ(total, free_bytes);
}

static void test_kmem_stats_fragmentation(void)
{
    setup_heap();
    void *a = kmalloc(64);
    void *b = kmalloc(64);
    void *c = kmalloc(64);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_NOT_NULL(c);

    /* Free a and c but not b — creates two non-adjacent free blocks */
    kfree(a);
    kfree(c);

    uintptr_t total, free_bytes, largest;
    kmem_stats(&total, &free_bytes, &largest);
    /* largest should be less than free_bytes (fragmented) */
    ASSERT(largest < free_bytes);
    ASSERT(largest > 0);
}

/* --- Slot allocator tests (re-implementation of kernel/mem.c slot code) --- */

#define MAX_SLOTS 8

static int t_num_slots;
static uint8_t  t_slot_used[MAX_SLOTS];
static uint32_t t_slot_bases[MAX_SLOTS];
static uint32_t t_slot_sz;

static void t_slot_init(uint32_t user_base, uint32_t user_size, int nslots)
{
    t_num_slots = nslots;
    t_slot_sz = (user_size / nslots) & ~3u;
    for (int i = 0; i < nslots; i++) {
        t_slot_used[i] = 0;
        t_slot_bases[i] = user_base + (uint32_t)i * t_slot_sz;
    }
}

static int t_slot_alloc(void)
{
    for (int i = 0; i < t_num_slots; i++) {
        if (!t_slot_used[i]) { t_slot_used[i] = 1; return i; }
    }
    return -1;
}

static void t_slot_free(int slot)
{
    if (slot >= 0 && slot < t_num_slots)
        t_slot_used[slot] = 0;
}

static uint32_t t_slot_base(int slot)
{
    if (slot >= 0 && slot < t_num_slots)
        return t_slot_bases[slot];
    return 0;
}

static void test_slot_alloc_basic(void)
{
    t_slot_init(0x040000, 704 * 1024, 6);
    int s = t_slot_alloc();
    ASSERT(s >= 0);
    ASSERT_EQ(t_slot_base(s), 0x040000);
}

static void test_slot_alloc_sequential(void)
{
    t_slot_init(0x040000, 704 * 1024, 6);
    int s0 = t_slot_alloc();
    int s1 = t_slot_alloc();
    ASSERT(s0 >= 0);
    ASSERT(s1 >= 0);
    ASSERT(s0 != s1);
    ASSERT(t_slot_base(s0) != t_slot_base(s1));
}

static void test_slot_alloc_exhaustion(void)
{
    t_slot_init(0x040000, 704 * 1024, 2);
    int s0 = t_slot_alloc();
    int s1 = t_slot_alloc();
    int s2 = t_slot_alloc();
    ASSERT(s0 >= 0);
    ASSERT(s1 >= 0);
    ASSERT_EQ(s2, -1);  /* no slots left */
}

static void test_slot_free_reuse(void)
{
    t_slot_init(0x040000, 704 * 1024, 2);
    int s0 = t_slot_alloc();
    (void)t_slot_alloc();  /* s1 — fill the second slot */
    ASSERT_EQ(t_slot_alloc(), -1);  /* exhausted */
    t_slot_free(s0);
    int s2 = t_slot_alloc();
    ASSERT(s2 >= 0);  /* freed slot reusable */
    ASSERT_EQ(t_slot_base(s2), t_slot_base(s0));
}

static void test_slot_base_invalid(void)
{
    t_slot_init(0x040000, 704 * 1024, 4);
    /* Invalid slot indices return 0 (documented weak spot) */
    ASSERT_EQ(t_slot_base(-1), 0);
    ASSERT_EQ(t_slot_base(99), 0);
    ASSERT_EQ(t_slot_base(MAX_SLOTS), 0);
}

static void test_slot_free_invalid(void)
{
    t_slot_init(0x040000, 704 * 1024, 2);
    /* Freeing invalid slots should not corrupt state */
    t_slot_free(-1);
    t_slot_free(99);
    int s = t_slot_alloc();
    ASSERT(s >= 0);  /* allocator still works */
}

static void test_slot_sizing_megadrive(void)
{
    /* Mega Drive: ~27.5 KB, 2 slots */
    uint32_t user_size = 0xFFFE00 - 0xFF9000;  /* 28160 bytes */
    t_slot_init(0xFF9000, user_size, 2);
    ASSERT(t_slot_sz > 0);
    ASSERT_EQ(t_slot_sz & 3, 0);  /* 4-byte aligned */
    /* Both slots fit within user memory */
    ASSERT(t_slot_bases[0] >= 0xFF9000);
    ASSERT(t_slot_bases[1] + t_slot_sz <= 0xFFFE00);
}

static void test_slot_no_leak_on_free(void)
{
    /* Verify all slots can be allocated, freed, and re-allocated */
    t_slot_init(0x040000, 704 * 1024, 4);
    int slots[4];
    for (int i = 0; i < 4; i++) {
        slots[i] = t_slot_alloc();
        ASSERT(slots[i] >= 0);
    }
    ASSERT_EQ(t_slot_alloc(), -1);  /* exhausted */
    for (int i = 0; i < 4; i++)
        t_slot_free(slots[i]);
    /* All slots available again */
    for (int i = 0; i < 4; i++) {
        int s = t_slot_alloc();
        ASSERT(s >= 0);
    }
    ASSERT_EQ(t_slot_alloc(), -1);
}

int main(void)
{
    printf("test_mem:\n");

    RUN_TEST(test_init);
    RUN_TEST(test_alloc_basic);
    RUN_TEST(test_alloc_aligned);
    RUN_TEST(test_alloc_multiple);
    RUN_TEST(test_alloc_too_large);
    RUN_TEST(test_free_null);
    RUN_TEST(test_free_reuse);
    RUN_TEST(test_free_coalesce);
    RUN_TEST(test_exhaust_and_free);
    RUN_TEST(test_write_to_allocated);

    printf("\n--- kmem_stats ---\n");
    RUN_TEST(test_kmem_stats_initial);
    RUN_TEST(test_kmem_stats_after_alloc);
    RUN_TEST(test_kmem_stats_after_free);
    RUN_TEST(test_kmem_stats_fragmentation);

    printf("\n--- slot allocator ---\n");
    RUN_TEST(test_slot_alloc_basic);
    RUN_TEST(test_slot_alloc_sequential);
    RUN_TEST(test_slot_alloc_exhaustion);
    RUN_TEST(test_slot_free_reuse);
    RUN_TEST(test_slot_base_invalid);
    RUN_TEST(test_slot_free_invalid);
    RUN_TEST(test_slot_sizing_megadrive);
    RUN_TEST(test_slot_no_leak_on_free);

    TEST_REPORT();
}
