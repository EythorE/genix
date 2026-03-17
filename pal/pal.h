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
int      pal_console_rows(void);
int      pal_console_cols(void);

/* ROM XIP support: return the memory-mapped ROM address of a file's data,
 * or 0 if the disk is not memory-mapped (workbench) or blocks aren't
 * contiguous. Only valid for dev=0 (ROM disk). */
struct inode;
uint32_t pal_rom_file_addr(struct inode *ip);

#endif
