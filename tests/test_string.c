/*
 * Unit tests for kernel/string.c
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "testutil.h"

/* Pull in the kernel implementation under test */
#define NULL ((void *)0)
#include "../kernel/string.c"

/* --- memset --- */
static void test_memset_basic(void)
{
    char buf[16];
    memset(buf, 'A', sizeof(buf));
    for (int i = 0; i < 16; i++)
        ASSERT_EQ(buf[i], 'A');
}

static void test_memset_zero(void)
{
    char buf[8] = {1,2,3,4,5,6,7,8};
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 8; i++)
        ASSERT_EQ(buf[i], 0);
}

static void test_memset_returns_ptr(void)
{
    char buf[4];
    ASSERT(memset(buf, 0, 4) == buf);
}

/* --- memcpy --- */
static void test_memcpy_basic(void)
{
    char src[] = "hello";
    char dst[8] = {0};
    memcpy(dst, src, 6);
    ASSERT_STR_EQ(dst, "hello");
}

static void test_memcpy_returns_dest(void)
{
    char s[4], d[4];
    ASSERT(memcpy(d, s, 4) == d);
}

/* --- memcmp --- */
static void test_memcmp_equal(void)
{
    ASSERT_EQ(memcmp("abc", "abc", 3), 0);
}

static void test_memcmp_less(void)
{
    ASSERT(memcmp("abc", "abd", 3) < 0);
}

static void test_memcmp_greater(void)
{
    ASSERT(memcmp("abd", "abc", 3) > 0);
}

static void test_memcmp_partial(void)
{
    ASSERT_EQ(memcmp("abcX", "abcY", 3), 0);
}

/* --- strlen --- */
static void test_strlen_basic(void)
{
    ASSERT_EQ(strlen("hello"), 5);
}

static void test_strlen_empty(void)
{
    ASSERT_EQ(strlen(""), 0);
}

/* --- strcmp --- */
static void test_strcmp_equal(void)
{
    ASSERT_EQ(strcmp("abc", "abc"), 0);
}

static void test_strcmp_less(void)
{
    ASSERT(strcmp("abc", "abd") < 0);
}

static void test_strcmp_greater(void)
{
    ASSERT(strcmp("abd", "abc") > 0);
}

static void test_strcmp_prefix(void)
{
    ASSERT(strcmp("ab", "abc") < 0);
    ASSERT(strcmp("abc", "ab") > 0);
}

/* --- strncmp --- */
static void test_strncmp_equal_within(void)
{
    ASSERT_EQ(strncmp("abcX", "abcY", 3), 0);
}

static void test_strncmp_diff_within(void)
{
    ASSERT(strncmp("abd", "abc", 3) > 0);
}

static void test_strncmp_zero_len(void)
{
    ASSERT_EQ(strncmp("abc", "xyz", 0), 0);
}

/* --- strcpy --- */
static void test_strcpy_basic(void)
{
    char dst[16];
    strcpy(dst, "hello");
    ASSERT_STR_EQ(dst, "hello");
}

static void test_strcpy_empty(void)
{
    char dst[4] = "xxx";
    strcpy(dst, "");
    ASSERT_EQ(dst[0], '\0');
}

/* --- strncpy --- */
static void test_strncpy_basic(void)
{
    char dst[8];
    strncpy(dst, "hello", 8);
    ASSERT_STR_EQ(dst, "hello");
    /* Should zero-pad */
    ASSERT_EQ(dst[6], '\0');
    ASSERT_EQ(dst[7], '\0');
}

static void test_strncpy_truncate(void)
{
    char dst[4];
    strncpy(dst, "hello", 3);
    /* Not null-terminated when src longer than n */
    ASSERT_EQ(dst[0], 'h');
    ASSERT_EQ(dst[1], 'e');
    ASSERT_EQ(dst[2], 'l');
}

/* --- strchr --- */
static void test_strchr_found(void)
{
    const char *s = "hello";
    char *p = strchr(s, 'l');
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p - s, 2);
}

static void test_strchr_not_found(void)
{
    ASSERT_NULL(strchr("hello", 'z'));
}

static void test_strchr_nul(void)
{
    const char *s = "hi";
    char *p = strchr(s, '\0');
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(*p, '\0');
    ASSERT_EQ(p - s, 2);
}

/* --- strrchr --- */
static void test_strrchr_found(void)
{
    const char *s = "hello";
    char *p = strrchr(s, 'l');
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p - s, 3);  /* last 'l' */
}

static void test_strrchr_not_found(void)
{
    ASSERT_NULL(strrchr("hello", 'z'));
}

static void test_strrchr_nul(void)
{
    const char *s = "hi";
    char *p = strrchr(s, '\0');
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p - s, 2);
}

/* --- memmove overlap tests (tests assembly memmove correctness) --- */

static void test_memmove_forward_overlap(void)
{
    /* src < dst, overlapping: copy backward needed */
    char buf[16] = "ABCDEFGHIJKLMNOP";
    memmove(buf + 2, buf, 10);
    /* buf[2..11] should be "ABCDEFGHIJ", original buf[0..1] untouched */
    ASSERT_EQ(buf[2], 'A');
    ASSERT_EQ(buf[3], 'B');
    ASSERT_EQ(buf[11], 'J');
}

static void test_memmove_backward_overlap(void)
{
    /* dst < src, overlapping: forward copy safe */
    char buf[16] = "ABCDEFGHIJKLMNOP";
    memmove(buf, buf + 3, 10);
    ASSERT_EQ(buf[0], 'D');
    ASSERT_EQ(buf[1], 'E');
    ASSERT_EQ(buf[9], 'M');
}

static void test_memmove_no_overlap(void)
{
    char src[] = "hello";
    char dst[8] = {0};
    memmove(dst, src, 6);
    ASSERT_STR_EQ(dst, "hello");
}

static void test_memmove_zero_length(void)
{
    char buf[4] = "ABC";
    memmove(buf + 1, buf, 0);
    ASSERT_STR_EQ(buf, "ABC");
}

static void test_memmove_returns_dest(void)
{
    char s[8], d[8];
    ASSERT(memmove(d, s, 4) == d);
}

/* --- large buffer tests (exercises MOVEM.L paths on 68000) --- */

static void test_memcpy_large(void)
{
    /* 512 bytes — the common block size, triggers bulk copy path */
    char src[512], dst[512];
    for (int i = 0; i < 512; i++)
        src[i] = (char)(i & 0xFF);
    memcpy(dst, src, 512);
    ASSERT_EQ(memcmp(dst, src, 512), 0);
}

static void test_memcpy_medium(void)
{
    /* 48 bytes — between byte and MOVEM.L thresholds */
    char src[48], dst[48];
    for (int i = 0; i < 48; i++)
        src[i] = (char)(i + 0x41);
    memcpy(dst, src, 48);
    ASSERT_EQ(memcmp(dst, src, 48), 0);
}

static void test_memset_large(void)
{
    char buf[512];
    memset(buf, 0xAA, 512);
    for (int i = 0; i < 512; i++)
        ASSERT_EQ((unsigned char)buf[i], 0xAA);
}

static void test_memset_zero_large(void)
{
    char buf[256];
    memset(buf, 0xFF, 256);  /* poison */
    memset(buf, 0, 256);
    for (int i = 0; i < 256; i++)
        ASSERT_EQ(buf[i], 0);
}

static void test_memcpy_one_byte(void)
{
    char src = 'X', dst = 0;
    memcpy(&dst, &src, 1);
    ASSERT_EQ(dst, 'X');
}

static void test_memset_one_byte(void)
{
    char buf = 0;
    memset(&buf, 'Z', 1);
    ASSERT_EQ(buf, 'Z');
}

static void test_memcpy_sentinel(void)
{
    /* Verify memcpy doesn't write past the end */
    char buf[20];
    memset(buf, 0xCC, 20);
    char src[10];
    memset(src, 'A', 10);
    memcpy(buf + 2, src, 10);
    ASSERT_EQ((unsigned char)buf[0], 0xCC);
    ASSERT_EQ((unsigned char)buf[1], 0xCC);
    ASSERT_EQ(buf[2], 'A');
    ASSERT_EQ(buf[11], 'A');
    ASSERT_EQ((unsigned char)buf[12], 0xCC);
    ASSERT_EQ((unsigned char)buf[19], 0xCC);
}

/* --- main --- */
int main(void)
{
    printf("test_string:\n");

    RUN_TEST(test_memset_basic);
    RUN_TEST(test_memset_zero);
    RUN_TEST(test_memset_returns_ptr);
    RUN_TEST(test_memcpy_basic);
    RUN_TEST(test_memcpy_returns_dest);
    RUN_TEST(test_memcmp_equal);
    RUN_TEST(test_memcmp_less);
    RUN_TEST(test_memcmp_greater);
    RUN_TEST(test_memcmp_partial);
    RUN_TEST(test_strlen_basic);
    RUN_TEST(test_strlen_empty);
    RUN_TEST(test_strcmp_equal);
    RUN_TEST(test_strcmp_less);
    RUN_TEST(test_strcmp_greater);
    RUN_TEST(test_strcmp_prefix);
    RUN_TEST(test_strncmp_equal_within);
    RUN_TEST(test_strncmp_diff_within);
    RUN_TEST(test_strncmp_zero_len);
    RUN_TEST(test_strcpy_basic);
    RUN_TEST(test_strcpy_empty);
    RUN_TEST(test_strncpy_basic);
    RUN_TEST(test_strncpy_truncate);
    RUN_TEST(test_strchr_found);
    RUN_TEST(test_strchr_not_found);
    RUN_TEST(test_strchr_nul);
    RUN_TEST(test_strrchr_found);
    RUN_TEST(test_strrchr_not_found);
    RUN_TEST(test_strrchr_nul);

    /* memmove overlap tests */
    RUN_TEST(test_memmove_forward_overlap);
    RUN_TEST(test_memmove_backward_overlap);
    RUN_TEST(test_memmove_no_overlap);
    RUN_TEST(test_memmove_zero_length);
    RUN_TEST(test_memmove_returns_dest);

    /* large buffer tests (exercises 68000 MOVEM.L paths on cross-build) */
    RUN_TEST(test_memcpy_large);
    RUN_TEST(test_memcpy_medium);
    RUN_TEST(test_memset_large);
    RUN_TEST(test_memset_zero_large);
    RUN_TEST(test_memcpy_one_byte);
    RUN_TEST(test_memset_one_byte);
    RUN_TEST(test_memcpy_sentinel);

    TEST_REPORT();
}
