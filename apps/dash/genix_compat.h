/* genix_compat.h — Genix-specific shims for dash.
 *
 * Included by config.h before any dash source. Provides missing
 * POSIX interfaces that dash needs but Genix libc doesn't have. */

#ifndef GENIX_COMPAT_H
#define GENIX_COMPAT_H

/* ---- fork → vfork ---- */
#define fork() vfork()

/* ---- wait3 → waitpid ---- */
/* dash uses wait3(status, flags, NULL) — third arg (rusage) is always NULL.
 * Equivalent to waitpid(-1, status, flags). */
#define wait3(status, flags, rusage) waitpid(-1, (status), (flags))

/* ---- strtoimax/strtoumax ---- */
/* GCC's intmax_t is long long. Map to strtoll/strtoull.
 * These are declared here since Genix libc doesn't have them yet. */
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
#define strtoimax(nptr, endptr, base) strtoll((nptr), (endptr), (base))
#define strtoumax(nptr, endptr, base) strtoull((nptr), (endptr), (base))

/* ---- sigsuspend stub ---- */
#include <signal.h>
static inline int sigsuspend(const sigset_t *mask)
{
    (void)mask;
    return -1;
}

/* ---- job control stubs (JOBS=0) ---- */
/* builtins.c references bgcmd/fgcmd unconditionally */
int bgcmd(int argc, char **argv);
int fgcmd(int argc, char **argv);

/* ---- strsignal ---- */
/* Genix doesn't have sys_siglist so provide a simple strsignal */
char *strsignal(int sig);

/* ---- alloca ---- */
#define alloca __builtin_alloca

/* ---- fcntl ---- */
#include <fcntl.h>
int fcntl(int fd, int cmd, ...);

/* ---- lstat = stat (no symlinks) ---- */
#define lstat stat

/* ---- setpgrp ---- */
/* dash calls setpgrp(0,0) — BSD form = setpgid(0,0) */
#include <unistd.h>
#define setpgrp(a, b) setpgid((a), (b))

/* ---- umask ---- */
#include <sys/types.h>
static inline mode_t umask(mode_t mask) { (void)mask; return 022; }

#endif /* GENIX_COMPAT_H */
