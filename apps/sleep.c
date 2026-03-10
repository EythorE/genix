/*
 * sleep — delay for a specified number of seconds
 *
 * Usage: sleep <seconds>
 *
 * Uses busy-wait with SYS_TIME (no sleep() syscall yet).
 */
#include <stdio.h>
#include <stdlib.h>

extern int time(void *);

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: sleep <seconds>\n");
        return 1;
    }

    int secs = atoi(argv[1]);
    if (secs <= 0)
        return 0;

    /* SYS_TIME returns ticks (100 Hz on workbench, 60 Hz on Mega Drive).
     * We don't know the exact rate, so approximate: busy-wait by
     * checking elapsed ticks. Assume ~100 ticks/sec as a rough target. */
    int start = time(NULL);
    /* Use ~100 ticks per second as approximation */
    int target = start + secs * 100;
    while (time(NULL) < target)
        ;
    return 0;
}
