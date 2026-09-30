---
phase: 11-validation
plan: 02
subsystem: testing
tags: [msvc, windows, drag-and-drop, ole, hang, test-seam]

requires:
  - phase: 11-validation
    provides: "11-01's trace of the hang, and the Windows pluginval step this plan unblocks"
provides:
  - "DragMidiButton::launchExternalDrag: a test seam whose default is the one real OS drag in the project"
  - "the MSVC suite exits by itself again; build-windows.sh green end to end, pluginval included"
  - "the drag-export test checks the hand-off, the completion callback and the refusal path"
affects: [every later MSVC run, 11-03..11-06 regression checks]

key-files:
  modified: [src/DragMidiButton.h, src/DragMidiButton.cpp, tests/UiTest.cpp]

key-decisions:
  - "A hook for the one OS call, not a general 'no real OS calls in tests' framework"
  - "false/this stay in the default branch: facts about the button, not choices for a caller"

duration: ~1 session
completed: 2026-09-30
description: "The MSVC post-main hang is gone: tests hand the drag to a fake, so JUCE's drag pool, which waits forever at shutdown, is never created. Cure proven structurally; the reverse mutation did not reproduce"
type: Summary
about: "Forró Box"
---

# Phase 11 Plan 02: The drag-pool hang

**The MSVC suite exits on its own again: 4 runs out of 4 since the fix, each reaching Windows pluginval PASS.** Tests no longer start a real OLE drag, so JUCE's drag pool is never created, and that pool is the only thing whose shutdown waits forever.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: production behaviour unchanged | **Pass** | Unset hook → `performExternalDragDropOfFiles (files, false, this, onFinished)`, the same arguments and completion lambda as before. The gesture latch, threshold, refusal branch and mouseUp suppression are untouched. `grep`: one real call in the project (`DragMidiButton.cpp:370`) |
| AC-2: no real OS drag in tests, more coverage | **Pass** | The fake is called once, with exactly the file the drag wrote. The completion callback clears the drag and press state. A refusing fake leaves the button out of its drag state, released off the button so no FileChooser opens. 4974 → 4979 checks on GCC 13 and Clang 18. Mutations rejected: wrong path, callback not clearing, refusal branch removed |
| AC-3: hang gone, and the fix is what cured it | **Partial** | **Gone:** 4 of 4 MSVC runs exit 0 with `juceInit destroyed (shutdownJuce_GUI returned)`, 4979/4979, and Windows pluginval PASS. **Cause of the cure:** proven *structurally*, not by reproduction; see below |

## The concern: necessary, not sufficient

**Timeline:**

| When | Build | Hung |
|------|-------|------|
| This morning | Real drag | 4 of 4 |
| At planning | That one gesture removed | 0 of 3 |
| This afternoon | Real drag restored (the reverse mutation) | **0 of 4** |

**So the drag is necessary but not sufficient.** Every hang observed had main blocked in `ThreadPoolHolder`, which exists only after a drag. But a drag doesn't always hang. The second factor is environmental and still unidentified. It's consistent with 10-01's 0 of 13 and with this morning's 4 of 4, and a simulated cursor move didn't release it.

**Why the fix still holds.** `DragAndDropHelpers::ThreadPoolHolder` is instantiated only by `DragAndDropContainer::performExternalDragDropOfFiles` (`juce_DragAndDrop_windows.cpp:323`/`:334`) and `performExternalDragDropOfText` (`:343`/`:365`). The project calls the first in exactly one place, the hook's default branch, and never calls the second. With the fake installed, the singleton is never created, so its wait-forever destructor never runs. That holds whatever the environmental factor turns out to be.

**What stays open:** the watchdog. If the hang recurs with the pool absent, the exit probe will name a different frame.

## Found on the way: a latent test weakness

The drag test called `written.getLast()` "the newest" export. Exports go into per-instance folders under `/tmp/forrobox/` and are found by a recursive walk in no useful order, so it was often a previous run's file. The name and MThd checks passed on any export, so the old check couldn't tell. The new hand-off check exposed it on the first run. It's now a before/after file set: exactly one new file, and the checks read that file.

## Files Modified

| File | Change |
|------|--------|
| `src/DragMidiButton.h` | `launchExternalDrag` hook, with the hang recorded in its doc; `isDragging()` accessor |
| `src/DragMidiButton.cpp` | `mouseDrag` calls the hook when it's set, and otherwise today's call |
| `tests/UiTest.cpp` | Fake launcher; hand-off, callback and refusal checks; created files compared by set |

## Deviations

| Type | Count | Impact |
|------|-------|--------|
| AC partially met | 1 | AC-3's reverse mutation didn't reproduce (0/4). The cure is proven structurally instead |
| Auto-fixed | 1 | The latent "newest file" weakness, which was needed for AC-2's path check to be meaningful |
| Accessor added | 1 | `isDragging()`, which the plan allowed |

## Skill audit
No required skill was triggered: a UI component and a test, with no audio-thread or processor code. SPECIAL-FLOWS' `/code-review` applies to processor work.

## Next Phase Readiness
**Ready:** 11-03 (bypass latency) and every later plan now have a green MSVC run with pluginval. **Concern:** the unidentified second factor, now harmless to the suite. **Blockers:** none.

---
*Phase: 11-validation, Plan: 02 · Completed: 2026-09-30*
