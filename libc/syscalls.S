/*
 * syscalls.S — Genix syscall stubs for user programs
 *
 * Convention: syscall number in d0, args in d1-d4
 * Entry via TRAP #0, return value in d0 (negative = -errno)
 *
 * The C ABI requires d2-d7/a2-a6 to be preserved by callees.
 * Since we put args in d2-d4, we must save/restore them.
 */

    .text

/* void _exit(int code) __attribute__((noreturn)) */
    .globl _exit
_exit:
    move.l  4(%sp), %d1         /* code */
    moveq   #1, %d0             /* SYS_EXIT */
    trap    #0
    /* not reached */
1:  bra     1b

/* int read(int fd, void *buf, int count) */
    .globl read
read:
    movem.l %d2-%d3, -(%sp)    /* save callee-saved */
    move.l  12(%sp), %d1       /* fd   (4+8=12) */
    move.l  16(%sp), %d2       /* buf  */
    move.l  20(%sp), %d3       /* count */
    moveq   #3, %d0            /* SYS_READ */
    trap    #0
    movem.l (%sp)+, %d2-%d3    /* restore */
    rts

/* int write(int fd, const void *buf, int count) */
    .globl write
write:
    movem.l %d2-%d3, -(%sp)    /* save callee-saved */
    move.l  12(%sp), %d1       /* fd   */
    move.l  16(%sp), %d2       /* buf  */
    move.l  20(%sp), %d3       /* count */
    moveq   #4, %d0            /* SYS_WRITE */
    trap    #0
    movem.l (%sp)+, %d2-%d3    /* restore */
    rts

/* int open(const char *path, int flags) */
    .globl open
open:
    move.l  %d2, -(%sp)        /* save callee-saved */
    move.l  8(%sp), %d1        /* path  (4+4=8) */
    move.l  12(%sp), %d2       /* flags */
    moveq   #5, %d0            /* SYS_OPEN */
    trap    #0
    move.l  (%sp)+, %d2        /* restore */
    rts

/* int close(int fd) */
    .globl close
close:
    move.l  4(%sp), %d1        /* fd */
    moveq   #6, %d0            /* SYS_CLOSE */
    trap    #0
    rts

/* int lseek(int fd, int offset, int whence) */
    .globl lseek
lseek:
    movem.l %d2-%d3, -(%sp)
    move.l  12(%sp), %d1       /* fd */
    move.l  16(%sp), %d2       /* offset */
    move.l  20(%sp), %d3       /* whence */
    moveq   #19, %d0           /* SYS_LSEEK */
    trap    #0
    movem.l (%sp)+, %d2-%d3
    rts

/* void *brk(void *addr) */
    .globl brk
brk:
    move.l  4(%sp), %d1        /* addr */
    move.l  #45, %d0           /* SYS_BRK */
    trap    #0
    rts

/* int getpid(void) */
    .globl getpid
getpid:
    moveq   #20, %d0           /* SYS_GETPID */
    trap    #0
    rts

/* int stat(const char *path, void *buf) */
    .globl stat
stat:
    move.l  %d2, -(%sp)
    move.l  8(%sp), %d1        /* path */
    move.l  12(%sp), %d2       /* buf */
    move.l  #106, %d0          /* SYS_STAT */
    trap    #0
    move.l  (%sp)+, %d2
    rts

/* int fstat(int fd, void *buf) */
    .globl fstat
fstat:
    move.l  %d2, -(%sp)
    move.l  8(%sp), %d1        /* fd */
    move.l  12(%sp), %d2       /* buf */
    move.l  #108, %d0          /* SYS_FSTAT */
    trap    #0
    move.l  (%sp)+, %d2
    rts

/* int dup(int fd) */
    .globl dup
dup:
    move.l  4(%sp), %d1        /* fd */
    move.l  #41, %d0           /* SYS_DUP */
    trap    #0
    rts

/* int dup2(int oldfd, int newfd) */
    .globl dup2
dup2:
    move.l  %d2, -(%sp)
    move.l  8(%sp), %d1        /* oldfd */
    move.l  12(%sp), %d2       /* newfd */
    move.l  #63, %d0           /* SYS_DUP2 */
    trap    #0
    move.l  (%sp)+, %d2
    rts

/* int chdir(const char *path) */
    .globl chdir
chdir:
    move.l  4(%sp), %d1        /* path */
    moveq   #12, %d0           /* SYS_CHDIR */
    trap    #0
    rts

/* int mkdir(const char *path) */
    .globl mkdir
mkdir:
    move.l  4(%sp), %d1        /* path */
    move.l  #39, %d0           /* SYS_MKDIR */
    trap    #0
    rts

/* int unlink(const char *path) */
    .globl unlink
unlink:
    move.l  4(%sp), %d1        /* path */
    moveq   #10, %d0           /* SYS_UNLINK */
    trap    #0
    rts
