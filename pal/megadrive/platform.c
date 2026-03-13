/*
 * Mega Drive Platform Abstraction Layer
 *
 * Implements the Genix PAL interface using proven FUZIX Mega Drive drivers:
 * - VDP (video display processor) via assembly routines (vdp.S, devvt.S)
 * - Saturn keyboard via keyboard.c / keyboard_read.S
 * - ROM/SRAM disk for storage
 * - VBlank-driven timer
 *
 * Driver code adapted from FUZIX platform-megadrive:
 * https://github.com/EythorE/FUZIX/tree/megadrive/Kernel/platform/platform-megadrive
 */
#include "../../kernel/kernel.h"
#include "keyboard.h"
#include "keycode.h"

/* ============================================================
 * External assembly routines (vdp.S)
 * ============================================================ */
extern void VDP_LoadRegisters(void);
extern void VDP_ClearVRAM(void);
extern void VDP_writePalette(void);
extern void VDP_fontInit(void);
extern void VDP_clear(void);
extern void VDP_reinit(void);

/* External assembly routines (devvt.S) */
extern void plot_char(int y, int x, int c);
extern void clear_across(int y, int x, int num);
extern void clear_lines(int y, int num);
extern void scroll_up(void);
extern void scroll_down(void);
extern void cursor_on(int y, int x);
extern void cursor_off(void);
extern void cursor_disable(void);

/* External assembly routines (dbg_output.S) */
extern void dbg_toggle(void);
extern void dbg_clear(void);

/* ============================================================
 * Hardware registers (for things not in assembly)
 * ============================================================ */

/* Z80 control */
#define Z80_BUSREQ  (*(volatile uint16_t *)0xA11100)
#define Z80_RESET   (*(volatile uint16_t *)0xA11200)

/* SRAM */
#define SRAM_BASE   0x200000
#define SRAM_SIZE   0x10000   /* 64KB typical */

/* ROM disk */
extern char _rom_disk_start;
extern char _rom_disk_end;

/* ============================================================
 * Console state — VT layer using assembly plot_char / scroll
 * ============================================================ */
static int cursor_x = 0;
static int cursor_y = 0;
#define COLS 40
#define ROWS 28

/* Tick counter — incremented by VBlank interrupt */
static volatile uint32_t md_ticks = 0;

/* ============================================================
 * PAL interface implementation
 * ============================================================ */

void pal_init(void)
{
    /* Z80: request bus, hold reset */
    Z80_BUSREQ = 0x0100;
    Z80_RESET = 0x0100;
    while (Z80_BUSREQ & 0x0100)
        ;

    /* Full VDP init via assembly (registers, VRAM clear, palette, font) */
    VDP_reinit();

    /* Enable SRAM if present */
    volatile uint8_t *sram_reg = (volatile uint8_t *)0xA130F1;
    *sram_reg = 0x03;  /* Enable SRAM, write-enable */

    /* Validate SRAM contents — check for minifs superblock magic.
     * SRAM is byte-accessible at odd addresses on Mega Drive:
     * byte[i] lives at SRAM_BASE + i*2 + 1. */
    {
        volatile uint8_t *sram = (volatile uint8_t *)(SRAM_BASE + 1);
        uint32_t magic = ((uint32_t)sram[0] << 24) |
                         ((uint32_t)sram[2] << 16) |
                         ((uint32_t)sram[4] << 8)  |
                         (uint32_t)sram[6];
        if (magic != 0x4D494E49) {  /* "MINI" — MINIFS_MAGIC */
            /* SRAM not initialized — zero it for fresh filesystem */
            for (uint32_t i = 0; i < SRAM_SIZE; i++)
                sram[i * 2] = 0;
        }
    }

    /* Initialize Saturn keyboard */
    keyboard_init();

    cursor_x = 0;
    cursor_y = 0;
}

void pal_console_putc(char c)
{
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        /* Advance to next 8-column tab stop */
        cursor_x = (cursor_x + 8) & ~7;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            plot_char(cursor_y, cursor_x, 0);  /* Clear the character */
        }
    } else {
        plot_char(cursor_y, cursor_x, (uint16_t)c);
        cursor_x++;
    }

    if (cursor_x >= COLS) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= ROWS) {
        scroll_up();
        clear_lines(ROWS - 1, 1);
        cursor_y = ROWS - 1;
    }
}

int pal_console_getc(void)
{
    uint8_t key;

    while (1) {
        key = keyboard_read();
        if (key != 0) {
            /* F12 toggles debug overlay */
            if (key == KEY_F12) {
                dbg_toggle();
                continue;
            }
            return (int)key;
        }
    }
}

int pal_console_ready(void)
{
    /* On Mega Drive, keyboard input is delivered via VBlank ISR
     * (pal_keyboard_poll → tty_inproc).  Returning 0 prevents
     * tty_read() from also polling keyboard_read() in its busy
     * loop, which would race with the ISR and corrupt the Saturn
     * keyboard protocol mid-read, producing ghost characters. */
    return 0;
}

void pal_disk_read(int dev, uint32_t block, void *buf)
{
    if (dev == 0) {
        /* ROM disk */
        uint8_t *src = (uint8_t *)&_rom_disk_start + block * BLOCK_SIZE;
        memcpy(buf, src, BLOCK_SIZE);
    } else {
        /* SRAM disk */
        uint32_t offset = block * BLOCK_SIZE;
        if (offset + BLOCK_SIZE > SRAM_SIZE) {
            memset(buf, 0, BLOCK_SIZE);
            return;
        }
        volatile uint8_t *sram = (volatile uint8_t *)SRAM_BASE;
        uint8_t *dst = (uint8_t *)buf;
        /* SRAM is byte-accessible at odd addresses on Mega Drive */
        for (int i = 0; i < BLOCK_SIZE; i++)
            dst[i] = sram[offset + i * 2 + 1];
    }
}

void pal_disk_write(int dev, uint32_t block, void *buf)
{
    if (dev == 0)
        return;  /* ROM disk is read-only */

    uint32_t offset = block * BLOCK_SIZE;
    if (offset + BLOCK_SIZE > SRAM_SIZE)
        return;
    volatile uint8_t *sram = (volatile uint8_t *)SRAM_BASE;
    const uint8_t *src = (const uint8_t *)buf;
    for (int i = 0; i < BLOCK_SIZE; i++)
        sram[offset + i * 2 + 1] = src[i];
}

uint32_t pal_mem_start(void)
{
    extern char _end;
    uint32_t end = (uint32_t)&_end;
    return (end + 3) & ~3;
}

uint32_t pal_mem_end(void)
{
    return 0xFFFFF0;
}

uint32_t pal_user_base(void)
{
    /* Per-process kstacks (512 bytes × 16 procs) push _end past 0xFF8000.
     * Binaries are linked at address 0 and relocated at exec() time,
     * so USER_BASE can be adjusted without rebuilding apps.
     * 0xFF9000 gives ~1.5 KB for heap and ~27.5 KB for user programs. */
    return 0xFF9000;
}

uint32_t pal_user_top(void)
{
    return 0xFFFE00;  /* 512 bytes reserved for kernel stack at top */
}

void pal_timer_init(int hz)
{
    (void)hz;
    /* Timer uses VBlank interrupt — already enabled by VDP init */
}

uint32_t pal_timer_ticks(void)
{
    return md_ticks;
}

void pal_halt(void)
{
    /* On real hardware, just stop the CPU */
    __asm__ volatile("stop #0x2700");
    for (;;)
        ;
}

/*
 * Return the memory-mapped ROM address of a file's first data byte.
 * Only works for ROM disk (dev 0) with contiguous block allocation.
 * Returns 0 if blocks are not contiguous or file is empty.
 */
uint32_t pal_rom_file_addr(struct inode *ip)
{
    if (!ip || ip->size == 0)
        return 0;

    uint16_t first_block = ip->direct[0];
    if (first_block == 0)
        return 0;

    /* Check that all blocks are contiguous.
     * BLOCK_SIZE is 1024 (power of 2): use >> 10 for division. */
    uint32_t nblocks = (ip->size + BLOCK_SIZE - 1) >> 10;

    /* Check direct blocks (up to 12) */
    uint32_t check = nblocks < 12 ? nblocks : 12;
    for (uint32_t i = 1; i < check; i++) {
        if (ip->direct[i] != first_block + i)
            return 0;  /* not contiguous */
    }

    /* Don't support indirect blocks for XIP (files > 12 KB).
     * Most apps are well under this limit. */
    if (nblocks > 12)
        return 0;

    /* BLOCK_SIZE is 1024: use << 10 for multiplication */
    return (uint32_t)&_rom_disk_start + ((uint32_t)first_block << 10);
}

/* Called from VBlank interrupt handler in crt0.S */
void md_vblank_handler(void)
{
    md_ticks++;
}

/*
 * pal_keyboard_poll — called from VBlank ISR to feed keyboard input
 * into the TTY layer via tty_inproc().
 *
 * keyboard_read() is non-blocking: returns 0 if no key is available.
 * This replaces the polling loop in tty_read() on Mega Drive — chars
 * arrive via interrupt and are queued before the reader wakes up.
 */
void pal_keyboard_poll(void)
{
    extern void tty_inproc(int minor, uint8_t c);
    uint8_t key = keyboard_read();
    if (key != 0) {
        /* F12 toggles debug overlay — don't pass to TTY */
        if (key == KEY_F12) {
            dbg_toggle();
            return;
        }
        tty_inproc(0, key);
    }
}
