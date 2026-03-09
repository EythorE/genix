/*
 * head — print first N lines of a file (default 10)
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0
#define DEFAULT_LINES 10

static void head_fd(int fd, int max_lines)
{
    char buf[256];
    int lines = 0;
    int n;

    while (lines < max_lines && (n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n && lines < max_lines; i++) {
            write(1, &buf[i], 1);
            if (buf[i] == '\n')
                lines++;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        head_fd(0, DEFAULT_LINES);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            static const char msg[] = "head: cannot open file\n";
            write(2, msg, sizeof(msg) - 1);
            return 1;
        }
        head_fd(fd, DEFAULT_LINES);
        close(fd);
    }
    return 0;
}
