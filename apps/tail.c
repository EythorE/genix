/*
 * tail — output the last N lines of a file (default 10)
 *
 * Simple ring-buffer approach: store line start positions,
 * then replay the last N.
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);
int lseek(int fd, int offset, int whence);

#define O_RDONLY 0
#define SEEK_SET 0
#define DEFAULT_LINES 10
#define BUF_SIZE 4096

/* Store the data in a buffer and track line positions */
static char data[BUF_SIZE];

static void tail_fd(int fd, int nlines)
{
    int total = 0;
    int n;

    /* Read entire file into buffer (up to BUF_SIZE) */
    while (total < BUF_SIZE && (n = read(fd, data + total, BUF_SIZE - total)) > 0)
        total += n;

    if (total == 0)
        return;

    /* Count backwards from end to find the start of the last N lines */
    int count = 0;
    int start = total;

    /* If last char is newline, skip it */
    if (start > 0 && data[start - 1] == '\n')
        start--;

    while (start > 0 && count < nlines) {
        start--;
        if (data[start] == '\n') {
            count++;
            if (count == nlines) {
                start++;  /* skip past the newline */
                break;
            }
        }
    }

    write(1, data + start, total - start);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        tail_fd(0, DEFAULT_LINES);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            static const char msg[] = "tail: cannot open file\n";
            write(2, msg, sizeof(msg) - 1);
            return 1;
        }
        tail_fd(fd, DEFAULT_LINES);
        close(fd);
    }
    return 0;
}
