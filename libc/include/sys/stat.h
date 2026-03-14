/* Genix sys/stat.h — file status */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <stdint.h>

/* File type bits in st_mode */
#define S_IFMT   0170000   /* type mask */
#define S_IFREG  0100000   /* regular file */
#define S_IFDIR  0040000   /* directory */
#define S_IFCHR  0020000   /* character device */
#define S_IFBLK  0060000   /* block device */
#define S_IFIFO  0010000   /* FIFO/pipe */

/* Type test macros */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)

/* Set-ID and sticky bits (not enforced on Genix, but defined for POSIX) */
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

/* Symlink test (always false on Genix) */
#define S_IFLNK  0120000
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) 0

/* Permission bits */
#define S_IRWXU  0000700
#define S_IRUSR  0000400
#define S_IWUSR  0000200
#define S_IXUSR  0000100
#define S_IRWXG  0000070
#define S_IRGRP  0000040
#define S_IWGRP  0000020
#define S_IXGRP  0000010
#define S_IRWXO  0000007
#define S_IROTH  0000004
#define S_IWOTH  0000002
#define S_IXOTH  0000001

/*
 * POSIX-compatible struct stat.
 * All fields are even-sized for 68000 alignment safety.
 */
struct stat {
    uint16_t st_dev;    /* device (0 for minifs) */
    uint16_t st_ino;    /* inode number */
    uint16_t st_mode;   /* file type + permissions */
    uint16_t st_nlink;  /* hard link count */
    uint16_t st_uid;    /* owner (always 0) */
    uint16_t st_gid;    /* group (always 0) */
    uint16_t st_rdev;   /* device type (major<<8|minor for devs) */
    uint16_t st_pad;    /* alignment padding */
    uint32_t st_size;   /* file size in bytes */
    uint32_t st_atime;  /* access time (= mtime) */
    uint32_t st_mtime;  /* modification time */
    uint32_t st_ctime;  /* change time (= mtime) */
};

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);

#endif
