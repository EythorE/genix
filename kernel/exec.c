/*
 * Binary loader and exec() implementation
 *
 * Loads Genix relocatable flat binaries from the filesystem and executes
 * them. All binaries are linked at address 0; the loader adds the actual
 * load address to all absolute references using the relocation table.
 *
 * Three modes:
 *   - Synchronous (do_exec): blocks caller until program exits.
 *     Used by autotest and the shell's single-tasking "exec" command.
 *   - Async (load_binary): loads binary contiguously, returns entry/sp
 *     for caller to set up a schedulable process. Used by do_spawn.
 *   - Split XIP (load_binary_xip): loads text and data to separate
 *     addresses with independent relocation bases. For bank-swapping
 *     where text lives in SRAM and data in main RAM.
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

    /* Entry must be within the loaded region (0-based offset) */
    if (hdr->entry >= hdr->load_size)
        return -ENOEXEC;

    /* text_size must not exceed load_size */
    if (hdr->text_size > hdr->load_size)
        return -ENOEXEC;

    /* Check that the binary fits in user memory.
     * Need: load_size + bss_size + stack.
     * The relocation table is loaded into BSS temporarily, so BSS
     * must be large enough for the reloc table (or we pad if needed). */
    uint32_t stack = hdr->stack_size ? hdr->stack_size : USER_STACK_DEFAULT;
    uint32_t reloc_bytes = hdr->reloc_count * 4;

    /* BSS must be at least as large as the relocation table,
     * since we load the reloc table into BSS area temporarily. */
    uint32_t effective_bss = hdr->bss_size;
    if (reloc_bytes > effective_bss)
        effective_bss = reloc_bytes;

    uint32_t total = hdr->load_size + effective_bss + stack;
    if (total > USER_SIZE)
        return -ENOMEM;

    return 0;
}

/*
 * Apply relocations to a loaded binary (contiguous layout).
 *
 * Each relocation entry is an offset into the loaded image where a
 * 32-bit word contains a zero-based absolute address. We add load_addr
 * to each such word.
 *
 * When text_size > 0 (split text/data), the relocator can distinguish
 * text references from data references:
 *   - Values < text_size reference text -> add text_base
 *   - Values >= text_size reference data -> subtract text_size, add data_base
 *
 * In the contiguous case, text_base == load_addr and
 * data_base == load_addr + text_size, so both paths produce the same
 * result (value + load_addr).
 *
 * For non-contiguous layouts (XIP), use apply_relocations_xip().
 */
static void apply_relocations(uint8_t *base, uint32_t load_addr,
                               uint32_t text_size, uint32_t load_size,
                               const uint32_t *relocs, uint32_t nrelocs)
{
    (void)load_size;

    if (text_size == 0) {
        /* Simple: all in one segment, just add load_addr */
        for (uint32_t i = 0; i < nrelocs; i++) {
            uint32_t off = relocs[i];
            uint32_t *ptr = (uint32_t *)(base + off);
            *ptr += load_addr;
        }
    } else {
        /* Split-aware: determine which segment each value references */
        uint32_t text_base = load_addr;
        uint32_t data_base = load_addr + text_size;
        for (uint32_t i = 0; i < nrelocs; i++) {
            uint32_t off = relocs[i];
            uint32_t *ptr = (uint32_t *)(base + off);
            uint32_t val = *ptr;
            if (val < text_size)
                *ptr = val + text_base;
            else
                *ptr = (val - text_size) + data_base;
        }
    }
}

/*
 * Apply relocations with text and data at separate addresses (XIP).
 *
 * Unlike apply_relocations() which assumes text+data are contiguous in
 * memory, this function handles the case where text lives at one address
 * (e.g., banked SRAM at 0x200000) and data at another (e.g., main RAM
 * at 0xFF9000). This is the core mechanism for EverDrive Pro
 * bank-swapping: text in per-process SRAM banks, data in shared RAM.
 *
 * For each relocation:
 *   1. Locate the word to patch: if offset < text_size, it's in text_mem;
 *      otherwise it's in data_mem at (offset - text_size).
 *   2. Read the zero-based value and determine what it references:
 *      values < text_size reference text, values >= text_size reference data.
 *   3. Patch with the appropriate base address.
 *
 * text_size must be > 0 (split mode only). For contiguous loading,
 * use apply_relocations() instead.
 */
void apply_relocations_xip(uint8_t *text_mem, uint32_t text_base,
                            uint8_t *data_mem, uint32_t data_base,
                            uint32_t text_size,
                            const uint32_t *relocs, uint32_t nrelocs)
{
    for (uint32_t i = 0; i < nrelocs; i++) {
        uint32_t off = relocs[i];

        /* Locate the word to patch */
        uint32_t *ptr;
        if (off < text_size)
            ptr = (uint32_t *)(text_mem + off);
        else
            ptr = (uint32_t *)(data_mem + (off - text_size));

        /* Determine what the value references and patch */
        uint32_t val = *ptr;
        if (val < text_size)
            *ptr = val + text_base;
        else
            *ptr = (val - text_size) + data_base;
    }
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
 * Reads the binary header, validates it, loads text+data at load_addr,
 * applies relocations (adds load_addr to all absolute addresses),
 * zeros BSS, and sets up the user stack with argc/argv.
 *
 * load_addr is the base address where the binary will be loaded.
 * Callers pass USER_BASE for single-tasking; future multitasking
 * can pass different addresses for each process.
 *
 * On success, fills in *entry_out and *user_sp_out and returns 0.
 * On failure, returns negative errno.
 */
int load_binary(const char *path, const char **argv, uint32_t load_addr,
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

    /* Load text+data into user memory at load_addr */
    n = fs_read(ip, (void *)load_addr, GENIX_HDR_SIZE, hdr.load_size);
    if (n != (int)hdr.load_size) {
        fs_iput(ip);
        kprintf("[exec] Short read: got %d, expected %d\n",
                n, hdr.load_size);
        return -EIO;
    }

    /* Apply relocations if present */
    if (hdr.reloc_count > 0) {
        uint32_t reloc_bytes = hdr.reloc_count * 4;

        /* Load relocation table into BSS area (temporary).
         * Validated in exec_validate_header: bss >= reloc_bytes,
         * or effective_bss accounts for this. */
        uint8_t *reloc_buf = (uint8_t *)(load_addr + hdr.load_size);
        uint32_t reloc_off = GENIX_HDR_SIZE + hdr.load_size;
        n = fs_read(ip, reloc_buf, reloc_off, reloc_bytes);
        if (n != (int)reloc_bytes) {
            fs_iput(ip);
            kprintf("[exec] Short reloc read: got %d, expected %d\n",
                    n, reloc_bytes);
            return -EIO;
        }

        apply_relocations((uint8_t *)load_addr, load_addr,
                          hdr.text_size, hdr.load_size,
                          (const uint32_t *)reloc_buf, hdr.reloc_count);
    }

    fs_iput(ip);

    /* Zero BSS (destroys the relocation table, which is no longer needed) */
    if (hdr.bss_size > 0)
        memset((void *)(load_addr + hdr.load_size), 0, hdr.bss_size);

    /* Set up user stack at top of user region.
     * stack_top = load_addr + USER_SIZE places the stack at the same
     * position relative to the loaded binary regardless of load_addr. */
    uint32_t stack_top = load_addr + USER_SIZE;
    uint32_t user_sp = exec_setup_stack(stack_top, path, argv);

    /* Update process info */
    if (curproc) {
        curproc->mem_base = load_addr;
        curproc->mem_size = USER_SIZE;
        curproc->brk = load_addr + hdr.load_size + hdr.bss_size;
    }

    /* Entry is a 0-based offset; add load address */
    uint32_t abs_entry = load_addr + hdr.entry;

    kprintf("[exec] %s: %d bytes loaded at 0x%x, entry 0x%x, %d relocs\n",
            path, hdr.load_size, load_addr, abs_entry, hdr.reloc_count);

    *entry_out = abs_entry;
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
    int err = load_binary(path, argv, USER_BASE, &entry, &user_sp);
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
