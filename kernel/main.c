/*
 * Genix kernel main
 */
#include "kernel.h"

/* Forward declarations */
void builtin_shell(void);
void shell_ls(const char *cmd);
void shell_cat(const char *path);
void shell_write(const char *arg);
#ifdef AUTOTEST
static void autotest(void);
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
    proc_init();

    kputs("All subsystems initialized.\n");

    /* Don't start timer yet — test basic shell first */
    /* pal_timer_init(TIMER_HZ); */

    /* Enable interrupts */
    __asm__ volatile("move.w #0x2000, %%sr" ::: "cc");

    kputs("System ready.\n");

    kputs("Starting shell...\n");

#ifdef AUTOTEST
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

    /* Test 3: exec nonexistent binary */
    kputs("[test] exec /bin/nonexistent: ");
    rc = do_exec("/bin/nonexistent", NULL);
    if (rc < 0) { kputs("PASS (correctly failed)\n"); pass++; }
    else { kputs("FAIL (should have failed)\n"); fail++; }

    /* Test 4: ls /bin (just exercise the path, don't check output) */
    kputs("[test] ls /bin:\n");
    shell_ls("ls /bin");
    pass++;  /* if we get here without crashing, it's a pass */

    /* Summary */
    kprintf("\n=== AUTOTEST DONE: %d passed, %d failed ===\n", pass, fail);
    if (fail > 0)
        kputs("AUTOTEST FAILED\n");
    else
        kputs("AUTOTEST PASSED\n");
}
#endif

/* Built-in emergency shell when no filesystem */
void builtin_shell(void)
{
    char line[256];
    int pos;

    for (;;) {
        kputs("genix> ");
        pos = 0;
        for (;;) {
            int c = kgetc();
            if (c == '\r' || c == '\n') {
                kputc('\n');
                break;
            }
            if (c == 0x7f || c == '\b') {
                if (pos > 0) {
                    pos--;
                    kputs("\b \b");
                }
                continue;
            }
            if (c == 3) {  /* Ctrl-C */
                kputc('\n');
                pos = 0;
                break;
            }
            if (pos < 255) {
                line[pos++] = c;
                kputc(c);
            }
        }
        line[pos] = '\0';

        if (pos == 0)
            continue;

        /* Built-in commands */
        if (strcmp(line, "help") == 0) {
            kputs("Commands: help, mem, ls, cat <file>, echo <text>, "
                  "exec <file>, halt\n");
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
        } else if (strncmp(line, "exec ", 5) == 0) {
            if (do_exec(line + 5, NULL) < 0)
                kputs("exec failed\n");
        } else if (strncmp(line, "write ", 6) == 0) {
            shell_write(line + 6);
        } else if (strncmp(line, "mkdir ", 6) == 0) {
            if (fs_mkdir(line + 6) < 0)
                kputs("mkdir failed\n");
        } else {
            kputs("Unknown command. Type 'help'.\n");
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
