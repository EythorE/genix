/* Genix minimal string.h */
#ifndef _STRING_H
#define _STRING_H

#ifndef NULL
#define NULL ((void *)0)
#endif

void        *memset(void *s, int c, unsigned int n);
void        *memcpy(void *dest, const void *src, unsigned int n);
int          memcmp(const void *s1, const void *s2, unsigned int n);
void        *memmove(void *dest, const void *src, unsigned int n);
unsigned int strlen(const char *s);
int          strcmp(const char *s1, const char *s2);
int          strncmp(const char *s1, const char *s2, unsigned int n);
char        *strcpy(char *dest, const char *src);
char        *strncpy(char *dest, const char *src, unsigned int n);
char        *strcat(char *dest, const char *src);
char        *strchr(const char *s, int c);
char        *strrchr(const char *s, int c);
char        *strdup(const char *s);
char        *strtok(char *s, const char *delim);

#endif
