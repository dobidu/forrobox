---
phase: 08-polish
plan: 04
subsystem: ui
tags: [juce, vst3, easter-egg, blend-modes, animation, path-glyph, css-keyframes]

requires:
  - phase: 04-ui-shell
    provides: "the single scale transform on a fixed 1200x780 chassis, and cubicBezierEase"
  - phase: 08-polish
    provides: "08-02's GearButton precedent for a shape no font carries"
provides:
  - "A chassis-wide post-process layer: a second rendering, a per-pixel screen blend, and the clip discipline that keeps it affordable"
  - "KeyframeLoop + keyframeValueAt — a CSS @keyframes track and its driver, eased between adjacent stops"
  - "NoteGlyph: U+266A as a juce::Path, sized from the type row it sits beside"
  - "type::baselineIn — THE baseline drawTracked uses, asked for rather than approximated"
  - "ValueScreen::setTextColour — an override on the value, not on its glow"
  - "maxPixelDifference over a region, nearestTo, brightnessDelta — three test instruments off row pointers"
affects: [08-05 Ciclotron, which reuses the effect layer]

tech-stack:
  added: []
  patterns:
    - "A second rendering is taken at the SOURCE's own coordinates, never at an offset"
    - "Coverage counts declarations against expectations; a bare name is not a unique key"
    - "An animation holds a phase and solves its curve; it never stores the value too"

key-files:
  created: [src/DrunkOverlay.h, src/DrunkOverlay.cpp, src/NoteGlyph.h, src/NoteGlyph.cpp]
  modified: [src/Chassis.h, src/Chassis.cpp, src/HeaderBar.h, src/HeaderBar.cpp,
             src/Surface.h, src/Surface.cpp, src/ValueScreen.h, src/ValueScreen.cpp,
             src/PluginEditor.cpp, src/Typography.h, src/Typography.cpp,
             src/DragMidiButton.cpp, CMakeLists.txt, tests/UiTest.cpp,
             scripts/verify-theme.py, scripts/verify-geometry.py]

key-decisions:
  - "NOT createComponentSnapshot: it renders a region at an offset, which measurably changes the pixels — 3/255 at 1x, 88/255 at 1.1x"
  - "The chassis owns its own transform; the editor asks for a scale"
  - "NO PONTO overflows the box CACHACA was measured for, and the type scale does not move"
  - "The 16 ms stop condition answered the decision; the suite asserts a RATIO"

patterns-established:
  - "An effect layer re-renders only the dirty region, at the source's own coordinates"
  - "A sub-pixel animation frame is not committed: the invalidation costs more than the motion shows"

duration: ~5h
started: 2026-09-22T09:00:00-03:00
completed: 2026-09-22T14:30:00-03:00
description: "The CACHAÇA easter egg — a warm wash from 65, and at 88 the sway, the orange readout and ♪ NO PONTO"
type: Summary
about: "Forró Box"
---

# Phase 8 Plan 04: The CACHAÇA easter egg — Summary

**Turn `CACHAÇA` past 65 and a warm wash fades over the whole chassis. Past 88 the chassis sways
±0.18° on a 6 s cycle, the readout turns orange, and the label becomes `♪ NO PONTO` with a 1.6 s
pulse. Below 65 the plugin renders and costs exactly what it did before.**

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 3 of 3 + 1 checkpoint, approved |
| Checks | 4100 → **4303** |
| Mutations | 32, each confirmed applied on disk before its result was read |
| `/code-review` | once after Task 1, as the plan required — 6 findings, all 6 addressed |
| `/simplify` | 4 angles, 30 findings — **2 real defects**, 14 more fixed, 4 deferred |
| Frame cost | 4.5 ms inactive → **12.0 ms active (2.6x)**, measured, against 33 ms at 30 Hz |
| Sway commits | ~120 transforms per cycle → **≤ 40**, for motion that was sub-pixel either way |
| Cross-checked values | verify-theme +18, verify-geometry 207 → **216** |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: the wash fades in from 65 and is screen-blended | **Pass** | Below 65 pixel-identical to a build with the layer hidden; monotone across the ramp; **no pixel darkens** in either theme; a partial repaint matches a full one at scale 1 and at 1.5x |
| AC-2: the sway composes with the editor's scale | **Pass** | At rest `getTransform()` is **exactly** `scale(s)`; at the extreme `sqrt(\|det\|)` is still `s` at 1x/1.5x/2x and the chassis centre does not move; clicks still land on their pads while it sways |
| AC-3: `♪ NO PONTO`, drawn | **Pass** | Glyph is a `Path` with a head, a stem and a flag, normalised to its box and scaling with the type row; it stands on the baseline `drawTracked` gives the word beside it; label swaps at 88; readout takes `--c-zabumba` and reverts |
| AC-4: the pulse and the sway are told their elapsed time | **Pass** | Both reach the phase a known duration implies, at every keyframe; neither reads a clock; twelve small steps land where one period does |
| AC-5: it costs nothing when it is off | **Pass** | **Zero** chassis re-renders below 65 — counted, not timed |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 / MSVC 2022 | **4303 / 4303**, real exit 0, 0 warnings from our sources |
| Cross-checks | all five, run explicitly |
| Install | hashes match, moduleinfo clean, `/mnt/d/VST3` |
| Checkpoint | approved |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| The second rendering is NOT `createComponentSnapshot` | That method grabs a region into an image of the region's SIZE, so the component paints at an OFFSET, and that measurably changes the pixels: substituting it back in gives **3/255 at 1x, 3/255 at 1.5x, 88/255 at 1.1x** against what an unclipped repaint puts there. The wash draws its result back, so that is a visible seam around every knob, LED and meter. Two explanations of the 1x case were wrong and the header now records it as unexplained | `DrunkOverlay::renderRegion` paints into a full-size buffer at true coordinates and clips |
| An animation stores its phase and nothing else | Two copies of the same driver each cached the curve's value beside the phase, and the two resets disagreed — one set a value the curve does not take at phase 0. `KeyframeLoop::value()` solves the track every call | The divergence is unrepresentable, not merely fixed |
| A sub-pixel sway frame is not committed | Every committed transform invalidates the whole chassis and costs the wash 936,000 pixels, for 0.075 px of corner motion at 30 Hz | `kSwayCommitDegrees`, and a counter so it is a guarantee |
| The chassis owns its transform; the editor sets a scale | The sway is a second contributor to one property. Two writers is two answers taking turns: a `resized()` during the sway would drop the rotation, a sway frame would drop a scale change | `PluginEditor::resized` calls `setChassisScale`; `applyChassisTransform` is the one writer |
| `♪ NO PONTO` overflows its box | Chassis.cpp sizes the meta column from `CACHAÇA` — 43.4 px against a 46 px floor — and the note plus `NO PONTO` is **61.5 px** at the same type row. The plan's boundary is that the type scale and 04-01's header geometry stand. The prototype does the same: css:210's `.gk-name` has no width | Checked to stop short of the preset arrows rather than checked to fit |
| The suite asserts a cost RATIO, not a millisecond count | The plan's 16 ms stop condition was a decision gate at implementation time, and the measurement answered it. An absolute threshold in the suite fails on a loaded runner or a sanitizer build for reasons that are not this code's | `on < off * 4` — machine-independent, still catches a mechanism regression |
| The wash's colours are read from `theme::accentSpecs` | css writes `rgba(232,101,10,…)` and `rgba(242,194,0,…)` where it means `--c-zabumba` and `--c-pandeiro`. Reading the accents puts them under the cross-check that already compares that table to the stylesheet | `verify-theme.py` checks the identity, not a fourth copy of a hex |

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Files outside `files_modified` | 8 | `PluginEditor.cpp`, `Surface.{h,cpp}`, `ValueScreen.{h,cpp}`, `Typography.{h,cpp}`, `DragMidiButton.cpp`, and both gate scripts |
| Plan text wrong as written | 1 | The checkpoint said the VST3 is installed by the final verify step. It is not |
| Deferred | see STATE.md | |

### The files the plan did not name

`PluginEditor.cpp` — the transform has one owner now, and the editor was the other writer.
`Surface.{h,cpp}` — `keyframeValueAt` was hoisted as the SECOND caller was being written, which is
the rule that file already states twice.
`ValueScreen.{h,cpp}` — the readout's colour had to become overridable; there was no seam.
`Typography.{h,cpp}` — `baselineIn` exposes the rule `drawTracked` had inlined, because this plan
drew a glyph on a text baseline and approximated it instead.
`DragMidiButton.cpp` — found to be the third caller of the keyframe track this plan hoisted.
`scripts/verify-theme.py` and `scripts/verify-geometry.py` — **18 and 3 design numbers were being
typed into C++ with nothing comparing them**, which is the shape of the menu-id collision 08-02
shipped. The plan did not name the gates; the project's law did.

### The checkpoint's claim about installation was false

`scripts/build-windows.sh` installs only with `--install`, and the verify runs did not pass it. The
first checkpoint attempt therefore judged **08-03's binary**: no wash, no sway, `CACHAÇA` unchanged,
readout not orange — every symptom explained by the stale DLL and none of them pointing at this
code. Named here because the plan's own checkpoint text carried the claim, so the next plan would
have repeated it.

## What `/simplify` found

Four angles, thirty findings. **Two were defects rather than tidiness**, and both had a check
standing beside them that could not see them.

### The note sat 0.75 px below its own word

`HeaderBar` put the glyph on `ValueScreen::kBaselineFromCentre` — 0.35 of the row — under a comment
claiming it was *"the same baseline `drawTracked` puts a centred line of text on"*. It is not:
`drawTracked` uses `(ascent − descent) / 2` from the font's own metrics, which for this style is
0.271 of the row. In `ValueScreen` that error is common-mode across the value and its suffix and so
invisible; here one half of the run used it and the other did not. `type::baselineIn` now exposes
THE rule and both call it.

The check that catches it is not the obvious one. A threshold scan finds the word's last inked row
and loses the note's, every time, by exactly one — the word's feet are flat lines covering 58% of
that row, the note's foot is the tangent point of a tilted ellipse. That **constant one-row offset**
is the assertion; a 0.75 px drop collapses it to zero.

### The pulse painted a frame at the wrong brightness

`pulseOpacity` was a stored copy of the curve, reset to 1.0 alongside `pulsePhase = 0` — while the
curve at phase 0 is css:101's 0% keyframe, **0.55**. Every crossing of 88% painted one frame at full
brightness and snapped down. The check that was supposed to prove it *"starts from rest"* asserted
the wrong one of the two values, one line above a check asserting the right one. Two representations
of one thing, disagreeing exactly where the reset put them; the value is derived now and there is no
second representation to disagree.

### The animation driver was written twice, and had already diverged

All three of reuse, simplification and altitude landed on it independently. `keyframeValueAt` was
hoisted in this plan as its second caller was written — and its *harness* was copied instead: a
gate, a phase, a `PollTimer`, a re-base on start and a reset on stop, in two files, with `HeaderBar`
carrying the comment *"Same shape as `Chassis::swayPoll`, and for the same reason"*. That is
verbatim the hoisting rule this project states. It is now `Surface.h`'s `KeyframeLoop`, holding no
value at all — which is what makes the divergence above unrepresentable. `DragMidiButton` turned out
to be the THIRD caller of the track itself, hand-rolled since 04-06, and now goes through it too.

### The sway spent a third of a core on sub-pixel motion

A 30 Hz tick moves the angle ~0.006°, which is **0.075 device pixels** at the furthest corner — and
every committed transform invalidates the whole chassis, costing the wash a full 936,000-pixel
frame. Committing only when the corner would move a quarter of a pixel cuts ~120 transforms a cycle
to **≤ 40**, measured by a counter rather than asserted by a comment, and cannot look different
because the dropped frames are the ones that do not move a pixel.

### My own justification for `renderRegion` was wrong twice

The header blamed an identity scale transform, then gradient dithering. `juce_graphics` contains no
dithering outside its JPEG decoder. The DEFECT is real — substituting `createComponentSnapshot` back
in and re-running the partial-repaint check gives **3/255 at 1x, 3/255 at 1.5x and 88/255 at 1.1x** —
and the comment now states those numbers, names the two mechanisms ruled out, and records the 1x
case as **unexplained** rather than offering a third guess.

## What the work found

### Two defects that no eye would have caught

**`createComponentSnapshot` seams.** Found by widening the partial-repaint check from three
arbitrary rectangles to the boxes `pollVisualisers` actually dirties — the LED plus its glow, the
activity meter, the head row. The first three all missed it. The first FIX missed it too: it blamed
an identity scale transform that method also adds, which was wrong, and the check said so
immediately.

**The wash lost its z-order on the first click.** `KitOverlay::setOpen` calls `toFront (false)` and
`AboutOverlay::setOpen` calls `toFront (true)` every time they open, and all three are always-on-top
siblings — so one click on the sub-dots put a panel in front of the wash for the rest of the
session, with a washed copy of that panel drawn underneath the real unwashed one. The
construction-time z-order check passed throughout. `Chassis::childrenChanged` now re-fronts
structurally; the check opens both panels.

### A hole in `verify-geometry`'s coverage, opened and then closed

`check_enrolment_coverage` matched on the BARE name through a set difference. `Chassis::kPulseSeconds`
(1.6 s) was therefore born already counted as compared, against `dragmidi::kPulseSeconds` (2.6 s) —
enrolled, unchecked, and green. Counting declarations against expectations instead found **three
more** that had been in that position for phases: `ChassisLayout::kWidth` and `::kHeight` — the
1200x780 every layout number in this project is a fraction of — and `side::kBorder`. All three now
have expectations; `scope_block` reads inside a `struct` so the first two could be written at all.

### Three mutants survived their first battery

The readout's colour was asserted by nothing, and the fractional-scale check trimmed two pixels off
exactly the edges the rounding bug moved. A third — releasing the gradient cache with the pixel
buffer — survived until a build counter made the cache a guarantee rather than a comment. And after
`/simplify`, a fourth: the baseline fix itself, whose mutation passed the entire suite until the
one-row-offset check existed. **32 mutations, all detected.**

## Notes for Future Plans

**08-05 inherits the effect layer.** `DrunkOverlay::renderRegion` and `screenOnto` are static and
the Ciclotron treatment needs the same shape: a second rendering, a per-pixel pass, a blit back.
What it must not inherit blindly is the CLIP discipline — its scanline overlay is a function of
absolute y, so a partial region needs the same `originInWash` treatment the wash gets.

**The sway makes every committed frame a full repaint.** `setTransform` invalidates the whole
chassis, so at 88% and above every commit re-renders all 936,000 pixels rather than the dirty
rectangle. That is inherent to a rotating component; the NUMBER of commits is not, which is why
`kSwayCommitDegrees` exists. 08-05's flicker is a `steps(1)` track — it changes value four times in
four seconds — so it should cost far less than this, and a `KeyframeLoop` with step stops is the
shape for it.

**`KeyframeLoop` is the seam 08-05 should build on.** Its scanline flicker and its 1.4 s blinking
sub-label are the third and fourth animations on the same trigger; both are keyframe tracks, and
neither should write another timer.
