# Genix — Claude Code Instructions

## Project Overview

Genix is a minimal, single-user, POSIX-enough OS for the Motorola 68000,
targeting the Sega Mega Drive (~5400 lines of kernel code).

**The Mega Drive is the primary target.** The workbench emulator exists only to
accelerate development — every feature must ultimately run on real Mega Drive
hardware. Design decisions should always consider the Mega Drive's constraints:
64 KB main RAM, 7.67 MHz CPU, no MMU, optional cartridge SRAM.

## Project Stage & Current Focus

Phases 1-4 complete. Phase 5 (ROM XIP) is the next major milestone.

Phases 5-8 are architecturally coupled — Phase 5 decisions constrain
Phases 6-8. See PLAN.md for the full dependency graph.

Design choices that are LOAD-BEARING (changing these is expensive):
- Binary format and relocation scheme (affects all 34 apps + libc + mkbin)
- Memory layout (USER_BASE, kernel/user RAM split, process slot sizes)
- ROM vs RAM text placement strategy (Phase 5 core question)
- Syscall numbers and calling convention (ABI stability across libc)
- Linker script structure (affects both platforms)

Design choices that ARE flexible right now:
- Individual app implementations
- Shell built-in command set
- Test harness internals
- Documentation formatting
- Buffer sizes, cache tuning knobs

## Decision-Making Rules

- When a plan step has a simple-vs-complex choice, STOP and explain
  the tradeoff. Do not decide autonomously which path to take.
- When encountering a problem during plan execution, STOP and report
  the problem. Do not redesign the approach or fall back to a simpler
  alternative. The plan was created with context that may have been
  compacted away from your current context window.
- If you find yourself wanting to "just do the simple version," check
  the plan's rejected approaches section first. There is usually a
  documented reason the simple version was rejected.
- "Simpler" code that uses more RAM is NOT simpler on this platform.
  Always verify RAM impact before choosing an approach.
- When unsure whether a design choice is load-bearing or flexible,
  ASK. Do not guess.

## Key Design Decisions

- **Mega Drive first** — real hardware is the target, emulators are for iteration
- **No fork()** — vfork()+exec() only (no MMU required)
- **No multi-user** — no UIDs, permissions, login
- **Custom filesystem (minifs)** — classic Unix inode layout
- **Single-tasking first** — then add multi-tasking
- **Workbench emulator** for rapid development (Musashi 68000 SBC)
- **BlastEm** for Mega Drive validation before real hardware
- **Relocatable binary format** — flat binary linked at 0, relocated at exec() time
- **Modular memory layout** — supports different cartridge configurations
- **SRAM is optional** — system runs on 64 KB main RAM alone; SRAM adds
  persistent storage and extended RAM
- **Flexible choices are cheap** — if a non-load-bearing convention is
  hurting us, change it. But verify it's actually flexible first (see
  load-bearing list above). Binary format, memory layout, syscall ABI,
  and linker structure are NOT cheap to change.

## Development Guidelines

### Testing rules

- **Host tests first**: Every kernel subsystem must have a `tests/test_*.c`.
- **Never skip `make test`**: Run it before every commit.
- **`make kernel` and `make megadrive` are mandatory**: They catch ABI
  mismatches and cross-compilation errors that host tests miss.
- **BlastEm is mandatory**: Run `make test-md-auto` (or `make test-all`)
  before considering any change done.
- **Discrepancy = bug**: If a test passes on one platform but fails on
  the other, investigate — see `docs/automated-testing.md`.

### Kernel style

- Target: entire kernel readable in one sitting (~3000-5000 lines total).
- One `.c` file per subsystem. No deep call hierarchies. Prefer linear code.
- If a function is over 80 lines, split it.
- Global state is OK. Don't over-abstract.
- **Single source of truth for declarations**: Never duplicate function
  declarations across headers. Use `#include` instead.

### Error handling

- Kernel code returns negative errno on failure (e.g., `-ENOENT`).
- Never silently swallow errors. If a function can fail, the caller must check.
- Use `kprintf` for warnings, `panic` only for truly unrecoverable state.
- Validate all user-supplied pointers/sizes at the syscall boundary.

### Adding new syscalls

1. Add `SYS_FOO` number to `kernel/kernel.h`
2. Implement `sys_foo()` in the appropriate subsystem file
3. Add case to `syscall_dispatch()` in `kernel/proc.c`
4. Add libc stub in `libc/syscalls.S`
5. Add host test if the logic is testable
6. Update this file's syscall list if it's user-visible

### Adding new user programs

1. Create `apps/foo.c` with a standard `int main(int argc, char **argv)`
2. Add `foo` to the `PROGRAMS` list in `apps/Makefile`
3. The build system links with crt0 + libc and produces a Genix binary
4. `make run` automatically includes it in the disk image at `/bin/foo`

### Design flexibility

- USER_BASE/USER_TOP and SRAM configuration are platform-provided (via PAL),
  never hardcoded in the kernel.
- SRAM is optional — the system must boot on 64 KB main RAM alone.
- Libc grows as we port more programs. Don't duplicate — add to libc.
- If a non-load-bearing decision is causing pain, change it. Document why
  in the commit message.

## Plan & Documentation Workflow

- Research documents (docs/*-research.md, docs/relocatable-binaries.md)
  are canonical references. Never delete or overwrite them.
- Implementation plans (docs/*-plan.md) are companions to research,
  not replacements. Both must be updated together.
- After executing a plan, add an "## Outcome" section documenting
  what was actually implemented, deviations from the plan, and
  problems encountered. Never delete a plan after execution.
- When research reveals a naive approach won't work, document WHY
  in the plan under "## Rejected Approaches" with enough detail
  that a future session (which may have lost context) understands
  the reasoning.
- PLAN.md is the forward roadmap. It should always reflect the
  current state of what's planned and what's completed.

## Documentation Preservation Rules

Project history is a critical asset. Follow these rules strictly:

- **Every design decision, bug fix, and pain point must be recorded in
  `HISTORY.md`** with enough detail that a future developer can understand
  the root cause, the fix, and the lesson learned. Never discard this
  information.
- **Never remove content from documentation without relocating it first.**
  If trimming a document for readability, move the removed content to the
  appropriate place (`HISTORY.md` for resolved history, `docs/` for
  technical details) before deleting it from the original file.
- **HISTORY.md is append-mostly.** Add new sections at the end of the
  relevant chapter (e.g., new bugs go in "Bugs and Lessons Learned").
  Do not summarize away existing detail — the verbose version is the
  valuable one.
- **README.md is user-facing and should keep all completed phases** in the
  status table with their descriptions. Do not remove completed items.
  Only add new items or update status.
- **CLAUDE.md Common Pitfalls must stay in CLAUDE.md** — these are active
  developer warnings, not historical notes. They may be duplicated in
  HISTORY.md for context but must remain here for quick reference.
- **When consolidating or reorganizing docs**, always diff before and after
  to verify no content was dropped. If in doubt, keep the longer version.

## Reference Documentation

Technical details live in docs/, not here. Key references:
- docs/architecture.md — memory maps, system layers
- docs/kernel.md — kernel subsystems
- docs/binary-format.md — Genix binary header and relocation
- docs/68000-programming.md — ISA constraints, ABI, division rules
- docs/megadrive.md — MD hardware, cartridges, SRAM, memory layout
- docs/automated-testing.md — testing ladder, discrepancy procedures
- docs/relocatable-binaries.md — relocation research (1128 lines, canonical)
- OPTIMIZATION_PLAN.md — performance gaps vs FUZIX with source refs
- PLAN.md — forward roadmap (Phases 5-9)
- HISTORY.md — full project timeline (for human reference)
- docs/decisions.md — active design decisions
- docs/shell-plan.md — phased plan: libc prereqs → kernel zones → userspace shell → dash port

Read the relevant docs/ file when working on a subsystem.
Do not duplicate their content into this file.

## Build & Test Quick Reference

1. **`make test`** — host unit tests (logic, no hardware)
2. **`make kernel`** — cross-compilation check
3. **`make test-emu`** — workbench autotest (STRICT_ALIGN + AUTOTEST)
4. **`make megadrive`** — Mega Drive build
5. **`make test-md`** — headless BlastEm boot (~5s)
6. **`make test-md-auto`** — BlastEm autotest (**primary quality gate**)
7. **Real hardware** — flash cartridge on a real Mega Drive

Run **`make test-all`** for the full ladder. Steps 4-6 must pass before
considering any change done.

## Common Pitfalls

These are lessons learned from debugging sessions (documented in full in
`HISTORY.md`). Keep them in mind:

- **68020 instructions in libgcc**: The distro `libgcc.a` contains `BSR.L`
  and other 68020 opcodes. Programs using `/` or `%` will crash on real
  68000 hardware. Fix: use `m68k-elf-gcc` (from `fetch-toolchain.sh`) or
  ensure `divmod.S` symbols take priority over libgcc.
- **Unaligned stack buffers**: `char buf[13]` on the stack may land at an
  odd address. The 68000 faults on word/long access at odd addresses.
  Always use even-sized local buffers.
- **USER_BASE/USER_TOP are platform-specific**: Workbench uses
  `0x040000-0x0F0000`, Mega Drive uses `0xFF8000-0xFFFE00`. These are set
  by PAL functions at boot, never hardcoded in the kernel.
- **JMP vs JSR for user entry**: `exec_asm.S` must use `JMP` (not `JSR`)
  to enter user programs — `JSR` pushes a return address that corrupts the
  argc/argv stack layout.
- **Workbench passes, Mega Drive fails**: Different linker scripts, memory
  layouts, and VDP vs UART output. Always verify both `make run` and
  `make megadrive`. See `docs/automated-testing.md` for the investigation
  procedure.

## Auto-Memory Rules

Do NOT add reference documentation to this file. If you learn
something about a subsystem, add it to the relevant docs/ file.
This file is ONLY for:
- Behavioral rules (how to work on this project)
- Project priorities (what matters, what doesn't)
- Active warnings (common pitfalls)
- Procedural checklists (adding syscalls, adding programs)

If this file exceeds 200 lines, something has been added that
belongs elsewhere. Identify it and move it before it accumulates.
