---
phase: 06-side-panel
plan: 03
subsystem: ui
tags: [juce, vst3, apvts, profiles, state-reload]

requires:
  - phase: 02-sequencer-clock
    provides: applyProfile and the 32-slot storage model
  - phase: 06-side-panel
    provides: 06-02's profile list and timbre rows
provides:
  - ForroBoxAudioProcessor::loadProfile — the full state reload, one law, two callers
  - ProfileButton — the profile list as a control
  - StepPad's confirmation flash
  - A test that right-clicks every component in the editor
affects: [06-04 cleanup, 07 MIDI out]

tech-stack:
  added: []
  patterns:
    - "A house rule needs a mechanism, not another hand-copied guard"
    - "A snapshot's subject must cover what the action can actually change"
    - "A threshold whose premise is data must be pinned from both sides"

key-files:
  created:
    - src/ProfileButton.h
    - src/ProfileButton.cpp
  modified:
    - src/PluginProcessor.h
    - src/PluginProcessor.cpp
    - src/SidePanel.h
    - src/SidePanel.cpp
    - src/HeaderBar.h
    - src/HeaderBar.cpp
    - src/StepPad.h
    - src/StepPad.cpp
    - src/PatternPads.h
    - src/PatternPads.cpp
    - src/SequencerGrid.h
    - src/SequencerGrid.cpp
    - src/KitOverlay.h
    - src/KitOverlay.cpp
    - src/Chassis.h
    - src/Chassis.cpp
    - src/Segmented.cpp
    - src/Surface.h
    - src/Theme.h
    - src/Theme.cpp
    - scripts/verify-geometry.py

key-decisions:
  - "The parameter half runs BEFORE the pattern publish — the mute gate is applied at schedule time"
  - "One loadProfile with two callers, asserted byte-identical from both"
  - "selectedProfileIndex returns -1 for an edited state; ProfileSelection carries both facts"
  - "Editing anything marks dirty stays deferred, settled with the user"

patterns-established:
  - "Right-click belongs to the host, enforced by a walk over every component"
  - "A group-opacity threshold is data-dependent and must be tested against the data"

duration: ~6h
completed: 2026-09-20
description: "Selecting a profile reloads the whole state — nine parameters and the pattern in one gesture, with the pads flashing"
type: Summary
about: "Forró Box"
---

# Phase 6 Plan 03: The Profile Reload — Summary

**Selecting a regional profile now reloads everything, from either entry point, without a click or a
glitch — and a right-click anywhere in the editor is guaranteed to change nothing.**

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: The profile buttons are a control | **Pass, with a measured caveat** | 34 pixels across six renders moved — corner antialiasing, because JUCE clips a child's paint to its bounds. The AC asked for byte-identical; this is the honest number |
| AC-2: Selecting a profile is ONE full reload | **Pass** | Every field asserted against `allProfiles()`, never transcribed. Both entry points produce a byte-identical state |
| AC-3: A dirty state clears both highlights | **Pass** | Needed `Segmented` to accept -1, which it had been clamping to 0 |
| AC-4: The pads flash | **Pass** | Told its elapsed time; decay asserted monotonic, because a brightness multiply clamps |
| AC-5: Nothing regressed, three compilers | **Pass** | 3776/3776 on GCC, Clang and MSVC. Three verify scripts green. VST3 installed, hashes match |

## Skill Audit

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/graphify` | ✓ | At planning — `loadProfile`'s fan-out, `updateProfileUI`'s highlight rule, `flashPads` |
| `/code-review` | ✓ | After Task 2, as the plan gated it twice. **The gap 06-02 left, and it earned the slot: eleven findings, four of them defects I had shipped** |
| `/simplify` | ✓ | During UNIFY. Four agents |
| `/impeccable` | — | Optional, not invoked |

All required skills invoked.

## Task Commits

| Task | Commit | Type |
|------|--------|------|
| Task 1: ProfileButton | `916f44e` | refactor |
| Task 2: the reload | `0091005` | feat |
| Task 3: the confirmation flash | `42d5c48` | feat |
| A shadowed parameter | `1eb23a6` | fix |
| `/code-review`'s eleven findings | `821e071` | fix |
| `/simplify` | `2e59b33` | refactor |

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| The parameter half runs before the pattern publish | The mute gate is applied at SCHEDULE time, so publishing first hands the audio thread a new groove under the old gates — loading CAMPINA mid-transport could schedule a stray bateria hit that rings out for its whole decay |
| One `loadProfile`, two callers | The side panel's list and the header's STYLE control must not load the same profile into two different states; a test asserts they do not |
| `ProfileSelection` carries index and dirty together | One sentinel for two facts made the caller take the lock twice to tell them apart |
| The flash decays linearly | `PLANNING.md:615` states the endpoints and the duration and nothing else; inventing an easing curve would be inventing spec |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 29 | 11 from `/code-review`, 18 from `/simplify` |
| Plan corrections | 1 | AC-1's "byte-identical" is 34 pixels, measured |
| Deferred | 6 | to 06-04, in a stated order |

### `/code-review` found four defects I had shipped

**A right-click on a profile button performed the full reload** — eight lanes, four globals and ten
channel gates overwritten with no undo, and it swallowed the host's automation menu.
`Button::mouseDown` had stated that rule since 04-03.

**The pattern was published before the mutes**, which is an audible glitch in the plan whose goal
says *"without a click, a glitch, or an audio-thread data race"*.

**"As a complete host gesture" was a comment stating something the code did not do** — unbracketed
writes change the value audibly and record no automation.

**The flash confirmed the outgoing groove**, because the views had not refreshed yet. It looked right
only because an earlier `StepPad::flash` armed every pad regardless — two errors cancelling, and
fixing either alone would have broken it.

**And my own audio test could not report what it claimed**: "finite and still audible" is equally
true of the profile being replaced, so it would have passed with the publication mechanism deleted.

### `/simplify` found three more instances of the bug `/code-review` had just fixed twice

`Chassis::mouseUp` (right-click the bateria sub-dots and the kit panel opened), `KitOverlay::mouseUp`
(right-click the scrim and it dismissed) and `SequencerGrid::mouseUp` (right-click a row label and it
isolated). Three hand-copied guards had not prevented holes four, five and six — so the answer is a
mechanism: **one test walks every component in a built editor, right-clicks it on an 8 px grid, and
asserts nothing moved.**

Getting that test to bite took three corrections, each found by running the mutants against it:

1. the snapshot was `getStateInformation` alone, and all three holes change things that are
   deliberately **not** persisted — the overlay's visibility and the isolated row. Three mutants
   passed against it.
2. one click at each component's centre reaches no container's hot region.
3. the chassis itself was skipped, because a top-level component is not `isVisible()` until told —
   and the chassis mutant was the one still passing.

It also held raw pointers across clicks that can destroy components; the `SequencerGrid` mutant
segfaulted rather than failing. All six mutants are caught now, including the pre-existing `Button`
guard that had never had a test.

### A threshold whose premise was data, and my first fix was also wrong

`Profiles.h` decodes `'1'`–`'9'` as level × 14, so **no profile can express a velocity above 126** —
whose opacity is 0.99465. Against a strict `< 1.0f` the pad opened a transparency layer, and an
offscreen image allocation, on **every lit pad of every profile**: +4.3 µs per pad per paint. The
comment claimed "full velocity lands on exactly 1.0, so the common case takes no layer", which was
true of a hand-typed 127 and of nothing the plugin ships.

The test I wrote for it then caught my **first** threshold too — 0.995 sits above 0.99465 and fixed
nothing — and now pins the value from both sides: the loudest profile hit must clear it, a
click-toggled pad at 100 must not.

### Also from `/simplify`

`ProfileSelection` collapses two lock takes into one; `theme::brightened` moved beside
`theme::saturated`, its CSS-filter sibling; `PollTimer::secondsSinceLastTick (maxSeconds)` absorbs a
clamp four sites had written out with the same comment; `fbtest::isFinite` replaces
`std::isfinite (bufferPeak (...))`, which cannot reliably propagate a NaN and so was the weaker
instrument for the thing it checked; the flash test chose its subjects under the outgoing profile;
two `/code-review` answer comments had lost their verbs; `onProfileLoaded` was declared inside
`attachParameters`' docstring, so the member wore the function's documentation.

**One agent left 214 lines of its own instrumentation in `tests/UiTest.cpp`** despite being told to
remove it. Reverted before anything else.

### Measured and not changed

The flash's repaints are not redundant — 24 of 25 consecutive frames render differently.
`advanceFlash` at rest is 69 ns for the whole sweep; `loadProfile` is 3.4 µs per click; the
pre-refresh inside `flashLitPads` is 2 ns when it is a no-op. No audio-thread contention: the audio
thread never takes `stateLock`, and the publisher's SpinLock is held for 19 ns.

### Deferred, in order

1. **The processor should ANNOUNCE a load** (06-04) — the highest-value item, and not currently in
   06-04's scope. Today the reload is a three-step ritual copied into two call sites plus two
   identical lambdas in `Chassis`; a load arriving from anywhere else — `setStateInformation`, a
   preset recall, a future undo — refreshes and flashes nothing. The poll infrastructure it needs
   already exists
2. **A `SelectableTile` base** for `ProfileButton` and `TimbreRow` (06-04) — the shape is now proven
   by two finished controls rather than guessed
3. **`PatternPads` should own the whole flash** (06-04) — the "refresh before you flash" law is
   stated in two headers, one with an extra `isVisible()` clause
4. **`HeaderBar::getStyleControl()`** (06-04, before the ChassisRig) — five test sites find the STYLE
   control by scanning, with two different predicates
5. **The ChassisRig's API**, now named by three plans (06-04, still last)
6. Plus everything 06-02 recorded: UTF-8 charset flags, root-space `collectChildren`, one
   `kUiPollHz`, `ViewState` for `{isolated, bateriaOpen}` only

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| I posted the checkpoint before `/code-review` returned | Withdrew it, fixed the four defects, re-presented. The user approved the fixed build |
| My error grep read a failing build as clean | The geometry gate refused two new flash constants and I ran a stale binary through three "passing" runs. The gate was right; the instrument was not |
| Two mutations silently failed to apply | The `assert b != s` guard caught both. Without it they would have read as "not detected" |

## Next Phase Readiness

**Ready:** the phase's goal statement is met — a profile loads completely, from either entry point,
without a click or a glitch, and the checkpoint confirmed it in Ableton Live 12.

**Concerns:** 06-04 now carries eleven recorded items across three plans. The announcement (item 1) is
the one that changes a design rather than tidying one, and it is not yet in 06-04's written scope.
