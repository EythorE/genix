/*
 * wc — count lines, words, and characters
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0

/* Simple integer-to-string, returns length written */
static int itoa_buf(char *buf, unsigned int n)
{
    char tmp[12];
    int len = 0;
    if (n == 0) {
        buf[0] = '0';
        return 1;
    }
    while (n > 0) {
        /* DIVU.W safe: divisor=10 fits in 16 bits */
        tmp[len++] = '0' + (n % 10);
        n /= 10;
    }
    for (int i = 0; i < len; i++)
        buf[i] = tmp[len - 1 - i];
    return len;
}

static void print_num(unsigned int n)
{
    char buf[12];
    int len = itoa_buf(buf, n);
    write(1, buf, len);
}

static void wc_fd(int fd)
{
    char buf[256];
    unsigned int lines = 0, words = 0, chars = 0;
    int in_word = 0;
    int n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            chars++;
            if (buf[i] == '\n')
                lines++;
            if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }

    write(1, "  ", 2);
    print_num(lines);
    write(1, "  ", 2);
    print_num(words);
    write(1, "  ", 2);
    print_num(chars);
    write(1, "\n", 1);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        wc_fd(0);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            static const char msg[] = "wc: cannot open file\n";
            write(2, msg, sizeof(msg) - 1);
            return 1;
        }
        wc_fd(fd);
        close(fd);
    }
    return 0;
}
