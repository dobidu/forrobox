---
phase: 06-side-panel
plan: 05
subsystem: ui
tags: [juce, components, hit-testing, refactor, hoisting]

requires:
  - phase: 06-03
    provides: ProfileButton, the wired flash, and the isVisible asymmetry this removes
provides:
  - SelectableTile — the base ProfileButton and TimbreRow both are
  - HitZone — an invisible clickable child replacing hand-rolled container hit-tests
  - one statement each of the flash law, the click law, the UI poll rate and the kit lane table
  - clickInside / hoverInside — hierarchy-routed test dispatch
affects: [06-06, 07-midi-out]

tech-stack:
  added: []
  patterns:
    - "A container wanting one clickable region puts a CHILD there; only controls override mouse handlers"
    - "A test delivers a click through the hierarchy, not to the component it guesses will receive it"
    - "State has ONE owner: the painter asks the zone rather than reading a mirrored member"

key-files:
  created: [src/SelectableTile.h, src/HitZone.h]
  modified: [src/SequencerGrid.cpp, src/Chassis.cpp, src/PatternPads.cpp, src/Surface.h, src/KitOverlay.h, scripts/verify-charset.py, scripts/verify-geometry.py, tests/UiTest.cpp]

key-decisions:
  - "KitOverlay::mouseUp stays a container override: it is the INVERSE test, and the panel's intercept flag already absorbs clicks inside it"
  - "The click law is a free predicate in Surface.h, not a third base class"
  - "Surface.h enrolled in the geometry gate, because the hoist moved constants out of its reach"
  - "HitZone is header-only, like SelectableTile — a .cpp cost 1.6 s per clean build for 15 lines"

patterns-established:
  - "When a hoist moves a constant between headers, check whether it left an enrolment gate"
  - "A comment asserting a structural guarantee must name the mechanism that provides it"

duration: ~150min
started: 2026-09-20T05:05:00-03:00
completed: 2026-09-20T07:35:00-03:00
description: "Five duplicated shapes get one home each — and three false claims, two checks that could not fail, and a tokenizer bug were found in the work that did it"
type: Summary
about: "Forró Box"
---

# Phase 6 Plan 5: Five shapes, one home each — Summary

**The flash law, the selectable tile, the container hit-test, the UI poll rate and the kit lane
table each have a single home — and the review passes over that work found three false claims, two
checks that could not fail, a silent enrolment escape and a tokenizer bug, all in code written
during this plan or the one before it.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~150 min |
| Tasks | 3 of 3, all PASS |
| Qualify | 3 PASS / 0 GAP / 0 DRIFT |
| Checks | 3787 → **3818** (+31) |
| Compilers | GCC 13, Clang 18, MSVC 2022 — 0 warnings |
| Cross-checks | 4 green; `verify-charset` 0.10 s → **0.04 s** |
| Suite runtime | ~2.99 s (unchanged) |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: flash law stated once | **Pass** | Mutation removing the refresh fails two 06-03 checks |
| AC-2: right-click guard in one place | **Pass** | Mutation fails the named check AND the editor-wide sweep |
| AC-3: selection and clicking unchanged | **Pass** | |
| AC-4: no container hand-tests a rectangle | **Partial** | `KitOverlay::mouseUp` remains — see Deviations |
| AC-5: one poll rate, one lane table | **Pass** | Three distinct rates remain by design; `kitLaneIds` gone |

## Accomplishments

- **`SelectableTile`** — `ProfileButton` and `TimbreRow` had byte-identical constructors, setters and
  `mouseUp` bodies. The right-click guard now exists once; it had gone missing from both.
- **`HitZone`** — five hand-rolled overrides in `SequencerGrid` and one in `Chassis` became children.
  Every remaining mouse override in `src/` is on a control, except the one named below.
- **One `kUiPollHz`, one `kitPieces`** — three copies of 30, and two parallel four-element tables
  plus a third spelling of the short code.
- **`clickInside` / `hoverInside`** — tests now deliver clicks through JUCE's own hit-test, so a zone
  at the wrong bounds, left invisible, or buried by a sibling fails as it would on screen.

## Files Created/Modified

| File | Change | Purpose |
|------|--------|---------|
| `src/SelectableTile.h` | **Created** (94) | The tile base; one right-click guard |
| `src/HitZone.h` | **Created** (100) | Invisible clickable child, header-only |
| `src/Surface.h` | Modified | `kUiPollHz`; `isPlainClickInside`, the one click law |
| `src/SequencerGrid.h`/`.cpp` | Modified | 5 label zones; 3 overrides + 2 helpers + a mirrored member deleted |
| `src/Chassis.h`/`.cpp` | Modified | Sub-dots zone as a private value member; `mouseUp` gone |
| `src/PatternPads.cpp` | Modified | Owns the whole flash law |
| `src/KitOverlay.h`/`.cpp` | Modified | `kitPieces`; forwarder guard restored with correct accounting |
| `scripts/verify-charset.py` | Modified | Single-pass alternation; 11× faster, one fewer failure mode |
| `scripts/verify-geometry.py` | Modified | `Surface.h` enrolled; two orphaned exemptions replaced |
| `tests/UiTest.cpp` | Modified | Routed dispatch; three checks that could not fail, fixed |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| `KitOverlay::mouseUp` stays a container override | It is the INVERSE test — a click anywhere EXCEPT the panel dismisses. The panel's `setInterceptsMouseClicks (true, true)` already absorbs clicks inside it, so the clause only sees press-on-scrim → drag-onto-panel → release | AC-4 partial, deliberately |
| The click law is a free predicate, not a base | `HitZone` and `SelectableTile` are not is-a each other; a third class to de-duplicate two is more structure than the law needs | `Button` and `Segmented` can adopt it later — recorded |
| `Surface.h` enrolled in the geometry gate | The hoist moved constants out of two enrolled headers into an unenrolled one. The gate kept passing | Proved: a new unpoliced constant there now fails the build |
| `HitZone` header-only | 1.6 s of clean-build time for 15 lines, and `SelectableTile.h` — same plan, same content — was header-only | Consistency, and the `.cpp` is gone |
| The shut-overlay guard returns, at the forwarder | The law stays in `PatternPads`; the overlay only declines to RUN it on pads `rebuild()` is about to destroy | Saves a lock + `State` copy + 128 `setVelocity`, not the "128 no-op flashes" the old comment claimed |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 12 | 6 from `/code-review`, 6+ from `/simplify` |
| Scope additions | 2 | Test helpers; `verify-geometry` enrolment |
| Non-findings | 1 | Task 1's second half |
| Criterion missed | 1 | "Net lines removed" — stated wrongly by me, again |

### The three false claims — all mine, all in this plan

**1. "The overlay-is-open guard went STRUCTURALLY."** `/code-review` found the first version false:
the zone was added *after* the overlay, so only `setOpen`'s `toFront` kept it below. I "fixed" it by
reordering the adds and writing *"the order is the whole guarantee."* `/simplify` found **that**
false too: `KitOverlay`'s constructor calls `setAlwaysOnTop (true)`, and `addChildComponent` walks
the insertion index back past every always-on-top sibling — order is irrelevant, and
`KitOverlay.cpp:181` **already said so**, citing the same JUCE line. Two false z-order claims in one
header in one plan. The comment now names `setAlwaysOnTop`, and the zone moved to the constructor
because the ordering it was placed in `attachParameters` to achieve does not exist.

**2. "`hoveredLabelRow` is now ANSWERED by the zones."** It was hand-tracked in my lambda, beside a
`HitZone::isHovered()` that had no caller. The painter now asks the zone; the member is gone. The
`previous`/current bookkeeping was unreachable too — the zones are disjoint siblings, so the set to
repaint was always this row alone.

**3. The shut-overlay accounting.** The header said the guard "was saving ~128 no-op `flash()`
calls." Measured: it was saving `lockPatternState()`, a whole-`State` copy and 128 `setVelocity`
calls as well.

### Three checks that could not fail

- **`clickInside` sent only `mouseUp`.** `Button::mouseUp` early-returns unless `mouseDown` set
  `pressed`, and `StepPad` and `Segmented` gate the same way — so the helper was a silent no-op on
  every Button while still returning non-null. Now sends down-then-up, and fails loudly on an
  invisible container instead of reading as a click that landed on nothing.
- **The kit-panel test bypassed routing.** `overlay.mouseUp (…panel.getCentre())` called directly
  would pass against an overlay with no panel child and no intercept flag. Routed, and it now
  asserts the panel *received* the click.
- **The pad-strip check** would have passed against a grid with no zones at all, because
  `grid.mouseUp` reached nothing after the conversion.

### The enrolment escape

`kHeaderPollHz`, `kFooterPollHz` and `kSidePanelPollHz` lived in headers enrolled in
`verify-geometry.py`'s coverage gate. Hoisting them into `Surface.h` — not enrolled — removed them
from it, left two exemption rows naming constants that no longer exist, and the gate kept passing.
This is the Key Decision recorded 2026-09-16 ("a constant in an enrolled geometry header must be
compared by an expectation, or excused by name") being silently evaded by a move. `Surface.h` is now
enrolled, and a mutation proved the gate fires there.

### The tokenizer bug

06-04's `verify-charset.py` was fixed during this plan's review for reading a comment marker inside
a string. My fix — a hand-rolled character loop — was 11× slower **and** carried its own bug: its
char-literal branch had no newline stop, so a digit separator (`1'000`) desynchronised the rest of
the file. Measured: **zero** literals found in a two-line probe. Replaced with a single-pass
alternation: same 556 literals, 40.8 ms → 3.6 ms, and all four probes correct.

### Non-finding

Task 1's second half asked to collapse "two identical lambdas" in `Chassis` into one named member.
`flashPadsForReload()` already **was** that member; the two lines are registrations of one handler
on two entry points. No change made.

### Criterion missed

"Net lines REMOVED in `src/`" — modified files net +3, new files +194. **Second plan running** I
wrote that criterion while adding files whose doc comments are the house style. Not writing it again.

## Deferred (logged to PROJECT.md)

- `Button` and `Segmented` still state the click law themselves; both gate on `pressed` first and
  `Button` tests `contentBox()`, so adopting `isPlainClickInside` is a change to their files.
- `KitOverlay::mouseUp`'s containment clause and the comment's reason for it.
- `PatternPads.h` has the same orphaned-doc-comment defect (`kToggleOnVelocity`).
- `ValueTooltip.cpp:41` is a bare `startTimerHz (60)` — a fourth, unnamed rate.
- 228 test-message literals carry non-ASCII as `const char*` and render as mojibake on failure; a
  `const char*` overload on `check` doing `fromUTF8` fixes all of them at once.

## Next Phase Readiness

**Ready:** 06-06 (the test seams and the ChassisRig) inherits `clickInside`/`hoverInside`, and the
rig's API question is now better informed — `HitZone` and `SelectableTile` are what the rig will
reach through.

**Concerns:** `AC-4` is partial by decision; one container override remains and is documented.

**Blockers:** None.

---
*Built with PAUL Framework v1.4*
*Phase: 06-side-panel, Plan: 05*
*Completed: 2026-09-20*
