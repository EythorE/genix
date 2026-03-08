/*
 * Workbench SBC platform — maps to emulator UART/timer/disk at fixed addresses
 *
 * Memory-mapped I/O:
 *   0xF00000  UART data (r/w byte)
 *   0xF00002  UART status (bit0=rx ready, bit1=tx ready)
 *   0xF10000  Timer count (32-bit, read-only)
 *   0xF10002  Timer control (write 1=enable, 0=disable)
 *   0xF20000  Disk command (write: 1=read, 2=write)
 *   0xF20002  Disk block number (32-bit)
 *   0xF20004  Disk status (read: 0=busy, 1=done, 0x80=error)
 *   0xF20008  Disk data buffer (1024 bytes)
 */

#include "../../kernel/kernel.h"

/* Hardware register access */
#define REG8(addr)   (*(volatile uint8_t  *)(addr))
#define REG16(addr)  (*(volatile uint16_t *)(addr))
#define REG32(addr)  (*(volatile uint32_t *)(addr))

#define UART_DATA    0xF00000
#define UART_STATUS  0xF00002
#define TIMER_COUNT  0xF10000
#define TIMER_CTRL   0xF10002
#define DISK_CMD     0xF20000
#define DISK_BLOCK   0xF20004
#define DISK_STATUS  0xF20008
#define DISK_BUFFER  0xF20010

#define UART_RX_RDY  0x01
#define UART_TX_RDY  0x02

/* Kernel starts after vectors + kernel code.
 * We'll place the heap at a known address. */
extern char _end;  /* Defined by linker script */

void pal_init(void)
{
    /* Nothing to do — emulator handles HW init */
}

void pal_console_putc(char c)
{
    /* Wait for TX ready (always ready in emu, but be correct) */
    while (!(REG16(UART_STATUS) & UART_TX_RDY))
        ;
    REG8(UART_DATA) = c;
}

int pal_console_getc(void)
{
    while (!(REG16(UART_STATUS) & UART_RX_RDY))
        ;
    return REG8(UART_DATA);
}

int pal_console_ready(void)
{
    return REG16(UART_STATUS) & UART_RX_RDY;
}

void pal_disk_read(int dev, uint32_t block, void *buf)
{
    (void)dev;
    REG32(DISK_BLOCK) = block;
    REG16(DISK_CMD) = 1;  /* read */

    /* Disk I/O is synchronous in the emulator */
    volatile uint8_t *src = (volatile uint8_t *)DISK_BUFFER;
    uint8_t *dst = (uint8_t *)buf;
    for (int i = 0; i < 1024; i++)
        dst[i] = src[i];
}

void pal_disk_write(int dev, uint32_t block, void *buf)
{
    (void)dev;
    volatile uint8_t *dst = (volatile uint8_t *)DISK_BUFFER;
    const uint8_t *src = (const uint8_t *)buf;
    for (int i = 0; i < 1024; i++)
        dst[i] = src[i];

    REG32(DISK_BLOCK) = block;
    REG16(DISK_CMD) = 2;  /* write */
}

uint32_t pal_mem_start(void)
{
    /* Heap starts after kernel BSS, aligned to 4 bytes */
    uint32_t end = (uint32_t)&_end;
    return (end + 3) & ~3;
}

uint32_t pal_mem_end(void)
{
    return 0x100000 - 0x1000;  /* Leave 4K at top for stack */
}

void pal_timer_init(int hz)
{
    (void)hz;
    REG16(TIMER_CTRL) = 1;  /* Enable timer interrupt */
}

uint32_t pal_timer_ticks(void)
{
    return REG32(TIMER_COUNT);
}
