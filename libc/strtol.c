/*
 * strtol / strtoul — string to integer conversion for Genix.
 */

static int isspace_local(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int isdigit_local(int c)
{
    return c >= '0' && c <= '9';
}

static int isalpha_local(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int digit_val(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    unsigned long result = 0;
    int any = 0;

    /* Skip whitespace */
    while (isspace_local(*s))
        s++;

    /* Optional '+' */
    if (*s == '+')
        s++;

    /* Determine base */
    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                base = 16;
                s++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            s += 2;
    }

    while (*s) {
        int d = digit_val(*s);
        if (d < 0 || d >= base)
            break;
        result = result * (unsigned long)base + (unsigned long)d;
        any = 1;
        s++;
    }

    if (endptr)
        *endptr = (char *)(any ? s : nptr);
    return result;
}

long strtol(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    int neg = 0;
    long result;

    /* Skip whitespace */
    while (isspace_local(*s))
        s++;

    /* Sign */
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    result = (long)strtoul(s, endptr, base);
    if (neg)
        result = -result;

    /* If no digits were consumed, endptr should point to nptr */
    if (endptr && *endptr == s)
        *endptr = (char *)nptr;

    return result;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    unsigned long long result = 0;
    int any = 0;

    while (isspace_local(*s))
        s++;

    if (*s == '+')
        s++;

    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                base = 16;
                s++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            s += 2;
    }

    while (*s) {
        int d = digit_val(*s);
        if (d < 0 || d >= base)
            break;
        result = result * (unsigned long long)base + (unsigned long long)d;
        any = 1;
        s++;
    }

    if (endptr)
        *endptr = (char *)(any ? s : nptr);
    return result;
}

long long strtoll(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    int neg = 0;
    long long result;

    while (isspace_local(*s))
        s++;

    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    result = (long long)strtoull(s, endptr, base);
    if (neg)
        result = -result;

    if (endptr && *endptr == s)
        *endptr = (char *)nptr;

    return result;
}
