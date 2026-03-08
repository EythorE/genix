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
# Uses -b (native headless) if available, otherwise falls back to Xvfb.
BLASTEM ?= blastem
test-md: megadrive
	@if $(BLASTEM) -h 2>&1 | grep -q -- '-b'; then \
		echo "blastem -b 300 pal/megadrive/genix-md.bin"; \
		timeout 30 $(BLASTEM) -b 300 pal/megadrive/genix-md.bin; \
	elif command -v Xvfb >/dev/null 2>&1; then \
		echo "blastem via Xvfb (no -b support)"; \
		Xvfb :57 -screen 0 640x480x24 >/dev/null 2>&1 & xvfb_pid=$$!; \
		sleep 1; \
		DISPLAY=:57 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy \
			timeout 10 $(BLASTEM) -g pal/megadrive/genix-md.bin >/dev/null 2>&1; \
		rc=$$?; kill $$xvfb_pid 2>/dev/null; \
		if [ $$rc -eq 124 ]; then echo "OK (timeout — ROM ran 10s without crash)"; \
		elif [ $$rc -eq 0 ]; then echo "OK"; \
		else echo "FAIL (exit code $$rc)"; exit 1; fi; \
	else \
		echo "ERROR: neither blastem -b nor Xvfb available" >&2; exit 1; \
	fi

clean:
	$(MAKE) -C emu clean
	$(MAKE) -C kernel clean
	$(MAKE) -C tools clean
	$(MAKE) -C libc clean
	$(MAKE) -C apps clean
	$(MAKE) -C tests clean
	rm -f disk.img
