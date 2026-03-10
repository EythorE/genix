/*
 * Unit tests for VDP device driver logic
 *
 * Tests the ioctl argument validation and VDP address encoding
 * that can be verified on the host without VDP hardware.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* Include the shared ioctl definitions */
#include "../kernel/dev_vdp.h"

/* ---- Tests for ioctl argument structure layout ---- */

static void test_struct_sizes(void)
{
    /* Verify structures are even-sized (68000 alignment requirement) */
    ASSERT_EQ(sizeof(struct vdp_tiles_arg) % 2, 0);
    ASSERT_EQ(sizeof(struct vdp_map_arg) % 2, 0);
    ASSERT_EQ(sizeof(struct vdp_pal_arg) % 2, 0);
    ASSERT_EQ(sizeof(struct vdp_sprite_arg) % 2, 0);
    ASSERT_EQ(sizeof(struct vdp_scroll_arg) % 2, 0);
}

/* ---- Tests for VDP address encoding ---- */

/* Re-implement the VDP write command encoder from dev_vdp.c */
static uint32_t vdp_write_cmd(uint32_t base_cmd, uint16_t offset)
{
    uint32_t addr = offset;
    return base_cmd | ((addr & 0x3FFF) << 16) | ((addr >> 14) & 3);
}

static void test_vram_write_cmd(void)
{
    /* VRAM write to address 0x0000 */
    uint32_t cmd = vdp_write_cmd(0x40000000, 0x0000);
    ASSERT_EQ(cmd, 0x40000000);

    /* VRAM write to address 0xC000 (Plane A nametable) */
    cmd = vdp_write_cmd(0x40000000, 0xC000);
    /* 0xC000 = 0b1100000000000000
     * bits 13-0 = 0x0000 (lower 14 bits of 0xC000 = 0x0000)
     * Wait, 0xC000 = 49152, which is 0b11_00000000000000
     * lower 14 bits: 0x0000, upper bits: 0x3
     * cmd = 0x40000000 | (0x0000 << 16) | 0x3 = 0x40000003 */
    ASSERT_EQ(cmd, 0x40000003);

    /* VRAM write to address 0x1000 (tile 128 for user tiles) */
    cmd = vdp_write_cmd(0x40000000, 0x1000);
    /* 0x1000 lower 14 bits = 0x1000, upper = 0
     * cmd = 0x40000000 | (0x1000 << 16) | 0 = 0x50000000 */
    ASSERT_EQ(cmd, 0x50000000);

    /* VRAM write to address 0xF000 (sprite table) */
    cmd = vdp_write_cmd(0x40000000, 0xF000);
    /* 0xF000 = 0b1111_000000000000
     * lower 14 bits = 0x3000, upper = 0x3
     * cmd = 0x40000000 | (0x3000 << 16) | 0x3 = 0x70000003 */
    ASSERT_EQ(cmd, 0x70000003);
}

static void test_cram_write_cmd(void)
{
    /* CRAM write to offset 0 (palette 0, color 0) */
    uint32_t cmd = vdp_write_cmd(0xC0000000, 0x0000);
    ASSERT_EQ(cmd, 0xC0000000);

    /* CRAM write to offset 32 (palette 1, color 0) */
    cmd = vdp_write_cmd(0xC0000000, 0x0020);
    /* 0x0020 lower 14 bits = 0x0020, upper = 0
     * cmd = 0xC0000000 | (0x0020 << 16) = 0xC0200000 */
    ASSERT_EQ(cmd, 0xC0200000);

    /* CRAM write to offset 64 (palette 2, color 0) */
    cmd = vdp_write_cmd(0xC0000000, 0x0040);
    ASSERT_EQ(cmd, 0xC0400000);
}

static void test_vsram_write_cmd(void)
{
    /* VSRAM write to offset 0 (Plane A vertical scroll) */
    uint32_t cmd = vdp_write_cmd(0x40000010, 0x0000);
    ASSERT_EQ(cmd, 0x40000010);

    /* VSRAM write to offset 2 (Plane B vertical scroll) */
    cmd = vdp_write_cmd(0x40000010, 0x0002);
    ASSERT_EQ(cmd, 0x40020010);
}

/* ---- Tests for ioctl command constants ---- */

static void test_ioctl_constants(void)
{
    /* Verify ioctl commands are distinct */
    ASSERT(VDP_IOC_LOADTILES != VDP_IOC_SETMAP);
    ASSERT(VDP_IOC_SETMAP != VDP_IOC_SETPAL);
    ASSERT(VDP_IOC_SETPAL != VDP_IOC_SETSPRITE);
    ASSERT(VDP_IOC_SETSPRITE != VDP_IOC_SCROLL);
    ASSERT(VDP_IOC_SCROLL != VDP_IOC_WAITVBLANK);
    ASSERT(VDP_IOC_WAITVBLANK != VDP_IOC_CLEAR);
}

/* ---- Tests for VDP constants ---- */

static void test_vdp_constants(void)
{
    ASSERT_EQ(VDP_COLS, 40);
    ASSERT_EQ(VDP_ROWS, 28);
    ASSERT_EQ(VDP_TILE_SIZE, 32);
    ASSERT_EQ(VDP_MAX_SPRITES, 80);
    ASSERT_EQ(VDP_PALETTES, 4);
    ASSERT_EQ(VDP_PAL_COLORS, 16);

    /* Total screen tiles */
    ASSERT_EQ(VDP_COLS * VDP_ROWS, 1120);

    /* Tile data for full screen */
    ASSERT_EQ(VDP_COLS * VDP_ROWS * VDP_TILE_SIZE, 35840);
}

/* ---- Tests for nametable address calculation ---- */

static void test_nametable_address(void)
{
    /* Plane A at 0xC000, each row is 128 bytes (64 tiles * 2 bytes) */
    uint16_t plane_a = 0xC000;

    /* Row 0, Col 0 */
    uint16_t addr = plane_a + 0 * 128 + 0 * 2;
    ASSERT_EQ(addr, 0xC000);

    /* Row 0, Col 39 (last column) */
    addr = plane_a + 0 * 128 + 39 * 2;
    ASSERT_EQ(addr, 0xC04E);

    /* Row 1, Col 0 */
    addr = plane_a + 1 * 128 + 0 * 2;
    ASSERT_EQ(addr, 0xC080);

    /* Row 27, Col 39 (bottom-right) */
    addr = plane_a + 27 * 128 + 39 * 2;
    ASSERT_EQ(addr, 0xCDCE);
}

/* ---- Tests for CRAM address calculation ---- */

static void test_cram_address(void)
{
    /* Palette 0, color 0 */
    uint16_t addr = 0 * 32 + 0 * 2;
    ASSERT_EQ(addr, 0);

    /* Palette 0, color 15 */
    addr = 0 * 32 + 15 * 2;
    ASSERT_EQ(addr, 30);

    /* Palette 1, color 0 */
    addr = 1 * 32 + 0 * 2;
    ASSERT_EQ(addr, 32);

    /* Palette 3, color 15 (last entry) */
    addr = 3 * 32 + 15 * 2;
    ASSERT_EQ(addr, 126);
}

/* ---- Tests for sprite attribute layout ---- */

static void test_sprite_layout(void)
{
    /* Sprite table at 0xF000, 8 bytes per sprite */
    uint16_t sat_base = 0xF000;

    /* Sprite 0 */
    ASSERT_EQ(sat_base + 0 * 8, 0xF000);

    /* Sprite 79 (last) */
    ASSERT_EQ(sat_base + 79 * 8, 0xF278);

    /* Sprite table size: 80 * 8 = 640 bytes */
    ASSERT_EQ(80 * 8, 640);
}

/* ---- Tests for argument validation logic ---- */

static void test_map_bounds(void)
{
    /* These represent the bounds checks in vdp_do_setmap */
    /* Valid: x=0,y=0,w=40,h=28 (full screen) */
    ASSERT(0 + 40 <= VDP_COLS);
    ASSERT(0 + 28 <= VDP_ROWS);

    /* Invalid: x=39,w=2 would exceed */
    ASSERT(!(39 + 2 <= VDP_COLS));

    /* Invalid: y=27,h=2 would exceed */
    ASSERT(!(27 + 2 <= VDP_ROWS));
}

static void test_palette_bounds(void)
{
    /* Valid palette range */
    ASSERT(0 < VDP_PALETTES);
    ASSERT(3 < VDP_PALETTES);
    ASSERT(!(4 < VDP_PALETTES));

    /* Valid color range */
    ASSERT(0 + 16 <= VDP_PAL_COLORS);
    ASSERT(!(1 + 16 <= VDP_PAL_COLORS));
}

/* ---- Tests for user tile VRAM offset ---- */

/*
 * Bug fix: user tiles must not overlap console font area.
 * Console font occupies VRAM 0x0000-0x0FFF (tiles 0-127).
 * User tiles start at VRAM_USER_TILES (0x1000, tile 128).
 *
 * Without the offset, gfx_tiles(1, ...) wrote to VRAM 0x0020
 * which corrupted font glyph 1.  With the offset, it writes to
 * VRAM 0x1020 (VRAM_USER_TILES + 1 * VDP_TILE_SIZE).
 */
#define VRAM_USER_TILES_TEST  0x1000
#define USER_TILE_OFFSET_TEST (VRAM_USER_TILES_TEST / VDP_TILE_SIZE)

static void test_user_tile_vram_address(void)
{
    /* User tile 0 → VRAM 0x1000 (tile 128) */
    uint16_t addr = VRAM_USER_TILES_TEST + 0 * VDP_TILE_SIZE;
    ASSERT_EQ(addr, 0x1000);

    /* User tile 1 → VRAM 0x1020 (tile 129) */
    addr = VRAM_USER_TILES_TEST + 1 * VDP_TILE_SIZE;
    ASSERT_EQ(addr, 0x1020);

    /* User tile 18 → VRAM 0x1240 (tile 146), used by imshow */
    addr = VRAM_USER_TILES_TEST + 18 * VDP_TILE_SIZE;
    ASSERT_EQ(addr, 0x1240);

    /* Verify no overlap with console font area (0x0000-0x0FFF) */
    ASSERT(VRAM_USER_TILES_TEST >= 128 * VDP_TILE_SIZE);
}

static void test_user_tile_nametable_offset(void)
{
    /* User tile 0 in nametable → VDP tile 128 */
    ASSERT_EQ(USER_TILE_OFFSET_TEST, 128);

    /* User tile 1 → nametable entry 129 */
    uint16_t entry = 1 + USER_TILE_OFFSET_TEST;
    ASSERT_EQ(entry, 129);

    /* Nametable entry 129 maps to VRAM 129*32 = 0x1020 */
    uint16_t vram = entry * VDP_TILE_SIZE;
    ASSERT_EQ(vram, 0x1020);

    /* Confirm VRAM addresses match between loadtiles and setmap */
    uint16_t loadtiles_addr = VRAM_USER_TILES_TEST + 1 * VDP_TILE_SIZE;
    uint16_t setmap_addr = (1 + USER_TILE_OFFSET_TEST) * VDP_TILE_SIZE;
    ASSERT_EQ(loadtiles_addr, setmap_addr);

    /* Same for tile 18 (imshow's gradient tile) */
    loadtiles_addr = VRAM_USER_TILES_TEST + 18 * VDP_TILE_SIZE;
    setmap_addr = (18 + USER_TILE_OFFSET_TEST) * VDP_TILE_SIZE;
    ASSERT_EQ(loadtiles_addr, setmap_addr);
}

/* ---- Tests for console tab stop calculation ---- */

/*
 * Bug fix: VDP console didn't handle '\t', causing it to be
 * displayed as a visible CP437 glyph (the circle at font index 9).
 * Fix: advance cursor_x to next 8-column tab stop.
 */
static void test_tab_stop_calculation(void)
{
    /* Tab stops at columns 0, 8, 16, 24, 32 */
    /* cursor_x = (cursor_x + 8) & ~7 */
    int x;

    /* From column 0 → tab to 8 */
    x = 0; x = (x + 8) & ~7;
    ASSERT_EQ(x, 8);

    /* From column 1 → tab to 8 */
    x = 1; x = (x + 8) & ~7;
    ASSERT_EQ(x, 8);

    /* From column 7 → tab to 8 */
    x = 7; x = (x + 8) & ~7;
    ASSERT_EQ(x, 8);

    /* From column 8 → tab to 16 */
    x = 8; x = (x + 8) & ~7;
    ASSERT_EQ(x, 16);

    /* From column 15 → tab to 16 */
    x = 15; x = (x + 8) & ~7;
    ASSERT_EQ(x, 16);

    /* From column 32 → tab to 40 (triggers line wrap on 40-col VDP) */
    x = 32; x = (x + 8) & ~7;
    ASSERT_EQ(x, 40);
}

int main(void)
{
    printf("=== VDP driver tests ===\n");

    RUN_TEST(test_struct_sizes);
    RUN_TEST(test_vram_write_cmd);
    RUN_TEST(test_cram_write_cmd);
    RUN_TEST(test_vsram_write_cmd);
    RUN_TEST(test_ioctl_constants);
    RUN_TEST(test_vdp_constants);
    RUN_TEST(test_nametable_address);
    RUN_TEST(test_cram_address);
    RUN_TEST(test_sprite_layout);
    RUN_TEST(test_map_bounds);
    RUN_TEST(test_palette_bounds);
    RUN_TEST(test_user_tile_vram_address);
    RUN_TEST(test_user_tile_nametable_offset);
    RUN_TEST(test_tab_stop_calculation);

    TEST_REPORT();
}
