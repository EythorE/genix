/*
 * comm — compare two sorted files line by line
 *
 * Usage: comm [-123] file1 file2
 * Outputs three columns: lines only in file1, only in file2, common.
 * -1/-2/-3 suppress respective columns.
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0
#define LINE_MAX 512

/* Read one line from fd into buf. Returns length, 0 on EOF. */
static int read_line(int fd, char *buf, int size)
{
    int pos = 0;
    char c;

    while (pos < size - 1 && read(fd, &c, 1) == 1) {
        if (c == '\n') {
            buf[pos] = '\0';
            return pos;
        }
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return pos;
}

static int str_cmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int main(int argc, char **argv)
{
    int suppress1 = 0, suppress2 = 0, suppress3 = 0;
    int i = 1;

    /* Parse flags */
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *p = &argv[i][1];
        while (*p) {
            if (*p == '1') suppress1 = 1;
            else if (*p == '2') suppress2 = 1;
            else if (*p == '3') suppress3 = 1;
            p++;
        }
        i++;
    }

    if (argc - i < 2) {
        static const char msg[] = "usage: comm [-123] file1 file2\n";
        write(2, msg, sizeof(msg) - 1);
        return 1;
    }

    int fd1 = open(argv[i], O_RDONLY);
    int fd2 = open(argv[i + 1], O_RDONLY);
    if (fd1 < 0 || fd2 < 0) {
        static const char msg[] = "comm: cannot open file\n";
        write(2, msg, sizeof(msg) - 1);
        return 1;
    }

    char line1[LINE_MAX], line2[LINE_MAX];
    int len1 = read_line(fd1, line1, LINE_MAX);
    int len2 = read_line(fd2, line2, LINE_MAX);

    while (len1 > 0 || len2 > 0) {
        int cmp;
        if (len1 == 0)
            cmp = 1;   /* file1 exhausted, file2 wins */
        else if (len2 == 0)
            cmp = -1;  /* file2 exhausted, file1 wins */
        else
            cmp = str_cmp(line1, line2);

        if (cmp < 0) {
            /* Line only in file1 */
            if (!suppress1) {
                write(1, line1, len1);
                write(1, "\n", 1);
            }
            len1 = read_line(fd1, line1, LINE_MAX);
        } else if (cmp > 0) {
            /* Line only in file2 */
            if (!suppress2) {
                if (!suppress1)
                    write(1, "\t", 1);
                write(1, line2, len2);
                write(1, "\n", 1);
            }
            len2 = read_line(fd2, line2, LINE_MAX);
        } else {
            /* Common line */
            if (!suppress3) {
                if (!suppress1)
                    write(1, "\t", 1);
                if (!suppress2)
                    write(1, "\t", 1);
                write(1, line1, len1);
                write(1, "\n", 1);
            }
            len1 = read_line(fd1, line1, LINE_MAX);
            len2 = read_line(fd2, line2, LINE_MAX);
        }
    }

    close(fd1);
    close(fd2);
    return 0;
}
