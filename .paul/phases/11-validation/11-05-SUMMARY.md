---
phase: 11-validation
plan: 05
subsystem: testing
tags: [jassert, JUCE_LOG_ASSERTIONS, debug, test-harness, gate, mixbus, focus]

requires:
  - phase: 11-validation
    provides: "11-01's gate script, extended here with a third verdict"
provides:
  - "FORROBOX_LOG_ASSERTIONS (build-debug only): every jassert reaches juce::Logger"
  - "fbtest::AssertionCounter + ExpectAssertions (count, file, why): an unexpected assertion is a failing check"
  - "the gate's third verdict: the whole suite in Debug, judged by exit code AND log (teardown assertions, leaks)"
  - "six hidden assertion sites closed, two of them production"
  - "forrobox::grabFocusIfVisible (src/Focus.h)"
affects: [11-06 (its reproduction is a Debug-suite check), every later plan: a jassert can no longer pass unseen]

key-files:
  created: [src/Focus.h]
  modified: [CMakeLists.txt, scripts/validate-plugin.sh, tests/TestHarness.h, tests/TestMain.cpp, tests/StateRoundTripTest.cpp, tests/VoiceTest.cpp, tests/UiTest.cpp, src/MixBus.cpp, src/AboutOverlay.cpp, src/Knob.cpp, src/BpmField.cpp]

key-decisions:
  - "The define is PUBLIC on ForroBox, not test-only: the tests link the shared-code library, so only that reaches src/ assertions and keeps every TU consistent"
  - "Expected assertions are matched by FILE, never just counted"
  - "MixBus: the 20 Hz floor yields to Nyquist (bit-identical at shipping rates)"

duration: ~1 session
completed: 2026-10-01
description: "A JUCE assertion can no longer pass unseen: the suite runs in Debug as a gate verdict and fails on any unexpected one; the 3489 that Release had always hidden, at 6 sites, are closed"
type: Summary
about: "Forró Box"
---

# Phase 11 Plan 05: The suite under Debug

**The gate now runs the whole suite in Debug, and an unexpected JUCE assertion fails it.** It landed red on exactly the 6 sites (3489 assertions) measured at planning. All six are closed. The gate is green on all three verdicts: pluginval Debug, pluginval Release, and the Debug suite (0 unexpected, 1 expected).

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: an unexpected assertion is a failing check | **Pass** | Each site gets a FAIL line with `file:line ×N, first in [section]`. `ExpectAssertions (n, file, why)` requires exactly n at that file: the toy proof showed one expected passes, expected-but-none FAILS and none-but-one FAILS. Release counts are unchanged (5001 on GCC, Clang and MSVC) |
| AC-2: the gate runs it | **Pass** | Third verdict `debug suite: linux-debug`. Before the fixes: FAIL listing exactly the six sites with the planning counts |
| AC-3: test-side fixed, not silenced | **Pass** | The rig prepares at its render size (`SyncedProcessor` block-size parameter); the bare knob renders at the labelled height; 19 `fbtest::utf8` routes; the expected assertion declared against `PluginProcessor.cpp` |
| AC-4: production fixed | **Pass** | MixBus: `jlimit (jmin (20, cap), cap, cutoff)`. Focus: `grabFocusIfVisible` at all three sites |
| AC-5: zero unexpected, everywhere green | **Pass** | Debug suite 0/1. Both pluginval verdicts PASS. 5001/5001 on GCC 13, Clang 18 and MSVC 2022, and Windows pluginval PASS. Reverting each fix makes the gate name its site (6 of 6) |

## What Release had been hiding

| Site | ×  | Cause | Kind |
|------|----|-------|------|
| VoiceEngine.cpp:684 | 2933 | `SyncedProcessor` prepared 256 while 4 tests rendered 512 | test |
| UiTest.cpp:150 | 486 | **A check that could not fail:** "an unlabelled knob's same region is empty" read the label row from a bare render *without one*. Every pixel was out of bounds and returned black. Now a real comparison, and it still passes | test |
| juce_String.cpp:327 | 39 | UTF-8 `const char*` into `juce::String` (19 sites); those messages would also print as mojibake on failure | test |
| juce_MathsFunctions.h:524 | 24 | MixBus `jlimit (20, 0.49 x rate, ...)` inverts its limits below ~41 Hz | **production** |
| juce_Component.cpp:2752 | 6 | Focus grabbed while not showing (AboutOverlay, Knob, BpmField) | **production** |
| PluginProcessor.cpp:795 | 1 | `stepsForChoiceIndex (7)`, a deliberate test of the fallback | expected |

## /code-review (required: MixBus is audio-thread code): 10 findings, all fixed

1. **The gate could never print PASSED.** Under `pipefail`, the FAIL-line `grep` exited 1 on a clean run. Every grep now tolerates finding nothing, including SIGPIPE through `head`.
2. **Teardown assertions and leak reports escaped the exit code.** The Debug-suite verdict now also applies pluginval's log rules (assertion lines, leak lines). Proven by a mutation: a `jassertfalse` after the report → rc 0, but the log line → FAIL.
3. `--render-audition` now calls `reportAssertions`.
4. `ExpectAssertions` is **site-specific**: matched by file name, with any other assertion still unexpected. Proven: a wrong expected site → both a "saw 0" FAIL and an unexpected FAIL.
5. Nested scopes no longer double-count (innermost matching scope only).
6. The gate no longer forces `-G Ninja` on an existing `build-debug`, and accepts every CMake truthy spelling.
7. **The absurd-rate MixBus check cannot fail in Release.** The old `jlimit` happened to return the upper limit, so output was finite before the fix too, and the timbre doesn't matter. Its comment and message now say so: the Debug assertion counter is the guard.
8. The logger is installed *before* the JUCE initialiser and removed *after* it, so JUCE's own start-up and shutdown are counted and the timer thread has stopped before the static is reset.
9. Re-entrancy: the lock covers counting only, and an assertion raised while logging goes straight to stderr instead of deadlocking.
10. The three pasted focus guards became one `forrobox::grabFocusIfVisible`, with JUCE's full condition `isShowing() || isOnDesktop()`.

**The review also confirmed:** the MixBus change is bit-identical at every shipping rate; Release behaviour is unchanged; definitions are consistent across targets; and the focus guards change no behaviour (JUCE's internal grab already returned early).

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| New file | 1 | `src/Focus.h`, the shared helper from review finding 10 |
| API shape | 1 | `ExpectAssertions` takes a file name (the plan had count + why). Review finding 4 |

## Next Phase Readiness

**Ready:** 11-06. Its reproduction is now a check: an off-thread construction inside the Debug suite would be an unexpected `PluginProcessor.cpp:238`.

**Concerns:** No MSVC Debug run; Linux Debug carries the assertion verdict, by plan. **Blockers:** none.

---
*Phase: 11-validation, Plan: 05 · Completed: 2026-10-01*
