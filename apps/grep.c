/*
 * grep — search files for a pattern
 *
 * Usage: grep [-inv] pattern [file ...]
 *   -i  ignore case
 *   -n  print line numbers
 *   -v  invert match (print non-matching lines)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <regex.h>

static int opt_invert = 0;
static int opt_number = 0;
static int opt_icase = 0;
static int multi_file = 0;

static char lc_buf[1024];

static const char *to_lower(const char *s)
{
    int i;
    for (i = 0; s[i] && i < 1022; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        lc_buf[i] = c;
    }
    lc_buf[i] = '\0';
    return lc_buf;
}

static int grep_file(const char *path, FILE *f, regex_t *re)
{
    char line[1024];
    int lineno = 0;
    int found = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        /* Remove trailing newline for matching */
        int len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        const char *target = opt_icase ? to_lower(line) : line;
        int matched = (regexec(re, target, NULL, NULL) == 0);
        if (opt_invert) matched = !matched;

        if (matched) {
            found = 1;
            if (multi_file)
                fprintf(stdout, "%s:", path);
            if (opt_number)
                fprintf(stdout, "%d:", lineno);
            fputs(line, stdout);
            fputc('\n', stdout);
        }
    }
    return found;
}

int main(int argc, char **argv)
{
    int c;
    regex_t re;

    while ((c = getopt(argc, argv, "inv")) != -1) {
        switch (c) {
        case 'i': opt_icase = 1; break;
        case 'n': opt_number = 1; break;
        case 'v': opt_invert = 1; break;
        default:
            fputs("Usage: grep [-inv] pattern [file ...]\n", stderr);
            return 2;
        }
    }

    if (optind >= argc) {
        fputs("Usage: grep [-inv] pattern [file ...]\n", stderr);
        return 2;
    }

    const char *pattern = argv[optind++];
    /* If case-insensitive, lowercase the pattern too */
    char pat_buf[256];
    if (opt_icase) {
        int i;
        for (i = 0; pattern[i] && i < 254; i++) {
            char ch = pattern[i];
            if (ch >= 'A' && ch <= 'Z') ch += 32;
            pat_buf[i] = ch;
        }
        pat_buf[i] = '\0';
        pattern = pat_buf;
    }

    if (regcomp(&re, pattern) != 0) {
        fputs("grep: bad pattern\n", stderr);
        return 2;
    }

    int found = 0;
    multi_file = (argc - optind > 1);

    if (optind >= argc) {
        /* Read from stdin */
        found = grep_file("(stdin)", stdin, &re);
    } else {
        while (optind < argc) {
            FILE *f = fopen(argv[optind], "r");
            if (!f) {
                fprintf(stderr, "grep: %s: No such file\n", argv[optind]);
                optind++;
                continue;
            }
            if (grep_file(argv[optind], f, &re))
                found = 1;
            fclose(f);
            optind++;
        }
    }

    regfree(&re);
    return found ? 0 : 1;
}
