/*
 * rm — remove files
 *
 * Usage: rm [-r] file ...
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static int opt_recursive;

static int rm_recursive(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0) {
        fprintf(stderr, "rm: cannot stat '%s'\n", path);
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!opt_recursive) {
            fprintf(stderr, "rm: '%s' is a directory\n", path);
            return 1;
        }
        DIR *d = opendir(path);
        if (!d) {
            fprintf(stderr, "rm: cannot open '%s'\n", path);
            return 1;
        }
        int ret = 0;
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 ||
                strcmp(ent->d_name, "..") == 0)
                continue;
            char child[256];
            int plen = strlen(path);
            if (plen + 1 + strlen(ent->d_name) + 1 > 256) {
                fprintf(stderr, "rm: path too long\n");
                ret = 1;
                continue;
            }
            strcpy(child, path);
            if (plen > 0 && child[plen - 1] != '/') {
                child[plen] = '/';
                child[plen + 1] = '\0';
            }
            strcat(child, ent->d_name);
            if (rm_recursive(child) != 0)
                ret = 1;
        }
        closedir(d);
        if (rmdir(path) < 0) {
            fprintf(stderr, "rm: cannot remove '%s'\n", path);
            ret = 1;
        }
        return ret;
    }

    if (unlink(path) < 0) {
        fprintf(stderr, "rm: cannot remove '%s'\n", path);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1;
    opt_recursive = 0;

    if (i < argc && strcmp(argv[i], "-r") == 0) {
        opt_recursive = 1;
        i++;
    }

    if (i >= argc) {
        fprintf(stderr, "Usage: rm [-r] file ...\n");
        return 1;
    }

    int ret = 0;
    for (; i < argc; i++) {
        if (rm_recursive(argv[i]) != 0)
            ret = 1;
    }
    return ret;
}
