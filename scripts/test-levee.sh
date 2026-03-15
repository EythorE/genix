#!/bin/bash
#
# test-levee.sh — Verify levee (vi clone) starts without crashing
#
# Usage: ./scripts/test-levee.sh
#

set -e

EMU="emu/emu68k"
KERNEL="kernel/kernel.bin"
DISK="disk.img"

if [ ! -x "$EMU" ]; then
    echo "ERROR: $EMU not found. Run 'make emu' first."
    exit 1
fi

pass=0
fail=0

echo "=== test-levee: levee editor integration tests ==="

echo ""
echo "--- Boot test ---"

printf "  %-40s" "levee starts without panic..."
output=$(printf '/bin/levee /tmp/testfile\n:q!\n' | timeout 5 "$EMU" "$KERNEL" "$DISK" 2>&1) || true

if echo "$output" | grep -qF "KERNEL PANIC"; then
    echo "FAIL (kernel panic)"
    echo "    $(echo "$output" | grep 'PANIC\|PC=' | head -2 | sed 's/^/    /')"
    fail=$((fail + 1))
elif echo "$output" | grep -qF "levee"; then
    echo "PASS"
    pass=$((pass + 1))
else
    echo "FAIL (no output)"
    fail=$((fail + 1))
fi

echo ""
echo "=== test-levee: $pass passed, $fail failed ==="
echo ""

if [ $fail -gt 0 ]; then
    exit 1
fi
exit 0
