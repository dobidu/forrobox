---
phase: 04-ui-shell
plan: 04
subsystem: ui
tags: [juce, header, bpm, transport, host-sync, segmented, glyph-glow, apvts, headless-render, cross-check, asan]

requires:
  - phase: 04-ui-shell
    provides: "ChassisLayout::flexRow/textBox, Button's variant table, ProportionAttachment, ToggleAttachment, the Knob, the headless pixel harness"
  - phase: 02-sequencer-clock
    provides: "host sync, AudioPlayHead position handling, FakePlayHead"
  - phase: 01-plugin-skeleton
    provides: "the APVTS parameter tree, ids::bpm/sync/swing/cachaca, the internal transport"
provides:
  - "Segmented — a radio group; STYLE here, OUTPUT in 04-05"
  - "ValueScreen — the recessed readout and css:597's glyph glow"
  - "BpmField + BpmAttachment — the third interaction law, 0.5 BPM/px anchored"
  - "LogoMark — the 46x34 mark scaled once"
  - "Button gains mini and transport variants, widthOf, and a read-only state"
  - "type::boxHeight and type::trackedRun"
  - "ForroBoxAudioProcessor::getHostBpm / isHostTransportRolling"
  - "a header whose every cluster is drawn and, where a parameter exists, live"
affects: [04-05 footer, 04-06 multi-out, 05 sequencer grid, 06 side panel]

tech-stack:
  added: []
  patterns:
    - "A control the user cannot reach shows a state and REFUSES input, visibly"
    - "The timer is scheduling; the refresh is the behaviour — expose the refresh"
    - "Ask the control for its size; never restate its box model in a layout"
    - "A glyph glow is DropShadow::drawForPath, not a hand-rolled convolution"

key-files:
  created:
    - src/Segmented.h
    - src/Segmented.cpp
    - src/ValueScreen.h
    - src/ValueScreen.cpp
    - src/BpmField.h
    - src/BpmField.cpp
    - src/BpmAttachment.h
    - src/BpmAttachment.cpp
    - src/LogoMark.h
    - src/LogoMark.cpp
  modified:
    - src/Chassis.h
    - src/Chassis.cpp
    - src/Button.h
    - src/Button.cpp
    - src/Knob.h
    - src/Knob.cpp
    - src/KnobAttachment.h
    - src/KnobAttachment.cpp
    - src/PluginProcessor.h
    - src/PluginProcessor.cpp
    - src/Theme.h
    - src/Typography.h
    - src/Typography.cpp
    - scripts/verify-geometry.py
    - tests/UiTest.cpp
    - CMakeLists.txt

key-decisions:
  - "While SYNC is on, the HOST's transport is the only one that plays"
  - "A control the user cannot reach shows a state and refuses input, visibly"
  - "The BPM field's law is the knob's shape in BPM units, not the fader's"
  - "theme::kScreenGlowRadius/Opacity finally have a caller, via ValueScreen"
  - "Knob gains onProportionChanged — the boundary was holding the worse design"

patterns-established:
  - "Expose a poll's logic; never make a test wait on a wall clock"
  - "A glyph-shaped shadow is DropShadow::drawForPath — check the API before hand-rolling"
  - "One box-height law, in the type scale where the row lives"

duration: ~21h elapsed, ~5h active
started: 2026-09-12T04:36:29Z
completed: 2026-09-13T03:20:00Z
description: "The header — the logo lockup, a BPM field with its own drag law, real transport, and the two signature 54 px knobs in their recessed group"
type: Summary
about: "Forró Box"
---

# Phase 4 Plan 04: The Header — Summary

**Every cluster of the 72 px header is drawn, and where a parameter exists behind it, live. The most
valuable thing the plan produced was not any of them: it was the user turning SYNC on at the
checkpoint and finding the beat stopped — a defect four plans of host-sync work had never once
rendered.**

## Performance

| | |
|---|---|
| Checks | 2100 → **2453**, green on GCC, Clang and MSVC, `DISPLAY` unset |
| Suite wall clock | 3.07 s |
| Cross-checks | `verify-geometry.py` 79 → **106 lengths**, 18 → **34 type-scale values** |
| Negative controls | **36**, all detected |
| New source | Segmented 250, ValueScreen 197, BpmField 287, BpmAttachment 189, LogoMark 156 lines |
| `tests/UiTest.cpp` | 4700 → 6600 lines |
| VST3 | Built and installed, hashes matched, moduleinfo clean |

## Acceptance Criteria Results

| AC | Result |
|----|--------|
| AC-1 the BPM field's own law | ✅ 40 px up from 120 gives 140, anchored; out-and-back exact; wheel ±1; `isReversed` honoured; junk text rejected and the editor left open |
| AC-2 SYNC read-only, host tempo | ✅ Every gesture refused, no host gesture opened, the host's tempo displayed, the parameter untouched — **plus the defect below** |
| AC-3 transport drives the processor | ✅ `isPlaying()` follows; `getStateInformation` byte-identical across a click |
| AC-4 SWING and CACHAÇA live | ✅ One gesture per drag; readouts follow the parameter; CACHAÇA's arc is the accent, SWING's neutral |
| AC-5 the ellipse, origin above the box | ✅ Tint falls 3.2 → 1.1 top to bottom; edge 3.2 against centre 4.9, which a circle cannot be; the shape undistorted |
| AC-6 Segmented is a radio group | ✅ N−1 dividers counted in the RENDER; STYLE reflects the persisted profile for all four; clicking changes nothing |
| AC-7 every constant cross-checked | ✅ 27 lengths and 16 type values added, each against the file that declares it |
| AC-8 instruments self-tested | ✅ And every gesture driven through a real event |
| AC-9 nothing regressed | ✅ Three compilers, no new warnings, three cross-checks green |

## Task Commits

| | |
|---|---|
| `a8835a1` `dc7a95b` `7fc64e6` | Task 1 — Segmented, ValueScreen, LogoMark, two Button variants |
| `a92d32f` `27413f4` | Task 2 — logo, BPM cluster, real transport |
| `2bddfaf` | `/code-review`'s ten findings, and Task 3 |
| `a38aced` `25632a9` `0b8f119` | Task 4 — preset stub and STYLE |
| `dabcc5c` | **the checkpoint defect: the host's transport governs under SYNC** |
| `718bb76` `7ae9a7b` | `/simplify` |

## The finding that mattered

The user turned SYNC on and the beat stopped. Reproduced immediately: SYNC on, host rolling at
120 BPM, **zero steps emitted**. Two transport gates where the spec describes one —

> `PLANNING.md:838` — SYNC should *"follow host tempo and transport"*
> `02-03-SUMMARY.md:16` — *"the host's transport decides whether anything plays"*

— because the plugin's own `playing` gated ahead of `planBlock`'s host check.

**It survived four plans of host-sync work because every host-sync rig calls `setPlaying(true)` in
its constructor.** `SyncedProcessor` in Phase 2 does it, and so did my own 04-04 tests. "Host
rolling, plugin stopped" had never been rendered once. All 2428 existing checks stayed green after
the fix, so nothing else moved.

The UI half followed: the transport pair goes read-only under SYNC and Play shows the **host's**
state. A Play button that still responded would be lying about what it controls; one showing
`isPlaying()` would sit dark while the groove ran.

This breached the plan's own boundary on `processBlock`, confirmed with the user first — the same
way 04-03's two `StripLayout` boxes were.

## `/code-review`: ten findings, three real bugs

| | |
|---|---|
| **HIGH** | The published host tempo went **stale**. The store sat below `getIsPlaying()`'s early return, so a stopped host froze the field on a tempo no longer running. My first fix then made `getPosition()` run three times a block — caught immediately by an existing Phase 2 test that counts the calls |
| **HIGH** | `header = {}` was a **heap-use-after-free**, confirmed by AddressSanitizer through `HeaderControls::operator=`. `KnobAttachment` was the last attachment still holding a raw reference; 04-03 converted the other two |
| **HIGH** | `BpmField`'s editor lambdas captured raw `this`, and `~BpmField` posts one via `onFocusLost` |

Four correctness findings (a wheel notch nesting a gesture inside a drag; `mouseDrag` not
re-checking read-only; a discarded rejection; a documented arrow key that did not exist) and three
assertions that could not fail.

## `/simplify`: four more assertions that could not fail, and a comment that was wrong

**The comment.** `ValueScreen`'s glow said *"juce::DropShadow blurs a rectangle, not glyphs, so it
cannot do this one"* and hand-rolled an offscreen image plus an `ImageConvolutionKernel` on that
basis. `DropShadow::drawForPath` (`juce_DropShadowEffect.h:56`) blurs a path's shape and does
exactly this: **170 µs → 34 µs** per screen, one fewer image allocation per paint, and the rewrite
collapsed **six layout passes into two**.

**The assertions.** `testEveryHeaderBoxIsFilled` compared each box against ONE pixel of a VERTICAL
gradient, so all fourteen "carries content" checks passed whether the box was filled or not. The
read-only field's ink check was *already* a `/code-review` finding whose fix changed the reference
colour and kept `> 0.0`. `preferredWidth() >= 46` on a screen built with min-width 46. And a
near-tautology after two band checks had already proved the same thing.

**Seven laws written more than once**, including the content-box height in four places — one of
them `ChassisLayout::textBox`, whose own docstring says it exists so the law is written once.

**The disagreement.** Altitude wanted a `CallbackGuard` so four attachment destructors stop
hand-listing their callbacks; reuse argued the four clear different signatures, a registry needs
more machinery than the twelve lines it replaces, and it would defeat the compile-error-names-the-control
property `ProportionAttachment` deliberately keeps. Reuse was right.

Reuse also overturned something I had been about to do: `Fader` is **not** a third instance of the
transparent-margin law — its margin is x-only, holds the overhanging thumb rather than a glow, and
deliberately does not override `hitTest`. My comment claimed a set of three and would have sent the
next reader into a refactor for two-and-a-half.

## MSVC caught three things Linux could not

Third plan running, third time. This one was the sharpest: `testTransportButtonIsHostDrivenUnderSync`
pumped a real message loop for 60 ms and hoped the 30 Hz poll ticked inside it. GCC and Clang: yes.
MSVC: no, and three assertions fell in sequence. **A poll whose logic can only be reached through a
timer is a poll that can only be tested flakily** — `refreshHeaderFromProcessor()` is public now,
the timer calls it, and `runDispatchLoopUntil (60)` appears zero times in the suite.

It also caught the undistorted-shape probe finding the *glow's* edge rather than the ground's, on
every platform — MSVC's rasteriser simply spread it four columns further. Measured: glow 0.027,
ground step 0.110/0.224, threshold now 0.06.

## Deviations from Plan

| Deviation | Why |
|---|---|
| **The transport fix**, breaching the plan's `processBlock` boundary | The checkpoint defect. Confirmed with the user before the change |
| **`Knob` gained `onProportionChanged`**, against "consumed unchanged" | `/simplify`: the boundary was the only thing making the two global readouts different from 04-03's ghost readout — one driven by a seam, one by a clock, in one file |
| **Two new type rows and a fourth/fifth/sixth Button variant** | `.mini-btn` and `.tp-btn` are their own box models; `PLANNING.md`'s 21-row table omits both labels |
| **Phase 4 is six plans, not four** | Decided at planning with the user: the header alone is six clusters, and `ids::outputMode` was decided as *implement multi-out for real*, which is a bus-layout change and not a footer one |

## My own errors, and what caught each

| | |
|---|---|
| The accented literals | Went into the source **double-encoded** — my edit script wrote the bytes and re-encoded them on save. `FORRÃ`, `CACHAÃA`. Caught by looking at the render |
| `getPosition()` ×3 | My first fix for the stale tempo. Caught by a Phase 2 test that counts the calls |
| AC-5's instrument | Counted "equal" as a descent, so it was arithmetically incapable of reporting a rise; and its glow probe read rows **off the top of the image**, where `getPixelAt` returns transparent black. Three controls passed it |
| The read-only ink check | Fixed the reference colour, left the threshold. Still could not fail |
| Play had no `onClick` | Everything rendered, the button pressed and lit, and nothing happened. The test asked the processor, not the pixels |

## Deferred Items

| Item | Why deferred |
|---|---|
| **Split the header out of `Chassis`** | 1354 lines, ~41% header-only, and `HeaderControls`/`StripControls` are structurally identical. Two instances is a coincidence — **04-05's footer is the third, and that is the moment.** The footer must not add a fourth build/paint/refresh/resize triad inside `Chassis` before the split lands |
| **`ChassisLayout` is three jobs** | Region geometry, a v0.1 stub-content table, and a projection of `ids::profileInfos`. Phase 6 replaces every stub literal, which is the forcing function |
| **A `utf8()` helper, and `/utf-8` for MSVC** | Ten sites still spell `juce::String (juce::CharPointer_UTF8 (...))`. The real fix is the compiler flag, which removes the `\xNN`-eats-the-next-character trap entirely — a build-config change, not this plan's |
| **Cache the glow image** | `DropShadow::drawForPath` took it from 170 µs to 34; a cache would take the three static screens to 2.9 but does nothing for the dragging BPM field |
| **A shared pressable protocol for Button and StepPad** | Their four mouse handlers are character-identical. Both pre-date this plan; a third pressable is the trigger |

## Skill Audit

| Skill | Status |
|---|---|
| `/graphify` | ○ **consumed, not re-run** — and it earned its keep anyway: the query is what surfaced that `wireBPM` anchors at mouse-down, so the BPM law is the knob's shape and not the fader's |
| `/code-review` | ✅ after Task 2, for the published atomic. Ten findings, three real bugs, one ASan-confirmed |
| `/simplify` | ✅ at UNIFY. Four can't-fail assertions, a wrong comment, seven duplicated laws |
| `/impeccable` | ○ optional, still not invoked in Phase 4 |

## Next Phase Readiness

04-05 has `Segmented` ready for OUTPUT, `ValueScreen` for any readout, `Button`'s six variants with
`widthOf`/`heightOf`, `ProportionAttachment` for the MASTER fader and `ToggleAttachment` for
LIMITER. `MixBus::gainReductionDb` already exists with an atomic exchange accessor, so the GR meter
is wired for real as agreed — and `Chassis::refreshHeaderFromProcessor`'s shape is the one a GR
meter should follow.

**04-05 must not grow `Chassis` a third time without splitting it first** — see the deferred items.

`ids::outputMode` is no longer open: it becomes **04-06**, implementing multi-out for real.
