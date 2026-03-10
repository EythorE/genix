/*
 * Unit tests for TTY line discipline
 *
 * Tests the TTY subsystem logic on the host (no 68000 needed).
 * Re-implements minimal kernel structures for testing.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* ---- Kernel stubs for host testing ---- */

/* Signal constants (must match kernel/kernel.h) */
#define SIGINT   2
#define SIGQUIT  3
#define SIGTSTP  20
#define NSIG     21

#define MAXPROC  16
#define MAXFD    16

#define P_FREE      0
#define P_RUNNING   1
#define P_READY     2
#define P_SLEEPING  3

/* Error numbers */
#define ENODEV   19
#define EINTR     4
#define EINVAL   22
#define ENOTTY   25

/* Stub process structure */
struct proc {
    uint8_t  state;
    uint8_t  pid;
    uint32_t sig_pending;
};

struct proc proctab[MAXPROC];
struct proc *curproc;
int nproc = 1;

/* Stub kernel I/O — captures output for testing */
static char output_buf[4096];
static int output_pos = 0;

/* Stub input queue — simulates pal_console_getc */
static uint8_t input_queue[256];
static int input_head = 0;
static int input_tail = 0;

static void reset_output(void)
{
    output_pos = 0;
    output_buf[0] = '\0';
}

static void reset_input(void)
{
    input_head = input_tail = 0;
}

static void push_input(uint8_t c)
{
    input_queue[input_head++] = c;
    /* input_head & 0xFF for wrapping but 256-size buffer so fine */
}

static void push_string(const char *s)
{
    while (*s)
        push_input((uint8_t)*s++);
}

/* Stub PAL functions */
void kputc(char c)
{
    if (output_pos < 4095) {
        output_buf[output_pos++] = c;
        output_buf[output_pos] = '\0';
    }
}

void kputs(const char *s)
{
    while (*s)
        kputc(*s++);
}

void pal_console_putc(char c)
{
    /* tty_write goes here — just capture output */
    if (output_pos < 4095) {
        output_buf[output_pos++] = c;
        output_buf[output_pos] = '\0';
    }
}

int pal_console_ready(void)
{
    return input_tail < input_head;
}

int pal_console_getc(void)
{
    if (input_tail < input_head)
        return input_queue[input_tail++];
    return -1;
}

void schedule(void) { /* no-op for testing */ }

/* Stubs for memset/memcpy/strcmp — use libc versions */
/* kernel.h normally provides these, but we're testing standalone */

/* ---- Include TTY implementation ---- */

/* We need to define the TTY structures inline for host testing,
 * since tty.h includes kernel types */

/* TTY constants (must match kernel/tty.h) */
#define ICRNL   0x0100
#define INLCR   0x0040
#define IGNCR   0x0080
#define OPOST   0x0001
#define ONLCR   0x0004
#define ISIG    0x0001
#define ICANON  0x0002
#define ECHO    0x0008
#define ECHOE   0x0010
#define ECHOK   0x0020
#define ECHONL  0x0040
#define NOFLSH  0x0080

#define VINTR   0
#define VQUIT   1
#define VERASE  2
#define VKILL   3
#define VEOF    4
#define VSTART  5
#define VSTOP   6
#define VMIN    6
#define VTIME   7
#define VSUSP   8
#define NCCS    12

#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414

#define TTY_BUFSZ  256
#define NTTY       1

struct kernel_termios {
    uint16_t c_iflag;
    uint16_t c_oflag;
    uint16_t c_cflag;
    uint16_t c_lflag;
    uint8_t  c_cc[NCCS];
};

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

struct tty {
    struct kernel_termios termios;
    struct winsize        winsize;
    uint8_t  inq[TTY_BUFSZ];
    uint8_t  inq_head;
    uint8_t  inq_tail;
    uint8_t  canon_buf[TTY_BUFSZ];
    uint8_t  canon_len;
    uint8_t  canon_ready;
    uint8_t  fg_pgrp;
    uint8_t  minor;
    uint8_t  flags;
    uint8_t  waiting;
};

struct tty tty_table[NTTY];

/* Default control characters */
static const uint8_t default_cc[NCCS] = {
    [VINTR]  = 3,    /* ^C */
    [VQUIT]  = 28,   /* ^\ */
    [VERASE] = 8,    /* ^H */
    [VKILL]  = 21,   /* ^U */
    [VEOF]   = 4,    /* ^D */
    [VSTART] = 17,   /* ^Q */
    [VMIN]   = 1,
    [VTIME]  = 0,
    [VSUSP]  = 26,   /* ^Z */
};

/* ---- Re-implement TTY functions for host testing ---- */

/* Internal helpers (same as tty.c) */
static inline uint8_t inq_count(struct tty *t)
{
    return (uint8_t)(t->inq_head - t->inq_tail);
}

static inline int inq_full(struct tty *t)
{
    return inq_count(t) == (TTY_BUFSZ - 1);
}

static inline void inq_put(struct tty *t, uint8_t c)
{
    t->inq[t->inq_head] = c;
    t->inq_head++;
}

static inline uint8_t inq_get(struct tty *t)
{
    uint8_t c = t->inq[t->inq_tail];
    t->inq_tail++;
    return c;
}

static void inq_flush(struct tty *t)
{
    t->inq_head = t->inq_tail = 0;
    t->canon_len = 0;
    t->canon_ready = 0;
}

static void tty_echo(struct tty *t, uint8_t c)
{
    (void)t;
    if (c < 0x20 && c != '\n' && c != '\t' && c != '\b') {
        kputc('^');
        kputc(c + '@');
    } else {
        kputc(c);
    }
}

static void tty_echo_erase(struct tty *t)
{
    (void)t;
    kputc('\b');
    kputc(' ');
    kputc('\b');
}

static void tty_echo_erase_ctrl(struct tty *t)
{
    tty_echo_erase(t);
    tty_echo_erase(t);
}

static void tty_wake_reader(struct tty *t)
{
    if (t->waiting > 0 && t->waiting <= MAXPROC) {
        struct proc *p = &proctab[t->waiting - 1];
        if (p->state == P_SLEEPING)
            p->state = P_READY;
        t->waiting = 0;
    }
}

static void tty_signal(struct tty *t, int sig)
{
    (void)t;
    if (curproc)
        curproc->sig_pending |= (1u << sig);
}

/* Re-implement tty_inproc for host testing */
static void tty_inproc(int minor, uint8_t c)
{
    if (minor < 0 || minor >= NTTY)
        return;
    struct tty *t = &tty_table[minor];
    uint16_t iflag = t->termios.c_iflag;
    uint16_t lflag = t->termios.c_lflag;

    if (c == '\r') {
        if (iflag & IGNCR) return;
        if (iflag & ICRNL) c = '\n';
    } else if (c == '\n') {
        if (iflag & INLCR) c = '\r';
    }

    if (lflag & ISIG) {
        if (c == t->termios.c_cc[VINTR]) {
            tty_signal(t, SIGINT);
            if (lflag & ECHO) { kputc('^'); kputc('C'); kputc('\n'); }
            if (!(lflag & NOFLSH)) inq_flush(t);
            tty_wake_reader(t);
            return;
        }
        if (c == t->termios.c_cc[VQUIT]) {
            tty_signal(t, SIGQUIT);
            if (lflag & ECHO) { kputc('^'); kputc('\\'); kputc('\n'); }
            if (!(lflag & NOFLSH)) inq_flush(t);
            tty_wake_reader(t);
            return;
        }
        if (c == t->termios.c_cc[VSUSP]) {
            tty_signal(t, SIGTSTP);
            if (lflag & ECHO) { kputc('^'); kputc('Z'); kputc('\n'); }
            if (!(lflag & NOFLSH)) inq_flush(t);
            tty_wake_reader(t);
            return;
        }
    }

    if (lflag & ICANON) {
        if (c == t->termios.c_cc[VERASE] || c == 0x7F) {
            if (t->canon_len > 0) {
                uint8_t erased = t->canon_buf[t->canon_len - 1];
                t->canon_len--;
                if (lflag & ECHO) {
                    if (lflag & ECHOE) {
                        if (erased < 0x20 && erased != '\t')
                            tty_echo_erase_ctrl(t);
                        else
                            tty_echo_erase(t);
                    }
                }
            }
            return;
        }

        if (c == t->termios.c_cc[VKILL]) {
            if (lflag & ECHO) {
                while (t->canon_len > 0) {
                    uint8_t erased = t->canon_buf[t->canon_len - 1];
                    t->canon_len--;
                    if (lflag & ECHOE) {
                        if (erased < 0x20 && erased != '\t')
                            tty_echo_erase_ctrl(t);
                        else
                            tty_echo_erase(t);
                    }
                }
                if (lflag & ECHOK) kputc('\n');
            }
            t->canon_len = 0;
            return;
        }

        if (c == t->termios.c_cc[VEOF]) {
            for (uint8_t i = 0; i < t->canon_len && !inq_full(t); i++)
                inq_put(t, t->canon_buf[i]);
            t->canon_len = 0;
            t->canon_ready = 1;
            tty_wake_reader(t);
            return;
        }

        if (t->canon_len < TTY_BUFSZ - 1)
            t->canon_buf[t->canon_len++] = c;

        if (lflag & ECHO)
            tty_echo(t, c);
        else if ((lflag & ECHONL) && c == '\n')
            kputc('\n');

        if (c == '\n') {
            for (uint8_t i = 0; i < t->canon_len && !inq_full(t); i++)
                inq_put(t, t->canon_buf[i]);
            t->canon_len = 0;
            t->canon_ready = 1;
            tty_wake_reader(t);
        }
        return;
    }

    /* Raw mode */
    if (!inq_full(t)) {
        inq_put(t, c);
        if (lflag & ECHO)
            tty_echo(t, c);
        tty_wake_reader(t);
    }
}

/* tty_ioctl re-implementation for host testing */
static int tty_ioctl(int minor, int cmd, void *arg)
{
    if (minor < 0 || minor >= NTTY) return -ENODEV;
    struct tty *t = &tty_table[minor];
    switch (cmd) {
    case TCGETS:
        memcpy(arg, &t->termios, sizeof(t->termios));
        return 0;
    case TCSETS: case TCSETSW: case TCSETSF:
        memcpy(&t->termios, arg, sizeof(t->termios));
        if (cmd == TCSETSF) inq_flush(t);
        return 0;
    case TIOCGWINSZ:
        memcpy(arg, &t->winsize, sizeof(t->winsize));
        return 0;
    case TIOCSWINSZ:
        memcpy(&t->winsize, arg, sizeof(t->winsize));
        return 0;
    default:
        return -EINVAL;
    }
}

/* ---- Test setup ---- */

static void init_test(void)
{
    memset(proctab, 0, sizeof(proctab));
    curproc = &proctab[0];
    curproc->state = P_RUNNING;
    curproc->pid = 0;
    curproc->sig_pending = 0;

    struct tty *t = &tty_table[0];
    memset(t, 0, sizeof(*t));
    t->termios.c_iflag = ICRNL;
    t->termios.c_oflag = OPOST | ONLCR;
    t->termios.c_lflag = ICANON | ECHO | ECHOE | ISIG;
    memcpy(t->termios.c_cc, default_cc, NCCS);
    t->winsize.ws_row = 28;
    t->winsize.ws_col = 40;

    reset_output();
    reset_input();
}

/* ======== Tests ======== */

/* -- Circular buffer tests -- */

static void test_inq_empty(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    ASSERT_EQ(inq_count(t), 0);
    ASSERT(!inq_full(t));
}

static void test_inq_put_get(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    inq_put(t, 'A');
    ASSERT_EQ(inq_count(t), 1);
    uint8_t c = inq_get(t);
    ASSERT_EQ(c, 'A');
    ASSERT_EQ(inq_count(t), 0);
}

static void test_inq_wrap(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    /* Fill and drain to force wrapping */
    for (int i = 0; i < 250; i++)
        inq_put(t, 'x');
    for (int i = 0; i < 250; i++)
        inq_get(t);
    /* Now head/tail are at 250 — put more to wrap */
    inq_put(t, 'A');
    inq_put(t, 'B');
    inq_put(t, 'C');
    ASSERT_EQ(inq_count(t), 3);
    ASSERT_EQ(inq_get(t), 'A');
    ASSERT_EQ(inq_get(t), 'B');
    ASSERT_EQ(inq_get(t), 'C');
    ASSERT_EQ(inq_count(t), 0);
}

static void test_inq_full(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    for (int i = 0; i < TTY_BUFSZ - 1; i++)
        inq_put(t, 'x');
    ASSERT(inq_full(t));
}

/* -- Canonical mode tests -- */

static void test_canon_basic_line(void)
{
    init_test();
    struct tty *t = &tty_table[0];

    /* Feed "hello\n" through tty_inproc */
    tty_inproc(0, 'h');
    tty_inproc(0, 'e');
    tty_inproc(0, 'l');
    tty_inproc(0, 'l');
    tty_inproc(0, 'o');
    tty_inproc(0, '\n');

    /* Should have "hello\n" in inq */
    ASSERT(t->canon_ready);
    ASSERT_EQ(inq_count(t), 6);

    char buf[32];
    int i = 0;
    while (inq_count(t) > 0 && i < 31)
        buf[i++] = inq_get(t);
    buf[i] = '\0';
    ASSERT_STR_EQ(buf, "hello\n");
}

static void test_canon_echo(void)
{
    init_test();
    reset_output();

    tty_inproc(0, 'A');
    tty_inproc(0, 'B');
    tty_inproc(0, '\n');

    /* Output should contain echoed "AB\n" */
    ASSERT(strstr(output_buf, "AB") != NULL);
}

static void test_canon_erase(void)
{
    init_test();
    struct tty *t = &tty_table[0];

    tty_inproc(0, 'h');
    tty_inproc(0, 'e');
    tty_inproc(0, 'x');  /* will be erased */
    tty_inproc(0, 8);    /* ^H — erase 'x' */
    tty_inproc(0, 'l');
    tty_inproc(0, '\n');

    ASSERT(t->canon_ready);
    char buf[32];
    int i = 0;
    while (inq_count(t) > 0 && i < 31)
        buf[i++] = inq_get(t);
    buf[i] = '\0';
    ASSERT_STR_EQ(buf, "hel\n");
}

static void test_canon_erase_empty(void)
{
    init_test();
    struct tty *t = &tty_table[0];

    /* Erasing on empty line should be harmless */
    tty_inproc(0, 8);
    ASSERT_EQ(t->canon_len, 0);
    ASSERT_EQ(inq_count(t), 0);
}

static void test_canon_kill(void)
{
    init_test();
    struct tty *t = &tty_table[0];

    tty_inproc(0, 'h');
    tty_inproc(0, 'e');
    tty_inproc(0, 'l');
    tty_inproc(0, 21);   /* ^U — kill line */
    ASSERT_EQ(t->canon_len, 0);

    tty_inproc(0, 'x');
    tty_inproc(0, '\n');

    char buf[32];
    int i = 0;
    while (inq_count(t) > 0 && i < 31)
        buf[i++] = inq_get(t);
    buf[i] = '\0';
    ASSERT_STR_EQ(buf, "x\n");
}

static void test_canon_eof_empty(void)
{
    init_test();
    struct tty *t = &tty_table[0];

    /* ^D on empty line should set canon_ready with no data */
    tty_inproc(0, 4);  /* ^D */
    ASSERT(t->canon_ready);
    ASSERT_EQ(inq_count(t), 0);
}

static void test_canon_eof_with_data(void)
{
    init_test();
    struct tty *t = &tty_table[0];

    tty_inproc(0, 'a');
    tty_inproc(0, 'b');
    tty_inproc(0, 4);  /* ^D — flush without newline */

    ASSERT(t->canon_ready);
    ASSERT_EQ(inq_count(t), 2);
    char buf[4];
    buf[0] = inq_get(t);
    buf[1] = inq_get(t);
    buf[2] = '\0';
    ASSERT_STR_EQ(buf, "ab");
}

static void test_canon_del_erase(void)
{
    init_test();
    struct tty *t = &tty_table[0];

    tty_inproc(0, 'a');
    tty_inproc(0, 'b');
    tty_inproc(0, 0x7F);  /* DEL acts as erase */
    tty_inproc(0, '\n');

    char buf[32];
    int i = 0;
    while (inq_count(t) > 0 && i < 31)
        buf[i++] = inq_get(t);
    buf[i] = '\0';
    ASSERT_STR_EQ(buf, "a\n");
}

/* -- CR/NL mapping tests -- */

static void test_icrnl(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    /* ICRNL is set by default — CR should become NL */
    tty_inproc(0, 'h');
    tty_inproc(0, 'i');
    tty_inproc(0, '\r');  /* should become \n */

    ASSERT(t->canon_ready);
    char buf[8];
    int i = 0;
    while (inq_count(t) > 0 && i < 7)
        buf[i++] = inq_get(t);
    buf[i] = '\0';
    ASSERT_STR_EQ(buf, "hi\n");
}

static void test_igncr(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    t->termios.c_iflag = IGNCR;

    tty_inproc(0, 'a');
    tty_inproc(0, '\r');  /* should be ignored */
    ASSERT_EQ(t->canon_len, 1);
    ASSERT(!t->canon_ready);  /* no newline, no line complete */
}

static void test_inlcr(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    t->termios.c_iflag = INLCR;

    tty_inproc(0, 'a');
    tty_inproc(0, '\n');  /* should become \r — NOT a line terminator */
    /* In canonical mode, \r is not a line terminator, so no ready */
    ASSERT(!t->canon_ready);
    ASSERT_EQ(t->canon_len, 2);
}

/* -- Signal generation tests -- */

static void test_sigint(void)
{
    init_test();
    curproc->sig_pending = 0;

    tty_inproc(0, 3);  /* ^C */
    ASSERT(curproc->sig_pending & (1u << SIGINT));
}

static void test_sigquit(void)
{
    init_test();
    curproc->sig_pending = 0;

    tty_inproc(0, 28);  /* ^\ */
    ASSERT(curproc->sig_pending & (1u << SIGQUIT));
}

static void test_sigtstp(void)
{
    init_test();
    curproc->sig_pending = 0;

    tty_inproc(0, 26);  /* ^Z */
    ASSERT(curproc->sig_pending & (1u << SIGTSTP));
}

static void test_signal_flushes_input(void)
{
    init_test();
    struct tty *t = &tty_table[0];

    tty_inproc(0, 'a');
    tty_inproc(0, 'b');
    tty_inproc(0, 3);  /* ^C — should flush */

    ASSERT_EQ(t->canon_len, 0);
    ASSERT_EQ(inq_count(t), 0);
}

static void test_signal_noflsh(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    t->termios.c_lflag |= NOFLSH;

    tty_inproc(0, 'a');
    tty_inproc(0, 'b');
    ASSERT_EQ(t->canon_len, 2);

    tty_inproc(0, 3);  /* ^C with NOFLSH — should NOT flush */
    ASSERT(curproc->sig_pending & (1u << SIGINT));
    ASSERT_EQ(t->canon_len, 2);  /* data preserved */
}

static void test_signal_isig_off(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    t->termios.c_lflag &= ~ISIG;
    curproc->sig_pending = 0;

    tty_inproc(0, 3);  /* ^C without ISIG — treated as normal char */
    ASSERT_EQ(curproc->sig_pending, 0u);
    ASSERT_EQ(t->canon_len, 1);  /* ^C stored as data */
}

/* -- Raw mode tests -- */

static void test_raw_immediate(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    /* Switch to raw mode */
    t->termios.c_lflag &= ~ICANON;

    tty_inproc(0, 'A');
    ASSERT_EQ(inq_count(t), 1);
    ASSERT_EQ(inq_get(t), 'A');
}

static void test_raw_no_line_editing(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    t->termios.c_lflag &= ~ICANON;

    tty_inproc(0, 'a');
    tty_inproc(0, 8);    /* ^H in raw mode — just data, not erase */
    tty_inproc(0, 'b');

    ASSERT_EQ(inq_count(t), 3);
    ASSERT_EQ(inq_get(t), 'a');
    ASSERT_EQ(inq_get(t), 8);
    ASSERT_EQ(inq_get(t), 'b');
}

static void test_raw_echo(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    t->termios.c_lflag = ECHO;  /* raw + echo */
    reset_output();

    tty_inproc(0, 'X');
    ASSERT(strstr(output_buf, "X") != NULL);
}

static void test_raw_no_echo(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    t->termios.c_lflag = 0;  /* raw, no echo */
    reset_output();

    tty_inproc(0, 'X');
    ASSERT_EQ(output_pos, 0);
    ASSERT_EQ(inq_count(t), 1);
}

/* -- ioctl tests -- */

static void test_ioctl_tcgets(void)
{
    init_test();
    struct kernel_termios got;
    int rc = tty_ioctl(0, TCGETS, &got);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(got.c_lflag, (uint16_t)(ICANON | ECHO | ECHOE | ISIG));
    ASSERT_EQ(got.c_cc[VINTR], 3);
}

static void test_ioctl_tcsets(void)
{
    init_test();
    struct kernel_termios new_t;
    memset(&new_t, 0, sizeof(new_t));
    new_t.c_lflag = 0;  /* raw mode */

    int rc = tty_ioctl(0, TCSETS, &new_t);
    ASSERT_EQ(rc, 0);

    struct tty *t = &tty_table[0];
    ASSERT_EQ(t->termios.c_lflag, 0);
}

static void test_ioctl_tcsetsf_flushes(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    inq_put(t, 'x');
    inq_put(t, 'y');
    ASSERT_EQ(inq_count(t), 2);

    struct kernel_termios new_t = t->termios;
    int rc = tty_ioctl(0, TCSETSF, &new_t);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(inq_count(t), 0);  /* flushed */
}

static void test_ioctl_tiocgwinsz(void)
{
    init_test();
    struct winsize ws;
    int rc = tty_ioctl(0, TIOCGWINSZ, &ws);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(ws.ws_row, 28);
    ASSERT_EQ(ws.ws_col, 40);
}

static void test_ioctl_tiocswinsz(void)
{
    init_test();
    struct winsize ws = { .ws_row = 25, .ws_col = 80 };
    int rc = tty_ioctl(0, TIOCSWINSZ, &ws);
    ASSERT_EQ(rc, 0);

    struct winsize got;
    tty_ioctl(0, TIOCGWINSZ, &got);
    ASSERT_EQ(got.ws_row, 25);
    ASSERT_EQ(got.ws_col, 80);
}

static void test_ioctl_invalid(void)
{
    init_test();
    int rc = tty_ioctl(0, 0xFFFF, NULL);
    ASSERT_EQ(rc, -EINVAL);
}

static void test_ioctl_bad_minor(void)
{
    init_test();
    struct kernel_termios t;
    int rc = tty_ioctl(-1, TCGETS, &t);
    ASSERT_EQ(rc, -ENODEV);
    rc = tty_ioctl(99, TCGETS, &t);
    ASSERT_EQ(rc, -ENODEV);
}

/* -- Echo control character display -- */

static void test_echo_ctrl_char(void)
{
    init_test();
    reset_output();
    struct tty *t = &tty_table[0];
    t->termios.c_lflag &= ~ISIG;  /* disable signals so ^C is data */

    tty_inproc(0, 3);  /* ^C as data, should echo as ^C */
    ASSERT(strstr(output_buf, "^C") != NULL);
}

/* -- Constants and structure layout -- */

static void test_termios_struct_size(void)
{
    /* Verify struct fits expectations */
    ASSERT(sizeof(struct kernel_termios) >= 8 + NCCS);
    ASSERT(sizeof(struct winsize) == 8);
}

static void test_cc_defaults(void)
{
    init_test();
    struct tty *t = &tty_table[0];
    ASSERT_EQ(t->termios.c_cc[VINTR], 3);    /* ^C */
    ASSERT_EQ(t->termios.c_cc[VQUIT], 28);   /* ^\ */
    ASSERT_EQ(t->termios.c_cc[VERASE], 8);   /* ^H */
    ASSERT_EQ(t->termios.c_cc[VKILL], 21);   /* ^U */
    ASSERT_EQ(t->termios.c_cc[VEOF], 4);     /* ^D */
    ASSERT_EQ(t->termios.c_cc[VSUSP], 26);   /* ^Z */
}

/* ======== Main ======== */

int main(void)
{
    printf("test_tty:\n");

    /* Circular buffer */
    RUN_TEST(test_inq_empty);
    RUN_TEST(test_inq_put_get);
    RUN_TEST(test_inq_wrap);
    RUN_TEST(test_inq_full);

    /* Canonical mode */
    RUN_TEST(test_canon_basic_line);
    RUN_TEST(test_canon_echo);
    RUN_TEST(test_canon_erase);
    RUN_TEST(test_canon_erase_empty);
    RUN_TEST(test_canon_kill);
    RUN_TEST(test_canon_eof_empty);
    RUN_TEST(test_canon_eof_with_data);
    RUN_TEST(test_canon_del_erase);

    /* CR/NL mapping */
    RUN_TEST(test_icrnl);
    RUN_TEST(test_igncr);
    RUN_TEST(test_inlcr);

    /* Signal generation */
    RUN_TEST(test_sigint);
    RUN_TEST(test_sigquit);
    RUN_TEST(test_sigtstp);
    RUN_TEST(test_signal_flushes_input);
    RUN_TEST(test_signal_noflsh);
    RUN_TEST(test_signal_isig_off);

    /* Raw mode */
    RUN_TEST(test_raw_immediate);
    RUN_TEST(test_raw_no_line_editing);
    RUN_TEST(test_raw_echo);
    RUN_TEST(test_raw_no_echo);

    /* ioctl */
    RUN_TEST(test_ioctl_tcgets);
    RUN_TEST(test_ioctl_tcsets);
    RUN_TEST(test_ioctl_tcsetsf_flushes);
    RUN_TEST(test_ioctl_tiocgwinsz);
    RUN_TEST(test_ioctl_tiocswinsz);
    RUN_TEST(test_ioctl_invalid);
    RUN_TEST(test_ioctl_bad_minor);

    /* Echo and display */
    RUN_TEST(test_echo_ctrl_char);

    /* Constants */
    RUN_TEST(test_termios_struct_size);
    RUN_TEST(test_cc_defaults);

    TEST_REPORT();
}
