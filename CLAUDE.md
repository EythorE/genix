# Genix — A Small 68000 OS for Mega Drive

## Project Overview

Genix is a minimal, single-user, POSIX-enough operating system for the Motorola 68000,
targeting the Sega Mega Drive. It replaces the FUZIX kernel with ~3000 lines of new code
while reusing proven Mega Drive drivers from
[EythorE/FUZIX](https://github.com/EythorE/FUZIX/tree/megadrive).

**The Mega Drive is the primary target.** The workbench emulator exists only to
accelerate development — every feature must ultimately run on real Mega Drive hardware.
Design decisions should always consider the Mega Drive's constraints: 64 KB main RAM,
7.67 MHz CPU, no MMU, optional cartridge SRAM.

## Getting Started

### 1. Install Prerequisites

You need a host C compiler and a 68000 cross-compiler.

```bash
# Host compiler + build tools (Ubuntu/Debian)
sudo apt-get install build-essential

# 68000 cross-compiler (quick start — works with Genix's workarounds)
sudo apt-get install gcc-m68k-linux-gnu binutils-m68k-linux-gnu
```

The distro cross-compiler defaults to 68020, but Genix works around this
by passing `-m68000` and providing its own division routines. For a fully
correct 68000 toolchain, build from source — see
[docs/toolchain.md](docs/toolchain.md) for download links and build
instructions.

### 2. Build and Run

```bash
make run       # Build everything + boot in the workbench emulator
```

This builds the emulator, kernel, user programs, and a filesystem image,
then boots Genix in your terminal. You'll see a `>` prompt.

### 3. Try It

```
> ls /bin           # List available programs
> exec /bin/hello   # Run the hello world program
> exec /bin/echo hello world
> mem               # Show memory allocator state
> help              # List all built-in commands
> halt              # Shut down (exits the emulator)
```

Press **Ctrl+]** to force-quit the emulator at any time (like telnet).

See [docs/emulator.md](docs/emulator.md) for full emulator documentation.

### 4. Build the Mega Drive ROM

```bash
make megadrive     # Produces pal/megadrive/genix-md.bin
```

Test in BlastEm:
```bash
blastem pal/megadrive/genix-md.bin
```

On the Mega Drive, Genix uses the VDP for text output and reads input
from a Saturn keyboard connected to controller port 2.

### 5. Run Tests

```bash
make test          # Host unit tests (no cross-compiler needed)
make test-md       # Headless BlastEm boot (~5s smoke test, needs blastem)
```

### All Build Targets

```bash
make emu           # Build workbench emulator only (host binary)
make kernel        # Build kernel only (needs cross-compiler)
make apps          # Build user programs (needs kernel built first)
make disk          # Create filesystem image
make run           # Build all + run in emulator
make megadrive     # Build Mega Drive ROM
make test          # Host unit tests
make test-md       # Headless BlastEm smoke test
make clean         # Remove all build artifacts
```

## Architecture

```
User Programs (sh, apps)  — linked with libc syscall stubs
        │ TRAP #0
Kernel (proc, fs, dev, mem, exec, syscall)
        │
Platform Abstraction Layer (PAL)
        │
Hardware / Emulator
```

## Key Design Decisions

- **Mega Drive first** — real hardware is the target, emulators are for iteration
- **No fork()** — vfork()+exec() only (no MMU required)
- **No multi-user** — no UIDs, permissions, login
- **Custom filesystem (minifs)** — classic Unix inode layout
- **Single-tasking first** — then add multi-tasking
- **Workbench emulator** for rapid development (Musashi 68000 SBC)
- **BlastEm** for Mega Drive validation before real hardware
- **Simple binary format** — flat binary with 32-byte header, relocatable later
- **Modular memory layout** — supports different cartridge configurations
- **SRAM is optional** — system runs on 64 KB main RAM alone; SRAM adds
  persistent storage and extended RAM
- **Design choices are cheap** — if a format/convention is hurting us, change it

## Directory Structure

```
genix/
├── emu/          # Workbench 68000 emulator (Musashi-based)
├── kernel/       # OS kernel
├── pal/          # Platform Abstraction Layer
│   ├── workbench/  # Emulated SBC
│   └── megadrive/  # Sega Mega Drive
├── libc/         # Minimal C library + syscall stubs (for user programs)
├── apps/         # Userspace programs (hello, echo, cat, sh)
├── tools/        # Host tools (mkfs, mkbin)
└── tests/        # Host unit tests
```

## Memory Layout

### Mega Drive (64 KB RAM — primary target)

```
ROM (cartridge):
0x000000  Vectors + Sega header + kernel .text + .rodata + .romdisk

Main RAM (64 KB):
0xFF0000  Kernel .data + .bss (~25 KB)
~0xFF6300 Kernel heap (~1 KB)
~0xFF6800 USER_BASE — user programs load here (~32 KB available)
~0xFFFE00 USER_TOP — user stack starts here (grows down)
0xFFFFFF  Top of RAM / kernel stack

SRAM (optional, cartridge-dependent):
0x200000  Read-write filesystem and/or extended user RAM
```

Kernel uses ~25 KB of the 64 KB main RAM, leaving ~40 KB for heap + user
programs. User programs run entirely in main RAM without SRAM. SRAM provides
persistent storage and optional extra RAM for larger programs.

See `docs/megadrive.md` for cartridge configurations and SRAM details.

### Workbench (1 MB RAM — development only)

```
0x000000  Vectors + Kernel code + data + BSS
~0x008000 Kernel heap (kmalloc)
0x040000  USER_BASE — user programs load here
0x0F0000  USER_TOP  — user stack starts here (grows down)
0x100000  Top of RAM / kernel stack
```

## Binary Format (Genix flat binary)

32-byte big-endian header followed by program text+data:
```c
struct genix_header {
    uint32_t magic;       /* 0x47454E58 "GENX" */
    uint32_t load_size;   /* bytes to load (text+data) */
    uint32_t bss_size;    /* bytes to zero after load */
    uint32_t entry;       /* absolute entry point address */
    uint32_t stack_size;  /* stack hint (0 = default 4KB) */
    uint32_t flags;       /* reserved */
    uint32_t reserved[2]; /* pad to 32 bytes */
};
```

Build flow: `.c → .o → .elf (m68k-linux-gnu-ld) → mkbin → genix binary`

## Syscall Convention

- Enter kernel via `TRAP #0`
- Syscall number in `d0`, arguments in `d1-d4`
- Return value in `d0` (negative = -errno)
- User stubs must save/restore callee-saved regs (d2-d7, a2-a6) they clobber

## Testing

### Debugging with GDB via BlastEm

```bash
m68k-linux-gnu-gdb -q --tui \
    -ex "target remote | blastem -D pal/megadrive/genix-md.bin" \
    pal/megadrive/genix-md.elf
```

### Testing ladder

1. **`make test`** — host unit tests (logic, no hardware)
2. **`make kernel`** — cross-compilation check (catches ABI/declaration errors)
3. **`make run`** — workbench emulator (interactive smoke test)
4. **`make test-md`** — headless BlastEm boot (~5s, no display needed)
5. **`make megadrive` + BlastEm** — interactive Mega Drive ROM validation
6. **Real hardware** — flash cartridge on a real Mega Drive

Steps 1–3 are for rapid iteration. Step 4 is automated and catches address
errors, illegal instructions, and bus faults in the Mega Drive build without
a display. Step 5 adds interactive VDP, keyboard, and SRAM validation.
Step 6 catches everything emulators miss (TMSS, mapper quirks, Z80 bus
conflicts). See `docs/megadrive.md` for details.

---

## Development Guidelines

These guidelines are critical for making this project succeed. Follow them strictly.

### 1. Every change must be testable

- **Host tests first**: Every kernel subsystem (mem, string, fs logic, exec header
  parsing, stack layout) must have a corresponding `tests/test_*.c` that runs on the
  host with `make test`. No cross-compiler required.
- **Test the logic, not the hardware**: Re-implement data structures with host-compatible
  types if needed (see `test_mem.c` pattern). Test boundary conditions and error paths.
- **Emulator smoke test**: After host tests pass, `make run` and manually verify in the
  emulator shell. This catches ABI issues the host tests can't.
- **Never skip `make test`**: Run it before every commit.
- **Also run `make kernel`**: Host tests don't compile the full kernel, so they
  can't catch missing declarations, ABI mismatches, or cross-compilation errors.
  Always run `make kernel` (or `make run`) before considering a change done.
- **Also run `make megadrive`**: The Mega Drive build links different files and
  has a different memory layout. A change can build fine for workbench but fail
  for megadrive (or vice versa). Always verify both.

### 2. Keep the kernel small and flat

- Target: entire kernel readable in one sitting (~3000-5000 lines total).
- One `.c` file per subsystem. No deep call hierarchies. Prefer linear code.
- If a function is over 80 lines, it's probably doing too much. Split it.
- Global state is OK for a single-user kernel. Don't over-abstract.
- No dynamic dispatch or vtables unless absolutely needed (device table is fine).
- **Single source of truth for declarations**: Never duplicate function
  declarations across headers. Use `#include` instead. Duplicated declarations
  drift apart silently (e.g., `kernel.h` vs `pal.h`).

### 3. 68000 ABI and assembly rules

- **Calling convention**: d0-d1/a0-a1 are caller-saved. d2-d7/a2-a6 are callee-saved.
  Return value in d0.
- **Syscall stubs** that use d2+ must save them with `movem.l` before loading args
  and restore after TRAP returns.
- **Stack must be word-aligned** (even address) at all times on 68000. Address errors
  are fatal — the CPU will bus-fault.
- **No unaligned access**: 68000 faults on word/long reads at odd addresses. All
  structures must have even-aligned fields. Use `uint8_t` only for truly byte-sized data.
- **MOVEM is your friend**: Save/restore multiple registers in one instruction.
  Use it for context switch, TRAP handler, interrupt handler.

### 4. Error handling philosophy

- Kernel code returns negative errno on failure (e.g., `-ENOENT`).
- Never silently swallow errors. If a function can fail, the caller must check.
- Use `kprintf` for warnings, `panic` only for truly unrecoverable state.
- In user-visible syscalls, always return a meaningful errno.
- Validate all user-supplied pointers/sizes at the syscall boundary (not deeper).

### 5. Build system rules

- `make test` must always work with just the host `gcc` (no cross-compiler).
- `make kernel` requires `m68k-linux-gnu-gcc`.
- `make apps` requires the kernel to be built first (for shared headers).
- `make run` builds everything and launches the emulator.
- Keep Makefiles simple. No recursive make nightmares. One level of `$(MAKE) -C`.

### 6. Adding new syscalls

1. Add `SYS_FOO` number to `kernel/kernel.h`
2. Implement `sys_foo()` in the appropriate subsystem file
3. Add case to `syscall_dispatch()` in `kernel/proc.c`
4. Add libc stub in `libc/syscalls.S`
5. Add host test if the logic is testable
6. Update this file's syscall list if it's user-visible

### 7. Adding new user programs

1. Create `apps/foo.c` with a standard `int main(int argc, char **argv)`
2. Add `foo` to the `PROGRAMS` list in `apps/Makefile`
3. The build system links with crt0 + libc and produces a Genix binary
4. `make run` automatically includes it in the disk image at `/bin/foo`

### 8. Design flexibility

- **Binary format**: Currently flat binary at fixed load address. Will add relocation
  for multitasking. The format is intentionally simple — changing it later is cheap.
- **Load address**: USER_BASE is a compile-time constant for now. When we add
  relocation/multitasking, programs will load at dynamic addresses.
- **Memory layout must be modular**: USER_BASE, USER_TOP, and SRAM configuration
  must be platform-provided (via PAL), not hardcoded in the kernel. Different
  cartridge configurations (no SRAM, small SRAM, large SRAM) require different
  layouts. See `docs/megadrive.md` for cartridge configurations.
- **SRAM is optional**: The system must boot and run user programs on 64 KB main
  RAM alone. SRAM provides persistent writable storage and extended RAM for larger
  programs, but is not required for basic operation.
- **Libc**: Currently minimal stubs. Will grow as we port more programs. If we need
  stdio/printf in userspace, add it to libc, don't duplicate.
- If a design decision is causing pain, change it. Document why in the commit message.

### 9. Division and arithmetic on the 68000

The 68000 has NO 32÷32 hardware divide. Only DIVU.W (32÷16 → 16-bit
quotient, 76-136 cycles) and DIVS.W (signed, 122-156 cycles). Full
32-bit division goes through software in `kernel/divmod.S` (~300-600
cycles). This matters in hot paths.

**Rules for every `/` and `%` in kernel code:**

- **Powers of 2**: Use `>> n` and `& (n-1)` explicitly. GCC does this
  at `-O2` for constants, but be explicit for clarity.
  ```c
  blk = offset >> 10;       /* not offset / 1024 */
  off = offset & 0x3FF;     /* not offset % 1024 */
  ```
- **16-bit constant divisors**: OK to use `/` and `%` — GCC or the
  DIVU.W fast path handles these efficiently. Annotate with a comment:
  ```c
  /* DIVU.W safe: divisor=21 fits in 16 bits */
  blk = 1 + (inum - 1) / INODES_PER_BLK;
  ```
- **Avoid division in hot paths**: Replace `cursor / COLS` with
  separate row/col tracking (addition + comparison, not division).
- **Circular buffers**: Use 256-byte buffers with `uint8_t` indices —
  wraps naturally at 256 with no division at all.
- **Never divide by a 32-bit runtime variable** in a hot path. If
  unavoidable, document why and measure the cost.
- **Annotate every `/` and `%`** with the divisor's properties so
  future developers know the performance cost.

### 10. Constrained system programming

The Mega Drive has 64 KB main RAM, a 7.67 MHz CPU, and no MMU. Every
byte and cycle matters. Follow these rules:

- **Measure before optimizing**: Profile in the emulator first. Most
  kernel code is not hot-path.
- **Prefer computation over memory**: RAM is scarcer than cycles. A
  few extra instructions to avoid a lookup table is usually better.
- **Use fixed-size arrays**: Dynamic allocation should be rare. Process
  table, buffer cache, inode cache — all statically sized.
- **Avoid deep call stacks**: Each function call costs 18 cycles (JSR)
  + register saves. Keep the call depth shallow.
- **Inline tiny functions**: Small helpers (< 5 instructions) should
  be `static inline` or macros to avoid call overhead.
- **Keep structures aligned**: All fields at even offsets. The 68000
  faults on unaligned word/long access. Pad with `uint8_t` if needed.
- **Use `volatile` for hardware registers**: The compiler will
  reorder or eliminate accesses to memory-mapped I/O without it.
- **Test on host, verify on target**: Host tests catch logic bugs.
  The emulator catches ABI and alignment bugs. Real hardware catches
  timing bugs.

### 11. Toolchain warnings

- The distro `m68k-linux-gnu-gcc` defaults to 68020 and will silently
  emit illegal instructions (BSR.L, EXTB.L, MULS.L). Prefer a
  self-built `m68k-elf-gcc` with `--with-cpu=68000`.
- Always pass `-m68000` to the compiler as a safety net.
- Genix provides its own `divmod.S` (`__udivsi3`, `__umodsi3`,
  `__divsi3`, `__modsi3`) to avoid depending on libgcc versions that
  may contain 68020 instructions.
- See `docs/toolchain.md` for the full toolchain build instructions.

## Documentation

Detailed technical documentation is in `docs/`:

| Document | Description |
|----------|-------------|
| `docs/emulator.md` | Workbench emulator: architecture, usage, keyboard |
| `docs/toolchain.md` | Cross-compiler setup (apt quick-start + build from source) |
| `docs/megadrive.md` | Mega Drive target: memory, cartridges, SRAM, testing |
| `docs/decisions.md` | Design decisions, reversals, pain points, project history |
| `docs/architecture.md` | System architecture, memory maps, layers |
| `docs/kernel.md` | Kernel subsystems |
| `docs/syscalls.md` | Syscall interface and convention |
| `docs/filesystem.md` | minifs on-disk layout |
| `docs/binary-format.md` | Genix binary format and loader |
| `docs/tty.md` | VDP console and planned TTY subsystem |
| `docs/multitasking.md` | Process model, vfork/exec, scheduling |
| `docs/68000-programming.md` | ISA constraints, division, ABI |
| `docs/fuzix-heritage.md` | What we took from Fuzix, what's different |
