/* Genix termios.h — terminal I/O interface */
#ifndef _TERMIOS_H
#define _TERMIOS_H

/* c_iflag bits */
#define ICRNL   0x0100
#define INLCR   0x0040
#define IGNCR   0x0080
#define IXOFF   0x1000
#define IXANY   0x0800

/* c_oflag bits */
#define OPOST   0x0001
#define ONLCR   0x0004

/* c_lflag bits */
#define ISIG    0x0001
#define ICANON  0x0002
#define ECHO    0x0008
#define ECHOE   0x0010
#define ECHOK   0x0020
#define ECHONL  0x0040
#define NOFLSH  0x0080

/* c_cc indices */
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

/* tcsetattr actions */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* ioctl numbers (for direct ioctl use) */
#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define TIOCGPGRP   0x540F
#define TIOCSPGRP   0x5410

struct termios {
    unsigned short c_iflag;
    unsigned short c_oflag;
    unsigned short c_cflag;
    unsigned short c_lflag;
    unsigned char  c_cc[NCCS];
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int tcgetattr(int fd, struct termios *tp);
int tcsetattr(int fd, int action, const struct termios *tp);

#endif
