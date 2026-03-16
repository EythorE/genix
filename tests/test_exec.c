/*
 * Unit tests for kernel/exec.c — binary header validation and stack setup
 *
 * These test the pure logic on the host (no 68000 needed).
 * Stack layout tests use host-native pointer sizes (uintptr_t).
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* Re-define kernel constants for host testing */
#define USER_BASE   0x040000
#define USER_TOP    0x0F0000
#define USER_SIZE   (USER_TOP - USER_BASE)
#define USER_STACK_DEFAULT 4096
#define GENIX_MAGIC 0x47454E58
#define GENIX_HDR_SIZE 32
#define ENOEXEC     8
#define ENOMEM      12

struct genix_header {
    uint32_t magic;
    uint32_t load_size;
    uint32_t bss_size;
    uint32_t entry;
    uint32_t stack_size;
    uint32_t flags;
    uint32_t text_size;
    uint32_t reloc_count;
};

/* Re-implement exec_mem_need from exec.c for host testing */
static uint32_t exec_mem_need(const struct genix_header *hdr)
{
    uint32_t stack = hdr->stack_size ? hdr->stack_size : USER_STACK_DEFAULT;
    uint32_t reloc_bytes = hdr->reloc_count * 4;
    uint32_t effective_bss = hdr->bss_size;
    if (reloc_bytes > effective_bss)
        effective_bss = reloc_bytes;
    return hdr->load_size + effective_bss + stack;
}

/* Re-implement the validation function from exec.c for host testing */
static int exec_validate_header(const struct genix_header *hdr, uint32_t region_size)
{
    if (hdr->magic != GENIX_MAGIC)
        return -ENOEXEC;

    if (hdr->load_size == 0)
        return -ENOEXEC;

    /* Entry must be within loaded region (0-based offset) */
    if (hdr->entry >= hdr->load_size)
        return -ENOEXEC;

    /* text_size must not exceed load_size */
    if (hdr->text_size > hdr->load_size)
        return -ENOEXEC;

    uint32_t total = exec_mem_need(hdr);
    if (total > region_size)
        return -ENOMEM;

    return 0;
}

/* --- Header validation tests --- */

static void test_header_valid(void)
{
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 1024,
        .bss_size = 256,
        .entry = 0,       /* 0-based offset */
        .stack_size = 4096,
        .flags = 0
    };
    ASSERT_EQ(exec_validate_header(&hdr, USER_SIZE), 0);
}

static void test_header_bad_magic(void)
{
    struct genix_header hdr = {
        .magic = 0xDEADBEEF,
        .load_size = 1024,
        .bss_size = 0,
        .entry = 0,
    };
    ASSERT_EQ(exec_validate_header(&hdr, USER_SIZE), -ENOEXEC);
}

static void test_header_zero_load(void)
{
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 0,
        .bss_size = 0,
        .entry = 0,
    };
    ASSERT_EQ(exec_validate_header(&hdr, USER_SIZE), -ENOEXEC);
}

static void test_header_too_large(void)
{
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = USER_SIZE + 1,
        .bss_size = 0,
        .entry = 0,
        .stack_size = 0,
    };
    ASSERT_EQ(exec_validate_header(&hdr, USER_SIZE), -ENOMEM);
}

static void test_header_bss_too_large(void)
{
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 1024,
        .bss_size = USER_SIZE,
        .entry = 0,
        .stack_size = 4096,
    };
    ASSERT_EQ(exec_validate_header(&hdr, USER_SIZE), -ENOMEM);
}

static void test_header_entry_past_load(void)
{
    /* Entry == load_size: one past the loaded region */
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 1024,
        .bss_size = 0,
        .entry = 1024,
    };
    ASSERT_EQ(exec_validate_header(&hdr, USER_SIZE), -ENOEXEC);
}

static void test_header_entry_within_load(void)
{
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 1024,
        .bss_size = 0,
        .entry = 512,
    };
    ASSERT_EQ(exec_validate_header(&hdr, USER_SIZE), 0);
}

static void test_header_default_stack(void)
{
    /* Test that stack_size=0 uses default (4096) */
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = USER_SIZE - USER_STACK_DEFAULT,
        .bss_size = 0,
        .entry = 0,
        .stack_size = 0,  /* should use USER_STACK_DEFAULT */
    };
    ASSERT_EQ(exec_validate_header(&hdr, USER_SIZE), 0);

    /* One more byte should fail */
    hdr.load_size = USER_SIZE - USER_STACK_DEFAULT + 1;
    ASSERT_EQ(exec_validate_header(&hdr, USER_SIZE), -ENOMEM);
}

static void test_header_size(void)
{
    /* Ensure the header struct is exactly 32 bytes */
    ASSERT_EQ(sizeof(struct genix_header), 32);
}

/* --- Stack setup tests ---
 *
 * The real kernel uses 32-bit pointers (68000). On a 64-bit host, we use
 * uintptr_t throughout. We test the LAYOUT logic (argc, argv ordering,
 * alignment) not the exact pointer sizes.
 */

static uint8_t fake_stack[8192] __attribute__((aligned(16)));

/*
 * Host-compatible stack setup that mirrors exec_setup_stack logic.
 * Uses native pointers instead of uint32_t.
 * Returns a pointer to the start of the stack frame.
 */
struct stack_frame {
    int argc;
    char **argv;        /* points to argv[0] */
    char **envp;        /* points to envp[0] */
    uintptr_t sp;       /* simulated SP value */
};

static struct stack_frame test_setup_stack(const char *path, const char **argv)
{
    struct stack_frame result;

    /* Count args */
    int argc = 0;
    if (argv) {
        while (argv[argc])
            argc++;
    }
    if (argc == 0)
        argc = 1;

    result.argc = argc;

    /* Calculate space needed */
    size_t str_size = strlen(path) + 1;
    if (argv) {
        for (int i = 1; i < argc; i++)
            str_size += strlen(argv[i]) + 1;
    }
    str_size = (str_size + 3) & ~(size_t)3;  /* align */

    /* Layout: [argc][argv[0]..argv[N]][NULL][NULL(envp)][strings] */
    size_t ptr_size = sizeof(uintptr_t);  /* 4 on 68000, 8 on host */
    size_t setup_size = ptr_size                      /* argc */
                      + (argc + 1) * ptr_size         /* argv + NULL */
                      + ptr_size                       /* envp NULL */
                      + str_size;                      /* strings */

    uintptr_t stack_top = (uintptr_t)(fake_stack + sizeof(fake_stack));
    uintptr_t sp = (stack_top - setup_size) & ~(uintptr_t)3;
    result.sp = sp;

    /* Write the frame */
    uintptr_t *frame = (uintptr_t *)sp;
    frame[0] = argc;

    /* String area starts after all pointers */
    char *str_pos = (char *)(sp + ptr_size + (argc + 1) * ptr_size + ptr_size);

    /* argv[0] = path */
    frame[1] = (uintptr_t)str_pos;
    strcpy(str_pos, path);
    str_pos += strlen(path) + 1;

    /* argv[1..] */
    if (argv) {
        for (int i = 1; i < argc; i++) {
            frame[1 + i] = (uintptr_t)str_pos;
            strcpy(str_pos, argv[i]);
            str_pos += strlen(argv[i]) + 1;
        }
    }

    /* argv[argc] = NULL */
    frame[1 + argc] = 0;
    /* envp[0] = NULL */
    frame[2 + argc] = 0;

    result.argv = (char **)(frame + 1);
    result.envp = (char **)(frame + 2 + argc);

    return result;
}

static void test_stack_no_args(void)
{
    memset(fake_stack, 0, sizeof(fake_stack));
    struct stack_frame f = test_setup_stack("/bin/hello", NULL);

    /* SP must be 4-byte aligned */
    ASSERT_EQ(f.sp & 3, 0);

    /* argc = 1 */
    ASSERT_EQ(f.argc, 1);

    /* argv[0] should be "/bin/hello" */
    ASSERT_NOT_NULL(f.argv[0]);
    ASSERT_STR_EQ(f.argv[0], "/bin/hello");

    /* argv[1] should be NULL */
    ASSERT_NULL(f.argv[1]);
}

static void test_stack_with_args(void)
{
    memset(fake_stack, 0, sizeof(fake_stack));
    const char *argv[] = { "/bin/echo", "hello", "world", NULL };
    struct stack_frame f = test_setup_stack("/bin/echo", argv);

    ASSERT_EQ(f.sp & 3, 0);
    ASSERT_EQ(f.argc, 3);

    /* argv[0] = "/bin/echo" (path, always first) */
    ASSERT_STR_EQ(f.argv[0], "/bin/echo");
    /* argv[1] = "hello" */
    ASSERT_STR_EQ(f.argv[1], "hello");
    /* argv[2] = "world" */
    ASSERT_STR_EQ(f.argv[2], "world");
    /* argv[3] = NULL */
    ASSERT_NULL(f.argv[3]);
}

static void test_stack_alignment(void)
{
    /* Test with odd-length strings to exercise alignment */
    memset(fake_stack, 0, sizeof(fake_stack));
    const char *argv[] = { "/bin/x", "a", NULL };
    struct stack_frame f = test_setup_stack("/bin/x", argv);
    ASSERT_EQ(f.sp & 3, 0);  /* must be 4-byte aligned */
    ASSERT_EQ(f.sp & 1, 0);  /* must be even (68000 requirement) */
}

static void test_stack_single_arg(void)
{
    memset(fake_stack, 0, sizeof(fake_stack));
    const char *argv[] = { "/bin/ls", NULL };
    struct stack_frame f = test_setup_stack("/bin/ls", argv);

    ASSERT_EQ(f.argc, 1);
    ASSERT_STR_EQ(f.argv[0], "/bin/ls");
    ASSERT_NULL(f.argv[1]);
}

static void test_stack_many_args(void)
{
    memset(fake_stack, 0, sizeof(fake_stack));
    const char *argv[] = { "/bin/cmd", "a", "bb", "ccc", "dddd", NULL };
    struct stack_frame f = test_setup_stack("/bin/cmd", argv);

    ASSERT_EQ(f.argc, 5);
    ASSERT_STR_EQ(f.argv[0], "/bin/cmd");
    ASSERT_STR_EQ(f.argv[1], "a");
    ASSERT_STR_EQ(f.argv[2], "bb");
    ASSERT_STR_EQ(f.argv[3], "ccc");
    ASSERT_STR_EQ(f.argv[4], "dddd");
    ASSERT_NULL(f.argv[5]);
}

/* --- Main --- */

int main(void)
{
    printf("test_exec:\n");

    RUN_TEST(test_header_valid);
    RUN_TEST(test_header_bad_magic);
    RUN_TEST(test_header_zero_load);
    RUN_TEST(test_header_too_large);
    RUN_TEST(test_header_bss_too_large);
    RUN_TEST(test_header_entry_past_load);
    RUN_TEST(test_header_entry_within_load);
    RUN_TEST(test_header_default_stack);
    RUN_TEST(test_header_size);
    RUN_TEST(test_stack_no_args);
    RUN_TEST(test_stack_with_args);
    RUN_TEST(test_stack_alignment);
    RUN_TEST(test_stack_single_arg);
    RUN_TEST(test_stack_many_args);

    TEST_REPORT();
}
