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

    /* Block only when pipe is empty AND we have no data yet.
     * Once we have any data, return immediately (POSIX semantics). */
    while (p->count == 0 && p->writers > 0) {
        /* Wake any blocked writer before sleeping */
        if (p->write_waiting && p->write_waiting <= MAXPROC) {
            struct proc *wp = &proctab[p->write_waiting - 1];
            if (wp->state == P_SLEEPING)
                wp->state = P_READY;
            p->write_waiting = 0;
        }
        p->read_waiting = curproc->pid + 1;  /* 1-based to distinguish from 0 */
        curproc->state = P_SLEEPING;
        schedule();
        p->read_waiting = 0;
    }

    /* Read available data (up to len) */
    while (n < len && p->count > 0) {
        dst[n++] = p->buf[p->read_pos];
        /* PIPE_SIZE is 512, use & (PIPE_SIZE-1) for power-of-2 wrap */
        p->read_pos = (p->read_pos + 1) & (PIPE_SIZE - 1);
        p->count--;
    }

    /* Wake blocked writer if we freed space */
    if (p->write_waiting && p->write_waiting <= MAXPROC) {
        struct proc *wp = &proctab[p->write_waiting - 1];
        if (wp->state == P_SLEEPING)
            wp->state = P_READY;
        p->write_waiting = 0;
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

    /* Block only when pipe is full AND we haven't written anything yet.
     * Once we write any data, return immediately (POSIX semantics). */
    while (p->count >= PIPE_SIZE && p->readers > 0) {
        /* Wake any blocked reader before sleeping */
        if (p->read_waiting && p->read_waiting <= MAXPROC) {
            struct proc *rp = &proctab[p->read_waiting - 1];
            if (rp->state == P_SLEEPING)
                rp->state = P_READY;
            p->read_waiting = 0;
        }
        p->write_waiting = curproc->pid + 1;
        curproc->state = P_SLEEPING;
        schedule();
        p->write_waiting = 0;
    }

    /* Write available space (up to len) */
    while (n < len && p->count < PIPE_SIZE) {
        if (p->readers == 0)
            return n > 0 ? n : -EPIPE;
        p->buf[p->write_pos] = src[n++];
        /* PIPE_SIZE is 512, power-of-2 wrap */
        p->write_pos = (p->write_pos + 1) & (PIPE_SIZE - 1);
        p->count++;
    }

    /* Wake blocked reader if we added data */
    if (p->read_waiting && p->read_waiting <= MAXPROC) {
        struct proc *rp = &proctab[p->read_waiting - 1];
        if (rp->state == P_SLEEPING)
            rp->state = P_READY;
        p->read_waiting = 0;
    }

    return n > 0 ? n : -EPIPE;
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
    p->read_waiting = 0;
    p->write_waiting = 0;

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

/* do_exec() and load_binary() are implemented in exec.c */

/*
 * Set up a process's kernel stack for its first context switch.
 *
 * Builds two stacked frames so that swtch() + proc_first_run can
 * enter user mode at the given entry point with user_sp as USP.
 *
 * Kstack layout (growing down from kstack top):
 *
 *   [RTE frame]        PC (entry), SR (0x0000 = user mode)
 *   [user regs]        d0-d7, a0-a6 (all zero)
 *   [USP]              user stack pointer
 *   [swtch frame]      return addr → proc_first_run
 *   [callee-saved]     d2-d7, a2-a6 (all zero)
 *   ← ksp points here
 */
void proc_setup_kstack(struct proc *p, uint32_t entry, uint32_t user_sp)
{
    /* Build the initial kstack frame using byte-level pointer math.
     * The exception frame has a 2-byte SR followed by a 4-byte PC,
     * which doesn't align to 4-byte boundaries. We must pack carefully.
     *
     * Total frame size from top of kstack:
     *   RTE:        6 bytes (SR=2 + PC=4)
     *   user regs: 60 bytes (d0-d7, a0-a6 = 15 × 4)
     *   USP:        4 bytes
     *   retaddr:    4 bytes (→ proc_first_run)
     *   swtch:     44 bytes (d2-d7, a2-a6 = 11 × 4)
     *   Total:    118 bytes */
    uint8_t *top = (uint8_t *)&p->kstack[KSTACK_WORDS];
    uint8_t *bp = top;

    /* RTE exception frame (6 bytes): SR then PC */
    bp -= 4;
    *(uint32_t *)bp = entry;        /* PC */
    bp -= 2;
    *(uint16_t *)bp = 0x0000;       /* SR: user mode, interrupts on */

    /* User registers: 15 × 4 = 60 bytes (all zero) */
    bp -= 60;
    memset(bp, 0, 60);

    /* Saved USP */
    bp -= 4;
    *(uint32_t *)bp = user_sp;

    /* --- swtch frame --- */

    /* Return address for swtch's RTS */
    bp -= 4;
    *(uint32_t *)bp = (uint32_t)proc_first_run;

    /* Callee-saved registers: 11 × 4 = 44 bytes (all zero) */
    bp -= 44;
    memset(bp, 0, 44);

    p->ksp = (uint32_t)bp;
}

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
 * Blocks (sleeps) if the child hasn't exited yet. The child's
 * do_exit() wakes us by setting P_SLEEPING → P_READY.
 */
int do_waitpid(int pid, int *status)
{
    for (;;) {
        int has_child = 0;
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

        /* Child exists but hasn't exited yet — sleep until woken.
         * do_exit() sets our state to P_READY when child exits. */
        curproc->state = P_SLEEPING;
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
 * do_spawn() — create a child process and load a binary into it.
 *
 * Asynchronous: loads the binary, sets up the child's kstack for
 * first context switch, marks it P_READY, and returns the child PID.
 * The child runs when the scheduler picks it (via timer preemption
 * or when the parent sleeps in do_waitpid).
 *
 * Only one user process can be in memory at a time (no MMU, shared
 * USER_BASE). The shell (process 0) runs in supervisor mode so it
 * doesn't compete for user memory.
 *
 * Returns child PID on success (caller uses do_waitpid to reap).
 * Returns negative errno on failure.
 */
int do_spawn(const char *path, const char **argv)
{
    uint8_t cpid = alloc_pid();
    if (cpid == 0xFF)
        return -EAGAIN;

    struct proc *child = &proctab[cpid];

    /* Initialize child from parent */
    child->state = P_FREE;  /* not yet ready */
    child->pid = cpid;
    child->ppid = curproc->pid;
    child->exitcode = 0;
    child->cwd = curproc->cwd;
    child->mem_base = 0;
    child->mem_size = 0;
    child->brk = 0;

    /* Copy and refcount file descriptors */
    for (int i = 0; i < MAXFD; i++) {
        child->fd[i] = curproc->fd[i];
        if (child->fd[i])
            child->fd[i]->refcount++;
    }
    nproc++;

    /* Save current curproc — load_binary updates curproc->mem_base etc. */
    struct proc *parent = curproc;
    curproc = child;

    /* Load binary into user memory */
    uint32_t entry, user_sp;
    int rc = load_binary(path, argv, &entry, &user_sp);

    /* Restore curproc to parent */
    curproc = parent;

    if (rc < 0) {
        /* Clean up child's inherited FDs */
        for (int i = 0; i < MAXFD; i++) {
            if (child->fd[i]) {
                child->fd[i]->refcount--;
                child->fd[i] = NULL;
            }
        }
        child->state = P_FREE;
        nproc--;
        return rc;
    }

    /* Build the child's kstack so swtch() can resume it */
    proc_setup_kstack(child, entry, user_sp);

    /* Child is ready to run — scheduler will pick it up */
    child->state = P_READY;
    return cpid;
}

/*
 * Round-robin scheduler with context switch.
 *
 * Picks the next READY process after the current one. If another
 * runnable process exists, performs a context switch via swtch().
 *
 * Called from:
 *   - Timer ISR (after saving user state on kstack)
 *   - Blocking syscalls (waitpid, pipe read/write)
 *   - do_exit (after marking process as zombie)
 *
 * swtch() saves callee-saved registers and SP, loads the new
 * process's SP, restores its callee-saved registers, and returns
 * to wherever the new process was (either kernel code or
 * proc_first_run for a brand-new process).
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

    struct proc *old = curproc;
    next->state = P_RUNNING;
    curproc = next;

    swtch(&old->ksp, next->ksp);
    /* Returns here when this process is scheduled again */
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

/* ======== Signals ======== */

/*
 * Deliver pending signals to the current process.
 *
 * Called on return from syscall (end of syscall_dispatch).
 * For now, only default actions are supported:
 *   - SIG_DFL: terminate process (most signals), or ignore (SIGCHLD, SIGCONT)
 *   - SIG_IGN: discard the signal
 *   - User handlers: not yet implemented (requires signal frame on user stack)
 *
 * SIGKILL and SIGSTOP cannot be caught or ignored.
 */
void sig_deliver(void)
{
    if (!curproc || !curproc->sig_pending)
        return;

    for (int sig = 1; sig < NSIG; sig++) {
        if (!(curproc->sig_pending & (1u << sig)))
            continue;
        curproc->sig_pending &= ~(1u << sig);

        uint32_t handler = curproc->sig_handler[sig];

        /* SIGKILL/SIGSTOP: always take default action */
        if (sig == SIGKILL) {
            do_exit(128 + sig);
            return;  /* not reached */
        }
        if (sig == SIGSTOP) {
            /* TODO: stop/continue support */
            continue;
        }

        if (handler == SIG_IGN)
            continue;

        if (handler == SIG_DFL) {
            /* Default actions */
            switch (sig) {
            case SIGCHLD:
            case SIGCONT:
                /* Default is ignore */
                break;
            default:
                /* Default is terminate */
                do_exit(128 + sig);
                return;  /* not reached */
            }
        } else {
            /* User handler: not yet implemented.
             * For now, treat like SIG_DFL to avoid ignoring signals.
             * TODO: build signal frame on user stack + sigreturn trampoline */
            switch (sig) {
            case SIGCHLD:
            case SIGCONT:
                break;
            default:
                do_exit(128 + sig);
                return;
            }
        }
    }
}

/* ======== getcwd ======== */

/*
 * Walk from the cwd inode to root, building the absolute path.
 *
 * Algorithm: starting at curproc->cwd, look up ".." to find parent,
 * then scan parent's directory for the entry pointing to current inode.
 * Repeat until we reach root (inode 1 or ".." points to self).
 */
static int sys_getcwd(char *buf, uint32_t size)
{
    if (!buf || size < 2)
        return -EINVAL;

    uint16_t cur = curproc ? curproc->cwd : 1;

    /* Root directory: just return "/" */
    if (cur == 1) {
        buf[0] = '/';
        buf[1] = '\0';
        return (int32_t)(uint32_t)buf;
    }

    /* Build path components in reverse.
     * Use a stack of names (limited depth to avoid deep recursion). */
    char names[8][NAME_MAX + 2];  /* max depth 8 */
    int depth = 0;

    uint16_t child_inum = cur;
    while (child_inum != 1 && depth < 8) {
        /* Open parent directory */
        struct inode *child_ip = fs_iget(child_inum);
        if (!child_ip)
            break;

        /* Read ".." entry to find parent inode */
        struct dirent_disk de;
        uint16_t parent_inum = 1;  /* fallback to root */
        uint32_t off = 0;
        while (off < child_ip->size) {
            if (fs_read(child_ip, &de, off, sizeof(de)) != sizeof(de))
                break;
            off += sizeof(de);
            if (de.inode && strcmp(de.name, "..") == 0) {
                parent_inum = de.inode;
                break;
            }
        }
        fs_iput(child_ip);

        if (parent_inum == child_inum)
            break;  /* root: ".." points to self */

        /* Scan parent for entry pointing to child_inum */
        struct inode *parent_ip = fs_iget(parent_inum);
        if (!parent_ip)
            break;

        off = 0;
        while (off < parent_ip->size) {
            if (fs_read(parent_ip, &de, off, sizeof(de)) != sizeof(de))
                break;
            off += sizeof(de);
            if (de.inode == child_inum &&
                strcmp(de.name, ".") != 0 &&
                strcmp(de.name, "..") != 0) {
                strncpy(names[depth], de.name, NAME_MAX);
                names[depth][NAME_MAX] = '\0';
                depth++;
                break;
            }
        }
        fs_iput(parent_ip);

        child_inum = parent_inum;
    }

    /* Build path from components (reverse order) */
    uint32_t pos = 0;
    if (depth == 0) {
        buf[0] = '/';
        buf[1] = '\0';
        return (int32_t)(uint32_t)buf;
    }
    for (int i = depth - 1; i >= 0; i--) {
        if (pos + 1 >= size)
            return -ERANGE;
        buf[pos++] = '/';
        uint32_t nlen = strlen(names[i]);
        if (pos + nlen >= size)
            return -ERANGE;
        memcpy(buf + pos, names[i], nlen);
        pos += nlen;
    }
    buf[pos] = '\0';
    return (int32_t)(uint32_t)buf;
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
    case SYS_SIGNAL: {
        int signum = (int)a1;
        uint32_t handler = a2;
        if (signum < 1 || signum >= NSIG)
            return -EINVAL;
        /* SIGKILL and SIGSTOP cannot be caught or ignored */
        if (signum == SIGKILL || signum == SIGSTOP)
            return -EINVAL;
        uint32_t old = curproc->sig_handler[signum];
        curproc->sig_handler[signum] = handler;
        return (int32_t)old;
    }
    case SYS_KILL: {
        int pid = (int)a1;
        int sig = (int)a2;
        if (sig < 0 || sig >= NSIG)
            return -EINVAL;
        if (sig == 0)
            return 0;  /* sig 0: check if process exists */
        /* Find target process */
        struct proc *target = NULL;
        for (int i = 0; i < MAXPROC; i++) {
            if (proctab[i].state != P_FREE &&
                proctab[i].pid == (uint8_t)pid) {
                target = &proctab[i];
                break;
            }
        }
        if (!target)
            return -ESRCH;
        target->sig_pending |= (1u << sig);
        /* Wake sleeping processes so they can handle the signal */
        if (target->state == P_SLEEPING)
            target->state = P_READY;
        return 0;
    }
    case SYS_FCNTL:
        /* Stub: return 0 for F_GETFL, etc. */
        return 0;
    case SYS_GETDENTS: {
        if (a1 >= MAXFD || !curproc->fd[a1]) return -EBADF;
        if (!curproc->fd[a1]->inode) return -EBADF;
        struct ofile *gof = curproc->fd[a1];
        int gn = fs_getdents(gof->inode, (void *)a2, gof->offset, a3);
        if (gn > 0)
            gof->offset += gn;
        return gn;
    }
    case SYS_GETCWD:
        return sys_getcwd((char *)a1, a2);
    case SYS_PIPE:
        return do_pipe((int *)a1);
    case SYS_TIME:
        return pal_timer_ticks();
    default:
        return -ENOSYS;
    }

    /* Not reached — all cases return above. If we add fall-through
     * cases in the future, sig_deliver() here will catch pending signals
     * before returning to user mode. */
}
