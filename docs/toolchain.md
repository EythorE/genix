# Toolchain

## The Short Version

You need a GCC cross-compiler built with `--with-cpu=68000`. The distro
package (`m68k-linux-gnu-gcc` from apt) **will silently produce broken
binaries** because it defaults to the 68020 instruction set.

## Why the Distro Compiler Doesn't Work

Debian/Ubuntu's `gcc-m68k-linux-gnu` targets Linux on 68k, which
requires an MMU (68010+ minimum). The compiler and its `libgcc.a` are
built for the 68020 or later.

The failure mode is insidious: code compiles and links without errors,
and mostly runs — until it hits a 68020-only instruction in libgcc.
The most common culprit is **`BSR.L`** (32-bit branch to subroutine),
which the 68000 does not have (only 8-bit and 16-bit branch offsets).

### Instructions the distro compiler may silently emit

| Instruction | What it does | Why it fails on 68000 |
|-------------|-------------|----------------------|
| `BSR.L` | 32-bit PC-relative call | 68000 only has 8/16-bit offsets |
| `EXTB.L` | Sign-extend byte to long | 68020+ only |
| `MULS.L` / `MULU.L` | 32×32 multiply | 68000 only has 16×16 |
| `DIVS.L` / `DIVU.L` | 32÷32 hardware divide | 68000 only has 32÷16 |
| `RTD` | Return and deallocate | 68010+ |

When `printf("%d")` triggers a divide-by-10 in libgcc's number
formatter, the BSR.L in libgcc fires an illegal instruction exception.
The unused interrupt vectors point to `stop #0x2700`, and the machine
hangs. Debugging this is painful because the code looks correct.

## Building the Correct Toolchain

Build `m68k-elf-gcc` from source with `--with-cpu=68000`:

```bash
# 1. binutils
cd ~/buildtools-m68k-elf/src/binutils-2.43
./configure \
    --target=m68k-elf \
    --prefix=~/buildtools-m68k-elf/ \
    --disable-nls --disable-werror
make -j4 && make install

# 2. GCC + libgcc (THE CRITICAL PART)
cd ~/buildtools-m68k-elf/src/gcc-14.2.0
./configure \
    --target=m68k-elf \
    --prefix=~/buildtools-m68k-elf/ \
    --disable-threads \
    --enable-languages=c \
    --disable-shared \
    --disable-libquadmath --disable-libssp --disable-libgcj \
    --disable-gold --disable-libmpx --disable-libgomp --disable-libatomic \
    --with-cpu=68000          # ← THIS IS THE CRITICAL FLAG
make -j4 all-gcc
make -j4 all-target-libgcc   # ← builds libgcc.a for 68000 specifically
make install-gcc
make install-target-libgcc

# 3. GDB (optional but recommended)
cd ~/buildtools-m68k-elf/src/gdb-15.2
./configure \
    --target=m68k-elf \
    --prefix=~/buildtools-m68k-elf/
make -j4 && make install
```

### What `--with-cpu=68000` enforces

- Only emits instructions valid on the base 68000
- Restricts branches to 8/16-bit offsets (no BSR.L)
- Uses software `__divsi3`/`__modsi3` loop instead of DIVS.L/DIVU.L
- Does not emit EXTB.L (uses EXT.W + EXT.L pair instead)
- Uses MULS.W/MULU.W (16×16→32) for multiplication
- libgcc.a itself is compiled with these restrictions

### Distro package vs self-built

| Factor | Distro `m68k-linux-gnu-gcc` | Self-built `m68k-elf-gcc` |
|--------|---------------------------|--------------------------|
| Default CPU | 68020+ | 68000 |
| ELF target | m68k-linux-gnu (Linux ABI) | m68k-elf (bare metal) |
| libgcc | Contains 68020 instructions | 68000 only |
| BSR.L emitted? | Yes (causes hang) | No |
| 32÷32 divide | DIVS.L (68020 hardware) | Software loop |

## Using the Distro Compiler (Workaround)

If you can't build the toolchain, the distro `m68k-linux-gnu-gcc` can
be used **if** you:

1. Always pass `-m68000` (code generation)
2. Provide your own `divmod.S` (Genix already does this in `kernel/divmod.S`)
3. Do NOT link against the distro's `libgcc.a`

Genix's kernel Makefile currently uses `CROSS ?= m68k-linux-gnu-` and
links with `$(LIBGCC)` from the cross compiler. If this libgcc contains
68020 instructions, replace it with Genix's `divmod.S` which provides
`__udivsi3`, `__umodsi3`, `__divsi3`, `__modsi3`.

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

## libgcc

GCC emits calls to these runtime functions for 32-bit divide/modulo:

| Symbol | Operation |
|--------|-----------|
| `__udivsi3(a, b)` | unsigned 32÷32 → quotient |
| `__umodsi3(a, b)` | unsigned 32÷32 → remainder |
| `__divsi3(a, b)` | signed 32÷32 → quotient |
| `__modsi3(a, b)` | signed 32÷32 → remainder |

Genix provides its own implementations in `kernel/divmod.S` using a
shift-and-subtract algorithm. This avoids depending on libgcc's versions
which may contain 68020 instructions.

See [68000-programming.md](68000-programming.md) for details on the
division strategy.

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
