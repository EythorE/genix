# Stale Documentation Report — 2026-03-17

This report identifies documentation that is outdated, contradicts the
current code, or needs consolidation. Generated as part of the kernel
review on 2026-03-17.

---

## Definitely Stale

### 1. README.md line 165: `apps-md` Link Address

**Current text:**
```
make apps-md         # Build user programs (Mega Drive, linked at 0xFF8000)
```

**Reality:** Since Phase 6 (relocatable binaries with `-msep-data`), all
user programs are linked at address 0 and relocated at exec() time. The
`apps-md` target may not even exist anymore — the Makefile uses `make apps`
for both platforms. The 0xFF8000 address is the old fixed USER_BASE.

**Fix:** Update to reflect relocatable binary scheme, or remove `apps-md`
line if the target no longer exists.

---

### 2. Makefile line 188: `test-levee` Marked "KNOWN BROKEN"

**Current text:**
```makefile
# Test levee editor (KNOWN BROKEN — crashes with kernel panic).
# Not included in test-all; run manually to check progress.
```

**Reality:** Bug 17 (levee missing `-msep-data`) was fixed. The kernel panic
at PC=0x30000 was caused by a5 register corruption, which is resolved. Levee
runs on the workbench. It may still be too tight on Mega Drive RAM (12.9 KB
data+bss vs ~27.5 KB available), but the "kernel panic" crash is fixed.

**Fix:** Either:
- Test levee and update the comment to reflect current status
- Change to "KNOWN LIMITATION: Too large for Mega Drive (12.9 KB data+bss)"
- If levee works on workbench, include it in workbench test suite

---

### 3. PLAN.md Contains Completed Phase Outcomes

**Issue:** PLAN.md header implies forward-looking content ("What remains to
be built") but includes detailed descriptions and outcome sections for
completed Phases 5, 6, C, and D. This makes the document half-roadmap and
half-history.

**Per CLAUDE.md documentation workflow:**
> After executing a plan, add an "## Outcome" section documenting what was
> actually implemented, deviations from the plan, and problems encountered.

The outcomes are correctly placed in PLAN.md per the rules. However, the
completed phase *descriptions* (not just outcomes) could be trimmed to
one-line summaries with a pointer to HISTORY.md for details.

**Fix:** Keep outcome sections in PLAN.md (per rules), but condense the
completed phase descriptions to brief summaries. Move detailed descriptions
to HISTORY.md if not already there.

---

### 4. Line Count References

**Locations claiming ~5,400 or ~6,500 kernel lines:**
- CLAUDE.md line 5: "~6500 lines of kernel code"
- README.md line 4: mentions kernel size

**Reality (from status-report-2026-03-15.md):**
- Kernel proper (kernel/*.c + kernel/*.h): ~5,884 lines
- PAL Mega Drive (pal/megadrive/): ~4,702 lines
- Total kernel+PAL: ~10,586 lines

The ~6,500 figure in CLAUDE.md is reasonable if counting kernel/*.c,
kernel/*.h, and kernel/*.S together. The key question is whether PAL
code "counts" as kernel code. It's part of the kernel binary but
platform-specific.

**Fix:** Clarify what's counted. Suggest: "~6,500 lines of core kernel code
(plus ~4,700 lines of platform-specific code)".

---

## Possibly Stale

### 5. PLAN.md: "Tier 1 remaining: ed"

PLAN.md references ed (line editor) as remaining Tier 1 work, but Tier 1
is described as complete elsewhere (HISTORY.md, CLAUDE.md). ed was dropped
from Wave 2 — only more and sort were ported.

**Fix:** Clarify ed's status — is it Tier 1 (essential) or Tier 2 (optional)?
If optional, move it to the optional apps list.

---

### 6. docs/plans/shell-plan.md: Phase C/D Completion Status

The shell plan documents Phases A-D in detail. With C and D complete, the
plan should have outcome sections (per CLAUDE.md rules). Verify these
exist and are accurate.

---

### 7. docs/plans/relocation-plan.md: Phase 5/6 Completion Status

Similar to shell-plan.md — verify outcome sections exist for the completed
relocation work. The research in relocatable-binaries.md is canonical and
shouldn't change, but the plan document should reflect what was built vs.
what was planned.

---

## Documentation Gaps (Not Stale, But Missing)

### 8. No `docs/adding-devices.md`

There's a checklist for adding syscalls and adding programs in CLAUDE.md,
but no equivalent for adding device drivers. With Phase 7 (SD card) ahead,
documenting the device registration pattern would be valuable.

### 9. No BlastEm Configuration Documentation

BlastEm config is embedded in the Makefile as a heredoc. There's no
standalone document explaining:
- What config options are used and why
- How to configure BlastEm for mapper testing (Phase 8)
- How `ram_init zero` affects test reproducibility
- Saturn keyboard setup for interactive testing

A `docs/blastem-config.md` would help.

### 10. No PAL Interface Specification

The PAL (Platform Abstraction Layer) is well-designed but undocumented as
an interface contract. `docs/architecture.md` mentions it exists but doesn't
list the required functions or their signatures. When adding a new platform
or modifying hardware support, developers must read the source.

---

## Contradictions Between Documents

### 11. User Program Count

Most documents say 47 user programs. This should be verified against the
actual `PROGRAMS` list in `apps/Makefile` and the `CORE_BINS` list in the
top-level Makefile to ensure accuracy.

---

## Recommendations

| # | Item | Effort | Priority |
|---|------|--------|----------|
| 1 | Fix README.md apps-md reference | 5 min | Medium |
| 2 | Update test-levee comment in Makefile | 5 min | Medium |
| 3 | Condense completed phases in PLAN.md | 30 min | Low |
| 4 | Clarify line counts | 10 min | Low |
| 5 | Clarify ed status in PLAN.md | 5 min | Low |
| 6 | Verify outcome sections in shell-plan.md | 15 min | Low |
| 7 | Verify outcome sections in relocation-plan.md | 15 min | Low |
| 8 | Write docs/adding-devices.md | 30 min | Medium |
| 9 | Write docs/blastem-config.md | 45 min | Medium |
| 10 | Document PAL interface in architecture.md | 30 min | Medium |
