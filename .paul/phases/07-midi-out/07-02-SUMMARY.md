---
phase: 07-midi-out
plan: 02
subsystem: ui
tags: [juce, midi, drag-and-drop, filechooser, animation, cubic-bezier, temp-files]

requires:
  - phase: 07-01
    provides: renderStandardMidiFile and the mute-only ChannelGate contract
  - phase: 04-05
    provides: the DragMidiButton stub, and the deferral this plan closes
provides:
  - GrooveExport — the current groove as bytes AND filename, in one place
  - native drag-out, an async save dialog, and a per-instance temp folder with a sweep
  - the 2.6 s idle pulse and bobbing arrow
  - forrobox::cubicBezierEase — a general CSS easing solver in Surface.h
  - verify-geometry.py can read @keyframes
affects: [07-03-live-midi, 08-polish]

tech-stack:
  added: []
  patterns:
    - "A UI control asks upward through a std::function and knows nothing of the processor"
    - "A keyframed CSS animation is cross-checked against its @keyframes, not trusted"
    - "An input threshold added to a control must be re-checked against every test that drags it"

key-files:
  created: [src/GrooveExport.h, src/GrooveExport.cpp]
  modified: [src/DragMidiButton.h, src/DragMidiButton.cpp, src/FooterBar.cpp, src/Surface.h, src/Surface.cpp, src/KitOverlay.cpp, scripts/verify-geometry.py, tests/UiTest.cpp, tests/MidiExportTest.cpp, CMakeLists.txt]

key-decisions:
  - "Click opens FileChooser::launchAsync — never modal, which the test-only JUCE_MODAL_LOOPS_PERMITTED forbids"
  - "The temp file is swept on the NEXT export, never in the drag's completion callback"
  - "The export folder is PER INSTANCE — two plugins on the same profile and BPM collide otherwise"
  - "The profile id is sanitised by CHARACTER, not by findProfile: the two answer different questions"
  - "juce::Easings ships the same curve and was not adopted — the module is not linked and returns a std::function"

patterns-established:
  - "Adding an input threshold silently disarms every existing test that cannot express it"
  - "Gate an expensive paint on the value that will be DRAWN, not on a float compared to zero"

duration: ~240min
started: 2026-09-21T01:10:00-03:00
completed: 2026-09-21T05:05:00-03:00
description: "MIDI drag-out, an async save dialog and the 2.6 s idle pulse — and the review found the new drag threshold had silently disarmed the test that was meant to guard it"
type: Summary
about: "Forró Box"
---

# Phase 7 Plan 2: Drag-out, the save dialog and the idle pulse — Summary

**The DRAG MIDI button stops lying: dragging it hands the host a real `.mid`, clicking it opens an
async save dialog, and the call to action finally animates. The reviews found a live path-traversal,
a threshold that silently disarmed an existing test, and a glow that rasterised 33.9 µs of invisible
shadow on every frame.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~240 min |
| Tasks | 4 of 4 (3 auto + 1 checkpoint) |
| Qualify | 3 PASS |
| Checks | 3861 → **3884** (+23) |
| Compilers | GCC 13, Clang 18, MSVC 2022 — 0 warnings |
| Cross-checks | 5 green; geometry 202 → **207** lengths |
| Checkpoint | Approved — drag, dialog, mute/solo and pulse verified in Ableton Live 12 |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: the groove leaves the plugin as a file a DAW accepts | **Pass** | Verified at the checkpoint; the write path now also has automated coverage |
| AC-2: the export reads MUTE, and solo does not reach it | **Pass** | Proved by mutating the gate to `! audible` |
| AC-3: the file outlives the drag and does not accumulate | **Pass** | Per-instance folder, swept on the next export |
| AC-4: click writes the same bytes without a modal loop | **Pass** | `launchAsync`; no modal call exists anywhere in `src/` |
| AC-5: the CTA animates, and stops when it is being used | **Pass** | Phase driven to four points; five constants cross-checked against the stylesheet |

## Accomplishments

- **The last stub in the chassis is gone.** `DragMidiButton.h` and `FooterBar.cpp` both carried
  comments saying it exported nothing; both are now false and both were rewritten rather than left
  to rot.
- **The idle pulse is cross-checked, not eyeballed.** The geometry gate refused to build until all
  five constants had a source in `forrobox.css`, which needed a `@keyframes` reader.
- **Ten mutations**, each confirmed applied on disk before its result was read, and each re-proved
  after the `/simplify` refactors rather than assumed to survive them.

## Files Created/Modified

| File | Change | Purpose |
|------|--------|---------|
| `src/GrooveExport.h/.cpp` | Created | The groove's bytes + filename, gathered in one place |
| `src/DragMidiButton.h/.cpp` | Modified | Drag, dialog, temp-file policy, the pulse |
| `src/FooterBar.cpp` | Modified | Supplies `onExportRequested` |
| `src/Surface.h/.cpp` | Modified | The general cubic-bezier solver + `easeInOut` |
| `src/KitOverlay.cpp` | Modified | Its curve renamed `kitEntranceEase`, now a wrapper |
| `scripts/verify-geometry.py` | Modified | `keyframe()` reader, five expectations |
| `tests/UiTest.cpp` | Modified | Pulse checks; `mouseEventOn` can express a drag |
| `tests/MidiExportTest.cpp` | Modified | Solo, filename, sanitiser and sweep checks |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| Sanitise by CHARACTER, not `findProfile` | They answer different questions. `findProfile` asks "do I know this groove"; this asks "is this safe in a path". A profile from a NEWER build is unknown to `findProfile` but is a perfectly good filename, and mapping it to "custom" would discard the name `State::readFrom` deliberately preserves | Disagrees with the reuse review, with the reason written at the call site |
| `juce::Easings` not adopted | JUCE 8.0.12 ships `createEaseInOut()` with the identical four control points. But `juce_animation` is not linked, it returns `std::function<float(float)>` where ours is an inlinable `double`, and one curve does not justify a module | Recorded in `Surface.h` so it is not rediscovered |
| No shared `Animation` type | Five animation sites, five different laws — one-shot clamped, bidirectional, asymmetric dB, frame-based, and this wrapping one. The only shared part is "told its elapsed time", already hoisted as `PollTimer::secondsSinceLastTick` | The special case is right; `pingPong` hoists if a second looping animation lands |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| `/code-review` fixes | 7 | Two would have broken the feature outright |
| `/simplify` fixes | 15 | One was a NameError, one a disarmed test |
| Files beyond `files_modified` | 5 | `Surface.*`, `KitOverlay.cpp`, `verify-geometry.py`, `UiTest.cpp` |
| Deferred | 3 | Logged to PROJECT.md |

### The threshold disarmed the test that guarded it

`/code-review` was right that `mouseDrag` needed a distance threshold — JUCE fires it on any pointer
*state* change, pressure included, so on a trackpad an ordinary click became a drag. But adding it
made an existing check pass for the wrong reason: `mouseEventOn` passed one point as *both* position
and mouse-down position, so `getDistanceFromDragStart()` was always 0, and `testDragMidi…`'s
`+40,+20` drag returned at the new guard. **The state comparison passed because no drag ever
started.** The helper now takes an optional mouse-down position, the check drags ~44 px for real,
and a new assertion proves a `.mid` actually lands on disk — the plan's headline deliverable, which
until then was exercised only by a human with a DAW.

### A live path traversal, and a planning claim of mine that was false

I told the user at planning that the prototype's `|| "custom"` fallback was unreachable because
`activeProfile` only ever holds one of four ids. `State::readFrom` preserves **any** unrecognised
string verbatim, deliberately. So a project file carrying `../../x` produced
`forrobox_../../x_132bpm.mid`, which `File::getChildFile` resolves — writing outside the temp folder
and seeding the save dialog outside Documents. My `jassert` did not cover it: asserts compile out of
the Release build that opens other people's projects.

### The seventh instance of a bug a previous `/simplify` removed six of

`keyframe()` called `fail()`, which is defined in `verify-profiles.py` and **not** in
`verify-geometry.py`. `function_args`' own docstring records removing six of exactly these. Mine was
dormant, waiting for the first renamed keyframe stop to throw a traceback out of a CMake custom
command. It is now the file's only reader that cannot; an AST sweep confirms zero undefined names.

### Measured waste, fixed

- **The glow guard could never fire.** `breath > 0.0` was true at every phase, because the bisection
  bottoms out at 2.66e-15 rather than zero — so a 33.9 µs shadow was rasterised even on the frames
  whose 8-bit alpha rounds away. Now gated on the alpha that will actually be drawn.
- **The failure paths re-ran the whole export per pointer event.** `dragging` was the latch, but it
  stays false when the write fails and is cleared when the OS refuses the drag — so both paths redid
  ~128 µs of filesystem work on the next event, ~125×/second on a held pointer. A separate
  `gestureTried` latches once per gesture whatever the outcome.
- **The repaint dirtied 21,922 px to change 206×77.** Narrowed to what the breath can reach.

### Other review fixes

Reusing `function_args` instead of a fourth bespoke transform reader; the clamped
`secondsSinceLastTick` overload the four other animation sites use; `isPlainClickInside` on `mouseUp`
(without it the dialog fired on a press released *outside* the button); the `syncPulseTimer()` call
`mouseDown` was missing; `arrowBobOffset()` deleted (no production caller, and wrong while hovered);
`exportFolder()` deleted (an accessor existing to hold a comment); `askForExport()` collapsing a
duplicated preamble; `cubicBezierEase` no longer naming two functions in one namespace.

### Five false comments, corrected

The `cursor: grab` comment still said the control "has nothing to hand over yet". Three test comments
still said it exports nothing. And the claim that "css:534 and css:541 set `animation: none` on
both" was wrong: css:541 scopes the arrow's stop to `:hover` only, so the prototype keeps bobbing
through a drag and we deliberately do not.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| Two mutation runs reported a stale binary's result — the geometry gate halted the build first | Re-run isolated with a mutation that touches no gated constant. Reported as contaminated rather than counted |
| `Surface.h`'s new declarations landed between `PollTimer`'s doc comment and `PollTimer` | Moved; the solver had been claiming to be "a juce::Timer that calls a std::function" |

## Deferred (logged to PROJECT.md)

- **The export seam.** `DragMidiButton` is the only class in `src/` that touches the filesystem, at
  466 lines against 217 for the next-largest footer control, and `MidiExportTest.cpp` now includes a
  UI header to reach `sweepOldExports` — which is already `static` and takes its folder, i.e. a free
  function wearing a class. Moving the write and sweep into `GrooveExport` is a file move, not a
  redesign.
- **The pulse repaints two constant strings 30×/second** — 17.9 µs of an 82.3 µs frame, 0.054% of a
  core. PROJECT.md already carries the identical item for the strip's LED and meter; the fix is the
  same layer split, and this is 1/15th of that debt.
- **`verify-geometry.py` spends 0.309 s of 0.335 s in an unmemoised brace scan.** One
  `functools.lru_cache` takes it from 285 ms to 110 ms with byte-identical output. Pre-existing.

## Next Phase Readiness

**Ready:** 07-03 (live MIDI out) needs none of this — it emits the humanised performance from the
engine's own trigger path, a different data path by decision. `GrooveExport` is the seam if it ever
wants the stored grid.

**Concerns:** The drag is verified on Windows/Ableton only; macOS's modeless save panel is the
platform where the `chooser != nullptr` guard matters most and is untested.

**Blockers:** None.

---
*Built with PAUL Framework v1.4*
*Phase: 07-midi-out, Plan: 02*
*Completed: 2026-09-21*
