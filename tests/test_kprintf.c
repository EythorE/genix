/*
 * Unit tests for kernel/kprintf.c — format string handling
 *
 * Strategy: capture kputc output into a buffer, then verify formatted strings.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* ---- Output capture buffer ---- */
static char capture_buf[1024];
static int capture_pos;

static void capture_reset(void)
{
    memset(capture_buf, 0, sizeof(capture_buf));
    capture_pos = 0;
}

/* ---- Mock PAL functions ---- */
void pal_console_putc(char c)
{
    if (capture_pos < (int)sizeof(capture_buf) - 1)
        capture_buf[capture_pos++] = c;
}

int pal_console_ready(void) { return 1; }
int pal_console_getc(void) { return 'x'; }

/* Prevent kernel.h from being included */
#define KERNEL_H

/* ---- Include the real kprintf.c ---- */
#include "../kernel/kprintf.c"

/* ================================================================
 * Tests
 * ================================================================ */

static void test_kputc_basic(void)
{
    capture_reset();
    kputc('A');
    ASSERT_EQ(capture_buf[0], 'A');
    ASSERT_EQ(capture_pos, 1);
}

static void test_kputc_newline(void)
{
    capture_reset();
    kputc('\n');
    /* kputc maps \n to \r\n */
    ASSERT_EQ(capture_buf[0], '\r');
    ASSERT_EQ(capture_buf[1], '\n');
    ASSERT_EQ(capture_pos, 2);
}

static void test_kputs_string(void)
{
    capture_reset();
    kputs("hello");
    ASSERT_EQ(capture_pos, 5);
    ASSERT(memcmp(capture_buf, "hello", 5) == 0);
}

static void test_kputs_with_newline(void)
{
    capture_reset();
    kputs("hi\n");
    /* "hi" + \r\n = 4 chars */
    ASSERT_EQ(capture_pos, 4);
    ASSERT(memcmp(capture_buf, "hi\r\n", 4) == 0);
}

static void test_kprintf_string(void)
{
    capture_reset();
    kprintf("hello %s!", "world");
    ASSERT_STR_EQ(capture_buf, "hello world!");
}

static void test_kprintf_null_string(void)
{
    capture_reset();
    kprintf("%s", (char *)NULL);
    ASSERT_STR_EQ(capture_buf, "(null)");
}

static void test_kprintf_decimal(void)
{
    capture_reset();
    kprintf("%d", (uint32_t)42);
    ASSERT_STR_EQ(capture_buf, "42");
}

static void test_kprintf_negative(void)
{
    capture_reset();
    kprintf("%d", (uint32_t)-1);
    ASSERT_STR_EQ(capture_buf, "-1");
}

static void test_kprintf_zero(void)
{
    capture_reset();
    kprintf("%d", (uint32_t)0);
    ASSERT_STR_EQ(capture_buf, "0");
}

static void test_kprintf_unsigned(void)
{
    capture_reset();
    kprintf("%u", (uint32_t)4000000000U);
    ASSERT_STR_EQ(capture_buf, "4000000000");
}

static void test_kprintf_hex(void)
{
    capture_reset();
    kprintf("%x", (uint32_t)0xDEAD);
    ASSERT_STR_EQ(capture_buf, "dead");
}

static void test_kprintf_hex_zero(void)
{
    capture_reset();
    kprintf("%x", (uint32_t)0);
    ASSERT_STR_EQ(capture_buf, "0");
}

static void test_kprintf_char(void)
{
    capture_reset();
    kprintf("%c", (int)'Z');
    ASSERT_EQ(capture_buf[0], 'Z');
    ASSERT_EQ(capture_pos, 1);
}

static void test_kprintf_percent(void)
{
    capture_reset();
    kprintf("100%%");
    ASSERT_STR_EQ(capture_buf, "100%");
}

static void test_kprintf_unknown_format(void)
{
    capture_reset();
    kprintf("%q");
    /* Unknown format: outputs % followed by the character */
    ASSERT_STR_EQ(capture_buf, "%q");
}

static void test_kprintf_multiple(void)
{
    capture_reset();
    kprintf("pid=%d name=%s", (uint32_t)7, "init");
    ASSERT_STR_EQ(capture_buf, "pid=7 name=init");
}

static void test_kprintf_large_number(void)
{
    capture_reset();
    kprintf("%d", (uint32_t)2147483647);  /* INT32_MAX */
    ASSERT_STR_EQ(capture_buf, "2147483647");
}

static void test_kprintf_no_format(void)
{
    capture_reset();
    kprintf("plain text");
    ASSERT_STR_EQ(capture_buf, "plain text");
}

/* ---- Main ---- */
int main(void)
{
    printf("test_kprintf:\n");

    RUN_TEST(test_kputc_basic);
    RUN_TEST(test_kputc_newline);
    RUN_TEST(test_kputs_string);
    RUN_TEST(test_kputs_with_newline);
    RUN_TEST(test_kprintf_string);
    RUN_TEST(test_kprintf_null_string);
    RUN_TEST(test_kprintf_decimal);
    RUN_TEST(test_kprintf_negative);
    RUN_TEST(test_kprintf_zero);
    RUN_TEST(test_kprintf_unsigned);
    RUN_TEST(test_kprintf_hex);
    RUN_TEST(test_kprintf_hex_zero);
    RUN_TEST(test_kprintf_char);
    RUN_TEST(test_kprintf_percent);
    RUN_TEST(test_kprintf_unknown_format);
    RUN_TEST(test_kprintf_multiple);
    RUN_TEST(test_kprintf_large_number);
    RUN_TEST(test_kprintf_no_format);

    TEST_REPORT();
}
