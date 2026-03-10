/*
 * cmp — compare two files byte by byte
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0

static unsigned int slen(const char *s)
{
    unsigned int n = 0;
    while (*s++) n++;
    return n;
}

static void put_num(unsigned long n)
{
    char buf[12];
    char tmp[12];
    int len = 0;

    if (n == 0) {
        write(1, "0", 1);
        return;
    }
    while (n > 0) {
        /* DIVU.W safe: divisor=10 fits in 16 bits */
        tmp[len++] = '0' + (n % 10);
        n /= 10;
    }
    for (int i = 0; i < len; i++)
        buf[i] = tmp[len - 1 - i];
    write(1, buf, len);
}

int main(int argc, char **argv)
{
    int fd1, fd2;
    unsigned char c1, c2;
    int n1, n2;
    unsigned long byte = 1;
    unsigned long line = 1;

    if (argc < 3) {
        static const char msg[] = "usage: cmp file1 file2\n";
        write(2, msg, sizeof(msg) - 1);
        return 2;
    }

    fd1 = open(argv[1], O_RDONLY);
    if (fd1 < 0) {
        write(2, "cmp: ", 5);
        write(2, argv[1], slen(argv[1]));
        write(2, ": cannot open\n", 14);
        return 2;
    }

    fd2 = open(argv[2], O_RDONLY);
    if (fd2 < 0) {
        write(2, "cmp: ", 5);
        write(2, argv[2], slen(argv[2]));
        write(2, ": cannot open\n", 14);
        close(fd1);
        return 2;
    }

    for (;;) {
        n1 = read(fd1, &c1, 1);
        n2 = read(fd2, &c2, 1);

        if (n1 <= 0 && n2 <= 0)
            break;  /* Both ended — files are identical */

        if (n1 <= 0 || n2 <= 0) {
            /* One file is shorter */
            write(1, "cmp: EOF on ", 12);
            write(1, n1 <= 0 ? argv[1] : argv[2],
                  slen(n1 <= 0 ? argv[1] : argv[2]));
            write(1, "\n", 1);
            close(fd1);
            close(fd2);
            return 1;
        }

        if (c1 != c2) {
            write(1, argv[1], slen(argv[1]));
            write(1, " ", 1);
            write(1, argv[2], slen(argv[2]));
            write(1, " differ: byte ", 14);
            put_num(byte);
            write(1, ", line ", 7);
            put_num(line);
            write(1, "\n", 1);
            close(fd1);
            close(fd2);
            return 1;
        }

        if (c1 == '\n')
            line++;
        byte++;
    }

    close(fd1);
    close(fd2);
    return 0;
}
