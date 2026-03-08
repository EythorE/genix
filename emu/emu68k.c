/*
 * Genix Workbench Emulator
 *
 * A simple 68000 SBC emulator for kernel development:
 *   0x000000 - 0x0FFFFF  1MB RAM (kernel loaded here at reset)
 *   0xF00000 - 0xF00003  UART (data + status)
 *   0xF10000 - 0xF10003  Timer (count + control)
 *   0xF20000 - 0xF20003  Disk control (command + block + status)
 *   0xF20004 - 0xF203FF  Disk data buffer (1024 bytes)
 *
 * Usage: ./emu68k kernel.bin [disk.img]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdint.h>
#include <errno.h>
#include "m68k.h"

/* Memory map */
#define RAM_SIZE        0x100000    /* 1MB */
#define UART_BASE       0xF00000
#define UART_DATA       0xF00000
#define UART_STATUS     0xF00002
#define TIMER_BASE      0xF10000
#define TIMER_COUNT     0xF10000
#define TIMER_CONTROL   0xF10002
#define DISK_BASE       0xF20000
#define DISK_CMD        0xF20000
#define DISK_BLOCK      0xF20004    /* 32-bit block number */
#define DISK_STATUS     0xF20008
#define DISK_BUFFER     0xF20010

/* UART status bits */
#define UART_RX_READY   0x01
#define UART_TX_READY   0x02

/* Disk commands */
#define DISK_CMD_READ   1
#define DISK_CMD_WRITE  2

/* Timer */
#define TIMER_HZ        100
#define CYCLES_PER_SEC  7670000     /* ~7.67 MHz (68000 in Mega Drive) */
#define CYCLES_PER_TICK (CYCLES_PER_SEC / TIMER_HZ)

/* Block size */
#define BLOCK_SIZE      1024

static unsigned char g_ram[RAM_SIZE];
static unsigned char g_disk_buf[BLOCK_SIZE];
static FILE *g_disk_file = NULL;
static uint32_t g_disk_block = 0;
static uint8_t g_disk_status = 0;   /* 0=idle, 1=done, 0x80=error */

static uint32_t g_timer_count = 0;
static uint8_t g_timer_enabled = 0;


static int g_quit = 0;
static struct termios g_orig_term;
static int g_term_setup = 0;

/* Forward declarations */
static void disk_do_read(uint32_t block);
static void disk_do_write(uint32_t block);

/* Terminal raw mode for UART */
static void term_setup(void)
{
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &g_orig_term) < 0)
        return;
    raw = g_orig_term;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    g_term_setup = 1;
}

static void term_restore(void)
{
    if (g_term_setup)
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_term);
}

static void sigint_handler(int sig)
{
    (void)sig;
    g_quit = 1;
}

/* Check if a character is available from stdin */
static int uart_rx_ready(void)
{
    fd_set fds;
    struct timeval tv = {0, 0};
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

static int g_stdin_eof = 0;

static int uart_read_char(void)
{
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) {
        if (c == 0x03) {  /* Ctrl+C — quit emulator */
            g_quit = 1;
            return -1;
        }
        return c;
    }
    if (n == 0) {
        /* EOF on stdin — signal quit so piped sessions terminate */
        g_stdin_eof = 1;
        g_quit = 1;
    }
    return -1;
}

/* Disk operations */
static void disk_do_read(uint32_t block)
{
    if (!g_disk_file) {
        g_disk_status = 0x80;
        return;
    }
    if (fseek(g_disk_file, (long)block * BLOCK_SIZE, SEEK_SET) != 0) {
        g_disk_status = 0x80;
        return;
    }
    memset(g_disk_buf, 0, BLOCK_SIZE);
    if (fread(g_disk_buf, 1, BLOCK_SIZE, g_disk_file) == 0 && ferror(g_disk_file)) {
        g_disk_status = 0x80;
        return;
    }
    g_disk_status = 1;
}

static void disk_do_write(uint32_t block)
{
    if (!g_disk_file) {
        g_disk_status = 0x80;
        return;
    }
    if (fseek(g_disk_file, (long)block * BLOCK_SIZE, SEEK_SET) != 0) {
        g_disk_status = 0x80;
        return;
    }
    if (fwrite(g_disk_buf, 1, BLOCK_SIZE, g_disk_file) != BLOCK_SIZE) {
        g_disk_status = 0x80;
        return;
    }
    fflush(g_disk_file);
    g_disk_status = 1;
}

/* Musashi memory callbacks */
unsigned int m68k_read_memory_8(unsigned int addr)
{
    addr &= 0xFFFFFF;
    if (addr < RAM_SIZE)
        return g_ram[addr];
    if (addr == UART_DATA) {
        int c = uart_read_char();
        return (c >= 0) ? (unsigned int)c : 0;
    }
    if (addr == (UART_STATUS))
        return (uart_rx_ready() ? UART_RX_READY : 0) | UART_TX_READY;
    if (addr == (UART_STATUS + 1))
        return (uart_rx_ready() ? UART_RX_READY : 0) | UART_TX_READY;
    if (addr >= DISK_BUFFER && addr < DISK_BUFFER + BLOCK_SIZE)
        return g_disk_buf[addr - DISK_BUFFER];
    if (addr == DISK_STATUS || addr == DISK_STATUS + 1)
        return g_disk_status;
    return 0;
}

unsigned int m68k_read_memory_16(unsigned int addr)
{
    addr &= 0xFFFFFF;
    if (addr < RAM_SIZE - 1)
        return (g_ram[addr] << 8) | g_ram[addr + 1];
    if (addr == UART_DATA) {
        int c = uart_read_char();
        return (c >= 0) ? (unsigned int)c : 0;
    }
    if (addr == UART_STATUS)
        return (uart_rx_ready() ? UART_RX_READY : 0) | UART_TX_READY;
    if (addr == TIMER_COUNT)
        return (g_timer_count >> 16) & 0xFFFF;
    if (addr == TIMER_COUNT + 2)
        return g_timer_count & 0xFFFF;
    if (addr == TIMER_CONTROL)
        return g_timer_enabled;
    if (addr == DISK_BLOCK)
        return (g_disk_block >> 16) & 0xFFFF;
    if (addr == DISK_BLOCK + 2)
        return g_disk_block & 0xFFFF;
    if (addr == DISK_STATUS)
        return g_disk_status;
    if (addr >= DISK_BUFFER && addr < DISK_BUFFER + BLOCK_SIZE - 1)
        return (g_disk_buf[addr - DISK_BUFFER] << 8) | g_disk_buf[addr - DISK_BUFFER + 1];
    return 0;
}

unsigned int m68k_read_memory_32(unsigned int addr)
{
    addr &= 0xFFFFFF;
    if (addr < RAM_SIZE - 3)
        return (g_ram[addr] << 24) | (g_ram[addr+1] << 16) |
               (g_ram[addr+2] << 8) | g_ram[addr+3];
    if (addr == TIMER_COUNT)
        return g_timer_count;
    if (addr == DISK_BLOCK)
        return g_disk_block;
    return (m68k_read_memory_16(addr) << 16) | m68k_read_memory_16(addr + 2);
}

void m68k_write_memory_8(unsigned int addr, unsigned int val)
{
    addr &= 0xFFFFFF;
    val &= 0xFF;
    if (addr < RAM_SIZE) {
        g_ram[addr] = val;
        return;
    }
    if (addr == UART_DATA || addr == UART_DATA + 1) {
        putchar(val);
        fflush(stdout);
        return;
    }
    if (addr >= DISK_BUFFER && addr < DISK_BUFFER + BLOCK_SIZE) {
        g_disk_buf[addr - DISK_BUFFER] = val;
        return;
    }
}

void m68k_write_memory_16(unsigned int addr, unsigned int val)
{
    addr &= 0xFFFFFF;
    val &= 0xFFFF;
    if (addr < RAM_SIZE - 1) {
        g_ram[addr] = (val >> 8) & 0xFF;
        g_ram[addr + 1] = val & 0xFF;
        return;
    }
    if (addr == UART_DATA) {
        putchar(val & 0xFF);
        fflush(stdout);
        return;
    }
    if (addr == TIMER_CONTROL) {
        g_timer_enabled = val & 1;
        return;
    }
    if (addr == DISK_CMD) {
        if (val == DISK_CMD_READ)
            disk_do_read(g_disk_block);
        else if (val == DISK_CMD_WRITE)
            disk_do_write(g_disk_block);
        return;
    }
    if (addr == DISK_BLOCK) {
        g_disk_block = (g_disk_block & 0xFFFF) | (val << 16);
        return;
    }
    if (addr == DISK_BLOCK + 2) {
        g_disk_block = (g_disk_block & 0xFFFF0000) | val;
        return;
    }
    if (addr >= DISK_BUFFER && addr < DISK_BUFFER + BLOCK_SIZE - 1) {
        g_disk_buf[addr - DISK_BUFFER] = (val >> 8) & 0xFF;
        g_disk_buf[addr - DISK_BUFFER + 1] = val & 0xFF;
        return;
    }
}

void m68k_write_memory_32(unsigned int addr, unsigned int val)
{
    addr &= 0xFFFFFF;
    if (addr < RAM_SIZE - 3) {
        g_ram[addr]   = (val >> 24) & 0xFF;
        g_ram[addr+1] = (val >> 16) & 0xFF;
        g_ram[addr+2] = (val >> 8) & 0xFF;
        g_ram[addr+3] = val & 0xFF;
        return;
    }
    if (addr == DISK_BLOCK) {
        g_disk_block = val;
        return;
    }
    m68k_write_memory_16(addr, (val >> 16) & 0xFFFF);
    m68k_write_memory_16(addr + 2, val & 0xFFFF);
}

/* Disassembler support (required by Musashi) */
unsigned int m68k_read_disassembler_16(unsigned int addr)
{
    return m68k_read_memory_16(addr);
}

unsigned int m68k_read_disassembler_32(unsigned int addr)
{
    return m68k_read_memory_32(addr);
}

static void load_binary(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open %s: %s\n", filename, strerror(errno));
        exit(1);
    }
    size_t n = fread(g_ram, 1, RAM_SIZE, f);
    fclose(f);
    fprintf(stderr, "[emu] Loaded %zu bytes from %s\n", n, filename);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: emu68k <kernel.bin> [disk.img]\n");
        return 1;
    }

    memset(g_ram, 0, RAM_SIZE);
    load_binary(argv[1]);

    if (argc >= 3) {
        g_disk_file = fopen(argv[2], "r+b");
        if (!g_disk_file) {
            /* Try read-only */
            g_disk_file = fopen(argv[2], "rb");
            if (g_disk_file)
                fprintf(stderr, "[emu] Disk %s opened read-only\n", argv[2]);
            else
                fprintf(stderr, "[emu] Warning: cannot open disk %s\n", argv[2]);
        } else {
            fprintf(stderr, "[emu] Disk %s opened read-write\n", argv[2]);
        }
    }

    term_setup();
    signal(SIGINT, sigint_handler);

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();

    fprintf(stderr, "[emu] Running...\n");

    unsigned int cycles_until_tick = CYCLES_PER_TICK;

    while (!g_quit) {
        unsigned int cycles = m68k_execute(1000);
        cycles_until_tick -= cycles;

        if (cycles_until_tick <= 0) {
            cycles_until_tick += CYCLES_PER_TICK;
            g_timer_count++;
            if (g_timer_enabled) {
                /* Level 6 autovector interrupt (timer) */
                m68k_set_irq(6);
                /* Auto-clear after a few cycles */
                m68k_execute(100);
                m68k_set_irq(0);
            }
        }
    }

    term_restore();
    fprintf(stderr, "\n[emu] Stopped.\n");

    if (g_disk_file)
        fclose(g_disk_file);

    return 0;
}
