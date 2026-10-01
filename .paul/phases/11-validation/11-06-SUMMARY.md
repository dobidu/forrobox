---
phase: 11-validation
plan: 06
subsystem: audio-processor
tags: [threading, loadProfile, constructor, Timer, destructor, VST3-factory]

requires:
  - phase: 11-validation
    provides: "11-05's Debug-suite counter: the only place this fix is observable"
provides:
  - "loadProfile (public, JUCE_ASSERT_MESSAGE_THREAD) / loadProfileUnchecked (private, constructor only)"
  - "~ForroBoxAudioProcessor stops the step-tiling timer before members die"
  - "a loader-thread construction test: prepared, destroyed off-thread, every parameter compared"
  - "ExpectAssertions const char* overload (UTF-8)"
affects: [11-07, Phase 13 multi-instance (construction/destruction threading)]

key-files:
  modified: [src/PluginProcessor.h, src/PluginProcessor.cpp, tests/StateRoundTripTest.cpp, tests/TestHarness.h]

key-decisions:
  - "Split, not an `|| constructing` flag: the guarantee stays on every post-construction caller with no second state to keep true"
  - "Named loadProfileUnchecked, not the planned applyProfile: forrobox::applyProfile (State&, Profile) already exists and the body calls it"

duration: ~1 session
completed: 2026-10-01
description: "A host may build Forró Box on a loader thread and destroy it there: no assertion, the same instance as on the message thread, and the timer stopped before teardown. The message-thread guarantee stays on every later caller"
type: Summary
about: "Forró Box"
---

# Phase 11 Plan 06: loadProfile on a host loader thread

**A host that constructs Forró Box on a loader thread now gets exactly the instance the message thread gets, with no assertion.** A host that prepares it and destroys it there no longer leaves the step-tiling timer firing into destroyed members. `loadProfile`'s thread guarantee stays on both UI callers.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: off-thread construction clean and correct | **Pass** | On a `std::thread`: the grid matches the default profile in all 8×32 slots, and **every** APVTS parameter equals a message-thread instance's. Then `prepareToPlay` and destruction there. Debug suite: 0 unexpected assertions |
| AC-2: the guarantee stays | **Pass** | `ExpectAssertions (1, "PluginProcessor.cpp", ...)` around an off-thread `loadProfile`. Mutations: constructor → `loadProfile` gives an unexpected `PluginProcessor.cpp:241`; assert removed gives "expected 1, saw 0" |
| AC-3: nothing else changes | **Pass** | Gate 3/3 green (0 unexpected, 2 expected); 5006/5006 on GCC 13, Clang 18 and MSVC 2022; Windows pluginval PASS |

## /code-review (required): 9 findings, all fixed

1. **The destructor never stopped the Timer** (pre-existing, made reachable by off-thread hosts). Members die before the `Timer` base class, so after `prepareToPlay` with no `releaseResources`, a callback could read a dead APVTS and lock a dead `stateLock`. Now `~ForroBoxAudioProcessor() { stopTimer(); }`, as JUCE's own `Timer::~Timer` comment prescribes. **Proven:** without it, the Debug suite fails on `juce_Timer.cpp:365`.
2. `loadProfileUnchecked` was declared in a `public:` section. It is now private, so the rule is the compiler's.
3. The safety claim "nothing else can reach this object yet" was false: the APVTS member runs its own 10 Hz flush timer. The comment now gives the real reason (atomic parameter writes, and the publisher's SpinLock).
4. The test destroyed an *unprepared* instance. It now prepares first, which is what exercises finding 1.
5. Nothing tested that `loadProfile` still asserts. Added (`ExpectAssertions`).
6. The test now says its assertion half is observable only in the logging tree.
7. The test checked only BATERIA's mute. It now compares every parameter.
8. The comment "publisher written only from the message thread" was stale; corrected.
9. "`loadProfile` and nothing new" was stale, and there was a stray blank line; both fixed.

## Caught by 11-05's instrument, in this plan's own code
Two `"… — …" + juce::String` messages, the exact class 11-05 cleared. One was in a `check`, and one was `ExpectAssertions`'s own reason, which a `juce::String` parameter read as Latin-1. The second led to the `const char*` overload in the harness, mirroring `check()`'s.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Name | 1 | `loadProfileUnchecked`, not `applyProfile` (the name was taken) |
| Scope addition | 1 | The destructor `stopTimer()`, review finding 1. A real use-after-free path, proven by JUCE's assertion |
| Harness | 1 | The `ExpectAssertions` UTF-8 overload |

## Next Phase Readiness
**Ready:** 11-07, the no-`<STATE>` blob. It is Phase 11's last plan, so `/simplify` is required at its UNIFY. **Blockers:** none.

---
*Phase: 11-validation, Plan: 06 · Completed: 2026-10-01*
