/*
 * Kernel console I/O and printf
 */
#include "kernel.h"

void kputc(char c)
{
    if (c == '\n')
        pal_console_putc('\r');
    pal_console_putc(c);
}

void kputs(const char *s)
{
    while (*s)
        kputc(*s++);
}

int kgetc(void)
{
    while (!pal_console_ready())
        ;
    return pal_console_getc();
}

/* Minimal printf: %d, %u, %x, %s, %c, %% */
static void print_num(uint32_t val, int base, int is_signed)
{
    char buf[12];
    int i = 0;
    int neg = 0;

    if (is_signed && (int32_t)val < 0) {
        neg = 1;
        val = -(int32_t)val;
    }

    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            int d = val % base;
            buf[i++] = d < 10 ? '0' + d : 'a' + d - 10;
            val /= base;
        }
    }
    if (neg)
        kputc('-');
    while (i > 0)
        kputc(buf[--i]);
}

void kprintf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            kputc(*fmt++);
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 'd':
            print_num(__builtin_va_arg(ap, uint32_t), 10, 1);
            break;
        case 'u':
            print_num(__builtin_va_arg(ap, uint32_t), 10, 0);
            break;
        case 'x':
            print_num(__builtin_va_arg(ap, uint32_t), 16, 0);
            break;
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (s)
                kputs(s);
            else
                kputs("(null)");
            break;
        }
        case 'c':
            kputc(__builtin_va_arg(ap, int));
            break;
        case '%':
            kputc('%');
            break;
        default:
            kputc('%');
            kputc(*fmt);
            break;
        }
        fmt++;
    }
    __builtin_va_end(ap);
}
