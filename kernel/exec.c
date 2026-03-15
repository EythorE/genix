/*
 * Binary loader and exec() implementation
 *
 * Loads Genix relocatable flat binaries from the filesystem and executes
 * them. All binaries are linked at address 0; the loader adds the actual
 * load address to all absolute references using the relocation table.
 *
 * Three loader modes:
 *   - Synchronous (do_exec): blocks caller until program exits.
 *     Used by autotest and the shell's single-tasking "exec" command.
 *   - Async (load_binary): loads binary contiguously, returns entry/sp
 *     for caller to set up a schedulable process. Used by do_spawn.
 *   - XIP (load_binary_xip): text stays in ROM, only data+BSS in RAM.
 *     Used on Mega Drive when binaries have GENIX_FLAG_XIP set by romfix.
 *
 * The XIP relocator (apply_relocations_xip) handles split text/data
 * at separate addresses for EverDrive Pro bank-swapping.
 */
#include "kernel.h"

/* Global state for single-tasking exec (exec_enter/exec_leave) */
int exec_exit_code;
int exec_active = 0;
uint32_t exec_user_a5;  /* a5 for -msep-data, set before exec_enter */

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

    /* Check that the binary fits in a memory slot.
     * Need: load_size + bss_size + stack.
     * The relocation table is loaded into BSS temporarily, so BSS
     * must be large enough for the reloc table (or we pad if needed). */
    uint32_t stack = HDR_STACK_SIZE(hdr);
    if (stack == 0) stack = USER_STACK_DEFAULT;
    uint32_t reloc_bytes = hdr->reloc_count * 4;

    /* BSS must be at least as large as the relocation table,
     * since we load the reloc table into BSS area temporarily. */
    uint32_t effective_bss = hdr->bss_size;
    if (reloc_bytes > effective_bss)
        effective_bss = reloc_bytes;

    uint32_t total = hdr->load_size + effective_bss + stack;
    if (total > slot_size())
        return -ENOMEM;

    return 0;
}

/*
 * Validate a Genix binary header for XIP loading.
 *
 * XIP binaries execute text from ROM; only data+BSS+stack must fit in RAM.
 * Requires text_size > 0 (split text/data) and GENIX_FLAG_XIP set.
 * Returns 0 on success, negative errno on failure.
 */
int exec_validate_header_xip(const struct genix_header *hdr)
{
    if (hdr->magic != GENIX_MAGIC)
        return -ENOEXEC;

    if (hdr->load_size == 0)
        return -ENOEXEC;

    /* Entry must be within the text segment */
    if (hdr->entry >= hdr->load_size)
        return -ENOEXEC;

    /* XIP requires split text/data info */
    if (hdr->text_size == 0 || hdr->text_size > hdr->load_size)
        return -ENOEXEC;

    /* XIP flag must be set (binary was resolved by romfix) */
    if (!(hdr->flags & GENIX_FLAG_XIP))
        return -ENOEXEC;

    /* Only data+BSS+stack must fit in a memory slot (text is in ROM) */
    uint32_t data_size = hdr->load_size - hdr->text_size;
    uint32_t stack = HDR_STACK_SIZE(hdr);
    if (stack == 0) stack = USER_STACK_DEFAULT;
    uint32_t total = data_size + hdr->bss_size + stack;
    if (total > slot_size())
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
    if (text_size == 0) {
        /* Simple: all in one segment, just add load_addr */
        for (uint32_t i = 0; i < nrelocs; i++) {
            uint32_t off = relocs[i];
            if ((off & 1) || off + 4 > load_size) {
                kprintf("[reloc] bad offset 0x%x (load_size=0x%x), skipped\n",
                        off, load_size);
                continue;
            }
            uint32_t *ptr = (uint32_t *)(base + off);
            *ptr += load_addr;
        }
    } else {
        /* Split-aware: determine which segment each value references */
        uint32_t text_base = load_addr;
        uint32_t data_base = load_addr + text_size;
        for (uint32_t i = 0; i < nrelocs; i++) {
            uint32_t off = relocs[i];
            if ((off & 1) || off + 4 > load_size) {
                kprintf("[reloc] bad offset 0x%x (load_size=0x%x), skipped\n",
                        off, load_size);
                continue;
            }
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
                            uint32_t text_size, uint32_t load_size,
                            const uint32_t *relocs, uint32_t nrelocs)
{
    for (uint32_t i = 0; i < nrelocs; i++) {
        uint32_t off = relocs[i];
        if ((off & 1) || off + 4 > load_size) {
            kprintf("[reloc-xip] bad offset 0x%x (load_size=0x%x), skipped\n",
                    off, load_size);
            continue;
        }

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

    /* Zero BSS (destroys the relocation table, which is no longer needed).
     * When reloc table was larger than BSS, zero the full extent so
     * stale relocation data doesn't leak into future sbrk() regions. */
    uint32_t reloc_bytes = hdr.reloc_count * 4;  /* reloc_count * 4: small constant */
    uint32_t zero_size = hdr.bss_size > reloc_bytes ? hdr.bss_size : reloc_bytes;
    if (zero_size > 0)
        memset((void *)(load_addr + hdr.load_size), 0, zero_size);

    /* Set up user stack at top of slot */
    uint32_t stack_top = load_addr + slot_size();
    uint32_t user_sp = exec_setup_stack(stack_top, path, argv);

    /* Update process info */
    if (curproc) {
        curproc->mem_base = load_addr;
        curproc->mem_size = slot_size();
        curproc->brk = load_addr + hdr.load_size + hdr.bss_size;
        curproc->text_size = 0;  /* non-XIP: text is in RAM slot */
        curproc->data_bss = hdr.load_size + hdr.bss_size;

        /* Compute a5 for -msep-data: data region base + got_offset.
         * In the contiguous layout, data starts at load_addr + text_size.
         * got_offset is relative to the start of the data section. */
        if (HDR_HAS_GOT(&hdr))
            curproc->data_a5 = load_addr + hdr.text_size + HDR_GOT_OFFSET(&hdr);
        else
            curproc->data_a5 = 0;
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
 * Load a binary for XIP (Execute-in-Place) from ROM.
 *
 * Text stays in ROM at text_addr; only data+BSS are loaded into RAM
 * at data_addr. The binary must have GENIX_FLAG_XIP set (by romfix),
 * meaning all relocations were already resolved at build time:
 *   - text refs → absolute ROM addresses
 *   - data refs → USER_BASE addresses
 *
 * No runtime relocation is needed. This saves precious RAM on the
 * Mega Drive by keeping text in ROM (~70% of a typical binary).
 *
 * On success, fills in *entry_out and *user_sp_out and returns 0.
 * On failure, returns negative errno.
 */
int load_binary_xip(const char *path, const char **argv,
                    uint32_t text_addr, uint32_t data_addr,
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

    int err = exec_validate_header_xip(&hdr);
    if (err < 0) {
        fs_iput(ip);
        return err;
    }

    /* Data segment size (everything after text in load_size) */
    uint32_t data_size = hdr.load_size - hdr.text_size;

    /* Copy only the data segment from ROM to RAM.
     * Text stays in ROM — the CPU executes it directly.
     * Data init values are at file offset: GENIX_HDR_SIZE + text_size */
    if (data_size > 0) {
        n = fs_read(ip, (void *)data_addr,
                    GENIX_HDR_SIZE + hdr.text_size, data_size);
        if (n != (int)data_size) {
            fs_iput(ip);
            kprintf("[xip] Short data read: got %d, expected %d\n",
                    n, data_size);
            return -EIO;
        }
    }

    /* For -msep-data binaries, apply data-segment relocations at runtime.
     * romfix resolved text-segment relocs but deferred data relocs because
     * the slot base varies per process. Read the reloc table from ROM and
     * patch only data-segment entries (offset >= text_size). */
    if (HDR_HAS_GOT(&hdr) && hdr.reloc_count > 0) {
        uint32_t reloc_file_off = GENIX_HDR_SIZE + hdr.load_size;
        uint32_t reloc_bytes = hdr.reloc_count * 4;

        /* Read reloc table from the file (it's still in ROM after header+load) */
        /* Use BSS area temporarily to hold reloc table */
        uint8_t *reloc_buf = (uint8_t *)(data_addr + data_size);
        n = fs_read(ip, reloc_buf, reloc_file_off, reloc_bytes);
        if (n == (int)reloc_bytes) {
            const uint32_t *relocs = (const uint32_t *)reloc_buf;
            for (uint32_t i = 0; i < hdr.reloc_count; i++) {
                uint32_t off = relocs[i];
                /* Only apply data-segment relocations */
                if (off < hdr.text_size)
                    continue;
                if ((off & 1) || off + 4 > hdr.load_size)
                    continue;
                /* Patch in the data region */
                uint32_t *ptr = (uint32_t *)((uint8_t *)data_addr +
                                             (off - hdr.text_size));
                uint32_t val = *ptr;
                if (val < hdr.text_size)
                    *ptr = val + text_addr;       /* data ref to text → ROM */
                else
                    *ptr = (val - hdr.text_size) + data_addr; /* data ref to data → slot */
            }
        }
    }

    fs_iput(ip);

    /* Zero BSS after data (also clears the reloc table we loaded there) */
    if (hdr.bss_size > 0)
        memset((void *)(data_addr + data_size), 0, hdr.bss_size);

    /* Set up user stack at top of slot */
    uint32_t stack_top = data_addr + slot_size();
    uint32_t user_sp = exec_setup_stack(stack_top, path, argv);

    /* Update process info */
    if (curproc) {
        curproc->mem_base = data_addr;
        curproc->mem_size = slot_size();
        curproc->brk = data_addr + data_size + hdr.bss_size;
        curproc->text_size = hdr.text_size;  /* XIP: text in ROM */
        curproc->data_bss = data_size + hdr.bss_size;

        /* Compute a5 for -msep-data: data_addr + got_offset */
        if (HDR_HAS_GOT(&hdr))
            curproc->data_a5 = data_addr + HDR_GOT_OFFSET(&hdr);
        else
            curproc->data_a5 = 0;
    }

    /* Entry: text_addr + entry offset (romfix already resolved all
     * internal references, but entry in the header is still 0-based) */
    uint32_t abs_entry = text_addr + hdr.entry;

    kprintf("[xip] %s: text %d @ 0x%x, data %d @ 0x%x, entry 0x%x\n",
            path, hdr.text_size, text_addr, data_size, data_addr,
            abs_entry);

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
    int err;

    /* Allocate a temporary slot for this synchronous exec */
    int slot = slot_alloc();
    if (slot < 0)
        return -ENOMEM;

    uint32_t data_addr = slot_base(slot);
    curproc->mem_slot = slot;
    curproc->mem_base = data_addr;
    curproc->mem_size = slot_size();

    /* Try XIP first: if the file is in memory-mapped ROM and has the
     * XIP flag, execute text directly from ROM (saves precious RAM). */
    struct inode *ip = fs_namei(path);
    if (ip) {
        uint32_t rom_addr = pal_rom_file_addr(ip);
        fs_iput(ip);
        if (rom_addr) {
            err = load_binary_xip(path, argv,
                                  rom_addr + GENIX_HDR_SIZE, data_addr,
                                  &entry, &user_sp);
            if (err == 0)
                goto run;
            /* XIP failed (not XIP-flagged, or validation error) —
             * fall through to regular loading */
        }
    }

    err = load_binary(path, argv, data_addr, &entry, &user_sp);
    if (err < 0) {
        slot_free(slot);
        curproc->mem_slot = -1;
        return err;
    }

run:

    /* Vfork child doing exec: become an independent schedulable process
     * instead of running synchronously.  Set up kstack for first context
     * switch via proc_first_run, mark P_READY, and wake the parent. */
    if (curproc->ppid < MAXPROC) {
        struct proc *parent = &proctab[curproc->ppid];
        if (parent->state == P_VFORK) {
            proc_setup_kstack(curproc, entry, user_sp, curproc->data_a5);
            curproc->state = P_READY;

            uint8_t child_pid = curproc->pid;
            parent->state = P_RUNNING;
            curproc = parent;
            vfork_restore(parent->vfork_ctx, child_pid);
            /* NOT REACHED — longjmp to parent's do_vfork() */
        }
    }

    /* Synchronous exec (builtin shell, autotest — no vfork parent) */
    exec_active = 1;
    exec_user_a5 = curproc->data_a5;
    int exitcode = exec_enter(entry, user_sp, proc_kstack_top(curproc));
    exec_active = 0;

    /* Free the temporary slot */
    slot_free(slot);
    curproc->mem_slot = -1;

    kprintf("[exec] Program exited with code %d\n", exitcode);
    return exitcode;
}
