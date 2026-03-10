/*
 * cut — select fields or character positions from lines
 *
 * Supports: -d delimiter -f field-list
 *           -c char-list
 * Field/char lists are simple: single numbers or ranges (N-M).
 */
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);

#define LINE_MAX 512

static int parse_range(const char *s, int *lo, int *hi)
{
    int n = 0;
    *lo = 0;
    *hi = 0;

    while (*s >= '0' && *s <= '9')
        n = n * 10 + (*s++ - '0');
    *lo = n;

    if (*s == '-') {
        s++;
        n = 0;
        while (*s >= '0' && *s <= '9')
            n = n * 10 + (*s++ - '0');
        *hi = n > 0 ? n : 9999;
    } else {
        *hi = *lo;
    }
    return *lo > 0;
}

static void cut_fields(int delim, const char *spec)
{
    char line[LINE_MAX];
    int pos = 0;
    char c;
    int lo, hi;

    if (!parse_range(spec, &lo, &hi))
        return;

    while (read(0, &c, 1) == 1) {
        if (c == '\n') {
            line[pos] = '\0';
            /* Extract fields lo..hi */
            int field = 1;
            int i = 0;
            int first = 1;
            while (i <= pos) {
                int fstart = i;
                while (i < pos && line[i] != delim)
                    i++;
                if (field >= lo && field <= hi) {
                    if (!first)
                        write(1, (char *)&delim, 1);
                    write(1, &line[fstart], i - fstart);
                    first = 0;
                }
                field++;
                if (i < pos) i++;  /* skip delimiter */
                else break;
            }
            write(1, "\n", 1);
            pos = 0;
        } else if (pos < LINE_MAX - 1) {
            line[pos++] = c;
        }
    }
}

static void cut_chars(const char *spec)
{
    char line[LINE_MAX];
    int pos = 0;
    char c;
    int lo, hi;

    if (!parse_range(spec, &lo, &hi))
        return;

    while (read(0, &c, 1) == 1) {
        if (c == '\n') {
            for (int i = lo - 1; i < hi && i < pos; i++)
                write(1, &line[i], 1);
            write(1, "\n", 1);
            pos = 0;
        } else if (pos < LINE_MAX - 1) {
            line[pos++] = c;
        }
    }
}

int main(int argc, char **argv)
{
    int delim = '\t';
    const char *field_spec = (const char *)0;
    const char *char_spec = (const char *)0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'd' && argv[i][2] == '\0') {
            if (i + 1 < argc)
                delim = argv[++i][0];
        } else if (argv[i][0] == '-' && argv[i][1] == 'f' && argv[i][2] == '\0') {
            if (i + 1 < argc)
                field_spec = argv[++i];
        } else if (argv[i][0] == '-' && argv[i][1] == 'c' && argv[i][2] == '\0') {
            if (i + 1 < argc)
                char_spec = argv[++i];
        }
    }

    if (field_spec) {
        cut_fields(delim, field_spec);
    } else if (char_spec) {
        cut_chars(char_spec);
    } else {
        static const char msg[] = "usage: cut -f list [-d delim] | -c list\n";
        write(2, msg, sizeof(msg) - 1);
        return 1;
    }
    return 0;
}
