# 68000 Programming Guide

Programming constraints, optimization strategies, and ABI rules for the
Motorola 68000 on the Sega Mega Drive (7.67 MHz, no MMU, no FPU).

## Calling Convention (C ABI)

| Registers | Convention | Notes |
|-----------|-----------|-------|
| d0-d1, a0-a1 | Caller-saved | Scratch registers, clobbered by calls |
| d2-d7, a2-a6 | Callee-saved | Must be preserved across function calls |
| d0 | Return value | 32-bit return in d0 |
| a7 (SP) | Stack pointer | Must stay even-aligned at all times |

### Syscall Convention (Genix-specific)

| Register | Purpose |
|----------|---------|
| d0 | Syscall number (in), return value (out) |
| d1-d4 | Arguments |

Entry via `TRAP #0`. Syscall stubs that use d2+ must save/restore
them with `MOVEM.L` before loading arguments.

## Critical Constraints

### 1. No Unaligned Access

The 68000 **faults on word/long reads at odd addresses**. This is a
hard bus error — the CPU will double-fault and hang.

Rules:
- All `uint16_t` and `uint32_t` fields must be at even offsets
- Stack pointer must always be even
- Use `uint8_t` only for truly byte-sized data
- When casting pointers, ensure alignment

### 2. Word-Aligned Stack

The stack pointer (A7) must always be even. An odd SP causes an
address error on any push/pop. This is enforced by the hardware —
there is no software workaround.

### 3. No BSR.L

The 68000 only has 8-bit and 16-bit branch offsets. The 68020 added
32-bit (`BSR.L`, opcode `61FF xxxx xxxx`). If you see illegal
instruction exceptions in libgcc calls, you're running 68020 code.
See [toolchain.md](toolchain.md).

## Division on the 68000

This is the most important optimization topic for constrained 68000
systems.

### Hardware Division Instructions

| Instruction | Operation | Divisor | Quotient | Remainder | Cycles |
|-------------|-----------|---------|----------|-----------|--------|
| `DIVU.W src, Dn` | Dn[31:0] ÷ src[15:0] | 16-bit unsigned | Dn[15:0] | Dn[31:16] | 76-136 |
| `DIVS.W src, Dn` | Dn[31:0] ÷ src[15:0] | 16-bit signed | Dn[15:0] | Dn[31:16] | 122-156 |

Key limitation: **the divisor is always 16-bit**. Both instructions
produce a 16-bit quotient. If the quotient overflows 16 bits, a divide
overflow exception fires.

For C's `int a / int b` where b could be a full 32-bit value, there is
no single hardware instruction. GCC must call `__divsi3` from libgcc
(or Genix's `divmod.S`).

### Division Cost Summary

| Divisor type | Mechanism | Cost |
|-------------|-----------|------|
| Power of 2 (constant) | Compiler uses `LSR.L` / `AND.L` | 6-22 cycles |
| Small constant (e.g. 10) | GCC may use multiply-by-reciprocal | ~50-100 cycles |
| 16-bit runtime value | DIVU.W hardware instruction | 76-136 cycles |
| 32-bit runtime value | Software shift-and-subtract loop | 300-600 cycles |

### Genix's divmod.S

`kernel/divmod.S` provides `__udivsi3`, `__umodsi3`, `__divsi3`,
`__modsi3` using a shift-and-subtract algorithm:

1. Shift divisor left until it exceeds dividend
2. Repeatedly: shift quotient left, compare, subtract if possible,
   shift divisor right
3. Signed versions negate operands, call unsigned, fix sign

This is ~170 lines of assembly, correct for all inputs.

### Division Strategy: Best Practices

#### Use shifts for powers of 2

```c
// GOOD: compiler generates LSR.L #10
blk = offset >> 10;           // was: offset / 1024
off = offset & 0x3FF;         // was: offset % 1024

// GOOD: byte-level operations
col = offset & 0xFF;          // was: offset % 256
```

#### Keep divisors 16-bit where possible

If the divisor fits in 16 bits, libgcc/divmod.S can use `DIVU.W`
(the fast hardware path). Most kernel divisors are small:

```c
// FAST (divisor = 40, fits in 16 bits → DIVU.W):
col = pos % 40;

// FAST (divisor = 10, constant → GCC may use reciprocal):
digit = n % 10;

// SLOW (if b is a 32-bit runtime value → software loop):
result = a / b;
```

#### Avoid division in hot paths

Replace division with tracking:

```c
// BAD: divides on every character output
void putc(int c) {
    row = cursor / 40;   // DIVU.W, ~100 cycles
    col = cursor % 40;   // another DIVU.W
    plot_char(row, col, c);
    cursor++;
}

// GOOD: no division at all
void putc(int c) {
    plot_char(cur_row, cur_col, c);
    if (++cur_col >= 40) {
        cur_col = 0;
        if (++cur_row >= ROWS) {
            cur_row = ROWS - 1;
            scroll_up();
        }
    }
}
```

This is how the Mega Drive PAL implements `pal_console_putc()`.

#### Circular buffers: use power-of-2 sizes with uint8_t indices

```c
struct ttyinq {
    uint8_t head, tail;    // wraps at 256 naturally
    uint8_t buf[256];
};
buf[head++] = c;           // no modulo needed!
c = buf[tail++];           // wraps at 256 via uint8_t overflow
```

This is the Fuzix tty input buffer design. Zero division cost.

### Annotating Division Sites

When writing kernel code, annotate every `/` and `%` with the divisor's
properties so future developers know the performance cost:

```c
/* Shift-optimized: BLOCK_SIZE=1024=2^10, GCC uses LSR */
uint32_t bn = offset / BLOCK_SIZE;

/* DIVU.W safe: INODES_PER_BLK=21 fits in 16 bits */
uint16_t blk = 1 + (inum - 1) / INODES_PER_BLK;

/* Division-free: uint8_t wraps at 256 */
buf[head++] = c;
```

## MOVEM — The 68000's Secret Weapon

`MOVEM.L` saves or restores multiple registers in a single instruction.
Used in three critical places:

### Context switch

```asm
movem.l d0-d7/a0-a6, -(sp)    ; save 15 regs in ONE instruction
; ... switch process ...
movem.l (sp)+, d0-d7/a0-a6    ; restore 15 regs
```

### Interrupt handlers

Save only caller-saved regs for C-calling handlers:
```asm
movem.l d0-d1/a0-a1, -(sp)    ; 4 regs
jsr     timer_interrupt
movem.l (sp)+, d0-d1/a0-a1
rte
```

### Syscall entry

```asm
movem.l d1-d7/a0-a6, -(sp)    ; save everything except d0 (return value)
; ... dispatch syscall ...
movem.l (sp)+, d1-d7/a0-a6
rte
```

## 68000 Instruction Timing Reference

At 7.67 MHz, one cycle ≈ 130 ns.

| Instruction | Cycles | Notes |
|------------|--------|-------|
| `MOVEQ #imm, Dn` | 4 | Load 8-bit immediate |
| `MOVE.L Dn, Dm` | 4 | Register-to-register |
| `ADD.L Dn, Dm` | 6 | 32-bit add |
| `LSR.L #n, Dn` | 6+2n | Shift right (n shifts) |
| `AND.L #imm, Dn` | 14 | Long immediate AND |
| `CMP.L Dn, Dm` | 6 | Compare |
| `BCC.S label` | 10/8 | Branch (taken/not taken) |
| `JSR label` | 18 | Jump to subroutine |
| `RTS` | 16 | Return from subroutine |
| `MOVEM.L regs, -(SP)` | 8+8n | Save n registers |
| `DIVU.W src, Dn` | 76-136 | 32÷16 unsigned divide |
| `DIVS.W src, Dn` | 122-156 | 32÷16 signed divide |
| `MULU.W src, Dn` | 38+2n | 16×16 multiply (n=1-bits in src) |
| `TRAP #n` | 34 | Software interrupt |
| `RTE` | 20 | Return from exception |

## Memory-Mapped I/O

The 68000 uses memory-mapped I/O (no separate I/O address space).
Hardware registers are accessed through volatile pointers:

```c
#define REG16(addr) (*(volatile uint16_t *)(addr))
REG16(0xC00004) = value;  // write VDP control port
```

Always use `volatile` for hardware registers to prevent the compiler
from optimizing away reads/writes or reordering them.

## Mega Drive VDP Access

VDP registers are accessed through two ports:
- `0xC00000` — data port (16-bit)
- `0xC00004` — control port (16-bit)

All VDP access must be word-aligned. DMA transfers must not cross
128 KB boundaries in 68000 address space. The VDP is clocked
independently and has its own address space (64 KB VRAM, 128 bytes
CRAM, 80 bytes VSRAM).
