# Genix top-level Makefile

.PHONY: all emu kernel tools libc apps disk run test test-md megadrive clean

all: emu kernel tools apps disk

# Build the workbench emulator (host binary)
emu:
	$(MAKE) -C emu

# Build the kernel (m68k binary)
kernel:
	$(MAKE) -C kernel

# Build host tools (mkfs, mkbin)
tools:
	$(MAKE) -C tools

# Build the C library for user programs
libc:
	$(MAKE) -C libc

# Build user programs
apps: libc tools
	$(MAKE) -C apps

# Collect all user binaries
APP_BINS = $(wildcard apps/hello apps/echo apps/cat)

# Create a filesystem image with user programs
disk: tools apps
	tools/mkfs.minifs disk.img 512 $(APP_BINS)

# Run the kernel in the emulator
run: emu kernel disk
	emu/emu68k kernel/kernel.bin disk.img

# Build for Mega Drive
megadrive: disk
	$(MAKE) -C pal/megadrive

# Run host unit tests (no cross-compiler needed)
test:
	$(MAKE) -C tests check

# Boot Mega Drive ROM headless in BlastEm (~5s smoke test)
BLASTEM ?= blastem
test-md: megadrive
	timeout 30 $(BLASTEM) -b 300 pal/megadrive/genix-md.bin

clean:
	$(MAKE) -C emu clean
	$(MAKE) -C kernel clean
	$(MAKE) -C tools clean
	$(MAKE) -C libc clean
	$(MAKE) -C apps clean
	$(MAKE) -C tests clean
	rm -f disk.img
