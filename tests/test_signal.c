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
#define P_STOPPED   6

#define MAXPROC  16

/* Simplified proc struct for testing */
struct proc {
    uint8_t  state;
    uint8_t  pid;
    uint8_t  ppid;
    int8_t   exitcode;
    uint8_t  pgrp;
    uint32_t sig_pending;
    uint32_t sig_handler[NSIG];
};

static struct proc proctab[MAXPROC];
static struct proc *curproc;
static int exit_called;
static int exit_code;
static int schedule_called;

/* Mock do_exit for testing */
static void do_exit(int code)
{
    exit_called = 1;
    exit_code = code;
    if (curproc)
        curproc->state = P_ZOMBIE;
}

/* Mock schedule for testing SIGTSTP/SIGSTOP */
static void schedule(void)
{
    schedule_called = 1;
}

/*
 * Signal frame constants (must match kernel/proc.c).
 *
 * On the real 68000: 84 bytes on user stack, kstack frame is 70 bytes.
 * On the host test: we can't dereference 32-bit "pointers" on a 64-bit
 * host, so we test the LOGIC (decision paths, one-shot reset, pending
 * bits, stop/continue) without actually writing to fake user memory.
 * The actual signal frame memory layout is validated on the 68000 target
 * via autotest.
 */
#define SIG_FRAME_SIZE   84

/* State captured by sig_deliver when a user handler is dispatched */
static int handler_dispatched;
static int handler_sig;
static uint32_t handler_addr;
static uint32_t handler_saved_usp;
static uint32_t handler_saved_d0;
static uint32_t handler_saved_pc;

/*
 * Re-implement sig_deliver for host testing.
 *
 * Tests the logic: SIG_DFL, SIG_IGN, SIGKILL, SIGSTOP, SIGTSTP,
 * user handler dispatch (one-shot, pending bit management, frame
 * redirection). Doesn't write to a fake user stack (68000-specific).
 *
 * frame layout (conceptual, 68000 kstack):
 *   frame[0]  = USP, frame[1] = d0, frame[2..15] = d1-d7/a0-a6
 *   byte 64-65 = SR, byte 66-69 = PC
 */
static void sig_deliver(uint32_t *frame)
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
        if (sig == SIGSTOP) {
            curproc->state = P_STOPPED;
            schedule();
            continue;
        }

        if (handler == SIG_IGN)
            continue;

        if (handler == SIG_DFL) {
            switch (sig) {
            case SIGCHLD:
            case SIGCONT:
                break;
            case SIGTSTP:
                curproc->state = P_STOPPED;
                schedule();
                break;
            default:
                do_exit(128 + sig);
                return;
            }
        } else {
            /* User handler — record what would be done, modify frame */
            handler_dispatched = 1;
            handler_sig = sig;
            handler_addr = handler;
            handler_saved_usp = frame[0];
            handler_saved_d0 = frame[1];
            handler_saved_pc = *(uint32_t *)((uint8_t *)frame + 66);

            /* Modify frame to redirect to handler (same as real kernel) */
            frame[0] = (frame[0] - SIG_FRAME_SIZE) & ~1u;  /* new USP */
            *(uint32_t *)((uint8_t *)frame + 66) = handler; /* PC → handler */

            /* One-shot: reset to SIG_DFL */
            curproc->sig_handler[sig] = SIG_DFL;

            /* Only one user handler per sig_deliver call */
            return;
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

/* Re-implement sys_kill for host testing (with SIGCONT support) */
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
    if (sig == SIGCONT && target->state == P_STOPPED) {
        target->state = P_READY;
        target->sig_pending &= ~((1u << SIGSTOP) | (1u << SIGTSTP));
    }
    if (target->state == P_SLEEPING)
        target->state = P_READY;
    return 0;
}

/*
 * Fake kstack frame for testing signal delivery.
 * Layout matches 68000 kstack: 16 uint32_t words + SR(2) + PC(4) = 70 bytes.
 * We allocate 72 bytes (18 uint32_t) for safe access.
 */
static uint32_t test_frame[18];

static void init_test(void)
{
    memset(proctab, 0, sizeof(proctab));
    for (int i = 0; i < MAXPROC; i++) {
        proctab[i].state = P_FREE;
        proctab[i].pid = i;
    }
    curproc = &proctab[0];
    curproc->state = P_RUNNING;
    curproc->pgrp = 0;
    exit_called = 0;
    exit_code = -1;
    schedule_called = 0;
    handler_dispatched = 0;
    handler_sig = 0;
    handler_addr = 0;
    handler_saved_usp = 0;
    handler_saved_d0 = 0;
    handler_saved_pc = 0;

    /* Initialize fake frame with recognizable values */
    memset(test_frame, 0, sizeof(test_frame));
    test_frame[0] = 0x0F0000;  /* fake USP (68000-style address) */
    test_frame[1] = 0x42;      /* d0 (mock return value) */
    /* Set PC at byte offset 66 */
    *(uint32_t *)((uint8_t *)test_frame + 66) = 0xDEAD;  /* mock PC */
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
    sig_deliver(test_frame);
    ASSERT(exit_called);
    ASSERT_EQ(exit_code, 128 + SIGTERM);
}

static void test_deliver_sigint_terminates(void)
{
    init_test();
    curproc->sig_pending = (1u << SIGINT);
    sig_deliver(test_frame);
    ASSERT(exit_called);
    ASSERT_EQ(exit_code, 128 + SIGINT);
}

static void test_deliver_sigkill_always_terminates(void)
{
    init_test();
    /* Even with SIG_IGN set (which shouldn't be possible, but test the logic) */
    curproc->sig_handler[SIGKILL] = SIG_IGN;
    curproc->sig_pending = (1u << SIGKILL);
    sig_deliver(test_frame);
    ASSERT(exit_called);
    ASSERT_EQ(exit_code, 128 + SIGKILL);
}

static void test_deliver_ignored_signal(void)
{
    init_test();
    curproc->sig_handler[SIGTERM] = SIG_IGN;
    curproc->sig_pending = (1u << SIGTERM);
    sig_deliver(test_frame);
    ASSERT(!exit_called);
    ASSERT_EQ(curproc->sig_pending, 0u);  /* cleared */
}

static void test_deliver_sigchld_default_ignore(void)
{
    init_test();
    curproc->sig_pending = (1u << SIGCHLD);
    sig_deliver(test_frame);
    ASSERT(!exit_called);
    ASSERT_EQ(curproc->sig_pending, 0u);
}

static void test_deliver_sigcont_default_ignore(void)
{
    init_test();
    curproc->sig_pending = (1u << SIGCONT);
    sig_deliver(test_frame);
    ASSERT(!exit_called);
    ASSERT_EQ(curproc->sig_pending, 0u);
}

static void test_deliver_clears_pending(void)
{
    init_test();
    curproc->sig_handler[SIGINT] = SIG_IGN;
    curproc->sig_handler[SIGTERM] = SIG_IGN;
    curproc->sig_pending = (1u << SIGINT) | (1u << SIGTERM);
    sig_deliver(test_frame);
    ASSERT(!exit_called);
    ASSERT_EQ(curproc->sig_pending, 0u);
}

static void test_no_pending_noop(void)
{
    init_test();
    curproc->sig_pending = 0;
    sig_deliver(test_frame);
    ASSERT(!exit_called);
}

static void test_deliver_multiple_first_kills(void)
{
    init_test();
    /* SIGINT and SIGTERM both pending, default action for both is terminate.
     * sig_deliver processes lowest-numbered first (SIGINT=2 before SIGTERM=15). */
    curproc->sig_pending = (1u << SIGINT) | (1u << SIGTERM);
    sig_deliver(test_frame);
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

/* ======== Phase 2d: User signal handler tests ======== */

static void test_user_handler_redirects_pc(void)
{
    init_test();
    sys_signal(SIGINT, 0x50000);
    curproc->sig_pending = (1u << SIGINT);
    sig_deliver(test_frame);

    /* Should NOT have called do_exit */
    ASSERT(!exit_called);
    ASSERT(handler_dispatched);

    /* PC should now point to handler */
    uint32_t new_pc = *(uint32_t *)((uint8_t *)test_frame + 66);
    ASSERT_EQ(new_pc, 0x50000u);
}

static void test_user_handler_modifies_usp(void)
{
    init_test();
    uint32_t orig_usp = test_frame[0];
    sys_signal(SIGINT, 0x50000);
    curproc->sig_pending = (1u << SIGINT);
    sig_deliver(test_frame);

    /* USP should have decreased by SIG_FRAME_SIZE (84 bytes, even-aligned) */
    uint32_t new_usp = test_frame[0];
    ASSERT(new_usp < orig_usp);
    ASSERT_EQ(orig_usp - new_usp, (uint32_t)SIG_FRAME_SIZE);
    ASSERT_EQ(new_usp & 1, 0u);  /* even-aligned */
}

static void test_user_handler_saves_original_state(void)
{
    init_test();
    uint32_t orig_usp = test_frame[0];
    uint32_t orig_d0 = test_frame[1];
    uint32_t orig_pc = *(uint32_t *)((uint8_t *)test_frame + 66);

    sys_signal(SIGINT, 0x50000);
    curproc->sig_pending = (1u << SIGINT);
    sig_deliver(test_frame);

    /* sig_deliver should have recorded the original state */
    ASSERT_EQ(handler_saved_usp, orig_usp);
    ASSERT_EQ(handler_saved_d0, orig_d0);
    ASSERT_EQ(handler_saved_pc, orig_pc);
}

static void test_user_handler_correct_signal(void)
{
    init_test();
    sys_signal(SIGTERM, 0x50000);
    curproc->sig_pending = (1u << SIGTERM);
    sig_deliver(test_frame);

    ASSERT(handler_dispatched);
    ASSERT_EQ(handler_sig, SIGTERM);
    ASSERT_EQ(handler_addr, 0x50000u);
}

static void test_user_handler_one_shot(void)
{
    init_test();
    sys_signal(SIGINT, 0x50000);
    curproc->sig_pending = (1u << SIGINT);
    sig_deliver(test_frame);

    /* Handler should be reset to SIG_DFL (one-shot semantics) */
    ASSERT_EQ(curproc->sig_handler[SIGINT], (uint32_t)SIG_DFL);
}

static void test_user_handler_doesnt_exit(void)
{
    init_test();
    sys_signal(SIGINT, 0x50000);
    sys_signal(SIGTERM, 0x60000);
    curproc->sig_pending = (1u << SIGINT) | (1u << SIGTERM);
    sig_deliver(test_frame);

    /* User handler should NOT call do_exit */
    ASSERT(!exit_called);
    ASSERT(handler_dispatched);
}

static void test_user_handler_usp_even_aligned(void)
{
    init_test();
    /* Set USP to an odd value to test alignment */
    test_frame[0] = 0x0F0001;
    sys_signal(SIGINT, 0x50000);
    curproc->sig_pending = (1u << SIGINT);
    sig_deliver(test_frame);

    /* New USP must be even-aligned (68000 requirement) */
    ASSERT_EQ(test_frame[0] & 1, 0u);
}

/* ======== Phase 2d: SIGTSTP / SIGCONT / P_STOPPED ======== */

static void test_sigtstp_default_stops(void)
{
    init_test();
    curproc->sig_pending = (1u << SIGTSTP);
    sig_deliver(test_frame);

    ASSERT(!exit_called);
    ASSERT_EQ(curproc->state, (uint8_t)P_STOPPED);
    ASSERT(schedule_called);
}

static void test_sigstop_always_stops(void)
{
    init_test();
    /* SIGSTOP can't be caught */
    curproc->sig_handler[SIGSTOP] = SIG_IGN;  /* should be ignored by signal() */
    curproc->sig_pending = (1u << SIGSTOP);
    sig_deliver(test_frame);

    ASSERT(!exit_called);
    ASSERT_EQ(curproc->state, (uint8_t)P_STOPPED);
    ASSERT(schedule_called);
}

static void test_sigcont_wakes_stopped(void)
{
    init_test();
    proctab[1].state = P_STOPPED;
    proctab[1].pid = 1;

    int rc = sys_kill(1, SIGCONT);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(proctab[1].state, (uint8_t)P_READY);
}

static void test_sigcont_clears_stop_signals(void)
{
    init_test();
    proctab[1].state = P_STOPPED;
    proctab[1].pid = 1;
    proctab[1].sig_pending = (1u << SIGTSTP) | (1u << SIGSTOP);

    sys_kill(1, SIGCONT);

    /* SIGTSTP and SIGSTOP should be cleared */
    ASSERT(!(proctab[1].sig_pending & (1u << SIGTSTP)));
    ASSERT(!(proctab[1].sig_pending & (1u << SIGSTOP)));
    /* SIGCONT should be pending */
    ASSERT(proctab[1].sig_pending & (1u << SIGCONT));
}

/* ======== Phase 2d: Process groups ======== */

static void test_pgrp_init(void)
{
    init_test();
    /* Process 0 should have pgrp 0 */
    ASSERT_EQ(curproc->pgrp, 0);
}

static void test_deliver_only_one_user_handler(void)
{
    /* When multiple user-handler signals are pending, sig_deliver should
     * deliver only one at a time and return. */
    init_test();
    sys_signal(SIGINT, 0x50000);
    sys_signal(SIGTERM, 0x60000);
    curproc->sig_pending = (1u << SIGINT) | (1u << SIGTERM);
    sig_deliver(test_frame);

    ASSERT(!exit_called);
    /* SIGINT (lower number) should be delivered first */
    uint32_t pc = *(uint32_t *)((uint8_t *)test_frame + 66);
    ASSERT_EQ(pc, 0x50000u);
    /* SIGTERM should still be pending */
    ASSERT(curproc->sig_pending & (1u << SIGTERM));
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
    /* Phase 2d: User signal handlers */
    RUN_TEST(test_user_handler_redirects_pc);
    RUN_TEST(test_user_handler_modifies_usp);
    RUN_TEST(test_user_handler_saves_original_state);
    RUN_TEST(test_user_handler_correct_signal);
    RUN_TEST(test_user_handler_one_shot);
    RUN_TEST(test_user_handler_doesnt_exit);
    RUN_TEST(test_user_handler_usp_even_aligned);
    /* Phase 2d: SIGTSTP/SIGCONT/P_STOPPED */
    RUN_TEST(test_sigtstp_default_stops);
    RUN_TEST(test_sigstop_always_stops);
    RUN_TEST(test_sigcont_wakes_stopped);
    RUN_TEST(test_sigcont_clears_stop_signals);
    /* Phase 2d: Process groups */
    RUN_TEST(test_pgrp_init);
    RUN_TEST(test_deliver_only_one_user_handler);

    TEST_REPORT();
}
