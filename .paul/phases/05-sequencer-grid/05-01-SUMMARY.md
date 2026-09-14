---
phase: 05-sequencer-grid
plan: 01
subsystem: ui
tags: [juce, vst3, sequencer, apvts, lock-free, constexpr]

requires:
  - phase: 02-sequencer-clock
    provides: the lock-free pattern handover (lockPatternState, PatternPublisher)
  - phase: 03-voices-mix-bus
    provides: VoiceEngine::channelForLane and the lane -> channel table
  - phase: 04-ui-shell
    provides: Chassis, StepPad, the five attachment classes, the headless pixel harness
provides:
  - SequencerGrid — five rows of pads that show and edit the stored pattern
  - ScopedControlCallbacks — the attachment lifetime guard, extracted from five copies
  - detail::channelToLanes / laneNamed / compositeEditLane — compile-time lane tables
  - tileAcross — the shared remainder-distribution tiling law
affects: [05-02 playhead and LEDs, 05-03 kit overlay and isolate, 06 profile loading]

tech-stack:
  added: []
  patterns:
    - "Derive from constexpr id tables, never a hand-written second copy"
    - "A dense child pool is indexed, not coordinate-tagged; store the cell only if the pool skips"
    - "Reserve the whole region's boxes in the plan that creates it, not the plan that fills it"

key-files:
  created:
    - src/SequencerGrid.h
    - src/SequencerGrid.cpp
    - src/ScopedControlCallbacks.h
  modified:
    - src/VoiceEngine.h
    - src/Chassis.h
    - src/Chassis.cpp
    - src/KnobAttachment.cpp
    - scripts/verify-geometry.py
    - tests/UiTest.cpp

key-decisions:
  - "The sequencer's 21px overflow is absorbed by the row gap; pads stay 26px (user decision)"
  - "The BATERIA row SHOWS max of four lanes but WRITES caixa alone — app.js:389"
  - "The drawn row gap is DERIVED; seq::kDeclaredRowGap is cross-checked but never drawn"
  - "KnobAttachment does not clear onProportionChanged — HeaderBar installs it, not this class"

patterns-established:
  - "A law written twice wants hoisting — tileAcross, laneNamed"
  - "Pin a design constant against its source file, not against itself"
  - "Negative controls read the script's own exit code, never a pipeline's"

duration: ~5h
completed: 2026-09-14
description: "The sequencer grid — five rows of pads that show and edit the real pattern, plus the lifetime guard extracted before a sixth copy"
type: Summary
about: "Forró Box"
---

# Phase 5 Plan 01: The Sequencer Grid — Summary

**Five rows of pads that show the stored pattern and edit the one the audio thread plays, with the
attachment lifetime guard extracted from the five classes that had hand-copied it.**

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: The lifetime guard exists once | **Pass** | `ScopedControlCallbacks`, used by all five. Chassis pixel-identical against a build of the COMMITTED tree, both themes, all three scales; the comparison was shown able to fail (a one-unit pixel change reports "1 px, first at (300,300), max delta 1") before it was believed |
| AC-2: Every box reserved, numbers are the stylesheet's | **Pass** | Head row, five rows, each label and pad strip. 145 lengths + 50 type-scale values cross-checked by `verify-geometry.py`; mutations fail naming the constant |
| AC-3: A pad shows the velocity that is stored | **Pass** | Full-range velocities, ghost threshold (via `StepPad`, `UiTest.cpp:3526`), BATERIA max-of-four. The 32-step half was **missing a test and was written at UNIFY** — see Deviations |
| AC-4: Clicking a pad edits the pattern | **Pass** | Toggles 0/100, BATERIA writes caixa, publishes through 02-04's handover, sets `dirty`. Round trip covered by `StateRoundTripTest.cpp:264-305` |
| AC-5: Real mouse events, nothing waits on a clock | **Pass** | Driven through `juce::MouseEvent`; refreshes are called. `runDispatchLoopUntil` with a timer-sized argument appears nowhere |
| AC-6: Nothing regressed, three compilers | **Pass** | 3189/3189 on GCC, Clang and MSVC with `DISPLAY` unset. Three verify scripts green. VST3 built and installed, hashes match, moduleinfo clean. Zero new warnings |

## Skill Audit

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/graphify` | ✓ | Before phase planning; the existing graph already covered all of app.js, and consuming it confirmed `togglePad() -> markCustom()` |
| `/code-review` | ✓ | After Task 3, as the plan required. Five findings; three fixed, two recorded for 05-02 |
| `/simplify` | ✓ | During UNIFY. Four agents; ten changes applied, two rejected with reasons, three deferred |
| `/impeccable` | — | Optional, not invoked |

All required skills invoked.

## Task Commits

| Task | Commit | Type | Description |
|------|--------|------|-------------|
| Task 1: Extract the lifetime guard | `879e0f2` | refactor | `ScopedControlCallbacks`, five classes, not one pixel moved |
| Task 2: Reserve the sequencer's interior | `bedc8fe` | feat | Every box reserved; the row gap derived because the declared one does not fit |
| Task 3: Pads that show and edit | `cd6d57f` | feat | The grid, reading and writing the pattern |
| `/code-review` findings | `b119fe0` | fix | A callback cleared by the wrong owner, plus two lane-derivation fixes |
| Beat-ring step test | `4a3840a` | test | Pinned WHICH steps carry the ring — the rule lived in two files, one checked |
| `/simplify` pass | `d9b0764` | refactor | Compile-time lane tables, dense pad pool, `tileAcross`, three unfailable checks |
| AC-3's missing test | *(this commit)* | test | The 32-step window, found while reconciling the plan |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| Pads stay 26px; the 21px overflow is absorbed by the row gap | PLANNING.md's 196px region and 26px pads conflict; the browser compresses the row boxes and lets pads overflow. User chose the pad height as the number that survives | `seq::kDeclaredRowGap` is cross-checked but never drawn, and that is deliberate |
| The BATERIA row shows max-of-four, writes caixa alone | `app.js:389` records it: the collapsed row edits the backbeat, deep edits are 05-03's kit overlay | One click cannot destroy a four-lane pattern |
| `KnobAttachment` stops clearing `onProportionChanged` | `HeaderBar.cpp:192` installs it and captures the parameter and screen, not the attachment — so there was nothing to dangle on, and a class must clear only its own callbacks | The seam now matches `ProportionAttachment<Fader>`, which already declined to clear the ghost readout |
| `toggleCell` keeps write-one-then-re-read-everything | `displayedVelocity` is a projection: on the BATERIA row the pad cannot know what it now displays, only the state can. `setVelocity` early-outs, so a refresh is 160 compares and one repaint | `/simplify` judged this the DEEPER shape, not the shallower one |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 6 | Essential; three were mine, found by review |
| Scope additions | 1 | AC-3's missing test, written at UNIFY |
| Deferred | 5 | Recorded in PROJECT.md |

**Total impact:** No scope creep. The additions close gaps the plan itself claimed.

### Auto-fixed

**1. A callback cleared by the wrong owner** — `/code-review`. `KnobAttachment`'s reset list carried
`onProportionChanged`, which it never installs. Pre-existing (`605b01b`'s destructor cleared it too);
what was new was a comment calling it "the five callbacks THIS class installs". Fixed with a test;
negative control reports 3091/3093 with both new checks named, build exit 0.

**2. A wrong-drum write in a degenerate row** — `/code-review`. `writeLaneForRow` returned lane 0
(ZABUMBA) for a row covering no lanes. Now `-1`, which every caller's bounds guard already rejects.

**3. 160 allocations per click** — `/code-review`. `lanesForRow` returned a heap `std::vector` per
CELL. Fixed first with a per-row memo, then properly: see below.

**4. The memo should not have existed** — `/simplify`. `lanesForRow` is the INVERSE of
`detail::laneToChannel`, a table built at compile time since 03-01. Building it the same way deletes
the memo, the bounds guard that only protected the memo, and the docstring explaining the memo. Both
`/code-review` and I had fixed the symptom.

**5. Three checks that could not fail** — `/simplify`. `check (covered > 0)` (every pad already
asserted non-empty above it); `check (gap >= 0)` (passed only today's region through a `jmax` clamp,
so the clamp was never exercised); and `kToggleOnVelocity` read by four assertions and pinned by none.
All three now either deleted or given a reachable input. Setting the constant to 7 fails EXACTLY ONE
check, which demonstrates the diagnosis rather than asserting it.

**6. A rule written twice with only one copy checked** — found while reading a checkpoint screenshot.
`SequencerGrid.cpp:223`'s `step % 4 == 0` and `app.js:353`'s `i % 4 === 0` agreed, but nothing tested
which steps carry the beat ring. `% 4 == 1` would have given four evenly spaced rings on a bar marked
off the beat. Negative control: 40 failures, 3138/3178.

### Scope addition

**AC-3's 32-step claim had no test.** The plan says "a 16-step window shows 16 pads and a 32-step
window shows 32, from the SAME stored 32 slots". `padBounds` was tested at both counts, but nothing
built a GRID at 32. Written at UNIFY rather than marking AC-3 partial. Negative control: making
`rebuildPads` ignore `ids::steps` fails 7 checks naming the claim.

### Deferred (all recorded in PROJECT.md)

- **The grid has no writer but itself** — `setStateInformation` and host automation of `ids::steps`
  both change state without reaching an open editor. 05-02
- **Nothing owns the STEPS 16/32 buttons** — reserved, unfilled, unassigned by any plan.
  `PLANNING.md:606-607` fixes the tiling law. 05-02, since it is a pattern write of Task 3's shape
- **A fresh instance claims a profile it is not playing** — `activeProfile` is "campina" with empty
  lanes. Phase 6; it is PROJECT.md's own "usable groove in under 30s" metric
- **`ScopedControlCallbacks` shares the law but the list is still hand-stated** — the reset is now a
  function pointer so a capture cannot compile, but the deeper fix registers the clear at
  installation. Rewrites five classes; too much at a plan close
- **`StepPad` rasterises a `DropShadow` per lit pad per paint** — measured at 19 µs × 57 lit pads,
  +50% on a chassis render. 04-03's code; 05-01 only changed the multiplier

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| Two of my own tests were wrong before the code was | Asserted `head` disjoint from boxes it contains; asserted a uniform 5px pad gap contradicting remainder distribution. Both corrected |
| MSVC install refused — the DLL was locked | Live had the plugin loaded for the checkpoint. The script correctly `exit 1`; I misread it as 0 because I had piped it to `tail`. Re-run with redirection |
| My checkpoint instructions contradicted themselves | Step 2 promised the campina pattern while I had also said a fresh instance is empty. The pattern is what the TEST seeds; a host instance never does. Corrected to the user |

## Next Phase Readiness

**Ready:**
- The grid exists, is editable, and publishes through the existing handover
- Every box in the 196px region is reserved and cross-checked — 05-02's playhead and LEDs and
  05-03's overlay drop into settled geometry
- `detail::channelToLanes`, `laneNamed` and `compositeEditLane` give both later plans compile-time
  lane resolution
- `tileAcross` is available for any future evenly-tiled row

**Concerns:**
- 05-02 inherits two real refresh gaps (host recall, STEPS automation) and the unowned STEPS buttons
- Phase 6's empty-grid gap is now visible to any user who loads the plugin

**Blockers:** None.

---
*Built with PAUL Framework v1.4*
*Phase: 05-sequencer-grid, Plan: 01*
*Completed: 2026-09-14*
