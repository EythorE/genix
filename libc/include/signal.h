/* Genix minimal signal.h */
#ifndef _SIGNAL_H
#define _SIGNAL_H

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGKILL  9
#define SIGPIPE  13
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20

#define SIG_DFL  ((void (*)(int))0)
#define SIG_IGN  ((void (*)(int))1)
#define SIG_ERR  ((void (*)(int))-1)

typedef void (*sighandler_t)(int);

int signal(int sig, void *handler);
int kill(int pid, int sig);

#endif
