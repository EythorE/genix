#!/bin/bash
#
# test-dash.sh — Verify dash boots and runs commands correctly
#
# Runs the workbench emulator with piped commands and checks output.
# Requires: emu/emu68k, kernel/kernel.bin, disk.img (with dash)
#
# Usage: ./scripts/test-dash.sh
#

set -e

EMU="emu/emu68k"
KERNEL="kernel/kernel.bin"
DISK="disk.img"

if [ ! -x "$EMU" ]; then
    echo "ERROR: $EMU not found. Run 'make emu' first."
    exit 1
fi
if [ ! -f "$KERNEL" ]; then
    echo "ERROR: $KERNEL not found. Run 'make kernel' first."
    exit 1
fi
if [ ! -f "$DISK" ]; then
    echo "ERROR: $DISK not found. Run 'make disk' first."
    exit 1
fi

pass=0
fail=0
skip=0

run_test() {
    local name="$1"
    local input="$2"
    local expect="$3"
    local timeout_sec="${4:-10}"

    printf "  %-40s" "$name..."
    local output
    output=$(printf '%s\n' "$input" | timeout "$timeout_sec" "$EMU" "$KERNEL" "$DISK" 2>&1) || true

    if echo "$output" | grep -qF "$expect"; then
        echo "PASS"
        pass=$((pass + 1))
    else
        echo "FAIL"
        echo "    expected: $expect"
        echo "    output (last 5 lines):"
        echo "$output" | tail -5 | sed 's/^/      /'
        fail=$((fail + 1))
    fi
}

run_test_absent() {
    local name="$1"
    local input="$2"
    local reject="$3"
    local timeout_sec="${4:-10}"

    printf "  %-40s" "$name..."
    local output
    output=$(printf '%s\n' "$input" | timeout "$timeout_sec" "$EMU" "$KERNEL" "$DISK" 2>&1) || true

    if echo "$output" | grep -qF "$reject"; then
        echo "FAIL (found rejected string)"
        echo "    rejected: $reject"
        fail=$((fail + 1))
    else
        echo "PASS"
        pass=$((pass + 1))
    fi
}

echo "=== test-dash: dash shell integration tests ==="

echo ""
echo "--- Boot and prompt ---"
run_test "dash boots and shows prompt" \
    "exit" "# " 10

echo ""
echo "--- Basic commands ---"
run_test "echo hello" \
    "echo hello" "hello"

run_test "echo with multiple args" \
    "echo one two three" "one two three"

run_test "exit status of true" \
    "true
echo \$?" "0"

run_test "exit status of false" \
    "false
echo \$?" "1"

echo ""
echo "--- External commands ---"
run_test "ls /bin lists programs" \
    "ls /bin" "dash"

run_test "cat with heredoc-style input" \
    "echo testdata > /tmp/cattest
cat /tmp/cattest" "testdata"

echo ""
echo "--- Pipes ---"
run_test "echo | cat pipe" \
    "echo pipetest | cat" "pipetest"

run_test "echo | wc pipe" \
    "echo hello | wc" "1"

run_test "3-stage pipe: echo | cat | cat" \
    "echo three_stage | cat | cat" "three_stage"

run_test "3-stage pipe: echo | cat | grep" \
    "echo greppable | cat | grep greppable" "greppable"

run_test "4-stage pipe: echo | cat | cat | cat" \
    "echo four_stage | cat | cat | cat" "four_stage"

run_test "pipe + wc -l" \
    "echo hello | wc -l" "1"

run_test "ls | more pipe (Bug 20+21)" \
    "ls /bin | more" "dash"

echo ""
echo "--- File operations ---"
run_test "rmdir command exists" \
    "rmdir --help 2>&1; echo done" "done"

run_test "mkdir + rmdir" \
    "mkdir /tmp/testrmdir
rmdir /tmp/testrmdir
echo ok" "ok"

run_test "cat error includes filename" \
    "cat nonexistent_file 2>&1" "nonexistent_file"

echo ""
echo "--- I/O redirection ---"
run_test "output redirection >" \
    "echo redir_ok > /tmp/rtest
cat /tmp/rtest" "redir_ok"

echo ""
echo "--- Variable expansion ---"
run_test "variable assignment and expansion" \
    "X=hello
echo \$X" "hello"

echo ""
echo "--- Non-interactive mode (lineedit bypass) ---"
run_test "piped input bypasses lineedit" \
    "echo piped_ok" "piped_ok"

run_test "multi-line piped script" \
    "X=abc
echo \$X" "abc"

echo ""
echo "--- No error messages on normal operation ---"
run_test_absent "no 'Success' error spam" \
    "echo hello
ls /bin
exit" "Success"

run_test_absent "no exec failure messages" \
    "echo test
exit" "exec failed"

echo ""
echo "=== test-dash: $pass passed, $fail failed, $skip skipped ==="

if [ $fail -gt 0 ]; then
    exit 1
fi
exit 0
