/*
 * Platform Abstraction Layer interface
 * Included by kernel.h — each platform implements these functions.
 */
#ifndef PAL_H
#define PAL_H

#include <stdint.h>

void     pal_init(void);
void     pal_console_putc(char c);
int      pal_console_getc(void);
int      pal_console_ready(void);
void     pal_disk_read(int dev, uint32_t block, void *buf);
void     pal_disk_write(int dev, uint32_t block, void *buf);
uint32_t pal_mem_start(void);
uint32_t pal_mem_end(void);
uint32_t pal_user_base(void);
uint32_t pal_user_top(void);
void     pal_timer_init(int hz);
uint32_t pal_timer_ticks(void);
void     pal_halt(void);

#endif
