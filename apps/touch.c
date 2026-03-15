/*
 * touch — create files or update timestamps
 *
 * Usage: touch file ...
 *
 * Creates the file if it doesn't exist. On Genix, timestamps
 * are not updated for existing files (no utimes syscall yet).
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: touch file ...\n");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT);
        if (fd < 0) {
            fprintf(stderr, "touch: cannot create '%s'\n", argv[i]);
            ret = 1;
        } else {
            close(fd);
        }
    }
    return ret;
}
