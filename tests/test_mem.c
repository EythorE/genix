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

    TEST_REPORT();
}
