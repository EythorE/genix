/* Genix minicurses — minimal curses library for TUI apps
 *
 * Emits ANSI escape sequences through write(1, ...).
 * Works on both platforms: Mega Drive (VDP ANSI parser) and
 * workbench (host terminal handles escapes natively).
 *
 * No windows/subwindows, no mouse, no wide chars.
 * One stdscr, one physical screen.
 */
#ifndef _CURSES_H
#define _CURSES_H

/* Boolean constants */
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#ifndef OK
#define OK    0
#define ERR   (-1)
#endif

/* Window type */
typedef struct {
    int cur_y, cur_x;       /* cursor position */
    int max_y, max_x;       /* dimensions (set by initscr) */
    int attrs;              /* current attributes */
    int delay;              /* nodelay flag */
    int keypad_on;          /* keypad mode */
} WINDOW;

extern WINDOW *stdscr;
extern int LINES;
extern int COLS;

/* Attribute bits */
#define A_NORMAL    0x0000
#define A_BOLD      0x0001
#define A_REVERSE   0x0002
#define A_UNDERLINE 0x0004
#define A_DIM       0x0008

/* Color constants */
#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7

#define COLOR_PAIRS   8

#define COLOR_PAIR(n) (((n) & 0x7) << 8)
#define PAIR_NUMBER(a) (((a) >> 8) & 0x7)

/* Key constants */
#define KEY_UP      0x101
#define KEY_DOWN    0x102
#define KEY_LEFT    0x103
#define KEY_RIGHT   0x104
#define KEY_HOME    0x105
#define KEY_END     0x106
#define KEY_DC      0x107    /* delete character */
#define KEY_IC      0x108    /* insert character */
#define KEY_NPAGE   0x109    /* next page */
#define KEY_PPAGE   0x10A    /* prev page */
#define KEY_BACKSPACE 0x10B
#define KEY_ENTER   0x10C
#define KEY_F(n)    (0x110 + (n))

/* Initialization */
WINDOW *initscr(void);
int endwin(void);

/* Output */
int addch(int c);
int addstr(const char *s);
int mvaddch(int y, int x, int c);
int mvaddstr(int y, int x, const char *s);
int printw(const char *fmt, ...);

/* Cursor */
int move(int y, int x);
void getyx_impl(WINDOW *win, int *y, int *x);
#define getyx(win, y, x) getyx_impl(win, &(y), &(x))

/* Attributes */
int attron(int attrs);
int attroff(int attrs);
int attrset(int attrs);

/* Color */
int start_color(void);
int init_pair(int pair, int fg, int bg);
int has_colors(void);

/* Screen management */
int clear(void);
int clrtoeol(void);
int clrtobot(void);
int refresh(void);
int erase(void);

/* Input */
int getch(void);
int nodelay(WINDOW *win, int bf);
int raw(void);
int noraw(void);
int cbreak(void);
int nocbreak(void);
int noecho(void);
int echo_curses(void);
int keypad(WINDOW *win, int bf);
int curs_set(int visibility);

#endif /* _CURSES_H */
