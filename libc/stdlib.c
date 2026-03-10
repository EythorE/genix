/*
 * Minimal stdlib for Genix user programs.
 * Provides malloc/free (via sbrk), atoi, getenv, strdup, abs, exit.
 */

extern void _exit(int code);
extern void *sbrk(int incr);
extern unsigned int strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern void *memcpy(void *dest, const void *src, unsigned int n);
/* Forward declarations (defined later in this file) */
void *malloc(unsigned int size);
void  free(void *ptr);

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

/* ======== Environment variables ======== */

/* Simple environment: array of "NAME=VALUE" strings, NULL terminated.
 * Max 32 entries. Stored in a static buffer since we have no fork(). */
#define ENV_MAX 32
static char *env_array[ENV_MAX + 1];  /* NULL-terminated */
static int env_count = 0;

extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, unsigned int n);

char **environ = env_array;

static int env_find(const char *name)
{
    unsigned int nlen = strlen(name);
    int i;
    for (i = 0; i < env_count; i++) {
        if (env_array[i] &&
            strncmp(env_array[i], name, nlen) == 0 &&
            env_array[i][nlen] == '=')
            return i;
    }
    return -1;
}

char *getenv(const char *name)
{
    int i = env_find(name);
    if (i < 0) return (char *)0;
    /* Return pointer past the '=' */
    return env_array[i] + strlen(name) + 1;
}

int setenv(const char *name, const char *value, int overwrite)
{
    int i;
    unsigned int nlen, vlen;
    char *entry;

    if (!name || !*name) return -1;

    i = env_find(name);
    if (i >= 0 && !overwrite) return 0;

    nlen = strlen(name);
    vlen = strlen(value);
    entry = (char *)malloc(nlen + 1 + vlen + 1);
    if (!entry) return -1;
    memcpy(entry, name, nlen);
    entry[nlen] = '=';
    memcpy(entry + nlen + 1, value, vlen);
    entry[nlen + 1 + vlen] = '\0';

    if (i >= 0) {
        free(env_array[i]);
        env_array[i] = entry;
    } else {
        if (env_count >= ENV_MAX) { free(entry); return -1; }
        env_array[env_count++] = entry;
        env_array[env_count] = (char *)0;
    }
    return 0;
}

int unsetenv(const char *name)
{
    int i = env_find(name);
    if (i < 0) return 0;
    free(env_array[i]);
    /* Shift remaining entries down */
    env_count--;
    while (i < env_count) {
        env_array[i] = env_array[i + 1];
        i++;
    }
    env_array[env_count] = (char *)0;
    return 0;
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

/* ======== qsort (shell sort — simple, no recursion, low stack) ======== */

static void swap_bytes(char *a, char *b, unsigned int size)
{
    unsigned int i;
    for (i = 0; i < size; i++) {
        char tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

void qsort(void *base, unsigned int nmemb, unsigned int size,
            int (*compar)(const void *, const void *))
{
    /* Shell sort: O(n^1.5) worst case, no recursion, constant stack.
     * Preferred over quicksort on 68000 because recursion is expensive
     * (18 cycles per JSR + register saves) and stack space is limited. */
    unsigned int gap, i, j;
    char *arr = (char *)base;

    if (nmemb <= 1) return;

    for (gap = nmemb >> 1; gap > 0; gap >>= 1) {
        for (i = gap; i < nmemb; i++) {
            for (j = i; j >= gap; j -= gap) {
                char *a = arr + (j - gap) * size;
                char *b = arr + j * size;
                if (compar(a, b) <= 0) break;
                swap_bytes(a, b, size);
            }
        }
    }
}

void *bsearch(const void *key, const void *base, unsigned int nmemb,
              unsigned int size, int (*compar)(const void *, const void *))
{
    unsigned int lo = 0, hi = nmemb;
    const char *arr = (const char *)base;

    while (lo < hi) {
        /* Shift safe: no division needed */
        unsigned int mid = lo + ((hi - lo) >> 1);
        int cmp = compar(key, arr + mid * size);
        if (cmp == 0) return (void *)(arr + mid * size);
        if (cmp < 0) hi = mid;
        else lo = mid + 1;
    }
    return (void *)0;
}

/* ======== Random number generator (LCG) ======== */

static unsigned int _rand_seed = 1;

void srand(unsigned int seed)
{
    _rand_seed = seed;
}

int rand(void)
{
    /* Linear congruential generator.
     * Constants from Numerical Recipes. Fits in 32-bit arithmetic.
     * No division needed — just multiply and add. */
    _rand_seed = _rand_seed * 1103515245 + 12345;
    return (_rand_seed >> 16) & 0x7FFF;
}
