---
phase: 14-remaining-debt
plan: 03
subsystem: gates
tags: [verify-geometry, verify-theme, Effects.h, constants, coverage]

requires:
  - phase: 08-polish
    provides: "08-05's src/Effects.h and its rule"
  - phase: 10-build-tooling
    provides: "gate_inputs.py: inputs declared once, enforced at run time"
provides:
  - "Effects.h: every design number of the two treatments, the wash's gradient table included, and nothing else"
  - "Chassis::kSwayCommitDegrees beside the sway it quantises"
  - "verify-theme reading the wash table out of Effects.h"
affects: [14-04]

key-files:
  modified: [src/Effects.h, src/EffectOverlay.cpp, src/Chassis.h, src/Chassis.cpp, scripts/verify-theme.py, scripts/verify-geometry.py]

key-decisions:
  - "Hold the header's stated rule, over a GEOMETRY_HEADERS glob (user, 14-03 planning; the glob measured 104 more constants in 22 headers)"
  - "Excuse only names the walk produces; the struct table and the enum are verify-theme's alone"

duration: ~1 session
completed: 2026-10-03
description: "src/Effects.h holds what its header says — the wash's gradient table moved in from EffectOverlay.cpp, the sway's render cadence moved out to Chassis.h — and both gates were repointed and shown to still bite"
type: Summary
about: "Forró Box"
---

# Phase 14 Plan 03: Effects.h holds its rule

**One feature's numbers, one header, each with a named checker.** 08-05 created `Effects.h` to end a
split, and this plan finished that work.
- **In:** the wash's gradient table, from the `.cpp` that `verify-theme` used to text-parse.
- **Out:** the one non-design value, the sway's render cadence, to `Chassis.h` beside the sway it
  quantises.

No value changed.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: design numbers only | **Pass** | `RadialWashLayer`, `kRadialLayers` and `kLinear*` are in `drunk::`. `kSwayCommitDegrees` is now `Chassis`'s private `static constexpr`. `EffectOverlay.cpp` declares no design constant |
| AC-2: nothing leaves a gate | **Pass** | `verify-theme` checks the same 16 wash values, out of `Effects.h`, which it declares through `gate_inputs`. `verify-geometry` coverage passes: the excuse is renamed to `Chassis::kSwayCommitDegrees`, and both linear alphas are named to `verify-theme` |
| AC-3: the gates still bite | **Pass** | A radial alpha 0.42 → 0.43 and `kLinearEndAlpha` 0.13 → 0.14 both FAIL `verify-theme`. A stray `drunk::kStray` and the deleted cadence excuse both FAIL `verify-geometry` coverage |
| AC-4: no pixel change, green | **Pass** | The suite is unchanged (wash renders included). Gate 3/3; 5132/5132 on GCC 13, Clang 18 and MSVC 2022; Windows pluginval PASS |

## Measured during apply
`verify-geometry`'s scope walk reads `kLinearTopAlpha` and `kLinearEndAlpha` but produces no name for
`kRadialLayers` (a struct table) or `kLinearAccent` (an enum). Those two get NO excuse entry: an
excuse for a name the walk never yields would itself fail the coverage check. `verify-theme`
compares every field of both.

## Skill audit
`/code-review` was not required: no processor or audio-thread code.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Excuses | 2, not 4 | Only the names the walk produces (see above) |

## Next
**14-04:** `ValueScreen` on `type::baselineIn`, with a visual checkpoint. It is the last plan of
Phase 14 and of v0.2, so `/simplify` runs at its UNIFY.

---
*Phase: 14-remaining-debt, Plan: 03 · Completed: 2026-10-03*
