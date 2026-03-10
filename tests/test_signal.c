/*
 * Unit tests for signal logic
 *
 * Tests the signal delivery logic on the host (no 68000 needed).
 * Re-implements the kernel's signal structures and sig_deliver logic.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* Signal constants (must match kernel/kernel.h) */
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGKILL  9
#define SIGPIPE  13
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define NSIG     21

#define SIG_DFL  0
#define SIG_IGN  1

/* Error numbers */
#define EINVAL   22
#define ESRCH     3

/* Process states */
#define P_FREE      0
#define P_RUNNING   1
#define P_READY     2
#define P_SLEEPING  3
#define P_ZOMBIE    4

#define MAXPROC  16

/* Simplified proc struct for testing */
struct proc {
    uint8_t  state;
    uint8_t  pid;
    uint8_t  ppid;
    int8_t   exitcode;
    uint32_t sig_pending;
    uint32_t sig_handler[NSIG];
};

static struct proc proctab[MAXPROC];
static struct proc *curproc;
static int exit_called;
static int exit_code;

/* Mock do_exit for testing */
static void do_exit(int code)
{
    exit_called = 1;
    exit_code = code;
    if (curproc)
        curproc->state = P_ZOMBIE;
}

/* Re-implement sig_deliver for host testing */
static void sig_deliver(void)
{
    if (!curproc || !curproc->sig_pending)
        return;

    for (int sig = 1; sig < NSIG; sig++) {
        if (!(curproc->sig_pending & (1u << sig)))
            continue;
        curproc->sig_pending &= ~(1u << sig);

        uint32_t handler = curproc->sig_handler[sig];

        if (sig == SIGKILL) {
            do_exit(128 + sig);
            return;
        }
        if (sig == SIGSTOP)
            continue;

        if (handler == SIG_IGN)
            continue;

        if (handler == SIG_DFL) {
            switch (sig) {
            case SIGCHLD:
            case SIGCONT:
                break;
            default:
                do_exit(128 + sig);
                return;
            }
        } else {
            /* User handler: not yet implemented */
            switch (sig) {
            case SIGCHLD:
            case SIGCONT:
                break;
            default:
                do_exit(128 + sig);
                return;
            }
        }
    }
}

/* Re-implement sys_signal for host testing */
static int sys_signal(int signum, uint32_t handler)
{
    if (signum < 1 || signum >= NSIG)
        return -EINVAL;
    if (signum == SIGKILL || signum == SIGSTOP)
        return -EINVAL;
    uint32_t old = curproc->sig_handler[signum];
    curproc->sig_handler[signum] = handler;
    return (int32_t)old;
}

/* Re-implement sys_kill for host testing */
static int sys_kill(int pid, int sig)
{
    if (sig < 0 || sig >= NSIG)
        return -EINVAL;
    if (sig == 0)
        return 0;
    struct proc *target = NULL;
    for (int i = 0; i < MAXPROC; i++) {
        if (proctab[i].state != P_FREE && proctab[i].pid == (uint8_t)pid) {
            target = &proctab[i];
            break;
        }
    }
    if (!target)
        return -ESRCH;
    target->sig_pending |= (1u << sig);
    if (target->state == P_SLEEPING)
        target->state = P_READY;
    return 0;
}

static void init_test(void)
{
    memset(proctab, 0, sizeof(proctab));
    for (int i = 0; i < MAXPROC; i++) {
        proctab[i].state = P_FREE;
        proctab[i].pid = i;
    }
    curproc = &proctab[0];
    curproc->state = P_RUNNING;
    exit_called = 0;
    exit_code = -1;
}

/* ======== Tests ======== */

static void test_signal_set_handler(void)
{
    init_test();
    int old = sys_signal(SIGINT, SIG_IGN);
    ASSERT_EQ(old, SIG_DFL);  /* was default */
    ASSERT_EQ(curproc->sig_handler[SIGINT], SIG_IGN);
}

static void test_signal_returns_old_handler(void)
{
    init_test();
    sys_signal(SIGTERM, SIG_IGN);
    int old = sys_signal(SIGTERM, SIG_DFL);
    ASSERT_EQ(old, SIG_IGN);
}

static void test_signal_cannot_catch_sigkill(void)
{
    init_test();
    int rc = sys_signal(SIGKILL, SIG_IGN);
    ASSERT_EQ(rc, -EINVAL);
}

static void test_signal_cannot_catch_sigstop(void)
{
    init_test();
    int rc = sys_signal(SIGSTOP, SIG_IGN);
    ASSERT_EQ(rc, -EINVAL);
}

static void test_signal_invalid_number(void)
{
    init_test();
    int rc = sys_signal(0, SIG_IGN);
    ASSERT_EQ(rc, -EINVAL);
    rc = sys_signal(NSIG, SIG_IGN);
    ASSERT_EQ(rc, -EINVAL);
    rc = sys_signal(-1, SIG_IGN);
    ASSERT_EQ(rc, -EINVAL);
}

static void test_kill_sets_pending(void)
{
    init_test();
    proctab[1].state = P_RUNNING;
    proctab[1].pid = 1;

    int rc = sys_kill(1, SIGTERM);
    ASSERT_EQ(rc, 0);
    ASSERT(proctab[1].sig_pending & (1u << SIGTERM));
}

static void test_kill_wakes_sleeping(void)
{
    init_test();
    proctab[1].state = P_SLEEPING;
    proctab[1].pid = 1;

    sys_kill(1, SIGTERM);
    ASSERT_EQ(proctab[1].state, P_READY);
}

static void test_kill_nonexistent(void)
{
    init_test();
    int rc = sys_kill(99, SIGTERM);
    ASSERT_EQ(rc, -ESRCH);
}

static void test_kill_sig_zero(void)
{
    init_test();
    /* sig 0 should just return 0 (existence check) */
    int rc = sys_kill(0, 0);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(curproc->sig_pending, 0u);  /* no signal actually sent */
}

static void test_deliver_default_terminates(void)
{
    init_test();
    curproc->sig_pending = (1u << SIGTERM);
    sig_deliver();
    ASSERT(exit_called);
    ASSERT_EQ(exit_code, 128 + SIGTERM);
}

static void test_deliver_sigint_terminates(void)
{
    init_test();
    curproc->sig_pending = (1u << SIGINT);
    sig_deliver();
    ASSERT(exit_called);
    ASSERT_EQ(exit_code, 128 + SIGINT);
}

static void test_deliver_sigkill_always_terminates(void)
{
    init_test();
    /* Even with SIG_IGN set (which shouldn't be possible, but test the logic) */
    curproc->sig_handler[SIGKILL] = SIG_IGN;
    curproc->sig_pending = (1u << SIGKILL);
    sig_deliver();
    ASSERT(exit_called);
    ASSERT_EQ(exit_code, 128 + SIGKILL);
}

static void test_deliver_ignored_signal(void)
{
    init_test();
    curproc->sig_handler[SIGTERM] = SIG_IGN;
    curproc->sig_pending = (1u << SIGTERM);
    sig_deliver();
    ASSERT(!exit_called);
    ASSERT_EQ(curproc->sig_pending, 0u);  /* cleared */
}

static void test_deliver_sigchld_default_ignore(void)
{
    init_test();
    curproc->sig_pending = (1u << SIGCHLD);
    sig_deliver();
    ASSERT(!exit_called);
    ASSERT_EQ(curproc->sig_pending, 0u);
}

static void test_deliver_sigcont_default_ignore(void)
{
    init_test();
    curproc->sig_pending = (1u << SIGCONT);
    sig_deliver();
    ASSERT(!exit_called);
    ASSERT_EQ(curproc->sig_pending, 0u);
}

static void test_deliver_clears_pending(void)
{
    init_test();
    curproc->sig_handler[SIGINT] = SIG_IGN;
    curproc->sig_handler[SIGTERM] = SIG_IGN;
    curproc->sig_pending = (1u << SIGINT) | (1u << SIGTERM);
    sig_deliver();
    ASSERT(!exit_called);
    ASSERT_EQ(curproc->sig_pending, 0u);
}

static void test_no_pending_noop(void)
{
    init_test();
    curproc->sig_pending = 0;
    sig_deliver();
    ASSERT(!exit_called);
}

static void test_deliver_multiple_first_kills(void)
{
    init_test();
    /* SIGINT and SIGTERM both pending, default action for both is terminate.
     * sig_deliver processes lowest-numbered first (SIGINT=2 before SIGTERM=15). */
    curproc->sig_pending = (1u << SIGINT) | (1u << SIGTERM);
    sig_deliver();
    ASSERT(exit_called);
    ASSERT_EQ(exit_code, 128 + SIGINT);
}

static void test_signal_constants(void)
{
    ASSERT_EQ(SIGHUP, 1);
    ASSERT_EQ(SIGINT, 2);
    ASSERT_EQ(SIGQUIT, 3);
    ASSERT_EQ(SIGKILL, 9);
    ASSERT_EQ(SIGPIPE, 13);
    ASSERT_EQ(SIGTERM, 15);
    ASSERT_EQ(SIGCHLD, 17);
    ASSERT_EQ(SIGCONT, 18);
    ASSERT_EQ(SIGSTOP, 19);
    ASSERT_EQ(SIGTSTP, 20);
    ASSERT(NSIG > SIGTSTP);
}

static void test_sig_pending_bitmask(void)
{
    /* Verify bitmask operations work correctly for all signals */
    uint32_t mask = 0;
    for (int sig = 1; sig < NSIG; sig++) {
        mask |= (1u << sig);
        ASSERT(mask & (1u << sig));
    }
    /* Signal 0 should never be in the mask */
    ASSERT(!(mask & 1));
}

/* ======== Main ======== */

int main(void)
{
    printf("test_signal:\n");

    RUN_TEST(test_signal_set_handler);
    RUN_TEST(test_signal_returns_old_handler);
    RUN_TEST(test_signal_cannot_catch_sigkill);
    RUN_TEST(test_signal_cannot_catch_sigstop);
    RUN_TEST(test_signal_invalid_number);
    RUN_TEST(test_kill_sets_pending);
    RUN_TEST(test_kill_wakes_sleeping);
    RUN_TEST(test_kill_nonexistent);
    RUN_TEST(test_kill_sig_zero);
    RUN_TEST(test_deliver_default_terminates);
    RUN_TEST(test_deliver_sigint_terminates);
    RUN_TEST(test_deliver_sigkill_always_terminates);
    RUN_TEST(test_deliver_ignored_signal);
    RUN_TEST(test_deliver_sigchld_default_ignore);
    RUN_TEST(test_deliver_sigcont_default_ignore);
    RUN_TEST(test_deliver_clears_pending);
    RUN_TEST(test_no_pending_noop);
    RUN_TEST(test_deliver_multiple_first_kills);
    RUN_TEST(test_signal_constants);
    RUN_TEST(test_sig_pending_bitmask);

    TEST_REPORT();
}
