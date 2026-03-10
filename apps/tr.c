/*
 * tr — translate characters
 *
 * Usage: tr string1 string2
 * Reads stdin, replaces chars in string1 with corresponding chars in string2.
 */
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);

int main(int argc, char **argv)
{
    unsigned char map[256];
    const unsigned char *s1, *s2;
    unsigned char c;
    int i;

    if (argc < 3) {
        static const char msg[] = "usage: tr string1 string2\n";
        write(2, msg, sizeof(msg) - 1);
        return 1;
    }

    /* Initialize identity mapping */
    for (i = 0; i < 256; i++)
        map[i] = (unsigned char)i;

    /* Build translation map */
    s1 = (const unsigned char *)argv[1];
    s2 = (const unsigned char *)argv[2];
    while (*s1 && *s2) {
        map[*s1] = *s2;
        s1++;
        s2++;
    }
    /* If s2 is shorter, remaining s1 chars map to last s2 char */
    if (*s1 && s2 > (const unsigned char *)argv[2]) {
        unsigned char last = s2[-1];
        while (*s1) {
            map[*s1] = last;
            s1++;
        }
    }

    /* Translate stdin to stdout */
    while (read(0, &c, 1) == 1) {
        c = map[c];
        write(1, &c, 1);
    }
    return 0;
}
