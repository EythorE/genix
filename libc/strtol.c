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
    int any = 0;

    /* Use 32-bit accumulation to avoid __muldi3 (libgcc's version
     * contains 68020 MULU.L instructions that crash on the 68000).
     * Track high and low 32-bit halves manually. */
    unsigned long lo = 0, hi = 0;

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
        /* Multiply hi:lo by base using 32-bit ops only.
         * base is at most 36, so lo * base fits in 64 bits
         * computed as two 32×32→32 products. */
        hi = hi * (unsigned long)base;
        {
            /* Split lo * base into high and low parts without
             * 64-bit multiply.  lo_hi * base may carry into hi. */
            unsigned long lo_lo = (lo & 0xFFFF) * (unsigned long)base;
            unsigned long lo_hi = (lo >> 16) * (unsigned long)base;
            unsigned long mid = lo_hi + (lo_lo >> 16);
            lo = (lo_lo & 0xFFFF) | (mid << 16);
            hi += mid >> 16;
        }
        lo += (unsigned long)d;
        if (lo < (unsigned long)d)
            hi++;
        any = 1;
        s++;
    }

    if (endptr)
        *endptr = (char *)(any ? s : nptr);
    return ((unsigned long long)hi << 32) | lo;
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
