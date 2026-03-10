/*
 * Unit tests for kernel/buf.c — block buffer cache
 *
 * Strategy: mock pal_disk_read/write with in-memory arrays,
 * then exercise bread/bwrite/brelse cache behavior.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* ---- Constants ---- */
#define BLOCK_SIZE 1024
#define NBUFS      4       /* small cache for testing eviction */

/* ---- Structures ---- */
struct buf {
    uint16_t blockno;
    uint8_t  dev;
    uint8_t  dirty;
    uint8_t  valid;
    uint8_t  _pad[3];
    uint8_t  data[BLOCK_SIZE];
};

/* ---- Mock disk ---- */
#define MOCK_NBLOCKS 32
static uint8_t mock_disk[MOCK_NBLOCKS][BLOCK_SIZE];
static int disk_read_count, disk_write_count;

void pal_disk_read(uint8_t dev, uint16_t blockno, uint8_t *buf)
{
    (void)dev;
    disk_read_count++;
    if (blockno < MOCK_NBLOCKS)
        memcpy(buf, mock_disk[blockno], BLOCK_SIZE);
    else
        memset(buf, 0, BLOCK_SIZE);
}

void pal_disk_write(uint8_t dev, uint16_t blockno, const uint8_t *buf)
{
    (void)dev;
    disk_write_count++;
    if (blockno < MOCK_NBLOCKS)
        memcpy(mock_disk[blockno], buf, BLOCK_SIZE);
}

/* Prevent kernel.h inclusion */
#define KERNEL_H

/* ---- Include the real buf.c ---- */
#include "../kernel/buf.c"

/* ---- Helper ---- */
static void reset(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    disk_read_count = 0;
    disk_write_count = 0;
    buf_init();
}

/* ================================================================
 * Tests
 * ================================================================ */

static void test_buf_init(void)
{
    reset();
    /* After init, all bufs should be invalid */
    for (int i = 0; i < NBUFS; i++) {
        ASSERT_EQ(bufs[i].valid, 0);
        ASSERT_EQ(bufs[i].dirty, 0);
    }
}

static void test_bread_reads_from_disk(void)
{
    reset();
    /* Put test data on disk */
    memset(mock_disk[5], 'X', BLOCK_SIZE);

    struct buf *b = bread(0, 5);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ(b->blockno, 5);
    ASSERT_EQ(b->dev, 0);
    ASSERT_EQ(b->valid, 1);
    ASSERT_EQ(b->data[0], 'X');
    ASSERT_EQ(b->data[BLOCK_SIZE - 1], 'X');
    ASSERT_EQ(disk_read_count, 1);
}

static void test_bread_cache_hit(void)
{
    reset();
    memset(mock_disk[3], 'A', BLOCK_SIZE);

    struct buf *b1 = bread(0, 3);
    ASSERT_EQ(disk_read_count, 1);

    struct buf *b2 = bread(0, 3);
    ASSERT_EQ(disk_read_count, 1);  /* no extra disk read — cache hit */
    ASSERT(b1 == b2);               /* same buffer */
}

static void test_bread_different_blocks(void)
{
    reset();
    memset(mock_disk[1], 'A', BLOCK_SIZE);
    memset(mock_disk[2], 'B', BLOCK_SIZE);

    struct buf *b1 = bread(0, 1);
    struct buf *b2 = bread(0, 2);
    ASSERT(b1 != b2);
    ASSERT_EQ(b1->data[0], 'A');
    ASSERT_EQ(b2->data[0], 'B');
    ASSERT_EQ(disk_read_count, 2);
}

static void test_bwrite_marks_clean(void)
{
    reset();
    struct buf *b = bread(0, 0);
    b->dirty = 1;
    bwrite(b);
    ASSERT_EQ(b->dirty, 0);
    ASSERT_EQ(disk_write_count, 1);
}

static void test_bwrite_noop_if_clean(void)
{
    reset();
    struct buf *b = bread(0, 0);
    b->dirty = 0;
    bwrite(b);
    ASSERT_EQ(disk_write_count, 0);  /* nothing written */
}

static void test_brelse_flushes_dirty(void)
{
    reset();
    struct buf *b = bread(0, 0);
    memset(b->data, 'Z', BLOCK_SIZE);
    b->dirty = 1;
    brelse(b);
    ASSERT_EQ(disk_write_count, 1);
    /* Data should be on disk */
    ASSERT_EQ(mock_disk[0][0], 'Z');
}

static void test_brelse_noop_if_clean(void)
{
    reset();
    struct buf *b = bread(0, 0);
    b->dirty = 0;
    brelse(b);
    ASSERT_EQ(disk_write_count, 0);
}

static void test_cache_eviction(void)
{
    reset();
    /* Fill all NBUFS (4) cache slots */
    for (int i = 0; i < NBUFS; i++) {
        memset(mock_disk[i], 'A' + i, BLOCK_SIZE);
        struct buf *b = bread(0, i);
        brelse(b);
    }
    ASSERT_EQ(disk_read_count, NBUFS);

    /* Read a new block — must evict one */
    memset(mock_disk[10], 'Q', BLOCK_SIZE);
    struct buf *b = bread(0, 10);
    ASSERT_EQ(b->data[0], 'Q');
    ASSERT_EQ(disk_read_count, NBUFS + 1);

    /* The evicted block's slot was reused */
    ASSERT_EQ(b->blockno, 10);
}

static void test_eviction_prefers_clean(void)
{
    reset();
    /* Fill all slots, mark slot 0 dirty, rest clean */
    for (int i = 0; i < NBUFS; i++) {
        struct buf *b = bread(0, i);
        if (i == 0)
            b->dirty = 1;
        /* Don't brelse — leave dirty flag as-is */
    }

    /* New block should evict a clean slot (not slot 0) */
    struct buf *b = bread(0, 20);
    ASSERT_EQ(b->blockno, 20);
    /* Slot 0 should still be in cache (it was dirty) */
    struct buf *b0 = bread(0, 0);
    ASSERT_EQ(b0->dirty, 1);
}

static void test_dirty_eviction_writes_back(void)
{
    reset();
    /* Fill all slots and mark all dirty */
    for (int i = 0; i < NBUFS; i++) {
        struct buf *b = bread(0, i);
        memset(b->data, 'D', BLOCK_SIZE);
        b->dirty = 1;
    }
    disk_write_count = 0;

    /* Force eviction — should write back dirty block */
    struct buf *b = bread(0, 20);
    (void)b;
    ASSERT(disk_write_count >= 1);
}

static void test_different_devices(void)
{
    reset();
    struct buf *b1 = bread(0, 5);
    struct buf *b2 = bread(1, 5);  /* different dev, same blockno */
    ASSERT(b1 != b2);  /* should be different cache entries */
}

/* ---- Main ---- */
int main(void)
{
    printf("test_buf:\n");

    RUN_TEST(test_buf_init);
    RUN_TEST(test_bread_reads_from_disk);
    RUN_TEST(test_bread_cache_hit);
    RUN_TEST(test_bread_different_blocks);
    RUN_TEST(test_bwrite_marks_clean);
    RUN_TEST(test_bwrite_noop_if_clean);
    RUN_TEST(test_brelse_flushes_dirty);
    RUN_TEST(test_brelse_noop_if_clean);
    RUN_TEST(test_cache_eviction);
    RUN_TEST(test_eviction_prefers_clean);
    RUN_TEST(test_dirty_eviction_writes_back);
    RUN_TEST(test_different_devices);

    TEST_REPORT();
}
