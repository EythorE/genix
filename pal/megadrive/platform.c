/*
 * Mega Drive Platform Abstraction Layer
 *
 * VDP (video display processor) for console output
 * Saturn keyboard for input
 * ROM/RAM disk for storage
 */
#include "../../kernel/kernel.h"
#include "font8x8.h"

/* VDP registers */
#define VDP_DATA     (*(volatile uint16_t *)0xC00000)
#define VDP_CTRL     (*(volatile uint16_t *)0xC00004)
#define VDP_CTRL32   (*(volatile uint32_t *)0xC00004)
#define VDP_HVCNT    (*(volatile uint16_t *)0xC00008)

/* VDP commands */
#define VDP_VRAM_WRITE(addr)  (0x40000000 | (((addr) & 0x3FFF) << 16) | (((addr) >> 14) & 3))
#define VDP_CRAM_WRITE(addr)  (0xC0000000 | (((addr) & 0x3FFF) << 16) | (((addr) >> 14) & 3))
#define VDP_VSRAM_WRITE(addr) (0x40000010 | (((addr) & 0x3FFF) << 16) | (((addr) >> 14) & 3))

/* Controller ports */
#define IO_DATA1    (*(volatile uint8_t *)0xA10003)
#define IO_DATA2    (*(volatile uint8_t *)0xA10005)
#define IO_CTRL1    (*(volatile uint8_t *)0xA10009)
#define IO_CTRL2    (*(volatile uint8_t *)0xA1000B)

/* Z80 control */
#define Z80_BUSREQ  (*(volatile uint16_t *)0xA11100)
#define Z80_RESET   (*(volatile uint16_t *)0xA11200)

/* Console state */
static int cursor_x = 0;
static int cursor_y = 0;
#define COLS 40
#define ROWS 28
#define PLANE_A_ADDR 0xC000  /* VRAM address of plane A nametable */

/* Tick counter — incremented by VBlank interrupt */
static volatile uint32_t md_ticks = 0;

/* SRAM disk: battery-backed at 0x200000 */
#define SRAM_BASE   0x200000
#define SRAM_SIZE   0x10000   /* 64KB typical */

/* ROM disk: at a known offset in the ROM */
extern char _rom_disk_start;
extern char _rom_disk_end;

static void vdp_set_reg(int reg, uint8_t val)
{
    VDP_CTRL = 0x8000 | (reg << 8) | val;
}

static void scroll_up(void)
{
    /* Move rows 1..ROWS-1 up to 0..ROWS-2 */
    for (int y = 0; y < ROWS - 1; y++) {
        uint32_t src_addr = PLANE_A_ADDR + (y + 1) * COLS * 2;
        uint32_t dst_addr = PLANE_A_ADDR + y * COLS * 2;

        /* Read from source */
        VDP_CTRL32 = 0x40000000 | ((src_addr & 0x3FFF) << 16) | ((src_addr >> 14) & 3);
        uint16_t row[COLS];
        for (int x = 0; x < COLS; x++)
            row[x] = VDP_DATA;

        /* Write to dest */
        VDP_CTRL32 = VDP_VRAM_WRITE(dst_addr);
        for (int x = 0; x < COLS; x++)
            VDP_DATA = row[x];
    }

    /* Clear last row */
    uint32_t last_addr = PLANE_A_ADDR + (ROWS - 1) * COLS * 2;
    VDP_CTRL32 = VDP_VRAM_WRITE(last_addr);
    for (int x = 0; x < COLS; x++)
        VDP_DATA = 0;
}

void pal_init(void)
{
    /* TMSS handshake already done in crt0.S */

    /* Z80: request bus, hold reset */
    Z80_BUSREQ = 0x0100;
    Z80_RESET = 0x0100;
    while (Z80_BUSREQ & 0x0100)
        ;

    /* VDP init */
    vdp_set_reg(0x00, 0x04);  /* Hint off */
    vdp_set_reg(0x01, 0x44);  /* Enable display, Vint on */
    vdp_set_reg(0x02, 0x30);  /* Plane A at 0xC000 */
    vdp_set_reg(0x03, 0x2C);  /* Window at 0xB000 */
    vdp_set_reg(0x04, 0x07);  /* Plane B at 0xE000 */
    vdp_set_reg(0x05, 0x54);  /* Sprite table at 0xA800 */
    vdp_set_reg(0x07, 0x00);  /* Background color: palette 0, color 0 */
    vdp_set_reg(0x0A, 0xFF);  /* Hint counter */
    vdp_set_reg(0x0B, 0x00);  /* Full screen scroll */
    vdp_set_reg(0x0C, 0x81);  /* 40-cell mode, no interlace */
    vdp_set_reg(0x0D, 0x34);  /* HScroll table at 0x6800 */
    vdp_set_reg(0x0F, 0x02);  /* Auto-increment 2 */
    vdp_set_reg(0x10, 0x01);  /* Scroll size: 64x32 */
    vdp_set_reg(0x11, 0x00);  /* Window H pos */
    vdp_set_reg(0x12, 0x00);  /* Window V pos */

    /* Clear VRAM */
    VDP_CTRL32 = VDP_VRAM_WRITE(0);
    for (int i = 0; i < 32768; i++)
        VDP_DATA = 0;

    /* Load a basic color palette */
    VDP_CTRL32 = VDP_CRAM_WRITE(0);
    VDP_DATA = 0x0000;  /* Black */
    VDP_DATA = 0x0EEE;  /* White */

    /* Load font tiles into VRAM.
     * Tiles 0x20-0x7F (96 glyphs), each tile is 32 bytes in 4bpp.
     * Font starts at tile 0x20 * 32 = 0x400 in VRAM. */
    for (int ch = 0; ch < 96; ch++) {
        uint32_t tile_addr = (ch + 0x20) * 32;
        VDP_CTRL32 = VDP_VRAM_WRITE(tile_addr);
        for (int row = 0; row < 8; row++) {
            unsigned char bits = font_8x8[ch][row];
            /* Expand 1bpp to 4bpp: each pixel is a nibble (color index 1) */
            uint32_t pixels = 0;
            for (int px = 0; px < 8; px++) {
                if (bits & (0x80 >> px))
                    pixels |= (uint32_t)1 << ((7 - px) * 4);
            }
            VDP_DATA = (uint16_t)(pixels >> 16);
            VDP_DATA = (uint16_t)(pixels & 0xFFFF);
        }
    }

    cursor_x = 0;
    cursor_y = 0;

    /* Enable SRAM if present */
    volatile uint8_t *sram_reg = (volatile uint8_t *)0xA130F1;
    *sram_reg = 0x03;  /* Enable SRAM, write-enable */
}

void pal_console_putc(char c)
{
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        if (cursor_x > 0) cursor_x--;
    } else {
        /* Write tile index to plane A */
        uint32_t addr = PLANE_A_ADDR + (cursor_y * COLS + cursor_x) * 2;
        VDP_CTRL32 = VDP_VRAM_WRITE(addr);
        VDP_DATA = (uint16_t)c;  /* Tile index = ASCII value (font loaded accordingly) */
        cursor_x++;
    }

    if (cursor_x >= COLS) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= ROWS) {
        scroll_up();
        cursor_y = ROWS - 1;
    }
}

/* Saturn keyboard via controller port 2 — simplified polling */
int pal_console_getc(void)
{
    /* Poll controller port for keyboard input */
    while (!pal_console_ready())
        ;

    /* TODO: implement Saturn keyboard protocol
     * For now, use a simpler polling approach */
    IO_CTRL2 = 0x60;
    IO_DATA2 = 0x60;

    uint8_t hi = IO_DATA2 & 0x0F;
    IO_DATA2 = 0x20;
    uint8_t lo = IO_DATA2 & 0x0F;

    return (hi << 4) | lo;
}

int pal_console_ready(void)
{
    /* Check if keyboard has data — simplified */
    IO_CTRL2 = 0x60;
    IO_DATA2 = 0x60;
    /* If TH=1 and controller responds, there may be data */
    return (IO_DATA2 & 0x0F) != 0x0F;
}

void pal_disk_read(int dev, uint32_t block, void *buf)
{
    if (dev == 0) {
        /* ROM disk — read from ROM */
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
    if (dev == 0) {
        /* ROM disk — read-only, ignore writes */
        return;
    }
    /* SRAM disk */
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
    /* Mega Drive main RAM: 0xFF0000 - 0xFFFFFF (64KB) */
    return 0xFFFFF0;
}

void pal_timer_init(int hz)
{
    (void)hz;
    /* Timer uses VBlank interrupt (60Hz NTSC / 50Hz PAL) — already enabled */
}

uint32_t pal_timer_ticks(void)
{
    return md_ticks;
}

/* Called from VBlank interrupt handler */
void md_vblank_handler(void)
{
    md_ticks++;
}
