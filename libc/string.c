/*
 * Minimal string functions for user programs.
 * These are the same as the kernel versions but compiled for userspace.
 */

#include <stddef.h>

void *memset(void *s, int c, unsigned int n)
{
    unsigned char *p = s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, unsigned int n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dest;
}

unsigned int strlen(const char *s)
{
    unsigned int n = 0;
    while (*s++) n++;
    return n;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : (void *)0;
}

char *strrchr(const char *s, int c)
{
    const char *last = (void *)0;
    while (*s) {
        if (*s == (char)c)
            last = s;
        s++;
    }
    if (c == '\0')
        return (char *)s;
    return (char *)last;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strncpy(char *dest, const char *src, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

int strncmp(const char *s1, const char *s2, unsigned int n)
{
    while (n > 0 && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int memcmp(const void *s1, const void *s2, unsigned int n)
{
    const unsigned char *p1 = s1, *p2 = s2;
    while (n-- > 0) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

void *memmove(void *dest, const void *src, unsigned int n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dest;
}

char *strtok(char *s, const char *delim)
{
    static char *saved;
    char *start;

    if (s)
        saved = s;
    if (!saved)
        return (void *)0;

    /* Skip leading delimiters */
    while (*saved) {
        const char *d = delim;
        int is_delim = 0;
        while (*d) {
            if (*saved == *d) { is_delim = 1; break; }
            d++;
        }
        if (!is_delim) break;
        saved++;
    }
    if (*saved == '\0') {
        saved = (void *)0;
        return (void *)0;
    }

    start = saved;
    /* Find end of token */
    while (*saved) {
        const char *d = delim;
        while (*d) {
            if (*saved == *d) {
                *saved++ = '\0';
                return start;
            }
            d++;
        }
        saved++;
    }
    saved = (void *)0;
    return start;
}
