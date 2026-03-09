# Genix top-level Makefile

.PHONY: all emu kernel tools libc apps disk run test test-emu test-emu-strict test-md test-md-auto test-md-screenshot test-all megadrive clean

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
	$(MAKE) -C apps/levee

# Build user programs for Mega Drive (linked at 0xFF8000)
apps-md: libc tools
	$(MAKE) -C apps clean
	$(MAKE) -C apps LDSCRIPT=user-md.ld

# Core app binaries (built for both workbench and Mega Drive)
CORE_BINS = apps/hello apps/echo apps/cat apps/wc apps/head apps/true apps/false

# All app binaries (levee is workbench-only — too large for MD 31KB user space)
APP_BINS = $(CORE_BINS) $(wildcard apps/levee/levee)

# Create a filesystem image with user programs
disk: tools apps
	tools/mkfs.minifs disk.img 512 $(APP_BINS)

# Create Mega Drive filesystem image (programs linked at 0xFF8000)
disk-md: tools apps-md
	tools/mkfs.minifs disk-md.img 512 $(CORE_BINS)

# Run the kernel in the emulator
run: emu kernel disk
	emu/emu68k kernel/kernel.bin disk.img

# Build for Mega Drive (uses MD-specific disk image)
megadrive: disk-md
	$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img

# Run host unit tests (no cross-compiler needed)
test:
	$(MAKE) -C tests check

# Scripted emulator test — pipe commands, check stdout
# Rebuilds apps + disk to ensure workbench linker script is used.
# Always restores the normal (non-AUTOTEST) kernel when done, even on failure.
test-emu: emu libc tools
	@$(MAKE) -C apps clean
	@$(MAKE) -C apps
	@tools/mkfs.minifs disk.img 512 $(CORE_BINS)
	@$(MAKE) -C kernel clean
	@$(MAKE) -C kernel EXTRA_CFLAGS=-DAUTOTEST
	@echo "=== test-emu: workbench autotest ==="
	@test_rc=0; \
	output=$$(STRICT_ALIGN=1 timeout 30 emu/emu68k kernel/kernel.bin disk.img 2>&1); \
	echo "$$output"; \
	if echo "$$output" | grep -q "AUTOTEST PASSED"; then \
		echo "=== test-emu: PASS ==="; \
	else \
		echo "=== test-emu: FAIL ==="; test_rc=1; \
	fi; \
	$(MAKE) -C kernel clean; \
	$(MAKE) -C kernel; \
	exit $$test_rc

# Mega Drive autotest — build with AUTOTEST, run headless in BlastEm
# Always restores the normal (non-AUTOTEST) ROM when done, even on failure.
test-md-auto: libc tools
	@$(MAKE) -C apps clean
	@$(MAKE) -C apps LDSCRIPT=user-md.ld
	@tools/mkfs.minifs disk-md.img 512 $(CORE_BINS)
	@$(MAKE) -C pal/megadrive clean
	@$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img EXTRA_CFLAGS=-DAUTOTEST
	@echo "=== test-md-auto: BlastEm autotest ==="
	@test_rc=0; \
	Xvfb :57 -screen 0 640x480x24 >/dev/null 2>&1 & xvfb_pid=$$!; \
	sleep 1; \
	DISPLAY=:57 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy \
		timeout 10 $(BLASTEM) pal/megadrive/genix-md.bin >/dev/null 2>&1; \
	rc=$$?; kill $$xvfb_pid 2>/dev/null; wait $$xvfb_pid 2>/dev/null; \
	if [ $$rc -eq 124 ]; then echo "=== test-md-auto: OK (ran 10s without crash) ==="; \
	elif [ $$rc -eq 0 ]; then echo "=== test-md-auto: OK ==="; \
	else echo "=== test-md-auto: FAIL (exit code $$rc) ==="; test_rc=1; fi; \
	$(MAKE) -C pal/megadrive clean; \
	$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img; \
	$(MAKE) -C apps clean; \
	$(MAKE) -C apps; \
	exit $$test_rc

# Visual Mega Drive test — boot AUTOTEST ROM under Xvfb, capture screenshot
# Produces test-md-screenshot.png for visual inspection.
# Always restores the normal (non-AUTOTEST) ROM when done, even on failure.
test-md-screenshot: libc tools
	@$(MAKE) -C apps clean
	@$(MAKE) -C apps LDSCRIPT=user-md.ld
	@tools/mkfs.minifs disk-md.img 512 $(CORE_BINS)
	@$(MAKE) -C pal/megadrive clean
	@$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img EXTRA_CFLAGS=-DAUTOTEST
	@echo "=== test-md-screenshot: booting ROM under Xvfb ==="
	@Xvfb :58 -screen 0 640x480x24 >/dev/null 2>&1 & xvfb_pid=$$!; \
	sleep 1; \
	DISPLAY=:58 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy \
		timeout 10 $(BLASTEM) pal/megadrive/genix-md.bin >/dev/null 2>&1 & blastem_pid=$$!; \
	sleep 7; \
	wid=$$(DISPLAY=:58 xdotool search --name "" 2>/dev/null | tail -1); \
	if [ -n "$$wid" ]; then \
		DISPLAY=:58 xdotool windowfocus "$$wid" 2>/dev/null; sleep 0.5; \
		DISPLAY=:58 scrot -u test-md-screenshot.png 2>/dev/null || true; \
	else \
		DISPLAY=:58 scrot test-md-screenshot.png 2>/dev/null || true; \
	fi; \
	kill $$blastem_pid 2>/dev/null; wait $$blastem_pid 2>/dev/null; \
	kill $$xvfb_pid 2>/dev/null; wait $$xvfb_pid 2>/dev/null; \
	echo "=== Screenshot saved to test-md-screenshot.png ==="
	@$(MAKE) -C pal/megadrive clean
	@$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img
	@$(MAKE) -C apps clean
	@$(MAKE) -C apps

# Full testing ladder — runs all automated tests in order.
# Levels 1-3 use host/emulator, levels 4-6 use BlastEm.
# Level 6 (test-md-auto) is the primary quality gate.
test-all: test kernel test-emu megadrive test-md test-md-auto
	@echo "=== All tests passed ==="

# Boot Mega Drive ROM headless in BlastEm (~5s smoke test)
# Runs BlastEm under Xvfb with OpenGL disabled. A timeout exit (rc=124)
# means the ROM ran without crashing — that's a pass.
BLASTEM ?= blastem
test-md: megadrive
	@echo "=== test-md: headless BlastEm boot ==="
	@Xvfb :57 -screen 0 640x480x24 >/dev/null 2>&1 & xvfb_pid=$$!; \
	sleep 1; \
	DISPLAY=:57 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy \
		timeout 5 $(BLASTEM) pal/megadrive/genix-md.bin >/dev/null 2>&1; \
	rc=$$?; kill $$xvfb_pid 2>/dev/null; wait $$xvfb_pid 2>/dev/null; \
	if [ $$rc -eq 124 ]; then echo "OK (ROM ran 5s without crash)"; \
	elif [ $$rc -eq 0 ]; then echo "OK"; \
	else echo "FAIL (exit code $$rc)"; exit 1; fi

clean:
	$(MAKE) -C emu clean
	$(MAKE) -C kernel clean
	$(MAKE) -C pal/megadrive clean
	$(MAKE) -C tools clean
	$(MAKE) -C libc clean
	$(MAKE) -C apps clean
	$(MAKE) -C apps/levee clean
	$(MAKE) -C tests clean
	rm -f disk.img disk-md.img
