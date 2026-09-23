---
phase: 09-content-convolution
plan: 05
subsystem: sequencer
tags: [pattern-slots, state, persistence, ui]

requires:
  - phase: 02-sequencer-clock
    plan: 04
    provides: "the lock-free pattern handover this plan deliberately does not touch"
provides:
  - "Eight storable patterns per channel, switchable and persistent"
  - "ForroBoxAudioProcessor::selectPatternSlot — the park-and-load swap"
  - "The per-strip cycler wired, reading its channel's slot"
  - "Migration: a project saved before 09-05 loses nothing"

affects: [09-06, which loads a groove into the active slot]

tech-stack:
  added: []
  patterns:
    - "Grow the stored state without changing what the audio thread receives"
    - "A fix with no failing mutation is an unproven fix"

key-files:
  created: []
  modified: [src/ForroBoxState.h, src/ForroBoxState.cpp, src/ParameterIDs.h,
             src/PluginProcessor.h, src/PluginProcessor.cpp, src/Profiles.cpp,
             src/Chassis.h, src/Chassis.cpp,
             tests/StateRoundTripTest.cpp, tests/UiTest.cpp]

key-decisions:
  - "Active patterns stay in State::lanes, so PatternLanes is untouched"
  - "A profile load resets every slot — it is a FULL reload"
  - "The strip caches its slot rather than reading the processor in paint()"
  - "'different-length variations' is NOT built — it needs a control the spec lacks"

patterns-established:
  - "The strip follows state it did not cause, via the existing 60 Hz poll"

duration: ~2h
started: 2026-09-23T16:00:00-03:00
completed: 2026-09-23T18:00:00-03:00
description: "PAT 01–08 — eight storable patterns per channel, and the cycler that reaches them"
type: Summary
about: "Forró Box"
---

# Phase 9 Plan 05: the pattern slots — Summary

**`PAT 01`–`08` stopped being a label. Each of the five channels carries eight storable patterns,
the per-strip cycler switches between them, the grid shows and edits the active one, and the whole
thing survives save and reload — including projects written before this plan existed. The audio
thread's contract did not change at all.**

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 2 of 2 |
| Checks | 4760 → **4824** |
| Mutations | **6**, each confirmed applied on disk before its result was read |
| `/code-review` | once after Task 2 — **9 findings, all 9 fixed** |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: eight patterns per channel | **Pass** | Switch away, edit, switch back — the first slot is exactly as left, and no other channel moved. Bateria's four lanes travel together |
| AC-2: the audio thread untouched | **Pass** | `PatternLanes = decltype (State::lanes)` unchanged; `~LockedState` still publishes `lanes` only; nothing audio-side reads `parkedLanes`; the allocation counter still reports zero |
| AC-3: save and reload | **Pass** | Five channels, five slots, patterns parked and active — every one byte-identical after a round trip. **Deleting the parked serialisation now fails it** |
| AC-4: a legacy project loads | **Pass** | A hand-built state node with a `GRID` and no parked child: its pattern becomes the active slot, the other seven come back empty |
| AC-5: the cycler reads the selection | **Pass** | `PAT 03` for slot 3, clamped at both ends, driven through the buttons — and the chassis **paints differently**, which is what proves the screen is not a literal |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 / MSVC 2022 | **4824 / 4824**, real exit 0 each, read without a pipeline |
| Cross-checks | all six, run together |
| Warnings from our sources | zero |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| Active patterns stay in `State::lanes` | `PatternSnapshot.h:55` is `using PatternLanes = decltype (State::lanes)`, so the handover copies that array as one block. Keeping the active patterns there means eight slots per channel arrived without the audio thread learning slots exist | Same type, same 256 bytes, same publish, `processBlock` untouched since 02-04 |
| A profile load resets every slot | Phase 6's headline is that selecting a profile is a **full** state reload, and seven stored patterns per channel are part of that state now | Without it, cross-profile content was reachable — see below |
| The strip caches its slot | `patternSlotOf` builds a `LockedState` whose destructor takes the publisher's spin lock — the one the audio thread takes. Reading it in `paint()` made a repaint spin-wait on the audio thread five times for a scalar | And the cache is what lets the strip follow a slot it did not change |
| "different-length variations" is NOT built | `PLANNING.md:844` gives it as the feature's rationale, but `STEPS` is a GLOBAL parameter, the grid is five rows of one length, and no per-channel length control exists anywhere in the design source. Polymeter would mean inventing one | Recorded in the ROADMAP so it can be reopened as an explicit decision, not narrowed silently |

## What `/code-review` found

Nine findings — the largest set this phase — and it confirmed the swap itself was correct: park
before load, the early return, `channelForLane` moving all four bateria lanes and only those, and
one `slotLaneProperty` helper so writer and reader cannot diverge. **The invariant held on the swap
path. It was broken in substance on two others, and three of my own tests could not fail.**

### A profile load left the previous profile's patterns reachable

`applyGroove` writes the active lanes, `activeProfile` and `dirty` — and touched neither
`parkedLanes` nor the slot indices. So: load CAMPINA, step zabumba to slot 2 (parking CAMPINA's
groove), load CARUARU. The state says `caruaru, dirty = false` and the strip says `PAT 02`. Step
back, and that lane **plays CAMPINA's groove while the side panel highlights CARUARU as unedited** —
and the cross-profile content survives save and reload.

Fixed in `applyProfile`, not `applyGroove`, and the distinction is the point: selecting a PROFILE is
a full reload; selecting a groove *within* one is not, which is exactly what 09-06 will need.

### Widening to 32 never reached the parked patterns

`tileToFullWidth` looped `lanes` only, so a parked pattern's second bar kept whatever was there when
it was parked. Edit at 16 steps, park, widen, switch back — and the restored lane's upper half was
the **pre-edit** content, breaking the one law that function exists for (`PLANNING.md:606`).

### Three of my own tests could not fail

- **`testPatternSlotPersistence` switched slots BEFORE filling the lanes**, so every parked entry it
  wrote was all-zero and its assertions only read the active lanes. Deleting the entire parked
  serialisation left it green — the seven non-active patterns are the feature's whole data-loss
  risk, and they were the one thing uncovered.
- **The UI test's central claim was `check (true, ...)`** after a render, under a comment saying
  "the paint path reads the processor, which is the part a string test cannot see". It would have
  passed against the old fixed `"PAT 01"` literal. It now compares two renders and requires them to
  differ.
- **The arrows were never clicked.** The test drove `selectPatternSlot` directly, so the wiring it
  existed to cover — the null guard, the clamp, the grid refresh — had no coverage at all. A seam
  now exposes the two buttons, the shape 06-06 established with `HeaderBar::getStyleControl`.

### And three more

- `readFrom` returned before reading the parked node when `GRID` was missing, discarding all eight
  slots rather than degrading to "no active pattern, slots intact".
- `selectPatternSlot` never set `dirty`, so stepping every channel to an empty slot left the plugin
  silent while the side panel and `STYLE` both still highlighted the profile as unedited.
  `PatternPads::toggle` sets it one lane over, for the same reason.
- A doc comment was orphaned onto the new test, documenting the wrong function — the third instance
  of that slip in three plans.

### The mutation discipline caught two fixes that were unproven

Fixing the profile-load reset and the parked tiling did not make anything fail. Both mutations —
removing the reset, removing the tiling — **passed**, which meant the fixes had no coverage. Each
got a test, and each mutation now fails by name. *A fix with no failing mutation is an unproven
fix*, which is the same law as 09-02's "a mutation proves nothing unless the unmutated case is
known to pass", applied from the other end.

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Scope narrowed, recorded | 1 | The spec's "different-length variations" clause — unbuildable without inventing a control. In the ROADMAP, not silent |
| Extra work | 1 | The review's findings 1, 2 and 5 are correctness on paths the plan did not enumerate: profile load, STEPS tiling, and a missing `GRID`. All three are now tested |

## Notes for Future Plans

**09-06 loads a groove into the ACTIVE slot.** `applyGroove` is the entry point and it already does
the right thing for one channel's worth of lanes; what it must NOT do is reset the slots, which is
`applyProfile`'s job and is now where that belongs.

**The precondition 09-04's review raised is still open and is now sharper.** `applyGroove` records
`profile.id()` and clears `dirty`, and `State` still has no groove field — so applying
`campina/xote-lento` leaves a state that says "CAMPINA GRANDE, pristine". 09-05 fixed the analogous
hole for SLOTS by marking `dirty` on a switch; 09-06 must decide the same question for grooves, and
it now has a precedent to follow or to argue against.

**An MSVC stall recurred twice and was not root-caused.** The test binary ran 36 minutes at full CPU
where it normally takes 32 seconds. The hypothesis that the working directory's filesystem was to
blame was **tested and falsified** — the same binary takes 32 s with the CWD on either side. Both
occurrences were concurrent with a `/code-review` agent that also builds and runs the full suite;
run alone, MSVC completes normally. Recorded as a correlation, not a cause. `build-windows.sh:129`
already documents four earlier misdiagnoses of this same symptom.
