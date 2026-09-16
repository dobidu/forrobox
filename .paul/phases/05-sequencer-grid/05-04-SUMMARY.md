---
phase: 05-sequencer-grid
plan: 04
subsystem: ui
tags: [juce, vst3, overlay, animation, isolate, dimming]

requires:
  - phase: 03-voices
    provides: the four bateria kit lanes and the mute/solo resolution
  - phase: 05-sequencer-grid
    provides: 05-01's grid and StepPad; 05-02's 60 Hz poll; 05-03's step-window reader
provides:
  - The BATERIA kit overlay — BB / CX / HH / TOM, individually editable at last
  - The entrance: a 200 ms cubic-bezier slide and fade, told its elapsed time
  - Row dimming from the resolved mute/solo gate
  - The row-label isolate, visual only and proved so sample for sample
  - readStepWindow / snapshotPattern — two laws two components both needed
  - verify-geometry.py's enrolment coverage gate
affects: [06 side panel, 06 CUSTOM tag, 06 cleanup plan]

tech-stack:
  added: []
  patterns:
    - "A container property belongs to a container component, not to every leaf"
    - "A component that animates owns its own tick"
    - "Establish a test's limits by running mutants, not by assuming them"

key-files:
  created:
    - src/KitOverlay.h
    - src/KitOverlay.cpp
  modified:
    - src/Chassis.h
    - src/Chassis.cpp
    - src/SequencerGrid.h
    - src/SequencerGrid.cpp
    - src/StepPad.h
    - src/StepPad.cpp
    - src/Typography.h
    - scripts/verify-geometry.py
    - CMakeLists.txt

key-decisions:
  - "The scrim covers the whole chassis — app.js:37 over PLANNING.md's prose"
  - "No backdrop blur; the --bg 78% scrim does the separation alone"
  - "The scrim does NOT fade with the panel — css:557 declares no transition"
  - "The isolate lives in the grid, not in State and not in a parameter"
  - "A row dims if EITHER the gate or the isolate says so"

patterns-established:
  - "An enrolled header's constants must all be compared, or excused by name"
  - "A measurement over transparency reports no difference — render on a ground"

duration: ~6h
completed: 2026-09-16
description: "The bateria kit overlay, its entrance, row dimming and the audio-neutral isolate — the last plan in Phase 5"
type: Summary
about: "Forró Box"
---

# Phase 5 Plan 04: The Kit Overlay, Dimming and the Isolate — Summary

**The four bateria lanes are reachable, the rows say what is audible and what the user is looking
at, and the isolate does not touch one sample.**

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: The overlay is the prototype's, and covers the whole chassis | **Pass** | `app.js:37` settles the spec conflict over `PLANNING.md:519`'s narrower prose. No blur, by decision, recorded where the scrim is painted |
| AC-2: Four sub-rows that edit four lanes, and the main row follows | **Pass** | Row order tied to `lanesForRow` by a `static_assert`, so a reordered lane table cannot mislabel them |
| AC-3: The panel slides in, told its elapsed time | **Pass** | Rendered x and alpha measured at 0, 0.5 and 1; the halfway position is checked against the SPEC's easing, not the implementation's |
| AC-4: Rows reflect what is audible and what the user is focused on | **Pass** | Through `resolveChannelSettings()`, the same resolver the engine renders with. Isolate proved audio-neutral sample for sample |
| AC-5: Nothing regressed on three compilers | **Pass** | 3451/3451 on GCC, Clang and MSVC with `DISPLAY` unset. Three verify scripts green. VST3 installed, hashes match, moduleinfo clean |

## Skill Audit

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/graphify` | ✓ | At planning; established the overlay is a direct child of `#fb-window`, which settled AC-1's spec conflict |
| `/code-review` | ✓ | After Task 1, as the plan gated. Nine findings; two deferred into Task 2 and closed there |
| `/simplify` | ✓ | During UNIFY. Four agents; the efficiency pass measured rather than reasoned, and corrected a mechanism I had chosen |
| `/impeccable` | — | Optional, not invoked |

All required skills invoked.

## Task Commits

| Task | Commit | Type |
|------|--------|------|
| Task 1: the overlay, four lanes | `3784ddc` | feat |
| `/code-review`'s findings | `b639808` | fix |
| Task 2: entrance driver, dimming, isolate | `854eafc` | feat |
| The entrance's children and the scrim | `387add7` | fix |
| Checkpoint renders and a double-encoded ç | `dd4786f` | fix |
| At the checkpoint | `b05d43c` | docs |
| `/simplify` | `d979bd7` | refactor |

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| The scrim covers the whole chassis | `app.js:37` appends the subview to `#fb-window` and `css:554` is `inset: 0`. The design source wins over `PLANNING.md:519`'s prose — the ruling 04-03 made for `.pad.beat` |
| No backdrop blur | `css:556`'s `blur(3px)` has no JUCE equivalent short of capturing and blurring the region behind. Decided with the user; the `--bg` 78% scrim in the same rule does the separation |
| The scrim does not fade with the panel | `css:557` switches `.subview` from `display:none` to `flex` and declares no transition on it. Only `.subview-panel` slides and fades. Task 1 had eased both |
| The isolate stays out of `State` and out of the parameters | `PLANNING.md:592` calls it a focus aid that does not affect audio. Keeping it in the grid means there is no path by which it could, and a reopened project does not restore which row someone was squinting at |
| A row dims if EITHER says so | `app.js:518`. An isolated row that is muted is still silent, so it is still dim |
| The panel is a child component, not a rectangle the overlay draws | `/simplify`. Two container properties open-coded at seventeen leaves, one of which was already forgotten once inside this plan |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 22 | 9 from `/code-review`, 13 from `/simplify` |
| Plan corrections | 2 | The entrance's driver location, and the dim's mechanism |
| Deferred | 6 | All to one Phase 6 cleanup plan, in a stated order |

### The plan named the wrong driver, and measuring named the wrong mechanism

**The plan said to drive the entrance from "the grid's existing 60 Hz poll".** The overlay is a
chassis child, not a grid child, so it went on the chassis's visualiser poll instead — and `/simplify`
then showed that was still the wrong owner. Four other components own a `PollTimer`; this was the one
whose tick lived in its parent, which cost `Chassis` a member meaning "when another component's
animation last ticked" and a call placed deliberately above `pollVisualisers`'s own early return.
The overlay now owns it, started on open and stopped on close.

**The dim shipped as `Component::setAlpha`, and that was measurably wrong.** JUCE takes a different
branch in `paintEntireComponent` when a component's alpha is below 1 and allocates an offscreen image
**per paint** — +2.1 µs and +16 heap allocations per dimmed pad per frame, about 3 800 allocations a
second on the message thread for as long as a channel stayed muted, and two stacked layers on a
dimmed lit pad. It now multiplies into the group opacity `StepPad::paint` already computes for
velocity, which is the same kind of thing `.seq-row.dimmed { opacity: 0.32 }` is. My comment defending
the choice argued correctly against `g.setOpacity` and then picked a third thing that was worse than
both.

### Auto-fixed — `/code-review` (9)

**The overlay was buried under some fifty strip controls.** `addChildComponent` appends to the FRONT
of the child list, so every knob, button and fader added after it painted over the scrim, strips 4
and 5 painted inside the panel, and all of them still took the mouse — a modal overlay you could drag
a knob through, under two comments claiming it was added last. Fixed with `setAlwaysOnTop(true)`,
which is the z-order as a property of the component rather than a rule the owner has to remember; a
test counts what is in front of it, 50 → 0.

Also: six constants declared with `css:` citations and policed by nothing, `kPadHeight` among them —
enrolling a header only lets the reader FIND a name; `cubicBezierEase`'s docstring claimed a test
against the control points that did not exist; the kit rows were bound positionally where the plan's
own instruction said to resolve by name; and an `apvts` member that was stored and never read.

### Auto-fixed — `/simplify` (13)

**`verify-geometry.py`'s three new readers called a `fail()` that does not exist in that file.** It
is defined in `verify-profiles.py`. Six live `NameError` paths, each of which would have thrown a
traceback out of a CMake custom command on the first day the stylesheet moved — which `indexed`'s own
docstring forbids in as many words. Replaced by one `function_args` reader on the file's `MISSING`
convention, which also absorbed `translate_px`, `rgba_alpha` and `bezier_points` into one.

**The enrolment gate now exists.** A constant in an enrolled header that no expectation compares is
a check that cannot fail, and that has now happened three times — `kTrailGap` shipped as 0 under a
comment saying 3 px, `kPadHeight` could have been 21, and the four easing points were invisible to
the reader because they were `double`. `check_enrolment_coverage` fails on any new one. Its baseline
of 94 names is labelled as a baseline and explicitly not as an audit.

Also: `readStepWindow` and `snapshotPattern` hoisted to free functions — the second is the
lock-ordering invariant 05-03 shipped wrong twice and 05-04 wrote out a third time; `open` deleted as
a second spelling of `isVisible()`; four null guards on a member documented "never null"; `mouseExit`
was `mouseMove` with the row pinned to −1; `kPanelShadowOffsetX` was the one shadow number left as a
literal while its radius and alpha were both enrolled.

**Two measured costs fixed.** `paintPanel` now culls its eleven tracked-text runs against the clip —
142 µs and 2 268 allocations per playhead-sized repaint while the panel was open, against 18 µs with
it shut — and the entrance repaints the panel's box rather than the whole 1200×780 chassis, which was
6.2 ms and 22 k allocations a frame against a 16.6 ms budget. The suite went 3.73 s → 2.93 s.

**Three suspicions measured as free, recorded so nobody re-litigates them.**
`resolveChannelSettings()` is 11.7 ns and `refreshRowStates` 13 ns, so the 60 Hz resolve is the right
altitude and an APVTS listener would save nothing while duplicating solo's precedence in the UI.
`Component::setAlpha` early-outs on an unchanged value. The grid's `rowDimmed` cache earns its place
— but for reasons its comment got wrong: the `const` label painter and the rebuild, plus the five
`repaint (label)` calls that have no early-out, not the 160 `setDimmed` calls that do.

### Deferred — one Phase 6 cleanup plan, in this order

1. **`PatternPads`.** The overlay and the grid each own a pad vector, a `stepCount`, a
   `lastPatternGeneration`, a `rebuildPads`, a `padFor`, a `toggleCell` and a `refreshIfStateChanged`,
   differing in a row table. The tax is already paid twice: both of Task 2's fixes were re-fixes of
   bugs the grid had already had
2. **Publish the resolved channel gate with a counter**, the way the step snapshot is published, so
   the one derived value the UI follows by unconditional recomputation follows a signal like the rest
3. **A `HitZone` component**, so a container stops hit-testing layout rectangles by hand — three
   instances arrived in this plan and Phase 6's side panel is the fourth
4. **`ViewState`.** `PLANNING.md:676-677` already groups `isolated`, `bateriaOpen` and `dirty` in one
   table; today the first two live in two components and the third arrives in Phase 6 with no home
5. **`ids::lanes` as one array of structs**, so `kitLaneIds` and `kitPieceName`'s parallel table stop
   being two lists bound positionally — the rule `ids::channelInfos` states in its own comment
6. **The ChassisRig**, LAST, because what it should expose is downstream of 1 and 3. Now 30 sites

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| The sub-line had been rendering `peÃ§a` since Task 1 | `subLineText` carried `C3 83 C2 A7` where `app.js:466` has `C3 A7` — under a comment saying "verbatim", and a test pinned the mojibake as "the accent intact". Found by looking at the reference render. The test now also rejects any U+00C3 in the decoded string |
| The audio-neutrality check was measuring the rig, not the isolate | Two renders with nothing changed diverged by 0.72: a transport stop deliberately does not cut sounding voices, so the previous render's tail was still ringing. `prepareToPlay` between renders is the only hard clear. The control is now an assertion |
| An ink measurement over a transparent image cannot fail | `Image::getPixelAt` returns an un-premultiplied colour, so a pad painted at 0.32 alpha reports the same brightness as one at 1.0. Measured through the grid's own opaque ground instead |
| The panel clip test passed on two blank regions | The test builds a bare chassis, and `addChildComponent (*kitOverlay)` happens in `attachParameters` — so both renders painted no overlay at all. Three mutants went undetected before the render targeted the overlay directly |
| A tight clip guard over-culled by 1 370 pixels | `drawTracked` centres glyphs in the box it is given and their ink overflows it. The guards expand by a line height; the test's limits were then established by running mutants and are recorded in it |

## Next Phase Readiness

**Ready:** every Phase 5 deliverable is in. The grid shows and edits the pattern, follows every
writer, sweeps a continuous playhead, lights per-channel LEDs and meters, tiles on a STEPS change,
reaches the four kit lanes, and dims for mute, solo and isolate.

**Concerns:** the six deferred items above are one plan's worth of work and three of them are named
in Phase 6's own scope anyway. The `CUSTOM` tag moved to Phase 6 with the side panel by decision at
this plan's planning, and a fresh instance still claims a profile it is not playing.
