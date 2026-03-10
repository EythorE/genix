/*
 * tee — read stdin, write to stdout and files
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_WRONLY 0x0001
#define O_CREAT  0x0040
#define O_TRUNC  0x0200
#define MAX_FILES 8

int main(int argc, char **argv)
{
    int fds[MAX_FILES];
    int nfds = 0;
    char buf[256];
    int n, i;

    /* Open output files */
    for (i = 1; i < argc && nfds < MAX_FILES; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) {
            static const char msg[] = "tee: cannot open file\n";
            write(2, msg, sizeof(msg) - 1);
            return 1;
        }
        fds[nfds++] = fd;
    }

    /* Copy stdin to stdout and all files */
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
        for (i = 0; i < nfds; i++)
            write(fds[i], buf, n);
    }

    for (i = 0; i < nfds; i++)
        close(fds[i]);
    return 0;
}
