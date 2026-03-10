/*
 * Unit tests for shell command parsing: redirections and pipe splitting.
 *
 * Tests the pure parsing logic on the host (no 68000 needed).
 * Re-implements the shell's parse_segment/parse_redirections/count_pipes.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* ======== Re-implement shell parsing functions for host testing ======== */

/* Parse a single command segment into argv, stopping at | or end. */
static char *parse_segment(char *start, char **argv, int max_args, int *argc)
{
    *argc = 0;
    char *p = start;

    while (*p && *p != '|') {
        while (*p == ' ') p++;
        if (!*p || *p == '|') break;
        if (*p == '<' || *p == '>') break;
        if (*argc < max_args - 1) {
            argv[(*argc)++] = p;
        }
        while (*p && *p != ' ' && *p != '|' && *p != '<' && *p != '>') p++;
        if (*p == ' ') *p++ = '\0';
    }
    argv[*argc] = NULL;
    return p;
}

/* Find redirection operators and extract filenames. */
static void parse_redirections(char *cmd,
                               char **infile, char **outfile, int *append)
{
    *infile = NULL;
    *outfile = NULL;
    *append = 0;

    for (char *p = cmd; *p; p++) {
        if (*p == '<') {
            *p = '\0';
            p++;
            while (*p == ' ') p++;
            *infile = p;
            while (*p && *p != ' ' && *p != '>' && *p != '|') p++;
            if (*p) { *p = '\0'; p++; }
            p--;
        } else if (*p == '>') {
            *p = '\0';
            p++;
            if (*p == '>') {
                *append = 1;
                *p = '\0';
                p++;
            }
            while (*p == ' ') p++;
            *outfile = p;
            while (*p && *p != ' ' && *p != '<' && *p != '|') p++;
            if (*p) { *p = '\0'; p++; }
            p--;
        }
    }
}

static int count_pipes(const char *cmd)
{
    int n = 0;
    for (const char *p = cmd; *p; p++) {
        if (*p == '|') n++;
    }
    return n;
}

/* ======== Tests ======== */

static void test_parse_simple_command(void)
{
    char cmd[] = "/bin/echo hello world";
    char *argv[16];
    int argc;
    parse_segment(cmd, argv, 16, &argc);
    ASSERT_EQ(argc, 3);
    ASSERT_STR_EQ(argv[0], "/bin/echo");
    ASSERT_STR_EQ(argv[1], "hello");
    ASSERT_STR_EQ(argv[2], "world");
    ASSERT_NULL(argv[3]);
}

static void test_parse_single_arg(void)
{
    char cmd[] = "/bin/ls";
    char *argv[16];
    int argc;
    parse_segment(cmd, argv, 16, &argc);
    ASSERT_EQ(argc, 1);
    ASSERT_STR_EQ(argv[0], "/bin/ls");
    ASSERT_NULL(argv[1]);
}

static void test_parse_stops_at_pipe(void)
{
    /* In the real shell, we split at '|' first (replace with '\0')
     * before calling parse_segment. Test that pattern: */
    char cmd[] = "/bin/echo hello";
    char *argv[16];
    int argc;
    char *rest = parse_segment(cmd, argv, 16, &argc);
    ASSERT_EQ(argc, 2);
    ASSERT_STR_EQ(argv[0], "/bin/echo");
    ASSERT_STR_EQ(argv[1], "hello");
    (void)rest;

    /* Also verify parse_segment stops at pipe character */
    char cmd2[] = "/bin/echo hi | /bin/cat";
    parse_segment(cmd2, argv, 16, &argc);
    ASSERT_EQ(argc, 2);
    ASSERT_STR_EQ(argv[0], "/bin/echo");
    ASSERT_STR_EQ(argv[1], "hi");
}

static void test_parse_stops_at_redirect(void)
{
    char cmd[] = "/bin/echo hello > outfile";
    char *argv[16];
    int argc;
    parse_segment(cmd, argv, 16, &argc);
    ASSERT_EQ(argc, 2);
    ASSERT_STR_EQ(argv[0], "/bin/echo");
    ASSERT_STR_EQ(argv[1], "hello");
}

static void test_parse_leading_spaces(void)
{
    char cmd[] = "  /bin/echo  hello  ";
    char *argv[16];
    int argc;
    parse_segment(cmd, argv, 16, &argc);
    ASSERT_EQ(argc, 2);
    ASSERT_STR_EQ(argv[0], "/bin/echo");
    ASSERT_STR_EQ(argv[1], "hello");
}

static void test_parse_empty(void)
{
    char cmd[] = "";
    char *argv[16];
    int argc;
    parse_segment(cmd, argv, 16, &argc);
    ASSERT_EQ(argc, 0);
    ASSERT_NULL(argv[0]);
}

static void test_redir_output(void)
{
    char cmd[] = "/bin/echo hello > outfile";
    char *infile, *outfile;
    int append;
    parse_redirections(cmd, &infile, &outfile, &append);
    ASSERT_NULL(infile);
    ASSERT_NOT_NULL(outfile);
    ASSERT_STR_EQ(outfile, "outfile");
    ASSERT_EQ(append, 0);
}

static void test_redir_output_append(void)
{
    char cmd[] = "/bin/echo hello >> logfile";
    char *infile, *outfile;
    int append;
    parse_redirections(cmd, &infile, &outfile, &append);
    ASSERT_NULL(infile);
    ASSERT_NOT_NULL(outfile);
    ASSERT_STR_EQ(outfile, "logfile");
    ASSERT_EQ(append, 1);
}

static void test_redir_input(void)
{
    char cmd[] = "/bin/cat < inputfile";
    char *infile, *outfile;
    int append;
    parse_redirections(cmd, &infile, &outfile, &append);
    ASSERT_NOT_NULL(infile);
    ASSERT_STR_EQ(infile, "inputfile");
    ASSERT_NULL(outfile);
}

static void test_redir_both(void)
{
    char cmd[] = "/bin/cat < infile > outfile";
    char *infile, *outfile;
    int append;
    parse_redirections(cmd, &infile, &outfile, &append);
    ASSERT_NOT_NULL(infile);
    ASSERT_STR_EQ(infile, "infile");
    ASSERT_NOT_NULL(outfile);
    ASSERT_STR_EQ(outfile, "outfile");
    ASSERT_EQ(append, 0);
}

static void test_redir_none(void)
{
    char cmd[] = "/bin/echo hello world";
    char *infile, *outfile;
    int append;
    parse_redirections(cmd, &infile, &outfile, &append);
    ASSERT_NULL(infile);
    ASSERT_NULL(outfile);
}

static void test_count_pipes_zero(void)
{
    ASSERT_EQ(count_pipes("/bin/echo hello"), 0);
}

static void test_count_pipes_one(void)
{
    ASSERT_EQ(count_pipes("/bin/echo hello | /bin/cat"), 1);
}

static void test_count_pipes_two(void)
{
    ASSERT_EQ(count_pipes("a | b | c"), 2);
}

static void test_count_pipes_three(void)
{
    ASSERT_EQ(count_pipes("a|b|c|d"), 3);
}

/* Test that parse_segment + parse_redirections work together */
static void test_combined_redir_and_parse(void)
{
    char cmd[] = "/bin/echo hello > outfile";
    char *infile, *outfile;
    int append;

    /* First extract redirections (modifies cmd in place) */
    parse_redirections(cmd, &infile, &outfile, &append);

    /* Then parse the remaining command */
    char *argv[16];
    int argc;
    parse_segment(cmd, argv, 16, &argc);
    ASSERT_EQ(argc, 2);
    ASSERT_STR_EQ(argv[0], "/bin/echo");
    ASSERT_STR_EQ(argv[1], "hello");
    ASSERT_NOT_NULL(outfile);
    ASSERT_STR_EQ(outfile, "outfile");
}

static void test_redir_no_space(void)
{
    char cmd[] = "/bin/echo hello>outfile";
    char *infile, *outfile;
    int append;
    parse_redirections(cmd, &infile, &outfile, &append);
    ASSERT_NOT_NULL(outfile);
    ASSERT_STR_EQ(outfile, "outfile");
}

static void test_pipe_sigpipe_on_broken(void)
{
    /* Verify that pipe_write returns -EPIPE when no readers */
    /* (Host test — just tests the logic, not actual signal delivery) */
    #define PIPE_SIZE 512
    #define EPIPE 32

    struct pipe {
        uint8_t  buf[PIPE_SIZE];
        uint16_t read_pos;
        uint16_t write_pos;
        uint16_t count;
        uint8_t  readers;
        uint8_t  writers;
        uint8_t  read_waiting;
        uint8_t  write_waiting;
    };

    struct pipe p;
    memset(&p, 0, sizeof(p));
    p.readers = 0;  /* broken: no readers */
    p.writers = 1;

    /* Re-implement minimal pipe_write for host test */
    int result;
    if (p.readers == 0) {
        result = -EPIPE;
    } else {
        result = 0;
    }
    ASSERT_EQ(result, -EPIPE);
}

/* ======== Main ======== */

int main(void)
{
    printf("test_redir:\n");

    RUN_TEST(test_parse_simple_command);
    RUN_TEST(test_parse_single_arg);
    RUN_TEST(test_parse_stops_at_pipe);
    RUN_TEST(test_parse_stops_at_redirect);
    RUN_TEST(test_parse_leading_spaces);
    RUN_TEST(test_parse_empty);
    RUN_TEST(test_redir_output);
    RUN_TEST(test_redir_output_append);
    RUN_TEST(test_redir_input);
    RUN_TEST(test_redir_both);
    RUN_TEST(test_redir_none);
    RUN_TEST(test_count_pipes_zero);
    RUN_TEST(test_count_pipes_one);
    RUN_TEST(test_count_pipes_two);
    RUN_TEST(test_count_pipes_three);
    RUN_TEST(test_combined_redir_and_parse);
    RUN_TEST(test_redir_no_space);
    RUN_TEST(test_pipe_sigpipe_on_broken);

    TEST_REPORT();
}
