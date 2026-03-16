/*
 * Minimal stdio for Genix user programs.
 * Provides FILE*, fopen/fclose/fgets/fileno, puts, printf.
 */

extern int write(int fd, const void *buf, int count);
extern int read(int fd, void *buf, int count);
extern int open(const char *path, int flags);
extern int close(int fd);
extern unsigned int strlen(const char *s);

/* Open flags (must match kernel/kernel.h) */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200

/* FILE structure */
typedef struct _FILE {
    int fd;
    int flags;  /* 1=open, 2=error, 4=eof */
} FILE;

#define _FILE_OPEN  1
#define _FILE_ERROR 2
#define _FILE_EOF   4

/* Static file table — enough for levee */
#define FOPEN_MAX 16
static FILE _files[FOPEN_MAX];

/* Standard streams */
static FILE _stdin_f  = { 0, _FILE_OPEN };
static FILE _stdout_f = { 1, _FILE_OPEN };
static FILE _stderr_f = { 2, _FILE_OPEN };

FILE *stdin  = &_stdin_f;
FILE *stdout = &_stdout_f;
FILE *stderr = &_stderr_f;

int fileno(FILE *f)
{
    return f ? f->fd : -1;
}

FILE *fopen(const char *path, const char *mode)
{
    int flags;
    int i;
    FILE *f;

    if (mode[0] == 'r')
        flags = O_RDONLY;
    else if (mode[0] == 'w')
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a')
        flags = O_WRONLY | O_CREAT;
    else
        return (FILE *)0;

    /* Find a free FILE slot */
    f = (FILE *)0;
    for (i = 0; i < FOPEN_MAX; i++) {
        if (!(_files[i].flags & _FILE_OPEN)) {
            f = &_files[i];
            break;
        }
    }
    if (!f)
        return (FILE *)0;

    int fd = open(path, flags);
    if (fd < 0)
        return (FILE *)0;

    f->fd = fd;
    f->flags = _FILE_OPEN;
    return f;
}

int fclose(FILE *f)
{
    int rc;
    if (!f || !(f->flags & _FILE_OPEN))
        return -1;
    rc = close(f->fd);
    f->flags = 0;
    return rc;
}

char *fgets(char *s, int size, FILE *f)
{
    int i = 0;
    unsigned char c;

    if (!f || !(f->flags & _FILE_OPEN) || size <= 0)
        return (char *)0;

    while (i < size - 1) {
        int n = read(f->fd, &c, 1);
        if (n <= 0) {
            if (i == 0) return (char *)0;
            break;
        }
        s[i++] = c;
        if (c == '\n')
            break;
    }
    s[i] = '\0';
    return s;
}

int feof(FILE *f)
{
    return f ? (f->flags & _FILE_EOF) : 1;
}

int ferror(FILE *f)
{
    return f ? (f->flags & _FILE_ERROR) : 1;
}

int fflush(FILE *f)
{
    (void)f;
    return 0;  /* No buffering — writes go directly */
}

int fputc(int c, FILE *f)
{
    unsigned char ch = (unsigned char)c;
    if (!f) return -1;
    if (write(f->fd, &ch, 1) != 1)
        return -1;
    return c;
}

int fgetc(FILE *f)
{
    unsigned char c;
    if (!f) return -1;
    int n = read(f->fd, &c, 1);
    if (n <= 0) {
        f->flags |= _FILE_EOF;
        return -1;
    }
    return c;
}

int puts(const char *s)
{
    int n = strlen(s);
    write(1, s, n);
    write(1, "\n", 1);
    return n + 1;
}

/*
 * Direct-write printf engine. Writes directly to fd via write() — no
 * buffer limit, no vsnprintf dependency.
 *
 * Supports: %s, %d, %u, %x, %c, %%, plus 'l' modifier (no-op on
 * 68000 where int == long == 32 bits).
 *
 * Varargs via stack-casting: on 68000, all args are 32-bit words
 * sequential on the stack after the last named parameter.
 */
static int do_printf(int fd, const char *fmt, const char **args)
{
    int arg_idx = 0;
    int total = 0;
    const char *p = fmt;

    while (*p) {
        if (*p == '%') {
            p++;
            /* Skip 'l' modifier — int and long are both 32 bits on 68000 */
            if (*p == 'l') p++;
            switch (*p) {
            case 's': {
                const char *s = args[arg_idx++];
                if (!s) s = "(null)";
                int n = strlen(s);
                write(fd, s, n);
                total += n;
                break;
            }
            case 'd': {
                long val = (long)args[arg_idx++];
                char buf[12];
                char out[12];
                int neg = 0, i = 0, j;
                if (val < 0) { neg = 1; val = -val; }
                if (val == 0) buf[i++] = '0';
                else while (val > 0) {
                    /* DIVU.W safe: divisor=10 fits in 16 bits */
                    buf[i++] = '0' + (val % 10);
                    val /= 10;
                }
                if (neg) buf[i++] = '-';
                for (j = 0; j < i; j++) out[j] = buf[i-1-j];
                write(fd, out, i);
                total += i;
                break;
            }
            case 'u': {
                unsigned long val = (unsigned long)args[arg_idx++];
                char buf[12];
                char out[12];
                int i = 0, j;
                if (val == 0) buf[i++] = '0';
                else while (val > 0) {
                    buf[i++] = '0' + (val % 10);
                    val /= 10;
                }
                for (j = 0; j < i; j++) out[j] = buf[i-1-j];
                write(fd, out, i);
                total += i;
                break;
            }
            case 'x': {
                unsigned long val = (unsigned long)args[arg_idx++];
                char buf[10];
                char out[10];
                int i = 0, j;
                if (val == 0) buf[i++] = '0';
                else while (val > 0) {
                    int d = val & 0xF;
                    buf[i++] = d < 10 ? '0' + d : 'a' + d - 10;
                    val >>= 4;
                }
                for (j = 0; j < i; j++) out[j] = buf[i-1-j];
                write(fd, out, i);
                total += i;
                break;
            }
            case 'c': {
                char c = (char)(long)args[arg_idx++];
                write(fd, &c, 1);
                total++;
                break;
            }
            case '%':
                write(fd, "%", 1);
                total++;
                break;
            default:
                write(fd, "%", 1);
                write(fd, p, 1);
                total += 2;
                break;
            }
        } else {
            const char *start = p;
            while (*p && *p != '%') p++;
            int n = p - start;
            write(fd, start, n);
            total += n;
            continue;
        }
        p++;
    }
    return total;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    int fd = f ? f->fd : 1;
    return do_printf(fd, fmt, (const char **)(&fmt + 1));
}

int printf(const char *fmt, ...)
{
    return do_printf(1, fmt, (const char **)(&fmt + 1));
}

int fputs(const char *s, FILE *f)
{
    int n;
    if (!f || !s) return -1;
    n = strlen(s);
    return write(f->fd, s, n) == n ? n : -1;
}

unsigned int fread(void *ptr, unsigned int size, unsigned int nmemb, FILE *f)
{
    unsigned int total = size * nmemb;
    int n;
    if (!f || total == 0) return 0;
    n = read(f->fd, ptr, total);
    if (n <= 0) {
        f->flags |= _FILE_EOF;
        return 0;
    }
    /* DIVU.W safe: size is typically small (1-1024) */
    return (unsigned int)n / size;
}

unsigned int fwrite(const void *ptr, unsigned int size, unsigned int nmemb, FILE *f)
{
    unsigned int total = size * nmemb;
    int n;
    if (!f || total == 0) return 0;
    n = write(f->fd, ptr, total);
    if (n <= 0) return 0;
    /* DIVU.W safe: size is typically small (1-1024) */
    return (unsigned int)n / size;
}

int ungetc(int c, FILE *f)
{
    (void)f; (void)c;
    return -1;  /* Not implemented — most apps don't need it */
}
