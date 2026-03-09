/*
 * Unit tests for process management and pipe logic
 *
 * Tests the pure logic on the host (no 68000 needed).
 * Pipe buffer management, process table operations.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* Re-define kernel constants for host testing */
#define MAXPROC     16
#define MAXFD       16
#define MAXOPEN     64
#define MAXINODE    128
#define MAXPIPE     4
#define PIPE_SIZE   512
#define NAME_MAX    30
#define BLOCK_SIZE  1024

/* Error numbers */
#define EPERM        1
#define ENOENT       2
#define EAGAIN      11
#define ENOMEM      12
#define EBADF        9
#define ECHILD      10
#define EPIPE       32
#define ENFILE      23

/* Process states */
#define P_FREE      0
#define P_RUNNING   1
#define P_READY     2
#define P_SLEEPING  3
#define P_ZOMBIE    4
#define P_VFORK     5

/* ======== Pipe logic (re-implemented for host testing) ======== */

struct pipe {
    uint8_t  buf[PIPE_SIZE];
    uint16_t read_pos;
    uint16_t write_pos;
    uint16_t count;
    uint8_t  readers;
    uint8_t  writers;
};

/* pipe_table not needed for host tests — logic is re-implemented above */

static int pipe_read(struct pipe *p, void *buf, int len)
{
    uint8_t *dst = (uint8_t *)buf;
    int n = 0;
    while (n < len) {
        if (p->count == 0) {
            if (p->writers == 0) return n;
            break;
        }
        dst[n++] = p->buf[p->read_pos];
        p->read_pos = (p->read_pos + 1) & (PIPE_SIZE - 1);
        p->count--;
    }
    return n;
}

static int pipe_write(struct pipe *p, const void *buf, int len)
{
    const uint8_t *src = (const uint8_t *)buf;
    int n = 0;
    if (p->readers == 0) return -EPIPE;
    while (n < len) {
        if (p->count >= PIPE_SIZE) break;
        p->buf[p->write_pos] = src[n++];
        p->write_pos = (p->write_pos + 1) & (PIPE_SIZE - 1);
        p->count++;
    }
    return n > 0 ? n : -EAGAIN;
}

/* ======== Process table logic (re-implemented for host testing) ======== */

struct proc {
    uint8_t  state;
    uint8_t  pid;
    uint8_t  ppid;
    int8_t   exitcode;
};

static struct proc proctab[MAXPROC];
static struct proc *curproc;
static int nproc;

static void proc_init_test(void)
{
    for (int i = 0; i < MAXPROC; i++) {
        proctab[i].state = P_FREE;
        proctab[i].pid = i;
    }
    curproc = &proctab[0];
    curproc->state = P_RUNNING;
    curproc->ppid = 0;
    nproc = 1;
}

static uint8_t next_pid = 1;

static uint8_t alloc_pid(void)
{
    for (int i = 0; i < MAXPROC; i++) {
        uint8_t p = next_pid;
        next_pid = (next_pid + 1) & (MAXPROC - 1);
        if (next_pid == 0) next_pid = 1;
        if (proctab[p].state == P_FREE)
            return p;
    }
    return 0xFF;
}

/* ======== Pipe tests ======== */

static void test_pipe_basic(void)
{
    struct pipe p = {0};
    p.readers = 1;
    p.writers = 1;

    char wbuf[] = "hello";
    int nw = pipe_write(&p, wbuf, 5);
    ASSERT_EQ(nw, 5);
    ASSERT_EQ(p.count, 5);

    char rbuf[8] = {0};
    int nr = pipe_read(&p, rbuf, 8);
    ASSERT_EQ(nr, 5);
    ASSERT_EQ(p.count, 0);
    ASSERT_STR_EQ(rbuf, "hello");
}

static void test_pipe_empty_read(void)
{
    struct pipe p = {0};
    p.readers = 1;
    p.writers = 1;

    char rbuf[8];
    int nr = pipe_read(&p, rbuf, 8);
    ASSERT_EQ(nr, 0);  /* empty, but writers exist → 0 bytes */
}

static void test_pipe_eof(void)
{
    struct pipe p = {0};
    p.readers = 1;
    p.writers = 0;  /* no writers */

    char rbuf[8];
    int nr = pipe_read(&p, rbuf, 8);
    ASSERT_EQ(nr, 0);  /* EOF */
}

static void test_pipe_broken(void)
{
    struct pipe p = {0};
    p.readers = 0;  /* no readers */
    p.writers = 1;

    char wbuf[] = "fail";
    int nw = pipe_write(&p, wbuf, 4);
    ASSERT_EQ(nw, -EPIPE);
}

static void test_pipe_full(void)
{
    struct pipe p = {0};
    p.readers = 1;
    p.writers = 1;

    /* Fill the pipe */
    uint8_t fill[PIPE_SIZE];
    memset(fill, 'A', PIPE_SIZE);
    int nw = pipe_write(&p, fill, PIPE_SIZE);
    ASSERT_EQ(nw, PIPE_SIZE);
    ASSERT_EQ(p.count, PIPE_SIZE);

    /* Try to write more — should get EAGAIN */
    char extra = 'B';
    nw = pipe_write(&p, &extra, 1);
    ASSERT_EQ(nw, -EAGAIN);
}

static void test_pipe_wrap(void)
{
    struct pipe p = {0};
    p.readers = 1;
    p.writers = 1;

    /* Write 400 bytes, read 400, write 400 more (wraps around) */
    uint8_t buf[400];
    memset(buf, 'X', 400);
    int nw = pipe_write(&p, buf, 400);
    ASSERT_EQ(nw, 400);

    char rbuf[400];
    int nr = pipe_read(&p, rbuf, 400);
    ASSERT_EQ(nr, 400);
    ASSERT_EQ(p.count, 0);

    /* Write again — should wrap around in the circular buffer */
    memset(buf, 'Y', 400);
    nw = pipe_write(&p, buf, 400);
    ASSERT_EQ(nw, 400);

    memset(rbuf, 0, sizeof(rbuf));
    nr = pipe_read(&p, rbuf, 400);
    ASSERT_EQ(nr, 400);
    ASSERT_EQ(rbuf[0], 'Y');
    ASSERT_EQ(rbuf[399], 'Y');
}

/* ======== Process table tests ======== */

static void test_alloc_pid(void)
{
    proc_init_test();
    next_pid = 1;

    uint8_t p1 = alloc_pid();
    ASSERT(p1 != 0xFF);
    ASSERT(p1 != 0);  /* PID 0 is the kernel shell */
    proctab[p1].state = P_RUNNING;

    uint8_t p2 = alloc_pid();
    ASSERT(p2 != 0xFF);
    ASSERT(p2 != p1);
    ASSERT(p2 != 0);
    proctab[p2].state = P_RUNNING;
}

static void test_alloc_pid_exhausted(void)
{
    proc_init_test();
    next_pid = 1;

    /* Fill all slots */
    for (int i = 1; i < MAXPROC; i++) {
        proctab[i].state = P_RUNNING;
    }

    uint8_t p = alloc_pid();
    ASSERT_EQ(p, 0xFF);  /* no free slot */
}

static void test_zombie_reap(void)
{
    proc_init_test();

    /* Create a zombie child */
    proctab[1].state = P_ZOMBIE;
    proctab[1].pid = 1;
    proctab[1].ppid = 0;
    proctab[1].exitcode = 42;
    nproc = 2;

    /* Find and reap zombie */
    int found = 0;
    for (int i = 0; i < MAXPROC; i++) {
        if (proctab[i].state == P_ZOMBIE &&
            proctab[i].ppid == curproc->pid) {
            found = 1;
            ASSERT_EQ(proctab[i].exitcode, 42);
            proctab[i].state = P_FREE;
            nproc--;
            break;
        }
    }
    ASSERT(found);
    ASSERT_EQ(nproc, 1);
}

static void test_reparent_children(void)
{
    proc_init_test();

    /* Process 1 has children 2 and 3 */
    proctab[1].state = P_RUNNING;
    proctab[1].pid = 1;
    proctab[1].ppid = 0;
    proctab[2].state = P_RUNNING;
    proctab[2].pid = 2;
    proctab[2].ppid = 1;
    proctab[3].state = P_SLEEPING;
    proctab[3].pid = 3;
    proctab[3].ppid = 1;
    nproc = 4;

    /* Process 1 exits — reparent children to process 0 */
    for (int i = 0; i < MAXPROC; i++) {
        if (proctab[i].state != P_FREE &&
            proctab[i].ppid == 1) {
            proctab[i].ppid = 0;
        }
    }
    ASSERT_EQ(proctab[2].ppid, 0);
    ASSERT_EQ(proctab[3].ppid, 0);
}

static void test_process_states(void)
{
    /* Verify state constants are distinct */
    ASSERT(P_FREE != P_RUNNING);
    ASSERT(P_RUNNING != P_READY);
    ASSERT(P_READY != P_SLEEPING);
    ASSERT(P_SLEEPING != P_ZOMBIE);
    ASSERT(P_ZOMBIE != P_VFORK);
}

/* ======== Main ======== */

int main(void)
{
    printf("test_proc:\n");

    /* Pipe tests */
    RUN_TEST(test_pipe_basic);
    RUN_TEST(test_pipe_empty_read);
    RUN_TEST(test_pipe_eof);
    RUN_TEST(test_pipe_broken);
    RUN_TEST(test_pipe_full);
    RUN_TEST(test_pipe_wrap);

    /* Process table tests */
    RUN_TEST(test_alloc_pid);
    RUN_TEST(test_alloc_pid_exhausted);
    RUN_TEST(test_zombie_reap);
    RUN_TEST(test_reparent_children);
    RUN_TEST(test_process_states);

    TEST_REPORT();
}
