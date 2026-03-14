/* Genix sys/param.h — minimal shim for POSIX programs */
#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

#include <limits.h>

#ifndef PIPE_BUF
#define PIPE_BUF 512
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN PATH_MAX
#endif

#endif
