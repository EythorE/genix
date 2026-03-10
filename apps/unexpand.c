/*
 * unexpand — convert leading spaces to tabs
 *
 * Usage: unexpand [-t tabstop] [file ...]
 * Default tab stop: 8. Only leading whitespace is converted.
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0
#define DEFAULT_TAB 8
#define LINE_MAX 512

static void unexpand_fd(int fd, int tabstop)
{
    char line[LINE_MAX];
    int pos = 0;
    char c;

    while (read(fd, &c, 1) == 1) {
        if (c == '\n') {
            line[pos] = '\0';

            /* Process leading spaces */
            int i = 0;
            int col = 0;

            /* Count leading spaces/tabs and convert to tabs */
            while (i < pos && (line[i] == ' ' || line[i] == '\t')) {
                if (line[i] == '\t') {
                    /* Advance to next tab stop. DIVU.W safe: tabstop is small. */
                    col = col + tabstop - (col % tabstop);
                } else {
                    col++;
                }
                i++;
            }

            /* Emit optimal tab/space sequence for 'col' columns */
            /* DIVU.W safe: tabstop is small constant */
            int ntabs = col / tabstop;
            int nspaces = col % tabstop;
            for (int j = 0; j < ntabs; j++)
                write(1, "\t", 1);
            for (int j = 0; j < nspaces; j++)
                write(1, " ", 1);

            /* Emit rest of line unchanged */
            if (i < pos)
                write(1, &line[i], pos - i);
            write(1, "\n", 1);
            pos = 0;
        } else if (pos < LINE_MAX - 1) {
            line[pos++] = c;
        }
    }
    /* Flush last line if no trailing newline */
    if (pos > 0) {
        write(1, line, pos);
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
        unexpand_fd(0, tabstop);
    } else {
        for (; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                static const char msg[] = "unexpand: cannot open file\n";
                write(2, msg, sizeof(msg) - 1);
                return 1;
            }
            unexpand_fd(fd, tabstop);
            close(fd);
        }
    }
    return 0;
}
