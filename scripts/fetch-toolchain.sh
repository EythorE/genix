#!/bin/bash
# Fetch pre-built m68k-elf toolchain and BlastEm from GitHub Releases.
# Idempotent: skips components that are already installed.
set -euo pipefail

TOOLCHAIN_DIR="$HOME/buildtools-m68k-elf"
REPO="EythorE/genix"
TAG="toolchain-latest"
ASSET="m68k-elf-toolchain-x86_64-linux.tar.gz"
URL="https://github.com/${REPO}/releases/download/${TAG}/${ASSET}"

BLASTEM_DIR="$HOME/blastem"
BLASTEM_URL="https://www.retrodev.com/blastem/blastem64-0.6.2.tar.gz"

# --- m68k-elf toolchain ---

if [ -x "$TOOLCHAIN_DIR/bin/m68k-elf-gcc" ]; then
    echo "Toolchain already installed at $TOOLCHAIN_DIR"
    "$TOOLCHAIN_DIR/bin/m68k-elf-gcc" --version | head -1
else
    echo "Fetching pre-built m68k-elf toolchain from ${REPO}..."
    echo "URL: $URL"
    curl -fSL "$URL" | tar xz -C "$HOME"

    echo "Installed to $TOOLCHAIN_DIR"
    "$TOOLCHAIN_DIR/bin/m68k-elf-gcc" --version | head -1
fi

# --- BlastEm ---

if [ -x "$BLASTEM_DIR/blastem" ]; then
    echo "BlastEm already installed at $BLASTEM_DIR"
else
    echo "Fetching BlastEm 0.6.2..."
    echo "URL: $BLASTEM_URL"
    mkdir -p "$BLASTEM_DIR"
    curl -fSL "$BLASTEM_URL" | tar xz --strip-components=1 -C "$BLASTEM_DIR"

    echo "Installed to $BLASTEM_DIR"
fi

echo ""
echo "Add to your environment:"
echo "  export PATH=$TOOLCHAIN_DIR/bin:$BLASTEM_DIR:\$PATH"
echo "  export CROSS=m68k-elf-"
