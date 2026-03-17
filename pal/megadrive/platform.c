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

/* Graphics mode flag — suppress console output when user owns VDP */
static int vdp_graphics_mode = 0;

void pal_vdp_set_graphics_mode(int on)
{
    vdp_graphics_mode = on;
}

/* ============================================================
 * ANSI escape sequence parser state
 * ============================================================ */
#define ESC_NORMAL   0
#define ESC_SEEN     1    /* received ESC */
#define ESC_CSI      2    /* received ESC[ */
#define ESC_CSI_Q    3    /* received ESC[? (private mode) */

static uint8_t  esc_state = ESC_NORMAL;
static uint8_t  esc_params[4];    /* up to 4 numeric params */
static uint8_t  esc_nparam = 0;   /* number of params collected */
static uint8_t  esc_partial = 0;  /* partial number being accumulated */
static uint8_t  esc_has_digit = 0;/* whether current param has digits */
static uint16_t current_attr = 0; /* palette bits for plot_char (bits 13-12) */
static uint8_t  saved_x = 0, saved_y = 0;
static uint8_t  cursor_visible = 1;

/* Get param with default value */
static int esc_param(int idx, int def)
{
    return (idx < esc_nparam && esc_params[idx] > 0) ?
           esc_params[idx] : def;
}

static void esc_reset(void)
{
    esc_state = ESC_NORMAL;
    esc_nparam = 0;
    esc_partial = 0;
    esc_has_digit = 0;
}

/* Clamp cursor to screen bounds */
static void cursor_clamp(void)
{
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_x >= COLS) cursor_x = COLS - 1;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_y >= ROWS) cursor_y = ROWS - 1;
}

/* Handle SGR (Set Graphic Rendition) — ESC[...m */
static void handle_sgr(void)
{
    /* If no params, treat as reset (SGR 0) */
    if (esc_nparam == 0) {
        current_attr = 0;
        return;
    }
    for (int i = 0; i < esc_nparam; i++) {
        int p = esc_params[i];
        switch (p) {
        case 0:  current_attr = 0; break;               /* reset */
        case 1:  current_attr = (3u << 13); break;       /* bold → palette 3 */
        case 7:  /* reverse video — swap fg/bg concept (limited) */
                 break;
        case 22: current_attr = 0; break;               /* normal intensity */
        case 27: break;                                 /* cancel reverse */
        case 30: case 31: case 32: case 33:             /* fg colors */
        case 34: case 35: case 36: case 37:
                 break;  /* V3b: will map to palette+tile range */
        case 39: current_attr = 0; break;               /* default fg */
        case 40: case 41: case 42: case 43:             /* bg colors */
        case 44: case 45: case 46: case 47:
                 break;  /* V3b: background colors */
        case 49: break;                                 /* default bg */
        case 90: case 91: case 92: case 93:             /* bright fg */
        case 94: case 95: case 96: case 97:
                 current_attr = (3u << 13); break;       /* bright → palette 3 */
        }
    }
}

/* Handle CSI sequence final character */
static void handle_csi(char cmd)
{
    int p1, p2;

    switch (cmd) {
    case 'A':  /* CUU — cursor up */
        cursor_y -= esc_param(0, 1);
        cursor_clamp();
        break;
    case 'B':  /* CUD — cursor down */
        cursor_y += esc_param(0, 1);
        cursor_clamp();
        break;
    case 'C':  /* CUF — cursor forward */
        cursor_x += esc_param(0, 1);
        cursor_clamp();
        break;
    case 'D':  /* CUB — cursor back */
        cursor_x -= esc_param(0, 1);
        cursor_clamp();
        break;
    case 'H':  /* CUP — cursor position */
    case 'f':  /* HVP — same as CUP */
        p1 = esc_param(0, 1);
        p2 = esc_param(1, 1);
        cursor_y = p1 - 1;  /* ANSI is 1-based */
        cursor_x = p2 - 1;
        cursor_clamp();
        break;
    case 'J':  /* ED — erase display */
        p1 = esc_param(0, 0);
        if (p1 == 0) {
            /* Clear from cursor to end of screen */
            clear_across(cursor_y, cursor_x, COLS - cursor_x);
            if (cursor_y + 1 < ROWS)
                clear_lines(cursor_y + 1, ROWS - cursor_y - 1);
        } else if (p1 == 1) {
            /* Clear from start to cursor */
            if (cursor_y > 0)
                clear_lines(0, cursor_y);
            clear_across(cursor_y, 0, cursor_x + 1);
        } else if (p1 == 2) {
            /* Clear entire screen */
            clear_lines(0, ROWS);
            cursor_x = 0;
            cursor_y = 0;
        }
        break;
    case 'K':  /* EL — erase line */
        p1 = esc_param(0, 0);
        if (p1 == 0) {
            /* Clear from cursor to end of line */
            clear_across(cursor_y, cursor_x, COLS - cursor_x);
        } else if (p1 == 1) {
            /* Clear from start of line to cursor */
            clear_across(cursor_y, 0, cursor_x + 1);
        } else if (p1 == 2) {
            /* Clear entire line */
            clear_across(cursor_y, 0, COLS);
        }
        break;
    case 'm':  /* SGR — set graphic rendition */
        handle_sgr();
        break;
    case 's':  /* SCP — save cursor position */
        saved_x = cursor_x;
        saved_y = cursor_y;
        break;
    case 'u':  /* RCP — restore cursor position */
        cursor_x = saved_x;
        cursor_y = saved_y;
        cursor_clamp();
        break;
    case 'n':  /* DSR — device status report */
        if (esc_param(0, 0) == 6) {
            /* Report cursor position: ESC[row;colR
             * Feed response into TTY input queue */
            extern void tty_inproc(int minor, uint8_t c);
            tty_inproc(0, '\033');
            tty_inproc(0, '[');
            /* Convert row+1 and col+1 to digits */
            int row = cursor_y + 1;
            int col = cursor_x + 1;
            if (row >= 10) tty_inproc(0, '0' + row / 10);
            tty_inproc(0, '0' + row % 10);
            tty_inproc(0, ';');
            if (col >= 10) tty_inproc(0, '0' + col / 10);
            tty_inproc(0, '0' + col % 10);
            tty_inproc(0, 'R');
        }
        break;
    }
}

/* Handle CSI? (private mode) sequence */
static void handle_csi_private(char cmd)
{
    if (cmd == 'h' || cmd == 'l') {
        int p = esc_param(0, 0);
        if (p == 25) {
            /* DECTCEM — show/hide cursor */
            cursor_visible = (cmd == 'h') ? 1 : 0;
            if (cursor_visible)
                cursor_on(cursor_y, cursor_x);
            else
                cursor_off();
        }
    }
}

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

int pal_console_rows(void) { return ROWS; }
int pal_console_cols(void) { return COLS; }

/* Output a regular character (handles control chars and printables) */
static void putc_normal(char c)
{
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            plot_char(cursor_y, cursor_x, 0);
        }
    } else {
        plot_char(cursor_y, cursor_x, (uint16_t)(unsigned char)c | current_attr);
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

void pal_console_putc(char c)
{
    if (vdp_graphics_mode)
        return;

    switch (esc_state) {
    case ESC_NORMAL:
        if (c == '\033') {
            esc_state = ESC_SEEN;
        } else {
            putc_normal(c);
        }
        break;

    case ESC_SEEN:
        if (c == '[') {
            esc_state = ESC_CSI;
            esc_nparam = 0;
            esc_partial = 0;
            esc_has_digit = 0;
            for (int i = 0; i < 4; i++)
                esc_params[i] = 0;
        } else {
            /* Not a CSI sequence — emit ESC and the character */
            esc_state = ESC_NORMAL;
            putc_normal(c);
        }
        break;

    case ESC_CSI:
        if (c == '?') {
            esc_state = ESC_CSI_Q;
        } else if (c >= '0' && c <= '9') {
            esc_partial = esc_partial * 10 + (c - '0');
            esc_has_digit = 1;
        } else if (c == ';') {
            if (esc_nparam < 4)
                esc_params[esc_nparam] = esc_partial;
            esc_nparam++;
            esc_partial = 0;
            esc_has_digit = 0;
        } else if (c >= 0x40 && c <= 0x7E) {
            /* Final character — finish collecting params */
            if (esc_has_digit || esc_nparam > 0) {
                if (esc_nparam < 4)
                    esc_params[esc_nparam] = esc_partial;
                esc_nparam++;
            }
            handle_csi(c);
            esc_reset();
        } else {
            /* Unexpected character — abort sequence */
            esc_reset();
        }
        break;

    case ESC_CSI_Q:
        if (c >= '0' && c <= '9') {
            esc_partial = esc_partial * 10 + (c - '0');
            esc_has_digit = 1;
        } else if (c >= 0x40 && c <= 0x7E) {
            if (esc_has_digit) {
                if (esc_nparam < 4)
                    esc_params[esc_nparam] = esc_partial;
                esc_nparam++;
            }
            handle_csi_private(c);
            esc_reset();
        } else {
            esc_reset();
        }
        break;
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
