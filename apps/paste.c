/*
 * paste — merge lines from files
 *
 * Usage: paste [-d delim] file1 file2 ...
 * Reads lines from each file and joins them with delimiter (default tab).
 * Use "-" for stdin.
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0
#define MAX_FILES 8
#define LINE_MAX 512

/* Read one line from fd into buf (up to size-1 chars). Returns length, 0 on EOF. */
static int read_line(int fd, char *buf, int size)
{
    int pos = 0;
    char c;

    while (pos < size - 1 && read(fd, &c, 1) == 1) {
        if (c == '\n')
            return pos;  /* Don't include newline */
        buf[pos++] = c;
    }
    return pos;  /* 0 means EOF */
}

int main(int argc, char **argv)
{
    char delim = '\t';
    int fds[MAX_FILES];
    int nfiles = 0;
    int done[MAX_FILES];
    char line[LINE_MAX];

    /* Parse args */
    int i = 1;
    if (i < argc && argv[i][0] == '-' && argv[i][1] == 'd' && argv[i][2] == '\0') {
        i++;
        if (i < argc)
            delim = argv[i++][0];
    }

    /* Open files */
    for (; i < argc && nfiles < MAX_FILES; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '\0') {
            fds[nfiles++] = 0;  /* stdin */
        } else {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                static const char msg[] = "paste: cannot open file\n";
                write(2, msg, sizeof(msg) - 1);
                return 1;
            }
            fds[nfiles++] = fd;
        }
    }

    if (nfiles == 0) {
        static const char msg[] = "usage: paste [-d delim] file1 file2 ...\n";
        write(2, msg, sizeof(msg) - 1);
        return 1;
    }

    /* Initialize done flags */
    for (i = 0; i < nfiles; i++)
        done[i] = 0;

    /* Merge lines */
    for (;;) {
        int all_done = 1;
        for (i = 0; i < nfiles; i++) {
            if (i > 0)
                write(1, &delim, 1);

            if (!done[i]) {
                int len = read_line(fds[i], line, LINE_MAX);
                if (len > 0) {
                    write(1, line, len);
                    all_done = 0;
                } else {
                    done[i] = 1;
                }
            }
        }
        write(1, "\n", 1);
        if (all_done)
            break;
    }

    /* Close files */
    for (i = 0; i < nfiles; i++) {
        if (fds[i] > 0)
            close(fds[i]);
    }
    return 0;
}
