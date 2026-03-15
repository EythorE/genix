/*
 * xargs — build and execute commands from stdin
 *
 * Usage: xargs [command [args ...]]
 *
 * Reads whitespace-separated arguments from stdin, appends them
 * to the command, and executes. Uses vfork()+exec() (no fork).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_ARGS 64
#define MAX_LINE 1024

int main(int argc, char **argv)
{
    const char * volatile cmd = "/bin/echo";
    int cmd_argc = 0;
    char *cmd_argv[MAX_ARGS + 1];

    /* Copy command and its initial args */
    if (argc > 1) {
        cmd = argv[1];
        for (int i = 1; i < argc && cmd_argc < MAX_ARGS; i++)
            cmd_argv[cmd_argc++] = argv[i];
    } else {
        cmd_argv[cmd_argc++] = "echo";
    }

    int base_argc = cmd_argc;

    /* Read arguments from stdin */
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), stdin)) {
        /* Strip trailing newline */
        int len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        /* Tokenize on whitespace */
        char *p = line;
        while (*p) {
            /* Skip whitespace */
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p == '\0')
                break;

            char *start = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
            if (*p)
                *p++ = '\0';

            if (cmd_argc < MAX_ARGS) {
                cmd_argv[cmd_argc++] = strdup(start);
            } else {
                /* Flush: execute what we have */
                cmd_argv[cmd_argc] = NULL;
                int pid = vfork();
                if (pid == 0) {
                    execve(cmd, cmd_argv, NULL);
                    fprintf(stderr, "xargs: cannot exec '%s'\n", cmd);
                    _exit(127);
                }
                if (pid > 0)
                    waitpid(pid, NULL, 0);

                /* Free duped args */
                for (int i = base_argc; i < cmd_argc; i++)
                    free(cmd_argv[i]);
                cmd_argc = base_argc;

                cmd_argv[cmd_argc++] = strdup(start);
            }
        }
    }

    /* Execute remaining args */
    if (cmd_argc > base_argc) {
        cmd_argv[cmd_argc] = NULL;
        int pid = vfork();
        if (pid == 0) {
            execve(cmd, cmd_argv, NULL);
            fprintf(stderr, "xargs: cannot exec '%s'\n", cmd);
            _exit(127);
        }
        if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        }
    }

    return 0;
}
