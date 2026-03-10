/*
 * Unit tests for kernel/fs.c — minifs filesystem
 *
 * Strategy: mock the buffer cache (bread/bwrite/brelse) with an in-memory
 * block array, then exercise the real fs.c logic.  We include fs.c directly
 * after providing all the stubs it needs.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "testutil.h"

/* ---- Constants from kernel.h (must precede fs.c include) ---- */

#define BLOCK_SIZE  1024
#define MAXINODE    128
#define NAME_MAX    30
#define PATH_MAX    256

#define EPERM        1
#define ENOENT       2
#define EIO          5
#define ENOEXEC      8
#define EEXIST      17
#define ENOTDIR     20
#define EISDIR      21
#define EINVAL      22
#define ENOSPC      28
#define ENOTEMPTY   39

#define FT_FREE     0
#define FT_FILE     1
#define FT_DIR      2
#define FT_DEV      3

/* ---- Structures from kernel.h ---- */

struct superblock {
    uint32_t magic;
    uint16_t block_size;
    uint16_t nblocks;
    uint16_t ninodes;
    uint16_t free_list;
    uint16_t free_inodes;
    uint16_t pad;
    uint32_t mtime;
};

struct disk_inode {
    uint8_t  type;
    uint8_t  nlink;
    uint8_t  dev_major;
    uint8_t  dev_minor;
    uint32_t size;
    uint32_t mtime;
    uint16_t direct[12];
    uint16_t indirect;
    uint8_t  pad[10];
};
/* 48 bytes */

struct dirent_disk {
    uint16_t inode;
    char     name[NAME_MAX];
};
/* 32 bytes */

struct inode {
    uint16_t inum;
    uint8_t  type;
    uint8_t  nlink;
    uint8_t  dev_major;
    uint8_t  dev_minor;
    uint32_t size;
    uint32_t mtime;
    uint16_t direct[12];
    uint16_t indirect;
    uint8_t  refcount;
    uint8_t  dirty;
};

struct buf {
    uint16_t blockno;
    uint8_t  dev;
    uint8_t  dirty;
    uint8_t  valid;
    uint8_t  _pad[3];
    uint8_t  data[BLOCK_SIZE];
};

/* Minimal proc stub for fs_namei (curproc->cwd) */
struct proc {
    uint16_t cwd;
};

struct proc *curproc = NULL;

/* ---- Mock buffer cache ---- */

#define MOCK_NBLOCKS 64
static uint8_t mock_disk[MOCK_NBLOCKS][BLOCK_SIZE];
static struct buf mock_buf;  /* single shared buffer */
static int bread_count, bwrite_count;

struct buf *bread(int dev, uint16_t blockno)
{
    (void)dev;
    bread_count++;
    mock_buf.blockno = blockno;
    mock_buf.dev = 0;
    mock_buf.dirty = 0;
    mock_buf.valid = 1;
    if (blockno < MOCK_NBLOCKS)
        memcpy(mock_buf.data, mock_disk[blockno], BLOCK_SIZE);
    else
        memset(mock_buf.data, 0, BLOCK_SIZE);
    return &mock_buf;
}

void bwrite(struct buf *b)
{
    bwrite_count++;
    if (b->blockno < MOCK_NBLOCKS)
        memcpy(mock_disk[b->blockno], b->data, BLOCK_SIZE);
}

void brelse(struct buf *b)
{
    (void)b;
}

/* kprintf / kputs stubs */
void kputs(const char *s) { (void)s; }
void kprintf(const char *fmt, ...) { (void)fmt; }

/* pal_timer_ticks stub */
uint32_t pal_timer_ticks(void) { return 42; }

/* strncpy from C library is fine on host */

/* Prevent kernel.h from being re-included by fs.c */
#define KERNEL_H

/* Forward declaration (fs_iput calls fs_iupdate before its definition) */
void fs_iupdate(struct inode *ip);

/* ---- Include the real fs.c ---- */
#include "../kernel/fs.c"

/* ---- Helper: build a minimal filesystem in mock_disk ---- */

#define TEST_NINODES   42
#define TEST_NBLOCKS   MOCK_NBLOCKS

/*
 * Layout:
 *   Block 0:     superblock
 *   Block 1-2:   inode table (42 inodes, 21 per block)
 *   Block 3:     free list head → 4 → 5 → ... → (TEST_NBLOCKS-1) → 0
 */
static void build_test_fs(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));

    /* Superblock */
    struct superblock *s = (struct superblock *)mock_disk[0];
    s->magic = 0x4D494E49;  /* "MINI" */
    s->block_size = BLOCK_SIZE;
    s->nblocks = TEST_NBLOCKS;
    s->ninodes = TEST_NINODES;
    s->free_list = 3;
    s->free_inodes = TEST_NINODES - 1;  /* inode 1 = root */

    /* Set up free block chain: 3 → 4 → 5 → ... */
    for (int i = 3; i < TEST_NBLOCKS; i++) {
        uint16_t next = (i + 1 < TEST_NBLOCKS) ? (uint16_t)(i + 1) : 0;
        mock_disk[i][0] = (next >> 8) & 0xFF;
        mock_disk[i][1] = next & 0xFF;
    }

    /* Root inode (inode 1) — type=DIR, nlink=2 */
    /* inode 1 is at block 1, offset 0 */
    struct disk_inode *root = (struct disk_inode *)mock_disk[1];
    root->type = FT_DIR;
    root->nlink = 2;
    root->size = 0;  /* empty root for now */

    /* Reset fs state */
    fs_ready = 0;
    memset(inode_cache, 0, sizeof(inode_cache));
    bread_count = 0;
    bwrite_count = 0;
}

/* Add a directory entry to the root dir, allocating a data block if needed */
static void add_root_entry(const char *name, uint16_t inum)
{
    struct disk_inode *root = (struct disk_inode *)mock_disk[1];

    /* If root has no data block yet, give it one (block 3) */
    if (root->direct[0] == 0) {
        root->direct[0] = 3;
        /* Clear the block (free-chain data looks like non-zero inodes
         * on little-endian hosts) */
        memset(mock_disk[3], 0, BLOCK_SIZE);
        /* Remove block 3 from free list: free_list → 4 */
        struct superblock *s = (struct superblock *)mock_disk[0];
        s->free_list = 4;
    }

    uint16_t dblk = root->direct[0];
    struct dirent_disk *de = (struct dirent_disk *)mock_disk[dblk];

    /* Find empty slot */
    int slots = BLOCK_SIZE / sizeof(struct dirent_disk);  /* 32 */
    for (int i = 0; i < slots; i++) {
        if (de[i].inode == 0) {
            de[i].inode = inum;
            strncpy(de[i].name, name, NAME_MAX - 1);
            de[i].name[NAME_MAX - 1] = '\0';
            root->size += sizeof(struct dirent_disk);
            return;
        }
    }
}

/* Create a file inode in the inode table */
static void create_test_inode(uint16_t inum, uint8_t type, uint32_t size)
{
    /* INODES_PER_BLK = 21 */
    uint16_t blk = 1 + (inum - 1) / 21;
    uint16_t off = ((inum - 1) % 21);
    struct disk_inode *di = (struct disk_inode *)mock_disk[blk] + off;
    di->type = type;
    di->nlink = 1;
    di->size = size;
}

/* ================================================================
 * Tests
 * ================================================================ */

/* ---- fs_init tests ---- */

static void test_fs_init_valid(void)
{
    build_test_fs();
    fs_init();
    ASSERT(fs_ready == 1);
}

static void test_fs_init_bad_magic(void)
{
    build_test_fs();
    /* Corrupt magic */
    struct superblock *s = (struct superblock *)mock_disk[0];
    s->magic = 0xDEADBEEF;
    fs_init();
    ASSERT(fs_ready == 0);
}

/* ---- inode_blocks / data_start (tested via fs_init + bmap) ---- */

static void test_inode_blocks_calc(void)
{
    build_test_fs();
    fs_init();
    /* 42 inodes, 21 per block → 2 inode blocks */
    ASSERT_EQ(inode_blocks(), 2);
    ASSERT_EQ(data_start(), 3);
}

/* ---- fs_iget / fs_iput ---- */

static void test_iget_root(void)
{
    build_test_fs();
    fs_init();
    struct inode *ip = fs_iget(1);
    ASSERT_NOT_NULL(ip);
    ASSERT_EQ(ip->inum, 1);
    ASSERT_EQ(ip->type, FT_DIR);
    ASSERT_EQ(ip->nlink, 2);
    ASSERT_EQ(ip->refcount, 1);
    fs_iput(ip);
    ASSERT_EQ(ip->refcount, 0);
}

static void test_iget_invalid(void)
{
    build_test_fs();
    fs_init();
    ASSERT_NULL(fs_iget(0));
    ASSERT_NULL(fs_iget(9999));
}

static void test_iget_cache(void)
{
    build_test_fs();
    fs_init();
    struct inode *a = fs_iget(1);
    struct inode *b = fs_iget(1);
    ASSERT(a == b);  /* same pointer from cache */
    ASSERT_EQ(a->refcount, 2);
    fs_iput(a);
    fs_iput(b);
    ASSERT_EQ(a->refcount, 0);
}

static void test_iget_not_ready(void)
{
    build_test_fs();
    /* Don't call fs_init → fs_ready=0 */
    ASSERT_NULL(fs_iget(1));
}

/* ---- fs_stat ---- */

static void test_stat_basic(void)
{
    build_test_fs();
    fs_init();
    struct inode *ip = fs_iget(1);
    struct kstat st;
    int r = fs_stat(ip, &st);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(st.inum, 1);
    ASSERT_EQ(st.type, FT_DIR);
    ASSERT_EQ(st.nlink, 2);
    fs_iput(ip);
}

static void test_stat_null(void)
{
    ASSERT_EQ(fs_stat(NULL, NULL), -ENOENT);
}

/* ---- bmap (non-allocating) ---- */

static void test_bmap_direct(void)
{
    build_test_fs();
    fs_init();
    struct inode *ip = fs_iget(1);

    /* Set up some direct blocks */
    ip->direct[0] = 10;
    ip->direct[5] = 20;
    ip->direct[11] = 30;

    ASSERT_EQ(bmap(ip, 0, 0), 10);
    ASSERT_EQ(bmap(ip, 5 * BLOCK_SIZE, 0), 20);
    ASSERT_EQ(bmap(ip, 11 * BLOCK_SIZE, 0), 30);

    /* Unset block returns 0 */
    ASSERT_EQ(bmap(ip, 3 * BLOCK_SIZE, 0), 0);

    fs_iput(ip);
}

static void test_bmap_indirect(void)
{
    build_test_fs();
    fs_init();
    struct inode *ip = fs_iget(1);

    /* Set up an indirect block at block 10 */
    ip->indirect = 10;
    uint16_t *ptrs = (uint16_t *)mock_disk[10];
    ptrs[0] = 50;
    ptrs[5] = 55;

    /* Offset past 12 direct blocks → indirect */
    ASSERT_EQ(bmap(ip, 12 * BLOCK_SIZE, 0), 50);
    ASSERT_EQ(bmap(ip, 17 * BLOCK_SIZE, 0), 55);

    /* No indirect block set → 0 */
    ip->indirect = 0;
    ASSERT_EQ(bmap(ip, 12 * BLOCK_SIZE, 0), 0);

    fs_iput(ip);
}

/* ---- fs_read ---- */

static void test_read_basic(void)
{
    build_test_fs();
    fs_init();

    /* Create a file inode at inode 2 with data in block 5 */
    create_test_inode(2, FT_FILE, 100);
    struct disk_inode *di = (struct disk_inode *)mock_disk[1] + 1;  /* inode 2 */
    di->direct[0] = 5;

    /* Write test data to block 5 */
    memset(mock_disk[5], 'A', BLOCK_SIZE);

    struct inode *ip = fs_iget(2);
    ASSERT_NOT_NULL(ip);

    char buf[64];
    int n = fs_read(ip, buf, 0, 64);
    ASSERT_EQ(n, 64);
    ASSERT_EQ(buf[0], 'A');
    ASSERT_EQ(buf[63], 'A');

    fs_iput(ip);
}

static void test_read_past_eof(void)
{
    build_test_fs();
    fs_init();

    create_test_inode(2, FT_FILE, 10);
    struct disk_inode *di = (struct disk_inode *)mock_disk[1] + 1;
    di->direct[0] = 5;
    memset(mock_disk[5], 'B', BLOCK_SIZE);

    struct inode *ip = fs_iget(2);
    char buf[64];
    int n = fs_read(ip, buf, 0, 64);
    ASSERT_EQ(n, 10);  /* clamped to size */

    /* Read starting past EOF */
    n = fs_read(ip, buf, 20, 10);
    ASSERT_EQ(n, 0);

    fs_iput(ip);
}

static void test_read_null_inode(void)
{
    ASSERT_EQ(fs_read(NULL, NULL, 0, 10), 0);
}

static void test_read_cross_block(void)
{
    build_test_fs();
    fs_init();

    /* File spanning 2 blocks: 1500 bytes */
    create_test_inode(2, FT_FILE, 1500);
    struct disk_inode *di = (struct disk_inode *)mock_disk[1] + 1;
    di->direct[0] = 5;
    di->direct[1] = 6;

    memset(mock_disk[5], 'X', BLOCK_SIZE);
    memset(mock_disk[6], 'Y', BLOCK_SIZE);

    struct inode *ip = fs_iget(2);
    char buf[1500];
    int n = fs_read(ip, buf, 0, 1500);
    ASSERT_EQ(n, 1500);
    ASSERT_EQ(buf[0], 'X');
    ASSERT_EQ(buf[1023], 'X');
    ASSERT_EQ(buf[1024], 'Y');
    ASSERT_EQ(buf[1499], 'Y');

    fs_iput(ip);
}

/* ---- fs_write ---- */

static void test_write_basic(void)
{
    build_test_fs();
    fs_init();

    create_test_inode(2, FT_FILE, 0);

    struct inode *ip = fs_iget(2);
    ASSERT_NOT_NULL(ip);

    char data[] = "hello, minifs!";
    int n = fs_write(ip, data, 0, strlen(data));
    ASSERT_EQ(n, (int)strlen(data));
    ASSERT(ip->size >= strlen(data));

    /* Read it back */
    char buf[64];
    memset(buf, 0, sizeof(buf));
    n = fs_read(ip, buf, 0, strlen(data));
    ASSERT_EQ(n, (int)strlen(data));
    ASSERT_STR_EQ(buf, "hello, minifs!");

    fs_iput(ip);
}

static void test_write_null_inode(void)
{
    ASSERT_EQ(fs_write(NULL, "x", 0, 1), -EIO);
}

/* ---- fs_namei (path resolution) ---- */

static void test_namei_root(void)
{
    build_test_fs();
    fs_init();

    struct inode *ip = fs_namei("/");
    ASSERT_NOT_NULL(ip);
    ASSERT_EQ(ip->inum, 1);
    ASSERT_EQ(ip->type, FT_DIR);
    fs_iput(ip);
}

static void test_namei_file_in_root(void)
{
    build_test_fs();
    /* Create file inode 2 and add to root */
    create_test_inode(2, FT_FILE, 100);
    add_root_entry("hello", 2);
    fs_init();

    struct inode *ip = fs_namei("/hello");
    ASSERT_NOT_NULL(ip);
    ASSERT_EQ(ip->inum, 2);
    ASSERT_EQ(ip->type, FT_FILE);
    fs_iput(ip);
}

static void test_namei_nonexistent(void)
{
    build_test_fs();
    fs_init();

    ASSERT_NULL(fs_namei("/nosuchfile"));
}

static void test_namei_trailing_slash(void)
{
    build_test_fs();
    fs_init();

    /* "/"  and "//" should both resolve to root */
    struct inode *ip = fs_namei("//");
    ASSERT_NOT_NULL(ip);
    ASSERT_EQ(ip->inum, 1);
    fs_iput(ip);
}

static void test_namei_not_ready(void)
{
    build_test_fs();
    /* Don't call fs_init */
    ASSERT_NULL(fs_namei("/"));
}

static void test_namei_file_as_dir(void)
{
    build_test_fs();
    create_test_inode(2, FT_FILE, 100);
    add_root_entry("file", 2);
    fs_init();

    /* /file/something — file is not a dir */
    ASSERT_NULL(fs_namei("/file/something"));
}

/* ---- fs_namei_parent ---- */

static void test_namei_parent_root_file(void)
{
    build_test_fs();
    fs_init();

    char name[NAME_MAX];
    struct inode *dp = fs_namei_parent("/hello.txt", name);
    ASSERT_NOT_NULL(dp);
    ASSERT_EQ(dp->inum, 1);  /* parent is root */
    ASSERT_STR_EQ(name, "hello.txt");
    fs_iput(dp);
}

static void test_namei_parent_bare_name(void)
{
    build_test_fs();
    fs_init();
    curproc = NULL;  /* no curproc → falls back to root */

    char name[NAME_MAX];
    struct inode *dp = fs_namei_parent("foo.txt", name);
    ASSERT_NOT_NULL(dp);
    ASSERT_EQ(dp->inum, 1);  /* root */
    ASSERT_STR_EQ(name, "foo.txt");
    fs_iput(dp);
}

/* ---- fs_create ---- */

static void test_create_file(void)
{
    build_test_fs();
    /* Add . and .. to root so dir_lookup works */
    add_root_entry(".", 1);
    add_root_entry("..", 1);
    fs_init();

    struct inode *ip = fs_create("/newfile", FT_FILE);
    ASSERT_NOT_NULL(ip);
    ASSERT_EQ(ip->type, FT_FILE);
    ASSERT(ip->inum > 1);  /* allocated a new inode */
    fs_iput(ip);
}

static void test_create_existing(void)
{
    build_test_fs();
    create_test_inode(2, FT_FILE, 50);
    add_root_entry("exists", 2);
    fs_init();

    /* Creating an existing file returns the existing inode */
    struct inode *ip = fs_create("/exists", FT_FILE);
    ASSERT_NOT_NULL(ip);
    ASSERT_EQ(ip->inum, 2);
    fs_iput(ip);
}

/* ---- fs_unlink ---- */

static void test_unlink_file(void)
{
    build_test_fs();
    create_test_inode(2, FT_FILE, 50);
    add_root_entry("delme", 2);
    fs_init();

    ASSERT_NOT_NULL(fs_namei("/delme"));
    /* Release the ref from namei */
    struct inode *ip = fs_namei("/delme");
    fs_iput(ip);

    int r = fs_unlink("/delme");
    ASSERT_EQ(r, 0);

    ASSERT_NULL(fs_namei("/delme"));
}

static void test_unlink_nonexistent(void)
{
    build_test_fs();
    fs_init();
    ASSERT_EQ(fs_unlink("/nosuch"), -ENOENT);
}

static void test_unlink_dir(void)
{
    build_test_fs();
    create_test_inode(2, FT_DIR, 0);
    add_root_entry("subdir", 2);
    fs_init();

    /* Can't unlink a directory */
    ASSERT_EQ(fs_unlink("/subdir"), -EISDIR);
}

/* ---- fs_getdents ---- */

static void test_getdents_basic(void)
{
    build_test_fs();
    create_test_inode(2, FT_FILE, 50);
    add_root_entry(".", 1);
    add_root_entry("..", 1);
    add_root_entry("hello", 2);
    fs_init();

    struct inode *ip = fs_iget(1);
    struct dirent_disk entries[4];
    int n = fs_getdents(ip, entries, 0, sizeof(entries));
    ASSERT(n > 0);

    /* First entry should be "." */
    ASSERT_EQ(entries[0].inode, 1);
    ASSERT_STR_EQ(entries[0].name, ".");

    fs_iput(ip);
}

static void test_getdents_not_dir(void)
{
    build_test_fs();
    create_test_inode(2, FT_FILE, 50);
    fs_init();

    struct inode *ip = fs_iget(2);
    struct dirent_disk entries[4];
    int r = fs_getdents(ip, entries, 0, sizeof(entries));
    ASSERT_EQ(r, -ENOTDIR);
    fs_iput(ip);
}

/* ---- fs_mkdir / fs_rmdir ---- */

static void test_mkdir_basic(void)
{
    build_test_fs();
    add_root_entry(".", 1);
    add_root_entry("..", 1);
    fs_init();

    int r = fs_mkdir("/subdir");
    ASSERT_EQ(r, 0);

    struct inode *ip = fs_namei("/subdir");
    ASSERT_NOT_NULL(ip);
    ASSERT_EQ(ip->type, FT_DIR);
    fs_iput(ip);
}

static void test_rmdir_notempty(void)
{
    build_test_fs();
    /* Create subdir inode 2 with entries ".", "..", and "file" */
    create_test_inode(2, FT_DIR, 3 * sizeof(struct dirent_disk));
    add_root_entry("subdir", 2);

    /* Give subdir a data block (block 7) with entries */
    struct disk_inode *di = (struct disk_inode *)mock_disk[1] + 1;
    di->direct[0] = 7;

    struct dirent_disk *de = (struct dirent_disk *)mock_disk[7];
    de[0].inode = 2; strcpy(de[0].name, ".");
    de[1].inode = 1; strcpy(de[1].name, "..");
    de[2].inode = 3; strcpy(de[2].name, "file");

    fs_init();

    ASSERT_EQ(fs_rmdir("/subdir"), -ENOTEMPTY);
}

/* ---- balloc / bfree ---- */

static void test_balloc_basic(void)
{
    build_test_fs();
    fs_init();

    /* Free list: 3 → 4 → 5 → ... */
    uint16_t b1 = balloc();
    ASSERT_EQ(b1, 3);
    uint16_t b2 = balloc();
    ASSERT_EQ(b2, 4);
    uint16_t b3 = balloc();
    ASSERT_EQ(b3, 5);
}

static void test_bfree_basic(void)
{
    build_test_fs();
    fs_init();

    uint16_t b = balloc();
    ASSERT_EQ(b, 3);
    /* Free it and allocate again — should get it back */
    bfree(b);
    uint16_t b2 = balloc();
    ASSERT_EQ(b2, 3);
}

static void test_balloc_exhaustion(void)
{
    build_test_fs();
    fs_init();

    /* Exhaust all blocks (3..63 = 61 blocks) */
    int count = 0;
    while (balloc() != 0)
        count++;
    ASSERT_EQ(count, TEST_NBLOCKS - 3);

    /* Next alloc should fail */
    ASSERT_EQ(balloc(), 0);
}

/* ---- ialloc ---- */

static void test_ialloc_basic(void)
{
    build_test_fs();
    fs_init();

    uint16_t inum = ialloc(FT_FILE);
    /* Inode 1 is root (FT_DIR), so first free should be 2 */
    ASSERT_EQ(inum, 2);
}

/* ---- fs_rename ---- */

static void test_rename_basic(void)
{
    build_test_fs();
    create_test_inode(2, FT_FILE, 50);
    add_root_entry("old", 2);
    fs_init();

    ASSERT_NOT_NULL(fs_namei("/old"));
    struct inode *ip = fs_namei("/old");
    fs_iput(ip);

    int r = fs_rename("/old", "/new");
    ASSERT_EQ(r, 0);
    ASSERT_NULL(fs_namei("/old"));
    ASSERT_NOT_NULL(fs_namei("/new"));
    ip = fs_namei("/new");
    fs_iput(ip);
}

static void test_rename_nonexistent(void)
{
    build_test_fs();
    fs_init();
    ASSERT_EQ(fs_rename("/nosuch", "/new"), -ENOENT);
}

/* ---- inode refcount ---- */

static void test_refcount_lifecycle(void)
{
    build_test_fs();
    fs_init();

    struct inode *ip = fs_iget(1);
    ASSERT_EQ(ip->refcount, 1);
    fs_iget(1);  /* ref++  */
    ASSERT_EQ(ip->refcount, 2);
    fs_iput(ip);
    ASSERT_EQ(ip->refcount, 1);
    fs_iput(ip);
    ASSERT_EQ(ip->refcount, 0);
}

/* ---- edge cases ---- */

static void test_iget_all_slots_used(void)
{
    build_test_fs();
    fs_init();

    /* Fill all MAXINODE cache slots */
    for (int i = 1; i <= MAXINODE && i <= (int)sb.ninodes; i++)
        fs_iget(i);

    /* Next iget for a new inode should fail */
    /* (We only have 42 inodes, so this doesn't apply.
     *  But verify that double-gets share cache) */
    struct inode *ip = fs_iget(1);
    ASSERT_NOT_NULL(ip);  /* cached */

    /* Release them all */
    for (int i = 0; i < MAXINODE; i++) {
        if (inode_cache[i].refcount > 0) {
            while (inode_cache[i].refcount > 0)
                fs_iput(&inode_cache[i]);
        }
    }
}

/* ---- Run all tests ---- */

int main(void)
{
    /* fs_init */
    RUN_TEST(test_fs_init_valid);
    RUN_TEST(test_fs_init_bad_magic);

    /* Layout calculations */
    RUN_TEST(test_inode_blocks_calc);

    /* iget / iput */
    RUN_TEST(test_iget_root);
    RUN_TEST(test_iget_invalid);
    RUN_TEST(test_iget_cache);
    RUN_TEST(test_iget_not_ready);

    /* stat */
    RUN_TEST(test_stat_basic);
    RUN_TEST(test_stat_null);

    /* bmap */
    RUN_TEST(test_bmap_direct);
    RUN_TEST(test_bmap_indirect);

    /* read */
    RUN_TEST(test_read_basic);
    RUN_TEST(test_read_past_eof);
    RUN_TEST(test_read_null_inode);
    RUN_TEST(test_read_cross_block);

    /* write */
    RUN_TEST(test_write_basic);
    RUN_TEST(test_write_null_inode);

    /* namei */
    RUN_TEST(test_namei_root);
    RUN_TEST(test_namei_file_in_root);
    RUN_TEST(test_namei_nonexistent);
    RUN_TEST(test_namei_trailing_slash);
    RUN_TEST(test_namei_not_ready);
    RUN_TEST(test_namei_file_as_dir);

    /* namei_parent */
    RUN_TEST(test_namei_parent_root_file);
    RUN_TEST(test_namei_parent_bare_name);

    /* create */
    RUN_TEST(test_create_file);
    RUN_TEST(test_create_existing);

    /* unlink */
    RUN_TEST(test_unlink_file);
    RUN_TEST(test_unlink_nonexistent);
    RUN_TEST(test_unlink_dir);

    /* getdents */
    RUN_TEST(test_getdents_basic);
    RUN_TEST(test_getdents_not_dir);

    /* mkdir / rmdir */
    RUN_TEST(test_mkdir_basic);
    RUN_TEST(test_rmdir_notempty);

    /* balloc / bfree */
    RUN_TEST(test_balloc_basic);
    RUN_TEST(test_bfree_basic);
    RUN_TEST(test_balloc_exhaustion);

    /* ialloc */
    RUN_TEST(test_ialloc_basic);

    /* rename */
    RUN_TEST(test_rename_basic);
    RUN_TEST(test_rename_nonexistent);

    /* refcount */
    RUN_TEST(test_refcount_lifecycle);
    RUN_TEST(test_iget_all_slots_used);

    TEST_REPORT();
}
