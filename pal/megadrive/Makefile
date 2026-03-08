CROSS ?= m68k-linux-gnu-
CC = $(CROSS)gcc
AS = $(CROSS)as
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
LIBGCC = $(shell $(CC) -m68000 -print-libgcc-file-name)

CFLAGS = -m68000 -nostdlib -nostartfiles -ffreestanding \
         -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -O2 \
         -I../../kernel -I../../pal -I.

KERNEL_DIR = ../../kernel

# Assembly sources (Mega Drive platform + kernel)
ASM_SRCS = crt0.S $(KERNEL_DIR)/divmod.S $(KERNEL_DIR)/exec_asm.S \
           keyboard_read.S vdp.S devvt.S dbg_output.S

# C sources (Mega Drive platform + kernel)
C_SRCS = platform.c keyboard.c fontdata_8x8.c \
         $(KERNEL_DIR)/main.c $(KERNEL_DIR)/kprintf.c \
         $(KERNEL_DIR)/mem.c $(KERNEL_DIR)/buf.c $(KERNEL_DIR)/fs.c \
         $(KERNEL_DIR)/dev.c $(KERNEL_DIR)/proc.c $(KERNEL_DIR)/exec.c \
         $(KERNEL_DIR)/string.c

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

# Embed filesystem image as .romdisk section.
# Copy to a fixed name so objcopy always generates predictable symbols.
romdisk.o: $(DISK_IMG)
	cp $< romdisk.bin
	$(OBJCOPY) -I binary -O elf32-m68k -B m68k \
		--rename-section .data=.romdisk \
		--redefine-sym _binary_romdisk_bin_start=_rom_disk_start \
		--redefine-sym _binary_romdisk_bin_end=_rom_disk_end \
		--redefine-sym _binary_romdisk_bin_size=_rom_disk_size \
		romdisk.bin $@
	rm -f romdisk.bin

$(TARGET): $(OBJS) romdisk.o megadrive.ld
	$(LD) -T megadrive.ld -o $@ $(OBJS) romdisk.o $(LIBGCC)

# ---- Assembly files ----

crt0.o: crt0.S
	$(CC) $(CFLAGS) -c -o $@ $<

divmod.o: $(KERNEL_DIR)/divmod.S
	$(CC) $(CFLAGS) -c -o $@ $<

keyboard_read.o: keyboard_read.S macros.S control_ports.def
	$(CC) $(CFLAGS) -c -o $@ $<

vdp.o: vdp.S control_ports.def
	$(CC) $(CFLAGS) -c -o $@ $<

devvt.o: devvt.S control_ports.def
	$(CC) $(CFLAGS) -c -o $@ $<

dbg_output.o: dbg_output.S control_ports.def
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- C files from kernel directory ----

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

exec.o: $(KERNEL_DIR)/exec.c
	$(CC) $(CFLAGS) -c -o $@ $<

exec_asm.o: $(KERNEL_DIR)/exec_asm.S
	$(CC) $(CFLAGS) -c -o $@ $<

string.o: $(KERNEL_DIR)/string.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- Mega Drive platform C files ----

platform.o: platform.c keyboard.h keycode.h
	$(CC) $(CFLAGS) -c -o $@ $<

keyboard.o: keyboard.c keyboard.h keycode.h
	$(CC) $(CFLAGS) -c -o $@ $<

fontdata_8x8.o: fontdata_8x8.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) romdisk.o $(TARGET) $(BINARY)

.PHONY: all clean
