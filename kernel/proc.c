/*
 * Process management — single-tasking for now
 */
#include "kernel.h"

/* System-wide open file table */
static struct ofile ofile_table[MAXOPEN];

void proc_init(void)
{
    for (int i = 0; i < MAXPROC; i++) {
        proctab[i].state = P_FREE;
        proctab[i].pid = i;
    }
    for (int i = 0; i < MAXOPEN; i++) {
        ofile_table[i].refcount = 0;
    }

    /* Process 0: kernel/shell */
    curproc = &proctab[0];
    curproc->state = P_RUNNING;
    curproc->pid = 0;
    curproc->ppid = 0;
    curproc->cwd = 1;  /* root directory */
    nproc = 1;

    /* Set up stdin/stdout/stderr to console */
    for (int fd = 0; fd < 3; fd++) {
        struct ofile *of = NULL;
        for (int i = 0; i < MAXOPEN; i++) {
            if (ofile_table[i].refcount == 0) {
                of = &ofile_table[i];
                break;
            }
        }
        if (of) {
            of->inode = NULL;  /* device files don't need an inode */
            of->offset = 0;
            of->flags = (fd == 0) ? O_RDONLY : O_WRONLY;
            of->refcount = 1;
            curproc->fd[fd] = of;
        }
    }

    kputs("[proc] Process table initialized.\n");
}

/* Allocate an open file entry */
static struct ofile *ofile_alloc(void)
{
    for (int i = 0; i < MAXOPEN; i++) {
        if (ofile_table[i].refcount == 0) {
            ofile_table[i].refcount = 1;
            return &ofile_table[i];
        }
    }
    return NULL;
}

/* Allocate a file descriptor in current process */
static int fd_alloc(struct ofile *of)
{
    for (int i = 0; i < MAXFD; i++) {
        if (curproc->fd[i] == NULL) {
            curproc->fd[i] = of;
            return i;
        }
    }
    return -EMFILE;
}

/* Single-tasking exec: just run the program and return */
int do_exec(const char *path, const char **argv)
{
    (void)argv;
    struct inode *ip = fs_namei(path);
    if (!ip)
        return -ENOENT;
    if (ip->type != FT_FILE) {
        fs_iput(ip);
        return -ENOEXEC;
    }
    /* For now, we can't actually load and run ELF binaries in single-tasking mode.
     * This will be implemented when we have the binary loader. */
    fs_iput(ip);
    return -ENOSYS;
}

void do_exit(int code)
{
    if (curproc) {
        curproc->exitcode = code;
        curproc->state = P_ZOMBIE;
    }
    /* In single-tasking, return to shell */
}

int do_waitpid(int pid, int *status)
{
    (void)pid; (void)status;
    return -ECHILD;
}

int do_vfork(void)
{
    return -ENOSYS;
}

/* ======== Syscall implementations ======== */

static int sys_open(uint32_t path_addr, uint32_t flags)
{
    const char *path = (const char *)path_addr;
    struct inode *ip = fs_namei(path);

    if (!ip) {
        if (flags & O_CREAT) {
            ip = fs_create(path, FT_FILE);
            if (!ip) return -EIO;
        } else {
            return -ENOENT;
        }
    }

    if (flags & O_TRUNC && ip->type == FT_FILE) {
        ip->size = 0;
        ip->dirty = 1;
        fs_iupdate(ip);
    }

    struct ofile *of = ofile_alloc();
    if (!of) {
        fs_iput(ip);
        return -ENFILE;
    }
    of->inode = ip;
    of->offset = 0;
    of->flags = flags;

    int fd = fd_alloc(of);
    if (fd < 0) {
        of->refcount = 0;
        fs_iput(ip);
        return fd;
    }

    return fd;
}

static int sys_close(uint32_t fd)
{
    if (fd >= MAXFD || !curproc->fd[fd])
        return -EBADF;

    struct ofile *of = curproc->fd[fd];
    curproc->fd[fd] = NULL;
    of->refcount--;
    if (of->refcount == 0) {
        if (of->inode)
            fs_iput(of->inode);
        of->inode = NULL;
    }
    return 0;
}

static int sys_read(uint32_t fd, uint32_t buf_addr, uint32_t count)
{
    if (fd >= MAXFD || !curproc->fd[fd])
        return -EBADF;

    struct ofile *of = curproc->fd[fd];
    void *buf = (void *)buf_addr;

    /* Console (stdin) */
    if (!of->inode && fd < 3) {
        return devtab[DEV_CONSOLE].read(0, buf, count);
    }

    if (!of->inode)
        return -EBADF;

    /* Device file */
    if (of->inode->type == FT_DEV) {
        int major = of->inode->dev_major;
        if (major < NDEV)
            return devtab[major].read(of->inode->dev_minor, buf, count);
        return -ENODEV;
    }

    int n = fs_read(of->inode, buf, of->offset, count);
    if (n > 0)
        of->offset += n;
    return n;
}

static int sys_write(uint32_t fd, uint32_t buf_addr, uint32_t count)
{
    if (fd >= MAXFD || !curproc->fd[fd])
        return -EBADF;

    struct ofile *of = curproc->fd[fd];
    const void *buf = (const void *)buf_addr;

    /* Console (stdout/stderr) */
    if (!of->inode && fd < 3) {
        return devtab[DEV_CONSOLE].write(0, buf, count);
    }

    if (!of->inode)
        return -EBADF;

    if (of->inode->type == FT_DEV) {
        int major = of->inode->dev_major;
        if (major < NDEV)
            return devtab[major].write(of->inode->dev_minor, buf, count);
        return -ENODEV;
    }

    if (of->flags & O_APPEND)
        of->offset = of->inode->size;

    int n = fs_write(of->inode, buf, of->offset, count);
    if (n > 0)
        of->offset += n;
    return n;
}

static int sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence)
{
    if (fd >= MAXFD || !curproc->fd[fd])
        return -EBADF;

    struct ofile *of = curproc->fd[fd];
    int32_t new_off;

    switch (whence) {
    case SEEK_SET:
        new_off = offset;
        break;
    case SEEK_CUR:
        new_off = of->offset + (int32_t)offset;
        break;
    case SEEK_END:
        new_off = (of->inode ? of->inode->size : 0) + (int32_t)offset;
        break;
    default:
        return -EINVAL;
    }
    if (new_off < 0) return -EINVAL;
    of->offset = new_off;
    return new_off;
}

static int sys_dup(uint32_t fd)
{
    if (fd >= MAXFD || !curproc->fd[fd])
        return -EBADF;
    struct ofile *of = curproc->fd[fd];
    of->refcount++;
    return fd_alloc(of);
}

static int sys_dup2(uint32_t oldfd, uint32_t newfd)
{
    if (oldfd >= MAXFD || !curproc->fd[oldfd])
        return -EBADF;
    if (newfd >= MAXFD)
        return -EBADF;
    if (curproc->fd[newfd])
        sys_close(newfd);
    struct ofile *of = curproc->fd[oldfd];
    of->refcount++;
    curproc->fd[newfd] = of;
    return newfd;
}

/* ======== Syscall dispatch ======== */

int32_t syscall_dispatch(uint32_t num, uint32_t a1, uint32_t a2,
                         uint32_t a3, uint32_t a4)
{
    (void)a4;

    switch (num) {
    case SYS_EXIT:
        do_exit(a1);
        return 0;
    case SYS_READ:
        return sys_read(a1, a2, a3);
    case SYS_WRITE:
        return sys_write(a1, a2, a3);
    case SYS_OPEN:
        return sys_open(a1, a2);
    case SYS_CLOSE:
        return sys_close(a1);
    case SYS_LSEEK:
        return sys_lseek(a1, a2, a3);
    case SYS_GETPID:
        return curproc ? curproc->pid : 0;
    case SYS_BRK:
    case SYS_SBRK:
        return (int32_t)(uint32_t)sbrk_proc((int32_t)a1);
    case SYS_EXEC:
        return do_exec((const char *)a1, (const char **)a2);
    case SYS_WAITPID:
        return do_waitpid(a1, (int *)a2);
    case SYS_VFORK:
        return do_vfork();
    case SYS_DUP:
        return sys_dup(a1);
    case SYS_DUP2:
        return sys_dup2(a1, a2);
    case SYS_CHDIR: {
        struct inode *ip = fs_namei((const char *)a1);
        if (!ip) return -ENOENT;
        if (ip->type != FT_DIR) { fs_iput(ip); return -ENOTDIR; }
        if (curproc) curproc->cwd = ip->inum;
        fs_iput(ip);
        return 0;
    }
    case SYS_MKDIR:
        return fs_mkdir((const char *)a1);
    case SYS_RMDIR:
        return fs_rmdir((const char *)a1);
    case SYS_UNLINK:
        return fs_unlink((const char *)a1);
    case SYS_RENAME:
        return fs_rename((const char *)a1, (const char *)a2);
    case SYS_STAT: {
        struct inode *ip = fs_namei((const char *)a1);
        if (!ip) return -ENOENT;
        int r = fs_stat(ip, (void *)a2);
        fs_iput(ip);
        return r;
    }
    case SYS_FSTAT: {
        if (a1 >= MAXFD || !curproc->fd[a1]) return -EBADF;
        struct ofile *of = curproc->fd[a1];
        if (!of->inode) return -EBADF;
        return fs_stat(of->inode, (void *)a2);
    }
    case SYS_TIME:
        return pal_timer_ticks();
    default:
        return -ENOSYS;
    }
}
