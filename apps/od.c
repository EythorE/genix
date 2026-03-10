/*
 * od — octal/hex dump of files
 *
 * Usage: od [-x] [file]
 *   -x  hex output (default: octal)
 *   Reads stdin if no file given.
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0

static void write_str(int fd, const char *s)
{
    int n = 0;
    while (s[n]) n++;
    write(fd, s, n);
}

static char hex_digit(int n)
{
    /* n is 0-15 */
    return n < 10 ? '0' + n : 'a' + n - 10;
}

static void write_hex16(unsigned int addr)
{
    char buf[8];
    int i;
    for (i = 5; i >= 0; i--) {
        /* Shift safe: no division */
        buf[i] = hex_digit(addr & 0xF);
        addr >>= 4;
    }
    buf[6] = ' ';
    buf[7] = ' ';
    write(1, buf, 8);
}

static void write_octal_byte(unsigned char b)
{
    char buf[4];
    /* Shift safe: no division */
    buf[0] = '0' + ((b >> 6) & 7);
    buf[1] = '0' + ((b >> 3) & 7);
    buf[2] = '0' + (b & 7);
    buf[3] = ' ';
    write(1, buf, 4);
}

static void write_hex_byte(unsigned char b)
{
    char buf[4];
    /* Shift safe: no division */
    buf[0] = hex_digit((b >> 4) & 0xF);
    buf[1] = hex_digit(b & 0xF);
    buf[2] = ' ';
    write(1, buf, 3);
}

static void od_fd(int fd, int hex_mode)
{
    unsigned char buf[16];
    unsigned int offset = 0;
    int n;

    while ((n = read(fd, buf, 16)) > 0) {
        write_hex16(offset);
        for (int i = 0; i < n; i++) {
            if (hex_mode)
                write_hex_byte(buf[i]);
            else
                write_octal_byte(buf[i]);
        }
        write(1, "\n", 1);
        offset += n;
    }
    write_hex16(offset);
    write(1, "\n", 1);
}

int main(int argc, char **argv)
{
    int hex_mode = 0;
    int argi = 1;

    /* Parse options */
    while (argi < argc && argv[argi][0] == '-') {
        if (argv[argi][1] == 'x')
            hex_mode = 1;
        argi++;
    }

    if (argi >= argc) {
        od_fd(0, hex_mode);
        return 0;
    }

    int fd = open(argv[argi], O_RDONLY);
    if (fd < 0) {
        write_str(2, "od: cannot open file\n");
        return 1;
    }
    od_fd(fd, hex_mode);
    close(fd);
    return 0;
}
