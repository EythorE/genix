# Genix top-level Makefile

.PHONY: all emu kernel tools libc apps disk disk-md run test test-opcodes test-dash test-levee test-emu test-md test-md-auto test-md-screenshot test-md-imshow test-all megadrive clean

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

# Build user programs (relocatable binaries, linked at address 0)
apps: libc tools
	$(MAKE) -C apps
	$(MAKE) -C apps/levee
	$(MAKE) -C apps/dash

# Core app binaries (built for both workbench and Mega Drive)
CORE_BINS = apps/hello apps/echo apps/cat apps/wc apps/head apps/true apps/false \
            apps/tail apps/tee apps/yes apps/basename apps/dirname \
            apps/rev apps/nl apps/cmp apps/cut apps/tr apps/uniq apps/imshow \
            apps/ls apps/sleep apps/strings apps/fold apps/expand apps/unexpand \
            apps/paste apps/comm apps/seq apps/tac \
            apps/grep apps/od apps/env apps/expr \
            apps/dash/dash

# All app binaries (levee is workbench-only — too large for MD 31KB user space)
APP_BINS = $(CORE_BINS) $(wildcard apps/levee/levee)

# Create a filesystem image with user programs
disk: tools apps
	tools/mkfs.minifs disk.img 512 $(APP_BINS)

# Create Mega Drive filesystem image (same relocatable binaries)
disk-md: tools apps
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
# Rebuilds apps + disk, then runs AUTOTEST kernel under workbench emulator.
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
# Uses -b 600 (~10s at 60fps) for truly headless operation (no Xvfb needed).
# Always restores the normal (non-AUTOTEST) ROM when done, even on failure.
test-md-auto: libc tools
	@$(MAKE) -C apps
	@tools/mkfs.minifs disk-md.img 512 $(CORE_BINS)
	@$(MAKE) -C pal/megadrive clean
	@$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img EXTRA_CFLAGS=-DAUTOTEST
	@echo "=== test-md-auto: BlastEm autotest ==="
	@test_rc=0; \
	$(BLASTEM) -b 600 pal/megadrive/genix-md.bin >/dev/null 2>&1; \
	rc=$$?; \
	if [ $$rc -eq 0 ]; then echo "=== test-md-auto: OK (600 frames, no crash) ==="; \
	else echo "=== test-md-auto: FAIL (exit code $$rc) ==="; test_rc=1; fi; \
	$(MAKE) -C pal/megadrive clean; \
	$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img; \
	exit $$test_rc

# Visual Mega Drive test — boot AUTOTEST ROM under Xvfb, capture screenshot.
# Uses scrot to capture the BlastEm window. BlastEm's native screenshot (p key)
# can't be triggered externally because SDL2 uses XInput2 for keyboard input,
# which doesn't receive XTest/xdotool synthetic key events under Xvfb.
# Produces test-md-screenshot.png for visual inspection.
# Requires: Xvfb, xdotool, scrot.
# Always restores the normal (non-AUTOTEST) ROM when done, even on failure.
test-md-screenshot: libc tools
	@$(MAKE) -C apps
	@tools/mkfs.minifs disk-md.img 512 $(CORE_BINS)
	@$(MAKE) -C pal/megadrive clean
	@$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img EXTRA_CFLAGS=-DAUTOTEST
	@echo "=== test-md-screenshot: booting ROM under Xvfb ==="
	@mkdir -p $(BLASTEM_CFG_DIR); \
	printf '$(BLASTEM_HEADLESS_CFG)' > $(BLASTEM_CFG_DIR)/blastem.cfg; \
	Xvfb :58 -screen 0 640x480x24 >/dev/null 2>&1 & xvfb_pid=$$!; \
	sleep 1; \
	HOME=/tmp/blastem-test-home DISPLAY=:58 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy \
		timeout -k 3 15 $(BLASTEM) pal/megadrive/genix-md.bin >/dev/null 2>&1 & blastem_pid=$$!; \
	sleep 7; \
	wid=$$(DISPLAY=:58 xdotool search --name "BlastEm" 2>/dev/null | tail -1); \
	if [ -n "$$wid" ]; then \
		DISPLAY=:58 xdotool windowfocus --sync "$$wid" 2>/dev/null; sleep 0.5; \
		DISPLAY=:58 scrot -u test-md-screenshot.png 2>/dev/null || \
		DISPLAY=:58 scrot test-md-screenshot.png 2>/dev/null || true; \
	else \
		DISPLAY=:58 scrot test-md-screenshot.png 2>/dev/null || true; \
	fi; \
	kill $$blastem_pid 2>/dev/null; kill -9 $$blastem_pid 2>/dev/null; wait $$blastem_pid 2>/dev/null; \
	kill $$xvfb_pid 2>/dev/null; wait $$xvfb_pid 2>/dev/null; \
	if [ -f test-md-screenshot.png ]; then \
		echo "=== Screenshot saved to test-md-screenshot.png ==="; \
	else \
		echo "=== WARNING: no screenshot captured (missing xdotool/scrot?) ==="; \
	fi
	@$(MAKE) -C pal/megadrive clean
	@$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img

# imshow screenshot test — spawn imshow in no-wait mode, capture the VDP
# color bar output. Validates the full graphics stack: kernel VDP driver,
# ioctl interface, libgfx, and userspace rendering.
# Produces test-md-imshow.png for visual inspection.
# Requires: Xvfb, xdotool, scrot, BlastEm.
# Always restores the normal ROM when done, even on failure.
test-md-imshow: libc tools
	@$(MAKE) -C apps
	@tools/mkfs.minifs disk-md.img 512 $(CORE_BINS)
	@$(MAKE) -C pal/megadrive clean
	@$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img EXTRA_CFLAGS=-DIMSHOW_TEST
	@echo "=== test-md-imshow: booting imshow ROM under Xvfb ==="
	@mkdir -p $(BLASTEM_CFG_DIR); \
	printf '$(BLASTEM_HEADLESS_CFG)' > $(BLASTEM_CFG_DIR)/blastem.cfg; \
	Xvfb :59 -screen 0 640x480x24 >/dev/null 2>&1 & xvfb_pid=$$!; \
	sleep 1; \
	HOME=/tmp/blastem-test-home DISPLAY=:59 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy \
		timeout -k 3 15 $(BLASTEM) pal/megadrive/genix-md.bin >/dev/null 2>&1 & blastem_pid=$$!; \
	sleep 5; \
	wid=$$(DISPLAY=:59 xdotool search --name "BlastEm" 2>/dev/null | tail -1); \
	if [ -n "$$wid" ]; then \
		DISPLAY=:59 xdotool windowfocus --sync "$$wid" 2>/dev/null; sleep 0.5; \
		DISPLAY=:59 scrot -u test-md-imshow.png 2>/dev/null || \
		DISPLAY=:59 scrot test-md-imshow.png 2>/dev/null || true; \
	else \
		DISPLAY=:59 scrot test-md-imshow.png 2>/dev/null || true; \
	fi; \
	kill $$blastem_pid 2>/dev/null; kill -9 $$blastem_pid 2>/dev/null; wait $$blastem_pid 2>/dev/null; \
	kill $$xvfb_pid 2>/dev/null; wait $$xvfb_pid 2>/dev/null; \
	if [ -f test-md-imshow.png ]; then \
		echo "=== imshow screenshot saved to test-md-imshow.png ==="; \
	else \
		echo "=== WARNING: no screenshot captured (missing xdotool/scrot?) ==="; \
	fi
	@$(MAKE) -C pal/megadrive clean
	@$(MAKE) -C pal/megadrive DISK_IMG=../../disk-md.img

# Check compiled binaries for 68020 illegal opcodes.
# Catches wrong-toolchain issues before they reach hardware.
test-opcodes: kernel apps
	@./scripts/check-opcodes.sh

# Test dash shell boots and runs commands in the workbench emulator.
test-dash: emu kernel disk
	@./scripts/test-dash.sh

# Test levee editor (KNOWN BROKEN — crashes with kernel panic).
# Not included in test-all; run manually to check progress.
test-levee: emu kernel disk
	@./scripts/test-levee.sh

# Full testing ladder — runs all automated tests in order.
# Levels 1-3 use host/emulator, levels 4-6 use BlastEm.
# Level 6 (test-md-auto) is the primary quality gate.
test-all: test kernel test-opcodes test-emu test-dash megadrive test-md test-md-auto
	@echo "=== All tests passed ==="

# Boot Mega Drive ROM headless in BlastEm (~5s smoke test)
# Uses -b 300 (~5s at 60fps) for truly headless operation (no Xvfb needed).
# Exit code 0 = ROM ran without crash; nonzero = crash/error.
BLASTEM ?= blastem

# Helper: BlastEm config for screenshot test (needs Xvfb + a window).
# Disables OpenGL (avoids "Failed to set vsync" dialog under software rendering).
define BLASTEM_HEADLESS_CFG
video {\n\tgl off\n\tvsync off\n}\nsystem {\n\tram_init zero\n}
endef
BLASTEM_CFG_DIR = /tmp/blastem-test-home/.config/blastem

test-md: megadrive
	@echo "=== test-md: headless BlastEm boot ==="
	@$(BLASTEM) -b 300 pal/megadrive/genix-md.bin >/dev/null 2>&1; \
	rc=$$?; \
	if [ $$rc -eq 0 ]; then echo "OK (300 frames, no crash)"; \
	else echo "FAIL (exit code $$rc)"; exit 1; fi

clean:
	$(MAKE) -C emu clean
	$(MAKE) -C kernel clean
	$(MAKE) -C pal/megadrive clean
	$(MAKE) -C tools clean
	$(MAKE) -C libc clean
	$(MAKE) -C apps clean
	$(MAKE) -C apps/levee clean
	$(MAKE) -C apps/dash clean
	$(MAKE) -C tests clean
	rm -f disk.img disk-md.img
