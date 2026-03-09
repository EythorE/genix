/*
 * Process management — multitasking with vfork/exec/waitpid
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

/* ======== Pipe implementation ======== */

struct pipe pipe_table[MAXPIPE];

/* Pipe flag bits stored in ofile->flags to identify pipe endpoints */
#define OFILE_PIPE_READ   0x1000
#define OFILE_PIPE_WRITE  0x2000
#define OFILE_PIPE_MASK   0x3000

int pipe_read(struct pipe *p, void *buf, int len)
{
    uint8_t *dst = (uint8_t *)buf;
    int n = 0;

    while (n < len) {
        if (p->count == 0) {
            /* Empty pipe: if no writers remain, return EOF */
            if (p->writers == 0)
                return n;
            /* Otherwise return what we have (non-blocking for now) */
            break;
        }
        dst[n++] = p->buf[p->read_pos];
        /* PIPE_SIZE is 512, use & (PIPE_SIZE-1) for power-of-2 wrap */
        p->read_pos = (p->read_pos + 1) & (PIPE_SIZE - 1);
        p->count--;
    }
    return n;
}

int pipe_write(struct pipe *p, const void *buf, int len)
{
    const uint8_t *src = (const uint8_t *)buf;
    int n = 0;

    /* No readers: broken pipe */
    if (p->readers == 0)
        return -EPIPE;

    while (n < len) {
        if (p->count >= PIPE_SIZE) {
            /* Pipe full — return what we wrote (non-blocking for now) */
            break;
        }
        p->buf[p->write_pos] = src[n++];
        /* PIPE_SIZE is 512, power-of-2 wrap */
        p->write_pos = (p->write_pos + 1) & (PIPE_SIZE - 1);
        p->count++;
    }
    return n > 0 ? n : -EAGAIN;
}

void pipe_close_read(struct pipe *p)
{
    if (p->readers > 0)
        p->readers--;
}

void pipe_close_write(struct pipe *p)
{
    if (p->writers > 0)
        p->writers--;
}

int do_pipe(int *fds)
{
    /* Find a free pipe */
    struct pipe *p = NULL;
    int pidx;
    for (pidx = 0; pidx < MAXPIPE; pidx++) {
        if (pipe_table[pidx].readers == 0 && pipe_table[pidx].writers == 0) {
            p = &pipe_table[pidx];
            break;
        }
    }
    if (!p)
        return -ENFILE;

    /* Initialize pipe */
    p->read_pos = 0;
    p->write_pos = 0;
    p->count = 0;
    p->readers = 1;
    p->writers = 1;

    /* Allocate read end */
    struct ofile *of_r = ofile_alloc();
    if (!of_r) {
        p->readers = 0;
        p->writers = 0;
        return -ENFILE;
    }
    of_r->inode = (struct inode *)(uintptr_t)(pidx + 1); /* pipe index, nonzero */
    of_r->offset = 0;
    of_r->flags = O_RDONLY | OFILE_PIPE_READ;

    int rfd = fd_alloc(of_r);
    if (rfd < 0) {
        of_r->refcount = 0;
        p->readers = 0;
        p->writers = 0;
        return rfd;
    }

    /* Allocate write end */
    struct ofile *of_w = ofile_alloc();
    if (!of_w) {
        curproc->fd[rfd] = NULL;
        of_r->refcount = 0;
        p->readers = 0;
        p->writers = 0;
        return -ENFILE;
    }
    of_w->inode = (struct inode *)(uintptr_t)(pidx + 1);
    of_w->offset = 0;
    of_w->flags = O_WRONLY | OFILE_PIPE_WRITE;

    int wfd = fd_alloc(of_w);
    if (wfd < 0) {
        curproc->fd[rfd] = NULL;
        of_r->refcount = 0;
        of_w->refcount = 0;
        p->readers = 0;
        p->writers = 0;
        return wfd;
    }

    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

/* Helper: check if an ofile is a pipe endpoint */
static struct pipe *ofile_pipe(struct ofile *of)
{
    if (!(of->flags & OFILE_PIPE_MASK))
        return NULL;
    int idx = (int)(uintptr_t)of->inode - 1;
    if (idx < 0 || idx >= MAXPIPE)
        return NULL;
    return &pipe_table[idx];
}

/* do_exec() is implemented in exec.c */

void do_exit(int code)
{
    /* If a user program is running via single-tasking exec_enter,
     * return to its caller */
    if (exec_active) {
        exec_exit_code = code;
        exec_leave();
        /* not reached */
    }

    if (!curproc)
        return;

    /* Close all open file descriptors */
    for (int i = 0; i < MAXFD; i++) {
        if (curproc->fd[i]) {
            struct ofile *of = curproc->fd[i];
            curproc->fd[i] = NULL;
            of->refcount--;
            if (of->refcount == 0) {
                if (of->inode)
                    fs_iput(of->inode);
                of->inode = NULL;
            }
        }
    }

    curproc->exitcode = code;
    curproc->state = P_ZOMBIE;

    /* Reparent children to process 0 (init/kernel) */
    for (int i = 0; i < MAXPROC; i++) {
        if (proctab[i].state != P_FREE &&
            proctab[i].ppid == curproc->pid) {
            proctab[i].ppid = 0;
        }
    }

    /* If parent is blocked in vfork, resume it via longjmp.
     * vfork_restore jumps back to the parent's vfork_save call site,
     * making do_vfork() return the child PID. Does not return. */
    if (curproc->ppid < MAXPROC) {
        struct proc *parent = &proctab[curproc->ppid];
        if (parent->state == P_VFORK) {
            uint8_t child_pid = curproc->pid;
            parent->state = P_RUNNING;
            curproc = parent;
            vfork_restore(parent->vfork_ctx, child_pid);
            /* not reached */
        }

        /* Wake parent if it's blocked in waitpid */
        if (parent->state == P_SLEEPING) {
            parent->state = P_READY;
        }
    }

    /* Switch to another process */
    schedule();
    /* If schedule returns (no other runnable process), we're stuck.
     * This shouldn't happen if process 0 (shell) is always runnable. */
}

/*
 * waitpid() — wait for a child process to exit.
 *
 * pid = -1: wait for any child
 * pid > 0:  wait for specific child
 *
 * Returns child PID on success, -ECHILD if no children.
 * For now, this is non-blocking: if no zombie child exists,
 * it busy-waits (cooperative multitasking step).
 */
int do_waitpid(int pid, int *status)
{
    /* Check that we have at least one child */
    int has_child = 0;

    for (;;) {
        has_child = 0;
        for (int i = 0; i < MAXPROC; i++) {
            if (proctab[i].state == P_FREE)
                continue;
            if (proctab[i].ppid != curproc->pid)
                continue;

            has_child = 1;

            /* Match specific PID or any child (-1) */
            if (pid > 0 && proctab[i].pid != (uint8_t)pid)
                continue;

            if (proctab[i].state == P_ZOMBIE) {
                int cpid = proctab[i].pid;
                int code = proctab[i].exitcode;
                if (status)
                    *status = (code & 0xFF) << 8; /* POSIX: exit status in bits 15-8 */
                /* Reap the zombie */
                proctab[i].state = P_FREE;
                nproc--;
                return cpid;
            }
        }

        if (!has_child)
            return -ECHILD;

        /* Child exists but hasn't exited yet — yield and retry.
         * In single-tasking exec mode, the child can't be running
         * concurrently, so we'd deadlock. For now, with vfork+exec
         * the child will have already exited or exec'd before
         * parent reaches here. In true multitasking, schedule(). */
        schedule();
    }
}

/* Next PID to allocate (wraps around, skips 0) */
static uint8_t next_pid = 1;

static uint8_t alloc_pid(void)
{
    /* MAXPROC fits in uint8_t (16), so PIDs 1..MAXPROC-1 */
    for (int i = 0; i < MAXPROC; i++) {
        uint8_t p = next_pid;
        next_pid = (next_pid + 1) & (MAXPROC - 1); /* power-of-2 wrap */
        if (next_pid == 0) next_pid = 1;
        if (proctab[p].state == P_FREE)
            return p;
    }
    return 0xFF; /* no free slot */
}

/*
 * vfork() — create child process sharing parent's address space.
 *
 * Uses vfork_save/vfork_restore (setjmp/longjmp style):
 *   - First call: saves parent context, returns 0 (child path)
 *   - When child calls _exit(): vfork_restore resumes parent,
 *     and do_vfork returns the child PID.
 *
 * The child MUST call exec() or _exit() immediately. It runs on
 * the parent's stack (parent is frozen in P_VFORK state).
 */
int do_vfork(void)
{
    uint8_t cpid = alloc_pid();
    if (cpid == 0xFF)
        return -EAGAIN;

    struct proc *parent = curproc;

    /* Save parent's kernel context (setjmp-style).
     * Returns 0 now (child path).
     * Returns child PID later via vfork_restore (parent path). */
    int saved = vfork_save(parent->vfork_ctx);
    if (saved != 0) {
        /* Resumed by vfork_restore — we are the parent.
         * saved = child PID passed by do_exit/vfork_restore. */
        return saved;
    }

    /* First return (saved == 0): set up child process */
    struct proc *child = &proctab[cpid];

    /* Copy parent's process state to child */
    *child = *parent;
    child->pid = cpid;
    child->ppid = parent->pid;
    child->state = P_RUNNING;
    child->exitcode = 0;

    /* Increment refcounts on open file descriptors */
    for (int i = 0; i < MAXFD; i++) {
        if (child->fd[i])
            child->fd[i]->refcount++;
    }

    nproc++;

    /* Block parent until child calls exec() or _exit() */
    parent->state = P_VFORK;

    /* Switch to child */
    curproc = child;

    return 0;  /* returned to child */
}

/*
 * do_spawn() — combined vfork+exec: create a child, load a binary,
 * run it to completion, and leave the child as a zombie.
 *
 * This avoids the problematic vfork_save/vfork_restore "return twice"
 * pattern, which crashes because the child's exec stack frames
 * overlap with the parent's saved SP.
 *
 * Returns child PID on success (caller uses do_waitpid to reap).
 * Returns negative errno on failure.
 */
int do_spawn(const char *path, const char **argv)
{
    uint8_t cpid = alloc_pid();
    if (cpid == 0xFF)
        return -EAGAIN;

    struct proc *parent = curproc;
    struct proc *child = &proctab[cpid];

    /* Copy parent's process state to child */
    *child = *parent;
    child->pid = cpid;
    child->ppid = parent->pid;
    child->state = P_RUNNING;
    child->exitcode = 0;

    /* Increment refcounts on inherited file descriptors */
    for (int i = 0; i < MAXFD; i++) {
        if (child->fd[i])
            child->fd[i]->refcount++;
    }
    nproc++;

    /* Switch to child context */
    curproc = child;

    /* Load and run the binary. do_exec() calls exec_enter() which
     * blocks until the user program calls _exit() via exec_leave().
     * The return value is the program's exit code (or negative errno). */
    int rc = do_exec(path, argv);

    /* If exec failed (e.g. file not found), clean up the child */
    if (rc < 0) {
        /* Close child's inherited FDs */
        for (int i = 0; i < MAXFD; i++) {
            if (child->fd[i]) {
                struct ofile *of = child->fd[i];
                child->fd[i] = NULL;
                of->refcount--;
                /* Don't release the underlying inode/pipe here —
                 * parent still holds a reference */
            }
        }
        child->state = P_FREE;
        nproc--;
        curproc = parent;
        return rc;
    }

    /* Program ran and exited. Make child a zombie for waitpid. */
    child->exitcode = rc;
    child->state = P_ZOMBIE;

    /* Close child's file descriptors */
    for (int i = 0; i < MAXFD; i++) {
        if (child->fd[i]) {
            struct ofile *of = child->fd[i];
            child->fd[i] = NULL;
            of->refcount--;
            if (of->refcount == 0) {
                struct pipe *p = ofile_pipe(of);
                if (p) {
                    if (of->flags & OFILE_PIPE_READ)
                        pipe_close_read(p);
                    if (of->flags & OFILE_PIPE_WRITE)
                        pipe_close_write(p);
                    of->inode = NULL;
                } else if (of->inode) {
                    fs_iput(of->inode);
                    of->inode = NULL;
                }
            }
        }
    }

    /* Switch back to parent */
    curproc = parent;
    return cpid;
}

/*
 * Round-robin scheduler.
 *
 * Picks the next READY process after the current one. If no other
 * process is runnable, returns immediately (cooperative: the caller
 * keeps running). Called from timer interrupt and blocking syscalls.
 */
void schedule(void)
{
    if (!curproc) return;

    struct proc *next = NULL;
    /* MAXPROC is 16, power of 2 — & (MAXPROC-1) avoids division */
    for (int i = 1; i < MAXPROC; i++) {
        int idx = (curproc->pid + i) & (MAXPROC - 1);
        if (proctab[idx].state == P_READY) {
            next = &proctab[idx];
            break;
        }
    }
    if (!next) return;  /* only one runnable process */

    /* Mark current process as READY (unless it's blocked/zombie) */
    if (curproc->state == P_RUNNING)
        curproc->state = P_READY;

    next->state = P_RUNNING;
    curproc = next;
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
        /* Handle pipe endpoint cleanup */
        struct pipe *p = ofile_pipe(of);
        if (p) {
            if (of->flags & OFILE_PIPE_READ)
                pipe_close_read(p);
            if (of->flags & OFILE_PIPE_WRITE)
                pipe_close_write(p);
            of->inode = NULL;
        } else if (of->inode) {
            fs_iput(of->inode);
            of->inode = NULL;
        }
    }
    return 0;
}

static int sys_read(uint32_t fd, uint32_t buf_addr, uint32_t count)
{
    if (fd >= MAXFD || !curproc->fd[fd])
        return -EBADF;

    struct ofile *of = curproc->fd[fd];
    void *buf = (void *)buf_addr;

    /* Pipe read */
    struct pipe *p = ofile_pipe(of);
    if (p)
        return pipe_read(p, buf, count);

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

    /* Pipe write */
    struct pipe *p = ofile_pipe(of);
    if (p)
        return pipe_write(p, buf, count);

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
    case SYS_IOCTL: {
        if (a1 >= MAXFD || !curproc->fd[a1]) return -EBADF;
        /* Console fds (0-2) without inode are console devices */
        if (!curproc->fd[a1]->inode && a1 < 3)
            return devtab[DEV_CONSOLE].ioctl(0, (int)a2, (void *)a3);
        if (curproc->fd[a1]->inode &&
            curproc->fd[a1]->inode->type == FT_DEV) {
            int major = curproc->fd[a1]->inode->dev_major;
            if (major < NDEV)
                return devtab[major].ioctl(
                    curproc->fd[a1]->inode->dev_minor, (int)a2, (void *)a3);
        }
        return -ENOTTY;
    }
    case SYS_SIGNAL:
        /* Basic signal stub: store handler, return 0
         * a1 = signal number, a2 = handler address */
        (void)a1; (void)a2;
        return 0;
    case SYS_KILL:
        /* Stub: just return 0 */
        (void)a1; (void)a2;
        return 0;
    case SYS_FCNTL:
        /* Stub: return 0 for F_GETFL, etc. */
        return 0;
    case SYS_GETDENTS:
        if (a1 >= MAXFD || !curproc->fd[a1]) return -EBADF;
        if (!curproc->fd[a1]->inode) return -EBADF;
        return fs_getdents(curproc->fd[a1]->inode, (void *)a2,
                          curproc->fd[a1]->offset, a3);
    case SYS_GETCWD:
        /* Simple getcwd stub - return "/" */
        if (a1 && a2 >= 2) {
            ((char *)a1)[0] = '/';
            ((char *)a1)[1] = '\0';
            return (int32_t)a1;
        }
        return -EINVAL;
    case SYS_PIPE:
        return do_pipe((int *)a1);
    case SYS_TIME:
        return pal_timer_ticks();
    default:
        return -ENOSYS;
    }
}
