/*
 * which — locate a command in PATH
 *
 * Usage: which command ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: which command ...\n");
        return 1;
    }

    const char *path = getenv("PATH");
    if (!path)
        path = "/bin";

    int ret = 1;
    for (int i = 1; i < argc; i++) {
        /* If it contains '/', check directly */
        if (strchr(argv[i], '/')) {
            if (access(argv[i], X_OK) == 0) {
                puts(argv[i]);
                ret = 0;
            } else {
                fprintf(stderr, "which: %s not found\n", argv[i]);
            }
            continue;
        }

        /* Search PATH */
        const char *p = path;
        int found = 0;
        while (*p) {
            const char *end = strchr(p, ':');
            int dirlen = end ? (int)(end - p) : (int)strlen(p);

            char fullpath[256];
            if (dirlen + 1 + (int)strlen(argv[i]) + 1 > 256) {
                p = end ? end + 1 : p + strlen(p);
                continue;
            }
            memcpy(fullpath, p, dirlen);
            fullpath[dirlen] = '/';
            strcpy(fullpath + dirlen + 1, argv[i]);

            if (access(fullpath, X_OK) == 0) {
                puts(fullpath);
                found = 1;
                ret = 0;
                break;
            }

            p = end ? end + 1 : p + strlen(p);
        }
        if (!found)
            fprintf(stderr, "which: %s not found\n", argv[i]);
    }
    return ret;
}
