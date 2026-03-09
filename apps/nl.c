/*
 * nl — number lines of a file
 */
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int open(const char *path, int flags);
int close(int fd);

#define O_RDONLY 0

static void put_num(unsigned int n)
{
    char buf[8];
    int len = 0;
    char tmp[8];

    if (n == 0) {
        tmp[len++] = '0';
    } else {
        while (n > 0) {
            /* DIVU.W safe: divisor=10 fits in 16 bits */
            tmp[len++] = '0' + (n % 10);
            n /= 10;
        }
    }

    /* Right-justify in 6 columns */
    int pad = 6 - len;
    for (int i = 0; i < pad; i++)
        buf[i] = ' ';
    for (int i = 0; i < len; i++)
        buf[pad + i] = tmp[len - 1 - i];

    write(1, buf, pad + len);
    write(1, "\t", 1);
}

static void nl_fd(int fd)
{
    unsigned int lineno = 1;
    int at_bol = 1;  /* at beginning of line */
    char buf[256];
    int n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (at_bol) {
                put_num(lineno++);
                at_bol = 0;
            }
            write(1, &buf[i], 1);
            if (buf[i] == '\n')
                at_bol = 1;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        nl_fd(0);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            static const char msg[] = "nl: cannot open file\n";
            write(2, msg, sizeof(msg) - 1);
            return 1;
        }
        nl_fd(fd);
        close(fd);
    }
    return 0;
}
