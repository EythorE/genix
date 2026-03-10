/*
 * TTY subsystem — line discipline for Genix
 *
 * Implements cooked (canonical) and raw input modes, echo,
 * erase/kill editing, signal generation, and termios ioctls.
 *
 * Inspired by Fuzix tty.c, simplified for single-user Genix.
 * The 256-byte circular buffer uses uint8_t indices that wrap
 * naturally at 256 — no modulo instruction on the 68000.
 */
#include "kernel.h"
#include "tty.h"

/* Single TTY for now — expandable to multi-TTY later */
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

/* Default termios settings */
static const struct kernel_termios default_termios = {
    .c_iflag = ICRNL,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = 0,
    .c_lflag = ICANON | ECHO | ECHOE | ISIG,
};

void tty_init(void)
{
    for (int i = 0; i < NTTY; i++) {
        struct tty *t = &tty_table[i];
        t->termios = default_termios;
        memcpy(t->termios.c_cc, default_cc, NCCS);
        t->winsize.ws_row = 28;   /* Mega Drive VDP: 28 rows */
        t->winsize.ws_col = 40;   /* Mega Drive VDP: 40 cols */
        t->winsize.ws_xpixel = 0;
        t->winsize.ws_ypixel = 0;
        t->inq_head = 0;
        t->inq_tail = 0;
        t->canon_len = 0;
        t->canon_ready = 0;
        t->fg_pgrp = 0;
        t->minor = i;
        t->flags = 0;
        t->waiting = 0;
    }
    kputs("[tty] TTY subsystem initialized.\n");
}

/* ---- Internal helpers ---- */

/* Number of bytes available in input queue */
static inline uint8_t inq_count(struct tty *t)
{
    return (uint8_t)(t->inq_head - t->inq_tail);
}

/* Check if input queue is full */
static inline int inq_full(struct tty *t)
{
    return inq_count(t) == (TTY_BUFSZ - 1);
}

/* Put a byte into the input queue (caller checks space) */
static inline void inq_put(struct tty *t, uint8_t c)
{
    t->inq[t->inq_head] = c;
    t->inq_head++;  /* uint8_t wraps at 256 naturally */
}

/* Get a byte from the input queue (caller checks non-empty) */
static inline uint8_t inq_get(struct tty *t)
{
    uint8_t c = t->inq[t->inq_tail];
    t->inq_tail++;  /* uint8_t wraps at 256 naturally */
    return c;
}

/* Flush the input queue */
static void inq_flush(struct tty *t)
{
    t->inq_head = t->inq_tail = 0;
    t->canon_len = 0;
    t->canon_ready = 0;
}

/* Echo a character to the console output */
static void tty_echo(struct tty *t, uint8_t c)
{
    (void)t;
    if (c < 0x20 && c != '\n' && c != '\t' && c != '\b') {
        /* Control character: echo as ^X */
        kputc('^');
        kputc(c + '@');
    } else {
        kputc(c);
    }
}

/* Echo erase: backspace-space-backspace */
static void tty_echo_erase(struct tty *t)
{
    (void)t;
    kputc('\b');
    kputc(' ');
    kputc('\b');
}

/* Echo erase for a control char (^X = 2 chars wide) */
static void tty_echo_erase_ctrl(struct tty *t)
{
    tty_echo_erase(t);
    tty_echo_erase(t);
}

/* Wake any process blocked on this TTY's input */
static void tty_wake_reader(struct tty *t)
{
    if (t->waiting > 0 && t->waiting <= MAXPROC) {
        struct proc *p = &proctab[t->waiting - 1];
        if (p->state == P_SLEEPING)
            p->state = P_READY;
        t->waiting = 0;
    }
}

/* ---- Signal generation ---- */

/*
 * Send a signal to the foreground process (or current process).
 * In a full implementation, this would send to the foreground
 * process group. For now, signal the current process.
 */
static void tty_signal(struct tty *t, int sig)
{
    (void)t;
    if (curproc) {
        curproc->sig_pending |= (1u << sig);
    }
}

/* ---- Input processing ---- */

/*
 * tty_inproc — process an incoming character through the line discipline.
 *
 * This is the heart of the TTY subsystem. It handles:
 *   - Input flag processing (CR/NL mapping)
 *   - Signal generation (ISIG: ^C, ^\, ^Z)
 *   - Canonical mode: line editing (erase, kill), echo, line buffering
 *   - Raw mode: immediate character delivery to input queue
 */
void tty_inproc(int minor, uint8_t c)
{
    if (minor < 0 || minor >= NTTY)
        return;
    struct tty *t = &tty_table[minor];
    uint16_t iflag = t->termios.c_iflag;
    uint16_t lflag = t->termios.c_lflag;

    /* Input flag processing: CR/NL mapping */
    if (c == '\r') {
        if (iflag & IGNCR)
            return;
        if (iflag & ICRNL)
            c = '\n';
    } else if (c == '\n') {
        if (iflag & INLCR)
            c = '\r';
    }

    /* Signal generation (ISIG) */
    if (lflag & ISIG) {
        if (c == t->termios.c_cc[VINTR]) {
            tty_signal(t, SIGINT);
            if (lflag & ECHO) {
                kputc('^');
                kputc('C');
                kputc('\n');
            }
            if (!(lflag & NOFLSH))
                inq_flush(t);
            tty_wake_reader(t);
            return;
        }
        if (c == t->termios.c_cc[VQUIT]) {
            tty_signal(t, SIGQUIT);
            if (lflag & ECHO) {
                kputc('^');
                kputc('\\');
                kputc('\n');
            }
            if (!(lflag & NOFLSH))
                inq_flush(t);
            tty_wake_reader(t);
            return;
        }
        if (c == t->termios.c_cc[VSUSP]) {
            tty_signal(t, SIGTSTP);
            if (lflag & ECHO) {
                kputc('^');
                kputc('Z');
                kputc('\n');
            }
            if (!(lflag & NOFLSH))
                inq_flush(t);
            tty_wake_reader(t);
            return;
        }
    }

    /* Canonical mode: line editing + buffering */
    if (lflag & ICANON) {
        /* Handle erase (^H or DEL) */
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

        /* Handle kill (^U) — erase entire line */
        if (c == t->termios.c_cc[VKILL]) {
            if (lflag & ECHO) {
                /* Erase each character visually */
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
                if (lflag & ECHOK)
                    kputc('\n');
            }
            t->canon_len = 0;
            return;
        }

        /* Handle EOF (^D) */
        if (c == t->termios.c_cc[VEOF]) {
            /* ^D on an empty line: signal EOF (0-length read).
             * ^D with data: flush current line without newline. */
            /* Transfer canon_buf to inq */
            for (uint8_t i = 0; i < t->canon_len && !inq_full(t); i++)
                inq_put(t, t->canon_buf[i]);
            t->canon_len = 0;
            t->canon_ready = 1;
            tty_wake_reader(t);
            return;
        }

        /* Regular character in canonical mode */
        if (t->canon_len < TTY_BUFSZ - 1) {
            t->canon_buf[t->canon_len++] = c;
        }

        /* Echo */
        if (lflag & ECHO)
            tty_echo(t, c);
        else if ((lflag & ECHONL) && c == '\n')
            kputc('\n');

        /* Newline completes the line */
        if (c == '\n') {
            /* Transfer canon_buf to inq */
            for (uint8_t i = 0; i < t->canon_len && !inq_full(t); i++)
                inq_put(t, t->canon_buf[i]);
            t->canon_len = 0;
            t->canon_ready = 1;
            tty_wake_reader(t);
        }
        return;
    }

    /* Raw mode: immediate character delivery */
    if (!inq_full(t)) {
        inq_put(t, c);
        if (lflag & ECHO)
            tty_echo(t, c);
        tty_wake_reader(t);
    }
}

/* Check if data is ready for reading */
int tty_data_ready(int minor)
{
    if (minor < 0 || minor >= NTTY)
        return 0;
    struct tty *t = &tty_table[minor];
    if (t->termios.c_lflag & ICANON)
        return t->canon_ready && inq_count(t) > 0;
    return inq_count(t) > 0;
}

/*
 * tty_read — read from the TTY input buffer.
 *
 * In canonical mode: blocks until a complete line is available,
 * then returns up to one line.
 *
 * In raw mode: returns available characters immediately (at
 * least VMIN characters, with VTIME timeout — for now, we
 * just block until at least one character is available).
 *
 * Polls for input while waiting (no interrupt-driven keyboard yet).
 */
int tty_read(int minor, void *buf, int len)
{
    if (minor < 0 || minor >= NTTY)
        return -ENODEV;
    if (len <= 0)
        return 0;

    struct tty *t = &tty_table[minor];
    uint8_t *dst = (uint8_t *)buf;
    int n = 0;

    if (t->termios.c_lflag & ICANON) {
        /* Canonical mode: wait for a complete line */
        while (!t->canon_ready || inq_count(t) == 0) {
            /* Poll for input */
            if (pal_console_ready()) {
                tty_inproc(minor, (uint8_t)pal_console_getc());
            } else {
                /* Check for pending signals */
                if (curproc && curproc->sig_pending)
                    return -EINTR;
                /* Yield to other processes while waiting */
                if (nproc > 1) {
                    t->waiting = curproc ? curproc->pid + 1 : 0;
                    curproc->state = P_SLEEPING;
                    schedule();
                    t->waiting = 0;
                    /* After waking, check signals */
                    if (curproc && curproc->sig_pending)
                        return -EINTR;
                }
            }
        }

        /* Read up to one line from inq */
        while (n < len && inq_count(t) > 0) {
            dst[n++] = inq_get(t);
        }

        /* If inq is drained, allow next line */
        if (inq_count(t) == 0)
            t->canon_ready = 0;

    } else {
        /* Raw mode: return available characters */
        /* Wait for at least one character */
        while (inq_count(t) == 0) {
            if (pal_console_ready()) {
                tty_inproc(minor, (uint8_t)pal_console_getc());
            } else {
                if (curproc && curproc->sig_pending)
                    return -EINTR;
                if (nproc > 1) {
                    t->waiting = curproc ? curproc->pid + 1 : 0;
                    curproc->state = P_SLEEPING;
                    schedule();
                    t->waiting = 0;
                    if (curproc && curproc->sig_pending)
                        return -EINTR;
                }
            }
        }

        /* Read available characters (up to len) */
        while (n < len && inq_count(t) > 0) {
            dst[n++] = inq_get(t);
        }
    }

    return n;
}

/*
 * tty_write — write to the TTY output.
 *
 * Applies output processing (OPOST):
 *   - ONLCR: map NL to CR-NL
 *
 * Output goes through kputc() which calls pal_console_putc().
 */
int tty_write(int minor, const void *buf, int len)
{
    if (minor < 0 || minor >= NTTY)
        return -ENODEV;

    struct tty *t = &tty_table[minor];
    const uint8_t *src = (const uint8_t *)buf;
    uint16_t oflag = t->termios.c_oflag;

    for (int i = 0; i < len; i++) {
        uint8_t c = src[i];
        if (oflag & OPOST) {
            if (c == '\n' && (oflag & ONLCR))
                pal_console_putc('\r');
        }
        pal_console_putc(c);
    }
    return len;
}

/*
 * tty_ioctl — terminal ioctl handling.
 */
int tty_ioctl(int minor, int cmd, void *arg)
{
    if (minor < 0 || minor >= NTTY)
        return -ENODEV;

    struct tty *t = &tty_table[minor];

    switch (cmd) {
    case TCGETS:
        memcpy(arg, &t->termios, sizeof(t->termios));
        return 0;

    case TCSETS:
    case TCSETSW:
    case TCSETSF:
        memcpy(&t->termios, arg, sizeof(t->termios));
        if (cmd == TCSETSF)
            inq_flush(t);
        return 0;

    case TIOCGWINSZ:
        memcpy(arg, &t->winsize, sizeof(t->winsize));
        return 0;

    case TIOCSWINSZ:
        memcpy(&t->winsize, arg, sizeof(t->winsize));
        return 0;

    case TIOCGPGRP:
        *(int *)arg = t->fg_pgrp;
        return 0;

    case TIOCSPGRP:
        t->fg_pgrp = *(int *)arg;
        return 0;

    default:
        return -EINVAL;
    }
}
