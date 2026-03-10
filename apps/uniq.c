/*
 * uniq — filter adjacent duplicate lines
 */
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);

#define LINE_MAX 512

static int readline(char *buf, int max)
{
    int pos = 0;
    char c;
    while (pos < max - 1 && read(0, &c, 1) == 1) {
        buf[pos++] = c;
        if (c == '\n')
            break;
    }
    buf[pos] = '\0';
    return pos;
}

static int streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int main(void)
{
    char prev[LINE_MAX];
    char curr[LINE_MAX];
    int n;

    prev[0] = '\0';

    /* Read first line */
    n = readline(prev, LINE_MAX);
    if (n == 0)
        return 0;
    write(1, prev, n);

    /* Process remaining lines */
    while ((n = readline(curr, LINE_MAX)) > 0) {
        if (!streq(curr, prev)) {
            write(1, curr, n);
            /* Copy curr to prev */
            for (int i = 0; i <= n; i++)
                prev[i] = curr[i];
        }
    }
    return 0;
}
