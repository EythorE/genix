# minifs — Minimal Unix Filesystem

minifs is a custom filesystem with a classic Unix inode layout, designed
to be simple enough to implement in ~600 lines of C.

## On-Disk Layout

```
Block 0:        Superblock
Block 1..N:     Inode table (21 inodes per 1 KB block)
Block N+1..:    Data blocks (free list linked through first 2 bytes)
```

### Superblock (16 bytes)

```c
struct superblock {
    uint32_t magic;         // 0x4D494E49 "MINI"
    uint16_t block_size;    // 1024
    uint16_t nblocks;       // total blocks in filesystem
    uint16_t ninodes;       // total inodes
    uint16_t free_list;     // head of free block list
    uint16_t free_inodes;   // (unused counter)
    uint16_t pad;
    uint32_t mtime;         // last mount time
};
```

### Disk Inode (48 bytes, 21 per block)

```c
struct disk_inode {
    uint8_t  type;          // FT_FREE=0, FT_FILE=1, FT_DIR=2, FT_DEV=3
    uint8_t  nlink;
    uint8_t  dev_major;     // for device files
    uint8_t  dev_minor;
    uint32_t size;
    uint32_t mtime;
    uint16_t direct[12];    // 12 direct blocks = 12 KB max without indirect
    uint16_t indirect;      // single indirect block = +512 blocks
    uint8_t  pad[10];
};
```

Maximum file size: 12 direct + 512 indirect = 524 blocks = **524 KB**.
This is more than enough for the Mega Drive's available RAM.

### Directory Entry (32 bytes, 32 per block)

```c
struct dirent_disk {
    uint16_t inode;         // inode number (0 = free slot)
    char     name[30];      // null-terminated filename
};
```

Maximum filename length: 29 characters (`NAME_MAX = 30` including null).

## In-Memory Structures

### Inode Cache

```c
struct inode {
    uint16_t inum;
    uint8_t  type, nlink;
    uint8_t  dev_major, dev_minor;
    uint32_t size, mtime;
    uint16_t direct[12];
    uint16_t indirect;
    uint8_t  refcount;      // open references
    uint8_t  dirty;         // needs write-back
};
```

Up to `MAXINODE = 128` inodes cached in memory. Looked up by inode
number with linear scan (fast enough at this scale).

## Key Operations

### Block Mapping (`bmap`)

Maps a file offset to a disk block number:

```
offset < 12 KB    → direct[offset / BLOCK_SIZE]
offset < 524 KB   → indirect block pointer table
```

If `alloc` is set, missing blocks are allocated from the free list.

### Free Block List

Free blocks form a linked list through their first 2 bytes (big-endian
block number of the next free block). The superblock's `free_list`
field points to the head.

```
balloc():  pop head of free list, update superblock
bfree():   push block onto head of free list
```

### Path Resolution (`fs_namei`)

Walks a `/`-separated path from root (inode 1) or cwd, looking up each
component with `dir_lookup()`. Returns the final inode or NULL.

### Directory Operations

- **`dir_lookup(dp, name, namelen)`** — scan directory entries for a
  matching name
- **`dir_link(dp, name, inum)`** — add a new entry (find free slot or
  append)
- **`dir_unlink(dp, name)`** — zero out the entry's inode number

### File Operations

- **`fs_read(ip, buf, off, n)`** — read bytes from an inode via buffer
  cache
- **`fs_write(ip, buf, off, n)`** — write bytes, allocating blocks as
  needed
- **`fs_create(path, type)`** — allocate inode, link into parent
  directory
- **`fs_unlink(path)`** — remove directory entry, decrement nlink
- **`fs_mkdir(path)`** — create directory with `.` and `..` entries
- **`fs_rmdir(path)`** — remove directory (must be empty)
- **`fs_rename(old, new)`** — unlink from old parent, link into new

## mkfs Tool (`tools/mkfs.c`)

Host tool to create a filesystem image:

```bash
tools/mkfs.minifs disk.img 512 apps/hello apps/echo apps/cat
```

Creates a 512-block image with the specified files placed in `/bin/`.

## Division in Filesystem Code

Block arithmetic uses division by `BLOCK_SIZE` (1024). Since 1024 is
a power of 2, GCC optimizes these to shifts:

```c
uint32_t bn = offset / BLOCK_SIZE;    // → lsr.l #10, dn
uint32_t boff = offset % BLOCK_SIZE;  // → and.l #0x3FF, dn
```

Inode table lookups use division by `INODES_PER_BLK` (21), which is
not a power of 2. This hits the 32-bit divide path but is not
performance-critical (inode lookups are infrequent compared to data
block reads). See [68000-programming.md](68000-programming.md) for
division strategies.
