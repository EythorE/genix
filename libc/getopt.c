/*
 * getopt — POSIX option parsing for Genix user programs.
 *
 * Supports short options (-a, -b, -abc, -o value, -ovalue).
 * No long options (getopt_long) — add later if needed.
 */

#include <string.h>

extern int write(int fd, const void *buf, int count);
extern unsigned int strlen(const char *s);

char *optarg;
int optind = 1;
int opterr = 1;
int optopt;

static int optpos = 0;  /* position within current argv element */

int getopt(int argc, char *const argv[], const char *optstring)
{
    const char *p;

    optarg = (char *)0;

    if (optind >= argc)
        return -1;

    /* Skip non-option arguments */
    if (argv[optind] == (char *)0 || argv[optind][0] != '-' || argv[optind][1] == '\0')
        return -1;

    /* "--" ends option processing */
    if (argv[optind][0] == '-' && argv[optind][1] == '-' && argv[optind][2] == '\0') {
        optind++;
        return -1;
    }

    if (optpos == 0)
        optpos = 1;

    optopt = argv[optind][optpos];

    /* Look up the option character in optstring */
    p = strchr(optstring, optopt);
    if (!p || optopt == ':') {
        /* Unknown option */
        if (opterr) {
            const char *prog = argv[0] ? argv[0] : "?";
            write(2, prog, strlen(prog));
            write(2, ": unknown option -", 18);
            write(2, &argv[optind][optpos], 1);
            write(2, "\n", 1);
        }
        /* Advance */
        optpos++;
        if (argv[optind][optpos] == '\0') {
            optind++;
            optpos = 0;
        }
        return '?';
    }

    /* Check if option takes an argument */
    if (p[1] == ':') {
        /* Option requires an argument */
        if (argv[optind][optpos + 1] != '\0') {
            /* Argument is rest of this argv element: -oVALUE */
            optarg = &argv[optind][optpos + 1];
        } else if (optind + 1 < argc) {
            /* Argument is next argv element: -o VALUE */
            optarg = argv[optind + 1];
            optind++;
        } else {
            /* Missing argument */
            if (opterr) {
                const char *prog = argv[0] ? argv[0] : "?";
                write(2, prog, strlen(prog));
                write(2, ": option -", 10);
                write(2, &argv[optind][optpos], 1);
                write(2, " requires an argument\n", 22);
            }
            optind++;
            optpos = 0;
            return optstring[0] == ':' ? ':' : '?';
        }
        optind++;
        optpos = 0;
    } else {
        /* No argument — advance within grouped options */
        optpos++;
        if (argv[optind][optpos] == '\0') {
            optind++;
            optpos = 0;
        }
    }

    return optopt;
}
