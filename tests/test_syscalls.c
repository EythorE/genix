/*
 * Unit tests for kernel syscall-related logic — fd management, fcntl, limits.
 *
 * Tests host-testable edges in proc.c syscall implementations:
 * - F_GETFD / F_SETFD round-trip (fd_flags behavior)
 * - F_GETFL mask (strips internal OFILE_PIPE_* flags)
 * - MAXFD limit (opening more than MAXFD files returns -EMFILE)
 * - FD_CLOEXEC propagation (fd_flags copied in do_spawn)
 * - dup/dup2 fd_flags behavior
 * - F_DUPFD (fcntl dup with minimum fd)
 * - close clears fd_flags
 * - EBADF on invalid fds
 *
 * Strategy: re-implement the core fd management and fcntl logic from
 * proc.c on the host, mirroring the kernel's data structures and
 * constants. This avoids needing the real kernel or filesystem.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* ---- Kernel constants (from kernel.h) ---- */
#define MAXFD       16
#define MAXOPEN     64
#define MAXPROC     16

/* Error numbers */
#define EBADF       9
#define EMFILE      24
#define ENFILE      23
#define EINVAL      22

/* Open flags */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0040
#define O_APPEND   0x0400

/* FD_CLOEXEC */
#define FD_CLOEXEC  1

/* Pipe flag bits stored in ofile->flags (internal, not user-visible) */
#define OFILE_PIPE_READ   0x1000
#define OFILE_PIPE_WRITE  0x2000
#define OFILE_PIPE_MASK   0x3000

/* ---- Minimal kernel structures ---- */

struct inode {
    uint16_t inum;
    uint8_t  type;
    uint8_t  refcount;
};

struct ofile {
    struct inode *inode;
    uint32_t offset;
    uint16_t flags;
    uint8_t  refcount;
};

struct proc {
    uint8_t  pid;
    struct ofile *fd[MAXFD];
    uint8_t  fd_flags[MAXFD];    /* per-fd flags (FD_CLOEXEC) */
};

/* ---- Global state (mirrors kernel globals) ---- */

static struct ofile ofile_table[MAXOPEN];
static struct proc proctab[MAXPROC];
static struct proc *curproc;

/* ---- Re-implement kernel fd/syscall functions ---- */

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

static int fd_alloc_from(struct ofile *of, int minfd)
{
    for (int i = minfd; i < MAXFD; i++) {
        if (curproc->fd[i] == NULL) {
            curproc->fd[i] = of;
            return i;
        }
    }
    return -EMFILE;
}

static int fd_alloc(struct ofile *of)
{
    return fd_alloc_from(of, 0);
}

/* Simplified sys_open: create a fake ofile and assign an fd */
static int sys_open_fake(uint16_t flags)
{
    struct ofile *of = ofile_alloc();
    if (!of)
        return -ENFILE;
    of->inode = NULL;  /* no real inode needed for these tests */
    of->offset = 0;
    of->flags = flags;

    int fd = fd_alloc(of);
    if (fd < 0) {
        of->refcount = 0;
        return fd;
    }
    curproc->fd_flags[fd] = 0;  /* new fd starts with no flags */
    return fd;
}

/* Simplified sys_close (no inode/pipe cleanup needed) */
static int sys_close(uint32_t fd)
{
    if (fd >= MAXFD || !curproc->fd[fd])
        return -EBADF;

    struct ofile *of = curproc->fd[fd];
    curproc->fd[fd] = NULL;
    curproc->fd_flags[fd] = 0;
    of->refcount--;
    return 0;
}

/* sys_dup */
static int sys_dup(uint32_t fd)
{
    if (fd >= MAXFD || !curproc->fd[fd])
        return -EBADF;
    struct ofile *of = curproc->fd[fd];
    of->refcount++;
    int newfd = fd_alloc(of);
    if (newfd < 0)
        of->refcount--;
    return newfd;
}

/* sys_dup2 */
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

/* fcntl dispatch (mirrors kernel SYS_FCNTL handling) */
static int sys_fcntl(uint32_t fd, uint32_t cmd, uint32_t arg)
{
    if (fd >= MAXFD || !curproc->fd[fd]) return -EBADF;
    struct ofile *fcntl_of = curproc->fd[fd];
    int fcntl_cmd = (int)cmd;
    switch (fcntl_cmd) {
    case 0: { /* F_DUPFD: dup to lowest fd >= arg */
        if (arg >= MAXFD) return -EINVAL;
        fcntl_of->refcount++;
        int fcntl_fd = fd_alloc_from(fcntl_of, arg);
        if (fcntl_fd < 0)
            fcntl_of->refcount--;
        return fcntl_fd;
    }
    case 1: /* F_GETFD */
        return curproc->fd_flags[fd];
    case 2: /* F_SETFD */
        curproc->fd_flags[fd] = (uint8_t)(arg & 1);
        return 0;
    case 3: /* F_GETFL: return open flags */
        return fcntl_of->flags & 0x0FFF; /* mask out internal pipe bits */
    case 4: /* F_SETFL: stub */
        return 0;
    default:
        return -EINVAL;
    }
}

/* ---- Test helpers ---- */

/* Reset all state to clean */
static void reset_state(void)
{
    memset(ofile_table, 0, sizeof(ofile_table));
    memset(proctab, 0, sizeof(proctab));
    curproc = &proctab[0];
    curproc->pid = 0;
    for (int i = 0; i < MAXFD; i++) {
        curproc->fd[i] = NULL;
        curproc->fd_flags[i] = 0;
    }
}

/* ================================================================
 * Tests
 * ================================================================ */

/* F_GETFD / F_SETFD round-trip */
static void test_fd_flags_roundtrip(void)
{
    reset_state();
    int fd = sys_open_fake(O_RDWR);
    ASSERT(fd >= 0);

    /* Default: no flags */
    int val = sys_fcntl(fd, 1, 0); /* F_GETFD */
    ASSERT_EQ(val, 0);

    /* Set FD_CLOEXEC */
    int rc = sys_fcntl(fd, 2, FD_CLOEXEC); /* F_SETFD */
    ASSERT_EQ(rc, 0);

    /* Read back */
    val = sys_fcntl(fd, 1, 0); /* F_GETFD */
    ASSERT_EQ(val, FD_CLOEXEC);

    /* Clear it */
    rc = sys_fcntl(fd, 2, 0); /* F_SETFD */
    ASSERT_EQ(rc, 0);
    val = sys_fcntl(fd, 1, 0); /* F_GETFD */
    ASSERT_EQ(val, 0);

    sys_close(fd);
}

/* F_SETFD masks to only bit 0 */
static void test_fd_flags_mask(void)
{
    reset_state();
    int fd = sys_open_fake(O_RDONLY);
    ASSERT(fd >= 0);

    /* Set with extra bits — kernel should mask to bit 0 only */
    sys_fcntl(fd, 2, 0xFF); /* F_SETFD with 0xFF */
    int val = sys_fcntl(fd, 1, 0); /* F_GETFD */
    ASSERT_EQ(val, 1); /* only bit 0 should survive */

    /* Set with 0xFE (bit 0 clear) */
    sys_fcntl(fd, 2, 0xFE); /* F_SETFD */
    val = sys_fcntl(fd, 1, 0); /* F_GETFD */
    ASSERT_EQ(val, 0); /* bit 0 is clear */

    sys_close(fd);
}

/* F_GETFL masks out internal pipe flags */
static void test_getfl_masks_pipe_bits(void)
{
    reset_state();

    /* Simulate a pipe read endpoint: flags include OFILE_PIPE_READ */
    struct ofile *of = ofile_alloc();
    ASSERT_NOT_NULL(of);
    of->flags = O_RDONLY | OFILE_PIPE_READ;
    of->inode = NULL;
    int fd = fd_alloc(of);
    ASSERT(fd >= 0);

    /* F_GETFL should return only the user-visible flags */
    int fl = sys_fcntl(fd, 3, 0); /* F_GETFL */
    ASSERT_EQ(fl, O_RDONLY);
    ASSERT_EQ(fl & OFILE_PIPE_MASK, 0); /* pipe bits stripped */

    sys_close(fd);
}

/* F_GETFL masks pipe write bits too */
static void test_getfl_masks_pipe_write(void)
{
    reset_state();

    struct ofile *of = ofile_alloc();
    ASSERT_NOT_NULL(of);
    of->flags = O_WRONLY | OFILE_PIPE_WRITE;
    of->inode = NULL;
    int fd = fd_alloc(of);
    ASSERT(fd >= 0);

    int fl = sys_fcntl(fd, 3, 0); /* F_GETFL */
    ASSERT_EQ(fl, O_WRONLY);
    ASSERT_EQ(fl & OFILE_PIPE_MASK, 0);

    sys_close(fd);
}

/* F_GETFL returns correct flags for normal files */
static void test_getfl_normal_file(void)
{
    reset_state();
    int fd = sys_open_fake(O_RDWR | O_APPEND);
    ASSERT(fd >= 0);

    int fl = sys_fcntl(fd, 3, 0); /* F_GETFL */
    ASSERT_EQ(fl, (int)(O_RDWR | O_APPEND));

    sys_close(fd);
}

/* MAXFD limit: opening more than MAXFD files returns -EMFILE */
static void test_maxfd_limit(void)
{
    reset_state();

    /* Open MAXFD files */
    int fds[MAXFD];
    for (int i = 0; i < MAXFD; i++) {
        fds[i] = sys_open_fake(O_RDONLY);
        ASSERT(fds[i] >= 0);
    }

    /* Next open should fail with -EMFILE */
    int extra = sys_open_fake(O_RDONLY);
    ASSERT_EQ(extra, -EMFILE);

    /* Close one and retry — should succeed */
    sys_close(fds[0]);
    extra = sys_open_fake(O_RDONLY);
    ASSERT(extra >= 0);
    ASSERT_EQ(extra, 0); /* should get fd 0 (lowest) */

    /* Clean up */
    for (int i = 1; i < MAXFD; i++)
        sys_close(fds[i]);
    sys_close(extra);
}

/* FD_CLOEXEC propagation: fd_flags copied to child in do_spawn */
static void test_cloexec_propagation(void)
{
    reset_state();

    /* Open some fds, set FD_CLOEXEC on some */
    int fd0 = sys_open_fake(O_RDONLY);
    int fd1 = sys_open_fake(O_WRONLY);
    int fd2 = sys_open_fake(O_RDWR);
    ASSERT(fd0 >= 0);
    ASSERT(fd1 >= 0);
    ASSERT(fd2 >= 0);

    sys_fcntl(fd1, 2, FD_CLOEXEC); /* F_SETFD on fd1 */

    /* Simulate do_spawn's fd copy to child (from proc.c:641-646) */
    struct proc *child = &proctab[1];
    child->pid = 1;
    for (int i = 0; i < MAXFD; i++) {
        child->fd[i] = curproc->fd[i];
        child->fd_flags[i] = curproc->fd_flags[i];
        if (child->fd[i])
            child->fd[i]->refcount++;
    }

    /* Verify child inherits fd_flags */
    ASSERT_EQ(child->fd_flags[fd0], 0);
    ASSERT_EQ(child->fd_flags[fd1], FD_CLOEXEC);
    ASSERT_EQ(child->fd_flags[fd2], 0);

    /* Simulate exec's FD_CLOEXEC closing (from exec.c:621-637) */
    struct proc *saved = curproc;
    curproc = child;
    for (int i = 0; i < MAXFD; i++) {
        if (curproc->fd_flags[i] & 1) {
            if (curproc->fd[i]) {
                struct ofile *of = curproc->fd[i];
                curproc->fd[i] = NULL;
                curproc->fd_flags[i] = 0;
                of->refcount--;
            }
        }
    }

    /* fd1 should be closed in child, others still open */
    ASSERT_NOT_NULL(child->fd[fd0]);
    ASSERT_NULL(child->fd[fd1]);
    ASSERT_NOT_NULL(child->fd[fd2]);
    ASSERT_EQ(child->fd_flags[fd1], 0);

    /* Parent still has all fds open */
    curproc = saved;
    ASSERT_NOT_NULL(curproc->fd[fd0]);
    ASSERT_NOT_NULL(curproc->fd[fd1]);
    ASSERT_NOT_NULL(curproc->fd[fd2]);

    /* Clean up */
    for (int i = 0; i < MAXFD; i++) {
        if (child->fd[i]) {
            child->fd[i]->refcount--;
            child->fd[i] = NULL;
        }
    }
    sys_close(fd0);
    sys_close(fd1);
    sys_close(fd2);
}

/* dup does NOT inherit FD_CLOEXEC */
static void test_dup_clears_cloexec(void)
{
    reset_state();
    int fd = sys_open_fake(O_RDWR);
    ASSERT(fd >= 0);

    sys_fcntl(fd, 2, FD_CLOEXEC); /* F_SETFD */
    ASSERT_EQ(sys_fcntl(fd, 1, 0), FD_CLOEXEC);

    int fd2 = sys_dup(fd);
    ASSERT(fd2 >= 0);
    ASSERT(fd2 != fd);

    /* POSIX: dup'd fd should NOT have FD_CLOEXEC (fd_flags defaults to 0) */
    ASSERT_EQ(curproc->fd_flags[fd2], 0);

    /* Original still has it */
    ASSERT_EQ(curproc->fd_flags[fd], FD_CLOEXEC);

    sys_close(fd);
    sys_close(fd2);
}

/* dup2 clears fd_flags on the target fd */
static void test_dup2_clears_cloexec(void)
{
    reset_state();

    int fd0 = sys_open_fake(O_RDONLY);
    int fd1 = sys_open_fake(O_WRONLY);
    ASSERT(fd0 >= 0);
    ASSERT(fd1 >= 0);

    /* Set FD_CLOEXEC on fd1, then dup2 fd0 onto fd1 */
    sys_fcntl(fd1, 2, FD_CLOEXEC);
    ASSERT_EQ(sys_fcntl(fd1, 1, 0), FD_CLOEXEC);

    int rc = sys_dup2(fd0, fd1);
    ASSERT_EQ(rc, fd1);

    /* dup2 closes the old fd1 (which clears fd_flags), and the
     * new fd1 should have fd_flags = 0 (POSIX: dup2 clears FD_CLOEXEC) */
    ASSERT_EQ(curproc->fd_flags[fd1], 0);

    sys_close(fd0);
    sys_close(fd1);
}

/* F_DUPFD: dup to lowest fd >= specified minimum */
static void test_f_dupfd(void)
{
    reset_state();

    int fd0 = sys_open_fake(O_RDONLY);
    int fd1 = sys_open_fake(O_RDONLY);
    ASSERT_EQ(fd0, 0);
    ASSERT_EQ(fd1, 1);

    /* F_DUPFD with min=5 should give fd 5 */
    int fd5 = sys_fcntl(fd0, 0, 5); /* F_DUPFD */
    ASSERT_EQ(fd5, 5);

    /* The ofile should be shared (same pointer) */
    ASSERT(curproc->fd[fd5] == curproc->fd[fd0]);

    /* F_DUPFD with min=5 again should give fd 6 (5 is taken) */
    int fd6 = sys_fcntl(fd0, 0, 5);
    ASSERT_EQ(fd6, 6);

    sys_close(fd0);
    sys_close(fd1);
    sys_close(fd5);
    sys_close(fd6);
}

/* F_DUPFD with invalid minimum */
static void test_f_dupfd_invalid_min(void)
{
    reset_state();
    int fd = sys_open_fake(O_RDONLY);
    ASSERT(fd >= 0);

    /* min >= MAXFD should return -EINVAL */
    int rc = sys_fcntl(fd, 0, MAXFD); /* F_DUPFD with min=MAXFD */
    ASSERT_EQ(rc, -EINVAL);

    sys_close(fd);
}

/* F_DUPFD when all fds above min are taken should return -EMFILE */
static void test_f_dupfd_full(void)
{
    reset_state();

    /* Fill all MAXFD slots */
    for (int i = 0; i < MAXFD; i++) {
        int fd = sys_open_fake(O_RDONLY);
        ASSERT_EQ(fd, i);
    }

    /* F_DUPFD should fail — no fds available */
    int rc = sys_fcntl(0, 0, 0); /* F_DUPFD from min=0 */
    ASSERT_EQ(rc, -EMFILE);

    /* Clean up */
    for (int i = 0; i < MAXFD; i++)
        sys_close(i);
}

/* close clears fd_flags */
static void test_close_clears_fd_flags(void)
{
    reset_state();
    int fd = sys_open_fake(O_RDWR);
    ASSERT(fd >= 0);

    sys_fcntl(fd, 2, FD_CLOEXEC);
    ASSERT_EQ(curproc->fd_flags[fd], FD_CLOEXEC);

    sys_close(fd);
    ASSERT_EQ(curproc->fd_flags[fd], 0);
    ASSERT_NULL(curproc->fd[fd]);
}

/* EBADF on invalid fd numbers */
static void test_ebadf(void)
{
    reset_state();

    /* Close on unopened fd */
    ASSERT_EQ(sys_close(0), -EBADF);
    ASSERT_EQ(sys_close(MAXFD), -EBADF);
    ASSERT_EQ(sys_close(999), -EBADF);

    /* fcntl on unopened fd */
    ASSERT_EQ(sys_fcntl(0, 1, 0), -EBADF);  /* F_GETFD */
    ASSERT_EQ(sys_fcntl(0, 3, 0), -EBADF);  /* F_GETFL */
    ASSERT_EQ(sys_fcntl(MAXFD, 1, 0), -EBADF);

    /* dup on unopened fd */
    ASSERT_EQ(sys_dup(0), -EBADF);
    ASSERT_EQ(sys_dup(MAXFD), -EBADF);

    /* dup2 on unopened fd */
    ASSERT_EQ(sys_dup2(0, 1), -EBADF);
    ASSERT_EQ(sys_dup2(MAXFD, 0), -EBADF);
}

/* fcntl with invalid command returns -EINVAL */
static void test_fcntl_invalid_cmd(void)
{
    reset_state();
    int fd = sys_open_fake(O_RDONLY);
    ASSERT(fd >= 0);

    ASSERT_EQ(sys_fcntl(fd, 99, 0), -EINVAL);
    ASSERT_EQ(sys_fcntl(fd, 5, 0), -EINVAL);
    ASSERT_EQ(sys_fcntl(fd, -1, 0), -EINVAL);

    sys_close(fd);
}

/* fd_alloc always returns lowest available fd */
static void test_fd_alloc_lowest(void)
{
    reset_state();

    int fd0 = sys_open_fake(O_RDONLY);
    int fd1 = sys_open_fake(O_RDONLY);
    int fd2 = sys_open_fake(O_RDONLY);
    ASSERT_EQ(fd0, 0);
    ASSERT_EQ(fd1, 1);
    ASSERT_EQ(fd2, 2);

    /* Close fd1, next open should get 1 */
    sys_close(1);
    int fd_new = sys_open_fake(O_RDONLY);
    ASSERT_EQ(fd_new, 1);

    sys_close(0);
    sys_close(1);
    sys_close(2);
}

/* ofile refcount tracks correctly across dup/close */
static void test_refcount(void)
{
    reset_state();

    int fd = sys_open_fake(O_RDWR);
    ASSERT(fd >= 0);
    struct ofile *of = curproc->fd[fd];
    ASSERT_EQ(of->refcount, 1);

    int fd2 = sys_dup(fd);
    ASSERT(fd2 >= 0);
    ASSERT_EQ(of->refcount, 2);

    int fd3 = sys_dup(fd);
    ASSERT(fd3 >= 0);
    ASSERT_EQ(of->refcount, 3);

    sys_close(fd);
    ASSERT_EQ(of->refcount, 2);

    sys_close(fd2);
    ASSERT_EQ(of->refcount, 1);

    sys_close(fd3);
    ASSERT_EQ(of->refcount, 0);
}

/* System-wide ofile table exhaustion returns -ENFILE */
static void test_ofile_table_exhaustion(void)
{
    reset_state();

    /* Allocate all MAXOPEN ofiles (we only have MAXFD per process,
     * so we fill via direct allocation) */
    for (int i = 0; i < MAXOPEN; i++) {
        ofile_table[i].refcount = 1;
    }

    /* sys_open_fake should fail with -ENFILE (no ofile slots) */
    int fd = sys_open_fake(O_RDONLY);
    ASSERT_EQ(fd, -ENFILE);

    /* Clean up */
    for (int i = 0; i < MAXOPEN; i++)
        ofile_table[i].refcount = 0;
}

/* F_SETFL stub returns 0 (no-op) */
static void test_f_setfl_stub(void)
{
    reset_state();
    int fd = sys_open_fake(O_RDONLY);
    ASSERT(fd >= 0);

    int rc = sys_fcntl(fd, 4, O_APPEND); /* F_SETFL */
    ASSERT_EQ(rc, 0);

    sys_close(fd);
}

/* ---- Main ---- */
int main(void)
{
    printf("test_syscalls:\n");

    RUN_TEST(test_fd_flags_roundtrip);
    RUN_TEST(test_fd_flags_mask);
    RUN_TEST(test_getfl_masks_pipe_bits);
    RUN_TEST(test_getfl_masks_pipe_write);
    RUN_TEST(test_getfl_normal_file);
    RUN_TEST(test_maxfd_limit);
    RUN_TEST(test_cloexec_propagation);
    RUN_TEST(test_dup_clears_cloexec);
    RUN_TEST(test_dup2_clears_cloexec);
    RUN_TEST(test_f_dupfd);
    RUN_TEST(test_f_dupfd_invalid_min);
    RUN_TEST(test_f_dupfd_full);
    RUN_TEST(test_close_clears_fd_flags);
    RUN_TEST(test_ebadf);
    RUN_TEST(test_fcntl_invalid_cmd);
    RUN_TEST(test_fd_alloc_lowest);
    RUN_TEST(test_refcount);
    RUN_TEST(test_ofile_table_exhaustion);
    RUN_TEST(test_f_setfl_stub);

    TEST_REPORT();
}
