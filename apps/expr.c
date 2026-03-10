/*
 * expr — evaluate expressions
 *
 * Usage: expr arg1 op arg2
 * Supports: +, -, *, /, %, =, !=, <, >, <=, >=, :
 * Returns via exit code: 0 if result is non-zero/non-null, 1 otherwise.
 */
int write(int fd, const void *buf, int count);

static int str_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int is_number(const char *s)
{
    if (*s == '-') s++;
    if (!*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        s++;
    }
    return 1;
}

static long to_num(const char *s)
{
    int neg = 0;
    long n = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        /* DIVU.W safe: constant 10 */
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

static void print_num(long n)
{
    char buf[16];
    int i = 0, neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) buf[i++] = '0';
    while (n > 0) {
        buf[i++] = '0' + (int)(n % 10);
        n /= 10;
    }
    if (neg) buf[i++] = '-';
    /* Reverse */
    char out[16];
    int j;
    for (j = 0; j < i; j++) out[j] = buf[i - 1 - j];
    out[i] = '\n';
    write(1, out, i + 1);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        static const char msg[] = "Usage: expr arg1 op arg2\n";
        write(2, msg, sizeof(msg) - 1);
        return 2;
    }

    const char *left = argv[1];
    const char *op = argv[2];
    const char *right = argv[3];

    /* Arithmetic operations */
    if (is_number(left) && is_number(right)) {
        long a = to_num(left);
        long b = to_num(right);
        long result = 0;

        if (str_eq(op, "+")) result = a + b;
        else if (str_eq(op, "-")) result = a - b;
        else if (str_eq(op, "*")) result = a * b;
        else if (str_eq(op, "/")) {
            if (b == 0) {
                static const char msg[] = "expr: division by zero\n";
                write(2, msg, sizeof(msg) - 1);
                return 2;
            }
            result = a / b;
        }
        else if (str_eq(op, "%")) {
            if (b == 0) {
                static const char msg[] = "expr: division by zero\n";
                write(2, msg, sizeof(msg) - 1);
                return 2;
            }
            result = a % b;
        }
        else if (str_eq(op, "="))  result = (a == b);
        else if (str_eq(op, "!=")) result = (a != b);
        else if (str_eq(op, "<"))  result = (a < b);
        else if (str_eq(op, ">"))  result = (a > b);
        else if (str_eq(op, "<=")) result = (a <= b);
        else if (str_eq(op, ">=")) result = (a >= b);
        else {
            static const char msg[] = "expr: unknown operator\n";
            write(2, msg, sizeof(msg) - 1);
            return 2;
        }

        print_num(result);
        return (result == 0) ? 1 : 0;
    }

    /* String comparison */
    if (str_eq(op, "=")) {
        int eq = str_eq(left, right);
        print_num(eq);
        return eq ? 0 : 1;
    }
    if (str_eq(op, "!=")) {
        int neq = !str_eq(left, right);
        print_num(neq);
        return neq ? 0 : 1;
    }

    static const char msg[] = "expr: unknown operator\n";
    write(2, msg, sizeof(msg) - 1);
    return 2;
}
