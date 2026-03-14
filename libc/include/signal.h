/* Genix signal.h */
#ifndef _SIGNAL_H
#define _SIGNAL_H

/* Signal numbers */
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20

#define NSIG     21   /* signals 0..20 */

/* Signal dispositions */
#define SIG_DFL  ((void (*)(int))0)
#define SIG_IGN  ((void (*)(int))1)
#define SIG_ERR  ((void (*)(int))-1)

typedef void (*sighandler_t)(int);

/* Signal set type and operations */
typedef unsigned long sigset_t;

#define sigemptyset(set)     (*(set) = 0, 0)
#define sigfillset(set)      (*(set) = ~0UL, 0)
#define sigaddset(set, sig)  (*(set) |= (1UL << (sig)), 0)
#define sigdelset(set, sig)  (*(set) &= ~(1UL << (sig)), 0)
#define sigismember(set, sig) ((*(set) >> (sig)) & 1)

/* sigaction flags */
#define SA_RESTART   0x10000000
#define SA_NOCLDSTOP 0x00000001
#define SA_RESETHAND 0x80000000

struct sigaction {
    void      (*sa_handler)(int);
    sigset_t  sa_mask;
    int       sa_flags;
};

/* Function declarations */
int signal(int sig, void *handler);
int kill(int pid, int sig);
int raise(int sig);
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oset);

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#endif
