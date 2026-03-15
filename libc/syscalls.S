/*
 * syscalls.S — Genix syscall stubs for user programs
 *
 * Convention: syscall number in d0, args in d1-d4
 * Entry via TRAP #0, return value in d0 (negative = -errno)
 *
 * The C ABI requires d2-d7/a2-a6 to be preserved by callees.
 * Since we put args in d2-d4, we must save/restore them.
 *
 * POSIX errno convention: on error, stubs set the global errno
 * variable to the positive error number and return -1. On success,
 * the raw non-negative value is returned unchanged.
 */

    .text

/*
 * __set_errno — common tail for syscall stubs
 *
 * If d0 < 0: negate into errno, return -1.
 * If d0 >= 0: return d0 unchanged.
 */
__set_errno:
    tst.l   %d0
    bpl     1f
    neg.l   %d0
    move.l  %d0, errno
    moveq   #-1, %d0
1:  rts

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
    bra     __set_errno

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
    bra     __set_errno

/* int open(const char *path, int flags) */
    .globl open
open:
    move.l  %d2, -(%sp)        /* save callee-saved */
    move.l  8(%sp), %d1        /* path  (4+4=8) */
    move.l  12(%sp), %d2       /* flags */
    moveq   #5, %d0            /* SYS_OPEN */
    trap    #0
    move.l  (%sp)+, %d2        /* restore */
    bra     __set_errno

/* int close(int fd) */
    .globl close
close:
    move.l  4(%sp), %d1        /* fd */
    moveq   #6, %d0            /* SYS_CLOSE */
    trap    #0
    bra     __set_errno

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
    bra     __set_errno

/* void *brk(void *addr) — returns pointer, not -errno */
    .globl brk
brk:
    move.l  4(%sp), %d1        /* addr */
    move.l  #45, %d0           /* SYS_BRK */
    trap    #0
    rts

/* int getpid(void) — always succeeds */
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
    bra     __set_errno

/* int fstat(int fd, void *buf) */
    .globl fstat
fstat:
    move.l  %d2, -(%sp)
    move.l  8(%sp), %d1        /* fd */
    move.l  12(%sp), %d2       /* buf */
    move.l  #108, %d0          /* SYS_FSTAT */
    trap    #0
    move.l  (%sp)+, %d2
    bra     __set_errno

/* int dup(int fd) */
    .globl dup
dup:
    move.l  4(%sp), %d1        /* fd */
    move.l  #41, %d0           /* SYS_DUP */
    trap    #0
    bra     __set_errno

/* int dup2(int oldfd, int newfd) */
    .globl dup2
dup2:
    move.l  %d2, -(%sp)
    move.l  8(%sp), %d1        /* oldfd */
    move.l  12(%sp), %d2       /* newfd */
    move.l  #63, %d0           /* SYS_DUP2 */
    trap    #0
    move.l  (%sp)+, %d2
    bra     __set_errno

/* int chdir(const char *path) */
    .globl chdir
chdir:
    move.l  4(%sp), %d1        /* path */
    moveq   #12, %d0           /* SYS_CHDIR */
    trap    #0
    bra     __set_errno

/* int mkdir(const char *path) */
    .globl mkdir
mkdir:
    move.l  4(%sp), %d1        /* path */
    move.l  #39, %d0           /* SYS_MKDIR */
    trap    #0
    bra     __set_errno

/* int unlink(const char *path) */
    .globl unlink
unlink:
    move.l  4(%sp), %d1        /* path */
    moveq   #10, %d0           /* SYS_UNLINK */
    trap    #0
    bra     __set_errno

/* int rename(const char *old, const char *new) */
    .globl rename
rename:
    move.l  %d2, -(%sp)
    move.l  8(%sp), %d1        /* old */
    move.l  12(%sp), %d2       /* new */
    move.l  #38, %d0           /* SYS_RENAME */
    trap    #0
    move.l  (%sp)+, %d2
    bra     __set_errno

/* int ioctl(int fd, int cmd, void *arg) */
    .globl ioctl
ioctl:
    movem.l %d2-%d3, -(%sp)
    move.l  12(%sp), %d1       /* fd */
    move.l  16(%sp), %d2       /* cmd */
    move.l  20(%sp), %d3       /* arg */
    move.l  #54, %d0           /* SYS_IOCTL */
    trap    #0
    movem.l (%sp)+, %d2-%d3
    bra     __set_errno

/* void *sbrk(int incr) — returns pointer, not -errno */
    .globl sbrk
sbrk:
    move.l  4(%sp), %d1        /* incr */
    move.l  #69, %d0           /* SYS_SBRK */
    trap    #0
    rts

/* int signal(int sig, void *handler) */
    .globl signal
signal:
    move.l  %d2, -(%sp)
    move.l  8(%sp), %d1        /* sig */
    move.l  12(%sp), %d2       /* handler */
    move.l  #48, %d0           /* SYS_SIGNAL */
    trap    #0
    move.l  (%sp)+, %d2
    bra     __set_errno

/* int kill(int pid, int sig) */
    .globl kill
kill:
    move.l  %d2, -(%sp)
    move.l  8(%sp), %d1        /* pid */
    move.l  12(%sp), %d2       /* sig */
    move.l  #37, %d0           /* SYS_KILL */
    trap    #0
    move.l  (%sp)+, %d2
    bra     __set_errno

/* int fcntl(int fd, int cmd, ...) */
    .globl fcntl
fcntl:
    movem.l %d2-%d3, -(%sp)
    move.l  12(%sp), %d1       /* fd */
    move.l  16(%sp), %d2       /* cmd */
    move.l  20(%sp), %d3       /* arg */
    move.l  #55, %d0           /* SYS_FCNTL */
    trap    #0
    movem.l (%sp)+, %d2-%d3
    bra     __set_errno

/*
 * int vfork(void)
 *
 * Special stub: saves return address in a1 (not on stack) because
 * the child shares the parent's stack. The child must call exec()
 * or _exit() immediately.
 *
 * Returns 0 to child, child PID to parent, -1 on error (sets errno).
 */
    .globl vfork
vfork:
    move.l  (%sp)+, %a1         /* save return address in a1 */
    move.l  #190, %d0           /* SYS_VFORK */
    trap    #0
    move.l  %a1, -(%sp)         /* restore return address */
    bra     __set_errno

/* int waitpid(int pid, int *status, int options) */
    .globl waitpid
waitpid:
    movem.l %d2-%d3, -(%sp)
    move.l  12(%sp), %d1       /* pid */
    move.l  16(%sp), %d2       /* status */
    move.l  20(%sp), %d3       /* options */
    moveq   #7, %d0            /* SYS_WAITPID */
    trap    #0
    movem.l (%sp)+, %d2-%d3
    bra     __set_errno

/* int execve(const char *path, char *const argv[], char *const envp[]) */
    .globl execve
execve:
    movem.l %d2-%d3, -(%sp)
    move.l  12(%sp), %d1       /* path */
    move.l  16(%sp), %d2       /* argv */
    move.l  20(%sp), %d3       /* envp (ignored) */
    moveq   #11, %d0           /* SYS_EXEC */
    trap    #0
    /* execve only returns on error (success replaces process) */
    movem.l (%sp)+, %d2-%d3
    bra     __set_errno

/* int pipe(int pipefd[2]) */
    .globl pipe
pipe:
    move.l  4(%sp), %d1        /* pipefd */
    move.l  #42, %d0           /* SYS_PIPE */
    trap    #0
    bra     __set_errno

/* int getdents(int fd, void *buf, int count) */
    .globl getdents
getdents:
    movem.l %d2-%d3, -(%sp)
    move.l  12(%sp), %d1       /* fd */
    move.l  16(%sp), %d2       /* buf */
    move.l  20(%sp), %d3       /* count */
    move.l  #141, %d0          /* SYS_GETDENTS */
    trap    #0
    movem.l (%sp)+, %d2-%d3
    bra     __set_errno

/*
 * char *getcwd(char *buf, int size)
 *
 * Returns buf pointer on success, negative errno on failure.
 * Route through __set_errno: success values are positive pointers,
 * errors are small negative numbers — same pattern as other stubs.
 */
    .globl getcwd
getcwd:
    move.l  %d2, -(%sp)
    move.l  8(%sp), %d1        /* buf */
    move.l  12(%sp), %d2       /* size */
    move.l  #79, %d0           /* SYS_GETCWD */
    trap    #0
    move.l  (%sp)+, %d2
    bra     __set_errno

/* int rmdir(const char *path) */
    .globl rmdir
rmdir:
    move.l  4(%sp), %d1        /* path */
    move.l  #40, %d0           /* SYS_RMDIR */
    trap    #0
    bra     __set_errno

/* time_t time(time_t *t) — always succeeds */
    .globl time
time:
    move.l  #13, %d0           /* SYS_TIME */
    trap    #0
    /* Store result to *t if t != NULL */
    move.l  4(%sp), %a0
    cmp.l   #0, %a0
    beq     1f
    move.l  %d0, (%a0)
1:  rts
