/*
 * dirname — strip last component from path
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
    const char *path;
    const char *end;

    if (argc < 2) {
        static const char msg[] = "usage: dirname path\n";
        write(2, msg, sizeof(msg) - 1);
        return 1;
    }

    path = argv[1];
    end = path + slen(path);

    /* Strip trailing slashes */
    while (end > path && end[-1] == '/')
        end--;

    /* Strip last component */
    while (end > path && end[-1] != '/')
        end--;

    /* Strip trailing slashes again */
    while (end > path && end[-1] == '/')
        end--;

    if (end == path) {
        /* No directory part — return "." or "/" */
        if (path[0] == '/')
            write(1, "/\n", 2);
        else
            write(1, ".\n", 2);
    } else {
        write(1, path, end - path);
        write(1, "\n", 1);
    }
    return 0;
}
