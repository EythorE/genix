/*
 * Minimal test framework — no dependencies beyond libc
 */
#ifndef TESTUTIL_H
#define TESTUTIL_H

#include <stdio.h>
#include <stdlib.h>

static int test_pass = 0;
static int test_fail = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        test_fail++; \
    } else { \
        test_pass++; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "  FAIL: %s:%d: %s == %lld, expected %lld\n", \
                __FILE__, __LINE__, #a, _a, _b); \
        test_fail++; \
    } else { \
        test_pass++; \
    } \
} while (0)

#define ASSERT_STR_EQ(a, b) do { \
    const char *_a = (a), *_b = (b); \
    if (strcmp(_a, _b) != 0) { \
        fprintf(stderr, "  FAIL: %s:%d: \"%s\" != \"%s\"\n", \
                __FILE__, __LINE__, _a, _b); \
        test_fail++; \
    } else { \
        test_pass++; \
    } \
} while (0)

#define ASSERT_NULL(p) ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)

#define RUN_TEST(fn) do { \
    printf("  %s...\n", #fn); \
    fn(); \
} while (0)

#define TEST_REPORT() do { \
    printf("%d passed, %d failed\n", test_pass, test_fail); \
    return test_fail ? 1 : 0; \
} while (0)

#endif /* TESTUTIL_H */
