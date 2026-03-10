/*
 * rev — reverse lines of a file
 */
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int open(const char *path, int flags);
int close(int fd);

#define O_RDONLY 0
#define LINE_MAX 512

static void rev_fd(int fd)
{
    char line[LINE_MAX];
    int pos = 0;
    char c;

    while (read(fd, &c, 1) == 1) {
        if (c == '\n') {
            /* Reverse and output the line */
            for (int i = pos - 1; i >= 0; i--)
                write(1, &line[i], 1);
            write(1, "\n", 1);
            pos = 0;
        } else if (pos < LINE_MAX - 1) {
            line[pos++] = c;
        }
    }
    /* Handle last line without trailing newline */
    if (pos > 0) {
        for (int i = pos - 1; i >= 0; i--)
            write(1, &line[i], 1);
        write(1, "\n", 1);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        rev_fd(0);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            static const char msg[] = "rev: cannot open file\n";
            write(2, msg, sizeof(msg) - 1);
            return 1;
        }
        rev_fd(fd);
        close(fd);
    }
    return 0;
}
