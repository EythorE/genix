/*
 * strings — extract printable strings from files
 *
 * Usage: strings [-n min] [file ...]
 * Prints sequences of >= min (default 4) printable characters.
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0
#define DEFAULT_MIN 4
#define BUF_SIZE 256

static void strings_fd(int fd, int minlen)
{
    unsigned char buf[BUF_SIZE];
    char run[BUF_SIZE];
    int runlen = 0;
    int n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            unsigned char c = buf[i];
            /* Printable ASCII: 0x20-0x7E, plus tab */
            if ((c >= 0x20 && c <= 0x7E) || c == '\t') {
                if (runlen < BUF_SIZE - 1)
                    run[runlen++] = (char)c;
            } else {
                if (runlen >= minlen) {
                    write(1, run, runlen);
                    write(1, "\n", 1);
                }
                runlen = 0;
            }
        }
    }
    /* Flush final run */
    if (runlen >= minlen) {
        write(1, run, runlen);
        write(1, "\n", 1);
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
    int minlen = DEFAULT_MIN;
    int i = 1;

    /* Parse -n option */
    if (i < argc && argv[i][0] == '-' && argv[i][1] == 'n' && argv[i][2] == '\0') {
        i++;
        if (i < argc)
            minlen = parse_int(argv[i++]);
        if (minlen < 1)
            minlen = 1;
    }
    /* Also support -N (e.g., -6) */
    if (i == 1 && i < argc && argv[i][0] == '-' && argv[i][1] >= '1' && argv[i][1] <= '9') {
        minlen = parse_int(&argv[i][1]);
        i++;
    }

    if (i >= argc) {
        strings_fd(0, minlen);
    } else {
        for (; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                static const char msg[] = "strings: cannot open file\n";
                write(2, msg, sizeof(msg) - 1);
                return 1;
            }
            strings_fd(fd, minlen);
            close(fd);
        }
    }
    return 0;
}
