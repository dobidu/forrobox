---
phase: 06-side-panel
plan: 02
subsystem: ui
tags: [juce, vst3, apvts, side-panel, timbre, utf8]

requires:
  - phase: 03-voices
    provides: the character bus and both its parameters, unused by any UI until now
  - phase: 06-side-panel
    provides: 06-01's PatternPads and the enrolment gate this plan was first to meet
provides:
  - SidePanel — the last empty region of the chassis, filled
  - TimbreRow — the three character rows, live on ids::timbre
  - Profile descriptions and timbre sub-labels in C++, cross-checked against data.js
  - The CUSTOM tag, following State::dirty
  - surface::glowDot, PollTimer::secondsSinceLastTick
affects: [06-03 profile reload, 06-04 cleanup]

tech-stack:
  added: []
  patterns:
    - "A control owns its own box model; the owner asks it for a height"
    - "A shared painter does not imply a shared value"
    - "A benchmark that measures the cheap half of a path is worse than none"

key-files:
  created:
    - src/SidePanel.h
    - src/SidePanel.cpp
    - src/TimbreRow.h
    - src/TimbreRow.cpp
  modified:
    - src/ParameterIDs.h
    - src/MixBus.h
    - src/Typography.h
    - src/Chassis.h
    - src/Chassis.cpp
    - src/Surface.h
    - src/Surface.cpp
    - src/KitOverlay.h
    - src/KitOverlay.cpp
    - scripts/verify-profiles.py
    - scripts/verify-geometry.py

key-decisions:
  - "The profile click is inert until 06-03 — a half-reload is worse than a button that does nothing yet"
  - "TimbreRow is its own control, because .timbre is its own CSS rule"
  - "The CUSTOM tag reads State::dirty directly; no publication, and the cost measured"
  - "The timbre displayName is cross-checked with the trademark stripped, Phase 8 owning the ™"

patterns-established:
  - "An accented literal's escape must be terminated, and the compiled value tested"
  - "One exclusion table with reasons, not two lists a check cannot tell apart"

duration: ~5h
completed: 2026-09-17
description: "The 280px side panel — profiles, the CUSTOM tag, live timbre rows and the MIX knob"
type: Summary
about: "Forró Box"
---

# Phase 6 Plan 02: The Side Panel — Summary

**The last empty region of the chassis is filled, and two parameters that have driven the character
bus since Phase 3 finally have a UI.**

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: The region is the prototype's, box for box | **Pass** | 21 new constants, every one compared — the enrolment gate refused the build until they were |
| AC-2: The active profile, with its own description | **Pass** | Descriptions added to C++ for the first time and cross-checked against `data.js`. An unknown id lights nothing |
| AC-3: The timbre rows and MIX are LIVE | **Pass** | The lit row follows the parameter, proved by host automation with no click; the sound differs per character, proved sample for sample |
| AC-4: The CUSTOM tag follows the dirty flag | **Pass** | And the poll measured — see below, because the first measurement was of the wrong half |
| AC-5: Nothing regressed, on three compilers | **Pass** | 3597/3597 on GCC, Clang and MSVC. Three verify scripts green. VST3 installed, hashes match |

## Skill Audit

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/graphify` | ✓ | At Phase 6 planning and again here — `buildSide` is the markup |
| `/code-review` | — | **Gap.** The plan gated it on Task 2's two APVTS bindings and it was not run. `/simplify`'s four agents covered the same diff and found the defects below, but that is not the same instrument |
| `/simplify` | ✓ | During UNIFY. Four agents; the efficiency pass corrected a number I had reported to the user |
| `/impeccable` | — | Optional, not invoked |

## Task Commits

| Task | Commit | Type |
|------|--------|------|
| Task 1: the region, its boxes and its paint | `ddf9280` | feat |
| Task 2: the live controls | `2d8f57d` | feat |
| `/simplify` | `a48cc80` | refactor |

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| The profile click stays inert until 06-03 | The reload's seven-way fan-out is one law and belongs in one plan. A half-reload would leave BPM and timbre disagreeing with the highlighted profile — worse than a button that plainly does not act yet |
| `TimbreRow` is its own control | `.timbre` is its own CSS rule with two stacked labels and an LED; `Button` models `.btn` and paints one label. Every other interactive rule in this stylesheet already became a control |
| The tag reads `State::dirty` through the lock | No publication invented. The judgement that dropped 06-01's Task 2, and this time the plan required the number |
| The timbre name is compared with the ™ stripped | `PluginProcessor.cpp` scheduled the trademark for Phase 8 with the visual treatment it belongs to. Stripping exactly that character still catches every other divergence |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 24 | all from `/simplify`'s four agents |
| Plan corrections | 1 | `TimbreRow`'s existence — the plan assumed these rows could be `Button`s |
| Gaps | 1 | `/code-review` not run |
| Deferred | 5 | to 06-03 and 06-04, in a stated order |

### The plan assumed the timbre rows could be Buttons

Its boundary said "this plan should NOT hand-roll a fourth container hit-test — if the profile
buttons and timbre rows want one, they are `Button`s". `.timbre` turned out to be its own CSS rule
that `Button` cannot paint, so the choice went back to the user and `TimbreRow` was built. **Half of
that boundary was honoured**: the profile buttons stayed painted, so 06-03 inherits the hit-test the
plan meant to avoid — recorded as its first task.

### A greedy hex escape, shipped and caught by one compiler

`"m\xc3\xa9dio"` reads `\xa9d` as a **three-digit** escape, out of range. Clang refused it; GCC
truncated it to `\x9d` silently, turning *médio* into *mÝio*. **`verify-profiles.py` could not see it
either** — it compares source text, so both sides read the same bytes off disk. The literals are now
split after each escape and a test reads the compiled value. Three of that test's four checks pass on
the mangled string; the one that catches it asserts every non-ASCII character is a letter this text
actually uses, and the mutant reports `U+DD`.

This is the **fourth** local fix of that class — `PluginProcessor.cpp:985`, `Chassis.cpp:57` and
`Chassis.cpp:421` each split a literal the same way, the last one on the identical word. The root is
that accented text is written as escapes at all; recorded for 06-04.

### The benchmark I reported to the user measured the cheap half

AC-4 required measuring the poll, and it printed **50.2 ns**. `/simplify` showed that number covers
only the state read: `refreshFromState` ended in an unconditional `repaint()`, and in the suite the
component has no peer so the repaint cost nothing — while a full panel paint measures **472 µs and
4213 allocations**, i.e. **14.2 ms of CPU and ~126 000 allocations per second** at 30 Hz, forever, on
the one region nothing else invalidates. The same shape as 05-03's "the listener was 75 ns, the timer
was the real cost". Now gated on change, and the figure is 37.1 ns and means what it says.

### Half of the fade was unreachable

`refreshFromState` snapped the tag's opacity to 0 whenever the flag cleared, and it runs first in
`poll()` — so `advanceCustomTag`'s fade-out arm could never execute, while the comment beside the
snap cited css:411's transition, which is symmetric. The test passed on the snap. Fixed both ways.

### Also fixed by `/simplify`

`check_timbres` stopped at the first closing quote — the very thing `join_literals` was added 135
lines above it to prevent, **in the same diff**; the first split literal in a timbre label would have
made it compare half a word and stay green. `paintBundle` was the only painter with no clip guard
(22 µs and 460 allocations for a clip touching nothing) and measured `trackedWidth ("BUNDLE: ")` on
every paint for an offset that cannot change. `ChassisLayout::indexOfProfile` already existed and my
id scan was a character-for-character copy. `TimbreRow`'s box model was computed from outside the
class that paints it. `PollTimer` absorbed the eight lines `KitOverlay` and `SidePanel` spelled
identically, comment included. `surface::glowDot` serves two of the three glowing dots — each keeping
its own radius, because a shared mechanism does not imply a shared value, and the bundle dot's blur
had been borrowing the LED's constant with nothing cross-checking it.

**The two exclusion lists became one.** I added `NO_DESIGN_SOURCE` beside `UNCHECKED_BASELINE` in
this plan's first half; the script subtracted them identically, so the distinction lived only in
prose and a name added to the wrong one was accepted in silence — a taxonomy a check cannot enforce,
which is what the gate exists to replace.

**A fourth parent-relative bounds read** was still live twenty lines below the filter this plan had
just converted, in the same function.

### Deferred

1. **The hex-escape class wants a charset flag, not a fifth local fix** (06-04, first) — real UTF-8
   literals plus `/utf-8` and `-finput-charset`, which deletes `decode_c_escapes`, `join_literals`
   and the allowlist test. The allowlist is currently hard-coded to twelve characters and omits
   `í ú à õ Ç É Ó`, so the first future string carrying one is a false failure
2. **The profile buttons should become a control** (06-03, first) — before the click is wired
3. **`collectChildren` should hand back root-space bounds** (06-04) — four sites found this plan
4. **A fourth copy of the 30 Hz poll rate** (06-04)
5. **The ChassisRig's API**, now named by two plans (06-04, still last)

And a **correction**: `dirty` is not view state. At 06-01's close I recorded that `PLANNING.md`
groups `isolated`, `bateriaOpen` and `dirty`; it does not — `dirty` is at `:670` in the persisted
block, `:706` requires it to round-trip, and it is serialised in `ForroBoxState`. 06-04's `ViewState`
must be scoped to the two UI-only fields.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| Three tests broke on a coordinate space | The side panel parents its controls to itself, and three filters read parent-relative bounds against chassis-space rectangles. All four sites now go through `boundsIn` |
| The untouched-regions guard had no subject left | Its own comment anticipated this: "Phase 6 will retire the last row". Inverted rather than deleted — every region must now change on attach, which is the rule 04-01 learned |
| A mutation silently failed to apply | The negative-control assertion caught it. Two `sed`/`replace` mutants this plan reported a false "not detected" before the guard was added |

## Next Phase Readiness

**Ready:** the chassis has no empty regions. 06-03 has the panel it needs to wire the reload into, and
`applyProfile` already writes the patterns — what it must add is the parameter half.

**Concerns:** 06-03 inherits the profile buttons as painted rectangles, and its first task should be
the conversion rather than a fifth hand-rolled hit-test. `/code-review` was not run on this plan; its
subject was two APVTS bindings and a control that writes the dirty flag, and 06-03 touches all of
that again.
