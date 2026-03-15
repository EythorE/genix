# Toolchain

> **You MUST use `m68k-elf-gcc`.** The distro package
> (`gcc-m68k-linux-gnu`) ships a `libgcc.a` with 68020 instructions
> that cause silent hangs on the 68000. This is not theoretical —
> dash shell will not boot with the distro compiler.

## Quick Start

```bash
./scripts/fetch-toolchain.sh
export PATH=~/buildtools-m68k-elf/bin:~/blastem:$PATH
```

That's it. All Makefiles default to `CROSS=m68k-elf-`. If
`fetch-toolchain.sh` fails, build from source — see
[Building the Correct Toolchain](#building-the-correct-toolchain) below.

**Do NOT use `apt install gcc-m68k-linux-gnu`** — see
[Why the Distro Compiler Is Broken](#why-the-distro-compiler-is-broken)
for the full explanation.

---

## Why the Distro Compiler Is Broken

The distro `m68k-linux-gnu-gcc` (from `apt`) **will silently break
dash and any program using 64-bit arithmetic**. While `-m68000`
controls code generation for YOUR code, `libgcc.a` is pre-compiled
for 68020 and contains `MULU.L`, `DIVU.L`, and `BSR.L` instructions.

`divmod.S` only replaces 32-bit division (`__divsi3`, `__udivsi3`).
It does NOT replace 64-bit operations (`__muldi3`, `__divdi3`,
`__moddi3`). Any `long long` multiply, divide, or modulus — including
inside `strtoull`, `vsnprintf`, and `strtoimax` — pulls in the 68020
libgcc functions and causes the 68000 to silently hang (illegal
instruction → infinite exception loop with no output).

**This cost multiple days of debugging.** The symptom is a completely
silent hang with no crash message, no output, no diagnostic
information. Do not use the distro compiler.

---

## Building the Correct Toolchain

Build `m68k-elf-gcc` from source with `--with-cpu=68000`. This produces
a compiler and libgcc that only emit base 68000 instructions.

### Prerequisites

```bash
# Build dependencies (Ubuntu/Debian)
sudo apt-get install build-essential
```

Only `build-essential` is required. GMP, MPFR, and MPC are built
in-tree (see [Building Without apt](#building-without-apt) below), and
`MAKEINFO=true` is passed to skip texinfo generation.

### Download Sources

```bash
mkdir -p ~/buildtools-m68k-elf/{src,build}
cd ~/buildtools-m68k-elf/src

# binutils 2.43
wget https://ftp.gnu.org/gnu/binutils/binutils-2.43.tar.gz
tar -xzf binutils-2.43.tar.gz

# GCC 14.2.0
wget https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.gz
tar -xzf gcc-14.2.0.tar.gz

# GDB 15.2 (optional but recommended for BlastEm debugging)
wget https://ftp.gnu.org/gnu/gdb/gdb-15.2.tar.gz
tar -xzf gdb-15.2.tar.gz
```

### Build

```bash
export PATH=~/buildtools-m68k-elf/bin:$PATH

# 1. binutils
mkdir -p ~/buildtools-m68k-elf/build/binutils && cd $_
~/buildtools-m68k-elf/src/binutils-2.43/configure \
    --target=m68k-elf \
    --prefix=$HOME/buildtools-m68k-elf \
    --disable-nls --disable-werror
make -j$(nproc) && make install

# 2. GCC + libgcc (THE CRITICAL PART)
mkdir -p ~/buildtools-m68k-elf/build/gcc && cd $_
~/buildtools-m68k-elf/src/gcc-14.2.0/configure \
    --target=m68k-elf \
    --prefix=$HOME/buildtools-m68k-elf \
    --disable-threads \
    --enable-languages=c \
    --disable-shared \
    --disable-libquadmath --disable-libssp --disable-libgcj \
    --disable-gold --disable-libmpx --disable-libgomp --disable-libatomic \
    --with-cpu=68000          # <-- THIS IS THE CRITICAL FLAG
make -j$(nproc) all-gcc
make -j$(nproc) all-target-libgcc   # builds libgcc.a for 68000 specifically
make install-gcc
make install-target-libgcc

# 3. GDB (optional)
mkdir -p ~/buildtools-m68k-elf/build/gdb && cd $_
~/buildtools-m68k-elf/src/gdb-15.2/configure \
    --target=m68k-elf \
    --prefix=$HOME/buildtools-m68k-elf
make -j$(nproc) && make install
```

### Add to PATH

```bash
# Add to ~/.bashrc or ~/.profile
export PATH=~/buildtools-m68k-elf/bin:$PATH
```

### Use with Genix

All Makefiles default to `CROSS=m68k-elf-`, so just ensure the
toolchain is on your PATH:

```bash
export PATH=~/buildtools-m68k-elf/bin:$PATH
make clean && make run
```

---

## Distro Compiler Technical Details

Debian/Ubuntu's `gcc-m68k-linux-gnu` targets Linux on 68k, which
requires an MMU (68010+ minimum). The compiler and its `libgcc.a` are
built for the 68020 or later.

The failure mode is insidious: code compiles and links without errors,
and mostly runs — until it hits a 68020-only instruction in libgcc.
The 68000 treats these as illegal instructions, and without a proper
handler the machine hangs silently.

### Instructions the distro compiler may silently emit

| Instruction | What it does | Why it fails on 68000 |
|-------------|-------------|----------------------|
| `BSR.L` | 32-bit PC-relative call | 68000 only has 8/16-bit offsets |
| `EXTB.L` | Sign-extend byte to long | 68020+ only |
| `MULS.L` / `MULU.L` | 32x32 multiply | 68000 only has 16x16 |
| `DIVS.L` / `DIVU.L` | 32/32 hardware divide | 68000 only has 32/16 |
| `RTD` | Return and deallocate | 68010+ |

### What `--with-cpu=68000` enforces

- Only emits instructions valid on the base 68000
- Restricts branches to 8/16-bit offsets (no BSR.L)
- Uses software `__divsi3`/`__modsi3` loop instead of DIVS.L/DIVU.L
- Does not emit EXTB.L (uses EXT.W + EXT.L pair instead)
- Uses MULS.W/MULU.W (16x16->32) for multiplication
- libgcc.a itself is compiled with these restrictions

### Distro package vs self-built

| Factor | Distro `m68k-linux-gnu-gcc` | Self-built `m68k-elf-gcc` |
|--------|---------------------------|--------------------------|
| Default CPU | 68020+ | 68000 |
| ELF target | m68k-linux-gnu (Linux ABI) | m68k-elf (bare metal) |
| libgcc | Contains 68020 instructions | 68000 only |
| BSR.L emitted? | Yes (causes hang) | No |
| 32/32 divide | DIVS.L (68020 hardware) | Software loop |

---

## Building Without apt

If `apt` is unavailable or broken (common in CI containers, restricted
networks, or when mirrors are down), you can build the toolchain with
**no apt dependencies beyond `build-essential`**. The trick is two-fold:

1. **GMP/MPFR/MPC** — download from GNU mirrors and place them inside
   the GCC source tree. GCC's build system detects them and builds them
   in-tree, so system `-dev` packages are not needed.
2. **texinfo** (`makeinfo`) — only needed for documentation. Pass
   `MAKEINFO=true` to skip info page generation.

```bash
mkdir -p ~/buildtools-m68k-elf/{src,build}
cd ~/buildtools-m68k-elf/src

# Download all sources from ftp.gnu.org
wget https://ftp.gnu.org/gnu/binutils/binutils-2.43.tar.gz
wget https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.gz
wget https://ftp.gnu.org/gnu/gmp/gmp-6.2.1.tar.xz
wget https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.1.tar.gz
wget https://ftp.gnu.org/gnu/mpc/mpc-1.3.1.tar.gz

# Extract everything
tar -xzf binutils-2.43.tar.gz
tar -xzf gcc-14.2.0.tar.gz
tar -xf  gmp-6.2.1.tar.xz
tar -xzf mpfr-4.2.1.tar.gz
tar -xzf mpc-1.3.1.tar.gz

# Symlink GMP/MPFR/MPC into the GCC source tree
# (GCC's configure detects these and builds them in-tree)
ln -sf ../gmp-6.2.1  gcc-14.2.0/gmp
ln -sf ../mpfr-4.2.1 gcc-14.2.0/mpfr
ln -sf ../mpc-1.3.1  gcc-14.2.0/mpc
```

Then follow the normal [Build](#build) steps above, but append
`MAKEINFO=true` to every `configure` and `make` command:

```bash
export PATH=~/buildtools-m68k-elf/bin:$PATH

# 1. binutils
mkdir -p ~/buildtools-m68k-elf/build/binutils && cd $_
~/buildtools-m68k-elf/src/binutils-2.43/configure \
    --target=m68k-elf \
    --prefix=$HOME/buildtools-m68k-elf \
    --disable-nls --disable-werror MAKEINFO=true
make -j$(nproc) MAKEINFO=true && make install MAKEINFO=true

# 2. GCC + libgcc
mkdir -p ~/buildtools-m68k-elf/build/gcc && cd $_
~/buildtools-m68k-elf/src/gcc-14.2.0/configure \
    --target=m68k-elf \
    --prefix=$HOME/buildtools-m68k-elf \
    --disable-threads \
    --enable-languages=c \
    --disable-shared \
    --disable-libquadmath --disable-libssp --disable-libgcj \
    --disable-gold --disable-libmpx --disable-libgomp --disable-libatomic \
    --with-cpu=68000 MAKEINFO=true
make -j$(nproc) all-gcc MAKEINFO=true
make -j$(nproc) all-target-libgcc MAKEINFO=true
make install-gcc MAKEINFO=true
make install-target-libgcc MAKEINFO=true
```

This produces the exact same toolchain as the normal build.

---

## Compiler Flags

### Kernel (`kernel/Makefile`)

```makefile
CFLAGS = -m68000 -nostdlib -nostartfiles -ffreestanding \
         -Wall -Wextra -Wno-unused-function -O2 -I. -I../pal
```

| Flag | Effect |
|------|--------|
| `-m68000` | Generate only base 68000 instructions |
| `-nostdlib` | Don't link standard C library |
| `-nostartfiles` | Don't link default crt0 |
| `-ffreestanding` | No hosted environment assumptions |
| `-O2` | Optimization (strength-reduces constant divisions) |

### Apps (`apps/Makefile`)

Same flags, plus linking with `crt0.o`, `libc.a`, and `libgcc`.

### Fuzix libc flags (for reference)

When porting the Fuzix libc, use these flags (from Fuzix's
`Library/libs/Makefile.68000`):

```makefile
CC_OPT = -DUSE_SYSMALLOC -fno-strict-aliasing -fomit-frame-pointer \
         -fno-builtin -msoft-float -Wall -m68000 -Os -c
```

| Flag | Why |
|------|-----|
| `-msoft-float` | No FPU on the 68000 |
| `-fomit-frame-pointer` | Saves LINK/UNLK per function, frees A6 |
| `-fno-builtin` | Prevents GCC replacing memcpy etc. with inlined versions |
| `-fno-strict-aliasing` | Kernel code often type-puns pointers |
| `-Os` | Optimize for size (cartridge ROM is limited) |

---

## libgcc

GCC emits calls to these runtime functions for 32-bit divide/modulo:

| Symbol | Operation |
|--------|-----------|
| `__udivsi3(a, b)` | unsigned 32/32 -> quotient |
| `__umodsi3(a, b)` | unsigned 32/32 -> remainder |
| `__divsi3(a, b)` | signed 32/32 -> quotient |
| `__modsi3(a, b)` | signed 32/32 -> remainder |

Genix provides its own implementations in `kernel/divmod.S` using a
shift-and-subtract algorithm. This avoids depending on libgcc's versions
which may contain 68020 instructions.

See [68000-programming.md](68000-programming.md) for details on the
division strategy.

---

## Fuzix Compiler Context

Fuzix uses different compilers for different CPU families:

| CPU | Compiler | Notes |
|-----|----------|-------|
| Z80, 8080, 6800 | Fuzix Compiler Kit (FCC) | Custom compiler for 8-bit |
| 6809 | GCC 4.6.4 + lwtools | Patched fork |
| 68HC11 | GCC 3.4.6 | Last supporting version |
| 65C816, 6502 | CC6303 / CC65 | Niche compilers |
| **68000** | **Self-built m68k-elf GCC** | Same as Genix |

The 68000 is the only Fuzix target that requires GCC, and it requires
a specifically-built version. The Fuzix Compiler Kit has no 68000
backend because GCC handles the 68000 well — there's no need to
duplicate that effort.
