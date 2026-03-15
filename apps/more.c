/*
 * more — page through text one screenful at a time
 *
 * Usage: more [file ...]
 *
 * Hardcoded for 40×28 VDP (Mega Drive) / any terminal.
 * Space = next page, Enter = next line, q = quit.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#define LINES 27   /* 28 rows minus 1 for "--more--" prompt */
#define COLS  80

static struct termios orig_tios;
static int tty_fd = -1;

static void raw_on(void)
{
    tty_fd = open("/dev/tty", O_RDONLY);
    if (tty_fd < 0)
        tty_fd = STDERR_FILENO;
    struct termios raw;
    tcgetattr(tty_fd, &orig_tios);
    raw = orig_tios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(tty_fd, TCSANOW, &raw);
}

static void raw_off(void)
{
    tcsetattr(tty_fd, TCSANOW, &orig_tios);
    if (tty_fd != STDERR_FILENO)
        close(tty_fd);
}

/* Returns: 0 = next page, 1 = next line, -1 = quit */
static int prompt(void)
{
    const char *msg = "--more--";
    write(STDERR_FILENO, msg, 8);

    char c;
    while (read(tty_fd, &c, 1) == 1) {
        /* Clear the prompt */
        write(STDERR_FILENO, "\r        \r", 10);
        if (c == ' ')
            return 0;
        if (c == '\n' || c == '\r')
            return 1;
        if (c == 'q' || c == 'Q')
            return -1;
    }
    return -1;
}

static int more_file(FILE *f)
{
    int line_count = 0;
    int c;

    while ((c = fgetc(f)) != EOF) {
        fputc(c, stdout);
        if (c == '\n') {
            line_count++;
            if (line_count >= LINES) {
                fflush(stdout);
                int action = prompt();
                if (action < 0)
                    return 1;
                line_count = (action == 0) ? 0 : LINES - 1;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (!isatty(STDOUT_FILENO)) {
        /* Not a terminal — just cat through */
        if (argc < 2) {
            int c;
            while ((c = getchar()) != EOF)
                putchar(c);
        } else {
            for (int i = 1; i < argc; i++) {
                FILE *f = fopen(argv[i], "r");
                if (!f) {
                    fprintf(stderr, "more: cannot open '%s'\n", argv[i]);
                    return 1;
                }
                int c;
                while ((c = fgetc(f)) != EOF)
                    putchar(c);
                fclose(f);
            }
        }
        return 0;
    }

    raw_on();

    int ret = 0;
    if (argc < 2) {
        ret = more_file(stdin);
    } else {
        for (int i = 1; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) {
                fprintf(stderr, "more: cannot open '%s'\n", argv[i]);
                ret = 1;
                continue;
            }
            if (argc > 2)
                printf("=== %s ===\n", argv[i]);
            if (more_file(f) != 0) {
                fclose(f);
                break;
            }
            fclose(f);
        }
    }

    raw_off();
    return ret;
}
