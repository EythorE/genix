/* Genix sys/times.h — stub for programs using times() */
#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H

struct tms {
    long tms_utime;
    long tms_stime;
    long tms_cutime;
    long tms_cstime;
};

static inline long times(struct tms *buf)
{
    buf->tms_utime = 0;
    buf->tms_stime = 0;
    buf->tms_cutime = 0;
    buf->tms_cstime = 0;
    return 0;
}

#endif
