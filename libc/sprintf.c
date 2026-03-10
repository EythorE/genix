/*
 * sprintf / snprintf — format strings into a buffer for Genix.
 *
 * Supports: %s, %d, %u, %x, %c, %%, %ld, %lu, %lx
 * No width/precision/padding — add if needed.
 */

extern unsigned int strlen(const char *s);

/* Emit a single character to the buffer, respecting the limit. */
static int emit_char(char *buf, int pos, int limit, char c)
{
    if (pos < limit - 1)
        buf[pos] = c;
    return pos + 1;
}

/* Emit a string to the buffer. */
static int emit_str(char *buf, int pos, int limit, const char *s)
{
    while (*s)
        pos = emit_char(buf, pos, limit, *s++);
    return pos;
}

/* Emit an unsigned integer in a given base. */
static int emit_uint(char *buf, int pos, int limit, unsigned long val, int base)
{
    char tmp[12];
    int len = 0;

    if (val == 0) {
        return emit_char(buf, pos, limit, '0');
    }

    while (val > 0) {
        /* DIVU.W safe for base <= 16 */
        int d = val % (unsigned long)base;
        tmp[len++] = d < 10 ? '0' + d : 'a' + d - 10;
        val /= (unsigned long)base;
    }

    for (int i = len - 1; i >= 0; i--)
        pos = emit_char(buf, pos, limit, tmp[i]);
    return pos;
}

static int do_vsnprintf(char *buf, int size, const char *fmt, const char **args)
{
    int pos = 0;
    int arg_idx = 0;
    const char *p = fmt;

    while (*p) {
        if (*p != '%') {
            const char *start = p;
            while (*p && *p != '%') p++;
            while (start < p)
                pos = emit_char(buf, pos, size, *start++);
            continue;
        }
        p++;  /* skip '%' */

        /* Check for 'l' modifier */
        int is_long = 0;
        if (*p == 'l') {
            is_long = 1;
            p++;
            (void)is_long; /* same size on 68000 (32-bit int/long) */
        }

        switch (*p) {
        case 's': {
            const char *s = args[arg_idx++];
            if (!s) s = "(null)";
            pos = emit_str(buf, pos, size, s);
            break;
        }
        case 'd': {
            long val = (long)args[arg_idx++];
            if (val < 0) {
                pos = emit_char(buf, pos, size, '-');
                val = -val;
            }
            pos = emit_uint(buf, pos, size, (unsigned long)val, 10);
            break;
        }
        case 'u': {
            unsigned long val = (unsigned long)args[arg_idx++];
            pos = emit_uint(buf, pos, size, val, 10);
            break;
        }
        case 'x': {
            unsigned long val = (unsigned long)args[arg_idx++];
            pos = emit_uint(buf, pos, size, val, 16);
            break;
        }
        case 'c': {
            char c = (char)(long)args[arg_idx++];
            pos = emit_char(buf, pos, size, c);
            break;
        }
        case '%':
            pos = emit_char(buf, pos, size, '%');
            break;
        default:
            pos = emit_char(buf, pos, size, '%');
            pos = emit_char(buf, pos, size, *p);
            break;
        }
        p++;
    }

    /* Null-terminate */
    if (size > 0)
        buf[pos < size ? pos : size - 1] = '\0';
    return pos;
}

int snprintf(char *buf, unsigned int size, const char *fmt, ...)
{
    const char **args = (const char **)(&fmt + 1);
    return do_vsnprintf(buf, (int)size, fmt, args);
}

int sprintf(char *buf, const char *fmt, ...)
{
    const char **args = (const char **)(&fmt + 1);
    /* Use a large limit — caller must ensure buf is big enough */
    return do_vsnprintf(buf, 0x7FFFFFFF, fmt, args);
}

/* ======== sscanf — formatted input parsing ======== */

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int sscanf(const char *str, const char *fmt, ...)
{
    void **args = (void **)(&fmt + 1);
    int arg_idx = 0;
    int matched = 0;
    const char *p = fmt;
    const char *s = str;

    while (*p && *s) {
        /* Whitespace in format matches any amount of whitespace in input */
        if (is_space(*p)) {
            while (is_space(*s)) s++;
            while (is_space(*p)) p++;
            continue;
        }

        /* Literal match */
        if (*p != '%') {
            if (*s != *p) break;
            s++; p++;
            continue;
        }

        p++;  /* skip '%' */

        /* Check for 'l' modifier */
        int is_long = 0;
        if (*p == 'l') {
            is_long = 1;
            p++;
            (void)is_long; /* same size on 68000 */
        }

        /* %n — store characters consumed so far */
        if (*p == 'n') {
            int *np = (int *)args[arg_idx++];
            *np = (int)(s - str);
            p++;
            continue;  /* %n doesn't count as a match */
        }

        /* %% — literal percent */
        if (*p == '%') {
            if (*s != '%') break;
            s++; p++;
            continue;
        }

        switch (*p) {
        case 'd': {
            /* Skip leading whitespace */
            while (is_space(*s)) s++;
            int neg = 0;
            if (*s == '-') { neg = 1; s++; }
            else if (*s == '+') s++;
            if (*s < '0' || *s > '9') goto done;
            long val = 0;
            while (*s >= '0' && *s <= '9') {
                /* DIVU.W safe: constant 10 */
                val = val * 10 + (*s - '0');
                s++;
            }
            if (neg) val = -val;
            *(int *)args[arg_idx++] = (int)val;
            matched++;
            break;
        }
        case 'u': {
            while (is_space(*s)) s++;
            if (*s < '0' || *s > '9') goto done;
            unsigned long val = 0;
            while (*s >= '0' && *s <= '9') {
                val = val * 10 + (*s - '0');
                s++;
            }
            *(unsigned int *)args[arg_idx++] = (unsigned int)val;
            matched++;
            break;
        }
        case 'x': {
            while (is_space(*s)) s++;
            /* Skip optional 0x prefix */
            if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
            if (!((*s >= '0' && *s <= '9') ||
                  (*s >= 'a' && *s <= 'f') ||
                  (*s >= 'A' && *s <= 'F'))) goto done;
            unsigned long val = 0;
            while (1) {
                if (*s >= '0' && *s <= '9')
                    val = (val << 4) | (*s - '0');
                else if (*s >= 'a' && *s <= 'f')
                    val = (val << 4) | (*s - 'a' + 10);
                else if (*s >= 'A' && *s <= 'F')
                    val = (val << 4) | (*s - 'A' + 10);
                else break;
                s++;
            }
            *(unsigned int *)args[arg_idx++] = (unsigned int)val;
            matched++;
            break;
        }
        case 's': {
            while (is_space(*s)) s++;
            char *dest = (char *)args[arg_idx++];
            while (*s && !is_space(*s))
                *dest++ = *s++;
            *dest = '\0';
            matched++;
            break;
        }
        case 'c': {
            *(char *)args[arg_idx++] = *s++;
            matched++;
            break;
        }
        default:
            goto done;
        }
        p++;
    }

done:
    return matched;
}
