---
phase: 06-side-panel
plan: 01
subsystem: ui
tags: [juce, vst3, refactor, pattern, z-order]

requires:
  - phase: 05-sequencer-grid
    provides: the grid, the kit overlay, and the two copies this plan merged
provides:
  - PatternPads — one rectangle of StepPads for every view of the pattern
  - PatternRow — which lanes a row reads, which lane a click writes, its colour
  - A rebuild that is z-order neutral in its host
  - The shared pattern laws in a header below both views
affects: [06-02 side panel, 06-03 profile reload, 06-04 cleanup]

tech-stack:
  added: []
  patterns:
    - "A hoisted component must not depend on what it was hoisted out of"
    - "A rebuild that re-parents children must not re-order its host"
    - "State a test's limits by running mutants, not by assuming them"

key-files:
  created:
    - src/PatternPads.h
    - src/PatternPads.cpp
  modified:
    - src/SequencerGrid.h
    - src/SequencerGrid.cpp
    - src/KitOverlay.h
    - src/KitOverlay.cpp
    - CMakeLists.txt
    - tests/UiTest.cpp

key-decisions:
  - "Composition, not a base class — the two views share the rectangle and nothing else"
  - "Task 2's channel-gate publication judged and REJECTED with the user, not built"
  - "A rebuild sends its pads to the back rather than siblings declaring always-on-top"
  - "The ChassisRig stays last in Phase 6, now with one of its two inputs resolved"

patterns-established:
  - "A refactor's proof is byte-identical renders plus unchanged tests, not a new test"
  - "A guard that cannot be false, beside a dereference that is not guarded, reads as the missing check"

duration: ~4h
completed: 2026-09-17
description: "PatternPads — the grid and the kit overlay stop being two copies of one rectangle, before the side panel makes a third"
type: Summary
about: "Forró Box"
---

# Phase 6 Plan 01: PatternPads — Summary

**The sequencer grid and the bateria kit overlay stop being two copies of one rectangle of pads, and
the plan's second task was judged and dropped rather than built.**

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: One holder for a component showing a slice of the pattern | **Pass** | The row table is the whole difference. The two views lost 296 lines and gained 113 |
| AC-2: The resolved channel gate is published and counted | **Dropped** | Judged and rejected with the user — see below. Recorded in PROJECT.md, marked do-not-re-raise |
| AC-3: Nothing changed, and that is proved | **Pass** | All 20 reference PNGs byte-identical against a baseline rebuilt from the pre-plan commit |
| AC-4: Nothing regressed, on three compilers | **Pass** | 3474/3474 on GCC, Clang and MSVC with `DISPLAY` unset. Three verify scripts green. VST3 installed, hashes match, moduleinfo clean |

## Skill Audit

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/graphify` | ✓ | At Phase 6 planning. Gave `loadProfile`'s fan-out, which 06-03 needs |
| `/code-review` | ✓ | Gated on Task 2, which was dropped — run anyway, because Task 1 moved the code that writes the pattern the audio thread reads. Seven findings |
| `/simplify` | ✓ | During UNIFY. Four agents; the efficiency pass found nothing to act on and said so with numbers |
| `/impeccable` | — | Optional; no new visual surface |

All required skills invoked.

## Task Commits

| Task | Commit | Type |
|------|--------|------|
| Task 1: PatternPads | `447a997` | refactor |
| Task 2 dropped, Task 3 proved | `7b9550f` | docs |
| `/code-review`'s seven findings | `ad749cd` | fix |
| `/simplify` | `58c62d6` | refactor |

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| Task 2 rejected rather than built | The UI needs five bits, not the 150-byte `Settings`; there is no duplicated LAW, only a duplicated CALL to one pure function; and publishing would make UI dimming depend on `processBlock` having run — wrong in a suspended plugin and in every headless test that mutes without rendering |
| Composition, not a base class | A grid and a modal overlay share the rectangle and nothing else — not painting, layout, lifetime, or the parent their pads belong to |
| A rebuild sends its pads to the back | The root of the z-order bug, rather than each sibling declaring `setAlwaysOnTop` |
| The shared laws move below both views | `PatternPads` was including the grid it had been hoisted out of |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 20 | 7 from `/code-review`, 13 from `/simplify` |
| Plan corrections | 1 | Task 2 dropped, with the user |
| Deferred | 5 | To 06-02, 06-03 and 06-04, in a stated order |

### The plan's own Task 2 did not survive reading the code

Task 2 said to publish the resolved mute/solo gate with a counter, as the step snapshot is published,
and its own text said to read `StepSnapshot.h` first. Doing so killed the premise three ways, and the
user chose to drop it. **Both UI readers touch one field** — `channels[i].audible` — so the payload is
five bits and the anticipated double-buffer was never needed. **There is no duplicated law**, only a
duplicated call to one pure function, which is exactly what separates it from `PatternPads`, where the
rule itself was copied and both copies grew the same two bugs. And **publishing would add a coupling
the direct read does not have**: `resolveChannelSettings` is a pure function of relaxed atomic loads,
correct from any thread at any time.

Recorded in PROJECT.md against the deferred item and marked *do not re-raise in 06-04*.

### Auto-fixed — `/code-review` (7)

**Every STEPS rebuild buried the playhead.** `attachParameters` made the sweep line front-most once
with `toBehind (nullptr)`; `rebuild` destroys every pad and adds the replacements, appending them
after it. A STEPS change — by click or by host automation — left the line painted under every pad it
crossed for the rest of the session. Pre-existing, and the mutant showed it was worse than reported:
**162 pads in front**. Fixed first with `setAlwaysOnTop`, then properly at `/simplify` (below).

**My new test passed verbatim against the pre-refactor tree.** Every symbol existed at the base commit
and every assertion held there, so it proved the two old `toggleCell` bodies agreed — not that there
is now one. Renamed to what it is, a contract guard, with that fact written into it: nothing
observable at runtime can prove a correct refactor, which is the point of one.

Also: a dead `readStepCount` whose doc still claimed it was the one chokepoint for the step window;
`setProcessor`'s doc saying "every method is a no-op until then" when `rebuild` and `setRows` are not;
three `padGrid` null guards that could not fire beside two unguarded dereferences; and two runtime
checks a `static_assert` already stops the build over.

### Auto-fixed — `/simplify` (13)

**The dependency arrow pointed backwards.** `PatternPads.cpp` included `SequencerGrid.h` — the thing it
had been hoisted out of — to get its own vocabulary back, dragging `SequencerLayout`, `Chassis.h`,
`Button.h`, `ChoiceButtonsAttachment.h`, `Typography.h` and `Surface.h` in for six declarations.

**One callback, fired from every rebuild path.** There were two: `onPadCreated`, firing 160 times a
rebuild to seed the dimming and left null by the overlay, and `onRebuilt`, firing from one of three
rebuild paths — so both owners hand-wrote `resized()` after the other two.

**`setAlwaysOnTop` was a symptom fix.** The root is that `rebuild` appends pads to the HOST's child
list and so silently re-orders a component it does not own. It now sends them to the back, and the
playhead needs no z-order call at all — nor does any sibling a later plan adds.

Also: `laneOf` re-implemented the `compositeEditLane()` its own comment cited; `velocityOf` and the
z-order counter were each written twice and are now file-scope helpers, the counter **typed**, because
"how many children are in front" was the wrong question — the STEPS buttons are, always were, and
overlap nothing; the overlay lost a wrapper whose guard could not be false, a write-only member and a
`unique_ptr` that existed only because `panel` was built in the constructor body.

**Clang caught a hazard GCC did not** — `-Wreorder-ctor`: `padGrid` holds a reference to `panel`, so
the init list must match declaration order.

### The efficiency pass found nothing, with numbers

A refresh is 191 ns and does **not** run at 60 Hz — only when the generation moves. An idle follow tick
is 2.24 ns. A full rebuild is 83 µs, once per STEPS change. Both `std::function`s capture `this` alone
and hit the small-buffer optimisation — and the pad's `onClick` capture is **exactly** the 16-byte
limit, so one more captured word would put 160 heap allocations into every rebuild. That is now a
comment on the capture list.

### Deferred, in order

1. **`State`'s revision counter covers only `lanes`** (06-02/06-03). `publishIfChanged` compares the
   lanes; `dirty` and `activeProfile` sit behind the same lock with no publication, so 06-02's `CUSTOM`
   tag has no follower and a profile load whose lanes happen to match bumps nothing
2. **`markCustom` belongs to the state, not to a pad rectangle** (06-03). Every writer currently has to
   remember `handle->dirty = true`
3. **A `StateFollower` split** (06-02). The side panel needs the edge-detect and has no pads — and the
   follower is where both twice-fixed bugs lived
4. **`PatternRow::dimmed`** (06-04), so per-row state lives with the row table
5. **Rows as a constructor argument** (06-04), removing an undocumented ordering rule
6. **The ChassisRig** (06-04) — still last, but 06-01 named one of its two missing verbs: all three new
   tests hand-sequence "poll every view once"

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| The render baseline was truncated by my own `head -12` | Rebuilt from the pre-plan commit with `git checkout <sha> -- src tests CMakeLists.txt`, so the comparison covered all 20 rather than 13 |
| The concurrent-write detector misses a narrow re-read | It catches the real 05-03 shape at 113/1500 trials because that leaves a 160-pad paint to land in; a re-read immediately after the lock is a nanosecond window no affordable trial count reaches. Written into the test rather than left implied — the ordering is held by `snapshotPattern` being the only way to take a snapshot |
| Nothing tested what a rebuild must restore | Removing the rebuild callback left every pad at 0×0 and the suite stayed green. `testARebuildRestoresWhatItReplaced` now asserts bounds, dimming and z-order |

## Next Phase Readiness

**Ready:** 06-02 can build its views on `PatternPads` rather than making a third copy, and the shared
laws are reachable without including a 350-line component header.

**Concerns:** deferred item 1 lands squarely in 06-02's path — the `CUSTOM` tag has nothing to follow
until the revision counter covers more than `lanes`. It should be planned there, not deferred again.
