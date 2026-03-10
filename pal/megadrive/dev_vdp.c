/*
 * VDP device driver — Mega Drive implementation
 *
 * Provides /dev/vdp with ioctl-based access to the VDP hardware.
 * Exclusive open: only one process may hold the device at a time.
 *
 * Overrides the weak stubs in kernel/dev.c.
 */
#include "../../kernel/kernel.h"
#include "../../kernel/dev_vdp.h"

/* VDP hardware ports */
#define VDP_DATA_PORT    (*(volatile uint16_t *)0xC00000)
#define VDP_DATA32_PORT  (*(volatile uint32_t *)0xC00000)
#define VDP_CTRL_PORT    (*(volatile uint32_t *)0xC00004)
#define VDP_CTRL16_PORT  (*(volatile uint16_t *)0xC00004)
#define VDP_HVCNT_PORT   (*(volatile uint16_t *)0xC00008)

/* VDP command words for VRAM/CRAM/VSRAM access */
#define VDP_VRAM_WRITE   0x40000000
#define VDP_CRAM_WRITE   0xC0000000
#define VDP_VSRAM_WRITE  0x40000010

/* VRAM layout (must match vdp.S / devvt.S) */
#define VRAM_PLANE_A     0xC000  /* Plane A nametable */
#define VRAM_PLANE_B     0xE000  /* Plane B nametable (debug overlay) */
#define VRAM_SPRITES     0xF000  /* Sprite attribute table */
#define VRAM_USER_TILES  0x1000  /* User tile area (tile 128+) */
/* User tile 0 maps to VDP tile 128 (VRAM_USER_TILES / VDP_TILE_SIZE) */
#define USER_TILE_OFFSET (VRAM_USER_TILES / VDP_TILE_SIZE)

/* Nametable row stride: 64 tiles * 2 bytes per entry = 128 bytes */
#define NAMETABLE_STRIDE 128

/* Exclusive ownership */
static uint8_t vdp_owner = 0;  /* pid of owning process, 0 = free */

/* Build a VDP write command for a given address type and offset */
static uint32_t vdp_write_cmd(uint32_t base_cmd, uint16_t offset)
{
    /* VDP address encoding: bits 13-0 go into command word
     * CD1 CD0 A13..A0 -> rearranged into the 32-bit command
     * base_cmd has the CD bits set, we add the address */
    uint32_t addr = offset;
    return base_cmd | ((addr & 0x3FFF) << 16) | ((addr >> 14) & 3);
}

/* Wait for VBlank by polling VDP status register */
static void wait_vblank(void)
{
    /* Bit 3 of VDP status = VBlank flag.
     * Wait for it to go high (entering VBlank). */
    while (!(VDP_CTRL16_PORT & 0x0008))
        ;
}

/* External assembly routines from vdp.S */
extern void VDP_reinit(void);
extern void VDP_clear(void);

int vdp_open(int minor)
{
    (void)minor;

    /* Exclusive access — only one process at a time */
    if (vdp_owner != 0)
        return -EBUSY;

    if (curproc)
        vdp_owner = curproc->pid;
    else
        vdp_owner = 1;  /* kernel context */

    return 0;
}

int vdp_close(int minor)
{
    (void)minor;

    /* Restore text console on close */
    VDP_reinit();
    vdp_owner = 0;

    return 0;
}

int vdp_read(int minor, void *buf, int len)
{
    (void)minor; (void)buf; (void)len;
    return -EINVAL;  /* VDP is ioctl-only */
}

int vdp_write(int minor, const void *buf, int len)
{
    (void)minor; (void)buf; (void)len;
    return -EINVAL;  /* VDP is ioctl-only */
}

static int vdp_do_loadtiles(void *arg)
{
    struct vdp_tiles_arg *a = (struct vdp_tiles_arg *)arg;
    if (!a->data || a->count == 0)
        return -EINVAL;

    /* Each tile is 32 bytes. Offset by VRAM_USER_TILES so user tiles
     * don't overwrite the console font area (VRAM 0x0000-0x0FFF). */
    uint16_t vram_addr = VRAM_USER_TILES + a->start_id * VDP_TILE_SIZE;
    VDP_CTRL_PORT = vdp_write_cmd(VDP_VRAM_WRITE, vram_addr);

    /* Write tile data as longwords for speed.
     * VDP_TILE_SIZE (32) / 4 = 8 longs per tile */
    const uint32_t *src = (const uint32_t *)a->data;
    /* count * 32 bytes / 4 = count * 8 longs */
    int nlongs = a->count * 8;  /* 8 longs per tile, DIVU.W safe: 8 fits in 16 bits */
    for (int i = 0; i < nlongs; i++)
        VDP_DATA32_PORT = src[i];

    return 0;
}

static int vdp_do_setmap(void *arg)
{
    struct vdp_map_arg *a = (struct vdp_map_arg *)arg;
    if (!a->tiles)
        return -EINVAL;
    if (a->x + a->w > VDP_COLS || a->y + a->h > VDP_ROWS)
        return -EINVAL;

    const uint16_t *src = a->tiles;
    for (int row = 0; row < a->h; row++) {
        /* Plane A nametable address for row (y+row), column x.
         * Each entry is 2 bytes, row stride is 128 bytes (64 tiles). */
        uint16_t addr = VRAM_PLANE_A +
                        (uint16_t)(a->y + row) * NAMETABLE_STRIDE +
                        a->x * 2;
        VDP_CTRL_PORT = vdp_write_cmd(VDP_VRAM_WRITE, addr);
        for (int col = 0; col < a->w; col++) {
            /* Offset user tile IDs into the user tile area (VDP tile 128+)
             * so they don't collide with console font tiles (0-127). */
            uint16_t entry = *src++;
            VDP_DATA_PORT = entry + USER_TILE_OFFSET;
        }
    }
    return 0;
}

static int vdp_do_setpal(void *arg)
{
    struct vdp_pal_arg *a = (struct vdp_pal_arg *)arg;
    if (!a->colors)
        return -EINVAL;
    if (a->palette >= VDP_PALETTES || a->index + a->count > VDP_PAL_COLORS)
        return -EINVAL;

    /* CRAM offset: palette * 32 + index * 2 */
    uint16_t cram_offset = a->palette * 32 + a->index * 2;
    VDP_CTRL_PORT = vdp_write_cmd(VDP_CRAM_WRITE, cram_offset);

    for (int i = 0; i < a->count; i++)
        VDP_DATA_PORT = a->colors[i];

    return 0;
}

static int vdp_do_setsprite(void *arg)
{
    struct vdp_sprite_arg *a = (struct vdp_sprite_arg *)arg;
    if (a->id >= VDP_MAX_SPRITES)
        return -EINVAL;

    /* Sprite attribute table: 8 bytes per sprite at VRAM_SPRITES.
     * Format: word0 = Y, word1 = size/link, word2 = tile, word3 = X */
    uint16_t addr = VRAM_SPRITES + a->id * 8;
    VDP_CTRL_PORT = vdp_write_cmd(VDP_VRAM_WRITE, addr);
    VDP_DATA_PORT = (uint16_t)(a->y + 128);                   /* Y pos */
    VDP_DATA_PORT = (a->size << 8) | (a->link & 0x7F);        /* size + link */
    VDP_DATA_PORT = a->tile;                                    /* tile + flags */
    VDP_DATA_PORT = (uint16_t)(a->x + 128);                   /* X pos */

    return 0;
}

static int vdp_do_scroll(void *arg)
{
    struct vdp_scroll_arg *a = (struct vdp_scroll_arg *)arg;
    if (a->plane > 1)
        return -EINVAL;

    /* Vertical scroll via VSRAM */
    uint16_t vsram_offset = a->plane * 2;
    VDP_CTRL_PORT = vdp_write_cmd(VDP_VSRAM_WRITE, vsram_offset);
    VDP_DATA_PORT = (uint16_t)a->y;

    /* Horizontal scroll via H-scroll table in VRAM (at 0xFC00).
     * Full-screen scroll mode: one entry per plane.
     * Plane A at offset 0, Plane B at offset 2. */
    uint16_t hscroll_addr = 0xFC00 + a->plane * 2;
    VDP_CTRL_PORT = vdp_write_cmd(VDP_VRAM_WRITE, hscroll_addr);
    VDP_DATA_PORT = (uint16_t)(-a->x);  /* HScroll is negated */

    return 0;
}

static int vdp_do_clear(void)
{
    /* Clear Plane A nametable */
    VDP_CTRL_PORT = vdp_write_cmd(VDP_VRAM_WRITE, VRAM_PLANE_A);
    for (int i = 0; i < 64 * 32; i++)  /* 64x32 tile nametable */
        VDP_DATA_PORT = 0;

    /* Clear sprite table (hide all sprites by setting Y=0, link=0) */
    VDP_CTRL_PORT = vdp_write_cmd(VDP_VRAM_WRITE, VRAM_SPRITES);
    for (int i = 0; i < VDP_MAX_SPRITES * 4; i++)  /* 4 words per sprite */
        VDP_DATA_PORT = 0;

    /* Reset scroll */
    VDP_CTRL_PORT = vdp_write_cmd(VDP_VSRAM_WRITE, 0);
    VDP_DATA_PORT = 0;
    VDP_DATA_PORT = 0;  /* both planes */

    VDP_CTRL_PORT = vdp_write_cmd(VDP_VRAM_WRITE, 0xFC00);
    VDP_DATA_PORT = 0;
    VDP_DATA_PORT = 0;  /* both planes */

    return 0;
}

int vdp_ioctl(int minor, int cmd, void *arg)
{
    (void)minor;

    switch (cmd) {
    case VDP_IOC_LOADTILES:
        return vdp_do_loadtiles(arg);
    case VDP_IOC_SETMAP:
        return vdp_do_setmap(arg);
    case VDP_IOC_SETPAL:
        return vdp_do_setpal(arg);
    case VDP_IOC_SETSPRITE:
        return vdp_do_setsprite(arg);
    case VDP_IOC_SCROLL:
        return vdp_do_scroll(arg);
    case VDP_IOC_WAITVBLANK:
        wait_vblank();
        return 0;
    case VDP_IOC_CLEAR:
        return vdp_do_clear();
    default:
        return -EINVAL;
    }
}
