/*
 * env — print or set environment variables
 *
 * Usage: env               — print all environment variables
 *        env NAME=VALUE    — set a variable (for this process)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;

int main(int argc, char **argv)
{
    if (argc <= 1) {
        /* Print all environment variables */
        if (environ) {
            int i;
            for (i = 0; environ[i]; i++) {
                fputs(environ[i], stdout);
                fputc('\n', stdout);
            }
        }
        return 0;
    }

    /* Set variables */
    int i;
    for (i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            /* Split at '=' */
            *eq = '\0';
            setenv(argv[i], eq + 1, 1);
            *eq = '=';  /* restore */
        } else {
            /* Just a name — print its value */
            char *val = getenv(argv[i]);
            if (val) {
                fputs(val, stdout);
                fputc('\n', stdout);
            }
        }
    }
    return 0;
}
