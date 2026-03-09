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
