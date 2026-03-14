/* Genix sys/types.h — POSIX type definitions */
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stdint.h>

typedef int          pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long         off_t;
typedef unsigned int mode_t;

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
#endif

typedef long ssize_t;

typedef int dev_t;
typedef unsigned int ino_t;
typedef unsigned int nlink_t;

#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
typedef long time_t;
#endif

#endif
