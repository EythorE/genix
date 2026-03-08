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
    /* keyboard_read() is non-blocking — returns 0 if no key.
     * But we can't peek without consuming, so just report ready
     * and let getc block. For proper readiness we'd need a
     * one-key buffer, but this is sufficient for the PAL contract. */
    return 1;
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

void pal_timer_init(int hz)
{
    (void)hz;
    /* Timer uses VBlank interrupt — already enabled by VDP init */
}

uint32_t pal_timer_ticks(void)
{
    return md_ticks;
}

/* Called from VBlank interrupt handler in crt0.S */
void md_vblank_handler(void)
{
    md_ticks++;
}
