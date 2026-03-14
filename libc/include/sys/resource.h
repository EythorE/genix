/* Genix sys/resource.h — stub for programs using getrlimit/setrlimit */
#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#include <errno.h>

typedef unsigned long rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

#define RLIM_INFINITY (-1L)
#define RLIMIT_CPU     0
#define RLIMIT_FSIZE   1
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_CORE    4
#define RLIMIT_RSS     5
#define RLIMIT_MEMLOCK 6
#define RLIMIT_NPROC   7
#define RLIMIT_NOFILE  8
#define RLIMIT_AS      9

static inline int getrlimit(int resource, struct rlimit *rlim)
{
    (void)resource;
    rlim->rlim_cur = RLIM_INFINITY;
    rlim->rlim_max = RLIM_INFINITY;
    return 0;
}

static inline int setrlimit(int resource, const struct rlimit *rlim)
{
    (void)resource;
    (void)rlim;
    errno = EPERM;
    return -1;
}

#endif
