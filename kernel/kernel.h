/*
 * Genix kernel header — everything in one place
 */
#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

/* Bool */
#define true  1
#define false 0
typedef int bool;

/* Limits */
#define MAXPROC     16
#define MAXFD       16
#define MAXOPEN     64       /* system-wide open file table */
#define MAXINODE    128
#define NAME_MAX    30
#define PATH_MAX    256
#define BLOCK_SIZE  1024
#define NBUFS       16

/* Error numbers */
#define EPERM        1
#define ENOENT       2
#define ESRCH        3
#define EINTR        4
#define EIO          5
#define ENXIO        6
#define E2BIG        7
#define ENOEXEC      8
#define EBADF        9
#define ECHILD      10
#define EAGAIN      11
#define ENOMEM      12
#define EACCES      13
#define EFAULT      14
#define EBUSY       16
#define EEXIST      17
#define EXDEV       18
#define ENODEV      19
#define ENOTDIR     20
#define EISDIR      21
#define EINVAL      22
#define ENFILE      23
#define EMFILE      24
#define ENOTTY      25
#define EFBIG       27
#define ENOSPC      28
#define ESPIPE      29
#define EROFS       30
#define EPIPE       32
#define ENOSYS      38
#define ENOTEMPTY   39
#define ENAMETOOLONG 36

/* Syscall numbers */
#define SYS_EXIT        1
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_WAITPID     7
#define SYS_EXEC        11
#define SYS_CHDIR       12
#define SYS_TIME        13
#define SYS_LSEEK       19
#define SYS_GETPID      20
#define SYS_MKDIR       39
#define SYS_RMDIR       40
#define SYS_DUP         41
#define SYS_PIPE        42
#define SYS_TIMES       43
#define SYS_BRK         45
#define SYS_IOCTL       54
#define SYS_DUP2        63
#define SYS_GETCWD      79
#define SYS_STAT        106
#define SYS_FSTAT       108
#define SYS_UNLINK      10
#define SYS_RENAME      38
#define SYS_FCNTL       55
#define SYS_GETDENTS    141
#define SYS_VFORK       190
#define SYS_KILL        37
#define SYS_SIGNAL      48
#define SYS_SBRK        69

/* User memory layout (single-tasking, fixed load address) */
#define USER_BASE   0x040000    /* user programs load here */
#define USER_TOP    0x0F0000    /* user stack starts here (grows down) */
#define USER_SIZE   (USER_TOP - USER_BASE)

/* Default user stack size */
#define USER_STACK_DEFAULT  4096

/* Genix binary format header (32 bytes, big-endian on disk) */
#define GENIX_MAGIC  0x47454E58  /* "GENX" */
#define GENIX_HDR_SIZE 32

struct genix_header {
    uint32_t magic;       /* 0x47454E58 "GENX" */
    uint32_t load_size;   /* bytes to copy from file (text+data) */
    uint32_t bss_size;    /* bytes to zero after load_size */
    uint32_t entry;       /* absolute entry point address */
    uint32_t stack_size;  /* stack size hint (0 = default 4KB) */
    uint32_t flags;       /* reserved, must be 0 */
    uint32_t reserved[2]; /* pad to 32 bytes */
};

/* Seek modes */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/* Open flags */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_EXCL      0x0080

/* File types (inode) */
#define FT_FREE     0
#define FT_FILE     1
#define FT_DIR      2
#define FT_DEV      3

/* ======== PAL (Platform Abstraction Layer) ======== */

void     pal_init(void);
void     pal_console_putc(char c);
int      pal_console_getc(void);     /* blocking */
int      pal_console_ready(void);    /* char available? */
void     pal_disk_read(int dev, uint32_t block, void *buf);
void     pal_disk_write(int dev, uint32_t block, void *buf);
uint32_t pal_mem_start(void);
uint32_t pal_mem_end(void);
void     pal_timer_init(int hz);
uint32_t pal_timer_ticks(void);

/* ======== Console I/O ======== */

void kputc(char c);
void kputs(const char *s);
void kprintf(const char *fmt, ...);
int  kgetc(void);

/* ======== Memory allocator ======== */

void  mem_init(uint32_t start, uint32_t end);
void *kmalloc(uint32_t size);
void  kfree(void *ptr);
void *sbrk_proc(int32_t incr);

/* ======== Filesystem (minifs) ======== */

/* On-disk structures */
struct superblock {
    uint32_t magic;         /* 0x4D494E49 "MINI" */
    uint16_t block_size;
    uint16_t nblocks;
    uint16_t ninodes;
    uint16_t free_list;     /* head of free block list */
    uint16_t free_inodes;
    uint16_t pad;
    uint32_t mtime;
};

struct disk_inode {
    uint8_t  type;          /* FT_FREE, FT_FILE, FT_DIR, FT_DEV */
    uint8_t  nlink;
    uint8_t  dev_major;     /* for FT_DEV */
    uint8_t  dev_minor;     /* for FT_DEV */
    uint32_t size;
    uint32_t mtime;
    uint16_t direct[12];    /* 12 direct blocks = 12KB */
    uint16_t indirect;      /* single indirect = +512 blocks */
    uint8_t  pad[10];
};
/* sizeof(disk_inode) = 48 bytes, 21 inodes per block */

struct dirent_disk {
    uint16_t inode;
    char     name[NAME_MAX];
};
/* sizeof(dirent_disk) = 32 bytes, 32 entries per block */

/* In-memory inode */
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
    uint8_t  refcount;      /* number of open fds pointing here */
    uint8_t  dirty;
};

/* Open file table entry */
struct ofile {
    struct inode *inode;
    uint32_t offset;
    uint16_t flags;
    uint8_t  refcount;
};

/* Filesystem interface */
void     fs_init(void);
struct inode *fs_iget(uint16_t inum);
void     fs_iput(struct inode *ip);
void     fs_iupdate(struct inode *ip);
struct inode *fs_namei(const char *path);
struct inode *fs_create(const char *path, uint8_t type);
int      fs_read(struct inode *ip, void *buf, uint32_t off, uint32_t n);
int      fs_write(struct inode *ip, const void *buf, uint32_t off, uint32_t n);
int      fs_unlink(const char *path);
int      fs_rename(const char *oldpath, const char *newpath);
int      fs_mkdir(const char *path);
int      fs_rmdir(const char *path);
int      fs_getdents(struct inode *ip, void *buf, uint32_t off, uint32_t n);
int      fs_stat(struct inode *ip, void *buf);

/* ======== Device table ======== */

struct device {
    int (*open)(int minor);
    int (*close)(int minor);
    int (*read)(int minor, void *buf, int len);
    int (*write)(int minor, const void *buf, int len);
    int (*ioctl)(int minor, int cmd, void *arg);
};

#define DEV_CONSOLE  0
#define DEV_DISK     1
#define NDEV         2

extern struct device devtab[];
void dev_init(void);

/* ======== Process management ======== */

#define P_FREE      0
#define P_RUNNING   1
#define P_READY     2
#define P_SLEEPING  3
#define P_ZOMBIE    4
#define P_VFORK     5      /* parent waiting for child vfork */

struct proc {
    uint8_t  state;
    uint8_t  pid;
    uint8_t  ppid;
    int8_t   exitcode;
    uint32_t sp;           /* saved stack pointer */
    uint32_t pc;           /* saved program counter */
    uint32_t regs[16];     /* d0-d7, a0-a7 saved registers */
    uint16_t sr;           /* saved status register */
    uint32_t mem_base;     /* start of process memory */
    uint32_t mem_size;     /* size of allocated memory */
    uint32_t brk;          /* current break (top of data) */
    uint16_t cwd;          /* current working directory inode */
    struct ofile *fd[MAXFD];
};

extern struct proc proctab[];
extern struct proc *curproc;
extern int nproc;

void proc_init(void);
int  do_exec(const char *path, const char **argv);
void do_exit(int code);
int  do_waitpid(int pid, int *status);
int  do_vfork(void);

/* Exec support (assembly in exec_asm.S) */
extern int exec_enter(uint32_t entry, uint32_t sp);
extern void exec_leave(void);
extern int exec_exit_code;
extern int exec_active;

/* ======== Syscall dispatch ======== */

int32_t syscall_dispatch(uint32_t num, uint32_t a1, uint32_t a2,
                         uint32_t a3, uint32_t a4);

/* ======== Buffer cache ======== */

struct buf {
    uint16_t blockno;
    uint8_t  dev;
    uint8_t  dirty;
    uint8_t  valid;
    uint8_t  data[BLOCK_SIZE];
};

struct buf *bread(uint8_t dev, uint16_t blockno);
void        bwrite(struct buf *b);
void        brelse(struct buf *b);
void        buf_init(void);

/* ======== Utility ======== */

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);
int   strcmp(const char *s1, const char *s2);
int   strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);

#endif /* KERNEL_H */
