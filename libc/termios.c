/*
 * Minimal termios for Genix user programs.
 * Wraps ioctl syscalls for terminal control.
 */
#include <termios.h>

extern int ioctl(int fd, int cmd, void *arg);

int tcgetattr(int fd, struct termios *tp)
{
    return ioctl(fd, TCGETS, tp);
}

int tcsetattr(int fd, int action, const struct termios *tp)
{
    int cmd;
    switch (action) {
    case TCSADRAIN: cmd = TCSETSW; break;
    case TCSAFLUSH: cmd = TCSETSF; break;
    default:        cmd = TCSETS;   break;
    }
    return ioctl(fd, cmd, (void *)tp);
}
