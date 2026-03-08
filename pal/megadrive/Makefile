TOOLCHAIN = /opt/m68k/m68k-68xxx--uclibc--stable-2024.05-1
CROSS = $(TOOLCHAIN)/bin/m68k-buildroot-linux-uclibc-
CC = $(CROSS)gcc
AS = $(CROSS)as
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
LIBGCC = $(shell $(CC) -m68000 -print-libgcc-file-name)

CFLAGS = -m68000 -nostdlib -nostartfiles -ffreestanding \
         -Wall -Wextra -Wno-unused-function -O2 \
         -I../../kernel -I../../pal

KERNEL_DIR = ../../kernel

# Mega Drive-specific startup
ASM_SRCS = crt0.S $(KERNEL_DIR)/divmod.S
C_SRCS = platform.c \
         $(KERNEL_DIR)/main.c $(KERNEL_DIR)/kprintf.c \
         $(KERNEL_DIR)/mem.c $(KERNEL_DIR)/buf.c $(KERNEL_DIR)/fs.c \
         $(KERNEL_DIR)/dev.c $(KERNEL_DIR)/proc.c $(KERNEL_DIR)/string.c

ASM_OBJS = $(notdir $(ASM_SRCS:.S=.o))
C_OBJS = $(notdir $(C_SRCS:.c=.o))
OBJS = $(ASM_OBJS) $(C_OBJS)

TARGET = genix-md.elf
BINARY = genix-md.bin
DISK_IMG = ../../disk.img

all: $(BINARY)

$(BINARY): $(TARGET)
	$(OBJCOPY) -O binary $< $@
	@echo "ROM size: $$(wc -c < $@) bytes"

# Embed filesystem image as .romdisk section
romdisk.o: $(DISK_IMG)
	$(OBJCOPY) -I binary -O elf32-m68k -B m68k \
		--rename-section .data=.romdisk \
		--redefine-sym _binary_____disk_img_start=_rom_disk_start \
		--redefine-sym _binary_____disk_img_end=_rom_disk_end \
		--redefine-sym _binary_____disk_img_size=_rom_disk_size \
		$< $@

$(TARGET): $(OBJS) romdisk.o megadrive.ld
	$(LD) -T megadrive.ld -o $@ $(OBJS) romdisk.o $(LIBGCC)

# Assembly
crt0.o: crt0.S
	$(CC) $(CFLAGS) -c -o $@ $<

divmod.o: $(KERNEL_DIR)/divmod.S
	$(CC) $(CFLAGS) -c -o $@ $<

# C files from kernel directory
main.o: $(KERNEL_DIR)/main.c
	$(CC) $(CFLAGS) -c -o $@ $<

kprintf.o: $(KERNEL_DIR)/kprintf.c
	$(CC) $(CFLAGS) -c -o $@ $<

mem.o: $(KERNEL_DIR)/mem.c
	$(CC) $(CFLAGS) -c -o $@ $<

buf.o: $(KERNEL_DIR)/buf.c
	$(CC) $(CFLAGS) -c -o $@ $<

fs.o: $(KERNEL_DIR)/fs.c
	$(CC) $(CFLAGS) -c -o $@ $<

dev.o: $(KERNEL_DIR)/dev.c
	$(CC) $(CFLAGS) -c -o $@ $<

proc.o: $(KERNEL_DIR)/proc.c
	$(CC) $(CFLAGS) -c -o $@ $<

string.o: $(KERNEL_DIR)/string.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Mega Drive platform
platform.o: platform.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) romdisk.o $(TARGET) $(BINARY)

.PHONY: all clean
