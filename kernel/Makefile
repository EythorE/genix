CROSS ?= m68k-elf-
CC = $(CROSS)gcc
AS = $(CROSS)as
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
LIBGCC = $(shell $(CC) -m68000 -print-libgcc-file-name)

CFLAGS = -m68000 -nostdlib -nostartfiles -ffreestanding \
         -Wall -Wextra -Wno-unused-function -O2 -I. -I../pal \
         $(EXTRA_CFLAGS)

ASFLAGS = -m68000

# Kernel sources
ASM_SRCS = crt0.S divmod.S exec_asm.S
C_SRCS = main.c kprintf.c mem.c buf.c fs.c dev.c tty.c proc.c exec.c string.c \
         ../pal/workbench/platform.c

ASM_OBJS = $(ASM_SRCS:.S=.o)
C_OBJS = $(C_SRCS:.c=.o)
OBJS = $(ASM_OBJS) $(C_OBJS)

TARGET = kernel.elf
BINARY = kernel.bin

all: $(BINARY)

$(BINARY): $(TARGET)
	$(OBJCOPY) -O binary $< $@

$(TARGET): $(OBJS) kernel.ld
	$(LD) -T kernel.ld -o $@ $(OBJS) $(LIBGCC)

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) $(BINARY)

.PHONY: all clean
