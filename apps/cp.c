/*
 * cp — copy files
 *
 * Usage: cp source dest
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: cp source dest\n");
        return 1;
    }

    int src = open(argv[1], O_RDONLY);
    if (src < 0) {
        fprintf(stderr, "cp: cannot open '%s'\n", argv[1]);
        return 1;
    }

    int dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (dst < 0) {
        fprintf(stderr, "cp: cannot create '%s'\n", argv[2]);
        close(src);
        return 1;
    }

    char buf[256];
    int n;
    while ((n = read(src, buf, sizeof(buf))) > 0) {
        int written = 0;
        while (written < n) {
            int w = write(dst, buf + written, n - written);
            if (w < 0) {
                fprintf(stderr, "cp: write error\n");
                close(src);
                close(dst);
                return 1;
            }
            written += w;
        }
    }

    close(src);
    close(dst);
    return (n < 0) ? 1 : 0;
}
