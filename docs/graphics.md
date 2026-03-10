# Genix Graphics — VDP Device Driver and Library

The Mega Drive's Video Display Processor (VDP) provides tile-based 2D
graphics with sprites, scrolling, and 64 simultaneous colors. Genix
exposes the VDP through a kernel device driver (`/dev/vdp`) and a
userspace C library (`libgfx`).

## Display Mode

Genix configures the VDP in **H40/V28 mode**:

| Parameter | Value |
|-----------|-------|
| Resolution | 320x224 pixels |
| Tile grid | 40 columns x 28 rows |
| Tile size | 8x8 pixels |
| Color depth | 4 bits per pixel (16 colors per palette) |
| Palettes | 4 (64 total colors) |
| Sprites | Up to 80 |
| Scroll planes | 2 (Plane A + Plane B) |

## Architecture

```
User program (imshow, game, etc.)
        │
    libgfx (gfx.h / gfx.c)
        │  open("/dev/vdp") + ioctl()
    Kernel VDP driver (dev_vdp.c)
        │  direct hardware register writes
    VDP hardware (0xC00000-0xC00008)
```

The driver is **ioctl-only** — `read()` and `write()` return `EINVAL`.
Access is **exclusive**: only one process may hold `/dev/vdp` open at a
time (returns `EBUSY` otherwise). Closing the device automatically
restores the text console by calling `VDP_reinit()`.

## VRAM Layout

64 KB of VDP-internal video RAM, organized as:

| Address | Size | Contents |
|---------|------|----------|
| `0x0000-0x0FFF` | 4 KB | Font tiles 0-127 (system text, 8x8 1bpp expanded to 4bpp) |
| `0x1000-0xBFFF` | 44 KB | User tiles 128+ (available for graphics programs) |
| `0xC000-0xDFFF` | 8 KB | Plane A nametable (64x32 entries, 2 bytes each) |
| `0xE000-0xEFFF` | 4 KB | Plane B nametable (debug overlay) |
| `0xF000-0xF27F` | 640 B | Sprite attribute table (80 sprites, 8 bytes each) |
| `0xFC00-0xFDFF` | 512 B | Horizontal scroll table |

VSRAM (separate from VRAM) stores vertical scroll values per plane.

## Color Format

Colors are 9-bit RGB stored in 16-bit words:

```
  15  12 11   9  8    6  5    3  2    0
  ├────┤ ├────┤  ├────┤  ├────┤  ├────┤
  0000   BBB     0  GGG    0  RRR    0
```

Each channel (R, G, B) is 3 bits (0-7), shifted left by 1.
Examples: `0x000E` = red, `0x00E0` = green, `0x0E00` = blue,
`0x0EEE` = white, `0x0000` = black/transparent.

## Ioctl Commands

All commands are defined in `kernel/dev_vdp.h`:

### `VDP_IOC_LOADTILES` (0x5600) — Upload Tile Patterns

Upload 8x8 4bpp tile data into VRAM.

```c
struct vdp_tiles_arg {
    uint16_t start_id;   /* first tile index */
    uint16_t count;      /* number of tiles */
    const uint8_t *data; /* 32 bytes per tile */
};
```

Tile data format: 4 bytes per pixel row, 8 rows per tile = 32 bytes.
Each byte encodes two pixels (high nibble = left, low nibble = right).
Tiles 0-127 are reserved for the system font.

### `VDP_IOC_SETMAP` (0x5601) — Write Nametable Entries

Map tile indices to screen positions on Plane A.

```c
struct vdp_map_arg {
    uint16_t x, y;        /* top-left tile position (0-39, 0-27) */
    uint16_t w, h;        /* rectangle size in tiles */
    const uint16_t *tiles; /* tile IDs (w*h entries) */
};
```

Each nametable entry is a 16-bit word encoding tile index, palette
number, priority, and horizontal/vertical flip flags.

### `VDP_IOC_SETPAL` (0x5602) — Set Palette Colors

Write colors into CRAM (Color RAM).

```c
struct vdp_pal_arg {
    uint16_t palette;      /* palette bank (0-3) */
    uint16_t index;        /* starting color index (0-15) */
    uint16_t count;        /* number of colors */
    uint16_t pad;          /* alignment */
    const uint16_t *colors; /* color values */
};
```

### `VDP_IOC_SETSPRITE` (0x5603) — Set Sprite Attributes

Configure a hardware sprite.

```c
struct vdp_sprite_arg {
    uint16_t id;    /* sprite index (0-79) */
    int16_t  x, y;  /* screen position (hardware adds +128 offset) */
    uint16_t tile;  /* tile index + priority/palette/flip flags */
    uint16_t size;  /* size code: 0=8x8, 5=16x16, ... */
    uint16_t link;  /* next sprite in display chain (0=end) */
};
```

Sprites use linked-list ordering. The `link` field chains sprites for
the VDP's rendering pipeline.

### `VDP_IOC_SCROLL` (0x5604) — Set Scroll Position

Scroll a background plane by pixel offset.

```c
struct vdp_scroll_arg {
    uint16_t plane; /* 0 = Plane A, 1 = Plane B */
    int16_t  x, y;  /* scroll offset in pixels */
    uint16_t pad;
};
```

Vertical scroll writes to VSRAM. Horizontal scroll writes to the
H-scroll table in VRAM (value is negated internally).

### `VDP_IOC_WAITVBLANK` (0x5605) — Wait for VBlank

Blocks until the next vertical blanking interval. Use this to
synchronize screen updates and avoid tearing.

### `VDP_IOC_CLEAR` (0x5606) — Clear Screen

Zeroes the Plane A nametable, all sprite attributes, and both scroll
registers. Does not touch tile data or palettes.

## Userspace Library (libgfx)

`libc/include/gfx.h` provides a thin C wrapper:

```c
#include <gfx.h>

int  gfx_open(void);      /* open /dev/vdp, clear screen */
void gfx_close(void);     /* close device, restore console */

int  gfx_tiles(int start_id, const uint8_t *data, int count);
int  gfx_map(int x, int y, int w, int h, const uint16_t *tiles);
int  gfx_map1(int x, int y, uint16_t tile);
int  gfx_palette(int pal, int idx, const uint16_t *colors, int count);
int  gfx_sprite(int id, int x, int y, uint16_t tile, int size, int link);
int  gfx_scroll(int plane, int x, int y);
int  gfx_vsync(void);
int  gfx_cls(void);
```

`gfx_open()` opens the device exclusively and clears the screen.
`gfx_close()` closes the device, which triggers the kernel to restore
the text console (font, palettes, VDP registers).

## Example: Color Bar Test (imshow)

`apps/imshow.c` demonstrates the full graphics stack:

```c
#include <gfx.h>

static const uint16_t palette[16] = {
    0x0000, 0x000E, 0x00E0, 0x00EE,  /* black, red, green, yellow */
    0x0E00, 0x0E0E, 0x0EE0, 0x0EEE,  /* blue, magenta, cyan, white */
    /* ... dark variants and greys ... */
};

int main(void) {
    gfx_open();
    gfx_palette(0, 0, palette, 16);

    /* Generate and upload solid-color tiles */
    uint8_t tile[GFX_TILE_SIZE];
    for (int i = 1; i < 16; i++) {
        /* Fill tile with color i in every nibble */
        memset(tile, (i << 4) | i, GFX_TILE_SIZE);
        gfx_tiles(i, tile, 1);
    }

    /* Map tiles to screen */
    for (int row = 0; row < GFX_ROWS; row++)
        for (int col = 0; col < GFX_COLS; col++)
            gfx_map1(col, row, 1 + (col * 15) / GFX_COLS);

    gfx_vsync();
    read(0, tile, 1);  /* wait for keypress */
    gfx_close();
    return 0;
}
```

Run on Mega Drive: `exec /bin/imshow`

## Text Console

The VDP also serves as the system text console via assembly routines in
`pal/megadrive/devvt.S`:

- `plot_char(y, x, c)` — write a character tile to Plane A
- `clear_across(y, x, n)` / `clear_lines(y, n)` — erase regions
- `scroll_up()` / `scroll_down()` — hardware vertical scroll (VSRAM, no data copy)
- `cursor_on(y, x)` / `cursor_off()` — sprite-based cursor (sprite 0)

The system font is 256 ASCII glyphs at 8x8 pixels (from Linux
`font_8x8.c` via FUZIX). It's stored as 1bpp and expanded to 4bpp
during VDP initialization by `VDP_fontInit` in `vdp.S`.

## Default Palettes

| Palette | Use | Key Colors |
|---------|-----|------------|
| 0 | System text | transparent, black bg, white fg |
| 1 | Debug overlay | white bg, dark fg |
| 2 | Cursor | grey tones |
| 3 | Reserved | unused |

## Hardware Details

| Register | Address | Purpose |
|----------|---------|---------|
| VDP Data | `0xC00000` | Read/write VRAM, CRAM, VSRAM |
| VDP Control | `0xC00004` | Send commands, read status |
| HV Counter | `0xC00008` | Current scanline/pixel position |

VDP command words encode the target memory (VRAM/CRAM/VSRAM), access
direction (read/write), and 16-bit address into a 32-bit value written
to the control port. The auto-increment register is set to 2 bytes,
allowing sequential word writes after a single command.

## Files

| File | Description |
|------|-------------|
| `kernel/dev_vdp.h` | Ioctl command definitions and structures |
| `pal/megadrive/dev_vdp.c` | Kernel driver implementation |
| `pal/megadrive/vdp.S` | VDP init, font loading, register table |
| `pal/megadrive/devvt.S` | Text console (plot_char, scroll, cursor) |
| `pal/megadrive/fontdata_8x8.c` | System font (256 glyphs, 2048 bytes) |
| `pal/megadrive/control_ports.def` | Hardware address constants |
| `libc/include/gfx.h` | Userspace graphics API header |
| `libc/gfx.c` | Userspace graphics library |
| `apps/imshow.c` | Color bar test/demo application |
| `tests/test_vdp.c` | VDP unit tests |

## Limitations

- **Ioctl-only** — no streaming read/write interface
- **Exclusive access** — one process at a time
- **No DMA exposed** — tile uploads use CPU writes (fast enough for
  current use cases; DMA can be added later if needed)
- **Full-screen scroll only** — per-line and per-column scroll modes
  are supported by hardware but not yet exposed
- **Workbench emulator** has no VDP — `/dev/vdp` only exists on Mega
  Drive builds. Graphics programs print an error and exit on workbench
