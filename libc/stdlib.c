/*
 * Minimal stdlib for Genix user programs.
 * Provides malloc/free (via sbrk), atoi, getenv, strdup, abs, exit.
 */

extern void _exit(int code);
extern void *sbrk(int incr);
extern unsigned int strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern void *memcpy(void *dest, const void *src, unsigned int n);

void exit(int code)
{
    _exit(code);
}

int abs(int n)
{
    return n < 0 ? -n : n;
}

int atoi(const char *s)
{
    int n = 0;
    int neg = 0;

    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    while (*s >= '0' && *s <= '9') {
        /* DIVU.W safe: divisor=10 fits in 16 bits */
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

long atol(const char *s)
{
    return (long)atoi(s);
}

/* getenv: always returns NULL (no environment on Genix yet) */
char *getenv(const char *name)
{
    (void)name;
    return (char *)0;
}

/* ======== Simple malloc/free using sbrk ======== */

/* Block header: size includes header. Next pointer for free list. */
struct mhdr {
    unsigned int size;    /* total block size including header */
    struct mhdr *next;    /* next free block (only valid when free) */
};

/* Alignment: 4 bytes (even address required on 68000) */
#define ALIGN4(x) (((x) + 3) & ~3)
#define MHDR_SIZE ALIGN4(sizeof(struct mhdr))

static struct mhdr *freelist = (void *)0;
void *malloc(unsigned int size)
{
    struct mhdr *p, *prev;
    unsigned int total;

    if (size == 0)
        return (void *)0;

    total = ALIGN4(size + MHDR_SIZE);

    /* Search free list for a big enough block */
    prev = (void *)0;
    for (p = freelist; p; prev = p, p = p->next) {
        if (p->size >= total) {
            /* Found a fit — unlink from free list */
            if (prev)
                prev->next = p->next;
            else
                freelist = p->next;
            return (char *)p + MHDR_SIZE;
        }
    }

    /* No free block found — grow heap via sbrk */
    if (total < 256)
        total = 256;  /* Minimum allocation to reduce sbrk calls */

    p = (struct mhdr *)sbrk(total);
    if ((int)(unsigned int)p < 0 || p == (void *)-1)
        return (void *)0;

    p->size = total;
    return (char *)p + MHDR_SIZE;
}

void free(void *ptr)
{
    struct mhdr *p;

    if (!ptr)
        return;

    p = (struct mhdr *)((char *)ptr - MHDR_SIZE);
    p->next = freelist;
    freelist = p;
}

void *calloc(unsigned int nmemb, unsigned int size)
{
    unsigned int total = nmemb * size;
    void *p = malloc(total);
    if (p) {
        char *cp = (char *)p;
        unsigned int i;
        for (i = 0; i < total; i++)
            cp[i] = 0;
    }
    return p;
}

void *realloc(void *ptr, unsigned int size)
{
    struct mhdr *old;
    void *newp;
    unsigned int oldsize;

    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return (void *)0;
    }

    old = (struct mhdr *)((char *)ptr - MHDR_SIZE);
    oldsize = old->size - MHDR_SIZE;
    if (oldsize >= size)
        return ptr;  /* Already big enough */

    newp = malloc(size);
    if (newp) {
        memcpy(newp, ptr, oldsize);
        free(ptr);
    }
    return newp;
}

char *strdup(const char *s)
{
    unsigned int len = strlen(s) + 1;
    char *p = (char *)malloc(len);
    if (p)
        strcpy(p, s);
    return p;
}
