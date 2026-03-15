/*
 * kill — send signal to process
 *
 * Usage: kill [-signal] pid ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static const struct {
    const char *name;
    int num;
} sigs[] = {
    { "HUP",  SIGHUP  }, { "INT",  SIGINT  }, { "QUIT", SIGQUIT },
    { "ILL",  SIGILL  }, { "TRAP", SIGTRAP }, { "ABRT", SIGABRT },
    { "BUS",  SIGBUS  }, { "FPE",  SIGFPE  }, { "KILL", SIGKILL },
    { "USR1", SIGUSR1 }, { "SEGV", SIGSEGV }, { "USR2", SIGUSR2 },
    { "PIPE", SIGPIPE }, { "ALRM", SIGALRM }, { "TERM", SIGTERM },
    { "CHLD", SIGCHLD }, { "CONT", SIGCONT }, { "STOP", SIGSTOP },
    { "TSTP", SIGTSTP },
    { NULL, 0 }
};

static int parse_signal(const char *s)
{
    /* Numeric? */
    char *end;
    long n = strtol(s, &end, 10);
    if (*end == '\0' && n >= 0 && n < NSIG)
        return (int)n;

    /* Named */
    for (int i = 0; sigs[i].name; i++) {
        if (strcasecmp(s, sigs[i].name) == 0)
            return sigs[i].num;
    }
    return -1;
}

int main(int argc, char **argv)
{
    int sig = SIGTERM;
    int i = 1;

    if (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        sig = parse_signal(argv[i] + 1);
        if (sig < 0) {
            fprintf(stderr, "kill: unknown signal '%s'\n", argv[i] + 1);
            return 1;
        }
        i++;
    }

    if (i >= argc) {
        fprintf(stderr, "Usage: kill [-signal] pid ...\n");
        return 1;
    }

    int ret = 0;
    for (; i < argc; i++) {
        int pid = atoi(argv[i]);
        if (kill(pid, sig) < 0) {
            fprintf(stderr, "kill: cannot signal %d\n", pid);
            ret = 1;
        }
    }
    return ret;
}
