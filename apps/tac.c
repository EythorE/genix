/*
 * tac — concatenate and print files in reverse (line by line)
 *
 * Usage: tac [file ...]
 * Reads entire input into buffer, then prints lines last-to-first.
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0
#define BUF_SIZE 4096

static char data[BUF_SIZE];

static void tac_fd(int fd)
{
    int total = 0;
    int n;

    /* Read entire input into buffer */
    while (total < BUF_SIZE && (n = read(fd, data + total, BUF_SIZE - total)) > 0)
        total += n;

    if (total == 0)
        return;

    /* Walk backwards, printing each line */
    int end = total;
    while (end > 0) {
        /* Find start of current line */
        int start = end - 1;
        while (start > 0 && data[start - 1] != '\n')
            start--;

        write(1, &data[start], end - start);
        /* Ensure line ends with newline */
        if (data[end - 1] != '\n')
            write(1, "\n", 1);

        end = start;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        tac_fd(0);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            static const char msg[] = "tac: cannot open file\n";
            write(2, msg, sizeof(msg) - 1);
            return 1;
        }
        tac_fd(fd);
        close(fd);
    }
    return 0;
}
