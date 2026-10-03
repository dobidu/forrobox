---
phase: 14-remaining-debt
plan: 04
subsystem: ui
tags: [ValueScreen, baseline, typography, visual-checkpoint]

requires:
  - phase: 08-polish
    provides: "type::baselineIn (08-04), the rule drawTracked uses"
provides:
  - "ValueScreen on type::baselineIn; kBaselineFromCentre deleted"
  - "TrackedRun::baselineFromCentre: the rule's offset carried by the run, no second Font in paint"
affects: [v0.2 close]

key-files:
  modified: [src/ValueScreen.h, src/ValueScreen.cpp, src/Typography.h, src/Typography.cpp, src/HeaderBar.cpp, src/Settings.h, src/Settings.cpp, src/Chassis.cpp, src/Effects.h, tests/UiTest.cpp, tests/StateRoundTripTest.cpp, tests/RigStart.h]

key-decisions:
  - "One baseline rule; the look approved by the user at the checkpoint"

duration: ~1 session
completed: 2026-10-03
description: "ValueScreen places its text on type::baselineIn, the one baseline rule, retiring its 0.35 approximation — every readout moves up 0.65-1.35 px, approved at a visual checkpoint; /simplify closes Phase 14"
type: Summary
about: "Forró Box"
---

# Phase 14 Plan 04: One baseline rule

**There is one baseline rule in the codebase now.** `ValueScreen` had its own approximation, 0.35 of
the row below centre. It now places its value and its suffix on `type::baselineIn`, the rule
`drawTracked` uses everywhere else.

Every readout moves up:

| Readout | Shift |
|---------|-------|
| BPM | 1.35 px |
| Global knob readouts | 0.98 px |
| Preset screen | 0.65 px |

The plan's 0.75 px estimate came from a single style. **The user approved the before/after at the
checkpoint.**

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: one rule | **Pass** | `kBaselineFromCentre` is deleted. Both baselines come from the rule; after /simplify, through the run's own offset |
| AC-2: rendered on the rule | **Pass** | A 4x render with red ink: the bottom of the ink lies within 0.35 px of `baselineIn` in every style. The run's offset equals the rule's exactly (1e-4). Restoring 0.35 FAILS all three styles |
| AC-3: nothing else moves | **Pass** | Each chassis render changes in exactly its four `ValueScreen`s; the kit renders change only in the BPM field. Every knob, logo and pad render is identical. JUCE clips a component's painting to its bounds, so nothing outside can move |
| AC-4: the user approves | **Approved** | Magnified before/after crops of BPM, preset screen, SWING and CACHAÇA, in dark and light |
| AC-5: green everywhere | **Pass** | Gate 3/3; 5143/5143 on GCC 13, Clang 18 and MSVC 2022 (Debug suite 5147, 0 unexpected, 4 expected); Windows pluginval PASS |

## /simplify at Phase 14 close (required): four agents

**Applied:**
- **No second Font per paint.**
  - `baselineIn` builds a Font, and `ValueScreen` called it twice per repaint.
  - `TrackedRun` now carries `baselineFromCentre`, computed from the Font its layout already built.
  - A test pins that offset to `baselineIn`'s exactly.
- **One refusal mechanism in the store.**
  - `seedSnapshot` now refuses a call from inside a notification, as `set` does.
  - `notify`'s unreachable guard and its stale comment are gone. In its place is an assertion that
    `notify` is never re-entered.
  - The refusal is proved in the Debug suite. In Release the mutation is invisible, because
    `published` is set before any listener runs, so a nested seed finds nothing new.
- **`StringPairArray`s moved, not copied**, in `writeUnderLock`.
- **`operator!=` deleted.** The project builds as C++20, which derives it.
- **Stale comments:**
  - `Chassis::applySettings`'s reason for its family check now describes the store setting the
    global first;
  - `Effects.h`'s pointer to `compositeOver`, which stayed in the `.cpp`, is fixed;
  - an over-long comment line in `set` is re-wrapped.
- **Tests:**
  - `renderComponentScaled`;
  - one `kRetiredBaselineFraction` shared by the two tests that measure it;
  - `writeAsOtherProcess` in `RigStart.h`, replacing 4 copies;
  - an include moved into its group.

**Skipped:**
- **A general hook list for process-global settings** in place of `notify`'s direct
  `setMonoFamily`. There is one global; generalising for hypothetical ones is speculative.
- **The double apply on a notifying seed**, documented at 14-01. JUCE coalesces the second repaint.
- **Rule-based skipping in `verify-geometry`** for names `verify-theme` owns. It is two entries.
- **Retiring 08-04's wash-last checks** as duplicates of 14-02's order test. That test is 08-04's
  record, and it also checks the hit test.
- **A shared lowest-ink helper.** The three scans use different colour rules.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Estimate corrected | 1 | The shift is 0.65–1.35 px by style, not 0.75 |
| Scope (simplify) | 1 | `seedSnapshot`'s refusal and the run-carried offset |

## Next
**Phase 14 complete, and with it every phase of v0.2.** Next is `/paul:complete-milestone`.

---
*Phase: 14-remaining-debt, Plan: 04 · Completed: 2026-10-03*
