/*
 * find — search for files in directory hierarchy
 *
 * Usage: find [path] [-name pattern] [-type f|d]
 *
 * Minimal implementation: supports -name (glob with * and ?)
 * and -type f/d. Prints matching paths.
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *opt_name;
static char opt_type;   /* 0 = any, 'f' = file, 'd' = dir */

/* Simple glob match supporting * and ? */
static int glob_match(const char *pattern, const char *str)
{
    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0')
                return 1;
            while (*str) {
                if (glob_match(pattern, str))
                    return 1;
                str++;
            }
            return 0;
        }
        if (*pattern == '?') {
            pattern++;
            str++;
            continue;
        }
        if (*pattern != *str)
            return 0;
        pattern++;
        str++;
    }
    while (*pattern == '*')
        pattern++;
    return (*pattern == '\0' && *str == '\0');
}

static void find_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "find: cannot open '%s'\n", path);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 ||
            strcmp(ent->d_name, "..") == 0)
            continue;

        /* Build full path */
        char child[256];
        int plen = strlen(path);
        if (plen + 1 + strlen(ent->d_name) + 1 > 256)
            continue;
        strcpy(child, path);
        if (plen > 0 && child[plen - 1] != '/') {
            child[plen] = '/';
            child[plen + 1] = '\0';
        }
        strcat(child, ent->d_name);

        struct stat st;
        if (stat(child, &st) < 0)
            continue;

        int is_dir = S_ISDIR(st.st_mode);

        /* Check -type filter */
        if (opt_type == 'f' && is_dir)
            goto recurse;
        if (opt_type == 'd' && !is_dir)
            continue;

        /* Check -name filter */
        if (opt_name) {
            if (!glob_match(opt_name, ent->d_name))
                goto recurse;
        }

        puts(child);

    recurse:
        if (is_dir)
            find_dir(child);
    }
    closedir(d);
}

int main(int argc, char **argv)
{
    const char *start_path = ".";
    opt_name = NULL;
    opt_type = 0;

    int i = 1;

    /* First non-option arg is the path */
    if (i < argc && argv[i][0] != '-')
        start_path = argv[i++];

    /* Parse options */
    while (i < argc) {
        if (strcmp(argv[i], "-name") == 0 && i + 1 < argc) {
            opt_name = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "-type") == 0 && i + 1 < argc) {
            opt_type = argv[i + 1][0];
            i += 2;
        } else {
            fprintf(stderr, "find: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    /* Print the start path itself if it matches */
    struct stat st;
    if (stat(start_path, &st) < 0) {
        fprintf(stderr, "find: '%s': no such file or directory\n", start_path);
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (opt_type != 'f' && !opt_name)
            puts(start_path);
        find_dir(start_path);
    } else {
        if (opt_type != 'd') {
            if (!opt_name || glob_match(opt_name, start_path))
                puts(start_path);
        }
    }

    return 0;
}
