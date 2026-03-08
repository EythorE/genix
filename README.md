# Genix — A Small 68000 OS for Mega Drive

Genix is a minimal, single-user, POSIX-enough operating system for the Motorola 68000,
targeting the Sega Mega Drive. It replaces the FUZIX kernel with ~3000 lines of new code
while reusing proven Mega Drive drivers.

## Prerequisites

- `gcc` (host, for emulator and tests)
- `m68k-elf-gcc` built with `--with-cpu=68000` (cross compiler for kernel/apps)
- `make`
- `blastem` (optional, for `make test-md` and Mega Drive ROM testing)

### Recommended: Build `m68k-elf-gcc` from source

The distro package (`gcc-m68k-linux-gnu` from apt) **defaults to the 68020** and
will silently emit illegal instructions (`BSR.L`, `EXTB.L`, `MULS.L`). Build a
bare-metal cross-compiler instead:

```bash
# binutils
cd ~/buildtools-m68k-elf/src/binutils-2.43
./configure --target=m68k-elf --prefix=~/buildtools-m68k-elf/ \
    --disable-nls --disable-werror
make -j4 && make install

# GCC + libgcc (--with-cpu=68000 is the critical flag)
cd ~/buildtools-m68k-elf/src/gcc-14.2.0
./configure --target=m68k-elf --prefix=~/buildtools-m68k-elf/ \
    --disable-threads --enable-languages=c --disable-shared \
    --disable-libquadmath --disable-libssp --disable-libgcj \
    --disable-gold --disable-libmpx --disable-libgomp --disable-libatomic \
    --with-cpu=68000
make -j4 all-gcc && make -j4 all-target-libgcc
make install-gcc && make install-target-libgcc
```

Then add `~/buildtools-m68k-elf/bin` to your `PATH` and set the cross-prefix:

```bash
export PATH=~/buildtools-m68k-elf/bin:$PATH
export CROSS=m68k-elf-
```

See [docs/toolchain.md](docs/toolchain.md) for full details on why the distro
compiler breaks and what `--with-cpu=68000` enforces.

### Distro compiler workaround

If you can't build the toolchain, the distro package works **only if** you never
link against its `libgcc.a` (Genix provides its own `divmod.S`):

```bash
sudo apt-get install gcc-m68k-linux-gnu binutils-m68k-linux-gnu
```

The kernel Makefile defaults to `CROSS=m68k-linux-gnu-` and passes `-m68000`
as a safety net, but the distro's libgcc may still contain 68020 instructions.

## Build

```bash
# Everything (emulator + kernel + tools + disk image)
make

# Run in the workbench emulator
make run

# Build a Mega Drive ROM
make megadrive
```

Individual targets:
```bash
make emu       # Host emulator only (Musashi-based 68000 SBC)
make kernel    # Cross-compile the kernel
make tools     # Host tools (mkfs.minifs)
make disk      # Create a minifs disk image
```

To exit the emulator, type `halt` at the shell prompt or press **Ctrl+]**.

## Running on Mega Drive (BlastEm)

[BlastEm](https://www.retrodev.com/blastem/) is the most accurate open-source
Mega Drive emulator. Genix requires a Saturn keyboard, which BlastEm emulates.

```bash
make megadrive
blastem pal/megadrive/genix-md.bin
```

### Keyboard setup (one-time)

Genix reads keyboard input from controller port 2 via the Saturn keyboard protocol.

1. Launch BlastEm with any ROM
2. Press **Escape** → **Settings** → **System**
3. Set **IO Port 2** to **Saturn Keyboard**
4. Exit settings (saved automatically to `~/.config/blastem/blastem.cfg`)

Or edit the config file directly:
```
io {
    devices {
        1 gamepad6.1
        2 saturn keyboard
    }
}
```

**Important:** Press **Right Ctrl** to toggle keyboard capture. When captured,
your keypresses go to the emulated Saturn keyboard. Press Right Ctrl again to
release back to BlastEm hotkeys.

### Automated smoke test

```bash
make test-md    # Boots ROM for ~5-10s, exits — catches crashes
```

### GDB debugging

```bash
m68k-linux-gnu-gdb -q --tui \
    -ex "target remote | blastem -D pal/megadrive/genix-md.bin" \
    pal/megadrive/genix-md.elf
```

## Running on Real Hardware

Copy `pal/megadrive/genix-md.bin` to a flash cartridge SD card and boot.
Connect a Saturn keyboard to controller port 2.

Tested flash carts: **Open EverDrive**, **Mega EverDrive Pro**. See
[docs/megadrive.md](docs/megadrive.md) for cartridge-specific SRAM
configuration and mapper differences.

## Testing

```bash
# Run host unit tests (no cross-compiler needed)
make test

# Mega Drive smoke test (BlastEm required)
make test-md
```

Tests cover `kernel/string.c`, `kernel/mem.c`, and `kernel/exec.c` — pure logic that runs natively on the host.

## Architecture

```
User Programs (sh, apps)  — linked with libc syscall stubs
        | TRAP #0
Kernel (proc, fs, dev, mem, syscall)
        |
Platform Abstraction Layer (PAL)
        |
Hardware / Emulator
```

Two platform targets:
- **Workbench** (`pal/workbench/`) — emulated SBC via Musashi, for development
- **Mega Drive** (`pal/megadrive/`) — real hardware with VDP, Saturn keyboard, SRAM

## Directory Structure

```
genix/
├── emu/          # Workbench 68000 emulator (Musashi-based)
├── kernel/       # OS kernel
├── pal/          # Platform Abstraction Layer
│   ├── workbench/  # Emulated SBC
│   └── megadrive/  # Sega Mega Drive
├── libc/         # Minimal C library + syscall stubs
├── apps/         # Userspace programs (hello, echo, cat)
├── tools/        # Host tools (mkfs, etc.)
└── tests/        # Host unit tests
```

## Design

- **No fork()** — vfork()+exec() only (no MMU)
- **No multi-user** — no UIDs, permissions, login
- **Custom filesystem (minifs)** — classic Unix inode layout
- **Syscalls via TRAP #0** — number in `d0`, args in `d1-d4`, return in `d0` (negative = -errno)

See [PLAN.md](PLAN.md) for the full design document and [docs/](docs/) for
detailed technical documentation (architecture, syscalls, filesystem, binary
format, TTY, multitasking, toolchain, 68000 programming).
