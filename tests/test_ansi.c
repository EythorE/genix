/*
 * Unit tests for the ANSI escape sequence parser
 *
 * The parser lives in pal/megadrive/platform.c but is static.
 * We re-implement the parser here with mock VDP functions that
 * record their calls, allowing us to verify correct behavior
 * on the host without VDP hardware.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* ============================================================
 * Console geometry (matches platform.c)
 * ============================================================ */
#define COLS 40
#define ROWS 28

/* ============================================================
 * Mock call recording
 * ============================================================ */
#define MAX_CALLS 64

/* plot_char log */
static struct {
    int y, x;
    int c;
} plot_log[MAX_CALLS];
static int plot_count;

/* clear_across log */
static struct {
    int y, x, num;
} clra_log[MAX_CALLS];
static int clra_count;

/* clear_lines log */
static struct {
    int y, num;
} clrl_log[MAX_CALLS];
static int clrl_count;

/* scroll_up call count */
static int scroll_count;

/* cursor_on log */
static struct {
    int y, x;
} curon_log[MAX_CALLS];
static int curon_count;

/* cursor_off call count */
static int curoff_count;

/* tty_inproc log */
static uint8_t inproc_buf[MAX_CALLS];
static int inproc_count;

/* ============================================================
 * Mock functions
 * ============================================================ */
static void plot_char(int y, int x, int c)
{
    if (plot_count < MAX_CALLS) {
        plot_log[plot_count].y = y;
        plot_log[plot_count].x = x;
        plot_log[plot_count].c = c;
        plot_count++;
    }
}

static void clear_across(int y, int x, int num)
{
    if (clra_count < MAX_CALLS) {
        clra_log[clra_count].y = y;
        clra_log[clra_count].x = x;
        clra_log[clra_count].num = num;
        clra_count++;
    }
}

static void clear_lines(int y, int num)
{
    if (clrl_count < MAX_CALLS) {
        clrl_log[clrl_count].y = y;
        clrl_log[clrl_count].num = num;
        clrl_count++;
    }
}

static void scroll_up(void)
{
    scroll_count++;
}

static void cursor_on(int y, int x)
{
    if (curon_count < MAX_CALLS) {
        curon_log[curon_count].y = y;
        curon_log[curon_count].x = x;
        curon_count++;
    }
}

static void cursor_off(void)
{
    curoff_count++;
}

static void tty_inproc(int minor, uint8_t c)
{
    (void)minor;
    if (inproc_count < MAX_CALLS)
        inproc_buf[inproc_count++] = c;
}

/* ============================================================
 * Parser state (copied from platform.c)
 * ============================================================ */
#define ESC_NORMAL  0
#define ESC_SEEN    1
#define ESC_CSI     2
#define ESC_CSI_Q   3

static int cursor_x = 0;
static int cursor_y = 0;

static uint8_t  esc_state = ESC_NORMAL;
static uint8_t  esc_params[4];
static uint8_t  esc_nparam = 0;
static uint8_t  esc_partial = 0;
static uint8_t  esc_has_digit = 0;
static uint16_t current_attr = 0;
static uint8_t  saved_x = 0, saved_y = 0;
static uint8_t  cursor_visible = 1;

static int esc_param(int idx, int def)
{
    return (idx < esc_nparam && esc_params[idx] > 0) ?
           esc_params[idx] : def;
}

static void esc_reset(void)
{
    esc_state = ESC_NORMAL;
    esc_nparam = 0;
    esc_partial = 0;
    esc_has_digit = 0;
}

static void cursor_clamp(void)
{
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_x >= COLS) cursor_x = COLS - 1;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_y >= ROWS) cursor_y = ROWS - 1;
}

/* Handle SGR (Set Graphic Rendition) — ESC[...m */
static void handle_sgr(void)
{
    if (esc_nparam == 0) {
        current_attr = 0;
        return;
    }
    for (int i = 0; i < esc_nparam; i++) {
        int p = esc_params[i];
        switch (p) {
        case 0:  current_attr = 0; break;
        case 1:  current_attr = (3u << 13); break;
        case 7:  break;
        case 22: current_attr = 0; break;
        case 27: break;
        case 30: case 31: case 32: case 33:
        case 34: case 35: case 36: case 37:
                 break;
        case 39: current_attr = 0; break;
        case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47:
                 break;
        case 49: break;
        case 90: case 91: case 92: case 93:
        case 94: case 95: case 96: case 97:
                 current_attr = (3u << 13); break;
        }
    }
}

/* Handle CSI sequence final character */
static void handle_csi(char cmd)
{
    int p1, p2;

    switch (cmd) {
    case 'A':
        cursor_y -= esc_param(0, 1);
        cursor_clamp();
        break;
    case 'B':
        cursor_y += esc_param(0, 1);
        cursor_clamp();
        break;
    case 'C':
        cursor_x += esc_param(0, 1);
        cursor_clamp();
        break;
    case 'D':
        cursor_x -= esc_param(0, 1);
        cursor_clamp();
        break;
    case 'H':
    case 'f':
        p1 = esc_param(0, 1);
        p2 = esc_param(1, 1);
        cursor_y = p1 - 1;
        cursor_x = p2 - 1;
        cursor_clamp();
        break;
    case 'J':
        p1 = esc_param(0, 0);
        if (p1 == 0) {
            clear_across(cursor_y, cursor_x, COLS - cursor_x);
            if (cursor_y + 1 < ROWS)
                clear_lines(cursor_y + 1, ROWS - cursor_y - 1);
        } else if (p1 == 1) {
            if (cursor_y > 0)
                clear_lines(0, cursor_y);
            clear_across(cursor_y, 0, cursor_x + 1);
        } else if (p1 == 2) {
            clear_lines(0, ROWS);
            cursor_x = 0;
            cursor_y = 0;
        }
        break;
    case 'K':
        p1 = esc_param(0, 0);
        if (p1 == 0) {
            clear_across(cursor_y, cursor_x, COLS - cursor_x);
        } else if (p1 == 1) {
            clear_across(cursor_y, 0, cursor_x + 1);
        } else if (p1 == 2) {
            clear_across(cursor_y, 0, COLS);
        }
        break;
    case 'm':
        handle_sgr();
        break;
    case 's':
        saved_x = cursor_x;
        saved_y = cursor_y;
        break;
    case 'u':
        cursor_x = saved_x;
        cursor_y = saved_y;
        cursor_clamp();
        break;
    case 'n':
        if (esc_param(0, 0) == 6) {
            tty_inproc(0, '\033');
            tty_inproc(0, '[');
            int row = cursor_y + 1;
            int col = cursor_x + 1;
            if (row >= 10) tty_inproc(0, '0' + row / 10);
            tty_inproc(0, '0' + row % 10);
            tty_inproc(0, ';');
            if (col >= 10) tty_inproc(0, '0' + col / 10);
            tty_inproc(0, '0' + col % 10);
            tty_inproc(0, 'R');
        }
        break;
    }
}

/* Handle CSI? (private mode) sequence */
static void handle_csi_private(char cmd)
{
    if (cmd == 'h' || cmd == 'l') {
        int p = esc_param(0, 0);
        if (p == 25) {
            cursor_visible = (cmd == 'h') ? 1 : 0;
            if (cursor_visible)
                cursor_on(cursor_y, cursor_x);
            else
                cursor_off();
        }
    }
}

/* Output a regular character (handles control chars and printables) */
static void putc_normal(char c)
{
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            plot_char(cursor_y, cursor_x, 0);
        }
    } else {
        plot_char(cursor_y, cursor_x, (uint16_t)(unsigned char)c | current_attr);
        cursor_x++;
    }

    if (cursor_x >= COLS) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= ROWS) {
        scroll_up();
        clear_lines(ROWS - 1, 1);
        cursor_y = ROWS - 1;
    }
}

static void pal_console_putc(char c)
{
    switch (esc_state) {
    case ESC_NORMAL:
        if (c == '\033') {
            esc_state = ESC_SEEN;
        } else {
            putc_normal(c);
        }
        break;

    case ESC_SEEN:
        if (c == '[') {
            esc_state = ESC_CSI;
            esc_nparam = 0;
            esc_partial = 0;
            esc_has_digit = 0;
            for (int i = 0; i < 4; i++)
                esc_params[i] = 0;
        } else {
            esc_state = ESC_NORMAL;
            putc_normal(c);
        }
        break;

    case ESC_CSI:
        if (c == '?') {
            esc_state = ESC_CSI_Q;
        } else if (c >= '0' && c <= '9') {
            esc_partial = esc_partial * 10 + (c - '0');
            esc_has_digit = 1;
        } else if (c == ';') {
            if (esc_nparam < 4)
                esc_params[esc_nparam] = esc_partial;
            esc_nparam++;
            esc_partial = 0;
            esc_has_digit = 0;
        } else if (c >= 0x40 && c <= 0x7E) {
            if (esc_has_digit || esc_nparam > 0) {
                if (esc_nparam < 4)
                    esc_params[esc_nparam] = esc_partial;
                esc_nparam++;
            }
            handle_csi(c);
            esc_reset();
        } else {
            esc_reset();
        }
        break;

    case ESC_CSI_Q:
        if (c >= '0' && c <= '9') {
            esc_partial = esc_partial * 10 + (c - '0');
            esc_has_digit = 1;
        } else if (c >= 0x40 && c <= 0x7E) {
            if (esc_has_digit) {
                if (esc_nparam < 4)
                    esc_params[esc_nparam] = esc_partial;
                esc_nparam++;
            }
            handle_csi_private(c);
            esc_reset();
        } else {
            esc_reset();
        }
        break;
    }
}

/* ============================================================
 * Test helpers
 * ============================================================ */

/* Reset all parser and mock state */
static void reset_all(void)
{
    cursor_x = 0;
    cursor_y = 0;
    esc_state = ESC_NORMAL;
    memset(esc_params, 0, sizeof(esc_params));
    esc_nparam = 0;
    esc_partial = 0;
    esc_has_digit = 0;
    current_attr = 0;
    saved_x = 0;
    saved_y = 0;
    cursor_visible = 1;

    plot_count = 0;
    clra_count = 0;
    clrl_count = 0;
    scroll_count = 0;
    curon_count = 0;
    curoff_count = 0;
    inproc_count = 0;
    memset(inproc_buf, 0, sizeof(inproc_buf));
}

/* Feed a string through the parser one character at a time */
static void feed(const char *s)
{
    while (*s)
        pal_console_putc(*s++);
}

/* ============================================================
 * Tests
 * ============================================================ */

static void test_plain_text(void)
{
    reset_all();
    feed("ABC");
    ASSERT_EQ(cursor_x, 3);
    ASSERT_EQ(cursor_y, 0);
    ASSERT_EQ(plot_count, 3);
    ASSERT_EQ(plot_log[0].c, 'A');
    ASSERT_EQ(plot_log[1].c, 'B');
    ASSERT_EQ(plot_log[2].c, 'C');
    ASSERT_EQ(plot_log[0].x, 0);
    ASSERT_EQ(plot_log[1].x, 1);
    ASSERT_EQ(plot_log[2].x, 2);
}

static void test_newline(void)
{
    reset_all();
    feed("AB\nC");
    ASSERT_EQ(cursor_x, 1);
    ASSERT_EQ(cursor_y, 1);
    /* 'A' at (0,0), 'B' at (0,1), newline, 'C' at (1,0) */
    ASSERT_EQ(plot_count, 3);
    ASSERT_EQ(plot_log[2].y, 1);
    ASSERT_EQ(plot_log[2].x, 0);
    ASSERT_EQ(plot_log[2].c, 'C');
}

static void test_tab(void)
{
    reset_all();
    feed("\tA");
    ASSERT_EQ(cursor_x, 9);
    ASSERT_EQ(cursor_y, 0);
    /* Tab produces no plot_char; 'A' is plotted at column 8 */
    ASSERT_EQ(plot_count, 1);
    ASSERT_EQ(plot_log[0].x, 8);
}

static void test_backspace(void)
{
    reset_all();
    feed("AB\b");
    ASSERT_EQ(cursor_x, 1);
    ASSERT_EQ(cursor_y, 0);
    /* 'A' plotted, 'B' plotted, then backspace plots 0 at (0,1) */
    ASSERT_EQ(plot_count, 3);
    ASSERT_EQ(plot_log[2].c, 0);
    ASSERT_EQ(plot_log[2].x, 1);
}

static void test_csi_cup(void)
{
    reset_all();
    /* CUP: ESC[5;10H — move to row 5, col 10 (1-based) */
    feed("\033[5;10H");
    ASSERT_EQ(cursor_y, 4);  /* 1-based → 0-based */
    ASSERT_EQ(cursor_x, 9);
}

static void test_csi_cup_default(void)
{
    reset_all();
    /* Move cursor away first */
    cursor_x = 10;
    cursor_y = 5;
    /* CUP with no params → defaults to (1,1) → (0,0) */
    feed("\033[H");
    ASSERT_EQ(cursor_y, 0);
    ASSERT_EQ(cursor_x, 0);
}

static void test_csi_cuu(void)
{
    reset_all();
    /* Position cursor at row 5 */
    cursor_y = 5;
    cursor_x = 0;
    /* CUU: ESC[2A — move up 2 */
    feed("\033[2A");
    ASSERT_EQ(cursor_y, 3);
    ASSERT_EQ(cursor_x, 0);
}

static void test_csi_cuf(void)
{
    reset_all();
    /* CUF: ESC[5C — move right 5 */
    feed("\033[5C");
    ASSERT_EQ(cursor_x, 5);
    ASSERT_EQ(cursor_y, 0);
}

static void test_csi_ed_clear_all(void)
{
    reset_all();
    cursor_x = 10;
    cursor_y = 5;
    /* ED 2: ESC[2J — clear entire screen, cursor to (0,0) */
    feed("\033[2J");
    ASSERT_EQ(cursor_x, 0);
    ASSERT_EQ(cursor_y, 0);
    ASSERT_EQ(clrl_count, 1);
    ASSERT_EQ(clrl_log[0].y, 0);
    ASSERT_EQ(clrl_log[0].num, ROWS);
}

static void test_csi_el_clear_right(void)
{
    reset_all();
    cursor_x = 5;
    cursor_y = 3;
    /* EL 0: ESC[K — clear from cursor to end of line */
    feed("\033[K");
    ASSERT_EQ(clra_count, 1);
    ASSERT_EQ(clra_log[0].y, 3);
    ASSERT_EQ(clra_log[0].x, 5);
    ASSERT_EQ(clra_log[0].num, COLS - 5);
}

static void test_csi_el_clear_line(void)
{
    reset_all();
    cursor_x = 10;
    cursor_y = 7;
    /* EL 2: ESC[2K — clear entire line */
    feed("\033[2K");
    ASSERT_EQ(clra_count, 1);
    ASSERT_EQ(clra_log[0].y, 7);
    ASSERT_EQ(clra_log[0].x, 0);
    ASSERT_EQ(clra_log[0].num, COLS);
}

static void test_sgr_bold(void)
{
    reset_all();
    feed("\033[1m");
    ASSERT_EQ(current_attr, (3u << 13));
}

static void test_sgr_reset(void)
{
    reset_all();
    feed("\033[1m");
    ASSERT_EQ(current_attr, (3u << 13));
    feed("\033[0m");
    ASSERT_EQ(current_attr, 0);
}

static void test_sgr_bright_fg(void)
{
    reset_all();
    feed("\033[91m");
    ASSERT_EQ(current_attr, (3u << 13));
}

static void test_save_restore_cursor(void)
{
    reset_all();
    cursor_x = 10;
    cursor_y = 5;
    /* Save cursor position */
    feed("\033[s");
    /* Move somewhere else */
    cursor_x = 20;
    cursor_y = 15;
    /* Restore cursor position */
    feed("\033[u");
    ASSERT_EQ(cursor_x, 10);
    ASSERT_EQ(cursor_y, 5);
}

static void test_dsr_cursor_report(void)
{
    reset_all();
    cursor_x = 4;  /* col 5 in 1-based */
    cursor_y = 2;  /* row 3 in 1-based */
    /* DSR: ESC[6n — request cursor position report */
    feed("\033[6n");
    /* Expected response: ESC [ 3 ; 5 R */
    ASSERT_EQ(inproc_count, 6);
    ASSERT_EQ(inproc_buf[0], '\033');
    ASSERT_EQ(inproc_buf[1], '[');
    ASSERT_EQ(inproc_buf[2], '3');
    ASSERT_EQ(inproc_buf[3], ';');
    ASSERT_EQ(inproc_buf[4], '5');
    ASSERT_EQ(inproc_buf[5], 'R');
}

static void test_dsr_cursor_report_two_digit(void)
{
    reset_all();
    cursor_x = 14;  /* col 15 in 1-based */
    cursor_y = 19;  /* row 20 in 1-based */
    feed("\033[6n");
    /* Expected response: ESC [ 2 0 ; 1 5 R */
    ASSERT_EQ(inproc_count, 8);
    ASSERT_EQ(inproc_buf[0], '\033');
    ASSERT_EQ(inproc_buf[1], '[');
    ASSERT_EQ(inproc_buf[2], '2');
    ASSERT_EQ(inproc_buf[3], '0');
    ASSERT_EQ(inproc_buf[4], ';');
    ASSERT_EQ(inproc_buf[5], '1');
    ASSERT_EQ(inproc_buf[6], '5');
    ASSERT_EQ(inproc_buf[7], 'R');
}

static void test_hide_show_cursor(void)
{
    reset_all();
    ASSERT_EQ(cursor_visible, 1);
    /* Hide: ESC[?25l */
    feed("\033[?25l");
    ASSERT_EQ(cursor_visible, 0);
    ASSERT_EQ(curoff_count, 1);
    /* Show: ESC[?25h */
    feed("\033[?25h");
    ASSERT_EQ(cursor_visible, 1);
    ASSERT_EQ(curon_count, 1);
}

static void test_word_wrap(void)
{
    reset_all();
    /* Fill a row with exactly 40 characters */
    for (int i = 0; i < 40; i++)
        pal_console_putc('X');
    /* Cursor should have wrapped to start of next line */
    ASSERT_EQ(cursor_x, 0);
    ASSERT_EQ(cursor_y, 1);
    ASSERT_EQ(plot_count, 40);
}

static void test_scroll(void)
{
    reset_all();
    /* Move cursor to last row */
    cursor_y = ROWS - 1;
    cursor_x = 0;
    /* Print a character that causes newline past the bottom */
    feed("X\n");
    /* Should have scrolled */
    ASSERT(scroll_count >= 1);
    ASSERT_EQ(cursor_y, ROWS - 1);
}

static void test_attr_in_plotchar(void)
{
    reset_all();
    /* Set bold attribute */
    feed("\033[1m");
    ASSERT_EQ(current_attr, (3u << 13));
    /* Now print 'A' — should include palette bits */
    feed("A");
    ASSERT_EQ(plot_count, 1);
    ASSERT_EQ(plot_log[0].c, (int)('A' | (3u << 13)));
}

static void test_carriage_return(void)
{
    reset_all();
    feed("ABC\rD");
    /* After ABC cursor_x=3, \r resets to 0, D plots at 0 */
    ASSERT_EQ(cursor_x, 1);
    ASSERT_EQ(cursor_y, 0);
    ASSERT_EQ(plot_log[3].x, 0);
    ASSERT_EQ(plot_log[3].c, 'D');
}

static void test_csi_cub(void)
{
    reset_all();
    cursor_x = 10;
    /* CUB: ESC[3D — move left 3 */
    feed("\033[3D");
    ASSERT_EQ(cursor_x, 7);
}

static void test_csi_cud(void)
{
    reset_all();
    cursor_y = 2;
    /* CUD: ESC[4B — move down 4 */
    feed("\033[4B");
    ASSERT_EQ(cursor_y, 6);
}

static void test_csi_cup_clamp(void)
{
    reset_all();
    /* CUP with out-of-bounds values should clamp */
    feed("\033[99;99H");
    ASSERT_EQ(cursor_y, ROWS - 1);
    ASSERT_EQ(cursor_x, COLS - 1);
}

static void test_csi_ed_clear_below(void)
{
    reset_all();
    cursor_x = 5;
    cursor_y = 10;
    /* ED 0: ESC[J — clear from cursor to end of screen (default) */
    feed("\033[J");
    /* Should clear rest of current line + all lines below */
    ASSERT_EQ(clra_count, 1);
    ASSERT_EQ(clra_log[0].y, 10);
    ASSERT_EQ(clra_log[0].x, 5);
    ASSERT_EQ(clra_log[0].num, COLS - 5);
    ASSERT_EQ(clrl_count, 1);
    ASSERT_EQ(clrl_log[0].y, 11);
    ASSERT_EQ(clrl_log[0].num, ROWS - 11);
}

static void test_csi_el_clear_left(void)
{
    reset_all();
    cursor_x = 15;
    cursor_y = 3;
    /* EL 1: ESC[1K — clear from start of line to cursor */
    feed("\033[1K");
    ASSERT_EQ(clra_count, 1);
    ASSERT_EQ(clra_log[0].y, 3);
    ASSERT_EQ(clra_log[0].x, 0);
    ASSERT_EQ(clra_log[0].num, 16);  /* cursor_x + 1 */
}

static void test_sgr_no_params(void)
{
    reset_all();
    feed("\033[1m");
    ASSERT_EQ(current_attr, (3u << 13));
    /* SGR with no params: ESC[m — should reset */
    feed("\033[m");
    ASSERT_EQ(current_attr, 0);
}

static void test_cuu_clamp_at_top(void)
{
    reset_all();
    cursor_y = 1;
    /* Try to move up 5 — should clamp at 0 */
    feed("\033[5A");
    ASSERT_EQ(cursor_y, 0);
}

static void test_cuf_clamp_at_right(void)
{
    reset_all();
    cursor_x = 38;
    /* Try to move right 5 — should clamp at COLS-1 */
    feed("\033[5C");
    ASSERT_EQ(cursor_x, COLS - 1);
}

static void test_backspace_at_col0(void)
{
    reset_all();
    /* Backspace at column 0 should not move */
    feed("\b");
    ASSERT_EQ(cursor_x, 0);
    ASSERT_EQ(plot_count, 0);  /* no plot_char called */
}

static void test_escape_not_csi(void)
{
    reset_all();
    /* ESC followed by something other than '[' — should output that char */
    feed("\033A");
    /* 'A' should be output normally via putc_normal */
    ASSERT_EQ(plot_count, 1);
    ASSERT_EQ(plot_log[0].c, 'A');
    ASSERT_EQ(esc_state, ESC_NORMAL);
}

/* ---- Tests added for a26fbf0+ coverage ---- */

static void test_sgr_normal_fg_colors(void)
{
    /* SGR 30-37 set normal foreground colors.
     * In this minimal parser they don't change current_attr (no palette
     * mapping for normal colors), but they should not corrupt state. */
    reset_all();
    feed("\033[31m");  /* red fg */
    /* Should not set bold/bright palette bits */
    ASSERT_EQ(current_attr, 0);
    /* State should be normal */
    ASSERT_EQ(esc_state, ESC_NORMAL);
}

static void test_sgr_normal_bg_colors(void)
{
    /* SGR 40-47 set normal background colors. */
    reset_all();
    feed("\033[42m");  /* green bg */
    ASSERT_EQ(current_attr, 0);
    ASSERT_EQ(esc_state, ESC_NORMAL);
}

static void test_sgr_bright_fg_all(void)
{
    /* SGR 90-97 all set bright foreground (palette bits 3<<13). */
    for (int code = 90; code <= 97; code++) {
        reset_all();
        char seq[16];
        snprintf(seq, sizeof(seq), "\033[%dm", code);
        feed(seq);
        ASSERT_EQ(current_attr, (3u << 13));
    }
}

static void test_sgr_multi_param(void)
{
    /* Multiple SGR params in one sequence: ESC[1;31m = bold + red */
    reset_all();
    feed("\033[1;31m");
    /* Bold should be set (param 1 processed first). Normal fg 31 is a no-op. */
    ASSERT_EQ(current_attr, (3u << 13));
}

static void test_sgr_reset_in_multi(void)
{
    /* ESC[0;1m — reset then bold */
    reset_all();
    feed("\033[97m");  /* set bright */
    ASSERT_EQ(current_attr, (3u << 13));
    feed("\033[0;1m");  /* reset then bold */
    ASSERT_EQ(current_attr, (3u << 13));
}

static void test_sgr_bold_then_reset(void)
{
    /* ESC[1;0m — bold then reset → should be reset */
    reset_all();
    feed("\033[1;0m");
    ASSERT_EQ(current_attr, 0);
}

static void test_sgr_default_fg(void)
{
    /* SGR 39 — default foreground: resets attr */
    reset_all();
    feed("\033[1m");
    ASSERT_EQ(current_attr, (3u << 13));
    feed("\033[39m");
    ASSERT_EQ(current_attr, 0);
}

static void test_sgr_normal_intensity(void)
{
    /* SGR 22 — normal intensity: clears bold */
    reset_all();
    feed("\033[1m");
    ASSERT_EQ(current_attr, (3u << 13));
    feed("\033[22m");
    ASSERT_EQ(current_attr, 0);
}

static void test_csi_cup_f_alias(void)
{
    /* 'f' is an alias for CUP (same as 'H') */
    reset_all();
    feed("\033[5;10f");
    ASSERT_EQ(cursor_y, 4);
    ASSERT_EQ(cursor_x, 9);
}

static void test_csi_ed_clear_above(void)
{
    /* ED 1: ESC[1J — clear from start of screen to cursor */
    reset_all();
    cursor_x = 5;
    cursor_y = 3;
    feed("\033[1J");
    /* Should clear lines 0 to cursor_y-1, then partial line up to cursor */
    ASSERT_EQ(clrl_count, 1);
    ASSERT_EQ(clrl_log[0].y, 0);
    ASSERT_EQ(clrl_log[0].num, 3);  /* lines 0-2 */
    ASSERT_EQ(clra_count, 1);
    ASSERT_EQ(clra_log[0].y, 3);
    ASSERT_EQ(clra_log[0].x, 0);
    ASSERT_EQ(clra_log[0].num, 6);  /* cursor_x + 1 */
}

static void test_invalid_csi_char(void)
{
    /* Non-digit, non-semicolon, non-final char in CSI → abort.
     * '=' aborts CSI, then 'H' is printed as a normal character. */
    reset_all();
    feed("\033[=H");
    ASSERT_EQ(esc_state, ESC_NORMAL);
    /* 'H' is printed at (0,0) after abort, advancing cursor_x to 1 */
    ASSERT_EQ(cursor_x, 1);
    ASSERT_EQ(cursor_y, 0);
    ASSERT_EQ(plot_count, 1);
    ASSERT_EQ(plot_log[0].c, 'H');
}

static void test_scroll_multiple_newlines(void)
{
    /* Fill screen with newlines → should scroll multiple times */
    reset_all();
    for (int i = 0; i < ROWS + 3; i++)
        pal_console_putc('\n');
    ASSERT(scroll_count >= 3);
    ASSERT_EQ(cursor_y, ROWS - 1);
}

static void test_tab_alignment(void)
{
    /* Tab at column 7 → column 8; tab at column 8 → column 16 */
    reset_all();
    cursor_x = 7;
    feed("\t");
    ASSERT_EQ(cursor_x, 8);
    feed("\t");
    ASSERT_EQ(cursor_x, 16);
}

/* ============================================================
 * Main
 * ============================================================ */
int main(void)
{
    printf("=== ANSI escape sequence parser tests ===\n");

    RUN_TEST(test_plain_text);
    RUN_TEST(test_newline);
    RUN_TEST(test_tab);
    RUN_TEST(test_backspace);
    RUN_TEST(test_csi_cup);
    RUN_TEST(test_csi_cup_default);
    RUN_TEST(test_csi_cuu);
    RUN_TEST(test_csi_cuf);
    RUN_TEST(test_csi_ed_clear_all);
    RUN_TEST(test_csi_el_clear_right);
    RUN_TEST(test_csi_el_clear_line);
    RUN_TEST(test_sgr_bold);
    RUN_TEST(test_sgr_reset);
    RUN_TEST(test_sgr_bright_fg);
    RUN_TEST(test_save_restore_cursor);
    RUN_TEST(test_dsr_cursor_report);
    RUN_TEST(test_dsr_cursor_report_two_digit);
    RUN_TEST(test_hide_show_cursor);
    RUN_TEST(test_word_wrap);
    RUN_TEST(test_scroll);
    RUN_TEST(test_attr_in_plotchar);
    RUN_TEST(test_carriage_return);
    RUN_TEST(test_csi_cub);
    RUN_TEST(test_csi_cud);
    RUN_TEST(test_csi_cup_clamp);
    RUN_TEST(test_csi_ed_clear_below);
    RUN_TEST(test_csi_el_clear_left);
    RUN_TEST(test_sgr_no_params);
    RUN_TEST(test_cuu_clamp_at_top);
    RUN_TEST(test_cuf_clamp_at_right);
    RUN_TEST(test_backspace_at_col0);
    RUN_TEST(test_escape_not_csi);

    /* Additional coverage for a26fbf0+ changes */
    RUN_TEST(test_sgr_normal_fg_colors);
    RUN_TEST(test_sgr_normal_bg_colors);
    RUN_TEST(test_sgr_bright_fg_all);
    RUN_TEST(test_sgr_multi_param);
    RUN_TEST(test_sgr_reset_in_multi);
    RUN_TEST(test_sgr_bold_then_reset);
    RUN_TEST(test_sgr_default_fg);
    RUN_TEST(test_sgr_normal_intensity);
    RUN_TEST(test_csi_cup_f_alias);
    RUN_TEST(test_csi_ed_clear_above);
    RUN_TEST(test_invalid_csi_char);
    RUN_TEST(test_scroll_multiple_newlines);
    RUN_TEST(test_tab_alignment);

    TEST_REPORT();
}
