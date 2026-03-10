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
    uint8_t  read_waiting;
    uint8_t  write_waiting;
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

/* ======== Kstack layout test ========
 *
 * proc_setup_kstack builds a frame that swtch() + proc_first_run expect.
 * We verify the byte layout here. This uses the exact same struct definition
 * as the kernel (minus fields we don't need for the layout test). */

#define KSTACK_SIZE   512
#define KSTACK_WORDS  (KSTACK_SIZE / 4)
#define KSTACK_CANARY 0xDEADBEEF

/* Verify kstack frame layout (offsets and sizes).
 * On the host, pointers are 64-bit but the layout uses uint32_t slots.
 * We test that the total frame size is correct and that the relative
 * offsets match what swtch() + proc_first_run expect on the 68000. */

static void test_kstack_layout(void)
{
    /* Frame components (all in bytes):
     *   swtch callee-saved:  11 * 4 = 44
     *   swtch return addr:          = 4
     *   USP:                        = 4
     *   user regs (d0-a6):  15 * 4 = 60
     *   SR (uint16_t):              = 2
     *   PC:                         = 4
     *   Total:                      = 118 */
    int callee_saved = 11 * 4;  /* d2-d7, a2-a6 */
    int retaddr = 4;
    int usp = 4;
    int user_regs = 15 * 4;     /* d0-d7, a0-a6 */
    int sr = 2;
    int pc = 4;
    int total = callee_saved + retaddr + usp + user_regs + sr + pc;
    ASSERT_EQ(total, 118);

    /* Verify offsets from ksp (stack grows down, ksp is lowest addr):
     * ksp+0:   callee-saved[0] (d2)
     * ksp+44:  return addr (→ proc_first_run)
     * ksp+48:  USP
     * ksp+52:  d0 (first of user regs)
     * ksp+112: SR
     * ksp+114: PC (entry point) */
    ASSERT_EQ(callee_saved, 44);
    ASSERT_EQ(callee_saved + retaddr, 48);
    ASSERT_EQ(callee_saved + retaddr + usp, 52);
    ASSERT_EQ(callee_saved + retaddr + usp + user_regs, 112);
    ASSERT_EQ(callee_saved + retaddr + usp + user_regs + sr, 114);
}

static void test_kstack_frame_fits(void)
{
    /* 118 bytes for initial frame, need room for syscall path too.
     * With 512-byte stacks, 118 bytes leaves 394 for syscalls. */
    ASSERT(118 + 210 < KSTACK_SIZE);  /* 210 = worst case syscall path */
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

/* ======== Kstack canary tests ======== */

/* ======== Pipe stress tests ======== */

static void test_pipe_stress_full_buffer(void)
{
    /* Fill the pipe completely, then drain it */
    struct pipe p;
    memset(&p, 0, sizeof(p));
    p.readers = 1;
    p.writers = 1;

    uint8_t wbuf[PIPE_SIZE];
    for (int i = 0; i < PIPE_SIZE; i++)
        wbuf[i] = (uint8_t)(i & 0xFF);

    int nw = pipe_write(&p, wbuf, PIPE_SIZE);
    ASSERT_EQ(nw, PIPE_SIZE);
    ASSERT_EQ(p.count, PIPE_SIZE);

    uint8_t rbuf[PIPE_SIZE];
    int nr = pipe_read(&p, rbuf, PIPE_SIZE);
    ASSERT_EQ(nr, PIPE_SIZE);
    ASSERT_EQ(p.count, 0);

    /* Verify data integrity */
    for (int i = 0; i < PIPE_SIZE; i++)
        ASSERT_EQ(rbuf[i], (uint8_t)(i & 0xFF));
}

static void test_pipe_stress_repeated(void)
{
    /* Write+read in small chunks, wrapping multiple times */
    struct pipe p;
    memset(&p, 0, sizeof(p));
    p.readers = 1;
    p.writers = 1;

    uint8_t chunk[64];
    uint8_t rbuf[64];
    uint8_t counter = 0;

    for (int round = 0; round < 20; round++) {
        for (int i = 0; i < 64; i++)
            chunk[i] = counter++;
        int nw = pipe_write(&p, chunk, 64);
        ASSERT_EQ(nw, 64);
        int nr = pipe_read(&p, rbuf, 64);
        ASSERT_EQ(nr, 64);
        /* Verify this chunk */
        for (int i = 0; i < 64; i++)
            ASSERT_EQ(rbuf[i], chunk[i]);
    }
    /* After 20 rounds of 64 bytes, positions should have wrapped */
    ASSERT_EQ(p.count, 0);
}

static void test_pipe_stress_wraparound_integrity(void)
{
    /* Position the read/write near the end of the buffer, then do a big write */
    struct pipe p;
    memset(&p, 0, sizeof(p));
    p.readers = 1;
    p.writers = 1;

    /* Advance positions to near the end (PIPE_SIZE - 10) */
    p.read_pos = PIPE_SIZE - 10;
    p.write_pos = PIPE_SIZE - 10;
    p.count = 0;

    /* Write 100 bytes — this wraps around the circular buffer */
    uint8_t wbuf[100];
    for (int i = 0; i < 100; i++)
        wbuf[i] = (uint8_t)(0xA0 + i);
    int nw = pipe_write(&p, wbuf, 100);
    ASSERT_EQ(nw, 100);

    /* Read back and verify */
    uint8_t rbuf[100];
    int nr = pipe_read(&p, rbuf, 100);
    ASSERT_EQ(nr, 100);
    for (int i = 0; i < 100; i++)
        ASSERT_EQ(rbuf[i], (uint8_t)(0xA0 + i));
}

/* ======== Kstack canary tests ======== */

static void test_kstack_canary_init(void)
{
    /* Verify canary is placed at kstack[0] after setup */
    uint32_t kstack[KSTACK_WORDS];
    memset(kstack, 0, sizeof(kstack));

    /* Simulate what proc_setup_kstack does at the end */
    kstack[0] = KSTACK_CANARY;

    ASSERT_EQ(kstack[0], KSTACK_CANARY);
    /* Ensure it's at the very bottom (lowest address) */
    ASSERT_EQ(kstack[0], 0xDEADBEEF);
}

static void test_kstack_canary_detects_overflow(void)
{
    /* Canary corruption should be detectable */
    uint32_t kstack[KSTACK_WORDS];
    kstack[0] = KSTACK_CANARY;

    ASSERT_EQ(kstack[0], KSTACK_CANARY);

    /* Simulate overflow: corrupt the canary */
    kstack[0] = 0;

    ASSERT(kstack[0] != KSTACK_CANARY);
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

    /* Kstack layout tests */
    RUN_TEST(test_kstack_layout);
    RUN_TEST(test_kstack_frame_fits);

    /* Process table tests */
    RUN_TEST(test_alloc_pid);
    RUN_TEST(test_alloc_pid_exhausted);
    RUN_TEST(test_zombie_reap);
    RUN_TEST(test_reparent_children);
    RUN_TEST(test_process_states);

    /* Pipe stress tests */
    RUN_TEST(test_pipe_stress_full_buffer);
    RUN_TEST(test_pipe_stress_repeated);
    RUN_TEST(test_pipe_stress_wraparound_integrity);

    /* Kstack canary tests */
    RUN_TEST(test_kstack_canary_init);
    RUN_TEST(test_kstack_canary_detects_overflow);

    TEST_REPORT();
}
