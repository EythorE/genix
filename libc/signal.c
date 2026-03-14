/*
 * signal.c — POSIX signal wrappers for Genix
 *
 * sigaction() wraps the kernel's signal() syscall.
 * sigprocmask() is a stub (no real signal mask support yet).
 * raise() sends a signal to the current process.
 */
#include <signal.h>
#include <unistd.h>
#include <errno.h>

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    if (sig < 1 || sig >= NSIG) {
        errno = EINVAL;
        return -1;
    }

    if (oact) {
        /* Get current handler via signal(sig, current) trick:
         * signal() returns the old handler, so call it twice
         * to read without changing. */
        int old = signal(sig, SIG_DFL);
        oact->sa_handler = (void (*)(int))(long)old;
        oact->sa_mask = 0;
        oact->sa_flags = 0;
        /* Restore the old handler if we're not setting a new one */
        if (!act) {
            signal(sig, (void *)(long)old);
            return 0;
        }
    }

    if (act) {
        int r = signal(sig, (void *)act->sa_handler);
        if (r == (int)(long)SIG_ERR) {
            errno = EINVAL;
            return -1;
        }
    }

    return 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
    /* Stub: Genix doesn't support signal masks yet */
    (void)how;
    (void)set;
    if (oset)
        *oset = 0;
    return 0;
}

int raise(int sig)
{
    return kill(getpid(), sig);
}
