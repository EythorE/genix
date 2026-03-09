/*
 * isatty — check if a file descriptor is a terminal.
 *
 * Uses ioctl(fd, TCGETS, ...) and checks for ENOTTY.
 * On Genix, fds 0/1/2 are the console.
 */

extern int ioctl(int fd, int cmd, void *arg);

#define TCGETS 0x5401

int isatty(int fd)
{
    char buf[44];  /* termios struct — we don't care about the contents */
    return ioctl(fd, TCGETS, buf) >= 0;
}
