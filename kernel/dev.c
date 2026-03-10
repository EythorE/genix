/*
 * Device table
 */
#include "kernel.h"

/* ======== Console device ======== */

/* termios-compatible constants for console ioctl */
#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404

/* c_lflag bits */
#define ISIG    0x0001
#define ICANON  0x0002
#define ECHO    0x0008

/* c_iflag bits */
#define ICRNL   0x0100
#define INLCR   0x0040
#define IXOFF   0x1000
#define IXANY   0x0800

/* c_cc indices */
#define VINTR   0
#define VQUIT   1
#define VERASE  2
#define VKILL   3
#define VEOF    4
#define VMIN    6
#define VTIME   7
#define NCCS    12

/* Minimal termios structure — matches what levee needs */
struct kernel_termios {
    uint16_t c_iflag;
    uint16_t c_oflag;
    uint16_t c_cflag;
    uint16_t c_lflag;
    uint8_t  c_cc[NCCS];
};

/* Console state */
static uint8_t con_raw = 0;  /* 0=cooked (line-buffered+echo), 1=raw */
static struct kernel_termios con_termios = {
    .c_iflag = ICRNL,
    .c_oflag = 0,
    .c_cflag = 0,
    .c_lflag = ICANON | ECHO | ISIG,
    .c_cc = { 3, 28, 8, 21, 4, 0, 1, 0, 0, 0, 0, 0 }
    /* VINTR=^C, VQUIT=^\, VERASE=^H, VKILL=^U, VEOF=^D, VMIN=1 */
};

static int con_open(int minor) { (void)minor; return 0; }
static int con_close(int minor) { (void)minor; return 0; }

static int con_read(int minor, void *buf, int len)
{
    (void)minor;
    uint8_t *p = (uint8_t *)buf;

    if (con_raw) {
        /* Raw mode: return characters immediately, no echo, no processing */
        for (int i = 0; i < len; i++) {
            p[i] = kgetc();
        }
        return len;
    }

    /* Cooked mode: line-buffered with echo */
    for (int i = 0; i < len; i++) {
        p[i] = kgetc();
        if (p[i] == '\r') p[i] = '\n';
        /* Signal generation when ISIG is set */
        if ((con_termios.c_lflag & ISIG) && curproc) {
            if (p[i] == con_termios.c_cc[VINTR]) {
                /* Ctrl+C: send SIGINT to current process */
                kputs("^C\n");
                curproc->sig_pending |= (1u << SIGINT);
                return -EINTR;
            }
            if (p[i] == con_termios.c_cc[VQUIT]) {
                /* Ctrl+\: send SIGQUIT to current process */
                kputs("^\\\n");
                curproc->sig_pending |= (1u << SIGQUIT);
                return -EINTR;
            }
        }
        /* Echo */
        kputc(p[i]);
        if (p[i] == '\n')
            return i + 1;
    }
    return len;
}

static int con_write(int minor, const void *buf, int len)
{
    (void)minor;
    const uint8_t *p = (const uint8_t *)buf;
    for (int i = 0; i < len; i++)
        kputc(p[i]);
    return len;
}

static int con_ioctl(int minor, int cmd, void *arg)
{
    (void)minor;
    struct kernel_termios *tp;

    switch (cmd) {
    case TCGETS:
        tp = (struct kernel_termios *)arg;
        memcpy(tp, &con_termios, sizeof(con_termios));
        return 0;

    case TCSETS:
    case TCSETSW:
    case TCSETSF:
        tp = (struct kernel_termios *)arg;
        memcpy(&con_termios, tp, sizeof(con_termios));
        /* Update raw mode flag based on ICANON */
        con_raw = !(con_termios.c_lflag & ICANON);
        return 0;

    default:
        return -EINVAL;
    }
}

/* ======== VDP device ======== */

/*
 * Weak stubs — overridden by pal/megadrive/dev_vdp.c on Mega Drive.
 * On workbench (no VDP hardware) these return -ENODEV.
 */
__attribute__((weak)) int vdp_open(int minor)
{ (void)minor; return -ENODEV; }

__attribute__((weak)) int vdp_close(int minor)
{ (void)minor; return -ENODEV; }

__attribute__((weak)) int vdp_read(int minor, void *buf, int len)
{ (void)minor; (void)buf; (void)len; return -ENODEV; }

__attribute__((weak)) int vdp_write(int minor, const void *buf, int len)
{ (void)minor; (void)buf; (void)len; return -ENODEV; }

__attribute__((weak)) int vdp_ioctl(int minor, int cmd, void *arg)
{ (void)minor; (void)cmd; (void)arg; return -ENODEV; }

/* ======== Disk device (pass-through to PAL) ======== */
static int disk_open(int minor) { (void)minor; return 0; }
static int disk_close(int minor) { (void)minor; return 0; }
static int disk_read(int minor, void *buf, int len) {
    (void)minor; (void)buf; (void)len;
    return -EIO;
}
static int disk_write(int minor, const void *buf, int len) {
    (void)minor; (void)buf; (void)len;
    return -EIO;
}
static int disk_ioctl(int minor, int cmd, void *arg) {
    (void)minor; (void)cmd; (void)arg;
    return -EINVAL;
}

/* ======== Null device (/dev/null) ======== */
static int null_open(int minor) { (void)minor; return 0; }
static int null_close(int minor) { (void)minor; return 0; }
static int null_read(int minor, void *buf, int len) {
    (void)minor; (void)buf; (void)len;
    return 0;  /* EOF */
}
static int null_write(int minor, const void *buf, int len) {
    (void)minor; (void)buf;
    return len;  /* discard, report success */
}
static int null_ioctl(int minor, int cmd, void *arg) {
    (void)minor; (void)cmd; (void)arg;
    return -EINVAL;
}

struct device devtab[NDEV] = {
    [DEV_CONSOLE] = { con_open, con_close, con_read, con_write, con_ioctl },
    [DEV_DISK]    = { disk_open, disk_close, disk_read, disk_write, disk_ioctl },
    [DEV_VDP]     = { vdp_open, vdp_close, vdp_read, vdp_write, vdp_ioctl },
    [DEV_NULL]    = { null_open, null_close, null_read, null_write, null_ioctl },
};

void dev_init(void)
{
    kputs("[dev] Console, disk, VDP, and null devices ready.\n");
}

/*
 * Create device nodes in the filesystem.
 * Must be called AFTER fs_init() (filesystem must be mounted).
 */
void dev_create_nodes(void)
{
    /* Create /dev directory if it doesn't exist */
    struct inode *devdir = fs_namei("/dev");
    if (!devdir) {
        fs_mkdir("/dev");
    } else {
        fs_iput(devdir);
    }

    /* Create /dev/null (major=DEV_NULL, minor=0) */
    struct inode *nulldev = fs_namei("/dev/null");
    if (!nulldev) {
        struct inode *ip = fs_create("/dev/null", FT_DEV);
        if (ip) {
            ip->dev_major = DEV_NULL;
            ip->dev_minor = 0;
            ip->dirty = 1;
            fs_iupdate(ip);
            fs_iput(ip);
        }
    } else {
        fs_iput(nulldev);
    }
}
