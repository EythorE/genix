/* Genix minimal stdlib.h */
#ifndef _STDLIB_H
#define _STDLIB_H

#ifndef NULL
#define NULL ((void *)0)
#endif

void   exit(int code);
int    abs(int n);
int    atoi(const char *s);
long   atol(const char *s);
long   strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
void   abort(void) __attribute__((noreturn));
char  *getenv(const char *name);
int    setenv(const char *name, const char *value, int overwrite);
int    unsetenv(const char *name);
void  *malloc(unsigned int size);
void   free(void *ptr);
void  *calloc(unsigned int nmemb, unsigned int size);
void  *realloc(void *ptr, unsigned int size);
void   qsort(void *base, unsigned int nmemb, unsigned int size,
              int (*compar)(const void *, const void *));
void  *bsearch(const void *key, const void *base, unsigned int nmemb,
               unsigned int size, int (*compar)(const void *, const void *));
int    rand(void);
void   srand(unsigned int seed);

#define RAND_MAX 32767

#endif
