# Bug 22, 23, 24: Pipeline Adventure — Timer Underflow and Heap Leak

## Bug 24: Emulator Timer Never Fires (unsigned int underflow)

### Symptoms

Preemptive scheduling never actually worked. The timer interrupt was supposed
to fire at 100 Hz, but `ls /bin | more` and other pipelines only worked
cooperatively (blocking on pipe I/O triggered schedule()). Predicted crash
under preemption: `PC=0x20100 SR=0x53e9 Access=0x4e900004`.

### Root Cause

In `emu/emu68k.c`, `cycles_until_tick` was declared as `unsigned int`.
The check `if (cycles_until_tick <= 0)` is **always false** for unsigned
values (except when exactly 0, which almost never happens):

```c
unsigned int cycles_until_tick = CYCLES_PER_TICK;  // 76700
...
cycles_until_tick -= cycles;  // when 700 - 1000 → wraps to 4294966996
if (cycles_until_tick <= 0) {  // NEVER TRUE (unsigned can't be negative)
```

The subtraction wraps to a huge positive number, and the `<= 0` comparison
against an unsigned value is equivalent to `== 0`. Since `m68k_execute(1000)`
returns approximately 1000 cycles and CYCLES_PER_TICK is 76700, the counter
almost never hits exactly 0. Result: the timer essentially never fires.

### Fix

Changed `unsigned int` to `int` for both `cycles_until_tick` and the
`cycles` return value in `emu/emu68k.c`. With signed arithmetic, the
subtraction correctly produces negative values, and `<= 0` triggers
as intended.

### Analysis of Predicted Crash

With the timer now firing at 100 Hz, preemptive scheduling is enabled.
The timer ISR (`_vec_timer_irq` in `kernel/crt0.S`) correctly:
- Checks SR bit 5 (S flag) — only preempts from user mode
- Saves full user state (70 bytes) on the per-process kstack
- Calls `schedule()` which may `swtch()` to another process
- Restores state and RTEs when re-scheduled

The kernel is NOT preemptible: syscall handlers run in supervisor mode (S=1),
and the timer ISR's `.Ltimer_super` path only increments `ticks` (no
schedule). Maximum kstack depth analysis:
- Timer preemption from user mode: ~150 bytes
- Deep syscall + timer in supervisor mode: ~300 bytes
- Blocking syscall + schedule: ~320 bytes

All well within KSTACK_SIZE (512 bytes). The predicted crash was not
reproducible after the timer fix — preemptive scheduling works correctly.

---

## Bug 22: Heap Leak — "Out of space" After Pipeline

### Symptoms

Running `echo A` then `echo B | cat` reports "Out of space". Each pipeline
leaks ~504 bytes of user heap, exhausting sbrk capacity after a few runs.

### Root Cause

The vfork child runs in the parent's address space. When dash evaluates a
pipeline command, it calls `ckmalloc` → `malloc` → `sbrk` **before exec**.
`sbrk_proc()` correctly redirects to the parent's memory region (the parent
is frozen in P_VFORK), but the parent's `brk` is permanently increased.

After the child execs (getting its own memory), the parent resumes with
`brk` at the higher value. The ~504 bytes between old_brk and new_brk are
"leaked" — they're allocated from the parent's perspective but never freed.

### Fix

Added `saved_brk` field to `struct proc` in `kernel/kernel.h`. Before
blocking the parent in `do_vfork()`, save `parent->saved_brk = parent->brk`.
When the parent resumes (in both `do_exec()`'s vfork path and `do_exit()`'s
vfork path), restore `parent->brk = parent->saved_brk`.

This undoes any sbrk growth caused by the child's pre-exec malloc calls.

### Files Changed

- `kernel/kernel.h`: Added `saved_brk` to `struct proc`
- `kernel/proc.c`: Save brk in `do_vfork()`, restore in `do_exit()` vfork path
- `kernel/exec.c`: Restore brk in `do_exec()` vfork path
- `emu/emu68k.c`: Fixed unsigned timer underflow

### Testing

- `make test`: 378 host tests pass
- `make test-emu`: 33 autotest cases pass (workbench emulator)
- `make test-md-auto`: BlastEm Mega Drive autotest passes
- `make test-dash`: 22/23 pass (1 pre-existing mkdir+rmdir failure)
