---
phase: 04-ui-shell
plan: 02
subsystem: ui
tags: [juce, knob, parameter-attachment, apvts, gestures, headless-render, cross-check]

requires:
  - phase: 04-ui-shell
    provides: "ChassisLayout::StripLayout, theme::saturated, the type scale, the headless pixel harness"
  - phase: 01-plugin-skeleton
    provides: "the APVTS parameter tree and ids::channelParam"
provides:
  - "Knob — one viewBox definition rendered at 28 / 32 / 54 px, both polarities"
  - "KnobAttachment — the parameter owns range, interval, default and display text"
  - "ValueTooltip — one shared tooltip for the whole editor"
  - "ChassisLayout::knobSlots and the strip's full reserved interior stack"
  - "scripts/verify-geometry.py — 35 lengths against forrobox.css AND controls.js"
  - "twenty live strip knobs bound to real parameters"
affects: [04-03 step pad and buttons, 04-04 header and footer, 05 sequencer grid, 06 side panel]

tech-stack:
  added: []
  patterns:
    - "A C++ test cannot police a constant it also consumes — cross-check it against the design source"
    - "Gestures are driven through juce::MouseEvent / KeyPress, never by calling the callback"
    - "A knob is a view: no range, interval, default or formatter of its own"

key-files:
  created:
    - src/Knob.h
    - src/Knob.cpp
    - src/KnobAttachment.h
    - src/KnobAttachment.cpp
    - src/ValueTooltip.h
    - src/ValueTooltip.cpp
    - scripts/verify-geometry.py
  modified:
    - src/Chassis.h
    - src/Chassis.cpp
    - src/Typography.h
    - src/PluginEditor.h
    - src/PluginEditor.cpp
    - tests/UiTest.cpp
    - CMakeLists.txt

key-decisions:
  - "Knob geometry is viewBox-relative, scaled once — never pixels"
  - "Alt+click resets; right-click falls through to the host (PLANNING.md:876-878)"
  - "The parameter owns range, interval, default and text; the knob owns a proportion"
  - "Shift+wheel moves one interval, not step*0.2, which is a no-op on integer parameters"
  - "verify-geometry.py reads controls.js as well as forrobox.css"

patterns-established:
  - "Cross-check design numbers against their SOURCE file, whichever file that is"
  - "Assert a relation between two independently derived things, never a constant against itself"
  - "Drive every gesture through a real event so a gesture wired to nothing fails"

duration: ~4h
started: 2026-09-11T03:20:00Z
completed: 2026-09-11T17:25:00Z
description: "The Knob — one viewBox definition at three sizes, both polarities, the full gesture set on real parameters, twenty live strip knobs, and the cross-check that finally polices the knob's identity"
type: Summary
about: "Forró Box"
---

# Phase 4 Plan 02: The Knob — Summary

**One 100×100 viewBox definition renders the knob at 28, 32 and 54 px; PITCH and PAN grow their
arcs from centre; every gesture matches `controls.js`'s law against real APVTS parameters; twenty
strip knobs are live. The most valuable thing the plan produced was discovering that the five
numbers defining the knob's shape were policed by nothing at all.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~4 h across one session |
| Tasks | 4 auto + 1 blocking checkpoint |
| Checks | 1348 → **1900**, on three compilers |
| Negative controls | **27 run, 27 detecting** — after three rounds |
| Suite wall time | 2.80 s → **2.16 s** (−23%) |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: geometry is the spec's, and RELATIVE | **Pass** | Arc radius measured at 28/32/54 px; a 54 px knob's radius is 54/32 × a 32 px knob's within 0.04, and asserted *not* to be 1.0 — the value a pixel implementation gives |
| AC-2: both polarities | **Pass** | Unipolar fills from −135°; bipolar has zero extent at centre and sweeps either way. Discriminated from the render by angular-sector ink, not by a getter |
| AC-3: the gesture set is `controls.js`'s | **Pass** | drag `(dy/160)×range`, Shift ×0.18, wheel `max(1, range/50)` intervals, arrows one interval, Alt+click reset, right-click not consumed. Each driven through a real event |
| AC-4: a knob is a view of a parameter | **Pass** | Range, interval, default and text all from `RangedAudioParameter`; repainting writes nothing back; one host gesture per drag |
| AC-5: the strip's full interior reserved | **Pass** | Twelve boxes, ordered, non-overlapping, contained. Only the knob grid and two dividers painted |
| AC-6: the instruments prove themselves | **Pass** *(after three corrections)* | `arcProfile` and `inkRadiusCentroid` proved against test-drawn arcs at known radii and sweeps, including the cases a quadrant check would pass |
| AC-7: nothing regressed, three compilers | **Pass** | 1900/1900 under GCC, Clang and MSVC with `DISPLAY` unset; zero warnings; VST3 builds and installs |

## Task Commits

| Task | Commit | Type |
|------|--------|------|
| Plan | `7728e15` | docs |
| 1 — reserve the strip stack | `1599798` | feat |
| 2 — the Knob | `6a98a6c` | feat |
| 2 — close two control gaps | `e52904c` | test |
| 3+4 — gestures, attachment, 20 knobs | `5b59940` | feat |
| `/code-review`'s eleven findings | `07d7efd` | fix |
| `/simplify` | `e6f072c` | refactor |
| the cell/knob relation | `329bea4` | test |

## What Was Built

| File | Purpose | Lines |
|------|---------|-------|
| `src/Knob.{h,cpp}` | The component: relative geometry, both polarities, the gesture set | 244 / 397 |
| `src/KnobAttachment.{h,cpp}` | Binds one knob to one parameter; owns every value decision | 64 / 135 |
| `src/ValueTooltip.{h,cpp}` | One shared tooltip, spec'd at `PLANNING.md:374` | 59 / 91 |
| `scripts/verify-geometry.py` | 35 lengths against `forrobox.css` **and** `controls.js` | 317 |
| `src/Chassis.{h,cpp}` | The full reserved strip stack, `knobSlots`, twenty placed knobs | — |

## The finding that mattered

**`verify-geometry.py` was written this plan to close "a C++ test cannot police a constant it also
consumes" — and then policed 29 CSS-declared lengths and ZERO of the five numbers
`PLANNING.md:349-357` calls the knob's identity.**

`kArcRadius` 38, `kHubRadius` 30, `kIndicatorTipY` 16 and the ±135° sweep live in `controls.js` —
a checked-in, read-only, trivially parseable file the script never opened. Every C++ assertion about
them multiplies the same constant it checks, and the indicator tests profile the angular *sector*,
so they measure direction and never length. **`kIndicatorTipY` could have been 40 — drawing a
third-length stub — with all 1891 checks green.** One wrong number there ships at 28, 32 and 54 px
simultaneously, and 04-04 and Phase 6 inherit all five unchanged.

The script now reads both files: 29 → 35 lengths, six new controls, six detecting.

## Negative controls: 27 run, 27 detecting

Three rounds, because the first two rounds found assertions that could not fail.

| Round | Controls | Found |
|---|---|---|
| Task 1 | c01–c04 | **Three did not detect.** Changing `kStripPadTop` 12→13, the divider margin and the knob column gap left 1695 checks green — the tests compare the layout against the same constants it is built from. This is what caused `verify-geometry.py` to exist |
| Task 2 | c05–c17 | **Two did not detect.** Freezing the indicator line's rotation passed everything (every line assertion measured at proportion 0.5, where the angle *is* 0°); nothing exercised the focus ring at all |
| `/simplify` | c18–c27 | **One did not detect.** Shrinking `kKnobCellHeight` to drop the label row left 1900 green — `cell.getHeight() == kKnobCellHeight` is a tautology |

All gaps closed and re-controlled.

## Deviations from Plan

| Type | Count |
|------|-------|
| Plan corrections | 4 |
| Auto-fixed (mine, found by control or review) | 7 |
| `/code-review` findings answered | 11 |
| `/simplify` findings applied | 6 could-not-fail + 12 quality |

**Plan corrections**

1. **The stack omitted `PLANNING.md:280`'s sample slot.** Everything below would have sat ~21 px too high.
2. **AC-5's "exactly tiles" was wrong.** `.strip` is `justify-content: flex-start` with no growing child (css:264-268), so the leftover is real empty space — 95 px on bateria, 112 px elsewhere. The assertions pin order, non-overlap, containment and the declared total instead.
3. **Tasks 3 and 4 were built in the other order.** AC-3 says "an attached knob", so the attachment had to exist before the gestures could be tested against anything real.
4. **Shift+wheel moves one interval, not `step * 0.2`.** In the prototype `set()` quantises to the step, so `step * 0.2` on a step of 1 rounds straight back — shift-wheel is a no-op for every integer control there.

**Deliberate spec deviations**

| Deviation | Why |
|---|---|
| Reset targets the **parameter's** default | `controls.js:35` freezes `def` at construction while `app.js:527` pushes profile values with `fire=false`, so the prototype's reset returns a knob to its page-load value |
| Right-click falls through; Alt+click resets | `PLANNING.md:876-878` overrides `:370` for a plugin. Found by `/graphify` |

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| MSVC crashed the whole suite where GCC and Clang passed | `writeReferenceRenders` declared the processor AFTER the chassis, so it died first while the chassis still held attachments deregistering from its parameters. Linux tolerated the use-after-free. I first misread the crash as my own timeout |
| The checkpoint renders showed empty strips | `writeReferenceRenders` built a bare `Chassis` with no `attachParameters` call. Each render now asserts it CONTAINS its knobs |
| One assertion was rasteriser-dependent | `== 0` on a sector adjacent to an arc's anti-aliased endpoint; only MSVC measured 1.69. Restated as a ratio outside the endpoint's sectors |

## My own measurement errors — five, all before any code was wrong

1. **Both arc instruments measured brightness.** The light theme breaks that: the orange value arc sits 0.036 from `--panel` in brightness and 0.545 in colour distance, so the instrument scored it near zero and reported a bipolar knob sweeping the wrong way. The dark theme and the white-on-black self-tests both hid it.
2. **My control runner read `tail`'s exit code**, not the script's — seven "exit=0" lines beside seven genuine detections.
3. **`KnobRig` sized its holder `preferredHeight (size, false)`**, so a labelled knob had no room for its label and the rig could not render one even when asked.
4. **A guessed threshold of 5.0** for the label's ink, where the real value is 4.7.
5. **The renders handed to the checkpoint were built from a bare chassis.**

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| Knob geometry is viewBox-relative | The same 100×100 box renders at 28/32/54 px, so 38/30/5/16 are viewBox units: at 32 px the track radius is 12.16 px |
| The parameter owns range, interval, default, text | Two copies of one law is the shape that produced 03-03's `dry = 1 − 0.5×wet` and 04-01's tracking bug |
| `styleFor` is `constexpr` with ONE table-wide `static_assert` | Strictly stronger than the per-call `jassert` it replaces, which only fired for a row someone looked up, in a debug build |
| `ChassisLayout::knobSlots` is the one knob-order table | The tests hand-copied it twice; reordering moved the knobs and both expectations together |

## Deferred Items

| Item | Effort | Why deferred |
|------|--------|--------------|
| Cache the knob's invariant layers | M | **Measured, not guessed:** track arc + hub + label = 17.8 of 24.75 µs per knob paint; a chassis repaint would go 1,192 → 892 µs. There is no repaint timer today — repaints are value-driven — so this earns itself when Phase 5's 60 fps playhead lands |
| A shared Python module for the three verify scripts | M | `verify-theme.py` still parses CSS with the lazy `\{(.*?)\}` that the other two scripts each independently rejected. Three parsers of one stylesheet |
| `arcProfile` / `inkRadiusCentroid` share one pixel walk | S | The `0.02` ink floor and the pixel-centre convention are written twice; every AC-1/AC-2 claim rests on both agreeing |
| The micro-label is confined to the knob's width, not its cell | S | `justify-items: center` on a full-width cell is the other reading of css:326 |
| `Knob::labelBounds()` is public for the test's benefit | S | Now has a real consumer (the label-ink assertion), so left public and recorded rather than hidden |

## Skill Audit

| Skill | Priority | Invoked | Notes |
|-------|----------|---------|-------|
| `/graphify` | required | ✅ at planning | Found `PLANNING.md:876-878` 500 lines from the Knob section, plus two AMBIGUOUS edges that became AC-4's findings |
| `/code-review` | required | ✅ after Task 4 | Eleven findings, all verified before fixing, two by reading JUCE's source |
| `/simplify` | required | ✅ at UNIFY | Four angles; six checks that could not fail |
| `/impeccable` | optional | ○ | Not invoked |

## Next Phase Readiness

**Ready:**
- `Knob` + `KnobAttachment` + `ValueTooltip` — 04-04's two 54 px header knobs reuse them wholesale, and Phase 6's 28 px `MIX` knob is a third size already proved
- `ChassisLayout::StripLayout` — nine further reserved boxes for 04-03 and Phase 5, all asserted
- `ChassisLayout::knobSlots` — the one knob-order table
- `verify-geometry.py` — the pattern for any later design number, now reading two source files

**Concerns:**
- **The gesture laws are deliberately NOT centralised.** `controls.js:231-243` makes the Fader absolute-positional with no `/160` drag and no wheel; `PLANNING.md:397` gives BPM its own 0.5 BPM/px. Three controls, three different laws — do not unify them in 04-03
- The knob's `0.4/0.6` saturation floor is not `theme::accentFill`'s `0.3/0.7`. Two CSS rules, two laws
- `tests/UiTest.cpp` is 2,909 lines with three render-rig idioms

**Blockers:** None.

**Not separately confirmed:** the checkpoint was approved on the renders. The in-host interaction
checks — particularly **right-click reaching Live's own parameter menu**, which cannot be verified
headlessly — were not reported back to me and are not claimed here as performed.

---
*Built with PAUL Framework · Phase: 04-ui-shell, Plan: 02*
*Completed: 2026-09-11*
