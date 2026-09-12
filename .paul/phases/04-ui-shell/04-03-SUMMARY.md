---
phase: 04-ui-shell
plan: 03
subsystem: ui
tags: [juce, step-pad, fader, buttons, parameter-attachment, apvts, gestures, headless-render, cross-check, asan]

requires:
  - phase: 04-ui-shell
    provides: "ChassisLayout::StripLayout, the Knob and its attachment, theme::saturated, the type scale, verify-geometry.py, the headless pixel harness"
  - phase: 01-plugin-skeleton
    provides: "the APVTS parameter tree, ids::channelParam, ids::mute/solo/ghost"
provides:
  - "StepPad — six static states, the backlit ellipse, velocity as an element opacity"
  - "Fader — absolutely positioned from the track rect; no wheel, tooltip, reset or nudge"
  - "Button — one component, four variants, one painting path"
  - "ProportionAttachment — the binding the Knob and Fader share"
  - "ToggleAttachment — a bool parameter as one complete gesture per click"
  - "type::ellipsised, and four type-scale rows forrobox.css declares and PLANNING.md omits"
  - "ChassisLayout::flexRow / textBox — the CSS flex-row law, written once"
  - "a finished channel strip: every reserved box filled but Phase 5's hit visualiser"
  - "verify-geometry.py — 79 lengths and 18 type-scale values, over forrobox.css, controls.js AND app.js"
affects: [04-04 header and footer, 05 sequencer grid, 06 side panel]

tech-stack:
  added: []
  patterns:
    - "A CSS flex row is as tall as its TALLEST child — one law, not one constant per box"
    - "An attachment holds its control through juce::Component::SafePointer, so no declaration order is load-bearing"
    - "Declare a UTF-8 literal's TYPE; juce::String and juce::StringRef disagree about const char*"
    - "Derive a readout from the parameter; a cached copy is a second representation"
    - "Chassis::paint honours getClipBounds, so a scoped repaint is actually scoped"

key-files:
  created:
    - src/StepPad.h
    - src/StepPad.cpp
    - src/Fader.h
    - src/Fader.cpp
    - src/Button.h
    - src/Button.cpp
    - src/ProportionAttachment.h
    - src/ToggleAttachment.h
    - src/ToggleAttachment.cpp
  modified:
    - src/Chassis.h
    - src/Chassis.cpp
    - src/KnobAttachment.h
    - src/KnobAttachment.cpp
    - src/Theme.h
    - src/Typography.h
    - src/Typography.cpp
    - scripts/verify-geometry.py
    - tests/UiTest.cpp
    - CMakeLists.txt

key-decisions:
  - "A ghost pad is a LIT pad plus a dot — the prototype, over PLANNING.md and this plan's own AC-1"
  - "Velocity is an ELEMENT opacity, so a transparency layer, not a per-layer alpha"
  - "color-mix with two percentages is normalised to sum 100 — theme::mixWeight"
  - "A component whose paint bleeds past its box reserves the room and is ASKED for its bounds"
  - "The fader is ABSOLUTE; ProportionAttachment is the half it shares with the knob"
  - "kPatternRowHeight 20 -> 26 and kSubDotsRowHeight 8 -> 9, both with the user's agreement"
  - "Mute, solo and LOAD have NO press scale — only .btn and .arrow-btn declare :active"
  - "verify-geometry.py reads app.js and the type scale, and resolves ns::name"

patterns-established:
  - "Ask the component for its size; never restate its geometry in the layout"
  - "Make a hazard unrepresentable rather than answering it once per site"
  - "An optimisation that changes the picture is a bug — the clipped repaint has a control"

duration: ~2h40m
started: 2026-09-11T23:43:55Z
completed: 2026-09-12T03:10:00Z
description: "The step pad, the fader, the button family and a finished channel strip — with the flex-row law that two short boxes revealed, and two lifetime hazards made unrepresentable"
type: Summary
about: "Forró Box"
---

# Phase 4 Plan 03: The Step Pad and the Strip's Own Controls — Summary

**Every box `StripLayout` reserves now carries content except Phase 5's hit visualiser. The pad's
six states are pairwise distinct in both themes, its backlit gradient is a real ellipse with the
pad's corners undistorted, and the fader jumps to the pointer rather than crawling by a delta. The
most valuable thing the plan produced was not any of those: it was discovering that a CSS flex row
is as tall as its TALLEST child and that seven boxes in the strip each restated one child's size.**

## Performance

| | |
|---|---|
| Checks | 1900 → **2100**, green on GCC, Clang and MSVC, `DISPLAY` unset |
| Suite wall clock | 2.27 s |
| Cross-checks | `verify-geometry.py` 41 → **79 lengths + 18 type-scale values**; theme and profiles unchanged |
| Negative controls | **33 behavioural + 59 constant-mutation**, all detected; 2 proved inert |
| New source | StepPad 415, Fader 277, Button 336, ProportionAttachment 97, ToggleAttachment 97 lines |
| `tests/UiTest.cpp` | 2909 → 4700 lines |
| VST3 | Built and installed, hashes matched, moduleinfo clean |

## Acceptance Criteria Results

| AC | Result |
|----|--------|
| AC-1 pad's six states distinguishable | ✅ Pairwise in both themes; closest pair 0.0314 (dark, beat vs hover — an 8/255 step) against an instrument proved to resolve 1/255. **AC amended**: a ghost is LIT plus a dot |
| AC-2 the backlit ellipse | ✅ Brightest row at 22% of height, measured by ARGMAX not centroid; horizontal falloff 1.55× the vertical; the rounded rectangle undistorted |
| AC-3 one button, three variants | ✅ **Four** variants — `.load-btn` is a fourth box model PLANNING.md's table omits. One `paint`, differences as table data |
| AC-4 MUTE, SOLO, GHOST drive parameters | ✅ One host gesture per click; lit state read from the parameter; the fader's law is the track rect, not `/160` |
| AC-5 the strip is finished | ✅ Five boxes filled, sub-dots on bateria only, the hit visualiser asserted EMPTY as Phase 5's |
| AC-6 every constant cross-checked | ✅ 38 new lengths and 18 type-scale values, each against the file that declares it, each in the same commit |
| AC-7 instruments prove themselves | ✅ `maxPixelDifference`, `brightestRow`, `tintDirection`, `litSpan` and `ellipsised` all self-tested with rejection cases |
| AC-8 nothing regressed | ✅ Three compilers, no new warnings, three cross-checks green |

## Task Commits

| | |
|---|---|
| `2d897d5` | Task 1 — the button family |
| `1ea3274` | Task 2 — the step pad |
| `9ab6f9c` | Task 3 — the fader, and `ProportionAttachment` |
| `f87aa99` | Task 4 — the strip filled |
| `ca38245` | the four gaps Task 4's controls found |
| `bf33075` | `/code-review`'s eight findings |
| `69da138` | `/simplify` |

## The finding that mattered

`ChassisLayout::kPatternRowHeight` was 20 px, derived from `.pat-screen` alone. But `.pattern-row`
is `display:flex; align-items:center` and its other two children are `.arrow-btn` at a fixed 26 px,
so the browser's row is 26 and **every box below the cycler sat 6 px high**. No check could see it:
04-02's stack assertions compare the derivation against the same constants it is built from, which
is precisely why `verify-geometry.py` exists — and the CSS declares the arrow's height, not the
row's, so the verifier could not see it either.

Fixing it revealed the real problem. `/simplify` found **seven** content-sized boxes, each
restating one child's size, and `kSubDotsRowHeight` had the identical bug one box lower.
`kMuteSoloHeight` was the dangerous one: M and S were stretched to the ROW, so if `.ms-btn`'s
padding moved, `verify-geometry.py` would update `kMuteSoloPadY`, `Button::preferredHeight` would
grow to 25, `kMuteSoloHeight` would stay 23, and the buttons would silently render squashed — and
the AC-5 containment check could not have caught it, because the child's bounds *were* the box.

Patching each box as it surfaced was the wrong altitude. The law is now written once —
`ChassisLayout::flexRow` and `textBox`, plus `Button::heightOf` — and every box reads it. Every
number is unchanged. M and S are sized by the button and centred, so containment compares two
things derived differently.

## `/code-review`: eight findings, one of them a real use-after-free

Every premise was verified before anything was changed; all eight stood.

`controls = {}` in `attachParameters` was a **heap-use-after-free** on any second call — the one
path the "Idempotent" comment exists to support. An implicitly-defined move-assignment assigns
members in DECLARATION order, unlike destruction, which runs in reverse, so each Button and Fader
was freed while its attachment still held a reference. **AddressSanitizer named it exactly**:
`heap-use-after-free` at `ToggleAttachment.cpp:51`, through `StripControls::operator=`, twenty
writes per call.

The first fix reset three attachments by hand before clearing. `/simplify` then pointed out that
this is a rule the owner has to remember and a fourth attachment would break it silently — and
that there is **no** declaration order safe on both paths. The attachments now hold their controls
through `juce::Component::SafePointer`, and the manual resets are gone: verified clean under ASan
without them.

The second real bug: a GHOST restored at **0%** painted no `NN%` readout, because
`onProportionChanged` fires only on a CHANGE and a Fader starts at 0 — and `paintGhostLabel`'s own
comment described an empty readout as the honest unwired state, which made it invisible.
`/simplify` removed the cache entirely; the readout is derived from the parameter at paint, so the
bug class is gone rather than patched.

## Negative controls: six of Task 4's nine were NOT detected, and each was a real hole

| | |
|---|---|
| `c56` sub-dots on every strip | A non-bateria `subDots` rect is `{0,0,0,0}` at the CHASSIS origin, so "empty" is a position, not an absence — the label drew over the header. Both the bare and populated renders got it, so a diff was blind; what separates them is that `paintHeader`'s gradient is VERTICAL, so every header row is uniform across its width |
| `c60` mute/solo invent a press | Nothing in the suite pressed a button. Worse, the first fix branched on `Button::specFor(variant).pressScale`, so the control moved the expectation with it — `pressesPerCss` is now read from css:142 and css:236 |
| `c58` a name not ellipsised | `type::ellipsised` had no test at all: all five sample names fit, so the strip never exercises it |
| `c59` S uses mute's colour | Nothing proved the STRIP handed S the solo `OnStyle` — two red buttons with every parameter assertion green |

**Two mutations turned out to be structurally INERT rather than undetected**, and that is recorded
rather than papered over. `juce::ParameterAttachment::callIfParameterValueChanged`
(`juce_ParameterAttachments.cpp:90-97`) drops a write-back of the value that just arrived, so a
repaint cannot become a parameter change; forcing a *different* value ping-pongs and hangs instead
of failing. And a Button that kept its own bool is indistinguishable here, because an
`AudioParameterBool` never refuses a click — the two only diverge against a host that filters,
which this suite has no way to be.

## Deviations from Plan

| Deviation | Why |
|---|---|
| **A ghost pad is LIT**, not off | `app.js:374-379` adds `ghost` on top of `on`, and `.pad.ghost` only appends the `::after` dot. `PLANNING.md:451` and this plan's AC-1 say "renders as off". Third spec/reference conflict in this phase, third resolved toward the running prototype — confirmed with the user, AC amended |
| **Two `StripLayout` boxes moved**, against the plan's own boundary | `kPatternRowHeight` 20→26 and `kSubDotsRowHeight` 8→9. Confirmed with the user before the stack moved; the second applied the same ruling rather than asking twice |
| **A fourth Button variant** | `.load-btn` (css:295) is 9 px with `padding: 4px 7px`, not `.btn`'s 10 px and `5px 9px`. Using `base` would have shipped the wrong box |
| **Four new type-scale rows** | `PLANNING.md`'s table lists 21 and omits LOAD, the pattern screen, the strip micro-label and the ghost readout — the same gap that produced `muteSoloLabel` in 04-02 |
| **Task 1's mute/solo press scale removed** | Only `.btn` and `.arrow-btn` declare `:active`. Task 1 gave mute/solo the base button's 0.96 — an invented behaviour, the same class as giving the fader a wheel because the knob has one |
| Files beyond each task's declared list | `Typography`, `Theme` and `verify-geometry.py` were touched by tasks whose file lists named only `Chassis` and the tests. The deliverable governed |

## My own errors, and what caught each

| | |
|---|---|
| `kSubDotsRowHeight` | Applied `/simplify`'s "fold `declaredTotal` into the table" literally, which made it sum the BUILT box heights — exactly what `removeFromTop`'s clamping hides, turning a live assertion into one that cannot fail. Caught on re-reading the comment above it; reverted to the declared constants, and a +40 px mutation confirms the check is live |
| A control runner | Swallowed the build output with `>/dev/null`, so a build the verifier had already failed left the OLD binary running and the "control" reported a pass. The same lesson 04-02 recorded about reading `tail`'s exit code instead of the script's |
| The ASan re-run | Reported the use-after-free as still present after the fix — a stale binary, because `verify-geometry.py` in that throwaway tree was the old copy and the build had failed silently |
| The first "nothing escapes a strip" check | Used `contrastMass` against `--raised`, which scores the header's own gradient as ink; then compared the EDITOR against a bare chassis, which differ by a `ValueTooltip`. Two chassis, and a horizontal-uniformity test |
| The mojibake | `U+2039` reached `juce::String` through its `const char*` constructor and rendered as `a<EUR>1/2` — with all 2053 checks green, because every one measured that there was ink rather than which ink |

## Decisions Made

All nine are in `.paul/STATE.md`'s Decisions table with the reasoning that settled each. In short:
the ghost pad is lit; velocity is a transparency layer; `color-mix` with two percentages is
normalised; a component that paints past its box is asked for its bounds; the fader is absolute
and `ProportionAttachment` is the shared half; `kPatternRowHeight` is the tallest child; mute, solo
and LOAD have no press scale; `verify-geometry.py` reads `app.js` and the type scale; an attachment
holds its control through a weak reference.

## Deferred Items

| Item | Why deferred |
|---|---|
| **A `PressableComponent` base for Button and StepPad** | They share four handlers (right-click passthrough, press cancelled on exit, click only on release-inside, repaint on transition) and two bools. But `Fader` deliberately declines three of them, so a third pressable is not coming for free. Revisit when Phase 5 says whether one arrives |
| **Cache the pad's glow image** | `juce::DropShadow` re-blurs on every `paintLit`: **8.92 µs of a lit pad's 14.56 µs**, and an 80-pad grid repaint measured **887 µs, of which 330 µs is DropShadow**, plus 37 heap images per frame. Phase 5 owns the grid; the numbers are here so it starts from them. The same applies to the accent bar (10.87 µs × 5) and the fader thumb (3.10 µs of 5.01 µs) |
| **Cache the 31 constant `GlyphArrangement`s** | 245 µs of a 1794 µs full repaint. The clip-aware `paint` covers the common case (a scoped repaint now costs ~15 µs instead of 291 µs), so this is only worth doing if a full repaint becomes frequent |
| **Gate the reference PNGs behind a flag** | PNG compression is **400 ms of the 2.27 s suite**, 16.8 % of its instructions. But those files ARE the human checkpoint's artefact, and a flag means CI stops producing them |
| **`type::ellipsised`'s linear walk** | 302 µs worst case for a 40-character name. Every current sample name fits, so the walk never runs; a binary search would make it ~45 µs |
| **The hit visualiser** | Phase 5's activity meter. Asserted EMPTY, so the plan that draws it owns it |

## Skill Audit

| Skill | Status |
|---|---|
| `/graphify` | ○ **deliberately skipped**, reason recorded in the plan — the existing graph already carries 16 Fader nodes over `controls.js`, and everything else this plan consumes is a literal that `verify-geometry.py` is the right instrument for |
| `/code-review` | ✅ after Task 4. Eight findings, all premises verified, one a use-after-free ASan then confirmed exactly |
| `/simplify` | ✅ at UNIFY. One rendering bug, two hazards made unrepresentable, five can't-fail assertions deleted |
| `/impeccable` | ○ optional, still not invoked in Phase 4. Worth considering at 04-04 |

## Next Phase Readiness

04-04 has what it needs: the Button family (four variants, one painting path), the Fader and its
attachment, `ProportionAttachment` for anything else that shows a proportion, `ToggleAttachment`
for the transport's bools, and `ChassisLayout::flexRow`/`textBox` for the header and footer rows —
which are flex rows with mixed children, exactly the shape that bit the strip twice.

Two things 04-04 should know. `theme::kScreenGlowRadius` and `kScreenGlowOpacity` are still read by
nothing: css:597 applies that shadow to `.bpm`, `.pscreen` and `.gk-read`, all of which 04-04
places, so 04-04 is their caller. And `ids::outputMode` remains declared and read by nothing —
04-04 decides its fate.

Phase 5 inherits `StepPad` ready for eighty instances, with the trigger flash and the playing
outline deliberately absent — both need its trigger FIFO and playhead, and no half-built accessor
was left for either.
