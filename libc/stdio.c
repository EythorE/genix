/*
 * Minimal stdio for Genix user programs
 */

extern int write(int fd, const void *buf, int count);
extern unsigned int strlen(const char *s);

int puts(const char *s)
{
    int n = strlen(s);
    write(1, s, n);
    write(1, "\n", 1);
    return n + 1;
}

int fputs(const char *s, int fd)
{
    int n = strlen(s);
    return write(fd, s, n);
}

static void print_num(int n)
{
    char buf[12];
    int neg = 0;
    int i = 0;

    if (n < 0) {
        neg = 1;
        n = -n;
    }

    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    if (neg)
        buf[i++] = '-';

    /* Reverse and write */
    char out[12];
    int j;
    for (j = 0; j < i; j++)
        out[j] = buf[i - 1 - j];

    write(1, out, i);
}

/* Minimal printf — supports %s, %d, %c, %% */
int printf(const char *fmt, ...)
{
    /* On 68000, varargs are on the stack after fmt.
     * We access them via pointer arithmetic. */
    const char **args = (const char **)(&fmt + 1);
    int arg_idx = 0;
    int total = 0;
    const char *p = fmt;

    while (*p) {
        if (*p == '%') {
            p++;
            switch (*p) {
            case 's': {
                const char *s = args[arg_idx++];
                if (!s) s = "(null)";
                int n = strlen(s);
                write(1, s, n);
                total += n;
                break;
            }
            case 'd': {
                int val = (int)(long)args[arg_idx++];
                print_num(val);
                total += 1; /* approximate */
                break;
            }
            case 'c': {
                char c = (char)(long)args[arg_idx++];
                write(1, &c, 1);
                total++;
                break;
            }
            case '%':
                write(1, "%", 1);
                total++;
                break;
            default:
                write(1, "%", 1);
                write(1, p, 1);
                total += 2;
                break;
            }
        } else {
            /* Find run of plain text */
            const char *start = p;
            while (*p && *p != '%') p++;
            int n = p - start;
            write(1, start, n);
            total += n;
            continue;
        }
        p++;
    }

    return total;
}
