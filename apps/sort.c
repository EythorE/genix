/*
 * sort — sort lines of text
 *
 * Usage: sort [-r] [-n] [file ...]
 *
 * Reads all input into memory, sorts with qsort, outputs.
 * -r = reverse, -n = numeric sort.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int opt_reverse;
static int opt_numeric;

static int cmp_lines(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    int r;

    if (opt_numeric) {
        long na = atol(sa);
        long nb = atol(sb);
        r = (na > nb) - (na < nb);
    } else {
        r = strcmp(sa, sb);
    }

    return opt_reverse ? -r : r;
}

/* Read all lines from f into a growable array.
 * Returns number of lines, fills *lines_out and *buf_out. */
static int read_lines(FILE *f, char ***lines_out)
{
    /* Read entire input into a single buffer */
    int cap = 1024;
    int len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return 0;

    int c;
    while ((c = fgetc(f)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return 0; }
            buf = nb;
        }
        buf[len++] = c;
    }
    buf[len] = '\0';

    /* Count lines */
    int nlines = 0;
    for (int i = 0; i < len; i++)
        if (buf[i] == '\n')
            nlines++;
    if (len > 0 && buf[len - 1] != '\n')
        nlines++;

    char **lines = malloc(nlines * sizeof(char *));
    if (!lines) { free(buf); return 0; }

    /* Split into lines */
    int li = 0;
    char *p = buf;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            buf[i] = '\0';
            lines[li++] = p;
            p = buf + i + 1;
        }
    }
    if (p < buf + len)
        lines[li++] = p;

    *lines_out = lines;
    return li;
}

int main(int argc, char **argv)
{
    opt_reverse = 0;
    opt_numeric = 0;

    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        for (const char *p = argv[i] + 1; *p; p++) {
            switch (*p) {
            case 'r': opt_reverse = 1; break;
            case 'n': opt_numeric = 1; break;
            default:
                fprintf(stderr, "sort: unknown option '-%c'\n", *p);
                return 1;
            }
        }
        i++;
    }

    /* Read input */
    char **lines;
    int nlines;

    if (i >= argc) {
        nlines = read_lines(stdin, &lines);
    } else {
        /* Concatenate all files */
        nlines = 0;
        lines = NULL;
        for (; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) {
                fprintf(stderr, "sort: cannot open '%s'\n", argv[i]);
                return 1;
            }
            char **flines;
            int fn = read_lines(f, &flines);
            fclose(f);

            if (fn > 0) {
                char **merged = realloc(lines, (nlines + fn) * sizeof(char *));
                if (!merged) { fprintf(stderr, "sort: out of memory\n"); return 1; }
                lines = merged;
                memcpy(lines + nlines, flines, fn * sizeof(char *));
                nlines += fn;
                free(flines);
            }
        }
    }

    if (nlines <= 0)
        return 0;

    qsort(lines, nlines, sizeof(char *), cmp_lines);

    for (int j = 0; j < nlines; j++)
        printf("%s\n", lines[j]);

    return 0;
}
