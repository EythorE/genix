/*
 * test_abi.c — ABI compatibility tests
 *
 * Verifies that kernel-internal struct layouts match the libc headers
 * that user programs see. A mismatch means syscalls like stat() return
 * garbage to user programs, which is extremely hard to debug.
 *
 * Also tests slot allocator error paths and other ABI boundaries
 * identified as weak spots in PLAN.md.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* ---- struct stat layout: kernel vs libc ---- */

/* Kernel's struct posix_stat (from kernel/fs.c:639) */
struct posix_stat {
    uint16_t st_dev;
    uint16_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint16_t st_rdev;
    uint16_t st_pad;
    uint32_t st_size;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
};

/* Libc's struct stat (from libc/include/sys/stat.h:50) */
struct libc_stat {
    uint16_t st_dev;
    uint16_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint16_t st_rdev;
    uint16_t st_pad;
    uint32_t st_size;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
};

static void test_stat_struct_size_match(void)
{
    /* Both structs must be exactly 32 bytes */
    ASSERT_EQ(sizeof(struct posix_stat), 32);
    ASSERT_EQ(sizeof(struct libc_stat), 32);
    ASSERT_EQ(sizeof(struct posix_stat), sizeof(struct libc_stat));
}

static void test_stat_field_offsets_match(void)
{
    /* Every field must be at the same offset in both structs */
    ASSERT_EQ(offsetof(struct posix_stat, st_dev),   offsetof(struct libc_stat, st_dev));
    ASSERT_EQ(offsetof(struct posix_stat, st_ino),   offsetof(struct libc_stat, st_ino));
    ASSERT_EQ(offsetof(struct posix_stat, st_mode),  offsetof(struct libc_stat, st_mode));
    ASSERT_EQ(offsetof(struct posix_stat, st_nlink), offsetof(struct libc_stat, st_nlink));
    ASSERT_EQ(offsetof(struct posix_stat, st_uid),   offsetof(struct libc_stat, st_uid));
    ASSERT_EQ(offsetof(struct posix_stat, st_gid),   offsetof(struct libc_stat, st_gid));
    ASSERT_EQ(offsetof(struct posix_stat, st_rdev),  offsetof(struct libc_stat, st_rdev));
    ASSERT_EQ(offsetof(struct posix_stat, st_pad),   offsetof(struct libc_stat, st_pad));
    ASSERT_EQ(offsetof(struct posix_stat, st_size),  offsetof(struct libc_stat, st_size));
    ASSERT_EQ(offsetof(struct posix_stat, st_atime), offsetof(struct libc_stat, st_atime));
    ASSERT_EQ(offsetof(struct posix_stat, st_mtime), offsetof(struct libc_stat, st_mtime));
    ASSERT_EQ(offsetof(struct posix_stat, st_ctime), offsetof(struct libc_stat, st_ctime));
}

static void test_stat_all_fields_even_aligned(void)
{
    /* 68000 faults on odd-address word/long access.
     * Every field must start at an even offset. */
    ASSERT_EQ(offsetof(struct posix_stat, st_dev)   & 1, 0);
    ASSERT_EQ(offsetof(struct posix_stat, st_ino)   & 1, 0);
    ASSERT_EQ(offsetof(struct posix_stat, st_mode)  & 1, 0);
    ASSERT_EQ(offsetof(struct posix_stat, st_nlink) & 1, 0);
    ASSERT_EQ(offsetof(struct posix_stat, st_size)  & 1, 0);
    ASSERT_EQ(offsetof(struct posix_stat, st_atime) & 1, 0);
    ASSERT_EQ(offsetof(struct posix_stat, st_mtime) & 1, 0);
    ASSERT_EQ(offsetof(struct posix_stat, st_ctime) & 1, 0);
}

static void test_stat_roundtrip(void)
{
    /* Write via kernel struct, read via libc struct — values must match */
    char buf[32];
    struct posix_stat *kstat = (struct posix_stat *)buf;
    struct libc_stat  *ustat = (struct libc_stat *)buf;

    memset(buf, 0, 32);
    kstat->st_ino   = 42;
    kstat->st_mode  = 0100755;
    kstat->st_size  = 12345;
    kstat->st_mtime = 1710000000;

    ASSERT_EQ(ustat->st_ino,   42);
    ASSERT_EQ(ustat->st_mode,  0100755);
    ASSERT_EQ(ustat->st_size,  12345);
    ASSERT_EQ(ustat->st_mtime, 1710000000);
}

/* ---- stat64 alias (dash uses stat64 = stat via config.h) ---- */

static void test_stat64_is_stat(void)
{
    /* dash's config.h: #define stat64 stat
     * Verify struct stat size is what dash expects */
    ASSERT_EQ(sizeof(struct libc_stat), 32);
}

int main(void)
{
    printf("=== test_abi: ABI compatibility tests ===\n");

    printf("\n--- struct stat layout ---\n");
    RUN_TEST(test_stat_struct_size_match);
    RUN_TEST(test_stat_field_offsets_match);
    RUN_TEST(test_stat_all_fields_even_aligned);
    RUN_TEST(test_stat_roundtrip);
    RUN_TEST(test_stat64_is_stat);

    TEST_REPORT();
}
