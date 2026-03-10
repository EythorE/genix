/*
 * gfx.h — Genix userspace graphics library
 *
 * Thin wrapper around /dev/vdp ioctls for Mega Drive tile/sprite graphics.
 * Open the device with gfx_open(), manipulate tiles/palettes/sprites,
 * then close with gfx_close() to restore the text console.
 */
#ifndef _GFX_H
#define _GFX_H

#include <stdint.h>

/* VDP constants */
#define GFX_COLS        40
#define GFX_ROWS        28
#define GFX_TILE_SIZE   32  /* bytes per 8x8 4bpp tile */
#define GFX_MAX_SPRITES 80

/* Open /dev/vdp for exclusive graphics access. Returns 0 on success. */
int  gfx_open(void);

/* Close /dev/vdp and restore text console. */
void gfx_close(void);

/* Upload tile patterns to VRAM.
 * start_id: first tile index, count: number of tiles,
 * data: tile data (32 bytes per tile, 8x8 4bpp). */
int  gfx_tiles(int start_id, const uint8_t *data, int count);

/* Set nametable entries (map tiles to screen positions).
 * x,y: top-left tile position, w,h: size in tiles,
 * tiles: array of tile indices (w*h entries). */
int  gfx_map(int x, int y, int w, int h, const uint16_t *tiles);

/* Set a single nametable entry. */
int  gfx_map1(int x, int y, uint16_t tile);

/* Set palette colors.
 * pal: palette number (0-3), idx: starting color index (0-15),
 * colors: color values (0000BBB0GGG0RRR0), count: number of colors. */
int  gfx_palette(int pal, int idx, const uint16_t *colors, int count);

/* Set a sprite attribute. */
int  gfx_sprite(int id, int x, int y, uint16_t tile, int size, int link);

/* Set scroll position for a plane (0=A, 1=B). */
int  gfx_scroll(int plane, int x, int y);

/* Wait for next VBlank interrupt. */
int  gfx_vsync(void);

/* Clear the screen (nametable + sprites + scroll). */
int  gfx_cls(void);

#endif /* _GFX_H */
