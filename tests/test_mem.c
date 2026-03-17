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

/* --- User memory allocator tests (re-implementation of kernel/mem.c umem code) ---
 *
 * The real umem_alloc scans proctab[] to find used regions. For host testing,
 * we simulate proctab with a simple array of {base, size, state} entries.
 */

#define MAXPROC 16
#define P_FREE    0
#define P_RUNNING 1

struct test_proc {
    uint8_t  state;
    uint32_t mem_base;
    uint32_t mem_size;
};

static struct test_proc t_proctab[MAXPROC];
static uint32_t t_umem_base, t_umem_top;

static void t_umem_init(uint32_t base, uint32_t top)
{
    t_umem_base = base;
    t_umem_top = top;
    for (int i = 0; i < MAXPROC; i++) {
        t_proctab[i].state = P_FREE;
        t_proctab[i].mem_base = 0;
        t_proctab[i].mem_size = 0;
    }
}

static uint32_t t_umem_alloc(uint32_t need)
{
    need = (need + 3) & ~3u;

    /* Collect used regions */
    uint32_t bases[MAXPROC], sizes[MAXPROC];
    int n = 0;
    for (int i = 0; i < MAXPROC; i++) {
        if (t_proctab[i].state != P_FREE && t_proctab[i].mem_base != 0) {
            bases[n] = t_proctab[i].mem_base;
            sizes[n] = t_proctab[i].mem_size;
            n++;
        }
    }

    /* Insertion sort by base */
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
    uint32_t gap_start = t_umem_base;
    for (int i = 0; i < n; i++) {
        uint32_t gap = bases[i] - gap_start;
        if (gap >= need)
            return gap_start;
        gap_start = bases[i] + sizes[i];
    }

    if (t_umem_top - gap_start >= need)
        return gap_start;

    return 0;
}

static void t_umem_stats(uint32_t *free_bytes, uint32_t *largest)
{
    *free_bytes = 0;
    *largest = 0;

    uint32_t bases[MAXPROC], sizes[MAXPROC];
    int n = 0;
    for (int i = 0; i < MAXPROC; i++) {
        if (t_proctab[i].state != P_FREE && t_proctab[i].mem_base != 0) {
            bases[n] = t_proctab[i].mem_base;
            sizes[n] = t_proctab[i].mem_size;
            n++;
        }
    }

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

    uint32_t gap_start = t_umem_base;
    for (int i = 0; i < n; i++) {
        uint32_t gap = bases[i] - gap_start;
        *free_bytes += gap;
        if (gap > *largest)
            *largest = gap;
        gap_start = bases[i] + sizes[i];
    }
    uint32_t gap = t_umem_top - gap_start;
    *free_bytes += gap;
    if (gap > *largest)
        *largest = gap;
}

/* Helper to simulate process allocation */
static int t_proc_alloc(uint32_t need)
{
    uint32_t base = t_umem_alloc(need);
    if (base == 0) return -1;
    for (int i = 0; i < MAXPROC; i++) {
        if (t_proctab[i].state == P_FREE) {
            t_proctab[i].state = P_RUNNING;
            t_proctab[i].mem_base = base;
            t_proctab[i].mem_size = need;
            return i;
        }
    }
    return -1;
}

static void t_proc_free(int idx)
{
    t_proctab[idx].state = P_FREE;
    t_proctab[idx].mem_base = 0;
    t_proctab[idx].mem_size = 0;
}

/* --- Tests --- */

static void test_umem_alloc_basic(void)
{
    t_umem_init(0x040000, 0x0F0000);
    uint32_t base = t_umem_alloc(1024);
    ASSERT(base != 0);
    ASSERT_EQ(base, 0x040000);
}

static void test_umem_alloc_alignment(void)
{
    t_umem_init(0x040000, 0x0F0000);
    uint32_t base = t_umem_alloc(13);
    ASSERT(base != 0);
    ASSERT_EQ(base & 3, 0);  /* 4-byte aligned */
}

static void test_umem_alloc_sequential(void)
{
    t_umem_init(0x040000, 0x0F0000);
    int p0 = t_proc_alloc(1000);
    int p1 = t_proc_alloc(2000);
    ASSERT(p0 >= 0);
    ASSERT(p1 >= 0);
    ASSERT(t_proctab[p0].mem_base != t_proctab[p1].mem_base);
    /* Second allocation starts after first */
    ASSERT_EQ(t_proctab[p1].mem_base, t_proctab[p0].mem_base + t_proctab[p0].mem_size);
}

static void test_umem_alloc_variable_sizes(void)
{
    /* Simulate Mega Drive: dash (5100) + ls (400) + more (650) */
    t_umem_init(0xFF9000, 0xFFFE00);
    int dash = t_proc_alloc(5100);
    int ls = t_proc_alloc(400);
    int more = t_proc_alloc(652);  /* 652 rounds to 652 -> aligned to 652 */
    ASSERT(dash >= 0);
    ASSERT(ls >= 0);
    ASSERT(more >= 0);
    /* All fit (5100 + 400 + 652 = 6152 << 28160 total) */
    uint32_t total_used = 5100 + 400 + 652;
    ASSERT(total_used < (0xFFFE00u - 0xFF9000u));
}

static void test_umem_alloc_exhaustion(void)
{
    t_umem_init(0xFF9000, 0xFFFE00);  /* 28160 bytes */
    int p0 = t_proc_alloc(20000);
    ASSERT(p0 >= 0);
    /* Only ~8160 left; allocating 10000 should fail */
    uint32_t base = t_umem_alloc(10000);
    ASSERT_EQ(base, 0);
}

static void test_umem_alloc_exact_fit(void)
{
    uint32_t total = 0xFFFE00u - 0xFF9000u;  /* 28160 */
    t_umem_init(0xFF9000, 0xFFFE00);
    /* Allocate exact total (after alignment) */
    int p = t_proc_alloc(total);
    ASSERT(p >= 0);
    /* No space left */
    ASSERT_EQ(t_umem_alloc(4), 0);
}

static void test_umem_free_reuse(void)
{
    t_umem_init(0x040000, 0x0F0000);
    int p0 = t_proc_alloc(1000);
    int p1 = t_proc_alloc(2000);
    ASSERT(p0 >= 0);
    ASSERT(p1 >= 0);

    uint32_t old_base = t_proctab[p0].mem_base;
    t_proc_free(p0);

    /* Should reuse the freed region */
    int p2 = t_proc_alloc(800);
    ASSERT(p2 >= 0);
    ASSERT_EQ(t_proctab[p2].mem_base, old_base);
}

static void test_umem_free_coalesce(void)
{
    t_umem_init(0x040000, 0x0F0000);
    int p0 = t_proc_alloc(10000);
    int p1 = t_proc_alloc(10000);
    int p2 = t_proc_alloc(10000);
    ASSERT(p0 >= 0);
    ASSERT(p1 >= 0);
    ASSERT(p2 >= 0);

    /* Free all three — gaps should coalesce into one big region */
    t_proc_free(p0);
    t_proc_free(p1);
    t_proc_free(p2);

    /* Should be able to allocate nearly the entire pool */
    uint32_t total = 0x0F0000u - 0x040000u;
    int p3 = t_proc_alloc(total - 100);
    ASSERT(p3 >= 0);
}

static void test_umem_gap_between_procs(void)
{
    /* Free a middle process and verify the gap is found */
    t_umem_init(0x040000, 0x0F0000);
    int p0 = t_proc_alloc(4000);
    int p1 = t_proc_alloc(4000);
    int p2 = t_proc_alloc(4000);
    ASSERT(p0 >= 0);
    ASSERT(p1 >= 0);
    ASSERT(p2 >= 0);

    uint32_t p1_base = t_proctab[p1].mem_base;
    t_proc_free(p1);

    /* Allocate into the gap left by p1 */
    int p3 = t_proc_alloc(3000);
    ASSERT(p3 >= 0);
    ASSERT_EQ(t_proctab[p3].mem_base, p1_base);
}

static void test_umem_gap_too_small(void)
{
    /* Create a gap that's too small, allocation should find the next gap */
    t_umem_init(0x040000, 0x0F0000);
    int p0 = t_proc_alloc(1000);
    int p1 = t_proc_alloc(1000);
    int p2 = t_proc_alloc(1000);
    ASSERT(p0 >= 0);
    ASSERT(p1 >= 0);
    ASSERT(p2 >= 0);

    t_proc_free(p1);  /* creates 1000-byte gap */

    /* 2000 doesn't fit in the 1000 gap — should go after p2 */
    int p3 = t_proc_alloc(2000);
    ASSERT(p3 >= 0);
    ASSERT(t_proctab[p3].mem_base >= t_proctab[p2].mem_base + t_proctab[p2].mem_size);
}

static void test_umem_fragmentation(void)
{
    /* Allocate alternating, free odds — creates checkerboard */
    t_umem_init(0x040000, 0x0F0000);
    int procs[8];
    for (int i = 0; i < 8; i++) {
        procs[i] = t_proc_alloc(4000);
        ASSERT(procs[i] >= 0);
    }
    /* Free odd indices */
    t_proc_free(procs[1]);
    t_proc_free(procs[3]);
    t_proc_free(procs[5]);
    t_proc_free(procs[7]);

    /* Each gap is 4000 bytes. Can allocate 4000 but not 5000 in any gap
     * (except after last proc, which might have more space) */
    int p = t_proc_alloc(4000);
    ASSERT(p >= 0);
    /* Should fit in the first gap (between proc[0] and proc[2]) */
    ASSERT_EQ(t_proctab[p].mem_base, t_proctab[procs[0]].mem_base + 4000);
}

static void test_umem_megadrive_pipeline(void)
{
    /* Realistic Mega Drive scenario: dash + ls | more */
    uint32_t md_base = 0xFF9000;
    uint32_t md_top = 0xFFFE00;
    uint32_t md_total = md_top - md_base;  /* 28160 */
    t_umem_init(md_base, md_top);

    /* dash with XIP: data+bss ~5100 bytes */
    int dash = t_proc_alloc(5100);
    ASSERT(dash >= 0);
    ASSERT_EQ(t_proctab[dash].mem_base, md_base);

    /* ls with XIP: ~400 bytes */
    int ls = t_proc_alloc(400);
    ASSERT(ls >= 0);

    /* more with XIP: ~650 bytes */
    int more = t_proc_alloc(652);
    ASSERT(more >= 0);

    /* All three fit — total ~6152 out of 28160 */
    uint32_t free_bytes, largest;
    t_umem_stats(&free_bytes, &largest);
    ASSERT(free_bytes > 20000);  /* Plenty of space left */
    ASSERT_EQ(free_bytes, md_total - 5100 - 400 - 652);
}

static void test_umem_stats_empty(void)
{
    t_umem_init(0x040000, 0x0F0000);
    uint32_t total = 0x0F0000u - 0x040000u;
    uint32_t free_bytes, largest;
    t_umem_stats(&free_bytes, &largest);
    ASSERT_EQ(free_bytes, total);
    ASSERT_EQ(largest, total);
}

static void test_umem_stats_partial(void)
{
    t_umem_init(0x040000, 0x0F0000);
    int p = t_proc_alloc(10000);
    ASSERT(p >= 0);

    uint32_t total = 0x0F0000u - 0x040000u;
    uint32_t free_bytes, largest;
    t_umem_stats(&free_bytes, &largest);
    ASSERT_EQ(free_bytes, total - 10000);
    ASSERT_EQ(largest, total - 10000);
}

static void test_umem_stats_fragmented(void)
{
    t_umem_init(0x040000, 0x0F0000);
    int p0 = t_proc_alloc(4000);
    int p1 = t_proc_alloc(4000);
    int p2 = t_proc_alloc(4000);
    ASSERT(p0 >= 0 && p1 >= 0 && p2 >= 0);

    t_proc_free(p1);  /* 4000-byte gap between p0 and p2 */

    uint32_t free_bytes, largest;
    t_umem_stats(&free_bytes, &largest);

    /* Free = gap (4000) + tail (total - 12000) */
    uint32_t total = 0x0F0000u - 0x040000u;
    ASSERT_EQ(free_bytes, total - 8000);
    /* Largest is the tail, not the gap */
    ASSERT(largest >= total - 12000);
    ASSERT(largest > 4000);
}

static void test_umem_alloc_zero(void)
{
    /* Allocating 0 should still return a valid aligned base */
    t_umem_init(0x040000, 0x0F0000);
    uint32_t base = t_umem_alloc(0);
    /* After alignment, need becomes 0 which is >= 0, so it should succeed.
     * This is an edge case — the allocator should return the pool base. */
    ASSERT(base != 0);
}

static void test_umem_many_procs(void)
{
    /* Fill all 16 proc slots with small allocations */
    t_umem_init(0x040000, 0x0F0000);
    int procs[MAXPROC];
    for (int i = 0; i < MAXPROC; i++) {
        procs[i] = t_proc_alloc(1000);
        ASSERT(procs[i] >= 0);
    }
    /* All 16 used — no more proc slots */
    int extra = t_proc_alloc(100);
    ASSERT_EQ(extra, -1);

    /* Free all and re-allocate */
    for (int i = 0; i < MAXPROC; i++)
        t_proc_free(procs[i]);
    int p = t_proc_alloc(1000);
    ASSERT(p >= 0);
    ASSERT_EQ(t_proctab[p].mem_base, 0x040000);
}

/* --- sbrk_proc vfork redirect tests (Bug 21) ---
 *
 * vfork children have mem_base==0. When they call sbrk, sbrk_proc must
 * redirect to the parent's region (the child shares the parent's address
 * space). Without this, pipelines like `ls bin | more` get "Out of space"
 * because dash's malloc can't extend the heap.
 */

#define P_VFORK   5
#define USER_STACK_DEFAULT  4096

struct sbrk_proc {
    uint8_t  state;
    uint8_t  pid;
    uint8_t  ppid;
    uint32_t mem_base;
    uint32_t mem_size;
    uint32_t brk;
};

static struct sbrk_proc s_proctab[MAXPROC];
static struct sbrk_proc *s_curproc;

static void *test_sbrk_proc(int32_t incr)
{
    struct sbrk_proc *p = s_curproc;
    if (!p)
        return (void *)(uintptr_t)-1;

    /* vfork redirect: child has mem_base==0, parent is P_VFORK */
    if (p->mem_base == 0 && p->ppid < MAXPROC) {
        struct sbrk_proc *parent = &s_proctab[p->ppid];
        if (parent->state == P_VFORK)
            p = parent;
    }

    if (p->mem_base == 0)
        return (void *)(uintptr_t)-1;

    uint32_t old_brk = p->brk;
    uint32_t new_brk = old_brk + incr;
    uint32_t mem_top = p->mem_base + p->mem_size;

    if (new_brk > mem_top - USER_STACK_DEFAULT || new_brk < p->mem_base)
        return (void *)(uintptr_t)-1;

    p->brk = new_brk;
    return (void *)(uintptr_t)old_brk;
}

static void test_sbrk_normal_proc(void)
{
    memset(s_proctab, 0, sizeof(s_proctab));
    s_proctab[0].state = P_RUNNING;
    s_proctab[0].pid = 0;
    s_proctab[0].ppid = 0;
    s_proctab[0].mem_base = 0x40000;
    s_proctab[0].mem_size = 0x20000;
    s_proctab[0].brk = 0x40000 + 0x1000;  /* brk at base + 4K */
    s_curproc = &s_proctab[0];

    void *r = test_sbrk_proc(256);
    ASSERT(r != (void *)(uintptr_t)-1);
    ASSERT_EQ((uint32_t)(uintptr_t)r, 0x40000 + 0x1000);
    ASSERT_EQ(s_proctab[0].brk, 0x40000 + 0x1000 + 256);
}

static void test_sbrk_vfork_child_redirects(void)
{
    memset(s_proctab, 0, sizeof(s_proctab));
    /* Parent: PID 0, P_VFORK (frozen) */
    s_proctab[0].state = P_VFORK;
    s_proctab[0].pid = 0;
    s_proctab[0].ppid = 0;
    s_proctab[0].mem_base = 0x40000;
    s_proctab[0].mem_size = 0x20000;
    s_proctab[0].brk = 0x40000 + 0x1000;

    /* Child: PID 1, no memory, parent = 0 */
    s_proctab[1].state = P_RUNNING;
    s_proctab[1].pid = 1;
    s_proctab[1].ppid = 0;
    s_proctab[1].mem_base = 0;
    s_proctab[1].mem_size = 0;
    s_proctab[1].brk = 0;
    s_curproc = &s_proctab[1];

    /* sbrk from vfork child should succeed using parent's region */
    void *r = test_sbrk_proc(256);
    ASSERT(r != (void *)(uintptr_t)-1);
    ASSERT_EQ((uint32_t)(uintptr_t)r, 0x40000 + 0x1000);
    /* Parent's brk should be updated */
    ASSERT_EQ(s_proctab[0].brk, 0x40000 + 0x1000 + 256);
}

static void test_sbrk_vfork_child_no_parent(void)
{
    memset(s_proctab, 0, sizeof(s_proctab));
    /* Child with no vfork parent — sbrk should fail */
    s_proctab[1].state = P_RUNNING;
    s_proctab[1].pid = 1;
    s_proctab[1].ppid = 0;
    s_proctab[1].mem_base = 0;
    s_proctab[1].mem_size = 0;
    s_proctab[1].brk = 0;

    /* Parent is P_RUNNING, not P_VFORK — no redirect */
    s_proctab[0].state = P_RUNNING;
    s_curproc = &s_proctab[1];

    void *r = test_sbrk_proc(256);
    ASSERT(r == (void *)(uintptr_t)-1);
}

static void test_sbrk_vfork_child_updates_parent_brk(void)
{
    memset(s_proctab, 0, sizeof(s_proctab));
    /* Parent with some existing heap usage */
    s_proctab[0].state = P_VFORK;
    s_proctab[0].pid = 0;
    s_proctab[0].ppid = 0;
    s_proctab[0].mem_base = 0x40000;
    s_proctab[0].mem_size = 0x20000;
    s_proctab[0].brk = 0x40000 + 0x8000;  /* 32K of heap already used */

    s_proctab[1].state = P_RUNNING;
    s_proctab[1].pid = 1;
    s_proctab[1].ppid = 0;
    s_proctab[1].mem_base = 0;
    s_proctab[1].mem_size = 0;
    s_curproc = &s_proctab[1];

    /* Multiple sbrk calls from child, all extend parent's brk */
    void *r1 = test_sbrk_proc(512);
    ASSERT(r1 != (void *)(uintptr_t)-1);
    void *r2 = test_sbrk_proc(512);
    ASSERT(r2 != (void *)(uintptr_t)-1);
    ASSERT_EQ(s_proctab[0].brk, 0x40000 + 0x8000 + 1024);

    /* Exhaustion: try to sbrk past the stack reserve */
    uint32_t remaining = s_proctab[0].mem_size - (s_proctab[0].brk - s_proctab[0].mem_base) - USER_STACK_DEFAULT;
    void *r3 = test_sbrk_proc(remaining + 1);
    ASSERT(r3 == (void *)(uintptr_t)-1);  /* should fail */
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

    printf("\n--- user memory allocator ---\n");
    RUN_TEST(test_umem_alloc_basic);
    RUN_TEST(test_umem_alloc_alignment);
    RUN_TEST(test_umem_alloc_sequential);
    RUN_TEST(test_umem_alloc_variable_sizes);
    RUN_TEST(test_umem_alloc_exhaustion);
    RUN_TEST(test_umem_alloc_exact_fit);
    RUN_TEST(test_umem_free_reuse);
    RUN_TEST(test_umem_free_coalesce);
    RUN_TEST(test_umem_gap_between_procs);
    RUN_TEST(test_umem_gap_too_small);
    RUN_TEST(test_umem_fragmentation);
    RUN_TEST(test_umem_megadrive_pipeline);
    RUN_TEST(test_umem_stats_empty);
    RUN_TEST(test_umem_stats_partial);
    RUN_TEST(test_umem_stats_fragmented);
    RUN_TEST(test_umem_alloc_zero);
    RUN_TEST(test_umem_many_procs);

    printf("\n--- sbrk_proc vfork redirect ---\n");
    RUN_TEST(test_sbrk_normal_proc);
    RUN_TEST(test_sbrk_vfork_child_redirects);
    RUN_TEST(test_sbrk_vfork_child_no_parent);
    RUN_TEST(test_sbrk_vfork_child_updates_parent_brk);

    TEST_REPORT();
}
