/* Genix minimal termios.h */
#ifndef _TERMIOS_H
#define _TERMIOS_H

/* c_lflag bits */
#define ISIG    0x0001
#define ICANON  0x0002
#define ECHO    0x0008

/* c_iflag bits */
#define ICRNL   0x0100
#define INLCR   0x0040
#define IXOFF   0x1000
#define IXANY   0x0800

/* c_cc indices */
#define VINTR   0
#define VQUIT   1
#define VERASE  2
#define VKILL   3
#define VEOF    4
#define VMIN    6
#define VTIME   7
#define NCCS    12

/* Speed constants */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

struct termios {
    unsigned short c_iflag;
    unsigned short c_oflag;
    unsigned short c_cflag;
    unsigned short c_lflag;
    unsigned char  c_cc[NCCS];
};

int tcgetattr(int fd, struct termios *tp);
int tcsetattr(int fd, int action, const struct termios *tp);

#endif
