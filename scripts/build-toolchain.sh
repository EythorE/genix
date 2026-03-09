#!/bin/bash
# Build m68k-elf toolchain (GCC 14.2.0 + binutils 2.43)
# Produces ~/buildtools-m68k-elf/ with bin/, lib/, etc.
#
# Requirements: build-essential (gcc, make, etc.)
# No other apt packages needed — GMP/MPFR/MPC are built in-tree.
set -euo pipefail

PREFIX="$HOME/buildtools-m68k-elf"
SRCDIR="$PREFIX/src"
BUILDDIR="$PREFIX/build"
JOBS="$(nproc)"

BINUTILS_VER="2.43"
GCC_VER="14.2.0"
GMP_VER="6.2.1"
MPFR_VER="4.2.1"
MPC_VER="1.3.1"

mkdir -p "$SRCDIR" "$BUILDDIR"
cd "$SRCDIR"

echo "=== Downloading sources ==="
wget -q https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.gz
wget -q https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.gz
wget -q https://ftp.gnu.org/gnu/gmp/gmp-${GMP_VER}.tar.xz
wget -q https://ftp.gnu.org/gnu/mpfr/mpfr-${MPFR_VER}.tar.gz
wget -q https://ftp.gnu.org/gnu/mpc/mpc-${MPC_VER}.tar.gz

echo "=== Extracting ==="
tar -xzf binutils-${BINUTILS_VER}.tar.gz
tar -xzf gcc-${GCC_VER}.tar.gz
tar -xf  gmp-${GMP_VER}.tar.xz
tar -xzf mpfr-${MPFR_VER}.tar.gz
tar -xzf mpc-${MPC_VER}.tar.gz

# Symlink GMP/MPFR/MPC into GCC source tree (built in-tree, no -dev packages)
ln -sf "$SRCDIR/gmp-${GMP_VER}"  "gcc-${GCC_VER}/gmp"
ln -sf "$SRCDIR/mpfr-${MPFR_VER}" "gcc-${GCC_VER}/mpfr"
ln -sf "$SRCDIR/mpc-${MPC_VER}"  "gcc-${GCC_VER}/mpc"

export PATH="$PREFIX/bin:$PATH"

echo "=== Building binutils ==="
mkdir -p "$BUILDDIR/binutils" && cd "$BUILDDIR/binutils"
"$SRCDIR/binutils-${BINUTILS_VER}/configure" \
    --target=m68k-elf \
    --prefix="$PREFIX" \
    --disable-nls --disable-werror MAKEINFO=true
make -j"$JOBS" MAKEINFO=true
make install MAKEINFO=true

echo "=== Building GCC + libgcc ==="
mkdir -p "$BUILDDIR/gcc" && cd "$BUILDDIR/gcc"
"$SRCDIR/gcc-${GCC_VER}/configure" \
    --target=m68k-elf \
    --prefix="$PREFIX" \
    --disable-threads \
    --enable-languages=c \
    --disable-shared \
    --disable-libquadmath --disable-libssp --disable-libgcj \
    --disable-gold --disable-libmpx --disable-libgomp --disable-libatomic \
    --with-cpu=68000 MAKEINFO=true
make -j"$JOBS" all-gcc MAKEINFO=true
make -j"$JOBS" all-target-libgcc MAKEINFO=true
make install-gcc MAKEINFO=true
make install-target-libgcc MAKEINFO=true

echo "=== Cleaning up source and build dirs to reduce archive size ==="
rm -rf "$SRCDIR" "$BUILDDIR"

echo "=== Done ==="
echo "Toolchain installed to $PREFIX"
echo "Add to PATH:  export PATH=$PREFIX/bin:\$PATH"
"$PREFIX/bin/m68k-elf-gcc" --version | head -1
