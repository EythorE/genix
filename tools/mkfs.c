/*
 * mkfs.minifs — create a minifs filesystem image
 *
 * Usage: mkfs.minifs <image> <size_blocks> [files...]
 *
 * The image is a raw block device image.
 * files... are added to the root directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <libgen.h>

#define BLOCK_SIZE  1024
#define NAME_MAX_FS 30
#define MINIFS_MAGIC 0x4D494E49

/* Must match kernel definitions */
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
};  /* 48 bytes */

struct dirent_disk {
    uint16_t inode;
    char     name[NAME_MAX_FS];
};  /* 32 bytes */

#define FT_FREE 0
#define FT_FILE 1
#define FT_DIR  2
#define FT_DEV  3

#define INODES_PER_BLK (BLOCK_SIZE / sizeof(struct disk_inode))  /* 21 */
#define DIRENTS_PER_BLK (BLOCK_SIZE / sizeof(struct dirent_disk)) /* 32 */

static FILE *img;
static int nblocks;
static int ninodes;
static int inode_blks;
static int data_start_blk;
static uint16_t next_free_blk;
static uint16_t next_inode = 1;  /* inode 0 unused, start at 1 */

/* Write big-endian uint16 */
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

/* Write big-endian uint32 */
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

static void write_block(int blkno, void *data)
{
    fseek(img, (long)blkno * BLOCK_SIZE, SEEK_SET);
    fwrite(data, BLOCK_SIZE, 1, img);
}

static void read_block(int blkno, void *data)
{
    fseek(img, (long)blkno * BLOCK_SIZE, SEEK_SET);
    fread(data, BLOCK_SIZE, 1, img);
}

/* Allocate a data block from the free list */
static uint16_t alloc_block(void)
{
    if (next_free_blk >= nblocks)
        return 0;
    return next_free_blk++;
}

/* Allocate an inode */
static uint16_t alloc_inode(void)
{
    if (next_inode > ninodes)
        return 0;
    return next_inode++;
}

/* Write an inode to the image */
static void write_inode(uint16_t inum, struct disk_inode *di)
{
    uint8_t blk_data[BLOCK_SIZE];
    int blk = 1 + (inum - 1) / INODES_PER_BLK;
    int off = ((inum - 1) % INODES_PER_BLK) * sizeof(struct disk_inode);

    read_block(blk, blk_data);

    /* Write in big-endian (68000 native) */
    uint8_t *p = blk_data + off;
    p[0] = di->type;
    p[1] = di->nlink;
    p[2] = di->dev_major;
    p[3] = di->dev_minor;
    put32(p + 4, di->size);
    put32(p + 8, di->mtime);
    for (int i = 0; i < 12; i++)
        put16(p + 12 + i * 2, di->direct[i]);
    put16(p + 36, di->indirect);
    memset(p + 38, 0, 10);

    write_block(blk, blk_data);
}

/* Write a directory entry */
static void add_dirent(uint16_t dir_inum, struct disk_inode *dir_di,
                       const char *name, uint16_t child_inum)
{
    /* Find which block of the directory to write to */
    int entry_idx = dir_di->size / sizeof(struct dirent_disk);
    int blk_idx = entry_idx / DIRENTS_PER_BLK;

    if (blk_idx >= 12) {
        fprintf(stderr, "Directory too large\n");
        return;
    }

    if (dir_di->direct[blk_idx] == 0) {
        dir_di->direct[blk_idx] = alloc_block();
        /* Zero the new block */
        uint8_t zeros[BLOCK_SIZE];
        memset(zeros, 0, BLOCK_SIZE);
        write_block(dir_di->direct[blk_idx], zeros);
    }

    uint8_t blk_data[BLOCK_SIZE];
    read_block(dir_di->direct[blk_idx], blk_data);

    int off_in_blk = (entry_idx % DIRENTS_PER_BLK) * sizeof(struct dirent_disk);
    uint8_t *p = blk_data + off_in_blk;

    /* Write big-endian inode number */
    put16(p, child_inum);
    memset(p + 2, 0, NAME_MAX_FS);
    strncpy((char *)(p + 2), name, NAME_MAX_FS - 1);

    write_block(dir_di->direct[blk_idx], blk_data);

    dir_di->size += sizeof(struct dirent_disk);
    write_inode(dir_inum, dir_di);
}

/* Add a file from the host filesystem */
static void add_file(uint16_t dir_inum, struct disk_inode *dir_di,
                     const char *hostpath)
{
    FILE *f = fopen(hostpath, "rb");
    if (!f) {
        perror(hostpath);
        return;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint16_t inum = alloc_inode();
    struct disk_inode di;
    memset(&di, 0, sizeof(di));
    di.type = FT_FILE;
    di.nlink = 1;
    di.size = size;
    di.mtime = time(NULL);

    /* Allocate blocks and write data */
    uint8_t buf[BLOCK_SIZE];
    int blk_idx = 0;
    while (size > 0) {
        memset(buf, 0, BLOCK_SIZE);
        int n = fread(buf, 1, BLOCK_SIZE, f);
        if (n <= 0) break;

        uint16_t blk = alloc_block();
        if (blk_idx < 12) {
            di.direct[blk_idx] = blk;
        } else {
            /* Indirect block: allocate on first use */
            if (di.indirect == 0) {
                di.indirect = alloc_block();
                uint8_t zbuf[BLOCK_SIZE];
                memset(zbuf, 0, BLOCK_SIZE);
                write_block(di.indirect, zbuf);
            }
            /* Write block number into indirect block */
            uint8_t ibuf[BLOCK_SIZE];
            read_block(di.indirect, ibuf);
            int off = (blk_idx - 12) * 2;  /* uint16_t per entry, big-endian */
            ibuf[off]     = (blk >> 8) & 0xFF;
            ibuf[off + 1] = blk & 0xFF;
            write_block(di.indirect, ibuf);
        }
        write_block(blk, buf);

        size -= n;
        blk_idx++;
    }
    fclose(f);

    write_inode(inum, &di);

    /* Add to directory */
    char *name = basename((char *)hostpath);
    add_dirent(dir_inum, dir_di, name, inum);

    printf("  %s -> inode %d (%d blocks)\n", name, inum, blk_idx);
}

/* Create a subdirectory */
static uint16_t make_subdir(uint16_t parent_inum, struct disk_inode *parent_di,
                            const char *name)
{
    uint16_t inum = alloc_inode();
    struct disk_inode di;
    memset(&di, 0, sizeof(di));
    di.type = FT_DIR;
    di.nlink = 2;
    di.mtime = time(NULL);

    write_inode(inum, &di);

    /* Add . and .. */
    add_dirent(inum, &di, ".", inum);
    add_dirent(inum, &di, "..", parent_inum);

    /* Add to parent */
    add_dirent(parent_inum, parent_di, name, inum);

    return inum;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: mkfs.minifs <image> <size_blocks> [file...]\n");
        return 1;
    }

    nblocks = atoi(argv[2]);
    if (nblocks < 16) {
        fprintf(stderr, "Minimum 16 blocks\n");
        return 1;
    }

    ninodes = nblocks / 4;  /* ~25% of blocks for inodes */
    if (ninodes < 16) ninodes = 16;
    if (ninodes > 1024) ninodes = 1024;

    inode_blks = (ninodes * sizeof(struct disk_inode) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    data_start_blk = 1 + inode_blks;
    next_free_blk = data_start_blk;

    printf("Creating minifs: %d blocks, %d inodes, %d inode blocks, data starts at block %d\n",
           nblocks, ninodes, inode_blks, data_start_blk);

    img = fopen(argv[1], "w+b");
    if (!img) {
        perror(argv[1]);
        return 1;
    }

    /* Zero the entire image */
    uint8_t zeros[BLOCK_SIZE];
    memset(zeros, 0, BLOCK_SIZE);
    for (int i = 0; i < nblocks; i++)
        write_block(i, zeros);

    /* Create root directory (inode 1) */
    uint16_t root_inum = alloc_inode();  /* Should be 1 */
    struct disk_inode root_di;
    memset(&root_di, 0, sizeof(root_di));
    root_di.type = FT_DIR;
    root_di.nlink = 2;
    root_di.mtime = time(NULL);
    write_inode(root_inum, &root_di);

    /* Add . and .. to root */
    add_dirent(root_inum, &root_di, ".", root_inum);
    add_dirent(root_inum, &root_di, "..", root_inum);

    /* Create /bin and /dev directories */
    struct disk_inode bin_di;
    memset(&bin_di, 0, sizeof(bin_di));
    bin_di.type = FT_DIR;
    bin_di.nlink = 2;
    bin_di.mtime = time(NULL);

    uint16_t bin_inum = make_subdir(root_inum, &root_di, "bin");

    /* Re-read root_di and bin_di since they were updated */
    /* (We need fresh copies after add_dirent modified them) */

    make_subdir(root_inum, &root_di, "dev");
    make_subdir(root_inum, &root_di, "tmp");

    /* Add files from command line */
    for (int i = 3; i < argc; i++) {
        /* Re-read bin directory inode */
        uint8_t blk_data[BLOCK_SIZE];
        int blk = 1 + (bin_inum - 1) / INODES_PER_BLK;
        int off = ((bin_inum - 1) % INODES_PER_BLK) * sizeof(struct disk_inode);
        read_block(blk, blk_data);
        uint8_t *p = blk_data + off;

        struct disk_inode di;
        di.type = p[0];
        di.nlink = p[1];
        di.dev_major = p[2];
        di.dev_minor = p[3];
        di.size = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) |
                  ((uint32_t)p[6] << 8) | p[7];
        di.mtime = ((uint32_t)p[8] << 24) | ((uint32_t)p[9] << 16) |
                   ((uint32_t)p[10] << 8) | p[11];
        for (int j = 0; j < 12; j++)
            di.direct[j] = ((uint16_t)p[12 + j*2] << 8) | p[12 + j*2 + 1];
        di.indirect = ((uint16_t)p[36] << 8) | p[37];

        add_file(bin_inum, &di, argv[i]);
    }

    /* Build free list from remaining blocks */
    uint16_t free_head = 0;
    for (int i = nblocks - 1; i >= (int)next_free_blk; i--) {
        uint8_t blk[BLOCK_SIZE];
        memset(blk, 0, BLOCK_SIZE);
        put16(blk, free_head);
        write_block(i, blk);
        free_head = i;
    }

    /* Write superblock */
    uint8_t sb_data[BLOCK_SIZE];
    memset(sb_data, 0, BLOCK_SIZE);
    put32(sb_data + 0, MINIFS_MAGIC);
    put16(sb_data + 4, BLOCK_SIZE);
    put16(sb_data + 6, nblocks);
    put16(sb_data + 8, ninodes);
    put16(sb_data + 10, free_head);
    put16(sb_data + 12, ninodes - (next_inode - 1));
    put16(sb_data + 14, 0);
    put32(sb_data + 16, (uint32_t)time(NULL));
    write_block(0, sb_data);

    fclose(img);
    printf("Done. Free blocks start at %d, free list head=%d\n",
           next_free_blk, free_head);
    return 0;
}
