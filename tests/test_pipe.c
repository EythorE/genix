/*
 * Unit tests for kernel pipe implementation — stress and edge cases
 *
 * Tests the pipe_read/pipe_write logic re-implemented on the host.
 * The real kernel pipes use blocking sleep/wake which can't be tested
 * on the host; we test the buffer management and POSIX semantics.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* ---- Pipe constants and structures ---- */
#define PIPE_SIZE   512
#define EPIPE       32
#define EAGAIN      11

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

/* ---- Re-implement pipe_read/pipe_write for host testing ----
 * These are non-blocking versions matching kernel logic. */

static int pipe_write_nb(struct pipe *p, const void *buf, int len)
{
    if (p->readers == 0)
        return -EPIPE;

    const uint8_t *src = (const uint8_t *)buf;
    int total = 0;

    while (total < len) {
        if (p->count >= PIPE_SIZE)
            break;  /* full — non-blocking returns partial */
        if (p->readers == 0)
            return total > 0 ? total : -EPIPE;

        p->buf[p->write_pos % PIPE_SIZE] = src[total];
        p->write_pos++;
        p->count++;
        total++;
    }
    return total;
}

static int pipe_read_nb(struct pipe *p, void *buf, int len)
{
    uint8_t *dst = (uint8_t *)buf;
    int total = 0;

    while (total < len) {
        if (p->count == 0) {
            if (p->writers == 0)
                return total;  /* EOF */
            break;  /* empty, non-blocking */
        }
        dst[total] = p->buf[p->read_pos % PIPE_SIZE];
        p->read_pos++;
        p->count--;
        total++;
    }
    return total;
}

/* ---- Helper ---- */
static void pipe_init(struct pipe *p)
{
    memset(p, 0, sizeof(*p));
    p->readers = 1;
    p->writers = 1;
}

/* ================================================================
 * Tests
 * ================================================================ */

static void test_pipe_basic_rw(void)
{
    struct pipe p;
    pipe_init(&p);

    char msg[] = "hello";
    int n = pipe_write_nb(&p, msg, 5);
    ASSERT_EQ(n, 5);
    ASSERT_EQ(p.count, 5);

    char buf[16];
    n = pipe_read_nb(&p, buf, 16);
    ASSERT_EQ(n, 5);
    ASSERT(memcmp(buf, "hello", 5) == 0);
    ASSERT_EQ(p.count, 0);
}

static void test_pipe_fill_exact(void)
{
    struct pipe p;
    pipe_init(&p);

    /* Fill pipe to exactly PIPE_SIZE */
    uint8_t data[PIPE_SIZE];
    memset(data, 'A', PIPE_SIZE);
    int n = pipe_write_nb(&p, data, PIPE_SIZE);
    ASSERT_EQ(n, PIPE_SIZE);
    ASSERT_EQ(p.count, PIPE_SIZE);

    /* Read it all back */
    uint8_t buf[PIPE_SIZE];
    n = pipe_read_nb(&p, buf, PIPE_SIZE);
    ASSERT_EQ(n, PIPE_SIZE);
    ASSERT_EQ(buf[0], 'A');
    ASSERT_EQ(buf[PIPE_SIZE - 1], 'A');
    ASSERT_EQ(p.count, 0);
}

static void test_pipe_overflow(void)
{
    struct pipe p;
    pipe_init(&p);

    /* Fill to capacity */
    uint8_t data[PIPE_SIZE + 100];
    memset(data, 'B', sizeof(data));
    int n = pipe_write_nb(&p, data, PIPE_SIZE + 100);
    /* Non-blocking: should write only PIPE_SIZE bytes */
    ASSERT_EQ(n, PIPE_SIZE);
    ASSERT_EQ(p.count, PIPE_SIZE);
}

static void test_pipe_partial_read(void)
{
    struct pipe p;
    pipe_init(&p);

    char data[] = "0123456789";
    pipe_write_nb(&p, data, 10);

    /* Read only 4 bytes */
    char buf[4];
    int n = pipe_read_nb(&p, buf, 4);
    ASSERT_EQ(n, 4);
    ASSERT(memcmp(buf, "0123", 4) == 0);
    ASSERT_EQ(p.count, 6);

    /* Read remaining */
    char buf2[10];
    n = pipe_read_nb(&p, buf2, 10);
    ASSERT_EQ(n, 6);
    ASSERT(memcmp(buf2, "456789", 6) == 0);
    ASSERT_EQ(p.count, 0);
}

static void test_pipe_empty_read_no_writers(void)
{
    struct pipe p;
    pipe_init(&p);
    p.writers = 0;  /* write end closed */

    char buf[8];
    int n = pipe_read_nb(&p, buf, 8);
    ASSERT_EQ(n, 0);  /* EOF */
}

static void test_pipe_empty_read_with_writers(void)
{
    struct pipe p;
    pipe_init(&p);

    char buf[8];
    int n = pipe_read_nb(&p, buf, 8);
    ASSERT_EQ(n, 0);  /* no data, but writers exist → would block; nb returns 0 */
}

static void test_pipe_write_no_readers(void)
{
    struct pipe p;
    pipe_init(&p);
    p.readers = 0;  /* read end closed */

    char data[] = "broken";
    int n = pipe_write_nb(&p, data, 6);
    ASSERT_EQ(n, -EPIPE);
}

static void test_pipe_circular_wrap(void)
{
    struct pipe p;
    pipe_init(&p);

    /* Write 400 bytes, read them, then write 300 more.
     * write_pos will be at 700, which wraps around in the 512-byte buffer. */
    uint8_t data[400];
    memset(data, 'X', 400);
    pipe_write_nb(&p, data, 400);

    uint8_t buf[400];
    pipe_read_nb(&p, buf, 400);
    ASSERT_EQ(p.count, 0);
    ASSERT_EQ(p.read_pos, 400);
    ASSERT_EQ(p.write_pos, 400);

    /* Write 300 more — wraps past 512 */
    uint8_t data2[300];
    for (int i = 0; i < 300; i++)
        data2[i] = (uint8_t)(i & 0xFF);
    int n = pipe_write_nb(&p, data2, 300);
    ASSERT_EQ(n, 300);
    ASSERT_EQ(p.count, 300);

    /* Read back and verify data integrity after wrap */
    uint8_t buf2[300];
    n = pipe_read_nb(&p, buf2, 300);
    ASSERT_EQ(n, 300);
    for (int i = 0; i < 300; i++)
        ASSERT_EQ(buf2[i], (uint8_t)(i & 0xFF));
}

static void test_pipe_many_small_writes(void)
{
    struct pipe p;
    pipe_init(&p);

    /* Write 1 byte at a time up to capacity */
    for (int i = 0; i < PIPE_SIZE; i++) {
        uint8_t b = (uint8_t)(i & 0xFF);
        int n = pipe_write_nb(&p, &b, 1);
        ASSERT_EQ(n, 1);
    }
    ASSERT_EQ(p.count, PIPE_SIZE);

    /* One more should return 0 (full, non-blocking) */
    uint8_t extra = 0xFF;
    int n = pipe_write_nb(&p, &extra, 1);
    ASSERT_EQ(n, 0);

    /* Read all and verify order */
    for (int i = 0; i < PIPE_SIZE; i++) {
        uint8_t b;
        int r = pipe_read_nb(&p, &b, 1);
        ASSERT_EQ(r, 1);
        ASSERT_EQ(b, (uint8_t)(i & 0xFF));
    }
}

static void test_pipe_reader_close_mid_write(void)
{
    struct pipe p;
    pipe_init(&p);

    /* Write some data */
    char data[] = "partial";
    pipe_write_nb(&p, data, 7);

    /* Close reader while data is in buffer */
    p.readers = 0;

    /* Write should fail with EPIPE */
    int n = pipe_write_nb(&p, "more", 4);
    ASSERT_EQ(n, -EPIPE);

    /* Existing data is still in buffer (no reader to consume it) */
    ASSERT_EQ(p.count, 7);
}

static void test_pipe_zero_length(void)
{
    struct pipe p;
    pipe_init(&p);

    int n = pipe_write_nb(&p, "x", 0);
    ASSERT_EQ(n, 0);

    n = pipe_read_nb(&p, NULL, 0);
    ASSERT_EQ(n, 0);
}

static void test_pipe_alternating_rw(void)
{
    struct pipe p;
    pipe_init(&p);

    /* Simulate alternating write/read like a shell pipeline */
    for (int round = 0; round < 100; round++) {
        char msg[8];
        int len = snprintf(msg, sizeof(msg), "%d", round);

        int n = pipe_write_nb(&p, msg, len);
        ASSERT_EQ(n, len);

        char buf[8];
        n = pipe_read_nb(&p, buf, len);
        ASSERT_EQ(n, len);
        ASSERT(memcmp(buf, msg, len) == 0);
    }
}

/* ---- Main ---- */
int main(void)
{
    printf("test_pipe:\n");

    RUN_TEST(test_pipe_basic_rw);
    RUN_TEST(test_pipe_fill_exact);
    RUN_TEST(test_pipe_overflow);
    RUN_TEST(test_pipe_partial_read);
    RUN_TEST(test_pipe_empty_read_no_writers);
    RUN_TEST(test_pipe_empty_read_with_writers);
    RUN_TEST(test_pipe_write_no_readers);
    RUN_TEST(test_pipe_circular_wrap);
    RUN_TEST(test_pipe_many_small_writes);
    RUN_TEST(test_pipe_reader_close_mid_write);
    RUN_TEST(test_pipe_zero_length);
    RUN_TEST(test_pipe_alternating_rw);

    TEST_REPORT();
}
