/* Genix time.h — time types (stubs) */
#ifndef _TIME_H
#define _TIME_H

#include <stdint.h>

typedef long time_t;
typedef long clock_t;

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct timespec {
    long tv_sec;
    long tv_nsec;
};

time_t time(time_t *t);

#endif
