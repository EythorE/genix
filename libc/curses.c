/*
 * curses.c — Genix minicurses implementation
 *
 * Emits ANSI escape sequences through write(1, ...).
 * Works on both platforms: workbench (host terminal) and
 * Mega Drive (VDP ANSI parser in platform.c).
 *
 * No screen buffer, no refresh diffing. Emit-as-you-go.
 * At 40x28 / 7.67 MHz this is fast enough.
 */
#include "include/curses.h"
#include "include/unistd.h"
#include "include/termios.h"
#include "include/string.h"

/* ioctl prototype */
extern int ioctl(int fd, int cmd, void *arg);

/* ============================================================
 * Internal state
 * ============================================================ */

static WINDOW stdscr_data;
WINDOW *stdscr = &stdscr_data;
int LINES = 24;
int COLS = 80;

static struct termios saved_termios;
static int curses_active = 0;

/* Color pair table: fg/bg for each pair */
static struct { int fg, bg; } color_pairs[COLOR_PAIRS];

/* ============================================================
 * Low-level output helpers
 * ============================================================ */

static void emit(const char *s, int len)
{
    write(STDOUT_FILENO, s, len);
}

static void emit_str(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    emit(s, len);
}

/* Format a small integer (0-999) into buf, return length */
static int itoa_small(int n, char *buf)
{
    int len = 0;
    if (n >= 100) buf[len++] = '0' + n / 100;
    if (n >= 10) buf[len++] = '0' + (n / 10) % 10;
    buf[len++] = '0' + n % 10;
    return len;
}

/* Emit ESC[<n1>;<n2><cmd> or ESC[<n1><cmd> */
static void emit_csi_1(int n, char cmd)
{
    char buf[12];
    int len = 0;
    buf[len++] = '\033';
    buf[len++] = '[';
    len += itoa_small(n, buf + len);
    buf[len++] = cmd;
    emit(buf, len);
}

static void emit_csi_2(int n1, int n2, char cmd)
{
    char buf[16];
    int len = 0;
    buf[len++] = '\033';
    buf[len++] = '[';
    len += itoa_small(n1, buf + len);
    buf[len++] = ';';
    len += itoa_small(n2, buf + len);
    buf[len++] = cmd;
    emit(buf, len);
}

/* Apply current attributes as SGR escape */
static void apply_attrs(void)
{
    int a = stdscr->attrs;
    int pair = PAIR_NUMBER(a);

    /* Always reset first, then apply */
    emit_str("\033[0");

    if (a & A_BOLD)
        emit_str(";1");
    if (a & A_REVERSE)
        emit_str(";7");
    if (a & A_UNDERLINE)
        emit_str(";4");
    if (a & A_DIM)
        emit_str(";2");

    if (pair > 0 && pair < COLOR_PAIRS) {
        char buf[8];
        int len = 0;
        buf[len++] = ';';
        len += itoa_small(30 + color_pairs[pair].fg, buf + len);
        emit(buf, len);
        if (color_pairs[pair].bg != COLOR_BLACK) {
            len = 0;
            buf[len++] = ';';
            len += itoa_small(40 + color_pairs[pair].bg, buf + len);
            emit(buf, len);
        }
    }

    emit("m", 1);
}

/* ============================================================
 * Initialization
 * ============================================================ */

WINDOW *initscr(void)
{
    /* Query terminal size */
    struct winsize ws;
    if (ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        LINES = ws.ws_row;
        COLS = ws.ws_col;
    }

    /* Set raw mode, no echo */
    tcgetattr(0, &saved_termios);
    struct termios t = saved_termios;
    t.c_lflag &= ~(ICANON | ECHO | ISIG);
    t.c_iflag &= ~ICRNL;
    tcsetattr(0, TCSANOW, &t);

    /* Clear screen, home cursor, reset attributes */
    emit_str("\033[0m\033[2J\033[H");

    stdscr->cur_y = 0;
    stdscr->cur_x = 0;
    stdscr->max_y = LINES;
    stdscr->max_x = COLS;
    stdscr->attrs = A_NORMAL;
    stdscr->delay = 0;
    stdscr->keypad_on = 0;

    /* Initialize color pairs to default */
    for (int i = 0; i < COLOR_PAIRS; i++) {
        color_pairs[i].fg = COLOR_WHITE;
        color_pairs[i].bg = COLOR_BLACK;
    }

    curses_active = 1;
    return stdscr;
}

int endwin(void)
{
    if (!curses_active)
        return ERR;

    /* Reset attributes, show cursor, move to bottom */
    emit_str("\033[0m\033[?25h");
    emit_csi_1(LINES, 'H');
    emit_str("\r\n");

    /* Restore terminal */
    tcsetattr(0, TCSANOW, &saved_termios);
    curses_active = 0;
    return OK;
}

/* ============================================================
 * Cursor movement
 * ============================================================ */

int move(int y, int x)
{
    if (y < 0 || y >= LINES || x < 0 || x >= COLS)
        return ERR;
    stdscr->cur_y = y;
    stdscr->cur_x = x;
    emit_csi_2(y + 1, x + 1, 'H');  /* ANSI is 1-based */
    return OK;
}

void getyx_impl(WINDOW *win, int *y, int *x)
{
    *y = win->cur_y;
    *x = win->cur_x;
}

/* ============================================================
 * Output
 * ============================================================ */

int addch(int c)
{
    char ch = (char)c;
    emit(&ch, 1);
    stdscr->cur_x++;
    if (stdscr->cur_x >= COLS) {
        stdscr->cur_x = 0;
        stdscr->cur_y++;
        if (stdscr->cur_y >= LINES)
            stdscr->cur_y = LINES - 1;
    }
    return OK;
}

int addstr(const char *s)
{
    while (*s) {
        addch(*s);
        s++;
    }
    return OK;
}

int mvaddch(int y, int x, int c)
{
    if (move(y, x) == ERR) return ERR;
    return addch(c);
}

int mvaddstr(int y, int x, const char *s)
{
    if (move(y, x) == ERR) return ERR;
    return addstr(s);
}

int printw(const char *fmt, ...)
{
    /* Minimal: just emit the format string literally.
     * Real printf formatting requires va_args + sprintf.
     * Apps that need formatting should sprintf first. */
    return addstr(fmt);
}

/* ============================================================
 * Attributes
 * ============================================================ */

int attron(int attrs)
{
    stdscr->attrs |= attrs;
    apply_attrs();
    return OK;
}

int attroff(int attrs)
{
    stdscr->attrs &= ~attrs;
    apply_attrs();
    return OK;
}

int attrset(int attrs)
{
    stdscr->attrs = attrs;
    apply_attrs();
    return OK;
}

/* ============================================================
 * Color
 * ============================================================ */

int start_color(void)
{
    return OK;
}

int has_colors(void)
{
    return TRUE;
}

int init_pair(int pair, int fg, int bg)
{
    if (pair < 0 || pair >= COLOR_PAIRS)
        return ERR;
    color_pairs[pair].fg = fg;
    color_pairs[pair].bg = bg;
    return OK;
}

/* ============================================================
 * Screen management
 * ============================================================ */

int clear(void)
{
    emit_str("\033[2J\033[H");
    stdscr->cur_y = 0;
    stdscr->cur_x = 0;
    return OK;
}

int erase(void)
{
    return clear();
}

int clrtoeol(void)
{
    emit_str("\033[K");
    return OK;
}

int clrtobot(void)
{
    emit_str("\033[J");
    return OK;
}

int refresh(void)
{
    /* No-op: we emit directly, no buffering */
    return OK;
}

int curs_set(int visibility)
{
    if (visibility)
        emit_str("\033[?25h");
    else
        emit_str("\033[?25l");
    return OK;
}

/* ============================================================
 * Input
 * ============================================================ */

int getch(void)
{
    unsigned char c;
    int n = read(STDIN_FILENO, &c, 1);
    if (n <= 0)
        return ERR;

    /* If not ESC or keypad not enabled, return raw byte */
    if (c != '\033' || !stdscr->keypad_on)
        return c;

    /* Try to read escape sequence */
    unsigned char seq[4];
    n = read(STDIN_FILENO, &seq[0], 1);
    if (n <= 0)
        return '\033';  /* bare ESC */

    if (seq[0] != '[')
        return '\033';  /* not CSI */

    n = read(STDIN_FILENO, &seq[1], 1);
    if (n <= 0)
        return '\033';

    /* Arrow keys: ESC[A, ESC[B, ESC[C, ESC[D */
    switch (seq[1]) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    }

    /* Extended sequences: ESC[1~, ESC[3~, etc. */
    if (seq[1] >= '0' && seq[1] <= '9') {
        n = read(STDIN_FILENO, &seq[2], 1);
        if (n > 0 && seq[2] == '~') {
            switch (seq[1]) {
            case '1': return KEY_HOME;
            case '3': return KEY_DC;
            case '4': return KEY_END;
            case '5': return KEY_PPAGE;
            case '6': return KEY_NPAGE;
            }
        }
    }

    return '\033';  /* unrecognized */
}

int nodelay(WINDOW *win, int bf)
{
    win->delay = bf;
    /* TODO: implement non-blocking read via VMIN/VTIME */
    return OK;
}

int raw(void)
{
    struct termios t;
    tcgetattr(0, &t);
    t.c_lflag &= ~(ICANON | ECHO | ISIG);
    t.c_iflag &= ~ICRNL;
    tcsetattr(0, TCSANOW, &t);
    return OK;
}

int noraw(void)
{
    struct termios t;
    tcgetattr(0, &t);
    t.c_lflag |= (ICANON | ECHO | ISIG);
    t.c_iflag |= ICRNL;
    tcsetattr(0, TCSANOW, &t);
    return OK;
}

int cbreak(void)
{
    struct termios t;
    tcgetattr(0, &t);
    t.c_lflag &= ~ICANON;
    tcsetattr(0, TCSANOW, &t);
    return OK;
}

int nocbreak(void)
{
    struct termios t;
    tcgetattr(0, &t);
    t.c_lflag |= ICANON;
    tcsetattr(0, TCSANOW, &t);
    return OK;
}

int noecho(void)
{
    struct termios t;
    tcgetattr(0, &t);
    t.c_lflag &= ~ECHO;
    tcsetattr(0, TCSANOW, &t);
    return OK;
}

int echo_curses(void)
{
    struct termios t;
    tcgetattr(0, &t);
    t.c_lflag |= ECHO;
    tcsetattr(0, TCSANOW, &t);
    return OK;
}

int keypad(WINDOW *win, int bf)
{
    win->keypad_on = bf;
    return OK;
}
