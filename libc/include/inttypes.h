/* Genix inttypes.h — format macros for fixed-width integer types */
#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

/* intmax_t is long long on m68k-elf-gcc */
#define PRIdMAX "lld"
#define PRIoMAX "llo"
#define PRIuMAX "llu"
#define PRIxMAX "llx"

#endif
