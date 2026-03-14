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

/* ======== vsnprintf — va_list version ======== */

#include <stdarg.h>

int vsnprintf(char *buf, unsigned int size, const char *fmt, va_list ap)
{
    int pos = 0;
    int limit = (int)size;
    const char *p = fmt;

    while (*p) {
        if (*p != '%') {
            pos = emit_char(buf, pos, limit, *p++);
            continue;
        }
        p++;  /* skip '%' */

        /* Flags and width (minimal support) */
        int pad_zero = 0;
        int left_align = 0;
        int width = 0;
        int precision = -1;

        if (*p == '-') { left_align = 1; p++; }
        if (*p == '0') { pad_zero = 1; p++; }
        while (*p >= '0' && *p <= '9')
            width = width * 10 + (*p++ - '0');
        if (*p == '.') {
            p++;
            precision = 0;
            if (*p == '*') {
                precision = va_arg(ap, int);
                p++;
            } else {
                while (*p >= '0' && *p <= '9')
                    precision = precision * 10 + (*p++ - '0');
            }
        }
        if (*p == '*') {
            width = va_arg(ap, int);
            p++;
        }

        /* Length modifier */
        int is_long = 0;
        int is_longlong = 0;
        if (*p == 'l') {
            is_long = 1;
            p++;
            if (*p == 'l') { is_longlong = 1; p++; }
        }
        if (*p == 'j') { is_longlong = 1; p++; }
        if (*p == 'z') { is_long = 1; p++; }

        switch (*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int slen = 0;
            const char *t = s;
            while (*t++) slen++;
            if (precision >= 0 && slen > precision)
                slen = precision;
            int pad = width > slen ? width - slen : 0;
            if (!left_align)
                while (pad-- > 0) pos = emit_char(buf, pos, limit, ' ');
            for (int i = 0; i < slen; i++)
                pos = emit_char(buf, pos, limit, s[i]);
            if (left_align)
                while (pad-- > 0) pos = emit_char(buf, pos, limit, ' ');
            break;
        }
        case 'd': case 'i': {
            /* Use 32-bit arithmetic to avoid __divdi3/__moddi3 from
             * libgcc which contain 68020 MULU.L/DIVU.L instructions.
             * Only widen to 64-bit for explicit %lld/%jd formats. */
            char tmp[22];
            int len = 0;
            int neg = 0;
            if (is_longlong) {
                long long llval = va_arg(ap, long long);
                if (llval < 0) { neg = 1; llval = -llval; }
                /* Convert using 32-bit division: extract low/high halves */
                unsigned long hi = (unsigned long)(llval >> 32);
                unsigned long lo = (unsigned long)llval;
                if (hi == 0) {
                    if (lo == 0) tmp[len++] = '0';
                    else while (lo > 0) { tmp[len++] = '0' + (lo % 10); lo /= 10; }
                } else {
                    /* Full 64-bit: divide hi:lo by 10 using 32-bit ops */
                    while (hi > 0 || lo > 0) {
                        unsigned long r = hi % 10;
                        hi /= 10;
                        /* Combine remainder with lo: (r << 32 | lo) / 10 */
                        unsigned long lo_hi = (r << 16) | (lo >> 16);
                        unsigned long q_hi = lo_hi / 10;
                        unsigned long r2 = lo_hi % 10;
                        unsigned long lo_lo = (r2 << 16) | (lo & 0xFFFF);
                        unsigned long q_lo = lo_lo / 10;
                        tmp[len++] = '0' + (int)(lo_lo % 10);
                        lo = (q_hi << 16) | q_lo;
                    }
                    if (len == 0) tmp[len++] = '0';
                }
            } else {
                long sval = (long)va_arg(ap, int);
                if (sval < 0) { neg = 1; sval = -sval; }
                unsigned long val = (unsigned long)sval;
                if (val == 0) tmp[len++] = '0';
                else while (val > 0) { tmp[len++] = '0' + (val % 10); val /= 10; }
            }
            int total = len + neg;
            int pad = width > total ? width - total : 0;
            if (!left_align && !pad_zero)
                while (pad-- > 0) pos = emit_char(buf, pos, limit, ' ');
            if (neg) pos = emit_char(buf, pos, limit, '-');
            if (!left_align && pad_zero)
                while (pad-- > 0) pos = emit_char(buf, pos, limit, '0');
            for (int i = len - 1; i >= 0; i--)
                pos = emit_char(buf, pos, limit, tmp[i]);
            if (left_align)
                while (pad-- > 0) pos = emit_char(buf, pos, limit, ' ');
            break;
        }
        case 'u': {
            char tmp[22];
            int len = 0;
            if (is_longlong) {
                unsigned long long ullval = va_arg(ap, unsigned long long);
                unsigned long hi = (unsigned long)(ullval >> 32);
                unsigned long lo = (unsigned long)ullval;
                if (hi == 0) {
                    if (lo == 0) tmp[len++] = '0';
                    else while (lo > 0) { tmp[len++] = '0' + (lo % 10); lo /= 10; }
                } else {
                    while (hi > 0 || lo > 0) {
                        unsigned long r = hi % 10;
                        hi /= 10;
                        unsigned long lo_hi = (r << 16) | (lo >> 16);
                        unsigned long q_hi = lo_hi / 10;
                        unsigned long r2 = lo_hi % 10;
                        unsigned long lo_lo = (r2 << 16) | (lo & 0xFFFF);
                        unsigned long q_lo = lo_lo / 10;
                        tmp[len++] = '0' + (int)(lo_lo % 10);
                        lo = (q_hi << 16) | q_lo;
                    }
                    if (len == 0) tmp[len++] = '0';
                }
            } else {
                unsigned long val = (unsigned long)va_arg(ap, unsigned int);
                if (val == 0) tmp[len++] = '0';
                else while (val > 0) { tmp[len++] = '0' + (val % 10); val /= 10; }
            }
            int pad = width > len ? width - len : 0;
            if (!left_align && !pad_zero)
                while (pad-- > 0) pos = emit_char(buf, pos, limit, ' ');
            if (!left_align && pad_zero)
                while (pad-- > 0) pos = emit_char(buf, pos, limit, '0');
            for (int i = len - 1; i >= 0; i--)
                pos = emit_char(buf, pos, limit, tmp[i]);
            if (left_align)
                while (pad-- > 0) pos = emit_char(buf, pos, limit, ' ');
            break;
        }
        case 'o': {
            /* Octal uses shifts — no 64-bit division needed */
            unsigned long long val;
            if (is_longlong) val = va_arg(ap, unsigned long long);
            else val = va_arg(ap, unsigned int);
            char tmp[22];
            int len = 0;
            if (val == 0) tmp[len++] = '0';
            else while (val > 0) {
                tmp[len++] = '0' + (int)(val & 7);
                val >>= 3;
            }
            for (int i = len - 1; i >= 0; i--)
                pos = emit_char(buf, pos, limit, tmp[i]);
            break;
        }
        case 'x': case 'X': {
            /* Hex uses shifts — no 64-bit division needed */
            unsigned long long val;
            if (is_longlong) val = va_arg(ap, unsigned long long);
            else val = va_arg(ap, unsigned int);
            char tmp[16];
            int len = 0;
            const char *hex = (*p == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            if (val == 0) tmp[len++] = '0';
            else while (val > 0) {
                tmp[len++] = hex[val & 0xF];
                val >>= 4;
            }
            int pad = width > len ? width - len : 0;
            if (!left_align)
                while (pad-- > 0) pos = emit_char(buf, pos, limit, pad_zero ? '0' : ' ');
            for (int i = len - 1; i >= 0; i--)
                pos = emit_char(buf, pos, limit, tmp[i]);
            if (left_align)
                while (pad-- > 0) pos = emit_char(buf, pos, limit, ' ');
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            pos = emit_char(buf, pos, limit, c);
            break;
        }
        case 'p': {
            unsigned long val = (unsigned long)va_arg(ap, void *);
            pos = emit_str(buf, pos, limit, "0x");
            pos = emit_uint(buf, pos, limit, val, 16);
            break;
        }
        case '%':
            pos = emit_char(buf, pos, limit, '%');
            break;
        default:
            pos = emit_char(buf, pos, limit, '%');
            pos = emit_char(buf, pos, limit, *p);
            break;
        }
        p++;
    }

    if (limit > 0)
        buf[pos < limit ? pos : limit - 1] = '\0';
    return pos;
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
