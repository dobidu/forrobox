---
phase: 11-validation
plan: 07
subsystem: state
tags: [setStateInformation, restore, CUSTOM, dirty, groove, kNoGroove]

requires:
  - phase: 08-polish
    provides: "08-01's fresh-instance and restore semantics; the CUSTOM tag following dirty"
provides:
  - "a restore with no grid (no <STATE>, or <STATE> without <GRID>): params applied, empty grid, dirty → CUSTOM, State::kNoGroove → no groove name, either arrow loads groove 01"
  - "the missing guard: a clean blob restores clean"
affects: [Phase 12 settings restructure (the state contract), any future reader of activeGroove]

key-files:
  modified: [src/PluginProcessor.cpp, src/ForroBoxState.h, tests/StateRoundTripTest.cpp]

key-decisions:
  - "Params applied + empty grid + CUSTOM (user, Phase 11 planning)"
  - "No groove name, and the arrows load groove 01 (user, at 11-07's review)"
  - "Keyed on NO GRID READ, not on the missing node: one rule covers both shapes"
  - "In setStateInformation, not State::readFrom: the restore's product decision, not the deserialiser's contract"

duration: ~1 session
completed: 2026-10-01
description: "A restore that carried no grid tells the truth: its parameters, an empty grid, CUSTOM, and no groove name; a clean blob still restores clean"
type: Summary
about: "Forró Box"
---

# Phase 11 Plan 07: The blob with no grid

**A Forró Box blob that carries no grid no longer claims an unedited CAMPINA over silence.** This covers a missing `<STATE>` and also a `<STATE>` without its `<GRID>`. The blob's parameters are applied; the grid comes back empty; the side panel shows CUSTOM; the preset screen names no groove; and either cycler arrow loads groove 01. The state survives a save and reload.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: params applied, grid empty, CUSTOM | **Pass** | Donor bpm 97 → restored 97; 0 notes; `dirty`; default profile name. Plus the UI predicates: `profileSelection().index == -1` and an empty `activeGrooveName()` |
| AC-2: every other path unchanged | **Pass** | **The guard was missing:** a mutation marking every restore dirty passed the whole suite. Added "a clean project restores CLEAN". All five mutations are now rejected (every-restore dirty, no-groove unset, cycler at 0, trigger on `<STATE>` only, the readout naming the sentinel) |
| AC-3: green everywhere | **Pass** | Gate 3/3; 5018/5018 on GCC 13, Clang 18 and MSVC 2022 (5020 in the Debug suite); Windows pluginval PASS |

## /code-review (required): 8 findings, 7 fixed, 1 recorded

1. **The header still named the default groove over the empty grid.** Fixed with `State::kNoGroove`, a reserved id distinct from EMPTY, which already means a pre-09-06 project. The readout is blank, as the user chose.
2. **`<STATE>` without `<GRID>`** was the same claim one level down. The trigger is now "no grid read".
3. **The left arrow did nothing** in that state. With no groove the index is −1, so either arrow lands on groove 01.
4. **JUCE's generic `"PARAMETERS"` tag lets another plugin's chunk through.** This is pre-existing and not made worse. Recorded in Deferred Issues, and the comment now says so honestly.
5. Asserting the raw flag only; the UI predicates are now asserted too.
6. Persistence across a save and reload, and the no-`<GRID>` case: both now tested.
7. "Truncated" in the comment was wrong: a truncated blob returns early. Corrected.
8. The lookup is hoisted, so the node is looked up once.

## /simplify at Phase 11 close (required): four agents

**Applied:**
- **validate-plugin.sh:**
  - `judge()` is now the single place for the rules, parameterised by kind and failed-test pattern. The Debug-suite verdict had re-implemented its greps.
  - The Debug tree builds the VST3 and the suite in one ninja pass.
  - It reuses `build-windows.sh`'s already-resolved Windows profile instead of spawning `cmd.exe` again, keeping the `*:*` check when run alone.
  - `GUI_ARGS` is an array, and a recomputed path is gone.
- **Processor:**
  - `restartClean()` (one helper for the un-bypass restart);
  - `currentHostPosition()` (the two playhead ternaries);
  - the redundant clear in `prepareToPlay` removed, with the flag's doc corrected.
- **`VoiceEngine::reset()` calls `silence()`**, so the voice-clearing code is in one place.
- **Tests:**
  - `testBypass` reuses `bufferPeak`, `checkSilent` and `maxDifference`;
  - a shared `countStillSounding` replaces two copies;
  - `noteCount` moved above its first use and now serves both 11-07 cases;
  - `ExpectAssertions` has one UTF-8 constructor and one scope (nesting reported as a failing check, since nothing nests).
- **`launchExternalDragForTest`:** renamed, so a public `std::function` on a production component reads as test-only where it is called.

**Re-proved:** the restart-emptied mutation fails all three restart checks; the removed-assert mutation gives "expected 1, saw 0"; gate 3/3; MSVC + Windows pluginval PASS.

**The gates caught one of my own edits:** `verify-charset` flagged a non-ASCII literal passed into `juce::String` in the new `ExpectAssertions` message. It is now `fromUTF8`.

**Skipped:**
- **Trimming the long "why" comments:** recording the finding and the review beside the code is this codebase's convention throughout, so trimming would make these the exceptions.
- **The `testBypass` scenario-helper refactor:** about 80 lines, but each scenario's setup differs in what it proves.

**Deferred to STATE.md:**
- a typed accessor for the groove kind (`activeGroove` now has four meanings in one string);
- a single sample-rate guard in `prepareToPlay` (the MixBus `jmin` is local);
- an `AudioProcessor::reset()` override through `restartClean()`, once it is verified which thread each wrapper calls it on.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Scope addition | 1 | `kNoGroove` + the cycler, the user's choice at review |
| Trigger widened | 1 | No grid read, not no node (review finding 2) |
| Guard added | 1 | The clean-restore check (a surviving mutation) |

---
*Phase: 11-validation, Plan: 07 · Completed: 2026-10-01*
