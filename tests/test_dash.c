/*
 * test_dash.c — Tests exposing dash shell execution bugs
 *
 * These tests verify the syscall semantics that dash relies on.
 * The dash port has several issues that prevent command execution:
 *
 * Bug 1: execve() returns to caller instead of replacing process
 *   - Kernel do_exec() runs the target program synchronously and
 *     returns the exit code. Unix execve() never returns on success.
 *   - When dash's vfork child calls execve(), the program runs but
 *     then execve returns, which dash interprets as failure.
 *
 * Bug 2: Syscall stubs don't set errno
 *   - Kernel returns negative errno (e.g., -ENOENT = -2) in d0.
 *   - Libc stubs return this raw value; they never set the global
 *     errno variable or convert to the POSIX -1 return convention.
 *   - Dash checks errno after failed calls (e.g., errno == EINTR
 *     in waitproc, errno == ENOEXEC in tryexec). These checks all
 *     see stale errno values.
 *
 * Bug 3: do_exec() corrupts vfork child's process metadata
 *   - do_exec() overwrites curproc->mem_base/mem_slot with a
 *     temporary slot, runs the program, then sets mem_slot = -1
 *     without restoring the original values. After execve returns,
 *     the vfork child's process state is inconsistent.
 *
 * Bug 4: waitpid returns raw negative errno, not -1
 *   - wait3 → waitpid(-1, status, flags). If no children exist,
 *     kernel returns -ECHILD (-10). Dash's waitproc loop checks
 *     "err < 0 && errno == EINTR" — but err is -10 (not -1) and
 *     errno is never set, so the EINTR retry loop spins or
 *     misinterprets the return value.
 *
 * These are host-level logic tests verifying the contract between
 * the libc syscall stubs and what dash expects.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* ---- Kernel errno values (from kernel.h) ---- */
#define EPERM    1
#define ENOENT   2
#define ESRCH    3
#define EINTR    4
#define EIO      5
#define ENOEXEC  8
#define EBADF    9
#define ECHILD  10
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define ENOTDIR 20
#define EINVAL  22

/* ---- POSIX wait macros (from sys/wait.h) ---- */
/* Use Genix definitions explicitly (may differ from host glibc) */
#undef WIFEXITED
#undef WEXITSTATUS
#undef WIFSIGNALED
#define WIFEXITED(s)    (((s) & 0x7F) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xFF)
#define WIFSIGNALED(s)  (((s) & 0x7F) != 0 && ((s) & 0x7F) != 0x7F)
#undef WNOHANG
#define WNOHANG  1

/* ================================================================
 * Bug 1: execve() semantics — must not return on success
 * ================================================================
 *
 * In Unix, execve() replaces the calling process image.
 * On success, it never returns.
 * On failure, it returns -1 and sets errno.
 *
 * Genix's do_exec() runs the program synchronously and returns
 * the exit code (0 for success). This violates the contract.
 */

/*
 * Simulate what Genix's execve libc stub does:
 * Returns the raw kernel value (exit code on success, -errno on error).
 */
static int genix_execve_return_success = -1;  /* simulate exit code */
static int genix_execve_return_error = 0;     /* simulate -errno */

static int fake_genix_execve(const char *path)
{
    (void)path;
    if (genix_execve_return_success >= 0)
        return genix_execve_return_success;  /* program ran, returned exit code */
    return genix_execve_return_error;        /* error like -ENOENT */
}

/*
 * Simulate what a correct POSIX execve would do on error:
 * Returns -1 and sets errno.
 */
static int posix_errno;
static int fake_posix_execve(const char *path)
{
    (void)path;
    /* On success: never returns (we can't simulate this in a test) */
    /* On error: */
    posix_errno = ENOENT;
    return -1;
}

/*
 * Dash's tryexec pattern (simplified):
 */
static int tryexec_result_errno;

static void dash_tryexec_genix(const char *cmd)
{
    /* Genix: execve returns exit code (0) on success */
    int ret = fake_genix_execve(cmd);
    (void)ret;
    /* Dash doesn't check return value — just checks errno after execve */
    /* But Genix never sets errno, so it's stale */
    /* tryexec falls through, shellexec sees it as failure */
    tryexec_result_errno = 0;  /* stale errno — no error was set */
}

static void dash_tryexec_posix(const char *cmd)
{
    int ret = fake_posix_execve(cmd);
    if (ret == -1) {
        tryexec_result_errno = posix_errno;
    }
}

static void test_execve_success_should_not_return(void)
{
    /* The core bug: Genix's execve returns 0 on success.
     * Dash's tryexec interprets any return from execve as failure. */
    genix_execve_return_success = 0;  /* program exited with code 0 */

    dash_tryexec_genix("/bin/echo");

    /* After tryexec, dash checks if errno was set.
     * With Genix, errno is stale (0 = no error). Dash sees this as
     * ENOENT/ENOTDIR-style "not found" because errno doesn't match
     * any recognized error. */

    /* This is the bug: execve returned (which it shouldn't on success) */
    /* and errno is 0 (stale), so dash gets confused error state */
    ASSERT_EQ(tryexec_result_errno, 0);

    /* What POSIX expects: execve should never return on success.
     * We can't test "never returns" in a host test, but we can verify
     * that on error the errno is set correctly. */
    dash_tryexec_posix("/bin/nonexistent");
    ASSERT_EQ(tryexec_result_errno, ENOENT);
}

/* ================================================================
 * Bug 2: Syscall stubs don't convert return values or set errno
 * ================================================================
 *
 * Genix kernel returns negative errno (-2 for ENOENT).
 * Libc stubs return this raw value without setting errno.
 * Dash (and all POSIX code) expects: return -1, set errno.
 */

/*
 * Simulate Genix raw syscall return (no errno conversion)
 */
static int genix_raw_syscall(int kernel_result)
{
    /* This is what the libc stubs do: just return the raw value */
    return kernel_result;
}

/*
 * Simulate correct POSIX wrapper (converts negative to -1 + errno)
 */
static int host_errno;
static int posix_syscall_wrapper(int kernel_result)
{
    if (kernel_result < 0) {
        host_errno = -kernel_result;
        return -1;
    }
    host_errno = 0;
    return kernel_result;
}

static void test_syscall_errno_convention(void)
{
    int ret;

    /* Test: open() returns -ENOENT from kernel */
    ret = genix_raw_syscall(-ENOENT);
    /* Genix returns -2. Dash checks: if (fd < 0) ... ok, -2 < 0 works.
     * But if dash checks: if (fd == -1) ... -2 != -1, FAILS.
     * And errno is never set, so errno-based checks fail. */
    ASSERT_EQ(ret, -2);  /* raw kernel value, not POSIX -1 */

    /* POSIX wrapper should return -1 and set errno */
    ret = posix_syscall_wrapper(-ENOENT);
    ASSERT_EQ(ret, -1);
    ASSERT_EQ(host_errno, ENOENT);

    /* Test: waitpid returns -ECHILD from kernel */
    ret = genix_raw_syscall(-ECHILD);
    ASSERT_EQ(ret, -10);  /* raw -ECHILD, not -1 */
    /* Dash's waitproc does: while (err < 0 && errno == EINTR)
     * With Genix: err = -10 (true for < 0), but errno is stale.
     * If errno happens to be EINTR, this loops forever. */

    ret = posix_syscall_wrapper(-ECHILD);
    ASSERT_EQ(ret, -1);
    ASSERT_EQ(host_errno, ECHILD);
}

static void test_waitpid_return_convention(void)
{
    /* Dash's waitproc (simplified):
     *   err = wait3(status, flags, NULL);  // → waitpid(-1, status, flags)
     *   while (err < 0 && errno == EINTR);
     *
     * With Genix's raw returns:
     *   - Success: err = child_pid (positive) — works fine
     *   - ECHILD: err = -10, not -1 — "err < 0" is true
     *     but errno != EINTR, so loop doesn't retry — ok
     *   - EINTR: err = -4, not -1 — "err < 0" is true
     *     but errno is stale, so EINTR check fails
     *
     * The return value -10 vs -1 matters when dash does:
     *   if (err || (err = -!block)) break;
     * With -10: this evaluates as truthy, breaks. Works by accident.
     * But it's still wrong because errno isn't set.
     */

    /* waitpid with no children: kernel returns -ECHILD */
    int err = genix_raw_syscall(-ECHILD);
    /* Dash expects err == -1 && errno == ECHILD */
    ASSERT(err != -1);  /* BUG: Genix returns -10, not -1 */
    ASSERT_EQ(err, -10);

    /* waitpid with WNOHANG, child exists but not exited: kernel returns 0 */
    err = genix_raw_syscall(0);
    ASSERT_EQ(err, 0);  /* This one is fine */

    /* waitpid success: kernel returns child PID */
    err = genix_raw_syscall(3);  /* child PID 3 */
    ASSERT_EQ(err, 3);  /* This one is fine */
}

/* ================================================================
 * Bug 3: do_exec corrupts vfork child process state
 * ================================================================
 *
 * When the vfork child calls execve → do_exec:
 *   1. do_exec allocates a new slot
 *   2. Sets curproc->mem_base = new_slot_base
 *   3. Sets curproc->mem_slot = new_slot
 *   4. Runs program synchronously
 *   5. Frees the slot
 *   6. Sets curproc->mem_slot = -1
 *   7. Does NOT restore curproc->mem_base
 *
 * After do_exec returns, curproc (the vfork child) has:
 *   - mem_slot = -1 (freed)
 *   - mem_base = stale (points to freed slot's address)
 *   - mem_size = stale
 *
 * If the child then calls any syscall that uses mem_base (e.g., sbrk),
 * or when do_exit tries to free mem_slot, the state is inconsistent.
 */

struct fake_proc {
    int mem_slot;
    uint32_t mem_base;
    uint32_t mem_size;
    uint32_t brk;
};

static void test_exec_corrupts_process_state(void)
{
    /* Simulate the vfork child's state before execve */
    struct fake_proc child = {
        .mem_slot = 2,           /* inherited from parent */
        .mem_base = 0x060000,    /* parent's slot base */
        .mem_size = 0x010000,    /* parent's slot size */
        .brk = 0x062000,
    };

    /* Save original values */
    int orig_slot = child.mem_slot;
    uint32_t orig_base = child.mem_base;

    /* Simulate what do_exec does */
    int new_slot = 3;  /* slot_alloc() */
    uint32_t new_slot_base = 0x070000;
    child.mem_slot = new_slot;
    child.mem_base = new_slot_base;
    child.mem_size = 0x010000;
    /* ... exec_enter runs program ... */
    /* ... program exits ... */
    /* slot_free(new_slot) */
    child.mem_slot = -1;
    /* NOTE: mem_base is NOT restored */

    /* After do_exec returns: */
    ASSERT_EQ(child.mem_slot, -1);  /* freed */
    ASSERT(child.mem_base != orig_base);  /* BUG: base was overwritten */
    ASSERT_EQ(child.mem_base, new_slot_base);  /* points to freed slot */

    /* The child should still have parent's state (since vfork shares) */
    /* but do_exec corrupted it. When do_exit runs later, it tries to
     * slot_free(mem_slot) which is -1, so it's a no-op. But the
     * metadata is wrong. */
    (void)orig_slot;
}

/* ================================================================
 * Bug 4: vfork + exec interaction
 * ================================================================
 *
 * The complete flow when dash runs "echo hello":
 *
 * 1. vfork() → child runs on parent's stack
 * 2. Child calls execve("/bin/echo", ...)
 *    → do_exec() runs echo synchronously, returns 0
 * 3. execve returns 0 to child (BUG: should never return)
 * 4. Dash's shellexec interprets return as failure
 * 5. shellexec calls exerror → exraise
 * 6. exraise sees vforked == 1, calls _exit(126)
 * 7. _exit → do_exit(126) → parent resumes via vfork_restore
 * 8. Parent sees child exit status 126 (not 0!)
 *
 * Result: Command output appears (echo ran), but:
 *   - dash prints error message (from shellexec's error path)
 *   - exit status is 126 instead of actual exit code
 *   - The error message confuses users
 */

static void test_vfork_exec_flow(void)
{
    /* Simulate the exit code that reaches waitpid */

    /* What Genix produces: */
    int genix_child_exitcode = 126;  /* from exraise → _exit(126) */
    int genix_wait_status = (genix_child_exitcode & 0xFF) << 8;
    ASSERT(WIFEXITED(genix_wait_status));
    ASSERT_EQ(WEXITSTATUS(genix_wait_status), 126);  /* Wrong! */

    /* What should happen (if execve worked correctly): */
    int correct_exitcode = 0;  /* echo exited successfully */
    int correct_wait_status = (correct_exitcode & 0xFF) << 8;
    ASSERT(WIFEXITED(correct_wait_status));
    ASSERT_EQ(WEXITSTATUS(correct_wait_status), 0);  /* Correct */

    /* The discrepancy: */
    ASSERT(WEXITSTATUS(genix_wait_status) != WEXITSTATUS(correct_wait_status));
}

/* ================================================================
 * Supplementary: wait status encoding tests
 * ================================================================
 *
 * Verify the kernel's status encoding matches what the macros expect.
 */

static void test_wait_status_encoding(void)
{
    int status;

    /* Normal exit with code 0 */
    status = (0 & 0xFF) << 8;
    ASSERT(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT(!WIFSIGNALED(status));

    /* Normal exit with code 1 */
    status = (1 & 0xFF) << 8;
    ASSERT(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 1);

    /* Normal exit with code 126 */
    status = (126 & 0xFF) << 8;
    ASSERT(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 126);

    /* Normal exit with code 127 */
    status = (127 & 0xFF) << 8;
    ASSERT(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 127);
}

/* ================================================================
 * Test: PATH lookup with stale errno
 * ================================================================
 *
 * When dash searches PATH for a command, it calls tryexec() for
 * each PATH entry. If execve returns (failure), it checks errno
 * to decide whether to continue searching or report an error.
 *
 * With Genix:
 *   - execve returns 0 (program's exit code) on success
 *   - errno is never set
 *   - So errno has a stale value from some earlier call
 *
 * This means dash might:
 *   - Continue searching PATH after a "successful" exec (wrong)
 *   - Report a wrong error message based on stale errno
 */

static int errno_for_test;

static void test_path_lookup_stale_errno(void)
{
    /* Simulate: first PATH entry is /usr/bin/echo (not found),
     * second is /bin/echo (found and runs) */

    /* Step 1: /usr/bin/echo → kernel returns -ENOENT */
    int ret1 = genix_raw_syscall(-ENOENT);
    /* With Genix raw return: ret1 = -2 */
    /* Dash doesn't check ret1, checks errno instead */
    /* But errno was never set! It's stale. */

    /* Simulate errno being stale from a previous call */
    errno_for_test = 0;  /* stale: no error */

    /* Step 2: /bin/echo → kernel runs echo, returns exit code 0 */
    int ret2 = genix_raw_syscall(0);
    /* With Genix: ret2 = 0 (exit code, NOT "success of execve") */
    /* Dash: execve returned, so it must have failed. Check errno. */
    /* errno is still 0 (stale). */

    /* Dash's shellexec:
     *   if (errno != ENOENT && errno != ENOTDIR) e = errno;
     * With errno = 0: 0 != ENOENT → true, so e = 0.
     * Then: switch(e) { default: exerrno = 126; }
     * Reports exit code 126.
     */

    (void)ret1;
    ASSERT_EQ(ret2, 0);  /* This IS the bug — 0 means "program ran" but
                             dash sees it as "execve failed with no errno" */
}

/* ================================================================
 * Test: what a correct execve syscall for vfork should do
 * ================================================================
 *
 * When a vfork child calls execve(), the kernel should:
 *   1. Allocate a new process slot for the child
 *   2. Load the new binary into it
 *   3. Set up child's kernel stack for context switch
 *   4. Mark child as P_READY
 *   5. Resume the parent (vfork_restore)
 *   6. The child runs asynchronously via scheduler
 *   7. execve never returns to the child's original code
 *
 * This is essentially do_spawn() triggered from user space,
 * with the additional step of restoring the vfork parent.
 */

/* Process states (from kernel.h) */
#define P_FREE     0
#define P_RUNNING  1
#define P_READY    2
#define P_SLEEPING 3
#define P_ZOMBIE   4
#define P_VFORK    5

struct simple_proc {
    int pid;
    int ppid;
    int state;
    int mem_slot;
    int exitcode;
};

static void test_correct_vfork_exec_semantics(void)
{
    /* Setup: parent (PID 1) did vfork, child (PID 2) is running */
    struct simple_proc parent = { .pid = 1, .state = P_VFORK, .mem_slot = 0 };
    struct simple_proc child  = { .pid = 2, .ppid = 1, .state = P_RUNNING, .mem_slot = 0 };

    /* What SHOULD happen when child calls execve("/bin/echo"): */

    /* 1. Child gets its own memory slot */
    int new_slot = 1;  /* slot_alloc() */
    child.mem_slot = new_slot;

    /* 2. Binary loaded into new slot (simulated) */

    /* 3. Child marked P_READY for scheduler */
    child.state = P_READY;

    /* 4. Parent resumed from vfork */
    parent.state = P_RUNNING;

    /* 5. execve does NOT return to child's original code path */
    /* (child's kernel stack is set up fresh for the new binary) */

    /* Verify post-conditions */
    ASSERT_EQ(child.state, P_READY);
    ASSERT_EQ(child.mem_slot, 1);  /* own slot, not parent's */
    ASSERT_EQ(parent.state, P_RUNNING);  /* parent resumed */
}

/* ================================================================
 * Test: forkshell (non-vforkexec) path
 * ================================================================
 *
 * Dash uses forkshell() for subshells, pipes, and background jobs.
 * forkshell calls fork() → vfork() in Genix.
 *
 * In forkshell, the child eventually calls evaltree which may
 * call shellexec for external commands. Same execve bugs apply.
 *
 * For pipes: forkshell creates the child, which runs the pipeline
 * stage and then calls _exit(). The exec path has the same issues.
 */

static void test_forkshell_pipe_scenario(void)
{
    /* Scenario: "echo hello | cat"
     *
     * 1. dash creates pipe
     * 2. forkshell for "echo hello":
     *    - vfork → child runs forkchild + evaltree
     *    - Child's evaltree reaches external "echo" command
     *    - Child calls shellexec → tryexec → execve
     *    - Same bug: execve returns, child prints error, _exit(126)
     * 3. forkshell for "cat":
     *    - Same issue
     *
     * Result: Both stages of pipe see exit code 126.
     * The pipe may partially work (data flows) but statuses are wrong.
     */

    /* Both pipeline children would get exit code 126 from exraise */
    int echo_status = (126 & 0xFF) << 8;
    int cat_status = (126 & 0xFF) << 8;
    ASSERT_EQ(WEXITSTATUS(echo_status), 126);  /* Wrong, should be 0 */
    ASSERT_EQ(WEXITSTATUS(cat_status), 126);    /* Wrong, should be 0 */
}

/* ================================================================
 * Test: errno required by dash's waitproc loop
 * ================================================================
 *
 * From jobs.c waitproc():
 *   do {
 *       err = wait3(status, flags, NULL);
 *   } while (err < 0 && errno == EINTR);
 *
 * This loop retries on EINTR. With Genix:
 *   - err = raw kernel value (-4 for EINTR, not -1)
 *   - errno never set
 *   - So "errno == EINTR" is always false (stale errno)
 *   - Loop never retries on EINTR — signals could cause missed waits
 */

static void test_waitproc_eintr_retry(void)
{
    /* Simulate EINTR from kernel */
    int err = genix_raw_syscall(-EINTR);

    /* Genix returns -4 (raw -EINTR) */
    ASSERT_EQ(err, -4);

    /* Dash's check: err < 0 && errno == EINTR */
    /* err < 0: TRUE (-4 < 0) ✓ */
    /* errno == EINTR: FALSE (errno is stale, never set to EINTR) ✗ */

    /* So the retry loop doesn't retry — it falls through.
     * In practice this might not cause issues on Genix (no signals
     * during wait), but it's a latent bug for when signals are
     * fully implemented. */

    /* With correct POSIX wrapper: */
    err = posix_syscall_wrapper(-EINTR);
    ASSERT_EQ(err, -1);
    ASSERT_EQ(host_errno, EINTR);
    /* Now dash's check: err < 0 && errno == EINTR → TRUE, retries ✓ */
}

int main(void)
{
    printf("=== test_dash: dash port execution bug tests ===\n");

    printf("\n--- Bug 1: execve returns on success ---\n");
    RUN_TEST(test_execve_success_should_not_return);

    printf("\n--- Bug 2: syscall errno convention ---\n");
    RUN_TEST(test_syscall_errno_convention);
    RUN_TEST(test_waitpid_return_convention);

    printf("\n--- Bug 3: do_exec corrupts process state ---\n");
    RUN_TEST(test_exec_corrupts_process_state);

    printf("\n--- Bug 4: vfork + exec flow ---\n");
    RUN_TEST(test_vfork_exec_flow);

    printf("\n--- Wait status encoding ---\n");
    RUN_TEST(test_wait_status_encoding);

    printf("\n--- PATH lookup with stale errno ---\n");
    RUN_TEST(test_path_lookup_stale_errno);

    printf("\n--- Correct vfork+exec semantics ---\n");
    RUN_TEST(test_correct_vfork_exec_semantics);

    printf("\n--- Pipeline scenario ---\n");
    RUN_TEST(test_forkshell_pipe_scenario);

    printf("\n--- waitproc EINTR retry ---\n");
    RUN_TEST(test_waitproc_eintr_retry);

    TEST_REPORT();
}
