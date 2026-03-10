/*
 * yes — repeatedly output a string (default "y")
 */
int write(int fd, const void *buf, int count);

static unsigned int slen(const char *s)
{
    unsigned int n = 0;
    while (*s++) n++;
    return n;
}

int main(int argc, char **argv)
{
    const char *msg = "y";
    unsigned int len;

    if (argc > 1)
        msg = argv[1];

    len = slen(msg);

    for (;;) {
        write(1, msg, len);
        write(1, "\n", 1);
    }
    /* not reached */
    return 0;
}
