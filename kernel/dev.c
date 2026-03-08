/*
 * Device table
 */
#include "kernel.h"

/* Console device */
static int con_open(int minor) { (void)minor; return 0; }
static int con_close(int minor) { (void)minor; return 0; }

static int con_read(int minor, void *buf, int len)
{
    (void)minor;
    uint8_t *p = (uint8_t *)buf;
    for (int i = 0; i < len; i++) {
        p[i] = kgetc();
        if (p[i] == '\r') p[i] = '\n';
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
    (void)minor; (void)cmd; (void)arg;
    return -ENOTTY;
}

/* Disk device (pass-through to PAL) */
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

struct device devtab[NDEV] = {
    [DEV_CONSOLE] = { con_open, con_close, con_read, con_write, con_ioctl },
    [DEV_DISK]    = { disk_open, disk_close, disk_read, disk_write, disk_ioctl },
};

void dev_init(void)
{
    kputs("[dev] Console and disk devices ready.\n");
}
