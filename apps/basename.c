/*
 * basename — strip directory and optional suffix from path
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
    const char *path, *p, *base;
    unsigned int baselen, suflen;

    if (argc < 2) {
        static const char msg[] = "usage: basename path [suffix]\n";
        write(2, msg, sizeof(msg) - 1);
        return 1;
    }

    path = argv[1];

    /* Strip trailing slashes */
    p = path + slen(path);
    while (p > path && p[-1] == '/')
        p--;

    if (p == path) {
        /* All slashes or empty */
        write(1, "/\n", 2);
        return 0;
    }

    /* Find last slash before the end */
    base = p;
    while (base > path && base[-1] != '/')
        base--;

    baselen = p - base;

    /* Strip suffix if provided */
    if (argc > 2) {
        suflen = slen(argv[2]);
        if (suflen < baselen) {
            const char *bs = p - suflen;
            const char *ss = argv[2];
            int match = 1;
            for (unsigned int i = 0; i < suflen; i++) {
                if (bs[i] != ss[i]) { match = 0; break; }
            }
            if (match)
                baselen -= suflen;
        }
    }

    write(1, base, baselen);
    write(1, "\n", 1);
    return 0;
}
