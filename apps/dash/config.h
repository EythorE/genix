/* config.h — Genix dash configuration.
 * Minimal feature set for a no-MMU, single-user, 68000 system. */

#ifndef DASH_CONFIG_H
#define DASH_CONFIG_H

/* Include Genix compatibility shims */
#include "genix_compat.h"

/* Package info */
#define PACKAGE "dash"
#define PACKAGE_VERSION "0.5.12"
#define VERSION "0.5.12"
#define PACKAGE_STRING "dash 0.5.12"
#define PACKAGE_NAME "dash"
#define PACKAGE_TARNAME "dash"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_URL ""

/* Build options */
#define SMALL 1
/* #undef JOBS */
/* #undef WITH_LINENO */

/* Functions available in Genix libc */
#define HAVE_BSEARCH 1
#define HAVE_ISALPHA 1
#define HAVE_SYSCONF 1
#define HAVE_STRTOD 1

/* Functions provided by genix_compat.h or dash's system.c */
#define HAVE_STRTOIMAX 1
#define HAVE_STRTOUMAX 1
/* mempcpy, stpcpy, strchrnul, strsignal provided by dash's system.c */
/* #undef HAVE_MEMPCPY */
/* #undef HAVE_STPCPY */
/* #undef HAVE_STRCHRNUL */
#define HAVE_STRSIGNAL 1
/* killpg provided by system.h inline when HAVE_KILLPG not defined */
/* #undef HAVE_KILLPG */

/* Functions NOT available */
/* #undef HAVE_FNMATCH */
/* #undef HAVE_GLOB */
/* #undef HAVE_GETPWNAM */
#define HAVE_GETRLIMIT 1
/* #undef HAVE_FACCESSAT */
/* #undef HAVE_SIGSETMASK */
/* #undef HAVE_ALLOCA_H */
/* #undef HAVE_ALIAS_ATTRIBUTE */

/* Headers */
#define HAVE_PATHS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
/* #undef HAVE_STRINGS_H */
/* #undef HAVE_INTTYPES_H */
/* #undef HAVE_MEMORY_H */
#define STDC_HEADERS 1

/* isblank is available via ctype.h */
#define HAVE_DECL_ISBLANK 1

/* stat does not have st_mtim on Genix */
/* #undef HAVE_ST_MTIM */

/* 68000: int is 32-bit, long long is 64-bit, intmax_t = long long */
#define SIZEOF_INTMAX_T 8
#define SIZEOF_LONG_LONG_INT 8

/* 64-bit operations are the same as 32-bit on this platform */
#define fstat64 fstat
#define lstat64 lstat
#define open64 open
#define stat64 stat
#define dirent64 dirent
#define readdir64 readdir

/* Shell path definitions — use paths.h defaults */
/* #undef _PATH_BSHELL */
/* #undef _PATH_DEVNULL */
/* #undef _PATH_TTY */

#endif /* DASH_CONFIG_H */
