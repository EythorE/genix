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
#ifndef NBUFS
#define NBUFS       16
#endif

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
#define ERANGE      34
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
#define SYS_SIGRETURN   119
#define SYS_MEMINFO     200

/* Process state for stopped jobs */
#define P_STOPPED   6

/* User memory layout — platform-provided, set at boot by kmain().
 * Workbench: 0x040000-0x0F0000 (704 KB in 1 MB RAM)
 * Mega Drive: 0xFF9000-0xFFFE00 (~27.5 KB in 64 KB RAM)  */
extern uint32_t USER_BASE;   /* user programs load here */
extern uint32_t USER_TOP;    /* user stack starts here (grows down) */
extern uint32_t USER_SIZE;   /* USER_TOP - USER_BASE */

/* Default user stack size */
#define USER_STACK_DEFAULT  4096

/* Genix binary format header (32 bytes, big-endian on disk)
 *
 * All binaries are relocatable: linked at address 0 with a relocation
 * table appended after text+data. The kernel adds the load address to
 * each relocated 32-bit word at exec() time.
 *
 * Binary file layout:
 *   [32-byte header]
 *   [text+data: load_size bytes]
 *   [relocation table: reloc_count * 4 bytes]
 */
#define GENIX_MAGIC  0x47454E58  /* "GENX" */
#define GENIX_HDR_SIZE 32

/* Header flags */
#define GENIX_FLAG_XIP  0x01  /* XIP-resolved: text in ROM, data refs → USER_BASE */

/* Extract actual stack size and GOT offset from packed stack_size field.
 * GOT offset is stored as (offset + 1) to distinguish "no GOT" (0)
 * from "GOT at data start" (1). HDR_HAS_GOT checks if GOT exists,
 * HDR_GOT_OFFSET returns the actual byte offset. */
#define HDR_STACK_SIZE(hdr)  ((hdr)->stack_size & 0xFFFF)
#define HDR_HAS_GOT(hdr)    (((hdr)->stack_size >> 16) != 0)
#define HDR_GOT_OFFSET(hdr)  (((hdr)->stack_size >> 16) - 1)

struct genix_header {
    uint32_t magic;       /* 0x47454E58 "GENX" */
    uint32_t load_size;   /* text+data bytes to load */
    uint32_t bss_size;    /* bytes to zero after load */
    uint32_t entry;       /* entry point offset (0-based) */
    uint32_t stack_size;  /* bits 0-15: stack hint, bits 16-31: GOT offset */
    uint32_t flags;       /* GENIX_FLAG_XIP etc. */
    uint32_t text_size;   /* text segment size (for split reloc / XIP) */
    uint32_t reloc_count; /* number of uint32_t relocation entries */
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

#include "pal.h"

/* ======== Console I/O ======== */

void kputc(char c);
void kputs(const char *s);
void kprintf(const char *fmt, ...);
int  kgetc(void);

/* ======== TTY subsystem ======== */
/* Full declarations in tty.h — include it where needed */

/* ======== Memory allocator ======== */

void  mem_init(uint32_t start, uint32_t end);
void *kmalloc(uint32_t size);
void  kfree(void *ptr);
void *sbrk_proc(int32_t incr);
void  kmem_stats(uint32_t *total, uint32_t *free_bytes, uint32_t *largest);

#define MAX_REGIONS   16  /* max concurrent user memory regions (= MAXPROC) */

/* ======== Memory info (SYS_MEMINFO) ======== */

struct region_info {
    uint8_t  used;        /* 1 if region is occupied */
    uint8_t  pid;         /* owning process PID (0 if free) */
    uint16_t _pad;
    uint32_t base;        /* region base address */
    uint32_t size;        /* region size */
    uint32_t text_size;   /* text in ROM (XIP), 0 if non-XIP */
    uint32_t data_bss;    /* data+bss bytes used */
    uint32_t brk;         /* current heap break */
};

struct meminfo {
    /* Kernel heap */
    uint32_t kheap_total;
    uint32_t kheap_free;
    uint32_t kheap_largest;
    /* User memory */
    uint32_t user_base;
    uint32_t user_top;
    uint32_t user_free;     /* total free bytes in user pool */
    uint32_t user_largest;  /* largest contiguous free region */
    struct region_info regions[MAX_REGIONS];
};

/* ======== User memory allocator ======== */

void     umem_init(void);
uint32_t umem_alloc(uint32_t need);   /* returns base address or 0 */
void     umem_free(uint32_t base);
void     umem_stats(uint32_t *free_bytes, uint32_t *largest);

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
#define DEV_VDP      2
#define DEV_NULL     3
#define NDEV         4

extern struct device devtab[];
void dev_init(void);
void dev_create_nodes(void);

/* ======== Pipes ======== */

#define PIPE_SIZE   512

struct pipe {
    uint8_t  buf[PIPE_SIZE];
    uint16_t read_pos;
    uint16_t write_pos;
    uint16_t count;
    uint8_t  readers;
    uint8_t  writers;
    uint8_t  read_waiting;   /* process waiting for data */
    uint8_t  write_waiting;  /* process waiting for space */
};

#define MAXPIPE     4
extern struct pipe pipe_table[];

int  do_pipe(int *fds);
int  pipe_read(struct pipe *p, void *buf, int len);
int  pipe_write(struct pipe *p, const void *buf, int len);
void pipe_close_read(struct pipe *p);
void pipe_close_write(struct pipe *p);

/* ======== Signals ======== */

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGKILL  9
#define SIGPIPE  13
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define NSIG     21   /* signals 0..20 */

#define SIG_DFL  0
#define SIG_IGN  1

/* Check and deliver pending signals. Called on return to user mode.
 * frame points to the saved user state on kstack:
 *   frame[0]=USP, frame[1..15]=d0-d7/a0-a6,
 *   byte offset 64=SR(16-bit), byte offset 66=PC(32-bit) */
void sig_deliver(uint32_t *frame);

/* Sigreturn: restore saved state from signal frame on user stack.
 * Called from _vec_syscall when syscall number is SYS_SIGRETURN. */
void sys_sigreturn(uint32_t *frame);

/* ======== Process management ======== */

#define P_FREE      0
#define P_RUNNING   1
#define P_READY     2
#define P_SLEEPING  3
#define P_ZOMBIE    4
#define P_VFORK     5      /* parent waiting for child vfork */

/* Per-process kernel stack size in bytes.
 * Must hold: exception frame (6) + saved regs (60) + USP (4) +
 * syscall args (20) + C call chain (~120) = ~210 bytes minimum.
 * The deepest path is TRAP→syscall_dispatch→sys_write→con_write→kputc→pal_putc.
 * 512 bytes gives comfortable headroom for nested timer ISR. */
#define KSTACK_SIZE   512
#define KSTACK_WORDS  (KSTACK_SIZE / 4)  /* 64 uint32_t entries */
#define KSTACK_CANARY 0xDEADBEEF         /* overflow detection */

struct proc {
    uint8_t  state;
    uint8_t  pid;
    uint8_t  ppid;
    int8_t   exitcode;
    uint32_t ksp;          /* saved kernel stack pointer (into kstack[]) */
    uint32_t mem_base;     /* start of process memory (0 = none) */
    uint32_t mem_size;     /* size of allocated memory */
    uint32_t brk;          /* current break (top of data) */
    uint32_t data_a5;      /* a5 value for -msep-data (GOT base) */
    uint32_t text_size;    /* text segment size (0 if non-XIP, ROM bytes if XIP) */
    uint32_t data_bss;     /* data+bss size loaded into slot */
    uint16_t cwd;          /* current working directory inode */
    uint32_t vfork_ctx[13]; /* vfork_save context (d2-d7,a2-a6,sp,retaddr) */
    uint8_t  pgrp;         /* process group ID (for job control) */
    uint8_t  _pad2;        /* align to even boundary */
    uint32_t sig_pending;  /* bitmask of pending signals */
    uint32_t sig_handler[NSIG]; /* signal handlers (0=SIG_DFL, 1=SIG_IGN, else addr) */
    struct ofile *fd[MAXFD];
    uint32_t kstack[KSTACK_WORDS]; /* per-process kernel stack */
};

extern struct proc proctab[];
extern struct proc *curproc;
extern int nproc;

void proc_init(void);
int  load_binary(const char *path, const char **argv, uint32_t load_addr,
                 uint32_t *entry_out, uint32_t *user_sp_out);
int  load_binary_xip(const char *path, const char **argv,
                     uint32_t text_addr, uint32_t data_addr,
                     uint32_t *entry_out, uint32_t *user_sp_out);
int  exec_validate_header(const struct genix_header *hdr, uint32_t region_size);
int  exec_validate_header_xip(const struct genix_header *hdr, uint32_t region_size);
uint32_t exec_mem_need(const struct genix_header *hdr);
uint32_t exec_mem_need_xip(const struct genix_header *hdr);
void apply_relocations_xip(uint8_t *text_mem, uint32_t text_base,
                            uint8_t *data_mem, uint32_t data_base,
                            uint32_t text_size, uint32_t load_size,
                            const uint32_t *relocs, uint32_t nrelocs);
int  do_exec(const char *path, const char **argv);
void do_exit(int code);
int  do_waitpid(int pid, int *status, int options);
int  do_vfork(void);
int  do_spawn(const char *path, const char **argv);
int  do_spawn_fd(const char *path, const char **argv,
                 int stdin_fd, int stdout_fd, int stderr_fd);
void schedule(void);

/* Exec support (assembly in exec_asm.S) */
extern int exec_enter(uint32_t entry, uint32_t user_sp, uint32_t kstack_top);
extern void exec_leave(void);
extern int exec_exit_code;
extern int exec_active;
extern uint32_t exec_user_a5;

/* Context switch (assembly in exec_asm.S) */
extern void swtch(uint32_t *old_ksp, uint32_t new_ksp);
extern void proc_first_run(void);  /* trampoline for new process entry */

/* Set up a process's kstack for its first context switch.
 * user_a5 is the GOT base for -msep-data processes (0 if not applicable). */
void proc_setup_kstack(struct proc *p, uint32_t entry, uint32_t user_sp,
                       uint32_t user_a5);

/* Helper: get the top of a process's kernel stack */
static inline uint32_t proc_kstack_top(struct proc *p)
{
    return (uint32_t)&p->kstack[KSTACK_WORDS];
}

/* vfork support (assembly in exec_asm.S) — setjmp/longjmp for kernel */
extern int vfork_save(uint32_t *regs);
extern void vfork_restore(uint32_t *regs, int retval);

/* ======== Syscall dispatch ======== */

int32_t syscall_dispatch(uint32_t num, uint32_t a1, uint32_t a2,
                         uint32_t a3, uint32_t a4);

/* ======== Buffer cache ======== */

struct buf {
    uint16_t blockno;
    uint8_t  dev;
    uint8_t  dirty;
    uint8_t  valid;
    uint8_t  _pad[3]; /* align data[] to offset 8 — fs code casts
                       * data+offset to struct pointers with uint16/32
                       * fields, and the 68000 faults on misaligned access */
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
