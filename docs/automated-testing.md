# Automated Guest-Level Testing

How to run automated tests *inside* Genix (not just host unit tests).

## Current State

| Target | Automated? | Method |
|--------|-----------|--------|
| Host unit tests | Yes | `make test` — pure logic, no hardware |
| Workbench emulator | Yes | Pipe commands to stdin: `printf 'cmd\nhalt\n' \| emu/emu68k kernel.bin disk.img` |
| BlastEm (Mega Drive) | Boot-only | `make test-md` — runs 300 frames, checks exit code (crash = fail) |

The workbench emulator reads from stdin and quits cleanly on EOF, so it
can already run scripted command sequences. BlastEm cannot accept piped
input — it reads from SDL keyboard events only.

## Approaches

### 1. Emulator: Pipe Scripts (works today)

The workbench emulator reads from `STDIN_FILENO`. You can pipe arbitrary
shell commands and check stdout for expected output:

```bash
printf 'exec /bin/hello\nhalt\n' | timeout 10 emu/emu68k kernel/kernel.bin disk.img
```

**Pros:** Works right now, no code changes needed, fast, easy to grep output.
**Cons:** Only tests the workbench PAL, not the Mega Drive build. Cannot test
VDP rendering, Saturn keyboard, or Mega Drive-specific hardware.

**CI-friendly wrapper:**
```bash
#!/bin/sh
output=$(printf 'exec /bin/hello\nhalt\n' | timeout 10 emu/emu68k kernel/kernel.bin disk.img 2>&1)
echo "$output" | grep -q "Hello from userspace!" || { echo "FAIL"; exit 1; }
echo "PASS"
```

### 2. Kernel Autorun Mode (small code change)

Add a compile-time or runtime flag that makes the kernel execute a
predetermined command sequence instead of reading from the keyboard.
This works on *both* emulator and BlastEm.

**Implementation:** Add `#ifdef AUTOTEST` to `builtin_shell()` or
`kernel_main()` that runs a hardcoded command list:

```c
#ifdef AUTOTEST
static const char *autotest_cmds[] = {
    "exec /bin/hello",
    "exec /bin/echo test passed",
    "mem",
    "halt",
    NULL
};
// Feed these instead of calling kgetc()
#endif
```

Or simpler: a dedicated `autotest()` function called instead of
`builtin_shell()` that exercises specific subsystems and prints
PASS/FAIL markers.

**Pros:** Tests the real kernel on the real platform. Works in BlastEm
headless mode (`-b 600`). No input simulation needed.
**Cons:** Requires rebuild per test suite. Can't easily parameterize
without a shell script parser.

### 3. ROM-Embedded Test Binary (best for syscall testing)

Build a special test program (`apps/test_syscalls.c`) that exercises
syscalls and prints results, then build a ROM that auto-execs it:

```c
// apps/test_syscalls.c
#include <libc.h>

int main(int argc, char **argv) {
    // Test write syscall
    write(1, "test_write: ", 12);
    int n = write(1, "OK\n", 3);
    if (n != 3) { write(1, "FAIL\n", 5); _exit(1); }

    // Test open + read
    int fd = open("/bin/hello", 0);
    if (fd < 0) { write(1, "test_open: FAIL\n", 16); _exit(1); }
    write(1, "test_open: OK\n", 14);
    close(fd);

    write(1, "ALL TESTS PASSED\n", 17);
    _exit(0);
}
```

Then in the kernel, `AUTOTEST` mode auto-execs `/bin/test_syscalls`
instead of starting the interactive shell. The output appears on the
workbench UART (greppable) and on the Mega Drive VDP (visible in
screenshots).

### 4. BlastEm Screenshots (visual verification)

BlastEm supports `ui.screenshot` bound to a key (default: unbound).
You can also configure `screenshot_path` in `blastem.cfg`.

**The trick:** Combine autorun mode (#2) with BlastEm's `-b` flag.
The test ROM runs its commands, prints results to the VDP, and then
halts. BlastEm renders the frames and exits. We grab a screenshot
before exit.

**Setup:**
```bash
# In blastem.cfg, bind screenshot to a key or use -b with a savestate
screenshot_path /tmp/genix-test
```

**Xvfb + screenshot approach:**
```bash
Xvfb :57 -screen 0 640x480x24 &
DISPLAY=:57 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy \
    timeout 10 blastem -g pal/megadrive/genix-md.bin &

# Wait for boot + test completion, then grab the framebuffer
sleep 5
DISPLAY=:57 import -window root /tmp/genix-screenshot.png
kill %1 %2
```

Then either eyeball the screenshot or run OCR/pixel-matching on it.

**Pros:** Tests the actual VDP output on the Mega Drive build. Can catch
rendering bugs, font issues, screen corruption.
**Cons:** Fragile (timing-dependent), slow, hard to parse programmatically.
Best as a supplement, not the primary test method.

### 5. BlastEm GDB Stub (inspect memory directly)

BlastEm's `-D` flag starts a GDB remote stub. You can script GDB to:
- Wait for the kernel to reach a known address (breakpoint)
- Read memory at a "test result" location
- Check registers or specific RAM contents

```bash
# Run BlastEm with GDB pipe
m68k-linux-gnu-gdb -batch \
    -ex "target remote | blastem -D pal/megadrive/genix-md.bin" \
    -ex "break *0x<test_done_addr>" \
    -ex "continue" \
    -ex "x/s 0xFF8000" \
    -ex "quit"
```

**Kernel-side convention:** The autotest writes a result string to a
known RAM address (e.g., `0xFF8000 = "ALL TESTS PASSED"`) and then
hits an infinite loop or a specific `ILLEGAL` instruction. GDB catches
the breakpoint and reads the result.

**Pros:** Precise — can inspect any memory/register. No VDP rendering
needed. Works with `-D` pipe mode (no display required).
**Cons:** Requires knowing addresses (use the .elf symbol table).
GDB scripting is finicky. Slower than the emulator pipe approach.

### 6. Shell Script Execution (future — needs init/exec)

Once the shell can execute scripts from the filesystem, embed a test
script in the disk image:

```
# /etc/autotest.sh (or read from romdisk)
exec /bin/test_syscalls
exec /bin/test_fs
echo ALL DONE
halt
```

The kernel's init process runs `/bin/sh /etc/autotest.sh` instead of
an interactive shell. This is the cleanest long-term approach but
requires a working shell with script support.

## Recommended Strategy

**Short-term (works now):**

1. **Emulator pipe tests** for kernel commands and exec — already
   works, add more `printf ... | emu68k` test cases to the Makefile.
2. **`make test-md`** continues to catch Mega Drive build crashes.

**Next step (small code change):**

3. **Autorun mode** (`-DAUTOTEST`) — hardcoded command list in the
   kernel. One rebuild tests both workbench and BlastEm. Combined
   with `-b 600` on BlastEm, this is fully headless and CI-ready.

**Medium-term (when exec is solid):**

4. **Test binary** (`apps/test_syscalls.c`) that exercises the syscall
   interface and reports PASS/FAIL. Auto-exec'd in test builds.

**Nice-to-have:**

5. **GDB stub** for targeted memory inspection on Mega Drive builds.
6. **Screenshots** for VDP rendering validation.

## Make Targets (proposed)

```makefile
# Scripted emulator tests (pipe commands, check output)
test-emu: emu kernel apps disk
	@echo "=== Scripted emulator tests ==="
	@printf 'exec /bin/hello\nhalt\n' | timeout 10 emu/emu68k kernel/kernel.bin disk.img 2>&1 \
		| grep -q "Hello from userspace!" && echo "PASS: exec hello" || echo "FAIL: exec hello"

# Autotest ROM (BlastEm headless)
test-md-auto: megadrive  # requires AUTOTEST=1 build
	@echo "=== BlastEm autotest ==="
	@DISPLAY=:57 ... blastem -b 600 -g pal/megadrive/genix-md.bin 2>&1

# Full test ladder
test-all: test test-emu test-md
```
