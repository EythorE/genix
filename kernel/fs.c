/*
 * minifs — minimal Unix filesystem
 *
 * Block 0:        Superblock
 * Block 1..N:     Inode table (21 inodes per block, 48 bytes each)
 * Block N+1..:    Data blocks
 */
#include "kernel.h"

#define MINIFS_MAGIC    0x4D494E49  /* "MINI" */
#define INODES_PER_BLK  (BLOCK_SIZE / sizeof(struct disk_inode))  /* 21 */
#define DIRENTS_PER_BLK (BLOCK_SIZE / sizeof(struct dirent_disk)) /* 32 */
#define PTRS_PER_BLK    (BLOCK_SIZE / sizeof(uint16_t))           /* 512 */

static struct superblock sb;
static struct inode inode_cache[MAXINODE];
static int fs_ready = 0;

/* Forward declaration — bfree is used by fs_iput, defined below */
static void bfree(uint16_t blk);

/* How many blocks for inode table */
static uint16_t inode_blocks(void)
{
    return (sb.ninodes + INODES_PER_BLK - 1) / INODES_PER_BLK;
}

/* First data block */
static uint16_t data_start(void)
{
    return 1 + inode_blocks();
}

void fs_init(void)
{
    struct buf *b = bread(0, 0);
    memcpy(&sb, b->data, sizeof(sb));
    brelse(b);

    if (sb.magic != MINIFS_MAGIC) {
        kputs("[fs] No filesystem found (no magic). Filesystem disabled.\n");
        fs_ready = 0;
        return;
    }

    kputs("[fs] minifs: ");
    kprintf("%d blocks, %d inodes\n", (uint32_t)sb.nblocks, (uint32_t)sb.ninodes);
    fs_ready = 1;

    for (int i = 0; i < MAXINODE; i++) {
        inode_cache[i].inum = 0;
        inode_cache[i].refcount = 0;
    }
}

/* Read an inode from disk */
struct inode *fs_iget(uint16_t inum)
{
    if (!fs_ready || inum == 0 || inum > sb.ninodes)
        return NULL;

    /* Check cache */
    for (int i = 0; i < MAXINODE; i++) {
        if (inode_cache[i].refcount > 0 && inode_cache[i].inum == inum) {
            inode_cache[i].refcount++;
            return &inode_cache[i];
        }
    }

    /* Find free cache slot */
    struct inode *ip = NULL;
    for (int i = 0; i < MAXINODE; i++) {
        if (inode_cache[i].refcount == 0) {
            ip = &inode_cache[i];
            break;
        }
    }
    if (!ip)
        return NULL;

    /* Read from disk */
    uint16_t blk = 1 + (inum - 1) / INODES_PER_BLK;
    uint16_t off = ((inum - 1) % INODES_PER_BLK) * sizeof(struct disk_inode);

    struct buf *b = bread(0, blk);
    struct disk_inode *di = (struct disk_inode *)(b->data + off);

    ip->inum = inum;
    ip->type = di->type;
    ip->nlink = di->nlink;
    ip->dev_major = di->dev_major;
    ip->dev_minor = di->dev_minor;
    ip->size = di->size;
    ip->mtime = di->mtime;
    memcpy(ip->direct, di->direct, sizeof(ip->direct));
    ip->indirect = di->indirect;
    ip->refcount = 1;
    ip->dirty = 0;
    brelse(b);

    return ip;
}

/* Release an inode reference */
void fs_iput(struct inode *ip)
{
    if (!ip) return;
    if (ip->refcount > 0)
        ip->refcount--;
    if (ip->refcount == 0 && ip->dirty)
        fs_iupdate(ip);
    if (ip->refcount == 0 && ip->nlink == 0 && ip->type != FT_FREE) {
        /* Free data blocks: direct blocks first */
        for (int i = 0; i < 12; i++) {
            if (ip->direct[i] != 0) {
                bfree(ip->direct[i]);
                ip->direct[i] = 0;
            }
        }
        /* Free indirect block and all blocks it points to */
        if (ip->indirect != 0) {
            struct buf *ib = bread(0, ip->indirect);
            uint16_t *ptrs = (uint16_t *)ib->data;
            for (int i = 0; i < (int)PTRS_PER_BLK; i++) {
                if (ptrs[i] != 0)
                    bfree(ptrs[i]);
            }
            brelse(ib);
            bfree(ip->indirect);
            ip->indirect = 0;
        }
        ip->size = 0;
        ip->type = FT_FREE;
        ip->dirty = 1;
        fs_iupdate(ip);
    }
}

/* Write inode back to disk */
void fs_iupdate(struct inode *ip)
{
    if (!fs_ready || !ip) return;

    uint16_t blk = 1 + (ip->inum - 1) / INODES_PER_BLK;
    uint16_t off = ((ip->inum - 1) % INODES_PER_BLK) * sizeof(struct disk_inode);

    struct buf *b = bread(0, blk);
    struct disk_inode *di = (struct disk_inode *)(b->data + off);

    di->type = ip->type;
    di->nlink = ip->nlink;
    di->dev_major = ip->dev_major;
    di->dev_minor = ip->dev_minor;
    di->size = ip->size;
    di->mtime = ip->mtime;
    memcpy(di->direct, ip->direct, sizeof(ip->direct));
    di->indirect = ip->indirect;

    b->dirty = 1;
    bwrite(b);
    brelse(b);
    ip->dirty = 0;
}

/* Allocate a free block from the free list */
static uint16_t balloc(void)
{
    if (sb.free_list == 0)
        return 0;

    uint16_t blk = sb.free_list;
    struct buf *b = bread(0, blk);
    /* First 2 bytes of a free block = next free block */
    uint16_t next = (b->data[0] << 8) | b->data[1];
    brelse(b);

    sb.free_list = next;

    /* Write back superblock */
    struct buf *sb_buf = bread(0, 0);
    memcpy(sb_buf->data, &sb, sizeof(sb));
    sb_buf->dirty = 1;
    bwrite(sb_buf);
    brelse(sb_buf);

    return blk;
}

/* Free a block back to the free list */
static void bfree(uint16_t blk)
{
    struct buf *b = bread(0, blk);
    memset(b->data, 0, BLOCK_SIZE);
    b->data[0] = (sb.free_list >> 8) & 0xFF;
    b->data[1] = sb.free_list & 0xFF;
    b->dirty = 1;
    bwrite(b);
    brelse(b);

    sb.free_list = blk;

    struct buf *sb_buf = bread(0, 0);
    memcpy(sb_buf->data, &sb, sizeof(sb));
    sb_buf->dirty = 1;
    bwrite(sb_buf);
    brelse(sb_buf);
}

/* Allocate a free inode */
static uint16_t ialloc(uint8_t type)
{
    for (uint16_t i = 1; i <= sb.ninodes; i++) {
        uint16_t blk = 1 + (i - 1) / INODES_PER_BLK;
        uint16_t off = ((i - 1) % INODES_PER_BLK) * sizeof(struct disk_inode);
        struct buf *b = bread(0, blk);
        struct disk_inode *di = (struct disk_inode *)(b->data + off);
        if (di->type == FT_FREE) {
            memset(di, 0, sizeof(*di));
            di->type = type;
            di->nlink = 1;
            b->dirty = 1;
            bwrite(b);
            brelse(b);
            return i;
        }
        brelse(b);
    }
    return 0;
}

/* Map file offset to block number, allocating if needed */
static uint16_t bmap(struct inode *ip, uint32_t offset, int alloc)
{
    uint32_t bn = offset / BLOCK_SIZE;

    if (bn < 12) {
        if (ip->direct[bn] == 0 && alloc) {
            ip->direct[bn] = balloc();
            ip->dirty = 1;
        }
        return ip->direct[bn];
    }

    bn -= 12;
    if (bn < PTRS_PER_BLK) {
        if (ip->indirect == 0) {
            if (!alloc) return 0;
            ip->indirect = balloc();
            if (ip->indirect == 0) return 0;
            /* Zero out the indirect block */
            struct buf *ib = bread(0, ip->indirect);
            memset(ib->data, 0, BLOCK_SIZE);
            ib->dirty = 1;
            bwrite(ib);
            brelse(ib);
            ip->dirty = 1;
        }
        struct buf *ib = bread(0, ip->indirect);
        /* On 68000 (big-endian), uint16_t in data[] is already native order */
        uint16_t *ptrs = (uint16_t *)ib->data;
        uint16_t blk = ptrs[bn];
        if (blk == 0 && alloc) {
            blk = balloc();
            ptrs[bn] = blk;
            ib->dirty = 1;
            bwrite(ib);
        }
        brelse(ib);
        return blk;
    }

    return 0;  /* File too large */
}

/* Read from an inode */
int fs_read(struct inode *ip, void *buf, uint32_t off, uint32_t n)
{
    if (!ip || off >= ip->size)
        return 0;
    if (off + n > ip->size)
        n = ip->size - off;

    uint32_t total = 0;
    uint8_t *dst = (uint8_t *)buf;

    while (total < n) {
        uint16_t blk = bmap(ip, off, 0);
        if (blk == 0) break;

        uint32_t boff = off % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - boff;
        if (chunk > n - total)
            chunk = n - total;

        struct buf *b = bread(0, blk);
        memcpy(dst, b->data + boff, chunk);
        brelse(b);

        dst += chunk;
        off += chunk;
        total += chunk;
    }
    return total;
}

/* Write to an inode */
int fs_write(struct inode *ip, const void *buf, uint32_t off, uint32_t n)
{
    if (!ip) return -EIO;

    uint32_t total = 0;
    const uint8_t *src = (const uint8_t *)buf;

    while (total < n) {
        uint16_t blk = bmap(ip, off, 1);
        if (blk == 0) return total > 0 ? (int)total : -ENOSPC;

        uint32_t boff = off % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - boff;
        if (chunk > n - total)
            chunk = n - total;

        struct buf *b = bread(0, blk);
        memcpy(b->data + boff, src, chunk);
        b->dirty = 1;
        bwrite(b);
        brelse(b);

        src += chunk;
        off += chunk;
        total += chunk;
    }

    if (off > ip->size) {
        ip->size = off;
        ip->dirty = 1;
    }
    ip->mtime = pal_timer_ticks();
    ip->dirty = 1;
    fs_iupdate(ip);
    return total;
}

/* Look up a name in a directory */
static struct inode *dir_lookup(struct inode *dp, const char *name, int namelen)
{
    if (dp->type != FT_DIR)
        return NULL;

    struct dirent_disk de;
    for (uint32_t off = 0; off < dp->size; off += sizeof(de)) {
        if (fs_read(dp, &de, off, sizeof(de)) != sizeof(de))
            break;
        if (de.inode == 0)
            continue;
        if (strncmp(de.name, name, namelen) == 0 && de.name[namelen] == '\0')
            return fs_iget(de.inode);
    }
    return NULL;
}

/* Add a directory entry */
static int dir_link(struct inode *dp, const char *name, uint16_t inum)
{
    /* Check for existing entry */
    int namelen = strlen(name);
    struct inode *check = dir_lookup(dp, name, namelen);
    if (check) {
        fs_iput(check);
        return -EEXIST;
    }

    /* Find a free slot or append */
    struct dirent_disk de;
    uint32_t off;
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (fs_read(dp, &de, off, sizeof(de)) != sizeof(de))
            break;
        if (de.inode == 0)
            goto found;
    }
    /* Append */
    off = dp->size;

found:
    memset(&de, 0, sizeof(de));
    de.inode = inum;
    strncpy(de.name, name, NAME_MAX - 1);
    de.name[NAME_MAX - 1] = '\0';

    if (fs_write(dp, &de, off, sizeof(de)) != sizeof(de))
        return -EIO;

    return 0;
}

/* Remove a directory entry by name */
static int dir_unlink(struct inode *dp, const char *name)
{
    struct dirent_disk de;
    for (uint32_t off = 0; off < dp->size; off += sizeof(de)) {
        if (fs_read(dp, &de, off, sizeof(de)) != sizeof(de))
            break;
        if (de.inode == 0)
            continue;
        if (strcmp(de.name, name) == 0) {
            de.inode = 0;
            memset(de.name, 0, NAME_MAX);
            fs_write(dp, &de, off, sizeof(de));
            return 0;
        }
    }
    return -ENOENT;
}

/* Resolve a pathname to an inode */
struct inode *fs_namei(const char *path)
{
    if (!fs_ready)
        return NULL;

    struct inode *ip;

    if (*path == '/')
        ip = fs_iget(1);  /* root inode is always 1 */
    else if (curproc)
        ip = fs_iget(curproc->cwd);
    else
        ip = fs_iget(1);

    if (!ip) return NULL;

    while (*path) {
        while (*path == '/') path++;
        if (*path == '\0') break;

        if (ip->type != FT_DIR) {
            fs_iput(ip);
            return NULL;
        }

        const char *name = path;
        while (*path && *path != '/') path++;
        int namelen = path - name;

        struct inode *next = dir_lookup(ip, name, namelen);
        fs_iput(ip);
        if (!next)
            return NULL;
        ip = next;
    }
    return ip;
}

/* Resolve parent directory and return the final component name */
static struct inode *fs_namei_parent(const char *path, char *name_out)
{
    if (!fs_ready)
        return NULL;

    /* Find last slash */
    const char *last_slash = strrchr(path, '/');
    struct inode *dp;

    if (!last_slash) {
        /* No slash — parent is cwd */
        dp = curproc ? fs_iget(curproc->cwd) : fs_iget(1);
        strncpy(name_out, path, NAME_MAX - 1);
        name_out[NAME_MAX - 1] = '\0';
    } else if (last_slash == path) {
        /* "/filename" — parent is root */
        dp = fs_iget(1);
        strncpy(name_out, path + 1, NAME_MAX - 1);
        name_out[NAME_MAX - 1] = '\0';
    } else {
        /* Copy parent path */
        char parent[PATH_MAX];
        int plen = last_slash - path;
        if (plen >= PATH_MAX) return NULL;
        memcpy(parent, path, plen);
        parent[plen] = '\0';
        dp = fs_namei(parent);
        strncpy(name_out, last_slash + 1, NAME_MAX - 1);
        name_out[NAME_MAX - 1] = '\0';
    }

    if (dp && dp->type != FT_DIR) {
        fs_iput(dp);
        return NULL;
    }
    return dp;
}

/* Create a new file/dir */
struct inode *fs_create(const char *path, uint8_t type)
{
    char name[NAME_MAX];
    struct inode *dp = fs_namei_parent(path, name);
    if (!dp) return NULL;

    /* Check if already exists */
    struct inode *existing = dir_lookup(dp, name, strlen(name));
    if (existing) {
        fs_iput(dp);
        return existing;
    }

    uint16_t inum = ialloc(type);
    if (inum == 0) {
        fs_iput(dp);
        return NULL;
    }

    struct inode *ip = fs_iget(inum);
    if (!ip) {
        fs_iput(dp);
        return NULL;
    }

    if (dir_link(dp, name, inum) < 0) {
        fs_iput(ip);
        fs_iput(dp);
        return NULL;
    }

    fs_iput(dp);
    return ip;
}

int fs_unlink(const char *path)
{
    char name[NAME_MAX];
    struct inode *dp = fs_namei_parent(path, name);
    if (!dp) return -ENOENT;

    struct inode *ip = dir_lookup(dp, name, strlen(name));
    if (!ip) {
        fs_iput(dp);
        return -ENOENT;
    }
    if (ip->type == FT_DIR) {
        fs_iput(ip);
        fs_iput(dp);
        return -EISDIR;
    }

    dir_unlink(dp, name);
    ip->nlink--;
    ip->dirty = 1;
    fs_iput(ip);
    fs_iput(dp);
    return 0;
}

int fs_rename(const char *oldpath, const char *newpath)
{
    struct inode *ip = fs_namei(oldpath);
    if (!ip) return -ENOENT;

    char oldname[NAME_MAX], newname[NAME_MAX];
    struct inode *old_dp = fs_namei_parent(oldpath, oldname);
    struct inode *new_dp = fs_namei_parent(newpath, newname);

    if (!old_dp || !new_dp) {
        if (old_dp) fs_iput(old_dp);
        if (new_dp) fs_iput(new_dp);
        fs_iput(ip);
        return -ENOENT;
    }

    /* Remove old entry, add new */
    dir_unlink(old_dp, oldname);
    dir_link(new_dp, newname, ip->inum);

    fs_iput(old_dp);
    fs_iput(new_dp);
    fs_iput(ip);
    return 0;
}

int fs_mkdir(const char *path)
{
    struct inode *ip = fs_create(path, FT_DIR);
    if (!ip) return -EIO;

    /* Add . and .. entries */
    char name[NAME_MAX];
    struct inode *dp = fs_namei_parent(path, name);
    uint16_t parent_inum = dp ? dp->inum : 1;
    if (dp) fs_iput(dp);

    dir_link(ip, ".", ip->inum);
    dir_link(ip, "..", parent_inum);

    fs_iput(ip);
    return 0;
}

int fs_rmdir(const char *path)
{
    struct inode *ip = fs_namei(path);
    if (!ip) return -ENOENT;
    if (ip->type != FT_DIR) {
        fs_iput(ip);
        return -ENOTDIR;
    }

    /* Check if empty (only . and ..) */
    struct dirent_disk de;
    int count = 0;
    for (uint32_t off = 0; off < ip->size; off += sizeof(de)) {
        if (fs_read(ip, &de, off, sizeof(de)) != sizeof(de))
            break;
        if (de.inode == 0) continue;
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
            continue;
        count++;
    }
    if (count > 0) {
        fs_iput(ip);
        return -ENOTEMPTY;
    }

    fs_iput(ip);
    return fs_unlink(path);
}

int fs_getdents(struct inode *ip, void *buf, uint32_t off, uint32_t n)
{
    if (!ip || ip->type != FT_DIR)
        return -ENOTDIR;
    return fs_read(ip, buf, off, n);
}

/* Simple stat — fills a flat struct */
struct kstat {
    uint16_t inum;
    uint8_t  type;
    uint8_t  nlink;
    uint32_t size;
    uint32_t mtime;
};

int fs_stat(struct inode *ip, void *buf)
{
    if (!ip) return -ENOENT;
    struct kstat *st = (struct kstat *)buf;
    st->inum = ip->inum;
    st->type = ip->type;
    st->nlink = ip->nlink;
    st->size = ip->size;
    st->mtime = ip->mtime;
    return 0;
}
