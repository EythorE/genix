/*
 * seq — print a sequence of numbers
 *
 * Usage: seq [first [incr]] last
 */
int write(int fd, const void *buf, int count);

/* Simple integer-to-string, returns length */
static int itoa_buf(char *buf, int n)
{
    char tmp[12];
    int len = 0;
    int neg = 0;

    if (n < 0) {
        neg = 1;
        n = -n;
    }
    if (n == 0) {
        buf[0] = '0';
        return 1;
    }
    while (n > 0) {
        /* DIVU.W safe: divisor=10 fits in 16 bits */
        tmp[len++] = '0' + (n % 10);
        n /= 10;
    }
    if (neg)
        tmp[len++] = '-';
    for (int i = 0; i < len; i++)
        buf[i] = tmp[len - 1 - i];
    return len;
}

static int parse_int(const char *s)
{
    int n = 0;
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

int main(int argc, char **argv)
{
    int first = 1, incr = 1, last;
    char buf[16];

    if (argc == 2) {
        last = parse_int(argv[1]);
    } else if (argc == 3) {
        first = parse_int(argv[1]);
        last = parse_int(argv[2]);
    } else if (argc == 4) {
        first = parse_int(argv[1]);
        incr = parse_int(argv[2]);
        last = parse_int(argv[3]);
    } else {
        static const char msg[] = "usage: seq [first [incr]] last\n";
        write(2, msg, sizeof(msg) - 1);
        return 1;
    }

    if (incr == 0)
        return 1;

    for (int n = first; incr > 0 ? n <= last : n >= last; n += incr) {
        int len = itoa_buf(buf, n);
        buf[len] = '\n';
        write(1, buf, len + 1);
    }
    return 0;
}
