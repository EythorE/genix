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
char  *getenv(const char *name);
void  *malloc(unsigned int size);
void   free(void *ptr);
void  *calloc(unsigned int nmemb, unsigned int size);
void  *realloc(void *ptr, unsigned int size);

#endif
