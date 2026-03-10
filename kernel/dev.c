/*
 * Device table
 */
#include "kernel.h"
#include "tty.h"

/* ======== Console device (routed through TTY layer) ======== */

static int con_open(int minor) { (void)minor; return 0; }
static int con_close(int minor) { (void)minor; return 0; }

static int con_read(int minor, void *buf, int len)
{
    (void)minor;
    return tty_read(0, buf, len);
}

static int con_write(int minor, const void *buf, int len)
{
    (void)minor;
    return tty_write(0, buf, len);
}

static int con_ioctl(int minor, int cmd, void *arg)
{
    (void)minor;
    return tty_ioctl(0, cmd, arg);
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
    tty_init();
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

    /* Create /dev/tty (major=DEV_CONSOLE, minor=0) — alias for controlling TTY */
    struct inode *ttydev = fs_namei("/dev/tty");
    if (!ttydev) {
        struct inode *ip = fs_create("/dev/tty", FT_DEV);
        if (ip) {
            ip->dev_major = DEV_CONSOLE;
            ip->dev_minor = 0;
            ip->dirty = 1;
            fs_iupdate(ip);
            fs_iput(ip);
        }
    } else {
        fs_iput(ttydev);
    }

    /* Create /dev/console (major=DEV_CONSOLE, minor=0) */
    struct inode *condev = fs_namei("/dev/console");
    if (!condev) {
        struct inode *ip = fs_create("/dev/console", FT_DEV);
        if (ip) {
            ip->dev_major = DEV_CONSOLE;
            ip->dev_minor = 0;
            ip->dirty = 1;
            fs_iupdate(ip);
            fs_iput(ip);
        }
    } else {
        fs_iput(condev);
    }
}
