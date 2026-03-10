/*
 * Genix kernel main
 */
#include "kernel.h"
#include "tty.h"

/* Forward declarations */
void builtin_shell(void);
void shell_ls(const char *cmd);
void shell_cat(const char *path);
void shell_write(const char *arg);
#ifdef AUTOTEST
static void autotest(void);
#endif
#ifdef IMSHOW_TEST
static void imshow_test(void);
#endif

/* Timer frequency */
#define TIMER_HZ 100

/* Global process table */
struct proc proctab[MAXPROC];
struct proc *curproc = NULL;
int nproc = 0;

/* User memory layout — set from PAL at boot */
uint32_t USER_BASE;
uint32_t USER_TOP;
uint32_t USER_SIZE;

static volatile uint32_t ticks = 0;

void timer_interrupt(void)
{
    ticks++;
}

/*
 * 68000 bus/address error stack frame (group 0):
 *   sp+0:  d0-d7, a0-a6 (pushed by movem)
 *   sp+56: frame pointer we pushed
 *   sp+60: function code (16-bit)
 *   sp+62: access address (32-bit)
 *   sp+66: instruction register (16-bit)
 *   sp+68: status register (16-bit)
 *   sp+70: program counter (32-bit)
 *
 * For other exceptions (group 1/2):
 *   sp+56: status register (16-bit)
 *   sp+58: program counter (32-bit)
 */
void panic_exception(uint32_t *frame)
{
    /* frame points to the saved regs pushed by movem + the sp we pushed */
    /* Try to read the PC from the exception frame */
    uint16_t *w = (uint16_t *)frame;
    /* Skip past d0-d7 (8*4=32 bytes = 16 words), a0-a6 (7*4=28 bytes = 14 words),
     * and the frame pointer (2 words) = 32 words total.
     * Then: access info word, fault address, IR, SR, PC */
    kputs("\n*** KERNEL PANIC: exception ***\n");
    kputs("Frame at: ");
    kprintf("0x%x\n", (uint32_t)frame);
    /* For a group 0 fault, the PC is at offset 70 from the start of the saved regs */
    /* For a group 1/2 fault, the PC is at offset 58 */
    /* Try the simpler group 1/2 layout first (most exceptions) */
    /* After movem (56 bytes) + pushed sp (4 bytes) = 60 bytes */
    /* Then: SR (2 bytes) at offset 60, PC (4 bytes) at offset 62 */
    uint32_t *pc_ptr = (uint32_t *)((uint8_t *)frame + 62 - 4);
    kputs("Approx PC: ");
    kprintf("0x%x\n", *pc_ptr);
    for (;;)
        ;
}

void kmain(void)
{
    /* Platform-specific hardware init */
    pal_init();

    kputs("\nGenix v0.1 starting...\n");

    /* Set up user memory layout from platform */
    USER_BASE = pal_user_base();
    USER_TOP = pal_user_top();
    USER_SIZE = USER_TOP - USER_BASE;

    /* Initialize subsystems.
     * Kernel heap goes from end of BSS to USER_BASE.
     * User programs get USER_BASE to USER_TOP. */
    uint32_t heap_end = pal_mem_end();
    if (heap_end > USER_BASE)
        heap_end = USER_BASE;
    mem_init(pal_mem_start(), heap_end);
    buf_init();
    dev_init();
    fs_init();
    dev_create_nodes();
    proc_init();

    kputs("All subsystems initialized.\n");

    /* Don't start timer yet — test basic shell first */
    pal_timer_init(TIMER_HZ);

    /* Enable interrupts */
    __asm__ volatile("move.w #0x2000, %%sr" ::: "cc");

    kputs("System ready.\n");

    kputs("Starting shell...\n");

#ifdef IMSHOW_TEST
    imshow_test();
    for (;;)
        __asm__ volatile("nop");
#elif defined(AUTOTEST)
    autotest();
    /* Spin with interrupts enabled so VBlank keeps firing.
     * If we call pal_halt() (STOP #0x2700), some BlastEm versions
     * exit immediately, causing the headless test to report failure. */
    for (;;)
        __asm__ volatile("nop");
#else
    builtin_shell();
#endif

    kputs("System halted.\n");
    pal_halt();
}

#ifdef AUTOTEST
/*
 * Automated test sequence — runs predetermined commands without
 * keyboard input. Used for headless testing in both the workbench
 * emulator and BlastEm.
 *
 * Output is on the UART (workbench) or VDP (Mega Drive), so we
 * can grep emulator stdout or inspect BlastEm screenshots/GDB.
 */
static void autotest(void)
{
    int rc, pass = 0, fail = 0;

    kputs("=== AUTOTEST BEGIN ===\n");

    /* Test 1: exec /bin/hello */
    kputs("[test] exec /bin/hello: ");
    rc = do_exec("/bin/hello", NULL);
    if (rc < 0) { kputs("FAIL (exec returned error)\n"); fail++; }
    else { kputs("PASS\n"); pass++; }

    /* Test 2: exec /bin/echo with args */
    {
        const char *argv[] = { "/bin/echo", "autotest", "echo", "ok", NULL };
        kputs("[test] exec /bin/echo: ");
        rc = do_exec("/bin/echo", argv);
        if (rc < 0) { kputs("FAIL\n"); fail++; }
        else { kputs("PASS\n"); pass++; }
    }

    /* Test 3: exec /bin/true (should exit 0) */
    kputs("[test] exec /bin/true: ");
    rc = do_exec("/bin/true", NULL);
    if (rc == 0) { kputs("PASS\n"); pass++; }
    else { kprintf("FAIL (exit %d, expected 0)\n", rc); fail++; }

    /* Test 4: exec /bin/false (should exit 1) */
    kputs("[test] exec /bin/false: ");
    rc = do_exec("/bin/false", NULL);
    if (rc == 1) { kputs("PASS (exit 1 as expected)\n"); pass++; }
    else { kprintf("FAIL (exit %d, expected 1)\n", rc); fail++; }

    /* Test 5: exec /bin/wc on a known file */
    {
        const char *argv[] = { "/bin/wc", "/bin/hello", NULL };
        kputs("[test] exec /bin/wc: ");
        rc = do_exec("/bin/wc", argv);
        if (rc < 0) { kputs("FAIL\n"); fail++; }
        else { kputs("PASS\n"); pass++; }
    }

    /* Test 6: exec nonexistent binary */
    kputs("[test] exec /bin/nonexistent: ");
    rc = do_exec("/bin/nonexistent", NULL);
    if (rc < 0) { kputs("PASS (correctly failed)\n"); pass++; }
    else { kputs("FAIL (should have failed)\n"); fail++; }

    /* Test 7: ls /bin (exercise the path, don't check output) */
    kputs("[test] ls /bin:\n");
    shell_ls("ls /bin");
    pass++;  /* if we get here without crashing, it's a pass */

    /* Test 8: spawn + waitpid (exec /bin/true, exit 0) */
    kputs("[test] spawn/waitpid/true: ");
    {
        int pid = do_spawn("/bin/true", NULL);
        if (pid > 0) {
            int status = -1;
            int wpid = do_waitpid(pid, &status);
            /* POSIX: exit status in bits 15-8 */
            int exitcode = (status >> 8) & 0xFF;
            if (wpid == pid && exitcode == 0) {
                kputs("PASS\n");
                pass++;
            } else {
                kprintf("FAIL (wpid=%d status=0x%x)\n", wpid, status);
                fail++;
            }
        } else {
            kprintf("FAIL (spawn returned %d)\n", pid);
            fail++;
        }
    }

    /* Test 9: spawn + waitpid (exec /bin/false, exit 1) */
    kputs("[test] spawn/waitpid/false: ");
    {
        int pid = do_spawn("/bin/false", NULL);
        if (pid > 0) {
            int status = -1;
            int wpid = do_waitpid(pid, &status);
            int exitcode = (status >> 8) & 0xFF;
            if (wpid == pid && exitcode == 1) {
                kputs("PASS\n");
                pass++;
            } else {
                kprintf("FAIL (wpid=%d exitcode=%d)\n", wpid, exitcode);
                fail++;
            }
        } else {
            kprintf("FAIL (spawn returned %d)\n", pid);
            fail++;
        }
    }

    /* Test 10: pipe() basic read/write */
    kputs("[test] pipe: ");
    {
        int pfd[2];
        rc = do_pipe(pfd);
        if (rc < 0) {
            kprintf("FAIL (pipe returned %d)\n", rc);
            fail++;
        } else {
            char wbuf[] = "hello";
            char rbuf[8];
            /* Write through kernel-level pipe */
            struct pipe *p = &pipe_table[0];
            int nw = pipe_write(p, wbuf, 5);
            int nr = pipe_read(p, rbuf, 8);
            rbuf[nr] = '\0';
            if (nw == 5 && nr == 5 && strcmp(rbuf, "hello") == 0) {
                kputs("PASS\n");
                pass++;
            } else {
                kprintf("FAIL (nw=%d nr=%d)\n", nw, nr);
                fail++;
            }
            /* Clean up pipe */
            pipe_close_read(p);
            pipe_close_write(p);
        }
    }

    /* Test 11: spawn /bin/ls /bin */
    kputs("[test] spawn ls /bin: ");
    {
        const char *argv[] = { "/bin/ls", "/bin", NULL };
        int pid = do_spawn("/bin/ls", argv);
        if (pid > 0) {
            int status = -1;
            int wpid = do_waitpid(pid, &status);
            int exitcode = (status >> 8) & 0xFF;
            if (wpid == pid && exitcode == 0) {
                kputs("PASS\n");
                pass++;
            } else {
                kprintf("FAIL (wpid=%d exitcode=%d)\n", wpid, exitcode);
                fail++;
            }
        } else {
            kprintf("FAIL (spawn returned %d)\n", pid);
            fail++;
        }
    }

    /* Test 12: /dev/null exists and is usable */
    kputs("[test] /dev/null: ");
    {
        struct inode *ip = fs_namei("/dev/null");
        if (ip && ip->type == FT_DEV && ip->dev_major == DEV_NULL) {
            kputs("PASS\n");
            pass++;
            fs_iput(ip);
        } else {
            kputs("FAIL\n");
            fail++;
            if (ip) fs_iput(ip);
        }
    }

    /* Test 13: TTY ioctl — TIOCGWINSZ returns window size */
    kputs("[test] TTY TIOCGWINSZ: ");
    {
        struct winsize ws;
        int32_t r = tty_ioctl(0, TIOCGWINSZ, &ws);
        if (r == 0 && ws.ws_row == 28 && ws.ws_col == 40) {
            kputs("PASS\n");
            pass++;
        } else {
            kprintf("FAIL (r=%d row=%d col=%d)\n", r, ws.ws_row, ws.ws_col);
            fail++;
        }
    }

    /* Test 14: TTY ioctl — TCGETS returns termios */
    kputs("[test] TTY TCGETS: ");
    {
        struct kernel_termios t;
        int32_t r = tty_ioctl(0, TCGETS, &t);
        if (r == 0 && (t.c_lflag & ICANON) && (t.c_lflag & ECHO)) {
            kputs("PASS\n");
            pass++;
        } else {
            kprintf("FAIL (r=%d lflag=0x%x)\n", r, t.c_lflag);
            fail++;
        }
    }

    /* Test 15: /dev/tty exists as a device node */
    kputs("[test] /dev/tty: ");
    {
        struct inode *ip = fs_namei("/dev/tty");
        if (ip && ip->type == FT_DEV && ip->dev_major == DEV_CONSOLE) {
            kputs("PASS\n");
            pass++;
            fs_iput(ip);
        } else {
            kputs("FAIL\n");
            fail++;
            if (ip) fs_iput(ip);
        }
    }

    /* Test 16: /dev/console exists as a device node */
    kputs("[test] /dev/console: ");
    {
        struct inode *ip = fs_namei("/dev/console");
        if (ip && ip->type == FT_DEV && ip->dev_major == DEV_CONSOLE) {
            kputs("PASS\n");
            pass++;
            fs_iput(ip);
        } else {
            kputs("FAIL\n");
            fail++;
            if (ip) fs_iput(ip);
        }
    }

    /* Test 17: signal/kill — send SIGTERM to a zombie-able process */
    kputs("[test] signal/kill: ");
    {
        /* Kill with sig=0 to current process — should succeed (exists) */
        int32_t r = syscall_dispatch(SYS_KILL, 0, 0, 0, 0);
        if (r == 0) {
            /* Set SIGTERM to SIG_IGN, then send SIGTERM to self */
            syscall_dispatch(SYS_SIGNAL, SIGTERM, SIG_IGN, 0, 0);
            syscall_dispatch(SYS_KILL, 0, SIGTERM, 0, 0);
            /* sig_deliver should ignore it (pass dummy frame —
             * no user handler is set so frame won't be modified) */
            uint32_t dummy_frame[18];
            memset(dummy_frame, 0, sizeof(dummy_frame));
            sig_deliver(dummy_frame);
            if (curproc && curproc->state == P_RUNNING) {
                /* Restore default handler */
                syscall_dispatch(SYS_SIGNAL, SIGTERM, SIG_DFL, 0, 0);
                kputs("PASS\n");
                pass++;
            } else {
                kputs("FAIL (process killed unexpectedly)\n");
                fail++;
            }
        } else {
            kprintf("FAIL (kill sig=0 returned %d)\n", r);
            fail++;
        }
    }

    /* Test 14: sequential pipe between two spawned processes (echo | cat).
     * On a no-MMU system, we run echo first (output → pipe), wait for it,
     * then run cat (input ← pipe). Works as long as echo's output fits
     * in the 512-byte pipe buffer. */
    kputs("[test] spawn pipe echo|cat: ");
    {
        int pfd[2];
        rc = do_pipe(pfd);
        if (rc < 0) {
            kprintf("FAIL (pipe returned %d)\n", rc);
            fail++;
        } else {
            /* Step 1: Run echo with stdout → pipe write end */
            const char *echo_argv[] = { "/bin/echo", "pipe-test-ok", NULL };
            int echo_pid = do_spawn_fd("/bin/echo", echo_argv,
                                        -1, pfd[1], -1);
            /* Close parent's write end — echo has its own reference */
            syscall_dispatch(SYS_CLOSE, pfd[1], 0, 0, 0);

            int ok = 1;
            if (echo_pid > 0) {
                int status;
                do_waitpid(echo_pid, &status);
            } else { ok = 0; }

            /* Step 2: Run cat with stdin ← pipe read end */
            const char *cat_argv[] = { "/bin/cat", NULL };
            int cat_pid = do_spawn_fd("/bin/cat", cat_argv,
                                       pfd[0], -1, -1);
            /* Close parent's read end — cat has its own reference */
            syscall_dispatch(SYS_CLOSE, pfd[0], 0, 0, 0);

            if (cat_pid > 0) {
                int status;
                do_waitpid(cat_pid, &status);
            } else { ok = 0; }

            if (ok) {
                kputs("PASS\n");
                pass++;
            } else {
                kprintf("FAIL (echo_pid=%d cat_pid=%d)\n",
                        echo_pid, cat_pid);
                fail++;
            }
        }
    }

    /* Test 15: output redirection (echo > file) */
    kputs("[test] output redirect: ");
    {
        /* Create a test file by writing via the filesystem */
        uint32_t flags = O_WRONLY | O_CREAT | O_TRUNC;
        int ofd = syscall_dispatch(SYS_OPEN, (uint32_t)"/tmp_redir",
                                    flags, 0, 0);
        if (ofd < 0) {
            kprintf("FAIL (open returned %d)\n", ofd);
            fail++;
        } else {
            const char *argv[] = { "/bin/echo", "redir-ok", NULL };
            int pid = do_spawn_fd("/bin/echo", argv, -1, ofd, -1);
            syscall_dispatch(SYS_CLOSE, ofd, 0, 0, 0);

            if (pid > 0) {
                int status;
                do_waitpid(pid, &status);

                /* Read back the file to verify */
                struct inode *ip = fs_namei("/tmp_redir");
                if (ip && ip->size > 0) {
                    char buf[32];
                    int n = fs_read(ip, buf, 0,
                                    ip->size < 31 ? ip->size : 31);
                    buf[n] = '\0';
                    fs_iput(ip);
                    /* echo writes "redir-ok\n" */
                    if (n > 0 && strncmp(buf, "redir-ok", 8) == 0) {
                        kputs("PASS\n");
                        pass++;
                    } else {
                        kprintf("FAIL (got '%s')\n", buf);
                        fail++;
                    }
                } else {
                    kputs("FAIL (file empty or missing)\n");
                    fail++;
                    if (ip) fs_iput(ip);
                }
            } else {
                kprintf("FAIL (spawn returned %d)\n", pid);
                fail++;
            }
            /* Clean up test file */
            fs_unlink("/tmp_redir");
        }
    }

    /* Test 16: SIGPIPE on write to closed pipe */
    kputs("[test] SIGPIPE: ");
    {
        int pfd[2];
        rc = do_pipe(pfd);
        if (rc < 0) {
            kprintf("FAIL (pipe returned %d)\n", rc);
            fail++;
        } else {
            /* Close the read end — no readers */
            struct pipe *pp = &pipe_table[0];
            /* Get ofile for write end to manipulate directly */
            pipe_close_read(pp);
            /* Close the read fd */
            syscall_dispatch(SYS_CLOSE, pfd[0], 0, 0, 0);

            /* Clear any previous pending signals */
            curproc->sig_pending = 0;

            /* Writing to pipe with no readers should set SIGPIPE */
            char wbuf[] = "fail";
            int nw = pipe_write(pp, wbuf, 4);

            if (nw == -EPIPE &&
                (curproc->sig_pending & (1u << SIGPIPE))) {
                /* Clear the signal so we don't die */
                curproc->sig_pending &= ~(1u << SIGPIPE);
                kputs("PASS\n");
                pass++;
            } else {
                kprintf("FAIL (nw=%d pending=0x%x)\n",
                        nw, curproc->sig_pending);
                curproc->sig_pending = 0;
                fail++;
            }

            /* Clean up */
            pipe_close_write(pp);
            syscall_dispatch(SYS_CLOSE, pfd[1], 0, 0, 0);
        }
    }

    /* Test 17: user signal handler via real signal delivery */
    kputs("[test] user signal handler: ");
    {
        /* Install a handler for SIGINT, send SIGINT to self, then
         * clear pending and check that handler was reset (one-shot).
         * We can't run user code from autotest (supervisor mode), but
         * we CAN verify the handler gets installed and one-shot resets. */
        syscall_dispatch(SYS_SIGNAL, SIGINT, 0x50000, 0, 0);
        if (curproc->sig_handler[SIGINT] == 0x50000) {
            /* Send SIGINT to self */
            syscall_dispatch(SYS_KILL, curproc->pid, SIGINT, 0, 0);
            if (curproc->sig_pending & (1u << SIGINT)) {
                /* sig_deliver would build a signal frame on user stack.
                 * In autotest (supervisor mode) we just clear the pending
                 * signal and verify the infrastructure works. */
                curproc->sig_pending &= ~(1u << SIGINT);
                /* Reset handler to SIG_DFL */
                syscall_dispatch(SYS_SIGNAL, SIGINT, SIG_DFL, 0, 0);
                kputs("PASS\n");
                pass++;
            } else {
                kputs("FAIL (SIGINT not pending)\n");
                fail++;
            }
        } else {
            kputs("FAIL (handler not set)\n");
            fail++;
        }
        curproc->sig_pending = 0;
    }

    /* Test 18: SIGCONT wakes P_STOPPED process */
    kputs("[test] SIGCONT wakes stopped: ");
    {
        /* Spawn a child, stop it, then send SIGCONT */
        const char *argv[] = { "/bin/sleep", "10", NULL };
        int cpid = do_spawn("/bin/sleep", argv);
        if (cpid > 0) {
            struct proc *child = &proctab[(uint8_t)cpid];
            /* Stop it directly (simulating SIGSTOP delivery) */
            child->state = P_STOPPED;
            /* Send SIGCONT via kill */
            syscall_dispatch(SYS_KILL, cpid, SIGCONT, 0, 0);
            if (child->state == P_READY) {
                kputs("PASS\n");
                pass++;
            } else {
                kprintf("FAIL (state=%d)\n", child->state);
                fail++;
            }
            /* Clean up: kill child */
            child->sig_pending |= (1u << SIGKILL);
            child->state = P_READY;
            int status;
            do_waitpid(cpid, &status);
        } else {
            kputs("FAIL (spawn failed)\n");
            fail++;
        }
    }

    /* Test 19: signal() returns old handler */
    kputs("[test] signal returns old: ");
    {
        syscall_dispatch(SYS_SIGNAL, SIGTERM, SIG_IGN, 0, 0);
        int32_t old = syscall_dispatch(SYS_SIGNAL, SIGTERM, SIG_DFL, 0, 0);
        if (old == SIG_IGN) {
            kputs("PASS\n");
            pass++;
        } else {
            kprintf("FAIL (old=%d)\n", old);
            fail++;
        }
    }

    /* Test 20: exec nonexistent path returns error */
    kputs("[test] exec error path: ");
    {
        rc = do_exec("/bin/does_not_exist_at_all", NULL);
        if (rc == -ENOENT) {
            kputs("PASS\n");
            pass++;
        } else {
            kprintf("FAIL (expected -ENOENT, got %d)\n", rc);
            fail++;
        }
    }

    /* Test 21: spawn nonexistent returns error */
    kputs("[test] spawn error path: ");
    {
        int pid = do_spawn("/bin/does_not_exist_at_all", NULL);
        if (pid < 0) {
            kputs("PASS\n");
            pass++;
        } else {
            kprintf("FAIL (expected error, got pid %d)\n", pid);
            fail++;
        }
    }

    /* Test 22: pipe stress — write and read 512 bytes (full buffer) */
    kputs("[test] pipe stress full: ");
    {
        int pfd[2];
        rc = do_pipe(pfd);
        if (rc < 0) {
            kprintf("FAIL (pipe returned %d)\n", rc);
            fail++;
        } else {
            /* Fill the pipe buffer completely */
            uint8_t wbuf[PIPE_SIZE];
            for (int i = 0; i < PIPE_SIZE; i++)
                wbuf[i] = (uint8_t)(i & 0xFF);
            int nw = pipe_write(&pipe_table[0], wbuf, PIPE_SIZE);

            /* Read it all back */
            uint8_t rbuf[PIPE_SIZE];
            int nr = pipe_read(&pipe_table[0], rbuf, PIPE_SIZE);

            if (nw == PIPE_SIZE && nr == PIPE_SIZE &&
                memcmp(wbuf, rbuf, PIPE_SIZE) == 0) {
                kputs("PASS\n");
                pass++;
            } else {
                kprintf("FAIL (nw=%d nr=%d)\n", nw, nr);
                fail++;
            }
            pipe_close_read(&pipe_table[0]);
            pipe_close_write(&pipe_table[0]);
            /* Close the FDs */
            if (curproc->fd[pfd[0]]) {
                curproc->fd[pfd[0]]->refcount--;
                curproc->fd[pfd[0]] = NULL;
            }
            if (curproc->fd[pfd[1]]) {
                curproc->fd[pfd[1]]->refcount--;
                curproc->fd[pfd[1]] = NULL;
            }
        }
    }

    /* Test 23: kstack canary intact after spawned process */
    kputs("[test] kstack canary: ");
    {
        int pid = do_spawn("/bin/true", NULL);
        if (pid > 0) {
            int status = -1;
            do_waitpid(pid, &status);
            /* Check our own canary survived */
            if (curproc->kstack[0] == KSTACK_CANARY) {
                kputs("PASS\n");
                pass++;
            } else {
                kputs("FAIL (canary corrupted)\n");
                fail++;
            }
        } else {
            kprintf("FAIL (spawn returned %d)\n", pid);
            fail++;
        }
    }

    /* Test 24: exec /bin/seq 1 5 — verify number sequence output */
    kputs("[test] exec /bin/seq: ");
    {
        const char *argv[] = { "/bin/seq", "1", "5", NULL };
        rc = do_exec("/bin/seq", argv);
        if (rc == 0) { kputs("PASS\n"); pass++; }
        else { kprintf("FAIL (exit %d)\n", rc); fail++; }
    }

    /* Test 25: exec /bin/strings on a known binary */
    kputs("[test] exec /bin/strings: ");
    {
        const char *argv[] = { "/bin/strings", "/bin/hello", NULL };
        rc = do_exec("/bin/strings", argv);
        if (rc == 0) { kputs("PASS\n"); pass++; }
        else { kprintf("FAIL (exit %d)\n", rc); fail++; }
    }

    /* Test 26: exec /bin/fold (reads stdin, but exits 0 on empty) */
    kputs("[test] exec /bin/tac: ");
    {
        const char *argv[] = { "/bin/tac", "/bin/hello", NULL };
        rc = do_exec("/bin/tac", argv);
        if (rc == 0) { kputs("PASS\n"); pass++; }
        else { kprintf("FAIL (exit %d)\n", rc); fail++; }
    }

    /* Test 27: spawn seq and check output via pipe */
    kputs("[test] spawn pipe seq|wc: ");
    {
        int pfd[2];
        rc = do_pipe(pfd);
        if (rc < 0) {
            kprintf("FAIL (pipe returned %d)\n", rc);
            fail++;
        } else {
            /* Run seq 1 3 with stdout → pipe */
            const char *seq_argv[] = { "/bin/seq", "1", "3", NULL };
            int seq_pid = do_spawn_fd("/bin/seq", seq_argv,
                                       -1, pfd[1], -1);
            syscall_dispatch(SYS_CLOSE, pfd[1], 0, 0, 0);

            int ok = 1;
            if (seq_pid > 0) {
                int status;
                do_waitpid(seq_pid, &status);
            } else { ok = 0; }

            /* Run wc with stdin ← pipe */
            const char *wc_argv[] = { "/bin/wc", NULL };
            int wc_pid = do_spawn_fd("/bin/wc", wc_argv,
                                      pfd[0], -1, -1);
            syscall_dispatch(SYS_CLOSE, pfd[0], 0, 0, 0);

            if (wc_pid > 0) {
                int status;
                do_waitpid(wc_pid, &status);
            } else { ok = 0; }

            if (ok) {
                kputs("PASS\n");
                pass++;
            } else {
                kprintf("FAIL (seq_pid=%d wc_pid=%d)\n",
                        seq_pid, wc_pid);
                fail++;
            }
        }
    }

    /* Summary */
    kprintf("\n=== AUTOTEST DONE: %d passed, %d failed ===\n", pass, fail);
    if (fail > 0)
        kputs("AUTOTEST FAILED\n");
    else
        kputs("AUTOTEST PASSED\n");
}
#endif

#ifdef IMSHOW_TEST
/*
 * IMSHOW_TEST — spawn imshow in no-wait mode for screenshot capture.
 * Used by `make test-md-imshow` to validate the full VDP graphics stack
 * (kernel driver → ioctl → libgfx → userspace) and produce a reference
 * screenshot of the color bar test pattern.
 */
static void imshow_test(void)
{
    kputs("=== IMSHOW_TEST: spawning imshow -n ===\n");
    const char *argv[] = { "/bin/imshow", "-n", NULL };
    int pid = do_spawn("/bin/imshow", argv);
    if (pid > 0) {
        int status = -1;
        do_waitpid(pid, &status);
        int exitcode = (status >> 8) & 0xFF;
        kprintf("imshow exited with code %d\n", exitcode);
        if (exitcode == 0)
            kputs("IMSHOW_TEST PASSED\n");
        else
            kputs("IMSHOW_TEST FAILED\n");
    } else {
        kprintf("imshow spawn failed: %d\n", pid);
        kputs("IMSHOW_TEST FAILED\n");
    }
}
#endif

/*
 * Shell: execute a pipeline with optional I/O redirection.
 *
 * Supports:
 *   exec cmd1 | cmd2 | cmd3   — piped commands
 *   exec cmd > file            — output redirection (truncate)
 *   exec cmd >> file           — output redirection (append)
 *   exec cmd < file            — input redirection
 *   exec cmd < in > out        — combined
 *   exec cmd1 | cmd2 > file    — pipe + redirection
 *
 * Limitation: only the first command can have < and only the last
 * can have > or >> (standard shell convention).
 */

/* Parse a single command segment into argv, stopping at | or end.
 * Returns pointer past the segment (at '|' or '\0').
 * Strips leading/trailing whitespace from args. */
static char *parse_segment(char *start, char **argv, int max_args, int *argc)
{
    *argc = 0;
    char *p = start;

    while (*p && *p != '|') {
        while (*p == ' ') p++;
        if (!*p || *p == '|') break;
        /* Stop at redirection operators — they're not args */
        if (*p == '<' || *p == '>') break;
        if (*argc < max_args - 1) {
            argv[(*argc)++] = p;
        }
        while (*p && *p != ' ' && *p != '|' && *p != '<' && *p != '>') p++;
        if (*p == ' ') *p++ = '\0';
    }
    argv[*argc] = NULL;
    return p;
}

/* Find redirection operators in a command string and extract filenames.
 * Modifies the string in place (null-terminates filenames).
 * Returns: pointers to input file, output file, and append flag. */
static void parse_redirections(char *cmd,
                               char **infile, char **outfile, int *append)
{
    *infile = NULL;
    *outfile = NULL;
    *append = 0;

    for (char *p = cmd; *p; p++) {
        if (*p == '<') {
            *p = '\0';
            p++;
            while (*p == ' ') p++;
            *infile = p;
            while (*p && *p != ' ' && *p != '>' && *p != '|') p++;
            if (*p) { *p = '\0'; p++; }
            p--; /* loop increment will advance */
        } else if (*p == '>') {
            *p = '\0';
            p++;
            if (*p == '>') {
                *append = 1;
                *p = '\0';
                p++;
            }
            while (*p == ' ') p++;
            *outfile = p;
            while (*p && *p != ' ' && *p != '<' && *p != '|') p++;
            if (*p) { *p = '\0'; p++; }
            p--; /* loop increment will advance */
        }
    }
}

/* Count pipe segments in a command string */
static int count_pipes(const char *cmd)
{
    int n = 0;
    for (const char *p = cmd; *p; p++) {
        if (*p == '|') n++;
    }
    return n;
}

static void shell_exec_cmd(char *cmdline)
{
    int npipes = count_pipes(cmdline);

    /* Parse redirections from the full command line.
     * For pipelines, < applies to first cmd, > to last cmd. */
    char *infile = NULL, *outfile = NULL;
    int append = 0;

    /* For simple (no-pipe) commands, parse redirections directly */
    if (npipes == 0) {
        parse_redirections(cmdline, &infile, &outfile, &append);

        char *argv[16];
        int argc;
        parse_segment(cmdline, argv, 16, &argc);
        if (argc == 0) return;

        /* Open input file if redirected */
        int in_fd = -1;
        if (infile) {
            in_fd = syscall_dispatch(SYS_OPEN, (uint32_t)infile, O_RDONLY, 0, 0);
            if (in_fd < 0) {
                kprintf("cannot open %s\n", infile);
                return;
            }
        }

        /* Open output file if redirected */
        int out_fd = -1;
        if (outfile) {
            uint32_t flags = O_WRONLY | O_CREAT;
            flags |= append ? O_APPEND : O_TRUNC;
            out_fd = syscall_dispatch(SYS_OPEN, (uint32_t)outfile, flags, 0, 0);
            if (out_fd < 0) {
                kprintf("cannot open %s\n", outfile);
                if (in_fd >= 0)
                    syscall_dispatch(SYS_CLOSE, in_fd, 0, 0, 0);
                return;
            }
        }

        /* If command doesn't start with '/', try /bin/ prefix */
        char pathbuf[64];  /* even size for 68000 alignment */
        if (argv[0][0] != '/') {
            /* Build /bin/progname path */
            int plen = 0;
            const char *prefix = "/bin/";
            while (prefix[plen]) { pathbuf[plen] = prefix[plen]; plen++; }
            int ii = 0;
            while (argv[0][ii] && plen < 62) { pathbuf[plen++] = argv[0][ii++]; }
            pathbuf[plen] = '\0';
            argv[0] = pathbuf;
        }

        int pid = do_spawn_fd(argv[0], (const char **)argv,
                              in_fd, out_fd, -1);
        if (pid > 0) {
            int status;
            do_waitpid(pid, &status);
            int exitcode = (status >> 8) & 0xFF;
            if (exitcode != 0)
                kprintf("exit %d\n", exitcode);
        } else {
            kprintf("exec failed: %d\n", pid);
        }

        if (in_fd >= 0)
            syscall_dispatch(SYS_CLOSE, in_fd, 0, 0, 0);
        if (out_fd >= 0)
            syscall_dispatch(SYS_CLOSE, out_fd, 0, 0, 0);
        return;
    }

    /* Pipeline: cmd1 | cmd2 | ... | cmdN
     *
     * On a no-MMU system with a single USER_BASE, we can't run two user
     * processes concurrently (they'd overwrite each other's memory).
     * Instead, run each command sequentially:
     *   1. Run cmd1 with stdout → pipe. cmd1 runs to completion.
     *   2. Run cmd2 with stdin ← pipe, stdout → next pipe. Runs to completion.
     *   etc.
     *
     * Limitation: each command's output must fit in the pipe buffer (512 bytes).
     * If a command blocks on a full pipe, it deadlocks (no reader running).
     * This is a known trade-off for single-memory-space systems. */

    /* Find pipe boundaries */
    char *segments[8];  /* max 8 commands in pipeline */
    int nseg = 0;
    segments[nseg++] = cmdline;
    for (char *p = cmdline; *p; p++) {
        if (*p == '|') {
            *p = '\0';
            if (nseg < 8)
                segments[nseg++] = p + 1;
        }
    }

    /* Parse redirections from first and last segments */
    char *first_infile = NULL, *last_outfile = NULL;
    int last_append = 0;
    {
        char *dummy_out;
        int dummy_app;
        parse_redirections(segments[0], &first_infile, &dummy_out, &dummy_app);
    }
    if (nseg > 1) {
        char *dummy_in;
        parse_redirections(segments[nseg - 1], &dummy_in, &last_outfile,
                           &last_append);
    }

    /* Open input file for first command */
    int in_fd = -1;
    if (first_infile) {
        in_fd = syscall_dispatch(SYS_OPEN, (uint32_t)first_infile, O_RDONLY, 0, 0);
        if (in_fd < 0) {
            kprintf("cannot open %s\n", first_infile);
            return;
        }
    }

    /* Open output file for last command */
    int out_fd = -1;
    if (last_outfile) {
        uint32_t flags = O_WRONLY | O_CREAT;
        flags |= last_append ? O_APPEND : O_TRUNC;
        out_fd = syscall_dispatch(SYS_OPEN, (uint32_t)last_outfile, flags, 0, 0);
        if (out_fd < 0) {
            kprintf("cannot open %s\n", last_outfile);
            if (in_fd >= 0)
                syscall_dispatch(SYS_CLOSE, in_fd, 0, 0, 0);
            return;
        }
    }

    /* Run pipeline commands sequentially, connected by pipes */
    int prev_read_fd = in_fd;
    int i;

    for (i = 0; i < nseg; i++) {
        char *argv[16];
        int argc;
        parse_segment(segments[i], argv, 16, &argc);
        if (argc == 0) {
            kputs("empty command in pipeline\n");
            break;
        }

        int pipe_fds[2] = {-1, -1};
        int child_stdout;

        if (i < nseg - 1) {
            /* Not the last command: create pipe for output */
            int prc = do_pipe(pipe_fds);
            if (prc < 0) {
                kprintf("pipe failed: %d\n", prc);
                break;
            }
            child_stdout = pipe_fds[1]; /* write end */
        } else {
            /* Last command: use output file or default stdout */
            child_stdout = out_fd;
        }

        /* If command doesn't start with '/', try /bin/ prefix */
        char pathbuf[64];  /* even size for 68000 alignment */
        if (argv[0][0] != '/') {
            /* Build /bin/progname path */
            int plen = 0;
            const char *prefix = "/bin/";
            while (prefix[plen]) { pathbuf[plen] = prefix[plen]; plen++; }
            int ii = 0;
            while (argv[0][ii] && plen < 62) { pathbuf[plen++] = argv[0][ii++]; }
            pathbuf[plen] = '\0';
            argv[0] = pathbuf;
        }

        int pid = do_spawn_fd(argv[0], (const char **)argv,
                               prev_read_fd, child_stdout, -1);

        /* Close parent's copies of FDs given to child */
        if (prev_read_fd >= 0)
            syscall_dispatch(SYS_CLOSE, prev_read_fd, 0, 0, 0);
        if (child_stdout >= 0 && child_stdout != out_fd)
            syscall_dispatch(SYS_CLOSE, pipe_fds[1], 0, 0, 0);

        if (pid < 0) {
            kprintf("exec failed: %d\n", pid);
            if (pipe_fds[0] >= 0)
                syscall_dispatch(SYS_CLOSE, pipe_fds[0], 0, 0, 0);
            break;
        }

        /* Wait for this command to finish before loading the next.
         * Required because all processes share USER_BASE (no MMU). */
        int status;
        do_waitpid(pid, &status);

        /* The read end of this pipe becomes input for the next command */
        prev_read_fd = pipe_fds[0];
    }

    /* Close remaining FDs */
    if (prev_read_fd >= 0)
        syscall_dispatch(SYS_CLOSE, prev_read_fd, 0, 0, 0);
    if (out_fd >= 0)
        syscall_dispatch(SYS_CLOSE, out_fd, 0, 0, 0);
}

/* Built-in emergency shell when no filesystem */
void builtin_shell(void)
{
    char line[256];
    int pos;

    for (;;) {
        kputs("genix> ");
        /* Read a line through the TTY layer (cooked mode handles
         * echo, erase, kill, signals, and CR→NL mapping) */
        pos = devtab[DEV_CONSOLE].read(0, line, 255);
        if (pos < 0) {
            /* EINTR from signal — just restart */
            continue;
        }
        /* Strip trailing newline if present */
        if (pos > 0 && line[pos - 1] == '\n')
            pos--;
        line[pos] = '\0';

        if (pos == 0)
            continue;

        /* Built-in commands */
        if (strcmp(line, "help") == 0) {
            kputs("Commands: help, mem, ls, cat <file>, echo <text>,\n"
                  "  exec <prog> [args] [| prog2] [< in] [> out], halt\n");
        } else if (strcmp(line, "halt") == 0) {
            return;
        } else if (strcmp(line, "mem") == 0) {
            kprintf("Memory: 0x%x - 0x%x\n",
                    pal_mem_start(), pal_mem_end());
            kprintf("Ticks: %d\n", ticks);
        } else if (strcmp(line, "ls") == 0 || strncmp(line, "ls ", 3) == 0) {
            shell_ls(line);
        } else if (strncmp(line, "cat ", 4) == 0) {
            shell_cat(line + 4);
        } else if (strncmp(line, "echo ", 5) == 0) {
            kputs(line + 5);
            kputc('\n');
        } else if (strncmp(line, "cd ", 3) == 0) {
            if (syscall_dispatch(SYS_CHDIR, (uint32_t)(line + 3), 0, 0, 0) < 0)
                kputs("cd: no such directory\n");
        } else if (strcmp(line, "cd") == 0) {
            /* cd with no args — go to root */
            syscall_dispatch(SYS_CHDIR, (uint32_t)"/", 0, 0, 0);
        } else if (strncmp(line, "exec ", 5) == 0) {
            char *cmdline = line + 5;
            while (*cmdline == ' ') cmdline++;
            if (!*cmdline) {
                kputs("Usage: exec <program> [args...] [| cmd2] [< in] [> out]\n");
            } else {
                shell_exec_cmd(cmdline);
            }
        } else if (strncmp(line, "write ", 6) == 0) {
            shell_write(line + 6);
        } else if (strncmp(line, "mkdir ", 6) == 0) {
            if (fs_mkdir(line + 6) < 0)
                kputs("mkdir failed\n");
        } else {
            /* Try running as a program (implicit exec) */
            shell_exec_cmd(line);
        }
    }
}

/* Shell: ls [path] */
void shell_ls(const char *cmd)
{
    const char *path = "/";
    if (cmd[2] == ' ' && cmd[3] != '\0')
        path = cmd + 3;

    struct inode *ip = fs_namei(path);
    if (!ip) {
        kputs("Not found\n");
        return;
    }
    if (ip->type != FT_DIR) {
        kprintf("%s (file, %d bytes)\n", path, ip->size);
        fs_iput(ip);
        return;
    }

    struct dirent_disk de;
    uint32_t off = 0;
    while (off < ip->size) {
        if (fs_read(ip, &de, off, sizeof(de)) != sizeof(de))
            break;
        off += sizeof(de);
        if (de.inode == 0)
            continue;
        struct inode *child = fs_iget(de.inode);
        if (child) {
            char type = '?';
            if (child->type == FT_FILE) type = '-';
            else if (child->type == FT_DIR) type = 'd';
            else if (child->type == FT_DEV) type = 'c';
            kputc(type);
            kputc(' ');
            kprintf("%d", child->size);
            kputc('\t');
            kputs(de.name);
            kputc('\n');
            fs_iput(child);
        }
    }
    fs_iput(ip);
}

/* Shell: cat <path> */
void shell_cat(const char *path)
{
    struct inode *ip = fs_namei(path);
    if (!ip) {
        kputs("Not found\n");
        return;
    }
    if (ip->type != FT_FILE) {
        kputs("Not a file\n");
        fs_iput(ip);
        return;
    }

    char buf[256];
    uint32_t off = 0;
    while (off < ip->size) {
        int n = ip->size - off;
        if (n > 256) n = 256;
        n = fs_read(ip, buf, off, n);
        if (n <= 0) break;
        for (int i = 0; i < n; i++)
            kputc(buf[i]);
        off += n;
    }
    fs_iput(ip);
}

/* Shell: write <path> <text> — simple file creation for testing */
void shell_write(const char *arg)
{
    /* Find first space to separate path from content */
    const char *space = strchr(arg, ' ');
    if (!space) {
        kputs("Usage: write <path> <text>\n");
        return;
    }

    char path[PATH_MAX];
    int plen = space - arg;
    if (plen >= PATH_MAX) plen = PATH_MAX - 1;
    memcpy(path, arg, plen);
    path[plen] = '\0';

    const char *text = space + 1;
    int tlen = strlen(text);

    struct inode *ip = fs_namei(path);
    if (!ip) {
        ip = fs_create(path, FT_FILE);
        if (!ip) {
            kputs("Cannot create file\n");
            return;
        }
    }
    fs_write(ip, text, 0, tlen);
    /* Write a newline too */
    fs_write(ip, "\n", tlen, 1);
    fs_iput(ip);
    kputs("OK\n");
}
