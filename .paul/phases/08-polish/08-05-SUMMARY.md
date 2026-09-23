---
phase: 08-polish
plan: 05
subsystem: ui
tags: [juce, vst3, easter-egg, css-filter, steps-timing, animation, cross-check]

requires:
  - phase: 08-polish
    provides: "08-04's effect layer, KeyframeLoop, and the measured cost of a second chassis render"
provides:
  - "KeyframeTiming — steps(1) as a property of the track, which a caller cannot get wrong"
  - "EffectOverlay — one layer, one chassis re-render, however many treatments are active"
  - "src/Effects.h — both treatments' design constants in one enrolled home"
  - "PollTimer::runningCount — a census that catches an animation driven twice"
  - "ciclotronTimbreIndex — one place that knows which character carries the joke"

affects: [any plan adding a chassis-wide visual treatment or a keyframe animation]

tech-stack:
  added: []
  patterns:
    - "A timing function belongs to the track, with no default"
    - "Count the clocks: two drivers on one animation are invisible to a suite that drives it by hand"
    - "Report a wall-clock measurement; assert the structure it proxies for"

key-files:
  created: [src/Effects.h]
  modified: [src/Surface.h, src/Surface.cpp, src/EffectOverlay.h, src/EffectOverlay.cpp,
             src/Chassis.h, src/Chassis.cpp, src/TimbreRow.h, src/TimbreRow.cpp,
             src/SelectableTile.h, src/SidePanel.h, src/SidePanel.cpp, src/MixBus.h,
             src/PluginProcessor.cpp, src/HeaderBar.h, src/DragMidiButton.cpp,
             tests/UiTest.cpp, tests/VoiceTest.cpp,
             scripts/verify-geometry.py, scripts/verify-theme.py, scripts/verify-profiles.py]
  renamed: ["src/DrunkOverlay.{h,cpp} -> src/EffectOverlay.{h,cpp}"]

key-decisions:
  - "One layer for both treatments: a second overlay costs a third chassis render"
  - "steps(1) holds; it is not a fast ease, and the timing has no default"
  - "The trademark lands and the cross-check compares it verbatim"
  - "Wall-clock thresholds are reported, not asserted — three failed on a busy machine"

patterns-established:
  - "saturate then contrast compose into one integer affine map"

duration: ~4h
started: 2026-09-22T20:30:00-03:00
completed: 2026-09-23T00:30:00-03:00
description: "The Ciclotron™ treatment — the chassis degrades, the scanlines flicker, and the name gets its trademark"
type: Summary
about: "Forró Box"
---

# Phase 8 Plan 05: The Ciclotron™ treatment — Summary

**Selecting `CICLOTRON™` degrades the whole chassis: a saturate/contrast filter, scanlines
flickering on a 4 s step loop, chromatic aberration on the character's own name, and
`TOTAL DISTORTION™` blinking in `--danger`. Deselecting it restores the chassis exactly. Phase 8's
scope is complete.**

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 3 of 3 + 1 checkpoint, approved |
| Checks | 4303 → **4410** |
| Mutations | 18, each confirmed applied on disk before its result was read |
| `/simplify` | 4 angles, 27 findings — **2 real defects**, 16 more fixed, 2 measured and refused |
| Frame cost | neither **4.6 ms**, wash **12.6**, ciclotron **11.1**, both **14.4** |
| Cross-checked values | verify-geometry 216 → **235**, verify-profiles 42 → **45** |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: `steps()` holds, and the flicker lands on the right values | **Pass** | A step track never takes a value between its stops, over 200 samples; the flicker reads css:110-116's five values band by band and no other |
| AC-2: the chassis degrades, on ONE re-render | **Pass** | Unselected is pixel-identical to a layer-absent build with **zero** re-renders; chroma scales by exactly `saturate × contrast` and luminance by `contrast` alone; the scanline period, depth and row phase are the stylesheet's; both treatments together take **exactly one** re-render, and the composition equals degrade-then-scanlines-then-wash pixel for pixel |
| AC-3: the name carries its trademark and its aberration | **Pass** | `CICLOTRON™` in the table, the host's choice list and the row; a cyan fringe at −1.2 px and a `--danger` one at +1.2 px, on the selected CICLOTRON row and no other; `verify-profiles` compares all three fields verbatim |
| AC-4: the blink is told its elapsed time, and it steps | **Pass** | Reaches css:427's opacities band by band; takes no unnamed value; the phase reaches the pixels |
| AC-5: nothing costs anything when nothing is on | **Pass** | Zero re-renders, and the buffer is released — counted, not timed |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 / MSVC 2022 | **4410 / 4410**, real exit 0, 0 warnings from our sources |
| Cross-checks | all five, run explicitly |
| Install | hashes match, `/mnt/d/VST3`, run with `--install` |
| Checkpoint | approved |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| One layer, one chassis re-render | The expensive part of either treatment is the second rendering they both need. A separate overlay would cost a third chassis paint: both-on near 22 ms of a 33 ms budget, for a joke | `EffectOverlay`, and one owner for the buffer they share |
| `steps(1)` belongs to the TRACK, with no default | The stylesheet declares timing per animation. It shipped as `= easeInOut` for one revision, which is exactly how the mistake can be made — a step track that forgets the argument fades silently where the design cuts | All five tracks name their timing |
| The trademark lands, and the strip goes | `PluginProcessor.cpp:188` scheduled it for Phase 8 "alongside the visual treatment it belongs with", and `verify-profiles` said so in its own docstring | The host's choice list reads `CICLOTRON™`; state is indices, so nothing round-trips differently |
| Saturate and contrast are ONE affine map | They compose: `out = Σ(c·m)·in + 255·0.5·(1−c)`. The contrast folds into the matrix and a scalar bias | 6.63 ms → **1.28 ms** over 936k pixels, measured |
| Wall-clock thresholds are reported, not asserted | Three have now failed on a busy machine rather than on the code, across two plans. The wash's own measurement swings 12.6 → 20.6 ms with a compiler running beside it | The structural claim — re-render count — is asserted instead, and cannot be failed by a loaded CPU |

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Files outside `files_modified` | 6 | `SelectableTile.h`, `HeaderBar.h`, `DragMidiButton.cpp`, `PluginProcessor.cpp`, `tests/VoiceTest.cpp`, `scripts/verify-theme.py` |
| Deferred | see STATE.md | |

### The files the plan did not name

`PluginProcessor.cpp` and `tests/VoiceTest.cpp` — the `™` made a LATIN-1 read matter for the first
time. `SelectableTile.h` — `selectionChanged()` replaced a `refreshBlink()` the owner had to
remember. `HeaderBar.h` and `DragMidiButton.cpp` — every keyframe track had to name its timing once
the default went. `verify-theme.py` — it no longer reads the wash's onset out of a component header,
because those constants moved.

## What the work found

### The two new animations ran at double speed

`KeyframeLoop` owns a `PollTimer` and starts it in `setRunning` — that is what 08-04 built it for.
08-05 then drove the scanline flicker from a **second** poll the chassis held, and the blink from a
second poll the side panel held. Both drivers called the same `advance`, so every frame moved the
phase twice: **the 4 s flicker ran in 2 s and the 1.4 s blink in 0.7 s**.

Nothing in the suite could see it, and that is a property of how these animations are tested rather
than an oversight. Each is told its elapsed time and every check drives it by hand with no message
loop — which is what makes them reliable (04-04 lost three checks to MSVC's clock) and is exactly
why two drivers are indistinguishable.

What IS observable is how many clocks exist. `PollTimer::runningCount()` is a census, and
`testTurningOnAnEffectAddsOneClock` asserts that each effect starts exactly one — including 08-04's
sway and pulse, which start two between them.

### Two setters disagreed about one buffer

`setAmount` freed the shared render buffer on `amount <= 0` alone; `setCiclotron` guarded on both
treatments. So dragging CACHAÇA down across 65 with CICLOTRON selected threw away a buffer the
flicker was still painting into — **3.7 MB freed and re-allocated per frame, 15 MB at 2x**, on the
message thread.

No render check could fail on it: every pixel drawn is still correct. Only the allocation count
moves, which is what `scratchBuildsForTest` now counts.

### A law written and broken thirty lines apart

`KeyframeTiming`'s docstring argues that timing belongs to the track "so the mistake cannot be
made" — and all three signatures carried `= easeInOut`.

### `advanceFlicker` had no caller, under a comment saying the suite used it

The deleted outer poll had been its only one. So nothing anywhere proved the flicker's phase reached
the pixels. It does now, and the two bands it is driven into brighten and darken the chassis in the
directions the stylesheet's opacities imply.

### Two optimisations measured and REFUSED

`/simplify` benchmarked fusing the three pixel passes into one loop: **3.56 ms as three against
3.66 ms fused** — the buffer streams out of memory either way and the per-row scanline branch costs
more inside the hot body than two traversals save. Stepping the scanline pass by its period instead
of testing every row measured identical to three decimals. Both are written into the header, because
"surely one loop is faster" is the obvious next thought and it is wrong here.

A nine-table LUT for the degrade pass was measured too, at 1.97 ms against the integer matrix's
1.28 ms: exact rather than within 1/255, and slower, because the gathers defeat vectorisation.

## Notes for Future Plans

**Phase 8's scope is complete.** All five ROADMAP items have shipped.

**`Effects.h`'s unit is worth revisiting**, and `/simplify` was right to say so. It holds both
treatments' design numbers and is enrolled in `verify-geometry` — but `kSwayCommitDegrees` in it is
explicitly NOT a design value, and the wash's own gradient constants did not move, so they are still
checked by a different script that text-parses a `.cpp`. Recorded in STATE.md with the named fix.
