#!/bin/bash
#
# check-opcodes.sh — Scan compiled binaries for 68020-only instructions
#
# The distro m68k-linux-gnu-gcc libgcc contains 68020 opcodes that
# silently hang on the 68000. This script catches them before they
# reach hardware.
#
# Checked opcodes:
#   MULU.L  (0x4C00 xx00-xx07)  — 32x32 multiply (68020+)
#   MULS.L  (0x4C00 xx08-xx0F)  — 32x32 signed multiply (68020+)
#   DIVU.L  (0x4C40 xx00-xx07)  — 32/32 divide (68020+)
#   DIVS.L  (0x4C40 xx08-xx0F)  — 32/32 signed divide (68020+)
#   EXTB.L  (0x49C0-0x49C7)     — sign-extend byte to long (68020+)
#   RTD     (0x4E74)             — return and deallocate (68010+)
#
# Usage: ./scripts/check-opcodes.sh [CROSS=m68k-elf-]
#

set -e

CROSS="${CROSS:-m68k-elf-}"
OBJDUMP="${CROSS}objdump"

if ! command -v "$OBJDUMP" >/dev/null 2>&1; then
    echo "ERROR: $OBJDUMP not found. Set CROSS= or add toolchain to PATH."
    exit 1
fi

fail=0
checked=0

check_elf() {
    local elf="$1"
    local name
    name=$(basename "$elf")
    checked=$((checked + 1))

    # Disassemble and look for 68020-only instructions.
    # objdump marks unknown opcodes as ".short 0xNNNN" on 68000.
    # We also scan for known 68020 mnemonics in case the toolchain
    # disassembles them by name.
    #
    # IMPORTANT: String constants (.LC* labels, .rodata) are embedded
    # in the text section and may contain byte patterns that match
    # 68020 opcodes. We filter these out by tracking the current
    # function — only flag opcodes inside real functions, not in
    # literal data regions (.LC*, .L*).
    local issues
    issues=$("$OBJDUMP" -d "$elf" 2>/dev/null | awk '
        /^[0-9a-f]+ <[^.][^>]*>:/ { in_func = 1; next }
        /^[0-9a-f]+ <\./ { in_func = 0; next }
        in_func && /mulu\.l|muls\.l|divu\.l|divs\.l|extb\.l|\.short 0x4c0[0-9a-f]|\.short 0x4c4[0-9a-f]|\.short 0x49c[0-7]|\.short 0x4e74/ { print NR": "$0 }
    ' || true)

    if [ -n "$issues" ]; then
        echo "FAIL: $name contains 68020 instructions:"
        echo "$issues" | head -10
        if [ "$(echo "$issues" | wc -l)" -gt 10 ]; then
            echo "  ... ($(echo "$issues" | wc -l) total)"
        fi
        fail=$((fail + 1))
    fi
}

echo "=== Checking for 68020 opcodes (CROSS=${CROSS}) ==="

# Check kernel
for elf in kernel/kernel.elf; do
    [ -f "$elf" ] && check_elf "$elf"
done

# Check user programs
for elf in apps/*.elf apps/dash/dash.elf apps/levee/levee.elf; do
    [ -f "$elf" ] && check_elf "$elf"
done

echo "=== Checked $checked binaries, $fail failures ==="

if [ $fail -gt 0 ]; then
    echo ""
    echo "68020 instructions detected! You are likely using the distro"
    echo "compiler (m68k-linux-gnu-gcc). Use m68k-elf-gcc instead:"
    echo ""
    echo "  ./scripts/fetch-toolchain.sh"
    echo "  export PATH=~/buildtools-m68k-elf/bin:\$PATH"
    echo "  export CROSS=m68k-elf-"
    echo "  make clean && make"
    exit 1
fi

exit 0
