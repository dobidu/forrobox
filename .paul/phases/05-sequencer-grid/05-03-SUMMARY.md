---
phase: 05-sequencer-grid
plan: 03
subsystem: ui
tags: [juce, vst3, apvts, state, tiling, polling]

requires:
  - phase: 02-sequencer-clock
    provides: the 32-slot storage model with `steps` as a view (expandPattern)
  - phase: 05-sequencer-grid
    provides: 05-01's grid and its rebuild path; 05-02's 60 Hz poll
provides:
  - The grid follows every writer of the pattern, not only its own clicks
  - STEPS 16/32 buttons, wired to ids::steps
  - State::tileToFullWidth — PLANNING.md:606's tiling law
  - ChoiceButtonsAttachment — N buttons bound to one choice parameter
  - currentStepWindow() — one reader for the active window
affects: [05-04 overlay and dimming, 06 profile loading]

tech-stack:
  added: []
  patterns:
    - "Detect an edge where it is consumed, rather than delivering it"
    - "A restore must not be indistinguishable from a user gesture"
    - "Record rights, not only provenance, before anything is published"

key-files:
  created:
    - src/ChoiceButtonsAttachment.h
    - src/ChoiceButtonsAttachment.cpp
  modified:
    - src/PluginProcessor.h
    - src/PluginProcessor.cpp
    - src/SequencerGrid.h
    - src/SequencerGrid.cpp
    - src/ForroBoxState.h
    - src/ForroBoxState.cpp
    - src/ScopedControlCallbacks.h
    - src/PatternSnapshot.h
    - src/Profiles.cpp

key-decisions:
  - "Tiling is the processor's, so automation behaves the same with no editor open"
  - "Narrowing the window tiles nothing — storage is 32 slots with `steps` as a view"
  - "The grid follows publishIfChanged's existing counter rather than a new signal"
  - "The edge is detected by the 15 Hz drain, not delivered to it by a listener"

patterns-established:
  - "A helper that cannot be written without its check removes the silent-skip shape"
  - "Assert control identity by index, never by reverse-engineering it from geometry"

duration: ~4h
completed: 2026-09-15
description: "The grid answers writers other than itself — STEPS 16/32 with tiling, and refresh on host recall and steps automation"
type: Summary
about: "Forró Box"
---

# Phase 5 Plan 03: The Grid Answers Other Writers — Summary

**The sequencer grid repaints for every writer of the pattern rather than only its own clicks, and
the STEPS 16/32 buttons are live with the tiling law `PLANNING.md:606` fixes.**

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: The grid shows what the pattern IS, whoever wrote it | **Pass** | Hangs off `publishIfChanged`'s existing counter — the seam `~LockedState` already routes every writer through, so Phase 6's profile load needs no new call |
| AC-2: A step-count change rebuilds the grid, from any source | **Pass** | Rebuild, not refresh: the pad COUNT changes. Driven by parameter, not by click |
| AC-3: Switching to 32 tiles, and does so with the editor closed | **Pass** | Proved by a test that constructs **no editor at all** — a UI-owned implementation cannot pass it |
| AC-4: The STEPS buttons are the prototype's, and follow the parameter | **Pass** | `Variant::base`, because `.steps-btn` has no CSS rule of its own. Lit state from the parameter, never the click |
| AC-5: Nothing regressed on three compilers | **Pass** | 3313/3313 on GCC, Clang and MSVC with `DISPLAY` unset. Three verify scripts green. VST3 installed, hashes match |

## Skill Audit

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/graphify` | ✓ | At planning; gave `setSteps → renderPads`, which is why a step change rebuilds rather than refreshes |
| `/code-review` | ✓ | After Task 2, as the plan gated. **Six findings, one of them data loss** |
| `/simplify` | ✓ | During UNIFY. Four agents; the efficiency pass measured rather than reasoned |
| `/impeccable` | — | Optional, not invoked |

All required skills invoked.

## Task Commits

| Task | Commit | Type |
|------|--------|------|
| Task 1: the grid follows every writer | `f8bb0ae` | feat |
| Task 2: STEPS buttons + tiling | `c28311a` | feat |
| `/code-review`'s six findings | `21ed60c` | fix |
| `/simplify` — structure | `d3def7a` | refactor |
| `/simplify` — the timer's cost | `2860a76` | perf |
| Licensing before publication | `0df49d6` | docs |

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| Tiling is the processor's, not the UI's | The prototype has one path to a step change; a plugin has two, and host automation can arrive with no editor open. A UI-owned tiling would make the same automation produce a different groove depending on whether a window happened to be open |
| Narrowing tiles nothing | Storage is always 32 slots with `steps` as a view (02-01). Narrowing stops reading the upper half; widening tiles over it, so nothing that survives is observable. Truncating would destroy work for no reachable benefit |
| The grid polls `publishIfChanged`'s counter | It already increments exactly when the lanes differ, and not on the reads the handle is also taken for. A second counter would have been duplication; a broadcaster would have to fire under `stateLock` |
| The 15 Hz drain detects the edge itself | A listener existed to deliver an edge to a timer that already ran and already held the baseline. Deleting it removed the plugin's only audio-thread work outside `processBlock` |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 14 | 6 from `/code-review`, 8 from `/simplify` |
| Plan corrections | 2 | Task 1's mechanism, and the geometry-reader instruction |
| Deferred | 1 | The ChassisRig, now flagged three plans running |

### The plan was wrong twice, and reading the code first caught both

**Task 1 specified a new state generation bumped in `~LockedState`.** That would have been wrong
twice over: the handle is taken for READ access too — which is why `publishIfChanged` exists rather
than an unconditional publish — so it would have fired 60×/second from the grid's own poll. And the
counter already existed. No new mechanism was built.

**The plan told me to add the STEPS buttons' lengths to `verify-geometry.py`.** Checking first, as
it also instructed: `.btn`'s padding is policed from 04-03, `seq::kStepsGap` from 05-01, and the new
header declares no constants. Nothing to add — which is the point, after 05-02 shipped a header with
nine constants policed by nothing.

### Auto-fixed — `/code-review` (6)

**A reload was destroying the second bar.** `apvts.replaceState()` fires `parameterChanged`
synchronously for every restored parameter, so a project saved at 32 steps looked identical to
somebody switching 16→32 — the tiling fired and overwrote the restored upper half with a copy of the
lower. **My round-trip test could not see it**: it never drained the pending update, and the suite
has no message loop, so it was asserting against work only a real host performs. One added line made
it fail immediately.

**`triggerAsyncUpdate` is not audio-thread safe** — JUCE's header says so verbatim. Replaced with a
wait-free flag. Also: the generation was recorded after the lock was released (re-introducing 05-01's
bug in a narrower window); a second `attachParameters` left the buttons lighting but unclickable; a
`static_assert` was `32 % 2 == 0` under a message about windows it never checked; and a null button
compacted the guards vector, shifting every later choice.

### Auto-fixed — `/simplify` (8)

**The listener should not have existed.** It delivered an edge to a timer that already ran and
already held the baseline. Deleting it removed two base classes, an atomic flag, two static_asserts,
the registration and a destructor — **and the plugin's only audio-thread work outside `processBlock`**,
making the whole `triggerAsyncUpdate` argument moot rather than won. 133 lines out, 76 in.

Also: "read the current step window" was written four times with a divergent fallback;
`lastStepCountSeen` shadowed `stepCount`; a nullable constructor had no caller that could reach it;
`litLabel` returned `bounds == steps16 ? 16 : 32` and so **could not fail** for a misplaced button;
one of six step-window blocks guarded **without a check**; `publicationCount`'s doc still said
"Diagnostic" at its definition while its new contract lived only in the consumer's header; and
`kPatternLength` was not tied to `stepWindows.front()`.

### The efficiency pass corrected a claim I had made

I wrote that deleting the listener removed the plugin's only audio-thread work and implied a safety
win. **Measured, the listener was free** — 75 ns, zero allocations. `LockedListeners::call` takes its
lock unconditionally whether or not anyone is registered, and `parameterValueChanged` early-outs on
`approximatelyEqual`, so a held automation value never reached it. What *was* unsafe was the original
`triggerAsyncUpdate`. Deleting the listener was a simplicity win, not a performance one — recorded so
a future reader does not learn the wrong lesson.

**The one real cost was mine:** the 30 Hz timer at 0.27% of a core per instance, forever — and APVTS's
own timer backs off to 500 ms when idle, so I had pinned the wake rate 15× higher. Now 15 Hz and
scoped to `prepareToPlay`/`releaseResources`.

### Deferred

- **The ChassisRig.** 22 verbatim sites, flagged by `/simplify` at 05-01, 05-02 **and** 05-03. One
  site carries a comment about a declaration order that crashes MSVC if reversed — a rig makes that
  unrepresentable. Three flags is a signal; recorded in PROJECT.md rather than left to become
  permanent by default

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| MSVC killed for low memory during the TEST run | Retried at `FORROBOX_MSVC_JOBS=2`. Measured the suite at 56 MB peak, so the kill was environmental, not the new 1500-trial test |
| The concurrent-write detector took four attempts | 0/10 single-threaded → 1/10 final-value-only → **0/10 after restructuring** (one write per trial leaves nothing to land inside the first one's refresh) → 1/10 bursting → 7/10 once each write repainted → 10/10 at 1500 trials, suite still 2.70 s |

## Next Phase Readiness

**Ready:** the grid follows every writer, so Phase 6's profile load will repaint it with no new call.
`currentStepWindow()` and `isTransportStopped()` are the single readers for their facts.

**Concerns:** the ChassisRig is three plans overdue. 05-04 still owns the overlay, isolate, dimming
and CUSTOM tag.

**Blockers:** None.

---
*Built with PAUL Framework v1.4*
*Phase: 05-sequencer-grid, Plan: 03*
*Completed: 2026-09-15*
