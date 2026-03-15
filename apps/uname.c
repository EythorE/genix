/*
 * uname — print system information
 *
 * Usage: uname [-a]
 */
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *sysname = "Genix";
    const char *machine = "m68000";
    const char *release = "0.1";

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'a') {
        printf("%s genix %s %s %s\n", sysname, release, release, machine);
    } else {
        puts(sysname);
    }
    return 0;
}
