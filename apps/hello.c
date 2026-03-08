/*
 * hello — first Genix user program
 */
int write(int fd, const void *buf, int count);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const char msg[] = "Hello from userspace!\n";
    write(1, msg, sizeof(msg) - 1);
    return 0;
}
