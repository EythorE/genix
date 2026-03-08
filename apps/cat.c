/*
 * cat — concatenate and print files
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0

static void cat_fd(int fd)
{
    char buf[256];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, n);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        /* No args: read from stdin */
        cat_fd(0);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            const char *msg = "cat: cannot open file\n";
            write(2, msg, 22);
            return 1;
        }
        cat_fd(fd);
        close(fd);
    }
    return 0;
}
