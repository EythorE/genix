# Genix Development Guidelines

See [README.md](README.md) for project overview, build targets, and architecture.
See [PLAN.md](PLAN.md) for the forward roadmap.

**The Mega Drive is the primary target.** 64 KB main RAM, 7.67 MHz 68000, no MMU,
optional SRAM. The workbench emulator is for iteration speed only.

## Testing Rules

Every change must pass the testing ladder before it's done:

1. `make test` — host unit tests (no cross-compiler)
2. `make kernel` — cross-compilation check
3. `make test-emu` — workbench autotest (STRICT_ALIGN + AUTOTEST)
4. `make megadrive` — Mega Drive build
5. `make test-md-auto` — BlastEm AUTOTEST (**primary quality gate**)

Or: `make test-all` for the full ladder.

- Host tests first: every kernel subsystem needs a `tests/test_*.c`
- If a test passes in workbench but fails in BlastEm (or vice versa), investigate the discrepancy — see `docs/automated-testing.md`

## Kernel Style

- Target: ~5,000 lines, readable in one sitting
- One `.c` file per subsystem, no deep call hierarchies
- Functions under 80 lines. Global state is OK.
- Single source of truth for declarations — never duplicate across headers
- Return negative errno on failure. Never swallow errors.
- `kprintf` for warnings, `panic` only for unrecoverable state

## 68000 Rules

- **ABI**: d0-d1/a0-a1 caller-saved, d2-d7/a2-a6 callee-saved, return in d0
- **Alignment**: stack must be even, no word/long access at odd addresses — address error is fatal
- **MOVEM**: use for bulk register save/restore (context switch, TRAP handler, setjmp)
- **JMP not JSR** for user entry in exec — JSR corrupts argc/argv stack layout

### Division (no 32÷32 hardware)

- Powers of 2: use `>> n` and `& (n-1)` explicitly
- 16-bit constant divisors: OK, annotate with `/* DIVU.W safe */`
- Never divide by a 32-bit runtime variable in a hot path
- Circular buffers: 256-byte with `uint8_t` index (wraps naturally)

### Assembly for Hot Paths

- memcpy/memset: MOVE.L for medium, MOVEM.L for large. Never byte-at-a-time.
- Block copies: fully unrolled MOVEM.L (see FUZIX's `lowlevel-68000.S`)
- DBRA for counted loops, `(a0)+` post-increment in copy loops
- Check `OPTIMIZATION_PLAN.md` before writing memory/arithmetic routines
- When porting from FUZIX, always prefer the `_68000.S` assembly variant

## Adding Syscalls

1. Add `SYS_FOO` to `kernel/kernel.h`
2. Implement `sys_foo()` in the subsystem file
3. Add case to `syscall_dispatch()` in `kernel/proc.c`
4. Add libc stub in `libc/syscalls.S`
5. Add host test

## Adding User Programs

1. Create `apps/foo.c` with `int main(int argc, char **argv)`
2. Add `foo` to `PROGRAMS` in `apps/Makefile`
3. Automatically included in disk image at `/bin/foo`

## Build System

- `make test` needs only host `gcc`
- `make kernel` needs `m68k-elf-gcc` (set `CROSS=m68k-elf-`)
- Fallback: `CROSS=m68k-linux-gnu-` (pass `-m68000`, beware 68020 libgcc)
- Genix provides `divmod.S` to avoid libgcc 68020 instructions
- Keep Makefiles simple — one level of `$(MAKE) -C`

## Common Pitfalls

- **68020 in libgcc**: distro `libgcc.a` has BSR.L — use `m68k-elf-gcc` or ensure `divmod.S` takes priority
- **Unaligned buffers**: `char buf[13]` on stack → odd address → fault. Use even sizes.
- **USER_BASE/USER_TOP**: platform-specific (workbench: 0x040000, Mega Drive: 0xFF9000), set by PAL, never hardcode
- **Workbench passes, MD fails**: different linker scripts, memory, VDP vs UART. Always verify both.

## Memory Layout

### Mega Drive (64 KB RAM)
```
ROM:      Vectors + kernel .text/.rodata + romdisk
0xFF0000  Kernel .data + .bss (~35 KB, includes 16 × 512B kstacks)
0xFF9000  USER_BASE (~27.5 KB for user programs)
0xFFFE00  USER_TOP (stack grows down)
0xFFFFFF  Kernel stack
SRAM:     0x200000 (optional, cartridge-dependent)
```

### Workbench (1 MB RAM)
```
0x000000  Vectors + kernel
0x040000  USER_BASE
0x0F0000  USER_TOP
0x100000  Kernel stack
```

## Key References

| Document | When to check |
|----------|---------------|
| `docs/decisions.md` | Before any design choice |
| `docs/automated-testing.md` | When tests behave differently across platforms |
| `docs/megadrive.md` | Cartridge configs, SRAM, memory layout |
| `OPTIMIZATION_PLAN.md` | Before writing memory/arithmetic routines |
| `HISTORY.md` | For context on past bugs and decisions |
