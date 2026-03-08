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

# Build user programs (workbench — linked at 0x040000)
apps: libc tools
	$(MAKE) -C apps

# Build user programs for Mega Drive (linked at 0xFF8000)
apps-md: libc tools
	$(MAKE) -C apps clean
	$(MAKE) -C apps LDSCRIPT=user-md.ld

# Collect all user binaries
APP_BINS = $(wildcard apps/hello apps/echo apps/cat)

# Create a filesystem image with user programs
disk: tools apps
	tools/mkfs.minifs disk.img 512 $(APP_BINS)

# Create Mega Drive filesystem image (programs linked at 0xFF8000)
disk-md: tools apps-md
	tools/mkfs.minifs disk-md.img 512 $(APP_BINS)

# Run the kernel in the emulator
run: emu kernel disk
	emu/emu68k kernel/kernel.bin disk.img

# Build for Mega Drive (uses MD-specific disk image)
megadrive: disk-md
	$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img

# Run host unit tests (no cross-compiler needed)
test:
	$(MAKE) -C tests check

# Boot Mega Drive ROM headless in BlastEm (~5s smoke test)
# Runs BlastEm under Xvfb with OpenGL disabled. A timeout exit (rc=124)
# means the ROM ran without crashing — that's a pass.
BLASTEM ?= blastem
test-md: megadrive
	@echo "=== test-md: headless BlastEm boot ==="
	@Xvfb :57 -screen 0 640x480x24 >/dev/null 2>&1 & xvfb_pid=$$!; \
	sleep 1; \
	DISPLAY=:57 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy \
		timeout 5 $(BLASTEM) -g pal/megadrive/genix-md.bin >/dev/null 2>&1; \
	rc=$$?; kill $$xvfb_pid 2>/dev/null; wait $$xvfb_pid 2>/dev/null; \
	if [ $$rc -eq 124 ]; then echo "OK (ROM ran 5s without crash)"; \
	elif [ $$rc -eq 0 ]; then echo "OK"; \
	else echo "FAIL (exit code $$rc)"; exit 1; fi

clean:
	$(MAKE) -C emu clean
	$(MAKE) -C kernel clean
	$(MAKE) -C tools clean
	$(MAKE) -C libc clean
	$(MAKE) -C apps clean
	$(MAKE) -C tests clean
	rm -f disk.img disk-md.img
