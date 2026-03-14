/* genix_stubs.c — stub functions for dash on Genix */
#include "config.h"

#include <errno.h>
#include <sys/stat.h>
#include "output.h"
#include "error.h"

/* bgcmd/fgcmd — JOBS=0, these are unreachable but referenced in builtins.c */
int bgcmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    sh_error("no job control");
    return 1;
}

int fgcmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    sh_error("no job control");
    return 1;
}

/* getgroups — single-user system, no groups */
int getgroups(int size, unsigned int *list)
{
    (void)size; (void)list;
    return 0;
}
