/*
 * clear — clear the terminal screen
 */
#include <unistd.h>

int main(void)
{
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    return 0;
}
