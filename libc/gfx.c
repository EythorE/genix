/*
 * gfx.c — Genix userspace graphics library
 *
 * Wraps /dev/vdp ioctls into a simple C API.
 */
#include "include/gfx.h"
#include "include/unistd.h"
#include "include/fcntl.h"
#include "include/termios.h"

/* ioctl prototype from syscalls.S */
extern int ioctl(int fd, int cmd, void *arg);

/* VDP ioctl commands — must match kernel/dev_vdp.h */
#define VDP_IOC_LOADTILES   0x5600
#define VDP_IOC_SETMAP      0x5601
#define VDP_IOC_SETPAL      0x5602
#define VDP_IOC_SETSPRITE   0x5603
#define VDP_IOC_SCROLL      0x5604
#define VDP_IOC_WAITVBLANK  0x5605
#define VDP_IOC_CLEAR       0x5606

/* ioctl argument structures — must match kernel/dev_vdp.h */
struct vdp_tiles_arg {
    uint16_t start_id;
    uint16_t count;
    const uint8_t *data;
};

struct vdp_map_arg {
    uint16_t x, y, w, h;
    const uint16_t *tiles;
};

struct vdp_pal_arg {
    uint16_t palette;
    uint16_t index;
    uint16_t count;
    uint16_t pad;
    const uint16_t *colors;
};

struct vdp_sprite_arg {
    uint16_t id;
    int16_t  x, y;
    uint16_t tile;
    uint16_t size;
    uint16_t link;
};

struct vdp_scroll_arg {
    uint16_t plane;
    int16_t  x, y;
    uint16_t pad;
};

static int vdp_fd = -1;
static struct termios gfx_saved_termios;

int gfx_open(void)
{
    if (vdp_fd >= 0)
        return 0;  /* already open */
    vdp_fd = open("/dev/vdp", O_RDWR);
    if (vdp_fd < 0)
        return vdp_fd;
    /* Disable ISIG to prevent Ctrl-Z/Ctrl-C in graphics mode */
    tcgetattr(0, &gfx_saved_termios);
    struct termios t = gfx_saved_termios;
    t.c_lflag &= ~ISIG;
    tcsetattr(0, TCSANOW, &t);
    /* Clear screen on open */
    ioctl(vdp_fd, VDP_IOC_CLEAR, (void *)0);
    return 0;
}

void gfx_close(void)
{
    if (vdp_fd >= 0) {
        close(vdp_fd);
        vdp_fd = -1;
        /* Restore terminal settings */
        tcsetattr(0, TCSANOW, &gfx_saved_termios);
    }
}

int gfx_tiles(int start_id, const uint8_t *data, int count)
{
    struct vdp_tiles_arg a;
    a.start_id = (uint16_t)start_id;
    a.count = (uint16_t)count;
    a.data = data;
    return ioctl(vdp_fd, VDP_IOC_LOADTILES, &a);
}

int gfx_map(int x, int y, int w, int h, const uint16_t *tiles)
{
    struct vdp_map_arg a;
    a.x = (uint16_t)x;
    a.y = (uint16_t)y;
    a.w = (uint16_t)w;
    a.h = (uint16_t)h;
    a.tiles = tiles;
    return ioctl(vdp_fd, VDP_IOC_SETMAP, &a);
}

int gfx_map1(int x, int y, uint16_t tile)
{
    return gfx_map(x, y, 1, 1, &tile);
}

int gfx_palette(int pal, int idx, const uint16_t *colors, int count)
{
    struct vdp_pal_arg a;
    a.palette = (uint16_t)pal;
    a.index = (uint16_t)idx;
    a.count = (uint16_t)count;
    a.pad = 0;
    a.colors = colors;
    return ioctl(vdp_fd, VDP_IOC_SETPAL, &a);
}

int gfx_sprite(int id, int x, int y, uint16_t tile, int size, int link)
{
    struct vdp_sprite_arg a;
    a.id = (uint16_t)id;
    a.x = (int16_t)x;
    a.y = (int16_t)y;
    a.tile = tile;
    a.size = (uint16_t)size;
    a.link = (uint16_t)link;
    return ioctl(vdp_fd, VDP_IOC_SETSPRITE, &a);
}

int gfx_scroll(int plane, int x, int y)
{
    struct vdp_scroll_arg a;
    a.plane = (uint16_t)plane;
    a.x = (int16_t)x;
    a.y = (int16_t)y;
    a.pad = 0;
    return ioctl(vdp_fd, VDP_IOC_SCROLL, &a);
}

int gfx_vsync(void)
{
    return ioctl(vdp_fd, VDP_IOC_WAITVBLANK, (void *)0);
}

int gfx_cls(void)
{
    return ioctl(vdp_fd, VDP_IOC_CLEAR, (void *)0);
}
