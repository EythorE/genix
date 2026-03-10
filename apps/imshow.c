/*
 * imshow — display a tile image on the VDP
 *
 * First graphics application for Genix. Validates the full graphics
 * stack: /dev/vdp kernel driver, ioctl interface, and libgfx.
 *
 * Displays a color bar test pattern using 16 colors and fills the
 * screen with tiles. Press any key to exit and restore the console.
 *
 * Ported from the FUZIX Mega Drive imshow concept.
 *
 * Options:
 *   -n   No-wait mode: display for ~2 seconds then exit (for automated tests)
 */
#include <gfx.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* 16-color test palette (Mega Drive format: 0000BBB0GGG0RRR0) */
static const uint16_t test_palette[16] = {
    0x0000,  /*  0: black (transparent) */
    0x000E,  /*  1: red */
    0x00E0,  /*  2: green */
    0x00EE,  /*  3: yellow */
    0x0E00,  /*  4: blue */
    0x0E0E,  /*  5: magenta */
    0x0EE0,  /*  6: cyan */
    0x0EEE,  /*  7: white */
    0x0444,  /*  8: dark grey */
    0x0008,  /*  9: dark red */
    0x0080,  /* 10: dark green */
    0x0088,  /* 11: dark yellow */
    0x0800,  /* 12: dark blue */
    0x0808,  /* 13: dark magenta */
    0x0880,  /* 14: dark cyan */
    0x0AAA,  /* 15: light grey */
};

/*
 * Generate a solid-color 8x8 tile (32 bytes, 4bpp).
 * Each pixel row is 4 bytes (8 pixels * 4 bits).
 * Color index is repeated in every nibble.
 */
static void make_solid_tile(uint8_t *out, int color)
{
    /* Each byte has two pixels: high nibble + low nibble.
     * 4 bytes per row, 8 rows per tile = 32 bytes total. */
    uint8_t byte = (uint8_t)((color << 4) | color);
    for (int i = 0; i < GFX_TILE_SIZE; i++)
        out[i] = byte;
}

/*
 * Generate a checkerboard tile (two colors alternating).
 */
static void make_checker_tile(uint8_t *out, int c1, int c2)
{
    uint8_t even_byte = (uint8_t)((c1 << 4) | c2);
    uint8_t odd_byte  = (uint8_t)((c2 << 4) | c1);
    for (int row = 0; row < 8; row++) {
        uint8_t b = (row & 1) ? odd_byte : even_byte;
        int off = row * 4;
        out[off]     = b;
        out[off + 1] = b;
        out[off + 2] = b;
        out[off + 3] = b;
    }
}

/*
 * Generate a gradient tile (horizontal stripes of varying intensity).
 */
static void make_gradient_tile(uint8_t *out, int base_color)
{
    for (int row = 0; row < 8; row++) {
        /* Use different color indices for each row */
        int c = (base_color + row) & 0x0F;
        uint8_t byte = (uint8_t)((c << 4) | c);
        int off = row * 4;
        out[off]     = byte;
        out[off + 1] = byte;
        out[off + 2] = byte;
        out[off + 3] = byte;
    }
}

int main(int argc, char **argv)
{
    int nowait = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0)
            nowait = 1;
    }

    int r = gfx_open();
    if (r < 0) {
        printf("imshow: cannot open /dev/vdp (err %d)\n", r);
        printf("(VDP device only available on Mega Drive)\n");
        return 1;
    }

    /* Load palette 0 with test colors */
    gfx_palette(0, 0, test_palette, 16);

    /* Generate and upload tiles:
     *   Tile 0: transparent/black (already zeroed by gfx_cls)
     *   Tiles 1-15: solid color tiles for each palette entry
     *   Tile 16: checkerboard (white/black)
     *   Tile 17: checkerboard (red/blue)
     *   Tile 18: gradient */
    uint8_t tile_buf[GFX_TILE_SIZE];
    int tile_id;

    /* Tiles 1-15: solid colors */
    for (tile_id = 1; tile_id < 16; tile_id++) {
        make_solid_tile(tile_buf, tile_id);
        gfx_tiles(tile_id, tile_buf, 1);
    }

    /* Tile 16: white/black checkerboard */
    make_checker_tile(tile_buf, 7, 0);
    gfx_tiles(16, tile_buf, 1);

    /* Tile 17: red/blue checkerboard */
    make_checker_tile(tile_buf, 1, 4);
    gfx_tiles(17, tile_buf, 1);

    /* Tile 18: gradient */
    make_gradient_tile(tile_buf, 0);
    gfx_tiles(18, tile_buf, 1);

    /* Fill screen with color bar pattern:
     * - Top section (rows 0-3): solid color bars (colors 1-15)
     * - Middle section (rows 4-23): color bars with gradient
     * - Bottom section (rows 24-27): checkerboard patterns */

    /* Top: solid color bars — each bar is ~2-3 columns wide */
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < GFX_COLS; col++) {
            /* 40 cols / 15 colors = ~2.6 cols per color */
            int color = 1 + (col * 15) / GFX_COLS;
            if (color > 15) color = 15;
            gfx_map1(col, row, (uint16_t)color);
        }
    }

    /* Middle: gradient color bars */
    for (int row = 4; row < 24; row++) {
        for (int col = 0; col < GFX_COLS; col++) {
            int color = 1 + (col * 15) / GFX_COLS;
            if (color > 15) color = 15;
            /* Alternate between solid and gradient for visual interest */
            uint16_t tile = (uint16_t)color;
            if ((row & 3) == 0)
                tile = 16;  /* checkerboard stripe every 4 rows */
            gfx_map1(col, row, tile);
        }
    }

    /* Bottom: alternating checkerboard patterns */
    for (int row = 24; row < GFX_ROWS; row++) {
        for (int col = 0; col < GFX_COLS; col++) {
            uint16_t tile = (col & 1) ? 16 : 17;
            if (row >= 26)
                tile = 18;  /* gradient tiles at very bottom */
            gfx_map1(col, row, tile);
        }
    }

    /* Sync to ensure display is updated */
    gfx_vsync();

    if (nowait) {
        /* Automated test mode: hold the display for ~2 seconds (120 frames
         * at 60 fps) so BlastEm renders enough frames for a screenshot. */
        for (int f = 0; f < 120; f++)
            gfx_vsync();
    } else {
        /* Interactive mode: wait for keypress */
        read(0, tile_buf, 1);
    }

    /* Close VDP — restores text console */
    gfx_close();

    return 0;
}
