/*
 * unistd_stubs.c — POSIX stubs for single-user Genix
 *
 * These functions exist so that POSIX programs compile and link.
 * Genix is single-user with no permissions model, so UID/GID
 * functions always return 0 and set functions are no-ops.
 */
#include <unistd.h>
#include <errno.h>

/* UID/GID — always root (0) */
int getuid(void)  { return 0; }
int geteuid(void) { return 0; }
int getgid(void)  { return 0; }
int getegid(void) { return 0; }
int setuid(int uid)  { (void)uid; return 0; }
int setgid(int gid)  { (void)gid; return 0; }
int seteuid(int uid) { (void)uid; return 0; }
int setegid(int gid) { (void)gid; return 0; }

/* Process groups — single-user, return pid as pgrp */
int getpgrp(void)             { return getpid(); }
int setpgid(int pid, int pgid) { (void)pid; (void)pgid; return 0; }
int getppid(void)             { return 1; }  /* init is always parent */
int tcsetpgrp(int fd, int pgrp) { (void)fd; (void)pgrp; return 0; }
int tcgetpgrp(int fd)          { (void)fd; return getpid(); }

/* Timer stubs */
unsigned int alarm(unsigned int seconds)
{
    (void)seconds;
    return 0;  /* no previous alarm */
}

unsigned int sleep(unsigned int seconds)
{
    (void)seconds;
    return 0;  /* pretend we slept */
}

/* sysconf — return hardcoded values */
long sysconf(int name)
{
    switch (name) {
    case _SC_CLK_TCK:
        return 60;  /* 60 Hz tick (Mega Drive NTSC VBlank) */
    default:
        errno = EINVAL;
        return -1;
    }
}

/* access — single-user, all files are accessible */
int access(const char *path, int mode)
{
    /* Use stat to check if file exists */
    extern int stat(const char *path, void *buf);
    char buf[32];  /* enough for struct stat */
    (void)mode;
    return stat(path, buf) < 0 ? -1 : 0;
}
