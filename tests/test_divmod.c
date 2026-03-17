/*
 * Unit tests for kernel/divmod.S DIVU.W fast path logic
 *
 * The 68000 assembly in divmod.S has a fast path for operands that
 * both fit in 16 bits (uses DIVU.W instruction). We can't test the
 * assembly on the host, but we can test the equivalent C logic to
 * verify correctness of the fast-path condition and the fallback.
 *
 * This mirrors the algorithm:
 * - if (dividend | divisor) has zero high word → use 16-bit divide
 * - otherwise → shift-and-subtract (slow path)
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* --- C equivalent of the divmod.S fast path logic --- */

static uint32_t udiv(uint32_t dividend, uint32_t divisor)
{
    if (divisor == 0) return 0;  /* undefined, kernel traps */

    /* Fast path: both fit in 16 bits */
    uint32_t combined = dividend | divisor;
    if ((combined >> 16) == 0) {
        /* DIVU.W: 32-bit / 16-bit → quotient in low word */
        return (uint16_t)(dividend / (uint16_t)divisor);
    }

    /* Slow path: shift-and-subtract */
    uint32_t d2 = dividend;
    uint32_t d3 = divisor;
    uint32_t q = 0;
    uint32_t shifted = d3;
    int bits = 0;

    while (shifted <= d2 && !(shifted & 0x80000000u)) {
        shifted <<= 1;
        bits++;
    }

    for (int i = 0; i <= bits; i++) {
        q <<= 1;
        if (d2 >= shifted) {
            d2 -= shifted;
            q |= 1;
        }
        shifted >>= 1;
    }
    return q;
}

static uint32_t umod(uint32_t dividend, uint32_t divisor)
{
    if (divisor == 0) return 0;

    /* Fast path: both fit in 16 bits */
    uint32_t combined = dividend | divisor;
    if ((combined >> 16) == 0) {
        /* DIVU.W: remainder in high word of result */
        return (uint16_t)(dividend % (uint16_t)divisor);
    }

    /* Slow path: compute via quotient */
    uint32_t q = udiv(dividend, divisor);
    return dividend - q * divisor;
}

/* --- Tests for the fast path (both operands < 65536) --- */

static void test_udiv_small(void)
{
    ASSERT_EQ(udiv(100, 7), 14);
    ASSERT_EQ(udiv(0, 7), 0);
    ASSERT_EQ(udiv(1, 1), 1);
    ASSERT_EQ(udiv(255, 1), 255);
    ASSERT_EQ(udiv(65535, 1), 65535);
    ASSERT_EQ(udiv(65535, 65535), 1);
    ASSERT_EQ(udiv(100, 100), 1);
    ASSERT_EQ(udiv(99, 100), 0);
    ASSERT_EQ(udiv(512, 512), 1);
}

static void test_umod_small(void)
{
    ASSERT_EQ(umod(100, 7), 2);
    ASSERT_EQ(umod(0, 7), 0);
    ASSERT_EQ(umod(10, 3), 1);
    ASSERT_EQ(umod(255, 256), 255);
    ASSERT_EQ(umod(65535, 10), 5);
    ASSERT_EQ(umod(512, 21), 512 % 21);  /* INODES_PER_BLK */
}

/* --- Tests for the slow path (at least one operand >= 65536) --- */

static void test_udiv_large(void)
{
    ASSERT_EQ(udiv(0x10000, 1), 0x10000);
    ASSERT_EQ(udiv(0x10000, 0x10000), 1);
    ASSERT_EQ(udiv(0xFFFFFFFF, 1), 0xFFFFFFFF);
    ASSERT_EQ(udiv(1000000, 1000), 1000);
    ASSERT_EQ(udiv(0x80000000, 2), 0x40000000);
    ASSERT_EQ(udiv(0xFFFFFFFF, 0xFFFFFFFF), 1);
    ASSERT_EQ(udiv(0xFFFFFFFF, 0x10000), 0xFFFF);
}

static void test_umod_large(void)
{
    ASSERT_EQ(umod(0x10000, 3), 1);      /* 65536 % 3 = 1 */
    ASSERT_EQ(umod(0xFFFFFFFF, 10), 5);  /* 4294967295 % 10 = 5 */
    ASSERT_EQ(umod(1000000, 1000), 0);
    ASSERT_EQ(umod(1000001, 1000), 1);
    ASSERT_EQ(umod(0xFFFFFFFF, 0xFFFFFFFF), 0);
}

/* --- Fast-path boundary: one operand at 0xFFFF, other just above --- */

static void test_udiv_boundary(void)
{
    /* Both exactly 0xFFFF → fast path */
    ASSERT_EQ(udiv(0xFFFF, 0xFFFF), 1);
    /* Dividend = 0x10000, divisor = 1 → slow path (dividend > 16 bits) */
    ASSERT_EQ(udiv(0x10000, 1), 0x10000);
    /* Dividend = 0xFFFF, divisor = 0x10000 → slow path (divisor > 16 bits) */
    ASSERT_EQ(udiv(0xFFFF, 0x10000), 0);
    /* Dividend = 0x10001, divisor = 0x10001 → slow path */
    ASSERT_EQ(udiv(0x10001, 0x10001), 1);
}

static void test_umod_boundary(void)
{
    ASSERT_EQ(umod(0xFFFF, 0xFFFF), 0);
    ASSERT_EQ(umod(0x10000, 3), 1);
    ASSERT_EQ(umod(0xFFFF, 0x10000), 0xFFFF);
}

/* --- Kernel-relevant values --- */

static void test_kernel_divisions(void)
{
    /* INODES_PER_BLK = 21 */
    ASSERT_EQ(udiv(100, 21), 4);
    ASSERT_EQ(umod(100, 21), 16);

    /* BLOCK_SIZE = 512 (power of 2 — GCC optimizes, but divmod should work) */
    ASSERT_EQ(udiv(1024, 512), 2);
    ASSERT_EQ(umod(1023, 512), 511);

    /* kprintf base 10 */
    ASSERT_EQ(udiv(12345, 10), 1234);
    ASSERT_EQ(umod(12345, 10), 5);

    /* kprintf base 16 */
    ASSERT_EQ(udiv(0xFF, 16), 15);
    ASSERT_EQ(umod(0xFF, 16), 15);
}

/* --- Verify consistency: dividend == quotient * divisor + remainder --- */

static void test_divmod_consistency(void)
{
    /* A spread of test values */
    uint32_t dividends[] = { 0, 1, 7, 100, 255, 256, 1000, 65535,
                             65536, 100000, 0x7FFFFFFF, 0xFFFFFFFF };
    uint32_t divisors[] = { 1, 2, 3, 7, 10, 16, 21, 100, 255, 256,
                            512, 65535, 65536, 0x7FFFFFFF };
    int nd = sizeof(dividends) / sizeof(dividends[0]);
    int nv = sizeof(divisors) / sizeof(divisors[0]);

    for (int i = 0; i < nd; i++) {
        for (int j = 0; j < nv; j++) {
            uint32_t a = dividends[i];
            uint32_t b = divisors[j];
            uint32_t q = udiv(a, b);
            uint32_t r = umod(a, b);
            /* Verify: a == q * b + r, and r < b */
            ASSERT_EQ(q * b + r, a);
            ASSERT(r < b);
        }
    }
}

/* --- Main --- */
int main(void)
{
    printf("test_divmod:\n");

    RUN_TEST(test_udiv_small);
    RUN_TEST(test_umod_small);
    RUN_TEST(test_udiv_large);
    RUN_TEST(test_umod_large);
    RUN_TEST(test_udiv_boundary);
    RUN_TEST(test_umod_boundary);
    RUN_TEST(test_kernel_divisions);
    RUN_TEST(test_divmod_consistency);

    TEST_REPORT();
}
