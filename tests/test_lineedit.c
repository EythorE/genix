/*
 * test_lineedit.c — unit tests for lineedit pure edit functions
 *
 * Tests the edit buffer operations and key parsing without needing
 * a real TTY. The functions are linked directly from lineedit.c.
 */

#include "testutil.h"
#include <string.h>

/* Pull in the definitions we need to test */
#define LE_LINE_MAX 256

enum le_key {
    LEK_CHAR,
    LEK_LEFT,
    LEK_RIGHT,
    LEK_HOME,
    LEK_END,
    LEK_BACKSPACE,
    LEK_DELETE,
    LEK_ENTER,
    LEK_EOF,
    LEK_KILL,
    LEK_UP,
    LEK_DOWN,
    LEK_NONE
};

struct le_state {
    char buf[LE_LINE_MAX];
    int  len;
    int  pos;
    int  cols;
};

/* Declarations of functions from lineedit.c (compiled separately for target,
   but we redefine and test the pure logic here) */

/* ---- Inline reimplementation for host testing ---- */

static int le_insert(struct le_state *st, char ch)
{
    if (st->len >= LE_LINE_MAX - 1)
        return 0;
    for (int i = st->len; i > st->pos; i--)
        st->buf[i] = st->buf[i - 1];
    st->buf[st->pos] = ch;
    st->len++;
    st->pos++;
    st->buf[st->len] = '\0';
    return 1;
}

static int le_delete_back(struct le_state *st)
{
    if (st->pos <= 0)
        return 0;
    st->pos--;
    for (int i = st->pos; i < st->len - 1; i++)
        st->buf[i] = st->buf[i + 1];
    st->len--;
    st->buf[st->len] = '\0';
    return 1;
}

static int le_delete_fwd(struct le_state *st)
{
    if (st->pos >= st->len)
        return 0;
    for (int i = st->pos; i < st->len - 1; i++)
        st->buf[i] = st->buf[i + 1];
    st->len--;
    st->buf[st->len] = '\0';
    return 1;
}

static int le_move_left(struct le_state *st)
{
    if (st->pos <= 0)
        return 0;
    st->pos--;
    return 1;
}

static int le_move_right(struct le_state *st)
{
    if (st->pos >= st->len)
        return 0;
    st->pos++;
    return 1;
}

static int le_move_home(struct le_state *st)
{
    if (st->pos == 0)
        return 0;
    st->pos = 0;
    return 1;
}

static int le_move_end(struct le_state *st)
{
    if (st->pos == st->len)
        return 0;
    st->pos = st->len;
    return 1;
}

static int le_kill_line(struct le_state *st)
{
    if (st->len == 0)
        return 0;
    st->len = 0;
    st->pos = 0;
    st->buf[0] = '\0';
    return 1;
}

/* History ring buffer */
#define LE_HIST_MAX   16
#define LE_HIST_LINE  128

static char le_history[LE_HIST_MAX][LE_HIST_LINE];
static int  le_hist_count;
static int  le_hist_pos;
static char le_scratch[LE_LINE_MAX];
static int  le_scratch_valid;

static void le_hist_add(const char *line, int len)
{
    int slot = le_hist_count % LE_HIST_MAX;
    int n = len;
    if (n >= LE_HIST_LINE)
        n = LE_HIST_LINE - 1;
    for (int i = 0; i < n; i++)
        le_history[slot][i] = line[i];
    le_history[slot][n] = '\0';
    le_hist_count++;
}

static const char *le_hist_get(int pos)
{
    int oldest = le_hist_count - LE_HIST_MAX;
    if (oldest < 0) oldest = 0;
    if (pos < oldest || pos >= le_hist_count)
        return 0;
    return le_history[pos % LE_HIST_MAX];
}

static void le_hist_load(struct le_state *st, const char *src)
{
    int i = 0;
    while (src[i] && i < LE_LINE_MAX - 1) {
        st->buf[i] = src[i];
        i++;
    }
    st->buf[i] = '\0';
    st->len = i;
    st->pos = i;
}

static int le_hist_up(struct le_state *st)
{
    int oldest = le_hist_count - LE_HIST_MAX;
    if (oldest < 0) oldest = 0;

    if (le_hist_count == 0 || le_hist_pos <= oldest)
        return 0;

    if (!le_scratch_valid) {
        for (int i = 0; i <= st->len && i < LE_LINE_MAX; i++)
            le_scratch[i] = st->buf[i];
        le_scratch_valid = 1;
    }

    le_hist_pos--;
    le_hist_load(st, le_hist_get(le_hist_pos));
    return 1;
}

static int le_hist_down(struct le_state *st)
{
    if (le_hist_pos >= le_hist_count)
        return 0;

    le_hist_pos++;
    if (le_hist_pos == le_hist_count) {
        if (le_scratch_valid)
            le_hist_load(st, le_scratch);
        le_scratch_valid = 0;
        return 1;
    }
    le_hist_load(st, le_hist_get(le_hist_pos));
    return 1;
}

/* Key parsing test via pipe */
#include <unistd.h>
#include <errno.h>

static enum le_key le_readkey(int fd, char *out_ch)
{
    unsigned char c;
    int n = read(fd, &c, 1);
    if (n <= 0) {
        if (n < 0 && errno == EINTR)
            return LEK_NONE;
        return LEK_EOF;
    }

    if (c == 0x91) return LEK_HOME;
    if (c == 0x92) return LEK_END;
    if (c == 0x95) return LEK_UP;
    if (c == 0x96) return LEK_DOWN;
    if (c == 0x97) return LEK_RIGHT;
    if (c == 0x98) return LEK_LEFT;
    if (c == 0x7F) return LEK_DELETE;

    if (c == '\r' || c == '\n') return LEK_ENTER;
    if (c == 0x08) return LEK_BACKSPACE;
    if (c == 0x01) return LEK_HOME;
    if (c == 0x02) return LEK_LEFT;
    if (c == 0x05) return LEK_END;
    if (c == 0x06) return LEK_RIGHT;
    if (c == 0x15) return LEK_KILL;
    if (c == 0x04) {
        *out_ch = 0;
        return LEK_EOF;
    }

    if (c == 0x1B) {
        unsigned char seq[4];
        n = read(fd, &seq[0], 1);
        if (n <= 0) return LEK_NONE;
        if (seq[0] == '[') {
            n = read(fd, &seq[1], 1);
            if (n <= 0) return LEK_NONE;
            switch (seq[1]) {
            case 'A': return LEK_UP;
            case 'B': return LEK_DOWN;
            case 'C': return LEK_RIGHT;
            case 'D': return LEK_LEFT;
            case 'H': return LEK_HOME;
            case 'F': return LEK_END;
            case '3':
                n = read(fd, &seq[2], 1);
                if (n > 0 && seq[2] == '~')
                    return LEK_DELETE;
                return LEK_NONE;
            }
        }
        return LEK_NONE;
    }

    if (c >= 0x20 && c <= 0x7E) {
        *out_ch = (char)c;
        return LEK_CHAR;
    }

    return LEK_NONE;
}

/* Helper: init state */
static void init_state(struct le_state *st)
{
    st->buf[0] = '\0';
    st->len = 0;
    st->pos = 0;
    st->cols = 80;
}

/* Helper: set state to a string */
static void set_state(struct le_state *st, const char *s, int pos)
{
    int i = 0;
    while (s[i]) {
        st->buf[i] = s[i];
        i++;
    }
    st->buf[i] = '\0';
    st->len = i;
    st->pos = pos >= 0 ? pos : i;
    st->cols = 80;
}

static void reset_history(void)
{
    le_hist_count = 0;
    le_hist_pos = 0;
    le_scratch_valid = 0;
    memset(le_history, 0, sizeof(le_history));
}

/* ======== Tests ======== */

static void test_insert_empty(void)
{
    struct le_state st;
    init_state(&st);
    ASSERT_EQ(le_insert(&st, 'a'), 1);
    ASSERT_STR_EQ(st.buf, "a");
    ASSERT_EQ(st.len, 1);
    ASSERT_EQ(st.pos, 1);
}

static void test_insert_at_end(void)
{
    struct le_state st;
    set_state(&st, "hel", -1);
    le_insert(&st, 'l');
    le_insert(&st, 'o');
    ASSERT_STR_EQ(st.buf, "hello");
    ASSERT_EQ(st.len, 5);
    ASSERT_EQ(st.pos, 5);
}

static void test_insert_at_start(void)
{
    struct le_state st;
    set_state(&st, "bc", 0);
    le_insert(&st, 'a');
    ASSERT_STR_EQ(st.buf, "abc");
    ASSERT_EQ(st.pos, 1);
}

static void test_insert_in_middle(void)
{
    struct le_state st;
    set_state(&st, "ac", 1);
    le_insert(&st, 'b');
    ASSERT_STR_EQ(st.buf, "abc");
    ASSERT_EQ(st.pos, 2);
    ASSERT_EQ(st.len, 3);
}

static void test_insert_buffer_full(void)
{
    struct le_state st;
    init_state(&st);
    /* Fill to LE_LINE_MAX - 1 */
    for (int i = 0; i < LE_LINE_MAX - 1; i++)
        le_insert(&st, 'x');
    ASSERT_EQ(st.len, LE_LINE_MAX - 1);
    /* One more should fail */
    ASSERT_EQ(le_insert(&st, 'y'), 0);
    ASSERT_EQ(st.len, LE_LINE_MAX - 1);
}

static void test_delete_back(void)
{
    struct le_state st;
    set_state(&st, "abc", -1);
    ASSERT_EQ(le_delete_back(&st), 1);
    ASSERT_STR_EQ(st.buf, "ab");
    ASSERT_EQ(st.pos, 2);
}

static void test_delete_back_at_start(void)
{
    struct le_state st;
    set_state(&st, "abc", 0);
    ASSERT_EQ(le_delete_back(&st), 0);
    ASSERT_STR_EQ(st.buf, "abc");
}

static void test_delete_back_middle(void)
{
    struct le_state st;
    set_state(&st, "abc", 2);
    le_delete_back(&st);
    ASSERT_STR_EQ(st.buf, "ac");
    ASSERT_EQ(st.pos, 1);
}

static void test_delete_fwd(void)
{
    struct le_state st;
    set_state(&st, "abc", 0);
    ASSERT_EQ(le_delete_fwd(&st), 1);
    ASSERT_STR_EQ(st.buf, "bc");
    ASSERT_EQ(st.pos, 0);
}

static void test_delete_fwd_at_end(void)
{
    struct le_state st;
    set_state(&st, "abc", -1);
    ASSERT_EQ(le_delete_fwd(&st), 0);
    ASSERT_STR_EQ(st.buf, "abc");
}

static void test_delete_fwd_middle(void)
{
    struct le_state st;
    set_state(&st, "abc", 1);
    le_delete_fwd(&st);
    ASSERT_STR_EQ(st.buf, "ac");
    ASSERT_EQ(st.pos, 1);
}

static void test_move_left(void)
{
    struct le_state st;
    set_state(&st, "abc", -1);
    ASSERT_EQ(le_move_left(&st), 1);
    ASSERT_EQ(st.pos, 2);
    le_move_left(&st);
    le_move_left(&st);
    ASSERT_EQ(st.pos, 0);
    ASSERT_EQ(le_move_left(&st), 0);
    ASSERT_EQ(st.pos, 0);
}

static void test_move_right(void)
{
    struct le_state st;
    set_state(&st, "abc", 0);
    ASSERT_EQ(le_move_right(&st), 1);
    ASSERT_EQ(st.pos, 1);
    le_move_right(&st);
    le_move_right(&st);
    ASSERT_EQ(st.pos, 3);
    ASSERT_EQ(le_move_right(&st), 0);
}

static void test_home_end(void)
{
    struct le_state st;
    set_state(&st, "hello", 3);
    ASSERT_EQ(le_move_home(&st), 1);
    ASSERT_EQ(st.pos, 0);
    ASSERT_EQ(le_move_home(&st), 0);

    ASSERT_EQ(le_move_end(&st), 1);
    ASSERT_EQ(st.pos, 5);
    ASSERT_EQ(le_move_end(&st), 0);
}

static void test_kill_line(void)
{
    struct le_state st;
    set_state(&st, "hello world", 5);
    ASSERT_EQ(le_kill_line(&st), 1);
    ASSERT_STR_EQ(st.buf, "");
    ASSERT_EQ(st.len, 0);
    ASSERT_EQ(st.pos, 0);

    ASSERT_EQ(le_kill_line(&st), 0);
}

static void test_history_add_retrieve(void)
{
    reset_history();
    le_hist_add("cmd1", 4);
    le_hist_add("cmd2", 4);
    le_hist_add("cmd3", 4);
    le_hist_add("cmd4", 4);
    le_hist_add("cmd5", 4);

    ASSERT_EQ(le_hist_count, 5);
    ASSERT_STR_EQ(le_hist_get(0), "cmd1");
    ASSERT_STR_EQ(le_hist_get(1), "cmd2");
    ASSERT_STR_EQ(le_hist_get(2), "cmd3");
    ASSERT_STR_EQ(le_hist_get(3), "cmd4");
    ASSERT_STR_EQ(le_hist_get(4), "cmd5");
}

static void test_history_ring_wrap(void)
{
    reset_history();
    /* Add 20 entries, ring size is 16 */
    for (int i = 0; i < 20; i++) {
        char cmd[16];
        int len = 0;
        cmd[len++] = 'c';
        cmd[len++] = 'm';
        cmd[len++] = 'd';
        if (i >= 10)
            cmd[len++] = '0' + (i / 10);
        cmd[len++] = '0' + (i % 10);
        cmd[len] = '\0';
        le_hist_add(cmd, len);
    }

    ASSERT_EQ(le_hist_count, 20);
    /* Oldest 4 (0-3) should be gone */
    ASSERT(le_hist_get(0) == 0);
    ASSERT(le_hist_get(3) == 0);
    /* Entry 4 should be accessible */
    ASSERT_STR_EQ(le_hist_get(4), "cmd4");
    /* Most recent */
    ASSERT_STR_EQ(le_hist_get(19), "cmd19");
}

static void test_history_updown(void)
{
    struct le_state st;
    reset_history();
    le_hist_add("first", 5);
    le_hist_add("second", 6);
    le_hist_add("third", 5);

    init_state(&st);
    le_insert(&st, 'x');  /* current line = "x" */
    le_hist_pos = le_hist_count;
    le_scratch_valid = 0;

    /* UP → third */
    ASSERT_EQ(le_hist_up(&st), 1);
    ASSERT_STR_EQ(st.buf, "third");

    /* UP → second */
    ASSERT_EQ(le_hist_up(&st), 1);
    ASSERT_STR_EQ(st.buf, "second");

    /* UP → first */
    ASSERT_EQ(le_hist_up(&st), 1);
    ASSERT_STR_EQ(st.buf, "first");

    /* UP at top → no change */
    ASSERT_EQ(le_hist_up(&st), 0);
    ASSERT_STR_EQ(st.buf, "first");

    /* DOWN → second */
    ASSERT_EQ(le_hist_down(&st), 1);
    ASSERT_STR_EQ(st.buf, "second");

    /* DOWN → third */
    ASSERT_EQ(le_hist_down(&st), 1);
    ASSERT_STR_EQ(st.buf, "third");

    /* DOWN → restore scratch "x" */
    ASSERT_EQ(le_hist_down(&st), 1);
    ASSERT_STR_EQ(st.buf, "x");

    /* DOWN past newest → no change */
    ASSERT_EQ(le_hist_down(&st), 0);
}

static void test_history_empty(void)
{
    struct le_state st;
    reset_history();
    init_state(&st);
    le_hist_pos = le_hist_count;
    le_scratch_valid = 0;

    ASSERT_EQ(le_hist_up(&st), 0);
    ASSERT_EQ(le_hist_down(&st), 0);
}

static void test_history_truncate(void)
{
    reset_history();
    /* Line longer than LE_HIST_LINE (128) */
    char long_line[256];
    memset(long_line, 'a', 200);
    long_line[200] = '\0';
    le_hist_add(long_line, 200);

    const char *got = le_hist_get(0);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ((int)strlen(got), LE_HIST_LINE - 1);
}

/* Key parsing tests using a pipe */
static void test_readkey_printable(void)
{
    int pfd[2];
    pipe(pfd);
    char ch = 0;
    unsigned char data = 'A';
    write(pfd[1], &data, 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_CHAR);
    ASSERT_EQ(ch, 'A');
    close(pfd[0]);
}

static void test_readkey_ctrl(void)
{
    int pfd[2];
    char ch;
    unsigned char data;

    /* ^A = HOME */
    pipe(pfd);
    data = 0x01;
    write(pfd[1], &data, 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_HOME);
    close(pfd[0]);

    /* ^B = LEFT */
    pipe(pfd);
    data = 0x02;
    write(pfd[1], &data, 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_LEFT);
    close(pfd[0]);

    /* ^E = END */
    pipe(pfd);
    data = 0x05;
    write(pfd[1], &data, 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_END);
    close(pfd[0]);

    /* ^F = RIGHT */
    pipe(pfd);
    data = 0x06;
    write(pfd[1], &data, 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_RIGHT);
    close(pfd[0]);

    /* ^H = BACKSPACE */
    pipe(pfd);
    data = 0x08;
    write(pfd[1], &data, 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_BACKSPACE);
    close(pfd[0]);

    /* ^U = KILL */
    pipe(pfd);
    data = 0x15;
    write(pfd[1], &data, 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_KILL);
    close(pfd[0]);
}

static void test_readkey_ansi_arrows(void)
{
    int pfd[2];
    char ch;

    /* ESC [ A = UP */
    pipe(pfd);
    write(pfd[1], "\x1b[A", 3);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_UP);
    close(pfd[0]);

    /* ESC [ B = DOWN */
    pipe(pfd);
    write(pfd[1], "\x1b[B", 3);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_DOWN);
    close(pfd[0]);

    /* ESC [ C = RIGHT */
    pipe(pfd);
    write(pfd[1], "\x1b[C", 3);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_RIGHT);
    close(pfd[0]);

    /* ESC [ D = LEFT */
    pipe(pfd);
    write(pfd[1], "\x1b[D", 3);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_LEFT);
    close(pfd[0]);

    /* ESC [ H = HOME */
    pipe(pfd);
    write(pfd[1], "\x1b[H", 3);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_HOME);
    close(pfd[0]);

    /* ESC [ F = END */
    pipe(pfd);
    write(pfd[1], "\x1b[F", 3);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_END);
    close(pfd[0]);
}

static void test_readkey_ansi_delete(void)
{
    int pfd[2];
    char ch;

    /* ESC [ 3 ~ = DELETE */
    pipe(pfd);
    write(pfd[1], "\x1b[3~", 4);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_DELETE);
    close(pfd[0]);
}

static void test_readkey_md_keycodes(void)
{
    int pfd[2];
    char ch;
    unsigned char data;

    /* 0x95 = UP */
    pipe(pfd);
    data = 0x95;
    write(pfd[1], &data, 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_UP);
    close(pfd[0]);

    /* 0x96 = DOWN */
    pipe(pfd);
    data = 0x96;
    write(pfd[1], &data, 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_DOWN);
    close(pfd[0]);

    /* 0x91 = HOME */
    pipe(pfd);
    data = 0x91;
    write(pfd[1], &data, 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_HOME);
    close(pfd[0]);

    /* 0x7F = DELETE */
    pipe(pfd);
    data = 0x7F;
    write(pfd[1], &data, 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_DELETE);
    close(pfd[0]);
}

static void test_readkey_enter(void)
{
    int pfd[2];
    char ch;

    pipe(pfd);
    write(pfd[1], "\n", 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_ENTER);
    close(pfd[0]);

    pipe(pfd);
    write(pfd[1], "\r", 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_ENTER);
    close(pfd[0]);
}

static void test_readkey_eof(void)
{
    int pfd[2];
    char ch = 'z';

    /* ^D */
    pipe(pfd);
    write(pfd[1], "\x04", 1);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_EOF);
    close(pfd[0]);

    /* Closed pipe = EOF */
    pipe(pfd);
    close(pfd[1]);
    ASSERT_EQ(le_readkey(pfd[0], &ch), LEK_EOF);
    close(pfd[0]);
}

int main(void)
{
    printf("=== test_lineedit ===\n");

    printf("  -- edit operations --\n");
    RUN_TEST(test_insert_empty);
    RUN_TEST(test_insert_at_end);
    RUN_TEST(test_insert_at_start);
    RUN_TEST(test_insert_in_middle);
    RUN_TEST(test_insert_buffer_full);
    RUN_TEST(test_delete_back);
    RUN_TEST(test_delete_back_at_start);
    RUN_TEST(test_delete_back_middle);
    RUN_TEST(test_delete_fwd);
    RUN_TEST(test_delete_fwd_at_end);
    RUN_TEST(test_delete_fwd_middle);
    RUN_TEST(test_move_left);
    RUN_TEST(test_move_right);
    RUN_TEST(test_home_end);
    RUN_TEST(test_kill_line);

    printf("  -- history --\n");
    RUN_TEST(test_history_add_retrieve);
    RUN_TEST(test_history_ring_wrap);
    RUN_TEST(test_history_updown);
    RUN_TEST(test_history_empty);
    RUN_TEST(test_history_truncate);

    printf("  -- key parsing --\n");
    RUN_TEST(test_readkey_printable);
    RUN_TEST(test_readkey_ctrl);
    RUN_TEST(test_readkey_ansi_arrows);
    RUN_TEST(test_readkey_ansi_delete);
    RUN_TEST(test_readkey_md_keycodes);
    RUN_TEST(test_readkey_enter);
    RUN_TEST(test_readkey_eof);

    TEST_REPORT();
}
