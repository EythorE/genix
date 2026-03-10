/*
 * expand — convert tabs to spaces
 *
 * Usage: expand [-t tabstop] [file ...]
 * Default tab stop: 8.
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0
#define DEFAULT_TAB 8

static void expand_fd(int fd, int tabstop)
{
    char c;
    int col = 0;

    while (read(fd, &c, 1) == 1) {
        if (c == '\t') {
            /* Expand to next tab stop. tabstop is small constant, so
             * modulo is DIVU.W safe (divisor fits in 16 bits). */
            int spaces = tabstop - (col % tabstop);
            for (int j = 0; j < spaces; j++)
                write(1, " ", 1);
            col += spaces;
        } else if (c == '\n') {
            write(1, &c, 1);
            col = 0;
        } else {
            write(1, &c, 1);
            col++;
        }
    }
}

static int parse_int(const char *s)
{
    int n = 0;
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (*s++ - '0');
    return n;
}

int main(int argc, char **argv)
{
    int tabstop = DEFAULT_TAB;
    int i = 1;

    /* Parse -t option */
    if (i < argc && argv[i][0] == '-' && argv[i][1] == 't' && argv[i][2] == '\0') {
        i++;
        if (i < argc)
            tabstop = parse_int(argv[i++]);
        if (tabstop < 1)
            tabstop = DEFAULT_TAB;
    }

    if (i >= argc) {
        expand_fd(0, tabstop);
    } else {
        for (; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                static const char msg[] = "expand: cannot open file\n";
                write(2, msg, sizeof(msg) - 1);
                return 1;
            }
            expand_fd(fd, tabstop);
            close(fd);
        }
    }
    return 0;
}
