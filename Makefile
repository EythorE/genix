# Genix top-level Makefile

.PHONY: all emu kernel tools disk run test clean

all: emu kernel tools disk

# Build the workbench emulator (host binary)
emu:
	$(MAKE) -C emu

# Build the kernel (m68k binary)
kernel:
	$(MAKE) -C kernel

# Build host tools (mkfs etc.)
tools:
	$(MAKE) -C tools

# Create a filesystem image
disk: tools
	tools/mkfs.minifs disk.img 512

# Run the kernel in the emulator
run: emu kernel disk
	emu/emu68k kernel/kernel.bin disk.img

# Build for Mega Drive
megadrive: disk
	$(MAKE) -C pal/megadrive

# Run host unit tests (no cross-compiler needed)
test:
	$(MAKE) -C tests check

clean:
	$(MAKE) -C emu clean
	$(MAKE) -C kernel clean
	$(MAKE) -C tools clean
	$(MAKE) -C tests clean
	rm -f disk.img
