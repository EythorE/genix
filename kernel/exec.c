/*
 * Binary loader and exec() implementation
 *
 * Loads Genix flat binaries from the filesystem and executes them.
 * Two modes:
 *   - Synchronous (do_exec): blocks caller until program exits.
 *     Used by autotest and the shell's single-tasking "exec" command.
 *   - Async (load_binary): loads binary, returns entry/sp for caller
 *     to set up a schedulable process. Used by do_spawn.
 */
#include "kernel.h"

/* Global state for single-tasking exec (exec_enter/exec_leave) */
int exec_exit_code;
int exec_active = 0;

/*
 * Validate a Genix binary header.
 * Returns 0 on success, negative errno on failure.
 */
int exec_validate_header(const struct genix_header *hdr)
{
    if (hdr->magic != GENIX_MAGIC)
        return -ENOEXEC;

    if (hdr->load_size == 0)
        return -ENOEXEC;

    /* Check that the binary fits in user memory */
    uint32_t stack = hdr->stack_size ? hdr->stack_size : USER_STACK_DEFAULT;
    uint32_t total = hdr->load_size + hdr->bss_size + stack;
    if (total > USER_SIZE)
        return -ENOMEM;

    /* Entry point must be within the loaded region */
    if (hdr->entry < USER_BASE ||
        hdr->entry >= USER_BASE + hdr->load_size)
        return -ENOEXEC;

    return 0;
}

/*
 * Set up the user stack with argc, argv[], envp[], and string data.
 * Returns the initial stack pointer (word-aligned).
 *
 * Stack layout (growing downward from stack_top):
 *   [string data for argv[0], argv[1], ...]
 *   [padding to 4-byte alignment]
 *   NULL                    (end of envp)
 *   NULL                    (end of argv)
 *   argv[argc-1]            (pointer to string)
 *   ...
 *   argv[0]                 (pointer to string)
 *   argc                    (uint32_t)
 *   <- SP points here
 */
uint32_t exec_setup_stack(uint32_t stack_top, const char *path,
                          const char **argv)
{
    /* Count args */
    int argc = 0;
    if (argv) {
        while (argv[argc])
            argc++;
    }
    /* If no argv, use path as argv[0] */
    if (argc == 0)
        argc = 1;

    /* Calculate string space needed */
    uint32_t str_size = strlen(path) + 1;  /* argv[0] = path */
    if (argv) {
        for (int i = 1; i < argc; i++)
            str_size += strlen(argv[i]) + 1;
    }

    /* Align string space up to 4 bytes */
    str_size = (str_size + 3) & ~3;

    /* Total stack setup:
     * str_size + (argc+1)*4 (argv pointers + NULL) + 4 (envp NULL) + 4 (argc) */
    uint32_t setup_size = str_size + (argc + 1) * 4 + 4 + 4;

    /* SP must be even on 68000 */
    uint32_t sp = (stack_top - setup_size) & ~3;

    /* Write argc */
    uint32_t *stack = (uint32_t *)sp;
    stack[0] = argc;

    /* Copy strings starting after all pointers */
    uint32_t str_base = sp + 4 + (argc + 1) * 4 + 4;
    uint32_t str_pos = str_base;

    /* argv[0] = path */
    stack[1] = str_pos;
    strcpy((char *)str_pos, path);
    str_pos += strlen(path) + 1;

    /* argv[1..] */
    if (argv) {
        for (int i = 1; i < argc; i++) {
            stack[1 + i] = str_pos;
            strcpy((char *)str_pos, argv[i]);
            str_pos += strlen(argv[i]) + 1;
        }
    }

    /* argv[argc] = NULL */
    stack[1 + argc] = 0;

    /* envp[0] = NULL */
    stack[2 + argc] = 0;

    return sp;
}

/*
 * Load a binary from the filesystem into user memory.
 *
 * Reads the binary header, validates it, loads text+data at USER_BASE,
 * zeros BSS, and sets up the user stack with argc/argv.
 *
 * On success, fills in *entry_out and *user_sp_out and returns 0.
 * On failure, returns negative errno.
 */
int load_binary(const char *path, const char **argv,
                uint32_t *entry_out, uint32_t *user_sp_out)
{
    struct inode *ip = fs_namei(path);
    if (!ip)
        return -ENOENT;

    if (ip->type != FT_FILE) {
        fs_iput(ip);
        return -ENOEXEC;
    }

    /* Read and validate header */
    struct genix_header hdr;
    int n = fs_read(ip, &hdr, 0, sizeof(hdr));
    if (n != sizeof(hdr)) {
        fs_iput(ip);
        return -ENOEXEC;
    }

    int err = exec_validate_header(&hdr);
    if (err < 0) {
        fs_iput(ip);
        return err;
    }

    /* Load text+data into user memory at USER_BASE */
    n = fs_read(ip, (void *)USER_BASE, GENIX_HDR_SIZE, hdr.load_size);
    fs_iput(ip);

    if (n != (int)hdr.load_size) {
        kprintf("[exec] Short read: got %d, expected %d\n",
                n, hdr.load_size);
        return -EIO;
    }

    /* Zero BSS */
    if (hdr.bss_size > 0)
        memset((void *)(USER_BASE + hdr.load_size), 0, hdr.bss_size);

    /* Set up user stack */
    uint32_t user_sp = exec_setup_stack(USER_TOP, path, argv);

    /* Update process info */
    if (curproc) {
        curproc->mem_base = USER_BASE;
        curproc->mem_size = USER_SIZE;
        curproc->brk = USER_BASE + hdr.load_size + hdr.bss_size;
    }

    kprintf("[exec] %s: %d bytes loaded at 0x%x, entry 0x%x\n",
            path, hdr.load_size, USER_BASE, hdr.entry);

    *entry_out = hdr.entry;
    *user_sp_out = user_sp;
    return 0;
}

/*
 * Synchronous exec: load and run a binary, blocking until it exits.
 *
 * Uses exec_enter/exec_leave to save kernel context, enter user mode,
 * and return when the program calls _exit(). Used by autotest and the
 * shell's single-tasking "exec" command via do_spawn's synchronous path.
 *
 * Returns the program's exit code (>= 0) or negative errno.
 */
int do_exec(const char *path, const char **argv)
{
    uint32_t entry, user_sp;
    int err = load_binary(path, argv, &entry, &user_sp);
    if (err < 0)
        return err;

    /* Transfer control to the user program.
     * exec_enter saves kernel context, switches to user mode, and jumps
     * to the entry point. Returns here when _exit() calls exec_leave(). */
    exec_active = 1;
    int exitcode = exec_enter(entry, user_sp, proc_kstack_top(curproc));
    exec_active = 0;

    kprintf("[exec] Program exited with code %d\n", exitcode);
    return exitcode;
}
