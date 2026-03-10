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

char *strstr(const char *haystack, const char *needle)
{
    unsigned int nlen;
    if (!*needle) return (char *)haystack;
    nlen = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0)
            return (char *)haystack;
        haystack++;
    }
    return (char *)0;
}

int strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && *s2) {
        int c1 = *s1, c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncasecmp(const char *s1, const char *s2, unsigned int n)
{
    while (n-- && *s1 && *s2) {
        int c1 = *s1, c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    if (n == (unsigned int)-1) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

unsigned int strcspn(const char *s, const char *reject)
{
    const char *p = s;
    while (*p) {
        const char *r = reject;
        while (*r) {
            if (*p == *r) return p - s;
            r++;
        }
        p++;
    }
    return p - s;
}

unsigned int strspn(const char *s, const char *accept)
{
    const char *p = s;
    while (*p) {
        const char *a = accept;
        int found = 0;
        while (*a) {
            if (*p == *a) { found = 1; break; }
            a++;
        }
        if (!found) return p - s;
        p++;
    }
    return p - s;
}
