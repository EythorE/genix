# Automated Testing

How to run automated tests at every level — from host logic tests to real
Mega Drive hardware validation.

## Philosophy

**The Mega Drive is the real target.** Every test that passes only in the
workbench emulator is a false positive until validated on BlastEm (or real
hardware). The workbench emulator exists to accelerate development, not to
replace hardware testing.

**Key rule: if a test passes in the workbench emulator but fails in BlastEm
(or vice versa), this is a bug in the emulator, not just a test failure.**
The discrepancy must be investigated, root-caused, and the emulator improved
to match real 68000/Mega Drive behavior more closely. This feedback loop
keeps the emulator honest and prevents divergence from real hardware.

Common causes of emu/BlastEm discrepancies:
- **Alignment**: Workbench silently handles unaligned access (unless
  `STRICT_ALIGN=1`), real 68000 bus-faults
- **Interrupt timing**: Workbench fires timer interrupts at approximate
  cycle counts; BlastEm models VBlank timing accurately
- **Memory map**: Workbench has 1 MB flat RAM; Mega Drive has 64 KB main
  RAM + ROM + optional SRAM with different address spaces
- **VDP behavior**: Workbench has no VDP; Mega Drive text output goes
  through VDP tile rendering
- **Illegal instructions**: Workbench Musashi may accept 68020 opcodes
  that BlastEm (correctly) rejects for a 68000

## Current State

| Level | Target | Automated? | Make Target | Method |
|-------|--------|-----------|-------------|--------|
| 1 | Host unit tests | Yes | `make test` | Pure logic, host gcc, no hardware |
| 2 | Cross-compilation | Yes | `make kernel` | Catches ABI/declaration errors |
| 3 | Workbench emulator | Yes | `make test-emu` | AUTOTEST kernel + STRICT_ALIGN |
| 4 | Mega Drive build | Yes | `make megadrive` | Cross-compile for MD memory layout |
| 5 | BlastEm smoke test | Yes | `make test-md` | Headless 5s boot, crash = fail |
| 6 | BlastEm autotest | Yes | `make test-md-auto` | AUTOTEST ROM, headless 10s |
| 7 | BlastEm screenshot | Semi | `make test-md-screenshot` | Visual VDP inspection |
| 8 | Real hardware | Manual | — | Flash cartridge, Saturn keyboard |

## Make Targets

### `make test` — Host Unit Tests

Runs `tests/test_string`, `tests/test_mem`, `tests/test_exec` on the host.
No cross-compiler needed. Tests pure logic: memory allocator, string
functions, binary header validation, stack layout.

```bash
make test
```

**What it catches:** Logic bugs, off-by-one errors, boundary conditions.
**What it misses:** ABI issues, alignment, hardware interactions.

### `make kernel` — Cross-Compilation Check

Compiles the full kernel with `m68k-linux-gnu-gcc -m68000`. Does not run
anything — just verifies that all declarations match, all symbols resolve,
and the compiler doesn't emit 68020 instructions.

```bash
make kernel
```

**What it catches:** Missing declarations, type mismatches, ABI errors.
**What it misses:** Runtime behavior.

### `make test-emu` — Workbench Autotest

Rebuilds the kernel with `-DAUTOTEST`, runs it in the workbench emulator
with `STRICT_ALIGN=1`, and greps stdout for `AUTOTEST PASSED`.

```bash
make test-emu
```

The AUTOTEST kernel (`kernel/main.c:autotest()`) runs a hardcoded test
sequence instead of starting the interactive shell:
1. `exec /bin/hello` — basic exec
2. `exec /bin/echo` with args — argument passing
3. `exec /bin/true` — exit code 0
4. `exec /bin/false` — exit code 1
5. `exec /bin/wc` on a file — file argument
6. `exec /bin/nonexistent` — error handling
7. `ls /bin` — filesystem traversal

Output markers: `=== AUTOTEST BEGIN ===`, `[test] name: PASS/FAIL`,
`=== AUTOTEST DONE: N passed, M failed ===`, `AUTOTEST PASSED`.

The emulator restores the normal (non-AUTOTEST) kernel after the test,
even on failure.

**What it catches:** Exec, syscalls, filesystem, argument passing, and
alignment bugs (STRICT_ALIGN).
**What it misses:** VDP rendering, Mega Drive memory layout, real timing.

### `make test-md` — BlastEm Smoke Test

Boots the normal (non-AUTOTEST) Mega Drive ROM headless under Xvfb for
5 seconds. A timeout (exit code 124) means it ran without crashing —
that's a pass. Any other non-zero exit code means the ROM crashed.

```bash
make test-md
```

Requires: `blastem`, `Xvfb`.

Set `BLASTEM` to override the BlastEm binary path:
```bash
make test-md BLASTEM=/path/to/blastem
```

**What it catches:** Address errors, illegal instructions, bus faults,
VDP init problems, ROM header issues — everything that crashes the
real 68000.
**What it misses:** Functional correctness (only tests "did it crash?").

### `make test-md-auto` — BlastEm Autotest

Builds an AUTOTEST Mega Drive ROM and runs it headless under Xvfb for
10 seconds. Same crash detection as `test-md`, but with the automated
test sequence running on the Mega Drive memory layout.

```bash
make test-md-auto
```

This is the most important automated test. It exercises the full Mega
Drive code path: 64 KB RAM, MD memory layout, VDP console output,
romdisk filesystem, user program loading at `0xFF8000`.

Restores the normal ROM and workbench apps after the test.

**What it catches:** Everything `test-emu` catches, plus Mega Drive-specific
issues (memory layout, VDP, linker script, romdisk).

### `make test-md-screenshot` — Visual Verification

Same as `test-md-auto` but captures a screenshot of the BlastEm window
after ~7 seconds. Saves to `test-md-screenshot.png`.

```bash
make test-md-screenshot
```

Requires: `blastem`, `Xvfb`, `xdotool`, `scrot`.

Useful for debugging VDP rendering issues — font corruption, missing
characters, screen positioning.

## Kernel AUTOTEST Implementation

The autotest code lives in `kernel/main.c` behind `#ifdef AUTOTEST`.
When compiled with `-DAUTOTEST`, `kmain()` calls `autotest()` instead
of `builtin_shell()`.

After the test sequence completes, the kernel spins with `nop` (not
`STOP`) so VBlank interrupts keep firing — some BlastEm versions exit
immediately on `STOP #0x2700`.

To add a new autotest:
1. Add a test case in `autotest()` in `kernel/main.c`
2. Print `[test] name: PASS` or `[test] name: FAIL`
3. Increment `pass` or `fail` counter
4. Run `make test-emu` and `make test-md-auto` to verify both paths

## Workbench Emulator Accuracy

The workbench emulator (`emu/emu68k.c`) uses the Musashi 68000 core.
It intentionally models real 68000 constraints to catch bugs early:

### STRICT_ALIGN mode

Set `STRICT_ALIGN=1` to enable strict alignment checking. The real
68000 bus-faults on word/long access at odd addresses. Musashi
normally handles these silently, hiding bugs that crash on real hardware.

`make test-emu` always enables STRICT_ALIGN.

### Clock speed

The emulator runs at ~7.67 MHz (matching the Mega Drive's 68000 clock)
with timer interrupts at 100 Hz. This approximates real timing but is
not cycle-accurate.

### Known emulator limitations

These are areas where the workbench emulator differs from real Mega Drive
hardware. Each is a potential source of emu/BlastEm discrepancies:

| Area | Workbench | Mega Drive / BlastEm |
|------|-----------|---------------------|
| RAM | 1 MB flat | 64 KB main + ROM + optional SRAM |
| Display | UART (stdout) | VDP tile rendering |
| Input | stdin | Saturn keyboard / controller |
| Disk | Host file I/O | Romdisk in ROM |
| Interrupts | Level 6 timer | VBlank (level 6), HBlank (level 4) |
| Alignment | Configurable | Always strict |
| Z80 | Not present | Present (sound CPU, bus arbitration) |

**When you find a discrepancy:** File it as a bug, root-cause it, and
decide whether the emulator should be improved to catch the issue earlier.
The goal is that if code works in `make test-emu`, it should also work in
`make test-md-auto` — and vice versa. Any divergence erodes trust in the
emulator as a development tool.

## Discrepancy Investigation Procedure

When a test passes in the emulator but fails in BlastEm (or vice versa):

1. **Identify the failure**: Which test? What's the error (crash, wrong
   output, hang)?

2. **Reproduce minimally**: Strip the test down to the smallest case that
   shows the discrepancy.

3. **Root-cause**: Use BlastEm's GDB stub (`-D` flag) to inspect the
   failing point:
   ```bash
   m68k-linux-gnu-gdb -q --tui \
       -ex "target remote | blastem -D pal/megadrive/genix-md.bin" \
       pal/megadrive/genix-md.elf
   ```

4. **Classify**: Is this a:
   - **Kernel bug** exposed by one platform but not the other?
   - **Emulator bug** where Musashi doesn't match real 68000 behavior?
   - **Build/link difference** between workbench and Mega Drive builds?
   - **Memory layout issue** (works in 1 MB, fails in 64 KB)?

5. **Fix the right thing**:
   - Kernel bug → fix the kernel, verify on both platforms
   - Emulator gap → improve the emulator (add checking, match behavior)
   - Build difference → ensure both builds exercise the same code paths
   - Memory issue → the Mega Drive build is correct; adjust code to fit

6. **Add a regression test**: If possible, add a host test that catches
   the logic bug, and verify the fix passes on both `test-emu` and
   `test-md-auto`.

## Testing Ladder

Run these in order. Each level catches different classes of bugs:

```
make test          # 1. Logic bugs (fast, no cross-compiler)
make kernel        # 2. ABI/declaration errors
make test-emu      # 3. Runtime + alignment (workbench)
make megadrive     # 4. Mega Drive build (link errors, memory layout)
make test-md       # 5. Mega Drive boot (crash detection)
make test-md-auto  # 6. Mega Drive functional tests (the real gate)
```

**Levels 1-3** are for rapid iteration during development.
**Levels 4-6** must pass before considering a change done.
**Level 6 (`test-md-auto`) is the primary quality gate** — if it
passes, the code is likely correct on real hardware.

For thorough validation, also run:
```
make test-md-screenshot   # Visual VDP check
blastem pal/megadrive/genix-md.bin   # Interactive BlastEm session
```

And ultimately, real hardware with a flash cartridge.

## Future Directions

### Test binary approach

Build a dedicated `apps/test_syscalls.c` that exercises syscalls from
userspace and reports PASS/FAIL. Auto-exec'd in AUTOTEST builds. This
tests the full syscall path (TRAP #0) rather than calling kernel
functions directly.

### BlastEm GDB automation

Use BlastEm's `-D` flag with scripted GDB to inspect memory at known
addresses after test completion. More precise than screenshot parsing.

```bash
m68k-linux-gnu-gdb -batch \
    -ex "target remote | blastem -D pal/megadrive/genix-md.bin" \
    -ex "break *0x<test_done_addr>" \
    -ex "continue" \
    -ex "x/s 0xFF8000" \
    -ex "quit"
```

### Shell script execution

Once the shell supports scripts, embed test scripts in the filesystem
and have the kernel auto-exec them. Cleanest long-term approach.

### BlastEm stdout capture

BlastEm may support logging VDP text output to stdout in future
versions, which would allow grepping functional test results the same
way the workbench emulator does today.

### Emulator accuracy improvements

Priority areas for making the workbench emulator match real hardware:
- Add illegal instruction detection (reject 68020 opcodes)
- Add memory bounds checking (reject accesses outside valid ranges)
- Model 64 KB RAM constraint (optional mode)
- Approximate VBlank timing instead of fixed-rate timer
