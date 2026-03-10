/*
 * fold — wrap lines to a specified width
 *
 * Usage: fold [-w width] [file ...]
 * Default width: 80 columns.
 */
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int close(int fd);

#define O_RDONLY 0
#define DEFAULT_WIDTH 80

static void fold_fd(int fd, int width)
{
    char c;
    int col = 0;

    while (read(fd, &c, 1) == 1) {
        if (c == '\n') {
            write(1, &c, 1);
            col = 0;
        } else if (c == '\t') {
            /* Tab advances to next 8-column stop */
            int next = (col + 8) & ~7;  /* power-of-2 mask, no division */
            if (next > width) {
                write(1, "\n", 1);
                col = 0;
            }
            write(1, &c, 1);
            col = next;
        } else {
            if (col >= width) {
                write(1, "\n", 1);
                col = 0;
            }
            write(1, &c, 1);
            col++;
        }
    }
}

static int parse_int(const char *s)
{
    int n = 0;
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (*s++ - '0');
    return n;
}

int main(int argc, char **argv)
{
    int width = DEFAULT_WIDTH;
    int i = 1;

    /* Parse -w option */
    if (i < argc && argv[i][0] == '-' && argv[i][1] == 'w' && argv[i][2] == '\0') {
        i++;
        if (i < argc)
            width = parse_int(argv[i++]);
        if (width < 1)
            width = DEFAULT_WIDTH;
    }

    if (i >= argc) {
        fold_fd(0, width);
    } else {
        for (; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                static const char msg[] = "fold: cannot open file\n";
                write(2, msg, sizeof(msg) - 1);
                return 1;
            }
            fold_fd(fd, width);
            close(fd);
        }
    }
    return 0;
}
