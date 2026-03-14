/*
 * ls — list directory contents
 *
 * Usage: ls [-l] [path ...]
 *
 * Without -l: prints names only, one per line.
 * With -l: prints type, size, and name.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>

static int opt_long = 0;

static void ls_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "ls: cannot open '%s'\n", path);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        /* Skip . and .. for cleaner output */
        if (strcmp(ent->d_name, ".") == 0 ||
            strcmp(ent->d_name, "..") == 0)
            continue;

        if (opt_long) {
            /* Build full path for stat */
            char fullpath[256];
            int plen = strlen(path);
            if (plen + 1 + strlen(ent->d_name) + 1 > 256) {
                puts(ent->d_name);
                continue;
            }
            strcpy(fullpath, path);
            if (plen > 0 && fullpath[plen - 1] != '/') {
                fullpath[plen] = '/';
                fullpath[plen + 1] = '\0';
            }
            strcat(fullpath, ent->d_name);

            struct stat st;
            if (stat(fullpath, &st) < 0) {
                printf("?  ??\t%s\n", ent->d_name);
                continue;
            }

            char type = '?';
            if (S_ISREG(st.st_mode))  type = '-';
            else if (S_ISDIR(st.st_mode))  type = 'd';
            else if (S_ISCHR(st.st_mode))  type = 'c';

            printf("%c %d\t%s\n", type, (int)st.st_size, ent->d_name);
        } else {
            puts(ent->d_name);
        }
    }
    closedir(d);
}

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "l")) != -1) {
        switch (opt) {
        case 'l':
            opt_long = 1;
            break;
        default:
            fprintf(stderr, "Usage: ls [-l] [path ...]\n");
            return 1;
        }
    }

    if (optind >= argc) {
        ls_dir(".");
    } else {
        for (int i = optind; i < argc; i++) {
            if (argc - optind > 1)
                printf("%s:\n", argv[i]);
            ls_dir(argv[i]);
        }
    }
    return 0;
}
