/*
 * vdptest — VDP terminal ANSI escape sequence test program
 *
 * Exercises every ANSI sequence supported by the Mega Drive VDP terminal
 * and displays a structured test pattern for visual verification in BlastEm.
 *
 * Each test labels what it does and shows the expected result, so a human
 * reviewing a screenshot can verify correctness.
 *
 * Options:
 *   -n   No-wait mode: display for ~120 frames then exit (for automated tests)
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Small delay via busy loop (~1/4 second at 7.67 MHz) */
static void delay(void)
{
    for (volatile int i = 0; i < 200000; i++)
        ;
}

/* Write a string directly (avoids printf overhead) */
static void wr(const char *s)
{
    write(1, s, strlen(s));
}

/*
 * Test 1: Clear screen and basic text
 * Expected: blank screen, "TEST 1: Basic text" at top-left
 */
static void test_clear_and_basic(void)
{
    wr("\033[2J");         /* clear entire screen */
    wr("\033[H");          /* home cursor (1,1) */
    wr("TEST 1: Basic text at (1,1)\n");
}

/*
 * Test 2: Bold text
 * Expected: "BOLD" in bright white, "NORMAL" in gray
 */
static void test_bold(void)
{
    wr("TEST 2: ");
    wr("\033[1mBOLD\033[0m NORMAL\n");
}

/*
 * Test 3: Cursor positioning
 * Expected: "X" at row 5, col 20 and "Y" at row 6, col 30
 */
static void test_cursor_position(void)
{
    wr("TEST 3: Cursor pos (see row 5-6)\n");
    wr("\033[5;20H");      /* move to row 5, col 20 */
    wr("X<-r5c20");
    wr("\033[6;30H");      /* move to row 6, col 30 */
    wr("Y<-r6c30");
}

/*
 * Test 4: Cursor movement (relative)
 * Expected: "ABCD" spelled out by moving cursor in all 4 directions
 */
static void test_cursor_movement(void)
{
    wr("\033[8;1H");       /* row 8, col 1 */
    wr("TEST 4: Rel move: ");
    wr("A");
    wr("\033[C");          /* forward 1 */
    wr("B");
    wr("\033[2D");         /* back 2 (on top of B) */
    wr("\033[B");          /* down 1 */
    wr("C");
    wr("\033[A");          /* up 1 */
    wr("\033[2C");         /* forward 2 */
    wr("D");
}

/*
 * Test 5: Line clear (EL)
 * Expected: row 10 shows "VISIBLE" then blanks, row 11 shows "TEST 5: OK"
 */
static void test_line_clear(void)
{
    wr("\033[10;1H");      /* row 10 */
    wr("VISIBLE___ERASED_PART_HERE___");
    wr("\033[10;8H");      /* back to col 8 on same row */
    wr("\033[K");           /* clear to end of line */
    wr("\033[11;1H");
    wr("TEST 5: Line clear (row 10 shows 'VISIBLE' only)\n");
}

/*
 * Test 6: Partial screen clear (ED)
 * Expected: rows 13-14 have text, ED 0 clears from cursor down
 */
static void test_screen_clear_partial(void)
{
    wr("\033[13;1H");
    wr("TEST 6: ED clear-below test");
    wr("\033[14;1H");
    wr("This line should remain");
    wr("\033[15;1H");
    wr("ERASED_BY_ED0_SHOULD_NOT_SEE");
    wr("\033[15;1H");
    wr("\033[J");           /* ED 0: clear from cursor to end */
    wr("\033[16;1H");
    wr("TEST 6: OK (row 15 should be blank)");
}

/*
 * Test 7: Save/restore cursor position
 * Expected: "SAVE" at one position, other text elsewhere, "REST" back at saved pos
 */
static void test_save_restore(void)
{
    wr("\033[18;1H");
    wr("TEST 7: Save/restore: ");
    wr("\033[s");           /* save cursor */
    wr("\033[19;10H");     /* move elsewhere */
    wr("(detour)");
    wr("\033[u");           /* restore cursor */
    wr("RESTORED");
}

/*
 * Test 8: Bright foreground codes
 * Expected: "BRIGHT" in bright white (same as bold on V3a)
 */
static void test_bright_fg(void)
{
    wr("\033[20;1H");
    wr("TEST 8: ");
    wr("\033[97mBRIGHT\033[39m NORMAL\n");
}

/*
 * Test 9: Word wrap
 * Expected: a long line that wraps at column 40
 */
static void test_word_wrap(void)
{
    wr("\033[22;1H");
    wr("TEST 9: Wrap->0123456789ABCDEF0123456789ABCDEF01234WRAP");
}

/*
 * Test 10: Scroll
 * Expected: filling last rows forces scroll, text moves up
 */
static void test_scroll(void)
{
    wr("\033[27;1H");
    wr("TEST 10: Scroll (this line scrolls up)");
    wr("\033[28;1H");
    wr("Line 28 - bottom row, next \\n scrolls");
    wr("\n");
    wr("After scroll - was at row 28");
}

/*
 * Summary: position at bottom
 */
static void test_summary(void)
{
    delay();
    wr("\033[2J\033[H");   /* clear and home */
    wr("\033[1m=== VDP Terminal Test Summary ===\033[0m\n\n");
    wr(" 1. Basic text output       - check row 1\n");
    wr(" 2. Bold / normal contrast  - bright vs gray\n");
    wr(" 3. Cursor positioning      - X at r5c20, Y at r6c30\n");
    wr(" 4. Relative cursor moves   - A_B pattern\n");
    wr(" 5. Line clear (EL)         - partial row erase\n");
    wr(" 6. Screen clear (ED)       - clear below cursor\n");
    wr(" 7. Save/restore cursor     - text at saved pos\n");
    wr(" 8. Bright foreground       - SGR 97\n");
    wr(" 9. Word wrap at col 40     - long line wraps\n");
    wr("10. Scroll at bottom row    - text scrolls up\n\n");
    wr("\033[1mAll tests displayed.\033[0m Check screenshot.\n");
}

int main(int argc, char **argv)
{
    int nowait = 0;

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n')
        nowait = 1;

    /* Page 1: Tests 1-10 */
    test_clear_and_basic();
    test_bold();
    test_cursor_position();
    test_cursor_movement();
    test_line_clear();
    test_screen_clear_partial();
    test_save_restore();
    test_bright_fg();
    test_word_wrap();
    test_scroll();

    if (!nowait) {
        /* Interactive: wait for keypress between pages */
        delay();
        delay();
        delay();
        delay();
    }

    /* Page 2: Summary */
    test_summary();

    if (nowait) {
        /* Automated: hold display for ~2 seconds (120 frames at 60fps) */
        for (int i = 0; i < 8; i++)
            delay();
    } else {
        /* Interactive: wait for keypress */
        char c;
        read(0, &c, 1);
    }

    /* Reset terminal */
    wr("\033[0m\033[2J\033[H");
    return 0;
}
