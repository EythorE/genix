/*
 * Minimal termios for Genix user programs.
 * Wraps ioctl syscalls for terminal control.
 */
#include <termios.h>

/* ioctl commands */
#define TCGETS  0x5401
#define TCSETS  0x5402

extern int ioctl(int fd, int cmd, void *arg);

int tcgetattr(int fd, struct termios *tp)
{
    return ioctl(fd, TCGETS, tp);
}

int tcsetattr(int fd, int action, const struct termios *tp)
{
    (void)action;  /* We always apply immediately */
    return ioctl(fd, TCSETS, (void *)tp);
}
