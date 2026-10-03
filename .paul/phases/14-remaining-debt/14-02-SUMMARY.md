---
phase: 14-remaining-debt
plan: 02
subsystem: ui
tags: [z-order, overlays, always-on-top, ABOUT, kit panel, wash]

requires:
  - phase: 08-polish
    provides: "the three overlays and 08-04's childrenChanged re-fronting"
provides:
  - "Chassis::stackOverlays: the one place the overlays are ordered (kit < ABOUT < wash)"
  - "AboutOverlay always-on-top by construction"
  - "a fix: ABOUT no longer opens underneath an open kit panel"
affects: [14-03, 14-04]

key-files:
  modified: [src/Chassis.h, src/Chassis.cpp, src/KitOverlay.cpp, src/AboutOverlay.cpp, tests/UiTest.cpp]

key-decisions:
  - "An ordinal in one function, not a reactive hook: all three always-on-top; toFront order inside the group IS the order"

duration: ~1 session
completed: 2026-10-03
description: "The three always-on-top overlays get one owner of their order, and the test written first found ABOUT opening underneath an open kit panel"
type: Summary
about: "Forró Box"
---

# Phase 14 Plan 02: One owner for the overlays' order

**The overlays' order is one rule in one function, and writing the test first found a bug.**

ABOUT was not always-on-top. JUCE stops a plain child's `toFront` BELOW every always-on-top
sibling, so ABOUT sat under the kit panel from attach onward. Opened from the gear while the kit
panel was open, it opened underneath, while a comment said "LAST so it sits above the kit overlay".

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: the order is a rule | **Pass** | The last three are [kit, ABOUT, wash] after attach and after every open/close sequence (kit→ABOUT, ABOUT→kit, either closed first). All three are always-on-top. A child added later lands below them |
| AC-2: one owner | **Pass** | `toFront` on the three appears only in `Chassis::stackOverlays`. `childrenChanged` is gone |
| AC-3: the suspected defect | **Confirmed, closed** | Before the change, all 11 order checks FAILED, from "as attached" on. After it, all pass |
| AC-4: green everywhere | **Pass** | Gate 3/3; 5132/5132 on GCC 13, Clang 18 and MSVC 2022 (Debug suite 5135, 0 unexpected); Windows pluginval PASS |

**Mutations, 4 rejected:**
- ABOUT stacked before the kit;
- `KitOverlay::setOpen` regaining `toFront` (the existing 08-04 wash checks catch it);
- ABOUT not always-on-top;
- `stackOverlays` not called.

## What changed
- **`AboutOverlay`'s constructor:** `setAlwaysOnTop (true)`, a property of the component, as the
  kit panel's own comment argues.
- **`Chassis::stackOverlays()`:** `toFront (false)` on the kit, ABOUT and the wash, in that order,
  at the end of `attachParameters`. css:554 puts both panels at z-index 40 with ABOUT after the kit;
  css:91 puts the wash at 60.
- **Deleted:**
  - both `setOpen` `toFront` calls (ABOUT keeps `grabFocusIfVisible` for focus);
  - the attach-time `aboutOverlay->toFront`;
  - `Chassis::childrenChanged` and its doc.
- **The kit's "nothing in front of it" check** now counts CONTROLS. The other overlays are meant to
  be in front of it.

## Skill audit
`/code-review` was not required: UI structure only.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Defect fixed | 1 | ABOUT under the kit panel: a visible change, matching the design |
| Test adjusted | 1 | The kit-in-front check excludes always-on-top overlays |
| Gate catch | 1 | `verify-charset` flagged a non-ASCII test message; that run had used the old binary, and was re-run after the build succeeded |

## Next
**14-03:** `src/Effects.h`'s unit and the geometry gate's enrolment.

---
*Phase: 14-remaining-debt, Plan: 02 · Completed: 2026-10-03*
