---
phase: 11-validation
plan: 04
subsystem: ui
tags: [fonts, typeface, leak, DeletedAtShutdown, pluginval]

requires:
  - phase: 11-validation
    provides: "11-01's gate (the leak's measurement); 11-03 (the gate's other finding, already closed)"
provides:
  - "EmbeddedTypefaces: the font cache as a DeletedAtShutdown singleton, freed by shutdownJuce_GUI"
  - "the pluginval gate GREEN on Linux Debug, Linux Release and Windows Release"
affects: [every later plan's regression check: the gate is now a real pass/fail]

key-files:
  modified: [src/Typography.cpp]

key-decisions:
  - "DeletedAtShutdown, JUCE's own idiom for a process-wide GUI cache, over a static (dies after the leak counter) or a SharedResourcePointer (rebuilds every face per last-editor close)"

duration: ~30 min
completed: 2026-10-01
description: "The font cache is freed by JUCE's shutdown instead of outliving its leak counter, and pluginval now passes on all three targets"
type: Summary
about: "Forró Box"
---

# Phase 11 Plan 04: The Typeface leak

**pluginval passes the VST3 on all three targets: Linux Debug, Linux Release and Windows Release, at strictness 10 with GUI tests on.** That makes Phase 11's headline true. The cause was the order statics are destroyed in, and the fix is JUCE's own idiom.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: freed by JUCE's shutdown, rebuilt after | **Pass** | `EmbeddedTypefaces : private DeletedAtShutdown`, `JUCE_DECLARE_SINGLETON_SINGLETHREADED_INLINE`, `clearSingletonInstance()` in the destructor. `getInstance()` rebuilds after a `deleteAll`. The keying, asserts and tables are byte-identical, and UiTest's typeface identity and distinctness checks pass |
| AC-2: the gate green everywhere | **Pass** | Linux Debug PASS, Linux Release PASS, Windows Release PASS (via `build-windows.sh`). **Reverse:** restoring the function-local static → Debug FAIL on exactly 6 Typeface, 6 FTFaceWrapper and 1 FTLibWrapper. Reverted → green |
| AC-3: nothing drawn changes | **Pass** | 4999/4999 on GCC 13, Clang 18 and MSVC 2022, pixel renders and type-scale checks included. No warnings from our sources |

## The mechanism (read at planning, proven by the reverse run)

The cache was a function-local `static`, constructed when `typefaceFor` was entered. That is *before* the first `Typeface` is made. `Typeface`'s `JUCE_LEAK_DETECTOR` counter is a static constructed inside that first `createSystemTypefaceFor`. Statics are destroyed in reverse order, so at unload the counter died first and reported every face the cache still held. FreeType's wrappers are owned by those faces, so they were reported too.

The leak appeared only with GUI tests on, because headless pluginval never paints.

## Files Modified

| File | Change |
|------|--------|
| `src/Typography.cpp` | `EmbeddedTypefaces` singleton with its lifetime reasoning; `typefaceFor` reads its slots |

## Deviations
None. The plan was executed as written.

## Skill audit
No required skill was triggered: a message-thread UI cache, with no processor or audio-thread code.

## Next Phase Readiness
**Ready:** 11-05 (`loadProfile` on a loader thread) and 11-06 (the no-`<STATE>` blob). Both now run under a gate that is green. **Concerns:** none. **Blockers:** none.

---
*Phase: 11-validation, Plan: 04 · Completed: 2026-10-01*
