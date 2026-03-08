/*
 * Block buffer cache
 */
#include "kernel.h"

static struct buf bufs[NBUFS];

void buf_init(void)
{
    for (int i = 0; i < NBUFS; i++) {
        bufs[i].blockno = 0xFFFF;
        bufs[i].dev = 0xFF;
        bufs[i].dirty = 0;
        bufs[i].valid = 0;
    }
}

struct buf *bread(uint8_t dev, uint16_t blockno)
{
    /* Check cache */
    for (int i = 0; i < NBUFS; i++) {
        if (bufs[i].valid && bufs[i].dev == dev && bufs[i].blockno == blockno)
            return &bufs[i];
    }

    /* Find a free or LRU slot (simple: first invalid, then first non-dirty) */
    struct buf *b = NULL;
    for (int i = 0; i < NBUFS; i++) {
        if (!bufs[i].valid) {
            b = &bufs[i];
            break;
        }
    }
    if (!b) {
        /* Evict first non-dirty, or first */
        for (int i = 0; i < NBUFS; i++) {
            if (!bufs[i].dirty) {
                b = &bufs[i];
                break;
            }
        }
        if (!b) {
            b = &bufs[0];
            bwrite(b);
        }
    }

    b->dev = dev;
    b->blockno = blockno;
    b->dirty = 0;
    pal_disk_read(dev, blockno, b->data);
    b->valid = 1;
    return b;
}

void bwrite(struct buf *b)
{
    if (b->dirty) {
        pal_disk_write(b->dev, b->blockno, b->data);
        b->dirty = 0;
    }
}

void brelse(struct buf *b)
{
    /* For now, just flush if dirty */
    if (b->dirty)
        bwrite(b);
}
