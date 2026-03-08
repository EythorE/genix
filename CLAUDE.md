# Genix — A Small 68000 OS for Mega Drive

## Project Overview

Genix is a minimal, single-user, POSIX-enough operating system for the Motorola 68000,
targeting the Sega Mega Drive. It replaces the FUZIX kernel with ~3000 lines of new code
while reusing proven Mega Drive drivers.

## Build

```bash
# Host toolchain (emulator)
make emu

# Kernel for workbench emulator
make kernel

# User programs (libc + apps)
make apps

# Full system (kernel + apps, run in emulator)
make run

# Mega Drive ROM
make megadrive

# Run host unit tests
make test
```

### Prerequisites

- `gcc` (host, for emulator)
- `m68k-linux-gnu-gcc` (cross compiler for kernel/apps)
- `make`

Install on Ubuntu/Debian:
```bash
sudo apt-get install gcc-m68k-linux-gnu binutils-m68k-linux-gnu
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

- **No fork()** — vfork()+exec() only (no MMU required)
- **No multi-user** — no UIDs, permissions, login
- **Custom filesystem (minifs)** — classic Unix inode layout
- **Single-tasking first** — then add multi-tasking
- **Musashi** 68000 emulator for development
- **Simple binary format** — flat binary with 32-byte header, relocatable later
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

## Memory Layout (Workbench, 1MB RAM)

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

```bash
# Run host unit tests (no cross-compiler needed)
make test

# Run in workbench emulator (interactive)
make run

# In the emulator shell:
#   exec /bin/hello
#   exec /bin/echo hello world
```

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

### 2. Keep the kernel small and flat

- Target: entire kernel readable in one sitting (~3000-5000 lines total).
- One `.c` file per subsystem. No deep call hierarchies. Prefer linear code.
- If a function is over 80 lines, it's probably doing too much. Split it.
- Global state is OK for a single-user kernel. Don't over-abstract.
- No dynamic dispatch or vtables unless absolutely needed (device table is fine).

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
- **Libc**: Currently minimal stubs. Will grow as we port more programs. If we need
  stdio/printf in userspace, add it to libc, don't duplicate.
- If a design decision is causing pain, change it. Document why in the commit message.
