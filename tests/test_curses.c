/*
 * Unit tests for Genix minicurses library
 *
 * The curses library emits ANSI escape sequences through write(1, ...).
 * We mock write(), tcgetattr(), tcsetattr(), and ioctl() to capture
 * and verify output without a real terminal.
 *
 * Strategy: define the Genix header guards and provide all types/constants
 * ourselves, then include curses.c directly. Mock functions (write, read,
 * tcgetattr, tcsetattr, ioctl) are defined here and linked instead of
 * the real ones.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testutil.h"

/* ---- Provide Genix unistd.h constants (guard the real header) ---- */
#define _UNISTD_H
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Declare read/write with Genix signatures (int count, not size_t) */
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);

/* ---- Provide Genix termios.h types and constants (guard the real header) ---- */
#define _TERMIOS_H
#define ICRNL   0x0100
#define ISIG    0x0001
#define ICANON  0x0002
#define ECHO    0x0008
#define TCSANOW   0
#define TIOCGWINSZ  0x5413
#define NCCS    12

struct termios {
    unsigned short c_iflag;
    unsigned short c_oflag;
    unsigned short c_cflag;
    unsigned short c_lflag;
    unsigned char  c_cc[NCCS];
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int tcgetattr(int fd, struct termios *tp);
int tcsetattr(int fd, int action, const struct termios *tp);

/* ---- Provide Genix string.h guard (use host string.h already included) ---- */
#define _STRING_H

/* ---- Output capture buffer ---- */
static char out_buf[4096];
static int out_len;

static void reset_output(void)
{
    memset(out_buf, 0, sizeof(out_buf));
    out_len = 0;
}

/* Check if out_buf contains a substring */
static int output_contains(const char *needle)
{
    int nlen = (int)strlen(needle);
    for (int i = 0; i <= out_len - nlen; i++) {
        if (memcmp(out_buf + i, needle, nlen) == 0)
            return 1;
    }
    return 0;
}

/* ---- Mock system calls ---- */

int write(int fd, const void *buf, int count)
{
    if (fd == 1 && out_len + count <= (int)sizeof(out_buf)) {
        memcpy(out_buf + out_len, buf, count);
        out_len += count;
    }
    return count;
}

int read(int fd, void *buf, int count)
{
    (void)fd; (void)buf; (void)count;
    return 0;
}

int tcgetattr(int fd, struct termios *tp)
{
    (void)fd;
    memset(tp, 0, sizeof(*tp));
    return 0;
}

int tcsetattr(int fd, int action, const struct termios *tp)
{
    (void)fd; (void)action; (void)tp;
    return 0;
}

int ioctl(int fd, int cmd, void *arg)
{
    if (cmd == TIOCGWINSZ) {
        struct winsize *ws = (struct winsize *)arg;
        ws->ws_row = 25;
        ws->ws_col = 80;
    }
    (void)fd;
    return 0;
}

/* ---- Include the curses implementation directly ---- */
#include "../libc/include/curses.h"
#include "../libc/curses.c"

/* ================================================================
 * Tests
 * ================================================================ */

static void test_initscr(void)
{
    reset_output();
    WINDOW *w = initscr();

    ASSERT_NOT_NULL(w);
    ASSERT_EQ(LINES, 25);   /* from our ioctl mock */
    ASSERT_EQ(COLS, 80);

    /* initscr emits: ESC[0m ESC[2J ESC[H */
    ASSERT(output_contains("\033[2J"));
    ASSERT(output_contains("\033[H"));

    /* stdscr initialized */
    ASSERT_EQ(stdscr->cur_x, 0);
    ASSERT_EQ(stdscr->cur_y, 0);
    ASSERT_EQ(stdscr->attrs, A_NORMAL);

    endwin();
}

static void test_move(void)
{
    reset_output();
    initscr();
    reset_output();  /* clear initscr output */

    int ret = move(5, 10);
    ASSERT_EQ(ret, OK);
    ASSERT_EQ(stdscr->cur_y, 5);
    ASSERT_EQ(stdscr->cur_x, 10);

    /* ANSI is 1-based: row 6, col 11 -> ESC[6;11H */
    ASSERT(output_contains("\033[6;11H"));

    endwin();
}

static void test_addch(void)
{
    reset_output();
    initscr();
    reset_output();

    int old_x = stdscr->cur_x;
    int ret = addch('A');
    ASSERT_EQ(ret, OK);
    ASSERT_EQ(stdscr->cur_x, old_x + 1);

    /* 'A' should be in the output */
    ASSERT(output_contains("A"));

    endwin();
}

static void test_addstr(void)
{
    reset_output();
    initscr();
    reset_output();

    int ret = addstr("hello");
    ASSERT_EQ(ret, OK);
    ASSERT(output_contains("hello"));
    ASSERT_EQ(stdscr->cur_x, 5);

    endwin();
}

static void test_mvaddstr(void)
{
    reset_output();
    initscr();
    reset_output();

    int ret = mvaddstr(3, 7, "test");
    ASSERT_EQ(ret, OK);

    /* move(3,7) -> ESC[4;8H (1-based) */
    ASSERT(output_contains("\033[4;8H"));
    ASSERT(output_contains("test"));

    endwin();
}

static void test_clear(void)
{
    reset_output();
    initscr();
    reset_output();

    clear();
    ASSERT(output_contains("\033[2J"));
    ASSERT(output_contains("\033[H"));
    ASSERT_EQ(stdscr->cur_x, 0);
    ASSERT_EQ(stdscr->cur_y, 0);

    endwin();
}

static void test_clrtoeol(void)
{
    reset_output();
    initscr();
    reset_output();

    clrtoeol();
    ASSERT(output_contains("\033[K"));

    endwin();
}

static void test_attron_bold(void)
{
    reset_output();
    initscr();
    reset_output();

    attron(A_BOLD);
    ASSERT(stdscr->attrs & A_BOLD);
    /* apply_attrs emits ESC[0;1m for bold */
    ASSERT(output_contains("\033[0;1m"));

    endwin();
}

static void test_attroff(void)
{
    reset_output();
    initscr();
    attron(A_BOLD);
    reset_output();

    attroff(A_BOLD);
    ASSERT_EQ(stdscr->attrs & A_BOLD, 0);
    /* After clearing bold, should emit ESC[0m (reset only, no ;1) */
    ASSERT(output_contains("\033[0m"));

    endwin();
}

static void test_color_pair(void)
{
    reset_output();
    initscr();
    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK);
    reset_output();

    attron(COLOR_PAIR(1));
    /* COLOR_RED = 1, so SGR fg = 30 + 1 = 31 -> ";31" in the output */
    ASSERT(output_contains(";31"));

    endwin();
}

static void test_curs_set(void)
{
    reset_output();
    initscr();
    reset_output();

    curs_set(0);
    ASSERT(output_contains("\033[?25l"));

    reset_output();
    curs_set(1);
    ASSERT(output_contains("\033[?25h"));

    endwin();
}

static void test_endwin(void)
{
    reset_output();
    initscr();
    reset_output();

    int ret = endwin();
    ASSERT_EQ(ret, OK);

    /* endwin emits: ESC[0m (reset attrs), ESC[?25h (show cursor) */
    ASSERT(output_contains("\033[0m"));
    ASSERT(output_contains("\033[?25h"));
}

/* ---- Main ---- */
int main(void)
{
    printf("test_curses:\n");

    RUN_TEST(test_initscr);
    RUN_TEST(test_move);
    RUN_TEST(test_addch);
    RUN_TEST(test_addstr);
    RUN_TEST(test_mvaddstr);
    RUN_TEST(test_clear);
    RUN_TEST(test_clrtoeol);
    RUN_TEST(test_attron_bold);
    RUN_TEST(test_attroff);
    RUN_TEST(test_color_pair);
    RUN_TEST(test_curs_set);
    RUN_TEST(test_endwin);

    TEST_REPORT();
}
