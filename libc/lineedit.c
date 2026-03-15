/*
 * lineedit.c — single-line editor with history for Genix
 *
 * Userspace library. Switches TTY to raw mode, reads keys, performs
 * editing, outputs display updates via write(). Returns a complete line.
 *
 * Display uses only \b and character writes — no ANSI cursor positioning.
 * This works on both VDP console (no ANSI parser) and UART terminals.
 *
 * Two input paths:
 *   Mega Drive: Saturn keyboard raw keycodes (0x91=HOME, 0x95=UP, etc.)
 *   Workbench:  ANSI escape sequences (ESC [ A, etc.)
 */

#include <lineedit.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

/* Key abstraction */
enum le_key {
    LEK_CHAR,       /* printable character */
    LEK_LEFT,
    LEK_RIGHT,
    LEK_HOME,
    LEK_END,
    LEK_BACKSPACE,
    LEK_DELETE,
    LEK_ENTER,
    LEK_EOF,        /* ^D on empty line */
    LEK_KILL,       /* ^U */
    LEK_UP,
    LEK_DOWN,
    LEK_NONE        /* unknown / ignored */
};

/* Editor state */
struct le_state {
    char buf[LE_LINE_MAX];
    int  len;       /* chars in buffer */
    int  pos;       /* cursor position */
    int  cols;      /* terminal width */
};

/* History ring buffer */
#define LE_HIST_MAX   16
#define LE_HIST_LINE  128

static char le_history[LE_HIST_MAX][LE_HIST_LINE];
static int  le_hist_count;  /* total entries ever added */

/* Browsing state (only valid during le_readline) */
static int  le_hist_pos;    /* current browse position */
static char le_scratch[LE_LINE_MAX]; /* saves current line on first UP */
static int  le_scratch_valid;

/* Terminal width */
static int le_term_cols = 80;

/* Saved termios for restoration */
static struct termios le_saved_termios;
static int le_termios_saved;

/*
 * Pure edit functions — operate on struct le_state, testable without TTY.
 * Return 1 if display needs refresh, 0 otherwise.
 */

int le_insert(struct le_state *st, char ch)
{
    if (st->len >= LE_LINE_MAX - 1)
        return 0;
    /* shift right from pos */
    for (int i = st->len; i > st->pos; i--)
        st->buf[i] = st->buf[i - 1];
    st->buf[st->pos] = ch;
    st->len++;
    st->pos++;
    st->buf[st->len] = '\0';
    return 1;
}

int le_delete_back(struct le_state *st)
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

int le_delete_fwd(struct le_state *st)
{
    if (st->pos >= st->len)
        return 0;
    for (int i = st->pos; i < st->len - 1; i++)
        st->buf[i] = st->buf[i + 1];
    st->len--;
    st->buf[st->len] = '\0';
    return 1;
}

int le_move_left(struct le_state *st)
{
    if (st->pos <= 0)
        return 0;
    st->pos--;
    return 1;
}

int le_move_right(struct le_state *st)
{
    if (st->pos >= st->len)
        return 0;
    st->pos++;
    return 1;
}

int le_move_home(struct le_state *st)
{
    if (st->pos == 0)
        return 0;
    st->pos = 0;
    return 1;
}

int le_move_end(struct le_state *st)
{
    if (st->pos == st->len)
        return 0;
    st->pos = st->len;
    return 1;
}

int le_kill_line(struct le_state *st)
{
    if (st->len == 0)
        return 0;
    st->len = 0;
    st->pos = 0;
    st->buf[0] = '\0';
    return 1;
}

/*
 * History functions
 */

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

/* Load a history entry (or scratch) into the edit buffer */
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

    /* Save current line on first UP */
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
        /* Restore scratch */
        if (le_scratch_valid)
            le_hist_load(st, le_scratch);
        le_scratch_valid = 0;
        return 1;
    }
    le_hist_load(st, le_hist_get(le_hist_pos));
    return 1;
}

/*
 * Key reader — reads byte(s) from fd, returns key type.
 * For LEK_CHAR, *out_ch is the character.
 */
static enum le_key le_readkey(int fd, char *out_ch)
{
    unsigned char c;
    int n = read(fd, &c, 1);
    if (n <= 0) {
        if (n < 0 && errno == EINTR)
            return LEK_NONE;
        return LEK_EOF;
    }

    /* Mega Drive Saturn keyboard raw keycodes */
    if (c == 0x91) return LEK_HOME;
    if (c == 0x92) return LEK_END;
    if (c == 0x95) return LEK_UP;
    if (c == 0x96) return LEK_DOWN;
    if (c == 0x97) return LEK_RIGHT;
    if (c == 0x98) return LEK_LEFT;
    if (c == 0x7F) return LEK_DELETE;

    /* Control characters */
    if (c == '\r' || c == '\n') return LEK_ENTER;
    if (c == 0x08) return LEK_BACKSPACE;  /* ^H / BS */
    if (c == 0x01) return LEK_HOME;       /* ^A */
    if (c == 0x02) return LEK_LEFT;       /* ^B */
    if (c == 0x05) return LEK_END;        /* ^E */
    if (c == 0x06) return LEK_RIGHT;      /* ^F */
    if (c == 0x15) return LEK_KILL;       /* ^U */
    if (c == 0x04) {                      /* ^D */
        *out_ch = 0;
        return LEK_EOF;
    }

    /* ANSI escape sequences */
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
            case '3': /* ESC [ 3 ~ = Delete */
                n = read(fd, &seq[2], 1);
                if (n > 0 && seq[2] == '~')
                    return LEK_DELETE;
                return LEK_NONE;
            }
        }
        return LEK_NONE;
    }

    /* Printable characters */
    if (c >= 0x20 && c <= 0x7E) {
        *out_ch = (char)c;
        return LEK_CHAR;
    }

    return LEK_NONE;
}

/*
 * Display update — uses only \b and character writes.
 *
 * After an edit, we need to:
 * 1. Move cursor back to edit position
 * 2. Write from pos to end of buffer
 * 3. Overwrite any trailing chars from previous content with spaces
 * 4. Move cursor back to the new pos
 */
static void le_refresh(int fd, struct le_state *st, int old_len, int old_pos)
{
    char tmp[LE_LINE_MAX + 64];
    int ti = 0;

    /* Move cursor to old_pos position by writing \b */
    /* (cursor is at old_pos after previous output) */

    /* Step 1: back up to position 0 */
    for (int i = 0; i < old_pos; i++)
        tmp[ti++] = '\b';

    /* Step 2: write entire buffer */
    for (int i = 0; i < st->len; i++)
        tmp[ti++] = st->buf[i];

    /* Step 3: overwrite leftover chars with spaces */
    int extra = old_len - st->len;
    for (int i = 0; i < extra; i++)
        tmp[ti++] = ' ';

    /* Step 4: back up to cursor position */
    int tail = st->len + (extra > 0 ? extra : 0) - st->pos;
    for (int i = 0; i < tail; i++)
        tmp[ti++] = '\b';

    write(fd, tmp, ti);
}

/* Simpler output for insert-at-end (common case) */
static void le_output_char(int fd, struct le_state *st)
{
    /* Just wrote at end — output the character */
    write(fd, &st->buf[st->pos - 1], 1);
}

/* Output for insert-in-middle */
static void le_output_insert(int fd, struct le_state *st)
{
    char tmp[LE_LINE_MAX + 64];
    int ti = 0;

    /* Write from pos-1 to end */
    for (int i = st->pos - 1; i < st->len; i++)
        tmp[ti++] = st->buf[i];
    /* Back up to pos */
    for (int i = st->pos; i < st->len; i++)
        tmp[ti++] = '\b';

    write(fd, tmp, ti);
}

/* Output for backspace */
static void le_output_backspace(int fd, struct le_state *st)
{
    char tmp[LE_LINE_MAX + 64];
    int ti = 0;

    tmp[ti++] = '\b';
    /* Rewrite from new pos to end */
    for (int i = st->pos; i < st->len; i++)
        tmp[ti++] = st->buf[i];
    /* Overwrite the old last char with space */
    tmp[ti++] = ' ';
    /* Back up */
    int back = st->len - st->pos + 1;
    for (int i = 0; i < back; i++)
        tmp[ti++] = '\b';

    write(fd, tmp, ti);
}

/* Output for delete-forward */
static void le_output_delete(int fd, struct le_state *st, int old_len)
{
    char tmp[LE_LINE_MAX + 64];
    int ti = 0;

    /* Rewrite from pos to end */
    for (int i = st->pos; i < st->len; i++)
        tmp[ti++] = st->buf[i];
    /* Space over old last char */
    int extra = old_len - st->len;
    for (int i = 0; i < extra; i++)
        tmp[ti++] = ' ';
    /* Back up */
    int back = st->len - st->pos + extra;
    for (int i = 0; i < back; i++)
        tmp[ti++] = '\b';

    write(fd, tmp, ti);
}

/*
 * Public API
 */

void le_init(int cols)
{
    le_term_cols = cols > 0 ? cols : 80;
}

int le_readline(int fd, char *buf, int bufsz)
{
    struct le_state st;
    struct termios raw;
    char ch;
    enum le_key key;

    /* Restore termios if previous call was interrupted */
    if (le_termios_saved) {
        tcsetattr(fd, TCSANOW, &le_saved_termios);
        le_termios_saved = 0;
    }

    /* Save and set raw mode */
    if (tcgetattr(fd, &le_saved_termios) < 0)
        return read(fd, buf, bufsz);  /* fallback if not a TTY */

    le_termios_saved = 1;
    raw = le_saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    /* Keep ISIG on so ^C/^Z are handled by kernel */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &raw);

    /* Init editor state */
    st.buf[0] = '\0';
    st.len = 0;
    st.pos = 0;
    st.cols = le_term_cols;

    /* Init history browsing */
    le_hist_pos = le_hist_count;
    le_scratch_valid = 0;

    for (;;) {
        ch = 0;
        key = le_readkey(fd, &ch);

        switch (key) {
        case LEK_CHAR:
            if (le_insert(&st, ch)) {
                if (st.pos == st.len)
                    le_output_char(fd, &st);
                else
                    le_output_insert(fd, &st);
            }
            break;

        case LEK_BACKSPACE:
            if (le_delete_back(&st))
                le_output_backspace(fd, &st);
            break;

        case LEK_DELETE: {
            int old_len = st.len;
            if (le_delete_fwd(&st))
                le_output_delete(fd, &st, old_len);
            break;
        }

        case LEK_LEFT:
            if (le_move_left(&st))
                write(fd, "\b", 1);
            break;

        case LEK_RIGHT:
            if (le_move_right(&st))
                write(fd, &st.buf[st.pos - 1], 1);
            break;

        case LEK_HOME: {
            int old_pos = st.pos;
            if (le_move_home(&st)) {
                char bk[LE_LINE_MAX];
                for (int i = 0; i < old_pos; i++)
                    bk[i] = '\b';
                write(fd, bk, old_pos);
            }
            break;
        }

        case LEK_END: {
            int old_pos = st.pos;
            if (le_move_end(&st))
                write(fd, st.buf + old_pos, st.len - old_pos);
            break;
        }

        case LEK_KILL: {
            int old_len = st.len;
            int old_pos = st.pos;
            if (le_kill_line(&st))
                le_refresh(fd, &st, old_len, old_pos);
            break;
        }

        case LEK_UP: {
            int old_len = st.len;
            int old_pos = st.pos;
            if (le_hist_up(&st))
                le_refresh(fd, &st, old_len, old_pos);
            break;
        }

        case LEK_DOWN: {
            int old_len = st.len;
            int old_pos = st.pos;
            if (le_hist_down(&st))
                le_refresh(fd, &st, old_len, old_pos);
            break;
        }

        case LEK_EOF:
            if (st.len == 0) {
                /* EOF on empty line */
                tcsetattr(fd, TCSANOW, &le_saved_termios);
                le_termios_saved = 0;
                return 0;
            }
            /* ^D with content = delete forward */
            {
                int old_len = st.len;
                if (le_delete_fwd(&st))
                    le_output_delete(fd, &st, old_len);
            }
            break;

        case LEK_ENTER:
            /* Add to history if non-empty */
            if (st.len > 0)
                le_hist_add(st.buf, st.len);
            /* Append newline and output it */
            write(fd, "\n", 1);
            tcsetattr(fd, TCSANOW, &le_saved_termios);
            le_termios_saved = 0;
            /* Copy to caller's buffer */
            {
                int n = st.len;
                if (n + 1 > bufsz)
                    n = bufsz - 1;
                for (int i = 0; i < n; i++)
                    buf[i] = st.buf[i];
                buf[n] = '\n';
                return n + 1;
            }

        case LEK_NONE:
            break;
        }
    }
}
