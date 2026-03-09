/*
 * VDP (Video Display Processor) device driver interface
 *
 * Shared between kernel and userspace (via libc/include/gfx.h).
 * Defines ioctl commands and argument structures for /dev/vdp.
 *
 * The VDP device provides exclusive access to the Mega Drive's
 * tile/sprite graphics hardware. Only one process may hold it open.
 */
#ifndef DEV_VDP_H
#define DEV_VDP_H

#include <stdint.h>

/* ioctl commands for /dev/vdp */
#define VDP_IOC_LOADTILES   0x5600  /* Upload tile patterns to VRAM */
#define VDP_IOC_SETMAP      0x5601  /* Write nametable entries */
#define VDP_IOC_SETPAL      0x5602  /* Write palette colors to CRAM */
#define VDP_IOC_SETSPRITE   0x5603  /* Write sprite attributes */
#define VDP_IOC_SCROLL      0x5604  /* Set scroll registers */
#define VDP_IOC_WAITVBLANK  0x5605  /* Wait for next VBlank */
#define VDP_IOC_CLEAR       0x5606  /* Clear nametable + sprites */

/* VDP_IOC_LOADTILES argument */
struct vdp_tiles_arg {
    uint16_t start_id;      /* starting tile index in VRAM */
    uint16_t count;         /* number of tiles to upload */
    const uint8_t *data;    /* tile data: 32 bytes per tile (8x8, 4bpp) */
};

/* VDP_IOC_SETMAP argument */
struct vdp_map_arg {
    uint16_t x;             /* tile column (0-39) */
    uint16_t y;             /* tile row (0-27) */
    uint16_t w;             /* width in tiles */
    uint16_t h;             /* height in tiles */
    const uint16_t *tiles;  /* tile indices (w*h entries) */
};

/* VDP_IOC_SETPAL argument */
struct vdp_pal_arg {
    uint16_t palette;       /* palette number (0-3) */
    uint16_t index;         /* starting color index (0-15) */
    uint16_t count;         /* number of colors to set */
    uint16_t pad;           /* alignment padding */
    const uint16_t *colors; /* colors in 0000BBB0GGG0RRR0 format */
};

/* VDP_IOC_SETSPRITE argument */
struct vdp_sprite_arg {
    uint16_t id;            /* sprite index (0-79) */
    int16_t  x;             /* X position (screen coords + 128) */
    int16_t  y;             /* Y position (screen coords + 128) */
    uint16_t tile;          /* tile index + flags (priority, palette, flip) */
    uint16_t size;          /* sprite size (0=8x8, 5=16x16, etc.) */
    uint16_t link;          /* next sprite in chain (0=end) */
};

/* VDP_IOC_SCROLL argument */
struct vdp_scroll_arg {
    uint16_t plane;         /* 0 = Plane A, 1 = Plane B */
    int16_t  x;             /* horizontal scroll (pixels) */
    int16_t  y;             /* vertical scroll (pixels) */
    uint16_t pad;           /* alignment padding */
};

/* VDP constants */
#define VDP_COLS        40  /* tiles per row (H40 mode) */
#define VDP_ROWS        28  /* tiles per column (V28 mode) */
#define VDP_MAX_SPRITES 80
#define VDP_TILE_SIZE   32  /* bytes per 8x8 4bpp tile */
#define VDP_PALETTES    4
#define VDP_PAL_COLORS  16

#endif /* DEV_VDP_H */
