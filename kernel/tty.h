/*
 * TTY subsystem — line discipline, termios, circular buffer
 *
 * Three-layer architecture:
 *   User: read()/write()/ioctl() on terminal fds
 *   tty.c: Line discipline (cooked/raw, echo, erase, signals)
 *   PAL: pal_console_putc()/pal_console_getc() hardware drivers
 *
 * Ported from Fuzix tty.c concepts, simplified for Genix.
 */
#ifndef TTY_H
#define TTY_H

#include <stdint.h>

/* ======== termios constants ======== */

/* c_iflag bits */
#define ICRNL   0x0100  /* map CR to NL on input */
#define INLCR   0x0040  /* map NL to CR on input */
#define IGNCR   0x0080  /* ignore CR on input */
#define IXOFF   0x1000
#define IXANY   0x0800

/* c_oflag bits */
#define OPOST   0x0001  /* post-process output */
#define ONLCR   0x0004  /* map NL to CR-NL on output */

/* c_lflag bits */
#define ISIG    0x0001  /* enable signals (^C, ^\, ^Z) */
#define ICANON  0x0002  /* canonical (line-buffered) input */
#define ECHO    0x0008  /* echo input characters */
#define ECHOE   0x0010  /* echo erase as BS-SP-BS */
#define ECHOK   0x0020  /* echo NL after kill */
#define ECHONL  0x0040  /* echo NL even if ECHO is off */
#define NOFLSH  0x0080  /* don't flush after signal */

/* c_cc indices */
#define VINTR   0   /* ^C */
#define VQUIT   1   /* ^\ */
#define VERASE  2   /* ^H / DEL */
#define VKILL   3   /* ^U */
#define VEOF    4   /* ^D */
#define VSTART  5   /* ^Q */
#define VSTOP   6   /* ^S (reused slot, was VMIN) */
#define VMIN    6   /* alias for raw mode min chars */
#define VTIME   7   /* raw mode timeout */
#define VSUSP   8   /* ^Z */
#define NCCS    12

/* ioctl commands */
#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define TIOCGPGRP   0x540F
#define TIOCSPGRP   0x5410

/* ======== TTY data structures ======== */

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
    uint16_t ws_xpixel;  /* unused, for POSIX compat */
    uint16_t ws_ypixel;  /* unused, for POSIX compat */
};

/*
 * 256-byte circular buffer with uint8_t head/tail.
 * Wraps at 256 naturally — no modulo instruction on the 68000.
 * This is a deliberate optimization from Fuzix.
 */
#define TTY_BUFSZ  256

/* TTY flags */
#define TTY_STOPPED   0x01  /* output stopped (^S) */
#define TTY_THROTTLE  0x02  /* input throttled */

#define NTTY  1  /* single TTY for now */

struct tty {
    struct kernel_termios termios;
    struct winsize        winsize;
    uint8_t  inq[TTY_BUFSZ];   /* input circular buffer */
    uint8_t  inq_head;          /* write pointer (producer writes here) */
    uint8_t  inq_tail;          /* read pointer (consumer reads here) */
    /* Canonical mode line buffer — accumulates until newline/EOF.
     * Separate from inq so we can erase characters before they
     * become visible to readers. */
    uint8_t  canon_buf[TTY_BUFSZ];
    uint8_t  canon_len;         /* bytes in canonical buffer */
    uint8_t  canon_ready;       /* line complete, ready to drain */
    uint8_t  fg_pgrp;           /* foreground process group (unused for now) */
    uint8_t  minor;
    uint8_t  flags;
    uint8_t  waiting;           /* pid+1 of process blocked on read (0=none) */
};

extern struct tty tty_table[];

/* ======== TTY interface ======== */

void tty_init(void);
int  tty_read(int minor, void *buf, int len);
int  tty_write(int minor, const void *buf, int len);
int  tty_ioctl(int minor, int cmd, void *arg);

/*
 * tty_inproc() — feed a character into the TTY input path.
 *
 * Called from the polling loop (or eventually from an ISR).
 * Handles:
 *   - Input flag processing (ICRNL, IGNCR, INLCR)
 *   - Signal generation (^C, ^\, ^Z) when ISIG
 *   - Canonical mode: echo, erase, kill, line buffering
 *   - Raw mode: immediate character delivery
 *
 * ISR-safe: only writes inq_head (single byte, atomic on 68000).
 */
void tty_inproc(int minor, uint8_t c);

/* Helper: check if TTY input buffer has data ready for reading */
int tty_data_ready(int minor);

#endif /* TTY_H */
