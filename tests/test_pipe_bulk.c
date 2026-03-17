/*
 * Unit tests for pipe bulk copy optimization
 *
 * The kernel pipe implementation uses memcpy of contiguous chunks
 * instead of byte-at-a-time copying. These tests exercise the
 * contiguous-chunk logic, especially around the circular buffer
 * wrap point.
 *
 * Re-implements the kernel's pipe_read/pipe_write (non-blocking,
 * no scheduler) for host testing.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* ---- Pipe constants and structures (match kernel/kernel.h) ---- */
#define PIPE_SIZE   512
#define EPIPE       32

struct pipe {
    uint8_t  buf[PIPE_SIZE];
    uint16_t read_pos;
    uint16_t write_pos;
    uint16_t count;
    uint8_t  readers;
    uint8_t  writers;
    uint8_t  read_waiting;
    uint8_t  write_waiting;
};

/* ---- Non-blocking pipe_read/pipe_write matching kernel bulk copy logic ---- */

static int pipe_write_bulk(struct pipe *p, const void *buf, int len)
{
    const uint8_t *src = (const uint8_t *)buf;
    int n = 0;

    if (p->readers == 0)
        return -EPIPE;

    while (n < len && p->count < PIPE_SIZE) {
        if (p->readers == 0)
            return n > 0 ? n : -EPIPE;
        int space = PIPE_SIZE - p->count;
        int want = len - n;
        int contig = PIPE_SIZE - p->write_pos;  /* bytes until wrap */
        int chunk = space < want ? space : want;
        if (chunk > contig)
            chunk = contig;
        memcpy(p->buf + p->write_pos, src + n, chunk);
        n += chunk;
        p->write_pos = (p->write_pos + chunk) & (PIPE_SIZE - 1);
        p->count += chunk;
    }

    return n;
}

static int pipe_read_bulk(struct pipe *p, void *buf, int len)
{
    uint8_t *dst = (uint8_t *)buf;
    int n = 0;

    /* Non-blocking: if empty and writers exist, return 0 (would block).
     * If empty and no writers, return 0 (EOF). */
    while (n < len && p->count > 0) {
        int avail = p->count;
        int want = len - n;
        int contig = PIPE_SIZE - p->read_pos;  /* bytes until wrap */
        int chunk = avail < want ? avail : want;
        if (chunk > contig)
            chunk = contig;
        memcpy(dst + n, p->buf + p->read_pos, chunk);
        n += chunk;
        p->read_pos = (p->read_pos + chunk) & (PIPE_SIZE - 1);
        p->count -= chunk;
    }

    return n;
}

/* ---- Helper ---- */
static void pipe_init(struct pipe *p)
{
    memset(p, 0, sizeof(*p));
    p->readers = 1;
    p->writers = 1;
}

/* Fill a buffer with a known pattern for verification */
static void fill_pattern(uint8_t *buf, int len, uint8_t seed)
{
    for (int i = 0; i < len; i++)
        buf[i] = (uint8_t)((seed + i) & 0xFF);
}

static int verify_pattern(const uint8_t *buf, int len, uint8_t seed)
{
    for (int i = 0; i < len; i++) {
        if (buf[i] != (uint8_t)((seed + i) & 0xFF))
            return 0;
    }
    return 1;
}

/* ================================================================
 * Tests
 * ================================================================ */

static void test_pipe_basic(void)
{
    struct pipe p;
    pipe_init(&p);

    int n = pipe_write_bulk(&p, "hello", 5);
    ASSERT_EQ(n, 5);
    ASSERT_EQ(p.count, 5);

    char buf[16];
    n = pipe_read_bulk(&p, buf, 16);
    ASSERT_EQ(n, 5);
    ASSERT(memcmp(buf, "hello", 5) == 0);
    ASSERT_EQ(p.count, 0);
}

static void test_pipe_large(void)
{
    struct pipe p;
    pipe_init(&p);

    /* Write exactly PIPE_SIZE bytes (full pipe) */
    uint8_t data[PIPE_SIZE];
    fill_pattern(data, PIPE_SIZE, 0x42);

    int n = pipe_write_bulk(&p, data, PIPE_SIZE);
    ASSERT_EQ(n, PIPE_SIZE);
    ASSERT_EQ(p.count, PIPE_SIZE);

    /* Read all back */
    uint8_t buf[PIPE_SIZE];
    n = pipe_read_bulk(&p, buf, PIPE_SIZE);
    ASSERT_EQ(n, PIPE_SIZE);
    ASSERT(verify_pattern(buf, PIPE_SIZE, 0x42));
    ASSERT_EQ(p.count, 0);
}

static void test_pipe_partial(void)
{
    struct pipe p;
    pipe_init(&p);

    /* Write 300 bytes */
    uint8_t data[300];
    fill_pattern(data, 300, 0x10);
    int n = pipe_write_bulk(&p, data, 300);
    ASSERT_EQ(n, 300);

    /* Read only 100 */
    uint8_t buf[100];
    n = pipe_read_bulk(&p, buf, 100);
    ASSERT_EQ(n, 100);
    ASSERT(verify_pattern(buf, 100, 0x10));
    ASSERT_EQ(p.count, 200);

    /* Read the remaining 200 */
    uint8_t buf2[200];
    n = pipe_read_bulk(&p, buf2, 200);
    ASSERT_EQ(n, 200);
    /* Pattern continues from offset 100 → seed + 100 */
    for (int i = 0; i < 200; i++)
        ASSERT_EQ(buf2[i], (uint8_t)((0x10 + 100 + i) & 0xFF));
    ASSERT_EQ(p.count, 0);
}

static void test_pipe_wrap(void)
{
    struct pipe p;
    pipe_init(&p);

    /* Write 400 bytes to advance write_pos to 400 */
    uint8_t data1[400];
    fill_pattern(data1, 400, 0xAA);
    pipe_write_bulk(&p, data1, 400);

    /* Read 300 to advance read_pos to 300, leaving 100 in buffer */
    uint8_t tmp[300];
    pipe_read_bulk(&p, tmp, 300);
    ASSERT_EQ(p.count, 100);
    ASSERT_EQ(p.read_pos, 300);
    ASSERT_EQ(p.write_pos, 400);

    /* Write 200 more — write_pos goes from 400 to 600, wrapping past 512.
     * This exercises the contiguous-chunk split at the wrap boundary. */
    uint8_t data2[200];
    fill_pattern(data2, 200, 0xBB);
    int n = pipe_write_bulk(&p, data2, 200);
    ASSERT_EQ(n, 200);
    ASSERT_EQ(p.count, 300);
    /* write_pos should wrap: (400 + 200) & 511 = 88 */
    ASSERT_EQ(p.write_pos, 88);

    /* Read remaining 300 bytes and verify data integrity.
     * First 100 bytes are the tail of data1 (offsets 300-399).
     * Next 200 bytes are data2. */
    uint8_t result[300];
    n = pipe_read_bulk(&p, result, 300);
    ASSERT_EQ(n, 300);

    /* Verify first 100: data1 pattern starting at offset 300 */
    for (int i = 0; i < 100; i++)
        ASSERT_EQ(result[i], (uint8_t)((0xAA + 300 + i) & 0xFF));

    /* Verify next 200: data2 pattern */
    ASSERT(verify_pattern(result + 100, 200, 0xBB));

    ASSERT_EQ(p.count, 0);
}

static void test_pipe_full_boundary(void)
{
    struct pipe p;
    pipe_init(&p);

    /* Fill pipe to exactly PIPE_SIZE */
    uint8_t data[PIPE_SIZE];
    fill_pattern(data, PIPE_SIZE, 0x55);
    int n = pipe_write_bulk(&p, data, PIPE_SIZE);
    ASSERT_EQ(n, PIPE_SIZE);
    ASSERT_EQ(p.count, PIPE_SIZE);

    /* Attempt to write one more byte — should return 0 (full) */
    uint8_t extra = 0xFF;
    n = pipe_write_bulk(&p, &extra, 1);
    ASSERT_EQ(n, 0);

    /* Read all back and verify */
    uint8_t buf[PIPE_SIZE];
    n = pipe_read_bulk(&p, buf, PIPE_SIZE);
    ASSERT_EQ(n, PIPE_SIZE);
    ASSERT(verify_pattern(buf, PIPE_SIZE, 0x55));
}

static void test_pipe_single_byte(void)
{
    struct pipe p;
    pipe_init(&p);

    /* Write and read single bytes, verify each */
    for (int i = 0; i < 64; i++) {
        uint8_t w = (uint8_t)(i * 3 + 7);
        int n = pipe_write_bulk(&p, &w, 1);
        ASSERT_EQ(n, 1);

        uint8_t r;
        n = pipe_read_bulk(&p, &r, 1);
        ASSERT_EQ(n, 1);
        ASSERT_EQ(r, w);
    }
    ASSERT_EQ(p.count, 0);
}

/* ---- Main ---- */
int main(void)
{
    printf("test_pipe_bulk:\n");

    RUN_TEST(test_pipe_basic);
    RUN_TEST(test_pipe_large);
    RUN_TEST(test_pipe_partial);
    RUN_TEST(test_pipe_wrap);
    RUN_TEST(test_pipe_full_boundary);
    RUN_TEST(test_pipe_single_byte);

    TEST_REPORT();
}
