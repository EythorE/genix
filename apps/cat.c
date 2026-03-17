/*
 * cat — concatenate and print files
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

static void cat_fd(int fd)
{
    char buf[256];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, n);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        /* No args: read from stdin */
        cat_fd(0);
        return 0;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "cat: %s: No such file or directory\n", argv[i]);
            ret = 1;
            continue;
        }
        cat_fd(fd);
        close(fd);
    }
    return ret;
}
